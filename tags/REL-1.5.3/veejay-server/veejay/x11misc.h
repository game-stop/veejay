/* veejay - Linux VeeJay
 * 	     (C) 2002-2006 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef X11MISC
#define X11MISC
void	x11_enable_screensaver( void *display );
void	x11_disable_screensaver( void *display );
void	x11_misc_init();
void	x11_misc_set_border( void *display, void *window, int status );

void	x11_move( void *display, void *window );
void	x11_info(void *display);

void	x11_user_select( int n );

#endif

