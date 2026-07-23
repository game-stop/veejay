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

/* Copyright (C) 2002 W.P. van Paassen - peter@paassen.tmfweb.nl */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "reflection.h"
#include <math.h>
#include <stdint.h>

#define REFLECTION_PARAMS 4

#define P_X    0
#define P_Y    1
#define P_MOVE 2
#define P_MODE 3

typedef struct {
    uint8_t *block;
    uint8_t *reflection_buffer;
    short *sin_x;
    short *sin_y;
    int *reflection_map;
    int sin_index;
    int sin_index2;
    int n_threads;
} reflection_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t reflection_chroma_scale(int mapv, uint8_t c)
{
    return (uint8_t)((((mapv * ((int)c - 128)) >> 8) + 128) & 0xff);
}

vj_effect *reflection_init(int width, int height)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = REFLECTION_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    ve->defaults[P_X] = 2;
    ve->defaults[P_Y] = 5;
    ve->defaults[P_MOVE] = 1;
    ve->defaults[P_MODE] = 0;

    ve->limits[0][P_X] = 0;
    ve->limits[1][P_X] = (width / 4) > 1 ? (width / 4) - 1 : 0;
    ve->limits[0][P_Y] = 0;
    ve->limits[1][P_Y] = (height / 4) > 1 ? (height / 4) - 1 : 0;
    ve->limits[0][P_MOVE] = 0;
    ve->limits[1][P_MOVE] = 1;
    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = 1;

    ve->description = "Bump 2D";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "X",
        "Y",
        "Move",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MOVE], P_MOVE, "Static", "Move");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Same Chroma", "Offset Chroma");

    int x_hi = ve->limits[1][P_X];
    int y_hi = ve->limits[1][P_Y];

    if(x_hi > 18)
        x_hi = 18;
    if(y_hi > 18)
        y_hi = 18;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 1, x_hi, 88, 100, 0, 320, 0, 1, 60, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 1, y_hi, 76, 94, 40, 520, 0, 1, 80, VJ_BEAT_COST_CHEAP, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *reflection_malloc(int width, int height)
{
    reflection_t *r = (reflection_t*)vj_calloc(sizeof(reflection_t));

    if(!r)
        return NULL;

    const int len = width * height;
    const size_t line_bytes = (size_t)width;
    const size_t sin_x_bytes = sizeof(short) * (size_t)width;
    const size_t sin_y_bytes = sizeof(short) * (size_t)height;
    const size_t map_bytes = sizeof(int) * (size_t)len;
    const size_t total = line_bytes + sin_x_bytes + sin_y_bytes + map_bytes + 64u;

    r->block = (uint8_t*)vj_malloc(total);

    if(!r->block) {
        free(r);
        return NULL;
    }

    uint8_t *p = r->block;

    r->reflection_buffer = p;
    p += line_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);
    r->sin_x = (short*)p;
    p += sin_x_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);
    r->sin_y = (short*)p;
    p += sin_y_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);
    r->reflection_map = (int*)p;

    r->sin_index = 0;
    r->sin_index2 = 0;
    r->n_threads = vje_advise_num_threads(len);

    const float hw = (float)width * 0.25f;
    const float hh = (float)height * 0.25f;
    const float m = (float)(width < height ? width : height) * 0.125f;
    const int half_w = width >> 1;
    const int half_h = height >> 1;

    for(int i = 0; i < width; i++) {
        const float rad = (float)i * (float)(M_PI / (double)width);
        r->sin_x[i] = (short)((a_sin(rad) * half_w) + half_w);
    }

    for(int i = 0; i < height; i++) {
        const float rad = (float)i * (float)(M_PI / (double)height);
        r->sin_y[i] = (short)((a_sin(rad) * half_h) + half_h);
    }

#pragma omp parallel for schedule(static) num_threads(r->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const float yy = ((float)y - hh) / hh;

        for(int x = 0; x < width; x++) {
            const float xx = ((float)x - hw) / hw;
            float zz = 1.0f - sqrtf(xx * xx + yy * yy);

            if(zz < 0.0f)
                zz = 0.0f;

            r->reflection_map[row + x] = (int)(zz * m);
        }
    }

    return (void*)r;
}

void reflection_free(void *ptr)
{
    reflection_t *r = (reflection_t*)ptr;

    free(r->block);
    free(r);
}

void reflection_apply(void *ptr, VJFrame *frame, int *args)
{
    reflection_t *r = (reflection_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;

    int index1 = args[P_X];
    int index2 = args[P_Y];
    const int move = clampi(args[P_MOVE], 0, 1);
    const int mode = clampi(args[P_MODE], 0, 1);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict prev_row = r->reflection_buffer;
    int *restrict map = r->reflection_map;

    if(!move) {
        r->sin_index = clampi(index1, 0, width - 1);
        r->sin_index2 = clampi(index2, 0, height - 1);
    }
    else {
        r->sin_index += index1;
        r->sin_index2 += index2;

        r->sin_index %= width;
        r->sin_index2 %= height;

        if(r->sin_index < 0)
            r->sin_index += width;
        if(r->sin_index2 < 0)
            r->sin_index2 += height;
    }

    const int lightx = r->sin_x[r->sin_index];
    const int lighty = r->sin_y[r->sin_index2] - (height >> 2);

    veejay_memcpy(prev_row, Y, width);

    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;
        const int temp = lighty - y;
        uint8_t p = Y[row];

        for(int x = 1; x < width - 1; x++) {
            const int i1 = (int)p;
            const int i2 = (int)Y[row + x + 1];
            const int i3 = (int)prev_row[x];

            int normalx = i2 - i1 + lightx - x;
            int normaly = i1 - i3 + temp;

            normalx = clampi(normalx, 0, width - 1);
            normaly = clampi(normaly, 0, height - 1);

            const int out = row + x;
            const int pos = normaly * width + normalx;
            const int chroma_x = x + mode < width ? x + mode : width - 1;
            const int mapv = map[pos];

            Y[out] = (uint8_t)clampi(mapv, 0, 255);
            Cb[out] = reflection_chroma_scale(mapv, Cb[row + chroma_x]);
            Cr[out] = reflection_chroma_scale(mapv, Cr[row + chroma_x]);

            p = (uint8_t)i2;
            prev_row[x] = (uint8_t)i2;
        }
    }
}
