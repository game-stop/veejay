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
    double *lut;
    double *atan2_lut;
    double *cos_lut;
    double *sqrt_lut;
    double *exp_lut;
    int last_petal_count;
    int last_petal_length;
} flower_t;

static void init_atan2_lut(flower_t *f, int w, int h, int cx, int cy)
{
    for(int x = 0; x < w; ++x ) {
        for(int y = 0; y < h; ++y ) {
            double angle = atan2(y - cy,x - cx);
            f->atan2_lut[ y * w + x ] = angle;
        }
    }
}

static void init_cos_lut(flower_t *f, int w, int h, int petal_count)
{
    const int size = w * h;
    for(int i = 0; i < size; ++i) {
        f->cos_lut[i] = cos(petal_count * f->atan2_lut[i]);
    }
    f->last_petal_count = petal_count;

}

static void init_sqrt_lut(flower_t *f, int w, int h, int cx, int cy)
{
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            int dx = x - cx;
            int dy = y - cy;
            double value;
            fast_sqrt(value, dx * dx + dy * dy );
            f->sqrt_lut[y * w + x] = value;
        }
    }
}

static void init_exp_lut(flower_t *f, int w, int h, int cx, int cy, int petal_length)
{
    for (int x = 0; x < w; ++x)
    {
        for (int y = 0; y < h; ++y)
        {
            double distance = f->sqrt_lut[ y * w + x ];
            f->exp_lut[y * w + x] = exp(-distance / petal_length);
        }
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

    s->lut = (double*) vj_malloc(sizeof(double) * (w * h * 4) );
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

    double *restrict cos_lut = s->cos_lut;
    double *restrict exp_lut = s->exp_lut;

    veejay_memcpy( bufY, srcY, len );
    veejay_memcpy( bufU, srcU, len );
    veejay_memcpy( bufV, srcV, len );

    int cx = width / 2;
    int cy = height / 2;
 
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
            
            double petalValue = (1.0 + cos_lut[y_pos * width + x_pos]) * exp_lut[y_pos * width + x_pos];

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

