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
#include <config.h>
#include <stdlib.h>
#include "common.h"
#include "split.h"
#include <stdlib.h>
#include <stdio.h>

static uint8_t *zoom_buffer[3];

vj_effect *zoom_init(int width , int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;
    ve->defaults[1] = 0;
    ve->defaults[2] = 1;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = width/2;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = height/2;

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 4;

    ve->description = "Zoom x 2";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 1;

  //    memset(zoom_buffer[0], 16, (width*height*2));
 //   memset(zoom_buffer[1], 128, (width*height*2));
  //  memset(zoom_buffer[2], 128, (width*height*2));    


    return ve;
}

int	zoom_malloc(int width, int height)
{
    zoom_buffer[0] = (uint8_t*)malloc(sizeof(uint8_t) * width * height * 2); 
	if(!zoom_buffer[0]) return 0;
    zoom_buffer[1] = (uint8_t*)malloc(sizeof(uint8_t) * width * height * 2);
	if(!zoom_buffer[1]) return 0;
    zoom_buffer[2] = (uint8_t*)malloc(sizeof(uint8_t) * width * height * 2);
	if(!zoom_buffer[2]) return 0;
	return 1;
}

void zoom_free() {
  if(zoom_buffer[0]) free(zoom_buffer[0]);
  if(zoom_buffer[1]) free(zoom_buffer[1]);
  if(zoom_buffer[2]) free(zoom_buffer[2]);
}

static inline uint8_t decide_pixel(uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4, const uint8_t upper, const uint8_t lower)
{
    uint8_t ps;
     /* look at neighbours to determine brightest value */
    if (p1 < p2 && p1 < p3)
	{
       	ps = ( p2 + p3 ) >> 1;
	}
    else
    {
		if( ( p1 < p3 ) && (p1 < p4) )
        {
	        ps = ( p3 + p4 ) >> 1;
    	}
		else
		{
       	 if( ( p1 < p4 ) && ( p1 < p2) ) 
	     {
			    ps = ( p2 + p4 ) >> 1;
         }
		 else /* average 4 pixels and divide by 4 */
         {
		 	  ps = ( p1 + p2 + p3 + p4 ) >> 2; 
		 }
        }
   }
   if( ps < lower ) ps = lower; else if ( ps > upper ) ps = upper;
	return ps;
}

/* this is shit */
/* c = doesnt work */
void zoom_apply( VJFrame *frame, int width, int height, int x_offset, int y_offset, int factor) {
	unsigned int y,x;
	int i=0,j=0,k=0;
 
	const int width2 = 2 * width;
    const int height2 = 2 * height;
	unsigned int uv_height = height >> frame->shift_v;
	unsigned int uv_width = width >> frame->shift_h;
	uint8_t *Y = zoom_buffer[0];
	uint8_t *Cb = zoom_buffer[1];
	uint8_t *Cr = zoom_buffer[2];
	unsigned int f;
	uint8_t *yuv[3];
    const int zy_offset =  y_offset;
	const int zx_offset =  x_offset;
	const int uv_height2 = height2 >> frame->shift_v;
	const int uv_width2 = width2 >> frame->shift_h;
	const int zy_uv_offset = zy_offset >> frame->shift_v;
	const int zx_uv_offset = zx_offset >> frame->shift_h;

	yuv[0] = frame->data[0];
	yuv[1] = frame->data[1];
	yuv[2] = frame->data[2];
    
	/* zoom x 2 the image by duplicating pixels */

	for(f=0; f < factor; f++)
	{

		for(y=0; y < height; y++)
		{
			for(x=0; x < width; x++)
			{
				Y[i]   = yuv[0][(y*width)+x];
				Y[i+1] = yuv[0][(y*width)+x];
				i+=2;
	   		}
		}
		i=0;
		for(y=0; y < uv_height; y++)
		{
	  		for(x=0; x < uv_width; x++)
			{
				Cb[i] = yuv[1][(y*uv_width)+x];
				Cr[i] = yuv[2][(y*uv_width)+x];
				Cb[i+1] = yuv[1][(y*uv_width)+x];
				Cr[i+1] = yuv[2][(y*uv_width)+x];
				i+=2;
	  		}
		}
	
		for(y =0; y  < height ; y ++ )
		{
			j = (y + zy_offset);
			if( j > height2 ) j -= height2;
			for(x = 0; x < width ; x ++ )
			{
				k = (x + zx_offset);
				if( k > width2 ) k -= width2;
				i = (j  * width2) + k;
				yuv[0][y*width+x] = Y[i];
			}
			if(x < width)
			{
				for( ; x < width; x++)
				{
					yuv[0][y * width +x ] = 16;
				}
			}
		}
		if( y < height )
		{
			for(  ;y < height; y ++ )
			{
				memset( yuv[0] + (y * width), 16, width);
			}
		}
	
		for(y =0; y  < uv_height && (y + zy_uv_offset) < uv_height2; y ++ )
		{
			j = ( y + zy_uv_offset );
			if( j > uv_height2 ) j -= uv_height2;

			for(x = 0; x < uv_width && (x + zx_uv_offset) < uv_width2; x ++ )
			{
				k = (x + zx_uv_offset);
				if( k > uv_width2) k -= uv_width2;
				i = j * uv_width2 + k;
				yuv[1][y*uv_width+x] = Cb[i];
				yuv[2][y*uv_width+x] = Cr[i];
			}
			if(x < uv_width)
			{
				for( ; x < uv_width; x++)
				{
					yuv[1][y * uv_width +x ] = 128;
				    yuv[2][y * uv_width +x ] = 128;
				}
			}
		}
		if( y < uv_height )
		{
			for(  ;y < uv_height; y ++ )
			{
				memset( yuv[1] + (y * uv_width), 128, uv_width);
				memset( yuv[2] + (y * uv_width), 128, uv_width);
			}
		}	
	}

}

