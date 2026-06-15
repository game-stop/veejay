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
#include "tiler.h"

#define TILER_PARAMS 7

#define P_TILES       0
#define P_OPACITY     1
#define P_PHASE_X     2
#define P_PHASE_Y     3
#define P_DRIFT_SPEED 4
#define P_TILE_DRIVE  5
#define P_MIX_DRIVE   6

typedef struct {
    uint8_t *buf[3];
    int n_threads;

    float tiles_s;
    float opacity_s;
    float phase_x_s;
    float phase_y_s;
    float drift_s;
    float tile_drive_s;
    float mix_drive_s;

    float drift_phase;
    int initialized;
} tiler_t;

static inline int tiler_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int tiler_wrapi(int v, int max)
{
    v %= max;
    if(v < 0)
        v += max;

    return v;
}

static inline uint8_t tiler_mix_u8(uint8_t src, uint8_t tile, int tile_q8)
{
    return (uint8_t)((((int)src * (256 - tile_q8)) + ((int)tile * tile_q8) + 128) >> 8);
}



static inline int tiler_smooth_to(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float k = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * k;

    *state = out;
    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}



vj_effect *tiler_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TILER_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    int max_tiles = (w < h ? w : h) / 8;
    if(max_tiles < 2)
        max_tiles = 2;

    ve->limits[0][P_TILES] = 2;
    ve->limits[1][P_TILES] = max_tiles;
    ve->defaults[P_TILES] = 2;

    ve->limits[0][P_OPACITY] = 0;
    ve->limits[1][P_OPACITY] = 255;
    ve->defaults[P_OPACITY] = 0;

    ve->limits[0][P_PHASE_X] = 0;
    ve->limits[1][P_PHASE_X] = 1000;
    ve->defaults[P_PHASE_X] = 0;

    ve->limits[0][P_PHASE_Y] = 0;
    ve->limits[1][P_PHASE_Y] = 1000;
    ve->defaults[P_PHASE_Y] = 0;

    ve->limits[0][P_DRIFT_SPEED] = -1000;
    ve->limits[1][P_DRIFT_SPEED] = 1000;
    ve->defaults[P_DRIFT_SPEED] = 0;

    ve->limits[0][P_TILE_DRIVE] = 0;
    ve->limits[1][P_TILE_DRIVE] = 1000;
    ve->defaults[P_TILE_DRIVE] = 0;

    ve->limits[0][P_MIX_DRIVE] = 0;
    ve->limits[1][P_MIX_DRIVE] = 1000;
    ve->defaults[P_MIX_DRIVE] = 0;

    ve->description = "Tiler";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Tiles",
        "Opacity",
        "Phase X",
        "Phase Y",
        "Drift Speed",
        "Tile Drive",
        "Mix Drive"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GRID_SIZE,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 2,                  max_tiles,           12, 46,  700, 2800, 0,    76,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       32,                 255,                 12, 46,  700, 2800, 0,    68,
        VJ_BEAT_GEOMETRY_PHASE,   VJ_BEAT_F_CONTINUOUS,                                                  0,                  1000,                12, 46, 1000, 3600, 0,    58,
        VJ_BEAT_GEOMETRY_PHASE,   VJ_BEAT_F_CONTINUOUS,                                                  0,                  1000,                12, 46, 1000, 3600, 0,    58,
        VJ_BEAT_SIGNED_SPEED,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS,   -1000,              1000,                12, 46,  900, 3200, 0,    62,
        VJ_BEAT_GRID_SIZE,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                        140,                1000,                16, 62,  700, 2800, 0,    94,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                        120,                1000,                14, 54,  800, 3200, 0,    86
    );

    return ve;
}

void *tiler_malloc(int w, int h)
{
    tiler_t *s = (tiler_t*) vj_calloc(sizeof(tiler_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    s->n_threads = vje_advise_num_threads(len);

    s->tiles_s = 2.0f;
    s->opacity_s = 0.0f;
    s->phase_x_s = 0.0f;
    s->phase_y_s = 0.0f;
    s->drift_s = 0.0f;
    s->tile_drive_s = 0.0f;
    s->mix_drive_s = 0.0f;
    s->drift_phase = 0.0f;
    s->initialized = 0;

    return (void*) s;
}

void tiler_free(void *ptr)
{
    tiler_t *s = (tiler_t*) ptr;

    free(s->buf[0]);
    free(s);
}

void tiler_apply(void *ptr, VJFrame *frame, int *args)
{
    tiler_t *s = (tiler_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;

    int max_tiles = (width < height ? width : height) / 8;
    if(max_tiles < 2)
        max_tiles = 2;

    const int tiles_arg = args[P_TILES];
    const int opacity_arg = args[P_OPACITY];
    const int phase_x_arg = args[P_PHASE_X];
    const int phase_y_arg = args[P_PHASE_Y];
    const int drift_arg = args[P_DRIFT_SPEED];
    const int tile_drive_arg = args[P_TILE_DRIVE];
    const int mix_drive_arg = args[P_MIX_DRIVE];

    const float fast_a = 0.34f;
    const float slow_r = 0.085f;

    if(!s->initialized) {
        s->tiles_s = (float)tiles_arg;
        s->opacity_s = (float)opacity_arg;
        s->phase_x_s = (float)phase_x_arg;
        s->phase_y_s = (float)phase_y_arg;
        s->drift_s = (float)drift_arg;
        s->tile_drive_s = (float)tile_drive_arg;
        s->mix_drive_s = (float)mix_drive_arg;
        s->initialized = 1;
    }

    int base_tiles = tiler_smooth_to(&s->tiles_s, tiles_arg, fast_a, slow_r);
    int base_opacity = tiler_smooth_to(&s->opacity_s, opacity_arg, fast_a, slow_r);
    int phase_x = tiler_smooth_to(&s->phase_x_s, phase_x_arg, fast_a * 0.56f, slow_r);
    int phase_y = tiler_smooth_to(&s->phase_y_s, phase_y_arg, fast_a * 0.56f, slow_r);
    int drift = tiler_smooth_to(&s->drift_s, drift_arg, fast_a * 0.48f, slow_r);
    int tile_drive = tiler_smooth_to(&s->tile_drive_s, tile_drive_arg, fast_a * 0.82f, slow_r);
    int mix_drive = tiler_smooth_to(&s->mix_drive_s, mix_drive_arg, fast_a * 0.82f, slow_r);

    base_tiles = tiler_clampi(base_tiles, 2, max_tiles);
    base_opacity = tiler_clampi(base_opacity, 0, 255);
    phase_x = tiler_clampi(phase_x, 0, 1000);
    phase_y = tiler_clampi(phase_y, 0, 1000);
    drift = tiler_clampi(drift, -1000, 1000);
    tile_drive = tiler_clampi(tile_drive, 0, 1000);
    mix_drive = tiler_clampi(mix_drive, 0, 1000);

    const float tile_drive_f = (float)tile_drive * 0.001f;
    const float mix_drive_f = (float)mix_drive * 0.001f;

    int tiles = base_tiles + (int)(((float)(max_tiles - base_tiles) * tile_drive_f * 0.90f) + 0.5f);
    tiles = tiler_clampi(tiles, 2, max_tiles);

    int effective_opacity = base_opacity + (int)((255.0f - (float)base_opacity) * mix_drive_f * 0.78f + 0.5f);
    effective_opacity = tiler_clampi(effective_opacity, 0, 255);

    s->drift_phase += (float)drift * 0.018f + tile_drive_f * 0.75f;
    if(s->drift_phase > 32768.0f || s->drift_phase < -32768.0f)
        s->drift_phase = 0.0f;

    const int phase_px = tiler_wrapi(((phase_x * width) + 500) / 1000 + (int)s->drift_phase + (int)(tile_drive_f * (float)width * 0.045f), width);
    const int phase_py = tiler_wrapi(((phase_y * height) + 500) / 1000 + (int)(s->drift_phase * 0.618f) - (int)(tile_drive_f * (float)height * 0.032f), height);

    const int small_w = (width  + tiles - 1) / tiles;
    const int small_h = (height + tiles - 1) / tiles;
    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < small_h; y++) {
        const int sy = tiler_wrapi(y * tiles + phase_py, height);
        const int src_row = sy * width;
        const int dst_row = y * small_w;

        for(int x = 0; x < small_w; x++) {
            const int sx = tiler_wrapi(x * tiles + phase_px, width);
            const int src = src_row + sx;
            const int dst = dst_row + x;

            bufY[dst] = srcY[src];
            bufU[dst] = srcU[src];
            bufV[dst] = srcV[src];
        }
    }

    if(effective_opacity >= 255)
        return;

    const int tile_q8 = 256 - ((effective_opacity * 256 + 127) / 255);
    const int tile_off_x = (phase_px / tiles) % small_w;
    const int tile_off_y = (phase_py / tiles) % small_h;

    if(tile_q8 >= 256) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int src_row = y * width;
            const int tile_y = (y + tile_off_y) % small_h;
            const int tile_row = tile_y * small_w;

            for(int x = 0; x < width; x++) {
                const int dst = src_row + x;
                const int tile = tile_row + ((x + tile_off_x) % small_w);

                srcY[dst] = bufY[tile];
                srcU[dst] = bufU[tile];
                srcV[dst] = bufV[tile];
            }
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int src_row = y * width;
            const int tile_y = (y + tile_off_y) % small_h;
            const int tile_row = tile_y * small_w;

            for(int x = 0; x < width; x++) {
                const int dst = src_row + x;
                const int tile = tile_row + ((x + tile_off_x) % small_w);

                srcY[dst] = tiler_mix_u8(srcY[dst], bufY[tile], tile_q8);
                srcU[dst] = tiler_mix_u8(srcU[dst], bufU[tile], tile_q8);
                srcV[dst] = tiler_mix_u8(srcV[dst], bufV[tile], tile_q8);
            }
        }
    }
}
