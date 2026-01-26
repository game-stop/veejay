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

/*
 * Look-up table optimized wave filter originally found in the MLT framework modules/kdenlive:
 */

/*
 * wave.c -- wave filter
 * Copyright (C) ?-2007 Leny Grisel <leny.grisel@laposte.net>
 * Copyright (C) 2007 Jean-Baptiste Mardelle <jb@ader.ch>
 * Copyright (c) 2022 Meltytech, LLC
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "wave.h"
vj_effect *wave_init(int w, int h) {
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;  //factor
    ve->defaults[0] = 10; 
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 100; //speed
    ve->defaults[1] = 1;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1; //deformX on/off
    ve->defaults[2] = 1;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1; //deformY on/off
    ve->defaults[3] = 1;

    ve->description = "Wave";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Factor", "Speed", "DeformX", "DeformY" );

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
} wave_t;

#define SIN_TABLE_SIZE 360
void* wave_malloc(int w, int h) {
    wave_t *data = (wave_t*) vj_malloc(sizeof(wave_t));
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

void wave_free(void *ptr) {
    wave_t *data = (wave_t*) ptr;
    if (data != NULL) {
        free(data->buf[0]);
        free(data->lut_x);
        free(data->lut_y);
        free(data);
    }
}

void wave_apply(void *ptr, VJFrame *frame, int *args) {
    wave_t *data = (wave_t*)ptr;

    int width = frame->width;
    int height = frame->height;

    int x, y;
    int decalY, decalX;
    float amplitude, phase, pulsation;

    float factor = args[0] * 0.1f;
    float speed = args[1] * 0.1f;
    
    int deformX = args[2];
    int deformY = args[3];

    amplitude = factor;
    pulsation = 0.5 / factor;

    data->speed += 0.1f;
    if( data->speed > speed ) {
           data->speed = 1.0f;
    }

    phase = factor * pulsation * data->speed / 10;

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    uint8_t *dstY = data->buf[0] + frame->offset;
    uint8_t *dstU = data->buf[1] + frame->offset;
    uint8_t *dstV = data->buf[2] + frame->offset;

    float *lut_x = data->lut_x;
    float *lut_y = data->lut_y;

    for( y = 0; y < height; y ++ ) {
        lut_y[y] = deformX ? a_sin( pulsation * y + phase + data->speed ) * amplitude : 0.0f;
    }

    for( x = 0; x < width; x ++ ) {
        lut_x[x] = deformY ? a_sin( pulsation * x * 2 + phase + data->speed ) * amplitude : 0.0f;
    }

    for (y = 0; y < height; y++) {
        decalX = lut_y[y];
        for (x = 0; x < width; x++) {
            decalY = lut_x[x];

            int srcX = ( x + decalX ); 
            int srcY = ( y + decalY );

            srcX = (srcX < 0) ? 0 : ((srcX >= width) ? width - 1 : srcX);
            srcY = (srcY < 0) ? 0 : ((srcY >= height) ? height - 1 : srcY);

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


