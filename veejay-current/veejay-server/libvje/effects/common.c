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
#include <math.h>
#include <libvje/internal.h>
#include <libvjmem/vjmem.h>
#include "common.h"

char	**vje_build_param_list( int num, ... )
{
	va_list args;
	char **list;
	char *tmp = NULL;
	list = (char**) vj_malloc(sizeof(char*) * (num+1) );

	va_start( args, num );

	int i;
	for( i = 0; i <num; i ++ ) {
		tmp = (char*) va_arg( args,char*);
		list[i] = (tmp == NULL ? NULL : vj_strdup(tmp));
	}
	list[num] = NULL;
	va_end(args);
	return list;
}
static inline void linearBlend(unsigned char *src, int stride)
{
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


matrix_t matrix_placementA(int photoindex, int size, int w , int h)
{
	matrix_t m;
	m.w = (photoindex % size) * (w/size);
	m.h = (photoindex / size) * (h/size);
	return m;
}

matrix_t matrix_placementB(int photoindex, int size, int w , int h)
{
	matrix_t m;
	m.w = (photoindex/size) * (w/size);
	m.h = (photoindex % size) * (h/size);
	return m;
}

matrix_t matrix_placementC(int photoindex, int size, int w , int h)
{
	matrix_t m;
	int n = size*size-1;
	m.w = ((n-photoindex) % size) * (w/size);
	m.h = ((n-photoindex) / size) * (h/size);
	return m;
}

matrix_t matrix_placementD(int photoindex, int size, int w , int h)
{
	matrix_t m;
	int n = size*size-1;
	m.w = ((n-photoindex) / size) * (w/size);
	m.h = ((n-photoindex) % size) * (h/size);
	return m;
}

matrix_f	get_matrix_func(int type)
{
	if(type==0)
		return &matrix_placementA;
	if(type==1)
		return &matrix_placementB;
	if(type==2)
		return &matrix_placementC;
	return &matrix_placementD;		
}


int power_of(int size)
{
	int power = 1;
	while( size-- )
		power *= 2;

	return power;
}

int max_power(int w)
{
	int i=1;
	while(power_of(i) < w)
		i++;
	return i;
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
}


void frameborder_yuvdata(uint8_t * input_y, uint8_t * input_u,
			 uint8_t * input_v, uint8_t * putin_y,
			 uint8_t * putin_u, uint8_t * putin_v, int width,
			 int height, int top, int bottom, int left,
			 int right, int shift_h, int shift_v)
{

    int line, x;
    int input_active_width;
    int input_active_height;
    uint8_t *rightvector;
	shift_h = 0;
	shift_v = 0;
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
    for (line = 0; line < (top >> shift_v); line++) {
	for (x = 0; x < (width >> shift_h); x++) {
	    *(input_u + x) = *(putin_u + x);
	}
	//memcpy (input_u, putin_u, width >> 1);
	input_u += width >> shift_h;
	putin_u += width >> shift_h;
    }

    rightvector = input_u + ((left + input_active_width) >> shift_h);
    for (line = 0; line < (input_active_height >> shift_v); line++) {
	//memcpy (input_u, putin_u, left >> 1);
	for (x = 0; x < (left >> shift_h); x++) {
	    *(input_u + x) = *(putin_u + x);
	}
	//memcpy (rightvector, putin_u + ((left + input_active_width)>>1) , right >> 1);
	for (x = 0; x < (right >> shift_h); x++) {
	    *(rightvector + x) = *(putin_u +
				   ((left + input_active_width + x) >> shift_h));

	}
	input_u += width >> shift_h;
	rightvector += width >> shift_h;
	putin_u += width >> shift_h;
    }

    for (line = 0; line < (bottom >> shift_v); line++) {
	for (x = 0; x < (width >> shift_h); x++)
	    *(input_u + x) = *(putin_u + x);
	//memcpy (input_u, putin_u, width >> 1);
	input_u += width >> shift_h;
	putin_u += width >> shift_h;
    }

    /* V component Top */
    for (line = 0; line < (top >> shift_v); line++) {
	//memcpy (input_v, putin_v, width >> 1);
	for (x = 0; x < (width >> shift_h); x++) {
	    *(input_v + x) = *(putin_v + x);
	}
	input_v += width >> shift_h;
	putin_v += width >> shift_h;
    }
    /* Left and Right */
    rightvector = input_v + ((left + input_active_width) >> shift_h);
    for (line = 0; line < (input_active_height >> shift_v); line++) {
	for (x = 0; x < (left >> shift_h); x++)
	    *(input_v + x) = *(putin_v + x);

	//memcpy (input_v, putin_v, left >> 1);
	//memcpy (rightvector, putin_v + ((left+input_active_width)>>1), right >> 1);
	for (x = 0; x < (right >> shift_h); x++)
	    *(rightvector + x) =
		*(putin_v + ((left + input_active_width + x) >> shift_h));

	input_v += width >> shift_h;
	rightvector += width >> shift_h;
	putin_v += width >> shift_h;
    }
    /* Bottom */
    for (line = 0; line < (bottom >> shift_v); line++) {
	//memcpy (input_v, putin_v, width >> 1);
	for (x = 0; x < (width >> shift_h); x++)
	    *(input_v + x) = *(putin_v + x);
	input_v += width >> shift_h;
	putin_v += width >> shift_h;
    }


}



/**********************************************************************************************
 *
 * frameborder_yuvdata, based upon blackborder_yuvdata.
 * instead of making the borders black, fill it with pixels coming from the second YUV420 frame.
 **********************************************************************************************/
void ffframeborder_yuvdata(uint8_t * input_y, uint8_t * input_u,
			 uint8_t * input_v, uint8_t * putin_y,
			 uint8_t * putin_u, uint8_t * putin_v, int width,
			 int height, int top, int bottom, int left,
			 int right, int wshift, int hshift)
{

    int line, x;
    int input_active_width = (height - top - bottom);
    int input_active_height = (width - left -right);
    uint8_t *rightvector;
    int uv_top = top >> hshift;
    int uv_bottom = bottom >> hshift;
    int uv_left = left >> wshift;
    int uv_right = right >> wshift;
    int uv_width = width >> wshift;
    int uv_height = height >> hshift;
    int uv_input_active_height = uv_height - uv_top - uv_bottom;
    int uv_input_active_width = uv_width - uv_left - uv_right;   

    /* Y component TOP */

    if(top)
    {
    	for (line = 0; line < top; line++) {
		for (x = 0; x < width; x++) {
		    *(input_y + x) = *(putin_y + x);
		}
		//memcpy (input_y, putin_y, width);
		input_y += width;
		putin_y += width;
   	 }
    }

    if(left && right)
    {
	    rightvector = input_y + left + input_active_width;
	    /* 	Y component LEFT AND RIGHT */
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
    }

 
    /* Y component BOTTOM  */
    if(bottom)
    {
  	  for (line = 0; line < bottom; line++) {
		for (x = 0; x < width; x++)
		    *(input_y + x) = *(putin_y + x);

		//memcpy (input_y, putin_y, width);
		input_y += width;
		putin_y += width;
    	}
    }


	/* U V components */
	/* U component TOP */

	if(uv_top)
   	{
    		for (line = 0; line < uv_top; line++)
		{
			for (x = 0; x < uv_width; x++)
			{
			    *(input_u + x) = *(putin_u + x);
			}
			input_u += uv_width;
			putin_u += uv_width;
    		}
    	}


	if(left && right)
	{
    		rightvector = input_u + uv_left + uv_input_active_width;
		for (line = 0; line < uv_input_active_height; line++)
		{
			//memcpy (input_u, putin_u, left >> 1);
			for (x = 0; x < uv_left; x++)
			{
			    *(input_u + x) = *(putin_u + x);
			}
			//memcpy (rightvector, putin_u + ((left + input_active_width)>>1) , right >> 1);
			for (x = 0; x < uv_right;  x++)
			{
		    		*(rightvector + x) = *(putin_u +
					   uv_left + uv_input_active_width + x );
	
			}
			input_u += uv_width; 
			rightvector += uv_width;
			putin_u += uv_width;
    		}
 	}

	if(uv_bottom)
    	{
		for (line = 0; line < uv_bottom; line++)
		{
			for (x = 0; x < uv_width; x++)
			    *(input_u + x) = *(putin_u + x);
			input_u += uv_width;
			putin_u += uv_width;
    		}
     	}

	/* V component Top */
    	if(uv_top) 
	{
	    for (line = 0; line < uv_top; line++)
	    {
		for (x = 0; x < uv_width; x++) 
		    *(input_v + x) = *(putin_v + x);
		input_v += uv_width;
		putin_v += uv_width;
   	    }
	}

	if(uv_left && uv_right)
	{
    /* Left and Right */
		rightvector = input_v + uv_left + uv_input_active_width;
   		for (line = 0; line < uv_input_active_height; line++)
		{
			for (x = 0; x < uv_left; x++)
	  			*(input_v + x) = *(putin_v + x);

			for (x = 0; x < uv_right;  x++)
	   			 *(rightvector + x) =
					*(putin_v + uv_left + uv_input_active_width + x);

			input_v += uv_width;
			rightvector += uv_width;
			putin_v += uv_width;
	
	    	}
	}

	if(uv_bottom)
	{
	/* Bottom */
		for (line = 0; line < uv_bottom; line++) {
			for (x = 0; x < uv_width; x++)
			    *(input_v + x) = *(putin_v + x);
			input_v += uv_width;
			putin_v += uv_width;
    		}
	}
}



void blackborder_yuvdata(uint8_t * input_y, uint8_t * input_u,
			 uint8_t * input_v, int width, int height, int top,
			 int bottom, int left, int right, int wshift, int hshift, int color)
{  
    int line, x;
    int input_active_width;
    int input_active_height;
    uint8_t *rightvector;
    uint8_t colorY = bl_pix_get_color_y(color);
    uint8_t colorCb= bl_pix_get_color_cb(color);
    uint8_t colorCr= bl_pix_get_color_cr(color);
        input_active_height = height - top - bottom;
    input_active_width = width - left - right;

    /* Y component TOP */
    for (line = 0; line < top; line++) {
	for (x = 0; x < width; x++) {
	    *(input_y + x) = colorY;
	}
	//memcpy (input_y, putin_y, width);
	input_y += width;
    }
    rightvector = input_y + left + input_active_width;
    /* Y component LEFT AND RIGHT */
    for (line = 0; line < input_active_height; line++) {
	for (x = 0; x < left; x++) {
	    *(input_y + x) = colorY;
	}
	//memcpy (input_y, putin_y, left);
	for (x = 0; x < right; x++) {
	    *(rightvector + x) = colorY;
	}
	//memcpy (rightvector, putin_y + left + input_active_width, right);
	input_y += width;
	rightvector += width;
    }
    /* Y component BOTTOM  */
    for (line = 0; line < bottom; line++) {
	for (x = 0; x < width; x++)
	    *(input_y + x) = colorY;

	//memcpy (input_y, putin_y, width);
	input_y += width;
    }


    /* U component TOP */
    for (line = 0; line < (top >> hshift); line++) {
	for (x = 0; x < (width >> wshift); x++) {
	    *(input_u + x) = colorCb;
	}
	//memcpy (input_u, putin_u, width >> 1);
	input_u += width >> wshift;
    }

    rightvector = input_u + ((left + input_active_width) >> wshift);
    for (line = 0; line < (input_active_height >> hshift); line++) {
	//memcpy (input_u, putin_u, left >> 1);
	for (x = 0; x < (left >> wshift); x++) {
	    *(input_u + x) = colorCb;
	}
	//memcpy (rightvector, putin_u + ((left + input_active_width)>>1) , right >> 1);
	for (x = 0; x < (right >> wshift); x++) {
	    *(rightvector + x) = colorCb;
	}
	input_u += width >> wshift;
	rightvector += width >> wshift;
    }

    for (line = 0; line < (bottom >> hshift); line++) {
	for (x = 0; x < (width >> wshift); x++)
	    *(input_u + x) = colorCb;
	//memcpy (input_u, putin_u, width >> 1);
	input_u += width >> wshift;
    }

    /* V component Top */
    for (line = 0; line < (top >> hshift); line++) {
	//memcpy (input_v, putin_v, width >> 1);
	for (x = 0; x < (width >> wshift); x++) {
	    *(input_v + x) = colorCr;
	}
	input_v += width >> wshift;
    }
    /* Left and Right */
    rightvector = input_v + ((left + input_active_width) >> wshift);
    for (line = 0; line < (input_active_height >> hshift); line++) {
	for (x = 0; x < (left >> wshift); x++)
	    *(input_v + x) = colorCr;

	//memcpy (input_v, putin_v, left >> 1);
	//memcpy (rightvector, putin_v + ((left+input_active_width)>>1), right >> 1);
	for (x = 0; x < (right >> wshift); x++)
	    *(rightvector + x) =colorCr;

	input_v += width >> wshift;
	rightvector += width >> wshift;
    }
    /* Bottom */
    for (line = 0; line < (bottom >> hshift); line++) {
	//memcpy (input_v, putin_v, width >> 1);
	for (x = 0; x < (width >> wshift); x++)
	    *(input_v + x) = colorCr;
	input_v += width >> wshift;
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
	return &bl_pix_swap_Y;
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
    return &bl_pix_noswap_Y;
}

/* function to blend chrominance pixel */
pix_func_C get_pix_func_C(const int pix_type)
{
    if (pix_type == 0)
	return &bl_pix_swap_C;
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
    return &bl_pix_noswap_C;
}


/* point arithemetic , these are blending functions. */

/* multiply pixel a with pixel b */
uint8_t bl_pix_multiply_Y(uint8_t y1, uint8_t y2)
{
	return ( (y1 * y2) >> 8);
}
/* divide pixel a with pixel b */
uint8_t bl_pix_divide_Y(uint8_t y1, uint8_t y2)
{
	int c = y1 * y2;
	int b = 0xff - y2;
	if( b <= pixel_Y_lo_ || c <= pixel_Y_lo_ )
		return pixel_Y_lo_;
	return ( c / b );
}

uint8_t bl_pix_additive_Y(uint8_t y1, uint8_t y2)
{
	return (y1 + ((2 * y2) - 0xff) );
}


uint8_t bl_pix_substract_Y(uint8_t y1, uint8_t y2)
{
	return ( y1 + (y2 - 0xff ) );
}

uint8_t bl_pix_softburn_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (a + b < 0xff) {
	if (a > pixel_Y_hi_) {
	    new_Y = pixel_Y_hi_;
	} else {
	    new_Y = (b >> 7) / (256 - a);
	    if (new_Y > pixel_Y_hi_)
		new_Y = pixel_Y_hi_;
	}
    } else {
	new_Y = 0xff - (((0xff - a) >> 7) / b);
    }
    return new_Y;
}

uint8_t bl_pix_inverseburn_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (a < 16) {
	new_Y = pixel_Y_lo_;
    } else {
	new_Y = 0xff - (((0xff - b) >> 8) / a);
    }
    return new_Y;
}


uint8_t bl_pix_colordodge_Y(uint8_t y1, uint8_t y2)
{
    if(y1 > pixel_Y_hi_ )
	    y1 = pixel_Y_hi_;
    return ((y2 >> 8) / (256 - y1));
}

uint8_t bl_pix_mulsub_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b;
    a = y1;
    b = (0xff - y2);
    if( a < 16 )
        a = 16;
    if (b < 16)
	b = 16;
    return ( a / b );
}

uint8_t bl_pix_lighten_Y(uint8_t y1, uint8_t y2)
{
    if (y1 > y2) 
	return y1;
    return y2;
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
    uint8_t a, b;
    a = 0xff - y1;
    b = y2;
    return ( 0xff - abs(a - b) );
}

uint8_t bl_pix_exclusive_Y(uint8_t y1, uint8_t y2)
{
    return ( y1 + y2 - ((y1 * y2) >> 8) );
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
    new_Y = c + a * ((0xff - (((0xff - a) * (0xff - b)) >> 7) - c) >> 7);
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
	new_Y = 0xff - ((0xff - a) * (0xff - a)) / b;
    }
    return new_Y;
}

uint8_t bl_pix_unfreeze_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (a < 16) {
	new_Y = pixel_Y_lo_;
    } else {
	if(b > pixel_Y_hi_) b = pixel_Y_hi_;
	new_Y = 0xff - ((0xff - b) * (0xff - b)) / a;
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
	new_Y = 0xff - ((0xff - b) * (0xff - a) >> 7);
    }
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
    return new_Y;
}

uint8_t bl_pix_relativeadd_C(uint8_t y1, uint8_t y2)
{
    uint8_t new_C;
    new_C = (y1 - y2 + 0xff) >> 1;
    return new_C;
}

uint8_t bl_pix_relativesub_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    new_Y = (a - b + 0xff) >> 1;
    return new_Y;
}

uint8_t bl_pix_maxsubsel_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (b > a) {
	new_Y = (b - a + 0xff) >> 1;
    } else {
	new_Y = (a - b + 0xff) >> 1;
    }
    return new_Y;
}

uint8_t bl_pix_minsubsel_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (b < a) {
	new_Y = (b - a + 0xff) >> 1;
    } else {
	new_Y = (a - b + 0xff) >> 1;
    }
    return new_Y;
}

uint8_t bl_pix_addsubsel_Y(uint8_t y1, uint8_t y2)
{
	return ( (y1 + y2) >> 1 );
}

uint8_t bl_pix_maxsel_Y(uint8_t y1, uint8_t y2)
{
	return ( (y2>y1 ? y2 : y1 ) );
}

uint8_t bl_pix_minsel_Y(uint8_t y1, uint8_t y2)
{
	return ( (y2 < y1  ? y2: y1 ));
}


uint8_t bl_pix_dblbneg_Y(uint8_t y1, uint8_t y2)
{
	return ( (y1 + (y2 << 1 ) - 0xff ) );
}

uint8_t bl_pix_dblbneg_C(uint8_t y1, uint8_t y2)
{
	return ( (y1 + (y2 << 1 ) - 0xff ));
}

uint8_t bl_pix_muldiv_Y(uint8_t y1, uint8_t y2)
{
	if( y2 > pixel_Y_hi_ ) y2 = pixel_Y_hi_;
	if( y1 < pixel_Y_lo_ ) y1 = pixel_Y_lo_;
	return ( (y1*y1) / (0xff - y2 ) );
}

uint8_t bl_pix_add_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = y2;
    if (b < 16)
	b = 16;
    if (a < 16)
	a = 16;
    if ((0xff - b) <= 0) {
	new_Y = (a * a) >> 8;
    } else {
	if( b > pixel_Y_hi_)
		b= pixel_Y_hi_;
	new_Y = (a * a) / (0xff - b);
    }
    return new_Y;
}

uint8_t bl_pix_relneg_Y(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_Y;
    a = y1;
    b = 0xff - y2;
    if (a < 16)
	a = 16;
    if (b < 16)
	b = y1;
    if (b < 16)
	b = 16;
    new_Y = (a * a) / b;
    return new_Y;
}

uint8_t bl_pix_relneg_C(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_C;
    a = y1;
    b = 0xff - y2;
    if (b < 16)
	b = y2;
    if (b < 16)
	b = 16;
    if (a < 16)
	a = 16;
    new_C = (a >> 1) + (b >> 1);
    if (new_C < 16)
	new_C = 16;
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
	    new_Y = 0xff - ((0xff - b) * (0xff - b)) / a;
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
	    new_Y = pixel_Y_lo_;
	} else {
	    if( a > pixel_Y_hi_ ) a = pixel_Y_hi_;
	    new_Y = 0xff - ((0xff - a) * (0xff - a)) / b;
	    if (new_Y < pixel_Y_lo_)
		new_Y = pixel_Y_lo_;
	}
	return new_Y;
    }
    return pixel_Y_lo_;
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
	new_Y = 0xff - abs(0xff - a - b);
	return new_Y;
    }
    return 0;
}

uint8_t bl_pix_swap_Y(uint8_t y1, uint8_t y2)
{
    return y2;
}

uint8_t bl_pix_swap_C(uint8_t y1, uint8_t y2)
{
    return y2;
}

uint8_t bl_pix_noswap_C(uint8_t y1, uint8_t y2)
{
    return y1;
}

uint8_t bl_pix_noswap_Y(uint8_t y1, uint8_t y2)
{
    return y1;
}

uint8_t bl_pix_add_distorted_Y(uint8_t y1, uint8_t y2)
{
	return ( y1 + y2 );
}

uint8_t bl_pix_add_distorted_C(uint8_t y1, uint8_t y2)
{
	return ( y1 + y2 );
}

uint8_t bl_pix_sub_distorted_Y(uint8_t y1, uint8_t y2)
{
    uint8_t new_Y;
    new_Y = y1 - y2;
    new_Y -= y2;
    return new_Y;
}

uint8_t bl_pix_sub_distorted_C(uint8_t y1, uint8_t y2)
{
    uint8_t new_C;
    new_C = y1 - y2;
    new_C -= y2;
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
    return new_Y;
}

uint8_t bl_pix_test3_C(uint8_t y1, uint8_t y2)
{
    uint8_t a, b, new_C;
    a = y1;
    b = 0xff - y2;
    if (b < 16)
	b = y2;
    if (a < 16)
	a = 16;
    new_C = (a >> 1) + (b >> 1);
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


uint8_t _pf_dneg(uint8_t a, uint8_t b)
{
	uint8_t t =  
		255 - ( abs ( (255 - abs((255-a)-a))  -    (255-abs((255-b)-b))) );
	return ( abs( abs(t-b) - b ));
}

uint8_t _pf_lghtn(uint8_t a, uint8_t b)
{
	if( a > b ) return a;
	return b;
}

uint8_t _pf_dneg2(uint8_t a,uint8_t b)
{
	return ( 255 - abs ( (255-a)- b )  );
}

uint8_t _pf_min(uint8_t a, uint8_t b)
{
	uint8_t p = ( (b < a) ? b : a);
	return ( 255 - abs( (255-p) - b ) );
}

uint8_t _pf_max(uint8_t a,uint8_t b)
{
	uint8_t p = ( (b > a) ? b : a);
	if( p<=16) p = 16;
	return ( 255 - ((255 - b) * (255 - b)) / p);
		
}

uint8_t _pf_pq(uint8_t a,uint8_t b)
{
	if( a <= 16) a = 16;
	if( b <= 16) b = 16;
	int p = 255 - ((255-a) * (255-a)) / a;
	int q = 255 - ((255-b) * (255-b)) / b;
	
	return ( 255 - ((255-p) * (255 - a)) / q);
}

uint8_t _pf_none(uint8_t a, uint8_t b)
{
	return a;
}

_pf	_get_pf(int type)
{
	
	switch(type)
	{
	 
	 case 0: return &_pf_dneg;
	 case 3: return &_pf_lghtn;
	 case 1: return &_pf_min;
	 case 2: return &_pf_max;
	 case 5: return &_pf_pq;
	 case 6: return &_pf_dneg2;

	}
	return &_pf_none;
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
void     vje_load_mask(uint8_t val)
{
        uint8_t mask[8] = { val,val,val,val,  val,val,val,val };
        uint8_t *m    = (uint8_t*)&mask;
        
        __asm __volatile(
                "movq   (%0),   %%mm4\n\t"
                :: "r" (m) );
}

void vje_diff_plane( uint8_t *A, uint8_t *B, uint8_t *O, int val, int len )
{
	unsigned int i;
  	uint8_t mask[8] = { val,val,val,val,  val,val,val,val };
        uint8_t *m    = (uint8_t*)&mask;
        
        __asm __volatile(
                "movq   (%0),   %%mm7\n\t"
                :: "r" (m) );


	for( i = len; i > 8; i -= 8 ) {
		__asm __volatile(
			"\n\t movq %[srcA], %%mm0"
			"\n\t movq %[srcB], %%mm1"
			"\n\t psubusb %%mm1,%%mm0" 
			"\n\t psubusb %%mm2,%%mm1" 
			"\n\t psubusb %%mm1,%%mm0" 
			"\n\t psubusb %%mm7,%%mm0" 
			"\n\t movq %%mm0, %[dest]"
			: [dest] "=m" (*(O + i))
			: [srcA] "m" (*(A + i ))
			, [srcB] "m" (*(B + i )));
	}
	
	do_emms;	
}

void     vje_mmx_negate( uint8_t *dst, uint8_t *in )
{
        __asm __volatile(
                "movq   (%0),   %%mm0\n\t"
                "movq   %%mm4,  %%mm1\n\t"
                "psubb  %%mm0,  %%mm1\n\t"
                "movq   %%mm1,  (%1)\n\t"
                :: "r" (in) , "r" (dst)
        );
}

void 	vje_mmx_negate_frame(uint8_t *dst, uint8_t *in, uint8_t val, int len )
{
	unsigned int i;

	vje_load_mask( val );

	for( i = len; i > 8; i -= 8 ) {
		vje_mmx_negate( dst + i, in + i );
	}


}

void	binarify_1src( uint8_t *dst, uint8_t *src, uint8_t v, int reverse,int w, int h )
{
	int len = (w * h)>>3;
	int i;
	uint8_t *s = src;
	uint8_t *d = dst;

	uint8_t mm[8] = { v,v,v,v, v,v,v,v };
	uint8_t *m = (uint8_t*) &(mm[0]);
	__asm __volatile(
		"movq	(%0),	%%mm7\n\t"
		:: "r" (m) );

	uint8_t *p = dst;

	for( i = 0; i < len ; i ++ )
	{
		__asm __volatile(
			"movq (%0),%%mm0\n\t"
			"pcmpgtb %%mm7,%%mm0\n\t"
			"movq %%mm0,(%1)\n\t"
			:: "r" (s), "r" (d)
		);
		s += 8;
		d += 8;
	}

	if( reverse )
	{
		__asm __volatile(
			"pxor	%%mm4,%%mm4" ::
			 );
		for( i = 0; i < len ; i ++ )
		{
			__asm __volatile(
			     "movq	(%0), %%mm0\n\t"
	      		     "pcmpeqb  %%mm4,  %%mm0\n\t"
        		     "movq   %%mm0,  (%1)\n\t"
			:: "r" (p), "r" (p) 
			);
			p += 8;
		}
	}

	do_emms;
}


#else
void vj_diff_plane( uint8_t *A, uint8_t *B, uint8_t *O, int threshold, int len )
{	
	unsigned int i;
	for( i = 0; i < len; i ++ ) {
		O[i] = ( abs( A[i] - B[i] ) > threshold ? 0xff : 0 );
	}	
}

void 	vj_mmx_negate_frame(uint8_t *dst, uint8_t *in, uint8_t val, int len )
{
	unsigned int i;
	for( i = 0; i < len; i++ ) {
		dst[i] = val - in[i];
	}	
}
void	binarify_1src( uint8_t *dst, uint8_t *src, uint8_t threshold,int reverse, int w, int h )
{
	const int len = w*h;
	int i;
	if(!reverse)
	for( i = 0; i < len; i ++ )
	{
		dst[i] = (  src[i] <= threshold ? 0: 0xff );
	}
	else
		for( i = 0; i < len; i ++ )
			dst[i] = (src[i] > threshold ? 0: 0xff );
}


#endif

void	binarify( uint8_t *bm, uint8_t *bg, uint8_t *src,int threshold,int reverse, const int len )
{
	int i;
	if(!reverse)
	{
		vje_diff_plane( bg, src, bm, threshold,len );
		vje_mmx_negate_frame( bm,bm, 0xff, len );
	}
	else
	{
		vje_diff_plane( bg, src, bm, threshold, len );
	}
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

//copied from xine
void blur(uint8_t *dst, uint8_t *src, int w, int radius, int dstStep, int srcStep){
	int x;
	const int length= radius*2 + 1;
	const int inv= ((1<<16) + length/2)/length;

	int sum= 0;

	for(x=0; x<radius; x++){
		sum+= src[x*srcStep]<<1;
	}
	sum+= src[radius*srcStep];

	for(x=0; x<radius; x++){
		sum+= src[(radius+x)*srcStep] - src[(radius-x)*srcStep];
		dst[x*dstStep]= (sum*inv + (1<<15))>>16;
	}

	for(; x<w-radius; x++){
		sum+= src[(radius+x)*srcStep] - src[(x-radius-1)*srcStep];
		dst[x*dstStep]= (sum*inv + (1<<15))>>16;
	}

	for(; x<w; x++){
		sum+= src[(2*w-radius-x-1)*srcStep] - src[(x-radius-1)*srcStep];
		dst[x*dstStep]= (sum*inv + (1<<15))>>16;
	}
}

//copied from xine
void blur2(uint8_t *dst, uint8_t *src, int w, int radius, int power, int dstStep, int srcStep){
	uint8_t temp[2][4096];
	uint8_t *a= temp[0], *b=temp[1];
	
	if(radius){
		blur(a, src, w, radius, 1, srcStep);
		for(; power>2; power--){
			uint8_t *c;
			blur(b, a, w, radius, 1, 1);
			c=a; a=b; b=c;
		}
		if(power>1)
			blur(dst, a, w, radius, dstStep, 1);
		else{
			int i;
			for(i=0; i<w; i++)
				dst[i*dstStep]= a[i];
		}
	}else{
		int i;
		for(i=0; i<w; i++)
			dst[i*dstStep]= src[i*srcStep];
	}
}


typedef struct 
{
	uint32_t hY[256];
	uint32_t hR[256];
	uint32_t hG[256];
	uint32_t hB[256];
} histogram_t;

void	*veejay_histogram_new()
{
	histogram_t *h = (histogram_t*) vj_calloc(sizeof(histogram_t));
	if(!h) return NULL;
	return h;
}

void	veejay_histogram_del(void *his)
{
	histogram_t *h = (histogram_t*) his;
	if( h ) free(h);
}

int		i_cmp( const void *a, const void *b )
{
	return ( *(uint32_t*) a - *(uint32_t*) b );
}

static	uint32_t	veejay_histogram_median( uint32_t *h )
{
	uint32_t list[256];
	veejay_memcpy( h, list, sizeof(list));
	qsort( list, 256, sizeof(uint32_t), i_cmp);
	return ( list[128] );
}

static	void	build_histogram_rgb( uint8_t *rgb, histogram_t *h, VJFrame *f)
{
	unsigned int i,j;
        uint32_t *Hr,*Hb,*Hg;

        Hr = h->hR;
	Hg = h->hG;
	Hb = h->hB;

	int W = f->width;
	int H = f->height;
	int r = W * 3;

	veejay_memset( Hr, 0,sizeof(uint32_t) * 256 );
	veejay_memset( Hg, 0,sizeof(uint32_t) * 256 );
	veejay_memset( Hb, 0,sizeof(uint32_t) * 256 );

	for( i = 0; i < H; i ++ )
	{
		for( j = 0; j < r; j += 3 )
		{
			Hr[ (rgb[i*r+j] ) ] ++;
			Hg[ (rgb[i*r+j+1]) ] ++;
			Hb[ (rgb[i*r+j+2]) ] ++;
		}
	}
}

static	void	build_histogram( histogram_t *h, VJFrame *f )
{
	unsigned int i, len;
	uint32_t *H;
	uint8_t  *p;

	// intensity histogram
	H = h->hY;
	p = f->data[0];
	len = f->len;
	veejay_memset( H, 0, sizeof(uint32_t) * 256 );
	for( i = 0; i < len; i ++ )
		H[ p[i] ] ++;

}

static	void	veejay_lut_calc( uint32_t *h, uint32_t *lut, int intensity, int strength, int len )
{
	unsigned int i;
	unsigned int op0 = 255 - strength;
	unsigned int op1 = strength;
	lut[0] = h[0];
	for( i = 1; i < 256; i ++ )
		lut[i] = lut[i-1] + h[i];
	for( i = 0; i < 256; i ++ )
		lut[i] = (lut[i] * intensity ) / len;
	for( i = 0; i < 256; i ++ )
		lut[i] = (op1 * lut[i] + op0 * i ) >> 8;
}

static	void	veejay_blit_histogram( uint8_t *D, uint32_t *h, int len )
{
	unsigned int i;
	for( i = 0; i < 256; i ++ )
		D[i] = (h[i] > 0 ? (len / h[i]) : 0 );
}


static	inline	void	veejay_histogram_qdraw( uint32_t *histi, histogram_t *h, VJFrame *f, uint8_t *plane, int left, int down)
{
	uint8_t lut[256];
	unsigned int i,j,len;

	len = f->len;
	veejay_blit_histogram( lut, histi, len );
	
	int his_height = f->height/5;
	int his_width = f->width/5;

	float sx = his_width/256.0f;
	float sy = his_height/256.0f;

//@ slow!
	for ( i = 0; i < 256; i ++ )
	{
		for( j = 0; j < 256 ; j ++ )
		{
			int dx = j * sx;
			int dy = i * sy;
		
			int pos = (f->height - dy - 1 - down) * f->width + dx + left;
			if( plane[pos] != 0xff)
				plane[pos] = (lut[j] >= i ? 0xff: 0);
		}
	}
}

void	veejay_histogram_draw( void *his, VJFrame *org, VJFrame *f, int intensity, int strength )
{
	histogram_t *h = (histogram_t*) his;

	veejay_histogram_analyze( his, org, 0 );
	veejay_histogram_qdraw( h->hY, h, f, f->data[0],0,0 );


	veejay_histogram_equalize( his, org, intensity, strength );
	veejay_histogram_analyze( his, org, 0 );
	veejay_histogram_qdraw( h->hY, h, f, f->data[0],f->width/4 + 10,0 );
}

void	veejay_histogram_draw_rgb( void *his, VJFrame *f, uint8_t *rgb, int in, int st, int mode )
{
	histogram_t *h = (histogram_t*) his;

	veejay_histogram_analyze_rgb(his,rgb,f );
	switch(mode)
	{
		case 0:
			veejay_histogram_qdraw( h->hR, h, f, f->data[0], 0,f->height/4 );
			break;
		case 1:	
			veejay_histogram_qdraw( h->hG, h, f, f->data[0], 0,f->height/4 );
			break;
		case 2:
			veejay_histogram_qdraw( h->hB, h, f, f->data[0], 0,f->height/4 );
			break;
		case 3:
			veejay_histogram_qdraw( h->hR, h , f, f->data[0], 0, f->height/4 );
			veejay_histogram_qdraw( h->hG, h , f, f->data[0], (f->width/4+10), f->height/4 );
			veejay_histogram_qdraw( h->hB, h,  f, f->data[0], (f->width/4+10)*2, f->height/4 );
			break;
	}

	veejay_histogram_equalize_rgb( his, f, rgb, in,st, mode );
	veejay_histogram_analyze_rgb(his,rgb,f );
	
	switch(mode)
	{
		case 0:
			veejay_histogram_qdraw( h->hR, h,f,f->data[0], 0,0 );
			break;
		case 1:
			veejay_histogram_qdraw( h->hG, h,f,f->data[0], 0,0);	
			break;
		case 2:
			veejay_histogram_qdraw( h->hB, h,f, f->data[0],0,0);
			break;
		case 3:
			veejay_histogram_qdraw( h->hR, h,f,f->data[0], 0, 0 );
			veejay_histogram_qdraw( h->hG, h,f,f->data[0], (f->width/4 + 10),0 );
			veejay_histogram_qdraw( h->hB, h,f,f->data[0], (f->width/4 + 10)*2,0 );
			break;
	}
}

void	veejay_histogram_equalize_rgb( void *his, VJFrame *f, uint8_t *rgb, int intensity, int strength, int mode )
{
	histogram_t *h = (histogram_t*) his;
	uint32_t LUTr[256];
	uint32_t LUTg[256];
	uint32_t LUTb[256];
	unsigned int i,j;
	unsigned int len = f->len;

	uint32_t r = f->width * 3;
	uint32_t H = f->height;

	switch(mode)
	{
		case 0:
			veejay_lut_calc( h->hR, LUTr, intensity , strength , len );
			for( i = 0; i < H; i ++ )
			{
				for( j = 0; j < r ; j +=3 )
					rgb[i*r+j] = LUTr[ rgb[i*r+j] ];
			}
			break;	
		case 1:
			veejay_lut_calc( h->hG, LUTg, intensity , strength , len );
			for( i = 0; i < H; i ++ )
			{
				for( j = 0; j < r; j += 3 )
					rgb[ i*r+j+1 ] = LUTg[rgb[i*r+j+1]];
			}
			break;
		case 2:
			veejay_lut_calc( h->hB, LUTb, intensity , strength , len );
			for( i = 0; i < H; i ++ )
			{
				for( j = 0; j < r; j += 3 )
					rgb[i*r+j+2] = LUTb[ rgb[i*r+j+2]];
			}
			break;
		case 3:
			veejay_lut_calc( h->hR, LUTr, intensity , strength , len );
			veejay_lut_calc( h->hG, LUTg, intensity , strength , len );
			veejay_lut_calc( h->hB, LUTb, intensity , strength , len );
	
			for( i = 0; i  < H; i ++ )
			{
				for( j = 0; j < r ; j +=3 )
				{
					rgb[i*r+j] = LUTr[ rgb[i*r+j] ];
					rgb[i*r+j+1] = LUTg[rgb[i*r+j+1]];
					rgb[i*r+j+2] = LUTb[ rgb[i*r+j+2]];
				}	
			}
			break;	
	}
}

void	veejay_histogram_equalize( void *his, VJFrame *f , int intensity, int strength)
{
	histogram_t *h = (histogram_t*) his;
	uint32_t LUT[256];
	unsigned int i;
	uint8_t *y;
	unsigned int len;

	len = f->len;
	veejay_lut_calc( h->hY, LUT, intensity, strength, len );
	y = f->data[0];
	for( i = 0; i < len ; i ++ )
		y[i] = LUT[ y[i] ];
}

void	veejay_histogram_analyze_rgb( void *his, uint8_t *rgb, VJFrame *f )
{
	histogram_t *h = (histogram_t*) his;
	build_histogram_rgb( rgb,h,f );
}

void	veejay_histogram_analyze( void *his, VJFrame *f, int type )
{
	histogram_t *h = (histogram_t*) his;

	build_histogram( h, f );
}

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
#define min4(a,b,c,d) MIN(MIN(MIN(a,b),c),d)
#define min5(a,b,c,d,e) MIN(MIN(MIN(MIN(a,b),c),d),e)

#ifndef MAX
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#endif

#define max4(a,b,c,d) MAX(MAX(MAX(a,b),c),d)

void	veejay_distance_transform8( uint8_t *plane, int w, int h, uint32_t *output)
{
	register unsigned int x,y;
	const uint8_t *I = plane;
	uint32_t *Id = output;
	const uint32_t wid = w - 1;
	const uint32_t hei = h - 2;

	for( y = 1; y < hei; y ++ )
	{
		for( x = 1; x < wid; x ++ )
		{
			if( I[ y * w + x ] )
				Id[ y * w + x ] = min4(
					(Id[ (y-1) * w + (x-1) ]) + 1,
					(Id[ (y-1) * w + x ]) + 1,
					(Id[ (y-1) * w + (x+1) ]) + 1,
					(Id[ y * w + (x-1) ]) + 1 );
		}
	}
	
	for( y = hei; y > 1; y -- )
	{
		for( x = wid; x > 1; x -- )
		{
			if( I[ y * w + x ] )	
				Id[ y * w + x ] = min5(
					(Id[ (y+1) * w + (x-1) ]) + 1,
					Id[ y * w + x ],
					(Id[ (y+1) * w + x ]) + 1,
					(Id[ y * w + (x + 1) ]) + 1,
					(Id[ (y+1) * w + (x+1) ]) + 1	
			);
		}
	}
}



void	veejay_distance_transform( uint32_t *plane, int w, int h, uint32_t *output)
{
	register unsigned int x,y;
	const uint32_t *I = plane;
	uint32_t *Id = output;
	const uint32_t wid = w - 1;
	const uint32_t hei = h - 2;

	for( y = 1; y < hei; y ++ )
	{
		for( x = 1; x < wid; x ++ )
		{
			if( I[ y * w + x ] )
				Id[ y * w + x ] = min4(
					(Id[ (y-1) * w + (x-1) ]) + 1,
					(Id[ (y-1) * w + x ]) + 1,
					(Id[ (y-1) * w + (x+1) ]) + 1,
					(Id[ y * w + (x-1) ]) + 1 );
		}
	}
	
	for( y = hei; y > 1; y -- )
	{
		for( x = wid; x > 1; x -- )
		{
			if( I[ y * w + x ] )	
				Id[ y * w + x ] = min5(
					(Id[ (y+1) * w + (x-1) ]) + 1,
					Id[ y * w + x ],
					(Id[ (y+1) * w + x ]) + 1,
					(Id[ y * w + (x + 1) ]) + 1,
					(Id[ (y+1) * w + (x+1) ]) + 1	
			);
		}
	}
}

uint32_t 	veejay_component_labeling(int w, int h, uint32_t *I , uint32_t *M)
{
	uint32_t label = 0;
	uint32_t x,y,i;
	uint32_t p1,p2;
	uint32_t Mi=0,Ma=0;
	uint32_t Eq[5000];

	uint32_t n_labels = 0;

	for( y = 1; y < (h-1); y ++ )
	{
		for ( x = 1; x < (w-1); x ++ )
		{
			if( I[ y * w + x] )
			{
				p1 = I[ (y-1) * w + x ];
				p2 = I[ y * w + (x-1) ];
			
				if( p1 == 0 && p2 == 0 )
				{
					label++;
					if( label > 5000 )
						return 0;

					I[ y * w + x ] = Eq[ label ] = label;
				} else if ( p1 == 0 ) {
					I[ y * w + x ] = p2;
				} else if ( p2 == 0 ) {
					I[ y * w + x ] = p1;
				} else if ( p1 == p2 ) {
					I[ y * w + x ] = p1;
				} else {	

			//	Mi = min4(p1,p2,p3,p4);
			//	Ma = max4(p1,p2,p3,p4);
				//Mi = MIN( p1,p2 );
				//Ma = MAX( p1,p2 );

				I[ y * w + x ] = Mi;
			
				while( Eq[ Ma ] != Ma )
					Ma = Eq[ Ma ];
				while( Eq[ Mi ] != Mi )
					Mi = Eq[ Mi ];

				if( Ma >= Mi )
					Eq[ Ma ] = Mi;
				else	
					Eq[ Mi ] = Ma;
				}
			}
		}
	}
	n_labels = 0;
	for( i = 1; i <= label; i ++ )
	{
		if( Eq[ i ] == i ) {
			n_labels ++;
			Eq[i] = n_labels;
		}
		else {	
			 Eq[i] = Eq[ Eq[i] ];
		}
	}

	if( n_labels > 5000 )
		return 0;	

	for( i = 1; i < n_labels ; i ++ )
		M[ i ] = 0;

	for( y = 0; y < h ; y ++ )
	{
		for( x = 0; x < w ; x ++ )
		{
			if( I[y * w + x ] )
			{
				I[y * w + x ] = Eq[ I[y * w + x] ];
				M[ I[y * w +x ] ]++;
			}
		}
	}

	return n_labels;
}

static inline int	center_of_blob(
	uint8_t *img,
	int width,
	int height,	
	uint8_t label,
	uint32_t *dx, uint32_t *dy, uint32_t *xsize, uint32_t *ysize)
{
	unsigned int i,j;
	uint32_t product_row = 0;
	uint32_t pixels_row = 0;
	uint32_t product_col = 0;
	uint32_t pixels_col = 0;
	uint32_t pixels_row_c = 0;
	uint32_t product_col_c = 0;

	for( i = 0; i < height; i ++ ) 
	{
		pixels_row = 0;
		for( j = 0; j < width; j ++ )
		{
			if ( img[i * width + j] == label )
				pixels_row++; 
		}
		if( pixels_row > *(xsize) )
			*xsize = pixels_row;
		product_row += (i * pixels_row);
		pixels_row_c += pixels_row;
	}

	for( j = 0; j < width; j ++ )
	{
		pixels_col = 0;
		for( i = 0; i < height; i ++ )
		{
			if( img[i * width + j ] == label )
				pixels_col ++;
		}
		if( pixels_col > *(ysize) )
			*ysize = pixels_col;
		product_col += (j * pixels_col);
		product_col_c += pixels_col;
	}


	if( pixels_row_c == 0 || product_col_c == 0 )
		return 0;

	*dy = ( product_row / pixels_row_c );
	*dx = ( product_col / product_col_c );

	return 1;
}

int	compare_l8( const void *a, const void *b )
{
	return ( *(int*)a - *(int*)b );
}

uint8_t 	veejay_component_labeling_8(int w, int h, uint8_t *I , uint32_t *M,
			uint32_t *XX,
			uint32_t *YY,
			uint32_t *xsize,
			uint32_t *ysize,
			int min_blob_weight)
{
	uint8_t label = 0;
	uint32_t x,y,i;
	uint8_t p1,p2;
	uint32_t Mi=0,Ma=0;
	uint8_t Eq[256];

	uint8_t n_labels = 0;

	veejay_memset( Eq, 0, sizeof(Eq) );
	
	for( y = 1; y < (h-1); y ++ )
	{
		for ( x = 1; x < (w-1); x ++ )
		{
			if( I[ y * w + x] )
			{
				p1 = I[ (y-1) * w + x ];
				p2 = I[ y * w + (x-1) ];

				if( p1 == 0 && p2 == 0 )
				{
					label++;
					if( label > 254 )
					{
						veejay_msg(0, "available labels exceeded");	
						return 0;
					}
					I[ y * w + x ] = Eq[ label ] = label;
				} else if ( p1 == 0) {
					I[ y * w + x ] = p2;
				} else if ( p2 == 0 ) {
					I[ y * w + x ] = p1;
				} else if ( p1 == p2 ) {
					I[ y * w + x ] = p1;
				} else {

				Mi = MIN( p1,p2 );
				Ma = MAX( p1,p2 );

				I[ y * w + x ] = Mi;
			
				while( Eq[ Ma ] != Ma )
					Ma = Eq[ Ma ];
				while( Eq[ Mi ] != Mi )
					Mi = Eq[ Mi ];

				if( Ma >= Mi )
					Eq[ Ma ] = Mi;
				else	
					Eq[ Mi ] = Ma;
				}
			}
		}
	}
	n_labels = 0;
	for( i = 1; i <= label; i ++ )
	{
		if( Eq[ i ] == i ) {
			n_labels ++;
			Eq[i] = n_labels;
		}
		else {	
			 Eq[i] = Eq[ Eq[i] ];
		}
	}

	if( n_labels > 254 )
	{
		veejay_msg(0, "Too many blobs");
		return 0;	
	}
	for( i = 0; i <= n_labels ; i ++ )
		M[ i ] = 0;

	for( y = 0; y < h ; y ++ )
	{
		for( x = 0; x < w ; x ++ )
		{
			if( I[y * w + x ] )
			{
				I[y * w + x ] = Eq[ I[y * w + x] ];
				M[ I[y * w +x ] ]++;
			}
		}
	}

	if( n_labels <= 0 )
		return 0;

	for( i = 1; i <= n_labels; i ++ )
	{
		if( (M[i] * 8) >= min_blob_weight )
		{
			if(! center_of_blob( I,w,h, i, &(XX[i]), &(YY[i]), &(xsize[i]), &(ysize[i]) ) )
			{
				M[i] = 0;
			}
		}
		else
		{
			M[i] = 0;
		}
	}

	return n_labels;
}

