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
    return ve;
}


void alphaflatten_apply( VJFrame *frame, int width, int height)
{
    int i;
    int len = frame->len;
    int uv_len = frame->uv_len;

    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
	uint8_t *a = frame->data[3];

	uint8_t *o0 = frame->data[0];
	uint8_t *o1 = frame->data[1];
	uint8_t *o2 = frame->data[2];
	uint8_t *dA = frame->data[3];
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

		dA = 0;	
	}
}
