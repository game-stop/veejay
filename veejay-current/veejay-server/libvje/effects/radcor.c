/* 
 * Linux VeeJay
 *
 * Copyright(C)2007 Niels Elburg <nwelburg@gmail>
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

/* Radial Distortion Correction
 * http://local.wasp.uwa.edu.au/~pbourke/projection/lenscorrection/
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "radcor.h"
#include <math.h>
#include <stdint.h>

#define RADCOR_INVALID 0xffffffffU

vj_effect *radcor_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 1000;
    ve->defaults[0] = 10;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 1000;
    ve->defaults[1] = 40;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[2] = 0;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->defaults[3] = 0;

    ve->description = "Lens correction";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->alpha = FLAG_ALPHA_OPTIONAL | FLAG_ALPHA_OUT;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Alpha X",
        "Alpha Y",
        "Direction",
        "Update Alpha"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 20,                 520,                6, 22, 1800, 4200, 900, 30,    /* Alpha X */
        VJ_BEAT_WARP,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 20,                 520,                6, 22, 1800, 4200, 900, 30,    /* Alpha Y */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Direction */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Update Alpha */
    );

    (void) w;
    (void) h;

    return ve;
}

typedef struct {
    uint8_t *badbuf;
    uint32_t *Map;
    int map_upd[3];
    float *x_lut;
} radcor_t;

void *radcor_malloc(int w, int h)
{
    radcor_t *r = (radcor_t*) vj_calloc(sizeof(radcor_t));
    if(!r)
        return NULL;

    const int len = w * h;
    const int total_len = len * 4;

    r->badbuf = (uint8_t*) vj_malloc(sizeof(uint8_t) * total_len);
    if(!r->badbuf) {
        free(r);
        return NULL;
    }

    r->Map = (uint32_t*) vj_malloc(sizeof(uint32_t) * len);
    if(!r->Map) {
        free(r->badbuf);
        free(r);
        return NULL;
    }

    r->x_lut = (float*) vj_malloc(sizeof(float) * w);
    if(!r->x_lut) {
        free(r->badbuf);
        free(r->Map);
        free(r);
        return NULL;
    }

    for(int i = 0; i < len; i++)
        r->Map[i] = RADCOR_INVALID;

    r->map_upd[0] = -1;
    r->map_upd[1] = -1;
    r->map_upd[2] = -1;

    return (void*) r;
}

void radcor_free(void *ptr)
{
    radcor_t *r = (radcor_t*) ptr;
    if(!r)
        return;

    if(r->badbuf)
        free(r->badbuf);
    if(r->Map)
        free(r->Map);
    if(r->x_lut)
        free(r->x_lut);

    free(r);
}

static inline int radcor_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static int radcor_build_map(radcor_t *radcor, int width, int height, int alpaX, int alpaY, int dir)
{
    const float inv_w = 1.0f / (float)width;
    const float inv_h = 1.0f / (float)height;
    const float half_w = 0.5f * (float)width;
    const float half_h = 0.5f * (float)height;

    float ax = (float)alpaX * 0.001f;
    float ay = (float)alpaY * 0.001f;

    uint32_t *restrict Map = radcor->Map;
    float *restrict x_lut = radcor->x_lut;

    if(!dir) {
        ax = -ax;
        ay = -ay;
    }

    for(int j = 0; j < width; j++)
        x_lut[j] = ((2.0f * (float)j) - (float)width) * inv_w;

    for(int i = 0; i < height; i++) {
        const float y = ((2.0f * (float)i) - (float)height) * inv_h;

        for(int j = 0; j < width; j++) {
            const float x = x_lut[j];
            const float r = x * x + y * y;

            const float d1x = 1.0f - ax * r;
            const float d1y = 1.0f - ay * r;

            const int pos = i * width + j;

            if(fabsf(d1x) < 0.000001f || fabsf(d1y) < 0.000001f) {
                Map[pos] = RADCOR_INVALID;
                continue;
            }

            const float x3 = x / d1x;
            const float y3 = y / d1y;
            const float r2 = x3 * x3 + y3 * y3;

            const float d2x = 1.0f - ax * r2;
            const float d2y = 1.0f - ay * r2;

            if(fabsf(d2x) < 0.000001f || fabsf(d2y) < 0.000001f) {
                Map[pos] = RADCOR_INVALID;
                continue;
            }

            const float x2 = x / d2x;
            const float y2 = y / d2y;

            const int i2 = (int)((y2 + 1.0f) * half_h);
            const int j2 = (int)((x2 + 1.0f) * half_w);

            if((unsigned)i2 < (unsigned)height && (unsigned)j2 < (unsigned)width)
                Map[pos] = (uint32_t)(i2 * width + j2);
            else
                Map[pos] = RADCOR_INVALID;
        }
    }

    radcor->map_upd[0] = alpaX;
    radcor->map_upd[1] = alpaY;
    radcor->map_upd[2] = dir;

    return 1;
}

void radcor_apply(void *ptr, VJFrame *frame, int *args)
{
    radcor_t *radcor = (radcor_t*) ptr;
    if(!radcor || !frame || !args)
        return;

    const int width  = frame->width;
    const int height = frame->height;
    const int len    = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    int alpaX = radcor_clampi(args[0], 1, 1000);
    int alpaY = radcor_clampi(args[1], 1, 1000);
    int dir   = radcor_clampi(args[2], 0, 1);
    int alpha = radcor_clampi(args[3], 0, 1);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A  = frame->data[3];

    if(!Y || !Cb || !Cr)
        return;

    if(alpha && !A)
        alpha = 0;

    uint8_t *restrict Yi  = radcor->badbuf;
    uint8_t *restrict Cbi = radcor->badbuf + len;
    uint8_t *restrict Cri = radcor->badbuf + len + len;
    uint8_t *restrict Ai  = radcor->badbuf + len + len + len;

    uint32_t *restrict Map = radcor->Map;

    veejay_memcpy(Yi,  Y,  len);
    veejay_memcpy(Cbi, Cb, len);
    veejay_memcpy(Cri, Cr, len);

    if(alpha)
        veejay_memcpy(Ai, A, len);

    if(radcor->map_upd[0] != alpaX ||
       radcor->map_upd[1] != alpaY ||
       radcor->map_upd[2] != dir)
    {
        radcor_build_map(radcor, width, height, alpaX, alpaY, dir);
    }

    veejay_memset(Y,  pixel_Y_lo_, len);
    veejay_memset(Cb, 128,         len);
    veejay_memset(Cr, 128,         len);

    if(alpha)
        veejay_memset(A, 0, len);

#pragma omp parallel for schedule(static) num_threads(vje_advise_num_threads(len))
    for(int i = 0; i < len; i++) {
        const uint32_t idx = Map[i];

        if(idx != RADCOR_INVALID) {
            Y[i]  = Yi[idx];
            Cb[i] = Cbi[idx];
            Cr[i] = Cri[idx];

            if(alpha)
                A[i] = Ai[idx];
        }
    }
}