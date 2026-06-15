/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 */

#include "common.h"
#include "distort.h"

#include <math.h>

#define DISTORT_TABLE_SIZE 512
#define DISTORT_TABLE_MASK 511

typedef struct {
    int plasma_table[DISTORT_TABLE_SIZE];
    int plasma_pos1;
    int plasma_pos2;
    uint8_t *plasma_buf[4];
    int n_threads;
} distortion_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int distortion_scale_wave(int wave, int amp)
{
    if(wave >= 0)
        return (wave * amp) >> 11;

    return -(((-wave) * amp) >> 11);
}

static inline int distortion_reflect_coord(int v, int limit)
{
    if(limit <= 1)
        return 0;

    const int last = limit - 1;
    const int period = last << 1;

    if(v < 0 || v > last)
    {
        v %= period;

        if(v < 0)
            v += period;

        if(v > last)
            v = period - v;
    }

    return v;
}

vj_effect *distortion_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 5;
    ve->defaults[1] = 3;
    ve->defaults[2] = 3;
    ve->defaults[3] = 1;
    ve->defaults[4] = 9;
    ve->defaults[5] = 8;

    for(int i = 0; i < ve->num_params; i++)
    {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 0xff;
    }

    ve->description = "Distortion (Plasma Grid)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "X Frequency", "Y Frequency", "Grid Bend", "Grid Twist", "Phase Speed", "Phase Drift");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 2,  82,  10, 38, 1100, 3600, 0, 58,
        VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 2,  82,  10, 38, 1100, 3600, 0, 58,
        VJ_BEAT_WARP,               VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 2,  132, 14, 56,  800, 2800, 0, 82,
        VJ_BEAT_FLOW,               VJ_BEAT_F_CONTINUOUS,                         0,  118, 12, 48,  900, 3200, 0, 70,
        VJ_BEAT_SPEED,              VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 1,  42,  14, 58,  700, 2600, 0, 86,
        VJ_BEAT_DRIFT,              VJ_BEAT_F_CONTINUOUS,                         0,  34,  10, 42,  900, 3200, 0, 58
    );
    return ve;
}

void *distortion_malloc(int w, int h)
{
    distortion_t *d = (distortion_t*) vj_calloc(sizeof(distortion_t));

    if(!d)
        return NULL;

    const int len = w * h;

    if(len <= 0)
    {
        free(d);
        return NULL;
    }

    d->plasma_buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * len * 3);

    if(!d->plasma_buf[0])
    {
        free(d);
        return NULL;
    }

    d->plasma_buf[1] = d->plasma_buf[0] + len;
    d->plasma_buf[2] = d->plasma_buf[1] + len;
    d->plasma_buf[3] = NULL;

    for(int i = 0; i < DISTORT_TABLE_SIZE; i++)
    {
        const float rad = ((float)i * 0.703125f) * 0.01745329252f;
        d->plasma_table[i] = myround(sinf(rad) * 1024.0f);
    }

    d->n_threads = vje_advise_num_threads(len);

    return d;
}

void distortion_free(void *ptr)
{
    distortion_t *d = (distortion_t*) ptr;

    if(!d)
        return;

    if(d->plasma_buf[0])
        free(d->plasma_buf[0]);

    free(d);
}

void distortion_apply(void *ptr, VJFrame *frame, int *args)
{
    distortion_t *d = (distortion_t*) ptr;
    const int inc_val1 = args[0];
    const int inc_val2 = args[1];
    const int inc_val3 = args[2];
    const int inc_val4 = args[3];
    const int inc_val5 = args[4];
    const int inc_val6 = args[5];

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    int strides[4] = { len, len, len, 0 };

    vj_frame_copy(frame->data, d->plasma_buf, strides);

    const uint8_t *restrict srcY = d->plasma_buf[0];
    const uint8_t *restrict srcCb = d->plasma_buf[1];
    const uint8_t *restrict srcCr = d->plasma_buf[2];
    const int *restrict plasma_table = d->plasma_table;

    int amp_x = w >> 4;
    int amp_y = h >> 4;

    amp_x = clampi(amp_x, 2, 96);
    amp_y = clampi(amp_y, 2, 96);

    const int plasma_pos1 = d->plasma_pos1 & DISTORT_TABLE_MASK;
    const int plasma_pos2 = d->plasma_pos2 & DISTORT_TABLE_MASK;

    #pragma omp parallel for schedule(static) num_threads(d->n_threads)
    for(int y = 0; y < h; y++)
    {
        int tpos1 = (plasma_pos1 + y * inc_val3) & DISTORT_TABLE_MASK;
        int tpos2 = (plasma_pos2 + y * inc_val4) & DISTORT_TABLE_MASK;
        const int row_phase1 = (plasma_pos2 + y * inc_val3) & DISTORT_TABLE_MASK;
        const int row_phase2 = (plasma_pos1 + y * inc_val4) & DISTORT_TABLE_MASK;
        const int row_wave1 = plasma_table[row_phase1];
        const int row_wave2 = plasma_table[row_phase2];
        const int row = y * w;

        for(int x = 0; x < w; x++)
        {
            const int wave_x = plasma_table[tpos1 & DISTORT_TABLE_MASK] + row_wave2;
            const int wave_y = plasma_table[tpos2 & DISTORT_TABLE_MASK] + row_wave1;
            const int dx = distortion_scale_wave(wave_x, amp_x);
            const int dy = distortion_scale_wave(wave_y, amp_y);
            const int sx = distortion_reflect_coord(x + dx, w);
            const int sy = distortion_reflect_coord(y + dy, h);
            const int dst = row + x;
            const int src = sy * w + sx;

            Y[dst] = srcY[src];
            Cb[dst] = srcCb[src];
            Cr[dst] = srcCr[src];

            tpos1 += inc_val1;
            tpos2 += inc_val2;
        }
    }

    d->plasma_pos1 = (plasma_pos1 + inc_val5) & DISTORT_TABLE_MASK;
    d->plasma_pos2 = (plasma_pos2 + inc_val6) & DISTORT_TABLE_MASK;
}
