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

#include "motionblur.h"
#include <stdlib.h>
#include <config.h>

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);

static uint8_t *previous_frame[3];
vj_effect *motionblur_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 15;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1000; /* time in frames */
    ve->description = "Motion blur";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 1;
    return ve;
}

int motionblur_malloc(int width, int height)
{
	previous_frame[0] = (uint8_t*) vj_malloc( width * height * sizeof(uint8_t));
	if(!previous_frame[0]) return 0;
	previous_frame[1] = (uint8_t*) vj_malloc( width * height * sizeof(uint8_t));
	if(!previous_frame[1]) return 0;
	previous_frame[2] = (uint8_t*) vj_malloc( width * height * sizeof(uint8_t));
   	if(!previous_frame[2]) return 0;

	return 1;
}

void motionblur_free() {
   if(previous_frame[0]) free(previous_frame[0]);
   if(previous_frame[1]) free(previous_frame[1]);
   if(previous_frame[2]) free(previous_frame[2]);
}


static int n_motion_frames = 0;
void motionblur_apply(uint8_t *yuv1[3], int width, int height, int n) {
	unsigned int len = width * height;
	unsigned int i;

	
        if(n_motion_frames > 0) {
	  
	  for(i=0; i < len; i++) {
		yuv1[0][i] = (yuv1[0][i] + previous_frame[0][i])>>1;
		previous_frame[0][i] = yuv1[0][i];
  	  }

	  len = len/4;

	  for(i=0; i < len; i++) {
		yuv1[1][i] = (yuv1[1][i] + previous_frame[1][i])>>1;
		yuv1[2][i] = (yuv1[2][i] + previous_frame[2][i])>>1;
		previous_frame[1][i] = yuv1[1][i];
		previous_frame[2][i] = yuv1[2][i];
    	  }
	  
	}
	else 
	{
		/* just copy to previous */
		veejay_memcpy( previous_frame[0], yuv1[0], len );
		veejay_memcpy( previous_frame[1], yuv1[1], len/4);
		veejay_memcpy( previous_frame[2], yuv1[2], len/4);
		
	}

	n_motion_frames ++;

	if(n_motion_frames >= n ) {
		n_motion_frames = 0;
/*
#ifdef HAVE_ASM_MMX
	memset_ycbcr( previous_frame[0], previous_frame[0], 0, (width*height));
	memset_ycbcr( previous_frame[1], previous_frame[1], 0, (width*height)/4);
	memset_ycbcr( previous_frame[2], previous_frame[2], 0, (width*height)/4);
#else
		memset( previous_frame[0], 0, (width*height));
		memset( previous_frame[1], 0, len/4);
		memset( previous_frame[2], 0, len/4);
#endif
	*/
	}

}

