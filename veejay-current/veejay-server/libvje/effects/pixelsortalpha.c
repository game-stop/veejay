/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nwelburg@gmail.com>
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

/*
    Based on the original implementation of Kim Asendorf
    https://github.com/kimasendorf/ASDFPixelSort/blob/master/ASDFPixelSort.pde
 */

#include "common.h"
#include "pixelsortalpha.h"

#include <omp.h>
#define PSA_THREAD_ID() omp_get_thread_num()

#define PSA_MODE_NONBLACK 0
#define PSA_MODE_WHITE    1
#define PSA_MODE_NONWHITE 2
#define PSA_PASS_COLUMNS_ROWS 0
#define PSA_PASS_ROWS_COLUMNS 1
#define PSA_PASS_COLUMNS_ONLY 2
#define PSA_PASS_ROWS_ONLY    3
#define PSA_INSERTION_LIMIT 32


typedef struct {
    uint32_t *line;
    uint32_t *sorted;
    unsigned int count[256];
} pixelsortalpha_worker_t;

typedef struct {
    int w;
    int h;
    int line_cap;
    int n_threads;
    pixelsortalpha_worker_t *workers;
} pixelsortalpha_t;

static inline int psa_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int psa_worker_id(pixelsortalpha_t *p)
{
#ifdef _OPENMP
    int id = omp_get_thread_num();
    return (id >= 0 && id < p->n_threads) ? id : 0;
#else
    return 0;
#endif
}

vj_effect *pixelsortalpha_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;
    ve->defaults[0] = PSA_MODE_NONBLACK;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 3;
    ve->defaults[1] = PSA_PASS_ROWS_ONLY;

    ve->description = "Alpha: Asendorf Glitch";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->alpha = FLAG_ALPHA_SRC_A | FLAG_ALPHA_OUT;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Pass"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][0],
        0,
        "Non-black Alpha",
        "White Alpha",
        "Non-white Alpha"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Columns first",
        "Rows first",
        "Columns Only",
        "Rows Only"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000, /* Mode */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000  /* Pass */
    );

    (void) w;
    (void) h;

    return ve;
}

void *pixelsortalpha_malloc(int w, int h)
{
    pixelsortalpha_t *p;

    if(w <= 0 || h <= 0)
        return NULL;

    p = (pixelsortalpha_t*) vj_calloc(sizeof(pixelsortalpha_t));
    if(!p)
        return NULL;

    p->w = w;
    p->h = h;
    p->line_cap = (w > h) ? w : h;
    p->n_threads = vje_advise_num_threads(w * h);
    if(p->n_threads < 1)
        p->n_threads = 1;

    p->workers = (pixelsortalpha_worker_t*) vj_calloc(sizeof(pixelsortalpha_worker_t) * p->n_threads);
    if(!p->workers) {
        free(p);
        return NULL;
    }

    for(int i = 0; i < p->n_threads; i++) {
        p->workers[i].line = (uint32_t*) vj_malloc(sizeof(uint32_t) * p->line_cap);
        p->workers[i].sorted = (uint32_t*) vj_malloc(sizeof(uint32_t) * p->line_cap);

        if(!p->workers[i].line || !p->workers[i].sorted) {
            for(int j = 0; j <= i; j++) {
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

    return (void*) p;
}

void pixelsortalpha_free(void *ptr)
{
    pixelsortalpha_t *p = (pixelsortalpha_t*) ptr;
    if(!p)
        return;

    if(p->workers) {
        for(int i = 0; i < p->n_threads; i++) {
            if(p->workers[i].line)
                free(p->workers[i].line);
            if(p->workers[i].sorted)
                free(p->workers[i].sorted);
        }

        free(p->workers);
    }

    free(p);
}

static void psa_csort32_range(
    pixelsortalpha_worker_t *wrk,
    uint32_t *restrict input,
    uint32_t *restrict output,
    unsigned int n,
    unsigned int lo_y,
    unsigned int hi_y
) {
    unsigned int *restrict count = wrk->count;
    unsigned int sum = 0;

    for(unsigned int i = lo_y; i <= hi_y; i++)
        count[i] = 0;

    for(unsigned int i = 0; i < n; i++)
        count[input[i] & 0xff]++;

    for(unsigned int i = lo_y; i <= hi_y; i++) {
        sum += count[i];
        count[i] = sum;
    }

    for(unsigned int i = n; i > 0; ) {
        i--;
        const unsigned int key = input[i] & 0xff;
        output[--count[key]] = input[i];
    }
}

static inline void psa_insertion32(uint32_t *restrict a, unsigned int n)
{
    for(unsigned int i = 1; i < n; i++) {
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

static inline int psa_need_sort_x_range(
    uint8_t *restrict Y,
    unsigned int base,
    unsigned int n,
    unsigned int *lo_y,
    unsigned int *hi_y
) {
    unsigned int prev = Y[base];
    unsigned int lo = prev;
    unsigned int hi = prev;
    int need_sort = 0;

    for(unsigned int i = 1; i < n; i++) {
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

static inline int psa_need_sort_y_range(
    uint8_t *restrict Y,
    unsigned int pos,
    unsigned int width,
    unsigned int n,
    unsigned int *lo_y,
    unsigned int *hi_y
) {
    unsigned int prev = Y[pos];
    unsigned int lo = prev;
    unsigned int hi = prev;
    int need_sort = 0;

    for(unsigned int i = 1; i < n; i++) {
        pos += width;
        unsigned int cy = Y[pos];

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

static inline void psa_pack_x(
    uint8_t *P[4],
    uint32_t *restrict dst,
    unsigned int n,
    unsigned int x,
    unsigned int y,
    unsigned int width
) {
    uint8_t *restrict Y = P[0];
    uint8_t *restrict U = P[1];
    uint8_t *restrict V = P[2];
    uint8_t *restrict A = P[3];

    const unsigned int base = y * width + x;

    for(unsigned int i = 0; i < n; i++) {
        const unsigned int pos = base + i;

        dst[i] =
            ((uint32_t)Y[pos]) |
            ((uint32_t)U[pos] << 8) |
            ((uint32_t)V[pos] << 16) |
            ((uint32_t)A[pos] << 24);
    }
}

static inline void psa_unpack_x(
    uint8_t *P[4],
    uint32_t *restrict src,
    unsigned int n,
    unsigned int x,
    unsigned int y,
    unsigned int width
) {
    uint8_t *restrict Y = P[0];
    uint8_t *restrict U = P[1];
    uint8_t *restrict V = P[2];
    uint8_t *restrict A = P[3];

    const unsigned int base = y * width + x;

    for(unsigned int i = 0; i < n; i++) {
        const unsigned int pos = base + i;
        const uint32_t v = src[i];

        Y[pos] = (uint8_t)(v & 0xff);
        U[pos] = (uint8_t)((v >> 8) & 0xff);
        V[pos] = (uint8_t)((v >> 16) & 0xff);
        A[pos] = (uint8_t)((v >> 24) & 0xff);
    }
}

static inline void psa_pack_y(
    uint8_t *P[4],
    uint32_t *restrict dst,
    unsigned int n,
    unsigned int x,
    unsigned int y,
    unsigned int width
) {
    uint8_t *restrict Y = P[0];
    uint8_t *restrict U = P[1];
    uint8_t *restrict V = P[2];
    uint8_t *restrict A = P[3];

    unsigned int pos = y * width + x;

    for(unsigned int i = 0; i < n; i++, pos += width) {
        dst[i] =
            ((uint32_t)Y[pos]) |
            ((uint32_t)U[pos] << 8) |
            ((uint32_t)V[pos] << 16) |
            ((uint32_t)A[pos] << 24);
    }
}

static inline void psa_unpack_y(
    uint8_t *P[4],
    uint32_t *restrict src,
    unsigned int n,
    unsigned int x,
    unsigned int y,
    unsigned int width
) {
    uint8_t *restrict Y = P[0];
    uint8_t *restrict U = P[1];
    uint8_t *restrict V = P[2];
    uint8_t *restrict A = P[3];

    unsigned int pos = y * width + x;

    for(unsigned int i = 0; i < n; i++, pos += width) {
        const uint32_t v = src[i];

        Y[pos] = (uint8_t)(v & 0xff);
        U[pos] = (uint8_t)((v >> 8) & 0xff);
        V[pos] = (uint8_t)((v >> 16) & 0xff);
        A[pos] = (uint8_t)((v >> 24) & 0xff);
    }
}

static inline void psa_sort_x(
    pixelsortalpha_t *p,
    pixelsortalpha_worker_t *wrk,
    uint8_t *P[4],
    unsigned int width,
    unsigned int x0,
    unsigned int y,
    unsigned int x1
) {
    if(x1 <= x0)
        return;

    const unsigned int n = x1 - x0;

    if(n < 2 || n > (unsigned int)p->line_cap)
        return;

    const unsigned int base = y * width + x0;

    unsigned int lo_y;
    unsigned int hi_y;

    if(!psa_need_sort_x_range(P[0], base, n, &lo_y, &hi_y))
        return;

    psa_pack_x(P, wrk->line, n, x0, y, width);

    if(n <= PSA_INSERTION_LIMIT) {
        psa_insertion32(wrk->line, n);
        psa_unpack_x(P, wrk->line, n, x0, y, width);
    } else {
        psa_csort32_range(wrk, wrk->line, wrk->sorted, n, lo_y, hi_y);
        psa_unpack_x(P, wrk->sorted, n, x0, y, width);
    }
}

static inline void psa_sort_y(
    pixelsortalpha_t *p,
    pixelsortalpha_worker_t *wrk,
    uint8_t *P[4],
    unsigned int width,
    unsigned int x,
    unsigned int y0,
    unsigned int y1
) {
    if(y1 <= y0)
        return;

    const unsigned int n = y1 - y0;

    if(n < 2 || n > (unsigned int)p->line_cap)
        return;

    const unsigned int pos = y0 * width + x;

    unsigned int lo_y;
    unsigned int hi_y;

    if(!psa_need_sort_y_range(P[0], pos, width, n, &lo_y, &hi_y))
        return;

    psa_pack_y(P, wrk->line, n, x, y0, width);

    if(n <= PSA_INSERTION_LIMIT) {
        psa_insertion32(wrk->line, n);
        psa_unpack_y(P, wrk->line, n, x, y0, width);
    } else {
        psa_csort32_range(wrk, wrk->line, wrk->sorted, n, lo_y, hi_y);
        psa_unpack_y(P, wrk->sorted, n, x, y0, width);
    }
}

#define PSA_ACTIVE_NONBLACK(pos) (A[(pos)] > pixel_Y_lo_)
#define PSA_ACTIVE_WHITE(pos)    (A[(pos)] >= pixel_Y_hi_)
#define PSA_ACTIVE_NONWHITE(pos) (A[(pos)] < pixel_Y_hi_)

#define DEFINE_PSA_ROW_RUN(NAME, ACTIVE)                                  \
static inline unsigned int psa_row_run_##NAME(                             \
    pixelsortalpha_t *p,                                                    \
    pixelsortalpha_worker_t *wrk,                                           \
    uint8_t *P[4],                                                          \
    unsigned int width,                                                     \
    unsigned int x,                                                         \
    unsigned int y)                                                         \
{                                                                          \
    uint8_t *restrict A = P[3];                                             \
    const unsigned int base = y * width;                                    \
                                                                           \
    while(x < width && !(ACTIVE(base + x)))                                 \
        x++;                                                               \
                                                                           \
    if(x >= width)                                                          \
        return width;                                                       \
                                                                           \
    const unsigned int x0 = x;                                              \
                                                                           \
    while(x < width && ACTIVE(base + x))                                    \
        x++;                                                               \
                                                                           \
    psa_sort_x(p, wrk, P, width, x0, y, x);                                 \
                                                                           \
    return x;                                                              \
}

#define DEFINE_PSA_COLUMN_RUN(NAME, ACTIVE)                                \
static inline unsigned int psa_column_run_##NAME(                           \
    pixelsortalpha_t *p,                                                    \
    pixelsortalpha_worker_t *wrk,                                           \
    uint8_t *P[4],                                                          \
    unsigned int width,                                                     \
    unsigned int height,                                                    \
    unsigned int x,                                                         \
    unsigned int y)                                                         \
{                                                                          \
    uint8_t *restrict A = P[3];                                             \
                                                                           \
    while(y < height && !(ACTIVE(y * width + x)))                           \
        y++;                                                               \
                                                                           \
    if(y >= height)                                                         \
        return height;                                                      \
                                                                           \
    const unsigned int y0 = y;                                              \
                                                                           \
    while(y < height && ACTIVE(y * width + x))                              \
        y++;                                                               \
                                                                           \
    psa_sort_y(p, wrk, P, width, x, y0, y);                                 \
                                                                           \
    return y;                                                              \
}

DEFINE_PSA_ROW_RUN(nonblack, PSA_ACTIVE_NONBLACK)
DEFINE_PSA_ROW_RUN(white,    PSA_ACTIVE_WHITE)
DEFINE_PSA_ROW_RUN(nonwhite, PSA_ACTIVE_NONWHITE)

DEFINE_PSA_COLUMN_RUN(nonblack, PSA_ACTIVE_NONBLACK)
DEFINE_PSA_COLUMN_RUN(white,    PSA_ACTIVE_WHITE)
DEFINE_PSA_COLUMN_RUN(nonwhite, PSA_ACTIVE_NONWHITE)

#define PSA_OMP_PARALLEL_FOR                                                \
    _Pragma("omp parallel for schedule(static) num_threads(p->n_threads)")

#define DEFINE_PSA_ROWS(NAME)                                             \
static void psa_rows_##NAME(                                               \
    pixelsortalpha_t *p,                                                    \
    uint8_t *P[4],                                                          \
    unsigned int width,                                                     \
    unsigned int height)                                                    \
{                                                                          \
    int h = (int)height;                                                    \
                                                                           \
    PSA_OMP_PARALLEL_FOR                                                    \
    for(int yi = 0; yi < h; yi++) {                                         \
        unsigned int y = (unsigned int)yi;                                  \
        unsigned int x = 0;                                                 \
        pixelsortalpha_worker_t *wrk = &p->workers[psa_worker_id(p)];       \
                                                                           \
        while(x < width) {                                                  \
            unsigned int nx = psa_row_run_##NAME(p, wrk, P, width, x, y);   \
            x = (nx <= x) ? x + 1 : nx;                                     \
        }                                                                  \
    }                                                                      \
}

#define DEFINE_PSA_COLUMNS(NAME)                                          \
static void psa_columns_##NAME(                                            \
    pixelsortalpha_t *p,                                                    \
    uint8_t *P[4],                                                          \
    unsigned int width,                                                     \
    unsigned int height)                                                    \
{                                                                          \
    int w = (int)width;                                                     \
                                                                           \
    PSA_OMP_PARALLEL_FOR                                                    \
    for(int xi = 0; xi < w; xi++) {                                         \
        unsigned int x = (unsigned int)xi;                                  \
        unsigned int y = 0;                                                 \
        pixelsortalpha_worker_t *wrk = &p->workers[psa_worker_id(p)];       \
                                                                           \
        while(y < height) {                                                 \
            unsigned int ny = psa_column_run_##NAME(p, wrk, P, width, height, x, y); \
            y = (ny <= y) ? y + 1 : ny;                                     \
        }                                                                  \
    }                                                                      \
}

DEFINE_PSA_ROWS(nonblack)
DEFINE_PSA_ROWS(white)
DEFINE_PSA_ROWS(nonwhite)

DEFINE_PSA_COLUMNS(nonblack)
DEFINE_PSA_COLUMNS(white)
DEFINE_PSA_COLUMNS(nonwhite)

static inline void psa_rows(
    pixelsortalpha_t *p,
    uint8_t *P[4],
    unsigned int width,
    unsigned int height,
    int mode
) {
    switch(mode) {
        case PSA_MODE_WHITE:
            psa_rows_white(p, P, width, height);
            break;
        case PSA_MODE_NONWHITE:
            psa_rows_nonwhite(p, P, width, height);
            break;
        case PSA_MODE_NONBLACK:
        default:
            psa_rows_nonblack(p, P, width, height);
            break;
    }
}

static inline void psa_columns(
    pixelsortalpha_t *p,
    uint8_t *P[4],
    unsigned int width,
    unsigned int height,
    int mode
) {
    switch(mode) {
        case PSA_MODE_WHITE:
            psa_columns_white(p, P, width, height);
            break;
        case PSA_MODE_NONWHITE:
            psa_columns_nonwhite(p, P, width, height);
            break;
        case PSA_MODE_NONBLACK:
        default:
            psa_columns_nonblack(p, P, width, height);
            break;
    }
}

void pixelsortalpha_apply(void *ptr, VJFrame *frame, int *args)
{
    pixelsortalpha_t *p = (pixelsortalpha_t*) ptr;
    if(!p || !frame || !args || !frame->data[3])
        return;

    const unsigned int width = (unsigned int)frame->width;
    const unsigned int height = (unsigned int)frame->height;

    if(width == 0 || height == 0)
        return;

    uint8_t *P[4] = {
        frame->data[0],
        frame->data[1],
        frame->data[2],
        frame->data[3]
    };

    const int mode = psa_clampi(args[0], 0, 2);
    const int pass = psa_clampi(args[1], 0, 3);

    switch(pass) {
        case PSA_PASS_ROWS_COLUMNS:
            psa_rows(p, P, width, height, mode);
            psa_columns(p, P, width, height, mode);
            break;

        case PSA_PASS_COLUMNS_ONLY:
            psa_columns(p, P, width, height, mode);
            break;

        case PSA_PASS_ROWS_ONLY:
            psa_rows(p, P, width, height, mode);
            break;

        case PSA_PASS_COLUMNS_ROWS:
        default:
            psa_columns(p, P, width, height, mode);
            psa_rows(p, P, width, height, mode);
            break;
    }
}
