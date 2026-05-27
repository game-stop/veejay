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
#include "pixelate.h"

vj_effect *pixelate_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = (width < height ? width : height);
    ve->defaults[0] = 8;

    ve->description = "Pixelate";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Pixel Size"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 2, 48, 6, 22, 2200, 5200, 1800, 25 /* Pixel Size */
    );

    return ve;
}

static inline int pixelate_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static void pixelate_plane(uint8_t *plane, int width, int height, int block_w, int block_h)
{
    if(!plane || width <= 0 || height <= 0 || block_w <= 0 || block_h <= 0)
        return;

    for(int by = 0; by < height; by += block_h) {
        const int y_end = (by + block_h < height) ? (by + block_h) : height;

        for(int bx = 0; bx < width; bx += block_w) {
            const int x_end = (bx + block_w < width) ? (bx + block_w) : width;

            int total = 0;
            int count = 0;

            for(int y = by; y < y_end; y++) {
                const int row = y * width;

                for(int x = bx; x < x_end; x++) {
                    total += plane[row + x];
                    count++;
                }
            }

            if(count <= 0)
                continue;

            const uint8_t avg = (uint8_t)(total / count);

            for(int y = by; y < y_end; y++) {
                const int row = y * width;

                for(int x = bx; x < x_end; x++) {
                    plane[row + x] = avg;
                }
            }
        }
    }
}

void pixelate_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    if(!frame || !args)
        return;

    const int width = frame->width;
    const int height = frame->height;

    if(width <= 0 || height <= 0)
        return;

    int pixel_size = pixelate_clampi(args[0], 1, (width < height ? width : height));

    uint8_t *dstY = frame->data[0];
    uint8_t *dstU = frame->data[1];
    uint8_t *dstV = frame->data[2];

    pixelate_plane(dstY, width, height, pixel_size, pixel_size);

    if(frame->ssm) {
        pixelate_plane(dstU, width, height, pixel_size, pixel_size);
        pixelate_plane(dstV, width, height, pixel_size, pixel_size);
    } else {
        const int uv_w = frame->uv_width;
        const int uv_h = frame->uv_height;

        int uv_block_w = pixel_size >> frame->shift_h;
        int uv_block_h = pixel_size >> frame->shift_v;

        if(uv_block_w < 1)
            uv_block_w = 1;
        if(uv_block_h < 1)
            uv_block_h = 1;

        pixelate_plane(dstU, uv_w, uv_h, uv_block_w, uv_block_h);
        pixelate_plane(dstV, uv_w, uv_h, uv_block_w, uv_block_h);
    }
}