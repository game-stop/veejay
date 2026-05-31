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
#include "sobel.h"

typedef struct {
    uint8_t *buf;
    int n_threads;
} sobel_t;

static inline int sobel_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int sobel_abs_i(int v)
{
    const int m = v >> 31;
    return (v ^ m) - m;
}

static inline uint8_t sobel_norm_grad(int gx, int gy)
{
    int grad = sobel_abs_i(gx) + sobel_abs_i(gy);
    int norm = (grad * 255 + 510) / 1020;

    return (uint8_t)sobel_clampi(norm, 0, 255);
}

vj_effect *sobel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 0;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 2;
    ve->defaults[1] = 1;

    ve->description = "Sobel";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Binary",
        "Gradient",
        "Wide Gradient"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 0,                  140,                6, 22, 1600, 3400, 700, 35,    /* Threshold */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Mode */
    );

    (void) w;
    (void) h;

    return ve;
}

void *sobel_malloc(int w, int h)
{
    sobel_t *s = (sobel_t*) vj_calloc(sizeof(sobel_t));
    if(!s)
        return NULL;

    s->buf = (uint8_t*) vj_malloc((size_t)w * (size_t)h);
    if(!s->buf) {
        free(s);
        return NULL;
    }

    s->n_threads = vje_advise_num_threads(w * h);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void sobel_free(void *ptr)
{
    sobel_t *s = (sobel_t*) ptr;
    if(!s)
        return;

    if(s->buf)
        free(s->buf);

    free(s);
}

static void sobel_binary_3x3(sobel_t *s, VJFrame *frame, int threshold)
{
    const int width = frame->width;
    const int height = frame->height;
    const int t2 = threshold * threshold;

    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict B = s->buf;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;

#pragma omp simd
        for(int x = 1; x < width - 1; x++) {
            const int i = row + x;

            const int nw = B[i - width - 1];
            const int n  = B[i - width];
            const int ne = B[i - width + 1];
            const int w  = B[i - 1];
            const int e  = B[i + 1];
            const int sw = B[i + width - 1];
            const int s0 = B[i + width];
            const int se = B[i + width + 1];

            const int gx = -nw + ne - (w << 1) + (e << 1) - sw + se;
            const int gy = -nw - (n << 1) - ne + sw + (s0 << 1) + se;
            const int mag2 = gx * gx + gy * gy;

            Y[i] = (mag2 > t2) ? pixel_Y_hi_ : pixel_Y_lo_;
        }
    }
}

static void sobel_gradient_3x3(sobel_t *s, VJFrame *frame, int threshold)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict B = s->buf;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;

#pragma omp simd
        for(int x = 1; x < width - 1; x++) {
            const int i = row + x;

            const int nw = B[i - width - 1];
            const int n  = B[i - width];
            const int ne = B[i - width + 1];
            const int w  = B[i - 1];
            const int e  = B[i + 1];
            const int sw = B[i + width - 1];
            const int s0 = B[i + width];
            const int se = B[i + width + 1];

            const int gx = -nw + ne - (w << 1) + (e << 1) - sw + se;
            const int gy = -nw - (n << 1) - ne + sw + (s0 << 1) + se;
            const uint8_t norm = sobel_norm_grad(gx, gy);

            Y[i] = (norm > threshold) ? norm : pixel_Y_lo_;
        }
    }
}

static void sobel_gradient_wide(sobel_t *s, VJFrame *frame, int threshold)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict B = s->buf;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 2; y < height - 2; y++) {
        const int row = y * width;

#pragma omp simd
        for(int x = 2; x < width - 2; x++) {
            const int i = row + x;

            const int nw = B[i - (2 * width) - 2];
            const int n  = B[i - (2 * width)];
            const int ne = B[i - (2 * width) + 2];

            const int w  = B[i - 2];
            const int e  = B[i + 2];

            const int sw = B[i + (2 * width) - 2];
            const int s0 = B[i + (2 * width)];
            const int se = B[i + (2 * width) + 2];

            const int gx = -nw + ne - (w << 1) + (e << 1) - sw + se;
            const int gy = -nw - (n << 1) - ne + sw + (s0 << 1) + se;
            const uint8_t norm = sobel_norm_grad(gx, gy);

            Y[i] = (norm > threshold) ? norm : pixel_Y_lo_;
        }
    }
}

void sobel_apply(void *ptr, VJFrame *frame, int *args)
{
    sobel_t *s = (sobel_t*) ptr;

    if(!s || !frame || !args || !frame->data[0])
        return;

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    if(len <= 0 || width < 3 || height < 3)
        return;

    const int threshold = sobel_clampi(args[0], 0, 255);
    const int mode = sobel_clampi(args[1], 0, 2);

    uint8_t *restrict Y = frame->data[0];

    veejay_memcpy(s->buf, Y, len);
    veejay_memset(Y, pixel_Y_lo_, len);

    switch(mode) {
        case 0:
            sobel_binary_3x3(s, frame, threshold);
            break;
        case 1:
            sobel_gradient_3x3(s, frame, threshold);
            break;
        case 2:
            if(width >= 5 && height >= 5)
                sobel_gradient_wide(s, frame, threshold);
            else
                sobel_gradient_3x3(s, frame, threshold);
            break;
        default:
            break;
    }

    if(frame->data[1] && frame->data[2]) {
        int uv_len = frame->ssm ? len : frame->uv_len;

        if(uv_len <= 0)
            uv_len = len;

        veejay_memset(frame->data[1], 128, uv_len);
        veejay_memset(frame->data[2], 128, uv_len);
    }
}