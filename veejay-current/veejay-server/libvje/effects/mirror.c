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
#include <limits.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>
#include "mirror.h"

vj_effect *mirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = w/2;
    ve->defaults[1] = 45;
    ve->description = "Reflection Mirror";
    ve->limits[0][0] = 0;
    ve->limits[1][0] = w;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 360;

    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->parallel = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Reflection Center", "Reflection Angle" );

    ve->hints = vje_init_value_hint_list( ve->num_params );

    return ve;
}

typedef struct {
    uint8_t *buf[3];
} mirror_t;

void *mirror_malloc(int w, int h)
{
    mirror_t *m = (mirror_t*) vj_malloc(sizeof(mirror_t));
    if(!m) {
        return NULL;
    }

    m->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 );
    if(!m->buf[0]) {
        free(m);
        return NULL;
    }

    m->buf[1] = m->buf[0] + (w*h);
    m->buf[2] = m->buf[1] + (w*h);

    return (void*) m;
}

void mirror_free(void *ptr) {
    mirror_t *m = (mirror_t*) ptr;
    if(m) {
        if(m->buf[0])
            free(m->buf[0]);
        free(m);
    }
}

void mirror_apply(void *ptr, VJFrame *frame, int *args) {

    mirror_t *m = (mirror_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict rotY = m->buf[0];
    uint8_t *restrict rotU = m->buf[1];
    uint8_t *restrict rotV = m->buf[2];

    const int reflectionCenterX = args[0];
    const int reflectionCenterY = height/2;
    const int reflectionAngle   = args[1];
    
    const float angle = (double) reflectionAngle;

    const float cosTheta = cosf( angle * M_PI / 180.0 );
    const float sinTheta = sinf( angle * M_PI / 180.0 );

    uint8_t black = pixel_Y_lo_;

    const uint8_t p = black;
    const uint8_t u = 128;
    const uint8_t v = 128;

    veejay_memcpy( rotY, srcY, frame->len );
    veejay_memcpy( rotU, srcU,frame->len);
    veejay_memcpy( rotV, srcV,frame->len);

    const int flip = ( reflectionAngle >= 180 );

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int offset_x = x - reflectionCenterX;
            const int offset_y = y - reflectionCenterY;

            const int mirroredX = reflectionCenterX - offset_x;
            const int mirroredY = reflectionCenterY - offset_y;

            const int rotatedX = 0.5f + reflectionCenterX + (mirroredX - reflectionCenterX) * cosTheta - (y - reflectionCenterY) * sinTheta;
            const int rotatedY = 0.5f + reflectionCenterY + (mirroredX - reflectionCenterX) * sinTheta + (y - reflectionCenterY) * cosTheta;

            const int inRotation = (rotatedX >= 0 && rotatedX < width && rotatedY >= 0 && rotatedY < height);

            const int isAboveTop = ( y < reflectionCenterY && rotatedY < 0);
            const int isToTheRight= flip ? ( x <= reflectionCenterX && x < rotatedX ) : ( x >= reflectionCenterX && x > rotatedX );
            const int isToTheBottom = (y > reflectionCenterY && rotatedY < 0 );

            const int index = y * width + x;
            const int rot_index = rotatedY * width + rotatedX;

            dstY[ index ] = (isAboveTop || isToTheRight || isToTheBottom ? rotY[ index ] : ( inRotation ? srcY[ rot_index ] : p ) );
            dstU[ index ] = (isAboveTop || isToTheRight || isToTheBottom ? rotU[ index ] : ( inRotation ? srcU[ rot_index ] : u ) );
            dstV[ index ] = (isAboveTop || isToTheRight || isToTheBottom ? rotV[ index ] : ( inRotation ? srcV[ rot_index ] : v ) );

        }
    }


}

