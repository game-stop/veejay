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
#include "alpha2img.h"

vj_effect *alpha2img_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->description = "Alpha: Show alpha as greyscale";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->parallel = 1;
	ve->has_user = 0;
	ve->alpha = FLAG_ALPHA_SRC_A;
    return ve;
}


void alpha2img_apply( VJFrame *frame)
{
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *a = frame->data[3];
	const int len = frame->len;

	veejay_memcpy(  Y, a, len );
	veejay_memset(  Cb,128, (frame->ssm ? len : frame->uv_len) );
	veejay_memset(  Cr,128, (frame->ssm ? len : frame->uv_len) );
}
