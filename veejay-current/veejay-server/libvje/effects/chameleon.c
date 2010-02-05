/* 
 * Linux VeeJay
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * ChameleonTV - Vanishing into the wall!!
 * Copyright (C) 2003 FUKUCHI Kentaro
 *
 * Ported to veejay by Niels Elburg 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include <config.h>
#include "chameleon.h"
#include "common.h"

vj_effect *chameleon_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->defaults[0] = 0;
    ve->description = "ChameleonTV (EffectTV)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Appearing/Dissapearing");
     return ve;
}

static int last_mode_  = -1;
static int N__ = 0;
static int n__ = 0;

static	int	has_bg = 0;
static int32_t	*sum = NULL;
static uint8_t		*timebuffer = NULL;
static uint8_t		*tmpimage[3] = { NULL,NULL,NULL};
static	int	plane = 0;
static uint8_t		*bgimage[3] = { NULL,NULL,NULL};

#define PLANES_DEPTH 6
#define	PLANES (1<< PLANES_DEPTH)

int	chameleon_prepare( uint8_t *map[3], int width, int height )
{
	if(!bgimage[0])
		return 0;

		//@ copy the iamge
	veejay_memcpy( bgimage[0], map[0], (width*height));
	veejay_memcpy( bgimage[1], map[1], (width*height));
	veejay_memcpy( bgimage[2], map[2], (width*height));
	
	VJFrame tmp;
	veejay_memset( &tmp, 0, sizeof(VJFrame));
	tmp.data[0] = bgimage[0];
	tmp.width = width;
	tmp.height = height;

	//@ 3x3 blur
	softblur_apply( &tmp, width,height,0);

	veejay_msg(2, "ChameleonTV: Snapped background frame");
	return 1;
}

int	chameleon_malloc(int w, int h)
{
	if( bgimage[0] )
		free(bgimage[0]);
	bgimage[0] = (uint8_t*)vj_yuvalloc(w,h);
	if(!bgimage[0])
		return 0;
	bgimage[1] = bgimage[0] + (w*h);
	bgimage[2] = bgimage[1] + (w*h);
	if(tmpimage[0])
		free(tmpimage[0]);
	tmpimage[0] = (uint8_t*)vj_yuvalloc(w,h);
	if(!tmpimage[0])
		return 0;
	tmpimage[1] = tmpimage[0] + (w*h);
	tmpimage[2] = tmpimage[1] + (w*h);

	if( sum )
		free(sum);
	sum = (int32_t*) vj_calloc( w * h * sizeof(int32_t));
	if( timebuffer )
		free(timebuffer);
	timebuffer = (uint8_t*) vj_calloc( w* h * PLANES );

	has_bg = 0;
	plane = 0;
	N__ = 0;
	n__ = 0;
	last_mode_ = -1;

	return 1;
}

void	chameleon_free()
{
	if( bgimage[0]) free(bgimage[0]);
	if( tmpimage[0]) free(tmpimage[0]);
	if( timebuffer ) free(timebuffer);
	if( sum ) free(sum);
	bgimage[0] = NULL;
	tmpimage[0] = NULL;
	timebuffer = NULL;
	sum = NULL;
	has_bg = 0;
	plane = 0;
}

static void drawAppearing(VJFrame *src, VJFrame *dest)
{
        int i;
        unsigned int Y;
        uint8_t *p, *qy, *qu, *qv;
        int32_t *s;
	const int video_area = src->len;

        p = timebuffer + plane * video_area;
        qy = bgimage[0];
	qu = bgimage[1];
	qv = bgimage[2];

	uint8_t *lum = src->data[0];
	uint8_t *u0  = src->data[1];
	uint8_t *v0  = src->data[2];

	uint8_t *Y1 = dest->data[0];
	uint8_t *U1 = dest->data[1];
	uint8_t *V1 = dest->data[2];

        s = sum;
	uint8_t a,b,c;
        for(i=0; i<video_area; i++) {
                Y = lum[i];
                *s -= *p;
                *s += Y;
                *p = Y;
                Y = (abs(((int)Y<<PLANES_DEPTH) - (int)(*s)) * 8)>>PLANES_DEPTH;
                if(Y>255) Y = 255;
		a = lum[i];
		b = u0[i]; 
		c = v0[i];
		a += (( qy[i]  - a ) * Y )>>8;
		Y1[i] = a;
		b += (( qu[i] - b ) * Y )>>8;
		U1[i] = b;
		c += (( qv[i] - c ) * Y )>>8;
		V1[i] = c;
                p++;
                s++;
        }
        plane++;
        plane = plane & (PLANES-1);
}


static	void	drawDisappearing(VJFrame *src, VJFrame *dest)
{
        int i;
        unsigned int Y;
        uint8_t *p, *qu, *qv, *qy;
	int32_t *s;
	const int video_area = src->len;

	uint8_t *Y1 = dest->data[0];
	uint8_t *U1 = dest->data[1];
	uint8_t *V1 = dest->data[2];
	uint8_t *lum = src->data[0];
	uint8_t *u0  = src->data[1];
	uint8_t *v0  = src->data[2];

        p = timebuffer + (plane * video_area);
        qy = bgimage[0];
	qu= bgimage[1];
	qv= bgimage[2];
        s = sum;

	uint8_t a,b,c,A,B,C;

        for(i=0; i < video_area; i++) {

		Y = a = lum[i]; 
		b = u0[i]; 
		c = v0[i];
	
		A = qy[i];
		B = qu[i];
		C = qv[i];

                *s -= *p;
                *s += Y;
                *p = Y;

                Y = (abs(((int)Y<<PLANES_DEPTH) - (int)(*s)) * 8)>>PLANES_DEPTH;
                if(Y>255) Y = 255;

		A += (( a - A ) * Y )>> 8;
		Y1[i] = A;	
		B += (( b - B ) * Y ) >> 8;
		U1[i] = B;
		C += (( c - C ) * Y ) >> 8;
		V1[i] = C;

                p++;
                s++;
        }

        plane++;
        plane = plane & (PLANES-1);
}

void chameleon_apply( VJFrame *frame, int width, int height, int mode)
{
	unsigned int i;
	const int len = (width * height);
	VJFrame source;
	veejay_memcpy( tmpimage[0], frame->data[0], len );
	veejay_memcpy( tmpimage[1], frame->data[1], len );
	veejay_memcpy( tmpimage[2], frame->data[2], len );
	veejay_memcpy( &source, frame, sizeof(VJFrame));
	source.data[0] = tmpimage[0];
	source.data[1] = tmpimage[1];
	source.data[2] = tmpimage[2];

	int interpolate = 0;
	uint32_t activity = 0;
	int auto_switch = 0;
	int tmp1,tmp2;
	if( motionmap_active() )
	{
		motionmap_scale_to( 32,32,1,1, &tmp1,&tmp2, &n__, &N__ );
		auto_switch = 1;
		activity = motionmap_activity();
	}
	else
	{
		N__ = 0;
		n__ = 0;
	}

	if( n__ == N__ || n__ == 0 )
		auto_switch = 0;
	

	if(auto_switch)
	{
		if( activity <= 40 )
		{
			// into the wall
			drawDisappearing( &source, frame );
		}
		else
		{
			// out of the wall
			drawAppearing( &source, frame );
		}
	}

	if( mode == 0 )
		drawDisappearing( &source, frame );
	else
		drawAppearing( &source, frame );
	

}
