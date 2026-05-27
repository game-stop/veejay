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
#include "kineticdisplay.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#define KD_PARAMS 12

#define P_AMOUNT       0
#define P_CELL_SIZE    1
#define P_THRESHOLD    2
#define P_DITHER       3
#define P_FLIP_SPEED   4
#define P_LAG          5
#define P_PERSIST      6
#define P_BRIGHTNESS   7
#define P_CONTRAST     8
#define P_MODE         9
#define P_MOTION       10
#define P_RESET        11

#define KD_CELL_MIN 4
#define KD_CELL_MAX 64
#define KD_MODES 12

#define KD_MODE_FLIP_DOT       0
#define KD_MODE_SPLIT_FLAP     1
#define KD_MODE_RELAY_LAMPS    2
#define KD_MODE_SHUTTERS       3
#define KD_MODE_AIRPORT_BOARD  4
#define KD_MODE_MAGNETIC       5
#define KD_MODE_BLINDS         6
#define KD_MODE_PUNCH_CARD     7
#define KD_MODE_DOT_MATRIX     8
#define KD_MODE_BROKEN_SIGN    9
#define KD_MODE_WAVE_FLIP      10
#define KD_MODE_RANDOM_CASCADE 11

typedef struct {
    int w;
    int h;
    int len;
    int n_threads;
    int frame;
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

    uint8_t *cell_value;
    uint8_t *cell_target;
    uint8_t *cell_delay;
    uint8_t *cell_phase;
    uint8_t *cell_rand;
    uint8_t *cell_dead;
    uint8_t *cell_avg_y;
    uint8_t *cell_avg_u;
    uint8_t *cell_avg_v;
    uint8_t *cell_motion;
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
    uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u + (uint32_t) salt * 2246822519u;
    return (uint8_t) (kd_hash_u32(h) & 255u);
}

static inline int kd_blend_q8(int src, int fx, int aq)
{
    return src + (((fx - src) * aq + 128) >> 8);
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
}

vj_effect *kineticdisplay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve) return NULL;

    ve->num_params = KD_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_AMOUNT] = 0;       ve->limits[1][P_AMOUNT] = 100;       ve->defaults[P_AMOUNT] = 100;
    ve->limits[0][P_CELL_SIZE] = 4;    ve->limits[1][P_CELL_SIZE] = 64;     ve->defaults[P_CELL_SIZE] = 12;
    ve->limits[0][P_THRESHOLD] = 0;    ve->limits[1][P_THRESHOLD] = 255;   ve->defaults[P_THRESHOLD] = 116;
    ve->limits[0][P_DITHER] = 0;       ve->limits[1][P_DITHER] = 100;      ve->defaults[P_DITHER] = 34;
    ve->limits[0][P_FLIP_SPEED] = 1;   ve->limits[1][P_FLIP_SPEED] = 100;  ve->defaults[P_FLIP_SPEED] = 48;
    ve->limits[0][P_LAG] = 0;          ve->limits[1][P_LAG] = 100;         ve->defaults[P_LAG] = 42;
    ve->limits[0][P_PERSIST] = 0;      ve->limits[1][P_PERSIST] = 100;     ve->defaults[P_PERSIST] = 68;
    ve->limits[0][P_BRIGHTNESS] = 20;  ve->limits[1][P_BRIGHTNESS] = 220;  ve->defaults[P_BRIGHTNESS] = 130;
    ve->limits[0][P_CONTRAST] = 20;    ve->limits[1][P_CONTRAST] = 220;    ve->defaults[P_CONTRAST] = 125;
    ve->limits[0][P_MODE] = 0;         ve->limits[1][P_MODE] = KD_MODES-1; ve->defaults[P_MODE] = KD_MODE_FLIP_DOT;
    ve->limits[0][P_MOTION] = 0;       ve->limits[1][P_MOTION] = 100;      ve->defaults[P_MOTION] = 55;
    ve->limits[0][P_RESET] = 0;        ve->limits[1][P_RESET] = 1;         ve->defaults[P_RESET] = 0;

    ve->description = "Kinetic Display Machine";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params,
        "Amount",
        "Cell Size",
        "Threshold",
        "Dither",
        "Flip Speed",
        "Mechanical Lag",
        "Persistence",
        "Brightness",
        "Contrast",
        "Display Mode",
        "Motion Reactivity",
        "Reset State"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_REJECT,                                                      VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Amount */
        VJ_BEAT_GRID_SIZE,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 6,                  32,                 6,  22, 2200, 5200, 1800, 25,    /* Cell Size */
        VJ_BEAT_DETAIL,           VJ_BEAT_F_PHRASE_ONLY,                                                 64,                 180,                6,  22, 1600, 3400, 700,  35,    /* Threshold */
        VJ_BEAT_DETAIL,           VJ_BEAT_F_CONTINUOUS,                                                  0,                  80,                 8,  30, 1200, 3000, 0,    45,    /* Dither */
        VJ_BEAT_SPEED,            VJ_BEAT_F_CONTINUOUS,                                                  18,                 92,                 12, 46, 900,  2400, 0,    75,    /* Flip Speed */
        VJ_BEAT_INERTIA,          VJ_BEAT_F_CONTINUOUS,                                                  8,                  80,                 10, 38, 1000, 2800, 0,    55,    /* Mechanical Lag */
        VJ_BEAT_MEMORY,           VJ_BEAT_F_CONTINUOUS,                                                  30,                 92,                 8,  32, 1200, 3200, 0,    50,    /* Persistence */
        VJ_BEAT_GLOW,             VJ_BEAT_F_CONTINUOUS,                                                  70,                 190,                10, 36, 1000, 2600, 0,    55,    /* Brightness */
        VJ_BEAT_DETAIL,           VJ_BEAT_F_CONTINUOUS,                                                  70,                 180,                8,  30, 1200, 3000, 0,    45,    /* Contrast */
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                               VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Display Mode */
        VJ_BEAT_MOTION_REACT,     VJ_BEAT_F_CONTINUOUS,                                                  20,                 95,                 12, 46, 900,  2400, 0,    70,    /* Motion Reactivity */
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE,     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000  /* Reset State */
    );
    (void) w;
    (void) h;
    return ve;
}

void *kineticdisplay_malloc(int w, int h)
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

    const size_t len = (size_t) k->len;
    const size_t cells = (size_t) k->max_cells;
    const size_t total = len + cells * 10u;

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
    k->cell_dead = p; p += cells;
    k->cell_avg_y = p; p += cells;
    k->cell_avg_u = p; p += cells;
    k->cell_avg_v = p; p += cells;
    k->cell_motion = p;

    for(int y = 0; y < k->max_rows; y++) {
        for(int x = 0; x < k->max_cols; x++) {
            const int idx = y * k->max_cols + x;
            k->cell_rand[idx] = kd_hash_cell(x, y, 11);
            k->cell_dead[idx] = kd_hash_cell(x, y, 97);
        }
    }

    kd_configure_grid(k, 12);
    kd_clear_cells(k);
    return (void *) k;
}

void kineticdisplay_free(void *ptr)
{
    kinetic_t *k = (kinetic_t *) ptr;
    if(!k) return;
    free(k->region);
    free(k);
}

static void kd_seed_cells(kinetic_t *k, const uint8_t *Y, const uint8_t *U, const uint8_t *V, int cell_size)
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
            const uint8_t ay = (uint8_t) (sum_y / count);
            k->cell_avg_y[idx] = ay;
            k->cell_avg_u[idx] = (uint8_t) (sum_u / count);
            k->cell_avg_v[idx] = (uint8_t) (sum_v / count);
            k->cell_motion[idx] = 0;
            k->cell_value[idx] = ay;
            k->cell_target[idx] = ay;
            k->cell_delay[idx] = 0;
            k->cell_phase[idx] = 0;
        }
    }
}

static void kd_update_cells(kinetic_t *k, const uint8_t *Y, const uint8_t *U, const uint8_t *V,
                            int cell_size, int threshold, int dither, int speed, int lag,
                            int persistence, int contrast, int motion_react, int mode)
{
    const int w = k->w;
    const int h = k->h;
    const int cols = k->cols;
    const int rows = k->rows;
    const int max_cols = k->max_cols;
    const int frame = k->frame;

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

            int ay = sum_y / count;
            const int au = sum_u / count;
            const int av = sum_v / count;
            int motion = sum_m / count;
            if(motion > 255) motion = 255;

            k->cell_avg_y[idx] = (uint8_t) ay;
            k->cell_avg_u[idx] = (uint8_t) au;
            k->cell_avg_v[idx] = (uint8_t) av;
            k->cell_motion[idx] = (uint8_t) motion;

            int cyy = 128 + (((ay - 128) * contrast) / 100);
            cyy += (motion * motion_react) / 180;
            cyy = kd_clampi(cyy, 0, 255);

            const int rnd = (int) k->cell_rand[idx] - 128;
            const int dthr = threshold + ((rnd * dither) / 100);
            int target = (cyy > dthr) ? cyy : 0;

            if(mode == KD_MODE_PUNCH_CARD) {
                target = (cyy > dthr) ? 255 : 0;
            } else if(mode == KD_MODE_DOT_MATRIX) {
                target = cyy;
            } else if(mode == KD_MODE_RELAY_LAMPS) {
                target = (cyy > dthr) ? kd_clampi(cyy + motion / 2, 0, 255) : 0;
            }

            const int old_target = k->cell_target[idx];
            if(abs(target - old_target) > 6) {
                k->cell_target[idx] = (uint8_t) target;
                if(k->cell_delay[idx] == 0) {
                    int delay = (lag * (int) k->cell_rand[idx]) / 255;
                    delay += (lag * (255 - motion)) / 512;
                    if(mode == KD_MODE_WAVE_FLIP) {
                        delay += ((cx * 5 + cy * 3 + frame) & 31) * lag / 150;
                    } else if(mode == KD_MODE_RANDOM_CASCADE) {
                        delay += (kd_hash_cell(cx, cy, frame >> 2) * lag) / 180;
                    } else if(mode == KD_MODE_AIRPORT_BOARD) {
                        delay += ((cy * 7 + cx) & 15) * lag / 180;
                    }
                    if(delay > 255) delay = 255;
                    k->cell_delay[idx] = (uint8_t) delay;
                }
            }

            if(k->cell_delay[idx] > 0) {
                k->cell_delay[idx]--;
                k->cell_phase[idx] = 255;
                continue;
            }

            int value = k->cell_value[idx];
            target = k->cell_target[idx];

            int boost = (motion * motion_react) / 255;
            int local_speed = speed + ((100 - speed) * boost) / 100;
            int step = 2 + (local_speed * 52) / 100;

            if(target < value) {
                int damp = 20 + (100 - persistence);
                step = (step * damp) / 120;
                if(step < 1) step = 1;
            }

            if(value < target) {
                value += step;
                if(value > target) value = target;
            } else if(value > target) {
                value -= step;
                if(value < target) value = target;
            }

            k->cell_value[idx] = (uint8_t) value;
            k->cell_phase[idx] = (uint8_t) kd_clampi(abs(target - value), 0, 255);
        }
    }
}

static inline void kd_write_pixel(uint8_t *Y, uint8_t *U, uint8_t *V, int p,
                                  int src_y, int src_u, int src_v,
                                  int fx_y, int fx_u, int fx_v, int amount_q)
{
    /*
     * Amount is still a dry/wet mix when below 100.
     * At Amount=100, however, write the generated display directly.
     * This avoids a tiny source contribution from Q8 rounding and keeps
     * Display Mode output fully opaque when requested.
     */
    if(amount_q >= 256) {
        Y[p] = kd_clip_u8(fx_y);
        U[p] = kd_clip_u8(fx_u);
        V[p] = kd_clip_u8(fx_v);
        return;
    }

    Y[p] = kd_clip_u8(kd_blend_q8(src_y, fx_y, amount_q));
    U[p] = kd_clip_u8(kd_blend_q8(src_u, fx_u, amount_q));
    V[p] = kd_clip_u8(kd_blend_q8(src_v, fx_v, amount_q));
}

static void kd_render_cells(kinetic_t *k, VJFrame *frame, int cell_size, int amount, int brightness, int mode)
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
    const int frame_no = k->frame;

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

            const int value = k->cell_value[idx];
            const int phase = k->cell_phase[idx];
            const int rnd = k->cell_rand[idx];
            const int dead = k->cell_dead[idx];
            const int avg_u = k->cell_avg_u[idx];
            const int avg_v = k->cell_avg_v[idx];
            const int motion = k->cell_motion[idx];

            int lit = (value * brightness) / 100;
            if(mode == KD_MODE_RANDOM_CASCADE) {
                lit += ((kd_hash_cell(cx, cy, frame_no) & 31) * phase) >> 8;
            }
            lit = kd_clampi(lit, 0, 255);

            int fx_u_lit = 128 + ((value * (avg_u - 128)) >> 8);
            int fx_v_lit = 128 + ((value * (avg_v - 128)) >> 8);
            int dark = 4 + ((rnd & 15) * 3) / 2;

            if(mode == KD_MODE_BROKEN_SIGN && dead > 230) {
                lit = (lit * 18) >> 8;
                fx_u_lit = 128;
                fx_v_lit = 128;
            }

            const int cx2 = (cw - 1);
            const int cy2 = (ch - 1);
            const int rx = (cw > 2) ? cw - 2 : cw;
            const int ry = (ch > 2) ? ch - 2 : ch;
            const int r2max = ((rx * rx) + (ry * ry));
            const int sub_w = (cw >= 9) ? cw / 3 : (cw >= 6 ? cw / 2 : cw);
            const int sub_h = (ch >= 9) ? ch / 3 : (ch >= 6 ? ch / 2 : ch);
            const int dot_order[9] = { 0, 6, 2, 8, 4, 1, 7, 3, 5 };

            for(int y = y0; y < y1; y++) {
                const int ly = y - y0;
                const int off = y * w;
                for(int x = x0; x < x1; x++) {
                    const int lx = x - x0;
                    const int p = off + x;
                    const int sy = Y[p];
                    const int su = U[p];
                    const int sv = V[p];

                    int fy = dark;
                    int fu = 128;
                    int fv = 128;

                    switch(mode) {
                        case KD_MODE_FLIP_DOT: {
                            const int dx = (lx << 1) - cx2;
                            const int dy = (ly << 1) - cy2;
                            const int r2 = dx * dx + dy * dy;
                            const int inside = (r2 * 2 <= r2max);
                            if(inside) {
                                fy = lit;
                                if(dx < 0 && dy < 0) fy = kd_clampi(fy + 18, 0, 255);
                                fu = fx_u_lit;
                                fv = fx_v_lit;
                            }
                            break;
                        }
                        case KD_MODE_SPLIT_FLAP: {
                            fy = lit;
                            if(ly < ch / 2) fy = kd_clampi(fy + 16, 0, 255);
                            else fy = kd_clampi(fy - 18, 0, 255);
                            if(ly == ch / 2 || ly == ch / 2 - 1) fy = dark;
                            if(phase > 24) {
                                const int flap = (phase * ch) >> 8;
                                if(abs(ly - flap) <= 1) fy = 245;
                            }
                            fu = fx_u_lit;
                            fv = fx_v_lit;
                            break;
                        }
                        case KD_MODE_RELAY_LAMPS: {
                            const int dx = (lx << 1) - cx2;
                            const int dy = (ly << 1) - cy2;
                            const int r2 = dx * dx + dy * dy;
                            if(r2 < r2max) {
                                int fall = r2max - r2;
                                fy = dark + (lit * fall) / (r2max ? r2max : 1);
                                if(r2 < (r2max >> 3)) fy = kd_clampi(fy + 28, 0, 255);
                                fu = fx_u_lit;
                                fv = fx_v_lit;
                            }
                            break;
                        }
                        case KD_MODE_SHUTTERS: {
                            const int open = (value * cw) >> 8;
                            const int left = (cw - open) >> 1;
                            const int right = left + open;

                            if(open > 0 && lx >= left && lx < right) {
                                /*
                                 * Opaque shutter: never average with sy/su/sv here.
                                 * The source is already represented by cell_value and
                                 * per-cell chroma; per-pixel source texture must not
                                 * shine through this display mode.
                                 */
                                int shade = lit;

                                if(lx == left || lx == right - 1) {
                                    shade = dark;
                                } else {
                                    const int rel = lx - left;
                                    const int grad = (open > 1) ? ((rel * 32) / open) : 0;
                                    shade = kd_clampi(lit + 10 - grad, 0, 255);
                                }

                                fy = shade;
                                fu = fx_u_lit;
                                fv = fx_v_lit;
                            } else {
                                fy = dark;
                                fu = 128;
                                fv = 128;
                            }
                            break;
                        }
                        case KD_MODE_AIRPORT_BOARD: {
                            const int band = (ch >= 6) ? ch / 3 : 1;
                            const int seam = (band > 0 && (ly % band) == 0);
                            fy = seam ? dark : lit;
                            if(((ly / (band ? band : 1)) & 1) == 0) fy = kd_clampi(fy + 12, 0, 255);
                            if(phase > 96 && ((ly + frame_no + rnd) & 7) == 0) fy = dark;
                            fu = fx_u_lit;
                            fv = fx_v_lit;
                            break;
                        }
                        case KD_MODE_MAGNETIC: {
                            const int diag = (lx * ch > ly * cw);
                            fy = diag ? lit : (lit >> 2);
                            if(abs((lx * ch) - (ly * cw)) < cw + ch) fy = kd_clampi(fy + 22, 0, 255);
                            fu = fx_u_lit;
                            fv = fx_v_lit;
                            break;
                        }
                        case KD_MODE_BLINDS: {
                            const int slat = (cw >= 16) ? 4 : 3;
                            const int open = 1 + ((value * (slat - 1)) >> 8);
                            const int pos = (lx + ((phase * slat) >> 8)) % slat;

                            if(pos < open) {
                                /*
                                 * Opaque blind slat: do not blend with sy/su/sv.
                                 * This preserves the mechanical-blinds look without
                                 * letting the original source texture leak through.
                                 */
                                int shade = lit;

                                if(pos == 0) {
                                    shade = kd_clampi(lit + 14, 0, 255);
                                } else if(pos == open - 1) {
                                    shade = kd_clampi(lit - 18, 0, 255);
                                }

                                fy = shade;
                                fu = fx_u_lit;
                                fv = fx_v_lit;
                            } else {
                                fy = dark;
                                fu = 128;
                                fv = 128;
                            }
                            break;
                        }
                        case KD_MODE_PUNCH_CARD: {
                            fy = 206;
                            fu = 118;
                            fv = 146;
                            if(value > 64) {
                                const int dx = (lx << 1) - cx2;
                                const int dy = (ly << 1) - cy2;
                                const int r2 = dx * dx + dy * dy;
                                if(r2 * 3 < r2max) {
                                    fy = 18;
                                    fu = 128;
                                    fv = 128;
                                }
                            }
                            break;
                        }
                        case KD_MODE_DOT_MATRIX: {
                            int sx = (sub_w > 0) ? lx / sub_w : 0;
                            int syb = (sub_h > 0) ? ly / sub_h : 0;
                            if(sx > 2) sx = 2;
                            if(syb > 2) syb = 2;
                            const int cell = syb * 3 + sx;
                            const int dots = (value * 10) >> 8;
                            const int on = dot_order[cell] < dots;
                            if(on) {
                                const int cxp = sx * sub_w + sub_w / 2;
                                const int cyp = syb * sub_h + sub_h / 2;
                                const int dx = lx - cxp;
                                const int dy = ly - cyp;
                                const int rr = (sub_w < sub_h ? sub_w : sub_h) / 2;
                                fy = (dx * dx + dy * dy <= rr * rr) ? lit : dark;
                                fu = fx_u_lit;
                                fv = fx_v_lit;
                            }
                            break;
                        }
                        case KD_MODE_BROKEN_SIGN: {
                            const int dx = (lx << 1) - cx2;
                            const int dy = (ly << 1) - cy2;
                            const int r2 = dx * dx + dy * dy;
                            if(r2 * 2 <= r2max) {
                                int flick = kd_hash_cell(cx, cy, frame_no) & 63;
                                int local = lit;
                                if(flick < (phase >> 3)) local >>= 2;
                                fy = local;
                                fu = fx_u_lit;
                                fv = fx_v_lit;
                            }
                            break;
                        }
                        case KD_MODE_WAVE_FLIP: {
                            int wave = ((cx * 13 + cy * 7 + frame_no * 3) & 63);
                            int band = abs(((lx + ly + wave) & 31) - 16);
                            fy = kd_clampi(lit + motion - band * 4, 0, 255);
                            if((ly == 0) || (lx == 0)) fy >>= 1;
                            fu = fx_u_lit;
                            fv = fx_v_lit;
                            break;
                        }
                        case KD_MODE_RANDOM_CASCADE:
                        default: {
                            int local = lit;
                            int hsh = kd_hash_cell(cx, cy, frame_no + lx + ly);
                            if((hsh & 255) < phase) local = 255 - local;
                            if((hsh & 31) == 0) local = 255;
                            fy = kd_clampi(local, 0, 255);
                            fu = fx_u_lit;
                            fv = fx_v_lit;
                            break;
                        }
                    }

                    kd_write_pixel(Y, U, V, p, sy, su, sv, fy, fu, fv, amount_q);
                }
            }
        }
    }
}

void kineticdisplay_apply(void *ptr, VJFrame *frame, int *args)
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
    const int threshold = kd_clampi(args[P_THRESHOLD], 0, 255);
    const int dither = kd_clampi(args[P_DITHER], 0, 100);
    const int speed = kd_clampi(args[P_FLIP_SPEED], 1, 100);
    const int lag = kd_clampi(args[P_LAG], 0, 100);
    const int persistence = kd_clampi(args[P_PERSIST], 0, 100);
    const int brightness = kd_clampi(args[P_BRIGHTNESS], 20, 220);
    const int contrast = kd_clampi(args[P_CONTRAST], 20, 220);
    const int mode = kd_clampi(args[P_MODE], 0, KD_MODES - 1);
    const int motion_react = kd_clampi(args[P_MOTION], 0, 100);
    const int reset = kd_clampi(args[P_RESET], 0, 1);

    if(cell_size != k->last_cell_size) {
        kd_configure_grid(k, cell_size);
        kd_clear_cells(k);
        k->seeded = 0;
    }

    if(!k->seeded || (reset && !k->last_reset)) {
        kd_seed_cells(k, Y, U, V, cell_size);
        veejay_memcpy(k->prev_y, Y, (size_t) k->len);
        k->seeded = 1;
    }
    k->last_reset = reset;

    if(amount <= 0) {
        veejay_memcpy(k->prev_y, Y, (size_t) k->len);
        k->frame++;
        return;
    }

    kd_update_cells(k, Y, U, V, cell_size, threshold, dither, speed, lag,
                    persistence, contrast, motion_react, mode);

    veejay_memcpy(k->prev_y, Y, (size_t) k->len);

    kd_render_cells(k, frame, cell_size, amount, brightness, mode);

    k->frame++;
}
