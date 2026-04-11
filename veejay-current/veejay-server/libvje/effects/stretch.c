/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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
#include "stretch.h"

vj_effect *stretch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1000;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1000;

    ve->defaults[0] = 255;
    ve->defaults[1] = 0;
    ve->defaults[2] = 40;
    ve->defaults[3] = 0;

    ve->description = "Chroma Stretch";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Upper bound", "Lower bound", "Gain factor", "Saturation Amplifier");
    return ve;
}

void stretch_apply(void *ptr, VJFrame *frame, int *args)
{
    const int upper = args[0];
    const int lower = args[1];
    const int gain = args[2];
    const int gain_saturation = args[3];

    const size_t len = (size_t)frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int fixed_gain = (gain * gain_saturation * 256) / 10000;
    if (gain_saturation == 0) {
        fixed_gain = (gain * 256) / 100;
    }

    const int n_threads = vje_advise_num_threads((int)len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (size_t i = 0; i < len; i++) {
        const uint8_t y_val = Y[i];
        
        if (y_val > lower && y_val < upper) {
            int cb = (int)Cb[i] - 128;
            int cr = (int)Cr[i] - 128;

            Cb[i] = (uint8_t)(128 + cb + ((cb * fixed_gain) >> 8));
            Cr[i] = (uint8_t)(128 + cr - ((cr * fixed_gain) >> 8));
        }
    }
}