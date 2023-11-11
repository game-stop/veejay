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
#include "smartblur.h"

vj_effect *smartblur_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 16;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->defaults[0] = 1;
    ve->defaults[1] = 10;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->description = "Smart Blur";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Threshold", "Swap", "Show Mask" );
    return ve;
}

typedef struct {
    uint8_t *buf[3];
    uint8_t *mask;
} smartblur_t;


void *smartblur_malloc(int w, int h) {
    smartblur_t *s = (smartblur_t*) vj_malloc( sizeof(smartblur_t) );
    if(!s) {
        return NULL;
    }
    s->buf[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + (w*h);
    s->buf[2] = s->buf[1] + (w*h);

    s->mask = (uint8_t*) vj_calloc(sizeof(uint8_t) * w * h );
    if(!s->mask) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    return (void*) s;
}

void smartblur_free(void *ptr) {
    smartblur_t *s = (smartblur_t*) ptr;
    free(s->buf[0]);
    free(s->mask);
    free(s);
}

void smartblur_apply(void *ptr, VJFrame *frame, int *args) {
    smartblur_t *s = (smartblur_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;
    const int radius = args[0];
    const int threshold = args[1];
    const int show = args[3];
    const int swap = args[2];
    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];
    uint8_t *restrict dstY = s->buf[0];
    uint8_t *restrict dstU = s->buf[1];
    uint8_t *restrict dstV = s->buf[2];
    uint8_t *restrict mask = s->mask;        

    const int minMaskValue = (swap ? 0xff: 0);
    const int maxMaskValue = (swap ? 0: 0xff);

    for (int y = radius; y < h - radius; y++) {
        for (int x = radius; x < w - radius; x++) {
            uint8_t maskValue = minMaskValue;
            for (int ky = -radius; ky <= radius; ky++) {
#pragma omp simd
                for (int kx = -radius; kx <= radius; kx++) {
                    const int intensity_diff = srcY[y * w + x] - srcY[ky * w + kx];
                    const int abs_diff = (intensity_diff >= 0) ? intensity_diff : -intensity_diff;
                    maskValue |= (maxMaskValue & ((threshold - abs_diff) >> 8));
                }
            }
            mask[y * w + x] = maskValue;
        }
    }

    if(show) {
        veejay_memcpy( srcY, mask, frame->len );
        veejay_memset( srcU, 128, frame->len );
        veejay_memset( srcV, 128, frame->len );
        return;
    }

    for (int y = radius; y < h - radius; y++) {
        for (int x = radius; x < w - radius; x++) {
            int ySum = 0;
            int uSum = 0;
            int vSum = 0;
            int pixelCount = 0;
            for (int ky = -radius; ky <= radius; ky++) {
#pragma omp simd
                for( int kx = -radius; kx <= radius; kx ++ ) {
                    const int maskValue = mask[(y + ky) * w + (x + kx)];
                    const int maskMultiplier = maskValue & 1;

                    ySum += maskMultiplier * srcY[(y + ky) * w + (x + kx)];
                    uSum += maskMultiplier * (srcU[(y + ky) * w + (x + kx)]);
                    vSum += maskMultiplier * (srcV[(y + ky) * w + (x + kx)]);
                    pixelCount += maskMultiplier;
                }
            }
           
            dstY[y * w + x] = (pixelCount == 0 ? srcY[ y * w + x ] : (uint8_t)( ySum / pixelCount ));
            dstU[y * w + x] = (pixelCount == 0 ? srcU[ y * w + x ] : (uint8_t)( uSum / pixelCount ));
            dstV[y * w + x] = (pixelCount == 0 ? srcV[ y * w + x ] : (uint8_t)( vSum / pixelCount ));
            
        }
    }
    veejay_memcpy(srcY, dstY, frame->len);
    veejay_memcpy(srcU, dstU, frame->len);
    veejay_memcpy(srcV, dstV, frame->len);
}

