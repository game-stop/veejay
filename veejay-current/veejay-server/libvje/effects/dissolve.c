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
#include "dissolve.h"

static inline uint8_t dissolve_blend255(uint8_t a, uint8_t b, int q)
{
    const int iq = 255 - q;

    return (uint8_t)(((int)a * iq + (int)b * q + 127) / 255);
}

vj_effect *dissolve_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150;

    ve->description = "Dissolve Overlay";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 24, 248, 16, 62, 700, 2600, 0, 86
    );
    return ve;
}

void dissolve_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void) ptr;

    const int opacity = args[0];
    const int len = frame->len;
    const int uv_len = frame->ssm ? frame->len : frame->uv_len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    if(opacity == 0)
        return;

    if(opacity == 255)
    {
        veejay_memcpy(Y, Y2, len);
        veejay_memcpy(Cb, Cb2, uv_len);
        veejay_memcpy(Cr, Cr2, uv_len);
        return;
    }

    const int n_threads = vje_advise_num_threads(len);

    #pragma omp parallel num_threads(n_threads)
    {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y[i] = dissolve_blend255(Y[i], Y2[i], opacity);

        #pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++)
        {
            Cb[i] = dissolve_blend255(Cb[i], Cb2[i], opacity);
            Cr[i] = dissolve_blend255(Cr[i], Cr2[i], opacity);
        }
    }
}
