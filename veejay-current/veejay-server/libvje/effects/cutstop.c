/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
 *
 * vvCutStop - ported from vvFFPP_basic
 * Copyright(C)2005 Maciek Szczesniak <maciek@visualvinyl.net>
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
#include <stdlib.h>
#include "common.h"
#include "split.h"
#include <stdlib.h>
#include <stdio.h>

static uint8_t *vvcutstop_buffer[4] = { NULL,NULL,NULL,NULL };
static unsigned int frq_cnt = 0;

vj_effect *cutstop_init(int width , int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 40; // treshold
    ve->defaults[1] = 50; // hold frame freq
    ve->defaults[2] = 0;   // cut mode
    ve->defaults[3] = 0;   // hold front/back

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;

    ve->description = "vvCutStop";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Frame freq", "Cut mode", "Hold front/back" );	
	frq_cnt = 256;

    return ve;
}

int	cutstop_malloc(int width, int height)
{
	int i;
	for( i = 0; i < 3 ;i ++ ) {
		vvcutstop_buffer[i] = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8( width * height )); 
		if(!vvcutstop_buffer[i] )
			return 0;
	}
	veejay_memset( vvcutstop_buffer[0],0, width*height);
	veejay_memset( vvcutstop_buffer[1],128,(width*height));
	veejay_memset( vvcutstop_buffer[2],128,(width*height));
	return 1;
}

void cutstop_free() {
	int i;
	for( i = 0; i < 3; i ++ ) {
		if(vvcutstop_buffer[i]) 
		  free(vvcutstop_buffer[i]);
		vvcutstop_buffer[i] = NULL;
	}
}


void cutstop_apply( VJFrame *frame, int width, int height, int threshold, int freq, int cutmode, int holdmode) {
	int i=0;
	const unsigned int len = frame->len;
 
	uint8_t *Yb = vvcutstop_buffer[0];
	uint8_t *Ub = vvcutstop_buffer[1];
	uint8_t *Vb = vvcutstop_buffer[2];
	uint8_t *Yd = frame->data[0];
	uint8_t *Ud = frame->data[1];
	uint8_t *Vd = frame->data[2];
	
	frq_cnt = frq_cnt + freq;
	
	if (freq == 255 || frq_cnt > 255) {
		veejay_memcpy(Yb, Yd, len);
		veejay_memcpy(Ub, Ud, len);
		veejay_memcpy(Vb, Vd, len);
		frq_cnt = 0;
	}	
	// moved cutmode & holdmode outside loop	
	if(cutmode && !holdmode)
	{
		for( i = 0; i < len; i ++ )
			if( threshold > Yb[i] )
			{
				Yd[i] = Yb[i]; Ud[i] = Ub[i]; Vd[i] = Vb[i];	
			}	
	}
	if(cutmode && holdmode)
	{
		for( i = 0; i < len; i ++ )
			if( threshold > Yd[i] )
			{
				Yd[i] = Yb[i]; Ud[i] = Ub[i]; Vd[i] = Vb[i];
			}
	}
	if(!cutmode && holdmode)
	{
		for( i =0 ; i < len; i ++ )
			if( threshold < Yd[i])
			{ 
				Yd[i] = Yb[i]; Ud[i] = Ub[i]; Vd[i] = Vb[i];
			}
	}
	if(!cutmode && !holdmode)
	{
		for( i = 0; i < len; i ++ )
			if(threshold < Yb[i])
			{
				Yd[i] = Yb[i]; Ud[i] = Ub[i]; Vd[i] = Vb[i];
			}
	}

}

