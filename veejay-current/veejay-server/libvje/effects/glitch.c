/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "glitch.h"

vj_effect *glitch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 360;
    ve->defaults[0] = 20;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 10;
    ve->defaults[1] = 2;

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 100;
    ve->defaults[2] = 2;

    ve->limits[0][3] = 1;
    ve->limits[1][3] = 200;
    ve->defaults[3] = 100;

    ve->limits[0][4] = 1;
    ve->limits[1][4] = 500;
    ve->defaults[4] = 20;

    ve->limits[0][5] = -100;
    ve->limits[1][5] = 100;
    ve->defaults[5] = 2;

    ve->limits[0][6] = -100;
    ve->limits[1][6] = 100;
    ve->defaults[6] = 2;

    ve->limits[0][7] = 0;
    ve->limits[1][7] = 500;
    ve->defaults[7] = 5;

    ve->description = "Glitch";

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Amplitude",
        "Noise Strength",
        "Noise Quantity",
        "Noise Scale",
        "Interval",
        "Distortion X",
        "Distortion Y",
        "Duration"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_KICK,         VJ_BEAT_F_CONTINUOUS,                                                8,                  190,                14, 58, 90,   720,  0,   84,    /* Amplitude */
        VJ_BEAT_HAT,          VJ_BEAT_F_CONTINUOUS,                                                1,                  9,                  4,  26, 80,   620,  0,   52,    /* Noise Strength */
        VJ_BEAT_DETAIL,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                          1,                  48,                 6,  22, 1800, 4200, 900, 30,    /* Noise Quantity */
        VJ_BEAT_HAT,          VJ_BEAT_F_CONTINUOUS,                                                20,                 190,                4,  26, 80,   620,  0,   48,    /* Noise Scale */
        VJ_BEAT_SPEED,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                          4,                  160,                6,  22, 1800, 4200, 900, 30,    /* Interval */
        VJ_BEAT_SNARE,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, -70,                70,                 10, 42, 120,  900,  0,   72,    /* Distortion X */
        VJ_BEAT_SNARE,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, -70,                70,                 10, 42, 120,  900,  0,   72,    /* Distortion Y */
        VJ_BEAT_SPEED,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                          2,                  96,                 6,  22, 1800, 4200, 900, 30     /* Duration */
    );
    return ve;
}

typedef struct
{
    uint8_t *buf[3];
    int     *rand_lut;
    int     *lsfr_lut;
    int      count;
    int      frameCount;
} glitch_t;

static inline int glitch_wrap_once(int v, int max)
{
    v = (v < 0) ? v + max : v;
    v = (v >= max) ? v - max : v;
    return v;
}

void *glitch_malloc(int w, int h)
{
    const int len = w * h;

    glitch_t *g = (glitch_t *) vj_calloc(sizeof(glitch_t));
    if(!g)
        return NULL;

    g->buf[0] = (uint8_t *) vj_malloc(sizeof(uint8_t) * len * 3);
    if(!g->buf[0]) {
        free(g);
        return NULL;
    }

    g->buf[1] = g->buf[0] + len;
    g->buf[2] = g->buf[1] + len;

    g->rand_lut = (int *) vj_malloc(sizeof(int) * len * 2);
    if(!g->rand_lut) {
        free(g->buf[0]);
        free(g);
        return NULL;
    }

    g->lsfr_lut = g->rand_lut + len;
    g->count = 0;
    g->frameCount = 0;

    for(int i = 0; i < len; i++) {
        g->rand_lut[i] = rand();
    }

    veejay_memcpy(g->lsfr_lut, g->rand_lut, sizeof(int) * len);

    return (void *) g;
}

void glitch_free(void *ptr)
{
    glitch_t *g = (glitch_t *) ptr;
    if(!g)
        return;

    if(g->buf[0])
        free(g->buf[0]);

    if(g->rand_lut)
        free(g->rand_lut);

    free(g);
}

void glitch_apply(void *ptr, VJFrame *frame, int *args)
{
    glitch_t *g = (glitch_t *) ptr;;

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    uint8_t *bY = g->buf[0];
    uint8_t *bU = g->buf[1];
    uint8_t *bV = g->buf[2];

    int *rand_lut = g->rand_lut;
    int *lsfr_lut = g->lsfr_lut;

    const int masterAmplitude = args[0];
    const int noiseStrength = args[1];
    const int noiseQuantity = (args[2] > 0) ? args[2] : 1;
    const int noiseScale = args[3];
    const int interval = (args[4] > 0) ? args[4] : 1;
    const int geometryDistortionX = args[5];
    const int geometryDistortionY = args[6];
    const int randinterval = (args[7] > 0) ? args[7] : 0;

    const float nS = 0.01f * (float) noiseScale;

    if(g->count == 0 && randinterval > 0) {
        veejay_memcpy(lsfr_lut, rand_lut, sizeof(int) * len);
    }

    if(randinterval > 0) {
        g->count = (g->count + 1) % randinterval;
    } else {
        g->count = 0;
    }

    g->frameCount = (g->frameCount + 1) % interval;

    if(randinterval > 0 && g->frameCount > randinterval) {
        return;
    }

    for(int i = 0; i < len; i++) {
        const int q = lsfr_lut[i] % noiseQuantity;
        const int centered = q - (noiseQuantity >> 1);
        const int noise = (int) ((float) (centered * noiseStrength) * nS);
        bY[i] = CLAMP_Y(Y[i] + masterAmplitude * noise);
    }

    if(randinterval > 0) {
        for(int i = 0; i < len; i++) {
            lsfr_lut[i] = fastrand(lsfr_lut[i]);
        }
    }

    int distortionX = (geometryDistortionX * width) / 100;
    int distortionY = (geometryDistortionY * height) / 100;

    if(distortionX <= -width || distortionX >= width) {
        distortionX %= width;
    }

    if(distortionY <= -height || distortionY >= height) {
        distortionY %= height;
    }

    veejay_memcpy(bU, U, len);
    veejay_memcpy(bV, V, len);

    for(int y = 0; y < height; y++) {
        const int ny = glitch_wrap_once(y + distortionY, height);
        const int row = y * width;
        const int drow = ny * width;

        for(int x = 0; x < width; x++) {
            const int nx = glitch_wrap_once(x + distortionX, width);
            const int src = row + x;
            const int dst = drow + nx;

            const unsigned int alpha = Y[dst];

            U[src] = (uint8_t) (((alpha * U[src]) + ((0xff - alpha) * bU[dst])) >> 8);
            V[src] = (uint8_t) (((alpha * V[src]) + ((0xff - alpha) * bV[dst])) >> 8);
            Y[src] = (uint8_t) ((Y[src] + bY[dst]) >> 1);
        }
    }
}