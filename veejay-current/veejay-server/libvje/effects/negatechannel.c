/* 
 * Linux VeeJay
 *
 * Copyright(C)2008 Niels Elburg <nwelburg@gmail.com>
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
#include "negatechannel.h"
#include "common.h"

#undef HAVE_ASM_MMX

vj_effect *negatechannel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 0xff;
    ve->defaults[0] = 0;
    ve->defaults[1] = 0xff;
    ve->description = "Negate a channel";
    ve->parallel = 1;
	ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Y=0, Cb=1, Cr=2", "Value" );
    return ve;
}

void negatechannel_apply( VJFrame *frame, int width, int height, int chan, int val)
{
    int i;
    int len = (width * height);
    int uv_len = frame->uv_len;

    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    switch( chan ) {
		case 0:
		 for (i = 0; i < len; i++) {
			Y[i] = val - Y[i];
		    }
		 break;
		case 1:
			for (i = 0; i < uv_len; i++) {
			Cb[i] = val - Cb[i];
		    }
		 break;
		case 2:
		for (i = 0; i < uv_len; i++) {
			Cr[i] = val - Cr[i];
		}
		 break;
   }
}
