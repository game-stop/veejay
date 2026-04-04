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

#include <libvje/effects/common.h>
#include <veejaycore/vjmem.h>
#include "transcarot.h"
#include <libvje/effects/common.h>
#include <veejaycore/vjmem.h>

typedef struct {
    float wipePositionX;
    float wipePositionY;
    float wipePosition;
    int directionX;
    int directionY;
    int initialized;
    int n_threads;
} wipe_t;

vj_effect *transcarot_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 2;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 100;

    ve->defaults[1] = 0;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->sub_format = 1;
    ve->description = "Transition Wipe Diagonal";
    ve->param_description = vje_build_param_list(ve->num_params, "Speed", "Mode");
    ve->extra_frame = 1;
    return ve;
}

void *transcarot_malloc(int w, int h)
{
    wipe_t *wipe = (wipe_t*) vj_calloc(sizeof(wipe_t));
    if(!wipe)
        return NULL;
    wipe->directionX = 1;
    wipe->directionY = 1;
    wipe->initialized = 0;
    wipe->n_threads = vje_advise_num_threads(w*h);
    return wipe;
}

void transcarot_free(void *ptr) {
    wipe_t *wipe = (wipe_t*) ptr;
    if(wipe) {
        free(wipe);
    }
}

void transcarot_apply_bouncybox(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    wipe_t *wipe = (wipe_t *)ptr;
    const int width = frame->width;
    const int height = frame->height;
    const int speed = args[0];

    if (!wipe->initialized) {
        wipe->wipePositionX = 0;
        wipe->wipePositionY = 0;
        wipe->initialized = 1;
    }

    wipe->wipePositionX += (float)speed * wipe->directionX;
    wipe->wipePositionY += (float)speed * wipe->directionY;

    if (wipe->wipePositionX >= width) { wipe->wipePositionX = (float)width; wipe->directionX = -1; }
    if (wipe->wipePositionX <= 0) { wipe->wipePositionX = 0; wipe->directionX = 1; }
    if (wipe->wipePositionY >= height) { wipe->wipePositionY = (float)height; wipe->directionY = -1; }
    if (wipe->wipePositionY <= 0) { wipe->wipePositionY = 0; wipe->directionY = 1; }

    const int curX = (int)wipe->wipePositionX;
    const int curY = (int)wipe->wipePositionY;

    #pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
    for (int i = 0; i < curY; ++i) {
        const int offset = i * width;

        uint8_t *dstY = &frame->data[0][offset];
        const uint8_t *srcY = &frame2->data[0][offset];
        uint8_t *dstU = &frame->data[1][offset];
        const uint8_t *srcU = &frame2->data[1][offset];
        uint8_t *dstV = &frame->data[2][offset];
        const uint8_t *srcV = &frame2->data[2][offset];

        #pragma omp simd
        for (int j = 0; j < curX; ++j) {
            dstY[j] = srcY[j];
            dstU[j] = srcU[j];
            dstV[j] = srcV[j];
        }
    }
}

void transcarot_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    wipe_t *wipe = (wipe_t *)ptr;
    const int width = frame->width;
    const int height = frame->height;
    const int speed = args[0];
    const int mode = args[1];

    if(mode == 1 ) {
        transcarot_apply_bouncybox(ptr,frame,frame2,args);
        return;
    }

    if (!wipe->initialized) {
        wipe->wipePosition = 0;
        wipe->initialized = 1;
    }

    wipe->wipePosition += (float)speed;
    const int total_span = width + height;
    if (wipe->wipePosition >= total_span) {
        wipe->wipePosition = 0;
    }

    const int progress = (int)wipe->wipePosition;

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < height; ++i) {
        const int offset = i * width;
        int limit = progress - i;

        if (limit < 0) limit = 0;
        if (limit > width) limit = width;

        uint8_t *dstY = &frame->data[0][offset];
        const uint8_t *srcY = &frame2->data[0][offset];
        uint8_t *dstU = &frame->data[1][offset];
        const uint8_t *srcU = &frame2->data[1][offset];
        uint8_t *dstV = &frame->data[2][offset];
        const uint8_t *srcV = &frame2->data[2][offset];

        #pragma omp simd
        for (int j = 0; j < limit; ++j) {
            dstY[j] = srcY[j];
            dstU[j] = srcU[j];
            dstV[j] = srcV[j];
        }
    }
}
