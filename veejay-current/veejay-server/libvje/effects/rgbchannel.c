/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/yuvconv.h>
#include <libavutil/pixfmt.h>
#include "rgbchannel.h"

vj_effect *rgbchannel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;

    ve->defaults[0] = 0;
    ve->defaults[0] = 0;
    ve->defaults[0] = 0;
    ve->description = "RGB Channel";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->rgba_only = 1;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list(ve->num_params, "Red", "Green", "Blue");
    return ve;
}

void rgbchannel_apply( void *ptr, VJFrame *frame, int *args) {
    
    const int chr = args[0];
    const int chg = args[1];
    const int chb = args[2];
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	int row_stride = width * 4;
	int x,y;
	uint8_t *rgba = frame->data[0];

	if(chr)
	{
		for( y = 0; y < height; y ++ )
		{
			for( x = 0; x < row_stride; x += 4 )
			{
				rgba[ y * row_stride + x ] = 0;
			}
		}
	}
	if(chg)
	{
		for( y = 0; y < height; y ++ )
		{
			for( x = 1; x < row_stride-1; x += 4 )
			{
				rgba[ y * row_stride + x ] = 0;
			}
		}
	}
	if(chb)
	{
		for( y = 0; y < height; y ++ )
		{
			for( x = 2; x < row_stride-2; x += 4 )
			{
				rgba[ y * row_stride + x ] = 0;
			}
		}
	}

}
