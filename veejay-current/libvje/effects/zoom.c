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
#include <stdlib.h>
#include <stdint.h>
#include <libvje/vje.h>
#include <veejay/vj-viewport.h>

vj_effect *zoom_init(int width , int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = width/2;
    ve->defaults[1] = height/2;
    ve->defaults[2] = 50;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = width;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = height;

    ve->limits[0][2] = 10;
    ve->limits[1][2] = 100;

    ve->description = "Zoom";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    return ve;
}
static int zoom_[3] = { 0,0,0 };
static void *zoom_vp_ = NULL;

static uint8_t *zoom_private_[3];

int	zoom_malloc(int width, int height)
{
	zoom_private_[0] = (uint8_t*) vj_yuvalloc( width,height );
	if(!zoom_private_[0] )
		return 0;
	zoom_private_[1] = zoom_private_[0] + (width*height);
	zoom_private_[2] = zoom_private_[1] + (width*height);
	return 1;
}

void zoom_free() {
	if( zoom_private_[0] )
		free(zoom_private_[0] );
	if( zoom_vp_ )
		viewport_destroy( zoom_vp_ );
	zoom_vp_ = NULL;
}

void zoom_apply( VJFrame *frame, int width, int height, int x, int y, int factor)
{
	if( zoom_[0] != x || zoom_[1] != y || zoom_[2] != factor || !zoom_vp_)
	{
		if( zoom_vp_ )
			viewport_destroy( zoom_vp_ );
		zoom_vp_ = viewport_fx_init( VP_QUADZOOM, width,height,x,y,factor );
		if(!zoom_vp_ )
			return;
		zoom_[0] = x; zoom_[1] = y; zoom_[2] = factor;
	}

	veejay_memcpy( zoom_private_[0], frame->data[0], (width*height));
	veejay_memcpy( zoom_private_[1], frame->data[1], (width*height));
	veejay_memcpy( zoom_private_[2], frame->data[2], (width*height));

	viewport_process_dynamic( zoom_vp_, zoom_private_, frame->data );
}

