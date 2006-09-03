/* veejay - Linux VeeJay OpenGL display driver
 *           (C) MPlayer www.mplayerhq.hu/ 
 *           (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
 *
 * Copy-pasted Mplayer source code.
 *
 * MPlayer's x11_common.c , vo_gl.c , vo_gl2.c
 * video_out_gl.c, X11/OpenGL interface
 * based on video_out_x11 by Aaron Holtzman,
 * and WS opengl window manager by Pontscho/Fresh!
 *
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <veejay/veejay.h>
#include <veejay/defs.h>
#include <libplugger/utility.h>
#include <ffmpeg/avcodec.h>
#include <libyuv/yuvconv.h>
#define	ERROR_MSG_LEN 100
#define XA_INIT(m,x) XA##x = XInternAtom(m, #x, False)

#undef	USE_ARB

#if defined (GL_ARB_fragment_program) || defined( GL_ATI_fragment_shader)
#define USE_ARB 1	
#endif

/** \defgroup X11 OpenGL Video Display
 *
 * Veejay blits video frames to a OpenGL video window
 * The OpenGL video window comes from MPlayer -vo gl2
 * This is the only video output display used.
 */

//! \struct TexSquare
struct TexSquare
{
  GLubyte *texture;	/*!< Texture */
  GLuint texobj;	/*!< Texture ID*/
  int isTexture;	/*!< Is Texture or not */
  GLfloat fx1, fy1, fx2, fy2, fx3, fy3, fx4, fy4; /*!< Vertex */
  GLfloat xcov, ycov;   /*!< Translate */
  int isDirty;		/*!< Dirty */
  int dirtyXoff, dirtyYoff, dirtyWidth, dirtyHeight; /*!< Dirty vertex */
};

//! \typedef display_ctx
typedef struct
{
	XImage	*img;
	XWindowAttributes attr;
	char	*name;
	long	window;
	int	depth;
	int	bpp;
	unsigned int mask;
	Display	*display;
	int	screen;
	int	disp_w;
	int	disp_h;
	int	display_ready;
	int	texture[2];
	Window	root_win;
	Window	win;
	GLXContext	*glx_ctx;
	uint8_t	*data;
	struct	TexSquare *texgrid;
	GLint	gl_internal_format;
	GLint	gl_bitmap_format;
	GLint	gl_bitmap_type;
	int	texnumx;
	int	texnumy;
	int	fs_state;
	GLfloat	texpercx;
	GLfloat	texpercy;
	XVisualInfo *info;
	GLuint  prog;
	int	raw_line_len;
	uint8_t *ref_data;
} display_ctx;
static Atom XA_NET_WM_PID;
static Atom XA_NET_WM_STATE;
static Atom XA_NET_WM_STATE_FULLSCREEN;
#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */

static int                  wsGLXAttrib[] = { GLX_RGBA,
                                       GLX_RED_SIZE,1,
                                       GLX_GREEN_SIZE,1,
                                       GLX_BLUE_SIZE,1,
                                       GLX_DOUBLEBUFFER,
                                       None };

static void resetTexturePointers(display_ctx *ctx);
static	int init_shader();
static	void	flip_page(display_ctx *ctx);

static void glAdjustAlignment(int stride) {
  GLint gl_alignment;
  if (stride % 8 == 0)
    gl_alignment=8;
  else if (stride % 4 == 0)
    gl_alignment=4;
  else if (stride % 2 == 0)
    gl_alignment=2;
  else
    gl_alignment=1;
  glPixelStorei (GL_UNPACK_ALIGNMENT, gl_alignment);
}

static	int	x11_err_( Display *display, XErrorEvent *event )
{
	char msg[ERROR_MSG_LEN];
	XGetErrorText( display, event->error_code, (char*) &msg, ERROR_MSG_LEN );\
	veejay_msg(0, "X11 Error: '%s'", msg );
	veejay_msg(0, "Type: %x, display: %x, resource ID: %x serial: %x",
			event->type,event->display,event->resourceid,event->serial);
	veejay_msg(0, "Error code: %x, request code: %x, minor code: %x",
			event->error_code, event->request_code, event->minor_code );
	exit(0);
}

static 	void	x11_screen_saver_disable( Display *display )
{
	int interval, prefer_blank, allow_exp;
#ifdef HAVE_XDPMS
	int x;
	if( DPMSQueryExtension( display, &x, &x ) )
	{
		BOOL onoff;
		CARD16 state;
		DPMSInfo( display, &state, &onoff );
		if(onoff)
		{
			veejay_msg(1, "Disable DPMS");
			Status stat = DPMSDisable( display );
			veejay_msg(1, "DPMS status = %d", stat);
		}
	}
#endif
}
static 	void	resize(int x, int y, int w, int h )
{
	glViewport(0,0,x,y);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho( 0,w,h,0,-1,1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void	x_display_resize( int x, int y, int w, int h )
{
	resize(x,y,w,h );
}	

static	inline int	x_pwr_two(int n)
{
	int 	p = 1;
	while( p < n )
		p <<= 1;
	return p;
}

static GLint getInternalFormat(display_ctx *ctx)
{
  int r_sz, g_sz, b_sz, a_sz;
  int rgb_sz;
  if (glXGetConfig(ctx->display, ctx->info, GLX_RED_SIZE, &r_sz) != 0) r_sz = 0;
  if (glXGetConfig(ctx->display, ctx->info, GLX_GREEN_SIZE, &g_sz) != 0) g_sz = 0;
  if (glXGetConfig(ctx->display, ctx->info, GLX_BLUE_SIZE, &b_sz) != 0) b_sz = 0;
  if (glXGetConfig(ctx->display, ctx->info, GLX_ALPHA_SIZE, &a_sz) != 0) a_sz = 0;

  rgb_sz=r_sz+g_sz+b_sz;
  if(rgb_sz<=0) rgb_sz=24;

  if(r_sz==3 && g_sz==3 && b_sz==2 && a_sz==0)
	  return GL_R3_G3_B2;
  if(r_sz==4 && g_sz==4 && b_sz==4 && a_sz==0)
	  return GL_RGB4;
  if(r_sz==5 && g_sz==5 && b_sz==5 && a_sz==0)
	  return GL_RGB5;
  if(r_sz==8 && g_sz==8 && b_sz==8 && a_sz==0)
	  return GL_RGB8;
  if(r_sz==10 && g_sz==10 && b_sz==10 && a_sz==0)
	  return GL_RGB10;
  if(r_sz==2 && g_sz==2 && b_sz==2 && a_sz==2)
	  return GL_RGBA2;
  if(r_sz==4 && g_sz==4 && b_sz==4 && a_sz==4)
	  return GL_RGBA4;
  if(r_sz==5 && g_sz==5 && b_sz==5 && a_sz==1)
	  return GL_RGB5_A1;
  if(r_sz==8 && g_sz==8 && b_sz==8 && a_sz==8)
	  return GL_RGBA8;
  if(r_sz==10 && g_sz==10 && b_sz==10 && a_sz==2)
	  return GL_RGB10_A2;
  return GL_RGB;
}

static inline void CalcFlatPoint(display_ctx *ctx,int x,int y,GLfloat *px,GLfloat *py)
{
  *px=(float)x*ctx->texpercx;
  if(*px>1.0) *px=1.0;
  *py=(float)y*ctx->texpercy;
  if(*py>1.0) *py=1.0;
}
struct gl_name_map_struct {
  GLint value;
  char *name;
};

#undef MAP
#define MAP(a) {a, #a}
static const struct gl_name_map_struct gl_name_map[] = {
  // internal format
  MAP(GL_R3_G3_B2), MAP(GL_RGB4), MAP(GL_RGB5), MAP(GL_RGB8),
  MAP(GL_RGB10), MAP(GL_RGB12), MAP(GL_RGB16), MAP(GL_RGBA2),
  MAP(GL_RGBA4), MAP(GL_RGB5_A1), MAP(GL_RGBA8), MAP(GL_RGB10_A2),
  MAP(GL_RGBA12), MAP(GL_RGBA16), MAP(GL_LUMINANCE8),

  // format
  MAP(GL_RGB), MAP(GL_RGBA), MAP(GL_RED), MAP(GL_GREEN), MAP(GL_BLUE),
  MAP(GL_ALPHA), MAP(GL_LUMINANCE), MAP(GL_LUMINANCE_ALPHA),
  MAP(GL_COLOR_INDEX),
  // rest 1.2 only
#ifdef GL_VERSION_1_2
  MAP(GL_BGR), MAP(GL_BGRA),
#endif

  //type
  MAP(GL_BYTE), MAP(GL_UNSIGNED_BYTE), MAP(GL_SHORT), MAP(GL_UNSIGNED_SHORT),
  MAP(GL_INT), MAP(GL_UNSIGNED_INT), MAP(GL_FLOAT), MAP(GL_DOUBLE),
  MAP(GL_2_BYTES), MAP(GL_3_BYTES), MAP(GL_4_BYTES),
  // rest 1.2 only
#ifdef GL_VERSION_1_2
  MAP(GL_UNSIGNED_BYTE_3_3_2), MAP(GL_UNSIGNED_BYTE_2_3_3_REV),
  MAP(GL_UNSIGNED_SHORT_5_6_5), MAP(GL_UNSIGNED_SHORT_5_6_5_REV),
  MAP(GL_UNSIGNED_SHORT_4_4_4_4), MAP(GL_UNSIGNED_SHORT_4_4_4_4_REV),
  MAP(GL_UNSIGNED_SHORT_5_5_5_1), MAP(GL_UNSIGNED_SHORT_1_5_5_5_REV),
  MAP(GL_UNSIGNED_INT_8_8_8_8), MAP(GL_UNSIGNED_INT_8_8_8_8_REV),
  MAP(GL_UNSIGNED_INT_10_10_10_2), MAP(GL_UNSIGNED_INT_2_10_10_10_REV),
#endif
  {0, 0}
};
#undef MAP

const char *glValName(GLint value)
{
  int i = 0;

  while (gl_name_map[i].name) {
    if (gl_name_map[i].value == value)
      return gl_name_map[i].name;
    i++;
  }
  return "Unknown format!";
}
static int initTextures(display_ctx *ctx, int img_w, int img_h)
{
  int e_x, e_y, s, i=0;
  int x=0, y=0;
  GLint format=0;
  GLenum err;
  struct TexSquare *tsq=0;

  ctx->gl_bitmap_format = GL_RGB;
  ctx->gl_bitmap_type = GL_UNSIGNED_BYTE;
  
 ctx->gl_internal_format = getInternalFormat(ctx);

  int texture_width = img_w;
  int texture_height = img_h;
  e_x=0; s=1;
  while (s<texture_width)
  { s*=2; e_x++; }
  texture_width=s;

  e_y=0; s=1;
  while (s<texture_height)
  { s*=2; e_y++; }
  texture_height=s;

  
  /* Test the max texture size */
  do
  {
    glTexImage2D (GL_PROXY_TEXTURE_2D, 0,
		  ctx->gl_internal_format,
		  texture_width, texture_height,
		  0, ctx->gl_bitmap_format, ctx->gl_bitmap_type, NULL); 

    glGetTexLevelParameteriv
      (GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);

    if (format != ctx->gl_internal_format)
    {
      veejay_msg(0, "[gl2] Needed texture [%dx%d] too big, trying ",
		texture_height, texture_width);

      if (texture_width > texture_height)
      {
	e_x--;
	texture_width = 1;
	for (i = e_x; i > 0; i--)
	  texture_width *= 2;
      }
      else
      {
	e_y--;
	texture_height = 1;
	for (i = e_y; i > 0; i--)
	  texture_height *= 2;
      }

      veejay_msg(0, "[%dx%d] !\n", texture_height, texture_width);

      if(texture_width < 64 || texture_height < 64)
      {
      	veejay_msg(0, "[gl2] Give up .. usable texture size not avaiable, or texture config error !");
	return -1;
      }
    }
  }
  while (format != ctx->gl_internal_format && texture_width > 1 && texture_height > 1);

  ctx->texnumx = img_w / texture_width;
  if ((img_w % texture_width) > 0)
    ctx->texnumx++;

  ctx->texnumy = img_h / texture_height;
  if ((img_h % texture_height) > 0)
    ctx->texnumy++;

  /* Allocate the texture memory */

  ctx->texpercx = (GLfloat) texture_width / (GLfloat) img_w;
  if (ctx->texpercx > 1.0)
    ctx->texpercx = 1.0;

  ctx->texpercy = (GLfloat) texture_height / (GLfloat) img_h;
  if (ctx->texpercy > 1.0)
    ctx->texpercy = 1.0;

  if (ctx->texgrid)
    free(ctx->texgrid);
  ctx->texgrid = (struct TexSquare *)
    calloc (ctx->texnumx * ctx->texnumy, sizeof (struct TexSquare));

  ctx->raw_line_len = img_w * 3;

  for (y = 0; y < ctx->texnumy; y++)
  {
    for (x = 0; x < ctx->texnumx; x++)
    {
      tsq = ctx->texgrid + y * ctx->texnumx + x;

      if (x == ctx->texnumx - 1 && img_w % texture_width)
	tsq->xcov =
	  (GLfloat) (img_w % texture_width) / (GLfloat) texture_width;
      else
	tsq->xcov = 1.0;

      if (y == ctx->texnumy - 1 && img_h % texture_height)
	tsq->ycov =
	  (GLfloat) (img_h % texture_height) / (GLfloat) texture_height;
      else
	tsq->ycov = 1.0;

      CalcFlatPoint (ctx,x, y, &(tsq->fx1), &(tsq->fy1));
      CalcFlatPoint (ctx,x + 1, y, &(tsq->fx2), &(tsq->fy2));
      CalcFlatPoint (ctx,x + 1, y + 1, &(tsq->fx3), &(tsq->fy3));
      CalcFlatPoint (ctx,x, y + 1, &(tsq->fx4), &(tsq->fy4));

      tsq->isDirty=GL_TRUE;
      tsq->isTexture=GL_FALSE;
      tsq->texobj=0;
      tsq->dirtyXoff=0; tsq->dirtyYoff=0; tsq->dirtyWidth=-1; tsq->dirtyHeight=-1;

      glGenTextures (1, &(tsq->texobj));

      glBindTexture (GL_TEXTURE_2D, tsq->texobj);
      err = glGetError ();
      if(err==GL_INVALID_ENUM)
      {
	veejay_msg(0, "GLERROR glBindTexture (glGenText) := GL_INVALID_ENUM, texnum x=%d, y=%d, texture=%d\n", x, y, tsq->texobj);
      } 

      if(glIsTexture(tsq->texobj) == GL_FALSE)
      {
	veejay_msg(0, "GLERROR ain't a texture (glGenText): texnum x=%d, y=%d, texture=%d\n",
		x, y, tsq->texobj);
      } else {
        tsq->isTexture=GL_TRUE;
      }

      glTexImage2D (GL_TEXTURE_2D, 0,
		    ctx->gl_internal_format,
		    texture_width, texture_height,
		    0, ctx->gl_bitmap_format, ctx->gl_bitmap_type, NULL); 

      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, 1.0);

      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

      glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    }	/* for all texnumx */
  }  /* for all texnumy */

  ctx->texture[0] = texture_width;
  ctx->texture[1] = texture_height;
  resetTexturePointers (ctx);
  
  return 0;
}

static void resetTexturePointers(display_ctx *ctx)
{
  unsigned char *imageSource = ctx->data;
  unsigned char *texdata_start, *line_start;
  struct TexSquare *tsq = ctx->texgrid;
  int x=0, y=0;

  line_start = (unsigned char *) imageSource;

  for (y = 0; y < ctx->texnumy; y++)
  {
    texdata_start = line_start;
    for (x = 0; x < ctx->texnumx; x++)
    {
      tsq->texture = texdata_start;
      texdata_start += ctx->texture[0] * 3;
      tsq++;
    }	/* for all texnumx */
    line_start += ctx->texture[1] * ctx->raw_line_len;
  }  /* for all texnumy */
}

static void setupTextureDirtyArea(display_ctx *ctx,int x, int y, int w,int h, int img_w, int img_h)
{
  struct TexSquare *square;
  int xi, yi, wd, ht, wh, hh;
  int wdecr, hdecr, xh, yh;
    
  wdecr=w; hdecr=h; xh=x; yh=y;

  for (yi = 0; hdecr>0 && yi < ctx->texnumy; yi++)
  {
    if (yi < ctx->texnumy - 1)
      ht = ctx->texture[1];
    else
      ht = img_h - ctx->texture[1] * yi;

    xh =x;
    wdecr =w;

    for (xi = 0; wdecr>0 && xi < ctx->texnumx; xi++)
    {
        square = ctx->texgrid + yi * ctx->texnumx + xi;

	if (xi < ctx->texnumx - 1)
	  wd = ctx->texture[0];
	else
	  wd = img_w - ctx->texture[0] * xi;

	if( 0 <= xh && xh < wd &&
            0 <= yh && yh < ht
          )
        {
        	square->isDirty=GL_TRUE;

		wh=(wdecr<wd)?wdecr:wd-xh;
		if(wh<0) wh=0;

		hh=(hdecr<ht)?hdecr:ht-yh;
		if(hh<0) hh=0;

		if(xh<square->dirtyXoff)
			square->dirtyXoff=xh;

		if(yh<square->dirtyYoff)
			square->dirtyYoff=yh;

		square->dirtyWidth = wd-square->dirtyXoff;
		square->dirtyHeight = ht-square->dirtyYoff;
		
		wdecr-=wh;

		if ( xi == ctx->texnumx - 1 )
			hdecr-=hh;
        }

	xh-=wd;
	if(xh<0) xh=0;
    }
    yh-=ht;
    if(yh<0) yh=0;
  }
}


static void drawTextureDisplay (display_ctx *ctx)
{
  struct TexSquare *square;
  int x, y/*, xoff=0, yoff=0, wd, ht*/;
  GLenum err;

  glColor3f(1.0,1.0,1.0);

  for (y = 0; y < ctx->texnumy; y++)
  {
    for (x = 0; x < ctx->texnumx; x++)
    {
      square = ctx->texgrid + y * ctx->texnumx + x;

      if(square->isTexture==GL_FALSE)
      {
        veejay_msg(0, "[gl2] ain't a texture(update): texnum x=%d, y=%d, texture=%d",
	  	x, y, square->texobj);
      	continue;
      }

      glBindTexture (GL_TEXTURE_2D, square->texobj);
      err = glGetError ();
      if(err==GL_INVALID_ENUM)
      {
	veejay_msg(0, "GLERROR glBindTexture := GL_INVALID_ENUM, texnum x=%d, y=%d, texture=%d\n", x, y, square->texobj);
      }
	      else if(err==GL_INVALID_OPERATION) {
		veejay_msg(0, "GLERROR glBindTexture := GL_INVALID_OPERATION, texnum x=%d, y=%d, texture=%d\n", x, y, square->texobj);
	      }

      if(glIsTexture(square->texobj) == GL_FALSE)
      {
        square->isTexture=GL_FALSE;
	veejay_msg(0, "GLERROR ain't a texture(update): texnum x=%d, y=%d, texture=%d\n",
		x, y, square->texobj);
      }

      if(square->isDirty)
      {
	glTexSubImage2D (GL_TEXTURE_2D, 0, 
		 square->dirtyXoff, square->dirtyYoff,
		 square->dirtyWidth, square->dirtyHeight,
		 ctx->gl_bitmap_format, ctx->gl_bitmap_type, square->texture);
        square->isDirty=GL_FALSE;
        square->dirtyXoff=0; square->dirtyYoff=0; square->dirtyWidth=-1; square->dirtyHeight=-1;
      }

	glBegin(GL_QUADS);

	glTexCoord2f (0, 0);
	glVertex2f (square->fx1, square->fy1);

	glTexCoord2f (0, square->ycov);
	glVertex2f (square->fx4, square->fy4);

	glTexCoord2f (square->xcov, square->ycov);
	glVertex2f (square->fx3, square->fy3);

	glTexCoord2f (square->xcov, 0);
	glVertex2f (square->fx2, square->fy2);

	glEnd();
    } /* for all texnumx */
  } /* for all texnumy */

  /* YES - let's catch this error ... 
   */
  (void) glGetError ();
}


void x_fullscreen(display_ctx *ctx, int action)
{

        XEvent xev;

        /* init X event structure for _NET_WM_FULLSCREEN client msg */
        xev.xclient.type = ClientMessage;
        xev.xclient.serial = 0;
        xev.xclient.send_event = True;
        xev.xclient.message_type = XInternAtom(ctx->display,
                                               "_NET_WM_STATE", False);
        xev.xclient.window = ctx->win;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = action;
        xev.xclient.data.l[1] = XInternAtom(ctx->display,
                                            "_NET_WM_STATE_FULLSCREEN",
                                            False);
        xev.xclient.data.l[2] = 0;
        xev.xclient.data.l[3] = 0;
        xev.xclient.data.l[4] = 0;

        /* finally send that damn thing */
        if (!XSendEvent(ctx->display, DefaultRootWindow(ctx->display), False,
                        SubstructureRedirectMask | SubstructureNotifyMask,
                        &xev))
        {
		veejay_msg(0, "error in fs");
        }
}

int		x_display_set_fullscreen( void *dctx, int status )
{
	display_ctx *ctx = (display_ctx*) dctx;
	if( ctx->fs_state && status == 0 )
	{
		x_fullscreen(ctx,_NET_WM_STATE_REMOVE );
		ctx->fs_state = 0;
		return 1;
	}
	else
	if ( !ctx->fs_state && status == 1 )
	{
		x_fullscreen(ctx,_NET_WM_STATE_ADD);
		ctx->fs_state = 1;
		return 1;
	}
	return 0;
}

static	int	x_display_event_update( display_ctx *ctx , int *dw, int *dh )
{
	XEvent Event;
	KeySym keySym;
	static XComposeStatus stat;
	while( XPending( ctx->display ) )
	{
		XNextEvent( ctx->display, &Event );

		switch(Event.type)
		{
			case ConfigureNotify:
				*dw = Event.xconfigure.width;
				*dh = Event.xconfigure.height;
				return 1;
				break;
			case KeyPress:
				{
					KeySym key_sym = XKeycodeToKeysym( ctx->display,
							Event.xkey.keycode,0);
					switch(key_sym)
					{
						case XK_F:
						case XK_f:
				 		  if( ctx->fs_state )
				   		  {
						    x_fullscreen(ctx,_NET_WM_STATE_REMOVE );
						    ctx->fs_state = 0;
	 			   		   }
				  		   else
				   	   	   {	
					  	    x_fullscreen(ctx,_NET_WM_STATE_ADD);
					   	     ctx->fs_state = 1;
						   }
						   break;
				    		case XK_Escape:
						   break;
   				   	}
				}
				break;
			default:
				break;	
		}
	}
	return 0;
}

static	int	x_display_init_gl( display_ctx *ctx, int w, int h )
{
	int i;
	
	ctx->texture[0] = x_pwr_two ( w );
	ctx->texture[1] = x_pwr_two ( h );

	
	
	if(initTextures(ctx,w,h)<0)
		return 0;

	glDisable( GL_BLEND );	
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);

	glPixelStorei( GL_UNPACK_ROW_LENGTH, w );
	glAdjustAlignment( w * 3 );

	glEnable(GL_TEXTURE_2D);

        glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  	glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	ctx->data = (uint8_t*)vj_malloc( ctx->texture[0] * ctx->texture[1] * 3 );
	for( i = 0; i < (ctx->texture[0] * ctx->texture[1] * 3); i +=3 )
	{
		ctx->data[i+0] = 16;
		ctx->data[i+1] = 128;
		ctx->data[i+2] = 128;
	}
	
	ctx->prog = init_shader();
	
	resize(w,h,w,h);
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT ); 

	//drawTextureDisplay(ctx);
	glFlush();
	flip_page(ctx);
        char *glVersion = glGetString( GL_VERSION );
        
        veejay_msg(2,"\tYUV -> RGB  :    %s", (ctx->prog ? "Hardware" : "Software" ));
        veejay_msg(2,"\tVendor      :    %s", glGetString( GL_VENDOR ) );
        veejay_msg(2,"\tRenderer    :    %s", glGetString( GL_RENDERER ));                              
        veejay_msg(2,"\tOpenGL version:  %s", glVersion );              
                
	return 1;
}


void	*x_display_init(void)
{
	display_ctx *ctx = (display_ctx*) vj_malloc(sizeof(display_ctx));
	memset(ctx, 0,sizeof(display_ctx));
   
	XSetErrorHandler( x11_err_ );

	ctx->name = XDisplayName(":0.0");
	ctx->display = XOpenDisplay( ctx->name );
	if(!ctx->display)
	{
		veejay_msg(0, "Error opening the X11 display %s", ctx->name );
		return NULL;
	}
	XA_INIT(ctx->display,_NET_WM_PID);
	
	ctx->screen = DefaultScreen( ctx->display );
	ctx->root_win = RootWindow( ctx->display, ctx->screen );
	ctx->disp_w = DisplayWidth( ctx->display, ctx->screen );
	ctx->disp_h = DisplayHeight( ctx->display, ctx->screen );

	XGetWindowAttributes( ctx->display, ctx->root_win, &(ctx->attr) );
	ctx->depth = ctx->attr.depth;
	if(ctx->depth == 15 || ctx->depth == 16 || ctx->depth == 24 || ctx->depth == 32 )
	{
		ctx->img = XGetImage( ctx->display, ctx->root_win,0,0,1,1,AllPlanes, ZPixmap );
		
	}
	veejay_msg(2, "OpenGL Video Window");
	if( ctx->img )
	{
		ctx->bpp = ctx->img->bits_per_pixel;
		//ctx->mask =
		//	ctx->img->red_mask | ctx->img->green_mask | ctx->img->blue_mask;
		veejay_msg(2, "\tX11 Color mask: %X (R:%lx G:%lx B:%lx) bpp=%d, depth=%d",
				ctx->mask,
				ctx->img->red_mask,
				ctx->img->green_mask,
				ctx->img->blue_mask,
			 	ctx->bpp,
			 	ctx->depth );
		XDestroyImage( ctx->img );
	}

	x11_screen_saver_disable(ctx->display);
        veejay_msg(2,"\tX11 Display:   %s", ctx->name);
        veejay_msg(2,"\tX11 Window :   %dx%d", ctx->disp_w, ctx->disp_h );
        veejay_msg(2,"\tColor depth:   %d", ctx->depth);
        veejay_msg(2,"\tBits per pixel:%d", ctx->bpp);

	
	return ctx;	
}


void	x_display_close(void *dctx)
{
	display_ctx *ctx = (display_ctx*) dctx;


	glXMakeCurrent( ctx->display,None,NULL);
	glXDestroyContext( ctx->display,ctx->glx_ctx );

	XSetErrorHandler( NULL );
	XUnmapWindow( ctx->display, ctx->win );
	XFree(ctx->info);
	XDestroyWindow( ctx->display,ctx->win );
	XCloseDisplay( ctx->display );

	
	if(ctx->data)
		free(ctx->data);
	if(ctx->texgrid)
		free(ctx->texgrid);
	free(ctx);
}

void	x_display_open(void *dctx, int w, int h)
{
	display_ctx *ctx = (display_ctx*) dctx;
	XSizeHints hint;
	XEvent event;
	XClassHint wmclass;
	pid_t pid = getpid();
	memset(&event,0,sizeof(XEvent));
	memset(&hint,0,sizeof(XSizeHints));
	wmclass.res_name = "gl";
	wmclass.res_class = "Veejay";
	
	hint.x = 0;
	hint.y = 0;
	hint.width = w;
	hint.height = h;
	hint.flags = PPosition | PSize;

	ctx->info = glXChooseVisual( ctx->display, ctx->screen,
			wsGLXAttrib );

	unsigned long xswamask = CWBackingStore | CWBorderPixel;
	XSetWindowAttributes xswa;

	Colormap cmap = XCreateColormap( ctx->display,ctx->root_win,
			ctx->info->visual, AllocNone );
	if( cmap != CopyFromParent )
	{
		xswa.colormap = cmap;
		xswamask |= CWColormap;
	}
	xswa.background_pixel = 0;
	xswa.border_pixel = 0;
	xswa.backing_store = Always;
	xswa.bit_gravity = StaticGravity;

	ctx->win = XCreateWindow( ctx->display, ctx->root_win,
			0,0,w,h,0, ctx->depth, CopyFromParent, ctx->info->visual, xswamask, &xswa );
	XSetForeground( ctx->display,
			XCreateGC( ctx->display, ctx->win,0,0 ),
			WhitePixel( ctx->display, ctx->screen )
			
			);

	XSetClassHint( ctx->display, ctx->win, &wmclass );
	XChangeProperty( ctx->display, ctx->win,XA_NET_WM_PID,
		XA_CARDINAL,32, PropModeReplace,(unsigned char*) &pid,1);	

	long event_mask = StructureNotifyMask | KeyPressMask;

	XSelectInput( ctx->display, ctx->win, event_mask );

	XSetStandardProperties( ctx->display, ctx->win,
		"veejay", "veejay", None, NULL,0, &hint );

	XMapWindow( ctx->display , ctx->win );
	do
	{
		XNextEvent( ctx->display, &event );	
	} while( event.type != MapNotify || event.xmap.event != ctx->win );

//	XSelectInput( ctx->display, ctx->win, NoEventMask );

	XSync( ctx->display, False );
	
	ctx->glx_ctx = glXCreateContext(
		ctx->display, ctx->info, NULL, True );		
	if(!ctx->glx_ctx)
	{
		veejay_msg(0, "Cannot create GL context");
		exit(0);
	}
	if(!glXMakeCurrent( ctx->display, ctx->win, ctx->glx_ctx ))
	{
		veejay_msg(0, "Failed to set GLX context");
		glXDestroyContext( ctx->display, ctx->glx_ctx );
		XFree(ctx->info);
		exit(0);
	}

	if(!x_display_init_gl( ctx, w, h ) )
	{
		veejay_msg(0, "Cannot initialize GL context");
		exit(0);
	}
	ctx->display_ready = 1;	
}

static	void	flip_page(display_ctx *ctx)
{
	drawTextureDisplay(ctx);
	
	glFinish();
	glXSwapBuffers( ctx->display, ctx->win );
}

//http://www.evl.uic.edu/rlk/howto/yuv/
static	int init_shader()
{
#ifdef USE_ARB
	GLuint modulateProg = 0;

 	const char *modulateYUV =
	 "!!ARBfp1.0\n"
	 "TEMP R0;\n"
	 "TEX R0, fragment.texcoord[0], texture[0], 2D; \n"

  	 "ADD R0, R0, {-0.0625, -0.5, -0.5, 0.0}; \n"
   	 "DP3 result.color.x, R0, {1.164,  1.596,  0.0}; \n"   
  	 "DP3 result.color.y, R0, {1.164, -0.813, -0.391}; \n" 
  	 "DP3 result.color.z, R0, {1.164,  0.0,    2.018}; \n" 
  	 "MOV result.color.w, R0.w; \n"  

	 "END"
	 ;

	glGenProgramsARB(1, &modulateProg);
	glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, modulateProg);
	glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
			 strlen(modulateYUV), (const GLubyte *)modulateYUV);

	int error = glGetError();

	if( error != GL_NO_ERROR )
	{
		veejay_msg(0,"GL Error: 0x%x, GL_PROGRAM_ERROR_STRING_ARB: %s",
				error, (char*) glGetString(GL_PROGRAM_ERROR_STRING_ARB));
		return 0;
	}
	if(glIsProgramARB(modulateProg))
		return modulateProg;
#endif
	return 0;
}
int	x_display_hw_accel( void *dctx )
{
	display_ctx *ctx = (display_ctx*) dctx;
	if(ctx->prog)
		return 1;
	return 0;
}

void	x_display_push(void *dctx, VJFrame *frame )
{
	display_ctx *ctx = (display_ctx*) dctx;
	
	if(!ctx->prog)
	{
		util_convertrgba24( frame->data,
			frame->width,
			frame->height,
			PIX_FMT_YUV444P,
			0,
			ctx->data );
	}
	else
	{
		yuv_planar_to_packed_444yvu( frame, ctx->data );	
		glEnable(GL_FRAGMENT_PROGRAM_ARB );
	}
	
	resetTexturePointers( ctx );
	setupTextureDirtyArea(ctx,
			0,0,
			frame->width,
			frame->height,
			frame->width,
			frame->height
			);
	
	int tw = ctx->texture[0];
	int th = ctx->texture[1];

	glBegin( GL_QUADS );
	    glTexCoord2f(0,0);glVertex2i(0,0);
 	    glTexCoord2f(0,1);glVertex2i(0,th);
	    glTexCoord2f(1,1);glVertex2i(tw ,th );
	    glTexCoord2f(1,0);glVertex2i( tw,0);
	glEnd();

	flip_page(ctx);

	int dw = frame->width;
	int dh = frame->height;
	if(x_display_event_update( ctx,&dw,&dh ))
		resize( dw,dh, frame->width,frame->height);
}

