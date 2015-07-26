 /*
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
 */

#include <config.h>
#ifdef HAVE_SDL_TTF
#include <stdint.h>
#include <SDL_ttf.h>
#include <veejay/vj-sdl-font.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libyuv/mmx.h>
#include <libyuv/mmx_macros.h>
#define DEFAULT_FONT ".veejay/default.ttf"
#define MAX_LINES 10

extern char* vj_font_default();

typedef struct {
	int front;
	int rear;
	int capacity;
	uint8_t **history;
	int	*width;
	int	*height;
} cq;

typedef struct
{
	void *font;
	uint8_t pos;
	cq	*q;	
} vj_sdl_font_t;

static inline void _overlay_text( uint8_t *dst, uint8_t *a, uint8_t *b, const int len, uint32_t ialpha)
{
#ifndef HAVE_ASM_MMX
	unsigned int op1 = (ialpha > 255) ? 255 : ialpha;
   	unsigned int op0 = 255 - op1;
	unsigned int i;
	    for( i = 0; i < size; i ++ )
        	dst[i] = (op0 * a[i] + op1 * b[i] ) >> 8;
#else
        unsigned int i;

        ialpha |= ialpha << 16;

        __asm __volatile
                ("\n\t pxor %%mm6, %%mm6"
                 ::);

        for (i = 0; i < len; i += 4) {
                __asm __volatile
                        ("\n\t movd %[alpha], %%mm3"
                         "\n\t movd %[src2], %%mm0"
                         "\n\t psllq $32, %%mm3"
                         "\n\t movd %[alpha], %%mm2"
                         "\n\t movd %[src1], %%mm1"
                         "\n\t por %%mm3, %%mm2"
                         "\n\t punpcklbw %%mm6, %%mm0"  
                         "\n\t punpcklbw %%mm6, %%mm1"  
                         "\n\t psubsw %%mm1, %%mm0"     
                         "\n\t pmullw %%mm2, %%mm0"     
                         "\n\t psrlw $8, %%mm0"        
                         "\n\t paddb %%mm1, %%mm0"     
                         "\n\t packuswb %%mm0, %%mm0\n\t"
                         "\n\t movd %%mm0, %[dest]\n\t"
                         : [dest] "=m" (*(dst + i))
                         : [src1] "m" (*(a + i))
                         , [src2] "m" (*(b + i))
                         , [alpha] "m" (ialpha));
        }
#endif
}

static inline void _overlay_text_uv( uint8_t *dst, uint8_t *a, const int len, uint32_t ialpha)
{
#ifndef HAVE_ASM_MMX
	unsigned int op1 = (ialpha > 255) ? 255 : ialpha;
   	unsigned int op0 = 255 - op1;
	unsigned int i;
	    for( i = 0; i < size; i ++ )
        	dst[i] = (op0 * a[i] + op1 * b[i] ) >> 8;
#else
        unsigned int i;
	uint8_t B[8] = { 128,128,128,128,128,128,128,128 };
	uint8_t *b = B;
        ialpha |= ialpha << 16;

        __asm __volatile
                ("\n\t pxor %%mm6, %%mm6"
                 ::);

        for (i = 0; i < len; i += 4) {
                __asm __volatile
                        ("\n\t movd %[alpha], %%mm3"
                         "\n\t movd %[src2], %%mm0"
                         "\n\t psllq $32, %%mm3"
                         "\n\t movd %[alpha], %%mm2"
                         "\n\t movd %[src1], %%mm1"
                         "\n\t por %%mm3, %%mm2"
                         "\n\t punpcklbw %%mm6, %%mm0"  
                         "\n\t punpcklbw %%mm6, %%mm1"  
                         "\n\t psubsw %%mm1, %%mm0"     
                         "\n\t pmullw %%mm2, %%mm0"     
                         "\n\t psrlw $8, %%mm0"        
                         "\n\t paddb %%mm1, %%mm0"     
                         "\n\t packuswb %%mm0, %%mm0\n\t"
                         "\n\t movd %%mm0, %[dest]\n\t"
                         : [dest] "=m" (*(dst + i))
                         : [src1] "m" (*(a + i))
                         , [src2] "m" (*(b))
                         , [alpha] "m" (ialpha));
        }
#endif
}


static	void *_try_font( char *path )
{
	void *res = TTF_OpenFontIndex( path, 10, 0 ); //FIXME never freed
	if(!res) {
		return NULL;
	}
	return res;
}

void	*vj_sdl_font_init(int w, int h)
{
	char path[1024];

	if( TTF_Init() == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Error while initializing SDL_ttf: %s\n", TTF_GetError() );
		return NULL;
	}

	vj_sdl_font_t *f = vj_calloc( sizeof( vj_sdl_font_t) );

	snprintf( path, sizeof(path), "%s/%s", getenv("HOME"), DEFAULT_FONT );
	f->font = _try_font( path );
	if( f->font == NULL ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Please copy a default truetype font to %s", path );
		snprintf( path, sizeof(path), "%s", vj_font_default());
		veejay_msg(VEEJAY_MSG_INFO, "Loading OSL font %s", path );
		f->font = _try_font( path );
	}

	if(!f->font) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error openining default font %s %s\n",path, TTF_GetError());
		free(f);
		return NULL;
	}

	f->q = vj_calloc(sizeof(cq));
	f->q->capacity = MAX_LINES;
	f->q->front = -1;
	f->q->rear = -1;
	f->q->history = (uint8_t**) vj_calloc( sizeof(uint8_t*) * f->q->capacity );
	f->q->width = (int*) vj_calloc(sizeof(int) * f->q->capacity );
	f->q->height= (int*) vj_calloc(sizeof(int) * f->q->capacity );
	return f;
}

static int	_q_line(cq *q, uint8_t *ptr, int w, int h )
{
	if( q->rear == (q->capacity-1)) {
		uint8_t *old = q->history[0];
		if(old) free(old);
		int i;
		for( i = 1; i < q->capacity; i ++ ) {
			q->history[i-1] = q->history[i];
			q->width[i-1] = q->width[i];
			q->height[i-1] = q->height[i];
		}

		q->history[ q->rear ] = ptr;
		q->width[ q->rear ] = w;
		q->height[ q->rear ] = h;
		return 1;
	}

	q->rear = (q->rear + 1) % q->capacity;
	q->history[ q->rear ] = ptr;
	q->width[ q->rear ] = w;
	q->height[ q->rear ] = h;
	if( q->front == -1 ) {
		q->front = q->rear;
	}
	return 1;
}

void	vj_sdl_font_free(void *font)
{
	vj_sdl_font_t *f = (vj_sdl_font_t*) font;
	if( font ) {
		TTF_CloseFont( f->font );
		if( f->q ) {
			int i;
			for( i = 0; i < MAX_LINES; i ++ ) {
				if( f->q->history[i] )
				    free(f->q->history[i]);
				f->q->history[i] = NULL;
			}
			if(f->q->history)
				free(f->q->history);
			if(f->q->width)
				free(f->q->width);
			if(f->q->height)
				free(f->q->height);
			free(f->q);
		}
		free(font);
	}
	TTF_Quit();
}


void*	vj_sdl_draw_log_line( void *f, uint8_t r, uint8_t g, uint8_t b, uint8_t a, const char *line)
{
	vj_sdl_font_t *font = (vj_sdl_font_t*) f;
	SDL_Color col = {r,g,b,a};
	SDL_Surface *text_surface = TTF_RenderText_Solid( font->font, line, col );
	return text_surface;
}

int	vj_sdl_draw_to_buffer( void *f, unsigned int w, unsigned int h )
{
	vj_sdl_font_t *font = (vj_sdl_font_t*) f;
	char *line = veejay_msg_ringfetch();
	if( line == NULL )
		return 0;

	SDL_Surface *text = (SDL_Surface*)
		vj_sdl_draw_log_line( f, 255,255,255,255, line );
	
	SDL_Color *colors = text->format->palette->colors;
	uint8_t   *pixels = text->pixels;
	unsigned int i,j;

	uint8_t *dst = (uint8_t*) vj_calloc( sizeof(uint8_t) * w * text->h );

	for( i = 0; i < text->h-1; i ++ ){
		for( j = 0; j < text->w; j ++ ) {
			uint8_t idx = pixels[ (i * text->pitch) +j ];
			dst[ (i *  w)+j ] = colors[idx].r;
		}
	}

	_q_line( font->q, dst, w,text->h );

	free(text);
	free(line);

	return 1;
}


void	vj_sdl_font_logging( void *f, uint8_t *planes[3], int w, int h )
{
	vj_sdl_font_t *font = (vj_sdl_font_t*) f;
	cq *q = font->q;
	int i;
	int offset_h = 0;
	int n = q->rear;
	int uw = w >> 1;
	unsigned int opacity = 220;

	unsigned int th = 0;
	for( i = 0; i <= n; i ++ )
		th += q->height[i];
	
	offset_h = h - th - 32;

	for( i = 0; i <= n; i ++ ) {
//		veejay_memcpy( planes[0] + (offset_h * w),
//			 	q->history[i],
//				q->width[i] * q->height[i] );

		_overlay_text( planes[0] + (offset_h*w), q->history[i], planes[0] + (offset_h*w), q->width[i] * q->height[i], 150 );
		_overlay_text_uv( planes[1] + (offset_h*uw), planes[1] + (offset_h*uw), uw * q->height[i], opacity );
		_overlay_text_uv( planes[2] + (offset_h*uw), planes[2] + (offset_h*uw), uw * q->height[i], opacity );

		offset_h += q->height[i];
	}

#ifdef HAVE_ASM_MMX
        __asm __volatile(_EMMS"       \n\t"
                         SFENCE"     \n\t"
                        :::"memory");   
        
#endif

}

#endif
