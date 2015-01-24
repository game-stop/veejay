/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <elburg@hio.hen.nl>
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
#include "common.h"
#include "negation.h"
vj_effect *negation_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 3;// 255;
    ve->description = "Negation";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->parallel = 1;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Value" );
    return ve;
}


void negation_apply( VJFrame *frame, int width, int height, int val)
{
    int i;
    int len = (width * height);
    int uv_len = frame->uv_len;

    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

#ifndef HAVE_ASM_MMX
    for (i = 0; i < len; i++) {
	*(Y) = val - *(Y);
	*(Y)++;
    }

    for (i = 0; i < uv_len; i++) {
	*(Cb) = val - *(Cb);
        *(Cb)++;
        *(Cr) = val - *(Cr);
	*(Cr)++;
    }
#else

    int left = len % 8;
    int work=  len >> 3;

    vje_load_mask(val);

    for( i = 0; i < work ; i ++ )
    {
	vje_mmx_negate( Y, Y );	
	Y += 8;
    }	

    if (left )
    {
	for( i = 0; i < left; i ++ )
	{
		*(Y) = val - *(Y);
		*(Y)++;
	}	
    }

    work = uv_len >> 3;
    left = uv_len % 8;
    for( i = 0; i < work ; i ++ )
    {
	vje_mmx_negate( Cb, Cb );
	vje_mmx_negate( Cr, Cr );
	Cb += 8;
	Cr += 8;
    }

    if(left )
    {
	for( i = 0; i < left; i ++ )
	{
		*(Cb) = val - *(Cb);
      	 	*(Cb)++;
     	  	*(Cr) = val - *(Cr);
		*(Cr)++;
	}
    }

    do_emms;
#endif
}
