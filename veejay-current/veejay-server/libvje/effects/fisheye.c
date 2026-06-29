/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "fisheye.h"

typedef struct {
    int w;
    int h;
    int len;
    int curve_key;
    int n_threads;
    float *polar_map;
    float *fish_angle;
    int *cached_coords;
    uint8_t *buf[3];
} fisheye_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *fisheye_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 2;
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

    ve->limits[0][0] = -1000; ve->limits[1][0] = 1000; ve->defaults[0] = 1;
    ve->limits[0][1] = 0;     ve->limits[1][1] = 1;    ve->defaults[1] = 0;

    ve->description = "Fish Eye";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Curve", "Mask to Alpha");
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL | FLAG_ALPHA_SRC_A;

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS | VJ_BEAT_F_REBUILDS_STATE, -720,                720,                4,  14, 3600, 9200, 2600, 22,
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                                                          VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *fisheye_malloc(int w, int h)
{
    fisheye_t *f = (fisheye_t*) vj_calloc(sizeof(fisheye_t));

    if(!f)
        return NULL;

    const int len = w * h;

    f->w = w;
    f->h = h;
    f->len = len;
    f->curve_key = 0x7fffffff;
    f->n_threads = vje_advise_num_threads(len);

    f->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len * 3);
    f->polar_map = (float*) vj_malloc(sizeof(float) * (size_t)len);
    f->fish_angle = (float*) vj_malloc(sizeof(float) * (size_t)len);
    f->cached_coords = (int*) vj_malloc(sizeof(int) * (size_t)len);

    if(!f->buf[0] || !f->polar_map || !f->fish_angle || !f->cached_coords) {
        fisheye_free(f);
        return NULL;
    }

    f->buf[1] = f->buf[0] + len;
    f->buf[2] = f->buf[1] + len;

    const int w2 = w >> 1;
    const int h2 = h >> 1;

#pragma omp parallel for schedule(static) num_threads(f->n_threads)
    for(int y = -h2; y < h - h2; y++) {
        const int py = h2 + y;
        const int row = py * w;

        for(int x = -w2; x < w - w2; x++) {
            const int px = w2 + x;
            const int p = row + px;
            f->polar_map[p] = sqrt_approx_f((float)(y * y + x * x));
            f->fish_angle[p] = atan2_approx_f((float)y, (float)x);
            f->cached_coords[p] = p;
        }
    }

    return f;
}

void fisheye_free(void *ptr)
{
    fisheye_t *f = (fisheye_t*) ptr;

    free(f->buf[0]);
    free(f->polar_map);
    free(f->fish_angle);
    free(f->cached_coords);
    free(f);
}

static inline double fisheye_curve_out(double r, double v, double e)
{
    return (exp(r / v) - 1.0) / e;
}

static inline double fisheye_curve_in(double r, double v, double e)
{
    return v * log(1.0 + e * r);
}

static void fisheye_rebuild_map(fisheye_t *f, int curve_key)
{
    const int w = f->w;
    const int h = f->h;
    const int len = f->len;
    const int w2 = w >> 1;
    const int h2 = h >> 1;
    const unsigned int radius = (unsigned int)(h >> 1);
    const int inverse = curve_key < 0;
    int mag = curve_key < 0 ? -curve_key : curve_key;

    if(mag <= 0)
        mag = 1;

    const double curve = 0.001 * (double)mag;
    const double coeff = (double)radius / log(curve * (double)radius + 1.0);
    const float *restrict polar_map = f->polar_map;
    const float *restrict fish_angle = f->fish_angle;
    int *restrict cached_coords = f->cached_coords;

    for(int i = 0; i < len; i++) {
        const float r = polar_map[i];

        if((unsigned int)r > radius) {
            cached_coords[i] = -1;
            continue;
        }

        const float a = fish_angle[i];
        double si;
        double co;
        const double r_dist = inverse ? fisheye_curve_in((double)r, coeff, curve)
                                      : fisheye_curve_out((double)r, coeff, curve);

        sin_cos(si, co, a);

        int px = (int)(r_dist * co) + w2;
        int py = (int)(r_dist * si) + h2;

        if(px < 0)
            px = 0;
        else if(px >= w)
            px = w - 1;

        if(py < 0)
            py = 0;
        else if(py >= h)
            py = h - 1;

        cached_coords[i] = py * w + px;
    }

    f->curve_key = curve_key;
}

void fisheye_apply(void *ptr, VJFrame *frame, int *args)
{
    fisheye_t *f = (fisheye_t*) ptr;

    int curve_key = args[0];
    const int alpha = args[1];
    const int len = f->len;

    if(curve_key == 0)
        curve_key = 1;

    if(curve_key != f->curve_key)
        fisheye_rebuild_map(f, curve_key);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A = frame->data[3];
    const int *restrict cached_coords = f->cached_coords;
    uint8_t **buf = f->buf;

    veejay_memcpy(buf[0], Y, len);

    if(alpha == 0) {
        veejay_memcpy(buf[1], Cb, len);
        veejay_memcpy(buf[2], Cr, len);
    }

#pragma omp parallel for schedule(static) num_threads(f->n_threads)
    for(int i = 0; i < len; i++) {
        const int coord = cached_coords[i];

        if(alpha == 0) {
            if(coord < 0) {
                Y[i] = pixel_Y_lo_;
                Cb[i] = 128;
                Cr[i] = 128;
            }
            else {
                Y[i] = buf[0][coord];
                Cb[i] = buf[1][coord];
                Cr[i] = buf[2][coord];
            }
        }
        else {
            A[i] = (coord < 0) ? 0 : buf[0][coord];
        }
    }
}

