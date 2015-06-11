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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "magicscratcher.h"
#include "common.h"

#define        RUP8(num)(((num)+8)&~8)
static uint8_t *mframe = NULL;
static int m_frame = 0;
static int m_reverse = 0;
static int m_rerun = 0;

vj_effect *magicscratcher_init(int w, int h)
{

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 9;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = MAX_SCRATCH_FRAMES - 1;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[0] = 1;
    ve->defaults[1] = 7;
    ve->defaults[2] = 1;
    ve->description = "Magic Overlay Scratcher";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Scratch frames", "PingPong");
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

void store_mframe(uint8_t * yuv1[3], int w, int h, int n, int no_reverse)
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
	} else {
	    m_frame = 0;
	}
    }

    if (m_frame == 0)
	m_reverse = 0;

}


void magicscratcher_apply(VJFrame *frame,
			  int width, int height, int mode, int n,
			  int no_reverse)
{

    unsigned int x, len = width * height;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    /* param 6 is cool ,8,7,10,13,15, ,9,11,12,14, 

       16 voor default ?, 17 (!),18,19, 20(!), 21, 24,,25,30 */
    int offset = len * m_frame;

    pix_func_Y func_y;

    switch (mode) {
	//case 0: mode = 5; break;
    case 1:
	mode = 22;
	break;
    case 2:
	mode = 25;
	break;
	//case 3: mode = 30; break;
    case 3:
	mode = 24;
	break;
	//case 5: mode = 21; break;
	//case 6: mode = 20; break;
	//case 7: mode = 19; break;
	//case 8: mode = 18; break;
    case 4:
	mode = 17;
	break;
    case 5:
	mode = 16;
	break;
	//case 11: mode = 14; break;
	//case 12: mode = 12; break;
	//case 13: mode = 11; break;
    case 6:
	mode = 9;
	break;
    case 7:
	mode = 8;
	break;
    case 8:
	mode = 7;
	break;
    case 9:
	mode = 6;
	break;
    }

    func_y = get_pix_func_Y((const int) mode);

    if (m_frame == 0) {
  	  veejay_memcpy(mframe + (len * m_frame), Y, len);
	  if( m_rerun > 0 ) {
		  veejay_memcpy( mframe + (len * m_rerun) , Y , len );  
	  }
    }

    for (x = 0; x < len; x++) {
		Y[x] = func_y( mframe[offset + x], Y[x]);
    }
    
    veejay_memset( Cb, 128, frame->uv_len);
    veejay_memset( Cr, 128, frame->uv_len);

    m_rerun = m_frame;

    store_mframe(frame->data, width, height, n, no_reverse);

}
