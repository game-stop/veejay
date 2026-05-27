/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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
#include "stretch.h"

static inline int stretch_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t stretch_u8(int v)
{
    return (uint8_t)stretch_clampi(v, 0, 255);
}

vj_effect *stretch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 255;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1] = 0;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1000;
    ve->defaults[2] = 40;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1000;
    ve->defaults[3] = 0;

    ve->description = "Chroma Stretch";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Upper bound",
        "Lower bound",
        "Gain factor",
        "Saturation Amplifier"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DETAIL,       VJ_BEAT_F_CONTINUOUS, 96,  255, 8,  30, 1200, 3000, 0, 45, /* Upper bound */
        VJ_BEAT_DETAIL,       VJ_BEAT_F_CONTINUOUS, 0,   160, 8,  30, 1200, 3000, 0, 45, /* Lower bound */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS, 0,   420, 8,  30, 1200, 3000, 0, 50, /* Gain factor */
        VJ_BEAT_INTENSITY,    VJ_BEAT_F_CONTINUOUS, 0,   700, 10, 38, 1000, 2600, 0, 60  /* Saturation Amplifier */
    );

    (void) w;
    (void) h;

    return ve;
}

void stretch_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    if(!frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int len = frame->len;
    if(len <= 0)
        return;

    int upper = stretch_clampi(args[0], 0, 255);
    int lower = stretch_clampi(args[1], 0, 255);
    int gain = stretch_clampi(args[2], 0, 1000);
    int gain_saturation = stretch_clampi(args[3], 0, 1000);

    if(lower > upper) {
        const int t = lower;
        lower = upper;
        upper = t;
    }

    if(lower == upper)
        return;

    int fixed_gain = gain_saturation > 0
        ? (gain * gain_saturation * 256) / 10000
        : (gain * 256) / 100;

    fixed_gain = stretch_clampi(fixed_gain, 0, 32768);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int n_threads = vje_advise_num_threads(len);
    if(n_threads < 1)
        n_threads = 1;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int y = Y[i];

        if(y > lower && y < upper) {
            const int cb = (int)Cb[i] - 128;
            const int cr = (int)Cr[i] - 128;

            const int out_cb = 128 + cb + ((cb * fixed_gain) >> 8);
            const int out_cr = 128 + cr - ((cr * fixed_gain) >> 8);

            Cb[i] = stretch_u8(out_cb);
            Cr[i] = stretch_u8(out_cr);
        }
    }
}