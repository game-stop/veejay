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

#include "transblend.h"
#include "../effects/common.h"
#include <stdlib.h>

vj_effect *transblend_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 1;
    ve->defaults[1] = 50;
    ve->defaults[2] = 50;
    ve->defaults[3] = 50;
    ve->defaults[4] = 50;
    ve->defaults[5] = 50;
    ve->defaults[6] = 50;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 30;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = width;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = height;
    ve->limits[0][3] = 1;
    ve->limits[1][3] = width;
    ve->limits[0][4] = 1;
    ve->limits[1][4] = height;
    ve->limits[0][5] = 1;
    ve->limits[1][5] = width;
    ve->limits[0][6] = 1;
    ve->limits[1][6] = height;
    ve->description = "Transition Translate Blend";
    ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->has_internal_data = 0;
    return ve;
}



void transblend_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		      int height, int type, int twidth, int theight,
		      int x1, int y1, int x2, int y2)
{

    int x, y;
    int p, q;
    int uv_width = width >> 1;

    int uvy1, uvy2, uvx1, uvx2;

    pix_func_Y func_y = get_pix_func_Y((const int) type);
    pix_func_C func_c = get_pix_func_C((const int) type);

    uvy1 = y1 >> 1;
    uvy2 = y2 >> 1;
    uvx1 = x1 >> 1;
    uvx2 = x2 >> 1;
    if( (theight + y2) > height ) y2 = (height-theight);
    if( (twidth + x2) > width) x2 = (width-twidth);

    for (y = 0; y < theight; y++) {
	for (x = 0; x < twidth; x++) {
	    p = (y2 + y) * width + x2 + x;
	    q = (y1 + y) * width + x1 + x;
	    yuv1[0][p] = func_y(yuv1[0][p], yuv2[0][q]);
	}
    }
    for (y = 0; y < (theight >> 1); y++) {
	for (x = 0; x < (twidth >> 1); x++) {
	    p = (uvy2 + y) * uv_width + uvx2 + x;
	    q = (uvy1 + y) * uv_width + uvx1 + x;
	    yuv1[1][p] = func_c(yuv1[1][p], yuv2[1][q]);
	    yuv2[2][p] = func_c(yuv2[2][p], yuv2[2][q]);
	}
    }

}
void transblend_free(){}
