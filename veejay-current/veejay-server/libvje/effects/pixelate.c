/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include "pixelate.h"

#define PIXELATE_PARAMS 1

#define P_PIXEL_SIZE 0

static inline int pixelate_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *pixelate_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = PIXELATE_PARAMS;
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

    const int max_size = width < height ? width : height;

    ve->limits[0][P_PIXEL_SIZE] = 1;
    ve->limits[1][P_PIXEL_SIZE] = max_size;
    ve->defaults[P_PIXEL_SIZE] = max_size < 8 ? max_size : 8;

    ve->description = "Pixelate";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(ve->num_params, "Pixel Size");

    int pixel_hi = max_size;

    if(pixel_hi > 40)
        pixel_hi = 40;
    if(pixel_hi < 2)
        pixel_hi = max_size;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, pixel_hi, 92, 100, 6, 420, 0, 1, 60, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static void pixelate_plane(uint8_t *restrict plane,
                           int width,
                           int height,
                           int block_w,
                           int block_h,
                           int n_threads)
{
    const int blocks_x = (width + block_w - 1) / block_w;
    const int blocks_y = (height + block_h - 1) / block_h;
    const int blocks = blocks_x * blocks_y;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int b = 0; b < blocks; b++) {
        const int by = (b / blocks_x) * block_h;
        const int bx = (b - ((b / blocks_x) * blocks_x)) * block_w;
        const int y_end = by + block_h < height ? by + block_h : height;
        const int x_end = bx + block_w < width ? bx + block_w : width;
        const int area = (y_end - by) * (x_end - bx);
        int total = 0;

        for(int y = by; y < y_end; y++) {
            const int row = y * width;

            for(int x = bx; x < x_end; x++)
                total += plane[row + x];
        }

        const uint8_t avg = (uint8_t)((total + (area >> 1)) / area);

        for(int y = by; y < y_end; y++) {
            uint8_t *restrict dst = plane + y * width + bx;

            for(int x = bx; x < x_end; x++)
                *dst++ = avg;
        }
    }
}

void pixelate_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int pixel_size = pixelate_clampi(args[P_PIXEL_SIZE], 1, width < height ? width : height);
    const int n_threads = vje_advise_num_threads(frame->len);

    if(pixel_size <= 1)
        return;

    pixelate_plane(frame->data[0], width, height, pixel_size, pixel_size, n_threads);

    if(frame->ssm) {
        pixelate_plane(frame->data[1], width, height, pixel_size, pixel_size, n_threads);
        pixelate_plane(frame->data[2], width, height, pixel_size, pixel_size, n_threads);
    }
    else {
        int uv_block_w = pixel_size >> frame->shift_h;
        int uv_block_h = pixel_size >> frame->shift_v;

        if(uv_block_w < 1)
            uv_block_w = 1;
        if(uv_block_h < 1)
            uv_block_h = 1;

        if(uv_block_w > 1 || uv_block_h > 1) {
            const int uv_threads = vje_advise_num_threads(frame->uv_len);

            pixelate_plane(frame->data[1], frame->uv_width, frame->uv_height, uv_block_w, uv_block_h, uv_threads);
            pixelate_plane(frame->data[2], frame->uv_width, frame->uv_height, uv_block_w, uv_block_h, uv_threads);
        }
    }
}
