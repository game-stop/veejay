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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "crosspixel.h"
#include "common.h"
#include <stdlib.h>

static uint8_t *cross_pixels[4] = { NULL,NULL,NULL, NULL};

vj_effect *crosspixel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 40;
    ve->defaults[0] = 0;
    ve->defaults[1] = 2;
    ve->description = "Pixel Raster";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Size" );
    return ve;
}

int crosspixel_malloc(int w, int h)
{
	int i;
	for( i = 0; i < 3 ; i ++ ) {
	   cross_pixels[i] = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(w*h) );
	   if(!cross_pixels[i])
		return 0;
	}
    return 1;
}

void crosspixel_free() {
	int i;
	for( i =0; i < 3 ;i ++ ) {
		if(cross_pixels[i])
		 free(cross_pixels[i]);
		cross_pixels[i] = NULL;
	}
}

void crosspixel_apply(VJFrame *frame, int w, int h, int t,int v) {
    unsigned int x,y,pos;
    const unsigned int vv = v * 2; // only even numbers 
    const unsigned int u_vv = vv >> frame->shift_h; // sfhit = / 2, shift_v = 2

	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
    const int uv_width = frame->uv_width;
	const int uv_height = frame->uv_height;

    unsigned int p = 0;
   	int strides[4] = { len, uv_len, uv_len ,0};
	vj_frame_copy( frame->data, cross_pixels, strides );

    if(t==0) {
	    vj_frame_clear1(Y, pixel_Y_lo_, len);
	    vj_frame_clear1(Cb, 128, uv_len);
	    vj_frame_clear1(Cr, 128, uv_len);
    }
    else
    {
	    vj_frame_clear1(Y, pixel_Y_hi_, len);
	    vj_frame_clear1(Cb, 128, uv_len);
	    vj_frame_clear1(Cr, 128, uv_len);
    }

    for(y=0; y < (h>>1); y++) {
	if( (y%vv)==1) {
	  for(x=0; x < w; x+=vv) {	
	  	  for(p=0; p < vv; p++) {
		    pos = (x+p) + ( y * w ); 	 
		    Y[(x+(y*w))] = cross_pixels[0][pos];  
		  }
	  }
	}
    }

    for(y=0; y < (uv_height >> frame->shift_v); y++) {
	if( (y%u_vv)==1) {
	  for(x=0; x < uv_width; x+=u_vv) {	
	  	  for(p=0; p < u_vv; p++) {
		    pos = (x+p) + ( y * uv_width ); 	 
		    Cb[(x+(y*uv_width))] = cross_pixels[1][pos];
		    Cr[(x+(y*uv_width))] = cross_pixels[2][pos];	
		  }
	  }
	}
    }


    for(y=(h>>1); y < h; y++) {
	if( (y%vv)==1) {
	  for(x=0; x < w; x+=vv) {	
	  	  for(p=0; p < vv; p++) {
		    pos = (x+p) + ( y * w ); 	 
		    Y[(x+(y*w))] = cross_pixels[0][pos];  
		  }
	  }
	}
    }
    for(y=(uv_height >> frame->shift_v); y < uv_height; y++) {
	if( (y%u_vv)==1) {
	  for(x=0; x < uv_width; x+=u_vv) {	
	  	  for(p=0; p < u_vv; p++) {
		    pos = (x+p) + ( y * uv_width ); 	 
		    Cb[(x+(y*uv_width))] = cross_pixels[1][pos];
		    Cr[(x+(y*uv_width))] = cross_pixels[2][pos];	
		  }
	  }
	}
    }


}


