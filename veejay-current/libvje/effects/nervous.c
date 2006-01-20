/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <elburg@hio.hen.nl>
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
	Nervous is loosly based on Kentaro's Nervous effect, found
	in EffecTV ( http://effectv.sf.net ).
	
*/
#include <config.h>
#include "nervous.h"
#include <stdlib.h>

#define		N_MAX	25

static uint8_t *nervous_buf[3]; // huge buffer
static int		frames_elapsed;

vj_effect *nervous_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_malloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = N_MAX;
    ve->defaults[0] = N_MAX;
    ve->description = "Nervous";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
    return ve;
}

int	nervous_malloc(int w, int h )
{
	nervous_buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * N_MAX);
	if(!nervous_buf[0]) return 0;
	nervous_buf[1] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * N_MAX);
	if(!nervous_buf[1]) return 0;
	nervous_buf[2] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * N_MAX);
	if(!nervous_buf[2]) return 0;
	frames_elapsed = 0;
	return 1;
}

void	nervous_free(void)
{
	if( nervous_buf[0] ) free(nervous_buf[0]);
	if( nervous_buf[1] ) free(nervous_buf[1]);
	if( nervous_buf[2] ) free(nervous_buf[2]);
	nervous_buf[0] = NULL;
	nervous_buf[1] = NULL;
	 nervous_buf[2] = NULL;
}


void nervous_apply( VJFrame *frame, int width, int height, int delay)
{
    unsigned int i;
    int len = (width * height);
    int uv_len = frame->uv_len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
	uint8_t *NY = nervous_buf[0] + (len * frames_elapsed );
	uint8_t *NCb= nervous_buf[1] + (uv_len * frames_elapsed );
	uint8_t *NCr= nervous_buf[2] + (uv_len * frames_elapsed );

	// copy original into nervous buf
	veejay_memcpy( NY, Y, len );
	veejay_memcpy( NCb, Cb, uv_len );
	veejay_memcpy( NCr, Cr, uv_len );

	if(frames_elapsed > 0)
	{
		// take a random frame
		unsigned int index = (unsigned int) ((double)frames_elapsed *
				rand() / (RAND_MAX+1.0) );
		// setup pointers
		uint8_t *sY = nervous_buf[0] + (len * index);
		uint8_t *sCb = nervous_buf[1] + (uv_len * index);
		uint8_t *sCr = nervous_buf[2] + (uv_len * index);
		// copy it to dst
		veejay_memcpy( Y, sY, len );
		veejay_memcpy( Cb, sCb, uv_len );
		veejay_memcpy( Cr, sCr, uv_len );
	}

	frames_elapsed ++;

	if( frames_elapsed == N_MAX )
		frames_elapsed = 0;

}
