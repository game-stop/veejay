/* Copyright (C) 2016 Benoit Touchette
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation version
 * 2.1 of the License.
 */

#ifndef __GTK3_CURVE__H__
#define __GTK3_CURVE__H__

#include <gtk/gtk.h>

#define GTK3_TYPE_CURVE                  (gtk3_curve_get_type ())
#define GTK3_CURVE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK3_TYPE_CURVE, Gtk3Curve))
#define GTK3_IS_CURVE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK3_TYPE_CURVE))
#define GTK3_CURVE_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST  ((klass), GTK3_TYPE_CURVE, Gtk3CurveClass))
#define GTK3_IS_CURVE_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE  ((klass), GTK3_TYPE_CURVE))
#define GTK3_CURVE_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS  ((obj), GTK3_TYPE_CURVE, Gtk3CurveClass))

#define GTK3_TYPE_CURVE_TYPE             (gtk3_curve_type_get_type ())
#define GTK3_CURVE_LIVE_TRACE_MAX        10

typedef enum
{
  GTK3_CURVE_GRID_MICRO,
  GTK3_CURVE_GRID_SMALL,
  GTK3_CURVE_GRID_MEDIUM,
  GTK3_CURVE_GRID_LARGE,
  GTK3_CURVE_GRID_XLARGE
} Gtk3CurveGridSize;

typedef enum
{
  GTK3_CURVE_TYPE_LINEAR,
  GTK3_CURVE_TYPE_SPLINE,
  GTK3_CURVE_TYPE_FREE
} Gtk3CurveType;

typedef struct _Gtk3Curve           Gtk3Curve;
typedef struct _Gtk3CurveClass      Gtk3CurveClass;
typedef struct _Gtk3CurvePrivate    Gtk3CurvePrivate;
typedef struct _Gtk3CurveColor      Gtk3CurveColor;
typedef struct _Gtk3CurveData       Gtk3CurveData;
typedef struct _Gtk3CurveVector     Gtk3CurveVector;
typedef struct _Gtk3CurvePoint      Gtk3CurvePoint;

struct _Gtk3CurvePoint
{
  gint x;
  gint y;
};

struct _Gtk3CurveColor
{
  gfloat red;
  gfloat green;
  gfloat blue;
  gfloat alpha;
};

struct _Gtk3CurveVector
{
  gfloat x;
  gfloat y;
};

struct _Gtk3CurveData
{
  gchar            *description;
  Gtk3CurveType     curve_type;
  gint              n_points;
  Gtk3CurvePoint   *d_point;
  gint              n_cpoints;
  Gtk3CurveVector  *d_cpoints;
};

struct _Gtk3Curve
{
  GtkWidget widget;
  Gtk3CurvePrivate *priv;
};

struct _Gtk3CurveClass
{
  GtkWidgetClass parent_class;
  void (* curve_type_changed) (Gtk3Curve *curve);
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};

GType gtk3_curve_type_get_type (void);
GType gtk3_curve_get_type (void) G_GNUC_CONST;
GtkWidget*  gtk3_curve_new (void);

void gtk3_curve_reset                             (GtkWidget         *widget);
void gtk3_curve_set_gamma                         (GtkWidget         *widget,
                                                   gfloat             gamma_);
void gtk3_curve_set_position                      (GtkWidget         *widget,
                                                   gdouble            position);
void gtk3_curve_set_grid_resolution               (GtkWidget         *widget,
                                                   gint               grid_resolution);
void gtk3_curve_set_x_lo                          (GtkWidget         *widget,
                                                   gfloat             min_x);
void gtk3_curve_set_x_hi                          (GtkWidget         *widget,
                                                   gfloat             max_x);
void gtk3_curve_set_range                         (GtkWidget         *widget,
                                                   gfloat             min_x,
                                                   gfloat             max_x,
                                                   gfloat             min_y,
                                                   gfloat             max_y);
void gtk3_curve_get_vector                        (GtkWidget         *widget,
                                                   gint               veclen,
                                                   gfloat             vector[]);
void gtk3_curve_set_vector                        (GtkWidget         *widget,
                                                   gint               veclen,
                                                   gfloat             vector[]);
void gtk3_curve_set_curve_type                    (GtkWidget         *widget,
                                                   Gtk3CurveType      type);

void gtk3_curve_set_color_background              (GtkWidget         *widget,
                                                   Gtk3CurveColor     color);
void gtk3_curve_set_color_grid                    (GtkWidget         *widget,
                                                   Gtk3CurveColor     color);
void gtk3_curve_set_color_curve                   (GtkWidget         *widget,
                                                   Gtk3CurveColor     color);
void gtk3_curve_set_color_cpoint                  (GtkWidget         *widget,
                                                   Gtk3CurveColor     color);

void gtk3_curve_set_color_background_rgba         (GtkWidget         *widget,
                                                   gfloat             r,
                                                   gfloat             g,
                                                   gfloat             b,
                                                   gfloat             a);
void gtk3_curve_set_color_grid_rgba               (GtkWidget         *widget,
                                                   gfloat             r,
                                                   gfloat             g,
                                                   gfloat             b,
                                                   gfloat             a);
void gtk3_curve_set_color_curve_rgba              (GtkWidget         *widget,
                                                   gfloat             r,
                                                   gfloat             g,
                                                   gfloat             b,
                                                   gfloat             a);
void gtk3_curve_set_color_cpoint_rgba             (GtkWidget         *widget,
                                                   gfloat             r,
                                                   gfloat             g,
                                                   gfloat             b,
                                                   gfloat             a);

Gtk3CurveType  gtk3_curve_get_curve_type          (GtkWidget         *widget);
Gtk3CurveColor gtk3_curve_get_color_background    (GtkWidget          *widget);
Gtk3CurveColor gtk3_curve_get_color_grid          (GtkWidget          *widget);
Gtk3CurveColor gtk3_curve_get_color_curve         (GtkWidget          *widget);
Gtk3CurveColor gtk3_curve_get_color_cpoint        (GtkWidget          *widget);

void gtk3_curve_set_use_theme_background          (GtkWidget          *widget,
                                                   gboolean            use);
gboolean gtk3_curve_get_use_theme_background      (GtkWidget          *widget);
void gtk3_curve_set_grid_size                     (GtkWidget          *widget,
                                                   Gtk3CurveGridSize   size);
Gtk3CurveGridSize gtk3_curve_get_grid_size        (GtkWidget          *widget);
void gtk3_curve_save                              (Gtk3CurveData      *data,
                                                   gchar              *filename);
Gtk3CurveData gtk3_curve_load                     (gchar              *filename);

void gtk3_curve_set_fps(GtkWidget *widget, gdouble fps);
void gtk3_curve_clear(GtkWidget *widget);

void gtk3_curve_live_trace_clear(GtkWidget *widget);
void gtk3_curve_live_trace_set_enabled(GtkWidget *widget, gboolean enabled);
void gtk3_curve_live_trace_push(GtkWidget   *widget,
                                gint         trace,
                                gfloat       value,
                                const gchar *label,
                                gfloat       red,
                                gfloat       green,
                                gfloat       blue,
                                gfloat       alpha);
void gtk3_curve_live_trace_push_at(GtkWidget   *widget,
                                   gint         trace,
                                   gfloat       x_value,
                                   gfloat       value,
                                   const gchar *label,
                                   gfloat       red,
                                   gfloat       green,
                                   gfloat       blue,
                                   gfloat       alpha);
void gtk3_curve_live_trace_set_dot(GtkWidget   *widget,
                                   gboolean     enabled,
                                   gfloat       x_value,
                                   gfloat       base_value,
                                   gfloat       value,
                                   const gchar *label,
                                   gfloat       red,
                                   gfloat       green,
                                   gfloat       blue,
                                   gfloat       alpha);

#endif
