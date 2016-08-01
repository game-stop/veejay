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
#include <libvjmem/vjmem.h>
#include "raster.h"

vj_effect *raster_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 4;
    ve->limits[1][0] = h/4;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[0] = 4;
    ve->defaults[1] = 1;
	ve->description = "Grid";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Grid size", "Mode");

	ve->hints = vje_init_value_hint_list( ve->num_params );
	vje_build_value_hint_list( ve->hints, ve->limits[1][1], 1,"Black", "White" );

    return ve;
}

void raster_apply(VJFrame *frame, int val, int mode)
{
	int x,y;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;

	if(val == 0 )
	  return;

	uint8_t pixel_color = mode ? pixel_Y_hi_ : pixel_Y_lo_;

	for(y=0; y < height; y++)
	{
		for(x=0; x < width; x++)
		{
			Y[y*width+x] = ((x%val>1)? ((y%val>1) ? Y[y*width+x]: pixel_color):pixel_color);
		}
	}

	const unsigned int uv_width= frame->uv_width;
	const unsigned int uv_height= frame->uv_height;

	for(y=0; y < uv_height; y++)
	{
		for(x=0; x < uv_width; x++)
		{
			Cb[y*uv_width+x] = ((x%val>1)? ((y%val>1) ? Cb[y*uv_width+x]:128):128);
			Cr[y*uv_width+x] = ((x%val>1)? ((y%val>1) ? Cr[y*uv_width+x]:128):128);
		}
	}
/*
	int x,y;
	int px,py;
	int i,j;
	double r,a;
	unsigned int R = h/2;
	double	curve; //curve
	double  coeef;
	int w2 = w/2;
	int h2 = h/2;
	int n1=0,n2=0,n3=0;
	int k,l,m,o1=0,o2=0;

	double (*pf)(double a, double b, double c);
	
	if( v==0) v =1;
	if( v < 0 ) {
		pf = &__fisheye_i;
		v = v * -1;
	}
	else  {
		pf = &__fisheye;
	}

	curve = 0.001 * v; //curve
	coeef = R / log(curve * R + 1);

	if(!buf)
	{
		buf = (uint8_t*)vj_calloc(sizeof(uint8_t) * w * h );
		if(!buf)return;
	}
	memcpy(buf, Y,(w*h));

	for(y= (-1*h2); y < (h-h2); y++)
//	for(y=0; y < h; y++)
	//for(y=0; y < h; y++)
	{
		for(x = (-1*w2); x < (w-w2); x++)
	//	for(x=0 ;x < w; x++)
		{
			//if(x > 0 && y > 0)
			//
			if(x==0 && y==0) r = 0; 
			else r = sqrt( y*y+x*x);
			if(x==0 && y==0)
			a= 1;
			else
			a = atan2( (float)y,x);

			//if(x > 0 && y < 0) a += 240;
			
			//if(x < 0 && y > 0) a+= 180;
		
			//if(x < 0 && y < 0) a+= 180; 
	
			i = (y+h2)*w+(w2+x);
			if( r <= R)
			{
				//r = coeef * log(1 + curve * r);
				r = pf( r, coeef, curve);
				//px en py ook zonder +
				px = (int) ( r * cos(a) );
				py = (int) ( r * sin(a) );
				px += w2;
				py += h2;
				if(px < 0) px =0;
				if(px > w) px = w;
				if(py < 0) py = 0;
				if(py > (h-1)) py = h-1;
				j = px + py * w;
				//k = py * w + (w - px);
				Y[i] = buf[j];	
			}
			else
			{
				Y[i] = 16;
			}			

			

		}

	}


	printf(" n1 = %d n2 = %d n3 = %d \n",n1,n2,n3);


	memset(Cb,128,(w*h)/4);
	memset(Cr,128,(w*h)/4);
	*/

}
