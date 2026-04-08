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
#include "mirrors.h"
#include "motionmap.h"

#define GET_YUV_PTRS \
    uint8_t * restrict py = yuv[0]; \
    uint8_t * restrict pu = yuv[1]; \
    uint8_t * restrict pv = yuv[2];

vj_effect *mirrors_init(int width,int height)
{

	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 2;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 1;
	ve->defaults[1] = 1;
	ve->limits[0][0] = 0;	/* horizontal or vertical mirror */
	ve->limits[1][0] = 3;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = (int)((float)(width * 0.33));
	ve->sub_format = 1;
	ve->description = "Multi Mirrors";
	ve->extra_frame = 0;
	ve->has_user = 0;
	ve->motion = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "H or V", "Number" );

	ve->hints = vje_init_value_hint_list( ve->num_params );
	vje_build_value_hint_list( ve->hints, ve->limits[1][0],0,  "Right to Left", "Left to Right" , "Bottom to Top" , "Top to Bottom" );

	return ve;
}

static void _mirrors_v(uint8_t *yuv[3], int width, int height, int factor, int swap, int n_threads)
{
    GET_YUV_PTRS
    const int tile_w = width / (factor + 1);
    if (tile_w < 2) return;

    const int half_tile = tile_w / 2;

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int y = 0; y < height; y++) {
        uint8_t *ry = py + (y * width);
        uint8_t *ru = pu + (y * width);
        uint8_t *rv = pv + (y * width);

        for (int t = 0; t <= factor; t++) {
            int tile_off = t * tile_w;
            for (int x = 0; x < half_tile; x++) {
                int src_x = swap ? (tile_w - 1 - x) : x;
                int dst_x = swap ? x : (tile_w - 1 - x);

                ry[tile_off + dst_x] = ry[tile_off + src_x];
                ru[tile_off + dst_x] = ru[tile_off + src_x];
                rv[tile_off + dst_x] = rv[tile_off + src_x];
            }
        }
    }
}

static void _mirrors_h(uint8_t *yuv[3], int width, int height, int factor, int swap, int n_threads)
{
    GET_YUV_PTRS
    const int tile_h = height / (factor + 1);
    if (tile_h < 2) return;

    const int half_tile = tile_h / 2;

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int t = 0; t <= factor; t++) {
        int tile_start = t * tile_h;

        for (int y = 0; y < half_tile; y++) {
            int src_y_local = swap ? (tile_h - 1 - y) : y;
            int dst_y_local = swap ? y : (tile_h - 1 - y);

            const uint8_t *sY = py + (tile_start + src_y_local) * width;
            const uint8_t *sU = pu + (tile_start + src_y_local) * width;
            const uint8_t *sV = pv + (tile_start + src_y_local) * width;

            uint8_t *dY = py + (tile_start + dst_y_local) * width;
            uint8_t *dU = pu + (tile_start + dst_y_local) * width;
            uint8_t *dV = pv + (tile_start + dst_y_local) * width;

            for (int x = 0; x < width; x++) {
                dY[x] = sY[x];
                dU[x] = sU[x];
                dV[x] = sV[x];
            }
        }
    }
}

typedef struct {
    int n__;
    int N__;
    void *motionmap;
	int n_threads;
} mirrors_t;
    
void *mirrors_malloc(int w, int h)
{
    mirrors_t *m = (mirrors_t*) vj_calloc(sizeof(mirrors_t));
    if(!m) {
        return NULL;
    }
	m->n_threads = vje_advise_num_threads(w*h);
    return m;
}       

void mirrors_free(void *ptr)
{
    mirrors_t *m = (mirrors_t*) ptr;
    free(m);
}

int mirrors_request_fx(void) {
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void mirrors_set_motionmap(void *ptr, void *priv) {
    mirrors_t *m = (mirrors_t*) ptr;
    m->motionmap = priv;
}   

void mirrors_apply(void *ptr, VJFrame *frame, int *args ) {
    int type = args[0];
    int factor = args[1];

    mirrors_t *m = (mirrors_t*) ptr;

	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	int interpolate = 1;
	int motion = 0;
	int tmp1 = 0;
	int tmp2 = factor;

	if( motionmap_active(m->motionmap) )
	{
		int hi = (int)((float)(width * 0.33));

		motionmap_scale_to( m->motionmap, hi,hi,0,0,&tmp1,&tmp2,&(m->n__),&(m->N__));
		motion = 1;
	}
	else
	{
		m->n__ = 0;
		m->N__ = 0;
		interpolate=0;
	}

	switch (type) {
		case 0:
			_mirrors_v(frame->data, width, height, tmp2, 0,m->n_threads);
		break;
		case 1:
			_mirrors_v(frame->data,width, height,tmp2,1,m->n_threads);
		break;
		case 2:
			_mirrors_h(frame->data,width, height,tmp2,0,m->n_threads);
		break;
		case 3:
			_mirrors_h(frame->data,width, height,tmp2,1,m->n_threads);
		break;
	}

	if( interpolate )
		motionmap_interpolate_frame( m->motionmap, frame, m->N__,m->n__ );
	if( motion )
		motionmap_store_frame( m->motionmap, frame );
}
