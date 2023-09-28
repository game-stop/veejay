/* 
 * Linux VeeJay
 *
 * Copyright(C)2004-2016 Niels Elburg <nwelburg@gmail.com>
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
#include "motionmap.h"
#include "magicmirror.h"
#include "motionmap.h"

typedef struct {
    uint8_t *magicmirrorbuf[4];
    double *funhouse_x;
    double *funhouse_y;
    unsigned int *cache_x;
    unsigned int *cache_y;
    unsigned int last[4]; // {0,0,20,20};
    int cx1;
    int cx2;
    int n__;
    int N__;
    void *motionmap;
} magicmirror_t;

vj_effect *magicmirror_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 5;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */

	ve->defaults[0] = w/4;
	ve->defaults[1] = h/4;
	ve->defaults[2] = 20;
	ve->defaults[3] = 20;
	ve->defaults[4] = 0;

	ve->limits[0][0] = 0;
	ve->limits[1][0] = w/2;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = h/2;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 100;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 100;
	ve->limits[0][4] = 0;
	ve->limits[1][4] = 2;

	ve->motion = 1;
	ve->sub_format = 1;
	ve->description = "Magic Mirror Surface";
	ve->has_user =0;
	ve->extra_frame = 0;
	ve->alpha = FLAG_ALPHA_SRC_A | FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;
	ve->param_description = vje_build_param_list(ve->num_params, "X", "Y", "X","Y", "Alpha" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][4], 4, "Normal", "Alpha Mirror Mask", "Alpha Mirror Mask Only" );

	return ve;
}

void *magicmirror_malloc(int w, int h)
{
    magicmirror_t *m = (magicmirror_t*) vj_calloc(sizeof(magicmirror_t));
    if(!m) {
        return NULL;
    }

	m->magicmirrorbuf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t)*(w*h*4));
	if(!m->magicmirrorbuf[0]) {
		free(m);
        return NULL;
    }

	m->magicmirrorbuf[1] = m->magicmirrorbuf[0] + (w*h);
	m->magicmirrorbuf[2] = m->magicmirrorbuf[1] + (w*h);
	m->magicmirrorbuf[3] = m->magicmirrorbuf[2] + (w*h);
	
	m->magicmirrorbuf[1][0] = 128;
	m->magicmirrorbuf[2][0] = 128;
	m->magicmirrorbuf[1][w] = 128;
	m->magicmirrorbuf[2][w] = 128;
	
	m->funhouse_x = (double*)vj_calloc(sizeof(double) * w );
	if(!m->funhouse_x) {
        free(m->magicmirrorbuf[0]);
        free(m);
        return NULL;
    }

	m->cache_x = (unsigned int *)vj_calloc(sizeof(unsigned int)*w);
	if(!m->cache_x) {
        free(m->magicmirrorbuf[0]);
        free(m->funhouse_x);
        free(m);
        return NULL;
    }

	m->funhouse_y = (double*)vj_calloc(sizeof(double) * h );
	if(!m->funhouse_y) {
        free(m->magicmirrorbuf[0]);
        free(m->funhouse_x);
        free(m->cache_x);
        free(m);
        return NULL;
    }

	m->cache_y = (unsigned int*)vj_calloc(sizeof(unsigned int)*h);
	if(!m->cache_y) {
        free(m->magicmirrorbuf[0]);
        free(m->funhouse_x);
        free(m->funhouse_y);
        free(m->cache_x);
        free(m);
        return NULL;
    }   

    m->last[2] = 20;
    m->last[3] = 20;

	return (void*) m;
}

void magicmirror_free(void *ptr)
{
    magicmirror_t *m = (magicmirror_t*) ptr;

    free(m->magicmirrorbuf[0]);
    free(m->funhouse_x);
    free(m->funhouse_y);
    free(m->cache_x);
    free(m->cache_y);
    free(m);
}

void magicmirror_apply( void *ptr, VJFrame *frame, int *args) {
    int vx = args[0];
    int vy = args[1];
    int d = args[2];
    int n = args[3];
    int alpha = args[4];
    
    magicmirror_t *m = (magicmirror_t*) ptr;

	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	double c1 = (double)vx;
	double c2 = (double)vy;
	int motion = 0;
	int interpolate = 1;

	if( motionmap_active(m->motionmap))
	{
		if( motionmap_is_locked(m->motionmap) ) {
			d = m->cx1;
			n = m->cx2;
		} else {
			motionmap_scale_to( m->motionmap, 100,100,0,0, &d, &n, &(m->n__), &(m->N__) );
			m->cx1 = d;
			m->cx2 = n;
		}
		motion = 1;
	}
	else
	{
		m->n__ = 0;
		m->N__ = 0;
	}

	if( m->N__ == m->n__ || m->n__ == 0 )
		interpolate = 0;

	double c3 = (double)d * 0.001;
	unsigned int dx,dy,x,y,p,q;
	double c4 = (double)n * 0.001;
	int changed = 0;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *A = frame->data[3];
    double *funhouse_x = m->funhouse_x;
    double *funhouse_y = m->funhouse_y;
    unsigned int *cache_x = m->cache_x;
    unsigned int *cache_y = m->cache_y;
    uint8_t **magicmirrorbuf = m->magicmirrorbuf;

	if( d != m->last[1] ) {
		changed = 1; m->last[1] =d;
	}
	if( n != m->last[0] ) {
		changed = 1; m->last[0] = n;
	}

	if( vx != m->last[2] ) {
		changed = 1; m->last[2] = vx;
	}
	if( vy != m->last[3] ) {
		changed = 1; m->last[3] = vy;
	} 

	if(changed==1)
	{	
		// degrees x or y changed, need new sin
		for(x=0; x < width ; x++)
		{
			double res;
			fast_sin(res,(double)(c3*x));
			funhouse_x[x] = res;
		}
		for(y=0; y < height; y++)
		{
			double res;
			fast_sin(res,(double)(c4*y));
			funhouse_y[y] = res;
		}
	}
	for(x=0; x < width; x++)
	{
		dx = x + funhouse_x[x] * c1;
		if(dx < 0) dx += width;
		if(dx < 0) dx = 0; else if (dx >= width) dx = width-1;
		cache_x[x] = dx;
	}
	for(y=0; y < height; y++)
	{
		dy = y + funhouse_y[y] * c2;
		if(dy < 0) dy += height;
		if(dy < 0) dy = 0; else if (dy >= height) dy = height-1;
		cache_y[y] = dy;
	}

	veejay_memcpy( magicmirrorbuf[0], frame->data[0], len );
	veejay_memcpy( magicmirrorbuf[1], frame->data[1], len );
	veejay_memcpy( magicmirrorbuf[2], frame->data[2], len );

	if( alpha ) {
		veejay_memcpy( magicmirrorbuf[3], frame->data[3], len );
		/* apply on alpha first */
		for(y=1; y < height-1; y++)
		{
			for(x=1; x < width-1; x++)
			{
				q = y * width + x;
				p = cache_y[y] * width + cache_x[x];
				A[q] = magicmirrorbuf[3][p];
			}
		}

		uint8_t *Am = magicmirrorbuf[3];
		
		switch(alpha) {
				case 1:
					for(y=1; y < height-1; y++)
					{
						for(x=1; x < width-1; x++)
						{
							q = y * width + x;
							p = cache_y[y] * width + cache_x[x];
							if( Am[p] || A[q] ) { 
								Y[q] = magicmirrorbuf[0][p];
								Cb[q] = magicmirrorbuf[1][p];
								Cr[q] = magicmirrorbuf[2][p];
							}
						}
					}
					break;
		}
	}
	else {
		for(y=1; y < height-1; y++)
		{
			for(x=1; x < width-1; x++)
			{
				q = y * width + x;
				p = cache_y[y] * width + cache_x[x];
	
				Y[q] = magicmirrorbuf[0][p];
				Cb[q] = magicmirrorbuf[1][p];
				Cr[q] = magicmirrorbuf[2][p];
			}
		}
	}


	if( interpolate )
	{
		motionmap_interpolate_frame( m->motionmap, frame, m->N__, m->n__ );
	}

	if( motion )
	{
		motionmap_store_frame( m->motionmap, frame );
	}

}

int magicmirror_request_fx() {
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void magicmirror_set_motionmap(void *ptr, void *priv)
{
    magicmirror_t *m = (magicmirror_t*) ptr;
    m->motionmap = priv;
}

