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
#include "dither.h"

vj_effect *dither_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 2;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 2;
	ve->defaults[1] = 0;

	ve->limits[0][0] = 2;
	ve->limits[1][0] = w-1;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 1;

	ve->description = "Matrix Dithering";
	ve->sub_format = -1;
	ve->extra_frame = 0;
	ve->has_user = 0;

	ve->param_description = vje_build_param_list( ve->num_params, "Value", "Mode" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][1], 1, "Static", "Random" );

	return ve;
}

typedef struct {
    int **dith;
    int w;
    int last_size;
} dither_t;

void *dither_malloc(int w, int h)
{
    dither_t *d = (dither_t*) vj_calloc(sizeof(dither_t));
    if(!d) {
        return NULL;
    }

    unsigned int i;
    d->dith = (int**) vj_calloc(sizeof(int*) * w);
    if(!d->dith) {
        free(d);
        return NULL;
    }

    for( i = 0; i < w; i ++ ) {
        d->dith[i] = (int*) vj_calloc(sizeof(int) * w);
        if(!d->dith[i]) {
            int j;
            for( j = 0; j < i; j ++ )
                if( d->dith[i] ) free(d->dith[i]);
            free(d->dith);
            free(d);
            return NULL;
        }
    }

    d->w = w;

    return (void*) d;
}

void dither_free(void *ptr)
{
    dither_t *d = (dither_t*) ptr;
    for( int i = 0; i < d->w; i ++ ) {
        if(d->dith[i]) {
            free(d->dith[i]);
        }
    }
    free(d->dith);
    free(d);
}

void dither_apply(void *ptr, VJFrame *frame, int *args) {
    int size = args[0];
    int random_on = args[1];

    dither_t *dh = (dither_t*) ptr;
    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    uint8_t *Y = frame->data[0];
    int **dith = dh->dith;

    if(dh->last_size != size || random_on) {
        for(int j = 0; j < size; j++) {
            for(int i = 0; i < size; i++) {
                dith[i][j] = (int)((double)size * rand() / (RAND_MAX + 1.0));
            }
        }
        dh->last_size = size;
    }

    for(size_t y = 0; y < height; y++) {
        int j = y % size;
        size_t row_base = y * width;
#pragma omp simd
        for(size_t x = 0; x < width; x++) {
            int i = x % size;
            int d = dith[i][j] << 4;
            int v = Y[row_base + x] + d;
            Y[row_base + x] = (uint8_t)((v >> 7) << 7);
        }
    }
}
