/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nelburg@gmail.com>
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

/*
 * originally from http://gc-films.com/chromakey.html
 */

#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "rgbkey.h"
#include <math.h>
#include "common.h"

vj_effect *alphaselect2_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 15;	/* tolerance near */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 255;	/* g */
    ve->defaults[3] = 0;	/* b */
	ve->defaults[4] = 15;	/* tolerance far */
	ve->defaults[5] = 0;    /* alpha operator */

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
	ve->limits[1][5] = 2;

	ve->has_user = 0;
    ve->parallel = 1;
	ve->description = "Alpha: Set by color key";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
	ve->param_description = vje_build_param_list(ve->num_params,"Tolerance Near","Red","Green","Blue", "Tolerance Far", "Alpha Operator");

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

void alphaselect2_apply( VJFrame *frame, int width,
		   int height, int tola, int r, int g,
		   int b, int tolb,int alpha)
{
    unsigned int pos;
	const unsigned int len = width * height;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *A  = frame->data[3];
	int cb,cr,iy,iu,iv;
	_rgb2yuv(r,g,b,iy,iu,iv);

	if(alpha == 0 ) {
		for (pos = len; pos != 0; pos--) {
			double d = color_distance( Cb[pos],Cr[pos],iu,iv,tola,tolb );
			uint8_t alpha = (uint8_t) (d * 255.0);
			if( alpha < 0xff ) {
				Cb[pos] = 128;
				Cr[pos] = 128;
			}
			else if (alpha == 0) {
				Cb[pos] = 128;
				Cr[pos] = 128;
				Y[pos] = pixel_Y_lo_;
			}
			A[pos] = alpha;
		}
		return;
	}
	
	switch(alpha)
	{
		case 1:
			for (pos = len; pos != 0; pos--) {
				double d = color_distance( Cb[pos],Cr[pos],iu,iv,tola,tolb );
				A[pos] = (uint8_t) (d * 255.0);
				if( A[pos] < 0xff ) {
					Y[pos] = A[pos];
					Cb[pos] = 128;
					Cr[pos] = 128;
				}
				else if( A[pos] == 0) {
					Cb[pos] = 128;
					Cr[pos] = 128;
					Y[pos] = pixel_Y_lo_;
				}
			}
			break;
		case 2:
			for (pos = len; pos != 0; pos--) {
				if(A[pos] == 0 )
					continue;
				if(A[pos] == 0xff) {
					Y[pos] = 0xff;
					Cb[pos] = 128;
					Cr[pos] = 128;
				}
				else {
					double d = color_distance( Cb[pos],Cr[pos],iu,iv,tola,tolb );
					A[pos] = (uint8_t) (d * 255.0);
					if( A[pos] < 0xff ) {
						Y[pos] = A[pos];
						Cb[pos] = 128;
						Cr[pos] = 128;
					}
					else if( A[pos] == 0) {
						Cb[pos] = 128;
						Cr[pos] = 128;
						Y[pos] = pixel_Y_lo_;
					}
				}
			}
			break;

	}
}

