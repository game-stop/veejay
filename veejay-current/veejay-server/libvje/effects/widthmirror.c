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
    ve->defaults[0] = 4;
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 256;
    ve->description = "Width Mirror";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Frequency");
    return ve;
}

void widthmirror_apply(void *ptr, VJFrame *frame, int *args)
{
    const int width = (int)frame->width;
    const int height = (int)frame->height;
    const int frequency = args[0];

    const int32_t band_w_fp = (width << 16) / frequency;
    const int band_w_int = band_w_fp >> 16;

    if (band_w_int < 1) return;

    uint8_t *restrict py = frame->data[0];
    uint8_t *restrict pu = frame->data[1];
    uint8_t *restrict pv = frame->data[2];

    int n_threads = vje_advise_num_threads(width * height);

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int y = 0; y < height; y++) {
        uint8_t *restrict ry = py + (y * width);
        uint8_t *restrict ru = pu + (y * width);
        uint8_t *restrict rv = pv + (y * width);

        int local_x = 0;
        int flip = 0;

        for (int x = 0; x < width; x++) {
            int src_x = flip ? (band_w_int - 1 - local_x) : local_x;

            src_x = (src_x < 0) ? 0 : (src_x >= band_w_int ? band_w_int - 1 : src_x);

            ry[x] = ry[src_x];
            ru[x] = ru[src_x];
            rv[x] = rv[src_x];

            local_x++;
            if (local_x >= band_w_int) {
                local_x = 0;
                flip = !flip;
            }
        }
    }
}