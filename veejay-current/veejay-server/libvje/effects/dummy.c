/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2010 Niels Elburg <nwelburg@gmail.com>
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
#include <config.h>
#include <stdlib.h>
#include <libvje/vje.h>
#include <libvje/effects/common.h>

vj_effect *dummy_init(int w, int h)
{

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 3;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 7;

    ve->description = "Dummy Frame";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user= 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Color");
	return ve;
}
void dummy_apply( VJFrame *frame, int width, int height, int color)
{
    const int len = frame->len;
    const int uv_len = frame->uv_len;
    char colorCb, colorCr, colorY;

    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    colorY = bl_pix_get_color_y(color);
    colorCb = bl_pix_get_color_cb(color);
    colorCr = bl_pix_get_color_cr(color);

    veejay_memset( Y, colorY, len);
    veejay_memset( Cb,colorCb,uv_len);
    veejay_memset( Cr,colorCr,uv_len);
}

void dummy_rgb_apply( VJFrame *frame, int width, int height, int r,int g, int b)
{
    	const int len = frame->len;
	const int uv_len = frame->uv_len;
	int colorCb, colorCr, colorY;

 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	_rgb2yuv(r,g,b,colorY,colorCb,colorCr);
  
 	veejay_memset( Y, colorY, len);
   	veejay_memset( Cb,colorCb,uv_len);
   	veejay_memset( Cr,colorCr,uv_len);
}
void dummy_free(){}
