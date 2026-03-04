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
#include "flashopacity.h"
#ifdef HAVE_ARM
#include <arm_neon.h>
#endif

vj_effect *flashopacity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 1;
    ve->limits[1][3] = 500;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 1;

    ve->defaults[0] = 5;
    ve->defaults[1] = 100;
    ve->defaults[2] = 255;
    ve->defaults[3] = 10;
    ve->defaults[4] = 0;

    ve->description = "Flash Opacity";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Exposure", "Start Opacity", "End Opacity" , "Interval", "Mode"); 
    return ve;
}

#define TABLE_SIZE 256

typedef struct
{
   int currentFrame;
   int exposure;
   float maxExposure;
   float explut[TABLE_SIZE];
} flash_t;


void *flashopacity_malloc( int w, int h )
{
    flash_t *f = (flash_t*) vj_malloc(sizeof(flash_t));
    if(!f)
        return NULL;
    f->exposure = 0.0f;
    f->currentFrame = 0;
    f->maxExposure = 100.0f;
    return (void*) f;
}

void flashopacity_free(void *ptr) {
    flash_t *f = (flash_t*) ptr;
    free(f);
}


static inline int32_t min_int(int32_t a, int32_t b) {
    return b + ((a - b) & ((a - b) >> 31));
}

static inline int32_t max_int(int32_t a, int32_t b) {
    return a - ((a - b) & ((a - b) >> 31));
}

void flashopacity_apply( void *ptr,  VJFrame *frame, VJFrame *frame2, int *args )
{
    flash_t *f = (flash_t*) ptr;
    const int len = frame->len;
    const int uv_len = frame->uv_len;

    const float exposureValue = (float) args[0] / 100.0f;
    const int opacityStart = args[1];
    const int opacityEnd = args[2];
    const int interval = args[3];
    const int mode = args[4];

    const int hInterval = interval / 2;
    int currentFrame = f->currentFrame;

    if (f->maxExposure != exposureValue) {
        for (int i = 0; i < TABLE_SIZE; i++) {
            float exposureFactor = (float)i / (float)(TABLE_SIZE - 1) * exposureValue;
            f->explut[i] = (int)(powf(2, exposureFactor) * 256.0f);
        }
        f->maxExposure = exposureValue;
    }

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict U2 = frame2->data[1];
    uint8_t *restrict V2 = frame2->data[2];

    if (currentFrame < hInterval) {
        float ratio = (float)currentFrame / (float)hInterval;
        int index = (int)(ratio * (TABLE_SIZE - 1));
        int fp_multiplier = (int)f->explut[index];
        
        #pragma omp simd
        for (int i = 0; i < len; i++) {
            int val = (Y[i] * fp_multiplier) >> 8;
            Y[i] = (uint8_t)min_int(val, 255);
        }

        if (mode == 1) {
            int lerp_fp = (currentFrame << 8) / hInterval; 
           
#pragma omp simd
            for (int i = 0; i < uv_len; i++) {
                // 1. Load as signed to avoid unsigned underflow wrap-around
                int u1 = (int)U[i];
                int v1 = (int)V[i];
                int u2 = (int)U2[i];
                int v2 = (int)V2[i];

                // 2. Linear interpolation: start + ((end - start) * factor) >> 8
                int resU = u1 + (((u2 - u1) * lerp_fp) >> 8);
                int resV = v1 + (((v2 - v1) * lerp_fp) >> 8);

                // 3. Branchless saturation to [0, 255]
                U[i] = (uint8_t)max_int(0, min_int(resU, 255));
                V[i] = (uint8_t)max_int(0, min_int(resV, 255));
            }
        }
    } 
    else {
        int t = currentFrame - hInterval;
        int opacity = opacityStart + (t * (opacityEnd - opacityStart)) / (interval - hInterval);
        int inv_opacity = 0xff - opacity;

        #pragma omp simd
        for (int i = 0; i < len; i++) {
            Y[i] = (Y[i] * inv_opacity + Y2[i] * opacity) >> 8;
        }
        #pragma omp simd
        for (int i = 0; i < uv_len; i++) {
            U[i] = (U[i] * inv_opacity + U2[i] * opacity) >> 8;
            V[i] = (V[i] * inv_opacity + V2[i] * opacity) >> 8;
        }
    }

    f->currentFrame = (currentFrame + 1) % interval;
}

