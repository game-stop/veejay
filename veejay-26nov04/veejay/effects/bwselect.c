
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

#include <stdlib.h>
#include "bwselect.h"

vj_effect *bwselect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 16;
    ve->defaults[1] = 235;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->description = "Black and White by Threshold";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}

void bwselect_apply(VJFrame *frame, int width, int height, int min_threshold, int max_threshold) {
	int r,c;
        const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	for(r=0; r < len; r+=width) {
		for(c=0; c < width; c++) {
			uint8_t p = Y[r+c];
			if( p > min_threshold && p < max_threshold) {
				Y[r+c] = 235;
			}
			else {
				Y[r+c] = 16;
			}
		}
	}
#ifdef HAVE_ASM_MMX
	memset_ycbcr( Cb, Cb, 128, frame->uv_width, frame->uv_height);
	memset_ycbcr( Cr, Cr, 128, frame->uv_width, frame->uv_height);
#else
	memset(Cb, 128, frame->uv_len);
	memset(Cr, 128, frame->uv_len);
#endif	
}

void bwselect_free(){}
