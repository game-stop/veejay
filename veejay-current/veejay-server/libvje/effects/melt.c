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
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>
#include "melt.h"
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <omp.h>
#include <unistd.h>

#define FP_SHIFT     16
#define FP_SCALE     (1 << FP_SHIFT)
#define TRIG_SHIFT   14
#define TRIG_SCALE   (1 << TRIG_SHIFT)
#define LUT_BITS     12
#define LUT_SIZE     (1 << LUT_BITS)
#define LUT_MASK     (LUT_SIZE - 1)
#define COS_OFFSET   (LUT_SIZE >> 2)

static int16_t sin_lut_q14[LUT_SIZE];
static int     lut_initialized = 0;

typedef struct {
    uint8_t *bufY, *bufU, *bufV;
    int32_t *vx;
    int32_t *vy;
    int32_t time_q16;
    int first_frame;
    uint32_t seed;
    int n_threads;
} melt_t;


static void init_trig_lut_q14(void) {
    if (lut_initialized) return;
    for (int i = 0; i < LUT_SIZE; ++i) {
        double angle = (2.0 * M_PI * i) / LUT_SIZE;
        sin_lut_q14[i] = (int16_t)(sin(angle) * TRIG_SCALE);
    }
    lut_initialized = 1;
}

vj_effect *melt_init(int w, int h) {
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 10;
    ve->defaults[1] = 64;
    ve->defaults[2] = 95;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->defaults[5] = 0;

    ve->limits[0][0] = 0;   ve->limits[1][0] = 100;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 255;
    ve->limits[0][2] = 50;  ve->limits[1][2] = 100;
    ve->limits[0][3] = -10; ve->limits[1][3] = 10;
    ve->limits[0][4] = 0;   ve->limits[1][4] = 100;
    ve->limits[0][5] = 0;   ve->limits[1][5] = 255;

    ve->description = "Melt";
    ve->extra_frame = 0;
    ve->sub_format  = 1;
    ve->has_user    = 0;
    ve->parallel    = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Speed", "Intensity", "Damping", "Gravity", "Curl", "Alpha");

    return ve;
}


void *melt_malloc(int w, int h) {
    const uint32_t len = w * h;
    melt_t *t = (melt_t*) vj_malloc(sizeof(melt_t));
    if (!t) return NULL;

    t->bufY = (uint8_t*) vj_malloc(len);
    t->bufU = (uint8_t*) vj_malloc(len);
    t->bufV = (uint8_t*) vj_malloc(len);
    t->vx = (int32_t*) vj_calloc(sizeof(int32_t) * len);
    t->vy = (int32_t*) vj_calloc(sizeof(int32_t) * len);

    t->time_q16 = 0;
    t->first_frame = 1;
    t->seed = 0x12345678u;

    init_trig_lut_q14();


	t->n_threads = vje_advise_num_threads(len);

	return (void*) t;
}

static inline int32_t get_hash_rand(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = seed ^ x ^ (y * 397);
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return (int32_t)(h & 0xFFFF) - 32768;
}

void melt_free(void *ptr) {
    melt_t *t = (melt_t*) ptr;
    if (t) {
        free(t->bufY); free(t->bufU); free(t->bufV);
        free(t->vx); free(t->vy);
        free(t);
    }
}
void melt_apply(void *ptr, VJFrame *A, int *args) {
    melt_t *t = (melt_t*) ptr;
    const uint32_t width  = A->width, height = A->height, len = A->len;

    const int32_t speed_q8   = args[0] << 8;
    const int32_t intensity  = args[1];
    const int32_t damping    = (args[2] * 1024) / 100;
    const int32_t gravity    = args[3] << 16;
    const int32_t curl_amt   = args[4];
    const int32_t alpha_perc = args[5];
   	const int32_t alpha_inv = 255 - alpha_perc;

    const int32_t alpha_smear = 100 - alpha_perc;
    uint8_t *restrict srcY = A->data[0];
    uint8_t *restrict srcU = A->data[1];
    uint8_t *restrict srcV = A->data[2];
    uint8_t *restrict bufY = t->bufY;
    uint8_t *restrict bufU = t->bufU;
    uint8_t *restrict bufV = t->bufV;
    int32_t *restrict vx  = t->vx;
    int32_t *restrict vy  = t->vy;

    if (t->first_frame) {
        veejay_memcpy(bufY, srcY, len);
        veejay_memcpy(bufU, srcU, len);
        veejay_memcpy(bufV, srcV, len);
        t->first_frame = 0;
    } else if (alpha_perc > 0) {
    	#pragma omp simd
		for (uint32_t i = 0; i < len; ++i) {
			bufY[i] = (uint8_t)((bufY[i] * alpha_inv + srcY[i] * alpha_perc + 127) >> 8);
			bufU[i] = (uint8_t)((bufU[i] * alpha_inv + srcU[i] * alpha_perc + 127) >> 8);
			bufV[i] = (uint8_t)((bufV[i] * alpha_inv + srcV[i] * alpha_perc + 127) >> 8);
		}
	}

	int32_t ce_factor = (curl_amt * ((speed_q8 * intensity) >> 4));
    const int32_t energy_base = (speed_q8 * intensity) >> 8;
    const int32_t w_minus1 = (int32_t)width - 1;
    const int32_t h_minus1 = (int32_t)height - 1;

    #pragma omp parallel for num_threads(t->n_threads) schedule(static)
	for (uint32_t y = 0; y < height; ++y) {
		const uint32_t row = y * width;
		for (uint32_t x = 0; x < width; ++x) {
			uint32_t i = row + x;
			int32_t vx_i = vx[i], vy_i = vy[i];

            int32_t rx = get_hash_rand(x, y, t->seed);
            int32_t ry = get_hash_rand(x, y + height, t->seed);
            vx_i += (rx * energy_base) >> 14;
            vy_i += (ry * energy_base) >> 14;

            int32_t angle_idx = ((x << 16) + (y << 16) + t->time_q16) >> 21;
            int32_t a_idx = (sin_lut_q14[angle_idx & LUT_MASK] +
                             sin_lut_q14[(angle_idx + COS_OFFSET) & LUT_MASK]) >> 5;

            vx_i += (sin_lut_q14[(a_idx + COS_OFFSET) & LUT_MASK] * ce_factor) >> TRIG_SHIFT;
            vy_i += (sin_lut_q14[a_idx & LUT_MASK] * ce_factor) >> TRIG_SHIFT;

            vy_i += gravity;
            vx[i] = (vx_i * damping) >> 10;
            vy[i] = (vy_i * damping) >> 10;
		}
	}

	t->time_q16 += 1966 * (curl_amt != 0);

    #pragma omp parallel for num_threads(t->n_threads) schedule(static)
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t row = y * width;

        #pragma omp simd
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t i = row + x;

            int32_t nx = x + (vx[i] >> FP_SHIFT);
            int32_t ny = y + (vy[i] >> FP_SHIFT);

			nx = nx < 0 ? 0 : (nx >= width ? w_minus1: nx);
            ny = ny < 0 ? 0 : (ny >= height ? h_minus1 : ny);

            uint32_t srcIdx = ny * width + nx;
            srcY[i] = bufY[srcIdx];
            srcU[i] = bufU[srcIdx];
            srcV[i] = bufV[srcIdx];
        }
    }

    t->seed++;
    veejay_memcpy(bufY, srcY, len);
    veejay_memcpy(bufU, srcU, len);
    veejay_memcpy(bufV, srcV, len);
}
