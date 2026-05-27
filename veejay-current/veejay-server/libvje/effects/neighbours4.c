/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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
#include "neighbours4.h"
#include <math.h>

#ifdef _OPENMP
#include <omp.h>
#define NB4_THREAD_ID() omp_get_thread_num()
#else
#define NB4_THREAD_ID() 0
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NB4_BINS 256
#define NB4_MAX_POINTS 2048
#define NB4_SCRATCH_PLANES 4
#define NB4_SCRATCH_STRIDE (NB4_BINS * NB4_SCRATCH_PLANES)

typedef struct {
    double x;
    double y;
} relpoint_t;

typedef struct {
    uint8_t y;
    uint8_t u;
    uint8_t v;
} pixel_t;

typedef struct {
    uint8_t *src[3];
    uint8_t *bin;
    int *scratch;
    relpoint_t points[NB4_MAX_POINTS];
    int width;
    int height;
    int last_radius;
    int last_depth;
    int n_threads;
} nb4_t;

vj_effect *neighbours4_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 2;
    ve->limits[1][0] = 32;
    ve->defaults[0] = 4;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 200;
    ve->defaults[1] = 4;

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 255;
    ve->defaults[2] = 8;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->defaults[3] = 1;

    ve->description = "ZArtistic Filter (Round Brush)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Radius",
        "Distance from center",
        "Smoothness",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][3],
        3,
        "Luma Only",
        "Luma and Chroma"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 2,                  24,                 6, 22, 1800, 4200, 900, 30,    /* Radius */
        VJ_BEAT_DENSITY,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 8,                  160,                6, 22, 1800, 4200, 900, 30,    /* Distance from center */
        VJ_BEAT_DETAIL,        VJ_BEAT_F_PHRASE_ONLY,                        8,                  180,                6, 22, 1600, 3400, 700, 30,    /* Smoothness */
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Mode */
    );

    (void) w;
    (void) h;

    return ve;
}

void *neighbours4_malloc(int w, int h)
{
    nb4_t *n = (nb4_t*) vj_calloc(sizeof(nb4_t));
    if(!n)
        return NULL;

    const int len = w * h;

    n->width = w;
    n->height = h;
    n->last_radius = -1;
    n->last_depth = -1;

    n->n_threads = vje_advise_num_threads(len);
    if(n->n_threads < 1)
        n->n_threads = 1;

    n->src[0] = (uint8_t*) vj_malloc((size_t) len * 4u);
    if(!n->src[0]) {
        free(n);
        return NULL;
    }

    n->src[1] = n->src[0] + len;
    n->src[2] = n->src[1] + len;
    n->bin    = n->src[2] + len;

    n->scratch = (int*) vj_calloc(sizeof(int) * NB4_SCRATCH_STRIDE * n->n_threads);
    if(!n->scratch) {
        free(n->src[0]);
        free(n);
        return NULL;
    }

    return (void*) n;
}

void neighbours4_free(void *ptr)
{
    nb4_t *n = (nb4_t*) ptr;
    if(!n)
        return;

    if(n->src[0])
        free(n->src[0]);

    if(n->scratch)
        free(n->scratch);

    free(n);
}

static inline int nb4_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t nb4_quant_luma(uint8_t y, int smoothness)
{
    return (uint8_t)(((int)y * smoothness + 127) / 255);
}

static void nb4_create_circle(nb4_t *n, int radius, int depth)
{
    if(radius == n->last_radius && depth == n->last_depth)
        return;

    for(int i = 0; i < depth; i++) {
        const double angle = 2.0 * M_PI * (double)i / (double)depth;
        n->points[i].x = a_cos(angle) * (double)radius;
        n->points[i].y = a_sin(angle) * (double)radius;
    }

    n->last_radius = radius;
    n->last_depth = depth;
}

static inline void nb4_clear_luma_scratch(int *hist, int *sum_y)
{
    veejay_memset(hist, 0, sizeof(int) * NB4_BINS);
    veejay_memset(sum_y, 0, sizeof(int) * NB4_BINS);
}

static inline void nb4_clear_color_scratch(int *hist, int *sum_y, int *sum_u, int *sum_v)
{
    veejay_memset(hist, 0, sizeof(int) * NB4_BINS);
    veejay_memset(sum_y, 0, sizeof(int) * NB4_BINS);
    veejay_memset(sum_u, 0, sizeof(int) * NB4_BINS);
    veejay_memset(sum_v, 0, sizeof(int) * NB4_BINS);
}

static inline int nb4_peak_bin(const int *hist)
{
    int peak_count = hist[0];
    int peak_bin = 0;

    for(int i = 1; i < NB4_BINS; i++) {
        if(hist[i] > peak_count) {
            peak_count = hist[i];
            peak_bin = i;
        }
    }

    return peak_bin;
}

static inline uint8_t nb4_eval_luma(
    int x,
    int y,
    int depth,
    const relpoint_t *restrict pts,
    const uint8_t *restrict src_y,
    const uint8_t *restrict bin,
    int *restrict hist,
    int *restrict sum_y,
    int width,
    int height
) {
    nb4_clear_luma_scratch(hist, sum_y);

    for(int i = 0; i < depth; i++) {
        int dx = nb4_clampi((int)(pts[i].x + (double)x), 0, width - 1);
        int dy = nb4_clampi((int)(pts[i].y + (double)y), 0, height - 1);
        int idx = dy * width + dx;
        int b = bin[idx];

        hist[b]++;
        sum_y[b] += src_y[idx];
    }

    const int peak = nb4_peak_bin(hist);
    const int count = hist[peak];

    return count > 15
        ? (uint8_t)(sum_y[peak] / count)
        : src_y[y * width + x];
}

static inline pixel_t nb4_eval_color(
    int x,
    int y,
    int depth,
    const relpoint_t *restrict pts,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    const uint8_t *restrict bin,
    int *restrict hist,
    int *restrict sum_y,
    int *restrict sum_u,
    int *restrict sum_v,
    int width,
    int height
) {
    pixel_t out;
    nb4_clear_color_scratch(hist, sum_y, sum_u, sum_v);

    for(int i = 0; i < depth; i++) {
        int dx = nb4_clampi((int)(pts[i].x + (double)x), 0, width - 1);
        int dy = nb4_clampi((int)(pts[i].y + (double)y), 0, height - 1);
        int idx = dy * width + dx;
        int b = bin[idx];

        hist[b]++;
        sum_y[b] += src_y[idx];
        sum_u[b] += src_u[idx];
        sum_v[b] += src_v[idx];
    }

    const int peak = nb4_peak_bin(hist);
    const int count = hist[peak];
    const int idx = y * width + x;

    if(count > 0) {
        out.y = (uint8_t)(sum_y[peak] / count);
        out.u = (uint8_t)(sum_u[peak] / count);
        out.v = (uint8_t)(sum_v[peak] / count);
    } else {
        out.y = src_y[idx];
        out.u = src_u[idx];
        out.v = src_v[idx];
    }

    return out;
}

static void nb4_apply_luma(
    nb4_t *n,
    uint8_t *restrict dst_y,
    const uint8_t *restrict src_y,
    const uint8_t *restrict bin,
    int width,
    int height,
    int depth
) {
    const relpoint_t *restrict pts = n->points;

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int y = 0; y < height; y++) {
        const int tid = NB4_THREAD_ID();
        int *scratch = n->scratch + tid * NB4_SCRATCH_STRIDE;
        int *hist = scratch;
        int *sum_y = scratch + NB4_BINS;

        for(int x = 0; x < width; x++) {
            dst_y[y * width + x] = nb4_eval_luma(
                x, y, depth, pts,
                src_y, bin,
                hist, sum_y,
                width, height
            );
        }
    }
}

static void nb4_apply_color(
    nb4_t *n,
    uint8_t *restrict dst_y,
    uint8_t *restrict dst_u,
    uint8_t *restrict dst_v,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    const uint8_t *restrict bin,
    int width,
    int height,
    int depth
) {
    const relpoint_t *restrict pts = n->points;

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int y = 0; y < height; y++) {
        const int tid = NB4_THREAD_ID();
        int *scratch = n->scratch + tid * NB4_SCRATCH_STRIDE;
        int *hist = scratch;
        int *sum_y = scratch + NB4_BINS;
        int *sum_u = scratch + NB4_BINS * 2;
        int *sum_v = scratch + NB4_BINS * 3;

        for(int x = 0; x < width; x++) {
            const int idx = y * width + x;
            pixel_t tmp = nb4_eval_color(
                x, y, depth, pts,
                src_y, src_u, src_v, bin,
                hist, sum_y, sum_u, sum_v,
                width, height
            );

            dst_y[idx] = tmp.y;
            dst_u[idx] = tmp.u;
            dst_v[idx] = tmp.v;
        }
    }
}

void neighbours4_apply(void *ptr, VJFrame *frame, int *args)
{
    nb4_t *n = (nb4_t*) ptr;
    if(!n || !frame || !args)
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    int radius = nb4_clampi(args[0], 2, 32);
    int depth = nb4_clampi(args[1], 1, 200);
    int smoothness = nb4_clampi(args[2], 1, 255);
    int mode = nb4_clampi(args[3], 0, 1);

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

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int i = 0; i < len; i++) {
        bin[i] = nb4_quant_luma(src_y[i], smoothness);
    }

    if(!mode) {
        const int uv_len = frame->ssm ? len : frame->uv_len;

        nb4_apply_luma(n, dst_y, src_y, bin, width, height, depth);
        veejay_memset(dst_u, 128, uv_len);
        veejay_memset(dst_v, 128, uv_len);
    } else {
        nb4_apply_color(n, dst_y, dst_u, dst_v, src_y, src_u, src_v, bin, width, height, depth);
    }
}