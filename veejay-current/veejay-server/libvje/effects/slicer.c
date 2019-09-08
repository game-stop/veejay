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
#include <veejaycore/vjmem.h>
#include "slicer.h"

typedef struct {
    int *slice_xshift;
    int *slice_yshift;
    int last_period;
    int current_period;
} slicer_t;

static	void recalc(slicer_t *s, int w, int h , uint8_t *Yinp, int v1, int v2, const int shatter )
{
  int x,y,dx,dy,r;
  int valx = v1;
  int valy = v2;

  int *slice_xshift = s->slice_xshift;
  int *slice_yshift = s->slice_yshift;

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

void *slicer_malloc(int width, int height)
{
    slicer_t *s = (slicer_t*) vj_calloc(sizeof(slicer_t));
    s->last_period = -1;
    s->current_period = 1;

    s->slice_xshift = (int*) vj_malloc(sizeof(int) * height);
    if(!s->slice_xshift) {
        free(s);
        return NULL;
    }

    s->slice_yshift = (int*) vj_malloc(sizeof(int) * width);
    if(!s->slice_yshift) {
        free(s->slice_xshift);
        free(s);
        return NULL;
    }

    return (void*) s;
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


void slicer_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args )
{
	int x,y,p,q;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2= frame2->data[0];
	uint8_t *Cb2=frame2->data[1];
	uint8_t *Cr2=frame2->data[2];
	uint8_t *aA = frame->data[3];
	uint8_t *aB = frame2->data[3];
	int dx,dy;

    int val1 = args[0];
    int val2 = args[1];
    int shatter = args[2];
    int period = args[3];
    int mode = args[4];

    slicer_t *s = (slicer_t*) ptr;

    int *slice_xshift = s->slice_xshift;
    int *slice_yshift = s->slice_yshift;

	if( period == 0 ) {
		srand( val1 * val2 * shatter );
	}
	else {
		srand( val1 * val2 * shatter * (int)( frame->timecode * 1000 ) );
	}
			
	if( period != s->last_period ) {
		s->last_period = period;
		s->current_period = s->last_period;
	}

	if( s->current_period <= 0 ) {
		recalc( s, width, height, Y2, val1 ,val2, shatter );
		s->current_period = s->last_period;
	}

	s->current_period --;

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

void slicer_free(void *ptr)
{
    slicer_t *s = (slicer_t*) ptr;
    free( s->slice_xshift );
    free( s->slice_yshift );
    free(s);
}
