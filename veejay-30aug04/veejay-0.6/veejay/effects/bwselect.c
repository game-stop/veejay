
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

void bwselect_apply(uint8_t *yuv1[3], int width, int height, int min_threshold, int max_threshold) {
	int r,c;
        const int len = width * height;
        const int uv_len = len /4;
	for(r=0; r < len; r+=width) {
		for(c=0; c < width; c++) {
			uint8_t p = yuv1[0][r+c];
			if( p > min_threshold && p < max_threshold) {
				yuv1[0][r+c] = 235;
			}
			else {
				yuv1[0][r+c] = 16;
			}
		}
	}
#ifdef HAVE_ASM_MMX
	memset_ycbcr( yuv1[1], yuv1[1], 128, width>>1, height>>1);
	memset_ycbcr( yuv1[2], yuv1[2], 128, width>>1, height>>1);
#else
	memset(yuv1[1], 128, uv_len);
	memset(yuv1[2], 128, uv_len);
#endif	
}

void bwselect_free(){}
