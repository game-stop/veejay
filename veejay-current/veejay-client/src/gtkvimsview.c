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
#include <pango/pangocairo.h>
#include <string.h>
#include "gtkvimsview.h"

#define GVR_VIMS_ROW_HEIGHT 27
#define GVR_VIMS_HEADER_HEIGHT 25
#define GVR_VIMS_ID_WIDTH 62
#define GVR_VIMS_NAMESPACE_MIN_HEIGHT 120
#define GVR_VIMS_ACTIONS_MIN_HEIGHT 96
#define GVR_VIMS_WORKSPACE_MIN_HEIGHT 120

typedef enum {
    GVR_VIMS_LIST_NAMESPACE = 0,
    GVR_VIMS_LIST_ACTIONS,
    GVR_VIMS_LIST_MIDI
} GvrVimsListKind;

typedef struct {
    int event_id;
    int params;
    gchar *description;
    gchar *format;
} GvrVimsNamespaceEntry;

typedef struct {
    int event_id;
    int key;
    int modifier;
    gboolean is_bundle;
    gchar *description;
    gchar *format;
    gchar *args;
    gchar *modifier_text;
    gchar *key_text;
    gchar *bundle_text;
} GvrVimsActionEntry;

typedef struct {
    gchar *mapping_key;
    int event_type;
    int parameter;
    int extra;
    gchar *event_text;
    gchar *mode_text;
    gchar *parameter_text;
    gchar *widget;
    gchar *message;
} GvrVimsMidiEntry;

typedef struct {
    GvrVimsView *owner;
    GvrVimsListKind kind;
    GtkWidget *area;
    GtkWidget *scrollbar;
    GtkAdjustment *adjustment;
    int selected_index;
} GvrVimsList;

struct _GvrVimsView {
    GtkBox parent_instance;
    GPtrArray *namespace_entries;
    GPtrArray *action_entries;
    GPtrArray *midi_entries;
    GvrVimsList namespace_list;
    GvrVimsList action_list;
    GvrVimsList midi_list;
    GtkWidget *main_paned;
    GtkWidget *lower_paned;
    GtkWidget *registry_notebook;
    GtkWidget *midi_unbind_button;
    GtkWidget *command_entry;
    GtkWidget *command_send_button;
    GtkWidget *command_hint;
    GtkWidget *command_copy_button;
    GtkWidget *copy_request_button;
    GtkWidget *search_revealer;
    GtkWidget *search_entry;
    GtkWidget *search_status;
    GtkWidget *response_view;
    GtkWidget *editor_view;
    GtkWidget *workspace;
    GtkWidget *buttons[GVR_VIMS_VIEW_N_ACTIONS][2];
    guint button_count[GVR_VIMS_VIEW_N_ACTIONS];
    gchar *search_query;
    gchar *last_request;
    GvrVimsListKind search_kind;
    int search_index;
};

struct _GvrVimsViewClass {
    GtkBoxClass parent_class;
};

enum {
    SIGNAL_RUN_REQUESTED = 0,
    SIGNAL_ADD_REQUESTED,
    SIGNAL_BIND_REQUESTED,
    SIGNAL_UNBIND_REQUESTED,
    SIGNAL_NEW_BUNDLE_REQUESTED,
    SIGNAL_UPDATE_BUNDLE_REQUESTED,
    SIGNAL_DELETE_BUNDLE_REQUESTED,
    SIGNAL_LOAD_REQUESTED,
    SIGNAL_SAVE_REQUESTED,
    SIGNAL_CLEAR_EDITOR_REQUESTED,
    SIGNAL_CLEAR_RESPONSE_REQUESTED,
    SIGNAL_NAMESPACE_SELECTION_CHANGED,
    SIGNAL_ACTION_SELECTION_CHANGED,
    SIGNAL_NAMESPACE_ACTIVATED,
    SIGNAL_ACTION_ACTIVATED,
    SIGNAL_MIDI_REFRESH_REQUESTED,
    SIGNAL_MIDI_UNBIND_REQUESTED,
    SIGNAL_LAST
};

static guint gvr_vims_view_signals[SIGNAL_LAST];

static const guint gvr_vims_view_action_signals[GVR_VIMS_VIEW_N_ACTIONS] = {
    SIGNAL_RUN_REQUESTED,
    SIGNAL_ADD_REQUESTED,
    SIGNAL_BIND_REQUESTED,
    SIGNAL_UNBIND_REQUESTED,
    SIGNAL_NEW_BUNDLE_REQUESTED,
    SIGNAL_UPDATE_BUNDLE_REQUESTED,
    SIGNAL_DELETE_BUNDLE_REQUESTED,
    SIGNAL_LOAD_REQUESTED,
    SIGNAL_SAVE_REQUESTED,
    SIGNAL_CLEAR_EDITOR_REQUESTED,
    SIGNAL_CLEAR_RESPONSE_REQUESTED
};

G_DEFINE_TYPE(GvrVimsView, gvr_vims_view, GTK_TYPE_BOX)

static void gvr_vims_namespace_entry_free(gpointer data)
{
    GvrVimsNamespaceEntry *entry = data;
    if(!entry)
        return;
    g_free(entry->description);
    g_free(entry->format);
    g_free(entry);
}

static void gvr_vims_action_entry_free(gpointer data)
{
    GvrVimsActionEntry *entry = data;
    if(!entry)
        return;
    g_free(entry->description);
    g_free(entry->format);
    g_free(entry->args);
    g_free(entry->modifier_text);
    g_free(entry->key_text);
    g_free(entry->bundle_text);
    g_free(entry);
}

static void gvr_vims_midi_entry_free(gpointer data)
{
    GvrVimsMidiEntry *entry = data;
    if(!entry)
        return;
    g_free(entry->mapping_key);
    g_free(entry->event_text);
    g_free(entry->mode_text);
    g_free(entry->parameter_text);
    g_free(entry->widget);
    g_free(entry->message);
    g_free(entry);
}

static gboolean gvr_vims_string_contains(const char *text,
                                         const char *query)
{
    gchar *folded_text;
    gchar *folded_query;
    gboolean match;

    if(!text || !query || !query[0])
        return FALSE;

    folded_text = g_utf8_casefold(text, -1);
    folded_query = g_utf8_casefold(query, -1);
    match = strstr(folded_text, folded_query) != NULL;
    g_free(folded_text);
    g_free(folded_query);
    return match;
}

static gboolean gvr_vims_event_matches(int event_id,
                                       const char *description,
                                       const char *query)
{
    char id[16];
    char padded_id[16];

    if(!query || !query[0])
        return FALSE;

    g_snprintf(id, sizeof(id), "%d", event_id);
    g_snprintf(padded_id, sizeof(padded_id), "%03d", event_id);
    return strstr(id, query) != NULL ||
           strstr(padded_id, query) != NULL ||
           gvr_vims_string_contains(description, query);
}

static int gvr_vims_view_search_total(GvrVimsView *view)
{
    return (int)view->namespace_entries->len +
           (int)view->action_entries->len;
}

static gboolean gvr_vims_view_search_match_at(GvrVimsView *view,
                                              int flat_index,
                                              GvrVimsListKind *kind,
                                              int *index)
{
    const int namespace_count = (int)view->namespace_entries->len;

    if(flat_index < 0 || flat_index >= gvr_vims_view_search_total(view))
        return FALSE;

    if(flat_index < namespace_count) {
        GvrVimsNamespaceEntry *entry =
            g_ptr_array_index(view->namespace_entries, flat_index);
        if(kind)
            *kind = GVR_VIMS_LIST_NAMESPACE;
        if(index)
            *index = flat_index;
        return gvr_vims_event_matches(entry->event_id,
                                      entry->description,
                                      view->search_query);
    }
    else {
        const int action_index = flat_index - namespace_count;
        GvrVimsActionEntry *entry =
            g_ptr_array_index(view->action_entries, action_index);
        if(kind)
            *kind = GVR_VIMS_LIST_ACTIONS;
        if(index)
            *index = action_index;
        return gvr_vims_event_matches(entry->event_id,
                                      entry->description,
                                      view->search_query);
    }
}

static int gvr_vims_view_search_flat_index(GvrVimsView *view)
{
    if(view->search_index < 0)
        return -1;
    if(view->search_kind == GVR_VIMS_LIST_NAMESPACE)
        return view->search_index;
    return (int)view->namespace_entries->len + view->search_index;
}

static int gvr_vims_view_search_match_count(GvrVimsView *view,
                                            int selected_flat,
                                            int *ordinal)
{
    const int total = gvr_vims_view_search_total(view);
    int matches = 0;
    int selected_ordinal = 0;

    for(int i = 0; i < total; i++) {
        if(!gvr_vims_view_search_match_at(view, i, NULL, NULL))
            continue;
        matches++;
        if(i == selected_flat)
            selected_ordinal = matches;
    }
    if(ordinal)
        *ordinal = selected_ordinal;
    return matches;
}

static void gvr_vims_view_search_status(GvrVimsView *view,
                                        int selected_flat)
{
    int ordinal = 0;
    const int matches = gvr_vims_view_search_match_count(view,
                                                          selected_flat,
                                                          &ordinal);
    gchar *text;

    if(!view->search_query || !view->search_query[0])
        text = g_strdup("Type an event ID or description");
    else if(matches == 0)
        text = g_strdup("No matching VIMS events");
    else
        text = g_strdup_printf("%d of %d", ordinal, matches);

    gtk_label_set_text(GTK_LABEL(view->search_status), text);
    g_free(text);
}

static GPtrArray *gvr_vims_list_entries(GvrVimsList *list)
{
    if(list->kind == GVR_VIMS_LIST_NAMESPACE)
        return list->owner->namespace_entries;
    if(list->kind == GVR_VIMS_LIST_ACTIONS)
        return list->owner->action_entries;
    return list->owner->midi_entries;
}

static int gvr_vims_list_visible_rows(GvrVimsList *list)
{
    GtkAllocation allocation;
    gtk_widget_get_allocation(list->area, &allocation);
    return MAX(1, (allocation.height - GVR_VIMS_HEADER_HEIGHT) /
                  GVR_VIMS_ROW_HEIGHT);
}

static void gvr_vims_list_update_adjustment(GvrVimsList *list)
{
    GPtrArray *entries = gvr_vims_list_entries(list);
    const int count = entries ? (int)entries->len : 0;
    const int visible = gvr_vims_list_visible_rows(list);
    const int page = MIN(MAX(1, count), visible);
    double value = gtk_adjustment_get_value(list->adjustment);

    gtk_adjustment_configure(list->adjustment,
                             MIN(value, MAX(0, count - page)),
                             0,
                             MAX(1, count),
                             1,
                             MAX(1, page - 1),
                             page);
}

static gpointer gvr_vims_list_entry_at_row(GvrVimsList *list,
                                           int display_row,
                                           int *index)
{
    GPtrArray *entries = gvr_vims_list_entries(list);
    int actual;

    if(!entries)
        return NULL;
    actual = (int)gtk_adjustment_get_value(list->adjustment) + display_row;
    if(actual < 0 || actual >= (int)entries->len)
        return NULL;
    if(index)
        *index = actual;
    return g_ptr_array_index(entries, actual);
}

static void gvr_vims_draw_text(GtkWidget *widget,
                               cairo_t *cr,
                               const char *text,
                               double x,
                               double y,
                               double width,
                               gboolean bold)
{
    PangoLayout *layout;
    PangoFontDescription *font;

    if(width <= 2.0)
        return;

    layout = gtk_widget_create_pango_layout(widget, text ? text : "");
    font = pango_font_description_new();
    pango_font_description_set_family(font, "Sans");
    pango_font_description_set_size(font, 10 * PANGO_SCALE);
    pango_font_description_set_weight(font,
                                      bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font);
    g_object_unref(layout);
}

static void gvr_vims_draw_separator(cairo_t *cr, double x, double height)
{
    cairo_set_source_rgba(cr, 0.32, 0.34, 0.39, 0.32);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x + 0.5, 0);
    cairo_line_to(cr, x + 0.5, height);
    cairo_stroke(cr);
}

static void gvr_vims_draw_namespace(GvrVimsList *list,
                                    GtkWidget *widget,
                                    cairo_t *cr,
                                    int width,
                                    int height)
{
    const double format_width = MAX(120.0, MIN(240.0, width * 0.25));
    const double descr_x = GVR_VIMS_ID_WIDTH + 8.0;
    const double format_x = MAX(descr_x + 120.0, width - format_width);
    const double descr_width = MAX(80.0, format_x - descr_x - 8.0);
    const int visible = gvr_vims_list_visible_rows(list);

    cairo_set_source_rgb(cr, 0.105, 0.115, 0.135);
    cairo_rectangle(cr, 0, 0, width, GVR_VIMS_HEADER_HEIGHT);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.82, 0.85, 0.90);
    gvr_vims_draw_text(widget, cr, "VIMS", 8, 4, GVR_VIMS_ID_WIDTH - 12, TRUE);
    gvr_vims_draw_text(widget, cr, "DESCRIPTION", descr_x, 4, descr_width, TRUE);
    gvr_vims_draw_text(widget, cr, "FORMAT", format_x + 6, 4,
                       width - format_x - 10, TRUE);

    gvr_vims_draw_separator(cr, GVR_VIMS_ID_WIDTH, height);
    gvr_vims_draw_separator(cr, format_x, height);

    for(int row = 0; row <= visible; row++) {
        int index = -1;
        GvrVimsNamespaceEntry *entry =
            gvr_vims_list_entry_at_row(list, row, &index);
        const double y = GVR_VIMS_HEADER_HEIGHT + row * GVR_VIMS_ROW_HEIGHT;
        char id[16];

        if(!entry || y >= height)
            break;
        if(index == list->selected_index) {
            cairo_set_source_rgba(cr, 0.36, 0.43, 0.55, 0.42);
            cairo_rectangle(cr, 0, y, width, GVR_VIMS_ROW_HEIGHT);
            cairo_fill(cr);
        }
        else if((row & 1) == 0) {
            cairo_set_source_rgba(cr, 0.12, 0.13, 0.15, 0.34);
            cairo_rectangle(cr, 0, y, width, GVR_VIMS_ROW_HEIGHT);
            cairo_fill(cr);
        }

        g_snprintf(id, sizeof(id), "%03d", entry->event_id);
        cairo_set_source_rgb(cr, 0.82, 0.85, 0.90);
        gvr_vims_draw_text(widget, cr, id, 8, y + 3,
                           GVR_VIMS_ID_WIDTH - 12, FALSE);
        gvr_vims_draw_text(widget, cr, entry->description,
                           descr_x, y + 3, descr_width, FALSE);
        cairo_set_source_rgb(cr, 0.66, 0.70, 0.77);
        gvr_vims_draw_text(widget, cr, entry->format,
                           format_x + 6, y + 3,
                           width - format_x - 10, FALSE);
    }
}

static void gvr_vims_action_columns(int width,
                                    double *descr_x,
                                    double *format_x,
                                    double *args_x,
                                    double *modifier_x,
                                    double *key_x)
{
    double key_width = MAX(92.0, MIN(132.0, width * 0.13));
    double mod_width = MAX(92.0, MIN(126.0, width * 0.12));
    double format_width = MAX(90.0, MIN(150.0, width * 0.14));
    double args_width = MAX(100.0, MIN(190.0, width * 0.18));
    double d_x = GVR_VIMS_ID_WIDTH + 8.0;
    double k_x = width - key_width;
    double m_x = k_x - mod_width;
    double a_x = m_x - args_width;
    double f_x = a_x - format_width;

    if(f_x < d_x + 110.0) {
        const double shortage = (d_x + 110.0) - f_x;
        args_width = MAX(72.0, args_width - shortage * 0.55);
        format_width = MAX(72.0, format_width - shortage * 0.45);
        a_x = m_x - args_width;
        f_x = a_x - format_width;
    }

    *descr_x = d_x;
    *format_x = f_x;
    *args_x = a_x;
    *modifier_x = m_x;
    *key_x = k_x;
}

static void gvr_vims_draw_actions(GvrVimsList *list,
                                  GtkWidget *widget,
                                  cairo_t *cr,
                                  int width,
                                  int height)
{
    double descr_x;
    double format_x;
    double args_x;
    double modifier_x;
    double key_x;
    const int visible = gvr_vims_list_visible_rows(list);

    gvr_vims_action_columns(width,
                            &descr_x,
                            &format_x,
                            &args_x,
                            &modifier_x,
                            &key_x);

    cairo_set_source_rgb(cr, 0.105, 0.115, 0.135);
    cairo_rectangle(cr, 0, 0, width, GVR_VIMS_HEADER_HEIGHT);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.82, 0.85, 0.90);
    gvr_vims_draw_text(widget, cr, "VIMS", 8, 4, GVR_VIMS_ID_WIDTH - 12, TRUE);
    gvr_vims_draw_text(widget, cr, "DESCRIPTION", descr_x, 4,
                       format_x - descr_x - 8, TRUE);
    gvr_vims_draw_text(widget, cr, "FORMAT", format_x + 6, 4,
                       args_x - format_x - 10, TRUE);
    gvr_vims_draw_text(widget, cr, "ARGS", args_x + 6, 4,
                       modifier_x - args_x - 10, TRUE);
    gvr_vims_draw_text(widget, cr, "MODIFIER", modifier_x + 6, 4,
                       key_x - modifier_x - 10, TRUE);
    gvr_vims_draw_text(widget, cr, "KEY", key_x + 6, 4,
                       width - key_x - 10, TRUE);

    gvr_vims_draw_separator(cr, GVR_VIMS_ID_WIDTH, height);
    gvr_vims_draw_separator(cr, format_x, height);
    gvr_vims_draw_separator(cr, args_x, height);
    gvr_vims_draw_separator(cr, modifier_x, height);
    gvr_vims_draw_separator(cr, key_x, height);

    for(int row = 0; row <= visible; row++) {
        int index = -1;
        GvrVimsActionEntry *entry =
            gvr_vims_list_entry_at_row(list, row, &index);
        const double y = GVR_VIMS_HEADER_HEIGHT + row * GVR_VIMS_ROW_HEIGHT;
        char id[16];

        if(!entry || y >= height)
            break;
        if(index == list->selected_index) {
            cairo_set_source_rgba(cr, 0.36, 0.43, 0.55, 0.42);
            cairo_rectangle(cr, 0, y, width, GVR_VIMS_ROW_HEIGHT);
            cairo_fill(cr);
        }
        else if((row & 1) == 0) {
            cairo_set_source_rgba(cr, 0.12, 0.13, 0.15, 0.34);
            cairo_rectangle(cr, 0, y, width, GVR_VIMS_ROW_HEIGHT);
            cairo_fill(cr);
        }

        g_snprintf(id, sizeof(id), "%03d", entry->event_id);
        cairo_set_source_rgb(cr, 0.82, 0.85, 0.90);
        gvr_vims_draw_text(widget, cr, id, 8, y + 3,
                           GVR_VIMS_ID_WIDTH - 12, FALSE);
        gvr_vims_draw_text(widget, cr, entry->description,
                           descr_x, y + 3,
                           format_x - descr_x - 8, FALSE);
        cairo_set_source_rgb(cr, 0.66, 0.70, 0.77);
        gvr_vims_draw_text(widget, cr, entry->format,
                           format_x + 6, y + 3,
                           args_x - format_x - 10, FALSE);
        gvr_vims_draw_text(widget, cr, entry->args,
                           args_x + 6, y + 3,
                           modifier_x - args_x - 10, FALSE);
        gvr_vims_draw_text(widget, cr, entry->modifier_text,
                           modifier_x + 6, y + 3,
                           key_x - modifier_x - 10, FALSE);
        gvr_vims_draw_text(widget, cr, entry->key_text,
                           key_x + 6, y + 3,
                           width - key_x - 10, FALSE);
    }
}

static void gvr_vims_draw_midi(GvrVimsList *list,
                               GtkWidget *widget,
                               cairo_t *cr,
                               int width,
                               int height)
{
    const double type_x = 8.0;
    const double param_x = MAX(190.0, width * 0.27);
    const double mode_x = MAX(param_x + 76.0, width * 0.39);
    const double widget_x = MAX(mode_x + 100.0, width * 0.55);
    const double message_x = MAX(widget_x + 150.0, width * 0.72);
    const int visible = gvr_vims_list_visible_rows(list);

    cairo_set_source_rgb(cr, 0.105, 0.115, 0.135);
    cairo_rectangle(cr, 0, 0, width, GVR_VIMS_HEADER_HEIGHT);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.82, 0.85, 0.90);
    gvr_vims_draw_text(widget, cr, "MIDI EVENT", type_x, 4,
                       param_x - type_x - 8, TRUE);
    gvr_vims_draw_text(widget, cr, "PARAM", param_x + 6, 4,
                       mode_x - param_x - 10, TRUE);
    gvr_vims_draw_text(widget, cr, "MODE", mode_x + 6, 4,
                       widget_x - mode_x - 10, TRUE);
    gvr_vims_draw_text(widget, cr, "WIDGET", widget_x + 6, 4,
                       message_x - widget_x - 10, TRUE);
    gvr_vims_draw_text(widget, cr, "VIMS", message_x + 6, 4,
                       width - message_x - 10, TRUE);

    gvr_vims_draw_separator(cr, param_x, height);
    gvr_vims_draw_separator(cr, mode_x, height);
    gvr_vims_draw_separator(cr, widget_x, height);
    gvr_vims_draw_separator(cr, message_x, height);

    if(list->owner->midi_entries->len == 0) {
        cairo_set_source_rgb(cr, 0.58, 0.62, 0.69);
        gvr_vims_draw_text(widget,
                           cr,
                           "No learned MIDI mappings. Enable MIDI Learn from the main menu, move a control, then use a UI control.",
                           12,
                           GVR_VIMS_HEADER_HEIGHT + 12,
                           width - 24,
                           FALSE);
        return;
    }

    for(int row = 0; row <= visible; row++) {
        int index = -1;
        GvrVimsMidiEntry *entry =
            gvr_vims_list_entry_at_row(list, row, &index);
        const double y = GVR_VIMS_HEADER_HEIGHT + row * GVR_VIMS_ROW_HEIGHT;
        if(!entry || y >= height)
            break;
        if(index == list->selected_index) {
            cairo_set_source_rgba(cr, 0.36, 0.43, 0.55, 0.42);
            cairo_rectangle(cr, 0, y, width, GVR_VIMS_ROW_HEIGHT);
            cairo_fill(cr);
        }
        else if((row & 1) == 0) {
            cairo_set_source_rgba(cr, 0.12, 0.13, 0.15, 0.34);
            cairo_rectangle(cr, 0, y, width, GVR_VIMS_ROW_HEIGHT);
            cairo_fill(cr);
        }

        cairo_set_source_rgb(cr, 0.82, 0.85, 0.90);
        gvr_vims_draw_text(widget, cr, entry->event_text,
                           type_x, y + 3,
                           param_x - type_x - 8, FALSE);
        gvr_vims_draw_text(widget, cr, entry->parameter_text,
                           param_x + 6, y + 3,
                           mode_x - param_x - 10, FALSE);
        cairo_set_source_rgb(cr, 0.66, 0.70, 0.77);
        gvr_vims_draw_text(widget, cr, entry->mode_text,
                           mode_x + 6, y + 3,
                           widget_x - mode_x - 10, FALSE);
        gvr_vims_draw_text(widget, cr, entry->widget,
                           widget_x + 6, y + 3,
                           message_x - widget_x - 10, FALSE);
        gvr_vims_draw_text(widget, cr, entry->message,
                           message_x + 6, y + 3,
                           width - message_x - 10, FALSE);
    }
}

static gboolean gvr_vims_list_draw(GtkWidget *widget,
                                   cairo_t *cr,
                                   gpointer user_data)
{
    GvrVimsList *list = user_data;
    GtkAllocation allocation;

    gtk_widget_get_allocation(widget, &allocation);
    cairo_set_source_rgb(cr, 0.050, 0.054, 0.064);
    cairo_paint(cr);

    if(list->kind == GVR_VIMS_LIST_NAMESPACE)
        gvr_vims_draw_namespace(list,
                                widget,
                                cr,
                                allocation.width,
                                allocation.height);
    else if(list->kind == GVR_VIMS_LIST_ACTIONS)
        gvr_vims_draw_actions(list,
                              widget,
                              cr,
                              allocation.width,
                              allocation.height);
    else
        gvr_vims_draw_midi(list,
                           widget,
                           cr,
                           allocation.width,
                           allocation.height);
    return FALSE;
}

static void gvr_vims_list_emit_selection(GvrVimsList *list)
{
    if(list->kind == GVR_VIMS_LIST_MIDI) {
        if(list->owner->midi_unbind_button)
            gtk_widget_set_sensitive(list->owner->midi_unbind_button,
                                     list->selected_index >= 0);
        return;
    }
    g_signal_emit(list->owner,
                  gvr_vims_view_signals[
                      list->kind == GVR_VIMS_LIST_NAMESPACE
                          ? SIGNAL_NAMESPACE_SELECTION_CHANGED
                          : SIGNAL_ACTION_SELECTION_CHANGED],
                  0);
}

static void gvr_vims_list_clear_other_selection(GvrVimsList *list)
{
    GvrVimsList *other;

    if(list->kind == GVR_VIMS_LIST_MIDI)
        return;
    other = list->kind == GVR_VIMS_LIST_NAMESPACE
        ? &list->owner->action_list
        : &list->owner->namespace_list;

    if(other->selected_index >= 0) {
        other->selected_index = -1;
        gtk_widget_queue_draw(other->area);
        gvr_vims_list_emit_selection(other);
    }
}

static void gvr_vims_list_scroll_to_selected(GvrVimsList *list)
{
    int visible;
    int top;

    if(list->selected_index < 0)
        return;
    visible = gvr_vims_list_visible_rows(list);
    top = (int)gtk_adjustment_get_value(list->adjustment);
    if(list->selected_index < top)
        gtk_adjustment_set_value(list->adjustment, list->selected_index);
    else if(list->selected_index >= top + visible)
        gtk_adjustment_set_value(list->adjustment,
                                 list->selected_index - visible + 1);
}

static void gvr_vims_list_set_selected(GvrVimsList *list,
                                       int index,
                                       gboolean clear_other)
{
    GPtrArray *entries = gvr_vims_list_entries(list);
    int next = entries && index >= 0 && index < (int)entries->len ? index : -1;

    if(clear_other)
        gvr_vims_list_clear_other_selection(list);
    if(next == list->selected_index)
        return;
    list->selected_index = next;
    gvr_vims_list_scroll_to_selected(list);
    gtk_widget_queue_draw(list->area);
    gvr_vims_list_emit_selection(list);
}

static void gvr_vims_view_select_search_result(GvrVimsView *view,
                                               GvrVimsListKind kind,
                                               int index)
{
    GvrVimsList *list = kind == GVR_VIMS_LIST_NAMESPACE
        ? &view->namespace_list
        : &view->action_list;

    view->search_kind = kind;
    view->search_index = index;
    gvr_vims_list_set_selected(list, index, TRUE);
}

static gboolean gvr_vims_view_find(GvrVimsView *view,
                                   int direction,
                                   gboolean restart)
{
    const int total = gvr_vims_view_search_total(view);
    int current;

    if(!view->search_query || !view->search_query[0] || total <= 0) {
        view->search_index = -1;
        gvr_vims_view_search_status(view, -1);
        return FALSE;
    }

    current = restart
        ? (direction > 0 ? -1 : total)
        : gvr_vims_view_search_flat_index(view);
    if(current < -1 || current >= total)
        current = direction > 0 ? -1 : total;

    for(int step = 1; step <= total; step++) {
        int candidate = current + direction * step;
        GvrVimsListKind kind;
        int index;

        while(candidate < 0)
            candidate += total;
        candidate %= total;
        if(!gvr_vims_view_search_match_at(view,
                                          candidate,
                                          &kind,
                                          &index))
            continue;

        gvr_vims_view_select_search_result(view, kind, index);
        gvr_vims_view_search_status(view, candidate);
        return TRUE;
    }

    view->search_index = -1;
    gvr_vims_view_search_status(view, -1);
    return FALSE;
}

static void gvr_vims_view_open_find(GvrVimsView *view)
{
    gtk_revealer_set_reveal_child(GTK_REVEALER(view->search_revealer), TRUE);
    gtk_widget_grab_focus(view->search_entry);
    gtk_editable_select_region(GTK_EDITABLE(view->search_entry), 0, -1);
}

static void gvr_vims_view_close_find(GvrVimsView *view)
{
    gtk_revealer_set_reveal_child(GTK_REVEALER(view->search_revealer), FALSE);
    if(view->namespace_list.selected_index >= 0)
        gtk_widget_grab_focus(view->namespace_list.area);
    else if(view->action_list.selected_index >= 0)
        gtk_widget_grab_focus(view->action_list.area);
}

static void gvr_vims_view_search_changed(GtkEditable *editable,
                                         gpointer user_data)
{
    GvrVimsView *view = GVR_VIMS_VIEW(user_data);
    gchar *query = g_strdup(gtk_entry_get_text(GTK_ENTRY(editable)));

    g_strstrip(query);
    g_free(view->search_query);
    view->search_query = query;
    view->search_kind = GVR_VIMS_LIST_NAMESPACE;
    view->search_index = -1;
    gvr_vims_view_find(view, 1, TRUE);
}

static void gvr_vims_view_search_activate(GtkEntry *entry,
                                          gpointer user_data)
{
    (void)entry;
    gvr_vims_view_find(GVR_VIMS_VIEW(user_data), 1, FALSE);
}

static gboolean gvr_vims_view_search_key_press(GtkWidget *widget,
                                               GdkEventKey *event,
                                               gpointer user_data)
{
    GvrVimsView *view = GVR_VIMS_VIEW(user_data);
    (void)widget;

    if(event->keyval == GDK_KEY_Escape) {
        gvr_vims_view_close_find(view);
        return TRUE;
    }
    if((event->keyval == GDK_KEY_Return ||
        event->keyval == GDK_KEY_KP_Enter) &&
       (event->state & GDK_SHIFT_MASK)) {
        gvr_vims_view_find(view, -1, FALSE);
        return TRUE;
    }
    return FALSE;
}

static void gvr_vims_view_find_next_clicked(GtkButton *button,
                                            gpointer user_data)
{
    (void)button;
    gvr_vims_view_find(GVR_VIMS_VIEW(user_data), 1, FALSE);
}

static void gvr_vims_view_find_previous_clicked(GtkButton *button,
                                                gpointer user_data)
{
    (void)button;
    gvr_vims_view_find(GVR_VIMS_VIEW(user_data), -1, FALSE);
}

static void gvr_vims_view_find_close_clicked(GtkButton *button,
                                             gpointer user_data)
{
    (void)button;
    gvr_vims_view_close_find(GVR_VIMS_VIEW(user_data));
}

static void gvr_vims_view_find_open_clicked(GtkButton *button,
                                            gpointer user_data)
{
    (void)button;
    gvr_vims_view_open_find(GVR_VIMS_VIEW(user_data));
}

static gboolean gvr_vims_view_key_press(GtkWidget *widget,
                                        GdkEventKey *event,
                                        gpointer user_data)
{
    GvrVimsView *view = GVR_VIMS_VIEW(user_data);
    const gboolean control = (event->state & GDK_CONTROL_MASK) != 0;
    const gboolean shift = (event->state & GDK_SHIFT_MASK) != 0;
    (void)widget;

    if(control && (event->keyval == GDK_KEY_f ||
                   event->keyval == GDK_KEY_F)) {
        gvr_vims_view_open_find(view);
        return TRUE;
    }
    if(event->keyval == GDK_KEY_F3) {
        gvr_vims_view_find(view, shift ? -1 : 1, FALSE);
        return TRUE;
    }
    if(event->keyval == GDK_KEY_Escape &&
       gtk_revealer_get_reveal_child(GTK_REVEALER(view->search_revealer))) {
        gvr_vims_view_close_find(view);
        return TRUE;
    }
    return FALSE;
}

static void gvr_vims_list_activate(GvrVimsList *list)
{
    if(list->selected_index < 0 || list->kind == GVR_VIMS_LIST_MIDI)
        return;
    g_signal_emit(list->owner,
                  gvr_vims_view_signals[
                      list->kind == GVR_VIMS_LIST_NAMESPACE
                          ? SIGNAL_NAMESPACE_ACTIVATED
                          : SIGNAL_ACTION_ACTIVATED],
                  0);
}

static gboolean gvr_vims_list_button_press(GtkWidget *widget,
                                           GdkEventButton *event,
                                           gpointer user_data)
{
    GvrVimsList *list = user_data;
    int row;
    int index = -1;

    if(event->button != 1 || event->y < GVR_VIMS_HEADER_HEIGHT)
        return FALSE;
    row = ((int)event->y - GVR_VIMS_HEADER_HEIGHT) / GVR_VIMS_ROW_HEIGHT;
    if(!gvr_vims_list_entry_at_row(list, row, &index))
        return FALSE;
    gtk_widget_grab_focus(widget);
    gvr_vims_list_set_selected(list, index, TRUE);
    if(event->type == GDK_2BUTTON_PRESS)
        gvr_vims_list_activate(list);
    return TRUE;
}

static gboolean gvr_vims_list_scroll(GtkWidget *widget,
                                     GdkEventScroll *event,
                                     gpointer user_data)
{
    GvrVimsList *list = user_data;
    double value = gtk_adjustment_get_value(list->adjustment);
    double delta = 0.0;
    (void)widget;

    if(event->direction == GDK_SCROLL_UP)
        delta = -3.0;
    else if(event->direction == GDK_SCROLL_DOWN)
        delta = 3.0;
    else if(event->direction == GDK_SCROLL_SMOOTH) {
        double delta_x = 0.0;
        double delta_y = 0.0;
        if(gdk_event_get_scroll_deltas((GdkEvent *)event,
                                       &delta_x,
                                       &delta_y))
            delta = delta_y;
    }
    gtk_adjustment_set_value(list->adjustment, value + delta);
    return TRUE;
}

static gboolean gvr_vims_list_key_press(GtkWidget *widget,
                                        GdkEventKey *event,
                                        gpointer user_data)
{
    GvrVimsList *list = user_data;
    GPtrArray *entries = gvr_vims_list_entries(list);
    int next = list->selected_index;
    int page = gvr_vims_list_visible_rows(list);
    (void)widget;

    if(!entries || entries->len == 0)
        return FALSE;
    if(next < 0)
        next = 0;

    switch(event->keyval) {
        case GDK_KEY_Up: next--; break;
        case GDK_KEY_Down: next++; break;
        case GDK_KEY_Page_Up: next -= page; break;
        case GDK_KEY_Page_Down: next += page; break;
        case GDK_KEY_Home: next = 0; break;
        case GDK_KEY_End: next = (int)entries->len - 1; break;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            gvr_vims_list_activate(list);
            return TRUE;
        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete:
            if(list->kind == GVR_VIMS_LIST_MIDI &&
               list->selected_index >= 0) {
                g_signal_emit(list->owner,
                              gvr_vims_view_signals[SIGNAL_MIDI_UNBIND_REQUESTED],
                              0);
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }

    next = CLAMP(next, 0, (int)entries->len - 1);
    gvr_vims_list_set_selected(list, next, TRUE);
    return TRUE;
}

static gboolean gvr_vims_list_query_tooltip(GtkWidget *widget,
                                            gint x,
                                            gint y,
                                            gboolean keyboard_mode,
                                            GtkTooltip *tooltip,
                                            gpointer user_data)
{
    GvrVimsList *list = user_data;
    int index = -1;
    gpointer entry;
    gchar *text;
    (void)widget;
    (void)x;

    if(keyboard_mode && list->selected_index >= 0) {
        GPtrArray *entries = gvr_vims_list_entries(list);
        entry = entries && list->selected_index < (int)entries->len
              ? g_ptr_array_index(entries, list->selected_index)
              : NULL;
    }
    else {
        if(y < GVR_VIMS_HEADER_HEIGHT)
            return FALSE;
        entry = gvr_vims_list_entry_at_row(
            list,
            (y - GVR_VIMS_HEADER_HEIGHT) / GVR_VIMS_ROW_HEIGHT,
            &index);
    }
    if(!entry)
        return FALSE;

    if(list->kind == GVR_VIMS_LIST_NAMESPACE) {
        GvrVimsNamespaceEntry *item = entry;
        text = g_strdup_printf("VIMS %03d\n%s\nFormat: %s\nArguments: %d",
                               item->event_id,
                               item->description,
                               item->format && item->format[0] ? item->format : "none",
                               item->params);
    }
    else if(list->kind == GVR_VIMS_LIST_ACTIONS) {
        GvrVimsActionEntry *item = entry;
        text = g_strdup_printf("VIMS %03d%s\n%s\nFormat: %s\nArgs: %s\nModifier: %s\nKey: %s%s%s",
                               item->event_id,
                               item->is_bundle ? " (bundle)" : "",
                               item->description,
                               item->format && item->format[0] ? item->format : "none",
                               item->args && item->args[0] ? item->args : "none",
                               item->modifier_text && item->modifier_text[0] ? item->modifier_text : "none",
                               item->key_text && item->key_text[0] ? item->key_text : "none",
                               item->is_bundle ? "\n\n" : "",
                               item->is_bundle && item->bundle_text ? item->bundle_text : "");
    }
    else {
        GvrVimsMidiEntry *item = entry;
        text = g_strdup_printf("MIDI %s\nType: %s [%d]\nParameter: %s [%d]\nMode: %s\nWidget: %s\nVIMS: %s",
                               item->mapping_key,
                               item->event_text,
                               item->event_type,
                               item->parameter_text,
                               item->parameter,
                               item->mode_text,
                               item->widget && item->widget[0] ? item->widget : "none",
                               item->message && item->message[0] ? item->message : "none");
    }
    gtk_tooltip_set_text(tooltip, text);
    g_free(text);
    return TRUE;
}

static void gvr_vims_list_size_allocate(GtkWidget *widget,
                                        GtkAllocation *allocation,
                                        gpointer user_data)
{
    GvrVimsList *list = user_data;
    (void)widget;
    (void)allocation;
    gvr_vims_list_update_adjustment(list);
}

static void gvr_vims_list_adjustment_changed(GtkAdjustment *adjustment,
                                             gpointer user_data)
{
    GvrVimsList *list = user_data;
    (void)adjustment;
    gtk_widget_queue_draw(list->area);
}

static GtkWidget *gvr_vims_list_new(GvrVimsView *view,
                                    GvrVimsList *list,
                                    GvrVimsListKind kind,
                                    int height)
{
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    list->owner = view;
    list->kind = kind;
    list->selected_index = -1;
    list->area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(list->area, TRUE);
    gtk_widget_set_has_tooltip(list->area, TRUE);
    gtk_widget_set_size_request(list->area, -1, height);
    gtk_widget_add_events(list->area,
                          GDK_BUTTON_PRESS_MASK |
                          GDK_SCROLL_MASK |
                          GDK_SMOOTH_SCROLL_MASK |
                          GDK_KEY_PRESS_MASK);
    gtk_box_pack_start(GTK_BOX(body), list->area, TRUE, TRUE, 0);

    list->adjustment = GTK_ADJUSTMENT(
        gtk_adjustment_new(0, 0, 1, 1, 1, 1));
    list->scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL,
                                        list->adjustment);
    gtk_box_pack_start(GTK_BOX(body), list->scrollbar, FALSE, FALSE, 0);

    g_signal_connect(list->area, "draw",
                     G_CALLBACK(gvr_vims_list_draw), list);
    g_signal_connect(list->area, "size-allocate",
                     G_CALLBACK(gvr_vims_list_size_allocate), list);
    g_signal_connect(list->area, "button-press-event",
                     G_CALLBACK(gvr_vims_list_button_press), list);
    g_signal_connect(list->area, "scroll-event",
                     G_CALLBACK(gvr_vims_list_scroll), list);
    g_signal_connect(list->area, "key-press-event",
                     G_CALLBACK(gvr_vims_list_key_press), list);
    g_signal_connect(list->area, "query-tooltip",
                     G_CALLBACK(gvr_vims_list_query_tooltip), list);
    g_signal_connect(list->adjustment, "value-changed",
                     G_CALLBACK(gvr_vims_list_adjustment_changed), list);
    return body;
}

static void gvr_vims_view_midi_refresh_clicked(GtkButton *button,
                                                gpointer user_data)
{
    (void)button;
    g_signal_emit(GVR_VIMS_VIEW(user_data),
                  gvr_vims_view_signals[SIGNAL_MIDI_REFRESH_REQUESTED],
                  0);
}

static void gvr_vims_view_midi_unbind_clicked(GtkButton *button,
                                               gpointer user_data)
{
    GvrVimsView *view = GVR_VIMS_VIEW(user_data);
    (void)button;
    if(view->midi_list.selected_index < 0)
        return;
    g_signal_emit(view,
                  gvr_vims_view_signals[SIGNAL_MIDI_UNBIND_REQUESTED],
                  0);
}

static void gvr_vims_view_registry_switch_page(GtkNotebook *notebook,
                                                GtkWidget *page,
                                                guint page_num,
                                                gpointer user_data)
{
    (void)notebook;
    (void)page;
    if(page_num == 1)
        g_signal_emit(GVR_VIMS_VIEW(user_data),
                      gvr_vims_view_signals[SIGNAL_MIDI_REFRESH_REQUESTED],
                      0);
}

static void gvr_vims_view_action_clicked(GtkButton *button, gpointer user_data)
{
    GvrVimsView *view = GVR_VIMS_VIEW(user_data);
    GvrVimsViewAction action = (GvrVimsViewAction)GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(button), "gvr-vims-action"));

    if(action >= 0 && action < GVR_VIMS_VIEW_N_ACTIONS)
        g_signal_emit(view,
                      gvr_vims_view_signals[gvr_vims_view_action_signals[action]],
                      0);
}

static GtkWidget *gvr_vims_view_button(GvrVimsView *view,
                                       GvrVimsViewAction action,
                                       const char *label,
                                       const char *tooltip)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_widget_set_can_focus(button, FALSE);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_style_context_add_class(gtk_widget_get_style_context(button),
                                "vims-history-button");
    g_object_set_data(G_OBJECT(button),
                      "gvr-vims-action",
                      GINT_TO_POINTER(action));
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(gvr_vims_view_action_clicked),
                     view);
    if(view->button_count[action] < 2)
        view->buttons[action][view->button_count[action]++] = button;
    return button;
}

static GtkWidget *gvr_vims_view_toolbar(GvrVimsView *view,
                                        const char *title)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *label = gtk_label_new(NULL);
    gchar *markup = g_markup_printf_escaped("<b>%s</b>", title);

    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(box),
                                "vims-history-toolbar");
    (void)view;
    return box;
}

static GtkWidget *gvr_vims_view_search_bar(GvrVimsView *view)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *label = gtk_label_new("Find");
    GtkWidget *button;

    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 2);
    gtk_widget_set_margin_bottom(box, 2);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    view->search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->search_entry),
                                   "VIMS number or description");
    gtk_widget_set_hexpand(view->search_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(box), view->search_entry, TRUE, TRUE, 0);

    view->search_status = gtk_label_new("Type an event ID or description");
    gtk_label_set_xalign(GTK_LABEL(view->search_status), 1.0f);
    gtk_widget_set_size_request(view->search_status, 104, -1);
    gtk_box_pack_start(GTK_BOX(box), view->search_status, FALSE, FALSE, 2);

    button = gtk_button_new_with_label("Previous");
    gtk_widget_set_can_focus(button, FALSE);
    gtk_widget_set_tooltip_text(button, "Find the previous match (Shift+F3)");
    gtk_style_context_add_class(gtk_widget_get_style_context(button),
                                "vims-history-button");
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(gvr_vims_view_find_previous_clicked),
                     view);

    button = gtk_button_new_with_label("Next");
    gtk_widget_set_can_focus(button, FALSE);
    gtk_widget_set_tooltip_text(button, "Find the next match (F3)");
    gtk_style_context_add_class(gtk_widget_get_style_context(button),
                                "vims-history-button");
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(gvr_vims_view_find_next_clicked),
                     view);

    button = gtk_button_new_with_label("Close");
    gtk_widget_set_can_focus(button, FALSE);
    gtk_widget_set_tooltip_text(button, "Close search (Escape)");
    gtk_style_context_add_class(gtk_widget_get_style_context(button),
                                "vims-history-button");
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(gvr_vims_view_find_close_clicked),
                     view);

    g_signal_connect(view->search_entry,
                     "search-changed",
                     G_CALLBACK(gvr_vims_view_search_changed),
                     view);
    g_signal_connect(view->search_entry,
                     "activate",
                     G_CALLBACK(gvr_vims_view_search_activate),
                     view);
    g_signal_connect(view->search_entry,
                     "key-press-event",
                     G_CALLBACK(gvr_vims_view_search_key_press),
                     view);

    gtk_style_context_add_class(gtk_widget_get_style_context(box),
                                "vims-history-toolbar");
    return box;
}

static void gvr_vims_view_apply_css(GtkWidget *widget, const char *css)
{
    GtkCssProvider *provider;

    if(!widget || !css)
        return;
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(widget),
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void gvr_vims_view_copy_text(const char *text)
{
    GtkClipboard *clipboard;

    if(!text || !text[0])
        return;
    clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    gtk_clipboard_store(clipboard);
}

static void gvr_vims_view_copy_command_clicked(GtkButton *button,
                                                gpointer user_data)
{
    GvrVimsView *view = GVR_VIMS_VIEW(user_data);
    gchar *command;
    (void)button;

    command = gvr_vims_view_get_command(GTK_WIDGET(view));
    gvr_vims_view_copy_text(command);
    g_free(command);
}

static void gvr_vims_view_copy_request_clicked(GtkButton *button,
                                                gpointer user_data)
{
    GvrVimsView *view = GVR_VIMS_VIEW(user_data);
    (void)button;
    gvr_vims_view_copy_text(view->last_request);
}

static void gvr_vims_view_command_changed(GtkEditable *editable,
                                           gpointer user_data)
{
    GvrVimsView *view = GVR_VIMS_VIEW(user_data);
    const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
    gboolean ready = gtk_widget_get_sensitive(view->command_entry) &&
                     text && text[0];

    gtk_widget_set_sensitive(view->command_copy_button, ready);
    if(view->command_send_button)
        gtk_widget_set_sensitive(view->command_send_button, ready);
}

static void gvr_vims_view_command_activate(GtkEntry *entry,
                                            gpointer user_data)
{
    GvrVimsView *view = GVR_VIMS_VIEW(user_data);
    (void)entry;
    g_signal_emit(view,
                  gvr_vims_view_signals[SIGNAL_RUN_REQUESTED],
                  0);
}

static GtkWidget *gvr_vims_view_command_bar(GvrVimsView *view)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *label = gtk_label_new("Command row");
    GtkWidget *button;

    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 2);
    gtk_widget_set_margin_bottom(box, 2);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);

    view->command_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->command_entry),
                                   "Select a VIMS event");
    gtk_widget_set_hexpand(view->command_entry, TRUE);
    gtk_widget_set_sensitive(view->command_entry, FALSE);
    gtk_box_pack_start(GTK_BOX(row), view->command_entry, TRUE, TRUE, 0);
    gvr_vims_view_apply_css(view->command_entry,
                            "* { font-family: monospace; font-size: 11pt; }");
    g_signal_connect(view->command_entry,
                     "changed",
                     G_CALLBACK(gvr_vims_view_command_changed),
                     view);
    g_signal_connect(view->command_entry,
                     "activate",
                     G_CALLBACK(gvr_vims_view_command_activate),
                     view);

    button = gvr_vims_view_button(view,
                                  GVR_VIMS_VIEW_RUN,
                                  "Send",
                                  "Send this exact VIMS row; Enter does the same");
    view->command_send_button = button;
    gtk_widget_set_sensitive(button, FALSE);
    gtk_box_pack_start(GTK_BOX(row), button, FALSE, FALSE, 0);

    view->command_copy_button = gtk_button_new_with_label("Copy");
    gtk_widget_set_can_focus(view->command_copy_button, FALSE);
    gtk_widget_set_sensitive(view->command_copy_button, FALSE);
    gtk_widget_set_tooltip_text(view->command_copy_button,
                                "Copy the pattern-ready VIMS row");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(view->command_copy_button),
        "vims-history-button");
    gtk_box_pack_start(GTK_BOX(row),
                       view->command_copy_button,
                       FALSE,
                       FALSE,
                       0);
    g_signal_connect(view->command_copy_button,
                     "clicked",
                     G_CALLBACK(gvr_vims_view_copy_command_clicked),
                     view);

    button = gvr_vims_view_button(view,
                                  GVR_VIMS_VIEW_ADD,
                                  "Add to Bundle",
                                  "Append this exact VIMS row to the Bundle Editor");
    gtk_box_pack_start(GTK_BOX(row), button, FALSE, FALSE, 0);

    view->command_hint = gtk_label_new(
        "Select an event to create a pattern-ready VIMS row.");
    gtk_label_set_xalign(GTK_LABEL(view->command_hint), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(view->command_hint), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(view->command_hint),
                                "dim-label");

    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), view->command_hint, FALSE, FALSE, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box),
                                "vims-history-toolbar");
    return box;
}

static GtkWidget *gvr_vims_view_section(GtkWidget *toolbar,
                                        GtkWidget *content)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), content, TRUE, TRUE, 0);
    return box;
}

static GtkWidget *gvr_vims_view_text_page(GvrVimsView *view,
                                          GtkWidget *text_view,
                                          gboolean response)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *toolbar = gvr_vims_view_toolbar(
        view,
        response ? "Backend Response" : "Bundle Editor");
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *button;

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), text_view);

    if(response) {
        view->copy_request_button = gtk_button_new_with_label("Copy Request");
        gtk_widget_set_can_focus(view->copy_request_button, FALSE);
        gtk_widget_set_sensitive(view->copy_request_button, FALSE);
        gtk_widget_set_tooltip_text(view->copy_request_button,
                                    "Copy the exact VIMS request shown below");
        gtk_style_context_add_class(
            gtk_widget_get_style_context(view->copy_request_button),
            "vims-history-button");
        gtk_box_pack_end(GTK_BOX(toolbar),
                         view->copy_request_button,
                         FALSE,
                         FALSE,
                         0);
        g_signal_connect(view->copy_request_button,
                         "clicked",
                         G_CALLBACK(gvr_vims_view_copy_request_clicked),
                         view);
        button = gvr_vims_view_button(view,
                                      GVR_VIMS_VIEW_CLEAR_RESPONSE,
                                      "Clear",
                                      "Clear the displayed backend reply");
        gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    }
    else {
        button = gvr_vims_view_button(view,
                                      GVR_VIMS_VIEW_NEW_BUNDLE,
                                      "Create",
                                      "Register the editor contents as a new bundle");
        gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
        button = gvr_vims_view_button(view,
                                      GVR_VIMS_VIEW_UPDATE_BUNDLE,
                                      "Update",
                                      "Update the selected bundle");
        gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
        button = gvr_vims_view_button(view,
                                      GVR_VIMS_VIEW_DELETE_BUNDLE,
                                      "Delete",
                                      "Delete the selected bundle");
        gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
        button = gvr_vims_view_button(view,
                                      GVR_VIMS_VIEW_CLEAR_EDITOR,
                                      "Clear",
                                      "Clear the editor without changing the backend");
        gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    return box;
}

static void gvr_vims_view_finalize(GObject *object)
{
    GvrVimsView *view = GVR_VIMS_VIEW(object);
    g_clear_pointer(&view->search_query, g_free);
    g_clear_pointer(&view->last_request, g_free);
    g_clear_pointer(&view->namespace_entries, g_ptr_array_unref);
    g_clear_pointer(&view->action_entries, g_ptr_array_unref);
    g_clear_pointer(&view->midi_entries, g_ptr_array_unref);
    G_OBJECT_CLASS(gvr_vims_view_parent_class)->finalize(object);
}

static void gvr_vims_view_class_init(GvrVimsViewClass *klass)
{
    static const char *names[SIGNAL_LAST] = {
        "run-requested",
        "add-requested",
        "bind-requested",
        "unbind-requested",
        "new-bundle-requested",
        "update-bundle-requested",
        "delete-bundle-requested",
        "load-requested",
        "save-requested",
        "clear-editor-requested",
        "clear-response-requested",
        "namespace-selection-changed",
        "action-selection-changed",
        "namespace-activated",
        "action-activated",
        "midi-refresh-requested",
        "midi-unbind-requested"
    };
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = gvr_vims_view_finalize;
    for(int i = 0; i < SIGNAL_LAST; i++)
        gvr_vims_view_signals[i] =
            g_signal_new(names[i],
                         G_TYPE_FROM_CLASS(klass),
                         G_SIGNAL_RUN_FIRST,
                         0,
                         NULL,
                         NULL,
                         g_cclosure_marshal_VOID__VOID,
                         G_TYPE_NONE,
                         0);
}

static void gvr_vims_view_init(GvrVimsView *view)
{
    GtkWidget *toolbar;
    GtkWidget *button;
    GtkWidget *body;
    GtkWidget *namespace_section;
    GtkWidget *namespace_content;
    GtkWidget *actions_section;
    GtkWidget *midi_section;
    GtkWidget *search_bar;

    gtk_orientable_set_orientation(GTK_ORIENTABLE(view),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(view), 2);
    gtk_widget_set_hexpand(GTK_WIDGET(view), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(view), TRUE);
    gtk_widget_add_events(GTK_WIDGET(view), GDK_KEY_PRESS_MASK);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(view)),
                                "vims-history-view");
    gvr_vims_view_apply_css(GTK_WIDGET(view),
                            "* { font-size: 10.5pt; }");

    view->namespace_entries = g_ptr_array_new_with_free_func(
        gvr_vims_namespace_entry_free);
    view->action_entries = g_ptr_array_new_with_free_func(
        gvr_vims_action_entry_free);
    view->midi_entries = g_ptr_array_new_with_free_func(
        gvr_vims_midi_entry_free);
    view->search_kind = GVR_VIMS_LIST_NAMESPACE;
    view->search_index = -1;

    toolbar = gvr_vims_view_toolbar(view, "VIMS Browser");
    button = gtk_button_new_with_label("Find");
    gtk_widget_set_can_focus(button, FALSE);
    gtk_widget_set_tooltip_text(button,
                                "Find a VIMS event by number or description (Ctrl+F)");
    gtk_style_context_add_class(gtk_widget_get_style_context(button),
                                "vims-history-button");
    gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(gvr_vims_view_find_open_clicked),
                     view);
    button = gvr_vims_view_button(view,
                                  GVR_VIMS_VIEW_LOAD,
                                  "Load",
                                  "Load bundles and keybindings from an action file");
    gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    button = gvr_vims_view_button(view,
                                  GVR_VIMS_VIEW_SAVE,
                                  "Save",
                                  "Save bundles and keybindings to an action file");
    gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(view), toolbar, FALSE, FALSE, 0);

    view->search_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(view->search_revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(GTK_REVEALER(view->search_revealer), FALSE);
    search_bar = gvr_vims_view_search_bar(view);
    gtk_container_add(GTK_CONTAINER(view->search_revealer), search_bar);
    gtk_box_pack_start(GTK_BOX(view), view->search_revealer, FALSE, FALSE, 0);

    toolbar = gvr_vims_view_toolbar(view, "VIMS Namespace");
    button = gvr_vims_view_button(view,
                                  GVR_VIMS_VIEW_BIND,
                                  "Bind Key",
                                  "Assign a VeeJay SDL-window shortcut");
    gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    body = gvr_vims_list_new(view,
                             &view->namespace_list,
                             GVR_VIMS_LIST_NAMESPACE,
                             GVR_VIMS_NAMESPACE_MIN_HEIGHT);
    namespace_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(namespace_content), body, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(namespace_content),
                       gvr_vims_view_command_bar(view),
                       FALSE,
                       FALSE,
                       0);
    namespace_section = gvr_vims_view_section(toolbar, namespace_content);
    gtk_widget_set_size_request(namespace_section,
                                -1,
                                GVR_VIMS_NAMESPACE_MIN_HEIGHT);

    toolbar = gvr_vims_view_toolbar(view, "Registered Actions and Bundles");
    button = gvr_vims_view_button(view,
                                  GVR_VIMS_VIEW_RUN,
                                  "Run",
                                  "Execute the selected action or bundle");
    gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    button = gvr_vims_view_button(view,
                                  GVR_VIMS_VIEW_BIND,
                                  "Bind / Change",
                                  "Assign or replace the selected shortcut");
    gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    button = gvr_vims_view_button(view,
                                  GVR_VIMS_VIEW_UNBIND,
                                  "Unbind",
                                  "Remove the selected shortcut");
    gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    body = gvr_vims_list_new(view,
                             &view->action_list,
                             GVR_VIMS_LIST_ACTIONS,
                             GVR_VIMS_ACTIONS_MIN_HEIGHT);
    actions_section = gvr_vims_view_section(toolbar, body);
    gtk_widget_set_size_request(actions_section,
                                -1,
                                GVR_VIMS_ACTIONS_MIN_HEIGHT);

    toolbar = gvr_vims_view_toolbar(view, "Learned MIDI Events");
    button = gtk_button_new_with_label("Refresh");
    gtk_widget_set_can_focus(button, FALSE);
    gtk_widget_set_tooltip_text(button, "Reload learned MIDI mappings");
    gtk_style_context_add_class(gtk_widget_get_style_context(button),
                                "vims-history-button");
    gtk_box_pack_end(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(gvr_vims_view_midi_refresh_clicked),
                     view);
    view->midi_unbind_button = gtk_button_new_with_label("Unbind");
    gtk_widget_set_can_focus(view->midi_unbind_button, FALSE);
    gtk_widget_set_sensitive(view->midi_unbind_button, FALSE);
    gtk_widget_set_tooltip_text(view->midi_unbind_button,
                                "Remove the selected learned MIDI mapping");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(view->midi_unbind_button),
        "vims-history-button");
    gtk_box_pack_end(GTK_BOX(toolbar),
                     view->midi_unbind_button,
                     FALSE,
                     FALSE,
                     0);
    g_signal_connect(view->midi_unbind_button,
                     "clicked",
                     G_CALLBACK(gvr_vims_view_midi_unbind_clicked),
                     view);
    body = gvr_vims_list_new(view,
                             &view->midi_list,
                             GVR_VIMS_LIST_MIDI,
                             GVR_VIMS_ACTIONS_MIN_HEIGHT);
    midi_section = gvr_vims_view_section(toolbar, body);
    gtk_widget_set_size_request(midi_section,
                                -1,
                                GVR_VIMS_ACTIONS_MIN_HEIGHT);

    view->registry_notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(view->registry_notebook), TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(view->registry_notebook),
                             actions_section,
                             gtk_label_new("Actions & Bundles"));
    gtk_notebook_append_page(GTK_NOTEBOOK(view->registry_notebook),
                             midi_section,
                             gtk_label_new("MIDI"));
    g_signal_connect(view->registry_notebook,
                     "switch-page",
                     G_CALLBACK(gvr_vims_view_registry_switch_page),
                     view);

    view->response_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view->response_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view->response_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view->response_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view->response_view), GTK_WRAP_NONE);
    gvr_vims_view_apply_css(view->response_view,
                            "* { font-family: monospace; font-size: 11pt; }");

    view->editor_view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view->editor_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view->editor_view), GTK_WRAP_NONE);
    gvr_vims_view_apply_css(view->editor_view,
                            "* { font-family: monospace; font-size: 11pt; }");

    view->workspace = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(view->workspace), TRUE);
    gtk_widget_set_size_request(view->workspace,
                                -1,
                                GVR_VIMS_WORKSPACE_MIN_HEIGHT);
    gtk_notebook_append_page(GTK_NOTEBOOK(view->workspace),
                             gvr_vims_view_text_page(view,
                                                     view->response_view,
                                                     TRUE),
                             gtk_label_new("Response"));
    gtk_notebook_append_page(GTK_NOTEBOOK(view->workspace),
                             gvr_vims_view_text_page(view,
                                                     view->editor_view,
                                                     FALSE),
                             gtk_label_new("Bundle Editor"));

    view->lower_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_wide_handle(GTK_PANED(view->lower_paned), TRUE);
    gtk_paned_pack1(GTK_PANED(view->lower_paned),
                    view->registry_notebook,
                    TRUE,
                    FALSE);
    gtk_paned_pack2(GTK_PANED(view->lower_paned),
                    view->workspace,
                    TRUE,
                    FALSE);
    gtk_paned_set_position(GTK_PANED(view->lower_paned), 190);

    view->main_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_wide_handle(GTK_PANED(view->main_paned), TRUE);
    gtk_paned_pack1(GTK_PANED(view->main_paned),
                    namespace_section,
                    TRUE,
                    FALSE);
    gtk_paned_pack2(GTK_PANED(view->main_paned),
                    view->lower_paned,
                    TRUE,
                    FALSE);
    gtk_paned_set_position(GTK_PANED(view->main_paned), 250);
    gtk_box_pack_start(GTK_BOX(view), view->main_paned, TRUE, TRUE, 0);

    g_signal_connect(view,
                     "key-press-event",
                     G_CALLBACK(gvr_vims_view_key_press),
                     view);
    gtk_widget_show_all(GTK_WIDGET(view));
}

GtkWidget *gvr_vims_view_new(void)
{
    return g_object_new(GVR_TYPE_VIMS_VIEW, NULL);
}

static gint gvr_vims_namespace_compare(gconstpointer a, gconstpointer b)
{
    const GvrVimsNamespaceEntry *ea = *(GvrVimsNamespaceEntry * const *)a;
    const GvrVimsNamespaceEntry *eb = *(GvrVimsNamespaceEntry * const *)b;
    return (ea->event_id > eb->event_id) - (ea->event_id < eb->event_id);
}

static gint gvr_vims_action_compare(gconstpointer a, gconstpointer b)
{
    const GvrVimsActionEntry *ea = *(GvrVimsActionEntry * const *)a;
    const GvrVimsActionEntry *eb = *(GvrVimsActionEntry * const *)b;

    if(ea->event_id != eb->event_id)
        return (ea->event_id > eb->event_id) - (ea->event_id < eb->event_id);
    if(ea->is_bundle != eb->is_bundle)
        return ea->is_bundle ? -1 : 1;
    if(ea->modifier != eb->modifier)
        return (ea->modifier > eb->modifier) - (ea->modifier < eb->modifier);
    return (ea->key > eb->key) - (ea->key < eb->key);
}

static gint gvr_vims_midi_compare(gconstpointer a, gconstpointer b)
{
    const GvrVimsMidiEntry *ea = *(GvrVimsMidiEntry * const *)a;
    const GvrVimsMidiEntry *eb = *(GvrVimsMidiEntry * const *)b;

    if(ea->event_type != eb->event_type)
        return (ea->event_type > eb->event_type) -
               (ea->event_type < eb->event_type);
    if(ea->parameter != eb->parameter)
        return (ea->parameter > eb->parameter) -
               (ea->parameter < eb->parameter);
    return g_strcmp0(ea->mapping_key, eb->mapping_key);
}

void gvr_vims_view_clear_namespace(GtkWidget *widget)
{
    GvrVimsView *view;
    if(!GVR_IS_VIMS_VIEW(widget))
        return;
    view = GVR_VIMS_VIEW(widget);
    g_ptr_array_set_size(view->namespace_entries, 0);
    view->namespace_list.selected_index = -1;
    gtk_adjustment_set_value(view->namespace_list.adjustment, 0);
    gvr_vims_list_update_adjustment(&view->namespace_list);
    gtk_widget_queue_draw(view->namespace_list.area);
    g_signal_emit(view,
                  gvr_vims_view_signals[SIGNAL_NAMESPACE_SELECTION_CHANGED],
                  0);
}

void gvr_vims_view_append_namespace(GtkWidget *widget,
                                    int event_id,
                                    const char *description,
                                    const char *format,
                                    int params)
{
    GvrVimsView *view;
    GvrVimsNamespaceEntry *entry;

    if(!GVR_IS_VIMS_VIEW(widget))
        return;
    view = GVR_VIMS_VIEW(widget);
    entry = g_new0(GvrVimsNamespaceEntry, 1);
    entry->event_id = event_id;
    entry->params = params;
    entry->description = g_strdup(description ? description : "");
    entry->format = g_strdup(format ? format : "");
    g_ptr_array_add(view->namespace_entries, entry);
    g_ptr_array_sort(view->namespace_entries, gvr_vims_namespace_compare);
    gvr_vims_list_update_adjustment(&view->namespace_list);
    gtk_widget_queue_draw(view->namespace_list.area);
}

gboolean gvr_vims_view_get_selected_namespace(GtkWidget *widget,
                                              int *event_id,
                                              gchar **format,
                                              int *params)
{
    GvrVimsView *view;
    GvrVimsNamespaceEntry *entry;
    int index;

    if(!GVR_IS_VIMS_VIEW(widget))
        return FALSE;
    view = GVR_VIMS_VIEW(widget);
    index = view->namespace_list.selected_index;
    if(index < 0 || index >= (int)view->namespace_entries->len)
        return FALSE;
    entry = g_ptr_array_index(view->namespace_entries, index);
    if(event_id)
        *event_id = entry->event_id;
    if(format)
        *format = g_strdup(entry->format);
    if(params)
        *params = entry->params;
    return TRUE;
}

void gvr_vims_view_clear_actions(GtkWidget *widget)
{
    GvrVimsView *view;
    if(!GVR_IS_VIMS_VIEW(widget))
        return;
    view = GVR_VIMS_VIEW(widget);
    g_ptr_array_set_size(view->action_entries, 0);
    view->action_list.selected_index = -1;
    gtk_adjustment_set_value(view->action_list.adjustment, 0);
    gvr_vims_list_update_adjustment(&view->action_list);
    gtk_widget_queue_draw(view->action_list.area);
    g_signal_emit(view,
                  gvr_vims_view_signals[SIGNAL_ACTION_SELECTION_CHANGED],
                  0);
}

void gvr_vims_view_append_action(GtkWidget *widget,
                                 int event_id,
                                 const char *description,
                                 const char *format,
                                 const char *args,
                                 const char *modifier_text,
                                 const char *key_text,
                                 int modifier,
                                 int key,
                                 gboolean is_bundle,
                                 const char *bundle_text)
{
    GvrVimsView *view;
    GvrVimsActionEntry *entry;

    if(!GVR_IS_VIMS_VIEW(widget))
        return;
    view = GVR_VIMS_VIEW(widget);
    entry = g_new0(GvrVimsActionEntry, 1);
    entry->event_id = event_id;
    entry->description = g_strdup(description ? description : "");
    entry->format = g_strdup(format ? format : "");
    entry->args = g_strdup(args ? args : "");
    entry->modifier_text = g_strdup(modifier_text ? modifier_text : "");
    entry->key_text = g_strdup(key_text ? key_text : "");
    entry->modifier = modifier;
    entry->key = key;
    entry->is_bundle = is_bundle;
    entry->bundle_text = g_strdup(bundle_text ? bundle_text : "");
    g_ptr_array_add(view->action_entries, entry);
    g_ptr_array_sort(view->action_entries, gvr_vims_action_compare);
    gvr_vims_list_update_adjustment(&view->action_list);
    gtk_widget_queue_draw(view->action_list.area);
}

gboolean gvr_vims_view_set_bundle_binding(GtkWidget *widget,
                                          int event_id,
                                          const char *modifier_text,
                                          const char *key_text,
                                          int modifier,
                                          int key)
{
    GvrVimsView *view;

    if(!GVR_IS_VIMS_VIEW(widget))
        return FALSE;
    view = GVR_VIMS_VIEW(widget);
    for(guint i = 0; i < view->action_entries->len; i++) {
        GvrVimsActionEntry *entry = g_ptr_array_index(view->action_entries, i);
        if(entry->is_bundle && entry->event_id == event_id) {
            entry->modifier = modifier;
            entry->key = key;
            g_free(entry->modifier_text);
            g_free(entry->key_text);
            entry->modifier_text = g_strdup(modifier_text ? modifier_text : "");
            entry->key_text = g_strdup(key_text ? key_text : "");
            gtk_widget_queue_draw(view->action_list.area);
            return TRUE;
        }
    }
    return FALSE;
}

gboolean gvr_vims_view_get_selected_action(GtkWidget *widget,
                                           int *event_id,
                                           int *key,
                                           int *modifier,
                                           gchar **args,
                                           gboolean *is_bundle,
                                           gchar **bundle_text)
{
    GvrVimsView *view;
    GvrVimsActionEntry *entry;
    int index;

    if(!GVR_IS_VIMS_VIEW(widget))
        return FALSE;
    view = GVR_VIMS_VIEW(widget);
    index = view->action_list.selected_index;
    if(index < 0 || index >= (int)view->action_entries->len)
        return FALSE;
    entry = g_ptr_array_index(view->action_entries, index);
    if(event_id)
        *event_id = entry->event_id;
    if(key)
        *key = entry->key;
    if(modifier)
        *modifier = entry->modifier;
    if(args)
        *args = g_strdup(entry->args);
    if(is_bundle)
        *is_bundle = entry->is_bundle;
    if(bundle_text)
        *bundle_text = g_strdup(entry->bundle_text);
    return TRUE;
}

gboolean gvr_vims_view_select_action(GtkWidget *widget,
                                     int event_id,
                                     int key,
                                     int modifier,
                                     gboolean is_bundle)
{
    GvrVimsView *view;

    if(!GVR_IS_VIMS_VIEW(widget))
        return FALSE;
    view = GVR_VIMS_VIEW(widget);
    for(guint i = 0; i < view->action_entries->len; i++) {
        GvrVimsActionEntry *entry = g_ptr_array_index(view->action_entries, i);
        if(entry->event_id == event_id && entry->is_bundle == is_bundle &&
           (is_bundle || (entry->key == key && entry->modifier == modifier))) {
            gvr_vims_list_set_selected(&view->action_list, (int)i, TRUE);
            return TRUE;
        }
    }
    return FALSE;
}

void gvr_vims_view_clear_midi(GtkWidget *widget)
{
    GvrVimsView *view;

    if(!GVR_IS_VIMS_VIEW(widget))
        return;
    view = GVR_VIMS_VIEW(widget);
    g_ptr_array_set_size(view->midi_entries, 0);
    view->midi_list.selected_index = -1;
    gtk_adjustment_set_value(view->midi_list.adjustment, 0);
    gvr_vims_list_update_adjustment(&view->midi_list);
    gtk_widget_queue_draw(view->midi_list.area);
    if(view->midi_unbind_button)
        gtk_widget_set_sensitive(view->midi_unbind_button, FALSE);
}

void gvr_vims_view_append_midi(GtkWidget *widget,
                               const char *mapping_key,
                               int event_type,
                               int parameter,
                               int extra,
                               const char *event_text,
                               const char *parameter_text,
                               const char *mode_text,
                               const char *source_widget,
                               const char *message)
{
    GvrVimsView *view;
    GvrVimsMidiEntry *entry;

    if(!GVR_IS_VIMS_VIEW(widget) || !mapping_key)
        return;
    view = GVR_VIMS_VIEW(widget);
    entry = g_new0(GvrVimsMidiEntry, 1);
    entry->mapping_key = g_strdup(mapping_key);
    entry->event_type = event_type;
    entry->parameter = parameter;
    entry->extra = extra;
    entry->event_text = g_strdup(event_text ? event_text : "MIDI event");
    entry->parameter_text = g_strdup(parameter_text ? parameter_text : "");
    entry->mode_text = g_strdup(mode_text ? mode_text : "fixed");
    entry->widget = g_strdup(source_widget && source_widget[0] ? source_widget : "Direct VIMS");
    entry->message = g_strdup(message ? message : "");
    g_ptr_array_add(view->midi_entries, entry);
    g_ptr_array_sort(view->midi_entries, gvr_vims_midi_compare);
    gvr_vims_list_update_adjustment(&view->midi_list);
    gtk_widget_queue_draw(view->midi_list.area);
}

gboolean gvr_vims_view_get_selected_midi(GtkWidget *widget,
                                          gchar **mapping_key,
                                          int *event_type,
                                          int *parameter)
{
    GvrVimsView *view;
    GvrVimsMidiEntry *entry;
    int index;

    if(!GVR_IS_VIMS_VIEW(widget))
        return FALSE;
    view = GVR_VIMS_VIEW(widget);
    index = view->midi_list.selected_index;
    if(index < 0 || index >= (int)view->midi_entries->len)
        return FALSE;
    entry = g_ptr_array_index(view->midi_entries, index);
    if(mapping_key)
        *mapping_key = g_strdup(entry->mapping_key);
    if(event_type)
        *event_type = entry->event_type;
    if(parameter)
        *parameter = entry->parameter;
    return TRUE;
}

void gvr_vims_view_clear_data(GtkWidget *widget)
{
    if(!GVR_IS_VIMS_VIEW(widget))
        return;
    gvr_vims_view_clear_namespace(widget);
    gvr_vims_view_clear_actions(widget);
    gvr_vims_view_clear_midi(widget);
}

void gvr_vims_view_set_command(GtkWidget *widget,
                               const char *command,
                               const char *hint,
                               gboolean sensitive)
{
    GvrVimsView *view;

    if(!GVR_IS_VIMS_VIEW(widget))
        return;
    view = GVR_VIMS_VIEW(widget);
    gtk_entry_set_text(GTK_ENTRY(view->command_entry), command ? command : "");
    gtk_label_set_text(GTK_LABEL(view->command_hint),
                       hint ? hint : "Pattern-ready VIMS row");
    gtk_widget_set_sensitive(view->command_entry, sensitive);
    if(view->command_send_button)
        gtk_widget_set_sensitive(view->command_send_button, sensitive);
    gtk_widget_set_sensitive(view->command_copy_button,
                             sensitive && command && command[0]);
    if(sensitive && command) {
        const char *colon = strchr(command, ':');
        if(colon)
            gtk_editable_set_position(GTK_EDITABLE(view->command_entry),
                                      (gint)(colon - command + 1));
    }
}

gchar *gvr_vims_view_get_command(GtkWidget *widget)
{
    GvrVimsView *view;
    gchar *text;
    gsize len;

    if(!GVR_IS_VIMS_VIEW(widget))
        return NULL;
    view = GVR_VIMS_VIEW(widget);
    text = g_strdup(gtk_entry_get_text(GTK_ENTRY(view->command_entry)));
    g_strstrip(text);
    len = strlen(text);
    if(len > 0 && text[len - 1] != ';') {
        gchar *normalized = g_strconcat(text, ";", NULL);
        g_free(text);
        text = normalized;
        gtk_entry_set_text(GTK_ENTRY(view->command_entry), text);
    }
    return text;
}

void gvr_vims_view_set_last_request(GtkWidget *widget, const char *request)
{
    GvrVimsView *view;

    if(!GVR_IS_VIMS_VIEW(widget))
        return;
    view = GVR_VIMS_VIEW(widget);
    g_free(view->last_request);
    view->last_request = g_strdup(request ? request : "");
    if(view->copy_request_button)
        gtk_widget_set_sensitive(view->copy_request_button,
                                 view->last_request[0] != '\0');
}

GtkWidget *gvr_vims_view_get_response_view(GtkWidget *widget)
{
    return GVR_IS_VIMS_VIEW(widget) ? GVR_VIMS_VIEW(widget)->response_view : NULL;
}

GtkWidget *gvr_vims_view_get_editor_view(GtkWidget *widget)
{
    return GVR_IS_VIMS_VIEW(widget) ? GVR_VIMS_VIEW(widget)->editor_view : NULL;
}

GtkWidget *gvr_vims_view_get_workspace_notebook(GtkWidget *widget)
{
    return GVR_IS_VIMS_VIEW(widget) ? GVR_VIMS_VIEW(widget)->workspace : NULL;
}

void gvr_vims_view_set_action_sensitive(GtkWidget *widget,
                                        GvrVimsViewAction action,
                                        gboolean sensitive)
{
    GvrVimsView *view;

    if(!GVR_IS_VIMS_VIEW(widget) || action < 0 || action >= GVR_VIMS_VIEW_N_ACTIONS)
        return;
    view = GVR_VIMS_VIEW(widget);
    for(guint i = 0; i < view->button_count[action]; i++) {
        GtkWidget *button = view->buttons[action][i];
        if(!button)
            continue;
        if(action == GVR_VIMS_VIEW_RUN && button == view->command_send_button)
            gtk_widget_set_sensitive(button,
                                     sensitive &&
                                     gtk_widget_get_sensitive(view->command_entry));
        else
            gtk_widget_set_sensitive(button, sensitive);
    }
}

void gvr_vims_view_show_workspace(GtkWidget *widget, int page)
{
    GtkWidget *notebook = gvr_vims_view_get_workspace_notebook(widget);
    if(notebook)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), page);
}

static void gvr_vims_view_set_text(GtkWidget *view, const char *text)
{
    GtkTextBuffer *buffer;
    GtkTextIter start;

    if(!view || !GTK_IS_TEXT_VIEW(view))
        return;
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer, text ? text : "", -1);
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_place_cursor(buffer, &start);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(view),
                                 &start,
                                 0.0,
                                 FALSE,
                                 0.0,
                                 0.0);
}

void gvr_vims_view_set_response(GtkWidget *widget, const char *text)
{
    gvr_vims_view_set_text(gvr_vims_view_get_response_view(widget), text);
    gvr_vims_view_show_workspace(widget, 0);
}

void gvr_vims_view_clear_response(GtkWidget *widget)
{
    gvr_vims_view_set_last_request(widget, NULL);
    gvr_vims_view_set_response(widget, "");
}

void gvr_vims_view_set_editor(GtkWidget *widget, const char *text)
{
    gvr_vims_view_set_text(gvr_vims_view_get_editor_view(widget), text);
}

gchar *gvr_vims_view_get_editor(GtkWidget *widget)
{
    GtkWidget *view = gvr_vims_view_get_editor_view(widget);
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;

    if(!view)
        return NULL;
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

void gvr_vims_view_clear_editor(GtkWidget *widget)
{
    gvr_vims_view_set_editor(widget, "");
}

void gvr_vims_view_append_editor(GtkWidget *widget, const char *text)
{
    GtkWidget *view = gvr_vims_view_get_editor_view(widget);
    GtkTextBuffer *buffer;
    GtkTextIter end;

    if(!view || !text)
        return;
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text, -1);
    gvr_vims_view_show_workspace(widget, 1);
}
