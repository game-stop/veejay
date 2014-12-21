/* 
 * Linux VeeJay
 *
 * Cvalyright(C) 2009 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your valtion) any later version.
 *
 * This program is distributed in the hvale that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a cvaly of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

/*  "weed"-plugin partially ported from LiVES (C) G. Finch (Salsaman) 2009
 *
 *  weed-plugins/multi_transitions.c?revision=286
 *
 */
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "iris.h"
#include "common.h"

vj_effect *iris_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 100;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 1;
    ve->defaults[0] = 1;
	ve->defaults[1] = 0;
    ve->description = "Iris Transition (Circle,Rect)";
    ve->sub_format = 1; //@todo: write this for native
    ve->extra_frame = 1;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Value", "Shape" );
    return ve;
}


void iris_apply( VJFrame *frame, VJFrame *frame2, int width, int height, int val, int shape)
{
    int i,j,k=0;
    int len = (width * height);

    uint8_t *Y0 = frame->data[0];
    uint8_t *Cb0 = frame->data[1];
    uint8_t *Cr0 = frame->data[2];

	uint8_t *Y1 = frame2->data[0];
    uint8_t *Cb1 = frame2->data[1];
    uint8_t *Cr1 = frame2->data[2];

	int half_wid = width >> 1;
	int half_hei = height >> 1;

	float 	val0 = (val * 0.01f );
	float 	val1 = 1.0f - val0;
	if( shape == 0 ) {
		float	x1,y1;
		double	rad	= (double) ( (half_hei * half_hei) + (half_wid * half_wid ) ); 
		double  sval=0;

		for( i = 0; i < len; i += width ) {
		 for( j = 0; j < width; j ++ ) {
			//@ todo: extend this with feather for smoothness
			x1 = (float)( k - half_hei );
			y1 = (float)( j - half_wid );
			fast_sqrt( sval, (x1*x1+y1*y1)/rad);
			if( sval > val0 ) {	
		//	if( sqrt( (x1 * x1 + y1 * y1 )/ rad)  > val0 ) {
				Y0[ i + j] = Y1[ i + j ];
				Cb0[i + j] = Cb1[ i + j ];
				Cr0[i + j] = Cr1[ i + j ];
			}	
		 }
		 k ++;
		}
	} else if( shape == 1 ) {
		float x1,y1;
		for( i = 0; i < len; i += width ) {
			for( j = 0; j < width; j ++ ){
				//@ todo: extend this with feather for smoothness
				x1 = (float)half_wid * val1 + 0.5f;
				y1 = (float)half_hei * val1 + 0.5f;

				if( j < x1 || j>=(width-x1) || k < y1 || k >= ( height-y1 )) {
					Y0[ i + j] = Y1[ i + j ];
					Cb0[i + j] = Cb1[ i + j ];
					Cr0[i + j] = Cr1[ i + j ];
				}	
			}
			k++;
		}
	} 
}
