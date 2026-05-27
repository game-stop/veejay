/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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

/*
 * This effect overlays 2 images.
 * It allows the user to set transparency per channel.
 * Result will vary over different color spaces.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "tripplicity.h"

static inline int tripplicity_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t tripplicity_mix_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

vj_effect *tripplicity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1] = 150;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[2] = 150;

    ve->description = "Normal Overlay (per Channel)";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity Y",
        "Opacity Cb",
        "Opacity Cr"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS, 0, 255, 8, 30, 1200, 3000, 0, 45, /* Opacity Y */
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS, 0, 255, 8, 30, 1200, 3000, 0, 45, /* Opacity Cb */
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS, 0, 255, 8, 30, 1200, 3000, 0, 45  /* Opacity Cr */
    );

    (void) w;
    (void) h;

    return ve;
}

void tripplicity_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void) ptr;

    if(!frame || !frame2 || !args ||
       !frame->data[0] || !frame->data[1] || !frame->data[2] ||
       !frame2->data[0] || !frame2->data[1] || !frame2->data[2])
        return;

    const int len = frame->len;
    if(len <= 0)
        return;

    const int uv_len = frame->ssm ? len : frame->uv_len;
    if(uv_len <= 0)
        return;

    const int qY  = (tripplicity_clampi(args[0], 0, 255) * 256 + 127) / 255;
    const int qCb = (tripplicity_clampi(args[1], 0, 255) * 256 + 127) / 255;
    const int qCr = (tripplicity_clampi(args[2], 0, 255) * 256 + 127) / 255;

    uint8_t *restrict Y1  = frame->data[0];
    uint8_t *restrict Cb1 = frame->data[1];
    uint8_t *restrict Cr1 = frame->data[2];

    const uint8_t *restrict Y2  = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    int n_threads = vje_advise_num_threads(len);
    if(n_threads < 1)
        n_threads = 1;

#pragma omp parallel num_threads(n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y1[i] = tripplicity_mix_u8(Y1[i], Y2[i], qY);

#pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            Cb1[i] = tripplicity_mix_u8(Cb1[i], Cb2[i], qCb);
            Cr1[i] = tripplicity_mix_u8(Cr1[i], Cr2[i], qCr);
        }
    }
}