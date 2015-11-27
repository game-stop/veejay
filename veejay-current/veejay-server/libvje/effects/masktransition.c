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

#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "masktransition.h"
#include "common.h"

vj_effect *masktransition_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1000;
    ve->defaults[0] = 0; 
    ve->defaults[1] = 50;
    ve->description = "Alpha: Transition Map";
	ve->sub_format = 1;
    ve->extra_frame = 1;
  	ve->has_user = 0; 
	ve->parallel = 1;
	ve->param_description = vje_build_param_list(ve->num_params, "Time Index", "Smooth" );
    return ve;
}

static inline double edge( int ti, uint8_t A, int d)
{
	if( ti < A )
		return 0.0;
	if( ti >= (A + d))
		return 1.0;
	return (double) (ti - A) / d;
}

void masktransition_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int time_index, int duration  )
{
	unsigned int i;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aA = frame->data[3];
	uint8_t *aB = frame2->data[3];
	const unsigned int len = frame->len;
	const int dur = 1 + duration;
	int t;
	uint8_t op0,op1;
	for( i = 0; i < len; i ++ ) {
		double ratio = edge( time_index, aA[i], dur );	
		op0 = 0xff * ratio;
		op1 = 0xff - op0;

		Y[i] = ((op0 * Y[i]) + (op1 * Y2[i])) >> 8;
		Cb[i]= ((op0 * Cb[i])+ (op1 * Cb2[i]))>> 8;
		Cr[i]= ((op0 * Cr[i])+ (op1 * Cr2[i]))>> 8;
	}

}
