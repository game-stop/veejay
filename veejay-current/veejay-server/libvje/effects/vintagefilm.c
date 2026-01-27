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


typedef struct {
    int framecounter;
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
    return (void*) vf;
}

void vintagefilm_free(void *ptr) {
    if(!ptr) return;
    free(ptr);
}


static inline float qfast_sqrt_err(float x) {
    union { float f; uint32_t i; } conv;
    conv.f = x;
    conv.i  = (1U << 29) + (conv.i >> 1) - (1U << 22);

	// for more precision but we dont need
	//     conv.f  = 0.5f * (conv.f + x / conv.f);
    return conv.f;
}

static inline uint8_t clamp(int v) {
    v = v & -(v >= 0);
    v = (v & -(v <= 255)) | (255 & -(v > 255));
    return (uint8_t)v;
}



void vintagefilm_apply(void *ptr, VJFrame *frame, int *args) {
    const int scratchPercent   = args[0];   // 0..100
    const int dustPercent      = args[1];   // 0..100
    const int flickerPercent   = args[2];   // 0..50
    const int flickerFreq      = args[3];   // 1..500
    const int grainStrength    = args[4];   // 0..50
    const int vignetteStrength = args[5];   // 0..100
    const int scratchLength    = args[6];   // 0..50

    vintagefilm_t *vf = (vintagefilm_t*) ptr;
    int (*rnd)(int) = fastrand;

    const int width  = frame->width;
    const int height = frame->height;
    const int cx = width / 2;
    const int cy = height / 2;
    const float maxDist = sqrtf((float)(cx*cx + cy*cy));

    uint8_t *srcY = frame->data[0];
    uint8_t *srcU = frame->data[1];
    uint8_t *srcV = frame->data[2];

    for(int y=0; y<height; y++) {
        int dy = y - cy;
        for(int x=0; x<width; x++) {
            int index = y * width + x;
            int dx = x - cx;

            float dist = qfast_sqrt_err((float)(dx*dx + dy*dy));
            float vignetteFactor = 1.0f;
            if(vignetteStrength > 0)
                vignetteFactor = 1.0f - (dist / maxDist) * (vignetteStrength / 100.0f);

            float U = (float)srcU[index] - 128.0f;
            float V = (float)srcV[index] - 128.0f;

            int rndVal = rnd(index);

			if(scratchPercent > 0) {
                int scratchMask = (rndVal & 0xFF) < scratchPercent*255/100;
                if(scratchMask && scratchLength > 0 && (y % (scratchLength+1)) == 0) {
                    int scratchNoise = ((rndVal >> 8) % 40 - 20);
                    srcY[index] = clamp(srcY[index] + scratchNoise);
                }
            }

			int dustMask = ((rndVal >> 16) & 0xFF) < dustPercent*255/100;
            if(dustMask) {
                int dustNoise = ((int)((rndVal >> 24) % 30 - 15) * dustPercent / 50);
                srcY[index] = clamp(srcY[index] + dustNoise);
            }
 
            int grainNoise = ((rndVal >> 8) % (2*grainStrength+1)) - grainStrength;
            srcY[index] = clamp(srcY[index] + grainNoise);
            U += grainNoise * 0.25f;
            V += grainNoise * 0.25f;

            srcY[index] = clamp((int)(srcY[index] * vignetteFactor));
            srcU[index] = clamp((int)(U * vignetteFactor + 128.0f));
            srcV[index] = clamp((int)(V * vignetteFactor + 128.0f));
        }
    }

    vf->framecounter++;
    if(flickerPercent > 0 && flickerFreq > 0 && (vf->framecounter % flickerFreq) == 0) {
        int flick = 100 + (rnd(0) % (flickerPercent*2+1) - flickerPercent);
        for(int i=0; i<frame->len; i++){
            srcY[i] = clamp(srcY[i] * flick / 100);
        }
    }
}
