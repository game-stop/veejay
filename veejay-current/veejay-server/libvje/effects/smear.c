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
#include <veejaycore/vjmem.h>
#include "smear.h"
#include "motionmap.h"

#define SMEAR_PARAMS 6

#define P_MODE        0
#define P_VALUE       1
#define P_LENGTH      2
#define P_MIX         3
#define P_CHROMA      4
#define P_SMEAR_DRIVE 5

typedef struct {
    uint8_t *tmp[3];
    int n__;
    int N__;
    int n_threads;
    void *motionmap;

    float eff_value;
    float eff_length;
    float eff_mix;
    float eff_chroma;
    float eff_smear_drive;
    int initialized;
} smear_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t smear_blend_y(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t smear_blend_uv(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);

    const int ac = (int)a - 128;
    const int bc = (int)b - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return (uint8_t)CLAMP_UV(v);
}

static inline int smear_to_q8_1000(int v)
{
    v = clampi(v, 0, 1000);
    return (v * 256 + 500) / 1000;
}



static inline int smear_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;
    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}



vj_effect *smear_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SMEAR_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_MODE] = 0;        ve->limits[1][P_MODE] = 3;        ve->defaults[P_MODE] = 0;
    ve->limits[0][P_VALUE] = 0;       ve->limits[1][P_VALUE] = 255;     ve->defaults[P_VALUE] = 1;
    ve->limits[0][P_LENGTH] = 0;      ve->limits[1][P_LENGTH] = 3000;   ve->defaults[P_LENGTH] = 1000;
    ve->limits[0][P_MIX] = 0;         ve->limits[1][P_MIX] = 1000;      ve->defaults[P_MIX] = 1000;
    ve->limits[0][P_CHROMA] = 0;      ve->limits[1][P_CHROMA] = 1000;   ve->defaults[P_CHROMA] = 1000;
    ve->limits[0][P_SMEAR_DRIVE] = 0; ve->limits[1][P_SMEAR_DRIVE] = 1000; ve->defaults[P_SMEAR_DRIVE] = 0;

    ve->description = "Pixel Smear";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->motion = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Value",
        "Smear Length",
        "Mix",
        "Chroma Amount",
        "Smear Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Horizontal",
        "Horizontal Average",
        "Vertical",
        "Vertical Average"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000,
        VJ_BEAT_DETAIL,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED,      4,   180,  12, 46, 1000, 3600, 0, 68,
        VJ_BEAT_TURBULENCE,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 180, 3000, 16, 62, 700,  2800, 0, 92,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 320, 1000, 12, 46, 1000, 3600, 0, 76,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 360, 1000, 12, 46, 1000, 3600, 0, 72,
        VJ_BEAT_TURBULENCE,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 140, 1000, 16, 62, 700,  2800, 0, 94
    );

    return ve;
}

void *smear_malloc(int w, int h)
{
    smear_t *s = (smear_t*) vj_calloc(sizeof(smear_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->tmp[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->tmp[0]) {
        free(s);
        return NULL;
    }

    s->tmp[1] = s->tmp[0] + len;
    s->tmp[2] = s->tmp[1] + len;

    s->n__ = 0;
    s->N__ = 0;
    s->motionmap = NULL;
    s->initialized = 0;

    s->n_threads = vje_advise_num_threads(len);

    return (void*) s;
}

void smear_free(void *ptr)
{
    smear_t *s = (smear_t*) ptr;

    free(s->tmp[0]);
    free(s);
}

static void smear_snapshot(smear_t *s, VJFrame *frame)
{
    const int len = frame->len;

    veejay_memcpy(s->tmp[0], frame->data[0], len);
    veejay_memcpy(s->tmp[1], frame->data[1], len);
    veejay_memcpy(s->tmp[2], frame->data[2], len);
}

static void smear_apply_axis(smear_t *s,
                             VJFrame *frame,
                             int val,
                             int length_q,
                             int mix_q8,
                             int chroma_mix_q8,
                             int vertical,
                             int average)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict sY  = s->tmp[0];
    const uint8_t *restrict sCb = s->tmp[1];
    const uint8_t *restrict sCr = s->tmp[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int idx = row + x;
            const int j = sY[idx];

            if(j < val)
                continue;

            int dist = (j * length_q + 500) / 1000;
            if(dist < 1 && length_q > 0)
                dist = 1;

            int sx = x;
            int sy = y;

            if(vertical) {
                sy += dist;
                if(sy >= height)
                    sy = height - 1;
            } else {
                sx += dist;
                if(sx >= width)
                    sx = width - 1;
            }

            const int src = sy * width + sx;

            int out_y;
            int out_u;
            int out_v;

            if(average) {
                out_y = ((int)sY[src] + (int)sY[idx] + 1) >> 1;
                out_u = (((int)sCb[src] - 128 + (int)sCb[idx] - 128) >> 1) + 128;
                out_v = (((int)sCr[src] - 128 + (int)sCr[idx] - 128) >> 1) + 128;
            } else {
                out_y = sY[src];
                out_u = sCb[src];
                out_v = sCr[src];
            }

            Y[idx]  = smear_blend_y(sY[idx],  (uint8_t)CLAMP_Y(out_y),  mix_q8);
            Cb[idx] = smear_blend_uv(sCb[idx], (uint8_t)CLAMP_UV(out_u), chroma_mix_q8);
            Cr[idx] = smear_blend_uv(sCr[idx], (uint8_t)CLAMP_UV(out_v), chroma_mix_q8);
        }
    }
}

int smear_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void smear_set_motionmap(void *ptr, void *priv)
{
    smear_t *s = (smear_t*) ptr;

    s->motionmap = priv;
}

void smear_apply(void *ptr, VJFrame *frame, int *args)
{
    smear_t *s = (smear_t*) ptr;

    int mode = args[P_MODE];
    int value = args[P_VALUE];
    int length = args[P_LENGTH];
    int mix = args[P_MIX];
    int chroma = args[P_CHROMA];
    int smear_drive = args[P_SMEAR_DRIVE];

    int tmp1 = mode;
    int tmp2 = value;
    int motion = 0;
    int interpolate = 0;

    if(s->motionmap && motionmap_active(s->motionmap)) {
        motionmap_scale_to(
            s->motionmap,
            255,
            3,
            0,
            0,
            &tmp2,
            &tmp1,
            &(s->n__),
            &(s->N__)
        );

        value = clampi(tmp2, 0, 255);
        mode = clampi(tmp1, 0, 3);

        motion = 1;
        interpolate = !(s->n__ == s->N__ || s->n__ == 0);
    } else {
        s->N__ = 0;
        s->n__ = 0;
    }

    const float fast = 0.28f;
    const float slow = 0.115f;

    if(!s->initialized) {
        s->eff_value = (float)value;
        s->eff_length = (float)length;
        s->eff_mix = (float)mix;
        s->eff_chroma = (float)chroma;
        s->eff_smear_drive = (float)smear_drive;
        s->initialized = 1;
    } else {
        value = smear_smooth_i(&s->eff_value, value, fast, slow);
        length = smear_smooth_i(&s->eff_length, length, fast * 0.72f, slow);
        mix = smear_smooth_i(&s->eff_mix, mix, fast * 0.80f, slow);
        chroma = smear_smooth_i(&s->eff_chroma, chroma, fast * 0.72f, slow);
        smear_drive = smear_smooth_i(&s->eff_smear_drive, smear_drive, fast, slow);
    }

    mode = clampi(mode, 0, 3);
    value = clampi(value, 0, 255);
    length = clampi(length, 0, 3000);
    mix = clampi(mix, 0, 1000);
    chroma = clampi(chroma, 0, 1000);
    smear_drive = clampi(smear_drive, 0, 1000);

    const int effective_value = clampi(value - ((smear_drive * 96 + 500) / 1000), 0, 255);
    const int effective_length = clampi(length + ((smear_drive * 2000 + 500) / 1000), 0, 3000);
    const int effective_mix = clampi(mix + (((1000 - mix) * smear_drive + 500) / 1000), 0, 1000);
    const int effective_chroma = clampi(chroma + (((1000 - chroma) * smear_drive + 500) / 1000), 0, 1000);

    const int mix_q8 = smear_to_q8_1000(effective_mix);
    const int chroma_q8 = smear_to_q8_1000(effective_chroma);
    const int chroma_mix_q8 = (mix_q8 * chroma_q8 + 128) >> 8;

    smear_snapshot(s, frame);

    switch(mode) {
        case 0:
            smear_apply_axis(s, frame, effective_value, effective_length, mix_q8, chroma_mix_q8, 0, 0);
            break;
        case 1:
            smear_apply_axis(s, frame, effective_value, effective_length, mix_q8, chroma_mix_q8, 0, 1);
            break;
        case 2:
            smear_apply_axis(s, frame, effective_value, effective_length, mix_q8, chroma_mix_q8, 1, 0);
            break;
        case 3:
            smear_apply_axis(s, frame, effective_value, effective_length, mix_q8, chroma_mix_q8, 1, 1);
            break;
        default:
            break;
    }

    if(interpolate)
        motionmap_interpolate_frame(s->motionmap, frame, s->N__, s->n__);

    if(motion)
        motionmap_store_frame(s->motionmap, frame);
}
