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
#include <limits.h>
#include "gradientfield.h"
#include <math.h>
#include <omp.h>

typedef struct {
    int n_threads;
    int width, height;
    int window;
    float feedback;

    uint8_t *copyY, *copyU, *copyV;
    uint32_t *intY_sum, *intU_sum, *intV_sum;
    uint64_t *intY_sq, *intU_sq, *intV_sq;

    uint8_t *diffX_Y, *diffY_Y;
    uint8_t *diffX_U, *diffY_U;
    uint8_t *diffX_V, *diffY_V;

    uint32_t inv_area_lut[1024];

} gradientfield_t;

static inline int clamp(int v,int lo,int hi){ return (v<lo)?lo:(v>hi?hi:v); }


static int CACHELINE_SIZE = 0;

vj_effect *gradientfield_init(int w, int h)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int)*ve->num_params);

    ve->limits[0][0]=2; ve->limits[1][0]=30; ve->defaults[0]=6;
    ve->limits[0][1]=0; ve->limits[1][1]=255; ve->defaults[1]=0;
    //ve->limits[0][2]=0; ve->limits[1][2]=1; ve->defaults[2]=0;

    ve->description = "Kuwahara Painting";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params,"Window Size","Opacity");

    CACHELINE_SIZE = cpu_get_cacheline_size();

    return ve;
}


void gradientfield_free(void *ptr){
    if(!ptr) return;
    gradientfield_t *s = (gradientfield_t*) ptr;
    free(s->copyY); // all memory is in single block
    free(s);
}

void *gradientfield_malloc(int w, int h) {
    gradientfield_t *s = (gradientfield_t*)vj_malloc(sizeof(gradientfield_t));
    if (!s) return NULL;

    s->n_threads = omp_get_max_threads();
    s->width = w; s->height = h;

    size_t sz_orig = (size_t)w * h;
    size_t stride = w + 1;
    size_t sz_padded = stride * (h + 1);

    size_t total_bytes = (sz_orig * 3 * sizeof(uint8_t)) + 
                         (sz_padded * 3 * sizeof(uint32_t)) + 
                         (sz_padded * 3 * sizeof(uint64_t));
    
    uint8_t *block = (uint8_t*)vj_calloc(total_bytes);
    if (!block) { free(s); return NULL; }

    uint8_t *p = block;
    s->copyY = p; p += sz_orig;
    s->copyU = p; p += sz_orig;
    s->copyV = p; p += sz_orig;

    s->intY_sum = (uint32_t*)p; p += sz_padded * sizeof(uint32_t);
    s->intY_sq  = (uint64_t*)p; p += sz_padded * sizeof(uint64_t);
    s->intU_sum = (uint32_t*)p; p += sz_padded * sizeof(uint32_t);
    s->intU_sq  = (uint64_t*)p; p += sz_padded * sizeof(uint64_t);
    s->intV_sum = (uint32_t*)p; p += sz_padded * sizeof(uint32_t);
    s->intV_sq  = (uint64_t*)p; p += sz_padded * sizeof(uint64_t);

    for(int i=1; i<1024; i++) { 
        s->inv_area_lut[i] = (1 << 16) / i;
    }

    return s;
}

static inline void compute_integral_padded(uint8_t *restrict src, uint32_t *restrict int_sum, uint64_t *restrict int_sq, int w, int h, int n_threads) {
    int stride = w + 1;
    const int num_lines = 8; 
    const int block_bytes = CACHELINE_SIZE * num_lines;
    const int block_elements = block_bytes / sizeof(uint64_t); 

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int y = 0; y < h; y++) {
        uint32_t row_s = 0;
        uint64_t row_sq = 0;
        uint8_t *row_src = &src[y * w];
        uint32_t *row_sum = &int_sum[(y + 1) * stride + 1];
        uint64_t *row_sq_ptr = &int_sq[(y + 1) * stride + 1];

        for (int x = 0; x < w; x++) {
            uint8_t val = row_src[x];
            row_s += val;
            row_sq += (uint64_t)val * val;
            row_sum[x] = row_s;
            row_sq_ptr[x] = row_sq;
        }
    }

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int xx = 1; xx <= w; xx += block_elements) {
        int x_end = (xx + block_elements > w + 1) ? w + 1 : xx + block_elements;
        
        for (int y = 1; y < h; y++) {
            uint32_t *restrict prev_row = &int_sum[y * stride];
            uint32_t *restrict curr_row = &int_sum[(y + 1) * stride];
            uint64_t *restrict prev_sq  = &int_sq[y * stride];
            uint64_t *restrict curr_sq  = &int_sq[(y + 1) * stride];

            for (int x = xx; x < x_end; x++) {
                curr_row[x] += prev_row[x];
                curr_sq[x]  += prev_sq[x];
            }
        }
    }
}

static inline void get_stats_fast(const uint32_t *sum, const uint64_t *sq, int r0, int r1, int x0, int x1, uint32_t *S_out, uint64_t *SS_out) {
    int c0 = x0, c1 = x1 + 1;
    int idx_r1_c1 = r1 + c1;
    int idx_r0_c1 = r0 + c1;
    int idx_r1_c0 = r1 + c0;
    int idx_r0_c0 = r0 + c0;
    
    *S_out  = sum[idx_r1_c1] - sum[idx_r0_c1] - sum[idx_r1_c0] + sum[idx_r0_c0];
    *SS_out = sq[idx_r1_c1] - sq[idx_r0_c1] - sq[idx_r1_c0] + sq[idx_r0_c0];
}

static inline int max_int(int a, int b) {
    return a ^ ((a ^ b) & -(a < b));
}

static inline int min_int(int a, int b) {
    return b ^ ((a ^ b) & -(a < b));
}

static inline uint32_t smooth_weight(uint64_t crit)
{
    const uint32_t K = 1024;
    return (uint32_t)(1ULL << 24) / (uint32_t)(crit + K);
}

static inline void gradientfield_apply_mode0(
    int w, int h, int stride, int a,
    uint8_t *restrict copyY, uint8_t *restrict copyU, uint8_t *restrict copyV,
    uint8_t *restrict outY, uint8_t *restrict outU, uint8_t *restrict outV,
    uint32_t *restrict intY_sum, uint32_t *restrict intY_sq,
    uint32_t *restrict intU_sum, uint32_t *restrict intU_sq,
    uint32_t *restrict intV_sum, uint32_t *restrict intV_sq,
    uint32_t *restrict inv_area_lut,
    uint32_t fb, uint32_t inv_fb,
    int y)
{
    int y0 = (y - a < 0) ? 0 : y - a;
    int y1 = y;
    int y2 = (y + a >= h) ? h - 1 : y + a;
    int y_clip = (y1 + 1 > y2) ? y2 : y1 + 1;

    uint32_t h_t = y1 - y0 + 1;
    uint32_t h_b = y2 - y_clip + 1;

    int r_y0     = y0 * stride;
    int r_y1_p1  = (y1 + 1) * stride;
    int r_y_clip = y_clip * stride;
    int r_y2_p1  = (y2 + 1) * stride;

    for (int x = 0; x < w - 1; x += 2) {
        int x0 = (x - a < 0) ? 0 : x - a;
        int x1 = x;
        int x2 = (x + a >= w) ? w - 1 : x + a;
        int x_clip = (x1 + 1 > x2) ? x2 : x1 + 1;

        uint32_t w_l = x1 - x0 + 1;
        uint32_t w_r = x2 - x_clip + 1;
        uint32_t areas[4] = { w_l * h_t, w_r * h_t, w_l * h_b, w_r * h_b };

        uint32_t win_S, cur_S, win_area;
        uint64_t win_SS, cur_SS, crit, min_crit;
        int win_x0, win_x1, win_r0, win_r1;

        // NW
        get_stats_fast(intY_sum, intY_sq, r_y0, r_y1_p1, x0, x1, &win_S, &win_SS);
        min_crit = ((uint64_t)areas[0] * win_SS) - ((uint64_t)win_S * win_S);
        win_area = areas[0]; win_x0 = x0; win_x1 = x1; win_r0 = r_y0; win_r1 = r_y1_p1;

        // NE
        get_stats_fast(intY_sum, intY_sq, r_y0, r_y1_p1, x_clip, x2, &cur_S, &cur_SS);
        crit = ((uint64_t)areas[1] * cur_SS) - ((uint64_t)cur_S * cur_S);
        if (crit < min_crit) { min_crit = crit; win_S = cur_S; win_area = areas[1]; win_x0 = x_clip; win_x1 = x2; }

        // SW
        get_stats_fast(intY_sum, intY_sq, r_y_clip, r_y2_p1, x0, x1, &cur_S, &cur_SS);
        crit = ((uint64_t)areas[2] * cur_SS) - ((uint64_t)cur_S * cur_S);
        if (crit < min_crit) { min_crit = crit; win_S = cur_S; win_area = areas[2]; win_x0 = x0; win_x1 = x1; win_r0 = r_y_clip; win_r1 = r_y2_p1; }

        // SE
        get_stats_fast(intY_sum, intY_sq, r_y_clip, r_y2_p1, x_clip, x2, &cur_S, &cur_SS);
        crit = ((uint64_t)areas[3] * cur_SS) - ((uint64_t)cur_S * cur_S);
        if (crit < min_crit) { min_crit = crit; win_S = cur_S; win_area = areas[3]; win_x0 = x_clip; win_x1 = x2; win_r0 = r_y_clip; win_r1 = r_y2_p1; }

        uint32_t SU, SV; uint64_t d;
        get_stats_fast(intU_sum, intU_sq, win_r0, win_r1, win_x0, win_x1, &SU, &d);
        get_stats_fast(intV_sum, intV_sq, win_r0, win_r1, win_x0, win_x1, &SV, &d);

        uint32_t inv_a = inv_area_lut[win_area];
        uint32_t mean_y = (win_S * inv_a) >> 16;
        uint32_t mean_u = (SU    * inv_a) >> 16;
        uint32_t mean_v = (SV    * inv_a) >> 16;

        uint8_t val_y = (fb * copyY[y * w + x] + inv_fb * mean_y) >> 8;
        uint8_t val_u = (fb * copyU[y * w + x] + inv_fb * mean_u) >> 8;
        uint8_t val_v = (fb * copyV[y * w + x] + inv_fb * mean_v) >> 8;

        uint16_t pack_y = (uint16_t)val_y * 257; 
        uint16_t pack_u = (uint16_t)val_u * 257;
        uint16_t pack_v = (uint16_t)val_v * 257;

        *(uint16_t*)(outY + x)     = pack_y;
        *(uint16_t*)(outY + x + w) = pack_y;
        *(uint16_t*)(outU + x)     = pack_u;
        *(uint16_t*)(outU + x + w) = pack_u;
        *(uint16_t*)(outV + x)     = pack_v;
        *(uint16_t*)(outV + x + w) = pack_v;
    }
}

void gradientfield_apply(void *ptr, VJFrame *frame, int *args) {
    gradientfield_t *s = (gradientfield_t*)ptr;
    
    int w = s->width, h = s->height, stride = w + 1;
    int a = args[0];
    int mode = args[2];

    uint32_t fb = (uint32_t)clamp(args[1], 0, 255);
    uint32_t inv_fb = 255 - fb;

    uint8_t *restrict data[3] = { frame->data[0], frame->data[1], frame->data[2] };
    uint8_t *restrict copy[3] = { s->copyY, s->copyU, s->copyV };

    uint32_t *restrict inv_area_lut = s->inv_area_lut;

    for (int i = 0; i < 3; i++) 
        veejay_memcpy(copy[i], data[i], w * h);

    compute_integral_padded(copy[0], s->intY_sum, s->intY_sq, w, h, s->n_threads);
    compute_integral_padded(copy[1], s->intU_sum, s->intU_sq, w, h, s->n_threads);
    compute_integral_padded(copy[2], s->intV_sum, s->intV_sq, w, h, s->n_threads);
    
    switch(mode ) {
        case 0:
    #pragma omp parallel for schedule(static) num_threads(s->n_threads)
            for (int y = 0; y < h - 1; y += 2) {
                gradientfield_apply_mode0(
                    w, h, stride, a,
                    s->copyY, s->copyU, s->copyV,
                    data[0] + y*w, data[1] + y*w, data[2] + y*w,
                    s->intY_sum, s->intY_sq,
                    s->intU_sum, s->intU_sq,
                    s->intV_sum, s->intV_sq,
                    s->inv_area_lut,
                    fb, inv_fb, y
                );
            }
            break;
    }
}