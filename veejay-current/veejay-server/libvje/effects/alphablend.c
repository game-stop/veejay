/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nelburg@gmail.com>
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
#include "alphablend.h"

static inline unsigned int alphablend_div255(unsigned int value)
{
    value += 128;
    return (value + (value >> 8)) >> 8;
}

vj_effect *alphablend_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 255;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->description = "Alpha: Blend";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->alpha = FLAG_ALPHA_SRC_B | FLAG_ALPHA_IN_BLEND;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity");
    return ve;
}

void alphablend_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    const int opacity = args[0];
    if(opacity == 0)
        return;

    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict U  = frame->data[1];
    uint8_t *restrict V  = frame->data[2];

    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict U2 = frame2->data[1];
    uint8_t *restrict V2 = frame2->data[2];
    uint8_t *restrict A2 = frame2->data[3];

    if(opacity == 255)
    {
#pragma omp parallel for num_threads(n_threads) schedule(static)
        for(int i = 0; i < len; i++)
        {
            const unsigned int a = A2[i];
            const unsigned int ia = 255 - a;

            Y[i] = (uint8_t) alphablend_div255(ia * Y[i] + a * Y2[i]);
            U[i] = (uint8_t) alphablend_div255(ia * U[i] + a * U2[i]);
            V[i] = (uint8_t) alphablend_div255(ia * V[i] + a * V2[i]);
        }
        return;
    }

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        const unsigned int a = alphablend_div255((unsigned int) A2[i] * opacity);
        const unsigned int ia = 255 - a;

        Y[i] = (uint8_t) alphablend_div255(ia * Y[i] + a * Y2[i]);
        U[i] = (uint8_t) alphablend_div255(ia * U[i] + a * U2[i]);
        V[i] = (uint8_t) alphablend_div255(ia * V[i] + a * V2[i]);
    }
}
