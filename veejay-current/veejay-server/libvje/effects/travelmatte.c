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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "common.h"
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
    ve->defaults[0] = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Matte Travel Luma" );
    return ve;
}


void travelmatte_apply( VJFrame *frame, VJFrame *frame2, int width, int height, int mode)
{
    int i;
    int len = frame->len;
    int uv_len = frame->uv_len;

    uint8_t *a0 = frame->data[0];
    uint8_t *a1 = frame->data[1];
    uint8_t *a2 = frame->data[2];

	uint8_t *o0 = frame->data[0];
	uint8_t *o1 = frame->data[1];
	uint8_t *o2 = frame->data[2];
	uint8_t *dA = frame->data[3];

	uint8_t *b0 = frame2->data[0];
	uint8_t *b1 = frame2->data[1];
	uint8_t *b2 = frame2->data[2];
	uint8_t *bA = frame2->data[3];

	if( mode == 0 ) {
		for( i = 0; i < len; i ++ ) 
		{
			if( bA[i] == 0 )
				continue;

			unsigned int op1 = bA[i];
			unsigned int op0 = 0xff - bA[i];
			o0[i] = (op0 * a0[i] + op1 * b0[i]) >> 8;
			o1[i] = (op0 * a1[i] + op1 * b1[i]) >> 8;
			o2[i] = (op0 * a2[i] + op1 * b2[i])>>8;
		}
	}
	else
	{
		for( i = 0; i < len; i ++ )
		{
			if( b0[i] == 0 ) /* if there is no alpha, we can take luma channel instead */
				continue;

			unsigned int op1 = b0[i];
			unsigned int op0 = 0xff - b0[i];
			o0[i] = (op0 * a0[i] + op1 * b0[i]) >> 8;
			o1[i] = (op0 * a1[i] + op1 * b1[i]) >> 8;
			o2[i] = (op0 * a2[i] + op1 * b2[i])>>8;
		}
	}
}
