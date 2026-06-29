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
#include "pressurewave.h"

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include <veejaycore/vjmem.h>
#include <libvje/vje.h>

#ifndef PW_PI
#define PW_PI 3.14159265358979323846
#endif

#define PW_MAX_WAVES 4

enum {
    PW_DISPLACE = 0,
    PW_IMPACT,
    PW_SHOCKWAVE,
    PW_WAVE_WIDTH,
    PW_WAVE_SPEED,
    PW_REFRACTION,
    PW_FLOW_SWING,
    PW_CENTER_DRIFT,
    PW_RING_GLOW,
    PW_SNARE_FLASH,
    PW_HAT_SPARKLE,
    PW_CHROMA_PUSH,
    PW_DECAY,
    PW_NUM_PARAMS
};

typedef struct {
    float pos;
    float amp;
    float speed;
    int cx;
    int cy;
    int width;
    int polarity;
    int active;
} pressure_wave_t;

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

    pressure_wave_t waves[PW_MAX_WAVES];

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
} pressurewave_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t pw_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline int16_t pw_i16(int v)
{
    return (int16_t)clampi(v, -32768, 32767);
}

static inline int pw_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline uint32_t pw_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;

    return x;
}

static inline float pw_env(float oldv, float target, float attack, float release)
{
    return target > oldv
        ? oldv + (target - oldv) * attack
        : oldv + (target - oldv) * release;
}

static void pw_spawn_wave(pressurewave_t *s,
                          float impact,
                          float shock,
                          int width,
                          int speed,
                          int center_drift)
{
    const int w = s->w;
    const int h = s->h;

    pressure_wave_t *wv = &s->waves[s->wave_slot];
    s->wave_slot = (s->wave_slot + 1) & (PW_MAX_WAVES - 1);

    const uint32_t hv = pw_hash_u32(s->frame_count * 1664525U +
                                    ((uint32_t)(impact * 1000.0f) << 5) +
                                    ((uint32_t)(shock * 1000.0f) << 13));

    const int rx = (int)(hv & 255) - 128;
    const int ry = (int)((hv >> 8) & 255) - 128;

    const int jitter_x = (rx * w * center_drift) / (128 * 240);
    const int jitter_y = (ry * h * center_drift) / (128 * 240);

    wv->cx = clampi((w >> 1) + jitter_x, 0, w - 1);
    wv->cy = clampi((h >> 1) + jitter_y, 0, h - 1);
    wv->pos = 0.0f;
    wv->amp = clampf(0.62f + impact * 0.46f + shock * 0.62f, 0.0f, 1.55f);
    wv->speed = 2.2f + ((float)speed * 0.115f) + impact * 6.5f + shock * 10.0f;
    wv->width = clampi(width + (int)(shock * 18.0f), 4, 120);
    wv->polarity = (hv & 0x10000U) ? 1 : -1;
    wv->active = 1;
}

vj_effect *pressurewave_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = PW_NUM_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * PW_NUM_PARAMS);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * PW_NUM_PARAMS);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * PW_NUM_PARAMS);

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

    ve->description = "Pressure Wave";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[PW_DISPLACE] = 72;
    ve->defaults[PW_IMPACT] = 0;
    ve->defaults[PW_SHOCKWAVE] = 0;
    ve->defaults[PW_WAVE_WIDTH] = 34;
    ve->defaults[PW_WAVE_SPEED] = 42;
    ve->defaults[PW_REFRACTION] = 86;
    ve->defaults[PW_FLOW_SWING] = 28;
    ve->defaults[PW_CENTER_DRIFT] = 34;
    ve->defaults[PW_RING_GLOW] = 70;
    ve->defaults[PW_SNARE_FLASH] = 0;
    ve->defaults[PW_HAT_SPARKLE] = 42;
    ve->defaults[PW_CHROMA_PUSH] = 32;
    ve->defaults[PW_DECAY] = 74;

    ve->limits[0][PW_DISPLACE] = 0;     ve->limits[1][PW_DISPLACE] = 200;
    ve->limits[0][PW_IMPACT] = 0;       ve->limits[1][PW_IMPACT] = 100;
    ve->limits[0][PW_SHOCKWAVE] = 0;    ve->limits[1][PW_SHOCKWAVE] = 100;
    ve->limits[0][PW_WAVE_WIDTH] = 4;   ve->limits[1][PW_WAVE_WIDTH] = 96;
    ve->limits[0][PW_WAVE_SPEED] = 0;   ve->limits[1][PW_WAVE_SPEED] = 100;
    ve->limits[0][PW_REFRACTION] = 0;   ve->limits[1][PW_REFRACTION] = 160;
    ve->limits[0][PW_FLOW_SWING] = 0;   ve->limits[1][PW_FLOW_SWING] = 128;
    ve->limits[0][PW_CENTER_DRIFT] = 0; ve->limits[1][PW_CENTER_DRIFT] = 100;
    ve->limits[0][PW_RING_GLOW] = 0;    ve->limits[1][PW_RING_GLOW] = 200;
    ve->limits[0][PW_SNARE_FLASH] = 0;  ve->limits[1][PW_SNARE_FLASH] = 100;
    ve->limits[0][PW_HAT_SPARKLE] = 0;  ve->limits[1][PW_HAT_SPARKLE] = 200;
    ve->limits[0][PW_CHROMA_PUSH] = 0;  ve->limits[1][PW_CHROMA_PUSH] = 120;
    ve->limits[0][PW_DECAY] = 0;        ve->limits[1][PW_DECAY] = 100;

    ve->param_description = vje_build_param_list(
        PW_NUM_PARAMS,
        "Displace",
        "Impact Pulse",
        "Shockwave",
        "Wave Width",
        "Wave Speed",
        "Refraction",
        "Flow Swing",
        "Center Drift",
        "Ring Glow",
        "Snare Flash",
        "Hat Sparkle",
        "Chroma Push",
        "Decay"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_MOTION_REACT,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      54, 200, 42, 100, 180, 1300, 0,    96,
        VJ_BEAT_KICK,          VJ_BEAT_F_IMPULSE,                                                    0,  100, 100,100, 1,   130,  28,  240,
        VJ_BEAT_INTENSITY,     VJ_BEAT_F_IMPULSE,                                                    0,  100, 96, 100, 1,   180,  38,  220,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 12, 64,  18, 58,  900, 2800, 700,   46,
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      24, 100, 40, 100, 160, 1200, 0,    94,
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      62, 160, 38, 100, 180, 1400, 0,    92,
        VJ_BEAT_FLOW,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      18, 124, 38, 100, 220, 1600, 0,    86,
        VJ_BEAT_DRIFT,         VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,  4,  58,  10, 36, 1800, 5200, 1600,  28,
        VJ_BEAT_GLOW,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      44, 176, 36, 94,  200, 1600, 0,    86,
        VJ_BEAT_SNARE,         VJ_BEAT_F_IMPULSE,                                                    0,  100, 100,100, 1,   120,  24,  210,
        VJ_BEAT_HAT,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      28, 200, 34, 100, 120, 1000, 30,   82,
        VJ_BEAT_COLOR_AMOUNT,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      18, 120, 34, 96,  220, 1500, 0,    78,
        VJ_BEAT_MEMORY,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      56, 100, 30, 90,  700, 3200, 200,  68
    );
    return ve;
}

void *pressurewave_malloc(int w, int h)
{
    const int len = w * h;
    const size_t plane = (size_t)len;
    const size_t map_bytes = plane * sizeof(int16_t);
    const size_t total = sizeof(pressurewave_t) + plane * 3u + map_bytes * 5u + 64u;

    pressurewave_t *s = (pressurewave_t *)vj_calloc(total);

    if(!s)
        return NULL;

    s->w = w;
    s->h = h;
    s->len = len;
    s->n_threads = vje_advise_num_threads(len);

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

    s->wave_slot = 0;

    for(int i = 0; i < PW_MAX_WAVES; i++) {
        s->waves[i].cx = w >> 1;
        s->waves[i].cy = h >> 1;
        s->waves[i].width = 32;
        s->waves[i].polarity = 1;
    }

    return s;
}

void pressurewave_free(void *ptr)
{
    free(ptr);
}

#define PW_ACCUM_WAVE_NB(K) do {                                             \
    const int dx0 = x - ax[(K)];                                              \
    const int dy0 = y - ay[(K)];                                              \
    const int adx = dx0 < 0 ? -dx0 : dx0;                                     \
    const int ady = dy0 < 0 ? -dy0 : dy0;                                     \
    const int mx = adx > ady ? adx : ady;                                     \
    const int mn = adx > ady ? ady : adx;                                     \
    const int dist = mx + (mn >> 1);                                          \
    int front = aw[(K)] - pw_absi(dist - ap[(K)]);                            \
    int tail = aw[(K)] - pw_absi(dist - atail[(K)]);                          \
    front = front > 0 ? front : 0;                                            \
    tail = tail > 0 ? tail : 0;                                               \
    const int wr = (((front - (tail >> 1)) * ascale[(K)]) >> 8) * apol[(K)];   \
    const int push = (wr * push_scale) >> 15;                                 \
    dx_acc += (dx0 * push) >> 8;                                              \
    dy_acc += (dy0 * push) >> 8;                                              \
    wave_sum += wr;                                                           \
    pull_sum += wr < 0 ? -wr : wr;                                            \
    glow_sum += wr > 0 ? wr : (wr >> 2);                                      \
} while(0)

#define PW_STORE_MAP() do {                                                   \
    const int i = row + x;                                                    \
    map_dx[i] = pw_i16(dx_acc);                                               \
    map_dy[i] = pw_i16(dy_acc);                                               \
    map_wave[i] = pw_i16(wave_sum);                                           \
    map_glow[i] = pw_i16(glow_sum);                                           \
    map_pull[i] = pw_i16(pull_sum);                                           \
} while(0)

void pressurewave_apply(void *ptr, VJFrame *frame, int *args)
{
    pressurewave_t *s = (pressurewave_t *)ptr;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict src_y = s->src_y;
    uint8_t *restrict src_u = s->src_u;
    uint8_t *restrict src_v = s->src_v;

    int16_t *restrict map_dx = s->map_dx;
    int16_t *restrict map_dy = s->map_dy;
    int16_t *restrict map_wave = s->map_wave;
    int16_t *restrict map_glow = s->map_glow;
    int16_t *restrict map_pull = s->map_pull;

    const int w = s->w;
    const int h = s->h;
    const int len = s->len;
    const int threads = s->n_threads;

    const int displace_arg = args[PW_DISPLACE];
    const int impact_arg = args[PW_IMPACT];
    const int shock_arg = args[PW_SHOCKWAVE];
    const int width_arg = args[PW_WAVE_WIDTH];
    const int speed_arg = args[PW_WAVE_SPEED];
    const int refraction_arg = args[PW_REFRACTION];
    const int swing_arg = args[PW_FLOW_SWING];
    const int center_arg = args[PW_CENTER_DRIFT];
    const int glow_arg = args[PW_RING_GLOW];
    const int snare_arg = args[PW_SNARE_FLASH];
    const int hat_arg = args[PW_HAT_SPARKLE];
    const int chroma_arg = args[PW_CHROMA_PUSH];
    const int decay_arg = args[PW_DECAY];

    const float impact_target = (float)impact_arg * 0.01f;
    const float shock_target = (float)shock_arg * 0.01f;
    const float snare_target = (float)snare_arg * 0.01f;
    const float hat_target = clampf((float)hat_arg * (1.0f / 200.0f), 0.0f, 1.0f);

    const float impact_delta = impact_target - s->last_impact;
    const float shock_delta  = shock_target  - s->last_shock;
    const float snare_delta  = snare_target  - s->last_snare;

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

    s->impact_env = pw_env(s->impact_env, impact_target, 0.82f, 0.105f);
    s->shock_env = pw_env(s->shock_env, shock_target, 0.78f, 0.085f);
    s->snare_env = pw_env(s->snare_env, snare_target, 0.86f, 0.220f);
    s->hat_env = pw_env(s->hat_env, hat_target, 0.70f, 0.360f);

    if(impact_rise) {
        pw_spawn_wave(s, impact_target, shock_target, width_arg, speed_arg, center_arg);
        s->impact_cooldown = 3;
    }

    if(shock_rise && !impact_rise) {
        pw_spawn_wave(s, impact_target * 0.65f, shock_target, width_arg, speed_arg + 8, center_arg);
        s->shock_cooldown = 4;
    }

    if(snare_rise) {
        pw_spawn_wave(
            s,
            impact_target * 0.35f,
            shock_target * 0.42f + snare_target * 0.58f,
            width_arg >> 1,
            speed_arg + 12,
            center_arg
        );
        s->snare_cooldown = 3;
    }

    if(s->impact_cooldown > 0)
        s->impact_cooldown--;
    if(s->shock_cooldown > 0)
        s->shock_cooldown--;
    if(s->snare_cooldown > 0)
        s->snare_cooldown--;

    const float decay = 0.855f + ((float)decay_arg * 0.00125f);
    const float max_dist = (float)(w > h ? w : h) * 1.55f;

    for(int i = 0; i < PW_MAX_WAVES; i++) {
        pressure_wave_t *wv = &s->waves[i];

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
        ((float)swing_arg * 0.0009f) +
        s->impact_env * 0.050f +
        s->snare_env * 0.030f +
        s->hat_env * 0.012f;

    if(s->swing_phase > (float)(PW_PI * 2.0))
        s->swing_phase -= (float)(PW_PI * 2.0);

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
    const int ring_glow = clampi(glow_arg + ((snare_i * 120) >> 8), 0, 320);
    const int chroma_push = clampi(chroma_arg + ((hat_i * 24) >> 8) + ((snare_i * 16) >> 8), 0, 180);
    const int push_scale = displace * refraction;

    const int swing_x = (int)lrintf(sinf(s->swing_phase) * (float)(swing_arg >> 2));
    const int swing_y = (int)lrintf(cosf(s->swing_phase * 0.71f) * (float)(swing_arg >> 3));

    int ax[PW_MAX_WAVES];
    int ay[PW_MAX_WAVES];
    int ap[PW_MAX_WAVES];
    int aw[PW_MAX_WAVES];
    int atail[PW_MAX_WAVES];
    int ascale[PW_MAX_WAVES];
    int apol[PW_MAX_WAVES];
    int nactive = 0;

    for(int i = 0; i < PW_MAX_WAVES; i++) {
        pressure_wave_t *wv = &s->waves[i];

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

                for(int x = 0; x < w; x++) {
                    const int i = row + x;
                    const int xm = x > 0 ? x - 1 : x;
                    const int xp = x < w - 1 ? x + 1 : x;

                    const int edge =
                        pw_absi((int)src_y[row + xp] - (int)src_y[row + xm]) +
                        pw_absi((int)src_y[row_dn + x] - (int)src_y[row_up + x]);

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
#pragma omp for schedule(static)
            for(int y = 0; y < h; y++) {
                const int row = y * w;

                switch(nactive) {
                    case 1:
                        for(int x = 0; x < w; x++) {
                            int dx_acc = 0;
                            int dy_acc = 0;
                            int wave_sum = 0;
                            int glow_sum = 0;
                            int pull_sum = 0;
                            PW_ACCUM_WAVE_NB(0);
                            PW_STORE_MAP();
                        }
                        break;

                    case 2:
                        for(int x = 0; x < w; x++) {
                            int dx_acc = 0;
                            int dy_acc = 0;
                            int wave_sum = 0;
                            int glow_sum = 0;
                            int pull_sum = 0;
                            PW_ACCUM_WAVE_NB(0);
                            PW_ACCUM_WAVE_NB(1);
                            PW_STORE_MAP();
                        }
                        break;

                    case 3:
                        for(int x = 0; x < w; x++) {
                            int dx_acc = 0;
                            int dy_acc = 0;
                            int wave_sum = 0;
                            int glow_sum = 0;
                            int pull_sum = 0;
                            PW_ACCUM_WAVE_NB(0);
                            PW_ACCUM_WAVE_NB(1);
                            PW_ACCUM_WAVE_NB(2);
                            PW_STORE_MAP();
                        }
                        break;

                    default:
                        for(int x = 0; x < w; x++) {
                            int dx_acc = 0;
                            int dy_acc = 0;
                            int wave_sum = 0;
                            int glow_sum = 0;
                            int pull_sum = 0;
                            PW_ACCUM_WAVE_NB(0);
                            PW_ACCUM_WAVE_NB(1);
                            PW_ACCUM_WAVE_NB(2);
                            PW_ACCUM_WAVE_NB(3);
                            PW_STORE_MAP();
                        }
                        break;
                }
            }

#pragma omp for schedule(static)
            for(int y = 0; y < h; y++) {
                const int row = y * w;
                const int row_up = (y > 0 ? y - 1 : y) * w;
                const int row_dn = (y < h - 1 ? y + 1 : y) * w;
                const int spark_y = y * 17;
                const int frame_a = (int)s->frame_count * 23;
                const int frame_b = (int)s->frame_count * 19;

                for(int x = 0; x < w; x++) {
                    const int i = row + x;
                    const int xm = x > 0 ? x - 1 : x;
                    const int xp = x < w - 1 ? x + 1 : x;

                    const int edge =
                        pw_absi((int)src_y[row + xp] - (int)src_y[row + xm]) +
                        pw_absi((int)src_y[row_dn + x] - (int)src_y[row_up + x]);

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

                    yy += (glow_sum * ring_glow) >> 8;
                    yy -= ((pull_sum - glow_sum) * ring_glow) >> 11;
                    yy += (snare_i * glow_sum) >> 8;

                    if(hat_i > 10) {
                        const int edge_active = edge_gate > 22 ? edge_gate - 22 : 0;
                        const int pull_active = pull_sum > 2 ? 1 : 0;
                        const int pat = ((x * 13 + spark_y + frame_a) & 31) - 8;

                        yy += (pat * hat_i * edge_active * pull_active) >> 13;
                    }

                    if(chroma_push > 0) {
                        const int cp = (wave_sum * chroma_push) >> 8;

                        uu += cp;
                        vv -= cp >> 1;

                        if(hat_i > 12) {
                            const int edge_active = edge_gate > 36 ? edge_gate - 36 : 0;
                            const int pat = ((x * 11 + spark_y + frame_b) & 31) - 15;
                            const int flick = (pat * hat_i * edge_active) >> 12;

                            uu += flick;
                            vv -= flick >> 1;
                        }
                    }

                    Y[i] = pw_u8(yy);
                    U[i] = pw_u8(uu);
                    V[i] = pw_u8(vv);
                }
            }
        }
    }
}
#undef PW_ACCUM_WAVE_NB
#undef PW_STORE_MAP
