/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "transop.h"

vj_effect *transop_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 150;	/* opacity */
    ve->defaults[1] = 265;	/* width of view port */
    ve->defaults[2] = 194;	/* height of viewport */
    ve->defaults[3] = 59;	/* y1 */
    ve->defaults[4] = 58;	/* x1 */
    ve->defaults[5] = 45;	/* y2 */
    ve->defaults[6] = 58;	/* x2 */

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = width;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = height;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = height;
    ve->limits[0][4] = 0;
    ve->limits[1][4] = width;
    ve->limits[0][5] = 0;
    ve->limits[1][5] = height;
    ve->limits[0][6] = 0;
    ve->limits[1][6] = width;
    ve->description = "Transition Translate Opacity";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Width", "Height", "Ay", "Ax", "By", "Bx");
    return ve;
}

/* translate, twidth,theight: size of block to transform */
/* moves block(x2,y2) to (x1,y1), size of block to move is twidth * theight  */
void transop_apply( VJFrame *frame, VJFrame *frame2,
		   int twidth, int theight, int x1, int y1, int x2, int y2,
		   int width, int height, int opacity)
{
	int x, y;
	unsigned int op0, op1;
  
	uint8_t *dY = frame->data[0];
	uint8_t *dCb = frame->data[1];
	uint8_t *dCr = frame->data[2];
	uint8_t *sY = frame2->data[0];
	uint8_t *sCb = frame2->data[1];
	uint8_t *sCr = frame2->data[2];

	op1 = (opacity > 255) ? 255 : opacity;
	op0 = 255 - op1;

	int view_width = twidth;
	int view_height = theight;
	int sy = y1;
	int sx = x1;

	int dy = y2;
	int dx = x2;

	if ( (dx + view_width ) > width )
		view_width = width - dx;
	if ( (dy + view_height ) > height )
		view_height = height - dy;


	if ( (sy + view_height) > height )
		view_height = height - sy;
	if ( (sx + view_width ) > width )
		view_width = width - sx;
	

	for( y = 0 ; y < view_height; y ++ )
	{
		for( x = 0 ; x < view_width; x ++ )
		{
			dY[ (dy + y ) * width + dx + x ] = 
				sY[ (sy + y) * width + sx + x ];

			dCb[ (dy + y ) * width + dx + x ] = 
				sCb[ (sy + y) * width + sx + x ];
					
			dCr[ (dy + y ) * width + dx + x ] = 
				sCr[ (sy + y) * width + sx + x ];

		}
	}
	

}

void transop_free(){}
