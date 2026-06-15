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
#include "fragmenttv.h"

#define MAX_TILES       4096
#define MAX_TILE_SIZE   128

#define FRAGMENTTV_PARAMS 10

#define P_TILE_SIZE     0
#define P_SIZE_RANDOM   1
#define P_SOURCE_OFFSET 2
#define P_TILE_DRIFT    3
#define P_COVERAGE      4
#define P_REFRESH       5
#define P_BLEND_MODE    6
#define P_BORDER        7
#define P_BLACK_BG      8
#define P_EDGE_STYLE    9

enum {
    BLEND_NORMAL = 0,
    BLEND_ADD,
    BLEND_DIFF,
    BLEND_MULTIPLY,
    BLEND_LUMA
};

typedef struct
{
    int dx, dy;
    int sx, sy;
    int size;
    uint8_t alphaX[MAX_TILE_SIZE];
    uint8_t alphaY[MAX_TILE_SIZE];
} frag_tile;

typedef struct
{
    int w;
    int h;
    int len;
    uint8_t *tmp[3];
    frag_tile tiles[MAX_TILES];
    int tile_count;
    int n_threads;
    int frame_count;
    int first;
    int prev[FRAGMENTTV_PARAMS];
    int drift_accum;
} fragmenttv_t;


static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t clamp8(int v)
{
    return (v < 0) ? 0 : (v > 255 ? 255 : (uint8_t)v);
}

vj_effect *fragmenttv_init(int w, int h)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = FRAGMENTTV_PARAMS;

    ve->defaults = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

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

    ve->defaults[P_TILE_SIZE]     = 32;
    ve->defaults[P_SIZE_RANDOM]   = 8;
    ve->defaults[P_SOURCE_OFFSET] = 16;
    ve->defaults[P_TILE_DRIFT]    = 4;
    ve->defaults[P_COVERAGE]      = 80;
    ve->defaults[P_REFRESH]       = 0;
    ve->defaults[P_BLEND_MODE]    = 0;
    ve->defaults[P_BORDER]        = 100;
    ve->defaults[P_BLACK_BG]      = 0;
    ve->defaults[P_EDGE_STYLE]    = 0;

    ve->limits[0][P_TILE_SIZE]     = 8;   ve->limits[1][P_TILE_SIZE]     = 128;
    ve->limits[0][P_SIZE_RANDOM]   = 0;   ve->limits[1][P_SIZE_RANDOM]   = 64;
    ve->limits[0][P_SOURCE_OFFSET] = 0;   ve->limits[1][P_SOURCE_OFFSET] = 256;
    ve->limits[0][P_TILE_DRIFT]    = 0;   ve->limits[1][P_TILE_DRIFT]    = 64;
    ve->limits[0][P_COVERAGE]      = 0;   ve->limits[1][P_COVERAGE]      = 100;
    ve->limits[0][P_REFRESH]       = 0;   ve->limits[1][P_REFRESH]       = 500;
    ve->limits[0][P_BLEND_MODE]    = 0;   ve->limits[1][P_BLEND_MODE]    = 4;
    ve->limits[0][P_BORDER]        = 0;   ve->limits[1][P_BORDER]        = 100;
    ve->limits[0][P_BLACK_BG]      = 0;   ve->limits[1][P_BLACK_BG]      = 1;
    ve->limits[0][P_EDGE_STYLE]    = 0;   ve->limits[1][P_EDGE_STYLE]    = 2;

    ve->description = "Fragment TV";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Tile Size",
        "Size Randomness",
        "Source Offset",
        "Tile Drift",
        "Coverage",
        "Refresh Frames",
        "Blend Mode",
        "Border Strength",
        "Black Background",
        "Edge Style"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_BLEND_MODE], P_BLEND_MODE, "Normal", "Add", "Difference", "Multiply", "Luma");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_BLACK_BG], P_BLACK_BG, "Source Background", "Black Background");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_EDGE_STYLE], P_EDGE_STYLE, "None", "Soft", "Reset Borders");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GRID_SIZE,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,     0,    0,    -1000,
        VJ_BEAT_DENSITY,    VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 4,   34,  3,  10, 5200, 12800, 4200, 16,
        VJ_BEAT_WARP,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 4,   128, 3,  10, 4800, 11800, 3800, 20,
        VJ_BEAT_DRIFT,      VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 0,   28,  3,  10, 5200, 12800, 4200, 18,
        VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, 52, 100, 3, 10, 4800, 11800, 3800, 22,
        VJ_BEAT_SPEED,      VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS | VJ_BEAT_F_REBUILDS_STATE, 10, 150, 3, 10, 5600, 14000, 4800, 14,
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,     0,    0,    -1000,
        VJ_BEAT_GLOW,       VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,     0,    0,    -1000,
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,     0,    0,    -1000,
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,     0,    0,    -1000
    );

    return ve;
}


void *fragmenttv_malloc(int w, int h)
{
    fragmenttv_t *m = (fragmenttv_t*) vj_calloc(sizeof(fragmenttv_t));
    if(!m)
        return NULL;

    const size_t len = (size_t)w * (size_t)h;

    m->w = w;
    m->h = h;
    m->len = (int)len;

    m->tmp[0] = (uint8_t*) vj_malloc(len * 3);
    if(!m->tmp[0]) {
        free(m);
        return NULL;
    }

    m->tmp[1] = m->tmp[0] + len;
    m->tmp[2] = m->tmp[1] + len;

    m->n_threads = vje_advise_num_threads((int)len);
    m->first = 1;
    m->drift_accum = 0;

    return m;
}

void fragmenttv_free(void *ptr)
{
    fragmenttv_t *m = (fragmenttv_t*) ptr;

    free(m->tmp[0]);
    free(m);
}

static void make_alpha(uint8_t *dst, int size)
{
    for(int i = 0; i < size; i++) {
        if(i < 3)
            dst[i] = 85 + i * 56;
        else if(i >= size - 3)
            dst[i] = 85 + (size - 1 - i) * 56;
        else
            dst[i] = 255;
    }
}


static void generate_tiles(fragmenttv_t *m,
                           int w, int h,
                           int tile,
                           int vary,
                           int scatter,
                           int drift,
                           int cover)
{
    m->tile_count = 0;
    m->drift_accum = (m->drift_accum * 7 + drift) >> 3;

    for(int gy = 0; gy < h; gy += tile) {
        for(int gx = 0; gx < w; gx += tile) {
            if(m->tile_count >= MAX_TILES)
                return;

            uint32_t seed = (uint32_t)(gx * 73856093u ^ gy * 19349663u ^ m->frame_count * 83492791u);
            int r = (seed >> 16) & 0x7fff;

            int local_cover = cover;
            int cx = gx / tile;
            int cy = gy / tile;
            int cluster = (cx * 17 + cy * 13 + m->frame_count) & 31;

            if(cluster < 8)
                local_cover = clampi(cover + 20, 0, 100);

            if((r % 100) > local_cover)
                continue;

            frag_tile *t = &m->tiles[m->tile_count++];

            int size = tile + (r % (vary + 1));
            size = clampi(size, 4, MAX_TILE_SIZE);

            int dx = gx + ((int)(seed >> 8) % (m->drift_accum * 2 + 1)) - m->drift_accum;
            int dy = gy + ((int)(seed >> 12) % (m->drift_accum * 2 + 1)) - m->drift_accum;

            dx = clampi(dx, 0, w - 1);
            dy = clampi(dy, 0, h - 1);

            if(dx + size > w)
                size = w - dx;
            if(dy + size > h)
                size = h - dy;

            t->dx = dx;
            t->dy = dy;

            t->sx = clampi(gx + ((int)(seed >> 4) % (scatter * 2 + 1)) - scatter, 0, w - size);
            t->sy = clampi(gy + ((int)(seed >> 6) % (scatter * 2 + 1)) - scatter, 0, h - size);
            t->size = size;

            make_alpha(t->alphaX, size);
            make_alpha(t->alphaY, size);
        }
    }
}

static inline uint8_t blend_fast(uint8_t d, uint8_t s, int a)
{
    int x = (int)d * (255 - a) + (int)s * a;
    return (uint8_t)(((x + 1) + (x >> 8)) >> 8);
}

static inline uint8_t blend_diff(uint8_t d, uint8_t s)
{
    int v = (int)d - (int)s;
    if(v < 0)
        v = -v;
    return (uint8_t)v;
}

static inline uint8_t blend_mul(uint8_t d, uint8_t s)
{
    int v = (d * s) + 128;
    return (uint8_t)(v >> 8);
}

static inline void do_blend(uint8_t *Y, uint8_t *U, uint8_t *V,
                            int i,
                            uint8_t sy, uint8_t su, uint8_t sv,
                            int a,
                            int mode)
{
    switch(mode)
    {
        default:
        case BLEND_NORMAL:
            Y[i] = blend_fast(Y[i], sy, a);
            U[i] = blend_fast(U[i], su, a);
            V[i] = blend_fast(V[i], sv, a);
            break;

        case BLEND_ADD:
        {
            int y = Y[i] + ((sy * a) >> 8);
            Y[i] = (uint8_t)clampi(y, 0, 255);
            U[i] = blend_fast(U[i], su, a);
            V[i] = blend_fast(V[i], sv, a);
            break;
        }

        case BLEND_DIFF:
            Y[i] = blend_diff(Y[i], sy);
            break;

        case BLEND_MULTIPLY:
            Y[i] = blend_mul(Y[i], sy);
            break;

        case BLEND_LUMA:
            Y[i] = blend_fast(Y[i], sy, a);
            break;
    }
}

static void draw_tiles_serial(fragmenttv_t *m,
                              uint8_t *Y, uint8_t *U, uint8_t *V,
                              uint8_t *sY, uint8_t *sU, uint8_t *sV,
                              int w,
                              int mode)
{
    for(int t = 0; t < m->tile_count; t++) {
        frag_tile *q = &m->tiles[t];

        const int base_x = q->dx;
        const int base_y = q->dy;
        const int size = q->size;

        for(int y = 0; y < size; y++) {
            const int di = (base_y + y) * w + base_x;
            const int si = (q->sy + y) * w + q->sx;
            const uint8_t ay = q->alphaY[y];

            for(int x = 0; x < size; x++) {
                const int idx = di + x;
                const int sidx = si + x;
                const int a = (q->alphaX[x] * ay) >> 8;

                do_blend(Y, U, V, idx, sY[sidx], sU[sidx], sV[sidx], a, mode);
            }
        }
    }
}

static void draw_tiles_parallel_no_overlap(fragmenttv_t *m,
                                           uint8_t *Y, uint8_t *U, uint8_t *V,
                                           uint8_t *sY, uint8_t *sU, uint8_t *sV,
                                           int w,
                                           int mode)
{
#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for(int t = 0; t < m->tile_count; t++) {
        frag_tile *q = &m->tiles[t];

        const int base_x = q->dx;
        const int base_y = q->dy;
        const int size = q->size;

        for(int y = 0; y < size; y++) {
            const int di = (base_y + y) * w + base_x;
            const int si = (q->sy + y) * w + q->sx;
            const uint8_t ay = q->alphaY[y];

            for(int x = 0; x < size; x++) {
                const int idx = di + x;
                const int sidx = si + x;
                const int a = (q->alphaX[x] * ay) >> 8;

                do_blend(Y, U, V, idx, sY[sidx], sU[sidx], sV[sidx], a, mode);
            }
        }
    }
}

static void draw_tiles(fragmenttv_t *m,
                       uint8_t *Y, uint8_t *U, uint8_t *V,
                       uint8_t *sY, uint8_t *sU, uint8_t *sV,
                       int w,
                       int mode,
                       int overlap_risk)
{
    if(overlap_risk)
        draw_tiles_serial(m, Y, U, V, sY, sU, sV, w, mode);
    else
        draw_tiles_parallel_no_overlap(m, Y, U, V, sY, sU, sV, w, mode);
}

static void draw_borders_serial(fragmenttv_t *m,
                                uint8_t *Y,
                                int w,
                                int strength)
{
    if(strength <= 0)
        return;

    for(int t = 0; t < m->tile_count; t++) {
        frag_tile *q = &m->tiles[t];

        const int x0 = q->dx;
        const int y0 = q->dy;
        const int x1 = q->dx + q->size - 1;
        const int y1 = q->dy + q->size - 1;
        const uint8_t s = (uint8_t)strength;

        for(int x = x0; x <= x1; x++) {
            const int i_top = y0 * w + x;
            const int i_bot = y1 * w + x;

            Y[i_top] = clamp8(Y[i_top] + s);
            Y[i_bot] = clamp8(Y[i_bot] + s);
        }

        for(int y = y0; y <= y1; y++) {
            const int row = y * w;
            const int i_l = row + x0;
            const int i_r = row + x1;

            Y[i_l] = clamp8(Y[i_l] + s);
            Y[i_r] = clamp8(Y[i_r] + s);
        }
    }
}

static void draw_borders_parallel_no_overlap(fragmenttv_t *m,
                                             uint8_t *Y,
                                             int w,
                                             int strength)
{
    if(strength <= 0)
        return;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for(int t = 0; t < m->tile_count; t++) {
        frag_tile *q = &m->tiles[t];

        const int x0 = q->dx;
        const int y0 = q->dy;
        const int x1 = q->dx + q->size - 1;
        const int y1 = q->dy + q->size - 1;
        const uint8_t s = (uint8_t)strength;

        for(int x = x0; x <= x1; x++) {
            const int i_top = y0 * w + x;
            const int i_bot = y1 * w + x;

            Y[i_top] = clamp8(Y[i_top] + s);
            Y[i_bot] = clamp8(Y[i_bot] + s);
        }

        for(int y = y0; y <= y1; y++) {
            const int row = y * w;
            const int i_l = row + x0;
            const int i_r = row + x1;

            Y[i_l] = clamp8(Y[i_l] + s);
            Y[i_r] = clamp8(Y[i_r] + s);
        }
    }
}

static void draw_borders(fragmenttv_t *m,
                         uint8_t *Y,
                         int w,
                         int strength,
                         int overlap_risk)
{
    if(overlap_risk)
        draw_borders_serial(m, Y, w, strength);
    else
        draw_borders_parallel_no_overlap(m, Y, w, strength);
}

void fragmenttv_apply(void *ptr, VJFrame *frame, int *args)
{
    fragmenttv_t *m = (fragmenttv_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    const int tile = args[P_TILE_SIZE];
    const int vary = args[P_SIZE_RANDOM];
    const int scatter = args[P_SOURCE_OFFSET];
    const int drift = args[P_TILE_DRIFT];
    const int cover = args[P_COVERAGE];
    const int refresh = args[P_REFRESH];
    const int mode = args[P_BLEND_MODE];
    const int border = args[P_BORDER];
    const int blackbg = args[P_BLACK_BG] ? 1 : 0;
    const int edge_mode = args[P_EDGE_STYLE];

    int stable_args[FRAGMENTTV_PARAMS] = { 0 };
    stable_args[P_TILE_SIZE] = tile;
    stable_args[P_SIZE_RANDOM] = vary;
    stable_args[P_SOURCE_OFFSET] = scatter;
    stable_args[P_TILE_DRIFT] = drift;
    stable_args[P_COVERAGE] = cover;
    stable_args[P_REFRESH] = refresh;
    stable_args[P_BLEND_MODE] = mode;
    stable_args[P_BORDER] = border;
    stable_args[P_BLACK_BG] = blackbg;
    stable_args[P_EDGE_STYLE] = edge_mode;

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    veejay_memcpy(m->tmp[0], Y, len);
    veejay_memcpy(m->tmp[1], U, len);
    veejay_memcpy(m->tmp[2], V, len);

    if(blackbg) {
        veejay_memset(Y, 0, len);
        veejay_memset(U, 128, len);
        veejay_memset(V, 128, len);
    }

    int changed =
        m->first ||
        (refresh > 0 && (m->frame_count % refresh) == 0) ||
        memcmp(m->prev, stable_args, sizeof(int) * FRAGMENTTV_PARAMS);

    if(changed) {
        generate_tiles(m, w, h, tile, vary, scatter, drift, cover);
        veejay_memcpy(m->prev, stable_args, sizeof(int) * FRAGMENTTV_PARAMS);
    }

    const int overlap_risk = (vary > 0 || drift > 0);

    draw_tiles(m, Y, U, V, m->tmp[0], m->tmp[1], m->tmp[2], w, mode, overlap_risk);

    if(edge_mode == 2) {
        draw_borders(m, Y, w, border, overlap_risk);
        m->frame_count = 0;
        m->drift_accum = 0;
        m->first = 1;
    }

    m->frame_count++;
    m->first = 0;
}
