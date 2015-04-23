/* 
 * Linux VeeJay
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * TimeDistortionTV - scratch the surface and playback old images.
 * Copyright (C) 2005 Ryo-ta
 *
 * Ported and arranged by Kentaro Fukuchi

 * Ported and modified by Niels Elburg 
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
#include <config.h>
#include <stdio.h>
#include <stdint.h>
#include <libvjmem/vjmem.h>
#include "timedistort.h"
#include "common.h"
#include "softblur.h"
#define PLANES 32

vj_effect *timedistort_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 5;
    ve->limits[1][0] = 100;
    ve->defaults[0] = 40;
    ve->description = "TimeDistortionTV (EffectTV)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Value");
    return ve;
}

static int n__ = 0;
static int N__ = 0;
static	uint8_t	*nonmap = NULL;
static uint8_t *planes[4] = { NULL, NULL, NULL, NULL };
static uint8_t *planetableY[PLANES];
static uint8_t *planetableU[PLANES];
static uint8_t *planetableV[PLANES];

static uint8_t *warptime[2];
static int state = 0;
static int plane = 0;
static int warptimeFrame = 0;

static int have_bg =0;
int 	timedistort_malloc( int w, int h )
{	
	unsigned int i;
	if(nonmap) timedistort_free();
	nonmap = vj_malloc( RUP8(w + 2 * w * h) * sizeof(uint8_t));
	if(!nonmap)
		return 0;

	planes[0] = vj_malloc( RUP8(PLANES * 3 * w * h) * sizeof(uint8_t));
	planes[1] = planes[0] + RUP8(PLANES * w * h );
	planes[2] = planes[1] + RUP8(PLANES * w * h );

	veejay_memset( planes[0],0, RUP8(PLANES * w * h ));
	veejay_memset( planes[1],128,RUP8(PLANES * w * h  ));
	veejay_memset( planes[2],128,RUP8(PLANES * w * h ));

	have_bg = 0;
	n__ = 0;
	N__ = 0;
	
	for( i = 0; i < PLANES; i ++ )
	{
		planetableY[i] = &planes[0][ (w*h) * i ];
		planetableU[i] = &planes[1][ (w*h) * i ];
		planetableV[i] = &planes[2][ (w*h) * i ];
	}

	warptime[0] = (uint8_t*) vj_calloc( sizeof(uint8_t) * RUP8((w * h)+w+1) );
	warptime[1] = (uint8_t*) vj_calloc( sizeof(uint8_t) * RUP8((w * h)+w+1) );
	if( warptime[0] == NULL || warptime[1] == NULL )
		return 0;

	plane = 0;
	state = 1;


	return 1;
}

void	timedistort_free()
{
	if(nonmap)
		free(nonmap);
	if( planes[0])
		free(planes[0]);
	if( warptime[0] )
		free(warptime[0]);
	if( warptime[1] )
		free(warptime[1] );
	veejay_memset( planetableY, 0, PLANES);
	veejay_memset( planetableU, 0, PLANES);
	veejay_memset( planetableV, 0, PLANES);
	planes[0] = NULL;
	warptime[0] = NULL;
	warptime[1] = NULL;
	state = 0;
	plane = 0;
	nonmap = NULL;
}

void timedistort_apply( VJFrame *frame, int width, int height, int val)
{
	unsigned int i;
	const int len = (width * height);

	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	uint8_t *diff = nonmap;
	uint8_t *prev = nonmap + len;
	int interpolate = 1;
	int motion = 0;
	int tmp1,tmp2;
	if(motionmap_active()) //@ use motion mapping frame
	{
		motionmap_scale_to( 255,255,1,1,&tmp1,&tmp2, &n__,&N__ );
		motion = 1;
		diff = motionmap_bgmap();
	}
	else
	{
		n__ = 0;
		N__ = 0;

		if(!have_bg)
		{
			vj_frame_copy1( Y, prev, len );
			VJFrame smooth;
			veejay_memcpy(&smooth,frame, sizeof(VJFrame));
			smooth.data[0] = prev;
			softblur_apply(&smooth, width, height, 0 );
			veejay_memset( diff, 0, len );
			have_bg = 1;
			return;
		}
		else
		{
			/*for( i = 0; i < len ; i ++ )
			{
				diff[i] = (abs(prev[i] - Y[i])> val ? 0xff: 0 );
			}*/
			vje_diff_plane( prev, Y, diff, val, len );
			vj_frame_copy1( Y, prev, len );
			VJFrame smooth;
			veejay_memcpy(&smooth,frame, sizeof(VJFrame));
			smooth.data[0] = prev;
			softblur_apply(&smooth, width, height, 0 );
		}
	}
	
	if( n__ == N__ || n__ == 0 )
		interpolate = 0;

	//@ process
	uint8_t *planeTables[4] = { planetableY[plane], planetableU[plane], planetableV[plane], NULL };
	int strides[4] = { len, len, len, 0 };
	vj_frame_copy( frame->data, planeTables, strides );

	uint8_t *p = warptime[ warptimeFrame	] + width + 1;
	uint8_t *q = warptime[ warptimeFrame ^ 1] + width + 1;

	unsigned int x,y;
	for( y = height - 2; y > 0 ; y -- )
	{
		for( x = width - 2; x > 0; x -- )
		{
			i = *(p - width) + *(p-1) + *(p+1) + *(p + width);
			if( i > 3 ) i-= 3;
			p++;
			*q++ = i >> 2;
	
		}
		p += 2;
		q += 2;
	}
	q = warptime[ warptimeFrame ^ 1 ] + width + 1;
	int n_plane = 0;
	for( i = 0; i < len; i ++ )
	{
		if( diff[i] ) {
			q[i] = PLANES - 1;
		}

		n_plane = ( plane - q[i] + PLANES ) & (PLANES-1);

		Y[i]  = planetableY[ n_plane ][i];
		Cb[i] = planetableU[ n_plane ][i];
		Cr[i] = planetableV[ n_plane ][i];
	}

	plane ++;
	plane = plane & (PLANES-1);
	warptimeFrame ^= 1;

	if(interpolate)
		motionmap_interpolate_frame( frame, N__,n__ );
	if(motion)
		motionmap_store_frame(frame);

}
