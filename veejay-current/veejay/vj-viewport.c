/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2007 Niels Elburg <nelburg@looze.net>
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
 *
 * Viewport with Perspective Transform Estimation for Veejay
 *
 * Code borrowed from:
 *	Gimp 1.0,2.0   (Perspective transformation (C) Spencer Kimball & Peter Mattis)
 *	Cinelerra      (Motion plugin, no author in any file present. GPL2).
 *	Xine           (bresenham line drawing routine)
 *
 *	Notable differences:
 *	  1. matrix_determinant(), invert_matrix(), adjoint_matrix()
 *	  2. Caches pixel coordinates in an integer map to speed up processing
 *	  3. No interpolation
 *	  4. Realtime              
 *	  5. Embedded UI (OSD) 
 *	  	  
 */
#include <config.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-common.h>
#include <libvje/vje.h>
#include <veejay/vj-viewport.h>
#include <libvje/effects/opacity.h>

#define X0 0
#define Y0 1
#define X1 2
#define Y1 3
#define X2 4
#define Y2 5
#define X3 6
#define Y3 7
#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
#ifndef MAX
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#endif

#define clamp1(x, y, z) ((x) = ((x) < (y) ? (y) : ((x) > (z) ? (z) : (x))))
#define distance1(x1,y1,x2,y2) (  sqrt( (x1 - x2) * (x1 - x2) + ( y1 - y2 ) * (y1 -y2 ) ) )

#define round1(x) ( (int32_t)( (x>0) ? (x) + 0.5 : (x)  - 0.5 ))
#define min4(a,b,c,d) MIN(MIN(MIN(a,b),c),d)
#define max4(a,b,c,d) MAX(MAX(MAX(a,b),c),d)

#undef _PREFETCH
#undef _PREFETCHW
#ifdef HAVE_ASM_DNOW
#define _PREFETCH  "prefetch"
#define _PREFETCHW "prefetchw"
#elif defined ( HAVE_ASM_MMX2 )
#define _PREFETCH "prefetchnta"
#define _PREFETCHW "prefetcht0"
#else
#define _PREFETCH "/nop"
#define _PREFETCHW "/nop"
#endif


typedef struct
{
	float m[3][3];
} matrix_t;

typedef struct
{
	int32_t x,y;
	int32_t h,w;
	float points[9];
	int   users[4];
	float usermouse[2];
	int   userm;
	int   user;
	int	save;
	int   user_ui;
	int   user_reverse;
	int   user_mode;
	int	grid_size;
	int   renew;
	int   disable;
	float x1;
	float x2;
	float x3;
	float x4;
	float y1;
	float y2;
	float y3;
	float y4;
	int32_t *map;
	uint8_t *img[3];
	matrix_t *M;
	matrix_t *m;
	char *help;
	uint8_t *grid;
	uint8_t  grid_val;
	int	parameters[8];
	char    *homedir;
	int32_t tx1,tx2,ty1,ty2;
	int	mode;
} viewport_t;

typedef struct
{
	int	reverse;
	int	grid_size;
	int	grid_color;
	int	frontback;
	float x1;
	float x2;
	float x3;
	float x4;
	float y1;
	float y2;
	float y3;
	float y4;
} viewport_config_t;


static void		viewport_draw( void *data, uint8_t *img );
static inline	int	grab_pixel( uint8_t *plane, int x, int y, int w );
static void		viewport_update_perspective( viewport_t *v, float *values );
static void		viewport_update_grid( viewport_t *v, int size, uint8_t val );
static void		viewport_process( viewport_t *p );
static int		viewport_configure( 
					viewport_t *v,
					float x1, float y1,
					float x2, float y2,
					float x3, float y3,
					float x4, float y4,
					int32_t x,  int32_t y,
					int32_t w,  int32_t h,
					uint32_t reverse,
					uint8_t color,
					int size);

static void		viewport_draw_grid(int32_t w, int32_t h, uint8_t *grid, int32_t grid_size, uint8_t op0);
static matrix_t		*viewport_transform(float x1,float y1,float x2,float y2,float *coord );
static inline void	point_map( matrix_t *M, float x, float y, float *nx, float *ny);
static matrix_t *	viewport_invert_matrix( matrix_t *M, matrix_t *D );
static	matrix_t 	*viewport_adjoint_matrix(matrix_t *M);
static double		viewport_matrix_determinant( matrix_t *M );
static matrix_t 	*viewport_multiply_matrix( matrix_t *A, matrix_t *B );
static	void		viewport_copy_from( matrix_t *A, matrix_t *B );
static void		viewport_scale_matrix( matrix_t *M, float sx, float sy );
static void		viewport_translate_matrix( matrix_t *M, float x, float y );
static matrix_t		*viewport_identity_matrix(void);
static matrix_t		*viewport_matrix(void);
static void		viewport_update_context_help(viewport_t *v);
static void		viewport_find_transform( float *coord, matrix_t *M );
static void		viewport_print_matrix( matrix_t *M );
static void 		viewport_line (uint8_t *plane,int x1, int y1, int x2, int y2, int w, int h, uint8_t col);
static void		draw_point( uint8_t *plane, int x, int y, int w, int h, int size, int col );
static viewport_config_t *viewport_load_settings( const char *dir, int mode );
static void		viewport_save_settings( viewport_t *v , int frontback);
static	void		viewport_prepare_process( viewport_t *v );

#ifdef HAVE_X86CPU
static inline int int_max( int a, int b )
{
        b = a-b;
        a -= b & (b>>31);
        return a;
}
static  inline int int_min( int a, int b )
{
        b = b- a;
        a += b & (b>>31);       // if(a < b) then a = b
        return a;
}
#else
static	inline int int_max(int a, int b )
{
	return MAX(a,b);
}
static	inline int int_min(int a, int b )
{
	return MIN(a,b);
}
#endif

static void		viewport_print_matrix( matrix_t *M )
{
	veejay_msg(0, "|%f\t%f\t%f", M->m[0][0], M->m[0][1], M->m[0][2] );
	veejay_msg(0, "|%f\t%f\t%f", M->m[1][0], M->m[1][1], M->m[1][2] );
	veejay_msg(0, "|%f\t%f\t%f", M->m[2][0], M->m[2][1], M->m[2][2] );
}
/*
 * Bresenham line implementation from Xine
 */

static void viewport_line (uint8_t *plane,
		      int x1, int y1, int x2, int y2, int w, int h, uint8_t col) {

  uint8_t *c;
  int dx, dy, t, inc, d, inc1, inc2;
  int swap_x = 0;
  int swap_y = 0;

  /* sort line */
  if (x2 < x1) {
    t  = x1;
    x1 = x2;
    x2 = t;
    swap_x = 1;
  }
  if (y2 < y1) {
    t  = y1;
    y1 = y2;
    y2 = t;
    swap_y = 1;
  }

  /* clip line */
  if (x1 < 0) {
    y1 = y1 + (y2-y1) * -x1 / (x2-x1);
    x1 = 0;
  }
  if (y1 < 0) {
    x1 = x1 + (x2-x1) * -y1 / (y2-y1);
    y1 = 0;
  }
  if (x2 > w) {
    y2 = y1 + (y2-y1) * (w-x1) / (x2-x1);
    x2 = w;
  }
  if (y2 > h) {
    x2 = x1 + (x2-x1) * (h-y1) / (y2-y1);
    y2 = h;
  }

  if (x1 >= w || y1 >= h)
    return;

  dx = x2 - x1;
  dy = y2 - y1;

  /* unsort line */
  if (swap_x) {
    t  = x1;
    x1 = x2;
    x2 = t;
  }
  if (swap_y) {
    t  = y1;
    y1 = y2;
    y2 = t;
  }

  if( dx>=dy ) {
    if( x1>x2 )
    {
      t = x2; x2 = x1; x1 = t;
      t = y2; y2 = y1; y1 = t;
    }

    if( y2 > y1 ) inc = 1; else inc = -1;

    inc1 = 2*dy;
    d = inc1 - dx;
    inc2 = 2*(dy-dx);

    c = plane + y1 * w + x1;

    while(x1<x2)
    {
      *c++ = col;

      x1++;
      if( d<0 ) {
        d+=inc1;
      } else {
        y1+=inc;
        d+=inc2;
        c = plane + y1 * w + x1;
      }
    }
  } else {
    if( y1>y2 ) {
      t = x2; x2 = x1; x1 = t;
      t = y2; y2 = y1; y1 = t;
    }

    if( x2 > x1 ) inc = 1; else inc = -1;

    inc1 = 2*dx;
    d = inc1-dy;
    inc2 = 2*(dx-dy);

    c = plane + y1 * w + x1;

    while(y1<y2) {
      *c = col;

      c += w;
      y1++;
      if( d<0 ) {
	d+=inc1;
      } else {
	x1+=inc;
	d+=inc2;
	c = plane + y1 * w + x1;
      }
    }
  }
}

static	void	draw_point( uint8_t *plane, int x, int y, int w, int h, int size, int col )
{
	int x1 = x - size *2;
	int y1 = y - size*2;
	int x2 = x + size*2;
	int y2 = y + size*2;

	if( x1 < 0 ) x1 = 0; else if ( x1 > w ) x1 = w;
	if( y1 < 0 ) y1 = 0; else if ( y1 > h ) y1 = h;
	if( x2 < 0 ) x2 = 0; else if ( x2 > w ) x2 = w;
	if( y2 < 0 ) y2 = 0; else if ( y2 > h ) y2 = h;

	unsigned int i,j;
	for( i = y1; i < y2; i ++ ) 
	{
		for( j = x1; j < x2 ; j ++ )
			plane[ i * w + j ] = col;
	}
}

static void		viewport_find_transform( float *coord, matrix_t *M )
{
	double dx1,dx2,dx3,dy1,dy2,dy3;
	double det1,det2;

	dx1 = coord[X1] - coord[X3];
	dx2 = coord[X2] - coord[X3];
	dx3 = coord[X0] - coord[X1] + coord[X3] - coord[X2];
	
	dy1 = coord[Y1] - coord[Y3];
	dy2 = coord[Y2] - coord[Y3];
	dy3 = coord[Y0] - coord[Y1] + coord[Y3] - coord[Y2];

	/* is the mapping affine? */
	if( ((dx3 == 0.0) && (dy3==0.0)) )
	{
		M->m[0][0] = coord[X1] - coord[X0];
		M->m[0][1] = coord[X3] - coord[X1];
		M->m[0][2] = coord[X0];

		M->m[1][0] = coord[Y1] - coord[Y0];
		M->m[1][1] = coord[Y3] - coord[Y1];
		M->m[1][2] = coord[Y0];

		M->m[2][0] = 0.0;
		M->m[2][1] = 0.0;
	}
	else
	{
		det1 = dx3 * dy2 - dy3 * dx2;
		det2 = dx1 * dy2 - dy1 * dx2;
		M->m[2][0] = det1/det2;
	
		det1 = dx1 * dy3 - dy1 * dx3;
		det2 = dx1 * dy2 - dy1 * dx2;
		M->m[2][1] = det1/det2;

		M->m[0][0] = coord[X1] - coord[X0] + M->m[2][0] * coord[X1];
		M->m[0][1] = coord[X2] - coord[X0] + M->m[2][1] * coord[X2];
		M->m[0][2] = coord[X0];

		M->m[1][0] = coord[Y1] - coord[Y0] + M->m[2][0] * coord[Y1];
		M->m[1][1] = coord[Y2] - coord[Y0] + M->m[2][1] * coord[Y2];
		M->m[1][2] = coord[Y0];
	}

	M->m[2][2] = 1.0;
}

static	void	viewport_update_context_help(viewport_t *v)
{
	char render_mode[32];
	sprintf(render_mode, "%s", ( v->user_ui == 0 ? "Grid Mode" : "Render Mode" ) );
	char reverse_mode[32];
	sprintf(reverse_mode, "%s", ( v->user_reverse ? "Forward"  : "Reverse" ) );
	char tmp[1024];
	char hlp[1024];

	if( v->user_ui )
	{
		sprintf(tmp, "Mouse Left = Select point\nMouse Left + SHIFT = Snap to grid\nMouse Right = %s\nMouse Middle = %s\nMouse Middle + SHIFT = Grid/Line Color\nMouse Wheel = Grid resolution (%dx%d)\nCTRL + h = Hide/Show this Help",
			reverse_mode, render_mode , v->grid_size,v->grid_size);
	}
	else
		sprintf(tmp, "Mouse Right = %s\nMouse Middle = %s\nCTRL + h = Hide/Show this Help", reverse_mode, render_mode );

	if(v->mode == 0 )
	{
		if( v->user_ui )
		sprintf(hlp, "Viewport\nPerspective Transform\n%s\n(1) %.2fx%.2f    Pos: %.2fx%.2f\n(2) %.2fx%.2f\n(3) %.2fx%.2f\n(4) %.2fx%.2f\n",
			tmp,v->x1,v->y1, v->usermouse[0],v->usermouse[1],
			v->x2,v->y2,v->x3,v->y3,v->x4,v->y4 );
		else
			sprintf(hlp, "Viewport\nPerspective Transform %s\n%s",
			reverse_mode, tmp );
	}
	else
	{
		if(v->user_ui )
			sprintf(hlp, "Projection calibration\n%s\n(1)  %.2fx%.2f    Pos: %.2fx%.2f\n(2) %.2fx%.2f\n(3) %.2fx%.2f\n(4) %.2fx%.2f\n",
                        tmp,v->x1,v->y1, v->usermouse[0],v->usermouse[1],
                        v->x2,v->y2,v->x3,v->y3,v->x4,v->y4 );
		else
			sprintf(hlp, "Projection calibration\nPerspective Transform %s\n%s", reverse_mode, tmp );

	}

	if(v->help)
		free(v->help);
	v->help = strdup( hlp );
}

static matrix_t	*viewport_matrix(void)
{
	matrix_t *M = (matrix_t*) vj_malloc(sizeof(matrix_t));
	uint32_t i,j;
	for( i = 0;i < 3 ; i ++ )
	{
	  for( j = 0; j < 3 ; j++ )
		M->m[i][j] = 0.0;
	}
	return M;
}

static matrix_t	*viewport_identity_matrix(void)
{
	matrix_t *M = viewport_matrix();
	M->m[0][0] = 1.0;
	M->m[1][1] = 1.0;
	M->m[2][2] = 1.0;
	return M;
}

static void		viewport_translate_matrix( matrix_t *M, float x, float y )
{
	float g = M->m[2][0];
	float h = M->m[2][1];
	float i = M->m[2][2];

	M->m[0][0] += x * g;
	M->m[0][1] += x * h;
	M->m[0][2] += x * i;

	M->m[1][0] += y * g;
	M->m[1][1] += y * h;
	M->m[1][2] += y * i;
}

static void		viewport_scale_matrix( matrix_t *M, float sx, float sy )
{
	M->m[0][0] *= sx;
	M->m[0][1] *= sx;
	M->m[0][2] *= sx;

	M->m[1][0] *= sy;
	M->m[1][1] *= sy;
	M->m[1][2] *= sy;
}

static	void		viewport_copy_from( matrix_t *A, matrix_t *B )
{
	uint32_t i,j;
	for( i =0 ; i < 3; i ++ )
		for( j = 0; j < 3 ; j ++ )
			A->m[i][j] = B->m[i][j];
}

static matrix_t 	*viewport_multiply_matrix( matrix_t *A, matrix_t *B )
{
	matrix_t *R = viewport_matrix();

	R->m[0][0] = A->m[0][0] * B->m[0][0] + A->m[0][1] * B->m[1][0] + A->m[0][2] * B->m[2][0];
	R->m[0][1] = A->m[0][0] * B->m[0][1] + A->m[0][1] * B->m[1][1] + A->m[0][2] * B->m[2][1];
	R->m[0][2] = A->m[0][0] * B->m[0][2] + A->m[0][1] * B->m[1][2] + A->m[0][2] * B->m[2][2];

	R->m[1][0] = A->m[1][0] * B->m[0][0] + A->m[1][1] * B->m[1][0] + A->m[1][2] * B->m[2][0];
	R->m[1][1] = A->m[1][0] * B->m[0][1] + A->m[1][1] * B->m[1][1] + A->m[1][2] * B->m[2][1];
	R->m[1][2] = A->m[1][0] * B->m[0][2] + A->m[1][1] * B->m[1][2] + A->m[1][2] * B->m[2][2];

	R->m[2][0] = A->m[2][0] * B->m[0][0] + A->m[2][1] * B->m[0][1] + A->m[2][2] * B->m[2][0];
	R->m[2][1] = A->m[2][0] * B->m[0][1] + A->m[2][1] * B->m[1][1] + A->m[2][2] * B->m[2][1];
	R->m[2][2] = A->m[2][0] * B->m[0][2] + A->m[2][1] * B->m[1][2] + A->m[2][2] * B->m[2][2];	


	return R;
}

static double		viewport_matrix_determinant( matrix_t *M )
{
	double D = M->m[0][0] * M->m[1][1] * M->m[2][2] + 
		    M->m[0][1] * M->m[1][2] * M->m[2][0] +
		    M->m[2][0] * M->m[1][1] * M->m[0][2] -
		    M->m[1][0] * M->m[0][1] * M->m[2][2] -
		    M->m[2][1] * M->m[1][2] * M->m[0][0];
	 	     
	return D;
}

static	matrix_t 	*viewport_adjoint_matrix(matrix_t *M)
{
	matrix_t *A = viewport_matrix();
	A->m[0][0] = M->m[0][0];
	A->m[0][1] = -M->m[0][1];
	A->m[0][2] = M->m[0][2];
	A->m[1][0] = -M->m[1][0];
	A->m[1][1] = M->m[1][1];
	A->m[1][2] = -M->m[1][2];
	A->m[2][0] = M->m[2][0];
	A->m[2][1] = -M->m[2][1];
	A->m[2][2] = M->m[2][2];
	return A;
}

static matrix_t *		viewport_invert_matrix( matrix_t *M, matrix_t *D )
{
	double det = viewport_matrix_determinant( M );
	if( det == 0.0 )
	{
		veejay_msg(0, "det = %f, inverse of matrix not possible");
		return NULL;
	}
	det = 1.0 / det;

	D->m[0][0] = (M->m[1][1] * M->m[2][2] - M->m[1][2] * M->m[2][1] ) * det;
	D->m[1][0] = (M->m[1][0] * M->m[2][2] - M->m[1][2] * M->m[2][0] ) * det;
	D->m[2][0] = (M->m[1][0] * M->m[2][1] - M->m[1][1] * M->m[2][0] ) * det;
	D->m[0][1] = (M->m[0][1] * M->m[2][2] - M->m[0][2] * M->m[2][1] ) * det;
	D->m[1][1] = (M->m[0][0] * M->m[2][2] - M->m[0][2] * M->m[2][0] ) * det;
	D->m[2][1] = (M->m[0][0] * M->m[2][1] - M->m[0][1] * M->m[2][0] ) * det;
	D->m[0][2] = (M->m[0][1] * M->m[1][2] - M->m[0][2] * M->m[1][1] ) * det;
	D->m[1][2] = (M->m[0][0] * M->m[1][2] - M->m[0][2] * M->m[1][0] ) * det;
	D->m[2][2] = (M->m[0][0] * M->m[1][1] - M->m[0][1] * M->m[1][0] ) * det;

	matrix_t *A = viewport_adjoint_matrix( D );

	return A;
}

static	inline void		point_map( matrix_t *M, float x, float y, float *nx, float *ny)
{
	float w = M->m[2][0] * x + M->m[2][1] * y + M->m[2][2];

	if( w == 0.0 )
		w = 1.0;
	else
		w = 1.0 / w;

	*nx = (M->m[0][0] * x + M->m[0][1] * y + M->m[0][2] ) * w;
	*ny = (M->m[1][0] * x + M->m[1][1] * y + M->m[1][2] ) * w;
}



static matrix_t	*viewport_transform(
	float x1,
	float y1,
	float x2,
	float y2,
	float *coord )
{
	float sx=1.0,sy=1.0;

	if( (x2-x1) > 0.0 )
		sx = 1.0 / (x2-x1);
	if( (y2-y1) > 0.0 )
		sy = 1.0 / (y2-y1);

	matrix_t *H = viewport_matrix();
	viewport_find_transform( coord, H );

	matrix_t *I = viewport_identity_matrix();
	viewport_translate_matrix( I, x1, y1 );
	viewport_scale_matrix( I, sx,sy );
	matrix_t *R = viewport_multiply_matrix( H,I );
	free(I);
	free(H);
	return R;
}

static	void		viewport_draw_grid(int32_t w, int32_t h, uint8_t *grid, int32_t grid_size, uint8_t op0)
{
	int32_t j,k;	
	uint8_t op1 = 0xff - op0;
	for( j = 0; j < h; j ++ )
		for( k = 0;k < w ; k ++ )
			grid[j*w+k] = ((k%grid_size>1)?((j%grid_size>1)? op1 : op0 ) : op0 );
}

static int		viewport_configure( 
					viewport_t *v,
					float x1, float y1,
					float x2, float y2,
					float x3, float y3,
					float x4, float y4,
					int32_t x,  int32_t y,
					int32_t w,  int32_t h,
					uint32_t reverse,
					uint8_t  color,
					int size)
{
	v->grid_size = size;

	v->points[X0] = (float) x + x1 * (float) w / 100.0;
	v->points[Y0] = (float) y + y1 * (float) h / 100.0;

	v->points[X1] = (float) x + x2 * (float) w / 100.0;
	v->points[Y1] = (float) y + y2 * (float) h / 100.0;

	v->points[X2] = (float) x + x3 * (float) w / 100.0;
	v->points[Y2] = (float) y + y3 * (float) h / 100.0;

	v->points[X3] = (float) x + x4 * (float) w / 100.0;
	v->points[Y3] = (float) y + y4 * (float) h / 100.0;

	v->w = w;
	v->x = x;
	v->h = h;
	v->y = y;

	v->grid_size = size;
	v->grid_val  = color;

	v->x1 = x1;
	v->x2 = x2;
	v->x3 = x3;
	v->x4 = x4;
	v->y1 = y1;
	v->y2 = y2;
	v->y3 = y3;
	v->y4 = y4;
	v->user_reverse = reverse;

	float tmp = v->points[X3];
	v->points[X3] = v->points[X2];
	v->points[X2] = tmp;
	tmp = v->points[Y3];
	v->points[Y3] = v->points[Y2];
	v->points[Y2] = tmp;

	matrix_t *m = viewport_transform( x, y, x + w, x + h, v->points );

	if ( reverse )
	{
		v->m = viewport_matrix();
		viewport_copy_from( v->m, m );
		matrix_t *im = viewport_matrix();
		v->M = viewport_invert_matrix( v->m, im );
		if(!v->M)
		{
			free(im);
			free(v->m);
			return 0;
		}
		free(im);
		viewport_prepare_process( v );
		return 1;

	}
	else
	{
		matrix_t *tmp = viewport_matrix();
		matrix_t *im = viewport_invert_matrix( m, tmp );
		free(tmp);
		if(!im)
		{
			free(m);
			return 0;
		}
		v->M = m;
		v->m = im;
		viewport_prepare_process( v );
		return 1;
	}

	return 0;
}

static void		viewport_process( viewport_t *p )
{
	const int32_t w = p->w;
	const int32_t h = p->h;
	const int32_t X = p->x;
	const int32_t Y = p->y;

	matrix_t *M = p->M;
	matrix_t *m = p->m;

	const int len = w * h;
	const float xinc = m->m[0][0];
	const float yinc = m->m[1][0];
	const float winc = m->m[2][0];

	const	int32_t	tx1 = p->tx1;
	const	int32_t tx2 = p->tx2;
	const	int32_t	ty1 = p->ty1;
	const	int32_t ty2 = p->ty2;

	const	float	m01 = m->m[0][1];
	const	float	m11 = m->m[1][1];
	const	float	m21 = m->m[2][1];
	const	float	m02 = m->m[0][2];
	const 	float	m12 = m->m[1][2];
	const 	float	m22 = m->m[2][2];

	float tx,ty,tw;
	float ttx,tty;
	int32_t x,y;
	int32_t itx,ity;

	int32_t *map = p->map;

	for( y = ty1; y < ty2; y ++ )
	{
		tx = xinc * ( tx1 + 0.5 ) + m01 * ( y + 0.5) + m02;
		ty = yinc * ( tx1 + 0.5 ) + m11 * ( y + 0.5) + m12;
		tw = winc * ( tx1 + 0.5 ) + m21 * ( y + 0.5) + m22;

		for( x = tx1; x < tx2 ; x ++ )
		{
			if( tw == 0.0 )	{
				ttx = 0.0;
				tty = 0.0;
			} else if ( tw != 1.0 ) {	
				ttx = tx / tw;
				tty = ty / tw;
			} else	{
				ttx = tx;
				tty = ty;
			}

			itx = (int32_t) ttx;
			ity = (int32_t) tty;

			if( itx >= X && itx <= w && ity >= Y && ity < h )
				map[ (y * w + x) ] = (ity * w + itx);
			else
				map[ (y * w + x) ] = (len+1);

			tx += xinc;
			ty += yinc;
			tw += winc;
		}
	}

	
}

static	void	viewport_prepare_process( viewport_t *v )
{
	const int32_t w = v->w;
	const int32_t h = v->h;
	const int32_t X = v->x;
	const int32_t Y = v->y;
	float dx1,dx2,dx3,dx4,dy1,dy2,dy3,dy4;
	matrix_t *M = v->M;

	point_map( M, v->x, v->y, &dx1, &dy1);
	point_map( M, v->x + w, v->y, &dx2, &dy2 );
	point_map( M, v->x, v->y + h, &dx4, &dy4 );
	point_map( M, v->x + w, v->y + h, &dx3, &dy3 );

	v->tx1 = round1( min4( dx1, dx2, dx3, dx4 ) );
	v->ty1 = round1( min4( dy1, dy2, dy3, dy4 ) );
	v->tx2 = round1( max4( dx1, dx2, dx3, dx4 ) );	
	v->ty2 = round1( max4( dy1, dy2, dy3, dy4 ) );

	clamp1( v->ty1 , Y, h-1 );
	clamp1( v->ty2 , Y, h-1 );
	clamp1( v->tx1, X, w );
	clamp1( v->tx2, X, w );

}

void		viewport_process_dynamic( void *data, uint8_t *in[3], uint8_t *out[3] )
{
	viewport_t *v = (viewport_t*) data;
	const int32_t w = v->w;
	const int32_t h = v->h;
	const int32_t X = v->x;
	const int32_t Y = v->y;
	matrix_t *M = v->M;
	matrix_t *m = v->m;

	const 	float xinc = m->m[0][0];
	const 	float yinc = m->m[1][0];
	const 	float winc = m->m[2][0];
	const	int32_t	tx1 = v->tx1;
	const	int32_t tx2 = v->tx2;
	const	int32_t	ty1 = v->ty1;
	const	int32_t ty2 = v->ty2;

	const	float	m01 = m->m[0][1];
	const	float	m11 = m->m[1][1];
	const	float	m21 = m->m[2][1];
	const	float	m02 = m->m[0][2];
	const 	float	m12 = m->m[1][2];
	const 	float	m22 = m->m[2][2];

	const	uint8_t	*inY	= in[0];
	const	uint8_t *inU	= in[1];
	const	uint8_t *inV	= in[2];

	uint8_t		*outY	= out[0];
	uint8_t		*outU	= out[1];
	uint8_t		*outV	= out[2];

	float tx,ty,tw;
	float ttx,tty;
	int32_t x,y;
	int32_t itx,ity;


	for( y =0 ; y < ty1; y ++ )
	{
		for( x = 0; x < tx1; x ++ )
		{
			outY[(y*w+x)] = 0;	
			outU[(y*w+x)] = 128;
			outV[(y*w+x)] = 128;
		}
		for( x = tx2; x < w; x ++ )
		{
			outY[(y*w+x)] = 0;	
			outU[(y*w+x)] = 128;
			outV[(y*w+x)] = 128;
		}
	}

	for( y = ty1; y < ty2; y ++ )
	{
		tx = xinc * ( tx1 + 0.5 ) + m01 * ( y + 0.5) + m02;
		ty = yinc * ( tx1 + 0.5 ) + m11 * ( y + 0.5) + m12;
		tw = winc * ( tx1 + 0.5 ) + m21 * ( y + 0.5) + m22;
		for( x = 0; x < tx1; x ++ )
		{
			outY[(y*w+x)] = 0;	
			outU[(y*w+x)] = 128;
			outV[(y*w+x)] = 128;
		}

		for( x = tx1; x < tx2 ; x ++ )
		{
			if( tw == 0.0 )	{
				ttx = 0.0;
				tty = 0.0;
			} else if ( tw != 1.0 ) {	
				ttx = tx / tw;
				tty = ty / tw;
			} else	{
				ttx = tx;
				tty = ty;
			}

			itx = (int32_t) ttx;
			ity = (int32_t) tty;

			if( itx >= X && itx <= w && ity >= Y && ity < h )
			{
				outY[(y*w+x)] = inY[(ity*w+itx)];
				outU[(y*w+x)] = inU[(ity*w+itx)];
				outV[(y*w+x)] = inV[(ity*w+itx)];
			}
			else
			{
				outY[(y*w+x)] = 0;
				outU[(y*w+x)] = 128;
				outV[(y*w+x)] = 128;

			}

			tx += xinc;
			ty += yinc;
			tw += winc;
		}
		for( x = tx2; x < w; x ++ )
		{
			outY[(y*w+x)] = 0;	
			outU[(y*w+x)] = 128;
			outV[(y*w+x)] = 128;
		}

	}
	for( y = ty2 ; y < h; y ++ )
	{
		for( x = 0; x < tx1; x ++ )
		{
			outY[(y*w+x)] = 0;	
			outU[(y*w+x)] = 128;
			outV[(y*w+x)] = 128;
		}
		for( x = tx2; x < w; x ++ )
		{
			outY[(y*w+x)] = 0;	
			outU[(y*w+x)] = 128;
			outV[(y*w+x)] = 128;
		}
	}

	
}


void			viewport_destroy( void *data )
{
	viewport_t *v = (viewport_t*)data;
	if( v )
	{
		if( v->M ) free( v->M );
		if( v->m ) free( v->m );
		if( v->grid) free( v->grid );
		if( v->map ) free( v->map );
		if( v->help ) free( v->help );
		if( v->homedir) free(v->homedir);
		free(v);
	}
	v = NULL;
}

static	void		viewport_update_grid( viewport_t *v, int size, uint8_t val )
{
	v->grid_size = size;
	v->grid_val  = val;

	viewport_draw_grid( v->w, v->h, v->grid, v->grid_size, v->grid_val );
}

static	void		viewport_update_perspective( viewport_t *v, float *values )
{
	int res = viewport_configure (v, v->x1, v->y1,
					 v->x2, v->y2,
					 v->x3, v->y3,
					 v->x4, v->y4,	
					     0,0,
					 v->w,  v->h,
					 v->user_reverse,
					 v->grid_val,
					 v->grid_size );


	if(! res )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Viewport: Invalid quadrilateral. Trying to fallback");

		v->x1 = values[0]; v->x2 = values[2]; v->x3 = values[4]; v->x4 = values[6];
		v->y1 = values[1]; v->y2 = values[3]; v->y3 = values[5]; v->y4 = values[7];

		if(!viewport_configure( v, v->x1, v->y1, v->x2, v->y2, v->x3, v->y3,v->x4,v->y4,
				0,0,v->w,v->h, v->user_reverse, v->grid_val,v->grid_size ));
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to configure the viewport");
			veejay_msg(VEEJAY_MSG_ERROR, "If you are using a preset-configuration, see ~/.veejay/viewport.cfg");
			v->disable = 1;
			return;
		}
	}


	// Clear map
	const int len = v->w * v->h;
	int k;
	for( k = 0 ; k < len ; k ++ )
		v->map[k] = len+1;
	
	v->disable = 0;

	// Update map
	viewport_process( v );
}


void *viewport_init(int w, int h, const char *homedir, int *enable, int *frontback, int mode)
{
	//@ try to load last saved settings
	viewport_config_t *vc = viewport_load_settings( homedir,mode );
	if(!vc)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No or invalid viewport configuration file in %s", homedir );
		veejay_msg(VEEJAY_MSG_ERROR, "Using default values");
	}

	viewport_t *v = (viewport_t*) vj_calloc(sizeof(viewport_t));

	v->homedir = strdup(homedir);
	v->mode	   = mode;
	int res;

	if( vc == NULL )
	{
		res = viewport_configure (v, 16.0, 16.0,
					 90.0, 16.0,
					 90.0,90.0,
					  10.0,90.0,
					    0,0,
					    w,h,
					    0,
					    0xff,
					    w/32 );

		*enable = 0;
		*frontback = 1;
		v->user_ui = 1;

	}
	else
	{
		res = viewport_configure( v, 	vc->x1, vc->y1,
					     	vc->x2, vc->y2,
						vc->x3, vc->y3,
					 	vc->x4, vc->y4,
						0,0,			
						w,h,
						vc->reverse,
						vc->grid_color,
						vc->grid_size );

		*enable = 1;
		*frontback = vc->frontback;
		v->user_ui = 0;

		free( vc );
	}


	if(! res )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid point locations");
		viewport_destroy( v );
		return NULL;
	}

	// Allocate memory for map
	v->map = (int32_t*) vj_malloc(sizeof(int32_t) * (v->w * v->h + v->w) );

	const int len = v->w * v->h;
	int k;
	for( k = 0 ; k < len ; k ++ )
		v->map[k] = len+1;

	// Allocate memory for grid
	v->grid = (uint8_t*) vj_malloc( len + v->w );
	
	//draw grid
	viewport_draw_grid( v->w, v->h, v->grid, v->grid_size, v->grid_val );

	// calculate initial view
	viewport_process( v );


    	return (void*)v;
}

int	 viewport_active( void *data )
{
	viewport_t *v = (viewport_t*) data;
	return v->user_ui;
}

char	*viewport_get_help(void *data)
{
	viewport_t *v = (viewport_t*)data;
	return v->help;
}

static	viewport_config_t 	*viewport_load_settings( const char *dir, int mode )
{
	viewport_config_t *vc = vj_calloc(sizeof(viewport_config_t));

	char path[1024];
	if(!mode)
		sprintf(path, "%s/viewport-mapping.cfg", dir);
	else
		sprintf(path, "%s/viewport-projection.cfg", dir);
	FILE *fd = fopen( path, "r" );
	if(!fd)
	{
		free(vc);
		return NULL;
	}
	fseek(fd,0,SEEK_END );
	unsigned int len = ftell( fd );
		
	if( len <= 0 )
	{
		free(vc);
		return NULL;
	}

	char *buf = vj_calloc( (len+1) );

	rewind( fd );
	fread( buf, len, 1 , fd);

	fclose(fd );

	int n = sscanf(buf, "%f %f %f %f %f %f %f %f %d %d %d %d",
			&vc->x1, &vc->y1,
			&vc->x2, &vc->y2,
			&vc->x3, &vc->y3,
			&vc->x4, &vc->y4,
			&vc->reverse,
			&vc->grid_size,
			&vc->grid_color,
			&vc->frontback);

	if( n != 12 )
	{
		veejay_msg(0, "Unable to read %s (file is %d bytes)",path, len );
		free(vc);
		free(buf);
		return NULL;
	}

	free(buf);
	veejay_msg(VEEJAY_MSG_INFO, "Viewport configuration:");
	veejay_msg(VEEJAY_MSG_INFO, "\tBehaviour:\t%s", (vc->reverse ? "Forward" : "Projection") );
	veejay_msg(VEEJAY_MSG_INFO, "\tQuad     :\t(1) %fx%f (2) %fx%f", vc->x1,vc->y1,vc->x2,vc->y2);
	veejay_msg(VEEJAY_MSG_INFO, "\t         :\t(3) %fx%f (4) %fx%f", vc->x2,vc->y2,vc->x3,vc->y3);
	veejay_msg(VEEJAY_MSG_INFO, "\tGrid     :\t%dx%d", vc->grid_size, vc->grid_size);
	veejay_msg(VEEJAY_MSG_INFO, "\tPencil   :\t%s", (vc->grid_color == 0xff ? "white" : "black" ) );

	return vc;
}

static	void	viewport_save_settings( viewport_t *v, int frontback )
{
	char path[1024];
	if( !v->mode )
		sprintf(path, "%s/viewport-mapping.cfg", v->homedir );
	else
		sprintf(path, "%s/viewport-projection.cfg", v->homedir );

	FILE *fd = fopen( path, "wb" );

	if(!fd)
	{
		veejay_msg(0, "Unable to open '%s' for writing. Cannot save viewport settings",
			path );
		return;
	}

	char content[512];

	sprintf( content, "%f %f %f %f %f %f %f %f %d %d %d %d\n",
			v->x1,v->y1,v->x2,v->y2,
			v->x3,v->y3,v->x4,v->y4,
			v->user_reverse,
			v->grid_size,
			v->grid_val,
			frontback );

	int res = fwrite( content, strlen(content), 1, fd );

	if( res <= 0 )
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to save viewport settings to %s", path );

	fclose( fd );

	veejay_msg(VEEJAY_MSG_DEBUG, "Saved viewport settings to %s", path);
}

void	viewport_external_mouse( void *data, int sx, int sy, int button, int frontback )
{
	if( sx == 0 && sy == 0 && button == 0 )
		return;

	viewport_t *v = (viewport_t*) data;

	int osd = 0;
	int grid =0;
	int ch = 0;
	int width = v->w;
	int height = v->h;
	int point = -1;
	int i;

	float x = (float)sx / ( width / 100.0f );
	float y = (float)sy / ( height / 100.0f );
	double dist = 100.0;

	float p[9];

	// make a copy of the parameters

	p[0] = v->x1;
	p[2] = v->x2;
	p[4] = v->x3;
	p[6] = v->x4;
	p[1] = v->y1;
	p[3] = v->y2;
	p[5] = v->y3;	
	p[7] = v->y4;

	for( i = 0; i < 4 ; i ++ )
		v->users[ i ]  = 1;

	if( v->user_ui )
	{
		double dt[4];
		dt[0] = sqrt( (p[0] - x) * (p[0] - x) + ( p[1] - y ) * (p[1] -y ) );
		dt[1] = sqrt( (p[2] - x) * (p[2] - x) + ( p[3] - y ) * (p[3] -y ) );
		dt[2] = sqrt( (p[4] - x) * (p[4] - x) + ( p[5] - y ) * (p[5] -y ) );
		dt[3] = sqrt( (p[6] - x) * (p[6] - x) + ( p[7] - y ) * (p[7] -y ) );
	
		for ( i = 0; i < 4;  i ++ )
		{
			if( dt[i] < dist )
			{
				dist = dt[i];
				point = i;
			}	
		}
	}
	
	v->save = 0;

	if( ( button == 6 || button == 1) && point >= 0 )
	{
		v->save = 1;
	}

	if( button == 0 && point >= 0)
	{
		v->users[ point ] = 2;
	}

	if( button == 0 )
	{
		v->usermouse[0] = x;
		v->usermouse[1] = y;
		osd = 1;
	}

	if( button == 2 )
	{
		if(v->user_reverse) v->user_reverse = 0; else v->user_reverse = 1;
		osd = 1;
		ch  = 1;
	}

	if( button == 3 )
	{
		if(v->user_ui) v->user_ui = 0; else v->user_ui = 1;
		osd = 1;

		if( v->user_ui == 0 )
		{
			viewport_save_settings(v, frontback);
		}
	}

	if( button == 4 ) // wheel up
	{
		if( v->user_ui )
		{
			if(v->grid_size <= 8 )
				v->grid_size = 8;
			else
				v->grid_size -=2;
			grid = 1;
			osd = 1;
		}
	}
	if (button == 5 ) // wheel down
	{	
		if( v->user_ui )
		{
			if( v->grid_size > ( width / 4 ) )
				v->grid_size = width/4;
			else
				v->grid_size +=2;
			grid = 1;
			osd = 1;
		}
	}

	if( button == 7 )
	{
		if( v->grid_val == 0xff )
			v->grid_val = 0;
		else
			v->grid_val = 0xff;
		grid = 1;
	}

	if( grid )	
		viewport_update_grid( v, v->grid_size, v->grid_val );

	if( osd )
		viewport_update_context_help(v);

	if(v->save)
	{
		if( button == 6 )
		{	//@ Snap selected point to grid (upper left corner)
			float rx = v->w / 100.0 * x;
			float ry = v->h / 100.0 * y;
			int   dx = (rx+0.5) / v->grid_size;
			int   dy = (ry+0.5) / v->grid_size;

			x = (float) ( dx * v->grid_size / ( v->w / 100.0 ) );
			y = (float) ( dy * v->grid_size / ( v->h / 100.0 ) );
		}

		switch( point )
		{
			case 0:
				v->x1 = x;
				v->y1 = y;
				break;
			case 1:
				v->x2 = x;
				v->y2 = y;
				break;
			case 2:
				v->x3 = x;
				v->y3 = y;
				break;
			case 3:
				v->x4 = x;
				v->y4 = y;
				break;
		}
		ch = 1;

	}

	if( ch )
		viewport_update_perspective( v, p );
	
}

static	inline	int	grab_pixel( uint8_t *plane, int x, int y, int w )
{
	if( plane[ (y*w) + x ] > 128 )
		return 0;
	return 255;
}

static void	viewport_draw( void *data, uint8_t *plane )
{
	viewport_t *v = (viewport_t*) data;
	int	width = v->w;
	int 	height = v->h;

	float wx =(float) v->w / 100.0;
	float wy =(float) v->h / 100.0;

	int fx1 = (int)( v->x1 *wx );
	int fy1 = (int)( v->y1 *wy );
	int fx2 = (int)( v->x2 *wx );
	int fy2 = (int)( v->y2 *wy );
	int fx3 = (int)( v->x3 *wx );
	int fy3 = (int)( v->y3 *wy );
	int fx4 = (int)( v->x4 *wx );
	int fy4 = (int)( v->y4 *wy );

	opacity_blend_luma_apply( plane,v->grid, (width*height), 100 );

	viewport_line( plane, fx1, fy1, fx2,fy2,width,height, v->grid_val);
	viewport_line( plane, fx1, fy1, fx4,fy4,width,height, v->grid_val );
	viewport_line( plane, fx4, fy4, fx3,fy3,width,height, v->grid_val );
	viewport_line( plane, fx2, fy2, fx3,fy3,width,height, v->grid_val );

	 draw_point( plane, fx1,fy1, width,height, v->users[0],v->grid_val );
	 draw_point( plane, fx2,fy2, width,height, v->users[1],v->grid_val );
	 draw_point( plane, fx3,fy3, width,height, v->users[2],v->grid_val );
	 draw_point( plane, fx4,fy4, width,height, v->users[3],v->grid_val );

	 int mx = v->usermouse[0] * wx;
	 int my = v->usermouse[1] * wy;
	 
	 if( mx >= 0 && my >= 0 && mx <= width && my < height )
	 {
		 int col = grab_pixel( plane, v->usermouse[0]*wx, v->usermouse[1]*wy,width );
		 draw_point( plane, v->usermouse[0]*wx,v->usermouse[1]*wy, width,height,1, v->grid_val );
	 }
}

int	viewport_render_ssm(void *vdata )
{
	viewport_t *v = (viewport_t*) vdata;

	if( v->disable || v->user_ui) 
		return 0;

	return 1;
}

void viewport_render( void *vdata, uint8_t *in[3], uint8_t *out[3],int width, int height, int uv_len )
{
	viewport_t *v = (viewport_t*) vdata;

	if( v->disable ) 
		return;

	int len = (width * height);

	if(! v->user_ui )
	{
		const int len = v->w * v->h;
		const int w = v->w;
		register uint32_t i,j,n;
		const int32_t *map = v->map;
		uint8_t *inY  = in[0];
		uint8_t *inU  = in[1];
		uint8_t *inV  = in[2];
		uint8_t       *outY = out[0];
		uint8_t	      *outU = out[1];
	        uint8_t	      *outV = out[2];

		inY[len+1] = 0;
		inU[len+1] = 128;
		inV[len+1] = 128;

		for( i = 0; i < len ; i += v->w )
		{
			for( j = 0; j < w; j ++ )
			{
				n = map[i+j];
				outY[i+j] = inY[n];
				outU[i+j] = inU[n];
				outV[i+j] = inV[n];
			}
		}
	}
	else
	{
		viewport_draw( v, in[0] );
		veejay_memset( in[1], 128, uv_len );
		veejay_memset( in[2], 128, uv_len );
	}
}
void viewport_render_dynamic( void *vdata, uint8_t *in[3], uint8_t *out[3],int width, int height )
{
	viewport_t *v = (viewport_t*) vdata;

	viewport_process_dynamic( v, in,out );

}

void *viewport_fx_init(int type, int wid, int hei, int x, int y, int zoom)
{
	viewport_t *v = (viewport_t*) vj_calloc(sizeof(viewport_t));

	float fracx = (float) wid;
	float fracy = (float) hei;

	fracx *= 0.01f;
	fracy *= 0.01f;

	if( type == VP_QUADZOOM )
	{
		float cx = (float) x;
		float cy = (float) y;

		cx = cx / fracx;
		cy = cy / fracy;

		float  w = 1.0 * zoom * 0.5; 
		float  h = 1.0 * zoom * 0.5;

		v->x1 = cx - w;
		v->y1 = cy - h;
		v->x2 = cx + w;
		v->y2 = cy - h;
		v->x3 = cx + w;
		v->y3 = cy + h;
		v->x4 = cx - w;
		v->y4 = cy + h;
	}

	int res = viewport_configure (v, 
			v->x1, v->y1,
			v->x2, v->y2,
			v->x3, v->y3,
			v->x4, v->y4,
			0,0,
			wid,hei,
			1,
			0xff,
			wid/32 );

	v->user_ui = 0;

	if(! res )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid point locations");
		viewport_destroy( v );
		return NULL;
	}


    	return (void*)v;
}

