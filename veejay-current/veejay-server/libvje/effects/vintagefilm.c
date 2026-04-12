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
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>
#include "vintagefilm.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int framecounter;
    uint8_t *scratch_map;
    uint32_t rng_state;
    int n_threads;
} vintagefilm_t;

vj_effect *vintagefilm_init(int w, int h) {
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 10;
    ve->defaults[1] = 20;
    ve->defaults[2] = 5;
    ve->defaults[3] = 50;
    ve->defaults[4] = 5;
    ve->defaults[5] = 10;
    ve->defaults[6] = 5;

    int minVals[] = {0, 0, 0, 0, 0, 0, 0};
    int maxVals[] = {100, 100, 50, 500, 50, 100, 50};
    for(int i = 0; i < ve->num_params; i++){
        ve->limits[0][i] = minVals[i];
        ve->limits[1][i] = maxVals[i];
    }

    ve->description = "Vintage Film";
    ve->extra_frame = 0;
    ve->sub_format = 1;

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

    ve->hints = vje_init_value_hint_list(ve->num_params);
    return ve;
}

void* vintagefilm_malloc(int w, int h) {
    vintagefilm_t *vf = (vintagefilm_t *) vj_calloc(sizeof(vintagefilm_t));
    if(!vf) return NULL;

    vf->scratch_map = (uint8_t*) vj_calloc(sizeof(uint8_t) * w);
    if(!vf->scratch_map) {
        free(vf);
        return NULL;
    }

    vf->rng_state = 123456789;
    vf->n_threads = vje_advise_num_threads(w * h);
    return (void*) vf;
}

void vintagefilm_free(void *ptr) {
    if(!ptr) return;
    vintagefilm_t *vf = (vintagefilm_t*) ptr;
    if(vf) {
        if(vf->scratch_map)
            free(vf->scratch_map);

        free(vf);
    }
}

static inline uint8_t clamp(int v) {
    v = v & -(v >= 0);
    v = (v & -(v <= 255)) | (255 & -(v > 255));
    return (uint8_t)v;
}

static inline uint32_t fast_rng(uint32_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static inline uint32_t pcg_hash(uint32_t input) {
    uint32_t state = input * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

void vintagefilm_apply(void *ptr, VJFrame *frame, int *args) {
    const int scratchIntensity = args[0];
    const int dustIntensity    = args[1];
    const int flickerIntensity = args[2];
    const int flickerFreq      = args[3];
    const int grainStrength    = args[4];
    const int vignetteStrength = args[5];
    const int scratchLength    = args[6];

    vintagefilm_t *vf = (vintagefilm_t*) ptr;
    const int n_threads = vf->n_threads;
    int global_gain = 256;

    const int width  = frame->width;
    const int height = frame->height;
    const int cx = width / 2;
    const int cy = height / 2;
    const float max_dist_sq = (float)(cx*cx + cy*cy);

    const int vig_scalar = (vignetteStrength > 0) ? (int)((vignetteStrength / 100.0f) * (256.0f / max_dist_sq) * 65536.0f) : 0;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];
    uint8_t *restrict scratch_map = vf->scratch_map;

    vf->framecounter++;

    if(flickerIntensity > 0 && flickerFreq > 0 && (vf->framecounter % flickerFreq) == 0) {
        int flick_mod = (fast_rng(&vf->rng_state) % (flickerIntensity * 2 + 1)) - flickerIntensity;
        global_gain = (256 * (100 + flick_mod)) / 100;
    }

    for (int x = 0; x < width; x++) {
        if (scratch_map[x] > 0) scratch_map[x]--;
    }

    if (scratchIntensity > 0) {
        int num_scratches = (width * scratchIntensity) / 5000;
        if (num_scratches < 1 && (fast_rng(&vf->rng_state) % 100) < scratchIntensity) {
            num_scratches = 1;
        }
        for(int i = 0; i < num_scratches; i++) {
            int x = fast_rng(&vf->rng_state) % width;
            scratch_map[x] = scratchLength + (fast_rng(&vf->rng_state) % 3);
        }
    }

    uint32_t frame_seed = fast_rng(&vf->rng_state);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < height; y++) {
        int dy = y - cy;
        int dy2 = dy * dy;
        
        uint32_t line_noise = pcg_hash(frame_seed ^ (uint32_t)y);

        for(int x = 0; x < width; x++) {
            int index = y * width + x;
            int dx = x - cx;
            
            int final_gain = global_gain;

            if (vig_scalar > 0) {
                int d2 = dx * dx + dy2;
                int vig_reduction = (int)(((int64_t)d2 * vig_scalar) >> 16);
                final_gain -= vig_reduction;
            }
            if (final_gain < 0) final_gain = 0;

            int Y = srcY[index];

            uint32_t p_noise = pcg_hash(line_noise ^ (uint32_t)(x * 0x1234567));

            if (scratch_map[x] > 0) {
                if ((p_noise & 0xFF) > 50) {
                    Y -= (30 + (p_noise % 30));
                }
            }

            if (dustIntensity > 0 && (p_noise & 0x7FFF) < (uint32_t)(dustIntensity * 5)) {
                int dust = ((p_noise >> 16) % 80) - 40;
                Y += dust;
            }

            if (grainStrength > 0) {
                int noise = ((int)((p_noise >> 8) % (2 * grainStrength + 1))) - grainStrength;
                Y += noise;
            }

            Y = (Y * final_gain) >> 8;
            srcY[index] = clamp(Y);

            int U = srcU[index];
            int V = srcV[index];
            
            U = (U * 4 + 110 * 6) / 10;
            V = (V * 4 + 150 * 6) / 10;

            srcU[index] = clamp(128 + (((U - 128) * final_gain) >> 8));
            srcV[index] = clamp(128 + (((V - 128) * final_gain) >> 8));
        }
    }
}
