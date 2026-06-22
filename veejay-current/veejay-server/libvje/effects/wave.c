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

#define WAVE_PARAMS 4

#define P_FACTOR   0
#define P_SPEED    1
#define P_DEFORM_X 2
#define P_DEFORM_Y 3

typedef struct {
    uint8_t *region;
    uint8_t *buf[3];
    int *map_x;
    int *map_y;
    int width;
    int height;
    float phase;
    float factor_env;
    float speed_env;
    int env_init;
    int n_threads;
} wave_t;

static inline int wave_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float wave_follow_f(float oldv, float target, float attack, float release)
{
    return target > oldv
        ? oldv + (target - oldv) * attack
        : oldv + (target - oldv) * release;
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
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_FACTOR] = 1;
    ve->limits[1][P_FACTOR] = 100;
    ve->defaults[P_FACTOR] = 18;

    ve->limits[0][P_SPEED] = 1;
    ve->limits[1][P_SPEED] = 100;
    ve->defaults[P_SPEED] = 8;

    ve->limits[0][P_DEFORM_X] = 0;
    ve->limits[1][P_DEFORM_X] = 1;
    ve->defaults[P_DEFORM_X] = 1;

    ve->limits[0][P_DEFORM_Y] = 0;
    ve->limits[1][P_DEFORM_Y] = 1;
    ve->defaults[P_DEFORM_Y] = 1;

    ve->description = "Wave";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Factor",
        "Speed",
        "DeformX",
        "DeformY"
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
        VJ_BEAT_WARP,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SQUARED | VJ_BEAT_F_NO_ZERO_CROSS, 8,                  72,                 16, 64,  800, 3200, 0,    86,
        VJ_BEAT_SPEED,    VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0, 0,    -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0, 0,    -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0, 0,    -1000
    );
    return ve;
}

void *wave_malloc(int w, int h)
{
    wave_t *data = (wave_t*) vj_calloc(sizeof(wave_t));
    if(!data)
        return NULL;

    const int len = w * h;
    const size_t frame_bytes = (size_t)len * 3u;
    const size_t map_bytes = sizeof(int) * (size_t)(w + h);
    const size_t total = frame_bytes + map_bytes + 16u;

    data->region = (uint8_t*) vj_malloc(total);
    if(!data->region) {
        free(data);
        return NULL;
    }

    data->buf[0] = data->region;
    data->buf[1] = data->buf[0] + len;
    data->buf[2] = data->buf[1] + len;

    uint8_t *p = data->buf[2] + len;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);

    data->map_x = (int*)p;
    data->map_y = data->map_x + w;

    data->width = w;
    data->height = h;
    data->phase = 0.0f;
    data->factor_env = 18.0f;
    data->speed_env = 8.0f;
    data->env_init = 0;

    data->n_threads = vje_advise_num_threads(len);

    return data;
}

void wave_free(void *ptr)
{
    wave_t *data = (wave_t*) ptr;

    free(data->region);
    free(data);
}

static void wave_build_maps(wave_t *data,
                            int width,
                            int height,
                            int factor_arg,
                            int deform_x,
                            int deform_y)
{
    const float factor = (float)factor_arg;
    const float amplitude = factor * 0.22f;
    const float wavelength = 28.0f + factor * 2.10f;
    const float pulsation = 6.28318530718f / wavelength;
    const float phase = data->phase;

    if(deform_y) {
        for(int x = 0; x < width; x++) {
            const int off = (int)(a_sin((pulsation * (float)x) + phase) * amplitude);
            data->map_x[x] = wave_clampi(x + off, 0, width - 1);
        }
    } else {
        for(int x = 0; x < width; x++)
            data->map_x[x] = x;
    }

    if(deform_x) {
        for(int y = 0; y < height; y++) {
            const int off = (int)(a_sin((pulsation * (float)y * 0.73f) + phase * 0.81f) * amplitude);
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

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int base_factor = args[P_FACTOR];
    const int base_speed = args[P_SPEED];
    const int deform_x = args[P_DEFORM_X] ? 1 : 0;
    const int deform_y = args[P_DEFORM_Y] ? 1 : 0;

    if(!deform_x && !deform_y)
        return;

    if(!data->env_init) {
        data->factor_env = (float)base_factor;
        data->speed_env = (float)base_speed;
        data->env_init = 1;
    }

    data->factor_env = wave_follow_f(data->factor_env, (float)base_factor, 0.115f, 0.060f);
    data->speed_env  = wave_follow_f(data->speed_env,  (float)base_speed,  0.105f, 0.055f);

    const int factor = wave_clampi((int)(data->factor_env + 0.5f), 1, 100);
    const int speed = wave_clampi((int)(data->speed_env + 0.5f), 1, 100);

    data->phase += (float)speed * 0.0065f;

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
