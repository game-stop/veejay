/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "cosmichue.h"

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *cosmichue_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 2000; ve->defaults[0] = 100;
    ve->limits[0][1] = 0; ve->limits[1][1] = 2000; ve->defaults[1] = 100;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255;  ve->defaults[2] = 100;
    ve->limits[0][3] = 0; ve->limits[1][3] = 3600; ve->defaults[3] = 0;

    ve->description = "Cosmic Hue";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Amplitude", "Frequency", "Opacity", "Hue Shift");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 120, 2000, 82, 100, 18, 480, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 60, 1900, 76, 100, 0, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 24, 255, 68, 96, 60, 760, 0, 1, 0, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, 3600, 78, 100, 0, 260, 0, 10, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void cosmichue_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int amplitude_arg = args[0];
    const int frequency_arg = args[1];
    const int opacity = args[2];
    const int hue_arg = args[3];
    const int len = frame->len;
    const int uv_len = frame->ssm ? frame->len : frame->uv_len;
    const int n_threads = vje_advise_num_threads(uv_len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const float amplitude = (float)amplitude_arg * 0.1f;
    const float frequency = (float)frequency_arg * 0.1f;
    float sin_lut[256];
    float cos_lut[256];

    for(int i = 0; i < 256; i++)
    {
        const float luminance = (float)i * (1.0f / 255.0f);
        const float phase = frequency * luminance;

        sin_lut[i] = amplitude * a_sin(phase);
        cos_lut[i] = amplitude * a_cos(phase);
    }

    float hue_shift = (float)hue_arg * 0.1f;
    hue_shift *= (float)(M_PI / 180.0);
    hue_shift = fmodf(hue_shift, (float)(2.0 * M_PI));

    if(hue_shift < 0.0f)
        hue_shift += (float)(2.0 * M_PI);

    const float cos_val = a_cos(hue_shift);
    const float sin_val = a_sin(hue_shift);
    const int inv_opacity = 255 - opacity;

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < uv_len; i++)
    {
        int u = (int)U[i] - 128;
        int v = (int)V[i] - 128;
        const uint8_t y = Y[i];

        u = (int)((float)u + sin_lut[y]);
        v = (int)((float)v + cos_lut[y]);

        int u_rot = 128 + (int)((float)u * cos_val - (float)v * sin_val);
        int v_rot = 128 + (int)((float)u * sin_val + (float)v * cos_val);

        u_rot = clampi(u_rot, 0, 255);
        v_rot = clampi(v_rot, 0, 255);

        U[i] = (uint8_t)((opacity * u_rot + inv_opacity * (int)U[i]) >> 8);
        V[i] = (uint8_t)((opacity * v_rot + inv_opacity * (int)V[i]) >> 8);
    }
}
