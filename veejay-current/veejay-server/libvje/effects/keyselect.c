/*
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include <math.h>

typedef uint8_t (*blend_func)(uint8_t a, uint8_t b);

#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)

static uint8_t blend_func1(uint8_t a, uint8_t b) { return 0xff - abs(0xff - a - b); }
static uint8_t blend_func2(uint8_t a, uint8_t b) { return (a == 0) ? 0 : CLAMP_Y(255 - ((255-b) * (255-b))/a); }
static uint8_t blend_func3(uint8_t a, uint8_t b) { return (uint8_t)(((uint16_t)a * b) >> 8); }
static uint8_t blend_func4(uint8_t a, uint8_t b) { int c = 0xff - b; return (c == 0) ? 0xff : CLAMP_Y((a*a)/c); }
static uint8_t blend_func5(uint8_t a, uint8_t b) { int c = 0xff - b; return (c == 0) ? b : CLAMP_Y(b * 0xff / c); }
static uint8_t blend_func6(uint8_t a, uint8_t b) { return CLAMP_Y(a + (b - 0xff)); }
static uint8_t blend_func7(uint8_t a, uint8_t b) { return CLAMP_Y(a + (2 * b) - 255); }
static uint8_t blend_func8(uint8_t a, uint8_t b) {
    int c = (b < 128) ? (a * b) >> 7 : 255 - (((255 - b) * (255 - a)) >> 7);
    return CLAMP_Y(c);
}

static blend_func get_blend_func(int mode) {
    switch(mode) {
        case 0: return blend_func1; case 1: return blend_func2;
        case 2: return blend_func3; case 3: return blend_func4;
        case 4: return blend_func5; case 5: return blend_func6;
        case 6: return blend_func7; case 7: return blend_func8;
        default: return blend_func1;
    }
}

vj_effect *keyselect_init(int w, int h) {
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 4500; /* Hue Angle */
    ve->defaults[1] = 0;    /* R */
    ve->defaults[2] = 0;    /* G */
    ve->defaults[3] = 255;  /* B */
    ve->defaults[4] = 40;   /* Threshold */
    ve->defaults[5] = 160;  /* Solidity */
    ve->defaults[6] = 3;    /* Blend mode */
    ve->defaults[7] = 0;

    ve->limits[0][0] = 500;  ve->limits[1][0] = 8500;
    ve->limits[0][1] = 0;    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;    ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;    ve->limits[1][4] = 255;
    ve->limits[0][5] = 1;    ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;    ve->limits[1][6] = 7;
    ve->limits[0][7] = 0;    ve->limits[1][7] = 1;

    ve->description = "Blend by Color Key (Advanced)";
    ve->param_description = vje_build_param_list(ve->num_params,
        "Hue Angle", "Red", "Green", "Blue", "Threshold", "Solidity", "Blend mode", "Swap Selection");

    ve->has_user = 0;
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    return ve;
}

void keyselect_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {

    int iy, iu, iv;
    int n_threads = vje_advise_num_threads(frame->len);

    _rgb2yuv(args[1], args[2], args[3], iy, iu, iv);

    const int SCALE = 4096;
    const float ut_f = (float)iu - 128.0f;
    const float vt_f = (float)iv - 128.0f;
    float mag_f = sqrtf(ut_f * ut_f + vt_f * vt_f);
    if (mag_f < 1.0f) mag_f = 1.0f;

    const int mag_fp   = (int)(mag_f * SCALE);
    const int cos_q_fp = (int)((ut_f / mag_f) * SCALE);
    const int sin_q_fp = (int)((vt_f / mag_f) * SCALE);

    const float angle_rad = ((float)args[0] / 100.0f) * (M_PI / 180.0f);
    const int inv_wedge_slope_fp = (int)((1.0f / tanf(angle_rad)) * SCALE);

    const float diff = (float)args[5] - (float)args[4];
    const int inv_range_fp = (int)((255.0f / (diff < 1.0f ? 1.0f : diff)) * (1 << 8));
    const int black_clip_fp = (int)(args[4] * SCALE);

    blend_func blend_pixel = get_blend_func(args[6]);
    const int swap = args[7];

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *src_Y = swap ? frame2->data[0] : frame->data[0];
    uint8_t *src_Cb = swap ? frame2->data[1] : frame->data[1];
    uint8_t *src_Cr = swap ? frame2->data[2] : frame->data[2];

    const uint8_t *bg_Y = swap ? frame->data[0] : frame2->data[0];
    const uint8_t *bg_U = swap ? frame->data[1] : frame2->data[1];
    const uint8_t *bg_V = swap ? frame->data[2] : frame2->data[2];

    const int len = frame->len;

    #pragma omp parallel num_threads(n_threads)
    {
        #pragma omp for schedule(static)
        for (int pos = 0; pos < len; pos++) {
            int uc = (int)Cb[pos] - 128;
            int vc = (int)Cr[pos] - 128;

            int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
            int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
            int abs_yy = (yy < 0) ? -yy : yy;

            int dist_fp = (mag_fp - (xx << 12)) + (abs_yy * inv_wedge_slope_fp);
            int alpha = ((dist_fp - black_clip_fp) * inv_range_fp) >> 20;

            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;

            int alpha_inv = 255 - alpha;

            if (alpha_inv > 0) {
                uint8_t blended_Y = blend_pixel(src_Y[pos], bg_Y[pos]);
                uint8_t b_Cb = CLAMP_Y(((bg_Y[pos] * (src_Cb[pos] - bg_U[pos])) >> 8) + src_Cb[pos]);
                uint8_t b_Cr = CLAMP_Y(((bg_Y[pos] * (src_Cr[pos] - bg_V[pos])) >> 8) + src_Cr[pos]);

                if (alpha_inv == 255) {
                    Y[pos]  = blended_Y;
                    Cb[pos] = b_Cb;
                    Cr[pos] = b_Cr;
                } else {
                    Y[pos]  = DIV255(blended_Y * alpha_inv + Y[pos]  * alpha);
                    Cb[pos] = DIV255(b_Cb      * alpha_inv + Cb[pos] * alpha);
                    Cr[pos] = DIV255(b_Cr      * alpha_inv + Cr[pos] * alpha);
                }
            }
        }
    }
}