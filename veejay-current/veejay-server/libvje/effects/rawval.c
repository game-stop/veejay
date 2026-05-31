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
#include "rawval.h"

vj_effect *rawval_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 232;
    ve->defaults[1] = 16;
    ve->defaults[2] = 16;
    ve->defaults[3] = 16;

    for(int i = 0; i < ve->num_params; i++) {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 255;
    }

    ve->sub_format = -1;
    ve->description = "Raw Chroma Pixel Replacement";
    ve->has_user = 0;
    ve->extra_frame = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Old Cb",
        "Old Cr",
        "New Cb",
        "New Cr"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Old Cb */
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Old Cr */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS,                                     64,                 192,                8, 30, 1200, 3000, 0,   45,    /* New Cb */
        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,                    64,                 192,                8, 30, 1200, 3000, 0,   45     /* New Cr */
    );

    (void) w;
    (void) h;

    return ve;
}

static inline int rawval_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

void rawval_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    if(!frame || !args || !frame->data[1] || !frame->data[2])
        return;

    const uint8_t old_cb = (uint8_t)rawval_clampi(args[0], 0, 255);
    const uint8_t old_cr = (uint8_t)rawval_clampi(args[1], 0, 255);
    const uint8_t new_cb = (uint8_t)rawval_clampi(args[2], 0, 255);
    const uint8_t new_cr = (uint8_t)rawval_clampi(args[3], 0, 255);

    const int uv_len = frame->ssm ? frame->len : frame->uv_len;

    if(uv_len <= 0)
        return;

    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int n_threads = vje_advise_num_threads(uv_len);

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < uv_len; i++) {
        if(Cb[i] == old_cb && Cr[i] == old_cr) {
            Cb[i] = new_cb;
            Cr[i] = new_cr;
        }
    }
}