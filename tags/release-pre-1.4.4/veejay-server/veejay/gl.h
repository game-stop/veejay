
#ifndef VJGL
#define VJGL
/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
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

//extern void veejay_gl_init( int fs, int w, int h, int x, int y);
void	*x_display_init(void *ptr);
void	x_display_close(void *dctx);
int		x_display_set_fullscreen( void *dctx, int status );
int	x_display_width(void *ptr );
int	x_display_height(void *ptr);
int	x_display_push(void *dctx, uint8_t **data, int width, int height, int out );
void	x_display_resize( int x, int y, int w, int h );
void       x_display_event( void *dctx, int w, int h );
int                x_display_get_fs( void *dctx );
void	 x_display_open(void *dctx, int w, int h);
void	x_display_mouse_update( void *dctx, int *a, int *b, int *c, int *d );
void		x_display_mouse_grab( void *dctx, int a, int b, int c, int d );
void	*x_get_display(void *ptr);
int	x_display_push_yvu(void *dctx, int width, int height, int out );
uint8_t	*x_display_get_buffer( void *dctx );
#endif
