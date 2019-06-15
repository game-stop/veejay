/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
#include "alphadampen.h"

vj_effect *alphadampen_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 64;
    ve->description = "Alpha: Dampen";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Dampening value" );
	ve->alpha = FLAG_ALPHA_OUT;
	return ve;
}

void alphadampen_apply( VJFrame *frame, int b1)
{
	size_t i;
	const int len = frame->len;
	uint8_t tmp;
	uint8_t *A = frame->data[3];

	const int base = (const int) b1;
	for( i = 0 ; i < len ; i ++ )
	{
		tmp = A[i];
		A[i] = (tmp / base) * base; // loose fractional part
	}
}
