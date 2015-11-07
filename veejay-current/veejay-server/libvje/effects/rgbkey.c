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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "rgbkey.h"
#include <stdlib.h>
#include <math.h>
#include "common.h"

/*
 * originally from http://gc-films.com/chromakey.html
 */

vj_effect *rgbkey_init(int w,int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 15;	/* tolerance near */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 255;	/* g */
    ve->defaults[3] = 0;	/* b */
    ve->defaults[4] = 1;	/* tolerance far */
    ve->defaults[5] = 1;	/* show selection */
	ve->defaults[6] = 0;    /* use alpha */

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 255;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 1;

    ve->limits[0][6] = 0;
    ve->limits[1][6] = 4; /* logical alpha operator */

	ve->param_description = vje_build_param_list(ve->num_params, "Tolerance Near", "Red", "Green", "Blue", "Tolerance Far", "Show FG", "Alpa operator");
	ve->has_user = 0;
    ve->description = "Chroma Key (RGB)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->rgb_conv = 1;
    ve->parallel = 1;
	return ve;
}

static inline double color_distance( uint8_t Cb, uint8_t Cr, int Cbk, int Crk, int dA, int dB )
{
		double tmp = 0.0; 
		fast_sqrt( tmp, (Cbk - Cb) * (Cbk-Cb) + (Crk - Cr) * (Crk - Cr) );
		if( tmp < dA ) { /* near color key == bg */
			return 0.0;
		}
		if( tmp < dB ) { /* middle region */
			return (tmp - dA)/(dB - dA); /* distance to key color */
		}
		return 1.0; /* far from color key == fg */
}



void rgbkey_apply(VJFrame *frame, VJFrame *frame2, int width,int height, int tola, int r, int g,int b, int tolb, int show, int alpha)
{
	unsigned int pos;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];
	uint8_t *B = frame2->data[3];
	uint8_t *A = frame->data[3];
	int iy,iu,iv;
	_rgb2yuv(r,g,b,iy,iu,iv);

	if( alpha == 0 ) {
		for (pos = (width * height); pos != 0; pos--) {
			double d = color_distance( Cb[pos],Cr[pos],iu,iv,tola,tolb );
			uint8_t op1 = (d * 255);
			if( op1 == 0 ) {
				Y[pos] = Y2[pos];
				Cb[pos] = Cb2[pos];
				Cr[pos] = Cr2[pos];
			}
			else {
				uint8_t op0 = op1;
				op1 = 255 - op1;
				Y[pos] = ( (op0 * Y[pos]) + (op1 * Y2[pos])) >> 8;;
				Cb[pos] = ( (op0 * Cb[pos]) + (op1 * Cb2[pos]))>> 8;
				Cr[pos] = ( (op0 * Cr[pos]) + (op1 * Cr2[pos]))>>8;
			}
		}
	}
	else {
		switch(alpha) {
			case 1:
				for (pos = (width * height); pos != 0; pos--) {
					if( A[pos] == 0 )
						continue;
					double d = color_distance( Cb[pos],Cr[pos],iu,iv,tola,tolb );
					uint8_t op1 = (d * 255);
					if( op1 == 0 ) {
						Y[pos] = Y2[pos];
						Cb[pos] = Cb2[pos];
						Cr[pos] = Cr2[pos];
					}
					else {
						uint8_t op0 = op1;
						op1 = 255 - op1;
						Y[pos] = ( (op0 * Y[pos]) + (op1 * Y2[pos])) >> 8;;
						Cb[pos] = ( (op0 * Cb[pos]) + (op1 * Cb2[pos]))>> 8;
						Cr[pos] = ( (op0 * Cr[pos]) + (op1 * Cr2[pos]))>>8;
					}
				}
				break;
			case 2:
				for (pos = (width * height); pos != 0; pos--) {
					if( A[pos] == 0 && B[pos] == 0)
						continue;
					double d = color_distance( Cb[pos],Cr[pos],iu,iv,tola,tolb );
					uint8_t op1 = (d * 255);
					if( op1 == 0 ) {
						Y[pos] = Y2[pos];
						Cb[pos] = Cb2[pos];
						Cr[pos] = Cr2[pos];
					}
					else {
						uint8_t op0 = op1;
						op1 = 255 - op1;
						Y[pos] = ( (op0 * Y[pos]) + (op1 * Y2[pos])) >> 8;;
						Cb[pos] = ( (op0 * Cb[pos]) + (op1 * Cb2[pos]))>> 8;
						Cr[pos] = ( (op0 * Cr[pos]) + (op1 * Cr2[pos]))>>8;
					}
				}
				break;
			case 3:
				for (pos = (width * height); pos != 0; pos--) {
					if( A[pos] == 0 || B[pos] == 0)
						continue;
					double d = color_distance( Cb[pos],Cr[pos],iu,iv,tola,tolb );
					uint8_t op1 = (d * 255);
					if( op1 == 0 ) {
						Y[pos] = Y2[pos];
						Cb[pos] = Cb2[pos];
						Cr[pos] = Cr2[pos];
					}
					else {
						uint8_t op0 = op1;
						op1 = 255 - op1;
						Y[pos] = ( (op0 * Y[pos]) + (op1 * Y2[pos])) >> 8;;
						Cb[pos] = ( (op0 * Cb[pos]) + (op1 * Cb2[pos]))>> 8;
						Cr[pos] = ( (op0 * Cr[pos]) + (op1 * Cr2[pos]))>>8;
					}
				}
				break;
			case 4:
				for (pos = (width * height); pos != 0; pos--) {
					if( B[pos] == 0 )
						continue;
					double d = color_distance( Cb[pos],Cr[pos],iu,iv,tola,tolb );
					uint8_t op1 = (d * 255);
					if( op1 == 0 ) {
						Y[pos] = Y2[pos];
						Cb[pos] = Cb2[pos];
						Cr[pos] = Cr2[pos];
					}
					else {
						uint8_t op0 = op1;
						op1 = 255 - op1;
						Y[pos] = ( (op0 * Y[pos]) + (op1 * Y2[pos])) >> 8;;
						Cb[pos] = ( (op0 * Cb[pos]) + (op1 * Cb2[pos]))>> 8;
						Cr[pos] = ( (op0 * Cr[pos]) + (op1 * Cr2[pos]))>>8;
					}
				}
				break;
		}
	}

}
