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

#include "videomask.h"
#include "../../config.h"
#include <stdlib.h>
#include "../subsample.h"


vj_effect *videomask_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150; /* threshold */
    ve->description = "Luminance Map";
    ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->has_internal_data = 0;
    return ve;
}



void videomask_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		   int height, int videomask)
{
    unsigned int i, op0, op1;
    unsigned int len = width * height;
    const uint8_t pure_white_y  = 235;
    const uint8_t pure_white_c  = 240;

    for (i = 0; i < len; i++) {
	op1 = yuv2[0][i];
	op0 = 255 - op1;
	yuv1[0][i] = (op0 * yuv1[0][i] + op1 * pure_white_y)>>8;
	}
    len >>= 2;		

    for (i = 0; i < len; i++) {
	op1 = yuv1[1][i];
	op0 = 255 - op1;
	yuv1[1][i] = (op0 * yuv1[1][i] + op1 * pure_white_c)>>8;
	op1 = yuv1[2][i];
	op0 = 255 - op1;
	yuv1[2][i] = (op0 * yuv1[2][i] + op1 * pure_white_c) >> 8;
    } 
}


/*

    1. load 1st input reference to mm0
    2. load 2nd input reference to mm1
    3. Output reference to mm2
    4. load op0 to mm7
    5. load op1 to mm6
    4. unpack byte 1st input to word on mm3 
    5. unpack byte 2nd input to word on mm4
    6. multiply mm7 with mm3
    7. multiply mm6 with mm4
    8. add mm3 + mm4
    9. shift rotate right with 8 ( / 256)

	


*/
void videomask_free(){}
