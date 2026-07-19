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
#include <string.h>
#include "gtkmediaview.h"

enum {
    MEDIA_COL_NAME,
    MEDIA_COL_TYPE,
    MEDIA_COL_LOCATION,
    MEDIA_COL_PATH,
    MEDIA_N_COLUMNS
};

enum {
    MEDIA_SIGNAL_REFRESH_REQUESTED,
    MEDIA_SIGNAL_FILE_ACTIVATED,
    MEDIA_SIGNAL_LAST
};

struct _GvrMediaView {
    GtkBox parent_instance;
    GtkWidget *refresh_button;
    GtkWidget *search_entry;
    GtkWidget *status_label;
    GtkWidget *tree;
    GtkListStore *store;
    GtkTreeModel *filter;
    guint file_count;
    gboolean updating;
};

struct _GvrMediaViewClass {
    GtkBoxClass parent_class;
};

static guint gvr_media_view_signals[MEDIA_SIGNAL_LAST];

static GtkTargetEntry gvr_media_drag_targets[] = {
    { (gchar *)GVR_MEDIA_VIEW_DND_TARGET, GTK_TARGET_SAME_APP, 0 }
};

G_DEFINE_TYPE(GvrMediaView, gvr_media_view, GTK_TYPE_BOX)

static void gvr_media_view_add_class(GtkWidget *widget, const char *name)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), name);
}

static void gvr_media_view_split_filename(const char *filename,
                                          char **name,
                                          char **location,
                                          char **type)
{
    const char *slash;
    const char *dot;

    slash = strrchr(filename, G_DIR_SEPARATOR);
    if(slash) {
        *name = g_strdup(slash + 1);
        if(slash == filename)
            *location = g_strdup(G_DIR_SEPARATOR_S);
        else
            *location = g_strndup(filename, slash - filename);
    }
    else {
        *name = g_strdup(filename);
        *location = g_strdup("Backend working directory");
    }

    dot = strrchr(*name, '.');
    if(dot && dot[1])
        *type = g_ascii_strup(dot + 1, -1);
    else
        *type = g_strdup("MEDIA");
}

static void gvr_media_view_update_status(GvrMediaView *view,
                                         const char *override)
{
    char text[160];

    if(override && override[0]) {
        gtk_label_set_text(GTK_LABEL(view->status_label), override);
        return;
    }

    if(view->updating) {
        gtk_label_set_text(GTK_LABEL(view->status_label), "Loading backend media…");
        return;
    }

    g_snprintf(text,
               sizeof(text),
               "%u media file%s · drag into Edit List to append · double-click to create sample",
               view->file_count,
               view->file_count == 1 ? "" : "s");
    gtk_label_set_text(GTK_LABEL(view->status_label), text);
}

static gboolean gvr_media_view_filter_visible(GtkTreeModel *model,
                                              GtkTreeIter *iter,
                                              gpointer user_data)
{
    GvrMediaView *view = GVR_MEDIA_VIEW(user_data);
    const char *needle_text;
    gchar *needle;
    gchar *name = NULL;
    gchar *type = NULL;
    gchar *location = NULL;
    gchar *path = NULL;
    gchar *haystack;
    gboolean visible;

    needle_text = gtk_entry_get_text(GTK_ENTRY(view->search_entry));
    if(!needle_text || !needle_text[0])
        return TRUE;

    gtk_tree_model_get(model,
                       iter,
                       MEDIA_COL_NAME, &name,
                       MEDIA_COL_TYPE, &type,
                       MEDIA_COL_LOCATION, &location,
                       MEDIA_COL_PATH, &path,
                       -1);

    needle = g_utf8_casefold(needle_text, -1);
    haystack = g_strdup_printf("%s\n%s\n%s\n%s",
                               name ? name : "",
                               type ? type : "",
                               location ? location : "",
                               path ? path : "");
    {
        gchar *folded = g_utf8_casefold(haystack, -1);
        visible = strstr(folded, needle) != NULL;
        g_free(folded);
    }

    g_free(haystack);
    g_free(needle);
    g_free(name);
    g_free(type);
    g_free(location);
    g_free(path);
    return visible;
}

static void gvr_media_view_search_changed(GtkEditable *editable,
                                          gpointer user_data)
{
    GvrMediaView *view = GVR_MEDIA_VIEW(user_data);
    (void)editable;
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(view->filter));
}

static gchar *gvr_media_view_selected_path(GvrMediaView *view)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    gchar *filename = NULL;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree));
    if(gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter, MEDIA_COL_PATH, &filename, -1);

    return filename;
}

static void gvr_media_view_refresh_clicked(GtkButton *button,
                                           gpointer user_data)
{
    GvrMediaView *view = GVR_MEDIA_VIEW(user_data);
    (void)button;
    g_signal_emit(view,
                  gvr_media_view_signals[MEDIA_SIGNAL_REFRESH_REQUESTED],
                  0);
}

static void gvr_media_view_row_activated(GtkTreeView *tree,
                                         GtkTreePath *path,
                                         GtkTreeViewColumn *column,
                                         gpointer user_data)
{
    GvrMediaView *view = GVR_MEDIA_VIEW(user_data);
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    gchar *filename = NULL;

    (void)column;

    if(!gtk_tree_model_get_iter(model, &iter, path))
        return;

    gtk_tree_model_get(model, &iter, MEDIA_COL_PATH, &filename, -1);
    if(filename && filename[0])
        g_signal_emit(view,
                      gvr_media_view_signals[MEDIA_SIGNAL_FILE_ACTIVATED],
                      0,
                      filename);
    g_free(filename);
}

static void gvr_media_view_drag_data_get(GtkWidget *widget,
                                         GdkDragContext *context,
                                         GtkSelectionData *selection,
                                         guint info,
                                         guint time,
                                         gpointer user_data)
{
    GvrMediaView *view = GVR_MEDIA_VIEW(user_data);
    gchar *filename = gvr_media_view_selected_path(view);

    (void)widget;
    (void)context;
    (void)info;
    (void)time;

    if(filename && filename[0]) {
        gtk_selection_data_set(selection,
                               gtk_selection_data_get_target(selection),
                               8,
                               (const guchar *)filename,
                               (gint)strlen(filename));
    }

    g_free(filename);
}

static void gvr_media_view_copy_path(GtkMenuItem *item,
                                     gpointer user_data)
{
    const char *filename = g_object_get_data(G_OBJECT(item), "gvr-media-path");
    (void)user_data;

    if(filename)
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                               filename,
                               -1);
}

static void gvr_media_view_add_sample(GtkMenuItem *item,
                                      gpointer user_data)
{
    GvrMediaView *view = GVR_MEDIA_VIEW(user_data);
    const char *filename = g_object_get_data(G_OBJECT(item), "gvr-media-path");

    if(filename)
        g_signal_emit(view,
                      gvr_media_view_signals[MEDIA_SIGNAL_FILE_ACTIVATED],
                      0,
                      filename);
}

static gboolean gvr_media_view_button_press(GtkWidget *widget,
                                            GdkEventButton *event,
                                            gpointer user_data)
{
    GvrMediaView *view = GVR_MEDIA_VIEW(user_data);
    GtkTreePath *path = NULL;

    if(event->button != 3)
        return FALSE;

    if(!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                      (gint)event->x,
                                      (gint)event->y,
                                      &path,
                                      NULL,
                                      NULL,
                                      NULL))
        return FALSE;

    gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, NULL, FALSE);
    gtk_tree_path_free(path);

    {
        gchar *filename = gvr_media_view_selected_path(view);
        GtkWidget *menu;
        GtkWidget *add_sample;
        GtkWidget *copy_path;

        if(!filename)
            return FALSE;

        menu = gtk_menu_new();
        add_sample = gtk_menu_item_new_with_label("Add as sample");
        copy_path = gtk_menu_item_new_with_label("Copy path");

        g_object_set_data_full(G_OBJECT(add_sample),
                               "gvr-media-path",
                               g_strdup(filename),
                               g_free);
        g_object_set_data_full(G_OBJECT(copy_path),
                               "gvr-media-path",
                               g_strdup(filename),
                               g_free);
        g_signal_connect(add_sample,
                         "activate",
                         G_CALLBACK(gvr_media_view_add_sample),
                         view);
        g_signal_connect(copy_path,
                         "activate",
                         G_CALLBACK(gvr_media_view_copy_path),
                         NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), add_sample);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_path);
        g_signal_connect(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        g_free(filename);
    }

    return TRUE;
}

static void gvr_media_view_finalize(GObject *object)
{
    GvrMediaView *view = GVR_MEDIA_VIEW(object);

    if(view->filter)
        g_object_unref(view->filter);
    if(view->store)
        g_object_unref(view->store);

    G_OBJECT_CLASS(gvr_media_view_parent_class)->finalize(object);
}

static void gvr_media_view_class_init(GvrMediaViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = gvr_media_view_finalize;

    gvr_media_view_signals[MEDIA_SIGNAL_REFRESH_REQUESTED] =
        g_signal_new("refresh-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    gvr_media_view_signals[MEDIA_SIGNAL_FILE_ACTIVATED] =
        g_signal_new("file-activated",
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

static void gvr_media_view_append_column(GvrMediaView *view,
                                         const char *title,
                                         int model_column,
                                         gboolean expand,
                                         int min_width)
{
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column =
        gtk_tree_view_column_new_with_attributes(title,
                                                  renderer,
                                                  "text",
                                                  model_column,
                                                  NULL);

    gtk_tree_view_column_set_expand(column, expand);
    gtk_tree_view_column_set_resizable(column, TRUE);
    if(min_width > 0)
        gtk_tree_view_column_set_min_width(column, min_width);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view->tree), column);
}

static void gvr_media_view_init(GvrMediaView *view)
{
    GtkWidget *toolbar;
    GtkWidget *title;
    GtkWidget *scrolled;
    GtkTreeSelection *selection;

    gtk_orientable_set_orientation(GTK_ORIENTABLE(view),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(view), 2);
    gvr_media_view_add_class(GTK_WIDGET(view), "media-view");

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gvr_media_view_add_class(toolbar, "media-view-toolbar");
    gtk_box_pack_start(GTK_BOX(view), toolbar, FALSE, FALSE, 0);

    title = gtk_label_new("Backend media");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gvr_media_view_add_class(title, "media-view-title");
    gtk_box_pack_start(GTK_BOX(toolbar), title, FALSE, FALSE, 4);

    view->search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->search_entry),
                                   "Filter media files");
    gtk_widget_set_hexpand(view->search_entry, TRUE);
    gvr_media_view_add_class(view->search_entry, "media-view-search");
    gtk_box_pack_start(GTK_BOX(toolbar), view->search_entry, TRUE, TRUE, 0);

    view->refresh_button = gtk_button_new_from_icon_name("view-refresh",
                                                         GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(view->refresh_button,
                                "Fetch video files from VeeJay's working directory");
    gtk_box_pack_end(GTK_BOX(toolbar), view->refresh_button, FALSE, FALSE, 0);

    view->store = gtk_list_store_new(MEDIA_N_COLUMNS,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(view->store),
                                         MEDIA_COL_NAME,
                                         GTK_SORT_ASCENDING);

    view->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(view->store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(view->filter),
                                           gvr_media_view_filter_visible,
                                           view,
                                           NULL);

    view->tree = gtk_tree_view_new_with_model(view->filter);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view->tree), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view->tree), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(view->tree), MEDIA_COL_NAME);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view->tree), MEDIA_COL_PATH);
    gvr_media_view_add_class(view->tree, "media-view-tree");

    gvr_media_view_append_column(view, "File", MEDIA_COL_NAME, TRUE, 180);
    gvr_media_view_append_column(view, "Type", MEDIA_COL_TYPE, FALSE, 58);
    gvr_media_view_append_column(view, "Location", MEDIA_COL_LOCATION, TRUE, 150);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

    gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(view->tree),
                                           GDK_BUTTON1_MASK,
                                           gvr_media_drag_targets,
                                           G_N_ELEMENTS(gvr_media_drag_targets),
                                           GDK_ACTION_COPY);

    g_signal_connect(view->refresh_button,
                     "clicked",
                     G_CALLBACK(gvr_media_view_refresh_clicked),
                     view);
    g_signal_connect(view->search_entry,
                     "changed",
                     G_CALLBACK(gvr_media_view_search_changed),
                     view);
    g_signal_connect(view->tree,
                     "row-activated",
                     G_CALLBACK(gvr_media_view_row_activated),
                     view);
    g_signal_connect(view->tree,
                     "drag-data-get",
                     G_CALLBACK(gvr_media_view_drag_data_get),
                     view);
    g_signal_connect(view->tree,
                     "button-press-event",
                     G_CALLBACK(gvr_media_view_button_press),
                     view);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                        GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scrolled), view->tree);
    gtk_box_pack_start(GTK_BOX(view), scrolled, TRUE, TRUE, 0);

    view->status_label = gtk_label_new("Press refresh to fetch backend media");
    gtk_widget_set_halign(view->status_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(view->status_label), PANGO_ELLIPSIZE_END);
    gvr_media_view_add_class(view->status_label, "media-view-status");
    gtk_box_pack_start(GTK_BOX(view), view->status_label, FALSE, FALSE, 2);

    gtk_widget_show_all(GTK_WIDGET(view));
}

GtkWidget *gvr_media_view_new(void)
{
    return g_object_new(GVR_TYPE_MEDIA_VIEW, NULL);
}

void gvr_media_view_begin_update(GtkWidget *widget)
{
    GvrMediaView *view;

    if(!GVR_IS_MEDIA_VIEW(widget))
        return;

    view = GVR_MEDIA_VIEW(widget);
    view->updating = TRUE;
    view->file_count = 0;
    gtk_list_store_clear(view->store);
    gtk_widget_set_sensitive(view->refresh_button, FALSE);
    gvr_media_view_update_status(view, NULL);
}

void gvr_media_view_append(GtkWidget *widget, const char *filename)
{
    GvrMediaView *view;
    GtkTreeIter iter;
    char *name;
    char *location;
    char *type;

    if(!GVR_IS_MEDIA_VIEW(widget) || !filename || !filename[0])
        return;

    view = GVR_MEDIA_VIEW(widget);
    gvr_media_view_split_filename(filename, &name, &location, &type);

    gtk_list_store_append(view->store, &iter);
    gtk_list_store_set(view->store,
                       &iter,
                       MEDIA_COL_NAME, name,
                       MEDIA_COL_TYPE, type,
                       MEDIA_COL_LOCATION, location,
                       MEDIA_COL_PATH, filename,
                       -1);
    view->file_count++;

    g_free(name);
    g_free(location);
    g_free(type);
}

void gvr_media_view_end_update(GtkWidget *widget)
{
    GvrMediaView *view;

    if(!GVR_IS_MEDIA_VIEW(widget))
        return;

    view = GVR_MEDIA_VIEW(widget);
    view->updating = FALSE;
    gtk_widget_set_sensitive(view->refresh_button, TRUE);
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(view->filter));
    gvr_media_view_update_status(view, NULL);
}

void gvr_media_view_set_error(GtkWidget *widget, const char *message)
{
    GvrMediaView *view;

    if(!GVR_IS_MEDIA_VIEW(widget))
        return;

    view = GVR_MEDIA_VIEW(widget);
    view->updating = FALSE;
    gtk_widget_set_sensitive(view->refresh_button, TRUE);
    gvr_media_view_update_status(view,
                                 message && message[0] ? message :
                                 "Unable to fetch backend media");
}

void gvr_media_view_clear(GtkWidget *widget)
{
    GvrMediaView *view;

    if(!GVR_IS_MEDIA_VIEW(widget))
        return;

    view = GVR_MEDIA_VIEW(widget);
    view->updating = FALSE;
    view->file_count = 0;
    gtk_list_store_clear(view->store);
    gtk_widget_set_sensitive(view->refresh_button, TRUE);
    gvr_media_view_update_status(view, NULL);
}
