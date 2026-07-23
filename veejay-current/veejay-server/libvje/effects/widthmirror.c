/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include <stdlib.h>
#include <veejaycore/vjmem.h>
#include "widthmirror.h"

#define WIDTHMIRROR_PARAMS 5

#define P_FREQUENCY    0
#define P_PHASE        1
#define P_DRIFT_SPEED  2
#define P_EDGE_WIDTH   3
#define P_EDGE_GLOW    4

typedef struct {
    void *region;
    uint8_t *buf[3];
    int *xmap;
    uint8_t *edge;
    int n_threads;
    int w;
    int h;
    int max_freq;
    float freq_env;
    float phase_env;
    float edge_env;
    float glow_env;
    float drift_pos;
} widthmirror_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t wm_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline float wm_wrapf(float v, float period)
{
    if(v >= period || v < 0.0f) {
        int k = (int)(v / period);
        v -= (float)k * period;
        if(v < 0.0f)
            v += period;
        else if(v >= period)
            v -= period;
    }

    return v;
}

static inline size_t wm_align_size(size_t off, size_t align)
{
    return (off + align - 1) & ~(align - 1);
}

vj_effect *widthmirror_init(int max_width, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = WIDTHMIRROR_PARAMS;

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

    int max_freq = max_width;
    if(max_freq > 256)
        max_freq = 256;
    if(max_freq < 2)
        max_freq = 2;

    int max_edge = max_width / 4;
    if(max_edge > 256)
        max_edge = 256;

    ve->defaults[P_FREQUENCY]   = 4;
    ve->defaults[P_PHASE]       = 0;
    ve->defaults[P_DRIFT_SPEED] = 8;
    ve->defaults[P_EDGE_WIDTH]  = 8;
    ve->defaults[P_EDGE_GLOW]   = 24;

    ve->limits[0][P_FREQUENCY]   = 2;    ve->limits[1][P_FREQUENCY]   = max_freq;
    ve->limits[0][P_PHASE]       = 0;    ve->limits[1][P_PHASE]       = 1000;
    ve->limits[0][P_DRIFT_SPEED] = 0;    ve->limits[1][P_DRIFT_SPEED] = 1000;
    ve->limits[0][P_EDGE_WIDTH]  = 0;    ve->limits[1][P_EDGE_WIDTH]  = max_edge;
    ve->limits[0][P_EDGE_GLOW]   = 0;    ve->limits[1][P_EDGE_GLOW]   = 255;

    ve->description = "Width Mirror";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Frequency",
        "Phase",
        "Drift Speed",
        "Edge Width",
        "Edge Glow"
    );

    int freq_hi = max_freq;
    if(freq_hi > 48)
        freq_hi = 48;

    int edge_hi = max_edge;
    if(edge_hi > 64)
        edge_hi = 64;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, freq_hi, 90, 100, 12, 700, 0, 1, 120, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_SMOOTHSTEP, 0, 1000, 96, 100, 8, 420, 0, 2, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 800, 96, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, edge_hi, 72, 98, 120, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 70, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 255, 94, 100, 4, 440, 24, 1, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    (void)h;

    return ve;
}

void *widthmirror_malloc(int w, int h)
{
    widthmirror_t *wm = (widthmirror_t*) vj_calloc(sizeof(widthmirror_t));
    if(!wm)
        return NULL;

    const int len = w * h;
    const size_t plane = (size_t)len;
    const size_t xmap_bytes = sizeof(int) * (size_t)w;
    const size_t edge_bytes = (size_t)w;
    const size_t off_xmap = wm_align_size(plane * 3u, 16u);
    const size_t off_edge = wm_align_size(off_xmap + xmap_bytes, 16u);
    const size_t total = off_edge + edge_bytes + 16u;

    wm->region = vj_malloc(total);
    if(!wm->region) {
        free(wm);
        return NULL;
    }

    uint8_t *p = (uint8_t*)wm->region;

    wm->buf[0] = p;
    wm->buf[1] = wm->buf[0] + plane;
    wm->buf[2] = wm->buf[1] + plane;
    wm->xmap = (int *)(p + off_xmap);
    wm->edge = (uint8_t *)(p + off_edge);

    wm->w = w;
    wm->h = h;
    wm->max_freq = w > 256 ? 256 : w;
    if(wm->max_freq < 2)
        wm->max_freq = 2;

    wm->freq_env = 4.0f;
    wm->phase_env = 0.0f;
    wm->edge_env = 8.0f;
    wm->glow_env = 24.0f;
    wm->drift_pos = 0.0f;

    wm->n_threads = vje_advise_num_threads(len);

    return (void*) wm;
}

void widthmirror_free(void *ptr)
{
    widthmirror_t *wm = (widthmirror_t*) ptr;

    free(wm->region);
    free(wm);
}

void widthmirror_apply(void *ptr, VJFrame *frame, int *args)
{
    widthmirror_t *wm = (widthmirror_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int max_edge = width > 1024 ? 256 : (width >> 2);

    const int frequency_arg = args[P_FREQUENCY];
    const int phase_arg = args[P_PHASE];
    const int drift_speed_arg = args[P_DRIFT_SPEED];
    const int edge_arg = args[P_EDGE_WIDTH];
    const int glow_arg = args[P_EDGE_GLOW];

    const float follow = 0.185f;

    wm->freq_env += ((float)frequency_arg - wm->freq_env) * follow;
    wm->phase_env += ((float)phase_arg - wm->phase_env) * follow;
    wm->edge_env += ((float)edge_arg - wm->edge_env) * follow;
    wm->glow_env += ((float)glow_arg - wm->glow_env) * follow;

    wm->freq_env = clampf(wm->freq_env, 2.0f, (float)wm->max_freq);
    wm->phase_env = clampf(wm->phase_env, 0.0f, 1000.0f);
    wm->edge_env = clampf(wm->edge_env, 0.0f, (float)max_edge);
    wm->glow_env = clampf(wm->glow_env, 0.0f, 255.0f);

    const float band_w = (float)width / wm->freq_env;

    const float base_step = (float)drift_speed_arg * 0.020f;

    wm->drift_pos += base_step;
    wm->drift_pos = wm_wrapf(wm->drift_pos, (float)width);

    const float manual_phase = ((wm->phase_env * (float)width) * 0.001f);
    const float phase_px = wm_wrapf(manual_phase + wm->drift_pos, (float)width);
    const float edge_width = wm->edge_env;
    const int glow = clampi((int)(wm->glow_env + 0.5f), 0, 255);

    int *restrict xmap = wm->xmap;
    uint8_t *restrict edge = wm->edge;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY  = wm->buf[0];
    uint8_t *restrict srcCb = wm->buf[1];
    uint8_t *restrict srcCr = wm->buf[2];

    veejay_memcpy(srcY,  Y,  len);
    veejay_memcpy(srcCb, Cb, len);
    veejay_memcpy(srcCr, Cr, len);

#pragma omp parallel num_threads(wm->n_threads)
    {
#pragma omp for schedule(static)
        for(int x = 0; x < width; x++) {
            float u = (float)x + phase_px;
            if(u >= (float)width)
                u -= (float)width;

            const int band = (int)(u / band_w);
            const float band_start = (float)band * band_w;
            float band_end = band_start + band_w;
            if(band_end > (float)width)
                band_end = (float)width;

            const float local = u - band_start;
            const float src_u = (band & 1)
                ? (band_end - 1.0f - local)
                : (band_start + local);

            float src_x = src_u - phase_px;
            if(src_x < 0.0f)
                src_x += (float)width;
            else if(src_x >= (float)width)
                src_x -= (float)width;

            int sx = (int)(src_x + 0.5f);
            if(sx >= width)
                sx = width - 1;

            xmap[x] = sx;

            if(edge_width > 0.1f) {
                float dist = local < (band_end - band_start - local)
                    ? local
                    : (band_end - band_start - local);
                float q = 1.0f - clampf(dist / edge_width, 0.0f, 1.0f);
                edge[x] = (uint8_t)(q * 255.0f + 0.5f);
            }
            else {
                edge[x] = 0;
            }
        }

#pragma omp for schedule(static)
        for(int y = 0; y < height; y++) {
            const int row = y * width;

            for(int x = 0; x < width; x++) {
                const int dst = row + x;
                const int src = row + xmap[x];
                const int q = edge[x];

                int yy = srcY[src];
                int uu = srcCb[src];
                int vv = srcCr[src];

                if(q > 0 && glow > 0) {
                    const int g = (q * glow + 127) / 255;
                    yy += g;
                }

                Y[dst]  = wm_u8(yy);
                Cb[dst] = wm_u8(uu);
                Cr[dst] = wm_u8(vv);
            }
        }
    }
}
