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

typedef struct {
    int framecounter;
    uint8_t *scratch_map;
    uint16_t *vignette_lut;
    uint32_t rng_state;
    int n_threads;
    int width;
    int height;
} vintagefilm_t;

static inline int vintagefilm_clampi(int v, int lo, int hi)
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
    ve->num_params = 7;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 10;
    ve->defaults[1] = 20;
    ve->defaults[2] = 5;
    ve->defaults[3] = 50;
    ve->defaults[4] = 5;
    ve->defaults[5] = 10;
    ve->defaults[6] = 5;

    ve->limits[0][0] = 0;   ve->limits[1][0] = 100;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 100;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 50;
    ve->limits[0][3] = 0;   ve->limits[1][3] = 500;
    ve->limits[0][4] = 0;   ve->limits[1][4] = 50;
    ve->limits[0][5] = 0;   ve->limits[1][5] = 100;
    ve->limits[0][6] = 0;   ve->limits[1][6] = 50;

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
        "Scratch Lifespan"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS,                             0,                  75,                 10, 38, 1000, 2600, 0,   58,    /* Scratch Intensity */
        VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS,                             0,                  80,                 10, 38, 1000, 2600, 0,   55,    /* Dust Intensity */
        VJ_BEAT_INTENSITY,  VJ_BEAT_F_CONTINUOUS,                             0,                  34,                 10, 38, 1000, 2600, 0,   60,    /* Flicker Intensity */
        VJ_BEAT_SPEED,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,        4,                  160,                6,  22, 1800, 4200, 900, 30,    /* Flicker Frequency */
        VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS,                             0,                  36,                 10, 38, 1000, 2600, 0,   58,    /* Grain Strength */
        VJ_BEAT_CONTRAST,   VJ_BEAT_F_CONTINUOUS,                             0,                  70,                 8,  30, 1200, 3000, 0,   45,    /* Vignette Strength */
        VJ_BEAT_MEMORY,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,        1,                  36,                 6,  22, 1800, 4200, 900, 30     /* Scratch Lifespan */
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
    vf->width = w;
    vf->height = h;

    vf->n_threads = vje_advise_num_threads(len);

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

    const int scratch_intensity = vintagefilm_clampi(args[0], 0, 100);
    const int dust_intensity    = vintagefilm_clampi(args[1], 0, 100);
    const int flicker_intensity = vintagefilm_clampi(args[2], 0, 50);
    const int flicker_freq      = vintagefilm_clampi(args[3], 0, 500);
    const int grain_strength    = vintagefilm_clampi(args[4], 0, 50);
    const int vignette_strength = vintagefilm_clampi(args[5], 0, 100);
    const int scratch_lifespan  = vintagefilm_clampi(args[6], 0, 50);

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

#pragma omp parallel for schedule(static) num_threads(vf->n_threads)
    for(int x = 0; x < width; x++) {
        if(scratch_map[x] > 0)
            scratch_map[x]--;
    }

    if(scratch_intensity > 0) {
        int num_scratches = (width * scratch_intensity) / 5000;

        if(num_scratches < 1 && (int)(vintagefilm_rng(&vf->rng_state) % 100u) < scratch_intensity)
            num_scratches = 1;

        for(int i = 0; i < num_scratches; i++) {
            const int x = (int)(vintagefilm_rng(&vf->rng_state) % (uint32_t)width);
            const int life = 1 + scratch_lifespan + (int)(vintagefilm_rng(&vf->rng_state) % 3u);

            scratch_map[x] = (uint8_t)vintagefilm_clampi(life, 1, 255);
        }
    }

    const uint32_t frame_seed = vintagefilm_rng(&vf->rng_state);
    const int vignette_q8 = (vignette_strength * 256 + 50) / 100;

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

            if(scratch_map[x] > 0 && ((p_noise & 0xffu) > 50u))
                yv -= 30 + (int)(p_noise % 30u);

            if(dust_intensity > 0 && (p_noise & 0x7fffu) < (uint32_t)(dust_intensity * 5)) {
                const int dust = (int)((p_noise >> 16) % 80u) - 40;

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