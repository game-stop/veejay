/* 
 * Linux VeeJay
 *
 * Copyright(C)2004-2016 Niels Elburg <nwelburg@gmail.com>
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
#include "morphology.h"

#define MORPHOLOGY_PARAMS 4

#define P_THRESHOLD 0
#define P_KERNEL    1
#define P_MODE      2
#define P_CHANNEL   3

#define MORPH_DILATE 0
#define MORPH_ERODE  1

#define MORPH_CHANNEL_LUMA  0
#define MORPH_CHANNEL_ALPHA 1

typedef struct {
    uint8_t *binary_img;
    int n_threads;
} morphology_t;

static const uint16_t morphology_kernel_bits[8] = {
    0x1ff,
    0x0ba,
    0x038,
    0x092,
    0x054,
    0x111,
    0x007,
    0x1c0
};

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint16_t morphology_neighborhood_bits(const uint8_t *restrict src, int idx, int width)
{
    uint16_t bits = 0;

    bits |= (uint16_t)(src[idx - width - 1] ? 0x001 : 0);
    bits |= (uint16_t)(src[idx - width]     ? 0x002 : 0);
    bits |= (uint16_t)(src[idx - width + 1] ? 0x004 : 0);
    bits |= (uint16_t)(src[idx - 1]         ? 0x008 : 0);
    bits |= (uint16_t)(src[idx]             ? 0x010 : 0);
    bits |= (uint16_t)(src[idx + 1]         ? 0x020 : 0);
    bits |= (uint16_t)(src[idx + width - 1] ? 0x040 : 0);
    bits |= (uint16_t)(src[idx + width]     ? 0x080 : 0);
    bits |= (uint16_t)(src[idx + width + 1] ? 0x100 : 0);

    return bits;
}

vj_effect *morphology_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MORPHOLOGY_PARAMS;
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

    ve->limits[0][P_THRESHOLD] = 0; ve->limits[1][P_THRESHOLD] = 255; ve->defaults[P_THRESHOLD] = 140;
    ve->limits[0][P_KERNEL] = 0;    ve->limits[1][P_KERNEL] = 7;     ve->defaults[P_KERNEL] = 0;
    ve->limits[0][P_MODE] = 0;      ve->limits[1][P_MODE] = 1;       ve->defaults[P_MODE] = MORPH_DILATE;
    ve->limits[0][P_CHANNEL] = 0;   ve->limits[1][P_CHANNEL] = 1;    ve->defaults[P_CHANNEL] = MORPH_CHANNEL_LUMA;

    ve->description = "Morphology (Erosion/Dilation)";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A | FLAG_ALPHA_OPTIONAL;
    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "Convolution Kernel", "Mode", "Channel");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_KERNEL], P_KERNEL,
                              "[1,1,1],[1,1,1],[1,1,1]",
                              "[0,1,0],[1,1,1],[0,1,0]",
                              "[0,0,0],[1,1,1],[0,0,0]",
                              "[0,1,0],[0,1,0],[0,1,0]",
                              "[0,0,1],[0,1,0],[1,0,0]",
                              "[1,0,0],[0,1,0],[0,0,1]",
                              "[1,1,1],[0,0,0],[0,0,0]",
                              "[0,0,0],[0,0,0],[1,1,1]");

    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Dilate", "Erode");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_CHANNEL], P_CHANNEL, "Luminance", "Alpha");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 18,                 225,                14, 54,  800, 3000, 0,    82,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *morphology_malloc(int w, int h)
{
    morphology_t *m = (morphology_t*) vj_calloc(sizeof(morphology_t));

    if(!m)
        return NULL;

    m->binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)w * (size_t)h);

    if(!m->binary_img) {
        free(m);
        return NULL;
    }

    m->n_threads = vje_advise_num_threads(w * h);

    return (void*) m;
}

void morphology_free(void *ptr)
{
    morphology_t *m = (morphology_t*) ptr;

    free(m->binary_img);
    free(m);
}

static void morphology_threshold_image(uint8_t *restrict binary_img,
                                       const uint8_t *restrict src,
                                       int len,
                                       int threshold)
{
#pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
        binary_img[i] = (src[i] < threshold) ? 0 : 255;
}

static void morphology_dilate(uint8_t *restrict dst,
                              const uint8_t *restrict binary_img,
                              int width,
                              int height,
                              uint16_t kernel)
{
#pragma omp for schedule(static)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;

        for(int x = 1; x < width - 1; x++) {
            const int idx = row + x;
            const uint16_t bits = morphology_neighborhood_bits(binary_img, idx, width);

            dst[idx] = (bits & kernel) ? 255 : 0;
        }
    }
}

static void morphology_erode(uint8_t *restrict dst,
                             const uint8_t *restrict binary_img,
                             int width,
                             int height,
                             uint16_t kernel)
{
#pragma omp for schedule(static)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;

        for(int x = 1; x < width - 1; x++) {
            const int idx = row + x;
            const uint16_t bits = morphology_neighborhood_bits(binary_img, idx, width);

            dst[idx] = ((bits & kernel) == kernel) ? 255 : 0;
        }
    }
}
void morphology_apply(void *ptr, VJFrame *frame, int *args)
{
    morphology_t *m = (morphology_t*) ptr;

    const int threshold = clampi(args[P_THRESHOLD], 0, 255);
    const int convolution_kernel = clampi(args[P_KERNEL], 0, 7);
    const int mode = clampi(args[P_MODE], 0, 1);
    const int channel = clampi(args[P_CHANNEL], 0, 1);
    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;
    const int uv_len = frame->uv_len;
    const uint16_t kernel = morphology_kernel_bits[convolution_kernel];

    uint8_t *restrict dst = channel == MORPH_CHANNEL_ALPHA ? frame->data[3] : frame->data[0];
    uint8_t *restrict binary_img = m->binary_img;

    if(threshold == 0)
        veejay_memcpy(binary_img, dst, len);

    if(channel == MORPH_CHANNEL_LUMA) {
        veejay_memset(frame->data[1], 128, uv_len);
        veejay_memset(frame->data[2], 128, uv_len);
    }

#pragma omp parallel num_threads(m->n_threads)
    {
        if(threshold != 0)
            morphology_threshold_image(binary_img, dst, len, threshold);

        if(mode == MORPH_DILATE)
            morphology_dilate(dst, binary_img, width, height, kernel);
        else
            morphology_erode(dst, binary_img, width, height, kernel);
    }
}
