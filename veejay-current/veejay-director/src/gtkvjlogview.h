/* director - Linux VeeJay - GVeejay GTK+-3/Glade User Interface 
 * (C) 2026 Niels Elburg <nwelburg@gmail.com>
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
#ifndef GTK_VJ_LOG_VIEW_H
#define GTK_VJ_LOG_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GVR_TYPE_LOG_VIEW (gvr_log_view_get_type())
G_DECLARE_FINAL_TYPE(GvrLogView, gvr_log_view, GVR, LOG_VIEW, GtkTextView)

GtkWidget *gvr_log_view_new(void);
void gvr_log_view_append(GvrLogView *view, const gchar *text);
void gvr_log_view_clear(GvrLogView *view);
void gvr_log_view_set_max_lines(GvrLogView *view, guint max_lines);

G_END_DECLS

#endif
