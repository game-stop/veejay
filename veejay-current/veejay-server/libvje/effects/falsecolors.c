/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include "falsecolors.h"
#include <omp.h>

#define FALSECOLORS_PARAMS 8

#define P_MOTION_SENS   0
#define P_CYCLE_SPEED   1
#define P_OPACITY       2
#define P_GAMMA         3
#define P_TRAIL_DECAY   4
#define P_MOTION_GAIN   5
#define P_BEAT_PUSH     6
#define P_BEAT_SMOOTH   7

typedef struct {
    uint8_t *buf[6];
    uint8_t *blur;
    uint8_t rainbow[256][3];
    uint8_t gamma_lut[256];
    int timestamp;
    int n_threads;
    float phase;
    float beat_env;
    float gamma;
} thermal_t;

static inline int fc_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int fc_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline uint8_t clamp_u8(int x)
{
    if ((unsigned)x > 255U)
        x = (~x >> 31) & 255;
    return (uint8_t)x;
}

static inline int fc_beat_shape_1000(int beat_push)
{
    beat_push = fc_clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return fc_clampi((beat_push * 38 + sq * 62 + 50) / 100, 0, 1000);
}

static inline int fc_mix_towards(int v, int target, int q)
{
    q = fc_clampi(q, 0, 1000);
    return fc_clampi(v + (((target - v) * q + ((target >= v) ? 500 : -500)) / 1000), 0, 1000);
}

static void build_gamma_lut(uint8_t lut[256], float gamma)
{
    for(int i = 0; i < 256; i++) {
        float x = (float)i / 255.0f;
        lut[i] = (uint8_t)(powf(x, gamma) * 255.0f + 0.5f);
    }
}

static void thermal_build_palette(uint8_t lut[256][3], float gamma)
{
    (void) gamma;

    const float t_points[7] = {0.0f, 0.14f, 0.28f, 0.42f, 0.57f, 0.71f, 1.0f};
    const float r_points[7] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float g_points[7] = {0.0f, 0.3f, 0.7f, 1.0f, 1.0f, 0.0f, 0.0f};
    const float b_points[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f};

    for(int i = 0; i < 256; i++) {
        float t = (float)i / 255.0f;
        int seg = 0;

        while(seg < 6 && t > t_points[seg + 1])
            seg++;

        float f = (t - t_points[seg]) / (t_points[seg + 1] - t_points[seg]);
        float r = r_points[seg] + f * (r_points[seg + 1] - r_points[seg]);
        float g = g_points[seg] + f * (g_points[seg + 1] - g_points[seg]);
        float b = b_points[seg] + f * (b_points[seg + 1] - b_points[seg]);

        /* RGB -> YUV (BT.601) */
        float Yf = 0.299f * r + 0.587f * g + 0.114f * b;
        float Uf = -0.169f * r - 0.331f * g + 0.5f * b + 0.5f;
        float Vf = 0.5f * r - 0.419f * g - 0.081f * b + 0.5f;

        lut[i][0] = (uint8_t)(fminf(fmaxf(Yf, 0.0f), 1.0f) * 255.0f + 0.5f);
        lut[i][1] = (uint8_t)(fminf(fmaxf(Uf, 0.0f), 1.0f) * 255.0f + 0.5f);
        lut[i][2] = (uint8_t)(fminf(fmaxf(Vf, 0.0f), 1.0f) * 255.0f + 0.5f);
    }
}

static void falsecolors_build_beat_hints(vj_effect *ve)
{
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_MOTION_REACT, VJ_BEAT_F_PHRASE_ONLY,                    48,  178, 5,  22, 1800, 4200, 900, 24,  /* Motion Sensitivity */
        VJ_BEAT_HAT,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,    0,   48,  4,  26, 80,   620,  0,   52,  /* Cycle Speed */
        VJ_BEAT_KICK,         VJ_BEAT_F_CONTINUOUS,                     128, 255, 14, 58, 90,   720,  0,   78,  /* Opacity / Heat Mix */
        VJ_BEAT_CONTRAST,     VJ_BEAT_F_PHRASE_ONLY,                    42,  132, 5,  20, 1800, 4200, 900, 20,  /* Gamma */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY,                    6,   64,  5,  20, 2200, 5200, 1200,24,  /* Trail Decay */
        VJ_BEAT_SNARE,        VJ_BEAT_F_CONTINUOUS,                     96,  820, 10, 46, 120,  900,  0,   72,  /* Motion Gain */
        VJ_BEAT_KICK,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE, 0,   900, 22, 88, 60,   360,  0,   100, /* Beat Push */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY,                    260, 820, 5,  18, 2200, 5200, 1200,18   /* Beat Smooth */
    );
}

vj_effect *falsecolors_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = FALSECOLORS_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[P_MOTION_SENS] = 128;
    ve->defaults[P_CYCLE_SPEED] = 1;
    ve->defaults[P_OPACITY]     = 200;
    ve->defaults[P_GAMMA]       = 64;
    ve->defaults[P_TRAIL_DECAY] = 16;
    ve->defaults[P_MOTION_GAIN] = 256;
    ve->defaults[P_BEAT_PUSH]   = 0;
    ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->limits[0][P_MOTION_SENS] = 0;    ve->limits[1][P_MOTION_SENS] = 255;
    ve->limits[0][P_CYCLE_SPEED] = 0;    ve->limits[1][P_CYCLE_SPEED] = 64;
    ve->limits[0][P_OPACITY]     = 0;    ve->limits[1][P_OPACITY]     = 255;
    ve->limits[0][P_GAMMA]       = 1;    ve->limits[1][P_GAMMA]       = 255;
    ve->limits[0][P_TRAIL_DECAY] = 1;    ve->limits[1][P_TRAIL_DECAY] = 128;
    ve->limits[0][P_MOTION_GAIN] = 0;    ve->limits[1][P_MOTION_GAIN] = 1024;
    ve->limits[0][P_BEAT_PUSH]   = 0;    ve->limits[1][P_BEAT_PUSH]   = 1000;
    ve->limits[0][P_BEAT_SMOOTH] = 0;    ve->limits[1][P_BEAT_SMOOTH] = 1000;

    ve->description = "False Color Map";

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Motion Sensitivity",
        "Cycle Speed",
        "Opacity",
        "Gamma",
        "Trail Decay",
        "Motion Gain",
        "Beat Push",
        "Beat Smooth"
    );

    falsecolors_build_beat_hints(ve);

    (void)w;
    (void)h;

    return ve;
}

void *falsecolors_malloc(int w, int h)
{
    thermal_t *s = (thermal_t*) vj_calloc(sizeof(thermal_t));
    if(!s)
        return NULL;

    const int len = w * h;
    if(len <= 0) {
        free(s);
        return NULL;
    }

    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    veejay_memset(s->buf[0], 0, len);
    veejay_memset(s->buf[1], 0, len);
    veejay_memset(s->buf[2], 0, len);

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads <= 0)
        s->n_threads = 1;

    thermal_build_palette(s->rainbow, 0.8f);

    const int max_dim = (w > h) ? w : h;
    s->blur = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)s->n_threads * (size_t)max_dim * 2u);
    if(!s->blur) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->gamma = -1.0f;
    s->phase = 0.0f;
    s->beat_env = 0.0f;
    s->timestamp = 0;

    return s;
}

void falsecolors_free(void *ptr)
{
    thermal_t *s = (thermal_t*) ptr;

    if(s) {
        if(s->buf[0])
            free(s->buf[0]);
        if(s->blur)
            free(s->blur);
        free(s);
    }
}

void falsecolors_apply(void *ptr, VJFrame *frame, int *args)
{
    thermal_t *s = (thermal_t*) ptr;
    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w   = frame->width;
    const int h   = frame->height;
    const int len = frame->len;

    if(w <= 1 || h <= 1 || len <= 0)
        return;

    const int opacity_user = fc_clampi(args[P_OPACITY], 0, 255);
    const int cycle_user   = fc_clampi(args[P_CYCLE_SPEED], 0, 64);
    const int sens_user    = fc_clampi(args[P_MOTION_SENS], 0, 255);
    const int gamma_user   = fc_clampi(args[P_GAMMA], 1, 255);
    const int trail_user   = fc_clampi(args[P_TRAIL_DECAY], 1, 128);
    const int gain_user    = fc_clampi(args[P_MOTION_GAIN], 0, 1024);
    const int beat_push    = fc_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int smooth_user  = fc_clampi(args[P_BEAT_SMOOTH], 0, 1000);

    const int shaped = fc_beat_shape_1000(beat_push);
    const float beat_target = (float)shaped * 0.001f;
    const float smooth_t = (float)smooth_user * 0.001f;
    const float attack = 0.20f + (1.0f - smooth_t) * 0.34f;
    const float release = 0.035f + (1.0f - smooth_t) * 0.090f;

    if(beat_target > s->beat_env)
        s->beat_env += (beat_target - s->beat_env) * attack;
    else
        s->beat_env += (beat_target - s->beat_env) * release;

    if(s->beat_env < 0.0001f)
        s->beat_env = 0.0f;
    else if(s->beat_env > 1.0f)
        s->beat_env = 1.0f;

    const int beat_q = fc_clampi((int)(s->beat_env * 1000.0f + 0.5f), 0, 1000);

    int sensitivity = sens_user - ((beat_q * 72 + 500) / 1000);
    int motion_gain = gain_user + ((beat_q * 280 + 500) / 1000);
    int cycle_speed = cycle_user + ((beat_q * 12 + 500) / 1000);
    int opacity = opacity_user + ((beat_q * 18 + 500) / 1000);
    int trail_decay = trail_user + ((beat_q * 10 + 500) / 1000);

    sensitivity = fc_clampi(sensitivity, 0, 255);
    motion_gain = fc_clampi(motion_gain, 0, 1024);
    cycle_speed = fc_clampi(cycle_speed, 0, 64);
    opacity = fc_clampi(opacity, 0, 255);
    trail_decay = fc_clampi(trail_decay, 1, 128);

    const int inv_opacity = 256 - opacity;
    const int heat_boost = (beat_q * 42 + 500) / 1000;
    const int decay_step = fc_clampi((256 + (trail_decay >> 1)) / trail_decay, 2, 256);
    const float gamma = ((float)gamma_user / 64.0f) + 0.1f;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict prev_luma = s->buf[0];
    uint8_t *restrict heat_buf  = s->buf[1];
    uint8_t *restrict blur_buf  = s->buf[2];

    const int max_dim = (w > h) ? w : h;

    if(fabsf(s->gamma - gamma) > 0.01f) {
        build_gamma_lut(s->gamma_lut, gamma);
        s->gamma = gamma;
    }

    s->phase += (float)cycle_speed;
    if(s->phase >= 4096.0f)
        s->phase -= 4096.0f;

    const int lut_offset = ((int)s->phase) & 0xFF;

    int global_min = 255;
    int global_max = 0;

#pragma omp parallel num_threads(s->n_threads) reduction(min:global_min) reduction(max:global_max)
    {
        const int tid = omp_get_thread_num();

        uint8_t *tmp = s->blur + (size_t)tid * (size_t)max_dim * 2u;
        uint8_t *col_tmp = tmp;
        uint8_t *col_out = tmp + max_dim;

#pragma omp for schedule(static)
        for(int y = 0; y < h; y++) {
            veejay_blur2(tmp, Y + (y * w), w, 2, 2, 1, 1);
            memcpy(blur_buf + (y * w), tmp, (size_t)w);
        }

#pragma omp for schedule(static)
        for(int x = 0; x < w; x++) {
            for(int y = 0; y < h; y++)
                col_tmp[y] = blur_buf[y * w + x];

            veejay_blur2(col_out, col_tmp, h, 2, 2, 1, 1);

            for(int y = 0; y < h; y++)
                blur_buf[y * w + x] = col_out[y];
        }

#pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            int v = blur_buf[i];

            if(v < global_min)
                global_min = v;
            if(v > global_max)
                global_max = v;
        }
    }

    int range = global_max - global_min;
    if(range < 64)
        range = 64;

    const int scale_fp = (255 << 16) / range;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++) {
        const int lum = blur_buf[i];

        int motion = fc_absi(lum - prev_luma[i]) - sensitivity;
        motion &= ~(motion >> 31);

        int heat = (((lum - global_min) * scale_fp) >> 16) + ((motion * motion_gain) >> 7);

        if(heat_boost > 0)
            heat += heat_boost;

        if(heat > 255)
            heat = 255;
        else if(heat < 0)
            heat = 0;

        int mixed = (heat * opacity + heat_buf[i] * inv_opacity) >> 8;
        int released = (int)heat_buf[i] - decay_step;

        if(released < 0)
            released = 0;

        int val = (mixed < released) ? released : mixed;

        heat_buf[i] = (uint8_t)val;
        prev_luma[i] = (uint8_t)lum;

        const int mapped = s->gamma_lut[val];
        int lut_idx = (mapped + lut_offset) & 0xFF;

        if(motion > 0) {
            int jump = (motion > 48) ? 64 : (motion << 6) / 48;

            jump += (beat_q * 18 + 500) / 1000;
            lut_idx = (lut_idx + jump) & 0xFF;
        }

        const uint8_t *restrict col = s->rainbow[lut_idx];

        Y[i] = col[0];
        U[i] = col[1];
        V[i] = col[2];
    }

    s->timestamp++;
}
