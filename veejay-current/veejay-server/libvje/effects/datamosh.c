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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */


#include "common.h"
#include <veejaycore/vjmem.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

#define DM_PARAMS 11

#define P_MOSH      0
#define P_HISTORY   1
#define P_BLOCK     2
#define P_MOTION    3
#define P_PERSIST   4
#define P_TEAR      5
#define P_SLIP      6
#define P_FLOW      7
#define P_FLOW_AMT  8
#define P_CHROMA    9
#define P_RESET     10

#define DM_HISTORY_MAX 24
#define DM_MIN_BLOCK 4
#define DM_FP_SHIFT 8
#define DM_FP_ONE (1 << DM_FP_SHIFT)
#define DM_FP_MASK (DM_FP_ONE - 1)

#define DM_CANDIDATES 17

typedef struct {
    int w;
    int h;
    int len;
    int n_threads;
    int frame;
    int initialized;
    int hist_head;
    int canvas_ping;
    int last_block;
    int last_reset;
    int max_blocks;
    void *region;
    uint8_t *hist[3];
    uint8_t *prev_y;
    uint8_t *canvas[2][3];
    int16_t *field_x;
    int16_t *field_y;
    uint8_t *energy;
} datamosh_t;

static size_t dm_align_size(size_t n, size_t a)
{
    return (n + (a - 1)) & ~(a - 1);
}

static inline int dm_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline uint8_t dm_clamp_u8(int v)
{
    return (uint8_t) ((v < 0) ? 0 : ((v > 255) ? 255 : v));
}

static inline uint32_t dm_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static inline int dm_percent_to_q8(int p)
{
    return (p <= 0) ? 0 : ((p >= 100) ? 256 : ((p * 256 + 50) / 100));
}

static inline uint8_t dm_blend_q8_u8(uint8_t a, uint8_t b, int tq)
{
    return (uint8_t) ((((int) a * (256 - tq)) + ((int) b * tq) + 128) >> 8);
}

static inline int dm_div_pow2_trunc_i32(int v, int sh)
{
    return (v >= 0) ? (v >> sh) : -((-v) >> sh);
}

static inline void dm_sample_bilinear_yuv(const uint8_t * restrict src_y,
                                          const uint8_t * restrict src_u,
                                          const uint8_t * restrict src_v,
                                          int w,
                                          int h,
                                          int xfp,
                                          int yfp,
                                          uint8_t * restrict oy,
                                          uint8_t * restrict ou,
                                          uint8_t * restrict ov)
{
    const int max_xfp = (w - 1) << DM_FP_SHIFT;
    const int max_yfp = (h - 1) << DM_FP_SHIFT;

    if (xfp < 0) xfp = 0;
    else if (xfp > max_xfp) xfp = max_xfp;

    if (yfp < 0) yfp = 0;
    else if (yfp > max_yfp) yfp = max_yfp;

    const int x0 = xfp >> DM_FP_SHIFT;
    const int y0 = yfp >> DM_FP_SHIFT;
    const int x1 = (x0 + 1 < w) ? x0 + 1 : x0;
    const int y1 = (y0 + 1 < h) ? y0 + 1 : y0;
    const int fx = xfp & DM_FP_MASK;
    const int fy = yfp & DM_FP_MASK;
    const int ifx = DM_FP_ONE - fx;
    const int ify = DM_FP_ONE - fy;

    const int i00 = y0 * w + x0;
    const int i01 = y0 * w + x1;
    const int i10 = y1 * w + x0;
    const int i11 = y1 * w + x1;
    const int round = 1 << ((DM_FP_SHIFT * 2) - 1);

    int top = src_y[i00] * ifx + src_y[i01] * fx;
    int bot = src_y[i10] * ifx + src_y[i11] * fx;
    *oy = (uint8_t) ((top * ify + bot * fy + round) >> (DM_FP_SHIFT * 2));

    top = src_u[i00] * ifx + src_u[i01] * fx;
    bot = src_u[i10] * ifx + src_u[i11] * fx;
    *ou = (uint8_t) ((top * ify + bot * fy + round) >> (DM_FP_SHIFT * 2));

    top = src_v[i00] * ifx + src_v[i01] * fx;
    bot = src_v[i10] * ifx + src_v[i11] * fx;
    *ov = (uint8_t) ((top * ify + bot * fy + round) >> (DM_FP_SHIFT * 2));
}

static int dm_block_sad(const uint8_t *cur,
                        const uint8_t *prev,
                        int w,
                        int h,
                        int x0,
                        int y0,
                        int x1,
                        int y1,
                        int dx,
                        int dy,
                        int step,
                        int *samples)
{
    int sad = 0;
    int n = 0;

    for (int y = y0; y < y1; y += step) {
        int py = y + dy;
        if (py < 0) py = 0;
        else if (py >= h) py = h - 1;

        const int row = y * w;
        const int prow = py * w;

        for (int x = x0; x < x1; x += step) {
            int px = x + dx;
            if (px < 0) px = 0;
            else if (px >= w) px = w - 1;

            int d = (int) cur[row + x] - (int) prev[prow + px];
            sad += (d < 0) ? -d : d;
            n++;
        }
    }

    if (samples)
        *samples = n;

    return sad;
}

static void dm_clear_fields(datamosh_t *d)
{
    memset(d->field_x, 0, (size_t) d->max_blocks * sizeof(int16_t));
    memset(d->field_y, 0, (size_t) d->max_blocks * sizeof(int16_t));
    memset(d->energy, 0, (size_t) d->max_blocks);
}

static void dm_fill_history(datamosh_t *d, const uint8_t *src_y, const uint8_t *src_u, const uint8_t *src_v)
{
    const size_t len = (size_t) d->len;

    for (int i = 0; i < DM_HISTORY_MAX; i++) {
        memcpy(d->hist[0] + (size_t) i * len, src_y, len);
        memcpy(d->hist[1] + (size_t) i * len, src_u, len);
        memcpy(d->hist[2] + (size_t) i * len, src_v, len);
    }

    memcpy(d->prev_y, src_y, len);

    for (int p = 0; p < 2; p++) {
        memcpy(d->canvas[p][0], src_y, len);
        memcpy(d->canvas[p][1], src_u, len);
        memcpy(d->canvas[p][2], src_v, len);
    }

    dm_clear_fields(d);

    d->hist_head = 0;
    d->canvas_ping = 0;
    d->initialized = 1;
}

static void dm_push_history(datamosh_t *d,
                            const uint8_t *src_y,
                            const uint8_t *src_u,
                            const uint8_t *src_v,
                            uint8_t **cur_y,
                            uint8_t **cur_u,
                            uint8_t **cur_v)
{
    const size_t len = (size_t) d->len;

    d->hist_head++;
    if (d->hist_head >= DM_HISTORY_MAX)
        d->hist_head = 0;

    *cur_y = d->hist[0] + (size_t) d->hist_head * len;
    *cur_u = d->hist[1] + (size_t) d->hist_head * len;
    *cur_v = d->hist[2] + (size_t) d->hist_head * len;

    memcpy(*cur_y, src_y, len);
    memcpy(*cur_u, src_u, len);
    memcpy(*cur_v, src_v, len);
}

vj_effect *datamosh_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if (!ve)
        return NULL;

    ve->num_params = DM_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_MOSH] = 0;
    ve->limits[1][P_MOSH] = 100;
    ve->defaults[P_MOSH] = 86;

    ve->limits[0][P_HISTORY] = 1;
    ve->limits[1][P_HISTORY] = DM_HISTORY_MAX;
    ve->defaults[P_HISTORY] = 12;

    ve->limits[0][P_BLOCK] = DM_MIN_BLOCK;
    ve->limits[1][P_BLOCK] = 40;
    ve->defaults[P_BLOCK] = 12;

    ve->limits[0][P_MOTION] = 0;
    ve->limits[1][P_MOTION] = 100;
    ve->defaults[P_MOTION] = 58;

    ve->limits[0][P_PERSIST] = 0;
    ve->limits[1][P_PERSIST] = 100;
    ve->defaults[P_PERSIST] = 90;

    ve->limits[0][P_TEAR] = 0;
    ve->limits[1][P_TEAR] = 100;
    ve->defaults[P_TEAR] = 28;

    ve->limits[0][P_SLIP] = 0;
    ve->limits[1][P_SLIP] = 100;
    ve->defaults[P_SLIP] = 66;

    ve->limits[0][P_FLOW] = 0;
    ve->limits[1][P_FLOW] = 7;
    ve->defaults[P_FLOW] = 0;

    ve->limits[0][P_FLOW_AMT] = 0;
    ve->limits[1][P_FLOW_AMT] = 100;
    ve->defaults[P_FLOW_AMT] = 38;

    ve->limits[0][P_CHROMA] = 0;
    ve->limits[1][P_CHROMA] = 100;
    ve->defaults[P_CHROMA] = 64;

    ve->limits[0][P_RESET] = 0;
    ve->limits[1][P_RESET] = 1;
    ve->defaults[P_RESET] = 0;

    ve->sub_format = 1;
    ve->description = "Datamosh History";
    ve->param_description = vje_build_param_list(ve->num_params,
        "Mosh Amount",
        "Time Depth",
        "Block Size",
        "Motion Reactivity",
        "Persistence",
        "Tear",
        "Time Slip",
        "Flow Direction",
        "Flow Strength",
        "Chroma Mosh",
        "Reset Memory"
    );

    (void) w;
    (void) h;
    return ve;
}

void datamosh_free(void *ptr)
{
    datamosh_t *d = (datamosh_t *) ptr;
    if (!d)
        return;

    free(d->region);
    free(d);
}

void *datamosh_malloc(int w, int h)
{
    datamosh_t *d = (datamosh_t *) vj_calloc(sizeof(datamosh_t));
    if (!d)
        return NULL;

    const size_t len = (size_t) w * (size_t) h;
    const int max_bw = (w + DM_MIN_BLOCK - 1) / DM_MIN_BLOCK;
    const int max_bh = (h + DM_MIN_BLOCK - 1) / DM_MIN_BLOCK;
    const size_t max_blocks = (size_t) max_bw * (size_t) max_bh;

    size_t total = 0;
    total += (size_t) 3 * DM_HISTORY_MAX * len;
    total += len;
    total += (size_t) 2 * 3 * len;
    total = dm_align_size(total, sizeof(int16_t));
    total += max_blocks * sizeof(int16_t);
    total += max_blocks * sizeof(int16_t);
    total = dm_align_size(total, sizeof(uint8_t));
    total += max_blocks;

    uint8_t *region = (uint8_t *) vj_calloc(total);
    if (!region) {
        free(d);
        return NULL;
    }

    size_t off = 0;

    d->hist[0] = region + off; off += (size_t) DM_HISTORY_MAX * len;
    d->hist[1] = region + off; off += (size_t) DM_HISTORY_MAX * len;
    d->hist[2] = region + off; off += (size_t) DM_HISTORY_MAX * len;

    d->prev_y = region + off; off += len;

    d->canvas[0][0] = region + off; off += len;
    d->canvas[0][1] = region + off; off += len;
    d->canvas[0][2] = region + off; off += len;
    d->canvas[1][0] = region + off; off += len;
    d->canvas[1][1] = region + off; off += len;
    d->canvas[1][2] = region + off; off += len;

    off = dm_align_size(off, sizeof(int16_t));
    d->field_x = (int16_t *) (region + off); off += max_blocks * sizeof(int16_t);
    d->field_y = (int16_t *) (region + off); off += max_blocks * sizeof(int16_t);
    off = dm_align_size(off, sizeof(uint8_t));
    d->energy = region + off;

    d->w = w;
    d->h = h;
    d->len = (int) len;
    d->max_blocks = (int) max_blocks;
    d->n_threads = vje_advise_num_threads((int) len);
    d->region = region;
    d->last_block = 0;
    d->last_reset = 0;

    return d;
}

void datamosh_apply(void *ptr, VJFrame *frame, int *args)
{
    datamosh_t *d = (datamosh_t *) ptr;
    if (!d)
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = w * h;

    if (w != d->w || h != d->h || len != d->len)
        return;

    uint8_t *frame_y = frame->data[0];
    uint8_t *frame_u = frame->data[1];
    uint8_t *frame_v = frame->data[2];
    const size_t llen = (size_t) len;

    const int reset = args[P_RESET] > 0;

    if (!d->initialized) {
        dm_fill_history(d, frame_y, frame_u, frame_v);
        d->last_reset = reset;
        d->frame++;
        return;
    }

    if (reset && !d->last_reset)
        dm_fill_history(d, frame_y, frame_u, frame_v);

    d->last_reset = reset;

    const int strength = dm_clampi(args[P_MOSH], 0, 100);
    const int history_depth = dm_clampi(args[P_HISTORY], 1, DM_HISTORY_MAX);
    const int block = dm_clampi(args[P_BLOCK], DM_MIN_BLOCK, 40);
    const int motion = dm_clampi(args[P_MOTION], 0, 100);
    const int persist = dm_clampi(args[P_PERSIST], 0, 100);
    const int tear = dm_clampi(args[P_TEAR], 0, 100);
    const int slip = dm_clampi(args[P_SLIP], 0, 100);
    const int flow = dm_clampi(args[P_FLOW], 0, 7);
    const int flow_strength = dm_clampi(args[P_FLOW_AMT], 0, 100);
    const int chroma = dm_clampi(args[P_CHROMA], 0, 100);

    if (block != d->last_block) {
        dm_clear_fields(d);
        d->last_block = block;
    }

    uint8_t *cur_y;
    uint8_t *cur_u;
    uint8_t *cur_v;
    dm_push_history(d, frame_y, frame_u, frame_v, &cur_y, &cur_u, &cur_v);

    if (strength <= 0) {
        veejay_memcpy(d->prev_y, cur_y, llen);
        for (int p = 0; p < 2; p++) {
            veejay_memcpy(d->canvas[p][0], cur_y, llen);
            veejay_memcpy(d->canvas[p][1], cur_u, llen);
            veejay_memcpy(d->canvas[p][2], cur_v, llen);
        }
        dm_clear_fields(d);
        d->canvas_ping = 0;
        d->frame++;
        return;
    }

    const int bw = (w + block - 1) / block;
    const int bh = (h + block - 1) / block;
    const int search_shift = 1 + (motion * 9) / 100;
    const int half_shift = (search_shift + 1) >> 1;
    const int sample_step = (block >= 24) ? 5 : ((block >= 16) ? 4 : ((block >= 9) ? 3 : 2));
    const int amp_q = DM_FP_ONE + (strength * DM_FP_ONE * 4) / 100;
    const int field_keep = 174 + (persist * 81) / 100;
    const int energy_keep = 160 + (persist * 95) / 100;
    const int flow_q = (flow_strength * DM_FP_ONE * 7) / 100;
    const int half_w = w >> 1;
    const int half_h = h >> 1;
    const int max_dim = (w > h) ? w : h;

    const int cand_x[DM_CANDIDATES] = {
        0,
        -half_shift, half_shift, 0, 0, -half_shift, half_shift, -half_shift, half_shift,
        -search_shift, search_shift, 0, 0, -search_shift, search_shift, -search_shift, search_shift
    };
    const int cand_y[DM_CANDIDATES] = {
        0,
        0, 0, -half_shift, half_shift, -half_shift, -half_shift, half_shift, half_shift,
        0, 0, -search_shift, search_shift, -search_shift, -search_shift, search_shift, search_shift
    };

    const int persist_q = dm_percent_to_q8(persist);
    const int chroma_strength = (strength * chroma + 50) / 100;
    const int flow_norm_q = (max_dim > 0) ? ((flow_q * 2 * 256 + (max_dim >> 1)) / max_dim) : 0;

    uint8_t back_lut[256];
    uint16_t hist_mix_q_lut[256];
    uint16_t local_y_q_lut[256];
    uint16_t local_c_q_lut[256];
    int16_t jitter_q_lut[256];

    const int hist_span = history_depth - 1;
    for (int e = 0; e < 256; e++) {
        back_lut[e] = (uint8_t) ((slip * e * hist_span + 12750) / 25500);
        hist_mix_q_lut[e] = (uint16_t) ((slip * e * 256 + 12750) / 25500);

        const int ey = 76 + ((e * 24) / 255);
        const int ec = 72 + ((e * 28) / 255);
        const int local_y_strength = dm_clampi((strength * ey + 50) / 100, 0, 100);
        const int local_c_strength = dm_clampi((chroma_strength * ec + 50) / 100, 0, 100);
        local_y_q_lut[e] = (uint16_t) dm_percent_to_q8(local_y_strength);
        local_c_q_lut[e] = (uint16_t) dm_percent_to_q8(local_c_strength);

        jitter_q_lut[e] = (int16_t) ((tear * (40 + e) * DM_FP_ONE * 5) / (100 * 295));
    }

    const int read_ping = d->canvas_ping;
    const int write_ping = read_ping ^ 1;

    const uint8_t *read_y = d->canvas[read_ping][0];
    const uint8_t *read_u = d->canvas[read_ping][1];
    const uint8_t *read_v = d->canvas[read_ping][2];
    uint8_t *write_y = d->canvas[write_ping][0];
    uint8_t *write_u = d->canvas[write_ping][1];
    uint8_t *write_v = d->canvas[write_ping][2];

#pragma omp parallel num_threads(d->n_threads)
    {
#pragma omp for schedule(static)
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                const int bi = by * bw + bx;
                const int x0 = bx * block;
                const int y0 = by * block;
                const int x1 = (x0 + block < w) ? x0 + block : w;
                const int y1 = (y0 + block < h) ? y0 + block : h;

                int samples = 0;
                const int zero_sad = dm_block_sad(cur_y, d->prev_y, w, h, x0, y0, x1, y1, 0, 0, sample_step, &samples);
                int best_sad = zero_sad;
                int best_dx = 0;
                int best_dy = 0;

                if (motion > 0) {
                    for (int c = 1; c < DM_CANDIDATES; c++) {
                        const int dx = cand_x[c];
                        const int dy = cand_y[c];
                        int sad = dm_block_sad(cur_y, d->prev_y, w, h, x0, y0, x1, y1, dx, dy, sample_step, NULL);
                        if (sad < best_sad) {
                            best_sad = sad;
                            best_dx = dx;
                            best_dy = dy;
                        }
                    }
                }

                const int mean_zero = (samples > 0) ? zero_sad / samples : 0;
                const int improve = (samples > 0) ? (zero_sad - best_sad) / samples : 0;
                const int threshold = 3 + (100 - motion) / 9;
                int act = (mean_zero - threshold) * (motion + 24) * 2 / 100;
                if (improve > 0)
                    act += improve * (motion + 25) / 90;
                act = dm_clampi(act, 0, 255);

                int target_x = best_dx * amp_q;
                int target_y = best_dy * amp_q;

                if (flow_q > 0 && flow != 0) {
                    const int cx = x0 + ((x1 - x0) >> 1);
                    const int cy = y0 + ((y1 - y0) >> 1);
                    const int rx = cx - half_w;
                    const int ry = cy - half_h;

                    switch (flow) {
                        case 1: target_x -= flow_q; break;
                        case 2: target_x += flow_q; break;
                        case 3: target_y -= flow_q; break;
                        case 4: target_y += flow_q; break;
                        case 5:
                            target_x += (rx * flow_norm_q) >> 8;
                            target_y += (ry * flow_norm_q) >> 8;
                            break;
                        case 6:
                            target_x -= (rx * flow_norm_q) >> 8;
                            target_y -= (ry * flow_norm_q) >> 8;
                            break;
                        case 7:
                            target_x += (-ry * flow_norm_q) >> 8;
                            target_y += ( rx * flow_norm_q) >> 8;
                            break;
                        default:
                            break;
                    }
                }

                if (tear > 0) {
                    uint32_t hv = dm_hash_u32((uint32_t) bi * 1103515245u + (uint32_t) d->frame * 12345u);
                    const int jitter_q = jitter_q_lut[act];
                    target_x += dm_div_pow2_trunc_i32(((int) (hv & 255u) - 128) * jitter_q, 7);
                    target_y += dm_div_pow2_trunc_i32(((int) ((hv >> 8) & 255u) - 128) * jitter_q, 7);
                }

                int old_x = d->field_x[bi];
                int old_y = d->field_y[bi];
                int new_x;
                int new_y;

                if (act > 0) {
                    const int follow = 36 + ((act * (motion + 40)) / 355);
                    new_x = (old_x * (255 - follow) + target_x * follow) / 255;
                    new_y = (old_y * (255 - follow) + target_y * follow) / 255;
                } else {
                    new_x = (old_x * field_keep) / 255;
                    new_y = (old_y * field_keep) / 255;
                }

                d->field_x[bi] = (int16_t) dm_clampi(new_x, -32768, 32767);
                d->field_y[bi] = (int16_t) dm_clampi(new_y, -32768, 32767);

                int old_e = d->energy[bi];
                int new_e = (act > old_e) ? act : (old_e * energy_keep) / 255;
                if (flow_q > 0 && flow != 0 && new_e < flow_strength)
                    new_e = flow_strength;
                d->energy[bi] = (uint8_t) dm_clampi(new_e, 0, 255);
            }
        }

#pragma omp single
        {
            veejay_memcpy(d->prev_y, cur_y, llen);
        }

#pragma omp for schedule(static)
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                const int bi = by * bw + bx;
                const int fx = d->field_x[bi];
                const int fy = d->field_y[bi];
                const int e = d->energy[bi];
                const int back = back_lut[e];
                int hslot = d->hist_head - back;
                if (hslot < 0)
                    hslot += DM_HISTORY_MAX;

                const uint8_t *hy = d->hist[0] + (size_t) hslot * llen;
                const uint8_t *hu = d->hist[1] + (size_t) hslot * llen;
                const uint8_t *hv = d->hist[2] + (size_t) hslot * llen;

                const int hist_mix_q = hist_mix_q_lut[e];
                const int local_y_q = local_y_q_lut[e];
                const int local_c_q = local_c_q_lut[e];

                const int x0 = bx * block;
                const int y0 = by * block;
                const int x1 = (x0 + block < w) ? x0 + block : w;
                const int y1 = (y0 + block < h) ? y0 + block : h;

                for (int y = y0; y < y1; y++) {
                    const int row = y * w;
                    const int sy_fp = (y << DM_FP_SHIFT) + fy;

                    for (int x = x0; x < x1; x++) {
                        const int idx = row + x;
                        const int sx_fp = (x << DM_FP_SHIFT) + fx;

                        uint8_t old_y;
                        uint8_t old_u;
                        uint8_t old_v;
                        dm_sample_bilinear_yuv(read_y, read_u, read_v, w, h, sx_fp, sy_fp, &old_y, &old_u, &old_v);

                        const uint8_t iny = dm_blend_q8_u8(cur_y[idx], hy[idx], hist_mix_q);
                        const uint8_t inu = dm_blend_q8_u8(cur_u[idx], hu[idx], hist_mix_q);
                        const uint8_t inv = dm_blend_q8_u8(cur_v[idx], hv[idx], hist_mix_q);

                        const uint8_t my = dm_blend_q8_u8(iny, old_y, persist_q);
                        const uint8_t mu = dm_blend_q8_u8(inu, old_u, persist_q);
                        const uint8_t mv = dm_blend_q8_u8(inv, old_v, persist_q);

                        write_y[idx] = my;
                        write_u[idx] = mu;
                        write_v[idx] = mv;

                        frame_y[idx] = dm_blend_q8_u8(cur_y[idx], my, local_y_q);
                        frame_u[idx] = dm_blend_q8_u8(cur_u[idx], mu, local_c_q);
                        frame_v[idx] = dm_blend_q8_u8(cur_v[idx], mv, local_c_q);
                    }
                }
            }
        }
    }

    d->canvas_ping = write_ping;
    d->frame++;
}
