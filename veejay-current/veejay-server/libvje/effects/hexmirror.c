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
#define RT3 1.73205080757f 
#define RT32 0.86602540378f 
#define RT322 0.43301270189f

#define LUT_SIZE 4096
#define LUT_DIVISOR (LUT_SIZE / TWO_PI)
#define LUT_MASK (LUT_SIZE - 1)
#define LUT_SIZE 4096
#define LUT_DIVISOR (LUT_SIZE / TWO_PI)

#define ONEQTR_PI (M_PI / 4.0f)
#define THRQTR_PI (3.0f * M_PI / 4.0f)

#define calc_angle(y, x) ((x) > 0. ? ((y) >= 0. ? atanf((y) / (x)) : TWO_PI + atanf((y) / (x))) \
                          : ((x) < 0. ? atanf((y) / (x)) + M_PI : ((y) > 0. ? ONE_PI2 : THREE_PI2)))


vj_effect *hexmirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

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
    ve->limits[0][4] = 0;
    ve->limits[1][4] = 100;

    ve->defaults[0] = 562;
    ve->defaults[1] = 0;
    ve->defaults[2] = 1;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;

    ve->description = "Salsaman's Kaleidoscope";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 2;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Size (log)", "Offset Angle", "Anti clockwise", "Swap", "Rotation Speed" );
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
    int n_threads;
} hexmirror_t;

void hexmirror_free(void *ptr) {
    hexmirror_t *s = (hexmirror_t*) ptr;
    free(s->lut);
    free(s->buf[0]);
    free(s);
}

static void init_atan_lut(hexmirror_t *f, int w, int h, int cx, int cy, int n_threads)
{
#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int y = 0; y < h; ++y) {

        float fy = y - cy;
        int row = y * w;

        for (int x = 0; x < w; ++x) {
            float fx = x - cx;
            f->atan_lut[row + x] = calc_angle(fy, fx);
        }
    }
}

static void init_sqrt_lut(hexmirror_t *f, int w, int h, int cx, int cy, int n_threads)
{
#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int y = 0; y < h; ++y) {

        int dy = y - cy;
        int dy2 = dy * dy;
        int row = y * w;

        for (int x = 0; x < w; ++x) {
            int dx = x - cx;
            f->sqrt_lut[row + x] = sqrtf((float)(dx * dx + dy2));
        }
    }
}

static void init_sin_cos_lut(hexmirror_t *f, int n_threads)
{
    const float step = TWO_PI / LUT_SIZE;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < LUT_SIZE; i++) {
        float a = i * step;
        f->sin_lut[i] = sinf(a);
        f->cos_lut[i] = cosf(a);
    }
}
 
static inline float wrap_angle(float a) {
    if (a < 0.0f) a += TWO_PI;
    else if (a >= TWO_PI) a -= TWO_PI;
    return a;
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

    size_t total = (w*h*2) + (LUT_SIZE*2);
    s->lut = (float*) vj_malloc(sizeof(float) * total );
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->atan_lut = s->lut;
    s->sqrt_lut = s->atan_lut + (w*h);
    s->cos_lut  = s->sqrt_lut + (w*h);
    s->sin_lut  = s->cos_lut + LUT_SIZE;

    s->n_threads = vje_advise_num_parallel_threads(w*h, vj_task_get_workers());

    init_atan_lut( s, w, h, w/2, h/2, s->n_threads );
    init_sqrt_lut( s, w, h,w/2, h/2, s->n_threads );
    init_sin_cos_lut( s, s->n_threads );

    return (void*) s;
}

static inline void calc_center(float j, float i, float sidex, float sidey, 
                                   float hsidex, float hsidey, float inv_sidex, 
                                   float inv_sidey, float side_off, float *x, float *y) {
    i -= side_off;
    i += (i > 0.0f) ? hsidey : -hsidey;
    j += (j > 0.0f) ? hsidex : -hsidex;

    int gridy = (int)(i * inv_sidey);
    int gridx = (int)(j * inv_sidex);

    float yy = (float)gridy * sidey;
    float xx = (float)gridx * sidex;

    float secy = i - yy;
    float secx = j - xx;

    if (secy < 0.0f) secy += sidey;
    if (secx < 0.0f) secx += sidex;

    if (!(gridy & 1)) {
        if (secy > (sidey - (hsidex - secx) * RT322)) {
            yy += sidey; xx -= hsidex;
        } else if (secy > sidey - (secx - hsidex) * RT322) {
            yy += sidey; xx += hsidex;
        }
    } else {
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
    *x = xx; *y = yy;
}

static inline void rotate(float r, float theta, float angle, float *x, float *y, float *cos_lut, float *sin_lut) {
    theta = wrap_angle(theta + angle);
    //int lut_pos =  ( (int) (theta * LUT_DIVISOR)) % LUT_SIZE;
    int lut_pos = (int)(theta * LUT_DIVISOR) & (LUT_SIZE - 1);

    *x = r * cos_lut[lut_pos];
    *y = r * sin_lut[lut_pos];
}

static inline float atan2_approx1(float y, float x) {
    if (x == 0.0f && y == 0.0f) return 0.0f;

    float ay = y >= 0.0f ? y : -y;
    float r = (x < 0.0f) ? (x + ay) / (ay - x) : (x - ay) / (x + ay);
    float angle = (x < 0.0f) ? (3.0f * M_PI_4) : (M_PI_4);

    angle += (0.1963f * r * r - 0.9817f) * r;

    return (y < 0.0f) ? -angle : angle;
}

static inline float sqrt_approx1(float x) {
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

static inline void process_pixel(int swap, float angle, float theta, float r, int hheight, int hwidth,
                                 uint8_t *srcY, uint8_t *srcU, uint8_t *srcV,
                                 uint8_t *pOutY, uint8_t *pOutU, uint8_t *pOutV, 
                                 int width, float *cos_lut, float *sin_lut) {
    
    float adif = wrap_angle(theta - angle);
    float fold_theta = swap ? wrap_angle(theta - angle) : theta;

    float stheta = (adif < ONE_PI3) ? fold_theta :
                   (adif < TWO_PI3) ? TWO_PI3 - fold_theta :
                   (adif < M_PI)    ? fold_theta - TWO_PI3 :
                   (adif < FOUR_PI3)? FOUR_PI3 - fold_theta :
                   (adif < FIVE_PI3)? fold_theta - FOUR_PI3 :
                   TWO_PI - fold_theta;

    stheta += angle;

    int lut_idx = (int)(stheta * LUT_DIVISOR) & LUT_MASK;

    int sx = (int)(r * cos_lut[lut_idx] + 0.5f);
    int sy = (int)(r * sin_lut[lut_idx] + 0.5f);

    if ((unsigned)(sx + hwidth) >= (unsigned)(hwidth * 2) ||
        (unsigned)(sy + hheight) >= (unsigned)(hheight * 2)) {
        *pOutY = pixel_Y_lo_;
        *pOutU = 128;
        *pOutV = 128;
        return;
    }

    int src_idx = swap ? (sx - sy * width) : (sx + sy * width);
    *pOutY = srcY[src_idx];
    *pOutU = srcU[src_idx];
    *pOutV = srcV[src_idx];
}

void hexmirror_apply(void *ptr, VJFrame *frame, int *args) {
    hexmirror_t *s = (hexmirror_t*)ptr;
    const int width  = frame->out_width;
    const int height = frame->out_height;
    const int centerX = width >> 1;
    const int centerY = height >> 1;

    uint8_t *outY, *outU, *outV;
    uint8_t *srcY = frame->data[0] - frame->offset;
    uint8_t *srcU = frame->data[1] - frame->offset;
    uint8_t *srcV = frame->data[2] - frame->offset;

    if(vje_setup_local_bufs(1, frame, &outY, &outU, &outV, NULL) == 0) {
        veejay_memcpy(s->buf[0], srcY, frame->len);
        veejay_memcpy(s->buf[1], srcU, frame->len);
        veejay_memcpy(s->buf[2], srcV, frame->len);

        srcY = s->buf[0];
        srcU = s->buf[0] + width * height;
        srcV = s->buf[0] + width * height * 2;
    }

    const float sfac = logf(args[0] * 0.01f) * 0.5f;
    const float side = (width < height ? centerX / RT32 : centerY) * sfac;
    const float sidex = side * RT3, sidey = side * 1.5f;
    const float hsidex = sidex * 0.5f, hsidey = sidey * 0.5f;
    const float inv_sidex = 1.0f / sidex, inv_sidey = 1.0f / sidey;
    const float side_off = side / FIVE_PI3;

    const float norm_speed = args[4] * 0.01f;
    s->xangle = wrap_angle(s->xangle + (norm_speed * norm_speed * 0.02f * (args[2] ? 1 : -1)));
    const float render_angle = wrap_angle(s->xangle + (args[1] / 360.0f) * TWO_PI);
    const float delta = render_angle - ONE_PI2;

    const uint8_t *restrict relY = srcY + centerY * width + centerX;
    const uint8_t *restrict relU = srcU + centerY * width + centerX;
    const uint8_t *restrict relV = srcV + centerY * width + centerX;

    const int start_y = centerY + (-centerY + frame->jobnum * frame->height);
    const int end_y   = start_y + frame->height;

    const float *restrict cos_lut = s->cos_lut;
    const float *restrict sin_lut = s->sin_lut;

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int i = start_y; i < end_y; i++) {
        const float fi = (float)(i - centerY);
        uint8_t *pOutY = outY + width * (i - start_y);
        uint8_t *pOutU = outU + width * (i - start_y);
        uint8_t *pOutV = outV + width * (i - start_y);

        const float *restrict atan_row = s->atan_lut + i * width;
        const float *restrict sqrt_row = s->sqrt_lut + i * width;

        for(int j = -centerX; j < centerX; j++) {
            const float theta_base = *atan_row++;
            const float r_base     = *sqrt_row++;

            float angle_rot = theta_base - delta;
            int l_idx_rot = ((int)(angle_rot * LUT_DIVISOR)) & LUT_MASK;
            float cos_rot = cos_lut[l_idx_rot];
            float sin_rot = sin_lut[l_idx_rot];

            float a_hex = r_base * cos_rot;
            float b_hex = r_base * sin_rot;

            float h_x, h_y;
            calc_center(a_hex, b_hex, sidex, sidey, hsidex, hsidey, inv_sidex, inv_sidey, side_off, &h_x, &h_y);

            float theta = atan2_approx1(h_y, h_x);
            float r     = sqrt_approx1(h_x * h_x + h_y * h_y);

            float angle_f = theta + delta;
            int l_idx_f = ((int)(angle_f * LUT_DIVISOR)) & LUT_MASK;

            float bfi = r * sin_lut[l_idx_f] - fi;
            float afj = r * cos_lut[l_idx_f] - (float)j;

            theta = atan2_approx1(bfi, afj);
            r     = sqrt_approx1(bfi * bfi + afj * afj);
            if(r < 10.0f) r = 10.0f;

            process_pixel(args[3], render_angle, theta, r, centerY, centerX,
                          relY, relU, relV, pOutY, pOutU, pOutV, width, cos_lut, sin_lut);

            pOutY++; pOutU++; pOutV++;
        }
    }
}