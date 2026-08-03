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
#ifndef GVR_VIEWPORT_MESH_H
#define GVR_VIEWPORT_MESH_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _GvrViewportMesh GvrViewportMesh;
typedef struct _GvrViewportMeshClass GvrViewportMeshClass;

struct _GvrViewportMeshClass {
    GtkDrawingAreaClass parent_class;
};

#define GVR_TYPE_VIEWPORT_MESH            (gvr_viewport_mesh_get_type())
#define GVR_VIEWPORT_MESH(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_VIEWPORT_MESH, GvrViewportMesh))
#define GVR_VIEWPORT_MESH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GVR_TYPE_VIEWPORT_MESH, GvrViewportMeshClass))
#define GVR_IS_VIEWPORT_MESH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_VIEWPORT_MESH))
#define GVR_IS_VIEWPORT_MESH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GVR_TYPE_VIEWPORT_MESH))
#define GVR_VIEWPORT_MESH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GVR_TYPE_VIEWPORT_MESH, GvrViewportMeshClass))

GType gvr_viewport_mesh_get_type(void) G_GNUC_CONST;
GtkWidget *gvr_viewport_mesh_new(void);

void gvr_viewport_mesh_set_mesh(GvrViewportMesh *mesh,
                                int columns,
                                int rows,
                                const double *points_xy,
                                int selected_point);
void gvr_viewport_mesh_set_selected_point(GvrViewportMesh *mesh,
                                          int selected_point);
int gvr_viewport_mesh_get_selected_point(GvrViewportMesh *mesh);
int gvr_viewport_mesh_get_columns(GvrViewportMesh *mesh);
int gvr_viewport_mesh_get_rows(GvrViewportMesh *mesh);
int gvr_viewport_mesh_get_point_count(GvrViewportMesh *mesh);
gboolean gvr_viewport_mesh_is_dragging(GvrViewportMesh *mesh);
gboolean gvr_viewport_mesh_get_point(GvrViewportMesh *mesh,
                                     int point,
                                     double *x,
                                     double *y);
void gvr_viewport_mesh_set_point(GvrViewportMesh *mesh,
                                 int point,
                                 double x,
                                 double y,
                                 gboolean emit_change);
void gvr_viewport_mesh_reset_regular(GvrViewportMesh *mesh,
                                     double inset);
void gvr_viewport_mesh_set_background_pixbuf(GvrViewportMesh *mesh,
                                             GdkPixbuf *pixbuf);
void gvr_viewport_mesh_set_view(GvrViewportMesh *mesh,
                                gboolean show_background,
                                gboolean show_mesh);
void gvr_viewport_mesh_set_background_opacity(GvrViewportMesh *mesh,
                                               double opacity);
void gvr_viewport_mesh_set_content_aspect(GvrViewportMesh *mesh,
                                          double aspect);
void gvr_viewport_mesh_set_workspace_margin(GvrViewportMesh *mesh, double margin);
void gvr_viewport_mesh_fit_workspace(GvrViewportMesh *mesh);
void gvr_viewport_mesh_fit_output(GvrViewportMesh *mesh);
void gvr_viewport_mesh_fit_all(GvrViewportMesh *mesh);
void gvr_viewport_mesh_center_view(GvrViewportMesh *mesh);
void gvr_viewport_mesh_zoom_by(GvrViewportMesh *mesh, double factor);
double gvr_viewport_mesh_get_zoom(GvrViewportMesh *mesh);

G_END_DECLS

#endif
