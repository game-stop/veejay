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
#include "common.h"
#include <veejaycore/vjmem.h>
#include "shutterdrag.h"

vj_effect *shutterdrag_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 3;
    ve->defaults[1] = 10;
    ve->defaults[2] = 3;
    ve->defaults[3] = 0;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 32;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 100;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 500;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Radius" , "Intensity", "Duration" , "Loop" );
    ve->description = "Shutter Drag Blocks";   
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->parallel = 0;   

    return ve;
}

typedef struct {
    uint8_t *buf[3];
    int frameCount;
    int gauss[4096];
    int radius;
    int intensity;
    float *sin_lut;
} shutterdrag_t;

void *shutterdrag_malloc(int w, int h)
{
    shutterdrag_t *s = (shutterdrag_t*) vj_calloc(sizeof(shutterdrag_t));
    if(!s) {
        return NULL;
    }
    s->buf[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->sin_lut = (float*) vj_calloc( sizeof(float) * h + 1 );
    if(!s->sin_lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + w * h;
    s->buf[2] = s->buf[1] + w * h;

    s->radius = -1;

    veejay_memset( s->buf[0], 0, w * h );
    veejay_memset( s->buf[1], 128, w * h * 2 );
    
    return (void*) s;
}

void shutterdrag_free(void *ptr) {
    shutterdrag_t *s = (shutterdrag_t*) ptr;
    free(s->buf[0]);
    free(s->sin_lut);
    free(s);
}

#define FIXED_POINT_BITS 16
#define FIXED_POINT_ONE (1 << FIXED_POINT_BITS)
#define MAX_KERNEL_VALUE 32767

void shutterdrag_apply(void *ptr, VJFrame *frame, int *args) {
    shutterdrag_t *s = (shutterdrag_t*)ptr;
    const int w = frame->width;
    const int h = frame->height;
    int radius = args[0];
    float intensity = (float)args[1] * 0.01f;
    int duration = args[2];
    int loop = args[3];
    int i;
    uint8_t *bufY = s->buf[0];
    uint8_t *bufU = s->buf[1];
    uint8_t *bufV = s->buf[2];

    float sigma = 1.0f;

    if( s->radius != radius ) {
        s->radius = radius;

        const int maxKernelValue = MAX_KERNEL_VALUE; 
        const float scaleFactor = (float)maxKernelValue / (float)s->gauss[radius];

        int sum = 0;
        for (i = -radius; i <= radius; i++) {
            s->gauss[i + radius] = (int)(scaleFactor * expf(-(i * i) / (2.0f * sigma * sigma)));
            sum += s->gauss[i + radius];
        }

        for (i = 0; i <= 2 * radius; i++) {
            s->gauss[i] = (int)((s->gauss[i] * maxKernelValue) / sum);
        }
    }

    if (s->frameCount == 0 || (duration > 0 && s->frameCount % duration == 0)) {
        veejay_memcpy(bufY, frame->data[0], w * h);
        veejay_memcpy(bufU, frame->data[1], w * h);
        veejay_memcpy(bufV, frame->data[2], w * h);
    }

    uint8_t *dstY = frame->data[0];
    uint8_t *dstU = frame->data[1];
    uint8_t *dstV = frame->data[2];

    if( s->intensity != intensity ) {
        for( int y = 0; y < h; y ++ ) {
            s->sin_lut[y] = a_sin( y * intensity );
        }
        s->intensity = intensity;
    }

    const float inv255 = 1.0f / 255.0f;
    const int shift = FIXED_POINT_BITS - 1;

    for (int y = 0; y < h; y++) {
        const float sV = radius * s->sin_lut[y];
        for (int x = 0; x < w; x++) {

            int offset = (int)( sV * (1.0f - dstY[y * w + x] * inv255));
            int l = 0, u = 0, v = 0, alpha = 0;

            for (int dy = -radius; dy <= radius; dy++) {
                int offsetY = y + dy + offset;
                offsetY = (offsetY < 0) ? (offsetY + h) : ((offsetY >= h) ? (offsetY - h) : offsetY);

                int srcIndex = offsetY * w + x;
                int weight = s->gauss[ dy + radius ];
                
                alpha += weight;

                l += bufY[srcIndex] * weight;
                u += bufU[srcIndex] * weight;
                v += bufV[srcIndex] * weight;
            }

            if (alpha > 0) {
                l = (l + (alpha >> shift)) / alpha; 
                u = (u + (alpha >> shift)) / alpha;
                v = (v + (alpha >> shift)) / alpha;
            }

            int dstIndex = y * w + x;
            dstY[dstIndex] = (uint8_t)l;
            dstU[dstIndex] = (uint8_t)u;
            dstV[dstIndex] = (uint8_t)v;
        }

    }
    
    veejay_memcpy(bufY, dstY, w * h);
    veejay_memcpy(bufU, dstU, w * h);
    veejay_memcpy(bufV, dstV, w * h);

    s->frameCount++;

    if (duration > 0 && s->frameCount >= duration && !loop) {
        s->frameCount = 0;
    }


}

