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
#include "gradientfield.h"

#define GRADIENTFIELD_PARAMS 2
#define P_WINDOW  0
#define P_OPACITY 1

typedef struct {
    int n_threads;
    int width;
    int height;
    int len;
    void *region;

    uint8_t *copyY;
    uint8_t *copyU;
    uint8_t *copyV;

    uint32_t *intY_sum;
    uint64_t *intY_sq;
    uint32_t *intU_sum;
    uint32_t *intV_sum;

    uint32_t inv_area_lut[1024];
} gradientfield_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline size_t gradientfield_align_size(size_t off, size_t align)
{
    return (off + align - 1) & ~(align - 1);
}

static inline uint8_t gradientfield_blend255(uint8_t a, uint8_t b, int opacity)
{
    const int inv = 255 - opacity;
    const int x = (int)a * inv + (int)b * opacity;
    return (uint8_t)(((x + 1) + (x >> 8)) >> 8);
}

vj_effect *gradientfield_init(int w, int h)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = GRADIENTFIELD_PARAMS;
    ve->defaults = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

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

    ve->limits[0][P_WINDOW] = 2;  ve->limits[1][P_WINDOW] = 30;  ve->defaults[P_WINDOW] = 6;
    ve->limits[0][P_OPACITY] = 0; ve->limits[1][P_OPACITY] = 255; ve->defaults[P_OPACITY] = 0;

    ve->description = "Kuwahara Painting";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Window Size", "Opacity");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,             3,  24,  4,  14, 3000, 8200, 2200, 22,
        VJ_BEAT_SOURCE_MIX,    VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED,                                      0,  210, 14, 54,  800, 3000, 0,    78
    );

    return ve;
}

void *gradientfield_malloc(int w, int h)
{
    gradientfield_t *s = (gradientfield_t*) vj_calloc(sizeof(gradientfield_t));

    if(!s)
        return NULL;

    const size_t len = (size_t)w * (size_t)h;
    const size_t stride = (size_t)w + 1u;
    const size_t ilen = stride * ((size_t)h + 1u);
    size_t off = 0;

    off += len * 3u;
    off = gradientfield_align_size(off, sizeof(uint32_t));
    off += ilen * sizeof(uint32_t);
    off = gradientfield_align_size(off, sizeof(uint64_t));
    off += ilen * sizeof(uint64_t);
    off = gradientfield_align_size(off, sizeof(uint32_t));
    off += ilen * sizeof(uint32_t) * 2u;

    s->region = vj_calloc(off);

    if(!s->region) {
        free(s);
        return NULL;
    }

    uint8_t *base = (uint8_t*)s->region;
    size_t p = 0;

    s->copyY = base + p; p += len;
    s->copyU = base + p; p += len;
    s->copyV = base + p; p += len;

    p = gradientfield_align_size(p, sizeof(uint32_t));
    s->intY_sum = (uint32_t*)(base + p); p += ilen * sizeof(uint32_t);

    p = gradientfield_align_size(p, sizeof(uint64_t));
    s->intY_sq = (uint64_t*)(base + p); p += ilen * sizeof(uint64_t);

    p = gradientfield_align_size(p, sizeof(uint32_t));
    s->intU_sum = (uint32_t*)(base + p); p += ilen * sizeof(uint32_t);
    s->intV_sum = (uint32_t*)(base + p);

    s->width = w;
    s->height = h;
    s->len = (int)len;
    s->n_threads = vje_advise_num_threads((int)len);

    for(int i = 1; i < 1024; i++)
        s->inv_area_lut[i] = (1u << 16) / (uint32_t)i;

    return s;
}

void gradientfield_free(void *ptr)
{
    gradientfield_t *s = (gradientfield_t*) ptr;

    free(s->region);
    free(s);
}

static void gradientfield_integral_y(const uint8_t *restrict src,
                                     uint32_t *restrict int_sum,
                                     uint64_t *restrict int_sq,
                                     int w,
                                     int h,
                                     int n_threads)
{
    const int stride = w + 1;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        const uint8_t *restrict src_row = src + (size_t)y * (size_t)w;
        uint32_t *restrict sum_row = int_sum + (size_t)(y + 1) * (size_t)stride + 1u;
        uint64_t *restrict sq_row = int_sq + (size_t)(y + 1) * (size_t)stride + 1u;
        uint32_t row_s = 0;
        uint64_t row_sq = 0;

#pragma omp simd
        for(int x = 0; x < w; x++) {
            const uint8_t v = src_row[x];

            row_s += v;
            row_sq += (uint64_t)v * (uint64_t)v;
            sum_row[x] = row_s;
            sq_row[x] = row_sq;
        }
    }

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int x0 = 1; x0 <= w; x0 += 64) {
        const int x1 = (x0 + 64 <= w + 1) ? x0 + 64 : w + 1;

        for(int y = 1; y < h; y++) {
            uint32_t *restrict prev_sum = int_sum + (size_t)y * (size_t)stride;
            uint32_t *restrict curr_sum = int_sum + (size_t)(y + 1) * (size_t)stride;
            uint64_t *restrict prev_sq = int_sq + (size_t)y * (size_t)stride;
            uint64_t *restrict curr_sq = int_sq + (size_t)(y + 1) * (size_t)stride;

#pragma omp simd
            for(int x = x0; x < x1; x++) {
                curr_sum[x] += prev_sum[x];
                curr_sq[x] += prev_sq[x];
            }
        }
    }
}

static void gradientfield_integral_sum(const uint8_t *restrict src,
                                       uint32_t *restrict int_sum,
                                       int w,
                                       int h,
                                       int n_threads)
{
    const int stride = w + 1;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        const uint8_t *restrict src_row = src + (size_t)y * (size_t)w;
        uint32_t *restrict sum_row = int_sum + (size_t)(y + 1) * (size_t)stride + 1u;
        uint32_t row_s = 0;

#pragma omp simd
        for(int x = 0; x < w; x++) {
            row_s += src_row[x];
            sum_row[x] = row_s;
        }
    }

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int x0 = 1; x0 <= w; x0 += 64) {
        const int x1 = (x0 + 64 <= w + 1) ? x0 + 64 : w + 1;

        for(int y = 1; y < h; y++) {
            uint32_t *restrict prev = int_sum + (size_t)y * (size_t)stride;
            uint32_t *restrict curr = int_sum + (size_t)(y + 1) * (size_t)stride;

#pragma omp simd
            for(int x = x0; x < x1; x++)
                curr[x] += prev[x];
        }
    }
}

static inline void gradientfield_stats_y(const uint32_t *restrict sum,
                                         const uint64_t *restrict sq,
                                         int r0,
                                         int r1,
                                         int x0,
                                         int x1,
                                         uint32_t *restrict S,
                                         uint64_t *restrict SS)
{
    const int c0 = x0;
    const int c1 = x1 + 1;

    *S = sum[r1 + c1] - sum[r0 + c1] - sum[r1 + c0] + sum[r0 + c0];
    *SS = sq[r1 + c1] - sq[r0 + c1] - sq[r1 + c0] + sq[r0 + c0];
}

static inline uint32_t gradientfield_stats_sum(const uint32_t *restrict sum,
                                               int r0,
                                               int r1,
                                               int x0,
                                               int x1)
{
    const int c0 = x0;
    const int c1 = x1 + 1;

    return sum[r1 + c1] - sum[r0 + c1] - sum[r1 + c0] + sum[r0 + c0];
}

static inline void gradientfield_apply_rowpair(
    const gradientfield_t *restrict s,
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    int a,
    int opacity,
    int y)
{
    const int w = s->width;
    const int h = s->height;
    const int stride = w + 1;
    const uint8_t *restrict copyY = s->copyY;
    const uint8_t *restrict copyU = s->copyU;
    const uint8_t *restrict copyV = s->copyV;
    const uint32_t *restrict intY_sum = s->intY_sum;
    const uint64_t *restrict intY_sq = s->intY_sq;
    const uint32_t *restrict intU_sum = s->intU_sum;
    const uint32_t *restrict intV_sum = s->intV_sum;
    const uint32_t *restrict inv_area_lut = s->inv_area_lut;

    const int y0 = y - a < 0 ? 0 : y - a;
    const int y1 = y;
    const int y2 = y + a >= h ? h - 1 : y + a;
    const int y_clip = y1 + 1 > y2 ? y2 : y1 + 1;

    const uint32_t h_t = (uint32_t)(y1 - y0 + 1);
    const uint32_t h_b = (uint32_t)(y2 - y_clip + 1);

    const int r_y0 = y0 * stride;
    const int r_y1_p1 = (y1 + 1) * stride;
    const int r_y_clip = y_clip * stride;
    const int r_y2_p1 = (y2 + 1) * stride;

    for(int x = 0; x < w - 1; x += 2) {
        const int x0 = x - a < 0 ? 0 : x - a;
        const int x1 = x;
        const int x2 = x + a >= w ? w - 1 : x + a;
        const int x_clip = x1 + 1 > x2 ? x2 : x1 + 1;

        const uint32_t w_l = (uint32_t)(x1 - x0 + 1);
        const uint32_t w_r = (uint32_t)(x2 - x_clip + 1);

        const uint32_t area0 = w_l * h_t;
        const uint32_t area1 = w_r * h_t;
        const uint32_t area2 = w_l * h_b;
        const uint32_t area3 = w_r * h_b;

        uint32_t best_s;
        uint64_t best_ss;
        uint64_t best_crit;
        uint32_t cur_s;
        uint64_t cur_ss;
        uint64_t cur_crit;
        uint32_t best_area = area0;
        int best_x0 = x0;
        int best_x1 = x1;
        int best_r0 = r_y0;
        int best_r1 = r_y1_p1;

        gradientfield_stats_y(intY_sum, intY_sq, r_y0, r_y1_p1, x0, x1, &best_s, &best_ss);
        best_crit = (uint64_t)area0 * best_ss - (uint64_t)best_s * (uint64_t)best_s;

        gradientfield_stats_y(intY_sum, intY_sq, r_y0, r_y1_p1, x_clip, x2, &cur_s, &cur_ss);
        cur_crit = (uint64_t)area1 * cur_ss - (uint64_t)cur_s * (uint64_t)cur_s;
        if(cur_crit < best_crit) {
            best_s = cur_s;
            best_crit = cur_crit;
            best_area = area1;
            best_x0 = x_clip;
            best_x1 = x2;
        }

        gradientfield_stats_y(intY_sum, intY_sq, r_y_clip, r_y2_p1, x0, x1, &cur_s, &cur_ss);
        cur_crit = (uint64_t)area2 * cur_ss - (uint64_t)cur_s * (uint64_t)cur_s;
        if(cur_crit < best_crit) {
            best_s = cur_s;
            best_crit = cur_crit;
            best_area = area2;
            best_x0 = x0;
            best_x1 = x1;
            best_r0 = r_y_clip;
            best_r1 = r_y2_p1;
        }

        gradientfield_stats_y(intY_sum, intY_sq, r_y_clip, r_y2_p1, x_clip, x2, &cur_s, &cur_ss);
        cur_crit = (uint64_t)area3 * cur_ss - (uint64_t)cur_s * (uint64_t)cur_s;
        if(cur_crit < best_crit) {
            best_s = cur_s;
            best_area = area3;
            best_x0 = x_clip;
            best_x1 = x2;
            best_r0 = r_y_clip;
            best_r1 = r_y2_p1;
        }

        const uint32_t inv_area = inv_area_lut[best_area];
        const uint8_t mean_y = (uint8_t)((best_s * inv_area) >> 16);
        const uint8_t mean_u = (uint8_t)((gradientfield_stats_sum(intU_sum, best_r0, best_r1, best_x0, best_x1) * inv_area) >> 16);
        const uint8_t mean_v = (uint8_t)((gradientfield_stats_sum(intV_sum, best_r0, best_r1, best_x0, best_x1) * inv_area) >> 16);

        const int p0 = y * w + x;
        const int p1 = p0 + w;

        const uint8_t out_y = gradientfield_blend255(mean_y, copyY[p0], opacity);
        const uint8_t out_u = gradientfield_blend255(mean_u, copyU[p0], opacity);
        const uint8_t out_v = gradientfield_blend255(mean_v, copyV[p0], opacity);

        Y[p0] = out_y;     Y[p0 + 1] = out_y;
        Y[p1] = out_y;     Y[p1 + 1] = out_y;
        U[p0] = out_u;     U[p0 + 1] = out_u;
        U[p1] = out_u;     U[p1 + 1] = out_u;
        V[p0] = out_v;     V[p0 + 1] = out_v;
        V[p1] = out_v;     V[p1 + 1] = out_v;
    }
}

void gradientfield_apply(void *ptr, VJFrame *frame, int *args)
{
    gradientfield_t *s = (gradientfield_t*)ptr;

    const int w = s->width;
    const int h = s->height;
    const int len = s->len;
    const int a = clampi(args[P_WINDOW], 2, 30);
    const int opacity = clampi(args[P_OPACITY], 0, 255);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    veejay_memcpy(s->copyY, Y, len);
    veejay_memcpy(s->copyU, U, len);
    veejay_memcpy(s->copyV, V, len);

    gradientfield_integral_y(s->copyY, s->intY_sum, s->intY_sq, w, h, s->n_threads);
    gradientfield_integral_sum(s->copyU, s->intU_sum, w, h, s->n_threads);
    gradientfield_integral_sum(s->copyV, s->intV_sum, w, h, s->n_threads);

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h - 1; y += 2)
        gradientfield_apply_rowpair(s, Y, U, V, a, opacity, y);
}
