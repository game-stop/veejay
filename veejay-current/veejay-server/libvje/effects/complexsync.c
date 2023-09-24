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
#include "complexsync.h"

typedef struct {
    uint8_t *c_outofsync_buffer[4];
    int complex_not_completed;
    int position;
} complexsync_t;

vj_effect *complexsync_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = height-1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = (25 * 10);
    ve->defaults[0] = 36;
    ve->defaults[1] = 1;
    ve->defaults[2] = 1;
    ve->description = "Out of Sync -Replace selection-";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;	
    ve->param_description = vje_build_param_list( ve->num_params, "Vertical size", "Mode", "Framespeed" );
    ve->is_transition_ready_func = complexsync_ready;
    return ve;
}

int complexsync_ready(void *ptr, int width, int height)
{
    complexsync_t *c = (complexsync_t*) ptr;
    return !c->complex_not_completed;
}

void *complexsync_malloc(int width, int height)
{
    complexsync_t *c = (complexsync_t*) vj_calloc(sizeof(complexsync_t));
    if(!c) {
        return NULL;
    }

    c->c_outofsync_buffer[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(width*height*3) );
    if(!c->c_outofsync_buffer[0]) {
        free(c);
        return NULL;
    }

    c->c_outofsync_buffer[1] = c->c_outofsync_buffer[0] + (width*height);
    c->c_outofsync_buffer[2] = c->c_outofsync_buffer[1] + (width*height);
  
    vj_frame_clear1( c->c_outofsync_buffer[0] , pixel_Y_lo_ , (width*height));
    vj_frame_clear1( c->c_outofsync_buffer[1] , 128, (width*height*2) );

    return (void*) c;
}

void complexsync_free(void *ptr) {

    complexsync_t *c = (complexsync_t*) ptr;

	free(c->c_outofsync_buffer[0]);
    free(c);
}

void complexsync_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    int val = args[0];
	int auto_inc = args[1];
    int duration = args[2];

    complexsync_t *c = (complexsync_t*) ptr;

    const int len = frame->len;
	const unsigned int width = frame->width;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	int planes[4] = { len, len, len, 0 };
	
    if( auto_inc == 1 ) {
        if( duration == 0 )
            duration = 1;
        c->position += ( val / duration ) + 1;
        if( c->position > frame->height - 2 )
            c->position = 1;
    } else {
        c->position = val;
    }

    int region = width * c->position;

	vj_frame_copy( frame->data, c->c_outofsync_buffer, planes );
	vj_frame_copy( frame2->data, frame->data, planes );

    c->complex_not_completed = (len - region) > 0;

    if( c->complex_not_completed )
	{
		uint8_t *dest[4] = { Y + region, Cb + region, Cr + region, NULL };
		int dst_strides[4] = { len - region, len - region, len - region,0 };

		vj_frame_copy( c->c_outofsync_buffer, dest, dst_strides );
	}
}
