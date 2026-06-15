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
#include "complexsaturate.h"

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int complexsaturation_blend255(int a, int b, int q)
{
    return ((a * q) + (b * (255 - q))) / 255;
}

vj_effect *complexsaturation_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 9;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 4500;
    ve->defaults[1] = 0;
    ve->defaults[2] = 255;
    ve->defaults[3] = 0;
    ve->defaults[4] = 40;
    ve->defaults[5] = 160;
    ve->defaults[6] = 50;
    ve->defaults[7] = 50;
    ve->defaults[8] = 0;

    ve->limits[0][0] = 500; ve->limits[1][0] = 8500;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;   ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;   ve->limits[1][4] = 255;
    ve->limits[0][5] = 1;   ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;   ve->limits[1][6] = 256;
    ve->limits[0][7] = 1;   ve->limits[1][7] = 360;
    ve->limits[0][8] = 0;   ve->limits[1][8] = 1;

    ve->description = "Complex Saturation (Advanced)";
    ve->param_description = vje_build_param_list(ve->num_params, "Hue Angle", "Red", "Green", "Blue", "Threshold", "Solidity", "Saturation", "Hue Shift", "Swap Selection");
    ve->has_user = 0;
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;

    return ve;
}

void complexsaturation_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int angle = args[0];
    const int r = args[1];
    const int g = args[2];
    const int b = args[3];
    const int threshold = args[4];
    const int solidity = args[5];
    const int saturation = args[6];
    const int hue_shift = args[7];
    const int swap = args[8];
    const int len = frame->ssm ? frame->len : frame->uv_len;

    int iy = 0;
    int iu = 128;
    int iv = 128;

    _rgb2yuv(r, g, b, iy, iu, iv);

    const int scale = 4096;
    const float ut_f = (float)(iu - 128);
    const float vt_f = (float)(iv - 128);
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
    const float hue_rot = ((float)hue_shift / 180.0f) * (float)M_PI;
    const float sat_scale = (float)saturation * 0.01f;
    const int s = (int)(sinf(hue_rot) * 65536.0f * sat_scale);
    const int c = (int)(cosf(hue_rot) * 65536.0f * sat_scale);
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int pos = 0; pos < len; pos++)
    {
        const int uc = (int)Cb[pos] - 128;
        const int vc = (int)Cr[pos] - 128;
        const int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
        const int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
        const int abs_yy = yy < 0 ? -yy : yy;
        const int64_t dist_fp = ((int64_t)mag_fp - ((int64_t)xx << 12)) + ((int64_t)abs_yy * (int64_t)inv_wedge_slope_fp);
        int alpha = (int)(((dist_fp - (int64_t)black_clip_fp) * (int64_t)inv_range_fp) >> 20);

        alpha = clampi(alpha, 0, 255);

        if(swap)
            alpha = 255 - alpha;

        if(alpha > 0)
        {
            const int n_u = (c * uc - s * vc + 32768) >> 16;
            const int n_v = (s * uc + c * vc + 32768) >> 16;
            const int blend_u = complexsaturation_blend255(n_u, uc, alpha);
            const int blend_v = complexsaturation_blend255(n_v, vc, alpha);

            Cb[pos] = (uint8_t)(clampi(blend_u, -128, 127) + 128);
            Cr[pos] = (uint8_t)(clampi(blend_v, -128, 127) + 128);
        }
    }
}
