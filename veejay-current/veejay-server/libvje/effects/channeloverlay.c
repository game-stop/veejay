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
#include "channeloverlay.h"

vj_effect *channeloverlay_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 0;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 7;

    ve->sub_format = 1;
    ve->description = "Channel Overlay";
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Operator");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0,
                              "Luminance B",
                              "Negative Luminance B",
                              "Chroma Blue B",
                              "Negative Chroma Blue B",
                              "Chroma Red B",
                              "Negative Chroma Red B",
                              "Alpha B",
                              "Negative Alpha B");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

#define PROCESS_PIXEL_VALUE(op0_) do {                  \
    const unsigned int op0 = (unsigned int)(op0_);       \
    const unsigned int op1 = 255u - op0;                 \
    Y[i]  = (uint8_t)((op0 * Y[i]  + op1 * Y2[i])  >> 8); \
    Cb[i] = (uint8_t)((op0 * Cb[i] + op1 * Cb2[i]) >> 8); \
    Cr[i] = (uint8_t)((op0 * Cr[i] + op1 * Cr2[i]) >> 8); \
} while(0)

void channeloverlay_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void) ptr;

    const int len = frame->len;

    const int mode = args[0];
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];
    const uint8_t *restrict A2 = frame2->data[3];

    switch(mode)
    {
        case 0:
            #pragma omp parallel for num_threads(n_threads) schedule(static)
            for(int i = 0; i < len; i++)
                PROCESS_PIXEL_VALUE(Y2[i]);
            break;

        case 1:
            #pragma omp parallel for num_threads(n_threads) schedule(static)
            for(int i = 0; i < len; i++)
                PROCESS_PIXEL_VALUE(255 - Y2[i]);
            break;

        case 2:
            #pragma omp parallel for num_threads(n_threads) schedule(static)
            for(int i = 0; i < len; i++)
                PROCESS_PIXEL_VALUE(Cb2[i]);
            break;

        case 3:
            #pragma omp parallel for num_threads(n_threads) schedule(static)
            for(int i = 0; i < len; i++)
                PROCESS_PIXEL_VALUE(255 - Cb2[i]);
            break;

        case 4:
            #pragma omp parallel for num_threads(n_threads) schedule(static)
            for(int i = 0; i < len; i++)
                PROCESS_PIXEL_VALUE(Cr2[i]);
            break;

        case 5:
            #pragma omp parallel for num_threads(n_threads) schedule(static)
            for(int i = 0; i < len; i++)
                PROCESS_PIXEL_VALUE(255 - Cr2[i]);
            break;

        case 6:
            if(A2) {
                #pragma omp parallel for num_threads(n_threads) schedule(static)
                for(int i = 0; i < len; i++)
                    PROCESS_PIXEL_VALUE(A2[i]);
            }
            else {
                #pragma omp parallel for num_threads(n_threads) schedule(static)
                for(int i = 0; i < len; i++)
                    PROCESS_PIXEL_VALUE(255);
            }
            break;

        case 7:
            if(A2) {
                #pragma omp parallel for num_threads(n_threads) schedule(static)
                for(int i = 0; i < len; i++)
                    PROCESS_PIXEL_VALUE(255 - A2[i]);
            }
            else {
                #pragma omp parallel for num_threads(n_threads) schedule(static)
                for(int i = 0; i < len; i++)
                    PROCESS_PIXEL_VALUE(0);
            }
            break;
    }
}

#undef PROCESS_PIXEL_VALUE
