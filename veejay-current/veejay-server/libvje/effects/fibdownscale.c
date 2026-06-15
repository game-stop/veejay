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
 * MERCHANTABILITY or FITNESS FOR more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "fibdownscale.h"

vj_effect *fibdownscale_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 2;
    ve->description = "Fibonacci Downscaler";
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
    ve->defaults[1] = 1;

    ve->limits[0][0] = 0; ve->limits[1][0] = 1;
    ve->limits[0][1] = 1; ve->limits[1][1] = 8;

    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Fib");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0, "Down", "Rectangle");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 1,                  6,                  4,  14, 2800, 7800, 2200, 22
    );

    return ve;
}

static void fibdownscale1_apply(VJFrame *frame)
{
    const int len = frame->len >> 1;
    const int uv_len = (frame->ssm ? frame->len : frame->uv_len) >> 1;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    if(len > 2) {
        for(int i = 2; i < len; i++) {
            const int f1 = i << 1;
            Y[i] = Y[f1];
        }

        veejay_memcpy(Y + len, Y, len);
    }

    if(uv_len > 2) {
        for(int i = 2; i < uv_len; i++) {
            const int f1 = i << 1;
            Cb[i] = Cb[f1];
            Cr[i] = Cr[f1];
        }

        veejay_memcpy(Cb + uv_len, Cb, uv_len);
        veejay_memcpy(Cr + uv_len, Cr, uv_len);
    }
}

static void fibrectangle1_apply(VJFrame *frame)
{
    const int len = frame->len >> 1;
    const int uv_len = (frame->ssm ? frame->len : frame->uv_len) >> 1;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    if(len > 2) {
        for(int i = 2; i < len; i++) {
            const int f1 = (i << 1) - 3;
            Y[i] = Y[f1];
        }
    }

    if(uv_len > 2) {
        for(int i = 2; i < uv_len; i++) {
            const int f1 = (i << 1) - 3;
            Cb[i] = Cb[f1];
            Cr[i] = Cr[f1];
        }
    }
}

void fibdownscale_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int mode = args[0];
    const int repeat = args[1];

    for(int i = 0; i < repeat; i++) {
        if(mode == 0)
            fibdownscale1_apply(frame);
        else
            fibrectangle1_apply(frame);
    }
}
