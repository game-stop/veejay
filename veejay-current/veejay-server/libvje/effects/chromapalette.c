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
#include "chromapalette.h"
#include "common.h"
#include <veejaycore/vjmem.h>
#include "chromapalette.h"

typedef struct {
	int n_threads;
	int softness;
	int tolerance;
	float *lut;
} chromapalette_t;

vj_effect *chromapalette_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if (!ve) return NULL;

    ve->num_params = 7;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 60;

    ve->limits[0][6] = 0;
    ve->limits[1][6] = 255;
    ve->defaults[6] = 20;

    for(int i=1; i<6; i++) {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 255;
    }
    ve->defaults[1] = 255; // Red
    ve->defaults[4] = 200; // Cb
    ve->defaults[5] = 20;  // Cr

    ve->description = "Chrominance Palette (rgb key)";
    ve->sub_format = 1;
    ve->rgb_conv = 1;

    ve->param_description = vje_build_param_list(ve->num_params, "Tolerance", "Red", "Green", "Blue", "Chroma Blue", "Chroma Red", "Softness");
    return ve;
}

void* chromapalette_malloc(int w, int h) {
	chromapalette_t *c = (chromapalette_t*) vj_malloc(sizeof(chromapalette_t));
	if(!c) {
		return NULL;
	}
	c->n_threads = vje_advise_num_threads(w*h);

	c->lut = (float*) vj_malloc(sizeof(float) * 512 * 512 );
	if(!c->lut) {
		free(c);
		return NULL;
	}

	return (void*) c;
}

void chromapalette_free(void *ptr) {
	chromapalette_t *c = (chromapalette_t*) ptr;
	if(c) {
		free(c->lut);
		free(c);
	}
}

static void calc_lut(chromapalette_t *c, int tolerance, int softness) {
    float outer_r = (float)tolerance;
    float inner_r = outer_r - (float)softness;
    if (inner_r < 0) inner_r = 0;
    float inv_range = 1.0f / ((outer_r - inner_r > 0.1f) ? (outer_r - inner_r) : 0.1f);

    for (int dv = -255; dv <= 255; dv++) {
        for (int du = -255; du <= 255; du++) {
            float dist = sqrtf((float)(du * du + dv * dv));
            float blend = 0.0f;
            if (dist < inner_r)
				blend = 1.0f;
            else if (dist < outer_r)
				blend = (outer_r - dist) * inv_range;

            c->lut[(dv + 255) * 512 + (du + 255)] = blend;
        }
    }
}

void chromapalette_apply(void *ptr, VJFrame *frame, int *args) {

    uint8_t lut_cb[256];
	uint8_t lut_cr[256];

	chromapalette_t *c = (chromapalette_t*) ptr;

    int tolerance = args[0];
    int r = args[1];
	int g = args[2];
	int b = args[3];
    int color_cb = args[4];
	int color_cr = args[5];
    int softness = args[6];

    const int len = frame->len;
	const int n_threads = c->n_threads;

    uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];

    int target_y = 0;
	int target_u = 128;
	int target_v = 128;

    _rgb2yuv(r, g, b, target_y, target_u, target_v);

    float outer_r = (float)tolerance;
    float inner_r = outer_r - (float)softness;

    if (inner_r < 0) inner_r = 0;

    float range = (outer_r - inner_r);

	if (range < 0.1f) range = 0.1f;

	if(softness != c->softness || tolerance != c->tolerance) {
		calc_lut(c, tolerance, softness);
		c->softness = softness;
		c->tolerance = tolerance;
	}

	const float *restrict lut = c->lut;

    for (int i = 0; i < 256; i++) {
        lut_cb[i] = CLAMP_UV(128 + (int)(((float)(color_cb - i) * 0.492f) + 0.5f));
        lut_cr[i] = CLAMP_UV(128 + (int)(((float)(color_cr - i) * 0.877f) + 0.5f));
    }

	#pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int i = 0; i < len; i++) {
        int du_idx = (int)Cb[i] - target_u + 255;
        int dv_idx = (int)Cr[i] - target_v + 255;

        float blend = lut[dv_idx * 512 + du_idx];

        if (blend > 0.0f) {
            int target_cb = lut_cb[Y[i]];
            int target_cr = lut_cr[Y[i]];

            Cb[i] = CLAMP_UV(Cb[i] + (int)(blend * (target_cb - Cb[i])));
            Cr[i] = CLAMP_UV(Cr[i] + (int)(blend * (target_cr - Cr[i])));
        }
    }
}
