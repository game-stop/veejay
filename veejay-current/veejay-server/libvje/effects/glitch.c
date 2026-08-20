/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "glitch.h"

#define GLITCH_PARAMS 8

#define P_AMPLITUDE    0
#define P_NOISE_GAIN   1
#define P_NOISE_QTY    2
#define P_NOISE_SCALE  3
#define P_INTERVAL     4
#define P_DISTORT_X    5
#define P_DISTORT_Y    6
#define P_DURATION     7

typedef struct {
    uint8_t *buf[3];
    int *rand_lut;
    int *lsfr_lut;
    int frame_count;
    int n_threads;
} glitch_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int glitch_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline int glitch_wrap_once(int v, int max)
{
    v = v < 0 ? v + max : v;
    v = v >= max ? v - max : v;
    return v;
}

static inline uint8_t glitch_blend255(uint8_t a, uint8_t b, int opacity)
{
    const int inv = 255 - opacity;
    const int x = (int)a * inv + (int)b * opacity;
    return (uint8_t)(((x + 1) + (x >> 8)) >> 8);
}

vj_effect *glitch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = GLITCH_PARAMS;
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

    ve->limits[0][P_AMPLITUDE] = 1;    ve->limits[1][P_AMPLITUDE] = 360;   ve->defaults[P_AMPLITUDE] = 20;
    ve->limits[0][P_NOISE_GAIN] = 1;   ve->limits[1][P_NOISE_GAIN] = 10;   ve->defaults[P_NOISE_GAIN] = 2;
    ve->limits[0][P_NOISE_QTY] = 1;    ve->limits[1][P_NOISE_QTY] = 100;   ve->defaults[P_NOISE_QTY] = 2;
    ve->limits[0][P_NOISE_SCALE] = 1;  ve->limits[1][P_NOISE_SCALE] = 200; ve->defaults[P_NOISE_SCALE] = 100;
    ve->limits[0][P_INTERVAL] = 1;     ve->limits[1][P_INTERVAL] = 500;    ve->defaults[P_INTERVAL] = 20;
    ve->limits[0][P_DISTORT_X] = -100; ve->limits[1][P_DISTORT_X] = 100;   ve->defaults[P_DISTORT_X] = 2;
    ve->limits[0][P_DISTORT_Y] = -100; ve->limits[1][P_DISTORT_Y] = 100;   ve->defaults[P_DISTORT_Y] = 2;
    ve->limits[0][P_DURATION] = 0;     ve->limits[1][P_DURATION] = 500;    ve->defaults[P_DURATION] = 5;

    ve->description = "Glitch";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Amplitude",
        "Noise Strength",
        "Noise Quantity",
        "Noise Scale",
        "Interval",
        "Distortion X",
        "Distortion Y",
        "Duration"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 12, 340, 92, 100, 0, 360, 0, 2, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 1, 10, 82, 100, 0, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, 90, 86, 100, 10, 440, 0, 1, 0, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 10, 190, 70, 96, 10, 520, 0, 2, 120, VJ_BEAT_COST_MODERATE, 70, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BPM, VJ_BEAT_OP_BEAT_TIME, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, 2, 180, 100, 100, 0, 0, 0, 1, 120, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, -95, 95, 88, 100, 8, 400, 0, 1, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_BIPOLAR, VJ_BEAT_CURVE_SMOOTHSTEP, -85, 85, 72, 96, 20, 620, 0, 1, 0, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, 90, 82, 100, 10, 520, 0, 1, 80, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *glitch_malloc(int w, int h)
{
    const int len = w * h;

    glitch_t *g = (glitch_t *) vj_calloc(sizeof(glitch_t));

    if(!g)
        return NULL;

    g->buf[0] = (uint8_t *) vj_malloc((size_t)len * 3u);

    if(!g->buf[0]) {
        free(g);
        return NULL;
    }

    g->buf[1] = g->buf[0] + len;
    g->buf[2] = g->buf[1] + len;

    g->rand_lut = (int *) vj_malloc(sizeof(int) * (size_t)len * 2u);

    if(!g->rand_lut) {
        free(g->buf[0]);
        free(g);
        return NULL;
    }

    g->lsfr_lut = g->rand_lut + len;
    g->frame_count = 0;
    g->n_threads = vje_advise_num_threads(len);

    for(int i = 0; i < len; i++)
        g->rand_lut[i] = rand();

    veejay_memcpy(g->lsfr_lut, g->rand_lut, sizeof(int) * (size_t)len);

    return (void *) g;
}

void glitch_free(void *ptr)
{
    glitch_t *g = (glitch_t *) ptr;

    free(g->buf[0]);
    free(g->rand_lut);
    free(g);
}

void glitch_apply(void *ptr, VJFrame *frame, int *args)
{
    glitch_t *g = (glitch_t *) ptr;

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;
    const int amplitude = args[P_AMPLITUDE];
    const int noise_gain = args[P_NOISE_GAIN];
    const int noise_qty = args[P_NOISE_QTY];
    const int noise_scale = args[P_NOISE_SCALE];
    const int interval = args[P_INTERVAL];
    const int distortion_x_arg = args[P_DISTORT_X];
    const int distortion_y_arg = args[P_DISTORT_Y];
    const int duration = args[P_DURATION];

    int phase = g->frame_count;
    g->frame_count = phase + 1;

    if(g->frame_count >= interval)
        g->frame_count = 0;

    if(phase >= interval)
        phase = 0;

    if(duration > 0 && phase >= duration)
        return;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict bY = g->buf[0];
    uint8_t *restrict bU = g->buf[1];
    uint8_t *restrict bV = g->buf[2];

    int *restrict rand_lut = g->rand_lut;
    int *restrict lsfr_lut = g->lsfr_lut;

    veejay_memcpy(bU, U, len);
    veejay_memcpy(bV, V, len);

    const int half_qty = noise_qty >> 1;
    const int noise_mul = amplitude * noise_gain * noise_scale;

    int distortion_x = (distortion_x_arg * width) / 100;
    int distortion_y = (distortion_y_arg * height) / 100;

    if(distortion_x <= -width || distortion_x >= width)
        distortion_x %= width;

    if(distortion_y <= -height || distortion_y >= height)
        distortion_y %= height;

    const int chroma_alpha_base = clampi(glitch_absi(distortion_x_arg) + glitch_absi(distortion_y_arg), 0, 200);
    const int chroma_alpha_scale = 96 + ((chroma_alpha_base * 159) / 200);

#pragma omp parallel num_threads(g->n_threads)
    {
        if(phase == 0) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                rand_lut[i] = fastrand(rand_lut[i]);
                lsfr_lut[i] = rand_lut[i];
            }
        }

#pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            const int q = lsfr_lut[i] % noise_qty;
            const int centered = q - half_qty;
            const int noise = (centered * noise_mul) / 100;

            bY[i] = CLAMP_Y((int)Y[i] + noise);
            lsfr_lut[i] = fastrand(lsfr_lut[i]);
        }

#pragma omp for schedule(static)
        for(int y = 0; y < height; y++) {
            const int ny = glitch_wrap_once(y + distortion_y, height);
            const int row = y * width;
            const int drow = ny * width;

#pragma omp simd
            for(int x = 0; x < width; x++) {
                const int nx = glitch_wrap_once(x + distortion_x, width);
                const int src = row + x;
                const int dst = drow + nx;
                const int alpha = ((int)Y[dst] * chroma_alpha_scale) >> 8;

                U[src] = glitch_blend255(U[src], bU[dst], 255 - alpha);
                V[src] = glitch_blend255(V[src], bV[dst], 255 - alpha);
                Y[src] = (uint8_t)(((int)Y[src] + (int)bY[dst] + 1) >> 1);
            }
        }
    }
}
