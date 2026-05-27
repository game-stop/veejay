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

typedef struct {
    uint8_t *buf[3];
    int16_t *lut_x;
    int16_t *lut_y;
    int width;
    int height;
    float phase;
    int n_threads;
} wave_t;

static inline int wave_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *wave_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;
    ve->defaults[0] = 10;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 100;
    ve->defaults[1] = 1;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[2] = 1;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->defaults[3] = 1;

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
        "DeformY"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][2],
        2,
        "Off",
        "On"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][3],
        3,
        "Off",
        "On"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,     VJ_BEAT_F_CONTINUOUS,                         1,                  80,                 8, 30, 1200, 3000, 0,   55,    /* Factor */
        VJ_BEAT_SPEED,    VJ_BEAT_F_CONTINUOUS,                         1,                  70,                 8, 30, 1200, 3000, 0,   50,    /* Speed */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* DeformX */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* DeformY */
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

    data->lut_x = (int16_t*) vj_malloc(sizeof(int16_t) * (size_t)(w + h));
    if(!data->lut_x) {
        free(data->buf[0]);
        free(data);
        return NULL;
    }

    data->lut_y = data->lut_x + w;

    data->width = w;
    data->height = h;
    data->phase = 0.0f;

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

    if(data->lut_x)
        free(data->lut_x);

    free(data);
}

static void wave_build_luts(wave_t *data,
                            int width,
                            int height,
                            int factor_arg,
                            int deform_x,
                            int deform_y)
{
    const float amplitude = (float)factor_arg * 0.1f;
    const float pulsation = 0.5f / amplitude;
    const float phase = data->phase;

    if(deform_x) {
        for(int y = 0; y < height; y++)
            data->lut_y[y] = (int16_t)(a_sin(pulsation * (float)y + phase) * amplitude);
    } else {
        veejay_memset(data->lut_y, 0, sizeof(int16_t) * (size_t)height);
    }

    if(deform_y) {
        for(int x = 0; x < width; x++)
            data->lut_x[x] = (int16_t)(a_sin((pulsation * (float)x * 2.0f) + phase) * amplitude);
    } else {
        veejay_memset(data->lut_x, 0, sizeof(int16_t) * (size_t)width);
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

    const int factor = wave_clampi(args[0], 1, 100);
    const int speed = wave_clampi(args[1], 1, 100);
    const int deform_x = args[2] ? 1 : 0;
    const int deform_y = args[3] ? 1 : 0;

    if(!deform_x && !deform_y)
        return;

    data->phase += (float)speed * 0.015f;

    if(data->phase > 4096.0f)
        data->phase -= 4096.0f;

    wave_build_luts(data, width, height, factor, deform_x, deform_y);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict dstY = data->buf[0];
    uint8_t *restrict dstU = data->buf[1];
    uint8_t *restrict dstV = data->buf[2];

#pragma omp parallel for schedule(static) num_threads(data->n_threads)
    for(int y = 0; y < height; y++) {
        const int sy = wave_clampi(y + data->lut_y[y], 0, height - 1);
        const int src_row = sy * width;
        const int dst_row = y * width;

        for(int x = 0; x < width; x++) {
            const int sx = wave_clampi(x + data->lut_x[x], 0, width - 1);
            const int src = src_row + sx;
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