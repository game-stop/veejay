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
#include "cartonize.h"

static inline int cartonize_chroma_step(int v)
{
    if(v <= 0)
        return 0;

    int s = v - 128;

    if(s < 0)
        s = -s;

    return s < 1 ? 1 : s;
}

vj_effect *cartonize_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1; ve->limits[1][0] = 255; ve->defaults[0] = 64;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 0;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255; ve->defaults[2] = 0;

    ve->description = "Cartoon";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Damp Y", "Damp U", "Damp V");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                     8,  148, 12, 50,  900, 3000, 0,    74,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 8,  108, 10, 40, 1100, 3400, 0,    58,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 8,  116, 10, 42, 1100, 3400, 0,    62
    );

    return ve;
}

void cartonize_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int b1 = args[0];
    const int b2 = args[1];
    const int b3 = args[2];
    const int ubase = cartonize_chroma_step(b2);
    const int vbase = cartonize_chroma_step(b3);
    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    #pragma omp parallel num_threads(n_threads)
    {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y[i] = (uint8_t)((Y[i] / b1) * b1);

        if(ubase > 0)
        {
            #pragma omp for schedule(static)
            for(int i = 0; i < uv_len; i++)
            {
                const int p = (int)Cb[i] - 128;
                Cb[i] = (uint8_t)((p / ubase) * ubase + 128);
            }
        }

        if(vbase > 0)
        {
            #pragma omp for schedule(static)
            for(int i = 0; i < uv_len; i++)
            {
                const int p = (int)Cr[i] - 128;
                Cr[i] = (uint8_t)((p / vbase) * vbase + 128);
            }
        }
    }
}
