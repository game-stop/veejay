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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>


typedef struct {
    float *accY;
    float *accU;
    float *accV;

    uint8_t *prevY;
    int initialized;
} motionblur_t;


vj_effect *motionblur_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 5;
    ve->defaults   = (int *) vj_calloc(sizeof(int) * 5);
    ve->limits[0]  = (int *) vj_calloc(sizeof(int) * 5);
    ve->limits[1]  = (int *) vj_calloc(sizeof(int) * 5);

    ve->defaults[0] = 8; ve->limits[0][0] = 1;   ve->limits[1][0] = 64;
    ve->defaults[1] = 90; ve->limits[0][1] = 50; ve->limits[1][1] = 100;
    ve->defaults[2] = 0; ve->limits[0][2] = -100; ve->limits[1][2] = 100;
    ve->defaults[3] = 50; ve->limits[0][3] = 0; ve->limits[1][3] = 100;
    ve->defaults[4] = 30; ve->limits[0][4] = 0; ve->limits[1][4] = 255;

    ve->description = "Motion Blur";
    ve->sub_format  = 1;
    ve->extra_frame = 0;
    ve->parallel    = 0;

    ve->param_description = vje_build_param_list(
        5,
        "Shutter", "Decay", "Direction", "Velocity", "Reset Threshold"
    );

    return ve;
}

void *motionblur_malloc(int w, int h)
{
    motionblur_t *m = vj_malloc(sizeof(motionblur_t));
    if (!m) return NULL;

    const int size = w*h;

    m->accY  = vj_malloc(sizeof(float)*size);
    m->accU  = vj_malloc(sizeof(float)*size);
    m->accV  = vj_malloc(sizeof(float)*size);
    m->prevY = vj_malloc(sizeof(uint8_t)*size);

    if (!m->accY || !m->accU || !m->accV || !m->prevY) {
        free(m->accY); free(m->accU); free(m->accV); free(m->prevY); free(m);
        return NULL;
    }

    m->initialized = 0;
    return m;
}

void motionblur_free(void *ptr)
{
    motionblur_t *m = ptr;
    if (!m) return;
    free(m->accY);
    free(m->accU);
    free(m->accV);
    free(m->prevY);
    free(m);
}

static inline uint8_t clamp_u8(float v)
{
    int iv = (int)(v + 0.5f);
    if (iv < 0) return 0;
    if (iv > 255) return 255;
    return (uint8_t)iv;
}

void motionblur_apply(void *ptr, VJFrame *f, int *a)
{
    motionblur_t *m = ptr;
    const int w = f->width;
    const int h = f->height;
    const int size = w * h;

    uint8_t *Y = f->data[0];
    uint8_t *U = f->data[1];
    uint8_t *V = f->data[2];

    const int shutter = a[0];
    const float decay = a[1] * 0.01f;
    const int dir = a[2];
    const float velg = a[3] * 0.01f;
    const int reset = a[4];

    if (!m->initialized) {
        for (int i = 0; i < size; i++) {
            m->accY[i] = Y[i];
            m->accU[i] = U[i] - 128.0f;
            m->accV[i] = V[i] - 128.0f;
            m->prevY[i] = Y[i];
        }
        m->initialized = 1;
        return;
    }

    const float alpha_base = 1.0f / (float)shutter;

    const float angle = dir * (float)M_PI / 100.0f;
    const float dx = cosf(angle) * 0.5f;
    const float dy = sinf(angle) * 0.5f;

    float row_bias[h], col_bias[w];

    for (int y = 0; y < h; y++)
        row_bias[y] = dy * ((float)y / (float)h - 0.5f);
    for (int x = 0; x < w; x++)
        col_bias[x] = dx * ((float)x / (float)w - 0.5f);


    float frame_diff = 0.0f;
    int sampled_count = 0;
    const int step = 16;
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            int i = y * w + x;
            frame_diff += fabsf(Y[i] - m->prevY[i]);
            sampled_count++;
        }
    }
    frame_diff /= (float)sampled_count;

    int reset_frame = frame_diff > (float)reset;

    if (reset_frame) {
        for (int i = 0; i < size; i++) {
            m->accY[i] = Y[i];
            m->accU[i] = U[i] - 128.0f;
            m->accV[i] = V[i] - 128.0f;

            Y[i] = clamp_u8(m->accY[i]);
            U[i] = clamp_u8(m->accU[i] + 128.0f);
            V[i] = clamp_u8(m->accV[i] + 128.0f);

            m->prevY[i] = Y[i];
        }
        return;
    }


	const float weight = (1.0f / 255.0f);
	for (int y = 0; y < h; y++) {
		float yf = row_bias[y];

		uint8_t *restrict Y_row = Y + y * w;
		uint8_t *restrict U_row = U + y * w;
		uint8_t *restrict V_row = V + y * w;
		float *restrict accY_row = m->accY + y * w;
		float *restrict accU_row = m->accU + y * w;
		float *restrict accV_row = m->accV + y * w;
		uint8_t *prevY_row = m->prevY + y * w;

		for (int x = 0; x < w; x++) {
			int i = x;

			int diff_int = (int)Y_row[i] - (int)prevY_row[i];
			diff_int = (diff_int ^ (diff_int >> 31)) - (diff_int >> 31);

			float v = 1.0f + velg * (diff_int * weight);

			float bias = 1.0f + col_bias[x] + yf;

			float a_blend = alpha_base * v * bias;
			a_blend = (a_blend > 1.f) * 1.f + (a_blend <= 1.f) * a_blend;

			float y_val = accY_row[i] * decay + Y_row[i] * a_blend;
			float u_val = accU_row[i] * decay + (U_row[i] - 128.0f) * a_blend;
			float v_val = accV_row[i] * decay + (V_row[i] - 128.0f) * a_blend;

			uint8_t y_clamped = clamp_u8(y_val);
			uint8_t u_clamped = clamp_u8(u_val + 128.0f);
			uint8_t v_clamped = clamp_u8(v_val + 128.0f);

			accY_row[i] = y_val;
			accU_row[i] = u_val;
			accV_row[i] = v_val;

            Y_row[i] = prevY_row[i] = y_clamped;
            U_row[i] = u_clamped;
            V_row[i] = v_clamped;
		}
	}

}
