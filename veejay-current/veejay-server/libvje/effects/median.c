/* 
 * Linux VeeJay
 *
 * Copyright(C)2010 Niels Elburg <nwelburg@gmail.com>
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

// Uses: ctmf
/*
 * ctmf.c - Constant-time median filtering
 * Copyright (C) 2006  Simon Perreault
 *
 * Reference: S. Perreault and P. Hébert, "Median Filtering in Constant Time",
 * IEEE Transactions on Image Processing, September 2007.
 *
 * This program has been obtained from http://nomis80.org/ctmf.html. No patent
 * covers this program, although it is subject to the following license:
 */

#include <unistd.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include <ctmf/ctmf.h>
#include "median.h"

static long l2_cache_size_;

vj_effect *medianfilter_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 127;
    ve->defaults[0] = 3;
    ve->description = "Constant-time median filtering";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Value" );
    
    l2_cache_size_ = sysconf( _SC_LEVEL2_CACHE_SIZE );
    
    return ve;
}

void medianfilter_apply( VJFrame *frame, int val)
{

    if( val == 0 )
        return;

    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;
    const int uv_len = frame->uv_len;
    const int u_hei = frame->uv_height;
    const int u_wid = frame->uv_width;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    uint8_t *buffer[3];
    size_t bufsize = RUP8( len + uv_len + uv_len ) * sizeof(uint8_t);

    buffer[0] = (uint8_t*) vj_malloc( bufsize );
    if(!buffer[0]) {
        return; // error
    }
    buffer[1] = buffer[0] + RUP8( len );
    buffer[2] = buffer[1] + RUP8( uv_len );

    ctmf( Y, buffer[0], width,height, width, width, val,1,l2_cache_size_);
    ctmf( Cb,buffer[1], u_wid, u_hei, u_wid, u_wid, val,1,l2_cache_size_);
    ctmf( Cr,buffer[2], u_wid, u_hei, u_wid, u_wid, val,1,l2_cache_size_);

    veejay_memcpy( Y, buffer[0], len);
    veejay_memcpy( Cb,buffer[1], uv_len);
    veejay_memcpy( Cr,buffer[2], uv_len);

    free(buffer[0]);
}
