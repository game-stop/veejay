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
#include <stdio.h>
#include <stdint.h>
#include "rotozoom.h"
#include <stdlib.h>
#include <math.h>


static  int *test_roto[9];
static int *test_roto2[9];
static int new_zpath = 0;
static int new_path = 0;
static int roto_old_p = 0;
static int roto_old_z = 0;
static uint8_t *rotobuffer[4] = { NULL,NULL,NULL,NULL };

vj_effect *rotozoom_init(int width, int height)
{
    int i, j;

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->defaults[2] = 1;
    ve->defaults[3] = 1;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 8;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 3;
    ve->description = "Rotozoom";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Rotate", "Zoom" , "Automatic");
	ve->has_user = 0;
    for (j = 0; j < 9; j++) {
	 test_roto[j] = (int *) vj_malloc(sizeof(int) * 256);
	 test_roto2[j] = (int *) vj_malloc(sizeof(int) * 256);
    }

    for (i = 0; i < 256; i++) {
	float rad = (float) i * 1.41176 * 0.0174532;
	float c = sin(rad);
	test_roto[0][i] = (c + 0.8) * 4096.0;
	test_roto2[0][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
	float rad = (float) i * 2.41176 * 0.0174532;
	float c = sin(rad);
	test_roto[1][i] = (c + 0.8) * 4096.0;
	test_roto2[1][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
	float rad = (float) i * 3.41576 * 0.0174532;
	float c = sin(rad);
	test_roto[2][i] = (c + 0.8) * 4096.0;
	test_roto2[2][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
	float rad = (float) i * 4.74176 * 0.0174532;
	float c = sin(rad);
	test_roto[3][i] = (c + 0.8) * 4096.0;
	test_roto2[3][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
	float rad = (float) i * 5.91176 * 0.0174532;
	float c = sin(rad);
	test_roto[4][i] = (c + 0.8) * 4096.0;
	test_roto2[4][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
	float rad = (float) i * 9.12345 * 0.0174532;
	float c = sin(rad);
	test_roto[5][i] = (c + 0.8) * 4096.0;
	test_roto2[5][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
	float rad = (float) i * 9.12345 * 0.0174532;
	float c = sin(rad);
	test_roto[6][i] = (c + 0.8) * 8096.0;
	test_roto2[6][i] = (2.0 * c) * 8096.0;
    }
    for (i = 0; i < 256; i++) {
	float rad = (float) i * 1.41176 * 0.0174532;
	float c = sin(rad);
	test_roto[7][i] = c * 4096.0;
	test_roto2[7][i] = c * 4096.0;
    }
    for (i = 0; i < 256; i++) {
	float rad = (float) i * 1.0 * 0.0174532;
	float c = sin(rad);
	test_roto[8][i] = c * 4096.0;
	test_roto2[8][i] = c * 4096.0;
    }

    return ve;
}

void	rotozoom_destroy()
{
    int j;
    for (j = 0; j < 9; j++) {
	 if( test_roto[j] )
		free(test_roto[j]);
	 if( test_roto2[j]);
		free(test_roto2[j]);
    }
}

int rotozoom_malloc(int width, int height)
{


   rotobuffer[0] = (uint8_t *) vj_calloc(sizeof(uint8_t) * width * height * 3);
	if(!rotobuffer[0])
		return 0;
	rotobuffer[1] = rotobuffer[0] + (width * height);
	rotobuffer[2] = rotobuffer[1] + (width * height);
   return 1;

}

void rotozoom_free() {
	if(rotobuffer[0])
		free(rotobuffer[0]);
	rotobuffer[0] = NULL;
	rotobuffer[1] = NULL;
	rotobuffer[2] = NULL;
}

/* rotozoomer, from the demo effects collection, works in supersampled YCbCr space.
   printf("Retro Rotozoom Effect - B. Ellacott, W.P. van Paassen - 2002\n");
 */
void draw_tile(int stepx, int stepy, int zoom, int w, int h,
	       uint8_t * src1[3], uint8_t * src2[3])
{

    int x, y, i, j, xd, yd, a, b, sx, sy;

    sx = sy = 0;
    xd = (stepx * zoom) >> 12;
    yd = (stepy * zoom) >> 12;

    for (j = 0; j < h; j++) {
	x = sx;
	y = sy;
	for (i = 0; i < w; i++) {
	    a = (x >> 12) & 255;
	    b = (y >> 12) & 255;
	    src1[0][(j * w) + i] = src2[0][b * w + a];
	    src1[1][(j * w) + i] = src2[1][b * w + a];
	    src1[2][(j * w) + i] = src2[2][b * w + a];
	    x += xd;
	    y += yd;
	}
	sx -= yd;
	sy += xd;
    }
}

void rotozoom2_apply(VJFrame *frame, uint8_t *data[3], int width,
		     int height, int n, int p, int z)
{


    draw_tile(test_roto[n][p],
	      test_roto[n][(p + 128) & 0xFF],
	      test_roto2[n][z], width, height, frame->data, data);
}

void rotozoom1_apply(VJFrame *frame, uint8_t *data[3], int w, int h,
		     int n, int p, int z)
{

    if (roto_old_p != p) {
	roto_old_p = p;
	new_path = p & 255;
    }
    if (roto_old_z != z) {
	roto_old_z = z;
	new_zpath = z & 255;
    }

    draw_tile(test_roto[n][new_path],
	      test_roto[n][(new_path + 128) & 0xff],
	      test_roto2[n][new_zpath], w, h, frame->data, data);

    new_path = (new_path - 1) & 255;
    new_zpath = (new_zpath + 1) & 255;

}


void rotozoom_apply( VJFrame *frame, int width, int height, int mode,
		    int rotate, int zoom, int autom)
{
	int strides[4] = { width*height,width*height,width*height,0};
    switch (autom) {		/* alas must do memcpy */
    case 0:
	vj_frame_copy( frame->data, rotobuffer, strides );
	rotozoom2_apply(frame, rotobuffer, width, height, mode, rotate,
			zoom);
	break;
    case 1:
	vj_frame_copy( frame->data,rotobuffer, strides );
	rotozoom1_apply(frame, rotobuffer, width, height, mode, rotate,
			zoom);
	break;
    }


}
