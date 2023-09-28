/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "crosspixel.h"

typedef struct {
    uint8_t *cross_pixels[4];
} crosspixel_t;

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

	ve->hints = vje_init_value_hint_list( ve->num_params );
	
	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,"Black", "White" );

    return ve;
}

void *crosspixel_malloc(int w, int h)
{
    crosspixel_t *c = (crosspixel_t*) vj_calloc(sizeof(crosspixel_t));
    if(!c) {
        return NULL;
    }

    const int total_len = ( w * h * 3 );
    const int len = (w * h);

    c->cross_pixels[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * total_len );
    if(!c->cross_pixels[0]) {
        free(c);
        return NULL;
    }

    c->cross_pixels[1] = c->cross_pixels[0] + len;
    c->cross_pixels[2] = c->cross_pixels[1] + len;

    return (void*) c;
}

void crosspixel_free(void *ptr) {
    
    crosspixel_t *c = (crosspixel_t*) ptr;
    free(c->cross_pixels[0]);
    free(c);
}

void crosspixel_apply(void *ptr, VJFrame *frame, int *args) {
    int t = args[0];
    int v = args[1];

    crosspixel_t *c = (crosspixel_t*) ptr;

    unsigned int x,y,pos;
    const unsigned int vv = v * 2; // only even numbers 
    const unsigned int u_vv = vv >> frame->shift_h; // sfhit = / 2, shift_v = 2

	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
    const unsigned int uv_width = frame->uv_width;
	const unsigned int uv_height = frame->uv_height;

    unsigned int p = 0;

    uint8_t **cross_pixels = c->cross_pixels;

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

    for(y=0; y < (height>>1); y++) {
	if( (y%vv)==1) {
	  for(x=0; x < width; x+=vv) {	
	  	  for(p=0; p < vv; p++) {
		    pos = (x+p) + ( y * width ); 	 
		    Y[(x+(y*width))] = cross_pixels[0][pos];  
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


    for(y=(height>>1); y < height; y++) {
	if( (y%vv)==1) {
	  for(x=0; x < width; x+=vv) {	
	  	  for(p=0; p < vv; p++) {
		    pos = (x+p) + ( y * width ); 	 
		    Y[(x+(y*width))] = cross_pixels[0][pos];  
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


