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
#include <libvjmem/vjmem.h>
#include "noiseadd.h"

static uint8_t *Yb_frame;

vj_effect *noiseadd_init(int width , int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 0;	/* type */
    ve->defaults[1] = 1000;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 5000;
    ve->description = "Amplify low noise";

    ve->extra_frame = 0;
    ve->sub_format = -1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Amplification");


	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
		"1x3 Mask", "3x3 Mask" ,"3x3 Inverted Mask" );

    return ve;
}

int noiseadd_malloc(int width, int height)
{
  
  Yb_frame = (uint8_t *) vj_calloc( sizeof(uint8_t) * width * height);
  if(!Yb_frame) return 0;
  return 1;
}

void noiseadd_free() {
  if(Yb_frame) free(Yb_frame);
  Yb_frame = NULL;
}

static void noiseblur1x3_maskapply(VJFrame* frame, int coeef ) {

    int r, c;
    double k = (coeef/100.0);
    uint8_t d;
	const unsigned int width = frame->width;
    const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];

    for (r = 0; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		Yb_frame[c + r] = (Y[r + c - 1] +
				  Y[r + c] +
				  Y[r + c + 1]
		    ) / 3;
	}
    }
    
    for(c=0; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	  d = (Yb_frame[c] - Y[c]) * k;
	  Y[c] = d;
	}

}

static void noiseblur3x3_maskapply(VJFrame* frame, int coeef ) {

    int r, c;
    const double k = (coeef/1000.0);
    uint8_t d;
	const unsigned int width = frame->width;
    const unsigned int len = (frame->len)-width;
	uint8_t *Y = frame->data[0];

    for (r = width; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		Yb_frame[c + r] = (Y[r - width + c - 1] +
				  Y[r - width + c] +
				  Y[r - width + c + 1] +
				  Y[r + width + c - 1] +
				  Y[r + width + c] +
				  Y[r + width + c + 1] +
				  Y[r + c] +
				  Y[r + c + 1] +
				  Y[r + c - 1]  
		    ) / 9;
	}
    }

    for(c=width; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	  d = (Yb_frame[c] - Y[c]) * k;
	  Y[c] = d;
	}

}

static void noiseneg3x3_maskapply(VJFrame *frame, int coeef ) {

    int r, c;
    const double k = (coeef/1000.0);
    uint8_t d;
	const unsigned int width = frame->width;
    const unsigned int len = (frame->len)-width;
	uint8_t *Y = frame->data[0];


    for (r = width; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		Yb_frame[c + r] = 255 - ((Y[r - width + c - 1] +
				  Y[r - width + c] +
				  Y[r - width + c + 1] +
				  Y[r + width + c - 1] +
				  Y[r + width + c] +
				  Y[r + width + c + 1] +
				  Y[r + c] +
				  Y[r + c + 1] +
				  Y[r + c - 1]  
		    )) / 9;
	}
    }

    for(c=width; c < len; c++) {
	  /* get higher signal frequencies and*/	
	  /* multiply result with coeffcient to get d*/
	  d = (Y[c] - Yb_frame[c]) * k;
	  Y[c] = d;
	}

}

/*
static void noiseadd3x3_maskapply(VJFrame *frame, int coeef ) {

    int r, c;
    const double k = (coeef/1000.0);
    uint8_t d;
	const int width = frame->width;
    const int len = (frame->len)-width;
	uint8_t *Y = frame->data[0];


    for (r = width; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
		Yb_frame[c + r] = (Y[r - width + c - 1] +
				  Y[r - width + c] +
				  Y[r - width + c + 1] +
				  Y[r + width + c - 1] +
				  Y[r + width + c] +
				  Y[r + width + c + 1] +
				  Y[r + c] +
				  Y[r + c + 1] +
				  Y[r + c - 1]  
		    ) / 9;
	}
    }

    for(c=width; c < len; c++) {
	  // get higher signal frequencies and
	  // multiply result with coeffcient to get d
	  d = (Y[c] - Yb_frame[c]) * k;
	  Y[c] = d;
	}

}
*/

void noiseadd_apply( VJFrame *frame, int type, int coeef) {

	switch(type) {
	case 0:
	noiseblur1x3_maskapply(frame, coeef);	break;
	case 1:
	noiseblur3x3_maskapply(frame, coeef);	break;
	case 2:
	noiseneg3x3_maskapply(frame, coeef);	 break;
	}
}
