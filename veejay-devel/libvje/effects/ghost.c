/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nelburg@looze.net>
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
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "common.h"
#include "ghost.h"

static uint8_t 		*ghost_buf[3];
static uint8_t 		*diff_map;
static int			 diff_period;

vj_effect *ghost_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 16; 
    ve->limits[1][0] = 255;  // opacity
    ve->defaults[0] = 134;
    ve->description = "Motion Ghost";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user =0;
    return ve;
}

// FIXME private 
int	ghost_malloc(int w, int h)
{
	const int len = (w * h );
	int i;

	for( i = 0; i < 3 ; i ++ )
	{
   		ghost_buf[i] = (uint8_t*) vj_malloc( sizeof(uint8_t) * len);
		if(!ghost_buf[i])
			return 0;
		memset( ghost_buf[i], (i ==0 ? 0: 128), len );
	}
	diff_map = (uint8_t*) vj_malloc( sizeof(uint8_t) * len);
	memset( diff_map , 0, len );
	diff_period = 0;

	return 1;
}

void ghost_free()
{
	int i;
	for(i = 0 ; i < 3; i ++ )
	{
		if(ghost_buf[i])
			free(ghost_buf[i]);
		ghost_buf[i] = NULL;
	}
	if( diff_map )
		free(diff_map);
}

void ghost_apply(VJFrame *frame,
			   int width, int height, int opacity)
{
	register int q,z=0;
 	int x,y,i,tmp;
    const int len = frame->len;
    const unsigned int op_a = opacity;
    const unsigned int op_b = 255 - op_a;
	uint8_t *srcY = frame->data[0];
	uint8_t *srcCb= frame->data[1];
	uint8_t *srcCr= frame->data[2];
	uint8_t *dY  = ghost_buf[0];
	uint8_t *dCb = ghost_buf[1];
	uint8_t *dCr = ghost_buf[2];
	uint8_t *bm = diff_map;

	const uint8_t kernel[9] =
	{
		1,1,1,1,1,1,1,1,1
	};
	// first time running 
	if(diff_period == 0)
	{
		veejay_memcpy( dY, srcY, len   );
		veejay_memcpy( dCb,srcCb, len  );
		veejay_memcpy( dCr,srcCr, len  );
		diff_period = 1;
		return;
	}


	// absolute difference on threshold

	for(i = 0; i < len; i ++ )
		bm[i] = ( abs(srcY[i] - dY[i]) > 1 ? 0xff: 0x0);

	  

	for( y = width; y < (len-width); y += width )
	{
		for( x = 1; x < width - 1; x ++ ) 
		{
			// input matrix
			uint8_t mt[9] = {
				bm[ x-1+y-width],	bm[ x+y-width],		bm[x+1+y-width],
				bm[ x-1+y ],		bm[ x+y ],			bm[x+1+y],
				bm[ x-1+y+width],	bm[ x+y+width ],	bm[x+1+y+width]
				};

			for( q = 0; q < 9; q ++ )
			{
				if( kernel[q] && mt[q] ) // dilation
					{ z ++; break; }
			}

			if( z > 0 ) // accept 
			{
				// new pixel value back in ghost buf (feedback)
				dY[x+y] = ( (op_a * srcY[x+y]  ) + (op_b * dY[x+y]) ) >> 8;
				dCb[x+y] = ((op_a * srcCb[x+y] ) + (op_b * dCb[x+y])) >> 8;
				dCr[x+y] = ((op_a * srcCr[x+y] ) + (op_b * dCr[x+y])) >> 8;
				// put result to screen
				srcY[x+y] = dY[x+y];
				srcCb[x+y]= dCb[x+y];
				srcCr[x+y] = dCr[x+y];
			}

			z = 0;
		}
	}
}
