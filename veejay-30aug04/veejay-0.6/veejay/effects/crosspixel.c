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

#include "crosspixel.h"
#include <stdlib.h>
#include "vj-common.h"

static uint8_t *cross_pixels[3];

vj_effect *crosspixel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 40;
    ve->defaults[0] = 0;
    ve->defaults[1] = 2;
    ve->description = "Pixel Raster";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data= 0;

    return ve;
}

int crosspixel_malloc(int w, int h)
{
   const int uv_len = (w*h)/4;
   cross_pixels[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * w * h);
   if(!cross_pixels[0]) return 0;
    cross_pixels[1] = (uint8_t*)vj_malloc(sizeof(uint8_t) * uv_len);
   if(!cross_pixels[1]) return 0;
    cross_pixels[2] = (uint8_t*)vj_malloc(sizeof(uint8_t) * uv_len);
   if(!cross_pixels[2]) return 0;

    return 1;
}

void crosspixel_free() {
 if(cross_pixels[0]) free(cross_pixels[0]);
 if(cross_pixels[1]) free(cross_pixels[1]);
 if(cross_pixels[2]) free(cross_pixels[2]);
}

void crosspixel_apply(uint8_t *yuv[3], int w, int h, int t,int v) {
    unsigned int x,y,pos;
    const unsigned int uv_height=h/2;
    const unsigned int uv_width=w/2;
    const unsigned int len = (w*h)/2;
    const unsigned int uv_len = (w*h) / 4;
    const unsigned int vv = v * 2; // only even numbers 
    const unsigned int u_vv = vv / 2;
    unsigned int i =  0;
    unsigned int p = 0;
    
    memcpy( cross_pixels[0], yuv[0], w*h);
    memcpy( cross_pixels[1], yuv[1], uv_len);
    memcpy( cross_pixels[2], yuv[2], uv_len);

    if(t==0) {
	    memset(yuv[0], 16, (w*h));
	    memset(yuv[1], 128, uv_len);
	    memset(yuv[2], 128, uv_len);
    }
    else
    {
	    memset(yuv[0], 235, (w*h));
	    memset(yuv[1], 128, uv_len);
	    memset(yuv[2], 128, uv_len);
    }

    for(y=0; y < (h/2); y++) {
	if( (y%vv)==1) {
	  for(x=0; x < w; x+=vv) {	
	  	  for(p=0; p < vv; p++) {
		    pos = (x+p) + ( y * w ); 	 
		    yuv[0][(x+(y*w))] = cross_pixels[0][pos];  
		  }
	  }
	}
    }

    for(y=0; y < (uv_height/2); y++) {
	if( (y%u_vv)==1) {
	  for(x=0; x < uv_width; x+=u_vv) {	
	  	  for(p=0; p < u_vv; p++) {
		    pos = (x+p) + ( y * uv_width ); 	 
		    yuv[1][(x+(y*uv_width))] = cross_pixels[1][pos];
		    yuv[2][(x+(y*uv_width))] = cross_pixels[2][pos];	
		  }
	  }
	}
    }


    for(y=(h/2); y < h; y++) {
	if( (y%vv)==1) {
	  for(x=0; x < w; x+=vv) {	
	  	  for(p=0; p < vv; p++) {
		    pos = (x+p) + ( y * w ); 	 
		    yuv[0][(x+(y*w))] = cross_pixels[0][pos];  
		  }
	  }
	}
    }
    for(y=(uv_height/2); y < uv_height; y++) {
	if( (y%u_vv)==1) {
	  for(x=0; x < uv_width; x+=u_vv) {	
	  	  for(p=0; p < u_vv; p++) {
		    pos = (x+p) + ( y * uv_width ); 	 
		    yuv[1][(x+(y*uv_width))] = cross_pixels[1][pos];
		    yuv[2][(x+(y*uv_width))] = cross_pixels[2][pos];	
		  }
	  }
	}
    }


}


