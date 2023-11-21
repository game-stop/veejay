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
#include "spherize.h"

vj_effect *spherize_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 500;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 3600;

    ve->limits[0][2] = 1;
    ve->limits[1][2] = (int) sqrt( w/2 * w/2 + h/2 * h/2);
    
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 100;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 100;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = w;

    ve->limits[0][6] = 0;
    ve->limits[1][6] = h;

    ve->defaults[0] = 0;
    ve->defaults[1] = 100;
    ve->defaults[2] = 100;
    ve->defaults[3] = 100;
    ve->defaults[4] = 100;
    ve->defaults[5] = w/2;
    ve->defaults[6] = h/2;

    ve->description = "Spherize";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Strength" , "Angle", "Radius", "Ratio X" , "Ratio Y", "Center X" , "Center Y" );
    return ve;
}

typedef struct 
{
    uint8_t *buf[3];
    double *lut;
    double *atan2_lut;
    double *sin_lut;
    double *sqrt_lut;
    double *exp_lut;
    int last_cx;
    int last_cy;
    int last_radius;
    double last_angle;
} spherize_t;

static void init_atan2_lut(spherize_t *f, int w, int h, int cx, int cy)
{
    for (int x = 0; x < w; ++x) {
        double dx = x - cx;

        for (int y = 0; y < h; ++y) {
            double dy = y - cy;
            f->atan2_lut[y * w + x] = atan2(dy, dx); // slow
        }
    }
    f->last_cx = cx;
    f->last_cy = cy;
}

static void init_sin_lut(spherize_t *f, int w, int h, double angle)
{
    const int size = w * h;
    for(int i = 0; i < size; ++i) {
        f->sin_lut[i] = sin(f->atan2_lut[i] - angle);
    }
    f->last_angle = angle;
}

static void init_sqrt_lut(spherize_t *f, int w, int h, int cx, int cy)
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

static void init_exp_lut(spherize_t *f, int w, int h, int radius)
{
    for (int x = 0; x < w; ++x)
    {
        for (int y = 0; y < h; ++y)
        {
            double distance = f->sqrt_lut[ y * w + x ];
            f->exp_lut[y * w + x] = exp(-distance * distance / ( 2 * radius * radius ));
        }
    }
    f->last_radius = radius;
}


void *spherize_malloc(int w, int h) {
    spherize_t *s = (spherize_t*) vj_calloc(sizeof(spherize_t));
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
    s->sin_lut = s->atan2_lut + (w*h);
    s->sqrt_lut = s->sin_lut + (w*h);
    s->exp_lut = s->sqrt_lut + (w*h);

    init_sqrt_lut( s, w, h,w/2, h/2 );

    return (void*) s;
}

void spherize_free(void *ptr) {
    spherize_t *s = (spherize_t*) ptr;
    free(s->buf[0]);
    free(s->lut);
    free(s);
}


void spherize_apply(void *ptr, VJFrame *frame, int *args) {
    spherize_t *s = (spherize_t*)ptr;
    const double angle = (double) args[1] * 0.1;
    const int radius = args[2];
    const double ratio_x = (double) args[3] * 0.01;
    const double ratio_y = (double) args[4] * 0.01;
    const int center_x = args[5];
    const int center_y = args[6];
    const double strength = args[0] * 0.01;

    const int len = frame->width * frame->height;
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    double *restrict sin_lut = s->sin_lut;
    double *restrict exp_lut = s->exp_lut;

    veejay_memcpy( bufY, srcY, len );
    veejay_memcpy( bufU, srcU, len );
    veejay_memcpy( bufV, srcV, len );

    if( s->last_cx != center_x || s->last_cy != center_y ) {
        init_atan2_lut(s, width,height, center_x, center_y );
    }

    if( s->last_angle != angle ) {
        init_sin_lut(s, width,height, angle);
    }

    if( s->last_radius != radius ) {
        init_exp_lut(s, width,height, radius);
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;

            double dx = x - center_x;
            double dy = y - center_y;
            
            double ratio = 1.0 + strength * sin_lut[ y * width + x ] * exp_lut[ y * width + x ]; 

            int new_x = (int)(center_x + dx * ratio_x * ratio);
            int new_y = (int)(center_y + dy * ratio_y * ratio);

            if (new_x >= 0 && new_x < width && new_y >= 0 && new_y < height)
            {
                srcY[idx] = bufY[new_y * width + new_x];
                srcU[idx] = bufU[new_y * width + new_x];
                srcV[idx] = bufV[new_y * width + new_x];
            }
        }
    }

}

