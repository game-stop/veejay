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
#ifndef GTK_VIMS_PATTERN_VIEW_H
#define GTK_VIMS_PATTERN_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GVR_TYPE_VIMS_PATTERN_VIEW            (gvr_vims_pattern_view_get_type())
#define GVR_VIMS_PATTERN_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_VIMS_PATTERN_VIEW, GvrVimsPatternView))
#define GVR_IS_VIMS_PATTERN_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_VIMS_PATTERN_VIEW))
#define GVR_VIMS_PATTERN_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GVR_TYPE_VIMS_PATTERN_VIEW, GvrVimsPatternViewClass))
#define GVR_IS_VIMS_PATTERN_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GVR_TYPE_VIMS_PATTERN_VIEW))
#define GVR_VIMS_PATTERN_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GVR_TYPE_VIMS_PATTERN_VIEW, GvrVimsPatternViewClass))

#define GVR_VIMS_PATTERN_BANKS       4
#define GVR_VIMS_PATTERN_SLOTS       100
#define GVR_VIMS_PATTERN_COLUMNS     8
#define GVR_VIMS_PATTERN_MESSAGE_MAX 256

#define GVR_VIMS_PATTERN_HAS_HOLD    (1u << 0)
#define GVR_VIMS_PATTERN_HAS_VIMS    (1u << 1)
#define GVR_VIMS_PATTERN_SAMPLE_BANK (-1)
#define GVR_VIMS_PATTERN_STREAM_BANK   (-2)
#define GVR_VIMS_PATTERN_SEQUENCE_BANK (-3)

#define GVR_VIMS_PATTERN_TRANSPORT_FORCE (1u << 0)
#define GVR_VIMS_PATTERN_TRANSPORT_PLAY  (1u << 1)

typedef struct _GvrVimsPatternView GvrVimsPatternView;
typedef struct _GvrVimsPatternViewClass GvrVimsPatternViewClass;
typedef struct {
    guint flags;
    guint event_count;
    guint row_count;
} GvrVimsPatternSummary;
typedef const char *(*GvrVimsPatternDescriptionLookup)(int vims_id,
                                                       gpointer user_data);

GType      gvr_vims_pattern_view_get_type(void);
GtkWidget *gvr_vims_pattern_view_new(void);
void       gvr_vims_pattern_view_clear_target(GtkWidget *widget);
void       gvr_vims_pattern_view_set_description_lookup(
               GtkWidget *widget,
               GvrVimsPatternDescriptionLookup lookup,
               gpointer user_data,
               GDestroyNotify destroy_notify);

void gvr_vims_pattern_view_bind_cell(GtkWidget *widget,
                                     int bank,
                                     int slot,
                                     int sample_id,
                                     int sample_type);
void gvr_vims_pattern_view_select_cell(GtkWidget *widget,
                                       int bank,
                                       int slot,
                                       int sample_id,
                                       int sample_type,
                                       int frame_count);
void gvr_vims_pattern_view_select_sample(GtkWidget *widget,
                                         int sample_id,
                                         int frame_count);
void gvr_vims_pattern_view_select_bank(GtkWidget *widget,
                                       int bank,
                                       int frame_count);
void gvr_vims_pattern_view_set_live_position(GtkWidget *widget,
                                             int bank,
                                             int slot,
                                             int frame,
                                             gboolean active);

int      gvr_vims_pattern_view_get_edit_step(GtkWidget *widget);
void     gvr_vims_pattern_view_set_edit_step(GtkWidget *widget, int step);
gboolean gvr_vims_pattern_view_get_learning(GtkWidget *widget);
gboolean gvr_vims_pattern_view_capture_message(GtkWidget *widget,
                                                const char *message,
                                                int live_frame);
gboolean gvr_vims_pattern_view_insert_message(GtkWidget *widget,
                                                    const char *message,
                                                    int frame,
                                                    int column,
                                                    gboolean advance);

void gvr_vims_pattern_view_update_playback(GtkWidget *widget,
                                           int bank,
                                           int slot,
                                           int frame,
                                           gboolean active,
                                           int max_linear_delta);
void gvr_vims_pattern_view_update_sample_playback(GtkWidget *widget,
                                                  int sample_id,
                                                  int frame,
                                                  gboolean active,
                                                  int max_linear_delta);
void gvr_vims_pattern_view_update_bank_playback(GtkWidget *widget,
                                                int bank,
                                                int frame,
                                                gboolean active,
                                                int max_linear_delta);
void gvr_vims_pattern_view_stop_all_playback(GtkWidget *widget);

void gvr_vims_pattern_view_clear_cell(GtkWidget *widget, int bank, int slot);
void gvr_vims_pattern_view_clear_bank(GtkWidget *widget, int bank);
void gvr_vims_pattern_view_clear_all(GtkWidget *widget);
void gvr_vims_pattern_view_swap_cells(GtkWidget *widget,
                                      int bank,
                                      int first_slot,
                                      int second_slot);
void gvr_vims_pattern_view_copy_cell(GtkWidget *widget,
                                     int src_bank,
                                     int src_slot,
                                     int dst_bank,
                                     int dst_slot);
void gvr_vims_pattern_view_copy_bank(GtkWidget *widget,
                                     int src_bank,
                                     int dst_bank);

guint gvr_vims_pattern_view_get_cell_flags(GtkWidget *widget,
                                            int bank,
                                            int slot);
gboolean gvr_vims_pattern_view_get_cell_summary(GtkWidget *widget,
                                                 int bank,
                                                 int slot,
                                                 GvrVimsPatternSummary *summary);
gboolean gvr_vims_pattern_view_get_source_summary(GtkWidget *widget,
                                                   int sample_id,
                                                   int sample_type,
                                                   GvrVimsPatternSummary *summary);
gboolean gvr_vims_pattern_view_get_bank_summary(GtkWidget *widget,
                                                 int bank,
                                                 GvrVimsPatternSummary *summary);

gchar   *gvr_vims_pattern_view_serialize(GtkWidget *widget, gsize *length);
gboolean gvr_vims_pattern_view_deserialize(GtkWidget *widget,
                                           const gchar *data,
                                           gsize length);

void gvr_vims_pattern_view_clipboard_clear(GtkWidget *widget);
void gvr_vims_pattern_view_clipboard_copy(GtkWidget *widget,
                                          int bank,
                                          int slot,
                                          int offset);
void gvr_vims_pattern_view_clipboard_paste(GtkWidget *widget,
                                           int offset,
                                           int bank,
                                           int slot);

G_END_DECLS

#endif
