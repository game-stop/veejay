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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include <math.h>
#include "common.h"
#include "distort.h"
#include "widthmirror.h"

static int plasma_table[512];
static int plasma_pos1 = 0;
static int plasma_pos2 = 0;

vj_effect *distortion_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    int i;
    float rad;
    for (i = 0; i < 512; i++) {
		rad = ((float) i * 0.703125f) * 0.0174532f;
		plasma_table[i] = myround( sinf(rad) * 1024.0f );
    }

    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->defaults[1] = 3;
	ve->defaults[2] = 3;
	ve->defaults[3] = 1;
	ve->defaults[4] = 9;
	ve->defaults[5] = 8;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 0xff;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 0xff;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 0xff;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 0xff;
	ve->limits[0][4] = 0;
	ve->limits[1][4] = 0xff;
	ve->limits[0][5] = 0;
	ve->limits[1][5] = 0xff;

    ve->description = "Distortion (Plasma)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Increment 1" , "Increment 2",
			"Increment 3", "Increment 4", "Increment 5", "Increment 6"	);

    return ve;
}

/* the distortion effect comes originally from the demo effects collection,
   it is the plasma effect */

void distortion_apply(VJFrame *frame, int inc_val1, int inc_val2, int inc_val3, int inc_val4, int inc_val5, int inc_val6 )
{

    unsigned int x, y, i, j,yi;
    uint8_t index;
    int tpos1 = 0, tpos2 = 0, tpos3 = 0, tpos4 = 0;
    int z = 511;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
    uint8_t p, cb, cr;

	int uv_width = frame->uv_width;
	int uv_height = frame->uv_height;	
	
	const int height = (const int) frame->height;
	const int width = (const int) frame->width;

	if( frame->ssm ) {
		uv_width = width;
		uv_height = height;
	}

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
	
		    index = (x >> 4);
	
		    Y[(i * width) + j] = Y[index];
	
		    tpos1 += inc_val1;
		    tpos2 += inc_val2;
		}
		tpos4 += inc_val4;
		tpos3 += inc_val3;
    }

	tpos3 = 0;
	tpos4 = 0;
	
    for (i = 0; i < uv_height; ++i) {
		tpos1 = plasma_pos1 + inc_val1;
		tpos2 = plasma_pos2 + inc_val2;

		tpos3 &= z;
		tpos4 &= z;

		for (j = 0; j < uv_width; ++j) {
		    tpos1 &= z;
		    tpos2 &= z;
		    x = plasma_table[tpos1] + plasma_table[tpos2] +
				plasma_table[tpos3] + plasma_table[tpos4];

		    index = (x >> 4);
	
		    Cb[(j * uv_width) + i] = Cb[index];
		    Cr[(j * uv_width) + i] = Cr[index];
		    tpos1 += inc_val1;
		    tpos2 += inc_val2;
		}
		tpos4 += inc_val4;
		tpos3 += inc_val3;
    }

    plasma_pos1 += inc_val5;
    plasma_pos2 += inc_val6;

    for (y = 0; y < height; y++) {
		yi = y * width;
		for (x = 0; x < width; x++) {
		    p = Y[yi + x];
		    Y[yi + (width - x - 1)] = p;
		}
	 }
	 for (y = 0; y < uv_height; y++) {
		yi = y * uv_width;
		for (x = 0; x < uv_width; x++) {
			cb = Cb[yi + x];
			cr = Cr[yi + x];
		    Cb[yi + uv_width - x - 1] = cb;
		    Cr[yi + uv_width - x - 1] = cr;
		}
    }
}
