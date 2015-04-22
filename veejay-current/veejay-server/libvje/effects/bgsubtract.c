/* 
 * Linux VeeJay
 *
 * Copyright(C)2008 Niels Elburg <nwelburg@gmail.com>
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
#include "bgsubtract.h"
#include "common.h"
#include <math.h>
#include <libyuv/yuvconv.h>
#include <libvjmsg/vj-msg.h>
#include "softblur.h"
static uint8_t *static_bg = NULL;

vj_effect *bgsubtract_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;	/* threshold */
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;	/* mode */
    ve->limits[1][1] = 1;
    ve->defaults[0] = 45;
    ve->defaults[1] = 0;
    ve->description = "Substract background (static, requires bg mask)";
    ve->extra_frame = 0;
    ve->sub_format = 0;
    ve->has_user = 1;
    ve->user_data = NULL;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Mode");
    return ve;
}

#define rup8(num)(((num)+8)&~8)
int bgsubtract_malloc(int width, int height)
{
	if(static_bg == NULL)	
		static_bg = (uint8_t*) vj_calloc( rup8( width + width * height * sizeof(uint8_t)) );
	return 1;
}

void bgsubtract_free()
{
	if( static_bg )
		free(static_bg );
	static_bg = NULL;
}

int bgsubtract_prepare(uint8_t *map[4], int width, int height)
{
	if(!static_bg )
	{
		return 0;
	}
	
	//@ copy the iamge
	veejay_memcpy( static_bg, map[0], (width*height));
	
	VJFrame tmp;
	veejay_memset( &tmp, 0, sizeof(VJFrame));
	tmp.data[0] = static_bg;
	tmp.width = width;
	tmp.height = height;

	//@ 3x3 blur
	softblur_apply( &tmp, width,height,0);

	veejay_msg(2, "Substract background: Snapped background frame");
	return 1;
}

void bgsubtract_apply(VJFrame *frame,int width, int height, int threshold, int mode )
{
	VJFrame tmp;
	veejay_memset( &tmp, 0, sizeof(VJFrame));
	tmp.data[0] = frame->data[0];
	tmp.width = width;
	tmp.height = height;

	//@ 3x3 blur
	softblur_apply( &tmp, width,height,0);

	if ( mode == 0 ) {
		binarify( frame->data[0], static_bg,frame->data[0], threshold, 0, width*height );
	} else if ( mode == 1 ) {
		binarify( frame->data[0], static_bg, frame->data[0], threshold, 1,width*height );
	}

	veejay_memset( frame->data[1], 128, frame->uv_len );
	veejay_memset( frame->data[2], 128, frame->uv_len );
	
}




