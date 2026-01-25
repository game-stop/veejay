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
#include "widthmirror.h"

vj_effect *widthmirror_init(int max_width,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 2;

    ve->limits[0][0] = 2;
    ve->limits[1][0] = max_width / 24;

    ve->description = "Width Mirror";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Widths");
    return ve;
}

void widthmirror_apply(void *ptr, VJFrame *frame, int *args)
{
    if (!frame || !args) return;

    int width_div = args[0];
    const unsigned int width = frame->width;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    if (width_div >= frame->width || width_div < 2)
        width_div = 2;

    unsigned int divisor = width / width_div;
    if (divisor == 0) divisor = 1; // safety

    for (unsigned int r = 0; r < len; r += width) {
        #pragma omp simd
        for (unsigned int c = 0; c < width; c++) {
            int p1 = r + (divisor - c < 0 ? 0 : (divisor - c >= (int)width ? width - 1 : divisor - c));
            int p2 = r + (width - c - 1 < 0 ? 0 : (width - c - 1 >= (int)width ? width - 1 : width - c - 1));

            Y[p1] = Y[r + c];
            Y[p2] = Y[r + c];

            Cb[p1] = Cb[r + c];
            Cb[p2] = Cb[r + c];

            Cr[p1] = Cr[r + c];
            Cr[p2] = Cr[r + c];
        }
    }
}

