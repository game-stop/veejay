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
  this effect takes lumaninance information of frame B  (0=no displacement,255=max displacement)
  to extract distortion offsets for frame A.
  h_scale and v_scale can be used to limit the scaling factor.
  if the value is < 128, the pixels will be shifted to the left
  otherwise to the right.
           


*/

#include "lumamask.h"
#include <config.h>
#include <stdlib.h>
#include "vj-common.h"

static uint8_t *buf[3];

vj_effect *lumamask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = width;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = height;
    ve->defaults[0] = width/20; 
    ve->defaults[1] = height/10;
    ve->description = "Displacement Map";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_internal_data = 1;
   
    return ve;
}

int lumamask_malloc(int width, int height)
{
   buf[0] = (uint8_t*)vj_malloc(sizeof(uint8_t)*width*height);
   if(!buf[0]) return 0;
   buf[1] = (uint8_t*)vj_malloc(sizeof(uint8_t)*width*height);
   if(!buf[1]) return 0;
   buf[2] = (uint8_t*)vj_malloc(sizeof(uint8_t)*width*height);
   if(!buf[2]) return 0;
   return 1;
}


void lumamask_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int v_scale, int h_scale)
{
	unsigned int x,y;
	int dx,dy,nx,ny;
	int tmp;
	// scale values in frame B to fit row/colum coordinates	
	double w_ratio = (double) v_scale / 128.0;
	double h_ratio = (double) h_scale / 128.0;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

	// keep copy of original frame
	memcpy(buf[0], Y, width * height );
	memcpy(buf[1], Cb, (width * height) );
	memcpy(buf[2], Cr, (width * height) );

	for(y=0; y < height; y++)
	{
		for(x=0; x < width ; x++)
		{
			// calculate new location of pixel
			tmp = Y2[(y*width+x)] - 128;
			// new x offset 
			dx = w_ratio * tmp;
			// new y offset 
			dy = h_ratio * tmp;
			// new pixel coordinates
			nx = x + dx;
			ny = y + dy;
			if(nx < 0) nx+=width;	
			if(nx < 0) nx = 0; else if (nx > width) nx = width;
			if(ny < 0) ny = 0; else if (ny >= height) ny = height-1;
			// put pixels from local copy
			Y[y*width+x] = buf[0][ny * width + x];
			Cb[y*width+x] = buf[1][ny * width + x];
			Cr[y*width+x] = buf[2][ny * width + x];
		}
	}
}
void lumamask_free()
{
  if(buf[0]) free(buf[0]);
  if(buf[1]) free(buf[1]);
  if(buf[2]) free(buf[2]);
}
