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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "transblend.h"
#include <libvje/effects/common.h>

vj_effect *transblend_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
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
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Width", "Height", "Ax offset", "Ay offset" , "Bx offset", "By offset");
    ve->sub_format = 0;
    ve->extra_frame = 1;
	ve->has_user = 0;
    return ve;
}



void transblend_apply( VJFrame *frame, VJFrame *frame2, int width,
		      int height, int type, int twidth, int theight,
		      int x1, int y1, int x2, int y2)
{

    int x, y;
    int p, q;
    int uv_width = frame->uv_width;

    int uvy1, uvy2, uvx1, uvx2;
    uint8_t *Y, *Cb, *Cr, *Y2, *Cb2, *Cr2;
    pix_func_Y func_y = get_pix_func_Y((const int) type);
    pix_func_C func_c = get_pix_func_C((const int) type);

    uvy1 = y1 >> frame->shift_v;
    uvy2 = y2 >> frame->shift_v;
    uvx1 = x1 >> frame->shift_h;
    uvx2 = x2 >> frame->shift_h;

  	Y = frame->data[0];
	Cb = frame->data[1];
	Cr = frame->data[2];
	Y2 = frame2->data[0];
	Cb2 = frame2->data[1];
	Cr2 = frame2->data[2];



    if( (theight + y2) > height ) y2 = (height-theight);
    if( (twidth + x2) > width) x2 = (width-twidth);

    for (y = 0; y < theight; y++) {
	for (x = 0; x < twidth; x++) {
	    p = (y2 + y) * width + x2 + x;
	    q = (y1 + y) * width + x1 + x;
	    Y[p] = func_y(Y[p], Y2[q]);
	}
    }

    for (y = 0; y < (theight >> frame->shift_v); y++) {
	for (x = 0; x < (twidth >> frame->shift_h); x++) {
	    p = (uvy2 + y) * uv_width + uvx2 + x;
	    q = (uvy1 + y) * uv_width + uvx1 + x;
	    Cb[p] = func_c( Cb[p], Cb2[q]);
	    Cr[p] = func_c( Cr[p], Cr2[q]);
	}
    }

}
void transblend_free(){}
