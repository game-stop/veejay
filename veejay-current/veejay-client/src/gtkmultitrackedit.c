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
#include <config.h>
#include <gtk/gtk.h>
#include <math.h>
#include <limits.h>
#include <string.h>
#include <pango/pangocairo.h>
#include <veejaycore/vims.h>
#include "gtkmultitrackedit.h"
#include "gtkshapeselector.h"

#define GVR_MT_HEADER_WIDTH       360
#define GVR_MT_RULER_HEIGHT        30
#define GVR_MT_LANE_HEIGHT         90
#define GVR_MT_NAVIGATOR_HEIGHT    26
#define GVR_MT_ZOOM_MIN           1.0
#define GVR_MT_ZOOM_MAX         128.0
#define GVR_MT_TICK_TARGET_PX      92.0
#define GVR_MT_LANE_BUTTON_SIZE       30
#define GVR_MT_PREVIEW_MAX_WIDTH      128
#define GVR_MT_PREVIEW_MAX_HEIGHT      72
#define GVR_MT_DRAW_FONT_CACHE_SIZE       8

#ifndef MODE_SAMPLE
#define MODE_SAMPLE 0
#endif
#ifndef MODE_STREAM
#define MODE_STREAM 1
#endif
#ifndef MODE_PLAIN
#define MODE_PLAIN 2
#endif
#ifndef MODE_PATTERN
#define MODE_PATTERN 3
#endif

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
    char *title;
} GvrMultiTrackClipData;

typedef struct {
    int project_frame;
    int order;
    int vims_id;
    char *label;
    char *message;
} GvrMultiTrackEventData;

typedef struct {
    gboolean connected;
    gboolean current_control;
    gboolean project_master;
    gboolean preview_enabled;
    char *hostname;
    int port;

    int play_mode;
    int source_id;
    int source_type;
    int frame;
    int total_frames;
    int speed;
    int fps_x100;
    int loop_type;
    gboolean fx_enabled;
    gboolean audio_muted;
    gboolean sequence_active;
    int sequence_bank;
    int sequence_slot;
    gboolean stream_buffer_supported;
    gboolean stream_buffer_enabled;
    int stream_buffer_capacity;
    int stream_buffer_filled;
    int stream_buffer_position;
    int drift_frames;
    int drift_millis;
    gboolean drift_correcting;

    GdkPixbuf *preview;
    GPtrArray *clips;
    GPtrArray *events;

    GtkWidget *row_event;
    GtkWidget *row_box;
    GtkWidget *preview_image;
    GtkWidget *title_label;
    GtkWidget *status_label;
    GtkWidget *a_button;
    GtkWidget *b_button;
    GtkWidget *preview_toggle;
    GtkWidget *switch_button;
} GvrMultiTrackLane;

struct _GvrMultiTrackEdit {
    GtkBox parent_instance;

    GtkWidget *connection_role_label;
    GtkWidget *connection_path_label;
    GtkWidget *connection_state_label;
    GtkWidget *project_label;
    GtkWidget *position_label;
    GtkWidget *selection_label;
    GtkWidget *zoom_out_button;
    GtkWidget *zoom_in_button;
    GtkWidget *zoom_fit_button;
    GtkWidget *zoom_scale;
    GtkWidget *zoom_label;
    GtkWidget *bus_a_label;
    GtkWidget *bus_b_label;
    GtkWidget *transition_method_combo;
    GtkWidget *transition_shape_selector;
    GtkWidget *transition_shape_revealer;
    GtkWidget *transition_duration_spin;
    GtkWidget *transition_duration_time;
    GtkWidget *transition_take_button;
    GtkWidget *transition_cut_button;
    GtkWidget *transition_progress;
    GtkWidget *transition_status_label;
    GtkWidget *drift_toggle;

    GtkWidget *header_scroll;
    GtkWidget *timeline_scroll;
    GtkWidget *header_content;
    GtkWidget *timeline_area;
    GtkWidget *navigator;
    GtkWidget *pan_scrollbar;
    GtkAdjustment *vertical_adjustment;
    GtkAdjustment *pan_adjustment;

    int max_tracks;
    GvrMultiTrackLane lanes[GVR_MULTI_TRACK_EDIT_MAX_TRACKS];
    int selected_track;
    int current_control_track;
    int project_master_track;
    int bus_a_track;
    int bus_b_track;
    int active_bus;
    int transition_progress_value;
    gboolean transition_active;
    int transition_method;
    int transition_shape;
    guint transition_shape_count;
    gboolean drift_lock_enabled;

    int bank;
    unsigned int revision;
    int total_frames;
    double fps;
    int playhead;
    int playhead_slot;
    gboolean playhead_active;
    gboolean transport_seekable;
    char *transport_seek_reason;
    gboolean seek_dragging;

    GArray *master_clips;

    double timeline_zoom;
    int timeline_view_start;
    int hover_track;
    int hover_frame;
    int focused_track;
    gboolean follow_playhead;
    gboolean follow_live_edge;
    gboolean syncing_preview_toggle;
    gboolean syncing_drift_toggle;

    gboolean source_drag_active;
    int source_drag_track;
    int source_drag_kind;
    int source_drag_slot;
    int source_drag_frame;

    int pending_source_track;
    int pending_source_id;
    int pending_source_type;
    int pending_source_kind;

    gboolean geometry_dirty;
    int cached_effective_total;
    int cached_visible_frames;

    PangoLayout *draw_layout;
    PangoFontDescription *draw_fonts[GVR_MT_DRAW_FONT_CACHE_SIZE];
    double draw_font_scales[GVR_MT_DRAW_FONT_CACHE_SIZE];
    PangoWeight draw_font_weights[GVR_MT_DRAW_FONT_CACHE_SIZE];
    int draw_font_count;
};

struct _GvrMultiTrackEditClass {
    GtkBoxClass parent_class;
};

enum {
    SIGNAL_TRACK_SELECTED,
    SIGNAL_SWITCH_REQUESTED,
    SIGNAL_PREVIEW_TOGGLED,
    SIGNAL_TRANSITION_SOURCE_SELECTED,
    SIGNAL_SEEK_REQUESTED,
    SIGNAL_SOURCE_PLAY_REQUESTED,
    SIGNAL_SEQUENCE_SOURCE_INSERT_REQUESTED,
    SIGNAL_REVEAL_SEQUENCE_SLOT_REQUESTED,
    SIGNAL_REVEAL_SOURCE_REQUESTED,
    SIGNAL_RESYNC_REQUESTED,
    SIGNAL_TRANSITION_REQUESTED,
    SIGNAL_DRIFT_LOCK_TOGGLED,
    SIGNAL_LAST
};

static guint gvr_multi_track_edit_signals[SIGNAL_LAST];

G_DEFINE_TYPE(GvrMultiTrackEdit, gvr_multi_track_edit, GTK_TYPE_BOX)

static GtkTargetEntry gvr_multi_track_edit_drop_targets[] = {
    { (gchar *)GVR_MULTI_TRACK_SOURCE_DND_TARGET, GTK_TARGET_SAME_APP, 0 }
};

static int gvr_mt_clampi(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

static void gvr_mt_add_class(GtkWidget *widget, const char *name)
{
    GtkStyleContext *context = gtk_widget_get_style_context(widget);

    if(!gtk_style_context_has_class(context, name))
        gtk_style_context_add_class(context, name);
}

static void gvr_mt_remove_class(GtkWidget *widget, const char *name)
{
    GtkStyleContext *context = gtk_widget_get_style_context(widget);

    if(gtk_style_context_has_class(context, name))
        gtk_style_context_remove_class(context, name);
}

static double gvr_mt_font_points(GtkWidget *widget)
{
    PangoContext *context;
    const PangoFontDescription *font;
    int size;
    double points;

    if(!widget)
        return 10.0;

    context = gtk_widget_get_pango_context(widget);
    font = context ? pango_context_get_font_description(context) : NULL;
    size = font ? pango_font_description_get_size(font) : 0;
    if(size <= 0)
        return 10.0;

    points = (double)size / PANGO_SCALE;
    if(pango_font_description_get_size_is_absolute(font))
        points *= 72.0 / 96.0;
    return CLAMP(points, 6.0, 32.0);
}

static PangoFontDescription *gvr_mt_font(GtkWidget *widget,
                                         double scale,
                                         PangoWeight weight)
{
    PangoContext *context = widget ? gtk_widget_get_pango_context(widget) : NULL;
    const PangoFontDescription *base = context ?
        pango_context_get_font_description(context) : NULL;
    PangoFontDescription *font = base ?
        pango_font_description_copy(base) :
        pango_font_description_from_string("Monospace 10");

    pango_font_description_set_family(font, "Monospace");
    pango_font_description_set_size(
        font,
        (int)lrint(gvr_mt_font_points(widget) * scale * PANGO_SCALE));
    pango_font_description_set_weight(font, weight);
    return font;
}

static void gvr_mt_draw_context_end(GvrMultiTrackEdit *view);
static int gvr_mt_sequence_insertion_frame(GvrMultiTrackEdit *view, int insertion_slot);

static void gvr_mt_draw_context_begin(GvrMultiTrackEdit *view, cairo_t *cr)
{
    if(view->draw_layout || view->draw_font_count > 0)
        gvr_mt_draw_context_end(view);
    view->draw_layout = pango_cairo_create_layout(cr);
    view->draw_font_count = 0;
}

static void gvr_mt_draw_context_end(GvrMultiTrackEdit *view)
{
    for(int i = 0; i < view->draw_font_count; i++) {
        pango_font_description_free(view->draw_fonts[i]);
        view->draw_fonts[i] = NULL;
    }
    view->draw_font_count = 0;
    g_clear_object(&view->draw_layout);
}

static PangoFontDescription *gvr_mt_draw_font(GvrMultiTrackEdit *view,
                                               double scale,
                                               PangoWeight weight)
{
    for(int i = 0; i < view->draw_font_count; i++) {
        if(view->draw_font_scales[i] == scale &&
           view->draw_font_weights[i] == weight)
            return view->draw_fonts[i];
    }

    if(view->draw_font_count < GVR_MT_DRAW_FONT_CACHE_SIZE) {
        const int index = view->draw_font_count++;
        view->draw_font_scales[index] = scale;
        view->draw_font_weights[index] = weight;
        view->draw_fonts[index] = gvr_mt_font(GTK_WIDGET(view), scale, weight);
        return view->draw_fonts[index];
    }

    return NULL;
}

static void gvr_mt_lookup_color(GtkWidget *widget,
                                const char *name,
                                const GdkRGBA *fallback,
                                GdkRGBA *result)
{
    if(!gtk_style_context_lookup_color(gtk_widget_get_style_context(widget),
                                       name,
                                       result))
        *result = *fallback;
}

static void gvr_mt_set_source(cairo_t *cr, const GdkRGBA *color)
{
    cairo_set_source_rgba(cr,
                          color->red,
                          color->green,
                          color->blue,
                          color->alpha);
}

static void gvr_mt_clip_free(gpointer data)
{
    GvrMultiTrackClipData *clip = data;
    if(!clip)
        return;
    g_free(clip->title);
    g_free(clip);
}

static void gvr_mt_event_free(gpointer data)
{
    GvrMultiTrackEventData *event = data;
    if(!event)
        return;
    g_free(event->label);
    g_free(event->message);
    g_free(event);
}

static void gvr_mt_timecode(GvrMultiTrackEdit *view,
                            int frame,
                            char *buffer,
                            size_t buffer_size)
{
    const double fps = view->fps > 0.0 ? view->fps : 25.0;
    const int nominal = MAX(1, (int)floor(fps + 0.5));
    int safe = MAX(0, frame);
    int seconds = (int)floor((double)safe / fps);
    int ff = (int)floor((double)safe - ((double)seconds * fps) + 0.5);

    if(ff >= nominal) {
        ff = 0;
        seconds++;
    }

    g_snprintf(buffer,
               buffer_size,
               "%02d:%02d:%02d:%02d",
               seconds / 3600,
               (seconds / 60) % 60,
               seconds % 60,
               ff);
}

static GvrMultiTrackLane *gvr_mt_project_master_lane(GvrMultiTrackEdit *view)
{
    if(!view || view->project_master_track < 0 ||
       view->project_master_track >= view->max_tracks)
        return NULL;
    return &view->lanes[view->project_master_track];
}

static gboolean gvr_mt_direct_stream_active(GvrMultiTrackEdit *view)
{
    GvrMultiTrackLane *lane = gvr_mt_project_master_lane(view);
    return lane && lane->connected && !lane->sequence_active &&
           lane->play_mode == MODE_STREAM && lane->source_id > 0;
}

static int gvr_mt_display_playhead(GvrMultiTrackEdit *view)
{
    GvrMultiTrackLane *lane = gvr_mt_project_master_lane(view);

    if(gvr_mt_direct_stream_active(view) && lane &&
       lane->stream_buffer_supported && lane->stream_buffer_enabled &&
       lane->stream_buffer_filled > 0) {
        const int total = MAX(1, MAX(lane->stream_buffer_capacity,
                                     lane->stream_buffer_filled));
        const int filled = MIN(total, lane->stream_buffer_filled);
        const int pos = gvr_mt_clampi(view->playhead, 0, filled - 1);
        return total - filled + pos;
    }

    return view->playhead;
}

static void gvr_mt_invalidate_geometry(GvrMultiTrackEdit *view)
{
    view->geometry_dirty = TRUE;
}

static int gvr_mt_effective_total(GvrMultiTrackEdit *view)
{
    int total;

    if(!view->geometry_dirty)
        return view->cached_effective_total;

    total = view->total_frames;

    if(gvr_mt_direct_stream_active(view)) {
        GvrMultiTrackLane *lane = gvr_mt_project_master_lane(view);
        total = MAX(total, lane->total_frames);
        total = MAX(total, lane->stream_buffer_filled);
        total = MAX(total, lane->stream_buffer_capacity);
    }
    else {
        for(int i = 0; i < view->max_tracks; i++) {
            GvrMultiTrackLane *lane = &view->lanes[i];
            if(lane->total_frames > total)
                total = lane->total_frames;
            for(guint j = 0; lane->clips && j < lane->clips->len; j++) {
                GvrMultiTrackClipData *clip = g_ptr_array_index(lane->clips, j);
                const int clip_end = clip->repeat_until >= clip->project_out ?
                                     clip->repeat_until : clip->project_out;
                if(clip_end == INT_MAX) {
                    total = INT_MAX;
                    break;
                }
                if(clip_end + 1 > total)
                    total = clip_end + 1;
            }
            if(total == INT_MAX)
                break;
        }
    }

    view->cached_effective_total = MAX(total, 1);
    view->cached_visible_frames = MAX(
        1,
        (int)ceil((double)view->cached_effective_total /
                  MAX(1.0, view->timeline_zoom)));
    view->geometry_dirty = FALSE;
    return view->cached_effective_total;
}

static int gvr_mt_visible_frames(GvrMultiTrackEdit *view)
{
    gvr_mt_effective_total(view);
    return view->cached_visible_frames;
}

static void gvr_mt_update_pan(GvrMultiTrackEdit *view)
{
    const int total = gvr_mt_effective_total(view);
    const int visible = MIN(total, gvr_mt_visible_frames(view));
    const int max_start = MAX(0, total - visible);

    view->timeline_view_start = gvr_mt_clampi(view->timeline_view_start,
                                              0,
                                              max_start);

    gtk_adjustment_configure(view->pan_adjustment,
                             view->timeline_view_start,
                             0.0,
                             MAX(1.0, (double)total),
                             MAX(1.0, visible / 16.0),
                             MAX(1.0, visible * 0.8),
                             MAX(1.0, (double)visible));
    gtk_widget_set_sensitive(view->pan_scrollbar, total > visible);
}

static void gvr_mt_queue_timeline_draw(GvrMultiTrackEdit *view)
{
    if(view->timeline_area)
        gtk_widget_queue_draw(view->timeline_area);
}

static void gvr_mt_queue_navigator_draw(GvrMultiTrackEdit *view)
{
    if(view->navigator)
        gtk_widget_queue_draw(view->navigator);
}

static void gvr_mt_queue_draw(GvrMultiTrackEdit *view)
{
    gvr_mt_queue_timeline_draw(view);
    gvr_mt_queue_navigator_draw(view);
}

static void gvr_mt_update_summary(GvrMultiTrackEdit *view)
{
    GvrMultiTrackLane *lane = gvr_mt_project_master_lane(view);
    char duration[32];
    char position[32];
    char project[192];
    char playhead[256];
    char selection[64];
    gboolean show_project = FALSE;
    int display_total;
    int display_frame;

    project[0] = '\0';

    if(lane && lane->connected && lane->sequence_active) {
        gvr_mt_timecode(view, MAX(0, view->total_frames - 1),
                        duration, sizeof(duration));
        gvr_mt_timecode(view, view->playhead, position, sizeof(position));
        g_snprintf(project,
                   sizeof(project),
                   "Sequence bank %d · revision %u · %d frames · %s",
                   view->bank + 1,
                   view->revision,
                   view->total_frames,
                   duration);
        g_snprintf(playhead,
                   sizeof(playhead),
                   "Project frame %d · %s%s",
                   view->playhead,
                   position,
                   view->playhead_active ? "" : " · idle");
        show_project = TRUE;
    }
    else if(lane && lane->connected && lane->source_id > 0) {
        display_frame = MAX(0, lane->frame);
        display_total = MAX(0, lane->total_frames);

        if(lane->play_mode == MODE_STREAM &&
           lane->stream_buffer_supported &&
           lane->stream_buffer_enabled) {
            display_frame = MAX(0, lane->stream_buffer_position);
            display_total = MAX(lane->stream_buffer_capacity,
                                lane->stream_buffer_filled);
            gvr_mt_timecode(view, display_frame,
                            position, sizeof(position));
            g_snprintf(playhead,
                       sizeof(playhead),
                       "Stream %d · buffer frame %d/%d · %s",
                       lane->source_id,
                       display_frame,
                       display_total,
                       position);
        }
        else {
            gvr_mt_timecode(view, display_frame,
                            position, sizeof(position));

            switch(lane->play_mode) {
                case MODE_SAMPLE:
                    g_snprintf(playhead,
                               sizeof(playhead),
                               "Sample %d · frame %d/%d · %s",
                               lane->source_id,
                               display_frame,
                               display_total,
                               position);
                    break;
                case MODE_PATTERN:
                    g_snprintf(playhead,
                               sizeof(playhead),
                               "Pattern · sample %d · frame %d/%d · %s",
                               lane->source_id,
                               display_frame,
                               display_total,
                               position);
                    break;
                case MODE_STREAM:
                    g_snprintf(playhead,
                               sizeof(playhead),
                               "Live stream %d · frame %d · %s",
                               lane->source_id,
                               display_frame,
                               position);
                    break;
                case MODE_PLAIN:
                    g_snprintf(playhead,
                               sizeof(playhead),
                               "Plain EDL · frame %d/%d · %s",
                               display_frame,
                               display_total,
                               position);
                    break;
                default:
                    g_snprintf(playhead,
                               sizeof(playhead),
                               "Playback source %d · frame %d/%d · %s",
                               lane->source_id,
                               display_frame,
                               display_total,
                               position);
                    break;
            }
        }
    }
    else if(lane && lane->connected && lane->play_mode == MODE_PLAIN) {
        display_total = MAX(0, lane->total_frames);
        display_frame = MAX(0, lane->frame);
        gvr_mt_timecode(view, display_frame,
                        position, sizeof(position));
        g_snprintf(playhead,
                   sizeof(playhead),
                   "Plain EDL · frame %d/%d · %s",
                   display_frame,
                   display_total,
                   position);
    }
    else {
        g_strlcpy(playhead, "Transport idle", sizeof(playhead));
    }

    if(view->selected_track >= 0)
        g_snprintf(selection,
                   sizeof(selection),
                   "Selected video track %d",
                   view->selected_track + 1);
    else
        g_strlcpy(selection, "Selected video track —", sizeof(selection));

    if(show_project) {
        gtk_label_set_text(GTK_LABEL(view->project_label), project);
        gtk_widget_show(view->project_label);
    }
    else {
        gtk_label_set_text(GTK_LABEL(view->project_label), "");
        gtk_widget_hide(view->project_label);
    }

    gtk_label_set_text(GTK_LABEL(view->position_label), playhead);
    gtk_label_set_text(GTK_LABEL(view->selection_label), selection);
}

static const char *gvr_mt_source_name(int play_mode, int source_id)
{
    if(source_id <= 0)
        return "No source";
    if(play_mode == MODE_STREAM)
        return "Stream";
    if(play_mode == MODE_PLAIN)
        return "Plain EDL";
    return "Sample";
}

static const char *gvr_mt_loop_name(int loop_type)
{
    switch(loop_type) {
        case 0: return "once";
        case 1: return "loop";
        case 2: return "ping-pong";
        case 3: return "random";
        case 4: return "once/no-pause";
        default: return "—";
    }
}

static void gvr_mt_set_button_icon(GtkWidget *button, const char *icon_name)
{
    GtkWidget *image;

    if(!button)
        return;
    if(!GTK_IS_BUTTON(button))
        return;

    image = gtk_button_get_image(GTK_BUTTON(button));
    if(!image)
        return;
    if(!GTK_IS_IMAGE(image)) {
        image = gtk_image_new();
        gtk_button_set_image(GTK_BUTTON(button), image);
    }
    gtk_image_set_from_icon_name(GTK_IMAGE(image),
                                 icon_name,
                                 GTK_ICON_SIZE_BUTTON);
}

static void gvr_mt_set_preview_icon(GtkWidget *button, gboolean enabled)
{
    gvr_mt_set_button_icon(button,
                           enabled ?
                               "view-visible-symbolic" :
                               "view-hidden-symbolic");
}

static void gvr_mt_set_switch_icon(GtkWidget *button, gboolean current)
{
    gvr_mt_set_button_icon(button,
                           current ?
                               "emblem-ok-symbolic" :
                               "go-jump-symbolic");
}

static const char *gvr_mt_transition_track_name(GvrMultiTrackEdit *view,
                                                 int track,
                                                 char *buffer,
                                                 size_t size)
{
    if(track < 0 || track >= view->max_tracks || !view->lanes[track].connected) {
        g_snprintf(buffer, size, "—");
        return buffer;
    }

    g_snprintf(buffer, size, "Video %d", track + 1);
    return buffer;
}

static void gvr_mt_update_transition_ui(GvrMultiTrackEdit *view);

static const char *gvr_mt_transition_method_name(int method)
{
    switch(method) {
        case VJ_MULTITRACK_TRANSITION_SHAPE_WIPE:
            return "Shape Wipe";
        case VJ_MULTITRACK_TRANSITION_DISSOLVE:
        default:
            return "Dissolve";
    }
}

static void gvr_mt_transition_method_changed(GtkComboBox *combo,
                                             gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);

    view->transition_method = gtk_combo_box_get_active(combo);
    if(view->transition_method == VJ_MULTITRACK_TRANSITION_SHAPE_WIPE &&
       view->transition_shape_count == 0) {
        view->transition_method = VJ_MULTITRACK_TRANSITION_DISSOLVE;
        gtk_combo_box_set_active(combo,
                                 VJ_MULTITRACK_TRANSITION_DISSOLVE);
        return;
    }
    if(view->transition_method != VJ_MULTITRACK_TRANSITION_SHAPE_WIPE)
        view->transition_method = VJ_MULTITRACK_TRANSITION_DISSOLVE;
    if(view->transition_shape_revealer) {
        if(view->transition_method == VJ_MULTITRACK_TRANSITION_SHAPE_WIPE)
            gtk_widget_show(view->transition_shape_revealer);
        else
            gtk_widget_hide(view->transition_shape_revealer);
    }
    gvr_mt_update_transition_ui(view);
}

static void gvr_mt_transition_shape_changed(GtkWidget *widget,
                                            int shape,
                                            gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    (void)widget;

    view->transition_shape = shape;
    gvr_mt_update_transition_ui(view);
}

static void gvr_mt_update_transition_ui(GvrMultiTrackEdit *view)
{
    char a_name[32];
    char b_name[32];
    char text[160];
    char seconds[48];
    const char *from_bus;
    const char *to_bus;
    const char *from_name;
    const char *to_name;
    int duration;
    int percent;
    gboolean ready;
    const char *method_name;
    const char *shape_name;

    if(!view->bus_a_label)
        return;

    gvr_mt_transition_track_name(view, view->bus_a_track,
                                 a_name, sizeof(a_name));
    gvr_mt_transition_track_name(view, view->bus_b_track,
                                 b_name, sizeof(b_name));
    g_snprintf(text, sizeof(text), "A  %s · PROJECT MASTER%s",
               a_name, view->active_bus == 0 ? "  ·  ON AIR" : "");
    gtk_label_set_text(GTK_LABEL(view->bus_a_label), text);
    g_snprintf(text, sizeof(text), "B  %s%s",
               b_name, view->active_bus == 1 ? "  ·  ON AIR" : "");
    gtk_label_set_text(GTK_LABEL(view->bus_b_label), text);

    duration = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(view->transition_duration_spin));
    if(view->fps > 0.0)
        g_snprintf(seconds, sizeof(seconds), "%.2f s", duration / view->fps);
    else
        g_snprintf(seconds, sizeof(seconds), "%d frames", duration);
    gtk_label_set_text(GTK_LABEL(view->transition_duration_time), seconds);

    view->transition_method = gtk_combo_box_get_active(
        GTK_COMBO_BOX(view->transition_method_combo));
    if(view->transition_method != VJ_MULTITRACK_TRANSITION_SHAPE_WIPE)
        view->transition_method = VJ_MULTITRACK_TRANSITION_DISSOLVE;
    view->transition_shape = gvr_shape_selector_get_active(
        view->transition_shape_selector);
    method_name = gvr_mt_transition_method_name(view->transition_method);
    shape_name = gvr_shape_selector_get_active_name(view->transition_shape_selector);

    view->transition_progress_value =
        gvr_mt_clampi(view->transition_progress_value, 0, 255);
    percent = (int)floor((view->transition_progress_value * 100.0 / 255.0) + 0.5);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(view->transition_progress),
                                  view->transition_active ?
                                      view->transition_progress_value / 255.0 : 0.0);

    if(view->active_bus == 1) {
        from_bus = "B";
        to_bus = "A";
        from_name = b_name;
        to_name = a_name;
    }
    else {
        from_bus = "A";
        to_bus = "B";
        from_name = a_name;
        to_name = b_name;
    }

    if(view->transition_active)
        g_snprintf(text, sizeof(text), "%s %s → %s %s  ·  %s  ·  %d%%",
                   from_bus, from_name, to_bus, to_name, method_name, percent);
    else if(view->transition_method == VJ_MULTITRACK_TRANSITION_SHAPE_WIPE)
        g_snprintf(text, sizeof(text), "NEXT  ·  %s %s  ·  %s #%d %s",
                   to_bus, to_name, method_name, view->transition_shape, shape_name);
    else
        g_snprintf(text, sizeof(text), "NEXT  ·  %s %s  ·  %s",
                   to_bus, to_name, method_name);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(view->transition_progress), text);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(view->transition_progress), TRUE);

    if(view->transition_active)
        g_snprintf(text, sizeof(text), "ON AIR · %s %s → %s %s · %s",
                   from_bus, from_name, to_bus, to_name, method_name);
    else
        g_snprintf(text, sizeof(text), "ON AIR · %s %s",
                   from_bus, from_name);
    gtk_label_set_text(GTK_LABEL(view->transition_status_label), text);

    ready = !view->transition_active &&
            view->bus_a_track >= 0 &&
            view->bus_b_track >= 0 &&
            view->bus_a_track != view->bus_b_track &&
            view->lanes[view->bus_a_track].connected &&
            view->lanes[view->bus_b_track].connected;
    gtk_widget_set_sensitive(view->transition_take_button, ready);
    gtk_widget_set_sensitive(view->transition_cut_button, ready);
    gtk_widget_set_sensitive(view->transition_method_combo, ready);
    gtk_widget_set_sensitive(view->transition_shape_selector,
                             ready && view->transition_shape_count > 0);

    if(view->transition_active)
        gvr_mt_add_class(view->transition_progress, "multi-track-transition-active");
    else
        gvr_mt_remove_class(view->transition_progress, "multi-track-transition-active");
}

static void gvr_mt_update_lane_header(GvrMultiTrackEdit *view, int track)
{
    GvrMultiTrackLane *lane;
    gchar *title;
    gchar *status;
    gchar *tip;
    char sync[48] = { 0 };

    if(track < 0 || track >= view->max_tracks)
        return;

    lane = &view->lanes[track];

    gvr_mt_remove_class(lane->status_label, "multi-track-drift-good");
    gvr_mt_remove_class(lane->status_label, "multi-track-drift-warn");
    gvr_mt_remove_class(lane->status_label, "multi-track-drift-bad");

    if(lane->connected) {
        const char *host = lane->hostname && lane->hostname[0] ?
                           lane->hostname : "localhost";
        title = lane->project_master ?
            g_strdup_printf("Video %d  ·  PROJECT MASTER  ·  %s:%d",
                            track + 1, host, lane->port) :
            g_strdup_printf("Video %d  ·  %s:%d",
                            track + 1, host, lane->port);

        if(lane->project_master) {
            g_snprintf(sync, sizeof(sync), " · CLOCK");
            gvr_mt_add_class(lane->status_label, "multi-track-drift-good");
        }
        else if(view->drift_lock_enabled) {
            const int master_fps_x100 =
                view->project_master_track >= 0 ?
                view->lanes[view->project_master_track].fps_x100 : 0;
            const int drift = lane->drift_frames;
            const gboolean mixed_fps =
                master_fps_x100 > 0 && lane->fps_x100 > 0 &&
                ABS(master_fps_x100 - lane->fps_x100) >= 2;

            if(mixed_fps)
                g_snprintf(sync, sizeof(sync), " · Δ %+dms%s",
                           lane->drift_millis,
                           lane->drift_correcting ? " correcting" : "");
            else
                g_snprintf(sync, sizeof(sync), " · Δ %+df%s",
                           drift,
                           lane->drift_correcting ? " correcting" : "");
            if(ABS(drift) <= 1)
                gvr_mt_add_class(lane->status_label, "multi-track-drift-good");
            else if(ABS(drift) <= 4)
                gvr_mt_add_class(lane->status_label, "multi-track-drift-warn");
            else
                gvr_mt_add_class(lane->status_label, "multi-track-drift-bad");
        }

        if(lane->source_id > 0) {
            const double fps = lane->fps_x100 > 0 ?
                               ((double)lane->fps_x100 / 100.0) : 0.0;
            char sequence[40] = { 0 };
            if(lane->sequence_active)
                g_snprintf(sequence, sizeof(sequence), " · SEQ B%d #%d",
                           lane->sequence_bank + 1,
                           lane->sequence_slot + 1);
            status = g_strdup_printf(
                "%s %d · %d/%d · %dx · %.2f fps · %s · FX %s%s%s%s",
                gvr_mt_source_name(lane->play_mode, lane->source_id),
                lane->source_id,
                MAX(0, lane->frame),
                MAX(0, lane->total_frames - 1),
                lane->speed,
                fps,
                gvr_mt_loop_name(lane->loop_type),
                lane->fx_enabled ? "on" : "off",
                lane->audio_muted ? " · muted" : "",
                sequence,
                sync);
        }
        else
            status = g_strdup_printf("Connected · waiting for source status%s", sync);

        tip = g_strdup_printf(
            "Video track %d is connected to %s:%d.%s%s%s",
            track + 1,
            host,
            lane->port,
            lane->project_master ?
                " This is the fixed project-master sequence clock and dry compositor source." : "",
            lane->current_control ?
                " The main Reloaded controls currently operate this instance." : "",
            !lane->project_master && view->drift_lock_enabled ?
                " Drift Lock compares media time with the project master, so different source frame rates remain aligned." : "");
    }
    else {
        title = g_strdup_printf("Video %d", track + 1);
        status = g_strdup("Not connected");
        tip = g_strdup("Empty video track. Use Add Track to connect another VeeJay instance.");
    }

    gtk_label_set_text(GTK_LABEL(lane->title_label), title);
    gtk_label_set_text(GTK_LABEL(lane->status_label), status);
    gtk_widget_set_tooltip_text(lane->row_event, tip);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lane->a_button),
                                 lane->project_master);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lane->b_button),
                                 !lane->project_master &&
                                 track == view->bus_b_track);

    if(lane->project_master) {
        gtk_widget_show(lane->a_button);
        gtk_widget_hide(lane->b_button);
    }
    else {
        gtk_widget_hide(lane->a_button);
        gtk_widget_show(lane->b_button);
    }

    gtk_widget_set_sensitive(lane->a_button, FALSE);
    gtk_widget_set_sensitive(lane->b_button,
                             lane->connected &&
                             !lane->project_master &&
                             !view->transition_active &&
                             view->active_bus == 0);
    gtk_widget_set_tooltip_text(
        lane->a_button,
        "Bus A is fixed to the Video 1 project master and dry compositor source. This is separate from VeeJay's --master output role.");
    gtk_widget_set_tooltip_text(
        lane->b_button,
        lane->connected ?
            (view->active_bus == 1 ?
                "Bus B is on air. CUT or TAKE back to Video 1 before selecting another B source." :
                "Select this video as Bus B. All follower unicast inputs are kept open on Video 1.") :
            "Connect a VeeJay instance before selecting it as Bus B.");

    view->syncing_preview_toggle = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lane->preview_toggle),
                                 lane->preview_enabled);
    gvr_mt_set_preview_icon(lane->preview_toggle, lane->preview_enabled);
    gtk_widget_set_sensitive(lane->preview_toggle, lane->connected);
    gtk_widget_set_tooltip_text(
        lane->preview_toggle,
        lane->connected ?
            "Toggle this lane preview. The eye button does not affect the on-air output." :
            "Connect a VeeJay instance before enabling its preview.");
    view->syncing_preview_toggle = FALSE;

    if(lane->current_control) {
        gvr_mt_set_switch_icon(lane->switch_button, TRUE);
        gtk_widget_set_sensitive(lane->switch_button, FALSE);
        gtk_widget_set_tooltip_text(
            lane->switch_button,
            "The main Reloaded controls already operate this VeeJay instance.");
    }
    else {
        gvr_mt_set_switch_icon(lane->switch_button, FALSE);
        gtk_widget_set_sensitive(lane->switch_button, lane->connected);
        gtk_widget_set_tooltip_text(
            lane->switch_button,
            "Switch the main Reloaded controls to this VeeJay instance. "
            "The on-air output and project-master clock remain unchanged.");
    }

    if(lane->current_control)
        gvr_mt_add_class(lane->row_event, "multi-track-current");
    else
        gvr_mt_remove_class(lane->row_event, "multi-track-current");

    if(lane->project_master)
        gvr_mt_add_class(lane->row_event, "multi-track-master");
    else
        gvr_mt_remove_class(lane->row_event, "multi-track-master");

    if(track == view->selected_track)
        gvr_mt_add_class(lane->row_event, "multi-track-selected");
    else
        gvr_mt_remove_class(lane->row_event, "multi-track-selected");

    g_free(tip);
    g_free(status);
    g_free(title);
}

static void gvr_mt_refresh_headers(GvrMultiTrackEdit *view)
{
    for(int i = 0; i < view->max_tracks; i++)
        gvr_mt_update_lane_header(view, i);
    gvr_mt_update_transition_ui(view);
}

static double gvr_mt_frame_to_x(GvrMultiTrackEdit *view,
                                int frame,
                                int width)
{
    const int visible = gvr_mt_visible_frames(view);
    return ((double)(frame - view->timeline_view_start) /
            (double)visible) * width;
}

static int gvr_mt_x_to_frame(GvrMultiTrackEdit *view,
                             double x,
                             int width)
{
    const int visible = gvr_mt_visible_frames(view);
    int frame;

    if(width <= 0)
        return view->timeline_view_start;

    frame = view->timeline_view_start +
            (int)floor((x / (double)width) * visible + 0.5);
    return gvr_mt_clampi(frame, 0, gvr_mt_effective_total(view) - 1);
}

static int gvr_mt_visible_track_count(GvrMultiTrackEdit *view)
{
    int visible = 0;

    if(!view)
        return 0;

    if(view->focused_track >= 0)
        return view->focused_track < view->max_tracks &&
               view->lanes[view->focused_track].connected ? 1 : 0;

    for(int track = 0; track < view->max_tracks; track++)
        if(view->lanes[track].connected)
            visible++;

    return visible;
}

static int gvr_mt_track_y(GvrMultiTrackEdit *view, int track)
{
    int row = 0;

    if(!view || track < 0 || track >= view->max_tracks ||
       !view->lanes[track].connected)
        return -1;
    if(view->focused_track >= 0)
        return track == view->focused_track ? GVR_MT_RULER_HEIGHT : -1;

    for(int i = 0; i < track; i++)
        if(view->lanes[i].connected)
            row++;

    return GVR_MT_RULER_HEIGHT + row * GVR_MT_LANE_HEIGHT;
}

static int gvr_mt_track_at_y(GvrMultiTrackEdit *view, double y)
{
    int row;
    int visible_row = 0;

    if(!view || y < GVR_MT_RULER_HEIGHT)
        return -1;
    if(view->focused_track >= 0)
        return view->focused_track < view->max_tracks &&
               view->lanes[view->focused_track].connected &&
               y < GVR_MT_RULER_HEIGHT + GVR_MT_LANE_HEIGHT ?
                   view->focused_track : -1;

    row = (int)((y - GVR_MT_RULER_HEIGHT) / GVR_MT_LANE_HEIGHT);
    for(int track = 0; track < view->max_tracks; track++) {
        if(!view->lanes[track].connected)
            continue;
        if(visible_row == row)
            return track;
        visible_row++;
    }

    return -1;
}

static void gvr_mt_update_focus_layout(GvrMultiTrackEdit *view)
{
    const int minimum_height = GVR_MT_RULER_HEIGHT;
    int visible_tracks;

    if(!view)
        return;

    if(view->focused_track >= 0 &&
       (view->focused_track >= view->max_tracks ||
        !view->lanes[view->focused_track].connected))
        view->focused_track = -1;

    visible_tracks = gvr_mt_visible_track_count(view);

    for(int track = 0; track < view->max_tracks; track++) {
        GvrMultiTrackLane *lane = &view->lanes[track];
        const gboolean visible = lane->connected &&
            (view->focused_track < 0 || track == view->focused_track);

        if(!lane->row_event)
            continue;

        gtk_widget_set_no_show_all(lane->row_event, visible ? FALSE : TRUE);
        if(visible)
            gtk_widget_show(lane->row_event);
        else
            gtk_widget_hide(lane->row_event);
    }

    if(view->timeline_area)
        gtk_widget_set_size_request(
            view->timeline_area,
            640,
            MAX(minimum_height,
                GVR_MT_RULER_HEIGHT +
                visible_tracks * GVR_MT_LANE_HEIGHT));

    gvr_mt_queue_draw(view);
}

static void gvr_mt_center_frame(GvrMultiTrackEdit *view, int frame)
{
    if(!view)
        return;
    view->timeline_view_start = frame - gvr_mt_visible_frames(view) / 2;
    gvr_mt_update_pan(view);
    gvr_mt_queue_draw(view);
}

static void gvr_mt_follow_transport(GvrMultiTrackEdit *view)
{
    int displayed;
    int visible;
    int left_margin;
    int right_margin;

    if(!view || !view->playhead_active)
        return;

    displayed = gvr_mt_display_playhead(view);
    visible = gvr_mt_visible_frames(view);
    if(visible <= 1)
        return;

    if(view->follow_live_edge && gvr_mt_direct_stream_active(view)) {
        view->timeline_view_start =
            MAX(0, gvr_mt_effective_total(view) - visible);
        gvr_mt_update_pan(view);
        return;
    }
    if(!view->follow_playhead)
        return;

    left_margin = view->timeline_view_start + visible / 5;
    right_margin = view->timeline_view_start + (visible * 4) / 5;
    if(displayed < left_margin)
        view->timeline_view_start = displayed - visible / 5;
    else if(displayed > right_margin)
        view->timeline_view_start = displayed - (visible * 4) / 5;
    else
        return;
    gvr_mt_update_pan(view);
}

static void gvr_mt_zoom_to_range(GvrMultiTrackEdit *view, int first, int last)
{
    const int total = gvr_mt_effective_total(view);
    const int span = MAX(1, last - first + 1);
    const double zoom = CLAMP((double)total * 0.80 / (double)span,
                              GVR_MT_ZOOM_MIN,
                              GVR_MT_ZOOM_MAX);

    gtk_range_set_value(GTK_RANGE(view->zoom_scale), zoom);
    gvr_mt_center_frame(view, first + span / 2);
}

static void gvr_mt_source_color(int sample_type, GdkRGBA *color)
{
    static const GdkRGBA palette[] = {
        { 0.160, 0.360, 0.620, 1.0 },
        { 0.050, 0.520, 0.620, 1.0 },
        { 0.080, 0.520, 0.240, 1.0 },
        { 0.720, 0.260, 0.420, 1.0 },
        { 0.570, 0.180, 0.760, 1.0 },
        { 0.760, 0.260, 0.100, 1.0 },
        { 0.320, 0.350, 0.430, 1.0 },
        { 0.560, 0.390, 0.140, 1.0 }
    };
    *color = palette[ABS(sample_type) % G_N_ELEMENTS(palette)];
}

static void gvr_mt_draw_text(GvrMultiTrackEdit *view,
                             cairo_t *cr,
                             const char *text,
                             double x,
                             double y,
                             double max_width,
                             double scale,
                             PangoWeight weight,
                             const GdkRGBA *color)
{
    PangoLayout *layout = view->draw_layout;
    PangoFontDescription *font;
    gboolean temporary = FALSE;
    gboolean owned_font = FALSE;

    if(!layout) {
        layout = pango_cairo_create_layout(cr);
        temporary = TRUE;
    }

    font = temporary ?
        gvr_mt_font(GTK_WIDGET(view), scale, weight) :
        gvr_mt_draw_font(view, scale, weight);
    if(!font) {
        font = gvr_mt_font(GTK_WIDGET(view), scale, weight);
        owned_font = TRUE;
    }

    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_width(layout,
                           max_width > 0 ?
                               (int)(max_width * PANGO_SCALE) : -1);

    gvr_mt_set_source(cr, color);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);

    if(temporary) {
        pango_font_description_free(font);
        g_object_unref(layout);
    }
    else if(owned_font)
        pango_font_description_free(font);
}

static gboolean gvr_mt_text_fits(GvrMultiTrackEdit *view,
                                 cairo_t *cr,
                                 const char *text,
                                 double max_width,
                                 double scale,
                                 PangoWeight weight)
{
    PangoLayout *layout = view->draw_layout;
    PangoFontDescription *font;
    gboolean temporary = FALSE;
    gboolean owned_font = FALSE;
    int text_width = 0;

    if(!text || !text[0] || max_width <= 0.0)
        return FALSE;

    if(!layout) {
        layout = pango_cairo_create_layout(cr);
        temporary = TRUE;
    }

    font = temporary ?
        gvr_mt_font(GTK_WIDGET(view), scale, weight) :
        gvr_mt_draw_font(view, scale, weight);
    if(!font) {
        font = gvr_mt_font(GTK_WIDGET(view), scale, weight);
        owned_font = TRUE;
    }

    pango_layout_set_font_description(layout, font);
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_text(layout, text, -1);
    pango_layout_get_pixel_size(layout, &text_width, NULL);

    if(temporary) {
        pango_font_description_free(font);
        g_object_unref(layout);
    }
    else if(owned_font)
        pango_font_description_free(font);

    return text_width <= (int)floor(max_width);
}

static double gvr_mt_nice_tick(double raw)
{
    const double power = pow(10.0, floor(log10(MAX(raw, 1.0))));
    const double normalized = raw / power;
    double step;

    if(normalized <= 1.0)
        step = 1.0;
    else if(normalized <= 2.0)
        step = 2.0;
    else if(normalized <= 5.0)
        step = 5.0;
    else
        step = 10.0;

    return step * power;
}

static void gvr_mt_draw_ruler(GvrMultiTrackEdit *view,
                              cairo_t *cr,
                              int width)
{
    const GdkRGBA fallback_text = { 0.80, 0.82, 0.88, 1.0 };
    const GdkRGBA fallback_grid = { 0.23, 0.25, 0.30, 1.0 };
    GdkRGBA text;
    GdkRGBA grid;
    const int visible = gvr_mt_visible_frames(view);
    const double frames_per_px = (double)visible / MAX(1, width);
    const int tick = MAX(1, (int)gvr_mt_nice_tick(frames_per_px *
                                                  GVR_MT_TICK_TARGET_PX));
    int first = (view->timeline_view_start / tick) * tick;

    gvr_mt_lookup_color(GTK_WIDGET(view), "text-muted", &fallback_text, &text);
    gvr_mt_lookup_color(GTK_WIDGET(view), "border-color", &fallback_grid, &grid);

    cairo_set_source_rgba(cr, 0.11, 0.12, 0.15, 1.0);
    cairo_rectangle(cr, 0, 0, width, GVR_MT_RULER_HEIGHT);
    cairo_fill(cr);

    for(int frame = first;
        frame <= view->timeline_view_start + visible;
        frame += tick)
    {
        const double x = floor(gvr_mt_frame_to_x(view, frame, width)) + 0.5;
        char timecode[32];

        gvr_mt_set_source(cr, &grid);
        cairo_move_to(cr, x, GVR_MT_RULER_HEIGHT - 8.0);
        cairo_line_to(cr, x, GVR_MT_RULER_HEIGHT);
        cairo_stroke(cr);

        gvr_mt_timecode(view, frame, timecode, sizeof(timecode));
        gvr_mt_draw_text(view,
                         cr,
                         timecode,
                         x + 4.0,
                         5.0,
                         98.0,
                         0.70,
                         PANGO_WEIGHT_NORMAL,
                         &text);
    }
}

static void gvr_mt_draw_clip(GvrMultiTrackEdit *view,
                             cairo_t *cr,
                             int width,
                             int lane_y,
                             int project_in,
                             int project_out,
                             int sample_id,
                             int sample_type,
                             const char *title,
                             gboolean live,
                             gboolean master)
{
    const int visible_start = view->timeline_view_start;
    const int visible_end = visible_start + gvr_mt_visible_frames(view) - 1;
    const GdkRGBA white = { 0.97, 0.98, 1.0, 1.0 };
    GdkRGBA color;
    double x1;
    double x2;
    double clip_width;
    double label_width;
    double label_x;
    gchar label[128];
    gchar compact_label[24];
    gboolean compact = FALSE;

    if(project_out < visible_start || project_in > visible_end)
        return;

    project_in = MAX(project_in, visible_start);
    project_out = MIN(project_out, visible_end);
    x1 = gvr_mt_frame_to_x(view, project_in, width);
    x2 = gvr_mt_frame_to_x(view, project_out + 1, width);
    clip_width = MAX(2.0, x2 - x1);

    gvr_mt_source_color(sample_type, &color);
    if(live)
        color.alpha = 0.72;

    gvr_mt_set_source(cr, &color);
    cairo_rectangle(cr,
                    x1 + 1.0,
                    lane_y + 8.0,
                    clip_width - 2.0,
                    GVR_MT_LANE_HEIGHT - 16.0);
    cairo_fill_preserve(cr);

    cairo_set_source_rgba(cr,
                          master ? 1.0 : 0.82,
                          master ? 0.52 : 0.86,
                          master ? 0.08 : 1.0,
                          0.95);
    cairo_set_line_width(cr, master ? 1.8 : 1.0);
    if(live)
        cairo_set_dash(cr, (double[]){ 5.0, 3.0 }, 2, 0.0);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);

    if(title && title[0])
        g_snprintf(label, sizeof(label), "%s", title);
    else if(master)
        g_snprintf(label, sizeof(label), "Sample %d", sample_id);
    else
        g_snprintf(label, sizeof(label), "%sSample %d",
                   live ? "Live · " : "",
                   sample_id);

    g_snprintf(compact_label,
               sizeof(compact_label),
               "%c%d",
               sample_type == MODE_STREAM ? 'T' : 'S',
               sample_id);
    label_width = MAX(8.0, clip_width - 14.0);
    if(!gvr_mt_text_fits(view,
                         cr,
                         label,
                         label_width,
                         0.78,
                         PANGO_WEIGHT_BOLD)) {
        g_strlcpy(label, compact_label, sizeof(label));
        compact = TRUE;
    }
    label_x = x1 + (compact ? 4.0 : 7.0);
    label_width = MAX(8.0, clip_width - (compact ? 8.0 : 14.0));

    gvr_mt_draw_text(view,
                     cr,
                     label,
                     label_x,
                     lane_y + 15.0,
                     label_width,
                     0.78,
                     PANGO_WEIGHT_BOLD,
                     &white);

    if(clip_width > 110.0) {
        gchar range[80];
        g_snprintf(range,
                   sizeof(range),
                   "%d–%d · %d frames",
                   project_in,
                   project_out,
                   project_out - project_in + 1);
        gvr_mt_draw_text(view,
                         cr,
                         range,
                         x1 + 7.0,
                         lane_y + 37.0,
                         MAX(8.0, clip_width - 14.0),
                         0.66,
                         PANGO_WEIGHT_NORMAL,
                         &white);
    }
}

static void gvr_mt_repeat_title(char *dst,
                                gsize dst_size,
                                const GvrMultiTrackClipData *clip,
                                int repeat_index,
                                const char *fallback)
{
    const char *title = clip->title && clip->title[0] ? clip->title : fallback;

    if(repeat_index <= 0) {
        if(clip->loop_type == 2) {
            g_snprintf(dst,
                       dst_size,
                       "%s %s",
                       clip->speed < 0 ? "←" : "→",
                       title ? title : "Ping-pong");
        }
        else if(clip->loop_type == 3) {
            g_snprintf(dst,
                       dst_size,
                       "◆ %s",
                       title ? title : "Random");
        }
        else {
            g_strlcpy(dst, title ? title : "", dst_size);
        }
        return;
    }

    switch(clip->loop_type) {
        case 2: {
            const gboolean reverse = (clip->speed < 0) ^ ((repeat_index & 1) != 0);
            g_snprintf(dst,
                       dst_size,
                       "%s %d · %s",
                       reverse ? "←" : "→",
                       repeat_index + 1,
                       title ? title : "Ping-pong");
            break;
        }
        case 3:
            g_snprintf(dst,
                       dst_size,
                       "◆ %d · %s",
                       repeat_index + 1,
                       title ? title : "Random");
            break;
        default:
            g_snprintf(dst,
                       dst_size,
                       "↻ %d · %s",
                       repeat_index + 1,
                       title ? title : "Repeat");
            break;
    }
}

static void gvr_mt_draw_clip_instance(GvrMultiTrackEdit *view,
                                      cairo_t *cr,
                                      int width,
                                      int lane_y,
                                      const GvrMultiTrackClipData *clip,
                                      int project_in,
                                      int project_out,
                                      const char *title,
                                      gboolean repeated)
{
    const int instance_length = project_out - project_in + 1;
    const int source_length = clip->source_length;

    if(source_length <= 0 || source_length >= instance_length) {
        gvr_mt_draw_clip(view,
                         cr,
                         width,
                         lane_y,
                         project_in,
                         project_out,
                         clip->sample_id,
                         clip->sample_type,
                         title,
                         repeated,
                         FALSE);
        return;
    }

    for(int tile = 0, start = project_in;
        start <= project_out;
        tile++, start += source_length) {
        const int end = MIN(project_out, start + source_length - 1);
        char tile_title[128];

        if(tile == 0)
            g_strlcpy(tile_title, title ? title : "", sizeof(tile_title));
        else
            gvr_mt_repeat_title(tile_title,
                                sizeof(tile_title),
                                clip,
                                tile,
                                title);

        gvr_mt_draw_clip(view,
                         cr,
                         width,
                         lane_y,
                         start,
                         end,
                         clip->sample_id,
                         clip->sample_type,
                         tile_title,
                         repeated || tile > 0,
                         FALSE);
    }
}

static void gvr_mt_draw_repeated_clip(GvrMultiTrackEdit *view,
                                      cairo_t *cr,
                                      int width,
                                      int lane_y,
                                      const GvrMultiTrackClipData *clip)
{
    const int repeat_period = clip->repeat_period;
    const int repeat_until = MAX(clip->project_out, clip->repeat_until);

    if(repeat_period <= 0 || repeat_until <= clip->project_out) {
        gvr_mt_draw_clip_instance(view,
                                  cr,
                                  width,
                                  lane_y,
                                  clip,
                                  clip->project_in,
                                  clip->project_out,
                                  clip->title,
                                  FALSE);
        return;
    }

    const int visible_start = view->timeline_view_start;
    const int visible_end = visible_start + gvr_mt_visible_frames(view) - 1;
    int first_cycle = 0;

    if(visible_start > clip->project_out) {
        first_cycle = (visible_start - clip->project_out + repeat_period - 1) /
                      repeat_period;
    }

    for(int cycle = MAX(0, first_cycle); ; cycle++) {
        const long long offset = (long long)cycle * (long long)repeat_period;
        const long long start64 = (long long)clip->project_in + offset;
        const long long end64 = (long long)clip->project_out + offset;
        int start;
        int end;

        if(start64 > repeat_until || start64 > visible_end || start64 > INT_MAX)
            break;
        start = (int)start64;
        end = (int)MIN((long long)repeat_until, end64);
        char cycle_title[128];

        if(end < start)
            break;

        gvr_mt_repeat_title(cycle_title,
                            sizeof(cycle_title),
                            clip,
                            cycle,
                            "Repeat");

        gvr_mt_draw_clip_instance(view,
                                  cr,
                                  width,
                                  lane_y,
                                  clip,
                                  start,
                                  end,
                                  cycle_title,
                                  cycle > 0);
    }
}

static void gvr_mt_draw_loop_continuation(GvrMultiTrackEdit *view,
                                          cairo_t *cr,
                                          int width,
                                          int lane_y,
                                          const GvrMultiTrackClipData *clip,
                                          int project_in,
                                          int project_out)
{
    const int visible_start = view->timeline_view_start;
    const int visible_end = visible_start + gvr_mt_visible_frames(view) - 1;
    const GdkRGBA white = { 0.97, 0.98, 1.0, 1.0 };
    GdkRGBA color;
    double x1;
    double x2;
    double band_width;
    char label[128];

    if(project_out < visible_start || project_in > visible_end)
        return;

    project_in = MAX(project_in, visible_start);
    project_out = MIN(project_out, visible_end);
    x1 = gvr_mt_frame_to_x(view, project_in, width);
    x2 = gvr_mt_frame_to_x(view, project_out + 1, width);
    band_width = MAX(2.0, x2 - x1);

    gvr_mt_source_color(clip->sample_type, &color);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.24);
    cairo_rectangle(cr,
                    x1 + 1.0,
                    lane_y + 8.0,
                    band_width - 2.0,
                    GVR_MT_LANE_HEIGHT - 16.0);
    cairo_fill(cr);

    cairo_save(cr);
    cairo_rectangle(cr,
                    x1 + 1.0,
                    lane_y + 8.0,
                    band_width - 2.0,
                    GVR_MT_LANE_HEIGHT - 16.0);
    cairo_clip(cr);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.34);
    cairo_set_line_width(cr, 1.0);
    const double hatch_start = x1 - GVR_MT_LANE_HEIGHT;
    const double hatch_span = (x2 - x1) + (2.0 * GVR_MT_LANE_HEIGHT);
    const int hatch_count = MAX(0, (int)ceil(hatch_span / 14.0));
    for(int hatch = 0; hatch < hatch_count; hatch++) {
        const double x = hatch_start + ((double)hatch * 14.0);
        cairo_move_to(cr, x, lane_y + GVR_MT_LANE_HEIGHT - 8.0);
        cairo_line_to(cr, x + GVR_MT_LANE_HEIGHT - 16.0, lane_y + 8.0);
    }
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_set_source_rgba(cr, 0.82, 0.86, 1.0, 0.90);
    cairo_set_line_width(cr, 1.0);
    cairo_set_dash(cr, (double[]){ 5.0, 4.0 }, 2, 0.0);
    cairo_rectangle(cr,
                    x1 + 1.0,
                    lane_y + 8.0,
                    band_width - 2.0,
                    GVR_MT_LANE_HEIGHT - 16.0);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);

    switch(clip->loop_type) {
        case 2:
            g_snprintf(label,
                       sizeof(label),
                       "∞ PING-PONG · %s",
                       clip->title && clip->title[0] ? clip->title : "Sample");
            break;
        case 3:
            g_snprintf(label,
                       sizeof(label),
                       "∞ RANDOM · %s",
                       clip->title && clip->title[0] ? clip->title : "Sample");
            break;
        default:
            g_snprintf(label,
                       sizeof(label),
                       "∞ LOOP · %s",
                       clip->title && clip->title[0] ? clip->title : "Sample");
            break;
    }

    gvr_mt_draw_text(view,
                     cr,
                     label,
                     x1 + 7.0,
                     lane_y + 25.0,
                     MAX(8.0, band_width - 14.0),
                     0.72,
                     PANGO_WEIGHT_BOLD,
                     &white);
}

static void gvr_mt_draw_lane_sample_phase(GvrMultiTrackEdit *view,
                                          cairo_t *cr,
                                          int width,
                                          int lane_y,
                                          const GvrMultiTrackLane *lane,
                                          const GvrMultiTrackClipData *clip)
{
    const int project_length = clip->project_out - clip->project_in + 1;
    int local_frame;
    int project_frame;
    double x;

    if(lane->total_frames <= 0 || project_length <= 0)
        return;

    local_frame = gvr_mt_clampi(lane->frame,
                                0,
                                MAX(0, lane->total_frames - 1));
    if(lane->total_frames <= 1 || project_length <= 1)
        project_frame = clip->project_in;
    else
        project_frame = clip->project_in +
            (int)llround((double)local_frame * (double)(project_length - 1) /
                         (double)(lane->total_frames - 1));

    x = floor(gvr_mt_frame_to_x(view, project_frame, width)) + 0.5;
    if(x < -4.0 || x > width + 4.0)
        return;

    cairo_set_source_rgba(cr, 0.30, 0.90, 1.0, 0.96);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, x, lane_y + 7.0);
    cairo_line_to(cr, x, lane_y + GVR_MT_LANE_HEIGHT - 7.0);
    cairo_stroke(cr);

    cairo_move_to(cr, x, lane_y + 7.0);
    cairo_line_to(cr, x - 4.0, lane_y + 13.0);
    cairo_line_to(cr, x + 4.0, lane_y + 13.0);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void gvr_mt_draw_direct_sample_clip(GvrMultiTrackEdit *view,
                                           cairo_t *cr,
                                           int width,
                                           int lane_y,
                                           int track,
                                           const GvrMultiTrackLane *lane,
                                           const GvrMultiTrackClipData *clip)
{
    const int repeat_until = MAX(clip->project_out, clip->repeat_until);

    gvr_mt_draw_clip_instance(view,
                              cr,
                              width,
                              lane_y,
                              clip,
                              clip->project_in,
                              clip->project_out,
                              clip->title,
                              FALSE);

    if(repeat_until > clip->project_out) {
        gvr_mt_draw_loop_continuation(view,
                                      cr,
                                      width,
                                      lane_y,
                                      clip,
                                      clip->project_out + 1,
                                      repeat_until);
    }

    if(track != view->project_master_track)
        gvr_mt_draw_lane_sample_phase(view,
                                      cr,
                                      width,
                                      lane_y,
                                      lane,
                                      clip);
}

static void gvr_mt_draw_event(GvrMultiTrackEdit *view,
                              cairo_t *cr,
                              int width,
                              int lane_y,
                              const GvrMultiTrackEventData *event)
{
    const GdkRGBA marker = { 1.0, 0.36, 0.82, 1.0 };
    const GdkRGBA text = { 1.0, 0.93, 0.98, 1.0 };
    const double x = gvr_mt_frame_to_x(view, event->project_frame, width);
    const double y = lane_y + 5.0 + (event->order % 3) * 18.0;
    const char *label = event->label && event->label[0] ?
                        event->label : "VIMS";

    if(x < -40.0 || x > width + 40.0)
        return;

    gvr_mt_set_source(cr, &marker);
    cairo_move_to(cr, x, lane_y + 2.0);
    cairo_line_to(cr, x - 5.0, lane_y + 9.0);
    cairo_line_to(cr, x + 5.0, lane_y + 9.0);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.30, 0.08, 0.25, 0.92);
    cairo_rectangle(cr, x + 4.0, y, 92.0, 16.0);
    cairo_fill(cr);
    gvr_mt_draw_text(view,
                     cr,
                     label,
                     x + 8.0,
                     y + 1.0,
                     84.0,
                     0.62,
                     PANGO_WEIGHT_BOLD,
                     &text);
}

static gboolean gvr_mt_timeline_draw(GtkWidget *widget,
                                     cairo_t *cr,
                                     gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    GtkAllocation allocation;
    const GdkRGBA fallback_bg = { 0.15, 0.16, 0.19, 1.0 };
    const GdkRGBA fallback_panel = { 0.18, 0.19, 0.22, 1.0 };
    const GdkRGBA fallback_border = { 0.24, 0.26, 0.31, 1.0 };
    const GdkRGBA fallback_accent = { 0.35, 0.43, 0.55, 1.0 };
    const GdkRGBA fallback_master = { 0.66, 0.54, 0.28, 1.0 };
    GdkRGBA bg;
    GdkRGBA panel;
    GdkRGBA border;
    GdkRGBA accent;
    GdkRGBA master;

    gtk_widget_get_allocation(widget, &allocation);
    gvr_mt_draw_context_begin(view, cr);
    gvr_mt_lookup_color(widget, "bg-color", &fallback_bg, &bg);
    gvr_mt_lookup_color(widget, "panel-color", &fallback_panel, &panel);
    gvr_mt_lookup_color(widget, "border-color", &fallback_border, &border);
    gvr_mt_lookup_color(widget, "select-color", &fallback_accent, &accent);
    gvr_mt_lookup_color(widget, "warn-color", &fallback_master, &master);

    gvr_mt_set_source(cr, &bg);
    cairo_paint(cr);
    gvr_mt_draw_ruler(view, cr, allocation.width);

    for(int track = 0; track < view->max_tracks; track++) {
        GvrMultiTrackLane *lane = &view->lanes[track];
        const int y = gvr_mt_track_y(view, track);

        if(y < 0)
            continue;

        if(track & 1)
            cairo_set_source_rgba(cr,
                                  panel.red * 0.94,
                                  panel.green * 0.94,
                                  panel.blue * 0.94,
                                  1.0);
        else
            gvr_mt_set_source(cr, &panel);
        cairo_rectangle(cr, 0, y, allocation.width, GVR_MT_LANE_HEIGHT);
        cairo_fill(cr);

        if(track == view->project_master_track) {
            cairo_set_source_rgba(cr,
                                  master.red,
                                  master.green,
                                  master.blue,
                                  0.11);
            cairo_rectangle(cr, 0, y, allocation.width, GVR_MT_LANE_HEIGHT);
            cairo_fill(cr);
            cairo_set_source_rgba(cr,
                                  master.red,
                                  master.green,
                                  master.blue,
                                  0.90);
            cairo_rectangle(cr, 0, y, 4.0, GVR_MT_LANE_HEIGHT);
            cairo_fill(cr);
        }

        if(track == view->selected_track) {
            cairo_set_source_rgba(cr,
                                  accent.red,
                                  accent.green,
                                  accent.blue,
                                  0.16);
            cairo_rectangle(cr, 0, y, allocation.width, GVR_MT_LANE_HEIGHT);
            cairo_fill(cr);
        }

        if(track == view->hover_track && !view->source_drag_active) {
            cairo_set_source_rgba(cr, 0.72, 0.78, 0.90, 0.07);
            cairo_rectangle(cr, 0, y, allocation.width, GVR_MT_LANE_HEIGHT);
            cairo_fill(cr);
        }

        gvr_mt_set_source(cr, &border);
        cairo_move_to(cr, 0, y + GVR_MT_LANE_HEIGHT - 0.5);
        cairo_line_to(cr, allocation.width, y + GVR_MT_LANE_HEIGHT - 0.5);
        cairo_stroke(cr);

        if(track == view->project_master_track &&
           lane->sequence_active &&
           view->master_clips &&
           view->master_clips->len > 0) {
            for(guint i = 0; i < view->master_clips->len; i++) {
                GvrMultiTrackMasterClip *clip =
                    &g_array_index(view->master_clips,
                                   GvrMultiTrackMasterClip,
                                   i);
                gchar label[64];
                g_snprintf(label,
                           sizeof(label),
                           "#%d - %s %d",
                           clip->slot + 1,
                           clip->sample_type == 0 ? "Sample" : "Stream",
                           clip->sample_id);
                gvr_mt_draw_clip(view,
                                 cr,
                                 allocation.width,
                                 y,
                                 clip->project_in,
                                 clip->project_out,
                                 clip->sample_id,
                                 clip->sample_type,
                                 label,
                                 FALSE,
                                 TRUE);
            }
        }
        else if(lane->clips && lane->clips->len > 0) {
            const gboolean direct_sample =
                !lane->sequence_active &&
                (lane->play_mode == MODE_SAMPLE ||
                 lane->play_mode == MODE_PATTERN);
            const int direct_horizon = direct_sample ?
                gvr_mt_effective_total(view) : 0;

            for(guint i = 0; i < lane->clips->len; i++) {
                GvrMultiTrackClipData *clip =
                    g_ptr_array_index(lane->clips, i);
                GvrMultiTrackClipData draw_clip = *clip;

                if(direct_sample && direct_horizon > draw_clip.project_out + 1) {
                    int repeat_period = draw_clip.repeat_period;

                    if(repeat_period <= 0)
                        repeat_period = draw_clip.source_length;
                    if(repeat_period <= 0)
                        repeat_period = draw_clip.project_out -
                                        draw_clip.project_in + 1;

                    if(repeat_period > 0) {
                        draw_clip.repeat_period = repeat_period;
                        draw_clip.repeat_until = MAX(draw_clip.repeat_until,
                                                     direct_horizon - 1);
                    }
                }

                if(direct_sample)
                    gvr_mt_draw_direct_sample_clip(view,
                                                   cr,
                                                   allocation.width,
                                                   y,
                                                   track,
                                                   lane,
                                                   &draw_clip);
                else
                    gvr_mt_draw_repeated_clip(view,
                                              cr,
                                              allocation.width,
                                              y,
                                              &draw_clip);
            }
        }
        else if(track == view->project_master_track &&
                lane->play_mode == MODE_STREAM &&
                lane->source_id > 0 &&
                lane->stream_buffer_supported) {
            const int total = MAX(1, MAX(lane->stream_buffer_capacity,
                                         lane->stream_buffer_filled));
            const int filled = MIN(total, lane->stream_buffer_filled);
            const int start = total - filled;
            const double x1 = gvr_mt_frame_to_x(view, start, allocation.width);
            const double x2 = gvr_mt_frame_to_x(view, total, allocation.width);
            gchar label[96];

            cairo_set_source_rgba(cr, 0.18, 0.22, 0.28, 0.65);
            cairo_rectangle(cr, 0, y + 9, allocation.width,
                            GVR_MT_LANE_HEIGHT - 18);
            cairo_fill(cr);
            if(filled > 0) {
                cairo_set_source_rgba(cr, 0.18, 0.55, 0.68, 0.72);
                cairo_rectangle(cr, x1, y + 9, MAX(1.0, x2 - x1),
                                GVR_MT_LANE_HEIGHT - 18);
                cairo_fill(cr);
            }
            g_snprintf(label, sizeof(label),
                       "Stream %d · buffered %d / %d",
                       lane->source_id, filled, total);
            {
                GdkRGBA white = { 0.92, 0.94, 0.98, 1.0 };
                gvr_mt_draw_text(view, cr, label, 8, y + 24,
                                 allocation.width - 16, 0.88,
                                 PANGO_WEIGHT_BOLD, &white);
            }
        }
        else if(track == view->project_master_track &&
                view->master_clips &&
                view->master_clips->len > 0) {
            for(guint i = 0; i < view->master_clips->len; i++) {
                GvrMultiTrackMasterClip *clip =
                    &g_array_index(view->master_clips,
                                   GvrMultiTrackMasterClip,
                                   i);
                gchar label[64];
                g_snprintf(label,
                           sizeof(label),
                           "#%d - %s %d",
                           clip->slot + 1,
                           clip->sample_type == 0 ? "Sample" : "Stream",
                           clip->sample_id);
                gvr_mt_draw_clip(view,
                                 cr,
                                 allocation.width,
                                 y,
                                 clip->project_in,
                                 clip->project_out,
                                 clip->sample_id,
                                 clip->sample_type,
                                 label,
                                 FALSE,
                                 TRUE);
            }
        }

        for(guint i = 0; lane->events && i < lane->events->len; i++)
            gvr_mt_draw_event(view,
                              cr,
                              allocation.width,
                              y,
                              g_ptr_array_index(lane->events, i));
    }

    if(view->hover_frame >= 0 && view->hover_track >= 0 &&
       !view->source_drag_active && !view->seek_dragging) {
        const double hover_x = floor(gvr_mt_frame_to_x(
            view, view->hover_frame, allocation.width)) + 0.5;
        cairo_set_source_rgba(cr, 0.78, 0.82, 0.90, 0.22);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, hover_x, GVR_MT_RULER_HEIGHT);
        cairo_line_to(cr, hover_x,
                      GVR_MT_RULER_HEIGHT +
                      gvr_mt_visible_track_count(view) * GVR_MT_LANE_HEIGHT);
        cairo_stroke(cr);
    }

    if(view->pending_source_track >= 0 &&
       view->pending_source_track < view->max_tracks) {
        const int pending_y = gvr_mt_track_y(view, view->pending_source_track);
        if(pending_y >= 0) {
            GdkRGBA pending_text = { 0.72, 0.88, 1.0, 1.0 };
            char pending_label[96];
            cairo_set_source_rgba(cr, 0.42, 0.72, 0.95, 0.85);
            cairo_set_line_width(cr, 2.0);
            cairo_rectangle(cr, 2.0, pending_y + 2.0,
                            MAX(1.0, allocation.width - 4.0),
                            GVR_MT_LANE_HEIGHT - 4.0);
            cairo_stroke(cr);
            if(view->pending_source_kind == 2)
                g_strlcpy(pending_label,
                          "Waiting for sequence refresh…",
                          sizeof(pending_label));
            else
                g_snprintf(pending_label, sizeof(pending_label),
                           "Switching to %s %d…",
                           view->pending_source_type == 0 ? "Sample" : "Stream",
                           view->pending_source_id);
            gvr_mt_draw_text(view, cr, pending_label, 10, pending_y + 62,
                             allocation.width - 20, 0.75,
                             PANGO_WEIGHT_BOLD, &pending_text);
        }
    }

    if(view->source_drag_active &&
       view->source_drag_track >= 0 &&
       view->source_drag_track < view->max_tracks) {
        const int y = gvr_mt_track_y(view, view->source_drag_track);
        cairo_set_source_rgba(cr, 0.35, 0.72, 0.95, 0.20);
        cairo_rectangle(cr, 0, y, allocation.width, GVR_MT_LANE_HEIGHT);
        cairo_fill(cr);

        if(view->source_drag_kind == 2) {
            const int insert_frame =
                gvr_mt_sequence_insertion_frame(view, view->source_drag_slot);
            const double insert_x = floor(gvr_mt_frame_to_x(
                view, insert_frame, allocation.width)) + 0.5;
            cairo_set_source_rgba(cr, 0.45, 0.88, 1.0, 1.0);
            cairo_set_line_width(cr, 3.0);
            cairo_move_to(cr, insert_x, y + 4);
            cairo_line_to(cr, insert_x, y + GVR_MT_LANE_HEIGHT - 4);
            cairo_stroke(cr);
        }
    }

    if(view->playhead_active) {
        const double x = floor(gvr_mt_frame_to_x(view,
                                                 gvr_mt_display_playhead(view),
                                                 allocation.width)) + 0.5;
        cairo_set_source_rgba(cr, 1.0, 0.52, 0.0, 1.0);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr,
                      x,
                      GVR_MT_RULER_HEIGHT +
                      gvr_mt_visible_track_count(view) * GVR_MT_LANE_HEIGHT);
        cairo_stroke(cr);
    }

    gvr_mt_draw_context_end(view);
    return TRUE;
}

static gboolean gvr_mt_navigator_draw(GtkWidget *widget,
                                      cairo_t *cr,
                                      gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    GtkAllocation allocation;
    const int total = gvr_mt_effective_total(view);
    const int visible = gvr_mt_visible_frames(view);
    double x1;
    double x2;

    gtk_widget_get_allocation(widget, &allocation);
    cairo_set_source_rgba(cr, 0.10, 0.11, 0.14, 1.0);
    cairo_paint(cr);

    if(view->master_clips) {
        for(guint i = 0; i < view->master_clips->len; i++) {
            GvrMultiTrackMasterClip *clip =
                &g_array_index(view->master_clips,
                               GvrMultiTrackMasterClip,
                               i);
            GdkRGBA color;
            gvr_mt_source_color(clip->sample_type, &color);
            x1 = ((double)clip->project_in / total) * allocation.width;
            x2 = ((double)(clip->project_out + 1) / total) * allocation.width;
            gvr_mt_set_source(cr, &color);
            cairo_rectangle(cr,
                            x1,
                            4.0,
                            MAX(1.0, x2 - x1),
                            allocation.height - 8.0);
            cairo_fill(cr);
        }
    }

    x1 = ((double)view->timeline_view_start / total) * allocation.width;
    x2 = ((double)(view->timeline_view_start + visible) / total) * allocation.width;
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.18);
    cairo_rectangle(cr, x1, 1.0, MAX(2.0, x2 - x1), allocation.height - 2.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.65, 0.72, 0.86, 0.9);
    cairo_stroke(cr);

    if(view->playhead_active) {
        const double px = ((double)gvr_mt_display_playhead(view) / total) * allocation.width;
        cairo_set_source_rgba(cr, 1.0, 0.52, 0.0, 1.0);
        cairo_move_to(cr, px, 0);
        cairo_line_to(cr, px, allocation.height);
        cairo_stroke(cr);
    }

    return TRUE;
}

static void gvr_mt_select_track(GvrMultiTrackEdit *view,
                                int track,
                                gboolean emit)
{
    if(track < 0 || track >= view->max_tracks)
        return;
    if(view->selected_track == track && !emit)
        return;

    view->selected_track = track;
    gvr_mt_refresh_headers(view);
    gvr_mt_update_summary(view);
    gvr_mt_queue_timeline_draw(view);

    if(emit)
        g_signal_emit(view,
                      gvr_multi_track_edit_signals[SIGNAL_TRACK_SELECTED],
                      0,
                      track);
}

static gboolean gvr_mt_header_button_press(GtkWidget *widget,
                                           GdkEventButton *event,
                                           gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    int track = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
                                                  "gvr-track"));
    if(event->button == 1)
        gvr_mt_select_track(view, track, TRUE);
    return FALSE;
}

static void gvr_mt_switch_clicked(GtkButton *button, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    int track = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),
                                                  "gvr-track"));

    if(track < 0 || track >= view->max_tracks ||
       !view->lanes[track].connected ||
       view->lanes[track].current_control)
        return;

    g_signal_emit(view,
                  gvr_multi_track_edit_signals[SIGNAL_SWITCH_REQUESTED],
                  0,
                  track);
}

static void gvr_mt_preview_toggled(GtkToggleButton *button, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    int track = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),
                                                  "gvr-track"));
    gboolean enabled;

    if(view->syncing_preview_toggle ||
       track < 0 || track >= view->max_tracks ||
       !view->lanes[track].connected)
        return;

    enabled = gtk_toggle_button_get_active(button);
    view->lanes[track].preview_enabled = enabled;
    gvr_mt_set_preview_icon(GTK_WIDGET(button), enabled);
    g_signal_emit(view,
                  gvr_multi_track_edit_signals[SIGNAL_PREVIEW_TOGGLED],
                  0,
                  track,
                  enabled);
}

static void gvr_mt_a_clicked(GtkButton *button, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    (void)button;

    if(view)
        gvr_mt_refresh_headers(view);
}

static void gvr_mt_b_clicked(GtkButton *button, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    int track = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),
                                                  "gvr-track"));

    if(track < 0 || track >= view->max_tracks ||
       !view->lanes[track].connected ||
       view->lanes[track].project_master ||
       view->transition_active ||
       view->active_bus == 1)
        return;

    view->bus_b_track = track;
    g_signal_emit(view,
                  gvr_multi_track_edit_signals[SIGNAL_TRANSITION_SOURCE_SELECTED],
                  0,
                  track,
                  1);
    gvr_mt_select_track(view, track, TRUE);
    gvr_mt_refresh_headers(view);
}

static void gvr_mt_transition_request(GvrMultiTrackEdit *view, int duration)
{
    int target_track;

    if(!view || view->transition_active ||
       view->bus_a_track < 0 ||
       view->bus_b_track < 0 ||
       view->bus_a_track == view->bus_b_track)
        return;

    target_track = view->active_bus == 1 ?
                   view->bus_a_track : view->bus_b_track;
    view->transition_method = gtk_combo_box_get_active(
        GTK_COMBO_BOX(view->transition_method_combo));
    if(view->transition_method != VJ_MULTITRACK_TRANSITION_SHAPE_WIPE ||
       view->transition_shape_count == 0)
        view->transition_method = VJ_MULTITRACK_TRANSITION_DISSOLVE;
    view->transition_shape = gvr_shape_selector_get_active(
        view->transition_shape_selector);

    g_signal_emit(view,
                  gvr_multi_track_edit_signals[SIGNAL_TRANSITION_REQUESTED],
                  0,
                  target_track,
                  MAX(0, duration),
                  view->transition_method,
                  view->transition_shape);
}

static void gvr_mt_transition_take_clicked(GtkButton *button, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    int duration = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(view->transition_duration_spin));
    (void)button;
    gvr_mt_transition_request(view, duration);
}

static void gvr_mt_transition_cut_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    gvr_mt_transition_request(GVR_MULTI_TRACK_EDIT(user_data), 0);
}

static void gvr_mt_transition_duration_changed(GtkSpinButton *spin,
                                                gpointer user_data)
{
    (void)spin;
    gvr_mt_update_transition_ui(GVR_MULTI_TRACK_EDIT(user_data));
}

static void gvr_mt_drift_toggled(GtkToggleButton *button, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    gboolean enabled;

    if(view->syncing_drift_toggle)
        return;

    enabled = gtk_toggle_button_get_active(button);
    view->drift_lock_enabled = enabled;
    gvr_mt_refresh_headers(view);
    g_signal_emit(view,
                  gvr_multi_track_edit_signals[SIGNAL_DRIFT_LOCK_TOGGLED],
                  0,
                  enabled);
}

typedef enum {
    GVR_MT_MENU_FOLLOW_PLAYHEAD = 1,
    GVR_MT_MENU_CENTER_PLAYHEAD,
    GVR_MT_MENU_FIT_PROJECT,
    GVR_MT_MENU_ZOOM_CLIP,
    GVR_MT_MENU_SEEK_CLIP,
    GVR_MT_MENU_REVEAL_SLOT,
    GVR_MT_MENU_REVEAL_SOURCE,
    GVR_MT_MENU_FOCUS_TRACK,
    GVR_MT_MENU_SHOW_ALL,
    GVR_MT_MENU_RESYNC_TRACK,
    GVR_MT_MENU_BUFFER_START,
    GVR_MT_MENU_LIVE_EDGE,
    GVR_MT_MENU_FOLLOW_LIVE,
    GVR_MT_MENU_ZOOM_CURRENT_SLOT
} GvrMtMenuAction;

typedef struct {
    GvrMultiTrackEdit *view;
    GvrMtMenuAction action;
    int track;
    int frame;
    int slot;
    int sample_id;
    int sample_type;
    int clip_in;
    int clip_out;
} GvrMtMenuData;

static void gvr_mt_menu_data_free(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

static void gvr_mt_context_action(GtkMenuItem *item, gpointer user_data)
{
    GvrMtMenuData *data = user_data;
    GvrMultiTrackEdit *view = data->view;
    GvrMultiTrackLane *lane =
        data->track >= 0 && data->track < view->max_tracks ?
            &view->lanes[data->track] : NULL;

    switch(data->action) {
        case GVR_MT_MENU_FOLLOW_PLAYHEAD:
            view->follow_playhead =
                gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
            if(view->follow_playhead)
                gvr_mt_follow_transport(view);
            break;
        case GVR_MT_MENU_CENTER_PLAYHEAD:
            if(view->playhead_active)
                gvr_mt_center_frame(view, gvr_mt_display_playhead(view));
            break;
        case GVR_MT_MENU_FIT_PROJECT:
            view->timeline_view_start = 0;
            gtk_range_set_value(GTK_RANGE(view->zoom_scale),
                                GVR_MT_ZOOM_MIN);
            gvr_mt_update_pan(view);
            gvr_mt_queue_draw(view);
            break;
        case GVR_MT_MENU_ZOOM_CLIP:
            gvr_mt_zoom_to_range(view, data->clip_in, data->clip_out);
            break;
        case GVR_MT_MENU_SEEK_CLIP:
            if(view->transport_seekable)
                g_signal_emit(view,
                              gvr_multi_track_edit_signals[SIGNAL_SEEK_REQUESTED],
                              0,
                              data->clip_in);
            break;
        case GVR_MT_MENU_REVEAL_SLOT:
            g_signal_emit(view,
                          gvr_multi_track_edit_signals[SIGNAL_REVEAL_SEQUENCE_SLOT_REQUESTED],
                          0,
                          lane ? lane->sequence_bank : view->bank,
                          data->slot);
            break;
        case GVR_MT_MENU_REVEAL_SOURCE:
            g_signal_emit(view,
                          gvr_multi_track_edit_signals[SIGNAL_REVEAL_SOURCE_REQUESTED],
                          0,
                          data->sample_id,
                          data->sample_type);
            break;
        case GVR_MT_MENU_FOCUS_TRACK:
            view->focused_track = data->track;
            gvr_mt_update_focus_layout(view);
            break;
        case GVR_MT_MENU_SHOW_ALL:
            view->focused_track = -1;
            gvr_mt_update_focus_layout(view);
            break;
        case GVR_MT_MENU_RESYNC_TRACK:
            g_signal_emit(view,
                          gvr_multi_track_edit_signals[SIGNAL_RESYNC_REQUESTED],
                          0,
                          data->track);
            break;
        case GVR_MT_MENU_BUFFER_START:
            g_signal_emit(view,
                          gvr_multi_track_edit_signals[SIGNAL_SEEK_REQUESTED],
                          0, 0);
            break;
        case GVR_MT_MENU_LIVE_EDGE:
            if(lane && lane->stream_buffer_filled > 0)
                g_signal_emit(view,
                              gvr_multi_track_edit_signals[SIGNAL_SEEK_REQUESTED],
                              0, lane->stream_buffer_filled - 1);
            break;
        case GVR_MT_MENU_FOLLOW_LIVE:
            view->follow_live_edge =
                gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
            if(view->follow_live_edge && lane && lane->stream_buffer_filled > 0) {
                g_signal_emit(view,
                              gvr_multi_track_edit_signals[SIGNAL_SEEK_REQUESTED],
                              0, lane->stream_buffer_filled - 1);
                gvr_mt_follow_transport(view);
            }
            break;
        case GVR_MT_MENU_ZOOM_CURRENT_SLOT:
            if(view->master_clips) {
                for(guint i = 0; i < view->master_clips->len; i++) {
                    GvrMultiTrackMasterClip *clip =
                        &g_array_index(view->master_clips,
                                       GvrMultiTrackMasterClip, i);
                    if(clip->slot == (lane ? lane->sequence_slot : view->playhead_slot)) {
                        gvr_mt_zoom_to_range(view,
                                             clip->project_in,
                                             clip->project_out);
                        break;
                    }
                }
            }
            break;
    }
}

static GtkWidget *gvr_mt_context_item(GtkWidget *menu,
                                      const char *label,
                                      gboolean check,
                                      gboolean active,
                                      GvrMultiTrackEdit *view,
                                      GvrMtMenuAction action,
                                      int track,
                                      int frame,
                                      const GvrMultiTrackMasterClip *clip)
{
    GtkWidget *item = check ?
        gtk_check_menu_item_new_with_label(label) :
        gtk_menu_item_new_with_label(label);
    GvrMtMenuData *data = g_new0(GvrMtMenuData, 1);

    data->view = view;
    data->action = action;
    data->track = track;
    data->frame = frame;
    if(clip) {
        data->slot = clip->slot;
        data->sample_id = clip->sample_id;
        data->sample_type = clip->sample_type;
        data->clip_in = clip->project_in;
        data->clip_out = clip->project_out;
    }
    else if(track >= 0 && track < view->max_tracks) {
        data->sample_id = view->lanes[track].source_id;
        data->sample_type = view->lanes[track].source_type;
    }

    if(check)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
    g_signal_connect_data(item,
                          "activate",
                          G_CALLBACK(gvr_mt_context_action),
                          data,
                          gvr_mt_menu_data_free,
                          0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

static GvrMultiTrackMasterClip *gvr_mt_master_clip_at(GvrMultiTrackEdit *view,
                                                       int track,
                                                       int frame)
{
    if(!view || track < 0 || track >= view->max_tracks ||
       track != view->project_master_track ||
       !view->lanes[track].sequence_active || !view->master_clips)
        return NULL;

    for(guint i = 0; i < view->master_clips->len; i++) {
        GvrMultiTrackMasterClip *clip =
            &g_array_index(view->master_clips, GvrMultiTrackMasterClip, i);
        if(frame >= clip->project_in && frame <= clip->project_out)
            return clip;
    }
    return NULL;
}

static void gvr_mt_show_context_menu(GvrMultiTrackEdit *view,
                                     GdkEventButton *event,
                                     int track,
                                     int frame)
{
    GtkWidget *menu = gtk_menu_new();
    GvrMultiTrackMasterClip *clip =
        track >= 0 ? gvr_mt_master_clip_at(view, track, frame) : NULL;
    GvrMultiTrackLane *lane =
        track >= 0 && track < view->max_tracks ? &view->lanes[track] : NULL;

    gvr_mt_context_item(menu, "Follow Playhead", TRUE,
                        view->follow_playhead, view,
                        GVR_MT_MENU_FOLLOW_PLAYHEAD, track, frame, NULL);
    gvr_mt_context_item(menu, "Center Playhead", FALSE, FALSE, view,
                        GVR_MT_MENU_CENTER_PLAYHEAD, track, frame, NULL);
    gvr_mt_context_item(menu, "Fit Project", FALSE, FALSE, view,
                        GVR_MT_MENU_FIT_PROJECT, track, frame, NULL);

    if(clip) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                              gtk_separator_menu_item_new());
        gvr_mt_context_item(menu, "Zoom to Clip", FALSE, FALSE, view,
                            GVR_MT_MENU_ZOOM_CLIP, track, frame, clip);
        if(lane && clip->slot == lane->sequence_slot)
            gvr_mt_context_item(menu, "Seek to Clip Start", FALSE, FALSE, view,
                                GVR_MT_MENU_SEEK_CLIP, track, frame, clip);
        if(lane && lane->current_control)
            gvr_mt_context_item(menu, "Reveal Slot in Sequence Editor", FALSE, FALSE, view,
                                GVR_MT_MENU_REVEAL_SLOT, track, frame, clip);
        gvr_mt_context_item(menu, "Reveal Source in Sample Bank", FALSE, FALSE, view,
                            GVR_MT_MENU_REVEAL_SOURCE, track, frame, clip);
    }
    else if(lane && lane->sequence_active &&
            track == view->project_master_track) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                              gtk_separator_menu_item_new());
        gvr_mt_context_item(menu, "Zoom to Current Slot", FALSE, FALSE, view,
                            GVR_MT_MENU_ZOOM_CURRENT_SLOT, track, frame, NULL);
    }
    else if(lane && lane->source_id > 0) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                              gtk_separator_menu_item_new());
        gvr_mt_context_item(menu, "Reveal Current Source in Sample Bank", FALSE, FALSE, view,
                            GVR_MT_MENU_REVEAL_SOURCE, track, frame, NULL);
    }

    if(lane && track == view->project_master_track &&
       !lane->sequence_active && lane->play_mode == MODE_STREAM &&
       lane->stream_buffer_enabled && lane->stream_buffer_filled > 0) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                              gtk_separator_menu_item_new());
        gvr_mt_context_item(menu, "Jump to Buffer Start", FALSE, FALSE, view,
                            GVR_MT_MENU_BUFFER_START, track, frame, NULL);
        gvr_mt_context_item(menu, "Jump to Live Edge", FALSE, FALSE, view,
                            GVR_MT_MENU_LIVE_EDGE, track, frame, NULL);
        gvr_mt_context_item(menu, "Follow Live Edge", TRUE,
                            view->follow_live_edge, view,
                            GVR_MT_MENU_FOLLOW_LIVE, track, frame, NULL);
    }

    if(lane) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                              gtk_separator_menu_item_new());
        if(view->focused_track < 0)
            gvr_mt_context_item(menu, "Focus This Track", FALSE, FALSE, view,
                                GVR_MT_MENU_FOCUS_TRACK, track, frame, NULL);
        else
            gvr_mt_context_item(menu, "Show All Tracks", FALSE, FALSE, view,
                                GVR_MT_MENU_SHOW_ALL, track, frame, NULL);
        if(track != view->project_master_track)
            gvr_mt_context_item(menu, "Resynchronise This Track", FALSE, FALSE, view,
                                GVR_MT_MENU_RESYNC_TRACK, track, frame, NULL);
    }

    g_signal_connect_swapped(menu,
                             "selection-done",
                             G_CALLBACK(gtk_widget_destroy),
                             menu);
    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu),
                   NULL,
                   NULL,
                   NULL,
                   NULL,
                   event->button,
                   event->time);
}

static gboolean gvr_mt_timeline_button_press(GtkWidget *widget,
                                             GdkEventButton *event,
                                             gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    GtkAllocation allocation;
    int track;
    int frame;

    gtk_widget_grab_focus(widget);
    gtk_widget_get_allocation(widget, &allocation);

    if(event->button == 3) {
        track = gvr_mt_track_at_y(view, event->y);
        frame = gvr_mt_x_to_frame(view, event->x, allocation.width);
        if(track >= 0)
            gvr_mt_select_track(view, track, TRUE);
        gvr_mt_show_context_menu(view, event, track, frame);
        return TRUE;
    }
    if(event->button != 1)
        return FALSE;

    if(event->y >= GVR_MT_RULER_HEIGHT) {
        track = gvr_mt_track_at_y(view, event->y);
        if(track >= 0 && track < view->max_tracks)
            gvr_mt_select_track(view, track, TRUE);
    }

    if(!view->transport_seekable) {
        gdk_display_beep(gtk_widget_get_display(widget));
        return TRUE;
    }

    frame = gvr_mt_x_to_frame(view, event->x, allocation.width);
    if(gvr_mt_direct_stream_active(view)) {
        GvrMultiTrackLane *lane = gvr_mt_project_master_lane(view);
        const int total = MAX(1, MAX(lane->stream_buffer_capacity,
                                     lane->stream_buffer_filled));
        const int filled = MAX(1, MIN(total, lane->stream_buffer_filled));
        frame = gvr_mt_clampi(frame - (total - filled), 0, filled - 1);
    }

    view->seek_dragging = TRUE;
    view->playhead = frame;
    view->playhead_active = TRUE;
    gtk_grab_add(widget);
    gvr_mt_update_summary(view);
    gvr_mt_queue_draw(view);
    return TRUE;
}

static gboolean gvr_mt_timeline_button_release(GtkWidget *widget,
                                               GdkEventButton *event,
                                               gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    GtkAllocation allocation;
    int frame;

    if(event->button != 1 || !view->seek_dragging)
        return FALSE;

    gtk_widget_get_allocation(widget, &allocation);
    frame = gvr_mt_x_to_frame(view, event->x, allocation.width);
    if(gvr_mt_direct_stream_active(view)) {
        GvrMultiTrackLane *lane = gvr_mt_project_master_lane(view);
        const int total = MAX(1, MAX(lane->stream_buffer_capacity,
                                     lane->stream_buffer_filled));
        const int filled = MAX(1, MIN(total, lane->stream_buffer_filled));
        frame = gvr_mt_clampi(frame - (total - filled), 0, filled - 1);
    }

    view->seek_dragging = FALSE;
    view->playhead = frame;
    view->playhead_active = TRUE;
    if(gtk_widget_has_grab(widget))
        gtk_grab_remove(widget);
    gvr_mt_update_summary(view);
    gvr_mt_queue_draw(view);
    g_signal_emit(view,
                  gvr_multi_track_edit_signals[SIGNAL_SEEK_REQUESTED],
                  0,
                  frame);
    return TRUE;
}

static gboolean gvr_mt_timeline_motion(GtkWidget *widget,
                                       GdkEventMotion *event,
                                       gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    GtkAllocation allocation;

    gtk_widget_get_allocation(widget, &allocation);
    view->hover_frame = gvr_mt_x_to_frame(view, event->x, allocation.width);
    view->hover_track = gvr_mt_track_at_y(view, event->y);

    if(view->seek_dragging && (event->state & GDK_BUTTON1_MASK)) {
        int frame = view->hover_frame;
        if(gvr_mt_direct_stream_active(view)) {
            GvrMultiTrackLane *lane = gvr_mt_project_master_lane(view);
            const int total = MAX(1, MAX(lane->stream_buffer_capacity,
                                         lane->stream_buffer_filled));
            const int filled = MAX(1, MIN(total, lane->stream_buffer_filled));
            frame = gvr_mt_clampi(frame - (total - filled), 0, filled - 1);
        }
        view->playhead = frame;
        view->playhead_active = TRUE;
        gvr_mt_update_summary(view);
        gvr_mt_queue_draw(view);
        return TRUE;
    }

    return FALSE;
}

static gboolean gvr_mt_timeline_leave(GtkWidget *widget,
                                      GdkEventCrossing *event,
                                      gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    (void)widget;
    (void)event;
    view->hover_track = -1;
    view->hover_frame = -1;
    gvr_mt_queue_timeline_draw(view);
    return FALSE;
}

static gboolean gvr_mt_query_tooltip(GtkWidget *widget,
                                     gint x,
                                     gint y,
                                     gboolean keyboard_mode,
                                     GtkTooltip *tooltip,
                                     gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    GtkAllocation allocation;
    int track;
    int frame;
    char timecode[32];
    char text[256];

    (void)keyboard_mode;
    gtk_widget_get_allocation(widget, &allocation);
    frame = gvr_mt_x_to_frame(view, x, allocation.width);
    track = gvr_mt_track_at_y(view, y);
    gvr_mt_timecode(view, frame, timecode, sizeof(timecode));

    if(track >= 0 && track < view->max_tracks) {
        GvrMultiTrackLane *lane = &view->lanes[track];
        g_snprintf(text,
                   sizeof(text),
                   "Video %d · project frame %d · %s\n%s",
                   track + 1,
                   frame,
                   timecode,
                   lane->connected ?
                       (lane->sequence_active ?
                            "Drop a sample or stream to insert it into the active sequence." :
                            "Drop a sample or stream to start it on this video lane.") :
                       "This track is not connected.");
    }
    else
        g_snprintf(text, sizeof(text),
                   "Project frame %d · %s", frame, timecode);

    gtk_tooltip_set_text(tooltip, text);
    return TRUE;
}

static gboolean gvr_mt_timeline_scroll(GtkWidget *widget,
                                       GdkEventScroll *event,
                                       gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    GtkAllocation allocation;
    const gboolean zoom = (event->state & GDK_CONTROL_MASK) != 0;
    double dx = 0.0;
    double dy = 0.0;
    int direction = 0;

    gtk_widget_get_allocation(widget, &allocation);
    if(event->direction == GDK_SCROLL_SMOOTH) {
        dx = event->delta_x;
        dy = event->delta_y;
        direction = fabs(dx) > fabs(dy) ? (dx > 0.0 ? 1 : -1) :
                                         (dy > 0.0 ? 1 : -1);
    }
    else if(event->direction == GDK_SCROLL_UP ||
            event->direction == GDK_SCROLL_LEFT)
        direction = -1;
    else if(event->direction == GDK_SCROLL_DOWN ||
            event->direction == GDK_SCROLL_RIGHT)
        direction = 1;
    else
        return FALSE;

    if(zoom) {
        const int anchor = gvr_mt_x_to_frame(view, event->x, allocation.width);
        double value = view->timeline_zoom;
        value *= direction < 0 ? 1.25 : (1.0 / 1.25);
        value = CLAMP(value, GVR_MT_ZOOM_MIN, GVR_MT_ZOOM_MAX);
        gtk_range_set_value(GTK_RANGE(view->zoom_scale), value);
        view->timeline_view_start = anchor -
            (int)((event->x / MAX(1.0, (double)allocation.width)) *
                  gvr_mt_visible_frames(view));
    }
    else {
        const int step = MAX(1, gvr_mt_visible_frames(view) / 12);
        const double scale = event->direction == GDK_SCROLL_SMOOTH ?
            MAX(0.25, MIN(4.0, fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy))) : 1.0;
        view->timeline_view_start +=
            direction * MAX(1, (int)floor(step * scale + 0.5));
    }

    gvr_mt_update_pan(view);
    gvr_mt_queue_draw(view);
    return TRUE;
}

static gboolean gvr_mt_timeline_key_press(GtkWidget *widget,
                                          GdkEventKey *event,
                                          gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    const int pan_step = MAX(1, gvr_mt_visible_frames(view) /
                                ((event->state & GDK_SHIFT_MASK) ? 2 : 12));
    double zoom;

    (void)widget;
    switch(event->keyval) {
        case GDK_KEY_plus:
        case GDK_KEY_equal:
        case GDK_KEY_KP_Add:
            zoom = gtk_range_get_value(GTK_RANGE(view->zoom_scale));
            gtk_range_set_value(GTK_RANGE(view->zoom_scale),
                                MIN(GVR_MT_ZOOM_MAX, zoom * 1.25));
            return TRUE;
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract:
            zoom = gtk_range_get_value(GTK_RANGE(view->zoom_scale));
            gtk_range_set_value(GTK_RANGE(view->zoom_scale),
                                MAX(GVR_MT_ZOOM_MIN, zoom / 1.25));
            return TRUE;
        case GDK_KEY_0:
        case GDK_KEY_KP_0:
            view->timeline_view_start = 0;
            gtk_range_set_value(GTK_RANGE(view->zoom_scale),
                                GVR_MT_ZOOM_MIN);
            gvr_mt_update_pan(view);
            gvr_mt_queue_draw(view);
            return TRUE;
        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            if(view->playhead_active)
                gvr_mt_center_frame(view, gvr_mt_display_playhead(view));
            return TRUE;
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            view->timeline_view_start -= pan_step;
            gvr_mt_update_pan(view);
            gvr_mt_queue_draw(view);
            return TRUE;
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            view->timeline_view_start += pan_step;
            gvr_mt_update_pan(view);
            gvr_mt_queue_draw(view);
            return TRUE;
        default:
            return FALSE;
    }
}

static gboolean gvr_mt_navigator_button(GtkWidget *widget,
                                        GdkEventButton *event,
                                        gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    GtkAllocation allocation;
    const int total = gvr_mt_effective_total(view);
    const int visible = gvr_mt_visible_frames(view);
    int center;

    if(event->button != 1)
        return FALSE;

    gtk_widget_get_allocation(widget, &allocation);
    center = (int)floor((event->x / MAX(1.0, (double)allocation.width)) * total);
    view->timeline_view_start = center - visible / 2;
    gvr_mt_update_pan(view);
    gvr_mt_queue_draw(view);
    return TRUE;
}

static void gvr_mt_pan_changed(GtkAdjustment *adjustment, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    view->timeline_view_start = (int)floor(gtk_adjustment_get_value(adjustment) + 0.5);
    gvr_mt_queue_draw(view);
}

static void gvr_mt_zoom_changed(GtkRange *range, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    gchar label[32];

    view->timeline_zoom = gtk_range_get_value(range);
    gvr_mt_invalidate_geometry(view);
    g_snprintf(label, sizeof(label), "%.1fx", view->timeline_zoom);
    gtk_label_set_text(GTK_LABEL(view->zoom_label), label);
    gvr_mt_update_pan(view);
    gvr_mt_queue_draw(view);
}

static void gvr_mt_zoom_out(GtkButton *button, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    double value = gtk_range_get_value(GTK_RANGE(view->zoom_scale));
    (void)button;
    gtk_range_set_value(GTK_RANGE(view->zoom_scale),
                        MAX(GVR_MT_ZOOM_MIN, value / 1.5));
}

static void gvr_mt_zoom_in(GtkButton *button, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    double value = gtk_range_get_value(GTK_RANGE(view->zoom_scale));
    (void)button;
    gtk_range_set_value(GTK_RANGE(view->zoom_scale),
                        MIN(GVR_MT_ZOOM_MAX, value * 1.5));
}

static void gvr_mt_zoom_fit(GtkButton *button, gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    (void)button;
    view->timeline_view_start = 0;
    gtk_range_set_value(GTK_RANGE(view->zoom_scale), 1.0);
    gvr_mt_update_pan(view);
    gvr_mt_queue_draw(view);
}

static int gvr_mt_sequence_insertion_slot(GvrMultiTrackEdit *view,
                                          int frame)
{
    GvrMultiTrackMasterClip *last;

    if(!view || !view->master_clips || view->master_clips->len == 0)
        return 0;

    for(guint i = 0; i < view->master_clips->len; i++) {
        GvrMultiTrackMasterClip *clip =
            &g_array_index(view->master_clips, GvrMultiTrackMasterClip, i);
        const int midpoint = clip->project_in +
            MAX(0, clip->project_out - clip->project_in) / 2;

        if(frame <= clip->project_out) {
            if(frame <= midpoint)
                return MAX(0, clip->slot);
            if(i + 1 < view->master_clips->len) {
                GvrMultiTrackMasterClip *next =
                    &g_array_index(view->master_clips,
                                   GvrMultiTrackMasterClip,
                                   i + 1);
                return MAX(0, next->slot);
            }
            return MAX(0, clip->slot + 1);
        }
    }

    last = &g_array_index(view->master_clips,
                          GvrMultiTrackMasterClip,
                          view->master_clips->len - 1);
    return MAX(0, last->slot + 1);
}

static int gvr_mt_sequence_insertion_frame(GvrMultiTrackEdit *view,
                                           int insertion_slot)
{
    if(!view || !view->master_clips || view->master_clips->len == 0)
        return 0;

    for(guint i = 0; i < view->master_clips->len; i++) {
        GvrMultiTrackMasterClip *clip =
            &g_array_index(view->master_clips, GvrMultiTrackMasterClip, i);
        if(clip->slot >= insertion_slot)
            return clip->project_in;
    }

    {
        GvrMultiTrackMasterClip *last =
            &g_array_index(view->master_clips,
                           GvrMultiTrackMasterClip,
                           view->master_clips->len - 1);
        return last->project_out + 1;
    }
}

static gboolean gvr_mt_source_drop_target(GvrMultiTrackEdit *view,
                                          int x,
                                          int y,
                                          int width,
                                          int *track_out,
                                          int *kind_out,
                                          int *slot_out,
                                          int *frame_out)
{
    const int track = gvr_mt_track_at_y(view, y);
    GvrMultiTrackLane *lane;
    int frame;

    if(!view || track < 0 || track >= view->max_tracks)
        return FALSE;
    lane = &view->lanes[track];
    if(!lane->connected)
        return FALSE;

    frame = gvr_mt_x_to_frame(view, x, width);
    if(lane->sequence_active) {
        if(track != view->project_master_track || !lane->current_control)
            return FALSE;
        if(track_out)
            *track_out = track;
        if(kind_out)
            *kind_out = 2;
        if(slot_out)
            *slot_out = gvr_mt_sequence_insertion_slot(view, frame);
        if(frame_out)
            *frame_out = frame;
        return TRUE;
    }

    if(lane->play_mode != MODE_SAMPLE && lane->play_mode != MODE_STREAM)
        return FALSE;

    if(track_out)
        *track_out = track;
    if(kind_out)
        *kind_out = 1;
    if(slot_out)
        *slot_out = -1;
    if(frame_out)
        *frame_out = frame;
    return TRUE;
}

static gboolean gvr_mt_drag_motion(GtkWidget *widget,
                                   GdkDragContext *context,
                                   gint x,
                                   gint y,
                                   guint time,
                                   gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    GtkAllocation allocation;
    int track = -1;
    int kind = 0;
    int slot = -1;
    int frame = 0;
    gboolean valid;

    gtk_widget_get_allocation(widget, &allocation);
    if(x < 32) {
        view->timeline_view_start -= MAX(1, gvr_mt_visible_frames(view) / 30);
        gvr_mt_update_pan(view);
    }
    else if(x > allocation.width - 32) {
        view->timeline_view_start += MAX(1, gvr_mt_visible_frames(view) / 30);
        gvr_mt_update_pan(view);
    }
    valid = gvr_mt_source_drop_target(view,
                                      x, y, allocation.width,
                                      &track, &kind, &slot, &frame);
    view->source_drag_active = valid;
    view->source_drag_track = track;
    view->source_drag_kind = kind;
    view->source_drag_slot = slot;
    view->source_drag_frame = frame;
    gdk_drag_status(context, valid ? GDK_ACTION_COPY : 0, time);
    gvr_mt_queue_timeline_draw(view);
    return TRUE;
}

static void gvr_mt_drag_leave(GtkWidget *widget,
                              GdkDragContext *context,
                              guint time,
                              gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    (void)widget;
    (void)context;
    (void)time;
    view->source_drag_active = FALSE;
    view->source_drag_track = -1;
    view->source_drag_kind = 0;
    view->source_drag_slot = -1;
    gvr_mt_queue_timeline_draw(view);
}

static gboolean gvr_mt_parse_source_payload(GtkSelectionData *selection,
                                            int *sample_id,
                                            int *sample_type)
{
    const guchar *bytes = gtk_selection_data_get_data(selection);
    const gint length = gtk_selection_data_get_length(selection);
    char *payload;
    int version = 0;
    int id = 0;
    int type = -1;
    int consumed = 0;
    gboolean valid = FALSE;

    if(!bytes || length <= 0)
        return FALSE;

    payload = g_strndup((const char *)bytes, length);
    if(sscanf(payload, "%d %d %d %n", &version, &id, &type, &consumed) == 3 &&
       version == 1 && id > 0 && type >= 0) {
        const char *tail = payload + consumed;
        while(*tail && g_ascii_isspace(*tail))
            tail++;
        valid = *tail == '\0';
    }
    g_free(payload);

    if(valid) {
        if(sample_id)
            *sample_id = id;
        if(sample_type)
            *sample_type = type;
    }
    return valid;
}

static void gvr_mt_drag_data_received(GtkWidget *widget,
                                      GdkDragContext *context,
                                      gint x,
                                      gint y,
                                      GtkSelectionData *selection,
                                      guint info,
                                      guint time,
                                      gpointer user_data)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(user_data);
    GtkAllocation allocation;
    int track = -1;
    int kind = 0;
    int insertion_slot = -1;
    int frame = 0;
    int sample_id = 0;
    int sample_type = -1;
    gboolean success = FALSE;

    (void)info;
    gtk_widget_get_allocation(widget, &allocation);

    if(gvr_mt_source_drop_target(view,
                                 x, y, allocation.width,
                                 &track, &kind, &insertion_slot, &frame) &&
       gvr_mt_parse_source_payload(selection, &sample_id, &sample_type)) {
        gvr_mt_select_track(view, track, TRUE);
        view->pending_source_track = track;
        view->pending_source_id = sample_id;
        view->pending_source_type = sample_type;
        view->pending_source_kind = kind;
        if(kind == 2) {
            g_signal_emit(
                view,
                gvr_multi_track_edit_signals[SIGNAL_SEQUENCE_SOURCE_INSERT_REQUESTED],
                0,
                track,
                view->lanes[track].sequence_bank,
                insertion_slot,
                sample_id,
                sample_type);
        }
        else {
            g_signal_emit(view,
                          gvr_multi_track_edit_signals[SIGNAL_SOURCE_PLAY_REQUESTED],
                          0,
                          track,
                          sample_id,
                          sample_type);
        }
        success = TRUE;
    }

    view->source_drag_active = FALSE;
    view->source_drag_track = -1;
    view->source_drag_kind = 0;
    view->source_drag_slot = -1;
    gvr_mt_queue_timeline_draw(view);
    gtk_drag_finish(context, success, FALSE, time);
}

static void gvr_mt_build_lane_header(GvrMultiTrackEdit *view, int track)
{
    GvrMultiTrackLane *lane = &view->lanes[track];
    GtkWidget *info_box;
    GtkWidget *preview_frame;
    GtkWidget *actions;

    lane->clips = g_ptr_array_new_with_free_func(gvr_mt_clip_free);
    lane->events = g_ptr_array_new_with_free_func(gvr_mt_event_free);

    lane->row_event = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(lane->row_event), TRUE);
    gtk_widget_set_size_request(lane->row_event,
                                GVR_MT_HEADER_WIDTH,
                                GVR_MT_LANE_HEIGHT);
    gvr_mt_add_class(lane->row_event, "multi-track-header");
    g_object_set_data(G_OBJECT(lane->row_event),
                      "gvr-track",
                      GINT_TO_POINTER(track));
    g_signal_connect(lane->row_event,
                     "button-press-event",
                     G_CALLBACK(gvr_mt_header_button_press),
                     view);

    lane->row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(lane->row_box), 5);
    gtk_container_add(GTK_CONTAINER(lane->row_event), lane->row_box);

    preview_frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(preview_frame), GTK_SHADOW_NONE);
    gtk_widget_set_size_request(preview_frame,
                                GVR_MT_PREVIEW_MAX_WIDTH + 4,
                                GVR_MT_PREVIEW_MAX_HEIGHT + 4);
    gtk_widget_set_valign(preview_frame, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(preview_frame, FALSE);
    gvr_mt_add_class(preview_frame, "multi-track-preview-frame");

    lane->preview_image = gtk_image_new();
    gtk_widget_set_size_request(lane->preview_image,
                                GVR_MT_PREVIEW_MAX_WIDTH,
                                GVR_MT_PREVIEW_MAX_HEIGHT);
    gtk_widget_set_valign(lane->preview_image, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(lane->preview_image, FALSE);
    gtk_container_add(GTK_CONTAINER(preview_frame), lane->preview_image);
    gtk_box_pack_start(GTK_BOX(lane->row_box),
                       preview_frame,
                       FALSE,
                       FALSE,
                       0);

    info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(info_box, TRUE);
    gtk_widget_set_valign(info_box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(info_box, FALSE);
    lane->title_label = gtk_label_new(NULL);
    gtk_widget_set_halign(lane->title_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(lane->title_label), PANGO_ELLIPSIZE_END);
    lane->status_label = gtk_label_new(NULL);
    gtk_widget_set_halign(lane->status_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(lane->status_label), PANGO_ELLIPSIZE_END);
    gvr_mt_add_class(lane->status_label, "multi-track-status");
    gtk_box_pack_start(GTK_BOX(info_box), lane->title_label, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(info_box), lane->status_label, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(lane->row_box), info_box, TRUE, TRUE, 0);

    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    gtk_widget_set_valign(actions, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(actions, FALSE);
    gtk_widget_set_vexpand(actions, FALSE);
    gvr_mt_add_class(actions, "multi-track-lane-actions");

    lane->a_button = gtk_toggle_button_new_with_label("A");
    gtk_widget_set_no_show_all(lane->a_button, TRUE);
    gtk_widget_set_size_request(lane->a_button,
                                GVR_MT_LANE_BUTTON_SIZE,
                                GVR_MT_LANE_BUTTON_SIZE);
    gtk_widget_set_halign(lane->a_button, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(lane->a_button, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(lane->a_button, FALSE);
    gtk_widget_set_vexpand(lane->a_button, FALSE);
    gvr_mt_add_class(lane->a_button, "multi-track-role-a");
    g_object_set_data(G_OBJECT(lane->a_button),
                      "gvr-track",
                      GINT_TO_POINTER(track));
    g_signal_connect(lane->a_button,
                     "clicked",
                     G_CALLBACK(gvr_mt_a_clicked),
                     view);
    gtk_box_pack_start(GTK_BOX(actions), lane->a_button, FALSE, FALSE, 0);

    lane->b_button = gtk_toggle_button_new_with_label("B");
    gtk_widget_set_no_show_all(lane->b_button, TRUE);
    gtk_widget_set_size_request(lane->b_button,
                                GVR_MT_LANE_BUTTON_SIZE,
                                GVR_MT_LANE_BUTTON_SIZE);
    gtk_widget_set_halign(lane->b_button, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(lane->b_button, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(lane->b_button, FALSE);
    gtk_widget_set_vexpand(lane->b_button, FALSE);
    gvr_mt_add_class(lane->b_button, "multi-track-role-b");
    g_object_set_data(G_OBJECT(lane->b_button),
                      "gvr-track",
                      GINT_TO_POINTER(track));
    g_signal_connect(lane->b_button,
                     "clicked",
                     G_CALLBACK(gvr_mt_b_clicked),
                     view);
    gtk_box_pack_start(GTK_BOX(actions), lane->b_button, FALSE, FALSE, 0);

    lane->preview_toggle = gtk_toggle_button_new();
    gtk_widget_set_size_request(lane->preview_toggle,
                                GVR_MT_LANE_BUTTON_SIZE,
                                GVR_MT_LANE_BUTTON_SIZE);
    gtk_widget_set_halign(lane->preview_toggle, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(lane->preview_toggle, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(lane->preview_toggle, FALSE);
    gtk_widget_set_vexpand(lane->preview_toggle, FALSE);
    gvr_mt_add_class(lane->preview_toggle, "multi-track-preview-toggle");
    gvr_mt_set_preview_icon(lane->preview_toggle, FALSE);
    g_object_set_data(G_OBJECT(lane->preview_toggle),
                      "gvr-track",
                      GINT_TO_POINTER(track));
    g_signal_connect(lane->preview_toggle,
                     "toggled",
                     G_CALLBACK(gvr_mt_preview_toggled),
                     view);
    gtk_box_pack_start(GTK_BOX(actions), lane->preview_toggle, FALSE, FALSE, 0);

    lane->switch_button = gtk_button_new();
    gtk_widget_set_size_request(lane->switch_button,
                                GVR_MT_LANE_BUTTON_SIZE,
                                GVR_MT_LANE_BUTTON_SIZE);
    gtk_widget_set_halign(lane->switch_button, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(lane->switch_button, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(lane->switch_button, FALSE);
    gtk_widget_set_vexpand(lane->switch_button, FALSE);
    gvr_mt_add_class(lane->switch_button, "multi-track-switch");
    gvr_mt_set_switch_icon(lane->switch_button, FALSE);
    g_object_set_data(G_OBJECT(lane->switch_button),
                      "gvr-track",
                      GINT_TO_POINTER(track));
    g_signal_connect(lane->switch_button,
                     "clicked",
                     G_CALLBACK(gvr_mt_switch_clicked),
                     view);
    gtk_box_pack_start(GTK_BOX(actions), lane->switch_button, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(lane->row_box), actions, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(view->header_content),
                       lane->row_event,
                       FALSE,
                       TRUE,
                       0);
}

static void gvr_multi_track_edit_dispose(GObject *object)
{
    GvrMultiTrackEdit *view = GVR_MULTI_TRACK_EDIT(object);

    for(int i = 0; i < GVR_MULTI_TRACK_EDIT_MAX_TRACKS; i++) {
        GvrMultiTrackLane *lane = &view->lanes[i];
        g_clear_pointer(&lane->hostname, g_free);
        if(lane->preview) {
            g_object_unref(lane->preview);
            lane->preview = NULL;
        }
        if(lane->clips) {
            g_ptr_array_free(lane->clips, TRUE);
            lane->clips = NULL;
        }
        if(lane->events) {
            g_ptr_array_free(lane->events, TRUE);
            lane->events = NULL;
        }
    }

    if(view->master_clips) {
        g_array_free(view->master_clips, TRUE);
        view->master_clips = NULL;
    }

    if(view->timeline_area && gtk_widget_has_grab(view->timeline_area))
        gtk_grab_remove(view->timeline_area);
    g_clear_pointer(&view->transport_seek_reason, g_free);
    if(view->draw_layout || view->draw_font_count > 0)
        gvr_mt_draw_context_end(view);

    G_OBJECT_CLASS(gvr_multi_track_edit_parent_class)->dispose(object);
}

static void gvr_multi_track_edit_class_init(GvrMultiTrackEditClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = gvr_multi_track_edit_dispose;

    gvr_multi_track_edit_signals[SIGNAL_TRACK_SELECTED] =
        g_signal_new("track-selected",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    gvr_multi_track_edit_signals[SIGNAL_SWITCH_REQUESTED] =
        g_signal_new("switch-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    gvr_multi_track_edit_signals[SIGNAL_PREVIEW_TOGGLED] =
        g_signal_new("preview-toggled",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_generic,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_INT,
                     G_TYPE_BOOLEAN);

    gvr_multi_track_edit_signals[SIGNAL_TRANSITION_SOURCE_SELECTED] =
        g_signal_new("transition-source-selected",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_generic,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_INT,
                     G_TYPE_INT);

    gvr_multi_track_edit_signals[SIGNAL_SEEK_REQUESTED] =
        g_signal_new("seek-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    gvr_multi_track_edit_signals[SIGNAL_SOURCE_PLAY_REQUESTED] =
        g_signal_new("source-play-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL,
                     g_cclosure_marshal_generic,
                     G_TYPE_NONE, 3,
                     G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

    gvr_multi_track_edit_signals[SIGNAL_SEQUENCE_SOURCE_INSERT_REQUESTED] =
        g_signal_new("sequence-source-insert-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL,
                     g_cclosure_marshal_generic,
                     G_TYPE_NONE, 5,
                     G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

    gvr_multi_track_edit_signals[SIGNAL_REVEAL_SEQUENCE_SLOT_REQUESTED] =
        g_signal_new("reveal-sequence-slot-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL,
                     g_cclosure_marshal_generic,
                     G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    gvr_multi_track_edit_signals[SIGNAL_REVEAL_SOURCE_REQUESTED] =
        g_signal_new("reveal-source-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL,
                     g_cclosure_marshal_generic,
                     G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    gvr_multi_track_edit_signals[SIGNAL_RESYNC_REQUESTED] =
        g_signal_new("resync-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE, 1, G_TYPE_INT);

    gvr_multi_track_edit_signals[SIGNAL_TRANSITION_REQUESTED] =
        g_signal_new("transition-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_generic,
                     G_TYPE_NONE,
                     4,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_INT);

    gvr_multi_track_edit_signals[SIGNAL_DRIFT_LOCK_TOGGLED] =
        g_signal_new("drift-lock-toggled",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__BOOLEAN,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_BOOLEAN);
}

static void gvr_multi_track_edit_init(GvrMultiTrackEdit *view)
{
    GtkWidget *connection_frame;
    GtkWidget *connection_box;
    GtkWidget *summary;
    GtkWidget *controls;
    GtkWidget *transition_root;
    GtkWidget *transition_bar;
    GtkWidget *transition_controls;
    GtkWidget *transition_title;
    GtkWidget *duration_label;
    GtkWidget *shape_frame;
    GtkWidget *shape_label;
    GtkWidget *shape_box;
    GtkWidget *body;
    GtkWidget *ruler_spacer;

    gtk_orientable_set_orientation(GTK_ORIENTABLE(view),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(view), 4);
    gvr_mt_add_class(GTK_WIDGET(view), "multi-track-edit");

    view->selected_track = -1;
    view->current_control_track = -1;
    view->project_master_track = -1;
    view->bus_a_track = -1;
    view->bus_b_track = -1;
    view->active_bus = 0;
    view->transition_progress_value = 0;
    view->transition_method = VJ_MULTITRACK_TRANSITION_DISSOLVE;
    view->transition_shape = 0;
    view->transition_shape_count = 0;
    view->drift_lock_enabled = TRUE;
    view->bank = 0;
    view->fps = 25.0;
    view->timeline_zoom = 1.0;
    view->focused_track = -1;
    view->source_drag_track = -1;
    view->pending_source_track = -1;
    view->pending_source_id = -1;
    view->pending_source_type = -1;
    view->source_drag_slot = -1;
    view->hover_track = -1;
    view->hover_frame = -1;
    view->transport_seekable = FALSE;
    view->transport_seek_reason = g_strdup("waiting for playback status");
    view->seek_dragging = FALSE;
    view->geometry_dirty = TRUE;
    view->cached_effective_total = 1;
    view->cached_visible_frames = 1;
    view->draw_layout = NULL;
    view->draw_font_count = 0;
    view->master_clips = g_array_new(FALSE,
                                     FALSE,
                                     sizeof(GvrMultiTrackMasterClip));

    connection_frame = gtk_frame_new("Reloaded connection");
    connection_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(connection_box), 4);
    view->connection_role_label = gtk_label_new(NULL);
    view->connection_path_label = gtk_label_new(NULL);
    view->connection_state_label = gtk_label_new(NULL);
    gtk_widget_set_halign(view->connection_role_label, GTK_ALIGN_START);
    gtk_widget_set_halign(view->connection_path_label, GTK_ALIGN_START);
    gtk_widget_set_halign(view->connection_state_label, GTK_ALIGN_END);
    gtk_widget_set_hexpand(view->connection_path_label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(view->connection_path_label),
                            PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_ellipsize(GTK_LABEL(view->connection_state_label),
                            PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(view->connection_state_label), 46);
    gvr_mt_add_class(connection_frame, "multi-track-connection-frame");
    gvr_mt_add_class(connection_box, "multi-track-connection");
    gvr_mt_add_class(view->connection_role_label,
                     "multi-track-connection-role");
    gvr_mt_add_class(view->connection_path_label,
                     "multi-track-connection-path");
    gvr_mt_add_class(view->connection_state_label,
                     "multi-track-connection-state");
    gtk_box_pack_start(GTK_BOX(connection_box),
                       view->connection_role_label,
                       FALSE,
                       FALSE,
                       0);
    gtk_box_pack_start(GTK_BOX(connection_box),
                       view->connection_path_label,
                       TRUE,
                       TRUE,
                       0);
    gtk_box_pack_end(GTK_BOX(connection_box),
                     view->connection_state_label,
                     FALSE,
                     FALSE,
                     0);
    gtk_container_add(GTK_CONTAINER(connection_frame), connection_box);
    gtk_box_pack_start(GTK_BOX(view), connection_frame, FALSE, TRUE, 0);

    summary = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    view->project_label = gtk_label_new(NULL);
    gtk_widget_set_no_show_all(view->project_label, TRUE);
    view->position_label = gtk_label_new("Transport idle");
    view->selection_label = gtk_label_new("Selected video track —");
    gtk_widget_set_halign(view->project_label, GTK_ALIGN_START);
    gtk_widget_set_halign(view->position_label, GTK_ALIGN_START);
    gtk_widget_set_halign(view->selection_label, GTK_ALIGN_END);
    gtk_widget_set_hexpand(view->project_label, TRUE);
    gtk_widget_set_hexpand(view->selection_label, TRUE);
    gvr_mt_add_class(summary, "multi-track-summary");
    gtk_box_pack_start(GTK_BOX(summary), view->project_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(summary), view->position_label, FALSE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(summary), view->selection_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(view), summary, FALSE, TRUE, 0);

    gvr_multi_track_edit_set_connection_topology(GTK_WIDGET(view),
                                                  FALSE,
                                                  NULL,
                                                  0,
                                                  FALSE,
                                                  FALSE,
                                                  FALSE,
                                                  NULL,
                                                  0,
                                                  FALSE);

    transition_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gvr_mt_add_class(transition_root, "multi-track-transition-root");
    transition_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    transition_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gvr_mt_add_class(transition_bar, "multi-track-transition-bar");
    gvr_mt_add_class(transition_controls, "multi-track-transition-controls");

    transition_title = gtk_label_new("A / B SWITCHER");
    gvr_mt_add_class(transition_title, "multi-track-transition-title");
    gtk_box_pack_start(GTK_BOX(transition_bar), transition_title, FALSE, FALSE, 2);

    view->bus_a_label = gtk_label_new("A  —");
    view->bus_b_label = gtk_label_new("B  —");
    gvr_mt_add_class(view->bus_a_label, "multi-track-transition-a");
    gvr_mt_add_class(view->bus_b_label, "multi-track-transition-b");
    gtk_widget_set_tooltip_text(view->bus_a_label,
                                "Source assigned to Bus A. The on-air bus is marked explicitly.");
    gtk_widget_set_tooltip_text(view->bus_b_label,
                                "Source assigned to Bus B. The on-air bus is marked explicitly.");
    gtk_box_pack_start(GTK_BOX(transition_bar), view->bus_a_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transition_bar), gtk_label_new("⇄"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transition_bar), view->bus_b_label, FALSE, FALSE, 0);

    view->transition_method_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view->transition_method_combo),
                                   "Dissolve");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view->transition_method_combo),
                                   "Shape Wipe");
    gtk_combo_box_set_active(GTK_COMBO_BOX(view->transition_method_combo),
                             VJ_MULTITRACK_TRANSITION_DISSOLVE);
    gvr_mt_add_class(view->transition_method_combo, "multi-track-transition-method");
    gtk_widget_set_tooltip_text(view->transition_method_combo,
                                "Select the A/B transition operator.");
    g_signal_connect(view->transition_method_combo,
                     "changed",
                     G_CALLBACK(gvr_mt_transition_method_changed),
                     view);
    gtk_box_pack_start(GTK_BOX(transition_controls),
                       gtk_label_new("Operator"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transition_controls),
                       view->transition_method_combo, FALSE, FALSE, 0);

    view->transition_shape_selector = gvr_shape_selector_new();
    gvr_mt_add_class(view->transition_shape_selector,
                     "multi-track-transition-shape");
    gtk_widget_set_size_request(view->transition_shape_selector, -1, 104);
    gtk_widget_set_hexpand(view->transition_shape_selector, TRUE);
    g_signal_connect(view->transition_shape_selector,
                     "shape-changed",
                     G_CALLBACK(gvr_mt_transition_shape_changed),
                     view);

    duration_label = gtk_label_new("Frames");
    view->transition_duration_spin = gtk_spin_button_new_with_range(1, 1500, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(view->transition_duration_spin), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(view->transition_duration_spin), 25);
    gtk_entry_set_width_chars(GTK_ENTRY(view->transition_duration_spin), 4);
    view->transition_duration_time = gtk_label_new("1.00 s");
    gvr_mt_add_class(view->transition_duration_time, "multi-track-transition-time");
    gtk_widget_set_tooltip_text(view->transition_duration_spin,
                                "Transition duration measured in rendered output frames.");
    g_signal_connect(view->transition_duration_spin,
                     "value-changed",
                     G_CALLBACK(gvr_mt_transition_duration_changed),
                     view);
    gtk_box_pack_start(GTK_BOX(transition_controls), duration_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transition_controls), view->transition_duration_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transition_controls), view->transition_duration_time, FALSE, FALSE, 0);

    view->transition_cut_button = gtk_button_new_with_label("CUT");
    view->transition_take_button = gtk_button_new_with_label("TAKE");
    gvr_mt_add_class(view->transition_cut_button, "multi-track-transition-cut");
    gvr_mt_add_class(view->transition_take_button, "multi-track-transition-take");
    gtk_widget_set_tooltip_text(view->transition_cut_button,
                                "Cut immediately from the on-air bus to the other assigned bus.");
    gtk_widget_set_tooltip_text(view->transition_take_button,
                                "Run the selected operator from the on-air bus to the other assigned bus.");
    g_signal_connect(view->transition_cut_button,
                     "clicked",
                     G_CALLBACK(gvr_mt_transition_cut_clicked),
                     view);
    g_signal_connect(view->transition_take_button,
                     "clicked",
                     G_CALLBACK(gvr_mt_transition_take_clicked),
                     view);
    gtk_box_pack_start(GTK_BOX(transition_controls), view->transition_cut_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transition_controls), view->transition_take_button, FALSE, FALSE, 0);

    view->transition_progress = gtk_progress_bar_new();
    gtk_widget_set_size_request(view->transition_progress, 150, 24);
    gtk_widget_set_hexpand(view->transition_progress, TRUE);
    gvr_mt_add_class(view->transition_progress, "multi-track-transition-progress");
    gtk_box_pack_start(GTK_BOX(transition_bar), view->transition_progress, TRUE, TRUE, 2);

    view->drift_toggle = gtk_check_button_new_with_label("Drift Lock");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(view->drift_toggle), TRUE);
    gtk_widget_set_tooltip_text(view->drift_toggle,
                                "Preserve each follower's relative phase against the project-master clock. Small accumulated timing errors are nudged without forcing matching start positions.");
    g_signal_connect(view->drift_toggle,
                     "toggled",
                     G_CALLBACK(gvr_mt_drift_toggled),
                     view);
    gtk_box_pack_end(GTK_BOX(transition_controls), view->drift_toggle, FALSE, FALSE, 0);

    view->transition_status_label = gtk_label_new("ON AIR · —");
    gtk_widget_set_halign(view->transition_status_label, GTK_ALIGN_END);
    gvr_mt_add_class(view->transition_status_label, "multi-track-transition-status");
    gtk_box_pack_end(GTK_BOX(transition_bar), view->transition_status_label, FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(transition_root), transition_bar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(transition_root), transition_controls, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(view), transition_root, FALSE, TRUE, 0);

    controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gvr_mt_add_class(controls, "multi-track-controls");
    view->zoom_out_button = gtk_button_new_with_label("−");
    view->zoom_in_button = gtk_button_new_with_label("+");
    view->zoom_fit_button = gtk_button_new_with_label("Fit");
    view->zoom_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                GVR_MT_ZOOM_MIN,
                                                GVR_MT_ZOOM_MAX,
                                                0.1);
    gtk_scale_set_draw_value(GTK_SCALE(view->zoom_scale), FALSE);
    gtk_range_set_value(GTK_RANGE(view->zoom_scale), 1.0);
    gtk_widget_set_size_request(view->zoom_scale, 180, -1);
    view->zoom_label = gtk_label_new("1.0x");
    gtk_widget_set_tooltip_text(view->zoom_out_button, "Zoom out on the project timeline.");
    gtk_widget_set_tooltip_text(view->zoom_in_button, "Zoom in on the project timeline.");
    gtk_widget_set_tooltip_text(view->zoom_fit_button, "Fit the complete sequence bank.");
    gtk_widget_set_tooltip_text(view->zoom_scale, "Timeline zoom. Ctrl+mouse wheel also zooms.");
    g_signal_connect(view->zoom_out_button, "clicked", G_CALLBACK(gvr_mt_zoom_out), view);
    g_signal_connect(view->zoom_in_button, "clicked", G_CALLBACK(gvr_mt_zoom_in), view);
    g_signal_connect(view->zoom_fit_button, "clicked", G_CALLBACK(gvr_mt_zoom_fit), view);
    g_signal_connect(view->zoom_scale, "value-changed", G_CALLBACK(gvr_mt_zoom_changed), view);
    gtk_box_pack_start(GTK_BOX(controls), gtk_label_new("Timeline"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), view->zoom_out_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), view->zoom_in_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), view->zoom_fit_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), view->zoom_scale, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), view->zoom_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(view), controls, FALSE, TRUE, 0);

    view->vertical_adjustment = GTK_ADJUSTMENT(gtk_adjustment_new(0.0,
                                                                  0.0,
                                                                  1.0,
                                                                  10.0,
                                                                  60.0,
                                                                  60.0));
    body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(body, TRUE);
    gtk_widget_set_vexpand(body, TRUE);

    view->header_scroll = gtk_scrolled_window_new(NULL,
                                                   view->vertical_adjustment);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view->header_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_NEVER);
    gtk_widget_set_size_request(view->header_scroll,
                                GVR_MT_HEADER_WIDTH,
                                260);
    view->header_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    ruler_spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(ruler_spacer,
                                GVR_MT_HEADER_WIDTH,
                                GVR_MT_RULER_HEIGHT);
    gvr_mt_add_class(ruler_spacer, "multi-track-ruler-spacer");
    GtkWidget *instance_label =
        gtk_label_new("VeeJay instances · Switch = full control");
    gtk_widget_set_halign(instance_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(instance_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_tooltip_text(
        instance_label,
        "Switch changes which VeeJay instance is operated by the main Reloaded controls. "
        "It does not change the project-master timeline.");
    gtk_box_pack_start(GTK_BOX(ruler_spacer), instance_label, TRUE, TRUE, 4);
    gtk_box_pack_start(GTK_BOX(view->header_content), ruler_spacer, FALSE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(view->header_scroll), view->header_content);
    gtk_box_pack_start(GTK_BOX(body), view->header_scroll, FALSE, TRUE, 0);

    view->timeline_scroll = gtk_scrolled_window_new(NULL,
                                                     view->vertical_adjustment);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view->timeline_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(view->timeline_scroll, TRUE);
    gtk_widget_set_vexpand(view->timeline_scroll, TRUE);
    view->timeline_area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(view->timeline_area, TRUE);
    gtk_widget_set_has_tooltip(view->timeline_area, TRUE);
    gtk_widget_add_events(view->timeline_area,
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_BUTTON1_MOTION_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_LEAVE_NOTIFY_MASK |
                          GDK_SCROLL_MASK |
                          GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(view->timeline_area,
                     "draw",
                     G_CALLBACK(gvr_mt_timeline_draw),
                     view);
    g_signal_connect(view->timeline_area,
                     "button-press-event",
                     G_CALLBACK(gvr_mt_timeline_button_press),
                     view);
    g_signal_connect(view->timeline_area,
                     "button-release-event",
                     G_CALLBACK(gvr_mt_timeline_button_release),
                     view);
    g_signal_connect(view->timeline_area,
                     "motion-notify-event",
                     G_CALLBACK(gvr_mt_timeline_motion),
                     view);
    g_signal_connect(view->timeline_area,
                     "leave-notify-event",
                     G_CALLBACK(gvr_mt_timeline_leave),
                     view);
    g_signal_connect(view->timeline_area,
                     "scroll-event",
                     G_CALLBACK(gvr_mt_timeline_scroll),
                     view);
    g_signal_connect(view->timeline_area,
                     "key-press-event",
                     G_CALLBACK(gvr_mt_timeline_key_press),
                     view);
    g_signal_connect(view->timeline_area,
                     "query-tooltip",
                     G_CALLBACK(gvr_mt_query_tooltip),
                     view);
    gtk_drag_dest_set(view->timeline_area,
                      GTK_DEST_DEFAULT_ALL,
                      gvr_multi_track_edit_drop_targets,
                      G_N_ELEMENTS(gvr_multi_track_edit_drop_targets),
                      GDK_ACTION_COPY);
    g_signal_connect(view->timeline_area,
                     "drag-motion",
                     G_CALLBACK(gvr_mt_drag_motion),
                     view);
    g_signal_connect(view->timeline_area,
                     "drag-leave",
                     G_CALLBACK(gvr_mt_drag_leave),
                     view);
    g_signal_connect(view->timeline_area,
                     "drag-data-received",
                     G_CALLBACK(gvr_mt_drag_data_received),
                     view);
    gtk_container_add(GTK_CONTAINER(view->timeline_scroll), view->timeline_area);
    gtk_box_pack_start(GTK_BOX(body), view->timeline_scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(view), body, TRUE, TRUE, 0);

    view->navigator = gtk_drawing_area_new();
    gtk_widget_set_size_request(view->navigator, -1, GVR_MT_NAVIGATOR_HEIGHT);
    gtk_widget_add_events(view->navigator, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(view->navigator,
                     "draw",
                     G_CALLBACK(gvr_mt_navigator_draw),
                     view);
    g_signal_connect(view->navigator,
                     "button-press-event",
                     G_CALLBACK(gvr_mt_navigator_button),
                     view);
    gtk_widget_set_tooltip_text(view->navigator,
                                "Sequence-bank overview. Click to move the visible timeline window.");
    gtk_box_pack_start(GTK_BOX(view), view->navigator, FALSE, TRUE, 0);

    view->pan_adjustment = GTK_ADJUSTMENT(gtk_adjustment_new(0.0,
                                                             0.0,
                                                             1.0,
                                                             1.0,
                                                             1.0,
                                                             1.0));
    g_signal_connect(view->pan_adjustment,
                     "value-changed",
                     G_CALLBACK(gvr_mt_pan_changed),
                     view);
    view->pan_scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL,
                                            view->pan_adjustment);
    gtk_widget_set_size_request(view->pan_scrollbar, -1, 11);
    gtk_widget_set_tooltip_text(view->pan_scrollbar,
                                "Pan the zoomed project timeline.");
    gtk_box_pack_start(GTK_BOX(view), view->pan_scrollbar, FALSE, TRUE, 0);

    view->transition_shape_revealer =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_no_show_all(view->transition_shape_revealer, TRUE);
    gtk_widget_set_size_request(view->transition_shape_revealer, -1, 124);
    gtk_widget_hide(view->transition_shape_revealer);
    gvr_mt_add_class(view->transition_shape_revealer,
                     "multi-track-transition-shape-panel");

    shape_frame = gtk_frame_new(NULL);
    shape_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(shape_label),
                         "<b>Shape Wipe</b>");
    gtk_widget_set_halign(shape_label, GTK_ALIGN_START);
    gtk_frame_set_label_widget(GTK_FRAME(shape_frame), shape_label);
    shape_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_container_set_border_width(GTK_CONTAINER(shape_box), 2);
    gtk_box_pack_start(GTK_BOX(shape_box),
                       view->transition_shape_selector,
                       TRUE,
                       TRUE,
                       0);
    gtk_container_add(GTK_CONTAINER(shape_frame), shape_box);
    gtk_container_add(GTK_CONTAINER(view->transition_shape_revealer),
                      shape_frame);
    gtk_widget_show_all(shape_frame);
    gtk_widget_hide(view->transition_shape_revealer);
    gtk_box_pack_start(GTK_BOX(view),
                       view->transition_shape_revealer,
                       FALSE,
                       TRUE,
                       0);
}

GtkWidget *gvr_multi_track_edit_new(int max_tracks)
{
    GvrMultiTrackEdit *view = g_object_new(GVR_TYPE_MULTI_TRACK_EDIT, NULL);

    view->max_tracks = gvr_mt_clampi(max_tracks,
                                     1,
                                     GVR_MULTI_TRACK_EDIT_MAX_TRACKS);
    for(int i = 0; i < view->max_tracks; i++)
        gvr_mt_build_lane_header(view, i);

    gtk_widget_set_size_request(view->timeline_area,
                                640,
                                GVR_MT_RULER_HEIGHT +
                                gvr_mt_visible_track_count(view) * GVR_MT_LANE_HEIGHT);
    gvr_mt_refresh_headers(view);
    gvr_mt_update_summary(view);
    gvr_mt_update_pan(view);

    /*
     * Show every lane child once before compacting disconnected rows.
     * This keeps preview GtkImages and lane controls visible internally;
     * later connecting a lane only has to show its outer row_event.
     */
    gtk_widget_show_all(GTK_WIDGET(view));
    gvr_mt_update_focus_layout(view);
    return GTK_WIDGET(view);
}


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
                                      gboolean vims_forwarding)
{
    GvrMultiTrackEdit *view;
    char path[512];
    char state[192];
    char local_endpoint[320];
    char upstream_endpoint[320];
    const char *local;
    const char *upstream;
    const char *role;
    const char *tip;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;

    view = GVR_MULTI_TRACK_EDIT(widget);
    local = local_host && local_host[0] ? local_host : "localhost";
    upstream = upstream_host && upstream_host[0] ? upstream_host : "localhost";
    if(local_port > 0)
        g_snprintf(local_endpoint, sizeof(local_endpoint), "%s:%d", local, local_port);
    else
        g_snprintf(local_endpoint, sizeof(local_endpoint), "%s", local);
    if(upstream_port > 0)
        g_snprintf(upstream_endpoint, sizeof(upstream_endpoint), "%s:%d", upstream, upstream_port);
    else
        g_snprintf(upstream_endpoint, sizeof(upstream_endpoint), "%s", upstream);

    if(!connected) {
        role = "<b>DISCONNECTED</b>";
        g_strlcpy(path, "Reloaded is not connected to a VeeJay instance", sizeof(path));
        g_strlcpy(state, "NO CONTROL CONNECTION", sizeof(state));
        tip = "Reloaded is not connected to a VeeJay instance.";
    }
    else if(backend_is_master) {
        role = "<b>MASTER OUTPUT</b>";
        g_snprintf(path, sizeof(path), "Reloaded → %s", local_endpoint);
        g_strlcpy(state,
                  "PROJECTION OWNER · SAMPLELIST SYNC DISABLED",
                  sizeof(state));
        tip = "Reloaded is connected directly to the projection/output master instance.";
    }
    else if(upstream_info_known && upstream_configured) {
        role = "<b>PREVIEW / EDITOR</b>";
        g_snprintf(path,
                   sizeof(path),
                   "Reloaded → %s → MASTER %s",
                   local_endpoint,
                   upstream_endpoint);
        g_snprintf(state,
                   sizeof(state),
                   "VIMS FORWARD %s · UPSTREAM MASTER CONFIGURED",
                   vims_forwarding ? "ON" : "OFF");
        tip = vims_forwarding ?
            "Reloaded controls a preview/editor instance with VIMS forwarding enabled." :
            "Reloaded controls a preview/editor instance with VIMS forwarding disabled.";
    }
    else if(upstream_info_known) {
        role = "<b>STANDALONE</b>";
        g_snprintf(path,
                   sizeof(path),
                   "Reloaded → %s · no upstream master",
                   local_endpoint);
        g_snprintf(state,
                   sizeof(state),
                   "VIMS FORWARD %s · NO UPSTREAM MASTER",
                   vims_forwarding ? "ON" : "OFF");
        tip = vims_forwarding ?
            "This standalone VeeJay instance has VIMS forwarding enabled but no upstream target." :
            "This standalone VeeJay instance has no upstream master.";
    }
    else {
        role = "<b>STANDARD INSTANCE</b>";
        g_snprintf(path,
                   sizeof(path),
                   "Reloaded → %s",
                   local_endpoint);
        g_snprintf(state,
                   sizeof(state),
                   "VIMS FORWARD %s · UPSTREAM ROLE NOT REPORTED",
                   vims_forwarding ? "ON" : "OFF");
        tip = "This backend does not report preview-to-master topology.";
    }

    gtk_label_set_markup(GTK_LABEL(view->connection_role_label), role);
    gtk_label_set_text(GTK_LABEL(view->connection_path_label), path);
    gtk_label_set_text(GTK_LABEL(view->connection_state_label), state);
    gtk_widget_set_tooltip_text(view->connection_role_label, tip);
    gtk_widget_set_tooltip_text(view->connection_path_label, tip);
    gtk_widget_set_tooltip_text(view->connection_state_label, tip);
}

void gvr_multi_track_edit_set_shape_catalog(GtkWidget *widget,
                                             const char *const *names,
                                             guint count)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;

    view = GVR_MULTI_TRACK_EDIT(widget);
    view->transition_shape_count = count;
    gvr_shape_selector_set_catalog(view->transition_shape_selector,
                                   names,
                                   count,
                                   TRUE);
    if(count == 0 &&
       gtk_combo_box_get_active(GTK_COMBO_BOX(view->transition_method_combo)) ==
           VJ_MULTITRACK_TRANSITION_SHAPE_WIPE)
        gtk_combo_box_set_active(GTK_COMBO_BOX(view->transition_method_combo),
                                 VJ_MULTITRACK_TRANSITION_DISSOLVE);
    view->transition_shape = gvr_shape_selector_get_active(
        view->transition_shape_selector);
    gvr_mt_update_transition_ui(view);
}

void gvr_multi_track_edit_set_project(GtkWidget *widget,
                                      int bank,
                                      unsigned int revision,
                                      int total_frames,
                                      double fps)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    view->bank = MAX(0, bank);
    view->revision = revision;
    view->total_frames = MAX(0, total_frames);
    gvr_mt_invalidate_geometry(view);
    if(fps > 0.0)
        view->fps = fps;
    view->playhead = gvr_mt_clampi(view->playhead,
                                  0,
                                  MAX(0, gvr_mt_effective_total(view) - 1));
    gvr_mt_update_summary(view);
    gvr_mt_update_pan(view);
    gvr_mt_update_transition_ui(view);
    gvr_mt_queue_draw(view);
}

void gvr_multi_track_edit_clear_pending_source(GtkWidget *widget)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;

    view = GVR_MULTI_TRACK_EDIT(widget);
    view->pending_source_track = -1;
    view->pending_source_id = -1;
    view->pending_source_type = -1;
    view->pending_source_kind = 0;
    gvr_mt_queue_timeline_draw(view);
}

void gvr_multi_track_edit_set_master_clips(
                                      GtkWidget *widget,
                                      const GvrMultiTrackMasterClip *clips,
                                      guint count)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    g_array_set_size(view->master_clips, 0);
    if(clips && count > 0)
        g_array_append_vals(view->master_clips, clips, count);
    if(view->pending_source_kind == 2) {
        view->pending_source_track = -1;
        view->pending_source_id = -1;
        view->pending_source_type = -1;
        view->pending_source_kind = 0;
    }
    gvr_mt_invalidate_geometry(view);
    gvr_mt_update_pan(view);
    gvr_mt_queue_draw(view);
}

void gvr_multi_track_edit_set_track(GtkWidget *widget,
                                    int track,
                                    gboolean connected,
                                    const char *hostname,
                                    int port)
{
    GvrMultiTrackEdit *view;
    GvrMultiTrackLane *lane;
    gboolean connection_changed;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    if(track < 0 || track >= view->max_tracks)
        return;
    lane = &view->lanes[track];
    connection_changed = lane->connected != (connected ? TRUE : FALSE);
    lane->connected = connected ? TRUE : FALSE;
    lane->port = connected ? port : 0;
    g_free(lane->hostname);
    lane->hostname = connected && hostname ? g_strdup(hostname) : NULL;
    if(!connected) {
        lane->source_id = 0;
        lane->frame = 0;
        lane->total_frames = 0;
        lane->speed = 0;
    }
    else if(view->bus_a_track < 0) {
        view->bus_a_track = track;
        view->active_bus = 0;
    }
    else if(view->bus_b_track < 0 && track != view->bus_a_track) {
        view->bus_b_track = track;
    }
    gvr_mt_invalidate_geometry(view);
    gvr_mt_refresh_headers(view);
    if(connection_changed)
        gvr_mt_update_focus_layout(view);
    gvr_mt_queue_draw(view);
}

void gvr_multi_track_edit_clear_track(GtkWidget *widget, int track)
{
    GvrMultiTrackEdit *view;
    GvrMultiTrackLane *lane;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    if(track < 0 || track >= view->max_tracks)
        return;
    lane = &view->lanes[track];
    lane->connected = FALSE;
    lane->current_control = FALSE;
    lane->project_master = FALSE;
    lane->preview_enabled = FALSE;
    g_clear_pointer(&lane->hostname, g_free);
    lane->port = 0;
    lane->source_id = 0;
    lane->frame = 0;
    lane->total_frames = 0;
    lane->speed = 0;
    lane->stream_buffer_supported = FALSE;
    lane->stream_buffer_enabled = FALSE;
    lane->stream_buffer_capacity = 0;
    lane->stream_buffer_filled = 0;
    lane->stream_buffer_position = 0;
    lane->drift_frames = 0;
    lane->drift_millis = 0;
    lane->drift_correcting = FALSE;
    if(view->focused_track == track)
        view->focused_track = -1;
    if(view->pending_source_track == track) {
        view->pending_source_track = -1;
        view->pending_source_id = -1;
        view->pending_source_type = -1;
        view->pending_source_kind = 0;
    }
    if(track == view->project_master_track) {
        view->transport_seekable = FALSE;
        g_free(view->transport_seek_reason);
        view->transport_seek_reason = g_strdup("project master is disconnected");
        view->seek_dragging = FALSE;
    }
    if(lane->preview) {
        g_object_unref(lane->preview);
        lane->preview = NULL;
    }
    gtk_image_clear(GTK_IMAGE(lane->preview_image));
    g_ptr_array_set_size(lane->clips, 0);
    g_ptr_array_set_size(lane->events, 0);
    if(view->bus_a_track == track)
        view->bus_a_track = -1;
    if(view->bus_b_track == track)
        view->bus_b_track = -1;

    if(view->active_bus == 0 && view->bus_a_track < 0) {
        for(int i = 0; i < view->max_tracks; i++) {
            if(view->lanes[i].connected && i != view->bus_b_track) {
                view->bus_a_track = i;
                break;
            }
        }
    }
    else if(view->active_bus == 1 && view->bus_b_track < 0) {
        for(int i = 0; i < view->max_tracks; i++) {
            if(view->lanes[i].connected && i != view->bus_a_track) {
                view->bus_b_track = i;
                break;
            }
        }
    }

    if(view->bus_a_track < 0 && view->bus_b_track >= 0) {
        view->bus_a_track = view->bus_b_track;
        view->bus_b_track = -1;
        view->active_bus = 0;
    }
    gvr_mt_invalidate_geometry(view);
    gvr_mt_refresh_headers(view);
    gvr_mt_update_focus_layout(view);
    gvr_mt_queue_draw(view);
}

void gvr_multi_track_edit_set_seekability(GtkWidget *widget,
                                           gboolean seekable,
                                           const char *reason)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;

    view = GVR_MULTI_TRACK_EDIT(widget);
    view->transport_seekable = seekable ? TRUE : FALSE;
    g_free(view->transport_seek_reason);
    view->transport_seek_reason =
        !view->transport_seekable && reason && reason[0] ?
            g_strdup(reason) : NULL;

    if(!view->transport_seekable && view->seek_dragging) {
        view->seek_dragging = FALSE;
        if(view->timeline_area && gtk_widget_has_grab(view->timeline_area))
            gtk_grab_remove(view->timeline_area);
    }

    gtk_widget_set_tooltip_text(
        view->timeline_area,
        view->transport_seekable ?
            "Click or drag the playhead to seek the active transport." :
            (view->transport_seek_reason ?
                view->transport_seek_reason :
                "The active transport is not seekable."));
    gvr_mt_update_summary(view);
    gvr_mt_queue_timeline_draw(view);
}

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
                                           int stream_buffer_position)
{
    GvrMultiTrackEdit *view;
    GvrMultiTrackLane *lane;
    gboolean geometry_changed;
    const int normalized_total = MAX(0, total_frames);
    const int normalized_capacity = MAX(0, stream_buffer_capacity);
    const int normalized_filled = MAX(0, stream_buffer_filled);
    const gboolean normalized_sequence = sequence_active ? TRUE : FALSE;
    const gboolean normalized_buffer_supported =
        stream_buffer_supported ? TRUE : FALSE;
    const gboolean normalized_buffer_enabled =
        stream_buffer_enabled ? TRUE : FALSE;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    if(track < 0 || track >= view->max_tracks)
        return;
    lane = &view->lanes[track];
    geometry_changed =
        lane->play_mode != play_mode ||
        lane->source_id != source_id ||
        lane->total_frames != normalized_total ||
        lane->sequence_active != normalized_sequence ||
        lane->stream_buffer_supported != normalized_buffer_supported ||
        lane->stream_buffer_enabled != normalized_buffer_enabled ||
        lane->stream_buffer_capacity != normalized_capacity ||
        lane->stream_buffer_filled != normalized_filled;

    lane->play_mode = play_mode;
    lane->source_id = source_id;
    lane->source_type = source_type;
    lane->frame = MAX(0, frame);
    lane->total_frames = normalized_total;
    lane->speed = speed;
    lane->fps_x100 = MAX(0, fps_x100);
    lane->loop_type = loop_type;
    lane->fx_enabled = fx_enabled ? TRUE : FALSE;
    lane->audio_muted = audio_muted ? TRUE : FALSE;
    lane->sequence_active = normalized_sequence;
    lane->sequence_bank = sequence_bank;
    lane->sequence_slot = sequence_slot;
    lane->stream_buffer_supported = normalized_buffer_supported;
    lane->stream_buffer_enabled = normalized_buffer_enabled;
    lane->stream_buffer_capacity = normalized_capacity;
    lane->stream_buffer_filled = normalized_filled;
    lane->stream_buffer_position = MAX(0, stream_buffer_position);

    if(view->pending_source_kind == 1 &&
       view->pending_source_track == track &&
       view->pending_source_id == source_id &&
       play_mode == (view->pending_source_type == 0 ? MODE_SAMPLE : MODE_STREAM)) {
        view->pending_source_track = -1;
        view->pending_source_id = -1;
        view->pending_source_type = -1;
        view->pending_source_kind = 0;
    }

    if(track == view->project_master_track && !view->seek_dragging &&
       !sequence_active && source_id > 0) {
        view->playhead = play_mode == MODE_STREAM && stream_buffer_enabled ?
                         gvr_mt_clampi(stream_buffer_position,
                                       0,
                                       MAX(0, stream_buffer_filled - 1)) :
                         gvr_mt_clampi(lane->frame,
                                       0,
                                       MAX(0, lane->total_frames - 1));
        view->playhead_slot = -1;
        view->playhead_active = TRUE;
    }

    if(track == view->project_master_track && !view->seek_dragging &&
       sequence_active &&
       sequence_bank == view->bank && view->master_clips) {
        for(guint i = 0; i < view->master_clips->len; i++) {
            GvrMultiTrackMasterClip *clip =
                &g_array_index(view->master_clips,
                               GvrMultiTrackMasterClip,
                               i);
            if(clip->slot == sequence_slot) {
                view->playhead = gvr_mt_clampi(clip->project_in + lane->frame,
                                               clip->project_in,
                                               clip->project_out);
                view->playhead_slot = sequence_slot;
                view->playhead_active = TRUE;
                break;
            }
        }
    }

    if(geometry_changed)
        gvr_mt_invalidate_geometry(view);
    if(geometry_changed)
        gvr_mt_update_pan(view);
    if(track == view->project_master_track)
        gvr_mt_follow_transport(view);

    gvr_mt_update_lane_header(view, track);
    if(track == view->project_master_track)
        gvr_mt_update_summary(view);

    if(track == view->project_master_track || geometry_changed)
        gvr_mt_queue_draw(view);
    else
        gvr_mt_queue_timeline_draw(view);
}

void gvr_multi_track_edit_set_track_preview(GtkWidget *widget,
                                            int track,
                                            GdkPixbuf *pixbuf)
{
    GvrMultiTrackEdit *view;
    GvrMultiTrackLane *lane;
    GdkPixbuf *scaled;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    if(track < 0 || track >= view->max_tracks)
        return;
    lane = &view->lanes[track];

    if(lane->preview) {
        g_object_unref(lane->preview);
        lane->preview = NULL;
    }

    if(!pixbuf || !lane->preview_enabled) {
        gtk_image_clear(GTK_IMAGE(lane->preview_image));
        return;
    }

    lane->preview = g_object_ref(pixbuf);

    {
        const int source_width = gdk_pixbuf_get_width(pixbuf);
        const int source_height = gdk_pixbuf_get_height(pixbuf);
        const double width_scale = (double)GVR_MT_PREVIEW_MAX_WIDTH / (double)source_width;
        const double height_scale = (double)GVR_MT_PREVIEW_MAX_HEIGHT / (double)source_height;
        const double scale = width_scale < height_scale ? width_scale : height_scale;
        int preview_width = (int)floor((double)source_width * scale + 0.5);
        int preview_height = (int)floor((double)source_height * scale + 0.5);

        if(preview_width < 1)
            preview_width = 1;
        if(preview_height < 1)
            preview_height = 1;

        scaled = gdk_pixbuf_scale_simple(pixbuf,
                                         preview_width,
                                         preview_height,
                                         GDK_INTERP_BILINEAR);
    }

    gtk_image_set_from_pixbuf(GTK_IMAGE(lane->preview_image), scaled);
    if(scaled)
        g_object_unref(scaled);
}

GdkPixbuf *gvr_multi_track_edit_ref_track_preview(GtkWidget *widget,
                                                  int track)
{
    GvrMultiTrackEdit *view;
    GvrMultiTrackLane *lane;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return NULL;

    view = GVR_MULTI_TRACK_EDIT(widget);
    if(track < 0 || track >= view->max_tracks)
        return NULL;

    lane = &view->lanes[track];
    return lane->preview ? g_object_ref(lane->preview) : NULL;
}

void gvr_multi_track_edit_set_track_preview_enabled(GtkWidget *widget,
                                                    int track,
                                                    gboolean enabled)
{
    GvrMultiTrackEdit *view;
    GvrMultiTrackLane *lane;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    if(track < 0 || track >= view->max_tracks)
        return;

    lane = &view->lanes[track];
    lane->preview_enabled = enabled ? TRUE : FALSE;
    if(!lane->preview_enabled) {
        if(lane->preview) {
            g_object_unref(lane->preview);
            lane->preview = NULL;
        }
        gtk_image_clear(GTK_IMAGE(lane->preview_image));
    }
    gvr_mt_update_lane_header(view, track);
}

void gvr_multi_track_edit_set_current_control(GtkWidget *widget, int track)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    view->current_control_track = track;
    for(int i = 0; i < view->max_tracks; i++)
        view->lanes[i].current_control = i == track;
    gvr_mt_refresh_headers(view);
    gvr_mt_queue_timeline_draw(view);
}

void gvr_multi_track_edit_set_project_master(GtkWidget *widget, int track)
{
    GvrMultiTrackEdit *view;
    gboolean changed;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    changed = view->project_master_track != track;
    view->project_master_track = track;
    if(changed) {
        view->transport_seekable = FALSE;
        g_free(view->transport_seek_reason);
        view->transport_seek_reason =
            g_strdup("waiting for project-master playback status");
        view->seek_dragging = FALSE;
        if(view->timeline_area && gtk_widget_has_grab(view->timeline_area))
            gtk_grab_remove(view->timeline_area);
    }
    for(int i = 0; i < view->max_tracks; i++)
        view->lanes[i].project_master = i == track;
    gvr_mt_invalidate_geometry(view);
    gvr_mt_update_pan(view);
    gvr_mt_update_summary(view);
    gvr_mt_refresh_headers(view);
    gvr_mt_queue_draw(view);
}

void gvr_multi_track_edit_set_selected_track(GtkWidget *widget, int track)
{
    if(GVR_IS_MULTI_TRACK_EDIT(widget))
        gvr_mt_select_track(GVR_MULTI_TRACK_EDIT(widget), track, FALSE);
}

int gvr_multi_track_edit_get_selected_track(GtkWidget *widget)
{
    return GVR_IS_MULTI_TRACK_EDIT(widget) ?
        GVR_MULTI_TRACK_EDIT(widget)->selected_track : -1;
}

void gvr_multi_track_edit_set_playhead(GtkWidget *widget,
                                       int bank,
                                       int slot,
                                       int project_frame,
                                       gboolean active)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    view->bank = MAX(0, bank);
    if(!view->seek_dragging) {
        view->playhead_slot = slot;
        view->playhead = gvr_mt_clampi(project_frame,
                                      0,
                                      MAX(0, gvr_mt_effective_total(view) - 1));
        view->playhead_active = active ? TRUE : FALSE;
    }
    gvr_mt_follow_transport(view);
    gvr_mt_update_summary(view);
    gvr_mt_queue_draw(view);
}

void gvr_multi_track_edit_set_track_clips(GtkWidget *widget,
                                          int track,
                                          const GvrMultiTrackClip *clips,
                                          guint count)
{
    GvrMultiTrackEdit *view;
    GvrMultiTrackLane *lane;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    if(track < 0 || track >= view->max_tracks)
        return;
    lane = &view->lanes[track];
    g_ptr_array_set_size(lane->clips, 0);

    for(guint i = 0; clips && i < count; i++) {
        GvrMultiTrackClipData *copy = g_new0(GvrMultiTrackClipData, 1);
        copy->id = clips[i].id;
        copy->sample_id = clips[i].sample_id;
        copy->sample_type = clips[i].sample_type;
        copy->project_in = clips[i].project_in;
        copy->project_out = clips[i].project_out;
        copy->source_in = clips[i].source_in;
        copy->source_length = clips[i].source_length;
        copy->repeat_period = clips[i].repeat_period;
        copy->repeat_until = clips[i].repeat_until;
        copy->speed = clips[i].speed;
        copy->loop_type = clips[i].loop_type;
        copy->title = g_strdup(clips[i].title);
        g_ptr_array_add(lane->clips, copy);
    }

    gvr_mt_invalidate_geometry(view);
    gvr_mt_update_pan(view);
    gvr_mt_queue_draw(view);
}

void gvr_multi_track_edit_set_track_events(GtkWidget *widget,
                                           int track,
                                           const GvrMultiTrackEvent *events,
                                           guint count)
{
    GvrMultiTrackEdit *view;
    GvrMultiTrackLane *lane;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    if(track < 0 || track >= view->max_tracks)
        return;
    lane = &view->lanes[track];
    g_ptr_array_set_size(lane->events, 0);

    for(guint i = 0; events && i < count; i++) {
        GvrMultiTrackEventData *copy = g_new0(GvrMultiTrackEventData, 1);
        copy->project_frame = events[i].project_frame;
        copy->order = events[i].order;
        copy->vims_id = events[i].vims_id;
        copy->label = g_strdup(events[i].label);
        copy->message = g_strdup(events[i].message);
        g_ptr_array_add(lane->events, copy);
    }

    gvr_mt_queue_timeline_draw(view);
}


void gvr_multi_track_edit_set_transition_buses(GtkWidget *widget,
                                                int bus_a_track,
                                                int bus_b_track,
                                                int active_bus)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    view->bus_a_track = bus_a_track;
    view->bus_b_track = bus_b_track;
    view->active_bus = active_bus == 1 ? 1 : 0;
    gvr_mt_refresh_headers(view);
}

void gvr_multi_track_edit_set_transition_state(GtkWidget *widget,
                                               gboolean active,
                                               int progress)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    view->transition_active = active ? TRUE : FALSE;
    view->transition_progress_value = gvr_mt_clampi(progress, 0, 255);
    gvr_mt_refresh_headers(view);
}

void gvr_multi_track_edit_set_track_drift(GtkWidget *widget,
                                          int track,
                                          int drift_frames,
                                          int drift_millis,
                                          gboolean correcting)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    if(track < 0 || track >= view->max_tracks)
        return;
    view->lanes[track].drift_frames = drift_frames;
    view->lanes[track].drift_millis = drift_millis;
    view->lanes[track].drift_correcting = correcting ? TRUE : FALSE;
    gvr_mt_update_lane_header(view, track);
}

void gvr_multi_track_edit_set_drift_lock(GtkWidget *widget, gboolean enabled)
{
    GvrMultiTrackEdit *view;

    if(!GVR_IS_MULTI_TRACK_EDIT(widget))
        return;
    view = GVR_MULTI_TRACK_EDIT(widget);
    view->drift_lock_enabled = enabled ? TRUE : FALSE;
    view->syncing_drift_toggle = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(view->drift_toggle),
                                 view->drift_lock_enabled);
    view->syncing_drift_toggle = FALSE;
    gvr_mt_refresh_headers(view);
}
