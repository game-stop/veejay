/* 
 * Linux VeeJay
 *
 * Copyright(C)2016 Niels Elburg <nwelburg@gmail.com>
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
#include "meanfilter.h"

static uint8_t *mean = NULL;

vj_effect *meanfilter_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->description = "Mean Filter (3x3)";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    return ve;
}

int meanfilter_malloc(int w, int h)
{
	if( mean == NULL ) {
		mean =  (uint8_t*) vj_calloc( RUP8(w*h) );
		if( mean == NULL )
			return 0;
	}

	return 1;
}

void meanfilter_free()
{
	if( mean ) {
		free(mean);
		mean = NULL;
	}
}


void meanfilter_apply( VJFrame *frame )
{
	vje_mean_filter( frame->data[0], mean, frame->width, frame->height );

	veejay_memcpy( frame->data[0], mean, frame->len );
}
