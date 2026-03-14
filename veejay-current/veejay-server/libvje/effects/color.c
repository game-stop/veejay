/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "color.h"

vj_effect *color_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 128;
    ve->defaults[1] = 128;
    ve->defaults[2] = 128;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->sub_format = -1;

    ve->description = "Color Vibrance";

    ve->param_description =
        vje_build_param_list(
            ve->num_params,
            "Vibrance",
            "Blue/Yellow Bias",
            "Red/Green Bias"
        );

    ve->has_user = 0;
    ve->extra_frame = 0;
    ve->parallel = 1;

    return ve;
}

void color_apply(void *ptr, VJFrame *frame, int *args)
{
    const int vibrance = args[0];
    const int bias_u   = args[1];
    const int bias_v   = args[2];

    const int uv_len = (frame->ssm ? frame->len : frame->uv_len);

    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

#pragma omp simd
    for(unsigned int i = 0; i < uv_len; i++)
    {
        int cb = Cb[i] - 128;
        int cr = Cr[i] - 128;

        int mag = abs(cb) + abs(cr);
        int norm = mag >> 1;
        int curve = 255 - ((norm * norm) >> 8);

        int boost = (vibrance * curve) >> 8;

        cb += (cb * boost) >> 8;
        cr += (cr * boost) >> 8;

        cb = (cb * bias_u) >> 8;
        cr = (cr * bias_v) >> 8;

        cb += 128;
        cr += 128;

        if(cb < 0) cb = 0;
        if(cb > 255) cb = 255;
        if(cr < 0) cr = 0;
        if(cr > 255) cr = 255;

        Cb[i] = cb;
        Cr[i] = cr;
    }
}