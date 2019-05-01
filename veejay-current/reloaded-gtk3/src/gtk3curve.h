/* Copyright (C) 2016 Benoit Touchette
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation version
 * 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
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
  GTK3_CURVE_TYPE_LINEAR,       /* linear interpolation */
  GTK3_CURVE_TYPE_SPLINE,       /* spline interpolation */
  GTK3_CURVE_TYPE_FREE          /* free form curve */
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

  /* Padding for future expansion */
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

#endif /* __GTK3_CURVE__H__ */
