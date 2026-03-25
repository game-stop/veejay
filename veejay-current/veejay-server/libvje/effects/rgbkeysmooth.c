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
#include <veejaycore/vjmem.h>
#include <math.h>
#pragma GCC optimize ("unroll-loops")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#define SMOOTHSTEP(edge0, edge1, x) \
    ({ float t = CLAMP(((x) - (edge0)) / ((edge1) - (edge0)), 0.0f, 1.0f); \
       t * t * (3.0f - 2.0f * t); })

typedef struct
{
    int n_threads;
    float mag_lut[32769];
    float inv_mag_lut[32769];
    uint8_t *alpha_map;
    uint8_t *alpha_temp;
} rgbkey_t;

vj_effect *rgbkeysmooth_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 12; // yay

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 4500; ve->defaults[1] = 0;   ve->defaults[2] = 255;
    ve->defaults[3] = 0;    ve->defaults[4] = 20;  ve->defaults[5] = 180;
    ve->defaults[6] = 20;   ve->defaults[7] = 235; ve->defaults[8] = 160;
    ve->defaults[9] = 100;  ve->defaults[10] = 0;  ve->defaults[11] = 60;

    for(int i=0; i<12; i++)
    {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 255;
    }

    ve->limits[1][0]  = 8500;
    ve->limits[1][10] = 2;
    ve->limits[1][11] = 255;

    ve->description = "Master Chroma Key";
    ve->param_description = vje_build_param_list(ve->num_params, 
        "Hue Angle", "Red", "Green", "Blue", "Matte Min", "Matte Max",
        "Luma Min", "Luma Max", "Spill Amount", "Spill Recovery", "View Mode", "Softness");

    ve->extra_frame = 1;
    ve->sub_format   = 1;
    ve->rgb_conv     = 1;

    return ve;
}

void *rgbkeysmooth_malloc(int w, int h) {
    rgbkey_t *r = (rgbkey_t*) vj_malloc(sizeof(rgbkey_t));
    if(!r) return NULL;
    r->n_threads = vje_advise_num_threads(w * h);
    r->alpha_map = (uint8_t*) vj_malloc(w * h);
    r->alpha_temp = (uint8_t*) vj_malloc(w * h);
    for (int i = 0; i <= 32768; i++) {
        float m = sqrtf((float)i);
        r->mag_lut[i] = (m < 0.0001f) ? 0.0001f : m;
        r->inv_mag_lut[i] = 1.0f / r->mag_lut[i];
    }
    return (void*) r;
}

void rgbkeysmooth_free(void *ptr) {
    if(ptr) {
        rgbkey_t *r = (rgbkey_t*)ptr;
        if(r->alpha_map) free(r->alpha_map);
        if(r->alpha_temp) free(r->alpha_temp);
        free(r);
    }
}

void rgbkeysmooth_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    rgbkey_t *rgbkey = (rgbkey_t*) ptr;
    int iy, iu, iv;
    _rgb2yuv(args[1], args[2], args[3], iy, iu, iv);

    const int kU_int = iu - 128;
    const int kV_int = iv - 128;
    const float kInvMag = rgbkey->inv_mag_lut[kU_int * kU_int + kV_int * kV_int];
    const float kU = (float)kU_int * kInvMag;
    const float kV = (float)kV_int * kInvMag;
    
    const float wedge_cos = cosf(((float)args[0] / 100.0f) * (M_PI / 180.0f));
    const float hue_denom = 1.0f / ((1.0f - wedge_cos) * 128.0f);
    
    const float m_min = (float)args[4] / 255.0f;
    const float m_max = ((float)args[5] / 255.0f <= m_min) ? m_min + 0.01f : (float)args[5] / 255.0f;
    const float m_range_inv = 1.0f / (m_max - m_min);

    const float l_min = (float)args[6];
    const float l_max = (float)args[7];
    const float s_amt = (float)args[8] / 255.0f;
    const float l_rec_half = (float)args[9] / 510.0f;
    
    const int mode = args[10];
    const int soft = args[11];

    const int w = frame->width;
    const int h = frame->height;
    const int len = w * h;

    const float l_min_inv = 1.0f / (l_min + 0.001f);
    const float l_max_inv = 1.0f / (255.0f - l_max + 0.001f);

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
        /*
        // the vectorized loop is slower, because no math is the fastest math
        for (int i = 0; i < len; i++) {
            int u_int = (int)Cb[i] - 128;
            int v_int = (int)Cr[i] - 128;
            int d = u_int * u_int + v_int * v_int;
            
            float fU = (float)u_int;
            float fV = (float)v_int;
            float fY = (float)Y[i];
            
            float invM = l_inv[d];
            float mag  = l_mag[d];
            float dot  = (fU * kU + fV * kV) * invM;
            
            float lw_low  = CLAMP(fY * l_min_inv, 0.0f, 1.0f);
            float lw_high = CLAMP((255.0f - fY) * l_max_inv, 0.0f, 1.0f);
            float lw = lw_low * lw_high;

            float raw = (dot - wedge_cos) * hue_denom * mag * lw;
            float t = CLAMP((raw - m_min) * m_range_inv, 0.0f, 1.0f);
            float smooth_alpha = 1.0f - (t * t * (3.0f - 2.0f * t));

            AM[i] = (uint8_t)(smooth_alpha * 255.0f);
        }*/

        for (int i = 0; i < len; i++) {
            int u_int = (int)Cb[i] - 128;
            int v_int = (int)Cr[i] - 128;
            int d = u_int * u_int + v_int * v_int;

            if (d < 16) { // fast path
                AM[i] = 255;
                continue;
            }

            float invM = l_inv[d];
            float dot  = ((float)u_int * kU + (float)v_int * kV) * invM;

            if (dot <= wedge_cos) { // fast path
                AM[i] = 255;
                continue;
            }

            float fY = (float)Y[i];
            float lw = CLAMP(fY * l_min_inv, 0.0f, 1.0f) * CLAMP((255.0f - fY) * l_max_inv, 0.0f, 1.0f);
            float raw = (dot - wedge_cos) * hue_denom * l_mag[d] * lw;
            float t = CLAMP((raw - m_min) * m_range_inv, 0.0f, 1.0f);

            AM[i] = (uint8_t)((1.0f - (t * t * (3.0f - 2.0f * t))) * 255.0f);
        }

        if (soft > 0) {
            #pragma omp for schedule(static)
            for (int y = 0; y < h; y++) {
                uint8_t *restrict row_in  = &AM[y * w];
                uint8_t *restrict row_out = &AT[y * w];
                row_out[0] = row_in[0];
                row_out[w-1] = row_in[w-1];
                for (int x = 1; x < w-1; x++) {
                    int sum = row_in[x-1] + row_in[x] + row_in[x+1];
                    int avg = (sum * 21846) >> 16;
                    row_out[x] = row_in[x] + (((avg - row_in[x]) * soft + 128) >> 8);
                }
            }

            #pragma omp for schedule(static)
            for (int y = 1; y < h-1; y++) {
                uint8_t *restrict r_top  = &AT[(y-1)*w];
                uint8_t *restrict r_mid  = &AT[y*w];
                uint8_t *restrict r_bot  = &AT[(y+1)*w];
                uint8_t *restrict r_dest = &AM[y*w];
                for (int x = 0; x < w; x++) {
                    int sum = (r_top[x] + r_mid[x] + r_bot[x]);
                    int avg = (sum * 21846) >> 16;
                    r_dest[x] = r_mid[x] + (((avg - r_mid[x]) * soft + 128) >> 8);
                }
            }
        }

        if (mode == 1) {
            #pragma omp for schedule(static)
            for (int i = 0; i < len; i++) {
                Y[i] = AM[i];
                Cb[i] = 128;
                Cr[i] = 128;
            }
        }
        else if (mode == 2) {
            #pragma omp for schedule(static)
            for (int i = 0; i < len; i++) {
                int u = (int)Cb[i]-128;
                int v = (int)Cr[i]-128;
                int d = u*u + v*v;

                float invM = l_inv[d];
                float dot = ((float)u * kU + (float)v * kV) * invM;
                
                float pos_dot = dot > 0.0f ? dot : 0.0f;
                float s = pos_dot * s_amt * l_mag[d];

                Y[i]  = (uint8_t)CLAMP((float)Y[i] + (s * l_rec_half), 0.0f, 255.0f);
                Cb[i] = (uint8_t)CLAMP(((float)u - kU * s) + 128.0f, 0.0f, 255.0f);
                Cr[i] = (uint8_t)CLAMP(((float)v - kV * s) + 128.0f, 0.0f, 255.0f);
            }
        }
        else {
            if (s_amt > 0.0f) {
                #pragma omp for schedule(static)
                for (int i = 0; i < len; i++) {
                    int a = AM[i];
                    if(LIKELY(a == 0)) { // fast path
                        Y[i] = Y2[i]; Cb[i] = Cb2[i]; Cr[i] = Cr2[i];
                        continue;
                    }

                    float fU = (float)Cb[i] - 128.0f;
                    float fV = (float)Cr[i] - 128.0f;
                    float fY = (float)Y[i];

                    int d = (int)(fU * fU + fV * fV);
                    float dot = fmaxf((fU * kU + fV * kV) * l_inv[d], 0.0f);
                    float s = dot * s_amt * l_mag[d];

                    fU -= kU * s;
                    fV -= kV * s;
                    fY += s * l_rec_half;

                    int ia = 255 - a;
                    Y[i]  = DIV255((int)CLAMP(fY, 0.0f, 255.0f) * a + Y2[i] * ia);
                    Cb[i] = DIV255((int)CLAMP(fU + 128.0f, 0.0f, 255.0f) * a + Cb2[i] * ia);
                    Cr[i] = DIV255((int)CLAMP(fV + 128.0f, 0.0f, 255.0f) * a + Cr2[i] * ia);
                }
            } else {
                #pragma omp for schedule(static)
                for (int i = 0; i < len; i++) {
                    const uint8_t a = AM[i];
                    const uint8_t ia = 255 - a;

                    Y[i]  = DIV255((int)Y[i] * a + (int)Y2[i] * ia);
                    Cb[i] = DIV255((int)Cb[i] * a + (int)Cb2[i] * ia);
                    Cr[i] = DIV255((int)Cr[i] * a + (int)Cr2[i] * ia);
                }
            }
        }
    }
}
