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
#include <config.h>
#include <limits.h>
#include <math.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>
#include "mirror.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    uint8_t *buf[3];
    int w, h;
    int n_threads;
} mirror_t;

vj_effect *mirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = w;
    ve->defaults[0] = w / 2;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = h;
    ve->defaults[1] = h / 2;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 360;
    ve->defaults[2] = 0;

    ve->description = "Axis Mirror Folding";
    ve->sub_format = 1;

    ve->param_description = vje_build_param_list(ve->num_params,  "Center X", "Center Y", "Angle");

    return ve;
}

void *mirror_malloc(int w, int h)
{
    mirror_t *m = (mirror_t*) vj_malloc(sizeof(mirror_t));
    if(!m) return NULL;

    m->buf[0] = (uint8_t*) vj_malloc(w * h * 3);
    if(!m->buf[0]) {
        free(m);
        return NULL;
    }
    m->buf[1] = m->buf[0] + (w * h);
    m->buf[2] = m->buf[1] + (w * h);
    m->w = w; m->h = h;

    m->n_threads = vje_advise_num_threads(w*h);

    return (void*) m;
}

void mirror_free(void *ptr) {
    mirror_t *m = (mirror_t*) ptr;
    if(m) {
        if(m->buf[0]) free(m->buf[0]);
        free(m);
    }
}

void mirror_apply(void *ptr, VJFrame *frame, int *args) {
    mirror_t *m = (mirror_t*) ptr;
    const int width = frame->width;
    const int height = frame->height;

    const uint8_t *srcY = m->buf[0];
    const uint8_t *srcU = m->buf[1];
    const uint8_t *srcV = m->buf[2];

    uint8_t *dstY = frame->data[0];
    uint8_t *dstU = frame->data[1];
    uint8_t *dstV = frame->data[2];

    veejay_memcpy(m->buf[0], dstY, width * height);
    veejay_memcpy(m->buf[1], dstU, width * height);
    veejay_memcpy(m->buf[2], dstV, width * height);

    const float cx = (float)args[0];
    const float cy = (float)args[1];
    const float angle_deg = (float)args[2];
    
    const float rad = angle_deg * (M_PI / 180.0f);
    const float nx = cosf(rad);
    const float ny = sinf(rad);

    #pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;

            float dot = (dx * nx + dy * ny);

            if (dot > 0) {
                float rx = (float)x - 2.0f * dot * nx;
                float ry = (float)y - 2.0f * dot * ny;

                int mx = (int)(rx + 0.5f);
                int my = (int)(ry + 0.5f);

                mx = (mx < 0) ? -mx : mx;
                int quotientX = mx / width;
                mx %= width;
                if (quotientX & 1) mx = (width - 1) - mx;

                my = (my < 0) ? -my : my;
                int quotientY = my / height;
                my %= height;
                if (quotientY & 1) my = (height - 1) - my;

                int idx = y * width + x;
                int s_idx = my * width + mx;

                dstY[idx] = srcY[s_idx];
                dstU[idx] = srcU[s_idx];
                dstV[idx] = srcV[s_idx];
            }
        }
    }
}