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
#include "histomatch.h"

#include <stdint.h>

#define HIST_SIZE 256
#define HIST_PLANES 6

#define H_Y1 0
#define H_Y2 1
#define H_U1 2
#define H_U2 3
#define H_V1 4
#define H_V2 5

typedef struct {
    int *hist;
    uint32_t *cdf;
    uint8_t *lut;
    int n_threads;
} histomatch_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t histomatch_blend255(int a, int b, int opacity)
{
    const int inv = 255 - opacity;
    const int x = a * inv + b * opacity;
    return (uint8_t)(((x + 1) + (x >> 8)) >> 8);
}

vj_effect *histomatch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 1;
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

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150;

    ve->description = "Histogram Matching";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 255, 88, 100, 10, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *histomatch_malloc(int w, int h)
{
    histomatch_t *s = (histomatch_t*) vj_calloc(sizeof(histomatch_t));

    if(!s)
        return NULL;

    s->hist = (int*) vj_malloc(sizeof(int) * HIST_SIZE * HIST_PLANES);
    s->cdf = (uint32_t*) vj_malloc(sizeof(uint32_t) * HIST_SIZE * HIST_PLANES);
    s->lut = (uint8_t*) vj_malloc(HIST_SIZE * 3);

    if(!s->hist || !s->cdf || !s->lut) {
        histomatch_free(s);
        return NULL;
    }

    s->n_threads = vje_advise_num_threads(w * h);

    return (void*) s;
}

void histomatch_free(void *ptr)
{
    histomatch_t *s = (histomatch_t*) ptr;

    if(s) {
        if(s->hist)
            free(s->hist);
        if(s->cdf)
            free(s->cdf);
        if(s->lut)
            free(s->lut);
        free(s);
    }
}

static void histomatch_calc_histogram(const uint8_t *restrict data, int len, int *restrict hist)
{
    int bank0[HIST_SIZE] = {0};
    int bank1[HIST_SIZE] = {0};
    int bank2[HIST_SIZE] = {0};
    int bank3[HIST_SIZE] = {0};
    int i = 0;

    for(; i <= len - 4; i += 4) {
        bank0[data[i + 0]]++;
        bank1[data[i + 1]]++;
        bank2[data[i + 2]]++;
        bank3[data[i + 3]]++;
    }

    for(; i < len; i++)
        bank0[data[i]]++;

#pragma GCC ivdep
    for(int j = 0; j < HIST_SIZE; j++)
        hist[j] = bank0[j] + bank1[j] + bank2[j] + bank3[j];
}

static void histomatch_calc_cdf(const int *restrict hist, uint32_t *restrict cdf)
{
    uint32_t total = 0;
    uint32_t acc = 0;

#pragma GCC ivdep
    for(int i = 0; i < HIST_SIZE; i++)
        total += (uint32_t)hist[i];

    for(int i = 0; i < HIST_SIZE; i++) {
        acc += (uint32_t)hist[i];
        cdf[i] = (uint32_t)(((uint64_t)acc * 65535u + (total >> 1)) / total);
    }

    cdf[255] = 65535u;
}

static void histomatch_map_and_blend(const uint32_t *restrict c1,
                                     const uint32_t *restrict c2,
                                     uint8_t *restrict lut,
                                     int opacity)
{
    int j = 0;

    for(int i = 0; i < HIST_SIZE; i++) {
        const uint32_t target = c1[i];

        while(j < 255 && c2[j + 1] < target)
            j++;

        const int next_j = j < 255 ? j + 1 : j;
        const uint32_t dist_curr = target >= c2[j] ? target - c2[j] : c2[j] - target;
        const uint32_t dist_next = target >= c2[next_j] ? target - c2[next_j] : c2[next_j] - target;
        const int mapped = dist_next < dist_curr ? next_j : j;

        lut[i] = histomatch_blend255(i, mapped, opacity);
    }
}

void histomatch_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    histomatch_t *s = (histomatch_t*) ptr;
    const int opacity = clampi(args[0], 0, 255);

    if(opacity == 0)
        return;

    const int len = frame->len;
    const int uv_len = frame->uv_len;

    int *restrict hist = s->hist;
    uint32_t *restrict cdf = s->cdf;
    uint8_t *restrict lut = s->lut;

    int *restrict hist_y1 = hist + H_Y1 * HIST_SIZE;
    int *restrict hist_y2 = hist + H_Y2 * HIST_SIZE;
    int *restrict hist_u1 = hist + H_U1 * HIST_SIZE;
    int *restrict hist_u2 = hist + H_U2 * HIST_SIZE;
    int *restrict hist_v1 = hist + H_V1 * HIST_SIZE;
    int *restrict hist_v2 = hist + H_V2 * HIST_SIZE;

    uint32_t *restrict cdf_y1 = cdf + H_Y1 * HIST_SIZE;
    uint32_t *restrict cdf_y2 = cdf + H_Y2 * HIST_SIZE;
    uint32_t *restrict cdf_u1 = cdf + H_U1 * HIST_SIZE;
    uint32_t *restrict cdf_u2 = cdf + H_U2 * HIST_SIZE;
    uint32_t *restrict cdf_v1 = cdf + H_V1 * HIST_SIZE;
    uint32_t *restrict cdf_v2 = cdf + H_V2 * HIST_SIZE;

    uint8_t *restrict lut_y = lut;
    uint8_t *restrict lut_u = lut_y + HIST_SIZE;
    uint8_t *restrict lut_v = lut_u + HIST_SIZE;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict U2 = frame2->data[1];
    const uint8_t *restrict V2 = frame2->data[2];

    histomatch_calc_histogram(Y, len, hist_y1);
    histomatch_calc_histogram(Y2, len, hist_y2);
    histomatch_calc_histogram(U, uv_len, hist_u1);
    histomatch_calc_histogram(U2, uv_len, hist_u2);
    histomatch_calc_histogram(V, uv_len, hist_v1);
    histomatch_calc_histogram(V2, uv_len, hist_v2);

    histomatch_calc_cdf(hist_y1, cdf_y1);
    histomatch_calc_cdf(hist_y2, cdf_y2);
    histomatch_calc_cdf(hist_u1, cdf_u1);
    histomatch_calc_cdf(hist_u2, cdf_u2);
    histomatch_calc_cdf(hist_v1, cdf_v1);
    histomatch_calc_cdf(hist_v2, cdf_v2);

    histomatch_map_and_blend(cdf_y1, cdf_y2, lut_y, opacity);
    histomatch_map_and_blend(cdf_u1, cdf_u2, lut_u, opacity);
    histomatch_map_and_blend(cdf_v1, cdf_v2, lut_v, opacity);

#pragma omp parallel num_threads(s->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y[i] = lut_y[Y[i]];

#pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            U[i] = lut_u[U[i]];
            V[i] = lut_v[V[i]];
        }
    }
}
