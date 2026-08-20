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
#define PI_F      3.14159265359f
#define PI_4_F    0.78539816339f

#define LUT_SIZE    4096
#define LUT_MASK    (LUT_SIZE - 1)
#define LUT_DIVISOR ((float)LUT_SIZE / TWO_PI)

#define HEXMIRROR_PARAMS 5
#define P_SIZE_LOG       0
#define P_OFFSET_ANGLE   1
#define P_ANTICLOCKWISE  2
#define P_SWAP           3
#define P_ROT_SPEED      4

#define calc_angle(y, x) ((x) > 0.0f ? ((y) >= 0.0f ? atanf((y) / (x)) : TWO_PI + atanf((y) / (x))) \
                          : ((x) < 0.0f ? atanf((y) / (x)) + PI_F : ((y) > 0.0f ? ONE_PI2 : THREE_PI2)))

typedef struct {
    uint8_t *buf[3];
    float *lut;
    float *atan_lut;
    float *sqrt_lut;
    float *cos_lut;
    float *sin_lut;
    float xangle;
    int n_threads;
} hexmirror_t;

static inline int hex_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float hex_wrap_angle(float a)
{
    if(a < 0.0f)
        a += TWO_PI;
    else if(a >= TWO_PI)
        a -= TWO_PI;
    return a;
}

static inline float hex_atan2_approx(float y, float x)
{
    if(x == 0.0f && y == 0.0f)
        return 0.0f;

    const float ay = y >= 0.0f ? y : -y;
    const float r = (x < 0.0f) ? (x + ay) / (ay - x) : (x - ay) / (x + ay);
    float angle = (x < 0.0f) ? (3.0f * PI_4_F) : PI_4_F;

    angle += (0.1963f * r * r - 0.9817f) * r;

    return y < 0.0f ? -angle : angle;
}

static inline float hex_sqrt_approx(float x)
{
    union { float f; uint32_t i; } u;

    if(x <= 0.0f)
        return 0.0f;

    u.f = x;
    u.i = 0x5f3759dfu - (u.i >> 1);

    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);

    return x * y;
}

static inline void hex_calc_center(float j,
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
        if(secy > sidey - (hsidex - secx) * RT322) {
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
            if(secy < sidey - secx * RT322)
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

static inline void hex_process_pixel(int swap,
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
    const float adif = hex_wrap_angle(theta - angle);
    const float fold_theta = swap ? hex_wrap_angle(theta - angle) : theta;

    float stheta = (adif < ONE_PI3)  ? fold_theta :
                   (adif < TWO_PI3)  ? TWO_PI3 - fold_theta :
                   (adif < PI_F)     ? fold_theta - TWO_PI3 :
                   (adif < FOUR_PI3) ? FOUR_PI3 - fold_theta :
                   (adif < FIVE_PI3) ? fold_theta - FOUR_PI3 :
                   TWO_PI - fold_theta;

    stheta += angle;

    const int lut_idx = (int)(stheta * LUT_DIVISOR) & LUT_MASK;
    const int sx = (int)(r * cos_lut[lut_idx] + 0.5f);
    const int sy = (int)(r * sin_lut[lut_idx] + 0.5f);

    if((unsigned)(sx + hwidth) >= (unsigned)(hwidth << 1) ||
       (unsigned)(sy + hheight) >= (unsigned)(hheight << 1)) {
        *pOutY = pixel_Y_lo_;
        *pOutU = 128;
        *pOutV = 128;
        return;
    }

    const int src_idx = swap ? (sx - sy * width) : (sx + sy * width);

    *pOutY = srcY[src_idx];
    *pOutU = srcU[src_idx];
    *pOutV = srcV[src_idx];
}

static void hex_init_atan_lut(hexmirror_t *s, int w, int h, int cx, int cy)
{
#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        const float fy = (float)(y - cy);
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const float fx = (float)(x - cx);
            s->atan_lut[row + x] = calc_angle(fy, fx);
        }
    }
}

static void hex_init_sqrt_lut(hexmirror_t *s, int w, int h, int cx, int cy)
{
#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        const int dy = y - cy;
        const int dy2 = dy * dy;
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const int dx = x - cx;
            s->sqrt_lut[row + x] = sqrtf((float)(dx * dx + dy2));
        }
    }
}

static void hex_init_sin_cos_lut(hexmirror_t *s)
{
    const float step = TWO_PI / (float)LUT_SIZE;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int i = 0; i < LUT_SIZE; i++) {
        const float a = (float)i * step;
        s->sin_lut[i] = sinf(a);
        s->cos_lut[i] = cosf(a);
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

    ve->limits[0][P_SIZE_LOG] = 102;      ve->limits[1][P_SIZE_LOG] = 1000;     ve->defaults[P_SIZE_LOG] = 562;
    ve->limits[0][P_OFFSET_ANGLE] = 0;    ve->limits[1][P_OFFSET_ANGLE] = 360;  ve->defaults[P_OFFSET_ANGLE] = 0;
    ve->limits[0][P_ANTICLOCKWISE] = 0;   ve->limits[1][P_ANTICLOCKWISE] = 1;   ve->defaults[P_ANTICLOCKWISE] = 1;
    ve->limits[0][P_SWAP] = 0;            ve->limits[1][P_SWAP] = 1;            ve->defaults[P_SWAP] = 0;
    ve->limits[0][P_ROT_SPEED] = 0;       ve->limits[1][P_ROT_SPEED] = 100;     ve->defaults[P_ROT_SPEED] = 0;

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
        "Rotation Speed"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 140, 920, 82, 100, 12, 480, 0, 4, 60, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, 360, 82, 100, 0, 380, 0, 1, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 100, 90, 100, 8, 400, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *hexmirror_malloc(int w, int h)
{
    hexmirror_t *s = (hexmirror_t *) vj_calloc(sizeof(hexmirror_t));

    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t *) vj_malloc((size_t)len * 3u);

    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    const size_t total = ((size_t)len * 2u) + ((size_t)LUT_SIZE * 2u);

    s->lut = (float *) vj_malloc(sizeof(float) * total);

    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->atan_lut = s->lut;
    s->sqrt_lut = s->atan_lut + len;
    s->cos_lut = s->sqrt_lut + len;
    s->sin_lut = s->cos_lut + LUT_SIZE;
    s->n_threads = vje_advise_num_threads(len);
    s->xangle = 0.0f;

    hex_init_atan_lut(s, w, h, w >> 1, h >> 1);
    hex_init_sqrt_lut(s, w, h, w >> 1, h >> 1);
    hex_init_sin_cos_lut(s);

    return (void *)s;
}

void hexmirror_free(void *ptr)
{
    hexmirror_t *s = (hexmirror_t *) ptr;

    free(s->lut);
    free(s->buf[0]);
    free(s);
}

void hexmirror_apply(void *ptr, VJFrame *frame, int *args)
{
    hexmirror_t *s = (hexmirror_t *)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
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
    const float sidex = side * RT3;
    const float sidey = side * 1.5f;
    const float hsidex = sidex * 0.5f;
    const float hsidey = sidey * 0.5f;
    const float inv_sidex = 1.0f / sidex;
    const float inv_sidey = 1.0f / sidey;
    const float side_off = side / FIVE_PI3;
    const float norm_speed = (float)hex_clampi(args[P_ROT_SPEED], 0, 100) * 0.01f;
    const float dir = args[P_ANTICLOCKWISE] ? 1.0f : -1.0f;
    const float rotation_step = norm_speed * norm_speed * 0.025f;

    s->xangle = hex_wrap_angle(s->xangle + rotation_step * dir);

    const float render_angle = hex_wrap_angle(s->xangle + ((float)hex_clampi(args[P_OFFSET_ANGLE], 0, 360) * (TWO_PI / 360.0f)));
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
            const float angle_rot = theta_base - delta;
            const int l_idx_rot = (int)(angle_rot * LUT_DIVISOR) & LUT_MASK;
            const float a_hex = r_base * cos_lut[l_idx_rot];
            const float b_hex = r_base * sin_lut[l_idx_rot];

            float h_x;
            float h_y;
            hex_calc_center(a_hex, b_hex, sidex, sidey, hsidex, hsidey, inv_sidex, inv_sidey, side_off, &h_x, &h_y);

            float center_hex_x;
            float center_hex_y;

            if(mru_valid && h_x == mru_hx && h_y == mru_hy) {
                center_hex_x = mru_cx;
                center_hex_y = mru_cy;
            }
            else {
                const float theta0 = hex_atan2_approx(h_y, h_x);
                const float r0 = hex_sqrt_approx(h_x * h_x + h_y * h_y);
                const float angle_f = theta0 + delta;
                const int l_idx_f = (int)(angle_f * LUT_DIVISOR) & LUT_MASK;

                center_hex_x = r0 * cos_lut[l_idx_f];
                center_hex_y = r0 * sin_lut[l_idx_f];

                mru_valid = 1;
                mru_hx = h_x;
                mru_hy = h_y;
                mru_cx = center_hex_x;
                mru_cy = center_hex_y;
            }

            const float bfi = center_hex_y - fi;
            const float afj = center_hex_x - (float)j;

            float theta = hex_atan2_approx(bfi, afj);
            float r = hex_sqrt_approx(bfi * bfi + afj * afj);

            if(r < 10.0f)
                r = 10.0f;

            hex_process_pixel(
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
