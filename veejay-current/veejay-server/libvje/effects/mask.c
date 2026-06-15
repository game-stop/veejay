/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include "mask.h"

#define SIMPLEMASK_PARAMS 2

#define P_THRESHOLD 0
#define P_MODE      1

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *simplemask_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = SIMPLEMASK_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_THRESHOLD] = 0; ve->limits[1][P_THRESHOLD] = 255; ve->defaults[P_THRESHOLD] = 128;
    ve->limits[0][P_MODE] = 0;      ve->limits[1][P_MODE] = 1;       ve->defaults[P_MODE] = 0;

    ve->description = "Binary Threshold Mask";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "Mode");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Threshold Black", "Threshold White");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    18,                 235,                14, 54,  800, 3000, 0,    82,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

static void simplemask_replace_black(uint8_t **restrict yuv1,
                                     uint8_t **restrict yuv2,
                                     int len,
                                     int threshold,
                                     int n_threads)
{
    uint8_t *restrict Y = yuv1[0];
    uint8_t *restrict Cb = yuv1[1];
    uint8_t *restrict Cr = yuv1[2];

    const uint8_t *restrict Y2 = yuv2[0];
    const uint8_t *restrict Cb2 = yuv2[1];
    const uint8_t *restrict Cr2 = yuv2[2];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int mask = -((int)Y[i] < threshold);

        Y[i] = (uint8_t)((Y2[i] & mask) | (Y[i] & ~mask));
        Cb[i] = (uint8_t)((Cb2[i] & mask) | (Cb[i] & ~mask));
        Cr[i] = (uint8_t)((Cr2[i] & mask) | (Cr[i] & ~mask));
    }
}

static void simplemask_replace_white(uint8_t **restrict yuv1,
                                     uint8_t **restrict yuv2,
                                     int len,
                                     int threshold,
                                     int n_threads)
{
    uint8_t *restrict Y = yuv1[0];
    uint8_t *restrict Cb = yuv1[1];
    uint8_t *restrict Cr = yuv1[2];

    const uint8_t *restrict Y2 = yuv2[0];
    const uint8_t *restrict Cb2 = yuv2[1];
    const uint8_t *restrict Cr2 = yuv2[2];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int mask = -((int)Y[i] > threshold);

        Y[i] = (uint8_t)((Y2[i] & mask) | (Y[i] & ~mask));
        Cb[i] = (uint8_t)((Cb2[i] & mask) | (Cb[i] & ~mask));
        Cr[i] = (uint8_t)((Cr2[i] & mask) | (Cr[i] & ~mask));
    }
}

void simplemask_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void) ptr;

    const int threshold = args[P_THRESHOLD];
    const int mode = args[P_MODE];
    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    if(mode == 0)
        simplemask_replace_black(frame->data, frame2->data, len, threshold, n_threads);
    else
        simplemask_replace_white(frame->data, frame2->data, len, threshold, n_threads);
}
