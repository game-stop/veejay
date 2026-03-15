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
#include "luminouswave.h"
#include <stdatomic.h>

vj_effect *luminouswave_init(int w, int h) {
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;

    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;     
    ve->limits[1][0] = 100;
    ve->defaults[0] = 4; 
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 100;
    ve->defaults[1] = 5;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 45; 
    ve->defaults[2] = 30;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 100;
    ve->defaults[3] = 10;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 360;
    ve->defaults[4] = 33;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 360;
    ve->defaults[5] = 10;

    ve->limits[0][6] = 1;
    ve->limits[1][6] = 500;
    ve->defaults[6] = 100;

    ve->description = "Luminous Wave";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Frequency X", "Frequency Y", "Amplitude", "Speed", "Angle X", "Angle Y", "Break" );

    return ve;
}

typedef struct {
    float sin_lut[1024] __attribute__((aligned(64)));
    float cos_lut[1024] __attribute__((aligned(64)));
    int width;
    int height;
    int speed;
    int update;
    int n_threads;
} luminouswave_t;

#define SIN_TABLE_SIZE 360
void* luminouswave_malloc(int w, int h) {
    luminouswave_t *data = (luminouswave_t*) vj_malloc(sizeof(luminouswave_t));
    if (!data) return NULL;

    data->width = w;
    data->height = h;
    data->speed = 0;

    for(int i = 0; i < 1024; i++) {
        float val = (i / 1024.0f) * (2.0f * M_PI);
        data->sin_lut[i] = a_sin(val);
        data->cos_lut[i] = a_cos(val);
    }

    data->n_threads = vje_advise_num_threads(w*h);

    return data;
}

void luminouswave_free(void *ptr) {
    luminouswave_t *data = (luminouswave_t*) ptr;
    if (data != NULL) {
        free(data);
    }
}
void luminouswave_apply(void *ptr, VJFrame *frame, int *args) {
    luminouswave_t *data = (luminouswave_t*)ptr;
    const int width = frame->width;
    const int height = frame->height;
    uint8_t *Y = frame->data[0];

    const float freqX = args[0] * 0.01f;
    const float freqY = args[1] * 0.01f;
    const float amplitude = (float)args[2];
    const int min_speed = args[3];
    const int break_speed = args[6];

    const float sX = data->sin_lut[(args[4] * 1024 / 360) & 1023];
    const float cX = data->cos_lut[(args[4] * 1024 / 360) & 1023];
    const float sY = data->sin_lut[(args[5] * 1024 / 360) & 1023];
    const float cY = data->cos_lut[(args[5] * 1024 / 360) & 1023];

    const int max_speed = (args[0] * width > args[1] * height) ? (args[0] * width) : (args[1] * height);
    int next_speed = data->speed + (max_speed / (break_speed * 10));
    if (next_speed > max_speed) next_speed = min_speed;
    data->speed = next_speed;

    const float f_speed = (min_speed + next_speed) * 0.01f;
    const float rad_to_idx = 1024.0f / (2.0f * M_PI);

    const float stepY = freqY * sX;
    const float stepX = freqX * cX;
    const int offset = (frame->jobnum * height);


    #pragma omp parallel for num_threads(data->n_threads) schedule(static)

    for (int y = 0; y < height; y++) {
        uint8_t *restrict row = &Y[y * width];
        const int actual_y = y + offset;
        
        const float base_Y = (freqY * actual_y * cY + f_speed) * rad_to_idx;
        const float base_X = (freqX * actual_y * sY + f_speed) * rad_to_idx;
        const float sY_inc = stepY * rad_to_idx;
        const float sX_inc = stepX * rad_to_idx;

        for (int x = 0; x < width; x++) {
            int idxY = (int)(x * sY_inc + base_Y) & 1023;
            int idxX = (int)(x * sX_inc + base_X) & 1023;
            
            float off = amplitude * (data->sin_lut[idxY] + data->sin_lut[idxX]);
            int luma = row[x] + (int)off;
            
            if (luma < pixel_Y_lo_) luma = pixel_Y_lo_;
            else if (luma > pixel_Y_hi_) luma = pixel_Y_hi_;
            
            row[x] = (uint8_t)luma;
        }
    }
}