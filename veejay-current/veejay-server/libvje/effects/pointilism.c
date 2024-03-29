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
#include <omp.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "pointilism.h"

#define FIXED_POINT_BITS 16
#define FIXED_POINT_ONE (1 << FIXED_POINT_BITS)

vj_effect *pointilism_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 1;
    ve->defaults[1] = 3;
    ve->defaults[2] = 2;
    ve->defaults[3] = 0;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 16;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 16;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 16;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Min" , "Max", "Kernel" , "Loop" );
    ve->description = "Pointilism";   
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    return ve;
}

typedef struct {
    uint8_t *buf[3];
    int *rand_lut;
} pointilism_t;

void *pointilism_malloc(int w, int h)
{
    int i;
    pointilism_t *s = (pointilism_t*) vj_calloc(sizeof(pointilism_t));
    if(!s) {
        return NULL;
    }
    s->buf[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->rand_lut = (int*) vj_malloc( sizeof(int) * w * h );
    if(!s->rand_lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + w * h;
    s->buf[2] = s->buf[1] + w * h;

    veejay_memset( s->buf[0], 0, w * h );
    veejay_memset( s->buf[1], 128, w * h * 2 );

    for( i = 0; i < (w*h); i ++ ) {
        s->rand_lut[i] = rand();
    }

    return (void*) s;
}

void pointilism_free(void *ptr) {
    pointilism_t *s = (pointilism_t*) ptr;
    free(s->buf[0]);
    free(s);
}

static inline void process_corners(
    int x, int y, int kernelRadius, int minRadius, int maxRadius,
    int w, int h, const uint8_t* srcY, const uint8_t* srcU, const uint8_t* srcV,
    uint8_t* dstY, uint8_t* dstU, uint8_t* dstV, int *lut) {
    
    uint8_t minL = 0xff;
    uint8_t maxL = 0;
    uint8_t minU = 0xff;
    uint8_t maxU = 0;
    uint8_t minV = 0xff;
    uint8_t maxV = 0xff;
    
    for (int kx = -kernelRadius; kx < kernelRadius; kx++) {
        for (int ky = -kernelRadius; ky < kernelRadius; ky++) {
            int nx = x + kx;
            int ny = y + ky;

            nx = (nx < 0) ? 0 : (nx >= w) ? w - 1 : nx;
            ny = (ny < 0) ? 0 : (ny >= h) ? h - 1 : ny;

            uint8_t L = srcY[ny * w + nx];
            uint8_t U = srcU[ny * w + nx];
            uint8_t V = srcV[ny * w + nx];

            minL = (L < minL) ? L : minL;
            maxL = (L > maxL) ? L : maxL;
            minU = (U < minU) ? U : minU;
            maxU = (U > maxU) ? U : maxU;
            minV = (V < minV) ? V : minV;
            maxV = (V > maxV) ? V : maxV;
        }
    }

    const int pointilismRadius = minRadius + lut[y * w + x] % (maxRadius - minRadius + 1);
    const int pointilismRadiusSquared = pointilismRadius * pointilismRadius;
    const int fixedPointRandomRadiusSquared = pointilismRadiusSquared << FIXED_POINT_BITS;

    const int startX = (x - pointilismRadius < 0) ? -x : -pointilismRadius;
    const int startY = (y - pointilismRadius < 0) ? -y : -pointilismRadius;
    const int endX = (x + pointilismRadius >= w) ? w - x - 1 : pointilismRadius;
    const int endY = (y + pointilismRadius >= h) ? h - y - 1 : pointilismRadius;


    for (int dx = startX; dx <= endX; dx++) {
        for (int dy = startY; dy <= endY; dy++) {
            int nx = x + dx;
            int ny = y + dy;

            int distanceSquared = (dx * dx) + (dy * dy);
            distanceSquared = ((unsigned int)(distanceSquared - pointilismRadiusSquared));

            int fixedPointDistanceSquared = distanceSquared << FIXED_POINT_BITS;

            const int gradient = (fixedPointRandomRadiusSquared - fixedPointDistanceSquared) / pointilismRadiusSquared;

            uint8_t newL = (uint8_t)((maxL * (FIXED_POINT_ONE - gradient) + minL * gradient) >> FIXED_POINT_BITS);
            uint8_t newU = (uint8_t)((maxU * (FIXED_POINT_ONE - gradient) + minU * gradient) >> FIXED_POINT_BITS);
            uint8_t newV = (uint8_t)((maxV * (FIXED_POINT_ONE - gradient) + minV * gradient) >> FIXED_POINT_BITS);

            dstY[ny * w + nx] = newL;
            dstU[ny * w + nx] = newU;
            dstV[ny * w + nx] = newV;
        }
    }
}

void pointilism_apply(void *ptr, VJFrame *frame, int *args) {

    pointilism_t *p = (pointilism_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;

    int minRadius = args[0];
    int maxRadius = args[1];

    const int kernelRadius = args[2];
    const uint8_t *restrict srcY = frame->data[0];
    const uint8_t *restrict srcU = frame->data[1];
    const uint8_t *restrict srcV = frame->data[2];
    uint8_t *restrict dstY = p->buf[0] + frame->offset;
    uint8_t *restrict dstU = p->buf[1] + frame->offset;
    uint8_t *restrict dstV = p->buf[2] + frame->offset;
    int *restrict lut = p->rand_lut;
    int x,y,kx,ky,nx,ny,dx,dy;

    int radiusLimit = ( kernelRadius > maxRadius ? kernelRadius: maxRadius );
    if( minRadius > radiusLimit )
        radiusLimit = minRadius;
    if( minRadius > maxRadius ) {
        int tmp = maxRadius;
        maxRadius = minRadius;
        minRadius = tmp;
    }

    for( y = radiusLimit; y < h - radiusLimit; y ++ ) {
        for( x = radiusLimit; x < w - radiusLimit ; x ++ ) {
            // no if conditionals here!
            uint8_t minL = 0xff;
            uint8_t maxL = 0;

            uint8_t minU = 0xff;
            uint8_t maxU = 0;

            uint8_t minV = 0xff;
            uint8_t maxV = 0;

            for (kx = -kernelRadius; kx < kernelRadius; kx ++) {
                for ( ky = -kernelRadius; ky < kernelRadius; ky ++) {
                    uint8_t L = srcY[(y + ky) * w + (x + kx)];
                    uint8_t U = srcU[(y + ky) * w + (x + kx)];
                    uint8_t V = srcV[(y + ky) * w + (x + kx)];

                    minL = (L < minL) ? L : minL;
                    maxL = (L > maxL) ? L : maxL;
                    minU = (U < minU) ? U : minU;
                    maxU = (U > maxU) ? U : maxU;
                    minV = (V < minV) ? V : minV;
                    maxV = (V > maxV) ? V : maxV;
                }
            }


            const int pointilismRadius = minRadius + lut[y * w + x] % (maxRadius - minRadius + 1 );
            const int pointilismRadiusSquared = pointilismRadius * pointilismRadius;
            const int fixedPointRandomRadiusSquared = pointilismRadiusSquared << FIXED_POINT_BITS;

            for( dx = -pointilismRadius; dx <= pointilismRadius; dx ++ ) {
                for( dy = -pointilismRadius; dy <= pointilismRadius; dy ++ ) {
                    nx = x + dx;
                    ny = y + dy;

                    int distanceSquared = (dx*dx) + (dy*dy);
                    distanceSquared = ( (unsigned int)( distanceSquared - pointilismRadiusSquared ));
                    
                    int fixedPointDistanceSquared = distanceSquared << FIXED_POINT_BITS;

                    const int gradient = (fixedPointRandomRadiusSquared - fixedPointDistanceSquared) / pointilismRadiusSquared;

                    uint8_t newL = (uint8_t)((maxL * (FIXED_POINT_ONE - gradient) + minL * gradient) >> FIXED_POINT_BITS);
                    uint8_t newU = (uint8_t)((maxU * (FIXED_POINT_ONE - gradient) + minU * gradient) >> FIXED_POINT_BITS);
                    uint8_t newV = (uint8_t)((maxV * (FIXED_POINT_ONE - gradient) + minV * gradient) >> FIXED_POINT_BITS);

                    dstY[ ny * w + nx ] = newL;
                    dstU[ ny * w + nx ] = newU;
                    dstV[ ny * w + nx ] = newV;
                }
            }
        }
    }


    // do the corners
    for (y = 0; y < radiusLimit; y++) {
        for (x = 0; x < radiusLimit; x++) {
            process_corners(x,y,kernelRadius,minRadius,maxRadius,w,h,srcY,srcU,srcV,dstY,dstU,dstV,lut);
        }
        for (x = w - radiusLimit; x < w; x++) {
            process_corners(x,y,kernelRadius,minRadius,maxRadius,w,h,srcY,srcU,srcV,dstY,dstU,dstV,lut);
        }
    }

    for (y = h - radiusLimit; y < h; y++) {
        for (x = 0; x < radiusLimit; x++) {
            process_corners(x,y,kernelRadius,minRadius,maxRadius,w,h,srcY,srcU,srcV,dstY,dstU,dstV,lut);
        }

        for (x = w - radiusLimit; x < w; x++) {
            process_corners(x,y,kernelRadius,minRadius,maxRadius,w,h,srcY,srcU,srcV,dstY,dstU,dstV,lut);
        }
    }

    veejay_memcpy( frame->data[0], dstY, frame->len );
    veejay_memcpy( frame->data[1], dstU, frame->len );
    veejay_memcpy( frame->data[2], dstV, frame->len );

}

