/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "rainbowshift.h"

vj_effect *rainbowshift_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
	ve->limits[0][1] = 0;
    ve->limits[1][1] = 10;
    ve->defaults[0] = 1;
	ve->defaults[1] = 1;
    ve->description = "Rainbow Shift";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Amplitude", "Frequency" );
    return ve;
}


void rainbowshift_apply(void *ptr, VJFrame *frame, int *args) {
    const int shift_amplitude = args[0];
    const int shift_frequency = args[1];

    const size_t len = (size_t)frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int n_threads = vje_advise_num_threads((int)len);
    const double freq_factor = (2.0 * M_PI * shift_frequency) / (double)len;

#pragma omp parallel num_threads(n_threads)
    {
#pragma omp for schedule(static)
        for (size_t i = 0; i < len; i++) {
            int wave_shift = (int)(shift_amplitude * a_sin(freq_factor * i));

            Cb[i] = (uint8_t)((Cb[i] + wave_shift) & 255);
            Cr[i] = (uint8_t)((Cr[i] - wave_shift) & 255);
            Y[i]  = (uint8_t)((Y[i] + wave_shift) & 255);
        }
    }
}
