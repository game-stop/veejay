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
#include <veejaycore/vjmem.h>
#include "motionblur.h"
#define FIXED_POINT_BITS 16
#define FIXED_POINT_ONE (1 << FIXED_POINT_BITS)

typedef struct {
    uint8_t *buf[3];
    uint8_t *tmp_buf[3];
    int *rand_lut;
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

    for(int i=0; i<w*h; i++) s->rand_lut[i] = rand();

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
 
static inline void process_corners(
    int x, int y, int kernelRadius, int minRadius, int maxRadius,
    int w, int h,
    const uint8_t *srcY, const uint8_t *srcU, const uint8_t *srcV,
    uint8_t *dstY, uint8_t *dstU, uint8_t *dstV,
    int *lut,
    uint8_t *tmpY, uint8_t *tmpU, uint8_t *tmpV)
{
    int x_start = (x - kernelRadius < 0) ? 0 : x - kernelRadius;
    int y_start = (y - kernelRadius < 0) ? 0 : y - kernelRadius;
    int x_end   = (x + kernelRadius >= w) ? w - 1 : x + kernelRadius;
    int y_end   = (y + kernelRadius >= h) ? h - 1 : y + kernelRadius;

    uint8_t minL=0xFF, maxL=0;
    uint8_t minU=0xFF, maxU=0;
    uint8_t minV=0xFF, maxV=0;

    for (int wy = y_start; wy <= y_end; wy++) {
        for (int wx = x_start; wx <= x_end; wx++) {
            int idx = wy * w + wx;
            uint8_t L = srcY[idx];
            uint8_t U = srcU[idx];
            uint8_t V = srcV[idx];
            
            if (L < minL) minL = L;
            if (L > maxL) maxL = L;
            if (U < minU) minU = U;
            if (U > maxU) maxU = U;
            if (V < minV) minV = V;
            if (V > maxV) maxV = V;
        }
    }

    int radius = minRadius + lut[y*w + x] % (maxRadius - minRadius + 1);
    if (radius < 1) radius = 1;
    int r2 = radius*radius;
    int fixedR2 = r2 << FIXED_POINT_BITS;

    int startX = (x-radius<0)? -x : -radius;
    int startY = (y-radius<0)? -y : -radius;
    int endX = (x+radius>=w)? w-x-1 : radius;
    int endY = (y+radius>=h)? h-y-1 : radius;

    for(int dx=startX; dx<=endX; dx++){
        for(int dy=startY; dy<=endY; dy++){
            int nx=x+dx, ny=y+dy;
            int dist2 = dx*dx + dy*dy;
            
            if (dist2 <= r2) {
                int gradient = ((r2 - dist2) << FIXED_POINT_BITS) / r2;

                dstY[ny*w + nx] = (uint8_t)((maxL*(FIXED_POINT_ONE - gradient) + minL*gradient) >> FIXED_POINT_BITS);
                dstU[ny*w + nx] = (uint8_t)((maxU*(FIXED_POINT_ONE - gradient) + minU*gradient) >> FIXED_POINT_BITS);
                dstV[ny*w + nx] = (uint8_t)((maxV*(FIXED_POINT_ONE - gradient) + minV*gradient) >> FIXED_POINT_BITS);
            }
        }
    }
}
void pointilism_apply(void *ptr, VJFrame *frame, int *args)
{
    pointilism_t *p = (pointilism_t*) ptr;
    const int w = frame->width, h = frame->height;

    int minRadius = args[0], maxRadius = args[1], kernelRadius = args[2];
    if(minRadius > maxRadius) { int tmp = maxRadius; maxRadius = minRadius; minRadius = tmp; }
    
    int step = maxRadius; 
    if (step < 1) step = 1;

    const uint8_t *srcY = frame->data[0], *srcU = frame->data[1], *srcV = frame->data[2];
    uint8_t *restrict dstY = p->buf[0];
    uint8_t *restrict dstU = p->buf[1];
    uint8_t *restrict dstV = p->buf[2];
    int *lut = p->rand_lut;

    if (args[3] == 1) { 
        // loop on,do nothing to the buffers. 
    } else if (args[4] == 1) { 
        veejay_memcpy(p->buf[0], srcY, w * h);
        veejay_memcpy(p->buf[1], srcU, w * h);
        veejay_memcpy(p->buf[2], srcV, w * h);
    } else {
        veejay_memset(p->buf[0], 0, w * h);
        veejay_memset(p->buf[1], 128, w * h);
        veejay_memset(p->buf[2], 128, w * h);
    }
    
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            
            int jitterX = lut[(y * w + x) % (w * h)] % step;
            int jitterY = lut[((y * w + x) + 1) % (w * h)] % step;
            
            int centerX = clamp(x + jitterX, 0, w - 1);
            int centerY = clamp(y + jitterY, 0, h - 1);

            uint8_t minL = 0xFF, maxL = 0;
            uint8_t minU = 0xFF, maxU = 0;
            uint8_t minV = 0xFF, maxV = 0;

            for (int ky = -kernelRadius; ky <= kernelRadius; ky++) {
                int ny = clamp(centerY + ky, 0, h - 1);
                for (int kx = -kernelRadius; kx <= kernelRadius; kx++) {
                    int nx = clamp(centerX + kx, 0, w - 1);
                    int idx = ny * w + nx;
                    if (srcY[idx] < minL) minL = srcY[idx];
                    if (srcY[idx] > maxL) maxL = srcY[idx];
                }
            }
            
            minU = srcU[centerY * w + centerX];
            minV = srcV[centerY * w + centerX];

            int radius = minRadius + (lut[centerY * w + centerX] % (maxRadius - minRadius + 1));
            int r2 = radius * radius;

            int startY = clamp(centerY - radius, 0, h - 1);
            int endY   = clamp(centerY + radius, 0, h - 1);
            int startX = clamp(centerX - radius, 0, w - 1);
            int endX   = clamp(centerX + radius, 0, w - 1);

            for (int py = startY; py <= endY; py++) {
                int dy = py - centerY;
                for (int px = startX; px <= endX; px++) {
                    int dx = px - centerX;
                    int dist2 = dx * dx + dy * dy;

                    if (dist2 <= r2) {
                        int gradient = ((r2 - dist2) << FIXED_POINT_BITS) / r2;
                        int out_idx = py * w + px;
                        
                        dstY[out_idx] = (uint8_t)((maxL * (FIXED_POINT_ONE - gradient) + minL * gradient) >> FIXED_POINT_BITS);
                        dstU[out_idx] = minU;
                        dstV[out_idx] = minV;
                    }
                }
            }
        }
    }

    veejay_memcpy(frame->data[0], dstY, w * h);
    veejay_memcpy(frame->data[1], dstU, w * h);
    veejay_memcpy(frame->data[2], dstV, w * h);
}