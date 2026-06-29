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
#include "flip.h"

vj_effect *flip_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[0] = 0;
    ve->defaults[1] = 0;

    ve->limits[0][0] = 0; ve->limits[1][0] = 1;
    ve->limits[0][1] = 0; ve->limits[1][1] = 1;

    ve->description = "Flip Frame";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Horizontal", "Vertical");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0, "Normal", "Flip Horizontal");
    vje_build_value_hint_list(ve->hints, ve->limits[1][1], 1, "Normal", "Flip Vertical");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000
    );

    return ve;
}

static inline void flip_swap_u8(uint8_t *restrict p, int a, int b)
{
    uint8_t t = p[a];
    p[a] = p[b];
    p[b] = t;
}

static void flip_horizontal_yuv444(VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->height;
    const int half_w = width >> 1;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp for schedule(static)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int last = row + width - 1;

        for(int x = 0; x < half_w; x++) {
            const int a = row + x;
            const int b = last - x;

            flip_swap_u8(Y, a, b);
            flip_swap_u8(Cb, a, b);
            flip_swap_u8(Cr, a, b);
        }
    }
}

static void flip_vertical_yuv444(VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->height;
    const int half_h = height >> 1;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp for schedule(static)
    for(int y = 0; y < half_h; y++) {
        const int row_a = y * width;
        const int row_b = (height - 1 - y) * width;

        for(int x = 0; x < width; x++) {
            const int a = row_a + x;
            const int b = row_b + x;

            flip_swap_u8(Y, a, b);
            flip_swap_u8(Cb, a, b);
            flip_swap_u8(Cr, a, b);
        }
    }
}

static void flip_both_yuv444(VJFrame *frame)
{
    const int len = frame->len;
    const int half = len >> 1;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp for schedule(static)
    for(int i = 0; i < half; i++) {
        const int j = len - 1 - i;

        flip_swap_u8(Y, i, j);
        flip_swap_u8(Cb, i, j);
        flip_swap_u8(Cr, i, j);
    }
}

void flip_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int horizontal = args[0];
    const int vertical = args[1];
    const int n_threads = vje_advise_num_threads(frame->len);

    if(horizontal == 0 && vertical == 0)
        return;

#pragma omp parallel num_threads(n_threads)
    {
        if(horizontal && vertical)
            flip_both_yuv444(frame);
        else if(horizontal)
            flip_horizontal_yuv444(frame);
        else
            flip_vertical_yuv444(frame);
    }
}
