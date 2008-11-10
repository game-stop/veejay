/* veejay - Linux VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef VJ_PIXBUF_H
#define VJ_PIXBUF_H
#include <config.h>
#include <libvje/vje.h>
typedef struct
{	
	int	w;
	int	h;
} veejay_image_t;

#ifdef USE_GDK_PIXBUF
void	vj_picture_init(void *templ);
void	vj_picture_cleanup( void *pic );
VJFrame *vj_picture_get( void *pic );
int	vj_picture_probe( const char *filename );
void	*vj_picture_open( const char *filename, int v_outw, int v_outh, int v_outf );
int	vj_picture_get_width(void *pic);
int	vj_picture_get_height(void *pic);
void	vj_picture_display_formats(void);
char	*vj_picture_get_filename( void *pic );
void *	vj_picture_prepare_save( const char *filename, char *type, int out_w, int out_h);
int	vj_picture_save( void *picture, uint8_t **frame, int w, int h , int fmt );
void  vj_picture_display_formats(void);

veejay_image_t *vj_picture_save_bw_to_memory( uint8_t **frame, int w, int h , int out_w, int out_h, int fmt  );
veejay_image_t *vj_picture_save_to_memory( uint8_t **frame, int w, int h , int out_w, int out_h, int fmt  );
void vj_fastbw_picture_save_to_mem( VJFrame *f, int out_w, int out_h, int fmt );
void vj_fast_picture_save_to_mem(VJFrame *f, int out_w, int out_h, int fmt );

void	vj_picture_free();

#endif


#endif
