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
#include "posterize.h"

vj_effect *posterize_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4;
    ve->defaults[1] = 16;
    ve->defaults[2] = 235;
	
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 256;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 256;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 256;

    ve->description = "Posterize (Threshold Range)";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Posterize", "Min Threshold", "Max Threshold");
    return ve;	
}

static void _posterize_y_simple(uint8_t *restrict src[3],
                                const int len,
                                const int value,
                                const int threshold_min,
                                const int threshold_max)
{
    uint8_t *restrict y = src[0];

    const int factor = 256 / (value > 0 ? value : 1);
    const uint8_t lo = pixel_Y_lo_;
    const uint8_t hi = pixel_Y_hi_;
    
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < len; ++i)
    {
        int Y_val = y[i];
        
        Y_val = (Y_val / factor) * factor;

        if (Y_val < threshold_min)
            y[i] = lo;
        else if (Y_val > threshold_max)
            y[i] = hi;
        else
            y[i] = (uint8_t)Y_val;
    }
}

void posterize_apply(void *ptr, VJFrame *frame, int *args)
{
    _posterize_y_simple(frame->data, frame->len, args[0], args[1], args[2]);
}