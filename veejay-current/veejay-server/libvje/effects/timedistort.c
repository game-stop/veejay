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
#include "common.h"
#include <veejaycore/vjmem.h>
#include "softblur.h"
#include "timedistort.h"
#include <libvje/internal.h>
#include <libvje/effects/motionmap.h>

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
	ve->motion = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Value");
    return ve;
}

typedef struct {
    int n__;
    int N__;
    uint8_t	*nonmap;
    uint8_t *planes[4];
    uint8_t *planetableY[PLANES];
    uint8_t *planetableU[PLANES];
    uint8_t *planetableV[PLANES];
    uint8_t *warptime[2];
    int state;
    int plane;
    int warptimeFrame;
    int have_bg;
    void *motionmap;
} timedistort_t;

void *timedistort_malloc( int w, int h )
{	
	unsigned int i;
    timedistort_t *td = (timedistort_t*) vj_calloc(sizeof(timedistort_t));
    if(!td) {
        return NULL;
    }

	td->nonmap = vj_calloc( (RUP8(2 * w * h) + RUP8(2 * w)) * sizeof(uint8_t));
	if(!td->nonmap) {
        free(td);
		return NULL;
    }

	td->planes[0] = vj_malloc( RUP8(PLANES * 3 * w * h) * sizeof(uint8_t));
    if(!td->planes[0]) {
        free(td->nonmap);
        free(td);
        return NULL;
    }
	td->planes[1] = td->planes[0] + RUP8(PLANES * w * h );
	td->planes[2] = td->planes[1] + RUP8(PLANES * w * h );

	veejay_memset( td->planes[0],0, RUP8(PLANES * w * h ));
	veejay_memset( td->planes[1],128,RUP8(PLANES * w * h  ));
	veejay_memset( td->planes[2],128,RUP8(PLANES * w * h ));

	td->have_bg = 0;
	td->n__ = 0;
	td->N__ = 0;
	
	for( i = 0; i < PLANES; i ++ )
	{
		td->planetableY[i] = &(td->planes[0][ (w*h) * i ]);
		td->planetableU[i] = &(td->planes[1][ (w*h) * i ]);
		td->planetableV[i] = &(td->planes[2][ (w*h) * i ]);
	}

	td->warptime[0] = (uint8_t*) vj_calloc( sizeof(uint8_t) * RUP8((w * h)+w+1) );
    if(!td->warptime[0]) {
        free(td->nonmap);
        free(td->planes[0]);
        free(td);
        return NULL;
    }
	td->warptime[1] = (uint8_t*) vj_calloc( sizeof(uint8_t) * RUP8((w * h)+w+1) );
    if(!td->warptime[1]) {
        free(td->nonmap);
        free(td->planes[0]);
        free(td->warptime[0]);
        free(td);
        return NULL;
    }

	td->plane = 0;
	td->state = 1;

	return (void*) td;
}

void	timedistort_free(void *ptr)
{
    timedistort_t *td = (timedistort_t*) ptr;

	if(td->nonmap)
		free(td->nonmap);
	if( td->planes[0])
		free(td->planes[0]);
	if( td->warptime[0] )
		free(td->warptime[0]);
	if( td->warptime[1] )
		free(td->warptime[1] );
    free(td);
}

int timedistort_request_fx() {
    return VJ_IMAGE_EFFECT_MOTIONMAP;
}

void timedistort_set_motionmap(void *ptr, void *priv)
{
    timedistort_t *t = (timedistort_t*) ptr;
    t->motionmap = priv;
}

void timedistort_apply( void *ptr,  VJFrame *frame, int *args )
{
	unsigned int i;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;

	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	int interpolate = 1;
	int motion = 0;
	int tmp1,tmp2;

    int val = args[0];

    timedistort_t *td = (timedistort_t*) ptr;
	uint8_t *diff = td->nonmap;
	uint8_t *prev = td->nonmap + len;

	if(motionmap_active(td->motionmap)) //@ use motion mapping frame
	{
		motionmap_scale_to(td->motionmap, 255,255,1,1,&tmp1,&tmp2, &(td->n__),&(td->N__) );
		diff = motionmap_bgmap(td->motionmap);
        motion = 1;
	}
	else
	{
		td->n__ = 0;
		td->N__ = 0;

		if(!td->have_bg)
		{
			vj_frame_copy1( Y, prev, len );
			VJFrame smooth;
			veejay_memcpy(&smooth,frame, sizeof(VJFrame));
			smooth.data[0] = prev;
			softblur_apply_internal(&smooth, 0 );
			veejay_memset( diff, 0, len );
			td->have_bg = 1;
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
			softblur_apply_internal(&smooth, 0 );
		}
	}
	
	if( td->n__ == td->N__ || td->n__ == 0 )
		interpolate = 0;

	//@ process
	uint8_t *planeTables[4] = { td->planetableY[td->plane], td->planetableU[td->plane], td->planetableV[td->plane], NULL };
	int strides[4] = { len, len, len, 0 };
	vj_frame_copy( frame->data, planeTables, strides );

	uint8_t *p = td->warptime[ td->warptimeFrame	] + width + 1;
	uint8_t *q = td->warptime[ td->warptimeFrame ^ 1] + width + 1;

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
	q = td->warptime[ td->warptimeFrame ^ 1 ] + width + 1;
	int n_plane = 0;
	for( i = 0; i < len; i ++ )
	{
		if( diff[i] ) {
			q[i] = PLANES - 1;
		}

		n_plane = ( td->plane - q[i] + PLANES ) & (PLANES-1);

		Y[i]  = td->planetableY[ n_plane ][i];
		Cb[i] = td->planetableU[ n_plane ][i];
		Cr[i] = td->planetableV[ n_plane ][i];
	}

	td->plane ++;
	td->plane = td->plane & (PLANES-1);
	td->warptimeFrame ^= 1;

	if(interpolate)
		motionmap_interpolate_frame( td->motionmap,frame, td->N__,td->n__ );
	if(motion)
		motionmap_store_frame(td->motionmap,frame);

}
