/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <elburg@hio.hen.nl>
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
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "noisepencil.h"

static uint8_t *Yb_frame = NULL;

vj_effect *noisepencil_init(int width , int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 0;	/* type */
    ve->defaults[1] = 1000;
    ve->defaults[2] = 68;
    ve->defaults[3] = 110;
  
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 10000;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
	ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Amplification", "Min Threshold", "Max Threshold");
    ve->description = "Noise Pencil";

    ve->extra_frame = 0;
    ve->sub_format = 0;
	ve->has_user = 0;
    return ve;
}

int  noisepencil_malloc(int width,int height)
{
  Yb_frame = (uint8_t *) vj_calloc( sizeof(uint8_t) * width * height);
  if(!Yb_frame) return 0;
  return 1;
}

void noisepencil_free() {
  if(Yb_frame) free(Yb_frame);
  Yb_frame = NULL;
}

void noisepencil_1_apply(uint8_t *src[3], int width, int height, int coeef, int min_t, int max_t ) {

    int r, c;
    double k = (coeef/100.0);
    int len = (width*height);
	uint8_t tmp;

	for( r = 0; r < width ; r ++ )
	{
		Yb_frame[ r ] = (src[0][r] + src[0][r+width])>>1;
		if(Yb_frame[r] < min_t || Yb_frame[r] > max_t)
		{
			Yb_frame[r] = 0;
		} 
	}

    for (r = 0; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		tmp = (src[0][r + c - 1] +
				  src[0][r + c] +
				  src[0][r + c + 1]
		    ) / 3;

		if( tmp >= min_t && tmp <= max_t )
		{
			Yb_frame[r + c ] = tmp;
		}
		else
		{
			Yb_frame[r + c ] = 0;
		}
	}
    }
    
    for(c=0; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	    if( Yb_frame[c] != 0) src[0][c] = (Yb_frame[c] - src[0][c]) * k;
	}

}
void noisepencil_2_apply(uint8_t *src[3], int width, int height, int coeef , int min_t, int max_t) {

    int r, c;
    double k = (coeef/1000.0);
    int len = (width*height)-width;
    uint8_t tmp;

    for( r = 0; r < width; r++)
	{
		Yb_frame[ r ] = (src[0][r + width] + src[0][r]) >> 1;
		if(Yb_frame[r] < min_t || Yb_frame[r] > max_t)
		{
			Yb_frame[r] = 0;
		}
	}

    for (r = width; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		tmp = (src[0][r - width + c - 1] +
				  src[0][r - width + c] +
				  src[0][r - width + c + 1] +
				  src[0][r + width + c - 1] +
				  src[0][r + width + c] +
				  src[0][r + width + c + 1] +
				  src[0][r + c] +
				  src[0][r + c + 1] +
				  src[0][r + c - 1]  
		    ) / 9;

		if( tmp >= min_t && tmp <= max_t)
		{
			Yb_frame[c + r ] = tmp;
		}	
		else
		{
			Yb_frame[c + r ] = 0;
		}
	}
    }

    for(c=0; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	  if(Yb_frame[c] != 0) src[0][c] = (Yb_frame[c] - src[0][c]) * k;
	}

}

void noisepencil_3_apply(uint8_t *src[3], int width, int height, int coeef, int min_t , int max_t  ) {

    int r, c;
    double k = (coeef/1000.0);
    int len = (width*height)-width;
    uint8_t tmp;
    for ( r = 0 ; r < width ; r++)
	{
		Yb_frame[r] = ( src[0][r + width] + src[0][r] ) >> 1;
		if( Yb_frame[r] <min_t || Yb_frame[r] > max_t) {
			Yb_frame[r] = 0;
		}
	}

    for (r = width; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		tmp = (src[0][r - width + c - 1] +
				  src[0][r - width + c] +
				  src[0][r - width + c + 1] +
				  src[0][r + width + c - 1] +
				  src[0][r + width + c] +
				  src[0][r + width + c + 1] +
				  src[0][r + c] +
				  src[0][r + c + 1] +
				  src[0][r + c - 1]  
		    ) / 9;

		if( min_t >= tmp && tmp <= max_t)
		{
			Yb_frame[c + r] = tmp;
		}
		else 
		{
			Yb_frame[c + r] = 0;
		}

	}
    }

    for(c=0; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	  if(Yb_frame[c] > 0 ) src[0][c] = (src[0][c] - Yb_frame[c]) * k;
	}

}

void noisepencil_4_apply(uint8_t *src[3], int width, int height, int coeef, int min_t, int max_t ) {

    int r, c;
    double k = (coeef/1000.0);
    int len = (width*height)-width;
    uint8_t tmp;

    for ( r = 0; r < width ; r++ )
	{
		Yb_frame[r] = (src[0][ r + width ] + src[0][ r ] ) >> 1;
		if(Yb_frame[r] < min_t || Yb_frame[r] > max_t )
		{
			Yb_frame[r] = 0;
		}
	}

    for (r = width; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		tmp  = (src[0][r - width + c - 1] +
				  src[0][r - width + c] +
				  src[0][r - width + c + 1] +
				  src[0][r + width + c - 1] +
				  src[0][r + width + c] +
				  src[0][r + width + c + 1] +
				  src[0][r + c] +
				  src[0][r + c + 1] +
				  src[0][r + c - 1]  
		    ) / 9;

		if( tmp >= min_t && tmp <= max_t )
		{
			Yb_frame[r] = tmp;
		}
		else
		{
			Yb_frame[r] = 16;
		}
	}
    }

	

    for(c=0; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	  if(Yb_frame[c] > 0) src[0][c] = (src[0][c] - Yb_frame[c]) * k;
	}

}

/* with min_t -> max_t select the threshold to 'noise ' */
void noisepencil_apply(VJFrame *frame, int width, int height, int type, int coeef, int min_t,
	int max_t) {

	switch(type) {
		case 0:
		noisepencil_1_apply(frame->data,width,height,coeef,min_t,max_t);	break;
		case 1:
		noisepencil_2_apply(frame->data,width,height,coeef,min_t,max_t);	break;
		case 2:
		noisepencil_3_apply(frame->data,width,height,coeef,min_t,max_t);	break;
		case 3:
		noisepencil_4_apply(frame->data,width,height,coeef,min_t,max_t);	break;
	}
}
