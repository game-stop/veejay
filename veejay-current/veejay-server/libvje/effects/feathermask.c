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
#include "feathermask.h"

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

typedef struct {
    uint8_t *mask;
} feathermask_t;

void *feathermask_malloc(int width, int height)
{
    feathermask_t *f = (feathermask_t*) vj_calloc(sizeof(feathermask_t));
    if(!f) {
        return NULL;
    }
    f->mask = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(width*height));
    if(!f->mask) {
		free(f);
        return NULL;
    }
	return (void*) f;
}
void feathermask_free(void *ptr)
{
    feathermask_t *f = (feathermask_t*) ptr;
    free(f->mask);
    free(f);
}

static void feathermask1_apply( VJFrame *frame, uint8_t *alpha, unsigned int width, unsigned int height)
{
	int r, c;
	const int len = frame->len - width;
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

void feathermask_apply(void *ptr, VJFrame *frame, int *args)
{
    feathermask_t *f = (feathermask_t*) ptr;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	vj_frame_copy1( frame->data[3],f->mask, len );
	feathermask1_apply(frame, f->mask, width, height);
}

