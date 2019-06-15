/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "overclock.h"

vj_effect *overclock_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = (h/8);
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 90;
    ve->defaults[0] = 5;
    ve->defaults[1] = 2;
    ve->description = "Radial cubics";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Radius", "Value");
    return ve;
}

static uint8_t *oc_buf[3] = { NULL,NULL,NULL };

int overclock_malloc(int w, int h)
{
	const int len = w* h;
	oc_buf[0] = (uint8_t*) vj_calloc(sizeof(uint8_t) * len );
	if(oc_buf[0]==NULL) return 0;
	return 1;
}

void overclock_free()
{
	if(oc_buf[0]) free( oc_buf[0] );
}

void overclock_apply(VJFrame *frame, int n, int radius )
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
    int x,y,dx,dy;
    uint8_t t = 0; 
	int s = 0;
    int i = 0;
    int N = ((n==0?1:n) * 2);
    uint8_t *Y = frame->data[0];

	for ( y = 0 ; y < height ; y ++)
	{
		blur2( oc_buf[0] + (y*width),Y + (y*width) ,width, radius,1,1,1);
	}

	for( y = N ; y < (height-N); y += (1+rand()%N) )
    {
	    int r = 1 + rand() % N;
		for( x = 0; x < width; x+= r )
	    {
			s = 0;
			for(dy = 0; dy < N; dy++ )
		    {
				for(dx = 0; dx < N; dx ++ )
			    {
				   s += oc_buf[0][(y+dy)*width+x+dx];
				}
			}
			t = (uint8_t) (s / (N*N));
			for(dy = 0; dy < N; dy++ )
		    {
				for(dx = 0; dx < N; dx ++ )
			    {
				   i  = (y+dy)*width+x+dx;
				   Y[i] = ( oc_buf[0][i] > Y[i] ? ((Y[i]+t)>>1) : t );
				}
			}
	    }
	}
}
