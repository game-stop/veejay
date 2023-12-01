
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
#include "solarize.h"

vj_effect *solarize_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 16;

    ve->limits[0][0] = 1;   /* threshold */
    ve->limits[1][0] = 255;
    ve->description = "Solarize";
    ve->parallel = 1;
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Threshold");
    return ve;
}

void solarize_apply( void *ptr, VJFrame *frame, int *args )
{
    unsigned int i;
    const int len= frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb= frame->data[1];
    uint8_t *Cr= frame->data[2];

    int threshold = args[0];
    uint8_t threshold_mask = -(Y[0] >= threshold);
    for (i = 0; i < len; i++) {
        uint8_t val = Y[i];
        uint8_t mask = -(val >= threshold);

        Y[i] = (val & mask) | ((0xff - val) & ~mask);
        Cb[i] = (Cb[i] & mask) | ((0xff - Cb[i]) & ~mask);
        Cr[i] = (Cr[i] & mask) | ((0xff - Cr[i]) & ~mask);

        threshold_mask = (threshold_mask | mask) & (-(val == threshold));
    }

}
