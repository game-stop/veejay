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

#include <config.h>
#include <stdint.h>
#include <sys/types.h>
#include "common.h"
#include "../vj-effect.h"
#include "../vj-lib.h"
#include "../vj-common.h"


static inline void linearBlend(unsigned char *src, int stride)
{
/*
#ifdef HAVE_ASM_MMX
#define PAVGB(a,b)  "pavgb " #a ", " #b " \n\t"

  asm volatile(
       "leal (%0, %1), %%eax                           \n\t"
       "leal (%%eax, %1, 4), %%ebx                     \n\t"

       "movq (%0), %%mm0                               \n\t" // L0
       "movq (%%eax, %1), %%mm1                        \n\t" // L2
       PAVGB(%%mm1, %%mm0)                                   // L0+L2
       "movq (%%eax), %%mm2                            \n\t" // L1
       PAVGB(%%mm2, %%mm0)
       "movq %%mm0, (%0)                               \n\t"
       "movq (%%eax, %1, 2), %%mm0                     \n\t" // L3
       PAVGB(%%mm0, %%mm2)                                   // L1+L3
       PAVGB(%%mm1, %%mm2)                                   // 2L2 + L1 + L3
       "movq %%mm2, (%%eax)                            \n\t"
       "movq (%0, %1, 4), %%mm2                        \n\t" // L4
       PAVGB(%%mm2, %%mm1)                                   // L2+L4
       PAVGB(%%mm0, %%mm1)                                   // 2L3 + L2 + L4
       "movq %%mm1, (%%eax, %1)                        \n\t"
       "movq (%%ebx), %%mm1                            \n\t" // L5
       PAVGB(%%mm1, %%mm0)                                   // L3+L5
       PAVGB(%%mm2, %%mm0)                                   // 2L4 + L3 + L5
       "movq %%mm0, (%%eax, %1, 2)                     \n\t"
       "movq (%%ebx, %1), %%mm0                        \n\t" // L6
       PAVGB(%%mm0, %%mm2)                                   // L4+L6
       PAVGB(%%mm1, %%mm2)                                   // 2L5 + L4 + L6
       "movq %%mm2, (%0, %1, 4)                        \n\t"
       "movq (%%ebx, %1, 2), %%mm2                     \n\t" // L7
       PAVGB(%%mm2, %%mm1)                                   // L5+L7
       PAVGB(%%mm0, %%mm1)                                   // 2L6 + L5 + L7
       "movq %%mm1, (%%ebx)                            \n\t"
      "movq (%0, %1, 8), %%mm1                        \n\t" // L8
       PAVGB(%%mm1, %%mm0)                                   // L6+L8
       PAVGB(%%mm2, %%mm0)                                   // 2L7 + L6 + L8
       "movq %%mm0, (%%ebx, %1)                        \n\t"
       "movq (%%ebx, %1, 4), %%mm0                     \n\t" // L9
       PAVGB(%%mm0, %%mm2)                                   // L7+L9
       PAVGB(%%mm1, %%mm2)                                   // 2L8 + L7 + L9
       "movq %%mm2, (%%ebx, %1, 2)                     \n\t"

       : : "r" (src), "r" (stride)
       : "%eax", "%ebx"
  );
 #undef PAVGB
#else
*/
  int x;
  for (x=0; x<8; x++)
  {
     src[0       ] = (src[0       ] + 2*src[stride  ] + src[stride*2])>>2;
     src[stride  ] = (src[stride  ] + 2*src[stride*2] + src[stride*3])>>2;
     src[stride*2] = (src[stride*2] + 2*src[stride*3] + src[stride*4])>>2;
     src[stride*3] = (src[stride*3] + 2*src[stride*4] + src[stride*5])>>2;
     src[stride*4] = (src[stride*4] + 2*src[stride*5] + src[stride*6])>>2;
     src[stride*5] = (src[stride*5] + 2*src[stride*6] + src[stride*7])>>2;
     src[stride*6] = (src[stride*6] + 2*src[stride*7] + src[stride*8])>>2;
     src[stride*7] = (src[stride*7] + 2*src[stride*8] + src[stride*9])>>2;

     src++;
  }
//#endif
}




/* some parts (linearBlend() and deinterlace() from :
 * 
 * Simple xawtv deinterlacing plugin - linear blend
 * 
 * CAVEATS: Still some interlacing effects in high motion perhaps
 * Some ghosting in instant transitions, slightly noticeable
 * 
 * BENEFITS: NO DROP IN FRAMERATE =]
 * Looks absolutely beautiful
 * Doesn't lower framerate
 * Oh and did I mention it doesn't lower framerate?
 * Plus, its MMX'itized now, so it really doesn't lower framerate.
 *
 * AUTHORS:
 * Conrad Kreyling <conrad@conrad.nerdland.org>
 * Patrick Barrett <yebyen@nerdland.org>
 *
 * This is licenced under the GNU GPL until someone tells me I'm stealing code
 * and can't do that ;) www.gnu.org for any version of the license.
 *
 * Based on xawtv-3.72/libng/plugins/flt-nop.c (also GPL)
 * Linear blend deinterlacing algorithm adapted from mplayer's libpostproc
 */



void	deinterlace(uint8_t *data, int w, int h, int v)
{
	int x,y;
	uint8_t *src;
	for(y=1; y < h-8; y+=8)
	{
		for(x=0; x < w; x+=8)
		{
			src = data + x + y * w;	
			linearBlend(src, w);
		}
	}
//#ifdef HAVE_ASM_MMX
//	  __asm__ __volatile__ ("emms");
//#endif
}


void frameborder_ycbcrdata(uint8_t * input_y, uint8_t * input_u,
			 uint8_t * input_v, uint8_t * putin_y,
			 uint8_t * putin_u, uint8_t * putin_v, int width,
			 int height, int top, int bottom, int left,
			 int right)
{

    int line, x;
    int input_active_width;
    int input_active_height;
    uint8_t *rightvector;
    uint8_t *rightvectoru;
    uint8_t *rightvectorv;

    input_active_height = height - top - bottom;
    input_active_width = width - left - right;

    /* Y component TOP */
    for (line = 0; line < top; line++) {
	for (x = 0; x < width; x++) {
	    *(input_y + x) = *(putin_y + x);
	    *(input_u + x) = *(putin_u + x);
	    *(input_v + x) = *(putin_v + x);
	}
	//memcpy (input_y, putin_y, width);
	input_y += width;
	putin_y += width;
	input_u += width;
	putin_u += width;
	input_v += width;
	putin_v += width;
    }

    rightvector = input_y + left + input_active_width;
    rightvectoru = input_u + left + input_active_width;
    rightvectorv = input_v + left + input_active_width;

    /* Y component LEFT AND RIGHT */
    for (line = 0; line < input_active_height; line++) {
	for (x = 0; x < left; x++) {
	    *(input_y + x) = *(putin_y + x);
	    *(input_u + x) = *(putin_u + x);
	    *(input_v + x) = *(putin_v + x);
	}
	//memcpy (input_y, putin_y, left);
	for (x = 0; x < right; x++) {
	    *(rightvector + x) =
		*(putin_y + left + input_active_width + x);
	    *(rightvectoru + x) =
		*(putin_u + left + input_active_width + x);
	    *(rightvectorv + x) =
		*(putin_v + left + input_active_width + x);
	}
	//memcpy (rightvector, putin_y + left + input_active_width, right);
	input_y += width;
	rightvector += width;
	putin_y += width;

	input_u += width;
	rightvectoru += width;
	putin_u += width;

	input_v += width;
	rightvectorv += width;
	putin_v += width;

    }

    /* Y component BOTTOM  */
    for (line = 0; line < bottom; line++) {
	for (x = 0; x < width; x++) {
	    *(input_y + x) = *(putin_y + x);
	    *(input_u + x) = *(putin_u + x);
	    *(input_v + x) = *(putin_v + x);
	}
	//memcpy (input_y, putin_y, width);
	input_y += width;
	putin_y += width;
	
	input_u += width;
	putin_u += width;

	input_v += width;
	input_v += width;
    }

}


/* deinterlace:
   Copyright (C) 2001,2002 Kawamata/Hitoshi <hitoshi.kawamata@nifty.ne.jp>

   yuvkineco , unused now, using linear blend plugin from simple xawtv deinterlace plugins 
deinterlace(uint8_t *data, int width, int height, int noise)
{
  int pln, i, j, n = 0;
  for (pln = 0; pln < 3; pln++) {
    for (i = 1; i < height - 3; i += 2) {
      for (j = 0; j < width; j++) {
	int ymin = data[((i - 1) * width) + j];
	int ynow = data[((i    ) * width) + j];
	int ymax = data[((i + 1) * width) + j];
	int ynxt = data[((i + 2) * width) + j];
	if (ymax < ymin) {
	  int ytmp = ymax; ymax = ymin; ymin = ytmp;
	}
	ymin -= noise;
	ymax += noise;
	if ((ynow < ymin && ynxt < ymin ) || (ymax < ynow && ymax < ynxt ))
	  n++;
	else if (!(ynow < ymin || ymax < ynow))
	  continue;
	ynow = (ymin + ymax) / 2;
	data[((i    ) * width) + j] = ynow;
	if (i < height - 3) {
	    continue;
	}
	data[((i + 2) * width) + j] = ynow;
      }
    }
    data += width * height;
    if (pln == 0) {
      width  /= 2;
      height /= 2;
    }
  }
  return n;
}


*/

/**********************************************************************************************
 *
 * frameborder_yuvdata, based upon blackborder_yuvdata.
 * instead of making the borders black, fill it with pixels coming from the second YUV420 frame.
 **********************************************************************************************/
void frameborder_yuvdata(uint8_t * input_y, uint8_t * input_u,
			 uint8_t * input_v, uint8_t * putin_y,
			 uint8_t * putin_u, uint8_t * putin_v, int width,
			 int height, int top, int bottom, int left,
			 int right)
{

    int line, x;
    int input_active_width;
    int input_active_height;
    uint8_t *rightvector;

    input_active_height = height - top - bottom;
    input_active_width = width - left - right;

    /* Y component TOP */
    for (line = 0; line < top; line++) {
	for (x = 0; x < width; x++) {
	    *(input_y + x) = *(putin_y + x);
	}
	//memcpy (input_y, putin_y, width);
	input_y += width;
	putin_y += width;
    }
    rightvector = input_y + left + input_active_width;
    /* Y component LEFT AND RIGHT */
    for (line = 0; line < input_active_height; line++) {
	for (x = 0; x < left; x++) {
	    *(input_y + x) = *(putin_y + x);
	}
	//memcpy (input_y, putin_y, left);
	for (x = 0; x < right; x++) {
	    *(rightvector + x) =
		*(putin_y + left + input_active_width + x);
	}
	//memcpy (rightvector, putin_y + left + input_active_width, right);
	input_y += width;
	rightvector += width;
	putin_y += width;
    }
    /* Y component BOTTOM  */
    for (line = 0; line < bottom; line++) {
	for (x = 0; x < width; x++)
	    *(input_y + x) = *(putin_y + x);

	//memcpy (input_y, putin_y, width);
	input_y += width;
	putin_y += width;
    }


    /* U component TOP */
    for (line = 0; line < (top >> 1); line++) {
	for (x = 0; x < (width >> 1); x++) {
	    *(input_u + x) = *(putin_u + x);
	}
	//memcpy (input_u, putin_u, width >> 1);
	input_u += width >> 1;
	putin_u += width >> 1;
    }

    rightvector = input_u + ((left + input_active_width) >> 1);
    for (line = 0; line < (input_active_height >> 1); line++) {
	//memcpy (input_u, putin_u, left >> 1);
	for (x = 0; x < (left >> 1); x++) {
	    *(input_u + x) = *(putin_u + x);
	}
	//memcpy (rightvector, putin_u + ((left + input_active_width)>>1) , right >> 1);
	for (x = 0; x < (right >> 1); x++) {
	    *(rightvector + x) = *(putin_u +
				   ((left + input_active_width + x) >> 1));

	}
	input_u += width >> 1;
	rightvector += width >> 1;
	putin_u += width >> 1;
    }

    for (line = 0; line < (bottom >> 1); line++) {
	for (x = 0; x < (width >> 1); x++)
	    *(input_u + x) = *(putin_u + x);
	//memcpy (input_u, putin_u, width >> 1);
	input_u += width >> 1;
	putin_u += width >> 1;
    }

    /* V component Top */
    for (line = 0; line < (top >> 1); line++) {
	//memcpy (input_v, putin_v, width >> 1);
	for (x = 0; x < (width >> 1); x++) {
	    *(input_v + x) = *(putin_v + x);
	}
	input_v += width >> 1;
	putin_v += width >> 1;
    }
    /* Left and Right */
    rightvector = input_v + ((left + input_active_width) >> 1);
    for (line = 0; line < (input_active_height >> 1); line++) {
	for (x = 0; x < (left >> 1); x++)
	    *(input_v + x) = *(putin_v + x);

	//memcpy (input_v, putin_v, left >> 1);
	//memcpy (rightvector, putin_v + ((left+input_active_width)>>1), right >> 1);
	for (x = 0; x < (right >> 1); x++)
	    *(rightvector + x) =
		*(putin_v + ((left + input_active_width + x) >> 1));

	input_v += width >> 1;
	rightvector += width >> 1;
	putin_v += width >> 1;
    }
    /* Bottom */
    for (line = 0; line < (bottom >> 1); line++) {
	//memcpy (input_v, putin_v, width >> 1);
	for (x = 0; x < (width >> 1); x++)
	    *(input_v + x) = *(putin_v + x);
	input_v += width >> 1;
	putin_v += width >> 1;
    }
}



/* comes from one of the mjpegtool's yuvtools, prob. yuvdenoise */
void blackborder_yuvdata(uint8_t * input_y, uint8_t * input_u,
			 uint8_t * input_v, int width, int height, int top,
			 int bottom, int left, int right, int color)
{

    int line;
    int input_active_width;
    int input_active_height;
    uint8_t *rightvector;
    int colorY, colorCb, colorCr;

    colorY = bl_pix_get_color_y(color);
    colorCb = bl_pix_get_color_cb(color);
    colorCr = bl_pix_get_color_cr(color);
    input_active_height = height - top - bottom;
    input_active_width = width - left - right;

    /* Y component TOP */
    for (line = 0; line < top; line++) {
	mymemset_generic(input_y, colorY, width);
	input_y += width;
    }
    rightvector = input_y + left + input_active_width;
    /* Y component LEFT AND RIGHT */
    for (line = 0; line < input_active_height; line++) {
	mymemset_generic(input_y, colorY, left);
	mymemset_generic(rightvector, colorY, right);
	input_y += width;
	rightvector += width;
    }
    /* Y component BOTTOM  */
    for (line = 0; line < bottom; line++) {
	mymemset_generic(input_y, colorY, width);
	input_y += width;
    }


    /* U component TOP */
    for (line = 0; line < (top >> 1); line++) {
	mymemset_generic(input_u, colorCb, width >> 1);
	input_u += width >> 1;
    }

    rightvector = input_u + ((left + input_active_width) >> 1);
    for (line = 0; line < (input_active_height >> 1); line++) {
	mymemset_generic(input_u, colorCb, left >> 1);
	mymemset_generic(rightvector, colorCb, right >> 1);
	input_u += width >> 1;
	rightvector += width >> 1;
    }

    for (line = 0; line < (bottom >> 1); line++) {
	mymemset_generic(input_u, colorCb, width >> 1);
	input_u += width >> 1;
    }

    /* V component Top */
    for (line = 0; line < (top >> 1); line++) {
	mymemset_generic(input_v, colorCr, width >> 1);
	input_v += width >> 1;
    }
    /* Left and Right */
    rightvector = input_v + ((left + input_active_width) >> 1);
    for (line = 0; line < (input_active_height >> 1); line++) {
	mymemset_generic(input_v, colorCr, left >> 1);
	mymemset_generic(rightvector, colorCr, right >> 1);
	input_v += width >> 1;
	rightvector += width >> 1;
    }
    /* Bottom */
    for (line = 0; line < (bottom >> 1); line++) {
	mymemset_generic(input_v, colorCr, width >> 1);
	input_v += width >> 1;
    }
}

// fastrand (C) FUKUCHI, Kentaro (EffectTV)
unsigned int fastrand(int val)
{
    return (val = val * 1103516245 + 12345);
}


/* function to blend luminance pixel */
pix_func_Y get_pix_func_Y(const int pix_type)
{
    if (pix_type == 0)
	return &bl_pix_noswap_Y;
    if (pix_type == VJ_EFFECT_BLEND_ADDDISTORT)
	return &bl_pix_add_distorted_Y;
    if (pix_type == VJ_EFFECT_BLEND_SUBDISTORT)
	return &bl_pix_sub_distorted_Y;
    if (pix_type == VJ_EFFECT_BLEND_MULTIPLY)
	return &bl_pix_multiply_Y;
    if (pix_type == VJ_EFFECT_BLEND_DIVIDE)
	return &bl_pix_divide_Y;
    if (pix_type == VJ_EFFECT_BLEND_ADDITIVE)
	return &bl_pix_additive_Y;
    if (pix_type == VJ_EFFECT_BLEND_SUBSTRACTIVE)
	return &bl_pix_substract_Y;
    if (pix_type == VJ_EFFECT_BLEND_SOFTBURN)
	return &bl_pix_softburn_Y;
    if (pix_type == VJ_EFFECT_BLEND_INVERSEBURN)
	return &bl_pix_inverseburn_Y;
    if (pix_type == VJ_EFFECT_BLEND_COLORDODGE)
	return &bl_pix_colordodge_Y;
    if (pix_type == VJ_EFFECT_BLEND_MULSUB)
	return &bl_pix_mulsub_Y;
    if (pix_type == VJ_EFFECT_BLEND_LIGHTEN)
	return &bl_pix_lighten_Y;
    if (pix_type == VJ_EFFECT_BLEND_DIFFERENCE)
	return &bl_pix_difference_Y;
    if (pix_type == VJ_EFFECT_BLEND_DIFFNEGATE)
	return &bl_pix_diffnegate_Y;
    if (pix_type == VJ_EFFECT_BLEND_EXCLUSIVE)
	return &bl_pix_exclusive_Y;
    if (pix_type == VJ_EFFECT_BLEND_BASECOLOR)
	return &bl_pix_basecolor_Y;
    if (pix_type == VJ_EFFECT_BLEND_FREEZE)
	return &bl_pix_freeze_Y;
    if (pix_type == VJ_EFFECT_BLEND_UNFREEZE)
	return &bl_pix_unfreeze_Y;
    if (pix_type == VJ_EFFECT_BLEND_HARDLIGHT)
	return &bl_pix_hardlight_Y;
    if (pix_type == VJ_EFFECT_BLEND_RELADD)
	return &bl_pix_relativeadd_Y;
    if (pix_type == VJ_EFFECT_BLEND_RELSUB)
	return &bl_pix_relativesub_Y;
    if (pix_type == VJ_EFFECT_BLEND_MAXSEL)
	return &bl_pix_maxsel_Y;
    if (pix_type == VJ_EFFECT_BLEND_MINSEL)
	return &bl_pix_minsel_Y;
    if (pix_type == VJ_EFFECT_BLEND_RELADDLUM)
	return &bl_pix_relativeadd_Y;
    if (pix_type == VJ_EFFECT_BLEND_RELSUBLUM)
	return &bl_pix_relativesub_Y;
    if (pix_type == VJ_EFFECT_BLEND_MINSUBSEL)
	return &bl_pix_minsubsel_Y;
    if (pix_type == VJ_EFFECT_BLEND_MAXSUBSEL)
	return &bl_pix_maxsubsel_Y;
    if (pix_type == VJ_EFFECT_BLEND_ADDSUBSEL)
	return &bl_pix_addsubsel_Y;
    if (pix_type == VJ_EFFECT_BLEND_ADDAVG)
	return &bl_pix_dblbneg_Y;
    if (pix_type == VJ_EFFECT_BLEND_ADDTEST2)
	return &bl_pix_dblbneg_Y;
    if (pix_type == VJ_EFFECT_BLEND_ADDTEST3)
	return &bl_pix_relneg_Y;
    if (pix_type == VJ_EFFECT_BLEND_ADDTEST4)
	return &bl_pix_test3_Y;
    /*
       if(pix_type == VJ_EFFECT_BLEND_SELECTMIN) return &bl_pix_minsel_Y;
       if(pix_type == VJ_EFFECT_BLEND_SELECTMAX) return &bl_pix_maxsel_Y;   
       if(pix_type == VJ_EFFECT_BLEND_SELECTDIFF) return &bl_pix_seldiff_Y;
       if(pix_type == VJ_EFFECT_BLEND_SELECTDIFFNEG) return &bl_pix_seldiffneg_Y;
       if(pix_type == VJ_EFFECT_BLEND_ADDLUM) return &bl_pix_relativeadd_Y;
       if(pix_type == VJ_EFFECT_BLEND_SELECTFREEZE) return &bl_pix_selfreeze_Y;
       if(pix_type == VJ_EFFECT_BLEND_SELECTUNFREEZE) return &bl_pix_selunfreeze_Y;
     */
    return &bl_pix_swap_Y;
}

/* function to blend chrominance pixel */
pix_func_C get_pix_func_C(const int pix_type)
{
    if (pix_type == 0)
	return &bl_pix_noswap_C;
    if (pix_type == VJ_EFFECT_BLEND_ADDDISTORT)
	return &bl_pix_add_distorted_C;
    if (pix_type == VJ_EFFECT_BLEND_SUBDISTORT)
	return &bl_pix_sub_distorted_C;
    if (pix_type == VJ_EFFECT_BLEND_RELADD)
	return &bl_pix_relativeadd_C;
    if (pix_type == VJ_EFFECT_BLEND_ADDTEST2)
	return &bl_pix_dblbneg_C;
    if (pix_type == VJ_EFFECT_BLEND_ADDTEST3)
	return &bl_pix_relneg_C;
    if (pix_type == VJ_EFFECT_BLEND_ADDTEST4)
	return &bl_pix_test3_C;
    /*
       if(pix_type == VJ_EFFECT_BLEND_SELECTMAX) return &bl_pix_swap_C;     
       if(pix_type == VJ_EFFECT_BLEND_SELECTDIFF) return &bl_pix_swap_C;
       if(pix_type == VJ_EFFECT_BLEND_SELECTDIFFNEG) return &bl_pix_swap_C;
       if(pix_type == VJ_EFFECT_BLEND_SELECTFREEZE) return &bl_pix_swap_C;
       if(pix_type == VJ_EFFECT_BLEND_SELECTUNFREEZE) return &bl_pix_swap_C;
     */
    return &bl_pix_swap_C;
}


/* point arithemetic , these are blending functions. */

/* multiply pixel a with pixel b */
uint8_t bl_pix_multiply_Y(uint8_t y1, uint8_t y2)
{
    uint8_t new_Y = (y1 * y2) >> 8;
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

/* divide pixel a with pixel b */
uint8_t bl_pix_divide_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = (y1 * y2);
    b = 256 - y2;
    if (b < 16)
	b = 16;
    new_Y = a / b;
    if (new_Y > 235)
	new_Y = 235;
    if (new_Y < 16)
	new_Y = 16;

    return new_Y;
}

uint8_t bl_pix_additive_Y(uint8_t y1, uint8_t y2)
{
    uint8_t new_Y = y1 + (2 * y2) - 235;
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}


uint8_t bl_pix_substract_Y(uint8_t y1, uint8_t y2)
{
    uint8_t new_Y = y1 + (y2 - 235);
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_softburn_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (a + b < 235) {
	if (a == 235) {
	    new_Y = a;
	} else {
	    new_Y = (b >> 7) / (256 - a);
	    if (new_Y > 235)
		new_Y = 235;
	}
    } else {
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = 16;
	new_Y = 256 - (((256 - a) >> 7) / b);
	if (new_Y < 16)
	    new_Y = 16;
    }
    return new_Y;
}

uint8_t bl_pix_inverseburn_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (a < 16) {
	new_Y = 16;
    } else {
	new_Y = 256 - (((256 - b) >> 8) / a);
	if (new_Y < 16)
	    new_Y = 16;
    }
    return new_Y;
}


uint8_t bl_pix_colordodge_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (a > 235) {
	new_Y = 235;
    } else {
	new_Y = (b >> 8) / (256 - a);
	if (new_Y > 235)
	    new_Y = 235;
    }
    return new_Y;
}

uint8_t bl_pix_mulsub_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = (256 - y2);
    if (b < 16)
	b = 16;
    new_Y = a / b;
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_lighten_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (a > b) {
	new_Y = a;
    } else {
	new_Y = b;
    }
    return new_Y;
}

uint8_t bl_pix_difference_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    new_Y = abs(a - b);
    return new_Y;
}

uint8_t bl_pix_diffnegate_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = 256 - y1;
    b = y2;
    new_Y = 256 - abs(a - b);
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_exclusive_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    new_Y = a + b - ((a * b) >> 8);
    return new_Y;
}


uint8_t bl_pix_basecolor_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, c, new_Y;
    a = y1;
    b = y2;
    if (a < 16)
	a = 16;
    if (b < 16)
	b = 16;
    c = a * b >> 7;
    new_Y = c + a * ((256 - (((256 - a) * (256 - b)) >> 7) - c) >> 7);
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_freeze_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (b < 16) {
	new_Y = 16;
    } else {
	new_Y = 256 - ((256 - a) * (256 - a)) / b;
	if (new_Y < 16)
	    new_Y = 16;
	if (new_Y > 235)
	    new_Y = 235;
    }
    return new_Y;
}

uint8_t bl_pix_unfreeze_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (a < 16) {
	new_Y = 16;
    } else {
	new_Y = 256 - ((256 - b) * (256 - b)) / a;
	if (new_Y > 235)
	    new_Y = 235;
	if (new_Y < 16)
	    new_Y = 16;
    }
    return new_Y;
}

uint8_t bl_pix_hardlight_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (b < 128) {
	new_Y = (a * b) >> 7;
    } else {
	new_Y = 256 - ((256 - b) * (256 - a) >> 7);
    }
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_relativeadd_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, c, d, new_Y;
    a = y1;
    b = y2;
    c = a >> 1;
    d = b >> 1;
    new_Y = c + d;
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_relativeadd_C(uint8_t y1, uint8_t y2)
{
    uint8_t new_C;
    new_C = (y1 - y2 + 256) >> 1;
    if (new_C < 16)
	new_C = 16;
    if (new_C > 240)
	new_C = 240;
    return new_C;
}

uint8_t bl_pix_relativesub_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    new_Y = (a - b + 256) >> 1;
    if (new_Y > 235)
	new_Y = 235;
    if (new_Y < 16)
	new_Y = 16;
    return new_Y;
}

uint8_t bl_pix_maxsubsel_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (b > a) {
	new_Y = (b - a + 256) >> 1;
    } else {
	new_Y = (a - b + 256) >> 1;
    }
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_minsubsel_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (b < a) {
	new_Y = (b - a + 256) >> 1;
    } else {
	new_Y = (a - b + 256) >> 1;
    }
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_addsubsel_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (b < 16)
	b = 16;
    else if (b > 235)
	b = 235;
    if (a < 16)
	a = 16;
    else if (a > 235)
	a = 235;
    if (b < a) {
	new_Y = (a + b) >> 1;
	return new_Y;
    }
    return y1;
}

uint8_t bl_pix_maxsel_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b;
    a = y1;
    b = y2;
    if (b > a) {
	return b;
    }
    return y1;
}

uint8_t bl_pix_minsel_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b;
    a = y1;
    b = y2;
    if (b < a) {
	return b;
    }
    return y1;
}


uint8_t bl_pix_dblbneg_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    new_Y = a + (b << 1) - 256;
    if (new_Y < 16)
	new_Y = 16;
    else if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_dblbneg_C(uint8_t y1, uint8_t y2)
{
    uint8_t new_C;
    new_C = y1 + (y2 << 1) - 256;
    if (new_C < 16)
	new_C = 16;
    else if (new_C > 240)
	new_C = 240;
    return new_C;
}

uint8_t bl_pix_muldiv_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;

    a = y1;
    b = 256 - y2;
    if (a < 16)
	a = 16;
    if (b < 16)
	b = y1;
    if (b < 16)
	b = 16;
    new_Y = (a * a) / b;
    if (new_Y < 16)
	new_Y = 16;
    else if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_add_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (b < 16)
	b = 16;
    else if (b > 235)
	b = 235;
    if (a < 16)
	a = 16;
    else if (a > 235)
	a = 235;
    if ((256 - b) <= 0) {
	new_Y = (a * a) / 256;
    } else {
	new_Y = (a * a) / (256 - b);
    }
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_relneg_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = 256 - y2;
    if (a < 16)
	a = 16;
    if (b < 16)
	b = y1;
    if (b < 16)
	b = 16;
    new_Y = (a * a) / b;
    if (new_Y < 16)
	new_Y = 16;
    else if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_relneg_C(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_C;
    a = y1;
    b = 256 - y2;
    if (b < 16)
	b = y2;
    if (b < 16)
	b = 16;
    if (a < 16)
	a = 16;
    new_C = (a >> 1) + (b >> 1);
    if (new_C < 16)
	new_C = 16;
    else if (new_C > 240)
	new_C = 240;
    return new_C;
}

uint8_t bl_pix_selfreeze_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;

    a = y1;
    b = y2;
    if (a > b) {
	if (a < 16) {
	    new_Y = 16;
	} else {
	    new_Y = 256 - ((256 - b) * (256 - b)) / a;
	    if (new_Y < 16)
		new_Y = 16;
	}
	return new_Y;
    }
    return 0;
}

uint8_t bl_pix_selunfreeze_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;

    a = y1;
    b = y2;
    if (a > b) {
	if (b < 16) {
	    new_Y = 16;
	} else {
	    new_Y = 256 - ((256 - a) * (256 - a)) / b;
	    if (new_Y < 16)
		new_Y = 16;
	}
	return new_Y;
    }
    return 0;
}

uint8_t bl_pix_seldiff_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b;
    a = y1;
    b = y2;
    if (a > b) {
	return (uint8_t) abs(y1 - y2);
    }
    return y1;
}


uint8_t bl_pix_seldiffneg_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;

    a = y1;
    b = y2;
    if (a > b) {
	new_Y = 256 - abs(256 - a - b);
	if (new_Y > 235)
	    new_Y = 235;
	if (new_Y < 16)
	    new_Y = 16;
	return new_Y;
    }
    return 0;
}

uint8_t bl_pix_swap_Y(uint8_t y1, uint8_t y2)
{
    uint8_t new_Y;
    new_Y = y2;
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_swap_C(uint8_t y1, uint8_t y2)
{
    if (y2 < 16)
	y2 = 16;
    if (y2 > 240)
	y2 = 240;
    return y2;
}

uint8_t bl_pix_noswap_C(uint8_t y1, uint8_t y2)
{
    if (y1 < 16)
	y1 = 16;
    if (y2 > 240)
	y1 = 240;
    return y1;
}

uint8_t bl_pix_noswap_Y(uint8_t y1, uint8_t y2)
{
    uint8_t new_Y;
    new_Y = y1;
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_add_distorted_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    new_Y = a + b;
    if (new_Y > 235)
	new_Y = 235;
    if (new_Y < 16)
	new_Y = 16;
    return new_Y;
}

uint8_t bl_pix_add_distorted_C(uint8_t y1, uint8_t y2)
{
    uint8_t new_C;
    new_C = y1 + y2;
    if (new_C > 240)
	new_C = 240;
    if (new_C < 16)
	new_C = 16;
    return new_C;
}

uint8_t bl_pix_sub_distorted_Y(uint8_t y1, uint8_t y2)
{
    uint8_t new_Y, a, b;
    a = y1;
    b = y2;
    new_Y = y1 - y2;
    new_Y -= y2;
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_sub_distorted_C(uint8_t y1, uint8_t y2)
{
    uint8_t new_C;
    new_C = y1 - y2;
    new_C -= y2;
    if (new_C < 16)
	new_C = 16;
    if (new_C > 240)
	new_C = 240;
    return new_C;
}


uint8_t bl_pix_test3_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (b < 16)
	b = 16;
    if (a < 16)
	a = 16;
    new_Y = (a >> 1) + (b >> 1);
    if (new_Y < 16)
	new_Y = 16;
    if (new_Y > 235)
	new_Y = 235;
    return new_Y;
}

uint8_t bl_pix_test3_C(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_C;
    a = y1;
    b = 256 - y2;
    if (b < 16)
	b = y2;
    if (a < 16)
	a = 16;
    new_C = (a >> 1) + (b >> 1);
    if (new_C < 16)
	new_C = 16;
    if (new_C > 240)
	new_C = 240;
    return new_C;
}

void _4byte_copy( uint8_t *src, uint8_t *dst, int width,int height, int x_start, int y_start)
{
	unsigned int x = 0, y = 0;
	int *in,*out;

	width = width >> 2;
	
	for(y = y_start; y < height; y++ )
	{
		out = (int*) &dst[y*width];
		in = (int*) &src[y*width];
		for(x=x_start;x < width; x++)
		{
			out[x] =(((in[x] >> 24) & 0xff)) + (((in[x] >> 16) & 0xff) << 8)  + (((in[x] >> 8) & 0xff) << 16) + (((in[x]) & 0xff) << 24);
		}
	}

}

int bl_pix_get_color_y(int color_num)
{
    switch (color_num) {
    case VJ_EFFECT_COLOR_RED:
	return VJ_EFFECT_LUM_RED;
    case VJ_EFFECT_COLOR_BLUE:
	return VJ_EFFECT_LUM_BLUE;
    case VJ_EFFECT_COLOR_GREEN:
	return VJ_EFFECT_LUM_GREEN;
    case VJ_EFFECT_COLOR_CYAN:
	return VJ_EFFECT_LUM_CYAN;
    case VJ_EFFECT_COLOR_MAGNETA:
	return VJ_EFFECT_LUM_MAGNETA;
    case VJ_EFFECT_COLOR_YELLOW:
	return VJ_EFFECT_LUM_YELLOW;
    case VJ_EFFECT_COLOR_BLACK:
	return VJ_EFFECT_LUM_BLACK;
    case VJ_EFFECT_COLOR_WHITE:
	return VJ_EFFECT_LUM_WHITE;
    }
    return VJ_EFFECT_LUM_BLACK;
}
int bl_pix_get_color_cb(int color_num)
{
    switch (color_num) {
    case VJ_EFFECT_COLOR_RED:
	return VJ_EFFECT_CB_RED;
    case VJ_EFFECT_COLOR_BLUE:
	return VJ_EFFECT_CB_BLUE;
    case VJ_EFFECT_COLOR_GREEN:
	return VJ_EFFECT_CB_GREEN;
    case VJ_EFFECT_COLOR_CYAN:
	return VJ_EFFECT_CB_CYAN;
    case VJ_EFFECT_COLOR_MAGNETA:
	return VJ_EFFECT_CB_MAGNETA;
    case VJ_EFFECT_COLOR_YELLOW:
	return VJ_EFFECT_CB_YELLOW;
    case VJ_EFFECT_COLOR_BLACK:
	return VJ_EFFECT_CB_BLACK;
    case VJ_EFFECT_COLOR_WHITE:
	return VJ_EFFECT_CB_WHITE;
    }
    return VJ_EFFECT_CB_BLACK;
}

int bl_pix_get_color_cr(int color_num)
{
    switch (color_num) {
    case VJ_EFFECT_COLOR_RED:
	return VJ_EFFECT_CR_RED;
    case VJ_EFFECT_COLOR_BLUE:
	return VJ_EFFECT_CR_BLUE;
    case VJ_EFFECT_COLOR_GREEN:
	return VJ_EFFECT_CR_GREEN;
    case VJ_EFFECT_COLOR_CYAN:
	return VJ_EFFECT_CR_CYAN;
    case VJ_EFFECT_COLOR_MAGNETA:
	return VJ_EFFECT_CR_MAGNETA;
    case VJ_EFFECT_COLOR_YELLOW:
	return VJ_EFFECT_CR_YELLOW;
    case VJ_EFFECT_COLOR_BLACK:
	return VJ_EFFECT_CR_BLACK;
    case VJ_EFFECT_COLOR_WHITE:
	return VJ_EFFECT_CR_WHITE;
    }
    return VJ_EFFECT_CR_BLACK;
}


int calculate_luma_value(uint8_t *Y, int w , int h) {
	unsigned int len = (w * h);
	unsigned int sum = 0;
	unsigned int i = len;
	while( i ) {
		sum += *(Y++);	
		i--;
	}
	return (sum/len);
}

int calculate_cbcr_value(uint8_t *Cb,uint8_t *Cr, int w, int h) {
	unsigned int len = (w * h) >> 1;
	unsigned int sum = 0;
	unsigned int i = len;
	while( i ) {
		sum += 	( Cb[i] + Cr[i] ) >> 1;
		i--;
	}
	return (sum/len);

}

#ifdef HAVE_ASM_MMX

#define MMX_load8byte_mm7(data)__asm__("\n\t movq %0,%%mm7\n" : "=m" (data):)

#endif 

void	memset_ycbcr(	uint8_t *in, 
			uint8_t *out, 
			uint8_t val, 
			unsigned int width, 
			unsigned int height)
{

#ifdef HAVE_ASM_MMX
	double mmtemp_d;
	uint8_t *mmtemp = (uint8_t*) &mmtemp_d;
	int i;
	unsigned int len = width * height;

	for(i = 0; i < 8; i++) mmtemp[i] = val;
	MMX_load8byte_mm7( mmtemp_d );

	for(i = 0; i < len; i+=width)
	{
		double *ipt = (double*) &in[i];
		double *opt = (double*) &out[i];
		int index = 0;
		int k = width / 8;
		while( index < k )
		{
			__asm__(
			"\n\t movq %0,%%mm0	\t"
			"\n\t movq %1,%%mm1	\t"
			"\n\t movq %%mm0,%%mm2	\t"
			"\n\t movq %%mm7,%%mm2	\t"
			"\n\t movq %%mm2,%0	\t"
			: "=m" (opt[index])
			:  "m" (ipt[index])
			);
			index ++;
		}
	}
	__asm__("emms" : :  );
#else
	memset( in, val, (width*height) );
#endif
}

double	m_get_radius( int x, int y )
{
	return (sqrt( (x*x) + (y*y) ));
}
double	m_get_angle( int x, int y )
{
	return (atan2( (float)y,x));
}

double	m_get_polar_x( double r, double a)
{
	return ( r * cos(a) );	
}
double	m_get_polar_y( double r, double a)
{
	return ( r * sin(a) );
}
