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

#include "opacity.h"
#include "../../config.h"
#include <stdlib.h>
#include "../subsample.h"

vj_effect *opacity_init(int w, int h)
{
    int width = 720;
    int height = 576;
	int len = width * height;
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150;
    ve->description = "Normal Overlay";
    ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->has_internal_data = 0;
 
    return ve;
}



void opacity_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		   int height, int opacity)
{
    unsigned int i, op0, op1;
    unsigned int len = width * height;

    op1 = (opacity > 255) ? 255 : opacity;
    op0 = 255 - op1;

    for (i = 0; i < len; i++) {
	yuv1[0][i] = (op0 * yuv1[0][i] + op1 * yuv2[0][i]) >> 8;
	}
    len >>= 2;		

    for (i = 0; i < len; i++) {
	yuv1[1][i] = (op0 * yuv1[1][i] + op1 * yuv2[1][i]) >> 8;
	yuv1[2][i] = (op0 * yuv1[2][i] + op1 * yuv2[2][i]) >> 8;
    }
/*

	unsigned int scale_x,nx,ny,x,i,y,len=width*height;
	double c1 = opacity;
	const double w_ratio = width / 255.0; 
	const double h_ratio = height / 255.0;
	int diff;

	memcpy(buf[0], yuv1[0],len);  
	memcpy(buf[1], yuv1[1],len);
	memcpy(buf[2], yuv1[2],len);

	for(y=0; y < height ; y++)
	{
		for(x=0; x < width; x++)
		{
			i = y * width + x;
			diff = abs( yuv1[0][i] - yuv2[0][i] );
			if(diff > opacity)	
			{
			diff = diff - 128;
			nx = diff * w_ratio + x;
			ny = diff * h_ratio + y;
			if(nx < 0) nx += width;
			if(nx < 0) nx = 0; else if (nx >= width) nx = width-1;
			if(ny < 0) ny = 0; else if (ny >= height) ny = height-1;
			yuv1[0][i] = buf[0][ny * width + nx];
			yuv1[1][i] = buf[1][ny * width + nx];
			yuv1[2][i] = buf[2][ny * width + nx];
			}
		}
	}	
*/	
 
}

void opacity_free(){}
