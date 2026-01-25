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
#include <config.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "fisheye.h"

vj_effect *fisheye_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = -1000;
    ve->limits[1][0] = 1000;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[0] = 1;
    ve->defaults[1] = 0;
    ve->description = "Fish Eye";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Curve", "Mask to Alpha" );
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL | FLAG_ALPHA_SRC_A;
    return ve;
}

typedef struct {
    int _v;
    float *polar_map;
    float *fish_angle;
    int *cached_coords; 
    uint8_t *buf[3];
} fisheye_t;

void *fisheye_malloc(int w, int h)
{
    int x,y;
    int h2=h/2;
    int w2=w/2;
    int p =0;

    fisheye_t *f = (fisheye_t*) vj_calloc(sizeof(fisheye_t));
    if(!f) {
        return NULL;
    }

    f->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w * h  *  3 ) );
    if(!f->buf[0]) {
        fisheye_free(f);
        return NULL;
    }

    f->buf[1] = f->buf[0] + (w*h);
    f->buf[2] = f->buf[1] + (w*h);

    f->polar_map = (float*) vj_calloc(sizeof(float) * (w * h) );
    if(!f->polar_map) {
        fisheye_free(f);
        return NULL;
    }

    f->fish_angle = (float*) vj_calloc(sizeof(float) * (w * h) );
    if(!f->fish_angle) {
        fisheye_free(f);
        return NULL;
    }

    f->cached_coords = (int*) vj_calloc(sizeof(int) * ( w * h));
    if(!f->cached_coords) {
        fisheye_free(f);
        return NULL;
    }

    for(y=(-1 *h2); y < (h-h2); y++)
    {
        for(x= (-1 * w2); x < (w-w2); x++)
        {
            float res = sqrt_approx_f( y * y + x * x );
            p = (h2+y)*w+(w2+x);
            f->polar_map[p] = res;
            f->fish_angle[p] = atan2_approx_f( (float) y, x);
        }
    }
    
    return (void*) f;
}

void    fisheye_free(void *ptr)
{
    fisheye_t *f = (fisheye_t*) ptr;
    if(f->buf[0]) {
        free(f->buf[0]);
    }
    if(f->polar_map)    free(f->polar_map);
    if(f->fish_angle)   free(f->fish_angle);
    if(f->cached_coords) free(f->cached_coords);

    free(f);
}

static double __fisheye(double r,double v, double e)
{
    return (exp( r / v )-1) / e;
}
            
static double __fisheye_i(double r, double v, double e)
{
    return v * log(1 + e * r);
}   

void fisheye_apply(void *ptr, VJFrame *frame, int *args) {
    int v = args[0];
    int alpha = args[1];

    fisheye_t *f = (fisheye_t*) ptr;

    double (*pf)(double a, double b, double c);
    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    float *polar_map = f->polar_map;
    float *fish_angle = f->fish_angle;
    int *cached_coords = f->cached_coords;
    uint8_t **buf = f->buf;

    if(v == 0) v = 1;

    if(v < 0) {
        pf = &__fisheye_i;
        v = v * -1;
    } else {
        pf = &__fisheye;
    }

    if (v != f->_v)
    {
        const double curve = 0.001 * v;
        const unsigned int R = height / 2;
        const double coeef = R / log(curve * R + 1);
        const int w2 = width / 2;
        const int h2 = height / 2;
        double co, si;

        for (int i = 0; i < len; i++)
        {
            float r = polar_map[i];
            float a = fish_angle[i];
            
            int in_circle = (r <= R);
            
            double r_dist = pf(r, coeef, curve);

            sin_cos(si, co, a);

            int px_potential = (int)(r_dist * co) + w2;
            int py_potential = (int)(r_dist * si) + h2;

            int px_clamped = (px_potential < 0) ? 0 : (px_potential >= width) ? (width - 1) : px_potential;
            int py_clamped = (py_potential < 0) ? 0 : (py_potential >= height) ? (height - 1) : py_potential;

            cached_coords[i] = (in_circle) ? (py_clamped * width + px_clamped) : -1;
        }

        f->_v = v;
    }

    veejay_memcpy(buf[0], Y, len);
    
    if(alpha == 0) {
        veejay_memcpy(buf[1], Cb, len);
        veejay_memcpy(buf[2], Cr, len);

        for(int i = 0; i < len; i++)
        {
            int coord = cached_coords[i];

            if (coord == -1) {
                Y[i] = 16;
                Cb[i] = 128;
                Cr[i] = 128;
            } else {
                Y[i] = buf[0][coord];
                Cb[i] = buf[1][coord];
                Cr[i] = buf[2][coord];
            }
        }
    } else {
        uint8_t *A = frame->data[3];
        for(int i = 0; i < len; i++)
        {
            int coord = cached_coords[i];
            if (coord == -1) {
                 A[i] = 0;
 	    } else {
                 A[i] = buf[0][coord];
            }
        }
    }
}
