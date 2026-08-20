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
#include "blackreplace.h"

vj_effect *blackreplace_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 512; ve->defaults[0] = 120;
    ve->limits[0][1] = 1; ve->limits[1][1] = 128; ve->defaults[1] = 24;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255; ve->defaults[2] = 150;
    ve->limits[0][3] = 0; ve->limits[1][3] = 255; ve->defaults[3] = 120;
    ve->limits[0][4] = 0; ve->limits[1][4] = 255; ve->defaults[4] = 250;

    ve->description = "Replace Black with Color (Darkness Key)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "Softness", "Red", "Green", "Blue");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 48, 360, 74, 100, 24, 420, 0, 2, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 4, 84, 54, 88, 30, 620, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

typedef struct {
    int n_threads;
} blackreplace_t;

void *blackreplace_malloc(int w, int h)
{
    blackreplace_t *br = (blackreplace_t*) vj_calloc(sizeof(blackreplace_t));

    if(!br)
        return NULL;

    br->n_threads = vje_advise_num_threads(w * h);

    return br;
}

void blackreplace_free(void *ptr)
{
    blackreplace_t *br = (blackreplace_t*) ptr;

    if(br)
        free(br);
}

static inline uint8_t blend_u8(uint8_t a, uint8_t b, int t)
{
    return (uint8_t)((a * (255 - t) + b * t) >> 8);
}

void blackreplace_apply(void *ptr, VJFrame *frame, int *args)
{
    blackreplace_t *br = (blackreplace_t*) ptr;

    const int threshold = args[0];
    const int softness = args[1];
    const int red = args[2];
    const int green = args[3];
    const int blue = args[4];
    const int len = frame->len;
    const int n_threads = br->n_threads;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int colorY = 0;
    int colorCb = 128;
    int colorCr = 128;

    _rgb2yuv(red, green, blue, colorY, colorCb, colorCr);

    const int full = threshold - softness;
    const int edge = threshold + softness;
    const int denom = edge - full;
    const int mul = (255 << 16) / denom;

    #pragma omp parallel for simd num_threads(n_threads) schedule(static)
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
        raw_t = raw_t & ~(raw_t >> 31);

        const int tmp = 255 - raw_t;
        const int gt = tmp >> 31;

        raw_t = raw_t + (gt & (255 - raw_t));

        const int mask_edge = (dark - edge) >> 31;
        const int mask_full = (dark - full) >> 31;
        const int chosen_t = (mask_full & 255) | (~mask_full & raw_t);
        const int t = mask_edge & chosen_t;

        Y[i] = blend_u8((uint8_t)y, (uint8_t)colorY, t);
        Cb[i] = blend_u8((uint8_t)cb, (uint8_t)colorCb, t);
        Cr[i] = blend_u8((uint8_t)cr, (uint8_t)colorCr, t);
    }
}
