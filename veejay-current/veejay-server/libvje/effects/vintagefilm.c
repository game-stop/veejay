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
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "vintagefilm.h"

#define VINTAGEFILM_PARAMS 9

#define P_SCRATCH_INTENSITY 0
#define P_DUST_INTENSITY    1
#define P_FLICKER_INTENSITY 2
#define P_FLICKER_FREQUENCY 3
#define P_GRAIN_STRENGTH    4
#define P_VIGNETTE_STRENGTH 5
#define P_SCRATCH_LIFESPAN  6
#define P_DIRT_DRIVE        7
#define P_FLICKER_DRIVE     8

typedef struct {
    uint8_t *region;
    int framecounter;
    uint8_t *scratch_map;
    uint16_t *vignette_lut;
    uint32_t rng_state;

    float scratch_env;
    float dust_env;
    float flicker_env;
    float flicker_freq_env;
    float grain_env;
    float vignette_env;
    float lifespan_env;
    float dirt_drive_env;
    float flicker_drive_env;
    int smooth_ready;

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



static inline float vintagefilm_smoothf(float oldv, float target, float attack, float release)
{
    return target > oldv
        ? oldv + (target - oldv) * attack
        : oldv + (target - oldv) * release;
}

static void vintagefilm_build_vignette(vintagefilm_t *vf, int w, int h)
{
    const int cx = w >> 1;
    const int cy = h >> 1;

    const int64_t max_d2 = (int64_t)cx * (int64_t)cx + (int64_t)cy * (int64_t)cy;

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
        free(ve->defaults);
        free(ve->limits[0]);
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
    ve->defaults[P_DIRT_DRIVE]        = 620;
    ve->defaults[P_FLICKER_DRIVE]     = 560;

    ve->limits[0][P_SCRATCH_INTENSITY] = 0;   ve->limits[1][P_SCRATCH_INTENSITY] = 100;
    ve->limits[0][P_DUST_INTENSITY]    = 0;   ve->limits[1][P_DUST_INTENSITY]    = 100;
    ve->limits[0][P_FLICKER_INTENSITY] = 0;   ve->limits[1][P_FLICKER_INTENSITY] = 50;
    ve->limits[0][P_FLICKER_FREQUENCY] = 0;   ve->limits[1][P_FLICKER_FREQUENCY] = 500;
    ve->limits[0][P_GRAIN_STRENGTH]    = 0;   ve->limits[1][P_GRAIN_STRENGTH]    = 50;
    ve->limits[0][P_VIGNETTE_STRENGTH] = 0;   ve->limits[1][P_VIGNETTE_STRENGTH] = 100;
    ve->limits[0][P_SCRATCH_LIFESPAN]  = 0;   ve->limits[1][P_SCRATCH_LIFESPAN]  = 50;
    ve->limits[0][P_DIRT_DRIVE]        = 0;   ve->limits[1][P_DIRT_DRIVE]        = 1000;
    ve->limits[0][P_FLICKER_DRIVE]     = 0;   ve->limits[1][P_FLICKER_DRIVE]     = 1000;

    ve->description = "Vintage Film";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Scratch Intensity",
        "Dust Intensity",
        "Flicker Intensity",
        "Flicker Frequency",
        "Grain Strength",
        "Vignette Strength",
        "Scratch Lifespan",
        "Dirt Drive",
        "Flicker Drive"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      0,   100,  36,  86, 120, 1200, 0,   76,
        VJ_BEAT_DETAIL,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      0,   100,  76, 100,  45,  520, 0,  100,
        VJ_BEAT_GLOW,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      0,    50,  72, 100,  35,  420, 0,  100,
        VJ_BEAT_SPEED,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,  6,   420,  28,  78, 160, 1400, 0,   68,
        VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      0,    50,  44,  90, 100, 1000, 0,   78,
        VJ_BEAT_CONTRAST,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      0,   100,  74, 100,  50,  620, 0,   98,
        VJ_BEAT_MEMORY,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 1,    42,  12,  44, 900, 3600, 500, 38,
        VJ_BEAT_TURBULENCE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000,
        VJ_BEAT_GLOW,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      0,  1000,  88, 100,  24,  360, 0,  100
    );

    return ve;
}

void *vintagefilm_malloc(int w, int h)
{
    vintagefilm_t *vf = (vintagefilm_t *) vj_calloc(sizeof(vintagefilm_t));
    if(!vf)
        return NULL;

    const int len = w * h;
    const size_t scratch_bytes = (size_t)w;
    const size_t vignette_bytes = sizeof(uint16_t) * (size_t)len;
    const size_t total = scratch_bytes + vignette_bytes + 16u;

    vf->region = (uint8_t*) vj_malloc(total);
    if(!vf->region) {
        free(vf);
        return NULL;
    }

    vf->scratch_map = vf->region;
    uint8_t *p = vf->scratch_map + scratch_bytes;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    vf->vignette_lut = (uint16_t*)p;

    veejay_memset(vf->scratch_map, 0, scratch_bytes);

    vf->framecounter = 0;
    vf->rng_state = 123456789u;

    vf->scratch_env = 0.0f;
    vf->dust_env = 0.0f;
    vf->flicker_env = 0.0f;
    vf->flicker_freq_env = 0.0f;
    vf->grain_env = 0.0f;
    vf->vignette_env = 0.0f;
    vf->lifespan_env = 0.0f;
    vf->dirt_drive_env = 0.0f;
    vf->flicker_drive_env = 0.0f;
    vf->smooth_ready = 0;

    vf->width = w;
    vf->height = h;

    vf->n_threads = vje_advise_num_threads(len);

    vintagefilm_build_vignette(vf, w, h);

    return (void*) vf;
}

void vintagefilm_free(void *ptr)
{
    vintagefilm_t *vf = (vintagefilm_t*) ptr;

    free(vf->region);
    free(vf);
}

void vintagefilm_apply(void *ptr, VJFrame *frame, int *args)
{
    vintagefilm_t *vf = (vintagefilm_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;

    const int scratch_arg = args[P_SCRATCH_INTENSITY];
    const int dust_arg = args[P_DUST_INTENSITY];
    const int flicker_arg = args[P_FLICKER_INTENSITY];
    const int flicker_freq_arg = args[P_FLICKER_FREQUENCY];
    const int grain_arg = args[P_GRAIN_STRENGTH];
    const int vignette_arg = args[P_VIGNETTE_STRENGTH];
    const int lifespan_arg = args[P_SCRATCH_LIFESPAN];
    const int dirt_drive_arg = args[P_DIRT_DRIVE];
    const int flicker_drive_arg = args[P_FLICKER_DRIVE];

    const float fast = 0.245f;
    const float slow = 0.118f;

    if(!vf->smooth_ready) {
        vf->scratch_env = (float)scratch_arg;
        vf->dust_env = (float)dust_arg;
        vf->flicker_env = (float)flicker_arg;
        vf->flicker_freq_env = (float)flicker_freq_arg;
        vf->grain_env = (float)grain_arg;
        vf->vignette_env = (float)vignette_arg;
        vf->lifespan_env = (float)lifespan_arg;
        vf->dirt_drive_env = (float)dirt_drive_arg;
        vf->flicker_drive_env = (float)flicker_drive_arg;
        vf->smooth_ready = 1;
    } else {
        vf->scratch_env = vintagefilm_smoothf(vf->scratch_env, (float)scratch_arg, fast, slow);
        vf->dust_env = vintagefilm_smoothf(vf->dust_env, (float)dust_arg, fast, slow);
        vf->flicker_env = vintagefilm_smoothf(vf->flicker_env, (float)flicker_arg, fast * 1.16f, slow);
        vf->flicker_freq_env = vintagefilm_smoothf(vf->flicker_freq_env, (float)flicker_freq_arg, fast * 0.62f, slow);
        vf->grain_env = vintagefilm_smoothf(vf->grain_env, (float)grain_arg, fast * 1.05f, slow);
        vf->vignette_env = vintagefilm_smoothf(vf->vignette_env, (float)vignette_arg, fast * 0.62f, slow);
        vf->lifespan_env = vintagefilm_smoothf(vf->lifespan_env, (float)lifespan_arg, fast * 0.72f, slow);
        vf->dirt_drive_env = vintagefilm_smoothf(vf->dirt_drive_env, (float)dirt_drive_arg, fast * 1.22f, slow);
        vf->flicker_drive_env = vintagefilm_smoothf(vf->flicker_drive_env, (float)flicker_drive_arg, fast * 1.36f, slow);
    }

    int scratch_intensity = clampi((int)(vf->scratch_env + 0.5f), 0, 100);
    int dust_intensity    = clampi((int)(vf->dust_env + 0.5f), 0, 100);
    int flicker_intensity = clampi((int)(vf->flicker_env + 0.5f), 0, 50);
    const int flicker_freq = clampi((int)(vf->flicker_freq_env + 0.5f), 0, 500);
    int grain_strength    = clampi((int)(vf->grain_env + 0.5f), 0, 50);
    int vignette_strength = clampi((int)(vf->vignette_env + 0.5f), 0, 100);
    const int scratch_lifespan = clampi((int)(vf->lifespan_env + 0.5f), 0, 50);
    const int dirt_q = clampi((int)(vf->dirt_drive_env + 0.5f), 0, 1000);
    const int flicker_q = clampi((int)(vf->flicker_drive_env + 0.5f), 0, 1000);

    scratch_intensity = clampi(scratch_intensity + (dirt_q * 62 + 500) / 1000, 0, 100);
    dust_intensity    = clampi(dust_intensity    + (dirt_q * 72 + 500) / 1000, 0, 100);
    grain_strength    = clampi(grain_strength    + (dirt_q * 34 + 500) / 1000, 0, 50);
    vignette_strength = clampi(vignette_strength + (flicker_q * 24 + 500) / 1000, 0, 100);
    flicker_intensity = clampi(flicker_intensity + (flicker_q * 30 + 500) / 1000, 0, 50);

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
        int drive_mod = (flicker_q * 40 + 500) / 1000;
        const uint32_t drive_noise = vintagefilm_hash((uint32_t)vf->framecounter ^ 0x9e3779b9u);

        if(drive_noise & 1u)
            drive_mod = -(drive_mod >> 1);

        global_gain += (256 * drive_mod) / 100;
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
            num_scratches += (width * dirt_q + 8000) / 16000;

        if(num_scratches < 1 && (int)(vintagefilm_rng(&vf->rng_state) % 100u) < scratch_intensity)
            num_scratches = 1;

        if(num_scratches > (width >> 2))
            num_scratches = width >> 2;

        for(int i = 0; i < num_scratches; i++) {
            const int x = (int)(vintagefilm_rng(&vf->rng_state) % (uint32_t)width);
            int life = 1 + scratch_lifespan + (int)(vintagefilm_rng(&vf->rng_state) % 3u);

            if(dirt_q > 0)
                life += (dirt_q * 4 + 500) / 1000;

            scratch_map[x] = (uint8_t)clampi(life, 1, 255);
        }
    }

    const uint32_t frame_seed = vintagefilm_rng(&vf->rng_state);
    const int vignette_q8 = (vignette_strength * 256 + 50) / 100;
    const int dust_gate = dust_intensity * 5 + (dirt_q >> 2);
    const int scratch_cut = 46 - ((dirt_q * 18 + 500) / 1000);

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
                yv -= 30 + (int)(p_noise % 30u) + ((dirt_q * 10 + 500) / 1000);

            if(dust_intensity > 0 && (p_noise & 0x7fffu) < (uint32_t)dust_gate) {
                const int dust_span = 40 + ((dirt_q * 16 + 500) / 1000);
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
