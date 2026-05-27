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
#include <math.h>
#include <veejaycore/vjmem.h>
#include "integralblur.h"

#define INTEGRALBLUR_PARAMS 6

#define P_RADIUS        0
#define P_ITERATIONS    1
#define P_BLUR_AMOUNT   2
#define P_CHROMA_AMOUNT 3
#define P_BEAT_PUSH     4
#define P_BEAT_SMOOTH   5

typedef struct {
    uint8_t  *planes;
    uint8_t  *orig;
    uint8_t  *mask;
    uint8_t  *tmp;
    uint32_t *integral;

    float radius_eff;
    float mix_eff;
    float beat_env;
    int initialized;

    int width;
    int stride;
    int height;
    int max_radius;
    int n_threads;
} integralblur_t;

static inline int ib_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t ib_u8(int v)
{
    return (uint8_t) ib_clampi(v, 0, 255);
}

static inline int ib_param1000_to_range(int v, int lo, int hi)
{
    v = ib_clampi(v, 0, 1000);
    if (hi <= lo)
        return lo;
    return lo + (((hi - lo) * v + 500) / 1000);
}

static inline int ib_range_to_param1000(int v, int lo, int hi)
{
    v = ib_clampi(v, lo, hi);
    if (hi <= lo)
        return 0;
    return ((v - lo) * 1000 + ((hi - lo) >> 1)) / (hi - lo);
}

static inline int ib_beat_shape_q1000(int beat_push)
{
    int bp = ib_clampi(beat_push, 0, 1000);
    int sq = (bp * bp + 500) / 1000;

    return ib_clampi(((bp * 420) + (sq * 580) + 500) / 1000, 0, 1000);
}

static inline void ib_update_beat_state(integralblur_t *f, int beat_push, int beat_smooth)
{
    int shaped = ib_beat_shape_q1000(beat_push);
    float target = (float) shaped;
    float smooth = (float) ib_clampi(beat_smooth, 0, 1000) * 0.001f;

    float attack = 0.52f - smooth * 0.25f;
    float release = 0.075f - smooth * 0.045f;

    if (attack < 0.18f)
        attack = 0.18f;
    if (release < 0.018f)
        release = 0.018f;

    if (target > f->beat_env)
        f->beat_env += (target - f->beat_env) * attack;
    else
        f->beat_env += (target - f->beat_env) * release;

    if (f->beat_env < 0.5f)
        f->beat_env = 0.0f;
    if (f->beat_env > 1000.0f)
        f->beat_env = 1000.0f;
}

static inline int ib_mix_u8(int a, int b, int q8)
{
    q8 = ib_clampi(q8, 0, 255);
    return a + (((b - a) * q8 + ((b >= a) ? 127 : -127)) / 255);
}

vj_effect *integralblur_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    int max_radius = MIN(w, h) / 4;

    if (!ve)
        return NULL;

    if (max_radius < 1)
        max_radius = 1;

    ve->num_params = INTEGRALBLUR_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if (!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if (ve->defaults) free(ve->defaults);
        if (ve->limits[0]) free(ve->limits[0]);
        if (ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_RADIUS]        = 0;
    ve->limits[1][P_RADIUS]        = 1000;
    ve->limits[0][P_ITERATIONS]    = 1;
    ve->limits[1][P_ITERATIONS]    = 6;
    ve->limits[0][P_BLUR_AMOUNT]   = 0;
    ve->limits[1][P_BLUR_AMOUNT]   = 1000;
    ve->limits[0][P_CHROMA_AMOUNT] = 0;
    ve->limits[1][P_CHROMA_AMOUNT] = 1000;
    ve->limits[0][P_BEAT_PUSH]     = 0;
    ve->limits[1][P_BEAT_PUSH]     = 1000;
    ve->limits[0][P_BEAT_SMOOTH]   = 0;
    ve->limits[1][P_BEAT_SMOOTH]   = 1000;

    ve->defaults[P_RADIUS]        = ib_range_to_param1000(3, 1, max_radius);
    ve->defaults[P_ITERATIONS]    = 1;
    ve->defaults[P_BLUR_AMOUNT]   = 1000;
    ve->defaults[P_CHROMA_AMOUNT] = 850;
    ve->defaults[P_BEAT_PUSH]     = 0;
    ve->defaults[P_BEAT_SMOOTH]   = 620;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Radius",
        "Iterations",
        "Blur Amount",
        "Chroma Blur",
        "Beat Push",
        "Beat Smooth"
    );

    ve->description = "Integral Blur";
    ve->sub_format = 1;

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY,                              ib_range_to_param1000(2, 1, max_radius), ib_range_to_param1000(MIN(max_radius, 18), 1, max_radius), 5, 20, 1800, 4200, 900, 24,    /* Radius */
        VJ_BEAT_DETAIL,        VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000, /* Iterations */
        VJ_BEAT_INTENSITY,     VJ_BEAT_F_CONTINUOUS,                               720, 1000, 8, 30, 1200, 3200, 300, 42,                      /* Blur Amount */
        VJ_BEAT_COLOR_AMOUNT,  VJ_BEAT_F_PHRASE_ONLY,                              520, 1000, 5, 20, 2200, 5200, 1200, 20,                     /* Chroma Blur */
        VJ_BEAT_INTENSITY,     VJ_BEAT_F_CONTINUOUS,                               0, 820, 16, 72, 120, 900, 0, 100,                           /* Beat Push */
        VJ_BEAT_INERTIA,       VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000  /* Beat Smooth */
    );

    return ve;
}

void *integralblur_malloc(int width, int height)
{
    integralblur_t *f;
    size_t len;
    size_t integral_len;

    if (width <= 0 || height <= 0)
        return NULL;

    f = (integralblur_t*) vj_calloc(sizeof(integralblur_t));
    if(!f)
        return NULL;

    f->width = width;
    f->height = height;
    f->stride = width + 1;
    f->max_radius = MIN(width, height) / 4;
    if (f->max_radius < 1)
        f->max_radius = 1;

    len = (size_t)width * (size_t)height;
    integral_len = (size_t)(width + 1) * (size_t)(height + 1);

    f->planes = (uint8_t*) vj_malloc(len * 3u);
    f->integral = (uint32_t*) vj_malloc(sizeof(uint32_t) * integral_len);

    if(!f->planes || !f->integral) {
        if (f->planes) free(f->planes);
        if (f->integral) free(f->integral);
        free(f);
        return NULL;
    }

    f->orig = f->planes;
    f->mask = f->orig + len;
    f->tmp  = f->mask + len;

    f->radius_eff = 3.0f;
    f->mix_eff = 1000.0f;
    f->beat_env = 0.0f;
    f->initialized = 0;

    f->n_threads = vje_advise_num_threads(width * height);
    if (f->n_threads <= 0)
        f->n_threads = 1;

    return f;
}

void integralblur_free(void *ptr)
{
    integralblur_t *f = (integralblur_t*) ptr;

    if (!f)
        return;

    if (f->planes)
        free(f->planes);
    if (f->integral)
        free(f->integral);
    free(f);
}

static void build_integral(integralblur_t *f, uint8_t *src)
{
    int w = f->width;
    int h = f->height;
    int stride = f->stride;

    uint32_t *I = f->integral;

    veejay_memset(I, 0, sizeof(uint32_t) * (w + 1));

    for (int y = 1; y <= h; y++)
    {
        uint32_t sum = 0;

        uint8_t  *src_row = src + (y - 1) * w;
        uint32_t *cur      = I + y * stride;
        uint32_t *prev     = I + (y - 1) * stride;

        cur[0] = 0;

        for (int x = 1; x <= w; x++)
        {
            sum += src_row[x - 1];
            cur[x] = sum + prev[x];
        }
    }
}

static void box_blur(integralblur_t *f,
                     uint8_t *src,
                     uint8_t *dst,
                     int radius)
{
    int w = f->width;
    int h = f->height;
    int stride = f->stride;

    uint32_t *I = f->integral;

#pragma omp parallel for schedule(static) num_threads(f->n_threads)
    for (int y = 0; y < h; y++)
    {
        int y0 = y - radius; if (y0 < 0) y0 = 0;
        int y1 = y + radius; if (y1 >= h) y1 = h - 1;

        int iy0 = y0;
        int iy1 = y1 + 1;

        uint32_t *row_i0 = I + iy0 * stride;
        uint32_t *row_i1 = I + iy1 * stride;

        uint8_t *out = dst + y * w;

        for (int x = 0; x < w; x++)
        {
            int x0 = x - radius; if (x0 < 0) x0 = 0;
            int x1 = x + radius; if (x1 >= w) x1 = w - 1;

            int ix0 = x0;
            int ix1 = x1 + 1;

            uint32_t sum =
                row_i1[ix1]
              - row_i0[ix1]
              - row_i1[ix0]
              + row_i0[ix0];

            int area = (x1 - x0 + 1) * (y1 - y0 + 1);

            out[x] = (uint8_t)(sum / area);
        }
    }
}

static uint8_t *integralblur_blur_plane(integralblur_t *f,
                                        uint8_t *plane,
                                        int len,
                                        int radius,
                                        int iter)
{
    uint8_t *src = f->mask;
    uint8_t *dst = f->tmp;

    veejay_memcpy(src, plane, len);

    if (iter < 2)
    {
        for (int i = 0; i < iter; i++)
        {
            uint8_t *swap;

            build_integral(f, src);
            box_blur(f, src, dst, radius);

            swap = src;
            src = dst;
            dst = swap;
        }
    }
    else
    {
        int n1 = iter / 2;
        int n2 = iter - n1;

        float r1f = (float)radius * sqrtf((float)n1);
        float r2f = (float)radius * sqrtf((float)n2);

        int r1 = (int)(r1f + 0.5f);
        int r2 = (int)(r2f + 0.5f);

        if (r1 <= 0) r1 = radius;
        if (r2 <= 0) r2 = radius;
        if (r1 > f->max_radius) r1 = f->max_radius;
        if (r2 > f->max_radius) r2 = f->max_radius;

        build_integral(f, src);
        box_blur(f, src, dst, r1);

        build_integral(f, dst);
        box_blur(f, dst, src, r2);
    }

    return src;
}

static void integralblur_mix_plane(integralblur_t *f,
                                   uint8_t *plane,
                                   uint8_t *blurred,
                                   int len,
                                   int mix_q8)
{
    uint8_t *restrict orig = f->orig;
    uint8_t *restrict blur = blurred;
    uint8_t *restrict out = plane;

    if (mix_q8 <= 0)
        return;

    if (mix_q8 >= 255) {
        if (out != blur)
            veejay_memcpy(out, blur, len);
        return;
    }

#pragma omp parallel for schedule(static) num_threads(f->n_threads)
    for (int i = 0; i < len; i++)
        out[i] = ib_u8(ib_mix_u8(orig[i], blur[i], mix_q8));
}

void integralblur_apply(void *ptr, VJFrame *frame, int *args)
{
    integralblur_t *f = (integralblur_t*) ptr;

    int radius_param;
    int base_radius;
    int target_radius;
    int radius;
    int iter;
    int blur_amount;
    int chroma_amount;
    int beat_push;
    int beat_smooth;
    int beat_env;
    int target_mix;
    int mix_q8;
    int chroma_mix_q8;
    int len;

    if (!f || !frame || !args)
        return;

    if (!frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    len = frame->len;
    if (len <= 0)
        return;

    if (len > f->width * f->height)
        len = f->width * f->height;

    radius_param  = ib_clampi(args[P_RADIUS], 0, 1000);
    iter          = ib_clampi(args[P_ITERATIONS], 1, 6);
    blur_amount   = ib_clampi(args[P_BLUR_AMOUNT], 0, 1000);
    chroma_amount = ib_clampi(args[P_CHROMA_AMOUNT], 0, 1000);
    beat_push     = ib_clampi(args[P_BEAT_PUSH], 0, 1000);
    beat_smooth   = ib_clampi(args[P_BEAT_SMOOTH], 0, 1000);

    base_radius = ib_param1000_to_range(radius_param, 1, f->max_radius);

    ib_update_beat_state(f, beat_push, beat_smooth);
    beat_env = ib_clampi((int)(f->beat_env + 0.5f), 0, 1000);

    target_radius = base_radius
                  + (((f->max_radius - base_radius) * beat_env * 42) / 100000)
                  + ((base_radius * beat_env * 80) / 100000);

    if (target_radius > f->max_radius)
        target_radius = f->max_radius;
    if (target_radius < 1)
        target_radius = 1;

    target_mix = blur_amount + (((1000 - blur_amount) * beat_env * 75) / 100000);
    target_mix = ib_clampi(target_mix, 0, 1000);

    if (!f->initialized) {
        f->radius_eff = (float)target_radius;
        f->mix_eff = (float)target_mix;
        f->initialized = 1;
    } else {
        float smooth = (float)beat_smooth * 0.001f;
        float r_attack = 0.34f - smooth * 0.12f;
        float r_release = 0.090f - smooth * 0.052f;
        float m_attack = 0.42f - smooth * 0.16f;
        float m_release = 0.080f - smooth * 0.045f;

        if (r_attack < 0.12f) r_attack = 0.12f;
        if (r_release < 0.020f) r_release = 0.020f;
        if (m_attack < 0.16f) m_attack = 0.16f;
        if (m_release < 0.020f) m_release = 0.020f;

        if ((float)target_radius > f->radius_eff)
            f->radius_eff += ((float)target_radius - f->radius_eff) * r_attack;
        else
            f->radius_eff += ((float)target_radius - f->radius_eff) * r_release;

        if ((float)target_mix > f->mix_eff)
            f->mix_eff += ((float)target_mix - f->mix_eff) * m_attack;
        else
            f->mix_eff += ((float)target_mix - f->mix_eff) * m_release;
    }

    radius = ib_clampi((int)(f->radius_eff + 0.5f), 1, f->max_radius);
    mix_q8 = ib_clampi((int)((f->mix_eff * 255.0f) / 1000.0f + 0.5f), 0, 255);
    chroma_mix_q8 = ib_clampi((mix_q8 * chroma_amount + 500) / 1000, 0, 255);

    for (int p = 0; p < 3; p++)
    {
        uint8_t *blurred;

        veejay_memcpy(f->orig, frame->data[p], len);

        blurred = integralblur_blur_plane(f, frame->data[p], len, radius, iter);

        if (p == 0)
            integralblur_mix_plane(f, frame->data[p], blurred, len, mix_q8);
        else
            integralblur_mix_plane(f, frame->data[p], blurred, len, chroma_mix_q8);
    }
}
