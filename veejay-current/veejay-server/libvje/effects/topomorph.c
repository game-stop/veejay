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
    int *histY, *histU, *histV;
    uint8_t gamma_lut[GAMMA_LUT_SIZE];
    double time;
    double phase;
    int width, height;
    int n_threads;
    float p1_x, p1_y;
    float p2_x, p2_y;
} box_topomorph_t;

static inline uint8_t clamp_u8(float v) {
    int i = (int)v;
    if (i < 0) return 0;
    if (i > 255) return 255;
    return (uint8_t)i;
}

vj_effect *topomorph_init(int width, int height) {
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    ve->num_params = 12;
    ve->defaults = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int)*ve->num_params);

    ve->defaults[0]=10;   ve->defaults[1]=256; ve->defaults[2]=1;
    ve->defaults[3]=0;   ve->defaults[4]=1;   ve->defaults[5]=60;
    ve->defaults[6]=100; ve->defaults[7]=1;   ve->defaults[8]=0;
    ve->defaults[9]=50;  ve->defaults[10]=1;  ve->defaults[11]=50;
    
    ve->limits[0][0]=-100; ve->limits[1][0]=100;
    ve->limits[0][1]=2;    ve->limits[1][1]=500;
    ve->limits[0][2]=1;    ve->limits[1][2]=20;
    ve->limits[0][3]=-100; ve->limits[1][3]=100;
    ve->limits[0][4]=-100; ve->limits[1][4]=100;
    ve->limits[0][5]=0;    ve->limits[1][5]=100;
    ve->limits[0][6]=-300; ve->limits[1][6]=300;
    ve->limits[0][7]=0;    ve->limits[1][7]=1;
    ve->limits[0][8]=0;    ve->limits[1][8]=2;
    ve->limits[0][9]=0;    ve->limits[1][9]=100;
    ve->limits[0][10]=0;   ve->limits[1][10]=1; 
    ve->limits[0][11]=10;  ve->limits[1][11]=80;
    
    ve->description = "Topological Morph";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params, 
        "Speed", "Scale Factor", "Branches", "Swirl", "Rot Speed", "Feedback", 
        "Pitch", "High Quality", "Genus", "Saliency Influence", "Geometry", "Shape P");
    return ve;
}

void *topomorph_malloc(int width, int height) {
    box_topomorph_t *t = (box_topomorph_t*) vj_calloc(sizeof(box_topomorph_t));
    if(!t) return NULL;
    t->width = width; t->height = height;
    int size = width * height;
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
    t->p1_x = -0.5f; t->p1_y = 0.0f;
    t->p2_x = 0.5f;  t->p2_y = 0.0f;
    return t;
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
    int p00 = buf[y * w + x];
    int p10 = buf[y * w + x1];
    int p01 = buf[y1 * w + x];
    int p11 = buf[y1 * w + x1];
    int64_t w00 = (int64_t)(FP_ONE - fx) * (FP_ONE - fy);
    int64_t w10 = (int64_t)fx * (FP_ONE - fy);
    int64_t w01 = (int64_t)(FP_ONE - fx) * fy;
    int64_t w11 = (int64_t)fx * fy;
    return (int32_t)((w00 * p00 + w10 * p10 + w01 * p01 + w11 * p11) >> (FP_SHIFT * 2));
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
    int p00 = buf[y * w + x] - 128;
    int p10 = buf[y * w + x1] - 128;
    int p01 = buf[y1 * w + x] - 128;
    int p11 = buf[y1 * w + x1] - 128;
    int64_t w00 = (int64_t)(FP_ONE - fx) * (FP_ONE - fy);
    int64_t w10 = (int64_t)fx * (FP_ONE - fy);
    int64_t w01 = (int64_t)(FP_ONE - fx) * fy;
    int64_t w11 = (int64_t)fx * fy;
    return (int32_t)((w00 * p00 + w10 * p10 + w01 * p01 + w11 * p11) >> (FP_SHIFT * 2));
}

static inline float get_radius_smooth(float X, float Y, float p_exponent) {
    // Minkowski distance (L-p norm)
    // p=1: Diamond, p=2: Circle, p=large: Rectangle
    // We add a tiny epsilon (0.00001) to X and Y to prevent the "cross" pinching
    float eps = 1e-5f;
    if (p_exponent == 20.0f) { // Optimized for Circle
        return sqrtf(X*X + Y*Y + eps);
    }
    float p = p_exponent * 0.1f;
    return powf(powf(fabsf(X) + eps, p) + powf(fabsf(Y) + eps, p), 1.0f / p);
}

static void update_saliency_poles(box_topomorph_t *t, uint8_t *srcY) {
    int w = t->width, h = t->height;
    int64_t sx1=0, sy1=0, sw1=0, sx2=0, sy2=0, sw2=0;
    for(int y=0; y<h; y+=8) {
        for(int x=0; x<w; x+=8) {
            int val = srcY[y*w+x];
            if (val < 64) continue;
            int weight = val*val;
            if (x < w/2) { sx1+=x*weight; sy1+=y*weight; sw1+=weight; }
            else { sx2+=x*weight; sy2+=y*weight; sw2+=weight; }
        }
    }
    float cx = w*0.5f, cy = h*0.5f;
    float tp1x = sw1 ? (sx1/sw1-cx)/cx : -0.5f, tp1y = sw1 ? (sy1/sw1-cy)/cy : 0;
    float tp2x = sw2 ? (sx2/sw2-cx)/cx : 0.5f, tp2y = sw2 ? (sy2/sw2-cy)/cy : 0;
    t->p1_x += (tp1x - t->p1_x)*0.05f; t->p1_y += (tp1y - t->p1_y)*0.05f;
    t->p2_x += (tp2x - t->p2_x)*0.05f; t->p2_y += (tp2y - t->p2_y)*0.05f;
}

static void process_core(box_topomorph_t *t, VJFrame *frame, int *args, int genus) {
    int w = t->width, h = t->height, size = w * h;
    t->time += args[0] * 0.000725f;
    t->phase += args[4] * 0.000725f;
    const float branches = (float)args[2], swirl = args[3]*0.01f, zoom = 0.8f + (args[1]/500.0f)*12.0f;
    const float factor = branches/zoom, inv_2pi = 0.15915494f, pitch = args[6]*0.01f, influence = args[9]*0.01f;
    const float shape_p = (float)args[11];
    const int32_t fb = TO_FP(args[5]*0.01f), inv_fb = FP_ONE - fb;
    uint8_t *srcY = frame->data[0], *srcU = frame->data[1], *srcV = frame->data[2];
    float cx = w*0.5f, cy = h*0.5f;
    float p1x = t->p1_x*influence, p1y = t->p1_y*influence;
    float p2x = t->p2_x*influence, p2y = t->p2_y*influence;

    #pragma omp parallel for schedule(static)
    for (int i=0; i<size; i++) {
        float dx = (i%w - cx)/cx, dy = (i/w - cy)/cy, X, Y;
        if (genus == 0) { X = dx - (p1x+p2x)*0.5f; Y = dy - (p1y+p2y)*0.5f; }
        else if (genus == 1) { 
            float X1 = dx-p1x, Y1 = dy-p1y, X2 = dx-p2x, Y2 = dy-p2y;
            X = X1*X2 - Y1*Y2; Y = X1*Y2 + Y1*X2;
        } else {
            float X1=dx-p1x, Y1=dy-p1y, X2=dx-p2x, Y2=dy-p2y, X3=dx+(p1x+p2x)*0.5f, Y3=dy+(p1y+p2y)*0.5f;
            float tx = X1*X2 - Y1*Y2, ty = X1*Y2 + Y1*X2;
            X = tx*X3 - ty*Y3; Y = tx*Y3 + ty*X3;
        }
        float r = fmaxf(get_radius_smooth(X, Y, shape_p), 1e-6f);
        float theta = atan2f(Y, X), log_r = logf(r);
        float v_f = (log_r*factor) + (theta*inv_2pi*branches) + t->time;
        float u_f = (theta*inv_2pi*branches) - (log_r*factor*pitch) + t->phase + (swirl*log_r);
        int32_t u_fp = (int32_t)(u_f*65536.0f), v_fp = (int32_t)(v_f*65536.0f);
        int64_t accY = (int64_t)sample_bilinear(srcY, u_fp, v_fp, w, h) << FP_SHIFT;
        int64_t accU = (int64_t)sample_bilinear_uv(srcU, u_fp, v_fp, w, h) << FP_SHIFT;
        int64_t accV = (int64_t)sample_bilinear_uv(srcV, u_fp, v_fp, w, h) << FP_SHIFT;
        t->histY[i] = ((accY * inv_fb) + ((int64_t)t->histY[i] * fb) + (1LL<<15)) >> FP_SHIFT;
        t->histU[i] = ((accU * inv_fb) + ((int64_t)t->histU[i] * fb) + (1LL<<15)) >> FP_SHIFT;
        t->histV[i] = ((accV * inv_fb) + ((int64_t)t->histV[i] * fb) + (1LL<<15)) >> FP_SHIFT;
        int y_val = t->histY[i] >> FP_SHIFT;
        t->dstY[i] = t->gamma_lut[((y_val<0?0:y_val>255?255:y_val)*1023)/255];
        t->dstU[i] = clamp_u8(((t->histU[i]>>FP_SHIFT)*1056>>10)+128);
        t->dstV[i] = clamp_u8(((t->histV[i]>>FP_SHIFT)*1056>>10)+128);
    }
    veejay_memcpy(srcY, t->dstY, size); veejay_memcpy(srcU, t->dstU, size); veejay_memcpy(srcV, t->dstV, size);
}

void topomorph_apply(void *ptr, VJFrame *frame, int *args) {
    update_saliency_poles((box_topomorph_t*)ptr, frame->data[0]);
    process_core((box_topomorph_t*)ptr, frame, args, args[8]);
}

void topomorph_free(void *ptr){
    box_topomorph_t *t = (box_topomorph_t*)ptr;
    if (t) { free(t->histY); free(t->dstY); free(t); }
}