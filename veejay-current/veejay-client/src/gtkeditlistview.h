/* Gveejay Reloaded - graphical interface for VeeJay
 * Custom edit decision list editor
 *      (C) 2026 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef GTK_EDIT_LIST_VIEW_H
#define GTK_EDIT_LIST_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GVR_TYPE_EDIT_LIST_VIEW            (gvr_edit_list_view_get_type())
#define GVR_EDIT_LIST_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_EDIT_LIST_VIEW, GvrEditListView))
#define GVR_IS_EDIT_LIST_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_EDIT_LIST_VIEW))
#define GVR_EDIT_LIST_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GVR_TYPE_EDIT_LIST_VIEW, GvrEditListViewClass))
#define GVR_IS_EDIT_LIST_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GVR_TYPE_EDIT_LIST_VIEW))
#define GVR_EDIT_LIST_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GVR_TYPE_EDIT_LIST_VIEW, GvrEditListViewClass))

typedef struct _GvrEditListView GvrEditListView;
typedef struct _GvrEditListViewClass GvrEditListViewClass;

/* index is zero-based. All frame ranges are inclusive. */
typedef struct {
    int index;
    const char *filename;
    int timeline_in;
    int timeline_out;
    int file_in;
    int file_out;
    const char *fourcc;
} GvrEditListSegment;

typedef enum {
    GVR_EDIT_LIST_ACTION_APPEND_FILE = 1,
    GVR_EDIT_LIST_ACTION_APPEND_FILE_AND_SAMPLE,
    GVR_EDIT_LIST_ACTION_SAVE_LIST,
    GVR_EDIT_LIST_ACTION_SAVE_SELECTION,
    GVR_EDIT_LIST_ACTION_CUT,
    GVR_EDIT_LIST_ACTION_COPY,
    GVR_EDIT_LIST_ACTION_PASTE,
    GVR_EDIT_LIST_ACTION_DELETE,
    GVR_EDIT_LIST_ACTION_CROP,
    GVR_EDIT_LIST_ACTION_NEW_SAMPLE,
    GVR_EDIT_LIST_ACTION_MOVE_RANGE,
    GVR_EDIT_LIST_ACTION_COPY_RANGE_TO,
    GVR_EDIT_LIST_ACTION_REFRESH
} GvrEditListAction;

GType      gvr_edit_list_view_get_type(void);
GtkWidget *gvr_edit_list_view_new(void);

void gvr_edit_list_view_clear(GtkWidget *widget);
void gvr_edit_list_view_set_sample(GtkWidget *widget,
                                   int sample_id,
                                   int total_frames,
                                   double fps);
void gvr_edit_list_view_set_segments(GtkWidget *widget,
                                     const GvrEditListSegment *segments,
                                     guint count);
void gvr_edit_list_view_set_playhead(GtkWidget *widget, int frame);
void gvr_edit_list_view_set_selection(GtkWidget *widget,
                                      int in_frame,
                                      int out_frame,
                                      gboolean active);
void gvr_edit_list_view_clear_selection(GtkWidget *widget);
void gvr_edit_list_view_set_clipboard(GtkWidget *widget,
                                      gboolean valid,
                                      gboolean was_cut,
                                      int frame_count,
                                      int source_in,
                                      int source_out);
void gvr_edit_list_view_set_editable(GtkWidget *widget, gboolean editable);

int      gvr_edit_list_view_get_playhead(GtkWidget *widget);
gboolean gvr_edit_list_view_get_selection(GtkWidget *widget,
                                          int *in_frame,
                                          int *out_frame);
gboolean gvr_edit_list_view_get_selected_segment(GtkWidget *widget,
                                                  int *segment_index);

/*
 * Signals:
 *
 * "action-requested"
 *   void callback(GtkWidget *widget,
 *                 int action,
 *                 int in_frame,
 *                 int out_frame,
 *                 int position,
 *                 gpointer user_data)
 *
 * "seek-requested"
 *   void callback(GtkWidget *widget, int frame, gpointer user_data)
 *
 * "selection-changed"
 *   void callback(GtkWidget *widget,
 *                 int in_frame,
 *                 int out_frame,
 *                 gboolean active,
 *                 gpointer user_data)
 *
 * "segment-selected"
 *   void callback(GtkWidget *widget, int segment_index, gpointer user_data)
 *
 * "separator-added" / "separator-moved"
 *   void callback(GtkWidget *widget, int separator_id, int frame, gpointer user_data)
 *
 * "separator-removed"
 *   void callback(GtkWidget *widget, int separator_id, gpointer user_data)
 */

G_END_DECLS

#endif
