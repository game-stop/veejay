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
#include "complexthreshold.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)
#define DIV3(x) (((x) * 21846) >> 16)

typedef struct {
    int n_threads;
    uint8_t *alpha_map;
    uint8_t *alpha_temp;
    uint8_t gamma_lut[256];
} promixer_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t complexthreshold_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

vj_effect *complexthreshold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 12;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0]  = 1200;
    ve->defaults[1]  = 120;
    ve->defaults[2]  = 20;
    ve->defaults[3]  = 240;
    ve->defaults[4]  = 128;
    ve->defaults[5]  = 15;
    ve->defaults[6]  = 20;
    ve->defaults[7]  = 160;
    ve->defaults[8]  = 60;
    ve->defaults[9]  = 20;
    ve->defaults[10] = 0;
    ve->defaults[11] = 0;

    for(int i = 0; i < ve->num_params; i++)
    {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 255;
    }

    ve->limits[1][0] = 3600;
    ve->limits[0][1] = 1;
    ve->limits[1][10] = 1;
    ve->limits[1][11] = 2;

    ve->description = "Kromatica Mixer (High-Fidelity Keyer)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    ve->param_description = vje_build_param_list(ve->num_params,
        "Key Color", "Key Reach", "Clip Black", "Clip White", "Matte Gamma",
        "Sat Gate", "Shadow Prot", "Spill Amount", "Spill Balance", "Edge Blur",
        "Invert Matte", "Output View");

    (void) w;
    (void) h;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, 3600, 50, 82, 0, 260, 0, 10, 0, VJ_BEAT_COST_CHEAP, 68, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 24, 230, 82, 100, 20, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SQUARE, 0, 112, 62, 94, 0, 560, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 1, 0, VJ_BEAT_GROUP_ASCENDING, 12),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 150, 255, 52, 86, 80, 1000, 0, 1, 0, VJ_BEAT_COST_CHEAP, 58, 1, 1, VJ_BEAT_GROUP_ASCENDING, 12),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 72, 220, 54, 88, 40, 720, 0, 1, 0, VJ_BEAT_COST_CHEAP, 64, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 120, 64, 96, 0, 440, 0, 1, 0, VJ_BEAT_COST_CHEAP, 74, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_LOW_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, 128, 46, 78, 80, 1000, 0, 1, 0, VJ_BEAT_COST_CHEAP, 48, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 230, 72, 100, 0, 460, 0, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, 24, 232, 50, 88, 60, 760, 0, 1, 0, VJ_BEAT_COST_CHEAP, 52, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 96, 78, 100, 0, 420, 0, 1, 40, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *complexthreshold_malloc(int w, int h)
{
    promixer_t *m = (promixer_t*) vj_calloc(sizeof(promixer_t));

    if(!m)
        return NULL;

    const int len = w * h;

    m->n_threads = vje_advise_num_threads(len);
    m->alpha_map = (uint8_t*) vj_malloc(len);
    m->alpha_temp = (uint8_t*) vj_malloc(len);

    if(!m->alpha_map || !m->alpha_temp)
    {
        complexthreshold_free(m);
        return NULL;
    }

    return m;
}

void complexthreshold_free(void *ptr)
{
    promixer_t *m = (promixer_t*)ptr;

    if(!m)
        return;

    if(m->alpha_map)
        free(m->alpha_map);

    if(m->alpha_temp)
        free(m->alpha_temp);

    free(m);
}

void complexthreshold_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    promixer_t *mk = (promixer_t*) ptr;

    const int key_color = clampi(args[0], 0, 3600);
    const int key_reach = clampi(args[1], 1, 255);
    const int clip_black = clampi(args[2], 0, 255);
    const int clip_white = clampi(args[3], 0, 255);
    const int matte_gamma = clampi(args[4], 0, 255);
    const int sat_gate = clampi(args[5], 0, 255);
    const int shadow_prot = clampi(args[6], 0, 255);
    const int spill_amt = clampi(args[7], 0, 255);
    const int spill_balance = clampi(args[8], 0, 255);
    const int soft = clampi(args[9], 0, 255);
    const int invert_matte = args[10] ? 1 : 0;
    const int output_view = (args[11] >= 0 && args[11] <= 2) ? args[11] : 0;

    const int w = frame->width;
    const int h = frame->height;
    const int len = w * h;

    const float angle_rad = ((float)key_color / 10.0f) * (float)(M_PI / 180.0f);
    const float target_u = cosf(angle_rad) * 127.0f;
    const float target_v = sinf(angle_rad) * 127.0f;
    float mag_target = sqrtf(target_u * target_u + target_v * target_v);

    if(mag_target < 1.0f)
        mag_target = 1.0f;

    const int scale = 4096;
    const int mag_target_fp = (int)(mag_target * (float)scale);
    const int cos_q_fp = (int)((target_u / mag_target) * (float)scale);
    const int sin_q_fp = (int)((target_v / mag_target) * (float)scale);

    const float g_val = fmaxf((float)matte_gamma / 128.0f, 0.1f);

    for(int i = 0; i < 256; i++)
        mk->gamma_lut[i] = complexthreshold_u8((int)(powf((float)i / 255.0f, 1.0f / g_val) * 255.0f));

    const int sat_gate_sq = sat_gate * sat_gate;
    const int matte_range = clampi(clip_white - clip_black, 1, 255);
    const int m_range_inv_fp = (255 << 12) / matte_range;
    const int inv_c_thresh_fp = (1 << 24) / (key_reach * scale);

    int spill_final_fp = 0;

    if(spill_balance >= 128)
    {
        float spill_softness = 1.0f - ((float)(spill_balance - 128) / 160.0f);

        if(spill_softness < 0.0f)
            spill_softness = 0.0f;

        spill_final_fp = (int)(((float)spill_amt / 255.0f) * spill_softness * 4096.0f);
    }

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    #pragma omp parallel num_threads(mk->n_threads)
    {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
        {
            const int uc = (int)Cb2[i] - 128;
            const int vc = (int)Cr2[i] - 128;
            int a = 0;

            if((uc * uc + vc * vc) >= sat_gate_sq)
            {
                const int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
                const int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
                const int abs_yy = yy < 0 ? -yy : yy;
                const int dist_fp = mag_target_fp - (xx << 12) + (abs_yy * 16);

                if(dist_fp < (key_reach * scale))
                    a = 255 - ((dist_fp * inv_c_thresh_fp) >> 16);
            }

            if(Y2[i] < shadow_prot)
            {
                const int l_a = (shadow_prot - Y2[i]) << 2;

                if(l_a > a)
                    a = l_a;
            }

            mk->alpha_map[i] = complexthreshold_u8(a);
        }

        if(soft > 0 && w > 2 && h > 2)
        {
            #pragma omp for schedule(static)
            for(int y = 0; y < h; y++)
            {
                uint8_t *restrict in = mk->alpha_map + y * w;
                uint8_t *restrict out = mk->alpha_temp + y * w;

                out[0] = in[0];
                out[w - 1] = in[w - 1];

                for(int x = 1; x < w - 1; x++)
                    out[x] = (uint8_t)DIV3(in[x - 1] + in[x] + in[x + 1]);
            }

            #pragma omp for schedule(static)
            for(int y = 1; y < h - 1; y++)
            {
                uint8_t *restrict m = mk->alpha_temp + y * w;
                uint8_t *restrict t = mk->alpha_temp + (y - 1) * w;
                uint8_t *restrict b = mk->alpha_temp + (y + 1) * w;
                uint8_t *restrict dest = mk->alpha_map + y * w;

                for(int x = 0; x < w; x++)
                    dest[x] = (uint8_t)DIV3(t[x] + m[x] + b[x]);
            }
        }

        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
        {
            const int raw_a = mk->alpha_map[i];
            const int alpha_f_fp = (raw_a - clip_black) * m_range_inv_fp;
            const uint8_t snapped_a = complexthreshold_u8(alpha_f_fp >> 12);
            int a = mk->gamma_lut[snapped_a];

            if(invert_matte)
                a = 255 - a;

            if(output_view == 1)
            {
                Y[i] = (uint8_t)a;
                Cb[i] = 128;
                Cr[i] = 128;
                continue;
            }

            const int uc = (int)Cb2[i] - 128;
            const int vc = (int)Cr2[i] - 128;
            const int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;

            int sY = Y2[i];
            int sCb = Cb2[i];
            int sCr = Cr2[i];

            if(xx > 2)
            {
                if(spill_balance >= 128)
                {
                    sCb = clampi((int)Cb2[i] - ((uc * spill_final_fp) >> 12), 0, 255);
                    sCr = clampi((int)Cr2[i] - ((vc * spill_final_fp) >> 12), 0, 255);
                    sY = clampi((int)Y2[i] + ((spill_balance - 128) >> 3), 0, 255);
                }
                else
                {
                    const int spill_f = (xx * spill_amt) >> 8;

                    sY = clampi((int)Y2[i] + ((spill_f * spill_balance) >> 6), 0, 255);
                    sCb = clampi((int)Cb2[i] - ((spill_f * cos_q_fp) >> 12), 0, 255);
                    sCr = clampi((int)Cr2[i] - ((spill_f * sin_q_fp) >> 12), 0, 255);
                }
            }

            if(output_view == 2)
            {
                Y[i] = (uint8_t)sY;
                Cb[i] = (uint8_t)sCb;
                Cr[i] = (uint8_t)sCr;
                continue;
            }

            if(a >= 254)
                continue;

            if(a <= 1)
            {
                Y[i] = (uint8_t)sY;
                Cb[i] = (uint8_t)sCb;
                Cr[i] = (uint8_t)sCr;
                continue;
            }

            const int ia = 255 - a;

            Y[i] = (uint8_t)DIV255((int)Y[i] * a + sY * ia);
            Cb[i] = (uint8_t)DIV255((int)Cb[i] * a + sCb * ia);
            Cr[i] = (uint8_t)DIV255((int)Cr[i] * a + sCr * ia);
        }
    }
}
