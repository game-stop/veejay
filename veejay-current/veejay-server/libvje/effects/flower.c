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
#include "flower.h"
#include <math.h>
#include <stdint.h>
#include <omp.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FP_SHIFT 16
#define FP_MULT  (1 << FP_SHIFT)
#define FP_HALF  (1 << (FP_SHIFT - 1))

#define LUT_SIZE 8192
#define LUT_MASK (LUT_SIZE - 1)
#define TWO_PI_F 6.28318530717958647692f
#define INV_TWO_PI_F 0.15915494309189533577f

#define FLOWER_PARAMS 5
#define P_PETAL_COUNT  0
#define P_PETAL_LENGTH 1
#define P_PETAL_BLOOM  2
#define P_ROTATION     3
#define P_SPIN_SPEED   4

typedef struct
{
    uint8_t *buf[3];
    uint16_t *atan2_idx;
    uint16_t *dist_idx;

    int32_t cos_lut_1d[LUT_SIZE];
    int32_t exp_lut_1d[LUT_SIZE];

    int last_petal_count;
    int last_petal_length;
    int max_petal_length;

    float phase;

    int n_threads;
} flower_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int flower_fast_clamp(int x, int max_val)
{
    x &= ~(x >> 31);
    int diff = max_val - x;
    return x + (diff & (diff >> 31));
}

static inline float flower_wrap_phase(float p)
{
    while(p >= TWO_PI_F)
        p -= TWO_PI_F;
    while(p < 0.0f)
        p += TWO_PI_F;
    return p;
}

static void flower_build_cos_lut(flower_t *s, int petal_count)
{
    const float scale = TWO_PI_F / (float)(LUT_SIZE - 1);

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int i = 0; i < LUT_SIZE; i++) {
        const float angle = (float)i * scale;
        s->cos_lut_1d[i] = (int32_t)((cosf((float)petal_count * angle) * 0.5f) * (float)FP_MULT);
    }

    s->last_petal_count = petal_count;
}

static void flower_build_exp_lut(flower_t *s, int petal_length)
{
    const float inv_len = 1.0f / (float)(petal_length + 1);

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int i = 0; i < LUT_SIZE; i++) {
        const float decay = expf(-(float)i * inv_len);
        s->exp_lut_1d[i] = (int32_t)(decay * (float)FP_MULT + 0.5f);
    }

    s->last_petal_length = petal_length;
}

vj_effect *flower_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    const int max_len = clampi(((w < h) ? w : h) / 2, 1, LUT_SIZE - 1);
    const int def_len = max_len;

    ve->num_params = FLOWER_PARAMS;

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

    ve->limits[0][P_PETAL_COUNT]  = 1;     ve->limits[1][P_PETAL_COUNT]  = 200;     ve->defaults[P_PETAL_COUNT]  = 8;
    ve->limits[0][P_PETAL_LENGTH] = 1;     ve->limits[1][P_PETAL_LENGTH] = max_len; ve->defaults[P_PETAL_LENGTH] = def_len;
    ve->limits[0][P_PETAL_BLOOM]  = 0;     ve->limits[1][P_PETAL_BLOOM]  = 1000;    ve->defaults[P_PETAL_BLOOM]  = 500;
    ve->limits[0][P_ROTATION]     = 0;     ve->limits[1][P_ROTATION]     = 360;     ve->defaults[P_ROTATION]     = 0;
    ve->limits[0][P_SPIN_SPEED]   = -1000; ve->limits[1][P_SPIN_SPEED]   = 1000;    ve->defaults[P_SPIN_SPEED]   = 0;

    ve->description = "Flower";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Petal Count",
        "Petal Length",
        "Petal Bloom",
        "Rotation",
        "Spin Speed"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 2,                  64,                 4,  18, 2200, 7600, 1600, 38,
        VJ_BEAT_WINDOW_RADIUS,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       1,                  max_len,            18, 78,  260, 1800, 0,    84,
        VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       0,                  1000,               34, 100,  80,  900, 0,    100,
        VJ_BEAT_GEOMETRY_PHASE,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,                                0,                  360,                30, 96,  120, 1100, 0,    94,
        VJ_BEAT_SIGNED_SPEED,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, -1000,               1000,               24, 86,  180, 1500, 0,    86
    );

    return ve;
}

void *flower_malloc(int w, int h)
{
    flower_t *s = (flower_t*) vj_calloc(sizeof(flower_t));
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

    void *mem = vj_malloc((size_t)len * sizeof(uint16_t) * 2u);
    if(!mem) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->atan2_idx = (uint16_t *)mem;
    s->dist_idx = s->atan2_idx + len;

    s->n_threads = vje_advise_num_threads(len);

    const int cx = w >> 1;
    const int cy = h >> 1;
    const float angle_scale = (float)(LUT_SIZE - 1) * INV_TWO_PI_F;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; ++y) {
        const int dy = y - cy;
        const int dy2 = dy * dy;
        const int row = y * w;

        for(int x = 0; x < w; ++x) {
            const int i = row + x;
            const int dx = x - cx;

            const float angle = atan2f((float)dy, (float)dx);
            int a_idx = (int)((angle + (float)M_PI) * angle_scale + 0.5f);
            if(a_idx < 0)
                a_idx = 0;
            else if(a_idx >= LUT_SIZE)
                a_idx = LUT_SIZE - 1;

            const float dist = sqrtf((float)(dx * dx + dy2));
            int d_idx = (int)(dist + 0.5f);
            if(d_idx >= LUT_SIZE)
                d_idx = LUT_SIZE - 1;

            s->atan2_idx[i] = (uint16_t)a_idx;
            s->dist_idx[i] = (uint16_t)d_idx;
        }
    }

    s->last_petal_count = -1;
    s->last_petal_length = -1;
    s->max_petal_length = clampi(((w < h) ? w : h) / 2, 1, LUT_SIZE - 1);
    s->phase = 0.0f;

    return (void*) s;
}

void flower_free(void *ptr)
{
    flower_t *s = (flower_t*) ptr;

    free(s->buf[0]);
    free(s->atan2_idx);
    free(s);
}

void flower_apply(void *ptr, VJFrame *frame, int *args)
{
    flower_t *s = (flower_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int w_limit = width - 1;
    const int h_limit = height - 1;
    const int cx = width >> 1;
    const int cy = height >> 1;

    const int petal_count = args[P_PETAL_COUNT];
    const int petal_length = clampi(args[P_PETAL_LENGTH], 1, s->max_petal_length);
    const int bloom_i = args[P_PETAL_BLOOM];
    const int rotation_i = args[P_ROTATION];
    const int spin_i = args[P_SPIN_SPEED];

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];
    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    veejay_memcpy(bufY, srcY, len);
    veejay_memcpy(bufU, srcU, len);
    veejay_memcpy(bufV, srcV, len);

    if(petal_count != s->last_petal_count)
        flower_build_cos_lut(s, petal_count);

    if(petal_length != s->last_petal_length)
        flower_build_exp_lut(s, petal_length);

    const int bloom_fp = (FP_MULT >> 1) + (int)(((int64_t)bloom_i * FP_MULT + 500) / 1000);
    const float spin_step = (float)spin_i * 0.000070f;

    s->phase = flower_wrap_phase(s->phase + spin_step);

    const float base_phase = ((float)rotation_i * (TWO_PI_F / 360.0f)) + s->phase;
    const int phase_idx = (int)(base_phase * ((float)LUT_SIZE * INV_TWO_PI_F)) & LUT_MASK;

    const uint16_t *restrict angle_idx = s->atan2_idx;
    const uint16_t *restrict dist_idx = s->dist_idx;
    const int32_t *restrict cos_lut = s->cos_lut_1d;
    const int32_t *restrict exp_lut = s->exp_lut_1d;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < height; y++) {
        const int dy = y - cy;
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int i = row + x;
            const int dx = x - cx;

            const int a_idx = ((int)angle_idx[i] + phase_idx) & LUT_MASK;
            const int d_idx = (int)dist_idx[i];

            int64_t combined = (int64_t)cos_lut[a_idx] * (int64_t)exp_lut[d_idx];
            int32_t pval = (int32_t)(combined >> FP_SHIFT);
            pval = (int32_t)(((int64_t)pval * bloom_fp + FP_HALF) >> FP_SHIFT);

            const int mx = x + ((dx * pval) >> FP_SHIFT);
            const int my = y + ((dy * pval) >> FP_SHIFT);
            const int src_idx = flower_fast_clamp(my, h_limit) * width + flower_fast_clamp(mx, w_limit);

            srcY[i] = bufY[src_idx];
            srcU[i] = bufU[src_idx];
            srcV[i] = bufV[src_idx];
        }
    }
}
