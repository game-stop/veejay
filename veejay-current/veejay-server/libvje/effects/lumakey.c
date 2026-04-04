/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "lumakey.h"

vj_effect *lumakey_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;
    ve->limits[1][4] = 1;
    ve->defaults[0] = 255;
    ve->defaults[1] = 0;
    ve->defaults[2] = 50;
    ve->defaults[3] = 20;
    ve->defaults[4] = 0;
    ve->description = "Luma Key Mixer";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Luma Min", "Luma Max", "Softness", "Invert");
    ve->hints = vje_init_value_hint_list( ve->num_params );


    return ve;
}

typedef struct {
	int n_threads;
} lumakey_t;

void *lumakey_malloc(int w, int h ) {
	lumakey_t *lk = (lumakey_t*) vj_malloc(sizeof(lumakey_t));
	if(!lk) return NULL;
	lk->n_threads = vje_advise_num_threads(w * h);
	return (void*) lk;
}

void lumakey_free(void *ptr) {
	lumakey_t *lk = (lumakey_t*) ptr;
	if(lk) {
		free(lk);
	}
}

static inline void lumakey_process(lumakey_t *lk, uint8_t *yuv1[3], uint8_t *yuv2[3], int width, int height,
                            int opacity, int luma_min, int luma_max, int softness, int invert)
{
    const unsigned int len = width * height;

    uint8_t *restrict Y1  = yuv1[0];
    uint8_t *restrict Cb1 = yuv1[1];
    uint8_t *restrict Cr1 = yuv1[2];

    uint8_t *restrict Y2  = yuv2[0];
    uint8_t *restrict Cb2 = yuv2[1];
    uint8_t *restrict Cr2 = yuv2[2];

	int alpha_lut[256];

    const float inv_soft = (softness > 0) ? 255.0f / (float)softness : 0.0f;
    const float op_scale = (float)opacity / 255.0f;

	if(invert) {
		for (int i = 0; i < 256; i++) {
			int a;

			if (i >= luma_min && i <= luma_max) {
				a = 0;
			}
			else if (softness > 0 && i >= (luma_min - softness) && i < luma_min) {
				a = 255 - (int)((i - (luma_min - softness)) * inv_soft);
			}
			else if (softness > 0 && i > luma_max && i <= (luma_max + softness)) {
				a = (int)((i - luma_max) * inv_soft);
			}
			else {
				a = 255;
			}

			if (a < 0) a = 0;
			if (a > 255) a = 255;

			a = 255 - a;

			alpha_lut[i] = (int)(a * op_scale);
		}
	}
	else {
		for (int i = 0; i < 256; i++) {
			int a;

			if (i >= luma_min && i <= luma_max) {
				a = 0;
			}
			else if (softness > 0 && i >= (luma_min - softness) && i < luma_min) {
				a = 255 - (int)((i - (luma_min - softness)) * inv_soft);
			}
			else if (softness > 0 && i > luma_max && i <= (luma_max + softness)) {
				a = (int)((i - luma_max) * inv_soft);
			}
			else {
				a = 255;
			}

			if (a < 0) a = 0;
			if (a > 255) a = 255;

			alpha_lut[i] = (int)(a * op_scale);
		}
	}

#pragma omp parallel for simd schedule(static) num_threads(lk->n_threads)
    for (unsigned int pos = 0; pos < len; pos++) {
        uint8_t y_val = Y1[pos];

        uint16_t alpha = alpha_lut[y_val];
        uint16_t inv_alpha = 256 - alpha;

        Y1[pos]  = (uint8_t)((Y1[pos]  * alpha + Y2[pos]  * inv_alpha) >> 8);
        Cb1[pos] = (uint8_t)((Cb1[pos] * alpha + Cb2[pos] * inv_alpha) >> 8);
        Cr1[pos] = (uint8_t)((Cr1[pos] * alpha + Cr2[pos] * inv_alpha) >> 8);
    }
}

void lumakey_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args )
{
	lumakey_t *lk = (lumakey_t*) ptr;

    int opacity   = args[0];
    int luma_min  = args[1];
    int luma_max  = args[2];
    int softness  = args[3];
    int invert    = args[4];

    lumakey_process(lk, frame->data, frame2->data, frame->width, frame->height,opacity, luma_min, luma_max, softness, invert);
}