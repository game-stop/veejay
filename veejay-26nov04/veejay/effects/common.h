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



#include <stdint.h>
#include <sys/types.h>


#define MAX_SCRATCH_FRAMES 25
#define func_opacity(a,b,p,q) (  ((a * p) + (b * q)) >> 8 )
#define limit_luma(c)  ( c < 16 ? 16 : ( c > 235 ? 235 : c) )
#define limit_chroma(c) ( c < 16 ? 16 :  ( c > 240 ? 240 : c) )
#define func_multiply(a,b) ( (a * b) >> 8 )
#define func_additive(a,b) ( a + (2 * b) - 235 )
#define func_substractive(a,b) ( a + (b - 235) )

#ifdef HAVE_MMX
#define MMX_load8byte_mm7(data)__asm__("\n\t movq %0,%%mm7\n":	"=m" (data):)
#endif

#define WITH_SINCOS
/* intentionally dangerous internal macro */
#ifndef WITH_SINCOS
# define sin_cos(si, co, x)     si = sin(x); co = cos(x)
#else
# define sin_cos(si, co, x)     asm ("fsincos" : "=t" (co), "=u" (si) : "0" (x))
# define fast_sqrt(res,x) 	asm ("fsqrt" : "=t" (res) : "0" (x)) 
# define fast_sin(res,x)	asm ("fsin" : "=t" (res) : "0" (x))
# define fast_cos(res,x)	asm ("fcos" : "=t" (res) : "0" (x))
# define fast_abs(res,x)	asm ("fabs" : "=t" (res) : "0" (x))
# define fast_exp(res,x)	asm ("fexp" : "=t" (res) : "0" (x))
#endif



#define Y_Red 		( 0.29900)
#define Y_Green 	( 0.58700)
#define Y_Blue 		( 0.11400)

#define U_Red		(-0.16874)
#define U_Green		(-0.33126)
#define U_Blue		( 0.50000)

#define V_Red		( 0.50000)
#define V_Green		(-0.41869)
#define V_Blue		(-0.08131)

#define Y_Redco		( 0.257 )
#define Y_Greenco	( 0.504 )
#define Y_Blueco	( 0.098 )
#define U_Redco		(-0.148 )
#define U_Greenco	(-0.291 )
#define U_Blueco	( 0.439 )
#define V_Redco		( 0.439 )
#define V_Greenco	(-0.368 )
#define V_Blueco	(-0.071 )


typedef uint8_t(*pix_func_Y) (uint8_t y1, uint8_t y2);
typedef uint8_t(*pix_func_C) (uint8_t y1, uint8_t y2);

pix_func_Y get_pix_func_Y(const int pix_type);	/* get blend function for luminance values */
pix_func_C get_pix_func_C(const int pix_type);	/* get blend function for chrominance values */


void frameborder_yuvdata(uint8_t * input_y, uint8_t * input_u,
			 uint8_t * input_v, uint8_t * putin_y,
			 uint8_t * putin_u, uint8_t * putin_v, int width,
			 int height, int top, int bottom, int left,
			 int right, int shiftw, int shifth);

void blackborder_yuvdata(uint8_t * input_y, uint8_t * input_u,
			 uint8_t * input_v, int width, int height, int top,
			 int bottom, int left, int right, int shiftw, int shifth, int color);

void
deinterlace(uint8_t *data, int width, int height, int noise);


uint8_t bl_pix_additive_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_divide_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_multiply_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_substract_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_softburn_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_inverseburn_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_colordodge_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_mulsub_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_lighten_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_difference_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_diffnegate_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_exclusive_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_basecolor_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_freeze_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_unfreeze_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_hardlight_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_relativeadd_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_relativeadd_C(uint8_t y1, uint8_t y2);
uint8_t bl_pix_relativesub_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_maxsubsel_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_minsubsel_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_addsubsel_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_maxsel_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_minsel_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_dblbneg_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_dblbneg_C(uint8_t y1, uint8_t y2);
uint8_t bl_pix_muldiv_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_add_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_relneg_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_relneg_C(uint8_t y1, uint8_t y2);
uint8_t bl_pix_selfreeze_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_selunfreeze_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_seldiffneg_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_swap_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_swap_C(uint8_t y1, uint8_t y2);
uint8_t bl_pix_test3_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_test3_C(uint8_t y1, uint8_t y2);
uint8_t bl_pix_sub_distorted_C(uint8_t y1, uint8_t y2);
uint8_t bl_pix_sub_distorted_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_add_distorted_C(uint8_t y1, uint8_t y2);
uint8_t bl_pix_add_distorted_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_noswap_Y(uint8_t y1, uint8_t y2);
uint8_t bl_pix_noswap_C(uint8_t y1, uint8_t y2);
uint8_t bl_pix_seldiff_Y(uint8_t y1, uint8_t y2);
void _4byte_copy( uint8_t *src, uint8_t *dst, int width,int height, int x_start, int y_start);
unsigned int fastrand(int val);
int bl_pix_get_color_y(int color_num);
int bl_pix_get_color_cb(int color_num);
int bl_pix_get_color_cr(int color_num);

void	memset_ycbcr(	uint8_t *in, 
			uint8_t *out, 
			uint8_t val, 
			unsigned int width, 
			unsigned int height);

double m_get_radius(int x, int y);
double m_get_angle(int x, int y);
double m_get_polar_x(double r, double a);
double m_get_polar_y(double r, double a);

