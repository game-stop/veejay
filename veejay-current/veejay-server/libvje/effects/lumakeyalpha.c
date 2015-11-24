/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
#include "lumakeyalpha.h"
#include "common.h"

vj_effect *lumakeyalpha_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;	/* type */
    ve->limits[1][0] = 7;
    ve->limits[0][1] = 0;	/* opacity */
    ve->limits[1][1] = 255;
    ve->defaults[0] = 0;
    ve->defaults[1] = 150;
    ve->description = "Alpha: Luma Key Matte";
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->parallel = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Selector Mode", "Opacity" );

    return ve;
}


void lumakeyalpha_apply( VJFrame *frame, VJFrame *frame2, int width,int height, int type, int opacity )
{
	unsigned int i, len = width * height;
    unsigned int op1 = (opacity > 255) ? 255 : opacity;
    unsigned int op0 = 255 - op1;

	unsigned int x,y;

	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *aA = frame->data[3];
	uint8_t *aB = frame2->data[3];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cb2 =frame2->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Cr2= frame2->data[2];

	switch( type ) {
		case 0:
			for( i = 0; i < len; i ++ ) {
				if( aB[i] == 0 ) 
					continue;

				Y[i] = FEATHER( Y[i],op0,aB[i],Y2[i],op1 );
				Cb[i]= FEATHER(Cb[i],op0,aB[i],Cb2[i],op1 );
				Cr[i]= FEATHER(Cr[i],op0,aB[i],Cr2[i],op1 );	
			}	
			break;
		case 1:							
			for( i = 0; i < len; i ++ ) {
				if( aA[i] == 0 )
					continue;

				Y[i] = FEATHER( Y[i],op0,aB[i],Y2[i],op1 );
				Cb[i]= FEATHER(Cb[i],op0,aB[i],Cb2[i],op1 );
				Cr[i]= FEATHER(Cr[i],op0,aB[i],Cr2[i],op1 );	
			}
			break;
		case 2:							
			for( i = 0; i < len; i ++ ) {
				if( aB[i] == 0 || aA[i] == 0 )
					continue;

				Y[i] = FEATHER( Y[i],op0,aB[i],Y2[i],op1 );
				Cb[i]= FEATHER(Cb[i],op0,aB[i],Cb2[i],op1 );
				Cr[i]= FEATHER(Cr[i],op0,aB[i],Cr2[i],op1 );	
			}
			break;
		case 3:							
			for( i = 0; i < len; i ++ ) {
				if( aB[i] == 0 && aA[i] == 0 )
					continue;

				Y[i] = FEATHER( Y[i],op0,aB[i],Y2[i],op1 );
				Cb[i]= FEATHER(Cb[i],op0,aB[i],Cb2[i],op1 );
				Cr[i]= FEATHER(Cr[i],op0,aB[i],Cr2[i],op1 );	
			}
			break;
		case 4:
			for( i = 0; i < len; i ++ ) {
				if( aB[i] == 0 ) 
					continue;

				Y[i] = FEATHER( Y[i],op0,aA[i],Y2[i],op1 );
				Cb[i]= FEATHER(Cb[i],op0,aA[i],Cb2[i],op1 );
				Cr[i]= FEATHER(Cr[i],op0,aA[i],Cr2[i],op1 );
			}	
			break;
		case 5:						
			for( i = 0; i < len; i ++ ) {
				if( aA[i] == 0 )
					continue;

				Y[i] = FEATHER( Y[i],op0,aA[i],Y2[i],op1 );
				Cb[i]= FEATHER(Cb[i],op0,aA[i],Cb2[i],op1 );
				Cr[i]= FEATHER(Cr[i],op0,aA[i],Cr2[i],op1 );	
			}
			break;
		case 6:	
			for( i = 0; i < len; i ++ ) {
				if( aB[i] == 0 || aA[i] == 0 )
					continue;

				Y[i] = FEATHER( Y[i],op0,aA[i],Y2[i],op1 );
				Cb[i]= FEATHER(Cb[i],op0,aA[i],Cb2[i],op1 );
				Cr[i]= FEATHER(Cr[i],op0,aA[i],Cr2[i],op1 );	
			}
			break;
		case 7:	
			for( i = 0; i < len; i ++ ) {
				if( aB[i] == 0 && aA[i] == 0 )
					continue;

				Y[i] = FEATHER( Y[i],op0,aA[i],Y2[i],op1 );
				Cb[i]= FEATHER(Cb[i],op0,aA[i],Cb2[i],op1 );
				Cr[i]= FEATHER(Cr[i],op0,aA[i],Cr2[i],op1 );	
			}
			break;
		
		default:
			break;
	}
}
