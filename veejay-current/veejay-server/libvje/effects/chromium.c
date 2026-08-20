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
#include "chromium.h"

#define CHROMIUM_PARAMS 4

#define P_MODE          0
#define P_AMOUNT        1
#define P_CHROMA_GAIN   2
#define P_CHROMA_ROTATE 3

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t chromium_u8(int v)
{
    return (uint8_t) clampi(v, 0, 255);
}

static inline int chromium_blend1000(int a, int b, int q)
{
    return (a * (1000 - q) + b * q + 500) / 1000;
}

static inline int chromium_scale_chroma(int c, int gain)
{
    return clampi(128 + (((c - 128) * gain + 500) / 1000), 0, 255);
}

static inline void chromium_mode_target(int mode, int cb, int cr, int *tb, int *tr)
{
    switch(mode) {
        case 0:
            *tb = 255 - cb;
            *tr = cr;
            break;
        case 1:
            *tb = cb;
            *tr = 255 - cr;
            break;
        case 2:
            *tb = 255 - cb;
            *tr = 255 - cr;
            break;
        case 3:
            *tb = cr;
            *tr = cb;
            break;
        case 4:
            *tb = 255 - cr;
            *tr = cr;
            break;
        case 5:
            *tb = cb;
            *tr = 255 - cb;
            break;
        case 6:
            *tb = 255 - cr;
            *tr = 255 - cb;
            break;
        case 7:
            *tb = 255 - cr;
            *tr = cb;
            break;
        case 8:
            *tb = cr;
            *tr = 255 - cb;
            break;
        case 9:
            *tb = (uint8_t)(0xaa - cb);
            *tr = cr;
            break;
        default:
            *tb = cb;
            *tr = cr;
            break;
    }
}

vj_effect *chromium_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = CHROMIUM_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = 9;
    ve->defaults[P_MODE] = 0;

    ve->limits[0][P_AMOUNT] = 0;
    ve->limits[1][P_AMOUNT] = 1000;
    ve->defaults[P_AMOUNT] = 720;

    ve->limits[0][P_CHROMA_GAIN] = 0;
    ve->limits[1][P_CHROMA_GAIN] = 2000;
    ve->defaults[P_CHROMA_GAIN] = 1100;

    ve->limits[0][P_CHROMA_ROTATE] = -1000;
    ve->limits[1][P_CHROMA_ROTATE] = 1000;
    ve->defaults[P_CHROMA_ROTATE] = 0;

    ve->description = "Chromium";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Transform Amount", "Chroma Energy", "Chroma Rotate");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE,
        "Chroma Blue", "Chroma Red", "Chroma Red and Blue", "Chroma Swap",
        "Chroma Yellow", "Chroma Orange", "Chroma Rose",
        "Chroma Green", "Chroma Purple", "Chroma Yellow swap White"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 180, 1000, 92, 100, 10, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 700, 1900, 82, 100, 80, 900, 0, 5, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, -800, 800, 88, 100, 0, 360, 0, 5, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void chromium_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int mode_arg = args[P_MODE];
    const int amount_arg = args[P_AMOUNT];
    const int chroma_gain_arg = args[P_CHROMA_GAIN];
    const int rotate_arg = args[P_CHROMA_ROTATE];

    const int mode = clampi(mode_arg, 0, 9);
    const int amount_eff = clampi(amount_arg, 0, 1000);
    const int gain_eff = clampi(chroma_gain_arg, 0, 2000);
    const int rotate_eff = clampi(rotate_arg, -1000, 1000);

    const int len = frame->ssm ? frame->len : frame->uv_len;

    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        const int cb0 = Cb[i];
        const int cr0 = Cr[i];

        int tb;
        int tr;

        chromium_mode_target(mode, cb0, cr0, &tb, &tr);

        int cb = chromium_blend1000(cb0, tb, amount_eff);
        int cr = chromium_blend1000(cr0, tr, amount_eff);

        if(rotate_eff > 0)
        {
            const int q = rotate_eff;
            const int rb = chromium_blend1000(cb, cr, q);
            const int rr = chromium_blend1000(cr, 255 - cb, q);

            cb = rb;
            cr = rr;
        }
        else if(rotate_eff < 0)
        {
            const int q = -rotate_eff;
            const int rb = chromium_blend1000(cb, 255 - cr, q);
            const int rr = chromium_blend1000(cr, cb, q);

            cb = rb;
            cr = rr;
        }

        cb = chromium_scale_chroma(cb, gain_eff);
        cr = chromium_scale_chroma(cr, gain_eff);

        Cb[i] = chromium_u8(cb);
        Cr[i] = chromium_u8(cr);
    }
}
