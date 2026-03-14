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
#include "camerabounce.h"

vj_effect *camerabounce_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 300;
    ve->defaults[0] = 30;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 100;
    ve->defaults[1] = 50;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 100;
    ve->defaults[2] = 20;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 100;
    ve->defaults[3] = 40;

    ve->sub_format = 1;

    ve->description = "Camera Bounce";
    ve->param_description = vje_build_param_list( ve->num_params, 
        "Tempo (Frames)", 
        "Impact (Percentage)", 
        "Motion Blur", 
        "Zoom Depth" 
    );
    
    return ve;
}

typedef struct {
    uint8_t *buf[3];
    uint32_t *sat[3];
    float *strength_map;
    int frameNumber;
    int n_threads;
    int w, h;
} camera_t;

void *camerabounce_malloc(int w, int h) {
    camera_t *c = (camera_t*) vj_calloc(sizeof(camera_t));
    if(!c) return NULL;

    size_t plane_size = w * h;

    c->buf[0] = (uint8_t*) vj_malloc(plane_size * 3);
    c->sat[0] = (uint32_t*) vj_malloc(plane_size * 3 * sizeof(uint32_t));
    c->strength_map = (float*) vj_malloc(plane_size * sizeof(float));

    if(!c->buf[0] || !c->sat[0] || !c->strength_map) {
        camerabounce_free(c);
        return NULL;
    }

    c->buf[1] = c->buf[0] + plane_size;
    c->buf[2] = c->buf[1] + plane_size;
    c->sat[1] = c->sat[0] + plane_size;
    c->sat[2] = c->sat[1] + plane_size;

    float halfW = w * 0.5f;
    float halfH = h * 0.5f;
    float maxDistSq = halfW * halfW + halfH * halfH;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float dx = halfW - x;
            float dy = halfH - y;
            float distSq = dx * dx + dy * dy;
            float norm = distSq / maxDistSq;
            c->strength_map[y * w + x] = norm * norm * 100.0f;
        }
    }

    c->w = w; c->h = h;
    c->n_threads = vje_advise_num_threads(plane_size);
    return (void*) c;
}

void camerabounce_free(void *ptr) {
    camera_t *c = (camera_t*) ptr;
    free(c->buf[0]);
    free(c);
}
void camerabounce_apply(void *ptr, VJFrame* frame, int *args)
{
    camera_t *c = (camera_t*) ptr;
    const int w = frame->width;
    const int h = frame->height;

    const int zoomInterval  = args[0];
    const int zoomDuration  = (args[1] * zoomInterval) / 100;
    const float blurAmount  = args[2] / 10.0f;
    
    const float maxZoomFactor = 1.0 + (args[3] / 50.0); 

    const int currentFrame = c->frameNumber % zoomInterval;
    c->frameNumber++;

    if (currentFrame > zoomDuration || zoomDuration <= 0)
        return;

    float interpolationFactor;
    int halfDuration = zoomDuration >> 1;
    if (halfDuration == 0) halfDuration = 1;

    if (currentFrame <= halfDuration)
        interpolationFactor = (float)currentFrame / halfDuration;
    else
        interpolationFactor = (float)(zoomDuration - currentFrame) / halfDuration;

    float zoomFactor = 1.0 + (interpolationFactor * (maxZoomFactor - 1.0));
    const float invZoom = 1.0 / zoomFactor;
    const float offsetX = (w - (w * zoomFactor)) * 0.5;
    const float offsetY = (h - (h * zoomFactor)) * 0.5;

#pragma omp parallel for num_threads(c->n_threads) schedule(static)
    for (int y = 0; y < h; y++) {
        int srcY = (int)((y - offsetY) * invZoom);
        srcY = (srcY < 0) ? 0 : (srcY >= h ? h - 1 : srcY);
        
        uint8_t *rowY = frame->data[0] + srcY * w;
        uint8_t *rowU = frame->data[1] + srcY * w;
        uint8_t *rowV = frame->data[2] + srcY * w;

        float srcX_float = (0.0 - offsetX) * invZoom;
        int row_idx = y * w;

        for (int x = 0; x < w; x++) {
            int srcX = (int)srcX_float;
            srcX = (srcX < 0) ? 0 : (srcX >= w ? w - 1 : srcX);
            
            int idx = row_idx + x;
            c->buf[0][idx] = rowY[srcX];
            c->buf[1][idx] = rowU[srcX];
            c->buf[2][idx] = rowV[srcX];
            srcX_float += invZoom; 
        }
    }

    for (int p = 0; p < 3; p++) {
        uint8_t *src = c->buf[p];
        uint32_t *sat = c->sat[p];
        sat[0] = src[0];
        for (int x = 1; x < w; x++) sat[x] = sat[x - 1] + src[x];
        for (int y = 1; y < h; y++) {
            uint32_t rowSum = 0;
            int cur = y * w;
            int pre = (y - 1) * w;
            for (int x = 0; x < w; x++) {
                rowSum += src[cur + x];
                sat[cur + x] = sat[pre + x] + rowSum;
            }
        }
    }

#pragma omp parallel for num_threads(c->n_threads) schedule(static)
    for (int y = 0; y < h; y++) {
        int row_off = y * w;
        for (int x = 0; x < w; x++) {
            int idx = row_off + x;
            float radius = blurAmount * c->strength_map[idx];
            if (radius > 6.0f) radius = 6.0f;

            if (radius < 0.5f) {
                frame->data[0][idx] = c->buf[0][idx];
                frame->data[1][idx] = c->buf[1][idx];
                frame->data[2][idx] = c->buf[2][idx];
                continue;
            }

            int r = (int)radius;
            int x1 = (x - r < 1) ? 0 : x - r - 1;
            int y1 = (y - r < 1) ? 0 : y - r - 1;
            int x2 = (x + r >= w) ? w - 1 : x + r;
            int y2 = (y + r >= h) ? h - 1 : y + r;

            float areaInv = 1.0f / ((x2 - x1) * (y2 - y1));

            for (int p = 0; p < 3; p++) {
                uint32_t sum = c->sat[p][y2*w+x2] - c->sat[p][y1*w+x2] - c->sat[p][y2*w+x1] + c->sat[p][y1*w+x1];
                frame->data[p][idx] = (uint8_t)(sum * areaInv + 0.5f);
            }
        }
    }
}