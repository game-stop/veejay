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
#include "contrast.h"

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t contrast_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

vj_effect *contrast_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 2;   ve->defaults[0] = 2;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 125;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255; ve->defaults[2] = 200;

    ve->description = "Contrast";
    ve->has_user = 0;
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Luma", "Chroma");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0, "Luma Only", "Chroma Only", "All Channels");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_CONTRAST,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 64,                 245,                14, 58,  800, 2800, 0,    86,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 72,                 245,                12, 50,  900, 3200, 0,    72
    );

    return ve;
}

void contrast_apply(void *ptr, VJFrame *frame, int *s)
{
    (void) ptr;

    if(!frame || !s)
        return;

    const int mode = clampi(s[0], 0, 2);
    const int luma = clampi(s[1], 0, 255);
    const int chroma = clampi(s[2], 0, 255);
    const int len = frame->len;
    const int uv_len = frame->uv_len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int scale_y = (luma << 8) / 100;
    const int scale_uv = (chroma << 8) / 100;
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel num_threads(n_threads)
    {
        if(mode == 0 || mode == 2)
        {
#pragma omp for schedule(static)
            for(int r = 0; r < len; r++)
            {
                int y = (((int)Y[r] - 128) * scale_y) >> 8;
                Y[r] = contrast_u8(y + 128);
            }
        }

        if((mode == 1 || mode == 2) && Cb && Cr && uv_len > 0)
        {
#pragma omp for schedule(static)
            for(int r = 0; r < uv_len; r++)
            {
                int cb = (((int)Cb[r] - 128) * scale_uv) >> 8;
                int cr = (((int)Cr[r] - 128) * scale_uv) >> 8;
                Cb[r] = contrast_u8(cb + 128);
                Cr[r] = contrast_u8(cr + 128);
            }
        }
    }
}
