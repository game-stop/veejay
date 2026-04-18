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

#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

#include "common.h"
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>

#define MAX_TILES       4096
#define MAX_TILE_SIZE   128
#define RAND_SIZE       1024

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
    uint8_t *tmp[3];
    frag_tile tiles[MAX_TILES];
    int tile_count;

    uint32_t rnd[RAND_SIZE];
    int rnd_pos;

    int n_threads;
    int frame_count;
    int first;
    int prev[10];

    int seed_frame;
    int stable_mode;
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

static void init_rand(fragmenttv_t *m, uint32_t seed)
{
    uint32_t s = seed;
    for(int i=0;i<RAND_SIZE;i++){
        s = s * 1664525u + 1013904223u;
        m->rnd[i] = s;
    }
    m->rnd_pos = 0;
}

static inline uint32_t rnd_next(fragmenttv_t *m)
{
    uint32_t v = m->rnd[m->rnd_pos++];
    if(m->rnd_pos >= RAND_SIZE) m->rnd_pos = 0;
    return v;
}

static inline int rnd_range(fragmenttv_t *m, int lo, int hi)
{
    return lo + (rnd_next(m) % (hi - lo + 1));
}

vj_effect *fragmenttv_init(int w, int h)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));

    ve->num_params = 10;

    ve->defaults = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 32;
    ve->defaults[1] = 8;
    ve->defaults[2] = 16;
    ve->defaults[3] = 4;
    ve->defaults[4] = 80;
    ve->defaults[5] = 0;
    ve->defaults[6] = 0;
    ve->defaults[7] = 100;
    ve->defaults[8] = 0;
    ve->defaults[9] = 0;

    ve->limits[0][0] = 8;   ve->limits[1][0] = 128;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 64;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 256;
    ve->limits[0][3] = 0;   ve->limits[1][3] = 64;
    ve->limits[0][4] = 0;   ve->limits[1][4] = 100;
    ve->limits[0][5] = 0;   ve->limits[1][5] = 500;
    ve->limits[0][6] = 0;   ve->limits[1][6] = 4;
    ve->limits[0][7] = 0;   ve->limits[1][7] = 100;
    ve->limits[0][8] = 0;   ve->limits[1][8] = 1;
    ve->limits[0][9] = 0;   ve->limits[1][9] = 2;

    ve->description = "Fragment TV";

    ve->sub_format = 1;

    ve->param_description =
        vje_build_param_list(ve->num_params,
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

    return ve;
}


void *fragmenttv_malloc(int w, int h)
{
    size_t len = w * h;

    fragmenttv_t *m = (fragmenttv_t*) vj_calloc(sizeof(fragmenttv_t));

    m->tmp[0] = (uint8_t*) vj_malloc(len);
    m->tmp[1] = (uint8_t*) vj_malloc(len);
    m->tmp[2] = (uint8_t*) vj_malloc(len);

    m->n_threads = vje_advise_num_threads(w * h);

    m->first = 1;
    m->seed_frame = 0;
    m->stable_mode = 1;
    m->drift_accum = 0;

    init_rand(m, 0x12345678);

    return m;
}

void fragmenttv_free(void *ptr)
{
    fragmenttv_t *m = (fragmenttv_t*) ptr;
    if(!m) return;

    free(m->tmp[0]);
    free(m->tmp[1]);
    free(m->tmp[2]);
    free(m);
}

static void make_alpha(uint8_t *dst, int size)
{
    for(int i=0;i<size;i++){
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

    for(int gy=0; gy<h; gy+=tile)
    for(int gx=0; gx<w; gx+=tile)
    {
        if(m->tile_count >= MAX_TILES)
            return;

        uint32_t seed =
            (uint32_t)(gx * 73856093u ^ gy * 19349663u ^ m->frame_count * 83492791u);

        int r = (seed >> 16) & 0x7FFF;

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

        int dx = gx + ((int)(seed >> 8) % (m->drift_accum*2+1)) - m->drift_accum;
        int dy = gy + ((int)(seed >> 12) % (m->drift_accum*2+1)) - m->drift_accum;

        dx = clampi(dx, 0, w - 1);
        dy = clampi(dy, 0, h - 1);

        if(dx + size > w) size = w - dx;
        if(dy + size > h) size = h - dy;

        t->dx = dx;
        t->dy = dy;

        t->sx = clampi(gx + ((int)(seed >> 4) % (scatter*2+1)) - scatter, 0, w-size);
        t->sy = clampi(gy + ((int)(seed >> 6) % (scatter*2+1)) - scatter, 0, h-size);

        t->size = size;

        make_alpha(t->alphaX, size);
        make_alpha(t->alphaY, size);
    }
}

static inline uint8_t blend_fast(uint8_t d, uint8_t s, int a)
{
    int r = (d * (255 - a) + s * a + 128) >> 8;
    return (uint8_t)clampi(r, 0, 255);
}

static inline uint8_t blend_diff(uint8_t d, uint8_t s)
{
    int v = (int)d - (int)s;
    if(v < 0) v = -v;
    return (uint8_t)v;
}

static inline uint8_t blend_mul(uint8_t d, uint8_t s)
{
    int v = (d * s) + 128;
    return (uint8_t)((v >> 8));
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
        }
        break;

        case BLEND_DIFF:
        {
            Y[i] = blend_diff(Y[i], sy);
        }
        break;

        case BLEND_MULTIPLY:
            Y[i] = blend_mul(Y[i], sy);
            break;

        case BLEND_LUMA:
            Y[i] = blend_fast(Y[i], sy, a);
            break;
    }
}

static void draw_tiles(fragmenttv_t *m,
                       uint8_t *Y, uint8_t *U, uint8_t *V,
                       uint8_t *sY, uint8_t *sU, uint8_t *sV,
                       int w,
                       int mode)
{
#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for(int t = 0; t < m->tile_count; t++)
    {
        frag_tile *q = &m->tiles[t];

        const int base_x = q->dx;
        const int base_y = q->dy;
        const int size   = q->size;

        for(int y = 0; y < size; y++)
        {
            const int dy = base_y + y;
            const int di = dy * w + base_x;
            const int si = (q->sy + y) * w + q->sx;
            const uint8_t ay = q->alphaY[y];

            for(int x = 0; x < size; x++)
            {
                const int idx = di + x;
                const int sidx = si + x;
                const int a = (q->alphaX[x] * ay) >> 8;

                do_blend(Y, U, V,
                         idx,
                         sY[sidx],
                         sU[sidx],
                         sV[sidx],
                         a,
                         mode);
            }
        }
    }
}
static void draw_borders(fragmenttv_t *m,
                          uint8_t *Y,
                          int w,
                          int strength)
{
    if(strength <= 0)
        return;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for(int t = 0; t < m->tile_count; t++)
    {
        frag_tile *q = &m->tiles[t];

        const int x0 = q->dx;
        const int y0 = q->dy;
        const int x1 = q->dx + q->size - 1;
        const int y1 = q->dy + q->size - 1;

        const uint8_t s = (uint8_t) strength;

        for(int x = x0; x <= x1; x++)
        {
            int i_top = y0 * w + x;
            int i_bot = y1 * w + x;

            Y[i_top] = clamp8(Y[i_top] + s);
            Y[i_bot] = clamp8(Y[i_bot] + s);
        }

        for(int y = y0; y <= y1; y++)
        {
            int row = y * w;

            int i_l = row + x0;
            int i_r = row + x1;

            Y[i_l] = clamp8(Y[i_l] + s);
            Y[i_r] = clamp8(Y[i_r] + s);
        }
    }
}

void fragmenttv_apply(void *ptr, VJFrame *frame, int *args)
{
    fragmenttv_t *m = (fragmenttv_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    const int tile     = args[0];
    const int vary     = args[1];
    const int scatter  = args[2];
    const int drift    = args[3];
    const int cover    = args[4];
    const int refresh  = args[5];
    const int mode     = args[6];
    const int border   = args[7];
    const int blackbg  = args[8];
    const int edge_mode = args[9];

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    veejay_memcpy(m->tmp[0], Y, len);
    veejay_memcpy(m->tmp[1], U, len);
    veejay_memcpy(m->tmp[2], V, len);

    if(blackbg){
        veejay_memset(Y, 0, len);
        veejay_memset(U, 128, len);
        veejay_memset(V, 128, len);
    }

    int changed =
        m->first ||
        (refresh > 0 && (m->frame_count % refresh) == 0) ||
        memcmp(m->prev, args, sizeof(int)*10);

    if(changed){
        generate_tiles(m, w, h, tile, vary, scatter, drift, cover);
        veejay_memcpy(m->prev, args, sizeof(int)*10);
    }

    draw_tiles(m, Y, U, V, m->tmp[0], m->tmp[1], m->tmp[2], w, mode);

    if(edge_mode == 2){
        draw_borders(m, Y, w, border);
        m->frame_count = 0;
        m->seed_frame = 0;
        m->drift_accum = 0;
        m->first = 1;
    }

    m->frame_count++;
    m->first = 0;
}