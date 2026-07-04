/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include "pixelsort.h"
#include <stdint.h>

#ifdef _OPENMP
#include <omp.h>
#define PS_OMP_FOR _Pragma("omp parallel for schedule(static) num_threads(p->n_threads)")
#define PS_THREAD_ID() omp_get_thread_num()
#else
#define PS_OMP_FOR
#define PS_THREAD_ID() 0
#endif

#define PS_PARAMS 6

#define P_MODE             0
#define P_PASS             1
#define P_THRESHOLD        2
#define P_ORDER            3
#define P_ROW_DIRECTION    4
#define P_COLUMN_DIRECTION 5

#define PS_MODE_WHITE   0
#define PS_MODE_BLACK   1
#define PS_MODE_BRIGHT  2
#define PS_MODE_DARK    3

#define PS_PASS_COLUMNS_ROWS 0
#define PS_PASS_ROWS_COLUMNS 1
#define PS_PASS_COLUMNS_ONLY 2
#define PS_PASS_ROWS_ONLY    3

#define PS_ORDER_ASCENDING  0
#define PS_ORDER_DESCENDING 1

#define PS_ROW_LEFT_RIGHT 0
#define PS_ROW_RIGHT_LEFT 1

#define PS_COLUMN_TOP_BOTTOM 0
#define PS_COLUMN_BOTTOM_TOP 1

#define PS_INSERTION_LIMIT 32

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
    int line_cap;
    int n_threads;
    pixelsort_worker_t *workers;
    uint32_t *scratch;
} pixelsort_t;

vj_effect *pixelsort_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = PS_PARAMS;
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

    ve->limits[0][P_MODE] = 0;             ve->limits[1][P_MODE] = 3;             ve->defaults[P_MODE] = PS_MODE_BRIGHT;
    ve->limits[0][P_PASS] = 0;             ve->limits[1][P_PASS] = 3;             ve->defaults[P_PASS] = PS_PASS_COLUMNS_ROWS;
    ve->limits[0][P_THRESHOLD] = 0;        ve->limits[1][P_THRESHOLD] = 255;      ve->defaults[P_THRESHOLD] = 127;
    ve->limits[0][P_ORDER] = 0;            ve->limits[1][P_ORDER] = 1;            ve->defaults[P_ORDER] = PS_ORDER_ASCENDING;
    ve->limits[0][P_ROW_DIRECTION] = 0;    ve->limits[1][P_ROW_DIRECTION] = 1;    ve->defaults[P_ROW_DIRECTION] = PS_ROW_LEFT_RIGHT;
    ve->limits[0][P_COLUMN_DIRECTION] = 0; ve->limits[1][P_COLUMN_DIRECTION] = 1; ve->defaults[P_COLUMN_DIRECTION] = PS_COLUMN_TOP_BOTTOM;

    ve->description = "Asendorf Pixel Sort";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode", "Pass", "Threshold", "Sort Order", "Row Direction", "Column Direction"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "White-ish", "Black-ish", "Bright", "Dark");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_PASS], P_PASS, "Columns then Rows", "Rows then Columns", "Columns Only", "Rows Only");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_ORDER], P_ORDER, "Ascending", "Descending");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_ROW_DIRECTION], P_ROW_DIRECTION, "Left to Right", "Right to Left");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_COLUMN_DIRECTION], P_COLUMN_DIRECTION, "Top to Bottom", "Bottom to Top");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_NO_ZERO_CROSS,                 24,                 232,                8, 34, 320,  1450, 520, 52,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000
    );

    return ve;
}

void pixelsort_free(void *ptr)
{
    pixelsort_t *p = (pixelsort_t *) ptr;

    free(p->scratch);
    free(p->workers);
    free(p);
}

void *pixelsort_malloc(int w, int h)
{
    pixelsort_t *p = (pixelsort_t *) vj_calloc(sizeof(pixelsort_t));

    if(!p)
        return NULL;

    p->line_cap = w > h ? w : h;

#ifdef _OPENMP
    p->n_threads = vje_advise_num_threads(w * h);
#else
    p->n_threads = 1;
#endif

    p->workers = (pixelsort_worker_t *) vj_calloc(sizeof(pixelsort_worker_t) * (size_t)p->n_threads);

    if(!p->workers) {
        free(p);
        return NULL;
    }

    p->scratch = (uint32_t *) vj_calloc(sizeof(uint32_t) * (size_t)p->n_threads * (size_t)p->line_cap * 2u);

    if(!p->scratch) {
        free(p->workers);
        free(p);
        return NULL;
    }

    for(int i = 0; i < p->n_threads; i++) {
        p->workers[i].line = p->scratch + ((size_t)i * (size_t)p->line_cap * 2u);
        p->workers[i].sorted = p->workers[i].line + p->line_cap;
    }

    return (void *) p;
}

static void pixelsort_csort32_range(pixelsort_worker_t *wrk,
                                    uint32_t *restrict input,
                                    uint32_t *restrict output,
                                    unsigned int n,
                                    unsigned int lo_y,
                                    unsigned int hi_y,
                                    int descending)
{
    unsigned int *restrict count = wrk->count;
    unsigned int sum = 0;

    for(unsigned int i = lo_y; i <= hi_y; i++)
        count[i] = 0;

    for(unsigned int i = 0; i < n; i++)
        count[input[i] & 0xff]++;

    if(descending) {
        for(unsigned int i = hi_y + 1; i > lo_y; ) {
            i--;
            sum += count[i];
            count[i] = sum;
        }
    }
    else {
        for(unsigned int i = lo_y; i <= hi_y; i++) {
            sum += count[i];
            count[i] = sum;
        }
    }

    for(unsigned int i = n; i > 0; ) {
        i--;

        const unsigned int key = input[i] & 0xff;

        output[--count[key]] = input[i];
    }
}

static inline void pixelsort_insertion32(uint32_t *restrict a, unsigned int n, int descending)
{
    for(unsigned int i = 1; i < n; i++) {
        const uint32_t v = a[i];
        const uint32_t key = v & 0xff;
        unsigned int j = i;

        if(descending) {
            while(j > 0 && ((a[j - 1] & 0xff) < key)) {
                a[j] = a[j - 1];
                j--;
            }
        }
        else {
            while(j > 0 && ((a[j - 1] & 0xff) > key)) {
                a[j] = a[j - 1];
                j--;
            }
        }

        a[j] = v;
    }
}

static inline int pixelsort_need_sort_x_range(const uint8_t *restrict Y,
                                              unsigned int base,
                                              unsigned int n,
                                              unsigned int *lo_y,
                                              unsigned int *hi_y,
                                              int physical_descending)
{
    unsigned int prev = Y[base];
    unsigned int lo = prev;
    unsigned int hi = prev;
    int need_sort = 0;

    for(unsigned int i = 1; i < n; i++) {
        const unsigned int cy = Y[base + i];

        if(physical_descending) {
            if(cy > prev)
                need_sort = 1;
        }
        else {
            if(cy < prev)
                need_sort = 1;
        }

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

static inline int pixelsort_need_sort_y_range(const uint8_t *restrict Y,
                                              unsigned int pos,
                                              unsigned int width,
                                              unsigned int n,
                                              unsigned int *lo_y,
                                              unsigned int *hi_y,
                                              int physical_descending)
{
    unsigned int prev = Y[pos];
    unsigned int lo = prev;
    unsigned int hi = prev;
    int need_sort = 0;

    for(unsigned int i = 1; i < n; i++) {
        pos += width;

        const unsigned int cy = Y[pos];

        if(physical_descending) {
            if(cy > prev)
                need_sort = 1;
        }
        else {
            if(cy < prev)
                need_sort = 1;
        }

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
    const uint8_t *restrict Y = P[0];
    const uint8_t *restrict U = P[1];
    const uint8_t *restrict V = P[2];
    const unsigned int base = y * width + x;

    for(unsigned int i = 0; i < n; i++) {
        const unsigned int pos = base + i;

        dst[i] = ((uint32_t)Y[pos]) | ((uint32_t)U[pos] << 8) | ((uint32_t)V[pos] << 16);
    }
}

static inline void pixelsort_unpack_x(uint8_t *P[3],
                                      const uint32_t *restrict src,
                                      unsigned int n,
                                      unsigned int x,
                                      unsigned int y,
                                      unsigned int width,
                                      int reverse_axis)
{
    uint8_t *restrict Y = P[0];
    uint8_t *restrict U = P[1];
    uint8_t *restrict V = P[2];
    const unsigned int base = y * width + x;

    for(unsigned int i = 0; i < n; i++) {
        const unsigned int pos = reverse_axis ? base + (n - 1u - i) : base + i;
        const uint32_t v = src[i];

        Y[pos] = (uint8_t)(v & 0xff);
        U[pos] = (uint8_t)((v >> 8) & 0xff);
        V[pos] = (uint8_t)((v >> 16) & 0xff);
    }
}

static inline void pixelsort_pack_y(uint8_t *P[3],
                                    uint32_t *restrict dst,
                                    unsigned int n,
                                    unsigned int x,
                                    unsigned int y,
                                    unsigned int width)
{
    const uint8_t *restrict Y = P[0];
    const uint8_t *restrict U = P[1];
    const uint8_t *restrict V = P[2];
    unsigned int pos = y * width + x;

    for(unsigned int i = 0; i < n; i++, pos += width)
        dst[i] = ((uint32_t)Y[pos]) | ((uint32_t)U[pos] << 8) | ((uint32_t)V[pos] << 16);
}

static inline void pixelsort_unpack_y(uint8_t *P[3],
                                      const uint32_t *restrict src,
                                      unsigned int n,
                                      unsigned int x,
                                      unsigned int y,
                                      unsigned int width,
                                      int reverse_axis)
{
    uint8_t *restrict Y = P[0];
    uint8_t *restrict U = P[1];
    uint8_t *restrict V = P[2];

    for(unsigned int i = 0; i < n; i++) {
        const unsigned int yy = reverse_axis ? y + (n - 1u - i) : y + i;
        const unsigned int pos = yy * width + x;
        const uint32_t v = src[i];

        Y[pos] = (uint8_t)(v & 0xff);
        U[pos] = (uint8_t)((v >> 8) & 0xff);
        V[pos] = (uint8_t)((v >> 16) & 0xff);
    }
}

static inline void pixelsort_sort_x(pixelsort_t *p,
                                    pixelsort_worker_t *wrk,
                                    uint8_t *P[3],
                                    unsigned int width,
                                    unsigned int x0,
                                    unsigned int y,
                                    unsigned int x1,
                                    int descending,
                                    int reverse_axis)
{
    const unsigned int n = x1 - x0;
    unsigned int lo_y;
    unsigned int hi_y;
    const unsigned int base = y * width + x0;
    const int physical_descending = descending ^ reverse_axis;

    if(n < 2)
        return;

    if(!pixelsort_need_sort_x_range(P[0], base, n, &lo_y, &hi_y, physical_descending))
        return;

    pixelsort_pack_x(P, wrk->line, n, x0, y, width);

    if(n <= PS_INSERTION_LIMIT) {
        pixelsort_insertion32(wrk->line, n, descending);
        pixelsort_unpack_x(P, wrk->line, n, x0, y, width, reverse_axis);
    }
    else {
        pixelsort_csort32_range(wrk, wrk->line, wrk->sorted, n, lo_y, hi_y, descending);
        pixelsort_unpack_x(P, wrk->sorted, n, x0, y, width, reverse_axis);
    }

    (void)p;
}

static inline void pixelsort_sort_y(pixelsort_t *p,
                                    pixelsort_worker_t *wrk,
                                    uint8_t *P[3],
                                    unsigned int width,
                                    unsigned int x,
                                    unsigned int y0,
                                    unsigned int y1,
                                    int descending,
                                    int reverse_axis)
{
    const unsigned int n = y1 - y0;
    unsigned int lo_y;
    unsigned int hi_y;
    const unsigned int pos = y0 * width + x;
    const int physical_descending = descending ^ reverse_axis;

    if(n < 2)
        return;

    if(!pixelsort_need_sort_y_range(P[0], pos, width, n, &lo_y, &hi_y, physical_descending))
        return;

    pixelsort_pack_y(P, wrk->line, n, x, y0, width);

    if(n <= PS_INSERTION_LIMIT) {
        pixelsort_insertion32(wrk->line, n, descending);
        pixelsort_unpack_y(P, wrk->line, n, x, y0, width, reverse_axis);
    }
    else {
        pixelsort_csort32_range(wrk, wrk->line, wrk->sorted, n, lo_y, hi_y, descending);
        pixelsort_unpack_y(P, wrk->sorted, n, x, y0, width, reverse_axis);
    }

    (void)p;
}

#define PS_ACTIVE_BRIGHT(pos) (P[0][(pos)] >= threshold)
#define PS_ACTIVE_DARK(pos)   (P[0][(pos)] <= threshold)
#define PS_ACTIVE_WHITE(pos)  (P[0][(pos)] >= threshold && P[1][(pos)] >= PS_WHITE_CHROMA_LO && P[1][(pos)] <= PS_WHITE_CHROMA_HI && P[2][(pos)] >= PS_WHITE_CHROMA_LO && P[2][(pos)] <= PS_WHITE_CHROMA_HI)
#define PS_ACTIVE_BLACK(pos)  (P[0][(pos)] <= threshold && P[1][(pos)] >= PS_BLACK_CHROMA_LO && P[1][(pos)] <= PS_BLACK_CHROMA_HI && P[2][(pos)] >= PS_BLACK_CHROMA_LO && P[2][(pos)] <= PS_BLACK_CHROMA_HI)

#define DEFINE_ROW_RUN(NAME, ACTIVE)                                      \
static inline unsigned int pixelsort_row_run_##NAME(                      \
    pixelsort_t *p,                                                       \
    pixelsort_worker_t *wrk,                                              \
    uint8_t *P[3],                                                        \
    unsigned int width,                                                   \
    unsigned int x,                                                       \
    unsigned int y,                                                       \
    int threshold,                                                        \
    int descending,                                                       \
    int reverse_axis)                                                     \
{                                                                         \
    const unsigned int base = y * width;                                  \
                                                                          \
    while(x < width && !(ACTIVE(base + x)))                               \
        x++;                                                              \
                                                                          \
    if(x >= width)                                                        \
        return width;                                                     \
                                                                          \
    const unsigned int x0 = x;                                            \
                                                                          \
    while(x < width && ACTIVE(base + x))                                  \
        x++;                                                              \
                                                                          \
    pixelsort_sort_x(p, wrk, P, width, x0, y, x, descending, reverse_axis);\
                                                                          \
    return x < width ? x + 1 : x;                                         \
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
    int threshold,                                                        \
    int descending,                                                       \
    int reverse_axis)                                                     \
{                                                                         \
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
    const unsigned int y0 = y;                                            \
                                                                          \
    while(y < height && ACTIVE(pos)) {                                    \
        y++;                                                              \
        pos += width;                                                     \
    }                                                                     \
                                                                          \
    pixelsort_sort_y(p, wrk, P, width, x, y0, y, descending, reverse_axis);\
                                                                          \
    return y < height ? y + 1 : y;                                        \
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
    int threshold,                                                        \
    int descending,                                                       \
    int reverse_axis)                                                     \
{                                                                         \
    PS_OMP_FOR                                                            \
    for(int yi = 0; yi < (int)height; yi++) {                             \
        unsigned int x = 0;                                               \
        const unsigned int y = (unsigned int)yi;                          \
        pixelsort_worker_t *wrk = &p->workers[PS_THREAD_ID()];            \
                                                                          \
        while(x < width) {                                                \
            const unsigned int nx = pixelsort_row_run_##NAME(             \
                p, wrk, P, width, x, y, threshold, descending, reverse_axis);\
                                                                          \
            x = nx <= x ? x + 1 : nx;                                     \
        }                                                                 \
    }                                                                     \
}

#define DEFINE_COLUMNS(NAME)                                              \
static void pixelsort_columns_##NAME(                                     \
    pixelsort_t *p,                                                       \
    uint8_t *P[3],                                                        \
    unsigned int width,                                                   \
    unsigned int height,                                                  \
    int threshold,                                                        \
    int descending,                                                       \
    int reverse_axis)                                                     \
{                                                                         \
    PS_OMP_FOR                                                            \
    for(int xi = 0; xi < (int)width; xi++) {                              \
        unsigned int y = 0;                                               \
        const unsigned int x = (unsigned int)xi;                          \
        pixelsort_worker_t *wrk = &p->workers[PS_THREAD_ID()];            \
                                                                          \
        while(y < height) {                                               \
            const unsigned int ny = pixelsort_column_run_##NAME(          \
                p, wrk, P, width, height, x, y, threshold, descending, reverse_axis);\
                                                                          \
            y = ny <= y ? y + 1 : ny;                                     \
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
                                  int threshold,
                                  int descending,
                                  int reverse_axis)
{
    switch(mode) {
        case PS_MODE_WHITE:
            pixelsort_rows_white(p, P, width, height, threshold, descending, reverse_axis);
            break;
        case PS_MODE_BLACK:
            pixelsort_rows_black(p, P, width, height, threshold, descending, reverse_axis);
            break;
        case PS_MODE_DARK:
            pixelsort_rows_dark(p, P, width, height, threshold, descending, reverse_axis);
            break;
        case PS_MODE_BRIGHT:
        default:
            pixelsort_rows_bright(p, P, width, height, threshold, descending, reverse_axis);
            break;
    }
}

static inline void pixelsort_columns(pixelsort_t *p,
                                     uint8_t *P[3],
                                     unsigned int width,
                                     unsigned int height,
                                     int mode,
                                     int threshold,
                                     int descending,
                                     int reverse_axis)
{
    switch(mode) {
        case PS_MODE_WHITE:
            pixelsort_columns_white(p, P, width, height, threshold, descending, reverse_axis);
            break;
        case PS_MODE_BLACK:
            pixelsort_columns_black(p, P, width, height, threshold, descending, reverse_axis);
            break;
        case PS_MODE_DARK:
            pixelsort_columns_dark(p, P, width, height, threshold, descending, reverse_axis);
            break;
        case PS_MODE_BRIGHT:
        default:
            pixelsort_columns_bright(p, P, width, height, threshold, descending, reverse_axis);
            break;
    }
}

void pixelsort_apply(void *ptr, VJFrame *frame, int *args)
{
    pixelsort_t *p = (pixelsort_t *)ptr;
    uint8_t *P[3] = {
        frame->data[0],
        frame->data[1],
        frame->data[2]
    };

    const unsigned int width = (unsigned int)frame->width;
    const unsigned int height = (unsigned int)frame->height;
    const int mode = args[P_MODE];
    const int pass = args[P_PASS];
    const int threshold = args[P_THRESHOLD];
    const int descending = args[P_ORDER] == PS_ORDER_DESCENDING;
    const int reverse_x = args[P_ROW_DIRECTION] == PS_ROW_RIGHT_LEFT;
    const int reverse_y = args[P_COLUMN_DIRECTION] == PS_COLUMN_BOTTOM_TOP;

    switch(pass) {
        case PS_PASS_ROWS_COLUMNS:
            pixelsort_rows(p, P, width, height, mode, threshold, descending, reverse_x);
            pixelsort_columns(p, P, width, height, mode, threshold, descending, reverse_y);
            break;
        case PS_PASS_COLUMNS_ONLY:
            pixelsort_columns(p, P, width, height, mode, threshold, descending, reverse_y);
            break;
        case PS_PASS_ROWS_ONLY:
            pixelsort_rows(p, P, width, height, mode, threshold, descending, reverse_x);
            break;
        case PS_PASS_COLUMNS_ROWS:
        default:
            pixelsort_columns(p, P, width, height, mode, threshold, descending, reverse_y);
            pixelsort_rows(p, P, width, height, mode, threshold, descending, reverse_x);
            break;
    }
}
