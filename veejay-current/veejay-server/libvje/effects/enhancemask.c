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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "enhancemask.h"

vj_effect *enhancemask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 125;	/* type */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2048;
    ve->description = "Sharpen";

    ve->extra_frame = 0;
    ve->sub_format = -1;
	ve->has_user = 0;
	ve->parallel = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Value" );
    return ve;
}

void enhancemask_apply(void *ptr, VJFrame *frame, int *s )
{

   unsigned int r;
   const unsigned int width = frame->width;
   const int len = frame->len;
   const int len2 = len-width-1;
   uint8_t *Y = frame->data[0];

   /* The sharpen comes from yuvdenoiser, we like grainy video so 512 is allowed.  */ 
      

   register int d,m;
   for(r=0; r < len2; r++) {
	m = ( Y[r] + Y[r+1] + Y[r+width] + Y[r+width+1] + 2) >> 2;
	d = Y[r] - m;
	d *= s[0];
	d /= 100;
	m = m + d;
	Y[r] = m;
	}
   for(r=len2; r < len; r++) {
        m = (Y[r] + Y[r+1] + Y[r-width] + Y[r-width+1] + 2) >> 2;
	d = Y[r]-m;
	d *= s[0];
	d /= 100;
	m = m + d;
	Y[r] = m;
   }

}

