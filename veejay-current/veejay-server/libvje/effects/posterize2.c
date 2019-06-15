/* 
 * Linux VeeJay
 *
 * Copyright(C)2018 Niels Elburg <nwelburg@gmail.com>
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
#include "posterize2.h"

vj_effect *posterize2_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4;
    ve->defaults[1] = 16;
    ve->defaults[2] = 235;
	ve->defaults[3] = 0;

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 256;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 256;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 256;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 5;

	ve->parallel = 1;
    ve->description = "Posterize II (Threshold Range)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Factor", "Min Threshold", "Max Threshold", "Mode");
	
	return ve;	
}

void posterize2_apply(VJFrame *frame, int vfactor, int t1,int t2, int mode)
{
	const int len = frame->len;
	const unsigned int factor = (256 / vfactor);
	int i;
	uint8_t v;

	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *A = frame->data[3];

	switch(mode) {

		case 0:
			for( i = 0; i < len; i ++ ) {
				v = Y[i];
				v = v - ( v % factor );
	
				if( v >= t1 && v <= t2 ) {
					Y[i] = v;
				}
				else {
					Y[i] = pixel_Y_lo_;
					Cb[i] = 128;
					Cr[i] = 128;
				}
			}
			break;
		case 1:
			for( i = 0; i < len; i ++ ) {
				v = Y[i];
				v = v - (v % factor);

				if( v >= t1 && v <= t2 ) {
					Y[i] = v;
					Cb[i] = 128;
					Cr[i] = 128;
				}
			}
			break;
		case 2:
			for( i = 0; i < len; i ++) {
				v = Y[i];
				v = v - (v % factor);

				if( v >= t1 && v <= t2 ) {
					Y[i] = v;
				}
				else if( v < t1) {
					Y[i] = pixel_Y_lo_;
					Cb[i] = 128;
					Cr[i] = 128;
				}
				else if( v > t2) {
					Y[i] = pixel_Y_hi_;
					Cb[i] = 128;
					Cr[i] = 128;
				}
			}
			break;
		case 3:
			for( i = 0; i < len; i ++ ) {
				v = Y[i];
				v = v - ( v % factor );
	
				if( v >= t1 && v <= t2 ) {
					A[i] = v;
				}
				else {
					A[i] = pixel_Y_lo_;
				}
			}
			break;
		case 4:
			for( i = 0; i < len; i ++ ) {
				v = Y[i];
				v = v - (v % factor);

				if( v >= t1 && v <= t2 ) {
					A[i] = v;
				}
			}
			break;
		case 5:
			for( i = 0; i < len; i ++) {
				v = Y[i];
				v = v - (v % factor);

				if( v >= t1 && v <= t2 ) {
					A[i] = v;
				}
				else if( v < t1) {
					A[i] = pixel_Y_lo_;
				}
				else if( v > t2) {
					A[i] = pixel_Y_hi_;
				}
			}

		default:
			break;
	}
}
