/*
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "vintagefilm.h"

#define VINTAGEFILM_PARAMS 11

#define P_SCRATCH_INTENSITY 0
#define P_DUST_INTENSITY    1
#define P_FLICKER_INTENSITY 2
#define P_FLICKER_FREQUENCY 3
#define P_GRAIN_STRENGTH    4
#define P_VIGNETTE_STRENGTH 5
#define P_SCRATCH_LIFESPAN  6
#define P_BEAT_DIRT         7
#define P_BEAT_FLICKER      8
#define P_BEAT_PUSH         9
#define P_BEAT_SMOOTH      10

typedef struct {
    int framecounter;
    uint8_t *scratch_map;
    uint16_t *vignette_lut;
    uint32_t rng_state;
    float beat_env;
    float beat_kick;
    int n_threads;
    int width;
    int height;
} vintagefilm_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t vintagefilm_u8(int v)
{
    if((unsigned int)v > 255U)
        return (v < 0) ? 0 : 255;

    return (uint8_t)v;
}

static inline uint32_t vintagefilm_rng(uint32_t *state)
{
    uint32_t x = *state;

    if(x == 0)
        x = 0x12345678u;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    *state = x;

    return x;
}

static inline uint32_t vintagefilm_hash(uint32_t input)
{
    uint32_t state = input * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;

    return (word >> 22u) ^ word;
}

static inline int vintagefilm_beat_shape(int beat_push)
{
    int sq;

    beat_push = clampi(beat_push, 0, 1000);
    sq = (beat_push * beat_push + 500) / 1000;

    return clampi((beat_push * 35 + sq * 65 + 50) / 100, 0, 1000);
}

static void vintagefilm_build_vignette(vintagefilm_t *vf, int w, int h)
{
    const int cx = w >> 1;
    const int cy = h >> 1;

    int64_t max_d2 = (int64_t)cx * (int64_t)cx + (int64_t)cy * (int64_t)cy;
    if(max_d2 <= 0)
        max_d2 = 1;

#pragma omp parallel for schedule(static) num_threads(vf->n_threads)
    for(int y = 0; y < h; y++) {
        const int dy = y - cy;
        const int dy2 = dy * dy;
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const int dx = x - cx;
            const int d2 = dx * dx + dy2;
            int v = (int)(((int64_t)d2 * 65535LL + (max_d2 >> 1)) / max_d2);

            if(v > 65535)
                v = 65535;

            vf->vignette_lut[row + x] = (uint16_t)v;
        }
    }
}

vj_effect *vintagefilm_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = VINTAGEFILM_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
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

    ve->defaults[P_SCRATCH_INTENSITY] = 10;
    ve->defaults[P_DUST_INTENSITY]    = 20;
    ve->defaults[P_FLICKER_INTENSITY] = 5;
    ve->defaults[P_FLICKER_FREQUENCY] = 50;
    ve->defaults[P_GRAIN_STRENGTH]    = 5;
    ve->defaults[P_VIGNETTE_STRENGTH] = 10;
    ve->defaults[P_SCRATCH_LIFESPAN]  = 5;
    ve->defaults[P_BEAT_DIRT]         = 45;
    ve->defaults[P_BEAT_FLICKER]      = 35;
    ve->defaults[P_BEAT_PUSH]         = 0;
    ve->defaults[P_BEAT_SMOOTH]       = 520;

    ve->limits[0][P_SCRATCH_INTENSITY] = 0;   ve->limits[1][P_SCRATCH_INTENSITY] = 100;
    ve->limits[0][P_DUST_INTENSITY]    = 0;   ve->limits[1][P_DUST_INTENSITY]    = 100;
    ve->limits[0][P_FLICKER_INTENSITY] = 0;   ve->limits[1][P_FLICKER_INTENSITY] = 50;
    ve->limits[0][P_FLICKER_FREQUENCY] = 0;   ve->limits[1][P_FLICKER_FREQUENCY] = 500;
    ve->limits[0][P_GRAIN_STRENGTH]    = 0;   ve->limits[1][P_GRAIN_STRENGTH]    = 50;
    ve->limits[0][P_VIGNETTE_STRENGTH] = 0;   ve->limits[1][P_VIGNETTE_STRENGTH] = 100;
    ve->limits[0][P_SCRATCH_LIFESPAN]  = 0;   ve->limits[1][P_SCRATCH_LIFESPAN]  = 50;
    ve->limits[0][P_BEAT_DIRT]         = 0;   ve->limits[1][P_BEAT_DIRT]         = 100;
    ve->limits[0][P_BEAT_FLICKER]      = 0;   ve->limits[1][P_BEAT_FLICKER]      = 100;
    ve->limits[0][P_BEAT_PUSH]         = 0;   ve->limits[1][P_BEAT_PUSH]         = 1000;
    ve->limits[0][P_BEAT_SMOOTH]       = 0;   ve->limits[1][P_BEAT_SMOOTH]       = 1000;

    ve->description = "Vintage Film";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Scratch Intensity",
        "Dust Intensity",
        "Flicker Intensity",
        "Flicker Frequency",
        "Grain Strength",
        "Vignette Strength",
        "Scratch Lifespan",
        "Beat Dirt",
        "Beat Flicker",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_TURBULENCE, VJ_BEAT_F_PHRASE_ONLY,                     0,                  60,                 6,  22, 1800, 4200, 900,  22,    /* Scratch Intensity */
        VJ_BEAT_TURBULENCE, VJ_BEAT_F_PHRASE_ONLY,                     0,                  70,                 6,  22, 1800, 4200, 900,  24,    /* Dust Intensity */
        VJ_BEAT_GLOW,       VJ_BEAT_F_PHRASE_ONLY,                     0,                  26,                 6,  22, 1800, 4200, 900,  24,    /* Flicker Intensity */
        VJ_BEAT_SPEED,      VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 4,                  160,                6,  22, 1800, 4200, 900,  26,    /* Flicker Frequency */
        VJ_BEAT_TURBULENCE, VJ_BEAT_F_PHRASE_ONLY,                     0,                  30,                 6,  22, 1800, 4200, 900,  22,    /* Grain Strength */
        VJ_BEAT_CONTRAST,   VJ_BEAT_F_PHRASE_ONLY,                     0,                  60,                 5,  18, 1800, 4200, 900,  18,    /* Vignette Strength */
        VJ_BEAT_MEMORY,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 1,                  36,                 6,  22, 1800, 4200, 900,  24,    /* Scratch Lifespan */

        VJ_BEAT_TURBULENCE, VJ_BEAT_F_REJECT,                          VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Beat Dirt */
        VJ_BEAT_GLOW,       VJ_BEAT_F_REJECT,                          VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Beat Flicker */

        VJ_BEAT_KICK,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,   0,                  780,                18, 72, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,     VJ_BEAT_F_PHRASE_ONLY,                     260,                820,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    (void) w;
    (void) h;

    return ve;
}

void *vintagefilm_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    vintagefilm_t *vf = (vintagefilm_t *) vj_calloc(sizeof(vintagefilm_t));
    if(!vf)
        return NULL;

    const int len = w * h;

    vf->scratch_map = (uint8_t*) vj_calloc((size_t)w);
    if(!vf->scratch_map) {
        free(vf);
        return NULL;
    }

    vf->vignette_lut = (uint16_t*) vj_malloc(sizeof(uint16_t) * (size_t)len);
    if(!vf->vignette_lut) {
        free(vf->scratch_map);
        free(vf);
        return NULL;
    }

    vf->framecounter = 0;
    vf->rng_state = 123456789u;
    vf->beat_env = 0.0f;
    vf->beat_kick = 0.0f;
    vf->width = w;
    vf->height = h;

    vf->n_threads = vje_advise_num_threads(len);
    if(vf->n_threads < 1)
        vf->n_threads = 1;

    vintagefilm_build_vignette(vf, w, h);

    return (void*) vf;
}

void vintagefilm_free(void *ptr)
{
    vintagefilm_t *vf = (vintagefilm_t*) ptr;

    if(!vf)
        return;

    if(vf->scratch_map)
        free(vf->scratch_map);

    if(vf->vignette_lut)
        free(vf->vignette_lut);

    free(vf);
}

void vintagefilm_apply(void *ptr, VJFrame *frame, int *args)
{
    vintagefilm_t *vf = (vintagefilm_t*) ptr;

    if(!vf || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    if(width != vf->width || height != vf->height)
        return;

    int scratch_intensity = clampi(args[P_SCRATCH_INTENSITY], 0, 100);
    int dust_intensity    = clampi(args[P_DUST_INTENSITY], 0, 100);
    int flicker_intensity = clampi(args[P_FLICKER_INTENSITY], 0, 50);
    const int flicker_freq = clampi(args[P_FLICKER_FREQUENCY], 0, 500);
    int grain_strength    = clampi(args[P_GRAIN_STRENGTH], 0, 50);
    int vignette_strength = clampi(args[P_VIGNETTE_STRENGTH], 0, 100);
    const int scratch_lifespan = clampi(args[P_SCRATCH_LIFESPAN], 0, 50);
    const int beat_dirt   = clampi(args[P_BEAT_DIRT], 0, 100);
    const int beat_flicker = clampi(args[P_BEAT_FLICKER], 0, 100);
    const int beat_push   = clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = clampi(args[P_BEAT_SMOOTH], 0, 1000);

    const int beat_shaped = vintagefilm_beat_shape(beat_push);
    const float target = (float)beat_shaped * 0.001f;
    const float smooth_t = (float)beat_smooth * 0.001f;
    const float attack = 0.16f + (1.0f - smooth_t) * 0.34f;
    const float release = 0.020f + (1.0f - smooth_t) * 0.085f;
    const float prev_env = vf->beat_env;

    if(target > vf->beat_env)
        vf->beat_env += (target - vf->beat_env) * attack;
    else
        vf->beat_env += (target - vf->beat_env) * release;

    if(target > prev_env)
        vf->beat_kick += (target - prev_env) * (0.70f + (1.0f - smooth_t) * 0.25f);

    vf->beat_kick *= 0.58f + smooth_t * 0.22f;

    if(vf->beat_env < 0.0001f)
        vf->beat_env = 0.0f;
    else if(vf->beat_env > 1.0f)
        vf->beat_env = 1.0f;

    if(vf->beat_kick < 0.0001f)
        vf->beat_kick = 0.0f;
    else if(vf->beat_kick > 1.0f)
        vf->beat_kick = 1.0f;

    const int beat_q = clampi((int)(vf->beat_env * 1000.0f + 0.5f), 0, 1000);
    const int kick_q = clampi((int)(vf->beat_kick * 1000.0f + 0.5f), 0, 1000);
    const int dirt_drive = clampi((beat_q * 35 + kick_q * 65 + 50) / 100, 0, 1000);
    const int dirt_q = clampi((dirt_drive * beat_dirt + 50) / 100, 0, 1000);
    const int flicker_q = clampi(((beat_q * 35 + kick_q * 65 + 50) / 100) * beat_flicker / 100, 0, 1000);

    scratch_intensity = clampi(scratch_intensity + (dirt_q * 36 + 500) / 1000, 0, 100);
    dust_intensity    = clampi(dust_intensity    + (dirt_q * 48 + 500) / 1000, 0, 100);
    grain_strength    = clampi(grain_strength    + (dirt_q * 24 + 500) / 1000, 0, 50);
    vignette_strength = clampi(vignette_strength + (flicker_q * 12 + 500) / 1000, 0, 100);
    flicker_intensity = clampi(flicker_intensity + (flicker_q * 14 + 500) / 1000, 0, 50);

    uint8_t *restrict Yp = frame->data[0];
    uint8_t *restrict Up = frame->data[1];
    uint8_t *restrict Vp = frame->data[2];

    uint8_t *restrict scratch_map = vf->scratch_map;
    const uint16_t *restrict vignette = vf->vignette_lut;

    vf->framecounter++;

    int global_gain = 256;

    if(flicker_intensity > 0 && flicker_freq > 0 && (vf->framecounter % flicker_freq) == 0) {
        const int range = flicker_intensity * 2 + 1;
        const int flick_mod = (int)(vintagefilm_rng(&vf->rng_state) % (uint32_t)range) - flicker_intensity;

        global_gain = (256 * (100 + flick_mod)) / 100;
    }

    if(flicker_q > 0) {
        int beat_mod = (flicker_q * 26 + 500) / 1000;
        const uint32_t beat_noise = vintagefilm_hash((uint32_t)vf->framecounter ^ 0x9e3779b9u);

        if(beat_noise & 1u)
            beat_mod = -(beat_mod >> 1);

        global_gain += (256 * beat_mod) / 100;
    }

    global_gain = clampi(global_gain, 72, 384);

#pragma omp parallel for schedule(static) num_threads(vf->n_threads)
    for(int x = 0; x < width; x++) {
        if(scratch_map[x] > 0)
            scratch_map[x]--;
    }

    if(scratch_intensity > 0) {
        int num_scratches = (width * scratch_intensity) / 5000;

        if(dirt_q > 0)
            num_scratches += (width * dirt_q + 12000) / 24000;

        if(num_scratches < 1 && (int)(vintagefilm_rng(&vf->rng_state) % 100u) < scratch_intensity)
            num_scratches = 1;

        if(num_scratches > (width >> 2))
            num_scratches = width >> 2;

        for(int i = 0; i < num_scratches; i++) {
            const int x = (int)(vintagefilm_rng(&vf->rng_state) % (uint32_t)width);
            int life = 1 + scratch_lifespan + (int)(vintagefilm_rng(&vf->rng_state) % 3u);

            if(kick_q > 0)
                life += (kick_q * 5 + 500) / 1000;

            scratch_map[x] = (uint8_t)clampi(life, 1, 255);
        }
    }

    const uint32_t frame_seed = vintagefilm_rng(&vf->rng_state);
    const int vignette_q8 = (vignette_strength * 256 + 50) / 100;
    const int dust_gate = dust_intensity * 5;
    const int scratch_cut = 50 - ((kick_q * 18 + 500) / 1000);

#pragma omp parallel for schedule(static) num_threads(vf->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const uint32_t line_noise = vintagefilm_hash(frame_seed ^ (uint32_t)y);

        for(int x = 0; x < width; x++) {
            const int idx = row + x;

            int final_gain = global_gain;

            if(vignette_q8 > 0) {
                const int vig_reduction = ((int)vignette[idx] * vignette_q8 + 32768) >> 16;

                final_gain -= vig_reduction;

                if(final_gain < 0)
                    final_gain = 0;
            }

            int yv = Yp[idx];

            const uint32_t p_noise = vintagefilm_hash(line_noise ^ ((uint32_t)x * 0x1234567u));

            if(scratch_map[x] > 0 && ((p_noise & 0xffu) > (uint32_t)scratch_cut))
                yv -= 30 + (int)(p_noise % 30u) + ((kick_q * 12 + 500) / 1000);

            if(dust_intensity > 0 && (p_noise & 0x7fffu) < (uint32_t)dust_gate) {
                const int dust_span = 40 + ((kick_q * 18 + 500) / 1000);
                const int dust = (int)((p_noise >> 16) % (uint32_t)(dust_span * 2 + 1)) - dust_span;

                yv += dust;
            }

            if(grain_strength > 0) {
                const int noise = (int)((p_noise >> 8) % (uint32_t)(2 * grain_strength + 1)) - grain_strength;

                yv += noise;
            }

            yv = (yv * final_gain) >> 8;
            Yp[idx] = vintagefilm_u8(yv);

            int u = Up[idx];
            int v = Vp[idx];

            u = (u * 4 + 110 * 6) / 10;
            v = (v * 4 + 150 * 6) / 10;

            Up[idx] = vintagefilm_u8(128 + (((u - 128) * final_gain) >> 8));
            Vp[idx] = vintagefilm_u8(128 + (((v - 128) * final_gain) >> 8));
        }
    }
}
