/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <libvjmem/vjmem.h>
#include "bathroom.h"
#include "common.h"
static uint8_t *bathroom_frame[4] = { NULL,NULL,NULL,NULL };

vj_effect *bathroom_init(int width,int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 64;
    ve->defaults[0] = 1;
    ve->defaults[1] = 32;
    ve->description = "Bathroom Window";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "H or V", "Value" );
    return ve;
}

static int n__ = 0;
static int N__ = 0;
int bathroom_malloc(int width, int height)
{
	int i;
	for( i = 0; i < 3; i ++ ) {
   	 bathroom_frame[i] = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(width*height));

   	 if(!bathroom_frame[i]) return 0;
  	}
	n__ = 0;
	N__ = 0;
    return 1;
}

void bathroom_free() {
	int i;	
	for( i = 0; i < 3 ; i ++ ) { 
 		if(bathroom_frame[i])
			free(bathroom_frame[i]);
		bathroom_frame[i] = NULL;
	}
}

void bathroom_verti_apply(VJFrame *frame, int width, int height, int val)
{
    unsigned int i;
    const unsigned int len = frame->len;
    unsigned int y_val = val;
    unsigned int x,y;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
	int strides[4] = { len, len, len, 0 };
	vj_frame_copy( frame->data, bathroom_frame, strides );

    if( y_val <= 0 )
	y_val = 1;

    for(y=0; y < height;y++) {
     for(x=0; x <width; x++) {
	i = (x + (x % y_val) - (y_val>>1)) + (y*width);
	if(i < 0) i = 0;
	if(i >= len) i = len-1;
	Y[y*width+x] = bathroom_frame[0][i];
	Cb[y*width+x] = bathroom_frame[1][i];
	Cr[y*width+x] = bathroom_frame[2][i];
     }
    }
	
}


void bathroom_hori_apply(VJFrame *frame, int width, int height, int val)
{
    unsigned int i;
    unsigned int len = (width * height);
    unsigned int y_val = val;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    unsigned int x,y;
	int strides[4] = { len, len, len, 0 };
	vj_frame_copy( frame->data, bathroom_frame, strides );

    for(y=0; y < height;y++) {
     for(x=0; x <width; x++) {
	i = ((y*width) + (y % y_val) - (y_val>>1)) + x;
	//while(i < 0) i += width;

	if( i < 0 ) i = 0; else if ( i >= len ) i = (len-1);

	Y[(y*width)+x] = bathroom_frame[0][i];
	Cb[(y*width)+x] = bathroom_frame[1][i];
	Cr[(y*width)+x] = bathroom_frame[2][i];
     }
    }

}

void bathroom_apply(VJFrame *frame, int width, int height, int mode, int val) {

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
	   case 1: bathroom_hori_apply(frame,width,height,tmp1); break;
	   case 0: bathroom_verti_apply(frame,width,height,tmp1); break;
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
