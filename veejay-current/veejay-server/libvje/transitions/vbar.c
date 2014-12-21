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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "vbar.h"     

vj_effect *vbar_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4;
    ve->defaults[1] = 1;
    ve->defaults[2] = 3;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = height;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = height;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = height;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = height;
    ve->limits[0][4] = 0;
    ve->limits[1][4] = height;

    ve->description = "Vertical Sliding Bars";
    ve->sub_format = 1;

    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Divider", "Top Y", "Bot Y", "Top X", "Bot X" ); 
   return ve;
}

/* p0 351 , p1 92 : positioneert image 1 bovenaan
   p4 beweegt image offset verticaal (frame in frame)
   p5 beweegt image offset horizontaal (frame in frame) */
static int bar_top_auto = 0;
static int bar_bot_auto = 0;
static int bar_top_vert = 0;
static int bar_bot_vert = 0;
void vbar_apply(VJFrame *frame, VJFrame *frame2, int width, int height,int divider, int top_y, int bot_y, int top_x, int bot_x  ) {

	//int top_width = width;		   /* frame in frame destination area */

	int top_width = width/divider; 
        int bottom_width = width - top_width;

	//int bottom_width = width;	  /* frame in frame destionation area */
	
	int x,y;
	int yy=0;

	int y2 = bar_top_auto + top_y;  /* destination */
	int x2 = bar_top_vert + top_x;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	int	len = frame->len;        



	if(y2 > height) { y2 = 0; bar_top_auto = 0; }
	if(x2 > width)  { x2 = 0; bar_top_vert = 0; }

	/* start with top frame in a frame */
	for( y = 0; y < height-y2; y++ ) {
		for ( x = 0; x < top_width; x++ ) {
			Y[ (y*width + x)] = Y2[ ( (y+y2) *width + x +x2)];
			Cb[ (y*width + x)] = Cb2[ ( (y+y2) *width + x +x2)];
			Cr[ (y*width + x)] = Cr2[ ( (y+y2) *width + x +x2)];
		}
	}

	/* do bottom part */
	y2 = bar_bot_auto + bot_y;
	x2 = bar_bot_vert + bot_x;

	if(y2 > height) { y2 = 0; bar_bot_auto = 0; }
	if(x2 > width)  { x2 = 0; bar_bot_vert = 0; }

	/* start with bottom frame in a frame */
	for ( y = 0; (yy+y2) < height; y++) {
		yy++;
		for(x=bottom_width; (x+x2) < width; x++ ) {
			int pos = (yy+y2) * width +x + x2 ;
			if(pos<len)
			{
			Y[ (y*width + x)] = Y2[((yy+y2)*width+x+x2)];
			Cb[ (y*width + x)] = Cb2[((yy+y2)*width+x+x2)];
			Cr[ (y*width + x)] = Cr2[((yy+y2)*width+x+x2)];
			}
		}
	}

	bar_top_auto += top_y;
	bar_bot_auto += bot_y;
	bar_top_vert += top_x;
	bar_bot_vert += bot_x;
}

void vbar_free(){}
