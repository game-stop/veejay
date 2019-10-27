/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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
#include "buffer.h"

/* very simple effect that stores frames in a buffer and plays them once a certain number of frames are stored,
 * effectively introducing a time delay
 * the frames played are freed after use, memory usage is (frame_delay * frame_size).
 * MAX_FRAMES is based on 25 fps * 60 seconds
 */

#define MAX_FRAMES 1500

vj_effect *buffer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = MAX_FRAMES;
    ve->defaults[0] = 50;
    ve->description = "Frame Delay";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->parallel = 0;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Frame Delay" );
    return ve;
}

typedef struct {
    VJFrame **frames;
    int last_size;
    int write_pos;
    int read_pos;
    int ready;
    int length;
} buffer_t;


void *buffer_malloc(int w, int h)
{
    buffer_t *b = (buffer_t*) vj_calloc(sizeof(buffer_t));
    if(!b) {
        return NULL;
    }

    b->frames = (VJFrame**) vj_calloc(sizeof(VJFrame*) * MAX_FRAMES );
    if(!b->frames) {
        free(b);
        return NULL;
    }
    return (void*) b;
}

void buffer_free( void *ptr )
{
    buffer_t* b = (buffer_t*) ptr;
    int i;
    for( i = 0; i < MAX_FRAMES; i++ ) {
        if(b->frames[i]) {
            free(b->frames[i]->data[0]);
            free(b->frames[i]);
            b->frames[i] = NULL;
        }
    }
    free(b->frames);
    free(b);
}

static int put_frame( buffer_t *b, VJFrame *frame )
{
    VJFrame *dst = (VJFrame*) vj_calloc( sizeof(VJFrame) );
    if(!dst) {
        return 0;
    }
    
    dst->data[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * (frame->len + frame->uv_len + frame->uv_len + frame->len) );
    if(!dst->data[0]) {
        free(dst);
        return 0;
    }
    dst->data[1] = dst->data[0] + frame->len;
    dst->data[2] = dst->data[1] + frame->uv_len;
    dst->data[3] = dst->data[2] + frame->uv_len;

    veejay_memcpy( dst->data[0], frame->data[0], frame->len );
    veejay_memcpy( dst->data[1], frame->data[1], frame->uv_len );
    veejay_memcpy( dst->data[2], frame->data[2], frame->uv_len );
    veejay_memcpy( dst->data[3], frame->data[3], frame->len );

    dst->stride[3] = frame->stride[3];
    dst->len = frame->len;
    dst->uv_len = frame->uv_len;
    dst->ssm = frame->ssm;

    b->frames[ b->write_pos ] = dst;
    
    if(b->write_pos == (b->length-1) )
        b->ready = 1;

    b->write_pos = (b->write_pos + 1) % b->length;

    return 1;
}

static void get_frame( buffer_t *b, VJFrame *dst)
{
    int pos = b->read_pos;

    veejay_memcpy( dst->data[0], b->frames[ pos ]->data[0], b->frames[ pos ]->len );
    veejay_memcpy( dst->data[1], b->frames[ pos ]->data[1], b->frames[ pos ]->uv_len );
    veejay_memcpy( dst->data[2], b->frames[ pos ]->data[2], b->frames[ pos ]->uv_len );
    veejay_memcpy( dst->data[3], b->frames[ pos ]->data[3], b->frames[ pos ]->len );

    dst->len        = b->frames[ pos ]->len;
    dst->uv_len     = b->frames[ pos ]->uv_len;
    dst->stride[3]  = b->frames[ pos ]->stride[3];
    dst->ssm        = b->frames[ pos ]->ssm;

    free( b->frames[ pos ]->data[0] );
    free( b->frames[ pos ] );

    b->frames[ pos ] = NULL;

    b->read_pos = (pos + 1) % b->length;
}

void buffer_apply( void *ptr, VJFrame *frame, int *args )
{
    buffer_t *b = (buffer_t*) ptr;

    if( args[0] == 0 ) 
        return;

    if( b->length != args[0] ) {
        int i;
        for( i = 0; i < MAX_FRAMES; i ++ ) {
            if(b->frames[i]) {
                free(b->frames[i]->data[0]);
                free(b->frames[i]);
                b->frames[i] = NULL;
            }
        }
        b->length = args[0];
        b->write_pos = 0;
        b->read_pos = 0;
        b->ready = 0;
    }


    if(!put_frame( b, frame ))
        return;

    if( b->ready ) {
        get_frame( b, frame );
    }
    else {
        veejay_memset( frame->data[0], pixel_Y_lo_, frame->len );
        veejay_memset( frame->data[1], 128, frame->uv_len );
        veejay_memset( frame->data[2], 128, frame->uv_len );
    }
}
