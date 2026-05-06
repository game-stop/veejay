/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include <string.h>

#define FP 16
#define FP_ONE (1 << FP)

static inline int mirror_coord(int v, int max)
{
    if (max <= 1) return 0;

    int period = (max << 1) - 2;
    if (v < 0)
        v = -v - 1;
    while (v >= period)
        v -= period;

    if (v >= max)
        v = period - v;

    return v;
}

vj_effect *virtualcamera_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;   ve->limits[1][0] = 100; ve->defaults[0] = 50;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 100; ve->defaults[1] = 50;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 100; ve->defaults[2] = 15;
    ve->limits[0][3] = 1;   ve->limits[1][3] = 400; ve->defaults[3] = 100;
    ve->limits[0][4] = 1;   ve->limits[1][4] = 400; ve->defaults[4] = 100;
    ve->limits[0][5] = 0;   ve->limits[1][5] = 1;   ve->defaults[5] = 1;
    ve->limits[0][6] = 0;   ve->limits[1][6] = 1;   ve->defaults[6] = 0;

    ve->sub_format = 1;
    ve->description = "Virtual Camera Pro";
    ve->param_description = vje_build_param_list( ve->num_params, 
        "Target X (%)", "Target Y (%)", "Move Speed (%)", 
        "FOV Width (%)", "FOV Height (%)", 
        "Lock Aspect (Toggle)", "Edge Mode (Mirr/Blk)"
    );
    
    return ve;
}

typedef struct {
    uint8_t *buf[3];
    int *xmap;
    int xmap_w;
    float current_x;
    float current_y;
    int is_initialized;
    int n_threads;
    int w, h;
} virtualcam_t;

void *virtualcamera_malloc(int w, int h) {
    virtualcam_t *c = (virtualcam_t*) vj_calloc(sizeof(virtualcam_t));
    if(!c) return NULL;

    size_t plane_size = w * h;

    c->buf[0] = (uint8_t*) vj_malloc(plane_size * 3);
    if(!c->buf[0]) {
        free(c);
        return NULL;
    }

    c->xmap = (int*) vj_malloc(sizeof(int) * w);
    if(!c->xmap) {
        free(c->buf[0]);
        free(c);
        return NULL;
    }

    c->xmap_w = w;

    c->buf[1] = c->buf[0] + plane_size;
    c->buf[2] = c->buf[1] + plane_size;

    c->is_initialized = 0;
    c->w = w; 
    c->h = h;
    c->n_threads = vje_advise_num_threads(plane_size);
    
    return (void*) c;
}

void virtualcamera_free(void *ptr) {
    virtualcam_t *c = (virtualcam_t*) ptr;
    if (c) {
        if (c->buf[0]) free(c->buf[0]);
        if (c->xmap) free(c->xmap);
        free(c);
    }
}

void virtualcamera_apply(void *ptr, VJFrame* frame, int *args)
{
    virtualcam_t *c = (virtualcam_t*) ptr;
    const int w = frame->width;
    const int h = frame->height;

    const float target_x = (args[0] * w) * 0.01f;
    const float target_y = (args[1] * h) * 0.01f;
    const float speed    = args[2] * 0.01f;

    if (!c->is_initialized) {
        c->current_x = target_x;
        c->current_y = target_y;
        c->is_initialized = 1;
    } else {
        c->current_x += (target_x - c->current_x) * speed;
        c->current_y += (target_y - c->current_y) * speed;
    }


    const float fov_w = (args[3] * w) * 0.01f;
    const float fov_h = (args[5])
        ? fov_w * ((float)h / (float)w)
        : (args[4] * h) * 0.01f;

    const float start_x = c->current_x - (fov_w * 0.5f);
    const float start_y = c->current_y - (fov_h * 0.5f);

    const float step_x = fov_w / (float)w;
    const float step_y = fov_h / (float)h;

    const int edge_black = args[6];


    uint8_t * restrict srcY_base = frame->data[0];
    uint8_t * restrict srcU_base = frame->data[1];
    uint8_t * restrict srcV_base = frame->data[2];

    uint8_t * restrict dstY_base = c->buf[0];
    uint8_t * restrict dstU_base = c->buf[1];
    uint8_t * restrict dstV_base = c->buf[2];

    const int start_x_fp = (int)(start_x * FP_ONE);
    const int start_y_fp = (int)(start_y * FP_ONE);
    const int step_x_fp  = (int)(step_x  * FP_ONE);
    const int step_y_fp  = (int)(step_y  * FP_ONE);

    int * restrict xmap = c->xmap;

    int sx_fp = start_x_fp;

    if (edge_black) {
        for (int x = 0; x < w; x++, sx_fp += step_x_fp)
            xmap[x] = sx_fp >> FP;
    } else {
        for (int x = 0; x < w; x++, sx_fp += step_x_fp) {
            int sx_i = sx_fp >> FP;
            xmap[x] = mirror_coord(sx_i, w);
        }
    }

    if (edge_black) {

        #pragma omp parallel for num_threads(c->n_threads) schedule(static)
        for (int y = 0; y < h; y++) {

            int sy_fp = start_y_fp + y * step_y_fp;
            int sy_i  = sy_fp >> FP;

            int y_out = (sy_i < 0 || sy_i >= h);

            uint8_t * restrict dstY = dstY_base + y * w;
            uint8_t * restrict dstU = dstU_base + y * w;
            uint8_t * restrict dstV = dstV_base + y * w;

            if (y_out) {
                // whole row black
                for (int x = 0; x < w; x++) {
                    *dstY++ = 16;
                    *dstU++ = 128;
                    *dstV++ = 128;
                }
                continue;
            }

            uint8_t * restrict srcY = srcY_base + sy_i * w;
            uint8_t * restrict srcU = srcU_base + sy_i * w;
            uint8_t * restrict srcV = srcV_base + sy_i * w;

            for (int x = 0; x < w; x++) {
                int sx = xmap[x];

                if (sx < 0 || sx >= w) {
                    *dstY++ = 16;
                    *dstU++ = 128;
                    *dstV++ = 128;
                } else {
                    *dstY++ = srcY[sx];
                    *dstU++ = srcU[sx];
                    *dstV++ = srcV[sx];
                }
            }
        }

    } else {

        for (int y = 0; y < h; y++) {

            int sy_fp = start_y_fp + y * step_y_fp;
            int sy = mirror_coord(sy_fp >> FP, h);

            uint8_t * restrict srcY = srcY_base + sy * w;
            uint8_t * restrict srcU = srcU_base + sy * w;
            uint8_t * restrict srcV = srcV_base + sy * w;

            uint8_t * restrict dstY = dstY_base + y * w;
            uint8_t * restrict dstU = dstU_base + y * w;
            uint8_t * restrict dstV = dstV_base + y * w;

            for (int x = 0; x < w; x++) {
                int sx = xmap[x];

                *dstY++ = srcY[sx];
                *dstU++ = srcU[sx];
                *dstV++ = srcV[sx];
            }
        }
    }

    size_t plane_size = w * h;
    veejay_memcpy(frame->data[0], c->buf[0], plane_size);
    veejay_memcpy(frame->data[1], c->buf[1], plane_size);
    veejay_memcpy(frame->data[2], c->buf[2], plane_size);
}