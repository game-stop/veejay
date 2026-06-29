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
#include "neighbours.h"

#ifdef _OPENMP
#include <omp.h>
#define NB_THREAD_ID() omp_get_thread_num()
#else
#define NB_THREAD_ID() 0
#endif

#define NEIGHBOURS_PARAMS 3

#define P_BRUSH      0
#define P_SMOOTHNESS 1
#define P_MODE       2

#define NB_BINS 256
#define NB_SCRATCH_PLANES 4
#define NB_SCRATCH_STRIDE (NB_BINS * NB_SCRATCH_PLANES)

typedef struct {
    uint8_t *src[4];
    int *scratch;
    int n_threads;
} nb_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t nb_quant_luma(uint8_t y, int smoothness)
{
    return (uint8_t)(((int)y * smoothness + 127) / 255);
}

static inline void nb_clear_luma_scratch(int *hist, int *sum_y, int active_bins)
{
    veejay_memset(hist, 0, sizeof(int) * active_bins);
    veejay_memset(sum_y, 0, sizeof(int) * active_bins);
}

static inline void nb_clear_color_scratch(int *hist, int *sum_y, int *sum_u, int *sum_v, int active_bins)
{
    veejay_memset(hist, 0, sizeof(int) * active_bins);
    veejay_memset(sum_y, 0, sizeof(int) * active_bins);
    veejay_memset(sum_u, 0, sizeof(int) * active_bins);
    veejay_memset(sum_v, 0, sizeof(int) * active_bins);
}

static inline int nb_peak_bin(const int *hist, int active_bins)
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

static inline void nb_add_luma_sample(int *hist,
                                      int *sum_y,
                                      const uint8_t *restrict src_y,
                                      const uint8_t *restrict bins,
                                      int idx)
{
    const int b = bins[idx];

    hist[b]++;
    sum_y[b] += src_y[idx];
}

static inline void nb_remove_luma_sample(int *hist,
                                         int *sum_y,
                                         const uint8_t *restrict src_y,
                                         const uint8_t *restrict bins,
                                         int idx)
{
    const int b = bins[idx];

    hist[b]--;
    sum_y[b] -= src_y[idx];
}

static inline void nb_add_color_sample(int *hist,
                                       int *sum_y,
                                       int *sum_u,
                                       int *sum_v,
                                       const uint8_t *restrict src_y,
                                       const uint8_t *restrict src_u,
                                       const uint8_t *restrict src_v,
                                       const uint8_t *restrict bins,
                                       int idx)
{
    const int b = bins[idx];

    hist[b]++;
    sum_y[b] += src_y[idx];
    sum_u[b] += src_u[idx];
    sum_v[b] += src_v[idx];
}

static inline void nb_remove_color_sample(int *hist,
                                          int *sum_y,
                                          int *sum_u,
                                          int *sum_v,
                                          const uint8_t *restrict src_y,
                                          const uint8_t *restrict src_u,
                                          const uint8_t *restrict src_v,
                                          const uint8_t *restrict bins,
                                          int idx)
{
    const int b = bins[idx];

    hist[b]--;
    sum_y[b] -= src_y[idx];
    sum_u[b] -= src_u[idx];
    sum_v[b] -= src_v[idx];
}

vj_effect *neighbours_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = NEIGHBOURS_PARAMS;
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

    ve->limits[0][P_BRUSH] = 2;      ve->limits[1][P_BRUSH] = 16;      ve->defaults[P_BRUSH] = 4;
    ve->limits[0][P_SMOOTHNESS] = 1; ve->limits[1][P_SMOOTHNESS] = 255; ve->defaults[P_SMOOTHNESS] = 4;
    ve->limits[0][P_MODE] = 0;       ve->limits[1][P_MODE] = 1;         ve->defaults[P_MODE] = 0;

    ve->description = "ZArtistic Filter (Oilpainting, acc. add )";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(ve->num_params, "Brush size", "Smoothness", "Mode");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Luma Only", "Luma and Chroma");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,             3,                  14,                 4,  14, 3000, 8200, 2200, 22,
        VJ_BEAT_DETAIL,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,             4,                  160,                14, 54,  800, 3000, 0,    78,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                         VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );
    return ve;
}

void neighbours_free(void *ptr)
{
    nb_t *n = (nb_t*) ptr;

    free(n->src[0]);
    free(n->scratch);
    free(n);
}

void *neighbours_malloc(int w, int h)
{
    nb_t *n = (nb_t*) vj_calloc(sizeof(nb_t));

    if(!n)
        return NULL;

    const int len = w * h;

    n->n_threads = vje_advise_num_threads(len);
    n->src[0] = (uint8_t*) vj_malloc((size_t)len * 4u);

    if(!n->src[0]) {
        free(n);
        return NULL;
    }

    n->src[1] = n->src[0] + len;
    n->src[2] = n->src[1] + len;
    n->src[3] = n->src[2] + len;
    n->scratch = (int*) vj_calloc(sizeof(int) * NB_SCRATCH_STRIDE * n->n_threads);

    if(!n->scratch) {
        neighbours_free(n);
        return NULL;
    }

    return (void*) n;
}

static void nb_apply_luma(nb_t *n,
                          uint8_t *restrict dst_y,
                          const uint8_t *restrict src_y,
                          const uint8_t *restrict bins,
                          int width,
                          int height,
                          int brush_size,
                          int active_bins)
{
#pragma omp for schedule(static)
    for(int y = 0; y < height; y++) {
        const int tid = NB_THREAD_ID();
        int *scratch = n->scratch + tid * NB_SCRATCH_STRIDE;
        int *hist = scratch;
        int *sum_y = scratch + NB_BINS;
        const int y0 = y > brush_size ? y - brush_size : 0;
        const int y1 = y + brush_size < height ? y + brush_size : height - 1;
        int left = 0;
        int right = -1;

        nb_clear_luma_scratch(hist, sum_y, active_bins);

        for(int x = 0; x < width; x++) {
            const int want_left = x > brush_size ? x - brush_size : 0;
            const int want_right = x + brush_size < width ? x + brush_size : width - 1;

            while(right < want_right) {
                right++;

                for(int yy = y0; yy <= y1; yy++)
                    nb_add_luma_sample(hist, sum_y, src_y, bins, yy * width + right);
            }

            while(left < want_left) {
                for(int yy = y0; yy <= y1; yy++)
                    nb_remove_luma_sample(hist, sum_y, src_y, bins, yy * width + left);

                left++;
            }

            const int peak = nb_peak_bin(hist, active_bins);
            const int count = hist[peak];
            const int idx = y * width + x;

            dst_y[idx] = count > 0 ? (uint8_t)(sum_y[peak] / count) : src_y[idx];
        }
    }
}

static void nb_apply_color(nb_t *n,
                           uint8_t *restrict dst_y,
                           uint8_t *restrict dst_u,
                           uint8_t *restrict dst_v,
                           const uint8_t *restrict src_y,
                           const uint8_t *restrict src_u,
                           const uint8_t *restrict src_v,
                           const uint8_t *restrict bins,
                           int width,
                           int height,
                           int brush_size,
                           int active_bins)
{
#pragma omp for schedule(static)
    for(int y = 0; y < height; y++) {
        const int tid = NB_THREAD_ID();
        int *scratch = n->scratch + tid * NB_SCRATCH_STRIDE;
        int *hist = scratch;
        int *sum_y = scratch + NB_BINS;
        int *sum_u = scratch + NB_BINS * 2;
        int *sum_v = scratch + NB_BINS * 3;
        const int y0 = y > brush_size ? y - brush_size : 0;
        const int y1 = y + brush_size < height ? y + brush_size : height - 1;
        int left = 0;
        int right = -1;

        nb_clear_color_scratch(hist, sum_y, sum_u, sum_v, active_bins);

        for(int x = 0; x < width; x++) {
            const int want_left = x > brush_size ? x - brush_size : 0;
            const int want_right = x + brush_size < width ? x + brush_size : width - 1;

            while(right < want_right) {
                right++;

                for(int yy = y0; yy <= y1; yy++)
                    nb_add_color_sample(hist, sum_y, sum_u, sum_v, src_y, src_u, src_v, bins, yy * width + right);
            }

            while(left < want_left) {
                for(int yy = y0; yy <= y1; yy++)
                    nb_remove_color_sample(hist, sum_y, sum_u, sum_v, src_y, src_u, src_v, bins, yy * width + left);

                left++;
            }

            const int peak = nb_peak_bin(hist, active_bins);
            const int count = hist[peak];
            const int idx = y * width + x;

            if(count > 0) {
                dst_y[idx] = (uint8_t)(sum_y[peak] / count);
                dst_u[idx] = (uint8_t)(sum_u[peak] / count);
                dst_v[idx] = (uint8_t)(sum_v[peak] / count);
            }
            else {
                dst_y[idx] = src_y[idx];
                dst_u[idx] = src_u[idx];
                dst_v[idx] = src_v[idx];
            }
        }
    }
}

void neighbours_apply(void *ptr, VJFrame *frame, int *args)
{
    nb_t *n = (nb_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int brush_size = args[P_BRUSH];
    const int smoothness = args[P_SMOOTHNESS];
    const int mode = args[P_MODE];
    const int active_bins = smoothness + 1;

    uint8_t *restrict dst_y = frame->data[0];
    uint8_t *restrict dst_u = frame->data[1];
    uint8_t *restrict dst_v = frame->data[2];

    uint8_t *restrict src_y = n->src[0];
    uint8_t *restrict src_u = n->src[1];
    uint8_t *restrict src_v = n->src[2];
    uint8_t *restrict bins = n->src[3];

    veejay_memcpy(src_y, dst_y, len);

    if(mode) {
        veejay_memcpy(src_u, dst_u, len);
        veejay_memcpy(src_v, dst_v, len);
    }

#pragma omp parallel num_threads(n->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            bins[i] = nb_quant_luma(src_y[i], smoothness);

        if(mode)
            nb_apply_color(n, dst_y, dst_u, dst_v, src_y, src_u, src_v, bins, width, height, brush_size, active_bins);
        else
            nb_apply_luma(n, dst_y, src_y, bins, width, height, brush_size, active_bins);
    }
}
