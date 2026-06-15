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
#include "complexopacity.h"

#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)

static inline int complexopacity_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *complexopacity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 7;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 4500;
    ve->defaults[1] = 0;
    ve->defaults[2] = 255;
    ve->defaults[3] = 0;
    ve->defaults[4] = 40;
    ve->defaults[5] = 160;
    ve->defaults[6] = 0;

    ve->limits[0][0] = 500; ve->limits[1][0] = 8500;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;   ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;   ve->limits[1][4] = 255;
    ve->limits[0][5] = 1;   ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;   ve->limits[1][6] = 1;

    ve->description = "Complex Overlay (Advanced)";
    ve->param_description = vje_build_param_list(ve->num_params, "Hue Angle", "Red", "Green", "Blue", "Threshold", "Solidity", "Swap Selection");
    ve->has_user = 0;
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 1;

    return ve;
}

void complexopacity_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void) ptr;

    const int angle_arg = args[0];
    const int r_arg = args[1];
    const int g_arg = args[2];
    const int b_arg = args[3];
    const int threshold_arg = args[4];
    const int solidity_arg = args[5];
    const int swap_arg = args[6];

    const int angle = complexopacity_clampi(angle_arg, 500, 8500);
    const int r = complexopacity_clampi(r_arg, 0, 255);
    const int g = complexopacity_clampi(g_arg, 0, 255);
    const int b = complexopacity_clampi(b_arg, 0, 255);
    const int threshold = complexopacity_clampi(threshold_arg, 0, 255);
    const int solidity = complexopacity_clampi(solidity_arg, 1, 255);
    const int swap = complexopacity_clampi(swap_arg, 0, 1);
    const int len = frame->len;

    int iy = 0;
    int iu = 128;
    int iv = 128;

    _rgb2yuv(r, g, b, iy, iu, iv);

    const int scale = 4096;
    const float ut_f = (float)iu - 128.0f;
    const float vt_f = (float)iv - 128.0f;
    float mag_f = sqrtf(ut_f * ut_f + vt_f * vt_f);

    if(mag_f < 1.0f)
        mag_f = 1.0f;

    const int mag_fp = (int)(mag_f * (float)scale);
    const int cos_q_fp = (int)((ut_f / mag_f) * (float)scale);
    const int sin_q_fp = (int)((vt_f / mag_f) * (float)scale);
    const float angle_rad = ((float)angle / 100.0f) * (float)(M_PI / 180.0f);
    float tan_v = tanf(angle_rad);

    if(tan_v > -0.0001f && tan_v < 0.0001f)
        tan_v = tan_v < 0.0f ? -0.0001f : 0.0001f;

    const int inv_wedge_slope_fp = (int)((1.0f / tan_v) * (float)scale);
    float diff = (float)solidity - (float)threshold;

    if(diff < 1.0f)
        diff = 1.0f;

    const int inv_range_fp = (int)((255.0f / diff) * (float)(1 << 8));
    const int black_clip_fp = threshold * scale;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int pos = 0; pos < len; pos++)
    {
        const int uc = (int)Cb[pos] - 128;
        const int vc = (int)Cr[pos] - 128;
        const int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
        const int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
        const int abs_yy = yy < 0 ? -yy : yy;
        const int64_t dist_fp = ((int64_t)mag_fp - ((int64_t)xx << 12)) + ((int64_t)abs_yy * (int64_t)inv_wedge_slope_fp);
        int alpha = (int)(((dist_fp - (int64_t)black_clip_fp) * (int64_t)inv_range_fp) >> 20);

        alpha = complexopacity_clampi(alpha, 0, 255);

        if(swap)
            alpha = 255 - alpha;

        if(alpha <= 0)
        {
            Y[pos] = Y2[pos];
            Cb[pos] = Cb2[pos];
            Cr[pos] = Cr2[pos];
        }
        else if(alpha < 255)
        {
            const int invA = 255 - alpha;

            Y[pos] = (uint8_t)DIV255((int)Y[pos] * alpha + (int)Y2[pos] * invA);
            Cb[pos] = (uint8_t)DIV255((int)Cb[pos] * alpha + (int)Cb2[pos] * invA);
            Cr[pos] = (uint8_t)DIV255((int)Cr[pos] * alpha + (int)Cr2[pos] * invA);
        }
    }
}
