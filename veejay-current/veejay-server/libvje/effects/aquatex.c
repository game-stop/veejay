/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "aquatex.h"
#include <math.h>
#include <omp.h>

#define NB_SIZE 128
#define LUT_SIZE 1024
#define RAND_LUT_SIZE 2048

vj_effect *aquatex_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;   ve->limits[1][0] = 100;
    ve->limits[0][1] = 1;   ve->limits[1][1] = 100;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 360;
    ve->limits[0][3] = 1;   ve->limits[1][3] = NB_SIZE;
    ve->limits[0][4] = 0;   ve->limits[1][4] = 100;

    ve->defaults[0] = 30;
    ve->defaults[1] = 10;
    ve->defaults[2] = 0;
    ve->defaults[3] = 20;
    ve->defaults[4] = 5;

    ve->sub_format = 1;

    ve->description = "Improved Aquatex";
    ve->param_description = vje_build_param_list( ve->num_params, "Intensity", "Wave Scale", "Phase", "Spread", "Noise" );
    return ve;
}

typedef struct  
{
    uint8_t *temp_buf;
    float *sin_lut;
    float *noise_lut;
    int n_threads;
} aquatex_t;

void *aquatex_malloc(int w, int h) {
    aquatex_t *s = (aquatex_t*) vj_calloc(sizeof(aquatex_t));
    if(!s) return NULL;
    
    s->temp_buf = (uint8_t*) vj_malloc(w * h * 4); 
    s->sin_lut = (float*) vj_malloc(sizeof(float) * LUT_SIZE);
    s->noise_lut = (float*) vj_malloc(sizeof(float) * RAND_LUT_SIZE);

    for(int i = 0; i < LUT_SIZE; ++i) {
        s->sin_lut[i] = sinf( 2.0f * M_PI * i / LUT_SIZE );
    }
    for(int i = 0; i < RAND_LUT_SIZE; ++i) {
        s->noise_lut[i] = ((float)rand() / (float)RAND_MAX) - 0.5f;
    }

    s->n_threads = vje_advise_num_threads(w*h);

    return (void*) s;
}

void aquatex_free(void *ptr) {
    aquatex_t *s = (aquatex_t*) ptr;
    if (!s) return;
    free(s->temp_buf);
    free(s->sin_lut);
    free(s->noise_lut);
    free(s);
}

void aquatex_apply(void *ptr, VJFrame *frame, int *args) {
    aquatex_t *s = (aquatex_t*)ptr;
    if (!s || !frame || !frame->data[0]) return;

    const float intensity   = (float)args[0]; 
    const float frequency   = (float)args[1] * 0.05f;
    const float phase_shift = (float)args[2] * (LUT_SIZE / 360.0f);
    const float noise_amp   = (float)args[4] * 0.1f;

    const int w = frame->width;
    const int h = frame->height;
    const int plane_size = w * h;

    veejay_memcpy(s->temp_buf,                  frame->data[0], plane_size);
    veejay_memcpy(s->temp_buf + plane_size,     frame->data[1], plane_size);
    veejay_memcpy(s->temp_buf + (plane_size*2), frame->data[2], plane_size);

    uint8_t *restrict srcY = s->temp_buf;
    uint8_t *restrict srcU = s->temp_buf + plane_size;
    uint8_t *restrict srcV = s->temp_buf + (plane_size * 2);

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    #pragma omp parallel for num_threads(s->n_threads) schedule(static) \
        firstprivate(w, h, frequency, phase_shift, intensity, noise_amp)
    for (int y = 0; y < h; y++) {
        int y_wave_idx = (int)(y * frequency + phase_shift) & (LUT_SIZE - 1);
        float v_offset = s->sin_lut[y_wave_idx] * intensity;
        
        int dst_row_offset = y * w;

        for (int x = 0; x < w; x++) {
            int x_wave_idx = (int)(x * frequency * 0.9f) & (LUT_SIZE - 1);
            float h_offset = s->sin_lut[x_wave_idx] * intensity;

            float noise = s->noise_lut[(x + y) % RAND_LUT_SIZE] * noise_amp;

            int sx = x + (int)(v_offset + noise + 0.5f);
            int sy = y + (int)(h_offset + noise + 0.5f);

            sx = (sx < 0) ? 0 : (sx >= w ? w - 1 : sx);
            sy = (sy < 0) ? 0 : (sy >= h ? h - 1 : sy);

            int src_idx = sy * w + sx;
            int dst_idx = dst_row_offset + x;

            dstY[dst_idx] = srcY[src_idx];
            dstU[dst_idx] = srcU[src_idx];
            dstV[dst_idx] = srcV[src_idx];
        }
    }
}
