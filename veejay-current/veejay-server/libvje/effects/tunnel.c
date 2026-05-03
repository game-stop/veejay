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

#define SIN_LUT_SIZE 4096
#define SIN_LUT_MASK 4095
#define SIN_LUT_MUL 651.898646904f
#define MAX_LAYERS 2
#define GAMMA_LUT_SIZE 1024

#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)
#define TO_FP(x) ((int32_t)((x) * FP_ONE))
#define FROM_FP(x) ((float)(x) / FP_ONE)


enum { MODE_RECT = 0, MODE_CIRCLE, MODE_DIAMOND, MODE_STAR,MODE_FLOWER, MODE_FLOW_TURBULENCE };

typedef struct {
    uint8_t *dstY, *dstU, *dstV;
    int *u_lut, *v_lut, *shade_lut;
    int *histY, *histU, *histV;
    float sin_lut[SIN_LUT_SIZE];
    uint8_t gamma_lut[GAMMA_LUT_SIZE];
    double time;
    int width, height;
    float velocity;
    int n_threads;
    float vel_state;
    float acc_state;
    int last_shape;
    float zoom_state;
    float zoom_vel;
    float phase;
    float phase_vel;
} box_tunnel_t;

#define FAST_SIN(val) (t->sin_lut[(int)((val)*SIN_LUT_MUL) & SIN_LUT_MASK])

static inline uint8_t clamp_u8(float v) {
    int i = (int)v;
    if (i < 0) return 0;
    if (i > 255) return 255;
    return (uint8_t)i;
}

static void generate_geometry(box_tunnel_t *t, int shape) {

    float cx = t->width * 0.5f;
    float cy = t->height * 0.5f;
    int size = t->width * t->height;

    #pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for (int y = 0; y < t->height; y++) {
        for (int x = 0; x < t->width; x++) {

            int i = y * t->width + x;

            float dx = (x - cx) / cx;
            float dy = (y - cy) / cy;

            float d = 0.0f;
            float u = 0.0f;

            switch (shape) {
            
                case MODE_CIRCLE:
                    d = sqrtf(dx*dx + dy*dy);
                    u = atan2f(dy, dx) * (0.15915494f) + 0.5f;
                    break;

                case MODE_DIAMOND:
                    d = fabsf(dx) + fabsf(dy);
                    u = atan2f(dy, dx) * (0.15915494f) + 0.5f;
                    break;

                case MODE_STAR:
                {
                    float ang = atan2f(dy, dx);
                    d = sqrtf(dx*dx + dy*dy);

                    float a = (ang + M_PI) * (0.7957747f);
                    float tri = fabsf(2.0f * (a - floorf(a + 0.5f)));

                    float mod = 0.65f + 0.35f * tri;
                    d /= fmaxf(mod, 0.2f);

                    u = ang * (0.15915494f) + 0.5f;
                    break;
                }

                case MODE_FLOW_TURBULENCE:
                {
                    float gx = dx;
                    float gy = dy;

                    float w1 = sinf(gx * 2.1f + gy * 1.7f);
                    float w2 = cosf(gy * 2.3f - gx * 1.4f);

                    float wx = gx + 0.35f * w1;
                    float wy = gy + 0.35f * w2;

                    float ang = atan2f(wy, wx);
                    float r = sqrtf(wx*wx + wy*wy);

                    float swirl = sinf(3.0f * ang + r * 6.0f);

                    float fx = wx + 0.25f * swirl;
                    float fy = wy + 0.25f * swirl;

                    d = fx*fx + fy*fy;

                    u = fx * 0.5f + 0.5f;

                    t->v_lut[i] = (int)((1.0f / (1.0f + d * 3.0f)) * FP_ONE);
                    t->shade_lut[i] = (int)(fminf(1.0f, d * 2.5f) * FP_ONE);
                    t->u_lut[i] = (int)((u - floorf(u)) * FP_ONE);

                    continue;
                }
                case MODE_FLOWER:
                {
                    float ang = atan2f(dy, dx);
                    d = sqrtf(dx*dx + dy*dy);

                    float petal = 0.75f + 0.25f * cosf(5.0f * ang);

                    d /= fmaxf(petal, 0.2f);

                    u = ang * (0.15915494f) + 0.5f;
                    break;
                }
                case MODE_RECT:
                default:
                {
                    float ax = fabsf(dx), ay = fabsf(dy);
                    d = fmaxf(ax, ay);

                    if (d > 1e-6f) {
                        if (ax > ay)
                            u = (dx > 0.0f) ? (dy / ax) : (dy / ax + 4.0f);
                        else
                            u = (dy > 0.0f) ? (2.0f - dx / ay) : (6.0f + dx / ay);

                        u = (u + 2.0f) / 8.0f;
                    }
                    break;
                }
            }

            if (shape != MODE_FLOW_TURBULENCE) {
                t->v_lut[i] = (int)(logf(d + 1e-6f) * FP_ONE); 
                t->shade_lut[i] = (int)(fminf(1.0f, d * 5.0f) * FP_ONE);
                t->u_lut[i] = (int)((u - floorf(u)) * FP_ONE);
            }
        }
    }

    t->last_shape = shape;
}

vj_effect *tunnel_init1(int width, int height) {
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    ve->num_params = 9;
    ve->defaults = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int)*ve->num_params);

    ve->defaults[0]=5; ve->defaults[1]=0; ve->defaults[2]=40;
    ve->defaults[3]=15; ve->defaults[4]=100; ve->defaults[5]=0;
    ve->defaults[6]=65; ve->defaults[7]=0; ve->defaults[8] = 2;

    ve->limits[0][0]=-100; ve->limits[1][0]=100;    // Speed
    ve->limits[0][1]=-100; ve->limits[1][1]=100;    // Twist
    ve->limits[0][2]=-100; ve->limits[1][2]=100;    // Swirl Lin
    ve->limits[0][3]=0;    ve->limits[1][3]=100;    // Swirl Sine
    ve->limits[0][4]=0;   ve->limits[1][4]=800;    // Zoom
    ve->limits[0][5]=0;    ve->limits[1][5]=2000;   // Layer Offset
    ve->limits[0][6]=0;    ve->limits[1][6]=100;    // Feedback
    ve->limits[0][7]=0;    ve->limits[1][7]=5;      // Shape Mode
    ve->limits[0][8]=0;    ve->limits[1][8]=1; // High Quality

    ve->description = "Fractal Tunnel (Multi-Geometry)";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params, 
        "Speed","Twist","Swirl Linear","Swirl Sine","Zoom","Offset","Feedback","Shape", "High Quality");
    return ve;
}

vj_effect *tunnel_init(int width, int height) {
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    ve->num_params = 9;
    ve->defaults = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int)*ve->num_params);

    ve->defaults[0]=15; ve->defaults[1]=40; ve->defaults[2]=20;
    ve->defaults[3]=0;  ve->defaults[4]=100; ve->defaults[5]=0;
    ve->defaults[6]=60; ve->defaults[7]=1;   ve->defaults[8]=1;

    ve->limits[0][0]=-100; ve->limits[1][0]=100;  // Speed
    ve->limits[0][1]=0;    ve->limits[1][1]=100;  // Curve Intensity
    ve->limits[0][2]=0;    ve->limits[1][2]=100;  // Curve Speed
    ve->limits[0][3]=-100; ve->limits[1][3]=100;  // Swirl
    ve->limits[0][4]=0;    ve->limits[1][4]=400;  // Zoom
    ve->limits[0][5]=0;    ve->limits[1][5]=1000; // Layer Offset
    ve->limits[0][6]=0;    ve->limits[1][6]=100;  // Feedback
    ve->limits[0][7]=0;    ve->limits[1][7]=5;   // Shape Mode
    ve->limits[0][8]=0;    ve->limits[1][8]=1;    // HQ

    ve->description = "Tunnel";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params, 
        "Speed","Curve Int","Curve Speed","Swirl","Zoom","Offset","Feedback","Shape", "High Quality");
    return ve;
}

void *tunnel_malloc(int width, int height) {
    box_tunnel_t *t = (box_tunnel_t*) vj_calloc(sizeof(box_tunnel_t));
    if(!t) return NULL;

    t->width = width; t->height = height;
    int size = width * height;

    t->u_lut = (int*) vj_malloc(sizeof(int) * size * 3);
    t->v_lut = t->u_lut + size;
    t->shade_lut = t->v_lut + size;

    t->histY = (int*) vj_calloc(sizeof(int) * size * 3); 
    t->histU = t->histY + size;
    t->histV = t->histU + size;

    t->dstY = (uint8_t*) vj_malloc(size * 3);
    t->dstU = t->dstY + size;
    t->dstV = t->dstU + size;

    for(int i = 0; i < SIN_LUT_SIZE; i++)
        t->sin_lut[i] = sinf(i * 2.0f * M_PI / SIN_LUT_SIZE);

    for(int i = 0; i < GAMMA_LUT_SIZE; i++) {
        float val = (float)i / (GAMMA_LUT_SIZE - 1);
        t->gamma_lut[i] = clamp_u8(powf(val, 0.85f) * 255.0f);
    }

    t->n_threads = vje_advise_num_threads(size);
    generate_geometry(t, MODE_RECT); 

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

    int64_t sum =
        w00 * p00 +
        w10 * p10 +
        w01 * p01 +
        w11 * p11;

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

    int64_t sum =
        w00 * p00 +
        w10 * p10 +
        w01 * p01 +
        w11 * p11;

    return (int32_t)(sum >> (FP_SHIFT * 2));
}

void tunnel_free(void *ptr){
    box_tunnel_t *t = (box_tunnel_t*)ptr;
    if (!t) return;
    free(t->u_lut);
    free(t->histY);
    free(t->dstY);
    free(t);
}


void tunnel_apply(void *ptr, VJFrame *frame, int *args) {
    box_tunnel_t *t = (box_tunnel_t*)ptr;
    int w = t->width, h = t->height, size = w * h;

    if (args[7] != t->last_shape)
        generate_geometry(t, args[7]);

    float speed = args[0] * 0.005f;
    t->time += speed;

    float raw_speed = args[0] * 0.01f;
    float speed_target = raw_speed * raw_speed * raw_speed * 0.15f;

    t->vel_state += (speed_target - t->vel_state) * 0.08f;
    t->time += t->vel_state;

    float ci_target = tanhf(args[1] * 0.015f) * 0.75f;
    t->acc_state += (ci_target - t->acc_state) * 0.06f;
    float curve_int = t->acc_state;

    float cs_input = args[2] * 0.01f;
    float cs_target = 0.2f * cs_input * cs_input + 0.02f;

    t->phase_vel += (cs_target - t->phase_vel) * 0.05f;
    float curve_spd = t->phase_vel;

    float swirl = tanhf(args[3] * 0.02f) * 1.2f;

    float zoom = (args[4] * 0.01f) + 0.2f;

    float po_target = args[5] * 0.002f;
    t->phase += (po_target - t->phase) * 0.05f;
    float phase_offset = t->phase;

    int active_layers = (args[8] == 0) ? 1 : MAX_LAYERS;
    int use_high_quality = (args[8] == 1);

    int32_t fb_fp = TO_FP(args[6] * 0.01f);
    int32_t inv_fb_fp = TO_FP(1.0f - (args[6] * 0.01f));

    uint8_t *srcY = frame->data[0];
    uint8_t *srcU = frame->data[1];
    uint8_t *srcV = frame->data[2];

    float liss_x = cosf(t->time * curve_spd * 2.0f) * curve_int;
    float liss_y = sinf(t->time * curve_spd * 3.0f) * curve_int;

    int32_t chroma_fb_fp = (fb_fp * 3) >> 2;
    int32_t chroma_inv_fp = TO_FP(1.0f) - chroma_fb_fp;

    int32_t current_inv_fb = FP_ONE - fb_fp;
    int32_t current_inv_chroma_fb = FP_ONE - chroma_fb_fp;

    #pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for (int i = 0; i < size; i++) {

        float u_base = FROM_FP(t->u_lut[i]);
        float v_base = FROM_FP(t->v_lut[i]);

        float v = v_base * zoom + t->time;
        v += liss_y;

        v += phase_offset * sinf(v_base * 4.0f + t->time * 0.5f);

        float u = u_base + liss_x;

        int px = i % w;
        int py = i / w;

        float dx = (px - w * 0.5f) / (w * 0.5f);
        float dy = (py - h * 0.5f) / (h * 0.5f);

        float d = sqrtf(dx*dx + dy*dy);

        float dir_phase = (speed >= 0.0f) ? 1.0f : -1.0f;

        float swirl_lin = swirl;
        float swirl_sin = swirl * 0.7f;

        float ang = atan2f(dy, dx);
        u += swirl_lin * (0.3f * ang * 0.15915494f);

        u += swirl_lin * d * dir_phase * 0.5f;

        u += swirl_sin * FAST_SIN(
            d * 2.5f +
            dx * 1.7f -
            dy * 1.3f +
            t->time * 0.8f
        );

        int64_t accY = 0, accU = 0, accV = 0;

        for (int layer = 0; layer < active_layers; layer++) {

            float uu = u;
            float vv = v + (float)layer * (phase_offset * 0.25f);

            int32_t u_fp = TO_FP(uu - floorf(uu));
            int32_t v_fp = TO_FP(vv - floorf(vv));

            if (use_high_quality) {
                accY += (int64_t)sample_bilinear(srcY, u_fp, v_fp, w, h) << FP_SHIFT;
                accU += (int64_t)sample_bilinear_uv(srcU, u_fp, v_fp, w, h) << FP_SHIFT;
                accV += (int64_t)sample_bilinear_uv(srcV, u_fp, v_fp, w, h) << FP_SHIFT;
            } else {
                int tx = ((u_fp >> 8) * (w - 1)) >> 8;
                int ty = ((v_fp >> 8) * (h - 1)) >> 8;

                accY += (int64_t)srcY[ty * w + tx] << FP_SHIFT;
                accU += (int64_t)(srcU[ty * w + tx] - 128) << FP_SHIFT;
                accV += (int64_t)(srcV[ty * w + tx] - 128) << FP_SHIFT;
            }
        }

        if (active_layers > 1) {
            accY /= active_layers;
            accU /= active_layers;
            accV /= active_layers;
        }

        t->histY[i] = ((accY * current_inv_fb) + ((int64_t)t->histY[i] * fb_fp) + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT;
        t->histU[i] = ((accU * current_inv_chroma_fb) + ((int64_t)t->histU[i] * chroma_fb_fp) + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT;
        t->histV[i] = ((accV * current_inv_chroma_fb) + ((int64_t)t->histV[i] * chroma_fb_fp) + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT;

        int y_val = t->histY[i] >> FP_SHIFT;
        int u_val = t->histU[i] >> FP_SHIFT;
        int v_val = t->histV[i] >> FP_SHIFT;

        u_val = (u_val * 1056) >> 10;
        v_val = (v_val * 1056) >> 10;

        int y_clamped = (y_val < 0) ? 0 : (y_val > 255 ? 255 : y_val);
        t->dstY[i] = t->gamma_lut[y_clamped * (GAMMA_LUT_SIZE - 1) / 255];

        t->dstU[i] = clamp_u8(u_val + 128);
        t->dstV[i] = clamp_u8(v_val + 128);
    }

    veejay_memcpy(srcY, t->dstY, size);
    veejay_memcpy(srcU, t->dstU, size);
    veejay_memcpy(srcV, t->dstV, size);
}

