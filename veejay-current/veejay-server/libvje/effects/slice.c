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
#include <stdint.h>
#include <stdio.h>
#include "slice.h"
#include <stdlib.h>
#include "common.h"

static uint8_t *slice_frame[4] = { NULL,NULL,NULL,NULL };
static int *slice_xshift = NULL;
static int *slice_yshift = NULL;
static int n__ = 0;
static int N__ = 0;
void slice_recalc(int width, int height, int val);

vj_effect *slice_init(int width,int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 128;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[0] = 63;
    ve->defaults[1] = 0;
    ve->description = "Slice Window";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user =0;
	ve->param_description = vje_build_param_list( ve->num_params, "Slices", "Mode");
    return ve;
}

int 	slice_malloc(int width, int height)
{
    slice_frame[0] = (uint8_t*)vj_malloc( sizeof(uint8_t) * RUP8(width * height * 3));
    if(!slice_frame[0])
	    return 0;
    slice_frame[1] = slice_frame[0] + (width * height);
    slice_frame[2] = slice_frame[1] + (width * height);
    slice_xshift = (int*) vj_malloc(sizeof(int) * height);
    if(!slice_xshift) return 0;
    slice_yshift = (int*) vj_malloc(sizeof(int) * width);
    if(!slice_yshift) return 0;
    slice_recalc(width,height, 63);
    n__ = 0;
    N__ = 0; 
    return 1;
}


void slice_free() {
 if(slice_frame[0])
	 free(slice_frame[0]);
 slice_frame[0] = NULL;
 slice_frame[1] = NULL;
 slice_frame[2] = NULL;
 if(slice_xshift)
	 free(slice_xshift);
 if(slice_yshift)
	 free(slice_yshift);
 slice_yshift = NULL;
 slice_xshift = NULL;
}

/* much like the bathroom window, width height indicate block size within frame */
void slice_recalc(int width, int height, int val) {
  unsigned int x,y,dx,dy,r;
  for(x = dx = 0; x < width; x++) 
  {
    if(dx==0)
	{ 
		r = ((rand() & val))-((val>>1)+1); 
		dx = 8 + (rand() & ((val>>1))-1);
	}
	else
	{
		 dx--;
	}
    slice_yshift[x] = r;
  }
 
  for(y=dy=0; y < height; y++) {
   if(dy==0) { r = (rand() & val)-((val>>1)+1); dy = 8 + rand() & ((val>>1)-1); } else dy--;
   slice_xshift[y] = r;
  }
}


void slice_apply(VJFrame *frame, int width, int height, int val, int re_init) {
  unsigned int x,y,dx,dy;
  unsigned int len = (width*height);
  uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	

	int interpolate = 1;
	int tmp1 = val;
	int tmp2 = re_init;
	int motion = 0;
	if( motionmap_active())
	{
		motionmap_scale_to( 128,1, 2, 0, &tmp1, &tmp2, &n__ , &N__ );
		if( val >= 64 )
		{
			if( (rand() % 25 )== 0)
				tmp2 = 1;
		}
		else
		{
			tmp2 = 1;
		}
		motion = 1;
	}
	else
	{
		n__ = 0;
		N__ = 0;
	}

	if( n__ == N__ || n__ == 0 )
		interpolate = 0;

  if(tmp2==1) slice_recalc(width,height,tmp1);

  int strides[4] = { len, len, len, 0 };
  vj_frame_copy( frame->data, slice_frame, strides );  

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

	if( interpolate )
		motionmap_interpolate_frame( frame, N__, n__ );

	if( motion )
		motionmap_store_frame( frame );

}

