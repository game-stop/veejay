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

#include "softblur.h"
#include <stdlib.h>


vj_effect *softblur_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1; /* 3*/
    ve->description = "Soft Blur (1x3) and (3x3)";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}

void softblur1_apply( VJFrame *frame, int width, int height)
{
    int r, c;
    int len = (width * height);
 	uint8_t *Y = frame->data[0];
    for (r = 0; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
	    Y[c + r] = (Y[r + c - 1] +
			      Y[r + c] +
			      Y[r + c + 1]
				) / 3;
				
	}
    }

}

void softblur3_apply(VJFrame *frame, int width, int height ) {
	int r,c;
	uint8_t *Y = frame->data[0];
	const int len = frame->len;
	
	for(c = 1; c < width - 1; c ++ )
	   Y[c ] = (Y[c - 1] +
	      Y[c ] +
	      Y[c + 1]
		) / 3;




	for(r=width; r < (len-width); r+=width) {
		for(c=1; c < (width-1); c++) {
			Y[r+c] = ( 	Y[r - width + c - 1] +
				   	Y[r - width + c ] +
					Y[r + c + 1] +
					Y[r - width + c] +
					Y[r + c] + 
					Y[r + c + 1] +
					Y[r + width + c - 1] +
					Y[r + width + c] +
					Y[r + width + c + 1]  ) / 9;

		}
	}

	for( c = (len-width) ; c < len; c ++ )
	  Y[c ] = (Y[c - 1] +
	      Y[c] +
	      Y[c + 1]
		) / 3;


}


void softblur_apply(VJFrame *frame, int width, int height, int type)
{
    switch (type) {
    case 0:
	softblur1_apply(frame, width, height);
	break;
	break;
    case 1:
	softblur3_apply(frame, width, height);
	break;
    }
}

