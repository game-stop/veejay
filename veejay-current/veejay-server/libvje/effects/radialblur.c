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
/*
 * Copyright (C) 2000-2004 the xine project
 * 
 * This file is part of xine, a free video player.
 * 
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: radialblur.c,v 1.1.1.1 2004/10/27 23:49:01 niels Exp $
 *
 * mplayer's boxblur
 * Copyright (C) 2002 Michael Niedermayer <michaelni@gmx.at>
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "radialblur.h"

typedef struct {
    uint8_t *radial_src[4];
} radialblur_t;

vj_effect *radialblur_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 15;
    ve->defaults[1] = 0;
    ve->defaults[2] = 2;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 90; // radius
    ve->limits[0][1] = 0; // power
    ve->limits[1][1] = 100;
    ve->limits[0][2] = 0; // direction
    ve->limits[1][2] = 2; // 2 = both
    ve->description = "Radial Blur";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Power", "Direction"); 
    return ve;
}

void *radialblur_malloc(int w, int h)
{
    radialblur_t *r = (radialblur_t*) vj_calloc(sizeof(radialblur_t));
    if(!r) {
        return NULL;
    }

    r->radial_src[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * (w*h*3));
    if(!r->radial_src[0]) {
        free(r);
        return NULL;
    }

    r->radial_src[1] = r->radial_src[0] + (w*h);
    r->radial_src[2] = r->radial_src[1] + (w*h);
	
    return (void*) r;
}


static void rhblur_apply( uint8_t *dst , uint8_t *src, int w, int h, int r , int p)
{
	int y;
	for(y = 0; y < h ; y ++ )
	{
		blur2( dst + y * w, src + y *w , w, r,p, 1, 1);
	}	

}
static void rvblur_apply( uint8_t *dst, uint8_t *src, int w, int h, int r , int p)
{
	int x;
	for(x=0; x < w; x++)
	{
		blur2( dst + x, src + x , h, r, p, w, w );
	}
}


void radialblur_apply(void *ptr, VJFrame *frame, int *args ) {
    int radius = args[0];
    int power = args[1];
    int direction = args[2];

    radialblur_t *r = (radialblur_t*) ptr;

	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	const int uv_len = frame->uv_len;

	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

	if(radius == 0) return;
	// inplace
	int strides[4] = { len, uv_len, uv_len, 0 };
	vj_frame_copy( frame->data, r->radial_src, strides );

	switch(direction)
	{
		case 0: rhblur_apply( Y, r->radial_src[0],width, height, radius, power );
			rhblur_apply( Cb, r->radial_src[1],frame->uv_width, frame->uv_height, radius, power );
			rhblur_apply( Cr, r->radial_src[2],frame->uv_width, frame->uv_height, radius, power );
			break;
		case 1: rvblur_apply( Y, r->radial_src[0],width, height, radius, power ); 
			rvblur_apply( Cb, r->radial_src[1],frame->uv_width, frame->uv_height, radius, power );
			rvblur_apply( Cr, r->radial_src[2],frame->uv_width, frame->uv_height, radius, power );
			break;
		case 2:
			rhblur_apply( Y, r->radial_src[0],width, height, radius, power );
			rhblur_apply( Cb, r->radial_src[1],frame->uv_width, frame->uv_height, radius, power );
			rhblur_apply( Cr, r->radial_src[2],frame->uv_width, frame->uv_height, radius, power );
			rvblur_apply( Y, r->radial_src[0],width, height, radius, power ); 
			rvblur_apply( Cb, r->radial_src[1],frame->uv_width, frame->uv_height, radius, power );
			rvblur_apply( Cr, r->radial_src[2],frame->uv_width, frame->uv_height, radius, power );
			break;
		
	}
}

void radialblur_free(void *ptr)
{
    radialblur_t *r = (radialblur_t*) ptr;
    free( r->radial_src[0] );
    free( r );
}
