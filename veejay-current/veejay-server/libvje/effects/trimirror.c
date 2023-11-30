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
#include "trimirror.h"

#define MAX_SEGMENTS 256

vj_effect *trimirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = MAX_SEGMENTS;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[0] = 1;
    ve->defaults[1] = 255;

    ve->description = "Kaleidoscope";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 2; // thread local buf mode
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Segments", "Normalization Factor"  );
    return ve;
}

typedef struct 
{
    uint8_t *buf[3];
    float *lut;
    float *atan2_lut;
    float *cos_lut;
    float *sqrt_lut;
    float *sin_lut;
} trimirror_t;


static void init_atan2_lut(trimirror_t *f, int w, int h, int cx, int cy)
{
    for(int x = 0; x < w; ++x ) {
        for(int y = 0; y < h; ++y ) {
            float angle = atan2f(y - cy,x - cx);
            f->atan2_lut[ y * w + x ] = angle;
        }
    }
}

static void init_sqrt_lut(trimirror_t *f, int w, int h, int cx, int cy)
{
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            int dx = x - cx;
            int dy = y - cy;
            f->sqrt_lut[y * w + x] = sqrtf( dx * dx + dy * dy );
        }
    }
}

static void init_sin_cos_lut(trimirror_t *f, int w, int h ) {
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            float angle = f->atan2_lut[y * w + x];
            f->sin_lut[y * w + x] = sin(angle);
            f->cos_lut[y * w + x] = cos(angle);
        }
    }
}

void *trimirror_malloc(int w, int h) {
    trimirror_t *s = (trimirror_t*) vj_calloc(sizeof(trimirror_t));
    if(!s) return NULL;

    // required for non threading because of inplace operations
    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + ( w * h );
    s->buf[2] = s->buf[1] + ( w * h );

    s->lut = (float*) vj_malloc(sizeof(float) * (w*h*4) );
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->atan2_lut = s->lut;
    s->sqrt_lut = s->atan2_lut + (w*h);
    s->cos_lut = s->sqrt_lut + (w*h);
    s->sin_lut = s->cos_lut + (w*h);

    init_atan2_lut( s, w, h, w/2, h/2 );
    init_sqrt_lut( s, w, h,w/2, h/2 );
    init_sin_cos_lut( s, w,h );

    return (void*) s;
}

void trimirror_free(void *ptr) {
    trimirror_t *s = (trimirror_t*) ptr;
    free(s->lut);
    free(s->buf[0]);
    free(s);
}

void trimirror_apply(void *ptr, VJFrame *frame, int *args) {
    trimirror_t *s = (trimirror_t*)ptr;
    const int numSegments = args[0];
    const uint8_t normFactor = args[1];

    const int width = frame->out_width;
    const int height = frame->out_height;

    const int w = frame->width;
    const int h = frame->height;

    uint8_t *restrict srcY = frame->data[0] - frame->offset;
    uint8_t *restrict srcU = frame->data[1] - frame->offset;
    uint8_t *restrict srcV = frame->data[2] - frame->offset;
    
    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    float *restrict sqrt_lut = s->sqrt_lut;
    float *restrict cos_lut = s->cos_lut;
    float *restrict sin_lut = s->sin_lut;


    const int centerX = width >> 1;
    const int centerY = height >> 1;

    const int maxSum = 255 * numSegments;

    float sinSegments[ numSegments ];
    float cosSegments[ numSegments ];
    
    uint8_t *outY;
    uint8_t *outU;
    uint8_t *outV;

    if( vje_setup_local_bufs( 1, frame, &outY, &outU, &outV, NULL ) == 0 ) {
        const int len = width * height;
        veejay_memcpy( bufY, srcY, len );
        veejay_memcpy( bufU, srcU, len );
        veejay_memcpy( bufV, srcV, len );

        srcY = bufY;
        srcU = bufU;
        srcV = bufV;
    }

    for (int i = 0; i < numSegments; ++i) {
        float segmentAngle = i * (2 * M_PI) / numSegments;
        sinSegments[i] = a_sin(segmentAngle);
        cosSegments[i] = a_cos(segmentAngle);
    }

    int mX[ numSegments ];
    int mY[ numSegments ];

    int start = frame->jobnum * h;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int mirroredValueY = 0;
            int mirroredValueU = 0;
            int mirroredValueV = 0;

            const float distance = sqrt_lut[ (start + y) * width + x];
            const float sinValue = sin_lut[ (start + y) * width + x];
            const float cosValue = cos_lut[ (start + y) * width + x];

            // start computing the contribution of each segment to the final pixel value

            // cache the coordinates in an auto-vectorizable loop
            for (int i = 0; i < numSegments; i++) {
                float sinSegment = sinSegments[i];
                float cosSegment = cosSegments[i];
                float sinSum = sinValue * cosSegment + cosValue * sinSegment;
                float cosSum = cosValue * cosSegment - sinValue * sinSegment;
                int mirrorX = centerX + (int)(distance * cosSum);
                int mirrorY = centerY + (int)(distance * sinSum); 

                
                mirrorX = (mirrorX >= 0 && mirrorX < width) ? mirrorX : centerX;
                mirrorY = (mirrorY >= 0 && mirrorY < height) ? mirrorY : centerY;

                mX[i] = mirrorX;
                mY[i] = mirrorY;
            }

            // memory access is not continious
            for (int i = 0; i < numSegments; i++) {
                int mirrorX = mX[i];
                int mirrorY = mY[i];

                mirroredValueY += srcY[mirrorY * width + mirrorX];
                mirroredValueU += (srcU[mirrorY * width + mirrorX] - 128);
                mirroredValueV += (srcV[mirrorY * width + mirrorX] - 128);
            }

            // normalized additive blend
            mirroredValueY = (mirroredValueY * normFactor) / maxSum;
            mirroredValueU = (mirroredValueU * normFactor) / maxSum;
            mirroredValueV = (mirroredValueV * normFactor) / maxSum;

            outY[y * w + x] = mirroredValueY;
            outU[y * w + x] = 128 + mirroredValueU;
            outV[y * w + x] = 128 + mirroredValueV;
        }
    }


}


