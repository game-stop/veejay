/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "blackreplace.h"

vj_effect *blackreplace_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->defaults[0] = 255;
    ve->defaults[1] = 150;
    ve->defaults[2] = 120;
    ve->defaults[3] = 250;
    ve->description = "Replace Black for a Color";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 1;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Red", "Green", "Blue" );
    return ve;
}


void blackreplace_apply( void *ptr, VJFrame *frame, int *args ) {
    int threshold = args[0];
    int red = args[1];
    int green = args[2];
    int blue = args[3];

    int i;
    const int len = frame->len;
    const int uv_len = (frame->ssm ? len : frame->uv_len );

    int colorCb=128, colorCr=128, colorY=0;

    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    _rgb2yuv(red,green,blue,colorY,colorCb,colorCr);

#pragma omp simd
    for( i = 0; i < len; i ++ ) {
        if( Y[i] < threshold && Cb[i] == 128 && Cr[i] == 128 ) {
            Y[i] = colorY;
            Cb[i] = colorCb;
            Cr[i] = colorCr;
        }
    }
}
