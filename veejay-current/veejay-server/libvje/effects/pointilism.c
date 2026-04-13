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
#include <omp.h>
#include <time.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "motionblur.h"
#define FIXED_POINT_BITS 16
#define FIXED_POINT_ONE (1 << FIXED_POINT_BITS)

typedef struct {
    uint8_t *buf[3];
    uint8_t *tmp_buf[3];
    int *rand_lut;
    int n_threads;
} pointilism_t;

vj_effect *pointilism_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 3;  // minRadius
    ve->defaults[1] = 7;  // maxRadius
    ve->defaults[2] = 2;  // kernelRadius
    ve->defaults[3] = 0;  // loop on ; do nothing with buffers
    ve->defaults[4] = 0;  // keep original ; clear or copy over buffer

    ve->limits[0][0] = 1; ve->limits[1][0] = 16;
    ve->limits[0][1] = 1; ve->limits[1][1] = 16;
    ve->limits[0][2] = 1; ve->limits[1][2] = 16;
    ve->limits[0][3] = 0; ve->limits[1][3] = 1;
    ve->limits[0][4] = 0; ve->limits[1][4] = 1;

    ve->param_description = vje_build_param_list(ve->num_params, "Min", "Max", "Kernel", "Loop", "Keep original");
    ve->description = "Pointilism";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    return ve;
}

static inline uint32_t xorshift32(uint32_t *state) {
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

void *pointilism_malloc(int w, int h)
{
    pointilism_t *s = (pointilism_t*) vj_calloc(sizeof(pointilism_t));
    if(!s) return NULL;

    size_t total = w*h*6;
    s->buf[0] = (uint8_t*) vj_malloc(total);
    if(!s->buf[0]) { free(s); return NULL; }

    s->buf[1] = s->buf[0] + w*h;
    s->buf[2] = s->buf[1] + w*h;
    s->tmp_buf[0] = s->buf[2] + w*h;
    s->tmp_buf[1] = s->tmp_buf[0] + w;
    s->tmp_buf[2] = s->tmp_buf[1] + w;

    s->rand_lut = (int*) vj_malloc(sizeof(int) * w * h);
    if(!s->rand_lut) { free(s->buf[0]); free(s); return NULL; }

    veejay_memset(s->buf[0], 0, w*h);
    veejay_memset(s->buf[1], 128, w*h*2);

//    unsigned int seed = (unsigned int) time(NULL) ^ (unsigned int)(uintptr_t)s;
//    for(int i=0; i<w*h; i++) s->rand_lut[i] = rand_r(&seed);

	uint32_t r_state = (uint32_t) time(NULL) + (uint32_t)(uintptr_t)s;
	if( r_state == 0 ) r_state = 1;
	for( int i = 0; i < w * h; i ++ ) {
		s->rand_lut[i] = (int) ( xorshift32( &r_state ) & 0x7FFFFFFF );
	}

    s->n_threads = vje_advise_num_threads(w*h);

    return (void*) s;
}

void pointilism_free(void *ptr)
{
    pointilism_t *s = (pointilism_t*) ptr;
    free(s->buf[0]);
    free(s->rand_lut);
    free(s);
}

static inline int clamp(int v, int min, int max) {
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

#include <omp.h>

void pointilism_apply(void *ptr, VJFrame *frame, int *args)
{
    pointilism_t *p = (pointilism_t*) ptr;
    const int w = frame->width, h = frame->height;
    const int total_pixels = w * h;

    int minRadius = args[0], maxRadius = args[1], kernelRadius = args[2];
    if(minRadius > maxRadius) { int tmp = maxRadius; maxRadius = minRadius; minRadius = tmp; }
    
    int step = maxRadius; 
    if (step < 1) step = 1;

    const uint8_t *restrict srcY = frame->data[0];
    const uint8_t *restrict srcU = frame->data[1];
    const uint8_t *restrict srcV = frame->data[2];
    uint8_t *restrict dstY = p->buf[0];
    uint8_t *restrict dstU = p->buf[1];
    uint8_t *restrict dstV = p->buf[2];
    int *restrict lut = p->rand_lut;

    if (args[3] != 1) {
        if (args[4] == 1) {
            veejay_memcpy(dstY, srcY, total_pixels);
            veejay_memcpy(dstU, srcU, total_pixels);
            veejay_memcpy(dstV, srcV, total_pixels);
        } else {
            veejay_memset(dstY, 0, total_pixels);
            veejay_memset(dstU, 128, total_pixels);
            veejay_memset(dstV, 128, total_pixels);
        }
    }

    int rad_range = (maxRadius - minRadius) + 1;
    if (rad_range < 1) rad_range = 1;

    for (int pass = 0; pass < 2; pass++) {
        #pragma omp parallel num_threads(p->n_threads)
        {
            int tid = omp_get_thread_num();
            if (tid % 2 == pass) {
                int num_t = omp_get_num_threads();
                int strip_h = h / num_t;
                int thread_startY = tid * strip_h;
                int thread_endY = (tid == num_t - 1) ? h : (tid + 1) * strip_h;

                for (int y = thread_startY; y < thread_endY; y += step) {
                    int y_offset = y * w;
                    for (int x = 0; x < w - step; x += step) {
                        int current_idx = y_offset + x;
                        uint32_t rnd = (uint32_t)lut[current_idx % total_pixels];
                        
                        int centerX = x + (rnd % step);
                        int centerY = y + ((rnd >> 8) % step);
                        
                        if (centerY < thread_startY || centerY >= thread_endY) continue;

                        int center_idx = centerY * w + centerX;

                        uint8_t minL = 255, maxL = 0;
                        for (int ky = -kernelRadius; ky <= kernelRadius; ky++) {
                            int ny = clamp(centerY + ky, 0, h - 1);
                            const uint8_t *kRow = &srcY[ny * w];
                            for (int kx = -kernelRadius; kx <= kernelRadius; kx++) {
                                int nx = clamp(centerX + kx, 0, w - 1);
                                uint8_t val = kRow[nx];
                                if (val < minL) minL = val;
                                if (val > maxL) maxL = val;
                            }
                        }
                        
                        uint8_t minU = srcU[center_idx], minV = srcV[center_idx];
                        int rangeL = maxL - minL;
                        int radius = minRadius + (rnd % rad_range);
                        int r2 = radius * radius;
                        if (r2 < 1) r2 = 1;
                        uint32_t invR2 = (1 << 16) / r2;

                        int drawStartY = clamp(centerY - radius, 0, h - 1);
                        int drawEndY   = clamp(centerY + radius, 0, h - 1);
                        int drawStartX = clamp(centerX - radius, 0, w - 1);
                        int drawEndX   = clamp(centerX + radius, 0, w - 1);

                        for (int py = drawStartY; py <= drawEndY; py++) {
                            int dy2 = (py - centerY) * (py - centerY);
                            int py_off = py * w;
                            for (int px = drawStartX; px <= drawEndX; px++) {
                                int dx = px - centerX;
                                int dist2 = dx * dx + dy2;
                                if (dist2 <= r2) {
                                    int weight = ((r2 - dist2) * invR2) >> 8;
                                    int out_idx = py_off + px;
                                    dstY[out_idx] = maxL - ((weight * rangeL) >> 8);
                                    dstU[out_idx] = minU;
                                    dstV[out_idx] = minV;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    veejay_memcpy(frame->data[0], dstY, total_pixels);
    veejay_memcpy(frame->data[1], dstU, total_pixels);
    veejay_memcpy(frame->data[2], dstV, total_pixels);
}