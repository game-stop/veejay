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
#include "opacityadv.h"

#define OPACITYADV_PARAMS 3

#define P_OPACITY 0
#define P_MIN_T   1
#define P_MAX_T   2

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t opacityadv_div255(int v)
{
    return (uint8_t)(((v + 128) + ((v + 128) >> 8)) >> 8);
}

vj_effect *opacityadv_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = OPACITYADV_PARAMS;
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

    ve->limits[0][P_OPACITY] = 0; ve->limits[1][P_OPACITY] = 255; ve->defaults[P_OPACITY] = 150;
    ve->limits[0][P_MIN_T] = 0;   ve->limits[1][P_MIN_T] = 255;   ve->defaults[P_MIN_T] = 40;
    ve->limits[0][P_MAX_T] = 0;   ve->limits[1][P_MAX_T] = 255;   ve->defaults[P_MAX_T] = 176;

    ve->description = "Soft-Edge Luma Key";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Min Threshold", "Max Threshold");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    18,                 245,                16, 62,  700, 2800, 0,    86,
        VJ_BEAT_DETAIL,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 8,                  118,                12, 46, 1000, 3600, 0,    64,
        VJ_BEAT_DETAIL,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    132,                248,                12, 46, 1000, 3600, 0,    64
    );

    return ve;
}

void opacityadv_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void)ptr;

    const int opacity = clampi(args[P_OPACITY], 0, 255);
    int tmin = clampi(args[P_MIN_T], 0, 255);
    int tmax = clampi(args[P_MAX_T], 0, 255);

    if(tmax < tmin) {
        const int tmp = tmin;
        tmin = tmax;
        tmax = tmp;
    }

    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y1 = frame->data[0];
    uint8_t *restrict U1 = frame->data[1];
    uint8_t *restrict V1 = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict U2 = frame2->data[1];
    const uint8_t *restrict V2 = frame2->data[2];

    if(opacity <= 0)
        return;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int y = Y1[i];
        int mask = 0;

        if(y >= tmin && y <= tmax)
            mask = 255;
        else if(y > tmin - 4 && y < tmin)
            mask = (y - (tmin - 4)) * 64;
        else if(y > tmax && y < tmax + 4)
            mask = ((tmax + 4) - y) * 64;

        if(mask > 255)
            mask = 255;

        const int w2 = opacityadv_div255(mask * opacity);

        if(w2 > 0) {
            const int w1 = 255 - w2;

            Y1[i] = opacityadv_div255(w1 * Y1[i] + w2 * Y2[i]);
            U1[i] = opacityadv_div255(w1 * U1[i] + w2 * U2[i]);
            V1[i] = opacityadv_div255(w1 * V1[i] + w2 * V2[i]);
        }
    }
}
