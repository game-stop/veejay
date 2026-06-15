/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RGBKEYSMOOTH_PARAMS 12
#define RGBKEYSMOOTH_MAG_LUT_SIZE 32769

#define P_HUE_ANGLE      0
#define P_RED            1
#define P_GREEN          2
#define P_BLUE           3
#define P_MATTE_MIN      4
#define P_MATTE_MAX      5
#define P_LUMA_MIN       6
#define P_LUMA_MAX       7
#define P_SPILL_AMOUNT   8
#define P_SPILL_RECOVERY 9
#define P_VIEW_MODE      10
#define P_SOFTNESS       11

#define RGBKEYSMOOTH_DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)

typedef struct {
    uint8_t *alpha_map;
    uint8_t *alpha_temp;
    float mag_lut[RGBKEYSMOOTH_MAG_LUT_SIZE];
    float inv_mag_lut[RGBKEYSMOOTH_MAG_LUT_SIZE];
    int n_threads;
} rgbkeysmooth_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float rgbkeysmooth_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t rgbkeysmooth_u8f(float v)
{
    return (uint8_t)clampi((int)v, 0, 255);
}

vj_effect *rgbkeysmooth_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RGBKEYSMOOTH_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    ve->defaults[P_HUE_ANGLE] = 4500;
    ve->defaults[P_RED] = 0;
    ve->defaults[P_GREEN] = 255;
    ve->defaults[P_BLUE] = 0;
    ve->defaults[P_MATTE_MIN] = 20;
    ve->defaults[P_MATTE_MAX] = 180;
    ve->defaults[P_LUMA_MIN] = 20;
    ve->defaults[P_LUMA_MAX] = 235;
    ve->defaults[P_SPILL_AMOUNT] = 160;
    ve->defaults[P_SPILL_RECOVERY] = 100;
    ve->defaults[P_VIEW_MODE] = 0;
    ve->defaults[P_SOFTNESS] = 60;

    ve->limits[0][P_HUE_ANGLE] = 500;     ve->limits[1][P_HUE_ANGLE] = 8500;
    ve->limits[0][P_RED] = 0;             ve->limits[1][P_RED] = 255;
    ve->limits[0][P_GREEN] = 0;           ve->limits[1][P_GREEN] = 255;
    ve->limits[0][P_BLUE] = 0;            ve->limits[1][P_BLUE] = 255;
    ve->limits[0][P_MATTE_MIN] = 0;       ve->limits[1][P_MATTE_MIN] = 255;
    ve->limits[0][P_MATTE_MAX] = 0;       ve->limits[1][P_MATTE_MAX] = 255;
    ve->limits[0][P_LUMA_MIN] = 0;        ve->limits[1][P_LUMA_MIN] = 255;
    ve->limits[0][P_LUMA_MAX] = 0;        ve->limits[1][P_LUMA_MAX] = 255;
    ve->limits[0][P_SPILL_AMOUNT] = 0;    ve->limits[1][P_SPILL_AMOUNT] = 255;
    ve->limits[0][P_SPILL_RECOVERY] = 0;  ve->limits[1][P_SPILL_RECOVERY] = 255;
    ve->limits[0][P_VIEW_MODE] = 0;       ve->limits[1][P_VIEW_MODE] = 2;
    ve->limits[0][P_SOFTNESS] = 0;        ve->limits[1][P_SOFTNESS] = 255;

    ve->description = "Master Chroma Key";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Hue Angle",
        "Red",
        "Green",
        "Blue",
        "Matte Min",
        "Matte Max",
        "Luma Min",
        "Luma Max",
        "Spill Amount",
        "Spill Recovery",
        "View Mode",
        "Softness"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_VIEW_MODE], P_VIEW_MODE, "Composite", "Matte", "Spill Only");

    return ve;
}

void *rgbkeysmooth_malloc(int w, int h)
{
    rgbkeysmooth_t *r = (rgbkeysmooth_t*)vj_calloc(sizeof(rgbkeysmooth_t));

    if(!r)
        return NULL;

    const int len = w * h;

    r->alpha_map = (uint8_t*)vj_malloc((size_t)len * 2u);

    if(!r->alpha_map) {
        free(r);
        return NULL;
    }

    r->alpha_temp = r->alpha_map + len;
    r->n_threads = vje_advise_num_threads(len);

    for(int i = 0; i < RGBKEYSMOOTH_MAG_LUT_SIZE; i++) {
        const float m = sqrtf((float)i);

        r->mag_lut[i] = m < 0.0001f ? 0.0001f : m;
        r->inv_mag_lut[i] = 1.0f / r->mag_lut[i];
    }

    return (void*)r;
}

void rgbkeysmooth_free(void *ptr)
{
    rgbkeysmooth_t *r = (rgbkeysmooth_t*)ptr;

    free(r->alpha_map);
    free(r);
}

void rgbkeysmooth_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    rgbkeysmooth_t *rgbkey = (rgbkeysmooth_t*)ptr;

    const int hue_angle_arg = args[P_HUE_ANGLE];
    const int matte_min_arg = args[P_MATTE_MIN];
    const int matte_max_arg = args[P_MATTE_MAX];
    const int luma_min_arg = args[P_LUMA_MIN];
    const int luma_max_arg = args[P_LUMA_MAX];
    const int spill_amount_arg = args[P_SPILL_AMOUNT];
    const int spill_recovery_arg = args[P_SPILL_RECOVERY];
    const int view_mode_arg = args[P_VIEW_MODE];
    const int softness_arg = args[P_SOFTNESS];

    int iy, iu, iv;

    _rgb2yuv(args[P_RED], args[P_GREEN], args[P_BLUE], iy, iu, iv);

    const int kU_int = iu - 128;
    const int kV_int = iv - 128;
    const int kD = kU_int * kU_int + kV_int * kV_int;
    const float kInvMag = rgbkey->inv_mag_lut[kD];
    const float kU = (float)kU_int * kInvMag;
    const float kV = (float)kV_int * kInvMag;

    const float angle = ((float)hue_angle_arg / 100.0f) * ((float)M_PI / 180.0f);
    const float wedge_cos = cosf(angle);
    const float hue_denom = 1.0f / ((1.0f - wedge_cos) * 128.0f);

    const float m_min = (float)clampi(matte_min_arg, 0, 255) * (1.0f / 255.0f);
    float m_max = (float)clampi(matte_max_arg, 0, 255) * (1.0f / 255.0f);

    if(m_max <= m_min)
        m_max = m_min + 0.01f;

    const float m_range_inv = 1.0f / (m_max - m_min);

    const float l_min = (float)clampi(luma_min_arg, 0, 255);
    const float l_max = (float)clampi(luma_max_arg, 0, 255);
    const float l_min_inv = 1.0f / (l_min + 0.001f);
    const float l_max_inv = 1.0f / (255.0f - l_max + 0.001f);

    const float s_amt = (float)clampi(spill_amount_arg, 0, 255) * (1.0f / 255.0f);
    const float l_rec_half = (float)clampi(spill_recovery_arg, 0, 255) * (1.0f / 510.0f);

    const int mode = clampi(view_mode_arg, 0, 2);
    const int soft = clampi(softness_arg, 0, 255);

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    uint8_t *restrict AM = rgbkey->alpha_map;
    uint8_t *restrict AT = rgbkey->alpha_temp;

    const float *restrict l_mag = rgbkey->mag_lut;
    const float *restrict l_inv = rgbkey->inv_mag_lut;

#pragma omp parallel num_threads(rgbkey->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            const int u_int = (int)Cb[i] - 128;
            const int v_int = (int)Cr[i] - 128;
            const int d = u_int * u_int + v_int * v_int;

            if(d < 16) {
                AM[i] = 255;
                continue;
            }

            const float invM = l_inv[d];
            const float dot = ((float)u_int * kU + (float)v_int * kV) * invM;

            if(dot <= wedge_cos) {
                AM[i] = 255;
                continue;
            }

            const float fY = (float)Y[i];
            const float lw = rgbkeysmooth_clampf(fY * l_min_inv, 0.0f, 1.0f) *
                             rgbkeysmooth_clampf((255.0f - fY) * l_max_inv, 0.0f, 1.0f);
            const float raw = (dot - wedge_cos) * hue_denom * l_mag[d] * lw;
            const float t = rgbkeysmooth_clampf((raw - m_min) * m_range_inv, 0.0f, 1.0f);

            AM[i] = (uint8_t)((1.0f - (t * t * (3.0f - 2.0f * t))) * 255.0f);
        }

        if(soft > 0) {
#pragma omp for schedule(static)
            for(int y = 0; y < h; y++) {
                uint8_t *restrict row_in = AM + y * w;
                uint8_t *restrict row_out = AT + y * w;

                row_out[0] = row_in[0];
                row_out[w - 1] = row_in[w - 1];

                for(int x = 1; x < w - 1; x++) {
                    const int sum = row_in[x - 1] + row_in[x] + row_in[x + 1];
                    const int avg = (sum * 21846) >> 16;

                    row_out[x] = row_in[x] + (((avg - row_in[x]) * soft + 128) >> 8);
                }
            }

#pragma omp for schedule(static)
            for(int y = 1; y < h - 1; y++) {
                uint8_t *restrict r_top = AT + (y - 1) * w;
                uint8_t *restrict r_mid = AT + y * w;
                uint8_t *restrict r_bot = AT + (y + 1) * w;
                uint8_t *restrict r_dest = AM + y * w;

                for(int x = 0; x < w; x++) {
                    const int sum = r_top[x] + r_mid[x] + r_bot[x];
                    const int avg = (sum * 21846) >> 16;

                    r_dest[x] = r_mid[x] + (((avg - r_mid[x]) * soft + 128) >> 8);
                }
            }
        }

        if(mode == 1) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                Y[i] = AM[i];
                Cb[i] = 128;
                Cr[i] = 128;
            }
        }
        else if(mode == 2) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int u = (int)Cb[i] - 128;
                const int v = (int)Cr[i] - 128;
                const int d = u * u + v * v;
                const float invM = l_inv[d];
                const float dot = ((float)u * kU + (float)v * kV) * invM;
                const float pos_dot = dot > 0.0f ? dot : 0.0f;
                const float s = pos_dot * s_amt * l_mag[d];

                Y[i] = rgbkeysmooth_u8f((float)Y[i] + (s * l_rec_half));
                Cb[i] = rgbkeysmooth_u8f(((float)u - kU * s) + 128.0f);
                Cr[i] = rgbkeysmooth_u8f(((float)v - kV * s) + 128.0f);
            }
        }
        else if(s_amt > 0.0f) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int a = AM[i];

                if(LIKELY(a == 0)) {
                    Y[i] = Y2[i];
                    Cb[i] = Cb2[i];
                    Cr[i] = Cr2[i];
                    continue;
                }

                float fU = (float)Cb[i] - 128.0f;
                float fV = (float)Cr[i] - 128.0f;
                float fY = (float)Y[i];

                const int d = (int)(fU * fU + fV * fV);
                const float dot = (fU * kU + fV * kV) * l_inv[d];
                const float pos_dot = dot > 0.0f ? dot : 0.0f;
                const float s = pos_dot * s_amt * l_mag[d];

                fU -= kU * s;
                fV -= kV * s;
                fY += s * l_rec_half;

                const int ia = 255 - a;

                Y[i] = RGBKEYSMOOTH_DIV255((int)rgbkeysmooth_clampf(fY, 0.0f, 255.0f) * a + Y2[i] * ia);
                Cb[i] = RGBKEYSMOOTH_DIV255((int)rgbkeysmooth_clampf(fU + 128.0f, 0.0f, 255.0f) * a + Cb2[i] * ia);
                Cr[i] = RGBKEYSMOOTH_DIV255((int)rgbkeysmooth_clampf(fV + 128.0f, 0.0f, 255.0f) * a + Cr2[i] * ia);
            }
        }
        else {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int a = AM[i];
                const int ia = 255 - a;

                Y[i] = RGBKEYSMOOTH_DIV255((int)Y[i] * a + (int)Y2[i] * ia);
                Cb[i] = RGBKEYSMOOTH_DIV255((int)Cb[i] * a + (int)Cb2[i] * ia);
                Cr[i] = RGBKEYSMOOTH_DIV255((int)Cr[i] * a + (int)Cr2[i] * ia);
            }
        }
    }
}
