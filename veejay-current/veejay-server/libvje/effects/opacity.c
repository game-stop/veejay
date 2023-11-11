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
#include "opacity.h"

vj_effect *opacity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150;
    ve->description = "Normal Overlay";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->parallel = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Opacity"); 
    return ve;
}

static inline void blend_plane( uint8_t *dst, uint8_t *A, uint8_t *B, size_t size, int opacity )
{
    const uint8_t op1 = (opacity > 255) ? 255 : opacity;
    const uint8_t op0 = 255 - op1;
#pragma omp simd
    for( int i = 0; i < size; i ++ )
        dst[i] = (op0 * A[i] + op1 * B[i] ) >> 8;
}

void opacity_apply( void *ptr,  VJFrame *frame, VJFrame *frame2, int *args )
{
    blend_plane( frame->data[0], frame->data[0], frame2->data[0], frame->len, args[0] );
    blend_plane( frame->data[1], frame->data[1], frame2->data[1], frame->ssm ? frame->len : frame->uv_len, args[0] );
    blend_plane( frame->data[2], frame->data[2], frame2->data[2], frame->ssm ? frame->len : frame->uv_len, args[0] );
}

void opacity_blend_apply( uint8_t *src1[3], uint8_t *src2[3], int len, int uv_len, int opacity )
{
    blend_plane( src1[0], src1[0], src2[0], len, opacity );
    blend_plane( src1[1], src1[1], src2[1], uv_len, opacity );
    blend_plane( src1[2], src1[2], src2[2], uv_len, opacity );

}
