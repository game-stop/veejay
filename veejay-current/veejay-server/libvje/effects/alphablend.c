/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nelburg@gmail.com>
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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "alphablend.h"
#include "common.h"

vj_effect *alphablend_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->description = "Alpha: Blend";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->parallel = 1;
	ve->has_user = 0;
	ve->alpha = FLAG_ALPHA_SRC_A | FLAG_ALPHA_SRC_B;
    return ve;
}

static	inline int blend_plane( uint8_t *dst, uint8_t *A, uint8_t *B, uint8_t *aA, size_t size )
{
    size_t i;
	for( i = 0; i < size; i ++ )
	{
		unsigned int op0 = aA[i];
		unsigned int op1 = 0xff - op0;
		dst[i] = (op1 * A[i] + op0 * B[i] ) >> 8;
	}
    return 0;
}

void alphablend_apply( VJFrame *frame, VJFrame *frame2, int width,int height)
{
	size_t uv_len = (frame->ssm ? frame->len : frame->uv_len );
	blend_plane( frame->data[0], frame->data[0], frame2->data[0], frame2->data[3], frame->len );
	blend_plane( frame->data[1], frame->data[1], frame2->data[1], frame2->data[3], uv_len );
	blend_plane( frame->data[2], frame->data[2], frame2->data[2], frame2->data[3], uv_len );
}
