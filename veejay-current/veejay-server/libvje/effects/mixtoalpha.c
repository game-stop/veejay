/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwewlburg@gmail.com>
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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include <math.h>
#include "mixtoalpha.h"

vj_effect *mixtoalpha_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->description = "Alpha: New from Mixing source";
    ve->sub_format = 0;
    ve->extra_frame = 1;
	ve->parallel = 1;
	ve->has_user = 0;
    return ve;
}

void mixtoalpha_apply( VJFrame *frame, VJFrame *frame2, int width,int height)
{
	veejay_memcpy(  a, Y, len );
}

