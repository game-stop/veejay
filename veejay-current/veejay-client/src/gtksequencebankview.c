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
#include "gtksequencebankview.h"

#ifndef GVR_SEQUENCE_BANKS
#define GVR_SEQUENCE_BANKS 4
#endif
#ifndef GVR_SEQUENCE_SLOTS
#define GVR_SEQUENCE_SLOTS 100
#endif
#ifndef GVR_SEQUENCE_COLUMNS
#define GVR_SEQUENCE_COLUMNS 10
#endif
#ifndef GVR_SEQUENCE_ROWS
#define GVR_SEQUENCE_ROWS 10
#endif

#define GVR_SEQ_TYPE_SAMPLE      0
#define GVR_SEQ_TYPE_YUV4MPEG    1
#define GVR_SEQ_TYPE_V4L         2
#define GVR_SEQ_TYPE_VLOOPBACK   3
#define GVR_SEQ_TYPE_COLOR       4
#define GVR_SEQ_TYPE_PICTURE     5
#define GVR_SEQ_TYPE_CALI        6
#define GVR_SEQ_TYPE_GENERATOR   7
#define GVR_SEQ_TYPE_SPLITTER    8
#define GVR_SEQ_TYPE_SHM        11
#define GVR_SEQ_TYPE_AVFORMAT   12
#define GVR_SEQ_TYPE_NET        13
#define GVR_SEQ_TYPE_MCAST      14
#define GVR_SEQ_TYPE_CLONE      15
#define GVR_SEQ_TYPE_DV1394     17

typedef struct {
    int sample_id;
    int sample_type;
} GvrSequenceCell;

typedef struct {
    GvrSequenceCell cells[GVR_SEQUENCE_SLOTS];
    int current;
    int size;
    unsigned int revision;
} GvrSequenceBank;

struct _GvrSequenceBankView {
    GtkDrawingArea parent_instance;
    GvrSequenceBank banks[GVR_SEQUENCE_BANKS];
    int active_bank;
    int queued_bank;
    gboolean queue_mode;
    int selected_bank;
    int selected_slot;
    int hover_bank;
    int hover_slot;
    gboolean drag_active;
    gboolean drag_grabbed;
    int drag_bank;
    int drag_from_slot;
    int drag_to_slot;
    gboolean mouse_select_active;
    gboolean mouse_select_started;
    gboolean mouse_select_toggle;
    gboolean mouse_select_grabbed;
    int mouse_select_bank;
    int mouse_select_press_slot;
    int mouse_select_last_slot;
    double mouse_select_start_x;
    double mouse_select_start_y;
    gboolean mouse_select_seen[GVR_SEQUENCE_SLOTS];
    gboolean range_active;
    int selection_anchor_slot;
    gboolean selected_cells[GVR_SEQUENCE_SLOTS];
    int selected_count;
    gboolean copy_valid;
    GvrSequenceCell copy_cells[GVR_SEQUENCE_SLOTS];
    int copy_offsets[GVR_SEQUENCE_SLOTS];
    int copy_count;
    int copy_base_slot;
    gboolean copy_is_bank;
    int copy_bank;
    gboolean bank_copy_valid;
    int bank_copy_source;
    gboolean sequence_active;
    GdkRectangle bank_rect[GVR_SEQUENCE_BANKS];
    GdkRectangle header_rect[GVR_SEQUENCE_BANKS];
    GdkRectangle cell_rect[GVR_SEQUENCE_BANKS][GVR_SEQUENCE_SLOTS];
};

struct _GvrSequenceBankViewClass {
    GtkDrawingAreaClass parent_class;
};

enum {
    SIGNAL_BANK_SELECTED,
    SIGNAL_BANK_QUEUE_REQUESTED,
    SIGNAL_SLOT_ASSIGN_REQUESTED,
    SIGNAL_SLOT_DELETE_REQUESTED,
    SIGNAL_SLOT_REORDER_REQUESTED,
    SIGNAL_SLOT_PASTE_REQUESTED,
    SIGNAL_BANK_PASTE_REQUESTED,
    SIGNAL_BANK_CLEAR_REQUESTED,
    SIGNAL_REFRESH_REQUESTED,
    SIGNAL_LAST
};

static guint gvr_sequence_bank_view_signals[SIGNAL_LAST];

G_DEFINE_TYPE(GvrSequenceBankView, gvr_sequence_bank_view, GTK_TYPE_DRAWING_AREA)

static int gvr_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void gvr_rect_clear(GdkRectangle *r)
{
    r->x = r->y = r->width = r->height = 0;
}

static gboolean gvr_rect_contains(const GdkRectangle *r, double x, double y)
{
    return x >= r->x && y >= r->y && x < (r->x + r->width) && y < (r->y + r->height);
}

static void gvr_sequence_bank_view_layout(GvrSequenceBankView *view, int width, int height)
{
    const int compact = (width < 520 || height < 340);
    const int outer = compact ? 3 : 6;
    const int gap = compact ? 4 : 8;
    const int inner = compact ? 2 : 4;
    const int header_h = compact ? 14 : 18;

    int bank_w = (width - (outer * 2) - gap) / 2;
    int bank_h = (height - (outer * 2) - gap) / 2;

    if(bank_w < 24)
        bank_w = 24;
    if(bank_h < 24)
        bank_h = 24;

    int grid_space_w = bank_w - (inner * 2);
    int grid_space_h = bank_h - header_h - (inner * 2);
    int cell = grid_space_w / GVR_SEQUENCE_COLUMNS;
    int cell_h = grid_space_h / GVR_SEQUENCE_ROWS;

    if(cell_h < cell)
        cell = cell_h;
    if(cell < 2)
        cell = 2;

    const int grid_side = cell * GVR_SEQUENCE_COLUMNS;
    const int total_w = bank_w * 2 + gap;
    const int total_h = bank_h * 2 + gap;
    int start_x = (width - total_w) / 2;
    int start_y = (height - total_h) / 2;

    if(start_x < outer)
        start_x = outer;
    if(start_y < outer)
        start_y = outer;

    for(int bank = 0; bank < GVR_SEQUENCE_BANKS; bank++) {
        const int col = bank & 1;
        const int row = bank >> 1;
        GdkRectangle *br = &view->bank_rect[bank];
        GdkRectangle *hr = &view->header_rect[bank];

        br->x = start_x + col * (bank_w + gap);
        br->y = start_y + row * (bank_h + gap);
        br->width = bank_w;
        br->height = bank_h;

        hr->x = br->x;
        hr->y = br->y;
        hr->width = br->width;
        hr->height = header_h;

        const int grid_x = br->x + (br->width - grid_side) / 2;
        const int grid_y = br->y + header_h + ((br->height - header_h - grid_side) / 2);

        for(int slot = 0; slot < GVR_SEQUENCE_SLOTS; slot++) {
            GdkRectangle *cr = &view->cell_rect[bank][slot];
            const int sx = slot % GVR_SEQUENCE_COLUMNS;
            const int sy = slot / GVR_SEQUENCE_COLUMNS;

            cr->x = grid_x + sx * cell;
            cr->y = grid_y + sy * cell;
            cr->width = cell - 1;
            cr->height = cell - 1;
        }
    }
}


static void gvr_sequence_slot_color(int sample_type, double *r, double *g, double *b)
{
    switch(sample_type) {
        case GVR_SEQ_TYPE_SAMPLE:
            *r = 0.160; *g = 0.360; *b = 0.620;
            break;
        case GVR_SEQ_TYPE_YUV4MPEG:
            *r = 0.050; *g = 0.520; *b = 0.620;
            break;
        case GVR_SEQ_TYPE_V4L:
            *r = 0.080; *g = 0.520; *b = 0.240;
            break;
        case GVR_SEQ_TYPE_VLOOPBACK:
            *r = 0.050; *g = 0.510; *b = 0.500;
            break;
        case GVR_SEQ_TYPE_COLOR:
            *r = 0.840; *g = 0.500; *b = 0.080;
            break;
        case GVR_SEQ_TYPE_PICTURE:
            *r = 0.720; *g = 0.260; *b = 0.420;
            break;
        case GVR_SEQ_TYPE_CALI:
            *r = 0.470; *g = 0.520; *b = 0.120;
            break;
        case GVR_SEQ_TYPE_GENERATOR:
            *r = 0.570; *g = 0.180; *b = 0.760;
            break;
        case GVR_SEQ_TYPE_SPLITTER:
            *r = 0.420; *g = 0.380; *b = 0.550;
            break;
        case GVR_SEQ_TYPE_SHM:
            *r = 0.260; *g = 0.430; *b = 0.560;
            break;
        case GVR_SEQ_TYPE_AVFORMAT:
            *r = 0.160; *g = 0.250; *b = 0.780;
            break;
        case GVR_SEQ_TYPE_NET:
            *r = 0.760; *g = 0.260; *b = 0.100;
            break;
        case GVR_SEQ_TYPE_MCAST:
            *r = 0.760; *g = 0.090; *b = 0.150;
            break;
        case GVR_SEQ_TYPE_CLONE:
            *r = 0.320; *g = 0.350; *b = 0.430;
            break;
        case GVR_SEQ_TYPE_DV1394:
            *r = 0.560; *g = 0.390; *b = 0.140;
            break;
        default:
            *r = 0.330; *g = 0.330; *b = 0.360;
            break;
    }
}

static void gvr_sequence_slot_text_color(double r, double g, double b, double *tr, double *tg, double *tb)
{
    double y = (0.299 * r) + (0.587 * g) + (0.114 * b);

    if(y > 0.52) {
        *tr = 0.035; *tg = 0.040; *tb = 0.050;
    } else {
        *tr = 0.985; *tg = 0.990; *tb = 1.000;
    }
}

static void gvr_set_rgba(cairo_t *cr, double r, double g, double b, double a)
{
    cairo_set_source_rgba(cr, r, g, b, a);
}

static void gvr_draw_text(cairo_t *cr, const char *text, double x, double y, double size)
{
    cairo_save(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
    cairo_restore(cr);
}

static gboolean gvr_sequence_bank_view_slot_in_selection(GvrSequenceBankView *view, int bank, int slot);

static void gvr_draw_centered_text(cairo_t *cr, const char *text, const GdkRectangle *r, double size)
{
    cairo_text_extents_t ext;

    cairo_save(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, size);
    cairo_text_extents(cr, text, &ext);
    cairo_move_to(cr,
                  r->x + ((double)r->width - ext.width) * 0.5 - ext.x_bearing,
                  r->y + ((double)r->height - ext.height) * 0.5 - ext.y_bearing);
    cairo_show_text(cr, text);
    cairo_restore(cr);
}

static gboolean gvr_sequence_bank_view_draw(GtkWidget *widget, cairo_t *cr)
{
    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    GtkAllocation allocation;

    gtk_widget_get_allocation(widget, &allocation);
    gvr_sequence_bank_view_layout(view, allocation.width, allocation.height);

    gvr_set_rgba(cr, 0.070, 0.075, 0.085, 1.0);
    cairo_paint(cr);

    for(int bank = 0; bank < GVR_SEQUENCE_BANKS; bank++) {
        GdkRectangle *br = &view->bank_rect[bank];
        GdkRectangle *hr = &view->header_rect[bank];
        gboolean active_bank = (bank == view->active_bank);
        gboolean queued_bank = (bank == view->queued_bank && !active_bank);
        char title[96];

        gvr_set_rgba(cr, active_bank ? 0.125 : (queued_bank ? 0.155 : 0.100),
                     active_bank ? 0.135 : (queued_bank ? 0.125 : 0.105),
                     active_bank ? 0.155 : (queued_bank ? 0.085 : 0.118),
                     1.0);
        cairo_rectangle(cr, br->x, br->y, br->width, br->height);
        cairo_fill(cr);

        if(active_bank)
            gvr_set_rgba(cr, 0.300, 0.420, 0.650, 1.0);
        else if(queued_bank)
            gvr_set_rgba(cr, 0.940, 0.600, 0.160, 1.0);
        else
            gvr_set_rgba(cr, 0.170, 0.180, 0.205, 1.0);
        cairo_set_line_width(cr, (active_bank || queued_bank) ? 2.5 : 1.0);
        cairo_rectangle(cr, br->x + 0.5, br->y + 0.5, br->width - 1, br->height - 1);
        cairo_stroke(cr);

        gvr_set_rgba(cr,
                     active_bank ? 0.150 : (queued_bank ? 0.200 : 0.115),
                     active_bank ? 0.170 : (queued_bank ? 0.145 : 0.125),
                     active_bank ? 0.210 : (queued_bank ? 0.090 : 0.145),
                     1.0);
        cairo_rectangle(cr, hr->x + 1, hr->y + 1, hr->width - 2, hr->height - 1);
        cairo_fill(cr);

        if(br->width < 230)
            snprintf(title, sizeof(title), "B%d%s  %d",
                     bank + 1,
                     active_bank ? (view->sequence_active ? "*" : "") : (queued_bank ? ">" : ""),
                     view->banks[bank].size);
        else
            snprintf(title, sizeof(title), "Bank %d%s  %d slots  rev %u",
                     bank + 1,
                     active_bank ? (view->sequence_active ? "  ACTIVE" : "  SELECTED") : (queued_bank ? "  QUEUED" : ""),
                     view->banks[bank].size,
                     view->banks[bank].revision);
        gvr_set_rgba(cr, 0.850, 0.880, 0.920, 1.0);
        gvr_draw_text(cr, title, hr->x + (br->width < 230 ? 4 : 8), hr->y + (br->width < 230 ? 13 : 16), br->width < 230 ? 9.0 : 11.0);

        for(int slot = 0; slot < GVR_SEQUENCE_SLOTS; slot++) {
            GdkRectangle *r = &view->cell_rect[bank][slot];
            int sample_id = view->banks[bank].cells[slot].sample_id;
            int sample_type = view->banks[bank].cells[slot].sample_type;
            gboolean filled = sample_id > 0;
            gboolean current = active_bank && view->sequence_active && slot == view->banks[bank].current;
            gboolean selected = bank == view->selected_bank && slot == view->selected_slot;
            gboolean selected_range = gvr_sequence_bank_view_slot_in_selection(view, bank, slot);
            gboolean hover = bank == view->hover_bank && slot == view->hover_slot;
            gboolean drag_source = view->drag_active && bank == view->drag_bank && slot == view->drag_from_slot;
            gboolean drag_target = view->drag_active && bank == view->drag_bank && slot == view->drag_to_slot && slot != view->drag_from_slot;

            if(r->width <= 0 || r->height <= 0)
                continue;

            if(filled) {
                double rr, gg, bb;
                gvr_sequence_slot_color(sample_type, &rr, &gg, &bb);
                gvr_set_rgba(cr, rr, gg, bb, 1.0);
                cairo_rectangle(cr, r->x, r->y, r->width, r->height);
                cairo_fill(cr);

                if(sample_type != 0) {
                    gvr_set_rgba(cr, 1.0, 1.0, 1.0, 0.16);
                    cairo_rectangle(cr, r->x, r->y, r->width, 3.0);
                    cairo_fill(cr);
                }

                if(r->width >= 18 && r->height >= 11) {
                    char label[16];
                    double tr, tg, tb;
                    double label_size = r->width < 28 ? 10.0 : (r->width < 40 ? 12.0 : 14.0);
                    snprintf(label, sizeof(label), "%d", sample_id);
                    gvr_sequence_slot_text_color(rr, gg, bb, &tr, &tg, &tb);
                    gvr_set_rgba(cr, tr > 0.5 ? 0.0 : 1.0, tg > 0.5 ? 0.0 : 1.0, tb > 0.5 ? 0.0 : 1.0, 0.28);
                    cairo_save(cr);
                    cairo_translate(cr, 1.0, 1.0);
                    gvr_draw_centered_text(cr, label, r, label_size);
                    cairo_restore(cr);
                    gvr_set_rgba(cr, tr, tg, tb, 1.0);
                    gvr_draw_centered_text(cr, label, r, label_size);
                }
            }
            else {
                gvr_set_rgba(cr, 0.055, 0.060, 0.068, 1.0);
                cairo_rectangle(cr, r->x, r->y, r->width, r->height);
                cairo_fill(cr);
            }

            if(selected || selected_range) {
                gvr_set_rgba(cr, 1.000, 1.000, 1.000, 0.20);
                cairo_rectangle(cr, r->x + 1.0, r->y + 1.0, r->width - 2.0, r->height - 2.0);
                cairo_fill(cr);

                gvr_set_rgba(cr, 1.000, 1.000, 1.000, 1.0);
                cairo_set_line_width(cr, selected ? 2.8 : 2.2);
                cairo_rectangle(cr, r->x + 1.0, r->y + 1.0, r->width - 2.0, r->height - 2.0);
                cairo_stroke(cr);

                if(r->width > 7 && r->height > 7) {
                    gvr_set_rgba(cr, 0.000, 0.000, 0.000, 0.88);
                    cairo_set_line_width(cr, 1.0);
                    cairo_rectangle(cr, r->x + 3.0, r->y + 3.0, r->width - 6.0, r->height - 6.0);
                    cairo_stroke(cr);
                }
            }

            if(hover || current || drag_source || drag_target) {
                if(current)
                    gvr_set_rgba(cr, 0.950, 0.900, 0.420, 1.0);
                else if(drag_target)
                    gvr_set_rgba(cr, 0.460, 0.920, 0.620, 1.0);
                else if(drag_source)
                    gvr_set_rgba(cr, 0.920, 0.620, 0.460, 1.0);
                else
                    gvr_set_rgba(cr, 0.450, 0.500, 0.600, 0.9);
                cairo_set_line_width(cr, (current || drag_source || drag_target) ? 2.0 : 1.2);
                cairo_rectangle(cr, r->x + 1.0, r->y + 1.0, r->width - 2.0, r->height - 2.0);
                cairo_stroke(cr);
            }
        }
    }

    return FALSE;
}

static gboolean gvr_sequence_bank_view_hit(GvrSequenceBankView *view, double x, double y, int *bank, int *slot, gboolean *header)
{
    for(int b = 0; b < GVR_SEQUENCE_BANKS; b++) {
        if(gvr_rect_contains(&view->header_rect[b], x, y)) {
            *bank = b;
            *slot = -1;
            *header = TRUE;
            return TRUE;
        }

        if(!gvr_rect_contains(&view->bank_rect[b], x, y))
            continue;

        for(int s = 0; s < GVR_SEQUENCE_SLOTS; s++) {
            if(gvr_rect_contains(&view->cell_rect[b][s], x, y)) {
                *bank = b;
                *slot = s;
                *header = FALSE;
                return TRUE;
            }
        }
    }

    *bank = -1;
    *slot = -1;
    *header = FALSE;
    return FALSE;
}

static void gvr_sequence_bank_view_activate_bank(GtkWidget *widget,
                                                    GvrSequenceBankView *view,
                                                    int bank)
{
    if(!view || bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return;

    if(view->queue_mode && view->sequence_active) {
        if(bank == view->active_bank) {
            gtk_widget_queue_draw(widget);
            return;
        }

        if(bank == view->queued_bank) {
            view->queued_bank = -1;
            g_signal_emit(view,
                          gvr_sequence_bank_view_signals[SIGNAL_BANK_QUEUE_REQUESTED],
                          0,
                          -1);
            gtk_widget_queue_draw(widget);
            return;
        }

        if(view->banks[bank].size <= 0) {
            gtk_widget_queue_draw(widget);
            return;
        }

        view->queued_bank = bank;
        g_signal_emit(view,
                      gvr_sequence_bank_view_signals[SIGNAL_BANK_QUEUE_REQUESTED],
                      0,
                      bank);
    }
    else {
        g_signal_emit(view,
                      gvr_sequence_bank_view_signals[SIGNAL_BANK_SELECTED],
                      0,
                      bank);
    }

    gtk_widget_queue_draw(widget);
}

static void gvr_sequence_bank_view_recount_bank(GvrSequenceBankView *view, int bank)
{
    int n = 0;

    if(!view || bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return;

    for(int slot = 0; slot < GVR_SEQUENCE_SLOTS; slot++)
        if(view->banks[bank].cells[slot].sample_id > 0)
            n++;

    view->banks[bank].size = n;
}

static void gvr_sequence_bank_view_reset_drag(GvrSequenceBankView *view, GtkWidget *widget)
{
    if(!view)
        return;

    if(view->drag_grabbed && widget)
        gtk_grab_remove(widget);

    view->drag_active = FALSE;
    view->drag_grabbed = FALSE;
    view->drag_bank = -1;
    view->drag_from_slot = -1;
    view->drag_to_slot = -1;
}

static void gvr_sequence_bank_view_ensure_selection(GvrSequenceBankView *view)
{
    if(!view)
        return;

    if(view->selected_bank < 0 || view->selected_bank >= GVR_SEQUENCE_BANKS)
        view->selected_bank = gvr_clampi(view->active_bank, 0, GVR_SEQUENCE_BANKS - 1);

    if(view->selected_slot < 0 || view->selected_slot >= GVR_SEQUENCE_SLOTS) {
        int current = view->banks[view->selected_bank].current;
        view->selected_slot = (current >= 0 && current < GVR_SEQUENCE_SLOTS) ? current : 0;
    }
}

static void gvr_sequence_bank_view_clear_selected_cells(GvrSequenceBankView *view)
{
    if(!view)
        return;

    memset(view->selected_cells, 0, sizeof(view->selected_cells));
    view->selected_count = 0;
    view->range_active = FALSE;
}

static void gvr_sequence_bank_view_mark_selected_cell(GvrSequenceBankView *view, int slot)
{
    if(!view || slot < 0 || slot >= GVR_SEQUENCE_SLOTS)
        return;

    if(!view->selected_cells[slot]) {
        view->selected_cells[slot] = TRUE;
        view->selected_count++;
    }

    view->range_active = view->selected_count > 1;
}

static void gvr_sequence_bank_view_toggle_selected_cell(GvrSequenceBankView *view, int slot)
{
    if(!view || slot < 0 || slot >= GVR_SEQUENCE_SLOTS)
        return;

    if(view->selected_cells[slot]) {
        view->selected_cells[slot] = FALSE;
        if(view->selected_count > 0)
            view->selected_count--;
    }
    else {
        view->selected_cells[slot] = TRUE;
        view->selected_count++;
    }

    view->range_active = view->selected_count > 1;
}

static void gvr_sequence_bank_view_mouse_select_visit(GvrSequenceBankView *view, int slot)
{
    if(!view || slot < 0 || slot >= GVR_SEQUENCE_SLOTS)
        return;

    if(view->mouse_select_seen[slot])
        return;

    view->mouse_select_seen[slot] = TRUE;

    if(view->mouse_select_toggle)
        gvr_sequence_bank_view_toggle_selected_cell(view, slot);
    else
        gvr_sequence_bank_view_mark_selected_cell(view, slot);

    view->selected_slot = slot;
    view->mouse_select_last_slot = slot;
}

static void gvr_sequence_bank_view_reset_mouse_selection(GvrSequenceBankView *view, GtkWidget *widget)
{
    if(!view)
        return;

    if(view->mouse_select_grabbed && widget)
        gtk_grab_remove(widget);

    view->mouse_select_active = FALSE;
    view->mouse_select_started = FALSE;
    view->mouse_select_toggle = FALSE;
    view->mouse_select_grabbed = FALSE;
    view->mouse_select_bank = -1;
    view->mouse_select_press_slot = -1;
    view->mouse_select_last_slot = -1;
    view->mouse_select_start_x = 0.0;
    view->mouse_select_start_y = 0.0;
    memset(view->mouse_select_seen, 0, sizeof(view->mouse_select_seen));
}

static void gvr_sequence_bank_view_begin_mouse_selection(GvrSequenceBankView *view,
                                                         GtkWidget *widget,
                                                         int bank,
                                                         int slot,
                                                         gboolean toggle,
                                                         double x,
                                                         double y)
{
    if(!view || bank < 0 || bank >= GVR_SEQUENCE_BANKS || slot < 0 || slot >= GVR_SEQUENCE_SLOTS)
        return;

    view->selected_bank = bank;
    view->selected_slot = slot;

    if(!toggle)
        gvr_sequence_bank_view_clear_selected_cells(view);

    view->mouse_select_active = TRUE;
    view->mouse_select_started = FALSE;
    view->mouse_select_toggle = toggle ? TRUE : FALSE;
    view->mouse_select_grabbed = TRUE;
    view->mouse_select_bank = bank;
    view->mouse_select_press_slot = slot;
    view->mouse_select_last_slot = slot;
    view->mouse_select_start_x = x;
    view->mouse_select_start_y = y;
    memset(view->mouse_select_seen, 0, sizeof(view->mouse_select_seen));

    gtk_grab_add(widget);
    gvr_sequence_bank_view_mouse_select_visit(view, slot);
}

static void gvr_sequence_bank_view_begin_path_selection(GvrSequenceBankView *view)
{
    if(!view)
        return;

    gvr_sequence_bank_view_ensure_selection(view);

    if(view->range_active && view->selected_count > 0)
        return;

    gvr_sequence_bank_view_clear_selected_cells(view);
    view->selection_anchor_slot = view->selected_slot;
    gvr_sequence_bank_view_mark_selected_cell(view, view->selected_slot);
}

static gboolean gvr_sequence_bank_view_slot_in_selection(GvrSequenceBankView *view, int bank, int slot)
{
    if(!view || view->selected_count <= 0)
        return FALSE;

    return bank == view->selected_bank &&
           slot >= 0 && slot < GVR_SEQUENCE_SLOTS &&
           view->selected_cells[slot];
}

static gboolean gvr_sequence_bank_view_has_slot_target(GvrSequenceBankView *view)
{
    return view &&
           view->selected_bank >= 0 &&
           view->selected_bank < GVR_SEQUENCE_BANKS &&
           view->selected_slot >= 0 &&
           view->selected_slot < GVR_SEQUENCE_SLOTS;
}

static gboolean gvr_sequence_bank_view_slot_is_filled(GvrSequenceBankView *view, int bank, int slot)
{
    return view &&
           bank >= 0 && bank < GVR_SEQUENCE_BANKS &&
           slot >= 0 && slot < GVR_SEQUENCE_SLOTS &&
           view->banks[bank].cells[slot].sample_id > 0;
}

static gboolean gvr_sequence_bank_view_selection_has_filled_cells(GvrSequenceBankView *view)
{
    int bank;

    if(!view)
        return FALSE;

    bank = view->selected_bank;
    if(bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return FALSE;

    if(view->selected_count > 0) {
        for(int slot = 0; slot < GVR_SEQUENCE_SLOTS; slot++)
            if(view->selected_cells[slot] &&
               view->banks[bank].cells[slot].sample_id > 0)
                return TRUE;

        return FALSE;
    }

    return gvr_sequence_bank_view_slot_is_filled(view, bank, view->selected_slot);
}

static void gvr_sequence_bank_view_collapse_selection(GvrSequenceBankView *view)
{
    if(!view)
        return;

    gvr_sequence_bank_view_ensure_selection(view);
    gvr_sequence_bank_view_clear_selected_cells(view);
    view->selection_anchor_slot = view->selected_slot;
}

static gboolean gvr_sequence_bank_view_move_selection(GvrSequenceBankView *view, int dx, int dy, gboolean extend)
{
    int col;
    int row;
    int new_col;
    int new_row;
    int new_slot;

    if(!view)
        return FALSE;

    gvr_sequence_bank_view_ensure_selection(view);

    if(extend)
        gvr_sequence_bank_view_begin_path_selection(view);

    col = view->selected_slot % GVR_SEQUENCE_COLUMNS;
    row = view->selected_slot / GVR_SEQUENCE_COLUMNS;
    new_col = gvr_clampi(col + dx, 0, GVR_SEQUENCE_COLUMNS - 1);
    new_row = gvr_clampi(row + dy, 0, GVR_SEQUENCE_ROWS - 1);
    new_slot = new_row * GVR_SEQUENCE_COLUMNS + new_col;

    if(new_slot == view->selected_slot) {
        if(!extend)
            gvr_sequence_bank_view_collapse_selection(view);
        return FALSE;
    }

    view->selected_slot = new_slot;

    if(extend)
        gvr_sequence_bank_view_mark_selected_cell(view, new_slot);
    else
        gvr_sequence_bank_view_collapse_selection(view);

    return TRUE;
}

static int gvr_sequence_bank_view_copy_selection(GvrSequenceBankView *view, gboolean whole_bank)
{
    int bank;
    int copied = 0;
    int base_slot = GVR_SEQUENCE_SLOTS;

    if(!view)
        return 0;

    if(whole_bank) {
        if(view->selected_bank < 0 || view->selected_bank >= GVR_SEQUENCE_BANKS)
            view->selected_bank = gvr_clampi(view->active_bank, 0, GVR_SEQUENCE_BANKS - 1);
    }
    else {
        gvr_sequence_bank_view_ensure_selection(view);
        if(!gvr_sequence_bank_view_selection_has_filled_cells(view))
            return 0;
    }

    bank = view->selected_bank;
    if(bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return 0;

    view->copy_count = 0;
    view->copy_base_slot = -1;
    view->copy_is_bank = whole_bank ? TRUE : FALSE;
    view->copy_bank = bank;

    if(whole_bank) {
        base_slot = 0;
        for(int slot = 0; slot < GVR_SEQUENCE_SLOTS; slot++) {
            view->copy_cells[view->copy_count] = view->banks[bank].cells[slot];
            view->copy_offsets[view->copy_count] = slot;
            if(view->banks[bank].cells[slot].sample_id > 0)
                copied++;
            view->copy_count++;
        }
    }
    else {
        if(view->selected_count > 0) {
            for(int slot = 0; slot < GVR_SEQUENCE_SLOTS; slot++) {
                if(view->selected_cells[slot]) {
                    base_slot = slot;
                    break;
                }
            }
        }
        else {
            base_slot = view->selected_slot;
        }

        if(base_slot < 0 || base_slot >= GVR_SEQUENCE_SLOTS)
            return 0;

        for(int slot = base_slot; slot < GVR_SEQUENCE_SLOTS && view->copy_count < GVR_SEQUENCE_SLOTS; slot++) {
            if((view->selected_count > 0 && view->selected_cells[slot]) ||
               (view->selected_count <= 0 && slot == view->selected_slot))
            {
                view->copy_cells[view->copy_count] = view->banks[bank].cells[slot];
                view->copy_offsets[view->copy_count] = slot - base_slot;
                if(view->banks[bank].cells[slot].sample_id > 0)
                    copied++;
                view->copy_count++;
            }
        }
    }

    view->copy_base_slot = base_slot;
    view->copy_valid = view->copy_count > 0;

    if(whole_bank) {
        view->bank_copy_valid = TRUE;
        view->bank_copy_source = bank;
    }

    return copied;
}

static void gvr_sequence_bank_view_delete_selection(GtkWidget *widget, GvrSequenceBankView *view)
{
    int bank;

    if(!view)
        return;

    gvr_sequence_bank_view_ensure_selection(view);
    bank = view->selected_bank;

    if(bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return;

    if(view->selected_count > 0) {
        for(int slot = 0; slot < GVR_SEQUENCE_SLOTS; slot++) {
            if(view->selected_cells[slot] && view->banks[bank].cells[slot].sample_id > 0)
                g_signal_emit(view,
                              gvr_sequence_bank_view_signals[SIGNAL_SLOT_DELETE_REQUESTED],
                              0,
                              bank,
                              slot);
        }
    }
    else if(view->selected_slot >= 0 && view->selected_slot < GVR_SEQUENCE_SLOTS &&
            view->banks[bank].cells[view->selected_slot].sample_id > 0)
    {
        g_signal_emit(view,
                      gvr_sequence_bank_view_signals[SIGNAL_SLOT_DELETE_REQUESTED],
                      0,
                      bank,
                      view->selected_slot);
    }

    gtk_widget_queue_draw(widget);
}

static void gvr_sequence_bank_view_paste_cells_to(GtkWidget *widget, GvrSequenceBankView *view, int bank, int first)
{
    int last = -1;

    if(!view || !view->copy_valid || view->copy_count <= 0)
        return;

    if(bank < 0 || bank >= GVR_SEQUENCE_BANKS || first < 0 || first >= GVR_SEQUENCE_SLOTS)
        return;

    view->selected_bank = bank;
    view->selected_slot = first;
    gvr_sequence_bank_view_clear_selected_cells(view);

    for(int i = 0; i < view->copy_count; i++) {
        int slot = first + view->copy_offsets[i];
        GvrSequenceCell *cell = &view->copy_cells[i];

        if(slot < 0 || slot >= GVR_SEQUENCE_SLOTS)
            continue;

        if(cell->sample_id > 0) {
            g_signal_emit(view,
                          gvr_sequence_bank_view_signals[SIGNAL_SLOT_PASTE_REQUESTED],
                          0,
                          bank,
                          slot,
                          cell->sample_id,
                          cell->sample_type);
        }
        else if(view->banks[bank].cells[slot].sample_id > 0) {
            g_signal_emit(view,
                          gvr_sequence_bank_view_signals[SIGNAL_SLOT_DELETE_REQUESTED],
                          0,
                          bank,
                          slot);
        }

        gvr_sequence_bank_view_mark_selected_cell(view, slot);
        last = slot;
    }

    if(last >= 0)
        view->selected_slot = last;

    view->range_active = view->selected_count > 1;
    gtk_widget_queue_draw(widget);
}

static void gvr_sequence_bank_view_paste_cells(GtkWidget *widget, GvrSequenceBankView *view)
{
    if(!view)
        return;

    gvr_sequence_bank_view_ensure_selection(view);
    gvr_sequence_bank_view_paste_cells_to(widget, view, view->selected_bank, view->selected_slot);
}

static void gvr_sequence_bank_view_copy_selection_to_bank(GtkWidget *widget, GvrSequenceBankView *view, int dst_bank)
{
    int first;

    if(!view || dst_bank < 0 || dst_bank >= GVR_SEQUENCE_BANKS)
        return;

    if(gvr_sequence_bank_view_copy_selection(view, FALSE) <= 0)
        return;

    if(!view->copy_valid || view->copy_count <= 0)
        return;

    first = view->copy_base_slot;
    if(first < 0 || first >= GVR_SEQUENCE_SLOTS)
        first = 0;

    gvr_sequence_bank_view_paste_cells_to(widget, view, dst_bank, first);
}

static void gvr_sequence_bank_view_paste_bank(GtkWidget *widget, GvrSequenceBankView *view)
{
    int dst;

    if(!view->bank_copy_valid)
        return;

    if(view->selected_bank < 0 || view->selected_bank >= GVR_SEQUENCE_BANKS)
        view->selected_bank = gvr_clampi(view->active_bank, 0, GVR_SEQUENCE_BANKS - 1);

    dst = view->selected_bank;

    if(dst < 0 || dst >= GVR_SEQUENCE_BANKS)
        return;

    if(view->bank_copy_source == dst)
        return;

    g_signal_emit(view,
                  gvr_sequence_bank_view_signals[SIGNAL_BANK_PASTE_REQUESTED],
                  0,
                  view->bank_copy_source,
                  dst);
    gtk_widget_queue_draw(widget);
}

static gboolean gvr_sequence_bank_view_key_press(GtkWidget *widget, GdkEventKey *event)
{
    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (event->state & GDK_SHIFT_MASK) != 0;
    int sample_id = 0;

    gvr_sequence_bank_view_ensure_selection(view);

    if((event->state & GDK_MOD1_MASK) != 0) {
        int bank = -1;

        switch(event->keyval) {
            case GDK_KEY_1: case GDK_KEY_KP_1: bank = 0; break;
            case GDK_KEY_2: case GDK_KEY_KP_2: bank = 1; break;
            case GDK_KEY_3: case GDK_KEY_KP_3: bank = 2; break;
            case GDK_KEY_4: case GDK_KEY_KP_4: bank = 3; break;
            default: break;
        }

        if(bank >= 0 && bank < GVR_SEQUENCE_BANKS) {
            view->selected_bank = bank;
            if(view->selected_slot < 0 || view->selected_slot >= GVR_SEQUENCE_SLOTS)
                view->selected_slot = 0;
            gvr_sequence_bank_view_clear_selected_cells(view);
            view->selection_anchor_slot = view->selected_slot;
            gvr_sequence_bank_view_activate_bank(widget, view, bank);
            return TRUE;
        }
    }

    switch(event->keyval) {
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            if(gvr_sequence_bank_view_move_selection(view, -1, 0, ctrl))
                gtk_widget_queue_draw(widget);
            return TRUE;

        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            if(gvr_sequence_bank_view_move_selection(view, 1, 0, ctrl))
                gtk_widget_queue_draw(widget);
            return TRUE;

        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            if(gvr_sequence_bank_view_move_selection(view, 0, -1, ctrl))
                gtk_widget_queue_draw(widget);
            return TRUE;

        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            if(gvr_sequence_bank_view_move_selection(view, 0, 1, ctrl))
                gtk_widget_queue_draw(widget);
            return TRUE;

        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete:
        case GDK_KEY_BackSpace:
            gvr_sequence_bank_view_delete_selection(widget, view);
            return TRUE;

        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            sample_id = view->banks[view->selected_bank].cells[view->selected_slot].sample_id;
            if(sample_id <= 0)
                g_signal_emit(view,
                              gvr_sequence_bank_view_signals[SIGNAL_SLOT_ASSIGN_REQUESTED],
                              0,
                              view->selected_bank,
                              view->selected_slot);
            gvr_sequence_bank_view_collapse_selection(view);
            return TRUE;

        case GDK_KEY_c:
        case GDK_KEY_C:
            if(!ctrl)
                return FALSE;
            gvr_sequence_bank_view_copy_selection(view, shift);
            return TRUE;

        case GDK_KEY_x:
        case GDK_KEY_X:
            if(!ctrl || shift)
                return FALSE;
            gvr_sequence_bank_view_copy_selection(view, FALSE);
            gvr_sequence_bank_view_delete_selection(widget, view);
            return TRUE;

        case GDK_KEY_v:
        case GDK_KEY_V:
            if(!ctrl)
                return FALSE;
            if(shift)
                gvr_sequence_bank_view_paste_bank(widget, view);
            else
                gvr_sequence_bank_view_paste_cells(widget, view);
            return TRUE;

        default:
            break;
    }

    return FALSE;
}

enum {
    GVR_SEQUENCE_MENU_ASSIGN = 1,
    GVR_SEQUENCE_MENU_DELETE,
    GVR_SEQUENCE_MENU_COPY,
    GVR_SEQUENCE_MENU_CUT,
    GVR_SEQUENCE_MENU_PASTE,
    GVR_SEQUENCE_MENU_COPY_BANK,
    GVR_SEQUENCE_MENU_PASTE_BANK,
    GVR_SEQUENCE_MENU_COPY_TO_BANK,
    GVR_SEQUENCE_MENU_CLEAR_BANK,
    GVR_SEQUENCE_MENU_QUEUE_BANK,
    GVR_SEQUENCE_MENU_REFRESH
};

typedef struct {
    GtkWidget *widget;
    int action;
    int bank;
} GvrSequenceMenuAction;

static void gvr_sequence_bank_view_menu_action_free(gpointer user_data, GClosure *closure)
{
    (void)closure;
    GvrSequenceMenuAction *data = (GvrSequenceMenuAction *)user_data;

    if(!data)
        return;

    if(data->widget)
        g_object_unref(data->widget);

    g_free(data);
}

static void gvr_sequence_bank_view_menu_done(GtkWidget *menu, gpointer user_data)
{
    (void)user_data;
    gtk_widget_destroy(menu);
}

static void gvr_sequence_bank_view_menu_action(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    GvrSequenceMenuAction *data = (GvrSequenceMenuAction *)user_data;

    if(!data || !GVR_IS_SEQUENCE_BANK_VIEW(data->widget))
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(data->widget);

    switch(data->action) {
        case GVR_SEQUENCE_MENU_ASSIGN:
            if(gvr_sequence_bank_view_has_slot_target(view))
                g_signal_emit(view,
                              gvr_sequence_bank_view_signals[SIGNAL_SLOT_ASSIGN_REQUESTED],
                              0,
                              view->selected_bank,
                              view->selected_slot);
            gtk_widget_queue_draw(data->widget);
            break;
        case GVR_SEQUENCE_MENU_DELETE:
            gvr_sequence_bank_view_delete_selection(data->widget, view);
            break;
        case GVR_SEQUENCE_MENU_COPY:
            if(gvr_sequence_bank_view_copy_selection(view, FALSE) > 0)
                gtk_widget_queue_draw(data->widget);
            break;
        case GVR_SEQUENCE_MENU_CUT:
            if(gvr_sequence_bank_view_copy_selection(view, FALSE) > 0)
                gvr_sequence_bank_view_delete_selection(data->widget, view);
            break;
        case GVR_SEQUENCE_MENU_PASTE:
            gvr_sequence_bank_view_paste_cells(data->widget, view);
            break;
        case GVR_SEQUENCE_MENU_COPY_BANK:
            gvr_sequence_bank_view_copy_selection(view, TRUE);
            gtk_widget_queue_draw(data->widget);
            break;
        case GVR_SEQUENCE_MENU_PASTE_BANK:
            gvr_sequence_bank_view_paste_bank(data->widget, view);
            break;
        case GVR_SEQUENCE_MENU_COPY_TO_BANK:
            gvr_sequence_bank_view_copy_selection_to_bank(data->widget, view, data->bank);
            break;
        case GVR_SEQUENCE_MENU_CLEAR_BANK:
            if(view->selected_bank >= 0 && view->selected_bank < GVR_SEQUENCE_BANKS)
                g_signal_emit(view, gvr_sequence_bank_view_signals[SIGNAL_BANK_CLEAR_REQUESTED], 0, view->selected_bank);
            gtk_widget_queue_draw(data->widget);
            break;
        case GVR_SEQUENCE_MENU_QUEUE_BANK: {
            int bank = view->selected_bank;

            if(bank == view->active_bank)
                break;

            if(bank == view->queued_bank)
                bank = -1;
            else if(bank < 0 || bank >= GVR_SEQUENCE_BANKS || view->banks[bank].size <= 0)
                break;

            if(view->sequence_active)
                view->queued_bank = bank;

            g_signal_emit(view, gvr_sequence_bank_view_signals[SIGNAL_BANK_QUEUE_REQUESTED], 0, bank);
            gtk_widget_queue_draw(data->widget);
            break;
        }
        case GVR_SEQUENCE_MENU_REFRESH:
            g_signal_emit(view, gvr_sequence_bank_view_signals[SIGNAL_REFRESH_REQUESTED], 0);
            break;
        default:
            break;
    }
}

static GtkWidget *gvr_sequence_bank_view_menu_item(GtkWidget *menu,
                                                   const char *label,
                                                   GtkWidget *widget,
                                                   int action,
                                                   int bank,
                                                   gboolean sensitive)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    GvrSequenceMenuAction *data = g_new0(GvrSequenceMenuAction, 1);

    data->widget = g_object_ref(widget);
    data->action = action;
    data->bank = bank;

    gtk_widget_set_sensitive(item, sensitive);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect_data(G_OBJECT(item),
                          "activate",
                          G_CALLBACK(gvr_sequence_bank_view_menu_action),
                          data,
                          gvr_sequence_bank_view_menu_action_free,
                          0);
    gtk_widget_show(item);

    return item;
}

static void gvr_sequence_bank_view_popup_menu(GtkWidget *widget,
                                              GvrSequenceBankView *view,
                                              GdkEventButton *event,
                                              gboolean bank_only)
{
    GtkWidget *menu = gtk_menu_new();
    gboolean bank_valid = view->selected_bank >= 0 &&
                          view->selected_bank < GVR_SEQUENCE_BANKS;
    gboolean have_slot_target = gvr_sequence_bank_view_has_slot_target(view);
    gboolean have_filled_selection = gvr_sequence_bank_view_selection_has_filled_cells(view);
    gboolean selected_slot_empty = have_slot_target &&
                                   !gvr_sequence_bank_view_slot_is_filled(view,
                                                                          view->selected_bank,
                                                                          view->selected_slot);
    gboolean can_paste = view->copy_valid &&
                         !view->copy_is_bank &&
                         view->copy_count > 0 &&
                         have_slot_target;
    gboolean can_paste_bank = view->bank_copy_valid &&
                              view->bank_copy_source >= 0 &&
                              view->bank_copy_source < GVR_SEQUENCE_BANKS &&
                              bank_valid &&
                              view->bank_copy_source != view->selected_bank;

    if(!bank_only) {
        gvr_sequence_bank_view_menu_item(menu, "Assign sample here", widget, GVR_SEQUENCE_MENU_ASSIGN, -1, selected_slot_empty);
        gvr_sequence_bank_view_menu_item(menu, "Delete selected", widget, GVR_SEQUENCE_MENU_DELETE, -1, have_filled_selection);
        gvr_sequence_bank_view_menu_item(menu, "Copy selected", widget, GVR_SEQUENCE_MENU_COPY, -1, have_filled_selection);
        gvr_sequence_bank_view_menu_item(menu, "Cut selected", widget, GVR_SEQUENCE_MENU_CUT, -1, have_filled_selection);
        gvr_sequence_bank_view_menu_item(menu, "Paste here", widget, GVR_SEQUENCE_MENU_PASTE, -1, can_paste);

        GtkWidget *copy_menu = gtk_menu_new();
        GtkWidget *copy_root = gtk_menu_item_new_with_label("Copy selected to bank");
        gtk_widget_set_sensitive(copy_root, have_filled_selection);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(copy_root), copy_menu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_root);
        gtk_widget_show(copy_root);

        for(int b = 0; b < GVR_SEQUENCE_BANKS; b++) {
            char label[32];
            snprintf(label, sizeof(label), "Bank %d", b + 1);
            gvr_sequence_bank_view_menu_item(copy_menu,
                                             label,
                                             widget,
                                             GVR_SEQUENCE_MENU_COPY_TO_BANK,
                                             b,
                                             have_filled_selection && b != view->selected_bank);
        }

        GtkWidget *sep = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
        gtk_widget_show(sep);
    }

    if(view->selected_bank == view->queued_bank)
        gvr_sequence_bank_view_menu_item(menu, "Cancel queued bank", widget, GVR_SEQUENCE_MENU_QUEUE_BANK, -1, TRUE);
    else if(view->queued_bank >= 0)
        gvr_sequence_bank_view_menu_item(menu, "Replace queued bank", widget, GVR_SEQUENCE_MENU_QUEUE_BANK, -1,
                                         bank_valid && view->selected_bank != view->active_bank &&
                                         view->banks[view->selected_bank].size > 0);
    else
        gvr_sequence_bank_view_menu_item(menu, "Play after current bank", widget, GVR_SEQUENCE_MENU_QUEUE_BANK, -1,
                                         bank_valid && view->selected_bank != view->active_bank &&
                                         view->banks[view->selected_bank].size > 0);

    GtkWidget *queue_sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), queue_sep);
    gtk_widget_show(queue_sep);

    gvr_sequence_bank_view_menu_item(menu, "Copy whole bank", widget, GVR_SEQUENCE_MENU_COPY_BANK, -1, bank_valid);
    gvr_sequence_bank_view_menu_item(menu, "Paste bank here", widget, GVR_SEQUENCE_MENU_PASTE_BANK, -1, can_paste_bank);
    gvr_sequence_bank_view_menu_item(menu, "Clear bank", widget, GVR_SEQUENCE_MENU_CLEAR_BANK, -1, bank_valid);

    GtkWidget *sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
    gtk_widget_show(sep);

    gvr_sequence_bank_view_menu_item(menu, "Refresh", widget, GVR_SEQUENCE_MENU_REFRESH, -1, TRUE);

    g_signal_connect(G_OBJECT(menu), "selection-done", G_CALLBACK(gvr_sequence_bank_view_menu_done), NULL);
    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu),
                   NULL,
                   NULL,
                   NULL,
                   NULL,
                   event ? event->button : 0,
                   event ? event->time : gtk_get_current_event_time());
}

static gboolean gvr_sequence_bank_view_button_press(GtkWidget *widget, GdkEventButton *event)
{
    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    GtkAllocation allocation;
    int bank = -1;
    int slot = -1;
    gboolean header = FALSE;

    gtk_widget_grab_focus(widget);
    gtk_widget_get_allocation(widget, &allocation);
    gvr_sequence_bank_view_layout(view, allocation.width, allocation.height);

    gvr_sequence_bank_view_reset_drag(view, widget);
    gvr_sequence_bank_view_reset_mouse_selection(view, widget);

    if(!gvr_sequence_bank_view_hit(view, event->x, event->y, &bank, &slot, &header))
        return FALSE;

    if(header) {
        view->selected_bank = bank;
        view->selected_slot = -1;
        gvr_sequence_bank_view_clear_selected_cells(view);
        view->selection_anchor_slot = -1;
        gtk_widget_queue_draw(widget);

        if(event->button == 1)
            gvr_sequence_bank_view_activate_bank(widget, view, bank);
        else if(event->button == 3)
            gvr_sequence_bank_view_popup_menu(widget, view, event, TRUE);

        return TRUE;
    }

    if(event->button == 3) {
        if(bank != view->selected_bank ||
           !gvr_sequence_bank_view_slot_in_selection(view, bank, slot))
        {
            view->selected_bank = bank;
            view->selected_slot = slot;
            gvr_sequence_bank_view_clear_selected_cells(view);
            view->selection_anchor_slot = slot;
        }

        gtk_widget_queue_draw(widget);
        gvr_sequence_bank_view_popup_menu(widget, view, event, FALSE);
        return TRUE;
    }

    if(event->button == 1 && (event->state & GDK_SHIFT_MASK)) {
        view->selected_bank = bank;
        view->selected_slot = slot;
        gvr_sequence_bank_view_clear_selected_cells(view);
        view->selection_anchor_slot = slot;
        gtk_widget_queue_draw(widget);

        if(view->banks[bank].cells[slot].sample_id > 0)
            g_signal_emit(view, gvr_sequence_bank_view_signals[SIGNAL_SLOT_DELETE_REQUESTED], 0, bank, slot);

        return TRUE;
    }

    if(event->button == 1 && (event->state & GDK_MOD1_MASK)) {
        view->selected_bank = bank;
        view->selected_slot = slot;
        gvr_sequence_bank_view_clear_selected_cells(view);
        view->selection_anchor_slot = slot;

        if(view->banks[bank].cells[slot].sample_id > 0) {
            view->drag_active = TRUE;
            view->drag_grabbed = TRUE;
            view->drag_bank = bank;
            view->drag_from_slot = slot;
            view->drag_to_slot = slot;
            gtk_grab_add(widget);
        }
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    if(event->button == 1) {
        gboolean toggle = (event->state & GDK_CONTROL_MASK) != 0;

        gvr_sequence_bank_view_begin_mouse_selection(view, widget, bank, slot, toggle, event->x, event->y);
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    return FALSE;
}

static gboolean gvr_sequence_bank_view_button_release(GtkWidget *widget, GdkEventButton *event)
{
    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    GtkAllocation allocation;
    int bank = -1;
    int slot = -1;
    gboolean header = FALSE;
    gboolean valid_drop = FALSE;

    if(view->mouse_select_active && event->button == 1) {
        gboolean was_toggle = view->mouse_select_toggle;
        gboolean was_started = view->mouse_select_started;
        int press_bank = view->mouse_select_bank;
        int press_slot = view->mouse_select_press_slot;

        gvr_sequence_bank_view_reset_mouse_selection(view, widget);

        if(!was_toggle && !was_started &&
           press_bank >= 0 && press_bank < GVR_SEQUENCE_BANKS &&
           press_slot >= 0 && press_slot < GVR_SEQUENCE_SLOTS)
        {
            view->selected_bank = press_bank;
            view->selected_slot = press_slot;
            view->selection_anchor_slot = press_slot;

            if(view->banks[press_bank].cells[press_slot].sample_id <= 0)
                g_signal_emit(view,
                              gvr_sequence_bank_view_signals[SIGNAL_SLOT_ASSIGN_REQUESTED],
                              0,
                              press_bank,
                              press_slot);
        }

        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    if(!view->drag_active || event->button != 1)
        return FALSE;

    gtk_widget_get_allocation(widget, &allocation);
    gvr_sequence_bank_view_layout(view, allocation.width, allocation.height);

    if(gvr_sequence_bank_view_hit(view, event->x, event->y, &bank, &slot, &header) && !header && bank == view->drag_bank) {
        view->drag_to_slot = slot;
        valid_drop = TRUE;
    }

    if(valid_drop &&
       view->drag_bank >= 0 &&
       view->drag_from_slot >= 0 &&
       view->drag_to_slot >= 0 &&
       view->drag_from_slot != view->drag_to_slot)
    {
        view->selected_bank = view->drag_bank;
        view->selected_slot = view->drag_to_slot;
        gvr_sequence_bank_view_clear_selected_cells(view);
        view->selection_anchor_slot = view->selected_slot;
        g_signal_emit(view,
                      gvr_sequence_bank_view_signals[SIGNAL_SLOT_REORDER_REQUESTED],
                      0,
                      view->drag_bank,
                      view->drag_from_slot,
                      view->drag_to_slot);
    }

    gvr_sequence_bank_view_reset_drag(view, widget);
    gtk_widget_queue_draw(widget);

    return TRUE;
}

static gboolean gvr_sequence_bank_view_motion(GtkWidget *widget, GdkEventMotion *event)
{
    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    int bank = -1;
    int slot = -1;
    gboolean header = FALSE;

    if(gvr_sequence_bank_view_hit(view, event->x, event->y, &bank, &slot, &header) && !header) {
        gboolean changed = FALSE;

        if(bank != view->hover_bank || slot != view->hover_slot) {
            view->hover_bank = bank;
            view->hover_slot = slot;
            changed = TRUE;
        }

        if(view->mouse_select_active && bank == view->mouse_select_bank) {
            const double dx = event->x - view->mouse_select_start_x;
            const double dy = event->y - view->mouse_select_start_y;

            if(slot != view->mouse_select_press_slot || (dx * dx + dy * dy) >= 9.0)
                view->mouse_select_started = TRUE;

            if(slot != view->mouse_select_last_slot || !view->mouse_select_seen[slot]) {
                gvr_sequence_bank_view_mouse_select_visit(view, slot);
                changed = TRUE;
            }
        }

        if(view->drag_active && bank == view->drag_bank && slot != view->drag_to_slot) {
            view->drag_to_slot = slot;
            changed = TRUE;
        }

        if(changed)
            gtk_widget_queue_draw(widget);
    }
    else {
        gboolean changed = FALSE;

        if(view->hover_bank >= 0 || view->hover_slot >= 0) {
            view->hover_bank = -1;
            view->hover_slot = -1;
            changed = TRUE;
        }

        if(view->drag_active && view->drag_to_slot != view->drag_from_slot) {
            view->drag_to_slot = view->drag_from_slot;
            changed = TRUE;
        }

        if(changed)
            gtk_widget_queue_draw(widget);
    }

    return (view->drag_active || view->mouse_select_active) ? TRUE : FALSE;
}

static gboolean gvr_sequence_bank_view_leave(GtkWidget *widget, GdkEventCrossing *event)
{
    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    (void)event;

    if(view->hover_bank >= 0 || view->hover_slot >= 0) {
        view->hover_bank = -1;
        view->hover_slot = -1;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

static void gvr_sequence_bank_view_class_init(GvrSequenceBankViewClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    widget_class->draw = gvr_sequence_bank_view_draw;
    widget_class->button_press_event = gvr_sequence_bank_view_button_press;
    widget_class->button_release_event = gvr_sequence_bank_view_button_release;
    widget_class->key_press_event = gvr_sequence_bank_view_key_press;
    widget_class->motion_notify_event = gvr_sequence_bank_view_motion;
    widget_class->leave_notify_event = gvr_sequence_bank_view_leave;

    gvr_sequence_bank_view_signals[SIGNAL_BANK_SELECTED] =
        g_signal_new("bank-selected",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    gvr_sequence_bank_view_signals[SIGNAL_BANK_QUEUE_REQUESTED] =
        g_signal_new("bank-queue-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    gvr_sequence_bank_view_signals[SIGNAL_SLOT_ASSIGN_REQUESTED] =
        g_signal_new("slot-assign-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_INT,
                     G_TYPE_INT);

    gvr_sequence_bank_view_signals[SIGNAL_SLOT_DELETE_REQUESTED] =
        g_signal_new("slot-delete-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_INT,
                     G_TYPE_INT);

    gvr_sequence_bank_view_signals[SIGNAL_SLOT_REORDER_REQUESTED] =
        g_signal_new("slot-reorder-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     3,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_INT);

    gvr_sequence_bank_view_signals[SIGNAL_SLOT_PASTE_REQUESTED] =
        g_signal_new("slot-paste-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     4,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_INT);

    gvr_sequence_bank_view_signals[SIGNAL_BANK_PASTE_REQUESTED] =
        g_signal_new("bank-paste-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_INT,
                     G_TYPE_INT);

    gvr_sequence_bank_view_signals[SIGNAL_BANK_CLEAR_REQUESTED] =
        g_signal_new("bank-clear-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    gvr_sequence_bank_view_signals[SIGNAL_REFRESH_REQUESTED] =
        g_signal_new("refresh-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);
}

static void gvr_sequence_bank_view_init(GvrSequenceBankView *view)
{
    gtk_widget_add_events(GTK_WIDGET(view),
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_LEAVE_NOTIFY_MASK);
    gtk_widget_set_can_focus(GTK_WIDGET(view), TRUE);
    gtk_widget_set_size_request(GTK_WIDGET(view), 520, 236);

    view->active_bank = 0;
    view->queued_bank = -1;
    view->queue_mode = FALSE;
    view->selected_bank = -1;
    view->selected_slot = -1;
    view->hover_bank = -1;
    view->hover_slot = -1;
    view->drag_active = FALSE;
    view->drag_grabbed = FALSE;
    view->drag_bank = -1;
    view->drag_from_slot = -1;
    view->drag_to_slot = -1;
    view->mouse_select_active = FALSE;
    view->mouse_select_started = FALSE;
    view->mouse_select_toggle = FALSE;
    view->mouse_select_grabbed = FALSE;
    view->mouse_select_bank = -1;
    view->mouse_select_press_slot = -1;
    view->mouse_select_last_slot = -1;
    view->mouse_select_start_x = 0.0;
    view->mouse_select_start_y = 0.0;
    memset(view->mouse_select_seen, 0, sizeof(view->mouse_select_seen));
    view->range_active = FALSE;
    view->selection_anchor_slot = -1;
    memset(view->selected_cells, 0, sizeof(view->selected_cells));
    view->selected_count = 0;
    view->copy_valid = FALSE;
    memset(view->copy_offsets, 0, sizeof(view->copy_offsets));
    view->copy_count = 0;
    view->copy_base_slot = -1;
    view->copy_is_bank = FALSE;
    view->copy_bank = -1;
    view->bank_copy_valid = FALSE;
    view->bank_copy_source = -1;
    view->sequence_active = FALSE;

    gtk_widget_set_has_tooltip(GTK_WIDGET(view), TRUE);

    for(int bank = 0; bank < GVR_SEQUENCE_BANKS; bank++) {
        view->banks[bank].current = -1;
        view->banks[bank].size = 0;
        view->banks[bank].revision = 0;
    }
}

GtkWidget *gvr_sequence_bank_view_new(void)
{
    return g_object_new(GVR_TYPE_SEQUENCE_BANK_VIEW, NULL);
}

void gvr_sequence_bank_view_set_active_bank(GtkWidget *widget, int bank)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget))
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    view->active_bank = gvr_clampi(bank, 0, GVR_SEQUENCE_BANKS - 1);
    if(view->queued_bank == view->active_bank)
        view->queued_bank = -1;
    gtk_widget_queue_draw(widget);
}

void gvr_sequence_bank_view_set_queued_bank(GtkWidget *widget, int bank)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget))
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    view->queued_bank = (bank >= 0 && bank < GVR_SEQUENCE_BANKS && bank != view->active_bank) ? bank : -1;
    gtk_widget_queue_draw(widget);
}

int gvr_sequence_bank_view_get_queued_bank(GtkWidget *widget)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget))
        return -1;

    return GVR_SEQUENCE_BANK_VIEW(widget)->queued_bank;
}

void gvr_sequence_bank_view_set_queue_mode(GtkWidget *widget, gboolean enabled)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget))
        return;

    GVR_SEQUENCE_BANK_VIEW(widget)->queue_mode = enabled ? TRUE : FALSE;
}

gboolean gvr_sequence_bank_view_get_queue_mode(GtkWidget *widget)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget))
        return FALSE;

    return GVR_SEQUENCE_BANK_VIEW(widget)->queue_mode;
}

void gvr_sequence_bank_view_set_selected_bank(GtkWidget *widget, int bank)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget) || bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    view->selected_bank = bank;
    view->selected_slot = -1;
    view->selection_anchor_slot = -1;
    gvr_sequence_bank_view_clear_selected_cells(view);
    gtk_widget_queue_draw(widget);
}

int gvr_sequence_bank_view_get_selected_bank(GtkWidget *widget)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget))
        return -1;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);

    if(view->selected_bank < 0 || view->selected_bank >= GVR_SEQUENCE_BANKS)
        return -1;

    return view->selected_bank;
}

void gvr_sequence_bank_view_set_sequence_active(GtkWidget *widget, gboolean active)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget))
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    view->sequence_active = active;
    gtk_widget_queue_draw(widget);
}

void gvr_sequence_bank_view_set_current_slot(GtkWidget *widget, int bank, int slot)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget) || bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    view->banks[bank].current = (slot >= 0 && slot < GVR_SEQUENCE_SLOTS) ? slot : -1;
    gtk_widget_queue_draw(widget);
}

void gvr_sequence_bank_view_set_bank_revision(GtkWidget *widget, int bank, unsigned int revision)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget) || bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    view->banks[bank].revision = revision;
    gtk_widget_queue_draw(widget);
}

void gvr_sequence_bank_view_set_bank_size(GtkWidget *widget, int bank, int size)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget) || bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    view->banks[bank].size = gvr_clampi(size, 0, GVR_SEQUENCE_SLOTS);
    gtk_widget_queue_draw(widget);
}

void gvr_sequence_bank_view_set_slot(GtkWidget *widget, int bank, int slot, int sample_id, int sample_type)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget) || bank < 0 || bank >= GVR_SEQUENCE_BANKS || slot < 0 || slot >= GVR_SEQUENCE_SLOTS)
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);

    if(sample_id <= 0) {
        sample_id = 0;
        sample_type = 0;
    }

    view->banks[bank].cells[slot].sample_id = sample_id;
    view->banks[bank].cells[slot].sample_type = sample_type;
    gvr_sequence_bank_view_recount_bank(view, bank);
    gtk_widget_queue_draw(widget);
}

void gvr_sequence_bank_view_clear_bank(GtkWidget *widget, int bank)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget) || bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    memset(view->banks[bank].cells, 0, sizeof(view->banks[bank].cells));
    view->banks[bank].current = -1;
    view->banks[bank].size = 0;
    if(view->queued_bank == bank)
        view->queued_bank = -1;
    if(view->selected_bank == bank)
        gvr_sequence_bank_view_clear_selected_cells(view);
    gtk_widget_queue_draw(widget);
}

void gvr_sequence_bank_view_clear_all(GtkWidget *widget)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget))
        return;

    for(int bank = 0; bank < GVR_SEQUENCE_BANKS; bank++)
        gvr_sequence_bank_view_clear_bank(widget, bank);
}



void gvr_sequence_bank_view_copy_bank(GtkWidget *widget, int src_bank, int dst_bank)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget) ||
       src_bank < 0 || src_bank >= GVR_SEQUENCE_BANKS ||
       dst_bank < 0 || dst_bank >= GVR_SEQUENCE_BANKS ||
       src_bank == dst_bank)
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);

    memcpy(view->banks[dst_bank].cells,
           view->banks[src_bank].cells,
           sizeof(view->banks[dst_bank].cells));
    view->banks[dst_bank].current = view->banks[src_bank].current;
    view->banks[dst_bank].size = view->banks[src_bank].size;
    view->banks[dst_bank].revision++;

    if(view->selected_bank == src_bank)
        view->selected_bank = dst_bank;

    gtk_widget_queue_draw(widget);
}

void gvr_sequence_bank_view_set_bank_clipboard(GtkWidget *widget, int bank)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget) || bank < 0 || bank >= GVR_SEQUENCE_BANKS)
        return;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);

    view->bank_copy_valid = TRUE;
    view->bank_copy_source = bank;
    view->copy_valid = TRUE;
    view->copy_is_bank = TRUE;
    view->copy_bank = bank;
    view->copy_count = GVR_SEQUENCE_SLOTS;
    view->copy_base_slot = 0;

    for(int slot = 0; slot < GVR_SEQUENCE_SLOTS; slot++) {
        view->copy_cells[slot] = view->banks[bank].cells[slot];
        view->copy_offsets[slot] = slot;
    }
}

gboolean gvr_sequence_bank_view_get_bank_clipboard(GtkWidget *widget, int *bank)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget))
        return FALSE;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);

    if(!view->bank_copy_valid || view->bank_copy_source < 0 || view->bank_copy_source >= GVR_SEQUENCE_BANKS)
        return FALSE;

    if(bank)
        *bank = view->bank_copy_source;

    return TRUE;
}

gboolean gvr_sequence_bank_view_get_slot(GtkWidget *widget, int bank, int slot, int *sample_id, int *sample_type)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget) || bank < 0 || bank >= GVR_SEQUENCE_BANKS || slot < 0 || slot >= GVR_SEQUENCE_SLOTS)
        return FALSE;

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);

    if(sample_id)
        *sample_id = view->banks[bank].cells[slot].sample_id;

    if(sample_type)
        *sample_type = view->banks[bank].cells[slot].sample_type;

    return TRUE;
}


gboolean gvr_sequence_bank_view_get_cell_at(GtkWidget *widget, int x, int y, int *bank, int *slot, gboolean *header)
{
    if(!GVR_IS_SEQUENCE_BANK_VIEW(widget)) {
        if(bank) *bank = -1;
        if(slot) *slot = -1;
        if(header) *header = FALSE;
        return FALSE;
    }

    GvrSequenceBankView *view = GVR_SEQUENCE_BANK_VIEW(widget);
    GtkAllocation allocation;
    int b = -1;
    int s = -1;
    gboolean h = FALSE;

    gtk_widget_get_allocation(widget, &allocation);
    gvr_sequence_bank_view_layout(view, allocation.width, allocation.height);

    gboolean hit = gvr_sequence_bank_view_hit(view, (double)x, (double)y, &b, &s, &h);

    if(bank) *bank = b;
    if(slot) *slot = s;
    if(header) *header = h;

    return hit;
}
