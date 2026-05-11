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
#include <veejaycore/vjmem.h>
#include <math.h>
#include <omp.h>

#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)

typedef struct {
    uint8_t *srcY, *srcU, *srcV;
    uint8_t *histY;
    float *vecX, *vecY;
    double time;
    int n_threads;
} liquid_fold_t;

vj_effect *edgefold_init(int w, int h) {
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;   ve->limits[1][0] = 255; ve->defaults[0] = 30;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 100; ve->defaults[1] = 85;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 200; ve->defaults[2] = 50;
    ve->limits[0][3] = 0;   ve->limits[1][3] = 100; ve->defaults[3] = 20;
    ve->limits[0][4] = 0;   ve->limits[1][4] = 400; ve->defaults[4] = 100;
    ve->limits[0][5] = 10;  ve->limits[1][5] = 200; ve->defaults[5] = 60;

    ve->description = "Liquid Edge Fold";
    ve->param_description = vje_build_param_list(ve->num_params,
        "Edge Sens.", "Momentum", "Fold Force", "Expansion", "Disp Scale", "Max Speed");

    return ve;
}

void *edgefold_malloc(int w, int h) {
    liquid_fold_t *s = (liquid_fold_t*) vj_malloc(sizeof(liquid_fold_t));
    int size = w * h;

    s->srcY = (uint8_t*) vj_malloc(size * 4);
    s->srcU = s->srcY + size;
    s->srcV = s->srcU + size;
    s->histY = s->srcV + size;

    s->vecX = (float*) vj_calloc(sizeof(float) * size * 2);
    s->vecY = s->vecX + size;
    s->time = 0.0;
    s->n_threads = omp_get_max_threads();

    return (void*) s;
}

void edgefold_free(void *ptr) {
    liquid_fold_t *s = (liquid_fold_t*) ptr;
    if (s) {
        if (s->srcY) free(s->srcY);
        if (s->vecX) free(s->vecX);
        free(s);
    }
}

static inline int32_t sample_bilinear(const uint8_t *buf, int32_t u_fp, int32_t v_fp, int w, int h) {
    int32_t u = u_fp & (FP_ONE - 1);
    int32_t v = v_fp & (FP_ONE - 1);
    int32_t xf = (int64_t)u * (w - 1);
    int32_t yf = (int64_t)v * (h - 1);
    int x = xf >> FP_SHIFT;
    int y = yf >> FP_SHIFT;
    int32_t fx = xf & (FP_ONE - 1);
    int32_t fy = yf & (FP_ONE - 1);
    int x1 = (x + 1 >= w) ? 0 : x + 1;
    int y1 = (y + 1 >= h) ? 0 : y + 1;
    
    int64_t sum = (int64_t)(FP_ONE - fx) * (FP_ONE - fy) * buf[y * w + x] +
                  (int64_t)fx * (FP_ONE - fy) * buf[y * w + x1] +
                  (int64_t)(FP_ONE - fx) * fy * buf[y1 * w + x] +
                  (int64_t)fx * fy * buf[y1 * w + x1];
                  
    return (int32_t)(sum >> (FP_SHIFT * 2));
}

static inline int32_t sample_bilinear_uv(const uint8_t *buf, int32_t u_fp, int32_t v_fp, int w, int h) {
    int32_t u = u_fp & (FP_ONE - 1);
    int32_t v = v_fp & (FP_ONE - 1);
    int32_t xf = (int64_t)u * (w - 1);
    int32_t yf = (int64_t)v * (h - 1);
    int x = xf >> FP_SHIFT;
    int y = yf >> FP_SHIFT;
    int32_t fx = xf & (FP_ONE - 1);
    int32_t fy = yf & (FP_ONE - 1);
    int x1 = (x + 1 >= w) ? 0 : x + 1;
    int y1 = (y + 1 >= h) ? 0 : y + 1;
    
    int64_t sum = (int64_t)(FP_ONE - fx) * (FP_ONE - fy) * (buf[y * w + x] - 128) +
                  (int64_t)fx * (FP_ONE - fy) * (buf[y * w + x1] - 128) +
                  (int64_t)(FP_ONE - fx) * fy * (buf[y1 * w + x] - 128) +
                  (int64_t)fx * fy * (buf[y1 * w + x1] - 128);
                  
    return (int32_t)(sum >> (FP_SHIFT * 2));
}

void edgefold_apply(void *ptr, VJFrame *frame, int *args) {
    liquid_fold_t *s = (liquid_fold_t *)ptr;
    if (!s || !frame) return;

    const int w = frame->width;
    const int h = frame->height;
    const size_t plane_size = w * h;

    const float edge_thresh_sq = (float)args[0] * (float)args[0];          
    const float momentum       = (args[1] / 100.0f) * 0.95f;        
    const float fold_force     = args[2] / 5.0f;         
    const float expansion      = (args[3] / 100.0f) * 0.5f;        
    const float global_scale   = args[4] / 100.0f;
    const float max_speed      = (float)args[5];
    const float max_speed_sq   = max_speed * max_speed;

    veejay_memcpy(s->srcY, frame->data[0], plane_size);
    veejay_memcpy(s->srcU, frame->data[1], plane_size);
    veejay_memcpy(s->srcV, frame->data[2], plane_size);

    #pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            const int idx = y * w + x;

            float gx = (float)(
                -1 * s->srcY[(y-1)*w + (x-1)] + 1 * s->srcY[(y-1)*w + (x+1)] +
                -2 * s->srcY[(y)*w   + (x-1)] + 2 * s->srcY[(y)*w   + (x+1)] +
                -1 * s->srcY[(y+1)*w + (x-1)] + 1 * s->srcY[(y+1)*w + (x+1)]
            );
            float gy = (float)(
                -1 * s->srcY[(y-1)*w + (x-1)] - 2 * s->srcY[(y-1)*w + x] - 1 * s->srcY[(y-1)*w + (x+1)] +
                 1 * s->srcY[(y+1)*w + (x-1)] + 2 * s->srcY[(y+1)*w + x] + 1 * s->srcY[(y+1)*w + (x+1)]
            );

            float mag_sq = gx*gx + gy*gy;
            
            float fX = 0.0f, fY = 0.0f;
            if (mag_sq > edge_thresh_sq) {
                fX = (gx / 255.0f) * fold_force;
                fY = (gy / 255.0f) * fold_force;
            }

            float vx = s->vecX[idx];
            float vy = s->vecY[idx];
            
            float avgX = (s->vecX[idx-1] + s->vecX[idx+1] + s->vecX[idx-w] + s->vecX[idx+w]) * 0.25f;
            float avgY = (s->vecY[idx-1] + s->vecY[idx+1] + s->vecY[idx-w] + s->vecY[idx+w]) * 0.25f;

            vx = vx * (1.0f - expansion) + avgX * expansion;
            vy = vy * (1.0f - expansion) + avgY * expansion;

            vx = (vx + fX) * momentum;
            vy = (vy + fY) * momentum;

            float speed_sq = vx*vx + vy*vy;
            if (speed_sq > max_speed_sq) { 
                float inv_speed = max_speed / sqrtf(speed_sq);
                vx *= inv_speed;
                vy *= inv_speed;
            }

            s->vecX[idx] = vx;
            s->vecY[idx] = vy;

            float nx = (x - (vx * global_scale)) / (float)w;
            float ny = (y - (vy * global_scale)) / (float)h;

            int32_t u_fp = (int32_t)(nx * FP_ONE);
            int32_t v_fp = (int32_t)(ny * FP_ONE);

            u_fp = (u_fp < 0) ? -u_fp : u_fp;
            u_fp = (u_fp > FP_ONE) ? (FP_ONE - (u_fp % FP_ONE)) : u_fp;
            
            v_fp = (v_fp < 0) ? -v_fp : v_fp;
            v_fp = (v_fp > FP_ONE) ? (FP_ONE - (v_fp % FP_ONE)) : v_fp;

            frame->data[0][idx] = (uint8_t)sample_bilinear(s->srcY, u_fp, v_fp, w, h);
            
            int32_t u_val = sample_bilinear_uv(s->srcU, u_fp, v_fp, w, h) + 128;
            int32_t v_val = sample_bilinear_uv(s->srcV, u_fp, v_fp, w, h) + 128;
            
            frame->data[1][idx] = (uint8_t)((u_val < 0) ? 0 : (u_val > 255 ? 255 : u_val));
            frame->data[2][idx] = (uint8_t)((v_val < 0) ? 0 : (v_val > 255 ? 255 : v_val));
        }
    }
    s->time += 1.0;
}