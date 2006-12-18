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
#include <libvjmem/vjmem.h>
#include "bathroom.h"

static uint8_t *bathroom_frame[3];

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
    ve->defaults[0] = 0;
    ve->defaults[1] = 32;
    ve->description = "Bathroom Window";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
    return ve;
}

// FIXME private

int bathroom_malloc(int width, int height)
{
    bathroom_frame[0] = (uint8_t*)vj_yuvalloc( width, height );
    if(!bathroom_frame[0]) return 0;
  
    bathroom_frame[1] = bathroom_frame[0] + (width * height );
    bathroom_frame[2] = bathroom_frame[1] + (width * height );
    return 1;
}

void bathroom_free() {
 if(bathroom_frame[0])
	 free(bathroom_frame[0]);
 bathroom_frame[0] = NULL;
 bathroom_frame[1] = NULL;
 bathroom_frame[2] = NULL;
}

void bathroom_verti_apply(VJFrame *frame, int width, int height, int val)
{
    unsigned int i;
    const unsigned int len = frame->len;
    const unsigned int y_val = val;
    unsigned int x,y;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    veejay_memcpy( bathroom_frame[0], Y, len);
    veejay_memcpy( bathroom_frame[1], Cb, len);
    veejay_memcpy( bathroom_frame[2], Cr, len);

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
    const unsigned int y_val = val;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    unsigned int x,y;
    veejay_memcpy( bathroom_frame[0], Y, len);
    veejay_memcpy( bathroom_frame[1], Cb, len);
    veejay_memcpy( bathroom_frame[2], Cr, len);

    for(y=0; y < height;y++) {
     for(x=0; x <width; x++) {
	i = ((y*width) + (y % y_val) - (y_val>>1)) + x;
	if(i < 0) i += width;
	Y[(y*width)+x] = bathroom_frame[0][i];
	Cb[(y*width)+x] = bathroom_frame[1][i];
	Cr[(y*width)+x] = bathroom_frame[2][i];
     }
    }

}


void bathroom_apply(VJFrame *frame, int width, int height, int mode, int val) {
 
  switch(mode) {
   case 1: bathroom_hori_apply(frame,width,height,val); break;
   case 0: bathroom_verti_apply(frame,width,height,val); break;
  }
}
