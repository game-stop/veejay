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

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)
#define DIV3(x) (((x) * 21846) >> 16)

typedef struct {
    int n_threads;
    uint8_t *alpha_map;
    uint8_t *alpha_temp;
    uint8_t gamma_lut[256];
} promixer_t;

vj_effect *complexthreshold_init(int w, int h) {
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 12;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0]  = 1200;
    ve->defaults[1]  = 120;
    ve->defaults[2]  = 20;
    ve->defaults[3]  = 240;
    ve->defaults[4]  = 128;
    ve->defaults[5]  = 15;
    ve->defaults[6]  = 20;
    ve->defaults[7]  = 160;
    ve->defaults[8]  = 60;
    ve->defaults[9]  = 20;
    ve->defaults[10] = 0;
    ve->defaults[11] = 0;

    for(int i = 0; i < ve->num_params; i++) {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 255;
    }
    ve->limits[1][0] = 3600; 

    ve->description = "Kromatica Mixer (High-Fidelity Keyer)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    ve->param_description = vje_build_param_list(ve->num_params,
        "Key Color", "Key Reach", "Clip Black", "Clip White", "Matte Gamma",
        "Sat Gate", "Shadow Prot", "Spill Amount", "Spill Balance", "Edge Blur",
        "Invert Matte", "Output View");
    return ve;
}

void *complexthreshold_malloc(int w, int h) {
    promixer_t *m = (promixer_t*) vj_malloc(sizeof(promixer_t));
    m->n_threads = vje_advise_num_threads(w * h);
    m->alpha_map = (uint8_t*) vj_malloc(w * h);
    m->alpha_temp = (uint8_t*) vj_malloc(w * h);
    return (void*) m;
}

void complexthreshold_free(void *ptr) {
    promixer_t *m = (promixer_t*)ptr;
    if(m) {
        if(m->alpha_map) free(m->alpha_map);
        if(m->alpha_temp) free(m->alpha_temp);
        free(m);
    }
}

void complexthreshold_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    promixer_t *mk = (promixer_t*) ptr;

    float angle_rad = ((float)args[0] / 10.0f) * (M_PI / 180.0f);
    const float target_u = cosf(angle_rad) * 127.0f;
    const float target_v = sinf(angle_rad) * 127.0f;
    const float mag_target = sqrtf(target_u * target_u + target_v * target_v);

    const int SCALE = 4096;
    const int cos_q_fp = (int)((target_u / mag_target) * SCALE);
    const int sin_q_fp = (int)((target_v / mag_target) * SCALE);

    float g_val = fmaxf((float)args[4] / 128.0f, 0.1f);
    for(int i=0; i<256; i++)
        mk->gamma_lut[i] = (uint8_t)(powf((float)i / 255.0f, 1.0f/g_val) * 255.0f);

    const int c_thresh = fmax(args[1], 1);
    const float m_min = (float)args[2];
    const int l_thresh = args[6];
    const int spill_amt = args[7];
    const int s_mode_val = args[8];
    const int soft = args[9];

    const int w = frame->width;
    const int h = frame->height;
    const int len = w * h;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];
    const int sat_gate_sq = args[5] * args[5];
    const int m_range_inv_fp = (255 << 12) / (fmax(args[3] - m_min, 1));
    const int inv_c_thresh_fp = (1 << 24) / (c_thresh * SCALE);

    int spill_final_fp = 0;
    if (s_mode_val >= 128) {
        float spill_softness = 1.0f - ((float)(s_mode_val - 128) / 160.0f);
        spill_final_fp = (int)(((float)spill_amt / 255.0f) * spill_softness * 4096.0f);
    }

    #pragma omp parallel num_threads(mk->n_threads)
    {
        #pragma omp for schedule(static)
        for (int i = 0; i < len; i++) {
            int uc = (int)Cb2[i] - 128;
            int vc = (int)Cr2[i] - 128;

            if ((uc*uc + vc*vc) < sat_gate_sq) {
                mk->alpha_map[i] = 0;
                continue;
            }

            int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
            int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
            int dist_fp = (int)(mag_target * SCALE) - (xx << 12) + (abs(yy) * 16);

            int a = 0;
            if (dist_fp < (c_thresh * SCALE)) {
                a = 255 - ((dist_fp * inv_c_thresh_fp) >> 16);
            }

            if (Y2[i] < l_thresh) {
                int l_a = (l_thresh - Y2[i]) * 4;
                if (l_a > a) a = l_a;
            }
            mk->alpha_map[i] = (uint8_t)CLAMP(a, 0, 255);
        }

        if (soft > 0) {
            #pragma omp for schedule(static)
            for (int y = 0; y < h; y++) {
                uint8_t *in = &mk->alpha_map[y * w];
                uint8_t *out = &mk->alpha_temp[y * w];
                for (int x = 1; x < w - 1; x++) {
                    out[x] = DIV3(in[x-1] + in[x] + in[x+1]);
                }
            }
            #pragma omp for schedule(static)
            for (int y = 1; y < h - 1; y++) {
                uint8_t *m = &mk->alpha_temp[y * w];
                uint8_t *t = &mk->alpha_temp[(y-1) * w];
                uint8_t *b = &mk->alpha_temp[(y+1) * w];
                uint8_t *dest = &mk->alpha_map[y * w];
                for (int x = 0; x < w; x++) {
                    dest[x] = DIV3(t[x] + m[x] + b[x]);
                }
            }
        }

        #pragma omp for schedule(static)
        for (int i = 0; i < len; i++) {
            int raw_a = mk->alpha_map[i];
            int alpha_f_fp = (raw_a - m_min) * m_range_inv_fp;

            uint8_t snapped_a = (uint8_t)CLAMP(alpha_f_fp >> 12, 0, 255);
            int a = mk->gamma_lut[snapped_a];

            if (args[10]) a = 255 - a;

            if (args[11] == 1) {
                Y[i] = a; Cb[i] = 128; Cr[i] = 128;
                continue;
            }

            int uc = (int)Cb2[i] - 128;
            int vc = (int)Cr2[i] - 128;
            int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;

            int sY = Y2[i], sCb = Cb2[i], sCr = Cr2[i];
            
            if (xx > 2) {
                if (s_mode_val >= 128) {
                    sCb = CLAMP(Cb2[i] - ((uc * spill_final_fp) >> 12), 0, 255);
                    sCr = CLAMP(Cr2[i] - ((vc * spill_final_fp) >> 12), 0, 255);
                    sY  = CLAMP(Y2[i] + ((s_mode_val - 128) >> 3), 0, 255);
                }
                else {
                    int spill_f = (xx * spill_amt) >> 8;
                    sY  = CLAMP(Y2[i] + ((spill_f * s_mode_val) >> 6), 0, 255);
                    sCb = CLAMP(Cb2[i] - ((spill_f * cos_q_fp) >> 12), 0, 255);
                    sCr = CLAMP(Cr2[i] - ((spill_f * sin_q_fp) >> 12), 0, 255);
                }
            }

            if (args[11] == 2) {
                Y[i] = sY; Cb[i] = sCb; Cr[i] = sCr;
                continue;
            }

            if (a >= 254) {
                continue;
            }
            
            if (a <= 1) {
                Y[i] = sY; Cb[i] = sCb; Cr[i] = sCr;
                continue;
            }

            int ia = 255 - a;
            Y[i]  = (uint8_t)DIV255(Y[i]  * a + sY  * ia);
            Cb[i] = (uint8_t)DIV255(Cb[i] * a + sCb * ia);
            Cr[i] = (uint8_t)DIV255(Cr[i] * a + sCr * ia);
        }
    }
}
