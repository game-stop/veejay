/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include "opacity.h"

typedef struct {
    int n_threads;
} darkreplace_t;

static inline uint8_t blend_u8(uint8_t a, uint8_t b, int t)
{
    return (uint8_t)((a * (255 - t) + b * t) >> 8);
}

vj_effect *darkreplace_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 2;
    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 512; ve->defaults[0] = 32;
    ve->limits[0][1] = 1; ve->limits[1][1] = 128; ve->defaults[1] = 24;

    ve->description = "Replace Dark";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "Softness");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 12, 300, 82, 100, 15, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INERTIA, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 4, 96, 68, 96, 0, 620, 0, 1, 0, VJ_BEAT_COST_CHEAP, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

void *darkreplace_malloc(int w, int h)
{
    darkreplace_t *dr = (darkreplace_t*) vj_calloc(sizeof(darkreplace_t));

    if(!dr)
        return NULL;

    dr->n_threads = vje_advise_num_threads(w * h);

    return dr;
}

void darkreplace_free(void *ptr)
{
    darkreplace_t *dr = (darkreplace_t*) ptr;

    if(dr)
        free(dr);
}

void darkreplace_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    darkreplace_t *dr = (darkreplace_t*) ptr;

    const int threshold = args[0];
    const int softness = args[1];
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    const int full = threshold - softness;
    const int edge = threshold + softness;
    const int denom = edge - full;
    const int mul = denom > 0 ? ((255 << 16) / denom) : 0;

    #pragma omp parallel for num_threads(dr->n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        const int y = Y[i];
        const int cb = Cb[i];
        const int cr = Cr[i];
        const int cbd = cb - 128;
        const int crd = cr - 128;
        const int abs_cb = (cbd ^ (cbd >> 31)) - (cbd >> 31);
        const int abs_cr = (crd ^ (crd >> 31)) - (crd >> 31);
        const int dark = y + abs_cb + abs_cr;
        const int diff = edge - dark;
        int raw_t = (int)(((int64_t)diff * (int64_t)mul) >> 16);

        const int mask_edge = (dark - edge) >> 31;
        const int mask_full = (dark - full) >> 31;
        const int chosen_t = (mask_full & 255) | (~mask_full & raw_t);
        const int t = mask_edge & chosen_t;

        Y[i] = blend_u8(Y[i], Y2[i], t);
        Cb[i] = blend_u8(Cb[i], Cb2[i], t);
        Cr[i] = blend_u8(Cr[i], Cr2[i], t);
    }
}
