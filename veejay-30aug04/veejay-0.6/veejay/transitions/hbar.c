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

#include <stdlib.h>
#include "../vj-effect.h"

vj_effect *hbar_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4;
    ve->defaults[1] = 1;
    ve->defaults[2] = 3;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = height;

    ve->sub_format = 0;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = height;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = height;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = height;
    ve->limits[0][4] = 0;
    ve->limits[1][4] = height;

    ve->description = "Horizontal Sliding Bars";

    ve->extra_frame = 1;
    ve->has_internal_data = 0;
    return ve;
}

/* p0 351 , p1 92 : positioneert image 1 bovenaan
   p4 beweegt image offset verticaal (frame in frame)
   p5 beweegt image offset horizontaal (frame in frame) */
static int bar_top_auto = 0;
static int bar_bot_auto = 0;
static int bar_top_vert = 0;
static int bar_bot_vert = 0;
void hbar_apply(uint8_t *yuv1[3], uint8_t *yuv2[3], int width, int height,int divider, int top_y, int bot_y, int top_x, int bot_x  ) {

	const int top_width = width;		   /* frame in frame destination area */
	const int top_height = height/(divider);

	const int bottom_width = width;	  /* frame in frame destionation area */
	const int bottom_height = (height - top_height);
	
	int x,y,uv_width,uvy1,uvy2,uvx1,uvx2;
	int yy=0;
	int y2 = bar_top_auto + top_y;  /* destination */
	int x2 = bar_top_vert + top_x;

        int uv_y2 = y2 >> 1;
        int uv_x2 = x2 >> 1;
        const int uv_width = width / 2;
        const int uv_height = height / 2;
        const int uv_top_width = top_width / 2;
        const int uv_top_height = top_height / 2;
        const int uv_bot_width = bottom_width / 2;
        const int uv_bot_height = bottom_height / 2;

	if(y2 > width) { y2 = 0; bar_top_auto = 0; }
	if(x2 > (height-top_height)) {x2 = 0; bar_top_vert = 0; }

        if(uv_y2 > width) { uv_y2 = 0; }
        if(uv_x2 > (uv_height - uv_top_height)) { uv_x2 = 0; }

	/* start with top frame in a frame */
	for( y = 0; y < top_height; y++ ) {
		for ( x = 0; x < top_width; x++ ) {
			yuv1[0][ (y*width + x)] = yuv2[0][ ( (y+x2) *width + x +y2)];
//			yuv1[1][ (y*width + x)] = yuv2[1][ ( (y+x2) *width + x +y2)];
//			yuv1[2][ (y*width + x)] = yuv2[2][ ( (y+x2) *width + x +y2)];

		}
	}
	/* start with top frame in a frame */
	for( y = 0; y < uv_top_height; y++ ) {
		for ( x = 0; x < uv_top_width; x++ ) {
			yuv1[1][ (y*uv_width + x)] = yuv2[1][ ( (y+uv_x2) *uv_width + x + uv_y2)];
			yuv1[2][ (y*uv_width + x)] = yuv2[2][ ( (y+uv_x2) *uv_width + x + uv_y2)];

		}
	}




	/* do bottom part */
	y2 = bar_bot_auto + bot_y;
	x2 = bar_bot_vert + bot_x;
	if(y2 > width) { y2 = 0; bar_bot_auto = 0; }
	if(x2 > (height-top_height)) { x2 = 0; bar_bot_vert = 0; }
	/* start with bottom frame in a frame */
	for ( y = bottom_height; y < height; y++) {
		yy++;
		for(x=0; x < bottom_width; x++ ) {
			yuv1[0][ (y*width + x)] = yuv2[0][((yy+x2)*width+x+y2)];
			yuv1[1][ (y*width + x)] = yuv2[1][((yy+x2)*width+x+y2)];
			yuv1[2][ (y*width + x)] = yuv2[2][((yy+x2)*width+x+y2)];
		}
	}

	bar_top_auto += top_y;
	bar_bot_auto += bot_y;
	bar_top_vert += top_x;
	bar_bot_vert += bot_x;
}
void hbar_applyq(uint8_t *yuv1[3], uint8_t *yuv2[3], int width, int height,int divider, int top_y, int bot_y, int top_x, int bot_x  ) {

	int top_width = width;		   /* frame in frame destination area */
	int top_height = height/(divider);

	int bottom_width = width;	  /* frame in frame destionation area */
	int bottom_height = (height - top_height);
	
	int x,y,uv_width,uvy1,uvy2,uvx1,uvx2;
	int yy=0;
	int y2 = bar_top_auto + top_y;  /* destination */
	int x2 = bar_top_vert + top_x;
	if(y2 > width) { y2 = 0; bar_top_auto = 0; }
	if(x2 > (height-top_height)) {x2 = 0; bar_top_vert = 0; }
	/* start with top frame in a frame */
	for( y = 0; y < top_height; y++ ) {
		for ( x = 0; x < top_width; x++ ) {
			yuv1[0][ (y*width + x)] = yuv2[0][ ( (y+x2) *width + x +y2)];
			yuv1[1][ (y*width + x)] = yuv2[1][ ( (y+x2) *width + x +y2)];
			yuv1[2][ (y*width + x)] = yuv2[2][ ( (y+x2) *width + x +y2)];

		}
	}
	/* do bottom part */
	y2 = bar_bot_auto + bot_y;
	x2 = bar_bot_vert + bot_x;
	if(y2 > width) { y2 = 0; bar_bot_auto = 0; }
	if(x2 > (height-top_height)) { x2 = 0; bar_bot_vert = 0; }
	/* start with bottom frame in a frame */
	for ( y = bottom_height; y < height; y++) {
		yy++;
		for(x=0; x < bottom_width; x++ ) {
			yuv1[0][ (y*width + x)] = yuv2[0][((yy+x2)*width+x+y2)];
			yuv1[1][ (y*width + x)] = yuv2[1][((yy+x2)*width+x+y2)];
			yuv1[2][ (y*width + x)] = yuv2[2][((yy+x2)*width+x+y2)];
		}
	}

	bar_top_auto += top_y;
	bar_bot_auto += bot_y;
	bar_top_vert += top_x;
	bar_bot_vert += bot_x;
}

void hbar_free(){}
