/* 
 * Linux VeeJay
 *
 * Copyright(C)2007 Niels Elburg <nwelburg@gmail>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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

#define RADCOR_PARAMS 4
#define RADCOR_INVALID 0xffffffffU

#define P_ALPHA_X      0
#define P_ALPHA_Y      1
#define P_DIRECTION    2
#define P_UPDATE_ALPHA 3

typedef struct {
    uint8_t *block;
    uint8_t *badbuf;
    uint32_t *Map;
    float *x_lut;
    int map_upd[3];
    int n_threads;
} radcor_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *radcor_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RADCOR_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    ve->limits[0][P_ALPHA_X] = 1;      ve->limits[1][P_ALPHA_X] = 1000;      ve->defaults[P_ALPHA_X] = 10;
    ve->limits[0][P_ALPHA_Y] = 1;      ve->limits[1][P_ALPHA_Y] = 1000;      ve->defaults[P_ALPHA_Y] = 40;
    ve->limits[0][P_DIRECTION] = 0;    ve->limits[1][P_DIRECTION] = 1;       ve->defaults[P_DIRECTION] = 0;
    ve->limits[0][P_UPDATE_ALPHA] = 0; ve->limits[1][P_UPDATE_ALPHA] = 1;    ve->defaults[P_UPDATE_ALPHA] = 0;

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

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_DIRECTION], P_DIRECTION, "Forward", "Reverse");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_UPDATE_ALPHA], P_UPDATE_ALPHA, "Off", "On");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 8, 360, 72, 92, 80, 900, 0, 2, 500, VJ_BEAT_COST_EXPENSIVE, 56, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 12, 420, 72, 92, 80, 900, 0, 2, 500, VJ_BEAT_COST_EXPENSIVE, 54, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *radcor_malloc(int w, int h)
{
    radcor_t *r = (radcor_t*)vj_calloc(sizeof(radcor_t));

    if(!r)
        return NULL;

    const int len = w * h;
    const size_t plane = (size_t)len;
    const size_t badbuf_bytes = plane * 4u;
    const size_t map_bytes = plane * sizeof(uint32_t);
    const size_t x_lut_bytes = (size_t)w * sizeof(float);
    const size_t total = badbuf_bytes + map_bytes + x_lut_bytes + 32u;

    r->block = (uint8_t*)vj_malloc(total);

    if(!r->block) {
        free(r);
        return NULL;
    }

    uint8_t *p = r->block;

    r->badbuf = p;
    p += badbuf_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);

    r->Map = (uint32_t*)p;
    p += map_bytes;

    r->x_lut = (float*)p;

    for(int i = 0; i < len; i++)
        r->Map[i] = RADCOR_INVALID;

    r->map_upd[0] = -1;
    r->map_upd[1] = -1;
    r->map_upd[2] = -1;
    r->n_threads = vje_advise_num_threads(len);

    return (void*)r;
}

void radcor_free(void *ptr)
{
    radcor_t *r = (radcor_t*)ptr;

    free(r->block);
    free(r);
}

static void radcor_build_map(radcor_t *radcor, int width, int height, int alpha_x, int alpha_y, int direction)
{
    const float inv_w = 1.0f / (float)width;
    const float inv_h = 1.0f / (float)height;
    const float half_w = 0.5f * (float)width;
    const float half_h = 0.5f * (float)height;

    float ax = (float)alpha_x * 0.001f;
    float ay = (float)alpha_y * 0.001f;

    uint32_t *restrict Map = radcor->Map;
    float *restrict x_lut = radcor->x_lut;

    if(!direction) {
        ax = -ax;
        ay = -ay;
    }

    for(int x = 0; x < width; x++)
        x_lut[x] = ((2.0f * (float)x) - (float)width) * inv_w;

    for(int y = 0; y < height; y++) {
        const float yy = ((2.0f * (float)y) - (float)height) * inv_h;
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const float xx = x_lut[x];
            const float r1 = xx * xx + yy * yy;
            const float d1x = 1.0f - ax * r1;
            const float d1y = 1.0f - ay * r1;
            const int pos = row + x;

            if(fabsf(d1x) < 0.000001f || fabsf(d1y) < 0.000001f) {
                Map[pos] = RADCOR_INVALID;
                continue;
            }

            const float x3 = xx / d1x;
            const float y3 = yy / d1y;
            const float r2 = x3 * x3 + y3 * y3;
            const float d2x = 1.0f - ax * r2;
            const float d2y = 1.0f - ay * r2;

            if(fabsf(d2x) < 0.000001f || fabsf(d2y) < 0.000001f) {
                Map[pos] = RADCOR_INVALID;
                continue;
            }

            const float sx = xx / d2x;
            const float sy = yy / d2y;

            const int iy = (int)((sy + 1.0f) * half_h);
            const int ix = (int)((sx + 1.0f) * half_w);

            Map[pos] = ((unsigned)iy < (unsigned)height && (unsigned)ix < (unsigned)width)
                ? (uint32_t)(iy * width + ix)
                : RADCOR_INVALID;
        }
    }

    radcor->map_upd[0] = alpha_x;
    radcor->map_upd[1] = alpha_y;
    radcor->map_upd[2] = direction;
}


void radcor_apply(void *ptr, VJFrame *frame, int *args)
{
    radcor_t *radcor = (radcor_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int alpha_x = args[P_ALPHA_X];
    const int alpha_y = args[P_ALPHA_Y];
    const int direction = args[P_DIRECTION];
    const int update_alpha = args[P_UPDATE_ALPHA];

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A = frame->data[3];

    uint8_t *restrict Yi = radcor->badbuf;
    uint8_t *restrict Cbi = radcor->badbuf + len;
    uint8_t *restrict Cri = radcor->badbuf + len + len;
    uint8_t *restrict Ai = radcor->badbuf + len + len + len;

    uint32_t *restrict Map = radcor->Map;

    if(radcor->map_upd[0] != alpha_x ||
       radcor->map_upd[1] != alpha_y ||
       radcor->map_upd[2] != direction)
        radcor_build_map(radcor, width, height, alpha_x, alpha_y, direction);

    veejay_memcpy(Yi, Y, len);
    veejay_memcpy(Cbi, Cb, len);
    veejay_memcpy(Cri, Cr, len);

    if(update_alpha)
        veejay_memcpy(Ai, A, len);

#pragma omp parallel for schedule(static) num_threads(radcor->n_threads)
    for(int i = 0; i < len; i++) {
        const uint32_t idx = Map[i];

        if(idx != RADCOR_INVALID) {
            Y[i] = Yi[idx];
            Cb[i] = Cbi[idx];
            Cr[i] = Cri[idx];
            if(update_alpha)
                A[i] = Ai[idx];
        }
        else {
            Y[i] = pixel_Y_lo_;
            Cb[i] = 128;
            Cr[i] = 128;
            if(update_alpha)
                A[i] = 0;
        }
    }
}
