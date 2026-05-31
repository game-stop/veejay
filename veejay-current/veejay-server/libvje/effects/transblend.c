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

#include <libvje/effects/common.h>
#include <veejaycore/vjmem.h>
#include "transblend.h"
#include <libvje/internal.h>
#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    uint16_t *angle_lut;
    int progress_q16;
    int direction;
    int n_threads;
    int w;
    int h;
} wipe_t;

static inline int transblend_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *transblend_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int max_speed = (width > height) ? width : height;
    if(max_speed < 1)
        max_speed = 1;

    ve->defaults[0] = 1;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = max_speed;

    ve->defaults[1] = 1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->description = "Transition Wipe Clockwise";
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Speed",
        "Bounce"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Loop",
        "Bounce"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SPEED,    VJ_BEAT_F_CONTINUOUS,                   0,                  max_speed > 240 ? 240 : max_speed, 8, 30, 1200, 3000, 0,   50,    /* Speed */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,              0, 0,  0,    0,    0,   -1000  /* Bounce */
    );

    return ve;
}

static void transblend_build_angle_lut(wipe_t *wipe, int w, int h)
{
    const float cx = ((float)w - 1.0f) * 0.5f;
    const float cy = ((float)h - 1.0f) * 0.5f;
    const float scale = 65535.0f / (float)(2.0 * M_PI);

#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
    for(int y = 0; y < h; y++) {
        const int row = y * w;
        const float dy = (float)y - cy;

        for(int x = 0; x < w; x++) {
            const float dx = (float)x - cx;

            float a = atan2f(dx, -dy);
            if(a < 0.0f)
                a += (float)(2.0 * M_PI);

            wipe->angle_lut[row + x] = (uint16_t)(a * scale + 0.5f);
        }
    }

    wipe->w = w;
    wipe->h = h;
}

void *transblend_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    wipe_t *wipe = (wipe_t*) vj_calloc(sizeof(wipe_t));
    if(!wipe)
        return NULL;

    const int len = w * h;

    wipe->angle_lut = (uint16_t*) vj_malloc(sizeof(uint16_t) * (size_t)len);
    if(!wipe->angle_lut) {
        free(wipe);
        return NULL;
    }

    wipe->progress_q16 = 0;
    wipe->direction = 1;
    wipe->n_threads = vje_advise_num_threads(len);
    if(wipe->n_threads < 1)
        wipe->n_threads = 1;

    transblend_build_angle_lut(wipe, w, h);

    return wipe;
}

void transblend_free(void *ptr)
{
    wipe_t *wipe = (wipe_t*) ptr;

    if(!wipe)
        return;

    if(wipe->angle_lut)
        free(wipe->angle_lut);

    free(wipe);
}

static void transblend_step(wipe_t *wipe, int speed, int bounce, int w, int h)
{
    const int max_speed = transblend_clampi((w > h) ? w : h, 1, 65535);
    int step = (speed * 65535) / max_speed;

    if(speed > 0 && step < 1)
        step = 1;

    if(bounce) {
        wipe->progress_q16 += step * wipe->direction;

        if(wipe->progress_q16 >= 65535) {
            wipe->progress_q16 = 65535;
            wipe->direction = -1;
        } else if(wipe->progress_q16 <= 0) {
            wipe->progress_q16 = 0;
            wipe->direction = 1;
        }
    } else {
        wipe->progress_q16 += step;

        while(wipe->progress_q16 > 65535)
            wipe->progress_q16 -= 65536;

        wipe->direction = 1;
    }
}

void transblend_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    wipe_t *wipe = (wipe_t*) ptr;

    if(!wipe || !frame || !frame2 || !args ||
       !frame->data[0] || !frame->data[1] || !frame->data[2] ||
       !frame2->data[0] || !frame2->data[1] || !frame2->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    if(width != wipe->w || height != wipe->h)
        transblend_build_angle_lut(wipe, width, height);

    const int max_speed = (width > height) ? width : height;
    const int speed = transblend_clampi(args[0], 0, max_speed);
    const int bounce = args[1] ? 1 : 0;

    transblend_step(wipe, speed, bounce, width, height);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict U  = frame->data[1];
    uint8_t *restrict V  = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict U2 = frame2->data[1];
    const uint8_t *restrict V2 = frame2->data[2];

    const uint16_t *restrict angle = wipe->angle_lut;
    const uint16_t progress = (uint16_t)wipe->progress_q16;

#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
    for(int i = 0; i < len; i++) {
        if(angle[i] <= progress) {
            Y[i] = Y2[i];
            U[i] = U2[i];
            V[i] = V2[i];
        }
    }
}