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
#include <string.h>
#include "gtkshapeselector.h"
#include "vj-api.h"

#define GVR_SHAPE_THUMB_WIDTH       64
#define GVR_SHAPE_THUMB_HEIGHT      64
#define GVR_SHAPE_ITEM_WIDTH        72
#define GVR_SHAPE_DEFAULT_HEIGHT    82
#define GVR_SHAPE_VISIBLE_MARGIN    16

enum {
    MODEL_PIXBUF,
    MODEL_VALUE,
    MODEL_LABEL,
    MODEL_NAME,
    MODEL_PATH,
    MODEL_TOOLTIP,
    MODEL_LOADED,
    MODEL_N_COLUMNS
};

enum {
    SIGNAL_SHAPE_CHANGED,
    SIGNAL_LAST
};

typedef struct {
    int value;
    char *path;
} GvrShapeThumbnailJob;

struct _GvrShapeSelector {
    GtkBox parent_instance;
    GtkWidget *selected_label;
    GtkWidget *count_label;
    GtkWidget *search_revealer;
    GtkWidget *search_entry;
    GtkWidget *scrolled;
    GtkWidget *icon_view;
    GtkListStore *store;
    GQueue *thumbnail_jobs;
    GHashTable *queued_values;
    guint thumbnail_source;
    guint visible_source;
    GtkAccelGroup *search_accel_group;
    GtkWidget *search_accel_window;
    int search_match_index;
    gboolean syncing;
    gboolean allow_random;
    guint shape_count;
    int active_value;
    char *active_name;
};

struct _GvrShapeSelectorClass {
    GtkBoxClass parent_class;
};

static guint gvr_shape_selector_signals[SIGNAL_LAST];

G_DEFINE_TYPE(GvrShapeSelector, gvr_shape_selector, GTK_TYPE_BOX)

static gpointer gvr_shape_value_key(int value)
{
    return GINT_TO_POINTER(value + 2);
}

static void gvr_shape_strip_image_extension(char *name)
{
    char *dot;

    if(!name)
        return;

    dot = strrchr(name, '.');
    if(dot && (g_ascii_strcasecmp(dot, ".png") == 0 ||
               g_ascii_strcasecmp(dot, ".pgm") == 0 ||
               g_ascii_strcasecmp(dot, ".tif") == 0 ||
               g_ascii_strcasecmp(dot, ".tiff") == 0))
        *dot = '\0';
}

static char *gvr_shape_tooltip_name(const char *name)
{
    char *display = g_path_get_basename((name && name[0]) ? name : "Shape");

    g_strstrip(display);
    gvr_shape_strip_image_extension(display);
    g_strstrip(display);

    if(!display[0]) {
        g_free(display);
        return g_strdup("Shape");
    }

    return display;
}

static char *gvr_shape_display_name(const char *name)
{
    char *display = gvr_shape_tooltip_name(name);
    const char suffix[] = " (TimFX)";
    size_t length = strlen(display);
    size_t suffix_length = sizeof(suffix) - 1;

    if(length >= suffix_length &&
       g_ascii_strcasecmp(display + length - suffix_length, suffix) == 0) {
        display[length - suffix_length] = '\0';
        g_strstrip(display);
    }

    return display;
}

static gboolean gvr_shape_is_image(const char *name)
{
    const char *dot;

    if(!name)
        return FALSE;

    dot = strrchr(name, '.');
    if(!dot)
        return FALSE;

    return g_ascii_strcasecmp(dot, ".png") == 0 ||
           g_ascii_strcasecmp(dot, ".pgm") == 0 ||
           g_ascii_strcasecmp(dot, ".tif") == 0 ||
           g_ascii_strcasecmp(dot, ".tiff") == 0;
}

static void gvr_shape_scan_directory(const char *directory,
                                     GHashTable *paths)
{
    GDir *dir;
    const char *name;

    if(!directory || !paths)
        return;

    dir = g_dir_open(directory, 0, NULL);
    if(!dir)
        return;

    while((name = g_dir_read_name(dir)) != NULL) {
        char *path;

        if(name[0] == '.' && (name[1] == '\0' ||
           (name[1] == '.' && name[2] == '\0')))
            continue;

        path = g_build_filename(directory, name, NULL);
        if(g_file_test(path, G_FILE_TEST_IS_DIR)) {
            gvr_shape_scan_directory(path, paths);
            g_free(path);
            continue;
        }

        if(g_file_test(path, G_FILE_TEST_IS_REGULAR) &&
           gvr_shape_is_image(name) &&
           !g_hash_table_contains(paths, name))
            g_hash_table_insert(paths, g_strdup(name), path);
        else
            g_free(path);
    }

    g_dir_close(dir);
}

static GHashTable *gvr_shape_local_paths(void)
{
    GHashTable *paths = g_hash_table_new_full(g_str_hash,
                                              g_str_equal,
                                              g_free,
                                              g_free);
    const char *home = g_get_home_dir();

    if(home) {
        char *user = g_build_filename(home, ".veejay", "shapes", NULL);
        gvr_shape_scan_directory(user, paths);
        g_free(user);
    }

    gvr_shape_scan_directory("/usr/local/share/veejay/shapes", paths);
    gvr_shape_scan_directory("/usr/share/veejay/shapes", paths);
    return paths;
}

static GdkPixbuf *gvr_shape_placeholder(gboolean random_item)
{
    GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
                                       TRUE,
                                       8,
                                       GVR_SHAPE_THUMB_WIDTH,
                                       GVR_SHAPE_THUMB_HEIGHT);
    if(pixbuf)
        gdk_pixbuf_fill(pixbuf,
                        random_item ? 0x3d4a66ffu : 0x323540ffu);
    return pixbuf;
}

static void gvr_shape_thumbnail_job_free(gpointer data)
{
    GvrShapeThumbnailJob *job = data;

    if(!job)
        return;
    g_free(job->path);
    g_free(job);
}

static gboolean gvr_shape_find_value(GvrShapeSelector *selector,
                                     int value,
                                     GtkTreeIter *result,
                                     GtkTreePath **result_path)
{
    GtkTreeIter iter;
    gboolean valid;

    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(selector->store), &iter);
    while(valid) {
        int row_value = 0;

        gtk_tree_model_get(GTK_TREE_MODEL(selector->store),
                           &iter,
                           MODEL_VALUE, &row_value,
                           -1);
        if(row_value == value) {
            if(result)
                *result = iter;
            if(result_path)
                *result_path = gtk_tree_model_get_path(
                    GTK_TREE_MODEL(selector->store), &iter);
            return TRUE;
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(selector->store), &iter);
    }

    return FALSE;
}

static void gvr_shape_update_header(GvrShapeSelector *selector)
{
    char count[64];
    char selected[256];

    if(selector->shape_count == 0) {
        gtk_label_set_text(GTK_LABEL(selector->selected_label),
                           "No Shape Wipe masks available");
        gtk_label_set_text(GTK_LABEL(selector->count_label), "");
        return;
    }

    if(selector->active_value < 0)
        g_snprintf(selected, sizeof(selected), "Random shape");
    else
        g_snprintf(selected,
                   sizeof(selected),
                   "#%d %s",
                   selector->active_value,
                   selector->active_name ? selector->active_name : "Shape");
    gtk_label_set_text(GTK_LABEL(selector->selected_label), selected);

    g_snprintf(count,
               sizeof(count),
               "%u shape%s",
               selector->shape_count,
               selector->shape_count == 1 ? "" : "s");
    gtk_label_set_text(GTK_LABEL(selector->count_label), count);
}

static void gvr_shape_sync_active(GvrShapeSelector *selector)
{
    GList *selected;
    GtkTreePath *path;
    GtkTreeIter iter;

    selected = gtk_icon_view_get_selected_items(
        GTK_ICON_VIEW(selector->icon_view));
    if(!selected)
        return;

    path = selected->data;
    if(gtk_tree_model_get_iter(GTK_TREE_MODEL(selector->store), &iter, path)) {
        g_free(selector->active_name);
        selector->active_name = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(selector->store),
                           &iter,
                           MODEL_VALUE, &selector->active_value,
                           MODEL_NAME, &selector->active_name,
                           -1);
    }

    g_list_free_full(selected, (GDestroyNotify)gtk_tree_path_free);
    gvr_shape_update_header(selector);
}

static void gvr_shape_selection_changed(GtkIconView *icon_view,
                                        gpointer user_data)
{
    GvrShapeSelector *selector = GVR_SHAPE_SELECTOR(user_data);
    (void)icon_view;

    gvr_shape_sync_active(selector);
    if(selector->syncing)
        return;

    g_signal_emit(selector,
                  gvr_shape_selector_signals[SIGNAL_SHAPE_CHANGED],
                  0,
                  selector->active_value);
}

static void gvr_shape_queue_job(GvrShapeSelector *selector,
                                int value,
                                const char *path)
{
    GvrShapeThumbnailJob *job;
    gpointer key;

    if(!path || !path[0])
        return;

    key = gvr_shape_value_key(value);
    if(g_hash_table_contains(selector->queued_values, key))
        return;

    job = g_new0(GvrShapeThumbnailJob, 1);
    job->value = value;
    job->path = g_strdup(path);
    g_hash_table_add(selector->queued_values, key);
    g_queue_push_tail(selector->thumbnail_jobs, job);
}

static gboolean gvr_shape_set_row_pixbuf(GvrShapeSelector *selector,
                                         int value,
                                         GdkPixbuf *pixbuf)
{
    GtkTreeIter iter;

    if(!gvr_shape_find_value(selector, value, &iter, NULL))
        return FALSE;

    gtk_list_store_set(selector->store,
                       &iter,
                       MODEL_PIXBUF, pixbuf,
                       MODEL_LOADED, TRUE,
                       -1);
    return TRUE;
}

static gboolean gvr_shape_thumbnail_idle(gpointer data)
{
    GvrShapeSelector *selector = GVR_SHAPE_SELECTOR(data);
    GvrShapeThumbnailJob *job;
    GdkPixbuf *pixbuf;

    job = g_queue_pop_head(selector->thumbnail_jobs);
    if(!job) {
        selector->thumbnail_source = 0;
        return G_SOURCE_REMOVE;
    }

    pixbuf = gdk_pixbuf_new_from_file_at_scale(job->path,
                                               GVR_SHAPE_THUMB_WIDTH,
                                               GVR_SHAPE_THUMB_HEIGHT,
                                               TRUE,
                                               NULL);
    if(pixbuf) {
        gvr_shape_set_row_pixbuf(selector, job->value, pixbuf);
        g_object_unref(pixbuf);
    }
    else {
        GtkTreeIter iter;
        if(gvr_shape_find_value(selector, job->value, &iter, NULL))
            gtk_list_store_set(selector->store,
                               &iter,
                               MODEL_LOADED, TRUE,
                               -1);
    }

    g_hash_table_remove(selector->queued_values,
                        gvr_shape_value_key(job->value));
    gvr_shape_thumbnail_job_free(job);
    return G_SOURCE_CONTINUE;
}

static void gvr_shape_start_thumbnail_source(GvrShapeSelector *selector)
{
    if(selector->thumbnail_source ||
       g_queue_is_empty(selector->thumbnail_jobs))
        return;

    selector->thumbnail_source = g_idle_add_full(G_PRIORITY_LOW,
                                                  gvr_shape_thumbnail_idle,
                                                  g_object_ref(selector),
                                                  g_object_unref);
}

static void gvr_shape_queue_index(GvrShapeSelector *selector, int index)
{
    GtkTreePath *tree_path;
    GtkTreeIter iter;
    gboolean loaded = FALSE;
    int value = 0;
    char *path = NULL;

    if(index < 0)
        return;

    tree_path = gtk_tree_path_new_from_indices(index, -1);
    if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(selector->store),
                                &iter,
                                tree_path)) {
        gtk_tree_path_free(tree_path);
        return;
    }
    gtk_tree_path_free(tree_path);

    gtk_tree_model_get(GTK_TREE_MODEL(selector->store),
                       &iter,
                       MODEL_VALUE, &value,
                       MODEL_PATH, &path,
                       MODEL_LOADED, &loaded,
                       -1);
    if(!loaded)
        gvr_shape_queue_job(selector, value, path);
    g_free(path);
}

static void gvr_shape_queue_visible(GvrShapeSelector *selector)
{
    GtkTreePath *start = NULL;
    GtkTreePath *end = NULL;
    const int model_count = (int)selector->shape_count +
                            (selector->allow_random ? 1 : 0);
    int first = 0;
    int last = MIN(MAX(0, model_count - 1), 31);

    if(gtk_icon_view_get_visible_range(GTK_ICON_VIEW(selector->icon_view),
                                       &start,
                                       &end)) {
        int *indices;

        indices = gtk_tree_path_get_indices(start);
        if(indices)
            first = indices[0];
        indices = gtk_tree_path_get_indices(end);
        if(indices)
            last = indices[0];
    }

    first = MAX(0, first - GVR_SHAPE_VISIBLE_MARGIN);
    last += GVR_SHAPE_VISIBLE_MARGIN;
    for(int index = first; index <= last; index++)
        gvr_shape_queue_index(selector, index);

    if(start)
        gtk_tree_path_free(start);
    if(end)
        gtk_tree_path_free(end);
    gvr_shape_start_thumbnail_source(selector);
}

static gboolean gvr_shape_visible_idle(gpointer data)
{
    GvrShapeSelector *selector = GVR_SHAPE_SELECTOR(data);

    selector->visible_source = 0;
    gvr_shape_queue_visible(selector);
    return G_SOURCE_REMOVE;
}

static void gvr_shape_schedule_visible(GvrShapeSelector *selector)
{
    if(selector->visible_source)
        return;

    selector->visible_source = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                                gvr_shape_visible_idle,
                                                g_object_ref(selector),
                                                g_object_unref);
}

static gboolean gvr_shape_search_matches(const char *name,
                                         int value,
                                         const char *text)
{
    char number[32];
    char *query;
    char *folded_name;
    const char *number_query;
    gboolean matches;

    if(!text || !text[0])
        return FALSE;

    query = g_utf8_casefold(text, -1);
    g_strstrip(query);
    if(!query[0]) {
        g_free(query);
        return FALSE;
    }

    folded_name = g_utf8_casefold(name ? name : "", -1);
    g_snprintf(number, sizeof(number), "%d", value);
    number_query = query[0] == '#' ? query + 1 : query;
    matches = strstr(folded_name, query) != NULL ||
              (number_query[0] && strstr(number, number_query) != NULL);

    g_free(folded_name);
    g_free(query);
    return matches;
}

static gboolean gvr_shape_search_find(GvrShapeSelector *selector,
                                      const char *text)
{
    GtkTreeIter iter;
    gboolean valid;
    int index = 0;

    selector->search_match_index = -1;
    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(selector->store), &iter);
    while(valid) {
        int value = 0;
        char *name = NULL;

        gtk_tree_model_get(GTK_TREE_MODEL(selector->store),
                           &iter,
                           MODEL_VALUE, &value,
                           MODEL_NAME, &name,
                           -1);
        if(gvr_shape_search_matches(name, value, text)) {
            GtkTreePath *path = gtk_tree_model_get_path(
                GTK_TREE_MODEL(selector->store), &iter);

            selector->search_match_index = index;
            gtk_icon_view_set_cursor(GTK_ICON_VIEW(selector->icon_view),
                                     path,
                                     NULL,
                                     FALSE);
            gtk_icon_view_scroll_to_path(GTK_ICON_VIEW(selector->icon_view),
                                         path,
                                         TRUE,
                                         0.5f,
                                         0.5f);
            gvr_shape_queue_index(selector, index);
            gvr_shape_start_thumbnail_source(selector);
            gtk_tree_path_free(path);
            g_free(name);
            return TRUE;
        }
        g_free(name);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(selector->store), &iter);
        index++;
    }

    return FALSE;
}

static void gvr_shape_search_changed(GtkEditable *entry,
                                     gpointer user_data)
{
    GvrShapeSelector *selector = GVR_SHAPE_SELECTOR(user_data);
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));

    if(!text || !text[0]) {
        selector->search_match_index = -1;
        return;
    }

    gvr_shape_search_find(selector, text);
}

static void gvr_shape_search_hide(GvrShapeSelector *selector)
{
    gtk_entry_set_text(GTK_ENTRY(selector->search_entry), "");
    gtk_widget_hide(selector->search_revealer);
    gtk_widget_grab_focus(selector->icon_view);
}

static void gvr_shape_search_activate(GtkEntry *entry,
                                      gpointer user_data)
{
    GvrShapeSelector *selector = GVR_SHAPE_SELECTOR(user_data);
    GtkTreePath *path;
    GtkTreeIter iter;
    int value = 0;

    (void)entry;
    if(selector->search_match_index < 0)
        return;

    path = gtk_tree_path_new_from_indices(selector->search_match_index, -1);
    if(gtk_tree_model_get_iter(GTK_TREE_MODEL(selector->store), &iter, path)) {
        gtk_tree_model_get(GTK_TREE_MODEL(selector->store),
                           &iter,
                           MODEL_VALUE, &value,
                           -1);
        gvr_shape_selector_set_active(GTK_WIDGET(selector), value);
    }
    gtk_tree_path_free(path);
    gvr_shape_search_hide(selector);
}

static gboolean gvr_shape_search_key_press(GtkWidget *widget,
                                           GdkEventKey *event,
                                           gpointer user_data)
{
    GvrShapeSelector *selector = GVR_SHAPE_SELECTOR(user_data);

    (void)widget;
    if(event->keyval == GDK_KEY_Escape) {
        gvr_shape_search_hide(selector);
        return TRUE;
    }

    return FALSE;
}

static void gvr_shape_search_show(GvrShapeSelector *selector)
{
    if(!gtk_widget_get_sensitive(selector->icon_view))
        return;

    gtk_widget_show(selector->search_revealer);
    gtk_widget_grab_focus(selector->search_entry);
    gtk_editable_select_region(GTK_EDITABLE(selector->search_entry), 0, -1);
}

static gboolean gvr_shape_search_accel(GtkAccelGroup *accel_group,
                                       GObject *acceleratable,
                                       guint keyval,
                                       GdkModifierType modifier,
                                       gpointer user_data)
{
    GvrShapeSelector *selector = GVR_SHAPE_SELECTOR(user_data);

    (void)accel_group;
    (void)acceleratable;
    (void)keyval;
    (void)modifier;
    if(!gtk_widget_get_mapped(GTK_WIDGET(selector)))
        return FALSE;

    gvr_shape_search_show(selector);
    return TRUE;
}

static void gvr_shape_search_accel_detach(GvrShapeSelector *selector)
{
    if(selector->search_accel_group) {
        if(GTK_IS_WINDOW(selector->search_accel_window))
            gtk_window_remove_accel_group(GTK_WINDOW(selector->search_accel_window),
                                          selector->search_accel_group);
    }

    g_clear_object(&selector->search_accel_group);
    selector->search_accel_window = NULL;
}

static void gvr_shape_selector_map(GtkWidget *widget,
                                   gpointer user_data)
{
    GvrShapeSelector *selector = GVR_SHAPE_SELECTOR(user_data);
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GClosure *closure;

    if(selector->search_accel_group)
        return;
    if(!GTK_IS_WINDOW(toplevel))
        return;

    selector->search_accel_group = gtk_accel_group_new();
    selector->search_accel_window = toplevel;
    closure = g_cclosure_new(G_CALLBACK(gvr_shape_search_accel),
                             selector,
                             NULL);
    gtk_accel_group_connect(selector->search_accel_group,
                            GDK_KEY_f,
                            GDK_CONTROL_MASK,
                            GTK_ACCEL_VISIBLE,
                            closure);
    gtk_window_add_accel_group(GTK_WINDOW(toplevel),
                               selector->search_accel_group);
}

static void gvr_shape_selector_unmap(GtkWidget *widget,
                                     gpointer user_data)
{
    (void)widget;
    gvr_shape_search_accel_detach(GVR_SHAPE_SELECTOR(user_data));
}

static void gvr_shape_adjustment_changed(GtkAdjustment *adjustment,
                                         gpointer user_data)
{
    (void)adjustment;
    gvr_shape_schedule_visible(GVR_SHAPE_SELECTOR(user_data));
}

static void gvr_shape_size_allocate(GtkWidget *widget,
                                    GtkAllocation *allocation,
                                    gpointer user_data)
{
    (void)widget;
    (void)allocation;
    gvr_shape_schedule_visible(GVR_SHAPE_SELECTOR(user_data));
}

static void gvr_shape_cancel_sources(GvrShapeSelector *selector)
{
    if(selector->thumbnail_source) {
        g_source_remove(selector->thumbnail_source);
        selector->thumbnail_source = 0;
    }
    if(selector->visible_source) {
        g_source_remove(selector->visible_source);
        selector->visible_source = 0;
    }

    while(!g_queue_is_empty(selector->thumbnail_jobs))
        gvr_shape_thumbnail_job_free(
            g_queue_pop_head(selector->thumbnail_jobs));
    g_hash_table_remove_all(selector->queued_values);
}

static void gvr_shape_selector_dispose(GObject *object)
{
    GvrShapeSelector *selector = GVR_SHAPE_SELECTOR(object);

    gvr_shape_cancel_sources(selector);
    gvr_shape_search_accel_detach(selector);
    g_clear_pointer(&selector->thumbnail_jobs, g_queue_free);
    g_clear_pointer(&selector->queued_values, g_hash_table_destroy);
    g_clear_pointer(&selector->active_name, g_free);
    G_OBJECT_CLASS(gvr_shape_selector_parent_class)->dispose(object);
}

static void gvr_shape_selector_class_init(GvrShapeSelectorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = gvr_shape_selector_dispose;
    gvr_shape_selector_signals[SIGNAL_SHAPE_CHANGED] =
        g_signal_new("shape-changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);
}

static gboolean gvr_shape_selector_query_tooltip(GtkWidget *widget,
                                                   gint x,
                                                   gint y,
                                                   gboolean keyboard_mode,
                                                   GtkTooltip *tooltip,
                                                   gpointer user_data)
{
    GtkTreeModel *model = NULL;
    GtkTreePath *path = NULL;
    GtkTreeIter iter;
    gchar *text = NULL;
    gboolean result = FALSE;

    (void)user_data;

    if(!gtk_icon_view_get_tooltip_context(GTK_ICON_VIEW(widget),
                                          &x,
                                          &y,
                                          keyboard_mode,
                                          &model,
                                          &path,
                                          &iter))
        return FALSE;

    gtk_tree_model_get(model, &iter, MODEL_TOOLTIP, &text, -1);
    if(text && text[0]) {
        gtk_icon_view_set_tooltip_item(GTK_ICON_VIEW(widget), tooltip, path);
        result = vj_gui_tooltip_set_text(widget, tooltip, text);
    }

    g_free(text);
    gtk_tree_path_free(path);
    return result;
}

static void gvr_shape_selector_init(GvrShapeSelector *selector)
{
    GtkWidget *header;
    GtkWidget *search_box;
    GtkAdjustment *vadjustment;

    gtk_orientable_set_orientation(GTK_ORIENTABLE(selector),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(selector), 3);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(selector)),
        "shape-selector");
    selector->thumbnail_jobs = g_queue_new();
    selector->queued_values = g_hash_table_new(g_direct_hash, g_direct_equal);
    selector->active_value = 0;
    selector->search_match_index = -1;

    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(header),
                                "shape-selector-header");
    selector->selected_label = gtk_label_new("No Shape Wipe masks available");
    selector->count_label = gtk_label_new("");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(selector->selected_label),
        "shape-selector-selected");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(selector->count_label),
        "shape-selector-count");
    gtk_widget_set_halign(selector->selected_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(selector->selected_label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(selector->selected_label),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(selector->count_label, GTK_ALIGN_END);
    vj_gui_widget_set_tooltip_text(selector->count_label,
                                "Press Ctrl+F to search shapes by name or number.");
    gtk_box_pack_start(GTK_BOX(header),
                       selector->selected_label,
                       TRUE,
                       TRUE,
                       2);
    gtk_box_pack_end(GTK_BOX(header),
                     selector->count_label,
                     FALSE,
                     FALSE,
                     2);
    gtk_box_pack_start(GTK_BOX(selector), header, FALSE, TRUE, 0);

    selector->search_revealer =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_no_show_all(selector->search_revealer, TRUE);
    gtk_widget_hide(selector->search_revealer);
    search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(search_box),
                                "shape-selector-search");
    selector->search_entry = gtk_entry_new();
    vj_gui_widget_set_tooltip_text(selector->search_entry,
                                "Search shape name or #");
    gtk_widget_set_hexpand(selector->search_entry, TRUE);
    gtk_widget_set_vexpand(selector->search_entry, FALSE);
    gtk_box_pack_start(GTK_BOX(search_box),
                       selector->search_entry,
                       TRUE,
                       TRUE,
                       0);
    gtk_container_add(GTK_CONTAINER(selector->search_revealer), search_box);
    gtk_box_pack_start(GTK_BOX(selector),
                       selector->search_revealer,
                       FALSE,
                       FALSE,
                       0);

    selector->store = gtk_list_store_new(MODEL_N_COLUMNS,
                                         GDK_TYPE_PIXBUF,
                                         G_TYPE_INT,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_BOOLEAN);
    selector->icon_view = gtk_icon_view_new_with_model(
        GTK_TREE_MODEL(selector->store));
    gtk_style_context_add_class(
        gtk_widget_get_style_context(selector->icon_view),
        "shape-selector-grid");
    gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(selector->icon_view),
                                    MODEL_PIXBUF);
    vj_gui_widget_set_has_tooltip(selector->icon_view, TRUE);
    gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(selector->icon_view),
                                     GTK_SELECTION_SINGLE);
    gtk_icon_view_set_item_width(GTK_ICON_VIEW(selector->icon_view),
                                 GVR_SHAPE_ITEM_WIDTH);
    gtk_icon_view_set_item_padding(GTK_ICON_VIEW(selector->icon_view), 3);
    gtk_icon_view_set_margin(GTK_ICON_VIEW(selector->icon_view), 2);
    gtk_icon_view_set_row_spacing(GTK_ICON_VIEW(selector->icon_view), 2);
    gtk_icon_view_set_column_spacing(GTK_ICON_VIEW(selector->icon_view), 2);
    gtk_widget_set_hexpand(selector->icon_view, TRUE);
    gtk_widget_set_vexpand(selector->icon_view, TRUE);

    selector->scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(selector->scrolled),
        "shape-selector-scroller");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(selector->scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(selector->scrolled),
                                        GTK_SHADOW_IN);
    gtk_widget_set_size_request(selector->scrolled,
                                -1,
                                GVR_SHAPE_DEFAULT_HEIGHT);
    gtk_widget_set_hexpand(selector->scrolled, TRUE);
    gtk_widget_set_vexpand(selector->scrolled, TRUE);
    gtk_container_add(GTK_CONTAINER(selector->scrolled), selector->icon_view);
    gtk_box_pack_start(GTK_BOX(selector), selector->scrolled, TRUE, TRUE, 0);

    g_signal_connect(selector->icon_view,
                     "query-tooltip",
                     G_CALLBACK(gvr_shape_selector_query_tooltip),
                     selector);
    g_signal_connect(selector->icon_view,
                     "selection-changed",
                     G_CALLBACK(gvr_shape_selection_changed),
                     selector);
    g_signal_connect(selector->search_entry,
                     "changed",
                     G_CALLBACK(gvr_shape_search_changed),
                     selector);
    g_signal_connect(selector->search_entry,
                     "activate",
                     G_CALLBACK(gvr_shape_search_activate),
                     selector);
    g_signal_connect(selector->search_entry,
                     "key-press-event",
                     G_CALLBACK(gvr_shape_search_key_press),
                     selector);
    g_signal_connect(selector,
                     "map",
                     G_CALLBACK(gvr_shape_selector_map),
                     selector);
    g_signal_connect(selector,
                     "unmap",
                     G_CALLBACK(gvr_shape_selector_unmap),
                     selector);
    g_signal_connect(selector->icon_view,
                     "size-allocate",
                     G_CALLBACK(gvr_shape_size_allocate),
                     selector);
    vadjustment = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(selector->scrolled));
    g_signal_connect(vadjustment,
                     "value-changed",
                     G_CALLBACK(gvr_shape_adjustment_changed),
                     selector);
}

GtkWidget *gvr_shape_selector_new(void)
{
    return g_object_new(GVR_TYPE_SHAPE_SELECTOR, NULL);
}

void gvr_shape_selector_set_catalog(GtkWidget *widget,
                                    const char *const *names,
                                    guint count,
                                    gboolean allow_random)
{
    GvrShapeSelector *selector;
    GHashTable *paths;
    GdkPixbuf *placeholder;
    int previous;

    if(!GVR_IS_SHAPE_SELECTOR(widget))
        return;

    selector = GVR_SHAPE_SELECTOR(widget);
    previous = gvr_shape_selector_get_active(widget);
    selector->allow_random = allow_random;
    selector->shape_count = count;
    gvr_shape_cancel_sources(selector);
    gtk_list_store_clear(selector->store);

    if(count == 0) {
        GtkTreeIter iter;

        placeholder = gvr_shape_placeholder(FALSE);
        gtk_list_store_append(selector->store, &iter);
        gtk_list_store_set(selector->store,
                           &iter,
                           MODEL_PIXBUF, placeholder,
                           MODEL_VALUE, 0,
                           MODEL_LABEL, "Unavailable",
                           MODEL_NAME, "Unavailable",
                           MODEL_PATH, NULL,
                           MODEL_TOOLTIP, "No Shape Wipe masks were reported by the backend.",
                           MODEL_LOADED, TRUE,
                           -1);
        if(placeholder)
            g_object_unref(placeholder);
        {
            GtkTreePath *unavailable_path = gtk_tree_model_get_path(
                GTK_TREE_MODEL(selector->store), &iter);
            selector->syncing = TRUE;
            gtk_icon_view_select_path(GTK_ICON_VIEW(selector->icon_view),
                                      unavailable_path);
            selector->syncing = FALSE;
            gtk_tree_path_free(unavailable_path);
        }
        selector->active_value = 0;
        g_free(selector->active_name);
        selector->active_name = g_strdup("Unavailable");
        gtk_widget_set_sensitive(selector->icon_view, FALSE);
        gvr_shape_update_header(selector);
        return;
    }

    paths = gvr_shape_local_paths();

    if(allow_random) {
        GtkTreeIter iter;

        placeholder = gvr_shape_placeholder(TRUE);
        gtk_list_store_append(selector->store, &iter);
        gtk_list_store_set(selector->store,
                           &iter,
                           MODEL_PIXBUF, placeholder,
                           MODEL_VALUE, -1,
                           MODEL_LABEL, "Random",
                           MODEL_NAME, "Random",
                           MODEL_PATH, NULL,
                           MODEL_TOOLTIP, "Choose a random Shape Wipe mask for each transition.",
                           MODEL_LOADED, TRUE,
                           -1);
        if(placeholder)
            g_object_unref(placeholder);
    }

    for(guint i = 0; i < count; i++) {
        const char *name = names && names[i] ? names[i] : "";
        char *basename = g_path_get_basename(name[0] ? name : "Shape");
        char *display_name = gvr_shape_display_name(name);
        char *tooltip_name = gvr_shape_tooltip_name(name);
        const char *path = g_hash_table_lookup(paths, basename);
        GtkTreeIter iter;
        char *tooltip = path ?
            g_strdup_printf("#%u %s\nLocal preview available.",
                            i,
                            tooltip_name) :
            g_strdup_printf("#%u %s\nNo matching local preview file; the backend shape remains selectable.",
                            i,
                            tooltip_name);

        placeholder = gvr_shape_placeholder(FALSE);
        gtk_list_store_append(selector->store, &iter);
        gtk_list_store_set(selector->store,
                           &iter,
                           MODEL_PIXBUF, placeholder,
                           MODEL_VALUE, (int)i,
                           MODEL_LABEL, "",
                           MODEL_NAME, display_name,
                           MODEL_PATH, path,
                           MODEL_TOOLTIP, tooltip,
                           MODEL_LOADED, path ? FALSE : TRUE,
                           -1);
        if(placeholder)
            g_object_unref(placeholder);
        g_free(tooltip);
        g_free(tooltip_name);
        g_free(display_name);
        g_free(basename);
    }

    g_hash_table_destroy(paths);
    gtk_widget_set_sensitive(selector->icon_view, TRUE);
    if((previous == -1 && allow_random) ||
       (previous >= 0 && (guint)previous < count))
        gvr_shape_selector_set_active(widget, previous);
    else
        gvr_shape_selector_set_active(widget, allow_random ? -1 : 0);
    gvr_shape_schedule_visible(selector);
}

void gvr_shape_selector_set_active(GtkWidget *widget, int shape)
{
    GvrShapeSelector *selector;
    GtkTreeIter iter;
    GtkTreePath *path = NULL;

    if(!GVR_IS_SHAPE_SELECTOR(widget))
        return;

    selector = GVR_SHAPE_SELECTOR(widget);
    if(!gvr_shape_find_value(selector, shape, &iter, &path)) {
        if(!gvr_shape_find_value(selector,
                                 selector->allow_random ? -1 : 0,
                                 &iter,
                                 &path))
            return;
    }

    selector->syncing = TRUE;
    gtk_icon_view_unselect_all(GTK_ICON_VIEW(selector->icon_view));
    gtk_icon_view_select_path(GTK_ICON_VIEW(selector->icon_view), path);
    gtk_icon_view_scroll_to_path(GTK_ICON_VIEW(selector->icon_view),
                                 path,
                                 TRUE,
                                 0.5f,
                                 0.5f);
    selector->syncing = FALSE;
    gvr_shape_sync_active(selector);

    {
        int *indices = gtk_tree_path_get_indices(path);
        if(indices)
            gvr_shape_queue_index(selector, indices[0]);
    }
    gtk_tree_path_free(path);
    gvr_shape_start_thumbnail_source(selector);
}

int gvr_shape_selector_get_active(GtkWidget *widget)
{
    if(!GVR_IS_SHAPE_SELECTOR(widget))
        return 0;
    return GVR_SHAPE_SELECTOR(widget)->active_value;
}

const char *gvr_shape_selector_get_active_name(GtkWidget *widget)
{
    if(!GVR_IS_SHAPE_SELECTOR(widget))
        return "";
    return GVR_SHAPE_SELECTOR(widget)->active_name ?
           GVR_SHAPE_SELECTOR(widget)->active_name : "";
}
