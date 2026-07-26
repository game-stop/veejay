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
#ifndef GTK_SHAPE_SELECTOR_H
#define GTK_SHAPE_SELECTOR_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GVR_TYPE_SHAPE_SELECTOR            (gvr_shape_selector_get_type())
#define GVR_SHAPE_SELECTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_SHAPE_SELECTOR, GvrShapeSelector))
#define GVR_IS_SHAPE_SELECTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_SHAPE_SELECTOR))
#define GVR_SHAPE_SELECTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GVR_TYPE_SHAPE_SELECTOR, GvrShapeSelectorClass))
#define GVR_IS_SHAPE_SELECTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GVR_TYPE_SHAPE_SELECTOR))
#define GVR_SHAPE_SELECTOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GVR_TYPE_SHAPE_SELECTOR, GvrShapeSelectorClass))

typedef struct _GvrShapeSelector GvrShapeSelector;
typedef struct _GvrShapeSelectorClass GvrShapeSelectorClass;

GType      gvr_shape_selector_get_type(void);
GtkWidget *gvr_shape_selector_new(void);

void gvr_shape_selector_set_catalog(GtkWidget *widget,
                                    const char *const *names,
                                    guint count,
                                    gboolean allow_random);
void gvr_shape_selector_set_active(GtkWidget *widget, int shape);
int  gvr_shape_selector_get_active(GtkWidget *widget);
const char *gvr_shape_selector_get_active_name(GtkWidget *widget);

/*
 * Signal:
 *
 * "shape-changed"
 *   void callback(GtkWidget *widget, int shape, gpointer user_data)
 */

G_END_DECLS

#endif
