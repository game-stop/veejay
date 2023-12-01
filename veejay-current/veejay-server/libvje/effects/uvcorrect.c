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
#include "common.h"
#include <veejaycore/vjmem.h>
#include "uvcorrect.h"

typedef struct {
    uint8_t *chrominance;
} uvcorrect_t;

vj_effect *uvcorrect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
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

    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_help = 1;
    ve->has_user = 0;
    
    return ve;
}

void *uvcorrect_malloc(int w, int h )
{
    uvcorrect_t *uv = (uvcorrect_t*) vj_calloc(sizeof(uvcorrect_t));
    if(!uv) {
        return NULL;
    }
    uv->chrominance = (uint8_t*) vj_malloc (sizeof(uint8_t) * 2 * 256 * 256 );
    if(!uv->chrominance) {
        free(uv);
        return NULL;
    }
    return uv;
}

void    uvcorrect_free(void *ptr)
{
    uvcorrect_t *uv = (uvcorrect_t*) ptr;
    if(uv->chrominance) free(uv->chrominance);
    free(uv);
}

static inline void _chrominance_treatment(uvcorrect_t *uv, uint8_t *u,uint8_t *v, const int len)
{
    uint8_t *restrict Uu_c_p = u;
    uint8_t *restrict Vu_c_p = v;
    uint32_t i, base;


    const uint8_t *restrict chroma = uv->chrominance;

    for (i = 0; i < len; i++)
    {
      base = ((((uint32_t) * Uu_c_p) << 8) + (*Vu_c_p)) << 1;   
      *(Uu_c_p++) = chroma[base++];
      *(Vu_c_p++) = chroma[base];
    }

}

void uvcorrect_apply(void *ptr, VJFrame *frame, int *args )
{
    float fU,fV,si,co;
    uint16_t iU,iV;
    uvcorrect_t *uv = (uvcorrect_t*) ptr;
    uint8_t *Uplane = frame->data[1];
    uint8_t *Vplane = frame->data[2];
    // chrominance vector
    uint8_t *table = uv->chrominance;
    int angle = args[0];
    int urot_center = args[1];
    int vrot_center = args[2];
    int iuFactor = args[3];
    int ivFactor = args[4];
    int uv_min = args[5];
    int uv_max = args[6];
    const uint8_t centerU = urot_center;
    const uint8_t centerV = vrot_center;
    const float Ufactor = (float)iuFactor * 0.1;
    const float Vfactor = (float)ivFactor * 0.1;
    const int uv_len = (frame->ssm ? frame->len : frame->uv_len);
    const uint8_t uvmin = (uint8_t) uv_min;
    const uint8_t uvmax = (uint8_t) uv_max;
    const float f_angle = (float) angle / 180.0 * M_PI; 

    sin_cos ( si, co, f_angle );


    for ( iU = 0; iU <= 255 ; iU ++ )
    {
        float term = ( (float) (iU - centerU ) * Ufactor );
        for( iV = 0; iV <= 255; iV ++ )
        {
            //U component  
            fU =  ( (term * co) - 
                   ((float) (iV - centerV ) * Vfactor ) * si) +
                 128.0;

            fU = (float) floor( 0.5 + fU );

            fU = ( fU < uvmin ? uvmin : fU > uvmax ? uvmax : fU );

            //V component
            fV = ((float) (iV - centerV ) * Vfactor ) * co  +  
                   (term * si ) + 
                128.0;

            fV = (float) floor( 0.5 + fV );

            fV = ( fV < uvmin ? uvmin : fU > uvmax ? uvmax: fV );

            //store in vector
            *(table)++ = (uint8_t) fU;
            *(table)++ = (uint8_t) fV;
        }
    }

    _chrominance_treatment( uv, Uplane,Vplane , uv_len );


}
