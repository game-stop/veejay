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

/*
  this effect takes lumaninance information of frame B  (0=no displacement,255=max displacement)
  to extract distortion offsets for frame A.
  h_scale and v_scale can be used to limit the scaling factor.
  if the value is < 128, the pixels will be shifted to the left
  otherwise to the right.
           


*/

#include "common.h"
#include <veejaycore/vjmem.h>
#include "lumamask.h"
#include "motionmap.h"

typedef struct {
    uint8_t *buf[4];
    void *motionmap;
    int n__;
    int N__;
} lumamask_t;

vj_effect *lumamask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = -width;
    ve->limits[1][0] = width;
    ve->limits[0][1] = -height;
    ve->limits[1][1] = height;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
	ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->defaults[0] = width/20; 
    ve->defaults[1] = height/10;
    ve->defaults[2] = 0; // border
    ve->description = "Displacement Map";
    ve->motion = 1;
	ve->sub_format = 1;
    ve->extra_frame = 1;
  	ve->has_user = 0; 
	ve->param_description = vje_build_param_list(ve->num_params, "X displacement", "Y displacement", "Mode", "Update Alpha" );
    return ve;
}

int lumamask_requests_fx(void) {
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void lumamask_set_motionmap(void *ptr, void *priv) {
    lumamask_t* l = (lumamask_t*) ptr;
    l->motionmap = priv;
}

void *lumamask_malloc(int width, int height)
{
    lumamask_t *l = (lumamask_t*) vj_calloc(sizeof(lumamask_t));
    if(!l) {
        return NULL;
    }

    l->buf[0] = (uint8_t*)vj_malloc( sizeof(uint8_t) * ( width * height * 4) );
    if(!l->buf[0]) {
        free(l);
        return NULL;
    }

    veejay_memset( l->buf[0], pixel_Y_lo_, width * height );
   
    l->buf[1] = l->buf[0] + (width *height);
    veejay_memset( l->buf[1], 128, width * height );
    
    l->buf[2] = l->buf[1] + (width *height);
    veejay_memset( l->buf[2], 128, width * height );
   
    l->buf[3] = l->buf[2] + (width *height);
    veejay_memset( l->buf[3], 0, width * height );

    return (void*) l;
}

void lumamask_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args ) {
    int v_scale = args[0];
    int h_scale = args[1];
    int border = args[2];
    int alpha = args[3];

    lumamask_t *l = (lumamask_t*) ptr;

	unsigned int x,y;
	int dx,dy,nx,ny;
	int tmp;
	int interpolate = 1;
	int tmp1 = v_scale;
	int tmp2 = h_scale;
	int motion = 0;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;

	if( motionmap_active(l->motionmap) )
	{
		motionmap_scale_to(l->motionmap, width,height,1,1,&tmp1,&tmp2,&(l->n__),&(l->N__) );
		motion = 1;
	}
	else
	{
		l->n__ = 0;
		l->N__ = 0;
	}	

	if( l->n__ == l->N__ || l->n__ == 0 )
		interpolate = 0;

	double w_ratio = (double) tmp1 / 128.0;
	double h_ratio = (double) tmp2 / 128.0;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aA = frame->data[3];
	uint8_t *aB = frame2->data[3];

	int strides[4] = { len, len, len ,( alpha ? len : 0 )};
	vj_frame_copy( frame->data, l->buf, strides );

	if( alpha == 0 )
	{
  	  if( border )
	  {
		for(y=0; y < height; y++)
		{
			for(x=0; x < width ; x++)
			{
				// calculate new location of pixel
				tmp = Y2[(y*width+x)] - 128;
				// new x offset 
				dx = -w_ratio * tmp;
				// new y offset 
				dy = -h_ratio * tmp;
				// new pixel coordinates
				nx = x + dx;
				ny = y + dy;

				if( nx < 0 || ny < 0 || nx >= width || ny >= height )
        	    {
                	Y[y*width+x] = 16;
                	Cb[y*width+x] = 128;
                   	Cr[y*width+x] = 128;
                }
                else
                {
                    Y[y*width+x] = Y2[ny * width + nx];
                   	Cb[y*width+x] = Cb2[ny * width + nx];
                   	Cr[y*width+x] = Cr2[ny * width + nx];
                }
			}
		}
	  }
	  else
	  {
		for(y=0; y < height; y++)
		{
			for(x=0; x < width ; x++)
			{
				tmp = Y2[(y*width+x)] - 128;
				dx = -w_ratio * tmp;
				dy = -h_ratio * tmp;
				nx = x + dx;
				ny = y + dy;
				while( nx < 0 )
					nx += width;
				while( ny < 0 )
					ny += height;
				if( nx < 0 || ny < 0 || nx >= width || ny >= height )
        	    {
					Y[y*width+x] = 16;
                    Cb[y*width+x] = 128;
                    Cr[y*width+x] = 128;
                }
                else
				{
					Y[y*width+x] = Y2[ny * width + nx];
                    Cb[y*width+x] = Cb2[ny * width + nx];
                    Cr[y*width+x] = Cr2[ny * width + nx];
                }
			}
		}
	  }
	}
	else /* write alpha */
	{
  	  if( border )
	  {
		for(y=0; y < height; y++)
		{
			for(x=0; x < width ; x++)
			{
				// calculate new location of pixel
				tmp = Y2[(y*width+x)] - 128;
				// new x offset 
				dx = -w_ratio * tmp;
				// new y offset 
				dy = -h_ratio * tmp;
				// new pixel coordinates
				nx = x + dx;
				ny = y + dy;

				if( nx < 0 || ny < 0 || nx >= width || ny >= height )
        	    {
                	Y[y*width+x] = 16;
                	Cb[y*width+x] = 128;
                   	Cr[y*width+x] = 128;
					aA[y*width+x] = 0;
                }
                else
                {
                    Y[y*width+x] = Y2[ny * width + nx];
                   	Cb[y*width+x] = Cb2[ny * width + nx];
                   	Cr[y*width+x] = Cr2[ny * width + nx];
					aA[y*width+x] = aB[ny * width + nx];
                }
			}
		}
	  }
	  else
	   {
		for(y=0; y < height; y++)
		{
			for(x=0; x < width ; x++)
			{
				tmp = Y2[(y*width+x)] - 128;
				dx = -w_ratio * tmp;
				dy = -h_ratio * tmp;
				nx = x + dx;
				ny = y + dy;
				while( nx < 0 )
					nx += width;
				while( ny < 0 )
					ny += height;
				if( nx < 0 || ny < 0 || nx >= width || ny >= height )
        	    {
					Y[y*width+x] = 16;
                    Cb[y*width+x] = 128;
                    Cr[y*width+x] = 128;
					aA[y*width+x] = 0;
                }
                else
				{
					Y[y*width+x] = Y2[ny * width + nx];
                    Cb[y*width+x] = Cb2[ny * width + nx];
                    Cr[y*width+x] = Cr2[ny * width + nx];
					aA[y*width+x] = aB[ny*width+nx];
                }
			}
		}
	  }
	}

	if( interpolate )
		motionmap_interpolate_frame( l->motionmap, frame, l->N__, l->n__ );
	
	if( motion )
		motionmap_store_frame( l->motionmap, frame );

}

void lumamask_free(void *ptr)
{
    lumamask_t *l = (lumamask_t*) ptr;
    free(l->buf[0]);
    free(l);
}
