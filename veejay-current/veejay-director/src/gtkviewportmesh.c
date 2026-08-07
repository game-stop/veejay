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
#include <math.h>
#include <string.h>
#include "gtkviewportmesh.h"

#define GVR_VIEWPORT_MESH_MIN_GRID 2
#define GVR_VIEWPORT_MESH_MAX_GRID 17
#define GVR_VIEWPORT_MESH_PADDING 18.0
#define GVR_VIEWPORT_MESH_POINT_RADIUS 5.0
#define GVR_VIEWPORT_MESH_SELECTED_RADIUS 8.0
#define GVR_VIEWPORT_MESH_DEFAULT_MARGIN 0.25
#define GVR_VIEWPORT_MESH_MIN_ZOOM 0.20
#define GVR_VIEWPORT_MESH_MAX_ZOOM 12.0

struct _GvrViewportMesh {
    GtkDrawingArea parent_instance;
    int columns;
    int rows;
    int point_count;
    int selected_point;
    int hover_point;
    int drag_point;
    double *points;
    GdkPixbuf *background;
    double content_aspect;
    double background_opacity;
    gboolean show_background;
    gboolean show_mesh;
    gboolean dragging;
    gboolean panning;
    double workspace_margin;
    double view_zoom;
    double view_pan_x;
    double view_pan_y;
    double pan_start_x;
    double pan_start_y;
    double pan_origin_x;
    double pan_origin_y;
};

G_DEFINE_TYPE(GvrViewportMesh, gvr_viewport_mesh, GTK_TYPE_DRAWING_AREA)

enum {
    SIGNAL_POINT_SELECTED,
    SIGNAL_POINT_CHANGED,
    SIGNAL_VIEW_CHANGED,
    SIGNAL_LAST
};

static guint mesh_signals[SIGNAL_LAST];

static double clampd(double value, double low, double high)
{
    return value < low ? low : (value > high ? high : value);
}

static gboolean mesh_valid_grid(int columns, int rows)
{
    return columns >= GVR_VIEWPORT_MESH_MIN_GRID &&
           columns <= GVR_VIEWPORT_MESH_MAX_GRID &&
           rows >= GVR_VIEWPORT_MESH_MIN_GRID &&
           rows <= GVR_VIEWPORT_MESH_MAX_GRID;
}

static double mesh_content_aspect(GvrViewportMesh *mesh)
{
    double aspect = mesh->content_aspect;
    if(aspect <= 0.0 && mesh->background && gdk_pixbuf_get_height(mesh->background) > 0)
        aspect = (double)gdk_pixbuf_get_width(mesh->background) /
                 (double)gdk_pixbuf_get_height(mesh->background);
    return aspect > 0.0 ? aspect : 4.0 / 3.0;
}

static void mesh_base_video_rect(GvrViewportMesh *mesh,
                                 double *x,
                                 double *y,
                                 double *width,
                                 double *height)
{
    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(mesh), &allocation);

    const double available_width = MAX(1.0, allocation.width - GVR_VIEWPORT_MESH_PADDING * 2.0);
    const double available_height = MAX(1.0, allocation.height - GVR_VIEWPORT_MESH_PADDING * 2.0);
    const double aspect = mesh_content_aspect(mesh);
    const double span = 1.0 + mesh->workspace_margin * 2.0;

    double video_height = available_height / span;
    double video_width = video_height * aspect;
    if(video_width * span > available_width) {
        video_width = available_width / span;
        video_height = video_width / aspect;
    }

    *width = MAX(1.0, video_width);
    *height = MAX(1.0, video_height);
    *x = ((double)allocation.width - *width) * 0.5;
    *y = ((double)allocation.height - *height) * 0.5;
}

static void mesh_video_rect(GvrViewportMesh *mesh,
                            double *x,
                            double *y,
                            double *width,
                            double *height)
{
    GtkAllocation allocation;
    double base_x, base_y, base_width, base_height;
    mesh_base_video_rect(mesh, &base_x, &base_y, &base_width, &base_height);
    gtk_widget_get_allocation(GTK_WIDGET(mesh), &allocation);

    (void)base_x;
    (void)base_y;
    *width = base_width * mesh->view_zoom;
    *height = base_height * mesh->view_zoom;
    *x = ((double)allocation.width - *width) * 0.5 + mesh->view_pan_x;
    *y = ((double)allocation.height - *height) * 0.5 + mesh->view_pan_y;
}

static void mesh_point_to_widget(GvrViewportMesh *mesh,
                                 int index,
                                 double *x,
                                 double *y)
{
    double rx, ry, rw, rh;
    mesh_video_rect(mesh, &rx, &ry, &rw, &rh);
    *x = rx + mesh->points[index * 2] * rw;
    *y = ry + mesh->points[index * 2 + 1] * rh;
}

static void mesh_widget_to_point(GvrViewportMesh *mesh,
                                 double widget_x,
                                 double widget_y,
                                 double *x,
                                 double *y)
{
    double rx, ry, rw, rh;
    mesh_video_rect(mesh, &rx, &ry, &rw, &rh);
    *x = (widget_x - rx) / rw;
    *y = (widget_y - ry) / rh;
}

static void mesh_zoom_at(GvrViewportMesh *mesh,
                         double factor,
                         double anchor_x,
                         double anchor_y)
{
    double old_x, old_y, old_width, old_height;
    mesh_video_rect(mesh, &old_x, &old_y, &old_width, &old_height);
    const double world_x = (anchor_x - old_x) / old_width;
    const double world_y = (anchor_y - old_y) / old_height;
    const double previous_zoom = mesh->view_zoom;

    mesh->view_zoom = clampd(mesh->view_zoom * factor,
                             GVR_VIEWPORT_MESH_MIN_ZOOM,
                             GVR_VIEWPORT_MESH_MAX_ZOOM);
    if(fabs(mesh->view_zoom - previous_zoom) < 1.0e-9)
        return;

    double new_x, new_y, new_width, new_height;
    mesh_video_rect(mesh, &new_x, &new_y, &new_width, &new_height);
    mesh->view_pan_x += anchor_x - (new_x + world_x * new_width);
    mesh->view_pan_y += anchor_y - (new_y + world_y * new_height);
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
    g_signal_emit(mesh, mesh_signals[SIGNAL_VIEW_CHANGED], 0);
}

static int mesh_nearest_point(GvrViewportMesh *mesh,
                              double x,
                              double y,
                              double max_distance)
{
    int nearest = -1;
    double nearest_sq = max_distance * max_distance;

    for(int i = 0; i < mesh->point_count; i++) {
        double px, py;
        mesh_point_to_widget(mesh, i, &px, &py);
        const double dx = px - x;
        const double dy = py - y;
        const double distance_sq = dx * dx + dy * dy;
        if(distance_sq <= nearest_sq) {
            nearest_sq = distance_sq;
            nearest = i;
        }
    }

    return nearest;
}

static gboolean mesh_affine_from_triangle(double sx0, double sy0,
                                           double sx1, double sy1,
                                           double sx2, double sy2,
                                           double dx0, double dy0,
                                           double dx1, double dy1,
                                           double dx2, double dy2,
                                           cairo_matrix_t *matrix)
{
    const double ux = sx1 - sx0;
    const double uy = sy1 - sy0;
    const double vx = sx2 - sx0;
    const double vy = sy2 - sy0;
    const double det = ux * vy - uy * vx;
    if(fabs(det) < 1.0e-12)
        return FALSE;

    const double dux = dx1 - dx0;
    const double duy = dy1 - dy0;
    const double dvx = dx2 - dx0;
    const double dvy = dy2 - dy0;
    const double inv = 1.0 / det;

    const double xx = (dux * vy - dvx * uy) * inv;
    const double xy = (dvx * ux - dux * vx) * inv;
    const double yx = (duy * vy - dvy * uy) * inv;
    const double yy = (dvy * ux - duy * vx) * inv;
    const double x0 = dx0 - xx * sx0 - xy * sy0;
    const double y0 = dy0 - yx * sx0 - yy * sy0;

    cairo_matrix_init(matrix, xx, yx, xy, yy, x0, y0);
    return TRUE;
}

static void mesh_paint_triangle(GvrViewportMesh *mesh,
                                cairo_t *cr,
                                double sx0, double sy0,
                                double sx1, double sy1,
                                double sx2, double sy2,
                                double dx0, double dy0,
                                double dx1, double dy1,
                                double dx2, double dy2)
{
    cairo_matrix_t matrix;
    if(!mesh_affine_from_triangle(sx0, sy0, sx1, sy1, sx2, sy2,
                                  dx0, dy0, dx1, dy1, dx2, dy2,
                                  &matrix))
        return;

    cairo_save(cr);
    cairo_move_to(cr, dx0, dy0);
    cairo_line_to(cr, dx1, dy1);
    cairo_line_to(cr, dx2, dy2);
    cairo_close_path(cr);
    cairo_clip(cr);
    cairo_transform(cr, &matrix);
    gdk_cairo_set_source_pixbuf(cr, mesh->background, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint_with_alpha(cr, mesh->background_opacity);
    cairo_restore(cr);
}

static gboolean mesh_draw_warped_background(GvrViewportMesh *mesh, cairo_t *cr)
{
    if(!mesh->background || mesh->columns < 2 || mesh->rows < 2 ||
       mesh->point_count != mesh->columns * mesh->rows)
        return FALSE;

    const int pixbuf_width = gdk_pixbuf_get_width(mesh->background);
    const int pixbuf_height = gdk_pixbuf_get_height(mesh->background);
    if(pixbuf_width <= 0 || pixbuf_height <= 0)
        return FALSE;

    for(int row = 0; row < mesh->rows - 1; row++) {
        const double sy0 = ((double)row / (mesh->rows - 1)) * pixbuf_height;
        const double sy1 = ((double)(row + 1) / (mesh->rows - 1)) * pixbuf_height;
        for(int column = 0; column < mesh->columns - 1; column++) {
            const double sx0 = ((double)column / (mesh->columns - 1)) * pixbuf_width;
            const double sx1 = ((double)(column + 1) / (mesh->columns - 1)) * pixbuf_width;
            const int p00 = row * mesh->columns + column;
            const int p10 = p00 + 1;
            const int p01 = p00 + mesh->columns;
            const int p11 = p01 + 1;
            double x00, y00, x10, y10, x01, y01, x11, y11;
            mesh_point_to_widget(mesh, p00, &x00, &y00);
            mesh_point_to_widget(mesh, p10, &x10, &y10);
            mesh_point_to_widget(mesh, p01, &x01, &y01);
            mesh_point_to_widget(mesh, p11, &x11, &y11);

            mesh_paint_triangle(mesh, cr,
                                sx0, sy0, sx1, sy0, sx1, sy1,
                                x00, y00, x10, y10, x11, y11);
            mesh_paint_triangle(mesh, cr,
                                sx0, sy0, sx1, sy1, sx0, sy1,
                                x00, y00, x11, y11, x01, y01);
        }
    }
    return TRUE;
}

static void mesh_draw_background(GvrViewportMesh *mesh, cairo_t *cr)
{
    double x, y, width, height;
    mesh_video_rect(mesh, &x, &y, &width, &height);

    cairo_set_source_rgb(cr, 0.153, 0.157, 0.184);
    cairo_paint(cr);

    cairo_rectangle(cr, x, y, width, height);
    cairo_set_source_rgb(cr, 0.176, 0.180, 0.212);
    cairo_fill(cr);

    if(mesh->show_background && mesh->background) {
        if(!mesh_draw_warped_background(mesh, cr)) {
            const int pixbuf_width = gdk_pixbuf_get_width(mesh->background);
            const int pixbuf_height = gdk_pixbuf_get_height(mesh->background);
            if(pixbuf_width > 0 && pixbuf_height > 0) {
                cairo_save(cr);
                cairo_rectangle(cr, x, y, width, height);
                cairo_clip(cr);
                cairo_translate(cr, x, y);
                cairo_scale(cr, width / (double)pixbuf_width,
                                 height / (double)pixbuf_height);
                gdk_cairo_set_source_pixbuf(cr, mesh->background, 0.0, 0.0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
                cairo_paint_with_alpha(cr, mesh->background_opacity);
                cairo_restore(cr);
            }
        }
    }
    else if(mesh->show_background) {
        cairo_save(cr);
        cairo_rectangle(cr, x, y, width, height);
        cairo_clip(cr);
        cairo_set_source_rgba(cr, 0.522, 0.537, 0.600, 0.62);
        cairo_select_font_face(cr, "Sans",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 15.0);
        const char *message = "Waiting for current video preview…";
        cairo_text_extents_t extents;
        cairo_text_extents(cr, message, &extents);
        cairo_move_to(cr,
                      x + (width - extents.width) * 0.5 - extents.x_bearing,
                      y + (height - extents.height) * 0.5 - extents.y_bearing);
        cairo_show_text(cr, message);
        cairo_restore(cr);
    }

    cairo_save(cr);
    const double dash[] = { 7.0, 5.0 };
    cairo_set_dash(cr, dash, 2, 0.0);
    cairo_rectangle(cr, x + 0.5, y + 0.5, MAX(1.0, width - 1.0), MAX(1.0, height - 1.0));
    cairo_set_source_rgba(cr, 0.522, 0.537, 0.600, 0.90);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_set_source_rgba(cr, 0.522, 0.537, 0.600, 0.76);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.0);
    cairo_move_to(cr, x + 5.0, y - 5.0);
    cairo_show_text(cr, "Output frame 0–100% · points may extend outside");
}

static void mesh_draw_lines(GvrViewportMesh *mesh, cairo_t *cr)
{
    cairo_set_source_rgb(cr, 0.353, 0.427, 0.549);
    cairo_set_line_width(cr, 1.4);

    for(int row = 0; row < mesh->rows; row++) {
        for(int column = 0; column < mesh->columns; column++) {
            const int index = row * mesh->columns + column;
            double x, y;
            mesh_point_to_widget(mesh, index, &x, &y);
            if(column == 0)
                cairo_move_to(cr, x, y);
            else
                cairo_line_to(cr, x, y);
        }
    }

    for(int column = 0; column < mesh->columns; column++) {
        for(int row = 0; row < mesh->rows; row++) {
            const int index = row * mesh->columns + column;
            double x, y;
            mesh_point_to_widget(mesh, index, &x, &y);
            if(row == 0)
                cairo_move_to(cr, x, y);
            else
                cairo_line_to(cr, x, y);
        }
    }

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.82);
    cairo_set_line_width(cr, 3.6);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr, 0.353, 0.427, 0.549);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);
}

static void mesh_draw_selected_axes(GvrViewportMesh *mesh, cairo_t *cr)
{
    if(mesh->selected_point < 0 || mesh->selected_point >= mesh->point_count)
        return;

    const int selected_row = mesh->selected_point / mesh->columns;
    const int selected_column = mesh->selected_point % mesh->columns;

    cairo_set_source_rgba(cr, 1.0, 0.52, 0.0, 0.58);
    cairo_set_line_width(cr, 2.0);

    for(int column = 0; column < mesh->columns; column++) {
        int index = selected_row * mesh->columns + column;
        double x, y;
        mesh_point_to_widget(mesh, index, &x, &y);
        if(column == 0)
            cairo_move_to(cr, x, y);
        else
            cairo_line_to(cr, x, y);
    }

    for(int row = 0; row < mesh->rows; row++) {
        int index = row * mesh->columns + selected_column;
        double x, y;
        mesh_point_to_widget(mesh, index, &x, &y);
        if(row == 0)
            cairo_move_to(cr, x, y);
        else
            cairo_line_to(cr, x, y);
    }

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.82);
    cairo_set_line_width(cr, 4.2);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 0.52, 0.0, 0.92);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);
}

static void mesh_draw_points(GvrViewportMesh *mesh, cairo_t *cr)
{
    for(int i = 0; i < mesh->point_count; i++) {
        double x, y;
        mesh_point_to_widget(mesh, i, &x, &y);
        const gboolean selected = i == mesh->selected_point;
        const gboolean hovered = i == mesh->hover_point;
        const double radius = selected ? GVR_VIEWPORT_MESH_SELECTED_RADIUS :
                              hovered ? GVR_VIEWPORT_MESH_POINT_RADIUS + 2.0 :
                                        GVR_VIEWPORT_MESH_POINT_RADIUS;

        cairo_arc(cr, x, y, radius, 0.0, 2.0 * G_PI);
        if(selected)
            cairo_set_source_rgb(cr, 1.0, 0.52, 0.0);
        else if(hovered)
            cairo_set_source_rgb(cr, 0.282, 0.659, 1.0);
        else
            cairo_set_source_rgb(cr, 0.353, 0.427, 0.549);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, selected ? 2.0 : 1.0);
        cairo_stroke(cr);

        if(selected) {
            cairo_move_to(cr, x - 13.0, y);
            cairo_line_to(cr, x + 13.0, y);
            cairo_move_to(cr, x, y - 13.0);
            cairo_line_to(cr, x, y + 13.0);
            cairo_set_source_rgba(cr, 1.0, 0.52, 0.0, 0.82);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }
    }
}

static gboolean gvr_viewport_mesh_draw(GtkWidget *widget, cairo_t *cr)
{
    GvrViewportMesh *mesh = GVR_VIEWPORT_MESH(widget);
    mesh_draw_background(mesh, cr);
    if(mesh->show_mesh && mesh->points && mesh->point_count > 0) {
        mesh_draw_lines(mesh, cr);
        mesh_draw_selected_axes(mesh, cr);
        mesh_draw_points(mesh, cr);
    }
    return TRUE;
}

static gboolean gvr_viewport_mesh_button_press(GtkWidget *widget,
                                                GdkEventButton *event)
{
    GvrViewportMesh *mesh = GVR_VIEWPORT_MESH(widget);

    if(event->button == GDK_BUTTON_MIDDLE ||
       (event->button == GDK_BUTTON_PRIMARY && (event->state & GDK_MOD1_MASK))) {
        mesh->panning = TRUE;
        mesh->pan_start_x = event->x;
        mesh->pan_start_y = event->y;
        mesh->pan_origin_x = mesh->view_pan_x;
        mesh->pan_origin_y = mesh->view_pan_y;
        gtk_widget_grab_focus(widget);
        return TRUE;
    }

    if(!mesh->show_mesh || event->button != GDK_BUTTON_PRIMARY || mesh->point_count <= 0)
        return FALSE;

    gtk_widget_grab_focus(widget);
    const int point = mesh_nearest_point(mesh, event->x, event->y, 24.0);
    if(point < 0)
        return FALSE;

    mesh->selected_point = point;
    g_signal_emit(mesh, mesh_signals[SIGNAL_POINT_SELECTED], 0, point + 1);

    if(event->state & GDK_CONTROL_MASK) {
        mesh->drag_point = -1;
        mesh->dragging = FALSE;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    mesh->drag_point = point;
    mesh->dragging = TRUE;
    double x, y;
    mesh_widget_to_point(mesh, event->x, event->y, &x, &y);
    mesh->points[point * 2] = x;
    mesh->points[point * 2 + 1] = y;
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean gvr_viewport_mesh_button_release(GtkWidget *widget,
                                                  GdkEventButton *event)
{
    GvrViewportMesh *mesh = GVR_VIEWPORT_MESH(widget);
    if(event->button == GDK_BUTTON_MIDDLE && mesh->panning) {
        mesh->panning = FALSE;
        return TRUE;
    }
    if(event->button != GDK_BUTTON_PRIMARY || !mesh->dragging)
        return FALSE;

    const int point = mesh->drag_point;
    mesh->dragging = FALSE;
    mesh->drag_point = -1;
    if(point >= 0 && point < mesh->point_count) {
        g_signal_emit(mesh,
                      mesh_signals[SIGNAL_POINT_CHANGED],
                      0,
                      point + 1,
                      mesh->points[point * 2],
                      mesh->points[point * 2 + 1]);
    }
    return TRUE;
}

static gboolean gvr_viewport_mesh_motion(GtkWidget *widget, GdkEventMotion *event)
{
    GvrViewportMesh *mesh = GVR_VIEWPORT_MESH(widget);

    if(mesh->panning) {
        mesh->view_pan_x = mesh->pan_origin_x + event->x - mesh->pan_start_x;
        mesh->view_pan_y = mesh->pan_origin_y + event->y - mesh->pan_start_y;
        gtk_widget_queue_draw(widget);
        g_signal_emit(mesh, mesh_signals[SIGNAL_VIEW_CHANGED], 0);
        return TRUE;
    }

    if(!mesh->show_mesh)
        return FALSE;

    if(mesh->dragging && mesh->drag_point >= 0) {
        double x, y;
        mesh_widget_to_point(mesh, event->x, event->y, &x, &y);
        mesh->points[mesh->drag_point * 2] = x;
        mesh->points[mesh->drag_point * 2 + 1] = y;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    const int hover = mesh_nearest_point(mesh, event->x, event->y, 14.0);
    if(hover != mesh->hover_point) {
        mesh->hover_point = hover;
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static gboolean gvr_viewport_mesh_leave(GtkWidget *widget, GdkEventCrossing *event)
{
    (void)event;
    GvrViewportMesh *mesh = GVR_VIEWPORT_MESH(widget);
    if(!mesh->dragging && !mesh->panning && mesh->hover_point != -1) {
        mesh->hover_point = -1;
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static gboolean gvr_viewport_mesh_key_press(GtkWidget *widget, GdkEventKey *event)
{
    GvrViewportMesh *mesh = GVR_VIEWPORT_MESH(widget);
    if(!mesh->show_mesh || mesh->selected_point < 0 || mesh->selected_point >= mesh->point_count)
        return FALSE;

    double dx = 0.0;
    double dy = 0.0;
    const double step = (event->state & GDK_SHIFT_MASK) ? 0.01 : 0.001;

    switch(event->keyval) {
        case GDK_KEY_Left:  dx = -step; break;
        case GDK_KEY_Right: dx = step; break;
        case GDK_KEY_Up:    dy = -step; break;
        case GDK_KEY_Down:  dy = step; break;
        case GDK_KEY_plus:
        case GDK_KEY_KP_Add:
            gvr_viewport_mesh_zoom_by(mesh, 1.20);
            return TRUE;
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract:
            gvr_viewport_mesh_zoom_by(mesh, 1.0 / 1.20);
            return TRUE;
        case GDK_KEY_0:
            gvr_viewport_mesh_fit_all(mesh);
            return TRUE;
        default: return FALSE;
    }

    const int point = mesh->selected_point;
    mesh->points[point * 2] += dx;
    mesh->points[point * 2 + 1] += dy;
    g_signal_emit(mesh,
                  mesh_signals[SIGNAL_POINT_CHANGED],
                  0,
                  point + 1,
                  mesh->points[point * 2],
                  mesh->points[point * 2 + 1]);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean gvr_viewport_mesh_scroll(GtkWidget *widget, GdkEventScroll *event)
{
    GvrViewportMesh *mesh = GVR_VIEWPORT_MESH(widget);
    double factor = 1.0;

    if(event->direction == GDK_SCROLL_UP)
        factor = 1.15;
    else if(event->direction == GDK_SCROLL_DOWN)
        factor = 1.0 / 1.15;
    else if(event->direction == GDK_SCROLL_SMOOTH)
        factor = event->delta_y < 0.0 ? 1.08 : (event->delta_y > 0.0 ? 1.0 / 1.08 : 1.0);
    else
        return FALSE;

    mesh_zoom_at(mesh, factor, event->x, event->y);
    return TRUE;
}

static void gvr_viewport_mesh_finalize(GObject *object)
{
    GvrViewportMesh *mesh = GVR_VIEWPORT_MESH(object);
    g_free(mesh->points);
    mesh->points = NULL;
    if(mesh->background) {
        g_object_unref(mesh->background);
        mesh->background = NULL;
    }
    G_OBJECT_CLASS(gvr_viewport_mesh_parent_class)->finalize(object);
}

static void gvr_viewport_mesh_class_init(GvrViewportMeshClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->finalize = gvr_viewport_mesh_finalize;
    widget_class->draw = gvr_viewport_mesh_draw;
    widget_class->button_press_event = gvr_viewport_mesh_button_press;
    widget_class->button_release_event = gvr_viewport_mesh_button_release;
    widget_class->motion_notify_event = gvr_viewport_mesh_motion;
    widget_class->leave_notify_event = gvr_viewport_mesh_leave;
    widget_class->key_press_event = gvr_viewport_mesh_key_press;
    widget_class->scroll_event = gvr_viewport_mesh_scroll;

    mesh_signals[SIGNAL_POINT_SELECTED] =
        g_signal_new("point-selected",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     NULL,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    mesh_signals[SIGNAL_POINT_CHANGED] =
        g_signal_new("point-changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     NULL,
                     G_TYPE_NONE,
                     3,
                     G_TYPE_INT,
                     G_TYPE_DOUBLE,
                     G_TYPE_DOUBLE);

    mesh_signals[SIGNAL_VIEW_CHANGED] =
        g_signal_new("view-changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     NULL,
                     G_TYPE_NONE,
                     0);
}

static void gvr_viewport_mesh_init(GvrViewportMesh *mesh)
{
    mesh->columns = 2;
    mesh->rows = 2;
    mesh->selected_point = 0;
    mesh->hover_point = -1;
    mesh->drag_point = -1;
    /* Prefer the preview image's native aspect until the controller supplies
     * the selected output's authoritative dimensions. */
    mesh->content_aspect = 0.0;
    mesh->background_opacity = 1.0;
    mesh->show_background = TRUE;
    mesh->show_mesh = TRUE;
    mesh->workspace_margin = GVR_VIEWPORT_MESH_DEFAULT_MARGIN;
    mesh->view_zoom = 1.0;
    mesh->view_pan_x = 0.0;
    mesh->view_pan_y = 0.0;
    gtk_widget_set_can_focus(GTK_WIDGET(mesh), TRUE);
    gtk_widget_add_events(GTK_WIDGET(mesh),
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_LEAVE_NOTIFY_MASK |
                          GDK_KEY_PRESS_MASK |
                          GDK_SCROLL_MASK);
    gtk_widget_set_size_request(GTK_WIDGET(mesh), 640, 480);
    gvr_viewport_mesh_reset_regular(mesh, 0.0);
}

GtkWidget *gvr_viewport_mesh_new(void)
{
    return g_object_new(GVR_TYPE_VIEWPORT_MESH, NULL);
}

void gvr_viewport_mesh_set_mesh(GvrViewportMesh *mesh,
                                int columns,
                                int rows,
                                const double *points_xy,
                                int selected_point)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    if(!mesh_valid_grid(columns, rows))
        return;

    const int point_count = columns * rows;
    double *points = g_new(double, (gsize)point_count * 2u);
    if(points_xy)
        memcpy(points, points_xy, sizeof(double) * (gsize)point_count * 2u);
    else {
        for(int row = 0; row < rows; row++) {
            for(int column = 0; column < columns; column++) {
                const int index = row * columns + column;
                points[index * 2] = (double)column / (double)(columns - 1);
                points[index * 2 + 1] = (double)row / (double)(rows - 1);
            }
        }
    }

    g_free(mesh->points);
    mesh->points = points;
    mesh->columns = columns;
    mesh->rows = rows;
    mesh->point_count = point_count;
    mesh->selected_point = CLAMP(selected_point - 1, 0, point_count - 1);
    mesh->hover_point = -1;
    mesh->drag_point = -1;
    mesh->dragging = FALSE;
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
}

void gvr_viewport_mesh_set_selected_point(GvrViewportMesh *mesh,
                                          int selected_point)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    if(selected_point < 1 || selected_point > mesh->point_count)
        return;
    mesh->selected_point = selected_point - 1;
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
}

int gvr_viewport_mesh_get_selected_point(GvrViewportMesh *mesh)
{
    g_return_val_if_fail(GVR_IS_VIEWPORT_MESH(mesh), 0);
    return mesh->selected_point + 1;
}

int gvr_viewport_mesh_get_columns(GvrViewportMesh *mesh)
{
    g_return_val_if_fail(GVR_IS_VIEWPORT_MESH(mesh), 0);
    return mesh->columns;
}

int gvr_viewport_mesh_get_rows(GvrViewportMesh *mesh)
{
    g_return_val_if_fail(GVR_IS_VIEWPORT_MESH(mesh), 0);
    return mesh->rows;
}

int gvr_viewport_mesh_get_point_count(GvrViewportMesh *mesh)
{
    g_return_val_if_fail(GVR_IS_VIEWPORT_MESH(mesh), 0);
    return mesh->point_count;
}

gboolean gvr_viewport_mesh_is_dragging(GvrViewportMesh *mesh)
{
    g_return_val_if_fail(GVR_IS_VIEWPORT_MESH(mesh), FALSE);
    return mesh->dragging;
}

gboolean gvr_viewport_mesh_get_point(GvrViewportMesh *mesh,
                                     int point,
                                     double *x,
                                     double *y)
{
    g_return_val_if_fail(GVR_IS_VIEWPORT_MESH(mesh), FALSE);
    if(point < 1 || point > mesh->point_count || !x || !y)
        return FALSE;
    const int index = point - 1;
    *x = mesh->points[index * 2];
    *y = mesh->points[index * 2 + 1];
    return TRUE;
}

void gvr_viewport_mesh_set_point(GvrViewportMesh *mesh,
                                 int point,
                                 double x,
                                 double y,
                                 gboolean emit_change)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    if(point < 1 || point > mesh->point_count)
        return;
    const int index = point - 1;
    mesh->points[index * 2] = x;
    mesh->points[index * 2 + 1] = y;
    mesh->selected_point = index;
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
    if(emit_change)
        g_signal_emit(mesh,
                      mesh_signals[SIGNAL_POINT_CHANGED],
                      0,
                      point,
                      mesh->points[index * 2],
                      mesh->points[index * 2 + 1]);
}

void gvr_viewport_mesh_reset_regular(GvrViewportMesh *mesh,
                                     double inset)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    inset = clampd(inset, 0.0, 0.45);
    const double span = 1.0 - inset * 2.0;
    const int columns = mesh->columns > 1 ? mesh->columns : 2;
    const int rows = mesh->rows > 1 ? mesh->rows : 2;
    const int point_count = columns * rows;

    if(mesh->point_count != point_count || !mesh->points) {
        g_free(mesh->points);
        mesh->points = g_new0(double, (gsize)point_count * 2u);
        mesh->point_count = point_count;
    }

    for(int row = 0; row < rows; row++) {
        for(int column = 0; column < columns; column++) {
            const int index = row * columns + column;
            mesh->points[index * 2] = inset + span * ((double)column / (double)(columns - 1));
            mesh->points[index * 2 + 1] = inset + span * ((double)row / (double)(rows - 1));
        }
    }

    mesh->selected_point = CLAMP(mesh->selected_point, 0, point_count - 1);
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
}
void gvr_viewport_mesh_set_background_pixbuf(GvrViewportMesh *mesh,
                                             GdkPixbuf *pixbuf)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    if(pixbuf)
        g_object_ref(pixbuf);
    if(mesh->background)
        g_object_unref(mesh->background);
    mesh->background = pixbuf;
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
}

void gvr_viewport_mesh_set_view(GvrViewportMesh *mesh,
                                gboolean show_background,
                                gboolean show_mesh)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    mesh->show_background = show_background;
    mesh->show_mesh = show_mesh;
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
}

void gvr_viewport_mesh_set_background_opacity(GvrViewportMesh *mesh,
                                               double opacity)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    mesh->background_opacity = clampd(opacity, 0.0, 1.0);
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
}

void gvr_viewport_mesh_set_content_aspect(GvrViewportMesh *mesh,
                                          double aspect)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    if(aspect > 0.0)
        mesh->content_aspect = aspect;
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
}


void gvr_viewport_mesh_set_workspace_margin(GvrViewportMesh *mesh, double margin)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    mesh->workspace_margin = clampd(margin, 0.0, 2.0);
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
    g_signal_emit(mesh, mesh_signals[SIGNAL_VIEW_CHANGED], 0);
}

void gvr_viewport_mesh_fit_workspace(GvrViewportMesh *mesh)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    mesh->view_zoom = 1.0;
    mesh->view_pan_x = 0.0;
    mesh->view_pan_y = 0.0;
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
    g_signal_emit(mesh, mesh_signals[SIGNAL_VIEW_CHANGED], 0);
}

void gvr_viewport_mesh_fit_output(GvrViewportMesh *mesh)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    mesh->view_zoom = 1.0 + mesh->workspace_margin * 2.0;
    mesh->view_pan_x = 0.0;
    mesh->view_pan_y = 0.0;
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
    g_signal_emit(mesh, mesh_signals[SIGNAL_VIEW_CHANGED], 0);
}

void gvr_viewport_mesh_center_view(GvrViewportMesh *mesh)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    mesh->view_pan_x = 0.0;
    mesh->view_pan_y = 0.0;
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
    g_signal_emit(mesh, mesh_signals[SIGNAL_VIEW_CHANGED], 0);
}

void gvr_viewport_mesh_zoom_by(GvrViewportMesh *mesh, double factor)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));
    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(mesh), &allocation);
    mesh_zoom_at(mesh, factor, allocation.width * 0.5, allocation.height * 0.5);
}

void gvr_viewport_mesh_fit_all(GvrViewportMesh *mesh)
{
    g_return_if_fail(GVR_IS_VIEWPORT_MESH(mesh));

    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = 1.0;
    double max_y = 1.0;
    for(int i = 0; i < mesh->point_count; i++) {
        min_x = fmin(min_x, mesh->points[i * 2]);
        min_y = fmin(min_y, mesh->points[i * 2 + 1]);
        max_x = fmax(max_x, mesh->points[i * 2]);
        max_y = fmax(max_y, mesh->points[i * 2 + 1]);
    }

    double world_width = MAX(0.01, max_x - min_x);
    double world_height = MAX(0.01, max_y - min_y);
    const double pad_x = MAX(0.05, world_width * 0.10);
    const double pad_y = MAX(0.05, world_height * 0.10);
    min_x -= pad_x;
    max_x += pad_x;
    min_y -= pad_y;
    max_y += pad_y;
    world_width = max_x - min_x;
    world_height = max_y - min_y;

    GtkAllocation allocation;
    double base_x, base_y, base_width, base_height;
    gtk_widget_get_allocation(GTK_WIDGET(mesh), &allocation);
    mesh_base_video_rect(mesh, &base_x, &base_y, &base_width, &base_height);
    (void)base_x;
    (void)base_y;
    const double available_width = MAX(1.0, allocation.width - GVR_VIEWPORT_MESH_PADDING * 2.0);
    const double available_height = MAX(1.0, allocation.height - GVR_VIEWPORT_MESH_PADDING * 2.0);
    mesh->view_zoom = clampd(fmin(available_width / (base_width * world_width),
                                  available_height / (base_height * world_height)),
                             GVR_VIEWPORT_MESH_MIN_ZOOM,
                             GVR_VIEWPORT_MESH_MAX_ZOOM);

    const double center_x = (min_x + max_x) * 0.5;
    const double center_y = (min_y + max_y) * 0.5;
    mesh->view_pan_x = -(center_x - 0.5) * base_width * mesh->view_zoom;
    mesh->view_pan_y = -(center_y - 0.5) * base_height * mesh->view_zoom;
    gtk_widget_queue_draw(GTK_WIDGET(mesh));
    g_signal_emit(mesh, mesh_signals[SIGNAL_VIEW_CHANGED], 0);
}

double gvr_viewport_mesh_get_zoom(GvrViewportMesh *mesh)
{
    g_return_val_if_fail(GVR_IS_VIEWPORT_MESH(mesh), 1.0);
    return mesh->view_zoom;
}
