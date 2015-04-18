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
    	
#include <stdint.h>	
#include <stdio.h>
#include <config.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "common.h"
#include "slicer.h"
static int *slice_xshift = NULL;
static int *slice_yshift = NULL;

static	void recalc(int w, int h , uint8_t *Yinp, int v1, int v2 )
{
  int x,y,dx,dy,r,p;
  int l = w * h;
  int valx = v1;//(w / 100.0) * v1;
  int valy = v2;//(h / 100.0) * v1;
  for(x = dx = 0; x < w; x++) 
  {
	if(dx==0)
       	{
	        uint8_t *Yin = Yinp + (x * h);
                r = ((rand() & valx))-((valx>>1)+1); 
                dx = 8 + ( (Yin[x] & ((valx>>1))-1) );
        }
        else
        {
                 dx--;
        }
    	slice_yshift[x] = r;
  }
 
  for(y=dy=0; y < h; y++) 
  {
   	if(dy==0) 
	{ 
		uint8_t *Yin = Yinp + (y * w);
		r = (rand() & valy)-((valy>>1)+1); 
		dy = ( 8 + Yin[x] & ((valy>>1)-1) );
	} 
	else 
	{ 
		dy--;
	}
   	slice_xshift[y] = r;
  }

}



int     slicer_malloc(int width, int height)
{
    slice_xshift = (int*) vj_malloc(sizeof(int) * height);
    if(!slice_xshift) return 0;
    slice_yshift = (int*) vj_malloc(sizeof(int) * width);
    if(!slice_yshift) return 0;
    return 1;
}

vj_effect *slicer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = w;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = h;
    ve->defaults[0] = 16;
    ve->defaults[1] = 16;
    ve->description = "Slicer";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "A", "b"); 
    return ve;
}


void slicer_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int val1, int val2)
{
	int x,y,p,q;
	const unsigned int len = (width * height);
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2= frame2->data[0];
	uint8_t *Cb2=frame2->data[1];
	uint8_t *Cr2=frame2->data[2];
	int dx,dy;

	srand( val1 * val2 );

	recalc( width, height, Y2, val1 ,val2 );

  	for(y=0; y < height; y++){ 
 	   for(x=0; x < width; x++) {
        	dx = x + slice_xshift[y]; 
		dy = y + slice_yshift[x];
		p = dy * width + dx;
		q = y * width + x;
		if( p >= 0 && p < len ) {
               	 Y[q] = Y2[p];
               	 Cb[q] = Cb2[p];
                 Cr[q] = Cr2[p];
		} else {
		  Y[q] = pixel_Y_lo_;
		  Cb[q] = 128;
		  Cr[q] = 128;
		}
            }
        }
  }

void slicer_free()
{
	if ( slice_xshift )
		free( slice_xshift );
	if ( slice_yshift )
		free( slice_yshift );
	slice_xshift = NULL;
	slice_yshift = NULL;
}
