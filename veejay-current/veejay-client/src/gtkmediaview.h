/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2026 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef GTK_MEDIA_VIEW_H
#define GTK_MEDIA_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GVR_TYPE_MEDIA_VIEW            (gvr_media_view_get_type())
#define GVR_MEDIA_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_MEDIA_VIEW, GvrMediaView))
#define GVR_IS_MEDIA_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_MEDIA_VIEW))
#define GVR_MEDIA_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GVR_TYPE_MEDIA_VIEW, GvrMediaViewClass))
#define GVR_IS_MEDIA_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GVR_TYPE_MEDIA_VIEW))
#define GVR_MEDIA_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GVR_TYPE_MEDIA_VIEW, GvrMediaViewClass))

#define GVR_MEDIA_VIEW_DND_TARGET "application/x-gveejay-media-file"

typedef struct _GvrMediaView GvrMediaView;
typedef struct _GvrMediaViewClass GvrMediaViewClass;

GType      gvr_media_view_get_type(void);
GtkWidget *gvr_media_view_new(void);

void gvr_media_view_begin_update(GtkWidget *widget);
void gvr_media_view_append(GtkWidget *widget, const char *filename);
void gvr_media_view_end_update(GtkWidget *widget);
void gvr_media_view_set_error(GtkWidget *widget, const char *message);
void gvr_media_view_clear(GtkWidget *widget);

/*
 * Signals:
 *
 * "refresh-requested"
 *   void callback(GtkWidget *widget, gpointer user_data)
 *
 * "file-activated"
 *   void callback(GtkWidget *widget, const char *filename, gpointer user_data)
 */

G_END_DECLS

#endif
