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
#include "neighbours.h"

#ifdef _OPENMP
#include <omp.h>
#define NB_THREAD_ID() omp_get_thread_num()
#else
#define NB_THREAD_ID() 0
#endif

#define NB_BINS 256
#define NB_SCRATCH_PLANES 4
#define NB_SCRATCH_STRIDE (NB_BINS * NB_SCRATCH_PLANES)

typedef struct {
    uint8_t *src[4];
    int *scratch;
    int width;
    int height;
    int n_threads;
} nb_t;

vj_effect *neighbours_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 2;
    ve->limits[1][0] = 16;
    ve->defaults[0] = 4;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 255;
    ve->defaults[1] = 4;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[2] = 0;

    ve->description = "ZArtistic Filter (Oilpainting, acc. add )";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Brush size",
        "Smoothness",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][2],
        2,
        "Luma Only",
        "Luma and Chroma"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 2,                  12,                 6, 22, 1800, 4200, 900, 30,    /* Brush size */
        VJ_BEAT_DETAIL,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 8,                  180,                6, 22, 1600, 3400, 700, 30,    /* Smoothness */
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Mode */
    );

    (void) w;
    (void) h;

    return ve;
}

void *neighbours_malloc(int w, int h)
{
    nb_t *n = (nb_t*) vj_calloc(sizeof(nb_t));
    if(!n)
        return NULL;

    const int len = w * h;

    n->width = w;
    n->height = h;
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
    n->src[3] = n->src[2] + len;

    n->scratch = (int*) vj_calloc(sizeof(int) * NB_SCRATCH_STRIDE * n->n_threads);
    if(!n->scratch) {
        free(n->src[0]);
        free(n);
        return NULL;
    }

    return (void*) n;
}

void neighbours_free(void *ptr)
{
    nb_t *n = (nb_t*) ptr;
    if(!n)
        return;

    if(n->src[0])
        free(n->src[0]);

    if(n->scratch)
        free(n->scratch);

    free(n);
}

static inline int nb_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t nb_quant_luma(uint8_t y, int smoothness)
{
    return (uint8_t) (((int)y * smoothness + 127) / 255);
}

static inline void nb_clear_luma_scratch(int *hist, int *sum_y)
{
    veejay_memset(hist, 0, sizeof(int) * NB_BINS);
    veejay_memset(sum_y, 0, sizeof(int) * NB_BINS);
}

static inline void nb_clear_color_scratch(int *hist, int *sum_y, int *sum_u, int *sum_v)
{
    veejay_memset(hist, 0, sizeof(int) * NB_BINS);
    veejay_memset(sum_y, 0, sizeof(int) * NB_BINS);
    veejay_memset(sum_u, 0, sizeof(int) * NB_BINS);
    veejay_memset(sum_v, 0, sizeof(int) * NB_BINS);
}

static inline int nb_peak_bin(const int *hist)
{
    int peak_count = hist[0];
    int peak_bin = 0;

    for(int i = 1; i < NB_BINS; i++) {
        if(hist[i] > peak_count) {
            peak_count = hist[i];
            peak_bin = i;
        }
    }

    return peak_bin;
}

static inline void nb_add_luma_sample(
    int *hist,
    int *sum_y,
    const uint8_t *restrict src_y,
    const uint8_t *restrict bins,
    int idx
) {
    const int b = bins[idx];
    hist[b]++;
    sum_y[b] += src_y[idx];
}

static inline void nb_remove_luma_sample(
    int *hist,
    int *sum_y,
    const uint8_t *restrict src_y,
    const uint8_t *restrict bins,
    int idx
) {
    const int b = bins[idx];
    hist[b]--;
    sum_y[b] -= src_y[idx];
}

static inline void nb_add_color_sample(
    int *hist,
    int *sum_y,
    int *sum_u,
    int *sum_v,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    const uint8_t *restrict bins,
    int idx
) {
    const int b = bins[idx];
    hist[b]++;
    sum_y[b] += src_y[idx];
    sum_u[b] += src_u[idx];
    sum_v[b] += src_v[idx];
}

static inline void nb_remove_color_sample(
    int *hist,
    int *sum_y,
    int *sum_u,
    int *sum_v,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    const uint8_t *restrict bins,
    int idx
) {
    const int b = bins[idx];
    hist[b]--;
    sum_y[b] -= src_y[idx];
    sum_u[b] -= src_u[idx];
    sum_v[b] -= src_v[idx];
}

static void nb_apply_luma(
    nb_t *n,
    uint8_t *restrict dst_y,
    const uint8_t *restrict src_y,
    const uint8_t *restrict bins,
    int width,
    int height,
    int brush_size
) {
#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int y = 0; y < height; y++) {
        const int tid = NB_THREAD_ID();
        int *scratch = n->scratch + tid * NB_SCRATCH_STRIDE;
        int *hist = scratch;
        int *sum_y = scratch + NB_BINS;

        const int row = y * width;
        int left = 0;
        int right = -1;

        nb_clear_luma_scratch(hist, sum_y);

        for(int x = 0; x < width; x++) {
            const int want_left = nb_clampi(x - brush_size, 0, width - 1);
            const int want_right = nb_clampi(x + brush_size, 0, width - 1);

            while(right < want_right) {
                right++;
                nb_add_luma_sample(hist, sum_y, src_y, bins, row + right);
            }

            while(left < want_left) {
                nb_remove_luma_sample(hist, sum_y, src_y, bins, row + left);
                left++;
            }

            const int peak = nb_peak_bin(hist);
            const int count = hist[peak];

            dst_y[row + x] = count > 0
                ? (uint8_t) (sum_y[peak] / count)
                : src_y[row + x];
        }
    }
}

static void nb_apply_color(
    nb_t *n,
    uint8_t *restrict dst_y,
    uint8_t *restrict dst_u,
    uint8_t *restrict dst_v,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    const uint8_t *restrict bins,
    int width,
    int height,
    int brush_size
) {
#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int y = 0; y < height; y++) {
        const int tid = NB_THREAD_ID();
        int *scratch = n->scratch + tid * NB_SCRATCH_STRIDE;
        int *hist = scratch;
        int *sum_y = scratch + NB_BINS;
        int *sum_u = scratch + NB_BINS * 2;
        int *sum_v = scratch + NB_BINS * 3;

        const int row = y * width;
        int left = 0;
        int right = -1;

        nb_clear_color_scratch(hist, sum_y, sum_u, sum_v);

        for(int x = 0; x < width; x++) {
            const int want_left = nb_clampi(x - brush_size, 0, width - 1);
            const int want_right = nb_clampi(x + brush_size, 0, width - 1);

            while(right < want_right) {
                right++;
                nb_add_color_sample(hist, sum_y, sum_u, sum_v, src_y, src_u, src_v, bins, row + right);
            }

            while(left < want_left) {
                nb_remove_color_sample(hist, sum_y, sum_u, sum_v, src_y, src_u, src_v, bins, row + left);
                left++;
            }

            const int peak = nb_peak_bin(hist);
            const int count = hist[peak];
            const int idx = row + x;

            if(count > 0) {
                dst_y[idx] = (uint8_t) (sum_y[peak] / count);
                dst_u[idx] = (uint8_t) (sum_u[peak] / count);
                dst_v[idx] = (uint8_t) (sum_v[peak] / count);
            } else {
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
    if(!n || !frame || !args)
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    int brush_size = args[0];
    int smoothness = args[1];
    int mode = args[2];

    brush_size = nb_clampi(brush_size, 2, 16);
    smoothness = nb_clampi(smoothness, 1, 255);
    mode = nb_clampi(mode, 0, 1);

    uint8_t *restrict dst_y = frame->data[0];
    uint8_t *restrict dst_u = frame->data[1];
    uint8_t *restrict dst_v = frame->data[2];

    uint8_t *restrict src_y = n->src[0];
    uint8_t *restrict src_u = n->src[1];
    uint8_t *restrict src_v = n->src[2];
    uint8_t *restrict bins  = n->src[3];

    veejay_memcpy(src_y, dst_y, len);

    if(mode) {
        veejay_memcpy(src_u, dst_u, len);
        veejay_memcpy(src_v, dst_v, len);
    }

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int i = 0; i < len; i++) {
        bins[i] = nb_quant_luma(src_y[i], smoothness);
    }

    if(!mode) {
        nb_apply_luma(n, dst_y, src_y, bins, width, height, brush_size);
    } else {
        nb_apply_color(n, dst_y, dst_u, dst_v, src_y, src_u, src_v, bins, width, height, brush_size);
    }
}
