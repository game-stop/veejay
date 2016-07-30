/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
#include <libvjmem/vjmem.h>
#include "slicer.h"

static int *slice_xshift = NULL;
static int *slice_yshift = NULL;
static int last_period = -1;
static int current_period = 1;

static	void recalc(int w, int h , uint8_t *Yinp, int v1, int v2, const int shatter )
{
  int x,y,dx,dy,r;
  int valx = v1;
  int valy = v2;
  for(x = dx = 0; x < w; x++) 
  {
	if(dx==0)
       	{
	        uint8_t *Yin = Yinp + (x * h);
                r = ((rand() & valx))-((valx>>1)+1); 
                dx = shatter + ( (Yin[x] & ((valx>>1))-1) );
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
		dy = shatter + ( Yin[x] & ((valy>>1)-1) );
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
	last_period = -1;
	current_period = 1;
    return 1;
}

vj_effect *slicer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = w;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = h;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 128;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 500;
	ve->limits[0][4] = 0;
	ve->limits[1][4] = 1;
    ve->defaults[0] = 16;
    ve->defaults[1] = 16;
	ve->defaults[2] = 8;
	ve->defaults[3] = 0;
	ve->defaults[4] = 0;
    ve->description = "Slicer";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Width", "Height", "Shatter", "Period", "Mode"); 
 
	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][4], 4, "No bounds", "With bounds" );

 
	return ve;
}


void slicer_apply( VJFrame *frame, VJFrame *frame2, int val1, int val2,int shatter, int period, int mode)
{
	int x,y,p,q;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2= frame2->data[0];
	uint8_t *Cb2=frame2->data[1];
	uint8_t *Cr2=frame2->data[2];
	uint8_t *aA = frame->data[3];
	uint8_t *aB = frame2->data[3];
	int dx,dy;

	if( period == 0 ) {
		srand( val1 * val2 * shatter );
	}
	else {
		srand( val1 * val2 * shatter * frame->timecode );
	}
			
	if( period != last_period ) {
		last_period = period;
		current_period = last_period;
	}

	if( current_period <= 0 ) {
		recalc( width, height, Y2, val1 ,val2, shatter );
		current_period = last_period;
	}

	current_period --;

	if( mode == 0 ) {
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
					aA[q] = aB[p];
				}
				else {
					Y[q] = pixel_Y_lo_;
					Cb[q] = 128;
					Cr[q] = 128;
					aA[q] = 0;
				}
			}
		}
	}
	else {
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
					aA[q] = aB[p];
				}
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
	last_period = -1;
	current_period = 1;
}
