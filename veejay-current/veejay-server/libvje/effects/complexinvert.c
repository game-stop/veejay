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

vj_effect *complexinvert_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */

    ve->defaults[0] = 3000; /* angle */
    ve->defaults[1] = 0;    /* r */
    ve->defaults[2] = 0;    /* g */
    ve->defaults[3] = 255;  /* b */
    ve->defaults[4] = 0;    /* threshold */
    ve->defaults[5] = 160;    /* solidity */
    ve->defaults[6] = 200;  /* spill kill */
    ve->defaults[7] = 0;    /* swap */

    ve->limits[0][0] = 1;    ve->limits[1][0] = 9000;
    ve->limits[0][1] = 0;    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;    ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;    ve->limits[1][4] = 255;
    ve->limits[0][5] = 1;    ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;    ve->limits[1][6] = 255;
    ve->limits[0][7] = 0;    ve->limits[1][7] = 1;

    ve->has_user = 0;
    ve->description = "Complex Invert (RGB)";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    
    ve->param_description = vje_build_param_list(ve->num_params,
        "Angle", "Red", "Green", "Blue", "Threshold", "Solidity", "Spill Kill", "Swap Selection");
    return ve;
}
#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)

void complexinvert_apply(void *ptr, VJFrame *frame, int *args) {
    int i_angle = args[0];
    int r = args[1], g = args[2], b = args[3];
    int i_threshold = args[4]; 
    int i_solidity = args[5];
    int i_spill = args[6];
    int swap = args[7];
    int n_threads = vje_advise_num_threads(frame->len);
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const int len = frame->len;

    int iy, iu, iv;
    _rgb2yuv(r, g, b, iy, iu, iv);

    const float ut_f = (float)(iu - 128);
    const float vt_f = (float)(iv - 128);
    float mag_f = sqrtf(ut_f * ut_f + vt_f * vt_f);
    if (mag_f < 1.0f) mag_f = 1.0f;

    const int SCALE = 4096;
    const int cos_q_fp = (int)((ut_f / mag_f) * SCALE);
    const int sin_q_fp = (int)((vt_f / mag_f) * SCALE);

    const float angle_rad = ((float)i_angle / 100.0f) * (M_PI / 180.0f);
    const int inv_wedge_slope_fp = (int)((1.0f / tanf(angle_rad)) * SCALE);

    float diff = (float)i_solidity - (float)i_threshold;
    if (diff < 10.0f) diff = 10.0f;
    
    const int inv_range_fp = (int)((255.0f / diff) * (1 << 8));
    const int black_clip_fp = (int)(i_threshold * SCALE);
    const int spill_factor_fp = (int)((((float)i_spill / 255.0f) / mag_f) * SCALE);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int pos = 0; pos < len; pos++) {
        int uc = (int)Cb[pos] - 128;
        int vc = (int)Cr[pos] - 128;

        int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
        int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
        int abs_yy = (yy < 0) ? -yy : yy;

        int dist_fp = (((int)(mag_f * SCALE)) - (xx << 12)) + (abs_yy * inv_wedge_slope_fp);
        int alpha = ((dist_fp - black_clip_fp) * inv_range_fp) >> 20;

        if (alpha < 0) alpha = 0; else if (alpha > 255) alpha = 255;
        if (swap) alpha = 255 - alpha;

        int cur_u = uc;
        int cur_v = vc;
        if (xx > 0) {
            int suppress_fp = (xx * spill_factor_fp);
            if (suppress_fp > SCALE) suppress_fp = SCALE;
            cur_u -= (suppress_fp * (int)ut_f) >> 12;
            cur_v -= (suppress_fp * (int)vt_f) >> 12;
        }

        int inv_Y = 255 - Y[pos];
        int inv_Cb = -cur_u;
        int inv_Cr = -cur_v;

        int out_Y  = DIV255(Y[pos] * 255 + (inv_Y - Y[pos]) * alpha);
        int out_Cb = DIV255(cur_u * 255 + (inv_Cb - cur_u) * alpha);
        int out_Cr = DIV255(cur_v * 255 + (inv_Cr - cur_v) * alpha);

        Y[pos]  = (uint8_t)(out_Y < 0 ? 0 : (out_Y > 255 ? 255 : out_Y));
        Cb[pos] = (uint8_t)((out_Cb < -128 ? -128 : (out_Cb > 127 ? 127 : out_Cb)) + 128);
        Cr[pos] = (uint8_t)((out_Cr < -128 ? -128 : (out_Cr > 127 ? 127 : out_Cr)) + 128);
    }
}