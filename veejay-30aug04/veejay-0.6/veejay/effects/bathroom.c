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
#include "bathroom.h"
#include "vj-common.h"
#include <stdlib.h>
extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);

static uint8_t *bathroom_frame[3];

vj_effect *bathroom_init(int width,int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 64;
    ve->defaults[0] = 0;
    ve->defaults[1] = 32;
    ve->description = "Bathroom Window";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data= 1;

    return ve;
}

int bathroom_malloc(int width, int height)
{
    const int uv_len = (width * height ) /4;
    bathroom_frame[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height);
    if(!bathroom_frame[0]) return 0;
    bathroom_frame[1] = (uint8_t*)vj_malloc(sizeof(uint8_t) * uv_len);
    if(!bathroom_frame[1]) return 0;
    bathroom_frame[2] = (uint8_t*)vj_malloc(sizeof(uint8_t) * uv_len);
    if(!bathroom_frame[2]) return 0;

    return 1;
}

void bathroom_free() {
 if(bathroom_frame[0]) free(bathroom_frame[0]);
 if(bathroom_frame[1]) free(bathroom_frame[1]);
 if(bathroom_frame[2]) free(bathroom_frame[2]);
}

void bathroom_verti_apply(uint8_t * yuv[3], int width, int height, int val)
{
    unsigned int i;
    const unsigned int len = (width * height);
    const unsigned int uv_len = len / 4;
    const unsigned int uv_height = height / 2;
    const unsigned int uv_width = width / 2;
    const unsigned int y_val = val * 2;
    const unsigned int u_val = y_val / 2;
    unsigned int x,y;

    veejay_memcpy( bathroom_frame[0], yuv[0], len);
    veejay_memcpy( bathroom_frame[1], yuv[1], uv_len);
    veejay_memcpy( bathroom_frame[2], yuv[2], uv_len);

    for(y=0; y < height;y++) {
     for(x=0; x <width; x++) {
	i = (x + (x % y_val) - (y_val>>1)) + (y*width);
	if(i < 0) i = 0;
	if(i > len) i = len;
	yuv[0][y*width+x] = bathroom_frame[0][i];
     }
    }
    for(y=0; y < uv_height;y++) {
     for(x=0; x <uv_width; x++) {
	i = (x + (x % u_val) - (u_val>>1)) + (y*uv_width);
	if(i < 0) i = 0;
	if(i > uv_len) i = uv_len;
	yuv[1][y*uv_width+x] = bathroom_frame[1][i];
	yuv[2][y*uv_width+x] = bathroom_frame[2][i];
     }
    }
	
}


void bathroom_hori_apply(uint8_t * yuv[3], int width, int height, int val)
{
    unsigned int i;
    unsigned int len = (width * height);
    const unsigned int uv_len = len /4;
    const unsigned int uv_height = height/2;
    const unsigned int uv_width = width / 2;
    const unsigned int y_val = val * 2;
    const unsigned int u_val = y_val  / 2;

    unsigned int x,y;
    veejay_memcpy( bathroom_frame[0], yuv[0], len);
    veejay_memcpy( bathroom_frame[1], yuv[1], uv_len);
    veejay_memcpy( bathroom_frame[2], yuv[2], uv_len);

    for(y=0; y < height;y++) {
     for(x=0; x <width; x++) {
	i = ((y*width) + (y % y_val) - (y_val>>1)) + x;
	if(i < 0) i += width;
	yuv[0][(y*width)+x] = bathroom_frame[0][i];
     }
    }

    for(y=0; y < uv_height;y++) {
     for(x=0; x <uv_width; x++) {
	i = ((y*uv_width) + (y % u_val) - (u_val>>1)) + x;
	if(i < 0) i += uv_width;
	yuv[1][(y*uv_width)+x] = bathroom_frame[1][i];
	yuv[2][(y*uv_width)+x] = bathroom_frame[2][i];
     }
    }
	
}


void bathroom_apply(uint8_t *yuv[3], int width, int height, int mode, int val) {
 
  switch(mode) {
   case 1: bathroom_hori_apply(yuv,width,height,val); break;
   case 0: bathroom_verti_apply(yuv,width,height,val); break;
  }
}
