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
#include <string.h>
#include <math.h>
#include <omp.h>
#include "common.h"
#include <veejaycore/vjmem.h>

#define CLAMP(x,a,b) ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))

typedef struct {
    int w, h;
    int n_threads;

    uint8_t *tmpY;
    uint8_t *tmpU;
    uint8_t *tmpV;

    float *vx;
    float *vy;
} chromawarp_t;

static inline __attribute__((always_inline)) uint8_t fast_bilinear(
    const uint8_t * restrict img, const int w, 
    const int x_fixed, const int y_fixed) 
{
    const int x = x_fixed >> 16;
    const int y = y_fixed >> 16;
    
    const int xf = (x_fixed >> 8) & 0xFF;
    const int yf = (y_fixed >> 8) & 0xFF;

    const int idx = y * w + x;
    
    const int w11 = (256 - xf) * (256 - yf);
    const int w21 = xf * (256 - yf);
    const int w12 = (256 - xf) * yf;
    const int w22 = xf * yf;

    const int res = (img[idx] * w11 + img[idx + 1] * w21 + 
                     img[idx + w] * w12 + img[idx + w + 1] * w22);

    return (uint8_t)(res >> 16);
}

vj_effect *chromawarp_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 5;
    ve->defaults   = (int *) vj_calloc(sizeof(int) * 6);
    ve->limits[0]  = (int *) vj_calloc(sizeof(int) * 6);
    ve->limits[1]  = (int *) vj_calloc(sizeof(int) * 6);

    ve->limits[0][0] = 0;   ve->limits[1][0] = 64;  ve->defaults[0] = 12;   
    ve->limits[0][1] = 0;   ve->limits[1][1] = 360; ve->defaults[1] = 0;    
    ve->limits[0][2] = 0;   ve->limits[1][2] = 255; ve->defaults[2] = 255;  
    ve->limits[0][3] = 0;   ve->limits[1][3] = 255; ve->defaults[3] = 160;  
    ve->limits[0][4] = 0;   ve->limits[1][4] = 255; ve->defaults[4] = 0;

    ve->sub_format = 1;
    ve->description = "Spectral Flow";

    ve->param_description =
        vje_build_param_list(5,
            "Warp Strength",
            "Flow Rotation",
            "Mix",
            "Temporal Smooth",
            "Directional Bias");

    return ve;
}

void *chromawarp_malloc(int w, int h)
{
    chromawarp_t *c = (chromawarp_t *) vj_calloc(sizeof(chromawarp_t));
    if (!c) return NULL;

    int sz = w * h;
    c->w = w;
    c->h = h;

    c->tmpY = (uint8_t *) vj_malloc(sz*3);
    c->tmpU = (uint8_t *) c->tmpY + sz;
    c->tmpV = (uint8_t *) c->tmpU + sz;

    c->vx = (float *) vj_calloc(sizeof(float) * sz * 2);
    c->vy = c->vx + sz;

    c->n_threads = vje_advise_num_threads(sz);

    return c;
}

void chromawarp_free(void *ptr)
{
    chromawarp_t *c = (chromawarp_t *) ptr;
    if (!c) return;

    free(c->tmpY);
    free(c->vx);
    free(c);
}

void chromawarp_apply(void *ptr, VJFrame *frame, int *args)
{
    chromawarp_t * restrict c = (chromawarp_t *) ptr;

    const int strength   = args[0];
    const int rotation   = args[1];
    const int mix        = args[2];
    const int tsmooth    = args[3];
    const int bias       = args[4];

    uint8_t * restrict Y = frame->data[0];
    uint8_t * restrict U = frame->data[1];
    uint8_t * restrict V = frame->data[2];

    const int w = c->w;
    const int h = c->h;
    const int sz = w * h;

    const float scale = strength * (1.0f / 16.0f);
    const float t     = tsmooth  * (1.0f / 255.0f);
    const float inv_t = 1.0f - t;
    const float b     = bias     * (2.0f / 255.0f); 

    const float rad   = (float)rotation * (M_PI / 180.0f);
    const float cos_a = cosf(rad);
    const float sin_a = sinf(rad);

    const float inv_w  = 1.0f / (float)w;
    const float inv_h  = 1.0f / (float)h;
    const float half_w = w * 0.5f;
    const float half_h = h * 0.5f;

    const float max_sx = (float)w - 1.001f;
    const float max_sy = (float)h - 1.001f;

    float * restrict vx = c->vx;
    float * restrict vy = c->vy;
    uint8_t * restrict tmpY = c->tmpY;
    uint8_t * restrict tmpU = c->tmpU;
    uint8_t * restrict tmpV = c->tmpV;

#pragma omp parallel for num_threads(c->n_threads) schedule(static)
    for (int y = 0; y < h; y++)
    {
        const int yw = y * w;
        const float cy = (y - half_h) * inv_h;
        const float cx_start = -half_w * inv_w;

        for (int x = 0; x < w; x++)
        {
            const int i = yw + x;
            float u_raw = (float)U[i] - 128.0f;
            float v_raw = (float)V[i] - 128.0f;
            float fx = u_raw * cos_a - v_raw * sin_a;
            float fy = u_raw * sin_a + v_raw * cos_a;

            if (bias > 0)
            {
                float cx = cx_start + (float)x * inv_w;
                float dot = fx * cx + fy * cy;
                fx += dot * b;
                fy += dot * b;
            }

            float vx_new = vx[i] * t + fx * inv_t;
            float vy_new = vy[i] * t + fy * inv_t;
            vx[i] = vx_new;
            vy[i] = vy_new;

            float sx_f = (float)x + vx_new * scale;
            float sy_f = (float)y + vy_new * scale;

            sx_f = CLAMP(sx_f, 0.0f, max_sx);
            sy_f = CLAMP(sy_f, 0.0f, max_sy);
            
            const int sx_fixed = (int)(sx_f * 65536.0f);
            const int sy_fixed = (int)(sy_f * 65536.0f);

            tmpY[i] = fast_bilinear(Y, w, sx_fixed, sy_fixed);
            tmpU[i] = fast_bilinear(U, w, sx_fixed, sy_fixed);
            tmpV[i] = fast_bilinear(V, w, sx_fixed, sy_fixed);
        }
    }

    if (mix == 255)
    {
        veejay_memcpy(Y, tmpY, sz);
        veejay_memcpy(U, tmpU, sz);
        veejay_memcpy(V, tmpV, sz);
    }
    else
    {
        const int inv_mix = 255 - mix;

#pragma omp parallel for simd num_threads(c->n_threads) schedule(static)
        for (int i = 0; i < sz; i++)
        {
            Y[i] = (Y[i] * inv_mix + tmpY[i] * mix) >> 8;
            U[i] = (U[i] * inv_mix + tmpU[i] * mix) >> 8;
            V[i] = (V[i] * inv_mix + tmpV[i] * mix) >> 8;
        }
    }
}