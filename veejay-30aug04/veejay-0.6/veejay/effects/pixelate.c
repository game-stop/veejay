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
#include "pixelate.h"
#include <stdlib.h>

static uint8_t values[512];

vj_effect *pixelate_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
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

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = nvalues-2;
    ve->defaults[0] = 8;
    ve->description = "Pixelate";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data= 0;

    

    return ve;
}

static void _pixelate_apply( uint8_t *yuv[3], int w, int h , int vv )
{
	unsigned int i,j ;
	unsigned int len = (w * h);
  	const unsigned int v = values[vv];
	const unsigned int uv_len = len /4;
        const unsigned int u_v = v/2;
	uint8_t *Yi = yuv[0];
    	uint8_t *Cb = yuv[1];
    	uint8_t *Cr = yuv[2];

	for (i = 0; i < len; i+=v) {
	   for(j=0; j < v; j++)
		{
		Yi[i+j] = Yi[i];
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

void pixelate_apply(uint8_t * yuv[3], int width, int height, int val)
{
  _pixelate_apply( yuv,width,height,val);
}
