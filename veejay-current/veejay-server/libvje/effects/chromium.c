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

#define CHROMIUM_PARAMS 5

#define P_MODE          0
#define P_AMOUNT        1
#define P_CHROMA_GAIN   2
#define P_CHROMA_ROTATE 3
#define P_BEAT_PUSH     4

static inline int chromium_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t chromium_u8(int v)
{
    return (uint8_t) chromium_clampi(v, 0, 255);
}

static inline int chromium_blend1000(int a, int b, int q)
{
    return (a * (1000 - q) + b * q + 500) / 1000;
}

static inline int chromium_scale_chroma(int c, int gain)
{
    return chromium_clampi(128 + (((c - 128) * gain + 500) / 1000), 0, 255);
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
            *tb = (uint8_t) (0xaa - cb);
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

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->description = "Chromium";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Transform Amount",
        "Chroma Energy",
        "Chroma Rotate",
        "Beat Push"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE,
        "Chroma Blue", "Chroma Red", "Chroma Red and Blue", "Chroma Swap",
        "Chroma Yellow", "Chroma Orange", "Chroma Rose",
        "Chroma Green", "Chroma Purple", "Chroma Yellow swap White"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000, /* Mode */
        VJ_BEAT_SNARE,    VJ_BEAT_F_CONTINUOUS,                           120,                1000,               12, 52,  120,  900,  0,    76,    /* Transform Amount */
        VJ_BEAT_KICK,     VJ_BEAT_F_CONTINUOUS,                           700,                1900,               14, 58,  90,   680,  0,    86,    /* Chroma Energy */
        VJ_BEAT_HAT,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,          -520,                520,                4,  24,  80,   520,  0,    48,    /* Chroma Rotate */
        VJ_BEAT_KICK,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,        0,                  1000,               20, 88,  60,   360,  0,    100     /* Beat Push */
    );

    (void) w;
    (void) h;

    return ve;
}

void chromium_apply(void *ptr, VJFrame *frame, int *args)
{
    int mode;
    int amount;
    int chroma_gain;
    int rotate;
    int beat_push;

    int amount_eff;
    int gain_eff;
    int rotate_eff;

    int n_threads;
    int len;

    uint8_t *restrict Cb;
    uint8_t *restrict Cr;

    int i;

    (void) ptr;

    if(!frame || !args)
        return;

    mode = chromium_clampi(args[P_MODE], 0, 9);
    amount = chromium_clampi(args[P_AMOUNT], 0, 1000);
    chroma_gain = chromium_clampi(args[P_CHROMA_GAIN], 0, 2000);
    rotate = chromium_clampi(args[P_CHROMA_ROTATE], -1000, 1000);
    beat_push = chromium_clampi(args[P_BEAT_PUSH], 0, 1000);

    amount_eff = amount + (((1000 - amount) * beat_push + 500) / 1000);
    amount_eff = chromium_clampi(amount_eff, 0, 1000);

    gain_eff = chroma_gain + ((beat_push * 450 + 500) / 1000);
    gain_eff = chromium_clampi(gain_eff, 0, 2000);

    rotate_eff = rotate;
    if(beat_push > 0)
        rotate_eff += ((mode & 1) ? beat_push : -beat_push) >> 2;
    rotate_eff = chromium_clampi(rotate_eff, -1000, 1000);

    n_threads = vje_advise_num_threads(frame->len);
    len = frame->ssm ? frame->len : frame->uv_len;

    Cb = frame->data[1];
    Cr = frame->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(i = 0; i < len; i++) {
        int cb0 = Cb[i];
        int cr0 = Cr[i];

        int tb;
        int tr;

        int cb;
        int cr;

        chromium_mode_target(mode, cb0, cr0, &tb, &tr);

        cb = chromium_blend1000(cb0, tb, amount_eff);
        cr = chromium_blend1000(cr0, tr, amount_eff);

        if(rotate_eff > 0) {
            int q = rotate_eff;
            int rb = chromium_blend1000(cb, cr, q);
            int rr = chromium_blend1000(cr, 255 - cb, q);
            cb = rb;
            cr = rr;
        }
        else if(rotate_eff < 0) {
            int q = -rotate_eff;
            int rb = chromium_blend1000(cb, 255 - cr, q);
            int rr = chromium_blend1000(cr, cb, q);
            cb = rb;
            cr = rr;
        }

        cb = chromium_scale_chroma(cb, gain_eff);
        cr = chromium_scale_chroma(cr, gain_eff);

        Cb[i] = chromium_u8(cb);
        Cr[i] = chromium_u8(cr);
    }
}
