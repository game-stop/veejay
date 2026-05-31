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
#include "rotate.h"

typedef struct {
    uint8_t *buf[3];
    float sin_lut[360];
    float cos_lut[360];
    double rotate;
    int frameCount;
    int direction;
    int n_threads;
} rotate_t;

static inline int rotate_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int rotate_wrap360(double v)
{
    int a = (int)v % 360;

    if(a < 0)
        a += 360;

    return a;
}

static inline uint8_t rotate_bilinear_u8(
    const uint8_t *restrict img,
    int w,
    int x_fixed,
    int y_fixed
) {
    const int x = x_fixed >> 16;
    const int y = y_fixed >> 16;

    const int xf = (x_fixed >> 8) & 0xff;
    const int yf = (y_fixed >> 8) & 0xff;

    const int idx = y * w + x;

    const int w11 = (256 - xf) * (256 - yf);
    const int w21 = xf * (256 - yf);
    const int w12 = (256 - xf) * yf;
    const int w22 = xf * yf;

    const int res =
        img[idx]         * w11 +
        img[idx + 1]     * w21 +
        img[idx + w]     * w12 +
        img[idx + w + 1] * w22;

    return (uint8_t)(res >> 16);
}

static inline float rotate_mirror_coord(float p, float max_safe)
{
    while(p < 0.0f || p > max_safe) {
        p = (p < 0.0f) ? -p : (2.0f * max_safe - p);
    }

    return p;
}

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
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Rotate",
        "Automatic",
        "Duration"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Manual",
        "Automatic"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,      0,                  360,                8,  30, 1200, 3000, 0,   45,    /* Rotate */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Automatic */
        VJ_BEAT_SPEED,    VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 24,                 360,                6,  22, 1800, 4200, 900, 30     /* Duration */
    );

    (void) width;
    (void) height;

    return ve;
}

void *rotate_malloc(int width, int height)
{
    rotate_t *r = (rotate_t*) vj_calloc(sizeof(rotate_t));
    if(!r)
        return NULL;

    const int len = width * height;

    r->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!r->buf[0]) {
        free(r);
        return NULL;
    }

    r->buf[1] = r->buf[0] + len;
    r->buf[2] = r->buf[1] + len;

    r->rotate = 0.0;
    r->frameCount = 0;
    r->direction = 1;

    for(int i = 0; i < 360; i++) {
        const double rad = (double)i * M_PI / 180.0;
        r->sin_lut[i] = a_sin(rad);
        r->cos_lut[i] = a_cos(rad);
    }

    r->n_threads = vje_advise_num_threads(len);

    return (void*) r;
}

void rotate_free(void *ptr)
{
    rotate_t *r = (rotate_t*) ptr;
    if(!r)
        return;

    if(r->buf[0])
        free(r->buf[0]);

    free(r);
}

void rotate_apply(void *ptr, VJFrame *frame, int *args)
{
    rotate_t *r = (rotate_t*) ptr;
    if(!r || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width < 2 || height < 2 || len <= 0)
        return;

    int rotate_arg = rotate_clampi(args[0], 0, 360);
    int automatic = args[1] ? 1 : 0;
    int duration = rotate_clampi(args[2], 1, 1500);

    double rotate_value;

    if(automatic) {
        rotate_value = r->rotate;

        r->rotate += (double)r->direction * (360.0 / (double)duration);
        r->frameCount++;

        if(r->frameCount >= duration || r->rotate <= 0.0 || r->rotate >= 360.0) {
            r->direction *= -1;
            r->frameCount = 0;

            if(r->rotate < 0.0)
                r->rotate = 0.0;
            else if(r->rotate > 360.0)
                r->rotate = 360.0;
        }
    } else {
        rotate_value = (double)rotate_arg;
        r->rotate = rotate_value;
        r->frameCount = 0;
        r->direction = 1;
    }

    veejay_memcpy(r->buf[0], frame->data[0], len);
    veejay_memcpy(r->buf[1], frame->data[1], len);
    veejay_memcpy(r->buf[2], frame->data[2], len);

    const float center_x = ((float)width  - 1.0f) * 0.5f;
    const float center_y = ((float)height - 1.0f) * 0.5f;

    const int angle = rotate_wrap360(rotate_value);
    const float c = r->cos_lut[angle];
    const float s = r->sin_lut[angle];

    const float max_x = (float)width  - 1.001f;
    const float max_y = (float)height - 1.001f;

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

#pragma omp parallel for schedule(static) num_threads(r->n_threads)
    for(int y = 0; y < height; y++) {
        const float dy = (float)y - center_y;
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const float dx = (float)x - center_x;

            float rx = dx * c - dy * s + center_x;
            float ry = dx * s + dy * c + center_y;

            rx = rotate_mirror_coord(rx, max_x);
            ry = rotate_mirror_coord(ry, max_y);

            const int rx_f = (int)(rx * 65536.0f);
            const int ry_f = (int)(ry * 65536.0f);
            const int dst = row + x;

            dstY[dst] = rotate_bilinear_u8(r->buf[0], width, rx_f, ry_f);
            dstU[dst] = rotate_bilinear_u8(r->buf[1], width, rx_f, ry_f);
            dstV[dst] = rotate_bilinear_u8(r->buf[2], width, rx_f, ry_f);
        }
    }
}