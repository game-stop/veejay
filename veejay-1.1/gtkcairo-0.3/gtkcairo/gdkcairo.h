/* gdkcairo - replacing a gdkwindow with a cairo surface
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

#ifndef GDKCAIRO_H
#define GDKCAIRO_H

#include <gtk/gtk.h>
#include <cairo.h>

#ifdef USE_GL
#ifndef CAIRO_HAS_GLITZ_SURFACE
#undef USE_GL
#endif
#endif

#ifdef USE_GL
#include <glitz-glx.h>
#endif

typedef enum
{
  GDKCAIRO_BACKEND_IMAGE,
  GDKCAIRO_BACKEND_XLIB,
  GDKCAIRO_BACKEND_GL,
  GDKCAIRO_BACKEND_NONE
}
gdkcairo_backend;

typedef struct gdkcairo_t
{
  GtkWidget *widget;
  cairo_t  *cr;
  gdkcairo_backend backend;

#ifdef USE_GL
  glitz_surface_t *glitz_surface;
#endif
} gdkcairo_t;

gdkcairo_t *gdkcairo_new (GtkWidget *widget);

void        gdkcairo_destroy (gdkcairo_t *self);

void        gdkcairo_realize (gdkcairo_t *self);

void        gdkcairo_size_allocate (gdkcairo_t *self,
                                    gint        x,
                                    gint        y,
                                    gint        width,
                                    gint        height);
gint        gdkcairo_expose (gdkcairo_t     *self,
                             GdkEventExpose *event);

#endif /* GDKCAIRO_H */
