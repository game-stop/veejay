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

#include "posterize.h"
#include <stdlib.h>
vj_effect *posterize_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4;
    ve->defaults[1] = 16;
    ve->defaults[2] = 235;

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 256;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 256;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 256;

    ve->description = "Posterize (Threshold Range)";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;	
}



static void _posterize_y_simple(uint8_t *src[3], int len, int value, int threshold_min,int threshold_max)
{
	unsigned int i;
	uint8_t Y;
	uint8_t *y = src[0];
	const unsigned int factor = (256 / value);
	for( i = 0; i < len ; i++ ) 
	{
		Y = y[i];
		Y = Y - ( Y % factor );

		if( Y >= threshold_min && Y <= threshold_max)
		{
			y[i] = Y;

		}
		else
		{
			if( Y < threshold_min) Y = 16; else Y = 235;
		}
	}
}

void posterize_apply(uint8_t * yuv[3], int width, int height, int factor, int t1,int t2)
{
   _posterize_y_simple( yuv, (width*height), factor, t1,t2);
}
void posterize_free(){}
