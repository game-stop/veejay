/* reloaded - Linux VeeJay
 *           (C) 2002-2019 Niels Elburg <nwelburg@gmail.com> 
 *           (C) 2019 Jerome Blanchi <d.j.a.y@free.fr> 

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

/*
  Utils for gtk
 */

#include "utils-gtk.h"

void vj_gtk_context_get_color (GtkStyleContext *context, const gchar *property, GtkStateFlags state, GdkRGBA *color)
{
    GdkRGBA *c;
    gtk_style_context_save (context);
    gtk_style_context_get (context, state, property, &c, NULL);
    *color = *c;
    gdk_rgba_free (c);
    gtk_style_context_restore (context);
}
