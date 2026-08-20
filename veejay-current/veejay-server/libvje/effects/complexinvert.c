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
#include "complexinvert.h"

#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *complexinvert_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 3000;
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;
    ve->defaults[3] = 255;
    ve->defaults[4] = 0;
    ve->defaults[5] = 160;
    ve->defaults[6] = 200;
    ve->defaults[7] = 0;

    ve->limits[0][0] = 1; ve->limits[1][0] = 9000;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255;
    ve->limits[0][3] = 0; ve->limits[1][3] = 255;
    ve->limits[0][4] = 0; ve->limits[1][4] = 255;
    ve->limits[0][5] = 1; ve->limits[1][5] = 255;
    ve->limits[0][6] = 0; ve->limits[1][6] = 255;
    ve->limits[0][7] = 0; ve->limits[1][7] = 1;

    ve->has_user = 0;
    ve->description = "Complex Invert (RGB)";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Angle", "Red", "Green", "Blue", "Threshold", "Solidity", "Spill Kill", "Swap Selection");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 500, 8500, 62, 92, 0, 260, 0, 25, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 170, 78, 100, 15, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 96, 1, 0, VJ_BEAT_GROUP_ASCENDING, 10),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 96, 255, 66, 94, 80, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 1, 1, VJ_BEAT_GROUP_ASCENDING, 10),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 255, 74, 100, 0, 460, 0, 1, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void complexinvert_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int i_angle = args[0];
    const int r = args[1];
    const int g = args[2];
    const int b = args[3];
    const int i_threshold = args[4];
    const int i_solidity = args[5];
    const int i_spill = args[6];
    const int swap = args[7];
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int iy = 0;
    int iu = 128;
    int iv = 128;

    _rgb2yuv(r, g, b, iy, iu, iv);

    const float ut_f = (float)(iu - 128);
    const float vt_f = (float)(iv - 128);
    float mag_f = sqrtf(ut_f * ut_f + vt_f * vt_f);

    if(mag_f < 1.0f)
        mag_f = 1.0f;

    const int scale = 4096;
    const int cos_q_fp = (int)((ut_f / mag_f) * (float)scale);
    const int sin_q_fp = (int)((vt_f / mag_f) * (float)scale);
    const float angle_rad = ((float)i_angle / 100.0f) * (float)(M_PI / 180.0f);
    float tan_v = tanf(angle_rad);

    if(tan_v > -0.0001f && tan_v < 0.0001f)
        tan_v = tan_v < 0.0f ? -0.0001f : 0.0001f;

    const int inv_wedge_slope_fp = (int)((1.0f / tan_v) * (float)scale);
    float diff = (float)i_solidity - (float)i_threshold;

    if(diff < 10.0f)
        diff = 10.0f;

    const int inv_range_fp = (int)((255.0f / diff) * (float)(1 << 8));
    const int black_clip_fp = i_threshold * scale;
    const int spill_factor_fp = (int)((((float)i_spill / 255.0f) / mag_f) * (float)scale);
    const int mag_fp = (int)(mag_f * (float)scale);
    const int ut_i = (int)ut_f;
    const int vt_i = (int)vt_f;
    const int n_threads = vje_advise_num_threads(len);

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

        int cur_u = uc;
        int cur_v = vc;

        if(xx > 0)
        {
            int suppress_fp = xx * spill_factor_fp;

            if(suppress_fp > scale)
                suppress_fp = scale;

            cur_u -= (suppress_fp * ut_i) >> 12;
            cur_v -= (suppress_fp * vt_i) >> 12;
        }

        const int inv_Y = 255 - Y[pos];
        const int inv_Cb = -cur_u;
        const int inv_Cr = -cur_v;
        const int out_Y = DIV255((int)Y[pos] * 255 + (inv_Y - (int)Y[pos]) * alpha);
        const int out_Cb = DIV255(cur_u * 255 + (inv_Cb - cur_u) * alpha);
        const int out_Cr = DIV255(cur_v * 255 + (inv_Cr - cur_v) * alpha);

        Y[pos] = (uint8_t)clampi(out_Y, 0, 255);
        Cb[pos] = (uint8_t)(clampi(out_Cb, -128, 127) + 128);
        Cr[pos] = (uint8_t)(clampi(out_Cr, -128, 127) + 128);
    }
}
