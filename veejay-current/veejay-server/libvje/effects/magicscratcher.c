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

#include "common.h"
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>
#include "magicscratcher.h"

typedef struct {
    uint8_t *mframe;
    int m_frame;
    int m_reverse;
    int m_rerun;
} magicscratcher_t;

vj_effect *magicscratcher_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = VJ_EFFECT_BLEND_COUNT;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = MAX_SCRATCH_FRAMES - 1;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->defaults[0] = 1;
    ve->defaults[1] = 7;
    ve->defaults[2] = 1;
    ve->defaults[3] = 1;
    ve->description = "Magic Overlay Scratcher";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Scratch frames", "PingPong", "Grayscale");

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0, VJ_EFFECT_BLEND_STRINGS);

	vje_build_value_hint_list( ve->hints, ve->limits[1][2], 2,
	                          "Enabled", "Disabled");

	vje_build_value_hint_list( ve->hints, ve->limits[1][3], 3,
	                          "Colorful", "Grayscale");

    return ve;
}

void *magicscratcher_malloc(int w, int h)
{
    magicscratcher_t *m = (magicscratcher_t*) vj_calloc(sizeof(magicscratcher_t));
    if(!m) {
        return NULL;
    }

    m->mframe =	(uint8_t *) vj_calloc( RUP8(w * h) * sizeof(uint8_t) * MAX_SCRATCH_FRAMES);
    if(!m->mframe) {
        free(m);
        return NULL;
    }
    return (void*) m;
}

void magicscratcher_free(void *ptr) {
    magicscratcher_t *m = (magicscratcher_t*) ptr;
    free(m->mframe);
    free(m);
}

static void store_mframe(magicscratcher_t *m, uint8_t * yuv1[3], int w, int h, int n, int no_reverse)
{
    if (!m->m_reverse) {
		veejay_memcpy(m->mframe + (w * h * m->m_frame), yuv1[0], (w * h));
    } else {
		veejay_memcpy(yuv1[0], m->mframe + (w * h * m->m_frame), (w * h));
    }
    if (m->m_reverse)
		m->m_frame--;
    else
		m->m_frame++;

    if (m->m_frame >= n ) {
		if (no_reverse == 0) {
		    m->m_reverse = 1;
	    	m->m_frame = n - 1;
			if(m->m_frame < 0 )
				m->m_frame = 0;
		} else {
	  	  m->m_frame = 0;
		}
    }

    if (m->m_frame == 0)
		m->m_reverse = 0;
}


void magicscratcher_apply(void *ptr, VJFrame *frame, int *args ) {
    int mode = args[0];
    int n = args[1];
    int no_reverse = args[2];
    int grayscale = args[3];

    magicscratcher_t *m = (magicscratcher_t*) ptr;

	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	unsigned int x;
	int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    /* param 6 is cool ,8,7,10,13,15, ,9,11,12,14, 

       16 voor default ?, 17 (!),18,19, 20(!), 21, 24,,25,30 */
    int offset = len * m->m_frame;

    pix_func_Y func_y = get_pix_func_Y((const int) mode);

    if (m->m_frame == 0) {
  	  veejay_memcpy(m->mframe + (len * m->m_frame), Y, len);
	  if( m->m_rerun > 0 ) {
		  veejay_memcpy( m->mframe + (len * m->m_rerun) , Y , len );  
	  }
    }

    for (x = 0; x < len; x++) {
		Y[x] = func_y( m->mframe[offset + x], Y[x]);
    }

    if (grayscale)
    {
        veejay_memset( Cb, 128, (frame->ssm ? len : frame->uv_len));
        veejay_memset( Cr, 128, (frame->ssm ? len : frame->uv_len));
    }

    m->m_rerun = m->m_frame;

    store_mframe(m, frame->data, width, height, n, no_reverse);
}

