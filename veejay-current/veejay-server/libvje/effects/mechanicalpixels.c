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
#include <veejaycore/vjmem.h>
#include "mechanicalpixels.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif

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

#define KD_WAVES 4
#define KD_WAVE_ROW           0
#define KD_WAVE_DIAGONAL      1
#define KD_WAVE_RADIAL        2
#define KD_WAVE_SOLENOIDS     3

#define KD_STEP_Q 21

typedef struct {
    int w;
    int h;
    int len;
    int n_threads;
    int frame;
    int motor_pos;
    int seeded;
    int last_reset;
    int last_cell_size;
    int cols;
    int rows;
    int max_cols;
    int max_rows;
    int max_cells;

    uint8_t *region;
    uint8_t *prev_y;

    uint8_t *cell_value;    /* current physical height */
    uint8_t *cell_target;   /* last actuator target */
    uint8_t *cell_delay;    /* delayed solenoid release */
    uint8_t *cell_phase;    /* distance to target / stress */
    uint8_t *cell_rand;     /* stable mechanical variation */
    uint8_t *cell_avg_y;
    uint8_t *cell_avg_u;
    uint8_t *cell_avg_v;
    uint8_t *cell_motion;   /* filtered source motion */
    uint8_t *cell_slow_y;   /* slow exposure/luma envelope */
    uint8_t *cell_pressure; /* stored source pressure, released by governor */
    uint8_t *cell_impact;   /* actuator hit flash / mechanical stress */
} kinetic_t;

static inline int kd_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
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
    int d = abs(a - b) & 255;
    return (d > 128) ? (256 - d) : d;
}

static inline int kd_quantize_height(int v)
{
    int q = ((v + (KD_STEP_Q / 2)) / KD_STEP_Q) * KD_STEP_Q;
    return kd_clampi(q, 0, 255);
}

static inline void kd_write_pixel(uint8_t *Y, uint8_t *U, uint8_t *V, int p,
                                  int sy, int su, int sv,
                                  int fy, int fu, int fv,
                                  int amount_q)
{
    if(amount_q >= 256) {
        Y[p] = kd_clip_u8(fy);
        U[p] = kd_clip_u8(fu);
        V[p] = kd_clip_u8(fv);
        return;
    }

    Y[p] = kd_clip_u8(kd_blend_q8(sy, fy, amount_q));
    U[p] = kd_clip_u8(kd_blend_q8(su, fu, amount_q));
    V[p] = kd_clip_u8(kd_blend_q8(sv, fv, amount_q));
}

static void kd_configure_grid(kinetic_t *k, int cell_size)
{
    cell_size = kd_clampi(cell_size, KD_CELL_MIN, KD_CELL_MAX);
    k->cols = (k->w + cell_size - 1) / cell_size;
    k->rows = (k->h + cell_size - 1) / cell_size;
    k->last_cell_size = cell_size;
}

static void kd_clear_cells(kinetic_t *k)
{
    const size_t n = (size_t) k->max_cells;
    memset(k->cell_value, 0, n);
    memset(k->cell_target, 0, n);
    memset(k->cell_delay, 0, n);
    memset(k->cell_phase, 0, n);
    memset(k->cell_avg_y, 0, n);
    memset(k->cell_avg_u, 128, n);
    memset(k->cell_avg_v, 128, n);
    memset(k->cell_motion, 0, n);
    memset(k->cell_slow_y, 0, n);
    memset(k->cell_pressure, 0, n);
    memset(k->cell_impact, 0, n);
}

static inline void kd_palette_color(int palette, int value, int avg_u, int avg_v,
                                    int rnd, int *u, int *v)
{

    switch(palette) {
        case KD_PALETTE_SOURCE_SOFT: {
            const int sat = 104 + ((value * 92) >> 8);
            *u = 128 + (((avg_u - 128) * sat) >> 8);
            *v = 128 + (((avg_v - 128) * sat) >> 8);
            break;
        }

        case KD_PALETTE_SOURCE_FULL: {
            const int sat = 190 + ((value * 96) >> 8);
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
            const int boost = 215 + ((value * 80) >> 8);
            du = (du * boost) >> 8;
            dv = (dv * boost) >> 8;

            if(abs(du) + abs(dv) < 18) {
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

    *u = kd_clampi(*u, 16, 240);
    *v = kd_clampi(*v, 16, 240);
}

static inline int kd_wave_phase_for_cell(int wave, int cx, int cy, int cols, int rows, int rnd)
{
    const int safe_cols = (cols > 0) ? cols : 1;
    const int safe_rows = (rows > 0) ? rows : 1;

    switch(wave) {
        case KD_WAVE_ROW:
            return (cy * 256) / safe_rows;

        case KD_WAVE_DIAGONAL:
            return (((cx * 128) / safe_cols) + ((cy * 128) / safe_rows)) & 255;

        case KD_WAVE_RADIAL: {
            const int dx = abs((cx << 1) - cols);
            const int dy = abs((cy << 1) - rows);
            const int denom = (cols + rows) ? (cols + rows) : 1;
            return ((dx + dy) * 128) / denom;
        }

        case KD_WAVE_SOLENOIDS:
        default:
            return rnd;
    }
}

vj_effect *mechanicalpixels_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve) return NULL;

    ve->num_params = KD_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_AMOUNT] = 0;       ve->limits[1][P_AMOUNT] = 100;       ve->defaults[P_AMOUNT] = 100;
    ve->limits[0][P_CELL_SIZE] = 6;    ve->limits[1][P_CELL_SIZE] = 80;     ve->defaults[P_CELL_SIZE] = 28;
    ve->limits[0][P_DEPTH] = 0;        ve->limits[1][P_DEPTH] = 100;        ve->defaults[P_DEPTH] = 86;
    ve->limits[0][P_MOTOR] = 0;        ve->limits[1][P_MOTOR] = 100;        ve->defaults[P_MOTOR] = 28;
    ve->limits[0][P_TRIGGER] = 0;      ve->limits[1][P_TRIGGER] = 100;      ve->defaults[P_TRIGGER] = 44;
    ve->limits[0][P_WAVE] = 0;         ve->limits[1][P_WAVE] = KD_WAVES - 1; ve->defaults[P_WAVE] = KD_WAVE_DIAGONAL;
    ve->limits[0][P_INERTIA] = 0;      ve->limits[1][P_INERTIA] = 100;      ve->defaults[P_INERTIA] = 82;
    ve->limits[0][P_PALETTE] = 0;      ve->limits[1][P_PALETTE] = KD_PALETTES - 1; ve->defaults[P_PALETTE] = KD_PALETTE_SOURCE_FULL;
    ve->limits[0][P_RESET] = 0;        ve->limits[1][P_RESET] = 1;          ve->defaults[P_RESET] = 0;

    ve->description = "Mechanical Pixels";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params,
        "Amount",
        "Pixel Size",
        "3D Depth",
        "Motor Speed",
        "Trigger",
        "Wave Shape",
        "Mechanical Inertia",
        "Palette",
        "Reset State"
    );

    (void) w;
    (void) h;
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
    k->motor_pos = 0;

    const size_t len = (size_t) k->len;
    const size_t cells = (size_t) k->max_cells;
    const size_t total = len + cells * 12u;

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
    k->cell_avg_y = p; p += cells;
    k->cell_avg_u = p; p += cells;
    k->cell_avg_v = p; p += cells;
    k->cell_motion = p; p += cells;
    k->cell_slow_y = p; p += cells;
    k->cell_pressure = p; p += cells;
    k->cell_impact = p;

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
    if(!k) return;
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

#pragma omp parallel for num_threads(k->n_threads) schedule(static)
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
            int count = 0;

            for(int y = y0; y < y1; y++) {
                const int off = y * w;
                for(int x = x0; x < x1; x++) {
                    const int p = off + x;
                    sum_y += Y[p];
                    sum_u += U[p];
                    sum_v += V[p];
                    count++;
                }
            }

            if(count <= 0) count = 1;

            const int ay = sum_y / count;
            const int initial = 28 + (ay >> 4);
            k->cell_avg_y[idx] = (uint8_t) ay;
            k->cell_avg_u[idx] = (uint8_t) (sum_u / count);
            k->cell_avg_v[idx] = (uint8_t) (sum_v / count);
            k->cell_motion[idx] = 0;
            k->cell_slow_y[idx] = (uint8_t) ay;
            k->cell_pressure[idx] = (uint8_t) kd_clampi(initial, 0, 255);
            k->cell_value[idx] = (uint8_t) kd_quantize_height(initial);
            k->cell_target[idx] = k->cell_value[idx];
            k->cell_delay[idx] = 0;
            k->cell_phase[idx] = 0;
            k->cell_impact[idx] = 0;
        }
    }
}

static void kd_update_cells(kinetic_t *k, const uint8_t *Y, const uint8_t *U,
                            const uint8_t *V, int cell_size, int motor_speed,
                            int trigger, int wave, int inertia)
{
    const int w = k->w;
    const int h = k->h;
    const int cols = k->cols;
    const int rows = k->rows;
    const int max_cols = k->max_cols;
    const int motor_phase = k->motor_pos & 255;
    const int motor_step = (motor_speed <= 0) ? 0 : (1 + (motor_speed * 11) / 100);
    const int wave_width = 2 + motor_step + ((100 - trigger) / 42); /* 2..15 */
    const int deadband = 6 + (trigger * 28) / 100;                 /* 6..34 */
    const int slow_shift = 5 + (trigger / 34);                     /* 5..7 */
    const int release_slop = 8 + (trigger / 5);                    /* 8..28 */
    const int actuator_step = 1 + ((100 - inertia) * 7) / 100;     /* 1..8 */
    const int fall_step = (actuator_step > 1) ? (actuator_step - 1) : 1;

#pragma omp parallel for num_threads(k->n_threads) schedule(static)
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
            int count = 0;

            for(int y = y0; y < y1; y++) {
                const int off = y * w;
                for(int x = x0; x < x1; x++) {
                    const int p = off + x;
                    const int yy = Y[p];
                    sum_y += yy;
                    sum_u += U[p];
                    sum_v += V[p];
                    sum_m += abs(yy - (int) k->prev_y[p]);
                    count++;
                }
            }

            if(count <= 0) count = 1;

            const int ay = sum_y / count;
            const int au = sum_u / count;
            const int av = sum_v / count;
            int raw_motion = sum_m / count;
            if(raw_motion > 255) raw_motion = 255;

            k->cell_avg_y[idx] = (uint8_t) ay;
            k->cell_avg_u[idx] = (uint8_t) au;
            k->cell_avg_v[idx] = (uint8_t) av;

            int slow_y = k->cell_slow_y[idx];
            const int luma_delta = abs(ay - slow_y);
            slow_y += (ay - slow_y) >> slow_shift;
            slow_y = kd_clampi(slow_y, 0, 255);
            k->cell_slow_y[idx] = (uint8_t) slow_y;

            int motion_energy = raw_motion * 8;
            if(motion_energy > 255) motion_energy = 255;

            int fm = k->cell_motion[idx];
            if(motion_energy > fm) {
                fm += ((motion_energy - fm) + 7) >> 3;  /* slow rise */
            } else {
                fm -= ((fm - motion_energy) + 19) / 20; /* slower fall */
            }
            fm = kd_clampi(fm, 0, 255);
            k->cell_motion[idx] = (uint8_t) fm;

            int base_relief = 22 + ((slow_y * 46) >> 8); /* 22..68 */
            int luma_impulse = 0;
            if(luma_delta > deadband) {
                luma_impulse = (luma_delta - deadband) * (140 - trigger) / 62;
            }

            int motion_impulse = 0;
            if(fm > (deadband << 1)) {
                motion_impulse = (fm - (deadband << 1)) * (150 - (trigger >> 1)) / 120;
            }

            int desired_pressure = base_relief + luma_impulse + motion_impulse;
            desired_pressure = kd_clampi(desired_pressure, 0, 255);

            int pressure = k->cell_pressure[idx];
            if(desired_pressure > pressure) {
                pressure += ((desired_pressure - pressure) + 3) >> 2;
            } else {
                const int decay = 24 + (inertia >> 2); /* heavy wall keeps pressure */
                pressure -= ((pressure - desired_pressure) + decay - 1) / decay;
            }
            pressure = kd_clampi(pressure, 0, 255);
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
                const int cell_phase = kd_wave_phase_for_cell(wave, cx, cy, cols, rows, rnd);
                const int d = kd_circular_dist8(motor_phase, cell_phase);

                if(d <= wave_width) {
                    int qtarget = kd_quantize_height(pressure);

                    qtarget += (((rnd & 7) - 3) * KD_STEP_Q) / 8;
                    qtarget = kd_quantize_height(qtarget);

                    if(abs(qtarget - (int) k->cell_target[idx]) >= release_slop) {
                        const int hit = abs(qtarget - (int) k->cell_value[idx]);
                        k->cell_target[idx] = (uint8_t) qtarget;
                        impact += 70 + (hit >> 1);
                        if(impact > 255) impact = 255;

                        /* Small per-cell release delay keeps waves physical, not digital. */
                        k->cell_delay[idx] = (uint8_t) ((rnd * (100 - motor_speed)) / 900);
                    }
                }
            }

            int value = k->cell_value[idx];
            const int target = k->cell_target[idx];
            const int diff = target - value;
            const int step = (diff >= 0) ? actuator_step : fall_step;

            if(abs(diff) <= step) {
                value = target;
            } else if(diff > 0) {
                value += step;
            } else if(diff < 0) {
                value -= step;
            }

            value = kd_clampi(value, 0, 255);
            k->cell_value[idx] = (uint8_t) value;
            k->cell_phase[idx] = (uint8_t) kd_clampi(abs(target - value), 0, 255);
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

#pragma omp parallel for num_threads(k->n_threads) schedule(static)
    for(int cy = 0; cy < rows; cy++) {
        for(int cx = 0; cx < cols; cx++) {
            const int idx = cy * max_cols + cx;
            const int x0 = cx * cell_size;
            const int y0 = cy * cell_size;
            const int x1 = (x0 + cell_size < w) ? x0 + cell_size : w;
            const int y1 = (y0 + cell_size < h) ? y0 + cell_size : h;
            const int cw = x1 - x0;
            const int ch = y1 - y0;
            if(cw <= 0 || ch <= 0) continue;

            const int value = kd_quantize_height(k->cell_value[idx]);
            const int phase = k->cell_phase[idx];
            const int impact = k->cell_impact[idx];
            const int rnd = k->cell_rand[idx];
            const int avg_u = k->cell_avg_u[idx];
            const int avg_v = k->cell_avg_v[idx];
            const int motion = k->cell_motion[idx];

            const int min_dim = (cw < ch) ? cw : ch;
            int gap = (min_dim >= 28) ? 4 : ((min_dim >= 18) ? 3 : ((min_dim >= 10) ? 2 : 1));
            if(min_dim <= 8) gap = 0;

            int max_lift = (min_dim * depth) / 118;
            if(max_lift < 1) max_lift = 1;
            if(max_lift > (min_dim - 2)) max_lift = min_dim - 2;

            int lift = 1 + ((value * max_lift) >> 8);
            if(lift < 1) lift = 1;
            if(lift > max_lift) lift = max_lift;

            int side = lift;
            if(side > min_dim / 2) side = min_dim / 2;
            if(side < 1) side = 1;

            int face_x0 = gap;
            int face_y0 = gap;
            int face_x1 = cw - gap - side;
            int face_y1 = ch - gap - side;

            if(face_x1 <= face_x0 + 2) face_x1 = cw - gap;
            if(face_y1 <= face_y0 + 2) face_y1 = ch - gap;

            const int face_w = (face_x1 - face_x0) ? (face_x1 - face_x0) : 1;
            const int face_h = (face_y1 - face_y0) ? (face_y1 - face_y0) : 1;
            const int face_mid_x = (face_x0 + face_x1) >> 1;
            const int face_mid_y = (face_y0 + face_y1) >> 1;

            int base_y = 5 + ((rnd & 15) >> 1);
            int face_y = 34 + ((value * 194) >> 8);
            face_y += (motion >> 5);
            face_y += (impact >> 4);
            if(phase > 80) face_y += (phase >> 6);


            if(palette == KD_PALETTE_SOURCE_FULL || palette == KD_PALETTE_NEON) {
                const int color_lift = ((int)k->cell_avg_y[idx] - 128) >> 3;
                face_y += color_lift;
            }

            face_y = kd_clampi(face_y, 0, 255);

            int face_u = 128;
            int face_v = 128;
            kd_palette_color(palette, value, avg_u, avg_v, rnd, &face_u, &face_v);

            const int side_u = 128 + ((face_u - 128) * 13) / 16;
            const int side_v = 128 + ((face_v - 128) * 13) / 16;
            const int shadow_u = 128 + ((face_u - 128) * 3) / 10;
            const int shadow_v = 128 + ((face_v - 128) * 3) / 10;
            const int back_u = 128 + ((face_u - 128) / 6);
            const int back_v = 128 + ((face_v - 128) / 6);

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

                    if(lx < gap || ly < gap || lx >= cw - gap || ly >= ch - gap) {
                        fy = 2;
                        fu = 128;
                        fv = 128;
                    } else if(lx >= face_x0 && lx < face_x1 && ly >= face_y0 && ly < face_y1) {
                        const int rel_x = lx - face_mid_x;
                        const int rel_y = ly - face_mid_y;
                        const int bevel_x = (lx == face_x0 || lx == face_x1 - 1);
                        const int bevel_y = (ly == face_y0 || ly == face_y1 - 1);

                        int grad = (rel_x * 18) / face_w + (rel_y * 24) / face_h;

                        int ax = abs(rel_x) * 255 / face_w;
                        int ay = abs(rel_y) * 255 / face_h;
                        int pillow = 36 - ((ax + ay) >> 3);
                        if(pillow < -18) pillow = -18;

                        int surface = 0;
                        if(palette == KD_PALETTE_WOOD) {
                            surface = (((lx * 3 + ly + rnd) & 7) - 3);
                        } else if(palette == KD_PALETTE_STEEL) {
                            surface = (((ly + rnd) & 7) - 3);
                        } else if(palette == KD_PALETTE_SOURCE_FULL) {
                            surface = (((lx ^ ly ^ rnd) & 3) - 1);
                        } else if(palette == KD_PALETTE_NEON) {
                            surface = (((lx + ly + rnd) & 3) == 0) ? 3 : 0;
                        }

                        fy = face_y - grad + pillow + surface;
                        if(bevel_x || bevel_y) fy -= 16;
                        if(lx <= face_x0 + 1 || ly <= face_y0 + 1) fy += 22;
                        if(lx >= face_x1 - 2 || ly >= face_y1 - 2) fy -= 25;


                        if(impact > 24) {
                            if(lx <= face_x0 + 1 || ly <= face_y0 + 1) {
                                fy += impact >> 3;
                            }
                            if(lx >= face_x1 - 2 || ly >= face_y1 - 2) {
                                fy -= impact >> 4;
                            }
                        }

                        fu = face_u;
                        fv = face_v;
                        if(impact > 48 &&
                           (palette == KD_PALETTE_SOURCE_FULL || palette == KD_PALETTE_NEON)) {
                            fu = kd_clampi(fu + (((face_u - 128) * impact) >> 10), 16, 240);
                            fv = kd_clampi(fv + (((face_v - 128) * impact) >> 10), 16, 240);
                        }
                    } else if(lx >= face_x1 && lx < face_x1 + side &&
                              ly >= face_y0 + side / 2 && ly < face_y1 + side / 2) {
                        const int d = lx - face_x1;
                        fy = face_y - 47 - d * 7 - (impact >> 5);
                        fu = side_u;
                        fv = side_v;
                    } else if(ly >= face_y1 && ly < face_y1 + side &&
                              lx >= face_x0 + side / 2 && lx < face_x1 + side / 2) {
                        const int d = ly - face_y1;
                        fy = face_y - 68 - d * 8 - (impact >> 5);
                        fu = side_u;
                        fv = side_v;
                    } else if(lx >= face_x1 + side / 2 || ly >= face_y1 + side / 2) {
                        fy = base_y - 4 - (lift >> 1) - (impact >> 6);
                        fu = shadow_u;
                        fv = shadow_v;
                    }

                    kd_write_pixel(Y, U, V, p, sy, su, sv, fy, fu, fv, amount_q);
                }
            }
        }
    }
}

void mechanicalpixels_apply(void *ptr, VJFrame *frame, int *args)
{
    kinetic_t *k = (kinetic_t *) ptr;
    if(!k || !frame || !args) return;

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    if(!Y || !U || !V) return;

    const int len = frame->width * frame->height;
    if(frame->width != k->w || frame->height != k->h || len != k->len) return;

    const int amount = kd_clampi(args[P_AMOUNT], 0, 100);
    const int cell_size = kd_clampi(args[P_CELL_SIZE], KD_CELL_MIN, KD_CELL_MAX);
    const int depth = kd_clampi(args[P_DEPTH], 0, 100);
    const int motor_speed = kd_clampi(args[P_MOTOR], 0, 100);
    const int trigger = kd_clampi(args[P_TRIGGER], 0, 100);
    const int wave = kd_clampi(args[P_WAVE], 0, KD_WAVES - 1);
    const int inertia = kd_clampi(args[P_INERTIA], 0, 100);
    const int palette = kd_clampi(args[P_PALETTE], 0, KD_PALETTES - 1);
    const int reset = kd_clampi(args[P_RESET], 0, 1);

    if(cell_size != k->last_cell_size) {
        kd_configure_grid(k, cell_size);
        kd_clear_cells(k);
        k->seeded = 0;
        k->motor_pos = 0;
    }

    if(!k->seeded || (reset && !k->last_reset)) {
        kd_seed_cells(k, Y, U, V, cell_size);
        veejay_memcpy(k->prev_y, Y, (size_t) k->len);
        k->seeded = 1;
        k->motor_pos = 0;
    }
    k->last_reset = reset;

    if(amount <= 0) {
        veejay_memcpy(k->prev_y, Y, (size_t) k->len);
        if(motor_speed > 0)
            k->motor_pos = (k->motor_pos + 1 + (motor_speed * 11) / 100) & 255;
        k->frame++;
        return;
    }

    kd_update_cells(k, Y, U, V, cell_size, motor_speed, trigger, wave, inertia);

    veejay_memcpy(k->prev_y, Y, (size_t) k->len);

    kd_render_wall(k, frame, cell_size, amount, depth, palette);

    if(motor_speed > 0)
        k->motor_pos = (k->motor_pos + 1 + (motor_speed * 11) / 100) & 255;
    k->frame++;
}
