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
#include "vj-common.h"

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
    ve->limits[1][0] = width;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = height;

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 4;

    ve->description = "Zoom x 2";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 1;

    return ve;
}

int zoom_malloc(int width,int height)
{
	zoom_buffer[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height * 2); 
	if(!zoom_buffer[0]) return 0;
	zoom_buffer[1] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height * 2);
	if(!zoom_buffer[1]) return 0;
	zoom_buffer[2] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height * 2);
	if(!zoom_buffer[2]) return 0;
	return 1;

}

void zoom_free() {
  if(zoom_buffer[0]) free(zoom_buffer[0]);
  if(zoom_buffer[1]) free(zoom_buffer[1]);
  if(zoom_buffer[2]) free(zoom_buffer[2]);
  zoom_buffer[0] = NULL;
  zoom_buffer[1] = NULL;
  zoom_buffer[2] = NULL;
}



/* zoom image x 2 and deinterlace
   (purely made for zooming from v4l: capture card)*/

void zoom_apply( uint8_t *yuv[3], int width, int height, int x_offset, int y_offset, int factor) {
	unsigned int len = (width *height);
	unsigned int uv_len = len >> 2;
	unsigned int y,x;
	unsigned int i=0,j=0;
	unsigned int width2 = 2 * width;
	unsigned int c=0;
	unsigned int zoomlen = (width*height*2)-width2;
	unsigned int uv_height = height >> 1;
	unsigned int uv_width = width >> 1;
	uint8_t p1,p2,p3,p4,ps;
	uint8_t *Y = zoom_buffer[0];
	uint8_t *Cb = zoom_buffer[1];
	uint8_t *Cr = zoom_buffer[2];
	unsigned int f;

	/* zoom x 2 the image by duplicating pixels */

	for(f=0; f < factor; f++) {

	for(y=0; y < height; y++) {
	   for(x=0; x < width; x++) {
		zoom_buffer[0][i]   = yuv[0][(y*width)+x];
		zoom_buffer[0][i+1] = yuv[0][(y*width)+x];
		i+=2;
	   }
	}
	i=0;
	for(y=0; y < uv_height; y++) {
	  for(x=0; x < uv_width; x++) {
		zoom_buffer[1][i] = yuv[1][(y*uv_width)+x];
		zoom_buffer[2][i] = yuv[2][(y*uv_width)+x];
		zoom_buffer[1][i+1] = yuv[1][(y*uv_width)+x];
		zoom_buffer[2][i+1] = yuv[2][(y*uv_width)+x];
		i+=2;
	  }
	}
	
	i=0;
	while( i < len ) {
	  c = (y_offset * width2) + x_offset + j;
	  if( c <= zoomlen ) {
	    p1 = Y[c];
	    p2 = Y[c+1];
	    p3 = Y[c+width2+1];
	    p4 = Y[c+width2];
 	    if(p1 < 16) p1 = 16; else if (p1 > 240) p1 = 240;
       	    if(p2 < 16) p2 = 16; else if (p2 > 240) p2 = 240;
       	    if(p3 < 16) p3 = 16; else if (p3 > 240) p3 = 240;
       	    if(p4 < 16) p4 = 16; else if (p4 > 240) p4 = 240;
 
      	    /* look at neighbours to determine brightest value */
       	    if (p1 < p2 && p1 < p3)
           	ps = ( p2 + p3 ) >> 1;
       	    else
              if( ( p1 < p3 ) && (p1 < p4) )
                ps = ( p3 + p4 ) >> 1;
            if( ( p1 < p4 ) && ( p1 < p2) ) 
	        ps = ( p2 + p4 ) >> 1;
              else /* average 4 pixels and divide by 4 */
          	  ps = ( p1 + p2 + p3 + p4 ) >> 2; 
	
	     yuv[0][i] = ps;
	     j++;
	   }
	   else {
            yuv[0][i] = 16;
	   }
	   i++;
	}

	deinterlace( yuv[0], width, height, 1);

	
	i=0;
	j=0;
        zoomlen = (width*height) - width;

	while( i < uv_len ) {
	  c = ( (y_offset>>1) * width) + ( ( x_offset>>1) + j);
	  if( c <= zoomlen ) {
	    p1 = Cb[c];
	    p2 = Cb[c+1];
	    p3 = Cb[c+width+1];
	    p4 = Cb[c+width];
 	    if(p1 < 16) p1 = 16; else if (p1 > 235) p1 = 235;
       	    if(p2 < 16) p2 = 16; else if (p2 > 235) p2 = 235;
       	    if(p3 < 16) p3 = 16; else if (p3 > 235) p3 = 235;
       	    if(p4 < 16) p4 = 16; else if (p4 > 235) p4 = 235;
 
      	    /* look at neighbours to determine brightest value */
       	    if (p1 < p2 && p1 < p3)
           	ps = ( p2 + p3 ) >> 1;
       	    else
              if( ( p1 < p3 ) && (p1 < p4) )
                ps = ( p3 + p4 ) >> 1;
            if( ( p1 < p4 ) && ( p1 < p2) ) 
	        ps = ( p2 + p4 ) >> 1;
              else /* average 4 pixels and divide by 4 */
          	  ps = ( p1 + p2 + p3 + p4 ) >> 2; 
	
	     yuv[1][i] = ps;

  	    p1 = Cr[c];
	    p2 = Cr[c+1];
	    p3 = Cr[c+width+1];
	    p4 = Cr[c+width];
 	    if(p1 < 16) p1 = 16; else if (p1 > 235) p1 = 235;
       	    if(p2 < 16) p2 = 16; else if (p2 > 235) p2 = 235;
       	    if(p3 < 16) p3 = 16; else if (p3 > 235) p3 = 235;
       	    if(p4 < 16) p4 = 16; else if (p4 > 235) p4 = 235;
 
      	    /* look at neighbours to determine brightest value */
       	    if (p1 < p2 && p1 < p3)
           	ps = ( p2 + p3 ) >> 1;
       	    else
              if( ( p1 < p3 ) && (p1 < p4) )
                ps = ( p3 + p4 ) >> 1;
            if( ( p1 < p4 ) && ( p1 < p2) ) 
	        ps = ( p2 + p4 ) >> 1;
              else /* average 4 pixels and divide by 4 */
          	  ps = ( p1 + p2 + p3 + p4 ) >> 2; 
	
	     yuv[2][i] = ps;


	     j++;
	   }
	   else {
            yuv[1][i] = 128;
	    yuv[2][i] = 128;
	   }
	   i++;
	}

	deinterlace( yuv[1], uv_width, uv_height, 1);
	deinterlace( yuv[2], uv_width, uv_height, 1);
	
	i=0;
	j=0;
	zoomlen = (width*height*2) - (width*2);

	}

}

