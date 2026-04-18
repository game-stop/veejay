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
#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include <veejaycore/vjmem.h>

#define INV_255        0.0039215686f
#define PI_X2          6.28318530718f
#define SIN_LUT_SIZE   4096
#define SIN_MASK       (SIN_LUT_SIZE - 1)
#define SCALER_TO_LUT  (SIN_LUT_SIZE / PI_X2)

#define FP_S    14
#define FP_M    (1 << FP_S)
#define FP_HALF (1 << (FP_S - 1))

static inline uint8_t clamp_u8(int x)
{
    return (uint8_t)((x | ((x | (255 - x)) >> 31)) & ~(x >> 31) & 255); //FIXME generalize this into common.h depending on target platform
}

typedef struct
{
    int w, h;
    float time;
    int16_t sin_lut_fp[SIN_LUT_SIZE];
    uint8_t contrast_lut[256];
    int32_t tint_lut_fp[256];
    float last_contrast;
    int last_pastel;
    int last_bprot;
    int last_wprot;
    int n_threads;
} chromaticdrift_t;

vj_effect *chromaticdrift_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 10;
    ve->defaults   = (int *) vj_calloc(sizeof(int) * 10);
    ve->limits[0]  = (int *) vj_calloc(sizeof(int) * 10);
    ve->limits[1]  = (int *) vj_calloc(sizeof(int) * 10);

    ve->defaults[0] = 0;
    ve->defaults[1] = 0;
    ve->defaults[2] = 55;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->defaults[5] = 0;
    ve->defaults[6] = 0;
    ve->defaults[7] = 255;
    ve->defaults[8] = 85;
    ve->defaults[9] = 1;

    ve->limits[0][0] = 0;   ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;   ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;   ve->limits[1][4] = 255;
    ve->limits[0][5] = 0;   ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;   ve->limits[1][6] = 255;
    ve->limits[0][7] = 0;   ve->limits[1][7] = 255;
    ve->limits[0][8] = 0;   ve->limits[1][8] = 255;
    ve->limits[0][9] = -1;  ve->limits[1][9] = 1;

    ve->sub_format = 1;
    ve->description = "Color Drift"; // shifts and optionally animates colors based on brightness and edges (can also morph to another broadcast standard)

    ve->param_description = vje_build_param_list(
        10,
        "Global Hue",
        "Rainbow Wrap",
        "Vibrance",
        "Pastel Glow",
        "Flux Speed",
        "Edge Softness",
        "Black Protect",
        "White Protect",
        "Luma Contrast",
        "Direction");

    return ve;
}

void *chromaticdrift_malloc(int w, int h)
{
    chromaticdrift_t *c = (chromaticdrift_t *) vj_calloc(sizeof(chromaticdrift_t));

    c->w = w;
    c->h = h;

    for (int i = 0; i < SIN_LUT_SIZE; i++)
    {
        float s = sinf((i * PI_X2) / SIN_LUT_SIZE);
        c->sin_lut_fp[i] = (int16_t)(s * FP_M);
    }

    c->last_contrast = -1.0f;
    c->last_pastel   = -1;
    c->last_bprot    = -1;
    c->last_wprot    = -1;

    c->n_threads = vje_advise_num_threads(w * h);

    return c;
}

void chromaticdrift_free(void *ptr)
{
    if (ptr)
        free(ptr);
}

void chromaticdrift_apply(void *ptr, VJFrame *frame, int *args)
{
    chromaticdrift_t *n = (chromaticdrift_t *) ptr;

    const int w = n->w;
    const int h = n->h;

    const int w_sub_1 = w - 1;
    const int h_sub_1 = h - 1;

    const float global_hue   = args[0] * (INV_255 * PI_X2);
    const float rainbow      = args[1] * (INV_255 * (PI_X2 * 4.0f));
    const float vibrance     = 0.85f + (args[2] * INV_255 * 0.7f);
    const float pastel_base  = args[3] * (INV_255 * 22.0f);
    const float speed        = args[4] * (INV_255 * 0.15f);
    const float softness     = args[5] * (INV_255 * 2.5f);
    const float contrast_val = 0.5f + (args[8] * INV_255 * 1.5f);
    const float dir          = (float) args[9];

    uint8_t *restrict py = frame->data[0];
    uint8_t *restrict pu = frame->data[1];
    uint8_t *restrict pv = frame->data[2];


    if (contrast_val != n->last_contrast)
    {
        for (int i = 0; i < 256; i++)
        {
            float x = (i + 0.5f) * INV_255;
            n->contrast_lut[i] =
                clamp_u8((int)(powf(x, contrast_val) * 255.0f));
        }
        n->last_contrast = contrast_val;
    }

    if (args[3] != n->last_pastel || args[6] != n->last_bprot || args[7] != n->last_wprot)
    {
        const float b_prot = args[6] * INV_255;
        const float w_prot = args[7] * INV_255;

        const float inv_b = 1.0f / (b_prot + 0.001f);
        const float inv_w = 1.0f / (1.0f - w_prot + 0.001f);

        for (int i = 0; i < 256; i++)
        {
            const float y_raw = i * INV_255;
            const float m_b = (y_raw < b_prot) ? (y_raw * inv_b) : 1.0f;
            const float m_w = (y_raw > w_prot) ? (1.0f - (y_raw - w_prot) * inv_w) : 1.0f;

            n->tint_lut_fp[i] = (int32_t)(pastel_base * m_b * m_b * m_w * m_w * FP_M);
        }

        n->last_pastel = args[3];
        n->last_bprot  = args[6];
        n->last_wprot  = args[7];
    }

    n->time += speed * dir;
    const float t = n->time;

    const float base_angle_f   = (global_hue + t) * SCALER_TO_LUT;
    const float t_flux_f       = t * 0.15f;
    const float rainbow_scaled = rainbow * INV_255 * SCALER_TO_LUT;

    const int32_t vibrance_fp = (int32_t)(vibrance * FP_M);
    const int32_t softness_fp = (int32_t)(softness * INV_255 * FP_M);

    const int16_t *restrict sin_lut = n->sin_lut_fp;
    const uint8_t *restrict contrast_lut = n->contrast_lut;
    const int32_t *restrict tint_lut = n->tint_lut_fp;

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for (int y = 1; y < h_sub_1; y++)
    {
        uint8_t *row  = py + y * w;
        uint8_t *up   = row - w;
        uint8_t *down = row + w;

        uint8_t *urow = pu + y * w;
        uint8_t *vrow = pv + y * w;

        for (int x = 1; x < w_sub_1; x++)
        {
            const int y_v = row[x];
            const int sum = y_v + row[x - 1] + row[x + 1] + up[x] + down[x];
            const int diff = y_v - ((sum * 13107) >> 16);
            const int mask = diff >> 31;
            const int edge_i = (diff + mask) ^ mask;
            const float field = (y_v * INV_255) + ((edge_i * INV_255) * 2.0f) + t_flux_f;

            const int s_idx = ((int)(base_angle_f + field * rainbow_scaled)) & SIN_MASK;

            const int c_idx = (s_idx + (SIN_LUT_SIZE >> 2)) & SIN_MASK;

            const int32_t s_fp = sin_lut[s_idx];
            const int32_t c_fp = sin_lut[c_idx];

            const int32_t u = (int32_t)urow[x] - 128;
            const int32_t v = (int32_t)vrow[x] - 128;

            const int32_t stab_fp = ((FP_M - ((softness_fp * edge_i) >> FP_S)) * vibrance_fp + FP_HALF) >> FP_S;

            int32_t u_rot = (u * c_fp - v * s_fp + FP_HALF) >> FP_S;
            int32_t v_rot = (u * s_fp + v * c_fp + FP_HALF) >> FP_S;

            u_rot = (u_rot * stab_fp + FP_HALF) >> FP_S;
            v_rot = (v_rot * stab_fp + FP_HALF) >> FP_S;

            const int32_t tint_fp = tint_lut[y_v];

            u_rot += (s_fp * tint_fp + FP_HALF) >> FP_S;
            v_rot += (c_fp * tint_fp + FP_HALF) >> FP_S;

            row[x]  = contrast_lut[y_v];
            urow[x] = clamp_u8(u_rot + 128);
            vrow[x] = clamp_u8(v_rot + 128);
        }
    }
}