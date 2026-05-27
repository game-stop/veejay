/* 
 * Linux VeeJay
 *
 * Copyright(C)2018 Niels Elburg <nwelburg@gmail.com>
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
#include "posterize2.h"

vj_effect *posterize2_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 4;
    ve->defaults[1] = 16;
    ve->defaults[2] = 235;
    ve->defaults[3] = 0;

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 256;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 256;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 256;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 5;

    ve->description = "Posterize II (Threshold Range)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Factor",
        "Min Threshold",
        "Max Threshold",
        "Mode"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 2,                  32,                 6, 22, 1600, 3400, 700, 35,    /* Factor */
        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY,                        8,                  120,                6, 22, 1600, 3400, 700, 35,    /* Min Threshold */
        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY,                        135,                245,                6, 22, 1600, 3400, 700, 35,    /* Max Threshold */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Mode */
    );

    (void) w;
    (void) h;

    return ve;
}

static inline int posterize2_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

void posterize2_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    if(!frame || !args)
        return;

    int vfactor = posterize2_clampi(args[0], 1, 256);
    int t1 = posterize2_clampi(args[1], 0, 256);
    int t2 = posterize2_clampi(args[2], 0, 256);
    int mode = posterize2_clampi(args[3], 0, 5);

    if(t2 < t1) {
        int tmp = t1;
        t1 = t2;
        t2 = tmp;
    }

    const int len = frame->len;
    if(len <= 0)
        return;

    const int factor = 256 / vfactor;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A  = frame->data[3];

    if(!Y || !Cb || !Cr)
        return;

    if(mode >= 3 && !A)
        return;

    const uint8_t lo = pixel_Y_lo_;
    const uint8_t hi = pixel_Y_hi_;
    const uint8_t neutral = 128;

    uint8_t lutY[256];
    uint8_t mask[256];

    for(int i = 0; i < 256; i++) {
        const uint8_t v = (uint8_t)((i / factor) * factor);

        lutY[i] = v;
        mask[i] = 0;

        switch(mode) {
            case 0:
                if(v < t1 || v > t2) {
                    lutY[i] = lo;
                    mask[i] = 1;
                }
                break;

            case 1:
                if(v >= t1 && v <= t2)
                    mask[i] = 1;
                break;

            case 2:
                if(v < t1) {
                    lutY[i] = lo;
                    mask[i] = 1;
                } else if(v > t2) {
                    lutY[i] = hi;
                    mask[i] = 1;
                }
                break;

            default:
                break;
        }
    }

    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel num_threads(n_threads)
    {
        if(mode <= 2) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const uint8_t y_idx = Y[i];

                Y[i] = lutY[y_idx];

                if(mask[y_idx]) {
                    Cb[i] = neutral;
                    Cr[i] = neutral;
                }
            }
        } else if(mode == 3) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const uint8_t v = (uint8_t)(((int)Y[i] / factor) * factor);

                if(v < t1 || v > t2)
                    A[i] = lo;
            }
        } else if(mode == 4) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const uint8_t v = (uint8_t)(((int)Y[i] / factor) * factor);

                if(v >= t1 && v <= t2)
                    A[i] = v;
            }
        } else {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const uint8_t v = (uint8_t)(((int)Y[i] / factor) * factor);

                if(v < t1)
                    A[i] = lo;
                else if(v > t2)
                    A[i] = hi;
                else
                    A[i] = v;
            }
        }
    }
}