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
 * Resources:
 *	Gimp 1.0,2.0   (Perspective transformation (C) Spencer Kimball & Peter Mattis)
 *	Cinelerra      (Motion plugin, no author in any file present. GPL2).
 *	Xine           (bresenham line drawing routine)
 *
 */
#include <config.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>
#include <veejay/vj-viewport.h>
#include <libvje/effects/opacity.h>
#include <libvjmem/vjmem.h>

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
	float m[4][4];
} matrix_t;

typedef struct
{
	int32_t x,y;
	int32_t h,w;
	int32_t x0,y0,w0,h0;
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
	int   snap_marker;
	int   marker_size;
	float x1;
	float x2;
	float x3;
	float x4;
	float y1;
	float y2;
	float y3;
	float y4;
	int32_t *map;
	uint8_t *img[4];
	matrix_t *M;
	matrix_t *m;
	char *help;
	uint8_t *grid;
	uint8_t  grid_val;
	int	parameters[8];
	char    *homedir;
	int32_t tx1,tx2,ty1,ty2;
	int32_t ttx1,ttx2,tty1,tty2;
	int	mode;
	int32_t 	*buf;
} viewport_t;

typedef struct
{
	int	reverse;
	int	grid_size;
	int	grid_color;
	int	frontback;
	int	x0,y0,w0,h0;
	float x1;
	float x2;
	float x3;
	float x4;
	float y1;
	float y2;
	float y3;
	float y4;
} viewport_config_t;

static void		viewport_draw_col( void *data, uint8_t *img, uint8_t *u, uint8_t *v );
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
					int32_t x0,  int32_t y0,
					int32_t w0,  int32_t h0,
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
void 		viewport_line (uint8_t *plane,int x1, int y1, int x2, int y2, int w, int h, uint8_t col);
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

void viewport_line (uint8_t *plane,
		      int x1, int y1, int x2, int y2, int w, int h, uint8_t col) {

  uint8_t *c;
  int dx, dy, t, inc, d, inc1, inc2;
  int swap_x = 0;
  int swap_y = 0;

  if( x1 < 0 ) x1 = 0; else if (x1 > w ) x1 = w;
  if( y1 < 0 ) y1 = 0; else if (y1 > h ) y1 = h;
  if( x2 < 0 ) x2 = 0; else if (x2 > w ) x2 = w;
  if( y2 < 0 ) y2 = 0; else if (y2 > h ) y2 = h;

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
		sprintf(tmp, "Mouse Left: Find center of blob\nMouse Left + SHIFT: Set point\nMouse Left + ALTGr: Set projection quad\nMouse Right: %s\nMouse Middle: %s\nMouse Middle + SHIFT: Line Color\nMouse Wheel: Marker size\nMouse Wheel + ALTGr: Scale projection quad\nMouse Wheel + CTRL: Scale camera and projection quad\nCTRL + h:Hide/Show this Help",
			reverse_mode, render_mode);
	}
	else
		sprintf(tmp, "Mouse Right = %s\nMouse Middle = %s\nCTRL + h = Hide/Show this Help\nMouse Wheel + CTRL = Scale quads\nMouse Wheel + ALTGr = Scale projection area", reverse_mode, render_mode );

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
			sprintf(hlp, "Interactive Camera/Projection setup\n%s\n(1)  %.2fx%.2f    Pos: %.2fx%.2f\n(2) %.2fx%.2f\n(3) %.2fx%.2f\n(4) %.2fx%.2f\n",
                        tmp,v->x1,v->y1, v->usermouse[0],v->usermouse[1],
                        v->x2,v->y2,v->x3,v->y3,v->x4,v->y4 );
		else
			sprintf(hlp, "Interactive Camera/Projection\nPerspective Transform %s\n%s", reverse_mode, tmp );

	}

	if(v->help)
		free(v->help);
	v->help = strdup( hlp );
}

char *viewport_get_my_help(viewport_t *v)
{
	char render_mode[32];
	sprintf(render_mode, "%s", ( v->user_ui == 0 ? "Grid Mode" : "Render Mode" ) );
	char reverse_mode[32];
	sprintf(reverse_mode, "%s", ( v->user_reverse ? "Forward"  : "Reverse" ) );
	char tmp[1024];
	char hlp[1024];

	if( v->user_ui )
	{
		sprintf(tmp, "Mouse Left: Find center of blob\nMouse Left + LSHIFT: Set point\nMouse Left + RSHIFT: Set projection quad\nMouse Right: %s\nMouse Middle: %s\nMouse Middle + LSHIFT: Line Color\nMouse Wheel: Marker size (%dx%d)\nMouse Wheel + RSHIFT:Scale projection quad\nMouse Wheel + CTRL: Scale camera and projection quad\nCTRL + h:Hide/Show this Help",
			reverse_mode, render_mode , v->marker_size,v->marker_size);
	}
	else
		sprintf(tmp, "Mouse Right = %s\nMouse Middle = %s\nCTRL + h = Hide/Show this Help\nMouse Wheel + CTRL = Scale quads\nMouse Wheel + ALTGr = Scale projection area", reverse_mode, render_mode );

	if(v->mode == 0 )
	{
		if( v->user_ui )
			sprintf(hlp, "Viewport\nPerspective Transform\n%s\n",tmp );
		else
			sprintf(hlp, "Viewport\nPerspective Transform %s\n%s",
			reverse_mode, tmp );
	}
	else
	{
		if(v->user_ui )
			sprintf(hlp, "Interactive Camera/Projection setup\n%s",tmp );
		else
			sprintf(hlp, "Interactive Camera/Projection\nPerspective Transform %s\n%s", reverse_mode, tmp );

	}

	return strdup( hlp );
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
	viewport_translate_matrix( I, -x1, -y1 );
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

void	viewport_get_projection_coords( void *data, int32_t *x0, int32_t *y0, int32_t *w0, int32_t *h0 )
{
	viewport_t *v = (viewport_t*) data;

	*x0 = v->x0;
	*y0 = v->y0;
	*w0 = v->w0;
	*h0 = v->h0;
}

float	*viewport_get_projection_points( void *data )
{
	viewport_t *v = (viewport_t*) data;
	float *res = vj_malloc( sizeof(float) * 8 );

	res[0] = v->x0;
	res[1] = v->y0;
	res[2] = v->x0 + v->w0;
	res[3] = v->y0;
	res[4] = v->x0;
	res[5] = v->y0 + v->h0;
	res[6] = v->x0 + v->w0;
	res[7] = v->y0 + v->h0;

	return res;
}

void	viewport_set_projection( void *data, float *res )
{
	viewport_t *v = (viewport_t*) data;
	v->x1 = res[0];
	v->y1 = res[1];
	v->x2 = res[2];
	v->y2 = res[3];
	v->x3 = res[4];
	v->y3 = res[5];
	v->x4 = res[6];
	v->y4 = res[7];
	
}

static int		viewport_configure( 
					viewport_t *v,
					float x1, float y1, /* output */
					float x2, float y2,
					float x3, float y3,
					float x4, float y4,
					int32_t x0,  int32_t y0, /* input */
					int32_t w0,  int32_t h0,
					int32_t wid,  int32_t hei,
					uint32_t reverse,
					uint8_t  color,
					int size)
{
	//FIXME
	int w = wid, h = hei;

	v->grid_size = size;

	
	v->points[X0] = (float) x1 * (float) w / 100.0;
	v->points[Y0] = (float) y1 * (float) h / 100.0;

	v->points[X1] = (float) x2 * (float) w / 100.0;
	v->points[Y1] = (float) y2 * (float) h / 100.0;

	v->points[X2] = (float) x3 * (float) w / 100.0;
	v->points[Y2] = (float) y3 * (float) h / 100.0;

	v->points[X3] = (float) x4 * (float) w / 100.0;
	v->points[Y3] = (float) y4 * (float) h / 100.0;
	
	v->w = wid; /* image plane boundaries */
	v->x = 0;
	v->h = hei;
	v->y = 0;

	v->x0 = x0; 
	v->y0 = y0;
	v->w0 = w0;
	v->h0 = h0;

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

	matrix_t *m = viewport_transform( x0, y0, x0 + w0, y0 + h0, v->points );
	
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
	const int32_t X = p->x0;
	const int32_t Y = p->y0;
	const int32_t W0 = p->w0;
	const int32_t H0 = p->h0;

	matrix_t *M = p->M;
	matrix_t *m = p->m;

	const int len = w * h;
	const float xinc = m->m[0][0];
	const float yinc = m->m[1][0];
	const float winc = m->m[2][0];

	const	int32_t	tx1 = p->ttx1;
	const	int32_t tx2 = p->ttx2;
	const	int32_t	ty1 = p->tty1;
	const	int32_t ty2 = p->tty2;

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
	register int32_t pos = 0;

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

			pos = ity * w + itx;

			if( pos >= 0 && pos < len )
				map[ (y * w + x) ] = pos;
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

	const int32_t X = v->x0;
	const int32_t Y = v->y0;

	float dx1,dx2,dx3,dx4,dy1,dy2,dy3,dy4;
	matrix_t *M = v->M;

	point_map( M, v->x, v->y, &dx1, &dy1);
	point_map( M, v->x + v->w, v->y, &dx2, &dy2 );
	point_map( M, v->x, v->y + v->h, &dx4, &dy4 );
	point_map( M, v->x + v->w, v->y + v->h, &dx3, &dy3 );
	
	v->tx1 = round1( min4( dx1, dx2, dx3, dx4 ) );
	v->ty1 = round1( min4( dy1, dy2, dy3, dy4 ) );
	v->tx2 = round1( max4( dx1, dx2, dx3, dx4 ) );	
	v->ty2 = round1( max4( dy1, dy2, dy3, dy4 ) );
	
	clamp1( v->ty1 , Y, Y + v->h0 );
	clamp1( v->ty2 ,Y,Y + v->h0 );
	clamp1( v->tx1, X, X + v->w0 );
	clamp1( v->tx2, X, X + v->w0 );

	v->ttx2 = v->tx2;
	v->tty2 = v->ty2;
	v->ttx1 = v->tx1;
	v->tty1 = v->ty1;

	clamp1( v->ttx2,0, v->w );	
	clamp1( v->tty2,0, v->h );
	clamp1( v->ttx1,0, v->w );	
	clamp1( v->tty1,0, v->h );
	
}


void		viewport_process_dynamic_map( void *data, uint8_t *in[3], uint8_t *out[3], uint32_t *map, int feather )
{
	viewport_t *v = (viewport_t*) data;
	const int32_t w = v->w;
	const int32_t h = v->h;
	const int32_t X = v->x0;
	const int32_t Y = v->y0;
	const int32_t W0 = v->w0;
	const int32_t H0 = v->h0;
	matrix_t *M = v->M;
	matrix_t *m = v->m;

	const 	float xinc = m->m[0][0];
	const 	float yinc = m->m[1][0];
	const 	float winc = m->m[2][0];
	const	int32_t	tx1 = v->ttx1;
	const	int32_t tx2 = v->ttx2;
	const	int32_t	ty1 = v->tty1;
	const	int32_t ty2 = v->tty2;

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
/*
#if defined (HAVE_ASM_MMX) || defined (HAVE_AMS_SSE ) 

	fast_memset_dirty( outY , 0, ty1 * v->w );
	fast_memset_dirty( outU , 128, ty1 * v->w );
	fast_memset_dirty( outV , 128, ty1 * v->w );
	fast_memset_finish();
#else

	for( y =0 ; y < ty1; y ++ )
	{
		for( x = 0 ; x < w ; x ++ )
		{
			outY[ (y * w +x ) ] = 0;
			outU[ (y * w +x ) ] = 128;
			outV[ (y * w +x ) ] = 128;
		}
	}
#endif
*/
	for( y = ty1; y < ty2; y ++ )
	{
		tx = xinc * ( tx1 + 0.5 ) + m01 * ( y + 0.5) + m02;
		ty = yinc * ( tx1 + 0.5 ) + m11 * ( y + 0.5) + m12;
		tw = winc * ( tx1 + 0.5 ) + m21 * ( y + 0.5) + m22;
	/*	for( x = 0; x < tx1; x ++ )
		{
			outY[(y*w+x)] = 0;	
			outU[(y*w+x)] = 128;
			outV[(y*w+x)] = 128;
		}*/

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

			if( itx >= X && itx <= w && ity >= Y && ity < h 
				&&
					map[( y * w + x)] >= feather )
			{
				outY[(y*w+x)] = inY[(ity*w+itx)];
				outU[(y*w+x)] = inU[(ity*w+itx)];
				outV[(y*w+x)] = inV[(ity*w+itx)];
			}
			/*else
			{
				outY[(y*w+x)] = 0;
				outU[(y*w+x)] = 128;
				outV[(y*w+x)] = 128;

			}*/

			tx += xinc;
			ty += yinc;
			tw += winc;
		}
		/*
		for( x = tx2; x < w; x ++ )
		{
			outY[(y*w+x)] = 0;	
			outU[(y*w+x)] = 128;
			outV[(y*w+x)] = 128;
		}*/

	}
/*
#if defined (HAVE_ASM_MMX) || defined (HAVE_AMS_SSE ) 
	int rest = h - ty2;
	fast_memset_dirty( outY + (ty2 * v->w),0, rest * v->w );
	fast_memset_dirty( outU + (ty2 * v->w), 128, rest * v->w );
	fast_memset_dirty( outV + (ty2 * v->w), 128, rest * v->w );
	fast_memset_finish();
#else
	for( y = ty2 ; y < h; y ++ )
	{
		for( x = 0; x < w; x ++ )
		{
			outY[(y*w+x)] = 0;	
			outU[(y*w+x)] = 128;
			outV[(y*w+x)] = 128;
		}			
	}
#endif	
*/
	
}
void		viewport_process_dynamic( void *data, uint8_t *in[3], uint8_t *out[3] )
{
	viewport_t *v = (viewport_t*) data;
	const int32_t w = v->w;
	const int32_t h = v->h;
	const int32_t X = v->x0;
	const int32_t Y = v->y0;
	matrix_t *M = v->M;
	matrix_t *m = v->m;

	const 	float xinc = m->m[0][0];
	const 	float yinc = m->m[1][0];
	const 	float winc = m->m[2][0];
	const	int32_t	tx1 = v->ttx1;
	const	int32_t tx2 = v->ttx2;
	const	int32_t	ty1 = v->tty1;
	const	int32_t ty2 = v->tty2;

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

#if defined (HAVE_ASM_MMX) || defined (HAVE_AMS_SSE ) 

	fast_memset_dirty( outY , 0, ty1 * v->w );
	fast_memset_dirty( outU , 128, ty1 * v->w );
	fast_memset_dirty( outV , 128, ty1 * v->w );
	fast_memset_finish();
#else
	
	for( y =0 ; y < ty1; y ++ )
	{
		for( x = 0 ; x < w ; x ++ )
		{
			outY[ (y * w +x ) ] = 0;
			outU[ (y * w +x ) ] = 128;
			outV[ (y * w +x ) ] = 128;
		}
	}
#endif
	
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

#if defined (HAVE_ASM_MMX) || defined (HAVE_AMS_SSE ) 
	int rest = h - ty2;
	fast_memset_dirty( outY + (ty2 * v->w),0, rest * v->w );
	fast_memset_dirty( outU + (ty2 * v->w), 128, rest * v->w );
	fast_memset_dirty( outV + (ty2 * v->w), 128, rest * v->w );
	fast_memset_finish();
#else
	for( y = ty2 ; y < h; y ++ )
	{
		for( x = 0; x < w; x ++ )
		{
			outY[(y*w+x)] = 0;	
			outU[(y*w+x)] = 128;
			outV[(y*w+x)] = 128;
		}			
	}
#endif	
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
		if( v->buf ) free(v->buf);
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
					 v->x0, v->y0,	
					 v->w0, v->h0,
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
				v->x0, v->y0, v->w0, v->h0,v->w,v->h, v->user_reverse, v->grid_val,v->grid_size ));
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


void *viewport_init(int x0, int y0, int w0, int h0, int w, int h, const char *homedir, int *enable, int *frontback, int mode )
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
	v->marker_size = 4;
	int res;

	if( vc == NULL )
	{
		res = viewport_configure (v, 16.0, 16.0,
					     90.0, 16.0,
					     90.0,90.0,
					     16.0,90.0,
					     x0,y0,w0,h0,
					     w,h,
					     1,
					     0xff,
					     w/16 );

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
						vc->x0, vc->y0,
						vc->w0, vc->h0,			
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
//	viewport_draw_grid( v->w, v->h, v->grid, v->grid_size, v->grid_val );

	// calculate initial view
	viewport_process( v );

	v->buf = vj_calloc( sizeof(int32_t) * 5000 );

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

	int n = sscanf(buf, "%f %f %f %f %f %f %f %f %d %d %d %d %d %d %d %d",
			&vc->x1, &vc->y1,
			&vc->x2, &vc->y2,
			&vc->x3, &vc->y3,
			&vc->x4, &vc->y4,
			&vc->reverse,
			&vc->grid_size,
			&vc->grid_color,
			&vc->x0,
			&vc->y0,
			&vc->w0,
			&vc->h0,
			&vc->frontback);

	if( n != 16 )
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

	sprintf( content, "%f %f %f %f %f %f %f %f %d %d %d %d %d %d %d %d\n",
			v->x1,v->y1,v->x2,v->y2,
			v->x3,v->y3,v->x4,v->y4,
			v->user_reverse,
			v->grid_size,
			v->grid_val,
			v->x0,
			v->y0,
			v->w0,
			v->h0,
			frontback );

	int res = fwrite( content, strlen(content), 1, fd );

	if( res <= 0 )
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to save viewport settings to %s", path );

	fclose( fd );

	veejay_msg(VEEJAY_MSG_DEBUG, "Saved viewport settings to %s", path);
}

static int	viewport_locate_marker( viewport_t *v, uint8_t *img, float fx, float fy , float *dx, float *dy )
{
	uint32_t x  = fx / 100.0f * v->w;
	uint32_t y  = fy / 100.0f * v->h;

	uint32_t x1 = x - v->marker_size;
	uint32_t y1 = y - v->marker_size;
	uint32_t x2 = x + v->marker_size;
	uint32_t y2 = y + v->marker_size;

	if( x1 < 0 ) x1 = 0; else if ( x1 > v->w ) x1 = v->w;
	if( y1 < 0 ) y1 = 0; else if ( y1 > v->h ) y1 = v->h;
	if( x2 < 0 ) x2 = 0; else if ( x2 > v->w ) x2 = v->w;
	if( y2 < 0 ) y2 = 0; else if ( y2 > v->h ) y2 = v->h;

	unsigned int i,j;
	uint32_t product_row = 0;
	uint32_t pixels_row = 0;
	uint32_t product_col = 0;
	uint32_t pixels_col = 0;
	uint32_t pixels_row_c = 0;
	uint32_t product_col_c = 0;

	unsigned long nc = 0, it  =0;
	uint8_t hist[256];
	uint8_t p0 = 0;
	int32_t ii=0,ji=0;
	veejay_memset(hist,0,sizeof(hist));

	// find average and most occuring pixel
	for( i = y1; i < y2; i ++ ) 
	{
		for( j = x1; j < x2 ; j ++ )
		{
			p0 = (img[i*v->w+j] >= 255 ? 0: img[i * v->w + j]);
			nc += p0;
			hist[ p0 ] ++;
			it ++;
		}
	}

	for( i =0 ;i < 256; i ++ )
	{
		if( hist[i] > ji )
		{
			ii = i;
			ji = hist[i];
		}
	}

	unsigned int avg = 0;
	if( nc > 0 )
		avg = (nc / it);

	int diff = abs( ii - avg );
	for( i = y1; i < y2; i ++ ) 
	{
		pixels_row = 0;
		for( j = x1; j < x2 ; j ++ )
		{
			if (abs(img[i * v->w + j] - diff)>= avg)
			{
				pixels_row++; 
			}
		}
		product_row += (i * pixels_row);
		pixels_row_c += pixels_row;
	}

	for( i = x1; i < x2; i ++ )
	{
		pixels_col = 0;
		for( j = y1; j < y2; j ++ )
		{
			if (abs(img[i * v->w + j] - diff)>= avg)
			{
				pixels_col ++;
			}
		}
		product_col += (i * pixels_col);
		product_col_c += pixels_col;	
	}

	if( pixels_row_c == 0 || product_col_c == 0 )
		return 0;


	uint32_t cy = ( product_row / pixels_row_c );
	uint32_t cx = ( product_col / product_col_c );

	*dx = (float) cx / (v->w / 100.0f);
	*dy = (float) cy / (v->h / 100.0f);

	return 1;
}

void	viewport_projection_inc( void *data, int incr, int screen_width, int screen_height )
{
	viewport_t *v = (viewport_t*) data;
	float p[9];

	p[0] = v->x1;
	p[2] = v->x2;
	p[4] = v->x3;
	p[6] = v->x4;
	p[1] = v->y1;
	p[3] = v->y2;
	p[5] = v->y3;	
	p[7] = v->y4;

	if( incr == -1 )
	{
		v->x0 ++;
		v->y0 ++;
		v->w0 -= 2;
		v->h0 -= 2;
	} else
	{
		v->x0 --;
		v->y0 --;
		v->w0 +=2;
		v->h0 +=2;
	}
	matrix_t *tmp = viewport_matrix();
	matrix_t *im = viewport_invert_matrix( v->M, tmp );

	float dx1 ,dy1,dx2,dy2,dx3,dy3,dx4,dy4;

	point_map( im, v->x0, v->y0, &dx1, &dy1);
	point_map( im, v->x0 + v->w0, v->y0, &dx2, &dy2 );
	point_map( im, v->x0, v->y0 + v->h0, &dx3, &dy3 );
	point_map( im, v->x0 + v->w0, v->y0 + v->h0, &dx4, &dy4 );

	v->x1 = dx1 / (screen_width / 100.0f);
	v->y1 = dy1 / (screen_height / 100.0f);
	v->x2 = dx2 / (screen_width / 100.0f);	
	v->y2 = dy2 / (screen_height / 100.0f);
	v->x4 = dx3 / (screen_width / 100.0f);
	v->y4 = dy3 / (screen_height / 100.0f);
	v->x3 = dx4 / (screen_width / 100.0f);	
	v->y3 = dy4 / (screen_height / 100.0f);
	free(im);
	free(tmp);

	viewport_update_perspective(v, p);
}

#ifdef ANIMAX
#include <libvjnet/mcastsender.h>
static void *sender_ = NULL;
#define GROUP 227.0.0.17
#define PORT 1234
#endif

void	viewport_transform_coords( void *data, int *in_x, int *in_y, int n, int blob_id )
{
	viewport_t *v = (viewport_t*) data;
	matrix_t *tmp = viewport_matrix();
	matrix_t *im = viewport_invert_matrix( v->M, tmp );
	int i,j=2;

	v->buf[0] = blob_id;
	v->buf[1] = n;

	for( i = 0; i < n; i ++ )
	{
		float dx1 ,dy1;
		point_map( im, in_x[i], in_y[i], &dx1, &dy1);
		v->buf[j+0] = dx1 / (v->w / 1000.0f);
		v->buf[j+1] = dy1 / (v->h / 1000.0f); 
		j+=2;
	}

	//@ send out coordinates

	/*
 		protocol: blob_id (4 bytes) | numer of points (4 bytes) | points 0..n (4 byte per point)
	*/

#ifdef ANIMAX	
	if(! sender_ )
		sender_ = mcast_new_sender( GROUP );
	if(mcast_send( sender_, v->buf, (n+2) * sizeof(int32_t), PORT_NUM )<=0)
	{
		veejay_msg(0, "Cannot send contour over mcast %s:%d", GROUP,PORT_NUM );
		mcast_close_sender( sender_ );
		sender_ = NULL;
	}
#endif
	free(im);	
	free(tmp);
}

void	viewport_external_mouse( void *data, uint8_t *img[3], int sx, int sy, int button, int frontback, int screen_width, int screen_height )
{
	viewport_t *v = (viewport_t*) data;
	if( sx == 0 && sy == 0 && button == 0 )
		return;

	int osd = 0;
	int grid =0;
	int ch = 0;
	int width = v->w;
	int height = v->h;
	int point = -1;
	int i;

	//@ use screen width/height
	float x = (float)sx / ( screen_width / 100.0f );
	float y = (float)sy / ( screen_height / 100.0f );
	double dist = 100.0;

	float p[9];
	// x,y in range 0.0-1.0
	// make a copy of the parameters

	if( button == 11 )
	{
		p[0] = v->x0;
		p[1] = v->y0;
		p[2] = v->x0 + v->w0;
		p[3] = v->y0;
		p[4] = v->x0 + v->w0;
		p[5] = v->y0 + v->h0;
		p[6] = v->x0;
		p[7] = v->y0 + v->h0;
	} else
	{
		p[0] = v->x1;
		p[2] = v->x2;
		p[4] = v->x3;
		p[6] = v->x4;
		p[1] = v->y1;
		p[3] = v->y2;
		p[5] = v->y3;	
		p[7] = v->y4;
	}

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

	if( ( button == 6 || button == 1 || button == 12) && point >= 0 )
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

	if( button == 4 || button == 13 || button == 15) // wheel up
	{
		if( button == 13 || button == 15 )
		{
			v->x0 --;
			v->y0 --;
			v->w0 +=2;
			v->h0 +=2;

			if( button == 15 )	
			{
				matrix_t *tmp = viewport_matrix();
				matrix_t *im = viewport_invert_matrix( v->M, tmp );
	
				float dx1 ,dy1,dx2,dy2,dx3,dy3,dx4,dy4;
				point_map( im, v->x0, v->y0, &dx1, &dy1);
				point_map( im, v->x0 + v->w0, v->y0, &dx2, &dy2 );
				point_map( im, v->x0, v->y0 + v->h0, &dx3, &dy3 );
				point_map( im, v->x0 + v->w0, v->y0 + v->h0, &dx4, &dy4 );

				veejay_msg(2, "Rect x0=%d, y0=%d, w0=%d, h0=%d",
					v->x0,v->y0, v->w0, v->h0 );

				v->x1 = dx1 / (screen_width / 100.0f);
				v->y1 = dy1 / (screen_height / 100.0f);
				v->x2 = dx2 / (screen_width / 100.0f);	
				v->y2 = dy2 / (screen_height / 100.0f);
				v->x4 = dx3 / (screen_width / 100.0f);
				v->y4 = dy3 / (screen_height / 100.0f);
				v->x3 = dx4 / (screen_width / 100.0f);	
				v->y3 = dy4 / (screen_height / 100.0f);

				free(im);
				free(tmp);
			}
			viewport_update_perspective( v, p );
			return;

		}
		else	
		{
			if( v->user_ui && v->snap_marker )
			{
				if( v->marker_size <= 2 )
					v->marker_size = 32;
				else
					v->marker_size -= 2;
	
				grid = 0;
			} else	if( v->user_ui )
			{
				if(v->grid_size <= 8 )
					v->grid_size = 8;
				else
					v->grid_size -=2;
				grid = 1;
				osd = 1;
			}
		}
	}
	if (button == 5 || button == 14 || button == 16) // wheel down
	{	
		if( button == 14 || button == 16 )
		{
			v->x0 ++;
			v->y0 ++;
			v->w0 -=2;
			v->h0 -=2;

			if( button == 16 )	
			{
				matrix_t *tmp = viewport_matrix();
				matrix_t *im = viewport_invert_matrix( v->M, tmp );

				float dx1 ,dy1,dx2,dy2,dx3,dy3,dx4,dy4;
				point_map( im, v->x0, v->y0, &dx1, &dy1);
				point_map( im, v->x0 + v->w0, v->y0, &dx2, &dy2 );
				point_map( im, v->x0, v->y0 + v->h0, &dx3, &dy3 );
				point_map( im, v->x0 + v->w0, v->y0 + v->h0, &dx4, &dy4 );
				veejay_msg(2, "Rect x0=%d, y0=%d, w0=%d, h0=%d",
					v->x0,v->y0, v->w0, v->h0 );

				v->x1 = dx1 / (screen_width / 100.0f);
				v->y1 = dy1 / (screen_height / 100.0f);
				v->x2 = dx2 / (screen_width / 100.0f);	
				v->y2 = dy2 / (screen_height / 100.0f);
				v->x4 = dx3 / (screen_width / 100.0f);
				v->y4 = dy3 / (screen_height / 100.0f);
				v->x3 = dx4 / (screen_width / 100.0f);	
				v->y3 = dy4 / (screen_height / 100.0f);

				free(im);
				free(tmp);
			}
			viewport_update_perspective(v,p);
			return;
		}
		else
		{
			if( v->user_ui && v->snap_marker )
			{
				if( v->marker_size > 32 )
					v->marker_size = 2;
				else
					v->marker_size += 2;
				grid = 0;
			} else 	if( v->user_ui )
			{
				if( v->grid_size > ( width / 4 ) )
					v->grid_size = width/4;
				else
					v->grid_size +=2;
					grid = 1;
				osd = 1;
			}
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

	if( grid && !v->snap_marker )	
		viewport_update_grid( v, v->grid_size, v->grid_val );

	if( osd )
		viewport_update_context_help(v);

	if(v->save)
	{
		if( button == 6 && !v->snap_marker)
		{	//@ Snap selected point to grid (upper left corner)
			float rx = v->w / 100.0 * x;
			float ry = v->h / 100.0 * y;
			int   dx = (rx+0.5) / v->grid_size;
			int   dy = (ry+0.5) / v->grid_size;

			x = (float) ( dx * v->grid_size / ( v->w / 100.0 ) );
			y = (float) ( dy * v->grid_size / ( v->h / 100.0 ) );
		}

		if( button == 12 )
		{
			int my,mx;
			switch( point )
			{
				case 0:	
					v->x0 = (int32_t)sx;
					v->y0 = (int32_t)sy;
					clamp1(v->x0, 0, v->w );
					clamp1(v->y0, 0, v->h );
					break;
				case 1:
					v->w0 = sx - v->x0;
					v->y0 = sy;		
					clamp1(v->w0, 0,v->w );
					clamp1(v->y0, 0,v->h );
					break;
				case 2:
					v->w0 = sx - v->x0;	
					v->h0 = sy - v->y0;
					clamp1(v->w0, 0,v->w );
					clamp1(v->h0, 0,v->h );
					break;
				case 3:	
					v->w0 = ( v->x0 - sx ) + v->w0;
					v->x0 = sx;
					v->h0 = sy - v->y0;
					clamp1(v->x0, 0,v->w );
					clamp1(v->h0, 0,v->h );
					clamp1(v->w0, 0,v->w );
				break;
			}


		}
		else
		{
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
		}
		ch = 1;

	}

	if( ch )
	{
		if( v->save && v->snap_marker && button != 12)
		{
			float tx = x;
			float ty = y;

			if( button != 6 && viewport_locate_marker( v, img[0], tx, ty, &x, &y ) )
			{
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
			}
		}
		viewport_update_perspective( v, p );
	}
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

//	opacity_blend_luma_apply( plane,v->grid, (width*height), 100 );

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

void		viewport_set_marker( void *data, int status )
{
	viewport_t *v = (viewport_t*) data;
	v->snap_marker = status;
	v->marker_size = 8;
}

static void	viewport_draw_col( void *data, uint8_t *plane, uint8_t *u, uint8_t *V )
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

	const uint8_t p = v->grid_val;
	const uint8_t uv = 128;
	//opacity_blend_luma_apply( plane,v->grid, (width*height), 100 );

	viewport_line( plane, fx1, fy1, fx2,fy2,width,height, p);
	viewport_line( plane, fx1, fy1, fx4,fy4,width,height, p );
	viewport_line( plane, fx4, fy4, fx3,fy3,width,height, p );
	viewport_line( plane, fx2, fy2, fx3,fy3,width,height, p );
	viewport_line( u, fx1, fy1, fx2,fy2,width,height, uv);
	viewport_line( u, fx1, fy1, fx4,fy4,width,height, uv );
	viewport_line( u, fx4, fy4, fx3,fy3,width,height, uv );
	viewport_line( u, fx2, fy2, fx3,fy3,width,height, uv );
	viewport_line( V, fx1, fy1, fx2,fy2,width,height, uv );
	viewport_line( V, fx1, fy1, fx4,fy4,width,height, uv );
	viewport_line( V, fx4, fy4, fx3,fy3,width,height, uv );
	viewport_line( V, fx2, fy2, fx3,fy3,width,height, uv );

       viewport_line( plane,   v->x0,          v->y0,                  v->x0 + v->w0,   v->y0,          width,height, 0xff);
       viewport_line( plane,   v->x0+v->w0,     v->y0,                  v->x0 + v->w0,   v->y0 + v->h0,   width,height, 0xff );
       viewport_line( plane,   v->x0 + v->w0,   v->y0 + v->h0,           v->x0,          v->y0 + v->h0,   width,height, 0xff );
       viewport_line( plane,   v->x0,          v->y0 +v->h0,            v->x0,          v->y0,          width,height, 0xff);






	draw_point( plane, fx1,fy1, width,height, v->users[0],p );
	draw_point( plane, fx2,fy2, width,height, v->users[1],p );
	draw_point( plane, fx3,fy3, width,height, v->users[2],p );
	draw_point( plane, fx4,fy4, width,height, v->users[3],p );
	draw_point( u, fx1,fy1, width,height, v->users[0],uv );
	draw_point( u, fx2,fy2, width,height, v->users[1],uv );
	draw_point( u, fx3,fy3, width,height, v->users[2],uv );
	draw_point( u, fx4,fy4, width,height, v->users[3],uv );
	draw_point( V, fx1,fy1, width,height, v->users[0],uv );
	draw_point( V, fx2,fy2, width,height, v->users[1],uv );
	draw_point( V, fx3,fy3, width,height, v->users[2],uv );
	draw_point( V, fx4,fy4, width,height, v->users[3],uv );

	 int mx = v->usermouse[0] * wx;
	 int my = v->usermouse[1] * wy;
	 
	 if( mx >= 0 && my >= 0 && mx <= width && my < height )
	 {
		 int col = grab_pixel( plane, v->usermouse[0]*wx, v->usermouse[1]*wy,width );
		 draw_point( plane, v->usermouse[0]*wx,v->usermouse[1]*wy, width,height,1, v->grid_val );

		if( v->snap_marker )
		{
			int mx1 = mx - (v->marker_size * 2);
			int my1 = my - (v->marker_size * 2);
			int mx2 = mx + (v->marker_size * 2);
			int my2 = my1;
			int mx3 = mx2;
			int my3 = my + v->marker_size*2;
			int mx4 = mx1;
			int my4 = my + v->marker_size * 2;

			viewport_line( plane, mx1, my1, mx2,my2,width,height, v->grid_val);
			viewport_line( plane, mx1, my1, mx4,my4,width,height, v->grid_val );
			viewport_line( plane, mx4, my4, mx3,my3,width,height, v->grid_val );
			viewport_line( plane, mx2, my2, mx3,my3,width,height, v->grid_val );

		}
	 }
}

int	viewport_render_ssm(void *vdata )
{
	viewport_t *v = (viewport_t*) vdata;

	if( v->disable || v->user_ui) 
		return 0;

	return 1;
}

void	viewport_draw_interface( void *vdata, uint8_t *img[3] )
{
	viewport_t *v = (viewport_t*) vdata;
	viewport_draw( v, img[0] );
}
void	viewport_draw_interface_color( void *vdata, uint8_t *img[3] )
{
	viewport_t *v = (viewport_t*) vdata;
	viewport_draw_col( v, img[0],img[1],img[2] );
}


void	viewport_produce_full_img( void *vdata, uint8_t *img[3], uint8_t *out_img[3] )
{
	viewport_t *v = (viewport_t*) vdata;
	const int len = v->w * v->h;
	register const int w = v->w;
	register uint32_t i,j,n;
	const int32_t *map = v->map;
	uint8_t *inY  = img[0];
	uint8_t *inU  = img[1];
	uint8_t *inV  = img[2];
	uint8_t       *outY = out_img[0];
	uint8_t	      *outU = out_img[1];
        uint8_t	      *outV = out_img[2];
	inY[len+1] = 0;
	inU[len+1] = 128;
	inV[len+1] = 128;

	register const	int32_t	tx1 = v->ttx1;
	register const	int32_t tx2 = v->ttx2;
	register const	int32_t	ty1 = v->tty1;
	register const	int32_t ty2 = v->tty2;
	int x,y;

	y  = ty1 * w;
#if defined (HAVE_ASM_MMX) || defined (HAVE_ASM_SSE)
	fast_memset_dirty( outY, 0, y );
	fast_memset_dirty( outU, 128, y );
	fast_memset_dirty( outV, 128, y );
#else
	veejay_memset( outY,0,y);
	veejay_memset( outU,128,y);
	veejay_memset( outV, 128,y);
#endif

	for( y = ty1; y < ty2; y ++ )
	{
#if defined (HAVE_ASM_MMX) || defined( HAVE_ASM_SSE )
		fast_memset_dirty( outY + (y * w ), 0, tx1 );
		fast_memset_dirty( outY + (y * w ) + tx2, 0, (w-tx2));

		fast_memset_dirty( outU + (y * w ), 128, tx1 );
		fast_memset_dirty( outU + (y * w ) + tx2, 128, (w-tx2));

		fast_memset_dirty( outV + (y * w ), 128, tx1 );
		fast_memset_dirty( outV + (y * w ) + tx2, 128, (w-tx2));
#else
		veejay_memset( outY + (y * w ), 0, tx1 );
		veejay_memset( outY + (y * w ) + tx2, 0, (w-tx2));

		veejay_memset( outU + (y * w ), 128, tx1 );
		veejay_memset( outU + (y * w ) + tx2, 128, (w-tx2));

		veejay_memset( outV + (y * w ), 128, tx1 );
		veejay_memset( outV + (y * w ) + tx2, 128, (w-tx2));
#endif
		for( x = tx1; x < tx2 ; x ++ )
		{
			i = y * w + x;
			n = map[i];
			outY[i] = inY[n];
			outU[i] = inU[n];
			outV[i] = inV[n];
		}
	}
	y = (v->h - ty2 ) * w;
	x = ty2 * w;
#if defined (HAVE_ASM_MMX) || defined (HAVE_AMS_SSE ) 

	fast_memset_dirty( outY + x, 0, y );
	fast_memset_dirty( outU + x, 128, y );
	fast_memset_dirty( outV + x, 128, y );
	fast_memset_finish();
#else
	veejay_memset( outY+x,0,y);
	veejay_memset( outU + x, 128,y);
	veejay_memset( outV + x , 128, y );

#endif
}

#define pack_yuyv_pixel( y0,u0,u1,y1,v0,v1 ) (\
		( (int) y0 ) & 0xff ) +\
                ( (((int) ((u0+u1)>>1) ) & 0xff) << 8) +\
                ( ((((int) y1) & 0xff) << 16 )) +\
                ( ((((int) ((v0+v1)>>1)) & 0xff) << 24 ))

void	viewport_produce_full_img_yuyv( void *vdata, uint8_t *img[3], uint8_t *out_img )
{
	viewport_t *v = (viewport_t*) vdata;
	const int len = v->w * v->h;
	const int32_t *map = v->map;
	register uint8_t *inY  = img[0];
	register uint8_t *inU  = img[1];
	register uint8_t *inV  = img[2];
	register uint32_t	*plane_yuyv = (uint32_t*)out_img;
	register uint8_t 	*outYUYV    = out_img;
	register const	int32_t	tx1 = v->ttx1;
	register const	int32_t tx2 = v->ttx2;
	register const	int32_t	ty1 = v->tty1;
	register const	int32_t ty2 = v->tty2;
	register const int w = v->w;
	register const int h = v->h;
	register const int uw = v->w >> 1;
	register uint32_t i,x,y;
	register int32_t n,m;

	inY[len+1] = 0;		// "out of range" pixel value 
	inU[len+1] = 128;
	inV[len+1] = 128;

	// clear the yuyv plane (black)
	y  = ty1 * w;
	if( y > 0) 
		yuyv_plane_clear( y*2, out_img);

	for( y = ty1; y < ty2; y ++ )
	{
		i = (y * w);
		yuyv_plane_clear( tx1+tx1, outYUYV + i + i );
		yuyv_plane_clear( (w-tx2)+(w-tx2),outYUYV + (i+tx2) + (i+tx2));
	}
	y = (v->h - ty2 ) * w;
	x = ty2 * w;
	if( y > 0 )
		yuyv_plane_clear( y*2, out_img + (x*2) );
#if defined (HAVE_ASM_MMX) || defined (HAVE_AMS_SSE ) 
	fast_memset_finish(); // finish yuyv_plane_clear
#endif
	for( y = ty1 ; y < ty2; y ++ )
	{
		for( x = tx1; x < tx2; x += 8 )
		{ // 4 YUYV pixels out, 8 Y in,  16 UV in
			i = y * w ;
			n = map[ i + x ];
			m = map[ i + x + 1];


			plane_yuyv[y * uw + ( (x+1)>>1)] = pack_yuyv_pixel( inY[n], inU[n], inU[m],
								 inY[m], inV[n], inV[m] );

			n = map[ i + x + 2 ];
			m = map[ i + x + 3 ];


			plane_yuyv[y * uw + ( (x+1+2)>>1)] = pack_yuyv_pixel( inY[n], inU[n], inU[m],
								 inY[m], inV[n], inV[m] );


			n = map[ i + x + 4 ];
			m = map[ i + x + 5 ];


			plane_yuyv[y * uw + ( (x+1+4)>>1)] = pack_yuyv_pixel( inY[n], inU[n], inU[m],
								 inY[m], inV[n], inV[m] );


			n = map[ i + x + 6 ];
			m = map[ i + x + 7 ];


			plane_yuyv[y * uw + ( (x+1+6)>>1)] = pack_yuyv_pixel( inY[n], inU[n], inU[m],
								 inY[m], inV[n], inV[m] );

		}	
		for( ; x < tx2; x += 2 )
		{
			i = y * w ;
			n = map[ i + x ];
			m = map[ i + x + 1];

			plane_yuyv[y * uw + ( (x+1)>>1)] = pack_yuyv_pixel( inY[n], inU[n], inU[m],
								 inY[m], inV[n], inV[m] );
		}
	}
}

void	viewport_produce_full_img_packed( void *vdata, uint8_t *img[3], uint8_t *out_img )
{
	viewport_t *v = (viewport_t*) vdata;
	const int len = v->w * v->h;
	const int32_t *map = v->map;
	uint8_t *inY  = img[0];
	uint8_t *inU  = img[1];
	uint8_t *inV  = img[2];
	uint8_t       *outYUYV = out_img;

	inY[len+1] = 0;
	inU[len+1] = 128;
	inV[len+1] = 128;

	register const	int32_t	tx1 = v->ttx1;
	register const	int32_t tx2 = v->ttx2;
	register const	int32_t	ty1 = v->tty1;
	register const	int32_t ty2 = v->tty2;
	register const int w = v->w;
	register uint32_t n,i,x,y,m;

	y  = ty1 * w;
	yuyv_plane_clear( y*3, out_img);

	for( y = ty1 ; y < ty2; y ++ )
	{
		yuyv_plane_clear( tx1*3, outYUYV + 3 * (y*w) );
		yuyv_plane_clear( (w-tx2)*3, outYUYV + 3 * (y*w+tx2));
		for( x = tx1; x < tx2; x ++ )
		{
			i = y * w + x;
			n = map[ i ];
			outYUYV[3  * i  ] = inY[n];
			outYUYV[3  * i + 1 ] = inV[n];
			outYUYV[3  * i + 3 ] = inU[n];
		}
	}
	y = (v->h - ty2 ) * w;
	x = ty2 * w;

	yuyv_plane_clear( y*3, out_img + (x*3) );
#if defined (HAVE_ASM_MMX) || defined (HAVE_AMS_SSE ) 
	fast_memset_finish();
#endif
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
		register uint32_t i,j,n,block;
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
		/*	for( j = 0; j < w; j ++ )
			{
				n = map[i+j];
				outY[i+j] = inY[n];
				outU[i+j] = inU[n];
				outV[i+j] = inV[n];
			}*/
	
			for( j = 0; j < w; j += 4 )
			{
				n = map[i + j];
				outY[i + j ] = inY[n];
				outU[i + j ] = inU[n];
				outV[i + j ] = inV[n];
				n = map[ i + j + 1 ];
				outY[ i + 1 + j ] = inY[n];
				outU[ i + 1 + j ] = inU[n];
				outV[ i + 1 + j ] = inV[n];
				n = map[ i + j + 2 ];
				outY[ i + 2 + j ] = inY[n];
				outU[ i + 2 + j ] = inU[n];
				outV[ i + 2 + j ] = inV[n];
				n = map[ i + j + 3 ];
				outY[ i + 3 + j ] = inY[n];
				outU[ i + 3 + j ] = inU[n];
				outV[ i + 3 + j ] = inV[n]; 
			}
			for( ; j < w; j ++ )
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

void *viewport_fx_init_map( int wid, int hei, int x1, int y1,  
		int x2, int y2, int x3, int y3, int x4, int y4)
{
	viewport_t *v = (viewport_t*) vj_calloc(sizeof(viewport_t));

	float fracx = (float) wid / 100.0f;
	float fracy = (float) hei / 100.0f;

	v->x1 = x1 / fracx;
	v->y1 = y1 / fracy;
	v->x2 = x2 / fracx;
	v->y2 = y2 / fracy;
	v->x3 = x3 / fracx;
	v->y3 = y3 / fracy;
	v->x4 = x4 / fracx;
	v->y4 = y4 / fracy;

	int res = viewport_configure (v, 
			v->x1, v->y1,
			v->x2, v->y2,
			v->x3, v->y3,
			v->x4, v->y4,
			0,0,
			wid,hei,
			wid,hei,
			0,
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
void *viewport_fx_init(int type, int wid, int hei, int x, int y, int zoom, int dir)
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
			wid,hei,
			dir,
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

