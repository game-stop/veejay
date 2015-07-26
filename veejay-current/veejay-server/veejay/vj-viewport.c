/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2008 Niels Elburg <nwelburg@gmail.com>
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
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <veejay/vj-viewport-xml.h>
#include <libvje/effects/opacity.h>
#include <libyuv/yuvconv.h>
#include <libavutil/pixfmt.h>
#include <libvjmem/vjmem.h>
#include <math.h>
#include <assert.h>
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

#define        RUP8(num)(((num)+8)&~8)

#define clamp1(x, y, z) ((x) = ((x) < (y) ? (y) : ((x) > (z) ? (z) : (x))))
#define distance1(x1,y1,x2,y2) (  sqrt( (x1 - x2) * (x1 - x2) + ( y1 - y2 ) * (y1 -y2 ) ) )

#define round1(x) ( (int32_t)( (x>0) ? (x) + 0.5 : (x)  - 0.5 ))
#define min4(a,b,c,d) MIN(MIN(MIN(a,b),c),d)
#define max4(a,b,c,d) MAX(MAX(MAX(a,b),c),d)

#include <xmmintrin.h>

#define GRID_STEP 1
#define GRID_START 44
typedef struct
{
	float m[4][4];
} matrix_t;

typedef struct
{
	void *scaler;
	uint8_t *buf[3];
	float	scale;
	float   sx;
	float	sy;
	int	sw;
	int	sh;
} ui_t;

typedef struct
{
	int x;
	int y;
} grid_t;


typedef struct
{
	int32_t x,y;
	int32_t h,w;
	int32_t x0,y0,w0,h0;
	float points[9];
	int   users[4];
	float usermouse[6];
	int   userm;
	int   user;
	int	save;
	int   user_ui;
	int   user_reverse;
	int   user_mode;
	int	grid_resolution;
	int	grid_width;
	int	grid_height;
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
	matrix_t *T;
	char *help;
	uint8_t  grid_val;
	int	parameters[8];
	char    *homedir;
	int32_t tx1,tx2,ty1,ty2;
	int32_t ttx1,ttx2,tty1,tty2;
	int	mode;
	int32_t 	*buf;
	void *sender;
	uint32_t	seq_id;
	ui_t	*ui;
	int 	saved_w;
	int	saved_h;
	grid_t	*grid;	
	int	grid_mode;
	int	initial_active;
} viewport_t;


static	float		msx(viewport_t *v, float x);
static	float		msy(viewport_t *v, float y);

static	float		vsx(viewport_t *v, float x);
static	float		vsy(viewport_t *v, float y);

static void		viewport_draw_col( void *data, uint8_t *img, uint8_t *u, uint8_t *v );
static int		viewport_update_perspective( viewport_t *v, float *values );
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
static void		viewport_find_transform( float *coord, matrix_t *M );
void 		viewport_line (uint8_t *plane,int x1, int y1, int x2, int y2, int w, int h, uint8_t col);
static void		draw_point( uint8_t *plane, int x, int y, int w, int h, int size, int col );
static viewport_config_t *viewport_load_settings( const char *dir, int mode );
static	void		viewport_prepare_process( viewport_t *v );
static	void	viewport_compute_grid( viewport_t *v );

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
/*
static void		viewport_print_matrix( matrix_t *M )
{
	veejay_msg(0, "|%f\t%f\t%f", M->m[0][0], M->m[0][1], M->m[0][2] );
	veejay_msg(0, "|%f\t%f\t%f", M->m[1][0], M->m[1][1], M->m[1][2] );
	veejay_msg(0, "|%f\t%f\t%f", M->m[2][0], M->m[2][1], M->m[2][2] );
}
*/
extern unsigned char *UTF8toLAT1(unsigned char*);

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

/*static	void	draw_dot( uint8_t *plane, int x, int y, int w, int h, int size, int col )
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
	for( i = y; i < y2; i ++ ) 
	{
		for( j = x1; j < x2 ; j ++ )
			plane[ y * w + x ] = col;
	}
}*/

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

void	viewport_set_ui(void *vv, int i)
{
	viewport_t *v = (viewport_t*) vv;
	v->user_ui = i;
}


char *viewport_get_my_help(void *vv)
{
	viewport_t *v = (viewport_t*) vv;
	if(!v->user_ui )
		return NULL;

	char reverse_mode[32];
	veejay_memset(reverse_mode,0,sizeof(reverse_mode));
	sprintf(reverse_mode, "%s", ( v->user_reverse ? "Forward"  : "Reverse" ) );
	
	char scroll_mode[64];

	switch(v->grid_mode) {
		case 0:
			snprintf(scroll_mode,sizeof(scroll_mode),
					"CTRL + Mousewheel: Marker %2dx%2d up=Grid down=Dots",v->marker_size,v->marker_size);
			break;
		case 1:
			snprintf(scroll_mode,sizeof(scroll_mode),
					"CTRL + Mousewheel: Dots %2dx%2d up=Marker down=Grid",v->grid_resolution,v->grid_resolution);
			break;
		case 2:
			snprintf(scroll_mode,sizeof(scroll_mode),
					"CTRL + Mousewheel: Grid %2dx%2d up=Grid down=Marker",v->grid_resolution,v->grid_resolution);
			break;
	}


	char tmp[1500];
	char startup_mode[16];
	snprintf(startup_mode,sizeof(startup_mode), "%s", (v->initial_active==1 ? "Active" :"Inactive"  ));
	snprintf(tmp,sizeof(tmp), "Interactive Input/Projection calibration\nMouse Left: Set point\nCTRL + Cursor Keys: Finetune point\nMouse Left + RSHIFT: Set projection quad \nMouse Right: %s\nMouse Middle: Exit without saving\nMouse Middle + LSHIFT: Line Color\nCTRL + h:Hide/Show this Help\nCTRL + p:Enable/disable transform\nCTRL + a: %s on startup.\nCTRL + s: Exit this screen.\n%s\n\n",
			reverse_mode,
			startup_mode,
			scroll_mode
			);
	

	return strdup( tmp );
}
char *viewport_get_my_status(void  *vv)
{
	viewport_t *v = (viewport_t*) vv;
	if(!v->user_ui )
		return NULL;

//	float x = v->usermouse[2] / ( v->w / 100.0f );
//	float y = v->usermouse[3] / ( v->h / 100.0f );

	float tx = vsx(v,v->usermouse[4]);
	float ty = vsy(v,v->usermouse[5]);

	char status[1024];
	snprintf(status,1024, "Projection Quad: %dx%d + %dx%d\nPoints: 1=%2.2fx%2.2f 2=%2.2fx%2.2f 3=%2.2fx%2.2f 4=%2.2fx%2.2f\nCurrent Position: %2.1fx%2.1f\n",
		v->x0,v->y0,
		v->w0,v->h0,
		v->x1,v->y1,
		v->x2,v->y2,
		v->x3,v->y3,
		v->x4,v->y4,
		tx,ty
		);

	return strdup( status );
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

static	inline void		point_map_int( matrix_t *M, float x, float y, int *nx, int *ny)
{
	float w = M->m[2][0] * x + M->m[2][1] * y + M->m[2][2];

	if( w == 0.0 )
		w = 1.0;
	else
		w = 1.0 / w;

	*nx = ceilf( (M->m[0][0] * x + M->m[0][1] * y + M->m[0][2] ) * w);
	*ny = ceilf( (M->m[1][0] * x + M->m[1][1] * y + M->m[1][2] ) * w);

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


/*
void	viewport_get_projection_coords( 
	void *data, int32_t *x0, int32_t *y0, int32_t *w0, int32_t *h0 )
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
	
}*/
static	float		msy(viewport_t *v, float y)
{
	if( v->ui->scale == 1.0f ) { 
		return y;
	}
	ui_t *u = v->ui;
	int	    cy = v->h / 2;
	int	    dy = cy - ( u->sh / 2 );

	float		a = (float) dy / ( v->h / 100.0f );
	float		s  = (float) v->h / (float) v->ui->sh;

	return (y/s) + a;
}

static	float		msx(viewport_t *v, float x)
{
	if( v->ui->scale == 1.0f ) { 
		return x;
	}
	ui_t *u = v->ui;
	int	    cx = v->w / 2;
	int	    dx = cx - ( u->sw / 2 );

	float		a = (float) dx / ( v->w / 100.0f );
	float		s  = (float) v->w / (float) v->ui->sw;

	return (x/s) + a;
}

static	float		vsx(viewport_t *v, float x)
{
	if( v->ui->scale == 1.0f ) { 
		return x;
	}
	ui_t *u = v->ui;
	int	    cx = v->w / 2;
	int	    dx = cx - ( u->sw / 2 );

	float		a = (float) dx / ( v->w / 100.0f );
	float		s  = (float) v->w / (float) v->ui->sw;
	return (x-a)*s;
}
static	float		vsy(viewport_t *v, float x)
{
	if( v->ui->scale == 1.0f ) 
		return x;

	ui_t *u = v->ui;
	int	    cy = v->h / 2;
	int	    dy = cy - ( u->sh / 2 );

	float		a = (float) dy / ( v->h / 100.0f );

	float		s  = (float) v->h / (float) v->ui->sh;
	return (x-a)*s;
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
					int grid_resolution)
{
	int w = wid, h = hei;
	if( grid_resolution <= 8 )
		grid_resolution = GRID_START;
	float rat = (h/(float)w);

	v->grid_width = grid_resolution;
	v->grid_height = grid_resolution * rat;
	
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

	if(v->m) {
	 free(v->m);
	 v->m = NULL;
	}
	if(v->M) {
	 free(v->M);
	 v->M = NULL;
	}
	
	if ( reverse )
	{
		v->m = viewport_matrix();
		viewport_copy_from( v->m, m );
		matrix_t *im = viewport_matrix();
		v->M = viewport_invert_matrix( v->m, im );
		if(!v->M)
		{
			free(m);
			free(im);
			free(v->m);
			v->m = NULL;
			return 0;
		}
		free(im);
		free(m);
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
		if( v->T ) free( v->T );
		if( v->map ) free( v->map );
		if( v->help ) free( v->help );
		if( v->homedir) free(v->homedir);
		if( v->buf ) free(v->buf);
		if( v->ui ) {
			if( v->ui->scaler ) 
				yuv_free_swscaler(v->ui->scaler);
			if( v->ui->buf )
				free(v->ui->buf[0]);
			free(v->ui);
		}	
		if(v->grid) free(v->grid);
		free(v);
	}
	v = NULL;
}

static	int		viewport_update_perspective( viewport_t *v, float *values )
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
					 v->grid_resolution );


	if(! res )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Viewport: Invalid quadrilateral. Trying to fallback");

		v->x1 = values[0]; v->x2 = values[2]; v->x3 = values[4]; v->x4 = values[6];
		v->y1 = values[1]; v->y2 = values[3]; v->y3 = values[5]; v->y4 = values[7];

		if(!viewport_configure( v, v->x1, v->y1, v->x2, v->y2, v->x3, v->y3,v->x4,v->y4,
				v->x0, v->y0, v->w0, v->h0,v->w,v->h, v->user_reverse, v->grid_val,v->grid_resolution ));
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to configure the viewport");
			veejay_msg(VEEJAY_MSG_ERROR, "If you are using a preset-configuration, see ~/.veejay/viewport.cfg");
			v->disable = 1;
			return res;
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

	return res;
}
static int      nearest_div(int val )
{
        int r = val % 8;
        while(r--)
                val--;
        return val;
}
/*static int      nearest_div4(int val )
{
        int r = val % 4;
        while(r--)
                val--;
        return val;
}
static int      nearest_div16(int val )
{
        int r = val % 16;
        while(r--)
                val--;
        return val;
}
*/
static	void	*viewport_init_swscaler(ui_t *u, int w, int h)
{
	uint8_t *dummy[3] = { NULL,NULL,NULL };
	int nw = w * u->scale;
	int nh = h * u->scale;
	u->sw  = nearest_div(nw);
	u->sh  = nearest_div(nh);
	VJFrame *srci = yuv_yuv_template( dummy[0],dummy[1],dummy[2],w,h,PIX_FMT_GRAY8);
	VJFrame *dsti = yuv_yuv_template( dummy[0],dummy[1],dummy[2],u->sw,u->sh,PIX_FMT_GRAY8);
	sws_template t;
	memset(&t,0,sizeof(sws_template));
	t.flags = yuv_which_scaler();
	u->sx   = (float)w / (float) u->sw;
	u->sy   = (float)h / (float) u->sh;
	void *scaler = yuv_init_swscaler( srci,dsti,&t,yuv_sws_get_cpu_flags());

	free(srci);	
	free(dsti);

	return scaler;
}


void	viewport_reconfigure(void *vv)
{
	viewport_t *v = (viewport_t*) vv;
	float p[9];
	
	p[0] = v->x1;
	p[2] = v->x2;
	p[4] = v->x3;
	p[6] = v->x4;
	p[1] = v->y1;
	p[3] = v->y2;
	p[5] = v->y3;	
	p[7] = v->y4;

	viewport_update_perspective(v,p);

}

void	viewport_set_composite(void *vc, int mode, int colormode)
{
	viewport_config_t *c = (viewport_config_t*) vc;
	c->composite_mode = mode;
	c->colormode = colormode;
}
int	viewport_get_color_mode_from_config(void *vc)
{
	viewport_config_t *c = (viewport_config_t*) vc;
	return c->colormode;
}

int	viewport_get_composite_mode_from_config(void *vc)
{
	viewport_config_t *c = (viewport_config_t*) vc;
	return c->composite_mode;
}

int	viewport_get_initial_active( void *vv )
{
	viewport_t *v = (viewport_t*) vv;
	return v->initial_active;
}

void	viewport_set_initial_active( void *vv, int status )
{
	viewport_t *v = (viewport_t*) vv;
	v->initial_active = status;
}

void	*viewport_get_configuration(void *vv )
{

	viewport_t *v = (viewport_t*) vv;
	viewport_config_t *o = (viewport_config_t*) vj_calloc(sizeof(viewport_config_t)); //FIXME not always freed?
	o->saved_w = v->saved_w;
	o->saved_h = v->saved_h;
	o->reverse = v->user_reverse;
	o->grid_resolution = v->grid_resolution;
	o->grid_color = v->grid_val;
	o->frontback = 0;
	o->x0 	= v->x0;
	o->y0	= v->y0;
	o->w0   = v->w0;
	o->h0   = v->h0;
	o->x1   = v->x1;
	o->x2   = v->x2;
        o->x3   = v->x3;
        o->x4   = v->x4;
        o->y1   = v->y1;
	o->y2   = v->y2;
	o->y3   = v->y3;
  	o->y4   = v->y4;
	o->scale = v->ui->scale;
	o->initial_active = v->initial_active;

	return o;
}

int	viewport_reconfigure_from_config(void *vv, void *vc)
{
	viewport_t *v = (viewport_t*) vv;
	viewport_config_t *c = (viewport_config_t*) vc;
	viewport_config_t *o = viewport_get_configuration(vv );

	if( c->saved_w != v->saved_w || c->saved_h != v->saved_h ) {
		float sx=1.0f;
		float sy=1.0f;	
		if( c->saved_w > 0 && c->saved_h > 0 ) { 
			sx = (float) c->saved_w / (float) v->saved_w;
			sy = (float) c->saved_h / (float) v->saved_h;
		}
		o->x0 = c->x0 * sx;
		o->y0 = c->y0 * sy;
		o->w0 = c->w0 * sx;
		o->h0 = c->h0 * sy;
	}


	// Clear map
	const int len = v->w * v->h;
	int k;
	for( k = 0 ; k < len ; k ++ )
		v->map[k] = len+1;
	
	v->disable = 0;

	// try to initialize the new settings
	int res = viewport_configure( v, 	c->x1,c->y1,
						c->x2,c->y2,
						c->x3,c->y3,
						c->x4,c->y4,		
						c->x0,c->y0,
						c->w0,c->h0,
						v->w,v->h,
						c->reverse,
						c->grid_color,
						c->grid_resolution );

	if(!res) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot load calibration settings, restoring defaults.");
		res = viewport_configure( v,	o->x1,o->y1,
						o->x2,o->y2,
						o->x3,o->y3,
						o->x4,o->y4,		
						o->x0,o->y0,
						o->w0,o->h0,
						v->w,v->h,
						o->reverse,
						o->grid_color,
						o->grid_resolution );


	}

	if(!res) {	
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to revert to old configuration.");
		v->disable = 1;
		free(o);
		return 0;
	}

	
	if( res ) {
		v->user_ui = 0;
		viewport_process( v );
		veejay_msg(VEEJAY_MSG_DEBUG, 
		"Reconfigured calibration for %dx%d to (1)=%fx%f\t(2)=%fx%f\t(3)=%fx%f\t(4)=%fx%f\t%dx%d+%dx%d",
			v->w,v->h,v->x1,v->y1,v->x2,v->y2,v->x3,v->y3,v->x4,v->y4,v->x0,v->y0,v->w0,v->h0);

	}
	free(o);
	return 1;
}
void	viewport_update_from(void *vv, void *bb)
{
	viewport_t *v = (viewport_t*) vv;
	viewport_t *b = (viewport_t*) bb;

	if(!v || !b) return;
	float p[9];
	
	p[0] = b->x1;
	p[2] = b->x2;
	p[4] = b->x3;
	p[6] = b->x4;
	p[1] = b->y1;
	p[3] = b->y2;
	p[5] = b->y3;	
	p[7] = b->y4;


	float sx = (float) b->w / (float) v->w;
	float sy = (float) b->h / (float) v->h;

	b->x0 = v->x0 * sx;
	b->y0 = v->y0 * sy;
	b->w0 = v->w0 * sx;
	b->h0 = v->h0 * sy;
	b->x  = v->x * sx;
	b->y  = v->y * sy;
	
	b->x1 = v->x1;
	b->y1 = v->y1;
	b->x2 = v->x2;
	b->y2 = v->y2;
	b->x3 = v->x3;
	b->y3 = v->y3;
	b->x4 = v->x4;
	b->y4 = v->y4;

	b->user_reverse = v->user_reverse;
	

	if(viewport_update_perspective(b,p)) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Configured input %dx%d to (1)=%fx%f\t(2)=%fx%f\t(3)=%fx%f\t(4)=%fx%f\t%dx%d+%dx%d",
			b->w,b->h,b->x1,b->y1,b->x2,b->y2,b->x3,b->y3,b->x4,b->y4,b->x0,b->y0,b->w0,b->h0);
	}
	else
		veejay_msg(VEEJAY_MSG_DEBUG,"Failed to configure input."); 


}

void *viewport_init(int x0, int y0, int w0, int h0, int w, int h, int iw, int ih,const char *homedir, int *enable, int *frontback, int mode )
{
	//@ try to load last saved settings
	viewport_config_t *vc = viewport_load_settings( homedir,mode );
	if(vc) {
		float sx = (float) w / (float) vc->saved_w;
		float sy = (float) h / (float) vc->saved_h;

		vc->x0 = vc->x0 * sx;
		vc->y0 = vc->y0 * sy;
		vc->w0 = vc->w0 * sx;
		vc->h0 = vc->h0 * sy;
		veejay_msg(VEEJAY_MSG_INFO,"\tQuad    : %dx%d+%dx%d",vc->x0,vc->y0,vc->w0,vc->h0 );
	} else {
		veejay_msg(VEEJAY_MSG_WARNING, "No or invalid viewport configuration file in %s", homedir );
		veejay_msg(VEEJAY_MSG_WARNING, "Using default values");
		veejay_msg(VEEJAY_MSG_INFO,"\tBacking  : %dx%d",w,h);
		veejay_msg(VEEJAY_MSG_INFO,"\tRectangle: %dx%d+%dx%d",x0,y0,w0,h0);
	}

	viewport_t *v = (viewport_t*) vj_calloc(sizeof(viewport_t));
	v->usermouse[0] = 0.0;
	v->usermouse[1] = 0.0;
	v->usermouse[2] = 0.0;
	v->usermouse[3] = 0.0;
	v->M = NULL;
	v->m = NULL;
	v->grid = NULL;
	v->ui  = vj_calloc( sizeof(ui_t));
	v->ui->buf[0] = vj_calloc(sizeof(uint8_t) * RUP8(w * h) );
	v->ui->scale  = 0.5f;
	v->ui->scaler = viewport_init_swscaler(v->ui,iw,ih);
	v->saved_w = w;
	v->saved_h = h;
	v->w = w;
	v->h = h;		
	v->marker_size = 4;
	v->homedir = strdup(homedir);
	int res;

	if( vc == NULL )
	{
		res = viewport_configure (v, 29.0, 28.0,
					     70.0, 30.0,
					70.0, 66.0,
						30.0, 69.0,

					     x0,y0,w0,h0,
					     w,h,
					     1,
					     0xff,
					     w/32 );

		*enable = 0;
		*frontback = 1;
		v->user_ui = 0;

	}
	else
	{
		v->marker_size = vc->marker_size;
		v->grid_resolution = vc->grid_resolution;
		v->grid_mode = vc->grid_mode;	
		v->initial_active = vc->initial_active;

		res = viewport_configure( v, 	vc->x1, vc->y1,
					     	vc->x2, vc->y2,
						vc->x3, vc->y3,
					 	vc->x4, vc->y4,
						vc->x0, vc->y0,
						vc->w0, vc->h0,			
						w,h,
						vc->reverse,
						vc->grid_color,
						vc->grid_resolution );

		*enable = vc->initial_active;
		*frontback = vc->frontback;
		v->user_ui = 0;

	}


	if(! res )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid point locations");
		free(v->homedir);
		free(v->ui->buf[0]);
		free(v->ui);
		free(v);
		free(vc);
		return NULL;
	}

	// Allocate memory for map
	v->map = (int32_t*) vj_calloc(sizeof(int32_t) * RUP8(v->w * v->h + (v->w*2)) );

	const int len = v->w * v->h;
	const int eln = len + ( v->w * 2 );
	int k;
	for( k = len ; k < eln ; k ++ )
		v->map[k] = len+1;

	// calculate initial view
	viewport_process( v );

	v->buf = vj_calloc( sizeof(int32_t) * 50000 );
	free(vc);
	
	if(v->grid_resolution > 0)
		viewport_compute_grid(v);

    	return (void*)v;
}

void *viewport_clone(void *vv, int new_w, int new_h )
{
	viewport_t *v = (viewport_t*) vv;
	if(!v) return NULL;
	viewport_t *q = (viewport_t*) vj_malloc(sizeof(viewport_t));
	veejay_memcpy(q,v,sizeof(viewport_t));

	float sx = (float) new_w / (float) v->w;
	float sy = (float) new_h / (float) v->h;
	q->M  = NULL;
	q->m  = NULL;
	q->grid = NULL;
	q->initial_active = v->initial_active;
	q->x0 = v->x0 * sx;
	q->y0 = v->y0 * sy;
	q->w0 = v->w0 * sx;
	q->h0 = v->h0 * sy;
	q->x  = v->x * sx;
	q->y  = v->y * sy;
	q->w  = new_w;
	q->h  = new_h;
	q->usermouse[0] = 0.0;
	q->usermouse[1] = 0.0;
	q->usermouse[2] = 0.0;
	q->usermouse[3] = 0.0;
	q->ui  = vj_calloc( sizeof(ui_t));
	q->ui->buf[0] = vj_calloc(sizeof(uint8_t) * RUP8(new_w * new_h) );
	q->ui->scale  = 1.0f;
	q->ui->scaler = viewport_init_swscaler(q->ui,new_w,new_h);
	q->homedir = strdup(v->homedir);

	int	res = viewport_configure( q, 	q->x1, q->y1,
					     	q->x2, q->y2,
						q->x3, q->y3,
					 	q->x4, q->y4,
						q->x0, q->y0,
						q->w0, q->h0,			
						new_w,new_h,
						q->user_reverse,
						q->grid_val,
						q->grid_resolution );

	q->user_ui = 0;

	if(! res )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid point locations");
		free(q->homedir);
		free(q->ui->buf[0]);
		free(q->ui);
		free(q);
		return NULL;
	}

	// Allocate memory for map
	q->map = (int32_t*) vj_calloc(sizeof(int32_t) * RUP8(q->w * q->h + (q->w*2)) );

	const int len = q->w * q->h;
	const int eln = len + ( q->w * 2 );
	int k;
	for( k = len ; k < eln ; k ++ )
		q->map[k] = len+1;

	viewport_process( q );

	q->buf = vj_calloc( sizeof(int32_t) * 50000 );
	veejay_msg(VEEJAY_MSG_INFO,"\tConfiguring input:");
	veejay_msg(VEEJAY_MSG_INFO, "\tPoints   :\t(1) %fx%f (2) %fx%f", q->x1,q->y1,q->x2,q->y2);
	veejay_msg(VEEJAY_MSG_INFO, "\t         :\t(3) %fx%f (4) %fx%f", q->x2,q->y2,q->x3,q->y3);
	veejay_msg(VEEJAY_MSG_INFO, "\tQuad     :\t%dx%d+%dx%d", q->x0,q->y0,q->w0,q->h0 );
	veejay_msg(VEEJAY_MSG_INFO, "\tDimension:\t%dx%d",q->w,q->h);
    	return (void*) q;
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
	sprintf(path, "%s/viewport.cfg", dir);
	FILE *fd = fopen( path, "r" );
	if(!fd)
	{
		veejay_msg(0, "Unable to open file %s",path);
		free(vc);
		return NULL;
	}
	fseek(fd,0,SEEK_END );
	unsigned int len = ftell( fd );
		
	if( len <= 0 )
	{
		veejay_msg(0, "%s is empty", path);
		free(vc);
		return NULL;
	}

	char *buf = vj_calloc( (len+1) );

	rewind( fd );
	fread( buf, len, 1 , fd);

	fclose(fd );

	int n = sscanf(buf, "%f %f %f %f %f %f %f %f %d %d %d %d %d %d %d %d %d %d %d %d %d",
			&vc->x1, &vc->y1,
			&vc->x2, &vc->y2,
			&vc->x3, &vc->y3,
			&vc->x4, &vc->y4,
			&vc->reverse,
			&vc->grid_resolution,
			&vc->grid_color,
			&vc->x0,
			&vc->y0,
			&vc->w0,
			&vc->h0,
			&vc->frontback,
			&vc->saved_w,
			&vc->saved_h,
			&vc->marker_size,
			&vc->grid_mode,
			&vc->initial_active);

	//@ pre 1.4.10
	if( n == 20 ) {
		vc->initial_active = 1;
		n++;
	}

	if( n != 21 )
	{
		veejay_msg(0, "Unable to read %s (file is %d bytes)",path, len );
		free(vc);
		free(buf);
		return NULL;
	}

	free(buf);
	veejay_msg(VEEJAY_MSG_INFO, "Projection configuration:");
	veejay_msg(VEEJAY_MSG_INFO, "\tBehaviour:\t%s", (vc->reverse ? "Forward" : "Projection") );
	veejay_msg(VEEJAY_MSG_INFO, "\tPoints     :\t(1) %fx%f (2) %fx%f", vc->x1,vc->y1,vc->x2,vc->y2);
	veejay_msg(VEEJAY_MSG_INFO, "\t         :\t(3) %fx%f (4) %fx%f", vc->x2,vc->y2,vc->x3,vc->y3);
	veejay_msg(VEEJAY_MSG_INFO, "\tPencil   :\t%s", (vc->grid_color == 0xff ? "white" : "black" ) );
	veejay_msg(VEEJAY_MSG_INFO, "\tEnabled  :\t%s",
	(vc->initial_active == 0 ? "No" : "Yes"));

	return vc;
}

void	viewport_save_settings( void *ptr, int frontback )
{
	viewport_t *v = (viewport_t *) ptr;
	char path[1024];
	sprintf(path, "%s/viewport.cfg", v->homedir );

	FILE *fd = fopen( path, "wb" );

	if(!fd)
	{
		veejay_msg(0, "Unable to open '%s' for writing. Cannot save viewport settings",
			path );
		return;
	}

	char content[512];

	sprintf( content, "%f %f %f %f %f %f %f %f %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
			v->x1,v->y1,v->x2,v->y2,
			v->x3,v->y3,v->x4,v->y4,
			v->user_reverse,
			0,
			v->grid_val,
			v->x0,
			v->y0,
			v->w0,
			v->h0,
			frontback,
			v->saved_w,
			v->saved_h,
			v->marker_size,
			v->grid_mode,
	      		v->initial_active );

	int res = fwrite( content, strlen(content), 1, fd );

	if( res <= 0 )
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to save viewport settings to %s", path );

	fclose( fd );

	veejay_msg(VEEJAY_MSG_DEBUG, "Saved viewport settings to %s", path);
}
/*
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
}*/

void	viewport_projection_inc( void *data, int incr, int screen_width, int screen_height )
{
}
/*
#define ANIMAX

#ifdef ANIMAX
#include <libvjnet/mcastsender.h>
#define GROUP "227.0.0.17"
#define PORT_NUM 1234
#endif

typedef struct
{
	int x;
	int y;
} point_t;

inline int is_left( point_t *p0, point_t *p1, point_t *p2 )
{
	return ( 
		(p1->x - p0->x) * (p2->y - p0->y) -
		(p2->x - p0->x) * (p1->y - p0->y) 
	       );
}

//@ chainhull 2D (C) 2001 softSurfer (www.softsurfer.com)
//@ http://geometryalgorithms.com/Archive/algorithm_0109/algorithm_0109.htm
point_t **chainhull_2d( point_t **p , int n, int *res )
{
	point_t **H = (point_t**) vj_malloc( n * sizeof(point_t));
	int i;

	int bot=0, top=-1;

	int xmin = p[0]->x;
	for( i = 1; i < n; i++)
		if( p[i]->x != xmin ) break;
	int minmax = i-1;
	int minmin = 0;
	if( minmax == (n-1)) {
		H[++top] = p[minmin];
		if( p[minmax]->y != p[minmin]->y )
			H[++top] = p[minmax];
		H[++top] = p[minmin];
		*res = top + 1;
	}

	int maxmin,maxmax = n-1;
	int xmax = p[n-1]->x;
	for( i = n-2; i >= 0; i -- )
		if( p[i]->x != xmax ) break;
	maxmin = i+1;

	H[++top] = p[minmin];
	i= minmax;
	while( ++i <= maxmin )
	{
		if ( is_left( p[minmin], p[maxmin], p[i]) >= 0 && i < maxmin )
			continue;
		while( top > 0 )
		{
			if ( is_left( H[top-1], H[top], p[i] ) > 0 )
				break;
			else
				top--;
		}
		H[++top] = p[i];
	}


	if( maxmax != maxmin )
		H[++top] = p[maxmax];
	bot = top;
	i = maxmin;
	while( --i >= minmax )
	{
		if( is_left( p[maxmax], p[minmax], p[i] ) >= 0 && i > minmax )
			continue;
		while( top > bot )
		{
			if( is_left( H[top-1], H[top], p[i] ) > 0 )
				break;
			else
				top--;
		}
		H[++top] = p[i];
	}
	if( minmax != minmin )
		H[++top] = p[minmin];

	*res = top + 1;
	
	return H;
}

static	void	shell_sort_points_by_degree( double *a , point_t **p, int n )
{
	int i,j,increment=3;
	double temp;
	int dx,dy;
	while( increment > 0 )
	{
		for( i = 0; i < n; i ++ )
		{
			j=i;
			temp = a[i];
			dx = p[i]->x;
			dy = p[i]->y;
			while(( j>= increment) && (a[j-increment] > temp ))
			{
				a[j] = a[j-increment];
				p[j]->x = p[j-increment]->x;
				p[j]->y = p[j-increment]->y;
				j = j - increment;
			}
			a[j] = temp;
			p[j]->x = dx;
			p[j]->y = dy;
		}
		if( increment / 2 != 0 )
			increment = increment / 2;
		else if (increment ==1 )
			increment = 0;
		else 
			increment = 1;
	}

}

static void sort_points_by_degree( double *a, point_t **p, int n )
{
        int i;
        for( i = 2; i <= n; i ++ )
        {
                float sentinel = a[i];
		point_t point;
		point.x = p[i]->x;
		point.y = p[i]->y;
		int k = i;
                while( sentinel < a[k-1] && k > 0)
		{
		       int j = k;
                       a[k] = a[--k];
		       p[j]->x = p[k]->x;
		       p[j]->y = p[k]->y;
	        }	       
	        a[k] = sentinel;
		p[k]->x = point.x;
		p[k]->y = point.y;
        }
}

#define VEEJAY_PACKET_SIZE 16384

void	viewport_dummy_send( void *data )
{
	viewport_t *v = (viewport_t*) data;
#ifdef ANIMAX
	unsigned char empty_buf[VEEJAY_PACKET_SIZE];
	veejay_memset( empty_buf, 0, VEEJAY_PACKET_SIZE );

	if(! v->sender )
	{
		v->sender = mcast_new_sender( GROUP );
		v->seq_id = 0;
	}
	if(!v->sender)
		return;

	int result = mcast_send( v->sender, empty_buf,VEEJAY_PACKET_SIZE, PORT_NUM );
	if(result<=0)
	{
		veejay_msg(0, "Cannot send empty packet over mcast %s:%d", GROUP,PORT_NUM );
		mcast_close_sender( v->sender );
		v->sender = NULL;
	}
#endif
}

void	viewport_transform_coords( 
		void *data, 
		void *input,
		int n, 
		int blob_id,
		int center_x,
		int center_y,
		int wid, 
		int hei, 
		int num_objects,
		uint8_t *plane )
{
	int i, res = 0;
	viewport_t *v = (viewport_t*) data;
#ifdef ANIMAX	
	if(! v->sender )
	{
		v->sender = mcast_new_sender( GROUP );
		v->seq_id = 0;
		veejay_memset( v->buf, 0, VEEJAY_PACKET_SIZE );
	}
	if(!v->sender)
		return;
#endif

	if( n <= 0 )
	{
		viewport_dummy_send( data );
		return;
	}
	
	if( !v->T )
	{
		matrix_t *tmp = viewport_matrix();
		v->T = viewport_invert_matrix( v->M, tmp );
		free(tmp);
	}

	point_t **points = (point_t**) input;
	double  *array   = (double*) vj_malloc( (n+3) * sizeof(double));

	for( i = 0; i < n; i ++ )
		array[i] = atan2( (points[i]->x - center_x), (points[i]->y - center_y) ) * (180.0/M_PI );

	//@ convex hull 
	point_t **contour = chainhull_2d( points, n, &res );

	if( res > 256 )
	{
		veejay_msg(1, "Convex Hull has %d points, Maximum allowed is 256", res );
		res = 256;
	}

	if ( plane )
	{
		for( i = 0; i < (res-1); i ++ )
		{
			//@ draw polygon 
			viewport_line( plane, 
				contour[i]->x,
				contour[i]->y,
				contour[i+1]->x,
				contour[i+1]->y,
				wid,
				hei,
				200 );
		}

		plane[ center_y * wid + center_x ] = 0xff; //@ display centroid
	}

	shell_sort_points_by_degree( array, points, n );
	
	//@ Protocol: 
	//@ bytes 0 ...  4 : blob id
	//        4 ...  8 : number of points in convex hull
	//        8 ... 12 : header symbol
	//       12 ... 16 : sequence number
	//       16 ... 20 : total number of blobs
	//       20 ... 24 : number of points in contour
	//       24 ... N1 : convex hull points
	//       N1 ... N2 : contour hull points
	//
	//       packet size: 16 Kbytes
	
	v->buf[0] = blob_id;		
	v->buf[1] = res*2;	
	v->buf[2] = -1;
	v->buf[3] = v->seq_id ++;
	v->buf[4] = num_objects;
	v->buf[5] = n*2;
	int j = 6;
	for( i = 0; i < res; i ++ )
	{
		float dx1,dy1;
		point_map( v->T, contour[i]->x, contour[i]->y, &dx1, &dy1 );
		v->buf[j + 0] = (int)((dx1/ (float) v->w) * 1000.0f );
		v->buf[j + 1] = (int)((dy1/ (float) v->h) * 1000.0f );
		j+=2;
	}
	
	for( i = 0; i < n; i ++ )
	{
		float dx1,dy1;
		point_map( v->T, points[i]->x, points[i]->y, &dx1,&dy1 );
		v->buf[j + 0] = (int) ( ( dx1/(float) v->w) * 1000.0f );
		v->buf[j + 1] = (int) ( ( dy1/(float) v->h) * 1000.0f );
		j += 2;
	}
	int payload = ((n*2)+(res * 2) + 6) * sizeof(int);
	int left = VEEJAY_PACKET_SIZE - payload;

	int *ptr = &(v->buf[j]);
	
	if(left > 0)
		veejay_memset( ptr,0, left );

	if( payload > VEEJAY_PACKET_SIZE )
		veejay_msg(1, "Contours and convex hull too large for packet");

#ifdef ANIMAX	
	int result = mcast_send( v->sender, v->buf,VEEJAY_PACKET_SIZE, PORT_NUM );
	if(result<=0)
	{
		veejay_msg(0, "Cannot send contour/convex hull over mcast %s:%d", GROUP,PORT_NUM );
		mcast_close_sender( v->sender );
		v->sender = NULL;
	}
#endif

	free(contour);
	free(array);
	
}

int	*viewport_event_get_projection(void *data, int scale) {
	viewport_t *v = (viewport_t*) data;
	float fscale = 1.0f;
	float set[9];	
	if( scale == 100 ) {
		set[0] = v->x1;// * sw;
		set[1] = v->y1;// * sh;
		set[2] = v->x2;// * sw;
		set[3] = v->y2;// * sh;
		set[4] = v->x3;// * sw;
		set[5] = v->y3;// * sh;
		set[6] = v->x4;// * sw;
		set[7] = v->y4;// * sh;
	} else {
		float sw = (float) scale / 100.0;
		float sh = (float) scale / 100.0;	
		fscale   = sw;
		set[0] = v->x1 * sw;
		set[1] = v->y1 * sh;
		set[2] = v->x2 * sw;
		set[3] = v->y2 * sh;
		set[4] = v->x3 * sw;
		set[5] = v->y3 * sh;
		set[6] = v->x4 * sw;
		set[7] = v->y4 * sh;
	}

	int *res = (int*) vj_malloc(sizeof(int) * 8 );
	int i;

	for( i = 0; i < 8 ; i ++ ) {
		res[i] = (int) ( set[i]);
	}
	return res;
}

int	viewport_event_set_projection(void *data, float x, float y, int num, int frontback) {
	

	viewport_t *v = (viewport_t*) data;
	switch(num) {
		case 1:
			v->x1 = x;
			v->y1 = y;
			break;
		case 2:
			v->x2 = x;
			v->y2 = y;
			break;
		case 3:
			v->x3 = x;
			v->y3 = y;
			break;
		case 4:
			v->x4 = x;
			v->y4 = y;
			break;
	}
	float p[8];
	p[0] = v->x1;
	p[2] = v->x2;
	p[4] = v->x3;
	p[6] = v->x4;
	p[1] = v->y1;
	p[3] = v->y2;
	p[5] = v->y3;	
	p[7] = v->y4;

	if(	viewport_update_perspective( v, p ) )  {
		veejay_msg(VEEJAY_MSG_INFO, "Accepted viewport configuration from remote.");
	} else {
		veejay_msg(0, "Error updating points");
	}

	return 1;
}*/


int	viewport_finetune_coord(void *data, int screen_width, int screen_height,int inc_x, int inc_y)
{
	viewport_t *v = (viewport_t*) data;
	if(!v->user_ui)
		return 0;

	int point = -1;
	int i;

	//@ use screen width/height
	double dist = 100.0;
	int	x = v->usermouse[4];
	int	y = v->usermouse[5];
	float p_cpy[9];
	float p[9];
	
	p[0] = v->x1;
	p[2] = v->x2;
	p[4] = v->x3;
	p[6] = v->x4;
	p[1] = v->y1;
	p[3] = v->y2;
	p[5] = v->y3;	
	p[7] = v->y4;

	int j;

	float ix = (float) inc_x * 0.1f;
	float iy = (float) inc_y * 0.1f;

	for  ( j = 0 ; j < 8 ; j += 2 ) {
		p_cpy[j] = p[j];
		p_cpy[j+1]=p[j+1];
		p[j]  =  msx(v, p[j] );
		p[j+1]=  msy(v, p[j+1] );
	}	

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
	
	if( point < 0 )
		return 0;

	switch( point ) 
	{
		case 0:
		v->x1 = vsx(v, p[0] + ix);
		v->y1 = vsy(v, p[1] + iy);
		break;
		case 1:
		v->x2 = vsx(v, p[2] + ix);
		v->y2 = vsy(v, p[3] + iy);
		break;
		case 2:
		v->x3 = vsx(v,p[4] + ix);
		v->y3 = vsy(v,p[5] + iy);
		break;
		case 3:
		v->x4 = vsx(v,p[6] + ix);
		v->y4 = vsy(v,p[7] + iy);
		break;
	}
	viewport_update_perspective( v, p_cpy );
	if(v->grid)
		viewport_compute_grid(v);
	return 1;
}

int	viewport_external_mouse( void *data, uint8_t *img[3], int sx, int sy, int button, int frontback, int screen_width, int screen_height )
{
	viewport_t *v = (viewport_t*) data;
	if( sx == 0 && sy == 0 && button == 0 )
		return 0;
	if( button == 3 && v->user_ui == 0 )
		return 0;

	int ch = 0;
	int point = -1;
	int i;

	//@ use screen width/height
	float x = (float)sx / ( screen_width / 100.0f );
	float y = (float)sy / ( screen_height / 100.0f );
	double dist = 100.0;
	int	    cx = v->w / 2;
	int	    cy = v->h / 2;
	int	    dx = cx - ( v->ui->sw / 2 );
	int	    dy = cy - ( v->ui->sh / 2 );
	float		scx  = (float) v->w / (float) v->ui->sw;
	float		scy = (float) v->h / (float) v->ui->sh;
	int 	nsx = (sx - dx) * scx;
	int     nsy = (sy - dy) * scy;

	v->usermouse[2] = (float) nsx;
	v->usermouse[3] = (float) nsy;
	v->usermouse[4] = x;
	v->usermouse[5] = y;

	float p_cpy[9];
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

	int j;
	for  ( j = 0 ; j < 8 ; j += 2 ) {
		p_cpy[j] = p[j];
		p_cpy[j+1]=p[j+1];
		p[j]  =  msx(v, p[j] );
		p[j+1]=  msy(v, p[j+1] );
	}	

	float tx = vsx(v,v->usermouse[4]);
	float ty = vsy(v,v->usermouse[5]);

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
		v->save = 1;

	if( button == 0 && point >= 0)
		v->users[ point ] = 2;

	if( button == 0 )
	{
		v->usermouse[0] = x;
		v->usermouse[1] = y;
	}

	if( button == 2 )
	{
		if(v->user_reverse) v->user_reverse = 0; else v->user_reverse = 1;
		ch  = 1;
	}

	if( button == 3 )
	{
		if(v->user_ui) v->user_ui = 0; else v->user_ui = 1;

		if( v->user_ui == 0 )
		{
			viewport_save_settings(v, frontback);
		}
	}

	
	if( button == 6 && point >= 0)
	{
		switch( point )
		{
			case 0:	
				v->x0 = (int32_t)nsx;
				v->y0 = (int32_t)nsy;
				clamp1(v->x0, 0, v->w );
				clamp1(v->y0, 0, v->h );
				break;
			case 1:
				v->w0 = nsx - v->x0;
				v->y0 = nsy;		
				clamp1(v->w0, 0,v->w );
				clamp1(v->y0, 0,v->h );
				break;
			case 2:
				v->w0 = nsx - v->x0;	
				v->h0 = nsy - v->y0;
				clamp1(v->w0, 0,v->w );
				clamp1(v->h0, 0,v->h );
				break;
			case 3:	
				v->w0 = ( v->x0 - nsx ) + v->w0;
				v->x0 = nsx;
				v->h0 = nsy - v->y0;
				clamp1(v->x0, 0,v->w );
				clamp1(v->h0, 0,v->h );
				clamp1(v->w0, 0,v->w );
			break;
		}
		ch = 1;
	}

	if( button == 15 ) {
		v->grid_mode --;
		if(v->grid_mode < 0 ) 
			v->grid_mode = 2;
	} 

	if( button == 5 ) // wheel up
	{
		if(v->grid_mode == 0 ) {
			v->marker_size --;
			if(v->marker_size < 2 )
				v->marker_size = 4;
		} else {
			v->grid_resolution -= GRID_STEP;	
			if(v->grid_resolution < 2 )	
				v->grid_resolution = 2;
			viewport_compute_grid(v);
		}
	}

	if( button == 16 )
	{
		v->grid_mode ++;
		if(v->grid_mode > 2 )
			v->grid_mode = 0;
	}

	if (button == 4 ) // wheel down
	{	
		if(v->grid_mode == 0 ) {
			v->marker_size ++;
			if(v->marker_size > v->w/16)	
				v->marker_size = 4;
		} else {
			v->grid_resolution += GRID_STEP;	
			if(v->grid_resolution > v->w )	
				v->grid_resolution = v->w;
			viewport_compute_grid(v);
		}
	}
	if( button == 7 )
	{
		if( v->grid_val == 0xff )
			v->grid_val = 0;
		else
			v->grid_val = 0xff;
	}


	if(v->save)
	{
		if( button == 12 )
		{
		}
		else if( button == 1 )
		{
			switch( point ) 
			{
				case 0:
					v->x1 = tx;
					v->y1 = ty;
					break;
				case 1:
					v->x2 = tx;
					v->y2 = ty;
					break;
				case 2:
					v->x3 = tx;
					v->y3 = ty;
					break;
				case 3:
					v->x4 = tx;
					v->y4 = ty;
				break;
				
			}
		}
		ch = 1;

	}

	if( ch )
	{
		viewport_update_perspective( v, p_cpy );
		if(v->grid)
			viewport_compute_grid(v);

		return 1;
	}
	return 0;
}


void		viewport_push_frame(void *data, int w, int h, uint8_t *Y, uint8_t *U, uint8_t *V )
{
	viewport_t *v = (viewport_t*) data;
	ui_t	   *u = v->ui;
	VJFrame *srci = yuv_yuv_template( Y, U,V, w,h, PIX_FMT_GRAY8 );
    VJFrame *dsti = yuv_yuv_template( u->buf[0],NULL,NULL,u->sw, u->sh, PIX_FMT_GRAY8); 
  
    yuv_convert_and_scale( u->scaler, srci,dsti );
    free(srci);
    free(dsti);
}

static void		viewport_translate_frame(void *data, uint8_t *plane ) 
{
	viewport_t *v = (viewport_t*) data;
	ui_t	   *u = v->ui;
	int	    cx = v->w / 2;
	int	    cy = v->h / 2;
	int	     w = v->w;
	int	    dx = cx - ( u->sw / 2 );
	int	    dy = cy - ( u->sh / 2 );

	int 		x,y;

	uint8_t		*img = u->buf[0];
	for( y = 0; y < u->sh; y ++ ) {
		for( x = 0; x < u->sw; x ++ ) {
			plane[ (dy + y ) * w + dx + x ] = img[ y * u->sw + x ];
		}
	}
	
}


static	void	viewport_draw_marker(viewport_t *v, int x, int y, int w, int h, uint8_t *plane )
{
	int x1 = x - v->marker_size;
	int y1 = y - v->marker_size;
	int x2 = x + v->marker_size;
	int y2 = y + v->marker_size;

	if( x1 < 0 ) x1 = 0; else if ( x1 > w ) x1 = w;
	if( y1 < 0 ) y1 = 0; else if ( y1 > h ) y1 = h;
	if( x2 < 0 ) x2 = 0; else if ( x2 > w ) x2 = w;
	if( y2 < 0 ) y2 = 0; else if ( y2 > h ) y2 = h;

	unsigned int i,j;
	for( j = x1; j < x2 ; j ++ )
		plane[ y1 * w + j ] = v->grid_val;

	for( i = y1; i < y2; i ++ ) 
	{
		plane[ i * w + x1 ] = v->grid_val;
		plane[ i * w + x2 ] = v->grid_val;
	}

	for( j = x1; j < x2 ; j ++ )
		plane[ y2 * w + j ] = v->grid_val;

}

static	void	viewport_draw_grid(viewport_t *v, int width, int height, uint8_t *plane )
{	
	int x,y;
	grid_t *grid = v->grid;
	int k = 0;
	int n = v->grid_width * v->grid_height; 
	int j = 0;
	for( y = 0; y < v->grid_height; y ++) {	
			k = y * v->grid_width;
			j = k + v->grid_width-1;
			viewport_line( plane, grid[k].x, grid[k].y,
			                     grid[j].x, grid[j].y,
						width,height,
						170);
	}

	k = 0;
	n = (v->grid_height-1) * v->grid_width;
	for( x = 0; x < v->grid_width; x ++ )
	{
		k = x;
		j = n + x;
		viewport_line( plane, grid[k].x, grid[k].y,
				      grid[j].x, grid[j].y,
					width,height,
					170);
	} 
}

static	void	viewport_draw_points(viewport_t *v, int width, int height, uint8_t *plane )
{
	int k;
	for(k = 0; k < (v->grid_width*v->grid_height); k ++ ) 
	{
		int x=v->grid[k].x;
		int y=v->grid[k].y;
		if( x >= 0 && y >= 0 && x < width && y < height )
			plane[y * width + x] = v->grid_val;
	}
}
static	void	viewport_compute_grid( viewport_t *v )
{
	int k = 0;
	int gw = v->w/ v->grid_resolution;
	int gh = v->h/v->grid_resolution;
	v->grid_width = gw;
	v->grid_height = gh;

	int x,y;
	if(v->grid) {
		free(v->grid);
		v->grid = NULL;
	}
	if(!v->grid) {
		v->grid = (grid_t*) vj_malloc(sizeof(grid_t) * gw *gh);
	}
	grid_t *grid = v->grid;

	for(y = 0; y < gh; y ++ )
		for( x = 0; x < gw; x ++ ) {
			point_map_int( v->M, x * v->grid_resolution,
				             y * v->grid_resolution,&(grid[k].x), &(grid[k].y));
			k++;
		}
}


void		viewport_set_marker( void *data, int status )
{
	viewport_t *v = (viewport_t*) data;
	v->snap_marker = status;
	//v->marker_size = 1;
}

static void	viewport_draw_col( void *data, uint8_t *plane, uint8_t *u, uint8_t *V )
{
	viewport_t *v = (viewport_t*) data;
	int	width = v->w;
	int 	height = v->h;

	float wx =(float) v->w / 100.0f;
	float wy =(float) v->h / 100.0f;

	int fx1 = (int)( msx(v,v->x1) *wx );
	int fy1 = (int)( msy(v,v->y1) *wy );
	int fx2 = (int)( msx(v,v->x2) *wx );
	int fy2 = (int)( msy(v,v->y2) *wy );
	int fx3 = (int)( msx(v,v->x3) *wx );
	int fy3 = (int)( msy(v,v->y3) *wy );
	int fx4 = (int)( msx(v,v->x4) *wx );
	int fy4 = (int)( msy(v,v->y4) *wy );

	const uint8_t p = v->grid_val;

	if(v->grid)
		switch(v->grid_mode)
		{
			case 2:
				viewport_draw_grid(v,width,height,plane);
			break;	
			case 1:
				viewport_draw_points(v,width,height,plane);
			break;
		}

	
	viewport_line( plane, fx1, fy1, fx2,fy2,width,height, p);
	viewport_line( plane, fx1, fy1, fx4,fy4,width,height, p );
	viewport_line( plane, fx4, fy4, fx3,fy3,width,height, p );
	viewport_line( plane, fx2, fy2, fx3,fy3,width,height, p );
	
	//@ Project rectangle in v->w * v->h , but scaled to size of >sw >sh
	ui_t *ui = v->ui;
	int	    cx = v->w / 2;
	int	    cy = v->h / 2;
	int	    dx = cx - ( ui->sw / 2 );
	int	    dy = cy - ( ui->sh / 2 );
	float		s  = (float) v->w / (float) v->ui->sw;
	float		sy = (float) v->h / (float) v->ui->sh;
	int vx0 = (v->x0  / s) + dx;
	int vy0 = (v->y0 / sy) + dy;
	int vw0 = v->w0  / s;
	int vh0 = v->h0  / sy;
      
       viewport_line( plane,   v->x0,          v->y0,                  v->x0 + v->w0,   v->y0,          width,height, 110);
       viewport_line( plane,   v->x0+v->w0,     v->y0,                  v->x0 + v->w0,   v->y0 + v->h0,   width,height, 110 );
       viewport_line( plane,   v->x0 + v->w0,   v->y0 + v->h0,           v->x0,          v->y0 + v->h0,   width,height, 110 );
       viewport_line( plane,   v->x0,          v->y0 +v->h0,            v->x0,          v->y0,          width,height, 110);


//* Projection quad
       viewport_line( plane,   vx0,          vy0,                  vx0 + vw0,   vy0,          width,height, 65);
       viewport_line( plane,   vx0+vw0,     vy0,                  vx0 + vw0,   vy0 + vh0,   width,height, 65 );
       viewport_line( plane,   vx0 + vw0,   vy0 + vh0,           vx0,          vy0 + vh0,   width,height, 65 );
       viewport_line( plane,   vx0,          vy0 +vh0,            vx0,          vy0,          width,height, 65);


	draw_point( plane, fx1,fy1, width,height, v->users[0],p );
	draw_point( plane, fx2,fy2, width,height, v->users[1],p );
	draw_point( plane, fx3,fy3, width,height, v->users[2],p );
	draw_point( plane, fx4,fy4, width,height, v->users[3],p );

	 int mx = v->usermouse[0] * wx;
	 int my = v->usermouse[1] * wy;
	 
	viewport_draw_marker(v, mx,my,width,height,plane );
	

	 if( mx >= 0 && my >= 0 && mx <= width && my < height )
	 {
		if( mx >= 0 && my >= 0 && mx < width && my < height )
		{
			if( abs(v->grid_val - plane[my*width+mx]) < 32 )
				plane[my*width+mx] = 0xff - plane[my*width+mx];
			else
				plane[my * width + mx] = v->grid_val;
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

void	viewport_draw_interface_color( void *vdata, uint8_t *img[3] )
{
	viewport_t *v = (viewport_t*) vdata;
	viewport_translate_frame( v, img[0] );
	viewport_draw_col( v, img[0],img[1],img[2] );
}


void	viewport_produce_full_img( void *vdata, uint8_t *img[3], uint8_t *out_img[3] )
{
	viewport_t *v = (viewport_t*) vdata;
	const int len = v->w * v->h;
	register const int w = v->w;
	register uint32_t i,n;
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
		
	vj_frame_clear1( outY,0,len);
	vj_frame_clear1( outU,128,len);
	vj_frame_clear1( outV,128,len);

	for( y = ty1; y < ty2; y ++ )
	{
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
}

void	viewport_produce_bw_img( void *vdata, uint8_t *img[3], uint8_t *out_img[3], int Yonly)
{
	if( !Yonly ) {
		viewport_produce_full_img( vdata, img, out_img );
		return;
	}



	viewport_t *v = (viewport_t*) vdata;
	const int len = v->w * v->h;
	register const int w = v->w;
	register uint32_t i,n;
	const int32_t *map = v->map;
	uint8_t *inY  = img[0];
	uint8_t       *outY = out_img[0];
	inY[len+1] = 0;

	register const	int32_t	tx1 = v->ttx1;
	register const	int32_t tx2 = v->ttx2;
	register const	int32_t	ty1 = v->tty1;
	register const	int32_t ty2 = v->tty2;

	int x,y;
	y  = ty1 * w;
	veejay_memset( outY,0,len);

	for( y = ty1; y < ty2; y ++ )
	{
		for( x = tx1; x < tx2 ; x ++ )
		{
			i = y * w + x;
			n = map[i];
			outY[i] = inY[n];
		}
	}
	y = (v->h - ty2 ) * w;
	x = ty2 * w;
}

#define pack_yuyv_4pixel( y0,u0,y1,v0 ) (\
		( (int) y0 ) & 0xff ) +\
                ( (((int) (u0>>1) ) & 0xff) << 8) +\
                ( ((((int) y1) & 0xff) << 16 )) +\
                ( ((((int) (v0>>1)) & 0xff) << 24 ))

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
	register const	int32_t	tx1 = v->ttx1;
	register const	int32_t tx2 = v->ttx2;
	register const	int32_t	ty1 = v->tty1;
	register const	int32_t ty2 = v->tty2;
	register const int w = v->w;
	register const int uw = v->w >> 1;
	register uint32_t i,x,y;
	register int32_t n,m;

	inY[len+1] = 0;		// "out of range" pixel value 
	inU[len+1] = 128;
	inV[len+1] = 128;

	yuyv_plane_clear( len*2, plane_yuyv); 

	for( y = ty1 ; y < ty2; y ++ )
	{
		for( x = tx1; x < tx2; x += 2 )
		{ // 4 YUYV pixels out, 8 Y in,  16 UV in
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
	register uint32_t n,i,x,y;

	// clear the yuyv plane (black)
	y  = ty1 * w;
	yuyv_plane_clear( len*2, out_img); 

	for( y = ty1 ; y < ty2; y ++ )
	{
		for( x = tx1; x < tx2; x ++ )
		{
			i = y * w + x;
			n = map[ i ];
			outYUYV[3  * i  ] = inY[n];
			outYUYV[3  * i + 1 ] = inV[n];
			outYUYV[3  * i + 3 ] = inU[n];
		}
	}
}


void viewport_render( void *vdata, uint8_t *in[3], uint8_t *out[3],int width, int height, int uv_len )
{
	viewport_t *v = (viewport_t*) vdata;

	if( v->disable ) 
		return;

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
			32 );

	v->user_ui = 0;

	if(! res )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid point locations");
		viewport_destroy( v );
		return NULL;
	}


    	return (void*)v;
}

int	viewport_get_mode( void *vv ) {
	viewport_t *v = (viewport_t*) vv;
	return v->user_ui;
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


static	void	flxml( xmlDocPtr doc, xmlNodePtr cur, float *dst , const xmlChar *name) {
   if(!xmlStrcmp(cur->name, name ) ) {
    xmlChar *xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode,1);
    unsigned char *chTemp = UTF8toLAT1(xmlTemp);
    if (chTemp) {
	 *dst = (float) atof( (char*)chTemp);
	 free(chTemp);
    }
    free(xmlTemp);
  }
}
static	void	ixml( xmlDocPtr doc, xmlNodePtr cur, int *dst , const xmlChar *name) {
   if(!xmlStrcmp(cur->name, name ) ) {
    xmlChar *xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode,1);
    unsigned char *chTemp = UTF8toLAT1(xmlTemp);
    if (chTemp) {
	 *dst = (int) atoi( (char*)chTemp);
	 free(chTemp);
    }
    free(xmlTemp);
  }
}
void	*viewport_load_xml(xmlDocPtr doc, xmlNodePtr cur, void *vv )
{
	viewport_t *vc = (viewport_t*) vv;
	if(!vc || !cur) return NULL;

	viewport_config_t *c = (viewport_config_t*) vj_calloc(sizeof(viewport_config_t));
	//effectIndex++;
	while( cur != NULL ) {
		flxml( doc,cur,&(c->x1),(const xmlChar*) "x1" );
		flxml( doc,cur,&(c->x2),(const xmlChar*)"x2" );
		flxml( doc,cur,&(c->y1),(const xmlChar*)"y1" );
		flxml( doc,cur,&(c->y2),(const xmlChar*)"y2" );
		flxml( doc,cur,&(c->x3),(const xmlChar*)"x3" );
		flxml( doc,cur,&(c->x4),(const xmlChar*)"x4" );
		flxml( doc,cur,&(c->y3),(const xmlChar*)"y3" );
		flxml( doc,cur,&(c->y4),(const xmlChar*)"y4" );
		ixml( doc,cur,&(c->x0),(const xmlChar*)"x0" );
		ixml( doc,cur,&(c->w0),(const xmlChar*)"w0" );
		ixml( doc,cur,&(c->y0),(const xmlChar*)"y0" );
		ixml( doc,cur,&(c->h0),(const xmlChar*)"h0" );
		ixml( doc,cur,&(c->saved_w),(const xmlChar*)"saved_w" );
		ixml( doc,cur,&(c->saved_h),(const xmlChar*)"saved_h" );
		ixml( doc,cur,&(c->reverse),(const xmlChar*)"reverse" );
		ixml( doc,cur,&(c->grid_color),(const xmlChar*)"grid_color" );
		ixml( doc,cur,&(c->grid_resolution),(const xmlChar*)"grid_resolution" );
		ixml( doc,cur,&(c->composite_mode),(const xmlChar*) "compositemode");
		ixml( doc,cur,&(c->colormode),(const xmlChar*) "colormode");	
		ixml( doc,cur,&(c->marker_size),(const xmlChar*) "markersize");
		ixml( doc,cur,&(c->grid_mode),(const xmlChar*) "gridmode");
		cur = cur->next;
	}
	return (void*) c;
}

void 	viewport_save_xml(xmlNodePtr parent,void *vv)
{
	viewport_config_t *vc = (viewport_config_t*) vv;
	if(!vc) return;

    xmlNodePtr node = xmlNewChild(parent, NULL,
		(const xmlChar*) "calibration", 
		NULL );

    char buffer[100];

    sprintf(buffer, "%f", vc->x1);
    xmlNewChild(node, NULL, (const xmlChar *) "x1",
		(const xmlChar *) buffer);
    sprintf(buffer, "%f", vc->y1);
    xmlNewChild(node, NULL, (const xmlChar *) "y1",
		(const xmlChar *) buffer);
  
    sprintf(buffer, "%f", vc->x2);
    xmlNewChild(node, NULL, (const xmlChar *) "x2",
		(const xmlChar *) buffer);
    sprintf(buffer, "%f", vc->y2);
    xmlNewChild(node, NULL, (const xmlChar *) "y2",
		(const xmlChar *) buffer);

    sprintf(buffer, "%f", vc->x3);
    xmlNewChild(node, NULL, (const xmlChar *) "x3",
		(const xmlChar *) buffer);
    sprintf(buffer, "%f", vc->y3);
    xmlNewChild(node, NULL, (const xmlChar *) "y3",
		(const xmlChar *) buffer);
 
   sprintf(buffer, "%f", vc->x4);
    xmlNewChild(node, NULL, (const xmlChar *) "x4",
		(const xmlChar *) buffer);
    sprintf(buffer, "%f", vc->y4);
    xmlNewChild(node, NULL, (const xmlChar *) "y4",
		(const xmlChar *) buffer);

   sprintf(buffer, "%d", vc->saved_w);
    xmlNewChild(node, NULL, (const xmlChar *) "saved_w",
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", vc->saved_h);
    xmlNewChild(node, NULL, (const xmlChar *) "saved_h",
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", vc->reverse);
    xmlNewChild(node, NULL, (const xmlChar *) "reverse",
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", vc->grid_color);
    xmlNewChild(node, NULL, (const xmlChar *) "grid_color",
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", vc->grid_resolution);
    xmlNewChild(node, NULL, (const xmlChar *) "grid_resolution",
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", vc->x0);
    xmlNewChild(node, NULL, (const xmlChar *) "x0",
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", vc->y0);
    xmlNewChild(node, NULL, (const xmlChar *) "y0",
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", vc->w0);
    xmlNewChild(node, NULL, (const xmlChar *) "w0",
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", vc->h0);
    xmlNewChild(node, NULL, (const xmlChar *) "h0",
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", vc->colormode);
    xmlNewChild(node, NULL, (const xmlChar *) "colormode",
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", vc->composite_mode);
    xmlNewChild(node, NULL, (const xmlChar *) "compositemode",
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", vc->marker_size);
    xmlNewChild(node, NULL, (const xmlChar *) "markersize",
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", vc->grid_mode);
    xmlNewChild(node, NULL, (const xmlChar *) "gridmode",
		(const xmlChar *) buffer);
}
