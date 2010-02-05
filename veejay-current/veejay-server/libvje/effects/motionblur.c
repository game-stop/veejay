/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include "motionblur.h"
#include <config.h>

static uint8_t *previous_frame[3];
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
	ve->param_description = vje_build_param_list( ve->num_params, "Frames" );
	 return ve;
}

int motionblur_malloc(int width, int height)
{
	previous_frame[0] = (uint8_t*) vj_yuvalloc( width , height );
	if(!previous_frame[0]) return 0;
	previous_frame[1] = previous_frame[0] + (width * height);
	previous_frame[2] = previous_frame[1] + (width  * height);
	return 1;
}

void motionblur_free() {
   if(previous_frame[0])
	   free(previous_frame[0]);
	previous_frame[0] = NULL;
	previous_frame[1] = NULL;
	previous_frame[2] = NULL;
}


static int n_motion_frames = 0;
void motionblur_apply( VJFrame *frame, int width, int height, int n) {
	const int len = width * height;
	const int uv_len = frame->uv_len;

	unsigned int i;
    uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

	
        if(n_motion_frames > 0) {
	  
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
		/* just copy to previous */
		veejay_memcpy( previous_frame[0], Y, len );
		veejay_memcpy( previous_frame[1], Cb, uv_len);
		veejay_memcpy( previous_frame[2], Cr, uv_len);
		
	}

	n_motion_frames ++;

	if(n_motion_frames >= n ) {
		n_motion_frames = 0;

		veejay_memset( previous_frame[0], 0, (width*height));
		veejay_memset( previous_frame[1], 0, uv_len);
		veejay_memset( previous_frame[2], 0, uv_len);
	
	}

}

