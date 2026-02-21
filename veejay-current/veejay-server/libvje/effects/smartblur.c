/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2026 Niels Elburg <nwelburg@gmail.com>
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
#include <config.h>
#include <math.h>
#include <omp.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "smartblur.h"

typedef struct {
    uint8_t *small_src;
    float *a_buf;
    float *b_buf;
    float *tmp_mu;
    float *tmp_var;
    float *mI;
    float *mII;
    float *inv_counts;
    int w, h, sw, sh;
} smartblur_t;

vj_effect *smartblur_init(int w, int h)
{
    vj_effect *ve = vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = vj_calloc(sizeof(int) * 3);
    ve->limits[0] = vj_calloc(sizeof(int) * 3);
    ve->limits[1] = vj_calloc(sizeof(int) * 3);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 1000;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 100;

    ve->defaults[0] = 8;
    ve->defaults[1] = 200;
    ve->defaults[2] = 50;

    ve->sub_format = 1;
    ve->description = "Smart Blur";
    ve->param_description = vje_build_param_list(3, "Radius", "Sharpness", "Chroma");
    return ve;
}

void *smartblur_malloc(int w, int h)
{
    smartblur_t *s = vj_malloc(sizeof(*s));
    if (!s) return NULL;

    s->w = w;  s->h = h;
    s->sw = w >> 1;
    s->sh = h >> 1;

    const int nt = omp_get_max_threads();
    const int max_sdim = (s->sw > s->sh) ? s->sw : s->sh;

    s->small_src = vj_malloc((size_t)s->sw * s->sh);
    s->a_buf     = vj_malloc((size_t)s->sw * s->sh * sizeof(float));
    s->b_buf     = vj_malloc((size_t)s->sw * s->sh * sizeof(float));
    s->tmp_mu    = vj_malloc((size_t)s->sw * s->sh * sizeof(float));
    s->tmp_var   = vj_malloc((size_t)s->sw * s->sh * sizeof(float));
    s->mI        = vj_malloc((size_t)nt * (max_sdim + 1) * sizeof(float));
    s->mII       = vj_malloc((size_t)nt * (max_sdim + 1) * sizeof(float));
    s->inv_counts = vj_malloc((size_t)(max_sdim * 2 + 2) * sizeof(float));

    if (!s->small_src || !s->a_buf || !s->b_buf || !s->tmp_mu || !s->tmp_var)
        return NULL;

    for (int i = 0; i < max_sdim * 2 + 2; i++)
        s->inv_counts[i] = (i > 0) ? (1.0f / (float)i) : 1.0f;

    return s;
}

void smartblur_free(void *ptr)
{
    smartblur_t *s = (smartblur_t *)ptr;
    if (!s) return;
    free(s->small_src); free(s->a_buf); free(s->b_buf);
    free(s->tmp_mu); free(s->tmp_var);
    free(s->mI); free(s->mII); free(s->inv_counts);
    free(s);
}

static inline uint8_t clamp_u8(float v)
{
    return (uint8_t)__builtin_fmaxf(0.0f, __builtin_fminf(255.0f, v));
}

#pragma GCC optimize ("unroll-loops","tree-vectorize")
static void apply_nuclear_plane_tiled(smartblur_t *s, uint8_t * restrict data,
                               int r, float eps, float offset, float strength, int luma_only)
{
    if (strength <= 0.001f) return;

    const int w = s->w, h = s->h, sw = s->sw, sh = s->sh;
    const float * restrict inv = s->inv_counts;
    const int max_sdim = (sw > sh) ? sw : sh;
    const int tile_w = cpu_cache_size();

    // downsample
    for (int y = 0; y < sh; y++) {
        uint8_t * restrict s_line = s->small_src + y * sw;
        uint8_t * restrict d1 = data + (y * 2 * w);
        uint8_t * restrict d2 = d1 + w;
        for (int x = 0; x < sw; x++) {
            int x2 = x << 1;
            s_line[x] = (d1[x2] + d1[x2 + 1] + d2[x2] + d2[x2 + 1]) >> 2;
        }
    }

    // horizontal pass
    {
        const int tid = 0; 
        float * restrict rowI  = s->mI  + tid * (max_sdim + 1);
        float * restrict rowII = s->mII + tid * (max_sdim + 1);

        for (int y = 0; y < sh; y++) {
            uint8_t * restrict line = s->small_src + y * sw;
            float * restrict tmu   = s->tmp_mu  + y * sw;
            float * restrict tvar  = s->tmp_var + y * sw;
            float accI = 0.0f, accII = 0.0f;

            for (int x = 0; x < sw; x++) {
                float v = (float)line[x] - offset;
                accI += v; accII += v * v;
                rowI[x]  = accI; rowII[x] = accII;
            }

            for (int x = 0; x < sw; x++) {
                int r1 = (x - r < 0) ? 0 : x - r;
                int r2 = (x + r >= sw) ? sw - 1 : x + r;
                float ic = inv[r2 - r1 + 1];
                float sumI  = rowI[r2]; float sumII = rowII[r2];
                if (r1 > 0) { sumI -= rowI[r1 - 1]; sumII -= rowII[r1 - 1]; }
                tmu[x] = sumI * ic; tvar[x] = sumII * ic;
            }
        }
    }

    // vertical pass
    {
        const int tid = 0;
        float * restrict colI  = s->mI  + tid * (max_sdim + 1);
        float * restrict colII = s->mII + tid * (max_sdim + 1);

        for (int tx = 0; tx < sw; tx += tile_w) {
            int tw = (tx + tile_w > sw) ? sw - tx : tile_w;
            for (int x = tx; x < tx + tw; x++) {
                float accI = 0.0f, accII = 0.0f;
                for (int y = 0; y < sh; y++) {
                    int idx = y * sw + x;
                    accI += s->tmp_mu[idx]; accII += s->tmp_var[idx];
                    colI[y]  = accI; colII[y] = accII;
                }
                for (int y = 0; y < sh; y++) {
                    int r1 = (y - r < 0) ? 0 : y - r;
                    int r2 = (y + r >= sh) ? sh - 1 : y + r;
                    float ic = inv[r2 - r1 + 1];
                    float sumI  = colI[r2]; float sumII = colII[r2];
                    if (r1 > 0) { sumI -= colI[r1 - 1]; sumII -= colII[r1 - 1]; }
                    float mu  = sumI * ic;
                    float var = (sumII * ic) - mu * mu;
                    float a   = var / (var + eps);
                    int idx = y * sw + x;
                    s->a_buf[idx] = a;
                    s->b_buf[idx] = (1.0f - a) * mu;
                }
            }
        }
    }

    // midpoint
    if (luma_only) {
        // full strength luma
        for (int y = 0; y < h; y++) {
            int ys = y >> 1;
            const float * restrict a_ptr = s->a_buf + ys * sw;
            const float * restrict b_ptr = s->b_buf + ys * sw;
            uint8_t * restrict out = data + y * w;

            for (int x = 0; x < (w & ~1); x += 2) {
                const float a = *a_ptr++;
                const float b = *b_ptr++;
                out[x]   = clamp_u8((float)out[x] * a + b);
                out[x+1] = clamp_u8((float)out[x+1] * a + b);
            }
            if (w & 1) {
                int x = w - 1;
                float a = s->a_buf[ys * sw + (x >> 1)];
                float b = s->b_buf[ys * sw + (x >> 1)];
                out[x] = clamp_u8((float)out[x] * a + b);
            }
        }
    } else {
        const float s_val = strength;
        for (int y = 0; y < h; y++) {
            int ys = y >> 1;
            const float * restrict a_ptr = s->a_buf + ys * sw;
            const float * restrict b_ptr = s->b_buf + ys * sw;
            uint8_t * restrict out = data + y * w;

            for (int x = 0; x < (w & ~1); x += 2) {
                const float av = *a_ptr++;
                const float bv = *b_ptr++;
                const float v_coeff = 1.0f + s_val * (av - 1.0f);
                const float sb_off  = s_val * (bv + offset * (1.0f - av));

                out[x]   = clamp_u8((float)out[x] * v_coeff + sb_off);
                out[x+1] = clamp_u8((float)out[x+1] * v_coeff + sb_off);
            }
            if (w & 1) {
                int x = w - 1;
                float av = s->a_buf[ys * sw + (x >> 1)];
                float bv = s->b_buf[ys * sw + (x >> 1)];
                float v_coeff = 1.0f + s_val * (av - 1.0f);
                float sb_off  = s_val * (bv + offset * (1.0f - av));
                out[x] = clamp_u8((float)out[x] * v_coeff + sb_off);
            }
        }
    }
}



void smartblur_apply(void *ptr, VJFrame *frame, int *args)
{
    smartblur_t *s = (smartblur_t *)ptr;

    int r = args[0]; 
    float eps = ((float)args[1] * 0.1f);
    eps *= eps;

    float c_strength = (float)args[2] / 100.0f;

    apply_nuclear_plane_tiled(s, frame->data[0], r, eps, 0.0f,   1.0f,1);
    apply_nuclear_plane_tiled(s, frame->data[1], r, eps, 128.0f, c_strength,0);
    apply_nuclear_plane_tiled(s, frame->data[2], r, eps, 128.0f, c_strength,0);
}
