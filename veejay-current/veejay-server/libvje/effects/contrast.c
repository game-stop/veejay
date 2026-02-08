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
#include "contrast.h"

vj_effect *contrast_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 2;	/* type */
    ve->defaults[1] = 125;
    ve->defaults[2] = 200;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->parallel = 1;
	ve->description = "Contrast";
	ve->has_user = 0;
	ve->parallel = 1;
    ve->extra_frame = 0;
    ve->sub_format = -1;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Luma", "Chroma" );

	ve->hints = vje_init_value_hint_list( ve->num_params );
	
	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
			"Luma Only", "Chroma Only" ,"All Channels" );

    return ve;
}


static void contrast_y_apply(VJFrame *frame, int *s)
{
    const int len = frame->len;
    uint8_t *Y = frame->data[0];

    const int scale_fp = (s[1] << 8) / 100;

#pragma omp simd
    for (int r = 0; r < len; r++) {
        int m = Y[r] - 128;
        m = (m * scale_fp) >> 8;
        m += 128;

		int tmp = m & ~(m >> 31);
		Y[r] = 255 + ((tmp - 255) & ((tmp - 255) >> 31));
    }
}

static void contrast_cb_apply(VJFrame *frame, int *s)
{
    const int uv_len = (frame->ssm ? frame->len : frame->uv_len);
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    const int scale_fp = (s[2] << 8) / 100;

#pragma omp simd
    for (int r = 0; r < uv_len; r++) {
        int cb = (Cb[r] - 128) * scale_fp >> 8;
        int cr = (Cr[r] - 128) * scale_fp >> 8;
        cb += 128;
        cr += 128;

		int tmp_cb = cb & ~(cb >> 31);
		Cb[r] = 255 + ((tmp_cb - 255) & ((tmp_cb - 255) >> 31));

		int tmp_cr = cr & ~(cr >> 31);
		Cr[r] = 255 + ((tmp_cr - 255) & ((tmp_cr - 255) >> 31));


    }
}

void contrast_apply(void *ptr, VJFrame *frame, int *s)
{
    switch (s[0])
    {
        case 0:
            contrast_y_apply(frame, s);
            break;
        case 1:
            contrast_cb_apply(frame, s);
            break;
        case 2:
            contrast_y_apply(frame, s);
            contrast_cb_apply(frame, s);
            break;
    }
}
