/*
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "strobo.h"

#define STROBO_PARAMS 9

#define P_THRESHOLD   0
#define P_DURATION    1
#define P_OPACITY     2
#define P_ECHOES      3
#define P_MODE        4
#define P_DELAY       5
#define P_BEAT_PUSH   6
#define P_BEAT_SMOOTH 7
#define P_BEAT_COLOR  8

typedef struct {
    uint8_t *buf[3];
    int timestamp;
    int n_threads;
    float beat_env;
    float beat_kick;
    float beat_prev;
} strobo_t;

static const struct {
    int r;
    int g;
    int b;
} strobo_rainbow[] = {
    {255, 0, 0},
    {255, 127, 0},
    {255, 255, 0},
    {0, 255, 0},
    {0, 0, 255},
    {75, 0, 130},
    {148, 0, 211},
    {128, 0, 0},
    {255, 69, 0},
    {255, 140, 0},
    {255, 255, 255},
    {0, 128, 0},
    {0, 0, 128},
    {139, 69, 19}
};

static inline int strobo_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t strobo_blend_y(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t strobo_blend_uv(uint8_t a, uint8_t b, int q8)
{
    const int ac = (int)a - 128;
    const int bc = (int)b - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return (uint8_t)CLAMP_UV(v);
}

static inline int strobo_beat_shape(int beat_push)
{
    beat_push = strobo_clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return strobo_clampi((beat_push * 42 + sq * 58 + 50) / 100, 0, 1000);
}

vj_effect *strobo_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = STROBO_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[P_THRESHOLD]   = 70;
    ve->defaults[P_DURATION]    = 10;
    ve->defaults[P_OPACITY]     = 150;
    ve->defaults[P_ECHOES]      = 3;
    ve->defaults[P_MODE]        = 0;
    ve->defaults[P_DELAY]       = 0;
    ve->defaults[P_BEAT_PUSH]   = 0;
    ve->defaults[P_BEAT_SMOOTH] = 520;
    ve->defaults[P_BEAT_COLOR]  = 360;

    ve->limits[0][P_THRESHOLD]   = 0; ve->limits[1][P_THRESHOLD]   = 255;
    ve->limits[0][P_DURATION]    = 1; ve->limits[1][P_DURATION]    = 1500;
    ve->limits[0][P_OPACITY]     = 0; ve->limits[1][P_OPACITY]     = 255;
    ve->limits[0][P_ECHOES]      = 1; ve->limits[1][P_ECHOES]      = 100;
    ve->limits[0][P_MODE]        = 0; ve->limits[1][P_MODE]        = 1;
    ve->limits[0][P_DELAY]       = 0; ve->limits[1][P_DELAY]       = 1500;
    ve->limits[0][P_BEAT_PUSH]   = 0; ve->limits[1][P_BEAT_PUSH]   = 1000;
    ve->limits[0][P_BEAT_SMOOTH] = 0; ve->limits[1][P_BEAT_SMOOTH] = 1000;
    ve->limits[0][P_BEAT_COLOR]  = 0; ve->limits[1][P_BEAT_COLOR]  = 1000;

    ve->description = "Strobotsu";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Duration",
        "Opacity",
        "Echoes",
        "Mode",
        "Delay",
        "Beat Push",
        "Beat Smooth",
        "Beat Color"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Trail",
        "Overlay"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_MOTION_REACT,     VJ_BEAT_F_PHRASE_ONLY,                            24,                 220,                6,  22, 1800, 4200, 900,  28,    /* Threshold */
        VJ_BEAT_SPEED,            VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,       2,                  240,                6,  22, 1800, 4200, 900,  30,    /* Duration */
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_PHRASE_ONLY,                            48,                 230,                6,  22, 1800, 4200, 900,  24,    /* Opacity */
        VJ_BEAT_MEMORY,           VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,       1,                  72,                 6,  22, 1800, 4200, 900,  28,    /* Echoes */
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,          VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Mode */
        VJ_BEAT_SPEED,            VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,       0,                  120,                6,  22, 1800, 4200, 900,  22,    /* Delay */
        VJ_BEAT_INTENSITY,        VJ_BEAT_F_CONTINUOUS,                             0,                  820,                18, 72, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,           VJ_BEAT_F_PHRASE_ONLY,                            260,                820,                5,  18, 2200, 5200, 1200, 18,    /* Beat Smooth */
        VJ_BEAT_COLOR_PHASE,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,            0,                  900,                8,  34, 900,  2400, 0,    42     /* Beat Color */
    );

    (void) w;
    (void) h;

    return ve;
}

void *strobo_malloc(int w, int h)
{
    strobo_t *s = (strobo_t*) vj_calloc(sizeof(strobo_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    veejay_memset(s->buf[0], 0,   len);
    veejay_memset(s->buf[1], 128, len);
    veejay_memset(s->buf[2], 128, len);

    s->timestamp = 0;
    s->beat_env = 0.0f;
    s->beat_kick = 0.0f;
    s->beat_prev = 0.0f;

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void strobo_free(void *ptr)
{
    strobo_t *s = (strobo_t*) ptr;
    if(!s)
        return;

    if(s->buf[0])
        free(s->buf[0]);

    free(s);
}

static void strobo_build_lookup(uint8_t lookup[256], int skew)
{
    skew = strobo_clampi(skew, 0, 255);

    for(int i = 0; i < 256; i++)
        lookup[i] = (uint8_t)((i * skew + 127) / 255);
}

void strobo_apply(void *ptr, VJFrame *frame, int *args)
{
    strobo_t *s = (strobo_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int len = frame->len;
    if(len <= 0)
        return;

    const int skew        = strobo_clampi(args[P_THRESHOLD], 0, 255);
    const int duration    = strobo_clampi(args[P_DURATION], 1, 1500);
    const int opacity     = strobo_clampi(args[P_OPACITY], 0, 255);
    const int echoes      = strobo_clampi(args[P_ECHOES], 1, 100);
    const int mode        = args[P_MODE] ? 1 : 0;
    const int delay       = strobo_clampi(args[P_DELAY], 0, 1500);
    const int beat_push   = strobo_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = strobo_clampi(args[P_BEAT_SMOOTH], 0, 1000);
    const int beat_color  = strobo_clampi(args[P_BEAT_COLOR], 0, 1000);

    const int shaped = strobo_beat_shape(beat_push);
    const float target = (float)shaped * 0.001f;
    const float smooth = (float)beat_smooth * 0.001f;
    const float attack = 0.20f + (1.0f - smooth) * 0.42f;
    const float release = 0.030f + (1.0f - smooth) * 0.110f;
    const float prev_env = s->beat_env;

    if(target > s->beat_env)
        s->beat_env += (target - s->beat_env) * attack;
    else
        s->beat_env += (target - s->beat_env) * release;

    if(s->beat_env < 0.0001f)
        s->beat_env = 0.0f;
    else if(s->beat_env > 1.0f)
        s->beat_env = 1.0f;

    const float rise = s->beat_env - prev_env;
    if(rise > s->beat_kick)
        s->beat_kick += (rise - s->beat_kick) * 0.80f;
    else
        s->beat_kick *= 0.58f;

    if(s->beat_kick < 0.0001f)
        s->beat_kick = 0.0f;
    else if(s->beat_kick > 1.0f)
        s->beat_kick = 1.0f;

    s->beat_prev = target;

    const int beat_q = strobo_clampi((int)(s->beat_env * 1000.0f + 0.5f), 0, 1000);
    const int kick_q = strobo_clampi((int)(s->beat_kick * 1000.0f + 0.5f), 0, 1000);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict bY = s->buf[0];
    uint8_t *restrict bU = s->buf[1];
    uint8_t *restrict bV = s->buf[2];

    uint8_t lookup[256];
    uint32_t histogram[256];

    veejay_memset(histogram, 0, sizeof(histogram));
    strobo_build_lookup(lookup, skew);

    for(int i = 0; i < len; i++)
        histogram[lookup[Y[i]]]++;

    const int base_threshold = (int)otsu_method(histogram);
    const int threshold_bias = ((beat_q * 18) + (kick_q * 30) + 500) / 1000;
    const int threshold = strobo_clampi(base_threshold + threshold_bias, 0, 255);

    const int color_total = (int)(sizeof(strobo_rainbow) / sizeof(strobo_rainbow[0]));
    const int color_count = (duration < color_total) ? duration : color_total;
    const int phase_div = duration / color_count;
    const int base_color_index = (s->timestamp / (phase_div > 0 ? phase_div : 1)) % color_count;

    const int color_drive = strobo_clampi((beat_q + kick_q + 1) >> 1, 0, 1000);
    const int color_jump = (color_count > 1)
        ? (((color_drive * beat_color) / 1000) * color_count + 500) / 1000
        : 0;
    const int color_index = (base_color_index + color_jump) % color_count;

    int cy = 0;
    int cu = 128;
    int cv = 128;

    _rgb2yuv(
        strobo_rainbow[color_index].r,
        strobo_rainbow[color_index].g,
        strobo_rainbow[color_index].b,
        cy,
        cu,
        cv
    );

    cy = CLAMP_Y(cy);
    cu = CLAMP_UV(cu);
    cv = CLAMP_UV(cv);

    const int beat_gate = (kick_q > 18 || beat_q > 320);
    const int update_now = (delay == 0 || (s->timestamp % delay) == 0 || beat_gate);

    int eff_echoes = echoes + ((beat_q * 12 + 500) / 1000);
    if(kick_q > 0)
        eff_echoes -= (kick_q * 4 + 500) / 1000;
    eff_echoes = strobo_clampi(eff_echoes, 1, 100);

    int persist_q8 = 128 + ((eff_echoes * 120 + 50) / 100);
    persist_q8 = strobo_clampi(persist_q8, 96, 248);

    const int deposit_lift = ((beat_q * 54) + (kick_q * 96) + 500) / 1000;
    const int deposit_q8 = update_now ? strobo_clampi(opacity + deposit_lift, 0, 255) : 0;
    const int out_q8 = strobo_clampi(opacity + ((beat_q * 34 + kick_q * 38 + 500) / 1000), 0, 255);

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++) {
        int ty = ((int)bY[i] * persist_q8) >> 8;
        int tu = 128 + ((((int)bU[i] - 128) * persist_q8) >> 8);
        int tv = 128 + ((((int)bV[i] - 128) * persist_q8) >> 8);

        if(deposit_q8 > 0 && skew > 0 && lookup[Y[i]] <= threshold) {
            ty = strobo_blend_y((uint8_t)CLAMP_Y(ty), (uint8_t)cy, deposit_q8);
            tu = strobo_blend_uv((uint8_t)CLAMP_UV(tu), (uint8_t)cu, deposit_q8);
            tv = strobo_blend_uv((uint8_t)CLAMP_UV(tv), (uint8_t)cv, deposit_q8);
        }

        bY[i] = (uint8_t)CLAMP_Y(ty);
        bU[i] = (uint8_t)CLAMP_UV(tu);
        bV[i] = (uint8_t)CLAMP_UV(tv);
    }

    if(mode == 0) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int i = 0; i < len; i++) {
            Y[i] = bY[i];
            U[i] = bU[i];
            V[i] = bV[i];
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int i = 0; i < len; i++) {
            if(bY[i] > 0) {
                Y[i] = strobo_blend_y(Y[i], bY[i], out_q8);
                U[i] = strobo_blend_uv(U[i], bU[i], out_q8);
                V[i] = strobo_blend_uv(V[i], bV[i], out_q8);
            }
        }
    }

    s->timestamp++;
}
