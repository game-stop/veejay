/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "lumablend.h"

vj_effect *lumablend_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 4;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 0;	/* type */
	ve->limits[1][0] = 2;
	ve->limits[0][1] = 0;	/* threshold 1 */
	ve->limits[1][1] = 255;
	ve->limits[0][2] = 0;	/* threshold 2 */
	ve->limits[1][2] = 255;
	ve->limits[0][3] = 0;	/*opacity */
	ve->limits[1][3] = 255;
	ve->defaults[0] = 1;
	ve->defaults[1] = 0;
	ve->defaults[2] = 35;
	ve->defaults[3] = 150;
	ve->description = "Opacity by Threshold";
	ve->extra_frame = 1;
	ve->parallel = 1;
	ve->sub_format = 1;
	ve->parallel = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Threshold A", "Threshold B", "Opacity" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
		   "Inclusive", "Exclusive","Blurred" );	

	return ve;
}


static void opacity_by_threshold(VJFrame *frame, VJFrame *frame2,
								 int threshold, int threshold2, int opacity)
{
	const unsigned int width = frame->width;
	const int len = frame->len;
	uint8_t **yuv1 = frame->data;
	uint8_t **yuv2 = frame2->data;
	unsigned int x, y;
	uint8_t a1, a2;
	unsigned int op0, op1;
	op1 = (opacity > 255) ? 255 : opacity;
	op0 = 255 - op1;


	for (y = 0; y < len; y += width) {
		for (x = 0; x < width; x++) {
			a1 = yuv1[0][x + y];
			a2 = yuv2[0][x + y];
			if (a1 >= threshold && a1 <= threshold2) {
				yuv1[0][x + y] = (op0 * a1 + op1 * a2) >> 8;
				yuv1[1][x + y] = (op0 * yuv1[1][x + y] + op1 * yuv2[1][x + y]) >> 8;
				yuv1[2][x + y] = (op0 * yuv1[2][x + y] + op1 * yuv2[2][x + y]) >> 8;
			}
		}
	}

}

static void opacity_by_threshold_(VJFrame *frame, VJFrame *frame2,
								  int threshold, int threshold2, int opacity)
{
	const unsigned int width = frame->width;
	const int len = frame->len;
	uint8_t **yuv1 = frame->data;
	uint8_t **yuv2 = frame2->data;
	unsigned int x, y;
	uint8_t a1, a2;
	unsigned int op0, op1;
	op1 = (opacity > 255) ? 255 : opacity;
	op0 = 255 - op1;

	for (y = 0; y < len; y += width) {
		for (x = 0; x < width; x++) {
			a1 = yuv1[0][x + y];
			a2 = yuv2[0][x + y];
			if (a2 > threshold && a2 < threshold2) {
				yuv1[0][x + y] = (op0 * a1 + op1 * a2) >> 8;
				yuv1[1][x + y] = (op0 * yuv1[1][x + y] + op1 * yuv2[1][x + y]) >> 8;
				yuv1[2][x + y] = (op0 * yuv1[2][x + y] + op1 * yuv2[2][x + y]) >> 8;
			}
		}
	}
}

static void opacity_by_threshold_blur(VJFrame *frame, VJFrame *frame2,
									  int threshold, int threshold2, int opacity)
{
	const unsigned int width = frame->width;
	const int len = frame->len - width;
	uint8_t **yuv1 = frame->data;
	uint8_t **yuv2 = frame2->data;
	unsigned int x, y;
	uint8_t a1=threshold, a2=threshold2;
	unsigned int op0, op1;
	op1 = (opacity > 255) ? 255 : opacity;
	op0 = 255 - op1;
	for( y = 0; y < width; y+= width ) {
	  for( x = 0; x < width; x ++ ) {
		
			yuv1[0][x + y] = (op0 * a1 + op1 * a2) >> 8;
			yuv1[1][x + y] =
			(op0 * yuv1[1][x + y] + op1 * yuv2[1][x + y]) >> 8;
			yuv1[2][x + y] =
			(op0 * yuv1[2][x + y] + op1 * yuv2[2][x + y]) >> 8;
		}
	}
	for (y = width; y < len; y += width) {
			yuv1[0][y] = (op0 * a1 + op1 * a2) >> 8;
			yuv1[1][y] =
			(op0 * yuv1[1][y] + op1 * yuv2[1][y]) >> 8;
			yuv1[2][y] =
			(op0 * yuv1[2][y] + op1 * yuv2[2][y]) >> 8;

	for (x = 1; x < width-1; x++) {
		a1 = yuv1[0][x + y];
		a2 = yuv2[0][x + y];
		if (a1 >= threshold && a1 <= threshold2) {
			a1 = (yuv1[0][y - width + x - 1] +
			  yuv1[0][y - width + x + 1] +
			  yuv1[0][y - width + x] +
			  yuv1[0][y + x] +
			  yuv1[0][y + x - 1] +
			  yuv1[0][y + x + 1] +
			  yuv1[0][y + width + x] +
			  yuv1[0][y + width + x + 1] +
			  yuv1[0][y + width + x - 1]
			) / 9;

			a2 = (yuv2[0][y - width + x - 1] +
			  yuv2[0][y - width + x + 1] +
			  yuv2[0][y - width + x] +
			  yuv2[0][y + x] +
			  yuv2[0][y + x - 1] +
			  yuv2[0][y + x + 1] +
			  yuv2[0][y + width + x] +
			  yuv2[0][y + width + x + 1] +
			  yuv2[0][y + width + x - 1]
			) / 9;

			yuv1[0][x + y] = (op0 * a1 + op1 * a2) >> 8;
			yuv1[1][x + y] =
			(op0 * yuv1[1][x + y] + op1 * yuv2[1][x + y]) >> 8;
			yuv1[2][x + y] =
			(op0 * yuv1[2][x + y] + op1 * yuv2[2][x + y]) >> 8;
		}
	   }

	  yuv1[0][width + y] = (op0 * a1 + op1 * a2) >> 8;
	  yuv1[1][width + y] =
			(op0 * yuv1[1][width + y] + op1 * yuv2[1][width + y]) >> 8;
			yuv1[2][width + y] =
			(op0 * yuv1[2][width + y] + op1 * yuv2[2][width + y]) >> 8;
	
   }
	for( x = 0; x < width; x ++ ) {
		
		yuv1[0][x + y] = (op0 * a1 + op1 * a2) >> 8;
		yuv1[1][x + y] =
		(op0 * yuv1[1][x + y] + op1 * yuv2[1][x + y]) >> 8;
		yuv1[2][x + y] =
		(op0 * yuv1[2][x + y] + op1 * yuv2[2][x + y]) >> 8;
	}

}

void lumablend_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args ) {
    int type = args[0];
    int t1 = args[1];
    int t2 = args[2];
    int opacity = args[3];

	switch (type) {
		case 0:
			opacity_by_threshold(frame, frame2, t1, t2, opacity);
		break;
		case 1:
			opacity_by_threshold_(frame, frame2, t1, t2, opacity);
		break;
		case 2:
			opacity_by_threshold_blur(frame, frame2, t1, t2, opacity);
		break;
	}
}
