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
#include <libvjmem/vjmem.h>
#include <config.h>
#include <stdlib.h>

static uint8_t *buf[3] = { NULL,NULL,NULL };

vj_effect *lumamask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = width;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = height;
    ve->defaults[0] = width/20; 
    ve->defaults[1] = height/10;
    ve->description = "Displacement Map";
    ve->sub_format = 1;
    ve->extra_frame = 1;
  	ve->has_user = 0; 
    return ve;
}

// FIXME: private

int lumamask_malloc(int width, int height)
{
   buf[0] = (uint8_t*)vj_yuvalloc(width,height);
   if(!buf[0]) return 0;
   buf[1] = buf[0] + (width *height);
   buf[2] = buf[1] + (width *height);
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
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	// keep copy of original frame
	veejay_memcpy(buf[0], Y, width * height );
	veejay_memcpy(buf[1], Cb, (width * height) );
	veejay_memcpy(buf[2], Cr, (width * height) );

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


			if( nx < 0 || ny < 0 || nx > width || ny >= height )
                        {
                               Y[y*width+x] = 16;
                               Cb[y*width+x] = 128;
                               Cr[y*width+x] = 128;
                        }
                        else
                        {
                               Y[y*width+x] = Y2[ny * width + nx];
                               Cb[y*width+x] = Cb2[ny * width + nx];
                               Cr[y*width+x] = Cr2[ny * width + nx];
                        }
		}
	}
}
void lumamask_free()
{
  if(buf[0]) free(buf[0]);
  buf[0] = NULL;
  buf[1] = NULL;
  buf[2] = NULL;
}
