/* 
 * Linux VeeJay
 *
 * Copyright(C)2010 Niels Elburg <nwelburg@gmail.com>
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
#include <stdlib.h>
#include <libvje/vje.h>
#include <libvjmem/vjmem.h>
#include "median.h"
#include <ctmf/ctmf.h>
vj_effect *medianfilter_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 1;

	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 0;
	ve->limits[1][0] = (h / 4) - 1;
	ve->defaults[0] = 3;// 255;
	ve->description = "Constant Time Median Filter";
	ve->sub_format = 0;
	ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Value" );
	return ve;
}

void medianfilter_apply( VJFrame *frame, int val)
{
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	if( val == 0 )
	   return; 

	 uint8_t *buffer = (uint8_t*) vj_malloc(sizeof(uint8_t)*frame->width*frame->height*3);
	 veejay_memset( buffer,0, frame->width*frame->height*3);
	 ctmf( Y, buffer, frame->width,frame->height,frame->width,frame->width,val,1,1024*1024*8);
	 ctmf( Cb,buffer + (frame->width*frame->height), frame->width,frame->height/2,frame->width,frame->width,val,1,512*1024);
	 ctmf( Cr,buffer + (frame->width*frame->height*2), frame->width,frame->height/2,frame->width,frame->width,val,1,512*1024);

	 veejay_memcpy( Y, buffer, frame->width*frame->height);
	 veejay_memcpy( Cb,buffer + (frame->width*frame->height), frame->width*frame->height);
	 veejay_memcpy( Cr,buffer + (frame->width*frame->height*2), frame->width*frame->height);
	 
	 free(buffer);

}
