/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <elburg@hio.hen.nl>
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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "chromapalette.h"
#include "common.h"
#include <math.h>

vj_effect *chromapalette_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	//angle,r,g,b,cbc,crc

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 900;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 255;	

    ve->defaults[0] = 319;//angle
    ve->defaults[1] = 255;   //r
    ve->defaults[2] = 0;   //g
    ve->defaults[3] = 0; //b
    ve->defaults[4] = 200;  //cb default
    ve->defaults[5] = 20; //cr default
   	ve->parallel = 1;

	ve->description = "Chrominance Palette (rgb key) ";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_help = 1;
	ve->has_user = 0;
	ve->rgb_conv = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Angle", "Red","Green","Blue", "Chroma Blue","Chroma Red" );
    return ve;
}

void	chromapalette_help() 
{
/*	veejay_msg(1,  "Chrominance Palette");
	veejay_msg(1,  "Select a color and set a new chroma value for the entire selection");
	veejay_msg(1,  "one or both of the parameters p4 and p5");
	veejay_msg(1,  "p0 = Accept angle");
	veejay_msg(1,  "p1 = Red");
	veejay_msg(1,  "p2 = Green");
	veejay_msg(1,  "p3 = Blue");
	veejay_msg(1,  "p4 = New chroma Blue value");
	veejay_msg(1,  "p5 = New chroma Red value"); 
*/
}

static inline int _chroma_key( uint8_t fg_cb, uint8_t fg_cr, uint8_t cb, uint8_t cr, int angle)
{
	// see rgbkey.c 
	short xx = ((fg_cb * cb) + (fg_cr * cr)) >> 7;
	short yy = ((fg_cr * cb) - (fg_cb * cr)) >> 7;
	int val;

	if (xx < -128) {
	    xx = -128;
	}
	else
	if (xx > 127) {
	    xx = 127;
	}

	if (yy < -128) {
	    yy = -128;
	}
	else
	if (yy > 127) {
	    yy = 127;
	}

	val = (xx * angle) >> 4;

	if( abs(yy) < val ) return 1;
	return 0;
}



void chromapalette_apply(VJFrame *frame, int width, int height, int angle, int r, int g, int b, int color_cb, int color_cr )
{
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t U;
	uint8_t V;
	int	y,u,v;
 	const float cb_mul = 0.492;
	const float cr_mul = 0.877;
	
	_rgb2yuv( r,g,b,y,u,v );

	const float aa = (const float) u;
	const float bb = (const float) v;

    float tmp = sqrt(((aa * aa) + (bb * bb)));
    const int colorKeycb = 127 * (aa / tmp);
    const int colorKeycr = 127 * (bb / tmp);
	float angle_f = (angle*0.1);
    const int accept_angle = 0xf * tan(M_PI * angle_f / 180.0);

	/*

				chrominance is defined as the difference between a color and a reference value luminance

				U = blue - Y
			    V = red - Y

				this effect does (on key selection)

				U = color_cb - Y
			 	V = color_cr - Y
				
			 	4:2:0 is supersampled to 4:4:4 so there is a chroma value for every Y

	*/

	if(color_cb != 0 && color_cr != 0) //both cb and cr
	{
		for( i = 0 ; i < len ; i ++ )
		{
				if( _chroma_key( Cb[i] , Cr[i], colorKeycb,colorKeycr, accept_angle))
				{
					U =  128+(int)( (float) (color_cb - Y[i]) * cb_mul );
					Cb[i] = CLAMP_UV( U );
					V =  128+(int)( (float) (color_cr - Y[i]) * cr_mul );
					Cr[i] = CLAMP_UV( V );
				}
		}
	}
	if(color_cr == 0 ) //only cr
	{
		for( i = 0 ; i < len ; i ++ )
		{
				if( _chroma_key( Cb[i], Cr[i], colorKeycb, colorKeycr, accept_angle))
				{
					V = 128+(int)( (float) (color_cr - Y[i]) * cr_mul );
					Cr[i] = CLAMP_UV( V );
				}
		}
	}
	if(color_cb == 0 ) // only cb
	{
		for( i = 0 ; i < len ; i ++ )
		{
			if( _chroma_key( Cb[i] , Cr[i], colorKeycb,colorKeycr, accept_angle))
			{
				U = 128 + (int)( (float) (color_cb - Y[i]) * cb_mul );
				Cb[i] = CLAMP_UV(U);
			}
		}
	}

 
}
