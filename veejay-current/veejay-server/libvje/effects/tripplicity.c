/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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
/*
	This effects overlays 2 images , 
	It allows the user to set the transparency per channel.
	Result will vary over different color spaces.

 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "tripplicity.h"

vj_effect *tripplicity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;	
    ve->limits[1][0] = 255; // opacity Y
    ve->limits[0][1] = 0;   
    ve->limits[1][1] = 255; // opacity Cb
    ve->limits[0][2] = 0;	
    ve->limits[1][2] = 255; // opacity Cr
    ve->defaults[0] = 150;
    ve->defaults[1] = 150;
    ve->defaults[2] = 150;

    ve->description = "Normal Overlay (per Channel)";
    ve->sub_format = -1;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Opacity Y", "Opacity Cb", "Opacity Cr" );

    return ve;
}

void tripplicity_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    const int opacityY  = args[0];
    const int opacityCb = args[1];
    const int opacityCr = args[2];

    const size_t len = (size_t)frame->len;
    const size_t uv_len = (frame->ssm ? len : (size_t)frame->uv_len);

    uint8_t *restrict Y1  = frame->data[0];
    uint8_t *restrict Cb1 = frame->data[1];
    uint8_t *restrict Cr1 = frame->data[2];

    const uint8_t *restrict Y2  = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    const int opY1  = (opacityY > 255) ? 255 : opacityY;
    const int opY0  = 255 - opY1;
    const int opCb1 = (opacityCb > 255) ? 255 : opacityCb;
    const int opCb0 = 255 - opCb1;
    const int opCr1 = (opacityCr > 255) ? 255 : opacityCr;
    const int opCr0 = 255 - opCr1;

    const int n_threads = vje_advise_num_threads((int)len);

#pragma omp parallel num_threads(n_threads)
    {
#pragma omp for schedule(static)
        for (int i = 0; i < (int)len; i++) {
            Y1[i] = (uint8_t)((opY0 * Y1[i] + opY1 * Y2[i]) >> 8);
        }

#pragma omp for schedule(static)
        for (int i = 0; i < (int)uv_len; i++) {
            Cb1[i] = (uint8_t)((opCb0 * Cb1[i] + opCb1 * Cb2[i]) >> 8);
            Cr1[i] = (uint8_t)((opCr0 * Cr1[i] + opCr1 * Cr2[i]) >> 8);
        }
    }
}
