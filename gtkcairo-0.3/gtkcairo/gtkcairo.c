/* gtkcairo - cairo drawing widget for gtk+
 *
 * Copyright © 2003, 2004  Carl D. Worth <carl@theworths.org>
 *                         Evan Martin   <martine@danga.com>
 *                         Øyvind Kolås  <oeyvindk@hig.no>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */

#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gdk/gdkx.h>

#include "gdkcairo.h"
#include "gtkcairo.h"

enum
{
  PAINT,
  LAST_SIGNAL
};

static void gtk_cairo_class_init (GtkCairoClass * klass);

static void gtk_cairo_init (GtkCairo *gtkcairo);

static void gtk_cairo_destroy (GtkObject *object);

static void gtk_cairo_realize (GtkWidget *widget);

static void
gtk_cairo_size_allocate (GtkWidget *widget, GtkAllocation * allocation);
static gint gtk_cairo_expose (GtkWidget *widget, GdkEventExpose *event);

static GtkWidgetClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

/*FIXME: make the cairo object a property as well,. and deprecate the get_cairo function */

GType
gtk_cairo_get_type (void)
{
  static GType gtk_cairo_type = 0;

  if (!gtk_cairo_type)
    {
      static const GTypeInfo gtk_cairo_info = {
        sizeof (GtkCairoClass),
        NULL,
        NULL,
        (GClassInitFunc) gtk_cairo_class_init,
        NULL,
        NULL,
        sizeof (GtkCairo),
        0,
        (GInstanceInitFunc) gtk_cairo_init,
      };

      gtk_cairo_type = g_type_register_static (GTK_TYPE_WIDGET, "GtkCairo",
                                               &gtk_cairo_info, 0);
    }

  return gtk_cairo_type;
}

static void
gtk_cairo_class_init (GtkCairoClass * class)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;

  object_class = (GtkObjectClass *) class;
  widget_class = (GtkWidgetClass *) class;

  parent_class = gtk_type_class (GTK_TYPE_WIDGET);

  object_class->destroy = gtk_cairo_destroy;

  widget_class->realize = gtk_cairo_realize;
  widget_class->expose_event = gtk_cairo_expose;
  widget_class->size_allocate = gtk_cairo_size_allocate;

  signals[PAINT] = g_signal_new ("paint",
                                 GTK_TYPE_CAIRO,
                                 G_SIGNAL_RUN_LAST,
                                 G_STRUCT_OFFSET (GtkCairoClass, paint),
                                 NULL, NULL,
                                 g_cclosure_marshal_VOID__POINTER,
                                 G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void
gtk_cairo_init (GtkCairo *gtkcairo)
{
  gtkcairo->gdkcairo = gdkcairo_new (GTK_WIDGET (gtkcairo));
}

GtkWidget *
gtk_cairo_new (void)
{
  GtkWidget *gtkcairo;
  gtkcairo = GTK_WIDGET (g_object_new (GTK_TYPE_CAIRO, NULL));

  gtk_widget_queue_draw (GTK_WIDGET (gtkcairo));

  return gtkcairo;
}

static void
gtk_cairo_destroy (GtkObject *object)
{
  GtkCairo *gtkcairo;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_CAIRO (object));

  gtkcairo = GTK_CAIRO (object);

  gdkcairo_destroy (gtkcairo->gdkcairo);

  if (GTK_OBJECT_CLASS (parent_class)->destroy)
    (*GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

static void
gtk_cairo_realize (GtkWidget *widget)
{
  GtkCairo *gtkcairo;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_CAIRO (widget));

  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);
  gtkcairo = GTK_CAIRO (widget);

  gdkcairo_realize (gtkcairo->gdkcairo);
}

static void
gtk_cairo_size_allocate (GtkWidget     *widget,
                         GtkAllocation *allocation)
{
  GtkCairo *gtkcairo;
  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_CAIRO (widget));
  g_return_if_fail (allocation != NULL);

  gtkcairo = GTK_CAIRO (widget);

  widget->allocation = *allocation;

  gdkcairo_size_allocate (gtkcairo->gdkcairo,
                          allocation->x, allocation->y,
                          allocation->width, allocation->height);
}

static gint
gtk_cairo_expose (GtkWidget      *widget,
                  GdkEventExpose *event)
{
  GtkCairo *gtkcairo;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_CAIRO (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  gtkcairo = GTK_CAIRO (widget);

  gdkcairo_expose (gtkcairo->gdkcairo, event);
  return FALSE;
}

cairo_t  *
gtk_cairo_get_cairo (GtkCairo *gtkcairo)
{
  g_return_val_if_fail (gtkcairo != NULL, NULL);
  g_return_val_if_fail (GTK_IS_CAIRO (gtkcairo), NULL);
  return ((gdkcairo_t *) gtkcairo->gdkcairo)->cr;
}

void
gtk_cairo_set_gdk_color (cairo_t  *cr,
                         GdkColor *color)
{
  double    red, green, blue;

  red = color->red / 65535.0;
  green = color->green / 65535.0;
  blue = color->blue / 65535.0;

  cairo_set_source_rgb (cr, red, green, blue);
}

int
gtk_cairo_backend_is_gl (GtkCairo *gtkcairo)
{
  if (((gdkcairo_t *) gtkcairo->gdkcairo)->backend == GDKCAIRO_BACKEND_GL)
    return 1;
  return 0;
}

#if 0
/* FIXME: premultiply the buffer, but who should own it?
 */

cairo_surface_t *
gtk_cairo_surface_create_for_gdk_pixbuf (const GdkPixbuf * pixbuf)
{
  cairo_surface_t *self;
  char     *data;
  cairo_format_t format;
  int       width;
  int       height;
  int       stride;

  if (!pixbuf)
    return NULL;
  data = gdk_pixbuf_get_pixels (pixbuf);
  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  format = CAIRO_FORMAT_ARGB32;
  stride = gdk_pixbuf_get_rowstride (pixbuf);

  self = cairo_surface_create_for_image (data, format, width, height, stride);
  return self;
}
#endif

/* vim: set ts=4 sw=4 noet : */
