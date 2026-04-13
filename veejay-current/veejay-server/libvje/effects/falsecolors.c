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
#include "falsecolors.h"
#include <math.h>
#include <omp.h>

vj_effect *falsecolors_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 128;   // motion sensitivity
    ve->defaults[1] = 1;     // cycle speed (for dynamic LUT rotation)
    ve->defaults[2] = 200;   // opacity
    ve->defaults[3] = 64;     // gamma
    ve->defaults[4] = 16;    // trail decay (frames)
    ve->defaults[5] = 256;   // motion gain

    ve->limits[0][0] = 0;    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;    ve->limits[1][1] = 64;
    ve->limits[0][2] = 0;    ve->limits[1][2] = 255;
    ve->limits[0][3] = 1;    ve->limits[1][3] = 255;
    ve->limits[0][4] = 1;    ve->limits[1][4] = 128;
    ve->limits[0][5] = 0;    ve->limits[1][5] = 1024;
    
    ve->description = "False Color Map";

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list( ve->num_params,
        "Motion Sensitivity", "Cycle Speed", "Opacity", "Gamma",
        "Trail Decay", "Motion Gain" );

    return ve;
}

typedef struct {
    uint8_t *buf[6];
    uint8_t *blur;
    uint8_t rainbow[256][3];
    uint8_t sigmoid_lut[256];
    uint8_t gamma_lut[256];
    int timestamp;
    int n_threads;
    float phase;
    float gamma;
} thermal_t;

static void build_gamma_lut(uint8_t lut[256], float gamma)
{
    for(int i=0;i<256;i++){
        float x = i / 255.0f;
        lut[i] = (uint8_t)(powf(x, gamma) * 255.0f);
    }
}

static void thermal_build_palette(uint8_t lut[256][3], float gamma)
{
    // 7 color points
    const float t_points[7] = {0.0f,0.14f,0.28f,0.42f,0.57f,0.71f,1.0f};
    const float r_points[7] = {1.0f,1.0f,1.0f,0.0f,0.0f,0.0f,1.0f};
    const float g_points[7] = {0.0f,0.3f,0.7f,1.0f,1.0f,0.0f,0.0f};
    const float b_points[7] = {0.0f,0.0f,0.0f,0.0f,0.5f,1.0f,1.0f};

    for(int i=0;i<256;i++){
        float t = i/255.0f;
        int seg = 0;
        while(seg<6 && t > t_points[seg+1]) seg++;

        float f = (t - t_points[seg])/(t_points[seg+1]-t_points[seg]);
        float r = r_points[seg] + f*(r_points[seg+1]-r_points[seg]);
        float g = g_points[seg] + f*(g_points[seg+1]-g_points[seg]);
        float b = b_points[seg] + f*(b_points[seg+1]-b_points[seg]);

        // RGB -> YUV (BT.601)
        float Yf = 0.299f*r + 0.587f*g + 0.114f*b;
        float Uf = -0.169f*r -0.331f*g + 0.5f*b + 0.5f;
        float Vf = 0.5f*r -0.419f*g -0.081f*b + 0.5f;

        lut[i][0] = (uint8_t)(fminf(fmaxf(Yf,0.0f),1.0f)*255.0f);
        lut[i][1] = (uint8_t)(fminf(fmaxf(Uf,0.0f),1.0f)*255.0f);
        lut[i][2] = (uint8_t)(fminf(fmaxf(Vf,0.0f),1.0f)*255.0f);
    }
}

void *falsecolors_malloc(int w, int h) {
    thermal_t *s = (thermal_t*) vj_calloc(sizeof(thermal_t));
    if(!s) return NULL;

    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t)*w*h*3); 
    if(!s->buf[0]){ free(s); return NULL; }
    s->buf[1] = s->buf[0] + (w*h);
    s->buf[2] = s->buf[1] + (w*h);

    veejay_memset(s->buf[0],0,w*h);
    veejay_memset(s->buf[1],0,w*h);
    veejay_memset(s->buf[2],0,w*h);

    s->n_threads = vje_advise_num_threads(w*h);
    thermal_build_palette(s->rainbow, 0.8f);

    int max_dim = (w > h) ? w : h;
    s->blur = (uint8_t*) vj_malloc(sizeof(uint8_t) * s->n_threads * max_dim * 2);

    s->gamma = -1.0f;
    
    return s;
}

void falsecolors_free(void *ptr){
    thermal_t *s = (thermal_t*) ptr;
    if(s){
        if(s->buf[0]) free(s->buf[0]);
        if(s->blur) free(s->blur);
        free(s);
    }
}

static inline int clamp_u8(int x) {
    if ((unsigned)x > 255)
        x = (~x >> 31) & 255;
    return x;
}

void falsecolors_apply(void *ptr, VJFrame *frame, int *args){
    thermal_t *s = (thermal_t*) ptr;
    const int w   = frame->width;
    const int h   = frame->height;
    const int len = frame->len;

    const int opacity     = args[2];
    const int inv_opacity = 256 - opacity;
    const int motion_gain = args[5];
    const int cycle_speed = args[1];
    const int sensitivity = args[0];
    const float gamma = (args[3] / 64.0f) + 0.1f;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict b0 = s->buf[0];
    uint8_t *restrict b1 = s->buf[1];
    uint8_t *restrict b2 = s->buf[2];

    int lut_offset = (s->timestamp * cycle_speed) & 0xFF;
    const int max_dim = (w > h ? w : h);

    if (fabsf(s->gamma - gamma) > 0.01f) {
        build_gamma_lut(s->gamma_lut, gamma);
        s->gamma = gamma;
    }

    int global_min = 255, global_max = 0;
    const int scale_fp = 0;

    #pragma omp parallel num_threads(s->n_threads) reduction(min:global_min) reduction(max:global_max)
    {
        int tid = omp_get_thread_num();
        uint8_t *tmp = s->blur + tid * max_dim * 2;
        uint8_t *col_tmp = tmp;
        uint8_t *col_out = tmp + max_dim;

        #pragma omp for schedule(static)
        for(int y = 0; y < h; y++){
            blur2(tmp, Y + (y * w), w, 2, 2, 1, 1);
            memcpy(b2 + (y * w), tmp, w);
        }

        #pragma omp for schedule(static)
        for(int x = 0; x < w; x++){
            for(int y = 0; y < h; y++)
                col_tmp[y] = b2[y*w + x];

            blur2(col_out, col_tmp, h, 2, 2, 1, 1);

            for(int y = 0; y < h; y++)
                b2[y*w + x] = col_out[y];
        }

        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++){
            int v = b2[i];
            if(v < global_min) global_min = v;
            if(v > global_max) global_max = v;
        }
    }

    int range = global_max - global_min;
    if (range < 64) range = 64;
    const int scale_fp_final = (255 << 16) / range;

    #pragma omp parallel num_threads(s->n_threads)
    {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++){
            const int lum = b2[i];
            int motion = abs(lum - b0[i]) - sensitivity;
            motion &= ~(motion >> 31);

            int val = (((lum - global_min) * scale_fp_final) >> 16) + ((motion * motion_gain) >> 7);
            if (val > 255) val = 255;

            val = (val * opacity + b1[i] * inv_opacity) >> 8;

            b1[i] = (uint8_t)val;
            b0[i] = (uint8_t)lum;

            const int mapped = s->gamma_lut[val];
            int lut_idx = (mapped + lut_offset) & 0xFF;

            if(motion > 0){
                int jump = (motion > 48) ? 64 : (motion << 6) / 48;
                lut_idx = (lut_idx + jump) & 0xFF;
            }

            const uint8_t *col = s->rainbow[lut_idx];
            Y[i] = col[0];
            U[i] = col[1];
            V[i] = col[2];
        }
    }

    s->timestamp++;
}
