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

#include "colorshift.h"
#include <stdlib.h>


vj_effect *colorshift_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->defaults[1] = 235;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 9;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->description = "Shift pixel values YCbCr";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}


/* bitwise and test */

void softmask2_apply(uint8_t * yuv1[3], int width, int height, int paramt)
{
    const unsigned int len = (width * height);
    unsigned int x;
    for (x = 0; x < len; x++)
	yuv1[0][x] &= paramt;
}

void softmask2_applycb(uint8_t * yuv1[3], int width, int height,
		       int paramt)
{
    const unsigned int len = (width * height) / 4;
    unsigned int x;
    for (x = 0; x < len; x++)
	yuv1[1][x] &= paramt;
}

void softmask2_applycr(uint8_t * yuv1[3], int width, int height,
		       int paramt)
{
    const unsigned int len = (width * height) / 4;
    unsigned int x;
    for (x = 0; x < len; x++)
	yuv1[2][x] &= paramt;
}

void softmask2_applycbcr(uint8_t * yuv1[3], int width, int height,
			 int paramt)
{
    const unsigned int len = (width * height) / 4;
    unsigned int x;
    for (x = 0; x < len; x++) {
	yuv1[1][x] &= paramt;
	yuv1[2][x] &= paramt;
    }
}

void softmask2_applyycbcr(uint8_t * yuv1[3], int width, int height,
			  int paramt)
{
    const unsigned int len = (width * height);
    unsigned int x;
    for (x = 0; x < len; x++)
	yuv1[0][x] &= paramt;
    for (x = 0; x < (len / 4); x++) {
	yuv1[1][x] &= paramt;
	yuv1[2][x] &= paramt;
    }
}

void softmask_apply(uint8_t * yuv1[3], int width, int height, int paramt)
{
    const unsigned int len = (width * height);
    unsigned int x;
    for (x = 0; x < len; x++)
	yuv1[0][x] |= paramt;
}


void softmask_applycb(uint8_t * yuv1[3], int width, int height, int paramt)
{
    const unsigned int len = (width * height) / 4;
    unsigned int x;
    for (x = 0; x < len; x++)
	yuv1[1][x] |= paramt;
}


void softmask_applycr(uint8_t * yuv1[3], int width, int height, int paramt)
{
    const unsigned int len = (width * height) / 4;
    unsigned int x;
    for (x = 0; x < len; x++)
	yuv1[2][x] |= paramt;
}


void softmask_applycbcr(uint8_t * yuv1[3], int width, int height,
			int paramt)
{
    const unsigned int len = (width * height) / 4;
    unsigned int x;
    for (x = 0; x < len; x++) {
	yuv1[1][x] |= paramt;
	yuv1[2][x] |= paramt;
    }
}

void softmask_applyycbcr(uint8_t * yuv1[3], int width, int height,
			 int paramt)
{
    const unsigned int len = (width * height);
    unsigned int x;
    for (x = 0; x < len; x++)
	yuv1[0][x] |= paramt;
    for (x = 0; x < (len / 4); x++) {
	yuv1[1][x] |= paramt;
	yuv1[2][x] |= paramt;
    }
}


void colorshift_apply(uint8_t * yuv1[3], int width, int height, int type,
		      int n)
{
    switch (type) {
    case 0:
	softmask_apply(yuv1, width, height, n);
	break;
    case 1:
	softmask_applycb(yuv1, width, height, n);
	break;
    case 2:
	softmask_applycr(yuv1, width, height, n);
	break;
    case 3:
	softmask_applycbcr(yuv1, width, height, n);
	break;
    case 4:
	softmask_applyycbcr(yuv1, width, height, n);
	break;
    case 5:
	softmask2_apply(yuv1, width, height, n);
	break;
    case 6:
	softmask2_applycb(yuv1, width, height, n);
	break;
    case 7:
	softmask2_applycr(yuv1, width, height, n);
	break;
    case 8:
	softmask2_applycbcr(yuv1, width, height, n);
	break;
    case 9:
	softmask2_applyycbcr(yuv1, width, height, n);
	break;
    }
}
void colorshift_free(){}
