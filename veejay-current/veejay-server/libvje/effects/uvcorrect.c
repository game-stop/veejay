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
/*
  *  yuvcorrect_functions.c
  *  Common functions between yuvcorrect and yuvcorrect_tune
  *  Copyright (C) 2002 Xavier Biquard <xbiquard@free.fr>
  * 
  *  This program is free software; you can redistribute it and/or modify
  *  it under the terms of the GNU General Public License as published by
  *  the Free Software Foundation; either version 2 of the License, or
  *  (at your option) any later version.
  *
  *  This program is distributed in the hope that it will be useful,
  *  but WITHOUT ANY WARRANTY; without even the implied warranty of
  *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  *  GNU General Public License for more details.
  *
  *  You should have received a copy of the GNU General Public License
  *  along with this program; if not, write to the Free Software
  *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
  */
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "uvcorrect.h"
#include "common.h"
#include <math.h>

static uint8_t *chrominance = NULL;

vj_effect *uvcorrect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	//angle,r,g,b,cbc,crc

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 360;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 100;
    ve->limits[0][4] = 0;
    ve->limits[1][4] = 100;
    ve->limits[0][5] = 0;
    ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;
    ve->limits[1][6] = 255;	

    ve->defaults[0] = 1;
    ve->defaults[1] = 128; 
    ve->defaults[2] = 128;  
    ve->defaults[3] = 10; 
    ve->defaults[4] = 10; 
    ve->defaults[5] = pixel_U_lo_; 
    ve->defaults[6] = pixel_U_hi_;
    ve->description = "U/V Correction";
	ve->param_description = vje_build_param_list( ve->num_params, "Angle" ,"U Rotate Center", "V Rotate Center",
			"Intensity U", "Intensity V", "Minimum UV", "Maximum UV");

    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_help = 1;
	ve->has_user = 0;
    
    return ve;
}

int	uvcorrect_malloc(int w, int h )
{
	chrominance = (uint8_t*) vj_malloc (sizeof(uint8_t) * 2 * 256 * 256 );
	if(!chrominance) return 0;
	return 1;
}

void	uvcorrect_free()
{
	if(chrominance) free(chrominance);
	chrominance= NULL;
}

void	uvcorrect_help() 
{
	/*veejay_msg(1,  "UV Correct (portion of yuvcorrect by Xavier Biquard");
	veejay_msg(1,  "Select a color and set a new chroma value for the entire selection");
	veejay_msg(1,  "one or both of the parameters p4 and p5");
	veejay_msg(1,  "p0 = UV rotation angle");
	veejay_msg(1,  "p1 = U rotate center");
	veejay_msg(1,  "p2 = V rotate center");
	veejay_msg(1,  "p3 = U factor");
	veejay_msg(1,  "p4 = V factor");
	veejay_msg(1,  "p5 = UV min");
	veejay_msg(1,  "p6 = UV max"); */
}

static inline void _chrominance_treatment(uint8_t *u,uint8_t *v, const int len)
{
  uint8_t *Uu_c_p, *Vu_c_p;
  uint32_t i, base;

  Uu_c_p = u;
  Vu_c_p = v;

  // Chroma
  for (i = 0; i < len; i++)
    {
      base = ((((uint32_t) * Uu_c_p) << 8) + (*Vu_c_p)) << 1;	// base = ((((uint32_t)*Uu_c_p) * 256) + (*Vu_c_p)) * 2
      *(Uu_c_p++) = chrominance[base++];
      *(Vu_c_p++) = chrominance[base];
    }

}

void uvcorrect_apply(VJFrame *frame, int width, int height, int angle, int urot_center, int vrot_center, int iuFactor, int ivFactor, int uv_min, int uv_max )
{
	float fU,fV,si,co;
	uint16_t iU,iV;

	const float f_angle = (float) angle / 180.0 * M_PI; 
	const uint8_t centerU = urot_center;
	const uint8_t centerV = vrot_center;
	const float Ufactor = (float)iuFactor * 0.1;
	const float Vfactor = (float)ivFactor * 0.1;
	const uint32_t uv_len = frame->uv_len;
	const uint8_t uvmin = (uint8_t) uv_min;
	const uint8_t uvmax = (uint8_t) uv_max;
	uint8_t *Uplane = frame->data[1];
	uint8_t *Vplane = frame->data[2];
	// chrominance vector
	uint8_t *table = chrominance;

	sin_cos ( si, co, f_angle );


	for ( iU = 0; iU <= 255 ; iU ++ )
	{
		for( iV = 0; iV <= 255; iV ++ )
		{
			//U component  
			fU =  (((float) (iU - centerU ) * Ufactor ) * co - 
			       ((float) (iV - centerV ) * Vfactor ) * si) +
				 128.0;

			fU = (float) floor( 0.5 + fU );

			//clamp U values
			if( fU  < uvmin )
			{
				fU = uvmin;
			}
			if( fU  > uvmax )
			{
				fU = uvmax;
			}
			//V component

			fV = (((float) ( iV - centerV) * Vfactor ) * co + 
		 	      ((float) ( iU - centerU) * Ufactor ) * si ) + 
				128.0;

			fV = (float) floor( 0.5 + fV );

			//clamp V values
			if(  fV < uvmin )
				fV = uvmin;
			if(  fV > uvmax )
				fV = uvmax;

			//store in vector
			*(table)++ = (uint8_t) fU;
			*(table)++ = (uint8_t) fV;
		}
	}

	_chrominance_treatment( Uplane,Vplane , uv_len );


}
