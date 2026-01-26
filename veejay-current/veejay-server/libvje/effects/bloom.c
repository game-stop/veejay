/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2019 Niels Elburg <nwelburg@gmail.com>
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
#include "bloom.h"
#ifdef STRICT_CHECKING
#include <assert.h>
#endif


vj_effect *bloom_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;
    ve->defaults   = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0]  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1]  = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 64;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->defaults[0] = 12;
    ve->defaults[1] = 160;
    ve->defaults[2] = 180;
    ve->defaults[3] = 0;

    ve->description = "Bloom";
    ve->sub_format  = -1;

    ve->param_description =
        vje_build_param_list(
            ve->num_params,
            "Radius",
            "Intensity",
            "Threshold",
            "Persistence"
        );

    return ve;
}

typedef struct {
    uint8_t *buf;
    uint8_t *last_bloom;
    int ds_w;
    int ds_h;
} bloom_t;


#define BLOOM_PLANES 4

void *bloom_malloc(int width, int height)
{
    bloom_t *b = (bloom_t*) vj_calloc(sizeof(bloom_t));
    if (!b) return NULL;

    b->ds_w = width  >> 1;
    b->ds_h = height >> 1;

    size_t full_res_len = (size_t)width * height;
    size_t ds_res_len   = (size_t)b->ds_w * b->ds_h;
    size_t total_size = full_res_len + (ds_res_len * 4);

    b->buf = (uint8_t*) vj_calloc(total_size);

    if (!b->buf) {
        free(b);
        return NULL;
    }

    return b;
}
void bloom_free(void *ptr)
{
    if (!ptr) return;

    bloom_t *b = (bloom_t*) ptr;
    
    if (b->buf) {
        free(b->buf);
    }
    
    free(b);
}

static void downsample2x(uint8_t *dst, uint8_t *src, int w, int h)
{
    int dw = w >> 1;

    for (int y = 0; y < h; y += 2) {
        uint8_t *s0 = src + y * w;
        uint8_t *s1 = s0 + w;
        uint8_t *d  = dst + (y >> 1) * dw;

        for (int x = 0; x < w; x += 2) {
            d[x >> 1] = (s0[x] + s0[x+1] + s1[x] + s1[x+1]) >> 2;
        }
    }
}

static void upsample2x(uint8_t *dst, uint8_t *src, int w, int h)
{
    int sw = w >> 1;

    for (int y = 0; y < h; y++) {
        uint8_t *d = dst + y * w;
        uint8_t *s = src + (y >> 1) * sw;

        for (int x = 0; x < w; x++) {
            d[x] = s[x >> 1];
        }
    }
}

void bloom_apply(void *ptr, VJFrame *frame, int *args)
{
    const int radius    = args[0] >> 1;
    const int intensity = args[1];
    const int threshold = args[2];
    const int persistence = args[3];

    if (radius <= 0 || intensity <= 0)
        return;

    bloom_t *b = (bloom_t*) ptr;

    const int w   = frame->width;
    const int h   = frame->height;
    const int len = frame->len;

    uint8_t *restrict L = frame->data[0];

    const int ds_len = b->ds_w * b->ds_h;
    
    uint8_t *B  = b->buf;
    uint8_t *D  = B  + len;
    uint8_t *T  = D  + ds_len;
    uint8_t *BL = T  + ds_len;
    uint8_t *PB = BL + ds_len;

#pragma omp simd
    for (int i = 0; i < len; i++) {
        int v = (int)L[i] - threshold;
        B[i] = (v > 0) ? (uint8_t)v : 0;
    }

    downsample2x(D, B, w, h);

    for (int y = 0; y < b->ds_h; y++) {
        veejay_blur(
            T  + y * b->ds_w,
            D  + y * b->ds_w,
            b->ds_w,
            radius,
            1,
            1
        );
    }

    for (int x = 0; x < b->ds_w; x++) {
        veejay_blur(
            BL + x,
            T  + x,
            b->ds_h,
            radius,
            b->ds_w,
            b->ds_w
        );
    }

    if (persistence > 0) {
#pragma omp simd
        for (int i = 0; i < ds_len; i++) {
            int persistent = (BL[i] * (255 - persistence) + PB[i] * persistence) >> 8;
            PB[i] = (uint8_t)persistent;
            int gain = persistent << 2;
            BL[i] = (gain > 255) ? 255 : (uint8_t)gain;
        }
    }
    else {
#pragma omp simd
        for (int i = 0; i < ds_len; i++) {
            int v = BL[i] * 2;
            BL[i] = (v > 255) ? 255 : v;
        }
    }

    upsample2x(B, BL, w, h);

#pragma omp simd
    for (int i = 0; i < len; i++) {
        int bloom = (B[i] * intensity) >> 7;
        int v = L[i] + bloom;
        L[i] = (v > 255) ? 255 : v;
    }
}
