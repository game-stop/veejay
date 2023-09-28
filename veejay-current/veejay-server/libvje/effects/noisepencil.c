/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "noisepencil.h"

typedef struct {
    uint8_t *Yb_frame;
} noisepencil_t;

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
    ve->limits[1][0] = 4;
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
    
	ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0, "1x3 NonZero", "3x3 NonZero","3x3 Invert", "3x3 Add", "1x3 All" );
    

	
	return ve;
}

void *noisepencil_malloc(int width,int height)
{
    noisepencil_t *n = (noisepencil_t*) vj_calloc(sizeof(noisepencil_t));
    if(!n) {
        return NULL;
    }

	n->Yb_frame = (uint8_t *) vj_calloc( sizeof(uint8_t) * (width * height));
	if(!n->Yb_frame) {
        free(n);
        return NULL;
    }

	return (void*) n;
}

void noisepencil_free(void *ptr) {
    noisepencil_t *n = (noisepencil_t*) ptr;
    free(n->Yb_frame);
    free(n);
}

static void noisepencil_1_apply(uint8_t *Yb_frame, uint8_t *src[3], int width, int height, int coeef, int min_t, int max_t ) {

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

static void noisepencil_2_apply(uint8_t *Yb_frame, uint8_t *src[3], int width, int height, int coeef , int min_t, int max_t) {

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

static void noisepencil_3_apply(uint8_t *Yb_frame, uint8_t *src[3], int width, int height, int coeef, int min_t , int max_t  ) {

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

static void noisepencil_4_apply(uint8_t *Yb_frame, uint8_t *src[3], int width, int height, int coeef, int min_t, int max_t ) {

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

static void noisepencil_5_apply(uint8_t *Yb_frame, uint8_t *src[3], int width, int height, int coeef, int min_t , int max_t  ) {

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
	  src[0][c] = (src[0][c] - Yb_frame[c]) * k;
	}

}



/* with min_t -> max_t select the threshold to 'noise ' */
void noisepencil_apply(void *ptr, VJFrame *frame, int *args ) {
    int type = args[0];
    int coeef = args[1];
    int min_t = args[2];
    int max_t = args[3];

	const unsigned int width = frame->width;
	const unsigned int height = frame->height;

    noisepencil_t *n = (noisepencil_t*) ptr;
        
        
    switch(type) {
		case 0:
		noisepencil_1_apply(n->Yb_frame, frame->data,width, height, coeef,min_t,max_t);
			break;
		case 1:
		noisepencil_2_apply(n->Yb_frame, frame->data, width, height, coeef,min_t,max_t);
			break;
		case 2:
		noisepencil_3_apply(n->Yb_frame, frame->data, width, height, coeef,min_t,max_t);
			break;
		case 3:
		noisepencil_4_apply(n->Yb_frame, frame->data, width, height, coeef,min_t,max_t);
			break;
		case 4:
		noisepencil_5_apply(n->Yb_frame, frame->data, width, height, coeef,min_t,max_t);
			break;
	}
}
