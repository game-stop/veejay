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
#include "mechanicalpixels.h"


#define KD_PARAMS 9

#define P_AMOUNT       0
#define P_CELL_SIZE    1
#define P_DEPTH        2
#define P_MOTOR        3
#define P_TRIGGER      4
#define P_WAVE         5
#define P_INERTIA      6
#define P_PALETTE      7
#define P_RESET        8

#define KD_CELL_MIN 6
#define KD_CELL_MAX 80

#define KD_PALETTES 6
#define KD_PALETTE_WOOD        0
#define KD_PALETTE_SOURCE_SOFT 1
#define KD_PALETTE_STEEL       2
#define KD_PALETTE_AMBER       3
#define KD_PALETTE_SOURCE_FULL 4
#define KD_PALETTE_NEON        5

#define KD_WAVES 8
#define KD_MODE_ROW            0
#define KD_MODE_COLUMN         1
#define KD_MODE_DIAGONAL       2
#define KD_MODE_CROSS_DIAGONAL 3
#define KD_MODE_RADIAL         4
#define KD_MODE_SPIRAL         5
#define KD_MODE_CHECKER        6
#define KD_MODE_SOLENOIDS      7

#define KD_STEP_Q 21
#define KD_MOTOR_Q 8

typedef struct {
    int w;
    int h;
    int len;
    int n_threads;
    int frame;
    int motor_q8;
    int seeded;
    int last_reset;
    int last_cell_size;
    int last_wave;
    int cols;
    int rows;
    int max_cols;
    int max_rows;
    int max_cells;

    uint8_t *region;
    uint8_t *prev_y;

    uint8_t *cell_value;
    uint8_t *cell_target;
    uint8_t *cell_delay;
    uint8_t *cell_phase;
    uint8_t *cell_rand;
    uint8_t *cell_wave;
    uint8_t *cell_avg_y;
    uint8_t *cell_avg_u;
    uint8_t *cell_avg_v;
    uint8_t *cell_motion;
    uint8_t *cell_slow_y;
    uint8_t *cell_pressure;
    uint8_t *cell_impact;
    uint8_t *cell_detail;
} kinetic_t;

typedef struct {
    int gap;
    int lift;
    int side;
    int face_x0;
    int face_y0;
    int face_x1;
    int face_y1;
} kd_geom_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int kd_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline uint8_t kd_clip_u8(int v)
{
    return (uint8_t) ((v < 0) ? 0 : (v > 255 ? 255 : v));
}

static inline uint32_t kd_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static inline uint8_t kd_hash_cell(int x, int y, int salt)
{
    const uint32_t h = (uint32_t) x * 374761393u +
                       (uint32_t) y * 668265263u +
                       (uint32_t) salt * 2246822519u;
    return (uint8_t) (kd_hash_u32(h) & 255u);
}

static inline int kd_blend_q8(int src, int fx, int aq)
{
    return src + (((fx - src) * aq + 128) >> 8);
}

static inline int kd_circular_dist8(int a, int b)
{
    int d = kd_absi(a - b) & 255;
    return (d > 128) ? (256 - d) : d;
}

static inline int kd_quantize_height(int v)
{
    int q = ((v + (KD_STEP_Q / 2)) / KD_STEP_Q) * KD_STEP_Q;
    return clampi(q, 0, 255);
}

static inline int kd_approach_shift(int cur, int target, int rise_shift, int fall_shift)
{
    cur = clampi(cur, 0, 255);
    target = clampi(target, 0, 255);

    if(target > cur) {
        const int d = target - cur;
        cur += (d + ((1 << rise_shift) - 1)) >> rise_shift;
    } else if(target < cur) {
        const int d = cur - target;
        cur -= (d + ((1 << fall_shift) - 1)) >> fall_shift;
    }

    return clampi(cur, 0, 255);
}

static inline int kd_motor_inc_q8(int speed)
{
    speed = clampi(speed, 0, 100);
    if(speed <= 0)
        return 0;

    return 4 + ((speed * speed * 30 + 50) / 100);
}

static void kd_configure_grid(kinetic_t *k, int cell_size)
{
    cell_size = clampi(cell_size, KD_CELL_MIN, KD_CELL_MAX);
    k->cols = (k->w + cell_size - 1) / cell_size;
    k->rows = (k->h + cell_size - 1) / cell_size;
    k->last_cell_size = cell_size;
    k->last_wave = -1;
}

static void kd_clear_cells(kinetic_t *k)
{
    const size_t n = (size_t) k->max_cells;
    veejay_memset(k->cell_value, 0, n);
    veejay_memset(k->cell_target, 0, n);
    veejay_memset(k->cell_delay, 0, n);
    veejay_memset(k->cell_phase, 0, n);
    veejay_memset(k->cell_wave, 0, n);
    veejay_memset(k->cell_avg_y, 0, n);
    veejay_memset(k->cell_avg_u, 128, n);
    veejay_memset(k->cell_avg_v, 128, n);
    veejay_memset(k->cell_motion, 0, n);
    veejay_memset(k->cell_slow_y, 0, n);
    veejay_memset(k->cell_pressure, 0, n);
    veejay_memset(k->cell_impact, 0, n);
    veejay_memset(k->cell_detail, 0, n);
}

static inline void kd_palette_color(int palette, int value, int avg_u, int avg_v,
                                    int rnd, int *u, int *v)
{
    switch(palette) {
        case KD_PALETTE_SOURCE_SOFT: {
            const int sat = 112 + ((value * 80) >> 8);
            *u = 128 + (((avg_u - 128) * sat) >> 8);
            *v = 128 + (((avg_v - 128) * sat) >> 8);
            break;
        }

        case KD_PALETTE_SOURCE_FULL: {
            const int sat = 204 + ((value * 78) >> 8);
            *u = 128 + (((avg_u - 128) * sat) >> 8);
            *v = 128 + (((avg_v - 128) * sat) >> 8);

            *u += ((rnd & 7) - 3);
            *v += (((rnd >> 3) & 7) - 3);
            break;
        }

        case KD_PALETTE_STEEL:
            *u = 134 + ((value * 14) >> 8) - ((rnd & 15) >> 2);
            *v = 121 - ((value * 8) >> 8) + ((rnd & 7) >> 1);
            break;

        case KD_PALETTE_AMBER:
            *u = 105 - ((value * 8) >> 8) + ((rnd & 7) >> 1);
            *v = 153 + ((value * 22) >> 8) - ((rnd & 15) >> 3);
            break;

        case KD_PALETTE_NEON: {
            int du = avg_u - 128;
            int dv = avg_v - 128;
            const int boost = 218 + ((value * 74) >> 8);
            du = (du * boost) >> 8;
            dv = (dv * boost) >> 8;

            if(kd_absi(du) + kd_absi(dv) < 18) {
                du += ((rnd & 1) ? 28 : -24);
                dv += ((rnd & 2) ? 34 : -30);
            }

            *u = 128 + du;
            *v = 128 + dv;
            break;
        }

        case KD_PALETTE_WOOD:
        default:
            *u = 112 + ((rnd & 15) - 7) + (((avg_u - 128) * value) >> 12);
            *v = 145 + ((rnd & 31) >> 2) + (((avg_v - 128) * value) >> 12);
            break;
    }

    *u = clampi(*u, 16, 240);
    *v = clampi(*v, 16, 240);
}

static inline int kd_phase_from_axis(int v, int max_v)
{
    if(max_v <= 0)
        return 0;
    return clampi((v * 255 + (max_v >> 1)) / max_v, 0, 255);
}

static inline int kd_pseudo_angle_phase(int dx, int dy)
{
    const int ax = kd_absi(dx);
    const int ay = kd_absi(dy);
    const int sum = ax + ay;

    if(sum <= 0)
        return 0;

    if(dx >= 0 && dy >= 0)
        return (ay * 64) / sum;
    if(dx < 0 && dy >= 0)
        return 64 + ((ax * 64) / sum);
    if(dx < 0 && dy < 0)
        return 128 + ((ay * 64) / sum);
    return 192 + ((ax * 64) / sum);
}

static inline int kd_radial_phase_for_cell(int cx, int cy, int cols, int rows)
{
    const int max_x = (cols > 1) ? (cols - 1) : 1;
    const int max_y = (rows > 1) ? (rows - 1) : 1;
    const int dx = kd_absi((cx << 1) - (cols - 1));
    const int dy = kd_absi((cy << 1) - (rows - 1));
    const int nx = clampi((dx * 255 + (max_x >> 1)) / max_x, 0, 255);
    const int ny = clampi((dy * 255 + (max_y >> 1)) / max_y, 0, 255);
    const int hi = (nx > ny) ? nx : ny;
    const int lo = (nx > ny) ? ny : nx;

    return clampi(((hi + (lo >> 1)) * 2 + 1) / 3, 0, 255);
}

static inline int kd_wave_phase_for_cell(int wave, int cx, int cy, int cols, int rows, int rnd)
{
    const int safe_cols = (cols > 0) ? cols : 1;
    const int safe_rows = (rows > 0) ? rows : 1;
    const int max_c = safe_cols - 1;
    const int max_r = safe_rows - 1;
    const int sx = kd_phase_from_axis(cx, max_c);
    const int sy = kd_phase_from_axis(cy, max_r);

    switch(wave) {
        case KD_MODE_ROW:
            return sy;

        case KD_MODE_COLUMN:
            return sx;

        case KD_MODE_DIAGONAL:
            return ((sx + sy) >> 1) & 255;

        case KD_MODE_CROSS_DIAGONAL:
            return ((sx + (255 - sy)) >> 1) & 255;

        case KD_MODE_RADIAL:
            return kd_radial_phase_for_cell(cx, cy, safe_cols, safe_rows);

        case KD_MODE_SPIRAL: {
            const int dx = (cx << 1) - max_c;
            const int dy = (cy << 1) - max_r;
            const int angle = kd_pseudo_angle_phase(dx, dy);
            const int radius = kd_radial_phase_for_cell(cx, cy, safe_cols, safe_rows);
            return (angle + ((radius * 3) >> 2)) & 255;
        }

        case KD_MODE_CHECKER: {
            const int bank = ((cx ^ cy) & 1) ? 128 : 0;
            const int sweep = ((sx + sy) >> 1);
            return (sweep + bank) & 255;
        }

        case KD_MODE_SOLENOIDS:
        default:
            return rnd;
    }
}

static void kd_rebuild_wave_lut(kinetic_t *k, int wave)
{
    const int cols = k->cols;
    const int rows = k->rows;
    const int max_cols = k->max_cols;

#pragma omp for schedule(static)
    for(int cy = 0; cy < rows; cy++) {
        for(int cx = 0; cx < cols; cx++) {
            const int idx = cy * max_cols + cx;
            k->cell_wave[idx] = (uint8_t) kd_wave_phase_for_cell(
                wave, cx, cy, cols, rows, k->cell_rand[idx]);
        }
    }

#pragma omp single
    k->last_wave = wave;
}

static inline kd_geom_t kd_make_geometry(int cw, int ch, int depth, int value)
{
    kd_geom_t g;
    const int min_dim = (cw < ch) ? cw : ch;

    g.gap = 0;
    g.lift = 0;
    g.side = 0;
    g.face_x0 = 0;
    g.face_y0 = 0;
    g.face_x1 = (cw > 0) ? cw : 1;
    g.face_y1 = (ch > 0) ? ch : 1;

    if(cw <= 2 || ch <= 2 || min_dim <= 2 || depth <= 0) {
        return g;
    }

    if(min_dim >= 12) {
        g.gap = (min_dim + 5) / 8;
        if(g.gap > 5) g.gap = 5;
    } else if(min_dim >= 8) {
        g.gap = 1;
    } else {
        g.gap = 0;
    }

    int inner_w = cw - (g.gap << 1);
    int inner_h = ch - (g.gap << 1);

    if(inner_w < 3 || inner_h < 3) {
        g.gap = 0;
        inner_w = cw;
        inner_h = ch;
        if(inner_w < 3 || inner_h < 3) {
            return g;
        }
    }

    const int min_face_w = clampi((inner_w * 55 + 50) / 100, 3, inner_w);
    const int min_face_h = clampi((inner_h * 55 + 50) / 100, 3, inner_h);

    int max_side_x = inner_w - min_face_w;
    int max_side_y = inner_h - min_face_h;
    int max_side = (max_side_x < max_side_y) ? max_side_x : max_side_y;
    if(max_side < 0) max_side = 0;

    if(max_side > 0) {
        int max_lift = (max_side * depth + 50) / 100;
        if(max_lift < 1) max_lift = 1;
        if(max_lift > max_side) max_lift = max_side;

        g.lift = 1 + ((value * max_lift) >> 8);
        if(g.lift > max_lift) g.lift = max_lift;
        g.side = g.lift;
    }

    g.face_x0 = g.gap;
    g.face_y0 = g.gap;
    g.face_x1 = cw - g.gap - g.side;
    g.face_y1 = ch - g.gap - g.side;

    if(g.face_x1 <= g.face_x0) g.face_x1 = g.face_x0 + 1;
    if(g.face_y1 <= g.face_y0) g.face_y1 = g.face_y0 + 1;
    if(g.face_x1 > cw - g.gap) g.face_x1 = cw - g.gap;
    if(g.face_y1 > ch - g.gap) g.face_y1 = ch - g.gap;

    return g;
}

vj_effect *mechanicalpixels_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve) return NULL;

    ve->num_params = KD_PARAMS;
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

    ve->limits[0][P_AMOUNT] = 0;       ve->limits[1][P_AMOUNT] = 100;       ve->defaults[P_AMOUNT] = 100;
    ve->limits[0][P_CELL_SIZE] = 6;    ve->limits[1][P_CELL_SIZE] = 80;     ve->defaults[P_CELL_SIZE] = 28;
    ve->limits[0][P_DEPTH] = 0;        ve->limits[1][P_DEPTH] = 100;        ve->defaults[P_DEPTH] = 88;
    ve->limits[0][P_MOTOR] = 0;        ve->limits[1][P_MOTOR] = 100;        ve->defaults[P_MOTOR] = 18;
    ve->limits[0][P_TRIGGER] = 0;      ve->limits[1][P_TRIGGER] = 100;      ve->defaults[P_TRIGGER] = 46;
    ve->limits[0][P_WAVE] = 0;         ve->limits[1][P_WAVE] = KD_WAVES - 1; ve->defaults[P_WAVE] = KD_MODE_DIAGONAL;
    ve->limits[0][P_INERTIA] = 0;      ve->limits[1][P_INERTIA] = 100;      ve->defaults[P_INERTIA] = 84;
    ve->limits[0][P_PALETTE] = 0;      ve->limits[1][P_PALETTE] = KD_PALETTES - 1; ve->defaults[P_PALETTE] = KD_PALETTE_SOURCE_FULL;
    ve->limits[0][P_RESET] = 0;        ve->limits[1][P_RESET] = 1;          ve->defaults[P_RESET] = 0;

    ve->description = "Mechanical Pixels";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params,
        "Amount",
        "Pixel Size",
        "3D Depth",
        "Cycle Speed",
        "Trigger",
        "Render Mode",
        "Mechanical Inertia",
        "Palette",
        "Reset State"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_WAVE], P_WAVE,
        "Rows", "Columns", "Diagonal", "Cross Diagonal", "Radial", "Spiral", "Checker", "Solenoids");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_PALETTE], P_PALETTE,
        "Wood", "Source Soft", "Steel", "Amber", "Source Full", "Neon");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_RESET], P_RESET, "Off", "Reset");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SOURCE_MIX,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                  62,                 100,                16, 62,  700, 2800, 0,    86,
        VJ_BEAT_GRID_SIZE,           VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 10, 48, 4, 14, 3200, 8600, 2400, 20,
        VJ_BEAT_GEOMETRY_AMPLITUDE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                  58,                 100,                18, 68,  650, 2600, 0,    92,
        VJ_BEAT_SPEED,               VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                  8,                  82,                 16, 62,  700, 2800, 0,    84,
        VJ_BEAT_MOTION_REACT,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,             14,                 68,                 14, 54,  850, 3200, 0,    72,
        VJ_BEAT_SELECTOR,            VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE,              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_INERTIA,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,             22,                 92,                 12, 46, 1000, 3600, 0,    58,
        VJ_BEAT_SELECTOR,            VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                         VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_RESET,               VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE,              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );
    return ve;
}

void *mechanicalpixels_malloc(int w, int h)
{
    kinetic_t *k = (kinetic_t *) vj_calloc(sizeof(kinetic_t));
    if(!k) return NULL;

    k->w = w;
    k->h = h;
    k->len = w * h;
    k->n_threads = vje_advise_num_threads(k->len);
    k->max_cols = (w + KD_CELL_MIN - 1) / KD_CELL_MIN;
    k->max_rows = (h + KD_CELL_MIN - 1) / KD_CELL_MIN;
    k->max_cells = k->max_cols * k->max_rows;
    k->last_cell_size = -1;
    k->last_wave = -1;
    k->motor_q8 = 0;

    const size_t len = (size_t) k->len;
    const size_t cells = (size_t) k->max_cells;
    const size_t total = len + cells * 14u;

    k->region = (uint8_t *) vj_calloc(total);
    if(!k->region) {
        free(k);
        return NULL;
    }

    uint8_t *p = k->region;
    k->prev_y = p; p += len;
    k->cell_value = p; p += cells;
    k->cell_target = p; p += cells;
    k->cell_delay = p; p += cells;
    k->cell_phase = p; p += cells;
    k->cell_rand = p; p += cells;
    k->cell_wave = p; p += cells;
    k->cell_avg_y = p; p += cells;
    k->cell_avg_u = p; p += cells;
    k->cell_avg_v = p; p += cells;
    k->cell_motion = p; p += cells;
    k->cell_slow_y = p; p += cells;
    k->cell_pressure = p; p += cells;
    k->cell_impact = p; p += cells;
    k->cell_detail = p;

    for(int y = 0; y < k->max_rows; y++) {
        for(int x = 0; x < k->max_cols; x++) {
            const int idx = y * k->max_cols + x;
            k->cell_rand[idx] = kd_hash_cell(x, y, 31);
        }
    }

    kd_configure_grid(k, 28);
    kd_clear_cells(k);
    return (void *) k;
}

void mechanicalpixels_free(void *ptr)
{
    kinetic_t *k = (kinetic_t *) ptr;

    free(k->region);
    free(k);
}

static void kd_seed_cells(kinetic_t *k, const uint8_t *Y, const uint8_t *U,
                          const uint8_t *V, int cell_size)
{
    const int w = k->w;
    const int h = k->h;
    const int cols = k->cols;
    const int rows = k->rows;
    const int max_cols = k->max_cols;
    uint8_t *prev_y = k->prev_y;

#pragma omp for schedule(static)
    for(int cy = 0; cy < rows; cy++) {
        for(int cx = 0; cx < cols; cx++) {
            const int idx = cy * max_cols + cx;
            const int x0 = cx * cell_size;
            const int y0 = cy * cell_size;
            const int x1 = (x0 + cell_size < w) ? x0 + cell_size : w;
            const int y1 = (y0 + cell_size < h) ? y0 + cell_size : h;

            int sum_y = 0;
            int sum_u = 0;
            int sum_v = 0;
            int min_y = 255;
            int max_y = 0;
            int count = 0;

            for(int y = y0; y < y1; y++) {
                const int off = y * w;
                for(int x = x0; x < x1; x++) {
                    const int p = off + x;
                    const int yy = Y[p];
                    prev_y[p] = (uint8_t) yy;
                    sum_y += yy;
                    sum_u += U[p];
                    sum_v += V[p];
                    if(yy < min_y) min_y = yy;
                    if(yy > max_y) max_y = yy;
                    count++;
                }
            }

            const int ay = sum_y / count;
            const int initial = 28 + (ay >> 4);
            k->cell_avg_y[idx] = (uint8_t) ay;
            k->cell_avg_u[idx] = (uint8_t) (sum_u / count);
            k->cell_avg_v[idx] = (uint8_t) (sum_v / count);
            k->cell_motion[idx] = 0;
            k->cell_slow_y[idx] = (uint8_t) ay;
            k->cell_pressure[idx] = (uint8_t) clampi(initial, 0, 255);
            k->cell_value[idx] = (uint8_t) kd_quantize_height(initial);
            k->cell_target[idx] = k->cell_value[idx];
            k->cell_delay[idx] = 0;
            k->cell_phase[idx] = 0;
            k->cell_impact[idx] = 0;
            k->cell_detail[idx] = (uint8_t) clampi(max_y - min_y, 0, 255);
        }
    }
}

static void kd_update_cells(kinetic_t *k, const uint8_t *Y, const uint8_t *U,
                            const uint8_t *V, int cell_size, int motor_speed,
                            int trigger, int inertia)
{
    const int w = k->w;
    const int h = k->h;
    const int cols = k->cols;
    const int rows = k->rows;
    const int max_cols = k->max_cols;
    const int motor_phase = (k->motor_q8 >> KD_MOTOR_Q) & 255;

    const int wave_width = 2 + ((100 - trigger) / 38) + ((motor_speed > 65) ? 1 : 0);
    const int deadband = 7 + (trigger * 29) / 100;                                   
    const int slow_shift = 5 + (trigger / 34);                                       
    const int release_slop = 9 + (trigger / 5);                                      
    const int actuator_step = 1 + ((100 - inertia) * 7) / 100;                       
    const int fall_step = (actuator_step > 1) ? (actuator_step - 1) : 1;
    const int pressure_decay = 26 + (inertia >> 2);
    const int motion_gain_q8 = 240 + clampi((cell_size - 10) * 11, 0, 224);

    uint8_t *prev_y = k->prev_y;

#pragma omp for schedule(static)
    for(int cy = 0; cy < rows; cy++) {
        for(int cx = 0; cx < cols; cx++) {
            const int idx = cy * max_cols + cx;
            const int x0 = cx * cell_size;
            const int y0 = cy * cell_size;
            const int x1 = (x0 + cell_size < w) ? x0 + cell_size : w;
            const int y1 = (y0 + cell_size < h) ? y0 + cell_size : h;

            int sum_y = 0;
            int sum_u = 0;
            int sum_v = 0;
            int sum_m = 0;
            int min_y = 255;
            int max_y = 0;
            int count = 0;

            for(int y = y0; y < y1; y++) {
                const int off = y * w;
                for(int x = x0; x < x1; x++) {
                    const int p = off + x;
                    const int yy = Y[p];
                    const int old = prev_y[p];
                    prev_y[p] = (uint8_t) yy;
                    sum_y += yy;
                    sum_u += U[p];
                    sum_v += V[p];
                    sum_m += kd_absi(yy - old);
                    if(yy < min_y) min_y = yy;
                    if(yy > max_y) max_y = yy;
                    count++;
                }
            }

            const int ay = sum_y / count;
            const int au = sum_u / count;
            const int av = sum_v / count;
            int raw_motion = sum_m / count;
            if(raw_motion > 255) raw_motion = 255;

            k->cell_avg_y[idx] = (uint8_t) ay;
            k->cell_avg_u[idx] = (uint8_t) au;
            k->cell_avg_v[idx] = (uint8_t) av;

            int slow_y = k->cell_slow_y[idx];
            const int luma_delta = kd_absi(ay - slow_y);
            slow_y = kd_approach_shift(slow_y, ay, slow_shift, slow_shift);
            k->cell_slow_y[idx] = (uint8_t) slow_y;

            const int raw_detail = clampi(max_y - min_y, 0, 255);
            int detail = k->cell_detail[idx];
            detail = kd_approach_shift(detail, raw_detail, 3, 4);
            k->cell_detail[idx] = (uint8_t) detail;

            int motion_energy = (raw_motion * 8 * motion_gain_q8) >> 8;
            if(motion_energy > 255) motion_energy = 255;

            int fm = k->cell_motion[idx];
            if(motion_energy > fm) {
                fm += ((motion_energy - fm) + 7) >> 3;
            } else {
                fm -= ((fm - motion_energy) + 21) / 22;
            }
            fm = clampi(fm, 0, 255);
            k->cell_motion[idx] = (uint8_t) fm;

            int base_relief = 18 + ((slow_y * 44) >> 8) + (detail >> 4);

            int luma_impulse = 0;
            if(luma_delta > deadband) {
                luma_impulse = (luma_delta - deadband) * (136 - trigger) / 64;
            }

            int motion_impulse = 0;
            if(fm > (deadband << 1)) {
                motion_impulse = (fm - (deadband << 1)) * (150 - (trigger >> 1)) / 124;
            }

            int detail_impulse = 0;
            if(detail > deadband) {
                detail_impulse = (detail - deadband) / 4;
            }

            int desired_pressure = base_relief + luma_impulse + motion_impulse + detail_impulse;
            desired_pressure = clampi(desired_pressure, 0, 255);

            int pressure = k->cell_pressure[idx];
            if(desired_pressure > pressure) {
                pressure += ((desired_pressure - pressure) + 3) >> 2;
            } else {
                pressure -= ((pressure - desired_pressure) + pressure_decay - 1) / pressure_decay;
            }
            pressure = clampi(pressure, 0, 255);
            k->cell_pressure[idx] = (uint8_t) pressure;

            int impact = k->cell_impact[idx];
            if(impact > 0) {
                impact -= 2 + (impact >> 4);
                if(impact < 0) impact = 0;
            }

            if(k->cell_delay[idx] > 0) {
                k->cell_delay[idx]--;
            } else if(motor_speed > 0) {
                const int rnd = k->cell_rand[idx];
                const int d = kd_circular_dist8(motor_phase, k->cell_wave[idx]);

                if(d <= wave_width) {
                    int qtarget = kd_quantize_height(pressure);

                    qtarget += (((rnd & 7) - 3) * KD_STEP_Q) / 8;
                    qtarget = kd_quantize_height(qtarget);

                    if(kd_absi(qtarget - (int) k->cell_target[idx]) >= release_slop) {
                        const int hit = kd_absi(qtarget - (int) k->cell_value[idx]);
                        k->cell_target[idx] = (uint8_t) qtarget;
                        impact += 66 + (hit >> 1);
                        if(detail > 32) impact += detail >> 4;
                        if(impact > 255) impact = 255;

                        k->cell_delay[idx] = (uint8_t) ((rnd * (100 - motor_speed)) / 950);
                    }
                }
            }

            int value = k->cell_value[idx];
            const int target = k->cell_target[idx];
            const int diff = target - value;
            const int step = (diff >= 0) ? actuator_step : fall_step;

            if(kd_absi(diff) <= step) {
                value = target;
            } else if(diff > 0) {
                value += step;
            } else {
                value -= step;
            }

            value = clampi(value, 0, 255);
            k->cell_value[idx] = (uint8_t) value;
            k->cell_phase[idx] = (uint8_t) clampi(kd_absi(target - value), 0, 255);
            k->cell_impact[idx] = (uint8_t) impact;
        }
    }
}

static void kd_render_wall(kinetic_t *k, VJFrame *frame, int cell_size,
                           int amount, int depth, int palette)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    const int w = k->w;
    const int h = k->h;
    const int cols = k->cols;
    const int rows = k->rows;
    const int max_cols = k->max_cols;
    const int amount_q = (amount * 256 + 50) / 100;
    const int full_amount = (amount_q >= 256);

#pragma omp for schedule(static)
    for(int cy = 0; cy < rows; cy++) {
        for(int cx = 0; cx < cols; cx++) {
            const int idx = cy * max_cols + cx;
            const int x0 = cx * cell_size;
            const int y0 = cy * cell_size;
            const int x1 = (x0 + cell_size < w) ? x0 + cell_size : w;
            const int y1 = (y0 + cell_size < h) ? y0 + cell_size : h;
            const int cw = x1 - x0;
            const int ch = y1 - y0;

            const int value = kd_quantize_height(k->cell_value[idx]);
            const int phase = k->cell_phase[idx];
            const int impact = k->cell_impact[idx];
            const int rnd = k->cell_rand[idx];
            const int avg_y = k->cell_avg_y[idx];
            const int avg_u = k->cell_avg_u[idx];
            const int avg_v = k->cell_avg_v[idx];
            const int motion = k->cell_motion[idx];
            const int detail = k->cell_detail[idx];

            const kd_geom_t g = kd_make_geometry(cw, ch, depth, value);

            const int face_w = (g.face_x1 > g.face_x0) ? (g.face_x1 - g.face_x0) : 1;
            const int face_h = (g.face_y1 > g.face_y0) ? (g.face_y1 - g.face_y0) : 1;
            const int face_mid_x = (g.face_x0 + g.face_x1) >> 1;
            const int face_mid_y = (g.face_y0 + g.face_y1) >> 1;
            const int grad_x_q8 = (18 << 8) / face_w;
            const int grad_y_q8 = (24 << 8) / face_h;
            const int ax_q8 = (255 << 8) / face_w;
            const int ay_q8 = (255 << 8) / face_h;
            const int source_texture_q8 =
                (palette == KD_PALETTE_SOURCE_FULL) ? (28 + (detail >> 3)) :
                (palette == KD_PALETTE_SOURCE_SOFT) ? (16 + (detail >> 4)) :
                (palette == KD_PALETTE_NEON) ? (20 + (detail >> 4)) : 0;

            int base_y = 5 + ((rnd & 15) >> 1);
            base_y += (detail >> 5);

            int face_y = 34 + ((value * 190) >> 8);
            face_y += (motion >> 5);
            face_y += (impact >> 4);
            face_y += (detail >> 5);
            if(phase > 80) face_y += (phase >> 6);

            if(palette == KD_PALETTE_SOURCE_FULL || palette == KD_PALETTE_NEON) {
                const int source_relief = avg_y + ((value - 128) >> 2);
                face_y = ((face_y * 3) + source_relief) >> 2;
            } else if(palette == KD_PALETTE_SOURCE_SOFT) {
                face_y += (avg_y - 128) >> 4;
            }

            face_y = clampi(face_y, 0, 255);

            int face_u = 128;
            int face_v = 128;
            kd_palette_color(palette, value, avg_u, avg_v, rnd, &face_u, &face_v);

            const int side_u = 128 + ((face_u - 128) * 13) / 16;
            const int side_v = 128 + ((face_v - 128) * 13) / 16;
            const int shadow_u = 128 + ((face_u - 128) * 3) / 10;
            const int shadow_v = 128 + ((face_v - 128) * 3) / 10;
            const int back_u = 128 + ((face_u - 128) / 6);
            const int back_v = 128 + ((face_v - 128) / 6);
            const int rim_flash = (impact > 32) ? (impact >> 3) : 0;

            for(int y = y0; y < y1; y++) {
                const int ly = y - y0;
                const int off = y * w;
                for(int x = x0; x < x1; x++) {
                    const int lx = x - x0;
                    const int p = off + x;

                    const int sy = Y[p];
                    const int su = U[p];
                    const int sv = V[p];

                    int fy = base_y;
                    int fu = back_u;
                    int fv = back_v;

                    if(g.gap > 0 &&
                       (lx < g.gap || ly < g.gap || lx >= cw - g.gap || ly >= ch - g.gap)) {
                        fy = 2;
                        fu = 128;
                        fv = 128;
                    } else if(lx >= g.face_x0 && lx < g.face_x1 &&
                              ly >= g.face_y0 && ly < g.face_y1) {
                        const int rel_x = lx - face_mid_x;
                        const int rel_y = ly - face_mid_y;
                        const int bevel_x = (lx == g.face_x0 || lx == g.face_x1 - 1);
                        const int bevel_y = (ly == g.face_y0 || ly == g.face_y1 - 1);

                        int grad = ((rel_x * grad_x_q8) + (rel_y * grad_y_q8)) >> 8;
                        int ax = (kd_absi(rel_x) * ax_q8) >> 8;
                        int ay = (kd_absi(rel_y) * ay_q8) >> 8;
                        int pillow = 34 - ((ax + ay) >> 3);
                        if(pillow < -18) pillow = -18;

                        int surface = ((detail + 16) >> 5);
                        if(palette == KD_PALETTE_WOOD) {
                            surface += (((lx * 3 + ly + rnd) & 7) - 3);
                        } else if(palette == KD_PALETTE_STEEL) {
                            surface += (((ly + rnd) & 7) - 3);
                        } else if(palette == KD_PALETTE_SOURCE_FULL) {
                            surface += (((lx ^ ly ^ rnd) & 3) - 1);
                        } else if(palette == KD_PALETTE_NEON) {
                            surface += (((lx + ly + rnd) & 3) == 0) ? 3 : 0;
                        }

                        fy = face_y - grad + pillow + surface;
                        if(source_texture_q8 > 0)
                            fy += ((sy - avg_y) * source_texture_q8) >> 8;

                        if(bevel_x || bevel_y) fy -= 14;
                        if(lx <= g.face_x0 + 1 || ly <= g.face_y0 + 1) fy += 20 + rim_flash;
                        if(lx >= g.face_x1 - 2 || ly >= g.face_y1 - 2) fy -= 23 + (impact >> 4);

                        if(impact > 24) {
                            if(lx <= g.face_x0 + 1 || ly <= g.face_y0 + 1) {
                                fy += impact >> 4;
                            }
                            if(lx >= g.face_x1 - 2 || ly >= g.face_y1 - 2) {
                                fy -= impact >> 4;
                            }
                        }

                        fu = face_u;
                        fv = face_v;
                        if(impact > 48 &&
                           (palette == KD_PALETTE_SOURCE_FULL || palette == KD_PALETTE_NEON)) {
                            fu = clampi(fu + (((face_u - 128) * impact) >> 10), 16, 240);
                            fv = clampi(fv + (((face_v - 128) * impact) >> 10), 16, 240);
                        }
                    } else if(g.side > 0 &&
                              lx >= g.face_x1 && lx < g.face_x1 + g.side &&
                              ly >= g.face_y0 + g.side / 2 && ly < g.face_y1 + g.side / 2) {
                        const int d = lx - g.face_x1;
                        fy = face_y - 47 - d * 7 - (impact >> 5);
                        fu = side_u;
                        fv = side_v;
                    } else if(g.side > 0 &&
                              ly >= g.face_y1 && ly < g.face_y1 + g.side &&
                              lx >= g.face_x0 + g.side / 2 && lx < g.face_x1 + g.side / 2) {
                        const int d = ly - g.face_y1;
                        fy = face_y - 68 - d * 8 - (impact >> 5);
                        fu = side_u;
                        fv = side_v;
                    } else if(lx >= g.face_x1 + g.side / 2 || ly >= g.face_y1 + g.side / 2) {
                        fy = base_y - 4 - (g.lift >> 1) - (impact >> 6);
                        fu = shadow_u;
                        fv = shadow_v;
                    }

                    if(full_amount) {
                        Y[p] = kd_clip_u8(fy);
                        U[p] = kd_clip_u8(fu);
                        V[p] = kd_clip_u8(fv);
                    } else {
                        Y[p] = kd_clip_u8(kd_blend_q8(sy, fy, amount_q));
                        U[p] = kd_clip_u8(kd_blend_q8(su, fu, amount_q));
                        V[p] = kd_clip_u8(kd_blend_q8(sv, fv, amount_q));
                    }
                }
            }
        }
    }
}

void mechanicalpixels_apply(void *ptr, VJFrame *frame, int *args)
{
    kinetic_t *k = (kinetic_t *) ptr;

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    const int amount = args[P_AMOUNT];
    const int cell_size = clampi(args[P_CELL_SIZE], KD_CELL_MIN, KD_CELL_MAX);
    const int depth = args[P_DEPTH];
    const int motor_speed = args[P_MOTOR];
    const int trigger = args[P_TRIGGER];
    const int wave = clampi(args[P_WAVE], 0, KD_WAVES - 1);
    const int inertia = args[P_INERTIA];
    const int palette = clampi(args[P_PALETTE], 0, KD_PALETTES - 1);
    const int reset = args[P_RESET];

    if(cell_size != k->last_cell_size) {
        kd_configure_grid(k, cell_size);
        kd_clear_cells(k);
        k->seeded = 0;
        k->motor_q8 = 0;
    }

    const int rebuild_wave = k->last_wave != wave;
    const int need_seed = !k->seeded || (reset && !k->last_reset);

#pragma omp parallel num_threads(k->n_threads)
    {
        if(rebuild_wave)
            kd_rebuild_wave_lut(k, wave);

        if(need_seed)
            kd_seed_cells(k, Y, U, V, cell_size);

        if(amount > 0) {
            kd_update_cells(k, Y, U, V, cell_size, motor_speed, trigger, inertia);
            kd_render_wall(k, frame, cell_size, amount, depth, palette);
        } else {
#pragma omp for schedule(static)
            for(int i = 0; i < k->len; i++)
                k->prev_y[i] = Y[i];
        }
    }

    if(need_seed) {
        k->seeded = 1;
        k->motor_q8 = 0;
    }

    k->last_reset = reset;

    if(motor_speed > 0)
        k->motor_q8 = (k->motor_q8 + kd_motor_inc_q8(motor_speed)) & 0xffff;

    k->frame++;
}
