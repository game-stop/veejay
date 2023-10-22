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

    ve->limits[0][3] = 0;
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
    const int width = frame->width;

    int currentFrame = f->currentFrame;

    int i;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict U2 = frame2->data[1];
    uint8_t *restrict V2 = frame2->data[2];

    if(f->maxExposure != exposureValue) {
        int i;
        for( i = 0; i < TABLE_SIZE; i ++ ) {
            float exposureFactor = (float)i / (float)(TABLE_SIZE - 1) * exposureValue;
            f->explut[i] = powf(2, exposureFactor);
        }
        f->maxExposure = exposureValue;
    }

    if( currentFrame < hInterval ) {

        float exposureFactor = exposureValue * (currentFrame / (float)hInterval);
    
        int index = (int)(exposureFactor / exposureValue * (TABLE_SIZE - 1));
        float powValue = f->explut[index];
        
        for (int i = 0; i < len; i++) {
              Y[i] = (uint8_t)(Y[i] * powValue > 255 ? 255 : (Y[i] * powValue));
        }

        if (mode == 1) {
            for (int i = 0; i < uv_len; i++) {
                int distanceToInitialU = abs(U2[i] - U[i]);
                int uValue = U[i] + (distanceToInitialU * currentFrame) / hInterval;
                U[i] = (uValue > 128 ? 128 : uValue);
 
                int distanceToInitialV = abs(V2[i] - V[i]);
                int vValue = V[i] + (distanceToInitialV * currentFrame) / hInterval;
                V[i] = (vValue > 128 ? 128 : vValue);
            }
        }
    }
    else if ( currentFrame >= hInterval && currentFrame < interval) {
        int opacity = opacityStart + (currentFrame - hInterval) * (opacityEnd - opacityStart) / hInterval;

        for (i = 0; i < len; i++)
        {
            Y[i] = (Y[i] * (0xff - opacity) + Y2[i] * opacity) / 0xff;
        }
        for( i = 0; i < uv_len; i ++ ) 
        {
            U[i] = (U[i] * (0xff - opacity) + U2[i] * opacity) / 0xff;
            V[i] = (V[i] * (0xff - opacity) + V2[i] * opacity) / 0xff;
        }
    }

    f->currentFrame = (f->currentFrame + 1) % interval;
}

