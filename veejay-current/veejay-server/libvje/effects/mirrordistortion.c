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
#include <libvje/internal.h>
#include "mirrordistortion.h"

vj_effect *mirrordistortion_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 3;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 10;
	ve->defaults[1] = w;
	ve->defaults[2] = h;
	ve->description = "Mirror Distortion";
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 100;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = w * 2;
        ve->limits[0][2] = 0;
        ve->limits[1][2] = h * 2;	
	ve->extra_frame = 0;
	ve->sub_format = 1;
	ve->has_user = 0;
	ve->parallel = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Distortion", "Offset X", "Offset Y" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	return ve;
}

#define TABLE_SIZE 360
#define TABLE_RESOLUTION 10000

typedef struct {
	float *sin_lut;
 	float *cos_lut;
	float distortion;
	uint8_t *buf[3];
	int strides[4];
} mirror_distortion_t;

void *mirrordistortion_malloc(int w, int h)
{
	mirror_distortion_t *m = (mirror_distortion_t*) vj_malloc(sizeof(mirror_distortion_t));
	if(!m) {
		return NULL;
	}
	m->distortion = -1.0f;
	m->cos_lut = (float*) vj_malloc( sizeof(float) * w );
	if(!m->cos_lut) {
		free(m);
		return NULL;
	}
	m->sin_lut = (float*) vj_malloc( sizeof(float) * h );
	if(!m->sin_lut) {
		free(m->cos_lut);
		free(m);
		return NULL;
	}
	m->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 );
	if(!m->buf[0]) {
		free(m->cos_lut);
		free(m->sin_lut);
		free(m);
		return NULL;
	}
	m->buf[1] = m->buf[0] + (w*h);
	m->buf[2] = m->buf[1] + (w*h);

	m->strides[0] = (w*h);
	m->strides[1] = m->strides[0];
	m->strides[2] = m->strides[1];
	m->strides[3] = 0;

	return (void*) m;
}

void mirrordistortion_free(void *ptr) {
	mirror_distortion_t *m = (mirror_distortion_t*) ptr;
	if(m) {
		if(m->sin_lut)
			free(m->sin_lut);
		if(m->cos_lut)
			free(m->cos_lut);
		free(m);
	}
}


void mirrordistortion_apply(void *ptr, VJFrame *frame, int *args ) {

	mirror_distortion_t *m = (mirror_distortion_t*) ptr;

    float distortionFactor = args[0] * 0.01f;
    int offsetX = args[1] - frame->width;
    int offsetY = args[2] - frame->height;

	const int w = frame->width;
	const int h = frame->height;

	int i;

	uint8_t *srcY = m->buf[0];
	uint8_t *srcU = m->buf[1];
	uint8_t *srcV = m->buf[2];

	if( distortionFactor != m->distortion ) {
		for( i = 0; i < w; i ++ ) {
			m->cos_lut[i] = a_cos( i * distortionFactor );
		}	
		for( i = 0; i < h; i ++ ) {
			m->sin_lut[i] = a_sin( i * distortionFactor );
		}
		m->distortion = distortionFactor;
	}

	veejay_memcpy( m->buf[0], frame->data[0], frame->len );
	veejay_memcpy( m->buf[1], frame->data[1], frame->len );
	veejay_memcpy( m->buf[2], frame->data[2], frame->len );

    for (int i = 0; i < frame->height; ++i) {
        for (int j = 0; j < frame->width; ++j) {
			int sourceX = j + offsetX * m->sin_lut[i];
            int sourceY = i + offsetY * m->cos_lut[j];

            sourceX = fmin(fmax(sourceX, 0), frame->width - 1);
            sourceY = fmin(fmax(sourceY, 0), frame->height - 1);

            frame->data[0][i * frame->width + j] = frame->data[0][sourceY * frame->width + sourceX];
            frame->data[1][i * frame->width + j] = frame->data[1][sourceY * frame->width + sourceX];
            frame->data[2][i * frame->width + j] = frame->data[2][sourceY * frame->width + sourceX];
        }
    }
}

