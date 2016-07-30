/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwewlburg@gmail.com>
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
#include <libvjmem/vjmem.h>
#include "mixtoalpha.h"

vj_effect *mixtoalpha_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->description = "Alpha: Set from Mixing source";
    ve->sub_format = -1;
    ve->extra_frame = 1;
	ve->parallel = 1;
	ve->has_user = 0;

	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);     /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
	ve->limits[0][1] = 0;
	ve->limits[1][1] =1;
    ve->defaults[0] = 0;
	ve->defaults[1] = !yuv_use_auto_ccir_jpeg();
    ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Scale to full range" );

	ve->alpha = FLAG_ALPHA_OUT;

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0],0, "Copy Luminance from B", "Copy Alpha from B"	);

    return ve;
}

void mixtoalpha_apply( VJFrame *frame, VJFrame *frame2, int mode, int scale)
{
	const unsigned int len = frame->len;
	uint8_t *a = frame->data[3];
	const uint8_t *Y = frame2->data[0];
	uint8_t __lookup_table[256];
	__init_lookup_table( __lookup_table,256, 16.0f, 235.0f, 0, 255 ); 

	const uint8_t *T = (const uint8_t*) __lookup_table;
	
	if( mode == 0 ) {
		veejay_memcpy(a, Y, len );
	}
	else if (mode == 1) {
		veejay_memcpy(a, frame2->data[3], len );
	}

	if( scale ) {
		int i;
		for( i = 0; i < len; i ++ )
		{
			a[i] = T[ a[i] ];
		}
	}
}

