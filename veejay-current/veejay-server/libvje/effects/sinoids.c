/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "sinoids.h"
#include "motionmap.h"

#define DEFAULT_SINOIDS 70

vj_effect *sinoids_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 1;
    ve->defaults[1] = DEFAULT_SINOIDS;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1000; /* sinoids */
    ve->description = "Sinoids";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->motion = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Sinoids");

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0, "Inplace", "On Copy" );

    return ve;
}

typedef struct {
    int *sinoids_X;
    uint8_t *sinoid_frame[3];
    int current_sinoids;
    int n__;
    int N__;
    void *motionmap;
} sinoids_t;

void *sinoids_malloc(int width, int height)
{
    int i;
    sinoids_t *s  = (sinoids_t*) vj_calloc( sizeof(sinoids_t) );
    if(!s) {
        return NULL;
    }

    s->sinoids_X = (int*) vj_calloc(sizeof(int) * width);
    if(!s->sinoids_X) {
        free(s);
        return NULL;

    }

    s->sinoid_frame[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(width*height*3));
    if(!s->sinoid_frame[0]) {
        free(s->sinoids_X);
        free(s);
        return NULL;
    }

    s->sinoid_frame[1] = s->sinoid_frame[0] + RUP8(width*height);
    s->sinoid_frame[2] = s->sinoid_frame[1] + RUP8(width*height);
    
    for(i=0; i < width; i++ ) {
	    s->sinoids_X[i] = (int) ( sin( ((double)i/(double)width) * 2 * 3.1415926) * 1);
	    s->sinoids_X[i] *= 4;
	}

    s->current_sinoids = DEFAULT_SINOIDS;
  
    return (void*) s;
}

void sinoids_free(void *ptr) {
    sinoids_t *s = (sinoids_t*) ptr;

    free(s->sinoids_X);
	free(s->sinoid_frame[0]);

    free(s);
}

static void sinoids_recalc(sinoids_t *s, int width, int z) {
	int i=0;
	double zoom = ( (double)z / 10.0);
    int *sinoids_X = s->sinoids_X;
	for(i=0; i < width; i++ ) {
	  sinoids_X[i] = (int) ( sin( ((double)i/(double)width) * 2 * 3.1415926) * zoom);
	  sinoids_X[i] *= 4;
	}
}

void sinoids_apply(void *ptr, VJFrame *frame, int *args)
{
	const int len = frame->len;
	const unsigned int width = frame->width;
	unsigned int r,c;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    int m = args[0];
    int ns = args[1];
    sinoids_t *s = (sinoids_t*) ptr;

	int interpolate = 1;
	int tmp1 = m;
	int tmp2 = ns;
	int motion = 0;
	if( motionmap_active(s->motionmap))
	{
		motionmap_scale_to( s->motionmap, 1,1000,0, 0, &tmp1, &tmp2, &(s->n__), &(s->N__) );
		motion = 1;
	}
	else
	{
		s->n__ = 0;
		s->N__ = 0;
	}
	if( s->n__ == s->N__ || s->n__ == 0 )
		interpolate = 0;
	

	
    if(tmp2 != s->current_sinoids) {
		sinoids_recalc(s, width, tmp2);
		s->current_sinoids = tmp2;
	}

    int *sinoids_X = s->sinoids_X;
    uint8_t **sinoid_frame = s->sinoid_frame;

	if(m==0) {
  	  for( r=width ; r < len-width; r+=width) {
             for( c = 0; c < width; c++) {
		Y[r+c] = Y[(r+c+sinoids_X[c])];
		Cb[r+c] = Cb[(r+c+sinoids_X[c])];
		Cr[r+c] = Cr[(r+c+sinoids_X[c])];
	     }
	  }
	}
    else {
		/* on copy */
	    for(r=0; r < len ;r++) {
		    sinoid_frame[0][r] = Y[r];
	    	sinoid_frame[1][r] = Cb[r];	
		    sinoid_frame[2][r] = Cr[r];
		}
	    
        for(r=width; r < len-width; r+= width) {
	        for(c=0; c < width; c++) {
	  	        Y[r+c] = sinoid_frame[0][(r+c+sinoids_X[c])];
		        Cb[r+c] = sinoid_frame[1][(r+c+sinoids_X[c])];
		        Cr[r+c] = sinoid_frame[2][(r+c+sinoids_X[c])];
	        }
	    }
	}

	if( interpolate )
		motionmap_interpolate_frame( s->motionmap, frame, s->N__, s->n__ );
	
	if (motion)
		motionmap_store_frame( s->motionmap, frame );

}

int sinoids_request_fx() {
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void sinoids_set_motionmap(void *ptr, void *priv )
{
    sinoids_t *s = (sinoids_t*) ptr;
    s->motionmap = priv;
}
