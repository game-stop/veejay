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

/* Copyright (C) 2002 W.P. van Paassen - peter@paassen.tmfweb.nl */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "reflection.h"
#include <math.h>

typedef struct {
    short *sin_x;
    short *sin_y;
    int *reflection_map;
    int sin_index;
    int sin_index2;
    uint8_t *reflection_buffer;
    int n_threads;
} reflection_t;

vj_effect *reflection_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 2;
    ve->defaults[1] = 5;
    ve->defaults[2] = 1;
    ve->defaults[3] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = (width / 4) > 1 ? (width / 4) - 1 : 0;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = (height / 4) > 1 ? (height / 4) - 1 : 0;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;

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
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][2],
        2,
        "Static",
        "Move"
    );
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][3],
        3,
        "Same Chroma",
        "Offset Chroma"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DRIFT,    VJ_BEAT_F_CONTINUOUS,                          0,                  ve->limits[1][0],   8,  30, 1200, 3000, 0,   45,    /* X */
        VJ_BEAT_DRIFT,    VJ_BEAT_F_CONTINUOUS,                          0,                  ve->limits[1][1],   8,  30, 1200, 3000, 0,   45,    /* Y */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Move */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000  /* Mode */
    );

    return ve;
}

void *reflection_malloc(int width, int height)
{
    reflection_t *r = (reflection_t*) vj_calloc(sizeof(reflection_t));
    if(!r)
        return NULL;

    r->reflection_buffer = (uint8_t*) vj_malloc(sizeof(uint8_t) * width);
    if(!r->reflection_buffer) {
        free(r);
        return NULL;
    }

    r->sin_x = (short*) vj_malloc(sizeof(short) * width);
    if(!r->sin_x) {
        free(r->reflection_buffer);
        free(r);
        return NULL;
    }

    r->sin_y = (short*) vj_malloc(sizeof(short) * height);
    if(!r->sin_y) {
        free(r->sin_x);
        free(r->reflection_buffer);
        free(r);
        return NULL;
    }

    r->reflection_map = (int*) vj_malloc(sizeof(int) * width * height);
    if(!r->reflection_map) {
        free(r->sin_y);
        free(r->sin_x);
        free(r->reflection_buffer);
        free(r);
        return NULL;
    }

    r->sin_index = 0;
    r->sin_index2 = 0;
    r->n_threads = vje_advise_num_threads(width * height);
    if(r->n_threads < 1)
        r->n_threads = 1;

    const float hw = (float)width * 0.25f;
    const float hh = (float)height * 0.25f;
    const float m = (float)(width < height ? width : height) * 0.125f;
    const int half_w = width >> 1;
    const int half_h = height >> 1;

    for(int i = 0; i < width; i++) {
        float rad = (float)i * (M_PI / (float)width);
        r->sin_x[i] = (short)((a_sin(rad) * half_w) + half_w);
    }

    for(int i = 0; i < height; i++) {
        float rad = (float)i * (M_PI / (float)height);
        r->sin_y[i] = (short)((a_sin(rad) * half_h) + half_h);
    }

#pragma omp parallel for schedule(static) num_threads(r->n_threads)
    for(int y = 0; y < height; y++) {
        for(int x = 0; x < width; x++) {
            float xx = hw > 0.0f ? ((float)x - hw) / hw : 0.0f;
            float yy = hh > 0.0f ? ((float)y - hh) / hh : 0.0f;
            float zz = 1.0f - sqrtf(xx * xx + yy * yy);

            if(zz < 0.0f)
                zz = 0.0f;

            r->reflection_map[y * width + x] = (int)(zz * m);
        }
    }

    return (void*) r;
}

void reflection_free(void *ptr)
{
    reflection_t *r = (reflection_t*) ptr;
    if(!r)
        return;

    if(r->reflection_map)
        free(r->reflection_map);
    if(r->reflection_buffer)
        free(r->reflection_buffer);
    if(r->sin_x)
        free(r->sin_x);
    if(r->sin_y)
        free(r->sin_y);

    free(r);
}

static inline int reflection_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t reflection_chroma_scale(int mapv, uint8_t c)
{
    return (uint8_t)((((mapv * ((int)c - 128)) >> 8) + 128) & 0xff);
}

void reflection_apply(void *ptr, VJFrame *frame, int *args)
{
    reflection_t *r = (reflection_t*) ptr;
    if(!r || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;

    if(width < 3 || height < 3)
        return;

    int index1 = args[0];
    int index2 = args[1];
    int move = reflection_clampi(args[2], 0, 1);
    int mode = reflection_clampi(args[3], 0, 1);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict prev_row = r->reflection_buffer;
    int *restrict map = r->reflection_map;

    if(!move) {
        r->sin_index = reflection_clampi(index1, 0, width - 1);
        r->sin_index2 = reflection_clampi(index2, 0, height - 1);
    } else {
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

            normalx = reflection_clampi(normalx, 0, width - 1);
            normaly = reflection_clampi(normaly, 0, height - 1);

            const int pos = normaly * width + normalx;
            const int out = row + x;
            const int chroma_x = x + mode < width ? x + mode : width - 1;
            const int chroma_pos = row + chroma_x;
            const int mapv = map[pos];

            Y[out]  = (uint8_t)reflection_clampi(mapv, 0, 255);
            Cb[out] = reflection_chroma_scale(mapv, Cb[chroma_pos]);
            Cr[out] = reflection_chroma_scale(mapv, Cr[chroma_pos]);

            p = (uint8_t)i2;
            prev_row[x] = (uint8_t)i2;
        }
    }
}