/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vjmem.h>
#include <math.h>
#include "pencilsketch2.h"

#define PENCILSKETCH2_PARAMS 6

#define P_RADIUS    0
#define P_GAMMA     1
#define P_STRENGTH  2
#define P_CONTRAST  3
#define P_LEVELS    4
#define P_GRAYSCALE 5

typedef struct {
    uint8_t *blur_tmp;
    uint8_t *blur_final;
    void *histogram_;
    uint8_t master_lut[256][256];
    int prev_gamma_arg;
    int prev_contrast;
    int prev_levels;
    int n_threads;
} pencilsketch_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t ps2_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static void rebuild_master_lut(pencilsketch_t *p, int gamma_arg, int contrast, int levels)
{
    uint8_t gamma_table[256];
    const double gamma_val = (double)gamma_arg / 1000.0;

    for(int i = 0; i < 256; i++) {
        const double val = pow((double)i * (1.0 / 255.0), gamma_val) * 255.0;

        gamma_table[i] = ps2_u8((int)(val + 0.5));
    }

    for(int orig = 0; orig < 256; orig++) {
        for(int blur_inv = 0; blur_inv < 256; blur_inv++) {
            int result;

            if(blur_inv >= 255)
                result = 255;
            else
                result = ps2_u8((orig * 255) / (255 - blur_inv));

            if(levels > 1) {
                int factor = 256 / levels;

                if(factor < 1)
                    factor = 1;

                result -= result % factor;
            }

            if(contrast > 0) {
                const int m = result - 128;

                result = ps2_u8(((m * contrast + 50) / 100) + 128);
            }

            p->master_lut[orig][blur_inv] = gamma_table[result];
        }
    }

    p->prev_gamma_arg = gamma_arg;
    p->prev_contrast = contrast;
    p->prev_levels = levels;
}

vj_effect *pencilsketch2_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = PENCILSKETCH2_PARAMS;
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

    ve->limits[0][P_RADIUS] = 3;    ve->limits[1][P_RADIUS] = 128;    ve->defaults[P_RADIUS] = 24;
    ve->limits[0][P_GAMMA] = 1;     ve->limits[1][P_GAMMA] = 9000;    ve->defaults[P_GAMMA] = 1000;
    ve->limits[0][P_STRENGTH] = 0;  ve->limits[1][P_STRENGTH] = 255;  ve->defaults[P_STRENGTH] = 0;
    ve->limits[0][P_CONTRAST] = 0;  ve->limits[1][P_CONTRAST] = 255;  ve->defaults[P_CONTRAST] = 0;
    ve->limits[0][P_LEVELS] = 0;    ve->limits[1][P_LEVELS] = 255;    ve->defaults[P_LEVELS] = 0;
    ve->limits[0][P_GRAYSCALE] = 0; ve->limits[1][P_GRAYSCALE] = 1;   ve->defaults[P_GRAYSCALE] = 1;

    ve->description = "Sketchify";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Blur Radius",
        "Gamma Compression",
        "Strength",
        "Contrast",
        "Levels",
        "Grayscale"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_GRAYSCALE], P_GRAYSCALE, "Color", "Grayscale");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,                            10,                 72,                 4,  14, 3000, 8200, 2200, 22,
        VJ_BEAT_DETAIL,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, 520,                2400,               4,  14, 3200, 8600, 2400, 18,
        VJ_BEAT_INTENSITY,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                                 8,                  210,                16, 62,  700, 2800, 0,    86,
        VJ_BEAT_CONTRAST,      VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE,                           0,                  190,                4,  14, 3200, 8600, 2400, 20,
        VJ_BEAT_DETAIL,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, 2,                  36,                 4,  14, 3600, 9200, 2600, 16,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                                       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );
    return ve;
}

void pencilsketch2_free(void *ptr)
{
    pencilsketch_t *p = (pencilsketch_t*) ptr;

    free(p->blur_tmp);
    veejay_histogram_del(p->histogram_);
    free(p);
}

void *pencilsketch2_malloc(int w, int h)
{
    pencilsketch_t *p = (pencilsketch_t*) vj_calloc(sizeof(pencilsketch_t));

    if(!p)
        return NULL;

    const int len = w * h;

    p->blur_tmp = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len * 2u);

    if(!p->blur_tmp) {
        free(p);
        return NULL;
    }

    p->blur_final = p->blur_tmp + len;
    p->histogram_ = veejay_histogram_new();

    if(!p->histogram_) {
        free(p->blur_tmp);
        free(p);
        return NULL;
    }

    p->n_threads = vje_advise_num_threads(len);
    p->prev_gamma_arg = -1;
    p->prev_contrast = -1;
    p->prev_levels = -1;

    return (void*) p;
}

static void ps2_hblur(uint8_t *restrict dst,
                      const uint8_t *restrict src,
                      int w,
                      int h,
                      int radius,
                      int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < h; y++)
        veejay_blur(dst + y * w, src + y * w, w, radius, 1, 1);
}

static void ps2_vblur(uint8_t *restrict dst,
                      const uint8_t *restrict src,
                      int w,
                      int h,
                      int radius,
                      int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int x = 0; x < w; x++)
        veejay_blur(dst + x, src + x, h, radius, w, w);
}

void pencilsketch2_apply(void *ptr, VJFrame *frame, int *args)
{
    pencilsketch_t *p = (pencilsketch_t*) ptr;

    const int len = frame->len;
    const int w = frame->width;
    const int h = frame->height;
    int radius = args[P_RADIUS];
    const int gamma_arg = args[P_GAMMA];
    const int strength = args[P_STRENGTH];
    const int contrast = args[P_CONTRAST];
    const int levels = args[P_LEVELS];
    const int grayscale = args[P_GRAYSCALE];;

    if(radius > w)
        radius = w;
    if(radius > h)
        radius = h;

    if(gamma_arg != p->prev_gamma_arg || contrast != p->prev_contrast || levels != p->prev_levels)
        rebuild_master_lut(p, gamma_arg, contrast, levels);

    uint8_t *restrict y_plane = frame->data[0];
    uint8_t *restrict tmp_buf = p->blur_tmp;
    uint8_t *restrict blur_buf = p->blur_final;

#pragma omp parallel for schedule(static) num_threads(p->n_threads)
    for(int i = 0; i < len; i++)
        tmp_buf[i] = (uint8_t)(255 - y_plane[i]);

    ps2_hblur(blur_buf, tmp_buf, w, h, radius, p->n_threads);
    ps2_vblur(tmp_buf, blur_buf, w, h, radius, p->n_threads);
    ps2_hblur(blur_buf, tmp_buf, w, h, radius, p->n_threads);
    ps2_vblur(tmp_buf, blur_buf, w, h, radius, p->n_threads);

#pragma omp parallel for schedule(static) num_threads(p->n_threads)
    for(int i = 0; i < len; i++)
        y_plane[i] = p->master_lut[y_plane[i]][tmp_buf[i]];

    if(strength > 0) {
        veejay_histogram_analyze(p->histogram_, frame, 0);
        veejay_histogram_equalize(p->histogram_, frame, 0xff, strength);
    }

    if(grayscale) {
        const int uv_len = frame->ssm ? len : frame->uv_len;

        veejay_memset(frame->data[1], 128, uv_len);
        veejay_memset(frame->data[2], 128, uv_len);
    }
}
