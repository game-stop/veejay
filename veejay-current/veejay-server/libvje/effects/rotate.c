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
#include "rotate.h"

typedef struct {
    uint8_t *buf[4];
    float sin_lut[360];
    float cos_lut[360];
    double rotate;
    int frameCount;
    int direction;
} rotate_t;

vj_effect *rotate_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->defaults[2] = 100;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 360;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 1500;
    ve->description = "Rotate";

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Rotate", "Automatic", "Duration");
    ve->has_user = 0;

    return ve;
}

void *rotate_malloc(int width, int height)
{
    int i;
    rotate_t *r = (rotate_t*) vj_calloc( sizeof(rotate_t) );
    if(!r) {
        return NULL;
    }

    r->buf[0] = (uint8_t *) vj_calloc(sizeof(uint8_t) * (width * height * 3));
    if(!r->buf[0]) {
        free(r);
        return NULL;
    }

    r->buf[1] = r->buf[0] + (width * height);
    r->buf[2] = r->buf[1] + (width * height);

    r->direction = 1;

    for( i = 0; i < 360; i ++ ) {
        r->sin_lut[i] = a_sin( i * M_PI / 180.0 );
        r->cos_lut[i] = a_cos( i * M_PI / 180.0 );
    }

    return (void*) r;
}

void rotate_free(void *ptr) {

    rotate_t *r = (rotate_t*) ptr;

    if(r->buf[0])
        free(r->buf[0]);

    free(r);
}


void rotate_apply( void *ptr, VJFrame *frame, int *args )
{
    rotate_t *r = (rotate_t*) ptr;

    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;
    
    double rotate = args[0];
    int autom = args[1];
    int maxFrames = args[2];

    if( autom ) {
        rotate = r->rotate;

        r->rotate += (r->direction * (360.0 / maxFrames));

        r->frameCount ++;

        if( r->frameCount % maxFrames == 0 || (r->rotate <= 0 || r->rotate >= 360)) {
            r->direction *= -1;
            r->frameCount = 0;
        }
    }

    uint8_t *dstY = frame->data[0];
    uint8_t *dstU = frame->data[1];
    uint8_t *dstV = frame->data[2];

    uint8_t *srcY = r->buf[0];
    uint8_t *srcU = r->buf[1];
    uint8_t *srcV = r->buf[2];

    veejay_memcpy( r->buf[0], frame->data[0], frame->len );
    veejay_memcpy( r->buf[1], frame->data[1], frame->len );
    veejay_memcpy( r->buf[2], frame->data[2], frame->len );

    const int centerX = width / 2;
    const int centerY = height / 2;

    float *cos_lut = r->cos_lut;
    float *sin_lut = r->sin_lut;

    int rotate_angle = (int)rotate % 360;
    float cos_val = cos_lut[rotate_angle];
    float sin_val = sin_lut[rotate_angle];


    for (int y = 0; y < height; ++y) {
#pragma omp simd
        for (int x = 0; x < width; ++x) {

            int rotatedX = (int)((x - centerX) * cos_val - (y - centerY) * sin_val + centerX);
            int rotatedY = (int)((x - centerX) * sin_val + (y - centerY) * cos_val + centerY);

            int newX = (int)((rotatedX - centerX) + centerX);
            int newY = (int)((rotatedY - centerY) + centerY);

            newX = (newX < 0) ? 0 : ((newX > width - 1) ? width - 1 : newX);
            newY = (newY < 0) ? 0 : ((newY > height - 1) ? height - 1 : newY);

            int srcIndex = newY * width + newX;
            int dstIndex = y * width + x;

            dstY[dstIndex] = srcY[srcIndex];
            dstU[dstIndex] = srcU[srcIndex];
            dstV[dstIndex] = srcV[srcIndex];
        }
    }

}
