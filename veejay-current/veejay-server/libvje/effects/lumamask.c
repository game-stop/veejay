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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "lumamask.h"
#include "common.h"
static uint8_t *buf[4] = { NULL,NULL,NULL,NULL };

vj_effect *lumamask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = width;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = height;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[0] = width/20; 
    ve->defaults[1] = height/10;
    ve->defaults[2] = 0; // border
    ve->description = "Displacement Map";
    ve->sub_format = 1;
    ve->extra_frame = 1;
  	ve->has_user = 0; 
	ve->param_description = vje_build_param_list(ve->num_params, "X displacement", "Y displcement", "Mode" );
    return ve;
}

static int n__ = 0;
static int N__ = 0;

int lumamask_malloc(int width, int height)
{
   buf[0] = (uint8_t*)vj_malloc( sizeof(uint8_t) * width * height * 3);
   if(!buf[0]) return 0;

   veejay_memset( buf[0], 0, width * height );

   buf[1] = buf[0] + (width *height);
   veejay_memset( buf[1], 128, width * height );
   buf[2] = buf[1] + (width *height);
   veejay_memset( buf[2], 128, width * height );
   n__ = 0;
   N__ = 0;   
   return 1;
}

void lumamask_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int v_scale, int h_scale, int border )
{
	unsigned int x,y;
	int dx,dy,nx,ny;
	int tmp;
	int interpolate = 1;
	int tmp1 = v_scale;
	int tmp2 = h_scale;
	int motion = 0;

	if( motionmap_active() )
	{
		motionmap_scale_to(width,height,1,1,&tmp1,&tmp2,&n__,&N__ );
		motion = 1;
	}
	else
	{
		n__ = 0;
		N__ = 0;
	}	
	if( n__ == N__ || n__ == 0 )
		interpolate = 0;

	double w_ratio = (double) tmp1 / 128.0;
	double h_ratio = (double) tmp2 / 128.0;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int strides[4] = { width * height, width * height, width * height ,0};
	vj_frame_copy( frame->data, buf, strides );

	if( border )
	{
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


				if( nx < 0 || ny < 0 || nx >= width || ny >= height )
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
	else
	{
		for(y=0; y < height; y++)
		{
			for(x=0; x < width ; x++)
			{
				tmp = Y2[(y*width+x)] - 128;
				dx = w_ratio * tmp;
				dy = h_ratio * tmp;
				nx = x + dx;
				ny = y + dy;
				while( nx < 0 )
					nx += width;
				while( ny < 0 )
					ny += height;
				//if( ny >= height ) ny = height - 1;
				//if( nx > width ) nx = width;	

				//Y[y*width+x] = Y2[ny * width + nx];
                                //Cb[y*width+x] = Cb2[ny * width + nx];
                                //Cr[y*width+x] = Cr2[ny * width + nx];
				if( nx < 0 || ny < 0 || nx >= width || ny >= height )
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

	if( interpolate )
		motionmap_interpolate_frame( frame, N__, n__ );
	
	if( motion )
		motionmap_store_frame( frame );

}
void lumamask_free()
{
  if(buf[0]) free(buf[0]);
  buf[0] = NULL;
  buf[1] = NULL;
  buf[2] = NULL;
  buf[3] = NULL;
}
