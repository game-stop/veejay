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

#include <stdlib.h>
#include "../vj-effect.h"
#include <stdint.h>

static uint8_t *Yb_frame;

vj_effect *noiseadd_init(int width , int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 0;	/* type */
    ve->defaults[1] = 1000;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 10000;
    ve->description = "Amplify low noise";

    ve->extra_frame = 0;
    ve->sub_format = 0;
    ve->has_internal_data = 1;

    return ve;
}

int noiseadd_malloc(int width, int height)
{
  
  Yb_frame = (uint8_t *) vj_malloc( sizeof(uint8_t) * width * height);
  memset(Yb_frame, 0, width*height);
  if(!Yb_frame) return 0;
  return 1;
}

void noiseadd_free() {
  if(Yb_frame) free(Yb_frame);
}

void noiseblur1x3_maskapply(uint8_t *src[3], int width, int height, int coeef ) {

    int r, c;
    double k = (coeef/100.0);
    uint8_t d;
    uint8_t b;
    const int len = (width*height);

    for (r = 0; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		Yb_frame[c + r] = (src[0][r + c - 1] +
				  src[0][r + c] +
				  src[0][r + c + 1]
		    ) / 3;
	}
    }
    
    for(c=0; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	  d = (Yb_frame[c] - src[0][c]) * k;
	  src[0][c] = d;
	}

}
void noiseblur3x3_maskapply(uint8_t *src[3], int width, int height, int coeef ) {

    int r, c;
    const double k = (coeef/1000.0);
    uint8_t d;
    uint8_t b;
    const int len = (width*height)-width;


    for (r = width; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		Yb_frame[c + r] = (src[0][r - width + c - 1] +
				  src[0][r - width + c] +
				  src[0][r - width + c + 1] +
				  src[0][r + width + c - 1] +
				  src[0][r + width + c] +
				  src[0][r + width + c + 1] +
				  src[0][r + c] +
				  src[0][r + c + 1] +
				  src[0][r + c - 1]  
		    ) / 9;
	}
    }

    for(c=width; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	  d = (Yb_frame[c] - src[0][c]) * k;
	  src[0][c] = d;
	}

}

void noiseneg3x3_maskapply(uint8_t *src[3], int width, int height, int coeef ) {

    int r, c;
    const double k = (coeef/1000.0);
    uint8_t d;
    uint8_t b;
    int len = (width*height)-width;


    for (r = width; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		Yb_frame[c + r] = (src[0][r - width + c - 1] +
				  src[0][r - width + c] +
				  src[0][r - width + c + 1] +
				  src[0][r + width + c - 1] +
				  src[0][r + width + c] +
				  src[0][r + width + c + 1] +
				  src[0][r + c] +
				  src[0][r + c + 1] +
				  src[0][r + c - 1]  
		    ) / 9;
	}
    }

    for(c=width; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	  d = (src[0][c] - Yb_frame[c]) * k;
	  src[0][c] = d;
	}

}

void noiseadd3x3_maskapply(uint8_t *src[3], int width, int height, int coeef ) {

    int r, c;
    const double k = (coeef/1000.0);
    uint8_t d;
    uint8_t b;
    const int len = (width*height)-width;


    for (r = width; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		Yb_frame[c + r] = (src[0][r - width + c - 1] +
				  src[0][r - width + c] +
				  src[0][r - width + c + 1] +
				  src[0][r + width + c - 1] +
				  src[0][r + width + c] +
				  src[0][r + width + c + 1] +
				  src[0][r + c] +
				  src[0][r + c + 1] +
				  src[0][r + c - 1]  
		    ) / 9;
	}
    }

    for(c=width; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	  d = (src[0][c] - Yb_frame[c]) * k;
	  src[0][c] = d;
	}

}


void noiseadd_apply( VJFrame *frame, int width, int height, int type, int coeef) {

	switch(type) {
	case 0:
	noiseblur1x3_maskapply(frame->data,width,height,coeef);	break;
	case 1:
	noiseblur3x3_maskapply(frame->data,width,height,coeef);	break;
	case 2:
	noiseneg3x3_maskapply(frame->data,width,height,coeef);	 break;
	}
}
