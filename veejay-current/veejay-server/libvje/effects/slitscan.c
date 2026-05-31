/*
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include "slitscan.h"

#define SS_PARAMS       12
#define SS_HISTORY_MAX  32
#define SS_HISTORY_MASK (SS_HISTORY_MAX - 1)
#define SS_GEOM_MODES   32

#define P_MODE         0
#define P_AMOUNT       1
#define P_DEPTH        2
#define P_SOURCE       3
#define P_SOURCE_GAIN  4
#define P_MOTION       5
#define P_TIME_MOTION  6
#define P_TIME_OFFSET  7
#define P_TIME_SCALE   8
#define P_TIME_BLEND   9
#define P_CHROMA      10
#define P_RESET       11

#define SS_MODE_ROWS              0
#define SS_MODE_COLUMNS           1
#define SS_MODE_DIAGONAL          2
#define SS_MODE_RADIAL            3
#define SS_MODE_SPIRAL            4
#define SS_MODE_WAVES             5
#define SS_MODE_CURTAINS          6
#define SS_MODE_TUNNEL            7
#define SS_MODE_RINGS             8
#define SS_MODE_ANGULAR           9
#define SS_MODE_DIAMOND          10
#define SS_MODE_CHECKER          11
#define SS_MODE_LISSAJOUS        12
#define SS_MODE_INTERFERENCE     13
#define SS_MODE_HORIZON          14
#define SS_MODE_ORGANIC          15
#define SS_MODE_FAN_BLADES       16
#define SS_MODE_POLAR_CHECKER    17
#define SS_MODE_SADDLE           18
#define SS_MODE_VORTEX_RINGS     19
#define SS_MODE_VENETIAN         20
#define SS_MODE_CRT_ROLL         21
#define SS_MODE_TOPOGRAPHIC      22
#define SS_MODE_CELLULAR_CRACKS  23
#define SS_MODE_ELLIPSE          24
#define SS_MODE_CROSS_FOLD       25
#define SS_MODE_OFFSET_RINGS     26
#define SS_MODE_NOISE_CURTAINS   27
#define SS_MODE_LIGHTNING        28
#define SS_MODE_PERSPECTIVE_GRID 29
#define SS_MODE_ORBIT_COMET      30
#define SS_MODE_BROKEN_TILES     31

#define SS_SRC_SHAPE       0
#define SS_SRC_LUMA        1
#define SS_SRC_MOTION      2
#define SS_SRC_LUMA_MOTION 3
#define SS_SRC_INV_LUMA    4

#define SS_TMOVE_FORWARD    0
#define SS_TMOVE_REVERSE    1
#define SS_TMOVE_DRIFT_FWD  2
#define SS_TMOVE_DRIFT_REV  3
#define SS_TMOVE_PULSE      4

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SS_SQRT_LUT_SIZE 8192
#define SS_ATAN_LUT_SIZE 4097
#define SS_ATAN_LUT_MAX  4096
#define SS_ANGLE_QUARTER 16384
#define SS_ANGLE_HALF    32768

static inline int ss_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float ss_clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float ss_absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

static inline uint8_t ss_blend_q8_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint16_t ss_u16_from_float(float v)
{
    int iv = (int)(v * 65535.0f + 0.5f);
    return (uint16_t)ss_clampi(iv, 0, 65535);
}

static inline uint16_t ss_triangle_u16(uint32_t v)
{
    v &= 65535u;
    return (uint16_t)((v < 32768u) ? (v << 1) : ((65535u - v) << 1));
}

static inline uint8_t ss_wave8(uint32_t v)
{
    v &= 65535u;
    return (uint8_t)((v < 32768u) ? (v >> 7) : ((65535u - v) >> 7));
}

static inline uint32_t ss_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static inline float ss_hash01_2d(int x, int y, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    return (float)(ss_hash_u32(h) & 65535u) * (1.0f / 65535.0f);
}

static inline float ss_smoothstep01(float x)
{
    return x * x * (3.0f - 2.0f * x);
}

static inline float ss_lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline float ss_value_noise(int x, int y, int scale, uint32_t seed)
{
    scale = (scale < 1) ? 1 : scale;

    const int xi = x / scale;
    const int yi = y / scale;
    const float fx = ss_smoothstep01((float)(x - xi * scale) / (float)scale);
    const float fy = ss_smoothstep01((float)(y - yi * scale) / (float)scale);

    const float a = ss_hash01_2d(xi,     yi,     seed);
    const float b = ss_hash01_2d(xi + 1, yi,     seed);
    const float c = ss_hash01_2d(xi,     yi + 1, seed);
    const float d = ss_hash01_2d(xi + 1, yi + 1, seed);
    const float ab = ss_lerpf(a, b, fx);
    const float cd = ss_lerpf(c, d, fx);
    return ss_lerpf(ab, cd, fy);
}

typedef struct {
    int w;
    int h;
    int len;
    int n_threads;
    int frame;
    int write_pos;
    int seeded;
    int last_reset;
    int last_depth;
    int last_mode;

    uint8_t *region;
    uint8_t *history;
    uint8_t *prev_y;
    uint16_t *geom;
    uint16_t *radial_lut;
    uint16_t *angle_lut;
    uint16_t time_lut[256];
    uint16_t sqrt_lut[SS_SQRT_LUT_SIZE];
    uint16_t atan_lut[SS_ATAN_LUT_SIZE];
    uint8_t *hist_y[SS_HISTORY_MAX];
    uint8_t *hist_u[SS_HISTORY_MAX];
    uint8_t *hist_v[SS_HISTORY_MAX];
} slitscan_t;

typedef struct {
    const uint16_t *geom;
    uint8_t *cur_y;
    uint8_t *cur_u;
    uint8_t *cur_v;
    uint8_t *out_y;
    uint8_t *out_u;
    uint8_t *out_v;
    const uint8_t *prev_y;
    int len;
    int write_slot;
    int amount_q8;
    int chroma_q8;
    int source;
    int source_gain_q8;
    int motion_mul_q8;
    int time_blend_q8;
    uint32_t phase_base;
    uint32_t scale_q16;
    uint32_t time_add;
    int time_reverse;
} ss_render_cfg;

static void ss_refresh_history_planes(slitscan_t *s)
{
    for(int k = 0; k < SS_HISTORY_MAX; k++) {
        const size_t base = (size_t)k * 3u * (size_t)s->len;
        s->hist_y[k] = s->history + base;
        s->hist_u[k] = s->hist_y[k] + s->len;
        s->hist_v[k] = s->hist_u[k] + s->len;
    }
}

static void ss_build_time_lut(slitscan_t *s, int depth)
{
    if(s->last_depth == depth) {
        return;
    }

    const int max_back_q8 = (depth > 1) ? ((depth - 1) << 8) : 0;
    for(int i = 0; i < 256; i++) {
        s->time_lut[i] = (uint16_t)((i * max_back_q8 + 127) / 255);
    }
    s->last_depth = depth;
}

static void ss_copy_current_to_slot(slitscan_t *s, VJFrame *frame, int slot)
{
    const int len = s->len;
    veejay_memcpy(s->hist_y[slot], frame->data[0], len);
    veejay_memcpy(s->hist_u[slot], frame->data[1], len);
    veejay_memcpy(s->hist_v[slot], frame->data[2], len);
}

static void ss_seed_history(slitscan_t *s, VJFrame *frame)
{
    for(int k = 0; k < SS_HISTORY_MAX; k++) {
        ss_copy_current_to_slot(s, frame, k);
    }
    veejay_memcpy(s->prev_y, frame->data[0], s->len);
    s->write_pos = 0;
    s->frame = 0;
    s->seeded = 1;
}


static void ss_build_math_luts(slitscan_t *s)
{
    const float inv_sqrt_max = 1.0f / (float)(SS_SQRT_LUT_SIZE - 1);
    for(int i = 0; i < SS_SQRT_LUT_SIZE; i++) {
        const float x = (float)i * inv_sqrt_max;
        s->sqrt_lut[i] = ss_u16_from_float(sqrtf(x));
    }

    const float atan_scale = 32768.0f / (float)M_PI;
    const float inv_atan_max = 1.0f / (float)SS_ATAN_LUT_MAX;
    for(int i = 0; i < SS_ATAN_LUT_SIZE; i++) {
        const float r = (float)i * inv_atan_max;
        int a = (int)(atanf(r) * atan_scale + 0.5f);
        s->atan_lut[i] = (uint16_t)ss_clampi(a, 0, SS_ANGLE_QUARTER);
    }
}

static inline uint16_t ss_sqrt_norm_u16(const slitscan_t *s, float norm2)
{
    norm2 = ss_clampf(norm2, 0.0f, 1.0f);
    const int idx = ss_clampi((int)(norm2 * (float)(SS_SQRT_LUT_SIZE - 1) + 0.5f), 0, SS_SQRT_LUT_SIZE - 1);
    return s->sqrt_lut[idx];
}

static inline float ss_sqrt_norm_f(const slitscan_t *s, float norm2)
{
    return (float)ss_sqrt_norm_u16(s, norm2) * (1.0f / 65535.0f);
}

static inline uint16_t ss_sqrt_ratio_u16(const slitscan_t *s, uint64_t d2, uint64_t max_d2)
{
    if(max_d2 == 0u) {
        return 0;
    }

    d2 = (d2 > max_d2) ? max_d2 : d2;
    const uint64_t idx64 = (d2 * (uint64_t)(SS_SQRT_LUT_SIZE - 1) + (max_d2 >> 1)) / max_d2;
    return s->sqrt_lut[(int)idx64];
}

static inline uint16_t ss_angle_from_dxy2_u16(const slitscan_t *s, int dx2, int dy2)
{
    const uint32_t ax = (uint32_t)((dx2 < 0) ? -dx2 : dx2);
    const uint32_t ay = (uint32_t)((dy2 < 0) ? -dy2 : dy2);

    if((ax | ay) == 0u) {
        return SS_ANGLE_HALF;
    }

    uint32_t q;
    if(ax >= ay) {
        const uint32_t r = (ax > 0u) ? (uint32_t)(((uint64_t)ay * SS_ATAN_LUT_MAX + (ax >> 1)) / ax) : 0u;
        q = s->atan_lut[(int)r];
    } else {
        const uint32_t r = (ay > 0u) ? (uint32_t)(((uint64_t)ax * SS_ATAN_LUT_MAX + (ay >> 1)) / ay) : 0u;
        q = (uint32_t)SS_ANGLE_QUARTER - (uint32_t)s->atan_lut[(int)r];
    }

    const int theta = (dx2 >= 0)
        ? ((dy2 >= 0) ? (int)q : -(int)q)
        : ((dy2 >= 0) ? (SS_ANGLE_HALF - (int)q) : ((int)q - SS_ANGLE_HALF));

    return (uint16_t)((theta + SS_ANGLE_HALF) & 65535);
}

static void ss_build_center_polar_luts(slitscan_t *s)
{
    const int w = s->w;
    const int h = s->h;
    const int max_dx2 = (w > 1) ? (w - 1) : 1;
    const int max_dy2 = (h > 1) ? (h - 1) : 1;
    const uint64_t max_d2 = (uint64_t)max_dx2 * (uint64_t)max_dx2 +
                            (uint64_t)max_dy2 * (uint64_t)max_dy2;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        const int dy2 = (y << 1) - (h - 1);
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const int dx2 = (x << 1) - (w - 1);
            const uint64_t d2 = (uint64_t)(dx2 * dx2) + (uint64_t)(dy2 * dy2);
            s->radial_lut[idx] = ss_sqrt_ratio_u16(s, d2, max_d2);
            s->angle_lut[idx] = ss_angle_from_dxy2_u16(s, dx2, dy2);
        }
    }
}

static inline uint32_t ss_geom_time_u16(uint16_t g, const ss_render_cfg *cfg)
{
    uint32_t v = ((uint32_t)(((uint64_t)g * cfg->scale_q16) >> 16) + cfg->phase_base + cfg->time_add) & 65535u;
    return cfg->time_reverse ? (65535u - v) : v;
}

static inline int ss_motion_value_q8(const ss_render_cfg *cfg, int idx, int cy)
{
    const int d = cy - (int)cfg->prev_y[idx];
    const int diff = (d < 0) ? -d : d;
    const int raw_mv = (diff * cfg->motion_mul_q8 + 128) >> 8;
    return (raw_mv > 255) ? 255 : raw_mv;
}

static inline int ss_source_time_value(const ss_render_cfg *cfg, int idx, int t)
{
    const int cy = cfg->cur_y[idx];
    const int sv = (cfg->source == SS_SRC_LUMA)
        ? cy
        : ((cfg->source == SS_SRC_INV_LUMA)
            ? (255 - cy)
            : ((cfg->source == SS_SRC_MOTION)
                ? ss_motion_value_q8(cfg, idx, cy)
                : ((cy + ss_motion_value_q8(cfg, idx, cy)) >> 1)));

    t += ((sv - t) * cfg->source_gain_q8 + 128) >> 8;
    return (t < 0) ? 0 : ((t > 255) ? 255 : t);
}

static void ss_render_shape_hard(slitscan_t *s, const ss_render_cfg *cfg)
{
#pragma omp for schedule(static)
    for(int idx = 0; idx < cfg->len; idx++) {
        const uint32_t g16 = ss_geom_time_u16(cfg->geom[idx], cfg);
        const uint16_t tq = s->time_lut[g16 >> 8];
        const int back = tq >> 8;
        const int slot = (cfg->write_slot - back) & SS_HISTORY_MASK;

        const uint8_t sy = s->hist_y[slot][idx];
        const uint8_t su = s->hist_u[slot][idx];
        const uint8_t sv = s->hist_v[slot][idx];

        cfg->out_y[idx] = ss_blend_q8_u8(cfg->cur_y[idx], sy, cfg->amount_q8);
        cfg->out_u[idx] = ss_blend_q8_u8(cfg->cur_u[idx], su, cfg->chroma_q8);
        cfg->out_v[idx] = ss_blend_q8_u8(cfg->cur_v[idx], sv, cfg->chroma_q8);
    }
}

static void ss_render_shape_soft(slitscan_t *s, const ss_render_cfg *cfg)
{
#pragma omp for schedule(static)
    for(int idx = 0; idx < cfg->len; idx++) {
        const uint32_t g16 = ss_geom_time_u16(cfg->geom[idx], cfg);
        const uint16_t tq = s->time_lut[g16 >> 8];
        const int back0 = tq >> 8;
        const int frac = tq & 255;
        int back1 = back0 + 1;
        back1 = (back1 >= SS_HISTORY_MAX) ? (SS_HISTORY_MAX - 1) : back1;

        const int slot0 = (cfg->write_slot - back0) & SS_HISTORY_MASK;
        const int slot1 = (cfg->write_slot - back1) & SS_HISTORY_MASK;
        const int q = ((slot1 != slot0) ? ((frac * cfg->time_blend_q8 + 128) >> 8) : 0);
        const int iq = 256 - q;

        const int sy0 = s->hist_y[slot0][idx];
        const int su0 = s->hist_u[slot0][idx];
        const int sv0 = s->hist_v[slot0][idx];

        const int sy = ((sy0 * iq) + ((int)s->hist_y[slot1][idx] * q) + 128) >> 8;
        const int su = ((su0 * iq) + ((int)s->hist_u[slot1][idx] * q) + 128) >> 8;
        const int sv = ((sv0 * iq) + ((int)s->hist_v[slot1][idx] * q) + 128) >> 8;

        cfg->out_y[idx] = ss_blend_q8_u8(cfg->cur_y[idx], (uint8_t)sy, cfg->amount_q8);
        cfg->out_u[idx] = ss_blend_q8_u8(cfg->cur_u[idx], (uint8_t)su, cfg->chroma_q8);
        cfg->out_v[idx] = ss_blend_q8_u8(cfg->cur_v[idx], (uint8_t)sv, cfg->chroma_q8);
    }
}

static void ss_render_source_hard(slitscan_t *s, const ss_render_cfg *cfg)
{
#pragma omp for schedule(static)
    for(int idx = 0; idx < cfg->len; idx++) {
        const uint32_t g16 = ss_geom_time_u16(cfg->geom[idx], cfg);
        int t = (int)(g16 >> 8);
        t = ss_source_time_value(cfg, idx, t);

        const uint16_t tq = s->time_lut[t];
        const int back = tq >> 8;
        const int slot = (cfg->write_slot - back) & SS_HISTORY_MASK;

        const uint8_t sy = s->hist_y[slot][idx];
        const uint8_t su = s->hist_u[slot][idx];
        const uint8_t sv = s->hist_v[slot][idx];

        cfg->out_y[idx] = ss_blend_q8_u8(cfg->cur_y[idx], sy, cfg->amount_q8);
        cfg->out_u[idx] = ss_blend_q8_u8(cfg->cur_u[idx], su, cfg->chroma_q8);
        cfg->out_v[idx] = ss_blend_q8_u8(cfg->cur_v[idx], sv, cfg->chroma_q8);
    }
}

static void ss_render_source_soft(slitscan_t *s, const ss_render_cfg *cfg)
{
#pragma omp for schedule(static)
    for(int idx = 0; idx < cfg->len; idx++) {
        const uint32_t g16 = ss_geom_time_u16(cfg->geom[idx], cfg);
        int t = (int)(g16 >> 8);
        t = ss_source_time_value(cfg, idx, t);

        const uint16_t tq = s->time_lut[t];
        const int back0 = tq >> 8;
        const int frac = tq & 255;
        int back1 = back0 + 1;
        back1 = (back1 >= SS_HISTORY_MAX) ? (SS_HISTORY_MAX - 1) : back1;

        const int slot0 = (cfg->write_slot - back0) & SS_HISTORY_MASK;
        const int slot1 = (cfg->write_slot - back1) & SS_HISTORY_MASK;
        const int q = ((slot1 != slot0) ? ((frac * cfg->time_blend_q8 + 128) >> 8) : 0);
        const int iq = 256 - q;

        const int sy0 = s->hist_y[slot0][idx];
        const int su0 = s->hist_u[slot0][idx];
        const int sv0 = s->hist_v[slot0][idx];

        const int sy = ((sy0 * iq) + ((int)s->hist_y[slot1][idx] * q) + 128) >> 8;
        const int su = ((su0 * iq) + ((int)s->hist_u[slot1][idx] * q) + 128) >> 8;
        const int sv = ((sv0 * iq) + ((int)s->hist_v[slot1][idx] * q) + 128) >> 8;

        cfg->out_y[idx] = ss_blend_q8_u8(cfg->cur_y[idx], (uint8_t)sy, cfg->amount_q8);
        cfg->out_u[idx] = ss_blend_q8_u8(cfg->cur_u[idx], (uint8_t)su, cfg->chroma_q8);
        cfg->out_v[idx] = ss_blend_q8_u8(cfg->cur_v[idx], (uint8_t)sv, cfg->chroma_q8);
    }
}

typedef void (*ss_render_fn)(slitscan_t *s, const ss_render_cfg *cfg);

typedef struct {
    float cx;
    float cy;
    float inv_w;
    float inv_h;
    float inv_d;
    float inv_maxr2;
    float inv_manhattan;
    float inv_2pi;
    float two_pi;
    int min_wh;
    int max_wh;
    int checker_tile;
    int small_tile;
    int large_tile;
    int noise_s0;
    int noise_s1;
    int noise_s2;
} ss_geom_ctx;

typedef void (*ss_geom_builder_fn)(slitscan_t *s, const ss_geom_ctx *c);

static void ss_build_geom_rows(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            v = (uint16_t)ss_clampi((int)((float)y * c->inv_h + 0.5f), 0, 65535);

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_columns(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            v = (uint16_t)ss_clampi((int)((float)x * c->inv_w + 0.5f), 0, 65535);

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_diagonal(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            v = (uint16_t)ss_clampi((int)((float)(x + y) * c->inv_d + 0.5f), 0, 65535);

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_radial(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;
    (void)c;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            s->geom[idx] = s->radial_lut[idx];
        }
    }
}

static void ss_build_geom_spiral(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;
    (void)c;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const uint32_t rad = (uint32_t)s->radial_lut[idx];
            const uint32_t ang = (uint32_t)s->angle_lut[idx];
            s->geom[idx] = (uint16_t)(((rad * 3u) + (ang * 2u)) & 65535u);
        }
    }
}

static void ss_build_geom_waves(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                const uint16_t row = (uint16_t)ss_clampi((int)((float)y * c->inv_h + 0.5f), 0, 65535);
                                const uint16_t col = (uint16_t)ss_clampi((int)((float)x * c->inv_w + 0.5f), 0, 65535);
                                const uint16_t dia = (uint16_t)ss_clampi((int)((float)(x + y) * c->inv_d + 0.5f), 0, 65535);
                                v = (uint16_t)((((uint32_t)ss_wave8(((uint32_t)col * 5u) + ((uint32_t)row * 2u)) << 8) | ss_wave8(((uint32_t)dia * 7u))) & 65535u);

                            }

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_curtains(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                const uint16_t col = (uint16_t)ss_clampi((int)((float)x * c->inv_w + 0.5f), 0, 65535);
                                const int d = (int)col - 32768;
                                v = (uint16_t)(((d < 0) ? -d : d) * 2);

                            }

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_tunnel(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;
    (void)c;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const uint32_t rad = (uint32_t)s->radial_lut[idx];
            const uint32_t ang = (uint32_t)s->angle_lut[idx];
            s->geom[idx] = (uint16_t)(((rad * 4u) + ang) & 65535u);
        }
    }
}

static void ss_build_geom_rings(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;
    (void)c;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const uint32_t rad = (uint32_t)s->radial_lut[idx];
            s->geom[idx] = ss_triangle_u16(rad * 9u);
        }
    }
}

static void ss_build_geom_angular(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;
    (void)c;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            s->geom[idx] = s->angle_lut[idx];
        }
    }
}

static void ss_build_geom_diamond(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const int dx2 = (x << 1) - (w - 1);
            const int dy2 = (y << 1) - (h - 1);
            const int ax2 = (dx2 < 0) ? -dx2 : dx2;
            const int ay2 = (dy2 < 0) ? -dy2 : dy2;
            const float manhattan = ((float)(ax2 + ay2) * 0.5f) * c->inv_manhattan;
            s->geom[idx] = (uint16_t)ss_clampi((int)(manhattan + 0.5f), 0, 65535);
        }
    }
}

static void ss_build_geom_checker(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                const int tx = x / c->checker_tile;
                                const int ty = y / c->checker_tile;
                                const int lx = x - tx * c->checker_tile;
                                const int ly = y - ty * c->checker_tile;
                                const int local = ((lx + ly) * 65535) / (c->checker_tile * 2);
                                v = (uint16_t)(((tx ^ ty) & 1) ? (65535 - local) : local);

                            }

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_lissajous(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                const float nx = (w > 1) ? ((float)x / (float)(w - 1)) : 0.0f;
                                const float ny = (h > 1) ? ((float)y / (float)(h - 1)) : 0.0f;
                                const float a = sinf(c->two_pi * (3.0f * nx + 0.35f * ny));
                                const float b = cosf(c->two_pi * (2.0f * ny - 0.20f * nx));
                                const float cv = sinf(c->two_pi * (nx + ny) * 1.5f);
                                v = ss_u16_from_float(ss_clampf(0.5f + 0.25f * a + 0.18f * b + 0.07f * cv, 0.0f, 1.0f));

                            }

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_interference(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const float fx = (float)x;
            const float fy = (float)y;
            const float x1 = c->cx * 0.35f;
            const float y1 = c->cy * 0.55f;
            const float x2 = c->cx * 1.65f;
            const float y2 = c->cy * 0.85f;
            const float x3 = c->cx * 1.05f;
            const float y3 = c->cy * 1.55f;
            const float dx1 = fx - x1;
            const float dy1 = fy - y1;
            const float dx2 = fx - x2;
            const float dy2 = fy - y2;
            const float dx3 = fx - x3;
            const float dy3 = fy - y3;
            const float r1 = ss_sqrt_norm_f(s, (dx1 * dx1 + dy1 * dy1) * c->inv_maxr2);
            const float r2 = ss_sqrt_norm_f(s, (dx2 * dx2 + dy2 * dy2) * c->inv_maxr2);
            const float r3 = ss_sqrt_norm_f(s, (dx3 * dx3 + dy3 * dy3) * c->inv_maxr2);
            const float n = 0.5f + 0.20f * sinf(c->two_pi * 7.0f * r1) + 0.18f * sinf(c->two_pi * 9.0f * r2) + 0.12f * cosf(c->two_pi * 5.0f * r3);
            s->geom[idx] = ss_u16_from_float(ss_clampf(n, 0.0f, 1.0f));
        }
    }
}

static void ss_build_geom_horizon(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;
    (void)c;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const float ny = (h > 1) ? ((float)y / (float)(h - 1)) : 0.0f;
            s->geom[idx] = ss_u16_from_float(ss_sqrt_norm_f(s, ny));
        }
    }
}

static void ss_build_geom_organic(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                float n0 = ss_value_noise(x, y, c->noise_s0, 11u);
                                float n1 = ss_value_noise(x, y, c->noise_s1, 47u);
                                float n2 = ss_value_noise(x, y, c->noise_s2, 89u);
                                float n = n0 * 0.55f + n1 * 0.30f + n2 * 0.15f;
                                const uint16_t row = (uint16_t)ss_clampi((int)((float)y * c->inv_h + 0.5f), 0, 65535);
                                const uint16_t col = (uint16_t)ss_clampi((int)((float)x * c->inv_w + 0.5f), 0, 65535);
                                n = n * 0.78f + ((float)(((uint32_t)row + ((uint32_t)col * 3u)) & 65535u) * (1.0f / 65535.0f)) * 0.22f;
                                v = ss_u16_from_float(ss_clampf(n, 0.0f, 1.0f));

                            }

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_fan_blades(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;
    (void)c;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const uint32_t ang = (uint32_t)s->angle_lut[idx];
            const uint32_t rad = (uint32_t)s->radial_lut[idx];
            s->geom[idx] = ss_triangle_u16((ang * 12u) + (rad * 2u));
        }
    }
}



static void ss_build_geom_polar_checker(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;
    (void)c;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const uint32_t ang = (uint32_t)s->angle_lut[idx];
            const uint32_t rad = (uint32_t)s->radial_lut[idx];
            const uint32_t ring = rad / 5461u;
            const uint32_t sector = ang / 4096u;
            const uint32_t local = ((rad * 5u) + (ang * 3u)) & 65535u;
            s->geom[idx] = (uint16_t)(((ring ^ sector) & 1u) ? (65535u - local) : local);
        }
    }
}

static void ss_build_geom_saddle(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                const float nx = ((float)x - c->cx) / ((c->cx > 0.0f) ? c->cx : 1.0f);
                                const float ny = ((float)y - c->cy) / ((c->cy > 0.0f) ? c->cy : 1.0f);
                                const float sdl = 0.5f + 0.5f * (nx * ny);
                                const float fold = 0.5f + 0.25f * (nx * nx - ny * ny);
                                v = ss_u16_from_float(ss_clampf(sdl * 0.72f + fold * 0.28f, 0.0f, 1.0f));

                            }

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_vortex_rings(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const float rn = (float)s->radial_lut[idx] * (1.0f / 65535.0f);
            const float an = (float)s->angle_lut[idx] * (1.0f / 65535.0f);
            const float n = 0.5f + 0.38f * sinf(c->two_pi * (rn * 7.0f + an * 2.0f)) + 0.12f * cosf(c->two_pi * (rn * 13.0f - an * 3.0f));
            s->geom[idx] = ss_u16_from_float(ss_clampf(n, 0.0f, 1.0f));
        }
    }
}



static void ss_build_geom_venetian(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                const int band = y / c->small_tile;
                                const int local = y - band * c->small_tile;
                                const int pos = (local * 65535) / c->small_tile;
                                v = (uint16_t)((band & 1) ? (65535 - pos) : pos);

                            }

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_crt_roll(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                const uint32_t row = (uint32_t)ss_clampi((int)((float)y * c->inv_h + 0.5f), 0, 65535);
                                const int band_h = ss_clampi(h / 12, 8, 96);
                                const int b = y / band_h;
                                const int local = y - b * band_h;
                                const uint32_t sync = (local < 3) ? 65535u : 0u;
                                const uint32_t wobble = (uint32_t)ss_wave8(row * 9u + (uint32_t)(b * 7919)) << 8;
                                v = (uint16_t)((row + (wobble >> 2) + sync) & 65535u);

                            }

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_topographic(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;
    (void)c;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const uint32_t rad = (uint32_t)s->radial_lut[idx];
            const uint32_t wave = ((uint32_t)ss_wave8(((uint32_t)x * 257u) + ((uint32_t)y * 389u)) << 8);
            const uint32_t q = ((rad + (wave >> 3)) / 4096u) * 4096u;
            s->geom[idx] = (uint16_t)(q & 65535u);
        }
    }
}

static void ss_build_geom_cellular_cracks(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                const int gx = x / c->large_tile;
                                const int gy = y / c->large_tile;
                                const int lx = x - gx * c->large_tile;
                                const int ly = y - gy * c->large_tile;
                                const int edge = ss_clampi((lx < ly ? lx : ly), 0, c->large_tile);
                                const int edge2 = ss_clampi(((c->large_tile - lx) < (c->large_tile - ly) ? (c->large_tile - lx) : (c->large_tile - ly)), 0, c->large_tile);
                                const int crack = (edge < edge2) ? edge : edge2;
                                const uint32_t h0 = ss_hash_u32((uint32_t)gx * 92837111u ^ (uint32_t)gy * 689287499u ^ 0x51ed270bu);
                                const uint32_t base = h0 & 65535u;
                                const uint32_t cv = (uint32_t)ss_clampi((crack * 65535) / c->large_tile, 0, 65535);
                                v = (uint16_t)((base + (65535u - cv) * 2u) & 65535u);

                            }

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_ellipse(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const float fx = ((float)x - c->cx) / ((c->cx > 0.0f) ? c->cx : 1.0f);
            const float fy = ((float)y - c->cy) / ((c->cy > 0.0f) ? c->cy : 1.0f);
            const float e = ss_sqrt_norm_f(s, fx * fx * 0.72f + fy * fy * 1.45f);
            s->geom[idx] = ss_u16_from_float(e);
        }
    }
}

static void ss_build_geom_cross_fold(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;
    (void)c;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const int dx2 = (x << 1) - (w - 1);
            const int dy2 = (y << 1) - (h - 1);
            const int ax2 = (dx2 < 0) ? -dx2 : dx2;
            const int ay2 = (dy2 < 0) ? -dy2 : dy2;
            const float ax = (w > 1) ? ((float)ax2 / (float)(w - 1)) : 0.0f;
            const float ay = (h > 1) ? ((float)ay2 / (float)(h - 1)) : 0.0f;
            const float cross = ss_clampf(((ax < ay) ? ax : ay) * 1.55f, 0.0f, 1.0f);
            const float diamond = ss_clampf((ax + ay) * 0.5f, 0.0f, 1.0f);
            s->geom[idx] = ss_u16_from_float(cross * 0.65f + diamond * 0.35f);
        }
    }
}



static void ss_build_geom_offset_rings(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const float ox = c->cx + 0.22f * (float)w;
            const float oy = c->cy - 0.13f * (float)h;
            const float fx = (float)x - ox;
            const float fy = (float)y - oy;
            const uint32_t rad = (uint32_t)ss_sqrt_norm_u16(s, (fx * fx + fy * fy) * c->inv_maxr2);
            s->geom[idx] = ss_triangle_u16(rad * 11u + (uint32_t)((float)x * c->inv_w));
        }
    }
}



static void ss_build_geom_noise_curtains(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                const float n0 = ss_value_noise(x, y, c->noise_s1, 123u);
                                const float col = (w > 1) ? ((float)x / (float)(w - 1)) : 0.0f;
                                const float stripe = 0.5f + 0.5f * sinf(c->two_pi * (col * 8.0f + n0 * 0.85f));
                                const float row = (h > 1) ? ((float)y / (float)(h - 1)) : 0.0f;
                                v = ss_u16_from_float(ss_clampf(stripe * 0.78f + row * 0.22f, 0.0f, 1.0f));

                            }

            s->geom[idx] = v;
        }
    }
}

static void ss_build_geom_lightning(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const float ny = (h > 1) ? ((float)y / (float)(h - 1)) : 0.0f;
            const float jag0 = ss_value_noise(0, y, c->noise_s2, 179u) - 0.5f;
            const float jag1 = ss_value_noise(17, y, c->noise_s1, 313u) - 0.5f;
            const float center = c->cx + (jag0 * 0.38f + jag1 * 0.17f) * (float)w;
            const float d = (float)x - center;
            const float dist = ((d < 0.0f) ? -d : d) / ((float)w * 0.5f);
            const float bolt = ss_clampf(1.0f - dist * 2.6f, 0.0f, 1.0f);
            s->geom[idx] = ss_u16_from_float(ss_clampf(ny * 0.58f + bolt * 0.42f, 0.0f, 1.0f));
        }
    }
}



static void ss_build_geom_perspective_grid(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const float ny = (h > 1) ? ((float)y / (float)(h - 1)) : 0.0f;
            const float px = ((float)x - c->cx) / ((float)c->max_wh + 1.0f);
            const float py = ny + 0.04f;
            const float gx = fmodf(px / py * 9.0f, 1.0f) - 0.5f;
            const float gy = fmodf((1.0f / py) * 0.55f, 1.0f) - 0.5f;
            const float gridx = ((gx < 0.0f) ? -gx : gx) * 2.0f;
            const float gridy = ((gy < 0.0f) ? -gy : gy) * 2.0f;
            const float grid = (gridx < gridy) ? gridx : gridy;
            s->geom[idx] = ss_u16_from_float(ss_clampf(ny * 0.75f + (1.0f - grid) * 0.25f, 0.0f, 1.0f));
        }
    }
}



static void ss_build_geom_orbit_comet(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const float rn = (float)s->radial_lut[idx] * (1.0f / 65535.0f);
            const float an = (float)s->angle_lut[idx] * (1.0f / 65535.0f);
            const float arm_s = sinf(c->two_pi * (an * 2.0f + rn * 1.7f));
            const float arm = (arm_s < 0.0f) ? -arm_s : arm_s;
            const float falloff = ss_clampf(1.0f - rn, 0.0f, 1.0f);
            s->geom[idx] = ss_u16_from_float(ss_clampf((an * 0.45f) + (rn * 0.35f) + (arm * falloff * 0.35f), 0.0f, 1.0f));
        }
    }
}

static void ss_build_geom_broken_tiles(slitscan_t *s, const ss_geom_ctx *c)
{
    const int w = s->w;
    const int h = s->h;

#pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int idx = y * w + x;
            uint16_t v = 0;

            {
                                const int tx = x / c->checker_tile;
                                const int ty = y / c->checker_tile;
                                const int lx = x - tx * c->checker_tile;
                                const int ly = y - ty * c->checker_tile;
                                const uint32_t h0 = ss_hash_u32((uint32_t)tx * 73856093u ^ (uint32_t)ty * 19349663u ^ 0xb5297a4du);
                                const uint32_t base = h0 & 65535u;
                                const uint32_t local = (uint32_t)(((lx * 257) ^ (ly * 521)) & 65535);
                                v = (uint16_t)((base + local) & 65535u);

                            }

            s->geom[idx] = v;
        }
    }
}
static const ss_geom_builder_fn ss_geom_builders[SS_GEOM_MODES] = {
    ss_build_geom_rows,
    ss_build_geom_columns,
    ss_build_geom_diagonal,
    ss_build_geom_radial,
    ss_build_geom_spiral,
    ss_build_geom_waves,
    ss_build_geom_curtains,
    ss_build_geom_tunnel,
    ss_build_geom_rings,
    ss_build_geom_angular,
    ss_build_geom_diamond,
    ss_build_geom_checker,
    ss_build_geom_lissajous,
    ss_build_geom_interference,
    ss_build_geom_horizon,
    ss_build_geom_organic,
    ss_build_geom_fan_blades,
    ss_build_geom_polar_checker,
    ss_build_geom_saddle,
    ss_build_geom_vortex_rings,
    ss_build_geom_venetian,
    ss_build_geom_crt_roll,
    ss_build_geom_topographic,
    ss_build_geom_cellular_cracks,
    ss_build_geom_ellipse,
    ss_build_geom_cross_fold,
    ss_build_geom_offset_rings,
    ss_build_geom_noise_curtains,
    ss_build_geom_lightning,
    ss_build_geom_perspective_grid,
    ss_build_geom_orbit_comet,
    ss_build_geom_broken_tiles
};

static void ss_build_geom(slitscan_t *s, int mode)
{
    mode = ss_clampi(mode, 0, SS_GEOM_MODES - 1);
    if(s->last_mode == mode) {
        return;
    }

    const int w = s->w;
    const int h = s->h;
    const float cx = (float)(w - 1) * 0.5f;
    const float cy = (float)(h - 1) * 0.5f;
    const int min_wh = (w < h) ? w : h;
    const int max_wh = (w > h) ? w : h;
    const float maxr2 = cx * cx + cy * cy;

    const ss_geom_ctx c = {
        cx,
        cy,
        (w > 1) ? (65535.0f / (float)(w - 1)) : 0.0f,
        (h > 1) ? (65535.0f / (float)(h - 1)) : 0.0f,
        ((w + h) > 2) ? (65535.0f / (float)(w + h - 2)) : 0.0f,
        (maxr2 > 0.0f) ? (1.0f / maxr2) : 0.0f,
        ((cx + cy) > 0.0f) ? (65535.0f / (cx + cy)) : 0.0f,
        1.0f / (2.0f * (float)M_PI),
        2.0f * (float)M_PI,
        min_wh,
        max_wh,
        ss_clampi(min_wh / 10, 8, 96),
        ss_clampi(min_wh / 18, 6, 64),
        ss_clampi(min_wh / 7, 16, 160),
        ss_clampi(min_wh / 7, 12, 192),
        ss_clampi(min_wh / 15, 8, 96),
        ss_clampi(min_wh / 31, 4, 48)
    };

    ss_geom_builders[mode](s, &c);
    s->last_mode = mode;
}

vj_effect *slitscan_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = SS_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = SS_GEOM_MODES - 1;
    ve->defaults[P_MODE] = SS_MODE_ROWS;

    ve->limits[0][P_AMOUNT] = 0;
    ve->limits[1][P_AMOUNT] = 100;
    ve->defaults[P_AMOUNT] = 100;

    ve->limits[0][P_DEPTH] = 1;
    ve->limits[1][P_DEPTH] = SS_HISTORY_MAX;
    ve->defaults[P_DEPTH] = 16;

    ve->limits[0][P_SOURCE] = 0;
    ve->limits[1][P_SOURCE] = 4;
    ve->defaults[P_SOURCE] = SS_SRC_SHAPE;

    ve->limits[0][P_SOURCE_GAIN] = 0;
    ve->limits[1][P_SOURCE_GAIN] = 100;
    ve->defaults[P_SOURCE_GAIN] = 55;

    ve->limits[0][P_MOTION] = 0;
    ve->limits[1][P_MOTION] = 100;
    ve->defaults[P_MOTION] = 55;

    ve->limits[0][P_TIME_MOTION] = 0;
    ve->limits[1][P_TIME_MOTION] = 4;
    ve->defaults[P_TIME_MOTION] = SS_TMOVE_FORWARD;

    ve->limits[0][P_TIME_OFFSET] = 0;
    ve->limits[1][P_TIME_OFFSET] = 1000;
    ve->defaults[P_TIME_OFFSET] = 0;

    ve->limits[0][P_TIME_SCALE] = 10;
    ve->limits[1][P_TIME_SCALE] = 400;
    ve->defaults[P_TIME_SCALE] = 100;

    ve->limits[0][P_TIME_BLEND] = 0;
    ve->limits[1][P_TIME_BLEND] = 100;
    ve->defaults[P_TIME_BLEND] = 45;

    ve->limits[0][P_CHROMA] = 0;
    ve->limits[1][P_CHROMA] = 100;
    ve->defaults[P_CHROMA] = 100;

    ve->limits[0][P_RESET] = 0;
    ve->limits[1][P_RESET] = 1;
    ve->defaults[P_RESET] = 0;

    ve->sub_format = 1;
    ve->description = "Slit Scan Time";
    ve->param_description = vje_build_param_list(ve->num_params,
        "Sculpture Mode",
        "Time Amount",
        "Time Depth",
        "Time Source",
        "Source Mix",
        "Motion Reactivity",
        "Time Animation",
        "Time Offset",
        "Time Scale",
        "Temporal Smoothing",
        "Chroma Amount",
        "Reset Memory"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Sculpture Mode */
        VJ_BEAT_INTENSITY,    VJ_BEAT_F_CONTINUOUS,                                              18,                 100,                10, 38, 1000, 2600, 0,   65,    /* Time Amount */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                        4,                  28,                 6,  22, 1800, 4200, 900, 30,    /* Time Depth */
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Time Source */
        VJ_BEAT_SOURCE_MIX,   VJ_BEAT_F_CONTINUOUS,                                              8,                  88,                 8,  30, 1200, 3000, 0,   48,    /* Source Mix */
        VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS,                                              8,                  92,                 10, 38, 1000, 2600, 0,   62,    /* Motion Reactivity */
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Time Animation */
        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,                             0,                  1000,               8,  30, 1200, 3000, 0,   45,    /* Time Offset */
        VJ_BEAT_SPEED,        VJ_BEAT_F_CONTINUOUS,                                              35,                 220,                8,  30, 1200, 3000, 0,   50,    /* Time Scale */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_CONTINUOUS,                                              0,                  82,                 8,  32, 1200, 3200, 0,   50,    /* Temporal Smoothing */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS,                                              0,                  100,                8,  30, 1200, 3000, 0,   45,    /* Chroma Amount */
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000  /* Reset Memory */
    );
    (void) w;
    (void) h;
    return ve;
}

void *slitscan_malloc(int w, int h)
{
    slitscan_t *s = (slitscan_t*) vj_calloc(sizeof(slitscan_t));
    if(!s) return NULL;

    const int len = w * h;
    const size_t history_bytes = (size_t)SS_HISTORY_MAX * 3u * (size_t)len;
    const size_t prev_bytes = (size_t)len;
    const size_t u16_plane_bytes = (size_t)len * sizeof(uint16_t);

    size_t off = 0;
    const size_t history_off = off;
    off += history_bytes;
    const size_t prev_off = off;
    off += prev_bytes;
    off = (off + sizeof(uint16_t) - 1u) & ~(sizeof(uint16_t) - 1u);
    const size_t geom_off = off;
    off += u16_plane_bytes;
    const size_t radial_off = off;
    off += u16_plane_bytes;
    const size_t angle_off = off;
    off += u16_plane_bytes;
    const size_t total = off;

    s->region = (uint8_t*) vj_calloc(total);
    if(!s->region) {
        free(s);
        return NULL;
    }

    s->w = w;
    s->h = h;
    s->len = len;
    s->n_threads = vje_advise_num_threads(len);
    s->history = s->region + history_off;
    s->prev_y = s->region + prev_off;
    s->geom = (uint16_t*)(void*)(s->region + geom_off);
    s->radial_lut = (uint16_t*)(void*)(s->region + radial_off);
    s->angle_lut = (uint16_t*)(void*)(s->region + angle_off);
    s->frame = 0;
    s->write_pos = 0;
    s->seeded = 0;
    s->last_reset = 0;
    s->last_depth = -1;
    s->last_mode = -1;

    ss_refresh_history_planes(s);
    ss_build_math_luts(s);
    ss_build_center_polar_luts(s);
    ss_build_geom(s, SS_MODE_ROWS);
    ss_build_time_lut(s, 16);
    return (void*)s;
}

void slitscan_free(void *ptr)
{
    slitscan_t *s = (slitscan_t*)ptr;
    if(!s) return;
    free(s->region);
    free(s);
}

void slitscan_apply(void *ptr, VJFrame *frame, int *args)
{
    slitscan_t *s = (slitscan_t*)ptr;
    if(!s) return;

    const int len = s->len;
    const int amount = ss_clampi(args[P_AMOUNT], 0, 100);
    const int depth = ss_clampi(args[P_DEPTH], 1, SS_HISTORY_MAX);
    const int mode = ss_clampi(args[P_MODE], 0, SS_GEOM_MODES - 1);
    const int source = ss_clampi(args[P_SOURCE], 0, 4);
    const int source_gain = ss_clampi(args[P_SOURCE_GAIN], 0, 100);
    const int motion = ss_clampi(args[P_MOTION], 0, 100);
    const int time_offset = ss_clampi(args[P_TIME_OFFSET], 0, 1000);
    const int time_scale = ss_clampi(args[P_TIME_SCALE], 10, 400);
    const int time_blend = ss_clampi(args[P_TIME_BLEND], 0, 100);
    const int chroma = ss_clampi(args[P_CHROMA], 0, 100);
    const int time_motion = ss_clampi(args[P_TIME_MOTION], 0, 4);
    const int reset = args[P_RESET] ? 1 : 0;

    if(!s->seeded || (reset && !s->last_reset)) {
        ss_seed_history(s, frame);
    }
    s->last_reset = reset;

    ss_build_geom(s, mode);

    const int write_slot = s->write_pos;
    ss_copy_current_to_slot(s, frame, write_slot);

    uint8_t *cur_y = s->hist_y[write_slot];
    uint8_t *cur_u = s->hist_u[write_slot];
    uint8_t *cur_v = s->hist_v[write_slot];

    if(amount <= 0 || depth <= 1) {
        veejay_memcpy(s->prev_y, cur_y, len);
        s->write_pos = (s->write_pos + 1) & SS_HISTORY_MASK;
        s->frame++;
        return;
    }

    ss_build_time_lut(s, depth);

    const int amount_q8 = (amount * 256 + 50) / 100;
    const int chroma_q8 = (amount_q8 * ((chroma * 256 + 50) / 100) + 128) >> 8;
    const int source_gain_q8 = (source_gain * 256 + 50) / 100;
    const int time_blend_q8 = (time_blend * 256 + 50) / 100;
    const int motion_mul_q8 = (motion * 1024 + 50) / 100;
    const uint32_t phase_base = ((uint32_t)time_offset * 65535u) / 1000u;
    const uint32_t scale_q16 = ((uint32_t)time_scale * 65536u) / 100u;
    const uint32_t drift = ((uint32_t)s->frame * 181u) & 65535u;
    const uint32_t phase64 = (uint32_t)(s->frame & 63);
    const uint32_t pulse = (phase64 <= 31u) ? (phase64 * 2048u) : ((63u - phase64) * 2048u);
    uint32_t time_add = 0u;
    time_add = (time_motion == SS_TMOVE_DRIFT_FWD) ? drift : time_add;
    time_add = (time_motion == SS_TMOVE_DRIFT_REV) ? ((0u - drift) & 65535u) : time_add;
    time_add = (time_motion == SS_TMOVE_PULSE) ? pulse : time_add;
    const int time_reverse = (time_motion == SS_TMOVE_REVERSE);
    const int use_source = (source != SS_SRC_SHAPE && source_gain_q8 > 0);
    const int use_soft = (time_blend_q8 > 0);
    const ss_render_fn render = (!use_source)
        ? (use_soft ? ss_render_shape_soft : ss_render_shape_hard)
        : (use_soft ? ss_render_source_soft : ss_render_source_hard);

    ss_render_cfg cfg;
    cfg.geom = s->geom;
    cfg.cur_y = cur_y;
    cfg.cur_u = cur_u;
    cfg.cur_v = cur_v;
    cfg.out_y = frame->data[0];
    cfg.out_u = frame->data[1];
    cfg.out_v = frame->data[2];
    cfg.prev_y = s->prev_y;
    cfg.len = len;
    cfg.write_slot = write_slot;
    cfg.amount_q8 = amount_q8;
    cfg.chroma_q8 = chroma_q8;
    cfg.source = source;
    cfg.source_gain_q8 = source_gain_q8;
    cfg.motion_mul_q8 = motion_mul_q8;
    cfg.time_blend_q8 = time_blend_q8;
    cfg.phase_base = phase_base;
    cfg.scale_q16 = scale_q16;
    cfg.time_add = time_add;
    cfg.time_reverse = time_reverse;

#pragma omp parallel num_threads(s->n_threads)
    {
        render(s, &cfg);

#pragma omp single
        {
            veejay_memcpy(s->prev_y, cur_y, len);
            s->write_pos = (s->write_pos + 1) & SS_HISTORY_MASK;
            s->frame++;
        }
    }
}
