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
#include "datamosh.h"

#define DM_PARAMS 11

#define P_FLOW      0
#define P_MOSH      1
#define P_MOTION    2
#define P_BLOCK     3
#define P_HISTORY   4
#define P_SLIP      5
#define P_PERSIST   6
#define P_FLOW_AMT  7
#define P_TEAR      8
#define P_SRC_OPAC  9
#define P_RESET     10

#define DM_HISTORY_MAX 24
#define DM_MIN_BLOCK 4
#define DM_FP_SHIFT 8
#define DM_FP_ONE (1 << DM_FP_SHIFT)
#define DM_FP_MASK (DM_FP_ONE - 1)

#define DM_CANDIDATES 17
#define DM_CANDIDATES_LOW 9
#define DM_CANDIDATES_HALF 9

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
    int last_flow;
    int last_reset;
    int max_blocks;
    void *region;
    uint8_t *hist[3];
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


static inline int dm_div_pow2_trunc_i32(int v, int sh)
{
    return (v >= 0) ? (v >> sh) : -((-v) >> sh);
}

static inline uint8_t dm_sample_y_bilinear_inner_idx(const uint8_t * restrict src_y,
                                                     int w,
                                                     int xfp,
                                                     int yfp,
                                                     int * restrict src_idx)
{
    const int x0 = xfp >> DM_FP_SHIFT;
    const int y0 = yfp >> DM_FP_SHIFT;
    const int fx = xfp & DM_FP_MASK;
    const int fy = yfp & DM_FP_MASK;
    const int ifx = DM_FP_ONE - fx;
    const int ify = DM_FP_ONE - fy;
    const int i00 = y0 * w + x0;
    const int i01 = i00 + 1;
    const int i10 = i00 + w;
    const int i11 = i10 + 1;
    const int round = 1 << ((DM_FP_SHIFT * 2) - 1);

    *src_idx = i00;

    const int top = src_y[i00] * ifx + src_y[i01] * fx;
    const int bot = src_y[i10] * ifx + src_y[i11] * fx;
    return (uint8_t) ((top * ify + bot * fy + round) >> (DM_FP_SHIFT * 2));
}

static inline uint8_t dm_sample_y_bilinear_clamped_idx(const uint8_t * restrict src_y,
                                                       int w,
                                                       int h,
                                                       int xfp,
                                                       int yfp,
                                                       int * restrict src_idx)
{
    const int max_xfp = (w - 1) << DM_FP_SHIFT;
    const int max_yfp = (h - 1) << DM_FP_SHIFT;

    xfp = (xfp < 0) ? 0 : ((xfp > max_xfp) ? max_xfp : xfp);
    yfp = (yfp < 0) ? 0 : ((yfp > max_yfp) ? max_yfp : yfp);

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

    *src_idx = i00;

    const int top = src_y[i00] * ifx + src_y[i01] * fx;
    const int bot = src_y[i10] * ifx + src_y[i11] * fx;
    return (uint8_t) ((top * ify + bot * fy + round) >> (DM_FP_SHIFT * 2));
}

static int dm_block_sad_inner(const uint8_t * restrict cur,
                              const uint8_t * restrict prev,
                              int w,
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
        const int row = y * w;
        const int prow = (y + dy) * w;

        for (int x = x0; x < x1; x += step) {
            const int d = (int) cur[row + x] - (int) prev[prow + x + dx];
            sad += (d < 0) ? -d : d;
            n++;
        }
    }

    if (samples)
        *samples = n;

    return sad;
}

static int dm_block_sad_clamped(const uint8_t * restrict cur,
                                const uint8_t * restrict prev,
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
        const int py = (y + dy < 0) ? 0 : ((y + dy >= h) ? h - 1 : y + dy);
        const int row = y * w;
        const int prow = py * w;

        for (int x = x0; x < x1; x += step) {
            const int px = (x + dx < 0) ? 0 : ((x + dx >= w) ? w - 1 : x + dx);
            const int d = (int) cur[row + x] - (int) prev[prow + px];
            sad += (d < 0) ? -d : d;
            n++;
        }
    }

    if (samples)
        *samples = n;

    return sad;
}

static inline int dm_block_sad(const uint8_t * restrict cur,
                               const uint8_t * restrict prev,
                               int w,
                               int h,
                               int x0,
                               int y0,
                               int x1,
                               int y1,
                               int dx,
                               int dy,
                               int step,
                               int *samples,
                               int interior)
{
    return interior
        ? dm_block_sad_inner(cur, prev, w, x0, y0, x1, y1, dx, dy, step, samples)
        : dm_block_sad_clamped(cur, prev, w, h, x0, y0, x1, y1, dx, dy, step, samples);
}

static void dm_clear_fields(datamosh_t *d)
{
    veejay_memset(d->field_x, 0, (size_t) d->max_blocks * sizeof(int16_t));
    veejay_memset(d->field_y, 0, (size_t) d->max_blocks * sizeof(int16_t));
    veejay_memset(d->energy, 0, (size_t) d->max_blocks);
}

static void dm_fill_history(datamosh_t *d, const uint8_t *src_y, const uint8_t *src_u, const uint8_t *src_v)
{
    const size_t len = (size_t) d->len;

    for (int i = 0; i < DM_HISTORY_MAX; i++) {
        veejay_memcpy(d->hist[0] + (size_t) i * len, src_y, len);
        veejay_memcpy(d->hist[1] + (size_t) i * len, src_u, len);
        veejay_memcpy(d->hist[2] + (size_t) i * len, src_v, len);
    }

    for (int p = 0; p < 2; p++) {
        veejay_memcpy(d->canvas[p][0], src_y, len);
        veejay_memcpy(d->canvas[p][1], src_u, len);
        veejay_memcpy(d->canvas[p][2], src_v, len);
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

    veejay_memcpy(*cur_y, src_y, len);
    veejay_memcpy(*cur_u, src_u, len);
    veejay_memcpy(*cur_v, src_v, len);
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

    ve->limits[0][P_FLOW] = 0;
    ve->limits[1][P_FLOW] = 7;
    ve->defaults[P_FLOW] = 2;

    ve->limits[0][P_MOSH] = 0;
    ve->limits[1][P_MOSH] = 100;
    ve->defaults[P_MOSH] = 82;

    ve->limits[0][P_MOTION] = 0;
    ve->limits[1][P_MOTION] = 100;
    ve->defaults[P_MOTION] = 64;

    ve->limits[0][P_BLOCK] = DM_MIN_BLOCK;
    ve->limits[1][P_BLOCK] = 64;
    ve->defaults[P_BLOCK] = 12;

    ve->limits[0][P_HISTORY] = 1;
    ve->limits[1][P_HISTORY] = DM_HISTORY_MAX;
    ve->defaults[P_HISTORY] = 16;

    ve->limits[0][P_SLIP] = 0;
    ve->limits[1][P_SLIP] = 100;
    ve->defaults[P_SLIP] = 62;

    ve->limits[0][P_PERSIST] = 0;
    ve->limits[1][P_PERSIST] = 100;
    ve->defaults[P_PERSIST] = 88;

    ve->limits[0][P_FLOW_AMT] = 0;
    ve->limits[1][P_FLOW_AMT] = 100;
    ve->defaults[P_FLOW_AMT] = 30;

    ve->limits[0][P_TEAR] = 0;
    ve->limits[1][P_TEAR] = 100;
    ve->defaults[P_TEAR] = 22;

    ve->limits[0][P_SRC_OPAC] = 0;
    ve->limits[1][P_SRC_OPAC] = 100;
    ve->defaults[P_SRC_OPAC] = 0;

    ve->limits[0][P_RESET] = 0;
    ve->limits[1][P_RESET] = 1;
    ve->defaults[P_RESET] = 0;

    ve->sub_format = 1;
    ve->description = "Datamosh";
    ve->param_description = vje_build_param_list(ve->num_params,
        "Flow Mode",
        "Mosh Amount",
        "Motion Reactivity",
        "Block Size",
        "Time Depth",
        "Time Slip",
        "Persistence",
        "Flow Strength",
        "Tear/Jitter",
        "Source Opacity",
        "Reset Memory"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                 VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000, /* Flow Mode */
        VJ_BEAT_WARP,         VJ_BEAT_F_CONTINUOUS,                                                     28,                 96,                 12, 48,  900,  2400, 0,    80,    /* Mosh Amount */
        VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS,                                                     16,                 90,                 12, 46,  900,  2400, 0,    70,    /* Motion Reactivity */
        VJ_BEAT_GRID_SIZE,    VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE,    6,                  32,                 6,  20,  2200, 5200, 1800, 25,    /* Block Size */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                               4,                  22,                 6,  24,  1800, 4200, 900,  35,    /* Time Depth */
        VJ_BEAT_DRIFT,        VJ_BEAT_F_CONTINUOUS,                                                     12,                 88,                 10, 38,  1000, 2800, 0,    60,    /* Time Slip */
        VJ_BEAT_INERTIA,      VJ_BEAT_F_PHRASE_ONLY,                                                    55,                 98,                 8,  30,  1800, 4200, 900,  45,    /* Persistence */
        VJ_BEAT_FLOW,         VJ_BEAT_F_CONTINUOUS,                                                     0,                  85,                 12, 46,  900,  2400, 0,    75,    /* Flow Strength */
        VJ_BEAT_WARP,         VJ_BEAT_F_CLIMAX_ONLY,                                                    0,                  70,                 4,  28,  1800, 4200, 600,  25,    /* Tear/Jitter */
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_REJECT,                                                     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000, /* Source Opacity */
        VJ_BEAT_TRIGGER,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000  /* Reset Memory */
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
    d->last_flow = -1;
    d->last_reset = 0;

    return d;
}


#define DM_RENDER_PIXEL(PURE_OUT) do { \
    const int cy__ = cur_y[idx]; \
    const int cu__ = cur_u[idx]; \
    const int cv__ = cur_v[idx]; \
    const int my__ = (cy__ * m_cur_w + (int) hy[idx] * m_hist_w + (int) old_y * m_old_w + 32768) >> 16; \
    const int mu__ = (cu__ * m_cur_w + (int) hu[idx] * m_hist_w + (int) read_u[old_idx] * m_old_w + 32768) >> 16; \
    const int mv__ = (cv__ * m_cur_w + (int) hv[idx] * m_hist_w + (int) read_v[old_idx] * m_old_w + 32768) >> 16; \
    write_y[idx] = (uint8_t) my__; \
    write_u[idx] = (uint8_t) mu__; \
    write_v[idx] = (uint8_t) mv__; \
    if (PURE_OUT) { \
        frame_y[idx] = (uint8_t) my__; \
        frame_u[idx] = (uint8_t) mu__; \
        frame_v[idx] = (uint8_t) mv__; \
    } else { \
        frame_y[idx] = (uint8_t) ((cy__ * om_inv + my__ * out_mix_q + 128) >> 8); \
        frame_u[idx] = (uint8_t) ((cu__ * om_inv + mu__ * out_mix_q + 128) >> 8); \
        frame_v[idx] = (uint8_t) ((cv__ * om_inv + mv__ * out_mix_q + 128) >> 8); \
    } \
} while (0)

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

    uint8_t * restrict frame_y = frame->data[0];
    uint8_t * restrict frame_u = frame->data[1];
    uint8_t * restrict frame_v = frame->data[2];
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
    const int block = dm_clampi(args[P_BLOCK], DM_MIN_BLOCK, 64);
    const int motion = dm_clampi(args[P_MOTION], 0, 100);
    const int persist = dm_clampi(args[P_PERSIST], 0, 100);
    const int tear = dm_clampi(args[P_TEAR], 0, 100);
    const int slip = dm_clampi(args[P_SLIP], 0, 100);
    const int flow_mode = dm_clampi(args[P_FLOW], 0, 7);
    const int flow_strength = dm_clampi(args[P_FLOW_AMT], 0, 100);
    const int source_opacity = dm_clampi(args[P_SRC_OPAC], 0, 100);

    if (block != d->last_block || flow_mode != d->last_flow) {
        dm_clear_fields(d);
        d->last_block = block;
        d->last_flow = flow_mode;
    }

    uint8_t *cur_y;
    uint8_t *cur_u;
    uint8_t *cur_v;
    dm_push_history(d, frame_y, frame_u, frame_v, &cur_y, &cur_u, &cur_v);

    const int prev_slot = (d->hist_head > 0) ? (d->hist_head - 1) : (DM_HISTORY_MAX - 1);
    const uint8_t * restrict prev_y = d->hist[0] + (size_t) prev_slot * llen;

    if (strength <= 0) {
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
    const int search_shift = (motion > 0) ? (1 + (motion * 11) / 100) : 0;
    const int half_shift = (search_shift + 1) >> 1;
    const int sample_step = (block >= 32) ? 6 : ((block >= 24) ? 5 : ((block >= 16) ? 4 : ((block >= 9) ? 3 : 2)));
    const int amp_q = DM_FP_ONE + (strength * DM_FP_ONE * 4) / 100;
    const int field_keep = 174 + (persist * 81) / 100;
    const int energy_keep = 160 + (persist * 95) / 100;
    const int flow_q = (flow_strength * DM_FP_ONE * 9) / 100;
    const int flow_active = (flow_mode != 0 && flow_q > 0);
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
    const int source_opacity_q = dm_percent_to_q8(source_opacity);
    const int flow_norm_q = (max_dim > 0) ? ((flow_q * 2 * 256 + (max_dim >> 1)) / max_dim) : 0;

    uint8_t back_lut[256];
    uint16_t hist_mix_q_lut[256];
    uint16_t out_mix_q_lut[256];
    int16_t jitter_q_lut[256];
    uint8_t follow_lut[256];

    const int hist_span = history_depth - 1;
    const int flow_follow_lut = flow_active ? (flow_strength >> 2) : 0;
    for (int e = 0; e < 256; e++) {
        back_lut[e] = (uint8_t) ((slip * e * hist_span + 12750) / 25500);
        hist_mix_q_lut[e] = (uint16_t) ((slip * e * 256 + 12750) / 25500);

        const int ey = 76 + ((e * 24) / 255);
        const int local_strength = dm_clampi((strength * ey + 50) / 100, 0, 100);
        const int old_q = dm_percent_to_q8(local_strength);
        out_mix_q_lut[e] = (uint16_t) (256 - (((256 - old_q) * source_opacity_q + 128) >> 8));

        jitter_q_lut[e] = (int16_t) ((tear * (48 + e) * DM_FP_ONE * 8) / (100 * 303));
        follow_lut[e] = (uint8_t) dm_clampi(28 + ((e * (motion + 48)) / 380) + flow_follow_lut, 20, 180);
    }

    const int read_ping = d->canvas_ping;
    const int write_ping = read_ping ^ 1;

    const uint8_t * restrict read_y = d->canvas[read_ping][0];
    const uint8_t * restrict read_u = d->canvas[read_ping][1];
    const uint8_t * restrict read_v = d->canvas[read_ping][2];
    uint8_t * restrict write_y = d->canvas[write_ping][0];
    uint8_t * restrict write_u = d->canvas[write_ping][1];
    uint8_t * restrict write_v = d->canvas[write_ping][2];
    const int max_xfp_inner = ((w - 2) << DM_FP_SHIFT);
    const int max_yfp_inner = ((h - 2) << DM_FP_SHIFT);

#pragma omp parallel for schedule(static) num_threads(d->n_threads)
    for (int bi = 0; bi < bw * bh; bi++) {
        const int by = bi / bw;
        const int bx = bi - by * bw;
        const int x0 = bx * block;
        const int y0 = by * block;
        const int x1 = (x0 + block < w) ? x0 + block : w;
        const int y1 = (y0 + block < h) ? y0 + block : h;

        int act = 0;
        int best_dx = 0;
        int best_dy = 0;

        if (motion > 0) {
            int samples = 0;
            const int sad_interior = (x0 - search_shift >= 0) &&
                                     (y0 - search_shift >= 0) &&
                                     (x1 + search_shift < w) &&
                                     (y1 + search_shift < h);
            const int zero_sad = dm_block_sad(cur_y, prev_y, w, h, x0, y0, x1, y1,
                                              0, 0, sample_step, &samples, sad_interior);
            const int mean_zero = (samples > 0) ? zero_sad / samples : 0;
            const int threshold = 4 + (100 - motion) / 8;

            if (mean_zero > threshold) {
                int best_sad = zero_sad;

                for (int c = 1; c < DM_CANDIDATES_HALF; c++) {
                    const int dx = cand_x[c];
                    const int dy = cand_y[c];
                    const int sad = dm_block_sad(cur_y, prev_y, w, h, x0, y0, x1, y1,
                                                 dx, dy, sample_step, NULL, sad_interior);
                    if (sad < best_sad) {
                        best_sad = sad;
                        best_dx = dx;
                        best_dy = dy;
                    }
                }

                const int half_improve = zero_sad - best_sad;
                const int try_full_search = (motion > 48) &&
                                            ((half_improve > samples * 2) ||
                                             (mean_zero > threshold + 16));
                if (try_full_search) {
                    for (int c = DM_CANDIDATES_HALF; c < DM_CANDIDATES; c++) {
                        const int dx = cand_x[c];
                        const int dy = cand_y[c];
                        const int sad = dm_block_sad(cur_y, prev_y, w, h, x0, y0, x1, y1,
                                                     dx, dy, sample_step, NULL, sad_interior);
                        if (sad < best_sad) {
                            best_sad = sad;
                            best_dx = dx;
                            best_dy = dy;
                        }
                    }
                }

                const int improve = (zero_sad - best_sad) / samples;
                act = (mean_zero - threshold) * motion * 3 / 100;
                act += (improve > 0) ? (improve * (motion + 35) / 72) : 0;
                act = dm_clampi(act, 0, 255);
            }
        }

        int target_x = best_dx * amp_q;
        int target_y = best_dy * amp_q;

        if (flow_active) {
            const int cx = x0 + ((x1 - x0) >> 1);
            const int cy = y0 + ((y1 - y0) >> 1);
            const int rx = cx - half_w;
            const int ry = cy - half_h;

            switch (flow_mode) {
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

        int drive = act;
        drive = (flow_active && drive < flow_strength) ? flow_strength : drive;
        if (tear > 0) {
            const int tear_drive = (tear * (strength + 40) + 70) / 140;
            drive = (drive < tear_drive) ? tear_drive : drive;
        }
        drive = dm_clampi(drive, 0, 255);

        if (tear > 0 && drive > 0) {
            const uint32_t hv = dm_hash_u32((uint32_t) bi * 1103515245u + (uint32_t) d->frame * 12345u);
            const int jitter_q = jitter_q_lut[drive];
            target_x += dm_div_pow2_trunc_i32(((int) (hv & 255u) - 128) * jitter_q, 7);
            target_y += dm_div_pow2_trunc_i32(((int) ((hv >> 8) & 255u) - 128) * jitter_q, 7);
        }

        const int old_x = d->field_x[bi];
        const int old_y = d->field_y[bi];
        int new_x;
        int new_y;

        if (drive > 0) {
            const int follow = follow_lut[drive];
            new_x = (old_x * (255 - follow) + target_x * follow) / 255;
            new_y = (old_y * (255 - follow) + target_y * follow) / 255;
        } else {
            new_x = (old_x * field_keep) / 255;
            new_y = (old_y * field_keep) / 255;
        }

        const int fx = dm_clampi(new_x, -32768, 32767);
        const int fy = dm_clampi(new_y, -32768, 32767);
        d->field_x[bi] = (int16_t) fx;
        d->field_y[bi] = (int16_t) fy;

        const int old_e = d->energy[bi];
        const int decayed_e = (old_e * energy_keep) / 255;
        const int e = dm_clampi((drive > decayed_e) ? drive : decayed_e, 0, 255);
        d->energy[bi] = (uint8_t) e;

        const int back = back_lut[e];
        int hslot = d->hist_head - back;
        hslot += (hslot < 0) ? DM_HISTORY_MAX : 0;

        const uint8_t * restrict hy = d->hist[0] + (size_t) hslot * llen;
        const uint8_t * restrict hu = d->hist[1] + (size_t) hslot * llen;
        const uint8_t * restrict hv = d->hist[2] + (size_t) hslot * llen;

        const int hist_mix_q = hist_mix_q_lut[e];
        const int out_mix_q = out_mix_q_lut[e];
        const int direct_sample = (fx == 0 && fy == 0);
        const int interior_sample = !direct_sample &&
                                    (((x0 << DM_FP_SHIFT) + fx) >= 0) &&
                                    ((((x1 - 1) << DM_FP_SHIFT) + fx) <= max_xfp_inner) &&
                                    (((y0 << DM_FP_SHIFT) + fy) >= 0) &&
                                    ((((y1 - 1) << DM_FP_SHIFT) + fy) <= max_yfp_inner);
        const int pure_mosh_out = (out_mix_q >= 256);
        const int inv_hist_q = 256 - hist_mix_q;
        const int inv_persist_q = 256 - persist_q;
        const int m_cur_w = inv_hist_q * inv_persist_q;
        const int m_hist_w = hist_mix_q * inv_persist_q;
        const int m_old_w = persist_q << 8;
        const int om_inv = 256 - out_mix_q;

        if (direct_sample) {
            if (pure_mosh_out) {
                for (int y = y0; y < y1; y++) {
                    const int row = y * w;
                    for (int x = x0; x < x1; x++) {
                        const int idx = row + x;
                        const int old_idx = idx;
                        const uint8_t old_y = read_y[idx];
                        DM_RENDER_PIXEL(1);
                    }
                }
            } else {
                for (int y = y0; y < y1; y++) {
                    const int row = y * w;
                    for (int x = x0; x < x1; x++) {
                        const int idx = row + x;
                        const int old_idx = idx;
                        const uint8_t old_y = read_y[idx];
                        DM_RENDER_PIXEL(0);
                    }
                }
            }
        } else if (interior_sample) {
            const int sx0_fp = (x0 << DM_FP_SHIFT) + fx;
            const int sy0_fp = (y0 << DM_FP_SHIFT) + fy;
            const int old_x0 = sx0_fp >> DM_FP_SHIFT;
            const int old_y0 = sy0_fp >> DM_FP_SHIFT;
            const int frac_x = sx0_fp & DM_FP_MASK;
            const int frac_y = sy0_fp & DM_FP_MASK;
            const int ifrac_x = DM_FP_ONE - frac_x;
            const int ifrac_y = DM_FP_ONE - frac_y;
            const int round = 1 << ((DM_FP_SHIFT * 2) - 1);

            if (pure_mosh_out) {
                for (int y = y0; y < y1; y++) {
                    const int row = y * w;
                    int old_idx = (old_y0 + (y - y0)) * w + old_x0;
                    for (int x = x0; x < x1; x++, old_idx++) {
                        const int idx = row + x;
                        const int top = read_y[old_idx] * ifrac_x + read_y[old_idx + 1] * frac_x;
                        const int bot = read_y[old_idx + w] * ifrac_x + read_y[old_idx + w + 1] * frac_x;
                        const uint8_t old_y = (uint8_t) ((top * ifrac_y + bot * frac_y + round) >> (DM_FP_SHIFT * 2));
                        DM_RENDER_PIXEL(1);
                    }
                }
            } else {
                for (int y = y0; y < y1; y++) {
                    const int row = y * w;
                    int old_idx = (old_y0 + (y - y0)) * w + old_x0;
                    for (int x = x0; x < x1; x++, old_idx++) {
                        const int idx = row + x;
                        const int top = read_y[old_idx] * ifrac_x + read_y[old_idx + 1] * frac_x;
                        const int bot = read_y[old_idx + w] * ifrac_x + read_y[old_idx + w + 1] * frac_x;
                        const uint8_t old_y = (uint8_t) ((top * ifrac_y + bot * frac_y + round) >> (DM_FP_SHIFT * 2));
                        DM_RENDER_PIXEL(0);
                    }
                }
            }
        } else {
            if (pure_mosh_out) {
                for (int y = y0; y < y1; y++) {
                    const int row = y * w;
                    const int sy_fp = (y << DM_FP_SHIFT) + fy;
                    for (int x = x0; x < x1; x++) {
                        const int idx = row + x;
                        const int sx_fp = (x << DM_FP_SHIFT) + fx;
                        int old_idx;
                        const uint8_t old_y = dm_sample_y_bilinear_clamped_idx(read_y, w, h, sx_fp, sy_fp, &old_idx);
                        DM_RENDER_PIXEL(1);
                    }
                }
            } else {
                for (int y = y0; y < y1; y++) {
                    const int row = y * w;
                    const int sy_fp = (y << DM_FP_SHIFT) + fy;
                    for (int x = x0; x < x1; x++) {
                        const int idx = row + x;
                        const int sx_fp = (x << DM_FP_SHIFT) + fx;
                        int old_idx;
                        const uint8_t old_y = dm_sample_y_bilinear_clamped_idx(read_y, w, h, sx_fp, sy_fp, &old_idx);
                        DM_RENDER_PIXEL(0);
                    }
                }
            }
        }
    }

    d->canvas_ping = write_ping;
    d->frame++;
}

#undef DM_RENDER_PIXEL
