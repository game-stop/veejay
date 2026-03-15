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
#include <limits.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>
#include <omp.h>
#include "fragmenttv.h"

#define MAX_FRAGMENTS 16384

#define MAX_TILES 4096
#define RANDBUF_SIZE 512

typedef struct {
    int dx, dy;
    int size;
    int srcX, srcYpos;
    float alphaX[MAX_FRAGMENTS];
    float alphaY[MAX_FRAGMENTS];
} tile_info_t;

typedef struct {
    uint8_t *buf[3];
    int count;
    int *tile_pos_x;
    int *tile_pos_y;
    int *tile_size;
    uint8_t *mask;
    int n_threads;
    uint32_t randbuf[RANDBUF_SIZE];
    tile_info_t tiles[MAX_TILES];
    int tile_count;
    int first_frame;
    int prev_tileSize;
    int prev_variation;
    int prev_jitter;
    int prev_scatter;
    int prev_density;
} fragmenttv_t;



vj_effect *fragmenttv_init(int w, int h)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));

    ve->num_params = 7;

    ve->defaults = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 28;
    ve->defaults[1] = 0;
    ve->defaults[2] = 8;
    ve->defaults[3] = 4;
    ve->defaults[4] = 0;
    ve->defaults[5] = 1;
    ve->defaults[6] = 100;

    ve->limits[0][0] = 8;
    ve->limits[1][0] = 128;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 128;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 256;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 64;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 500;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 1;

    ve->limits[0][6] = 0;
    ve->limits[1][6] = 100;

    ve->description = "Stable Fragment TV";

    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description =
        vje_build_param_list(ve->num_params,
            "Tile Size",
            "Size Variation",
            "Scatter",
            "Grid Jitter",
            "Interval",
            "Black Background",
            "Tile Density");

    ve->hints = vje_init_value_hint_list(ve->num_params);

    return ve;
}

static void init_rand_lut(fragmenttv_t *m, uint32_t seed)
{
    uint32_t s = seed;
    for(int i=0;i<RANDBUF_SIZE;i++){
        s = s * 1664525u + 1013904223u;
        m->randbuf[i] = s;
    }
}

static inline int randbuf_val(fragmenttv_t *m, int pos, int min, int max) {
    return min + (int)(m->randbuf[pos % RANDBUF_SIZE] % (max - min + 1));
}

void *fragmenttv_malloc(int w, int h)
{
    size_t buf_size   = w * h;
    size_t tile_size  = sizeof(int) * MAX_TILES;
    size_t total_size = sizeof(fragmenttv_t) + 3 * buf_size + buf_size + 3 * tile_size;

    uint8_t *block = (uint8_t*) vj_malloc(total_size);
    if (!block) return NULL;

    fragmenttv_t *m = (fragmenttv_t*) block;
    uint8_t *ptr = block + sizeof(fragmenttv_t);

    m->buf[0] = ptr; ptr += buf_size;
    m->buf[1] = ptr; ptr += buf_size;
    m->buf[2] = ptr; ptr += buf_size;
    m->mask   = ptr; ptr += buf_size;

    m->tile_pos_x = (int*) ptr; ptr += tile_size;
    m->tile_pos_y = (int*) ptr; ptr += tile_size;
    m->tile_size  = (int*) ptr; ptr += tile_size;

    veejay_memset(m->tile_pos_x, 0, tile_size);
    veejay_memset(m->tile_pos_y, 0, tile_size);
    veejay_memset(m->tile_size,  0, tile_size);

    m->n_threads = vje_advise_num_threads(w*h);
    m->count = 0;
    m->first_frame = 1;
    init_rand_lut(m, 12345); // initial seed

    return m;
}


void fragmenttv_free(void *ptr)
{
    if(ptr)
        free(ptr);
}

static inline uint32_t lcg_rand(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static inline int clampi(int v,int min,int max) {
    if(v < min) return min;
    if(v > max) return max;
    return v;
}

static inline void blendPixel(uint8_t *dstY, uint8_t *dstU, uint8_t *dstV,
                              int idx, float alpha)
{
    if(alpha <= 0.0f) return;
    dstY[idx] = (uint8_t)(dstY[idx]*(1.0f - alpha));
    dstU[idx] = (uint8_t)(dstU[idx]*(1.0f - alpha) + 128*alpha);
    dstV[idx] = (uint8_t)(dstV[idx]*(1.0f - alpha) + 128*alpha);
}

static inline int randbuf_next(fragmenttv_t *m, int *rpos, int min, int max){
    uint32_t val = m->randbuf[*rpos];
    (*rpos)++;
    if(*rpos>=RANDBUF_SIZE) *rpos=0;
    return min + (int)(val % (max - min + 1));
}

static void drawTileBorders_parallel(fragmenttv_t *m,
                                     uint8_t *dstY, uint8_t *dstU, uint8_t *dstV,
                                     int width, int height,
                                     int requested_border,
                                     uint8_t *mask)
{
    const float alpha_factor = 1.0f / 3.0f; // for edges

    #pragma omp parallel for schedule(dynamic) num_threads(m->n_threads)
    for(int t = 0; t < MAX_TILES; t++){
        int size = m->tile_size[t];
        if(size <= 0) continue;

        int dx = m->tile_pos_x[t];
        int dy = m->tile_pos_y[t];
        int border = clampi(requested_border, 1, size/2);

        float alphaBorder[3];
        for(int i=0;i<3;i++) alphaBorder[i] = (i+1)*alpha_factor;

        for(int y=0; y<border; y++){
            float ay = (y<3) ? alphaBorder[y] : 1.0f;
            int yy_top = dy + y;
            int yy_bot = dy + size - 1 - y;

            for(int x=0; x<size; x++){
                float ax;
                if(x < 3) ax = alphaBorder[x];
                else if(x >= size-3) ax = alphaBorder[size-1-x];
                else ax = 1.0f;
                float alpha = ax * ay;

                int xx = dx + x;

                if(xx < 0 || xx >= width) continue;

                if(yy_top >= 0 && yy_top < height){
                    int idx = yy_top*width + xx;
                    if(mask[idx]==0) blendPixel(dstY,dstU,dstV, idx, alpha);
                }

                if(yy_bot >= 0 && yy_bot < height){
                    int idx = yy_bot*width + xx;
                    if(mask[idx]==0) blendPixel(dstY,dstU,dstV, idx, alpha);
                }
            }
        }

        for(int x=0; x<border; x++){
            float ax = (x<3) ? alphaBorder[x] : 1.0f;
            int xx_left = dx + x;
            int xx_right = dx + size - 1 - x;

            for(int y=0; y<size; y++){
                float ay;
                if(y<3) ay = alphaBorder[y];
                else if(y >= size-3) ay = alphaBorder[size-1-y];
                else ay = 1.0f;
                float alpha = ax * ay;

                int yy = dy + y;
                if(yy < 0 || yy >= height) continue;

                if(xx_left >=0 && xx_left < width){
                    int idx = yy*width + xx_left;
                    if(mask[idx]==0) blendPixel(dstY,dstU,dstV, idx, alpha);
                }

                if(xx_right >=0 && xx_right < width){
                    int idx = yy*width + xx_right;
                    if(mask[idx]==0) blendPixel(dstY,dstU,dstV, idx, alpha);
                }
            }
        }
    }
}


static void generateTiles(fragmenttv_t *m,
                          int width, int height,
                          int tileSize, int variation, int jitter, int scatter,
                          int density)
{
    int rpos = 0;
    m->tile_count = 0;

    const float alpha_factor = 0.6f / 3.0f;
    const float alpha_base   = 0.4f;

    for(int gy = 0; gy < height; gy += tileSize){
        for(int gx = 0; gx < width; gx += tileSize){
            if(randbuf_next(m, &rpos, 0, 99) > density) continue;

            int size = tileSize + (variation > 0 ? randbuf_next(m, &rpos, 0, variation) : 0);
            int dx = gx + randbuf_next(m, &rpos, -jitter, jitter);
            int dy = gy + randbuf_next(m, &rpos, -jitter, jitter);

            if(dx < 0) { size += dx; dx = 0; }
            if(dy < 0) { size += dy; dy = 0; }
            if(dx + size > width) size = width - dx;
            if(dy + size > height) size = height - dy;
            if(size <= 0) continue;

            if(m->tile_count >= MAX_TILES) continue;

            tile_info_t *t = &m->tiles[m->tile_count++];
            t->dx = dx;
            t->dy = dy;
            t->size = size;
            t->srcX = clampi(gx + randbuf_next(m, &rpos, -scatter, scatter), 0, width - 1);
            t->srcYpos = clampi(gy + randbuf_next(m, &rpos, -scatter, scatter), 0, height - 1);

            for(int y=0; y<size; y++){
                if(y<3) t->alphaY[y] = (y+1)*alpha_factor + alpha_base;
                else if(y >= size-3) t->alphaY[y] = (size - y)*alpha_factor + alpha_base;
                else t->alphaY[y] = 1.0f;
            }
            for(int x=0; x<size; x++){
                if(x<3) t->alphaX[x] = (x+1)*alpha_factor + alpha_base;
                else if(x >= size-3) t->alphaX[x] = (size - x)*alpha_factor + alpha_base;
                else t->alphaX[x] = 1.0f;
            }
        }
    }
}

static void drawTiles_parallel(fragmenttv_t *m,
                               uint8_t *dstY, uint8_t *dstU, uint8_t *dstV,
                               uint8_t *srcY, uint8_t *srcU, uint8_t *srcV,
                               int width)
{
    #pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for(int t = 0; t < m->tile_count; t++){
        tile_info_t *tile = &m->tiles[t];

        for(int y=0; y<tile->size; y++){
            int yy = tile->dy + y;
            int sy = tile->srcYpos + y;
            int drow = yy * width;
            int srow = sy * width;
            float ay = tile->alphaY[y];

            for(int x=0; x<tile->size; x++){
                int xx = tile->dx + x;
                int sx = tile->srcX + x;
                int di = drow + xx;
                int si = srow + sx;

                float alpha = tile->alphaX[x] * ay;

                dstY[di] = (uint8_t)(dstY[di]*(1.0f - alpha) + srcY[si]*alpha);
                dstU[di] = (uint8_t)(dstU[di]*(1.0f - alpha) + srcU[si]*alpha);
                dstV[di] = (uint8_t)(dstV[di]*(1.0f - alpha) + srcV[si]*alpha);
            }
        }
    }
}

void fragmenttv_apply(void *ptr, VJFrame *frame, int *args)
{
    fragmenttv_t *m = (fragmenttv_t*) ptr;

    const int width  = frame->width;
    const int height = frame->height;

    int tileSize  = args[0];
    int variation = args[1];
    int scatter   = args[2];
    int jitter    = args[3];
    int interval  = args[4];
    int black_bg  = args[5];
    int density   = args[6];

    uint8_t *srcY = frame->data[0];
    uint8_t *srcU = frame->data[1];
    uint8_t *srcV = frame->data[2];

    uint8_t *outY, *outU, *outV;
    if(vje_setup_local_bufs(1, frame, &outY, &outU, &outV, NULL)==0){
        veejay_memcpy(m->buf[0], srcY, frame->len);
        veejay_memcpy(m->buf[1], srcU, frame->len);
        veejay_memcpy(m->buf[2], srcV, frame->len);
        srcY = m->buf[0];
        srcU = m->buf[1];
        srcV = m->buf[2];
    } else {
        veejay_memcpy(outY, srcY, frame->len);
        veejay_memcpy(outU, srcU, frame->len);
        veejay_memcpy(outV, srcV, frame->len);
    }

    if(black_bg){
        veejay_memset(outY, 0, frame->len);
        veejay_memset(outU, 128, frame->len);
        veejay_memset(outV, 128, frame->len);
    }

    veejay_memset(m->mask, 0, width*height);

    if(m->first_frame ||
       (interval>0 && (m->count % interval)==0) ||
       tileSize  != m->prev_tileSize ||
       variation != m->prev_variation ||
       jitter    != m->prev_jitter ||
       scatter   != m->prev_scatter ||
       density   != m->prev_density)
    {
        generateTiles(m, width, height, tileSize, variation, jitter, scatter, density);

        m->prev_tileSize  = tileSize;
        m->prev_variation = variation;
        m->prev_jitter    = jitter;
        m->prev_scatter   = scatter;
        m->prev_density   = density;
    }


    drawTiles_parallel(m, outY, outU, outV, srcY, srcU, srcV, width);

    drawTileBorders_parallel(m, outY, outU, outV, width, height, 1, m->mask);

    if(interval>0)
        m->count = (m->count + 1) % interval;

    m->first_frame = 0;
}
