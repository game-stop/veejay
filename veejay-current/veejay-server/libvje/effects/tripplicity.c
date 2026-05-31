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

#define TRIPPLICITY_PARAMS 7

#define P_OPACITY_Y      0
#define P_OPACITY_CB     1
#define P_OPACITY_CR     2
#define P_BEAT_MIX       3
#define P_BEAT_CHROMA    4
#define P_BEAT_PUSH      5
#define P_BEAT_SMOOTH    6

typedef struct {
    float beat_env;
    float beat_kick;
    int n_threads;
} tripplicity_t;

static inline int tripplicity_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t tripplicity_mix_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline int tripplicity_to_q8(int v)
{
    v = tripplicity_clampi(v, 0, 255);
    return (v * 256 + 127) / 255;
}

static inline int tripplicity_shape_beat(int beat_push)
{
    beat_push = tripplicity_clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    const int shaped = (beat_push * 30 + sq * 70 + 50) / 100;

    return tripplicity_clampi(shaped, 0, 1000);
}

static inline float tripplicity_unit(int v)
{
    return (float)tripplicity_clampi(v, 0, 1000) * 0.001f;
}

static inline int tripplicity_apply_beat_lift(int base_q8, int amount, float drive)
{
    amount = tripplicity_clampi(amount, 0, 1000);
    base_q8 = tripplicity_clampi(base_q8, 0, 256);

    const int headroom = 256 - base_q8;
    const int lift = (int)((float)headroom * ((float)amount * 0.001f) * drive + 0.5f);

    return tripplicity_clampi(base_q8 + lift, 0, 256);
}

vj_effect *tripplicity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TRIPPLICITY_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_OPACITY_Y]   = 0; ve->limits[1][P_OPACITY_Y]   = 255;  ve->defaults[P_OPACITY_Y]   = 150;
    ve->limits[0][P_OPACITY_CB]  = 0; ve->limits[1][P_OPACITY_CB]  = 255;  ve->defaults[P_OPACITY_CB]  = 150;
    ve->limits[0][P_OPACITY_CR]  = 0; ve->limits[1][P_OPACITY_CR]  = 255;  ve->defaults[P_OPACITY_CR]  = 150;
    ve->limits[0][P_BEAT_MIX]    = 0; ve->limits[1][P_BEAT_MIX]    = 1000; ve->defaults[P_BEAT_MIX]    = 260;
    ve->limits[0][P_BEAT_CHROMA] = 0; ve->limits[1][P_BEAT_CHROMA] = 1000; ve->defaults[P_BEAT_CHROMA] = 180;
    ve->limits[0][P_BEAT_PUSH]   = 0; ve->limits[1][P_BEAT_PUSH]   = 1000; ve->defaults[P_BEAT_PUSH]   = 0;
    ve->limits[0][P_BEAT_SMOOTH] = 0; ve->limits[1][P_BEAT_SMOOTH] = 1000; ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->description = "Normal Overlay (per Channel)";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity Y",
        "Opacity Cb",
        "Opacity Cr",
        "Beat Mix",
        "Beat Chroma",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS,                       32,                 220,                8,  30, 1200, 3000, 0,    45,    /* Opacity Y */
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS,                       32,                 220,                8,  30, 1200, 3000, 0,    42,    /* Opacity Cb */
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS,                       32,                 220,                8,  30, 1200, 3000, 0,    42,    /* Opacity Cr */
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_REJECT,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Beat Mix - user tuning */
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_REJECT,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Beat Chroma - user tuning */
        VJ_BEAT_KICK,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,   0,                  760,                18, 76, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,           VJ_BEAT_F_PHRASE_ONLY,                      260,                820,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    (void) w;
    (void) h;

    return ve;
}

void *tripplicity_malloc(int w, int h)
{
    tripplicity_t *t = (tripplicity_t*) vj_calloc(sizeof(tripplicity_t));
    if(!t)
        return NULL;

    t->beat_env = 0.0f;
    t->beat_kick = 0.0f;
    t->n_threads = vje_advise_num_threads(w * h);

    return (void*) t;
}

void tripplicity_free(void *ptr)
{
    if(ptr)
        free(ptr);
}

void tripplicity_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    tripplicity_t *t = (tripplicity_t*) ptr;

    const int beat_mix = tripplicity_clampi(args[P_BEAT_MIX], 0, 1000);
    const int beat_chroma = tripplicity_clampi(args[P_BEAT_CHROMA], 0, 1000);
    const int beat_push = tripplicity_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = tripplicity_clampi(args[P_BEAT_SMOOTH], 0, 1000);

    const float target = (float)tripplicity_shape_beat(beat_push) * 0.001f;

    float beat_env = 0.0f;
    float beat_kick = 0.0f;

    const float old_env = t->beat_env;
    const float smooth = tripplicity_unit(beat_smooth);
    const float attack = 0.52f - smooth * 0.34f;
    const float release = 0.16f - smooth * 0.105f;
    const float kick_decay = 0.42f + smooth * 0.34f;

    if(target > t->beat_env)
        t->beat_env += (target - t->beat_env) * attack;
    else
        t->beat_env += (target - t->beat_env) * release;

    if(t->beat_env < 0.0001f)
        t->beat_env = 0.0f;
    else if(t->beat_env > 1.0f)
        t->beat_env = 1.0f;

    if(t->beat_env > old_env)
        t->beat_kick += (t->beat_env - old_env) * 1.45f;

    t->beat_kick *= kick_decay;
    if(t->beat_kick > 1.0f)
        t->beat_kick = 1.0f;
    else if(t->beat_kick < 0.0001f)
        t->beat_kick = 0.0f;

    beat_env = t->beat_env;
    beat_kick = t->beat_kick;


    const float drive = beat_env * beat_env + beat_kick * 0.55f;

    int qY  = tripplicity_to_q8(args[P_OPACITY_Y]);
    int qCb = tripplicity_to_q8(args[P_OPACITY_CB]);
    int qCr = tripplicity_to_q8(args[P_OPACITY_CR]);

    qY  = tripplicity_apply_beat_lift(qY,  beat_mix, drive);
    qCb = tripplicity_apply_beat_lift(qCb, beat_chroma, drive);
    qCr = tripplicity_apply_beat_lift(qCr, beat_chroma, drive);

    uint8_t *restrict Y1  = frame->data[0];
    uint8_t *restrict Cb1 = frame->data[1];
    uint8_t *restrict Cr1 = frame->data[2];

    const uint8_t *restrict Y2  = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    const int len = frame->len;
    const int uv_len = frame->uv_len;
#pragma omp parallel num_threads(t->n_threads)
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
