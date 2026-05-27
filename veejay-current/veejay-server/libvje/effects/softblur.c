/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "softblur.h"

extern int vje_get_quality(void);

typedef struct {
    uint8_t *src;
    uint8_t *tmp;
    int max_len;
    int n_threads;
} softblur_t;

static inline int softblur_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *softblur_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 0;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;

    ve->description = "Soft Blur";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Kernel Size"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][0],
        0,
        "1x3",
        "3x3",
        "5x5"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 0, 2, 6, 22, 1800, 4200, 900, 30 /* Kernel Size */
    );

    (void) w;
    (void) h;

    return ve;
}

void *softblur_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    softblur_t *sb = (softblur_t*) vj_calloc(sizeof(softblur_t));
    if(!sb)
        return NULL;

    const int len = w * h;

    sb->src = (uint8_t*) vj_malloc((size_t)len * 2u);
    if(!sb->src) {
        free(sb);
        return NULL;
    }

    sb->tmp = sb->src + len;
    sb->max_len = len;

    sb->n_threads = vje_advise_num_threads(len);
    if(sb->n_threads < 1)
        sb->n_threads = 1;

    return (void*) sb;
}

void softblur_free(void *ptr)
{
    softblur_t *sb = (softblur_t*) ptr;
    if(!sb)
        return;

    if(sb->src)
        free(sb->src);

    free(sb);
}

static void softblur1_core(const uint8_t *restrict src,
                           uint8_t *restrict dst,
                           int w,
                           int h,
                           int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < h; y++) {
        const uint8_t *restrict row = src + y * w;
        uint8_t *restrict out = dst + y * w;

        if(w <= 1) {
            out[0] = row[0];
            continue;
        }

        out[0] = (uint8_t)(((int)row[0] * 2 + (int)row[1] + 1) / 3);

#pragma omp simd
        for(int x = 1; x < w - 1; x++)
            out[x] = (uint8_t)(((int)row[x - 1] + (int)row[x] + (int)row[x + 1] + 1) / 3);

        out[w - 1] = (uint8_t)(((int)row[w - 2] + (int)row[w - 1] * 2 + 1) / 3);
    }
}

static void softblur3_core(const uint8_t *src,
                           uint8_t *tmp,
                           uint8_t *dst,
                           int w,
                           int h,
                           int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < h; y++) {
        const uint8_t *restrict row = src + y * w;
        uint8_t *restrict trow = tmp + y * w;

        if(w <= 1) {
            trow[0] = row[0];
            continue;
        }

        trow[0] = (uint8_t)(((int)row[0] * 2 + (int)row[1] + 1) / 3);

#pragma omp simd
        for(int x = 1; x < w - 1; x++)
            trow[x] = (uint8_t)(((int)row[x - 1] + (int)row[x] + (int)row[x + 1] + 1) / 3);

        trow[w - 1] = (uint8_t)(((int)row[w - 2] + (int)row[w - 1] * 2 + 1) / 3);
    }

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < h; y++) {
        const int ym = (y > 0) ? y - 1 : y;
        const int yp = (y < h - 1) ? y + 1 : y;

        const uint8_t *restrict r0 = tmp + ym * w;
        const uint8_t *restrict r1 = tmp + y * w;
        const uint8_t *restrict r2 = tmp + yp * w;
        uint8_t *restrict out = dst + y * w;

#pragma omp simd
        for(int x = 0; x < w; x++)
            out[x] = (uint8_t)(((int)r0[x] + (int)r1[x] + (int)r2[x] + 1) / 3);
    }
}

static void softblur_plane(softblur_t *sb,
                           uint8_t *plane,
                           int w,
                           int h,
                           int type)
{
    if(!sb || !plane || w <= 0 || h <= 0)
        return;

    const int len = w * h;

    if(len <= 0 || len > sb->max_len)
        return;

    veejay_memcpy(sb->src, plane, len);

    switch(type) {
        case 0:
            softblur1_core(sb->src, plane, w, h, sb->n_threads);
            break;

        case 1:
            softblur3_core(sb->src, sb->tmp, plane, w, h, sb->n_threads);
            break;

        case 2:
            softblur3_core(sb->src, sb->tmp, plane, w, h, sb->n_threads);
            softblur3_core(plane, sb->tmp, plane, w, h, sb->n_threads);
            break;

        default:
            break;
    }
}

void softblur_apply_internal(VJFrame *frame)
{
    if(!frame || !frame->data[0] || frame->width <= 0 || frame->height <= 0 || frame->len <= 0)
        return;

    const int type = softblur_clampi(vje_get_quality(), 0, 2);
    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *src = (uint8_t*) vj_malloc((size_t)len * 2u);
    if(!src)
        return;

    uint8_t *tmp = src + len;

    veejay_memcpy(src, frame->data[0], len);

    switch(type) {
        case 0:
            softblur1_core(src, frame->data[0], frame->width, frame->height, n_threads > 0 ? n_threads : 1);
            break;
        case 1:
            softblur3_core(src, tmp, frame->data[0], frame->width, frame->height, n_threads > 0 ? n_threads : 1);
            break;
        case 2:
            softblur3_core(src, tmp, frame->data[0], frame->width, frame->height, n_threads > 0 ? n_threads : 1);
            softblur3_core(frame->data[0], tmp, frame->data[0], frame->width, frame->height, n_threads > 0 ? n_threads : 1);
            break;
        default:
            break;
    }

    free(src);
}

void softblur_apply(void *ptr, VJFrame *frame, int *args)
{
    softblur_t *blur = (softblur_t*) ptr;

    if(!blur || !frame || !args || !frame->data[0])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    const int type = softblur_clampi(args[0], 0, 2);

    softblur_plane(blur, frame->data[0], width, height, type);

    if(frame->data[1] && frame->data[2]) {
        const int uv_width  = frame->ssm ? width  : frame->uv_width;
        const int uv_height = frame->ssm ? height : frame->uv_height;
        const int uv_len    = frame->ssm ? len    : frame->uv_len;

        if(uv_width > 0 && uv_height > 0 && uv_len > 0 &&
           uv_width * uv_height <= blur->max_len)
        {
            softblur_plane(blur, frame->data[1], uv_width, uv_height, type);
            softblur_plane(blur, frame->data[2], uv_width, uv_height, type);
        }
    }
}