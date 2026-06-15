/*
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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

#include "common.h"
#include "melt.h"
#include "internal.h"
#include <stdint.h>

#define MELT_PARAMS 6

#define P_SPEED     0
#define P_INTENSITY 1
#define P_DAMPING   2
#define P_GRAVITY   3
#define P_CURL      4
#define P_ALPHA     5

#define FP_SHIFT     16
#define TRIG_SHIFT   14
#define TRIG_SCALE   (1 << TRIG_SHIFT)
#define LUT_BITS     12
#define LUT_SIZE     (1 << LUT_BITS)
#define LUT_MASK     (LUT_SIZE - 1)
#define COS_OFFSET   (LUT_SIZE >> 2)

static int16_t sin_lut_q14[LUT_SIZE];
static int lut_initialized = 0;

typedef struct {
    uint8_t *bufY;
    uint8_t *bufU;
    uint8_t *bufV;
    int32_t *vx;
    int32_t *vy;
    int32_t time_q16;
    int first_frame;
    uint32_t seed;
    int n_threads;
} melt_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t melt_blend255(uint8_t a, uint8_t b, int alpha)
{
    const int x = (int)a * (255 - alpha) + (int)b * alpha;
    return (uint8_t)(((x + 1) + (x >> 8)) >> 8);
}

static void init_trig_lut_q14(void)
{
    if(lut_initialized)
        return;

    for(int i = 0; i < LUT_SIZE; i++) {
        const float angle = ((float)i * 6.28318530718f) / (float)LUT_SIZE;
        sin_lut_q14[i] = (int16_t)(a_sin(angle) * (float)TRIG_SCALE);
    }

    lut_initialized = 1;
}

static inline int32_t get_hash_rand(uint32_t x, uint32_t y, uint32_t seed)
{
    uint32_t h = seed ^ x ^ (y * 397u);

    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;

    return (int32_t)(h & 0xffffu) - 32768;
}

vj_effect *melt_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MELT_PARAMS;
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

    ve->defaults[P_SPEED] = 10;
    ve->defaults[P_INTENSITY] = 64;
    ve->defaults[P_DAMPING] = 95;
    ve->defaults[P_GRAVITY] = 0;
    ve->defaults[P_CURL] = 0;
    ve->defaults[P_ALPHA] = 30;

    ve->limits[0][P_SPEED] = 0;      ve->limits[1][P_SPEED] = 100;
    ve->limits[0][P_INTENSITY] = 0;  ve->limits[1][P_INTENSITY] = 255;
    ve->limits[0][P_DAMPING] = 50;   ve->limits[1][P_DAMPING] = 100;
    ve->limits[0][P_GRAVITY] = -10;  ve->limits[1][P_GRAVITY] = 10;
    ve->limits[0][P_CURL] = 0;       ve->limits[1][P_CURL] = 100;
    ve->limits[0][P_ALPHA] = 0;      ve->limits[1][P_ALPHA] = 255;

    ve->description = "Melt";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Speed", "Intensity", "Damping", "Gravity", "Curl", "Alpha");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SPEED,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                  3,   76,  16, 62,  700, 2800, 0,    84,
        VJ_BEAT_WARP,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                  24,  220, 18, 68,  650, 2600, 0,    92,
        VJ_BEAT_MEMORY,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                  72,  100, 12, 46, 1000, 3600, 0,    64,
        VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS,             -7,  7,   10, 38, 1400, 4200, 0,    52,
        VJ_BEAT_TURBULENCE,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                  4,   92,  16, 62,  700, 2800, 0,    86,
        VJ_BEAT_SOURCE_MIX,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,              16,  180, 16, 42, 1200, 4200, 0,    46
    );

    return ve;
}

void melt_free(void *ptr)
{
    melt_t *t = (melt_t*) ptr;

    if(t) {
        free(t->bufY);
        free(t->vx);
        free(t);
    }
}

void *melt_malloc(int w, int h)
{
    const int len = w * h;
    melt_t *t = (melt_t*) vj_calloc(sizeof(melt_t));

    if(!t)
        return NULL;

    t->bufY = (uint8_t*) vj_malloc((size_t)len * 3);
    t->vx = (int32_t*) vj_calloc(sizeof(int32_t) * (size_t)len * 2);

    if(!t->bufY || !t->vx) {
        melt_free(t);
        return NULL;
    }

    t->bufU = t->bufY + len;
    t->bufV = t->bufU + len;
    t->vy = t->vx + len;
    t->time_q16 = 0;
    t->first_frame = 1;
    t->seed = 0x12345678u;
    t->n_threads = vje_advise_num_threads(len);

    init_trig_lut_q14();

    return (void*) t;
}

void melt_apply(void *ptr, VJFrame *A, int *args)
{
    melt_t *t = (melt_t*) ptr;

    const uint32_t width = A->width;
    const uint32_t height = A->height;
    const uint32_t len = A->len;

    const int32_t speed = args[P_SPEED];
    const int32_t intensity = args[P_INTENSITY];
    const int32_t damping = (args[P_DAMPING] * 1024) / 100;
    const int32_t gravity = args[P_GRAVITY] << FP_SHIFT;
    const int32_t curl_amt = args[P_CURL];
    const int32_t alpha_perc = args[P_ALPHA];
    const int32_t speed_q8 = speed << 8;
    const int32_t chroma_anchor = 32 + (alpha_perc >> 2);

    uint8_t *restrict srcY = A->data[0];
    uint8_t *restrict srcU = A->data[1];
    uint8_t *restrict srcV = A->data[2];

    uint8_t *restrict bufY = t->bufY;
    uint8_t *restrict bufU = t->bufU;
    uint8_t *restrict bufV = t->bufV;

    int32_t *restrict vx = t->vx;
    int32_t *restrict vy = t->vy;

    if(t->first_frame) {
        veejay_memcpy(bufY, srcY, len);
        veejay_memcpy(bufU, srcU, len);
        veejay_memcpy(bufV, srcV, len);
        t->first_frame = 0;
    }
    else if(alpha_perc > 0) {
#pragma omp parallel for num_threads(t->n_threads) schedule(static)
        for(uint32_t i = 0; i < len; i++) {
            bufY[i] = melt_blend255(bufY[i], srcY[i], alpha_perc);
            bufU[i] = melt_blend255(bufU[i], srcU[i], alpha_perc);
            bufV[i] = melt_blend255(bufV[i], srcV[i], alpha_perc);
        }
    }

    const int32_t ce_factor = curl_amt * ((speed_q8 * intensity) >> 4);
    const int32_t energy_base = (speed_q8 * intensity) >> 8;
    const int32_t w_minus1 = (int32_t)width - 1;
    const int32_t h_minus1 = (int32_t)height - 1;

#pragma omp parallel for num_threads(t->n_threads) schedule(static)
    for(uint32_t y = 0; y < height; y++) {
        const uint32_t row = y * width;

        for(uint32_t x = 0; x < width; x++) {
            const uint32_t i = row + x;
            int32_t vx_i = vx[i];
            int32_t vy_i = vy[i];

            const int32_t rx = get_hash_rand(x, y, t->seed);
            const int32_t ry = get_hash_rand(x, y + height, t->seed);
            const int32_t angle_idx = (int32_t)((((uint64_t)x + (uint64_t)y) << 16) + (uint32_t)t->time_q16) >> 21;
            const int32_t a_idx = (sin_lut_q14[angle_idx & LUT_MASK] +
                                  sin_lut_q14[(angle_idx + COS_OFFSET) & LUT_MASK]) >> 5;

            vx_i += (rx * energy_base) >> 14;
            vy_i += (ry * energy_base) >> 14;
            vx_i += (int32_t)(((int64_t)sin_lut_q14[(a_idx + COS_OFFSET) & LUT_MASK] * (int64_t)ce_factor) >> TRIG_SHIFT);
            vy_i += (int32_t)(((int64_t)sin_lut_q14[a_idx & LUT_MASK] * (int64_t)ce_factor) >> TRIG_SHIFT);
            vy_i += gravity;

            vx[i] = (vx_i * damping) >> 10;
            vy[i] = (vy_i * damping) >> 10;
        }
    }

    if(curl_amt)
        t->time_q16 += 1966;

#pragma omp parallel for num_threads(t->n_threads) schedule(static)
    for(uint32_t y = 0; y < height; y++) {
        const uint32_t row = y * width;

#pragma omp simd
        for(uint32_t x = 0; x < width; x++) {
            const uint32_t i = row + x;
            int32_t nx = (int32_t)x + (vx[i] >> FP_SHIFT);
            int32_t ny = (int32_t)y + (vy[i] >> FP_SHIFT);
            int32_t cnx = (int32_t)x + (vx[i] >> (FP_SHIFT + 1));
            int32_t cny = (int32_t)y + (vy[i] >> (FP_SHIFT + 1));

            nx = nx < 0 ? 0 : (nx >= (int32_t)width ? w_minus1 : nx);
            ny = ny < 0 ? 0 : (ny >= (int32_t)height ? h_minus1 : ny);
            cnx = cnx < 0 ? 0 : (cnx >= (int32_t)width ? w_minus1 : cnx);
            cny = cny < 0 ? 0 : (cny >= (int32_t)height ? h_minus1 : cny);

            const uint32_t src_idx = (uint32_t)ny * width + (uint32_t)nx;
            const uint32_t chroma_idx = (uint32_t)cny * width + (uint32_t)cnx;
            const uint8_t live_u = srcU[i];
            const uint8_t live_v = srcV[i];

            srcY[i] = bufY[src_idx];
            srcU[i] = melt_blend255(bufU[chroma_idx], live_u, chroma_anchor);
            srcV[i] = melt_blend255(bufV[chroma_idx], live_v, chroma_anchor);
        }
    }

    t->seed++;

    veejay_memcpy(bufY, srcY, len);
    veejay_memcpy(bufU, srcU, len);
    veejay_memcpy(bufV, srcV, len);
}
