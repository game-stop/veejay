/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.coml>
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
#ifndef VJE_COMMON
#define VJE_COMMON
#include <config.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <sys/types.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <veejaycore/vj-task.h>
#ifdef HAVE_ARM
#include <arm_neon.h>
#endif
#ifdef HAVE_ASM_SSE
#include <emmintrin.h>
#endif
#define MAX_SCRATCH_FRAMES 50
#define GREY_LEVELS 256

#define ALPHA_IGNORE_STR "Ignore Alpha-IN"
#define ALPHA_IGNORE 0
#define ALPHA_IN_A_STR "Alpha-IN A"
#define ALPHA_IN_A 1
#define ALPHA_IN_B_STR "Alpha-IN B"
#define ALPHA_IN_B 2
#define ALPHA_IN_A_OR_B_STR "Alpha-IN A or B"
#define ALPHA_IN_A_OR_B 3
#define ALPHA_IN_A_AND_B_STR "Alpha-IN A and B"
#define ALPHA_IN_A_AND_B 4

#define CLAMP_Y( a ) ( a < pixel_Y_lo_ ? pixel_Y_lo_ : (a > pixel_Y_hi_ ? pixel_Y_hi_ : a ) )
#define CLAMP_UV( a )( a < pixel_U_lo_ ? pixel_U_lo_ : (a > pixel_U_hi_ ? pixel_U_hi_ : a ) )

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
#define min4(a,b,c,d) MIN(MIN(MIN(a,b),c),d)
#define min5(a,b,c,d,e) MIN(MIN(MIN(MIN(a,b),c),d),e)

#ifndef MAX
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#endif

extern uint8_t pixel_Y_hi_;
extern uint8_t pixel_U_hi_;
extern uint8_t pixel_Y_lo_;
extern uint8_t pixel_U_lo_;

extern void vje_diff_plane( uint8_t *A, uint8_t *B, uint8_t *O, const int val, const int len );
extern void set_pixel_range(uint8_t Yhi,uint8_t Uhi, uint8_t Ylo, uint8_t Ulo);

extern void veejay_msg(int type, const char format[], ...);
extern int vje_get_rgb_parameter_conversion_type();


#define ALPHA_BLEND( a0, p0, p1 ) ( ((0xff - a0) * p0 + (a0 * p1) ) >> 8 )
#define FEATHER( P, op0, aB, Q, op1 ) \
	( ( P * op0 ) + ALPHA_BLEND( aB, P, Q) * op1 ) >> 8;

//#ifdef HAVE_ASM_3DNOW
//#define do_emms __asm__ __volatile__( "femms":::"memory" )
//#else

#if defined(ARCH_X86) || defined(ARCH_X86_64)
#define do_emms __asm__ __volatile__ ( "emms":::"memory" )
#endif

static inline double a_sin( double x ) {
	const double B = 4.0 / M_PI;
	const double C = -4.0 / (M_PI * M_PI);
	const double P = 0.225;

	x = fmod( x + M_PI, 2.0 * M_PI ) - M_PI;

	double y = B * x + C * x * fabs(x);

	return P * (y * fabs(y) - y) + y;
}

static inline double a_cos( double x ) {
	const double B = 4.0 / M_PI;
	const double C = -4.0 / (M_PI * M_PI);
	const double P = 0.225;

	x = fmod( x + M_PI , 2.0 * M_PI ) - M_PI;

	double y = B * x + C * x * fabs(x);

	return P * (y * fabs(y) - y) + y;
}

#ifdef ARCH_X86_64
#define sin_cos(si, co, x) asm ("fsincos" : "=t" (co), "=u" (si) : "0" (x))
#define fast_sin(res__,x) asm ("fsin" : "=t" (res__) : "0" (x))
#define fast_cos(res__,x) asm ("fcos" : "=t" (res__) : "0" (x))
#define fast_abs(res__,x) asm ("fabs" : "=t" (res__) : "0" (x))
#define fast_exp(res__,x) asm ("fexp" : "=t" (res__) : "0" (x))
#else
#define sin_cos(si, co, x)     si = sin(x); co = cos(x)
#define fast_sin(res,x ) res = a_sin(x)
#define fast_cos(res,x ) res = a_cos(x)
#define fast_abs(res,x ) res = abs(x)
#define fast_exp(res,x ) res = exp(x)
#endif


#if defined(HAVE_ASM_SSE2)
#include <emmintrin.h>
static inline double __sqrt_sd(double value) {
	double ret;
	__m128d v = _mm_load_sd(&value);
	_mm_store_sd(&ret, _mm_sqrt_sd(v, v));
	return ret;
}
#define fast_sqrt( res,x ) res = __sqrt_sd(x)
#else
#define fast_sqrt( res,x ) res = sqrt(x)
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

/* RGB to YUV conversion, www.fourcc.org */

#define Y_Redco		( 0.257f )
#define Y_Greenco	( 0.504f )
#define Y_Blueco	( 0.098f )

#define U_Redco		( 0.439f )
#define U_Greenco	( 0.368f )
#define U_Blueco	( 0.071f )

#define V_Redco		( 0.148f )
#define V_Greenco	(0.291f )
#define V_Blueco	(0.439f )


/*
  http://www.w3.org/Graphics/JPEG/jfif.txt
  YCbCr (256 levels) can be computed directly from 8-bit RGB as follows:
  IEC 601
 */

/* MJPEGtools lavtools/colorspace.c by matthew */
#define YCBCR_to_IEC601 ( y, u, v ) \
 {\
 	y =  y * 219.0 / 256.0 + 16 ;\
	u =  (u - 128 ) * 224.0 / 256.0 + 128;\
	v =  (v - 128 ) * 224.0 / 256.0 + 128;\
  }

#define IEC601_to_YCBCR( y, u, v ) \
 {\
	y = ( y  - 16 ) / 219.0 * 256.0;\
	u = ( u - 128 ) / 224.0 * 256.0 + 128;\
	v = ( v - 128 ) / 224.0 * 256.0 + 128;\
 }



static inline int myround(float n) 
{
  if (n >= 0) 
    return (int)(n + 0.5f);
  else
    return (int)(n - 0.5f);
}
/* End colorspace.c */	


#define JPEGJFIF_RGB( r,g,b,y,u,v) \
 {\
	r = y + 1.40200 * ( u - 128 );\
	g = y - 0.34414 * ( v - 128 ) - 0.71414 * ( u - 128 );\
	b = y + 1.77200 * ( v - 128 );\
 }

#define COLOR_rgb2yuv(r,g,b, y,u,v ) \
 {\
 y = (int) (  (Y_Redco  * (float) r) + (Y_Greenco * (float)g) + (Y_Blueco * (float)b) + 16.0);\
 u = (int) (  (U_Redco  * (float) r) - (U_Greenco * (float)g) + (U_Blueco * (float)b) + 128.0);\
 v = (int) ( -(V_Redco  * (float) r) - (V_Greenco * (float)g) + (V_Blueco * (float)b) + 128.0);\
 }
/*
#define CCIR601_rgb2yuv(r,g,b,y,u,v) \
 {\
 float Ey = (0.299f * (float)r) + (0.587f * (float) g) + (0.114f * (float)b );\
 float Eu = (0.713f * ( ((float)r) - Ey ) );\
 float Ev = (0.564f * ( ((float)b) - Ey ) );\
 y = (int) ( 255.0 * Ey );\
 u = (int) (( 255.0 * Eu ) + 128);\
 v = (int) (( 255.0 * Ev ) + 128);\
}*/


#define CCIR601_rgb2yuv(r,g,b,y,u,v)\
{\
    float Ey = ((0.2568f * (float)r ) + ( 0.5041f * (float) g) + (0.0979f * (float) b ) ) + 16.0;\
    float Eu = ((-0.1482f * (float)r) + (-0.2910f * (float) g) + (0.4392f * (float) b ) ) + 128.0;\
    float Ev = ((0.4392f * (float)r ) + (-0.3678f * (float) g) + (-0.0714f * (float) b )) + 128.0;\
    y = CLAMP_Y(myround(Ey));\
    u = CLAMP_UV(myround(Eu));\
    v = CLAMP_UV(myround(Ev));\
}

#define GIMP_rgb2yuv(r,g,b,y,u,v)\
 {\
	float Ey = (0.299 * (float)r) + (0.587 * (float)g) + (0.114 * (float) b);\
	float Eu = (-0.168736 * (float)r) - (0.331264 * (float)g) + (0.500 * (float)b) + 128.0;\
	float Ev = (0.500 * (float)r) - (0.418688 * (float)g) - (0.081312 * (float)b)+ 128.0;\
    	y = CLAMP_Y(myround(Ey));\
	u = CLAMP_UV(myround(Eu));\
	v = CLAMP_UV(myround(Ev));\
 }

#define CCYUV_red( r,y,u,v )\
	r = (int)( (float) y + 1.14f * (float) v) 

#define CCYUV_green( g,y,u,v )\
	g = (int)( (float)y - 0.396f * (float)u - 0.581f * (float)v)

#define	CCYUV_blue( b,y,u,v )\
	b = (int)( (float) y + 2.029f * (float) u )

enum 
{
	GIMP_RGB=0,
	CCIR601_RGB=1,
	OLD_RGB=2,
	JFIF_RGB=3,
};

#define	_rgb2yuv(r,g,b,y,u,v)\
{\
 if( vje_get_rgb_parameter_conversion_type() == GIMP_RGB )\
	GIMP_rgb2yuv(r,g,b,y,u,v)\
 if( vje_get_rgb_parameter_conversion_type() == CCIR601_RGB )\
	CCIR601_rgb2yuv(r,g,b,y,u,v)\
 if( vje_get_rgb_parameter_conversion_type() == OLD_RGB )\
	COLOR_rgb2yuv(r,g,b,y,u,v)\
}	
#define round1(x) ( (int32_t)( (x>0) ? (x) : (x) ))
#define _CLAMP(a,min,max) ( round1(a) < min ? min : ( round1(a) > max ? max : round1(a) ))


#define	__init_lookup_table( T,Tsize, b_min, b_max, min, max ) \
	float __c = ((float) (Tsize-1)) / ( (float) b_max - (float) b_min );\
	int __i;\
	for( __i = 0; __i < Tsize; __i ++ ) {\
		T[__i] = _CLAMP( (float) __i * __c - (float) b_min, min,max );\
	}\

typedef uint8_t (*pix_func_Y) (uint8_t y1, uint8_t y2);
typedef uint8_t (*pix_func_C) (uint8_t y1, uint8_t y2);
typedef uint8_t (*_pf) (uint8_t a, uint8_t b);


pix_func_Y get_pix_func_Y(const int pix_type);	/* get blend function for luminance values */
pix_func_C get_pix_func_C(const int pix_type);	/* get blend function for chrominance values */

typedef struct
{
	uint8_t *data[3];
	int	w;
	int	h;
} picture_t;

typedef struct
{
	int w;
	int h;
} matrix_t;

typedef	matrix_t (*matrix_f)(int i, int s, int w, int h);

matrix_t matrix_placementA(int photoindex, int size, int w , int h);
matrix_t matrix_placementB(int photoindex, int size, int w , int h);
matrix_t matrix_placementC(int photoindex, int size, int w , int h);
matrix_t matrix_placementD(int photoindex, int size, int w , int h);
matrix_t matrix_placementE(int photoindex, int size, int w , int h);
matrix_t matrix_placementF(int photoindex, int size, int w , int h);
matrix_t matrix_placementG(int photoindex, int size, int w , int h);
matrix_t matrix_placementH(int photoindex, int size, int w , int h);

matrix_f get_matrix_func(int type);
int get_matrix_func_n();
void fx_shuffle_int_array( int *A, unsigned int n );

int power_of(int size);
int max_power(int w);

void frameborder_yuvdata(uint8_t * input_y, uint8_t * input_u,
			 uint8_t * input_v, uint8_t * putin_y,
			 uint8_t * putin_u, uint8_t * putin_v, int width,
			 int height, int top, int bottom, int left,
			 int right, int shiftw, int shifth);

void blackborder_yuvdata(uint8_t * input_y, uint8_t * input_u,
			 uint8_t * input_v, int width, int height, int top,
			 int bottom, int left, int right, int shiftw, int shifth, int color);

void deinterlace(uint8_t *data, int width, int height, int noise);
_pf		_get_pf(int type);

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
uint8_t bl_pix_subtract_Y(uint8_t y1, uint8_t y2);
unsigned int fastrand(int val);
int bl_pix_get_color_y(int color_num);
int bl_pix_get_color_cb(int color_num);
int bl_pix_get_color_cr(int color_num);
double m_get_radius(int x, int y);
double m_get_angle(int x, int y);
double m_get_polar_x(double r, double a);
double m_get_polar_y(double r, double a);
uint8_t _pf_dneg(uint8_t a, uint8_t b);
uint8_t _pf_lghtn(uint8_t a, uint8_t b);
uint8_t _pf_dneg2(uint8_t a,uint8_t b);
uint8_t _pf_min(uint8_t a, uint8_t b);
uint8_t _pf_max(uint8_t a,uint8_t b);
uint8_t _pf_pq(uint8_t a,uint8_t b);
uint8_t _pf_none(uint8_t a, uint8_t b);
int calculate_luma_value(uint8_t *Y, int w , int h);
int calculate_cbcr_value(uint8_t *Cb,uint8_t *Cr, int w, int h);
uint32_t veejay_component_labeling(int w, int h, uint32_t *I , uint32_t *M);
int i_cmp( const void *a, const void *b );
int compare_l8( const void *a, const void *b );
void veejay_blur(uint8_t *dst, uint8_t *src, int w, int radius, int dstStep, int srcStep);
void blur2(uint8_t *dst, uint8_t *src, int w, int radius, int power, int dstStep, int srcStep);
extern void viewport_destroy(void *v);
extern void vj_get_yuvgrey_template(VJFrame *src, int w, int h);
extern int    yuv_use_auto_ccir_jpeg();

void veejay_draw_circle( uint8_t *data, int cx, int cy, const int bw, const int bh, const int w, const int h, int radius, uint8_t value );

void veejay_histogram_analyze( void *his, VJFrame *f , int t);
void veejay_histogram_del(void *his);
void *veejay_histogram_new();
void veejay_histogram_draw( void *his, VJFrame *src, VJFrame *dst , int intensity, int strength );
void veejay_histogram_equalize( void *his, VJFrame *f, int intensity, int strength );
void vje_histogram_auto_eq( VJFrame *frame );
void veejay_histogram_analyze_rgb( void *his, uint8_t *rgb, VJFrame *f );
void veejay_histogram_equalize_rgb( void *his, VJFrame *f, uint8_t *rgb, int in, int st, int mode );
void veejay_histogram_draw_rgb( void *his, VJFrame *f, uint8_t *rgb, int in, int st, int mode );

void veejay_distance_transform( uint32_t *plane, int w, int h, uint32_t *output);
void veejay_distance_transform8( uint8_t *plane, int w, int h, uint32_t *output);

uint8_t veejay_component_labeling_8(int w, int h, uint8_t *I , uint32_t *M, uint32_t *XX, uint32_t *YY,uint32_t *xsize, uint32_t *ysize, int blob);

void vj_diff_plane( uint8_t *A, uint8_t *B, uint8_t *O, int threshold, int len );
void binarify_1src( uint8_t *dst, uint8_t *src, uint8_t threshold,int reverse, int w, int h );
void binarify( uint8_t *bm, uint8_t *bg, uint8_t *src,int threshold,int reverse, const int len);
void vje_mean_filter( const uint8_t *src, uint8_t *dst, const int w, const int h );

void init_sqrt_map_pixel_values();
double sqrt_table_get_pixel( int x, int h );
void sqrt_table_pixels_free();


typedef enum _vj_effect_orientation{
    VJ_EFFECT_ORIENTATION_CENTER = 0,
    VJ_EFFECT_ORIENTATION_NORTH,
    VJ_EFFECT_ORIENTATION_NORTHEAST,
    VJ_EFFECT_ORIENTATION_EAST,
    VJ_EFFECT_ORIENTATION_SOUTHEAST,
    VJ_EFFECT_ORIENTATION_SOUTH,
    VJ_EFFECT_ORIENTATION_SOUTHWEST,
    VJ_EFFECT_ORIENTATION_WEST,
    VJ_EFFECT_ORIENTATION_NORTHWEST,
}vj_effect_orientation;

typedef enum _vj_effect_parity{
    VJ_EFFECT_PARITY_EVEN = 0,
    VJ_EFFECT_PARITY_ODD ,
    VJ_EFFECT_PARITY_NO ,
}vj_effect_parity;

void grid_getbounds_from_orientation(int radius, vj_effect_orientation orientation, vj_effect_parity parity, int * x_inf, int * y_inf, int * x_sup, int * y_sup);

double atan2_approx(double y, double x);

#ifdef HAVE_ASM_MMX
void vje_load_mask(uint8_t val);
void vje_mmx_negate_frame(uint8_t *dst, uint8_t *in, uint8_t val, int len );
void vje_mmx_negate( uint8_t *dst, uint8_t *in );
#endif
void vje_build_value_hint_list_array( vj_value_hint_t **hints, int limit, int num, char **arr );
void vje_build_value_hint_list( vj_value_hint_t **hints, int num, int limit, ... );
vj_value_hint_t **vje_init_value_hint_list(int n_params);
#endif
