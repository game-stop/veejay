/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2026 Niels Elburg <nwelburg@gmail.com>
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
#include <math.h>
#include "smartblur.h"

#ifndef CLAMP
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

typedef struct {
    uint8_t *small_src;
    uint8_t *small_tmp;
    int ds_w;
    int ds_h;
} smartblur_t;

vj_effect *smartblur_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 64;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1; 

    ve->defaults[0] = 10;
    ve->defaults[1] = 20;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;

    ve->description = "Smart Blur (DS)";
    ve->param_description =
        vje_build_param_list(ve->num_params, "Radius", "Threshold", "Swap", "Show Mask");

    ve->sub_format = -1;
    return ve;
}

void *smartblur_malloc(int w, int h) {
    smartblur_t *s = (smartblur_t*) vj_malloc(sizeof(smartblur_t));
    if(!s) return NULL;
    
    s->ds_w = w / 2;
    s->ds_h = h / 2;

    size_t ds_size = s->ds_w * s->ds_h;
    s->small_src = (uint8_t*) vj_calloc(ds_size * 2);
    
    if(!s->small_src) {
        free(s);
        return NULL;
    }
    
    s->small_tmp = s->small_src + ds_size;
    return (void*) s;
}

void smartblur_free(void *ptr) {
    smartblur_t *s = (smartblur_t*) ptr;
    if(s && s->small_src) free(s->small_src);
    if(s) free(s);
}

static inline void downsample(const uint8_t *src, uint8_t *dst, int w, int h, int dst_w) {
    #pragma omp parallel for
    for (int y = 0; y < h / 2; y++) {
        for (int x = 0; x < w / 2; x++) {
            int p1 = src[(y * 2) * w + (x * 2)];
            int p2 = src[(y * 2) * w + (x * 2 + 1)];
            int p3 = src[(y * 2 + 1) * w + (x * 2)];
            int p4 = src[(y * 2 + 1) * w + (x * 2 + 1)];
            dst[y * dst_w + x] = (uint8_t)((p1 + p2 + p3 + p4 + 2) >> 2);
        }
    }
}

static inline void upsample(uint8_t *dst, const uint8_t *src, int w, int h, int src_w) {
    #pragma omp parallel for
    for (int y = 0; y < h; y++) {
        int sy = y >> 1;
        for (int x = 0; x < w; x++) {
            int sx = x >> 1;
            dst[y * w + x] = src[sy * src_w + sx];
        }
    }
}

void smartblur_apply(void *ptr, VJFrame *frame, int *args) {
    smartblur_t *s = (smartblur_t*) ptr;
    if (!s || !frame || !args) return;

    const int w = frame->width;
    const int h = frame->height;

    const int radius    = CLAMP(args[0] >> 1, 1, 64);
    const int threshold = args[1];
    const int swap      = args[2];
    const int show      = args[3];

    uint8_t *restrict src = frame->data[0];
    uint8_t *restrict ds_src = s->small_src;
    uint8_t *restrict ds_tmp = s->small_tmp;
    
    const int dw = s->ds_w;
    const int dh = s->ds_h;


    downsample(src, ds_src, w, h, dw);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < dh; y++) {
        uint8_t *row_src = ds_src + (y * dw);
        uint8_t *row_tmp = ds_tmp + (y * dw);

        for (int x = 0; x < dw; x++) {
            const int center = row_src[x];
            int sum = 0;
            int count = 0;

            #pragma omp simd reduction(+:sum,count)
            for (int k = -radius; k <= radius; k++) {
                int ix = x + k;
                ix = (ix < 0) ? 0 : (ix >= dw ? dw - 1 : ix);
                
                const int val = row_src[ix];
                const int diff = abs(val - center);

                int condition = swap ? (diff > threshold) : (diff <= threshold);
                int weight = condition * (swap ? diff : (threshold - diff));

                sum   += val * weight;
                count += weight;
            }
            row_tmp[x] = (count > 0) ? (uint8_t)(sum / count) : (uint8_t)center;
        }
    }

    const int TILE_WIDTH = 16;
    #pragma omp parallel for schedule(dynamic)
    for (int tx = 0; tx < dw; tx += TILE_WIDTH) {
        int tile_w = (tx + TILE_WIDTH > dw) ? (dw - tx) : TILE_WIDTH;

        for (int y = 0; y < dh; y++) {
            #pragma omp simd
            for (int x = tx; x < tx + tile_w; x++) {
                const int center = ds_tmp[y * dw + x];
                int sum = 0;
                int count = 0;

                for (int k = -radius; k <= radius; k++) {
                    int iy = y + k;
                    iy = (iy < 0) ? 0 : (iy >= dh ? dh - 1 : iy);
                    
                    const int val = ds_tmp[iy * dw + x];
                    const int diff = abs(val - center);

                    int condition = swap ? (diff > threshold) : (diff <= threshold);
                    int weight = condition * (swap ? diff : (threshold - diff));

                    sum   += val * weight;
                    count += weight;
                }
                ds_src[y * dw + x] = (count > 0) ? (uint8_t)(sum / count) : (uint8_t)center;
            }
        }
    }

    if (show) {
        upsample(src, ds_src, w, h, dw);
        
        #pragma omp parallel for
        for (int i = 0; i < w * h; i++) {
             if (src[i] > 10) src[i] = 255; else src[i] = 0;
        }
        veejay_memset(frame->data[1], 128, frame->len/4);
        veejay_memset(frame->data[2], 128, frame->len/4);
    } else {
        upsample(src, ds_src, w, h, dw);
    }
}