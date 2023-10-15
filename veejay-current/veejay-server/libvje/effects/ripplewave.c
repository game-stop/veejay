/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "ripplewave.h"

vj_effect *ripplewave_init(int w, int h) {
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;     
    ve->limits[1][0] = 100;
    ve->defaults[0] = 10; 
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 100;
    ve->defaults[1] = 15;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 45; 
    ve->defaults[2] = 30;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 100;
    ve->defaults[3] = 10;

    ve->description = "Wave Patterns (H/V)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Frequency X", "Frequency Y", "Amplitude", "Speed" );

    return ve;
}

typedef struct {
    uint8_t *buf[3];
    float *lut_x;
    float *lut_y;
    int width;
    int height;
    float factor;
    float speed;
    int deformX;
    int deformY;
} ripplewave_t;

#define SIN_TABLE_SIZE 360
void* ripplewave_malloc(int w, int h) {
    ripplewave_t *data = (ripplewave_t*) vj_malloc(sizeof(ripplewave_t));
    if (!data)
        return NULL;
    data->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3);
    if(!data->buf[0]) {
        free(data);
        return NULL;
    }
    data->lut_x = (float*) vj_malloc(sizeof(float) * w);
    if(!data->lut_x) {
        free(data->buf[0]);
        free(data);
        return NULL;
    }
    data->lut_y = (float*) vj_malloc(sizeof(float) * h);
    if(!data->lut_y) {
        free(data->buf[0]);
        free(data->lut_x);
        free(data);
        return NULL;
    }

    data->buf[1] = data->buf[0] + (w*h);
    data->buf[2] = data->buf[1] + (w*h);

    data->width = w;
    data->height = h;
    data->factor = 10.0;
    data->speed = 1.0;
    data->deformX = 1;
    data->deformY = 1;
    
    return data;
}

void ripplewave_free(void *ptr) {
    ripplewave_t *data = (ripplewave_t*) ptr;
    if (data != NULL) {
        free(data->buf[0]);
        free(data->lut_x);
        free(data->lut_y);
        free(data);
    }
}

void ripplewave_apply(void *ptr, VJFrame *frame, int *args) {
    ripplewave_t *data = (ripplewave_t*)ptr;

    int width = frame->width;
    int height = frame->height;

    int x, y;
    
    float frequencyX = args[0] * 0.01f;
    float frequencyY = args[1] * 0.01f;
    float amplitude  = args[2];
    float speed      = args[3] * 0.1f;
    
    data->speed += 0.1f;
    if( data->speed > speed ) {
           data->speed = 1.0f;
    }

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    uint8_t *dstY = data->buf[0] + frame->offset;
    uint8_t *dstU = data->buf[1] + frame->offset;
    uint8_t *dstV = data->buf[2] + frame->offset;

    float *lut_x = data->lut_x;
    float *lut_y = data->lut_y;

    for( y = 0; y < height; y ++ ) {
        lut_y[y] = a_sin( frequencyY * y + data->speed );
    }

    for( x = 0; x < width; x ++ ) {
        lut_x[x] = a_cos( frequencyX * x + data->speed );
    }

    for (y = 0; y < height; y++) {
        float offset_y = amplitude * lut_y[y];
        for (x = 0; x < width; x++) {
            float offset_x = amplitude * lut_x[x];

            int srcX = (int) ( x + offset_x ) % width; 
            int srcY = (int) ( y + offset_y ) % height;

            srcX = (srcX < 0) ? (width + srcX) : srcX;
            srcY = (srcY < 0) ? (height + srcY) : srcY;

            int srcIndex = srcY * width + srcX;
            int dstIndex = y * width + x;

            dstY[dstIndex] = Y[srcIndex];
            dstU[dstIndex] = U[srcIndex];
            dstV[dstIndex] = V[srcIndex];
        }
    }

    veejay_memcpy( Y, dstY, frame->len );
    veejay_memcpy( U, dstU, frame->len );
    veejay_memcpy( V, dstV, frame->len );

}


