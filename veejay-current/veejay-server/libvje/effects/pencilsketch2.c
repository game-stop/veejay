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
#include "pencilsketch2.h"

#define PS2_CLAMP_8BIT(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

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

static void rebuild_master_lut(pencilsketch_t *p, int gamma_arg, int contrast, int levels)
{
    uint8_t gamma_table[256];

    if(gamma_arg < 1)
        gamma_arg = 1;

    const double gamma_val = (double)gamma_arg / 1000.0;

    for(int i = 0; i < 256; i++) {
        double val = pow((double)i / 255.0, gamma_val) * 255.0;
        gamma_table[i] = (uint8_t)PS2_CLAMP_8BIT((int)(val + 0.5));
    }

    for(int orig = 0; orig < 256; orig++) {
        for(int blur_inv = 0; blur_inv < 256; blur_inv++) {
            int result;

            if(blur_inv >= 255) {
                result = 255;
            } else {
                result = (orig * 255) / (255 - blur_inv);
                result = PS2_CLAMP_8BIT(result);
            }

            if(levels > 1) {
                int factor = 256 / levels;
                if(factor < 1)
                    factor = 1;
                result = result - (result % factor);
            }

            if(contrast > 0) {
                int m = result - 128;
                m = (m * contrast + 50) / 100;
                result = PS2_CLAMP_8BIT(m + 128);
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
    ve->num_params = 6;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 3;
    ve->limits[1][0] = 128;
    ve->defaults[0] = 24;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 9000;
    ve->defaults[1] = 1000;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[2] = 0;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->defaults[3] = 0;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;
    ve->defaults[4] = 0;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 1;
    ve->defaults[5] = 1;

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

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                                 6,                  72,                 6, 22, 1800, 4200, 900, 30,    /* Blur Radius */
        VJ_BEAT_DETAIL,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE,       450,                2400,               6, 22, 1800, 4200, 900, 30,    /* Gamma Compression */
        VJ_BEAT_DETAIL,        VJ_BEAT_F_CONTINUOUS,                                                       0,                  180,                8, 30, 1200, 3000, 0,   45,    /* Strength */
        VJ_BEAT_CONTRAST,      VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE,       0,                  180,                6, 22, 1800, 4200, 900, 30,    /* Contrast */
        VJ_BEAT_DETAIL,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE,       0,                  24,                 6, 22, 1600, 3400, 700, 30,    /* Levels */
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Grayscale */
    );

    (void) w;
    (void) h;

    return ve;
}

void pencilsketch2_free(void *ptr)
{
    pencilsketch_t *p = (pencilsketch_t*) ptr;
    if(p) {
        if(p->blur_tmp)
            free(p->blur_tmp);
        if(p->blur_final)
            free(p->blur_final);
        if(p->histogram_)
            veejay_histogram_del(p->histogram_);
        free(p);
    }
}

void *pencilsketch2_malloc(int w, int h)
{
    pencilsketch_t *p = (pencilsketch_t*) vj_calloc(sizeof(pencilsketch_t));
    if(!p)
        return NULL;

    const int len = w * h;

    p->blur_tmp = (uint8_t*) vj_malloc(sizeof(uint8_t) * len);
    p->blur_final = (uint8_t*) vj_malloc(sizeof(uint8_t) * len);

    if(!p->blur_tmp || !p->blur_final) {
        pencilsketch2_free(p);
        return NULL;
    }

    p->histogram_ = veejay_histogram_new();
    if(!p->histogram_) {
        pencilsketch2_free(p);
        return NULL;
    }

    p->n_threads = vje_advise_num_threads(len);
    if(p->n_threads < 1)
        p->n_threads = 1;

    p->prev_gamma_arg = -1;
    p->prev_contrast = -1;
    p->prev_levels = -1;

    return (void*) p;
}

static void rhblur_apply(uint8_t *dst, const uint8_t *src, int w, int h, int r, int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < h; y++) {
        veejay_blur(dst + y * w, src + y * w, w, r, 1, 1);
    }
}

static void rvblur_apply(uint8_t *dst, const uint8_t *src, int w, int h, int r, int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int x = 0; x < w; x++) {
        veejay_blur(dst + x, src + x, h, r, w, w);
    }
}

static inline int ps2_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

void pencilsketch2_apply(void *ptr, VJFrame *frame, int *args)
{
    pencilsketch_t *p = (pencilsketch_t*) ptr;
    if(!p || !frame || !args)
        return;

    const int len = frame->len;
    const int w = frame->width;
    const int h = frame->height;

    if(len <= 0 || w <= 0 || h <= 0)
        return;

    int radius = ps2_clampi(args[0], 3, 128);
    int gamma_arg = ps2_clampi(args[1], 1, 9000);
    int strength = ps2_clampi(args[2], 0, 255);
    int contrast = ps2_clampi(args[3], 0, 255);
    int levels = ps2_clampi(args[4], 0, 255);
    int grayscale = ps2_clampi(args[5], 0, 1);

    if(radius > w)
        radius = w;
    if(radius > h)
        radius = h;

    if(gamma_arg != p->prev_gamma_arg || contrast != p->prev_contrast || levels != p->prev_levels) {
        rebuild_master_lut(p, gamma_arg, contrast, levels);
    }

    uint8_t *restrict y_plane = frame->data[0];
    uint8_t *restrict tmp_buf = p->blur_tmp;
    uint8_t *restrict blur_buf = p->blur_final;

#pragma omp parallel for schedule(static) num_threads(p->n_threads)
    for(int i = 0; i < len; i++) {
        tmp_buf[i] = 0xff - y_plane[i];
    }

    rhblur_apply(blur_buf, tmp_buf, w, h, radius, p->n_threads);
    rvblur_apply(tmp_buf, blur_buf, w, h, radius, p->n_threads);
    rhblur_apply(blur_buf, tmp_buf, w, h, radius, p->n_threads);
    rvblur_apply(tmp_buf, blur_buf, w, h, radius, p->n_threads);

#pragma omp parallel for schedule(static) num_threads(p->n_threads)
    for(int i = 0; i < len; i++) {
        y_plane[i] = p->master_lut[y_plane[i]][tmp_buf[i]];
    }

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