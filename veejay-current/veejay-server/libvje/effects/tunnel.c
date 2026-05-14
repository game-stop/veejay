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
#include <stdint.h>
#include <stdlib.h>

#define SIN_LUT_SIZE 4096
#define SIN_LUT_MASK 4095
#define SIN_LUT_MUL 651.898646904f
#define MAX_LAYERS 2
#define GAMMA_LUT_SIZE 1024

#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)
#define FP_MASK (FP_ONE - 1)
#define FP_INV (1.0f / (float)FP_ONE)
#define TO_FP(x) ((int32_t)((x) * FP_ONE))
#define FROM_FP(x) ((float)(x) * FP_INV)

#define EH_PI 3.14159265358979323846f
#define EH_TWO_PI 6.28318530717958647692f
#define EH_INV_TWO_PI 0.15915494309189533577f

enum { MODE_RECT = 0, MODE_CIRCLE, MODE_DIAMOND, MODE_STAR, MODE_FLOWER, MODE_FLOW_TURBULENCE };

typedef struct {
    uint8_t *dstY, *dstU, *dstV;
    int *u_lut, *v_lut, *shade_lut;
    int *histY, *histU, *histV;
    float *warp_pos_lut, *warp_neg_lut, *wave_lut;
    float sin_lut[SIN_LUT_SIZE];
    uint8_t gamma_lut[GAMMA_LUT_SIZE];
    uint8_t gamma8[256];
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

#define FAST_SIN_T(t, val) ((t)->sin_lut[(int)((val) * SIN_LUT_MUL) & SIN_LUT_MASK])

static inline uint8_t clamp_u8_float(float v) {
    int i = (int)v;
    if (i < 0) return 0;
    if (i > 255) return 255;
    return (uint8_t)i;
}

static inline uint8_t clamp_u8_int(int v) {
    if ((unsigned int)v > 255U)
        return (v < 0) ? 0 : 255;
    return (uint8_t)v;
}

static inline int32_t frac_to_fp_fast(float x) {
    int xi = (int)x;
    float f = x - (float)xi;
    if (f < 0.0f)
        f += 1.0f;
    return ((int32_t)(f * (float)FP_ONE)) & FP_MASK;
}

static inline int bilerp_i(int p00, int p10, int p01, int p11, int fx, int fy) {
    int a = p00 + (((p10 - p00) * fx) >> FP_SHIFT);
    int b = p01 + (((p11 - p01) * fx) >> FP_SHIFT);
    return a + (((b - a) * fy) >> FP_SHIFT);
}

static inline void sample_bilinear_yuv_fast(
    const uint8_t *srcY,
    const uint8_t *srcU,
    const uint8_t *srcV,
    int32_t u_fp,
    int32_t v_fp,
    int w,
    int h,
    int *outY,
    int *outU,
    int *outV)
{
    int32_t u = u_fp & FP_MASK;
    int32_t v = v_fp & FP_MASK;

    int32_t xf = u * (w - 1);
    int32_t yf = v * (h - 1);

    int x = xf >> FP_SHIFT;
    int y = yf >> FP_SHIFT;

    int fx = xf & FP_MASK;
    int fy = yf & FP_MASK;

    int x1 = (x + 1 >= w) ? 0 : x + 1;
    int y1 = (y + 1 >= h) ? 0 : y + 1;

    int row0 = y * w;
    int row1 = y1 * w;

    int i00 = row0 + x;
    int i10 = row0 + x1;
    int i01 = row1 + x;
    int i11 = row1 + x1;

    *outY = bilerp_i(srcY[i00], srcY[i10], srcY[i01], srcY[i11], fx, fy);

    *outU = bilerp_i(
        (int)srcU[i00] - 128,
        (int)srcU[i10] - 128,
        (int)srcU[i01] - 128,
        (int)srcU[i11] - 128,
        fx, fy);

    *outV = bilerp_i(
        (int)srcV[i00] - 128,
        (int)srcV[i10] - 128,
        (int)srcV[i01] - 128,
        (int)srcV[i11] - 128,
        fx, fy);
}

static inline void tunnel_write_pixel(
    box_tunnel_t *t,
    int i,
    int32_t accY,
    int32_t accU,
    int32_t accV,
    int32_t fb_fp,
    int32_t inv_fb_fp,
    int32_t chroma_fb_fp,
    int32_t inv_chroma_fp)
{
    int hy = (int)(((int64_t)accY * inv_fb_fp + (int64_t)t->histY[i] * fb_fp + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT);
    int hu = (int)(((int64_t)accU * inv_chroma_fp + (int64_t)t->histU[i] * chroma_fb_fp + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT);
    int hv = (int)(((int64_t)accV * inv_chroma_fp + (int64_t)t->histV[i] * chroma_fb_fp + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT);

    t->histY[i] = hy;
    t->histU[i] = hu;
    t->histV[i] = hv;

    int y_val = hy >> FP_SHIFT;
    int u_val = hu >> FP_SHIFT;
    int v_val = hv >> FP_SHIFT;

    u_val = (u_val * 1056) >> 10;
    v_val = (v_val * 1056) >> 10;

    t->dstY[i] = t->gamma8[clamp_u8_int(y_val)];
    t->dstU[i] = clamp_u8_int(u_val + 128);
    t->dstV[i] = clamp_u8_int(v_val + 128);
}

static void precompute_warp_luts(box_tunnel_t *t) {
    int w = t->width;
    int h = t->height;
    float cx = w * 0.5f;
    float cy = h * 0.5f;
    float inv_cx = 1.0f / cx;
    float inv_cy = 1.0f / cy;

    #pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for (int y = 0; y < h; y++) {
        float dy = ((float)y - cy) * inv_cy;
        int row = y * w;

        for (int x = 0; x < w; x++) {
            int i = row + x;
            float dx = ((float)x - cx) * inv_cx;
            float d = sqrtf(dx * dx + dy * dy);
            float ang = atan2f(dy, dx);
            float aterm = 0.3f * ang * EH_INV_TWO_PI;

            t->warp_pos_lut[i] = aterm + d * 0.5f;
            t->warp_neg_lut[i] = aterm - d * 0.5f;
            t->wave_lut[i] = d * 2.5f + dx * 1.7f - dy * 1.3f;
        }
    }
}

static void generate_geometry(box_tunnel_t *t, int shape) {
    int w = t->width;
    int h = t->height;

    float cx = w * 0.5f;
    float cy = h * 0.5f;
    float inv_cx = 1.0f / cx;
    float inv_cy = 1.0f / cy;

    #pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for (int y = 0; y < h; y++) {
        float dy = ((float)y - cy) * inv_cy;
        int row = y * w;

        for (int x = 0; x < w; x++) {
            int i = row + x;
            float dx = ((float)x - cx) * inv_cx;

            float d = 0.0f;
            float u = 0.0f;

            switch (shape) {
                case MODE_CIRCLE:
                    d = sqrtf(dx * dx + dy * dy);
                    u = atan2f(dy, dx) * EH_INV_TWO_PI + 0.5f;
                    break;

                case MODE_DIAMOND:
                    d = fabsf(dx) + fabsf(dy);
                    u = atan2f(dy, dx) * EH_INV_TWO_PI + 0.5f;
                    break;

                case MODE_STAR:
                {
                    float ang = atan2f(dy, dx);
                    d = sqrtf(dx * dx + dy * dy);

                    float a = (ang + EH_PI) * 0.7957747f;
                    float tri = fabsf(2.0f * (a - floorf(a + 0.5f)));

                    float mod = 0.65f + 0.35f * tri;
                    d /= fmaxf(mod, 0.2f);

                    u = ang * EH_INV_TWO_PI + 0.5f;
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
                    float r = sqrtf(wx * wx + wy * wy);

                    float swirl = sinf(3.0f * ang + r * 6.0f);

                    float fx = wx + 0.25f * swirl;
                    float fy = wy + 0.25f * swirl;

                    d = fx * fx + fy * fy;

                    u = fx * 0.5f + 0.5f;

                    t->v_lut[i] = (int)((1.0f / (1.0f + d * 3.0f)) * FP_ONE);
                    t->shade_lut[i] = (int)(fminf(1.0f, d * 2.5f) * FP_ONE);
                    t->u_lut[i] = (int)((u - floorf(u)) * FP_ONE);

                    continue;
                }

                case MODE_FLOWER:
                {
                    float ang = atan2f(dy, dx);
                    d = sqrtf(dx * dx + dy * dy);

                    float petal = 0.75f + 0.25f * cosf(5.0f * ang);
                    d /= fmaxf(petal, 0.2f);

                    u = ang * EH_INV_TWO_PI + 0.5f;
                    break;
                }

                case MODE_RECT:
                default:
                {
                    float ax = fabsf(dx);
                    float ay = fabsf(dy);
                    d = fmaxf(ax, ay);

                    if (d > 1e-6f) {
                        if (ax > ay)
                            u = (dx > 0.0f) ? (dy / ax) : (dy / ax + 4.0f);
                        else
                            u = (dy > 0.0f) ? (2.0f - dx / ay) : (6.0f + dx / ay);

                        u = (u + 2.0f) * 0.125f;
                    }
                    break;
                }
            }

            t->v_lut[i] = (int)(logf(d + 1e-6f) * FP_ONE);
            t->shade_lut[i] = (int)(fminf(1.0f, d * 5.0f) * FP_ONE);
            t->u_lut[i] = (int)((u - floorf(u)) * FP_ONE);
        }
    }

    t->last_shape = shape;
}

vj_effect *tunnel_init1(int width, int height) {
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    ve->num_params = 9;
    ve->defaults = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 5; ve->defaults[1] = 0; ve->defaults[2] = 40;
    ve->defaults[3] = 15; ve->defaults[4] = 100; ve->defaults[5] = 0;
    ve->defaults[6] = 65; ve->defaults[7] = 0; ve->defaults[8] = 2;

    ve->limits[0][0] = -100; ve->limits[1][0] = 100;
    ve->limits[0][1] = -100; ve->limits[1][1] = 100;
    ve->limits[0][2] = -100; ve->limits[1][2] = 100;
    ve->limits[0][3] = 0;    ve->limits[1][3] = 100;
    ve->limits[0][4] = 0;    ve->limits[1][4] = 800;
    ve->limits[0][5] = 0;    ve->limits[1][5] = 2000;
    ve->limits[0][6] = 0;    ve->limits[1][6] = 100;
    ve->limits[0][7] = 0;    ve->limits[1][7] = 5;
    ve->limits[0][8] = 0;    ve->limits[1][8] = 1;

    ve->description = "Tunnel";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params,
        "Speed", "Twist", "Swirl Linear", "Swirl Sine", "Zoom", "Offset", "Feedback", "Shape", "High Quality");
    return ve;
}

vj_effect *tunnel_init(int width, int height) {
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    ve->num_params = 9;
    ve->defaults = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = -5; ve->defaults[1] = 40; ve->defaults[2] = 20;
    ve->defaults[3] = 0;  ve->defaults[4] = 15; ve->defaults[5] = 0;
    ve->defaults[6] = 60; ve->defaults[7] = 1;  ve->defaults[8] = 1;

    ve->limits[0][0] = -100; ve->limits[1][0] = 100;
    ve->limits[0][1] = 0;    ve->limits[1][1] = 100;
    ve->limits[0][2] = 0;    ve->limits[1][2] = 100;
    ve->limits[0][3] = -100; ve->limits[1][3] = 100;
    ve->limits[0][4] = 0;    ve->limits[1][4] = 400;
    ve->limits[0][5] = 0;    ve->limits[1][5] = 1000;
    ve->limits[0][6] = 0;    ve->limits[1][6] = 100;
    ve->limits[0][7] = 0;    ve->limits[1][7] = 5;
    ve->limits[0][8] = 0;    ve->limits[1][8] = 1;

    ve->description = "Tunnel";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params,
        "Speed", "Curve Int", "Curve Speed", "Swirl", "Zoom", "Offset", "Feedback", "Shape", "High Quality");
    return ve;
}

void *tunnel_malloc(int width, int height) {
    box_tunnel_t *t = (box_tunnel_t*) vj_calloc(sizeof(box_tunnel_t));
    if (!t)
        return NULL;

    t->width = width;
    t->height = height;

    int size = width * height;

    t->n_threads = vje_advise_num_threads(size);
    t->last_shape = -1;

    t->u_lut = (int*) vj_malloc(sizeof(int) * size * 3);
    if (!t->u_lut) {
        free(t);
        return NULL;
    }

    t->v_lut = t->u_lut + size;
    t->shade_lut = t->v_lut + size;

    t->histY = (int*) vj_calloc(sizeof(int) * size * 3);
    if (!t->histY) {
        free(t->u_lut);
        free(t);
        return NULL;
    }

    t->histU = t->histY + size;
    t->histV = t->histU + size;

    t->dstY = (uint8_t*) vj_malloc(size * 3);
    if (!t->dstY) {
        free(t->histY);
        free(t->u_lut);
        free(t);
        return NULL;
    }

    t->dstU = t->dstY + size;
    t->dstV = t->dstU + size;

    t->warp_pos_lut = (float*) vj_malloc(sizeof(float) * size * 3);
    if (!t->warp_pos_lut) {
        free(t->dstY);
        free(t->histY);
        free(t->u_lut);
        free(t);
        return NULL;
    }

    t->warp_neg_lut = t->warp_pos_lut + size;
    t->wave_lut = t->warp_neg_lut + size;

    for (int i = 0; i < SIN_LUT_SIZE; i++)
        t->sin_lut[i] = sinf((float)i * EH_TWO_PI / (float)SIN_LUT_SIZE);

    for (int i = 0; i < GAMMA_LUT_SIZE; i++) {
        float val = (float)i / (float)(GAMMA_LUT_SIZE - 1);
        t->gamma_lut[i] = clamp_u8_float(powf(val, 0.85f) * 255.0f);
    }

    for (int i = 0; i < 256; i++)
        t->gamma8[i] = t->gamma_lut[(i * (GAMMA_LUT_SIZE - 1)) / 255];

    precompute_warp_luts(t);
    generate_geometry(t, MODE_RECT);

    return t;
}

void tunnel_free(void *ptr) {
    box_tunnel_t *t = (box_tunnel_t*) ptr;
    if (!t)
        return;

    free(t->warp_pos_lut);
    free(t->u_lut);
    free(t->histY);
    free(t->dstY);
    free(t);
}

void tunnel_apply(void *ptr, VJFrame *frame, int *args) {
    box_tunnel_t *t = (box_tunnel_t*) ptr;

    int w = t->width;
    int h = t->height;
    int size = w * h;

    int shape = args[7];
    if (shape != t->last_shape)
        generate_geometry(t, shape);

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
    float swirl_sin = swirl * 0.7f;

    float zoom = (args[4] * 0.01f) + 0.2f;

    float po_target = args[5] * 0.002f;
    t->phase += (po_target - t->phase) * 0.05f;
    float phase_offset = t->phase;

    int two_layers = (args[8] != 0);
    int high_quality = (args[8] == 1);

    int32_t fb_fp = TO_FP(args[6] * 0.01f);
    int32_t inv_fb_fp = FP_ONE - fb_fp;

    int32_t chroma_fb_fp = (fb_fp * 3) >> 2;
    int32_t inv_chroma_fp = FP_ONE - chroma_fb_fp;

    uint8_t *srcY = frame->data[0];
    uint8_t *srcU = frame->data[1];
    uint8_t *srcV = frame->data[2];

    float timef = (float)t->time;
    float liss_x = cosf(timef * curve_spd * 2.0f) * curve_int;
    float liss_y = sinf(timef * curve_spd * 3.0f) * curve_int;

    float v_phase_time = timef * 0.5f;
    float swirl_time = timef * 0.8f;
    float layer_step = phase_offset * 0.25f;

    int swirl_active = (swirl > 0.00001f || swirl < -0.00001f);
    int phase_active = (phase_offset > 0.00001f || phase_offset < -0.00001f);
    int layer_active = two_layers && (layer_step > 0.00001f || layer_step < -0.00001f);

    const float *warp_lut = (speed >= 0.0f) ? t->warp_pos_lut : t->warp_neg_lut;
    const float *wave_lut = t->wave_lut;

    int wm1 = w - 1;
    int hm1 = h - 1;

    if (high_quality) {
        #pragma omp parallel for schedule(static) num_threads(t->n_threads)
        for (int i = 0; i < size; i++) {
            float u_base = FROM_FP(t->u_lut[i]);
            float v_base = FROM_FP(t->v_lut[i]);

            float v = v_base * zoom + timef + liss_y;

            if (phase_active)
                v += phase_offset * FAST_SIN_T(t, v_base * 4.0f + v_phase_time);

            float u = u_base + liss_x;

            if (swirl_active) {
                u += swirl * warp_lut[i];
                u += swirl_sin * FAST_SIN_T(t, wave_lut[i] + swirl_time);
            }

            int y0, u0, v0;

            int32_t u_fp = frac_to_fp_fast(u);
            int32_t v_fp = frac_to_fp_fast(v);

            sample_bilinear_yuv_fast(srcY, srcU, srcV, u_fp, v_fp, w, h, &y0, &u0, &v0);

            int32_t accY;
            int32_t accU;
            int32_t accV;

            if (layer_active) {
                int y1, u1, v1;
                v_fp = frac_to_fp_fast(v + layer_step);
                sample_bilinear_yuv_fast(srcY, srcU, srcV, u_fp, v_fp, w, h, &y1, &u1, &v1);

                accY = (int32_t)((y0 + y1) << (FP_SHIFT - 1));
                accU = (int32_t)((u0 + u1) << (FP_SHIFT - 1));
                accV = (int32_t)((v0 + v1) << (FP_SHIFT - 1));
            } else {
                accY = (int32_t)y0 << FP_SHIFT;
                accU = (int32_t)u0 << FP_SHIFT;
                accV = (int32_t)v0 << FP_SHIFT;
            }

            tunnel_write_pixel(t, i, accY, accU, accV, fb_fp, inv_fb_fp, chroma_fb_fp, inv_chroma_fp);
        }
    } else {
        #pragma omp parallel for schedule(static) num_threads(t->n_threads)
        for (int i = 0; i < size; i++) {
            float u_base = FROM_FP(t->u_lut[i]);
            float v_base = FROM_FP(t->v_lut[i]);

            float v = v_base * zoom + timef + liss_y;

            if (phase_active)
                v += phase_offset * FAST_SIN_T(t, v_base * 4.0f + v_phase_time);

            float u = u_base + liss_x;

            if (swirl_active) {
                u += swirl * warp_lut[i];
                u += swirl_sin * FAST_SIN_T(t, wave_lut[i] + swirl_time);
            }

            int32_t u_fp = frac_to_fp_fast(u);
            int32_t v_fp = frac_to_fp_fast(v);

            int tx = ((u_fp >> 8) * wm1) >> 8;
            int ty = ((v_fp >> 8) * hm1) >> 8;
            int si = ty * w + tx;

            int32_t accY = (int32_t)srcY[si] << FP_SHIFT;
            int32_t accU = ((int32_t)srcU[si] - 128) << FP_SHIFT;
            int32_t accV = ((int32_t)srcV[si] - 128) << FP_SHIFT;

            if (layer_active) {
                v_fp = frac_to_fp_fast(v + layer_step);
                tx = ((u_fp >> 8) * wm1) >> 8;
                ty = ((v_fp >> 8) * hm1) >> 8;
                si = ty * w + tx;

                accY = (accY + ((int32_t)srcY[si] << FP_SHIFT)) >> 1;
                accU = (accU + (((int32_t)srcU[si] - 128) << FP_SHIFT)) >> 1;
                accV = (accV + (((int32_t)srcV[si] - 128) << FP_SHIFT)) >> 1;
            }

            tunnel_write_pixel(t, i, accY, accU, accV, fb_fp, inv_fb_fp, chroma_fb_fp, inv_chroma_fp);
        }
    }

    veejay_memcpy(srcY, t->dstY, size);
    veejay_memcpy(srcU, t->dstU, size);
    veejay_memcpy(srcV, t->dstV, size);
}
