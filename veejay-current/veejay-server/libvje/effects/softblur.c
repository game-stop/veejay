/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include <stdlib.h>
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "softblur.h"
#include "common.h"

vj_effect *softblur_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1; /* 3*/
    ve->description = "Soft Blur (1x3) and (3x3)";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Kernel size");
    return ve;
}

void softblur1_apply( VJFrame *frame, int width, int height)
{
    int r, c;
    int len = (width * height);
 	uint8_t *Y = frame->data[0];
    for (r = 0; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
	    Y[c + r] = (Y[r + c - 1] +
			      Y[r + c] +
			      Y[r + c + 1]
				) / 3;
				
	}
    }

}

void softblur3_apply(VJFrame *frame, int width, int height ) {
	int r,c;
	uint8_t *Y = frame->data[0];
	const int len = frame->len;
	
	for(c = 1; c < width - 1; c ++ )
	   Y[c ] = (Y[c - 1] +
	      Y[c ] +
	      Y[c + 1]
		) / 3;




	for(r=width; r < (len-width); r+=width) {
		for(c=1; c < (width-1); c++) {
			Y[r+c] = ( 	Y[r - width + c - 1] +
				   	Y[r - width + c ] +
					Y[r + c + 1] +
					Y[r - width + c] +
					Y[r + c] + 
					Y[r + c + 1] +
					Y[r + width + c - 1] +
					Y[r + width + c] +
					Y[r + width + c + 1]  ) / 9;

		}
	}

	for( c = (len-width) ; c < len; c ++ )
	  Y[c ] = (Y[c - 1] +
	      Y[c] +
	      Y[c + 1]
		) / 3;


}
    	
#ifdef HAVE_ASM_MMX
/* mmx_blur() taken from libvisual plugins
 *
 * Libvisual-plugins - Standard plugins for libvisual
 *
 * Copyright (C) 2002, 2003, 2004, 2005 Dennis Smit <ds@nerds-incorporated.org>
 *
 * Authors: Dennis Smit <ds@nerds-incorporated.org>
 */

static	void	mmx_blur(uint8_t *buffer, int width, int height)
{
	__asm __volatile
		("\n\t pxor %%mm6, %%mm6"
		 ::);

	int scrsh = (width * height) >> 1;
	int i;

	int len = (width * height);

	uint8_t *buf = buffer;
	/* Prepare substraction register */
	for (i = 0; i < scrsh; i += 4) {
		__asm __volatile
			("\n\t movd %[buf], %%mm0"
			 "\n\t movd %[add1], %%mm1"
			 "\n\t punpcklbw %%mm6, %%mm0"
			 "\n\t movd %[add2], %%mm2"
			 "\n\t punpcklbw %%mm6, %%mm1"
			 "\n\t movd %[add3], %%mm3"
			 "\n\t punpcklbw %%mm6, %%mm2"
			 "\n\t paddw %%mm1, %%mm0"
			 "\n\t punpcklbw %%mm6, %%mm3"
			 "\n\t paddw %%mm2, %%mm0"
			 "\n\t paddw %%mm3, %%mm0"
			 "\n\t psrlw $2, %%mm0"
			 "\n\t packuswb %%mm6, %%mm0"
			 "\n\t movd %%mm0, %[buf]"
			 :: [buf] "m" (*(buf + i))
			 , [add1] "m" (*(buf + i + width))
			 , [add2] "m" (*(buf + i + width + 1))
			 , [add3] "m" (*(buf + i + width - 1))
		  );
		//	 : "mm0", "mm1", "mm2", "mm3", "mm6");
	}

	len = (width*height)-4;

	for (i = len; i > scrsh; i -= 4) {
		__asm __volatile
			("\n\t movd %[buf], %%mm0"
			 "\n\t movd %[add1], %%mm1"
			 "\n\t punpcklbw %%mm6, %%mm0"
			 "\n\t movd %[add2], %%mm2"
			 "\n\t punpcklbw %%mm6, %%mm1"
			 "\n\t movd %[add3], %%mm3"
			 "\n\t punpcklbw %%mm6, %%mm2"
			 "\n\t paddw %%mm1, %%mm0"
			 "\n\t punpcklbw %%mm6, %%mm3"
			 "\n\t paddw %%mm2, %%mm0"
			 "\n\t paddw %%mm3, %%mm0"
			 "\n\t psrlw $2, %%mm0"
			 "\n\t packuswb %%mm6, %%mm0"
			 "\n\t movd %%mm0, %[buf]"
			 :: [buf] "m" (*(buf + i))
			 , [add1] "m" (*(buf + i - width))
			 , [add2] "m" (*(buf + i - width + 1))
			 , [add3] "m" (*(buf + i - width - 1))
		);//	 : "mm0", "mm1", "mm2", "mm3", "mm6");
	}

	do_emms;
}

#endif

void softblur_apply(VJFrame *frame, int width, int height, int type)
{
    switch (type) {
    case 0:
#ifdef HAVE_ASM_MMX
	mmx_blur( frame->data[0],width,height );
#else
	softblur1_apply(frame, width, height);
#endif
	break;
	break;
    case 1:
	softblur3_apply(frame, width, height);
	break;
    }
}

