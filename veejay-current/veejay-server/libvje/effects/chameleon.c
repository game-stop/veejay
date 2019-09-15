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

#include "common.h"
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include "softblur.h"
#include "chameleon.h"
#include "motionmap.h"

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
    ve->static_bg = 1;
	ve->has_user = 0;
	ve->motion = 1;
	ve->param_description = vje_build_param_list(ve->num_params, "Appearing/Dissapearing");
	return ve;
}


typedef struct {
    int last_mode_;
    int N__;
    int n__;
    void *motionmap;
    int has_bg;
    int32_t *sum;
    uint8_t *timebuffer;
    uint8_t *tmpimage[4];
    int plane;
    uint8_t	*bgimage[4];
} chameleon_t;

#define PLANES_DEPTH 6
#define	PLANES (1<< PLANES_DEPTH)

int	chameleon_prepare( void *ptr, VJFrame *frame )
{
    chameleon_t *c = (chameleon_t*) ptr;

	int strides[4] = { frame->len, frame->len, frame->len, 0  };
	vj_frame_copy( frame->data, c->bgimage, strides );	
	
	VJFrame tmp;
	veejay_memset( &tmp, 0, sizeof(VJFrame));
	tmp.data[0] = c->bgimage[0];
	tmp.width = frame->width;
	tmp.height = frame->height;
    tmp.len = frame->len;
	//@ 3x3 blur
	softblur_apply_internal( &tmp, 0);

	veejay_msg(2, "ChameleonTV: Snapped background frame");
	return 1;
}

void *chameleon_malloc(int w, int h)
{
    int i;
    chameleon_t *c = (chameleon_t*) vj_calloc(sizeof(chameleon_t));
    if(!c) {
        return NULL;
    }

    const int len = RUP8(w*h);
    const int safe_zone = RUP8(w*2);
    c->bgimage[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * (len + safe_zone) * 3 );
    if(!c->bgimage[0]) {
        free(c);
        return NULL;
    }
    c->tmpimage[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * (len * 3));
    if(!c->tmpimage[0]) {
        free(c->bgimage[0]);
        free(c);
        return NULL;
    }

    c->bgimage[1] = c->bgimage[0] + len + safe_zone;
    c->bgimage[2] = c->bgimage[1] + len + safe_zone;
    c->tmpimage[1] = c->tmpimage[0] + len;
    c->tmpimage[2] = c->tmpimage[1] + len;

	vj_frame_clear1( c->bgimage[0], pixel_Y_lo_, len + safe_zone);
	vj_frame_clear1( c->tmpimage[0], pixel_Y_lo_, len);
    
	for( i = 1; i < 3; i ++ ) {
		vj_frame_clear1( c->bgimage[i], 128, len + safe_zone);
		vj_frame_clear1( c->tmpimage[i], 128, len);
	}
	
	c->sum = (int32_t*) vj_calloc( len * sizeof(int32_t));
    if(!c->sum) {
        free(c->bgimage[0]);
        free(c->tmpimage[0]);
        free(c);
        return NULL;
    }

	c->timebuffer = (uint8_t*) vj_calloc( len * PLANES );
    if(!c->timebuffer) {
        free(c->bgimage[0]);
        free(c->tmpimage[0]);
        free(c->sum);
        free(c);
        return NULL;
    }

	c->last_mode_ = -1;

	return (void*) c;
}

void	chameleon_free(void *ptr)
{
    chameleon_t *c = (chameleon_t*) ptr;

    free(c->bgimage[0]);
    free(c->tmpimage[0]);
    free(c->timebuffer);
    free(c->sum);
    free(c);
}

static void drawAppearing(chameleon_t *cb, VJFrame *src, VJFrame *dest )
{
	int i;
	unsigned int Y;
	uint8_t *p, *qy, *qu, *qv;
	int32_t *s;
	const int video_area = src->len;

	p = cb->timebuffer + cb->plane * video_area;
	qy = cb->bgimage[0];
	qu = cb->bgimage[1];
	qv = cb->bgimage[2];

	uint8_t *lum = src->data[0];
	uint8_t *u0  = src->data[1];
	uint8_t *v0  = src->data[2];

	uint8_t *Y1 = dest->data[0];
	uint8_t *U1 = dest->data[1];
	uint8_t *V1 = dest->data[2];

	s = cb->sum;
	uint8_t a,b,c;
	for(i=0; i<video_area; i++)
	{
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

	cb->plane++;
	cb->plane = cb->plane & (PLANES-1);
}


static	void	drawDisappearing(chameleon_t *cb, VJFrame *src, VJFrame *dest)
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

	p = cb->timebuffer + (cb->plane * video_area);
	qy = cb->bgimage[0];
	qu = cb->bgimage[1];
	qv = cb->bgimage[2];
	s = cb->sum;

	uint8_t a,b,c,A,B,C;

	for(i=0; i < video_area; i++)
	{
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

	cb->plane++;
	cb->plane = cb->plane & (PLANES-1);
}

void chameleon_apply( void *ptr, VJFrame *frame, int *args ){
    int mode = args[0];
    chameleon_t *c = (chameleon_t*) ptr;

	const int len = frame->len;
	VJFrame source;
	int strides[4] = { len, len, len, 0 };
	vj_frame_copy( frame->data, c->tmpimage, strides );

	source.data[0] = c->tmpimage[0];
	source.data[1] = c->tmpimage[1];
	source.data[2] = c->tmpimage[2];
	source.len = len;

	uint32_t activity = 0;
	int auto_switch = 0;
	int tmp1,tmp2;

	if( motionmap_active(c->motionmap) )
	{
		motionmap_scale_to(c->motionmap, 32,32,1,1, &tmp1,&tmp2, &(c->n__), &(c->N__) );
		auto_switch = 1;
		activity = motionmap_activity(c->motionmap);
	}
	else
	{
		c->N__ = 0;
		c->n__ = 0;
	}

	if( c->n__ == c->N__ || c->n__ == 0 )
		auto_switch = 0;
	
	if(auto_switch)
	{
		if( activity <= 40 )
		{
			// into the wall
			drawDisappearing(c, &source, frame );
		}
		else
		{
			// out of the wall
			drawAppearing(c, &source, frame );
		}
	}
    else {

	    if( mode == 0 )
		    drawDisappearing(c, &source, frame );
	    else
		    drawAppearing(c, &source, frame );
    }

}

int chameleon_request_fx() {
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void chameleon_set_motionmap(void *ptr, void *priv) {
    chameleon_t *c = (chameleon_t*) ptr;
    c->motionmap = priv;
}

