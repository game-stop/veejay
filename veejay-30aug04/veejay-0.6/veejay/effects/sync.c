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

#include "sync.h"
#include <stdlib.h>
#include <config.h>
#include "vj-common.h"

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);


static uint8_t *outofsync_buffer[3];

vj_effect *sync_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = height;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = (25 * 10);
    ve->defaults[0] = height/4;
    ve->defaults[1] = 1;
    ve->defaults[2] = 5;
    ve->description = "Out of Sync (Horizontal)";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 1;

    return ve;
}
int 	sync_malloc(int width, int height)
{
    outofsync_buffer[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height );
	if(!outofsync_buffer[0]) return 0;
    outofsync_buffer[1] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height );
	if(!outofsync_buffer[1]) return 0;
    outofsync_buffer[2] = (uint8_t*)vj_malloc(sizeof(uint8_t) * width * height );
   	if(!outofsync_buffer[2]) return 0;
	return 1;
}

/* bug in uv sampling */

void sync_free() {
 if(outofsync_buffer[0]) free(outofsync_buffer[0]);
 if(outofsync_buffer[1]) free(outofsync_buffer[1]);
 if(outofsync_buffer[2]) free(outofsync_buffer[2]);
}

void sync_apply(uint8_t *yuv[3], int width, int height, int val)
{
    unsigned int i,j=0;
    const unsigned int len = (width*height);
    unsigned r_start = 0;
    const unsigned int uv_len = len>>2 ;//>> 1;
    unsigned int uv_start = 0;
    const unsigned int uv_width = width>>1;// >> 1;
    /* copy region to buffer */
    veejay_memcpy( outofsyncbuffer[0], yuv[0], (val*  width)  );
    veejay_memcpy( outofsyncbuffer[1], yuv[1], (val * uv_width)  );
    veejay_memcpy( outofsyncbuffer[2], yuv[2], (val * uv_width)  );
	/* 
    for(i=0; i < (val*width); i++) {
	outofsync_buffer[0][i] = yuv[0][i];
	outofsync_buffer[1][i] = yuv[1][i];
	outofsync_buffer[2][i] = yuv[2][i];
	}
	*/
    r_start = val * width;
    uv_start = val * uv_width;


    /* copy other region over current 
    for(i=r_start; i < len; i++) {
	yuv[0][j++] = yuv[0][i];
	yuv[1][j] = yuv[1][i];
	yuv[2][j] = yuv[2][i];
    }
	*/
    veejay_memcpy( yuv[0], yuv[0]+r_start, len);
    veejay_memcpy( yuv[1], yuv[1]+uv_start, uv_len);
    veejay_memcpy( yuv[2], yuv[2]+uv_start, uv_len);    
	
    veejay_memcpy( yuv[0]+r_start, outofsync_buffer[0], (val*width));
    veejay_memcpy( yuv[1]+uv_start, outofsync_buffer[1], (val*uv_width));
    veejay_memcpy( yuv[2]+uv_start, outofsync_buffer[2], (val*uv_width));
    /* copy from buffer over region  
    for(i=0; i < (val * width); i++) {
	yuv[0][j++] = outofsync_buffer[0][i]; 
	yuv[1][j] = outofsync_buffer[1][i];
	yuv[2][j] = outofsync_buffer[2][i];
    }
	*/

}
void sync_free(){}
