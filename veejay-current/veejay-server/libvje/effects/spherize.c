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
#include "common.h"
#include <veejaycore/vjmem.h>
#include "spherize.h"

vj_effect *spherize_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 100;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 360;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = (int)sqrtf(w*w/4.0f + h*h/4.0f);
    ve->limits[0][3] = 10;
    ve->limits[1][3] = 200;
    ve->limits[0][4] = 10;
    ve->limits[1][4] = 200;
    ve->limits[0][5] = 0;
    ve->limits[1][5] = w;
    ve->limits[0][6] = 0;
    ve->limits[1][6] = h;
    ve->limits[0][7] = 0;
    ve->limits[1][7] = 2;

    ve->defaults[0] = 50;
    ve->defaults[1] = 0;
    ve->defaults[2] = ve->limits[1][2] / 2;
    ve->defaults[3] = 100;
    ve->defaults[4] = 100;
    ve->defaults[5] = w / 2;
    ve->defaults[6] = h / 2;

    ve->description = "Spherize";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Strength" , "Angle", "Radius", "Ratio X" , "Ratio Y", "Center X" , "Center Y", "Mode" );
    return ve;
}

typedef struct 
{
    uint8_t *buf[3];
    uint8_t *buf_alloc;

    float *lut;
    float *atan2_lut;
    float *sin_lut;
    float *sqrt_lut;
    float *exp_lut;

    int last_cx;
    int last_cy;
    int last_radius;
    float last_angle;
} spherize_t;

static void init_atan2_lut(spherize_t *f, int w, int h, int cx, int cy)
{
    for (int x = 0; x < w; ++x) {
        double dx = x - cx;

        for (int y = 0; y < h; ++y) {
            double dy = y - cy;
            f->atan2_lut[y * w + x] = atan2(dy, dx); // slow
        }
    }
    f->last_cx = cx;
    f->last_cy = cy;
}

static inline void init_sin_lut(spherize_t *f, int w, int h, float angle)
{
    const int size = w * h;
    for (int i = 0; i < size; ++i)
        f->sin_lut[i] = sinf(f->atan2_lut[i] - angle);

    f->last_angle = angle;
}

static void init_sqrt_lut(spherize_t *f, int w, int h, int cx, int cy)
{
    for (int y = 0; y < h; ++y) {
        const float dy = (float)(y - cy);
        const int row = y * w;

        for (int x = 0; x < w; ++x) {
            const float dx = (float)(x - cx);
            f->sqrt_lut[row + x] = sqrtf(dx * dx + dy * dy);
        }
    }
}

static void init_exp_lut(spherize_t *f, int w, int h, int radius)
{
    const float inv_sigma =
        1.0f / (2.0f * radius * radius);

    const int size = w * h;

    for (int i = 0; i < size; ++i) {
        const float d = f->sqrt_lut[i];
        f->exp_lut[i] = expf(-d * d * inv_sigma);
    }

    f->last_radius = radius;
}

void *spherize_malloc(int w, int h)
{
    spherize_t *s = (spherize_t*) vj_calloc(sizeof(spherize_t));
    if (!s) return NULL;

    const int padded_w = w + 2;
    const int padded_h = h + 2;
    const int padded_pixels = padded_w * padded_h;

    s->buf[0] = (uint8_t*) vj_malloc(padded_pixels * 3);
    if (!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->buf_alloc = s->buf[0];

    s->buf[1] = s->buf[0] + padded_pixels;
    s->buf[2] = s->buf[1] + padded_pixels;

    s->buf[0] += padded_w + 1;
    s->buf[1] += padded_w + 1;
    s->buf[2] += padded_w + 1;

    const int pixels = w * h;
    s->lut = (float*) vj_malloc(sizeof(float) * pixels * 4);
    if (!s->lut) {
        free(s->buf_alloc);
        free(s);
        return NULL;
    }

    s->atan2_lut = s->lut;
    s->sin_lut   = s->atan2_lut + pixels;
    s->sqrt_lut  = s->sin_lut + pixels;
    s->exp_lut   = s->sqrt_lut + pixels;

    init_sqrt_lut(s, w, h, w/2, h/2);

    return s;
}

void spherize_free(void *ptr)
{
    if (!ptr) return;
    spherize_t *s = (spherize_t*) ptr;

    if(s->buf_alloc) {
        free(s->buf_alloc);
    }
    // Free LUT allocation
    if(s->lut)
        free(s->lut);

    free(s);
}

void spherize_apply(void *ptr, VJFrame *frame, int *args) {
    spherize_t *s = (spherize_t*)ptr;

    const float strength = args[0] * 0.01f;
    const float angle    = args[1] * (M_PI/180.0f);
    const float ratio_x  = args[3] * 0.01f;
    const float ratio_y  = args[4] * 0.01f;
    const int   center_x = args[5];
    const int   center_y = args[6];
    const int radius = args[2];
    const int mode = args[7];

    const int width  = frame->width;
    const int height = frame->height;
    const int len = width * height;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    veejay_memcpy(bufY, srcY, len);
    veejay_memcpy(bufU, srcU, len);
    veejay_memcpy(bufV, srcV, len);

    if (s->last_cx != center_x || s->last_cy != center_y)
        init_atan2_lut(s, width, height, center_x, center_y);

    if( s->last_angle != angle )
        init_sin_lut(s, width, height, angle);

    if (s->last_radius != radius)
        init_exp_lut(s, width, height, radius);

    float *restrict sin_lut = s->sin_lut;
    float *restrict exp_lut = s->exp_lut;

    switch(mode) {
        case 0:
            for (int y = 0; y < height; ++y) {
                const int row = y * width;
                const float dy_scaled = (y - center_y) * ratio_y;

                for (int x = 0; x < width; ++x) {
                    const float dx_scaled = (x - center_x) * ratio_x;
                    const float ratio = 1.0f + strength * sin_lut[row + x] * exp_lut[row + x];

                    int new_x = (int)(center_x + dx_scaled * ratio);
                    int new_y = (int)(center_y + dy_scaled * ratio);

                    if (new_x >= 0 && new_x < width && new_y >= 0 && new_y < height) {
                        int idx = row + x;
                        srcY[idx] = bufY[new_y * width + new_x];
                        srcU[idx] = bufU[new_y * width + new_x];
                        srcV[idx] = bufV[new_y * width + new_x];
                    }
                }
            }
            break;
        case 1:
            for (int y = 0; y < height; ++y) {
                const int row = y * width;
                const float dy_scaled = (y - center_y) * ratio_y;

                for (int x = 0; x < width; ++x) {
                    const float dx_scaled = (x - center_x) * ratio_x;
                    const float ratio = 1.0f + strength * sin_lut[row + x] * exp_lut[row + x];

                    int new_x = (int)(center_x + dx_scaled * ratio);
                    int new_y = (int)(center_y + dy_scaled * ratio);
                    new_x = (new_x + width) % width;
                    new_y = (new_y + height) % height;
                    int idx = row + x;
                    srcY[idx] = bufY[new_y * width + new_x];
                    srcU[idx] = bufU[new_y * width + new_x];
                    srcV[idx] = bufV[new_y * width + new_x];
                }
            }
            break;
        case 2:
            for (int y = 0; y < height; ++y) {
                const int row = y * width;
                const float dy_scaled = (y - center_y) * ratio_y;

                for (int x = 0; x < width; ++x) {
                    const float dx_scaled = (x - center_x) * ratio_x;
                    const float ratio = 1.0f + strength * sin_lut[row + x] * exp_lut[row + x];

                    int new_x = (int)(center_x + dx_scaled * ratio);
                    int new_y = (int)(center_y + dy_scaled * ratio);
                    if(new_x < 0) new_x = -new_x;
                    if(new_x >= width) new_x = 2*width - new_x - 2;
                    if(new_y < 0) new_y = -new_y;
                    if(new_y >= height) new_y = 2*height - new_y - 2;
                    int idx = row + x;
                    srcY[idx] = bufY[new_y * width + new_x];
                    srcU[idx] = bufU[new_y * width + new_x];
                    srcV[idx] = bufV[new_y * width + new_x];
                }
            }
            break;
    }
}
