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
#include <veejaycore/vjmem.h>
#include <math.h>
#include <omp.h>
#include "pencilsketch2.h"

#define CLAMP_8BIT(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

typedef struct {
    uint8_t *blur_tmp;
    uint8_t *blur_final;
    void *histogram_;
    
    // 2D LUT for unifying Dodge, Posterize, Contrast, and Gamma
    uint8_t master_lut[256][256]; 
    
    // State tracking to prevent unnecessary LUT rebuilds
    double prev_gamma;
    int prev_contrast;
    int prev_levels;
    int n_threads;
} pencilsketch_t;

vj_effect *pencilsketch2_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    
    ve->limits[0][0] = 3;   ve->limits[1][0] = 128;   // Blur Radius
    ve->limits[0][1] = 0;   ve->limits[1][1] = 9000;  // Gamma
    ve->limits[0][2] = 0;   ve->limits[1][2] = 255;   // Strength (Equalizer)
    ve->limits[0][3] = 0;   ve->limits[1][3] = 255;   // Contrast
    ve->limits[0][4] = 0;   ve->limits[1][4] = 255;   // Levels
    ve->limits[0][5] = 0;   ve->limits[1][5] = 1;     // Grayscale

    ve->defaults[0] = 24;   ve->defaults[1] = 1000;
    ve->defaults[2] = 0;    ve->defaults[3] = 0;
    ve->defaults[4] = 0;    ve->defaults[5] = 1;
    
    ve->description = "Sketchify Optimized";
    ve->sub_format = -1;
    ve->param_description = vje_build_param_list(ve->num_params, "Blur Radius", "Gamma Compression", "Strength", "Contrast", "Levels", "Grayscale");
    return ve;
}

// Builds the 256x256 LUT. This takes fractions of a millisecond and only triggers when parameters change.
static void rebuild_master_lut(pencilsketch_t *p, double gamma_val, int contrast, int levels) 
{
    uint8_t gamma_table[256];
    int i, orig, blur_inv;

    // Precompute 1D Gamma
    for (i = 0; i < 256; i++) {
        double val = pow((double)i / 255.0, gamma_val) * 255.0;
        gamma_table[i] = (uint8_t)CLAMP_8BIT((int)val);
    }

    // Build 2D Master LUT: master_lut[original_pixel][blurred_inverted_pixel]
    for (orig = 0; orig < 256; orig++) {
        for (blur_inv = 0; blur_inv < 256; blur_inv++) {
            int result;

            // 1. Dodge Blend
            if (blur_inv == 255) {
                result = 255;
            } else {
                result = (orig * 255) / (255 - blur_inv);
                result = CLAMP_8BIT(result);
            }

            // 2. Posterize
            if (levels > 0) {
                int factor = 256 / levels;
                result = result - (result % factor);
            }

            // 3. Contrast
            if (contrast > 0) {
                int m = result - 128;
                m *= contrast;
                m = (m + 50) / 100; // Integer division rounding
                m += 128;
                result = CLAMP_8BIT(m);
            }

            // 4. Gamma
            result = gamma_table[result];

            p->master_lut[orig][blur_inv] = (uint8_t)result;
        }
    }

    p->prev_gamma = gamma_val;
    p->prev_contrast = contrast;
    p->prev_levels = levels;
}

void *pencilsketch2_malloc(int w, int h) {
    pencilsketch_t *p = (pencilsketch_t*) vj_calloc(sizeof(pencilsketch_t));
    if(!p) return NULL;

    // We only need two frame-sized buffers now, saving memory
    p->blur_tmp = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w * h));
    p->blur_final = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w * h));
    
    if(!p->blur_tmp || !p->blur_final) {
        pencilsketch2_free(p);
        return NULL;
    }

    p->histogram_ = veejay_histogram_new();
    p->n_threads = vje_advise_num_threads(w*h);
    
    // Force initial LUT build
    p->prev_gamma = -1.0; 
    
    return (void*) p;
}

void pencilsketch2_free(void *ptr) {
    pencilsketch_t *p = (pencilsketch_t*) ptr;
    if(p) {
        if(p->blur_tmp) free(p->blur_tmp);
        if(p->blur_final) free(p->blur_final);
        if(p->histogram_) veejay_histogram_del(p->histogram_);
        free(p);
    }
}

// Multithreaded Horizontal Blur wrapper
static void rhblur_apply(uint8_t *dst, const uint8_t *src, int w, int h, int r) {
    int y;
    #pragma omp parallel for schedule(dynamic)
    for(y = 0; y < h; y++) {
        veejay_blur(dst + y * w, src + y * w, w, r, 1, 1);
    }       
}

// Multithreaded Vertical Blur wrapper
static void rvblur_apply(uint8_t *dst, const uint8_t *src, int w, int h, int r) {
    int x;
    #pragma omp parallel for schedule(dynamic)
    for(x = 0; x < w; x++) {
        veejay_blur(dst + x, src + x, h, r, w, w);
    }
}

void pencilsketch2_apply(void *ptr, VJFrame *frame, int *args) {
    pencilsketch_t *p = (pencilsketch_t*) ptr;
    
    int radius    = args[0];
    double g_val  = (double)args[1] / 1000.0;
    int strength  = args[2];
    int contrast  = args[3];
    int levels    = args[4];
    int grayscale = args[5];

    // 1. Rebuild Master LUT only if parameters changed
    if (g_val != p->prev_gamma || contrast != p->prev_contrast || levels != p->prev_levels) {
        rebuild_master_lut(p, g_val, contrast, levels);
    }

    uint8_t *y_plane = frame->data[0];
    uint8_t *tmp_buf = p->blur_tmp;
    uint8_t *blur_buf = p->blur_final;
    int len = frame->len;
    int w = frame->width;
    int h = frame->height;
    int i;

    // 2. Negate into tmp buffer
    #pragma omp parallel for simd
    for(i = 0; i < len; i++) {
        tmp_buf[i] = 0xFF - y_plane[i];
    }

    // 3. Double-Pass Separable Blur (Approximates Gaussian for much better visual quality)
    // Pass 1
    rhblur_apply(blur_buf, tmp_buf, w, h, radius);
    rvblur_apply(tmp_buf, blur_buf, w, h, radius);
    // Pass 2
    rhblur_apply(blur_buf, tmp_buf, w, h, radius);
    rvblur_apply(tmp_buf, blur_buf, w, h, radius);

    // 4. Unified Processing: Dodge + Posterize + Contrast + Gamma
    // We apply the pre-calculated 2D LUT to map the original and blurred pixels.
    #pragma omp parallel for simd
    for(i = 0; i < len; i++) {
        y_plane[i] = p->master_lut[ y_plane[i] ][ tmp_buf[i] ];
    }

    // 5. Histogram Equalization (Must be run after the base image is formed)
    if(strength > 0) {
        veejay_histogram_analyze(p->histogram_, frame, 0);
        veejay_histogram_equalize(p->histogram_, frame, 0xff, strength);
    }

    // 6. Grayscale handling
    if(grayscale) {
        veejay_memset(frame->data[1], 128, frame->uv_len);
        veejay_memset(frame->data[2], 128, frame->uv_len);
    }
}