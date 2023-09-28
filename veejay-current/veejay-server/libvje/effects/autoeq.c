/* 
 * Linux VeeJay
 *
 * Copyright(C)2007 Niels Elburg <nwelburg@gmail.com>
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
#include "autoeq.h"

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

    ve->defaults[0] = 0; // show histogram
    ve->defaults[1] = 200; // intensity
    ve->defaults[2] = 132; // strength

    ve->description = "Automatic Histogram Equalizer";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Mode","Intensity","Strength");
    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0],0,
		    "Show Histogram", "Equalize Frame" );
    return ve;
}

typedef struct {
    void	*histogram_;
    uint8_t *tmp;
} autoeq_t;

void *autoeq_malloc(int w, int h)
{
    autoeq_t *a = (autoeq_t*) vj_calloc(sizeof(autoeq_t));
    if(!a) {
        return NULL;
    }
    a->tmp = (uint8_t*) vj_calloc(sizeof(uint8_t) * (w*h));
    if(!a->tmp) {
        free(a);
        return NULL;
    }

	a->histogram_ = veejay_histogram_new();
    if(!a->histogram_) {
        free(a->tmp);
        free(a);
        return NULL;
    }
	return (void*) a;
}

void	autoeq_free(void *ptr)
{
    autoeq_t *a = (autoeq_t*) ptr;
	veejay_histogram_del(a->histogram_);
    free(a->tmp);
    free(a);
}


void autoeq_apply( void *ptr, VJFrame *frame,int *args) {
    int val = args[0];
    int intensity = args[1];
    int strength = args[2];
    autoeq_t *a = (autoeq_t*) ptr;

	const int len = frame->len;
	const int uv_len = frame->uv_len;
	if( val == 0 )
	{
		VJFrame tmp;
		veejay_memcpy( &tmp, frame, sizeof(VJFrame));
		tmp.data[0] = a->tmp;
		vj_frame_copy1( frame->data[0], tmp.data[0], len );

		veejay_histogram_draw( a->histogram_,&tmp, frame, intensity, strength );

		vj_frame_clear1( frame->data[1], 128, uv_len );
		vj_frame_clear1( frame->data[2], 128, uv_len );
	}
	else
	{
		veejay_histogram_analyze( a->histogram_, frame, 0 );
		veejay_histogram_equalize( a->histogram_, frame, intensity, strength );
	}
}
