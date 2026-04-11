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
#include <veejaycore/vjmem.h>
#include "colmorphology.h"

static const uint8_t kernels[8][9] = {
    { 0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF }, // square
    { 0x00, 0xFF, 0x00,  0xFF, 0xFF, 0xFF,  0x00, 0xFF, 0x00 }, // cross
    { 0x00, 0x00, 0x00,  0xFF, 0xFF, 0xFF,  0x00, 0x00, 0x00 }, // horizontal
    { 0x00, 0xFF, 0x00,  0x00, 0xFF, 0x00,  0x00, 0xFF, 0x00 }, // vertical
    { 0x00, 0x00, 0xFF,  0x00, 0xFF, 0x00,  0xFF, 0x00, 0x00 }, // diagonal 1
    { 0xFF, 0x00, 0x00,  0x00, 0xFF, 0x00,  0x00, 0x00, 0xFF }, // diagonal 2
    { 0xFF, 0xFF, 0xFF,  0x00, 0x00, 0x00,  0x00, 0x00, 0x00 }, // top edge
    { 0x00, 0x00, 0x00,  0x00, 0x00, 0x00,  0xFF, 0xFF, 0xFF }  // bottom edge
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

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 7;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[0] = 140;
    ve->defaults[1] = 1;
    ve->defaults[2] = 0;

    ve->description = "Colored Morphology";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "Kernel", "Dilate or Erode");
    return ve;
}

void *colmorphology_malloc(int w, int h)
{
    colmorph_t *c = (colmorph_t*) vj_calloc(sizeof(colmorph_t));
    if(!c) return NULL;

    c->binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w * h));
    if(!c->binary_img) {
        free(c);
        return NULL;
    }

	c->n_threads = vje_advise_num_threads(w*h);
    return (void*) c;
}

void colmorphology_free(void *ptr)
{
    colmorph_t *c = (colmorph_t*) ptr;
    free(c->binary_img);
    free(c);
}

static inline uint8_t do_dilate(const uint8_t *k, const uint8_t *r0, const uint8_t *r1, const uint8_t *r2, int x)
{
    uint8_t match = (k[0] & r0[x-1]) | (k[1] & r0[x]) | (k[2] & r0[x+1]) |
                    (k[3] & r1[x-1]) | (k[4] & r1[x]) | (k[5] & r1[x+1]) |
                    (k[6] & r2[x-1]) | (k[7] & r2[x]) | (k[8] & r2[x+1]);
    return match ? pixel_Y_hi_ : pixel_Y_lo_;
}

static inline uint8_t do_erode(const uint8_t *k, const uint8_t *r0, const uint8_t *r1, const uint8_t *r2, int x)
{
    uint8_t shrink = (k[0] & ~r0[x-1]) | (k[1] & ~r0[x]) | (k[2] & ~r0[x+1]) |
                     (k[3] & ~r1[x-1]) | (k[4] & ~r1[x]) | (k[5] & ~r1[x+1]) |
                     (k[6] & ~r2[x-1]) | (k[7] & ~r2[x]) | (k[8] & ~r2[x+1]);
    return shrink ? pixel_Y_lo_ : pixel_Y_hi_;
}

void colmorphology_apply(void *ptr, VJFrame *frame, int *args)
{
    int threshold = args[0];
    int type = args[1] % 8;
    int passes = args[2];

    colmorph_t *c = (colmorph_t*) ptr;
    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    int len = frame->len;

    uint8_t *Y = frame->data[0];
    uint8_t *binary_img = c->binary_img;
    const uint8_t *k = kernels[type];

    #pragma omp simd
    for(int i = 0; i < len; i++) {
        binary_img[i] = (Y[i] < threshold) ? 0x00 : 0xFF;
    }

    for(int i = 0; i < width; i++) {
        Y[i] = pixel_Y_lo_;
        Y[len - width + i] = pixel_Y_lo_;
    }

    if (passes == 0)
    {
        #pragma omp parallel for schedule(static) num_threads(c->n_threads)
        for(int y = 1; y < height - 1; y++)
        {
            const uint8_t *r0 = binary_img + (y - 1) * width;
            const uint8_t *r1 = binary_img + y * width;
            const uint8_t *r2 = binary_img + (y + 1) * width;
            uint8_t *dst_row  = Y + y * width;

            dst_row[0] = pixel_Y_lo_;
            dst_row[width - 1] = pixel_Y_lo_;

            for(int x = 1; x < width - 1; x++)
            {
                if (r1[x] == 0xFF) {
                    dst_row[x] = pixel_Y_hi_;
                } else {
                    dst_row[x] = do_dilate(k, r0, r1, r2, x);
                }
            }
        }
    }
    else
    {
        #pragma omp parallel for schedule(static) num_threads(c->n_threads)
        for(int y = 1; y < height - 1; y++)
        {
            const uint8_t *r0 = binary_img + (y - 1) * width;
            const uint8_t *r1 = binary_img + y * width;
            const uint8_t *r2 = binary_img + (y + 1) * width;
            uint8_t *dst_row  = Y + y * width;

            dst_row[0] = pixel_Y_lo_;
            dst_row[width - 1] = pixel_Y_lo_;

            for(int x = 1; x < width - 1; x++)
            {
                if (r1[x] == 0x00) {
                    dst_row[x] = pixel_Y_lo_;
                } else {
                    dst_row[x] = do_erode(k, r0, r1, r2, x);
                }
            }
        }
    }
}