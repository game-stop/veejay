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
#include "mirrors.h"
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <string.h>

vj_effect *mirrors_init(int width,int height)
{

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 1;
    ve->defaults[1] = 1;
    ve->limits[0][0] = 0;	/* horizontal or vertical mirror */
    ve->limits[1][0] = 3;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = (int)((float)(width * 0.33));
    ve->sub_format = 1;
    ve->description = "Multi Mirrors";
    ve->extra_frame = 0;
	ve->has_user = 0;
    return ve;
}

void _mirrors_v( uint8_t *yuv[3], int width, int height, int factor, int swap)
{
	const unsigned int len = width * height;
	unsigned int r,c;
	const unsigned int line_width = width / ( factor + 1);
	unsigned int i=0;

	if(swap)
	{
		for(r = 0; r < len; r += width )
		{
			for( c = 0 ; c < width; c+= line_width)
			{
				for(i = 0; i < line_width; i++)
				{
					yuv[0][r + c + (line_width-i)] = yuv[0][r + c + i];
					yuv[1][r + c + (line_width-i)] = yuv[1][r + c + i];
					yuv[2][r + c + (line_width-i)] = yuv[2][r + c + i];
				}
			}
	
		}
	}
	else
	{
		for(r = 0; r < len; r += width )
		{
			for( c = 0 ; c < width; c+= line_width)
			{
				for(i = 0; i < line_width; i++)
				{
					yuv[0][r + c + i] = yuv[0][r + c + (line_width-i)];
					yuv[1][r + c + i] = yuv[1][r + c + (line_width-i)];
					yuv[2][r + c + i] = yuv[2][r + c + (line_width-i)];
				}
			}
	
		}
	}
}
void _mirrors_h( uint8_t *yuv[3], int width, int height, int factor, int swap)
{
	unsigned int len = width * height;
	unsigned int line_height = height / ( factor + 1);

	unsigned int nr = height / line_height;
	unsigned int x,y,i;
	unsigned int slice = 0;
	unsigned int slice_end = 0;
	if(swap)
	{
		for(i=0; i < nr; i++)
		{
			slice = i * line_height;
			slice_end = slice + line_height;
			for(y=slice; y < slice_end; y++)
			{
				for(x=0; x < width; x++)
				{
					yuv[0][(y*width)+x] = yuv[0][(slice_end-y)*width+x];
					yuv[1][y*width+x] = yuv[1][(slice_end-y)*width+x];
					yuv[2][y*width+x] = yuv[2][(slice_end-y)*width+x];
				} 
			}
		}
	}
	else
	{
		for(i=0; i < nr; i++)
		{
			slice = i * line_height;
			slice_end = slice + line_height;
			for(y=slice_end; y > 0; y--)
			{
				for(x=0; x < width; x++)
				{
					yuv[0][y*width+x] = yuv[0][(slice_end-y)*width+x];
					yuv[1][y*width+x] = yuv[1][(slice_end-y)*width+x];
					yuv[2][y*width+x] = yuv[2][(slice_end-y)*width+x];
				} 
			}
		}
	}
}


void mirrors_apply(VJFrame *frame, int width, int height, int type,
		   int factor )
{
    switch (type) {
    case 0:
	_mirrors_v(frame->data, width, height, factor, 0);
	break;
    case 1:
	_mirrors_v(frame->data,width,height,factor,1);
	break;
    case 2:
	_mirrors_h(frame->data,width,height,factor,0);
	break;
    case 3:
	_mirrors_h(frame->data,width,height,factor,1);
	break;
	}
}
void mirrors_free(){}
