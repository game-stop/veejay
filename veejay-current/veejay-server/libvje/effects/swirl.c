/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "swirl.h"

vj_effect *swirl_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 360;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[0] = 250;
    ve->defaults[1] = 0;
    ve->description = "Swirl";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Degrees", "Mode" );
    return ve;
}

typedef struct {
    double *polar_map;
    double *fish_angle;
    int *cached_coords;
    uint8_t *buf[4];
    int v;
    int mode;
} swirl_t;

void  *swirl_malloc(int w, int h)
{
    int x,y;
    int h2=h/2;
    int w2=w/2;
    int p = 0;


    swirl_t *s = (swirl_t*) vj_calloc( sizeof(swirl_t) );
    if(!s) {
        return NULL;
    }
    
    s->polar_map = (double*) vj_calloc(sizeof(double) * (w * h) );
    if(!s->polar_map) {
        swirl_free(s);
        return NULL;
    }

    s->fish_angle = (double*) vj_calloc(sizeof(double) * (w * h) );
    if(!s->fish_angle) {
        swirl_free(s);
        return NULL;
    }

    s->cached_coords = (int*) vj_calloc(sizeof(int) * (w * h) );
    if(!s->cached_coords) {
        swirl_free(s);
        return NULL;
    }

    double *polar_map = s->polar_map;
    double *fish_angle = s->fish_angle;

    for(y=(-1 *h2); y < (h-h2); y++)
    {
        for(x=(-1 * w2); x < (w-w2); x++)
        {
            p = (h2+y) * w + (w2+x);
            polar_map[p] = sqrt( y*y + x*x );
            fish_angle[p] = atan2( (float) y, x);
        }
    }

    return (void*) s;
}

void    swirl_free(void *ptr)
{
    swirl_t *s = (swirl_t*) ptr;

    if(s) {
        if( s->polar_map )
            free(s->polar_map);
        if( s->fish_angle )
            free(s->fish_angle);
        if( s->cached_coords )
            free(s->cached_coords );
        free(s);
    }
}

void swirl_apply(void *ptr, VJFrame *frame, int *args)
{
    int i;
    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb= frame->data[1];
    uint8_t *Cr= frame->data[2];

    int v = args[0];
    int mode = args[1];

    swirl_t *s = (swirl_t*) ptr;

    double *polar_map = s->polar_map;
    double *fish_angle = s->fish_angle;
    int *cached_coords = s->cached_coords;

    if (s->v != v || s->mode != mode) {
        const unsigned int R = width;
        const double coeef = v;

        int px, py;
        double r, a;
        double si, co;
        const int w2 = width >> 1;
        const int h2 = height >> 1;

        if( mode ==  1 ) {
            for (int y = 0; y <= h2; y++) {
                for (int x = 0; x <= w2; x++) {
                    int i = y * width + x;

                    r = polar_map[i];
                    a = fish_angle[i];

                    sin_cos(co, si, (a + r / coeef));

                    px = (int)(r * co) + w2;
                    py = (int)(r * si) + h2;

                    px = (px < 0) ? 0 : ((px >= width) ? (width - 1) : px);
                    py = (py < 0) ? 0 : ((py >= height) ? (height - 1) : py);

                    cached_coords[y * width + x] = py * width + px;
                    cached_coords[y * width + (width - x)] = py * width + px;
                    cached_coords[(height - y - 1) * width + x] = py * width + px;
                    cached_coords[(height - y - 1) * width + (width - x)] = py * width + px;
                }
            }
        }
        else  {
            for (i = 0; i < len; i++) {
                r = polar_map[i];
                a = fish_angle[i];

                sin_cos(co, si, (a + r / coeef));

                px = (int)(r * co) + w2;
                py = (int)(r * si) + h2;

                px = (px < 0) ? 0 : ((px >= width) ? (width - 1) : px);
                py = (py < 0) ? 0 : ((py >= height) ? (height - 1) : py);

                cached_coords[i] = (py * width) + px;
            }
        }
	s->v = v;
	s->mode = mode;
    }

    for(i=0; i < len; i++)
    {
        int idx = cached_coords[i];

        Y[i] = Y[idx];
        Cb[i] = Cb[idx];
        Cr[i] = Cr[idx];
    }
}
