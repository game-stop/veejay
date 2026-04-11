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
#include <veejaycore/vjmem.h>
#include "rawman.h"

vj_effect *rawman_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;
    ve->defaults[1] = 15;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 4;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 255;
    ve->sub_format = -1;
    ve->description = "Raw Data Manipulation";
	ve->has_user = 0;
    ve->extra_frame = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Value");

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0],0,
		"Additive", "Subtractive","Multiply","Divide","Lighten","Hardlight");

    return ve;
}

void rawman_apply(void *ptr, VJFrame *frame, int *args)
{
    const int mode = args[0];
    const unsigned int YY = (unsigned int)args[1];
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];

    const int n_threads = vje_advise_num_threads(len);

    switch (mode) {
        case 1:
#pragma omp parallel for num_threads(n_threads) schedule(static)
            for (int i = 0; i < len; i++) {
                Y[i] = (Y[i] < YY) ? (uint8_t)(Y[i] * 2) : (uint8_t)(Y[i] / 2);
            }
            break;

        case 2:
#pragma omp parallel for num_threads(n_threads) schedule(static)
            for (int i = 0; i < len; i++) {
                Y[i] -= (uint8_t)YY;
            }
            break;

        case 3:
#pragma omp parallel for num_threads(n_threads) schedule(static)
            for (int i = 0; i < len; i++) {
                Y[i] = (Y[i] < YY) ? (uint8_t)(Y[i] / 2) : (uint8_t)(Y[i] * 2);
            }
            break;

        case 4:
#pragma omp parallel for num_threads(n_threads) schedule(static)
            for (int i = 0; i < len; i++) {
                Y[i] = (Y[i] < YY) ? (uint8_t)(Y[i] + YY) : (uint8_t)(Y[i] - YY);
            }
            break;

        default:
#pragma omp parallel for num_threads(n_threads) schedule(static)
            for (int i = 0; i < len; i++) {
                Y[i] += (uint8_t)YY;
            }
            break;
    }
}