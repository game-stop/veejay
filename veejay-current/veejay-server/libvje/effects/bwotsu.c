/* 
 * Linux VeeJay
 *
 * Copyright(C)2016 Niels Elburg <nwelburg@gmail.com>
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
#include "bwotsu.h"

vj_effect *bwotsu_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 1;    ve->defaults[0] = 0;
    ve->limits[0][1] = 0; ve->limits[1][1] = 0xff; ve->defaults[1] = 0xff;
    ve->limits[0][2] = 0; ve->limits[1][2] = 1;    ve->defaults[2] = 0;

    ve->description = "Black and White Mask by Otsu's method";
    ve->sub_format = -1;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;
    ve->param_description = vje_build_param_list(ve->num_params, "To Alpha", "Skew", "Invert");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 88,                 255,                10, 42, 1000, 3200, 0,    68,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void bwotsu_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;
    int mode = args[0];
    const int skew = args[1];
    const int invert = args[2];
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A = frame->data[3];

    if(mode == 1 && !A)
        mode = 0;

    uint32_t histogram[256] = { 0 };
    const int n_threads = vje_advise_num_threads(len);

    if(skew != 0xff)
    {
        uint8_t lookup[256];

        __init_lookup_table(lookup, 256, 0.0f, 255.0f, 0.0f, (float)skew);

        #pragma omp parallel num_threads(n_threads)
        {
            uint32_t local[256] = { 0 };

            #pragma omp for schedule(static)
            for(int i = 0; i < len; i++)
                local[lookup[Y[i]]]++;

            #pragma omp critical
            {
                for(int i = 0; i < 256; i++)
                    histogram[i] += local[i];
            }
        }
    }
    else
    {
        #pragma omp parallel num_threads(n_threads)
        {
            uint32_t local[256] = { 0 };

            #pragma omp for schedule(static)
            for(int i = 0; i < len; i++)
                local[Y[i]]++;

            #pragma omp critical
            {
                for(int i = 0; i < 256; i++)
                    histogram[i] += local[i];
            }
        }
    }

    const uint32_t threshold = otsu_method(histogram);
    const uint8_t low = invert ? 0xff : 0x00;
    const uint8_t high = invert ? 0x00 : 0xff;

    if(mode == 0)
    {
        #pragma omp parallel for num_threads(n_threads) schedule(static)
        for(int i = 0; i < len; i++)
        {
            const uint8_t cond = Y[i] >= threshold;
            Y[i] = (cond * high) | ((1 - cond) * low);
        }

        const int uv_len = frame->ssm ? len : frame->uv_len;

        #pragma omp parallel sections num_threads(n_threads)
        {
            #pragma omp section
            veejay_memset(Cb, 128, uv_len);

            #pragma omp section
            veejay_memset(Cr, 128, uv_len);
        }
    }
    else
    {
        #pragma omp parallel for num_threads(n_threads) schedule(static)
        for(int i = 0; i < len; i++)
        {
            const uint8_t cond = Y[i] >= threshold;
            A[i] = (cond * high) | ((1 - cond) * low);
        }
    }
}
