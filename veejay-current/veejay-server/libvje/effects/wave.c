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

/*
 * Look-up table optimized wave filter originally found in the MLT framework modules/kdenlive:
 */

/*
 * wave.c -- wave filter
 * Copyright (C) ?-2007 Leny Grisel <leny.grisel@laposte.net>
 * Copyright (C) 2007 Jean-Baptiste Mardelle <jb@ader.ch>
 * Copyright (c) 2022 Meltytech, LLC
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "wave.h"
#include <stdint.h>

#define WAVE_PARAMS 7

#define P_FACTOR      0
#define P_SPEED       1
#define P_DEFORM_X    2
#define P_DEFORM_Y    3
#define P_BEAT_WARP   4
#define P_BEAT_PUSH   5
#define P_BEAT_SMOOTH 6

typedef struct {
    uint8_t *buf[3];
    int *map_x;
    int *map_y;
    int width;
    int height;
    float phase;
    float beat_env;
    float beat_kick;
    float beat_prev;
    int n_threads;
} wave_t;

static inline int wave_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int wave_beat_shape(int beat_push)
{
    int sq;

    beat_push = wave_clampi(beat_push, 0, 1000);
    sq = (beat_push * beat_push + 500) / 1000;

    return wave_clampi((beat_push * 35 + sq * 65 + 50) / 100, 0, 1000);
}

vj_effect *wave_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = WAVE_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
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

    ve->limits[0][P_FACTOR] = 1;
    ve->limits[1][P_FACTOR] = 100;
    ve->defaults[P_FACTOR] = 10;

    ve->limits[0][P_SPEED] = 1;
    ve->limits[1][P_SPEED] = 100;
    ve->defaults[P_SPEED] = 1;

    ve->limits[0][P_DEFORM_X] = 0;
    ve->limits[1][P_DEFORM_X] = 1;
    ve->defaults[P_DEFORM_X] = 1;

    ve->limits[0][P_DEFORM_Y] = 0;
    ve->limits[1][P_DEFORM_Y] = 1;
    ve->defaults[P_DEFORM_Y] = 1;

    ve->limits[0][P_BEAT_WARP] = 0;
    ve->limits[1][P_BEAT_WARP] = 1000;
    ve->defaults[P_BEAT_WARP] = 520;

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->limits[0][P_BEAT_SMOOTH] = 0;
    ve->limits[1][P_BEAT_SMOOTH] = 1000;
    ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->description = "Wave";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Factor",
        "Speed",
        "DeformX",
        "DeformY",
        "Beat Warp",
        "Beat Push",
        "Beat Smooth"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_DEFORM_X],
        P_DEFORM_X,
        "Off",
        "On"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_DEFORM_Y],
        P_DEFORM_Y,
        "Off",
        "On"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,     VJ_BEAT_F_CONTINUOUS,
        1, 72, 8, 30, 1200, 3000, 0, 38, /* Factor */

        VJ_BEAT_SPEED,    VJ_BEAT_F_CONTINUOUS,
        1, 82, 10, 42, 900, 2400, 0, 58, /* Speed */

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,
        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,
        0, 0, 0, 0, 0, -1000, /* DeformX */

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,
        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,
        0, 0, 0, 0, 0, -1000, /* DeformY */

        VJ_BEAT_WARP,     VJ_BEAT_F_REJECT,
        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,
        0, 0, 0, 0, 0, -1000, /* Beat Warp */

        VJ_BEAT_KICK,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,
        0, 780, 18, 70, 80, 780, 0, 100, /* Beat Push */

        VJ_BEAT_MEMORY,   VJ_BEAT_F_PHRASE_ONLY,
        260, 840, 5, 18, 2200, 5200, 1200, 18 /* Beat Smooth */
    );

    (void) w;
    (void) h;

    return ve;
}

void *wave_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    wave_t *data = (wave_t*) vj_calloc(sizeof(wave_t));
    if(!data)
        return NULL;

    const int len = w * h;

    data->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!data->buf[0]) {
        free(data);
        return NULL;
    }

    data->buf[1] = data->buf[0] + len;
    data->buf[2] = data->buf[1] + len;

    data->map_x = (int*) vj_malloc(sizeof(int) * (size_t)(w + h));
    if(!data->map_x) {
        free(data->buf[0]);
        free(data);
        return NULL;
    }

    data->map_y = data->map_x + w;

    data->width = w;
    data->height = h;
    data->phase = 0.0f;
    data->beat_env = 0.0f;
    data->beat_kick = 0.0f;
    data->beat_prev = 0.0f;

    data->n_threads = vje_advise_num_threads(len);
    if(data->n_threads < 1)
        data->n_threads = 1;

    return data;
}

void wave_free(void *ptr)
{
    wave_t *data = (wave_t*) ptr;

    if(!data)
        return;

    if(data->buf[0])
        free(data->buf[0]);

    if(data->map_x)
        free(data->map_x);

    free(data);
}

static void wave_build_maps(wave_t *data,
                            int width,
                            int height,
                            int factor_arg,
                            int deform_x,
                            int deform_y)
{
    const float amplitude = (float)factor_arg * 0.1f;
    const float pulsation = 0.5f / amplitude;
    const float phase = data->phase;

    if(deform_y) {
        for(int x = 0; x < width; x++) {
            const int off = (int)(a_sin((pulsation * (float)x * 2.0f) + phase) * amplitude);
            data->map_x[x] = wave_clampi(x + off, 0, width - 1);
        }
    } else {
        for(int x = 0; x < width; x++)
            data->map_x[x] = x;
    }

    if(deform_x) {
        for(int y = 0; y < height; y++) {
            const int off = (int)(a_sin(pulsation * (float)y + phase) * amplitude);
            data->map_y[y] = wave_clampi(y + off, 0, height - 1);
        }
    } else {
        for(int y = 0; y < height; y++)
            data->map_y[y] = y;
    }
}

void wave_apply(void *ptr, VJFrame *frame, int *args)
{
    wave_t *data = (wave_t*) ptr;

    if(!data || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    if(width != data->width || height != data->height)
        return;

    const int base_factor = wave_clampi(args[P_FACTOR], 1, 100);
    const int base_speed = wave_clampi(args[P_SPEED], 1, 100);
    const int deform_x = args[P_DEFORM_X] ? 1 : 0;
    const int deform_y = args[P_DEFORM_Y] ? 1 : 0;
    const int beat_warp = wave_clampi(args[P_BEAT_WARP], 0, 1000);
    const int beat_push = wave_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = wave_clampi(args[P_BEAT_SMOOTH], 0, 1000);

    if(!deform_x && !deform_y)
        return;

    {
        const int shaped = wave_beat_shape(beat_push);
        const float target = (float)shaped * 0.001f;
        const float smooth_t = (float)beat_smooth * 0.001f;
        const float attack = 0.16f + (1.0f - smooth_t) * 0.36f;
        const float release = 0.030f + (1.0f - smooth_t) * 0.095f;

        if(target > data->beat_env) {
            const float rise = target - data->beat_prev;
            if(rise > 0.001f) {
                data->beat_kick += rise * 0.72f;
                if(data->beat_kick > 1.0f)
                    data->beat_kick = 1.0f;
            }

            data->beat_env += (target - data->beat_env) * attack;
        } else {
            data->beat_env += (target - data->beat_env) * release;
        }

        data->beat_prev = target;
        data->beat_kick *= 0.60f + smooth_t * 0.25f;

        if(data->beat_env < 0.0001f)
            data->beat_env = 0.0f;
        else if(data->beat_env > 1.0f)
            data->beat_env = 1.0f;

        if(data->beat_kick < 0.0001f)
            data->beat_kick = 0.0f;
    }

    const int beat_q = wave_clampi((int)(data->beat_env * 1000.0f + 0.5f), 0, 1000);
    const int kick_q = wave_clampi((int)(data->beat_kick * 1000.0f + 0.5f), 0, 1000);

    const int factor_add =
        (int)(((int64_t)beat_q * beat_warp * 55 + 500000) / 1000000) +
        (int)(((int64_t)kick_q * beat_warp * 18 + 500000) / 1000000);

    const int speed_add =
        ((beat_q * 30 + 500) / 1000) +
        ((kick_q * 42 + 500) / 1000);

    const int factor = wave_clampi(base_factor + factor_add, 1, 100);
    const int speed = wave_clampi(base_speed + speed_add, 1, 100);

    data->phase += (float)speed * 0.015f;
    data->phase += data->beat_kick * ((float)beat_warp * 0.00070f);

    if(data->phase > 4096.0f)
        data->phase -= 4096.0f;

    wave_build_maps(data, width, height, factor, deform_x, deform_y);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict dstY = data->buf[0];
    uint8_t *restrict dstU = data->buf[1];
    uint8_t *restrict dstV = data->buf[2];

    const int *restrict map_x = data->map_x;
    const int *restrict map_y = data->map_y;

#pragma omp parallel for schedule(static) num_threads(data->n_threads)
    for(int y = 0; y < height; y++) {
        const int src_row = map_y[y] * width;
        const int dst_row = y * width;

        for(int x = 0; x < width; x++) {
            const int src = src_row + map_x[x];
            const int dst = dst_row + x;

            dstY[dst] = Y[src];
            dstU[dst] = U[src];
            dstV[dst] = V[src];
        }
    }

    veejay_memcpy(Y, dstY, len);
    veejay_memcpy(U, dstU, len);
    veejay_memcpy(V, dstV, len);
}
