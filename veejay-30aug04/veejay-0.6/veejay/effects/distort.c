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

/* distortion effects */

#include "distort.h"
#include <stdlib.h>
#include <math.h>
#include "widthmirror.h"

static int *plasma_table;
static int plasma_pos1 = 0;
static int plasma_pos2 = 0;

vj_effect *distortion_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    int i;
    float rad;
    plasma_table = (int *) malloc(sizeof(int) * 512);
    for (i = 0; i < 512; i++) {
	rad = ((float) i * 0.703125) * 0.0174532;
	plasma_table[i] = sin(rad) * (1024);
    }

    ve->num_params = 2;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->defaults[1] = 3;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 8;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 8;
    ve->description = "Distortion";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}

void distortion_free() {
   if(plasma_table) free(plasma_table);
}

/* the distortion effect comes originally from the demo effects collection,
   it is the plasma effect */

void distortion_apply(uint8_t * yuv1[3], int width, int height,
		      int inc_val1, int inc_val2)
{

    unsigned int x, y, i, j;
    uint8_t index;
    int tpos1 = 0, tpos2 = 0, tpos3 = 0, tpos4 = 0;
    int z = 511;

    unsigned int yi;

    uint8_t p, cb, cr;

    for (i = 0; i < height; ++i) {
	tpos1 = plasma_pos1 + inc_val1;
	tpos2 = plasma_pos2 + inc_val2;

	tpos3 &= z;
	tpos4 &= z;

	for (j = 0; j < width; ++j) {
	    tpos1 &= z;
	    tpos2 &= z;
	    x = plasma_table[tpos1] + plasma_table[tpos2] +
		plasma_table[tpos3] + plasma_table[tpos4];

	    index = 128 + (x >> 4);

	    /* image = index */
	    yuv1[0][(i * width) + j] = yuv1[0][index];

	    tpos1 += inc_val1;
	    tpos2 += inc_val2;
	}
	tpos4 += 3;
	tpos3 += 1;
    }
    for (i = 0; i < (height / 2); ++i) {
	tpos1 = plasma_pos1 + inc_val1;
	tpos2 = plasma_pos2 + inc_val2;

	tpos3 &= z;
	tpos4 &= z;

	for (j = 0; j < (width / 2); ++j) {
	    tpos1 &= z;
	    tpos2 &= z;
	    x = plasma_table[tpos1] + plasma_table[tpos2] +
		plasma_table[tpos3] + plasma_table[tpos4];

	    index = 128 + (x >> 4);

	    /* image = index */
	    yuv1[1][(j * (width / 2)) + i] = yuv1[1][index];
	    yuv1[2][(j * (width / 2)) + i] = yuv1[2][index];
	    tpos1 += inc_val1;
	    tpos2 += inc_val2;
	}
	tpos4 += 3;
	tpos3 += 1;
    }

    plasma_pos1 += 9;
    plasma_pos2 += 8;

    for (y = 0; y < height; y++) {
	yi = y * width;
	for (x = 0; x < width; x++) {
	    p = yuv1[0][yi + x];
	    yuv1[0][yi + (width - x - 1)] = p;
	}
    }
    for (y = 0; y < (height / 2); y++) {
	yi = y * (width / 2);
	for (x = 0; x < (width / 2); x++) {
	    cb = yuv1[1][yi + x];
	    cr = yuv1[2][yi + x];
	    yuv1[1][yi + ((width / 2) - x - 1)] = cb;
	    yuv1[2][yi + ((width / 2) - x - 1)] = cr;
	}
    }

}
void distort_free(){}
