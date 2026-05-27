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

#include <config.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "pointilism.h"

typedef struct {
    uint8_t *buf[3];
    int *rand_lut;
    int n_threads;
} pointilism_t;

static inline uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline int pointilism_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *pointilism_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 3;
    ve->defaults[1] = 7;
    ve->defaults[2] = 2;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;

    ve->limits[0][0] = 1; ve->limits[1][0] = 16;
    ve->limits[0][1] = 1; ve->limits[1][1] = 16;
    ve->limits[0][2] = 1; ve->limits[1][2] = 16;
    ve->limits[0][3] = 0; ve->limits[1][3] = 1;
    ve->limits[0][4] = 0; ve->limits[1][4] = 1;

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
    ve->parallel = 0;

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 1,                  7,                  6, 22, 1800, 4200, 900, 30,    /* Min */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 5,                  16,                 6, 22, 1800, 4200, 900, 30,    /* Max */
        VJ_BEAT_DETAIL,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 1,                  8,                  6, 22, 1600, 3400, 700, 30,    /* Kernel */
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Loop */
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Keep original */
    );

    (void) w;
    (void) h;

    return ve;
}

void *pointilism_malloc(int w, int h)
{
    pointilism_t *s = (pointilism_t*) vj_calloc(sizeof(pointilism_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    s->rand_lut = (int*) vj_malloc(sizeof(int) * len);
    if(!s->rand_lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    veejay_memset(s->buf[0], pixel_Y_lo_, len);
    veejay_memset(s->buf[1], 128, len);
    veejay_memset(s->buf[2], 128, len);

    uint32_t r_state = (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)s;
    if(r_state == 0)
        r_state = 1;

    for(int i = 0; i < len; i++)
        s->rand_lut[i] = (int)(xorshift32(&r_state) & 0x7fffffff);

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void pointilism_free(void *ptr)
{
    pointilism_t *s = (pointilism_t*) ptr;
    if(!s)
        return;

    if(s->buf[0])
        free(s->buf[0]);

    if(s->rand_lut)
        free(s->rand_lut);

    free(s);
}

static inline void pointilism_background(
    pointilism_t *p,
    const uint8_t *restrict srcY,
    const uint8_t *restrict srcU,
    const uint8_t *restrict srcV,
    int len,
    int keep_original
) {
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
    } else {
        veejay_memset(dstY, pixel_Y_lo_, len);
        veejay_memset(dstU, 128, len);
        veejay_memset(dstV, 128, len);
    }
}

static inline void pointilism_copy_back(
    pointilism_t *p,
    uint8_t *restrict dstY,
    uint8_t *restrict dstU,
    uint8_t *restrict dstV,
    int len
) {
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

void pointilism_apply(void *ptr, VJFrame *frame, int *args)
{
    pointilism_t *p = (pointilism_t*) ptr;
    if(!p || !frame || !args)
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    if(w <= 0 || h <= 0 || len <= 0)
        return;

    int minRadius = pointilism_clampi(args[0], 1, 16);
    int maxRadius = pointilism_clampi(args[1], 1, 16);
    int kernelRadius = pointilism_clampi(args[2], 1, 16);
    const int loop = args[3] ? 1 : 0;
    const int keep_original = args[4] ? 1 : 0;

    if(minRadius > maxRadius) {
        int tmp = maxRadius;
        maxRadius = minRadius;
        minRadius = tmp;
    }

    const int step = maxRadius > 0 ? maxRadius : 1;
    const int rad_range = (maxRadius - minRadius) + 1;

    const uint8_t *restrict srcY = frame->data[0];
    const uint8_t *restrict srcU = frame->data[1];
    const uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict dstY = p->buf[0];
    uint8_t *restrict dstU = p->buf[1];
    uint8_t *restrict dstV = p->buf[2];

    int *restrict lut = p->rand_lut;

    if(!loop)
        pointilism_background(p, srcY, srcU, srcV, len, keep_original);

    for(int y = 0; y < h; y += step) {
        const int y_offset = y * w;

        for(int x = 0; x < w; x += step) {
            const int current_idx = y_offset + x;
            const uint32_t rnd = (uint32_t)lut[current_idx % len];

            int centerX = x + (int)(rnd % (uint32_t)step);
            int centerY = y + (int)((rnd >> 8) % (uint32_t)step);

            if(centerX >= w)
                centerX = w - 1;
            if(centerY >= h)
                centerY = h - 1;

            const int center_idx = centerY * w + centerX;

            uint8_t minL = 255;
            uint8_t maxL = 0;

            for(int ky = -kernelRadius; ky <= kernelRadius; ky++) {
                const int ny = pointilism_clampi(centerY + ky, 0, h - 1);
                const uint8_t *kRow = srcY + ny * w;

                for(int kx = -kernelRadius; kx <= kernelRadius; kx++) {
                    const int nx = pointilism_clampi(centerX + kx, 0, w - 1);
                    const uint8_t val = kRow[nx];

                    if(val < minL)
                        minL = val;
                    if(val > maxL)
                        maxL = val;
                }
            }

            const uint8_t dotU = srcU[center_idx];
            const uint8_t dotV = srcV[center_idx];

            const int rangeL = (int)maxL - (int)minL;
            const int radius = minRadius + (int)(rnd % (uint32_t)rad_range);
            const int r2 = radius * radius;
            const uint32_t invR2 = (uint32_t)((1 << 16) / (r2 > 0 ? r2 : 1));

            const int drawStartY = pointilism_clampi(centerY - radius, 0, h - 1);
            const int drawEndY   = pointilism_clampi(centerY + radius, 0, h - 1);
            const int drawStartX = pointilism_clampi(centerX - radius, 0, w - 1);
            const int drawEndX   = pointilism_clampi(centerX + radius, 0, w - 1);

            for(int py = drawStartY; py <= drawEndY; py++) {
                const int dy = py - centerY;
                const int dy2 = dy * dy;
                const int py_off = py * w;

                for(int px = drawStartX; px <= drawEndX; px++) {
                    const int dx = px - centerX;
                    const int dist2 = dx * dx + dy2;

                    if(dist2 <= r2) {
                        const int weight = (int)(((uint32_t)(r2 - dist2) * invR2) >> 8);
                        const int out_idx = py_off + px;

                        dstY[out_idx] = (uint8_t)((int)maxL - ((weight * rangeL) >> 8));
                        dstU[out_idx] = dotU;
                        dstV[out_idx] = dotV;
                    }
                }
            }
        }
    }

    pointilism_copy_back(p, frame->data[0], frame->data[1], frame->data[2], len);
}