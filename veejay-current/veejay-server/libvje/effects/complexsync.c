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
#include <stdint.h>
#include <stdio.h>
#include "complexsync.h"
#include <stdlib.h>

static uint8_t *c_outofsync_buffer[3];

vj_effect *complexsync_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = height-1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = (25 * 10);
    ve->defaults[0] = 36;
    ve->defaults[1] = 1;
    ve->defaults[2] = 1;
    ve->description = "Out of Sync -Replace selection-";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->has_user = 0;	
	ve->param_description = vje_build_param_list( ve->num_params, "Vertical size", "Mode", "Framespeed" );
    return ve;
}

int complexsync_malloc(int width, int height)
{
   c_outofsync_buffer[0] = (uint8_t*)vj_yuvalloc(width ,height );
   c_outofsync_buffer[1] = c_outofsync_buffer[0]  + (width * height );
   c_outofsync_buffer[2] = c_outofsync_buffer[1] + ( width * height );
   return 1;

}

void complexsync_free() {
	if(c_outofsync_buffer[0])
	   free(c_outofsync_buffer[0]);
   	c_outofsync_buffer[0] = NULL;
	c_outofsync_buffer[1] = NULL;
	c_outofsync_buffer[2] = NULL;
}
void complexsync_apply(VJFrame *frame, VJFrame *frame2, int width, int height, int val)
{

	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	const unsigned int region = width * val;

	veejay_memcpy( c_outofsync_buffer[0], Y, region );
        veejay_memcpy( c_outofsync_buffer[1], Cb, region );
        veejay_memcpy( c_outofsync_buffer[2], Cr, region );

	veejay_memcpy( Y, Y2, region );
	veejay_memcpy( Cb, Cb2, region );
	veejay_memcpy( Cr, Cr2, region );

        if( (len - region) > 0)
	{
		veejay_memcpy( Y + region, c_outofsync_buffer[0], (len-region) );
		veejay_memcpy( Cb + region, c_outofsync_buffer[1], (len - region) );
		veejay_memcpy( Cr + region, c_outofsync_buffer[2], (len - region) );
	}
}
