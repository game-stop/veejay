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
#include "fragmenttv.h"

#define MAX_FRAGMENTS 16384
#define LUT_SIZE 3600
#define TWO_PI 6.28318530718f

vj_effect *fragmenttv_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 14;
    ve->defaults[1] = 2;
    ve->defaults[2] = 45;
    ve->defaults[3] = 220;
    ve->defaults[4] = 0;
    ve->description = "FragmentTV";
    ve->limits[0][0] = 10;
    ve->limits[1][0] = 100;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = (w < h ? w : h) / 4;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = (w < h ? w : h) / 4;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1000;
    ve->limits[0][4] = 0;
    ve->limits[1][4] = 500;
    
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->parallel = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Grid Size", "Minimum Size", "Maximum Size", "Displacement", "Interval" );

    ve->hints = vje_init_value_hint_list( ve->num_params );

    return ve;
}

typedef struct {
    float *sin_lut;
    float *cos_lut;
    float distortion;
    uint8_t *buf[3];
    int *randbuf;
    int count;
} fragmenttv_t;

static void init_sin_lut(fragmenttv_t *m) {
    for(int i = 0; i < LUT_SIZE; i ++ ) {               
        float angle = i * (TWO_PI / LUT_SIZE);
        m->sin_lut[i] = sinf(angle);
        m->cos_lut[i] = cosf(angle);
    }
}

static void init_rand_lut(fragmenttv_t *m) {
    for( int i = 0; i < MAX_FRAGMENTS; i ++ ) {
        m->randbuf[i] = rand();
    }

}

void *fragmenttv_malloc(int w, int h)
{
    fragmenttv_t *m = (fragmenttv_t*) vj_malloc(sizeof(fragmenttv_t));
    if(!m) {
        return NULL;
    }
    m->sin_lut = (float*) vj_malloc( sizeof(float) * LUT_SIZE * 2 );
    if(!m->sin_lut) {
        free(m);
        return NULL;
    }
    m->cos_lut = m->sin_lut + LUT_SIZE;

    m->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 );
    if(!m->buf[0]) {
        free(m->sin_lut);
        free(m);
        return NULL;
    }

    m->randbuf = (int*) vj_malloc(sizeof(int) * MAX_FRAGMENTS );
    if(!m->randbuf) {
        free(m->buf[0]);
        free(m->sin_lut);
        free(m);
        return NULL;
    }

    m->buf[1] = m->buf[0] + (w*h);
    m->buf[2] = m->buf[1] + (w*h);

    m->count = 0;


    init_sin_lut(m);
    init_rand_lut(m);

    return (void*) m;
}

void fragmenttv_free(void *ptr) {
    fragmenttv_t *m = (fragmenttv_t*) ptr;
    if(m) {
        if(m->sin_lut)
            free(m->sin_lut);
        if(m->buf[0])
            free(m->buf[0]);
        if(m->randbuf)
            free(m->randbuf);
        free(m);
    }
}

typedef struct {
    int x, y;
} Point;

static inline void rotatePoint(int x, int y, int cx, int cy, float theta, int *newX, int *newY, fragmenttv_t *m) {
    int lutpos = (int)(theta * LUT_SIZE / TWO_PI) % LUT_SIZE;

    float cosTheta = m->cos_lut[ lutpos ];
    float sinTheta = m->sin_lut[ lutpos ];

    *newX = cx + (x - cx) * cosTheta - (y - cy) * sinTheta;
    *newY = cy + (x - cx) * sinTheta + (y - cy) * cosTheta;
}

void drawGridTriangles(fragmenttv_t *m, uint8_t *dstY, uint8_t *dstU, uint8_t *dstV, uint8_t *srcY, uint8_t *srcU, uint8_t *srcV, int width, int height, const int gridSize, const int minSize, const int maxSize, const float displacementWeight) {
    const int numTrianglesX = width / gridSize;
    const int numTrianglesY = height / gridSize;

    int *restrict rand_lut = m->randbuf; 
    int  rand_lut_pos = 0;
    int  rotatedX, rotatedY;
    int  triangleSize;
    float angle;

    for (int i = 0; i < numTrianglesX; ++i) {
        for (int j = 0; j < numTrianglesY; ++j) {
            triangleSize = minSize + rand_lut[rand_lut_pos] % (maxSize - minSize);

            Point p1 = {i * gridSize, j * gridSize};
            Point p2 = {p1.x + triangleSize, p1.y};
            Point p3 = {p1.x, p1.y + triangleSize};

            rand_lut_pos = (rand_lut_pos + 1 ) % MAX_FRAGMENTS;
            angle = (float) rand_lut[ rand_lut_pos ] / RAND_MAX * TWO_PI;

            rotatePoint(p1.x, p1.y, p2.x, p2.y, angle, &rotatedX, &rotatedY, m);
            p2.x = rotatedX;
            p2.y = rotatedY;

            rotatePoint(p1.x, p1.y, p3.x, p3.y, angle, &rotatedX, &rotatedY, m);
            p3.x = rotatedX;
            p3.y = rotatedY;

            for (int y = p1.y; y <= p3.y; ++y) {
                for (int x = p1.x; x <= p2.x; ++x) {
                    if (x <= width && y <= height) {
                        int sign1 = (p2.x - p1.x) * (y - p1.y) - (x - p1.x) * (p2.y - p1.y);
                        int sign2 = (p3.x - p2.x) * (y - p2.y) - (x - p2.x) * (p3.y - p2.y);
                        int sign3 = (p1.x - p3.x) * (y - p3.y) - (x - p3.x) * (p1.y - p3.y);

                        int insideTriangle = (sign1 >= 0) && (sign2 >= 0) && (sign3 >= 0);

                        if (insideTriangle) {
                            float displacementX = (p2.x - p1.x != 0) ? m->sin_lut[(x - p1.x) * LUT_SIZE / (p2.x - p1.x)] * displacementWeight : 0;
                            float displacementY = (p3.y - p1.y != 0) ? m->sin_lut[(y - p1.y) * LUT_SIZE / (p3.y - p1.y)] * displacementWeight : 0;

                            int mirroredX = width - x - 1 + displacementX;
                            int mirroredY = y + displacementY;

                            mirroredX = mirroredX >= 0 ? mirroredX : 0;
                            mirroredX = mirroredX < width ? mirroredX : width - 1;
                            mirroredY = mirroredY >= 0 ? mirroredY : 0;
                            mirroredY = mirroredY < height ? mirroredY : height - 1;
                            
                            int index = y * width + x;
                            int mirroredIndex = mirroredY * width + mirroredX;

                            dstY[index] = (dstY[index] + srcY[mirroredIndex]) >> 1;
                            dstU[index] = (dstU[index] + srcU[mirroredIndex]) >> 1;
                            dstV[index] = (dstV[index] + srcV[mirroredIndex]) >> 1;
                        }
                    }
                }
            }
        }
    }

}


void fragmenttv_apply(void *ptr, VJFrame *frame, int *args) {
    fragmenttv_t *m = (fragmenttv_t *)ptr;

    const int width = frame->width;
    const int height = frame->height;

    const int numFragments = args[0];
    int minSize = args[1];
    int maxSize = args[2];
    const float displacementWeight = args[3] * 0.1f;
    const int interval = args[4];   

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = m->buf[0];
    uint8_t *restrict bufU = m->buf[1];
    uint8_t *restrict bufV = m->buf[2];

    uint8_t *outY;
    uint8_t *outU;
    uint8_t *outV;

    if( vje_setup_local_bufs( 1, frame, &outY, &outU, &outV, NULL ) == 0 ) {
        veejay_memcpy( bufY, srcY, frame->len );
        veejay_memcpy( bufU, srcU, frame->len );
        veejay_memcpy( bufV, srcV, frame->len );

        srcY = bufY;
        srcU = bufU;
        srcV = bufV;
    }
    else {
        veejay_memcpy( outY, srcY, frame->len );
        veejay_memcpy( outU, srcU, frame->len );
        veejay_memcpy( outV, srcV, frame->len );
    }

    if( minSize == maxSize )
        maxSize = minSize + 1;

    drawGridTriangles(m, outY, outU, outV, srcY, srcU, srcV, width, height, numFragments, minSize, maxSize, displacementWeight);

    if(interval == 0)
        return;

    m->count = (m->count + 1) % interval;
    if(m->count == 0) {
        init_rand_lut(m);
    }
    
}

