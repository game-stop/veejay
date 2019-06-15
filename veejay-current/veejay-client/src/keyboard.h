/* Gveejay Reloaded - graphical interface for VeeJay
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

#ifndef GDKSDL
#define GDKSDL

#include <gdk/gdkkeysyms.h>
#include <SDL/SDL_keysym.h>
#include <glib.h>
#include <stdio.h>

int		sdl2gdk_key( int sdl_key );
int		gdk2sdl_key( int gdk_key );
int		gdk2sdl_mod( int gdk_mod );
gchar		*sdlkey_by_id( int sdl_key );
gchar		*sdlmod_by_id( int sdk_mod );
gchar		*gdkkey_by_id( int gdk_key );
gchar		*gdkmod_by_id(int gdkmod);
int		sdlmod_by_name( gchar *name );
int		sdlkey_by_name( gchar *name );
int		gdk2sdl_mod( int gdk_mod );

gboolean	key_snooper(GtkWidget *w, GdkEventKey *event, gpointer user_data);

#endif
