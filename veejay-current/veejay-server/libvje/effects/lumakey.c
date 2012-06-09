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
#include "lumakey.h"
#include "common.h"

vj_effect *lumakey_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;	/* feather */
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;	/* threshold min */
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;	/* threshold max */
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 1;	/* distance */
    ve->limits[1][3] = width;
    ve->limits[0][4] = 0;	/* type */
    ve->limits[1][4] = 2;
    ve->defaults[0] = 255;
    ve->defaults[1] = 150;
    ve->defaults[2] = 200;
    ve->defaults[3] = 1;
    ve->defaults[4] = 1;
    ve->description = "Luma Key";
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Feather", "Min Threshold","Max Threshold","Distance", "Mode" );

    return ve;
}



void lumakey_simple(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		    int height, int threshold, int threshold2, int opacity)
{

    unsigned int x, y, len = width * height;
    uint8_t a1, a2;
    unsigned int op0, op1;
    uint8_t Y, Cb, Cr;
    op1 = (opacity > 255) ? 255 : opacity;
    op0 = 255 - op1;

    for (y = 0; y < len; y += width) {
	for (x = 0; x < width; x++) {
	    a1 = yuv1[0][x + y];
	    a2 = yuv2[0][x + y];
	    /*
	       if  ( a1 >= threshold && a1 <= threshold2) {
	       Y = (op0 * a1 + op1 * a2 )/255;
	       Cb = (op0 * yuv1[1][x+y] + op1 * yuv2[1][x+y])/255;
	       Cr =(op0 * yuv1[2][x+y] + op1 * yuv2[2][x+y])/255;

	       }
	     */
	    if (a1 >= threshold && a1 <= threshold2) {
		Y = (op0 * a1 + op1 * a2) >> 8;
		Cb = (op0 * yuv1[1][x + y] + op1 * yuv2[1][x + y]) >> 1;
		Cr = (op0 * yuv1[2][x + y] + op1 * yuv2[2][x + y]) >> 1;
		yuv1[0][x + y] = Y; 	// < 16 ? 16 : Y > 235 ? 235 : Y;
		yuv1[1][x + y] = Cb; 	//  < 16 ? 16 : Cb > 240 ? 240 : Cb;
		yuv1[2][x + y] = Cr; 	//  < 16 ? 16 : Cr > 240 ? 240 : Cr;
	    }

	}
    }
}


void lumakey_smooth(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		    int height, int threshold, int threshold2, int opacity,
		    int distance)
{

    unsigned int x, y = 0, len = width * height;
    uint8_t a1, a2;
    unsigned int op0, op1;
    uint8_t Y, Cb, Cr;
    unsigned int soft0, soft1;
    unsigned int t2, t3;
    uint8_t p1, p2;
    op1 = (opacity > 255) ? 255 : opacity;
    op0 = 255 - op1;

    soft0 = 255 / distance;
    soft1 = 255 - soft0;

    t2 = threshold - distance;	// 0 - 4
    t3 = threshold2 + distance;	// 3 + 4

    /* first row */

    if (t2 < 0)
	t2 = 0;
    if (t3 > 255)
	t3 = 255;

    for (x = 0; x < width; x++) {
	a1 = yuv1[0][x];
	a2 = yuv2[0][x];
	if (a1 >= threshold && a1 <= threshold2) {
	    Y = (op0 * a1 + op1 * a2) >> 8;
	    Cb = (op0 * yuv1[1][x] + op1 * yuv2[1][x]) >> 8;
	    Cr = (op0 * yuv1[2][x] + op1 * yuv2[2][x]) >> 8;

	    yuv1[0][x] = Y; 	//< 16 ? 16 : Y > 235 ? 235 : Y;
	    yuv1[1][x] = Cb; 	// < 16 ? 16 : Cb > 240 ? 240 : Cb;
	    yuv1[2][x] = Cr;	// < 16 ? 16 : Cr > 240 ? 240 : Cr;
	}
    }

    for (y = width; y < len - width; y += width) {
	/* first pixel in column */
	a1 = yuv1[0][y];
	a2 = yuv2[0][y];
	if (a1 >= threshold && a1 <= threshold2) {
	    Y = (op0 * a1 + op1 * a2)  >> 8;
	    Cb = (op0 * yuv1[1][y] + op1 * yuv2[1][y]) >> 8;
	    Cr = (op0 * yuv1[2][y] + op1 * yuv2[2][y]) >> 8;
	}
	/* rest of pixels in column */
	for (x = 1; x < width - 1; x++) {
	    a1 = yuv1[0][x + y];
	    a2 = yuv2[0][x + y];

	    if ((a1 >= t2 && a1 < threshold)
		|| (a1 > threshold2 && a1 <= t3)) {
		/* special case */
		p1 = (		/* calculate mean of a1 */
			 yuv1[0][y - width + x - 1] +
			 yuv1[0][y - width + x + 1] +
			 yuv1[0][y - width + x] +
			 yuv1[0][y + x] +
			 yuv1[0][y + x - 1] +
			 yuv1[0][y + x + 1] +
			 yuv1[0][y + width + x] +
			 yuv1[0][y + width + x + 1] +
			 yuv1[0][y + width + x - 1]
		    ) / 9;
		p2 = (		/* calculate mean of a1 */
			 yuv2[0][y - width + x - 1] +
			 yuv2[0][y - width + x + 1] +
			 yuv2[0][y - width + x] +
			 yuv2[0][y + x] +
			 yuv2[0][y + x - 1] +
			 yuv2[0][y + x + 1] +
			 yuv2[0][y + width + x] +
			 yuv2[0][y + width + x + 1] +
			 yuv2[0][y + width + x - 1]
		    ) / 9;

		yuv1[0][x + y] = (op0 * p1 + op1 * p2)  >> 8;
		yuv1[1][x + y] =
		    (op0 * yuv1[1][x + y] + op1 * yuv2[1][x + y])  >> 8;
		yuv1[2][x + y] =
		    (op0 * yuv1[2][x + y] + op1 * yuv2[2][x + y])  >> 8;

	    } else {
		if (a1 >= threshold && a1 <= threshold2) {
		    Y = (op0 * a1 + op1 * a2)  >> 8;
		    Cb = (op0 * yuv1[1][x + y] +
			  op1 * yuv2[1][x + y])  >> 8;
		    Cr = (op0 * yuv1[2][x + y] +
			  op1 * yuv2[2][x + y])  >> 8;

		    yuv1[0][x + y] = Y; 	// < 16 ? 16 : Y > 235 ? 235 : Y;
		    yuv1[1][x + y] = Cb; 	// < 16 ? 16 : Cb > 240 ? 240 : Cb;
		    yuv1[2][x + y] = Cr; 	// < 16 ? 16 : Cr > 240 ? 240 : Cr;
		}
	    }
	}
    }
    /* last row */
    for (x = len - width; x < len; x++) {
	a1 = yuv1[0][x];
	a2 = yuv2[0][x];
	if (a1 >= threshold && a1 <= threshold2) {
	    Y = (op0 * a1 + op1 * a2) >> 8;
	    Cb = (op0 * yuv1[1][x] + op1 * yuv2[1][x]) >> 8;
	    Cr = (op0 * yuv1[2][x] + op1 * yuv2[2][x]) >> 8;

	    yuv1[0][x] = Y; 	// < 16 ? 16 : Y > 235 ? 235 : Y;
	    yuv1[1][x] = Cb;	// < 16 ? 16 : Cb > 240 ? 240 : Cb;
	    yuv1[2][x] = Cr;	// < 16 ? 16 : Cr > 240 ? 240 : Cr;
	}
    }

}

void lumakey_smooth_white(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int threshold, int threshold2,
			  int opacity, int distance)
{

    unsigned int x, y = 0, len = width * height;
    uint8_t a1, a2;
    unsigned int op0, op1;
    uint8_t Y, Cb, Cr;
    unsigned int soft0, soft1;
    unsigned int t2, t3;
    op1 = (opacity > 255) ? 255 : opacity;
    op0 = 255 - op1;

    soft0 = 255 / distance;
    soft1 = 255 - soft0;

    t2 = threshold - distance;
    t3 = threshold2 + distance;

    /* first row */

    for (x = 0; x < width; x++) {
	a1 = yuv1[0][x];
	a2 = yuv2[0][x];
	if (a1 >= threshold && a1 <= threshold2) {
	    Y = (op0 * a1 + op1 * a2) >> 8;
	    Cb = (op0 * yuv1[1][x] + op1 * yuv2[1][x])  >> 8;
	    Cr = (op0 * yuv1[2][x] + op1 * yuv2[2][x])  >> 8;

	    yuv1[0][x] = Y;	// < 16 ? 16 : Y > 235 ? 235 : Y;
	    yuv1[1][x] = Cb;	// < 16 ? 16 : Cb > 240 ? 240 : Cb;
	    yuv1[2][x] = Cr;	// < 16 ? 16 : Cr > 240 ? 240 : Cr;
	}
    }

    for (y = width; y < len - width; y += width) {
	/* first pixel in column */
	a1 = yuv1[0][y];
	a2 = yuv2[0][y];
	if (a1 >= threshold && a1 <= threshold2) {
	    Y = (op0 * a1 + op1 * a2)  >> 8;
	    Cb = (op0 * yuv1[1][y] + op1 * yuv2[1][y]) >> 8;
	    Cr = (op0 * yuv1[2][y] + op1 * yuv2[2][y])  >> 8;
	}
	/* rest of pixels in column */
	/* rest of pixels in column */
	for (x = 1; x < width - 1; x++) {
	    a1 = yuv1[0][x + y];
	    a2 = yuv2[0][x + y];

	    if ((a1 >= t2 && a1 < threshold)
		|| (a1 > threshold2 && a1 <= t3)) {
		/* special case */
		yuv1[0][x + y] = pixel_Y_hi_;
		yuv1[1][x + y] = 128;
		yuv1[2][x + y] = 128;
	    } else {
		if (a1 >= threshold && a1 <= threshold2) {
		    Y = (op0 * a1 + op1 * a2) >> 8;
		    Cb = (op0 * yuv1[1][x + y] +
			  op1 * yuv2[1][x + y])  >> 8;
		    Cr = (op0 * yuv1[2][x + y] +
			  op1 * yuv2[2][x + y])  >> 8;

		    yuv1[0][x + y] = Y;		// < 16 ? 16 : Y > 235 ? 235 : Y;
		    yuv1[1][x + y] = Cb;	// < 16 ? 16 : Cb > 240 ? 240 : Cb;
		    yuv1[2][x + y] = Cr;	// < 16 ? 16 : Cr > 240 ? 240 : Cr;
		}
	    }
	}
    }
    /* last row */
    for (x = len - width; x < len; x++) {
	a1 = yuv1[0][x];
	a2 = yuv2[0][x];
	if (a1 >= threshold && a1 <= threshold2) {
	    Y = (op0 * a1 + op1 * a2)  >> 8;
	    Cb = (op0 * yuv1[1][x] + op1 * yuv2[1][x])  >> 8;
	    Cr = (op0 * yuv1[2][x] + op1 * yuv2[2][x])  >> 8;

	    yuv1[0][x] = Y;	// < 16 ? 16 : Y > 235 ? 235 : Y;
	    yuv1[1][x] = Cb;	// < 16 ? 16 : Cb > 240 ? 240 : Cb;
	    yuv1[2][x] = Cr;	// < 16 ? 16 : Cr > 240 ? 240 : Cr;
	}
    }
}



void lumakey_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int type, int threshold, int threshold2,
		   int feather, int d)
{

    switch (type) {
    case 0:
	/* normal overlay */
	lumakey_simple(frame->data, frame2->data, width, height, threshold, threshold2,
		       feather);
	break;
    case 1:
	/* threshold */
	lumakey_smooth_white(frame->data, frame2->data, width, height, threshold,
			     threshold2, feather, d);
	break;
    case 2:
	lumakey_smooth(frame->data, frame2->data, width, height, threshold, threshold2,
		       feather, d);
	break;
    }

}
void lumakey_free(){}
