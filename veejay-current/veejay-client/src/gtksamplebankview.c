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
#include <stdio.h>
#include <string.h>
#include "gtksamplebankview.h"

#ifndef GVR_SAMPLE_BANK_MAX_PAGES
#define GVR_SAMPLE_BANK_MAX_PAGES 512
#endif
#ifndef GVR_SAMPLE_BANK_SLOTS
#define GVR_SAMPLE_BANK_SLOTS 12
#endif
#ifndef GVR_SAMPLE_BANK_COLUMNS
#define GVR_SAMPLE_BANK_COLUMNS 6
#endif
#ifndef GVR_SAMPLE_BANK_ROWS
#define GVR_SAMPLE_BANK_ROWS 2
#endif
#ifndef GVR_SAMPLE_BANK_VISIBLE_MAX
#define GVR_SAMPLE_BANK_VISIBLE_MAX 512
#endif
#ifndef GVR_SAMPLE_BANK_DYNAMIC_CELL
#define GVR_SAMPLE_BANK_DYNAMIC_CELL 120
#endif

#define GVR_SB_TYPE_SAMPLE      0
#define GVR_SB_TYPE_YUV4MPEG    1
#define GVR_SB_TYPE_V4L         2
#define GVR_SB_TYPE_VLOOPBACK   3
#define GVR_SB_TYPE_COLOR       4
#define GVR_SB_TYPE_PICTURE     5
#define GVR_SB_TYPE_CALI        6
#define GVR_SB_TYPE_GENERATOR   7
#define GVR_SB_TYPE_SPLITTER    8
#define GVR_SB_TYPE_SHM        11
#define GVR_SB_TYPE_AVFORMAT   12
#define GVR_SB_TYPE_NET        13
#define GVR_SB_TYPE_MCAST      14
#define GVR_SB_TYPE_CLONE      15
#define GVR_SB_TYPE_DV1394     17

typedef struct {
    int sample_id;
    int sample_type;
    char *title;
    char *timecode;
    GdkPixbuf *thumb;
} GvrSampleBankCell;

typedef struct {
    GvrSampleBankCell cells[GVR_SAMPLE_BANK_SLOTS];
    int size;
} GvrSampleBankPage;

struct _GvrSampleBankView {
    GtkDrawingArea parent_instance;
    GvrSampleBankPage *pages;
    int columns;
    int rows;
    int page_count;
    int current_page;
    int selected_page;
    int selected_slot;
    int hover_page;
    int hover_slot;
    int current_sample_id;
    int current_sample_type;
    GdkRectangle header_rect;
    GdkRectangle cell_rect[GVR_SAMPLE_BANK_VISIBLE_MAX];
    int cell_page[GVR_SAMPLE_BANK_VISIBLE_MAX];
    int cell_slot[GVR_SAMPLE_BANK_VISIBLE_MAX];
    int layout_cells;
    int layout_columns;
    int layout_rows;
};

struct _GvrSampleBankViewClass {
    GtkDrawingAreaClass parent_class;
};

enum {
    SIGNAL_PAGE_SELECTED,
    SIGNAL_SLOT_SELECTED,
    SIGNAL_SLOT_ACTIVATED,
    SIGNAL_SLOT_MIX_REQUESTED,
    SIGNAL_LAST
};

static guint gvr_sample_bank_view_signals[SIGNAL_LAST];

G_DEFINE_TYPE(GvrSampleBankView, gvr_sample_bank_view, GTK_TYPE_DRAWING_AREA)


typedef struct {
    char family[128];
    double points;
} GvrSampleBankFontMetrics;

static void gvr_sample_bank_font_metrics(GtkWidget *widget,
                                         GvrSampleBankFontMetrics *metrics)
{
    GtkStyleContext *style = NULL;
    PangoFontDescription *font = NULL;
    const char *family;
    int size;

    g_strlcpy(metrics->family, "Sans", sizeof(metrics->family));
    metrics->points = 10.0;

    if(widget) {
        style = gtk_widget_get_style_context(widget);
        if(style)
            gtk_style_context_get(style,
                                  gtk_style_context_get_state(style),
                                  "font",
                                  &font,
                                  NULL);
    }

    if(!font && widget) {
        PangoContext *context = gtk_widget_get_pango_context(widget);
        const PangoFontDescription *fallback =
            context ? pango_context_get_font_description(context) : NULL;

        if(fallback)
            font = pango_font_description_copy(fallback);
    }

    if(!font)
        return;

    family = pango_font_description_get_family(font);
    if(family && family[0])
        g_strlcpy(metrics->family, family, sizeof(metrics->family));

    size = pango_font_description_get_size(font);
    if(size > 0) {
        metrics->points = (double)size / PANGO_SCALE;
        if(pango_font_description_get_size_is_absolute(font))
            metrics->points *= 72.0 / 96.0;
        metrics->points = CLAMP(metrics->points, 6.0, 32.0);
    }

    pango_font_description_free(font);
}

static double gvr_sample_bank_font_px(
        const GvrSampleBankFontMetrics *metrics,
        double scale)
{
    return MAX(7.0,
               metrics->points *
               (96.0 / 72.0) * scale);
}

static int gvr_sample_bank_footer_height(
        const GvrSampleBankFontMetrics *metrics)
{
    const double title_font = gvr_sample_bank_font_px(metrics, 0.82);
    const double meta_font = gvr_sample_bank_font_px(metrics, 0.76);

    return MAX(36, (int)ceil(title_font + meta_font + 16.0));
}

static int gvr_sample_bank_cell_side(
        const GvrSampleBankFontMetrics *metrics)
{
    return MAX(120, 84 + gvr_sample_bank_footer_height(metrics));
}

static void gvr_sample_bank_view_update_size_request(GtkWidget *widget)
{
    GvrSampleBankView *view;
    GvrSampleBankFontMetrics metrics;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    gvr_sample_bank_font_metrics(widget, &metrics);

    const int side = gvr_sample_bank_cell_side(&metrics);

    gtk_widget_set_size_request(widget,
                                view->columns * side,
                                view->rows * side);
}

static int gvr_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static gboolean gvr_rect_contains(const GdkRectangle *r, double x, double y)
{
    return r && x >= r->x && y >= r->y && x < (r->x + r->width) && y < (r->y + r->height);
}

static void gvr_set_rgba(cairo_t *cr, double r, double g, double b, double a)
{
    cairo_set_source_rgba(cr, r, g, b, a);
}

static void gvr_sample_slot_color(int sample_type, double *r, double *g, double *b)
{
    switch(sample_type) {
        case GVR_SB_TYPE_SAMPLE:    *r = 0.160; *g = 0.360; *b = 0.620; break;
        case GVR_SB_TYPE_YUV4MPEG:  *r = 0.050; *g = 0.520; *b = 0.620; break;
        case GVR_SB_TYPE_V4L:       *r = 0.080; *g = 0.520; *b = 0.240; break;
        case GVR_SB_TYPE_VLOOPBACK: *r = 0.050; *g = 0.510; *b = 0.500; break;
        case GVR_SB_TYPE_COLOR:     *r = 0.840; *g = 0.500; *b = 0.080; break;
        case GVR_SB_TYPE_PICTURE:   *r = 0.720; *g = 0.260; *b = 0.420; break;
        case GVR_SB_TYPE_CALI:      *r = 0.470; *g = 0.520; *b = 0.120; break;
        case GVR_SB_TYPE_GENERATOR: *r = 0.570; *g = 0.180; *b = 0.760; break;
        case GVR_SB_TYPE_SPLITTER:  *r = 0.420; *g = 0.380; *b = 0.550; break;
        case GVR_SB_TYPE_SHM:       *r = 0.260; *g = 0.430; *b = 0.560; break;
        case GVR_SB_TYPE_AVFORMAT:  *r = 0.160; *g = 0.250; *b = 0.780; break;
        case GVR_SB_TYPE_NET:       *r = 0.760; *g = 0.260; *b = 0.100; break;
        case GVR_SB_TYPE_MCAST:     *r = 0.760; *g = 0.090; *b = 0.150; break;
        case GVR_SB_TYPE_CLONE:     *r = 0.320; *g = 0.350; *b = 0.430; break;
        case GVR_SB_TYPE_DV1394:    *r = 0.560; *g = 0.390; *b = 0.140; break;
        default:                    *r = 0.330; *g = 0.330; *b = 0.360; break;
    }
}

static const char *gvr_sample_bank_type_label(int sample_type)
{
    switch(sample_type) {
        case GVR_SB_TYPE_SAMPLE:    return "Sample";
        case GVR_SB_TYPE_YUV4MPEG:  return "YUV4MPEG stream";
        case GVR_SB_TYPE_V4L:       return "V4L stream";
        case GVR_SB_TYPE_VLOOPBACK: return "VLoopback stream";
        case GVR_SB_TYPE_COLOR:     return "Color source";
        case GVR_SB_TYPE_PICTURE:   return "Picture source";
        case GVR_SB_TYPE_CALI:      return "Calibration source";
        case GVR_SB_TYPE_GENERATOR: return "Generator source";
        case GVR_SB_TYPE_SPLITTER:  return "Splitter source";
        case GVR_SB_TYPE_SHM:       return "Shared-memory stream";
        case GVR_SB_TYPE_AVFORMAT:  return "AVFormat stream";
        case GVR_SB_TYPE_NET:       return "Network stream";
        case GVR_SB_TYPE_MCAST:     return "Multicast stream";
        case GVR_SB_TYPE_CLONE:     return "Clone source";
        case GVR_SB_TYPE_DV1394:    return "DV1394 stream";
        default:                    return "Source";
    }
}

static void gvr_sample_text_color(double r, double g, double b, double *tr, double *tg, double *tb)
{
    double y = (0.299 * r) + (0.587 * g) + (0.114 * b);

    if(y > 0.52) {
        *tr = 0.035; *tg = 0.040; *tb = 0.050;
    } else {
        *tr = 0.985; *tg = 0.990; *tb = 1.000;
    }
}

static double gvr_text_width(cairo_t *cr, const char *family, const char *text, double size, cairo_font_weight_t weight)
{
    cairo_text_extents_t ext;

    cairo_save(cr);
    cairo_select_font_face(cr, family ? family : "Sans", CAIRO_FONT_SLANT_NORMAL, weight);
    cairo_set_font_size(cr, size);
    cairo_text_extents(cr, text ? text : "", &ext);
    cairo_restore(cr);

    return ext.width;
}

static void gvr_draw_text_right(cairo_t *cr, const char *family, const char *text, double right_x, double y, double size, cairo_font_weight_t weight)
{
    cairo_text_extents_t ext;

    cairo_save(cr);
    cairo_select_font_face(cr, family ? family : "Sans", CAIRO_FONT_SLANT_NORMAL, weight);
    cairo_set_font_size(cr, size);
    cairo_text_extents(cr, text ? text : "", &ext);
    cairo_move_to(cr, right_x - ext.width, y);
    cairo_show_text(cr, text ? text : "");
    cairo_restore(cr);
}

static void gvr_draw_ellipsized_text(cairo_t *cr,
                                     const char *family,
                                     const char *text,
                                     double x,
                                     double y,
                                     double max_w,
                                     double size,
                                     cairo_font_weight_t weight)
{
    char buf[96];
    cairo_text_extents_t ext;
    size_t n;

    if(!text)
        text = "";

    g_strlcpy(buf, text, sizeof(buf));

    cairo_save(cr);
    cairo_select_font_face(cr, family ? family : "Sans", CAIRO_FONT_SLANT_NORMAL, weight);
    cairo_set_font_size(cr, size);

    cairo_text_extents(cr, buf, &ext);
    n = strlen(buf);
    while(n > 3 && ext.width > max_w) {
        buf[--n] = '\0';
        if(n > 3) {
            buf[n - 1] = '.';
            buf[n - 2] = '.';
            buf[n - 3] = '.';
        }
        cairo_text_extents(cr, buf, &ext);
    }

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, buf);
    cairo_restore(cr);
}


static int gvr_sample_bank_view_visible_page_step(GvrSampleBankView *view)
{
    int cells = view ? view->layout_cells : GVR_SAMPLE_BANK_SLOTS;
    int pages;

    if(cells < GVR_SAMPLE_BANK_SLOTS)
        cells = GVR_SAMPLE_BANK_SLOTS;

    pages = cells / GVR_SAMPLE_BANK_SLOTS;
    if(pages < 1)
        pages = 1;

    return pages;
}

static void gvr_sample_bank_view_layout(GvrSampleBankView *view, int width, int height)
{
    const int outer = 5;
    const int inner = 5;
    const int base_columns = gvr_clampi(view->columns, 1, GVR_SAMPLE_BANK_SLOTS);
    const int base_rows = gvr_clampi(view->rows, 1, GVR_SAMPLE_BANK_SLOTS);
    const int grid_w = width - outer * 2;
    const int grid_h = height - outer * 2;
    GvrSampleBankFontMetrics metrics;
    int columns = base_columns;
    int rows = base_rows;
    int target_cell;
    int cell;
    int total_w;
    int total_h;
    int start_x;
    int start_y;

    gvr_sample_bank_font_metrics(GTK_WIDGET(view), &metrics);
    target_cell = gvr_sample_bank_cell_side(&metrics);

    view->header_rect.x = 0;
    view->header_rect.y = 0;
    view->header_rect.width = 0;
    view->header_rect.height = 0;
    view->layout_cells = 0;
    view->layout_columns = base_columns;
    view->layout_rows = base_rows;

    for(int i = 0; i < GVR_SAMPLE_BANK_VISIBLE_MAX; i++) {
        view->cell_rect[i].x = 0;
        view->cell_rect[i].y = 0;
        view->cell_rect[i].width = 0;
        view->cell_rect[i].height = 0;
        view->cell_page[i] = -1;
        view->cell_slot[i] = -1;
    }

    if(grid_w <= 2 || grid_h <= 2)
        return;

    columns = grid_w / target_cell;
    rows = grid_h / target_cell;

    if(columns < base_columns)
        columns = base_columns;
    if(rows < base_rows)
        rows = base_rows;

    while(columns * rows > GVR_SAMPLE_BANK_VISIBLE_MAX && rows > base_rows)
        rows--;
    while(columns * rows > GVR_SAMPLE_BANK_VISIBLE_MAX && columns > base_columns)
        columns--;

    cell = (grid_w - inner * (columns - 1)) / columns;
    {
        const int available_cell_h =
            (grid_h - inner * (rows - 1)) / rows;
        if(available_cell_h < cell)
            cell = available_cell_h;
    }

    if(cell < 8)
        cell = 8;

    total_w = columns * cell + inner * (columns - 1);
    total_h = rows * cell + inner * (rows - 1);
    start_x = (width - total_w) / 2;
    start_y = (height - total_h) / 2;

    if(start_x < outer)
        start_x = outer;
    if(start_y < outer)
        start_y = outer;

    view->layout_columns = columns;
    view->layout_rows = rows;

    for(int idx = 0; idx < columns * rows && idx < GVR_SAMPLE_BANK_VISIBLE_MAX; idx++) {
        const int sx = idx % columns;
        const int sy = idx / columns;
        const int page = view->current_page + (idx / GVR_SAMPLE_BANK_SLOTS);
        const int slot = idx % GVR_SAMPLE_BANK_SLOTS;
        GdkRectangle *r = &view->cell_rect[idx];

        r->x = start_x + sx * (cell + inner);
        r->y = start_y + sy * (cell + inner);
        r->width = cell;
        r->height = cell;
        view->cell_page[idx] = page;
        view->cell_slot[idx] = slot;
        view->layout_cells++;
    }
}

static void gvr_sample_bank_view_recount_page(GvrSampleBankView *view, int page)
{
    int n = 0;

    if(!view || page < 0 || page >= GVR_SAMPLE_BANK_MAX_PAGES)
        return;

    for(int slot = 0; slot < GVR_SAMPLE_BANK_SLOTS; slot++)
        if(view->pages[page].cells[slot].sample_id > 0)
            n++;

    view->pages[page].size = n;
}


static gboolean gvr_sample_bank_view_hit(GvrSampleBankView *view, double x, double y, int *page, int *slot, gboolean *header)
{
    for(int i = 0; i < view->layout_cells; i++) {
        if(gvr_rect_contains(&view->cell_rect[i], x, y)) {
            *page = view->cell_page[i];
            *slot = view->cell_slot[i];
            *header = FALSE;
            return TRUE;
        }
    }

    *page = -1;
    *slot = -1;
    *header = FALSE;
    return FALSE;
}

static void gvr_draw_thumbnail(cairo_t *cr, GdkPixbuf *pixbuf, const GdkRectangle *area)
{
    int pw, ph;
    double sx, sy, scale, w, h, x, y;

    if(!pixbuf || area->width <= 2 || area->height <= 2)
        return;

    pw = gdk_pixbuf_get_width(pixbuf);
    ph = gdk_pixbuf_get_height(pixbuf);
    if(pw <= 0 || ph <= 0)
        return;

    sx = (double)area->width / (double)pw;
    sy = (double)area->height / (double)ph;
    scale = sx < sy ? sx : sy;
    w = (double)pw * scale;
    h = (double)ph * scale;
    x = area->x + ((double)area->width - w) * 0.5;
    y = area->y + ((double)area->height - h) * 0.5;

    cairo_save(cr);
    cairo_rectangle(cr, area->x, area->y, area->width, area->height);
    cairo_clip(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, scale, scale);
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

static gboolean gvr_sample_bank_view_draw(GtkWidget *widget, cairo_t *cr)
{
    GvrSampleBankView *view = GVR_SAMPLE_BANK_VIEW(widget);
    GtkAllocation allocation;
    int page;
    GvrSampleBankFontMetrics font_metrics;
    const char *font_family;
    double title_font;
    double meta_font;
    int preferred_footer_h;

    if(!view->pages)
        return FALSE;

    gtk_widget_get_allocation(widget, &allocation);
    gvr_sample_bank_font_metrics(widget, &font_metrics);
    font_family = font_metrics.family;
    title_font = gvr_sample_bank_font_px(&font_metrics, 0.82);
    meta_font = gvr_sample_bank_font_px(&font_metrics, 0.76);
    preferred_footer_h = gvr_sample_bank_footer_height(&font_metrics);
    gvr_sample_bank_view_layout(view, allocation.width, allocation.height);

    gvr_set_rgba(cr, 0.070, 0.075, 0.085, 1.0);
    cairo_paint(cr);

    page = gvr_clampi(view->current_page, 0, view->page_count - 1);
    if(page != view->current_page)
        view->current_page = page;

    for(int idx = 0; idx < view->layout_cells; idx++) {
        GdkRectangle *r = &view->cell_rect[idx];
        int cell_page = view->cell_page[idx];
        int slot = view->cell_slot[idx];
        GvrSampleBankCell empty_cell;
        GvrSampleBankCell *cell = &empty_cell;
        gboolean filled;
        gboolean selected;
        gboolean current;
        gboolean hover;
        double rr, gg, bb, tr, tg, tb;

        memset(&empty_cell, 0, sizeof(empty_cell));

        if(cell_page >= 0 && cell_page < view->page_count &&
           slot >= 0 && slot < GVR_SAMPLE_BANK_SLOTS)
            cell = &view->pages[cell_page].cells[slot];

        filled = cell->sample_id > 0;
        selected = (cell_page == view->selected_page && slot == view->selected_slot);
        current = (filled && cell->sample_id == view->current_sample_id && cell->sample_type == view->current_sample_type);
        hover = (cell_page == view->hover_page && slot == view->hover_slot);

        if(r->width <= 0 || r->height <= 0)
            continue;

        if(filled) {
            GdkRectangle img_area;
            int footer_h = r->height > 72 ?
                preferred_footer_h :
                MAX(26, (int)ceil(title_font + 14.0));

            footer_h = MIN(footer_h, MAX(20, r->height - 12));

            gvr_sample_slot_color(cell->sample_type, &rr, &gg, &bb);
            gvr_sample_text_color(rr, gg, bb, &tr, &tg, &tb);

            gvr_set_rgba(cr, 0.050, 0.058, 0.068, 1.0);
            cairo_rectangle(cr, r->x, r->y, r->width, r->height);
            cairo_fill(cr);

            img_area.x = r->x + 3;
            img_area.y = r->y + 3;
            img_area.width = r->width - 6;
            img_area.height = r->height - footer_h - 6;

            gvr_set_rgba(cr, 0.020, 0.024, 0.030, 1.0);
            cairo_rectangle(cr, img_area.x, img_area.y, img_area.width, img_area.height);
            cairo_fill(cr);
            gvr_draw_thumbnail(cr, cell->thumb, &img_area);

            gvr_set_rgba(cr, rr, gg, bb, 0.92);
            cairo_rectangle(cr, r->x + 1, r->y + r->height - footer_h, r->width - 2, footer_h - 1);
            cairo_fill(cr);

            gvr_set_rgba(cr, tr, tg, tb, 1.0);
            if(r->height >= 64) {
                char fallback_title[32];
                char sample_label[32];
                const char *title = (cell->title && cell->title[0]) ? cell->title : NULL;
                const char *timecode = (cell->timecode && cell->timecode[0]) ? cell->timecode : NULL;
                double footer_top = r->y + r->height - footer_h;
                double y1 = footer_top + title_font + 4.0;
                double y2 = r->y + r->height - 6.0;
                double right_w;
                double left_w;

                snprintf(fallback_title, sizeof(fallback_title), "Sample %d", cell->sample_id);
                snprintf(sample_label, sizeof(sample_label),
                         "%c%d",
                         cell->sample_type == GVR_SB_TYPE_SAMPLE ? 'S' : 'T',
                         cell->sample_id);
                if(!title)
                    title = fallback_title;

                right_w = gvr_text_width(cr, font_family, sample_label, meta_font, CAIRO_FONT_WEIGHT_NORMAL) + 8.0;
                left_w = r->width - 16.0 - right_w;
                if(left_w < 20.0)
                    left_w = r->width - 16.0;

                gvr_draw_ellipsized_text(cr,
                                         font_family,
                                         title,
                                         r->x + 8,
                                         y1,
                                         r->width - 16,
                                         title_font,
                                         CAIRO_FONT_WEIGHT_BOLD);
                if(timecode)
                    gvr_draw_ellipsized_text(cr,
                                             "Monospace",
                                             timecode,
                                             r->x + 8,
                                             y2,
                                             left_w,
                                             meta_font,
                                             CAIRO_FONT_WEIGHT_NORMAL);
                gvr_draw_text_right(cr,
                                    font_family,
                                    sample_label,
                                    r->x + r->width - 8,
                                    y2,
                                    meta_font,
                                    CAIRO_FONT_WEIGHT_NORMAL);
            }
            else {
                char fallback_title[32];
                const char *title = (cell->title && cell->title[0]) ? cell->title : NULL;

                snprintf(fallback_title, sizeof(fallback_title), "Sample %d", cell->sample_id);
                if(!title)
                    title = fallback_title;

                gvr_draw_ellipsized_text(cr,
                                         font_family,
                                         title,
                                         r->x + 8,
                                         r->y + r->height - footer_h +
                                             (footer_h + title_font * 0.65) * 0.5,
                                         r->width - 16,
                                         title_font,
                                         CAIRO_FONT_WEIGHT_BOLD);
            }
        }
        else {
            gvr_set_rgba(cr, 0.055, 0.060, 0.068, 1.0);
            cairo_rectangle(cr, r->x, r->y, r->width, r->height);
            cairo_fill(cr);
        }

        gvr_set_rgba(cr, 0.130, 0.145, 0.170, 1.0);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, r->x + 0.5, r->y + 0.5, r->width - 1.0, r->height - 1.0);
        cairo_stroke(cr);

        if(selected && !current) {
            gvr_set_rgba(cr, 0.950, 0.970, 1.000, 0.15);
            cairo_rectangle(cr, r->x + 1, r->y + 1, r->width - 2, r->height - 2);
            cairo_fill(cr);
            gvr_set_rgba(cr, 0.950, 0.970, 1.000, 1.0);
            cairo_set_line_width(cr, 2.0);
            cairo_rectangle(cr, r->x + 1.0, r->y + 1.0, r->width - 2.0, r->height - 2.0);
            cairo_stroke(cr);
        }

        if(current) {
            gvr_set_rgba(cr, 1.000, 0.520, 0.000, 1.0);
            cairo_set_line_width(cr, 2.6);
            cairo_rectangle(cr, r->x + 1.5, r->y + 1.5, r->width - 3.0, r->height - 3.0);
            cairo_stroke(cr);
        }
        else if(hover) {
            gvr_set_rgba(cr, 0.450, 0.500, 0.600, 0.9);
            cairo_set_line_width(cr, 1.4);
            cairo_rectangle(cr, r->x + 1.0, r->y + 1.0, r->width - 2.0, r->height - 2.0);
            cairo_stroke(cr);
        }
    }

    return FALSE;
}

static gboolean gvr_sample_bank_view_cell_valid(GvrSampleBankView *view, int page, int slot);

static void gvr_sample_bank_view_emit_page(GvrSampleBankView *view)
{
    g_signal_emit(view,
                  gvr_sample_bank_view_signals[SIGNAL_PAGE_SELECTED],
                  0,
                  view->current_page);
}


static gboolean gvr_sample_bank_view_scroll(GtkWidget *widget, GdkEventScroll *event)
{
    GvrSampleBankView *view = GVR_SAMPLE_BANK_VIEW(widget);
    int old_page = view->current_page;
    int delta = 0;
    int step;

    if(event->direction == GDK_SCROLL_UP)
        delta = -1;
    else if(event->direction == GDK_SCROLL_DOWN)
        delta = 1;
#if GTK_CHECK_VERSION(3,4,0)
    else if(event->direction == GDK_SCROLL_SMOOTH)
        delta = event->delta_y > 0.0 ? 1 : (event->delta_y < 0.0 ? -1 : 0);
#endif

    if(delta == 0)
        return FALSE;

    step = gvr_sample_bank_view_visible_page_step(view);
    view->current_page = gvr_clampi(view->current_page + delta * step, 0, view->page_count - 1);
    if(view->current_page != old_page) {
        gtk_widget_queue_draw(widget);
        gvr_sample_bank_view_emit_page(view);
    }

    return TRUE;
}

static gboolean gvr_sample_bank_view_button_press(GtkWidget *widget, GdkEventButton *event)
{
    GvrSampleBankView *view = GVR_SAMPLE_BANK_VIEW(widget);
    GtkAllocation allocation;
    int page = -1;
    int slot = -1;
    gboolean header = FALSE;

    gtk_widget_grab_focus(widget);
    gtk_widget_get_allocation(widget, &allocation);
    gvr_sample_bank_view_layout(view, allocation.width, allocation.height);

    if(!gvr_sample_bank_view_hit(view, event->x, event->y, &page, &slot, &header))
        return FALSE;

    if(header && event->button == 1) {
        view->selected_page = view->current_page;
        view->selected_slot = -1;
        gtk_widget_queue_draw(widget);
        gvr_sample_bank_view_emit_page(view);
        return TRUE;
    }

    if(slot < 0 || slot >= GVR_SAMPLE_BANK_SLOTS)
        return FALSE;

    if(!gvr_sample_bank_view_cell_valid(view, page, slot))
        return FALSE;

    if(event->button == 1) {
        view->selected_page = page;
        view->selected_slot = slot;
        gtk_widget_queue_draw(widget);

        if((event->state & GDK_SHIFT_MASK) != 0) {
            g_signal_emit(view,
                          gvr_sample_bank_view_signals[SIGNAL_SLOT_MIX_REQUESTED],
                          0,
                          page,
                          slot);
            return TRUE;
        }

        if(event->type == GDK_2BUTTON_PRESS) {
            g_signal_emit(view,
                          gvr_sample_bank_view_signals[SIGNAL_SLOT_ACTIVATED],
                          0,
                          page,
                          slot);
            return TRUE;
        }

        g_signal_emit(view,
                      gvr_sample_bank_view_signals[SIGNAL_SLOT_SELECTED],
                      0,
                      page,
                      slot);
        return TRUE;
    }

    return FALSE;
}


static gboolean gvr_sample_bank_view_motion(GtkWidget *widget, GdkEventMotion *event)
{
    GvrSampleBankView *view = GVR_SAMPLE_BANK_VIEW(widget);
    int page = -1;
    int slot = -1;
    gboolean header = FALSE;

    if(gvr_sample_bank_view_hit(view, event->x, event->y, &page, &slot, &header) && !header) {
        if(page != view->hover_page || slot != view->hover_slot) {
            view->hover_page = page;
            view->hover_slot = slot;
            gtk_widget_queue_draw(widget);
        }
    }
    else if(view->hover_slot >= 0 || view->hover_page >= 0) {
        view->hover_page = -1;
        view->hover_slot = -1;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}


static gboolean gvr_sample_bank_view_leave(GtkWidget *widget, GdkEventCrossing *event)
{
    GvrSampleBankView *view = GVR_SAMPLE_BANK_VIEW(widget);
    (void)event;

    if(view->hover_slot >= 0 || view->hover_page >= 0) {
        view->hover_page = -1;
        view->hover_slot = -1;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

static gboolean gvr_sample_bank_view_cell_valid(GvrSampleBankView *view, int page, int slot)
{
    return view &&
           page >= 0 && page < view->page_count &&
           slot >= 0 && slot < GVR_SAMPLE_BANK_SLOTS;
}


static int gvr_sample_bank_view_keyboard_slot(GvrSampleBankView *view)
{
    if(view->selected_page == view->current_page &&
       view->selected_slot >= 0 && view->selected_slot < GVR_SAMPLE_BANK_SLOTS)
        return view->selected_slot;

    if(view->hover_page == view->current_page &&
       view->hover_slot >= 0 && view->hover_slot < GVR_SAMPLE_BANK_SLOTS)
        return view->hover_slot;

    return 0;
}

static void gvr_sample_bank_view_select_keyboard_slot(GtkWidget *widget, int page, int slot)
{
    GvrSampleBankView *view = GVR_SAMPLE_BANK_VIEW(widget);
    int old_page = view->current_page;

    if(!gvr_sample_bank_view_cell_valid(view, page, slot))
        return;

    view->current_page = page;
    view->selected_page = page;
    view->selected_slot = slot;
    gtk_widget_queue_draw(widget);

    if(view->current_page != old_page)
        gvr_sample_bank_view_emit_page(view);

    g_signal_emit(view,
                  gvr_sample_bank_view_signals[SIGNAL_SLOT_SELECTED],
                  0,
                  page,
                  slot);
}

static gboolean gvr_sample_bank_view_query_tooltip(GtkWidget *widget,
                                                   gint x,
                                                   gint y,
                                                   gboolean keyboard_mode,
                                                   GtkTooltip *tooltip)
{
    GvrSampleBankView *view = GVR_SAMPLE_BANK_VIEW(widget);
    int page = -1;
    int slot = -1;
    gboolean header = FALSE;
    GvrSampleBankCell *cell;
    GtkAllocation allocation;
    char text[768];

    gtk_widget_get_allocation(widget, &allocation);
    gvr_sample_bank_view_layout(view, allocation.width, allocation.height);

    if(keyboard_mode) {
        page = view->current_page;
        slot = gvr_sample_bank_view_keyboard_slot(view);
    }
    else if(!gvr_sample_bank_view_hit(view, x, y, &page, &slot, &header) || header) {
        return FALSE;
    }

    if(!gvr_sample_bank_view_cell_valid(view, page, slot) || !view->pages)
        return FALSE;

    cell = &view->pages[page].cells[slot];

    if(cell->sample_id <= 0) {
        snprintf(text, sizeof(text),
                 "Empty sample-bank slot\n"
                 "Bank %d/%d · slot %d\n\n"
                 "Cursor keys: move selection\n"
                 "Page Up / Page Down: change bank\n"
                 "Mouse wheel: change bank",
                 page + 1,
                 view->page_count,
                 slot + 1);
        gtk_tooltip_set_text(tooltip, text);
        return TRUE;
    }

    snprintf(text, sizeof(text),
             "%s %d\n"
             "Title: %s\n"
             "%s%s%s"
             "Bank %d/%d · slot %d\n\n"
             "Click / cursor keys: select\n"
             "Double-click / Enter: play\n"
             "Shift-click / Shift-Enter: use as mixer source\n"
             "Page Up / Page Down or mouse wheel: change bank",
             gvr_sample_bank_type_label(cell->sample_type),
             cell->sample_id,
             (cell->title && cell->title[0]) ? cell->title : "untitled",
             (cell->timecode && cell->timecode[0]) ? "Length/type: " : "",
             (cell->timecode && cell->timecode[0]) ? cell->timecode : "",
             (cell->timecode && cell->timecode[0]) ? "\n" : "",
             page + 1,
             view->page_count,
             slot + 1);

    gtk_tooltip_set_text(tooltip, text);
    return TRUE;
}

static gboolean gvr_sample_bank_view_key_press(GtkWidget *widget, GdkEventKey *event)
{
    GvrSampleBankView *view = GVR_SAMPLE_BANK_VIEW(widget);
    int old_page = view->current_page;
    int page = view->current_page;
    int slot = gvr_sample_bank_view_keyboard_slot(view);
    int new_slot = slot;
    int columns = gvr_clampi(view->columns, 1, GVR_SAMPLE_BANK_SLOTS);

    switch(event->keyval) {
        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            view->current_page = gvr_clampi(view->current_page - 1, 0, view->page_count - 1);
            break;
        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            view->current_page = gvr_clampi(view->current_page + 1, 0, view->page_count - 1);
            break;
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            new_slot = gvr_clampi(slot - 1, 0, GVR_SAMPLE_BANK_SLOTS - 1);
            gvr_sample_bank_view_select_keyboard_slot(widget, page, new_slot);
            return TRUE;
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            new_slot = gvr_clampi(slot + 1, 0, GVR_SAMPLE_BANK_SLOTS - 1);
            gvr_sample_bank_view_select_keyboard_slot(widget, page, new_slot);
            return TRUE;
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            new_slot = gvr_clampi(slot - columns, 0, GVR_SAMPLE_BANK_SLOTS - 1);
            gvr_sample_bank_view_select_keyboard_slot(widget, page, new_slot);
            return TRUE;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            new_slot = gvr_clampi(slot + columns, 0, GVR_SAMPLE_BANK_SLOTS - 1);
            gvr_sample_bank_view_select_keyboard_slot(widget, page, new_slot);
            return TRUE;
        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            gvr_sample_bank_view_select_keyboard_slot(widget, page, 0);
            return TRUE;
        case GDK_KEY_End:
        case GDK_KEY_KP_End:
            gvr_sample_bank_view_select_keyboard_slot(widget, page, GVR_SAMPLE_BANK_SLOTS - 1);
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if(view->selected_page == view->current_page &&
               gvr_sample_bank_view_cell_valid(view, view->selected_page, view->selected_slot)) {
                g_signal_emit(view,
                              gvr_sample_bank_view_signals[(event->state & GDK_SHIFT_MASK) ? SIGNAL_SLOT_MIX_REQUESTED : SIGNAL_SLOT_ACTIVATED],
                              0,
                              view->selected_page,
                              view->selected_slot);
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }

    if(view->current_page != old_page) {
        gtk_widget_queue_draw(widget);
        gvr_sample_bank_view_emit_page(view);
    }

    return TRUE;
}

static void gvr_sample_bank_view_finalize(GObject *object)
{
    GvrSampleBankView *view = GVR_SAMPLE_BANK_VIEW(object);

    if(view->pages) {
        for(int page = 0; page < GVR_SAMPLE_BANK_MAX_PAGES; page++) {
            for(int slot = 0; slot < GVR_SAMPLE_BANK_SLOTS; slot++) {
                GvrSampleBankCell *cell = &view->pages[page].cells[slot];
                g_clear_pointer(&cell->title, g_free);
                g_clear_pointer(&cell->timecode, g_free);
                if(cell->thumb) {
                    g_object_unref(cell->thumb);
                    cell->thumb = NULL;
                }
            }
        }
        g_free(view->pages);
        view->pages = NULL;
    }

    G_OBJECT_CLASS(gvr_sample_bank_view_parent_class)->finalize(object);
}


static void gvr_sample_bank_view_style_updated(GtkWidget *widget)
{
    GTK_WIDGET_CLASS(gvr_sample_bank_view_parent_class)->style_updated(widget);
    gvr_sample_bank_view_update_size_request(widget);
    gtk_widget_queue_resize(widget);
    gtk_widget_queue_draw(widget);
}

static void gvr_sample_bank_view_class_init(GvrSampleBankViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->finalize = gvr_sample_bank_view_finalize;
    widget_class->style_updated = gvr_sample_bank_view_style_updated;
    widget_class->draw = gvr_sample_bank_view_draw;
    widget_class->button_press_event = gvr_sample_bank_view_button_press;
    widget_class->scroll_event = gvr_sample_bank_view_scroll;
    widget_class->motion_notify_event = gvr_sample_bank_view_motion;
    widget_class->leave_notify_event = gvr_sample_bank_view_leave;
    widget_class->key_press_event = gvr_sample_bank_view_key_press;
    widget_class->query_tooltip = gvr_sample_bank_view_query_tooltip;

    gvr_sample_bank_view_signals[SIGNAL_PAGE_SELECTED] =
        g_signal_new("page-selected",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    gvr_sample_bank_view_signals[SIGNAL_SLOT_SELECTED] =
        g_signal_new("slot-selected",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_INT,
                     G_TYPE_INT);

    gvr_sample_bank_view_signals[SIGNAL_SLOT_ACTIVATED] =
        g_signal_new("slot-activated",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_INT,
                     G_TYPE_INT);

    gvr_sample_bank_view_signals[SIGNAL_SLOT_MIX_REQUESTED] =
        g_signal_new("slot-mix-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_INT,
                     G_TYPE_INT);
}

static void gvr_sample_bank_view_init(GvrSampleBankView *view)
{
    view->pages = g_new0(GvrSampleBankPage, GVR_SAMPLE_BANK_MAX_PAGES);
    view->columns = GVR_SAMPLE_BANK_COLUMNS;
    view->rows = GVR_SAMPLE_BANK_ROWS;
    view->page_count = 1;
    view->current_page = 0;
    view->selected_page = -1;
    view->selected_slot = -1;
    view->hover_page = -1;
    view->hover_slot = -1;
    view->current_sample_id = -1;
    view->current_sample_type = -1;
    view->layout_cells = 0;
    view->layout_columns = view->columns;
    view->layout_rows = view->rows;

    gtk_widget_set_can_focus(GTK_WIDGET(view), TRUE);
    gtk_widget_set_has_tooltip(GTK_WIDGET(view), TRUE);
    gtk_widget_add_events(GTK_WIDGET(view),
                          GDK_BUTTON_PRESS_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_LEAVE_NOTIFY_MASK |
                          GDK_SCROLL_MASK |
                          GDK_SMOOTH_SCROLL_MASK);
    gvr_sample_bank_view_update_size_request(GTK_WIDGET(view));
}

GtkWidget *gvr_sample_bank_view_new(void)
{
    return g_object_new(GVR_TYPE_SAMPLE_BANK_VIEW, NULL);
}

void gvr_sample_bank_view_set_layout(GtkWidget *widget, int columns, int rows)
{
    GvrSampleBankView *view;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    if(columns < 1 || rows < 1 || columns * rows != GVR_SAMPLE_BANK_SLOTS) {
        columns = GVR_SAMPLE_BANK_COLUMNS;
        rows = GVR_SAMPLE_BANK_ROWS;
    }

    view = GVR_SAMPLE_BANK_VIEW(widget);
    if(view->columns == columns && view->rows == rows)
        return;

    view->columns = columns;
    view->rows = rows;
    gvr_sample_bank_view_update_size_request(widget);
    gtk_widget_queue_resize(widget);
    gtk_widget_queue_draw(widget);
}

void gvr_sample_bank_view_set_page_count(GtkWidget *widget, int pages)
{
    GvrSampleBankView *view;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    pages = gvr_clampi(pages, 1, GVR_SAMPLE_BANK_MAX_PAGES);
    if(view->page_count == pages)
        return;

    view->page_count = pages;
    view->current_page = gvr_clampi(view->current_page, 0, view->page_count - 1);
    gtk_widget_queue_draw(widget);
}

int gvr_sample_bank_view_get_page_count(GtkWidget *widget)
{
    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return 0;
    return GVR_SAMPLE_BANK_VIEW(widget)->page_count;
}

void gvr_sample_bank_view_set_current_page(GtkWidget *widget, int page)
{
    GvrSampleBankView *view;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    page = gvr_clampi(page, 0, view->page_count - 1);
    if(view->current_page == page)
        return;

    view->current_page = page;
    gtk_widget_queue_draw(widget);
}

int gvr_sample_bank_view_get_current_page(GtkWidget *widget)
{
    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return 0;
    return GVR_SAMPLE_BANK_VIEW(widget)->current_page;
}

static int gvr_sample_bank_view_reveal_page(GvrSampleBankView *view, int page)
{
    GtkAllocation allocation;
    int step;
    int first;
    int last;
    int max_first;

    if(!view)
        return 0;

    page = gvr_clampi(page, 0, view->page_count - 1);
    gtk_widget_get_allocation(GTK_WIDGET(view), &allocation);
    gvr_sample_bank_view_layout(view, allocation.width, allocation.height);

    step = gvr_sample_bank_view_visible_page_step(view);
    first = view->current_page;
    last = MIN(view->page_count - 1, first + step - 1);
    if(page >= first && page <= last)
        return first;

    max_first = MAX(0, view->page_count - step);
    first = (page / step) * step;
    if(first > max_first)
        first = max_first;

    return gvr_clampi(first, 0, view->page_count - 1);
}

void gvr_sample_bank_view_reveal_slot(GtkWidget *widget, int page, int slot)
{
    GvrSampleBankView *view;
    int old_page;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    if(!gvr_sample_bank_view_cell_valid(view, page, slot))
        return;

    old_page = view->current_page;
    view->current_page = gvr_sample_bank_view_reveal_page(view, page);
    gtk_widget_queue_draw(widget);

    if(view->current_page != old_page)
        gvr_sample_bank_view_emit_page(view);
}


void gvr_sample_bank_view_step_page(GtkWidget *widget, int delta)
{
    GvrSampleBankView *view;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    gvr_sample_bank_view_set_current_page(widget,
        view->current_page + delta * gvr_sample_bank_view_visible_page_step(view));
}

void gvr_sample_bank_view_set_slot(GtkWidget *widget,
                                   int page,
                                   int slot,
                                   int sample_id,
                                   int sample_type,
                                   const char *title,
                                   const char *timecode)
{
    GvrSampleBankView *view;
    GvrSampleBankCell *cell;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    if(!GVR_SAMPLE_BANK_VIEW(widget)->pages)
        return;

    if(page < 0 || page >= GVR_SAMPLE_BANK_MAX_PAGES || slot < 0 || slot >= GVR_SAMPLE_BANK_SLOTS)
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    cell = &view->pages[page].cells[slot];

    if((cell->sample_id != sample_id || cell->sample_type != sample_type) && cell->thumb) {
        g_object_unref(cell->thumb);
        cell->thumb = NULL;
    }

    cell->sample_id = sample_id;
    cell->sample_type = sample_type;

    g_free(cell->title);
    cell->title = title ? g_strdup(title) : NULL;
    g_free(cell->timecode);
    cell->timecode = timecode ? g_strdup(timecode) : NULL;

    if(sample_id <= 0 && cell->thumb) {
        g_object_unref(cell->thumb);
        cell->thumb = NULL;
    }

    gvr_sample_bank_view_recount_page(view, page);
    if(page == view->current_page)
        gtk_widget_queue_draw(widget);
}

void gvr_sample_bank_view_clear_slot(GtkWidget *widget, int page, int slot)
{
    GvrSampleBankView *view;
    GvrSampleBankCell *cell;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    if(!GVR_SAMPLE_BANK_VIEW(widget)->pages)
        return;

    if(page < 0 || page >= GVR_SAMPLE_BANK_MAX_PAGES || slot < 0 || slot >= GVR_SAMPLE_BANK_SLOTS)
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    cell = &view->pages[page].cells[slot];
    cell->sample_id = -1;
    cell->sample_type = -1;
    g_clear_pointer(&cell->title, g_free);
    g_clear_pointer(&cell->timecode, g_free);
    if(cell->thumb) {
        g_object_unref(cell->thumb);
        cell->thumb = NULL;
    }

    gvr_sample_bank_view_recount_page(view, page);
    if(page == view->current_page)
        gtk_widget_queue_draw(widget);
}

void gvr_sample_bank_view_clear_all(GtkWidget *widget)
{
    GvrSampleBankView *view;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    if(!view->pages)
        return;

    for(int page = 0; page < GVR_SAMPLE_BANK_MAX_PAGES; page++) {
        for(int slot = 0; slot < GVR_SAMPLE_BANK_SLOTS; slot++) {
            GvrSampleBankCell *cell = &view->pages[page].cells[slot];
            cell->sample_id = -1;
            cell->sample_type = -1;
            g_clear_pointer(&cell->title, g_free);
            g_clear_pointer(&cell->timecode, g_free);
            if(cell->thumb) {
                g_object_unref(cell->thumb);
                cell->thumb = NULL;
            }
        }
        view->pages[page].size = 0;
    }

    view->selected_page = -1;
    view->selected_slot = -1;
    view->hover_page = -1;
    view->hover_slot = -1;
    view->current_sample_id = -1;
    view->current_sample_type = -1;
    gtk_widget_queue_draw(widget);
}

void gvr_sample_bank_view_set_thumbnail(GtkWidget *widget, int page, int slot, GdkPixbuf *pixbuf)
{
    GvrSampleBankView *view;
    GvrSampleBankCell *cell;
    GdkPixbuf *copy;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget) || !pixbuf)
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    if(!view->pages)
        return;

    if(page < 0 || page >= GVR_SAMPLE_BANK_MAX_PAGES || slot < 0 || slot >= GVR_SAMPLE_BANK_SLOTS)
        return;

    cell = &view->pages[page].cells[slot];
    copy = gdk_pixbuf_copy(pixbuf);
    if(!copy)
        return;

    if(cell->thumb)
        g_object_unref(cell->thumb);
    cell->thumb = copy;

    if(page == view->current_page)
        gtk_widget_queue_draw(widget);
}

void gvr_sample_bank_view_set_selected_slot(GtkWidget *widget, int page, int slot)
{
    GvrSampleBankView *view;
    int old_page;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    old_page = view->current_page;

    view->selected_page = page;
    view->selected_slot = slot;
    if(gvr_sample_bank_view_cell_valid(view, page, slot))
        view->current_page = gvr_sample_bank_view_reveal_page(view, page);

    gtk_widget_queue_draw(widget);

    if(view->current_page != old_page)
        gvr_sample_bank_view_emit_page(view);
}

void gvr_sample_bank_view_set_current_source(GtkWidget *widget, int sample_id, int sample_type)
{
    GvrSampleBankView *view;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    if(view->current_sample_id == sample_id && view->current_sample_type == sample_type)
        return;

    view->current_sample_id = sample_id;
    view->current_sample_type = sample_type;
    gtk_widget_queue_draw(widget);
}

gboolean gvr_sample_bank_view_get_slot(GtkWidget *widget,
                                        int page,
                                        int slot,
                                        int *sample_id,
                                        int *sample_type)
{
    GvrSampleBankView *view;
    GvrSampleBankCell *cell;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return FALSE;

    if(!GVR_SAMPLE_BANK_VIEW(widget)->pages)
        return FALSE;

    if(page < 0 || page >= GVR_SAMPLE_BANK_MAX_PAGES || slot < 0 || slot >= GVR_SAMPLE_BANK_SLOTS)
        return FALSE;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    cell = &view->pages[page].cells[slot];
    if(sample_id)
        *sample_id = cell->sample_id;
    if(sample_type)
        *sample_type = cell->sample_type;

    return cell->sample_id > 0;
}

gboolean gvr_sample_bank_view_get_cell_at(GtkWidget *widget,
                                           int x,
                                           int y,
                                           int *page,
                                           int *slot,
                                           gboolean *header)
{
    GvrSampleBankView *view;
    GtkAllocation allocation;

    if(!GVR_IS_SAMPLE_BANK_VIEW(widget))
        return FALSE;

    view = GVR_SAMPLE_BANK_VIEW(widget);
    gtk_widget_get_allocation(widget, &allocation);
    gvr_sample_bank_view_layout(view, allocation.width, allocation.height);

    return gvr_sample_bank_view_hit(view, x, y, page, slot, header);
}
