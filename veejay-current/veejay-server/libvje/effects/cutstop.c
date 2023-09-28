/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "cutstop.h"

typedef struct {
    uint8_t *vvcutstop_buffer[4];
    unsigned int frq_cnt;
} cutstop_t;

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

    return ve;
}

void *cutstop_malloc(int width, int height)
{
    cutstop_t *c = (cutstop_t*) vj_calloc(sizeof(cutstop_t));
    if(!c) {
        return NULL;
    }

    const int len = (width*height);
    const int total_len = 3 * len;

    c->vvcutstop_buffer[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * total_len);
    if(!c->vvcutstop_buffer[0]) {
        free(c);
        return NULL;
    }

    c->vvcutstop_buffer[1] = c->vvcutstop_buffer[0] + len;
    c->vvcutstop_buffer[2] = c->vvcutstop_buffer[1] + len;

	veejay_memset( c->vvcutstop_buffer[0], pixel_Y_lo_, width*height);
	veejay_memset( c->vvcutstop_buffer[1],128,(width*height));
	veejay_memset( c->vvcutstop_buffer[2],128,(width*height));

    c->frq_cnt = 256;

	return (void*) c;
}

void cutstop_free(void *ptr) {
    cutstop_t *c = (cutstop_t*) ptr;
    free(c->vvcutstop_buffer[0]);
    free(c);
}


void cutstop_apply( void *ptr, VJFrame *frame, int *args) {
    int threshold = args[0];
    int freq = args[1];
    int cutmode = args[2];
    int holdmode = args[3];

    cutstop_t *c = (cutstop_t*) ptr;

	int i=0;
	const int len = frame->len;

	uint8_t *Yb = c->vvcutstop_buffer[0];
	uint8_t *Ub = c->vvcutstop_buffer[1];
	uint8_t *Vb = c->vvcutstop_buffer[2];
	uint8_t *Yd = frame->data[0];
	uint8_t *Ud = frame->data[1];
	uint8_t *Vd = frame->data[2];
	
	c->frq_cnt = c->frq_cnt + freq;
	
	if (freq == 255 || c->frq_cnt > 255) {
		veejay_memcpy(Yb, Yd, len);
		veejay_memcpy(Ub, Ud, len);
		veejay_memcpy(Vb, Vd, len);
		c->frq_cnt = 0;
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

