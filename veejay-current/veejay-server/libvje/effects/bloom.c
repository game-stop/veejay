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
#include "bloom.h"

vj_effect *bloom_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 64;  ve->defaults[0] = 12;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 160;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255; ve->defaults[2] = 180;
    ve->limits[0][3] = 0; ve->limits[1][3] = 255; ve->defaults[3] = 0;

    ve->description = "Bloom";
    ve->sub_format = -1;
    ve->param_description = vje_build_param_list(ve->num_params, "Radius", "Intensity", "Threshold", "Persistence");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                    4,  44,  4,  14, 3400, 8800, 2400, 18,
        VJ_BEAT_GLOW,          VJ_BEAT_F_CONTINUOUS,                                         72, 225, 14, 58,  900, 2800, 0,    76,
        VJ_BEAT_CONTRAST,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 72, 220, 12, 46, 1000, 3200, 0,    68,
        VJ_BEAT_MEMORY,        VJ_BEAT_F_CONTINUOUS,                                         0,  168, 10, 42, 1200, 4200, 0,    62
    );

    return ve;
}

typedef struct {
    uint8_t *buf;
    int ds_w;
    int ds_h;
    int n_threads;
} bloom_t;

void *bloom_malloc(int width, int height)
{
    bloom_t *b = (bloom_t*) vj_calloc(sizeof(bloom_t));

    if(!b)
        return NULL;

    b->ds_w = (width + 1) >> 1;
    b->ds_h = (height + 1) >> 1;
    b->n_threads = vje_advise_num_threads(width * height);

    const size_t full_res_len = (size_t)width * (size_t)height;
    const size_t ds_res_len = (size_t)b->ds_w * (size_t)b->ds_h;
    const size_t total_size = full_res_len + (ds_res_len * 4);

    b->buf = (uint8_t*) vj_calloc(total_size);

    if(!b->buf) {
        free(b);
        return NULL;
    }

    return b;
}

void bloom_free(void *ptr)
{
    bloom_t *b = (bloom_t*) ptr;

    if(!b)
        return;

    if(b->buf)
        free(b->buf);

    free(b);
}

static void downsample2x(uint8_t *dst, const uint8_t *src, int w, int h, int dw, int n_threads)
{
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < h; y += 2)
    {
        const int y1 = y + 1 < h ? y + 1 : y;
        const uint8_t *restrict s0 = src + y * w;
        const uint8_t *restrict s1 = src + y1 * w;
        uint8_t *restrict d = dst + (y >> 1) * dw;

        for(int x = 0; x < w; x += 2)
        {
            const int x1 = x + 1 < w ? x + 1 : x;
            d[x >> 1] = (uint8_t)(((int)s0[x] + (int)s0[x1] + (int)s1[x] + (int)s1[x1]) >> 2);
        }
    }
}

static void upsample2x(uint8_t *dst, const uint8_t *src, int w, int h, int sw, int n_threads)
{
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < h; y++)
    {
        uint8_t *restrict d = dst + y * w;
        const uint8_t *restrict s = src + (y >> 1) * sw;

        for(int x = 0; x < w; x++)
            d[x] = s[x >> 1];
    }
}

void bloom_apply(void *ptr, VJFrame *frame, int *args)
{
    bloom_t *b = (bloom_t*) ptr;

    const int radius = args[0] >> 1;
    const int intensity = args[1];
    const int threshold = args[2];
    const int persistence = args[3];

    if(radius <= 0 || intensity <= 0)
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;
    const int ds_len = b->ds_w * b->ds_h;
    const int n_threads = b->n_threads;

    uint8_t *restrict L = frame->data[0];

    uint8_t *B = b->buf;
    uint8_t *D = B + len;
    uint8_t *T = D + ds_len;
    uint8_t *BL = T + ds_len;
    uint8_t *PB = BL + ds_len;

    #pragma omp parallel for simd num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        const int v = (int)L[i] - threshold;
        B[i] = v > 0 ? (uint8_t)v : 0;
    }

    downsample2x(D, B, w, h, b->ds_w, n_threads);

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < b->ds_h; y++)
        veejay_blur(T + y * b->ds_w, D + y * b->ds_w, b->ds_w, radius, 1, 1);

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int x = 0; x < b->ds_w; x++)
        veejay_blur(BL + x, T + x, b->ds_h, radius, b->ds_w, b->ds_w);

    if(persistence > 0)
    {
        #pragma omp parallel for simd num_threads(n_threads) schedule(static)
        for(int i = 0; i < ds_len; i++)
        {
            const int persistent = (BL[i] * (255 - persistence) + PB[i] * persistence) >> 8;
            const int gain = persistent << 2;

            PB[i] = (uint8_t)persistent;
            BL[i] = gain > 255 ? 255 : (uint8_t)gain;
        }
    }
    else
    {
        #pragma omp parallel for simd num_threads(n_threads) schedule(static)
        for(int i = 0; i < ds_len; i++)
        {
            const int v = BL[i] << 1;
            PB[i] = 0;
            BL[i] = v > 255 ? 255 : (uint8_t)v;
        }
    }

    upsample2x(B, BL, w, h, b->ds_w, n_threads);

    #pragma omp parallel for simd num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        const int bloom = (B[i] * intensity) >> 7;
        const int v = (int)L[i] + bloom;

        L[i] = v > 255 ? 255 : (uint8_t)v;
    }
}
