/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "chromapalette.h"

typedef struct {
    int n_threads;
    int softness;
    int tolerance;
    float *lut;
} chromapalette_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *chromapalette_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 7;
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

    ve->limits[0][0] = 1; ve->limits[1][0] = 255; ve->defaults[0] = 60;

    for(int i = 1; i < 6; i++) {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 255;
    }

    ve->limits[0][6] = 0; ve->limits[1][6] = 255; ve->defaults[6] = 20;

    ve->defaults[1] = 255;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->defaults[4] = 200;
    ve->defaults[5] = 20;

    ve->description = "Chrominance Palette (rgb key)";
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Tolerance", "Red", "Green", "Blue", "Chroma Blue", "Chroma Red", "Softness");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,      VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE, 18,                 170,                4,  14, 3400, 8800, 2400, 20,
        VJ_BEAT_SELECTOR,    VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR,    VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR,    VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS,                                                  48,                 250,                12, 48,  900, 3000, 0,    68,
        VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS,                                                  16,                 238,                12, 48,  900, 3000, 0,    68,
        VJ_BEAT_DETAIL,      VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE, 4,                  128,                4,  14, 3400, 8800, 2400, 18
    );

    return ve;
}

void *chromapalette_malloc(int w, int h)
{
    chromapalette_t *c = (chromapalette_t*) vj_calloc(sizeof(chromapalette_t));

    if(!c)
        return NULL;

    c->n_threads = vje_advise_num_threads(w * h);
    c->softness = -1;
    c->tolerance = -1;
    c->lut = (float*) vj_malloc(sizeof(float) * 512 * 512);

    if(!c->lut) {
        free(c);
        return NULL;
    }

    return c;
}

void chromapalette_free(void *ptr)
{
    chromapalette_t *c = (chromapalette_t*) ptr;

    if(!c)
        return;

    if(c->lut)
        free(c->lut);

    free(c);
}

static void calc_lut(chromapalette_t *c, int tolerance, int softness)
{
    const float outer_r = (float)tolerance;
    float inner_r = outer_r - (float)softness;

    if(inner_r < 0.0f)
        inner_r = 0.0f;

    const float range = outer_r - inner_r;
    const float inv_range = 1.0f / (range > 0.1f ? range : 0.1f);

    for(int dv = -255; dv <= 255; dv++)
    {
        const int dv2 = dv * dv;
        float *restrict row = c->lut + (dv + 255) * 512 + 255;

        for(int du = -255; du <= 255; du++)
        {
            const float dist = sqrtf((float)(du * du + dv2));
            float blend = 0.0f;

            if(dist < inner_r)
                blend = 1.0f;
            else if(dist < outer_r)
                blend = (outer_r - dist) * inv_range;

            row[du] = blend;
        }
    }
}

void chromapalette_apply(void *ptr, VJFrame *frame, int *args)
{
    chromapalette_t *c = (chromapalette_t*) ptr;

    uint8_t lut_cb[256];
    uint8_t lut_cr[256];

    const int tolerance = args[0];
    const int r = args[1];
    const int g = args[2];
    const int b = args[3];
    const int color_cb = args[4];
    const int color_cr = args[5];
    const int softness = args[6];
    const int len = frame->len;
    const int n_threads = c->n_threads;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int target_y = 0;
    int target_u = 128;
    int target_v = 128;

    _rgb2yuv(r, g, b, target_y, target_u, target_v);

    target_u = clampi(target_u, 0, 255);
    target_v = clampi(target_v, 0, 255);

    if(softness != c->softness || tolerance != c->tolerance) {
        calc_lut(c, tolerance, softness);
        c->softness = softness;
        c->tolerance = tolerance;
    }

    const float *restrict lut = c->lut;

    for(int i = 0; i < 256; i++) {
        lut_cb[i] = CLAMP_UV(128 + (int)(((float)(color_cb - i) * 0.492f) + 0.5f));
        lut_cr[i] = CLAMP_UV(128 + (int)(((float)(color_cr - i) * 0.877f) + 0.5f));
    }

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
    {
        const int du_idx = (int)Cb[i] - target_u + 255;
        const int dv_idx = (int)Cr[i] - target_v + 255;
        const float blend = lut[dv_idx * 512 + du_idx];

        if(blend > 0.0f)
        {
            const int target_cb = lut_cb[Y[i]];
            const int target_cr = lut_cr[Y[i]];

            Cb[i] = CLAMP_UV((int)Cb[i] + (int)(blend * ((float)target_cb - (float)Cb[i])));
            Cr[i] = CLAMP_UV((int)Cr[i] + (int)(blend * ((float)target_cr - (float)Cr[i])));
        }
    }
}
