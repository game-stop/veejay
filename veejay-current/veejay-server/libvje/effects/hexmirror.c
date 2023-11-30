// kaleidoscope.c
// weed plugin
// (c) G. Finch (salsaman) 2013
//
// released under the GNU GPL 3 or later
// see file COPYING or www.gnu.org for details

/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
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
#include "hexmirror.h"

#define FIVE_PI3 5.23598775598f
#define FOUR_PI3 4.18879020479f

#define THREE_PI2 4.71238898038f

#define TWO_PI 6.28318530718f
#define TWO_PI3 2.09439510239f

#define ONE_PI2 1.57079632679f
#define ONE_PI3 1.0471975512f

#define RT2 1.41421356237f
#define RT3 1.73205080757f //sqrt(3)
#define RT32 0.86602540378f //sqrt(3)/2

#define RT322 0.43301270189f

#define LUT_SIZE 3600
#define LUT_DIVISOR (LUT_SIZE / TWO_PI)

#define ONEQTR_PI (M_PI / 4.0f)
#define THRQTR_PI (3.0f * M_PI / 4.0f)

#define calc_angle(y, x) ((x) > 0. ? ((y) >= 0. ? atanf((y) / (x)) : TWO_PI + atanf((y) / (x))) \
                          : ((x) < 0. ? atanf((y) / (x)) + M_PI : ((y) > 0. ? ONE_PI2 : THREE_PI2)))


static void put_pixel2(float angle, float theta, float r, int hheight, int hwidth, uint8_t *srcY, uint8_t *srcU, uint8_t *srcV, uint8_t *dstY, uint8_t *dstU, uint8_t *dstV, int jj, int width, float *cos_lut, float *sin_lut);

static void put_pixel1(float angle, float theta, float r, int hheight, int hwidth, uint8_t *srcY, uint8_t *srcU, uint8_t *srcV, uint8_t *dstY, uint8_t *dstU, uint8_t *dstV, int jj, int width, float *cos_lut, float *sin_lut);





vj_effect *hexmirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 102;
    ve->limits[1][0] = 1000;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 360;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;

    ve->defaults[0] = 562;
    ve->defaults[1] = 0;
    ve->defaults[2] = 1;
    ve->defaults[3] = 0;

    ve->description = "Salsaman's Kaleidoscope";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 2; // thread local buf mode
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Size (log)", "Offset Angle", "Anti clockwise", "Swap" );
    return ve;
}

typedef struct 
{
    uint8_t *buf[3];
    float *lut;
    float *atan_lut;
    float *cos_lut;
    float *sqrt_lut;
    float *sin_lut;

    float xangle;
} hexmirror_t;

void hexmirror_free(void *ptr) {
    hexmirror_t *s = (hexmirror_t*) ptr;
    free(s->lut);
    free(s->buf[0]);
    free(s);
}

static void init_atan_lut(hexmirror_t *f, int w, int h, int cx, int cy)
{
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            float x = j - cx;
            float y = i - cy;
            f->atan_lut[i * w + j] = calc_angle(y, x);
        }
    }
}

static void init_sqrt_lut(hexmirror_t *f, int w, int h, int cx, int cy)
{
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            int dx = x - cx;
            int dy = y - cy;
            f->sqrt_lut[y * w + x] = sqrtf( dx * dx + dy * dy );
        }
    }
}

static void init_sin_cos_lut(hexmirror_t *f) {
      
    for(int i = 0; i < LUT_SIZE; i ++ ) {               
        float angle = i * (TWO_PI / LUT_SIZE);
        f->sin_lut[i] = sinf(angle);
        f->cos_lut[i] = cosf(angle);
    }
}

void *hexmirror_malloc(int w, int h) {
    hexmirror_t *s = (hexmirror_t*) vj_calloc(sizeof(hexmirror_t));
    if(!s) return NULL;
    
    // required for non threading because of inplace operations
    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + ( w * h );
    s->buf[2] = s->buf[1] + ( w * h );

    s->lut = (float*) vj_malloc(sizeof(float) * ((w*h*4)) );
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->atan_lut = s->lut;
    s->sqrt_lut = s->atan_lut + (w*h);
    s->cos_lut = s->sqrt_lut + (w*h);
    s->sin_lut = s->cos_lut + (w*h);

    init_atan_lut( s, w, h, w/2, h/2 );
    init_sqrt_lut( s, w, h,w/2, h/2 );
    init_sin_cos_lut( s );

    return (void*) s;
}


static void calc_center(float side, float j, float i, float *x, float *y) {
    // find nearest hex center
    int gridx, gridy;

    float secx, secy;

    float sidex = side * RT3; // 2 * side * cos(30)
    float sidey = side * 1.5; // side + side * sin(30)

    float hsidex = sidex / 2., hsidey = sidey / 2.;

    i -= side / FIVE_PI3;
//  j -= hsidex;

    i += (i > 0.0) ? hsidey : -hsidey;
    j += (j > 0.0) ? hsidex : -hsidex;


    // find the square first
    gridy = i / sidey;
    gridx = j / sidex;


    // center
    float yy = gridy * sidey;
    float xx = gridx * sidex;

    secy = i - yy;
    secx = j - xx;

    secy += (secy < 0.0) ? sidey : 0.0;
    secx += (secx < 0.0) ? sidex : 0.0;

    if (!(gridy & 1)) {
        // even row (inverted Y)
        if (secy > (sidey - (hsidex - secx) * RT322)) {
            yy += sidey;
            xx -= hsidex;
        } else if (secy > sidey - (secx - hsidex) * RT322) {
            yy += sidey;
            xx += hsidex;
        }
    } else {
        // odd row, center is left or right (Y)
        if (secx <= hsidex) {
            if (secy < (sidey - secx * RT322)) {
                xx -= hsidex;
            } else yy += sidey;
        } else {
            if (secy < sidey - (sidex - secx) * RT322) {
                xx += hsidex;
            } else yy += sidey;
        }
    }

    *x = xx;
    *y = yy;
}

static inline void rotate(float r, float theta, float angle, float *x, float *y, float *cos_lut, float *sin_lut) {
    theta += angle;
    theta = ( theta < 0. ? theta += TWO_PI : theta >= TWO_PI ? theta -= TWO_PI : theta );

    int lut_pos =  ( (int) (theta * LUT_DIVISOR)) % LUT_SIZE;

    *x = r * cos_lut[lut_pos];
    *y = r * sin_lut[lut_pos];
}

static inline void put_pixel1(float angle, float theta, float r, int hheight,
                     int hwidth, uint8_t *srcY, uint8_t *srcU, uint8_t *srcV, uint8_t *dstY, uint8_t *dstU, uint8_t *dstV, int jj, int width, float *cos_lut, float *sin_lut) {
    // dest point is at i,j; r tells us which point to copy, and theta related to angle gives us the transform

    float adif = theta - angle;

    if (adif < 0.0) adif += TWO_PI;
    if (adif >= TWO_PI) adif -= TWO_PI;

    theta -= angle;

    if (theta < 0.0) theta += TWO_PI;
    if (theta >= TWO_PI) theta -= TWO_PI;

    float stheta = (adif < ONE_PI3) ? theta :
                   (adif < TWO_PI3) ? TWO_PI3 - theta :
                   (adif < M_PI) ? theta - TWO_PI3 :
                   (adif < FOUR_PI3) ? FOUR_PI3 - theta :
                   (adif < FIVE_PI3) ? theta - FOUR_PI3 :
                   TWO_PI - theta;

    stheta += angle;

    const int lut_pos = (int) (stheta * LUT_DIVISOR) % LUT_SIZE;
    const int sx = r * cos_lut[ lut_pos ] + 0.5;
    const int sy = r * sin_lut[ lut_pos ] + 0.5;

    const int src_index = sx + sy * width; // here, lets add
    if (sy < -hheight || sy >= hheight || sx < -hwidth || sx >= hwidth)
        return;

    dstY[jj] = srcY[src_index];
    dstU[jj] = srcU[src_index];
    dstV[jj] = srcV[src_index];
}

static inline void put_pixel2(float angle, float theta, float r, int hheight,
                     int hwidth, uint8_t *srcY, uint8_t *srcU, uint8_t *srcV, uint8_t *dstY, uint8_t *dstU, uint8_t *dstV, int jj, int width, float *cos_lut, float *sin_lut) {
    // dest point is at i,j; r tells us which point to copy, and theta related to angle gives us the transform

    float adif = theta - angle;

    if (adif < 0.0) adif += TWO_PI;
    if (adif >= TWO_PI) adif -= TWO_PI;

    theta -= angle;

    if (theta < 0.0) theta += TWO_PI;
    if (theta >= TWO_PI) theta -= TWO_PI;

    float stheta = (adif < ONE_PI3) ? theta :
                   (adif < TWO_PI3) ? TWO_PI3 - theta :
                   (adif < M_PI) ? theta - TWO_PI3 :
                   (adif < FOUR_PI3) ? FOUR_PI3 - theta :
                   (adif < FIVE_PI3) ? theta - FOUR_PI3 :
                   TWO_PI - theta;

    stheta += angle;

    const int lut_pos = (int) (stheta * LUT_DIVISOR) % LUT_SIZE;
    const int sx = r * cos_lut[ lut_pos ] + 0.5;
    const int sy = r * sin_lut[ lut_pos ] + 0.5;

    const int src_index = sx - sy * width; // original function, subtract

    if (sy < -hheight || sy >= hheight || sx < -hwidth || sx >= hwidth)
    {
            return;
    }
    dstY[jj] = srcY[src_index];
    dstU[jj] = srcU[src_index];
    dstV[jj] = srcV[src_index];
}



static inline float atan2_approx1(float y, float x) {
    float fabs_y = y * (y < 0.0f ? -1.0f : 1.0f);
    float r = (x < 0.0f) ? (x + fabs_y) / (fabs_y - x) : (x - fabs_y) / (x + fabs_y);
    float angle = (x < 0.0f) ? THRQTR_PI : ONEQTR_PI;
    angle += (0.1963f * r * r - 0.9817f) * r;
    return (y < 0.0f) ? -angle : angle;
}

static inline float sqrt_approx(float x) {
    return __builtin_sqrtf(x);
}

void hexmirror_apply(void *ptr, VJFrame *frame, int *args) {
    hexmirror_t *s = (hexmirror_t*)ptr;

    const int width = frame->out_width;
    const int height = frame->out_height;

    uint8_t *restrict srcY = frame->data[0] - frame->offset;
    uint8_t *restrict srcU = frame->data[1] - frame->offset;
    uint8_t *restrict srcV = frame->data[2] - frame->offset;

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    uint8_t *outY;
    uint8_t *outU;
    uint8_t *outV;

    float *restrict sqrt_lut = s->sqrt_lut;
    float *restrict cos_lut = s->cos_lut;
    float *restrict sin_lut = s->sin_lut;
    float *restrict atan_lut = s->atan_lut;

    float theta, r, delta, last_theta = 0., last_r = 0.;
    float x, y, a, b, last_x = 0., last_y = 0.;
    float side, fi, fj;

    float ifac = args[0] * 0.01f; // Zoom
    float sfac = log(ifac) / 2.0f; // Size (log)
    float angleoffs = args[1] * 0.1f; // Angle offsets, rotation
    int cw = args[2]; // Clockwise TODO
    int swap = args[3];

    float xangle = s->xangle;

    int start, end;
    int i, j, jj;

    const int centerX = width >> 1; 
    const int centerY = height >> 1;

    if( vje_setup_local_bufs( 1, frame, &outY, &outU, &outV, NULL ) == 0 ) {
        const int len = width * height;
        veejay_memcpy( bufY, srcY, len );
        veejay_memcpy( bufU, srcU, len );
        veejay_memcpy( bufV, srcV, len );

        srcY = bufY;
        srcU = bufU;
        srcV = bufV;    
    }


	void (*put_pixel_ptr)(float, float, float, int, int, uint8_t*, uint8_t*, uint8_t*, uint8_t*, uint8_t*, uint8_t*, int, int, float*, float*);


    put_pixel_ptr = &put_pixel1;
    if(swap) {
        put_pixel_ptr = &put_pixel2;
    }

    side = ( width < height ? centerX / RT32 : centerY );

    xangle += (float)angleoffs / 360. * TWO_PI;
    xangle = ( xangle >= TWO_PI ? xangle -= TWO_PI : xangle );

    side *= sfac;
    
    srcY += centerY * width + centerX;
    srcU += centerY * width + centerX;
    srcV += centerY * width + centerX;

    start = -centerY + (frame->jobnum * frame->height);
    end = start + frame->height;

    delta = xangle - ONE_PI2;

    const int yStart = centerY + start;
    const int yEnd = centerY + end;
    const int xStart = -centerX;
    const int xEnd = centerX;

    const int yOffset = (frame->jobnum * frame->height);

    for (i = yStart; i < yEnd; i++) {
        fi = (float)(i - centerY);
        jj = width * (i - yStart);

        for (j = xStart; j < xEnd; j++) {
                // first find the nearest hex center to our input point
                // hexes are rotating about the origin, this is equivalent to the point rotating
                // in the opposite sense
          
                fj = (float)j;

                int lut_pos = (i - yStart + yOffset) * (xEnd - xStart) + (j - xStart);
                // get angle of this point from origin
                theta = atan_lut[ lut_pos ];
                // get dist of point from origin
                r = sqrt_lut[ lut_pos ];
                
                // rotate point around origin
                rotate(r, theta, -delta, &a, &b, cos_lut, sin_lut);
                // find nearest hex center and angle to it
                calc_center(side, a, b, &x, &y);


                //theta = atan2_approx1(y, x);
                //r = sqrt_approx(x * x + y * y);

                // the hexes turn as they orbit, so calculating the angle to the center, we add the
                // rotation amount to get the final mapping
                if (x == last_x && y == last_y) {
                    theta = last_theta;
                    r = last_r;
                }
                else {
                    last_x = x;
                    last_y = y;
                    last_theta = theta = atan2_approx1(y, x);
                    last_r = r = sqrt_approx(x * x + y * y);
                }

                rotate(r, theta, delta, &a, &b, cos_lut, sin_lut);

                float bfi = b - fi;
                float afj = a - fj;
        
                theta = atan2_approx1(bfi, afj);
                r = sqrt_approx( bfi * bfi + afj * afj );

                if (r < 10.0f) {
                    r = 10.0f;
                    theta = atan2_approx1(bfi, afj);
                    rotate(r, theta, delta, &a, &b, cos_lut, sin_lut);
                }

                put_pixel_ptr(xangle, theta, r, centerY, centerX, srcY, srcU, srcV, outY, outU, outV, jj, width, cos_lut, sin_lut);
                jj++;
         }
     }
}

