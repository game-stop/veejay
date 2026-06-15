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
#include "lumablend.h"

#define LUMABLEND_PARAMS 4

#define P_MODE       0
#define P_THRESH_A   1
#define P_THRESH_B   2
#define P_OPACITY    3

typedef struct {
    int n_threads;
} lumablend_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t lumablend_mix_q8(uint8_t a, uint8_t b, int q)
{
    return (uint8_t)(((int)a * (256 - q) + (int)b * q + 128) >> 8);
}

vj_effect *lumablend_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = LUMABLEND_PARAMS;
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

    ve->limits[0][P_MODE] = 0;     ve->limits[1][P_MODE] = 1;     ve->defaults[P_MODE] = 0;
    ve->limits[0][P_THRESH_A] = 0; ve->limits[1][P_THRESH_A] = 255; ve->defaults[P_THRESH_A] = 0;
    ve->limits[0][P_THRESH_B] = 0; ve->limits[1][P_THRESH_B] = 255; ve->defaults[P_THRESH_B] = 35;
    ve->limits[0][P_OPACITY] = 0;  ve->limits[1][P_OPACITY] = 255;  ve->defaults[P_OPACITY] = 150;

    ve->description = "Soft-Edge Luma Flow Mixer";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Threshold A", "Threshold B", "Opacity");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Source A", "Source B");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_DETAIL,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_DETAIL,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS,                    48,                 230,                8, 34,1200, 3200, 0,    46
    );

    return ve;
}

void *lumablend_malloc(int w, int h)
{
    lumablend_t *lb = (lumablend_t*) vj_malloc(sizeof(lumablend_t));

    if(!lb)
        return NULL;

    lb->n_threads = vje_advise_num_threads(w * h);

    return (void*) lb;
}

void lumablend_free(void *ptr)
{
    free(ptr);
}

void lumablend_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    lumablend_t *lb = (lumablend_t*) ptr;

    const int mode = args[P_MODE];
    const int a = args[P_THRESH_A];
    const int b = args[P_THRESH_B];
    const int t1 = a < b ? a : b;
    const int t2 = a < b ? b : a;
    const int opacity = args[P_OPACITY];

    if(opacity <= 0)
        return;

    const int len = frame->len;
    const int lo0 = t1 - 4;
    const int hi1 = t2 + 4;

    uint8_t *restrict y1 = frame->data[0];
    uint8_t *restrict u1 = frame->data[1];
    uint8_t *restrict v1 = frame->data[2];

    const uint8_t *restrict y2 = frame2->data[0];
    const uint8_t *restrict u2 = frame2->data[1];
    const uint8_t *restrict v2 = frame2->data[2];

#pragma omp parallel for schedule(static) num_threads(lb->n_threads)
    for(int i = 0; i < len; i++) {
        const int trigger = mode ? y2[i] : y1[i];
        int mask = 0;

        if(trigger >= t1 && trigger <= t2) {
            mask = 256;
        }
        else if(trigger > lo0 && trigger < t1) {
            mask = (trigger - lo0) << 6;
        }
        else if(trigger > t2 && trigger < hi1) {
            mask = (hi1 - trigger) << 6;
        }

        if(mask > 0) {
            const int q = (mask * opacity) >> 8;

            y1[i] = lumablend_mix_q8(y1[i], y2[i], q);
            u1[i] = lumablend_mix_q8(u1[i], u2[i], q);
            v1[i] = lumablend_mix_q8(v1[i], v2[i], q);
        }
    }
}
