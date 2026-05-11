/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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

/**
 *
 * Based on the original implementation of Kim Asendorf
 *
 * https://github.com/kimasendorf/ASDFPixelSort/blob/master/ASDFPixelSort.pde
 *
 * ASDFPixelSort
 * Processing script to sort portions of pixels in an image.
 * DEMO: http://kimasendorf.com/mountain-tour/ http://kimasendorf.com/sorted-aerial/
 * Kim Asendorf 2010 http://kimasendorf.com
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "pixelsort.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _OPENMP
extern int omp_get_thread_num(void);
#define PS_OMP_FOR _Pragma("omp parallel for schedule(static) num_threads(p->n_threads)")
#else
#define PS_OMP_FOR
#endif

#define PS_MODE_WHITE   0
#define PS_MODE_BLACK   1
#define PS_MODE_BRIGHT  2
#define PS_MODE_DARK    3

#define PS_PASS_COLUMNS_ROWS 0
#define PS_PASS_ROWS_COLUMNS 1
#define PS_PASS_COLUMNS_ONLY 2
#define PS_PASS_ROWS_ONLY    3

#ifndef PS_INSERTION_LIMIT
#define PS_INSERTION_LIMIT 32
#endif

#define PS_WHITE_CHROMA_LO 64
#define PS_WHITE_CHROMA_HI 192

#define PS_BLACK_CHROMA_LO 48
#define PS_BLACK_CHROMA_HI 208

typedef struct {
    uint32_t *line;
    uint32_t *sorted;
    unsigned int count[256];
} pixelsort_worker_t;

typedef struct {
    int w;
    int h;
    int line_cap;
    int n_threads;

    pixelsort_worker_t *workers;
} pixelsort_t;

static inline int ps_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int ps_worker_id(pixelsort_t *p)
{
#ifdef _OPENMP
    int id = omp_get_thread_num();

    if(id < 0 || id >= p->n_threads)
        return 0;

    return id;
#else
    return 0;
#endif
}

vj_effect *pixelsort_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)  free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    /*
     * Mode:
     *   0 = White-ish runs  : high Y, neutral-ish chroma
     *   1 = Black-ish runs  : low Y, neutral-ish chroma
     *   2 = Bright runs     : high Y, chroma ignored
     *   3 = Dark runs       : low Y, chroma ignored
     *
     * Pass:
     *   0 = columns then rows
     *   1 = rows then columns
     *   2 = columns only
     *   3 = rows only
     *
     * Threshold:
     *   0..255 Y threshold.
     */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 3;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->defaults[0] = PS_MODE_BRIGHT;
    ve->defaults[1] = PS_PASS_COLUMNS_ROWS;
    ve->defaults[2] = 127;

    ve->description = "Asendorf Pixel Sort";
    ve->sub_format = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Pass",
        "Threshold"
    );

    return ve;
}

void *pixelsort_malloc(int w, int h)
{
    pixelsort_t *p;
    int i;

    if(w <= 0 || h <= 0)
        return NULL;

    p = (pixelsort_t *) vj_calloc(sizeof(pixelsort_t));
    if(!p)
        return NULL;

    p->w = w;
    p->h = h;
    p->line_cap = (w > h ? w : h);

#ifdef _OPENMP
    p->n_threads = vje_advise_num_threads(w * h);
    if(p->n_threads <= 0)
        p->n_threads = 1;
#else
    p->n_threads = 1;
#endif

    p->workers = (pixelsort_worker_t *)
        vj_calloc(sizeof(pixelsort_worker_t) * (size_t) p->n_threads);

    if(!p->workers) {
        free(p);
        return NULL;
    }

    for(i = 0; i < p->n_threads; i++) {
        p->workers[i].line =
            (uint32_t *) vj_calloc(sizeof(uint32_t) * (size_t) p->line_cap);

        p->workers[i].sorted =
            (uint32_t *) vj_calloc(sizeof(uint32_t) * (size_t) p->line_cap);

        if(!p->workers[i].line || !p->workers[i].sorted) {
            int j;

            for(j = 0; j <= i; j++) {
                if(p->workers[j].line)
                    free(p->workers[j].line);

                if(p->workers[j].sorted)
                    free(p->workers[j].sorted);
            }

            free(p->workers);
            free(p);
            return NULL;
        }
    }

    return (void *) p;
}

void pixelsort_free(void *ptr)
{
    pixelsort_t *p = (pixelsort_t *) ptr;
    int i;

    if(!p)
        return;

    if(p->workers) {
        for(i = 0; i < p->n_threads; i++) {
            if(p->workers[i].line)
                free(p->workers[i].line);

            if(p->workers[i].sorted)
                free(p->workers[i].sorted);
        }

        free(p->workers);
    }

    free(p);
}

static void pixelsort_csort32_range(pixelsort_worker_t *wrk,
                                    uint32_t *restrict input,
                                    uint32_t *restrict output,
                                    unsigned int n,
                                    unsigned int lo_y,
                                    unsigned int hi_y)
{
    unsigned int *restrict count = wrk->count;
    unsigned int i;
    unsigned int sum;

    for(i = lo_y; i <= hi_y; i++)
        count[i] = 0;

    for(i = 0; i < n; i++)
        count[input[i] & 0xff]++;

    sum = 0;
    for(i = lo_y; i <= hi_y; i++) {
        sum += count[i];
        count[i] = sum;
    }

    i = n;
    while(i > 0) {
        unsigned int key;

        i--;
        key = input[i] & 0xff;
        output[--count[key]] = input[i];
    }
}

static inline void pixelsort_insertion32(uint32_t *restrict a, unsigned int n)
{
    unsigned int i;

    for(i = 1; i < n; i++) {
        uint32_t v = a[i];
        uint32_t key = v & 0xff;
        unsigned int j = i;

        while(j > 0 && ((a[j - 1] & 0xff) > key)) {
            a[j] = a[j - 1];
            j--;
        }

        a[j] = v;
    }
}

static inline int pixelsort_need_sort_x_range(uint8_t *restrict Y,
                                              unsigned int base,
                                              unsigned int n,
                                              unsigned int *lo_y,
                                              unsigned int *hi_y)
{
    unsigned int i;
    unsigned int prev;
    unsigned int lo;
    unsigned int hi;
    int need_sort = 0;

    prev = Y[base];
    lo = prev;
    hi = prev;

    for(i = 1; i < n; i++) {
        unsigned int cy = Y[base + i];

        if(cy < prev)
            need_sort = 1;

        if(cy < lo)
            lo = cy;
        else if(cy > hi)
            hi = cy;

        prev = cy;
    }

    *lo_y = lo;
    *hi_y = hi;

    return need_sort;
}

static inline int pixelsort_need_sort_y_range(uint8_t *restrict Y,
                                              unsigned int pos,
                                              unsigned int width,
                                              unsigned int n,
                                              unsigned int *lo_y,
                                              unsigned int *hi_y)
{
    unsigned int i;
    unsigned int prev;
    unsigned int lo;
    unsigned int hi;
    int need_sort = 0;

    prev = Y[pos];
    lo = prev;
    hi = prev;

    for(i = 1; i < n; i++) {
        unsigned int cy;

        pos += width;
        cy = Y[pos];

        if(cy < prev)
            need_sort = 1;

        if(cy < lo)
            lo = cy;
        else if(cy > hi)
            hi = cy;

        prev = cy;
    }

    *lo_y = lo;
    *hi_y = hi;

    return need_sort;
}

static inline void pixelsort_pack_x(uint8_t *P[3],
                                    uint32_t *restrict dst,
                                    unsigned int n,
                                    unsigned int x,
                                    unsigned int y,
                                    unsigned int width)
{
    uint8_t *restrict Y = P[0];
    uint8_t *restrict U = P[1];
    uint8_t *restrict V = P[2];

    unsigned int i;
    unsigned int base = y * width + x;

    for(i = 0; i < n; i++) {
        unsigned int pos = base + i;

        dst[i] =
            ((uint32_t) Y[pos])        |
            ((uint32_t) U[pos] << 8)   |
            ((uint32_t) V[pos] << 16);
    }
}

static inline void pixelsort_unpack_x(uint8_t *P[3],
                                      uint32_t *restrict src,
                                      unsigned int n,
                                      unsigned int x,
                                      unsigned int y,
                                      unsigned int width)
{
    uint8_t *restrict Y = P[0];
    uint8_t *restrict U = P[1];
    uint8_t *restrict V = P[2];

    unsigned int i;
    unsigned int base = y * width + x;

    for(i = 0; i < n; i++) {
        unsigned int pos = base + i;
        uint32_t v = src[i];

        Y[pos] = (uint8_t) (v & 0xff);
        U[pos] = (uint8_t) ((v >> 8) & 0xff);
        V[pos] = (uint8_t) ((v >> 16) & 0xff);
    }
}

static inline void pixelsort_pack_y(uint8_t *P[3],
                                    uint32_t *restrict dst,
                                    unsigned int n,
                                    unsigned int x,
                                    unsigned int y,
                                    unsigned int width)
{
    uint8_t *restrict Y = P[0];
    uint8_t *restrict U = P[1];
    uint8_t *restrict V = P[2];

    unsigned int i;
    unsigned int pos = y * width + x;

    for(i = 0; i < n; i++, pos += width) {
        dst[i] =
            ((uint32_t) Y[pos])        |
            ((uint32_t) U[pos] << 8)   |
            ((uint32_t) V[pos] << 16);
    }
}

static inline void pixelsort_unpack_y(uint8_t *P[3],
                                      uint32_t *restrict src,
                                      unsigned int n,
                                      unsigned int x,
                                      unsigned int y,
                                      unsigned int width)
{
    uint8_t *restrict Y = P[0];
    uint8_t *restrict U = P[1];
    uint8_t *restrict V = P[2];

    unsigned int i;
    unsigned int pos = y * width + x;

    for(i = 0; i < n; i++, pos += width) {
        uint32_t v = src[i];

        Y[pos] = (uint8_t) (v & 0xff);
        U[pos] = (uint8_t) ((v >> 8) & 0xff);
        V[pos] = (uint8_t) ((v >> 16) & 0xff);
    }
}

static inline void pixelsort_sort_x(pixelsort_t *p,
                                    pixelsort_worker_t *wrk,
                                    uint8_t *P[3],
                                    unsigned int width,
                                    unsigned int x0,
                                    unsigned int y,
                                    unsigned int x1)
{
    unsigned int n;
    unsigned int base;
    unsigned int lo_y;
    unsigned int hi_y;

    if(x1 <= x0)
        return;

    n = x1 - x0;

    if(n < 2 || n > (unsigned int) p->line_cap)
        return;

    base = y * width + x0;

    if(!pixelsort_need_sort_x_range(P[0], base, n, &lo_y, &hi_y))
        return;

    pixelsort_pack_x(P, wrk->line, n, x0, y, width);

    if(n <= PS_INSERTION_LIMIT) {
        pixelsort_insertion32(wrk->line, n);
        pixelsort_unpack_x(P, wrk->line, n, x0, y, width);
    }
    else {
        pixelsort_csort32_range(wrk, wrk->line, wrk->sorted, n, lo_y, hi_y);
        pixelsort_unpack_x(P, wrk->sorted, n, x0, y, width);
    }
}

static inline void pixelsort_sort_y(pixelsort_t *p,
                                    pixelsort_worker_t *wrk,
                                    uint8_t *P[3],
                                    unsigned int width,
                                    unsigned int x,
                                    unsigned int y0,
                                    unsigned int y1)
{
    unsigned int n;
    unsigned int pos;
    unsigned int lo_y;
    unsigned int hi_y;

    if(y1 <= y0)
        return;

    n = y1 - y0;

    if(n < 2 || n > (unsigned int) p->line_cap)
        return;

    pos = y0 * width + x;

    if(!pixelsort_need_sort_y_range(P[0], pos, width, n, &lo_y, &hi_y))
        return;

    pixelsort_pack_y(P, wrk->line, n, x, y0, width);

    if(n <= PS_INSERTION_LIMIT) {
        pixelsort_insertion32(wrk->line, n);
        pixelsort_unpack_y(P, wrk->line, n, x, y0, width);
    }
    else {
        pixelsort_csort32_range(wrk, wrk->line, wrk->sorted, n, lo_y, hi_y);
        pixelsort_unpack_y(P, wrk->sorted, n, x, y0, width);
    }
}

#define PS_ACTIVE_BRIGHT(pos) \
    (Y[(pos)] >= threshold)

#define PS_ACTIVE_DARK(pos) \
    (Y[(pos)] <= threshold)

#define PS_ACTIVE_WHITE(pos) \
    (Y[(pos)] >= threshold && \
     U[(pos)] >= PS_WHITE_CHROMA_LO && U[(pos)] <= PS_WHITE_CHROMA_HI && \
     V[(pos)] >= PS_WHITE_CHROMA_LO && V[(pos)] <= PS_WHITE_CHROMA_HI)

#define PS_ACTIVE_BLACK(pos) \
    (Y[(pos)] <= threshold && \
     U[(pos)] >= PS_BLACK_CHROMA_LO && U[(pos)] <= PS_BLACK_CHROMA_HI && \
     V[(pos)] >= PS_BLACK_CHROMA_LO && V[(pos)] <= PS_BLACK_CHROMA_HI)

#define DEFINE_ROW_RUN(NAME, ACTIVE)                                      \
static inline unsigned int pixelsort_row_run_##NAME(                      \
    pixelsort_t *p,                                                       \
    pixelsort_worker_t *wrk,                                              \
    uint8_t *P[3],                                                        \
    unsigned int width,                                                   \
    unsigned int x,                                                       \
    unsigned int y,                                                       \
    int threshold)                                                        \
{                                                                         \
    uint8_t *restrict Y = P[0];                                           \
    uint8_t *restrict U = P[1];                                           \
    uint8_t *restrict V = P[2];                                           \
    unsigned int base = y * width;                                        \
                                                                          \
    while(x < width && !(ACTIVE(base + x)))                               \
        x++;                                                              \
                                                                          \
    if(x >= width)                                                        \
        return width;                                                     \
                                                                          \
    {                                                                     \
        unsigned int x0 = x;                                              \
        unsigned int x1;                                                  \
                                                                          \
        while(x < width && ACTIVE(base + x))                              \
            x++;                                                          \
                                                                          \
        x1 = x;                                                           \
        pixelsort_sort_x(p, wrk, P, width, x0, y, x1);                    \
    }                                                                     \
                                                                          \
    return (x < width) ? x + 1 : x;                                       \
}

#define DEFINE_COLUMN_RUN(NAME, ACTIVE)                                   \
static inline unsigned int pixelsort_column_run_##NAME(                   \
    pixelsort_t *p,                                                       \
    pixelsort_worker_t *wrk,                                              \
    uint8_t *P[3],                                                        \
    unsigned int width,                                                   \
    unsigned int height,                                                  \
    unsigned int x,                                                       \
    unsigned int y,                                                       \
    int threshold)                                                        \
{                                                                         \
    uint8_t *restrict Y = P[0];                                           \
    uint8_t *restrict U = P[1];                                           \
    uint8_t *restrict V = P[2];                                           \
    unsigned int pos = y * width + x;                                     \
                                                                          \
    while(y < height && !(ACTIVE(pos))) {                                 \
        y++;                                                              \
        pos += width;                                                     \
    }                                                                     \
                                                                          \
    if(y >= height)                                                       \
        return height;                                                    \
                                                                          \
    {                                                                     \
        unsigned int y0 = y;                                              \
        unsigned int y1;                                                  \
                                                                          \
        while(y < height && ACTIVE(pos)) {                                \
            y++;                                                          \
            pos += width;                                                 \
        }                                                                 \
                                                                          \
        y1 = y;                                                           \
        pixelsort_sort_y(p, wrk, P, width, x, y0, y1);                    \
    }                                                                     \
                                                                          \
    return (y < height) ? y + 1 : y;                                      \
}

DEFINE_ROW_RUN(bright, PS_ACTIVE_BRIGHT)
DEFINE_ROW_RUN(dark,   PS_ACTIVE_DARK)
DEFINE_ROW_RUN(white,  PS_ACTIVE_WHITE)
DEFINE_ROW_RUN(black,  PS_ACTIVE_BLACK)

DEFINE_COLUMN_RUN(bright, PS_ACTIVE_BRIGHT)
DEFINE_COLUMN_RUN(dark,   PS_ACTIVE_DARK)
DEFINE_COLUMN_RUN(white,  PS_ACTIVE_WHITE)
DEFINE_COLUMN_RUN(black,  PS_ACTIVE_BLACK)

#define DEFINE_ROWS(NAME)                                                 \
static void pixelsort_rows_##NAME(                                        \
    pixelsort_t *p,                                                       \
    uint8_t *P[3],                                                        \
    unsigned int width,                                                   \
    unsigned int height,                                                  \
    int threshold)                                                        \
{                                                                         \
    int yi;                                                               \
    int h = (int) height;                                                 \
                                                                          \
    PS_OMP_FOR                                                           \
    for(yi = 0; yi < h; yi++) {                                           \
        unsigned int y = (unsigned int) yi;                               \
        unsigned int x = 0;                                               \
        pixelsort_worker_t *wrk = &p->workers[ps_worker_id(p)];           \
                                                                          \
        while(x < width) {                                                \
            unsigned int nx = pixelsort_row_run_##NAME(                   \
                p, wrk, P, width, x, y, threshold);                       \
                                                                          \
            x = (nx <= x) ? x + 1 : nx;                                   \
        }                                                                 \
    }                                                                     \
}

#define DEFINE_COLUMNS(NAME)                                              \
static void pixelsort_columns_##NAME(                                     \
    pixelsort_t *p,                                                       \
    uint8_t *P[3],                                                        \
    unsigned int width,                                                   \
    unsigned int height,                                                  \
    int threshold)                                                        \
{                                                                         \
    int xi;                                                               \
    int w = (int) width;                                                  \
                                                                          \
    PS_OMP_FOR                                                           \
    for(xi = 0; xi < w; xi++) {                                           \
        unsigned int x = (unsigned int) xi;                               \
        unsigned int y = 0;                                               \
        pixelsort_worker_t *wrk = &p->workers[ps_worker_id(p)];           \
                                                                          \
        while(y < height) {                                               \
            unsigned int ny = pixelsort_column_run_##NAME(                \
                p, wrk, P, width, height, x, y, threshold);               \
                                                                          \
            y = (ny <= y) ? y + 1 : ny;                                   \
        }                                                                 \
    }                                                                     \
}

DEFINE_ROWS(bright)
DEFINE_ROWS(dark)
DEFINE_ROWS(white)
DEFINE_ROWS(black)

DEFINE_COLUMNS(bright)
DEFINE_COLUMNS(dark)
DEFINE_COLUMNS(white)
DEFINE_COLUMNS(black)

static inline void pixelsort_rows(pixelsort_t *p,
                                  uint8_t *P[3],
                                  unsigned int width,
                                  unsigned int height,
                                  int mode,
                                  int threshold)
{
    switch(mode) {
        case PS_MODE_WHITE:
            pixelsort_rows_white(p, P, width, height, threshold);
            break;

        case PS_MODE_BLACK:
            pixelsort_rows_black(p, P, width, height, threshold);
            break;

        case PS_MODE_DARK:
            pixelsort_rows_dark(p, P, width, height, threshold);
            break;

        case PS_MODE_BRIGHT:
        default:
            pixelsort_rows_bright(p, P, width, height, threshold);
            break;
    }
}

static inline void pixelsort_columns(pixelsort_t *p,
                                     uint8_t *P[3],
                                     unsigned int width,
                                     unsigned int height,
                                     int mode,
                                     int threshold)
{
    switch(mode) {
        case PS_MODE_WHITE:
            pixelsort_columns_white(p, P, width, height, threshold);
            break;

        case PS_MODE_BLACK:
            pixelsort_columns_black(p, P, width, height, threshold);
            break;

        case PS_MODE_DARK:
            pixelsort_columns_dark(p, P, width, height, threshold);
            break;

        case PS_MODE_BRIGHT:
        default:
            pixelsort_columns_bright(p, P, width, height, threshold);
            break;
    }
}

void pixelsort_apply(void *ptr, VJFrame *frame, int *args)
{
    pixelsort_t *p = (pixelsort_t *) ptr;
    unsigned int width;
    unsigned int height;

    uint8_t *P[3];

    int mode;
    int pass;
    int threshold;

    width  = (unsigned int) frame->width;
    height = (unsigned int) frame->height;

    P[0] = frame->data[0];
    P[1] = frame->data[1];
    P[2] = frame->data[2];

    mode      = ps_clampi(args[0], 0, 3);
    pass      = ps_clampi(args[1], 0, 3);
    threshold = ps_clampi(args[2], 0, 255);

    switch(pass) {
        case PS_PASS_ROWS_COLUMNS:
            pixelsort_rows(p, P, width, height, mode, threshold);
            pixelsort_columns(p, P, width, height, mode, threshold);
            break;

        case PS_PASS_COLUMNS_ONLY:
            pixelsort_columns(p, P, width, height, mode, threshold);
            break;

        case PS_PASS_ROWS_ONLY:
            pixelsort_rows(p, P, width, height, mode, threshold);
            break;

        case PS_PASS_COLUMNS_ROWS:
        default:
            pixelsort_columns(p, P, width, height, mode, threshold);
            pixelsort_rows(p, P, width, height, mode, threshold);
            break;
    }
}