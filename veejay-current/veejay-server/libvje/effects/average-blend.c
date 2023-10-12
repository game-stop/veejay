/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
#include "average-blend.h"

vj_effect *average_blend_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 7;
    ve->defaults[0] = 1;
    ve->description = "Average Mixer";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->parallel = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Recursions"); 
    return ve;
}

static inline void ac_average( uint8_t *dst, const uint8_t *src1, const uint8_t *src2, const int len )
{
    unsigned int i;
#pragma omp simd
    for( i = 0; i < len; i ++ ) {
        dst[i] = (src1[i] + src2[i]) >> 1;
    }
}

static void average_blend_apply1( VJFrame *frame, VJFrame *frame2, int average_blend)
{
    unsigned int i;
    for( i = 0; i < average_blend; i ++ ) {
        ac_average( frame->data[0], frame->data[0], frame2->data[0], frame->len );
        ac_average( frame->data[1], frame->data[1], frame2->data[1], frame->uv_len );
        ac_average( frame->data[2], frame->data[2], frame2->data[2], frame->uv_len );
    }
}

void average_blend_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    average_blend_apply1( frame,frame2,args[0] );
}

void average_blend_applyN( void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    int average_blend = args[0];

    average_blend_apply1( frame,frame2,average_blend );
    
}

