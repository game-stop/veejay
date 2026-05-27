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
#include "hexmirror.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_4
#define M_PI_4 0.78539816339744830962
#endif

#define FIVE_PI3  5.23598775598f
#define FOUR_PI3  4.18879020479f
#define THREE_PI2 4.71238898038f
#define TWO_PI    6.28318530718f
#define TWO_PI3   2.09439510239f
#define ONE_PI2   1.57079632679f
#define ONE_PI3   1.04719755120f
#define RT3       1.73205080757f
#define RT32      0.86602540378f
#define RT322     0.43301270189f

#define LUT_SIZE    4096
#define LUT_MASK    (LUT_SIZE - 1)
#define LUT_DIVISOR (LUT_SIZE / TWO_PI)

#define HEXMIRROR_PARAMS 6
#define P_SIZE_LOG       0
#define P_OFFSET_ANGLE   1
#define P_ANTICLOCKWISE  2
#define P_SWAP           3
#define P_ROT_SPEED      4
#define P_BEAT_PUSH      5

#define calc_angle(y, x) ((x) > 0.0f ? ((y) >= 0.0f ? atanf((y) / (x)) : TWO_PI + atanf((y) / (x))) \
                          : ((x) < 0.0f ? atanf((y) / (x)) + (float)M_PI : ((y) > 0.0f ? ONE_PI2 : THREE_PI2)))

typedef struct 
{
    uint8_t *buf[3];
    float *lut;
    float *atan_lut;
    float *cos_lut;
    float *sqrt_lut;
    float *sin_lut;


    float xangle;
    float beat_env;
    int n_threads;
} hexmirror_t;

static inline int hex_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float wrap_angle(float a)
{
    if(a < 0.0f)
        a += TWO_PI;
    else if(a >= TWO_PI)
        a -= TWO_PI;
    return a;
}

static inline float atan2_approx1(float y, float x)
{
    if(x == 0.0f && y == 0.0f)
        return 0.0f;

    float ay = y >= 0.0f ? y : -y;
    float r = (x < 0.0f) ? (x + ay) / (ay - x) : (x - ay) / (x + ay);
    float angle = (x < 0.0f) ? (3.0f * (float)M_PI_4) : (float)M_PI_4;

    angle += (0.1963f * r * r - 0.9817f) * r;

    return (y < 0.0f) ? -angle : angle;
}

static inline float sqrt_approx1(float x)
{
    if(x <= 0.0f)
        return 0.0f;

    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f3759dfu - (u.i >> 1);

    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);

    return x * y;
}

static inline int hex_beat_shape(int beat_push)
{
    beat_push = hex_clampi(beat_push, 0, 1000);
    const int sq = (beat_push * beat_push + 500) / 1000;
    return hex_clampi((beat_push * 35 + sq * 65 + 50) / 100, 0, 1000);
}

static inline void calc_center(float j,
                               float i,
                               float sidex,
                               float sidey,
                               float hsidex,
                               float hsidey,
                               float inv_sidex,
                               float inv_sidey,
                               float side_off,
                               float *x,
                               float *y)
{
    i -= side_off;
    i += (i > 0.0f) ? hsidey : -hsidey;
    j += (j > 0.0f) ? hsidex : -hsidex;

    int gridy = (int)(i * inv_sidey);
    int gridx = (int)(j * inv_sidex);

    float yy = (float)gridy * sidey;
    float xx = (float)gridx * sidex;

    float secy = i - yy;
    float secx = j - xx;

    if(secy < 0.0f)
        secy += sidey;
    if(secx < 0.0f)
        secx += sidex;

    if(!(gridy & 1)) {
        if(secy > (sidey - (hsidex - secx) * RT322)) {
            yy += sidey;
            xx -= hsidex;
        }
        else if(secy > sidey - (secx - hsidex) * RT322) {
            yy += sidey;
            xx += hsidex;
        }
    }
    else {
        if(secx <= hsidex) {
            if(secy < (sidey - secx * RT322))
                xx -= hsidex;
            else
                yy += sidey;
        }
        else {
            if(secy < sidey - (sidex - secx) * RT322)
                xx += hsidex;
            else
                yy += sidey;
        }
    }

    *x = xx;
    *y = yy;
}

static inline void hex_rotate(float r,
                              float theta,
                              float angle,
                              float *x,
                              float *y,
                              const float *restrict cos_lut,
                              const float *restrict sin_lut)
{
    theta = wrap_angle(theta + angle);
    int lut_pos = (int)(theta * LUT_DIVISOR) & LUT_MASK;

    *x = r * cos_lut[lut_pos];
    *y = r * sin_lut[lut_pos];
}

static inline void process_pixel_common(int swap,
                                        float angle,
                                        float theta,
                                        float r,
                                        int hheight,
                                        int hwidth,
                                        const uint8_t *restrict srcY,
                                        const uint8_t *restrict srcU,
                                        const uint8_t *restrict srcV,
                                        uint8_t *restrict pOutY,
                                        uint8_t *restrict pOutU,
                                        uint8_t *restrict pOutV,
                                        int width,
                                        const float *restrict cos_lut,
                                        const float *restrict sin_lut)
{
    float adif = wrap_angle(theta - angle);
    float fold_theta = swap ? wrap_angle(theta - angle) : theta;

    float stheta = (adif < ONE_PI3)     ? fold_theta :
                   (adif < TWO_PI3)     ? TWO_PI3 - fold_theta :
                   (adif < (float)M_PI) ? fold_theta - TWO_PI3 :
                   (adif < FOUR_PI3)    ? FOUR_PI3 - fold_theta :
                   (adif < FIVE_PI3)    ? fold_theta - FOUR_PI3 :
                   TWO_PI - fold_theta;

    stheta += angle;

    int lut_idx = (int)(stheta * LUT_DIVISOR) & LUT_MASK;

    int sx = (int)(r * cos_lut[lut_idx] + 0.5f);
    int sy = (int)(r * sin_lut[lut_idx] + 0.5f);

    if((unsigned)(sx + hwidth) >= (unsigned)(hwidth * 2) ||
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

static void init_atan_lut(hexmirror_t *f, int w, int h, int cx, int cy, int n_threads)
{
#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < h; ++y) {
        float fy = (float)(y - cy);
        int row = y * w;

        for(int x = 0; x < w; ++x) {
            float fx = (float)(x - cx);
            f->atan_lut[row + x] = calc_angle(fy, fx);
        }
    }
}

static void init_sqrt_lut(hexmirror_t *f, int w, int h, int cx, int cy, int n_threads)
{
#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < h; ++y) {
        int dy = y - cy;
        int dy2 = dy * dy;
        int row = y * w;

        for(int x = 0; x < w; ++x) {
            int dx = x - cx;
            f->sqrt_lut[row + x] = sqrtf((float)(dx * dx + dy2));
        }
    }
}

static void init_sin_cos_lut(hexmirror_t *f, int n_threads)
{
    const float step = TWO_PI / (float)LUT_SIZE;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < LUT_SIZE; i++) {
        float a = (float)i * step;
        f->sin_lut[i] = sinf(a);
        f->cos_lut[i] = cosf(a);
    }
}

vj_effect *hexmirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = HEXMIRROR_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_SIZE_LOG]      = 102;  ve->limits[1][P_SIZE_LOG]      = 1000; ve->defaults[P_SIZE_LOG]      = 562;
    ve->limits[0][P_OFFSET_ANGLE]  = 0;    ve->limits[1][P_OFFSET_ANGLE]  = 360;  ve->defaults[P_OFFSET_ANGLE]  = 0;
    ve->limits[0][P_ANTICLOCKWISE] = 0;    ve->limits[1][P_ANTICLOCKWISE] = 1;    ve->defaults[P_ANTICLOCKWISE] = 1;
    ve->limits[0][P_SWAP]          = 0;    ve->limits[1][P_SWAP]          = 1;    ve->defaults[P_SWAP]          = 0;
    ve->limits[0][P_ROT_SPEED]     = 0;    ve->limits[1][P_ROT_SPEED]     = 100;  ve->defaults[P_ROT_SPEED]     = 0;
    ve->limits[0][P_BEAT_PUSH]     = 0;    ve->limits[1][P_BEAT_PUSH]     = 1000; ve->defaults[P_BEAT_PUSH]     = 0;

    ve->description = "Salsaman's Kaleidoscope";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Size (log)",
        "Offset Angle",
        "Anti clockwise",
        "Swap",
        "Rotation Speed",
        "Beat Push"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE,      VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 160,                900,                6,  22, 2200, 5200, 1800, 28,    /* Size (log) */
        VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,      0,                  360,                8,  32, 1200, 3200, 0,    42,    /* Offset Angle */
        VJ_BEAT_SELECTOR,       VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Anti clockwise */
        VJ_BEAT_SELECTOR,       VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Swap */
        VJ_BEAT_SPEED,          VJ_BEAT_F_CONTINUOUS,                       0,                  78,                 10, 40, 800,  2200, 0,    70,    /* Rotation Speed */
        VJ_BEAT_INTENSITY,      VJ_BEAT_F_CONTINUOUS,                       0,                  760,                18, 68, 80,   760,  0,    100    /* Beat Push */
    );

    (void)w;
    (void)h;

    return ve;
}

void *hexmirror_malloc(int w, int h)
{
    hexmirror_t *s = (hexmirror_t *) vj_calloc(sizeof(hexmirror_t));
    if(!s)
        return NULL;

    int len = w * h;
    if(len <= 0) {
        free(s);
        return NULL;
    }

    s->buf[0] = (uint8_t *) vj_malloc(sizeof(uint8_t) * (size_t)len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    size_t total = ((size_t)len * 2u) + ((size_t)LUT_SIZE * 2u);
    s->lut = (float *) vj_malloc(sizeof(float) * total);
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->atan_lut = s->lut;
    s->sqrt_lut = s->atan_lut + len;
    s->cos_lut  = s->sqrt_lut + len;
    s->sin_lut  = s->cos_lut + LUT_SIZE;

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads <= 0)
        s->n_threads = 1;

    s->xangle = 0.0f;
    s->beat_env = 0.0f;

    init_atan_lut(s, w, h, w / 2, h / 2, s->n_threads);
    init_sqrt_lut(s, w, h, w / 2, h / 2, s->n_threads);
    init_sin_cos_lut(s, s->n_threads);


    return (void *)s;
}

void hexmirror_free(void *ptr)
{
    hexmirror_t *s = (hexmirror_t *) ptr;

    if(!s)
        return;


    free(s->lut);
    free(s->buf[0]);
    free(s);
}

void hexmirror_apply(void *ptr, VJFrame *frame, int *args)
{
    hexmirror_t *s = (hexmirror_t *)ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width  = frame->out_width;
    const int height = frame->out_height;
    const int len = width * height;

    if(width <= 2 || height <= 2 || len <= 0)
        return;

    const int centerX = width >> 1;
    const int centerY = height >> 1;
    const int proc_w = centerX << 1;

    uint8_t *restrict outY = frame->data[0];
    uint8_t *restrict outU = frame->data[1];
    uint8_t *restrict outV = frame->data[2];

    veejay_memcpy(s->buf[0], outY, len);
    veejay_memcpy(s->buf[1], outU, len);
    veejay_memcpy(s->buf[2], outV, len);

    const uint8_t *restrict srcY = s->buf[0];
    const uint8_t *restrict srcU = s->buf[1];
    const uint8_t *restrict srcV = s->buf[2];

    const int size_arg = hex_clampi(args[P_SIZE_LOG], 102, 1000);
    const float sfac = logf((float)size_arg * 0.01f) * 0.5f;
    const float side = (width < height ? (float)centerX / RT32 : (float)centerY) * sfac;

    if(side <= 0.0001f)
        return;

    const float sidex = side * RT3;
    const float sidey = side * 1.5f;
    const float hsidex = sidex * 0.5f;
    const float hsidey = sidey * 0.5f;
    const float inv_sidex = 1.0f / sidex;
    const float inv_sidey = 1.0f / sidey;
    const float side_off = side / FIVE_PI3;

    const float norm_speed = (float)hex_clampi(args[P_ROT_SPEED], 0, 100) * 0.01f;
    const float dir = args[P_ANTICLOCKWISE] ? 1.0f : -1.0f;

    const int beat_push = hex_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_shaped = hex_beat_shape(beat_push);
    const float beat_target = (float)beat_shaped * 0.001f;

    if(beat_target > s->beat_env)
        s->beat_env += (beat_target - s->beat_env) * 0.38f;
    else
        s->beat_env += (beat_target - s->beat_env) * 0.065f;

    if(s->beat_env < 0.0001f)
        s->beat_env = 0.0f;
    else if(s->beat_env > 1.0f)
        s->beat_env = 1.0f;

    const float beat_drive = s->beat_env * s->beat_env;
    const float rotation_step = (norm_speed * norm_speed * 0.02f) + (beat_drive * 0.028f);

    s->xangle = wrap_angle(s->xangle + rotation_step * dir);

    const float render_angle = wrap_angle(s->xangle + ((float)hex_clampi(args[P_OFFSET_ANGLE], 0, 360) / 360.0f) * TWO_PI);
    const float delta = render_angle - ONE_PI2;
    const int swap = args[P_SWAP] ? 1 : 0;

    const uint8_t *restrict relY = srcY + centerY * width + centerX;
    const uint8_t *restrict relU = srcU + centerY * width + centerX;
    const uint8_t *restrict relV = srcV + centerY * width + centerX;

    const float *restrict cos_lut = s->cos_lut;
    const float *restrict sin_lut = s->sin_lut;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int i = 0; i < height; i++) {
            const float fi = (float)(i - centerY);
            uint8_t *restrict pOutY = outY + width * i;
            uint8_t *restrict pOutU = outU + width * i;
            uint8_t *restrict pOutV = outV + width * i;

            const float *restrict atan_row = s->atan_lut + i * width;
            const float *restrict sqrt_row = s->sqrt_lut + i * width;

            int mru_valid = 0;
            float mru_hx = 0.0f;
            float mru_hy = 0.0f;
            float mru_cx = 0.0f;
            float mru_cy = 0.0f;

            for(int x = 0; x < proc_w; x++) {
                const int j = x - centerX;
                const float theta_base = atan_row[x];
                const float r_base = sqrt_row[x];

                float angle_rot = theta_base - delta;
                int l_idx_rot = ((int)(angle_rot * LUT_DIVISOR)) & LUT_MASK;
                float cos_rot = cos_lut[l_idx_rot];
                float sin_rot = sin_lut[l_idx_rot];

                float a_hex = r_base * cos_rot;
                float b_hex = r_base * sin_rot;

                float h_x;
                float h_y;
                calc_center(a_hex, b_hex, sidex, sidey, hsidex, hsidey, inv_sidex, inv_sidey, side_off, &h_x, &h_y);

                float center_hex_x;
                float center_hex_y;

                if(mru_valid && h_x == mru_hx && h_y == mru_hy) {
                    center_hex_x = mru_cx;
                    center_hex_y = mru_cy;
                } else {
                    float theta0 = atan2_approx1(h_y, h_x);
                    float r0 = sqrt_approx1(h_x * h_x + h_y * h_y);
                    float angle_f = theta0 + delta;
                    int l_idx_f = ((int)(angle_f * LUT_DIVISOR)) & LUT_MASK;

                    center_hex_x = r0 * cos_lut[l_idx_f];
                    center_hex_y = r0 * sin_lut[l_idx_f];

                    mru_valid = 1;
                    mru_hx = h_x;
                    mru_hy = h_y;
                    mru_cx = center_hex_x;
                    mru_cy = center_hex_y;
                }

                float bfi = center_hex_y - fi;
                float afj = center_hex_x - (float)j;

                float theta = atan2_approx1(bfi, afj);
                float r = sqrt_approx1(bfi * bfi + afj * afj);
                if(r < 10.0f)
                    r = 10.0f;

                process_pixel_common(
                    swap,
                    render_angle,
                    theta,
                    r,
                    centerY,
                    centerX,
                    relY,
                    relU,
                    relV,
                    pOutY + x,
                    pOutU + x,
                    pOutV + x,
                    width,
                    cos_lut,
                    sin_lut
                );
            }
        }
}
