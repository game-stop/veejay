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
    ve->parallel = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Frequency X", "Frequency Y", "Amplitude", "Speed", "Angle X", "Angle Y", "Break" );

    return ve;
}

typedef struct {
    float cos_lut[384] __attribute__((aligned(64)));
    float sin_lut[384] __attribute__((aligned(64)));
    int width;
    int height;
    int speed;
    int update;
} luminouswave_t;

#define SIN_TABLE_SIZE 360
void* luminouswave_malloc(int w, int h) {
    luminouswave_t *data = (luminouswave_t*) vj_malloc(sizeof(luminouswave_t));
    if (!data)
        return NULL;

    data->width = w;
    data->height = h;
    data->speed = 0;

    for(int i = 0; i < 360; i ++ ) {
        float val = i * (M_PI/180.0f);
        data->sin_lut[i] = a_sin( val );
        data->cos_lut[i] = a_cos( val );
    }

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

    int x = 0, y;
    
    const float frequencyX = args[0] * 0.01f;
    const float frequencyY = args[1] * 0.01f;
    const float amplitude  = args[2];
    const int min_speed      = args[3];
    const int waveAngleX = args[4];
    const int waveAngleY = args[5];
    const int break_speed = args[6];

    const float *sin_lut = data->sin_lut;
    const float *cos_lut = data->cos_lut;

    const int max_speed = (args[0] * width > args[1] * height) ? (args[0] * width) : (args[1] * height);

    uint8_t *Y = frame->data[0];

    const int cur_speed = data->speed;
    int new_speed = cur_speed + (max_speed / (break_speed * 10));
    if (new_speed > max_speed) {
        new_speed = min_speed;
    }

    const float f_speed = (min_speed + cur_speed) * 0.01f;

    int current_count = atomic_fetch_add_explicit(&data->update, 1, memory_order_relaxed);

    if (frame->totaljobs == 0 || current_count % frame->totaljobs == 0) {
        data->speed = new_speed;
    }

    const int offset = (frame->jobnum * height);

    for (y = 0; y < height; y++) {
        const float cos_y = cos_lut[ waveAngleY ];
        const float sin_y = sin_lut[ waveAngleY ];
        const int offset_y = y + offset;

        for (x = 0; x < width; x++) {
            float offsetY = amplitude * a_sin(frequencyY * (x * sin_lut[waveAngleX] + offset_y * cos_y) + f_speed);
            float offsetX = amplitude * a_sin(frequencyX * (x * cos_lut[waveAngleX] + offset_y * sin_y) + f_speed);
            int luma = Y[y * width + x] + offsetX + offsetY;
            Y[y*width+x] = (luma < pixel_Y_lo_) ? pixel_Y_lo_ : (luma > pixel_Y_hi_) ? pixel_Y_hi_ : luma;
        }
    }
}


