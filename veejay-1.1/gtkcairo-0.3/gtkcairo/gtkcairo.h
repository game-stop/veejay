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

#ifndef GTK_CAIRO_H
#define GTK_CAIRO_H

#include <gtk/gtkwidget.h>
#include <cairo.h>

#ifdef __cplusplus
extern    "C"
{
#endif                          /* __cplusplus */

#define GTK_TYPE_CAIRO	(gtk_cairo_get_type())
#define GTK_CAIRO(obj)	GTK_CHECK_CAST (obj, GTK_TYPE_CAIRO, GtkCairo)
#define GTK_CAIRO_CLASS(klass) GTK_CHECK_CLASS_CAST (klass, GTK_TYPE_CAIRO, GtkCairoClass)
#define GTK_IS_CAIRO(obj)	GTK_CHECK_TYPE (obj, GTK_TYPE_CAIRO)

  typedef struct _GtkCairo GtkCairo;
  typedef struct _GtkCairoClass GtkCairoClass;

#define gdkcairo_t void

  struct _GtkCairo
  {
    GtkWidget widget;
    gdkcairo_t *gdkcairo;
  };

#undef gdkcairo_t

  struct _GtkCairoClass
  {
    GtkWidgetClass parent_class;
    void      (*paint) (GtkCairo *, cairo_t *c);
  };

  GType     gtk_cairo_get_type (void);

  GtkWidget *gtk_cairo_new (void);

  cairo_t  *gtk_cairo_get_cairo (GtkCairo *gtkcairo);


/* convenience function to set the current cairo color
 * from a GdkColor
 */
  void      gtk_cairo_set_gdk_color (cairo_t  *cr,
                                     GdkColor *color);

#ifdef __cplusplus
}
#endif                          /* __cplusplus */

#endif                          /* GTK_CAIRO_H */
