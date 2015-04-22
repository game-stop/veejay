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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "cartonize.h"

vj_effect *cartonize_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 255;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 255;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 255;

    ve->defaults[0] = 64;
	ve->defaults[1] = 0;
	ve->defaults[2] = 0;

    ve->description = "Cartoon";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Damp Y", "Damp U", "Damp V" );
    return ve;
}

void cartonize_apply( VJFrame *frame, int width, int height, int b1, int b2, int b3)
{
    unsigned int i;
    int len = (width * height);
    int uv_len = frame->uv_len;
	uint8_t tmp;	
	int		p;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

	const int	base = (const int) b1;
	int	ubase= (const int) b2 - 128;
	int 	vbase= (const int) b3 - 128;
	// ubase/vbase cannot be 0
	if(ubase==0) ubase=1;
	if(vbase==0) vbase=1;
	for( i = 0 ; i < len ; i ++ )	
	{
		tmp = Y[i];
		Y[i] = (tmp / base) * base; // loose fractional part
	}

	if(b2 > 0)
	for( i = 0; i < uv_len; i ++ )
	{
		p = Cb[i] - 128;
		Cb[i] = (p / ubase) * ubase + 128;
	}

	if(b3> 0 )
	for( i = 0; i < uv_len; i ++ )
	{
		p = Cr[i] - 128;
		Cr[i] = (p / vbase) * vbase + 128;
	}

}
