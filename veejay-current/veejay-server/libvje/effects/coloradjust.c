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
#include "coloradjust.h"

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *coloradjust_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 360;  ve->defaults[0] = 50;
    ve->limits[0][1] = 0; ve->limits[1][1] = 256;  ve->defaults[1] = 50;
    ve->limits[0][2] = 0; ve->limits[1][2] = 1024; ve->defaults[2] = 256;

    ve->param_description = vje_build_param_list(ve->num_params, "Degrees", "Intensity", "Exposure");
    ve->description = "Exposure, Hue and Saturation";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 0;

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 48,  255, 16, 62,  700, 2800, 0,    86,
        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 64,  255, 12, 46, 1000, 3400, 0,    58,
        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 64,  255, 12, 46, 1000, 3400, 0,    58
    );

    return ve;
}

void coloradjust_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;
    const int val = args[0];
    const int intensity = args[1];
    const int exposureValue = args[2];
    const int len = frame->len;
    const int uv_len = frame->uv_len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const float hue = ((float)val / 180.0f) * (float)M_PI;
    const float sat = (float)intensity * 0.01f;
    const int s = (int)rintf(a_sin(hue) * (1 << 16) * sat);
    const int c = (int)rintf(a_cos(hue) * (1 << 16) * sat);
    const int do_exp = exposureValue != 256;
    const int n_threads = vje_advise_num_threads(len);
    const float powValue = exposureValue > 0 ? ((float)exposureValue / 256.0f) : 1.0f;

#pragma omp parallel num_threads(n_threads)
    {
        if(do_exp) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                int y = (int)((float)Y[i] * powValue);

                Y[i] = (uint8_t)clampi(y, 0, 255);
            }
        }

#pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            const int u = (int)Cb[i] - 128;
            const int v = (int)Cr[i] - 128;
            const int new_u = (c * u - s * v + (1 << 15) + (128 << 16)) >> 16;
            const int new_v = (s * u + c * v + (1 << 15) + (128 << 16)) >> 16;

            Cb[i] = (uint8_t)clampi(new_u, 0, 255);
            Cr[i] = (uint8_t)clampi(new_v, 0, 255);
        }
    }
}
