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

#define P_THRESHOLD   0
#define P_DURATION    1
#define P_OPACITY     2
#define P_ECHOES      3
#define P_MODE        4
#define P_DELAY       5
#define P_COLOR_DRIVE 6

typedef struct {
    uint8_t *buf[3];
    int timestamp;
    int n_threads;

    float eff_threshold;
    float eff_duration;
    float eff_opacity;
    float eff_echoes;
    float eff_delay;
    float eff_color;
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

    ve->defaults[P_THRESHOLD]   = 70;
    ve->defaults[P_DURATION]    = 10;
    ve->defaults[P_OPACITY]     = 150;
    ve->defaults[P_ECHOES]      = 3;
    ve->defaults[P_MODE]        = 0;
    ve->defaults[P_DELAY]       = 0;
    ve->defaults[P_COLOR_DRIVE] = 0;

    ve->limits[0][P_THRESHOLD]   = 0; ve->limits[1][P_THRESHOLD]   = 255;
    ve->limits[0][P_DURATION]    = 1; ve->limits[1][P_DURATION]    = 1500;
    ve->limits[0][P_OPACITY]     = 0; ve->limits[1][P_OPACITY]     = 255;
    ve->limits[0][P_ECHOES]      = 1; ve->limits[1][P_ECHOES]      = 100;
    ve->limits[0][P_MODE]        = 0; ve->limits[1][P_MODE]        = 1;
    ve->limits[0][P_DELAY]       = 0; ve->limits[1][P_DELAY]       = 1500;
    ve->limits[0][P_COLOR_DRIVE] = 0; ve->limits[1][P_COLOR_DRIVE] = 1000;

    ve->description = "Strobotsu";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Duration",
        "Opacity",
        "Echoes",
        "Mode",
        "Delay",
        "Color Drive"
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
        VJ_BEAT_MOTION_REACT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         18,                 190,                10, 38,  900, 3600, 0,    58,
        VJ_BEAT_SPEED,            VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 2, 180, 4, 14, 2600, 9000, 1800, 28,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         72,                 210,                10, 38, 1000, 3800, 0,    54,
        VJ_BEAT_MEMORY,           VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,     2,                  28,                 4,  14, 2800, 9200, 1800, 26,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SPEED,            VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 0, 240, 4, 14, 3200, 11000, 2400, 18,
        VJ_BEAT_COLOR_PHASE,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         0,                  320,                8,  30, 1600, 5600, 0,    38
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
    s->eff_threshold = 70.0f;
    s->eff_duration = 10.0f;
    s->eff_opacity = 150.0f;
    s->eff_echoes = 3.0f;
    s->eff_delay = 0.0f;
    s->eff_color = 0.0f;
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

static void strobo_build_lookup(uint8_t lookup[256], int skew)
{
    skew = clampi(skew, 0, 255);

    for(int i = 0; i < 256; i++)
        lookup[i] = (uint8_t)((i * skew + 127) / 255);
}



void strobo_apply(void *ptr, VJFrame *frame, int *args)
{
    strobo_t *s = (strobo_t*) ptr;

    const int len = frame->len;

    const float fast_a = 0.30f;
    const float fast_r = 0.12f;
    const float slow_a = 0.15f;
    const float slow_r = 0.070f;

    const int threshold_arg = args[P_THRESHOLD];
    const int duration_arg = args[P_DURATION];
    const int opacity_arg = args[P_OPACITY];
    const int echoes_arg = args[P_ECHOES];
    const int delay_arg = args[P_DELAY];
    const int color_drive_arg = args[P_COLOR_DRIVE];

    if(!s->eff_ready) {
        s->eff_threshold = (float)threshold_arg;
        s->eff_duration = (float)duration_arg;
        s->eff_opacity = (float)opacity_arg;
        s->eff_echoes = (float)echoes_arg;
        s->eff_delay = (float)delay_arg;
        s->eff_color = (float)color_drive_arg;
        s->eff_ready = 1;
    }

    int skew = strobo_smooth_i(&s->eff_threshold, threshold_arg, fast_a, fast_r);
    int duration = strobo_smooth_i(&s->eff_duration, duration_arg, slow_a, slow_r);
    int opacity = strobo_smooth_i(&s->eff_opacity, opacity_arg, fast_a, fast_r);
    int echoes = strobo_smooth_i(&s->eff_echoes, echoes_arg, slow_a, slow_r);
    int delay = strobo_smooth_i(&s->eff_delay, delay_arg, slow_a, slow_r);
    int color_drive = strobo_smooth_i(&s->eff_color, color_drive_arg, fast_a, fast_r);

    skew = clampi(skew, 0, 255);
    duration = clampi(duration, 1, 1500);
    opacity = clampi(opacity, 0, 255);
    echoes = clampi(echoes, 1, 100);
    delay = clampi(delay, 0, 1500);
    color_drive = clampi(color_drive, 0, 1000);

    const int mode = args[P_MODE] ? 1 : 0;
    const int direct_q = color_drive;

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
    const int threshold = clampi(base_threshold + ((skew - 128) >> 1), 0, 255);

    const int color_total = (int)(sizeof(strobo_rainbow) / sizeof(strobo_rainbow[0]));
    const int color_hold = duration < 1 ? 1 : duration;
    const int base_color_index = (s->timestamp / color_hold) % color_total;
    const int color_direct = (color_drive * color_total + 500) / 1000;
    const int color_index = (base_color_index + color_direct) % color_total;

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

    const int update_now = (delay == 0 || (s->timestamp % delay) == 0);

    int persist_q8 = 96 + ((echoes * 152 + 50) / 100);
    persist_q8 = clampi(persist_q8, 64, 250);

    const int color_lift = (direct_q * 22 + 500) / 1000;
    const int deposit_lift = (direct_q * 36 + 500) / 1000;
    const int deposit_q8 = update_now ? clampi(opacity + deposit_lift, 0, 255) : 0;
    const int out_q8 = opacity;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++) {
        int ty = ((int)bY[i] * persist_q8) >> 8;
        int tu = 128 + ((((int)bU[i] - 128) * persist_q8) >> 8);
        int tv = 128 + ((((int)bV[i] - 128) * persist_q8) >> 8);

        if(deposit_q8 > 0 && Y[i] <= threshold) {
            const int dy = CLAMP_Y(cy + color_lift);
            ty = blend_y((uint8_t)clampi(ty, 0, 255), (uint8_t)dy, deposit_q8);
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
