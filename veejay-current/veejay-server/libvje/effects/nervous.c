/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "nervous.h"

#define N_MAX 100

typedef struct {
    uint8_t *nervous_buf[4];
    int	frames_elapsed;
} nervous_t;

vj_effect *nervous_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = N_MAX;
    ve->defaults[0] = N_MAX;
    ve->description = "Nervous";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Buffer length");
    return ve;
}

void *nervous_malloc(int w, int h )
{
    nervous_t *n = (nervous_t*) vj_calloc(sizeof(nervous_t));
    if(!n) {
        return NULL;
    }

    size_t total_len = (w * h * N_MAX * 4);
	n->nervous_buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * total_len);
	if(!n->nervous_buf[0]) {
        free(n);
		return NULL;
	}

	n->nervous_buf[1] = n->nervous_buf[0] + (w*h*N_MAX);
	n->nervous_buf[2] = n->nervous_buf[1] + (w*h*N_MAX);
	n->nervous_buf[3] = n->nervous_buf[2] + (w*h*N_MAX);
	n->frames_elapsed = 0;

	vj_frame_clear1( n->nervous_buf[0], 0, (w*h) * N_MAX );
	vj_frame_clear1( n->nervous_buf[1], 128, (w*h) * N_MAX );
	vj_frame_clear1( n->nervous_buf[2], 128, (w*h) * N_MAX );
	vj_frame_clear1( n->nervous_buf[3], 0, (w*h) * N_MAX );

	return (void*) n;
}

void	nervous_free(void *ptr)
{
    nervous_t *n = (nervous_t*) ptr;

	free(n->nervous_buf[0]);
    free(n);
}	


void nervous_apply( void *ptr, VJFrame *frame, int *args ) {
    int length = args[0];
    nervous_t *n = (nervous_t*) ptr;

	const int len = frame->len;
	int uv_len = (frame->ssm == 1 ? len : frame->uv_len);
	uint8_t *NY = n->nervous_buf[0] + (len * n->frames_elapsed );
	uint8_t *NCb= n->nervous_buf[1] + (uv_len * n->frames_elapsed );
	uint8_t *NCr= n->nervous_buf[2] + (uv_len * n->frames_elapsed );
	uint8_t *NA = n->nervous_buf[3] + (len * n->frames_elapsed);
	uint8_t *dest[4] = { NY, NCb, NCr, NA };
	int strides[4] = { len, uv_len,uv_len, len };
	// copy original into nervous buf	
	vj_frame_copy( frame->data, dest, strides );

	if(n->frames_elapsed > 0)
	{
		// take a random frame
		unsigned int index = (unsigned int) ((double)n->frames_elapsed * rand() / (RAND_MAX+1.0) );
		// setup pointers
		uint8_t *sY = n->nervous_buf[0] + (len * index);
		uint8_t *sCb = n->nervous_buf[1] + (uv_len * index);
		uint8_t *sCr = n->nervous_buf[2] + (uv_len * index);
		uint8_t *sA = n->nervous_buf[3] + (len * index);
		// copy it to dst
		dest[0] = sY;
		dest[1] = sCb;
		dest[2] = sCr;
		dest[3] = sA;
		vj_frame_copy( dest, frame->data, strides );
	}

	n->frames_elapsed ++;

	if( n->frames_elapsed == length )
		n->frames_elapsed = 0;

}
