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

#include "common.h"
#include <libvjmem/vjmem.h>
#include "alphaselect2.h"

vj_effect *alphaselect2_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 15;	/* acceptance near */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 255;	/* g */
    ve->defaults[3] = 0;	/* b */
	ve->defaults[4] = 15;	/* acceptance far */
	ve->defaults[5] = 0;    /* alpha operator */

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 2550;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

	ve->limits[0][4] = 0;
	ve->limits[1][4] = 2550;

	ve->limits[0][5] = 0;
	ve->limits[1][5] = 3;

	ve->has_user = 0;
    ve->parallel = 1;
	ve->description = "Alpha: Set by color key";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
	ve->param_description = vje_build_param_list(ve->num_params, "Tolerance Near","Red","Green","Blue", "Tolerance Far", "Alpha Operator");

	ve->alpha = FLAG_ALPHA_SRC_A | FLAG_ALPHA_OPTIONAL | FLAG_ALPHA_OUT;
	ve->hints = vje_init_value_hint_list( ve->num_params );
	vje_build_value_hint_list( ve->hints, ve->limits[1][5],5, 
			
			"Ignore Alpha-IN", "Alpha-IN A or B", "Alpha-In A and B avg", "Alpha-In A and B" );


    return ve;
}

static inline double color_distance( uint8_t Cb, uint8_t Cr, int Cbk, int Crk, const double dA, const double dB )
{
		//double tmp = 0.0; 
		//fast_sqrt( tmp, (Cbk - Cb) * (Cbk-Cb) + (Crk - Cr) * (Crk - Cr) );

		double tmp = sqrt_table_get_pixel( (Cbk-Cb), (Crk-Cr) );
		
		if( tmp < dA ) { /* near color key == bg */
			return 0.0;
		}
		if( tmp < dB ) { /* middle region */
			return (tmp - dA)/(dB - dA); /* distance to key color */
		}
		return 1.0; /* far from color key == fg */
}

void alphaselect2_apply( VJFrame *frame,int tola, int r, int g,
		   int b, int tolb,int alpha)
{
    unsigned int pos;
	const int len = frame->len;
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *A  = frame->data[3];
	int iy=0,iu=128,iv=128;
	_rgb2yuv(r,g,b,iy,iu,iv);

	const double dtola = tola * 0.1f;
	const double dtolb = tolb * 0.1f;

	switch(alpha)
	{
		case 0:
			for (pos = len; pos != 0; pos--) {
				double d = color_distance( Cb[pos],Cr[pos],iu,iv,dtola,dtolb );
				A[pos] = (uint8_t) (d*255.0); /* overwrite alpha regardless */
			}
			break;
		case 1:
			for (pos = len; pos != 0; pos--) {
				double d = color_distance( Cb[pos],Cr[pos],iu,iv,dtola,dtolb );
				if( A[pos] == 0 ) {
					A[pos] = (uint8_t) (d * 255.0);
				}
			}
			break;
		case 2:
			for (pos = len; pos != 0; pos--) {
				double d = color_distance( Cb[pos],Cr[pos],iu,iv,dtola,dtolb );
				uint8_t tmp = (uint8_t) (d * 255.0);
				A[pos] = (A[pos] + tmp ) >> 1;
			}
			break;
		case 3:
			for (pos = len; pos != 0; pos--) {
				double d = color_distance( Cb[pos],Cr[pos],iu,iv,dtola,dtolb );
				int tmp = A[pos] + (uint8_t) (d * 255.0);    
				A[pos] = ( tmp > 0xff ? 0xff: tmp );
			}
			break;

	}
}

