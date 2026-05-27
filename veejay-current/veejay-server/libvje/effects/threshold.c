/* 
 * Linux VeeJay
 *
 * Copyright(C)2006 Niels Elburg <nwelburg@gmail.com>
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
#include "threshold.h"

typedef struct {
    uint8_t *mask;
    int n_threads;
} threshold_t;

static inline int threshold_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *threshold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 40;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[1] = 0;

    ve->description = "Map B from threshold mask";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Reverse"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Normal",
        "Reverse"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS,                         0,                  180,                8, 30, 1200, 3000, 0,   55,    /* Threshold */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Reverse */
    );

    (void) w;
    (void) h;

    return ve;
}

void *threshold_malloc(int w, int h)
{
    threshold_t *t = (threshold_t*) vj_calloc(sizeof(threshold_t));
    if(!t)
        return NULL;

    const int len = w * h;

    t->mask = (uint8_t*) vj_malloc((size_t)len);
    if(!t->mask) {
        free(t);
        return NULL;
    }

    t->n_threads = vje_advise_num_threads(len);
    if(t->n_threads < 1)
        t->n_threads = 1;

    return (void*) t;
}

void threshold_free(void *ptr)
{
    threshold_t *t = (threshold_t*) ptr;

    if(!t)
        return;

    if(t->mask)
        free(t->mask);

    free(t);
}

static void threshold_build_soft_mask(threshold_t *t, const uint8_t *restrict Y, int w, int h)
{
    uint8_t *restrict mask = t->mask;

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for(int y = 0; y < h; y++) {
        const int ym = (y > 0) ? y - 1 : y;
        const int yp = (y < h - 1) ? y + 1 : y;

        const uint8_t *restrict r0 = Y + ym * w;
        const uint8_t *restrict r1 = Y + y  * w;
        const uint8_t *restrict r2 = Y + yp * w;

        uint8_t *restrict out = mask + y * w;

        for(int x = 0; x < w; x++) {
            const int xm = (x > 0) ? x - 1 : x;
            const int xp = (x < w - 1) ? x + 1 : x;

            const int sum =
                (int)r0[xm] + (int)r0[x] + (int)r0[xp] +
                (int)r1[xm] + (int)r1[x] + (int)r1[xp] +
                (int)r2[xm] + (int)r2[x] + (int)r2[xp];

            out[x] = (uint8_t)((sum + 4) / 9);
        }
    }
}

void threshold_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    threshold_t *t = (threshold_t*) ptr;

    if(!t || !frame || !frame2 || !args ||
       !frame->data[0] || !frame->data[1] || !frame->data[2] ||
       !frame2->data[0] || !frame2->data[1] || !frame2->data[2])
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    if(w <= 0 || h <= 0 || len <= 0)
        return;

    const int threshold = threshold_clampi(args[0], 0, 255);
    const int reverse = args[1] ? 1 : 0;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2  = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    threshold_build_soft_mask(t, Y, w, h);

    uint8_t *restrict mask = t->mask;

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for(int i = 0; i < len; i++) {
        const int white = mask[i] > threshold;
        const int take_b = reverse ? !white : white;

        if(take_b) {
            Y[i]  = Y2[i];
            Cb[i] = Cb2[i];
            Cr[i] = Cr2[i];
        } else {
            Y[i]  = 0;
            Cb[i] = 128;
            Cr[i] = 128;
        }
    }
}