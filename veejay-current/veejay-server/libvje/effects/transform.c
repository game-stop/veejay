 /* 
  * Linux VeeJay
  *
  * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "transform.h"

vj_effect *transform_init(int width,int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = (height / 16);

    ve->description = "Transform Cubics";
    ve->sub_format = 1;
    ve->extra_frame = 0;
   	ve->has_user = 0;
	ve->parallel = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Cubics");
    return ve;
}

void transform_apply(void *ptr, VJFrame *frame, int *args)
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb= frame->data[1];
	uint8_t *restrict Cr= frame->data[2];

    const unsigned int size = args[0];
	const unsigned int hsize = size >> 1;
	for (unsigned int y = 1; y < height; y++)
	{
   		unsigned int ty_offset = y % size;
    	unsigned int ty = (y / size) % 2 ? y - ty_offset + hsize : y + ty_offset - hsize;
    	ty = (ty < 0) ? 0 : (ty >= (height - 1)) ? height - 1 : ty;
    	for (unsigned int x = 1; x < width; x++)
    	{
        	unsigned int tx_offset = x % size;
        	unsigned int tx = (x / size) % 2 ? x - tx_offset + hsize : x + tx_offset - hsize;
        	tx = (tx < 0) ? 0 : (tx >= (width - 1)) ? width - 1 : tx;

        	Y[x + (y * width)] = Y[(ty * width) + tx];
        	Cb[x + (y * width)] = Cb[(ty * width) + tx];
        	Cr[x + (y * width)] = Cr[(ty * width) + tx];
    	}
	}


}
