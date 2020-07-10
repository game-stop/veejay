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
#include "motionblur.h"

vj_effect *motionblur_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 15;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1000; /* time in frames */
    ve->description = "Motion blur";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->parallel = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Frames" );
	 return ve;
}

typedef struct {
    uint8_t *previous_frame[3];
    int last_max;
    int n_motion_frames;
} motionblur_t;

void *motionblur_malloc(int width, int height)
{
    motionblur_t *m = (motionblur_t*) vj_calloc(sizeof(motionblur_t));
    if(!m) {
        return NULL;
    }
    m->previous_frame[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(width * height *3));
    if(!m->previous_frame[0]) {
        free(m);
        return NULL;
    }
    m->previous_frame[1] = m->previous_frame[0] + RUP8(width*height);
    m->previous_frame[2] = m->previous_frame[1] + RUP8(width*height);
	
    return (void*) m;
}

void motionblur_free(void *ptr) {

    motionblur_t *m = (motionblur_t*) ptr;
    free(m->previous_frame[0]);
    free(m);
}


void motionblur_apply( void *ptr, VJFrame *frame, int *args ) {
    int n = args[0];
	const int len = frame->len;
	const int uv_len = frame->uv_len;

    motionblur_t *m = (motionblur_t*) ptr;

	unsigned int i;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

    uint8_t **previous_frame = m->previous_frame;

    if(m->n_motion_frames > 0) {
	  
	  for(i=0; i < len; i++) {
		Y[i] = (Y[i] + previous_frame[0][i])>>1;
		previous_frame[0][i] = Y[i];
  	  }


	  for(i=0; i < uv_len; i++) {
		Cb[i] = (Cb[i] + previous_frame[1][i])>>1;
		Cr[i] = (Cr[i] + previous_frame[2][i])>>1;
		previous_frame[1][i] = Cb[i];
		previous_frame[2][i] = Cr[i];
    	  }
	  
	}
	else 
	{
		for( i = 0; i < len ;  i ++ ) {
			previous_frame[0][i] = Y[i];
		}
		for( i = 0; i < uv_len ;  i ++ ) {
			previous_frame[1][i] = Cb[i];
			previous_frame[2][i] = Cr[i];		
		}
	}

	m->n_motion_frames ++;

	if( m->last_max != n ) {
		m->last_max = n;
		if( m->n_motion_frames > m->last_max ) {
			m->n_motion_frames = 1;
		}
	}

}

