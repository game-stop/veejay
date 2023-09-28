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

#include "common.h"
#include <veejaycore/vjmem.h>
#include <libsubsample/subsample.h>
#include "bgpush.h"

typedef struct {
    uint8_t *frame_data;
    uint8_t *frame_ptr[4];
} bgpush_t;

vj_effect *bgpush_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->description = "Background take-frame";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->global = 1;
    ve->static_bg = 1;
    return ve;
}

void *bgpush_malloc(int w, int h)
{
    bgpush_t *b = (bgpush_t*) vj_calloc(sizeof(bgpush_t));
    if(!b) {
        free(b);
        return NULL;
    }
    
	b->frame_data =  (uint8_t*) vj_malloc( (w*h*4) );
	b->frame_ptr[0] = b->frame_data;
	b->frame_ptr[1] = b->frame_ptr[0] + (w*h);
	b->frame_ptr[2] = b->frame_ptr[1] + (w*h);
	b->frame_ptr[3] = b->frame_ptr[2] + (w*h);

	veejay_memset( b->frame_ptr[0], 0, w * h );
	veejay_memset( b->frame_ptr[1], 128, w * h );
	veejay_memset( b->frame_ptr[2], 128, w * h );
	veejay_memset( b->frame_ptr[3], 0, w * h );
	
	return (void*) b;
}

void bgpush_free(void *ptr)
{
    bgpush_t *b = (bgpush_t*) ptr;

	free(b->frame_data);
    free(b);
}

int bgpush_prepare(void *ptr, VJFrame *frame )
{
	const int uv_len = (frame->ssm ? frame->len : frame->uv_len );
	bgpush_t *b = (bgpush_t*) ptr;

	veejay_memcpy( b->frame_ptr[0], frame->data[0], frame->len );
	veejay_memcpy( b->frame_ptr[1], frame->data[1], uv_len );
	veejay_memcpy( b->frame_ptr[2], frame->data[2], uv_len );

	if( frame->ssm == 0 ) {
		chroma_supersample( SSM_422_444, frame, b->frame_ptr );
	}

	return 1;
}

//FIXME: issue #78 , background frame in 4:4:4
//alpha channel should be cleared according to info->settings->alpha_value
void bgpush_apply( void *ptr, VJFrame *frame, int *args )
{
}

uint8_t *bgpush_get_bg_frame( void *ptr, unsigned int plane )
{
    bgpush_t *b = (bgpush_t*) ptr;
	if( b->frame_data == NULL )
		return NULL;
	return b->frame_ptr[plane];
}
