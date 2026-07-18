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
#ifndef GVR_GTK_VIMS_VIEW_H
#define GVR_GTK_VIMS_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GVR_TYPE_VIMS_VIEW            (gvr_vims_view_get_type())
#define GVR_VIMS_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_VIMS_VIEW, GvrVimsView))
#define GVR_IS_VIMS_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_VIMS_VIEW))
#define GVR_VIMS_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GVR_TYPE_VIMS_VIEW, GvrVimsViewClass))
#define GVR_IS_VIMS_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GVR_TYPE_VIMS_VIEW))
#define GVR_VIMS_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GVR_TYPE_VIMS_VIEW, GvrVimsViewClass))

typedef struct _GvrVimsView GvrVimsView;
typedef struct _GvrVimsViewClass GvrVimsViewClass;

typedef enum {
    GVR_VIMS_VIEW_RUN = 0,
    GVR_VIMS_VIEW_ADD,
    GVR_VIMS_VIEW_BIND,
    GVR_VIMS_VIEW_UNBIND,
    GVR_VIMS_VIEW_NEW_BUNDLE,
    GVR_VIMS_VIEW_UPDATE_BUNDLE,
    GVR_VIMS_VIEW_DELETE_BUNDLE,
    GVR_VIMS_VIEW_LOAD,
    GVR_VIMS_VIEW_SAVE,
    GVR_VIMS_VIEW_CLEAR_EDITOR,
    GVR_VIMS_VIEW_CLEAR_RESPONSE,
    GVR_VIMS_VIEW_N_ACTIONS
} GvrVimsViewAction;

GType      gvr_vims_view_get_type(void);
GtkWidget *gvr_vims_view_new(void);

void       gvr_vims_view_clear_namespace(GtkWidget *widget);
void       gvr_vims_view_append_namespace(GtkWidget *widget,
                                          int event_id,
                                          const char *description,
                                          const char *format,
                                          int params);
gboolean   gvr_vims_view_get_selected_namespace(GtkWidget *widget,
                                                int *event_id,
                                                gchar **format,
                                                int *params);

void       gvr_vims_view_clear_actions(GtkWidget *widget);
void       gvr_vims_view_append_action(GtkWidget *widget,
                                       int event_id,
                                       const char *description,
                                       const char *format,
                                       const char *args,
                                       const char *modifier_text,
                                       const char *key_text,
                                       int modifier,
                                       int key,
                                       gboolean is_bundle,
                                       const char *bundle_text);
gboolean   gvr_vims_view_set_bundle_binding(GtkWidget *widget,
                                            int event_id,
                                            const char *modifier_text,
                                            const char *key_text,
                                            int modifier,
                                            int key);
gboolean   gvr_vims_view_get_selected_action(GtkWidget *widget,
                                             int *event_id,
                                             int *key,
                                             int *modifier,
                                             gchar **args,
                                             gboolean *is_bundle,
                                             gchar **bundle_text);
gboolean   gvr_vims_view_select_action(GtkWidget *widget,
                                       int event_id,
                                       int key,
                                       int modifier,
                                       gboolean is_bundle);
void       gvr_vims_view_clear_data(GtkWidget *widget);

void       gvr_vims_view_clear_midi(GtkWidget *widget);
void       gvr_vims_view_append_midi(GtkWidget *widget,
                                     const char *mapping_key,
                                     int event_type,
                                     int parameter,
                                     int extra,
                                     const char *event_text,
                                     const char *parameter_text,
                                     const char *mode_text,
                                     const char *source_widget,
                                     const char *message);
gboolean   gvr_vims_view_get_selected_midi(GtkWidget *widget,
                                           gchar **mapping_key,
                                           int *event_type,
                                           int *parameter);

void       gvr_vims_view_set_command(GtkWidget *widget,
                                      const char *command,
                                      const char *hint,
                                      gboolean sensitive);
gchar     *gvr_vims_view_get_command(GtkWidget *widget);
void       gvr_vims_view_set_last_request(GtkWidget *widget,
                                           const char *request);

GtkWidget *gvr_vims_view_get_response_view(GtkWidget *widget);
GtkWidget *gvr_vims_view_get_editor_view(GtkWidget *widget);
GtkWidget *gvr_vims_view_get_workspace_notebook(GtkWidget *widget);
void       gvr_vims_view_set_action_sensitive(GtkWidget *widget,
                                               GvrVimsViewAction action,
                                               gboolean sensitive);
void       gvr_vims_view_show_workspace(GtkWidget *widget, int page);
void       gvr_vims_view_set_response(GtkWidget *widget, const char *text);
void       gvr_vims_view_clear_response(GtkWidget *widget);
void       gvr_vims_view_set_editor(GtkWidget *widget, const char *text);
gchar     *gvr_vims_view_get_editor(GtkWidget *widget);
void       gvr_vims_view_clear_editor(GtkWidget *widget);
void       gvr_vims_view_append_editor(GtkWidget *widget, const char *text);

G_END_DECLS

#endif
