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
#include "mirrors2.h"

vj_effect *mirrors2_init(int w, int h)
{

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4;
    ve->limits[0][0] = 0;	/* horizontal or vertical mirror */
    ve->limits[1][0] = 5;
    ve->sub_format = 1;
    ve->description = "Mirror";
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "H or V mode");
    return ve;
}



void mirror_multi_dr(uint8_t * yuv[3], int width, int height)
{

    unsigned int x, y;
    unsigned int hlen = height / 2;
    unsigned int vlen = width / 2;
    unsigned int yi1, yi2;
    uint8_t p, cr, cb;

    for (y = hlen; y < height; y++) {
	yi1 = y * width;
	yi2 = (height - y - 1) * width;
	for (x = vlen; x < width; x++) {
	    p = yuv[0][yi1 + x];
	    yuv[0][yi1 + x + (width - x - 1)] = p;
	    yuv[0][yi2 + x] = p;
	    yuv[0][yi2 + (width - x - 1)] = p;
	    cb = yuv[1][yi1 + x];
	    cr = yuv[2][yi1 + x];
	    yuv[1][yi1 + x + (width - x - 1)] = cb;
	    yuv[1][yi2 + x] = cb;
	    yuv[1][yi2 + (width - x - 1)] = cb;
	    yuv[2][yi1 + x + (width - x - 1)] = cr;
	    yuv[2][yi2 + x] = cr;
	    yuv[2][yi2 + (width - x - 1)] = cr;
	}
    }
}

void mirror_multi_u(uint8_t * yuv[3], int width, int height)
{
    unsigned int x, y;

    unsigned int yi1, yi2;
    unsigned int hlen = height / 2;

    uint8_t p, cb, cr;

    for (y = 0; y < hlen; y++) {
	yi1 = y * width;
	yi2 = (height - y - 1) * width;
	for (x = 0; x < width; x++) {
	    p = yuv[0][yi1 + x];
	    yuv[0][yi2 + x] = p;
	    cb = yuv[1][yi1 + x];
	    cr = yuv[2][yi1 + x];
	    yuv[1][yi2 + x] = cb;
	    yuv[2][yi2 + x] = cr;
	}
    }
}

void mirror_multi_d(uint8_t * yuv[3], int width, int height)
{
    unsigned int x, y;
    unsigned int yi1 = 0;
    unsigned int yi2 = 0;
    unsigned int hlen = height / 2;
    uint8_t p, cb, cr;

    for (y = hlen; y < height; y++) {
	yi1 = y * width;
	yi2 = (height - y - 1) * width;

	for (x = 0; x < width; x++) {
	    p = yuv[0][yi1 + x];
	    yuv[0][yi2 + x] = p;
	    cb = yuv[1][yi1 + x];
	    cr = yuv[2][yi1 + x];
	    yuv[1][yi2 + x] = cb;
	    yuv[2][yi2 + x] = cr;
	}
    }
}

void mirror_multi_l(uint8_t * yuv[3], int width, int height)
{
    unsigned int x, y;

    unsigned int yi;
    unsigned int vlen = width / 2;
    uint8_t p, cb, cr;
    for (y = 0; y < height; y++) {
	yi = y * width;
	for (x = vlen; x < width; x++) {
	    p = yuv[0][yi + x];
	    yuv[0][yi + (width - x - 1)] = p;
	    cb = yuv[1][yi + x];
	    yuv[1][yi + (width - x - 1)] = cb;
	    cr = yuv[2][yi + x];
	    yuv[2][yi + (width - x - 1)] = cr;
	}
    }
}

void mirror_multi_r(uint8_t * yuv[3], int width, int height)
{
    unsigned int x, y;

    unsigned int yi;

    uint8_t p, cb, cr;

    for (y = 0; y < height; y++) {
	yi = y * width;
	for (x = 0; x < width; x++) {
	    p = yuv[0][yi + x];
	    yuv[0][yi + (width - x - 1)] = p;
	    cb = yuv[1][yi + x];
	    cr = yuv[2][yi + x];
	    yuv[1][yi + (width - x - 1)] = cb;
	    yuv[2][yi + (width - x - 1)] = cr;
	}
    }
}

void mirror_multi_ur(uint8_t * yuv[3], int width, int height)
{
    unsigned int x, y;

    unsigned int yi, yi2;
    unsigned int vlen = width / 2;
    unsigned int hlen = width / 2;
    uint8_t p, cb, cr;

    for (y = hlen; y < height; y++) {
	yi = y * width;
	yi2 = (height - y - 1) * width;
	for (x = 0; x < vlen; x++) {
	    p = yuv[0][yi + x];
	    yuv[0][yi + (width - x - 1)] = p;
	    yuv[0][yi2 + x] = p;
	    yuv[0][yi2 + x + (width - x - 1)] = p;
	    cb = yuv[1][yi + x];
	    cr = yuv[2][yi + x];
	    yuv[1][yi + (width - x - 1)] = cb;
	    yuv[1][yi2 + x] = cb;
	    yuv[1][yi2 + x + (width - x - 1)] = cb;
	    yuv[2][yi + (width - x - 1)] = cr;
	    yuv[2][yi2 + x] = cr;
	    yuv[2][yi2 + x + (width - x - 1)] = cr;
	}
    }
}

void mirrors2_apply( VJFrame *frame, int width, int height, int type)
{
    switch (type) {
    case 0:
	mirror_multi_dr(frame->data, width, height);
	break;
    case 1:
	mirror_multi_ur(frame->data, width, height);
	break;
    case 2:
	mirror_multi_u(frame->data, width, height);
	break;
    case 3:
	mirror_multi_d(frame->data, width, height);
	break;
    case 4:
	mirror_multi_l(frame->data, width, height);
	break;
    case 5:
	mirror_multi_r(frame->data, width, height);
	break;
    }
}
void mirrors2_free(){}
