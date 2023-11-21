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

#define NB_SIZE 128

vj_effect *aquatex_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */

	ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 100;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 360;
	ve->limits[0][3] = 1;
	ve->limits[1][3] = NB_SIZE;
	ve->limits[0][4] = 0;
	ve->limits[1][4] = 100;

	ve->defaults[0] = 1;
	ve->defaults[1] = 1;
	ve->defaults[2] = 1;
	ve->defaults[3] = 1;
	ve->defaults[4] = 1;

    ve->description = "Turbulent Aquatex";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Intensity", "Frequency", "Phase Shift", "Neighbourhood Size", "Turbulence" );
    return ve;
}

#define LUT_SIZE 360
#define RAND_LUT_SIZE 1000

typedef struct 
{
    uint8_t *buf[3];
    double *lut;
    double *sin_lut;
	double *rand_lut;
	double *offset_y_lut;
	double *offset_x_lut;
} aquatex_t;

static void init_sin_lut(aquatex_t *f)
{
    const int size = LUT_SIZE;
    for(int i = 0; i < size; ++i) {
        f->sin_lut[i] = sin( 2 * M_PI * i / LUT_SIZE );
    }
}

void *aquatex_malloc(int w, int h) {
    aquatex_t *s = (aquatex_t*) vj_calloc(sizeof(aquatex_t));
    if(!s) return NULL;
    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + ( w * h );
    s->buf[2] = s->buf[1] + ( w * h );

	const int nb = NB_SIZE * 2 + 1;
    s->lut = (double*) vj_malloc(sizeof(double) * (( LUT_SIZE + RAND_LUT_SIZE) + (3 * nb) ));
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->sin_lut = s->lut;
	s->rand_lut = s->sin_lut + LUT_SIZE;
	s->offset_y_lut = s->rand_lut + RAND_LUT_SIZE;
	s->offset_x_lut = s->offset_y_lut + nb;

	init_sin_lut(s);

    return (void*) s;
}

void aquatex_free(void *ptr) {
    aquatex_t *s = (aquatex_t*) ptr;
    free(s->buf[0]);
    free(s->lut);
    free(s);
}

void aquatex_apply(void *ptr, VJFrame *frame, int *args) {
    aquatex_t *s = (aquatex_t*)ptr;

    const double intensity = (double)args[0] * 0.01;
    const double frequency = (double)args[1] * 0.1;
    const double phase_shift = (double)args[2] * LUT_SIZE / 360.0;
    const int neighborhood_size = args[3];
    const double turbulence = (double)args[4] * 0.01;

    const int len = frame->width * frame->height;
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    double *restrict sin_lut = s->sin_lut;
	double *restrict rand_lut = s->rand_lut;
	double *restrict offset_y_lut = s->offset_y_lut;
	double *restrict offset_x_lut = s->offset_x_lut;

	const int nb = NB_SIZE * 2 + 1;

    veejay_memcpy(bufY, srcY, len);
    veejay_memcpy(bufU, srcU, len);
    veejay_memcpy(bufV, srcV, len);

    if( turbulence > 0 ) {
		for (int i = 0; i < RAND_LUT_SIZE; ++i) {
        	rand_lut[i] = (rand() / (double)RAND_MAX - 0.5) * turbulence;
    	}
	}

	for (int i = 0; i < nb; i++) {
    	const int index = (int)(frequency * (i - neighborhood_size) / neighborhood_size * LUT_SIZE + phase_shift) % LUT_SIZE;
    	offset_y_lut[i] = intensity * sin_lut[(index + LUT_SIZE) % LUT_SIZE];
    	offset_x_lut[i] = intensity * sin_lut[(index + LUT_SIZE) % LUT_SIZE];
	}

	if( turbulence > 0 ) {
		for (int y_pos = 0; y_pos < height; y_pos++) {
				for (int x_pos = 0; x_pos < width; x_pos++) {
					const int pixel_index = y_pos * width + x_pos;
					const float offset_y = offset_y_lut[y_pos % (2 * neighborhood_size + 1)] + rand_lut[ pixel_index % RAND_LUT_SIZE];
					const float offset_x = offset_x_lut[x_pos % (2 * neighborhood_size + 1)] + rand_lut[ pixel_index % RAND_LUT_SIZE];
					
					int new_y = (int)(y_pos + offset_y * height);
					int new_x = (int)(x_pos + offset_x * width);
					
					new_y = (new_y < 0) ? 0 : ((new_y >= height) ? height - 1 : new_y);
					new_x = (new_x < 0) ? 0 : ((new_x >= width) ? width - 1 : new_x);
					
					const int src_idx = new_y * width + new_x;
					
					srcY[pixel_index] = bufY[src_idx];
					srcU[pixel_index] = bufU[src_idx];
					srcV[pixel_index] = bufV[src_idx];
				}
		}
	} 
	else {
		for (int y_pos = 0; y_pos < height; y_pos++) {
			for (int x_pos = 0; x_pos < width; x_pos++) {
					const int pixel_index = y_pos * width + x_pos;
					const float offset_y = offset_y_lut[y_pos % (2 * neighborhood_size + 1)];
					const float offset_x = offset_x_lut[x_pos % (2 * neighborhood_size + 1)];

					int new_y = (int)(y_pos + offset_y * height);
					int new_x = (int)(x_pos + offset_x * width);

					new_y = (new_y < 0) ? 0 : ((new_y >= height) ? height - 1 : new_y);
					new_x = (new_x < 0) ? 0 : ((new_x >= width) ? width - 1 : new_x);

					const int src_idx = new_y * width + new_x;

					srcY[pixel_index] = bufY[src_idx];
					srcU[pixel_index] = bufU[src_idx];
					srcV[pixel_index] = bufV[src_idx];
        	}
    	}
	}
}

