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
#ifndef GTK_MULTI_TRACK_EDIT_H
#define GTK_MULTI_TRACK_EDIT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GVR_TYPE_MULTI_TRACK_EDIT            (gvr_multi_track_edit_get_type())
#define GVR_MULTI_TRACK_EDIT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_MULTI_TRACK_EDIT, GvrMultiTrackEdit))
#define GVR_IS_MULTI_TRACK_EDIT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_MULTI_TRACK_EDIT))
#define GVR_MULTI_TRACK_EDIT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GVR_TYPE_MULTI_TRACK_EDIT, GvrMultiTrackEditClass))
#define GVR_IS_MULTI_TRACK_EDIT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GVR_TYPE_MULTI_TRACK_EDIT))
#define GVR_MULTI_TRACK_EDIT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GVR_TYPE_MULTI_TRACK_EDIT, GvrMultiTrackEditClass))

#define GVR_MULTI_TRACK_EDIT_MAX_TRACKS 16

#define GVR_MULTI_TRACK_SOURCE_DND_TARGET \
    "application/x-gveejay-source-v1"

typedef struct _GvrMultiTrackEdit GvrMultiTrackEdit;
typedef struct _GvrMultiTrackEditClass GvrMultiTrackEditClass;

typedef struct {
    int slot;
    int sample_id;
    int sample_type;
    int project_in;
    int project_out;
} GvrMultiTrackMasterClip;

typedef struct {
    guint id;
    int sample_id;
    int sample_type;
    int project_in;
    int project_out;
    int source_in;
    int source_length;
    int repeat_period;
    int repeat_until;
    int speed;
    int loop_type;
    const char *title;
} GvrMultiTrackClip;

typedef struct {
    int project_frame;
    int order;
    int vims_id;
    const char *label;
    const char *message;
} GvrMultiTrackEvent;

GType      gvr_multi_track_edit_get_type(void);
GtkWidget *gvr_multi_track_edit_new(int max_tracks);
void gvr_multi_track_edit_set_connection_topology(
                                      GtkWidget *widget,
                                      gboolean connected,
                                      const char *local_host,
                                      int local_port,
                                      gboolean backend_is_master,
                                      gboolean upstream_info_known,
                                      gboolean upstream_configured,
                                      const char *upstream_host,
                                      int upstream_port,
                                      gboolean vims_forwarding);

void gvr_multi_track_edit_set_shape_catalog(GtkWidget *widget,
                                             const char *const *names,
                                             guint count);

void gvr_multi_track_edit_set_project(GtkWidget *widget,
                                      int bank,
                                      unsigned int revision,
                                      int total_frames,
                                      double fps);
void gvr_multi_track_edit_set_master_clips(
                                      GtkWidget *widget,
                                      const GvrMultiTrackMasterClip *clips,
                                      guint count);
void gvr_multi_track_edit_set_track(GtkWidget *widget,
                                    int track,
                                    gboolean connected,
                                    const char *hostname,
                                    int port);
void gvr_multi_track_edit_clear_track(GtkWidget *widget, int track);
void gvr_multi_track_edit_set_seekability(GtkWidget *widget,
                                           gboolean seekable,
                                           const char *reason);
void gvr_multi_track_edit_set_track_status(GtkWidget *widget,
                                           int track,
                                           int play_mode,
                                           int source_id,
                                           int source_type,
                                           int frame,
                                           int total_frames,
                                           int speed,
                                           int fps_x100,
                                           int loop_type,
                                           gboolean fx_enabled,
                                           gboolean audio_muted,
                                           gboolean sequence_active,
                                           int sequence_bank,
                                           int sequence_slot,
                                           gboolean stream_buffer_supported,
                                           gboolean stream_buffer_enabled,
                                           int stream_buffer_capacity,
                                           int stream_buffer_filled,
                                           int stream_buffer_position);
void gvr_multi_track_edit_set_track_preview(GtkWidget *widget,
                                            int track,
                                            GdkPixbuf *pixbuf);
GdkPixbuf *gvr_multi_track_edit_ref_track_preview(GtkWidget *widget,
                                                  int track);
void gvr_multi_track_edit_set_track_preview_enabled(GtkWidget *widget,
                                                    int track,
                                                    gboolean enabled);
void gvr_multi_track_edit_set_current_control(GtkWidget *widget, int track);
void gvr_multi_track_edit_set_project_master(GtkWidget *widget, int track);
void gvr_multi_track_edit_set_selected_track(GtkWidget *widget, int track);
int  gvr_multi_track_edit_get_selected_track(GtkWidget *widget);
void gvr_multi_track_edit_set_playhead(GtkWidget *widget,
                                       int bank,
                                       int slot,
                                       int project_frame,
                                       gboolean active);
void gvr_multi_track_edit_set_track_clips(GtkWidget *widget,
                                          int track,
                                          const GvrMultiTrackClip *clips,
                                          guint count);
void gvr_multi_track_edit_set_track_events(GtkWidget *widget,
                                           int track,
                                           const GvrMultiTrackEvent *events,
                                           guint count);
void gvr_multi_track_edit_set_transition_buses(GtkWidget *widget,
                                                int bus_a_track,
                                                int bus_b_track,
                                                int active_bus);
void gvr_multi_track_edit_set_transition_state(GtkWidget *widget,
                                               gboolean active,
                                               int progress);
void gvr_multi_track_edit_set_track_drift(GtkWidget *widget,
                                          int track,
                                          int drift_frames,
                                          int drift_millis,
                                          gboolean correcting);
void gvr_multi_track_edit_set_drift_lock(GtkWidget *widget, gboolean enabled);
void gvr_multi_track_edit_clear_pending_source(GtkWidget *widget);

/*
 * Signals:
 *
 * "track-selected"
 *   void callback(GtkWidget *widget, int track, gpointer user_data)
 *
 * "switch-requested"
 *   void callback(GtkWidget *widget, int track, gpointer user_data)
 *
 * "preview-toggled"
 *   void callback(GtkWidget *widget, int track, gboolean enabled, gpointer user_data)
 *
 * "seek-requested"
 *   void callback(GtkWidget *widget, int frame, gpointer user_data)
 *
 * "source-play-requested"
 *   void callback(GtkWidget *widget, int track, int sample_id,
 *                 int sample_type, gpointer user_data)
 *
 * "sequence-source-insert-requested"
 *   void callback(GtkWidget *widget, int track, int bank,
 *                 int insertion_slot, int sample_id, int sample_type,
 *                 gpointer user_data)
 *
 * "reveal-sequence-slot-requested"
 *   void callback(GtkWidget *widget, int bank, int slot, gpointer user_data)
 *
 * "reveal-source-requested"
 *   void callback(GtkWidget *widget, int sample_id, int sample_type, gpointer user_data)
 *
 * "resync-requested"
 *   void callback(GtkWidget *widget, int track, gpointer user_data)
 *
 * "transition-source-selected"
 *   void callback(GtkWidget *widget, int track, int bus, gpointer user_data)
 *   bus 0 assigns the source to Bus A; bus 1 assigns it to Bus B.
 *
 * "transition-requested"
 *   void callback(GtkWidget *widget, int target_track,
 *                 int duration_frames, int method, int shape,
 *                 gpointer user_data)
 *
 * "drift-lock-toggled"
 *   void callback(GtkWidget *widget, gboolean enabled, gpointer user_data)
 */

G_END_DECLS

#endif
