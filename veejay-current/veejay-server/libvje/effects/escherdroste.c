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

#define GAMMA_LUT_SIZE 1024

#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)
#define TO_FP(x) ((int32_t)((x) * FP_ONE))
#define FROM_FP(x) ((float)(x) / FP_ONE)

typedef struct {
    uint8_t *dstY, *dstU, *dstV;
    int *u_lut, *v_lut;
    int *histY, *histU, *histV;
    uint8_t gamma_lut[GAMMA_LUT_SIZE];
    double time;
    double phase;
    int width, height;
    int n_threads;
} box_escherdroste_t;

static inline uint8_t clamp_u8(float v) {
    int i = (int)v;
    if (i < 0) return 0;
    if (i > 255) return 255;
    return (uint8_t)i;
}

static void generate_geometry(box_escherdroste_t *t) {
    float cx = t->width * 0.5f;
    float cy = t->height * 0.5f;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < t->height; y++) {
        for (int x = 0; x < t->width; x++) {
            int i = y * t->width + x;

            float dx = (x - cx) / cx;
            float dy = (y - cy) / cy;

            float r = fmaxf(sqrtf(dx*dx + dy*dy), 1e-6f);
            float theta = atan2f(dy, dx);

            t->v_lut[i] = (int)(logf(r) * FP_ONE);
            t->u_lut[i] = (int)(theta * FP_ONE);
        }
    }
}

vj_effect *escherdroste_init(int width, int height) {
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;
    ve->defaults = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int)*ve->num_params);

    ve->defaults[0]=1; 
    ve->defaults[1]=256;
    ve->defaults[2]=1;
    ve->defaults[3]=0;
    ve->defaults[4]=1;
    ve->defaults[5]=60;
    ve->defaults[6]=100;
    ve->defaults[7]=1;
    
    ve->limits[0][0]=-100; ve->limits[1][0]=100;  // Speed
    ve->limits[0][1]=2;    ve->limits[1][1]=500;  // Scale Factor
    ve->limits[0][2]=1;    ve->limits[1][2]=20;   // Branches
    ve->limits[0][3]=-100; ve->limits[1][3]=100;  // Swirl
    ve->limits[0][4]=-100; ve->limits[1][4]=100;  // Rot Speed
    ve->limits[0][5]=0;    ve->limits[1][5]=100;  // Feedback
    ve->limits[0][6]=-300;  ve->limits[1][6]=300; // Pitch
    ve->limits[0][7]=0;    ve->limits[1][7]=1;    // HQ
    
    ve->description = "Escher Droste";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params, 
        "Speed", "Scale Factor", "Branches", "Swirl", "Rot Speed", "Feedback","Pitch", "High Quality");
    return ve;
}

void *escherdroste_malloc(int width, int height) {
    box_escherdroste_t *t = (box_escherdroste_t*) vj_calloc(sizeof(box_escherdroste_t));
    if(!t) return NULL;

    t->width = width; t->height = height;
    int size = width * height;

    t->u_lut = (int*) vj_malloc(sizeof(int) * size * 2);
    t->v_lut = t->u_lut + size;

    t->histY = (int*) vj_calloc(sizeof(int) * size * 3); 
    t->histU = t->histY + size;
    t->histV = t->histU + size;

    t->dstY = (uint8_t*) vj_malloc(size * 3);
    t->dstU = t->dstY + size;
    t->dstV = t->dstU + size;

    for(int i = 0; i < GAMMA_LUT_SIZE; i++) {
        float val = (float)i / (GAMMA_LUT_SIZE - 1);
        t->gamma_lut[i] = clamp_u8(powf(val, 0.85f) * 255.0f);
    }

    t->n_threads = vje_advise_num_threads(size);
    generate_geometry(t); 

    return t;
}

static inline int32_t sample_bilinear(const uint8_t *buf, int32_t u_fp, int32_t v_fp, int w, int h)
{
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

    int p00 = buf[y * w + x];
    int p10 = buf[y * w + x1];
    int p01 = buf[y1 * w + x];
    int p11 = buf[y1 * w + x1];

    int64_t w00 = (int64_t)(FP_ONE - fx) * (FP_ONE - fy);
    int64_t w10 = (int64_t)fx * (FP_ONE - fy);
    int64_t w01 = (int64_t)(FP_ONE - fx) * fy;
    int64_t w11 = (int64_t)fx * fy;

    int64_t sum = w00 * p00 + w10 * p10 + w01 * p01 + w11 * p11;

    return (int32_t)(sum >> (FP_SHIFT * 2));
}

static inline int32_t sample_bilinear_uv(const uint8_t *buf, int32_t u_fp, int32_t v_fp, int w, int h)
{
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

    int p00 = buf[y * w + x]  - 128;
    int p10 = buf[y * w + x1] - 128;
    int p01 = buf[y1 * w + x] - 128;
    int p11 = buf[y1 * w + x1]- 128;

    int64_t w00 = (int64_t)(FP_ONE - fx) * (FP_ONE - fy);
    int64_t w10 = (int64_t)fx * (FP_ONE - fy);
    int64_t w01 = (int64_t)(FP_ONE - fx) * fy;
    int64_t w11 = (int64_t)fx * fy;

    int64_t sum = w00 * p00 + w10 * p10 + w01 * p01 + w11 * p11;

    return (int32_t)(sum >> (FP_SHIFT * 2));
}

void escherdroste_free(void *ptr){
    box_escherdroste_t *t = (box_escherdroste_t*)ptr;
    if (!t) return;
    free(t->u_lut);
    free(t->histY);
    free(t->dstY);
    free(t);
}

void escherdroste_apply(void *ptr, VJFrame *frame, int *args) {
    box_escherdroste_t *t = (box_escherdroste_t*)ptr;
    int w = t->width, h = t->height, size = w * h;

    t->time += args[0] * 0.000725f; 
    t->phase += args[4] * 0.000725f;

    const float branches = (float)args[2];
    const float swirl = args[3] * 0.01f;
    const int use_high_quality = (args[7] == 1);

    const float zoom_intensity = 0.8f + ((float)args[1] / 500.0f) * 12.0f;
    const float factor = branches / zoom_intensity; 
    const float inv_2pi = 0.15915494f;
    const float pitch = (float)args[6] * 0.01f;

    const int32_t fb_fp = TO_FP(args[5] * 0.01f);
    const int32_t current_inv_fb = FP_ONE - fb_fp;
    const int32_t chroma_fb_fp = (fb_fp * 3) >> 2;
    const int32_t current_inv_chroma_fb = FP_ONE - chroma_fb_fp;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < size; i++) {
        float log_r = FROM_FP(t->v_lut[i]);
        float theta = FROM_FP(t->u_lut[i]);

        float base_v = (log_r * factor) + (theta * inv_2pi * branches);
        float base_u = (theta * inv_2pi * branches) - (log_r * factor * pitch);

        float v_val_f = base_v + t->time;
        float u_val_f = base_u + t->phase + (swirl * log_r);

        int32_t u_fp = (int32_t)(u_val_f * 65536.0f);
        int32_t v_fp = (int32_t)(v_val_f * 65536.0f);

        int64_t accY, accU, accV;

        if (use_high_quality) {
            accY = (int64_t)sample_bilinear(srcY, u_fp, v_fp, w, h) << FP_SHIFT;
            accU = (int64_t)sample_bilinear_uv(srcU, u_fp, v_fp, w, h) << FP_SHIFT;
            accV = (int64_t)sample_bilinear_uv(srcV, u_fp, v_fp, w, h) << FP_SHIFT;
        } else {
            int32_t um = u_fp & 0xFFFF;
            int32_t vm = v_fp & 0xFFFF;
            int tx = ((um >> 8) * (w - 1)) >> 8;
            int ty = ((vm >> 8) * (h - 1)) >> 8;
            accY = (int64_t)srcY[ty * w + tx] << FP_SHIFT;
            accU = (int64_t)(srcU[ty * w + tx] - 128) << FP_SHIFT;
            accV = (int64_t)(srcV[ty * w + tx] - 128) << FP_SHIFT;
        }

        t->histY[i] = ((accY * current_inv_fb) + ((int64_t)t->histY[i] * fb_fp) + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT;
        t->histU[i] = ((accU * current_inv_chroma_fb) + ((int64_t)t->histU[i] * chroma_fb_fp) + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT;
        t->histV[i] = ((accV * current_inv_chroma_fb) + ((int64_t)t->histV[i] * chroma_fb_fp) + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT;

        int y_val = t->histY[i] >> FP_SHIFT;
        int u_px = (t->histU[i] >> FP_SHIFT);
        int v_px = (t->histV[i] >> FP_SHIFT);

        int y_idx = (y_val < 0) ? 0 : (y_val > 255 ? 255 : y_val);
        t->dstY[i] = t->gamma_lut[y_idx * (GAMMA_LUT_SIZE - 1) / 255];
        t->dstU[i] = clamp_u8(((u_px * 1056) >> 10) + 128);
        t->dstV[i] = clamp_u8(((v_px * 1056) >> 10) + 128);
    }

    veejay_memcpy(srcY, t->dstY, size);
    veejay_memcpy(srcU, t->dstU, size);
    veejay_memcpy(srcV, t->dstV, size);
}