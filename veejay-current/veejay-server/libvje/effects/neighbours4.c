/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vjmem.h>
#include "neighbours4.h"
#include <stdint.h>

#ifdef _OPENMP
#include <omp.h>
#define NB4_THREAD_ID() omp_get_thread_num()
#else
#define NB4_THREAD_ID() 0
#endif

#define NEIGHBOURS4_PARAMS 4

#define P_RADIUS     0
#define P_DEPTH      1
#define P_SMOOTHNESS 2
#define P_MODE       3

#define NB4_BINS 256
#define NB4_MAX_POINTS 2048
#define NB4_SCRATCH_PLANES 4
#define NB4_SCRATCH_STRIDE (NB4_BINS * NB4_SCRATCH_PLANES)

typedef struct {
    int16_t x;
    int16_t y;
} nb4_point_t;

typedef struct {
    uint8_t y;
    uint8_t u;
    uint8_t v;
} nb4_pixel_t;

typedef struct {
    uint8_t *src[3];
    uint8_t *bin;
    int *scratch;
    nb4_point_t points[NB4_MAX_POINTS];
    int last_radius;
    int last_depth;
    int n_threads;
} nb4_t;

static inline int nb4_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t nb4_quant_luma(uint8_t y, int smoothness)
{
    return (uint8_t)(((int)y * smoothness + 127) / 255);
}

static inline void nb4_clear_luma_scratch(int *hist, int *sum_y, int active_bins)
{
    veejay_memset(hist, 0, sizeof(int) * active_bins);
    veejay_memset(sum_y, 0, sizeof(int) * active_bins);
}

static inline void nb4_clear_color_scratch(int *hist, int *sum_y, int *sum_u, int *sum_v, int active_bins)
{
    veejay_memset(hist, 0, sizeof(int) * active_bins);
    veejay_memset(sum_y, 0, sizeof(int) * active_bins);
    veejay_memset(sum_u, 0, sizeof(int) * active_bins);
    veejay_memset(sum_v, 0, sizeof(int) * active_bins);
}

static inline int nb4_peak_bin(const int *hist, int active_bins)
{
    int peak_count = hist[0];
    int peak_bin = 0;

    for(int i = 1; i < active_bins; i++) {
        if(hist[i] > peak_count) {
            peak_count = hist[i];
            peak_bin = i;
        }
    }

    return peak_bin;
}

static inline void nb4_emit_point(nb4_point_t *points, int *count, int x, int y)
{
    if(*count < NB4_MAX_POINTS) {
        points[*count].x = (int16_t)x;
        points[*count].y = (int16_t)y;
        (*count)++;
    }
}

static inline void nb4_emit_octants(nb4_point_t *points, int *count, int x, int y)
{
    nb4_emit_point(points, count,  x,  y);
    nb4_emit_point(points, count,  y,  x);
    nb4_emit_point(points, count, -y,  x);
    nb4_emit_point(points, count, -x,  y);
    nb4_emit_point(points, count, -x, -y);
    nb4_emit_point(points, count, -y, -x);
    nb4_emit_point(points, count,  y, -x);
    nb4_emit_point(points, count,  x, -y);
}

static void nb4_create_circle(nb4_t *n, int radius, int depth)
{
    if(radius == n->last_radius && depth == n->last_depth)
        return;

    nb4_point_t candidates[NB4_MAX_POINTS];
    int count = 0;
    int x = radius;
    int y = 0;
    int err = 0;

    while(x >= y) {
        nb4_emit_octants(candidates, &count, x, y);

        y++;
        err += 1 + (y << 1);

        if(((err - x) << 1) + 1 > 0) {
            x--;
            err += 1 - (x << 1);
        }
    }

    for(int i = 0; i < depth; i++) {
        const int k = ((i * count) + (depth >> 1)) / depth;

        n->points[i] = candidates[k < count ? k : count - 1];
    }

    n->last_radius = radius;
    n->last_depth = depth;
}

vj_effect *neighbours4_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = NEIGHBOURS4_PARAMS;
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

    ve->limits[0][P_RADIUS] = 2;     ve->limits[1][P_RADIUS] = 32;      ve->defaults[P_RADIUS] = 4;
    ve->limits[0][P_DEPTH] = 1;      ve->limits[1][P_DEPTH] = 200;      ve->defaults[P_DEPTH] = 4;
    ve->limits[0][P_SMOOTHNESS] = 1; ve->limits[1][P_SMOOTHNESS] = 255; ve->defaults[P_SMOOTHNESS] = 8;
    ve->limits[0][P_MODE] = 0;       ve->limits[1][P_MODE] = 1;         ve->defaults[P_MODE] = 1;

    ve->description = "ZArtistic Filter (Round Brush)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Radius", "Distance from center", "Smoothness", "Mode");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Luma Only", "Luma and Chroma");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, 28, 78, 100, 15, 560, 0, 1, 260, VJ_BEAT_COST_MODERATE, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DENSITY, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 4, 170, 84, 100, 0, 620, 0, 1, 260, VJ_BEAT_COST_MODERATE, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 4, 180, 80, 100, 10, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void neighbours4_free(void *ptr)
{
    nb4_t *n = (nb4_t*) ptr;

    free(n->src[0]);
    free(n->scratch);
    free(n);
}

void *neighbours4_malloc(int w, int h)
{
    nb4_t *n = (nb4_t*) vj_calloc(sizeof(nb4_t));

    if(!n)
        return NULL;

    const int len = w * h;

    n->n_threads = vje_advise_num_threads(len);
    n->last_radius = -1;
    n->last_depth = -1;
    n->src[0] = (uint8_t*) vj_malloc((size_t)len * 4u);

    if(!n->src[0]) {
        free(n);
        return NULL;
    }

    n->src[1] = n->src[0] + len;
    n->src[2] = n->src[1] + len;
    n->bin = n->src[2] + len;
    n->scratch = (int*) vj_calloc(sizeof(int) * NB4_SCRATCH_STRIDE * n->n_threads);

    if(!n->scratch) {
        neighbours4_free(n);
        return NULL;
    }

    return (void*) n;
}

static inline uint8_t nb4_eval_luma(int x,
                                    int y,
                                    int depth,
                                    const nb4_point_t *restrict points,
                                    const uint8_t *restrict src_y,
                                    const uint8_t *restrict bin,
                                    int *restrict hist,
                                    int *restrict sum_y,
                                    int width,
                                    int height,
                                    int active_bins)
{
    nb4_clear_luma_scratch(hist, sum_y, active_bins);

    for(int i = 0; i < depth; i++) {
        const int sx = nb4_clampi(x + points[i].x, 0, width - 1);
        const int sy = nb4_clampi(y + points[i].y, 0, height - 1);
        const int idx = sy * width + sx;
        const int b = bin[idx];

        hist[b]++;
        sum_y[b] += src_y[idx];
    }

    const int peak = nb4_peak_bin(hist, active_bins);
    const int count = hist[peak];
    const int idx = y * width + x;

    return count > 15 ? (uint8_t)(sum_y[peak] / count) : src_y[idx];
}

static inline nb4_pixel_t nb4_eval_color(int x,
                                         int y,
                                         int depth,
                                         const nb4_point_t *restrict points,
                                         const uint8_t *restrict src_y,
                                         const uint8_t *restrict src_u,
                                         const uint8_t *restrict src_v,
                                         const uint8_t *restrict bin,
                                         int *restrict hist,
                                         int *restrict sum_y,
                                         int *restrict sum_u,
                                         int *restrict sum_v,
                                         int width,
                                         int height,
                                         int active_bins)
{
    nb4_pixel_t out;

    nb4_clear_color_scratch(hist, sum_y, sum_u, sum_v, active_bins);

    for(int i = 0; i < depth; i++) {
        const int sx = nb4_clampi(x + points[i].x, 0, width - 1);
        const int sy = nb4_clampi(y + points[i].y, 0, height - 1);
        const int idx = sy * width + sx;
        const int b = bin[idx];

        hist[b]++;
        sum_y[b] += src_y[idx];
        sum_u[b] += src_u[idx];
        sum_v[b] += src_v[idx];
    }

    const int peak = nb4_peak_bin(hist, active_bins);
    const int count = hist[peak];
    const int idx = y * width + x;

    if(count > 0) {
        out.y = (uint8_t)(sum_y[peak] / count);
        out.u = (uint8_t)(sum_u[peak] / count);
        out.v = (uint8_t)(sum_v[peak] / count);
    }
    else {
        out.y = src_y[idx];
        out.u = src_u[idx];
        out.v = src_v[idx];
    }

    return out;
}

static void nb4_apply_luma(nb4_t *n,
                           uint8_t *restrict dst_y,
                           const uint8_t *restrict src_y,
                           const uint8_t *restrict bin,
                           int width,
                           int height,
                           int depth,
                           int active_bins)
{
    const nb4_point_t *restrict points = n->points;

#pragma omp for schedule(static)
    for(int y = 0; y < height; y++) {
        const int tid = NB4_THREAD_ID();
        int *scratch = n->scratch + tid * NB4_SCRATCH_STRIDE;
        int *hist = scratch;
        int *sum_y = scratch + NB4_BINS;

        for(int x = 0; x < width; x++)
            dst_y[y * width + x] = nb4_eval_luma(x, y, depth, points, src_y, bin, hist, sum_y, width, height, active_bins);
    }
}

static void nb4_apply_color(nb4_t *n,
                            uint8_t *restrict dst_y,
                            uint8_t *restrict dst_u,
                            uint8_t *restrict dst_v,
                            const uint8_t *restrict src_y,
                            const uint8_t *restrict src_u,
                            const uint8_t *restrict src_v,
                            const uint8_t *restrict bin,
                            int width,
                            int height,
                            int depth,
                            int active_bins)
{
    const nb4_point_t *restrict points = n->points;

#pragma omp for schedule(static)
    for(int y = 0; y < height; y++) {
        const int tid = NB4_THREAD_ID();
        int *scratch = n->scratch + tid * NB4_SCRATCH_STRIDE;
        int *hist = scratch;
        int *sum_y = scratch + NB4_BINS;
        int *sum_u = scratch + NB4_BINS * 2;
        int *sum_v = scratch + NB4_BINS * 3;

        for(int x = 0; x < width; x++) {
            const int idx = y * width + x;
            nb4_pixel_t p = nb4_eval_color(
                x,
                y,
                depth,
                points,
                src_y,
                src_u,
                src_v,
                bin,
                hist,
                sum_y,
                sum_u,
                sum_v,
                width,
                height,
                active_bins
            );

            dst_y[idx] = p.y;
            dst_u[idx] = p.u;
            dst_v[idx] = p.v;
        }
    }
}

void neighbours4_apply(void *ptr, VJFrame *frame, int *args)
{
    nb4_t *n = (nb4_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int radius = args[P_RADIUS];
    const int depth = args[P_DEPTH];
    const int smoothness = args[P_SMOOTHNESS];
    const int mode = args[P_MODE];
    const int active_bins = smoothness + 1;

    uint8_t *restrict dst_y = frame->data[0];
    uint8_t *restrict dst_u = frame->data[1];
    uint8_t *restrict dst_v = frame->data[2];

    uint8_t *restrict src_y = n->src[0];
    uint8_t *restrict src_u = n->src[1];
    uint8_t *restrict src_v = n->src[2];
    uint8_t *restrict bin = n->bin;

    nb4_create_circle(n, radius, depth);

    veejay_memcpy(src_y, dst_y, len);

    if(mode) {
        veejay_memcpy(src_u, dst_u, len);
        veejay_memcpy(src_v, dst_v, len);
    }

#pragma omp parallel num_threads(n->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            bin[i] = nb4_quant_luma(src_y[i], smoothness);

        if(mode) {
            nb4_apply_color(n, dst_y, dst_u, dst_v, src_y, src_u, src_v, bin, width, height, depth, active_bins);
        }
        else {
            nb4_apply_luma(n, dst_y, src_y, bin, width, height, depth, active_bins);

#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                dst_u[i] = 128;
                dst_v[i] = 128;
            }
        }
    }
}
