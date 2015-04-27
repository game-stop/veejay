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
#include "distort.h"
#include "widthmirror.h"

static int *plasma_table = NULL;
static int plasma_pos1 = 0;
static int plasma_pos2 = 0;

vj_effect *distortion_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    int i;
    float rad;
    plasma_table = (int *) vj_calloc(sizeof(int) * 512);
    for (i = 0; i < 512; i++) {
	rad = ((float) i * 0.703125) * 0.0174532;
	plasma_table[i] = sin(rad) * (1024);
    }

    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->defaults[1] = 3;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 8;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 8;
    ve->description = "Distortion";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Inc 1" , "Inc2" );
    return ve;
}

void distortion_free() {
   if(plasma_table) free(plasma_table);
}

void	distortion_destroy()
{
	if(plasma_table)
		free( plasma_table );
}

/* the distortion effect comes originally from the demo effects collection,
   it is the plasma effect */

void distortion_apply(VJFrame *frame, int width, int height,
		      int inc_val1, int inc_val2)
{

    unsigned int x, y, i, j;
    uint8_t index;
    int tpos1 = 0, tpos2 = 0, tpos3 = 0, tpos4 = 0;
    int z = 511;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	const int uv_width = frame->uv_width;
	const int uv_height = frame->uv_height;	
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
	    Y[(i * width) + j] = Y[index];

	    tpos1 += inc_val1;
	    tpos2 += inc_val2;
	}
	tpos4 += 3;
	tpos3 += 1;
    }



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

	    index = 128 + (x >> 4);

	    /* image = index */
	    Cb[(j * uv_width) + i] = Cb[index];
	    Cr[(j * uv_width) + i] = Cr[index];
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
	    p =Y[yi + x];
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
void distort_free(){}
