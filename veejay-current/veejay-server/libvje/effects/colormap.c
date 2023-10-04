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
#include "colormap.h"

vj_effect *colormap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->defaults[0] = 46;
    ve->defaults[1] = 109;
    ve->defaults[2] = 92;

    ve->description = "Color Harmony";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Red","Green","Blue" );
    return ve;
}

void colormap_apply( void *ptr, VJFrame *frame, int *args ) {
    int r = args[0];
    int g = args[1];
    int b = args[2];

    unsigned int i;
    const int uv_len = frame->uv_len;
	
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
	
	uint8_t u_[256];
	uint8_t v_[256];

	for(i = 0; i < 256; i ++ )
	{
		u_[i] = ( i + b - g ) > 255 ? 255 : (( i + b - g) < 0 ? 0 : (  i + b - g ));
		v_[i] = ( i + r -g ) > 255 ? 255 : (( i + r - g ) < 0 ? 0 : ( i + r - g ));
	}
    
    for (i = 0; i < uv_len; i++)
	{
		Cb[i] = u_[ Cb[i] ];
		Cr[i] = v_[ Cr[i] ];
	}
}
