/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "flower.h"

vj_effect *flower_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;
    ve->defaults[0] = 8;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = w/2;
    ve->defaults[1] = h/2;
    ve->description = "Flower";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Petal Count", "Petal Length" );
    return ve;
}

typedef struct 
{
    uint8_t *buf[3];
    float *lut;
    float *atan2_lut;
    float *cos_lut;
    float *sqrt_lut;
    float *exp_lut;
    int last_petal_count;
    int last_petal_length;
} flower_t;

static void init_atan2_lut(flower_t *f, int w, int h, int cx, int cy)
{
    for(int x = 0; x < w; ++x ) {
        for(int y = 0; y < h; ++y ) {
            float angle = atan2f(y - cy,x - cx);
            f->atan2_lut[ y * w + x ] = angle;
        }
    }
}

static void init_cos_lut(flower_t *f, int w, int h, int petal_count)
{
    const int half_w = w >> 1;
    const int half_h = h >> 1;

    for (int x = 0; x < half_w; ++x)
    {
        for (int y = 0; y < half_h; ++y)
        {
            int index = y * w + x;
            float cos_value = cosf(petal_count * f->atan2_lut[index]);

            f->cos_lut[index] = cos_value;
            f->cos_lut[y * w + (w - x - 1)] = cos_value;
            f->cos_lut[(h - y - 1) * w + x] = cos_value;
            f->cos_lut[(h - y - 1) * w + (w - x - 1)] = cos_value;
        }
    }

    if (w % 2 != 0)
    {
        for (int y = 0; y < half_h; ++y)
        {
            int index = y * w + half_w;
            float cos_value = cosf(petal_count * f->atan2_lut[index]);

            f->cos_lut[index] = cos_value;
            f->cos_lut[(h - y - 1) * w + half_w] = cos_value;
        }
    }

    if (h % 2 != 0)
    {
        for (int x = 0; x < half_w; ++x)
        {
            int index = half_h * w + x;
            float cos_value = cosf(petal_count * f->atan2_lut[index]);

            f->cos_lut[index] = cos_value;
            f->cos_lut[half_h * w + (w - x - 1)] = cos_value;
        }
    }

    if (w % 2 != 0 && h % 2 != 0)
    {
        int index = half_h * w + half_w;
        f->cos_lut[index] = cosf(petal_count * f->atan2_lut[index]);
    }

    f->last_petal_count = petal_count;
}

static inline float sqrt_approx(float x) {
    return __builtin_sqrtf(x);
}

static void init_sqrt_lut(flower_t *f, int w, int h, int cx, int cy)
{
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            int dx = x - cx;
            int dy = y - cy;
            float value = sqrt_approx( dx * dx + dy * dy );
            f->sqrt_lut[y * w + x] = value;
        }
    }
}

static void init_exp_lut(flower_t *f, int w, int h, int cx, int cy, int petal_length)
{
    int half_w = w >> 1;
    int half_h = h >> 1;

    for (int x = 0; x < half_w; ++x)
    {
        for (int y = 0; y < half_h; ++y)
        {
            float distance = f->sqrt_lut[y * w + x];
            float exp_value = expf(-distance / petal_length);

            f->exp_lut[y * w + x] = exp_value;
            f->exp_lut[y * w + (w - x - 1)] = exp_value;
            f->exp_lut[(h - y - 1) * w + x] = exp_value;
            f->exp_lut[(h - y - 1) * w + (w - x - 1)] = exp_value;
        }
    }

    if (w % 2 != 0)
    {
        for (int y = 0; y < half_h; ++y)
        {
            float distance = f->sqrt_lut[y * w + half_w];
            float exp_value = expf(-distance / petal_length);

            f->exp_lut[y * w + half_w] = exp_value;
            f->exp_lut[(h - y - 1) * w + half_w] = exp_value;
        }
    }

    if (h % 2 != 0)
    {
        for (int x = 0; x < half_w; ++x)
        {
            float distance = f->sqrt_lut[half_h * w + x];
            float exp_value = expf(-distance / petal_length);

            f->exp_lut[half_h * w + x] = exp_value;
            f->exp_lut[half_h * w + (w - x - 1)] = exp_value;
        }
    }

    if (w % 2 != 0 && h % 2 != 0)
    {
        float distance = f->sqrt_lut[half_h * w + half_w];
        f->exp_lut[half_h * w + half_w] = exp(-distance / petal_length);
    }

    f->last_petal_length = petal_length;
}



void *flower_malloc(int w, int h) {
    flower_t *s = (flower_t*) vj_calloc(sizeof(flower_t));
    if(!s) return NULL;
    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + ( w * h );
    s->buf[2] = s->buf[1] + ( w * h );

    s->lut = (float*) vj_malloc(sizeof(float) * (w * h * 4) );
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->atan2_lut = s->lut;
    s->cos_lut = s->atan2_lut + (w*h);
    s->sqrt_lut = s->cos_lut + (w*h);
    s->exp_lut = s->sqrt_lut + (w*h);

    init_atan2_lut( s, w, h, w/2, h/2 );
    init_sqrt_lut( s, w, h,w/2, h/2 );

    return (void*) s;
}

void flower_free(void *ptr) {
    flower_t *s = (flower_t*) ptr;
    free(s->buf[0]);
    free(s->lut);
    free(s);
}



void flower_apply(void *ptr, VJFrame *frame, int *args) {
    flower_t *s = (flower_t*)ptr;
    const int petalCount = args[0];
    const int petalLength = args[1];

    const int len = frame->width * frame->height;
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    float *restrict cos_lut = s->cos_lut;
    float *restrict exp_lut = s->exp_lut;

    veejay_memcpy( bufY, srcY, len );
    veejay_memcpy( bufU, srcU, len );
    veejay_memcpy( bufV, srcV, len );

    int cx = width >> 1;
    int cy = height >> 1;
 
    if (petalCount != s->last_petal_count) {
        init_cos_lut(s, width,height, petalCount);
    }
    if( petalLength != s->last_petal_length) {
        init_exp_lut(s, width, height, cx, cy, petalLength );
    }

    for (int y_pos = 0; y_pos < height; y_pos++) {
      for (int x_pos = 0; x_pos < width; x_pos++) {
            int pixel_index = y_pos * width + x_pos;

            int dx = x_pos - cx;
            int dy = y_pos - cy;
            
            float petalValue = (1.0 + cos_lut[y_pos * width + x_pos]) * exp_lut[y_pos * width + x_pos];

            int mirroredX = cx + (int)(dx * petalValue);
            int mirroredY = cy + (int)(dy * petalValue);

            mirroredX = mirroredX < 0 ? 0 : (mirroredX >= width ? width - 1 : mirroredX);
            mirroredY = mirroredY < 0 ? 0 : (mirroredY >= height ? height - 1 : mirroredY);

            srcY[pixel_index] = bufY[mirroredY * width + mirroredX];
            srcU[pixel_index] = bufU[mirroredY * width + mirroredX];
            srcV[pixel_index] = bufV[mirroredY * width + mirroredX];
        }
    }
}

