/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
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
#include "slice.h"
#include "motionmap.h"

typedef struct {
    uint8_t *slice_frame[4];
    int *slice_xshift;
    int *slice_yshift;
    int frame_periods;
    int current_period;
    int n__;
    int N__;
    void *motionmap;
} slice_t;

void slice_recalc(slice_t *s, int width, int height, int val);

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
    ve->limits[1][1] = 8 * 30;
    ve->defaults[0] = 63;
    ve->defaults[1] = 0;
    ve->description = "Slice Window";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->motion = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Slices", "Slice Period");

    return ve;
}

void *slice_malloc(int width, int height)
{
    slice_t *s = (slice_t*) vj_calloc(sizeof(slice_t));
    if(!s) {
        return NULL;
    }
    s->slice_frame[0] = (uint8_t*)vj_malloc( sizeof(uint8_t) * (width * height * 4));
    if(!s->slice_frame[0]) {
	    free(s);
        return NULL;
    }
    s->slice_frame[1] = s->slice_frame[0] + (width * height);
    s->slice_frame[2] = s->slice_frame[1] + (width * height);
	s->slice_frame[3] = s->slice_frame[2] + (width * height);
    s->slice_xshift = (int*) vj_malloc(sizeof(int) * height);
    if(!s->slice_xshift) {
        free(s->slice_frame[0]);
        free(s);
        return NULL;
    }
    s->slice_yshift = (int*) vj_malloc(sizeof(int) * width);
    if(!s->slice_yshift) {
        free(s->slice_frame[0]);
        free(s->slice_xshift);
        free(s);
        return NULL;
    }

    slice_recalc(s, width,height, 63);
    
    return (void*) s;
}


void slice_free(void *ptr) {
    slice_t *s = (slice_t*) ptr;
    free(s->slice_frame[0]);
    free(s->slice_xshift);
    free(s->slice_yshift);
    free(s);
}

/* much like the bathroom window, width height indicate block size within frame */
void slice_recalc(slice_t *s, int width, int height, int val) {
  unsigned int x,y,dx,dy,r;

  int *slice_xshift = s->slice_xshift;
  int *slice_yshift = s->slice_yshift;

  const int hval = val >> 1;
  for(x = dx = 0; x < width; x++) 
  {
    if(dx==0)
	{ 
		r = ((rand() & val))-( hval + 1); 
		dx = 8 + (rand() & ( hval - 1));
	}
	else
	{
		 dx--;
	}
    slice_yshift[x] = r;
  }
 
  for(y=dy=0; y < height; y++) {
  	if(dy==0)
  	{
		r = (rand() & val)-(hval + 1);
		dy = 8 + (rand() & (hval-1));
  	}
  	else
  	{
		dy--;
  	}
  	slice_xshift[y] = r;
  }
}

void slice_apply(void *ptr, VJFrame *frame, int *args ) {
    int val = args[0];
    int re_init = args[1];
	unsigned int x,y,dx,dy;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *A = frame->data[3];

    slice_t *s = (slice_t*) ptr;

	int interpolate = 1;
	int tmp1 = val;
	int tmp2 = re_init;
	int motion = 0;

    uint8_t **slice_frame = s->slice_frame;
    int *slice_xshift = s->slice_xshift;
    int *slice_yshift = s->slice_yshift;

	if( s->frame_periods != re_init ) {
		s->frame_periods = re_init;
		s->current_period = s->frame_periods;
	}

	if( motionmap_active(s->motionmap))
	{
		motionmap_scale_to(s->motionmap, 128,1, 2, 0, &tmp1, &tmp2, &(s->n__) , &(s->N__) );
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
		s->n__ = 0;
		s->N__ = 0;
	}

	if( s->n__ == s->N__ || s->n__ == 0 )
		interpolate = 0;

  if( motionmap_active(s->motionmap) ) {
	 if(tmp2==1) slice_recalc(s,width,height,tmp1);
  }
  else {
		s->current_period --;
	  
		if (s->current_period == 0 ) {
			slice_recalc(s,width,height,tmp1);
		}
		if( s->current_period <= 0 ) {
			s->current_period = s->frame_periods;
		}
  }

	int strides[4] = { len, len, len, 0 };
	vj_frame_copy( frame->data, slice_frame, strides );  

	for(y=0; y < height; y++){ 
	 	for(x=0; x < width; x++) {
			dx = x + slice_xshift[y];
			dy = y + slice_yshift[x];
			if(dx < width && dy < height && dx >= 0 && dy >= 0)
			{
				Y[(y*width)+x] = slice_frame[0][(dy*width)+dx];
				Cb[(y*width)+x] = slice_frame[1][(dy*width)+dx];
				Cr[(y*width)+x] = slice_frame[2][(dy*width)+dx];
				//A[(y*width)+x] = slice_frame[3][(dy*width)+dx];
			}
		}
	}


	if( interpolate )
		motionmap_interpolate_frame( s->motionmap, frame, s->N__, s->n__ );

	if( motion )
		motionmap_store_frame( s->motionmap, frame );

}

int slice_request_fx(void) {
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void slice_set_motionmap(void *ptr, void *priv)
{
    slice_t *s = (slice_t*) ptr;
    s->motionmap = priv;
}

