/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include "pointilism.h"

#define POINTILISM_PARAMS 5

#define P_MIN_RADIUS 0
#define P_MAX_RADIUS 1
#define P_KERNEL     2
#define P_LOOP       3
#define P_KEEP_ORIG  4

typedef struct {
    uint8_t *buf[3];
    uint32_t *rand_lut;
    int n_threads;
} pointilism_t;

static inline uint32_t pointilism_xorshift32(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    *state = x;

    return x;
}

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *pointilism_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = POINTILISM_PARAMS;
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    ve->limits[0][P_MIN_RADIUS] = 1; ve->limits[1][P_MIN_RADIUS] = 16; ve->defaults[P_MIN_RADIUS] = 3;
    ve->limits[0][P_MAX_RADIUS] = 1; ve->limits[1][P_MAX_RADIUS] = 16; ve->defaults[P_MAX_RADIUS] = 7;
    ve->limits[0][P_KERNEL] = 1;     ve->limits[1][P_KERNEL] = 16;     ve->defaults[P_KERNEL] = 2;
    ve->limits[0][P_LOOP] = 0;       ve->limits[1][P_LOOP] = 1;        ve->defaults[P_LOOP] = 0;
    ve->limits[0][P_KEEP_ORIG] = 0;  ve->limits[1][P_KEEP_ORIG] = 1;   ve->defaults[P_KEEP_ORIG] = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Min",
        "Max",
        "Kernel",
        "Loop",
        "Keep original"
    );

    ve->description = "Pointilism";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_LOOP], P_LOOP, "Off", "On");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_KEEP_ORIG], P_KEEP_ORIG, "Off", "On");

;
    return ve;
}

void *pointilism_malloc(int w, int h)
{
    pointilism_t *s = (pointilism_t*)vj_calloc(sizeof(pointilism_t));

    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t*)vj_malloc((size_t)len * 3u);

    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    s->rand_lut = (uint32_t*)vj_malloc(sizeof(uint32_t) * (size_t)len);

    if(!s->rand_lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    veejay_memset(s->buf[0], pixel_Y_lo_, len);
    veejay_memset(s->buf[1], 128, len);
    veejay_memset(s->buf[2], 128, len);

    uint32_t r_state = 0x9e3779b9U ^ ((uint32_t)w * 0x85ebca6bU) ^ ((uint32_t)h * 0xc2b2ae35U);

    for(int i = 0; i < len; i++)
        s->rand_lut[i] = pointilism_xorshift32(&r_state);

    s->n_threads = vje_advise_num_threads(len);

    return (void*)s;
}

void pointilism_free(void *ptr)
{
    pointilism_t *s = (pointilism_t*)ptr;

    free(s->buf[0]);
    free(s->rand_lut);
    free(s);
}

static inline void pointilism_background(pointilism_t *p,
                                         const uint8_t *restrict srcY,
                                         const uint8_t *restrict srcU,
                                         const uint8_t *restrict srcV,
                                         int len,
                                         int keep_original)
{
    uint8_t *restrict dstY = p->buf[0];
    uint8_t *restrict dstU = p->buf[1];
    uint8_t *restrict dstV = p->buf[2];

    if(keep_original) {
#pragma omp parallel for schedule(static) num_threads(p->n_threads)
        for(int i = 0; i < len; i++) {
            dstY[i] = srcY[i];
            dstU[i] = srcU[i];
            dstV[i] = srcV[i];
        }
    }
    else {
        veejay_memset(dstY, pixel_Y_lo_, len);
        veejay_memset(dstU, 128, len);
        veejay_memset(dstV, 128, len);
    }
}

static inline void pointilism_copy_back(pointilism_t *p,
                                        uint8_t *restrict dstY,
                                        uint8_t *restrict dstU,
                                        uint8_t *restrict dstV,
                                        int len)
{
    const uint8_t *restrict srcY = p->buf[0];
    const uint8_t *restrict srcU = p->buf[1];
    const uint8_t *restrict srcV = p->buf[2];

#pragma omp parallel for schedule(static) num_threads(p->n_threads)
    for(int i = 0; i < len; i++) {
        dstY[i] = srcY[i];
        dstU[i] = srcU[i];
        dstV[i] = srcV[i];
    }
}

static inline void pointilism_luma_range(const uint8_t *restrict Y,
                                         int w,
                                         int h,
                                         int cx,
                                         int cy,
                                         int kernel,
                                         uint8_t *min_luma,
                                         uint8_t *max_luma)
{
    const int y0 = clampi(cy - kernel, 0, h - 1);
    const int y1 = clampi(cy + kernel, 0, h - 1);
    const int x0 = clampi(cx - kernel, 0, w - 1);
    const int x1 = clampi(cx + kernel, 0, w - 1);
    uint8_t lo = 255;
    uint8_t hi = 0;

    for(int y = y0; y <= y1; y++) {
        const uint8_t *restrict row = Y + y * w;

        for(int x = x0; x <= x1; x++) {
            const uint8_t v = row[x];

            if(v < lo)
                lo = v;
            if(v > hi)
                hi = v;
        }
    }

    *min_luma = lo;
    *max_luma = hi;
}

static inline void pointilism_draw_dot(uint8_t *restrict dstY,
                                       uint8_t *restrict dstU,
                                       uint8_t *restrict dstV,
                                       int w,
                                       int h,
                                       int cx,
                                       int cy,
                                       int radius,
                                       uint8_t min_luma,
                                       uint8_t max_luma,
                                       uint8_t dot_u,
                                       uint8_t dot_v)
{
    const int r2 = radius * radius;
    const uint32_t invR2 = (uint32_t)((1u << 16) / (uint32_t)r2);
    const int y0 = clampi(cy - radius, 0, h - 1);
    const int y1 = clampi(cy + radius, 0, h - 1);
    const int x0 = clampi(cx - radius, 0, w - 1);
    const int x1 = clampi(cx + radius, 0, w - 1);
    const int range = (int)max_luma - (int)min_luma;

    for(int y = y0; y <= y1; y++) {
        const int dy = y - cy;
        const int dy2 = dy * dy;
        const int row = y * w;

        for(int x = x0; x <= x1; x++) {
            const int dx = x - cx;
            const int dist2 = dx * dx + dy2;

            if(dist2 <= r2) {
                const int weight = (int)(((uint32_t)(r2 - dist2) * invR2) >> 8);
                const int idx = row + x;

                dstY[idx] = (uint8_t)((int)max_luma - ((weight * range) >> 8));
                dstU[idx] = dot_u;
                dstV[idx] = dot_v;
            }
        }
    }
}

void pointilism_apply(void *ptr, VJFrame *frame, int *args)
{
    pointilism_t *p = (pointilism_t*)ptr;
    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;
    int min_radius = args[P_MIN_RADIUS];
    int max_radius = args[P_MAX_RADIUS];
    const int kernel_radius = args[P_KERNEL];
    const int loop = args[P_LOOP];
    const int keep_original = args[P_KEEP_ORIG];

    if(min_radius > max_radius) {
        const int tmp = min_radius;
        min_radius = max_radius;
        max_radius = tmp;
    }

    const int step = max_radius;
    const int radius_range = (max_radius - min_radius) + 1;

    const uint8_t *restrict srcY = frame->data[0];
    const uint8_t *restrict srcU = frame->data[1];
    const uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict dstY = p->buf[0];
    uint8_t *restrict dstU = p->buf[1];
    uint8_t *restrict dstV = p->buf[2];

    const uint32_t *restrict lut = p->rand_lut;

    if(!loop)
        pointilism_background(p, srcY, srcU, srcV, len, keep_original);

    for(int y = 0; y < h; y += step) {
        const int y_offset = y * w;

        for(int x = 0; x < w; x += step) {
            const int idx = y_offset + x;
            const uint32_t rnd = lut[idx];
            int cx = x + (int)(rnd % (uint32_t)step);
            int cy = y + (int)((rnd >> 8) % (uint32_t)step);

            if(cx >= w)
                cx = w - 1;
            if(cy >= h)
                cy = h - 1;

            const int center_idx = cy * w + cx;
            uint8_t min_luma;
            uint8_t max_luma;

            pointilism_luma_range(srcY, w, h, cx, cy, kernel_radius, &min_luma, &max_luma);

            const int radius = min_radius + (int)(rnd % (uint32_t)radius_range);

            pointilism_draw_dot(
                dstY,
                dstU,
                dstV,
                w,
                h,
                cx,
                cy,
                radius,
                min_luma,
                max_luma,
                srcU[center_idx],
                srcV[center_idx]
            );
        }
    }

    pointilism_copy_back(p, frame->data[0], frame->data[1], frame->data[2], len);
}
