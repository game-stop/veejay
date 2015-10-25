/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <elburg@hio.hen.nl>
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
#include "toalpha.h"

vj_effect *toalpha_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->description = "Alpha: New from Image";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->parallel = 1;
	ve->has_user = 0;
    return ve;
}


void toalpha_apply( VJFrame *frame, int width, int height)
{
    int len = frame->len;

    uint8_t *Y = frame->data[0];
	uint8_t *a = frame->data[3];

	veejay_memcpy( a, Y, len );
}
