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

#include "transop.h"
#include <stdlib.h>
#include <stdio.h>

vj_effect *transop_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 150;	/* opacity */
    ve->defaults[1] = 265;	/* twdith */
    ve->defaults[2] = 194;	/* theight */
    ve->defaults[3] = 59;	/* y1 */
    ve->defaults[4] = 58;	/* x1 */
    ve->defaults[5] = 45;	/* y2 */
    ve->defaults[6] = 58;	/* x2 */

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = width;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = height;
    ve->limits[0][3] = 1;
    ve->limits[1][3] = height;
    ve->limits[0][4] = 1;
    ve->limits[1][4] = width;
    ve->limits[0][5] = 1;
    ve->limits[1][5] = height;
    ve->limits[0][6] = 1;
    ve->limits[1][6] = width;
    ve->has_internal_data = 0;
    ve->description = "Transition Translate Opacity";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    return ve;
}

/* translate, twidth,theight: size of block to transform */
/* moves block(x2,y2) to (x1,y1), size of block to move is twidth * theight  */
void transop_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
		   int twidth, int theight, int x1, int y1, int x2, int y2,
		   int width, int height, int opacity)
{
    int x, y;
    int uv_width, uvy1, uvy2, uvx1, uvx2;
    unsigned int op0, op1;
    op1 = (opacity > 255) ? 255 : opacity;
    op0 = 255 - op1;
    /* translate yuv2 into yuv1, Luminance */
    if( (theight + y2) > height ) y2 = (height-theight);
    if( (twidth + x2) > width) x2 = (width-twidth);
    for (y = 0; y < theight; y++) {
	for (x = 0; x < twidth; x++) {
	    yuv1[0][((y2 + y) * width + x2 + x)] = (op0 * yuv1[0][((y2 + y) * width + x2 + x)] + op1 * yuv2[0][((y1 + y) * width + x1 + x)]) >> 8;	///235; 
 yuv1[1][((y2 + y) * width + x2 + x)] = (op0 * yuv1[1][((y2 + y) * width + x2 + x)] + op1 * yuv2[1][((y1 + y) * width + x1 + x)]) >> 8;	
 yuv1[2][((y2 + y) * width + x2 + x)] = (op0 * yuv1[2][((y2 + y) * width + x2 + x)] + op1 * yuv2[2][((y1 + y) * width + x1 + x)]) >> 8;	
	}
    }

	/*	
    uv_width = width / 2;
    uvy1 = y1 / 2;
    uvy2 = y2 / 2;
    uvx1 = x1 / 2;
    uvx2 = x2 / 2;

    for (y = 0; y < (theight/2); y++) {

	for (x = 0; x < (twidth/2
); x++) {
	    yuv1[1][((uvy2 + y) * uv_width + uvx2 + x)] = (op0 * yuv1[1][((uvy2 + y) * uv_width + uvx2 + x)] + op1 * yuv2[1][((uvy1 + y) * uv_width + uvx1 + x)]) >> 8;	///235;
	    yuv1[2][((uvy2 + y) * uv_width + uvx2 + x)] = (op0 * yuv1[2][((uvy2 + y) * uv_width + uvx2 + x)] + op1 * yuv2[2][((uvy1 + y) * uv_width + uvx1 + x)]) >> 8;	//235; 


	}
    }
	*/
}
void transop_free(){}
