/* 
 * Linux VeeJay
 *
 * Copyright(C)2007 Niels Elburg <elburg@hio.hen.nl>
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
#include "autoeq.h"
#include <stdlib.h>
#include "common.h"
vj_effect *autoeq_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->defaults[0] = 0; // y only, v only, u only, all
    ve->defaults[1] = 200; // intensity
    ve->defaults[2] = 132; // strength

    ve->description = "Automatic Histogram Equalizer";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Channel (Y,U,V,All)","Intensity","Strength");
    return ve;
}

static void	*histogram_ = NULL;

int	autoeq_malloc(int w, int h)
{
	if( histogram_ )
		veejay_histogram_del(histogram_);
	histogram_ = veejay_histogram_new();
	return 1;
}

void	autoeq_free()
{
	if( histogram_ )
		veejay_histogram_del(histogram_);
	histogram_ = NULL;
}


void autoeq_apply( VJFrame *frame, int width, int height, int val, int intensity, int strength)
{
	if( val == 0 )
	{
		VJFrame tmp;
		veejay_memcpy( &tmp, frame, sizeof(VJFrame));
		tmp.data[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * frame->len );
		vj_frame_copy1( frame->data[0], tmp.data[0], frame->len );

		veejay_histogram_draw( histogram_,&tmp, frame, intensity, strength );

		vj_frame_clear1( frame->data[1], 128, frame->uv_len );
		vj_frame_clear1( frame->data[2], 128, frame->uv_len );

		free(tmp.data[0]);
	}
	else
	{
		veejay_histogram_analyze( histogram_, frame, 0 );
		veejay_histogram_equalize( histogram_, frame, intensity, strength );
	}
}
