/* * Linux VeeJay
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
#define INV_TWO_PI 0.15915494309f


typedef struct {
    int *map;
    float *atan_lut;
    float *sqrt_lut;
    float *cos_lut;
    float *sin_lut;
    float angle;
    int n_threads;
    int last_args[9];
    uint8_t *buf[3];
} fractalkaleido_t;

vj_effect *fractalkaleido_init(int w, int h)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));

    ve->num_params = 10;

    ve->defaults = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    // segments
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 48;
    ve->defaults[0] = 20;

    // rotation
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 360;
    ve->defaults[1] = 0;

    // scale
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 1000;
    ve->defaults[2] = 150;

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
    ve->defaults[5] = 0;

    // rotation speed
    ve->limits[0][6] = -100;
    ve->limits[1][6] = 100;
    ve->defaults[6] = 0;

    // twist
    ve->limits[0][7] = -300;
    ve->limits[1][7] = 300;
    ve->defaults[7]  = 20;

    // chaos
    ve->limits[0][8] = 0;
    ve->limits[1][8] = 100;
    ve->defaults[8]  = 15;

    // Twist mode
    ve->limits[0][9] = 0;
    ve->limits[1][9] = 5;
    ve->defaults[9]  = 0;

    ve->description = "Fractal Kaleido";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Segment Count",
        "Global Rotation",
        "Zoom",
        "Center X",
        "Center Y",
        "Mirror Mode",
        "Spin Speed",
        "Twist Energy",
        "Chaos Field",
        "Twist Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints, ve->limits[1][9], 9,
        "Radial Twist",
        "Wave Twist",
        "Log Spiral Twist",
        "Segment-Coupled Twist",
        "Vortex Collapse Twist",
        "Inversion Twist"
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

    size_t num_pixels = w * h;
    size_t total_floats = (num_pixels * 2) + (LUT_SIZE * 2);

    s->atan_lut = (float*) vj_malloc(sizeof(float) * total_floats);
    if(!s->atan_lut) {
        free(s);
        return NULL;
    }

    s->sqrt_lut = s->atan_lut + num_pixels;
    s->cos_lut  = s->sqrt_lut + num_pixels;
    s->sin_lut  = s->cos_lut + LUT_SIZE;

    s->buf[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * num_pixels * 3 );
    if(!s->buf[0]) {
        free(s->atan_lut);
        free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + num_pixels;
    s->buf[2] = s->buf[1] + num_pixels;

    s->map = (int*) vj_calloc(sizeof(int) * num_pixels);
    if(!s->map) {
        free(s->atan_lut);
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->n_threads = vje_advise_num_parallel_threads(num_pixels, vj_task_get_workers());

    init_sqrtatan_lut(s, w, h, w/2, h/2, s->n_threads);
    init_sin_cos_lut(s, s->n_threads);

    return (void*) s;
}

void fractalkaleido_free(void *ptr)
{
    fractalkaleido_t *s = (fractalkaleido_t*) ptr;
    if(s) {
        free(s->atan_lut);
        free(s->buf[0]);
        free(s->map);
        free(s);
    }
}

static inline float wrap_angle2(float a)
{
    float k = floorf(a * INV_TWO_PI);
    return a - k * TWO_PI;
}
/*

static inline float wrap_angle(float a)
{
    float x = a * INV_TWO_PI;
    int i = (int)x;
    float fx = (float)i;

    float frac = x - fx;

    frac += (x < fx) ? 1.0f : 0.0f;

    return frac * TWO_PI;
}*/

static inline float wrap_angle(float a)
{
    a *= INV_TWO_PI;

    int i = (int)a;

    a -= (float)i;

    if (UNLIKELY(a < 0.0f))
        a += 1.0f;

    return a * TWO_PI;
}

static inline float get_unified_rot(fractalkaleido_t *s, float rnorm, float base_angle)
{
    float phase = s->angle + base_angle;

    float radial = rnorm * rnorm;

    return TWO_PI * (radial + phase * 0.15f);
}

static inline float chaos_spectral(float input)
{
    float c = input * (1.0f / 100.0f);
    float low  = sinf(c * 1.0f);
    float mid  = sinf(c * 6.0f + low * 2.0f);
    float high = sinf(c * 18.0f + mid * 3.0f);
    return low  * 0.55f * mid  * 0.30f + high * 0.15f;
}

static void fractalkaleido_apply1_twistinversion(void *ptr, VJFrame *frame, int *args, float base_angle)
{
    fractalkaleido_t *s = (fractalkaleido_t*) ptr;

    const int w = frame->out_width;
    const int h = frame->out_height;
    const int hw = w >> 1;
    const int hh = h >> 1;

    const int segments_i = (args[0] < 2) ? 2 : args[0];
    const float rnorm = args[1] * (1.0f / 360.0f);
    const float scale_val = args[2] * (1.0f / 1000.0f);
    const float offxw = args[3] * 0.01f * w + (w * 0.5f);
    const float offyh = args[4] * 0.01f * h + (h * 0.5f);
    const float mirror = (float) args[5];
    const float twist_amt_val = args[7] * (1.0f / 300.0f);
    const float chaos_amt = chaos_spectral(args[8]);

    const float shaped = twist_amt_val / (0.35f + fabsf(twist_amt_val));
    const float twist_amt = shaped * 5.0f;
    const float inv_w = 1.0f / (float)w;
    const float inv_h = 1.0f / (float)h;
    const float inv_radius_norm = 1.0f / (float)(w + h);
    const float *cos_lut = s->cos_lut;
    const float *sin_lut = s->sin_lut;
    const float seg_angle = TWO_PI / (float)segments_i;
    const float inv_seg   = 1.0f / seg_angle;
    const float unified_rot = get_unified_rot(s, rnorm, base_angle);
    const float scale = 0.1f + scale_val * scale_val * 4.0f;

    int *map = s->map;

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y <= hh; y++)
    {
        const int row = y * w;
        const float *atan_row = s->atan_lut + row;
        const float *sqrt_row = s->sqrt_lut + row;

        for(int x = 0; x <= hw; x++)
        {
            float r = sqrt_row[x];
            float theta = atan_row[x];
            float f = theta * inv_seg;
            int seg_i = (int)f;
            float u = f - seg_i;
            float u2 = u + u - 1.0f;
            float tri = 1.0f - u2 * u2;
                  tri = tri * tri * (3.0f - 2.0f * tri);

            float seg_theta = tri * seg_angle;
                  seg_theta = seg_theta * (1.0f - mirror) + (seg_angle - seg_theta) * mirror;

            float rn = r * inv_radius_norm;
            float falloff = rn * rn;
            float zone = sinf(rn * 10.0f); //FIXME optimize?
            float sign = (zone > 0.0f) ? 1.0f : -1.0f;
            float twist_angle = twist_amt * falloff * sign * TWO_PI * 0.75f;
            float chaos_phase = r * 0.035f + theta * 3.0f;
            float c_f = chaos_phase * LUT_DIVISOR;
            int c_i = (int)c_f;
            float c_frac = c_f - c_i;
            int c0 = c_i & LUT_MASK;
            int c1 = (c_i + 1) & LUT_MASK;

            float chaos_wave = sin_lut[c0] + (sin_lut[c1] - sin_lut[c0]) * c_frac;
            float chaos = chaos_amt * (0.5f + 0.5f * falloff) * chaos_wave;
            float a = seg_theta + twist_angle + chaos;
            float mod = 0.85f + 0.15f * falloff;
            float final_angle = a * mod + unified_rot;

            float lut_f = final_angle * LUT_DIVISOR;
            int lut_i = (int)lut_f;
            float frac = lut_f - lut_i;
            int i0 = lut_i & LUT_MASK;
            int i1 = (lut_i + 1) & LUT_MASK;
            
            float t = frac * frac * (3.0f - 2.0f * frac);
            float cosv = cos_lut[i0] + (cos_lut[i1] - cos_lut[i0]) * t;
            float sinv = sin_lut[i0] + (sin_lut[i1] - sin_lut[i0]) * t;

            float tx = r * cosv * scale + offxw;
            float ty = r * sinv * scale + offyh;

            float u_wrap = tx * inv_w;
            float v_wrap = ty * inv_h;

            int tilex = (int)u_wrap;
            int tiley = (int)v_wrap;

            float fx = u_wrap - tilex;
            float fy = v_wrap - tiley;

            int negx = (u_wrap < 0.0f) & (fx != 0.0f);
            int negy = (v_wrap < 0.0f) & (fy != 0.0f);

            tilex -= negx;
            tiley -= negy;

            fx += (float)negx;
            fy += (float)negy;

            int sx = (int)(fx * (float)(w - 1) + 0.5f);
            int sy = (int)(fy * (float)(h - 1) + 0.5f);

            int maskx = (unsigned)sx < (unsigned)w;
            int masky = (unsigned)sy < (unsigned)h;

            sx = maskx * sx;
            sy = masky * sy;

            int idx = sy * w + sx;

            map[y * w + x] = idx;
            map[y * w + (w - 1 - x)] = idx;
            map[(h - 1 - y) * w + x] = idx;
            map[(h - 1 - y) * w + (w - 1 - x)] = idx;
        }
    }
}


static void fractalkaleido_apply1(void *ptr, VJFrame *frame, int *args, float base_angle)
{
    fractalkaleido_t *s = (fractalkaleido_t*) ptr;

    const int w = frame->out_width;
    const int h = frame->out_height;
    const int hw = w >> 1;
    const int hh = h >> 1;

    const int segments_i = (args[0] < 2) ? 2 : args[0];
    const float rnorm = args[1] * (1.0f / 360.0f);
    const float scale_val = args[2] * (1.0f / 1000.0f);
    const float offxw = args[3] * 0.01f * w + (w * 0.5f);
    const float offyh = args[4] * 0.01f * h + (h * 0.5f);
    const float mirror = (float) args[5];
    const float twist_amt_val = args[7] * (1.0f / 300.0f);

    const float chaos_amt = chaos_spectral(args[8]);
    const float scale = 0.1f + scale_val * scale_val * 4.0f;
    const float twist_amt = expf(twist_amt_val * 2.2f) - 1.0f;
    const float inv_w = 1.0f / (float)w;
    const float inv_h = 1.0f / (float)h;
    const float seg_angle = TWO_PI / (float)segments_i;
    const float inv_seg   = 1.0f / seg_angle;
    const float inv_radius_norm = 1.0f / (float)(w + h);
    const float *cos_lut = s->cos_lut;
    const float *sin_lut = s->sin_lut;
    const float unified_rot = get_unified_rot(s, rnorm, base_angle);

    int *map = s->map;

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y <= hh; y++)
    {
        const int row = y * w;
        const float *atan_row = s->atan_lut + row;
        const float *sqrt_row = s->sqrt_lut + row;

        for(int x = 0; x <= hw; x++)
        {
            float r = sqrt_row[x];
            float theta = atan_row[x];
            float f = theta * inv_seg;
            int seg_i = (int)f;
            float u = f - seg_i;
            float u2 = u + u - 1.0f;
            float tri = 1.0f - u2 * u2;
                  tri = tri * tri * (3.0f - 2.0f * tri);

            float seg_theta = tri * seg_angle;
                  seg_theta = seg_theta * (1.0f - mirror) + (seg_angle - seg_theta) * mirror;

            float rn = r * inv_radius_norm + 1e-6f;
            float falloff = rn;
            float spiral = logf(1.0f + rn * 4.0f); //FIXME optimize?
            float twist_angle = twist_amt * falloff * spiral * TWO_PI * 0.5f;
            float chaos_phase = r * 0.035f + theta * 3.0f;
            float c_f = chaos_phase * LUT_DIVISOR;
            int c_i = (int)c_f;
            float c_frac = c_f - c_i;

            int c0 = c_i & LUT_MASK;
            int c1 = (c_i + 1) & LUT_MASK;

            float chaos_wave = sin_lut[c0] + (sin_lut[c1] - sin_lut[c0]) * c_frac;
            float chaos = chaos_amt * (0.5f + 0.5f * falloff) * chaos_wave;
            float a = seg_theta + twist_angle + chaos;
            float mod = 0.85f + 0.15f * falloff;
            float final_angle = a * mod + unified_rot;

            float lut_f = final_angle * LUT_DIVISOR;
            int lut_i = (int)lut_f;
            float frac = lut_f - lut_i;

            int i0 = lut_i & LUT_MASK;
            int i1 = (lut_i + 1) & LUT_MASK;

            float cosv = cos_lut[i0] + (cos_lut[i1] - cos_lut[i0]) * frac;
            float sinv = sin_lut[i0] + (sin_lut[i1] - sin_lut[i0]) * frac;

            float tx = r * cosv * scale + offxw;
            float ty = r * sinv * scale + offyh;

            float u_wrap = tx * inv_w;
            float v_wrap = ty * inv_h;

            int tilex = (int)u_wrap;
            int tiley = (int)v_wrap;

            float fx = u_wrap - tilex;
            float fy = v_wrap - tiley;

            int negx = (u_wrap < 0.0f) & (fx != 0.0f);
            int negy = (v_wrap < 0.0f) & (fy != 0.0f);

            tilex -= negx;
            tiley -= negy;

            fx += (float)negx;
            fy += (float)negy;

            int sx = (int)(fx * (float)(w - 1) + 0.5f);
            int sy = (int)(fy * (float)(h - 1) + 0.5f);

            int maskx = (unsigned)sx < (unsigned)w;
            int masky = (unsigned)sy < (unsigned)h;

            sx = maskx * sx;
            sy = masky * sy;

            int idx = sy * w + sx;

            map[y * w + x] = idx;
            map[y * w + (w - 1 - x)] = idx;
            map[(h - 1 - y) * w + x] = idx;
            map[(h - 1 - y) * w + (w - 1 - x)] = idx;
        }
    }
}


static void fractalkaleido_apply1_segcouple(void *ptr, VJFrame *frame, int *args, float base_angle)
{
    fractalkaleido_t *s = (fractalkaleido_t*) ptr;

    const int w = frame->out_width;
    const int h = frame->out_height;
    const int hw = w >> 1;
    const int hh = h >> 1;

    const int segments_i = (args[0] < 2 ? 2 : args[0]);
    const float rnorm = args[1] * (1.0f / 360.0f);
    const float scale_val = args[2] * (1.0f / 1000.0f);
    const float offxw = args[3] * 0.01f * w + (w * 0.5f);
    const float offyh = args[4] * 0.01f * h + (h * 0.5f);
    const float mirror = (float) args[5];
    const float twist_amt_val = args[7] * (1.0f / 300.0f);
    const float chaos_amt = chaos_spectral(args[8]);
    
    const float seg_boost = 1.0f + 0.15f * segments_i;
    const float twist_amt = (twist_amt_val * twist_amt_val) * seg_boost * 4.0f;
    const float scale = 0.1f + scale_val * scale_val * 4.0f;
    const float seg_angle = TWO_PI / (float)segments_i;
    const float inv_seg   = 1.0f / seg_angle;
    const float inv_w = 1.0f / (float)w;
    const float inv_h = 1.0f / (float)h;
    const float inv_radius_norm = 1.0f / (float)(w + h);
    const float *cos_lut = s->cos_lut;
    const float *sin_lut = s->sin_lut;
    const float unified_rot = get_unified_rot(s, rnorm, base_angle);

    int *map = s->map;

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y <= hh; y++)
    {
        const int row = y * w;
        const float *atan_row = s->atan_lut + row;
        const float *sqrt_row = s->sqrt_lut + row;

        for(int x = 0; x <= hw; x++)
        {
            float r = sqrt_row[x];
            float theta = atan_row[x];
            float f = theta * inv_seg;
            int seg_i = (int)f;
            float u = f - seg_i;
            float u2 = u + u - 1.0f;
            float tri = 1.0f - u2 * u2;
                  tri = tri * tri * (3.0f - 2.0f * tri);

            float seg_theta = tri * seg_angle;
                  seg_theta = seg_theta * (1.0f - mirror) + (seg_angle - seg_theta) * mirror;

            float rn = r * inv_radius_norm;
            float falloff = rn;
            float seg_phase = (float)seg_i * 0.75f;
            //float local = sinf(seg_phase + rn * 6.0f);
            float sp = (seg_phase + rn * 6.0f) * LUT_DIVISOR;
            int si = (int)sp;
            float sf = sp - (float)si;

            int s0 = si & LUT_MASK;
            int s1 = (si + 1) & LUT_MASK;

            float local = sin_lut[s0] + (sin_lut[s1] - sin_lut[s0]) * sf;

            float twist_angle = twist_amt * falloff * local * TWO_PI * 0.5f;
            float chaos_phase = r * 0.035f + theta * 3.0f;
            float c_f = chaos_phase * LUT_DIVISOR;
            int c_i = (int)c_f;
            float c_frac = c_f - c_i;

            int c0 = c_i & LUT_MASK;
            int c1 = (c_i + 1) & LUT_MASK;

            float chaos_wave = sin_lut[c0] + (sin_lut[c1] - sin_lut[c0]) * c_frac;
            float chaos = chaos_amt * (0.5f + 0.5f * falloff) * chaos_wave;
            float a = seg_theta + twist_angle + chaos;
            float mod = 0.85f + 0.15f * falloff;

            float final_angle = a * mod + unified_rot;

            float lut_f = final_angle * LUT_DIVISOR;
            int lut_i = (int)lut_f;
            float frac = lut_f - lut_i;

            int i0 = lut_i & LUT_MASK;
            int i1 = (lut_i + 1) & LUT_MASK;

            float cosv = cos_lut[i0] + (cos_lut[i1] - cos_lut[i0]) * frac;
            float sinv = sin_lut[i0] + (sin_lut[i1] - sin_lut[i0]) * frac;

            float tx = r * cosv * scale + offxw;
            float ty = r * sinv * scale + offyh;

            float u_wrap = tx * inv_w;
            float v_wrap = ty * inv_h;

            int tilex = (int)u_wrap;
            int tiley = (int)v_wrap;

            float fx = u_wrap - tilex;
            float fy = v_wrap - tiley;

            int negx = (u_wrap < 0.0f) & (fx != 0.0f);
            int negy = (v_wrap < 0.0f) & (fy != 0.0f);

            tilex -= negx;
            tiley -= negy;

            fx += (float)negx;
            fy += (float)negy;

            int sx = (int)(fx * (float)(w - 1) + 0.5f);
            int sy = (int)(fy * (float)(h - 1) + 0.5f);

            int maskx = (unsigned)sx < (unsigned)w;
            int masky = (unsigned)sy < (unsigned)h;

            sx = maskx * sx;
            sy = masky * sy;

            int idx = sy * w + sx;

            map[y * w + x] = idx;
            map[y * w + (w - 1 - x)] = idx;
            map[(h - 1 - y) * w + x] = idx;
            map[(h - 1 - y) * w + (w - 1 - x)] = idx;
        }
    }
}


static void fractalkaleido_apply1_vortex(void *ptr, VJFrame *frame, int *args, float base_angle)
{
    fractalkaleido_t *s = (fractalkaleido_t*) ptr;

    const int w = frame->out_width;
    const int h = frame->out_height;
    const int hw = w >> 1;
    const int hh = h >> 1;

    const int segments_i = (args[0] < 2 ? 2 : args[0]);
    const float rnorm = args[1] * (1.0f / 360.0f);
    const float scale_val = args[2] * (1.0f / 1000.0f);
    const float offxw = args[3] * 0.01f * w + (w * 0.5f);
    const float offyh = args[4] * 0.01f * h + (h * 0.5f);
    const float mirror = (float) args[5];
    const float twist_amt_val = args[7] * (1.0f / 300.0f);
    const float chaos_amt = chaos_spectral(args[8]);

    const float twist_amt = (twist_amt_val * twist_amt_val) * 6.0f;
    const float seg_angle = TWO_PI / (float)segments_i;
    const float inv_seg   = 1.0f / seg_angle;
    const float scale = 0.1f + scale_val * scale_val * 4.0f;
    const float inv_w = 1.0f / (float)w;
    const float inv_h = 1.0f / (float)h;
    const float inv_radius_norm = 1.0f / (float)(w + h);
    const float *cos_lut = s->cos_lut;
    const float *sin_lut = s->sin_lut;
    const float unified_rot = get_unified_rot(s, rnorm, base_angle);

    int *map = s->map;

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y <= hh; y++)
    {
        const int row = y * w;
        const float *atan_row = s->atan_lut + row;
        const float *sqrt_row = s->sqrt_lut + row;

        for(int x = 0; x <= hw; x++)
        {
            float r = sqrt_row[x];
            float theta = atan_row[x];
            float f = theta * inv_seg;
            int seg_i = (int)f;
            float u = f - seg_i;
            float u2 = u + u - 1.0f;
            float tri = 1.0f - u2 * u2;
                  tri = tri * tri * (3.0f - 2.0f * tri);

            float seg_theta = tri * seg_angle;
                  seg_theta = seg_theta * (1.0f - mirror) + (seg_angle - seg_theta) * mirror;

            float rn = r * inv_radius_norm + 1e-6f;
            float falloff = 1.0f / (1.0f + rn * rn);
            float twist_angle = twist_amt * falloff * TWO_PI * 0.75f;
            float chaos_phase = r * 0.035f + theta * 3.0f;
            float c_f = chaos_phase * LUT_DIVISOR;
            int c_i = (int)c_f;
            float c_frac = c_f - c_i;

            int c0 = c_i & LUT_MASK;
            int c1 = (c_i + 1) & LUT_MASK;

            float chaos_wave = sin_lut[c0] + (sin_lut[c1] - sin_lut[c0]) * c_frac;
            float chaos = chaos_amt * (0.5f + 0.5f * falloff) * chaos_wave;
            float a = seg_theta + twist_angle + chaos;
            float mod = 0.85f + 0.15f * falloff;
            float final_angle = a * mod + unified_rot;

            float lut_f = final_angle * LUT_DIVISOR;
            int lut_i = (int)lut_f;
            float frac = lut_f - lut_i;

            int i0 = lut_i & LUT_MASK;
            int i1 = (lut_i + 1) & LUT_MASK;

            //float cosv = cos_lut[i0] + (cos_lut[i1] - cos_lut[i0]) * frac;
            //float sinv = sin_lut[i0] + (sin_lut[i1] - sin_lut[i0]) * frac;

            // flower:
            float t = frac * frac * (3.0f - 2.0f * frac);
            float cosv = cos_lut[i0] + (cos_lut[i1] - cos_lut[i0]) * t;
            float sinv = sin_lut[i0] + (sin_lut[i1] - sin_lut[i0]) * t;

            float tx = r * cosv * scale + offxw;
            float ty = r * sinv * scale + offyh;

            float u_wrap = tx * inv_w;
            float v_wrap = ty * inv_h;

            int tilex = (int)u_wrap;
            int tiley = (int)v_wrap;

            float fx = u_wrap - tilex;
            float fy = v_wrap - tiley;

            int negx = (u_wrap < 0.0f) & (fx != 0.0f);
            int negy = (v_wrap < 0.0f) & (fy != 0.0f);

            tilex -= negx;
            tiley -= negy;

            fx += (float)negx;
            fy += (float)negy;

            int sx = (int)(fx * (float)(w - 1) + 0.5f);
            int sy = (int)(fy * (float)(h - 1) + 0.5f);

            int maskx = (unsigned)sx < (unsigned)w;
            int masky = (unsigned)sy < (unsigned)h;

            sx = maskx * sx;
            sy = masky * sy;

            int idx = sy * w + sx;

            map[y * w + x] = idx;
            map[y * w + (w - 1 - x)] = idx;
            map[(h - 1 - y) * w + x] = idx;
            map[(h - 1 - y) * w + (w - 1 - x)] = idx;
        }
    }
}


static void fractalkaleido_apply1_wave(void *ptr, VJFrame *frame, int *args, float base_angle)
{
    fractalkaleido_t *s = (fractalkaleido_t*) ptr;

    const int w = frame->out_width;
    const int h = frame->out_height;
    const int hw = w >> 1;
    const int hh = h >> 1;

    const int segments_i = (args[0] < 2) ? 2 : args[0];
    const float rnorm = args[1] * (1.0f / 360.0f);
    const float scale_val = args[2] * (1.0f / 1000.0f);
    const float offxw = args[3] * 0.01f * w + (w * 0.5f);
    const float offyh = args[4] * 0.01f * h + (h * 0.5f);
    const float mirror = (float) args[5];
    const float twist_amt_val = args[7] * (1.0f / 300.0f);
    const float chaos_amt = chaos_spectral(args[8]);

    const float twist_amt = twist_amt_val * 3.5f;
    const float scale = 0.1f + scale_val * scale_val * 4.0f;
    const float inv_w = 1.0f / (float)w;
    const float inv_h = 1.0f / (float)h;
    const float inv_radius_norm = 1.0f / (float)(w + h);
    const float *cos_lut = s->cos_lut;
    const float *sin_lut = s->sin_lut;
    const float seg_angle = TWO_PI / (float)segments_i;
    const float inv_seg   = 1.0f / seg_angle;
    const float unified_rot = get_unified_rot(s, rnorm, base_angle);

    int *map = s->map;

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y <= hh; y++)
    {
        const int row = y * w;
        const float *atan_row = s->atan_lut + row;
        const float *sqrt_row = s->sqrt_lut + row;

        for(int x = 0; x <= hw; x++)
        {
            float r = sqrt_row[x];
            float theta = atan_row[x];
            float f = theta * inv_seg;
            int seg_i = (int)f;
            float u = f - seg_i;
            float u2 = u + u - 1.0f;
            float tri = 1.0f - u2 * u2;
                  tri = tri * tri * (3.0f - 2.0f * tri);
            float seg_theta = tri * seg_angle;
                  seg_theta = seg_theta * (1.0f - mirror) + (seg_angle - seg_theta) * mirror;

            float rn = r * inv_radius_norm;
            float falloff = rn * (1.0f - rn);
            float phase = theta * 6.0f + rn * 8.0f;
            float wave = sinf(phase);
            float twist_angle = twist_amt * falloff * wave * TWO_PI;
            float chaos_phase = r * 0.035f + theta * 3.0f;
            float c_f = chaos_phase * LUT_DIVISOR;
            int c_i = (int)c_f;
            float c_frac = c_f - c_i;

            int c0 = c_i & LUT_MASK;
            int c1 = (c_i + 1) & LUT_MASK;

            float chaos_wave = sin_lut[c0] + (sin_lut[c1] - sin_lut[c0]) * c_frac;
            float chaos = chaos_amt * (0.5f + 0.5f * falloff) * chaos_wave;

            float a = seg_theta + twist_angle + chaos;
            float mod = 0.85f + 0.15f * falloff;
            float final_angle = a * mod + unified_rot;

            float lut_f = final_angle * LUT_DIVISOR;
            int lut_i = (int)lut_f;
            float frac = lut_f - lut_i;

            int i0 = lut_i & LUT_MASK;
            int i1 = (lut_i + 1) & LUT_MASK;

            float t = frac * frac * (3.0f - 2.0f * frac);
            float cosv = cos_lut[i0] + (cos_lut[i1] - cos_lut[i0]) * t;
            float sinv = sin_lut[i0] + (sin_lut[i1] - sin_lut[i0]) * t;

            float tx = r * cosv * scale + offxw;
            float ty = r * sinv * scale + offyh;

            float u_wrap = tx * inv_w;
            float v_wrap = ty * inv_h;

            int tilex = (int)u_wrap;
            int tiley = (int)v_wrap;

            float fx = u_wrap - tilex;
            float fy = v_wrap - tiley;

            int negx = (u_wrap < 0.0f) & (fx != 0.0f);
            int negy = (v_wrap < 0.0f) & (fy != 0.0f);

            tilex -= negx;
            tiley -= negy;

            fx += (float)negx;
            fy += (float)negy;

            int sx = (int)(fx * (float)(w - 1) + 0.5f);
            int sy = (int)(fy * (float)(h - 1) + 0.5f);

            int maskx = (unsigned)sx < (unsigned)w;
            int masky = (unsigned)sy < (unsigned)h;

            sx = maskx * sx;
            sy = masky * sy;

            int idx = sy * w + sx;

            map[y * w + x] = idx;
            map[y * w + (w - 1 - x)] = idx;
            map[(h - 1 - y) * w + x] = idx;
            map[(h - 1 - y) * w + (w - 1 - x)] = idx;
        }
    }
}

static void fractalkaleido_apply1_radialclassic(void *ptr, VJFrame *frame, int *args, float base_angle)
{
    fractalkaleido_t *s = (fractalkaleido_t*) ptr;

    const int w = frame->out_width;
    const int h = frame->out_height;
    const int hw = w >> 1;
    const int hh = h >> 1;

    const int segments_i = (args[0] < 2 ? 2 : args[0]);
    const float scale_val = args[2] * (1.0f / 1000.0f);
    const float offxw = args[3] * 0.01f * w + (w * 0.5f);
    const float offyh = args[4] * 0.01f * h + (h * 0.5f);
    const float mirror = (float) args[5];
    const float rot_speed = args[6] * 0.0002f;
    const float twist_amt_val = args[7] * (1.0f / 300.0f);
    const float chaos_amt = chaos_spectral(args[8]);

    const float rotation_dir = (rot_speed >= 0.0f) ? 1.0f : -1.0f;
    const float twist_amt = twist_amt_val * twist_amt_val * twist_amt_val * 2.5f;
    const float scale = 0.1f + scale_val * scale_val * 4.0f;
    const float inv_w = 1.0f / (float)w;
    const float inv_h = 1.0f / (float)h;
    const float inv_radius_norm = 1.0f / (float)(w + h);
    const float seg_angle = TWO_PI / (float)segments_i;
    const float inv_seg   = 1.0f / seg_angle;
    const float *cos_lut = s->cos_lut;
    const float *sin_lut = s->sin_lut;

    int *map = s->map;

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int y = 0; y <= hh; y++)
    {
        const int row = y * w;

        const float *atan_row = s->atan_lut + row;
        const float *sqrt_row = s->sqrt_lut + row;

        for(int x = 0; x <= hw; x++)
        {
            float theta = atan_row[x];
            float r = sqrt_row[x];
            float rn = r * inv_radius_norm;

            float falloff = rn / (1.0f + rn);
            falloff = falloff * falloff * (3.0f - 2.0f * falloff);

            theta += falloff * twist_amt * TWO_PI * 4.0f * rotation_dir;

            float chaos_phase = r * 0.035f + theta * 2.5f;
            float c_f = chaos_phase * LUT_DIVISOR;
            int c_i = (int)c_f;
            float c_frac = c_f - c_i;

            int c0 = c_i & LUT_MASK;
            int c1 = (c_i + 1) & LUT_MASK;

            float chaos_wave = sin_lut[c0] + (sin_lut[c1] - sin_lut[c0]) * c_frac;
            float chaos = chaos_amt * (0.4f + 0.6f * falloff) * chaos_wave;

            ////chaos:
            //float a = wrap_angle( theta + chaos + base_angle * rotation_dir);

            //float a = wrap_angle((theta + chaos) * rotation_dir + base_angle);
            float local = theta + chaos;
            //float a = wrap_angle(local + base_angle * rotation_dir);
            float a = local + base_angle * rotation_dir;
            float f = a * inv_seg;
            int seg_i = (int)f;
            float u = f - seg_i;
            float u2 = u + u - 1.0f;
            float tri = 1.0f - u2 * u2;
                  tri = tri * tri * (3.0f - 2.0f * tri);

            float seg = tri * seg_angle;
                  seg = seg * (1.0f - mirror) + (seg_angle - seg) * mirror;

            float lut_f = seg * LUT_DIVISOR;


            int lut_i = (int)lut_f;
            float frac = lut_f - lut_i;
            
            int i0 = lut_i & LUT_MASK;
            int i1 = (lut_i + 1) & LUT_MASK;

            float t = frac * frac * (3.0f - 2.0f * frac);
            float cosv = cos_lut[i0] + (cos_lut[i1] - cos_lut[i0]) * t;
            float sinv = sin_lut[i0] + (sin_lut[i1] - sin_lut[i0]) * t;

            float nx = r * cosv * scale + offxw;
            float ny = r * sinv * scale + offyh;

            float tx = nx * inv_w;
            float ty = ny * inv_h;

            int tilex = (int)tx;
            int tiley = (int)ty;

            float fx = tx - tilex;
            float fy = ty - tiley;

            float nxm = (tx < 0.0f && fx != 0.0f) ? 1.0f : 0.0f;
            float nym = (ty < 0.0f && fy != 0.0f) ? 1.0f : 0.0f;

            tilex -= (int)nxm;
            tiley -= (int)nym;

            fx += nxm;
            fy += nym;

            int sx = (int)(fx * (float)(w - 1) + 0.5f);
            int sy = (int)(fy * (float)(h - 1) + 0.5f);

            int maskx = (unsigned)sx < (unsigned)w;
            int masky = (unsigned)sy < (unsigned)h;

            sx = maskx * sx;
            sy = masky * sy;

            int idx = sy * w + sx;

            map[y * w + x] = idx;
            map[y * w + (w - 1 - x)] = idx;
            map[(h - 1 - y) * w + x] = idx;
            map[(h - 1 - y) * w + (w - 1 - x)] = idx;
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
    const int len = w * h;

    const int mode = args[9];

    int needs_update = (args[6] != 0); 
    for(int i=0; i< 10; i++) {
        if(s->last_args[i] != args[i]) { 
            needs_update = 1; 
            s->last_args[i] = args[i];
        }
    }

    if(needs_update) {
        float rot_speed = args[6] * 0.0002f;
        s->angle = wrap_angle(s->angle + rot_speed);
        float base_angle = wrap_angle(s->angle + (args[1] / 360.0f) * TWO_PI);
        
        
        switch(mode) {
            case 4: fractalkaleido_apply1(s, frame, args, base_angle); break;
            case 3: fractalkaleido_apply1_twistinversion(s,frame,args,base_angle); break;
            case 2: fractalkaleido_apply1_segcouple(s,frame,args,base_angle); break;
            case 1: fractalkaleido_apply1_wave(s,frame,args,base_angle); break;
            case 5: fractalkaleido_apply1_vortex(s,frame,args,base_angle); break;
            case 0: fractalkaleido_apply1_radialclassic(s, frame, args, base_angle); break;
        }

    }

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int i = 0; i < len; i++) {
        int idx = map[i];

        outY[i] = srcY[idx];
        outU[i] = srcU[idx];
        outV[i] = srcV[idx];
    }
    
    veejay_memcpy( frame->data[0], outY, frame->len );
    veejay_memcpy( frame->data[1], outU, frame->uv_len );
    veejay_memcpy( frame->data[2], outV, frame->uv_len );

}