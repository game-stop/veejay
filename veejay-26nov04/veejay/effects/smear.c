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
#include "smear.h"
#include <stdlib.h>


vj_effect *smear_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->description = "Pixel Smear";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_internal_data= 0;

    return ve;
}
static void _smear_apply_x(VJFrame *frame, int width, int height, int val)
{
    unsigned int i,j;
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
    unsigned int i,j;
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
			Y[y*width+x] = (Y[y*width+x+j] + Y[y*width+x]) >> 1; 
			Cb[y*width+x] = ((Cb[y*width+x+j]-128)+(Cb[y*width+x]-128)>>1)+128;
			Cr[y*width+x] = ((Cb[y*width+x+j]-128)+(Cr[y*width+x]-128)>>1)+128;
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
			Y[y*width+x] = Y[i]+Y[y*width+x]>>1; 
			Cb[y*width+x] = ((Cb[i]-128)+(Cb[y*width+x]-128)>>1)+128;
			Cr[y*width+x] = ((Cr[i]-128)+(Cr[y*width+x]-128)>>1)+128;
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



void smear_apply( VJFrame *frame, int width, int height,int mode, int val)
{
   switch(mode)
   {
	case 0:	_smear_apply_x(frame,width,height,val); break;
	case 1: _smear_apply_x_avg(frame,width,height,val); break;
	case 2: _smear_apply_y(frame,width,height,val); break; 
        case 3: _smear_apply_y_avg(frame,width,height,val); break;
	default: break;

   }
}

