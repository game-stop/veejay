/* 
 * veejay  
 *
 * Copyright (C) 2026 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
#include "common.h"
#include "bowshock.h"

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#include <veejaycore/vjmem.h>
#include <libvje/vje.h>

#define BS_MAX_WAVES 4

enum {
    BS_MODE = 0,
    BS_DISPLACE,
    BS_IMPACT,
    BS_SHOCKWAVE,
    BS_FRONT_WIDTH,
    BS_FRONT_SPEED,
    BS_REFRACTION,
    BS_GEOMETRY,
    BS_CENTER_DRIFT,
    BS_FRONT_GLOW,
    BS_SNARE_FLASH,
    BS_HAT_SPARKLE,
    BS_CHROMA_PUSH,
    BS_NUM_PARAMS
};

enum {
    BS_MODE_BOW = 0,
    BS_MODE_TORSION = 1,
    BS_MODE_HYBRID = 2
};

typedef struct {
    float pos;
    float amp;
    float speed;
    int cx;
    int cy;
    int width;
    int polarity;
    int dir_x;
    int dir_y;
    int active;
} bowshock_wave_t;

typedef struct {
    int w;
    int h;
    int len;
    int n_threads;

    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;

    int16_t *map_dx;
    int16_t *map_dy;
    int16_t *map_wave;
    int16_t *map_glow;
    int16_t *map_pull;

    bowshock_wave_t waves[BS_MAX_WAVES];

    float impact_env;
    float shock_env;
    float snare_env;
    float hat_env;
    float swing_phase;

    float last_impact;
    float last_shock;
    float last_snare;

    int impact_cooldown;
    int shock_cooldown;
    int snare_cooldown;

    int wave_slot;
    uint32_t frame_count;
} bowshock_t;

static const int bs_dir_x[16] = {
     256,  237,  181,   98,    0,  -98, -181, -237,
    -256, -237, -181,  -98,    0,   98,  181,  237
};

static const int bs_dir_y[16] = {
       0,   98,  181,  237,  256,  237,  181,   98,
       0,  -98, -181, -237, -256, -237, -181,  -98
};

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float bs_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t bs_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline int16_t bs_i16(int v)
{
    return (int16_t)clampi(v, -32768, 32767);
}

static inline int bs_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline uint32_t bs_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline float bs_env(float oldv, float target, float attack, float release)
{
    return target > oldv
        ? oldv + (target - oldv) * attack
        : oldv + (target - oldv) * release;
}

static void bs_spawn_wave(bowshock_t *s,
                          float impact,
                          float shock,
                          int width,
                          int speed,
                          int center_drift,
                          int geometry,
                          int mode)
{
    if(!s)
        return;

    const int w = s->w;
    const int h = s->h;

    bowshock_wave_t *wv = &s->waves[s->wave_slot];
    s->wave_slot = (s->wave_slot + 1) & (BS_MAX_WAVES - 1);

    const uint32_t hv = bs_hash_u32(s->frame_count * 1664525U +
                                    ((uint32_t)(impact * 1000.0f) << 5) +
                                    ((uint32_t)(shock * 1000.0f) << 13) +
                                    ((uint32_t)(geometry + mode * 37) << 19));

    const int rx = (int)(hv & 255) - 128;
    const int ry = (int)((hv >> 8) & 255) - 128;
    const int dir_idx = (int)((hv >> 17) & 15U);

    const int jitter_x = (rx * w * center_drift) / (128 * 240);
    const int jitter_y = (ry * h * center_drift) / (128 * 240);

    wv->cx = clampi((w >> 1) + jitter_x, 0, w - 1);
    wv->cy = clampi((h >> 1) + jitter_y, 0, h - 1);
    wv->pos = mode == BS_MODE_BOW ? (float)(-width) : 0.0f;
    wv->amp = bs_clampf(0.62f + impact * 0.46f + shock * 0.62f, 0.0f, 1.55f);
    wv->speed = 2.1f + ((float)speed * 0.118f) + impact * 6.5f + shock * 10.0f;
    wv->width = clampi(width + (int)(shock * 18.0f), 4, 120);
    wv->polarity = (hv & 0x10000U) ? 1 : -1;
    wv->dir_x = bs_dir_x[dir_idx];
    wv->dir_y = bs_dir_y[dir_idx];
    wv->active = 1;
}

vj_effect *bowshock_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = BS_NUM_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * BS_NUM_PARAMS);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * BS_NUM_PARAMS);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * BS_NUM_PARAMS);

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

    ve->description = "Bow Shock";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;

    ve->defaults[BS_MODE]         = 0;
    ve->defaults[BS_DISPLACE]     = 76;
    ve->defaults[BS_IMPACT]       = 0;
    ve->defaults[BS_SHOCKWAVE]    = 0;
    ve->defaults[BS_FRONT_WIDTH]  = 34;
    ve->defaults[BS_FRONT_SPEED]  = 42;
    ve->defaults[BS_REFRACTION]   = 88;
    ve->defaults[BS_GEOMETRY]     = 58;
    ve->defaults[BS_CENTER_DRIFT] = 34;
    ve->defaults[BS_FRONT_GLOW]   = 74;
    ve->defaults[BS_SNARE_FLASH]  = 0;
    ve->defaults[BS_HAT_SPARKLE]  = 42;
    ve->defaults[BS_CHROMA_PUSH]  = 34;

    ve->limits[0][BS_MODE]         = 0;
    ve->limits[1][BS_MODE]         = 2;
    ve->limits[0][BS_DISPLACE]     = 0;
    ve->limits[1][BS_DISPLACE]     = 200;
    ve->limits[0][BS_IMPACT]       = 0;
    ve->limits[1][BS_IMPACT]       = 100;
    ve->limits[0][BS_SHOCKWAVE]    = 0;
    ve->limits[1][BS_SHOCKWAVE]    = 100;
    ve->limits[0][BS_FRONT_WIDTH]  = 4;
    ve->limits[1][BS_FRONT_WIDTH]  = 96;
    ve->limits[0][BS_FRONT_SPEED]  = 0;
    ve->limits[1][BS_FRONT_SPEED]  = 100;
    ve->limits[0][BS_REFRACTION]   = 0;
    ve->limits[1][BS_REFRACTION]   = 160;
    ve->limits[0][BS_GEOMETRY]     = 0;
    ve->limits[1][BS_GEOMETRY]     = 128;
    ve->limits[0][BS_CENTER_DRIFT] = 0;
    ve->limits[1][BS_CENTER_DRIFT] = 100;
    ve->limits[0][BS_FRONT_GLOW]   = 0;
    ve->limits[1][BS_FRONT_GLOW]   = 200;
    ve->limits[0][BS_SNARE_FLASH]  = 0;
    ve->limits[1][BS_SNARE_FLASH]  = 100;
    ve->limits[0][BS_HAT_SPARKLE]  = 0;
    ve->limits[1][BS_HAT_SPARKLE]  = 200;
    ve->limits[0][BS_CHROMA_PUSH]  = 0;
    ve->limits[1][BS_CHROMA_PUSH]  = 120;

    ve->param_description = vje_build_param_list(
        BS_NUM_PARAMS,
        "Mode",
        "Displace",
        "Impact Pulse",
        "Shockwave",
        "Front Width",
        "Front Speed",
        "Refraction",
        "Geometry",
        "Center Drift",
        "Front Glow",
        "Snare Flash",
        "Hat Sparkle",
        "Chroma Push"
    );
    
    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ONSET, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 48, 168, 34, 82, 30, 520, 0, 0, 0, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_TRIGGER, VJ_BEAT_F_IMPULSE, VJ_BEAT_SRC_BEAT_PULSE, VJ_BEAT_OP_IMPULSE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 100, 100, 100, 0, 90, 18, 1, 0, VJ_BEAT_COST_CHEAP, 240, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_KICK, VJ_BEAT_F_IMPULSE, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_IMPULSE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 100, 94, 100, 0, 110, 22, 1, 0, VJ_BEAT_COST_CHEAP, 225, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, VJ_BEAT_SRC_PHRASE, VJ_BEAT_OP_SAMPLE_HOLD, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 12, 76, 24, 58, 0, 0, 2600, 2, 1200, VJ_BEAT_COST_MODERATE, 38, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ACTIVITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 16, 82, 30, 72, 100, 900, 0, 0, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_MID_ACTIVITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 54, 150, 30, 74, 80, 700, 0, 0, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_FLUX, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SQUARE, 22, 116, 24, 62, 120, 900, 0, 0, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_PHRASE_ONLY, VJ_BEAT_SRC_PHRASE, VJ_BEAT_OP_SAMPLE_HOLD, VJ_BEAT_POLARITY_ALTERNATE, VJ_BEAT_CURVE_SMOOTHSTEP, 8, 78, 20, 60, 0, 0, 1600, 1, 900, VJ_BEAT_COST_MODERATE, 52, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 34, 154, 28, 68, 70, 650, 0, 0, 0, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SNARE, VJ_BEAT_F_IMPULSE, VJ_BEAT_SRC_SNARE_PULSE, VJ_BEAT_OP_IMPULSE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 92, 96, 100, 0, 95, 18, 1, 0, VJ_BEAT_COST_CHEAP, 210, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_HAT, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 18, 148, 22, 54, 20, 260, 36, 0, 0, VJ_BEAT_COST_CHEAP, 68, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_BIPOLAR, VJ_BEAT_CURVE_SMOOTHSTEP, 8, 98, 18, 52, 120, 850, 0, 0, 0, VJ_BEAT_COST_CHEAP, 62, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *bowshock_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    const int len = w * h;
    const int n_threads = vje_advise_num_threads(len);
    const size_t plane = (size_t)len;
    const size_t map_bytes = plane * sizeof(int16_t);
    const size_t total = sizeof(bowshock_t) + plane * 3 + map_bytes * 5 + 64;

    bowshock_t *s = (bowshock_t *)vj_calloc(total);

    if(!s)
        return NULL;

    s->w = w;
    s->h = h;
    s->len = len;
    s->n_threads = n_threads;

    uint8_t *p = (uint8_t *)(s + 1);

    s->src_y = p;
    p += plane;
    s->src_u = p;
    p += plane;
    s->src_v = p;
    p += plane;

    p = (uint8_t *)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);

    s->map_dx = (int16_t *)p;
    p += map_bytes;
    s->map_dy = (int16_t *)p;
    p += map_bytes;
    s->map_wave = (int16_t *)p;
    p += map_bytes;
    s->map_glow = (int16_t *)p;
    p += map_bytes;
    s->map_pull = (int16_t *)p;

    s->impact_env = 0.0f;
    s->shock_env = 0.0f;
    s->snare_env = 0.0f;
    s->hat_env = 0.0f;
    s->swing_phase = 0.0f;

    s->last_impact = 0.0f;
    s->last_shock = 0.0f;
    s->last_snare = 0.0f;

    s->impact_cooldown = 0;
    s->shock_cooldown = 0;
    s->snare_cooldown = 0;

    s->wave_slot = 0;
    s->frame_count = 0;

    for(int i = 0; i < BS_MAX_WAVES; i++) {
        s->waves[i].pos = 0.0f;
        s->waves[i].amp = 0.0f;
        s->waves[i].speed = 0.0f;
        s->waves[i].cx = w >> 1;
        s->waves[i].cy = h >> 1;
        s->waves[i].width = 32;
        s->waves[i].polarity = 1;
        s->waves[i].dir_x = 256;
        s->waves[i].dir_y = 0;
        s->waves[i].active = 0;
    }

    return s;
}

void bowshock_free(void *ptr)
{
    if(ptr)
        free(ptr);
}

#define BS_ACCUM_BOW(K) do {                                             \
    const int dx0 = x - ax[(K)];                                         \
    const int dy0 = y - ay[(K)];                                         \
    const int dirx = adirx[(K)];                                         \
    const int diry = adiry[(K)];                                         \
    const int forward = (dx0 * dirx + dy0 * diry) >> 8;                  \
    const int side = (-dx0 * diry + dy0 * dirx) >> 8;                    \
    const int side_l = side < -bow_side_cap ? -bow_side_cap : (side > bow_side_cap ? bow_side_cap : side); \
    const int side_q = (side_l * side_l) >> 8;                           \
    const int curved = forward + ((side_q * bow_curve) >> 7);            \
    int front = aw[(K)] - bs_absi(curved - ap[(K)]);                     \
    int tail = aw[(K)] - bs_absi(curved - atail[(K)]);                   \
    front = front > 0 ? front : 0;                                       \
    tail = tail > 0 ? tail : 0;                                          \
    const int wr = (((front - (tail >> 1)) * ascale[(K)]) >> 8) * apol[(K)]; \
    const int push = (wr * push_scale) >> 15;                            \
    const int shear = (push * bow_shear * side_l) >> 16;                 \
    dx_acc += (dirx * push - diry * shear) >> 8;                         \
    dy_acc += (diry * push + dirx * shear) >> 8;                         \
    wave_sum += wr;                                                      \
    pull_sum += wr < 0 ? -wr : wr;                                       \
    glow_sum += wr > 0 ? wr : (wr >> 2);                                 \
} while(0)

#define BS_ACCUM_TORSION(K) do {                                         \
    const int dx0 = x - ax[(K)];                                         \
    const int dy0 = y - ay[(K)];                                         \
    const int adx = dx0 < 0 ? -dx0 : dx0;                                \
    const int ady = dy0 < 0 ? -dy0 : dy0;                                \
    const int mx = adx > ady ? adx : ady;                                \
    const int mn = adx > ady ? ady : adx;                                \
    const int dist = mx + (mn >> 1);                                     \
    int front = aw[(K)] - bs_absi(dist - ap[(K)]);                       \
    int tail = aw[(K)] - bs_absi(dist - atail[(K)]);                     \
    front = front > 0 ? front : 0;                                       \
    tail = tail > 0 ? tail : 0;                                          \
    const int wr = (((front - (tail >> 1)) * ascale[(K)]) >> 8) * apol[(K)]; \
    const int push = (wr * push_scale) >> 15;                            \
    dx_acc += (-dy0 * push * torsion_twist) >> 15;                       \
    dy_acc += ( dx0 * push * torsion_twist) >> 15;                       \
    wave_sum += wr;                                                      \
    pull_sum += wr < 0 ? -wr : wr;                                       \
    glow_sum += wr > 0 ? wr : (wr >> 2);                                 \
} while(0)

#define BS_ACCUM_HYBRID(K) do {                                          \
    const int dx0 = x - ax[(K)];                                         \
    const int dy0 = y - ay[(K)];                                         \
    const int adx = dx0 < 0 ? -dx0 : dx0;                                \
    const int ady = dy0 < 0 ? -dy0 : dy0;                                \
    const int mx = adx > ady ? adx : ady;                                \
    const int mn = adx > ady ? ady : adx;                                \
    const int dist = mx + (mn >> 1);                                     \
    const int dirx = adirx[(K)];                                         \
    const int diry = adiry[(K)];                                         \
    const int forward = (dx0 * dirx + dy0 * diry) >> 8;                  \
    const int side = (-dx0 * diry + dy0 * dirx) >> 8;                    \
    const int side_l = side < -bow_side_cap ? -bow_side_cap : (side > bow_side_cap ? bow_side_cap : side); \
    const int side_q = (side_l * side_l) >> 8;                           \
    const int curved = forward + ((side_q * bow_curve) >> 8);            \
    int front_a = aw[(K)] - bs_absi(curved - ap[(K)]);                   \
    int tail_a = aw[(K)] - bs_absi(curved - atail[(K)]);                 \
    int front_b = aw[(K)] - bs_absi(dist - ap[(K)]);                     \
    int tail_b = aw[(K)] - bs_absi(dist - atail[(K)]);                   \
    front_a = front_a > 0 ? front_a : 0;                                 \
    tail_a = tail_a > 0 ? tail_a : 0;                                    \
    front_b = front_b > 0 ? front_b : 0;                                 \
    tail_b = tail_b > 0 ? tail_b : 0;                                    \
    const int wr_a = (((front_a - (tail_a >> 1)) * ascale[(K)]) >> 9) * apol[(K)]; \
    const int wr_b = (((front_b - (tail_b >> 1)) * ascale[(K)]) >> 9) * apol[(K)]; \
    const int wr = wr_a + wr_b;                                          \
    const int push_a = (wr_a * push_scale) >> 15;                        \
    const int push_b = (wr_b * push_scale) >> 15;                        \
    const int shear = (push_a * bow_shear * side_l) >> 16;               \
    dx_acc += (dirx * push_a - diry * shear) >> 8;                       \
    dy_acc += (diry * push_a + dirx * shear) >> 8;                       \
    dx_acc += (-dy0 * push_b * torsion_twist) >> 16;                     \
    dy_acc += ( dx0 * push_b * torsion_twist) >> 16;                     \
    wave_sum += wr;                                                      \
    pull_sum += wr < 0 ? -wr : wr;                                       \
    glow_sum += wr > 0 ? wr : (wr >> 2);                                 \
} while(0)

#define BS_STORE_MAP() do {                                              \
    const int i = row + x;                                               \
    const int dx_l = dx_acc < -map_limit ? -map_limit : (dx_acc > map_limit ? map_limit : dx_acc); \
    const int dy_l = dy_acc < -map_limit ? -map_limit : (dy_acc > map_limit ? map_limit : dy_acc); \
    const int wave_l = wave_sum < -signal_limit ? -signal_limit : (wave_sum > signal_limit ? signal_limit : wave_sum); \
    const int glow_l = glow_sum < -signal_limit ? -signal_limit : (glow_sum > signal_limit ? signal_limit : glow_sum); \
    const int pull_l = pull_sum > signal_limit ? signal_limit : pull_sum; \
    map_dx[i] = (int16_t)dx_l;                                           \
    map_dy[i] = (int16_t)dy_l;                                           \
    map_wave[i] = (int16_t)wave_l;                                       \
    map_glow[i] = (int16_t)glow_l;                                       \
    map_pull[i] = (int16_t)pull_l;                                       \
} while(0)

#define BS_OMP_SIMD _Pragma("omp simd")

#define BS_ROW_SWITCH(ACCUM) do {                                        \
    switch(nactive) {                                                    \
        case 1:                                                          \
            BS_OMP_SIMD                                                  \
            for(int x = 0; x < w; x++) {                                 \
                int dx_acc = 0;                                          \
                int dy_acc = 0;                                          \
                int wave_sum = 0;                                        \
                int glow_sum = 0;                                        \
                int pull_sum = 0;                                        \
                ACCUM(0);                                                \
                BS_STORE_MAP();                                          \
            }                                                            \
            break;                                                       \
        case 2:                                                          \
            BS_OMP_SIMD                                                  \
            for(int x = 0; x < w; x++) {                                 \
                int dx_acc = 0;                                          \
                int dy_acc = 0;                                          \
                int wave_sum = 0;                                        \
                int glow_sum = 0;                                        \
                int pull_sum = 0;                                        \
                ACCUM(0);                                                \
                ACCUM(1);                                                \
                BS_STORE_MAP();                                          \
            }                                                            \
            break;                                                       \
        case 3:                                                          \
            BS_OMP_SIMD                                                  \
            for(int x = 0; x < w; x++) {                                 \
                int dx_acc = 0;                                          \
                int dy_acc = 0;                                          \
                int wave_sum = 0;                                        \
                int glow_sum = 0;                                        \
                int pull_sum = 0;                                        \
                ACCUM(0);                                                \
                ACCUM(1);                                                \
                ACCUM(2);                                                \
                BS_STORE_MAP();                                          \
            }                                                            \
            break;                                                       \
        default:                                                         \
            BS_OMP_SIMD                                                  \
            for(int x = 0; x < w; x++) {                                 \
                int dx_acc = 0;                                          \
                int dy_acc = 0;                                          \
                int wave_sum = 0;                                        \
                int glow_sum = 0;                                        \
                int pull_sum = 0;                                        \
                ACCUM(0);                                                \
                ACCUM(1);                                                \
                ACCUM(2);                                                \
                ACCUM(3);                                                \
                BS_STORE_MAP();                                          \
            }                                                            \
            break;                                                       \
    }                                                                    \
} while(0)

void bowshock_apply(void *ptr, VJFrame *frame, int *args)
{
    bowshock_t *s = (bowshock_t *)ptr;

    uint8_t * restrict Y = frame->data[0];
    uint8_t * restrict U = frame->data[1];
    uint8_t * restrict V = frame->data[2];

    uint8_t * restrict src_y = s->src_y;
    uint8_t * restrict src_u = s->src_u;
    uint8_t * restrict src_v = s->src_v;

    int16_t * restrict map_dx = s->map_dx;
    int16_t * restrict map_dy = s->map_dy;
    int16_t * restrict map_wave = s->map_wave;
    int16_t * restrict map_glow = s->map_glow;
    int16_t * restrict map_pull = s->map_pull;

    const int w = s->w;
    const int h = s->h;
    const int len = s->len;
    const int threads = s->n_threads;

    const int mode_arg = args[BS_MODE];
    const int displace_arg = args[BS_DISPLACE];
    const int impact_arg = args[BS_IMPACT];
    const int shock_arg = args[BS_SHOCKWAVE];
    const int width_arg = args[BS_FRONT_WIDTH];
    const int speed_arg = args[BS_FRONT_SPEED];
    const int refraction_arg = args[BS_REFRACTION];
    const int geometry_arg = args[BS_GEOMETRY];
    const int center_arg = args[BS_CENTER_DRIFT];
    const int glow_arg = args[BS_FRONT_GLOW];
    const int snare_arg = args[BS_SNARE_FLASH];
    const int hat_arg = args[BS_HAT_SPARKLE];
    const int chroma_arg = args[BS_CHROMA_PUSH];

    const float impact_target = (float)impact_arg * 0.01f;
    const float shock_target = (float)shock_arg * 0.01f;
    const float snare_target = (float)snare_arg * 0.01f;
    const float hat_target = bs_clampf((float)hat_arg * (1.0f / 200.0f), 0.0f, 1.0f);

    const float impact_delta = impact_target - s->last_impact;
    const float shock_delta = shock_target - s->last_shock;
    const float snare_delta = snare_target - s->last_snare;

    const int impact_rise =
        s->impact_cooldown <= 0 &&
        (
            (impact_target > 0.34f && s->last_impact < 0.22f) ||
            impact_delta > 0.12f
        );

    const int shock_rise =
        s->shock_cooldown <= 0 &&
        (
            (shock_target > 0.30f && s->last_shock < 0.18f) ||
            shock_delta > 0.10f ||
            (impact_rise && shock_target > 0.18f)
        );

    const int snare_rise =
        s->snare_cooldown <= 0 &&
        (
            (snare_target > 0.32f && s->last_snare < 0.20f) ||
            snare_delta > 0.12f
        );

    s->impact_env = bs_env(s->impact_env, impact_target, 0.82f, 0.095f);
    s->shock_env = bs_env(s->shock_env, shock_target, 0.78f, 0.075f);
    s->snare_env = bs_env(s->snare_env, snare_target, 0.86f, 0.190f);
    s->hat_env = bs_env(s->hat_env, hat_target, 0.70f, 0.360f);

    if(impact_rise) {
        bs_spawn_wave(s, impact_target, shock_target, width_arg, speed_arg, center_arg, geometry_arg, mode_arg);
        s->impact_cooldown = 3;
    }

    if(shock_rise && !impact_rise) {
        bs_spawn_wave(s, impact_target * 0.65f, shock_target, width_arg, speed_arg + 8, center_arg, geometry_arg, mode_arg);
        s->shock_cooldown = 4;
    }

    if(snare_rise) {
        bs_spawn_wave(s,
                      impact_target * 0.35f,
                      shock_target * 0.42f + snare_target * 0.58f,
                      width_arg >> 1,
                      speed_arg + 12,
                      center_arg,
                      geometry_arg + 17,
                      mode_arg);
        s->snare_cooldown = 3;
    }

    if(s->impact_cooldown > 0)
        s->impact_cooldown--;
    if(s->shock_cooldown > 0)
        s->shock_cooldown--;
    if(s->snare_cooldown > 0)
        s->snare_cooldown--;

    const float decay = 0.9475f;
    const float max_dist = (float)(w > h ? w : h) * (mode_arg == BS_MODE_BOW ? 2.25f : 1.55f);

    for(int i = 0; i < BS_MAX_WAVES; i++) {
        bowshock_wave_t *wv = &s->waves[i];

        if(!wv->active)
            continue;

        wv->pos += wv->speed;
        wv->amp *= decay;

        if(wv->amp < 0.012f || wv->pos > max_dist)
            wv->active = 0;
    }

    s->swing_phase +=
        0.010f +
        ((float)speed_arg * 0.0011f) +
        ((float)geometry_arg * 0.0007f) +
        s->impact_env * 0.050f +
        s->snare_env * 0.030f +
        s->hat_env * 0.012f;

    if(s->swing_phase > (float)(M_PI * 2.0))
        s->swing_phase -= (float)(M_PI * 2.0);

    s->last_impact = impact_target;
    s->last_shock = shock_target;
    s->last_snare = snare_target;
    s->frame_count++;

    const int impact_i = (int)(s->impact_env * 256.0f);
    const int shock_i = (int)(s->shock_env * 256.0f);
    const int snare_i = (int)(s->snare_env * 256.0f);
    const int hat_i = (int)(s->hat_env * 256.0f);

    const int displace = clampi(displace_arg + ((impact_i * 46) >> 8) + ((shock_i * 54) >> 8), 0, 260);
    const int refraction = clampi(refraction_arg + ((impact_i * 42) >> 8), 0, 220);
    const int front_glow = clampi(glow_arg + ((snare_i * 120) >> 8), 0, 320);
    const int chroma_push = clampi(chroma_arg + ((hat_i * 24) >> 8) + ((snare_i * 16) >> 8), 0, 180);
    const int hat_mix = hat_i > 10 ? hat_i : 0;
    const int push_scale = displace * refraction;

    const int bow_curve = 18 + (geometry_arg >> 1);
    const int bow_shear = 28 + (geometry_arg >> 2);
    const int bow_side_cap = 384 + (width_arg << 2);
    const int torsion_twist = 48 + geometry_arg;
    const int map_limit = clampi(16 + (push_scale >> 8), 8, 160);
    const int signal_limit = 1024;

    const int swing_x = (int)lrintf(sinf(s->swing_phase) * (float)(geometry_arg >> 3));
    const int swing_y = (int)lrintf(cosf(s->swing_phase * 0.71f) * (float)(geometry_arg >> 4));

    int ax[BS_MAX_WAVES];
    int ay[BS_MAX_WAVES];
    int ap[BS_MAX_WAVES];
    int aw[BS_MAX_WAVES];
    int atail[BS_MAX_WAVES];
    int ascale[BS_MAX_WAVES];
    int apol[BS_MAX_WAVES];
    int adirx[BS_MAX_WAVES];
    int adiry[BS_MAX_WAVES];
    int nactive = 0;

    for(int i = 0; i < BS_MAX_WAVES; i++) {
        bowshock_wave_t *wv = &s->waves[i];

        if(!wv->active || wv->amp <= 0.0f)
            continue;

        const int width = clampi(wv->width, 4, 120);
        const int amp = (int)(wv->amp * 192.0f);

        if(amp <= 0)
            continue;

        ax[nactive] = wv->cx;
        ay[nactive] = wv->cy;
        ap[nactive] = (int)wv->pos;
        aw[nactive] = width;
        atail[nactive] = ap[nactive] - (width << 1);
        ascale[nactive] = (amp << 8) / width;
        apol[nactive] = wv->polarity;
        adirx[nactive] = wv->dir_x;
        adiry[nactive] = wv->dir_y;
        nactive++;
    }

    if(nactive <= 0 && swing_x == 0 && swing_y == 0)
        return;

#pragma omp parallel num_threads(threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            src_y[i] = Y[i];
            src_u[i] = U[i];
            src_v[i] = V[i];
        }

        if(nactive <= 0) {
#pragma omp for schedule(static)
            for(int y = 0; y < h; y++) {
                const int row = y * w;
                const int row_up = (y > 0 ? y - 1 : y) * w;
                const int row_dn = (y < h - 1 ? y + 1 : y) * w;

#pragma omp simd
                for(int x = 0; x < w; x++) {
                    const int i = row + x;
                    const int xm = x > 0 ? x - 1 : x;
                    const int xp = x < w - 1 ? x + 1 : x;

                    const int edge =
                        bs_absi((int)src_y[row + xp] - (int)src_y[row + xm]) +
                        bs_absi((int)src_y[row_dn + x] - (int)src_y[row_up + x]);

                    const int edge_gate = edge < 255 ? edge : 255;
                    const int swing_gate = 64 + (edge_gate >> 2);

                    int px = x + ((swing_x * swing_gate) >> 7);
                    int py = y + ((swing_y * swing_gate) >> 7);

                    px = px < 0 ? 0 : (px >= w ? w - 1 : px);
                    py = py < 0 ? 0 : (py >= h ? h - 1 : py);

                    const int pi = py * w + px;

                    Y[i] = src_y[pi];
                    U[i] = src_u[pi];
                    V[i] = src_v[pi];
                }
            }
        }
        else {
            if(mode_arg == BS_MODE_BOW) {
#pragma omp for schedule(static)
                for(int y = 0; y < h; y++) {
                    const int row = y * w;
                    BS_ROW_SWITCH(BS_ACCUM_BOW);
                }
            }
            else if(mode_arg == BS_MODE_TORSION) {
#pragma omp for schedule(static)
                for(int y = 0; y < h; y++) {
                    const int row = y * w;
                    BS_ROW_SWITCH(BS_ACCUM_TORSION);
                }
            }
            else {
#pragma omp for schedule(static)
                for(int y = 0; y < h; y++) {
                    const int row = y * w;
                    BS_ROW_SWITCH(BS_ACCUM_HYBRID);
                }
            }

#pragma omp for schedule(static)
            for(int y = 0; y < h; y++) {
                const int row = y * w;
                const int row_up = (y > 0 ? y - 1 : y) * w;
                const int row_dn = (y < h - 1 ? y + 1 : y) * w;
#pragma omp simd
                for(int x = 0; x < w; x++) {
                    const int i = row + x;
                    const int xm = x > 0 ? x - 1 : x;
                    const int xp = x < w - 1 ? x + 1 : x;

                    const int edge =
                        bs_absi((int)src_y[row + xp] - (int)src_y[row + xm]) +
                        bs_absi((int)src_y[row_dn + x] - (int)src_y[row_up + x]);

                    const int edge_gate = edge < 255 ? edge : 255;
                    const int pull_sum = map_pull[i];
                    const int swing_gate = 64 + ((edge_gate + pull_sum) >> 2);

                    int px = x + map_dx[i] + ((swing_x * swing_gate) >> 7);
                    int py = y + map_dy[i] + ((swing_y * swing_gate) >> 7);

                    px = px < 0 ? 0 : (px >= w ? w - 1 : px);
                    py = py < 0 ? 0 : (py >= h ? h - 1 : py);

                    const int pi = py * w + px;

                    int yy = src_y[pi];
                    int uu = src_u[pi];
                    int vv = src_v[pi];

                    const int wave_sum = map_wave[i];
                    const int glow_sum = map_glow[i];

                    yy += (glow_sum * front_glow) >> 8;
                    yy -= ((pull_sum - glow_sum) * front_glow) >> 11;
                    yy += (snare_i * glow_sum) >> 8;

                    const int edge_active = edge_gate > 22 ? edge_gate - 22 : 0;
                    const int pull_active = pull_sum > 255 ? 255 : pull_sum;
                    const int cp = (wave_sum * chroma_push) >> 8;
                    const int wake_cp = ((pull_sum - glow_sum) * chroma_push) >> 11;

                    yy += (hat_mix * edge_active * pull_active) >> 15;
                    uu += cp - wake_cp;
                    vv -= (cp >> 1) - (wake_cp >> 1);

                    Y[i] = bs_u8(yy);
                    U[i] = bs_u8(uu);
                    V[i] = bs_u8(vv);
                }
            }
        }
    }
}


#undef BS_ACCUM_BOW
#undef BS_ACCUM_TORSION
#undef BS_ACCUM_HYBRID
#undef BS_STORE_MAP
#undef BS_OMP_SIMD
#undef BS_ROW_SWITCH
