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


typedef struct {
    int framecounter;
    uint8_t *scratch_map;
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

    int minVals[] = {0,0,0,0,0,0,0};
    int maxVals[] = {100,100,50,500,50,100,50};
    for(int i=0;i<ve->num_params;i++){
        ve->limits[0][i] = minVals[i];
        ve->limits[1][i] = maxVals[i];
    }

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
        "Scratch Length"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    return ve;
}

void* vintagefilm_malloc(int w, int h) {
    vintagefilm_t *vf = (vintagefilm_t *) vj_calloc(sizeof(vintagefilm_t));
    if(!vf)
        return NULL;
    vf->scratch_map = (uint8_t*) vj_calloc( sizeof(uint8_t) * w);
    return (void*) vf;
}

void vintagefilm_free(void *ptr) {
    if(!ptr) return;
    vintagefilm_t *vf = (vintagefilm_t*) ptr;
    free(vf->scratch_map);
    free(vf);
}

static inline uint8_t clamp(int v) {
    v = v & -(v >= 0);
    v = (v & -(v <= 255)) | (255 & -(v > 255));
    return (uint8_t)v;
}

static uint32_t rng_state = 123456789;
static inline uint32_t fast_rng() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

void vintagefilm_apply(void *ptr, VJFrame *frame, int *args) {
    const int scratchIntensity = args[0];   // 0..100
    const int dustIntensity    = args[1];   // 0..100
    const int flickerIntensity = args[2];   // 0..50
    const int flickerFreq      = args[3];   // 1..500
    const int grainStrength    = args[4];   // 0..50
    const int vignetteStrength = args[5];   // 0..100
    const int scratchLength    = args[6];   // 0..50

    vintagefilm_t *vf = (vintagefilm_t*) ptr;
    int global_gain = 256;

    const int width  = frame->width;
    const int height = frame->height;
    const int len = width * height;
    const int cx = width / 2;
    const int cy = height / 2;
    //const float maxDist = sqrtf((float)(cx*cx + cy*cy));
    const float max_dist_sq = (float)(cx*cx + cy*cy);

    const int vig_scalar = (vignetteStrength > 0) ? 
        (int)((vignetteStrength / 100.0f) * (256.0f / max_dist_sq) * 65536.0f) : 0;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];
    uint8_t *restrict scratch_map = vf->scratch_map;

    vf->framecounter++;
    if(flickerIntensity > 0 && flickerFreq > 0 && (vf->framecounter % flickerFreq) == 0) {
        int flick_mod = (fast_rng() % (flickerIntensity * 2 + 1)) - flickerIntensity;
        global_gain = (256 * (100 + flick_mod)) / 100;
    }

    if (scratchIntensity > 0) {
        int num_scratches = (width * scratchIntensity) / 2000;
        if (num_scratches < 1 && scratchIntensity > 0) num_scratches = 1;
        
        for(int i=0; i < num_scratches; i++) {
            int x = fast_rng() % width;
            scratch_map[x] = 1;
        }
    }

    for(int y=0; y<height; y++) {
        int dy = y - cy;
        int dy2 = dy * dy;
        
        for(int x=0; x<width; x++) {
            int index = y * width + x;
            int dx = x - cx;
            
            int final_gain = global_gain;
            
            if (vig_scalar > 0) {
                int d2 = dx*dx + dy2;
                int vig_reduction = (int)(((int64_t)d2 * vig_scalar) >> 16);
                final_gain -= vig_reduction;
            }
            if (final_gain < 0) final_gain = 0;

            int Y = srcY[index];
            int U = srcU[index];
            int V = srcV[index];
            
            uint32_t r = fast_rng();

            if (scratch_map[x]) {
                if ((r & 0xFF) > 50) { 
                    Y = clamp(Y - 40 + (r % 20));
                }
            }

            if (dustIntensity > 0) {
                if ((r & 0xFFFF) < (dustIntensity * 10)) {
                    int dust = ((r >> 16) % 60) - 30;
                    Y = clamp(Y + dust);
                }
            }

            if (grainStrength > 0) {
                int noise = ((int)((r >> 8) % (2 * grainStrength + 1))) - grainStrength;
                Y += noise;
            }

            Y = (Y * final_gain) >> 8;
            
            // U = 128 + ((U - 128) * final_gain >> 8);
            // V = 128 + ((V - 128) * final_gain >> 8);

            srcY[index] = clamp(Y);
            // srcU[index] = clamp(U); // Uncomment if modifying chroma
            // srcV[index] = clamp(V); // Uncomment if modifying chroma
        }
    }

}
