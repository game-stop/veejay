/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
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


/*
  shift pixels in row/column to get a 'bathroom' window look. Use the parameters
  to set the distance and mode
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "bathroom.h"
#include "motionmap.h"

typedef struct {
    uint8_t *bathroom_frame[4];
    void *motionmap;
    int n__;
    int N__;
} bathroom_t;

vj_effect *bathroom_init(int width,int height)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 4;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 3;
	ve->limits[0][1] = 1;
	ve->limits[1][1] = 64;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = width;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = width;
	ve->defaults[0] = 1; 
	ve->defaults[1] = 32;
	ve->defaults[2] = 0;
	ve->defaults[3] = width;
	ve->description = "Bathroom Window";
	ve->sub_format = 1;
	ve->extra_frame = 0;
	ve->has_user = 0;
	ve->motion = 1;

	ve->alpha = FLAG_ALPHA_SRC_A| FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;

	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Distance","X start position", "X end position" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0, "Horizontal", "Vertical", "Horizontal (Alpha)", "Vertical (Alpha)" );


	return ve;
}

void *bathroom_malloc(int width, int height)
{
    bathroom_t *b = (bathroom_t*) vj_calloc(sizeof(bathroom_t));
    if(!b) {
        return NULL;
    }

   	b->bathroom_frame[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * (width*height * 4));
	if(!b->bathroom_frame[0]) {
        free(b);
		return NULL;
    }

	b->bathroom_frame[1] = b->bathroom_frame[0] + (width*height);
	b->bathroom_frame[2] = b->bathroom_frame[1] + (width*height);
	b->bathroom_frame[3] = b->bathroom_frame[2] + (width*height);

    return (void*) b;
}

void bathroom_free(void *ptr) {

    bathroom_t *b = (bathroom_t*) ptr;

	free(b->bathroom_frame[0]);	
    free(b);
}

static void bathroom_verti_apply(bathroom_t *b, VJFrame *frame, int val, int x0, int x1)
{
    const unsigned int width  = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;

    if (val <= 0) val = 1;
    const int half_val = val >> 1;

    uint8_t *Y  = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    uint8_t **bathroom_frame = b->bathroom_frame;

    vj_frame_copy(frame->data, bathroom_frame, NULL);

    for (unsigned int y = 0; y < height; y++) {
        for (unsigned int x = x0; x < x1; x++) {
            int src_idx = x + (x % val) - half_val + y * width;

            src_idx &= ~(src_idx >> 31);
            int diff = src_idx - (len - 1);
            src_idx = (len - 1) + (diff & (diff >> 31));

            Y[y*width + x]  = bathroom_frame[0][src_idx];
            Cb[y*width + x] = bathroom_frame[1][src_idx];
            Cr[y*width + x] = bathroom_frame[2][src_idx];
        }
    }
}

static void bathroom_alpha_verti_apply(bathroom_t *b, VJFrame *frame, int val, int x0, int x1)
{
    const unsigned int width  = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;

    if (val <= 0) val = 1;
    const int half_val = val >> 1;

    uint8_t *Y  = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *A  = frame->data[3];

    uint8_t **bathroom_frame = b->bathroom_frame;

    int strides[4] = {len, len, len, len};
    vj_frame_copy(frame->data, bathroom_frame, strides);

    for (unsigned int y = 0; y < height; y++) {
        for (unsigned int x = x0; x < x1; x++) {
            int src_idx = x + (x % val) - half_val + y * width;

            src_idx &= ~(src_idx >> 31);
            int diff = src_idx - (len - 1);
            src_idx = (len - 1) + (diff & (diff >> 31));

            Y[y*width + x]  = bathroom_frame[0][src_idx];
            Cb[y*width + x] = bathroom_frame[1][src_idx];
            Cr[y*width + x] = bathroom_frame[2][src_idx];
            A[y*width + x]  = bathroom_frame[3][src_idx];
        }
    }
}


static void bathroom_hori_apply(bathroom_t *b, VJFrame *frame, int val, int x0, int x1)
{
    const unsigned int width  = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;

    if (val <= 0) val = 1;
    const int half_val = val >> 1;

    uint8_t *Y  = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    uint8_t **bathroom_frame = b->bathroom_frame;

    int strides[4] = {len, len, len, 0};
    vj_frame_copy(frame->data, bathroom_frame, strides);

    for (unsigned int y = 0; y < height; y++) {
        int y_offset = (y % val) - half_val;
        for (unsigned int x = x0; x < x1; x++) {
            int src_idx = (y * width) + y_offset + x;

            src_idx &= ~(src_idx >> 31);
            int diff = src_idx - (len - 1);
            src_idx = (len - 1) + (diff & (diff >> 31));

            Y[y*width + x]  = bathroom_frame[0][src_idx];
            Cb[y*width + x] = bathroom_frame[1][src_idx];
            Cr[y*width + x] = bathroom_frame[2][src_idx];
        }
    }
}

static void bathroom_alpha_hori_apply(bathroom_t *b, VJFrame *frame, int val, int x0, int x1)
{
    const unsigned int width  = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;

    if (val <= 0) val = 1;
    const int half_val = val >> 1;

    uint8_t *Y  = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *A  = frame->data[3];

    uint8_t **bathroom_frame = b->bathroom_frame;

    int strides[4] = {len, len, len, len};
    vj_frame_copy(frame->data, bathroom_frame, strides);

    for (unsigned int y = 0; y < height; y++) {
        int y_offset = (y % val) - half_val;

        for (unsigned int x = x0; x < x1; x++) {
            int src_idx = (y * width) + y_offset + x;

            src_idx &= ~(src_idx >> 31);
            int diff = src_idx - (len - 1);
            src_idx = (len - 1) + (diff & (diff >> 31));

            Y[y*width + x]  = bathroom_frame[0][src_idx];
            Cb[y*width + x] = bathroom_frame[1][src_idx];
            Cr[y*width + x] = bathroom_frame[2][src_idx];
            A[y*width + x]  = bathroom_frame[3][src_idx];
        }
    }
}

void bathroom_apply(void *ptr, VJFrame *frame, int *args) {
    int mode = args[0];
    int val = args[1];
    int x0 = args[2];
    int x1 = args[3];
	int interpolate = 1;
 	int tmp1 = val;
	int tmp2 = 0;
	int motion = 0;

    bathroom_t *b = (bathroom_t*) ptr;

	if(motionmap_active(b->motionmap))
	{
		motionmap_scale_to(b->motionmap, 64, 64, 1, 1, &tmp1, &tmp2, &(b->n__), &(b->N__) );
		motion = 1;
	}
	else
	{
		b->N__ = 0;
		b->n__ = 0;
	}
	if( b->n__ == b->N__ || b->n__ == 0 )
		interpolate = 0;

	switch(mode)
	{
		case 1: bathroom_hori_apply(b,frame,tmp1,x0,x1); break;
		case 0: bathroom_verti_apply(b,frame,tmp1,x0,x1); break;
		case 2: bathroom_alpha_hori_apply(b,frame,tmp1,x0,x1); break;
		case 3: bathroom_alpha_verti_apply(b,frame,tmp1,x0,x1); break;
  	}

	if( interpolate )
	{
		motionmap_interpolate_frame( b->motionmap,frame, b->N__,b->n__ );
	}

	if(motion)
	{
		motionmap_store_frame( b->motionmap,frame );
	}
}
