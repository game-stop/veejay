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
#include "fadecolor.h"
#include <veejaycore/yuvconv.h>

typedef struct {
    int last_mode;
    int last_color;
} fadecolor_t;

vj_effect *fadecolor_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);



    ve->limits[0][0] = 0; ve->limits[1][0] = 255; ve->defaults[0] = 128;
    ve->limits[0][1] = 0; ve->limits[1][1] = 7;   ve->defaults[1] = 0;
    ve->limits[0][2] = 0; ve->limits[1][2] = 1;   ve->defaults[2] = 0;

    ve->description = "Fade to Color";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Color", "Mode");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][1], 1, "Black", "Red", "Green", "Blue", "Cyan", "Magenta", "Yellow", "White");
    vje_build_value_hint_list(ve->hints, ve->limits[1][2], 2, "Normal", "Inverted");

    return ve;
}

void *fadecolor_malloc(int w, int h)
{
    fadecolor_t *f = (fadecolor_t *) vj_calloc(sizeof(fadecolor_t));
    if(!f)
        return NULL;

    f->last_mode = -1;
    f->last_color = -1;

    return f;
}

void fadecolor_free(void *ptr)
{
    fadecolor_t *f = (fadecolor_t *) ptr;
    if(f)
        free(f);
}

void fadecolor_apply(void *ptr, VJFrame *frame, int *args)
{
    fadecolor_t *f = (fadecolor_t *) ptr;
    const int op1 = args[0];
    const int color = args[1];
    const int mode = args[2];

    if(op1 <= 0)
        return;

    uint8_t colorY = bl_pix_get_color_y(color);
    uint8_t colorCb = bl_pix_get_color_cb(color);
    uint8_t colorCr = bl_pix_get_color_cr(color);

    const int len = frame->len;
    const int uv_len = frame->uv_len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int inv_op = 255 - op1;

    if(mode == 0) {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            Y[i] = (Y[i] * inv_op + colorY * op1) >> 8;
        }
        #pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            Cb[i] = (Cb[i] * inv_op + colorCb * op1) >> 8;
            Cr[i] = (Cr[i] * inv_op + colorCr * op1) >> 8;
        }
    } else {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            Y[i] = 255 - (( (255 - Y[i]) * inv_op + (255 - colorY) * op1) >> 8);
        }
        #pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            Cb[i] = 128 + (( (Cb[i] - 128) * inv_op + (colorCb - 128) * op1) >> 8);
            Cr[i] = 128 + (( (Cr[i] - 128) * inv_op + (colorCr - 128) * op1) >> 8);
        }
    }
}