/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
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

#include <gtk/gtk.h>
#include "keyboard.h"

int		sdl2gdk_key(int sdl_key)
{
	return 0;
}

int		gdk2sdl_key(int gdk_key)
{
	int i;
	for ( i = 0; key_translation_table_t[i].title != NULL ; i ++ )
	{
		if( gdk_key == key_translation_table_t[i].gdk_sym )
			return key_translation_table_t[i].sdl_sym;
	}
	return 0;
}

int		gdk2sdl_mod( int gdk_mod )
{
	int i;
	for ( i = 0; modifier_translation_table_t[i].title != NULL ; i ++ )
	{
		if( gdk_mod == modifier_translation_table_t[i].gdk_mod )
			return modifier_translation_table_t[i].sdl_mod;
	}
	return 0;
}

int		sdlmod_by_name( gchar *name )
{
	int i;
	for ( i = 0; modifier_translation_table_t[i].title != NULL ; i ++ )
	{
		if( g_utf8_collate(name,
				modifier_translation_table_t[i].title) == 0)
			return modifier_translation_table_t[i].sdl_mod;
	}

	return 0;
}

int		sdlkey_by_name( gchar *name )
{
	int i;
	for ( i = 0; key_translation_table_t[i].title != NULL ; i ++ )
	{
		if( g_utf8_collate(name,
				key_translation_table_t[i].title) == 0)
			return key_translation_table_t[i].sdl_sym;
	}
	return 0;
}

gchar		*sdlkey_by_id( int sdl_key )
{
	int i;
	gchar *ret = NULL;
	for ( i = 0; key_translation_table_t[i].title != NULL ; i ++ )
	{
		if( sdl_key == key_translation_table_t[i].sdl_sym )
			return key_translation_table_t[i].title;
	}
	return ret;
}

gchar		*sdlmod_by_id( int sdl_mod )
{
	int i;
	gchar *ret = NULL;
	for ( i = 0; modifier_translation_table_t[i].title != NULL ; i ++ )
	{
		if( sdl_mod == modifier_translation_table_t[i].sdl_mod )
			return modifier_translation_table_t[i].title;
	}
	return ret;
}
gchar		*gdkmod_by_id( int gdk_mod )
{
	int i;
	for( i = 0; modifier_translation_table_t[i].title != NULL ; i ++ )
	{
		if( gdk_mod == modifier_translation_table_t[i].gdk_mod ||
			gdk_mod & modifier_translation_table_t[i].gdk_mod )
			return modifier_translation_table_t[i].title;
	}
	return NULL;
}

gchar		*gdkkey_by_id( int gdk_key )
{
	int i;
	for( i = 0; key_translation_table_t[i].title != NULL ; i ++ )
	{
		if( gdk_key == key_translation_table_t[i].gdk_sym )
			return key_translation_table_t[i].title;
	}
	return NULL;
}

static	void	key_func(gboolean pressed, guint16 unicode, guint16 keymod)
{


}
/*
Key snooper functions are called before normal event delivery.
They can be used to implement custom key event handling.
grab_widget¿:	the widget to which the event will be delivered.
event¿:	the key event.
func_data¿:	the func_data supplied to gtk_key_snooper_install().
Returns¿:	TRUE to stop further processing of event, FALSE to continue.
*/

gboolean	key_snooper(GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
	printf(" %d, %d, %d\n",
		event->type,
		event->keyval,
		event->state );
	return FALSE;
}
// gtk_key_snooper_install
