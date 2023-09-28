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
#include <veejay/vj-viewport.h>
#include "perspective.h"

vj_effect *perspective_init(int width , int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 9;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 30; /* x1, y1 */
    ve->defaults[1] = 30;

    ve->defaults[2] = 70; /* x2, y2 */
    ve->defaults[3] = 30;

    ve->defaults[4] = 70; /* x3, y3 */
    ve->defaults[5] = 70;

    ve->defaults[6] = 30; /* x3, y4 */
    ve->defaults[7] = 70;

    ve->defaults[0] = 0; /* reverse */

    int i;
    for( i = 0; i < 8 ; i ++ ) {
        ve->limits[0][i] = -100;
        ve->limits[1][i] = 100;
    }

    ve->limits[0][8] = 0;
    ve->limits[1][8] = 1;
    
    ve->description = "Perspective Tool";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    
    ve->param_description = vje_build_param_list( ve->num_params, 
            "Point 1 (X)", "Point 1 (Y)", "Point 2 (X)", "Point 2 (Y)",
            "Point 3 (X)", "Point 3 (Y)", "Point 4 (X)", "Point 4 (Y)",
            "Reverse"   );
    return ve;
}

typedef struct {
    int perspective_[9];
    void *perspective_vp_;
    uint8_t *perspective_private_[4];
} perspective_t;

void *perspective_malloc(int width, int height)
{
    perspective_t *p = (perspective_t*) vj_calloc( sizeof(perspective_t ));
    if(!p) {
        return NULL;
    }

    p->perspective_private_[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * ((width*height+width)*3));
    if(!p->perspective_private_[0]) {
        free(p);
        return NULL;
    }

    p->perspective_private_[1] = p->perspective_private_[0] + ( width * height + width );
    p->perspective_private_[2] = p->perspective_private_[1] + ( width * height + width );

    return (void*) p;
}

void perspective_free(void *ptr) {
    perspective_t *p = (perspective_t*) ptr;

    free(p->perspective_private_[0] );
    viewport_destroy( p->perspective_vp_ );
    free(p);
}

void perspective_apply( void *ptr, VJFrame *frame, int *args ) {
    
    int x1 = args[0];
    int y1 = args[1];
    int x2 = args[2];
    int y2 = args[3];
    int x3 = args[4];
    int y3 = args[5];
    int x4 = args[6];
    int y4 = args[7];
    int reverse = args[8];

    perspective_t *p = (perspective_t*) ptr;

    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;

    if( x1 != p->perspective_[0] || y1 != p->perspective_[1] || x2 != p->perspective_[2] || y2 != p->perspective_[3] ||
            x3 != p->perspective_[4] || y3 != p->perspective_[5] || x4 != p->perspective_[6] || y4 != p->perspective_[7] || reverse != p->perspective_[8] )
    {
        if( p->perspective_vp_ )
            viewport_destroy( p->perspective_vp_ );
        p->perspective_vp_ = viewport_fx_init_map( width,height,x1,y1, x2,y2, x3,y3, x4,y4, reverse );
        if(!p->perspective_vp_ )
            return;
        p->perspective_[0] = x1;
        p->perspective_[1] = y1;
        p->perspective_[2] = x2;
        p->perspective_[3] = y2;
        p->perspective_[4] = x3;
        p->perspective_[5] = y3;
        p->perspective_[6] = x4;
        p->perspective_[7] = y4;
        p->perspective_[8] = reverse;
    }

    int strides[4] = { len, len, len, 0 };
    vj_frame_copy( frame->data, p->perspective_private_, strides );

    viewport_process_dynamic( p->perspective_vp_, p->perspective_private_, frame->data );
    
}

