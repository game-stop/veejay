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
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "wave.h"
vj_effect *wave_init(int w, int h) {
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;  //factor
    ve->defaults[0] = 10; 
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 100; //speed
    ve->defaults[1] = 1;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1; //deformX on/off
    ve->defaults[2] = 1;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1; //deformY on/off
    ve->defaults[3] = 1;

    ve->description = "Wave";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Factor", "Speed", "DeformX", "DeformY" );

    return ve;
}


typedef struct {
    uint8_t *buf[3];
    int16_t *lut_x;
    int16_t *lut_y;
    int width;
    int height;
    float factor;
    float speed;
    int deformX;
    int deformY;

    float lut_factor;
    float lut_speed;
    int lut_deformX;
    int lut_deformY;
} wave_t;


#define SIN_TABLE_SIZE 360


static inline int clamp(int v, int min, int max) {
    return (v < min) ? min : ((v > max) ? max : v);
}

void* wave_malloc(int w, int h) {
    wave_t *data = (wave_t*) vj_malloc(sizeof(wave_t));
    if (!data) return NULL;

    data->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3);
    if (!data->buf[0]) { free(data); return NULL; }

    data->lut_x = (int16_t*) vj_malloc(sizeof(int16_t) * w);
    if (!data->lut_x) { free(data->buf[0]); free(data); return NULL; }

    data->lut_y = (int16_t*) vj_malloc(sizeof(int16_t) * h);
    if (!data->lut_y) { free(data->buf[0]); free(data->lut_x); free(data); return NULL; }

    data->buf[1] = data->buf[0] + (w*h);
    data->buf[2] = data->buf[1] + (w*h);

    data->width = w;
    data->height = h;
    data->factor = 10.0f;
    data->speed = 1.0f;
    data->deformX = 1;
    data->deformY = 1;

    return data;
}

void wave_free(void *ptr) {
    wave_t *data = (wave_t*) ptr;
    if (!data) return;

    free(data->buf[0]);
    free(data->lut_x);
    free(data->lut_y);
    free(data);
}

static void wave_build_luts(wave_t *data, int width, int height, float factor, float speed, int deformX, int deformY) {
    if (factor == data->lut_factor &&
        speed == data->lut_speed &&
        deformX == data->lut_deformX &&
        deformY == data->lut_deformY)
        return;

    const float amplitude = factor;
    const float pulsation = 0.5f / factor;
    const float phase = factor * pulsation * speed / 10.0f;
    const float offsetX = phase + speed;
    const float offsetY = phase + speed;

    if (deformX) {
        for (int y = 0; y < height; y++) {
            data->lut_y[y] = (int16_t)(a_sin(pulsation * y + offsetY) * amplitude);
        }
    } else {
        veejay_memset(data->lut_y, 0, sizeof(int16_t) * height);
    }

    if (deformY) {
        for (int x = 0; x < width; x++) {
            data->lut_x[x] = (int16_t)(a_sin(pulsation * x * 2 + offsetX) * amplitude);
        }
    } else {
        veejay_memset(data->lut_x, 0, sizeof(int16_t) * width);
    }

    data->lut_factor = factor;
    data->lut_speed = speed;
    data->lut_deformX = deformX;
    data->lut_deformY = deformY;
}

void wave_apply(void *ptr, VJFrame *frame, int *args) {
    wave_t *data = (wave_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;

    const float factor = args[0] * 0.1f;
    const float speed_limit = args[1] * 0.1f;
    const int deformX = args[2];
    const int deformY = args[3];

    float speed = data->speed + 0.1f;
    data->speed = (speed > speed_limit) ? 1.0f : speed;

    uint8_t *restrict Y = frame->data[0] + frame->offset;
    uint8_t *restrict U = frame->data[1] + frame->offset;
    uint8_t *restrict V = frame->data[2] + frame->offset;

    uint8_t *restrict dstY = data->buf[0] + frame->offset;
    uint8_t *restrict dstU = data->buf[1] + frame->offset;
    uint8_t *restrict dstV = data->buf[2] + frame->offset;

    wave_build_luts(data, width, height, factor, speed, deformX, deformY);

    for (int y = 0; y < height; y++) {
        const int srcY_base = clamp(y + data->lut_y[y], 0, height - 1);
        const int src_row = srcY_base * width;
        const int dst_row = y * width;

        uint8_t *restrict pDstY = dstY + dst_row;
        uint8_t *restrict pDstU = dstU + dst_row;
        uint8_t *restrict pDstV = dstV + dst_row;

        const int16_t *restrict pLutX = data->lut_x;
        #pragma omp simd
        for (int x = 0; x < width; x++) {
            const int srcX = clamp(x + pLutX[x], 0, width - 1);
            const int srcIndex = src_row + srcX;

            pDstY[x] = Y[srcIndex];
            pDstU[x] = U[srcIndex];
            pDstV[x] = V[srcIndex];
        }
    }

    veejay_memcpy(Y, dstY, frame->len);
    veejay_memcpy(U, dstU, frame->len);
    veejay_memcpy(V, dstV, frame->len);
}