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
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
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
    ve->sub_format = 1;
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
    int n_threads;
} mirror_distortion_t;

void *mirrordistortion_malloc(int w, int h) {
    mirror_distortion_t *m = (mirror_distortion_t*) vj_malloc(sizeof(mirror_distortion_t));
    if(!m) return NULL;

    m->distortion = -1.0f;
    m->cos_lut = (float*) vj_malloc(sizeof(float) * w);
    m->sin_lut = (float*) vj_malloc(sizeof(float) * h);

    m->buf[0] = (uint8_t*) vj_malloc(w * h * 3);

    if(!m->cos_lut || !m->sin_lut || !m->buf[0]) {
        if(m->cos_lut) free(m->cos_lut);
        if(m->sin_lut) free(m->sin_lut);
        if(m->buf[0]) free(m->buf[0]);
        free(m);
        return NULL;
    }

    m->buf[1] = m->buf[0] + (w * h);
    m->buf[2] = m->buf[1] + (w * h);
    m->n_threads = vje_advise_num_threads(w * h);

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

void mirrordistortion_apply(void *ptr, VJFrame *frame, int *args) {
    mirror_distortion_t *m = (mirror_distortion_t*) ptr;
    const int w = frame->width;
    const int h = frame->height;

    float dist = args[0] * 0.01f;
    float offX = (float)(args[1] - w);
    float offY = (float)(args[2] - h);

    if(dist != m->distortion) {
        for(int i = 0; i < w; i++) m->cos_lut[i] = a_cos(i * dist);
        for(int i = 0; i < h; i++) m->sin_lut[i] = a_sin(i * dist);
        m->distortion = dist;
    }

    veejay_memcpy(m->buf[0], frame->data[0], w * h);
    veejay_memcpy(m->buf[1], frame->data[1], w * h);
    veejay_memcpy(m->buf[2], frame->data[2], w * h);

    uint8_t * restrict srcY = m->buf[0];
    uint8_t * restrict srcU = m->buf[1];
    uint8_t * restrict srcV = m->buf[2];

    #pragma omp parallel for num_threads(m->n_threads) schedule(static)
    for (int i = 0; i < h; i++) {
        uint8_t *dstY = frame->data[0] + (i * w);
        uint8_t *dstU = frame->data[1] + (i * w);
        uint8_t *dstV = frame->data[2] + (i * w);

        float s_sin = m->sin_lut[i];
        for (int j = 0; j < w; j++) {
            int sx = j + (int)(offX * s_sin);
            int sy = i + (int)(offY * m->cos_lut[j]);
            sx = (sx < 0) ? 0 : (sx >= w ? w - 1 : sx);
            sy = (sy < 0) ? 0 : (sy >= h ? h - 1 : sy);

            int srcIdx = sy * w + sx;

            dstY[j] = srcY[srcIdx];
            dstU[j] = srcU[srcIdx];
            dstV[j] = srcV[srcIdx];
        }
    }
}