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
#include "rotate.h"

typedef struct {
    uint8_t *buf[4];
    float sin_lut[360];
    float cos_lut[360];
    double rotate;
    int frameCount;
    int direction;
    int n_threads;
} rotate_t;

/* --- Internal Helpers --- */

static inline __attribute__((always_inline)) uint8_t fast_bilinear(
    const uint8_t * __restrict__ img, const int w, 
    const int x_fixed, const int y_fixed) 
{
    const int x = x_fixed >> 16;
    const int y = y_fixed >> 16;
    
    const int xf = (x_fixed >> 8) & 0xFF;
    const int yf = (y_fixed >> 8) & 0xFF;

    const int idx = y * w + x;
    
    const int w11 = (256 - xf) * (256 - yf);
    const int w21 = xf * (256 - yf);
    const int w12 = (256 - xf) * yf;
    const int w22 = xf * yf;

    const int res = (img[idx] * w11 + img[idx + 1] * w21 + 
                     img[idx + w] * w12 + img[idx + w + 1] * w22);

    return (uint8_t)(res >> 16);
}

static inline float mirror_coord(float pos, float max) {
    if (pos < 0) pos = -pos;
    if (pos >= max) pos = 2.0f * max - pos;
    // Final clamp to ensure bilinear +1 safety
    if (pos < 0) return 0;
    if (pos > max - 1.001f) return max - 1.001f;
    return pos;
}

/* --- VeeJay Effect Interface --- */

vj_effect *rotate_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->defaults[2] = 100;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 360;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 1500;
    ve->description = "Rotate (Bilinear/Mirror)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Rotate", "Automatic", "Duration");
    ve->has_user = 0;
    return ve;
}

void *rotate_malloc(int width, int height)
{
    rotate_t *r = (rotate_t*) vj_calloc(sizeof(rotate_t));
    if(!r) return NULL;

    // Buffer for Y, U, V
    r->buf[0] = (uint8_t *) vj_calloc(sizeof(uint8_t) * (width * height * 3));
    if(!r->buf[0]) {
        free(r);
        return NULL;
    }
    r->buf[1] = r->buf[0] + (width * height);
    r->buf[2] = r->buf[1] + (width * height);
    r->direction = 1;

    for(int i = 0; i < 360; i++) {
        r->sin_lut[i] = a_sin(i * M_PI / 180.0);
        r->cos_lut[i] = a_cos(i * M_PI / 180.0);
    }
    r->n_threads = vje_advise_num_threads(width * height);
    return (void*) r;
}

void rotate_free(void *ptr) {
    rotate_t *r = (rotate_t*) ptr;
    if(r->buf[0]) free(r->buf[0]);
    free(r);
}
void rotate_apply(void *ptr, VJFrame *frame, int *args)
{
    rotate_t *r = (rotate_t*) ptr;
    const int width = frame->width;
    const int height = frame->height;
    const int plane_size = width * height;
    
    double rotate = args[0];
    if(args[1]) { 
        rotate = r->rotate;
        r->rotate += (r->direction * (360.0 / args[2]));
        r->frameCount++;
        if(r->frameCount % args[2] == 0 || (r->rotate <= 0 || r->rotate >= 360)) {
            r->direction *= -1;
            r->frameCount = 0;
        }
    }

    // 1. Copy ALL planes at full resolution (4:4:4)
    veejay_memcpy(r->buf[0], frame->data[0], plane_size);
    veejay_memcpy(r->buf[1], frame->data[1], plane_size);
    veejay_memcpy(r->buf[2], frame->data[2], plane_size);

    const float centerX = width * 0.5f;
    const float centerY = height * 0.5f;
    const int angle = (int)rotate % 360;
    const float c = r->cos_lut[angle];
    const float s = r->sin_lut[angle];

    uint8_t *dstY = frame->data[0];
    uint8_t *dstU = frame->data[1];
    uint8_t *dstV = frame->data[2];

    const float w_max = (float)width - 1.001f;
    const float h_max = (float)height - 1.001f;

#pragma omp parallel for schedule(static) num_threads(r->n_threads)
    for (int y = 0; y < height; ++y) {
        const float dy = (float)y - centerY;
        const int row_off = y * width;

        for (int x = 0; x < width; ++x) {
            const float dx = (float)x - centerX;

            // Rotate
            float rx = dx * c - dy * s + centerX;
            float ry = dx * s + dy * c + centerY;

            // Reflective Mirroring
            while (rx < 0 || rx > w_max) {
                if (rx < 0) rx = -rx;
                else rx = 2.0f * w_max - rx;
            }
            while (ry < 0 || ry > h_max) {
                if (ry < 0) ry = -ry;
                else ry = 2.0f * h_max - ry;
            }

            // Convert to Fixed Point once per pixel
            const int rx_f = (int)(rx * 65536.0f);
            const int ry_f = (int)(ry * 65536.0f);
            const int dst_idx = row_off + x;

            // 2. Sample all planes at full resolution
            dstY[dst_idx] = fast_bilinear(r->buf[0], width, rx_f, ry_f);
            dstU[dst_idx] = fast_bilinear(r->buf[1], width, rx_f, ry_f);
            dstV[dst_idx] = fast_bilinear(r->buf[2], width, rx_f, ry_f);
        }
    }
}