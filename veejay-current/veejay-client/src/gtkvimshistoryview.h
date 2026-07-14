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
#ifndef GTK_VIMS_HISTORY_VIEW_H
#define GTK_VIMS_HISTORY_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GVR_TYPE_VIMS_HISTORY_VIEW            (gvr_vims_history_view_get_type())
#define GVR_VIMS_HISTORY_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_VIMS_HISTORY_VIEW, GvrVimsHistoryView))
#define GVR_IS_VIMS_HISTORY_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_VIMS_HISTORY_VIEW))
#define GVR_VIMS_HISTORY_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GVR_TYPE_VIMS_HISTORY_VIEW, GvrVimsHistoryViewClass))
#define GVR_IS_VIMS_HISTORY_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GVR_TYPE_VIMS_HISTORY_VIEW))
#define GVR_VIMS_HISTORY_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GVR_TYPE_VIMS_HISTORY_VIEW, GvrVimsHistoryViewClass))

#define GVR_VIMS_HISTORY_DND_TARGET "application/x-gveejay-vims-message"

typedef struct _GvrVimsHistoryView GvrVimsHistoryView;
typedef struct _GvrVimsHistoryViewClass GvrVimsHistoryViewClass;

GType      gvr_vims_history_view_get_type(void);
GtkWidget *gvr_vims_history_view_new(void);
void       gvr_vims_history_view_push(GtkWidget *widget,
                                      int vims_id,
                                      const char *message,
                                      const char *description);
void       gvr_vims_history_view_clear(GtkWidget *widget);

G_END_DECLS

#endif
