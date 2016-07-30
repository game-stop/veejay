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
#include <libvjmem/vjmem.h>
#include "travelmatte.h"

vj_effect *travelmatte_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->description = "Alpha: Travel Matte";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->parallel = 1;
	ve->has_user = 0;

	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);     /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->defaults[0] = 1;
    ve->param_description = vje_build_param_list( ve->num_params, "Matte Travel Luma" );
    return ve;
}


void travelmatte_apply( VJFrame *frame, VJFrame *frame2, int mode)
{
	const unsigned int len = frame->len;

	uint8_t *a0 = frame->data[0];
	uint8_t *a1 = frame->data[1];
	uint8_t *a2 = frame->data[2];
	uint8_t *aA = frame->data[3];

	uint8_t *o0 = frame->data[0];
	uint8_t *o1 = frame->data[1];
	uint8_t *o2 = frame->data[2];

	uint8_t *b0 = frame2->data[0];
	uint8_t *b1 = frame2->data[1];
	uint8_t *b2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];

	unsigned int i;

	if( mode == 0 ) {
		for( i = 0; i < len; i ++ ) 
		{
			if( aA[i] == 0 )
				continue;

			if( aA[i] == 0xff ) {
				o0[i] = b0[i];
				o1[i] = b1[i];
				o2[i] = b2[i];
			}
			else {
				o0[i] = ALPHA_BLEND( aA[i], a0[i], b0[i] );
				o1[i] = ALPHA_BLEND( aA[i], a1[i], b1[i] );
				o2[i] = ALPHA_BLEND( aA[i], a2[i], b2[i] );
			}
		}
	}
	else
	{
		for( i = 0; i < len; i ++ )
		{
			if( aB[i] == 0 ) 
				continue;

			if( aB[i] == 0xff ) {
				o0[i] = b0[i];
				o1[i] = b1[i];
				o2[i] = b2[i];
			}
			else {
				o0[i] = ALPHA_BLEND( aB[i], a0[i], b0[i] );
				o1[i] = ALPHA_BLEND( aB[i], a1[i], b1[i] );
				o2[i] = ALPHA_BLEND( aB[i], a2[i], b2[i] );
			}
		}
	}
}
