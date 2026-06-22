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
#include "chromawarp.h"

typedef struct {
    int w;
    int h;
    int n_threads;
    uint8_t *tmpY;
    uint8_t *tmpU;
    uint8_t *tmpV;
    float *vx;
    float *vy;
} chromawarp_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline __attribute__((always_inline)) uint8_t fast_bilinear(const uint8_t *restrict img, const int w, const int x_fixed, const int y_fixed)
{
    const int x = x_fixed >> 16;
    const int y = y_fixed >> 16;
    const int xf = (x_fixed >> 8) & 0xff;
    const int yf = (y_fixed >> 8) & 0xff;
    const int idx = y * w + x;
    const int w11 = (256 - xf) * (256 - yf);
    const int w21 = xf * (256 - yf);
    const int w12 = (256 - xf) * yf;
    const int w22 = xf * yf;
    const int res = img[idx] * w11 + img[idx + 1] * w21 + img[idx + w] * w12 + img[idx + w + 1] * w22;

    return (uint8_t)(res >> 16);
}

vj_effect *chromawarp_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

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

    ve->limits[0][0] = 0; ve->limits[1][0] = 64;  ve->defaults[0] = 12;
    ve->limits[0][1] = 0; ve->limits[1][1] = 360; ve->defaults[1] = 0;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255; ve->defaults[2] = 255;
    ve->limits[0][3] = 0; ve->limits[1][3] = 255; ve->defaults[3] = 160;
    ve->limits[0][4] = 0; ve->limits[1][4] = 255; ve->defaults[4] = 0;

    ve->sub_format = 1;
    ve->description = "Spectral Flow";
    ve->param_description = vje_build_param_list(ve->num_params, "Warp Strength", "Flow Rotation", "Mix", "Temporal Smooth", "Directional Bias");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WARP,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                     0,   64,  30, 100, 120, 1100, 0,   100,
        VJ_BEAT_DRIFT,            VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,                              0,  360,  22,  86, 180, 1600, 0,    88,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                   160,  255,  18,  72, 240, 1800, 0,    72,
        VJ_BEAT_INERTIA,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 0, 255,  22,  90, 180, 1500, 80,   84,
        VJ_BEAT_FLOW,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                     0,  255,  24,  92, 180, 1500, 80,   86
    );

    return ve;
}

void *chromawarp_malloc(int w, int h)
{
    chromawarp_t *c = (chromawarp_t *) vj_calloc(sizeof(chromawarp_t));

    if(!c)
        return NULL;

    const int sz = w * h;

    c->w = w;
    c->h = h;
    c->n_threads = vje_advise_num_threads(sz);

    c->tmpY = (uint8_t *) vj_malloc(sz * 3);

    if(!c->tmpY) {
        free(c);
        return NULL;
    }

    c->tmpU = c->tmpY + sz;
    c->tmpV = c->tmpU + sz;

    c->vx = (float *) vj_calloc(sizeof(float) * sz * 2);

    if(!c->vx) {
        free(c->tmpY);
        free(c);
        return NULL;
    }

    c->vy = c->vx + sz;

    return c;
}

void chromawarp_free(void *ptr)
{
    chromawarp_t *c = (chromawarp_t *) ptr;

    if(!c)
        return;

    if(c->tmpY)
        free(c->tmpY);

    if(c->vx)
        free(c->vx);

    free(c);
}

void chromawarp_apply(void *ptr, VJFrame *frame, int *args)
{
    chromawarp_t *restrict c = (chromawarp_t *) ptr;

    const int strength_arg = args[0];
    const int rotation_arg = args[1];
    const int mix_arg = args[2];
    const int tsmooth_arg = args[3];
    const int bias_arg = args[4];

    const int strength = clampi(strength_arg, 0, 64);
    const int rotation = clampi(rotation_arg, 0, 360);
    const int mix = clampi(mix_arg, 0, 255);
    const int tsmooth = clampi(tsmooth_arg, 0, 255);
    const int bias = clampi(bias_arg, 0, 255);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const int w = c->w;
    const int h = c->h;
    const int sz = w * h;

    const float scale = (float)strength * (1.0f / 16.0f);
    const float t = (float)tsmooth * (1.0f / 255.0f);
    const float inv_t = 1.0f - t;
    const float b = (float)bias * (2.0f / 255.0f);
    const float rad = (float)rotation * (M_PI / 180.0f);
    const float cos_a = cosf(rad);
    const float sin_a = sinf(rad);
    const float inv_w = 1.0f / (float)w;
    const float inv_h = 1.0f / (float)h;
    const float half_w = (float)w * 0.5f;
    const float half_h = (float)h * 0.5f;
    const float max_sx = (float)w - 1.001f;
    const float max_sy = (float)h - 1.001f;

    float *restrict vx = c->vx;
    float *restrict vy = c->vy;
    uint8_t *restrict tmpY = c->tmpY;
    uint8_t *restrict tmpU = c->tmpU;
    uint8_t *restrict tmpV = c->tmpV;

#pragma omp parallel for num_threads(c->n_threads) schedule(static)
    for(int y = 0; y < h; y++)
    {
        const int yw = y * w;
        const float cy = ((float)y - half_h) * inv_h;
        const float cx_start = -half_w * inv_w;

        for(int x = 0; x < w; x++)
        {
            const int i = yw + x;
            const float u_raw = (float)U[i] - 128.0f;
            const float v_raw = (float)V[i] - 128.0f;
            float fx = u_raw * cos_a - v_raw * sin_a;
            float fy = u_raw * sin_a + v_raw * cos_a;

            if(bias > 0)
            {
                const float cx = cx_start + (float)x * inv_w;
                const float dot = fx * cx + fy * cy;

                fx += dot * b;
                fy += dot * b;
            }

            const float vx_new = vx[i] * t + fx * inv_t;
            const float vy_new = vy[i] * t + fy * inv_t;

            vx[i] = vx_new;
            vy[i] = vy_new;

            const float sx_f = clampf((float)x + vx_new * scale, 0.0f, max_sx);
            const float sy_f = clampf((float)y + vy_new * scale, 0.0f, max_sy);
            const int sx_fixed = (int)(sx_f * 65536.0f);
            const int sy_fixed = (int)(sy_f * 65536.0f);

            tmpY[i] = fast_bilinear(Y, w, sx_fixed, sy_fixed);
            tmpU[i] = fast_bilinear(U, w, sx_fixed, sy_fixed);
            tmpV[i] = fast_bilinear(V, w, sx_fixed, sy_fixed);
        }
    }

    if(mix >= 255)
    {
        veejay_memcpy(Y, tmpY, sz);
        veejay_memcpy(U, tmpU, sz);
        veejay_memcpy(V, tmpV, sz);
    }
    else if(mix > 0)
    {
        const int inv_mix = 255 - mix;

#pragma omp parallel for simd num_threads(c->n_threads) schedule(static)
        for(int i = 0; i < sz; i++)
        {
            Y[i] = (uint8_t)(((int)Y[i] * inv_mix + (int)tmpY[i] * mix) >> 8);
            U[i] = (uint8_t)(((int)U[i] * inv_mix + (int)tmpU[i] * mix) >> 8);
            V[i] = (uint8_t)(((int)V[i] * inv_mix + (int)tmpV[i] * mix) >> 8);
        }
    }
}
