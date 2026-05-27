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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include "chromadrift.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define INV_255        0.0039215686f
#define PI_X2          6.28318530718f
#define SIN_LUT_SIZE   4096
#define SIN_MASK       (SIN_LUT_SIZE - 1)
#define SCALER_TO_LUT  (SIN_LUT_SIZE / PI_X2)

#define FP_S    14
#define FP_M    (1 << FP_S)
#define FP_HALF (1 << (FP_S - 1))

#define CD_IDX_FP 12
#define CD_IDX_ONE (1 << CD_IDX_FP)

#define CHROMATICDRIFT_PARAMS 13

#define P_GLOBAL_HUE    0
#define P_RAINBOW_WRAP  1
#define P_VIBRANCE      2
#define P_PASTEL_GLOW   3
#define P_FLUX_SPEED    4
#define P_EDGE_SOFTNESS 5
#define P_BLACK_PROTECT 6
#define P_WHITE_PROTECT 7
#define P_LUMA_CONTRAST 8
#define P_DIRECTION     9
#define P_CHROMA_GUARD 10
#define P_BEAT_PUSH    11
#define P_BEAT_SMOOTH  12

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int absi_i32(int v)
{
    return v < 0 ? -v : v;
}

static inline uint8_t clamp_u8(int x)
{
    if(x < 0)
        return 0;
    if(x > 255)
        return 255;
    return (uint8_t)x;
}

static inline int percent_to_param(int v)
{
    v = clampi(v, 0, 255);
    return (v * 1000 + 127) / 255;
}

static inline int beat_shape(int beat_push)
{
    int lin;
    int sq;

    beat_push = clampi(beat_push, 0, 1000);

    lin = beat_push;
    sq = (beat_push * beat_push + 500) / 1000;

    return clampi((lin * 35 + sq * 65 + 50) / 100, 0, 1000);
}

static inline int add_clamped(int v, int add)
{
    return clampi(v + add, 0, 1000);
}

static inline int mix_towards(int v, int target, int q)
{
    q = clampi(q, 0, 1000);
    return clampi(v + (((target - v) * q + ((target >= v) ? 500 : -500)) / 1000), 0, 1000);
}

static inline void cd_limit_chroma_i32(int32_t *u, int32_t *v, int limit)
{
    int au = absi_i32((int)*u);
    int av = absi_i32((int)*v);
    int m = au > av ? au + (av >> 1) : av + (au >> 1);

    if(m > limit && m > 0) {
        int scale = (limit << FP_S) / m;
        *u = (*u * scale) >> FP_S;
        *v = (*v * scale) >> FP_S;
    }
}

static inline int cd_guard_luma_black_i(int y_in, int y_out, int floor_y, int knee_y, int mix_q8)
{
    int target;

    if(y_in <= 1 || y_out >= floor_y)
        return y_out;

    knee_y = clampi(knee_y, floor_y + 1, 192);
    mix_q8 = clampi(mix_q8, 0, 256);

    target = floor_y;

    if(y_in < knee_y) {
        target = floor_y + ((y_in * (knee_y - floor_y) + (knee_y >> 1)) / knee_y);
        if(target < floor_y)
            target = floor_y;
    }

    return y_out + (((target - y_out) * mix_q8 + 128) >> 8);
}

typedef struct
{
    int w;
    int h;
    float time;
    float beat_env;
    uint8_t *srcY_copy;
    int16_t sin_lut_fp[SIN_LUT_SIZE];
    uint8_t contrast_lut[256];
    uint8_t luma_lut[256];
    int32_t tint_lut_fp[256];
    int last_contrast_param;
    int last_luma_floor;
    int last_luma_knee;
    int last_luma_guard_q8;
    int last_pastel_param;
    int last_bprot_param;
    int last_wprot_param;
    int n_threads;
} chromaticdrift_t;

vj_effect *chromaticdrift_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = CHROMATICDRIFT_PARAMS;
    ve->defaults   = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0]  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1]  = (int *) vj_calloc(sizeof(int) * ve->num_params);

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

    ve->defaults[P_GLOBAL_HUE]    = percent_to_param(0);
    ve->defaults[P_RAINBOW_WRAP]  = percent_to_param(0);
    ve->defaults[P_VIBRANCE]      = percent_to_param(55);
    ve->defaults[P_PASTEL_GLOW]   = percent_to_param(0);
    ve->defaults[P_FLUX_SPEED]    = percent_to_param(0);
    ve->defaults[P_EDGE_SOFTNESS] = percent_to_param(0);
    ve->defaults[P_BLACK_PROTECT] = 120;
    ve->defaults[P_WHITE_PROTECT] = percent_to_param(255);
    ve->defaults[P_LUMA_CONTRAST] = percent_to_param(85);
    ve->defaults[P_DIRECTION]     = 1;
    ve->defaults[P_CHROMA_GUARD]  = 720;
    ve->defaults[P_BEAT_PUSH]     = 0;
    ve->defaults[P_BEAT_SMOOTH]   = 520;

    ve->limits[0][P_GLOBAL_HUE]    = 0;     ve->limits[1][P_GLOBAL_HUE]    = 1000;
    ve->limits[0][P_RAINBOW_WRAP]  = 0;     ve->limits[1][P_RAINBOW_WRAP]  = 1000;
    ve->limits[0][P_VIBRANCE]      = 0;     ve->limits[1][P_VIBRANCE]      = 1000;
    ve->limits[0][P_PASTEL_GLOW]   = 0;     ve->limits[1][P_PASTEL_GLOW]   = 1000;
    ve->limits[0][P_FLUX_SPEED]    = 0;     ve->limits[1][P_FLUX_SPEED]    = 1000;
    ve->limits[0][P_EDGE_SOFTNESS] = 0;     ve->limits[1][P_EDGE_SOFTNESS] = 1000;
    ve->limits[0][P_BLACK_PROTECT] = 0;     ve->limits[1][P_BLACK_PROTECT] = 1000;
    ve->limits[0][P_WHITE_PROTECT] = 0;     ve->limits[1][P_WHITE_PROTECT] = 1000;
    ve->limits[0][P_LUMA_CONTRAST] = 0;     ve->limits[1][P_LUMA_CONTRAST] = 1000;
    ve->limits[0][P_DIRECTION]     = -1;    ve->limits[1][P_DIRECTION]     = 1;
    ve->limits[0][P_CHROMA_GUARD]  = 0;     ve->limits[1][P_CHROMA_GUARD]  = 1000;
    ve->limits[0][P_BEAT_PUSH]     = 0;     ve->limits[1][P_BEAT_PUSH]     = 1000;
    ve->limits[0][P_BEAT_SMOOTH]   = 0;     ve->limits[1][P_BEAT_SMOOTH]   = 1000;

    ve->sub_format = 1;
    ve->description = "Chromatic Drift Guard";

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Global Hue",
        "Rainbow Wrap",
        "Vibrance",
        "Pastel Glow",
        "Flux Speed",
        "Edge Softness",
        "Black Protect",
        "White Protect",
        "Luma Contrast",
        "Direction",
        "Chroma Guard",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,   0,                  1000,               10, 36, 1100, 2600, 0,    46,    /* Global Hue */
        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS,                    0,                  520,                8,  28, 1400, 3200, 250,  34,    /* Rainbow Wrap */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS,                    80,                 680,                10, 34, 1000, 2600, 0,    52,    /* Vibrance */
        VJ_BEAT_GLOW,         VJ_BEAT_F_CONTINUOUS,                    0,                  420,                8,  28, 1200, 3000, 100,  36,    /* Pastel Glow */
        VJ_BEAT_SPEED,        VJ_BEAT_F_CONTINUOUS,                    0,                  700,                10, 40, 900,  2400, 0,    58,    /* Flux Speed */
        VJ_BEAT_DETAIL,       VJ_BEAT_F_PHRASE_ONLY,                   0,                  620,                5,  18, 1800, 4200, 900,  22,    /* Edge Softness */
        VJ_BEAT_DETAIL,       VJ_BEAT_F_PHRASE_ONLY,                   140,                560,                5,  18, 1800, 4200, 900,  20,    /* Black Protect */
        VJ_BEAT_DETAIL,       VJ_BEAT_F_PHRASE_ONLY,                   640,                1000,               5,  18, 1800, 4200, 900,  18,    /* White Protect */
        VJ_BEAT_CONTRAST,     VJ_BEAT_F_PHRASE_ONLY,                   240,                540,                6,  20, 1800, 4200, 900,  20,    /* Luma Contrast */
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Direction */
        VJ_BEAT_CONTRAST,     VJ_BEAT_F_REJECT,                        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Chroma Guard */
        VJ_BEAT_INTENSITY,    VJ_BEAT_F_CONTINUOUS,                    0,                  700,                18, 74, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY,                   360,                860,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    (void)w;
    (void)h;

    return ve;
}

void *chromaticdrift_malloc(int w, int h)
{
    chromaticdrift_t *c = (chromaticdrift_t *) vj_calloc(sizeof(chromaticdrift_t));
    if(!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->time = 0.0f;
    c->beat_env = 0.0f;

    c->srcY_copy = (uint8_t *) vj_malloc((size_t)w * (size_t)h);
    if(!c->srcY_copy) {
        free(c);
        return NULL;
    }

    for (int i = 0; i < SIN_LUT_SIZE; i++) {
        float s = sinf((i * PI_X2) / SIN_LUT_SIZE);
        c->sin_lut_fp[i] = (int16_t)(s * FP_M);
    }

    c->last_contrast_param = -1;
    c->last_luma_floor = -1;
    c->last_luma_knee = -1;
    c->last_luma_guard_q8 = -1;
    c->last_pastel_param = -1;
    c->last_bprot_param = -1;
    c->last_wprot_param = -1;

    c->n_threads = vje_advise_num_threads(w * h);
    if(c->n_threads <= 0)
        c->n_threads = 1;

    return c;
}

void chromaticdrift_free(void *ptr)
{
    chromaticdrift_t *c = (chromaticdrift_t *)ptr;

    if(c) {
        if(c->srcY_copy)
            free(c->srcY_copy);
        free(c);
    }
}

void chromaticdrift_apply(void *ptr, VJFrame *frame, int *args)
{
    chromaticdrift_t *n = (chromaticdrift_t *) ptr;

    if(!n || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w = n->w;
    const int h = n->h;

    if(w <= 2 || h <= 2)
        return;

    const int w_sub_1 = w - 1;
    const int h_sub_1 = h - 1;

    int hue_i       = clampi(args[P_GLOBAL_HUE], 0, 1000);
    int rainbow_i   = clampi(args[P_RAINBOW_WRAP], 0, 1000);
    int vibrance_i  = clampi(args[P_VIBRANCE], 0, 1000);
    int pastel_i    = clampi(args[P_PASTEL_GLOW], 0, 1000);
    int flux_i      = clampi(args[P_FLUX_SPEED], 0, 1000);
    int softness_i  = clampi(args[P_EDGE_SOFTNESS], 0, 1000);
    int bprot_i     = clampi(args[P_BLACK_PROTECT], 0, 1000);
    int wprot_i     = clampi(args[P_WHITE_PROTECT], 0, 1000);
    int contrast_i  = clampi(args[P_LUMA_CONTRAST], 0, 1000);
    int direction_i = clampi(args[P_DIRECTION], -1, 1);
    int guard_i     = clampi(args[P_CHROMA_GUARD], 0, 1000);
    int beat_push_i = clampi(args[P_BEAT_PUSH], 0, 1000);
    int smooth_i    = clampi(args[P_BEAT_SMOOTH], 0, 1000);

    const int shaped = beat_shape(beat_push_i);
    const float target = (float)shaped * 0.001f;
    const float smooth_t = (float)smooth_i * 0.001f;
    const float attack = 0.18f + (1.0f - smooth_t) * 0.34f;
    const float release = 0.025f + (1.0f - smooth_t) * 0.095f;

    if(target > n->beat_env)
        n->beat_env += (target - n->beat_env) * attack;
    else
        n->beat_env += (target - n->beat_env) * release;

    if(n->beat_env < 0.0001f)
        n->beat_env = 0.0f;
    else if(n->beat_env > 1.0f)
        n->beat_env = 1.0f;

    const int beat_q = clampi((int)(n->beat_env * 1000.0f + 0.5f), 0, 1000);

    if(beat_q > 0) {
        vibrance_i = clampi(vibrance_i + (beat_q * 150 + 500) / 1000, 0, 760);
        pastel_i = clampi(pastel_i + (beat_q * 105 + 500) / 1000, 0, 540);
        flux_i = add_clamped(flux_i, (beat_q * 210 + 500) / 1000);
        softness_i = add_clamped(softness_i, (beat_q * 145 + 500) / 1000);
        bprot_i = mix_towards(bprot_i, 300, (beat_q * 140 + 500) / 1000);
        contrast_i = mix_towards(contrast_i, 455, (beat_q * 180 + 500) / 1000);
    }

    const float global_hue   = (float)hue_i * 0.001f * PI_X2;
    const float rainbow_idx  = (float)rainbow_i * (3.20f * (float)SIN_LUT_SIZE / 1000.0f);
    const float pastel_base  = (float)pastel_i * 0.001f * 14.0f;
    const float speed        = (float)flux_i * 0.00013f;
    const float contrast_val = 0.72f + ((float)contrast_i * 0.00090f);
    const float dir          = (float) direction_i;
    const int chroma_limit   = 42 + ((guard_i * 82 + 500) / 1000);
    const int luma_floor     = 4 + ((bprot_i * 20 + 500) / 1000) + ((beat_q * 8 + 500) / 1000);
    const int luma_knee      = 28 + ((bprot_i * 92 + 500) / 1000);
    const int luma_guard_q8  = clampi(112 + ((bprot_i * 96 + 500) / 1000) + ((beat_q * 36 + 500) / 1000), 0, 256);

    uint8_t *restrict py = frame->data[0];
    uint8_t *restrict pu = frame->data[1];
    uint8_t *restrict pv = frame->data[2];

    const int len = w * h;
    veejay_memcpy(n->srcY_copy, py, len);
    const uint8_t *restrict srcY = n->srcY_copy;

    if (contrast_i != n->last_contrast_param) {
        for (int i = 0; i < 256; i++) {
            float x = (i + 0.5f) * INV_255;
            n->contrast_lut[i] = clamp_u8((int)(powf(x, contrast_val) * 255.0f + 0.5f));
        }

        n->last_contrast_param = contrast_i;
        n->last_luma_floor = -1;
    }

    if (luma_floor != n->last_luma_floor ||
        luma_knee != n->last_luma_knee ||
        luma_guard_q8 != n->last_luma_guard_q8) {
        for (int i = 0; i < 256; i++) {
            n->luma_lut[i] = clamp_u8(cd_guard_luma_black_i(i, n->contrast_lut[i], luma_floor, luma_knee, luma_guard_q8));
        }

        n->last_luma_floor = luma_floor;
        n->last_luma_knee = luma_knee;
        n->last_luma_guard_q8 = luma_guard_q8;
    }

    if (pastel_i != n->last_pastel_param ||
        bprot_i != n->last_bprot_param ||
        wprot_i != n->last_wprot_param) {
        const float b_prot = (float)bprot_i * 0.001f;
        const float w_prot = (float)wprot_i * 0.001f;

        const float inv_b = 1.0f / (b_prot + 0.001f);
        const float inv_w = 1.0f / (1.0f - w_prot + 0.001f);

        for (int i = 0; i < 256; i++) {
            const float y_raw = (float)i * INV_255;
            const float m_b = (y_raw < b_prot) ? (y_raw * inv_b) : 1.0f;
            const float m_w = (y_raw > w_prot) ? (1.0f - (y_raw - w_prot) * inv_w) : 1.0f;
            const float mask = m_b * m_b * m_w * m_w;
            n->tint_lut_fp[i] = (int32_t)(pastel_base * mask * (float)FP_M + 0.5f);
        }

        n->last_pastel_param = pastel_i;
        n->last_bprot_param = bprot_i;
        n->last_wprot_param = wprot_i;
    }

    n->time += speed * dir;

    const float t = n->time;
    const float base_idx_f = ((global_hue + t) * SCALER_TO_LUT) + (t * 0.15f * rainbow_idx * INV_255);
    const int32_t base_idx_fp = (int32_t)(base_idx_f * (float)CD_IDX_ONE + ((base_idx_f >= 0.0f) ? 0.5f : -0.5f));
    const int32_t field_mul_fp = (int32_t)(rainbow_idx * INV_255 * (float)CD_IDX_ONE + 0.5f);

    const int32_t vibrance_fp = (int32_t)(((int64_t)(780000 + vibrance_i * 520) * FP_M + 500000) / 1000000);
    const int32_t softness_fp = (int32_t)(((int64_t)softness_i * 215 * FP_M + 50000) / 100000);

    const int16_t *restrict sin_lut = n->sin_lut_fp;
    const uint8_t *restrict luma_lut = n->luma_lut;
    const int32_t *restrict tint_lut = n->tint_lut_fp;

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for (int y = 1; y < h_sub_1; y++) {
        const uint8_t *row  = srcY + y * w;
        const uint8_t *up   = row - w;
        const uint8_t *down = row + w;

        uint8_t *yout = py + y * w;
        uint8_t *urow = pu + y * w;
        uint8_t *vrow = pv + y * w;

        for (int x = 1; x < w_sub_1; x++) {
            const int y_v = row[x];
            const int sum = y_v + row[x - 1] + row[x + 1] + up[x] + down[x];
            const int diff = y_v - ((sum * 13107) >> 16);
            const int mask = diff >> 31;
            const int edge_i = (diff + mask) ^ mask;
            const int field_i = y_v + (edge_i << 1);
            const int s_idx = (int)((base_idx_fp + field_mul_fp * field_i) >> CD_IDX_FP) & SIN_MASK;
            const int c_idx = (s_idx + (SIN_LUT_SIZE >> 2)) & SIN_MASK;

            const int32_t s_fp = sin_lut[s_idx];
            const int32_t c_fp = sin_lut[c_idx];

            const int32_t u = (int32_t)urow[x] - 128;
            const int32_t v = (int32_t)vrow[x] - 128;

            int32_t edge_damp = FP_M - ((softness_fp * edge_i) >> 8);
            int32_t stab_fp;

            if(edge_damp < (FP_M >> 2))
                edge_damp = FP_M >> 2;
            else if(edge_damp > FP_M)
                edge_damp = FP_M;

            stab_fp = (edge_damp * vibrance_fp + FP_HALF) >> FP_S;

            int32_t u_rot = (u * c_fp - v * s_fp + FP_HALF) >> FP_S;
            int32_t v_rot = (u * s_fp + v * c_fp + FP_HALF) >> FP_S;

            u_rot = (u_rot * stab_fp + FP_HALF) >> FP_S;
            v_rot = (v_rot * stab_fp + FP_HALF) >> FP_S;

            {
                const int32_t tint_fp = tint_lut[y_v];
                u_rot += (s_fp * tint_fp + FP_HALF) >> FP_S;
                v_rot += (c_fp * tint_fp + FP_HALF) >> FP_S;
            }

            cd_limit_chroma_i32(&u_rot, &v_rot, chroma_limit);

            yout[x] = luma_lut[y_v];

            urow[x] = clamp_u8((int)u_rot + 128);
            vrow[x] = clamp_u8((int)v_rot + 128);
        }
    }
}
