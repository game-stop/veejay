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
#include "crosspixel.h"

typedef struct {
    uint8_t *cross_pixels[3];
    int n_threads;
} crosspixel_t;

vj_effect *crosspixel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 1;  ve->defaults[0] = 0;
    ve->limits[0][1] = 1; ve->limits[1][1] = 40; ve->defaults[1] = 2;

    ve->description = "Pixel Raster";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Size");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0, "Black", "White");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                                    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 1,                  24,                 4,  16, 3000, 8200, 2200, 24
    );

    return ve;
}

void *crosspixel_malloc(int w, int h)
{
    crosspixel_t *c = (crosspixel_t*) vj_calloc(sizeof(crosspixel_t));

    if(!c)
        return NULL;

    const int len = w * h;

    if(len <= 0) {
        free(c);
        return NULL;
    }

    c->cross_pixels[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * len * 3);

    if(!c->cross_pixels[0]) {
        free(c);
        return NULL;
    }

    c->cross_pixels[1] = c->cross_pixels[0] + len;
    c->cross_pixels[2] = c->cross_pixels[1] + len;
    c->n_threads = vje_advise_num_threads(len);

    return c;
}

void crosspixel_free(void *ptr)
{
    crosspixel_t *c = (crosspixel_t*) ptr;

    if(!c)
        return;

    if(c->cross_pixels[0])
        free(c->cross_pixels[0]);

    free(c);
}

static void crosspixel_copy_raster_rows(uint8_t *restrict dst,
                                        const uint8_t *restrict src,
                                        int width,
                                        int height,
                                        unsigned int step,
                                        int n_threads)
{
    if(!dst || !src || width <= 0 || height <= 0 || step == 0)
        return;

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < height; y++)
    {
        if(((unsigned int)y % step) == 1u)
        {
            veejay_memcpy(dst + ((size_t)y * (size_t)width),
                          src + ((size_t)y * (size_t)width),
                          width);
        }
    }
}

void crosspixel_apply(void *ptr, VJFrame *frame, int *args)
{
    crosspixel_t *c = (crosspixel_t*) ptr;

    const int mode = args[0];
    const int size = args[1];
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int uv_width = frame->uv_width;
    const int uv_height = frame->uv_height;
    const int uv_len = frame->uv_len;

    const unsigned int step_y = (unsigned int)size * 2u;
    unsigned int step_uv = step_y >> frame->shift_h;

    if(step_uv == 0)
        step_uv = 1;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int strides[4] = { len, uv_len, uv_len, 0 };

    vj_frame_copy(frame->data, c->cross_pixels, strides);

    vj_frame_clear1(Y, mode == 0 ? pixel_Y_lo_ : pixel_Y_hi_, len);
    vj_frame_clear1(Cb, 128, uv_len);
    vj_frame_clear1(Cr, 128, uv_len);

    crosspixel_copy_raster_rows(Y, c->cross_pixels[0], width, height, step_y, c->n_threads);
    crosspixel_copy_raster_rows(Cb, c->cross_pixels[1], uv_width, uv_height, step_uv, c->n_threads);
    crosspixel_copy_raster_rows(Cr, c->cross_pixels[2], uv_width, uv_height, step_uv, c->n_threads);
}
