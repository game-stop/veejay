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
#include "slice.h"
#include <stdlib.h>
#include "vj-common.h"

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);

static uint8_t *slice_frame[3];
static int *slice_xshift;
static int *slice_yshift;
void slice_recalc(int width, int height, int val);



vj_effect *slice_init(int width,int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 128;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[0] = 63;
    ve->defaults[1] = 0;
    ve->description = "Slice Window";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_internal_data= 1;
    return ve;
}
int 	slice_malloc(int width, int height)
{
    slice_frame[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height);
    if(!slice_frame[0]) return 0;
    slice_frame[1] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height);
    if(!slice_frame[1]) return 0; 
    slice_frame[2] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height);
    if(!slice_frame[2]) return 0;
    slice_xshift = (int*) vj_malloc(sizeof(int) * height);
    if(!slice_xshift) return 0;
    slice_yshift = (int*) vj_malloc(sizeof(int) * width);
    if(!slice_yshift) return 0;
    slice_recalc(width,height, 63);
 
    return 1;
}


void slice_free() {
 if(slice_frame[0]) free(slice_frame[0]);
 if(slice_frame[1]) free(slice_frame[1]);
 if(slice_frame[2]) free(slice_frame[2]);
 if(slice_xshift) free(slice_xshift);
 if(slice_yshift) free(slice_yshift);
}

/* much like the bathroom window, width height indicate block size within frame */
void slice_recalc(int width, int height, int val) {
  unsigned int x,y,dx,dy,r;
  for(x = dx = 0; x < width; x++) 
  {
    if(dx==0) { r = (rand() & val)-((val>>1)+1); dx = 8 + rand() & ((val>>1)-1); } else dx--;
    slice_yshift[x] = r;
  }
 
  for(y=dy=0; y < height; y++) {
   if(dy==0) { r = (rand() & val)-((val>>1)+1); dy = 8 + rand() & ((val>>1)-1); } else dy--;
   slice_xshift[y] = r;
  }
}

void slice_apply(VJFrame *frame, int width, int height, int val, int re_init) {
  unsigned int x,y,r,dx,dy,xshift[height], yshift[width];
  unsigned int len = (width*height);
  uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	

  if(re_init==1) slice_recalc(width,height,val);

  veejay_memcpy( slice_frame[0], Y, len);
  veejay_memcpy( slice_frame[1], Cb, len);
  veejay_memcpy( slice_frame[2], Cr, len);

  for(y=0; y < height; y++){ 
    for(x=0; x < width; x++) {
	dx = x + slice_xshift[y] ; dy = y + slice_yshift[x];
	if(dx < width && dy < height && dx >= 0 && dy >= 0) {
		Y[(y*width)+x] = slice_frame[0][(dy*width)+dx];
		Cb[(y*width)+x] = slice_frame[1][(dy*width)+dx];
		Cr[(y*width)+x] = slice_frame[2][(dy*width)+dx];
	}
     }
  }
}

