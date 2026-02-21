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

#define SIN_LUT_SCALE 1024 

typedef struct {
    uint8_t *buf[3];
    int *randbuf;
    int count;
    int16_t *sin_lut;
    int16_t *cos_lut_int;
    float distortion;
} fragmenttv_t;

static void init_sin_lut(fragmenttv_t *m) {
    for (int i = 0; i < LUT_SIZE; i++) {
        float angle = i * (TWO_PI / LUT_SIZE);
        m->sin_lut[i] = (int16_t)(sinf(angle));
        m->cos_lut_int[i] = (int16_t)(cosf(angle));
    }
}

static void init_rand_lut(fragmenttv_t *m) {
    for (int i = 0; i < MAX_FRAGMENTS; i++) {
        m->randbuf[i] = rand();
    }
}

void *fragmenttv_malloc(int w, int h) {
    fragmenttv_t *m = (fragmenttv_t*) vj_malloc(sizeof(fragmenttv_t));
    if (!m) return NULL;

    m->sin_lut = (int16_t*) vj_malloc(sizeof(int16_t) * LUT_SIZE);
    if (!m->sin_lut) { free(m); return NULL; }

    m->cos_lut_int = (int16_t*) vj_malloc(sizeof(int16_t) * LUT_SIZE);
    if (!m->cos_lut_int) { free(m->sin_lut); free(m); return NULL; }

    m->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h);
    m->buf[1] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h);
    m->buf[2] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h);
    if (!m->buf[0] || !m->buf[1] || !m->buf[2]) {
        if (m->buf[0]) free(m->buf[0]);
        if (m->buf[1]) free(m->buf[1]);
        if (m->buf[2]) free(m->buf[2]);
        free(m->cos_lut_int);
        free(m->sin_lut);
        free(m);
        return NULL;
    }

    m->randbuf = (int*) vj_malloc(sizeof(int) * MAX_FRAGMENTS);
    if (!m->randbuf) {
        free(m->buf[0]);
        free(m->buf[1]);
        free(m->buf[2]);
        free(m->cos_lut_int);
        free(m->sin_lut);
        free(m);
        return NULL;
    }

    m->count = 0;

    init_sin_lut(m);
    init_rand_lut(m);

    return (void*) m;
}

void fragmenttv_free(void *ptr) {
    if (!ptr) return;

    fragmenttv_t *m = (fragmenttv_t*) ptr;

    if (m->sin_lut) free(m->sin_lut);
    if (m->cos_lut_int) free(m->cos_lut_int);

    if (m->buf[0]) free(m->buf[0]);
    if (m->buf[1]) free(m->buf[1]);
    if (m->buf[2]) free(m->buf[2]);

    if (m->randbuf) free(m->randbuf);

    free(m);
}

typedef struct {
    int x, y;
} Point;

static inline void rotatePoint(int x, int y, int cx, int cy, float theta, int *newX, int *newY, fragmenttv_t *m) {
    int lutpos = (int)(theta * LUT_SIZE / TWO_PI);

    if (lutpos < 0) lutpos = (lutpos % LUT_SIZE + LUT_SIZE) % LUT_SIZE;
    else if (lutpos >= LUT_SIZE) lutpos = lutpos % LUT_SIZE;

    float cosTheta = m->cos_lut_int[lutpos];
    float sinTheta = m->sin_lut[lutpos];

    *newX = (int)(cx + (x - cx) * cosTheta - (y - cy) * sinTheta + 0.5f);
    *newY = (int)(cy + (x - cx) * sinTheta + (y - cy) * cosTheta + 0.5f);
}

void drawGridTriangles(fragmenttv_t *m,
                       uint8_t *dstY, uint8_t *dstU, uint8_t *dstV,
                       uint8_t *srcY, uint8_t *srcU, uint8_t *srcV,
                       int width, int height,
                       const int gridSize, const int minSize, const int maxSize,
                       const float displacementWeight) 
{
    const int numTrianglesX = width / gridSize;
    const int numTrianglesY = height / gridSize;

    int *restrict rand_lut = m->randbuf;
    int rand_lut_pos = 0;

    for (int i = 0; i < numTrianglesX; ++i) {
        for (int j = 0; j < numTrianglesY; ++j) {
            int triangleSize = minSize + rand_lut[rand_lut_pos] % (maxSize - minSize + 1);
            rand_lut_pos = (rand_lut_pos + 1) % MAX_FRAGMENTS;

            Point p1 = { i * gridSize, j * gridSize };
            Point p2 = { p1.x + triangleSize, p1.y };
            Point p3 = { p1.x, p1.y + triangleSize };

            float angle = (float)rand_lut[rand_lut_pos] / RAND_MAX * TWO_PI;
            rand_lut_pos = (rand_lut_pos + 1) % MAX_FRAGMENTS;

            int rotatedX, rotatedY;
            rotatePoint(p1.x, p1.y, p2.x, p2.y, angle, &rotatedX, &rotatedY, m);
            p2.x = rotatedX; p2.y = rotatedY;

            rotatePoint(p1.x, p1.y, p3.x, p3.y, angle, &rotatedX, &rotatedY, m);
            p3.x = rotatedX; p3.y = rotatedY;

            if (p1.x < 0) p1.x = 0; if (p1.y < 0) p1.y = 0;
            if (p2.x < 0) p2.x = 0; if (p2.y < 0) p2.y = 0;
            if (p3.x < 0) p3.x = 0; if (p3.y < 0) p3.y = 0;
            if (p1.x >= width) p1.x = width - 1; if (p1.y >= height) p1.y = height - 1;
            if (p2.x >= width) p2.x = width - 1; if (p2.y >= height) p2.y = height - 1;
            if (p3.x >= width) p3.x = width - 1; if (p3.y >= height) p3.y = height - 1;

            int dx21 = p2.x - p1.x, dy21 = p2.y - p1.y;
            int dx32 = p3.x - p2.x, dy32 = p3.y - p2.y;
            int dx13 = p1.x - p3.x, dy13 = p1.y - p3.y;

            int heightY = p3.y - p1.y;

            for (int y = p1.y; y <= p3.y; ++y) {

                float displacementY = 0.0f;
                if(heightY != 0){
                    int lutY = (y - p1.y) * LUT_SIZE / heightY;
                    if(lutY >= LUT_SIZE) lutY = LUT_SIZE - 1;
                    displacementY = m->sin_lut[lutY] * displacementWeight;
                }

                uint8_t *rowY = dstY + y * width;
                uint8_t *rowU = dstU + y * width;
                uint8_t *rowV = dstV + y * width;

                int widthX = p2.x - p1.x;
                for (int x = p1.x; x <= p2.x; ++x) {

                    float displacementX = 0.0f;
                    if(widthX != 0){
                        int lutX = (x - p1.x) * LUT_SIZE / widthX;
                        if(lutX >= LUT_SIZE) lutX = LUT_SIZE - 1;
                        displacementX = m->sin_lut[lutX] * displacementWeight;
                    }

                    int sign1 = dx21 * (y - p1.y) - (x - p1.x) * dy21;
                    int sign2 = dx32 * (y - p2.y) - (x - p2.x) * dy32;
                    int sign3 = dx13 * (y - p3.y) - (x - p3.x) * dy13;

                    if(sign1 >= 0 && sign2 >= 0 && sign3 >= 0){
                        int mirroredX = width - x - 1 + (int)(displacementX + 0.5f);
                        int mirroredY = y + (int)(displacementY + 0.5f);

                        if(mirroredX < 0) mirroredX = 0;
                        if(mirroredX >= width) mirroredX = width - 1;
                        if(mirroredY < 0) mirroredY = 0;
                        if(mirroredY >= height) mirroredY = height - 1;

                        int mirroredIndex = mirroredY * width + mirroredX;

                        rowY[x] = (rowY[x] + srcY[mirroredIndex]) >> 1;
                        rowU[x] = (rowU[x] + srcU[mirroredIndex]) >> 1;
                        rowV[x] = (rowV[x] + srcV[mirroredIndex]) >> 1;
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

