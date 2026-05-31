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

#include "common.h"
#include "smartblur.h"
#include <omp.h>

typedef struct {
    uint8_t *region;
    uint8_t *small_src;
    float *a_buf;
    float *b_buf;
    float *tmp_mu;
    float *tmp_var;
    float *mI;
    float *mII;
    float *inv_counts;
    int w;
    int h;
    int sw;
    int sh;
    int small_len;
    int max_sdim;
    int n_threads;
} smartblur_t;

static inline int smartblur_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t smartblur_u8(float v)
{
    int iv = (int)(v + 0.5f);
    return (uint8_t)smartblur_clampi(iv, 0, 255);
}

static inline uintptr_t smartblur_align_up(uintptr_t p, uintptr_t a)
{
    return (p + (a - 1u)) & ~(a - 1u);
}

vj_effect *smartblur_init(int w, int h)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults  = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;
    ve->defaults[0] = 8;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 1000;
    ve->defaults[1] = 200;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 100;
    ve->defaults[2] = 50;

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->description = "Smart Blur";

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Radius",
        "Sharpness",
        "Chroma"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS, 2,  36,  8, 30, 1200, 3000, 0, 45, /* Radius */
        VJ_BEAT_CONTRAST,      VJ_BEAT_F_CONTINUOUS, 60, 460, 8, 30, 1200, 3000, 0, 50, /* Sharpness */
        VJ_BEAT_COLOR_AMOUNT,  VJ_BEAT_F_CONTINUOUS, 0,  100, 8, 30, 1200, 3000, 0, 45  /* Chroma */
    );

    (void) w;
    (void) h;

    return ve;
}

void *smartblur_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    smartblur_t *s = (smartblur_t*) vj_calloc(sizeof(*s));
    if(!s)
        return NULL;

    s->w = w;
    s->h = h;
    s->sw = (w + 1) >> 1;
    s->sh = (h + 1) >> 1;
    s->small_len = s->sw * s->sh;
    s->max_sdim = (s->sw > s->sh) ? s->sw : s->sh;

    s->n_threads = vje_advise_num_threads(w * h);
    if(s->n_threads < 1)
        s->n_threads = 1;

    const size_t small_bytes = (size_t)s->small_len;
    const size_t plane_floats = (size_t)s->small_len;
    const size_t prefix_floats = (size_t)s->n_threads * (size_t)(s->max_sdim + 1);
    const size_t inv_floats = (size_t)(s->max_sdim + 2);

    uintptr_t off = 0;
    const uintptr_t small_off = off;
    off += small_bytes;

    off = smartblur_align_up(off, (uintptr_t)sizeof(float));
    const uintptr_t a_off = off;
    off += plane_floats * sizeof(float);

    const uintptr_t b_off = off;
    off += plane_floats * sizeof(float);

    const uintptr_t tmp_mu_off = off;
    off += plane_floats * sizeof(float);

    const uintptr_t tmp_var_off = off;
    off += plane_floats * sizeof(float);

    const uintptr_t mi_off = off;
    off += prefix_floats * sizeof(float);

    const uintptr_t mii_off = off;
    off += prefix_floats * sizeof(float);

    const uintptr_t inv_off = off;
    off += inv_floats * sizeof(float);

    s->region = (uint8_t*) vj_malloc((size_t)off);
    if(!s->region) {
        free(s);
        return NULL;
    }

    s->small_src  = s->region + small_off;
    s->a_buf      = (float*)(void*)(s->region + a_off);
    s->b_buf      = (float*)(void*)(s->region + b_off);
    s->tmp_mu     = (float*)(void*)(s->region + tmp_mu_off);
    s->tmp_var    = (float*)(void*)(s->region + tmp_var_off);
    s->mI         = (float*)(void*)(s->region + mi_off);
    s->mII        = (float*)(void*)(s->region + mii_off);
    s->inv_counts = (float*)(void*)(s->region + inv_off);

    for(int i = 0; i < s->max_sdim + 2; i++)
        s->inv_counts[i] = (i > 0) ? (1.0f / (float)i) : 1.0f;

    return (void*) s;
}

void smartblur_free(void *ptr)
{
    smartblur_t *s = (smartblur_t*) ptr;
    if(!s)
        return;

    if(s->region)
        free(s->region);

    free(s);
}

static void smartblur_downsample_2x(smartblur_t *s, const uint8_t *restrict data)
{
    const int w = s->w;
    const int h = s->h;
    const int sw = s->sw;
    const int sh = s->sh;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < sh; y++) {
        const int y0 = y << 1;
        const int y1 = (y0 + 1 < h) ? y0 + 1 : y0;

        const uint8_t *restrict r0 = data + y0 * w;
        const uint8_t *restrict r1 = data + y1 * w;
        uint8_t *restrict out = s->small_src + y * sw;

        for(int x = 0; x < sw; x++) {
            const int x0 = x << 1;
            const int x1 = (x0 + 1 < w) ? x0 + 1 : x0;

            out[x] = (uint8_t)(((int)r0[x0] + (int)r0[x1] + (int)r1[x0] + (int)r1[x1] + 2) >> 2);
        }
    }
}

static void smartblur_build_coefficients(smartblur_t *s, int r, float eps, float offset)
{
    const int sw = s->sw;
    const int sh = s->sh;
    const int max_sdim = s->max_sdim;
    const float *restrict inv = s->inv_counts;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < sh; y++) {
        const int tid = omp_get_thread_num();

        float *restrict rowI  = s->mI  + (size_t)tid * (size_t)(max_sdim + 1);
        float *restrict rowII = s->mII + (size_t)tid * (size_t)(max_sdim + 1);

        const uint8_t *restrict line = s->small_src + y * sw;
        float *restrict tmu  = s->tmp_mu  + y * sw;
        float *restrict tvar = s->tmp_var + y * sw;

        rowI[0] = 0.0f;
        rowII[0] = 0.0f;

        for(int x = 0; x < sw; x++) {
            const float v = (float)line[x] - offset;
            rowI[x + 1] = rowI[x] + v;
            rowII[x + 1] = rowII[x] + v * v;
        }

        for(int x = 0; x < sw; x++) {
            const int r1 = (x - r < 0) ? 0 : x - r;
            const int r2 = (x + r >= sw) ? sw - 1 : x + r;
            const int count = r2 - r1 + 1;
            const float ic = inv[count];

            tmu[x]  = (rowI[r2 + 1]  - rowI[r1])  * ic;
            tvar[x] = (rowII[r2 + 1] - rowII[r1]) * ic;
        }
    }

#pragma omp parallel num_threads(s->n_threads)
    {
        const int tid = omp_get_thread_num();

        float *restrict colI  = s->mI  + (size_t)tid * (size_t)(max_sdim + 1);
        float *restrict colII = s->mII + (size_t)tid * (size_t)(max_sdim + 1);

#pragma omp for schedule(static)
        for(int x = 0; x < sw; x++) {
            colI[0] = 0.0f;
            colII[0] = 0.0f;

            for(int y = 0; y < sh; y++) {
                const int idx = y * sw + x;

                colI[y + 1] = colI[y] + s->tmp_mu[idx];
                colII[y + 1] = colII[y] + s->tmp_var[idx];
            }

            for(int y = 0; y < sh; y++) {
                const int r1 = (y - r < 0) ? 0 : y - r;
                const int r2 = (y + r >= sh) ? sh - 1 : y + r;
                const int count = r2 - r1 + 1;
                const float ic = inv[count];

                const float mu = (colI[r2 + 1] - colI[r1]) * ic;
                float var = (colII[r2 + 1] - colII[r1]) * ic - mu * mu;

                if(var < 0.0f)
                    var = 0.0f;

                const float a = var / (var + eps);
                const int idx = y * sw + x;

                s->a_buf[idx] = a;
                s->b_buf[idx] = (1.0f - a) * mu;
            }
        }
    }
}

static void smartblur_apply_coefficients(smartblur_t *s,
                                         uint8_t *restrict data,
                                         float offset,
                                         float strength,
                                         int luma_only)
{
    if(strength <= 0.001f)
        return;

    const int w = s->w;
    const int h = s->h;
    const int sw = s->sw;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < h; y++) {
        const int ys = y >> 1;
        uint8_t *restrict out = data + y * w;

        if(luma_only) {
            for(int x = 0; x < w; x++) {
                const int xs = x >> 1;
                const int si = ys * sw + xs;
                const float a = s->a_buf[si];
                const float b = s->b_buf[si];

                out[x] = smartblur_u8((float)out[x] * a + b);
            }
        } else {
            for(int x = 0; x < w; x++) {
                const int xs = x >> 1;
                const int si = ys * sw + xs;
                const float a = s->a_buf[si];
                const float b = s->b_buf[si];
                const float v_coeff = 1.0f + strength * (a - 1.0f);
                const float sb_off  = strength * (b + offset * (1.0f - a));

                out[x] = smartblur_u8((float)out[x] * v_coeff + sb_off);
            }
        }
    }
}

static void smartblur_plane(smartblur_t *s,
                            uint8_t *restrict data,
                            int radius,
                            float eps,
                            float offset,
                            float strength,
                            int luma_only)
{
    if(!data || strength <= 0.001f)
        return;

    const int max_r = ((s->sw < s->sh) ? s->sw : s->sh) - 1;
    const int r = smartblur_clampi(radius, 1, (max_r > 1) ? max_r : 1);

    smartblur_downsample_2x(s, data);
    smartblur_build_coefficients(s, r, eps, offset);
    smartblur_apply_coefficients(s, data, offset, strength, luma_only);
}

void smartblur_apply(void *ptr, VJFrame *frame, int *args)
{
    smartblur_t *s = (smartblur_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    int radius = smartblur_clampi(args[0], 1, 100);
    int sharpness = smartblur_clampi(args[1], 1, 1000);
    int chroma = smartblur_clampi(args[2], 0, 100);

    float eps = (float)sharpness * 0.1f;
    eps *= eps;

    if(eps < 0.0001f)
        eps = 0.0001f;

    const float chroma_strength = (float)chroma * 0.01f;

    smartblur_plane(s, frame->data[0], radius, eps, 0.0f,   1.0f,            1);
    smartblur_plane(s, frame->data[1], radius, eps, 128.0f, chroma_strength, 0);
    smartblur_plane(s, frame->data[2], radius, eps, 128.0f, chroma_strength, 0);
}