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

#include "complexsync.h"
#include <stdlib.h>
#include "vj-common.h"
extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);

static uint8_t *c_outofsync_buffer[3];

vj_effect *complexsync_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
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
    ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->has_internal_data = 1;

    return ve;
}

int complexsync_malloc(int width, int height)
{
   c_outofsync_buffer[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height );
   memset( c_outofsync_buffer[0], 16, (width*height));
   if(!c_outofsync_buffer[0]) return 0;
    c_outofsync_buffer[1] = (uint8_t*)vj_malloc(sizeof(uint8_t) * (width * height)/4 );
   memset( c_outofsync_buffer[1], 128, (width*height)/4);
   if(!c_outofsync_buffer[1]) return 0;
    c_outofsync_buffer[2] = (uint8_t*)vj_malloc(sizeof(uint8_t) * (width * height)/4 );
    memset( c_outofsync_buffer[2], 128, (width*height)/4);
   if(!c_outofsync_buffer[2]) return 0;
   return 1;

}

void complexsync_free() {
   if(c_outofsync_buffer[0]) free(c_outofsync_buffer[0]);
   if(c_outofsync_buffer[1]) free(c_outofsync_buffer[1]);
   if(c_outofsync_buffer[2]) free(c_outofsync_buffer[2]);
}
void complexsync_apply(uint8_t *yuv[3],uint8_t *yuv2[3], int width, int height, int val)
{
	const unsigned int len = (width * height);
	const unsigned int uv_len = len / 4;
	const unsigned int region = width * val;
	const unsigned int uv_region = (width/2) * (val/2);

	veejay_memcpy( c_outofsync_buffer[0], yuv[0], region );
        veejay_memcpy( c_outofsync_buffer[1], yuv[1], uv_region );
        veejay_memcpy( c_outofsync_buffer[2], yuv[2], uv_region );

	veejay_memcpy( yuv[0], yuv2[0], region );
	veejay_memcpy( yuv[1], yuv2[1], uv_region );
	veejay_memcpy( yuv[2], yuv2[2], uv_region );

        if( (len - region) > 0)
	{
		veejay_memcpy( yuv[0] + region, c_outofsync_buffer[0], (len - region) );
		veejay_memcpy( yuv[1] + uv_region, c_outofsync_buffer[1], (uv_len - uv_region) );
		veejay_memcpy( yuv[2] + uv_region, c_outofsync_buffer[2], (uv_len - uv_region) );
	}
}
