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

/* Portions of this code Copyright (C) 1997 David Mosberger and
 * Copyright (C) 1997 - 2000 GTK+ Team.
 */

/* Gveejay Reloaded - graphical interface for VeeJay
 *          (C) 2002-2026 Niels Elburg <nwelburg@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <gtk/gtk.h>
#include <vj-api.h> 

#include "gtk3curve.h"
#include "utils-gtk.h" 

//~ #define DEBUG

#ifdef DEBUG
#define DEBUG_INFO g_print
#define DEBUG_ERROR g_printerr
#else
#define DEBUG_INFO(...)
#define DEBUG_ERROR(...)
#endif

static guint                curve_type_changed_signal = 0;
static gint                 Gtk3Curve_private_offset = 0;
static GtkDrawingAreaClass *gtk3_curve_parent_class = NULL;

#define _gtk3_marshal_VOID__VOID  g_cclosure_marshal_VOID__VOID

#define GTK3_PARAM_READABLE G_PARAM_READABLE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB
#define GTK3_PARAM_WRITABLE G_PARAM_WRITABLE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB
#define GTK3_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

#define RADIUS            3 /* radius of the control points */
#define MIN_DISTANCE      8 /* min distance between control points */
#define GRAPH_MASK       (GDK_EXPOSURE_MASK | \
                          GDK_POINTER_MOTION_MASK | \
                          GDK_POINTER_MOTION_HINT_MASK | \
                          GDK_ENTER_NOTIFY_MASK | \
                          GDK_LEAVE_NOTIFY_MASK | \
                          GDK_BUTTON_PRESS_MASK | \
                          GDK_BUTTON_RELEASE_MASK | \
                          GDK_BUTTON1_MOTION_MASK)

struct _Gtk3CurvePrivate
{
  GdkWindow *event_window;
  GList *children;

  gint cursor_type;

  gfloat min_x;
  gfloat max_x;
  gfloat min_y;
  gfloat max_y;

  gboolean use_bg_theme;

  Gtk3CurveColor background;
  Gtk3CurveColor cpoint;
  Gtk3CurveColor grid;
  Gtk3CurveColor curve;

  Gtk3CurveType curve_type;

  gint width;
  gint height;

  gint grab_point;
  gint last;

  Gtk3CurveGridSize grid_size;
  Gtk3CurveData curve_data;

  guint state                 : 1;
  guint in_curve              : 1;

  gfloat yaxis_lo;
  gfloat yaxis_hi;

  gfloat xaxis_lo;
  gfloat xaxis_hi;

  gfloat last_x;
  gfloat last_y;

  gdouble  grid_resolution;
  gdouble  y_grid_resolution;

  gdouble  current_position;
  gboolean draw_position;
  gboolean draw_h_guides;

  gdouble  marker_x;
  gdouble  marker_y;
  gfloat   marker_value;
  gboolean marker_hover;

  gint x_grid_step;

  gdouble fps;

  gint     hover_cpoint;
  gdouble  hover_cpoint_x;
  gdouble  hover_cpoint_y;
  gfloat   hover_cpoint_value;
  gfloat   hover_cpoint_position;
};

enum
{
  PROP_0,
  PROP_CURVE_TYPE,
  PROP_MIN_X,
  PROP_MAX_X,
  PROP_MIN_Y,
  PROP_MAX_Y
};

static void gtk3_curve_realize              (GtkWidget            *widget);
static void gtk3_curve_unrealize            (GtkWidget            *widget);
static void gtk3_curve_style_updated        (GtkWidget            *widget);
static void gtk3_curve_size_allocate        (GtkWidget            *widget,
                                             GtkAllocation        *allocation);
static void gtk3_curve_configure            (Gtk3Curve            *curve);
static gboolean gtk3_curve_draw             (GtkWidget            *widget,
                                             cairo_t              *cr);
static void gtk3_curve_map                  (GtkWidget            *widget);
static void gtk3_curve_unmap                (GtkWidget            *widget);
static gboolean gtk3_curve_enter            (GtkWidget            *widget,
                                             GdkEventCrossing     *event);
static gboolean gtk3_curve_leave            (GtkWidget            *widget,
                                             GdkEventCrossing     *event);
static gboolean gtk3_curve_button_press     (GtkWidget            *widget,
                                             GdkEventButton       *event);
static gboolean gtk3_curve_button_release   (GtkWidget            *widget,
                                             GdkEventButton       *event);
static gboolean gtk3_curve_motion_notify    (GtkWidget            *widget,
                                             GdkEventMotion       *event);
static void gtk3_curve_screen_changed       (GtkWidget            *widget,
                                             GdkScreen            *prev_screen);
static void gtk3_curve_style_updated        (GtkWidget            *widget);
static void gtk3_curve_finalize             (GObject              *object);
static void gtk3_curve_dispose              (GObject              *object);
static void gtk3_curve_get_property         (GObject              *object,
                                             guint                 param_id,
                                             GValue               *value,
                                             GParamSpec           *pspec);
static void gtk3_curve_set_property         (GObject              *object,
                                             guint                 param_id,
                                             const GValue         *value,
                                             GParamSpec           *pspec);
static void gtk3_curve_create_layouts       (GtkWidget            *widget);
static void gtk3_curve_reset_vector         (GtkWidget            *widget);
static void gtk3_curve_interpolate          (GtkWidget            *widget,
                                             gint                  width,
                                             gint                  height);
static int project                          (gfloat                value,
                                             gfloat                min,
                                             gfloat                max,
                                             int                   norm);
static gfloat unproject                     (gint                  value,
                                             gfloat                min,
                                             gfloat                max,
                                             int                   norm);
static void spline_solve                    (int                   n,
                                             gfloat                x[],
                                             gfloat                y[],
                                             gfloat                y2[]);
static gfloat spline_eval                   (int                   n,
                                             gfloat                x[],
                                             gfloat                y[],
                                             gfloat                y2[],
                                             gfloat                val);
static void gtk3_curve_draw_line            (cairo_t              *cr,
                                             gdouble               x1,
                                             gdouble               y1,
                                             gdouble               x2,
                                             gdouble               y2);
static void gtk3_curve_class_init           (Gtk3CurveClass       *klass);
static void gtk3_curve_init                 (Gtk3Curve            *self);

static void gtk3_curve_get_cursor_coord     (GtkWidget            *widget,
                                             gint                 *tx,
                                             gint                 *ty);
static gboolean gtk3_curve_get_position_marker(GtkWidget *widget,
                                             gint       width,
                                             gint       height,
                                             gdouble   *mx,
                                             gdouble   *my,
                                             gfloat    *value);

static void gtk3_curve_draw_position_marker(GtkWidget *widget,
                                             cairo_t   *cr,
                                             gint       allocation_width,
                                             gint       allocation_height,
                                             gint       width,
                                             gint       height);
static gdouble gtk3_curve_auto_y_grid_resolution(gfloat min_y,
                                                  gfloat max_y);
static gint gtk3_curve_nice_x_grid_step(gint frame_count,
                                        gdouble fps);

static gdouble gtk3_curve_auto_x_grid_resolution(gfloat min_x,
                                                 gfloat max_x,
                                                 gdouble fps);
static void gtk3_curve_draw_hover_label(GtkWidget *widget,
                                        cairo_t   *cr,
                                        gdouble    px,
                                        gdouble    py,
                                        gfloat     position,
                                        gfloat     value,
                                        gint       allocation_width,
                                        gint       allocation_height);

static void gtk3_curve_update_x_grid(Gtk3CurvePrivate *priv);

static inline gpointer
gtk3_curve_get_instance_private (Gtk3Curve *self)
{
  return (G_STRUCT_MEMBER_P (self, Gtk3Curve_private_offset));
}


GType
gtk3_curve_get_type (void)
{
  static GType curve_type = 0;

  if (G_UNLIKELY (curve_type == 0))
    {
      const GTypeInfo curve_info =
      {
        sizeof (Gtk3CurveClass),
        NULL,   // base_init
        NULL,   // base_finalize
        (GClassInitFunc) gtk3_curve_class_init,
        NULL,   // class_finalize
        NULL,   // class_data
        sizeof (Gtk3Curve),
        0,    // n_preallocs
        (GInstanceInitFunc) gtk3_curve_init,
      };

      curve_type = g_type_register_static (GTK_TYPE_WIDGET, "Gtk3Curve",
                                           &curve_info, 0);

      Gtk3Curve_private_offset =
        g_type_add_instance_private (curve_type, sizeof (Gtk3CurvePrivate));
    }
  return curve_type;
}

GType
gtk3_curve_type_get_type (void)
{
  static GType etype = 0;
  if (G_UNLIKELY(etype == 0))
    {
      static const GEnumValue values[] =
      {
        { GTK3_CURVE_TYPE_LINEAR, "GTK3_CURVE_TYPE_LINEAR", "linear" },
        { GTK3_CURVE_TYPE_SPLINE, "GTK3_CURVE_TYPE_SPLINE", "spline" },
        { GTK3_CURVE_TYPE_FREE, "GTK3_CURVE_TYPE_FREE", "free" },
        { 0, NULL, NULL }
      };
      etype = g_enum_register_static (g_intern_static_string ("Gtk3CurveType"),
                                      values);
    }
  return etype;
}

static void
gtk3_curve_class_init (Gtk3CurveClass* klass)
{
  GObjectClass *gobject_class;
  GtkWidgetClass *widget_class;

  DEBUG_INFO("class_init [S]\n");

  widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->realize = gtk3_curve_realize;
  widget_class->unrealize = gtk3_curve_unrealize;

  widget_class->draw = gtk3_curve_draw;
  widget_class->motion_notify_event = gtk3_curve_motion_notify;

  widget_class->map = gtk3_curve_map;
  widget_class->unmap = gtk3_curve_unmap;

  widget_class->enter_notify_event = gtk3_curve_enter;
  widget_class->leave_notify_event = gtk3_curve_leave;

  widget_class->button_press_event = gtk3_curve_button_press;
  widget_class->button_release_event = gtk3_curve_button_release;

  widget_class->size_allocate = gtk3_curve_size_allocate;
  widget_class->screen_changed = gtk3_curve_screen_changed;
  widget_class->style_updated = gtk3_curve_style_updated;

  gtk_widget_class_set_accessible_role (widget_class, ATK_ROLE_DRAWING_AREA);

  gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_adjust_private_offset (klass, &Gtk3Curve_private_offset);

  gtk3_curve_parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gtk3_curve_finalize;
  gobject_class->dispose = gtk3_curve_dispose;
  gobject_class->set_property = gtk3_curve_set_property;
  gobject_class->get_property = gtk3_curve_get_property;

  g_object_class_install_property (gobject_class,
                                   PROP_CURVE_TYPE,
                                   g_param_spec_enum ("curve-type",
                                       "Curve type",
                                       "Is this curve linear, spline interpolated, or free-form",
                                       GTK3_TYPE_CURVE_TYPE,
                                       GTK3_CURVE_TYPE_SPLINE,
                                       GTK3_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
                                   PROP_MIN_X,
                                   g_param_spec_float ("min-x",
                                       "Minimum X",
                                       "Minimum possible value for X",
                                       -G_MAXFLOAT,
                                       G_MAXFLOAT,
                                       0.0,
                                       GTK3_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
                                   PROP_MAX_X,
                                   g_param_spec_float ("max-x",
                                       "Maximum X",
                                       "Maximum possible X value",
                                       -G_MAXFLOAT,
                                       G_MAXFLOAT,
                                       1.0,
                                       GTK3_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
                                   PROP_MIN_Y,
                                   g_param_spec_float ("min-y",
                                       "Minimum Y",
                                       "Minimum possible value for Y",
                                       -G_MAXFLOAT,
                                       G_MAXFLOAT,
                                       0.0,
                                       GTK3_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
                                   PROP_MAX_Y,
                                   g_param_spec_float ("max-y",
                                       "Maximum Y",
                                       "Maximum possible value for Y",
                                       -G_MAXFLOAT,
                                       G_MAXFLOAT,
                                       1.0,
                                       GTK3_PARAM_READWRITE));

  curve_type_changed_signal =
    g_signal_new ("curve-type-changed",
                  G_OBJECT_CLASS_TYPE (gobject_class),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (Gtk3CurveClass, curve_type_changed),
                  NULL, NULL,
                  _gtk3_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  DEBUG_INFO("class_init [E]\n");
}


static gdouble
gtk3_curve_auto_y_grid_resolution(gfloat min_y, gfloat max_y)
{
  gdouble span = fabs((gdouble) max_y - (gdouble) min_y);

  if (span <= 0.0)
    return 1.0;

  if (span <= 16.0 && fabs(span - floor(span + 0.5)) < 0.0001)
    return CLAMP(span, 1.0, 16.0);

  gdouble target_steps = 10.0;
  gdouble raw_step = span / target_steps;

  if (raw_step <= 0.0)
    return 1.0;

  gdouble magnitude = pow(10.0, floor(log10(raw_step)));
  gdouble normalized = raw_step / magnitude;
  gdouble nice_normalized;

  if (normalized <= 1.0)
    nice_normalized = 1.0;
  else if (normalized <= 2.0)
    nice_normalized = 2.0;
  else if (normalized <= 5.0)
    nice_normalized = 5.0;
  else
    nice_normalized = 10.0;

  gdouble nice_step = nice_normalized * magnitude;
  gdouble divisions = ceil(span / nice_step);

  return CLAMP(divisions, 1.0, 32.0);
}

static void
gtk3_curve_init (Gtk3Curve* self)
{
  Gtk3CurvePrivate *priv;

  DEBUG_INFO("init [S]\n");

  gtk_widget_set_has_window(GTK_WIDGET(self), TRUE);
  gtk_widget_set_redraw_on_allocate(GTK_WIDGET(self), TRUE);

  self->priv = gtk3_curve_get_instance_private (self);
  priv = self->priv;

  priv->curve_data.description = NULL;
  priv->curve_data.n_points = 0;
  priv->curve_data.d_point = NULL;
  priv->curve_data.n_cpoints = 0;
  priv->curve_data.d_cpoints = NULL;
  priv->curve_data.curve_type = GTK3_CURVE_TYPE_SPLINE;

  priv->use_bg_theme = TRUE;
  priv->cursor_type = GDK_TOP_LEFT_ARROW;
  priv->grid_size = GTK3_CURVE_GRID_MICRO;

  priv->height = 0;
  priv->grab_point = -1;

  priv->min_x = 0.0;
  priv->max_x = 1.0;
  priv->min_y = 0.0;
  priv->max_y = 1.0;

  priv->last_x = 0.0f;
  priv->last_y = 0.0f;
  priv->xaxis_lo = 0.0f;
  priv->xaxis_hi = 0.0f;
  priv->yaxis_lo = 0.0f;
  priv->yaxis_hi = 0.0f;

  priv->grid_resolution = 10.0f;
  priv->y_grid_resolution = gtk3_curve_auto_y_grid_resolution(priv->min_y,priv->max_y);
  priv->x_grid_step = 1;
  priv->fps = 0.0;
  gtk3_curve_update_x_grid(priv);

  priv->draw_position = TRUE;
  priv->draw_h_guides = TRUE;

  priv->current_position = 0.0;
  priv->draw_position = TRUE;
  priv->draw_h_guides = TRUE;

  priv->marker_x = 0.0;
  priv->marker_y = 0.0;
  priv->marker_value = 0.0f;
  priv->marker_hover = FALSE;

  priv->state = FALSE;
  priv->in_curve = FALSE;
  priv->last = 0;
  priv->width = 0;

  priv->hover_cpoint = -1;
  priv->hover_cpoint_x = 0.0;
  priv->hover_cpoint_y = 0.0;
  priv->hover_cpoint_value = 0.0f;
  priv->hover_cpoint_position = 0.0f;

  gtk3_curve_set_color_background_rgba (GTK_WIDGET(self), 1.0, 1.0, 1.0, 1.0);
  gtk3_curve_set_color_curve_rgba (GTK_WIDGET(self), 0.0, 0.0, 0.0, 1.0);
  gtk3_curve_set_color_grid_rgba (GTK_WIDGET(self), 0.0, 0.0, 0.0, 1.0);
  gtk3_curve_set_color_cpoint_rgba (GTK_WIDGET(self), 0.2, 0.2, 0.2, 1.0);


  DEBUG_INFO("init [E]\n");
}

static gboolean
gtk3_curve_mouse_scroll(GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
  (void) widget;
  (void) ev;
  (void) user_data;

  return FALSE;
}

GtkWidget *
gtk3_curve_new(void)
{
  GtkWidget *widget = g_object_new (GTK3_TYPE_CURVE, NULL);

  gtk_widget_set_events(widget,
      GDK_EXPOSURE_MASK |
      GDK_POINTER_MOTION_HINT_MASK |
      GDK_POINTER_MOTION_MASK |
      GDK_ENTER_NOTIFY_MASK |
      GDK_LEAVE_NOTIFY_MASK |
      GDK_BUTTON1_MOTION_MASK |
      GDK_BUTTON2_MOTION_MASK |
      GDK_BUTTON3_MOTION_MASK |
      GDK_BUTTON_PRESS_MASK |
      GDK_BUTTON_RELEASE_MASK |
      GDK_2BUTTON_PRESS |
      GDK_SCROLL_MASK);

  g_signal_connect(widget,"scroll-event", G_CALLBACK(gtk3_curve_mouse_scroll), NULL);

  return widget;
}

static gfloat
scale_param_value(Gtk3CurvePrivate *priv, gfloat y, gfloat height)
{
    if (height <= 0.0f)
        return priv->min_y;

    gfloat distance = priv->max_y - priv->min_y;

    if (distance == 0.0f)
        return priv->min_y;

    gfloat v = priv->min_y + ((distance / height) * (height - y));

    if (v < priv->min_y)
        v = priv->min_y;
    else if (v > priv->max_y)
        v = priv->max_y;

    return v;
}

static gfloat
scale_pos_value(Gtk3CurvePrivate *priv, gfloat x, gfloat width)
{
    if (width <= 0.0f)
        return priv->min_x;

    gfloat distance = priv->max_x - priv->min_x;

    if (distance == 0.0f)
        return priv->min_x;

    gfloat v = priv->min_x + ((distance / width) * x);

    if (v < priv->min_x)
        v = priv->min_x;
    else if (v > priv->max_x)
        v = priv->max_x;

    return v;
}

static gint
gtk3_curve_nice_x_grid_step(gint frame_count, gdouble fps)
{
  gint target_divisions = 10;
  gint raw_step;

  if (frame_count <= 0)
    return 1;

  raw_step = frame_count / target_divisions;

  if (raw_step < 1)
    raw_step = 1;


  if (frame_count <= 50)
    return 5;

  if (frame_count <= 100)
    return 10;

  if (fps > 1.0) {
    static const gdouble nice_seconds[] = {
      1.0,
      2.0,
      5.0,
      10.0,
      15.0,
      30.0,
      60.0,
      120.0,
      300.0,
      600.0
    };

    for (guint i = 0; i < G_N_ELEMENTS(nice_seconds); i++) {
      gint step = (gint)(nice_seconds[i] * fps + 0.5);

      if (step >= raw_step)
        return step;
    }

    return (gint)(600.0 * fps + 0.5);
  }

  {
    static const gint nice_frames[] = {
      1, 2, 5,
      10, 20, 25, 50,
      100, 125, 250, 500,
      1000, 1500, 2500, 5000,
      10000
    };

    for (guint i = 0; i < G_N_ELEMENTS(nice_frames); i++) {
      if (nice_frames[i] >= raw_step)
        return nice_frames[i];
    }
  }

  return raw_step;
}

static gdouble
gtk3_curve_auto_x_grid_resolution(gfloat min_x, gfloat max_x, gdouble fps)
{
  gint frame_count = (gint)(fabs(max_x - min_x) + 0.5);
  gint step;

  if (frame_count <= 0)
    return 1.0;

  step = gtk3_curve_nice_x_grid_step(frame_count, fps);

  if (step <= 0)
    step = 1;

  return CLAMP(ceil((gdouble) frame_count / (gdouble) step), 1.0, 32.0);
}

static void
gtk3_curve_update_x_grid(Gtk3CurvePrivate *priv)
{
  gint frame_count;

  if (!priv)
    return;

  frame_count = (gint)(fabs((gdouble)priv->max_x -
                            (gdouble)priv->min_x) + 0.5);

  if (frame_count <= 0)
    frame_count = 1;

  //priv->x_grid_step = gtk3_curve_nice_x_grid_step(frame_count, 0.0);
  priv->x_grid_step = gtk3_curve_nice_x_grid_step(frame_count, priv->fps);

  if (priv->x_grid_step <= 0)
    priv->x_grid_step = 1;

  priv->grid_resolution =
    CLAMP(ceil((gdouble) frame_count / (gdouble) priv->x_grid_step),
          1.0,
          32.0);
}

static void
gtk3_curve_style_updated (GtkWidget *widget)
{
  GTK_WIDGET_CLASS (gtk3_curve_parent_class)->style_updated (widget);

  DEBUG_INFO("style_updated [S]\n");
  if (gtk_widget_get_realized (widget) &&
      gtk_widget_get_has_window (widget))
    {
      gtk_widget_queue_draw (widget);
    }
  DEBUG_INFO("style_updated [E]\n");
}

static void
gtk3_curve_realize (GtkWidget *widget)
{
  GtkAllocation allocation;
  GdkWindow *parent_window;
  GdkWindowAttr attributes;
  gint attributes_mask;
  Gtk3CurvePrivate *priv;

  priv = GTK3_CURVE (widget)->priv;

  DEBUG_INFO("realize [S]\n");
  if (!gtk_widget_get_has_window (widget))
    {
      GTK_WIDGET_CLASS (gtk3_curve_parent_class)->realize (widget);
    }
  else
    {
      gtk_widget_set_realized (widget, TRUE);
      parent_window = gtk_widget_get_parent_window (widget);
      gtk_widget_set_window (widget, parent_window);
      g_object_ref (parent_window);

      gtk_widget_get_allocation (widget, &allocation);
      DEBUG_INFO("allocation [%d,%d] [%dx%d]\n",
              allocation.x,
              allocation.y,
              allocation.width,
              allocation.height);

      attributes.window_type = GDK_WINDOW_CHILD;
      attributes.x = allocation.x;
      attributes.y = allocation.y;
      attributes.width = allocation.width;
      attributes.height = allocation.height;
      attributes.wclass = GDK_INPUT_OUTPUT;
      attributes.visual = gtk_widget_get_visual (widget);
      attributes.event_mask = gtk_widget_get_events (widget) |
                              GRAPH_MASK;

      attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

      priv->event_window = gdk_window_new (parent_window,
                                           &attributes,
                                           attributes_mask);

      gtk_widget_register_window (widget, priv->event_window);
      gtk_widget_set_window (widget, priv->event_window);
    }

  gtk3_curve_configure (GTK3_CURVE (widget));

  DEBUG_INFO("realize [E]\n");
}

static void
gtk3_curve_unrealize (GtkWidget *widget)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE (widget)->priv;

  DEBUG_INFO("unrealize [S]\n");
  if (priv->event_window != NULL)
    {
      DEBUG_INFO("unregister/destroy\n");
      gtk_widget_unregister_window (widget, priv->event_window);
      gdk_window_destroy (priv->event_window);
      priv->event_window = NULL;
    }

  DEBUG_INFO("unrealize [E]\n");
}

static void
gtk3_curve_size_allocate (GtkWidget     *widget,
                          GtkAllocation *allocation)
{
  g_return_if_fail (GTK3_IS_CURVE (widget));
  g_return_if_fail (allocation != NULL);

  DEBUG_INFO("size_allocate [S]\n");
  gtk_widget_set_allocation (widget, allocation);

  DEBUG_INFO("allocation [%d,%d] [%dx%d]\n",
          allocation->x,
          allocation->y,
          allocation->width,
          allocation->height);

  if (gtk_widget_get_realized (widget))
    {
      if (gtk_widget_get_has_window (widget))
        {
          gdk_window_move_resize (gtk_widget_get_window (widget),
                                  allocation->x, allocation->y,
                                  allocation->width, allocation->height);
        }

      gtk3_curve_configure (GTK3_CURVE (widget));
    }
  DEBUG_INFO("size_allocate [E]\n");
}

static void
gtk3_curve_configure (Gtk3Curve *curve)
{
  GtkAllocation allocation;
  GtkWidget *widget;
  GdkEvent *event = gdk_event_new (GDK_CONFIGURE);

  DEBUG_INFO("configure [S]\n");

  widget = GTK_WIDGET (curve);
  gtk_widget_get_allocation (widget, &allocation);

  DEBUG_INFO("allocation [%d,%d] [%dx%d]\n",
             allocation.x,
             allocation.y,
             allocation.width,
             allocation.height);

  event->configure.window = g_object_ref (gtk_widget_get_window (widget));
  event->configure.send_event = TRUE;
  event->configure.x = allocation.x;
  event->configure.y = allocation.y;
  event->configure.width = allocation.width;
  event->configure.height = allocation.height;

  gtk_widget_event (widget, event);
  gtk_widget_queue_draw (widget);

  gdk_event_free (event);

  DEBUG_INFO("configure [E]\n");
}

static void gtk3_curve_draw_line (cairo_t   *cr,
                                  gdouble x1, gdouble y1,
                                  gdouble x2, gdouble y2)
{
  cairo_move_to (cr, x1, y1);
  cairo_line_to (cr, x2, y2);
  cairo_stroke (cr);
}

static void
gtk3_curve_draw_labels(GtkWidget *widget, cairo_t *cr, gint hei, gint wid)
{
  gchar text[100];
  Gtk3CurvePrivate *priv;
  gint i, incr;
  Gtk3Curve *curve;
  gfloat y_grid;
  cairo_text_extents_t extents;

  curve = GTK3_CURVE(widget);
  priv = curve->priv;

  y_grid = priv->y_grid_resolution;

  if (y_grid < 1.0f)
    y_grid = 1.0f;
  else if (y_grid > 64.0f)
    y_grid = 64.0f;

  cairo_set_source_rgba(cr,
                        priv->grid.red,
                        priv->grid.green,
                        priv->grid.blue,
                        priv->grid.alpha);

  cairo_select_font_face(cr,
                         "Sans",
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 11);

  gfloat wm = wid - (RADIUS * 2);
  gfloat hm = hei - (RADIUS * 2);

  {
    gint min_frame = (gint)(priv->min_x + 0.5f);
    gint max_frame = (gint)(priv->max_x + 0.5f);
    gint step = priv->x_grid_step;

    if (step <= 0)
      step = 1;

    for (gint frame = min_frame; frame <= max_frame; frame += step) {
      gdouble x1 = RADIUS + project((gfloat) frame,
                                    priv->min_x,
                                    priv->max_x,
                                    (gint) wm);
      gdouble y1 = hei - 2.0;

      snprintf(text, sizeof(text), "%d", frame);

      cairo_text_extents(cr, text, &extents);
      cairo_move_to(cr, x1 - (extents.width / 2.0), y1);
      cairo_show_text(cr, text);
    }

    if (((max_frame - min_frame) % step) != 0) {
      gdouble x1 = RADIUS + project((gfloat) max_frame,
                                    priv->min_x,
                                    priv->max_x,
                                    (gint) wm);
      gdouble y1 = hei - 2.0;

      snprintf(text, sizeof(text), "%d", max_frame);

      cairo_text_extents(cr, text, &extents);
      cairo_move_to(cr, x1 - (extents.width / 2.0), y1);
      cairo_show_text(cr, text);
    }
  }

  incr = 1;

  snprintf(text, sizeof(text), "%d", (int) priv->max_y);
  cairo_text_extents(cr, text, &extents);

  while (((extents.height * 2.0 * y_grid) / incr) > hei)
    incr++;

  for (i = 0; i <= (int) y_grid; i += incr) {
    gdouble y1 = RADIUS + hm - (i * (hm / y_grid));
    gdouble x1 = 2.0;

    snprintf(text,
             sizeof(text),
             "%d",
             (int) scale_param_value(priv,
                                      (gfloat)(y1 - RADIUS),
                                      (gfloat) hm));

    cairo_text_extents(cr, text, &extents);
    cairo_move_to(cr, x1, y1 + (extents.height / 2.0));
    cairo_show_text(cr, text);
  }
}

static gboolean
gtk3_curve_get_position_marker(GtkWidget *widget,
                               gint       width,
                               gint       height,
                               gdouble   *mx,
                               gdouble   *my,
                               gfloat    *value)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE(widget)->priv;

  if (!priv->draw_position)
    return FALSE;

  if (!priv->curve_data.d_point || priv->curve_data.n_points <= 0)
    return FALSE;

  if (width <= 1 || height <= 1)
    return FALSE;

  if (priv->max_x <= priv->min_x)
    return FALSE;

  if (priv->max_y <= priv->min_y)
    return FALSE;

  gdouble pos = priv->current_position;

  if (pos < priv->min_x)
    pos = priv->min_x;
  else if (pos > priv->max_x)
    pos = priv->max_x;

  gint x = project((gfloat) pos, priv->min_x, priv->max_x, width);

  if (x < 0)
    x = 0;
  else if (x >= priv->curve_data.n_points)
    x = priv->curve_data.n_points - 1;

  gdouble px = priv->curve_data.d_point[x].x;
  gdouble py = priv->curve_data.d_point[x].y;

  gfloat v = unproject((gint)(RADIUS + height - py + 0.5),
                       priv->min_y,
                       priv->max_y,
                       height);

  if (v < priv->min_y)
    v = priv->min_y;
  else if (v > priv->max_y)
    v = priv->max_y;

  if (mx)
    *mx = px;

  if (my)
    *my = py;

  if (value)
    *value = v;

  return TRUE;
}

static void
gtk3_curve_draw_hover_label(GtkWidget *widget,
                            cairo_t   *cr,
                            gdouble    px,
                            gdouble    py,
                            gfloat     position,
                            gfloat     value,
                            gint       allocation_width,
                            gint       allocation_height)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE(widget)->priv;

  gchar line1[96];
  gchar line2[96];
  gchar line3[96];

  cairo_text_extents_t e1;
  cairo_text_extents_t e2;
  cairo_text_extents_t e3;

  gdouble text_w;
  gdouble box_w;
  gdouble box_h;
  gdouble box_x;
  gdouble box_y;

  char *timecode = format_selection_time(priv->min_x, position);

  g_snprintf(line1, sizeof(line1), "Frame %.0f", position);
  g_snprintf(line2, sizeof(line2), "Value %.2f", value);
  g_snprintf(line3, sizeof(line3), "%s", timecode ? timecode : "");

  if (timecode)
    free(timecode);

  cairo_save(cr);

  cairo_select_font_face(cr,
                         "Sans",
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 11.0);

  cairo_text_extents(cr, line1, &e1);
  cairo_text_extents(cr, line2, &e2);
  cairo_text_extents(cr, line3, &e3);

  text_w = MAX(e1.width, e2.width);
  text_w = MAX(text_w, e3.width);

  box_w = text_w + 14.0;
  box_h = 48.0;

  box_x = px + 12.0;
  box_y = py - 26.0;

  if (box_x + box_w > allocation_width - 2.0)
    box_x = px - box_w - 12.0;

  if (box_y < 2.0)
    box_y = py + 12.0;

  if (box_y + box_h > allocation_height - 2.0)
    box_y = allocation_height - box_h - 2.0;

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.78);
  cairo_rectangle(cr, box_x, box_y, box_w, box_h);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
  cairo_rectangle(cr, box_x + 0.5, box_y + 0.5, box_w - 1.0, box_h - 1.0);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);

  cairo_move_to(cr, box_x + 7.0, box_y + 14.0);
  cairo_show_text(cr, line1);

  cairo_move_to(cr, box_x + 7.0, box_y + 29.0);
  cairo_show_text(cr, line2);

  cairo_move_to(cr, box_x + 7.0, box_y + 44.0);
  cairo_show_text(cr, line3);

  cairo_restore(cr);
}

static void
gtk3_curve_draw_position_marker(GtkWidget *widget,
                                cairo_t   *cr,
                                gint       allocation_width,
                                gint       allocation_height,
                                gint       width,
                                gint       height)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE(widget)->priv;
  gdouble mx = 0.0;
  gdouble my = 0.0;
  gfloat value = 0.0f;

  if (!gtk3_curve_get_position_marker(widget, width, height, &mx, &my, &value))
    return;

  priv->marker_x = mx;
  priv->marker_y = my;
  priv->marker_value = value;

  cairo_save(cr);

  // soft halo
  cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 0.18);
  cairo_arc(cr, mx, my, 8.0, 0.0, 2.0 * M_PI);
  cairo_fill(cr);

  //red dot
  cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 0.95);
  cairo_arc(cr, mx, my, 4.5, 0.0, 2.0 * M_PI);
  cairo_fill(cr);

  // dark outline
  cairo_set_source_rgba(cr, 0.25, 0.0, 0.0, 0.95);
  cairo_arc(cr, mx, my, 5.5, 0.0, 2.0 * M_PI);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  if (priv->marker_hover) {
    gtk3_curve_draw_hover_label(widget,
                                cr,
                                mx,
                                my,
                                (gfloat) priv->current_position,
                                value,
                                allocation_width,
                                allocation_height);
  }

  cairo_restore(cr);
}

static gboolean
gtk3_curve_draw(GtkWidget *widget, cairo_t *cr)
{
  Gtk3CurvePrivate *priv;
  GtkStyleContext  *style_context;
  GdkRGBA           color;
  GtkAllocation     allocation;
  Gtk3Curve        *curve;

  curve = GTK3_CURVE(widget);
  priv = curve->priv;

  if (!cr)
    return TRUE;

  gtk_widget_get_allocation(widget, &allocation);

  gint width  = allocation.width  - RADIUS * 2;
  gint height = allocation.height - RADIUS * 2;

  if (width <= 1 || height <= 1)
    return FALSE;

  if (priv->width != width ||
      priv->height != height ||
      priv->curve_data.n_points != width)
  {
    gtk3_curve_interpolate(widget, width, height);
  }

  if (priv->use_bg_theme) {
    style_context = gtk_widget_get_style_context(widget);

    gtk_render_background(style_context,
                          cr,
                          0,
                          0,
                          allocation.width,
                          allocation.height);

    vj_gtk_context_get_color(style_context,
                             "background-color",
                             gtk_style_context_get_state(style_context),
                             &color);

    gdk_cairo_set_source_rgba(cr, &color);
  } else {
    cairo_set_source_rgba(cr,
                          priv->background.red,
                          priv->background.green,
                          priv->background.blue,
                          priv->background.alpha);
  }

  cairo_paint(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  gfloat y_grid = priv->y_grid_resolution;
  if (y_grid < 1.0f)
    y_grid = 1.0f;
  else if (y_grid > 64.0f)
    y_grid = 64.0f;

  gfloat wm = allocation.width  - (RADIUS * 2);
  gfloat hm = allocation.height - (RADIUS * 2);

  cairo_set_line_width(cr, 0.5);
  cairo_set_source_rgba(cr,
                        priv->grid.red,
                        priv->grid.green,
                        priv->grid.blue,
                        priv->grid.alpha * 0.55);

  for (int i = 0; i <= (int) y_grid; i++) {
    gdouble gy = RADIUS + (i * (hm / y_grid));
    gtk3_curve_draw_line(cr, RADIUS, gy, wm + RADIUS, gy);
  }

  {
    gint min_frame = (gint)(priv->min_x + 0.5f);
    gint max_frame = (gint)(priv->max_x + 0.5f);
    gint step = priv->x_grid_step;

    if (step <= 0)
      step = 1;

    for (gint frame = min_frame; frame <= max_frame; frame += step) {
      gdouble gx = RADIUS + project((gfloat) frame,
                                    priv->min_x,
                                    priv->max_x,
                                    (gint) wm);

      gtk3_curve_draw_line(cr, gx, RADIUS, gx, hm + RADIUS);
    }

    if (((max_frame - min_frame) % step) != 0) {
      gdouble gx = RADIUS + project((gfloat) max_frame,
                                    priv->min_x,
                                    priv->max_x,
                                    (gint) wm);

      gtk3_curve_draw_line(cr, gx, RADIUS, gx, hm + RADIUS);
    }
  }

  if (priv->draw_h_guides) {
    cairo_set_line_width(cr, 0.5);
    cairo_set_source_rgba(cr, 0.8, 0.3, 0.3, priv->curve.alpha * 0.45);
    gtk3_curve_draw_line(cr,
                         RADIUS,
                         RADIUS + hm / 2.0,
                         RADIUS + wm,
                         RADIUS + hm / 2.0);
  }

  gtk3_curve_draw_labels(widget, cr, allocation.height, allocation.width);

  if (priv->curve_data.d_point && priv->curve_data.n_points > 1) {
    cairo_set_line_width(cr, 1.25);
    cairo_set_source_rgba(cr,
                          priv->curve.red,
                          priv->curve.green,
                          priv->curve.blue,
                          priv->curve.alpha);

    gdouble last_x = priv->curve_data.d_point[0].x;
    gdouble last_y = priv->curve_data.d_point[0].y;

    for (int i = 1; i < priv->curve_data.n_points; i++) {
      gdouble x = priv->curve_data.d_point[i].x;
      gdouble y = priv->curve_data.d_point[i].y;

      gtk3_curve_draw_line(cr, last_x, last_y, x, y);

      last_x = x;
      last_y = y;
    }
  }

  if (priv->curve_data.curve_type != GTK3_CURVE_TYPE_FREE) {
    for (int i = 0; i < priv->curve_data.n_cpoints; ++i) {
      gdouble x, y;

      if (priv->curve_data.d_cpoints[i].x < priv->min_x)
        continue;

      x = RADIUS + project(priv->curve_data.d_cpoints[i].x,
                           priv->min_x,
                           priv->max_x,
                           width);

      y = RADIUS + hm - project(priv->curve_data.d_cpoints[i].y,
                                priv->min_y,
                                priv->max_y,
                                height);
      if (i == priv->hover_cpoint) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.35);
        cairo_arc(cr, x, y, RADIUS * 2.5, 0, 2 * M_PI);
        cairo_fill(cr);
      }
      cairo_set_source_rgba(cr,
                            priv->cpoint.red,
                            priv->cpoint.green,
                            priv->cpoint.blue,
                            priv->cpoint.alpha);

      cairo_arc(cr, x, y, RADIUS * 1.5, 0, 2 * M_PI);
      cairo_fill(cr);
    }
  }

  if (priv->hover_cpoint >= 0) {
    gtk3_curve_draw_hover_label(widget,
                                cr,
                                priv->hover_cpoint_x,
                                priv->hover_cpoint_y,
                                priv->hover_cpoint_position,
                                priv->hover_cpoint_value,
                                allocation.width,
                                allocation.height);
  }

  gtk3_curve_draw_position_marker(widget,
                                  cr,
                                  allocation.width,
                                  allocation.height,
                                  width,
                                  height);

  return FALSE;
}

static void
gtk3_curve_map (GtkWidget *widget)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE (widget)->priv;

  GTK_WIDGET_CLASS (gtk3_curve_parent_class)->map (widget);

  DEBUG_INFO("map [S]\n");
  if (priv->event_window)
    {
      DEBUG_INFO("show\n");
      gdk_window_show (priv->event_window);
    }
  DEBUG_INFO("map [E]\n");
}

static void
gtk3_curve_unmap (GtkWidget *widget)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE (widget)->priv;

  DEBUG_INFO("unmap [S]\n");
  if (priv->event_window)
    {
      DEBUG_INFO("hide\n");
      gdk_window_hide (priv->event_window);
    }

  GTK_WIDGET_CLASS (gtk3_curve_parent_class)->unmap (widget);
  DEBUG_INFO("unmap [E]\n");
}

static gboolean
gtk3_curve_enter (GtkWidget        *widget,
                  GdkEventCrossing *event)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE (widget)->priv;

  DEBUG_INFO("enter [S]\n");
  if (event->window == priv->event_window)
    {
      priv->in_curve = TRUE;
    }
  DEBUG_INFO("enter [E]\n");

  return FALSE;
}

static gboolean
gtk3_curve_leave(GtkWidget        *widget,
                 GdkEventCrossing *event)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE(widget)->priv;

  DEBUG_INFO("leave [S]\n");

  if (event->window == priv->event_window) {
    priv->in_curve = FALSE;

    if (priv->marker_hover) {
      priv->marker_hover = FALSE;

      if (gtk_widget_is_visible(widget))
        gtk_widget_queue_draw(widget);
    }
  }

  if (priv->hover_cpoint >= 0) {
    priv->hover_cpoint = -1;

    if (gtk_widget_is_visible(widget))
      gtk_widget_queue_draw(widget);
  }

  DEBUG_INFO("leave [E]\n");

  return FALSE;
}

static
void gtk3_curve_get_cursor_coord(GtkWidget *widget, gint *tx, gint *ty)
{
  GdkSeat          *user_seat;
  GdkDevice        *device_pointer;

  user_seat = gdk_display_get_default_seat (gtk_widget_get_display (widget));
  device_pointer = gdk_seat_get_pointer (user_seat);
  gdk_window_get_device_position (gtk_widget_get_window (widget),
                                  device_pointer,
                                  tx, ty, NULL);
}

static gboolean
gtk3_curve_button_press(GtkWidget *widget, GdkEventButton *event)
{
  (void) event;

  Gtk3CurvePrivate *priv = GTK3_CURVE(widget)->priv;
  GtkAllocation allocation;
  gint x, y, width, height;
  gint tx, ty;
  gint closest_point = 0;
  guint distance = ~0U;

  if (!event || event->button != 1)
    return FALSE;

  gtk_grab_add(widget);

  gtk_widget_get_allocation(widget, &allocation);

  width = allocation.width - RADIUS * 2;
  height = allocation.height - RADIUS * 2;

  if (width <= 1 || height <= 1)
    return FALSE;

  if (!priv->curve_data.d_point || priv->curve_data.n_points != width)
    gtk3_curve_interpolate(widget, width, height);

  gtk3_curve_get_cursor_coord(widget, &tx, &ty);

  x = CLAMP((tx - RADIUS), 0, width - 1);
  y = CLAMP((ty - RADIUS), 0, height - 1);

  gfloat min_x = priv->min_x;

  for (int i = 0; i < priv->curve_data.n_cpoints; ++i) {
    gint cx = project(priv->curve_data.d_cpoints[i].x,
                      min_x,
                      priv->max_x,
                      width);

    guint d = (guint) abs(x - cx);

    if (d < distance) {
      distance = d;
      closest_point = i;
    }
  }

  switch (priv->curve_data.curve_type) {
    default:
    case GTK3_CURVE_TYPE_LINEAR:
    case GTK3_CURVE_TYPE_SPLINE:
      if (distance > MIN_DISTANCE) {
        if (priv->curve_data.n_cpoints > 0) {
          gint cx = project(priv->curve_data.d_cpoints[closest_point].x,
                            min_x,
                            priv->max_x,
                            width);

          if (x > cx)
            ++closest_point;
        }

        ++priv->curve_data.n_cpoints;

        priv->curve_data.d_cpoints =
          g_realloc(priv->curve_data.d_cpoints,
                    priv->curve_data.n_cpoints *
                    sizeof(*priv->curve_data.d_cpoints));

        for (int i = priv->curve_data.n_cpoints - 1; i > closest_point; --i) {
          memcpy(priv->curve_data.d_cpoints + i,
                 priv->curve_data.d_cpoints + i - 1,
                 sizeof(*priv->curve_data.d_cpoints));
        }
      }

      priv->grab_point = closest_point;

      priv->curve_data.d_cpoints[priv->grab_point].x =
        unproject(x, min_x, priv->max_x, width);

      priv->curve_data.d_cpoints[priv->grab_point].y =
        unproject(height - y, priv->min_y, priv->max_y, height);

      gtk3_curve_interpolate(widget, width, height);
      break;

    case GTK3_CURVE_TYPE_FREE:
      if (priv->curve_data.d_point && x < priv->curve_data.n_points) {
        priv->curve_data.d_point[x].x = RADIUS + x;
        priv->curve_data.d_point[x].y = RADIUS + y;
        priv->grab_point = x;
        priv->last = y;
      }
      break;
  }

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);

  return FALSE;
}

static gboolean
gtk3_curve_button_release (GtkWidget        *widget,
                           GdkEventButton   *event)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE (widget)->priv;
  GtkAllocation     allocation;
  gint              src, dst, width, height;
  gfloat            min_x;

  DEBUG_INFO("button release [S]\n");

  gtk_grab_remove (widget);

  gtk_widget_get_allocation (widget, &allocation);

  width = allocation.width - RADIUS * 2;
  height = allocation.height - RADIUS * 2;

  if ((width < 0) || (height < 0))
    return FALSE;

  min_x = priv->min_x;

  if (priv->curve_data.curve_type != GTK3_CURVE_TYPE_FREE)
    {
      for (src = dst = 0; src < priv->curve_data.n_cpoints; ++src)
        {
          if (priv->curve_data.d_cpoints[src].x >= min_x)
            {
              memcpy (priv->curve_data.d_cpoints + dst,
                      priv->curve_data.d_cpoints + src,
                      sizeof (*priv->curve_data.d_cpoints));
              ++dst;
            }
        }

      if (dst < src)
        {
          priv->curve_data.n_cpoints -= (src - dst);

          if (priv->curve_data.n_cpoints <= 0)
            {
              priv->curve_data.n_cpoints = 1;
              priv->curve_data.d_cpoints[0].x = min_x;
              priv->curve_data.d_cpoints[0].y = priv->min_y;
              gtk3_curve_interpolate (widget, width, height);

              if (gtk_widget_is_visible (widget))
                {
                  DEBUG_INFO("queue draw\n");
                  gtk_widget_queue_draw (widget);
                }
            }

          priv->curve_data.d_cpoints =
            g_realloc (priv->curve_data.d_cpoints,
                       priv->curve_data.n_cpoints *
                         sizeof (*priv->curve_data.d_cpoints));
        }
    }

  priv->grab_point = -1;

  DEBUG_INFO("button release [E]\n");

  return FALSE;
}

static gboolean
gtk3_curve_motion_notify(GtkWidget *widget, GdkEventMotion *event)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE(widget)->priv;
  GtkAllocation allocation;
  GdkCursorType new_type = priv->cursor_type;
  gint tx, ty;
  gint x, y;
  gint width, height;
  guint distance;
  gboolean changed = FALSE;
  gboolean hover_changed = FALSE;

  gtk_widget_get_allocation(widget, &allocation);

  width = allocation.width - RADIUS * 2;
  height = allocation.height - RADIUS * 2;

  if (width <= 1 || height <= 1)
    return FALSE;

  gtk3_curve_get_cursor_coord(widget, &tx, &ty);

  x = CLAMP((tx - RADIUS), 0, width - 1);
  y = CLAMP((ty - RADIUS), 0, height - 1);

  priv->last_x = (gfloat) x;
  priv->last_y = (gfloat) y;

  if (priv->draw_position) {
    gdouble dx = (gdouble) tx - priv->marker_x;
    gdouble dy = (gdouble) ty - priv->marker_y;
    gboolean marker_hover = ((dx * dx + dy * dy) <= (12.0 * 12.0));

    if (marker_hover != priv->marker_hover) {
      priv->marker_hover = marker_hover;
      hover_changed = TRUE;
    }
  }

  gfloat min_x = priv->min_x;

  gint old_hover_cpoint = priv->hover_cpoint;

  priv->hover_cpoint = -1;
  priv->hover_cpoint_x = 0.0;
  priv->hover_cpoint_y = 0.0;
  priv->hover_cpoint_value = 0.0f;
  priv->hover_cpoint_position = 0.0f;

  if (priv->curve_data.curve_type != GTK3_CURVE_TYPE_FREE &&
      priv->grab_point == -1 &&
      priv->curve_data.d_cpoints &&
      priv->curve_data.n_cpoints > 0)
  {
    guint best_dist2 = 12U * 12U;

    for (int i = 0; i < priv->curve_data.n_cpoints; ++i) {
      gdouble cx;
      gdouble cy;
      gint dxp;
      gint dyp;
      guint dist2;

      if (priv->curve_data.d_cpoints[i].x < priv->min_x)
        continue;

      if (priv->curve_data.d_cpoints[i].x > priv->max_x)
        continue;

      cx = RADIUS + project(priv->curve_data.d_cpoints[i].x,
                            priv->min_x,
                            priv->max_x,
                            width);

      cy = RADIUS + height - project(priv->curve_data.d_cpoints[i].y,
                                    priv->min_y,
                                    priv->max_y,
                                    height);

      dxp = tx - (gint)(cx + 0.5);
      dyp = ty - (gint)(cy + 0.5);

      dist2 = (guint)(dxp * dxp + dyp * dyp);

      if (dist2 <= best_dist2) {
        best_dist2 = dist2;

        priv->hover_cpoint = i;
        priv->hover_cpoint_x = cx;
        priv->hover_cpoint_y = cy;
        priv->hover_cpoint_position = priv->curve_data.d_cpoints[i].x;
        priv->hover_cpoint_value = priv->curve_data.d_cpoints[i].y;
      }
    }
  }

  if (old_hover_cpoint != priv->hover_cpoint)
    hover_changed = TRUE;

  distance = ~0U;

  for (int i = 0; i < priv->curve_data.n_cpoints; ++i) {
    gint cx = project(priv->curve_data.d_cpoints[i].x,
                      min_x,
                      priv->max_x,
                      width);

    guint d = (guint) abs(x - cx);

    if (d < distance)
      distance = d;
  }

  switch (priv->curve_data.curve_type) {
    default:
    case GTK3_CURVE_TYPE_LINEAR:
    case GTK3_CURVE_TYPE_SPLINE:
      if (priv->grab_point == -1) {
        if (distance <= MIN_DISTANCE)
          new_type = GDK_FLEUR;
        else
          new_type = GDK_TCROSS;
      } else {
        gint leftbound = -MIN_DISTANCE;
        gint rightbound = width + RADIUS * 2 + MIN_DISTANCE;

        new_type = GDK_TCROSS;

        if (priv->grab_point > 0) {
          leftbound =
            RADIUS + project(priv->curve_data.d_cpoints[priv->grab_point - 1].x,
                             min_x,
                             priv->max_x,
                             width);
        }

        if (priv->grab_point + 1 < priv->curve_data.n_cpoints) {
          rightbound =
            RADIUS + project(priv->curve_data.d_cpoints[priv->grab_point + 1].x,
                             min_x,
                             priv->max_x,
                             width);
        }

        if (tx <= leftbound ||
            tx >= rightbound ||
            ty > height + RADIUS * 2 + MIN_DISTANCE ||
            ty < -MIN_DISTANCE)
        {
          priv->curve_data.d_cpoints[priv->grab_point].x = min_x - 1.0f;
        } else {
          priv->curve_data.d_cpoints[priv->grab_point].x =
            unproject(x, min_x, priv->max_x, width);

          priv->curve_data.d_cpoints[priv->grab_point].y =
            unproject(height - y, priv->min_y, priv->max_y, height);
        }

        gtk3_curve_interpolate(widget, width, height);
        changed = TRUE;
      }
      break;

    case GTK3_CURVE_TYPE_FREE:
      if (priv->grab_point != -1 && priv->curve_data.d_point) {
        gint x1, x2, y1, y2;

        if (priv->grab_point > x) {
          x1 = x;
          x2 = priv->grab_point;
          y1 = y;
          y2 = priv->last;
        } else {
          x1 = priv->grab_point;
          x2 = x;
          y1 = priv->last;
          y2 = y;
        }

        if (x1 < 0)
          x1 = 0;

        if (x2 >= priv->curve_data.n_points)
          x2 = priv->curve_data.n_points - 1;

        if (x2 != x1) {
          for (int i = x1; i <= x2; i++) {
            priv->curve_data.d_point[i].x = RADIUS + i;
            priv->curve_data.d_point[i].y =
              RADIUS + (y1 + ((y2 - y1) * (i - x1)) / (x2 - x1));
          }
        } else if (x >= 0 && x < priv->curve_data.n_points) {
          priv->curve_data.d_point[x].x = RADIUS + x;
          priv->curve_data.d_point[x].y = RADIUS + y;
        }

        priv->grab_point = x;
        priv->last = y;
        changed = TRUE;
      }

      if (event->state & GDK_BUTTON1_MASK)
        new_type = GDK_TCROSS;
      else
        new_type = GDK_PENCIL;

      break;
  }

  if (new_type != (GdkCursorType) priv->cursor_type) {
    GdkCursor *cursor;

    priv->cursor_type = new_type;

    cursor = gdk_cursor_new_for_display(gtk_widget_get_display(widget),
                                        priv->cursor_type);

    gdk_window_set_cursor(gtk_widget_get_window(widget), cursor);
    g_object_unref(cursor);
  }

  if ((changed || hover_changed) && gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);

  return TRUE;
}

static void
gtk3_curve_screen_changed (GtkWidget *widget,
                           GdkScreen *prev_screen)
{
  DEBUG_INFO("screen changed [S]\n");
  gtk3_curve_create_layouts (widget);
  DEBUG_INFO("screen changed [E]\n");
}

static void
gtk3_curve_dispose (GObject *object)
{
  G_OBJECT_CLASS (gtk3_curve_parent_class)->dispose (object);
}

static void
gtk3_curve_finalize (GObject *object)
{
  Gtk3Curve *curve;
  Gtk3CurvePrivate *priv;

  g_return_if_fail (GTK3_IS_CURVE (object));

  curve = GTK3_CURVE (object);
  priv = curve->priv;

  if (priv->curve_data.d_point)
    g_free (priv->curve_data.d_point);
  if (priv->curve_data.d_cpoints)
    g_free (priv->curve_data.d_cpoints);

  G_OBJECT_CLASS (gtk3_curve_parent_class)->finalize (object);
}

void
gtk3_curve_set_curve_type(GtkWidget *widget, Gtk3CurveType new_type)
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;
  gfloat rx, dx;
  gint x, i;

  if (new_type == priv->curve_data.curve_type)
    return;

  gint width = gtk_widget_get_allocated_width(widget) - RADIUS * 2;
  gint height = gtk_widget_get_allocated_height(widget) - RADIUS * 2;

  if (width <= 1 || height <= 1) {
    priv->curve_data.curve_type = new_type;

    g_signal_emit(curve, curve_type_changed_signal, 0);
    g_object_notify(G_OBJECT(curve), "curve-type");

    if (gtk_widget_is_visible(widget))
      gtk_widget_queue_draw(widget);

    return;
  }

  if (new_type == GTK3_CURVE_TYPE_FREE) {

    gtk3_curve_interpolate(widget, width, height);
    priv->curve_data.curve_type = new_type;
  }
  else if (priv->curve_data.curve_type == GTK3_CURVE_TYPE_FREE) {

    g_free(priv->curve_data.d_cpoints);

    priv->curve_data.n_cpoints = 9;
    priv->curve_data.d_cpoints =
      g_malloc(priv->curve_data.n_cpoints *
               sizeof(*priv->curve_data.d_cpoints));

    if (!priv->curve_data.d_cpoints) {
      priv->curve_data.n_cpoints = 0;
      return;
    }

    if (!priv->curve_data.d_point || priv->curve_data.n_points <= 0) {

      priv->curve_data.n_cpoints = 2;

      priv->curve_data.d_cpoints[0].x = priv->min_x;
      priv->curve_data.d_cpoints[0].y = priv->min_y;
      priv->curve_data.d_cpoints[1].x = priv->max_x;
      priv->curve_data.d_cpoints[1].y = priv->max_y;
    } else {
      rx = 0.0f;

      if (priv->curve_data.n_cpoints > 1 &&
          priv->curve_data.n_points > 1)
      {
        dx = (gfloat)(priv->curve_data.n_points - 1) /
             (gfloat)(priv->curve_data.n_cpoints - 1);
      } else {
        dx = 0.0f;
      }

      for (i = 0; i < priv->curve_data.n_cpoints; ++i, rx += dx) {
        x = (gint)(rx + 0.5f);

        if (x < 0)
          x = 0;
        else if (x >= priv->curve_data.n_points)
          x = priv->curve_data.n_points - 1;

        priv->curve_data.d_cpoints[i].x =
          unproject(x,
                    priv->min_x,
                    priv->max_x,
                    priv->curve_data.n_points);

        priv->curve_data.d_cpoints[i].y =
          unproject(RADIUS + height - priv->curve_data.d_point[x].y,
                    priv->min_y,
                    priv->max_y,
                    height);

        if (priv->curve_data.d_cpoints[i].y < priv->min_y)
          priv->curve_data.d_cpoints[i].y = priv->min_y;
        else if (priv->curve_data.d_cpoints[i].y > priv->max_y)
          priv->curve_data.d_cpoints[i].y = priv->max_y;
      }

      priv->curve_data.d_cpoints[0].x = priv->min_x;
      priv->curve_data.d_cpoints[priv->curve_data.n_cpoints - 1].x = priv->max_x;
    }

    priv->curve_data.curve_type = new_type;
    gtk3_curve_interpolate(widget, width, height);
  }
  else {

    priv->curve_data.curve_type = new_type;
    gtk3_curve_interpolate(widget, width, height);
  }

  g_signal_emit(curve, curve_type_changed_signal, 0);
  g_object_notify(G_OBJECT(curve), "curve-type");

  DEBUG_INFO("set curve type\n");

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}

static void
gtk3_curve_set_property (GObject              *object,
                         guint                 prop_id,
                         const GValue         *value,
                         GParamSpec           *pspec)
{
  Gtk3Curve *curve = GTK3_CURVE (object);
  GtkWidget *widget = GTK_WIDGET(object);
  Gtk3CurvePrivate *priv = curve->priv;

  switch (prop_id)
    {
    case PROP_CURVE_TYPE:
      gtk3_curve_set_curve_type (widget, g_value_get_enum (value));
      break;

    case PROP_MIN_X:
      gtk3_curve_set_range (widget, g_value_get_float (value), priv->max_x,
                            priv->min_y, priv->max_y);
      break;

    case PROP_MAX_X:
      gtk3_curve_set_range (widget, priv->min_x, g_value_get_float (value),
                            priv->min_y, priv->max_y);
      break;

    case PROP_MIN_Y:
      gtk3_curve_set_range (widget, priv->min_x, priv->max_x,
                            g_value_get_float (value), priv->max_y);
      break;

    case PROP_MAX_Y:
      gtk3_curve_set_range (widget, priv->min_x, priv->max_x,
                            priv->min_y, g_value_get_float (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk3_curve_get_property (GObject              *object,
                         guint                 prop_id,
                         GValue               *value,
                         GParamSpec           *pspec)
{
  Gtk3Curve *curve = GTK3_CURVE (object);
  Gtk3CurvePrivate *priv = curve->priv;

  switch (prop_id)
    {
    case PROP_CURVE_TYPE:
      g_value_set_enum (value, priv->curve_data.curve_type);
      break;

    case PROP_MIN_X:
      g_value_set_float (value, priv->min_x);
      break;

    case PROP_MAX_X:
      g_value_set_float (value, priv->max_x);
      break;

    case PROP_MIN_Y:
      g_value_set_float (value, priv->min_y);
      break;

    case PROP_MAX_Y:
      g_value_set_float (value, priv->max_y);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

void
gtk3_curve_set_vector(GtkWidget *widget, int veclen, gfloat vector[])
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;
  Gtk3CurveType old_type;
  gfloat rx, dx, ry;
  gint i, height;

  if (!widget || !vector || veclen <= 0)
    return;

  DEBUG_INFO("set vector [S]\n");
  DEBUG_INFO("vector len [%d]\n", veclen);

  old_type = priv->curve_data.curve_type;
  priv->curve_data.curve_type = GTK3_CURVE_TYPE_FREE;

  if (priv->curve_data.d_point) {
    height = gtk_widget_get_allocated_height(widget) - RADIUS * 2;

    if (height <= 1) {
      height = priv->height;
      if (height <= 1)
        height = (gint)(priv->max_y - priv->min_y);
      if (height <= 1)
        height = 128;
    }
  } else {
    height = gtk_widget_get_allocated_height(widget) - RADIUS * 2;

    if (height <= 1) {
      height = (gint)(priv->max_y - priv->min_y);
      if (height <= 1)
        height = 128;
    }

    priv->curve_data.n_points = veclen;
    priv->curve_data.d_point =
      g_malloc(priv->curve_data.n_points *
               sizeof(priv->curve_data.d_point[0]));

    if (!priv->curve_data.d_point) {
      priv->curve_data.n_points = 0;
      priv->curve_data.curve_type = old_type;
      return;
    }
  }

  if (priv->curve_data.n_points <= 0) {
    priv->curve_data.curve_type = old_type;
    return;
  }

  priv->height = height;

  rx = 0.0f;

  if (priv->curve_data.n_points > 1 && veclen > 1)
    dx = (veclen - 1.0f) / (priv->curve_data.n_points - 1.0f);
  else
    dx = 0.0f;

  for (i = 0; i < priv->curve_data.n_points; ++i, rx += dx) {
    gint src = (gint)(rx + 0.5f);

    if (src < 0)
      src = 0;
    else if (src >= veclen)
      src = veclen - 1;

    ry = vector[src];

    if (ry > priv->max_y)
      ry = priv->max_y;
    else if (ry < priv->min_y)
      ry = priv->min_y;

    priv->curve_data.d_point[i].x = RADIUS + i;
    priv->curve_data.d_point[i].y =
      RADIUS + height - project(ry, priv->min_y, priv->max_y, height);
  }

  if (old_type != GTK3_CURVE_TYPE_FREE) {
    g_signal_emit(curve, curve_type_changed_signal, 0);
    g_object_notify(G_OBJECT(curve), "curve-type");
  }

  priv->width = priv->curve_data.n_points;

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);

  DEBUG_INFO("set vector [E]\n");
}

static void
gtk3_curve_interpolate(GtkWidget *widget, gint width, gint height)
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;

  if (width <= 1 || height <= 1)
    return;

  gfloat *vector = g_malloc(width * sizeof(vector[0]));

  if (!vector)
    return;

  gtk3_curve_get_vector(widget, width, vector);

  priv->width = width;
  priv->height = height;

  if (priv->curve_data.n_points != width) {
    priv->curve_data.n_points = width;
    g_free(priv->curve_data.d_point);
    priv->curve_data.d_point =
      g_malloc(priv->curve_data.n_points * sizeof(priv->curve_data.d_point[0]));

    if (!priv->curve_data.d_point) {
      priv->curve_data.n_points = 0;
      g_free(vector);
      return;
    }
  }

  for (int i = 0; i < width; ++i) {
    priv->curve_data.d_point[i].x = RADIUS + i;
    priv->curve_data.d_point[i].y =
      RADIUS + height - project(vector[i], priv->min_y, priv->max_y, height);
  }

  g_free(vector);
}

/*                          =====================                          */
/* ===========================   YE OLDE MATH   ========================== */
/*                          =====================                          */

/* Solve the tridiagonal equation system that determines the second
   derivatives for the interpolation points.  (Based on Numerical
   Recipies 2nd Edition.) 
   Guarded version   
*/
static void
spline_solve(int n, gfloat x[], gfloat y[], gfloat y2[])
{
  gfloat p, sig;
  gfloat *u;
  gint i, k;

  if (n <= 0 || !x || !y || !y2)
    return;

  if (n == 1) {
    y2[0] = 0.0f;
    return;
  }

  /*
   * Natural spline boundary condition.
   */
  y2[0] = 0.0f;
  y2[n - 1] = 0.0f;

  u = g_malloc((n - 1) * sizeof(u[0]));

  if (!u) {
    for (i = 0; i < n; ++i)
      y2[i] = 0.0f;
    return;
  }

  u[0] = 0.0f;

  for (i = 1; i < n - 1; ++i) {
    gfloat x_im1 = x[i - 1];
    gfloat x_i   = x[i];
    gfloat x_ip1 = x[i + 1];

    gfloat den_a = x_ip1 - x_im1;
    gfloat den_b = x_ip1 - x_i;
    gfloat den_c = x_i - x_im1;

    if (den_a <= 0.0f || den_b <= 0.0f || den_c <= 0.0f) {
      y2[i] = 0.0f;
      u[i] = 0.0f;
      continue;
    }

    sig = den_c / den_a;
    p = sig * y2[i - 1] + 2.0f;

    if (p == 0.0f) {
      y2[i] = 0.0f;
      u[i] = 0.0f;
      continue;
    }

    y2[i] = (sig - 1.0f) / p;

    u[i] = ((y[i + 1] - y[i]) / den_b) -
           ((y[i] - y[i - 1]) / den_c);

    u[i] = ((6.0f * u[i] / den_a) - sig * u[i - 1]) / p;
  }

  for (k = n - 2; k >= 0; --k)
    y2[k] = y2[k] * y2[k + 1] + u[k];

  g_free(u);
}

static gfloat
spline_eval(int n, gfloat x[], gfloat y[], gfloat y2[], gfloat val)
{
  gint k_lo, k_hi, k;
  gfloat h, b, a;

  if (n <= 0 || !x || !y || !y2)
    return 0.0f;

  if (n == 1)
    return y[0];

  if (val <= x[0])
    return y[0];

  if (val >= x[n - 1])
    return y[n - 1];

  k_lo = 0;
  k_hi = n - 1;

  while (k_hi - k_lo > 1) {
    k = (k_hi + k_lo) / 2;

    if (x[k] > val)
      k_hi = k;
    else
      k_lo = k;
  }

  h = x[k_hi] - x[k_lo];

  if (h <= 0.0f)
    return y[k_lo];

  a = (x[k_hi] - val) / h;
  b = (val - x[k_lo]) / h;

  return a * y[k_lo] + b * y[k_hi] +
         ((a * a * a - a) * y2[k_lo] +
          (b * b * b - b) * y2[k_hi]) * (h * h) / 6.0f;
}

static int project(gfloat value, gfloat min, gfloat max, int norm)
{
  if (norm <= 1)
    return 0;

  if (max == min)
    return 0;

  gfloat t = (value - min) / (max - min);

  if (t < 0.0f)
    t = 0.0f;
  else if (t > 1.0f)
    t = 1.0f;

  return (int) ((norm - 1) * t + 0.5f);
}

static gfloat unproject(gint value, gfloat min, gfloat max, int norm)
{
  if (norm <= 1)
    return min;

  if (max == min)
    return min;

  if (value < 0)
    value = 0;
  else if (value > norm - 1)
    value = norm - 1;

  return ((gfloat) value / (gfloat) (norm - 1)) * (max - min) + min;
}

void
gtk3_curve_set_gamma (GtkWidget *widget, gfloat gamma)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  gfloat x, one_over_gamma, height;
  Gtk3CurveType old_type;
  gint i;

  if (priv->curve_data.n_points < 2)
    return;

  old_type = priv->curve_data.curve_type;
  priv->curve_data.curve_type = GTK3_CURVE_TYPE_FREE;

  if (gamma <= 0)
    one_over_gamma = 1.0;
  else
    one_over_gamma = 1.0 / gamma;
  height = priv->height;
  for (i = 0; i < priv->curve_data.n_points; ++i)
    {
      x = (gfloat) i / (priv->curve_data.n_points - 1);
      priv->curve_data.d_point[i].x = RADIUS + i;
      priv->curve_data.d_point[i].y =
        RADIUS + (height * (1.0 - pow (x, one_over_gamma)) + 0.5);
    }

  if (old_type != GTK3_CURVE_TYPE_FREE)
    g_signal_emit (curve, curve_type_changed_signal, 0);

  priv->width = priv->curve_data.n_points;

  DEBUG_INFO("set gamma \n");
  if (gtk_widget_is_visible (GTK_WIDGET (curve)))
    {
      DEBUG_INFO("queue draw \n");
      gtk_widget_queue_draw (GTK_WIDGET (curve));
    }
}

void
gtk3_curve_set_fps(GtkWidget *widget, gdouble fps)
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;

  if (fps <= 1.0)
    fps = 0.0;

  if (priv->fps == fps)
    return;

  priv->fps = fps;

  gtk3_curve_update_x_grid(priv);

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}

void
gtk3_curve_set_x_lo (GtkWidget *widget,
                      gfloat    min_x)
{
    Gtk3Curve *curve = GTK3_CURVE(widget);
    Gtk3CurvePrivate *priv = curve->priv;
    priv->xaxis_lo = min_x;

    DEBUG_INFO("set_x_lo %f\n", priv->xaxis_lo);
    gtk3_curve_set_range (widget, min_x, priv->max_x, priv->min_y, priv->max_y);
}

void
gtk3_curve_set_x_hi (GtkWidget *widget,
                      gfloat    max_x)
{
    Gtk3Curve *curve = GTK3_CURVE(widget);
    Gtk3CurvePrivate *priv = curve->priv;
    priv->xaxis_hi = max_x;

    DEBUG_INFO("set_x_hi %f\n", priv->xaxis_hi);
    gtk3_curve_set_range (widget, priv->min_x, max_x, priv->min_y, priv->max_y);
}

void
gtk3_curve_set_range(GtkWidget *widget,
                     gfloat    min_x,
                     gfloat    max_x,
                     gfloat    min_y,
                     gfloat    max_y)
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;

  if (max_x <= min_x)
    max_x = min_x + 1.0f;

  if (max_y <= min_y)
    max_y = min_y + 1.0f;

  g_object_freeze_notify(G_OBJECT(curve));

  if (priv->min_x != min_x) {
    priv->min_x = min_x;
    g_object_notify(G_OBJECT(curve), "min-x");
  }

  if (priv->max_x != max_x) {
    priv->max_x = max_x;
    g_object_notify(G_OBJECT(curve), "max-x");
  }

  if (priv->min_y != min_y) {
    priv->min_y = min_y;
    g_object_notify(G_OBJECT(curve), "min-y");
  }

  if (priv->max_y != max_y) {
    priv->max_y = max_y;
    g_object_notify(G_OBJECT(curve), "max-y");
  }

  g_object_thaw_notify(G_OBJECT(curve));

  priv->y_grid_resolution = gtk3_curve_auto_y_grid_resolution(priv->min_y,priv->max_y);
  gtk3_curve_update_x_grid(priv);
  
  gtk3_curve_reset_vector(widget);
}

void
gtk3_curve_get_vector(GtkWidget *widget, int veclen, gfloat vector[])
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;
  gfloat rx, ry, dx, dy, min_x, delta_x;
  gfloat *mem, *xv, *yv, *y2v, prev;
  gint dst, i, x, next;
  gint num_active_ctlpoints = 0;
  gint first_active = -1;

  if (!vector || veclen <= 0)
    return;

  if (veclen == 1) {
    vector[0] = priv->min_y;
    return;
  }

  min_x = priv->min_x;

  if (priv->curve_data.curve_type != GTK3_CURVE_TYPE_FREE) {

    prev = min_x - 1.0f;

    for (i = 0; i < priv->curve_data.n_cpoints; ++i) {
      if (priv->curve_data.d_cpoints[i].x > prev) {
        if (first_active < 0)
          first_active = i;

        prev = priv->curve_data.d_cpoints[i].x;
        ++num_active_ctlpoints;
      }
    }

    if (num_active_ctlpoints < 2) {
      if (num_active_ctlpoints > 0)
        ry = priv->curve_data.d_cpoints[first_active].y;
      else
        ry = priv->min_y;

      if (ry < priv->min_y)
        ry = priv->min_y;
      else if (ry > priv->max_y)
        ry = priv->max_y;

      for (x = 0; x < veclen; ++x)
        vector[x] = ry;

      return;
    }
  }

  switch (priv->curve_data.curve_type) {
    default:
    case GTK3_CURVE_TYPE_SPLINE:
      mem = g_malloc(3 * num_active_ctlpoints * sizeof(gfloat));

      if (!mem) {
        for (x = 0; x < veclen; ++x)
          vector[x] = priv->min_y;
        return;
      }

      xv  = mem;
      yv  = mem + num_active_ctlpoints;
      y2v = mem + 2 * num_active_ctlpoints;

      prev = min_x - 1.0f;

      for (i = dst = 0; i < priv->curve_data.n_cpoints; ++i) {
        if (priv->curve_data.d_cpoints[i].x > prev) {
          prev = priv->curve_data.d_cpoints[i].x;

          xv[dst] = priv->curve_data.d_cpoints[i].x;
          yv[dst] = priv->curve_data.d_cpoints[i].y;

          if (yv[dst] < priv->min_y)
            yv[dst] = priv->min_y;
          else if (yv[dst] > priv->max_y)
            yv[dst] = priv->max_y;

          ++dst;
        }
      }

      spline_solve(num_active_ctlpoints, xv, yv, y2v);

      rx = min_x;
      dx = (priv->max_x - min_x) / (veclen - 1);

      for (x = 0; x < veclen; ++x, rx += dx) {
        ry = spline_eval(num_active_ctlpoints, xv, yv, y2v, rx);

        if (ry < priv->min_y)
          ry = priv->min_y;
        else if (ry > priv->max_y)
          ry = priv->max_y;

        vector[x] = ry;
      }

      g_free(mem);
      break;

    case GTK3_CURVE_TYPE_LINEAR:
    {
      gfloat *mem = g_malloc(2 * num_active_ctlpoints * sizeof(gfloat));

      if (!mem) {
        for (x = 0; x < veclen; ++x)
          vector[x] = priv->min_y;
        return;
      }

      gfloat *xv = mem;
      gfloat *yv = mem + num_active_ctlpoints;

      prev = min_x - 1.0f;

      for (i = dst = 0; i < priv->curve_data.n_cpoints; ++i) {
        if (priv->curve_data.d_cpoints[i].x > prev) {
          prev = priv->curve_data.d_cpoints[i].x;

          xv[dst] = priv->curve_data.d_cpoints[i].x;
          yv[dst] = priv->curve_data.d_cpoints[i].y;

          if (yv[dst] < priv->min_y)
            yv[dst] = priv->min_y;
          else if (yv[dst] > priv->max_y)
            yv[dst] = priv->max_y;

          ++dst;
        }
      }

      xv[0] = priv->min_x;
      xv[num_active_ctlpoints - 1] = priv->max_x;

      gint seg = 0;

      for (x = 0; x < veclen; ++x) {
        if (veclen > 1)
          rx = priv->min_x +
              ((priv->max_x - priv->min_x) * (gfloat)x) / (gfloat)(veclen - 1);
        else
          rx = priv->min_x;

        while (seg + 1 < num_active_ctlpoints - 1 &&
              rx > xv[seg + 1])
        {
          ++seg;
        }

        if (rx <= xv[0]) {
          ry = yv[0];
        }
        else if (rx >= xv[num_active_ctlpoints - 1]) {
          ry = yv[num_active_ctlpoints - 1];
        }
        else {
          gfloat x0 = xv[seg];
          gfloat x1 = xv[seg + 1];
          gfloat y0 = yv[seg];
          gfloat y1 = yv[seg + 1];
          gfloat den = x1 - x0;

          if (den <= 0.0f) {
            ry = y0;
          } else {
            gfloat t = (rx - x0) / den;

            if (t < 0.0f)
              t = 0.0f;
            else if (t > 1.0f)
              t = 1.0f;

            ry = y0 + ((y1 - y0) * t);
          }
        }

        if (ry < priv->min_y)
          ry = priv->min_y;
        else if (ry > priv->max_y)
          ry = priv->max_y;

        vector[x] = ry;
      }

      vector[0] = yv[0];
      vector[veclen - 1] = yv[num_active_ctlpoints - 1];

      g_free(mem);
      break;
    }

    case GTK3_CURVE_TYPE_FREE:

      if (priv->curve_data.d_point &&
          priv->curve_data.n_points > 0 &&
          priv->height > 1)
      {
        rx = 0.0f;
        dx = priv->curve_data.n_points / (gfloat) veclen;

        for (x = 0; x < veclen; ++x, rx += dx) {
          gint idx = (gint) rx;

          if (idx < 0)
            idx = 0;
          else if (idx >= priv->curve_data.n_points)
            idx = priv->curve_data.n_points - 1;

          ry = unproject(RADIUS + priv->height - priv->curve_data.d_point[idx].y,
                         priv->min_y,
                         priv->max_y,
                         priv->height);

          if (ry < priv->min_y)
            ry = priv->min_y;
          else if (ry > priv->max_y)
            ry = priv->max_y;

          vector[x] = ry;
        }
      } else {
        for (x = 0; x < veclen; ++x)
          vector[x] = priv->min_y;
      }
      break;
  }
}

void gtk3_curve_set_grid_resolution(GtkWidget *widget, gint grid_resolution)
{
    Gtk3Curve *curve = GTK3_CURVE(widget);
    Gtk3CurvePrivate *priv = curve->priv;

    (void) grid_resolution;

    gtk3_curve_update_x_grid(priv);

    if (gtk_widget_is_visible(widget))
        gtk_widget_queue_draw(widget);
}

void gtk3_curve_set_color_background (GtkWidget *widget, Gtk3CurveColor color)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  priv->background.red = color.red;
  priv->background.green = color.green;
  priv->background.blue = color.blue;
  priv->background.alpha = color.alpha;
  if (gtk_widget_is_visible (widget))
    {
      DEBUG_INFO("queue draw\n");
      gtk_widget_queue_draw (widget);
    }
}

void gtk3_curve_set_color_background_rgba (GtkWidget *widget, gfloat r,
                                           gfloat g, gfloat b, gfloat a)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  priv->background.red = r;
  priv->background.green = g;
  priv->background.blue = b;
  priv->background.alpha = a;
  if (gtk_widget_is_visible (widget))
    {
      DEBUG_INFO("queue draw\n");
      gtk_widget_queue_draw (widget);
    }
}

Gtk3CurveColor gtk3_curve_get_color_background (GtkWidget *widget)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  return priv->background;
}

void gtk3_curve_set_color_grid (GtkWidget *widget, Gtk3CurveColor color)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  priv->grid.red = color.red;
  priv->grid.green = color.green;
  priv->grid.blue = color.blue;
  priv->grid.alpha = color.alpha;
  if (gtk_widget_is_visible (widget))
    {
      DEBUG_INFO("queue draw\n");
      gtk_widget_queue_draw (widget);
    }
}

void gtk3_curve_set_color_grid_rgba (GtkWidget *widget, gfloat r,
                                     gfloat g, gfloat b, gfloat a)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  priv->grid.red = r;
  priv->grid.green = g;
  priv->grid.blue = b;
  priv->grid.alpha = a;
  if (gtk_widget_is_visible (widget))
    {
      DEBUG_INFO("queue draw\n");
      gtk_widget_queue_draw (widget);
    }
}

Gtk3CurveColor gtk3_curve_get_color_grid (GtkWidget *widget)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  return priv->grid;
}

void gtk3_curve_set_color_curve (GtkWidget *widget, Gtk3CurveColor color)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  priv->curve.red = color.red;
  priv->curve.green = color.green;
  priv->curve.blue = color.blue;
  priv->curve.alpha = color.alpha;
  if (gtk_widget_is_visible (widget))
    {
      DEBUG_INFO("queue draw\n");
      gtk_widget_queue_draw (widget);
    }
}

void gtk3_curve_set_color_curve_rgba (GtkWidget *widget, gfloat r,
                                      gfloat g, gfloat b, gfloat a)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  priv->curve.red = r;
  priv->curve.green = g;
  priv->curve.blue = b;
  priv->curve.alpha = a;
  if (gtk_widget_is_visible (widget))
    {
      DEBUG_INFO("queue draw\n");
      gtk_widget_queue_draw (widget);
    }
}

Gtk3CurveColor gtk3_curve_get_color_curve (GtkWidget *widget)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  return priv->curve;
}

void gtk3_curve_set_color_cpoint (GtkWidget *widget, Gtk3CurveColor color)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  priv->cpoint.red = color.red;
  priv->cpoint.green = color.green;
  priv->cpoint.blue = color.blue;
  priv->cpoint.alpha = color.alpha;
  if (gtk_widget_is_visible (widget))
    {
      DEBUG_INFO("queue draw\n");
      gtk_widget_queue_draw (widget);
    }
}

void gtk3_curve_set_color_cpoint_rgba (GtkWidget *widget, gfloat r,
                                       gfloat g, gfloat b, gfloat a)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  priv->cpoint.red = r;
  priv->cpoint.green = g;
  priv->cpoint.blue = b;
  priv->cpoint.alpha = a;
  if (gtk_widget_is_visible (widget))
    {
      DEBUG_INFO("queue draw\n");
      gtk_widget_queue_draw (widget);
    }
}

Gtk3CurveColor gtk3_curve_get_color_cpoint (GtkWidget *widget)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  return priv->cpoint;
}



Gtk3CurveType gtk3_curve_get_curve_type (GtkWidget *widget)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  return  priv->curve_data.curve_type;
}

void gtk3_curve_set_use_theme_background(GtkWidget *widget, gboolean use)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  priv->use_bg_theme = use;
}

gboolean gtk3_curve_get_use_theme_background(GtkWidget *widget)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  return priv->use_bg_theme;
}

void gtk3_curve_set_grid_size(GtkWidget *widget, Gtk3CurveGridSize size)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  priv->grid_size = size;
}

Gtk3CurveGridSize gtk3_curve_get_grid_size(GtkWidget *widget)
{
  Gtk3Curve *curve = GTK3_CURVE (widget);
  Gtk3CurvePrivate *priv = curve->priv;
  return priv->grid_size;
}

void
gtk3_curve_reset(GtkWidget *widget)
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;
  Gtk3CurveType old_type = priv->curve_data.curve_type;

  priv->curve_data.curve_type = GTK3_CURVE_TYPE_SPLINE;

  priv->grid_resolution = 10.0f;
  priv->y_grid_resolution = gtk3_curve_auto_y_grid_resolution(priv->min_y,priv->max_y);
  gtk3_curve_update_x_grid(priv);
  priv->xaxis_lo = 0.0f;
  priv->xaxis_hi = 1.0f;
  priv->yaxis_lo = 0.0f;
  priv->yaxis_hi = 1.0f;
  priv->current_position = 0.0;
  priv->marker_x = 0.0;
  priv->marker_y = 0.0;
  priv->marker_value = 0.0f;
  priv->marker_hover = FALSE;
  priv->hover_cpoint = -1;
  priv->hover_cpoint_x = 0.0;
  priv->hover_cpoint_y = 0.0;
  priv->hover_cpoint_value = 0.0f;
  priv->hover_cpoint_position = 0.0f;

  gtk3_curve_reset_vector(widget);

  if (old_type != GTK3_CURVE_TYPE_SPLINE) {
    g_signal_emit(curve, curve_type_changed_signal, 0);
    g_object_notify(G_OBJECT(curve), "curve-type");
  }

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}

void
gtk3_curve_save(Gtk3CurveData *data, gchar *filename) {
  // TODO - add code here
}


Gtk3CurveData
gtk3_curve_load(gchar *filename) {
  // TODO - add code here
  Gtk3CurveData empty={};
  return empty;
}

static void gtk3_curve_create_layouts (GtkWidget *widget) {
}

static void
gtk3_curve_reset_vector(GtkWidget *widget)
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;
  gint width, height;

  g_free(priv->curve_data.d_cpoints);

  priv->curve_data.n_cpoints = 2;
  priv->curve_data.d_cpoints =
    g_malloc(2 * sizeof(priv->curve_data.d_cpoints[0]));

  if (!priv->curve_data.d_cpoints) {
    priv->curve_data.n_cpoints = 0;
    return;
  }

  priv->curve_data.d_cpoints[0].x = priv->min_x;
  priv->curve_data.d_cpoints[0].y = priv->min_y;
  priv->curve_data.d_cpoints[1].x = priv->max_x;
  priv->curve_data.d_cpoints[1].y = priv->max_y;

  width = gtk_widget_get_allocated_width(widget) - RADIUS * 2;
  height = gtk_widget_get_allocated_height(widget) - RADIUS * 2;

  if (width <= 1 || height <= 1)
    return;

  if (priv->curve_data.curve_type == GTK3_CURVE_TYPE_FREE) {
    Gtk3CurveType old_type = priv->curve_data.curve_type;

    priv->curve_data.curve_type = GTK3_CURVE_TYPE_LINEAR;
    gtk3_curve_interpolate(widget, width, height);
    priv->curve_data.curve_type = old_type;
  } else {
    gtk3_curve_interpolate(widget, width, height);
  }

  if (gtk_widget_is_visible(GTK_WIDGET(curve)))
    gtk_widget_queue_draw(GTK_WIDGET(curve));
}

void
gtk3_curve_set_position(GtkWidget *widget, gdouble pos)
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;

  if (pos < priv->min_x)
    pos = priv->min_x;
  else if (pos > priv->max_x)
    pos = priv->max_x;

  if (priv->current_position == pos)
    return;

  priv->current_position = pos;

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}