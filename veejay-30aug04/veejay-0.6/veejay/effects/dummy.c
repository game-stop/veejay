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

#include "dummy.h"
#include "common.h"
#include <stdlib.h>
vj_effect *dummy_init(int w, int h)
{

    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    /*
       ve->param_description = (char**)malloc(sizeof(char)* ve->num_params);
       for(i=0; i < ve->num_params; i++) {
       ve->param_description[i] = (char*)malloc(sizeof(char) * 100);
       }
       sprintf(ve->param_description[0], "Static color");
     */
    ve->defaults[0] = 3;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 7;

    ve->description = "Dummy Frame";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}

void dummy_apply(uint8_t * yuv1[3], int width, int height, int color)
{
    unsigned int len = (width * height);
    char colorCb, colorCr, colorY;

    colorY = bl_pix_get_color_y(color);
    colorCb = bl_pix_get_color_cb(color);
    colorCr = bl_pix_get_color_cr(color);
  
    memset( yuv1[0], colorY, len);
    len = (width * height) >> 2;
    memset( yuv1[1],colorCb,len);
    memset( yuv1[2],colorCr,len);
	/*
    for (i = 0; i < len; i++) {
	yuv1[1][i] = colorCb;
	yuv1[2][i] = colorCr;
    }
	*/
}
void dummy_free(){}
