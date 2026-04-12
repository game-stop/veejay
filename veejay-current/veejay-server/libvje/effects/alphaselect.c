/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nelburg@gmail.com>
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
#include "alphaselect.h"
#include <math.h>

vj_effect *alphaselect_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 4500; /* Hue Angle */
    ve->defaults[1] = 0;    /* R */
    ve->defaults[2] = 255;  /* G */
    ve->defaults[3] = 0;    /* B */
    ve->defaults[4] = 40;   /* Threshold (Black Clip) */
    ve->defaults[5] = 160;  /* Solidity (Range) */
    ve->defaults[6] = 1;    /* Show Mask (Default to 1 so mask is visible on activation) */
    ve->defaults[7] = 0;    /* Invert */

    ve->limits[0][0] = 500;  ve->limits[1][0] = 8500;
    ve->limits[0][1] = 0;    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;    ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;    ve->limits[1][4] = 255;
    ve->limits[0][5] = 1;    ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;    ve->limits[1][6] = 1;
    ve->limits[0][7] = 0;    ve->limits[1][7] = 1;

    ve->has_user = 0;
    ve->description = "Alpha: Advanced Chroma Key Mask";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;

    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL | FLAG_ALPHA_SRC_A;

    ve->param_description = vje_build_param_list(ve->num_params,
        "Hue Angle", "Red", "Green", "Blue", "Threshold", "Solidity", "Show Mask", "Invert");

    return ve;
}

#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)

void alphaselect_apply(void *ptr, VJFrame *frame, int *args)
{
    const int i_angle   = args[0];
    const int r         = args[1];
    const int g         = args[2];
    const int b         = args[3];
    const int threshold = args[4];
    const int solidity  = args[5];
    const int show_mask = args[6];
    const int invert    = args[7];

    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A  = frame->data[3];

    int iy = 0, iu = 128, iv = 128;
    _rgb2yuv(r, g, b, iy, iu, iv);

    const int SCALE = 4096;

    const float ut_f = (float)iu - 128.0f;
    const float vt_f = (float)iv - 128.0f;

    float mag_f = sqrtf(ut_f * ut_f + vt_f * vt_f);
    if (mag_f < 1.0f) mag_f = 1.0f;

    const int mag_fp   = (int)(mag_f * SCALE);
    const int cos_q_fp = (int)((ut_f / mag_f) * SCALE);
    const int sin_q_fp = (int)((vt_f / mag_f) * SCALE);

    float angle_rad = ((float)i_angle / 100.0f) * ((float)M_PI / 180.0f);
    float t = tanf(angle_rad);
    if (fabsf(t) < 1e-6f) t = 1e-6f;

    const int inv_wedge_slope_fp = (int)((1.0f / t) * SCALE);

    float diff = (float)solidity - (float)threshold;
    if (diff < 1.0f) diff = 1.0f;

    const int inv_range_fp = (int)((255.0f / diff) * (1 << 8));
    const int black_clip_fp = (int)(threshold * SCALE);

    #pragma omp parallel num_threads(n_threads)
    {
        #pragma omp for schedule(static)
        for (int pos = 0; pos < len; pos++)
        {
            const int uc = (int)Cb[pos] - 128;
            const int vc = (int)Cr[pos] - 128;

            const int xx = (uc * cos_q_fp + vc * sin_q_fp);
            const int yy = (vc * cos_q_fp - uc * sin_q_fp);

            const int abs_yy = (yy < 0) ? -yy : yy;
            const int dx = mag_fp - xx;
            const int abs_dx = (dx < 0) ? -dx : dx;

            int dist_fp =
                abs_dx +
                ((abs_yy * inv_wedge_slope_fp) >> 12);

            int alpha = ((dist_fp - black_clip_fp) * inv_range_fp) >> 20;

            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;

            if (invert)
                alpha = 255 - alpha;

            const int alpha_inv = 255 - alpha;

            if (show_mask)
            {
                A[pos]  = (uint8_t)alpha;
                Y[pos]  = (uint8_t)alpha;
                Cb[pos] = 128;
                Cr[pos] = 128;
            }
            else
            {
                if (alpha_inv == 0)
                {
                    continue;
                }

                if (alpha_inv == 255)
                {
                    Y[pos]  = Y[pos];
                    Cb[pos] = Cb[pos];
                    Cr[pos] = Cr[pos];
                }
                else
                {
                    Y[pos]  = DIV255(Y[pos]  * alpha_inv + Y[pos]  * alpha);
                    Cb[pos] = DIV255(Cb[pos] * alpha_inv + Cb[pos] * alpha);
                    Cr[pos] = DIV255(Cr[pos] * alpha_inv + Cr[pos] * alpha);
                }
            }
        }
    }
}