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
#include <stdlib.h>

typedef struct {
    uint8_t *temp_Y;
    uint8_t *blur_Y;
    int n_threads;
} charcoal_t;

vj_effect *charcoalsketch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0][0] = 1; ve->limits[1][0] = 50;
    ve->defaults[0] = 7; 
    ve->limits[0][1] = 0; ve->limits[1][1] = 255;
    ve->defaults[1] = 180;
    ve->limits[0][2] = 0; ve->limits[1][2] = 64;
    ve->defaults[2] = 10;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Stroke Thickness", "Intensity", "Grain Level"
    );

    ve->description = "Charcoal Sketch";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 0;

    return ve;
}

void *charcoalsketch_malloc(int w, int h) {
    charcoal_t *c = (charcoal_t*) vj_calloc(sizeof(charcoal_t));
    if(!c) return NULL;
    c->temp_Y = (uint8_t*) vj_malloc( w * h * sizeof(uint8_t));
    if(!c->temp_Y) {
        free(c);
        return NULL;
    }

    c->blur_Y = (uint8_t*) vj_malloc( w * h * sizeof(uint8_t));
    if(!c->blur_Y) {
        free(c->temp_Y);
        free(c);
        return NULL;
    }
    c->n_threads = vje_advise_num_threads(w*h);
    return (void*) c;
}

void charcoalsketch_free(void *ptr) {
    charcoal_t *c = (charcoal_t*) ptr;
    if(c) {
        if(c->temp_Y) free(c->temp_Y);
        if(c->blur_Y) free(c->blur_Y);
        free(c);
    }
}

void charcoalsketch_apply(void *ptr, VJFrame *frame, int *args)
{
    charcoal_t *c = (charcoal_t*) ptr;
    int radius      = args[0];
    int intensity   = args[1];
    int grain       = args[2];

    int width  = frame->width;
    int height = frame->height;
    int len    = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict temp_Y = c->temp_Y;
    uint8_t *restrict blur_Y = c->blur_Y;

    if (radius < 1) radius = 1;

    #pragma omp parallel for num_threads(c->n_threads)
    for (int y = 0; y < height; y++) {
        int sum = 0;
        int row_offset = y * width;
        for (int x = -radius; x <= radius; x++) {
            int nx = x < 0 ? 0 : (x >= width ? width - 1 : x);
            sum += Y[row_offset + nx];
        }
        for (int x = 0; x < width; x++) {
            temp_Y[row_offset + x] = sum / (2 * radius + 1);
            int t_x = x - radius;
            int h_x = x + radius + 1;
            sum += Y[row_offset + (h_x >= width ? width - 1 : h_x)] - 
                   Y[row_offset + (t_x < 0 ? 0 : t_x)];
        }
    }

    #pragma omp parallel for num_threads(c->n_threads)
    for (int x = 0; x < width; x++) {
        int sum = 0;
        for (int y = -radius; y <= radius; y++) {
            int ny = y < 0 ? 0 : (y >= height ? height - 1 : y);
            sum += temp_Y[ny * width + x];
        }
        for (int y = 0; y < height; y++) {
            blur_Y[y * width + x] = sum / (2 * radius + 1);
            int t_y = y - radius;
            int h_y = y + radius + 1;
            sum += temp_Y[(h_y >= height ? height - 1 : h_y) * width + x] - 
                   temp_Y[(t_y < 0 ? 0 : t_y) * width + x];
        }
    }

    #pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for (int y = 0; y < height; y++) {
        unsigned int seed = y * 0x9E3779B9 + grain;
        for (int x = 0; x < width; x++) {
            int i = y * width + x;
            uint32_t orig = Y[i];
            uint32_t blur = blur_Y[i];

            uint32_t sketch = (orig << 8) / (blur + 1);
            
            sketch = (sketch * intensity) >> 8;

            if (grain > 0) {
                seed = (seed * 1103515245 + 12345) & 0x7fffffff;
                int noise = (int)(seed % (grain + 1)) - (grain >> 1);
                sketch = (int)sketch + noise;
            }

            if (sketch > 255) sketch = 255;
            else if (sketch < 0) sketch = 0;

            Y[i] = (uint8_t)sketch;
        }
    }

    int uv_len = frame->ssm ? len : frame->uv_len;
    if (frame->data[1] && frame->data[2]) {
        veejay_memset(frame->data[1], 128, uv_len);
        veejay_memset(frame->data[2], 128, uv_len);
    }
}