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
#include <libvjmem/vjmem.h>
#include <libvje/internal.h>
#include "magicscratcher.h"

static uint8_t *mframe = NULL;
static int m_frame = 0;
static int m_reverse = 0;
static int m_rerun = 0;

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
int magicscratcher_malloc(int w, int h)
{
   mframe =
	(uint8_t *) vj_calloc( RUP8(w * h) * sizeof(uint8_t) * MAX_SCRATCH_FRAMES);
   if(!mframe) return 0;
   vj_frame_clear1( mframe, w * h * MAX_SCRATCH_FRAMES, 0 );
   return 1;
	
}
void magicscratcher_free() {
  if(mframe) free(mframe);
  m_rerun = 0;
}

static void store_mframe(uint8_t * yuv1[3], int w, int h, int n, int no_reverse)
{
    if (!m_reverse) {
		veejay_memcpy(mframe + (w * h * m_frame), yuv1[0], (w * h));
    } else {
		veejay_memcpy(yuv1[0], mframe + (w * h * m_frame), (w * h));
    }
    if (m_reverse)
		m_frame--;
    else
		m_frame++;

    if (m_frame >= n ) {
		if (no_reverse == 0) {
		    m_reverse = 1;
	    	m_frame = n - 1;
			if(m_frame < 0 )
				m_frame = 0;
		} else {
	  	  m_frame = 0;
		}
    }

    if (m_frame == 0)
		m_reverse = 0;
}


void magicscratcher_apply(VJFrame *frame, int mode, int n, int no_reverse, int grayscale)
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	unsigned int x;
	int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    /* param 6 is cool ,8,7,10,13,15, ,9,11,12,14, 

       16 voor default ?, 17 (!),18,19, 20(!), 21, 24,,25,30 */
    int offset = len * m_frame;

    pix_func_Y func_y = get_pix_func_Y((const int) mode);

    if (m_frame == 0) {
  	  veejay_memcpy(mframe + (len * m_frame), Y, len);
	  if( m_rerun > 0 ) {
		  veejay_memcpy( mframe + (len * m_rerun) , Y , len );  
	  }
    }

    for (x = 0; x < len; x++) {
		Y[x] = func_y( mframe[offset + x], Y[x]);
    }

    if (grayscale)
    {
        veejay_memset( Cb, 128, (frame->ssm ? len : frame->uv_len));
        veejay_memset( Cr, 128, (frame->ssm ? len : frame->uv_len));
    }

    m_rerun = m_frame;

    store_mframe(frame->data, width, height, n, no_reverse);
}
