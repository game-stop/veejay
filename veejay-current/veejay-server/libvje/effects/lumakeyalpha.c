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
    ve->limits[1][0] = 8;
    ve->limits[0][1] = 0;	/* opacity */
    ve->limits[1][1] = 255;
    ve->defaults[0] = 0;
    ve->defaults[1] = 150;
    ve->description = "Alpha: Luma Key composite";
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
			for( i = 0; i < len; i ++ ) { /* skip alpha-IN 0 */
				if( aA[i] == 0 )
					continue;

				Y[i] = (op0 * Y[i] + op1 * Y2[i]) >> 8;
				Cb[i]= (op0 * Cb[i] + op1 * Cb2[i])>> 8;
				Cr[i]= (op0 * Cr[i] + op1 * Cr2[i])>> 8;
			}
			break;
		case 1:							/* skip alpha-IN 1 */
			for( i = 0; i < len; i ++ ) {
				if( aB[i] == 0 )
					continue;

				Y[i] = (op0 * Y[i] + op1 * Y2[i]) >> 8;
				Cb[i]= (op0 * Cb[i] + op1 * Cb2[i])>> 8;
				Cr[i]= (op0 * Cr[i] + op1 * Cr2[i])>> 8;
			}
			break;
		case 2:							/* skip alpa-IN 0 or alpha-IN 1 */
			for( i = 0; i < len; i ++ ) {
				if( aB[i] == 0 || aA[i] == 0 )
					continue;

				Y[i] = (op0 * Y[i] + op1 * Y2[i]) >> 8;
				Cb[i]= (op0 * Cb[i] + op1 * Cb2[i])>> 8;
				Cr[i]= (op0 * Cr[i] + op1 * Cr2[i])>> 8;
			}
			break;
		case 3:							/* skip if both */
			for( i = 0; i < len; i ++ ) {
				if( aB[i] == 0 && aA[i] == 0 )
					continue;

				Y[i] = (op0 * Y[i] + op1 * Y2[i]) >> 8;
				Cb[i]= (op0 * Cb[i] + op1 * Cb2[i])>> 8;
				Cr[i]= (op0 * Cr[i] + op1 * Cr2[i])>> 8;
			}
			break;

		case 4:  /* transparent */
			for( i = 0; i < len; i ++ ) {
				if( aA[i] == 0 )
					continue;
				uint8_t ab = (op0 * aB[i] + op1 * aA[i])>> 8;
				uint8_t aa = 0xff - ab;

				Y[i] = (aa * Y[i] + ab * Y2[i]) >> 8;
				Cb[i]= (aa * Cb[i] + ab * Y2[i])>> 8;
				Cr[i]= (aa * Cr[i] + ab * Y2[i])>> 8;
			}
			break;
		case 5:
			for( i = 0; i < len; i ++ ) {
				if( aB[i] == 0 )
					continue;
				uint8_t ab = (op0 * aB[i] + op1 * aA[i])>> 8;
				uint8_t aa = 0xff - ab;

				Y[i] = (aa * Y[i] + ab * Y2[i]) >> 8;
				Cb[i]= (aa * Cb[i] + ab * Y2[i])>> 8;
				Cr[i]= (aa * Cr[i] + ab * Y2[i])>> 8;
			}
			break;
		case 6:
			for( i = 0; i < len; i ++ ) {
				if( aA[i] == 0 || aB[i] == 0 )
					continue;
				uint8_t ab = (op0 * aB[i] + op1 * aA[i])>> 8;
				uint8_t aa = 0xff - ab;

				Y[i] = (aa * Y[i] + ab * Y2[i]) >> 8;
				Cb[i]= (aa * Cb[i] + ab * Y2[i])>> 8;
				Cr[i]= (aa * Cr[i] + ab * Y2[i])>> 8;
			}
			break;
		case 7:
			for( i = 0; i < len; i ++ ) {
				if( aA[i] == 0 && aB[i] == 0 )
					continue;
				uint8_t ab = (op0 * aB[i] + op1 * aA[i])>> 8;
				uint8_t aa = 0xff - ab;

				Y[i] = (aa * Y[i] + ab * Y2[i]) >> 8;
				Cb[i]= (aa * Cb[i] + ab * Y2[i])>> 8;
				Cr[i]= (aa * Cr[i] + ab * Y2[i])>> 8;
			}
			break;
		case 8: // write back
			for( i = 0; i < len; i ++ ) {
				if( aA[i] == 0 || aB[i] == 0 )
					continue;
				uint8_t ab = (op0 * aB[i] + op1 * aA[i])>> 8;
				uint8_t aa = 0xff - ab;

				Y[i] = (aa * Y[i] + ab * Y2[i]) >> 8;
				Cb[i]= (aa * Cb[i] + ab * Y2[i])>> 8;
				Cr[i]= (aa * Cr[i] + ab * Y2[i])>> 8;
				aA[i]= aa;
			}
			break;
		default:
			break;
	}
}
