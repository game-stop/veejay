/* 
 * Linux VeeJay
 *
 * Copyright(C)2006 Niels Elburg <nwelburg@gmail.com>
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
#include "colmorphology.h"

static const uint8_t kernels[8][9] = {
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0x00, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0x00 },
    { 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 },
    { 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00 },
    { 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00 },
    { 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff },
    { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff }
};

typedef struct {
    uint8_t *binary_img;
    int n_threads;
} colmorph_t;

vj_effect *colmorphology_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 255; ve->defaults[0] = 140;
    ve->limits[0][1] = 0; ve->limits[1][1] = 7;   ve->defaults[1] = 1;
    ve->limits[0][2] = 0; ve->limits[1][2] = 1;   ve->defaults[2] = 0;

    ve->description = "Colored Morphology";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "Kernel", "Dilate or Erode");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 64,                 220,                12, 46,  900, 3200, 0,    68,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *colmorphology_malloc(int w, int h)
{
    colmorph_t *c = (colmorph_t*) vj_calloc(sizeof(colmorph_t));

    if(!c)
        return NULL;

    c->binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w * h));

    if(!c->binary_img) {
        free(c);
        return NULL;
    }

    c->n_threads = vje_advise_num_threads(w * h);
    return c;
}

void colmorphology_free(void *ptr)
{
    colmorph_t *c = (colmorph_t*) ptr;

    if(!c)
        return;

    if(c->binary_img)
        free(c->binary_img);

    free(c);
}

static inline uint8_t do_dilate(const uint8_t *k, const uint8_t *r0, const uint8_t *r1, const uint8_t *r2, int x)
{
    uint8_t match = (k[0] & r0[x - 1]) | (k[1] & r0[x]) | (k[2] & r0[x + 1]) |
                    (k[3] & r1[x - 1]) | (k[4] & r1[x]) | (k[5] & r1[x + 1]) |
                    (k[6] & r2[x - 1]) | (k[7] & r2[x]) | (k[8] & r2[x + 1]);

    return match ? pixel_Y_hi_ : pixel_Y_lo_;
}

static inline uint8_t do_erode(const uint8_t *k, const uint8_t *r0, const uint8_t *r1, const uint8_t *r2, int x)
{
    uint8_t shrink = (k[0] & ~r0[x - 1]) | (k[1] & ~r0[x]) | (k[2] & ~r0[x + 1]) |
                     (k[3] & ~r1[x - 1]) | (k[4] & ~r1[x]) | (k[5] & ~r1[x + 1]) |
                     (k[6] & ~r2[x - 1]) | (k[7] & ~r2[x]) | (k[8] & ~r2[x + 1]);

    return shrink ? pixel_Y_lo_ : pixel_Y_hi_;
}

void colmorphology_apply(void *ptr, VJFrame *frame, int *args)
{
    colmorph_t *c = (colmorph_t*) ptr;
    const int threshold = args[0];
    const int type = args[1];
    const int erode = args[2];
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict binary_img = c->binary_img;
    const uint8_t *restrict k = kernels[type];

    if(width < 3 || height < 3) {
        veejay_memset(Y, pixel_Y_lo_, len);
        return;
    }

#pragma omp parallel num_threads(c->n_threads)
    {
#pragma omp for simd schedule(static)
        for(int i = 0; i < len; i++)
            binary_img[i] = Y[i] < threshold ? 0x00 : 0xff;

#pragma omp for simd schedule(static)
        for(int x = 0; x < width; x++)
        {
            Y[x] = pixel_Y_lo_;
            Y[len - width + x] = pixel_Y_lo_;
        }

        if(!erode)
        {
#pragma omp for schedule(static)
            for(int y = 1; y < height - 1; y++)
            {
                const uint8_t *restrict r0 = binary_img + (y - 1) * width;
                const uint8_t *restrict r1 = binary_img + y * width;
                const uint8_t *restrict r2 = binary_img + (y + 1) * width;
                uint8_t *restrict dst_row = Y + y * width;

                dst_row[0] = pixel_Y_lo_;
                dst_row[width - 1] = pixel_Y_lo_;

                for(int x = 1; x < width - 1; x++)
                    dst_row[x] = r1[x] == 0xff ? pixel_Y_hi_ : do_dilate(k, r0, r1, r2, x);
            }
        }
        else
        {
#pragma omp for schedule(static)
            for(int y = 1; y < height - 1; y++)
            {
                const uint8_t *restrict r0 = binary_img + (y - 1) * width;
                const uint8_t *restrict r1 = binary_img + y * width;
                const uint8_t *restrict r2 = binary_img + (y + 1) * width;
                uint8_t *restrict dst_row = Y + y * width;

                dst_row[0] = pixel_Y_lo_;
                dst_row[width - 1] = pixel_Y_lo_;

                for(int x = 1; x < width - 1; x++)
                    dst_row[x] = r1[x] == 0x00 ? pixel_Y_lo_ : do_erode(k, r0, r1, r2, x);
            }
        }
    }
}

