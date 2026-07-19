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

#include <math.h>
#include <string.h>
#include <pango/pangocairo.h>
#include "gtkeditlistview.h"
#include "gtkmediaview.h"
#include "vj-api.h"

typedef struct {
    int index;
    char *filename;
    int timeline_in;
    int timeline_out;
    int file_in;
    int file_out;
    char *fourcc;
    guint color_index;
} GvrEditListSegmentData;

typedef struct {
    guint id;
    int frame;
    char *name;
    guint color_index;
} GvrEditListSeparatorData;

typedef struct {
    guint start_separator_id;
    guint end_separator_id;
    char *name;
    guint color_index;
} GvrEditListRegionData;

typedef enum {
    GVR_EDIT_LIST_DRAG_NONE = 0,
    GVR_EDIT_LIST_DRAG_SCRUB,
    GVR_EDIT_LIST_DRAG_CREATE,
    GVR_EDIT_LIST_DRAG_RESIZE_IN,
    GVR_EDIT_LIST_DRAG_RESIZE_OUT,
    GVR_EDIT_LIST_DRAG_MOVE_SELECTION,
    GVR_EDIT_LIST_DRAG_MOVE_SEPARATOR,
    GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_IN,
    GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_OUT,
    GVR_EDIT_LIST_DRAG_PAN_TIMELINE,
    GVR_EDIT_LIST_DRAG_PAN_NAVIGATOR
} GvrEditListDragMode;

typedef enum {
    GVR_EDIT_LIST_HIT_NONE = 0,
    GVR_EDIT_LIST_HIT_RULER,
    GVR_EDIT_LIST_HIT_CLIP,
    GVR_EDIT_LIST_HIT_SELECTION_IN,
    GVR_EDIT_LIST_HIT_SELECTION_OUT,
    GVR_EDIT_LIST_HIT_SELECTION_BODY,
    GVR_EDIT_LIST_HIT_SEPARATOR,
    GVR_EDIT_LIST_HIT_SEGMENT_IN,
    GVR_EDIT_LIST_HIT_SEGMENT_OUT,
    GVR_EDIT_LIST_HIT_REGION
} GvrEditListHit;

enum {
    MODEL_INDEX,
    MODEL_FILENAME,
    MODEL_EDL_RANGE,
    MODEL_FILE_RANGE,
    MODEL_LENGTH,
    MODEL_FOURCC,
    MODEL_TIMELINE_IN,
    MODEL_TIMELINE_OUT,
    MODEL_ARRAY_POS,
    MODEL_PLAYING,
    MODEL_N_COLUMNS
};

enum {
    SIGNAL_ACTION_REQUESTED,
    SIGNAL_SEEK_REQUESTED,
    SIGNAL_SELECTION_CHANGED,
    SIGNAL_SEGMENT_SELECTED,
    SIGNAL_SEPARATOR_ADDED,
    SIGNAL_SEPARATOR_MOVED,
    SIGNAL_SEPARATOR_REMOVED,
    SIGNAL_MEDIA_FILE_DROPPED,
    SIGNAL_LAST
};

typedef enum {
    GVR_EDIT_LIST_PASTE_PLAYHEAD = 1,
    GVR_EDIT_LIST_PASTE_SELECTION_IN,
    GVR_EDIT_LIST_PASTE_SELECTION_OUT,
    GVR_EDIT_LIST_PASTE_EXPLICIT
} GvrEditListPasteTarget;

struct _GvrEditListView {
    GtkBox parent_instance;

    GtkWidget *target_label;
    GtkWidget *playhead_label;
    GtkWidget *selection_label;
    GtkWidget *clipboard_label;

    GtkWidget *overview;
    GtkWidget *navigator;
    GtkWidget *zoom_out_button;
    GtkWidget *zoom_in_button;
    GtkWidget *zoom_fit_button;
    GtkWidget *zoom_selection_button;
    GtkWidget *zoom_frame_button;
    GtkWidget *snap_toggle;
    GtkWidget *separator_add_button;
    GtkWidget *separator_remove_button;
    GtkWidget *zoom_scale;
    GtkWidget *zoom_label;
    GtkWidget *pan_scrollbar;
    GtkAdjustment *pan_adjustment;
    GtkWidget *tree;
    GtkListStore *store;

    GtkWidget *append_button;
    GtkWidget *save_button;
    GtkWidget *cut_button;
    GtkWidget *copy_button;
    GtkWidget *paste_button;
    GtkWidget *delete_button;
    GtkWidget *crop_button;
    GtkWidget *new_sample_button;
    GtkWidget *save_selection_item;

    GtkWidget *in_spin;
    GtkWidget *out_spin;
    GtkWidget *in_time_label;
    GtkWidget *out_time_label;
    GtkWidget *set_in_button;
    GtkWidget *set_out_button;
    GtkWidget *select_all_button;
    GtkWidget *clear_selection_button;

    GtkWidget *paste_popover;
    GtkWidget *paste_frame_spin;

    GPtrArray *segments;
    GHashTable *separator_sets;
    GPtrArray *separators;
    GHashTable *region_sets;
    GPtrArray *regions;
    guint next_separator_id;
    guint selected_separator_id;

    int sample_id;
    int source_file_count;
    int total_frames;
    double fps;
    int playhead;

    gboolean selection_active;
    int selection_in;
    int selection_out;

    gboolean clipboard_valid;
    gboolean clipboard_cut;
    int clipboard_frames;
    int clipboard_source_in;
    int clipboard_source_out;

    int selected_segment;
    gboolean editable;
    gboolean syncing;
    gboolean syncing_tree;
    gboolean syncing_view;

    double timeline_zoom;
    int timeline_view_start;

    GvrEditListDragMode drag_mode;
    gboolean drag_moved;
    gboolean drag_copy;
    int drag_anchor_frame;
    int drag_original_in;
    int drag_original_out;
    int drag_preview_in;
    int drag_preview_out;
    int drag_destination;
    int drag_separator_origin;
    guint drag_separator_id;
    int drag_segment_pos;
    int drag_segment_in;
    int drag_segment_out;
    int drag_trim_boundary;
    int drag_pan_origin_start;
    int drag_pan_anchor_frames;
    double drag_press_x;
    double drag_press_y;
    gboolean snap_enabled;
    gboolean snap_visible;
    int snap_frame;
    int hover_frame;
    GvrEditListHit hover_hit;
    int hover_segment_pos;
    gboolean hover_segment_left;
};

struct _GvrEditListViewClass {
    GtkBoxClass parent_class;
};

static guint gvr_edit_list_view_signals[SIGNAL_LAST];

static void gvr_edit_list_emit_seek(GvrEditListView *view, int frame);
static void gvr_edit_list_take_focus(GvrEditListView *view);

#define GVR_EDIT_LIST_ZOOM_MIN 1.0
#define GVR_EDIT_LIST_ZOOM_MAX 128.0
#define GVR_EDIT_LIST_SEPARATOR_HEIGHT_MIN 32.0
#define GVR_EDIT_LIST_REGION_TOP_MIN 17.0
#define GVR_EDIT_LIST_REGION_HEIGHT_MIN 14.0
#define GVR_EDIT_LIST_RULER_HEIGHT_MIN 22.0
#define GVR_EDIT_LIST_MIN_CLIP_HEIGHT_MIN 42.0
#define GVR_EDIT_LIST_TICK_TARGET_PX 88.0
#define GVR_EDIT_LIST_HANDLE_HIT_PX 13.0
#define GVR_EDIT_LIST_HANDLE_DRAW_PX 9.0
#define GVR_EDIT_LIST_SEPARATOR_HIT_PX 8.0
#define GVR_EDIT_LIST_SNAP_RADIUS_PX 8.0
#define GVR_EDIT_LIST_AUTOSCROLL_PX 22.0
#define GVR_EDIT_LIST_SEGMENT_EDGE_HIT_PX 7.0
#define GVR_EDIT_LIST_NAVIGATOR_HEIGHT_MIN 24

G_DEFINE_TYPE(GvrEditListView, gvr_edit_list_view, GTK_TYPE_BOX)

static GtkTargetEntry gvr_edit_list_media_drop_targets[] = {
    { (gchar *)GVR_MEDIA_VIEW_DND_TARGET, GTK_TARGET_SAME_APP, 0 }
};

static double gvr_edit_list_font_points(GtkWidget *widget)
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

static double gvr_edit_list_font_px(GvrEditListView *view, double scale)
{
    return MAX(7.0,
               gvr_edit_list_font_points(view ? GTK_WIDGET(view) : NULL) *
               (96.0 / 72.0) * scale);
}

static PangoFontDescription *gvr_edit_list_font_description(GtkWidget *widget,
                                                             double scale,
                                                             PangoWeight weight)
{
    PangoContext *context = widget ? gtk_widget_get_pango_context(widget) : NULL;
    const PangoFontDescription *base = context ? pango_context_get_font_description(context) : NULL;
    PangoFontDescription *font = base ?
        pango_font_description_copy(base) :
        pango_font_description_from_string("Monospace 10");

    pango_font_description_set_family(font, "Monospace");
    pango_font_description_set_size(
        font,
        (int)lrint(gvr_edit_list_font_points(widget) * scale * PANGO_SCALE));
    pango_font_description_set_weight(font, weight);
    return font;
}

static double gvr_edit_list_separator_height(GvrEditListView *view)
{
    return MAX(GVR_EDIT_LIST_SEPARATOR_HEIGHT_MIN,
               ceil(gvr_edit_list_font_px(view, 0.75) + 22.0));
}

static double gvr_edit_list_region_top(GvrEditListView *view)
{
    return MAX(GVR_EDIT_LIST_REGION_TOP_MIN,
               floor(gvr_edit_list_separator_height(view) * 0.53));
}

static double gvr_edit_list_region_height(GvrEditListView *view)
{
    return MAX(GVR_EDIT_LIST_REGION_HEIGHT_MIN,
               ceil(gvr_edit_list_font_px(view, 0.75) + 4.0));
}

static double gvr_edit_list_ruler_height(GvrEditListView *view)
{
    return MAX(GVR_EDIT_LIST_RULER_HEIGHT_MIN,
               ceil(gvr_edit_list_font_px(view, 0.75) + 12.0));
}

static double gvr_edit_list_clip_top(GvrEditListView *view)
{
    return gvr_edit_list_separator_height(view) +
           gvr_edit_list_ruler_height(view) + 1.0;
}

static double gvr_edit_list_min_clip_height(GvrEditListView *view)
{
    return MAX(GVR_EDIT_LIST_MIN_CLIP_HEIGHT_MIN,
               ceil(gvr_edit_list_font_px(view, 0.75) + 32.0));
}

static int gvr_edit_list_navigator_height(GvrEditListView *view)
{
    return MAX(GVR_EDIT_LIST_NAVIGATOR_HEIGHT_MIN,
               (int)ceil(gvr_edit_list_font_px(view, 0.75) + 14.0));
}


static const GdkRGBA gvr_edit_list_file_palette[] = {
    { 0.25, 0.43, 0.56, 1.0 },
    { 0.29, 0.50, 0.38, 1.0 },
    { 0.56, 0.40, 0.24, 1.0 },
    { 0.46, 0.34, 0.58, 1.0 },
    { 0.24, 0.50, 0.49, 1.0 },
    { 0.57, 0.32, 0.35, 1.0 },
    { 0.31, 0.39, 0.62, 1.0 },
    { 0.55, 0.48, 0.23, 1.0 },
    { 0.31, 0.53, 0.31, 1.0 },
    { 0.54, 0.32, 0.49, 1.0 },
    { 0.24, 0.46, 0.63, 1.0 },
    { 0.59, 0.37, 0.26, 1.0 }
};

static int gvr_edit_list_clampi(int value, int min_value, int max_value)
{
    if(value < min_value)
        return min_value;
    if(value > max_value)
        return max_value;
    return value;
}

static void gvr_edit_list_add_class(GtkWidget *widget, const char *name)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), name);
}

static void gvr_edit_list_queue_timeline_draw(GvrEditListView *view)
{
    if(view->overview)
        gtk_widget_queue_draw(view->overview);
    if(view->navigator)
        gtk_widget_queue_draw(view->navigator);
}

static void gvr_edit_list_segment_free(gpointer data)
{
    GvrEditListSegmentData *segment = data;

    if(!segment)
        return;

    g_free(segment->filename);
    g_free(segment->fourcc);
    g_free(segment);
}

static void gvr_edit_list_separator_free(gpointer data)
{
    GvrEditListSeparatorData *separator = data;

    if(!separator)
        return;

    g_free(separator->name);
    g_free(separator);
}

static void gvr_edit_list_separator_array_free(gpointer data)
{
    GPtrArray *separators = data;

    if(separators)
        g_ptr_array_free(separators, TRUE);
}

static void gvr_edit_list_region_free(gpointer data)
{
    GvrEditListRegionData *region = data;

    if(!region)
        return;

    g_free(region->name);
    g_free(region);
}

static void gvr_edit_list_region_array_free(gpointer data)
{
    GPtrArray *regions = data;

    if(regions)
        g_ptr_array_free(regions, TRUE);
}

static gpointer gvr_edit_list_separator_set_key(int sample_id)
{
    return GINT_TO_POINTER(sample_id + 2);
}

static GPtrArray *gvr_edit_list_separator_set(GvrEditListView *view,
                                               int sample_id,
                                               gboolean create)
{
    gpointer key = gvr_edit_list_separator_set_key(sample_id);
    GPtrArray *separators = g_hash_table_lookup(view->separator_sets, key);

    if(!separators && create) {
        separators = g_ptr_array_new_with_free_func(gvr_edit_list_separator_free);
        g_hash_table_insert(view->separator_sets, key, separators);
    }

    return separators;
}

static GPtrArray *gvr_edit_list_region_set(GvrEditListView *view,
                                           int sample_id,
                                           gboolean create)
{
    gpointer key = gvr_edit_list_separator_set_key(sample_id);
    GPtrArray *regions = g_hash_table_lookup(view->region_sets, key);

    if(!regions && create) {
        regions = g_ptr_array_new_with_free_func(gvr_edit_list_region_free);
        g_hash_table_insert(view->region_sets, key, regions);
    }

    return regions;
}

static GvrEditListSeparatorData *gvr_edit_list_separator_by_id(GvrEditListView *view,
                                                                guint id,
                                                                guint *array_pos)
{
    guint i;

    if(!view || id == 0)
        return NULL;

    for(i = 0; i < view->separators->len; i++) {
        GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, i);
        if(separator->id == id) {
            if(array_pos)
                *array_pos = i;
            return separator;
        }
    }

    return NULL;
}

static gint gvr_edit_list_separator_compare(gconstpointer a, gconstpointer b)
{
    const GvrEditListSeparatorData *sa = *(GvrEditListSeparatorData * const *)a;
    const GvrEditListSeparatorData *sb = *(GvrEditListSeparatorData * const *)b;

    if(sa->frame != sb->frame)
        return sa->frame < sb->frame ? -1 : 1;
    return sa->id < sb->id ? -1 : (sa->id > sb->id ? 1 : 0);
}

static GvrEditListRegionData *gvr_edit_list_region_by_pair(GvrEditListView *view,
                                                            guint start_id,
                                                            guint end_id,
                                                            guint *array_pos)
{
    guint i;

    if(!view->regions)
        return NULL;

    for(i = 0; i < view->regions->len; i++) {
        GvrEditListRegionData *region = g_ptr_array_index(view->regions, i);
        if(region->start_separator_id == start_id &&
           region->end_separator_id == end_id) {
            if(array_pos)
                *array_pos = i;
            return region;
        }
    }

    return NULL;
}

static gboolean gvr_edit_list_region_bounds(GvrEditListView *view,
                                             GvrEditListRegionData *region,
                                             int *start_frame,
                                             int *end_boundary)
{
    GvrEditListSeparatorData *start;
    GvrEditListSeparatorData *end;
    guint start_pos;
    guint end_pos;

    if(!region)
        return FALSE;

    start = gvr_edit_list_separator_by_id(view, region->start_separator_id, &start_pos);
    end = gvr_edit_list_separator_by_id(view, region->end_separator_id, &end_pos);
    if(!start || !end || end_pos != start_pos + 1 || end->frame <= start->frame)
        return FALSE;

    if(start_frame)
        *start_frame = start->frame;
    if(end_boundary)
        *end_boundary = end->frame;
    return TRUE;
}

static gboolean gvr_edit_list_region_pair_at_frame(GvrEditListView *view,
                                                    int frame,
                                                    GvrEditListSeparatorData **start_out,
                                                    GvrEditListSeparatorData **end_out)
{
    GvrEditListSeparatorData *start = NULL;
    GvrEditListSeparatorData *end = NULL;
    guint i;

    for(i = 0; i < view->separators->len; i++) {
        GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, i);
        if(separator->frame <= frame)
            start = separator;
        if(separator->frame > frame) {
            end = separator;
            break;
        }
    }

    if(!start || !end || end->frame <= start->frame)
        return FALSE;

    if(start_out)
        *start_out = start;
    if(end_out)
        *end_out = end;
    return TRUE;
}

static GvrEditListRegionData *gvr_edit_list_region_at_frame(GvrEditListView *view,
                                                             int frame)
{
    GvrEditListSeparatorData *start;
    GvrEditListSeparatorData *end;

    if(!gvr_edit_list_region_pair_at_frame(view, frame, &start, &end))
        return NULL;

    return gvr_edit_list_region_by_pair(view, start->id, end->id, NULL);
}

static void gvr_edit_list_prune_regions(GvrEditListView *view)
{
    gint i;

    if(!view->regions)
        return;

    for(i = (gint)view->regions->len - 1; i >= 0; i--) {
        GvrEditListRegionData *region = g_ptr_array_index(view->regions, (guint)i);
        if(!gvr_edit_list_region_bounds(view, region, NULL, NULL))
            g_ptr_array_remove_index(view->regions, (guint)i);
    }
}

static int gvr_edit_list_frame_max(GvrEditListView *view)
{
    return MAX(0, view->total_frames - 1);
}

static int gvr_edit_list_clamp_frame(GvrEditListView *view, int frame)
{
    return gvr_edit_list_clampi(frame, 0, gvr_edit_list_frame_max(view));
}

static int gvr_edit_list_clamp_paste_frame(GvrEditListView *view, int frame)
{
    return gvr_edit_list_clampi(frame, 0, MAX(0, view->total_frames));
}

static gboolean gvr_edit_list_selection_valid(GvrEditListView *view)
{
    return view->selection_active &&
           view->selection_in >= 0 &&
           view->selection_out >= view->selection_in &&
           view->selection_out < MAX(1, view->total_frames);
}

static int gvr_edit_list_selection_length(GvrEditListView *view)
{
    if(!gvr_edit_list_selection_valid(view))
        return 0;

    return view->selection_out - view->selection_in + 1;
}

static gchar *gvr_edit_list_format_time(GvrEditListView *view, int frame)
{
    double fps = view->fps > 0.0 ? view->fps : 25.0;
    int nominal_fps = MAX(1, (int)floor(fps + 0.5));
    int safe_frame = MAX(0, frame);
    int total_seconds = (int)floor((double)safe_frame / fps);
    int frame_part = (int)floor((double)safe_frame - ((double)total_seconds * fps) + 0.5);
    int hours;
    int minutes;
    int seconds;

    if(frame_part >= nominal_fps) {
        frame_part = 0;
        total_seconds++;
    }

    hours = total_seconds / 3600;
    minutes = (total_seconds / 60) % 60;
    seconds = total_seconds % 60;

    return g_strdup_printf("%02d:%02d:%02d:%02d",
                           hours,
                           minutes,
                           seconds,
                           frame_part);
}

static gchar *gvr_edit_list_range_text(int start, int end)
{
    return g_strdup_printf("%d–%d", start, end);
}

static void gvr_edit_list_lookup_color(GtkWidget *widget,
                                       const char *name,
                                       const GdkRGBA *fallback,
                                       GdkRGBA *result)
{
    GtkStyleContext *context = gtk_widget_get_style_context(widget);

    if(!gtk_style_context_lookup_color(context, name, result))
        *result = *fallback;
}

static void gvr_edit_list_set_source_rgba(cairo_t *cr, const GdkRGBA *color)
{
    cairo_set_source_rgba(cr,
                          color->red,
                          color->green,
                          color->blue,
                          color->alpha);
}

static void gvr_edit_list_update_summary(GvrEditListView *view)
{
    gchar *duration = gvr_edit_list_format_time(view, MAX(0, view->total_frames - 1));
    gchar *target;
    gchar *position = gvr_edit_list_format_time(view, view->playhead);
    gchar *playhead;
    gchar *selection;

    if(view->sample_id > 0) {
        target = g_strdup_printf("Sample %d   %d source file%s · %u segment%s · %d frames · %s",
                                 view->sample_id,
                                 view->source_file_count,
                                 view->source_file_count == 1 ? "" : "s",
                                 view->segments->len,
                                 view->segments->len == 1 ? "" : "s",
                                 view->total_frames,
                                 duration);
    }
    else {
        target = g_strdup_printf("Edit list   %d source file%s · %u segment%s · %d frames · %s",
                                 view->source_file_count,
                                 view->source_file_count == 1 ? "" : "s",
                                 view->segments->len,
                                 view->segments->len == 1 ? "" : "s",
                                 view->total_frames,
                                 duration);
    }

    playhead = g_strdup_printf("Playhead %d · %s", view->playhead, position);

    if(gvr_edit_list_selection_valid(view)) {
        gchar *in_time = gvr_edit_list_format_time(view, view->selection_in);
        gchar *out_time = gvr_edit_list_format_time(view, view->selection_out);

        selection = g_strdup_printf("Selection %d–%d · %d frames · %s–%s",
                                    view->selection_in,
                                    view->selection_out,
                                    gvr_edit_list_selection_length(view),
                                    in_time,
                                    out_time);
        g_free(in_time);
        g_free(out_time);
    }
    else {
        selection = g_strdup("Selection —");
    }

    gtk_label_set_text(GTK_LABEL(view->target_label), target);
    gtk_label_set_text(GTK_LABEL(view->playhead_label), playhead);
    gtk_label_set_text(GTK_LABEL(view->selection_label), selection);

    g_free(selection);
    g_free(playhead);
    g_free(position);
    g_free(target);
    g_free(duration);
}

static void gvr_edit_list_update_drag_summary(GvrEditListView *view,
                                               const char *verb,
                                               int in_frame,
                                               int out_frame,
                                               int destination)
{
    gchar *in_time = gvr_edit_list_format_time(view, in_frame);
    gchar *out_time = gvr_edit_list_format_time(view, out_frame);
    gchar *text;

    if(destination >= 0) {
        gchar *dest_time = gvr_edit_list_format_time(view,
                                                     MIN(destination, gvr_edit_list_frame_max(view)));
        text = g_strdup_printf("%s %d–%d · %d frames · %s–%s · destination %d (%s)",
                               verb, in_frame, out_frame, out_frame - in_frame + 1,
                               in_time, out_time, destination, dest_time);
        g_free(dest_time);
    } else {
        text = g_strdup_printf("%s %d–%d · %d frames · %s–%s",
                               verb, in_frame, out_frame, out_frame - in_frame + 1,
                               in_time, out_time);
    }

    gtk_label_set_text(GTK_LABEL(view->selection_label), text);

    view->syncing = TRUE;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(view->in_spin), in_frame);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(view->out_spin), out_frame);
    gtk_label_set_text(GTK_LABEL(view->in_time_label), in_time);
    gtk_label_set_text(GTK_LABEL(view->out_time_label), out_time);
    view->syncing = FALSE;

    g_free(text);
    g_free(out_time);
    g_free(in_time);
}

static void gvr_edit_list_update_range_labels(GvrEditListView *view)
{
    gchar *in_time = gvr_edit_list_format_time(view, view->selection_in);
    gchar *out_time = gvr_edit_list_format_time(view, view->selection_out);

    gtk_label_set_text(GTK_LABEL(view->in_time_label), in_time);
    gtk_label_set_text(GTK_LABEL(view->out_time_label), out_time);

    g_free(in_time);
    g_free(out_time);
}

static void gvr_edit_list_update_clipboard_label(GvrEditListView *view)
{
    gchar *label;
    gchar *detail;

    if(!view->clipboard_valid || view->clipboard_frames <= 0) {
        gtk_label_set_text(GTK_LABEL(view->clipboard_label), "Clipboard empty");
        gtk_widget_set_tooltip_text(view->clipboard_label,
                                    "The edit-list clipboard is empty.");
        return;
    }

    label = g_strdup_printf("Clipboard: %d frame%s %s",
                            view->clipboard_frames,
                            view->clipboard_frames == 1 ? "" : "s",
                            view->clipboard_cut ? "cut" : "copied");
    detail = g_strdup_printf("%d frame%s %s from the inclusive range %d–%d.",
                             view->clipboard_frames,
                             view->clipboard_frames == 1 ? "" : "s",
                             view->clipboard_cut ? "cut" : "copied",
                             view->clipboard_source_in,
                             view->clipboard_source_out);

    gtk_label_set_text(GTK_LABEL(view->clipboard_label), label);
    gtk_widget_set_tooltip_text(view->clipboard_label, detail);

    g_free(detail);
    g_free(label);
}

static void gvr_edit_list_update_sensitivity(GvrEditListView *view)
{
    gboolean selection = gvr_edit_list_selection_valid(view);
    gboolean complete_selection = selection &&
                                  view->selection_in == 0 &&
                                  view->selection_out == gvr_edit_list_frame_max(view);
    gboolean has_content = view->total_frames > 0;

    gtk_widget_set_sensitive(view->append_button, view->editable);
    gtk_widget_set_sensitive(view->save_button, view->editable && has_content);
    gtk_widget_set_sensitive(view->save_selection_item,
                             view->editable && selection);

    gtk_widget_set_sensitive(view->cut_button, view->editable && selection);
    gtk_widget_set_sensitive(view->copy_button, view->editable && selection);
    gtk_widget_set_sensitive(view->paste_button,
                             view->editable && view->clipboard_valid);
    gtk_widget_set_sensitive(view->delete_button, view->editable && selection);
    gtk_widget_set_sensitive(view->crop_button,
                             view->editable && selection && !complete_selection);
    gtk_widget_set_sensitive(view->new_sample_button,
                             view->editable && selection);

    gtk_widget_set_sensitive(view->in_spin, view->editable && has_content);
    gtk_widget_set_sensitive(view->out_spin, view->editable && has_content);
    gtk_widget_set_sensitive(view->set_in_button, view->editable && has_content);
    gtk_widget_set_sensitive(view->set_out_button, view->editable && has_content);
    gtk_widget_set_sensitive(view->select_all_button,
                             view->editable && has_content);
    gtk_widget_set_sensitive(view->clear_selection_button,
                             view->editable && selection);
    gtk_widget_set_sensitive(view->zoom_selection_button, selection);
    gtk_widget_set_sensitive(view->zoom_frame_button, has_content);
    gtk_widget_set_sensitive(view->separator_add_button, view->editable && has_content);
    gtk_widget_set_sensitive(view->separator_remove_button,
                             view->editable && view->selected_separator_id != 0);
}

static void gvr_edit_list_sync_spins(GvrEditListView *view)
{
    view->syncing = TRUE;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(view->in_spin), view->selection_in);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(view->out_spin), view->selection_out);
    view->syncing = FALSE;

    gvr_edit_list_update_range_labels(view);
}

static void gvr_edit_list_emit_selection(GvrEditListView *view)
{
    g_signal_emit(view,
                  gvr_edit_list_view_signals[SIGNAL_SELECTION_CHANGED],
                  0,
                  view->selection_in,
                  view->selection_out,
                  view->selection_active);
}

static void gvr_edit_list_clear_tree_selection(GvrEditListView *view)
{
    GtkTreeSelection *selection;

    if(!view || !view->tree)
        return;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree));
    view->syncing_tree = TRUE;
    gtk_tree_selection_unselect_all(selection);
    view->syncing_tree = FALSE;
    view->selected_segment = -1;
}

static void gvr_edit_list_set_selection_internal(GvrEditListView *view,
                                                 int in_frame,
                                                 int out_frame,
                                                 gboolean active,
                                                 gboolean emit_signal)
{
    if(view->total_frames <= 0) {
        in_frame = 0;
        out_frame = 0;
        active = FALSE;
    }
    else {
        in_frame = gvr_edit_list_clamp_frame(view, in_frame);
        out_frame = gvr_edit_list_clamp_frame(view, out_frame);

        if(out_frame < in_frame) {
            int tmp = in_frame;
            in_frame = out_frame;
            out_frame = tmp;
        }
    }

    if(view->selection_active == active &&
       view->selection_in == in_frame &&
       view->selection_out == out_frame)
    {
        return;
    }

    view->selection_active = active ? TRUE : FALSE;
    view->selection_in = in_frame;
    view->selection_out = out_frame;

    if(!view->selection_active)
        gvr_edit_list_clear_tree_selection(view);

    gvr_edit_list_sync_spins(view);
    gvr_edit_list_update_summary(view);
    gvr_edit_list_update_sensitivity(view);
    gvr_edit_list_queue_timeline_draw(view);

    if(emit_signal)
        gvr_edit_list_emit_selection(view);
}

static void gvr_edit_list_adjust_separators_for_action(GvrEditListView *view,
                                                        GvrEditListAction action,
                                                        int in_frame,
                                                        int out_frame,
                                                        int position)
{
    int length = out_frame - in_frame + 1;
    gint i;

    if(view->separators->len == 0)
        return;

    if(action == GVR_EDIT_LIST_ACTION_CUT || action == GVR_EDIT_LIST_ACTION_DELETE) {
        for(i = (gint)view->separators->len - 1; i >= 0; i--) {
            GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, (guint)i);
            if(separator->frame >= in_frame && separator->frame <= out_frame)
                g_ptr_array_remove_index(view->separators, (guint)i);
            else if(separator->frame > out_frame)
                separator->frame -= length;
        }
    }
    else if(action == GVR_EDIT_LIST_ACTION_CROP) {
        for(i = (gint)view->separators->len - 1; i >= 0; i--) {
            GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, (guint)i);
            if(separator->frame < in_frame || separator->frame > out_frame)
                g_ptr_array_remove_index(view->separators, (guint)i);
            else
                separator->frame -= in_frame;
        }
    }
    else if(action == GVR_EDIT_LIST_ACTION_PASTE && view->clipboard_frames > 0) {
        for(i = 0; i < (gint)view->separators->len; i++) {
            GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, (guint)i);
            if(separator->frame >= position)
                separator->frame += view->clipboard_frames;
        }
    }
    else if(action == GVR_EDIT_LIST_ACTION_COPY_RANGE_TO) {
        for(i = 0; i < (gint)view->separators->len; i++) {
            GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, (guint)i);
            if(separator->frame >= position)
                separator->frame += length;
        }
    }
    else if(action == GVR_EDIT_LIST_ACTION_MOVE_RANGE) {
        int final_in;
        if(position < in_frame)
            final_in = position;
        else if(position > out_frame + 1)
            final_in = position - length;
        else
            final_in = in_frame;

        for(i = 0; i < (gint)view->separators->len; i++) {
            GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, (guint)i);
            if(separator->frame >= in_frame && separator->frame <= out_frame) {
                separator->frame = final_in + (separator->frame - in_frame);
            }
            else if(position < in_frame &&
                    separator->frame >= position && separator->frame < in_frame) {
                separator->frame += length;
            }
            else if(position > out_frame + 1 &&
                    separator->frame > out_frame && separator->frame < position) {
                separator->frame -= length;
            }
        }
    }

    g_ptr_array_sort(view->separators, gvr_edit_list_separator_compare);
    gvr_edit_list_prune_regions(view);
    if(view->selected_separator_id != 0 &&
       !gvr_edit_list_separator_by_id(view, view->selected_separator_id, NULL))
        view->selected_separator_id = 0;
    gtk_widget_set_sensitive(view->separator_remove_button,
                             view->editable && view->selected_separator_id != 0);
    gvr_edit_list_queue_timeline_draw(view);
}

static void gvr_edit_list_emit_action(GvrEditListView *view,
                                      GvrEditListAction action,
                                      int position)
{
    int in_frame = gvr_edit_list_selection_valid(view) ? view->selection_in : 0;
    int out_frame = gvr_edit_list_selection_valid(view) ? view->selection_out : 0;

    if(action == GVR_EDIT_LIST_ACTION_COPY ||
       action == GVR_EDIT_LIST_ACTION_CUT ||
       action == GVR_EDIT_LIST_ACTION_COPY_RANGE_TO)
    {
        gvr_edit_list_view_set_clipboard(GTK_WIDGET(view),
                                         TRUE,
                                         action == GVR_EDIT_LIST_ACTION_CUT,
                                         out_frame - in_frame + 1,
                                         in_frame,
                                         out_frame);
    }

    if(action == GVR_EDIT_LIST_ACTION_CUT ||
       action == GVR_EDIT_LIST_ACTION_DELETE ||
       action == GVR_EDIT_LIST_ACTION_CROP ||
       action == GVR_EDIT_LIST_ACTION_PASTE ||
       action == GVR_EDIT_LIST_ACTION_MOVE_RANGE ||
       action == GVR_EDIT_LIST_ACTION_COPY_RANGE_TO)
    {
        gvr_edit_list_adjust_separators_for_action(view, action,
                                                   in_frame, out_frame, position);
    }

    g_signal_emit(view,
                  gvr_edit_list_view_signals[SIGNAL_ACTION_REQUESTED],
                  0,
                  action,
                  in_frame,
                  out_frame,
                  position);
}

static void gvr_edit_list_toolbar_action(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GvrEditListAction action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "gvr-edit-action"));

    gvr_edit_list_emit_action(view, action, -1);
}

#define GVR_EDIT_LIST_ICON_MAX 22

static GdkPixbuf *gvr_edit_list_load_icon(const char *filename,
                                          gboolean flip_horizontal)
{
    char path[4096];
    GError *error = NULL;
    GdkPixbuf *pixbuf;

    get_gd(path, NULL, filename);
    pixbuf = gdk_pixbuf_new_from_file(path, &error);
    if(error) {
        g_error_free(error);
        return NULL;
    }

    if(flip_horizontal) {
        GdkPixbuf *flipped = gdk_pixbuf_flip(pixbuf, TRUE);
        g_object_unref(pixbuf);
        pixbuf = flipped;
    }

    if(pixbuf) {
        int width = gdk_pixbuf_get_width(pixbuf);
        int height = gdk_pixbuf_get_height(pixbuf);

        if(width > GVR_EDIT_LIST_ICON_MAX || height > GVR_EDIT_LIST_ICON_MAX) {
            double scale = MIN((double)GVR_EDIT_LIST_ICON_MAX / (double)width,
                               (double)GVR_EDIT_LIST_ICON_MAX / (double)height);
            int scaled_width = MAX(1, (int)floor((double)width * scale + 0.5));
            int scaled_height = MAX(1, (int)floor((double)height * scale + 0.5));
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf,
                                                        scaled_width,
                                                        scaled_height,
                                                        GDK_INTERP_BILINEAR);
            g_object_unref(pixbuf);
            pixbuf = scaled;
        }
    }

    return pixbuf;
}

static GtkWidget *gvr_edit_list_icon_child(const char *filename,
                                            gboolean flip_horizontal,
                                            const char *fallback)
{
    GdkPixbuf *pixbuf = gvr_edit_list_load_icon(filename, flip_horizontal);
    GtkWidget *child;

    if(!pixbuf)
        return gtk_label_new(fallback ? fallback : "?");

    child = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    return child;
}

static void gvr_edit_list_button_set_accessible_name(GtkWidget *button,
                                                      const char *name)
{
    AtkObject *accessible = gtk_widget_get_accessible(button);

    if(accessible && name)
        atk_object_set_name(accessible, name);
}

static GtkWidget *gvr_edit_list_image_button(const char *filename,
                                              gboolean flip_horizontal,
                                              const char *fallback,
                                              const char *tooltip)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *child = gvr_edit_list_icon_child(filename,
                                                flip_horizontal,
                                                fallback);

    gtk_container_add(GTK_CONTAINER(button), child);
    gtk_widget_show(child);
    gtk_widget_set_tooltip_text(button, tooltip);
    gvr_edit_list_button_set_accessible_name(button, fallback);
    return button;
}

static GtkWidget *gvr_edit_list_toolbar_button(GtkWidget *toolbar,
                                               const char *filename,
                                               const char *fallback,
                                               const char *tooltip,
                                               GvrEditListAction action,
                                               GvrEditListView *view)
{
    GtkWidget *button = gvr_edit_list_image_button(filename,
                                                    FALSE,
                                                    fallback,
                                                    tooltip);

    g_object_set_data(G_OBJECT(button),
                      "gvr-edit-action",
                      GINT_TO_POINTER(action));
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(gvr_edit_list_toolbar_action),
                     view);
    gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    gtk_widget_show(button);

    return button;
}

static GtkWidget *gvr_edit_list_text_toolbar_button(GtkWidget *toolbar,
                                                    const char *label,
                                                    const char *tooltip,
                                                    GvrEditListAction action,
                                                    GvrEditListView *view)
{
    GtkWidget *button = gtk_button_new_with_label(label);

    g_object_set_data(G_OBJECT(button),
                      "gvr-edit-action",
                      GINT_TO_POINTER(action));
    gtk_widget_set_tooltip_text(button, tooltip);
    gvr_edit_list_button_set_accessible_name(button, label);
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(gvr_edit_list_toolbar_action),
                     view);
    gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    gtk_widget_show(button);

    return button;
}

static GtkWidget *gvr_edit_list_menu_item(GtkWidget *menu,
                                          const char *label,
                                          const char *tooltip,
                                          GCallback callback,
                                          gpointer data)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);

    gtk_widget_set_tooltip_text(item, tooltip);
    g_signal_connect(item, "activate", callback, data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);

    return item;
}

static void gvr_edit_list_menu_action(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GvrEditListAction action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "gvr-edit-action"));

    gvr_edit_list_emit_action(view, action, -1);
}

static GtkWidget *gvr_edit_list_action_menu_item(GtkWidget *menu,
                                                 const char *label,
                                                 const char *tooltip,
                                                 GvrEditListAction action,
                                                 GvrEditListView *view)
{
    GtkWidget *item = gvr_edit_list_menu_item(menu,
                                              label,
                                              tooltip,
                                              G_CALLBACK(gvr_edit_list_menu_action),
                                              view);
    g_object_set_data(G_OBJECT(item),
                      "gvr-edit-action",
                      GINT_TO_POINTER(action));
    return item;
}

static void gvr_edit_list_paste_emit(GvrEditListView *view, int position)
{
    gvr_edit_list_emit_action(view,
                              GVR_EDIT_LIST_ACTION_PASTE,
                              gvr_edit_list_clamp_paste_frame(view, position));
}

static void gvr_edit_list_paste_target(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GvrEditListPasteTarget target = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "gvr-paste-target"));
    int position = view->playhead;

    switch(target) {
        case GVR_EDIT_LIST_PASTE_SELECTION_IN:
            if(gvr_edit_list_selection_valid(view))
                position = view->selection_in;
            break;

        case GVR_EDIT_LIST_PASTE_SELECTION_OUT:
            if(gvr_edit_list_selection_valid(view))
                position = view->selection_out + 1;
            break;

        case GVR_EDIT_LIST_PASTE_EXPLICIT:
            gtk_spin_button_set_range(GTK_SPIN_BUTTON(view->paste_frame_spin),
                                      0,
                                      MAX(0, view->total_frames));
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(view->paste_frame_spin),
                                      view->playhead);
            gtk_popover_popup(GTK_POPOVER(view->paste_popover));
            return;

        case GVR_EDIT_LIST_PASTE_PLAYHEAD:
        default:
            break;
    }

    gvr_edit_list_paste_emit(view, position);
}

static GtkWidget *gvr_edit_list_paste_menu_item(GtkWidget *menu,
                                                const char *label,
                                                GvrEditListPasteTarget target,
                                                GvrEditListView *view)
{
    GtkWidget *item = gvr_edit_list_menu_item(menu,
                                              label,
                                              NULL,
                                              G_CALLBACK(gvr_edit_list_paste_target),
                                              view);
    g_object_set_data(G_OBJECT(item),
                      "gvr-paste-target",
                      GINT_TO_POINTER(target));
    return item;
}

static void gvr_edit_list_paste_explicit_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    int position = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(view->paste_frame_spin));

    (void)widget;
    gtk_popover_popdown(GTK_POPOVER(view->paste_popover));
    gvr_edit_list_paste_emit(view, position);
}

static int gvr_edit_list_visible_frames(GvrEditListView *view)
{
    int total = MAX(1, view->total_frames);
    double zoom = CLAMP(view->timeline_zoom,
                        GVR_EDIT_LIST_ZOOM_MIN,
                        GVR_EDIT_LIST_ZOOM_MAX);

    return gvr_edit_list_clampi((int)ceil((double)total / zoom), 1, total);
}

static int gvr_edit_list_view_max_start(GvrEditListView *view)
{
    return MAX(0, view->total_frames - gvr_edit_list_visible_frames(view));
}

static gboolean gvr_edit_list_frame_visible(GvrEditListView *view, int frame)
{
    int visible = gvr_edit_list_visible_frames(view);

    return frame >= view->timeline_view_start &&
           frame < view->timeline_view_start + visible;
}

static void gvr_edit_list_sync_timeline_controls(GvrEditListView *view)
{
    int visible = gvr_edit_list_visible_frames(view);
    int total = MAX(1, view->total_frames);
    double zoom_value;
    gchar *label;

    view->timeline_view_start = gvr_edit_list_clampi(view->timeline_view_start,
                                                    0,
                                                    gvr_edit_list_view_max_start(view));
    zoom_value = log(CLAMP(view->timeline_zoom,
                           GVR_EDIT_LIST_ZOOM_MIN,
                           GVR_EDIT_LIST_ZOOM_MAX)) / log(2.0);

    view->syncing_view = TRUE;
    gtk_range_set_value(GTK_RANGE(view->zoom_scale), zoom_value);
    gtk_adjustment_configure(view->pan_adjustment,
                             view->timeline_view_start,
                             0.0,
                             total,
                             MAX(1.0, visible * 0.08),
                             MAX(1.0, visible * 0.80),
                             visible);
    view->syncing_view = FALSE;

    if(view->timeline_zoom <= 1.001)
        label = g_strdup("Fit");
    else if(fabs(view->timeline_zoom - floor(view->timeline_zoom + 0.5)) < 0.05)
        label = g_strdup_printf("%.0f×", view->timeline_zoom);
    else
        label = g_strdup_printf("%.1f×", view->timeline_zoom);

    gtk_label_set_text(GTK_LABEL(view->zoom_label), label);
    gtk_widget_set_sensitive(view->zoom_out_button,
                             view->timeline_zoom > GVR_EDIT_LIST_ZOOM_MIN + 0.001);
    gtk_widget_set_sensitive(view->zoom_in_button,
                             view->timeline_zoom < GVR_EDIT_LIST_ZOOM_MAX - 0.001);
    gtk_widget_set_sensitive(view->zoom_fit_button,
                             view->timeline_zoom > GVR_EDIT_LIST_ZOOM_MIN + 0.001);
    gtk_widget_set_sensitive(view->pan_scrollbar,
                             view->timeline_zoom > GVR_EDIT_LIST_ZOOM_MIN + 0.001 &&
                             view->total_frames > 1);
    g_free(label);
}

static void gvr_edit_list_set_zoom_internal(GvrEditListView *view,
                                            double zoom,
                                            int anchor_frame,
                                            double anchor_ratio)
{
    int visible;
    int start;

    if(view->total_frames <= 1) {
        view->timeline_zoom = GVR_EDIT_LIST_ZOOM_MIN;
        view->timeline_view_start = 0;
        gvr_edit_list_sync_timeline_controls(view);
        gvr_edit_list_queue_timeline_draw(view);
        return;
    }

    zoom = CLAMP(zoom, GVR_EDIT_LIST_ZOOM_MIN, GVR_EDIT_LIST_ZOOM_MAX);
    anchor_ratio = CLAMP(anchor_ratio, 0.0, 1.0);
    anchor_frame = gvr_edit_list_clamp_frame(view, anchor_frame);

    view->timeline_zoom = zoom;
    visible = gvr_edit_list_visible_frames(view);
    start = (int)floor(anchor_frame - (anchor_ratio * visible) + 0.5);
    view->timeline_view_start = gvr_edit_list_clampi(start,
                                                    0,
                                                    gvr_edit_list_view_max_start(view));

    gvr_edit_list_sync_timeline_controls(view);
    gvr_edit_list_queue_timeline_draw(view);
}

static void gvr_edit_list_zoom_scale_changed(GtkRange *range, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    int visible;
    int anchor;
    double ratio;
    double zoom;

    if(view->syncing_view)
        return;

    visible = gvr_edit_list_visible_frames(view);
    if(gvr_edit_list_frame_visible(view, view->playhead)) {
        anchor = view->playhead;
        ratio = visible > 0 ?
                (double)(anchor - view->timeline_view_start) / visible : 0.5;
    }
    else {
        anchor = view->timeline_view_start + (visible / 2);
        ratio = 0.5;
    }

    zoom = pow(2.0, gtk_range_get_value(range));
    gvr_edit_list_set_zoom_internal(view, zoom, anchor, ratio);
}

static void gvr_edit_list_zoom_out_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    double value = gtk_range_get_value(GTK_RANGE(view->zoom_scale));

    (void)widget;
    gtk_range_set_value(GTK_RANGE(view->zoom_scale), MAX(0.0, value - 0.5));
}

static void gvr_edit_list_zoom_in_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    double value = gtk_range_get_value(GTK_RANGE(view->zoom_scale));

    (void)widget;
    gtk_range_set_value(GTK_RANGE(view->zoom_scale), MIN(7.0, value + 0.5));
}

static void gvr_edit_list_zoom_fit_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);

    (void)widget;
    gvr_edit_list_set_zoom_internal(view,
                                    GVR_EDIT_LIST_ZOOM_MIN,
                                    view->total_frames / 2,
                                    0.5);
}

static void gvr_edit_list_pan_changed(GtkAdjustment *adjustment, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);

    if(view->syncing_view)
        return;

    view->timeline_view_start = gvr_edit_list_clampi((int)floor(gtk_adjustment_get_value(adjustment) + 0.5),
                                                    0,
                                                    gvr_edit_list_view_max_start(view));
    gvr_edit_list_queue_timeline_draw(view);
}

static double gvr_edit_list_frame_boundary_to_x(GvrEditListView *view,
                                                double boundary,
                                                double width)
{
    double visible = MAX(1, gvr_edit_list_visible_frames(view));

    return ((boundary - view->timeline_view_start) / visible) * width;
}

static double gvr_edit_list_frame_to_x(GvrEditListView *view,
                                       int frame,
                                       double width)
{
    double visible = MAX(1, gvr_edit_list_visible_frames(view));

    return ((((double)frame + 0.5) - view->timeline_view_start) / visible) * width;
}

static int gvr_edit_list_x_to_frame(GvrEditListView *view,
                                    double x,
                                    double width)
{
    int visible;
    int frame;

    if(width <= 1.0 || view->total_frames <= 1)
        return 0;

    visible = gvr_edit_list_visible_frames(view);
    frame = view->timeline_view_start +
            (int)floor(CLAMP(x / width, 0.0, 1.0) * visible);
    return gvr_edit_list_clamp_frame(view, frame);
}

static int gvr_edit_list_x_to_boundary(GvrEditListView *view,
                                       double x,
                                       double width)
{
    int visible;
    int boundary;

    if(width <= 1.0 || view->total_frames <= 0)
        return 0;

    visible = gvr_edit_list_visible_frames(view);
    boundary = view->timeline_view_start +
               (int)floor(CLAMP(x / width, 0.0, 1.0) * visible + 0.5);
    return gvr_edit_list_clampi(boundary, 0, view->total_frames);
}

static double gvr_edit_list_pixels_per_frame(GvrEditListView *view, double width)
{
    return width / MAX(1.0, (double)gvr_edit_list_visible_frames(view));
}

static GvrEditListSeparatorData *gvr_edit_list_separator_at_x(GvrEditListView *view,
                                                               double x,
                                                               double width)
{
    GvrEditListSeparatorData *best = NULL;
    double best_distance = G_MAXDOUBLE;
    guint i;

    for(i = 0; i < view->separators->len; i++) {
        GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, i);
        double sx = gvr_edit_list_frame_boundary_to_x(view, separator->frame, width);
        double distance = fabs(x - sx);

        if(distance <= GVR_EDIT_LIST_SEPARATOR_HIT_PX && distance < best_distance) {
            best = separator;
            best_distance = distance;
        }
    }

    return best;
}

static int gvr_edit_list_snap_boundary(GvrEditListView *view,
                                       int boundary,
                                       double width,
                                       GdkModifierType state,
                                       int ignore_a,
                                       int ignore_b)
{
    int best = gvr_edit_list_clampi(boundary, 0, view->total_frames);
    const double origin_x = gvr_edit_list_frame_boundary_to_x(view, best, width);
    double best_distance = GVR_EDIT_LIST_SNAP_RADIUS_PX + 0.001;
    guint i;

    view->snap_visible = FALSE;
    view->snap_frame = best;

    if(!view->snap_enabled || (state & GDK_MOD1_MASK) || width <= 1.0)
        return best;

#define GVR_TRY_SNAP(candidate_) do { \
    int candidate = gvr_edit_list_clampi((candidate_), 0, view->total_frames); \
    if(candidate != ignore_a && candidate != ignore_b) { \
        double distance = fabs(gvr_edit_list_frame_boundary_to_x(view, candidate, width) - origin_x); \
        if(distance <= best_distance) { \
            best = candidate; \
            best_distance = distance; \
            view->snap_visible = TRUE; \
            view->snap_frame = candidate; \
        } \
    } \
} while(0)

    GVR_TRY_SNAP(0);
    GVR_TRY_SNAP(view->total_frames);
    GVR_TRY_SNAP(view->playhead);

    for(i = 0; i < view->segments->len; i++) {
        GvrEditListSegmentData *segment = g_ptr_array_index(view->segments, i);
        GVR_TRY_SNAP(segment->timeline_in);
        GVR_TRY_SNAP(segment->timeline_out + 1);
    }

    for(i = 0; i < view->separators->len; i++) {
        GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, i);
        GVR_TRY_SNAP(separator->frame);
    }

    if(gvr_edit_list_selection_valid(view)) {
        GVR_TRY_SNAP(view->selection_in);
        GVR_TRY_SNAP(view->selection_out + 1);
    }

#undef GVR_TRY_SNAP
    return best;
}

static GvrEditListHit gvr_edit_list_segment_edge_hit(GvrEditListView *view,
                                                      double x,
                                                      double width,
                                                      int *segment_pos,
                                                      gboolean *left_edge)
{
    double best_score = G_MAXDOUBLE;
    GvrEditListHit best_hit = GVR_EDIT_LIST_HIT_NONE;
    guint i;

    if(segment_pos)
        *segment_pos = -1;
    if(left_edge)
        *left_edge = FALSE;

    for(i = 0; i < view->segments->len; i++) {
        GvrEditListSegmentData *segment = g_ptr_array_index(view->segments, i);
        double left_x;
        double right_x;
        double distance;
        double score;

        if(segment->timeline_out <= segment->timeline_in)
            continue;
        if(segment->timeline_out < view->timeline_view_start ||
           segment->timeline_in >= view->timeline_view_start + gvr_edit_list_visible_frames(view))
            continue;

        left_x = gvr_edit_list_frame_boundary_to_x(view, segment->timeline_in, width);
        distance = fabs(x - left_x);
        score = distance + (x < left_x ? 0.30 : 0.0);
        if(distance <= GVR_EDIT_LIST_SEGMENT_EDGE_HIT_PX && score < best_score) {
            best_score = score;
            best_hit = GVR_EDIT_LIST_HIT_SEGMENT_IN;
            if(segment_pos)
                *segment_pos = (int)i;
            if(left_edge)
                *left_edge = TRUE;
        }

        right_x = gvr_edit_list_frame_boundary_to_x(view, segment->timeline_out + 1, width);
        distance = fabs(x - right_x);
        score = distance + (x >= right_x ? 0.30 : 0.0);
        if(distance <= GVR_EDIT_LIST_SEGMENT_EDGE_HIT_PX && score < best_score) {
            best_score = score;
            best_hit = GVR_EDIT_LIST_HIT_SEGMENT_OUT;
            if(segment_pos)
                *segment_pos = (int)i;
            if(left_edge)
                *left_edge = FALSE;
        }
    }

    return best_hit;
}

static GvrEditListHit gvr_edit_list_hit_test(GvrEditListView *view,
                                              double x,
                                              double y,
                                              double width)
{
    view->hover_segment_pos = -1;
    view->hover_segment_left = FALSE;

    if(y < gvr_edit_list_separator_height(view)) {
        if(gvr_edit_list_separator_at_x(view, x, width))
            return GVR_EDIT_LIST_HIT_SEPARATOR;
        if(y >= gvr_edit_list_region_top(view)) {
            int frame = gvr_edit_list_x_to_frame(view, x, width);
            if(gvr_edit_list_region_at_frame(view, frame))
                return GVR_EDIT_LIST_HIT_REGION;
        }
        return GVR_EDIT_LIST_HIT_NONE;
    }

    if(y < gvr_edit_list_clip_top(view))
        return GVR_EDIT_LIST_HIT_RULER;

    if(gvr_edit_list_selection_valid(view)) {
        double in_x = gvr_edit_list_frame_boundary_to_x(view, view->selection_in, width);
        double out_x = gvr_edit_list_frame_boundary_to_x(view, view->selection_out + 1, width);
        double in_distance = fabs(x - in_x);
        double out_distance = fabs(x - out_x);
        gboolean in_hit = in_distance <= GVR_EDIT_LIST_HANDLE_HIT_PX;
        gboolean out_hit = out_distance <= GVR_EDIT_LIST_HANDLE_HIT_PX;

        if(in_hit && out_hit) {
            double midpoint = (in_x + out_x) * 0.5;
            return x <= midpoint ? GVR_EDIT_LIST_HIT_SELECTION_IN :
                                   GVR_EDIT_LIST_HIT_SELECTION_OUT;
        }
        if(in_hit)
            return GVR_EDIT_LIST_HIT_SELECTION_IN;
        if(out_hit)
            return GVR_EDIT_LIST_HIT_SELECTION_OUT;
    }

    {
        GvrEditListHit edge = gvr_edit_list_segment_edge_hit(view, x, width,
                                                              &view->hover_segment_pos,
                                                              &view->hover_segment_left);
        if(edge != GVR_EDIT_LIST_HIT_NONE)
            return edge;
    }

    if(gvr_edit_list_selection_valid(view)) {
        double in_x = gvr_edit_list_frame_boundary_to_x(view, view->selection_in, width);
        double out_x = gvr_edit_list_frame_boundary_to_x(view, view->selection_out + 1, width);
        if(x > in_x && x < out_x)
            return GVR_EDIT_LIST_HIT_SELECTION_BODY;
    }

    return GVR_EDIT_LIST_HIT_CLIP;
}

static void gvr_edit_list_update_cursor(GvrEditListView *view, GvrEditListHit hit)
{
    GdkWindow *window = gtk_widget_get_window(view->overview);
    GdkDisplay *display;
    GdkCursor *cursor = NULL;

    if(!window)
        return;

    display = gdk_window_get_display(window);
    switch(hit) {
        case GVR_EDIT_LIST_HIT_SELECTION_IN:
        case GVR_EDIT_LIST_HIT_SELECTION_OUT:
        case GVR_EDIT_LIST_HIT_SEGMENT_IN:
        case GVR_EDIT_LIST_HIT_SEGMENT_OUT:
            cursor = gdk_cursor_new_for_display(display, GDK_SB_H_DOUBLE_ARROW);
            break;
        case GVR_EDIT_LIST_HIT_SELECTION_BODY:
        case GVR_EDIT_LIST_HIT_SEPARATOR:
        case GVR_EDIT_LIST_HIT_REGION:
            cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
            break;
        case GVR_EDIT_LIST_HIT_RULER:
            cursor = gdk_cursor_new_for_display(display, GDK_HAND1);
            break;
        case GVR_EDIT_LIST_HIT_CLIP:
            cursor = gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
            break;
        default:
            break;
    }

    gdk_window_set_cursor(window, cursor);
    if(cursor)
        g_object_unref(cursor);
}

static int gvr_edit_list_tick_step(GvrEditListView *view, double width)
{
    double visible = gvr_edit_list_visible_frames(view);
    double raw = MAX(1.0, visible * GVR_EDIT_LIST_TICK_TARGET_PX / MAX(1.0, width));
    double fps = view->fps > 0.0 ? view->fps : 25.0;
    double unit;
    double scaled;
    double nice;

    if(raw < fps) {
        unit = pow(10.0, floor(log10(raw)));
        scaled = raw / unit;
        nice = scaled <= 1.0 ? 1.0 : (scaled <= 2.0 ? 2.0 : (scaled <= 5.0 ? 5.0 : 10.0));
        return MAX(1, (int)floor(nice * unit + 0.5));
    }

    raw /= fps;
    unit = pow(10.0, floor(log10(raw)));
    scaled = raw / unit;
    nice = scaled <= 1.0 ? 1.0 : (scaled <= 2.0 ? 2.0 : (scaled <= 5.0 ? 5.0 : 10.0));
    return MAX(1, (int)floor(nice * unit * fps + 0.5));
}

static guint gvr_edit_list_file_color_index(const char *filename)
{
    guint count = G_N_ELEMENTS(gvr_edit_list_file_palette);

    return count > 0 ? g_str_hash(filename ? filename : "") % count : 0;
}

static GdkRGBA gvr_edit_list_file_color(const GvrEditListSegmentData *segment,
                                        gboolean selected)
{
    GdkRGBA color = gvr_edit_list_file_palette[
        segment->color_index % G_N_ELEMENTS(gvr_edit_list_file_palette)];

    if(selected) {
        color.red = MIN(1.0, color.red + 0.10);
        color.green = MIN(1.0, color.green + 0.10);
        color.blue = MIN(1.0, color.blue + 0.10);
    }

    return color;
}

static GvrEditListSegmentData *gvr_edit_list_segment_at_frame(GvrEditListView *view,
                                                              int frame,
                                                              guint *array_pos)
{
    guint i;

    for(i = 0; i < view->segments->len; i++) {
        GvrEditListSegmentData *segment = g_ptr_array_index(view->segments, i);

        if(frame >= segment->timeline_in && frame <= segment->timeline_out) {
            if(array_pos)
                *array_pos = i;
            return segment;
        }
    }

    return NULL;
}

static void gvr_edit_list_select_segment_pos(GvrEditListView *view,
                                              guint array_pos,
                                              gboolean extend,
                                              gboolean zoom)
{
    GvrEditListSegmentData *segment;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeSelection *tree_selection;
    gboolean valid;

    if(array_pos >= view->segments->len)
        return;

    segment = g_ptr_array_index(view->segments, array_pos);
    view->selected_segment = segment->index;

    if(extend && gvr_edit_list_selection_valid(view)) {
        gvr_edit_list_set_selection_internal(view,
                                             MIN(view->selection_in, segment->timeline_in),
                                             MAX(view->selection_out, segment->timeline_out),
                                             TRUE, TRUE);
    } else {
        gvr_edit_list_set_selection_internal(view,
                                             segment->timeline_in,
                                             segment->timeline_out,
                                             TRUE, TRUE);
    }

    tree_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree));
    model = GTK_TREE_MODEL(view->store);
    valid = gtk_tree_model_get_iter_first(model, &iter);
    view->syncing_tree = TRUE;
    while(valid) {
        int pos = -1;
        gtk_tree_model_get(model, &iter, MODEL_ARRAY_POS, &pos, -1);
        if(pos == (int)array_pos) {
            GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
            gtk_tree_selection_select_iter(tree_selection, &iter);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(view->tree), path,
                                         NULL, FALSE, 0.0, 0.0);
            gtk_tree_path_free(path);
            break;
        }
        valid = gtk_tree_model_iter_next(model, &iter);
    }
    view->syncing_tree = FALSE;

    g_signal_emit(view,
                  gvr_edit_list_view_signals[SIGNAL_SEGMENT_SELECTED],
                  0,
                  view->selected_segment);

    if(zoom) {
        int span = segment->timeline_out - segment->timeline_in + 1;
        double zoom_value = CLAMP((double)MAX(1, view->total_frames) / MAX(1, span),
                                  GVR_EDIT_LIST_ZOOM_MIN,
                                  GVR_EDIT_LIST_ZOOM_MAX);
        gvr_edit_list_set_zoom_internal(view, zoom_value,
                                        segment->timeline_in + span / 2, 0.5);
    }

    gvr_edit_list_queue_timeline_draw(view);
}

static int gvr_edit_list_nearest_edit_boundary(GvrEditListView *view,
                                                int from_frame,
                                                gboolean next)
{
    int candidate = next ? view->total_frames : 0;
    guint i;

    for(i = 0; i < view->segments->len; i++) {
        GvrEditListSegmentData *segment = g_ptr_array_index(view->segments, i);
        int boundaries[2] = { segment->timeline_in, segment->timeline_out + 1 };
        int j;
        for(j = 0; j < 2; j++) {
            int boundary = boundaries[j];
            if(next) {
                if(boundary > from_frame && boundary < candidate)
                    candidate = boundary;
            } else {
                if(boundary < from_frame && boundary > candidate)
                    candidate = boundary;
            }
        }
    }

    return gvr_edit_list_clampi(candidate, 0, view->total_frames);
}

static gboolean gvr_edit_list_seek_edit_boundary(GvrEditListView *view,
                                                  gboolean next,
                                                  gboolean extend)
{
    int boundary;
    int target_frame;

    if(view->total_frames <= 0)
        return FALSE;

    boundary = gvr_edit_list_nearest_edit_boundary(view, view->playhead, next);
    if(boundary == view->playhead || (!next && boundary == 0 && view->playhead == 0))
        return FALSE;

    target_frame = boundary >= view->total_frames ? view->total_frames - 1 : boundary;
    if(extend) {
        int in_frame;
        int out_frame;
        if(next) {
            in_frame = gvr_edit_list_selection_valid(view) ? view->selection_in : view->playhead;
            out_frame = MAX(in_frame, boundary - 1);
        } else {
            in_frame = boundary;
            out_frame = gvr_edit_list_selection_valid(view) ? view->selection_out : view->playhead;
        }
        gvr_edit_list_set_selection_internal(view, in_frame, out_frame, TRUE, TRUE);
    }
    gvr_edit_list_emit_seek(view, target_frame);
    return TRUE;
}

static void gvr_edit_list_separator_emit(GvrEditListView *view,
                                         guint signal_id,
                                         guint id,
                                         int frame)
{
    if(signal_id == SIGNAL_SEPARATOR_REMOVED)
        g_signal_emit(view, gvr_edit_list_view_signals[signal_id], 0, (int)id);
    else
        g_signal_emit(view, gvr_edit_list_view_signals[signal_id], 0, (int)id, frame);
}

static GvrEditListSeparatorData *gvr_edit_list_add_separator(GvrEditListView *view,
                                                             int frame,
                                                             const char *name,
                                                             gboolean emit_signal)
{
    GvrEditListSeparatorData *separator;

    if(view->total_frames <= 0)
        return NULL;

    separator = g_new0(GvrEditListSeparatorData, 1);
    separator->id = view->next_separator_id++;
    separator->frame = gvr_edit_list_clampi(frame, 0, view->total_frames);
    separator->name = g_strdup(name && *name ? name : "");
    separator->color_index = (separator->id - 1) % G_N_ELEMENTS(gvr_edit_list_file_palette);
    g_ptr_array_add(view->separators, separator);
    g_ptr_array_sort(view->separators, gvr_edit_list_separator_compare);
    gvr_edit_list_prune_regions(view);
    view->selected_separator_id = separator->id;
    gtk_widget_set_sensitive(view->separator_remove_button, TRUE);
    gvr_edit_list_queue_timeline_draw(view);

    if(emit_signal)
        gvr_edit_list_separator_emit(view, SIGNAL_SEPARATOR_ADDED, separator->id, separator->frame);

    return separator;
}

static void gvr_edit_list_remove_separator(GvrEditListView *view, guint id, gboolean emit_signal)
{
    guint pos;
    GvrEditListSeparatorData *separator = gvr_edit_list_separator_by_id(view, id, &pos);

    if(!separator)
        return;

    g_ptr_array_remove_index(view->separators, pos);
    gvr_edit_list_prune_regions(view);
    if(view->selected_separator_id == id)
        view->selected_separator_id = 0;
    gtk_widget_set_sensitive(view->separator_remove_button, view->selected_separator_id != 0);
    gvr_edit_list_queue_timeline_draw(view);

    if(emit_signal)
        gvr_edit_list_separator_emit(view, SIGNAL_SEPARATOR_REMOVED, id, 0);
}

static void gvr_edit_list_move_separator(GvrEditListView *view,
                                         guint id,
                                         int frame,
                                         gboolean emit_signal)
{
    GvrEditListSeparatorData *separator = gvr_edit_list_separator_by_id(view, id, NULL);

    if(!separator)
        return;

    frame = gvr_edit_list_clampi(frame, 0, view->total_frames);
    if(separator->frame == frame)
        return;

    separator->frame = frame;
    g_ptr_array_sort(view->separators, gvr_edit_list_separator_compare);
    gvr_edit_list_queue_timeline_draw(view);

    if(emit_signal)
        gvr_edit_list_separator_emit(view, SIGNAL_SEPARATOR_MOVED, id, frame);
}

static void gvr_edit_list_rename_separator(GvrEditListView *view, guint id)
{
    GvrEditListSeparatorData *separator = gvr_edit_list_separator_by_id(view, id, NULL);
    GtkWidget *dialog;
    GtkWidget *entry;
    GtkWidget *content;
    GtkWindow *parent = NULL;

    if(!separator)
        return;

    if(gtk_widget_get_toplevel(GTK_WIDGET(view)) &&
       GTK_IS_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view))))
    {
        parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view)));
    }

    dialog = gtk_dialog_new_with_buttons("Rename Separator",
                                         parent,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Rename", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), separator->name ? separator->name : "");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);

    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        g_free(separator->name);
        separator->name = g_strdup(text ? text : "");
        gvr_edit_list_queue_timeline_draw(view);
    }

    gtk_widget_destroy(dialog);
}

static void gvr_edit_list_name_region(GvrEditListView *view, int frame)
{
    GvrEditListSeparatorData *start;
    GvrEditListSeparatorData *end;
    GvrEditListRegionData *region;
    GtkWidget *dialog;
    GtkWidget *entry;
    GtkWidget *content;
    GtkWindow *parent = NULL;

    if(!gvr_edit_list_region_pair_at_frame(view, frame, &start, &end))
        return;

    region = gvr_edit_list_region_by_pair(view, start->id, end->id, NULL);
    if(gtk_widget_get_toplevel(GTK_WIDGET(view)) &&
       GTK_IS_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view))))
        parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view)));

    dialog = gtk_dialog_new_with_buttons(region ? "Rename Region" : "Name Region",
                                         parent,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         region ? "_Rename" : "_Create", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), region && region->name ? region->name : "");
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Region name");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);

    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        if(!region) {
            region = g_new0(GvrEditListRegionData, 1);
            region->start_separator_id = start->id;
            region->end_separator_id = end->id;
            region->color_index = start->color_index;
            g_ptr_array_add(view->regions, region);
        }
        g_free(region->name);
        region->name = g_strdup(text && *text ? text : "Region");
        gvr_edit_list_queue_timeline_draw(view);
    }

    gtk_widget_destroy(dialog);
}

static void gvr_edit_list_clear_region(GvrEditListView *view, int frame)
{
    GvrEditListSeparatorData *start;
    GvrEditListSeparatorData *end;
    guint pos;

    if(!gvr_edit_list_region_pair_at_frame(view, frame, &start, &end))
        return;
    if(gvr_edit_list_region_by_pair(view, start->id, end->id, &pos)) {
        g_ptr_array_remove_index(view->regions, pos);
        gvr_edit_list_queue_timeline_draw(view);
    }
}

static gboolean gvr_edit_list_select_region(GvrEditListView *view,
                                             int frame,
                                             gboolean zoom)
{
    GvrEditListSeparatorData *start;
    GvrEditListSeparatorData *end;
    int out_frame;

    if(!gvr_edit_list_region_pair_at_frame(view, frame, &start, &end))
        return FALSE;

    out_frame = MIN(gvr_edit_list_frame_max(view), end->frame - 1);
    if(out_frame < start->frame)
        return FALSE;

    gvr_edit_list_set_selection_internal(view, start->frame, out_frame, TRUE, TRUE);
    if(zoom) {
        int span = out_frame - start->frame + 1;
        double zoom_value = CLAMP((double)MAX(1, view->total_frames) / MAX(1, span),
                                  GVR_EDIT_LIST_ZOOM_MIN,
                                  GVR_EDIT_LIST_ZOOM_MAX);
        gvr_edit_list_set_zoom_internal(view, zoom_value,
                                        start->frame + span / 2, 0.5);
    }
    return TRUE;
}

static void gvr_edit_list_separator_add_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    (void)widget;
    gvr_edit_list_add_separator(view, view->playhead, NULL, TRUE);
}

static void gvr_edit_list_separator_remove_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    (void)widget;
    gvr_edit_list_remove_separator(view, view->selected_separator_id, TRUE);
}

static void gvr_edit_list_snap_toggled(GtkToggleButton *button, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    view->snap_enabled = gtk_toggle_button_get_active(button);
    view->snap_visible = FALSE;
    gvr_edit_list_queue_timeline_draw(view);
}

static void gvr_edit_list_zoom_selection_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    int span;
    double zoom;
    (void)widget;

    if(!gvr_edit_list_selection_valid(view))
        return;

    span = MAX(1, gvr_edit_list_selection_length(view));
    zoom = CLAMP((double)MAX(1, view->total_frames) / (double)span,
                 GVR_EDIT_LIST_ZOOM_MIN,
                 GVR_EDIT_LIST_ZOOM_MAX);
    gvr_edit_list_set_zoom_internal(view,
                                    zoom,
                                    view->selection_in + span / 2,
                                    0.5);
}

static void gvr_edit_list_zoom_frame_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkAllocation allocation;
    double target_visible;
    double zoom;
    (void)widget;

    gtk_widget_get_allocation(view->overview, &allocation);
    target_visible = MAX(1.0, (double)MAX(1, allocation.width) / 8.0);
    zoom = CLAMP((double)MAX(1, view->total_frames) / target_visible,
                 GVR_EDIT_LIST_ZOOM_MIN,
                 GVR_EDIT_LIST_ZOOM_MAX);
    gvr_edit_list_set_zoom_internal(view, zoom, view->playhead, 0.5);
}

static gboolean gvr_edit_list_overview_draw(GtkWidget *widget,
                                            cairo_t *cr,
                                            gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkAllocation allocation;
    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    const GdkRGBA fallback_panel = { 0.176, 0.180, 0.212, 1.0 };
    const GdkRGBA fallback_raised = { 0.208, 0.212, 0.243, 1.0 };
    const GdkRGBA fallback_border = { 0.196, 0.208, 0.251, 1.0 };
    const GdkRGBA fallback_text = { 1.0, 1.0, 1.0, 1.0 };
    const GdkRGBA fallback_muted = { 0.522, 0.537, 0.600, 1.0 };
    const GdkRGBA fallback_selection_fill = { 0.055, 0.690, 0.790, 0.30 };
    const GdkRGBA fallback_selection_edge = { 0.300, 0.940, 1.000, 1.0 };
    const GdkRGBA fallback_playing = { 1.0, 0.518, 0.0, 1.0 };
    GdkRGBA panel, raised, border, text, muted, selection_fill_color, selection_edge, playing;
    double width, height, bar_y, bar_h, px_per_frame;
    int visible_frames, visible_start, visible_end, tick_step, tick;
    guint i;

    gtk_widget_get_allocation(widget, &allocation);
    width = allocation.width;
    height = allocation.height;
    bar_y = gvr_edit_list_clip_top(view);
    bar_h = MAX(gvr_edit_list_min_clip_height(view), height - bar_y - 2.0);
    visible_frames = gvr_edit_list_visible_frames(view);
    visible_start = view->timeline_view_start;
    visible_end = MIN(view->total_frames, visible_start + visible_frames);
    px_per_frame = gvr_edit_list_pixels_per_frame(view, width);

    gtk_render_background(context, cr, 0, 0, width, height);
    gvr_edit_list_lookup_color(widget, "panel-color", &fallback_panel, &panel);
    gvr_edit_list_lookup_color(widget, "raised-color", &fallback_raised, &raised);
    gvr_edit_list_lookup_color(widget, "border-color", &fallback_border, &border);
    gvr_edit_list_lookup_color(widget, "text-color", &fallback_text, &text);
    gvr_edit_list_lookup_color(widget, "text-muted", &fallback_muted, &muted);
    gvr_edit_list_lookup_color(widget, "edit-selection-fill-color",
                               &fallback_selection_fill, &selection_fill_color);
    gvr_edit_list_lookup_color(widget, "edit-selection-edge-color",
                               &fallback_selection_edge, &selection_edge);
    gvr_edit_list_lookup_color(widget, "slot-playing-color", &fallback_playing, &playing);

    gvr_edit_list_set_source_rgba(cr, &panel);
    cairo_rectangle(cr, 0.0, 0.0, width, gvr_edit_list_separator_height(view));
    cairo_fill(cr);
    gvr_edit_list_set_source_rgba(cr, &raised);
    cairo_rectangle(cr, 0.0, gvr_edit_list_separator_height(view), width, gvr_edit_list_ruler_height(view));
    cairo_fill(cr);

    gvr_edit_list_set_source_rgba(cr, &border);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 0.0, gvr_edit_list_separator_height(view) - 0.5);
    cairo_line_to(cr, width, gvr_edit_list_separator_height(view) - 0.5);
    cairo_move_to(cr, 0.0, gvr_edit_list_clip_top(view) - 0.5);
    cairo_line_to(cr, width, gvr_edit_list_clip_top(view) - 0.5);
    cairo_stroke(cr);

    tick_step = gvr_edit_list_tick_step(view, width);
    tick = ((visible_start + tick_step - 1) / tick_step) * tick_step;
    for(; tick <= visible_end; tick += tick_step) {
        double x = floor(gvr_edit_list_frame_boundary_to_x(view, tick, width)) + 0.5;
        PangoLayout *layout;
        PangoFontDescription *font;
        gchar *time;

        if(x < -1.0 || x > width + 1.0)
            continue;

        gvr_edit_list_set_source_rgba(cr, &border);
        cairo_move_to(cr, x, gvr_edit_list_clip_top(view) - 7.0);
        cairo_line_to(cr, x, gvr_edit_list_clip_top(view));
        cairo_stroke(cr);

        time = gvr_edit_list_format_time(view, tick);
        layout = gtk_widget_create_pango_layout(widget, time);
        font = gvr_edit_list_font_description(widget,
                                                  1.0,
                                                  PANGO_WEIGHT_NORMAL);
        pango_layout_set_font_description(layout, font);
        pango_layout_set_single_paragraph_mode(layout, TRUE);
        gvr_edit_list_set_source_rgba(cr, &muted);
        cairo_move_to(cr, x + 3.0, gvr_edit_list_separator_height(view) + 1.0);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(font);
        g_object_unref(layout);
        g_free(time);
    }

    if(px_per_frame >= 6.0) {
        int frame;
        int first = MAX(visible_start, 0);
        int last = MIN(visible_end, view->total_frames);
        GdkRGBA grid = border;
        grid.alpha = px_per_frame >= 12.0 ? 0.48 : 0.25;
        gvr_edit_list_set_source_rgba(cr, &grid);
        cairo_set_line_width(cr, 1.0);
        for(frame = first; frame <= last; frame++) {
            double x = floor(gvr_edit_list_frame_boundary_to_x(view, frame, width)) + 0.5;
            cairo_move_to(cr, x, bar_y);
            cairo_line_to(cr, x, bar_y + bar_h);
        }
        cairo_stroke(cr);
    }

    gvr_edit_list_set_source_rgba(cr, &panel);
    cairo_rectangle(cr, 0.5, bar_y + 0.5, width - 1.0, bar_h - 1.0);
    cairo_fill(cr);

    for(i = 0; i < view->segments->len; i++) {
        GvrEditListSegmentData *segment = g_ptr_array_index(view->segments, i);
        int clip_in, clip_out;
        double x0, x1, segment_width;
        GdkRGBA fill, highlight;

        if(segment->timeline_out < visible_start || segment->timeline_in >= visible_end)
            continue;

        clip_in = MAX(segment->timeline_in, visible_start);
        clip_out = MIN(segment->timeline_out + 1, visible_end);
        x0 = CLAMP(gvr_edit_list_frame_boundary_to_x(view, clip_in, width), 0.0, width);
        x1 = CLAMP(gvr_edit_list_frame_boundary_to_x(view, clip_out, width), 0.0, width);
        segment_width = MAX(1.0, x1 - x0);
        fill = gvr_edit_list_file_color(segment, segment->index == view->selected_segment);
        highlight = fill;
        highlight.red = MIN(1.0, highlight.red + 0.09);
        highlight.green = MIN(1.0, highlight.green + 0.09);
        highlight.blue = MIN(1.0, highlight.blue + 0.09);

        gvr_edit_list_set_source_rgba(cr, &fill);
        cairo_rectangle(cr, x0, bar_y, segment_width, bar_h);
        cairo_fill(cr);
        gvr_edit_list_set_source_rgba(cr, &highlight);
        cairo_rectangle(cr, x0, bar_y, segment_width, 3.0);
        cairo_fill(cr);
        gvr_edit_list_set_source_rgba(cr, &border);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, floor(x0) + 0.5, bar_y);
        cairo_line_to(cr, floor(x0) + 0.5, bar_y + bar_h);
        cairo_move_to(cr, floor(x1) - 0.5, bar_y);
        cairo_line_to(cr, floor(x1) - 0.5, bar_y + bar_h);
        cairo_stroke(cr);

        if((view->hover_hit == GVR_EDIT_LIST_HIT_SEGMENT_IN ||
            view->hover_hit == GVR_EDIT_LIST_HIT_SEGMENT_OUT) &&
           view->hover_segment_pos == (int)i && view->drag_mode == GVR_EDIT_LIST_DRAG_NONE) {
            double edge_x = view->hover_hit == GVR_EDIT_LIST_HIT_SEGMENT_IN ? x0 : x1;
            GdkRGBA edge_hot = selection_edge;
            edge_hot.alpha = 1.0;
            gvr_edit_list_set_source_rgba(cr, &edge_hot);
            cairo_set_line_width(cr, 4.0);
            cairo_move_to(cr, floor(edge_x) + 0.5, bar_y);
            cairo_line_to(cr, floor(edge_x) + 0.5, bar_y + bar_h);
            cairo_stroke(cr);
        }

        if(segment->index == view->selected_segment) {
            GdkRGBA outline = text;
            outline.alpha = 0.80;
            gvr_edit_list_set_source_rgba(cr, &outline);
            cairo_set_line_width(cr, 2.0);
            cairo_rectangle(cr, floor(x0) + 1.0, bar_y + 1.0,
                            MAX(1.0, floor(segment_width) - 2.0), bar_h - 2.0);
            cairo_stroke(cr);
        }

        if(segment_width >= 24.0) {
            PangoLayout *layout = gtk_widget_create_pango_layout(widget, NULL);
            PangoFontDescription *font =
                gvr_edit_list_font_description(widget,
                                               1.0,
                                               PANGO_WEIGHT_BOLD);
            gchar *base = g_path_get_basename(segment->filename ? segment->filename : "(missing)");
            gchar *label = segment_width < 52.0 ?
                           g_strdup_printf("#%d", segment->index + 1) : g_strdup(base);

            pango_layout_set_font_description(layout, font);
            pango_layout_set_text(layout, label, -1);
            pango_layout_set_width(layout, MAX(1, (int)(segment_width - 8.0)) * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
            pango_layout_set_single_paragraph_mode(layout, TRUE);
            gvr_edit_list_set_source_rgba(cr, &text);
            cairo_move_to(cr, x0 + 4.0, bar_y + 7.0);
            pango_cairo_show_layout(cr, layout);

            if(segment_width >= 88.0 && bar_h >= 40.0) {
                gchar *range = g_strdup_printf("file %d–%d", segment->file_in, segment->file_out);
                pango_font_description_set_weight(font, PANGO_WEIGHT_NORMAL);
                pango_font_description_set_size(
                    font,
                    (int)lrint(gvr_edit_list_font_points(widget) * 0.9 * PANGO_SCALE));
                pango_layout_set_font_description(layout, font);
                pango_layout_set_text(layout, range, -1);
                cairo_move_to(cr, x0 + 4.0, bar_y + 24.0);
                pango_cairo_show_layout(cr, layout);
                g_free(range);
            }
            g_free(label);
            g_free(base);
            pango_font_description_free(font);
            g_object_unref(layout);
        }
    }

    for(i = 0; i < view->regions->len; i++) {
        GvrEditListRegionData *region = g_ptr_array_index(view->regions, i);
        int region_start;
        int region_end;
        double rx0;
        double rx1;
        GdkRGBA region_color;
        PangoLayout *layout;
        PangoFontDescription *font;

        if(!gvr_edit_list_region_bounds(view, region, &region_start, &region_end))
            continue;
        if(region_end < visible_start || region_start > visible_end)
            continue;

        rx0 = CLAMP(gvr_edit_list_frame_boundary_to_x(view, MAX(region_start, visible_start), width), 0.0, width);
        rx1 = CLAMP(gvr_edit_list_frame_boundary_to_x(view, MIN(region_end, visible_end), width), 0.0, width);
        region_color = gvr_edit_list_file_palette[region->color_index % G_N_ELEMENTS(gvr_edit_list_file_palette)];
        region_color.alpha = 0.54;
        gvr_edit_list_set_source_rgba(cr, &region_color);
        cairo_rectangle(cr, rx0, gvr_edit_list_region_top(view),
                        MAX(1.0, rx1 - rx0), gvr_edit_list_region_height(view));
        cairo_fill(cr);

        if(rx1 - rx0 >= 28.0) {
            layout = gtk_widget_create_pango_layout(widget, region->name ? region->name : "Region");
            font = gvr_edit_list_font_description(widget,
                                                  0.8,
                                                  PANGO_WEIGHT_BOLD);
            pango_layout_set_font_description(layout, font);
            pango_layout_set_width(layout, MAX(1, (int)(rx1 - rx0 - 6.0)) * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
            gvr_edit_list_set_source_rgba(cr, &text);
            cairo_move_to(cr, rx0 + 3.0, gvr_edit_list_region_top(view) - 1.0);
            pango_cairo_show_layout(cr, layout);
            pango_font_description_free(font);
            g_object_unref(layout);
        }
    }

    for(i = 0; i < view->separators->len; i++) {
        GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, i);
        double x;
        GdkRGBA color;
        PangoLayout *layout;
        PangoFontDescription *font;

        if(separator->frame < visible_start || separator->frame > visible_end)
            continue;
        x = floor(gvr_edit_list_frame_boundary_to_x(view, separator->frame, width)) + 0.5;
        color = gvr_edit_list_file_palette[separator->color_index % G_N_ELEMENTS(gvr_edit_list_file_palette)];
        if(separator->id == view->selected_separator_id) {
            color.red = MIN(1.0, color.red + 0.22);
            color.green = MIN(1.0, color.green + 0.22);
            color.blue = MIN(1.0, color.blue + 0.22);
        }
        gvr_edit_list_set_source_rgba(cr, &color);
        cairo_move_to(cr, x - 6.0, 2.0);
        cairo_line_to(cr, x + 6.0, 2.0);
        cairo_line_to(cr, x, 10.0);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_set_line_width(cr, separator->id == view->selected_separator_id ? 2.0 : 1.0);
        cairo_move_to(cr, x, 10.0);
        cairo_line_to(cr, x, height);
        cairo_stroke(cr);

        if(separator->name && *separator->name) {
            layout = gtk_widget_create_pango_layout(widget, separator->name);
            font = gvr_edit_list_font_description(widget,
                                                  0.9,
                                                  PANGO_WEIGHT_NORMAL);
            pango_layout_set_font_description(layout, font);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
            pango_layout_set_width(layout, 120 * PANGO_SCALE);
            gvr_edit_list_set_source_rgba(cr, &text);
            cairo_move_to(cr, x + 7.0, 5.0);
            pango_cairo_show_layout(cr, layout);
            pango_font_description_free(font);
            g_object_unref(layout);
        }
    }

    if((view->drag_mode == GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_IN ||
        view->drag_mode == GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_OUT) &&
       view->drag_moved && view->drag_preview_out >= view->drag_preview_in) {
        double tx0 = CLAMP(gvr_edit_list_frame_boundary_to_x(view,
                            MAX(view->drag_preview_in, visible_start), width), 0.0, width);
        double tx1 = CLAMP(gvr_edit_list_frame_boundary_to_x(view,
                            MIN(view->drag_preview_out + 1, visible_end), width), 0.0, width);
        double edge_x = gvr_edit_list_frame_boundary_to_x(view, view->drag_trim_boundary, width);
        GdkRGBA remove_fill = { 0.92, 0.18, 0.20, 0.34 };
        GdkRGBA remove_edge = { 1.00, 0.46, 0.24, 1.00 };

        if(tx1 > tx0) {
            gvr_edit_list_set_source_rgba(cr, &remove_fill);
            cairo_rectangle(cr, tx0, bar_y, tx1 - tx0, bar_h);
            cairo_fill(cr);
            cairo_set_line_width(cr, 1.0);
            cairo_set_dash(cr, (double[]){ 4.0, 3.0 }, 2, 0.0);
            gvr_edit_list_set_source_rgba(cr, &remove_edge);
            cairo_rectangle(cr, floor(tx0) + 0.5, bar_y + 0.5,
                            MAX(1.0, floor(tx1 - tx0) - 1.0), bar_h - 1.0);
            cairo_stroke(cr);
            cairo_set_dash(cr, NULL, 0, 0.0);
        }

        gvr_edit_list_set_source_rgba(cr, &remove_edge);
        cairo_set_line_width(cr, 4.0);
        cairo_move_to(cr, floor(edge_x) + 0.5, bar_y);
        cairo_line_to(cr, floor(edge_x) + 0.5, bar_y + bar_h);
        cairo_stroke(cr);
    }

    if((gvr_edit_list_selection_valid(view) &&
        view->drag_mode != GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_IN &&
        view->drag_mode != GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_OUT) ||
       view->drag_mode == GVR_EDIT_LIST_DRAG_CREATE)
    {
        int draw_in = gvr_edit_list_selection_valid(view) ? view->selection_in : view->drag_preview_in;
        int draw_out = gvr_edit_list_selection_valid(view) ? view->selection_out : view->drag_preview_out;
        gboolean moving = view->drag_mode == GVR_EDIT_LIST_DRAG_MOVE_SELECTION && view->drag_moved;
        double sx0, sx1;
        GdkRGBA selection_fill = selection_fill_color;
        GdkRGBA selection_outline = selection_edge;

        if(moving) {
            double gx0 = gvr_edit_list_frame_boundary_to_x(view, view->selection_in, width);
            double gx1 = gvr_edit_list_frame_boundary_to_x(view, view->selection_out + 1, width);
            GdkRGBA ghost = muted;
            ghost.alpha = 0.24;
            gvr_edit_list_set_source_rgba(cr, &ghost);
            cairo_rectangle(cr, gx0, bar_y, MAX(1.0, gx1 - gx0), bar_h);
            cairo_fill(cr);
            draw_in = view->drag_preview_in;
            draw_out = view->drag_preview_out;
        } else if(view->drag_mode == GVR_EDIT_LIST_DRAG_RESIZE_IN ||
                  view->drag_mode == GVR_EDIT_LIST_DRAG_RESIZE_OUT ||
                  view->drag_mode == GVR_EDIT_LIST_DRAG_CREATE) {
            draw_in = view->drag_preview_in;
            draw_out = view->drag_preview_out;
        }

        if(draw_out >= visible_start && draw_in < visible_end) {
            sx0 = CLAMP(gvr_edit_list_frame_boundary_to_x(view, MAX(draw_in, visible_start), width), 0.0, width);
            sx1 = CLAMP(gvr_edit_list_frame_boundary_to_x(view, MIN(draw_out + 1, visible_end), width), 0.0, width);
            gboolean in_active = view->drag_mode == GVR_EDIT_LIST_DRAG_RESIZE_IN ||
                                 view->hover_hit == GVR_EDIT_LIST_HIT_SELECTION_IN;
            gboolean out_active = view->drag_mode == GVR_EDIT_LIST_DRAG_RESIZE_OUT ||
                                  view->hover_hit == GVR_EDIT_LIST_HIT_SELECTION_OUT;
            double handle_w = GVR_EDIT_LIST_HANDLE_DRAW_PX;
            double handle_h = MAX(18.0, bar_h - 8.0);
            double handle_y = bar_y + (bar_h - handle_h) * 0.5;
            GdkRGBA in_handle = selection_outline;
            GdkRGBA out_handle = selection_outline;
            GdkRGBA handle_notch = panel;

            selection_fill.alpha = moving ? 0.42 : 0.28;
            gvr_edit_list_set_source_rgba(cr, &selection_fill);
            cairo_rectangle(cr, sx0, bar_y, MAX(1.0, sx1 - sx0), bar_h);
            cairo_fill(cr);

            selection_outline.alpha = 0.98;
            gvr_edit_list_set_source_rgba(cr, &selection_outline);
            cairo_set_line_width(cr, 2.0);
            cairo_rectangle(cr, floor(sx0) + 1.0, bar_y + 1.0,
                            MAX(1.0, floor(sx1 - sx0) - 2.0), bar_h - 2.0);
            cairo_stroke(cr);

            if(in_active) {
                in_handle.red = MIN(1.0, in_handle.red + 0.18);
                in_handle.green = MIN(1.0, in_handle.green + 0.18);
                in_handle.blue = MIN(1.0, in_handle.blue + 0.18);
            }
            if(out_active) {
                out_handle.red = MIN(1.0, out_handle.red + 0.18);
                out_handle.green = MIN(1.0, out_handle.green + 0.18);
                out_handle.blue = MIN(1.0, out_handle.blue + 0.18);
            }

            handle_notch.alpha = 0.78;

            if(draw_in >= visible_start && draw_in <= visible_end) {
                gvr_edit_list_set_source_rgba(cr, &in_handle);
                cairo_rectangle(cr, sx0 - handle_w * 0.5, handle_y, handle_w, handle_h);
                cairo_fill(cr);
                cairo_move_to(cr, sx0 - handle_w * 0.5, handle_y);
                cairo_line_to(cr, sx0 + handle_w * 0.5, handle_y);
                cairo_line_to(cr, sx0, handle_y + 6.0);
                cairo_close_path(cr);
                cairo_fill(cr);

                gvr_edit_list_set_source_rgba(cr, &handle_notch);
                cairo_set_line_width(cr, 1.0);
                cairo_move_to(cr, sx0, handle_y + 8.0);
                cairo_line_to(cr, sx0, handle_y + handle_h - 5.0);
                cairo_stroke(cr);
            }

            if(draw_out + 1 >= visible_start && draw_out + 1 <= visible_end) {
                gvr_edit_list_set_source_rgba(cr, &out_handle);
                cairo_rectangle(cr, sx1 - handle_w * 0.5, handle_y, handle_w, handle_h);
                cairo_fill(cr);
                cairo_move_to(cr, sx1 - handle_w * 0.5, handle_y);
                cairo_line_to(cr, sx1 + handle_w * 0.5, handle_y);
                cairo_line_to(cr, sx1, handle_y + 6.0);
                cairo_close_path(cr);
                cairo_fill(cr);

                gvr_edit_list_set_source_rgba(cr, &handle_notch);
                cairo_set_line_width(cr, 1.0);
                cairo_move_to(cr, sx1, handle_y + 8.0);
                cairo_line_to(cr, sx1, handle_y + handle_h - 5.0);
                cairo_stroke(cr);
            }
        }
    }

    if(view->drag_mode == GVR_EDIT_LIST_DRAG_MOVE_SELECTION && view->drag_moved) {
        double x = floor(gvr_edit_list_frame_boundary_to_x(view, view->drag_destination, width)) + 0.5;
        GdkRGBA insertion = playing;
        insertion.alpha = 0.95;
        gvr_edit_list_set_source_rgba(cr, &insertion);
        cairo_set_line_width(cr, 3.0);
        cairo_move_to(cr, x, gvr_edit_list_separator_height(view));
        cairo_line_to(cr, x, height);
        cairo_stroke(cr);
    }

    if(view->snap_visible) {
        double x = floor(gvr_edit_list_frame_boundary_to_x(view, view->snap_frame, width)) + 0.5;
        GdkRGBA snap = text;
        snap.alpha = 0.72;
        gvr_edit_list_set_source_rgba(cr, &snap);
        cairo_set_line_width(cr, 1.0);
        cairo_set_dash(cr, (double[]){ 3.0, 3.0 }, 2, 0.0);
        cairo_move_to(cr, x, gvr_edit_list_separator_height(view));
        cairo_line_to(cr, x, height);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0.0);
    }

    if(view->total_frames > 0 && gvr_edit_list_frame_visible(view, view->playhead)) {
        double play_x = floor(gvr_edit_list_frame_to_x(view, view->playhead, width)) + 0.5;
        GdkRGBA shadow = playing;
        shadow.alpha = 0.24;
        gvr_edit_list_set_source_rgba(cr, &shadow);
        cairo_set_line_width(cr, 6.0);
        cairo_move_to(cr, play_x, gvr_edit_list_separator_height(view));
        cairo_line_to(cr, play_x, height);
        cairo_stroke(cr);
        gvr_edit_list_set_source_rgba(cr, &playing);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, play_x, gvr_edit_list_separator_height(view));
        cairo_line_to(cr, play_x, height);
        cairo_stroke(cr);
    }

    gtk_render_frame(context, cr, 0, bar_y, width, bar_h);
    if(gtk_widget_has_focus(widget)) {
        GdkRGBA focus = selection_edge;
        focus.alpha = 0.82;
        gvr_edit_list_set_source_rgba(cr, &focus);
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, 1.0, 1.0, MAX(1.0, width - 2.0), MAX(1.0, height - 2.0));
        cairo_stroke(cr);
    }
    return FALSE;
}

static double gvr_edit_list_navigator_boundary_x(GvrEditListView *view,
                                                   int boundary,
                                                   double width)
{
    return ((double)gvr_edit_list_clampi(boundary, 0, MAX(0, view->total_frames)) /
            MAX(1.0, (double)view->total_frames)) * width;
}

static int gvr_edit_list_navigator_x_to_boundary(GvrEditListView *view,
                                                  double x,
                                                  double width)
{
    if(width <= 1.0 || view->total_frames <= 0)
        return 0;
    return gvr_edit_list_clampi((int)floor((CLAMP(x, 0.0, width) / width) *
                                          view->total_frames + 0.5),
                                0, view->total_frames);
}

static gboolean gvr_edit_list_navigator_draw(GtkWidget *widget,
                                              cairo_t *cr,
                                              gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkAllocation allocation;
    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    const GdkRGBA fallback_panel = { 0.135, 0.140, 0.165, 1.0 };
    const GdkRGBA fallback_border = { 0.300, 0.320, 0.380, 1.0 };
    const GdkRGBA fallback_selection = { 0.055, 0.690, 0.790, 0.58 };
    const GdkRGBA fallback_playing = { 1.0, 0.518, 0.0, 1.0 };
    GdkRGBA panel, border, selection, playing;
    double width;
    double height;
    guint i;

    gtk_widget_get_allocation(widget, &allocation);
    width = allocation.width;
    height = allocation.height;
    gtk_render_background(context, cr, 0, 0, width, height);
    gvr_edit_list_lookup_color(widget, "panel-color", &fallback_panel, &panel);
    gvr_edit_list_lookup_color(widget, "border-color", &fallback_border, &border);
    gvr_edit_list_lookup_color(widget, "edit-selection-edge-color", &fallback_selection, &selection);
    gvr_edit_list_lookup_color(widget, "slot-playing-color", &fallback_playing, &playing);

    gvr_edit_list_set_source_rgba(cr, &panel);
    cairo_rectangle(cr, 0.0, 0.0, width, height);
    cairo_fill(cr);

    if(view->total_frames <= 0) {
        gtk_render_frame(context, cr, 0, 0, width, height);
        return FALSE;
    }

    for(i = 0; i < view->segments->len; i++) {
        GvrEditListSegmentData *segment = g_ptr_array_index(view->segments, i);
        double x0 = gvr_edit_list_navigator_boundary_x(view, segment->timeline_in, width);
        double x1 = gvr_edit_list_navigator_boundary_x(view, segment->timeline_out + 1, width);
        GdkRGBA fill = gvr_edit_list_file_color(segment, FALSE);
        fill.alpha = 0.88;
        gvr_edit_list_set_source_rgba(cr, &fill);
        cairo_rectangle(cr, x0, 3.0, MAX(1.0, x1 - x0), height - 6.0);
        cairo_fill(cr);
    }

    if(gvr_edit_list_selection_valid(view)) {
        double x0 = gvr_edit_list_navigator_boundary_x(view, view->selection_in, width);
        double x1 = gvr_edit_list_navigator_boundary_x(view, view->selection_out + 1, width);
        selection.alpha = 0.60;
        gvr_edit_list_set_source_rgba(cr, &selection);
        cairo_rectangle(cr, x0, 2.0, MAX(1.0, x1 - x0), height - 4.0);
        cairo_fill(cr);
    }

    for(i = 0; i < view->separators->len; i++) {
        GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, i);
        double x = floor(gvr_edit_list_navigator_boundary_x(view, separator->frame, width)) + 0.5;
        GdkRGBA color = gvr_edit_list_file_palette[separator->color_index % G_N_ELEMENTS(gvr_edit_list_file_palette)];
        color.alpha = 0.92;
        gvr_edit_list_set_source_rgba(cr, &color);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, x, 1.0);
        cairo_line_to(cr, x, height - 1.0);
        cairo_stroke(cr);
    }

    {
        int visible = gvr_edit_list_visible_frames(view);
        double vx0 = gvr_edit_list_navigator_boundary_x(view, view->timeline_view_start, width);
        double vx1 = gvr_edit_list_navigator_boundary_x(view,
                                                        MIN(view->total_frames,
                                                            view->timeline_view_start + visible), width);
        GdkRGBA shade = panel;
        shade.alpha = 0.52;
        gvr_edit_list_set_source_rgba(cr, &shade);
        cairo_rectangle(cr, 0.0, 0.0, vx0, height);
        cairo_fill(cr);
        cairo_rectangle(cr, vx1, 0.0, MAX(0.0, width - vx1), height);
        cairo_fill(cr);
        border.alpha = 0.95;
        gvr_edit_list_set_source_rgba(cr, &border);
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, floor(vx0) + 1.0, 1.0,
                        MAX(2.0, floor(vx1 - vx0) - 2.0), height - 2.0);
        cairo_stroke(cr);
    }

    {
        double x = floor(gvr_edit_list_navigator_boundary_x(view, view->playhead, width)) + 0.5;
        gvr_edit_list_set_source_rgba(cr, &playing);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, x, 0.0);
        cairo_line_to(cr, x, height);
        cairo_stroke(cr);
    }

    gtk_render_frame(context, cr, 0, 0, width, height);
    return FALSE;
}

static gboolean gvr_edit_list_navigator_button_press(GtkWidget *widget,
                                                      GdkEventButton *event,
                                                      gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkAllocation allocation;
    int frame;
    int visible;
    int viewport_end;

    if((event->button != 1 && event->button != 2) || view->total_frames <= 0)
        return FALSE;

    gvr_edit_list_take_focus(view);
    gtk_widget_get_allocation(widget, &allocation);
    frame = gvr_edit_list_navigator_x_to_boundary(view, event->x, allocation.width);
    visible = gvr_edit_list_visible_frames(view);
    viewport_end = view->timeline_view_start + visible;
    view->drag_mode = GVR_EDIT_LIST_DRAG_PAN_NAVIGATOR;
    view->drag_pan_origin_start = view->timeline_view_start;
    view->drag_press_x = event->x;
    view->drag_moved = FALSE;

    if(frame >= view->timeline_view_start && frame <= viewport_end)
        view->drag_pan_anchor_frames = frame - view->timeline_view_start;
    else {
        view->drag_pan_anchor_frames = visible / 2;
        view->timeline_view_start = gvr_edit_list_clampi(frame - view->drag_pan_anchor_frames,
                                                        0, gvr_edit_list_view_max_start(view));
        gvr_edit_list_sync_timeline_controls(view);
        gvr_edit_list_queue_timeline_draw(view);
    }

    return TRUE;
}

static gboolean gvr_edit_list_navigator_motion(GtkWidget *widget,
                                                GdkEventMotion *event,
                                                gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkAllocation allocation;
    int frame;

    if(view->drag_mode != GVR_EDIT_LIST_DRAG_PAN_NAVIGATOR)
        return FALSE;

    gtk_widget_get_allocation(widget, &allocation);
    frame = gvr_edit_list_navigator_x_to_boundary(view, event->x, allocation.width);
    view->drag_moved = TRUE;
    view->timeline_view_start = gvr_edit_list_clampi(frame - view->drag_pan_anchor_frames,
                                                    0, gvr_edit_list_view_max_start(view));
    gvr_edit_list_sync_timeline_controls(view);
    gvr_edit_list_queue_timeline_draw(view);
    return TRUE;
}

static gboolean gvr_edit_list_navigator_button_release(GtkWidget *widget,
                                                        GdkEventButton *event,
                                                        gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    (void)widget;

    if((event->button != 1 && event->button != 2) ||
       view->drag_mode != GVR_EDIT_LIST_DRAG_PAN_NAVIGATOR)
        return FALSE;

    view->drag_mode = GVR_EDIT_LIST_DRAG_NONE;
    view->drag_moved = FALSE;
    gvr_edit_list_take_focus(view);
    gvr_edit_list_queue_timeline_draw(view);
    return TRUE;
}

static gboolean gvr_edit_list_navigator_query_tooltip(GtkWidget *widget,
                                                       gint x,
                                                       gint y,
                                                       gboolean keyboard_mode,
                                                       GtkTooltip *tooltip,
                                                       gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkAllocation allocation;
    int frame;
    gchar *time;
    gchar *text;
    (void)y;

    if(keyboard_mode || view->total_frames <= 0)
        return FALSE;

    gtk_widget_get_allocation(widget, &allocation);
    frame = MIN(gvr_edit_list_frame_max(view),
                gvr_edit_list_navigator_x_to_boundary(view, x, allocation.width));
    time = gvr_edit_list_format_time(view, frame);
    text = g_strdup_printf("Timeline overview\nFrame %d · %s\nDrag the viewport to pan", frame, time);
    gtk_tooltip_set_text(tooltip, text);
    g_free(text);
    g_free(time);
    return TRUE;
}

static void gvr_edit_list_update_playing_rows(GvrEditListView *view)
{
    GtkTreeModel *model = GTK_TREE_MODEL(view->store);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);

    while(valid) {
        int in_frame;
        int out_frame;
        gboolean playing;

        gtk_tree_model_get(model,
                           &iter,
                           MODEL_TIMELINE_IN, &in_frame,
                           MODEL_TIMELINE_OUT, &out_frame,
                           -1);
        playing = view->playhead >= in_frame && view->playhead <= out_frame;
        gtk_list_store_set(view->store,
                           &iter,
                           MODEL_PLAYING, playing,
                           -1);
        valid = gtk_tree_model_iter_next(model, &iter);
    }
}

static void gvr_edit_list_emit_seek(GvrEditListView *view, int frame)
{
    frame = gvr_edit_list_clamp_frame(view, frame);
    gvr_edit_list_view_set_playhead(GTK_WIDGET(view), frame);
    g_signal_emit(view,
                  gvr_edit_list_view_signals[SIGNAL_SEEK_REQUESTED],
                  0,
                  frame);
}

static void gvr_edit_list_overview_autoscroll(GvrEditListView *view,
                                               double x,
                                               double width)
{
    int visible;
    int delta = 0;

    if(view->timeline_zoom <= 1.001 || width <= 1.0)
        return;

    visible = gvr_edit_list_visible_frames(view);
    if(x < GVR_EDIT_LIST_AUTOSCROLL_PX)
        delta = -MAX(1, visible / 40);
    else if(x > width - GVR_EDIT_LIST_AUTOSCROLL_PX)
        delta = MAX(1, visible / 40);

    if(delta != 0) {
        view->timeline_view_start = gvr_edit_list_clampi(view->timeline_view_start + delta,
                                                        0,
                                                        gvr_edit_list_view_max_start(view));
        gvr_edit_list_sync_timeline_controls(view);
    }
}

static void gvr_edit_list_separator_menu_rename(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    (void)widget;
    gvr_edit_list_rename_separator(view, view->selected_separator_id);
}

static void gvr_edit_list_separator_menu_add(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    int frame = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "gvr-separator-frame"));
    gvr_edit_list_add_separator(view, frame, NULL, TRUE);
}

static void gvr_edit_list_separator_menu_remove(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    (void)widget;
    gvr_edit_list_remove_separator(view, view->selected_separator_id, TRUE);
}

static void gvr_edit_list_take_focus(GvrEditListView *view)
{
    GtkWidget *toplevel;

    if(!view || !view->overview)
        return;

    gtk_widget_grab_focus(view->overview);
    toplevel = gtk_widget_get_toplevel(view->overview);
    if(GTK_IS_WINDOW(toplevel))
        gtk_window_set_focus(GTK_WINDOW(toplevel), view->overview);
}

static gboolean gvr_edit_list_overview_focus_event(GtkWidget *widget,
                                                    GdkEventFocus *event,
                                                    gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);

    (void)widget;
    (void)event;
    gvr_edit_list_queue_timeline_draw(view);
    return FALSE;
}

static gboolean gvr_edit_list_overview_leave(GtkWidget *widget,
                                              GdkEventCrossing *event,
                                              gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);

    (void)widget;
    (void)event;
    if(view->drag_mode == GVR_EDIT_LIST_DRAG_NONE) {
        view->hover_hit = GVR_EDIT_LIST_HIT_NONE;
        gvr_edit_list_update_cursor(view, GVR_EDIT_LIST_HIT_NONE);
        gvr_edit_list_queue_timeline_draw(view);
    }
    return FALSE;
}

static int gvr_edit_list_menu_frame(GtkWidget *widget)
{
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "gvr-region-frame"));
}

static void gvr_edit_list_region_menu_name(GtkWidget *widget, gpointer user_data)
{
    gvr_edit_list_name_region(GVR_EDIT_LIST_VIEW(user_data),
                              gvr_edit_list_menu_frame(widget));
}

static void gvr_edit_list_region_menu_select(GtkWidget *widget, gpointer user_data)
{
    gvr_edit_list_select_region(GVR_EDIT_LIST_VIEW(user_data),
                                gvr_edit_list_menu_frame(widget), FALSE);
}

static void gvr_edit_list_region_menu_zoom(GtkWidget *widget, gpointer user_data)
{
    gvr_edit_list_select_region(GVR_EDIT_LIST_VIEW(user_data),
                                gvr_edit_list_menu_frame(widget), TRUE);
}

static void gvr_edit_list_region_menu_clear(GtkWidget *widget, gpointer user_data)
{
    gvr_edit_list_clear_region(GVR_EDIT_LIST_VIEW(user_data),
                               gvr_edit_list_menu_frame(widget));
}

static void gvr_edit_list_context_seek_here(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    gvr_edit_list_emit_seek(view, gvr_edit_list_menu_frame(widget));
}

static void gvr_edit_list_context_select_segment(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    guint segment_pos = 0;

    if(gvr_edit_list_segment_at_frame(view,
                                      gvr_edit_list_menu_frame(widget),
                                      &segment_pos))
        gvr_edit_list_select_segment_pos(view, segment_pos, FALSE, FALSE);
}

static void gvr_edit_list_context_zoom_segment(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    guint segment_pos = 0;

    if(gvr_edit_list_segment_at_frame(view,
                                      gvr_edit_list_menu_frame(widget),
                                      &segment_pos))
        gvr_edit_list_select_segment_pos(view, segment_pos, FALSE, TRUE);
}

static void gvr_edit_list_context_paste_here(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    gvr_edit_list_paste_emit(view, gvr_edit_list_menu_frame(widget));
}

static void gvr_edit_list_context_clear_selection(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);

    (void)widget;
    gvr_edit_list_set_selection_internal(view, 0, 0, FALSE, TRUE);
}

static gboolean gvr_edit_list_overview_context_menu(GvrEditListView *view,
                                                     GdkEventButton *event,
                                                     int frame,
                                                     GvrEditListSeparatorData *separator)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item;
    GvrEditListSeparatorData *region_start = NULL;
    GvrEditListSeparatorData *region_end = NULL;
    GvrEditListRegionData *region = NULL;
    GvrEditListSegmentData *segment = NULL;
    gboolean has_region_pair;
    gboolean selection = gvr_edit_list_selection_valid(view);

    segment = gvr_edit_list_segment_at_frame(view, frame, NULL);
    view->selected_separator_id = separator ? separator->id : 0;
    gtk_widget_set_sensitive(view->separator_remove_button,
                             view->editable && view->selected_separator_id != 0);

    item = gtk_menu_item_new_with_label("Seek Here");
    g_object_set_data(G_OBJECT(item), "gvr-region-frame", GINT_TO_POINTER(frame));
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_context_seek_here), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Select Segment");
    g_object_set_data(G_OBJECT(item), "gvr-region-frame", GINT_TO_POINTER(frame));
    gtk_widget_set_sensitive(item, segment != NULL);
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_context_select_segment), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Zoom to Segment");
    g_object_set_data(G_OBJECT(item), "gvr-region-frame", GINT_TO_POINTER(frame));
    gtk_widget_set_sensitive(item, segment != NULL);
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_context_zoom_segment), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = gvr_edit_list_action_menu_item(menu, "Cut Selection", NULL,
                                          GVR_EDIT_LIST_ACTION_CUT, view);
    gtk_widget_set_sensitive(item, view->editable && selection);

    item = gvr_edit_list_action_menu_item(menu, "Copy Selection", NULL,
                                          GVR_EDIT_LIST_ACTION_COPY, view);
    gtk_widget_set_sensitive(item, view->editable && selection);

    item = gvr_edit_list_action_menu_item(menu, "Delete Selection", NULL,
                                          GVR_EDIT_LIST_ACTION_DELETE, view);
    gtk_widget_set_sensitive(item, view->editable && selection);

    item = gvr_edit_list_action_menu_item(menu, "Crop to Selection", NULL,
                                          GVR_EDIT_LIST_ACTION_CROP, view);
    gtk_widget_set_sensitive(item, view->editable && selection &&
                                   !(view->selection_in == 0 &&
                                     view->selection_out == gvr_edit_list_frame_max(view)));

    item = gtk_menu_item_new_with_label("Paste Here");
    g_object_set_data(G_OBJECT(item), "gvr-region-frame", GINT_TO_POINTER(frame));
    gtk_widget_set_sensitive(item, view->editable && view->clipboard_valid);
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_context_paste_here), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Clear Selection");
    gtk_widget_set_sensitive(item, selection);
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_context_clear_selection), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = gtk_menu_item_new_with_label("Add Separator Here");
    g_object_set_data(G_OBJECT(item), "gvr-separator-frame", GINT_TO_POINTER(frame));
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_separator_menu_add), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Rename Separator…");
    gtk_widget_set_sensitive(item, view->selected_separator_id != 0);
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_separator_menu_rename), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Delete Separator");
    gtk_widget_set_sensitive(item, view->selected_separator_id != 0);
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_separator_menu_remove), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    has_region_pair = gvr_edit_list_region_pair_at_frame(view, frame,
                                                          &region_start,
                                                          &region_end);
    if(has_region_pair)
        region = gvr_edit_list_region_by_pair(view, region_start->id, region_end->id, NULL);

    item = gtk_menu_item_new_with_label(region ? "Rename Region…" : "Name Region…");
    g_object_set_data(G_OBJECT(item), "gvr-region-frame", GINT_TO_POINTER(frame));
    gtk_widget_set_sensitive(item, has_region_pair);
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_region_menu_name), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Select Region");
    g_object_set_data(G_OBJECT(item), "gvr-region-frame", GINT_TO_POINTER(frame));
    gtk_widget_set_sensitive(item, has_region_pair);
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_region_menu_select), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Zoom to Region");
    g_object_set_data(G_OBJECT(item), "gvr-region-frame", GINT_TO_POINTER(frame));
    gtk_widget_set_sensitive(item, has_region_pair);
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_region_menu_zoom), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Clear Region Name");
    g_object_set_data(G_OBJECT(item), "gvr-region-frame", GINT_TO_POINTER(frame));
    gtk_widget_set_sensitive(item, region != NULL);
    g_signal_connect(item, "activate", G_CALLBACK(gvr_edit_list_region_menu_clear), view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}

static gboolean gvr_edit_list_overview_button_press(GtkWidget *widget,
                                                    GdkEventButton *event,
                                                    gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkAllocation allocation;
    GvrEditListHit hit;
    GvrEditListSeparatorData *separator;
    int frame;
    int boundary;

    gvr_edit_list_take_focus(view);

    if(view->total_frames <= 0)
        return TRUE;

    gtk_widget_get_allocation(widget, &allocation);
    hit = gvr_edit_list_hit_test(view, event->x, event->y, allocation.width);
    view->hover_hit = hit;
    gvr_edit_list_update_cursor(view, hit);
    frame = gvr_edit_list_x_to_frame(view, event->x, allocation.width);
    boundary = gvr_edit_list_x_to_boundary(view, event->x, allocation.width);
    separator = event->y < gvr_edit_list_separator_height(view) ?
                gvr_edit_list_separator_at_x(view, event->x, allocation.width) : NULL;

    if(event->button == 3)
        return gvr_edit_list_overview_context_menu(view, event, frame, separator);

    view->drag_mode = GVR_EDIT_LIST_DRAG_NONE;
    view->drag_moved = FALSE;
    view->drag_copy = FALSE;
    view->snap_visible = FALSE;
    view->drag_press_x = event->x;
    view->drag_press_y = event->y;
    view->drag_original_in = view->selection_in;
    view->drag_original_out = view->selection_out;
    view->drag_preview_in = view->selection_in;
    view->drag_preview_out = view->selection_out;

    if(event->button == 2) {
        view->drag_mode = GVR_EDIT_LIST_DRAG_PAN_TIMELINE;
        view->drag_pan_origin_start = view->timeline_view_start;
        gvr_edit_list_update_cursor(view, GVR_EDIT_LIST_HIT_SELECTION_BODY);
        return TRUE;
    }

    if(event->button != 1)
        return FALSE;

    if(event->type == GDK_2BUTTON_PRESS) {
        if(separator) {
            view->selected_separator_id = separator->id;
            gvr_edit_list_rename_separator(view, separator->id);
            return TRUE;
        }
        if(hit == GVR_EDIT_LIST_HIT_REGION) {
            gvr_edit_list_name_region(view, frame);
            return TRUE;
        }
        if(event->y >= gvr_edit_list_clip_top(view)) {
            guint segment_pos = 0;
            GvrEditListSegmentData *segment = gvr_edit_list_segment_at_frame(view, frame, &segment_pos);
            if(segment) {
                gboolean extend = (event->state & GDK_SHIFT_MASK) != 0;
                gboolean zoom = (event->state & GDK_CONTROL_MASK) != 0;
                gvr_edit_list_select_segment_pos(view, segment_pos, extend, zoom);
                return TRUE;
            }
        }
    }

    if(hit != GVR_EDIT_LIST_HIT_SEPARATOR) {
        view->selected_separator_id = 0;
        gtk_widget_set_sensitive(view->separator_remove_button, FALSE);
    }

    switch(hit) {
        case GVR_EDIT_LIST_HIT_SEPARATOR:
            if(separator) {
                view->selected_separator_id = separator->id;
                view->drag_separator_id = separator->id;
                view->drag_separator_origin = separator->frame;
                view->drag_mode = GVR_EDIT_LIST_DRAG_MOVE_SEPARATOR;
                gtk_widget_set_sensitive(view->separator_remove_button, TRUE);
                gvr_edit_list_queue_timeline_draw(view);
            }
            break;

        case GVR_EDIT_LIST_HIT_REGION:
            if(!gvr_edit_list_select_region(view, frame, FALSE))
                gvr_edit_list_emit_seek(view, frame);
            return TRUE;

        case GVR_EDIT_LIST_HIT_RULER:
            view->drag_mode = GVR_EDIT_LIST_DRAG_SCRUB;
            gvr_edit_list_emit_seek(view, frame);
            break;

        case GVR_EDIT_LIST_HIT_SELECTION_IN:
            if(view->editable) {
                view->drag_mode = GVR_EDIT_LIST_DRAG_RESIZE_IN;
                view->drag_anchor_frame = view->selection_out;
            }
            break;

        case GVR_EDIT_LIST_HIT_SELECTION_OUT:
            if(view->editable) {
                view->drag_mode = GVR_EDIT_LIST_DRAG_RESIZE_OUT;
                view->drag_anchor_frame = view->selection_in;
            }
            break;

        case GVR_EDIT_LIST_HIT_SELECTION_BODY:
            if(view->editable) {
                view->drag_mode = GVR_EDIT_LIST_DRAG_MOVE_SELECTION;
                view->drag_anchor_frame = boundary - view->selection_in;
                view->drag_destination = view->selection_in;
            }
            break;

        case GVR_EDIT_LIST_HIT_SEGMENT_IN:
        case GVR_EDIT_LIST_HIT_SEGMENT_OUT:
            if(view->editable && view->hover_segment_pos >= 0 &&
               (guint)view->hover_segment_pos < view->segments->len) {
                GvrEditListSegmentData *segment =
                    g_ptr_array_index(view->segments, (guint)view->hover_segment_pos);
                view->drag_segment_pos = view->hover_segment_pos;
                view->drag_segment_in = segment->timeline_in;
                view->drag_segment_out = segment->timeline_out;
                view->drag_trim_boundary = hit == GVR_EDIT_LIST_HIT_SEGMENT_IN ?
                                           segment->timeline_in : segment->timeline_out + 1;
                view->drag_preview_in = 0;
                view->drag_preview_out = -1;
                view->drag_mode = hit == GVR_EDIT_LIST_HIT_SEGMENT_IN ?
                                  GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_IN :
                                  GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_OUT;
            }
            break;

        case GVR_EDIT_LIST_HIT_CLIP:
            if(view->editable) {
                view->drag_mode = GVR_EDIT_LIST_DRAG_CREATE;
                view->drag_anchor_frame = frame;
                view->drag_preview_in = frame;
                view->drag_preview_out = frame;
            } else {
                gvr_edit_list_emit_seek(view, frame);
            }
            break;

        default:
            break;
    }

    if(view->drag_mode != GVR_EDIT_LIST_DRAG_NONE)
        gvr_edit_list_queue_timeline_draw(view);

    return view->drag_mode != GVR_EDIT_LIST_DRAG_NONE;
}

static gboolean gvr_edit_list_overview_motion(GtkWidget *widget,
                                              GdkEventMotion *event,
                                              gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkAllocation allocation;
    int frame;
    int boundary;

    gtk_widget_get_allocation(widget, &allocation);
    view->hover_frame = gvr_edit_list_x_to_frame(view, event->x, allocation.width);

    if(view->drag_mode == GVR_EDIT_LIST_DRAG_NONE) {
        GvrEditListHit previous_hit = view->hover_hit;
        int previous_segment = view->hover_segment_pos;
        view->hover_hit = gvr_edit_list_hit_test(view, event->x, event->y, allocation.width);
        gvr_edit_list_update_cursor(view, view->hover_hit);
        if(previous_hit != view->hover_hit || previous_segment != view->hover_segment_pos)
            gvr_edit_list_queue_timeline_draw(view);
        return FALSE;
    }

    if(!view->drag_moved &&
       fabs(event->x - view->drag_press_x) < 3.0 &&
       fabs(event->y - view->drag_press_y) < 3.0)
        return TRUE;

    view->drag_moved = TRUE;

    if(view->drag_mode == GVR_EDIT_LIST_DRAG_PAN_TIMELINE) {
        double px_per_frame = gvr_edit_list_pixels_per_frame(view, allocation.width);
        int delta = px_per_frame > 0.0 ?
                    (int)floor((view->drag_press_x - event->x) / px_per_frame +
                               ((view->drag_press_x - event->x) >= 0.0 ? 0.5 : -0.5)) : 0;
        view->timeline_view_start = gvr_edit_list_clampi(view->drag_pan_origin_start + delta,
                                                        0,
                                                        gvr_edit_list_view_max_start(view));
        gvr_edit_list_sync_timeline_controls(view);
        gvr_edit_list_queue_timeline_draw(view);
        return TRUE;
    }

    gvr_edit_list_overview_autoscroll(view, event->x, allocation.width);
    frame = gvr_edit_list_x_to_frame(view, event->x, allocation.width);
    boundary = gvr_edit_list_x_to_boundary(view, event->x, allocation.width);

    switch(view->drag_mode) {
        case GVR_EDIT_LIST_DRAG_SCRUB:
            gvr_edit_list_emit_seek(view, frame);
            break;

        case GVR_EDIT_LIST_DRAG_CREATE: {
            int snapped = gvr_edit_list_snap_boundary(view, boundary,
                                                      allocation.width,
                                                      event->state, -1, -1);
            int current = gvr_edit_list_clamp_frame(view,
                                                    snapped >= view->total_frames ?
                                                    view->total_frames - 1 : snapped);
            view->drag_preview_in = MIN(view->drag_anchor_frame, current);
            view->drag_preview_out = MAX(view->drag_anchor_frame, current);
            gvr_edit_list_update_drag_summary(view, "Select",
                                              view->drag_preview_in, view->drag_preview_out, -1);
            gvr_edit_list_queue_timeline_draw(view);
            break;
        }

        case GVR_EDIT_LIST_DRAG_RESIZE_IN: {
            int snapped = gvr_edit_list_snap_boundary(view, boundary,
                                                      allocation.width,
                                                      event->state,
                                                      view->drag_original_out + 1, -1);
            view->drag_preview_in = gvr_edit_list_clampi(snapped,
                                                        0,
                                                        view->drag_original_out);
            view->drag_preview_out = view->drag_original_out;
            gvr_edit_list_update_drag_summary(view, "Trim In",
                                              view->drag_preview_in, view->drag_preview_out, -1);
            gvr_edit_list_queue_timeline_draw(view);
            break;
        }

        case GVR_EDIT_LIST_DRAG_RESIZE_OUT: {
            int snapped = gvr_edit_list_snap_boundary(view, boundary,
                                                      allocation.width,
                                                      event->state,
                                                      view->drag_original_in, -1);
            int out_boundary = gvr_edit_list_clampi(snapped,
                                                    view->drag_original_in + 1,
                                                    view->total_frames);
            view->drag_preview_in = view->drag_original_in;
            view->drag_preview_out = out_boundary - 1;
            gvr_edit_list_update_drag_summary(view, "Trim Out",
                                              view->drag_preview_in, view->drag_preview_out, -1);
            gvr_edit_list_queue_timeline_draw(view);
            break;
        }

        case GVR_EDIT_LIST_DRAG_MOVE_SELECTION: {
            int span = view->drag_original_out - view->drag_original_in + 1;
            int proposed = boundary - view->drag_anchor_frame;
            int max_start;
            int final_in;

            view->drag_copy = (event->state & GDK_CONTROL_MASK) != 0;
            max_start = view->drag_copy ? view->total_frames :
                        MAX(0, view->total_frames - span);
            proposed = gvr_edit_list_clampi(proposed, 0, max_start);
            proposed = gvr_edit_list_snap_boundary(view, proposed,
                                                   allocation.width,
                                                   event->state,
                                                   view->drag_original_in,
                                                   view->drag_original_out + 1);
            final_in = gvr_edit_list_clampi(proposed, 0, max_start);
            view->drag_preview_in = final_in;
            view->drag_preview_out = final_in + span - 1;
            if(view->drag_copy)
                view->drag_destination = final_in;
            else if(final_in < view->drag_original_in)
                view->drag_destination = final_in;
            else if(final_in > view->drag_original_in)
                view->drag_destination = final_in + span;
            else
                view->drag_destination = view->drag_original_in;
            gvr_edit_list_update_drag_summary(view,
                                              view->drag_copy ? "Copy" : "Move",
                                              view->drag_preview_in, view->drag_preview_out,
                                              view->drag_destination);
            gvr_edit_list_queue_timeline_draw(view);
            break;
        }

        case GVR_EDIT_LIST_DRAG_MOVE_SEPARATOR: {
            int snapped = gvr_edit_list_snap_boundary(view, boundary,
                                                      allocation.width,
                                                      event->state, -1, -1);
            gvr_edit_list_move_separator(view, view->drag_separator_id, snapped, FALSE);
            break;
        }

        case GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_IN: {
            int snapped = gvr_edit_list_snap_boundary(view, boundary,
                                                      allocation.width,
                                                      event->state,
                                                      view->drag_segment_in,
                                                      view->drag_segment_out + 1);
            int new_in = gvr_edit_list_clampi(snapped,
                                              view->drag_segment_in,
                                              view->drag_segment_out);
            view->drag_trim_boundary = new_in;
            view->drag_preview_in = view->drag_segment_in;
            view->drag_preview_out = new_in - 1;
            if(view->drag_preview_out >= view->drag_preview_in)
                gvr_edit_list_update_drag_summary(view, "Remove Head",
                                                  view->drag_preview_in,
                                                  view->drag_preview_out, -1);
            else
                gvr_edit_list_update_summary(view);
            gvr_edit_list_queue_timeline_draw(view);
            break;
        }

        case GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_OUT: {
            int snapped = gvr_edit_list_snap_boundary(view, boundary,
                                                      allocation.width,
                                                      event->state,
                                                      view->drag_segment_in,
                                                      view->drag_segment_out + 1);
            int new_out_boundary = gvr_edit_list_clampi(snapped,
                                                        view->drag_segment_in + 1,
                                                        view->drag_segment_out + 1);
            view->drag_trim_boundary = new_out_boundary;
            view->drag_preview_in = new_out_boundary;
            view->drag_preview_out = view->drag_segment_out;
            if(view->drag_preview_out >= view->drag_preview_in)
                gvr_edit_list_update_drag_summary(view, "Remove Tail",
                                                  view->drag_preview_in,
                                                  view->drag_preview_out, -1);
            else
                gvr_edit_list_update_summary(view);
            gvr_edit_list_queue_timeline_draw(view);
            break;
        }

        default:
            break;
    }

    return TRUE;
}

static gboolean gvr_edit_list_overview_button_release(GtkWidget *widget,
                                                      GdkEventButton *event,
                                                      gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GvrEditListDragMode mode = view->drag_mode;
    gboolean middle_pan = mode == GVR_EDIT_LIST_DRAG_PAN_TIMELINE;

    (void)widget;

    if(mode == GVR_EDIT_LIST_DRAG_NONE)
        return FALSE;
    if((middle_pan && event->button != 2) || (!middle_pan && event->button != 1))
        return FALSE;

    switch(mode) {
        case GVR_EDIT_LIST_DRAG_CREATE:
        case GVR_EDIT_LIST_DRAG_RESIZE_IN:
        case GVR_EDIT_LIST_DRAG_RESIZE_OUT:
            if(view->drag_moved)
                gvr_edit_list_set_selection_internal(view,
                                                     view->drag_preview_in,
                                                     view->drag_preview_out,
                                                     TRUE,
                                                     TRUE);
            else if(mode == GVR_EDIT_LIST_DRAG_CREATE)
                gvr_edit_list_emit_seek(view, view->drag_anchor_frame);
            break;

        case GVR_EDIT_LIST_DRAG_MOVE_SELECTION:
            if(view->drag_moved &&
               (view->drag_preview_in != view->drag_original_in || view->drag_copy)) {
                gvr_edit_list_emit_action(view,
                                          view->drag_copy ?
                                          GVR_EDIT_LIST_ACTION_COPY_RANGE_TO :
                                          GVR_EDIT_LIST_ACTION_MOVE_RANGE,
                                          view->drag_destination);
                if(!view->drag_copy)
                    gvr_edit_list_set_selection_internal(view,
                                                         view->drag_preview_in,
                                                         view->drag_preview_out,
                                                         TRUE,
                                                         TRUE);
            }
            break;

        case GVR_EDIT_LIST_DRAG_MOVE_SEPARATOR: {
            GvrEditListSeparatorData *separator =
                gvr_edit_list_separator_by_id(view, view->drag_separator_id, NULL);
            gvr_edit_list_prune_regions(view);
            if(separator && separator->frame != view->drag_separator_origin)
                gvr_edit_list_separator_emit(view, SIGNAL_SEPARATOR_MOVED,
                                             separator->id, separator->frame);
            break;
        }

        case GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_IN:
            if(view->drag_moved && view->drag_trim_boundary > view->drag_segment_in) {
                gvr_edit_list_set_selection_internal(view,
                                                     view->drag_segment_in,
                                                     view->drag_trim_boundary - 1,
                                                     TRUE, TRUE);
                gvr_edit_list_emit_action(view, GVR_EDIT_LIST_ACTION_DELETE, -1);
            }
            break;

        case GVR_EDIT_LIST_DRAG_TRIM_SEGMENT_OUT:
            if(view->drag_moved && view->drag_trim_boundary <= view->drag_segment_out) {
                gvr_edit_list_set_selection_internal(view,
                                                     view->drag_trim_boundary,
                                                     view->drag_segment_out,
                                                     TRUE, TRUE);
                gvr_edit_list_emit_action(view, GVR_EDIT_LIST_ACTION_DELETE, -1);
            }
            break;

        case GVR_EDIT_LIST_DRAG_PAN_TIMELINE:
            break;

        default:
            break;
    }

    gvr_edit_list_take_focus(view);
    view->drag_mode = GVR_EDIT_LIST_DRAG_NONE;
    view->drag_moved = FALSE;
    view->drag_copy = FALSE;
    view->snap_visible = FALSE;
    gvr_edit_list_update_summary(view);
    gvr_edit_list_update_cursor(view,
                                gvr_edit_list_hit_test(view,
                                                       event->x,
                                                       event->y,
                                                       gtk_widget_get_allocated_width(view->overview)));
    gvr_edit_list_queue_timeline_draw(view);
    return TRUE;
}

static gboolean gvr_edit_list_overview_scroll(GtkWidget *widget,
                                              GdkEventScroll *event,
                                              gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkAllocation allocation;
    int direction = 0;
    int step = 1;

    switch(event->direction) {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_RIGHT:
            direction = 1;
            break;

        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_LEFT:
            direction = -1;
            break;

        case GDK_SCROLL_SMOOTH:
            if(event->delta_y < 0.0 || event->delta_x > 0.0)
                direction = 1;
            else if(event->delta_y > 0.0 || event->delta_x < 0.0)
                direction = -1;
            break;

        default:
            break;
    }

    if(direction == 0)
        return FALSE;

    gtk_widget_get_allocation(widget, &allocation);

    if(event->state & GDK_CONTROL_MASK) {
        double ratio = allocation.width > 1 ?
                       CLAMP(event->x / allocation.width, 0.0, 1.0) : 0.5;
        int anchor = gvr_edit_list_x_to_frame(view,
                                              event->x,
                                              allocation.width);
        double factor = direction > 0 ? sqrt(2.0) : (1.0 / sqrt(2.0));

        gvr_edit_list_set_zoom_internal(view,
                                        view->timeline_zoom * factor,
                                        anchor,
                                        ratio);
        return TRUE;
    }

    if(view->timeline_zoom > 1.001) {
        int visible = gvr_edit_list_visible_frames(view);
        int divisor = (event->state & GDK_SHIFT_MASK) ? 4 : 12;
        int delta = -direction * MAX(1, visible / divisor);

        view->timeline_view_start = gvr_edit_list_clampi(view->timeline_view_start + delta,
                                                        0,
                                                        gvr_edit_list_view_max_start(view));
        gvr_edit_list_sync_timeline_controls(view);
        gvr_edit_list_queue_timeline_draw(view);
        return TRUE;
    }

    if(event->state & GDK_MOD1_MASK)
        step = MAX(1, (int)floor((view->fps > 0.0 ? view->fps : 25.0) * 5.0 + 0.5));
    else
        step = MAX(1, (int)floor((view->fps > 0.0 ? view->fps : 25.0) + 0.5));

    gvr_edit_list_emit_seek(view, view->playhead + (direction * step));
    return TRUE;
}

static gboolean gvr_edit_list_overview_query_tooltip(GtkWidget *widget,
                                                     gint x,
                                                     gint y,
                                                     gboolean keyboard_mode,
                                                     GtkTooltip *tooltip,
                                                     gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkAllocation allocation;
    GvrEditListSegmentData *segment;
    int frame;
    gchar *text;

    if(keyboard_mode || view->total_frames <= 0)
        return FALSE;

    gtk_widget_get_allocation(widget, &allocation);
    frame = gvr_edit_list_x_to_frame(view, x, allocation.width);

    if(y < (gint)gvr_edit_list_separator_height(view)) {
        GvrEditListSeparatorData *separator =
            gvr_edit_list_separator_at_x(view, x, allocation.width);
        if(separator) {
            gchar *time = gvr_edit_list_format_time(view,
                                                    MIN(separator->frame, gvr_edit_list_frame_max(view)));
            text = g_strdup_printf("%s\nSeparator at boundary %d · %s\nDrag to move · double-click to rename · Delete removes it",
                                   separator->name ? separator->name : "Separator",
                                   separator->frame, time);
            gtk_tooltip_set_text(tooltip, text);
            g_free(text);
            g_free(time);
            return TRUE;
        }

        if(y >= (gint)gvr_edit_list_region_top(view)) {
            GvrEditListRegionData *region = gvr_edit_list_region_at_frame(view, frame);
            int start_frame;
            int end_boundary;
            if(region && gvr_edit_list_region_bounds(view, region, &start_frame, &end_boundary)) {
                gchar *start_time = gvr_edit_list_format_time(view, start_frame);
                gchar *end_time = gvr_edit_list_format_time(view, MAX(start_frame, end_boundary - 1));
                text = g_strdup_printf("%s\nRegion %d–%d · %d frames\n%s–%s\nClick selects · double-click renames · right-click for region actions",
                                       region->name ? region->name : "Region",
                                       start_frame, end_boundary - 1,
                                       end_boundary - start_frame,
                                       start_time, end_time);
                gtk_tooltip_set_text(tooltip, text);
                g_free(text);
                g_free(start_time);
                g_free(end_time);
                return TRUE;
            }
        }
    }

    segment = gvr_edit_list_segment_at_frame(view, frame, NULL);
    if(!segment)
        return FALSE;

    {
        gchar *time = gvr_edit_list_format_time(view, frame);
        int source_frame = segment->file_in + (frame - segment->timeline_in);
        const char *edge_hint = "";
        GvrEditListHit hit = gvr_edit_list_hit_test(view, x, y, allocation.width);
        if(hit == GVR_EDIT_LIST_HIT_SEGMENT_IN)
            edge_hint = "\nDrag right to remove frames from this segment head";
        else if(hit == GVR_EDIT_LIST_HIT_SEGMENT_OUT)
            edge_hint = "\nDrag left to remove frames from this segment tail";
        text = g_strdup_printf("%s\nTimeline frame %d · %s\nSource frame %d · source range %d–%d\nSegment %d–%d · %d frames · %s%s\nDouble-click selects segment · Ctrl+double-click also zooms\nCtrl+wheel zooms · wheel pans · middle-drag pans · Alt bypasses snapping",
                               segment->filename,
                               frame,
                               time,
                               source_frame,
                               segment->file_in,
                               segment->file_out,
                               segment->timeline_in,
                               segment->timeline_out,
                               segment->timeline_out - segment->timeline_in + 1,
                               segment->fourcc && *segment->fourcc ? segment->fourcc : "unknown format",
                               edge_hint);
        g_free(time);
    }
    gtk_tooltip_set_text(tooltip, text);
    g_free(text);
    return TRUE;
}

static void gvr_edit_list_cell_data(GtkTreeViewColumn *column,
                                    GtkCellRenderer *renderer,
                                    GtkTreeModel *model,
                                    GtkTreeIter *iter,
                                    gpointer user_data)
{
    gboolean playing = FALSE;

    (void)column;
    (void)user_data;

    gtk_tree_model_get(model, iter, MODEL_PLAYING, &playing, -1);
    g_object_set(renderer,
                 "weight", playing ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                 "foreground-set", playing,
                 "foreground", playing ? "#ff8400" : NULL,
                 NULL);
}

static GtkTreeViewColumn *gvr_edit_list_append_text_column(GvrEditListView *view,
                                                           const char *title,
                                                           int model_column,
                                                           gboolean expand,
                                                           int min_width)
{
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(title,
                                                                         renderer,
                                                                         "text",
                                                                         model_column,
                                                                         NULL);

    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_expand(column, expand);
    gtk_tree_view_column_set_min_width(column, min_width);
    gtk_tree_view_column_set_cell_data_func(column,
                                            renderer,
                                            gvr_edit_list_cell_data,
                                            view,
                                            NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view->tree), column);
    return column;
}

static void gvr_edit_list_tree_selection_changed(GtkTreeSelection *selection,
                                                 gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkTreeModel *model;
    GtkTreeIter iter;
    int array_pos;
    int in_frame;
    int out_frame;

    if(view->syncing_tree ||
       !gtk_tree_selection_get_selected(selection, &model, &iter))
    {
        return;
    }

    gtk_tree_model_get(model,
                       &iter,
                       MODEL_ARRAY_POS, &array_pos,
                       MODEL_TIMELINE_IN, &in_frame,
                       MODEL_TIMELINE_OUT, &out_frame,
                       -1);

    if(array_pos < 0 || (guint)array_pos >= view->segments->len)
        return;

    view->selected_segment = ((GvrEditListSegmentData *)
                              g_ptr_array_index(view->segments, array_pos))->index;
    gvr_edit_list_set_selection_internal(view,
                                         in_frame,
                                         out_frame,
                                         TRUE,
                                         TRUE);
    gvr_edit_list_queue_timeline_draw(view);
    g_signal_emit(view,
                  gvr_edit_list_view_signals[SIGNAL_SEGMENT_SELECTED],
                  0,
                  view->selected_segment);
}

static void gvr_edit_list_tree_row_activated(GtkTreeView *tree,
                                             GtkTreePath *path,
                                             GtkTreeViewColumn *column,
                                             gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    int frame;

    (void)column;

    if(!gtk_tree_model_get_iter(model, &iter, path))
        return;

    gtk_tree_model_get(model, &iter, MODEL_TIMELINE_IN, &frame, -1);
    gvr_edit_list_emit_seek(view, frame);
}

static void gvr_edit_list_context_paste_before(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    (void)widget;

    if(gvr_edit_list_selection_valid(view))
        gvr_edit_list_paste_emit(view, view->selection_in);
}

static void gvr_edit_list_context_paste_after(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    (void)widget;

    if(gvr_edit_list_selection_valid(view))
        gvr_edit_list_paste_emit(view, view->selection_out + 1);
}

static gboolean gvr_edit_list_tree_button_press(GtkWidget *widget,
                                                GdkEventButton *event,
                                                gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    GtkTreePath *path = NULL;
    GtkTreeViewColumn *column = NULL;
    GtkWidget *menu;
    GtkWidget *item;

    if(event->button != 3)
        return FALSE;

    if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                     (gint)event->x,
                                     (gint)event->y,
                                     &path,
                                     &column,
                                     NULL,
                                     NULL))
    {
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, column, FALSE);
        gtk_tree_path_free(path);
    }

    menu = gtk_menu_new();
    item = gvr_edit_list_action_menu_item(menu,
                                          "Cut",
                                          NULL,
                                          GVR_EDIT_LIST_ACTION_CUT,
                                          view);
    gtk_widget_set_sensitive(item, view->editable && gvr_edit_list_selection_valid(view));

    item = gvr_edit_list_action_menu_item(menu,
                                          "Copy",
                                          NULL,
                                          GVR_EDIT_LIST_ACTION_COPY,
                                          view);
    gtk_widget_set_sensitive(item, gvr_edit_list_selection_valid(view));

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = gvr_edit_list_menu_item(menu,
                                   "Paste Before",
                                   NULL,
                                   G_CALLBACK(gvr_edit_list_context_paste_before),
                                   view);
    gtk_widget_set_sensitive(item, view->editable && view->clipboard_valid);

    item = gvr_edit_list_menu_item(menu,
                                   "Paste After",
                                   NULL,
                                   G_CALLBACK(gvr_edit_list_context_paste_after),
                                   view);
    gtk_widget_set_sensitive(item, view->editable && view->clipboard_valid);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = gvr_edit_list_action_menu_item(menu,
                                          "Delete",
                                          NULL,
                                          GVR_EDIT_LIST_ACTION_DELETE,
                                          view);
    gtk_widget_set_sensitive(item, view->editable && gvr_edit_list_selection_valid(view));

    item = gvr_edit_list_action_menu_item(menu,
                                          "Crop to Selection",
                                          NULL,
                                          GVR_EDIT_LIST_ACTION_CROP,
                                          view);
    gtk_widget_set_sensitive(item,
                             view->editable &&
                             gvr_edit_list_selection_valid(view) &&
                             !(view->selection_in == 0 &&
                               view->selection_out == gvr_edit_list_frame_max(view)));

    item = gvr_edit_list_action_menu_item(menu,
                                          "Create Sample",
                                          NULL,
                                          GVR_EDIT_LIST_ACTION_NEW_SAMPLE,
                                          view);
    gtk_widget_set_sensitive(item, gvr_edit_list_selection_valid(view));

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}

static gboolean gvr_edit_list_seek_separator(GvrEditListView *view, gboolean next)
{
    GvrEditListSeparatorData *candidate = NULL;
    guint i;

    for(i = 0; i < view->separators->len; i++) {
        GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, i);
        if(next) {
            if(separator->frame > view->playhead &&
               (!candidate || separator->frame < candidate->frame))
                candidate = separator;
        } else {
            if(separator->frame < view->playhead &&
               (!candidate || separator->frame > candidate->frame))
                candidate = separator;
        }
    }

    if(!candidate)
        return FALSE;

    view->selected_separator_id = candidate->id;
    gtk_widget_set_sensitive(view->separator_remove_button, TRUE);
    gvr_edit_list_emit_seek(view, candidate->frame >= view->total_frames ?
                                  view->total_frames - 1 : candidate->frame);
    gvr_edit_list_queue_timeline_draw(view);
    return TRUE;
}

static void gvr_edit_list_cancel_drag(GvrEditListView *view)
{
    if(view->drag_mode == GVR_EDIT_LIST_DRAG_MOVE_SEPARATOR &&
       view->drag_separator_id != 0)
        gvr_edit_list_move_separator(view, view->drag_separator_id,
                                     view->drag_separator_origin, FALSE);
    else if(view->drag_mode == GVR_EDIT_LIST_DRAG_PAN_TIMELINE ||
            view->drag_mode == GVR_EDIT_LIST_DRAG_PAN_NAVIGATOR) {
        view->timeline_view_start = gvr_edit_list_clampi(view->drag_pan_origin_start,
                                                        0,
                                                        gvr_edit_list_view_max_start(view));
        gvr_edit_list_sync_timeline_controls(view);
    }

    view->drag_mode = GVR_EDIT_LIST_DRAG_NONE;
    view->drag_moved = FALSE;
    view->drag_copy = FALSE;
    view->snap_visible = FALSE;
    view->drag_preview_in = view->selection_in;
    view->drag_preview_out = view->selection_out;
    gvr_edit_list_update_summary(view);
    gvr_edit_list_update_cursor(view, GVR_EDIT_LIST_HIT_NONE);
    gvr_edit_list_queue_timeline_draw(view);
}

static gboolean gvr_edit_list_key_press(GtkWidget *widget,
                                        GdkEventKey *event,
                                        gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (event->state & GDK_SHIFT_MASK) != 0;
    gboolean alt = (event->state & GDK_MOD1_MASK) != 0;

    (void)widget;

    if(alt && (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_KP_Left))
        return gvr_edit_list_seek_separator(view, FALSE);
    if(alt && (event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_KP_Right))
        return gvr_edit_list_seek_separator(view, TRUE);

    if(event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up ||
       event->keyval == GDK_KEY_Page_Up)
        return gvr_edit_list_seek_edit_boundary(view, FALSE, shift);
    if(event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down ||
       event->keyval == GDK_KEY_Page_Down)
        return gvr_edit_list_seek_edit_boundary(view, TRUE, shift);

    if(event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_KP_Left ||
       event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_KP_Right)
    {
        int direction = (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_KP_Left) ? -1 : 1;
        int step = 1;
        if(ctrl)
            step = MAX(1, (int)floor((view->fps > 0.0 ? view->fps : 25.0) + 0.5));
        else if(shift)
            step = 10;
        gvr_edit_list_emit_seek(view, view->playhead + direction * step);
        return TRUE;
    }

    if(ctrl) {
        switch(event->keyval) {
            case GDK_KEY_c:
            case GDK_KEY_C:
                if(gvr_edit_list_selection_valid(view))
                    gvr_edit_list_emit_action(view, GVR_EDIT_LIST_ACTION_COPY, -1);
                return TRUE;

            case GDK_KEY_x:
            case GDK_KEY_X:
                if(view->editable && gvr_edit_list_selection_valid(view))
                    gvr_edit_list_emit_action(view, GVR_EDIT_LIST_ACTION_CUT, -1);
                return TRUE;

            case GDK_KEY_v:
            case GDK_KEY_V:
                if(view->editable && view->clipboard_valid)
                    gvr_edit_list_paste_emit(view, view->playhead);
                return TRUE;

            case GDK_KEY_a:
            case GDK_KEY_A:
                if(view->total_frames > 0)
                    gvr_edit_list_set_selection_internal(view, 0,
                                                         gvr_edit_list_frame_max(view),
                                                         TRUE, TRUE);
                return TRUE;

            case GDK_KEY_plus:
            case GDK_KEY_equal:
            case GDK_KEY_KP_Add:
                gvr_edit_list_zoom_in_clicked(NULL, view);
                return TRUE;

            case GDK_KEY_minus:
            case GDK_KEY_KP_Subtract:
                gvr_edit_list_zoom_out_clicked(NULL, view);
                return TRUE;

            case GDK_KEY_0:
            case GDK_KEY_KP_0:
                gvr_edit_list_zoom_fit_clicked(NULL, view);
                return TRUE;

            case GDK_KEY_1:
            case GDK_KEY_KP_1:
                gvr_edit_list_zoom_frame_clicked(NULL, view);
                return TRUE;

            default:
                break;
        }
    }

    switch(event->keyval) {
        case GDK_KEY_i:
        case GDK_KEY_I:
        case GDK_KEY_bracketleft:
            gvr_edit_list_set_selection_internal(view,
                                                 view->playhead,
                                                 gvr_edit_list_selection_valid(view) ?
                                                     MAX(view->playhead, view->selection_out) :
                                                     view->playhead,
                                                 TRUE, TRUE);
            return TRUE;

        case GDK_KEY_o:
        case GDK_KEY_O:
        case GDK_KEY_bracketright:
            gvr_edit_list_set_selection_internal(view,
                                                 gvr_edit_list_selection_valid(view) ?
                                                     MIN(view->selection_in, view->playhead) :
                                                     view->playhead,
                                                 view->playhead,
                                                 TRUE, TRUE);
            return TRUE;

        case GDK_KEY_m:
        case GDK_KEY_M:
            if(view->editable && view->total_frames > 0)
                gvr_edit_list_separator_add_clicked(NULL, view);
            return TRUE;

        case GDK_KEY_s:
        case GDK_KEY_S:
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(view->snap_toggle),
                                         !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->snap_toggle)));
            return TRUE;

        case GDK_KEY_f:
        case GDK_KEY_F:
            if(gvr_edit_list_selection_valid(view))
                gvr_edit_list_zoom_selection_clicked(NULL, view);
            else
                gvr_edit_list_zoom_fit_clicked(NULL, view);
            return TRUE;

        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete:
        case GDK_KEY_BackSpace:
            if(view->editable && gvr_edit_list_selection_valid(view))
                gvr_edit_list_emit_action(view, GVR_EDIT_LIST_ACTION_DELETE, -1);
            else if(view->editable && view->selected_separator_id != 0)
                gvr_edit_list_remove_separator(view, view->selected_separator_id, TRUE);
            return TRUE;

        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if(view->selected_separator_id != 0)
                gvr_edit_list_rename_separator(view, view->selected_separator_id);
            else if(gvr_edit_list_selection_valid(view))
                gvr_edit_list_emit_seek(view, view->selection_in);
            return TRUE;

        case GDK_KEY_Escape:
            if(view->drag_mode != GVR_EDIT_LIST_DRAG_NONE) {
                gvr_edit_list_cancel_drag(view);
                return TRUE;
            }
            view->selected_separator_id = 0;
            gvr_edit_list_set_selection_internal(view, 0, 0, FALSE, TRUE);
            gtk_widget_set_sensitive(view->separator_remove_button, FALSE);
            gvr_edit_list_queue_timeline_draw(view);
            return TRUE;

        default:
            break;
    }

    return FALSE;
}

static void gvr_edit_list_spin_changed(GtkSpinButton *spin, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    int in_frame;
    int out_frame;

    (void)spin;

    if(view->syncing)
        return;

    in_frame = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(view->in_spin));
    out_frame = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(view->out_spin));

    if(in_frame > out_frame) {
        if(GTK_WIDGET(spin) == view->in_spin)
            out_frame = in_frame;
        else
            in_frame = out_frame;
    }

    gvr_edit_list_set_selection_internal(view,
                                         in_frame,
                                         out_frame,
                                         TRUE,
                                         TRUE);
}

static void gvr_edit_list_set_in_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    int out_frame = gvr_edit_list_selection_valid(view) ?
                    MAX(view->selection_out, view->playhead) :
                    view->playhead;

    (void)widget;
    gvr_edit_list_set_selection_internal(view,
                                         view->playhead,
                                         out_frame,
                                         TRUE,
                                         TRUE);
}

static void gvr_edit_list_set_out_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    int in_frame = gvr_edit_list_selection_valid(view) ?
                   MIN(view->selection_in, view->playhead) :
                   view->playhead;

    (void)widget;
    gvr_edit_list_set_selection_internal(view,
                                         in_frame,
                                         view->playhead,
                                         TRUE,
                                         TRUE);
}

static void gvr_edit_list_select_all_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    (void)widget;

    if(view->total_frames > 0)
        gvr_edit_list_set_selection_internal(view,
                                             0,
                                             gvr_edit_list_frame_max(view),
                                             TRUE,
                                             TRUE);
}

static void gvr_edit_list_clear_clicked(GtkWidget *widget, gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    (void)widget;

    gvr_edit_list_set_selection_internal(view, 0, 0, FALSE, TRUE);
}

static GtkWidget *gvr_edit_list_menu_button(const char *filename,
                                            const char *fallback,
                                            const char *tooltip,
                                            GtkWidget *menu)
{
    GtkWidget *button = gtk_menu_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    GtkWidget *image = gvr_edit_list_icon_child(filename, FALSE, fallback);
    GtkWidget *arrow = gtk_label_new("▾");

    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), arrow, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button), box);
    gtk_widget_show_all(box);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(button), menu);
    gtk_widget_set_tooltip_text(button, tooltip);
    gvr_edit_list_button_set_accessible_name(button, fallback);
    return button;
}

static GtkWidget *gvr_edit_list_range_label(const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    g_object_set(G_OBJECT(label), "xalign", 0.0f, NULL);
    return label;
}

static void gvr_edit_list_view_finalize(GObject *object)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(object);

    if(view->segments)
        g_ptr_array_free(view->segments, TRUE);
    if(view->separator_sets)
        g_hash_table_destroy(view->separator_sets);
    if(view->region_sets)
        g_hash_table_destroy(view->region_sets);

    G_OBJECT_CLASS(gvr_edit_list_view_parent_class)->finalize(object);
}


static void gvr_edit_list_view_style_updated(GtkWidget *widget)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(widget);

    GTK_WIDGET_CLASS(gvr_edit_list_view_parent_class)->style_updated(widget);

    if(view->overview) {
        gtk_widget_set_size_request(
            view->overview,
            480,
            MAX(124,
                (int)ceil(gvr_edit_list_clip_top(view) +
                          gvr_edit_list_min_clip_height(view) + 2.0)));
        gtk_widget_queue_resize(view->overview);
        gtk_widget_queue_draw(view->overview);
    }
    if(view->navigator) {
        gtk_widget_set_size_request(view->navigator,
                                    480,
                                    gvr_edit_list_navigator_height(view));
        gtk_widget_queue_resize(view->navigator);
        gtk_widget_queue_draw(view->navigator);
    }
}

static void gvr_edit_list_media_drag_data_received(GtkWidget *widget,
                                                   GdkDragContext *context,
                                                   gint x,
                                                   gint y,
                                                   GtkSelectionData *selection,
                                                   guint info,
                                                   guint time,
                                                   gpointer user_data)
{
    GvrEditListView *view = GVR_EDIT_LIST_VIEW(user_data);
    const guchar *data;
    gint length;
    gchar *filename = NULL;
    gboolean accepted = FALSE;

    (void)widget;
    (void)x;
    (void)y;
    (void)info;

    if(!view->editable)
        goto done;

    data = gtk_selection_data_get_data(selection);
    length = gtk_selection_data_get_length(selection);
    if(!data || length <= 0)
        goto done;

    filename = g_strndup((const gchar *)data, (gsize)length);
    if(!filename || !filename[0] || !g_utf8_validate(filename, -1, NULL))
        goto done;

    g_signal_emit(view,
                  gvr_edit_list_view_signals[SIGNAL_MEDIA_FILE_DROPPED],
                  0,
                  filename);
    accepted = TRUE;

done:
    gtk_drag_finish(context, accepted, FALSE, time);
    g_free(filename);
}

static void gvr_edit_list_register_media_drop(GtkWidget *widget,
                                              GvrEditListView *view)
{
    gtk_drag_dest_set(widget,
                      GTK_DEST_DEFAULT_ALL,
                      gvr_edit_list_media_drop_targets,
                      G_N_ELEMENTS(gvr_edit_list_media_drop_targets),
                      GDK_ACTION_COPY);
    g_signal_connect(widget,
                     "drag-data-received",
                     G_CALLBACK(gvr_edit_list_media_drag_data_received),
                     view);
}

static void gvr_edit_list_view_class_init(GvrEditListViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    widget_class->style_updated = gvr_edit_list_view_style_updated;
    object_class->finalize = gvr_edit_list_view_finalize;

    gvr_edit_list_view_signals[SIGNAL_ACTION_REQUESTED] =
        g_signal_new("action-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL,
                     NULL,
                     NULL,
                     G_TYPE_NONE,
                     4,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_INT);

    gvr_edit_list_view_signals[SIGNAL_SEEK_REQUESTED] =
        g_signal_new("seek-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    gvr_edit_list_view_signals[SIGNAL_SELECTION_CHANGED] =
        g_signal_new("selection-changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL,
                     NULL,
                     NULL,
                     G_TYPE_NONE,
                     3,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_BOOLEAN);

    gvr_edit_list_view_signals[SIGNAL_SEGMENT_SELECTED] =
        g_signal_new("segment-selected",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    gvr_edit_list_view_signals[SIGNAL_SEPARATOR_ADDED] =
        g_signal_new("separator-added",
                     G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST, 0,
                     NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    gvr_edit_list_view_signals[SIGNAL_SEPARATOR_MOVED] =
        g_signal_new("separator-moved",
                     G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST, 0,
                     NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    gvr_edit_list_view_signals[SIGNAL_SEPARATOR_REMOVED] =
        g_signal_new("separator-removed",
                     G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST, 0,
                     NULL, NULL, g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE, 1, G_TYPE_INT);

    gvr_edit_list_view_signals[SIGNAL_MEDIA_FILE_DROPPED] =
        g_signal_new("media-file-dropped",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__STRING,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_STRING);
}

static void gvr_edit_list_view_init(GvrEditListView *view)
{
    GtkWidget *toolbar;
    GtkWidget *statusbar;
    GtkWidget *timeline_box;
    GtkWidget *timeline_controls;
    GtkWidget *scrolled;
    GtkWidget *rangebar;
    GtkWidget *append_menu;
    GtkWidget *save_menu;
    GtkWidget *paste_menu;
    GtkWidget *item;
    GtkWidget *separator;
    GtkWidget *popover_box;
    GtkWidget *paste_now;
    GtkTreeSelection *selection;

    gtk_orientable_set_orientation(GTK_ORIENTABLE(view), GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(view), 2);
    gtk_widget_set_can_focus(GTK_WIDGET(view), TRUE);
    gvr_edit_list_add_class(GTK_WIDGET(view), "edit-list-view");

    view->segments = g_ptr_array_new_with_free_func(gvr_edit_list_segment_free);
    view->separator_sets = g_hash_table_new_full(g_direct_hash,
                                                 g_direct_equal,
                                                 NULL,
                                                 gvr_edit_list_separator_array_free);
    view->separators = gvr_edit_list_separator_set(view, -1, TRUE);
    view->region_sets = g_hash_table_new_full(g_direct_hash,
                                              g_direct_equal,
                                              NULL,
                                              gvr_edit_list_region_array_free);
    view->regions = gvr_edit_list_region_set(view, -1, TRUE);
    view->next_separator_id = 1;
    view->selected_separator_id = 0;
    view->sample_id = -1;
    view->source_file_count = 0;
    view->total_frames = 0;
    view->fps = 25.0;
    view->playhead = 0;
    view->selection_active = FALSE;
    view->selection_in = 0;
    view->selection_out = 0;
    view->selected_segment = -1;
    view->editable = TRUE;
    view->timeline_zoom = GVR_EDIT_LIST_ZOOM_MIN;
    view->timeline_view_start = 0;
    view->drag_mode = GVR_EDIT_LIST_DRAG_NONE;
    view->snap_enabled = TRUE;
    view->snap_visible = FALSE;
    view->hover_hit = GVR_EDIT_LIST_HIT_NONE;
    view->hover_segment_pos = -1;
    view->drag_segment_pos = -1;
    view->syncing_view = FALSE;

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gvr_edit_list_add_class(toolbar, "edit-list-toolbar");
    gtk_box_pack_start(GTK_BOX(view), toolbar, FALSE, FALSE, 0);

    view->target_label = gtk_label_new("No edit list");
    g_object_set(G_OBJECT(view->target_label), "xalign", 0.0f, NULL);
    gtk_label_set_single_line_mode(GTK_LABEL(view->target_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(view->target_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_tooltip_text(
        view->target_label,
        "Source files are opened media files. Segments are timeline ranges; several "
        "segments may reference the same source file.");
    gtk_widget_set_hexpand(view->target_label, TRUE);
    gtk_box_pack_start(GTK_BOX(toolbar), view->target_label, TRUE, TRUE, 3);

    append_menu = gtk_menu_new();
    gvr_edit_list_action_menu_item(append_menu,
                                   "Append File",
                                   "Append a video file to the end of this edit list.",
                                   GVR_EDIT_LIST_ACTION_APPEND_FILE,
                                   view);
    gvr_edit_list_action_menu_item(append_menu,
                                   "Append File + Sample",
                                   "Append a video file and request creation of a sample.",
                                   GVR_EDIT_LIST_ACTION_APPEND_FILE_AND_SAMPLE,
                                   view);
    view->append_button = gvr_edit_list_menu_button("icon_open.png",
                                                    "Append",
                                                    "Append video material to the edit list.",
                                                    append_menu);
    gtk_box_pack_start(GTK_BOX(toolbar), view->append_button, FALSE, FALSE, 0);

    save_menu = gtk_menu_new();
    gvr_edit_list_action_menu_item(save_menu,
                                   "Save List",
                                   "Save the complete edit list.",
                                   GVR_EDIT_LIST_ACTION_SAVE_LIST,
                                   view);
    view->save_selection_item = gvr_edit_list_action_menu_item(save_menu,
                                                               "Save Selection",
                                                               "Save only the selected frame range.",
                                                               GVR_EDIT_LIST_ACTION_SAVE_SELECTION,
                                                               view);
    view->save_button = gvr_edit_list_menu_button("icon_save.png",
                                                  "Save",
                                                  "Save the complete edit list or the selected range.",
                                                  save_menu);
    gtk_box_pack_start(GTK_BOX(toolbar), view->save_button, FALSE, FALSE, 0);

    separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(toolbar), separator, FALSE, FALSE, 2);

    view->cut_button = gvr_edit_list_toolbar_button(toolbar,
                                                    "icon_cut.png",
                                                    "Cut",
                                                    "Cut the selected frames to the edit-list clipboard (Ctrl+X).",
                                                    GVR_EDIT_LIST_ACTION_CUT,
                                                    view);
    view->copy_button = gvr_edit_list_toolbar_button(toolbar,
                                                     "icon_copy.png",
                                                     "Copy",
                                                     "Copy the selected frames to the edit-list clipboard (Ctrl+C).",
                                                     GVR_EDIT_LIST_ACTION_COPY,
                                                     view);

    paste_menu = gtk_menu_new();
    gvr_edit_list_paste_menu_item(paste_menu,
                                  "At Playhead",
                                  GVR_EDIT_LIST_PASTE_PLAYHEAD,
                                  view);
    gvr_edit_list_paste_menu_item(paste_menu,
                                  "At Selection Start",
                                  GVR_EDIT_LIST_PASTE_SELECTION_IN,
                                  view);
    gvr_edit_list_paste_menu_item(paste_menu,
                                  "After Selection",
                                  GVR_EDIT_LIST_PASTE_SELECTION_OUT,
                                  view);
    gvr_edit_list_paste_menu_item(paste_menu,
                                  "At Frame…",
                                  GVR_EDIT_LIST_PASTE_EXPLICIT,
                                  view);
    view->paste_button = gvr_edit_list_menu_button("icon_paste.png",
                                                   "Paste",
                                                   "Paste edit-list clipboard frames at a chosen destination (Ctrl+V uses the playhead).",
                                                   paste_menu);
    gtk_box_pack_start(GTK_BOX(toolbar), view->paste_button, FALSE, FALSE, 0);

    view->delete_button = gvr_edit_list_toolbar_button(toolbar,
                                                       "button_skull.png",
                                                       "Delete",
                                                       "Delete the selected frames without copying them.",
                                                       GVR_EDIT_LIST_ACTION_DELETE,
                                                       view);
    gvr_edit_list_add_class(view->delete_button, "destructive-action");

    view->crop_button = gvr_edit_list_toolbar_button(toolbar,
                                                     "icon_crop.png",
                                                     "Crop",
                                                     "Keep only the selected frame range.",
                                                     GVR_EDIT_LIST_ACTION_CROP,
                                                     view);
    gvr_edit_list_add_class(view->crop_button, "destructive-action");

    view->new_sample_button = gvr_edit_list_text_toolbar_button(toolbar,
                                                                "New Sample",
                                                                "Create a sample from the selected frame range.",
                                                                GVR_EDIT_LIST_ACTION_NEW_SAMPLE,
                                                                view);

    statusbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gvr_edit_list_add_class(statusbar, "edit-list-status");
    gtk_box_pack_start(GTK_BOX(view), statusbar, FALSE, FALSE, 1);

    view->playhead_label = gtk_label_new("Playhead 0 · 00:00:00:00");
    g_object_set(G_OBJECT(view->playhead_label), "xalign", 0.0f, NULL);
    gtk_box_pack_start(GTK_BOX(statusbar), view->playhead_label, FALSE, FALSE, 3);

    view->selection_label = gtk_label_new("Selection —");
    g_object_set(G_OBJECT(view->selection_label), "xalign", 0.0f, NULL);
    gtk_widget_set_hexpand(view->selection_label, TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(view->selection_label), 64);
    gtk_label_set_ellipsize(GTK_LABEL(view->selection_label), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(statusbar), view->selection_label, TRUE, TRUE, 3);

    timeline_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gvr_edit_list_add_class(timeline_box, "edit-list-timeline");
    gtk_box_pack_start(GTK_BOX(view), timeline_box, FALSE, TRUE, 0);

    timeline_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gvr_edit_list_add_class(timeline_controls, "edit-list-timeline-controls");
    gtk_box_pack_start(GTK_BOX(timeline_box), timeline_controls, FALSE, FALSE, 0);

    item = gtk_label_new("Timeline");
    g_object_set(G_OBJECT(item), "xalign", 0.0f, NULL);
    gtk_widget_set_hexpand(item, TRUE);
    gtk_box_pack_start(GTK_BOX(timeline_controls), item, TRUE, TRUE, 3);

    view->separator_add_button = gtk_button_new_with_label("M+");
    gtk_widget_set_tooltip_text(view->separator_add_button, "Add a separator at the playhead (M).");
    g_signal_connect(view->separator_add_button, "clicked",
                     G_CALLBACK(gvr_edit_list_separator_add_clicked), view);
    gtk_box_pack_start(GTK_BOX(timeline_controls), view->separator_add_button, FALSE, FALSE, 0);

    view->separator_remove_button = gtk_button_new_with_label("M−");
    gtk_widget_set_tooltip_text(view->separator_remove_button, "Delete the selected separator.");
    gtk_widget_set_sensitive(view->separator_remove_button, FALSE);
    g_signal_connect(view->separator_remove_button, "clicked",
                     G_CALLBACK(gvr_edit_list_separator_remove_clicked), view);
    gtk_box_pack_start(GTK_BOX(timeline_controls), view->separator_remove_button, FALSE, FALSE, 0);

    view->snap_toggle = gtk_toggle_button_new_with_label("Snap");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(view->snap_toggle), TRUE);
    gtk_widget_set_tooltip_text(view->snap_toggle, "Snap to cuts, separators and the playhead (S; hold Alt to bypass).");
    g_signal_connect(view->snap_toggle, "toggled",
                     G_CALLBACK(gvr_edit_list_snap_toggled), view);
    gtk_box_pack_start(GTK_BOX(timeline_controls), view->snap_toggle, FALSE, FALSE, 2);

    view->zoom_label = gtk_label_new("Fit");
    gtk_widget_set_size_request(view->zoom_label, 34, -1);
    gtk_box_pack_end(GTK_BOX(timeline_controls), view->zoom_label, FALSE, FALSE, 2);

    view->zoom_frame_button = gtk_button_new_with_label("Frame");
    gtk_widget_set_tooltip_text(view->zoom_frame_button, "Zoom to approximately 8 pixels per frame.");
    g_signal_connect(view->zoom_frame_button, "clicked",
                     G_CALLBACK(gvr_edit_list_zoom_frame_clicked), view);
    gtk_box_pack_end(GTK_BOX(timeline_controls), view->zoom_frame_button, FALSE, FALSE, 0);

    view->zoom_selection_button = gtk_button_new_with_label("Sel");
    gtk_widget_set_tooltip_text(view->zoom_selection_button, "Fit the current selection in the timeline.");
    g_signal_connect(view->zoom_selection_button, "clicked",
                     G_CALLBACK(gvr_edit_list_zoom_selection_clicked), view);
    gtk_box_pack_end(GTK_BOX(timeline_controls), view->zoom_selection_button, FALSE, FALSE, 0);

    view->zoom_fit_button = gtk_button_new_with_label("Fit");
    gtk_widget_set_tooltip_text(view->zoom_fit_button, "Fit the complete edit list in the timeline.");
    g_signal_connect(view->zoom_fit_button,
                     "clicked",
                     G_CALLBACK(gvr_edit_list_zoom_fit_clicked),
                     view);
    gtk_box_pack_end(GTK_BOX(timeline_controls), view->zoom_fit_button, FALSE, FALSE, 0);

    view->zoom_in_button = gtk_button_new_with_label("+");
    gtk_widget_set_tooltip_text(view->zoom_in_button, "Zoom into the timeline.");
    g_signal_connect(view->zoom_in_button,
                     "clicked",
                     G_CALLBACK(gvr_edit_list_zoom_in_clicked),
                     view);
    gtk_box_pack_end(GTK_BOX(timeline_controls), view->zoom_in_button, FALSE, FALSE, 0);

    view->zoom_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                0.0,
                                                7.0,
                                                0.25);
    gtk_scale_set_draw_value(GTK_SCALE(view->zoom_scale), FALSE);
    gtk_widget_set_size_request(view->zoom_scale, 116, -1);
    gtk_widget_set_tooltip_text(view->zoom_scale,
                                "Timeline zoom. Ctrl+mouse wheel zooms around the pointer; wheel pans.");
    g_signal_connect(view->zoom_scale,
                     "value-changed",
                     G_CALLBACK(gvr_edit_list_zoom_scale_changed),
                     view);
    gtk_box_pack_end(GTK_BOX(timeline_controls), view->zoom_scale, FALSE, FALSE, 2);

    view->zoom_out_button = gtk_button_new_with_label("−");
    gtk_widget_set_tooltip_text(view->zoom_out_button, "Zoom out of the timeline.");
    g_signal_connect(view->zoom_out_button,
                     "clicked",
                     G_CALLBACK(gvr_edit_list_zoom_out_clicked),
                     view);
    gtk_box_pack_end(GTK_BOX(timeline_controls), view->zoom_out_button, FALSE, FALSE, 0);

    view->overview = gtk_drawing_area_new();
    gtk_widget_set_size_request(
        view->overview,
        480,
        MAX(124,
            (int)ceil(gvr_edit_list_clip_top(view) +
                      gvr_edit_list_min_clip_height(view) + 2.0)));
    gtk_widget_set_can_focus(view->overview, TRUE);
    gtk_widget_set_has_tooltip(view->overview, TRUE);
    gtk_widget_add_events(view->overview,
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON1_MOTION_MASK |
                          GDK_BUTTON2_MOTION_MASK |
                          GDK_KEY_PRESS_MASK |
                          GDK_FOCUS_CHANGE_MASK |
                          GDK_LEAVE_NOTIFY_MASK |
                          GDK_SCROLL_MASK |
                          GDK_SMOOTH_SCROLL_MASK);
    gvr_edit_list_add_class(view->overview, "edit-list-overview");
    g_signal_connect(view->overview,
                     "draw",
                     G_CALLBACK(gvr_edit_list_overview_draw),
                     view);
    g_signal_connect(view->overview,
                     "button-press-event",
                     G_CALLBACK(gvr_edit_list_overview_button_press),
                     view);
    g_signal_connect(view->overview,
                     "motion-notify-event",
                     G_CALLBACK(gvr_edit_list_overview_motion),
                     view);
    g_signal_connect(view->overview,
                     "button-release-event",
                     G_CALLBACK(gvr_edit_list_overview_button_release),
                     view);
    g_signal_connect(view->overview,
                     "scroll-event",
                     G_CALLBACK(gvr_edit_list_overview_scroll),
                     view);
    g_signal_connect(view->overview,
                     "query-tooltip",
                     G_CALLBACK(gvr_edit_list_overview_query_tooltip),
                     view);
    g_signal_connect(view->overview,
                     "key-press-event",
                     G_CALLBACK(gvr_edit_list_key_press),
                     view);
    g_signal_connect(view->overview,
                     "focus-in-event",
                     G_CALLBACK(gvr_edit_list_overview_focus_event),
                     view);
    g_signal_connect(view->overview,
                     "focus-out-event",
                     G_CALLBACK(gvr_edit_list_overview_focus_event),
                     view);
    g_signal_connect(view->overview,
                     "leave-notify-event",
                     G_CALLBACK(gvr_edit_list_overview_leave),
                     view);
    gtk_box_pack_start(GTK_BOX(timeline_box), view->overview, FALSE, TRUE, 0);

    view->navigator = gtk_drawing_area_new();
    gtk_widget_set_size_request(view->navigator, 480, gvr_edit_list_navigator_height(view));
    gtk_widget_set_has_tooltip(view->navigator, TRUE);
    gtk_widget_add_events(view->navigator,
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON1_MOTION_MASK |
                          GDK_BUTTON2_MOTION_MASK);
    gvr_edit_list_add_class(view->navigator, "edit-list-navigator");
    g_signal_connect(view->navigator,
                     "draw",
                     G_CALLBACK(gvr_edit_list_navigator_draw),
                     view);
    g_signal_connect(view->navigator,
                     "button-press-event",
                     G_CALLBACK(gvr_edit_list_navigator_button_press),
                     view);
    g_signal_connect(view->navigator,
                     "motion-notify-event",
                     G_CALLBACK(gvr_edit_list_navigator_motion),
                     view);
    g_signal_connect(view->navigator,
                     "button-release-event",
                     G_CALLBACK(gvr_edit_list_navigator_button_release),
                     view);
    g_signal_connect(view->navigator,
                     "query-tooltip",
                     G_CALLBACK(gvr_edit_list_navigator_query_tooltip),
                     view);
    gtk_box_pack_start(GTK_BOX(timeline_box), view->navigator, FALSE, TRUE, 0);

    view->pan_adjustment = GTK_ADJUSTMENT(gtk_adjustment_new(0.0,
                                                            0.0,
                                                            1.0,
                                                            1.0,
                                                            1.0,
                                                            1.0));
    g_signal_connect(view->pan_adjustment,
                     "value-changed",
                     G_CALLBACK(gvr_edit_list_pan_changed),
                     view);
    view->pan_scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL,
                                            view->pan_adjustment);
    gtk_widget_set_size_request(view->pan_scrollbar, -1, 11);
    gtk_widget_set_tooltip_text(view->pan_scrollbar, "Pan the zoomed timeline.");
    gtk_box_pack_start(GTK_BOX(timeline_box), view->pan_scrollbar, FALSE, TRUE, 0);

    view->store = gtk_list_store_new(MODEL_N_COLUMNS,
                                     G_TYPE_INT,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_INT,
                                     G_TYPE_STRING,
                                     G_TYPE_INT,
                                     G_TYPE_INT,
                                     G_TYPE_INT,
                                     G_TYPE_BOOLEAN);

    view->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(view->store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view->tree), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view->tree), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(view->tree), MODEL_FILENAME);
    gtk_widget_set_can_focus(view->tree, TRUE);
    gvr_edit_list_add_class(view->tree, "edit-list-tree");

    gvr_edit_list_append_text_column(view, "Segment", MODEL_INDEX, FALSE, 68);
    gvr_edit_list_append_text_column(view, "Source file", MODEL_FILENAME, TRUE, 180);
    gvr_edit_list_append_text_column(view, "Timeline frames", MODEL_EDL_RANGE, FALSE, 118);
    gvr_edit_list_append_text_column(view, "Source frames", MODEL_FILE_RANGE, FALSE, 112);
    gvr_edit_list_append_text_column(view, "Length", MODEL_LENGTH, FALSE, 68);
    gvr_edit_list_append_text_column(view, "Format", MODEL_FOURCC, FALSE, 58);

    gtk_widget_set_tooltip_text(
        view->tree,
        "Each row is a timeline segment. Copy, paste, cut and move operations may "
        "create several segments that all reference the same source file. Drag a "
        "file from Backend Media here to append it to this Edit List.");

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection,
                     "changed",
                     G_CALLBACK(gvr_edit_list_tree_selection_changed),
                     view);
    g_signal_connect(view->tree,
                     "row-activated",
                     G_CALLBACK(gvr_edit_list_tree_row_activated),
                     view);
    g_signal_connect(view->tree,
                     "button-press-event",
                     G_CALLBACK(gvr_edit_list_tree_button_press),
                     view);
    g_signal_connect(view->tree,
                     "key-press-event",
                     G_CALLBACK(gvr_edit_list_key_press),
                     view);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scrolled), view->tree);
    gtk_box_pack_start(GTK_BOX(view), scrolled, TRUE, TRUE, 0);

    rangebar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gvr_edit_list_add_class(rangebar, "edit-list-rangebar");
    gtk_box_pack_start(GTK_BOX(view), rangebar, FALSE, FALSE, 1);

    item = gvr_edit_list_range_label("In");
    gtk_box_pack_start(GTK_BOX(rangebar), item, FALSE, FALSE, 2);

    view->in_spin = gtk_spin_button_new_with_range(0, 0, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(view->in_spin), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(view->in_spin), 8);
    g_signal_connect(view->in_spin,
                     "value-changed",
                     G_CALLBACK(gvr_edit_list_spin_changed),
                     view);
    gtk_box_pack_start(GTK_BOX(rangebar), view->in_spin, FALSE, FALSE, 0);

    view->in_time_label = gvr_edit_list_range_label("00:00:00:00");
    gtk_box_pack_start(GTK_BOX(rangebar), view->in_time_label, FALSE, FALSE, 2);

    view->set_in_button = gvr_edit_list_image_button("button_gotostart.png",
                                                      TRUE,
                                                      "Set In",
                                                      "Set the selection start to the current playhead (I).");
    g_signal_connect(view->set_in_button,
                     "clicked",
                     G_CALLBACK(gvr_edit_list_set_in_clicked),
                     view);
    gtk_box_pack_start(GTK_BOX(rangebar), view->set_in_button, FALSE, FALSE, 0);

    separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(rangebar), separator, FALSE, FALSE, 4);

    item = gvr_edit_list_range_label("Out");
    gtk_box_pack_start(GTK_BOX(rangebar), item, FALSE, FALSE, 2);

    view->out_spin = gtk_spin_button_new_with_range(0, 0, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(view->out_spin), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(view->out_spin), 8);
    g_signal_connect(view->out_spin,
                     "value-changed",
                     G_CALLBACK(gvr_edit_list_spin_changed),
                     view);
    gtk_box_pack_start(GTK_BOX(rangebar), view->out_spin, FALSE, FALSE, 0);

    view->out_time_label = gvr_edit_list_range_label("00:00:00:00");
    gtk_box_pack_start(GTK_BOX(rangebar), view->out_time_label, FALSE, FALSE, 2);

    view->set_out_button = gvr_edit_list_image_button("button_gotoend.png",
                                                       TRUE,
                                                       "Set Out",
                                                       "Set the inclusive selection end to the current playhead (O).");
    g_signal_connect(view->set_out_button,
                     "clicked",
                     G_CALLBACK(gvr_edit_list_set_out_clicked),
                     view);
    gtk_box_pack_start(GTK_BOX(rangebar), view->set_out_button, FALSE, FALSE, 0);

    separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(rangebar), separator, FALSE, FALSE, 4);

    view->select_all_button = gvr_edit_list_image_button("icon_copy.png",
                                                          FALSE,
                                                          "Select All",
                                                          "Select the complete edit list (Ctrl+A).");
    g_signal_connect(view->select_all_button,
                     "clicked",
                     G_CALLBACK(gvr_edit_list_select_all_clicked),
                     view);
    gtk_box_pack_start(GTK_BOX(rangebar), view->select_all_button, FALSE, FALSE, 0);

    view->clear_selection_button = gvr_edit_list_image_button("icon_clear.png",
                                                               FALSE,
                                                               "Clear Selection",
                                                               "Clear the frame selection (Esc).");
    g_signal_connect(view->clear_selection_button,
                     "clicked",
                     G_CALLBACK(gvr_edit_list_clear_clicked),
                     view);
    gtk_box_pack_start(GTK_BOX(rangebar), view->clear_selection_button, FALSE, FALSE, 0);

    view->clipboard_label = gtk_label_new("Clipboard empty");
    g_object_set(G_OBJECT(view->clipboard_label), "xalign", 1.0f, NULL);
    gtk_label_set_single_line_mode(GTK_LABEL(view->clipboard_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(view->clipboard_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(view->clipboard_label), 28);
    gtk_label_set_max_width_chars(GTK_LABEL(view->clipboard_label), 28);
    gtk_widget_set_halign(view->clipboard_label, GTK_ALIGN_END);
    gtk_widget_set_hexpand(view->clipboard_label, TRUE);
    gtk_box_pack_end(GTK_BOX(rangebar), view->clipboard_label, TRUE, TRUE, 4);

    view->paste_popover = gtk_popover_new(view->paste_button);
    popover_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(popover_box), 6);
    gtk_container_add(GTK_CONTAINER(view->paste_popover), popover_box);

    item = gtk_label_new("Paste at frame");
    gtk_box_pack_start(GTK_BOX(popover_box), item, FALSE, FALSE, 0);

    view->paste_frame_spin = gtk_spin_button_new_with_range(0, 0, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(view->paste_frame_spin), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(view->paste_frame_spin), 8);
    gtk_box_pack_start(GTK_BOX(popover_box), view->paste_frame_spin, FALSE, FALSE, 0);

    paste_now = gvr_edit_list_image_button("icon_paste.png",
                                            FALSE,
                                            "Paste",
                                            "Paste at the entered frame.");
    g_signal_connect(paste_now,
                     "clicked",
                     G_CALLBACK(gvr_edit_list_paste_explicit_clicked),
                     view);
    gtk_box_pack_start(GTK_BOX(popover_box), paste_now, FALSE, FALSE, 0);
    gtk_widget_show_all(popover_box);

    g_signal_connect(view,
                     "key-press-event",
                     G_CALLBACK(gvr_edit_list_key_press),
                     view);

    gtk_widget_show_all(toolbar);
    gtk_widget_show_all(statusbar);
    gtk_widget_show_all(timeline_box);
    gtk_widget_show_all(scrolled);
    gtk_widget_show_all(rangebar);

    gvr_edit_list_register_media_drop(GTK_WIDGET(view), view);
    gvr_edit_list_register_media_drop(view->overview, view);
    gvr_edit_list_register_media_drop(view->navigator, view);
    gvr_edit_list_register_media_drop(view->tree, view);

    gvr_edit_list_update_summary(view);
    gvr_edit_list_update_range_labels(view);
    gvr_edit_list_update_clipboard_label(view);
    gvr_edit_list_update_sensitivity(view);
    gvr_edit_list_sync_timeline_controls(view);
}

GtkWidget *gvr_edit_list_view_new(void)
{
    return g_object_new(GVR_TYPE_EDIT_LIST_VIEW, NULL);
}

void gvr_edit_list_view_clear(GtkWidget *widget)
{
    GvrEditListView *view;

    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return;

    view = GVR_EDIT_LIST_VIEW(widget);
    g_ptr_array_set_size(view->segments, 0);
    g_hash_table_remove_all(view->separator_sets);
    g_hash_table_remove_all(view->region_sets);
    view->separators = gvr_edit_list_separator_set(view, -1, TRUE);
    view->regions = gvr_edit_list_region_set(view, -1, TRUE);
    view->next_separator_id = 1;
    view->selected_separator_id = 0;
    gtk_list_store_clear(view->store);

    view->sample_id = -1;
    view->source_file_count = 0;
    view->total_frames = 0;
    view->playhead = 0;
    view->selection_active = FALSE;
    view->selection_in = 0;
    view->selection_out = 0;
    view->selected_segment = -1;
    view->clipboard_valid = FALSE;
    view->clipboard_cut = FALSE;
    view->clipboard_frames = 0;
    view->clipboard_source_in = 0;
    view->clipboard_source_out = 0;
    view->timeline_zoom = GVR_EDIT_LIST_ZOOM_MIN;
    view->timeline_view_start = 0;
    view->drag_mode = GVR_EDIT_LIST_DRAG_NONE;
    view->drag_moved = FALSE;
    view->snap_visible = FALSE;

    gtk_spin_button_set_range(GTK_SPIN_BUTTON(view->in_spin), 0, 0);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(view->out_spin), 0, 0);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(view->paste_frame_spin), 0, 0);

    gvr_edit_list_sync_spins(view);
    gvr_edit_list_update_summary(view);
    gvr_edit_list_update_clipboard_label(view);
    gvr_edit_list_update_sensitivity(view);
    gvr_edit_list_sync_timeline_controls(view);
    gvr_edit_list_queue_timeline_draw(view);
}

void gvr_edit_list_view_set_sample(GtkWidget *widget,
                                   int sample_id,
                                   int total_frames,
                                   double fps)
{
    GvrEditListView *view;
    int max_frame;
    int old_sample_id;
    gint i;

    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return;

    view = GVR_EDIT_LIST_VIEW(widget);
    old_sample_id = view->sample_id;
    view->sample_id = sample_id;
    if(old_sample_id != sample_id) {
        view->separators = gvr_edit_list_separator_set(view, sample_id, TRUE);
        view->regions = gvr_edit_list_region_set(view, sample_id, TRUE);
        view->selected_separator_id = 0;
        view->drag_separator_id = 0;
    }
    view->total_frames = MAX(0, total_frames);
    view->fps = fps > 0.0 ? fps : 25.0;
    max_frame = gvr_edit_list_frame_max(view);

    if(view->total_frames <= 0) {
        view->selection_active = FALSE;
        view->selection_in = 0;
        view->selection_out = 0;
        view->selected_segment = -1;
    }

    view->playhead = gvr_edit_list_clamp_frame(view, view->playhead);
    if(view->selection_active) {
        view->selection_in = gvr_edit_list_clamp_frame(view, view->selection_in);
        view->selection_out = gvr_edit_list_clamp_frame(view, view->selection_out);
        if(view->selection_out < view->selection_in)
            view->selection_out = view->selection_in;
    }

    for(i = (gint)view->separators->len - 1; i >= 0; i--) {
        GvrEditListSeparatorData *separator = g_ptr_array_index(view->separators, (guint)i);
        if(view->total_frames <= 0)
            g_ptr_array_remove_index(view->separators, (guint)i);
        else
            separator->frame = gvr_edit_list_clampi(separator->frame, 0, view->total_frames);
    }

    gvr_edit_list_prune_regions(view);

    gtk_spin_button_set_range(GTK_SPIN_BUTTON(view->in_spin), 0, max_frame);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(view->out_spin), 0, max_frame);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(view->paste_frame_spin),
                              0,
                              MAX(0, view->total_frames));

    gvr_edit_list_sync_spins(view);
    gvr_edit_list_update_playing_rows(view);
    gvr_edit_list_update_summary(view);
    gvr_edit_list_update_sensitivity(view);
    gvr_edit_list_sync_timeline_controls(view);
    gvr_edit_list_queue_timeline_draw(view);
}

void gvr_edit_list_view_set_segments(GtkWidget *widget,
                                     const GvrEditListSegment *segments,
                                     guint count,
                                     guint source_file_count)
{
    GvrEditListView *view;
    int derived_total = 0;
    int preserve_segment;
    guint i;

    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return;

    view = GVR_EDIT_LIST_VIEW(widget);
    preserve_segment = view->selected_segment;
    view->source_file_count = (int)source_file_count;

    view->syncing_tree = TRUE;
    gtk_list_store_clear(view->store);
    g_ptr_array_set_size(view->segments, 0);

    for(i = 0; segments && i < count; i++) {
        const GvrEditListSegment *source = &segments[i];
        GvrEditListSegmentData *segment = g_new0(GvrEditListSegmentData, 1);
        GtkTreeIter iter;
        gchar *edl_range;
        gchar *file_range;
        int length;

        segment->index = source->index;
        segment->filename = g_strdup(source->filename ? source->filename : "(missing)");
        segment->color_index = gvr_edit_list_file_color_index(segment->filename);
        segment->timeline_in = MAX(0, source->timeline_in);
        segment->timeline_out = MAX(segment->timeline_in, source->timeline_out);
        segment->file_in = MAX(0, source->file_in);
        segment->file_out = MAX(segment->file_in, source->file_out);
        segment->fourcc = g_strdup(source->fourcc ? source->fourcc : "");

        derived_total = MAX(derived_total, segment->timeline_out + 1);
        length = segment->timeline_out - segment->timeline_in + 1;
        edl_range = gvr_edit_list_range_text(segment->timeline_in,
                                             segment->timeline_out);
        file_range = gvr_edit_list_range_text(segment->file_in,
                                              segment->file_out);

        g_ptr_array_add(view->segments, segment);
        gtk_list_store_append(view->store, &iter);
        gtk_list_store_set(view->store,
                           &iter,
                           MODEL_INDEX, segment->index + 1,
                           MODEL_FILENAME, segment->filename,
                           MODEL_EDL_RANGE, edl_range,
                           MODEL_FILE_RANGE, file_range,
                           MODEL_LENGTH, length,
                           MODEL_FOURCC, segment->fourcc,
                           MODEL_TIMELINE_IN, segment->timeline_in,
                           MODEL_TIMELINE_OUT, segment->timeline_out,
                           MODEL_ARRAY_POS, (int)i,
                           MODEL_PLAYING, view->playhead >= segment->timeline_in &&
                                          view->playhead <= segment->timeline_out,
                           -1);

        g_free(file_range);
        g_free(edl_range);
    }

    view->syncing_tree = FALSE;

    gvr_edit_list_view_set_sample(widget,
                                  view->sample_id,
                                  derived_total,
                                  view->fps);

    view->selected_segment = -1;
    if(preserve_segment >= 0) {
        GtkTreeModel *model = GTK_TREE_MODEL(view->store);
        GtkTreeIter iter;
        gboolean valid = gtk_tree_model_get_iter_first(model, &iter);

        while(valid) {
            int array_pos;
            gtk_tree_model_get(model, &iter, MODEL_ARRAY_POS, &array_pos, -1);
            if(array_pos >= 0 && (guint)array_pos < view->segments->len &&
               ((GvrEditListSegmentData *)
                g_ptr_array_index(view->segments, array_pos))->index == preserve_segment)
            {
                view->syncing_tree = TRUE;
                gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree)),
                                               &iter);
                view->syncing_tree = FALSE;
                view->selected_segment = preserve_segment;
                break;
            }
            valid = gtk_tree_model_iter_next(model, &iter);
        }
    }

    gvr_edit_list_queue_timeline_draw(view);
}

void gvr_edit_list_view_set_playhead(GtkWidget *widget, int frame)
{
    GvrEditListView *view;
    GtkAllocation allocation;
    int old_frame;
    gboolean viewport_changed = FALSE;

    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return;

    view = GVR_EDIT_LIST_VIEW(widget);
    frame = gvr_edit_list_clamp_frame(view, frame);
    if(frame == view->playhead)
        return;

    old_frame = view->playhead;
    view->playhead = frame;

    if(view->timeline_zoom > 1.001 &&
       view->drag_mode == GVR_EDIT_LIST_DRAG_NONE &&
       !gvr_edit_list_frame_visible(view, frame))
    {
        int visible = gvr_edit_list_visible_frames(view);
        view->timeline_view_start = gvr_edit_list_clampi(frame - (visible / 2),
                                                        0,
                                                        gvr_edit_list_view_max_start(view));
        gvr_edit_list_sync_timeline_controls(view);
        viewport_changed = TRUE;
    }

    gvr_edit_list_update_playing_rows(view);
    gvr_edit_list_update_summary(view);

    if(viewport_changed) {
        gvr_edit_list_queue_timeline_draw(view);
        return;
    }

    gtk_widget_get_allocation(view->overview, &allocation);
    if(allocation.width > 1 && view->total_frames > 0) {
        if(gvr_edit_list_frame_visible(view, old_frame)) {
            int old_x = (int)floor(gvr_edit_list_frame_to_x(view,
                                                            old_frame,
                                                            allocation.width));
            gtk_widget_queue_draw_area(view->overview,
                                       MAX(0, old_x - 5),
                                       0,
                                       11,
                                       allocation.height);
        }
        if(gvr_edit_list_frame_visible(view, frame)) {
            int new_x = (int)floor(gvr_edit_list_frame_to_x(view,
                                                            frame,
                                                            allocation.width));
            gtk_widget_queue_draw_area(view->overview,
                                       MAX(0, new_x - 5),
                                       0,
                                       11,
                                       allocation.height);
        }
    }
    else {
        gvr_edit_list_queue_timeline_draw(view);
    }
}

void gvr_edit_list_view_set_selection(GtkWidget *widget,
                                      int in_frame,
                                      int out_frame,
                                      gboolean active)
{
    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return;

    gvr_edit_list_set_selection_internal(GVR_EDIT_LIST_VIEW(widget),
                                         in_frame,
                                         out_frame,
                                         active,
                                         FALSE);
}

void gvr_edit_list_view_clear_selection(GtkWidget *widget)
{
    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return;

    gvr_edit_list_set_selection_internal(GVR_EDIT_LIST_VIEW(widget),
                                         0,
                                         0,
                                         FALSE,
                                         FALSE);
}

void gvr_edit_list_view_set_clipboard(GtkWidget *widget,
                                      gboolean valid,
                                      gboolean was_cut,
                                      int frame_count,
                                      int source_in,
                                      int source_out)
{
    GvrEditListView *view;

    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return;

    view = GVR_EDIT_LIST_VIEW(widget);
    view->clipboard_valid = valid ? TRUE : FALSE;
    view->clipboard_cut = was_cut ? TRUE : FALSE;
    view->clipboard_frames = MAX(0, frame_count);
    view->clipboard_source_in = MAX(0, source_in);
    view->clipboard_source_out = MAX(view->clipboard_source_in, source_out);

    if(!view->clipboard_valid || view->clipboard_frames <= 0) {
        view->clipboard_valid = FALSE;
        view->clipboard_cut = FALSE;
        view->clipboard_frames = 0;
        view->clipboard_source_in = 0;
        view->clipboard_source_out = 0;
    }

    gvr_edit_list_update_clipboard_label(view);
    gvr_edit_list_update_sensitivity(view);
}

void gvr_edit_list_view_set_editable(GtkWidget *widget, gboolean editable)
{
    GvrEditListView *view;

    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return;

    view = GVR_EDIT_LIST_VIEW(widget);
    view->editable = editable ? TRUE : FALSE;
    gvr_edit_list_update_sensitivity(view);
}

int gvr_edit_list_view_get_playhead(GtkWidget *widget)
{
    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return 0;

    return GVR_EDIT_LIST_VIEW(widget)->playhead;
}

gboolean gvr_edit_list_view_get_selection(GtkWidget *widget,
                                          int *in_frame,
                                          int *out_frame)
{
    GvrEditListView *view;

    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return FALSE;

    view = GVR_EDIT_LIST_VIEW(widget);
    if(!gvr_edit_list_selection_valid(view))
        return FALSE;

    if(in_frame)
        *in_frame = view->selection_in;
    if(out_frame)
        *out_frame = view->selection_out;
    return TRUE;
}

gboolean gvr_edit_list_view_get_selected_segment(GtkWidget *widget,
                                                  int *segment_index)
{
    GvrEditListView *view;

    if(!GVR_IS_EDIT_LIST_VIEW(widget))
        return FALSE;

    view = GVR_EDIT_LIST_VIEW(widget);
    if(view->selected_segment < 0)
        return FALSE;

    if(segment_index)
        *segment_index = view->selected_segment;
    return TRUE;
}
