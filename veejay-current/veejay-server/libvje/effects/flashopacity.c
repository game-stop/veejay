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
#include "flashopacity.h"

#include <math.h>
#include <stdint.h>

#define TABLE_SIZE 256

typedef struct {
    int currentFrame;
    int last_exposure;
    int n_threads;
    uint16_t explut[TABLE_SIZE];
} flash_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t flashopacity_u8(int v)
{
    return (uint8_t) clampi(v, 0, 255);
}

static inline uint8_t flashopacity_blend255(uint8_t a, uint8_t b, int opacity)
{
    const int inv = 255 - opacity;
    const int x = (int)a * inv + (int)b * opacity;
    return (uint8_t)(((x + 1) + (x >> 8)) >> 8);
}

static void flashopacity_build_lut(flash_t *f, int exposure)
{
    const float exposureValue = (float)exposure * 0.01f;

    for(int i = 0; i < TABLE_SIZE; i++) {
        const float t = (float)i * (1.0f / (float)(TABLE_SIZE - 1));
        int v = (int)(powf(2.0f, t * exposureValue) * 256.0f + 0.5f);

        if(v < 256)
            v = 256;
        else if(v > 1024)
            v = 1024;

        f->explut[i] = (uint16_t)v;
    }

    f->last_exposure = exposure;
}

vj_effect *flashopacity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 5;
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

    ve->limits[0][0] = 1; ve->limits[1][0] = 100; ve->defaults[0] = 5;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 100;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255; ve->defaults[2] = 255;
    ve->limits[0][3] = 1; ve->limits[1][3] = 500; ve->defaults[3] = 10;
    ve->limits[0][4] = 0; ve->limits[1][4] = 1;   ve->defaults[4] = 0;

    ve->description = "Flash Opacity";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Exposure", "Start Opacity", "End Opacity", "Interval", "Mode");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_KICK,             VJ_BEAT_F_IMPULSE | VJ_BEAT_F_NO_ZERO_CROSS,                                    3,                  92,                 86, 100, 1,    360,  48,   190,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS,                                                            0,                  180,                12, 46, 1000, 3600, 0,    58,
        VJ_BEAT_SOURCE_MIX,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                  140,                255,                16, 62,  700, 2800, 0,    84,
        VJ_BEAT_SPEED,            VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 4, 96, 4, 14, 3000, 8200, 2200, 22,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                         VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *flashopacity_malloc(int w, int h)
{
    flash_t *f = (flash_t*) vj_calloc(sizeof(flash_t));

    if(!f)
        return NULL;

    f->currentFrame = 0;
    f->last_exposure = -1;
    f->n_threads = vje_advise_num_threads(w * h);

    return f;
}

void flashopacity_free(void *ptr)
{
    free(ptr);
}

void flashopacity_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    flash_t *f = (flash_t*) ptr;

    const int len = frame->len;
    const int uv_len = frame->uv_len;
    const int exposure = args[0];
    const int opacityStart = args[1];
    const int opacityEnd = args[2];
    const int interval = args[3];
    const int mode = args[4];

    if(f->last_exposure != exposure)
        flashopacity_build_lut(f, exposure);

    int currentFrame = f->currentFrame;

    if(currentFrame < 0 || currentFrame >= interval)
        currentFrame %= interval;
    if(currentFrame < 0)
        currentFrame = 0;

    const int hInterval = interval >> 1;
    const int rising = hInterval > 0 && currentFrame < hInterval;
    int fp_multiplier = 256;
    int lerp = 0;
    int opacity = opacityEnd;

    if(rising) {
        const int index = (currentFrame * (TABLE_SIZE - 1) + (hInterval >> 1)) / hInterval;
        fp_multiplier = f->explut[index];
        if(mode == 1)
            lerp = (currentFrame * 255 + (hInterval >> 1)) / hInterval;
    }
    else {
        const int denom = interval - hInterval;
        const int t = currentFrame - hInterval;

        if(denom <= 1)
            opacity = opacityEnd;
        else
            opacity = opacityStart + (t * (opacityEnd - opacityStart) + ((opacityEnd >= opacityStart) ? (denom >> 1) : -(denom >> 1))) / (denom - 1);

        opacity = clampi(opacity, 0, 255);
    }

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict U2 = frame2->data[1];
    const uint8_t *restrict V2 = frame2->data[2];

#pragma omp parallel num_threads(f->n_threads)
    {
        if(rising) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                int v = ((int)Y[i] * fp_multiplier) >> 8;
                Y[i] = flashopacity_u8(v);
            }

            if(mode == 1) {
#pragma omp for schedule(static)
                for(int i = 0; i < uv_len; i++) {
                    U[i] = flashopacity_blend255(U[i], U2[i], lerp);
                    V[i] = flashopacity_blend255(V[i], V2[i], lerp);
                }
            }
        }
        else {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++)
                Y[i] = flashopacity_blend255(Y[i], Y2[i], opacity);

#pragma omp for schedule(static)
            for(int i = 0; i < uv_len; i++) {
                U[i] = flashopacity_blend255(U[i], U2[i], opacity);
                V[i] = flashopacity_blend255(V[i], V2[i], opacity);
            }
        }
    }

    f->currentFrame = currentFrame + 1;
    if(f->currentFrame >= interval)
        f->currentFrame = 0;
}


