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
#include <config.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <veejaycore/vims.h>
#include "gtkvimshistoryview.h"


static GtkTargetEntry gvr_vims_message_drag_targets[] = {
    { (gchar *)GVR_VIMS_HISTORY_DND_TARGET, GTK_TARGET_SAME_APP, 0 }
};

static int gvr_vims_history_last_integer(const char *message, int fallback)
{
    int result = fallback;
    const char *p = message;

    while(p && *p) {
        char *end = NULL;
        long value;

        while(*p && !g_ascii_isdigit(*p) && *p != '-')
            p++;
        if(!*p)
            break;

        value = strtol(p, &end, 10);
        if(end == p) {
            p++;
            continue;
        }

        result = (int)value;
        p = end;
    }

    return result;
}

static void gvr_vims_history_format_label(int id,
                                          const char *message,
                                          char *label,
                                          gsize label_size)
{
    int value = gvr_vims_history_last_integer(message, 0);

    if(id == VIMS_SAMPLE_HOLD_FRAME) {
        g_snprintf(label, label_size, "HOLD %d", value);
        return;
    }
    if(id == VIMS_VIDEO_SET_SPEED ||
       id == VIMS_VIDEO_SET_SPEEDK ||
       id == VIMS_SAMPLE_MIX_SET_SPEED ||
       id == VIMS_STREAM_BUFFER_SET_SPEED) {
        g_snprintf(label, label_size, "SPEED %d", value);
        return;
    }
    if(id == VIMS_VIDEO_SET_SLOW ||
       id == VIMS_SAMPLE_MIX_SET_DUP ||
       id == VIMS_STREAM_BUFFER_SET_SLOW) {
        g_snprintf(label, label_size, "SLOW %d", value);
        return;
    }
    if(id == VIMS_VIDEO_PLAY_FORWARD ||
       id == VIMS_STREAM_BUFFER_FORWARD) {
        g_strlcpy(label, "FWD", label_size);
        return;
    }
    if(id == VIMS_VIDEO_PLAY_BACKWARD ||
       id == VIMS_STREAM_BUFFER_BACKWARD) {
        g_strlcpy(label, "REV", label_size);
        return;
    }
    if(id == VIMS_VIDEO_PLAY_STOP ||
       id == VIMS_STREAM_BUFFER_STOP) {
        g_strlcpy(label, "STOP", label_size);
        return;
    }
    if(id == VIMS_VIDEO_SKIP_FRAME) {
        g_strlcpy(label, "JUMP +1", label_size);
        return;
    }
    if(id == VIMS_VIDEO_PREV_FRAME) {
        g_strlcpy(label, "JUMP -1", label_size);
        return;
    }
    if(id == VIMS_VIDEO_SKIP_SECOND) {
        g_strlcpy(label, "JUMP +1s", label_size);
        return;
    }
    if(id == VIMS_VIDEO_PREV_SECOND) {
        g_strlcpy(label, "JUMP -1s", label_size);
        return;
    }
    if(id == VIMS_VIDEO_GOTO_START) {
        g_strlcpy(label, "JUMP START", label_size);
        return;
    }
    if(id == VIMS_VIDEO_GOTO_END) {
        g_strlcpy(label, "JUMP END", label_size);
        return;
    }
    if(id == VIMS_STREAM_BUFFER_SKIP_FRAME) {
        g_snprintf(label, label_size, "JUMP %d", value);
        return;
    }
    if(id == VIMS_STREAM_BUFFER_SKIP_SECOND) {
        g_snprintf(label, label_size, "JUMP %ds", value);
        return;
    }
    if(id == VIMS_STREAM_BUFFER_PREV_SECOND) {
        g_snprintf(label, label_size, "JUMP -%ds", ABS(value));
        return;
    }
    if(id == VIMS_STREAM_BUFFER_SET_FRAME) {
        g_snprintf(label, label_size, "JUMP %d", value);
        return;
    }

    g_snprintf(label, label_size, "%03d", id);
}

static void gvr_vims_history_draw_text(cairo_t *cr,
                                       const char *text,
                                       double x,
                                       double y,
                                       double size,
                                       cairo_font_weight_t weight)
{
    cairo_save(cr);
    cairo_select_font_face(cr,
                           "Monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           weight);
    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text ? text : "");
    cairo_restore(cr);
}

static void gvr_vims_history_draw_cell_text(cairo_t *cr,
                                            const char *text,
                                            double x,
                                            double baseline,
                                            double width,
                                            double font_size)
{
    char buffer[40];
    cairo_text_extents_t ext;
    gsize length;

    g_strlcpy(buffer, text ? text : "", sizeof(buffer));
    length = strlen(buffer);

    cairo_save(cr);
    cairo_select_font_face(cr,
                           "Monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, font_size);
    cairo_text_extents(cr, buffer, &ext);

    while(length > 1 && ext.width > width - 8.0) {
        buffer[--length] = '\0';
        cairo_text_extents(cr, buffer, &ext);
    }

    cairo_move_to(cr, x + 4.0, baseline);
    cairo_show_text(cr, buffer);
    cairo_restore(cr);
}

#define GVR_HISTORY_ROW_HEIGHT_MIN 24
#define GVR_HISTORY_HEADER_HEIGHT_MIN 23
#define GVR_HISTORY_ID_WIDTH 62
#define GVR_HISTORY_MAX_ENTRIES 512

typedef struct {
    int vims_id;
    char *message;
    char *description;
    char label[32];
    guint count;
} GvrVimsHistoryEntry;

struct _GvrVimsHistoryView {
    GtkBox parent_instance;
    GtkWidget *area;
    GtkWidget *scrollbar;
    GtkAdjustment *vadjustment;
    GPtrArray *entries;
    int selected_index;
};

struct _GvrVimsHistoryViewClass {
    GtkBoxClass parent_class;
};

enum {
    HISTORY_SIGNAL_MESSAGE_ACTIVATED,
    HISTORY_SIGNAL_LAST
};

static guint gvr_vims_history_signals[HISTORY_SIGNAL_LAST];

G_DEFINE_TYPE(GvrVimsHistoryView,
              gvr_vims_history_view,
              GTK_TYPE_BOX)


static double gvr_vims_history_font_points(GtkWidget *widget)
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

static double gvr_vims_history_font_px(GvrVimsHistoryView *view,
                                        double scale)
{
    return MAX(7.0,
               gvr_vims_history_font_points(view ? view->area : NULL) *
               (96.0 / 72.0) * scale);
}

static int gvr_vims_history_row_height(GvrVimsHistoryView *view)
{
    return MAX(GVR_HISTORY_ROW_HEIGHT_MIN,
               (int)ceil(gvr_vims_history_font_px(view, 0.75) + 14.0));
}

static int gvr_vims_history_header_height(GvrVimsHistoryView *view)
{
    return MAX(GVR_HISTORY_HEADER_HEIGHT_MIN,
               (int)ceil(gvr_vims_history_font_px(view, 0.75) + 12.0));
}

static void gvr_vims_history_entry_free(gpointer data)
{
    GvrVimsHistoryEntry *entry = data;

    if(!entry)
        return;

    g_free(entry->message);
    g_free(entry->description);
    g_free(entry);
}

static int gvr_vims_history_visible_rows(GvrVimsHistoryView *view)
{
    GtkAllocation allocation;
    int rows;

    gtk_widget_get_allocation(view->area, &allocation);
    rows = (allocation.height - gvr_vims_history_header_height(view)) /
           gvr_vims_history_row_height(view);
    return MAX(1, rows);
}

static void gvr_vims_history_update_adjustment(GvrVimsHistoryView *view)
{
    const int count = view->entries ? (int)view->entries->len : 0;
    const int visible = gvr_vims_history_visible_rows(view);
    const int page = MIN(MAX(1, count), visible);
    double value = gtk_adjustment_get_value(view->vadjustment);

    gtk_adjustment_configure(view->vadjustment,
                             MIN(value, MAX(0, count - page)),
                             0,
                             MAX(1, count),
                             1,
                             MAX(1, page - 1),
                             page);
}

static GvrVimsHistoryEntry *gvr_vims_history_entry_at_row(
        GvrVimsHistoryView *view,
        int display_row,
        int *index)
{
    int top;
    int actual;

    if(!view || !view->entries)
        return NULL;

    top = (int)gtk_adjustment_get_value(view->vadjustment);
    actual = (int)view->entries->len - 1 - (top + display_row);
    if(actual < 0 || actual >= (int)view->entries->len)
        return NULL;

    if(index)
        *index = actual;
    return g_ptr_array_index(view->entries, actual);
}

static gboolean gvr_vims_history_draw(GtkWidget *widget,
                                      cairo_t *cr,
                                      gpointer user_data)
{
    GvrVimsHistoryView *view = GVR_VIMS_HISTORY_VIEW(user_data);
    GtkAllocation allocation;
    int visible;
    int row_height;
    int header_height;
    double font_size;
    double small_font_size;
    double header_baseline;
    double descr_width;

    gtk_widget_get_allocation(widget, &allocation);
    visible = gvr_vims_history_visible_rows(view);
    row_height = gvr_vims_history_row_height(view);
    header_height = gvr_vims_history_header_height(view);
    font_size = gvr_vims_history_font_px(view, 0.75);
    small_font_size = gvr_vims_history_font_px(view, 0.68);
    header_baseline = (header_height + font_size) * 0.5 - 1.0;
    descr_width = MAX(120.0,
                      MIN(300.0,
                          allocation.width * 0.38));

    cairo_set_source_rgb(cr, 0.050, 0.054, 0.064);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.105, 0.115, 0.135);
    cairo_rectangle(cr,
                    0,
                    0,
                    allocation.width,
                    header_height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.82, 0.85, 0.90);
    gvr_vims_history_draw_text(cr,
                          "VIMS",
                          8,
                          header_baseline,
                          font_size,
                          CAIRO_FONT_WEIGHT_BOLD);
    gvr_vims_history_draw_text(cr,
                          "DESCRIPTION",
                          GVR_HISTORY_ID_WIDTH + 6,
                          header_baseline,
                          font_size,
                          CAIRO_FONT_WEIGHT_BOLD);
    gvr_vims_history_draw_text(cr,
                          "COMMAND",
                          GVR_HISTORY_ID_WIDTH + descr_width + 10,
                          header_baseline,
                          font_size,
                          CAIRO_FONT_WEIGHT_BOLD);

    for(int row = 0; row <= visible; row++) {
        int index = -1;
        GvrVimsHistoryEntry *entry =
            gvr_vims_history_entry_at_row(view, row, &index);
        const double y = header_height + row * row_height;
        const double baseline = y + (row_height + font_size) * 0.5 - 1.0;
        char id_text[16];
        char count_text[24];

        if(!entry || y >= allocation.height)
            break;

        if(index == view->selected_index) {
            cairo_set_source_rgba(cr, 0.36, 0.43, 0.55, 0.34);
            cairo_rectangle(cr,
                            0,
                            y,
                            allocation.width,
                            row_height);
            cairo_fill(cr);
        }
        else if((row & 1) == 0) {
            cairo_set_source_rgba(cr, 0.12, 0.13, 0.15, 0.34);
            cairo_rectangle(cr,
                            0,
                            y,
                            allocation.width,
                            row_height);
            cairo_fill(cr);
        }

        g_snprintf(id_text, sizeof(id_text), "%03d", entry->vims_id);
        cairo_set_source_rgb(cr, 0.62, 0.82, 1.0);
        gvr_vims_history_draw_text(cr,
                              id_text,
                              8,
                              baseline,
                              font_size,
                              CAIRO_FONT_WEIGHT_BOLD);

        cairo_set_source_rgb(cr, 0.78, 0.81, 0.86);
        gvr_vims_history_draw_cell_text(
            cr,
            entry->description && entry->description[0] ?
                entry->description : entry->label,
            GVR_HISTORY_ID_WIDTH,
            baseline,
            descr_width,
            font_size);

        cairo_set_source_rgb(cr, 0.72, 0.78, 0.90);
        gvr_vims_history_draw_cell_text(
            cr,
            entry->message,
            GVR_HISTORY_ID_WIDTH + descr_width + 4,
            baseline,
            allocation.width -
                (GVR_HISTORY_ID_WIDTH + descr_width + 4),
            font_size);

        if(entry->count > 1) {
            g_snprintf(count_text,
                       sizeof(count_text),
                       "×%u",
                       entry->count);
            cairo_set_source_rgb(cr, 1.0, 0.68, 0.20);
            gvr_vims_history_draw_text(cr,
                                  count_text,
                                  allocation.width - 38,
                                  baseline,
                                  small_font_size,
                                  CAIRO_FONT_WEIGHT_BOLD);
        }
    }

    cairo_set_source_rgba(cr, 0.18, 0.20, 0.24, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, GVR_HISTORY_ID_WIDTH + 0.5, 0);
    cairo_line_to(cr,
                  GVR_HISTORY_ID_WIDTH + 0.5,
                  allocation.height);
    cairo_move_to(cr,
                  GVR_HISTORY_ID_WIDTH + descr_width + 0.5,
                  0);
    cairo_line_to(cr,
                  GVR_HISTORY_ID_WIDTH + descr_width + 0.5,
                  allocation.height);
    cairo_stroke(cr);

    for(int row = 0; row <= visible; row++) {
        const double y = header_height +
                         row * row_height + 0.5;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, allocation.width, y);
    }
    cairo_stroke(cr);

    return FALSE;
}

static void gvr_vims_history_size_allocate(GtkWidget *widget,
                                           GtkAllocation *allocation,
                                           gpointer user_data)
{
    (void)widget;
    (void)allocation;
    gvr_vims_history_update_adjustment(
        GVR_VIMS_HISTORY_VIEW(user_data));
}

static void gvr_vims_history_adjustment_changed(GtkAdjustment *adjustment,
                                                gpointer user_data)
{
    (void)adjustment;
    gtk_widget_queue_draw(GVR_VIMS_HISTORY_VIEW(user_data)->area);
}

static gboolean gvr_vims_history_scroll(GtkWidget *widget,
                                        GdkEventScroll *event,
                                        gpointer user_data)
{
    GvrVimsHistoryView *view = GVR_VIMS_HISTORY_VIEW(user_data);
    double value = gtk_adjustment_get_value(view->vadjustment);
    double delta = 0.0;

    (void)widget;

    if(event->direction == GDK_SCROLL_UP)
        delta = -3.0;
    else if(event->direction == GDK_SCROLL_DOWN)
        delta = 3.0;
#if GTK_CHECK_VERSION(3,4,0)
    else if(event->direction == GDK_SCROLL_SMOOTH)
        delta = event->delta_y * 3.0;
#endif

    if(delta == 0.0)
        return FALSE;

    gtk_adjustment_set_value(view->vadjustment, value + delta);
    return TRUE;
}


static void gvr_vims_history_copy_menu_activate(GtkMenuItem *item,
                                                gpointer user_data)
{
    const char *message = g_object_get_data(G_OBJECT(item),
                                            "gvr-vims-message");
    (void)user_data;

    if(message)
        gtk_clipboard_set_text(
            gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
            message,
            -1);
}

static gboolean gvr_vims_history_button_press(GtkWidget *widget,
                                              GdkEventButton *event,
                                              gpointer user_data)
{
    GvrVimsHistoryView *view = GVR_VIMS_HISTORY_VIEW(user_data);
    int display_row;
    int index = -1;
    GvrVimsHistoryEntry *entry;

    gtk_widget_grab_focus(widget);

    if(event->y < gvr_vims_history_header_height(view))
        return FALSE;

    display_row = (int)((event->y - gvr_vims_history_header_height(view)) /
                        gvr_vims_history_row_height(view));
    entry = gvr_vims_history_entry_at_row(view,
                                          display_row,
                                          &index);
    if(!entry)
        return FALSE;

    view->selected_index = index;
    gtk_widget_queue_draw(view->area);

    if(event->button == 1 && event->type == GDK_2BUTTON_PRESS) {
        g_signal_emit(view,
                      gvr_vims_history_signals[
                          HISTORY_SIGNAL_MESSAGE_ACTIVATED],
                      0,
                      entry->message);
        return TRUE;
    }

    if(event->button == 3) {
        GtkWidget *menu = gtk_menu_new();
        GtkWidget *copy = gtk_menu_item_new_with_label("Copy command");
        GtkWidget *clear = gtk_menu_item_new_with_label("Clear history");

        g_object_set_data_full(G_OBJECT(copy),
                               "gvr-vims-message",
                               g_strdup(entry->message),
                               g_free);
        g_signal_connect(copy,
                         "activate",
                         G_CALLBACK(gvr_vims_history_copy_menu_activate),
                         NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), clear);
        g_signal_connect_swapped(clear,
                                 "activate",
                                 G_CALLBACK(gvr_vims_history_view_clear),
                                 view);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }

    /*
     * Keep the row selected, but do not consume an ordinary left press.
     * gtk_drag_source_set() must receive that press to start a drag.
     */
    return FALSE;
}

static gboolean gvr_vims_history_key_press(GtkWidget *widget,
                                           GdkEventKey *event,
                                           gpointer user_data)
{
    GvrVimsHistoryView *view = GVR_VIMS_HISTORY_VIEW(user_data);
    int count = view->entries ? (int)view->entries->len : 0;

    (void)widget;

    if(count <= 0)
        return FALSE;

    if(view->selected_index < 0)
        view->selected_index = count - 1;

    switch(event->keyval) {
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            view->selected_index = MIN(count - 1,
                                       view->selected_index + 1);
            break;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            view->selected_index = MAX(0,
                                       view->selected_index - 1);
            break;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter: {
            GvrVimsHistoryEntry *entry =
                g_ptr_array_index(view->entries,
                                  view->selected_index);
            g_signal_emit(view,
                          gvr_vims_history_signals[
                              HISTORY_SIGNAL_MESSAGE_ACTIVATED],
                          0,
                          entry->message);
            return TRUE;
        }
        default:
            return FALSE;
    }

    gtk_widget_queue_draw(view->area);
    return TRUE;
}

static gboolean gvr_vims_history_query_tooltip(GtkWidget *widget,
                                               gint x,
                                               gint y,
                                               gboolean keyboard_mode,
                                               GtkTooltip *tooltip,
                                               gpointer user_data)
{
    GvrVimsHistoryView *view = GVR_VIMS_HISTORY_VIEW(user_data);
    int index = view->selected_index;
    GvrVimsHistoryEntry *entry = NULL;
    char text[768];

    (void)widget;
    (void)x;

    if(!keyboard_mode) {
        int display_row;

        if(y < gvr_vims_history_header_height(view))
            return FALSE;
        display_row = (y - gvr_vims_history_header_height(view)) /
                      gvr_vims_history_row_height(view);
        entry = gvr_vims_history_entry_at_row(view,
                                              display_row,
                                              &index);
    }
    else if(index >= 0 &&
            index < (int)view->entries->len) {
        entry = g_ptr_array_index(view->entries, index);
    }

    if(!entry)
        return FALSE;

    g_snprintf(text,
               sizeof(text),
               "VIMS %03d%s%s\nCommand: %s\nSeen %u time%s\n\nDrag this row onto a visible Pattern cell, or double-click/Enter to place it at the current Pattern cursor.",
               entry->vims_id,
               entry->description && entry->description[0] ? " — " : "",
               entry->description && entry->description[0] ? entry->description : "",
               entry->message,
               entry->count,
               entry->count == 1 ? "" : "s");
    gtk_tooltip_set_text(tooltip, text);
    return TRUE;
}

static void gvr_vims_history_drag_data_get(GtkWidget *widget,
                                           GdkDragContext *context,
                                           GtkSelectionData *selection,
                                           guint info,
                                           guint time,
                                           gpointer user_data)
{
    GvrVimsHistoryView *view = GVR_VIMS_HISTORY_VIEW(user_data);
    GvrVimsHistoryEntry *entry;

    (void)widget;
    (void)context;
    (void)info;
    (void)time;

    if(!view->entries ||
       view->selected_index < 0 ||
       view->selected_index >= (int)view->entries->len)
        return;

    entry = g_ptr_array_index(view->entries,
                              view->selected_index);

    if(entry && entry->message) {
        const guchar *payload =
            (const guchar *)entry->message;
        const gint payload_length =
            (gint)strlen(entry->message);

        gtk_selection_data_set(
            selection,
            gtk_selection_data_get_target(selection),
            8,
            payload,
            payload_length);
    }
}

static void gvr_vims_history_clear_clicked(GtkButton *button,
                                           gpointer user_data)
{
    (void)button;
    gvr_vims_history_view_clear(GTK_WIDGET(user_data));
}

static void gvr_vims_history_view_finalize(GObject *object)
{
    GvrVimsHistoryView *view = GVR_VIMS_HISTORY_VIEW(object);

    if(view->entries)
        g_ptr_array_free(view->entries, TRUE);

    G_OBJECT_CLASS(gvr_vims_history_view_parent_class)->finalize(object);
}


static void gvr_vims_history_view_style_updated(GtkWidget *widget)
{
    GvrVimsHistoryView *view = GVR_VIMS_HISTORY_VIEW(widget);

    GTK_WIDGET_CLASS(gvr_vims_history_view_parent_class)->style_updated(widget);
    if(view->area && view->vadjustment) {
        gvr_vims_history_update_adjustment(view);
        gtk_widget_queue_resize(view->area);
        gtk_widget_queue_draw(view->area);
    }
}

static void gvr_vims_history_view_class_init(
        GvrVimsHistoryViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    widget_class->style_updated = gvr_vims_history_view_style_updated;
    object_class->finalize = gvr_vims_history_view_finalize;

    gvr_vims_history_signals[HISTORY_SIGNAL_MESSAGE_ACTIVATED] =
        g_signal_new("message-activated",
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

static void gvr_vims_history_view_init(GvrVimsHistoryView *view)
{
    GtkWidget *toolbar;
    GtkWidget *body;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(view),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(view), 2);
    view->entries = g_ptr_array_new_with_free_func(
        gvr_vims_history_entry_free);
    view->selected_index = -1;

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(toolbar),
        "vims-history-toolbar");
    gtk_box_pack_start(GTK_BOX(view), toolbar, FALSE, FALSE, 0);
    gtk_widget_show(toolbar);

    body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(view), body, TRUE, TRUE, 0);
    gtk_widget_show(body);

    view->area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(view->area, TRUE);
    gtk_widget_set_has_tooltip(view->area, TRUE);
    gtk_widget_add_events(view->area,
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_SCROLL_MASK |
                          GDK_SMOOTH_SCROLL_MASK |
                          GDK_KEY_PRESS_MASK);
    gtk_box_pack_start(GTK_BOX(body), view->area, TRUE, TRUE, 0);
    g_signal_connect(view->area,
                     "draw",
                     G_CALLBACK(gvr_vims_history_draw),
                     view);
    g_signal_connect(view->area,
                     "size-allocate",
                     G_CALLBACK(gvr_vims_history_size_allocate),
                     view);
    g_signal_connect(view->area,
                     "scroll-event",
                     G_CALLBACK(gvr_vims_history_scroll),
                     view);
    g_signal_connect(view->area,
                     "button-press-event",
                     G_CALLBACK(gvr_vims_history_button_press),
                     view);
    g_signal_connect(view->area,
                     "key-press-event",
                     G_CALLBACK(gvr_vims_history_key_press),
                     view);
    g_signal_connect(view->area,
                     "query-tooltip",
                     G_CALLBACK(gvr_vims_history_query_tooltip),
                     view);
    gtk_drag_source_set(view->area,
                        GDK_BUTTON1_MASK,
                        gvr_vims_message_drag_targets,
                        G_N_ELEMENTS(gvr_vims_message_drag_targets),
                        GDK_ACTION_COPY);
    g_signal_connect(view->area,
                     "drag-data-get",
                     G_CALLBACK(gvr_vims_history_drag_data_get),
                     view);
    gtk_widget_show(view->area);

    view->vadjustment = GTK_ADJUSTMENT(
        gtk_adjustment_new(0, 0, 1, 1, 1, 1));
    g_signal_connect(view->vadjustment,
                     "value-changed",
                     G_CALLBACK(gvr_vims_history_adjustment_changed),
                     view);
    view->scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL,
                                        view->vadjustment);
    gtk_box_pack_start(GTK_BOX(body),
                       view->scrollbar,
                       FALSE,
                       FALSE,
                       0);
    gtk_widget_show(view->scrollbar);
}

GtkWidget *gvr_vims_history_view_new(void)
{
    return g_object_new(GVR_TYPE_VIMS_HISTORY_VIEW, NULL);
}

void gvr_vims_history_view_push(GtkWidget *widget,
                                int vims_id,
                                const char *message,
                                const char *description)
{
    GvrVimsHistoryView *view;
    GvrVimsHistoryEntry *entry;

    if(!GVR_IS_VIMS_HISTORY_VIEW(widget) ||
       !message || !message[0] ||
       vims_id < 0)
        return;

    view = GVR_VIMS_HISTORY_VIEW(widget);

    if(view->entries->len > 0) {
        entry = g_ptr_array_index(view->entries,
                                  view->entries->len - 1);
        if(entry && strcmp(entry->message, message) == 0) {
            entry->count++;
            if((!entry->description || !entry->description[0]) &&
               description && description[0])
            {
                g_free(entry->description);
                entry->description = g_strdup(description);
            }
            view->selected_index = (int)view->entries->len - 1;
            gtk_adjustment_set_value(view->vadjustment, 0);
            gtk_widget_queue_draw(view->area);
            return;
        }
    }

    entry = g_new0(GvrVimsHistoryEntry, 1);
    entry->vims_id = vims_id;
    entry->message = g_strdup(message);
    entry->description = g_strdup(description ? description : "");
    entry->count = 1;
    gvr_vims_history_format_label(vims_id,
                             message,
                             entry->label,
                             sizeof(entry->label));
    g_ptr_array_add(view->entries, entry);

    while(view->entries->len > GVR_HISTORY_MAX_ENTRIES)
        g_ptr_array_remove_index(view->entries, 0);

    view->selected_index = (int)view->entries->len - 1;
    gvr_vims_history_update_adjustment(view);
    gtk_adjustment_set_value(view->vadjustment, 0);
    gtk_widget_queue_draw(view->area);
}

void gvr_vims_history_view_clear(GtkWidget *widget)
{
    GvrVimsHistoryView *view;

    if(!GVR_IS_VIMS_HISTORY_VIEW(widget))
        return;

    view = GVR_VIMS_HISTORY_VIEW(widget);
    g_ptr_array_set_size(view->entries, 0);
    view->selected_index = -1;
    gtk_adjustment_set_value(view->vadjustment, 0);
    gvr_vims_history_update_adjustment(view);
    gtk_widget_queue_draw(view->area);
}
