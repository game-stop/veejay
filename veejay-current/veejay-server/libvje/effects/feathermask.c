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
#include <stdlib.h>
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "feathermask.h"
#include "common.h"

vj_effect *feathermask_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->description = "Alpha: Feather Mask";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->parallel = 0;
	ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A;
    return ve;
}

static uint8_t *mask = NULL;
int feathermask_malloc(int width, int height)
{
    mask = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(width*height));
    if(!mask)
		return 0;
	return 1;
}
void feathermask_free()
{
	if(mask) {
		free(mask);
		mask = NULL;
	}
}

static void feathermask1_apply( VJFrame *frame, uint8_t *alpha, int width, int height)
{
    int r, c;
    int len = (width * height) - width;
 	uint8_t *aA = frame->data[3];
	
	for(r=width; r < len; r+=width) {
		for(c=1; c < (width-1); c++) {
			if( alpha[r+c] != 0xff ) {
				aA[r+c] = (
						alpha[ r + c - width ] +
						alpha[ r + c + width ] +
						alpha[ r + c - 1 ] +
						alpha[ r + c + 1 ] +
						alpha[ r + c ] 
							) / 5;

			}	
		}
	}
}

void feathermask_apply(VJFrame *frame, int width, int height)
{
	vj_frame_copy1( frame->data[3],mask, width * height );
	feathermask1_apply(frame, mask, width, height);
}

