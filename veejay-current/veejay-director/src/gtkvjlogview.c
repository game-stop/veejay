/* director - Linux VeeJay - GVeejay GTK+-3/Glade User Interface 
 * (C) 2026 Niels Elburg <nwelburg@gmail.com>
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
#include <string.h>
#include "gtkvjlogview.h"

#define GVR_LOG_DEFAULT_MAX_LINES 12000

struct _GvrLogView {
    GtkTextView parent_instance;
    GtkTextTag *normal;
    GtkTextTag *bold;
    GtkTextTag *red;
    GtkTextTag *red_bold;
    GtkTextTag *green;
    GtkTextTag *green_bold;
    GtkTextTag *yellow;
    GtkTextTag *yellow_bold;
    GtkTextTag *blue;
    GtkTextTag *blue_bold;
    GtkTextTag *white;
    GtkTextTag *white_bold;
    GtkTextTag *cyan;
    GtkTextTag *magenta;
    guint max_lines;
    gboolean ansi_bold;
    gint ansi_color;
    GString *ansi_pending;
};

G_DEFINE_TYPE(GvrLogView, gvr_log_view, GTK_TYPE_TEXT_VIEW)

static GtkTextTag *gvr_log_view_tag(GvrLogView *view, gint color, gboolean bold)
{
    switch(color) {
        case 31: return bold ? view->red_bold : view->red;
        case 32: return bold ? view->green_bold : view->green;
        case 33: return bold ? view->yellow_bold : view->yellow;
        case 34: return bold ? view->blue_bold : view->blue;
        case 35: return view->magenta;
        case 36: return view->cyan;
        case 37: return bold ? view->white_bold : view->white;
        default: return bold ? view->bold : view->normal;
    }
}

static void gvr_log_view_trim(GvrLogView *view)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gint lines = gtk_text_buffer_get_line_count(buffer);
    if(view->max_lines == 0 || lines <= (gint)view->max_lines)
        return;

    gint remove_lines = lines - (gint)view->max_lines;
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_iter_at_line(buffer, &end, remove_lines);
    gtk_text_buffer_delete(buffer, &start, &end);
}

static void gvr_log_view_init(GvrLogView *view)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);

    view->max_lines = GVR_LOG_DEFAULT_MAX_LINES;
    view->ansi_pending = g_string_new(NULL);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 6);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 6);

    view->normal = gtk_text_buffer_create_tag(buffer, "vj-normal",
                                               "foreground", "#d8dee9", NULL);
    view->bold = gtk_text_buffer_create_tag(buffer, "vj-bold",
                                             "foreground", "#ffffff",
                                             "weight", PANGO_WEIGHT_BOLD, NULL);
    view->red = gtk_text_buffer_create_tag(buffer, "vj-red",
                                            "foreground", "#ff5f56", NULL);
    view->red_bold = gtk_text_buffer_create_tag(buffer, "vj-red-bold",
                                                 "foreground", "#ff6b65",
                                                 "weight", PANGO_WEIGHT_BOLD, NULL);
    view->green = gtk_text_buffer_create_tag(buffer, "vj-green",
                                              "foreground", "#8bd450", NULL);
    view->green_bold = gtk_text_buffer_create_tag(buffer, "vj-green-bold",
                                                   "foreground", "#a7e06e",
                                                   "weight", PANGO_WEIGHT_BOLD, NULL);
    view->yellow = gtk_text_buffer_create_tag(buffer, "vj-yellow",
                                               "foreground", "#f5c451", NULL);
    view->yellow_bold = gtk_text_buffer_create_tag(buffer, "vj-yellow-bold",
                                                    "foreground", "#ffd66b",
                                                    "weight", PANGO_WEIGHT_BOLD, NULL);
    view->blue = gtk_text_buffer_create_tag(buffer, "vj-blue",
                                             "foreground", "#5da9ff", NULL);
    view->blue_bold = gtk_text_buffer_create_tag(buffer, "vj-blue-bold",
                                                  "foreground", "#7bbaff",
                                                  "weight", PANGO_WEIGHT_BOLD, NULL);
    view->white = gtk_text_buffer_create_tag(buffer, "vj-white",
                                              "foreground", "#e6e6e6", NULL);
    view->white_bold = gtk_text_buffer_create_tag(buffer, "vj-white-bold",
                                                   "foreground", "#ffffff",
                                                   "weight", PANGO_WEIGHT_BOLD, NULL);
    view->cyan = gtk_text_buffer_create_tag(buffer, "vj-cyan",
                                             "foreground", "#5fd7d7", NULL);
    view->magenta = gtk_text_buffer_create_tag(buffer, "vj-magenta",
                                                "foreground", "#d787ff", NULL);

    (void)table;

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "textview.vj-console, textview.vj-console text {"
        " background-color: #27282f; color: #ffffff;"
        " font-family: monospace; font-size: 10.5pt;"
        "}"
        "textview.vj-console text selection {"
        " background-color: #3d4a66; color: #ffffff;"
        "}", -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(GTK_WIDGET(view));
    gtk_style_context_add_class(context, "vj-console");
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void gvr_log_view_finalize(GObject *object)
{
    GvrLogView *view = GVR_LOG_VIEW(object);
    if(view->ansi_pending)
        g_string_free(view->ansi_pending, TRUE);
    G_OBJECT_CLASS(gvr_log_view_parent_class)->finalize(object);
}

static void gvr_log_view_class_init(GvrLogViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = gvr_log_view_finalize;
}

GtkWidget *gvr_log_view_new(void)
{
    return g_object_new(GVR_TYPE_LOG_VIEW, NULL);
}

void gvr_log_view_set_max_lines(GvrLogView *view, guint max_lines)
{
    g_return_if_fail(GVR_IS_LOG_VIEW(view));
    view->max_lines = max_lines;
    gvr_log_view_trim(view);
}

void gvr_log_view_clear(GvrLogView *view)
{
    g_return_if_fail(GVR_IS_LOG_VIEW(view));
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer, "", -1);
    view->ansi_bold = FALSE;
    view->ansi_color = 0;
    if(view->ansi_pending)
        g_string_truncate(view->ansi_pending, 0);
}

void gvr_log_view_append(GvrLogView *view, const gchar *text)
{
    g_return_if_fail(GVR_IS_LOG_VIEW(view));
    if(!text || !*text)
        return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);

    GString *input = g_string_new(NULL);
    if(view->ansi_pending && view->ansi_pending->len > 0) {
        g_string_append_len(input, view->ansi_pending->str, view->ansi_pending->len);
        g_string_truncate(view->ansi_pending, 0);
    }
    g_string_append(input, text);

    gboolean bold = view->ansi_bold;
    gint color = view->ansi_color;
    const gchar *run = input->str;
    const gchar *p = input->str;

    while(*p) {
        if((guchar)p[0] == 0x1b && p[1] == '[') {
            const gchar *q = p + 2;
            gint params[8];
            gint n_params = 0;
            gint current = 0;
            gboolean have_digit = FALSE;
            gboolean valid = TRUE;

            while(*q && *q != 'm' && n_params < 8) {
                if(g_ascii_isdigit(*q)) {
                    current = current * 10 + (*q - '0');
                    have_digit = TRUE;
                }
                else if(*q == ';') {
                    params[n_params++] = have_digit ? current : 0;
                    current = 0;
                    have_digit = FALSE;
                }
                else {
                    valid = FALSE;
                    break;
                }
                q++;
            }

            if(valid && !*q) {
                if(p > run) {
                    GtkTextTag *tag = gvr_log_view_tag(view, color, bold);
                    gtk_text_buffer_insert_with_tags(buffer, &end, run, (gint)(p - run), tag, NULL);
                }
                g_string_append(view->ansi_pending, p);
                run = p + strlen(p);
                p = run;
                break;
            }

            if(valid && *q == 'm') {
                if(p > run) {
                    GtkTextTag *tag = gvr_log_view_tag(view, color, bold);
                    gtk_text_buffer_insert_with_tags(buffer, &end, run, (gint)(p - run), tag, NULL);
                }
                if(have_digit || n_params == 0)
                    params[n_params++] = have_digit ? current : 0;
                for(gint i = 0; i < n_params; i++) {
                    gint code = params[i];
                    if(code == 0) {
                        bold = FALSE;
                        color = 0;
                    }
                    else if(code == 1)
                        bold = TRUE;
                    else if(code == 22)
                        bold = FALSE;
                    else if(code == 39)
                        color = 0;
                    else if(code >= 30 && code <= 37)
                        color = code;
                    else if(code >= 90 && code <= 97)
                        color = code - 60;
                }
                p = q + 1;
                run = p;
                continue;
            }
        }
        p++;
    }

    if(p > run) {
        GtkTextTag *tag = gvr_log_view_tag(view, color, bold);
        gtk_text_buffer_insert_with_tags(buffer, &end, run, (gint)(p - run), tag, NULL);
    }
    view->ansi_bold = bold;
    view->ansi_color = color;
    g_string_free(input, TRUE);

    gvr_log_view_trim(view);
    gtk_text_buffer_get_end_iter(buffer, &end);
    GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, &end, FALSE);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(view), mark);
    gtk_text_buffer_delete_mark(buffer, mark);
}
