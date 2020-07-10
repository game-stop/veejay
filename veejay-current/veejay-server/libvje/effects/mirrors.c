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

vj_effect *mirrors_init(int width,int height)
{

	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 2;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 0;
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
	return ve;
}

static void _mirrors_v( uint8_t *yuv[3], int width, int height, int factor, int swap)
{
	const int len = width * height;
	int r,c;
	const int line_width = width / ( factor + 1);
	int i=0;

	if(swap)
	{
		for(r = 0; r < len; r += width )
		{
			for( c = 0 ; c < width; c+= line_width)
			{
				for(i = 0; i < line_width; i++)
				{
					yuv[0][r + c + (line_width-i)] = yuv[0][r + c + i];
					yuv[1][r + c + (line_width-i)] = yuv[1][r + c + i];
					yuv[2][r + c + (line_width-i)] = yuv[2][r + c + i];
				}
			}
	
		}
	}
	else
	{
		for(r = 0; r < len; r += width )
		{
			for( c = 0 ; c < width; c+= line_width)
			{
				for(i = 0; i < line_width; i++)
				{
					yuv[0][r + c + i] = yuv[0][r + c + (line_width-i)];
					yuv[1][r + c + i] = yuv[1][r + c + (line_width-i)];
					yuv[2][r + c + i] = yuv[2][r + c + (line_width-i)];
				}
			}
	
		}
	}
}

static void _mirrors_h( uint8_t *yuv[3], int width, int height, int factor, int swap)
{
	int line_height = height / ( factor + 1);

	int nr = height / line_height;
	int x,y,i;
	int slice = 0;
	int slice_end = 0;
	if(swap)
	{
		for(i=0; i < nr; i++)
		{
			slice = i * line_height;
			slice_end = slice + line_height;
			for(y=slice; y < slice_end; y++)
			{
				for(x=0; x < width; x++)
				{
					yuv[0][(y*width)+x] = yuv[0][(slice_end-y)*width+x];
					yuv[1][y*width+x] = yuv[1][(slice_end-y)*width+x];
					yuv[2][y*width+x] = yuv[2][(slice_end-y)*width+x];
				} 
			}
		}
	}
	else
	{
		for(i=0; i < nr; i++)
		{
			slice = i * line_height;
			slice_end = slice + line_height;
			for(y=slice_end; y > 0; y--)
			{
				for(x=0; x < width; x++)
				{
					yuv[0][y*width+x] = yuv[0][(slice_end-y)*width+x];
					yuv[1][y*width+x] = yuv[1][(slice_end-y)*width+x];
					yuv[2][y*width+x] = yuv[2][(slice_end-y)*width+x];
				} 
			}
		}
	}
}

typedef struct {
    int n__;
    int N__;
    void *motionmap;
} mirrors_t;
    
void *mirrors_malloc(int w, int h)
{
    mirrors_t *m = (mirrors_t*) vj_calloc(sizeof(mirrors_t));
    if(!m) {
        return NULL;
    }
    return m;
}       

void mirrors_free(void *ptr)
{
    mirrors_t *m = (mirrors_t*) ptr;
    free(m);
}

int mirrors_request_fx() {
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
			_mirrors_v(frame->data, width, height, tmp2, 0);
		break;
		case 1:
			_mirrors_v(frame->data,width, height,tmp2,1);
		break;
		case 2:
			_mirrors_h(frame->data,width, height,tmp2,0);
		break;
		case 3:
			_mirrors_h(frame->data,width, height,tmp2,1);
		break;
	}

	if( interpolate )
		motionmap_interpolate_frame( m->motionmap, frame, m->N__,m->n__ );
	if( motion )
		motionmap_store_frame( m->motionmap, frame );
}
