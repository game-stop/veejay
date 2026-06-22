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
#include <veejaycore/vjmem.h>
#include "strobo.h"

#define STROBO_PARAMS 7

#define P_THRESHOLD    0
#define P_COLOR_HOLD   1
#define P_OPACITY      2
#define P_TRAIL        3
#define P_MODE         4
#define P_INTERVAL     5
#define P_COLOR_OFFSET 6

typedef struct {
    uint8_t *buf[3];
    int timestamp;
    int pulse_count;
    int strobe_countdown;
    int n_threads;

    float eff_threshold;
    float eff_color_hold;
    float eff_opacity;
    float eff_trail;
    float eff_interval;
    float eff_color_offset;
    int eff_ready;
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

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t blend_y(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t blend_uv(uint8_t a, uint8_t b, int q8)
{
    const int ac = (int)a - 128;
    const int bc = (int)b - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return (uint8_t)CLAMP_UV(v);
}

static inline int strobo_roundf_i(float v)
{
    return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

static inline int strobo_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float coef = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * coef;

    *state = out;
    return strobo_roundf_i(out);
}

vj_effect *strobo_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = STROBO_PARAMS;

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

    ve->defaults[P_THRESHOLD]    = 96;
    ve->defaults[P_COLOR_HOLD]   = 12;
    ve->defaults[P_OPACITY]      = 180;
    ve->defaults[P_TRAIL]        = 34;
    ve->defaults[P_MODE]         = 0;
    ve->defaults[P_INTERVAL]     = 4;
    ve->defaults[P_COLOR_OFFSET] = 0;

    ve->limits[0][P_THRESHOLD]    = 0; ve->limits[1][P_THRESHOLD]    = 255;
    ve->limits[0][P_COLOR_HOLD]   = 1; ve->limits[1][P_COLOR_HOLD]   = 1500;
    ve->limits[0][P_OPACITY]      = 0; ve->limits[1][P_OPACITY]      = 255;
    ve->limits[0][P_TRAIL]        = 0; ve->limits[1][P_TRAIL]        = 100;
    ve->limits[0][P_MODE]         = 0; ve->limits[1][P_MODE]         = 1;
    ve->limits[0][P_INTERVAL]     = 1; ve->limits[1][P_INTERVAL]     = 1500;
    ve->limits[0][P_COLOR_OFFSET] = 0; ve->limits[1][P_COLOR_OFFSET] = 13;

    ve->description = "Strobotsu";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold Bias",
        "Color Hold",
        "Opacity",
        "Trail",
        "Mode",
        "Strobe Interval",
        "Color Offset"
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
        VJ_BEAT_MOTION_REACT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         48,                 170,                10, 38,  700, 2600, 0,    52,
        VJ_BEAT_COLOR_PHASE,      VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,    4,                  96,                 3,  12, 2600, 8200, 1800, 22,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         96,                 255,                14, 56,  450, 2000, 0,    86,
        VJ_BEAT_MEMORY,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         10,                 92,                 8,  32,  700, 2800, 0,    74,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,    0,    -1000,
        VJ_BEAT_SPEED,            VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,       1,                  48,                 3,  18,  450, 2200, 0,    96,
        VJ_BEAT_COLOR_PHASE,      VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,    0,                  13,                 3,  10, 2600, 8200, 1800, 18
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

    veejay_memset(s->buf[0], 0, len);
    veejay_memset(s->buf[1], 128, len);
    veejay_memset(s->buf[2], 128, len);

    s->timestamp = 0;
    s->pulse_count = 0;
    s->strobe_countdown = 0;
    s->eff_threshold = 96.0f;
    s->eff_color_hold = 12.0f;
    s->eff_opacity = 180.0f;
    s->eff_trail = 34.0f;
    s->eff_interval = 4.0f;
    s->eff_color_offset = 0.0f;
    s->eff_ready = 0;

    s->n_threads = vje_advise_num_threads(len);

    return (void*) s;
}

void strobo_free(void *ptr)
{
    strobo_t *s = (strobo_t*) ptr;

    free(s->buf[0]);
    free(s);
}

static inline int strobo_clock_tick(strobo_t *s, int interval)
{
    int update_now;

    if(s->strobe_countdown >= interval)
        s->strobe_countdown = interval - 1;

    if(s->strobe_countdown <= 0) {
        update_now = 1;
        s->strobe_countdown = interval;
        s->pulse_count++;
    }
    else {
        update_now = 0;
    }

    s->strobe_countdown--;

    return update_now;
}

void strobo_apply(void *ptr, VJFrame *frame, int *args)
{
    strobo_t *s = (strobo_t*) ptr;

    const int len = frame->len;

    const float fast_a = 0.30f;
    const float fast_r = 0.12f;
    const float slow_a = 0.13f;
    const float slow_r = 0.060f;

    const int threshold_arg = args[P_THRESHOLD];
    const int color_hold_arg = args[P_COLOR_HOLD];
    const int opacity_arg = args[P_OPACITY];
    const int trail_arg = args[P_TRAIL];
    const int interval_arg = args[P_INTERVAL];
    const int color_offset_arg = args[P_COLOR_OFFSET];

    if(!s->eff_ready) {
        s->eff_threshold = (float)threshold_arg;
        s->eff_color_hold = (float)color_hold_arg;
        s->eff_opacity = (float)opacity_arg;
        s->eff_trail = (float)trail_arg;
        s->eff_interval = (float)interval_arg;
        s->eff_color_offset = (float)color_offset_arg;
        s->eff_ready = 1;
    }

    int threshold_bias = strobo_smooth_i(&s->eff_threshold, threshold_arg, fast_a, fast_r);
    int color_hold = strobo_smooth_i(&s->eff_color_hold, color_hold_arg, slow_a, slow_r);
    int opacity = strobo_smooth_i(&s->eff_opacity, opacity_arg, fast_a, fast_r);
    int trail = strobo_smooth_i(&s->eff_trail, trail_arg, slow_a, slow_r);
    int interval = strobo_smooth_i(&s->eff_interval, interval_arg, slow_a, slow_r);
    int color_offset = strobo_smooth_i(&s->eff_color_offset, color_offset_arg, slow_a, slow_r);

    threshold_bias = clampi(threshold_bias, 0, 255);
    color_hold = clampi(color_hold, 1, 1500);
    opacity = clampi(opacity, 0, 255);
    trail = clampi(trail, 0, 100);
    interval = clampi(interval, 1, 1500);
    color_offset = clampi(color_offset, 0, 13);

    const int mode = args[P_MODE] ? 1 : 0;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict bY = s->buf[0];
    uint8_t *restrict bU = s->buf[1];
    uint8_t *restrict bV = s->buf[2];

    uint32_t histogram[256];

    veejay_memset(histogram, 0, sizeof(histogram));

    for(int i = 0; i < len; i++)
        histogram[Y[i]]++;

    const int base_threshold = (int)otsu_method(histogram);
    const int threshold = clampi(base_threshold + ((threshold_bias - 128) >> 1), 0, 255);
    const int color_total = (int)(sizeof(strobo_rainbow) / sizeof(strobo_rainbow[0]));
    const int base_color_index = (s->timestamp / color_hold) % color_total;
    const int color_index = (base_color_index + color_offset) % color_total;
    const int update_now = strobo_clock_tick(s, interval);

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

    int persist_q8 = 16 + ((trail * 234 + 50) / 100);
    persist_q8 = clampi(persist_q8, 16, 250);

    const int deposit_q8 = update_now ? opacity : 0;
    const int out_q8 = opacity;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++) {
        int ty = ((int)bY[i] * persist_q8) >> 8;
        int tu = 128 + ((((int)bU[i] - 128) * persist_q8) >> 8);
        int tv = 128 + ((((int)bV[i] - 128) * persist_q8) >> 8);

        if(deposit_q8 > 0 && Y[i] <= threshold) {
            ty = blend_y((uint8_t)clampi(ty, 0, 255), (uint8_t)cy, deposit_q8);
            tu = blend_uv((uint8_t)CLAMP_UV(tu), (uint8_t)cu, deposit_q8);
            tv = blend_uv((uint8_t)CLAMP_UV(tv), (uint8_t)cv, deposit_q8);
        }

        bY[i] = (uint8_t)clampi(ty, 0, 255);
        bU[i] = (uint8_t)CLAMP_UV(tu);
        bV[i] = (uint8_t)CLAMP_UV(tv);
    }

    if(mode == 0) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int i = 0; i < len; i++) {
            if(bY[i] > 1) {
                Y[i] = (uint8_t)CLAMP_Y(bY[i]);
                U[i] = bU[i];
                V[i] = bV[i];
            }
            else {
                Y[i] = pixel_Y_lo_;
                U[i] = 128;
                V[i] = 128;
            }
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int i = 0; i < len; i++) {
            if(bY[i] > 1) {
                Y[i] = blend_y(Y[i], (uint8_t)CLAMP_Y(bY[i]), out_q8);
                U[i] = blend_uv(U[i], bU[i], out_q8);
                V[i] = blend_uv(V[i], bV[i], out_q8);
            }
        }
    }

    s->timestamp++;
}
