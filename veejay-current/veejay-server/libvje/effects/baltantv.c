/* 
 * Linux VeeJay
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * BaltanTV - like StreakTV, but following for a long time
 * Copyright (C) 2001-2002 FUKUCHI Kentaro
 * Ported to veejay by Niels Elburg 
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
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "baltantv.h"
#include "common.h"

vj_effect *baltantv_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 16;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[0] = 8;
    ve->defaults[1] = 0;
    ve->description = "BaltanTV (EffecTV)";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Stride", "Mode" );
    return ve;
}

static	unsigned int	plane_ = 0;

#define	PLANES 64

static	uint8_t	*planetable_ = NULL;

int	baltantv_malloc(int w, int h)
{
	if( planetable_ )
		free(planetable_ );
	planetable_ = (uint8_t*) vj_malloc( sizeof(uint8_t*) * PLANES * (w * h ));
	if(!planetable_) {
		veejay_msg(0,"Not enough memory to allocate %d planes of size %dx%d", PLANES,w,h);
		return 0;
	}

	return 1;
}

void	baltantv_free()
{
	plane_ = 0;
	if(planetable_)
		free(planetable_);
	planetable_ = NULL;
}

void baltantv_apply( VJFrame *frame, int width, int height, int stride, int mode)
{
	unsigned int i,cf;
	const int len = (width * height);
	uint8_t *Y = frame->data[0];
	uint8_t *pDst = planetable_ + (plane_ * frame->len);

	for( i = 0; i < len ; i ++ )
		pDst[i] = (Y[i] >> 2 );

	cf = plane_ & (stride-1);	

	uint8_t *pSrc[4] = {
			planetable_ + (cf * frame->len),
			planetable_ + ((cf+stride) * frame->len),
			planetable_ + ((cf+stride*2) * frame->len),
			planetable_ + ((cf+stride*3) * frame->len)
		};

	if( mode == 0 )
	{
		for( i = 0; i < len; i ++ )
		{	
			Y[i] =  pSrc[0][i] + 
				pSrc[1][i] + 
				pSrc[2][i] + 
				pSrc[3][i];
			pDst[i] = (Y[i] >> 2 );
		}
	}
	else
	{
		for( i = 0; i < len ; i++ )
		{
			Y[i] =  pSrc[0][i] + 
				pSrc[1][i] + 
				pSrc[2][i] + 
				pSrc[3][i];
		}
	}
	plane_ ++;

	plane_ = plane_ & (PLANES-1);
}
