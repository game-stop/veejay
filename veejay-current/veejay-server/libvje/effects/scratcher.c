/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
#include "opacity.h"
#include "scratcher.h"

typedef struct {
    uint8_t *frame[4];
    int nframe;
    int nreverse;
    int last_reverse;
    int last_n;
} scratcher_t;


vj_effect *scratcher_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = (MAX_SCRATCH_FRAMES-1);
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[0] = 150;
    ve->defaults[1] = 8;
    ve->defaults[2] = 1;
    ve->description = "Overlay Scratcher";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Scratch buffer", "PingPong");
    return ve;

}

void scratcher_free(void *ptr) {
    scratcher_t *s = (scratcher_t*) ptr;
    free(s->frame[0]);
    free(s);
}

void *scratcher_malloc(int w, int h)
{
    scratcher_t *s = (scratcher_t*) vj_calloc(sizeof(scratcher_t));
    if(!s) {
        return NULL;
    }

    s->last_reverse = 1;
    s->last_n = 8;

	/* need memory for bounce mode ... */
    s->frame[0] = (uint8_t *) vj_malloc( (w * h * 3) * sizeof(uint8_t) * MAX_SCRATCH_FRAMES);
	if(!s->frame[0]) {
        free(s);
        return NULL;
    }

    veejay_memset( s->frame[0], pixel_Y_lo_, w * h * MAX_SCRATCH_FRAMES );

    s->frame[1] =
	    s->frame[0] + ( (w * h) * MAX_SCRATCH_FRAMES );
    s->frame[2] =
	    s->frame[1] + ( (w * h)  * MAX_SCRATCH_FRAMES );

    veejay_memset( s->frame[1], 128, (w * h * MAX_SCRATCH_FRAMES) );
    veejay_memset( s->frame[2], 128, (w * h * MAX_SCRATCH_FRAMES) );
    
    return (void*) s;
}


static void store_frame(scratcher_t *s, VJFrame *src, int n, int no_reverse )
{
	const int len = src->len;
	const int uv_len = src->uv_len;
	int strides[4] = { len, uv_len, uv_len , 0 };

	uint8_t *dest[4] = {
		s->frame[0] + (len*s->nframe),
		s->frame[1] + (uv_len*s->nframe),
		s->frame[2] + (uv_len*s->nframe),
       	NULL
	};

	if (!s->nreverse) {
		vj_frame_copy( src->data, dest, strides ); 
    }
	else {
		vj_frame_copy( dest, src->data, strides );
    }

	if (s->nreverse)
		s->nframe--;
	else
		s->nframe++;

	if (s->nframe >= n) {
		if (s->nreverse == 0) {
		    s->nreverse = 1;
		    s->nframe = n - 1;
			if(s->nframe < 0)
				s->nframe = 0;
		} else {
		    s->nframe = 0;
		}
    }

   	if (s->nframe == 0)
		s->nreverse = 0;

}


void scratcher_apply(void *ptr, VJFrame *src, int *args)
{
    const int len = src->len;
    const int uv_len = src->uv_len;

    int opacity = args[0];
    int n = args[1];
    int no_reverse = args[2];
    
    scratcher_t *s = (scratcher_t*) ptr;
    const int offset = len * s->nframe;
    const int uv_offset = uv_len * s->nframe;


	VJFrame tmp;
	veejay_memcpy( &tmp, src, sizeof(VJFrame) );
	
	tmp.data[0] = s->frame[0] + offset;
	tmp.data[1] = s->frame[1] + uv_offset;
	tmp.data[2] = s->frame[2] + uv_offset;

	if( no_reverse != s->last_reverse || n != s->last_n )
	{
		s->last_reverse = no_reverse;
		s->nframe = n;
		s->last_n = n;
	}		

	if( s->nframe == 0 ) {
		tmp.data[0] = src->data[0];
		tmp.data[1] = src->data[1];
		tmp.data[2] = src->data[2];
	}

	opacity_apply( NULL, src, &tmp, args );
	
	store_frame(s, src, n, no_reverse);
}
