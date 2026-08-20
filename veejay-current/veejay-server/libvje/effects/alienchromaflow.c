/*
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include "alienchromaflow.h"

#define INV_255 0.0039215686f
#define PI_X2 6.28318530718f
#define SIN_LUT_SIZE 4096
#define SIN_MASK (SIN_LUT_SIZE - 1)
#define SCALER_TO_LUT (SIN_LUT_SIZE / PI_X2)

static inline uint8_t clamp_u8(int x)
{
    return (uint8_t)((x | ((x | (255 - x)) >> 31)) & ~(x >> 31) & 255);
}

static inline float get_avg_luma_fast(const uint8_t *py, int i, int w, int h)
{
    const int sz = w * h;

    if(i < w || i >= sz - w)
        return py[i] * INV_255;

    float sum = py[i] + py[i - 1] + py[i + 1] + py[i - w] + py[i + w];
    return sum * 0.2f * INV_255;
}

typedef struct {
    int w;
    int h;
    float time;
    float *sin_lut;
    uint8_t contrast_lut[256];
    float last_contrast;
    int n_threads;
} alien_t;

vj_effect *alienchromaflow_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    ve->num_params = 10;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;  ve->limits[1][0] = 255; ve->defaults[0] = 128;
    ve->limits[0][1] = 0;  ve->limits[1][1] = 255; ve->defaults[1] = 90;
    ve->limits[0][2] = 0;  ve->limits[1][2] = 255; ve->defaults[2] = 180;
    ve->limits[0][3] = 0;  ve->limits[1][3] = 255; ve->defaults[3] = 40;
    ve->limits[0][4] = 0;  ve->limits[1][4] = 255; ve->defaults[4] = 32;
    ve->limits[0][5] = 0;  ve->limits[1][5] = 255; ve->defaults[5] = 128;
    ve->limits[0][6] = 0;  ve->limits[1][6] = 255; ve->defaults[6] = 40;
    ve->limits[0][7] = 0;  ve->limits[1][7] = 255; ve->defaults[7] = 220;
    ve->limits[0][8] = 0;  ve->limits[1][8] = 255; ve->defaults[8] = 128;
    ve->limits[0][9] = -1; ve->limits[1][9] = 1;   ve->defaults[9] = 1;

    ve->sub_format = 1;
    ve->description = "Alien Chromaflow Prism";
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Global Hue",
        "Rainbow Wrap",
        "Vibrance",
        "Pastel Glow",
        "Flux Speed",
        "Edge Softness",
        "Black Protect",
        "White Protect",
        "Luma Contrast",
        "Direction"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, 0, 255, 78, 100, 0, 120, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 32, 245, 70, 100, 30, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 80, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 72, 245, 84, 100, 0, 240, 0, 1, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 180, 76, 100, 0, 320, 0, 1, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, 96, 88, 100, 0, 240, 0, 1, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 16, 180, 42, 82, 160, 1100, 0, 1, 0, VJ_BEAT_COST_CHEAP, 54, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 64, 230, 76, 100, 0, 380, 0, 1, 0, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_SPEED, VJ_BEAT_F_DISCRETE, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_GATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, -1, 1, 100, 100, 0, 0, 80, 1, 80, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *alienchromaflow_malloc(int w, int h)
{
    alien_t *c = (alien_t *)vj_calloc(sizeof(alien_t));

    c->w = w;
    c->h = h;
    c->sin_lut = (float *)vj_malloc(SIN_LUT_SIZE * sizeof(float));

    for(int i = 0; i < SIN_LUT_SIZE; i++)
        c->sin_lut[i] = sinf(((float)i * PI_X2) / (float)SIN_LUT_SIZE);

    c->last_contrast = -1.0f;
    c->n_threads = vje_advise_num_threads(w * h);

    return c;
}

void alienchromaflow_free(void *ptr)
{
    if(ptr)
    {
        alien_t *n = (alien_t *)ptr;

        if(n->sin_lut)
            free(n->sin_lut);

        free(n);
    }
}

void alienchromaflow_apply(void *ptr, VJFrame *frame, int *args)
{
    alien_t *n = (alien_t *)ptr;
    const int sz = n->w * n->h;

    const float global_hue   = args[0] * INV_255 * PI_X2;
    const float rainbow      = args[1] * INV_255 * (PI_X2 * 4.0f);
    const float vibrance     = args[2] * INV_255 * 2.5f;
    const float pastel       = args[3] * INV_255 * 110.0f;
    const float speed        = args[4] * INV_255 * 0.085f;
    const float softness     = args[5] * INV_255;
    const float b_prot       = args[6] * INV_255;
    const float w_prot       = args[7] * INV_255;
    const float contrast_val = 0.5f + (args[8] * INV_255 * 1.5f);
    const float dir          = (float)args[9];

    if(contrast_val != n->last_contrast)
    {
        for(int i = 0; i < 256; i++)
            n->contrast_lut[i] = clamp_u8((int)(powf((float)i * INV_255, contrast_val) * 255.0f));

        n->last_contrast = contrast_val;
    }

    n->time += speed * dir;
    const float t = n->time;

    uint8_t *py = frame->data[0];
    uint8_t *pu = frame->data[1];
    uint8_t *pv = frame->data[2];

    #pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int i = 0; i < sz; i++)
    {
        const uint8_t y_orig = py[i];
        const float y_raw = (float)y_orig * INV_255;

        const float y_avg = get_avg_luma_fast(py, i, n->w, n->h);
        const float y_mixed = (y_raw * (1.0f - softness)) + (y_avg * softness);

        const float angle = global_hue + (y_mixed * rainbow * dir) + t;
        const int s_idx = (int)(angle * SCALER_TO_LUT) & SIN_MASK;
        const int c_idx = (s_idx + (SIN_LUT_SIZE / 4)) & SIN_MASK;

        const float s = n->sin_lut[s_idx];
        const float c = n->sin_lut[c_idx];

        const float u_in = (float)pu[i] - 128.0f;
        const float v_in = (float)pv[i] - 128.0f;

        float u_out = ((u_in * c) - (v_in * s)) * vibrance;
        float v_out = ((u_in * s) + (v_in * c)) * vibrance;

        const float m_b = (y_raw < b_prot)
            ? (y_raw / (b_prot + 0.001f))
            : 1.0f;

        const float m_w = (y_raw > w_prot)
            ? (1.0f - ((y_raw - w_prot) / (1.0f - w_prot + 0.001f)))
            : 1.0f;

        const float mask = (m_b * m_b) * (m_w * m_w);

        u_out += s * pastel * mask;
        v_out += c * pastel * mask;

        py[i] = n->contrast_lut[y_orig];
        pu[i] = clamp_u8((int)(u_out + 128.5f));
        pv[i] = clamp_u8((int)(v_out + 128.5f));
    }
}
