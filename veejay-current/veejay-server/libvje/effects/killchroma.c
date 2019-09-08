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
#include "killchroma.h"

vj_effect *killchroma_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 1;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->sub_format = -1;
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 2;
	ve->defaults[0] = 0;
	ve->has_user = 0;
	ve->description = "Filter out chroma channels";
	ve->extra_frame = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Mode" );
	ve->parallel = 1;

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
	                          "All Channels", "Chroma Blue", "Chroma Red" );

	return ve;
}

void killchroma_apply(void *ptr, VJFrame *frame, int *args ) {
    int n = args[0];

	const int len = frame->len;
	if(n==0)
	{
		veejay_memset( frame->data[1], 128, (frame->ssm ? len : frame->uv_len) );
		veejay_memset( frame->data[2], 128, (frame->ssm ? len : frame->uv_len) );
	}
	else
	{
		veejay_memset( frame->data[n], 128, (frame->ssm ? len : frame->uv_len ) );
	}
}
