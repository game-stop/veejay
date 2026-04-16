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
#include "dotillism.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#define TRIG_SAMPLES 256
#define MAX_R 65

static inline void get_bilinear_color(uint8_t *plane, float x, float y, int w, int h, float *out) { // FIXME 
    int x1 = (int)x;
    int y1 = (int)y;
    int x2 = (x1 + 1 < w) ? x1 + 1 : x1;
    int y2 = (y1 + 1 < h) ? y1 + 1 : y1;

    float dx = x - x1;
    float dy = y - y1;

    float p1 = plane[y1 * w + x1];
    float p2 = plane[y1 * w + x2];
    float p3 = plane[y2 * w + x1];
    float p4 = plane[y2 * w + x2];

    *out = p1 * (1.0f - dx) * (1.0f - dy) +
           p2 * dx * (1.0f - dy) +
           p3 * (1.0f - dx) * dy +
           p4 * dx * dy;
}

static uint32_t fast_rand(uint32_t *seed) {
    *seed = *seed * 1664525 + 1013904223;
    return *seed;
}

static inline uint32_t hash_coord(float x, float y, uint32_t k) {
    union { float f; uint32_t u; } ux, uy;
    ux.f = x; uy.f = y;
    uint32_t h = ux.u * 374761393u + uy.u * 668265263u + k * 1013904223u + 1337u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline uint8_t clamp_u8(double v) {
    if (v <= 0.0)   return 0;
    if (v >= 255.0) return 255;
    return (uint8_t)v;
}

typedef struct {
    float x, y, radius;
    uint8_t y_col, u_col, v_col;
} Point;

typedef struct {
    uint8_t *temp_y, *temp_u, *temp_v, *edge_map;
    Point *points;
    int *active_list, *grid;
    int max_points, grid_w, grid_h, n_threads;
    float last_hue;
    int last_levels;
    uint32_t frame_ticks;
    float s_table[TRIG_SAMPLES], c_table[TRIG_SAMPLES];
    uint8_t span_lut[MAX_R][MAX_R];
    uint8_t rot_u[256][256], rot_v[256][256], q_y[256];
} dotillism_t;

vj_effect *dotillism_init(int w, int h) {
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 4;  ve->limits[1][0] = 64;  ve->defaults[0] = 16;
    ve->limits[0][1] = 1;  ve->limits[1][1] = 32;  ve->defaults[1] = 4;
    ve->limits[0][2] = 2;  ve->limits[1][2] = 256; ve->defaults[2] = 16;
    ve->limits[0][3] = 0;  ve->limits[1][3] = 100; ve->defaults[3] = 75;
    ve->limits[0][4] = 0;  ve->limits[1][4] = 1;   ve->defaults[4] = 1;
    ve->limits[0][5] = 0;  ve->limits[1][5] = 360; ve->defaults[5] = 0;
    ve->limits[0][6] = 0;  ve->limits[1][6] = 100; ve->defaults[6] = 0;

    ve->description = "Dotillism (wip)";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Max Radius", "Min Radius", "Color Levels", "Edge Weight", "Clear Background", "Hue Rotate", "Jitter");
    return ve;
}

void dotillism_free(void *ptr) {
    dotillism_t *d = (dotillism_t*) ptr;
    free(d->points);
    free(d->active_list);
    free(d->grid);
    free(d->temp_y);
    free(d->temp_u);
    free(d->temp_v);
    free(d->edge_map);
    free(d);
}

static void rebuild_luts(dotillism_t *d, float hue_rot, int levels) {
    float rad = hue_rot * M_PI / 180.0f;
    float co = cos(rad), si = sin(rad);
    int q = 256 / (levels > 0 ? levels : 1);
    if (q < 1) q = 1;

    for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 256; j++) {
            float du = i - 128, dv = j - 128;
            int nu = (int)(du * co - dv * si + 128.5f);
            int nv = (int)(du * si + dv * co + 128.5f);
            d->rot_u[i][j] = (uint8_t)(nu - (nu % q));
            d->rot_v[i][j] = (uint8_t)(nv - (nv % q));
        }
        d->q_y[i] = (uint8_t)(i - (i % q));
    }
}

void *dotillism_malloc(int w, int h) {
    dotillism_t *d = (dotillism_t*) vj_calloc(sizeof(dotillism_t));
    d->max_points = (w * h) / 8;
    if (d->max_points > 400000) d->max_points = 400000;

    d->points = (Point*) vj_malloc(d->max_points * sizeof(Point));
    d->active_list = (int*) vj_malloc(d->max_points * sizeof(int));
    d->temp_y = (uint8_t*) vj_malloc(w * h);
    d->temp_u = (uint8_t*) vj_malloc(w * h);
    d->temp_v = (uint8_t*) vj_malloc(w * h); 
    d->edge_map = (uint8_t*) vj_malloc(w * h);
    d->n_threads = vje_advise_num_threads(w * h);

    for(int i=0; i<TRIG_SAMPLES; i++) {
        d->s_table[i] = sinf(i * 2.0f * M_PI / TRIG_SAMPLES);
        d->c_table[i] = cosf(i * 2.0f * M_PI / TRIG_SAMPLES);
    }

    for(int r=0; r<MAX_R; r++) {
        for(int dy=0; dy<MAX_R; dy++) {
            if (dy > r) d->span_lut[r][dy] = 0;
            else d->span_lut[r][dy] = (uint8_t)sqrtf((float)(r*r - dy*dy));
        }
    }

    return (void*) d;
}

static inline void draw_circle_fast(uint8_t *Y, uint8_t *U, uint8_t *V,
                                    const Point *p,
                                    int w, int h,
                                    dotillism_t *d)
{
    int px = (int)p->x;
    int py = (int)p->y;
    int pr = (int)p->radius;

    int y_min = py - pr; if (y_min < 0) y_min = 0;
    int y_max = py + pr; if (y_max >= h) y_max = h - 1;

    const uint8_t yc = p->y_col;
    const uint8_t uc = p->u_col;
    const uint8_t vc = p->v_col;

    for (int y = y_min; y <= y_max; y++) {
        int dy = y - py; if (dy < 0) dy = -dy;

        int width = d->span_lut[pr][dy];
        int x_min = px - width; if (x_min < 0) x_min = 0;
        int x_max = px + width; if (x_max >= w) x_max = w - 1;

        uint8_t *rowY = Y + y * w + x_min;
        uint8_t *rowU = U + y * w + x_min;
        uint8_t *rowV = V + y * w + x_min;

        int len = x_max - x_min + 1;

        for (int i = 0; i < len; i++) {
            rowY[i] = yc;
            rowU[i] = uc;
            rowV[i] = vc;
        }
    }
}

static inline void get_integrated_color(dotillism_t *d,
                                        float nx, float ny,
                                        float radius,
                                        int w, int h,
                                        uint8_t *oy,
                                        uint8_t *ou,
                                        uint8_t *ov)
{
    float step = radius * 0.4f;
    float ox[5] = {0, -step, step, -step, step};
    float oyf[5] = {0, -step, -step, step, step};
    int sy = 0, su = 0, sv = 0;

    for (int i = 0; i < 5; i++) {
        int px = (int)(nx + ox[i]);
        int py = (int)(ny + oyf[i]);

        if (px < 0) px = 0;
        else if (px >= w) px = w - 1;

        if (py < 0) py = 0;
        else if (py >= h) py = h - 1;

        uint8_t *rowY = d->temp_y + py * w;
        uint8_t *rowU = d->temp_u + py * w;
        uint8_t *rowV = d->temp_v + py * w;

        sy += rowY[px];
        su += rowU[px];
        sv += rowV[px];
    }

    *oy = sy / 5;
    *ou = su / 5;
    *ov = sv / 5;
}

void dotillism_apply(void *ptr, VJFrame *frame, int *args) {
    dotillism_t *d = (dotillism_t*) ptr;

    d->frame_ticks++;
    uint32_t ft = d->frame_ticks;
    int jitter_amt = args[6];

    int r_max = args[0], r_min = args[1], levels = args[2], clear_bg = args[4];
    float edge_weight = args[3] / 100.0f, hue_rot = (float)args[5];
    int w = frame->width, h = frame->height;
    uint8_t *Y = frame->data[0], *U = frame->data[1], *V = frame->data[2];

    #pragma omp parallel for num_threads(d->n_threads) schedule(static)
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int off = y * w + x;
            #define V_GLUE(p) (p[off-w]*2 + p[off]*4 + p[off+w]*2) / 8
            d->temp_y[off] = V_GLUE(Y);
            d->temp_u[off] = V_GLUE(U);
            d->temp_v[off] = V_GLUE(V);
            #undef V_GLUE
        }
    }

    #pragma omp parallel for num_threads(d->n_threads) schedule(static)
    for (int y = 1; y < h - 1; y++) {
        uint8_t *row0 = d->temp_y + (y - 1) * w;
        uint8_t *row1 = d->temp_y + y * w;
        uint8_t *row2 = d->temp_y + (y + 1) * w;
        uint8_t *out = d->edge_map + y * w;

        for (int x = 1; x < w - 1; x++) {
            int xm1 = x - 1;
            int xp1 = x + 1;

            int gx = -row0[xm1] + row0[xp1]
                     -2 * row1[xm1] + 2 * row1[xp1]
                     -row2[xm1] + row2[xp1];

            int gy = -row0[xm1] - 2 * row0[x] - row0[xp1]
                     +row2[xm1] + 2 * row2[x] + row2[xp1];

            int mag = abs(gx) + abs(gy);
            out[x] = (mag > 255) ? 255 : mag;
        }
    }

    float overlap = 0.92f;
    float cell_size = (r_min * 1.414f * overlap);
    int gw = (int)ceil(w / cell_size), gh = (int)ceil(h / cell_size);

    if (!d->grid || d->grid_w != gw || d->grid_h != gh) {
        if(d->grid) free(d->grid);
        d->grid = (int*)vj_malloc(gw * gh * sizeof(int));
        d->grid_w = gw; d->grid_h = gh;
    }
    veejay_memset(d->grid, -1, gw * gh * sizeof(int));

    int active_head = 0;
    int num_p = 0, num_a = 0;

    if(d->last_hue != hue_rot || d->last_levels != levels) {
        rebuild_luts(d, hue_rot, levels);
        d->last_hue = hue_rot;
        d->last_levels = levels;
    }

    #define ADD_P(nx, ny, nr) { \
        uint8_t ay, au, av; \
        get_integrated_color(d, nx, ny, nr, w, h, &ay, &au, &av); \
        d->points[num_p] = (Point){nx, ny, nr, d->q_y[ay], d->rot_u[au][av], d->rot_v[au][av]}; \
        d->grid[(int)(ny/cell_size)*gw + (int)(nx/cell_size)] = num_p; \
        d->active_list[num_a++] = num_p++; \
    }

    ADD_P(w/2.0f, h/2.0f, (float)r_max);

    while (active_head < num_a && num_p < d->max_points) {
        int ridx = active_head;
        Point p = d->points[d->active_list[ridx]];
        int found = 0;

        for (int k = 0; k < 30; k++) {
            uint32_t r = hash_coord(p.x, p.y, k);

            if (jitter_amt > 0) {
                uint32_t jr = hash_coord(p.x, p.y, ft + k);
                if ((jr % 100) < (uint32_t)jitter_amt) {
                    r ^= jr;
                }
            }

            float base_r = p.radius * 2.0f * overlap;
            int tidx = r & 255;
            float r01 = ((r >> 8) & 0xFFFFFF) * (1.0f / 16777215.0f);
            float dist = base_r * (1.0f + r01);

            float nx = p.x + dist * d->c_table[tidx];
            float ny = p.y + dist * d->s_table[tidx];

            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;

            float ev = d->edge_map[(int)ny*w+(int)nx] / 255.0f;
            float raw_nr = r_max - (ev * edge_weight * (r_max - r_min));
            raw_nr = fmaxf((float)r_min, raw_nr);
            int nr = (int)(raw_nr + 0.5f);

            int search_dist = (int)ceilf(((nr + r_max) * overlap) / cell_size);
            int gx = (int)(nx/cell_size), gy = (int)(ny/cell_size), valid = 1;
            
            for (int j = gy - search_dist; j <= gy + search_dist && valid; j++) {
                if (j < 0 || j >= gh) continue;
                for (int i = gx - search_dist; i <= gx + search_dist; i++) {
                    if (i < 0 || i >= gw) continue;
                    int nid = d->grid[j*gw+i];
                    if (nid != -1) {
                        float dx = d->points[nid].x - nx, dy = d->points[nid].y - ny;
                        float md = (nr + d->points[nid].radius) * overlap;
                        float d2 = dx*dx + dy*dy;
                        float m2 = md*md;
                        if (d2 < m2 - 0.25f) {
                            valid = 0;
                            break;
                        }
                    }
                }
            }
            if (valid) { ADD_P(nx, ny, nr); found = 1; break; }
        }
        if (!found) {
            active_head++;
        }
    }

    #undef ADD_P

    if (clear_bg) {
        veejay_memset(Y, pixel_Y_lo_, w*h);
        veejay_memset(U, 128, w*h);
        veejay_memset(V, 128, w*h);
    }

    #pragma omp parallel for num_threads(d->n_threads) schedule(static)
    for (int i = 0; i < num_p; i++) {
        draw_circle_fast(Y, U, V, &d->points[i], w, h, d);
    }
}