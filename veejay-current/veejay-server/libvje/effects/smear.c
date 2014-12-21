/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include <stdint.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "smear.h"
#include "common.h"

vj_effect *smear_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->description = "Pixel Smear";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Value" );
    return ve;
}
static void _smear_apply_x(VJFrame *frame, int width, int height, int val)
{
    unsigned int j;
    unsigned int x,y;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
    for(y=0; y < height-1; y++)
    {
	for(x=0; x < width; x++)
	{
		j = Y[y*width+x];
		if(j >= val)
		{
			Y[y*width+x] = Y[y*width+x+j]; 
			Cb[y*width+x] = Cb[y*width+x+j];
			Cr[y*width+x] = Cr[y*width+x+j];
		}
	}
     }
}
static void _smear_apply_x_avg(VJFrame *frame, int width, int height, int val)
{
    unsigned int j;
    unsigned int x,y;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
    for(y=0; y < height-1; y++)
    {
	for(x=0; x < width; x++)
	{
		j = Y[y*width+x];
		if(j >= val)
		{
			Y[y*width+x] = ((Y[y*width+x+j] + Y[y*width+x])) >> 1; 
			Cb[y*width+x] = (((Cb[y*width+x+j]-128)+(Cb[y*width+x]-128))>>1)+128;
			Cr[y*width+x] = (((Cb[y*width+x+j]-128)+(Cr[y*width+x]-128))>>1)+128;
		}
	}
     }
}
static void _smear_apply_y_avg(VJFrame *frame, int width, int height, int val)
{
    unsigned int i,j;
    unsigned int x,y;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

    for(y=1; y < height; y++)
    {
	for(x=0; x < width; x++)
	{
		j = Y[y*width+x];
		if(j >= val)
		{
			if(j >= height) j = height-1;
			i = j * width + x;
			Y[y*width+x] = (Y[i]+Y[y*width+x])>>1; 
			Cb[y*width+x] = (((Cb[i]-128)+(Cb[y*width+x]-128))>>1)+128;
			Cr[y*width+x] = (((Cr[i]-128)+(Cr[y*width+x]-128))>>1)+128;
		}
	}
     }
}
static void _smear_apply_y(VJFrame *frame, int width, int height, int val)
{
    unsigned int i,j;
    unsigned int x,y;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

    for(y=1; y < height; y++)
    {
	for(x=0; x < width; x++)
	{
		j = Y[y*width+x];
		if(j >= val)
		{
			if(j >= height) j = height-1;
			i = j * width + x;
			Y[y*width+x] = Y[i]; 
			Cb[y*width+x] = Cb[i];
			Cr[y*width+x] = Cr[i];
		}
	}
     }
}


static int n__ = 0;
static int N__ = 0;

void smear_apply( VJFrame *frame, int width, int height,int mode, int val)
{
   	int interpolate = 1;
        int tmp1 = mode;
        int tmp2 = val;
        int motion = 0;
        if(motionmap_active())
        {
                motionmap_scale_to( 255, 3, 0, 0, &tmp2, &tmp1, &n__, &N__ );
                motion = 1;
        }
	else
	{
		N__ = 0;
		n__ = 0;
	}

        if( n__ == N__ || n__ == 0 )
                interpolate = 0;

	switch(mode)
   	{
		case 0:	_smear_apply_x(frame,width,height,tmp2); break;
		case 1: _smear_apply_x_avg(frame,width,height,tmp2); break;
		case 2: _smear_apply_y(frame,width,height,tmp2); break; 
		case 3: _smear_apply_y_avg(frame,width,height,tmp2); break;
		default: break;
   	}

  	if( interpolate )
        {
                motionmap_interpolate_frame( frame, N__,n__ );
        }

        if(motion)
        {
                motionmap_store_frame( frame );
        }

}

