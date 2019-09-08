/* 
 * Linux VeeJay
 *
 * Copyright(C)2006 Niels Elburg <nwelburg@gmail.com>
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
#include "colflash.h"

// very simple color flashing fx

vj_effect *colflash_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 50; //tempo in frames
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->limits[0][4] = 1;
    ve->limits[1][4] = 10;
    ve->defaults[0] = 5;
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->defaults[4] = 3; //delay
    ve->description = "Color Flash";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->rgb_conv = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Frametime" , "Red", "Green", "Blue", "Delay" );
    return ve;
}

typedef struct {
    int color_flash_;
    int color_delay_;
    int delay_;
} colflash_t;

void *colflash_malloc(int w, int h )
{
    colflash_t *c = (colflash_t*) vj_calloc(sizeof(colflash_t));
    if(!c) {
        return NULL;
    }
    return (void*) c;
}

void colflash_free(void *ptr) {
    colflash_t *c = (colflash_t*) ptr;
    free(c);
}

void colflash_apply(void *ptr, VJFrame *frame, int *args) {
    int f = args[0];
    int r = args[1];
    int g = args[2];
    int b = args[3];
    int d = args[4];

    colflash_t *c = (colflash_t*) ptr;

	const int len = frame->len;
	const int uv_len = (frame->ssm ? len : frame->uv_len);

	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

	uint8_t y=0,u=0,v=0;

	_rgb2yuv( r,g,b,y,u,v );

	if( d != c->delay_ )
	{
		c->delay_ = d;
		c->color_delay_ = d;
	}

	if( c->color_delay_ )
	{
		veejay_memset(  Y, y, len );
		veejay_memset( Cb, u, uv_len );
		veejay_memset( Cr, v, uv_len );
		c->color_delay_ -- ;
	}
	else
	{
		c->color_flash_ ++ ;
		if( c->color_flash_ >= f )
		{
			c->color_delay_ = c->delay_;
			c->color_flash_ = 0;
		}
	}

}

