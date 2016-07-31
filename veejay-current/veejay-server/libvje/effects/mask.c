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
#include <libvjmem/vjmem.h>
#include "mask.h"

vj_effect *simplemask_init(int w, int h )
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 2;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 255;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 2;
	ve->defaults[0] = 128;
	ve->defaults[1] = 0;
	ve->description = "Binary Threshold Mask";
	ve->sub_format = 1;
	ve->extra_frame = 1;
	ve->has_user =0;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Mode" );


	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][1], 1,
			"Normal", "Threshold Black", "Threshold White" );

 	return ve;
}

static void mask_replace_black(uint8_t *yuv1[3], uint8_t *yuv2[3],
                               const int w, const int h, const int len, int threshold) {
  unsigned int i=0;
  for(i=0; i < len; i++) {
    if (yuv1[0][i] > threshold) {
      yuv1[0][i] = yuv2[0][i];
      yuv1[1][i] = yuv2[1][i];
      yuv1[2][i] = yuv2[2][i];
    } 
  }
}

static void mask_replace_black_fill(uint8_t *yuv1[3], uint8_t *yuv2[3],
                                    const int w, const int h, const int len, int threshold) {
  unsigned int i=0;
  for(i=0; i < len; i++) {
    if (yuv1[0][i] > threshold) {
      yuv1[0][i] = yuv2[0][i];
      yuv1[1][i] = yuv2[1][i];
      yuv1[2][i] = yuv2[2][i];
    } 
    else {
      yuv1[0][i] = 16;
      yuv1[1][i] = 128;
      yuv1[2][i] = 128;
    }
  }
}

static void mask_replace_white_fill(uint8_t *yuv1[3], uint8_t *yuv2[3],
                                    const int w, const int h, const int len, int threshold) {
  unsigned int i=0;
  for(i=0; i < len; i++) {
    if (yuv1[0][i] > threshold) {
      yuv1[0][i] = yuv2[0][i];
      yuv1[1][i] = yuv2[1][i];
      yuv1[2][i] = yuv2[2][i];
    } 
    else {
      yuv1[0][i] = pixel_Y_hi_;
      yuv1[1][i] = 128;
      yuv1[2][i] = 128;
    }
  }
}

void simplemask_apply( VJFrame *frame, VJFrame *frame2, int threshold, int invert)
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const unsigned int len = frame->len;
	switch(invert) {
		case 0 : mask_replace_black(frame->data,frame2->data,width,height,len,threshold);
			break;
		case 1 : mask_replace_black_fill(frame->data,frame2->data,width,height,len,threshold);
			break;
		case 2: mask_replace_white_fill(frame->data,frame2->data,width,height,len,threshold);
			break;
    	}  
}
