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

#include "smuck.h"
#include "common.h"
#include <stdlib.h>
#include <stdio.h>

int smuck_rand_val;

vj_effect *smuck_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 1;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 14;

    ve->description = "Smuck";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}



unsigned int smuck_fastrand()
{
    return (smuck_rand_val = smuck_rand_val * 1103516245 + 12345);
}

/* this effect comes from Effect TV as well; the code for this one is in Transform 
   different is the smuck table containing some values. 
*/
void smuck_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		 int height, int n)
{
    unsigned int yd, xd, x, y;
    const unsigned int smuck[15] =
	{ 30, 60, 58, 59, 57, 56, 55, 54, 53, 89, 90, 88, 87, 86, 85 };
    for (y = 0; y < height; y++) {
	for (x = 0; x < width; x++) {
	    yd = y + (smuck_fastrand() >> smuck[n]) - 2;
	    xd = x + (smuck_fastrand() >> smuck[n]) - 2;
	    if (xd > width)
		xd = width-1;
	    if (yd > height)
		yd = height;
	    yuv1[0][x + y * width] = yuv2[0][xd + yd * width];
	}
    }
}
void smuck_free(){}
