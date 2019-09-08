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
#include "alphaflatten.h"

vj_effect *alphaflatten_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->description = "Alpha: Flatten Image";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 1;
	ve->has_user = 0;
	ve->num_params = 1;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
	ve->defaults = 	(int *) vj_calloc(sizeof(int) * ve->num_params);     /* default values */
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 1;
	ve->defaults[0] = 0;

	ve->alpha = FLAG_ALPHA_SRC_A | FLAG_ALPHA_OUT;
    
	ve->param_description = vje_build_param_list( ve->num_params, "Clear Alpha" );
	
	return ve;
}


void alphaflatten_apply( void *ptr, VJFrame *frame, int *args ) {
    int mode = args[0];

	unsigned int i;
	const int len = frame->len;

	uint8_t *o0 = frame->data[0];
	uint8_t *o1 = frame->data[1];
	uint8_t *o2 = frame->data[2];
	uint8_t *oA = frame->data[3];
	uint8_t *a0 = frame->data[0];
	uint8_t *a1 = frame->data[1];
	uint8_t *a2 = frame->data[2];
	uint8_t *aA = frame->data[3];

	for( i = 0; i < len; i ++ )
	{
		unsigned int op1 = 0xff - aA[i];
		unsigned int op0 = aA[i];
		o0[i] = (op0 * a0[i]) >> 8;
		o1[i] = (op0 * a1[i] + op1 * 128) >> 8;
		o2[i] = (op0 * a2[i] + op1 * 128)>>8;
	}

	if( mode ) {
		veejay_memset( oA, 0, len );
	}
}
