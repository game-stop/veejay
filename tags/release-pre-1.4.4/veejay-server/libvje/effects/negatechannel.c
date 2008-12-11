/* 
 * Linux VeeJay
 *
 * Copyright(C)2008 Niels Elburg <nwelburg@gmail.com>
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
#include <libvjmem/vjmem.h>
#include "negatechannel.h"

vj_effect *negatechannel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;
    ve->limits[0][1] = 0;
    ve->limits[1][2] = 0xff;
    ve->defaults[0] = 0;
    ve->defaults[1] = 0xff;
    ve->description = "Negate a channel";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Y=0, Cb=1, Cr=2", "Value" );
    return ve;
}

#ifdef HAVE_ASM_MMX
#undef HAVE_K6_2PLUS
#if !defined( HAVE_ASM_MMX2) && defined( HAVE_ASM_3DNOW )
#define HAVE_K6_2PLUS
#endif

#undef _EMMS

#ifdef HAVE_K6_2PLUS
/* On K6 femms is faster of emms. On K7 femms is directly mapped on emms. */
#define _EMMS     "femms"
#else
#define _EMMS     "emms"
#endif
static	inline void	negate_mask(uint8_t val)
{
	uint8_t mask[8] = { val,val,val,val,  val,val,val,val };
//	uint64_t mask = 0xffffffffffffffffLL;
        uint8_t *m    = (uint8_t*)&mask;
	
	__asm __volatile(
		"movq	(%0),	%%mm4\n\t"
		:: "r" (m) );
}

static	inline void	mmx_negate( uint8_t *dst, uint8_t *in )
{
	__asm __volatile(
		"movq	(%0),	%%mm0\n\t"
		"movq	%%mm4,	%%mm1\n\t"
		"psubb	%%mm0,  %%mm1\n\t"
		"movq	%%mm1,	(%1)\n\t"
		:: "r" (in) , "r" (dst)
	);
}

#endif

void negatechannel_apply( VJFrame *frame, int width, int height, int chan, int val)
{
    int i;
    int len = (width * height);
    int uv_len = frame->uv_len;

    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

#ifndef HAVE_ASM_MMX 
    switch( chan ) {
	case 0:
    for (i = 0; i < len; i++) {
	*(Y) = val - *(Y);
	*(Y)++;
    }
	break;
	case 1:

    for (i = 0; i < uv_len; i++) {
	*(Cb) = val - *(Cb);
        *(Cb)++;
    }
	break;
	case 2:
   for (i = 0; i < uv_len; i++) {
        *(Cr) = val - *(Cr);
	*(Cr)++;
    }
	break;
   }

#else

    int left = len % 8;
    int work=  len >> 3;

    negate_mask(val);

    switch( chan ) {
	case 0:
    for( i = 0; i < work ; i ++ )
    {
	mmx_negate( Y, Y );	
	Y += 8;
    }	

    if (left )
    {
	for( i = 0; i < left; i ++ )
	{
		*(Y) = val - *(Y);
		*(Y)++;
	}	
    }
	break;
	case 1:

    work = uv_len >> 3;
    left = uv_len % 8;
    for( i = 0; i < work ; i ++ )
    {
	mmx_negate( Cb, Cb );
	Cr += 8;
    }

    if(left )
    {
	for( i = 0; i < left; i ++ )
	{
		*(Cb) = val - *(Cb);
      	 	*(Cb)++;
	}
    }
	break;
	case 2:
    work = uv_len >> 3;
    left = uv_len % 8;
    for( i = 0; i < work ; i ++ )
    {
	mmx_negate( Cr, Cr );
	Cr += 8;
    }

    if(left )
    {
	for( i = 0; i < left; i ++ )
	{
     	  	*(Cr) = val - *(Cr);
		*(Cr)++;
	}
    }
	break;
 }

	__asm__ __volatile__ ( _EMMS:::"memory");

#endif
}
