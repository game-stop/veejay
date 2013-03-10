/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include "pixelate.h"

static uint8_t values[2048];

vj_effect *pixelate_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    int i;
    int nvalues=0;
    for(i=1; i < width; i++)
    {
	if( (width%i)== 0)
	{
		values[nvalues] = i;
		nvalues++; 
	}
    }

    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = nvalues-2;
    ve->defaults[0] = 8;
    ve->description = "Pixelate";
    ve->sub_format = 0;
    ve->extra_frame = 0;

	ve->has_user =0;
	ve->param_description = vje_build_param_list( ve->num_params, "Pixels");
    return ve;
}

void pixelate_apply( VJFrame *frame, int w, int h , int vv )
{
	unsigned int i,j ;
	unsigned int len = frame->len;
  	const unsigned int v = values[vv];
	const unsigned int uv_len = frame->uv_len;
        const unsigned int u_v = v >> frame->shift_h;
    	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

	for (i = 0; i < len; i+=v) {
	   for(j=0; j < v; j++)
		{
		Y[i+j] = Y[i];
	    	}
    	}

	for (i = 0; i < uv_len; i+=u_v) {
	   for(j=0; j < u_v; j++)
		{
		Cb[i+j] = Cb[i];
		Cr[i+j] = Cr[i];
	    	}
    	}
}

