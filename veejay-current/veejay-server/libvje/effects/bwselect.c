
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
#include "bwselect.h"
#include "common.h"
vj_effect *bwselect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 16;
    ve->defaults[1] = 235;
	ve->parallel = 1;	
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->description = "Black and White by Threshold";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user =0 ;
	ve->param_description = vje_build_param_list( ve->num_params, "Min threshold", "Max threshold" );
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
				Y[r+c] = pixel_Y_hi_;
			}
			else {
				Y[r+c] = pixel_Y_lo_;
			}
		}
	}
	veejay_memset(Cb, 128, frame->uv_len);
	veejay_memset(Cr, 128, frame->uv_len);
}

void bwselect_free(){}
