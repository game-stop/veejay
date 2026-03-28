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
#include <math.h>

#define TWO_PI 6.28318530718f
#define ONE_PI2 1.57079632679f

#define LUT_SIZE 4096
#define LUT_MASK (LUT_SIZE - 1)
#define LUT_DIVISOR (LUT_SIZE / TWO_PI)

static inline float wrap_angle(float a) {
    if (a < 0.0f) a += TWO_PI;
    else if (a >= TWO_PI) a -= TWO_PI;
    return a;
}

typedef struct {
    int *map;
    float *lut;
    float *atan_lut;
    float *sqrt_lut;
    float *cos_lut;
    float *sin_lut;
    float angle;
    int n_threads;
    int last_args[8];
    uint8_t *buf[3];
} fractalkaleido_t;

vj_effect *fractalkaleido_init(int w, int h)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));

    ve->num_params = 8;

    ve->defaults = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    // segments
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 32;
    ve->defaults[0] = 6;

    // rotation
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 360;
    ve->defaults[1] = 0;

    // scale
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 1000;
    ve->defaults[2] = 100;

    // offset X
    ve->limits[0][3] = -200;
    ve->limits[1][3] = 200;
    ve->defaults[3] = 0;

    // offset Y
    ve->limits[0][4] = -200;
    ve->limits[1][4] = 200;
    ve->defaults[4] = 0;

    // mirror
    ve->limits[0][5] = 0;
    ve->limits[1][5] = 1;
    ve->defaults[5] = 1;

    // rotation speed
    ve->limits[0][6] = 0;
    ve->limits[1][6] = 100;
    ve->defaults[6] = 0;

    // twist
    ve->limits[0][7] = -50;
    ve->limits[1][7] = 50;
    ve->defaults[7]  = 0;

    ve->description = "Fractal Kaleido";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Segments",
        "Rotation",
        "Scale",
        "Offset X",
        "Offset Y",
        "Mirror Tiles",
        "Rotation Speed",
        "Twist"
    );

    return ve;
}

static void init_sqrtatan_lut(fractalkaleido_t *f, int w, int h, int cx, int cy, int n_threads)
{
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        const float dy = (float)(y - cy);
        const int row = y * w;
        
        for(int x = 0; x < w; x++) {
            const float dx = (float)(x - cx);
            const int idx = row + x;

            f->sqrt_lut[idx] = sqrtf(dx * dx + dy * dy);

            float angle = atan2f(dy, dx);
            if (angle < 0.0f) angle += TWO_PI;
            
            f->atan_lut[idx] = angle;
        }
    }
}

static void init_sin_cos_lut(fractalkaleido_t *f, int n_threads)
{
    const float step = TWO_PI / LUT_SIZE;
    for(int i = 0; i < LUT_SIZE; i++) {
        float a = i * step;
        f->sin_lut[i] = sinf(a);
        f->cos_lut[i] = cosf(a);
    }
}

void *fractalkaleido_malloc(int w, int h)
{
    fractalkaleido_t *s = (fractalkaleido_t*) vj_calloc(sizeof(fractalkaleido_t));
    if(!s) return NULL;

    size_t total = (w*h*2) + (LUT_SIZE*2);

    s->lut = (float*) vj_malloc(sizeof(float) * total);
    if(!s->lut) {
        free(s);
        return NULL;
    }
    s->buf[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s->lut);
        free(s);
        return NULL;
    }
    s->map = (int*) vj_calloc(sizeof(int) * w * h);
    if(!s->map) {
        free(s->lut);
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->atan_lut = s->lut;
    s->sqrt_lut = s->atan_lut + (w*h);
    s->cos_lut = s->sqrt_lut + (w*h);
    s->sin_lut = s->cos_lut + LUT_SIZE;
    s->buf[1] = s->buf[0] + w * h;
    s->buf[2] = s->buf[1] + w * h;

    s->n_threads = vje_advise_num_parallel_threads(w*h, vj_task_get_workers());

    init_sqrtatan_lut(s, w, h, w/2, h/2, s->n_threads);
    init_sin_cos_lut(s, s->n_threads);

    return (void*) s;
}

void fractalkaleido_free(void *ptr)
{
    fractalkaleido_t *s = (fractalkaleido_t*) ptr;
    if(s) {
        free(s->lut);
        free(s->buf[0]);
        free(s->map);
        free(s);
    }
}


static void fractalkaleido_apply1(void *ptr, VJFrame *frame, int *args, float base_angle)
{
    fractalkaleido_t *s = (fractalkaleido_t*) ptr;

    const int w = frame->out_width;
    const int h = frame->out_height;

    const int hw = w >> 1;
    const int hh = h >> 1;
    const int cx = w >> 1;
    const int cy = h >> 1;

    const int segments = args[0];
    const float seg_angle = TWO_PI / segments;

    const float scale = args[2] * 0.01f;
    const float offx = args[3] * 0.01f;
    const float offy = args[4] * 0.01f;
    const int mirror = args[5];

    const float *cos_lut = s->cos_lut;
    const float *sin_lut = s->sin_lut;

    const float inv_w = 1.0f / (float)w;
    const float inv_h = 1.0f / (float)h;
    const float inv_seg = 1.0f / seg_angle;

    const float twist = args[7] * 0.0005f;
    const float offxw = offx * w;
    const float offyh = offy * h;
    const float half_seg = seg_angle * 0.5f;

    int *map = s->map;

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y <= hh; y++)
    {
        const int row_offset = y * w;

        const float *restrict atan_row = s->atan_lut + row_offset;
        const float *restrict sqrt_row = s->sqrt_lut + row_offset;

        for(int x = 0; x <= hw; x++)
        {
            float theta = atan_row[x];
            float r     = sqrt_row[x];

            theta += r * twist;

            float dither = ((x ^ y) & 1) ? 0.001f : -0.001f;
            float a = theta + base_angle + dither;

            if (a < 0.0f)
                a += TWO_PI * 100.0f;

            float f = a * inv_seg;
            int seg_i = (int)f;
            float seg = (f - seg_i) * seg_angle;

            float t = seg - half_seg;
            seg = (t < 0.0f) ? -t : t;
            seg = seg_angle - seg;

            int lut = (int)(seg * LUT_DIVISOR) & LUT_MASK;

            float nx = r * cos_lut[lut];
            float ny = r * sin_lut[lut];

            nx = nx * scale + offxw;
            ny = ny * scale + offyh;

            float tx = nx * inv_w;
            float ty = ny * inv_h;

            int tilex = (int)tx;
            int tiley = (int)ty;

            float fx = tx - (float)tilex;
            float fy = ty - (float)tiley;

            if (tx < 0.0f && fx != 0.0f) { tilex--; fx += 1.0f; }
            if (ty < 0.0f && fy != 0.0f) { tiley--; fy += 1.0f; }

            if(mirror)
            {
                fx = (tilex & 1) ? (1.0f - fx) : fx;
                fy = (tiley & 1) ? (1.0f - fy) : fy;
            }

            int sx = (int)(fx * (float)(w - 1) + 0.5f);
            int sy = (int)(fy * (float)(h - 1) + 0.5f);

            if ((unsigned)sx >= (unsigned)w) sx = 0;
            if ((unsigned)sy >= (unsigned)h) sy = 0;

            int src_idx = sy * w + sx;

            int x1 = x;
            int y1 = y;

            int x2 = w - x - 1;
            int y2 = y;

            int x3 = x;
            int y3 = h - y - 1;

            int x4 = w - x - 1;
            int y4 = h - y - 1;

            map[y1 * w + x1] = src_idx;
            map[y2 * w + x2] = src_idx;
            map[y3 * w + x3] = src_idx;
            map[y4 * w + x4] = src_idx;
        }
    }
}

void fractalkaleido_apply(void *ptr, VJFrame *frame, int *args) {
    fractalkaleido_t *s = (fractalkaleido_t*) ptr;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];
    uint8_t *restrict outY = s->buf[0];
    uint8_t *restrict outU = s->buf[1];
    uint8_t *restrict outV = s->buf[2];

    const int w = frame->out_width;
    const int h = frame->out_height;
    const int *restrict map = s->map;

    int needs_update = (args[6] > 0); 
    for(int i=0; i<8; i++) {
        if(s->last_args[i] != args[i]) { 
            needs_update = 1; 
            s->last_args[i] = args[i];
        }
    }

    if(needs_update) {
        float rot_speed = args[6] * 0.0002f;
        s->angle = wrap_angle(s->angle + rot_speed);
        float base_angle = s->angle + (args[1] / 360.0f) * TWO_PI;
        fractalkaleido_apply1(s, frame, args, base_angle);
    }

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int i = 0; i < w * h; i++) {
        int idx = map[i];
        outY[i] = srcY[idx];
        outU[i] = srcU[idx];
        outV[i] = srcV[idx];
    }
    
    veejay_memcpy( frame->data[0], outY, frame->len );
    veejay_memcpy( frame->data[1], outU, frame->uv_len );
    veejay_memcpy( frame->data[2], outV, frame->uv_len );
}
