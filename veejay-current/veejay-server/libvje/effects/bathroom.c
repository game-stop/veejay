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
#include <libvjmem/vjmem.h>
#include "bathroom.h"

static uint8_t *bathroom_frame[4] = { NULL,NULL,NULL,NULL };

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

static int n__ = 0;
static int N__ = 0;
int bathroom_malloc(int width, int height)
{
   	bathroom_frame[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(width*height * 4));
	if(!bathroom_frame[0])
		return 0;
	bathroom_frame[1] = bathroom_frame[0] + RUP8(width*height);
	bathroom_frame[2] = bathroom_frame[1] + RUP8(width*height);
	bathroom_frame[3] = bathroom_frame[2] + RUP8(width*height);

	n__ = 0;
	N__ = 0;
    return 1;
}

void bathroom_free() {
	int i;
	if(bathroom_frame[0])
		free(bathroom_frame[0]);	
	for( i = 0; i < 4; i ++ ) { 
		bathroom_frame[i] = NULL;
	}
}

static void bathroom_verti_apply(VJFrame *frame, int val, int x0, int x1)
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
    const unsigned int len = frame->len;
    unsigned int y_val = val;
    unsigned int x,y;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
	int strides[4] = { len, len, len, 0 };
	int i;
	vj_frame_copy( frame->data, bathroom_frame, strides );

    if( y_val <= 0 )
		y_val = 1;

    for(y=0; y < height;y++) {
     for(x=x0; x < x1; x++) {
	i = (x + (x % y_val) - (y_val>>1)) + (y*width);
	if( i < 0 ) i = 0; else if ( i > len ) i = len;
	Y[y*width+x] = bathroom_frame[0][i];
	Cb[y*width+x] = bathroom_frame[1][i];
	Cr[y*width+x] = bathroom_frame[2][i];
     }
    }
}

static void	bathroom_alpha_verti_apply(VJFrame *frame, int val, int x0, int x1)
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
    const unsigned int len = frame->len;
    unsigned int y_val = val;
    unsigned int x,y;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
	uint8_t *A = frame->data[3];
	int strides[4] = { len, len, len, len };
	int i;
	vj_frame_copy( frame->data, bathroom_frame, strides );

    if( y_val <= 0 )
		y_val = 1;

    for(y=0; y < height;y++) {
     for(x=x0; x < x1; x++) {
		i = (x + (x % y_val) - (y_val>>1)) + (y*width);
		if( i < 0 ) i = 0; else if ( i > len ) i = len;

		Y[y*width+x] = bathroom_frame[0][i];
		Cb[y*width+x] = bathroom_frame[1][i];
		Cr[y*width+x] = bathroom_frame[2][i];
		A[y*width+x] = bathroom_frame[3][i];
     }
    }
}

static void bathroom_hori_apply(VJFrame *frame, int val, int x0, int x1)
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
    const unsigned int len = frame->len;
    unsigned int y_val = val;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    unsigned int x,y;
	int i;
	int strides[4] = { len, len, len, 0 };
	vj_frame_copy( frame->data, bathroom_frame, strides );

    for(y=0; y < height;y++) {
     for(x=x0; x < x1; x++) {
		i = ((y*width) + (y % y_val) - (y_val>>1)) + x;
		if( i < 0 ) i = 0; else if ( i > len ) i = len;

		Y[(y*width)+x] = bathroom_frame[0][i];
		Cb[(y*width)+x] = bathroom_frame[1][i];
		Cr[(y*width)+x] = bathroom_frame[2][i];
     }
    }
}

static void bathroom_alpha_hori_apply(VJFrame *frame, int val, int x0, int x1)
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
    const unsigned int len = frame->len;
    unsigned int y_val = val;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
	uint8_t *A = frame->data[3];
	
    unsigned int x,y;
	int strides[4] = { len, len, len, len };
	int i;
	vj_frame_copy( frame->data, bathroom_frame, strides );

    for(y=0; y < height;y++) {
     for(x=x0; x < x1; x++) {
		i = ((y*width) + (y % y_val) - (y_val>>1)) + x;
		if( i < 0 ) i = 0; else if ( i > len ) i = len;

		Y[(y*width)+x] = bathroom_frame[0][i];
		Cb[(y*width)+x] = bathroom_frame[1][i];
		Cr[(y*width)+x] = bathroom_frame[2][i];
		A[(y*width)+x] = bathroom_frame[3][i];
     }
    }
}


void bathroom_apply(VJFrame *frame, int mode, int val, int x0, int x1) {

	int interpolate = 1;
 	int tmp1 = val;
	int tmp2 = 0;
	int motion = 0;
	if(motionmap_active())
	{
		motionmap_scale_to( 64, 64, 1, 1, &tmp1, &tmp2, &n__, &N__ );
		motion = 1;
	}
	else
	{
		N__ = 0;
		n__ = 0;
	}
	if( n__ == N__ || n__ == 0 )
		interpolate = 0;

	switch(mode)
	{
		case 1: bathroom_hori_apply(frame,tmp1,x0,x1); break;
		case 0: bathroom_verti_apply(frame,tmp1,x0,x1); break;
		case 2: bathroom_alpha_hori_apply(frame,tmp1,x0,x1); break;
		case 3: bathroom_alpha_verti_apply(frame,tmp1,x0,x1); break;
  	}

	if( interpolate )
	{
		motionmap_interpolate_frame( frame, N__,n__ );
	}

	if(motion)
	{
		motionmap_store_frame( frame );
	}
}
