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
#include "curve.h"
#include "utils-gtk.h"

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

#ifndef GDK_SMOOTH_SCROLL_MASK
#define GDK_SMOOTH_SCROLL_MASK 0
#endif

#define RADIUS            3
#define MIN_DISTANCE      8
#define GRAPH_MASK       (GDK_EXPOSURE_MASK | \
                          GDK_POINTER_MOTION_MASK | \
                          GDK_POINTER_MOTION_HINT_MASK | \
                          GDK_ENTER_NOTIFY_MASK | \
                          GDK_LEAVE_NOTIFY_MASK | \
                          GDK_BUTTON_PRESS_MASK | \
                          GDK_BUTTON_RELEASE_MASK | \
                          GDK_BUTTON1_MOTION_MASK | \
                          GDK_SCROLL_MASK | \
                          GDK_SMOOTH_SCROLL_MASK)

#define CURVE_X_LABEL_HEIGHT_MIN 18
#define CURVE_X_NAV_HEIGHT_MIN   18
#define CURVE_LEGEND_HEIGHT_MIN  24
#define CURVE_LIVE_TRACE_Y_PAD 2.0

#ifndef GTK3_CURVE_LIVE_TRACE_MAX
#define GTK3_CURVE_LIVE_TRACE_MAX 10
#endif
#ifndef GTK3_CURVE_LIVE_TRACE_LEN
#define GTK3_CURVE_LIVE_TRACE_LEN 8192
#endif
#ifndef GTK3_CURVE_LIVE_TRACE_LABEL
#define GTK3_CURVE_LIVE_TRACE_LABEL 32
#endif

struct _Gtk3CurvePrivate
{
  GdkWindow *event_window;
  GList *children;

  gint cursor_type;

  gfloat min_x;
  gfloat max_x;
  gfloat min_y;
  gfloat max_y;

  gfloat timeline_min_x;
  gfloat timeline_max_x;

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
  gboolean marker_live_trace;
  Gtk3CurveColor marker_live_color;

  gint x_grid_step;

  gdouble fps;

  gint     hover_cpoint;
  gdouble  hover_cpoint_x;
  gdouble  hover_cpoint_y;
  gfloat   hover_cpoint_value;
  gfloat   hover_cpoint_position;


  gfloat  *frame_vector;
  gint     frame_vector_len;
  gfloat   frame_vector_min_x;
  gfloat   frame_vector_max_x;

  gboolean live_trace_enabled;
  gboolean live_trace_user_override;
  gint     live_trace_pos;
  gint     live_trace_count;
  gboolean live_trace_active[GTK3_CURVE_LIVE_TRACE_MAX];
  gfloat (*live_trace_values)[GTK3_CURVE_LIVE_TRACE_LEN];
  gfloat (*live_trace_x)[GTK3_CURVE_LIVE_TRACE_LEN];
  gboolean (*live_trace_point_used)[GTK3_CURVE_LIVE_TRACE_LEN];
  gboolean *live_trace_slot_used;
  gint     live_trace_last_slot;
  gint     live_trace_last_slot_for[GTK3_CURVE_LIVE_TRACE_MAX];
  gboolean live_trace_have_source_x;
  gfloat   live_trace_last_source_x;
  Gtk3CurveColor live_trace_color[GTK3_CURVE_LIVE_TRACE_MAX];
  gchar    live_trace_label[GTK3_CURVE_LIVE_TRACE_MAX][GTK3_CURVE_LIVE_TRACE_LABEL];
  Gtk3CurveLiveTraceDomain live_trace_domain;
  gfloat   live_trace_local_min_x;
  gfloat   live_trace_local_max_x;
  gfloat   live_trace_view_min_x;
  gfloat   live_trace_view_max_x;
  gfloat   live_trace_auto_x;
  gfloat   live_trace_last_input_x;
  gfloat   live_trace_pending_x;
  gboolean live_trace_have_last_input;
  gboolean live_trace_pending_valid;
  gboolean live_trace_clear_on_next_push;
  gboolean live_trace_dot_enabled;
  gfloat   live_trace_dot_x;
  gfloat   live_trace_dot_base_value;
  gfloat   live_trace_dot_value;
  Gtk3CurveColor live_trace_dot_color;
  gchar    live_trace_dot_label[GTK3_CURVE_LIVE_TRACE_LABEL];
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
static gboolean gtk3_curve_scroll_event      (GtkWidget            *widget,
                                             GdkEventScroll       *event);
static void gtk3_curve_screen_changed       (GtkWidget            *widget,
                                             GdkScreen            *prev_screen);
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
static gdouble projectd                     (gfloat                value,
                                             gfloat                min,
                                             gfloat                max,
                                             int                   norm);
static gfloat unproject                     (gint                  value,
                                             gfloat                min,
                                             gfloat                max,
                                             int                   norm);
static gfloat unprojectd                    (gdouble               value,
                                             gfloat                min,
                                             gfloat                max,
                                             int                   norm);
static gint gtk3_curve_project_cpoint_x      (Gtk3CurvePrivate     *priv,
                                             gfloat                value,
                                             gint                  width);
static gint gtk3_curve_project_cpoint_y      (Gtk3CurvePrivate     *priv,
                                             gfloat                value,
                                             gint                  height);
static gfloat gtk3_curve_unproject_cpoint_x  (Gtk3CurvePrivate     *priv,
                                             gint                  value,
                                             gint                  width);
static gfloat gtk3_curve_unproject_cpoint_y  (Gtk3CurvePrivate     *priv,
                                             gint                  value,
                                             gint                  height);
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
static void gtk3_curve_draw_live_traces     (GtkWidget           *widget,
                                             cairo_t             *cr,
                                             gint                 allocation_width,
                                             gint                 graph_width,
                                             gint                 graph_height,
                                             gboolean             as_underlay);
static gboolean gtk3_curve_live_trace_use_local_axis(Gtk3CurvePrivate *priv);
static gfloat gtk3_curve_live_axis_min_x(Gtk3CurvePrivate *priv);
static gfloat gtk3_curve_live_axis_max_x(Gtk3CurvePrivate *priv);
static gfloat gtk3_curve_live_trace_jump_threshold(Gtk3CurvePrivate *priv);
static gfloat gtk3_curve_live_trace_draw_jump_threshold(Gtk3CurvePrivate *priv);
static Gtk3CurvePrivate *gtk3_curve_live_trace_priv(GtkWidget *widget);
static void gtk3_curve_live_trace_clear_samples(Gtk3CurvePrivate *priv);
static void gtk3_curve_live_trace_disable_for_user_edit(GtkWidget *widget);
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

static void gtk3_curve_draw_hover_label(GtkWidget *widget,
                                        cairo_t   *cr,
                                        gdouble    px,
                                        gdouble    py,
                                        gfloat     position,
                                        gfloat     value,
                                        gint       allocation_width,
                                        gint       allocation_height);
static void gtk3_curve_draw_cursor_legend(GtkWidget *widget,
                                          cairo_t   *cr,
                                          gint       allocation_width,
                                          gint       allocation_height,
                                          gint       graph_width,
                                          gint       graph_height);

static Gtk3CurveColor gtk3_curve_legend_background_color(GtkWidget *widget,
                                                             Gtk3CurvePrivate *priv,
                                                             gfloat alpha);
static Gtk3CurveColor gtk3_curve_legend_border_color(GtkWidget *widget,
                                                     Gtk3CurvePrivate *priv,
                                                     gfloat alpha);
static Gtk3CurveColor gtk3_curve_legend_text_color(GtkWidget *widget,
                                                   Gtk3CurvePrivate *priv,
                                                   gfloat alpha);

static const char *gtk3_curve_type_name(Gtk3CurveType type);

static void gtk3_curve_update_x_grid(Gtk3CurvePrivate *priv);
static void gtk3_curve_draw_x_zoom_indicators(GtkWidget *widget,
                                              cairo_t   *cr,
                                              gint       allocation_width,
                                              gint       graph_width,
                                              gint       graph_height);
static gboolean gtk3_curve_x_nav_hit(GtkWidget *widget,
                                     gint       tx,
                                     gint       ty,
                                     gboolean  *hit_left,
                                     gboolean  *hit_right,
                                     gboolean  *in_nav);

static gfloat
scale_pos_value(Gtk3CurvePrivate *priv, gfloat x, gfloat width);

void gtk3_curve_zoom_x(GtkWidget *widget, gfloat center_x, gfloat factor);
void gtk3_curve_pan_x(GtkWidget *widget, gfloat delta);

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
        NULL,
        NULL,
        (GClassInitFunc) gtk3_curve_class_init,
        NULL,
        NULL,
        sizeof (Gtk3Curve),
        0,
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

static gboolean
gtk3_curve_has_curve_data(Gtk3CurvePrivate *priv)
{
    return ((priv->curve_data.d_point &&
             priv->curve_data.n_points > 0) ||
            (priv->curve_data.d_cpoints &&
             priv->curve_data.n_cpoints > 0));
}

static const char *
gtk3_curve_type_name(Gtk3CurveType type)
{
  switch (type) {
    case GTK3_CURVE_TYPE_LINEAR: return "Linear";
    case GTK3_CURVE_TYPE_SPLINE: return "Spline";
    case GTK3_CURVE_TYPE_FREE:   return "Freehand";
    default:                     return "Curve";
  }
}



static double
gtk3_curve_font_points(GtkWidget *widget)
{
  PangoContext *context;
  const PangoFontDescription *font;
  gint size;
  gdouble points;

  if (!widget)
    return 10.0;

  context = gtk_widget_get_pango_context(widget);
  font = context ? pango_context_get_font_description(context) : NULL;
  size = font ? pango_font_description_get_size(font) : 0;
  if (size <= 0)
    return 10.0;

  points = (gdouble)size / PANGO_SCALE;
  if (pango_font_description_get_size_is_absolute(font))
    points *= 72.0 / 96.0;
  return CLAMP(points, 6.0, 32.0);
}

static double
gtk3_curve_font_px(GtkWidget *widget, gdouble scale)
{
  return MAX(7.0,
             gtk3_curve_font_points(widget) *
             (96.0 / 72.0) * scale);
}

static const char *
gtk3_curve_font_family(GtkWidget *widget)
{
  PangoContext *context;
  const PangoFontDescription *font;
  const gchar *family;

  if (!widget)
    return "Sans";

  context = gtk_widget_get_pango_context(widget);
  font = context ? pango_context_get_font_description(context) : NULL;
  family = font ? pango_font_description_get_family(font) : NULL;
  return (family && family[0]) ? family : "Sans";
}

static gint
gtk3_curve_x_label_height(GtkWidget *widget)
{
  return MAX(CURVE_X_LABEL_HEIGHT_MIN,
             (gint)ceil(gtk3_curve_font_px(widget, 0.82) + 7.0));
}

static gint
gtk3_curve_x_nav_height(GtkWidget *widget)
{
  return MAX(CURVE_X_NAV_HEIGHT_MIN,
             (gint)ceil(gtk3_curve_font_px(widget, 0.82) + 7.0));
}

static gint
gtk3_curve_legend_height(GtkWidget *widget)
{
  return MAX(CURVE_LEGEND_HEIGHT_MIN,
             (gint)ceil(gtk3_curve_font_px(widget, 0.82) + 13.0));
}

static gint
gtk3_curve_graph_width_from_allocation(gint allocation_width)
{
  return allocation_width - (RADIUS * 2);
}

static gint
gtk3_curve_graph_height_from_allocation(GtkWidget *widget, gint allocation_height)
{
  return allocation_height - (RADIUS * 2)
                           - gtk3_curve_x_label_height(widget)
                           - gtk3_curve_x_nav_height(widget)
                           - gtk3_curve_legend_height(widget);
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
  widget_class->scroll_event = gtk3_curve_scroll_event;

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
                                       "Curve interpolation mode: linear, spline, or freehand",
                                       GTK3_TYPE_CURVE_TYPE,
                                       GTK3_CURVE_TYPE_SPLINE,
                                       GTK3_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
                                   PROP_MIN_X,
                                   g_param_spec_float ("min-x",
                                       "Minimum X",
                                       "Minimum X value",
                                       -G_MAXFLOAT,
                                       G_MAXFLOAT,
                                       0.0,
                                       GTK3_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
                                   PROP_MAX_X,
                                   g_param_spec_float ("max-x",
                                       "Maximum X",
                                       "Maximum X value",
                                       -G_MAXFLOAT,
                                       G_MAXFLOAT,
                                       1.0,
                                       GTK3_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
                                   PROP_MIN_Y,
                                   g_param_spec_float ("min-y",
                                       "Minimum Y",
                                       "Minimum Y value",
                                       -G_MAXFLOAT,
                                       G_MAXFLOAT,
                                       0.0,
                                       GTK3_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
                                   PROP_MAX_Y,
                                   g_param_spec_float ("max-y",
                                       "Maximum Y",
                                       "Maximum Y value",
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
  gtk_widget_add_events(GTK_WIDGET(self), GRAPH_MASK);

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
  priv->timeline_min_x = priv->min_x;
  priv->timeline_max_x = priv->max_x;

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
  priv->marker_live_trace = FALSE;
  memset(&priv->marker_live_color, 0, sizeof(priv->marker_live_color));

  priv->state = FALSE;
  priv->in_curve = FALSE;
  priv->last = 0;
  priv->width = 0;

  priv->hover_cpoint = -1;
  priv->hover_cpoint_x = 0.0;
  priv->hover_cpoint_y = 0.0;
  priv->hover_cpoint_value = 0.0f;
  priv->hover_cpoint_position = 0.0f;

  priv->frame_vector = NULL;
  priv->frame_vector_len = 0;
  priv->frame_vector_min_x = priv->min_x;
  priv->frame_vector_max_x = priv->max_x;

  priv->live_trace_values = (gfloat (*)[GTK3_CURVE_LIVE_TRACE_LEN])
    g_new0(gfloat, GTK3_CURVE_LIVE_TRACE_MAX * GTK3_CURVE_LIVE_TRACE_LEN);
  priv->live_trace_x = (gfloat (*)[GTK3_CURVE_LIVE_TRACE_LEN])
    g_new0(gfloat, GTK3_CURVE_LIVE_TRACE_MAX * GTK3_CURVE_LIVE_TRACE_LEN);
  priv->live_trace_point_used = (gboolean (*)[GTK3_CURVE_LIVE_TRACE_LEN])
    g_new0(gboolean, GTK3_CURVE_LIVE_TRACE_MAX * GTK3_CURVE_LIVE_TRACE_LEN);
  priv->live_trace_slot_used = g_new0(gboolean, GTK3_CURVE_LIVE_TRACE_LEN);

  priv->live_trace_enabled = FALSE;
  priv->live_trace_user_override = FALSE;
  priv->live_trace_pos = 0;
  priv->live_trace_count = 0;
  memset(priv->live_trace_active, 0, sizeof(priv->live_trace_active));
  priv->live_trace_last_slot = -1;
  for (gint t = 0; t < GTK3_CURVE_LIVE_TRACE_MAX; t++)
    priv->live_trace_last_slot_for[t] = -1;
  priv->live_trace_have_source_x = FALSE;
  priv->live_trace_last_source_x = 0.0f;
  memset(priv->live_trace_label, 0, sizeof(priv->live_trace_label));
  priv->live_trace_domain = GTK3_CURVE_LIVE_TRACE_DOMAIN_FRAME;
  priv->live_trace_local_min_x = 0.0f;
  priv->live_trace_local_max_x = (gfloat)(GTK3_CURVE_LIVE_TRACE_LEN - 1);
  priv->live_trace_view_min_x = priv->live_trace_local_min_x;
  priv->live_trace_view_max_x = priv->live_trace_local_max_x;
  priv->live_trace_auto_x = NAN;
  priv->live_trace_last_input_x = 0.0f;
  priv->live_trace_pending_x = NAN;
  priv->live_trace_have_last_input = FALSE;
  priv->live_trace_pending_valid = FALSE;
  priv->live_trace_clear_on_next_push = FALSE;
  priv->live_trace_have_source_x = FALSE;
  priv->live_trace_last_source_x = 0.0f;
  priv->live_trace_dot_enabled = FALSE;
  priv->live_trace_dot_x = 0.0f;
  priv->live_trace_dot_base_value = 0.0f;
  priv->live_trace_dot_value = 0.0f;
  memset(&priv->live_trace_dot_color, 0, sizeof(priv->live_trace_dot_color));
  priv->live_trace_dot_label[0] = '\0';

  gtk3_curve_set_color_background_rgba (GTK_WIDGET(self), 1.0, 1.0, 1.0, 1.0);
  gtk3_curve_set_color_curve_rgba (GTK_WIDGET(self), 0.0, 0.0, 0.0, 1.0);
  gtk3_curve_set_color_grid_rgba (GTK_WIDGET(self), 0.0, 0.0, 0.0, 1.0);
  gtk3_curve_set_color_cpoint_rgba (GTK_WIDGET(self), 0.2, 0.2, 0.2, 1.0);


  DEBUG_INFO("init [E]\n");
}

static gboolean
gtk3_curve_handle_scroll(GtkWidget *widget, GdkEventScroll *ev)
{
  Gtk3CurvePrivate *priv;
  GtkAllocation allocation;
  gint width;
  gfloat span;
  gfloat center;
  gfloat delta;
  gfloat factor;
  gdouble ex;
  gboolean scroll_up;
  gboolean scroll_down;

  if (!widget || !ev)
    return FALSE;

  priv = GTK3_CURVE(widget)->priv;

  gtk_widget_get_allocation(widget, &allocation);
  width = gtk3_curve_graph_width_from_allocation(allocation.width);

  if (width <= 1)
    return TRUE;

  ex = ev->x - RADIUS;
  if (ex < 0.0)
    ex = 0.0;
  else if (ex > (gdouble)(width - 1))
    ex = (gdouble)(width - 1);

  if (gtk3_curve_live_trace_use_local_axis(priv)) {
    gfloat axis_min_x = gtk3_curve_live_axis_min_x(priv);
    gfloat axis_max_x = gtk3_curve_live_axis_max_x(priv);
    gdouble t = ex / MAX(1.0, (gdouble)(width - 1));
    center = axis_min_x + (gfloat)(t * (axis_max_x - axis_min_x));
    span = axis_max_x - axis_min_x;
  } else {
    center = scale_pos_value(priv, (gfloat) ex, (gfloat) width);
    span = priv->max_x - priv->min_x;
  }

  if (span <= 0.0f)
    span = 1.0f;

  scroll_up = (ev->direction == GDK_SCROLL_UP ||
               ev->direction == GDK_SCROLL_LEFT ||
               (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y < 0.0));

  scroll_down = (ev->direction == GDK_SCROLL_DOWN ||
                 ev->direction == GDK_SCROLL_RIGHT ||
                 (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y > 0.0));

  if (!scroll_up && !scroll_down)
    return TRUE;

  if (ev->state & GDK_SHIFT_MASK) {
    delta = span * 0.10f;
    gtk3_curve_pan_x(widget, scroll_up ? -delta : delta);
    return TRUE;
  }

  factor = scroll_up ? 1.25f : 0.80f;

  if (ev->state & GDK_CONTROL_MASK)
    factor = scroll_up ? 1.50f : 0.6666667f;

  gtk3_curve_zoom_x(widget, center, factor);
  return TRUE;
}

static gboolean
gtk3_curve_scroll_event(GtkWidget *widget, GdkEventScroll *event)
{
  return gtk3_curve_handle_scroll(widget, event);
}

static gboolean
gtk3_curve_mouse_scroll(GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
  (void) user_data;
  return gtk3_curve_handle_scroll(widget, ev);
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
      GDK_SCROLL_MASK |
      GDK_SMOOTH_SCROLL_MASK);

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
gtk3_curve_next_nice_x_step(gint min_step, gdouble fps)
{
  if (min_step <= 1)
    return 1;

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
      if (step >= min_step)
        return MAX(1, step);
    }
  }

  {
    static const gint nice_frames[] = {
      1, 2, 5,
      10, 20, 25, 50,
      100, 125, 250, 500,
      1000, 1500, 2500, 5000,
      10000, 20000, 50000, 100000
    };

    for (guint i = 0; i < G_N_ELEMENTS(nice_frames); i++) {
      if (nice_frames[i] >= min_step)
        return nice_frames[i];
    }
  }

  return min_step;
}

static gint
gtk3_curve_nice_x_grid_step(gint frame_count, gdouble fps)
{
  gint raw_step;

  if (frame_count <= 0)
    return 1;

  raw_step = frame_count / 10;

  if (raw_step < 1)
    raw_step = 1;

  return gtk3_curve_next_nice_x_step(raw_step, fps);
}

static gint
gtk3_curve_visible_x_grid_step(Gtk3CurvePrivate *priv, gint graph_width)
{
  gint frame_count;
  gint max_labels;
  gint min_step;
  gint step;

  if (!priv)
    return 1;

  step = priv->x_grid_step > 0 ? priv->x_grid_step : 1;

  if (graph_width <= 0)
    return step;

  frame_count = (gint)(fabs((gdouble)priv->max_x -
                            (gdouble)priv->min_x) + 0.5);
  if (frame_count <= 1)
    return step;

  max_labels = graph_width / 42;
  if (max_labels < 2)
    max_labels = 2;
  else if (max_labels > 16)
    max_labels = 16;

  min_step = (frame_count + max_labels - 1) / max_labels;
  if (min_step < 1)
    min_step = 1;

  if (step < min_step)
    step = gtk3_curve_next_nice_x_step(min_step, priv->fps);

  return step > 0 ? step : 1;
}

static inline gboolean
gtk3_curve_color_is_near_black(Gtk3CurveColor c)
{
  return c.red < 0.035f && c.green < 0.035f && c.blue < 0.035f;
}

static inline gboolean
gtk3_curve_color_is_near_white(Gtk3CurveColor c)
{
  return c.red > 0.94f && c.green > 0.94f && c.blue > 0.94f;
}

static inline gboolean
gtk3_curve_color_is_usable(Gtk3CurveColor c)
{
  return c.alpha > 0.001f &&
         !gtk3_curve_color_is_near_black(c) &&
         !gtk3_curve_color_is_near_white(c);
}

static inline Gtk3CurveColor
gtk3_curve_color_from_rgba(GdkRGBA rgba, gfloat alpha)
{
  Gtk3CurveColor c;
  c.red = rgba.red;
  c.green = rgba.green;
  c.blue = rgba.blue;
  c.alpha = alpha;
  return c;
}

static inline Gtk3CurveColor
gtk3_curve_color_rgba(gfloat r, gfloat g, gfloat b, gfloat a)
{
  Gtk3CurveColor c;
  c.red = r;
  c.green = g;
  c.blue = b;
  c.alpha = a;
  return c;
}

static Gtk3CurveColor
gtk3_curve_legend_background_color(GtkWidget *widget,
                                   Gtk3CurvePrivate *priv,
                                   gfloat alpha)
{
  Gtk3CurveColor c;

  if (widget) {
    GtkStyleContext *sc = gtk_widget_get_style_context(widget);
    GdkRGBA rgba;

    vj_gtk_context_get_color(sc,
                             "background-color",
                             gtk_style_context_get_state(sc),
                             &rgba);

    c = gtk3_curve_color_from_rgba(rgba, alpha);
    if (gtk3_curve_color_is_usable(c))
      return c;
  }

  if (priv) {
    c = priv->background;
    c.alpha = alpha;
    if (gtk3_curve_color_is_usable(c))
      return c;
  }

  return gtk3_curve_color_rgba(45.0f / 255.0f,
                               46.0f / 255.0f,
                               54.0f / 255.0f,
                               alpha);
}

static Gtk3CurveColor
gtk3_curve_legend_border_color(GtkWidget *widget,
                               Gtk3CurvePrivate *priv,
                               gfloat alpha)
{
  Gtk3CurveColor c;

  if (widget) {
    GtkStyleContext *sc = gtk_widget_get_style_context(widget);
    GdkRGBA rgba;

    vj_gtk_context_get_color(sc,
                             "border-color",
                             gtk_style_context_get_state(sc),
                             &rgba);

    c = gtk3_curve_color_from_rgba(rgba, alpha);
    if (gtk3_curve_color_is_usable(c) || gtk3_curve_color_is_near_white(c))
      return c;
  }

  if (priv) {
    c = priv->cpoint;
    c.alpha = alpha;
    if (gtk3_curve_color_is_usable(c))
      return c;

    c = priv->grid;
    c.alpha = alpha;
    if (gtk3_curve_color_is_usable(c))
      return c;
  }

  return gtk3_curve_color_rgba(50.0f / 255.0f,
                               53.0f / 255.0f,
                               64.0f / 255.0f,
                               alpha);
}

static Gtk3CurveColor
gtk3_curve_legend_text_color(GtkWidget *widget,
                             Gtk3CurvePrivate *priv,
                             gfloat alpha)
{
  Gtk3CurveColor c;

  if (widget) {
    GtkStyleContext *sc = gtk_widget_get_style_context(widget);
    GdkRGBA rgba;

    gtk_style_context_get_color(sc,
                                gtk_style_context_get_state(sc),
                                &rgba);

    c = gtk3_curve_color_from_rgba(rgba, alpha);
    if (c.alpha > 0.001f)
      return c;
  }

  if (priv) {
    c = priv->curve;
    c.alpha = alpha;
    if (c.alpha > 0.001f)
      return c;
  }

  return gtk3_curve_color_rgba(1.0f, 1.0f, 1.0f, alpha);
}

static void
gtk3_curve_draw_cursor_legend(GtkWidget *widget,
                              cairo_t   *cr,
                              gint       allocation_width,
                              gint       allocation_height,
                              gint       graph_width,
                              gint       graph_height)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE(widget)->priv;

  gchar left[128];
  gchar right[128];

  gfloat x_value;
  gfloat y_value;
  char *timecode = NULL;

  if (graph_width <= 1 || graph_height <= 1)
    return;

  if (priv->in_curve) {
    if (gtk3_curve_live_trace_use_local_axis(priv)) {
      gfloat axis_min_x = gtk3_curve_live_axis_min_x(priv);
      gfloat axis_max_x = gtk3_curve_live_axis_max_x(priv);
      gdouble t = CLAMP(priv->last_x, 0.0f, (gfloat)(graph_width - 1)) /
                  MAX(1.0, (gdouble)(graph_width - 1));
      x_value = axis_min_x + (gfloat)(t * (axis_max_x - axis_min_x));
    } else {
      x_value = scale_pos_value(priv,
                                CLAMP(priv->last_x, 0.0f, (gfloat)(graph_width - 1)),
                                (gfloat) graph_width);
    }

    y_value = scale_param_value(priv,
                                CLAMP(priv->last_y, 0.0f, (gfloat)(graph_height - 1)),
                                (gfloat) graph_height);

    if (gtk3_curve_live_trace_use_local_axis(priv)) {
      g_snprintf(left,
                 sizeof(left),
                 "Live %.0f",
                 x_value);
    } else {
      timecode = format_selection_time(priv->min_x, x_value);
      g_snprintf(left,
                 sizeof(left),
                 "Frame %.0f%s%s",
                 x_value,
                 timecode ? " / " : "",
                 timecode ? timecode : "");
    }

    g_snprintf(right,
               sizeof(right),
               "Value %.2f     Mode: %s",
               y_value,
               gtk3_curve_type_name(priv->curve_data.curve_type));
  } else {
    g_snprintf(left,
               sizeof(left),
               gtk3_curve_live_trace_use_local_axis(priv) ? "Live" : "Frame");

    g_snprintf(right,
               sizeof(right),
               "Value    Mode: %s",
               gtk3_curve_type_name(priv->curve_data.curve_type));
  }

  if (timecode)
    free(timecode);

  cairo_save(cr);

  gdouble y = allocation_height - gtk3_curve_legend_height(widget);

  cairo_set_source_rgba(cr,
                        priv->grid.red,
                        priv->grid.green,
                        priv->grid.blue,
                        priv->grid.alpha * 0.10);
  cairo_rectangle(cr, 0.0, y, allocation_width, gtk3_curve_legend_height(widget));
  cairo_fill(cr);

  cairo_set_source_rgba(cr,
                        priv->grid.red,
                        priv->grid.green,
                        priv->grid.blue,
                        priv->grid.alpha * 0.35);
  cairo_set_line_width(cr, 0.5);
  gtk3_curve_draw_line(cr, 0.0, y + 0.5, allocation_width, y + 0.5);

  cairo_select_font_face(cr,
                         gtk3_curve_font_family(widget),
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, gtk3_curve_font_px(widget, 0.82));

  cairo_set_source_rgba(cr,
                        priv->grid.red,
                        priv->grid.green,
                        priv->grid.blue,
                        priv->grid.alpha * 0.90);

  cairo_text_extents_t left_ext;
  cairo_text_extents_t right_ext;

  cairo_text_extents(cr, left, &left_ext);
  cairo_text_extents(cr, right, &right_ext);

  cairo_move_to(cr,
                6.0,
                y + (gtk3_curve_legend_height(widget) +
                     gtk3_curve_font_px(widget, 0.82) * 0.65) * 0.5);
  cairo_show_text(cr, left);

  if (6.0 + left_ext.width + 20.0 <
      allocation_width - right_ext.width - 8.0) {
    cairo_move_to(cr,
                  allocation_width - right_ext.width - 8.0,
                  y + (gtk3_curve_legend_height(widget) +
                       gtk3_curve_font_px(widget, 0.82) * 0.65) * 0.5);
    cairo_show_text(cr, right);
  }

  cairo_restore(cr);
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
      gtk_widget_queue_resize (widget);
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


typedef struct
{
  gdouble x;
  gdouble y;
  gdouble w;
  gdouble h;
  gdouble x1;
  gdouble y1;
} Gtk3CurveGraphRect;

static gboolean
gtk3_curve_live_trace_use_local_axis(Gtk3CurvePrivate *priv)
{
  return priv && priv->live_trace_domain == GTK3_CURVE_LIVE_TRACE_DOMAIN_CLOCK;
}

static gboolean
gtk3_curve_curve_is_visually_flat(Gtk3CurvePrivate *priv)
{
  if (!priv)
    return TRUE;

  if (priv->curve_data.d_point && priv->curve_data.n_points > 1) {
    gint y0 = priv->curve_data.d_point[0].y;

    for (gint i = 1; i < priv->curve_data.n_points; i++) {
      if (abs(priv->curve_data.d_point[i].y - y0) > 1)
        return FALSE;
    }

    return TRUE;
  }

  if (priv->curve_data.d_cpoints && priv->curve_data.n_cpoints > 1) {
    gfloat y0 = priv->curve_data.d_cpoints[0].y;

    for (gint i = 1; i < priv->curve_data.n_cpoints; i++) {
      if (fabsf(priv->curve_data.d_cpoints[i].y - y0) > 0.001f)
        return FALSE;
    }

    return TRUE;
  }

  return TRUE;
}

static gfloat
gtk3_curve_live_axis_min_x(Gtk3CurvePrivate *priv)
{
  if (!priv)
    return 0.0f;

  if (gtk3_curve_live_trace_use_local_axis(priv) &&
      isfinite(priv->live_trace_view_min_x))
    return priv->live_trace_view_min_x;

  if (isfinite(priv->min_x))
    return priv->min_x;

  return 0.0f;
}

static gfloat
gtk3_curve_live_axis_max_x(Gtk3CurvePrivate *priv)
{
  if (!priv)
    return 1.0f;

  if (gtk3_curve_live_trace_use_local_axis(priv) &&
      isfinite(priv->live_trace_view_min_x) &&
      isfinite(priv->live_trace_view_max_x) &&
      priv->live_trace_view_max_x > priv->live_trace_view_min_x)
    return priv->live_trace_view_max_x;

  if (isfinite(priv->max_x) &&
      (!isfinite(priv->min_x) || priv->max_x > priv->min_x))
    return priv->max_x;

  return gtk3_curve_live_axis_min_x(priv) + 1.0f;
}

static gfloat
gtk3_curve_live_domain_min_x(Gtk3CurvePrivate *priv)
{
  if (!priv)
    return 0.0f;

  if (gtk3_curve_live_trace_use_local_axis(priv)) {
    if (isfinite(priv->live_trace_local_min_x))
      return priv->live_trace_local_min_x;
    return 0.0f;
  }

  if (isfinite(priv->timeline_min_x) &&
      isfinite(priv->timeline_max_x) &&
      priv->timeline_max_x > priv->timeline_min_x)
    return priv->timeline_min_x;

  if (isfinite(priv->min_x))
    return priv->min_x;

  return 0.0f;
}

static gfloat
gtk3_curve_live_domain_max_x(Gtk3CurvePrivate *priv)
{
  if (!priv)
    return 1.0f;

  if (gtk3_curve_live_trace_use_local_axis(priv)) {
    if (isfinite(priv->live_trace_local_min_x) &&
        isfinite(priv->live_trace_local_max_x) &&
        priv->live_trace_local_max_x > priv->live_trace_local_min_x)
      return priv->live_trace_local_max_x;
    return gtk3_curve_live_domain_min_x(priv) + 1.0f;
  }

  if (isfinite(priv->timeline_min_x) &&
      isfinite(priv->timeline_max_x) &&
      priv->timeline_max_x > priv->timeline_min_x)
    return priv->timeline_max_x;

  if (isfinite(priv->max_x))
    return priv->max_x;

  return 1.0f;
}

static gint
gtk3_curve_live_trace_slot_for_x(Gtk3CurvePrivate *priv,
                                 gfloat            x_value)
{
  gfloat min_x;
  gfloat max_x;
  gdouble t;
  gint slot;

  if (!priv || !isfinite(x_value))
    return 0;

  min_x = gtk3_curve_live_domain_min_x(priv);
  max_x = gtk3_curve_live_domain_max_x(priv);

  if (!isfinite(min_x))
    min_x = 0.0f;
  if (!isfinite(max_x) || max_x <= min_x)
    max_x = min_x + 1.0f;

  if (x_value <= min_x)
    return 0;
  if (x_value >= max_x)
    return GTK3_CURVE_LIVE_TRACE_LEN - 1;

  t = ((gdouble)x_value - (gdouble)min_x) /
      ((gdouble)max_x - (gdouble)min_x);
  slot = (gint)((t * (GTK3_CURVE_LIVE_TRACE_LEN - 1)) + 0.5);

  if (slot < 0)
    slot = 0;
  else if (slot >= GTK3_CURVE_LIVE_TRACE_LEN)
    slot = GTK3_CURVE_LIVE_TRACE_LEN - 1;

  return slot;
}

static gint
gtk3_curve_live_trace_history_capacity(Gtk3CurvePrivate *priv)
{
  gfloat min_x;
  gfloat max_x;
  gfloat span;
  gint capacity;

  if (!priv)
    return GTK3_CURVE_LIVE_TRACE_LEN;

  min_x = gtk3_curve_live_domain_min_x(priv);
  max_x = gtk3_curve_live_domain_max_x(priv);

  if (!isfinite(min_x))
    min_x = 0.0f;
  if (!isfinite(max_x) || max_x <= min_x)
    max_x = min_x + 1.0f;

  span = max_x - min_x;
  capacity = (gint) floorf(span + 0.5f) + 1;

  if (capacity < 2)
    capacity = 2;
  else if (capacity > GTK3_CURVE_LIVE_TRACE_LEN)
    capacity = GTK3_CURVE_LIVE_TRACE_LEN;

  return capacity;
}

static gint
gtk3_curve_live_trace_iteration_count(Gtk3CurvePrivate *priv)
{
  if (!gtk3_curve_live_trace_use_local_axis(priv))
    return GTK3_CURVE_LIVE_TRACE_LEN;

  return MIN(priv->live_trace_count,
             gtk3_curve_live_trace_history_capacity(priv));
}

static gboolean
gtk3_curve_live_trace_point_at_order(Gtk3CurvePrivate *priv,
                                     gint              trace,
                                     gint              order,
                                     gint             *slot,
                                     gfloat           *x_value,
                                     gfloat           *value)
{
  gint idx;

  if (!priv || trace < 0 || trace >= GTK3_CURVE_LIVE_TRACE_MAX || order < 0)
    return FALSE;

  if (!priv->live_trace_point_used || !priv->live_trace_x || !priv->live_trace_values)
    return FALSE;

  if (gtk3_curve_live_trace_use_local_axis(priv)) {
    gint capacity = gtk3_curve_live_trace_history_capacity(priv);
    gint count = MIN(priv->live_trace_count, capacity);
    gint oldest;
    gfloat min_x;
    gfloat max_x;
    gfloat step;

    if (order >= count)
      return FALSE;

    oldest = (count < capacity) ? 0 : priv->live_trace_pos;
    if (oldest < 0 || oldest >= capacity)
      oldest = 0;

    idx = (oldest + order) % capacity;

    min_x = gtk3_curve_live_domain_min_x(priv);
    max_x = gtk3_curve_live_domain_max_x(priv);
    if (!isfinite(min_x))
      min_x = 0.0f;
    if (!isfinite(max_x) || max_x <= min_x)
      max_x = min_x + 1.0f;

    step = (capacity > 1) ? (max_x - min_x) / (gfloat)(capacity - 1) : 1.0f;

    if (x_value)
      *x_value = max_x - ((gfloat)(count - 1 - order) * step);
  }
  else {
    if (order >= GTK3_CURVE_LIVE_TRACE_LEN)
      return FALSE;

    idx = order;

    if (x_value)
      *x_value = priv->live_trace_x[trace][idx];
  }

  if (!priv->live_trace_point_used[trace][idx])
    return FALSE;

  if (slot)
    *slot = idx;
  if (value)
    *value = priv->live_trace_values[trace][idx];

  return TRUE;
}

static gboolean
gtk3_curve_live_trace_clock_x_for_slot(Gtk3CurvePrivate *priv,
                                       gint              slot,
                                       gfloat           *x_value)
{
  gint capacity;
  gint count;
  gint oldest;
  gint order;
  gfloat min_x;
  gfloat max_x;
  gfloat step;

  if (!priv || !x_value || !gtk3_curve_live_trace_use_local_axis(priv))
    return FALSE;

  capacity = gtk3_curve_live_trace_history_capacity(priv);
  count = MIN(priv->live_trace_count, capacity);

  if (count <= 0 || slot < 0 || slot >= capacity)
    return FALSE;

  oldest = (count < capacity) ? 0 : priv->live_trace_pos;
  if (oldest < 0 || oldest >= capacity)
    oldest = 0;

  order = slot - oldest;
  if (order < 0)
    order += capacity;
  if (order < 0 || order >= count)
    return FALSE;

  min_x = gtk3_curve_live_domain_min_x(priv);
  max_x = gtk3_curve_live_domain_max_x(priv);
  if (!isfinite(min_x))
    min_x = 0.0f;
  if (!isfinite(max_x) || max_x <= min_x)
    max_x = min_x + 1.0f;

  step = (capacity > 1) ? (max_x - min_x) / (gfloat)(capacity - 1) : 1.0f;
  *x_value = max_x - ((gfloat)(count - 1 - order) * step);

  return TRUE;
}

static gboolean
gtk3_curve_live_graph_rect(gint graph_width,
                           gint graph_height,
                           Gtk3CurveGraphRect *r)
{
  if (!r || graph_width <= 1 || graph_height <= 1)
    return FALSE;

  r->x = (gdouble) RADIUS;
  r->y = (gdouble) RADIUS;
  r->w = (gdouble) graph_width;
  r->h = (gdouble) graph_height;
  r->x1 = r->x + r->w - 1.0;
  r->y1 = r->y + r->h - 1.0;

  return TRUE;
}

static gboolean
gtk3_curve_live_project_x(Gtk3CurvePrivate      *priv,
                          const Gtk3CurveGraphRect *r,
                          gfloat                 value,
                          gdouble               *x)
{
  gdouble t;

  if (!priv || !r || !x)
    return FALSE;

  gfloat min_x = gtk3_curve_live_axis_min_x(priv);
  gfloat max_x = gtk3_curve_live_axis_max_x(priv);

  if (!isfinite(value) || max_x <= min_x)
    return FALSE;

  t = ((gdouble)value - (gdouble)min_x) /
      ((gdouble)max_x - (gdouble)min_x);

  if (t < 0.0 || t > 1.0)
    return FALSE;

  *x = r->x + (t * (r->w - 1.0));
  return TRUE;
}

static void
gtk3_curve_live_value_range(Gtk3CurvePrivate *priv,
                            gfloat           *min_y,
                            gfloat           *max_y)
{
  gfloat lo = 0.0f;
  gfloat hi = 100.0f;

  if (priv && !gtk3_curve_live_trace_use_local_axis(priv)) {
    lo = priv->min_y;
    hi = priv->max_y;

    if (!isfinite(lo))
      lo = 0.0f;
    if (!isfinite(hi) || hi <= lo)
      hi = lo + 1.0f;
  }

  if (min_y)
    *min_y = lo;
  if (max_y)
    *max_y = hi;
}

static gfloat
gtk3_curve_live_clamp_value(Gtk3CurvePrivate *priv,
                            gfloat            value)
{
  gfloat min_y;
  gfloat max_y;

  gtk3_curve_live_value_range(priv, &min_y, &max_y);

  if (!isfinite(value))
    value = min_y;

  if (value < min_y)
    value = min_y;
  else if (value > max_y)
    value = max_y;

  return value;
}

static gdouble
gtk3_curve_live_project_y_norm(Gtk3CurvePrivate          *priv,
                               const Gtk3CurveGraphRect *r,
                               gfloat                    value)
{
  gdouble t;
  gfloat min_y;
  gfloat max_y;

  if (!r)
    return 0.0;

  gtk3_curve_live_value_range(priv, &min_y, &max_y);
  value = gtk3_curve_live_clamp_value(priv, value);

  t = ((gdouble)value - (gdouble)min_y) / ((gdouble)max_y - (gdouble)min_y);

  if (t < 0.0)
    t = 0.0;
  else if (t > 1.0)
    t = 1.0;

  {
    gdouble pad = CURVE_LIVE_TRACE_Y_PAD;
    gdouble span;

    if (r->h <= (pad * 2.0) + 2.0)
      pad = 0.0;

    span = (r->h - 1.0) - (pad * 2.0);
    if (span < 1.0)
      span = r->h - 1.0;

    return (r->y1 - pad) - (t * span);
  }
}

static void
gtk3_curve_draw_live_dot(Gtk3CurvePrivate *priv,
                         cairo_t          *cr,
                         const Gtk3CurveGraphRect *r)
{
  gdouble x;
  gdouble y0;
  gdouble y1;
  gdouble radius = 5.0;

  if (!priv || !cr || !r)
    return;

  if (!priv->live_trace_dot_enabled)
    return;

  if (!gtk3_curve_live_project_x(priv, r, priv->live_trace_dot_x, &x))
    return;

  (void)y0;
  y1 = gtk3_curve_live_project_y_norm(priv, r, priv->live_trace_dot_value);

  if (x < r->x + radius)
    x = r->x + radius;
  else if (x > r->x1 - radius)
    x = r->x1 - radius;

  if (y1 < r->y + radius)
    y1 = r->y + radius;
  else if (y1 > r->y1 - radius)
    y1 = r->y1 - radius;

  cairo_save(cr);
  cairo_set_source_rgba(cr,
                        priv->live_trace_dot_color.red,
                        priv->live_trace_dot_color.green,
                        priv->live_trace_dot_color.blue,
                        MIN(1.0, priv->live_trace_dot_color.alpha + 0.20));
  cairo_arc(cr, x, y1, radius, 0.0, 2.0 * M_PI);
  cairo_fill_preserve(cr);

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.42);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  cairo_restore(cr);
}


static void
gtk3_curve_draw_live_trace_current_dot(Gtk3CurvePrivate *priv,
                                       cairo_t          *cr,
                                       const Gtk3CurveGraphRect *r,
                                       gint              trace,
                                       gint              idx)
{
  gdouble x;
  gdouble y;
  gdouble radius = 2.8;
  gfloat x_value;

  if (!priv || !cr || !r)
    return;

  if (trace < 0 || trace >= GTK3_CURVE_LIVE_TRACE_MAX)
    return;

  if (idx < 0 || idx >= GTK3_CURVE_LIVE_TRACE_LEN)
    return;

  if (!priv->live_trace_active[trace])
    return;

  if (!priv->live_trace_point_used || !priv->live_trace_point_used[trace][idx])
    return;

  x_value = priv->live_trace_x[trace][idx];
  if (gtk3_curve_live_trace_use_local_axis(priv) &&
      !gtk3_curve_live_trace_clock_x_for_slot(priv, idx, &x_value))
    return;

  if (!gtk3_curve_live_project_x(priv, r, x_value, &x))
    return;

  y = gtk3_curve_live_project_y_norm(priv, r, priv->live_trace_values[trace][idx]);

  if (x < r->x + radius)
    x = r->x + radius;
  else if (x > r->x1 - radius)
    x = r->x1 - radius;

  if (y < r->y + radius)
    y = r->y + radius;
  else if (y > r->y1 - radius)
    y = r->y1 - radius;

  cairo_save(cr);
  cairo_set_source_rgba(cr,
                        priv->live_trace_color[trace].red,
                        priv->live_trace_color[trace].green,
                        priv->live_trace_color[trace].blue,
                        MIN(1.0, priv->live_trace_color[trace].alpha + 0.26));
  cairo_arc(cr, x, y, radius, 0.0, 2.0 * M_PI);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.36);
  cairo_set_line_width(cr, 0.8);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void
gtk3_curve_draw_live_trace_segment(cairo_t       *cr,
                                   Gtk3CurveType  type,
                                   gdouble       *x,
                                   gdouble       *y,
                                   gint           n)
{
  if (!cr || !x || !y || n <= 0)
    return;

  cairo_move_to(cr, x[0], y[0]);

  if (type == GTK3_CURVE_TYPE_SPLINE && n > 2) {
    for (gint i = 0; i < n - 1; i++) {
      gint i0 = (i > 0) ? i - 1 : i;
      gint i1 = i;
      gint i2 = i + 1;
      gint i3 = (i + 2 < n) ? i + 2 : i + 1;
      gdouble c1x = x[i1] + (x[i2] - x[i0]) / 6.0;
      gdouble c1y = y[i1] + (y[i2] - y[i0]) / 6.0;
      gdouble c2x = x[i2] - (x[i3] - x[i1]) / 6.0;
      gdouble c2y = y[i2] - (y[i3] - y[i1]) / 6.0;
      cairo_curve_to(cr, c1x, c1y, c2x, c2y, x[i2], y[i2]);
    }
  } else {
    for (gint i = 1; i < n; i++)
      cairo_line_to(cr, x[i], y[i]);
  }

  cairo_stroke(cr);
}

static void
gtk3_curve_live_trace_clear_samples(Gtk3CurvePrivate *priv)
{
  if (!priv)
    return;

  priv->live_trace_pos = 0;
  priv->live_trace_count = 0;
  memset(priv->live_trace_active, 0, sizeof(priv->live_trace_active));
  if (priv->live_trace_values)
    memset(priv->live_trace_values[0], 0, sizeof(gfloat) * GTK3_CURVE_LIVE_TRACE_MAX * GTK3_CURVE_LIVE_TRACE_LEN);
  if (priv->live_trace_x)
    memset(priv->live_trace_x[0], 0, sizeof(gfloat) * GTK3_CURVE_LIVE_TRACE_MAX * GTK3_CURVE_LIVE_TRACE_LEN);
  if (priv->live_trace_point_used)
    memset(priv->live_trace_point_used[0], 0, sizeof(gboolean) * GTK3_CURVE_LIVE_TRACE_MAX * GTK3_CURVE_LIVE_TRACE_LEN);
  if (priv->live_trace_slot_used)
    memset(priv->live_trace_slot_used, 0, sizeof(gboolean) * GTK3_CURVE_LIVE_TRACE_LEN);
  priv->live_trace_last_slot = -1;
  for (gint t = 0; t < GTK3_CURVE_LIVE_TRACE_MAX; t++)
    priv->live_trace_last_slot_for[t] = -1;
  priv->live_trace_have_source_x = FALSE;
  priv->live_trace_last_source_x = 0.0f;
  memset(priv->live_trace_color, 0, sizeof(priv->live_trace_color));
  memset(priv->live_trace_label, 0, sizeof(priv->live_trace_label));
  priv->live_trace_pending_x = NAN;
  priv->live_trace_pending_valid = FALSE;
  priv->live_trace_clear_on_next_push = FALSE;
}


static void
gtk3_curve_live_trace_disable_for_user_edit(GtkWidget *widget)
{
  Gtk3CurvePrivate *priv = gtk3_curve_live_trace_priv(widget);

  if (!priv)
    return;

  priv->live_trace_user_override = TRUE;

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}

static gboolean
gtk3_curve_live_trace_label_is_drawn(Gtk3CurvePrivate *priv,
                                      const gchar      *label)
{
  if (!priv || !label || label[0] == '\0')
    return FALSE;

  for (gint t = 0; t < GTK3_CURVE_LIVE_TRACE_MAX; t++) {
    if (!priv->live_trace_active[t])
      continue;
    if (priv->live_trace_label[t][0] == '\0')
      continue;
    if (g_strcmp0(priv->live_trace_label[t], label) == 0)
      return TRUE;
  }

  return FALSE;
}

static void
gtk3_curve_draw_live_traces(GtkWidget *widget,
                            cairo_t   *cr,
                            gint       allocation_width,
                            gint       graph_width,
                            gint       graph_height,
                            gboolean   as_underlay)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE(widget)->priv;
  Gtk3CurveGraphRect gr;

  if (!priv->live_trace_enabled || priv->live_trace_count <= 0)
    return;

  if (!gtk3_curve_live_graph_rect(graph_width, graph_height, &gr))
    return;

  cairo_save(cr);
  cairo_rectangle(cr, gr.x, gr.y, gr.w, gr.h);
  cairo_clip(cr);

  for (gint t = 0; t < GTK3_CURVE_LIVE_TRACE_MAX; t++) {
    gdouble sx[GTK3_CURVE_LIVE_TRACE_LEN];
    gdouble sy[GTK3_CURVE_LIVE_TRACE_LEN];
    gint n = 0;
    gint iteration_count;
    gboolean scrolling_history;

    if (!priv->live_trace_active[t])
      continue;

    gdouble trace_alpha = priv->live_trace_color[t].alpha;
    gdouble trace_width = (t == GTK3_CURVE_LIVE_TRACE_MAX - 1) ? 1.85 : 1.20;

    if (as_underlay) {
      trace_alpha *= 0.52;
      trace_width = (t == GTK3_CURVE_LIVE_TRACE_MAX - 1) ? 1.35 : 0.95;
    }

    cairo_set_line_width(cr, trace_width);
    cairo_set_source_rgba(cr,
                          priv->live_trace_color[t].red,
                          priv->live_trace_color[t].green,
                          priv->live_trace_color[t].blue,
                          CLAMP(trace_alpha, 0.0, 1.0));

    gfloat last_value_x = 0.0f;
    gboolean have_last_value_x = FALSE;
    gfloat jump_threshold = gtk3_curve_live_trace_draw_jump_threshold(priv);
    scrolling_history = gtk3_curve_live_trace_use_local_axis(priv);
    iteration_count = gtk3_curve_live_trace_iteration_count(priv);

    for (gint order = 0; order < iteration_count; order++) {
      gint idx;
      gdouble x;
      gdouble y;
      gfloat value_x;
      gfloat trace_value;

      if (!gtk3_curve_live_trace_point_at_order(priv,
                                                t,
                                                order,
                                                &idx,
                                                &value_x,
                                                &trace_value)) {
        if (scrolling_history && n > 0) {
          gtk3_curve_draw_live_trace_segment(cr, GTK3_CURVE_TYPE_LINEAR, sx, sy, n);
          n = 0;
          have_last_value_x = FALSE;
        }
        continue;
      }

      if (!scrolling_history &&
          have_last_value_x &&
          fabsf(value_x - last_value_x) > jump_threshold) {
        gtk3_curve_draw_live_trace_segment(cr, GTK3_CURVE_TYPE_LINEAR, sx, sy, n);
        n = 0;
        have_last_value_x = FALSE;
      }

      if (!gtk3_curve_live_project_x(priv, &gr, value_x, &x)) {
        gtk3_curve_draw_live_trace_segment(cr, GTK3_CURVE_TYPE_LINEAR, sx, sy, n);
        n = 0;
        have_last_value_x = FALSE;
        continue;
      }

      y = gtk3_curve_live_project_y_norm(priv, &gr, trace_value);

      if (n > 0 && x < sx[n - 1] - 0.5) {
        gtk3_curve_draw_live_trace_segment(cr, GTK3_CURVE_TYPE_LINEAR, sx, sy, n);
        n = 0;
        have_last_value_x = FALSE;
      }

      if (n > 0 && fabs(x - sx[n - 1]) <= 0.5) {
        sy[n - 1] = y;
        continue;
      }

      sx[n] = x;
      sy[n] = y;
      n++;
      last_value_x = value_x;
      have_last_value_x = TRUE;
    }

    gtk3_curve_draw_live_trace_segment(cr, GTK3_CURVE_TYPE_LINEAR, sx, sy, n);

    if (!as_underlay && priv->live_trace_last_slot_for[t] >= 0)
      gtk3_curve_draw_live_trace_current_dot(priv, cr, &gr, t, priv->live_trace_last_slot_for[t]);
  }

  if (!as_underlay && gtk3_curve_live_trace_use_local_axis(priv))
    gtk3_curve_draw_live_dot(priv, cr, &gr);

  cairo_restore(cr);

  if (as_underlay)
    return;

  cairo_save(cr);
  cairo_select_font_face(cr,
                         gtk3_curve_font_family(widget),
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, gtk3_curve_font_px(widget, 0.72));

  {
    gchar labels[GTK3_CURVE_LIVE_TRACE_MAX + 1][GTK3_CURVE_LIVE_TRACE_LABEL];
    Gtk3CurveColor colors[GTK3_CURVE_LIVE_TRACE_MAX + 1];
    gint label_count = 0;
    gdouble max_w = 0.0;
    cairo_text_extents_t ext;

    for (gint t = 0; t < GTK3_CURVE_LIVE_TRACE_MAX; t++) {
      if (!priv->live_trace_active[t] || priv->live_trace_label[t][0] == '\0')
        continue;

      g_strlcpy(labels[label_count],
                priv->live_trace_label[t],
                GTK3_CURVE_LIVE_TRACE_LABEL);
      colors[label_count] = priv->live_trace_color[t];
      cairo_text_extents(cr, labels[label_count], &ext);
      if (ext.width > max_w)
        max_w = ext.width;
      label_count++;
      if (label_count >= GTK3_CURVE_LIVE_TRACE_MAX)
        break;
    }

    if (priv->live_trace_dot_enabled &&
        priv->live_trace_dot_label[0] != '\0' &&
        !gtk3_curve_live_trace_label_is_drawn(priv, priv->live_trace_dot_label) &&
        label_count < GTK3_CURVE_LIVE_TRACE_MAX + 1) {
      g_strlcpy(labels[label_count],
                priv->live_trace_dot_label,
                GTK3_CURVE_LIVE_TRACE_LABEL);
      colors[label_count] = priv->live_trace_dot_color;
      cairo_text_extents(cr, labels[label_count], &ext);
      if (ext.width > max_w)
        max_w = ext.width;
      label_count++;
    }

    if (label_count > 0) {
      gdouble box_w = MIN((gdouble)allocation_width - 12.0, max_w + 14.0);
      gdouble box_h = MIN((gdouble)label_count * 11.0 + 8.0, gr.h - 8.0);
      gdouble box_x = MAX(gr.x + 4.0, gr.x1 - box_w - 5.0);
      gdouble box_y = gr.y + 5.0;
      gint max_rows = (gint)((box_h - 8.0) / 11.0);

      if (max_rows < 1)
        max_rows = 1;

      Gtk3CurveColor legend_bg =
        gtk3_curve_legend_background_color(widget, priv, 0.88f);
      Gtk3CurveColor legend_border =
        gtk3_curve_legend_border_color(widget, priv, 0.72f);

      cairo_set_source_rgba(cr,
                            legend_bg.red,
                            legend_bg.green,
                            legend_bg.blue,
                            legend_bg.alpha);
      cairo_rectangle(cr, box_x, box_y, box_w, box_h);
      cairo_fill(cr);

      cairo_set_source_rgba(cr,
                            legend_border.red,
                            legend_border.green,
                            legend_border.blue,
                            legend_border.alpha);
      cairo_set_line_width(cr, 0.5);
      cairo_rectangle(cr, box_x + 0.5, box_y + 0.5, box_w - 1.0, box_h - 1.0);
      cairo_stroke(cr);

      for (gint i = 0; i < label_count && i < max_rows; i++) {
        cairo_set_source_rgba(cr,
                              colors[i].red,
                              colors[i].green,
                              colors[i].blue,
                              MIN(1.0, colors[i].alpha + 0.25));
        cairo_move_to(cr, box_x + 7.0, box_y + 14.0 + ((gdouble)i * 11.0));
        cairo_show_text(cr, labels[i]);
      }
    }
  }

  cairo_restore(cr);
}

static gboolean
gtk3_curve_x_nav_hit(GtkWidget *widget,
                     gint       tx,
                     gint       ty,
                     gboolean  *hit_left,
                     gboolean  *hit_right,
                     gboolean  *in_nav)
{
  Gtk3CurvePrivate *priv;
  GtkAllocation allocation;
  gint width;
  gint height;
  gint nav_y0;
  gint nav_y1;
  gint left_x0;
  gint left_x1;
  gint right_x0;
  gint right_x1;
  gboolean can_left;
  gboolean can_right;

  if (hit_left)
    *hit_left = FALSE;

  if (hit_right)
    *hit_right = FALSE;

  if (in_nav)
    *in_nav = FALSE;

  if (!widget)
    return FALSE;

  priv = GTK3_CURVE(widget)->priv;

  gtk_widget_get_allocation(widget, &allocation);

  width = gtk3_curve_graph_width_from_allocation(allocation.width);
  height = gtk3_curve_graph_height_from_allocation(widget, allocation.height);

  if (width <= 1 || height <= 1)
    return FALSE;

  nav_y0 = RADIUS + height + gtk3_curve_x_label_height(widget);
  nav_y1 = nav_y0 + gtk3_curve_x_nav_height(widget);

  if (ty < nav_y0 || ty >= nav_y1)
    return FALSE;

  if (in_nav)
    *in_nav = TRUE;

  {
    gfloat domain_min = gtk3_curve_live_trace_use_local_axis(priv) ? priv->live_trace_local_min_x : priv->timeline_min_x;
    gfloat domain_max = gtk3_curve_live_trace_use_local_axis(priv) ? priv->live_trace_local_max_x : priv->timeline_max_x;
    gfloat view_min = gtk3_curve_live_axis_min_x(priv);
    gfloat view_max = gtk3_curve_live_axis_max_x(priv);

    if (domain_max <= domain_min)
      return FALSE;

    can_left = (view_min > domain_min + 0.5f);
    can_right = (view_max < domain_max - 0.5f);
  }

  left_x0 = RADIUS + 4;
  left_x1 = left_x0 + 24;
  right_x0 = RADIUS + width - 24 - 4;
  right_x1 = right_x0 + 24;

  if (can_left && tx >= left_x0 && tx < left_x1) {
    if (hit_left)
      *hit_left = TRUE;
    return TRUE;
  }

  if (can_right && tx >= right_x0 && tx < right_x1) {
    if (hit_right)
      *hit_right = TRUE;
    return TRUE;
  }

  return FALSE;
}

static void
gtk3_curve_draw_x_zoom_indicators(GtkWidget *widget,
                                  cairo_t   *cr,
                                  gint       allocation_width,
                                  gint       graph_width,
                                  gint       graph_height)
{
  Gtk3CurvePrivate *priv = GTK3_CURVE(widget)->priv;
  gboolean has_left;
  gboolean has_right;
  gchar view_text[128];
  cairo_text_extents_t ext;
  gdouble nav_y;
  gdouble nav_h;
  gdouble left_x;
  gdouble right_x;
  gdouble box_w;
  gdouble box_h;

  if (!cr || graph_width <= 1 || graph_height <= 1)
    return;

  {
    gfloat domain_min = gtk3_curve_live_trace_use_local_axis(priv) ? priv->live_trace_local_min_x : priv->timeline_min_x;
    gfloat domain_max = gtk3_curve_live_trace_use_local_axis(priv) ? priv->live_trace_local_max_x : priv->timeline_max_x;
    gfloat view_min = gtk3_curve_live_axis_min_x(priv);
    gfloat view_max = gtk3_curve_live_axis_max_x(priv);

    if (domain_max <= domain_min)
      return;

    has_left = (view_min > domain_min + 0.5f);
    has_right = (view_max < domain_max - 0.5f);
  }

  if (!has_left && !has_right)
    return;

  nav_y = RADIUS + graph_height + gtk3_curve_x_label_height(widget);
  nav_h = gtk3_curve_x_nav_height(widget);
  box_w = 24.0;
  box_h = nav_h - 3.0;
  left_x = RADIUS + 4.0;
  right_x = RADIUS + graph_width - box_w - 4.0;

  cairo_save(cr);

  cairo_set_source_rgba(cr,
                        priv->grid.red,
                        priv->grid.green,
                        priv->grid.blue,
                        priv->grid.alpha * 0.10);
  cairo_rectangle(cr, 0.0, nav_y, allocation_width, nav_h);
  cairo_fill(cr);

  cairo_set_source_rgba(cr,
                        priv->grid.red,
                        priv->grid.green,
                        priv->grid.blue,
                        priv->grid.alpha * 0.25);
  cairo_set_line_width(cr, 0.5);
  gtk3_curve_draw_line(cr, 0.0, nav_y + 0.5, allocation_width, nav_y + 0.5);

  cairo_select_font_face(cr,
                         gtk3_curve_font_family(widget),
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, gtk3_curve_font_px(widget, 0.82));

  if (has_left) {
    cairo_set_source_rgba(cr,
                          priv->grid.red,
                          priv->grid.green,
                          priv->grid.blue,
                          priv->grid.alpha * 0.14);
    cairo_rectangle(cr, left_x, nav_y + 1.5, box_w, box_h);
    cairo_fill(cr);

    cairo_set_source_rgba(cr,
                          priv->grid.red,
                          priv->grid.green,
                          priv->grid.blue,
                          priv->grid.alpha * 0.85);
    cairo_text_extents(cr, "<", &ext);
    cairo_move_to(cr,
                  left_x + (box_w - ext.width) * 0.5 - ext.x_bearing,
                  nav_y + (nav_h +
                           gtk3_curve_font_px(widget, 0.82) * 0.65) * 0.5);
    cairo_show_text(cr, "<");
  }

  if (has_right) {
    cairo_set_source_rgba(cr,
                          priv->grid.red,
                          priv->grid.green,
                          priv->grid.blue,
                          priv->grid.alpha * 0.14);
    cairo_rectangle(cr, right_x, nav_y + 1.5, box_w, box_h);
    cairo_fill(cr);

    cairo_set_source_rgba(cr,
                          priv->grid.red,
                          priv->grid.green,
                          priv->grid.blue,
                          priv->grid.alpha * 0.85);
    cairo_text_extents(cr, ">", &ext);
    cairo_move_to(cr,
                  right_x + (box_w - ext.width) * 0.5 - ext.x_bearing,
                  nav_y + (nav_h +
                           gtk3_curve_font_px(widget, 0.82) * 0.65) * 0.5);
    cairo_show_text(cr, ">");
  }

  g_snprintf(view_text,
             sizeof(view_text),
             "View %.0f-%.0f / %.0f-%.0f",
             priv->min_x,
             priv->max_x,
             priv->timeline_min_x,
             priv->timeline_max_x);

  cairo_select_font_face(cr,
                         gtk3_curve_font_family(widget),
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, gtk3_curve_font_px(widget, 0.78));
  cairo_text_extents(cr, view_text, &ext);

  if (ext.width < allocation_width - 72.0) {
    cairo_set_source_rgba(cr,
                          priv->grid.red,
                          priv->grid.green,
                          priv->grid.blue,
                          priv->grid.alpha * 0.75);
    cairo_move_to(cr,
                  (allocation_width - ext.width) * 0.5,
                  nav_y + (nav_h +
                           gtk3_curve_font_px(widget, 0.78) * 0.65) * 0.5);
    cairo_show_text(cr, view_text);
  }

  cairo_restore(cr);
}

static void
gtk3_curve_draw_labels(GtkWidget *widget,
                       cairo_t   *cr,
                       gint       allocation_width,
                       gint       allocation_height,
                       gint       graph_width,
                       gint       graph_height)
{
  gchar text[100];
  Gtk3CurvePrivate *priv;
  gint i, incr;
  Gtk3Curve *curve;
  gfloat y_grid;
  cairo_text_extents_t extents;

  curve = GTK3_CURVE(widget);
  priv = curve->priv;

  if (graph_width <= 1 || graph_height <= 1)
    return;

  (void) allocation_height;

  y_grid = priv->y_grid_resolution;

  if (y_grid < 1.0f)
    y_grid = 1.0f;
  else if (y_grid > 64.0f)
    y_grid = 64.0f;

  cairo_save(cr);

  cairo_set_source_rgba(cr,
                        priv->grid.red,
                        priv->grid.green,
                        priv->grid.blue,
                        priv->grid.alpha);

  cairo_select_font_face(cr,
                         gtk3_curve_font_family(widget),
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, gtk3_curve_font_px(widget, 0.82));

  gfloat wm = graph_width;
  gfloat hm = graph_height;

  {
    gfloat label_min_x = gtk3_curve_live_axis_min_x(priv);
    gfloat label_max_x = gtk3_curve_live_axis_max_x(priv);
    gint min_frame = (gint)(label_min_x + 0.5f);
    gint max_frame = (gint)(label_max_x + 0.5f);
    gint frame_count = MAX(1, (gint)(fabsf(label_max_x - label_min_x) + 0.5f));
    gint step = gtk3_curve_live_trace_use_local_axis(priv) ?
                gtk3_curve_nice_x_grid_step(frame_count, 0.0) :
                gtk3_curve_visible_x_grid_step(priv, graph_width);
    gdouble label_y = RADIUS + hm +
                      (gtk3_curve_x_label_height(widget) +
                       gtk3_curve_font_px(widget, 0.82) * 0.65) * 0.5;
    gdouble last_label_right = -1000000.0;

    if (step <= 0)
      step = 1;

    for (gint frame = min_frame; frame <= max_frame; frame += step) {
      gdouble x1 = RADIUS + project((gfloat) frame,
                                    label_min_x,
                                    label_max_x,
                                    (gint) wm);

      snprintf(text, sizeof(text), "%d", frame);

      cairo_text_extents(cr, text, &extents);
      {
        gdouble lx = x1 - (extents.width / 2.0);

        if (lx < 2.0)
          lx = 2.0;
        else if (lx + extents.width > allocation_width - 2.0)
          lx = allocation_width - extents.width - 2.0;

        if (lx >= last_label_right + 6.0 || frame == min_frame) {
          cairo_move_to(cr, lx, label_y);
          cairo_show_text(cr, text);
          last_label_right = lx + extents.width;
        }
      }
    }

    if (((max_frame - min_frame) % step) != 0) {
      gdouble x1 = RADIUS + project((gfloat) max_frame,
                                    label_min_x,
                                    label_max_x,
                                    (gint) wm);

      snprintf(text, sizeof(text), "%d", max_frame);

      cairo_text_extents(cr, text, &extents);
      {
        gdouble lx = x1 - (extents.width / 2.0);

        if (lx < 2.0)
          lx = 2.0;
        else if (lx + extents.width > allocation_width - 2.0)
          lx = allocation_width - extents.width - 2.0;

        if (lx >= last_label_right + 6.0) {
          cairo_move_to(cr, lx, label_y);
          cairo_show_text(cr, text);
          last_label_right = lx + extents.width;
        }
      }
    }
  }

  incr = 1;

  snprintf(text, sizeof(text), "%d", (int) priv->max_y);
  cairo_text_extents(cr, text, &extents);

  while (((extents.height * 2.0 * y_grid) / incr) > graph_height)
    incr++;

  for (i = 0; i <= (int) y_grid; i += incr) {
    gdouble y1 = RADIUS + hm - (i * (hm / y_grid));
    gdouble x1 = 2.0;
    gdouble baseline;

    snprintf(text,
             sizeof(text),
             "%d",
             (int) scale_param_value(priv,
                                      (gfloat)(y1 - RADIUS),
                                      (gfloat) hm));

    cairo_text_extents(cr, text, &extents);

    baseline = y1 - extents.y_bearing - (extents.height / 2.0);

    if (baseline + extents.y_bearing < 1.0)
      baseline = 1.0 - extents.y_bearing;

    if (baseline + extents.y_bearing + extents.height >
        RADIUS + hm - 1.0)
    {
      baseline = RADIUS + hm - 1.0 - extents.y_bearing - extents.height;
    }

    cairo_move_to(cr, x1, baseline);
    cairo_show_text(cr, text);
  }

  cairo_set_source_rgba(cr,
                        priv->grid.red,
                        priv->grid.green,
                        priv->grid.blue,
                        priv->grid.alpha * 0.20);
  cairo_set_line_width(cr, 0.5);
  gtk3_curve_draw_line(cr,
                       0.0,
                       RADIUS + hm + 0.5,
                       allocation_width,
                       RADIUS + hm + 0.5);

  cairo_restore(cr);
}


static gint
gtk3_curve_live_trace_primary_trace(Gtk3CurvePrivate *priv)
{
  if (!priv)
    return -1;

  if (priv->live_trace_active[GTK3_CURVE_LIVE_TRACE_MAX - 1])
    return GTK3_CURVE_LIVE_TRACE_MAX - 1;

  for (gint t = GTK3_CURVE_LIVE_TRACE_MAX - 2; t >= 0; t--) {
    if (priv->live_trace_active[t])
      return t;
  }

  return -1;
}

static gboolean
gtk3_curve_curve_value_at_x(Gtk3CurvePrivate *priv,
                            gfloat            x_value,
                            gfloat           *value)
{
  gfloat ry = 0.0f;

  if (!priv || !value || !isfinite(x_value))
    return FALSE;

  if (!isfinite(priv->min_x) || !isfinite(priv->max_x) ||
      priv->max_x <= priv->min_x)
    return FALSE;

  if (!isfinite(priv->min_y) || !isfinite(priv->max_y) ||
      priv->max_y <= priv->min_y)
    return FALSE;

  if (x_value < priv->min_x || x_value > priv->max_x)
    return FALSE;

  switch (priv->curve_data.curve_type) {
    case GTK3_CURVE_TYPE_FREE:
      if (priv->frame_vector && priv->frame_vector_len > 0) {
        gfloat src_min = priv->frame_vector_min_x;
        gfloat src_max = priv->frame_vector_max_x;
        gfloat src_span = src_max - src_min;
        gfloat src_pos;
        gint idx0;
        gint idx1;
        gfloat frac;
        gfloat y0;
        gfloat y1;

        if (!isfinite(src_min))
          src_min = priv->min_x;
        if (!isfinite(src_max) || src_max <= src_min)
          src_max = priv->max_x;

        src_span = src_max - src_min;

        if (priv->frame_vector_len == 1 || src_span <= 0.0f) {
          ry = priv->frame_vector[0];
          break;
        }

        src_pos = ((x_value - src_min) / src_span) *
                  (gfloat)(priv->frame_vector_len - 1);

        if (src_pos < 0.0f)
          src_pos = 0.0f;
        else if (src_pos > (gfloat)(priv->frame_vector_len - 1))
          src_pos = (gfloat)(priv->frame_vector_len - 1);

        idx0 = (gint) floorf(src_pos);
        idx1 = idx0 + 1;
        frac = src_pos - (gfloat) idx0;

        if (idx0 < 0)
          idx0 = 0;
        else if (idx0 >= priv->frame_vector_len)
          idx0 = priv->frame_vector_len - 1;

        if (idx1 < 0)
          idx1 = 0;
        else if (idx1 >= priv->frame_vector_len)
          idx1 = priv->frame_vector_len - 1;

        y0 = priv->frame_vector[idx0];
        y1 = priv->frame_vector[idx1];
        ry = y0 + ((y1 - y0) * frac);
      }
      else if (priv->curve_data.d_point &&
               priv->curve_data.n_points > 0 &&
               priv->height > 1)
      {
        gfloat src_pos;
        gint idx0;
        gint idx1;
        gfloat frac;
        gfloat y0;
        gfloat y1;

        if (priv->curve_data.n_points == 1) {
          ry = unproject(RADIUS + priv->height - priv->curve_data.d_point[0].y,
                         priv->min_y,
                         priv->max_y,
                         priv->height);
          break;
        }

        src_pos = ((x_value - priv->min_x) / (priv->max_x - priv->min_x)) *
                  (gfloat)(priv->curve_data.n_points - 1);

        if (src_pos < 0.0f)
          src_pos = 0.0f;
        else if (src_pos > (gfloat)(priv->curve_data.n_points - 1))
          src_pos = (gfloat)(priv->curve_data.n_points - 1);

        idx0 = (gint) floorf(src_pos);
        idx1 = idx0 + 1;
        frac = src_pos - (gfloat) idx0;

        if (idx0 < 0)
          idx0 = 0;
        else if (idx0 >= priv->curve_data.n_points)
          idx0 = priv->curve_data.n_points - 1;

        if (idx1 < 0)
          idx1 = 0;
        else if (idx1 >= priv->curve_data.n_points)
          idx1 = priv->curve_data.n_points - 1;

        y0 = unproject(RADIUS + priv->height - priv->curve_data.d_point[idx0].y,
                       priv->min_y,
                       priv->max_y,
                       priv->height);
        y1 = unproject(RADIUS + priv->height - priv->curve_data.d_point[idx1].y,
                       priv->min_y,
                       priv->max_y,
                       priv->height);

        ry = y0 + ((y1 - y0) * frac);
      }
      else {
        ry = priv->min_y;
      }
      break;

    case GTK3_CURVE_TYPE_LINEAR:
    case GTK3_CURVE_TYPE_SPLINE:
    default:
    {
      gint count = 0;
      gint first = -1;
      gfloat prev = priv->min_x - 1.0f;

      for (gint i = 0; i < priv->curve_data.n_cpoints; i++) {
        if (priv->curve_data.d_cpoints[i].x > prev) {
          if (first < 0)
            first = i;
          prev = priv->curve_data.d_cpoints[i].x;
          count++;
        }
      }

      if (count <= 0) {
        ry = priv->min_y;
        break;
      }

      if (count == 1) {
        ry = priv->curve_data.d_cpoints[first].y;
        break;
      }

      if (priv->curve_data.curve_type == GTK3_CURVE_TYPE_SPLINE) {
        gfloat *mem = g_malloc(3 * count * sizeof(gfloat));
        gfloat *xv;
        gfloat *yv;
        gfloat *y2v;
        gint dst = 0;

        if (!mem) {
          ry = priv->min_y;
          break;
        }

        xv = mem;
        yv = mem + count;
        y2v = mem + (2 * count);

        prev = priv->min_x - 1.0f;
        for (gint i = 0; i < priv->curve_data.n_cpoints; i++) {
          if (priv->curve_data.d_cpoints[i].x > prev) {
            prev = priv->curve_data.d_cpoints[i].x;
            xv[dst] = priv->curve_data.d_cpoints[i].x;
            yv[dst] = priv->curve_data.d_cpoints[i].y;
            if (yv[dst] < priv->min_y)
              yv[dst] = priv->min_y;
            else if (yv[dst] > priv->max_y)
              yv[dst] = priv->max_y;
            dst++;
          }
        }

        spline_solve(count, xv, yv, y2v);
        ry = spline_eval(count, xv, yv, y2v, x_value);
        g_free(mem);
      }
      else {
        gfloat *mem = g_malloc(2 * count * sizeof(gfloat));
        gfloat *xv;
        gfloat *yv;
        gint dst = 0;
        gint seg = 0;

        if (!mem) {
          ry = priv->min_y;
          break;
        }

        xv = mem;
        yv = mem + count;

        prev = priv->min_x - 1.0f;
        for (gint i = 0; i < priv->curve_data.n_cpoints; i++) {
          if (priv->curve_data.d_cpoints[i].x > prev) {
            prev = priv->curve_data.d_cpoints[i].x;
            xv[dst] = priv->curve_data.d_cpoints[i].x;
            yv[dst] = priv->curve_data.d_cpoints[i].y;
            if (yv[dst] < priv->min_y)
              yv[dst] = priv->min_y;
            else if (yv[dst] > priv->max_y)
              yv[dst] = priv->max_y;
            dst++;
          }
        }

        xv[0] = priv->min_x;
        xv[count - 1] = priv->max_x;

        while (seg + 1 < count - 1 && x_value > xv[seg + 1])
          seg++;

        if (x_value <= xv[0]) {
          ry = yv[0];
        }
        else if (x_value >= xv[count - 1]) {
          ry = yv[count - 1];
        }
        else {
          gfloat x0 = xv[seg];
          gfloat x1 = xv[seg + 1];
          gfloat y0 = yv[seg];
          gfloat y1 = yv[seg + 1];
          gfloat den = x1 - x0;

          if (den <= 0.0f) {
            ry = y0;
          }
          else {
            gfloat t = (x_value - x0) / den;
            if (t < 0.0f)
              t = 0.0f;
            else if (t > 1.0f)
              t = 1.0f;
            ry = y0 + ((y1 - y0) * t);
          }
        }

        g_free(mem);
      }
      break;
    }
  }

  if (!isfinite(ry))
    ry = priv->min_y;

  if (ry < priv->min_y)
    ry = priv->min_y;
  else if (ry > priv->max_y)
    ry = priv->max_y;

  *value = ry;
  return TRUE;
}


static gboolean
gtk3_curve_display_curve_value_at_position(Gtk3CurvePrivate *priv,
                                            gint              width,
                                            gint              height,
                                            gfloat            x_value,
                                            gdouble          *mx,
                                            gdouble          *my,
                                            gfloat           *value)
{
  gdouble screen_x;
  gdouble screen_y;
  gfloat v;

  if (!priv || !value || width <= 1 || height <= 1)
    return FALSE;

  if (!priv->curve_data.d_point || priv->curve_data.n_points <= 0)
    return FALSE;

  if (!isfinite(x_value) || !isfinite(priv->min_x) || !isfinite(priv->max_x) ||
      !isfinite(priv->min_y) || !isfinite(priv->max_y) ||
      priv->max_x <= priv->min_x || priv->max_y <= priv->min_y)
    return FALSE;

  if (x_value < priv->min_x || x_value > priv->max_x)
    return FALSE;

  screen_x = RADIUS + projectd(x_value, priv->min_x, priv->max_x, width);

  if (screen_x <= priv->curve_data.d_point[0].x) {
    screen_x = priv->curve_data.d_point[0].x;
    screen_y = priv->curve_data.d_point[0].y;
  }
  else if (screen_x >= priv->curve_data.d_point[priv->curve_data.n_points - 1].x) {
    screen_x = priv->curve_data.d_point[priv->curve_data.n_points - 1].x;
    screen_y = priv->curve_data.d_point[priv->curve_data.n_points - 1].y;
  }
  else {
    gint left = 0;
    gint right = priv->curve_data.n_points - 1;

    while (right - left > 1) {
      gint mid = left + ((right - left) / 2);
      if ((gdouble)priv->curve_data.d_point[mid].x <= screen_x)
        left = mid;
      else
        right = mid;
    }

    {
      gdouble x0 = priv->curve_data.d_point[left].x;
      gdouble y0 = priv->curve_data.d_point[left].y;
      gdouble x1 = priv->curve_data.d_point[right].x;
      gdouble y1 = priv->curve_data.d_point[right].y;
      gdouble den = x1 - x0;
      gdouble t = 0.0;

      if (den > 0.000001)
        t = (screen_x - x0) / den;

      if (t < 0.0)
        t = 0.0;
      else if (t > 1.0)
        t = 1.0;

      screen_y = y0 + ((y1 - y0) * t);
    }
  }

  v = unprojectd((RADIUS + height) - screen_y,
                 priv->min_y,
                 priv->max_y,
                 height);

  if (!isfinite(v))
    v = priv->min_y;

  if (v < priv->min_y)
    v = priv->min_y;
  else if (v > priv->max_y)
    v = priv->max_y;

  if (mx)
    *mx = screen_x;
  if (my)
    *my = screen_y;

  *value = v;
  return TRUE;
}

static gboolean
gtk3_curve_live_trace_value_at_x(Gtk3CurvePrivate *priv,
                                 gfloat            x_value,
                                 gfloat           *value,
                                 Gtk3CurveColor   *color)
{
  gint trace;
  gboolean have_left = FALSE;
  gboolean have_right = FALSE;
  gboolean have_nearest = FALSE;
  gfloat left_x = 0.0f;
  gfloat right_x = 0.0f;
  gfloat nearest_x = 0.0f;
  gfloat left_v = 0.0f;
  gfloat right_v = 0.0f;
  gfloat nearest_v = 0.0f;
  gfloat nearest_d = G_MAXFLOAT;
  gfloat jump_threshold;

  if (!priv || !value || !priv->live_trace_enabled ||
      !priv->live_trace_point_used || !priv->live_trace_x ||
      !priv->live_trace_values || !isfinite(x_value))
    return FALSE;

  trace = gtk3_curve_live_trace_primary_trace(priv);
  if (trace < 0)
    return FALSE;

  jump_threshold = gtk3_curve_live_trace_draw_jump_threshold(priv);
  if (jump_threshold < 1.0f)
    jump_threshold = 1.0f;

  gint iteration_count = gtk3_curve_live_trace_iteration_count(priv);

  for (gint order = 0; order < iteration_count; order++) {
    gint idx;
    gfloat px;
    gfloat pv;
    gfloat d;

    if (!gtk3_curve_live_trace_point_at_order(priv,
                                              trace,
                                              order,
                                              &idx,
                                              &px,
                                              &pv))
      continue;

    if (!isfinite(px) || !isfinite(pv))
      continue;

    d = fabsf(px - x_value);
    if (!have_nearest || d < nearest_d) {
      have_nearest = TRUE;
      nearest_d = d;
      nearest_x = px;
      nearest_v = pv;
    }

    if (px <= x_value) {
      if (!have_left || px > left_x) {
        have_left = TRUE;
        left_x = px;
        left_v = pv;
      }
    }

    if (px >= x_value) {
      if (!have_right || px < right_x) {
        have_right = TRUE;
        right_x = px;
        right_v = pv;
      }
    }
  }

  if (have_left && have_right) {
    gfloat span = right_x - left_x;

    if (fabsf(span) <= 0.0001f) {
      *value = right_v;
    }
    else if (span <= jump_threshold) {
      gfloat t = (x_value - left_x) / span;
      if (t < 0.0f)
        t = 0.0f;
      else if (t > 1.0f)
        t = 1.0f;
      *value = left_v + ((right_v - left_v) * t);
    }
    else if (have_nearest && nearest_d <= jump_threshold) {
      *value = nearest_v;
    }
    else {
      return FALSE;
    }
  }
  else if (have_nearest && nearest_d <= jump_threshold) {
    (void)nearest_x;
    *value = nearest_v;
  }
  else {
    return FALSE;
  }

  *value = gtk3_curve_live_clamp_value(priv, *value);

  if (color)
    *color = priv->live_trace_color[trace];

  return TRUE;
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

  priv->marker_live_trace = FALSE;

  if (width <= 1 || height <= 1)
    return FALSE;

  gdouble pos = priv->current_position;

  if (priv->live_trace_enabled && !priv->live_trace_user_override) {
    if (gtk3_curve_live_trace_use_local_axis(priv))
      return FALSE;

    Gtk3CurveGraphRect gr;
    gfloat live_value = 0.0f;
    Gtk3CurveColor live_color;
    gdouble px;

    memset(&live_color, 0, sizeof(live_color));

    if (gtk3_curve_live_graph_rect(width, height, &gr) &&
        gtk3_curve_live_trace_value_at_x(priv, (gfloat)pos, &live_value, &live_color) &&
        gtk3_curve_live_project_x(priv, &gr, (gfloat)pos, &px)) {
      if (mx)
        *mx = px;
      if (my)
        *my = gtk3_curve_live_project_y_norm(priv, &gr, live_value);
      if (value)
        *value = live_value;

      priv->marker_live_trace = TRUE;
      priv->marker_live_color = live_color;
      return TRUE;
    }

    return FALSE;
  }

  if (priv->max_x <= priv->min_x)
    return FALSE;

  if (priv->max_y <= priv->min_y)
    return FALSE;

  if (pos < priv->min_x || pos > priv->max_x) {
    priv->marker_hover = FALSE;
    return FALSE;
  }

  gfloat v = 0.0f;
  gdouble px = 0.0;
  gdouble py = 0.0;

  if (gtk3_curve_display_curve_value_at_position(priv, width, height, (gfloat)pos, &px, &py, &v)) {
    if (mx)
      *mx = px;
    if (my)
      *my = py;
    if (value)
      *value = v;

    return TRUE;
  }

  if (!gtk3_curve_curve_value_at_x(priv, (gfloat)pos, &v))
    return FALSE;

  gint x = project((gfloat) pos, priv->min_x, priv->max_x, width);
  gint y = project(v, priv->min_y, priv->max_y, height);

  px = RADIUS + x;
  py = RADIUS + height - y;

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
                         gtk3_curve_font_family(widget),
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, gtk3_curve_font_px(widget, 0.82));

  cairo_text_extents(cr, line1, &e1);
  cairo_text_extents(cr, line2, &e2);
  cairo_text_extents(cr, line3, &e3);

  text_w = MAX(e1.width, e2.width);
  text_w = MAX(text_w, e3.width);

  box_w = text_w + 14.0;
  box_h = MAX(48.0,
              gtk3_curve_font_px(widget, 0.82) * 3.0 + 15.0);

  box_x = px + 12.0;
  box_y = py - 26.0;

  if (box_x + box_w > allocation_width - 2.0)
    box_x = px - box_w - 12.0;

  if (box_y < 2.0)
    box_y = py + 12.0;

  if (box_y + box_h > allocation_height - 2.0)
    box_y = allocation_height - box_h - 2.0;

  {
    Gtk3CurveColor legend_bg =
      gtk3_curve_legend_background_color(widget, priv, 0.90f);
    Gtk3CurveColor legend_border =
      gtk3_curve_legend_border_color(widget, priv, 0.72f);
    Gtk3CurveColor legend_text =
      gtk3_curve_legend_text_color(widget, priv, 0.95f);

    cairo_set_source_rgba(cr,
                          legend_bg.red,
                          legend_bg.green,
                          legend_bg.blue,
                          legend_bg.alpha);
    cairo_rectangle(cr, box_x, box_y, box_w, box_h);
    cairo_fill(cr);

    cairo_set_source_rgba(cr,
                          legend_border.red,
                          legend_border.green,
                          legend_border.blue,
                          legend_border.alpha);
    cairo_rectangle(cr, box_x + 0.5, box_y + 0.5, box_w - 1.0, box_h - 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr,
                          legend_text.red,
                          legend_text.green,
                          legend_text.blue,
                          legend_text.alpha);
  }

  {
    gdouble line_step = gtk3_curve_font_px(widget, 0.82) + 4.0;
    gdouble first_line = box_y + gtk3_curve_font_px(widget, 0.82) + 4.0;

    cairo_move_to(cr, box_x + 7.0, first_line);
    cairo_show_text(cr, line1);

    cairo_move_to(cr, box_x + 7.0, first_line + line_step);
    cairo_show_text(cr, line2);

    cairo_move_to(cr, box_x + 7.0, first_line + line_step * 2.0);
    cairo_show_text(cr, line3);
  }

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

  Gtk3CurveColor marker_color;
  if (priv->marker_live_trace) {
    marker_color = priv->marker_live_color;
    if (marker_color.alpha <= 0.0f)
      marker_color.alpha = 0.95f;
  } else {
    marker_color.red = 1.000000f;
    marker_color.green = 0.517647f;
    marker_color.blue = 0.000000f;
    marker_color.alpha = 0.95f;
  }

  cairo_set_source_rgba(cr,
                        marker_color.red,
                        marker_color.green,
                        marker_color.blue,
                        0.18);
  cairo_arc(cr, mx, my, 8.0, 0.0, 2.0 * M_PI);
  cairo_fill(cr);

  cairo_set_source_rgba(cr,
                        marker_color.red,
                        marker_color.green,
                        marker_color.blue,
                        MIN(1.0, marker_color.alpha + 0.25));
  cairo_arc(cr, mx, my, 4.5, 0.0, 2.0 * M_PI);
  cairo_fill(cr);

  cairo_set_source_rgba(cr,
                        marker_color.red * 0.45,
                        marker_color.green * 0.45,
                        marker_color.blue * 0.45,
                        0.95);
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
  gboolean          live_scope;
  gboolean          hide_curve_for_live_trace;
  gboolean          live_trace_underlay;

  curve = GTK3_CURVE(widget);
  priv = curve->priv;
  live_scope = priv->live_trace_enabled && gtk3_curve_live_trace_use_local_axis(priv);
  hide_curve_for_live_trace = live_scope ||
                              (!priv->live_trace_user_override &&
                               priv->live_trace_enabled &&
                               !gtk3_curve_live_trace_use_local_axis(priv) &&
                               gtk3_curve_curve_is_visually_flat(priv));
  live_trace_underlay = priv->live_trace_enabled &&
                        !gtk3_curve_live_trace_use_local_axis(priv) &&
                        !hide_curve_for_live_trace;

  if (!cr)
    return TRUE;

  gtk_widget_get_allocation(widget, &allocation);

  gint width  = gtk3_curve_graph_width_from_allocation(allocation.width);
  gint height = gtk3_curve_graph_height_from_allocation(widget, allocation.height);

  if (width <= 1 || height <= 1)
    return FALSE;

  if ((priv->curve_data.d_point || priv->curve_data.d_cpoints) &&
    (priv->width != width ||
     priv->height != height ||
     priv->curve_data.n_points != width))
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

  gfloat wm = width;
  gfloat hm = height;

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
    gfloat grid_min_x = gtk3_curve_live_axis_min_x(priv);
    gfloat grid_max_x = gtk3_curve_live_axis_max_x(priv);
    gint min_frame = (gint)(grid_min_x + 0.5f);
    gint max_frame = (gint)(grid_max_x + 0.5f);
    gint frame_count = MAX(1, (gint)(fabsf(grid_max_x - grid_min_x) + 0.5f));
    gint step = gtk3_curve_live_trace_use_local_axis(priv) ?
                gtk3_curve_nice_x_grid_step(frame_count, 0.0) :
                gtk3_curve_visible_x_grid_step(priv, width);

    if (step <= 0)
      step = 1;

    for (gint frame = min_frame; frame <= max_frame; frame += step) {
      gdouble gx = RADIUS + project((gfloat) frame,
                                    grid_min_x,
                                    grid_max_x,
                                    (gint) wm);

      gtk3_curve_draw_line(cr, gx, RADIUS, gx, hm + RADIUS);
    }

    if (((max_frame - min_frame) % step) != 0) {
      gdouble gx = RADIUS + project((gfloat) max_frame,
                                    grid_min_x,
                                    grid_max_x,
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

  gtk3_curve_draw_labels(widget,
                         cr,
                         allocation.width,
                         allocation.height,
                         width,
                         height);

  gtk3_curve_draw_x_zoom_indicators(widget,
                                    cr,
                                    allocation.width,
                                    width,
                                    height);

  if (live_trace_underlay)
    gtk3_curve_draw_live_traces(widget,
                                cr,
                                allocation.width,
                                width,
                                height,
                                TRUE);

  if (!hide_curve_for_live_trace && priv->curve_data.d_point && priv->curve_data.n_points > 1) {
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

  if (!hide_curve_for_live_trace && priv->curve_data.curve_type != GTK3_CURVE_TYPE_FREE) {
    for (int i = 0; i < priv->curve_data.n_cpoints; ++i) {
      gdouble x, y;

      if (priv->curve_data.d_cpoints[i].x < priv->min_x)
        continue;

      if (priv->curve_data.d_cpoints[i].x > priv->max_x)
        continue;

      x = RADIUS + gtk3_curve_project_cpoint_x(priv, priv->curve_data.d_cpoints[i].x, width);

      y = RADIUS + hm - gtk3_curve_project_cpoint_y(priv, priv->curve_data.d_cpoints[i].y, height);
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

  if (!live_trace_underlay)
    gtk3_curve_draw_live_traces(widget,
                                cr,
                                allocation.width,
                                width,
                                height,
                                FALSE);

  if (!hide_curve_for_live_trace && priv->hover_cpoint >= 0) {
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

  gtk3_curve_draw_cursor_legend(widget,
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

  gtk_widget_get_allocation(widget, &allocation);

  width = gtk3_curve_graph_width_from_allocation(allocation.width);
  height = gtk3_curve_graph_height_from_allocation(widget, allocation.height);

  if (width <= 1 || height <= 1)
    return FALSE;

  gtk3_curve_get_cursor_coord(widget, &tx, &ty);

  {
    gboolean hit_left = FALSE;
    gboolean hit_right = FALSE;
    gboolean in_nav = FALSE;

    if (gtk3_curve_x_nav_hit(widget, tx, ty, &hit_left, &hit_right, &in_nav)) {
      gfloat span = priv->max_x - priv->min_x;
      gfloat delta;

      if (span <= 0.0f)
        span = 1.0f;

      delta = span * 0.20f;

      if (hit_left)
        gtk3_curve_pan_x(widget, -delta);
      else if (hit_right)
        gtk3_curve_pan_x(widget, delta);

      return TRUE;
    }

    if (in_nav)
      return FALSE;
  }

  if (tx < RADIUS ||
      tx > RADIUS + width ||
      ty < RADIUS ||
      ty > RADIUS + height)
    return FALSE;

  gtk3_curve_live_trace_disable_for_user_edit(widget);

  gtk_grab_add(widget);

  if (!priv->curve_data.d_point || priv->curve_data.n_points != width)
    gtk3_curve_interpolate(widget, width, height);

  x = CLAMP((tx - RADIUS), 0, width);
  y = CLAMP((ty - RADIUS), 0, height);

  gfloat min_x = priv->min_x;

  for (int i = 0; i < priv->curve_data.n_cpoints; ++i) {
    gint cx = gtk3_curve_project_cpoint_x(priv, priv->curve_data.d_cpoints[i].x, width);

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
          gint cx = gtk3_curve_project_cpoint_x(priv, priv->curve_data.d_cpoints[closest_point].x, width);

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
        gtk3_curve_unproject_cpoint_x(priv, x, width);

      priv->curve_data.d_cpoints[priv->grab_point].y =
        gtk3_curve_unproject_cpoint_y(priv, height - y, height);

      gtk3_curve_interpolate(widget, width, height);
      break;

    case GTK3_CURVE_TYPE_FREE:
      if (priv->curve_data.d_point && priv->curve_data.n_points > 0) {
        gint fx = CLAMP(x, 0, priv->curve_data.n_points - 1);

        g_free(priv->frame_vector);
        priv->frame_vector = NULL;
        priv->frame_vector_len = 0;

        priv->curve_data.d_point[fx].x = RADIUS + fx;
        priv->curve_data.d_point[fx].y = RADIUS + y;
        priv->grab_point = fx;
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

  width = gtk3_curve_graph_width_from_allocation(allocation.width);
  height = gtk3_curve_graph_height_from_allocation(widget, allocation.height);

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

  width = gtk3_curve_graph_width_from_allocation(allocation.width);
  height = gtk3_curve_graph_height_from_allocation(widget, allocation.height);

  if (width <= 1 || height <= 1)
    return FALSE;

  gtk3_curve_get_cursor_coord(widget, &tx, &ty);

  {
    gboolean hit_left = FALSE;
    gboolean hit_right = FALSE;
    gboolean in_nav = FALSE;

    gtk3_curve_x_nav_hit(widget, tx, ty, &hit_left, &hit_right, &in_nav);

    if (in_nav) {
      new_type = (hit_left || hit_right) ? GDK_HAND2 : GDK_TOP_LEFT_ARROW;

      priv->in_curve = FALSE;
      priv->hover_cpoint = -1;
      priv->hover_cpoint_x = 0.0;
      priv->hover_cpoint_y = 0.0;
      priv->hover_cpoint_value = 0.0f;
      priv->hover_cpoint_position = 0.0f;

      if (priv->marker_hover) {
        priv->marker_hover = FALSE;
        hover_changed = TRUE;
      }

      if (new_type != (GdkCursorType) priv->cursor_type) {
        GdkCursor *cursor;

        priv->cursor_type = new_type;

        cursor = gdk_cursor_new_for_display(gtk_widget_get_display(widget),
                                            priv->cursor_type);

        gdk_window_set_cursor(gtk_widget_get_window(widget), cursor);
        g_object_unref(cursor);
      }

      if (hover_changed && gtk_widget_is_visible(widget))
        gtk_widget_queue_draw(widget);

      return TRUE;
    }
  }

  x = CLAMP((tx - RADIUS), 0, width);
  y = CLAMP((ty - RADIUS), 0, height);

  priv->in_curve =
    (tx >= RADIUS &&
    tx <  RADIUS + width &&
    ty >= RADIUS &&
    ty <  RADIUS + height);

  priv->last_x = (gfloat) x;
  priv->last_y = (gfloat) y;

  if (priv->draw_position) {
    gboolean marker_in_view =
      (priv->current_position >= priv->min_x &&
       priv->current_position <= priv->max_x);

    if (!marker_in_view) {
      if (priv->marker_hover) {
        priv->marker_hover = FALSE;
        hover_changed = TRUE;
      }
    } else {
      gdouble dx = (gdouble) tx - priv->marker_x;
      gdouble dy = (gdouble) ty - priv->marker_y;
      gboolean marker_hover = ((dx * dx + dy * dy) <= (12.0 * 12.0));

      if (marker_hover != priv->marker_hover) {
        priv->marker_hover = marker_hover;
        hover_changed = TRUE;
      }
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

      cx = RADIUS + gtk3_curve_project_cpoint_x(priv, priv->curve_data.d_cpoints[i].x, width);

      cy = RADIUS + height - gtk3_curve_project_cpoint_y(priv, priv->curve_data.d_cpoints[i].y, height);

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
    gint cx = gtk3_curve_project_cpoint_x(priv, priv->curve_data.d_cpoints[i].x, width);

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
            RADIUS + gtk3_curve_project_cpoint_x(priv, priv->curve_data.d_cpoints[priv->grab_point - 1].x, width);
        }

        if (priv->grab_point + 1 < priv->curve_data.n_cpoints) {
          rightbound =
            RADIUS + gtk3_curve_project_cpoint_x(priv, priv->curve_data.d_cpoints[priv->grab_point + 1].x, width);
        }

        if (tx <= leftbound ||
            tx >= rightbound ||
            ty > height + RADIUS * 2 + MIN_DISTANCE ||
            ty < -MIN_DISTANCE)
        {
          priv->curve_data.d_cpoints[priv->grab_point].x = min_x - 1.0f;
        } else {
          priv->curve_data.d_cpoints[priv->grab_point].x =
            gtk3_curve_unproject_cpoint_x(priv, x, width);

          priv->curve_data.d_cpoints[priv->grab_point].y =
            gtk3_curve_unproject_cpoint_y(priv, height - y, height);
        }

        gtk3_curve_interpolate(widget, width, height);
        changed = TRUE;
      }
      break;

    case GTK3_CURVE_TYPE_FREE:
      if (priv->grab_point != -1 && priv->curve_data.d_point && priv->curve_data.n_points > 0) {
        gint fx = CLAMP(x, 0, priv->curve_data.n_points - 1);
        gint x1, x2, y1, y2;

        if (priv->frame_vector) {
          g_free(priv->frame_vector);
          priv->frame_vector = NULL;
          priv->frame_vector_len = 0;
        }

        if (priv->grab_point > fx) {
          x1 = fx;
          x2 = priv->grab_point;
          y1 = y;
          y2 = priv->last;
        } else {
          x1 = priv->grab_point;
          x2 = fx;
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
        } else {
          priv->curve_data.d_point[fx].x = RADIUS + fx;
          priv->curve_data.d_point[fx].y = RADIUS + y;
        }

        priv->grab_point = fx;
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

  if (changed)
    gtk3_curve_live_trace_disable_for_user_edit(widget);

  if ((changed || hover_changed || priv->in_curve) && gtk_widget_is_visible(widget))
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
  if (priv->frame_vector)
    g_free(priv->frame_vector);
  g_free(priv->live_trace_values);
  g_free(priv->live_trace_x);
  g_free(priv->live_trace_point_used);
  g_free(priv->live_trace_slot_used);

  G_OBJECT_CLASS (gtk3_curve_parent_class)->finalize (object);
}

void
gtk3_curve_clear(GtkWidget *widget)
{
    Gtk3Curve *curve = GTK3_CURVE(widget);
    Gtk3CurvePrivate *priv = curve->priv;

    g_free(priv->curve_data.d_point);
    g_free(priv->curve_data.d_cpoints);

    priv->curve_data.d_point = NULL;
    priv->curve_data.n_points = 0;

    priv->curve_data.d_cpoints = NULL;
    priv->curve_data.n_cpoints = 0;

    g_free(priv->frame_vector);
    priv->frame_vector = NULL;
    priv->frame_vector_len = 0;
    priv->frame_vector_min_x = priv->min_x;
    priv->frame_vector_max_x = priv->max_x;

    priv->grab_point = -1;
    priv->last = 0;

    priv->hover_cpoint = -1;
    priv->hover_cpoint_x = 0.0;
    priv->hover_cpoint_y = 0.0;
    priv->hover_cpoint_value = 0.0f;
    priv->hover_cpoint_position = 0.0f;

    priv->marker_x = 0.0;
    priv->marker_y = 0.0;
    priv->marker_value = 0.0f;
    priv->marker_hover = FALSE;

    priv->width = 0;
    priv->height = 0;

    if (priv->live_trace_enabled) {
        gtk3_curve_live_trace_clear_samples(priv);
        priv->live_trace_auto_x = NAN;
        priv->live_trace_last_input_x = 0.0f;
        priv->live_trace_pending_x = NAN;
        priv->live_trace_have_last_input = FALSE;
        priv->live_trace_pending_valid = FALSE;
        priv->live_trace_dot_enabled = FALSE;
        priv->live_trace_dot_x = 0.0f;
        priv->live_trace_dot_base_value = 0.0f;
        priv->live_trace_dot_value = 0.0f;
        memset(&priv->live_trace_dot_color, 0, sizeof(priv->live_trace_dot_color));
        priv->live_trace_dot_label[0] = '\0';
    }

    if (gtk_widget_is_visible(widget))
        gtk_widget_queue_draw(widget);
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

  gint width = gtk3_curve_graph_width_from_allocation(gtk_widget_get_allocated_width(widget));
  gint height = gtk3_curve_graph_height_from_allocation(widget, gtk_widget_get_allocated_height(widget));

  if (width <= 1 || height <= 1 || !gtk3_curve_has_curve_data(priv)) {
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
  gfloat *new_frame_vector;
  gint width;
  gint height;

  if (!widget || !vector || veclen <= 0)
    return;

  DEBUG_INFO("set vector [S]\n");
  DEBUG_INFO("vector len [%d]\n", veclen);

  new_frame_vector = g_malloc(sizeof(new_frame_vector[0]) * veclen);
  if (!new_frame_vector)
    return;

  for (int i = 0; i < veclen; i++) {
    gfloat v = vector[i];

    if (v > priv->max_y)
      v = priv->max_y;
    else if (v < priv->min_y)
      v = priv->min_y;

    new_frame_vector[i] = v;
  }

  g_free(priv->frame_vector);
  priv->frame_vector = new_frame_vector;
  priv->frame_vector_len = veclen;
  priv->frame_vector_min_x = priv->min_x;
  priv->frame_vector_max_x = priv->max_x;

  old_type = priv->curve_data.curve_type;
  priv->curve_data.curve_type = GTK3_CURVE_TYPE_FREE;

  width = gtk3_curve_graph_width_from_allocation(gtk_widget_get_allocated_width(widget));
  height = gtk3_curve_graph_height_from_allocation(widget, gtk_widget_get_allocated_height(widget));

  if (width <= 1)
    width = veclen;

  if (height <= 1) {
    height = priv->height;
    if (height <= 1)
      height = (gint)(priv->max_y - priv->min_y);
    if (height <= 1)
      height = 128;
  }

  gtk3_curve_interpolate(widget, width, height);

  if (old_type != GTK3_CURVE_TYPE_FREE) {
    g_signal_emit(curve, curve_type_changed_signal, 0);
    g_object_notify(G_OBJECT(curve), "curve-type");
  }

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
  return (int)(projectd(value, min, max, norm) + 0.5);
}

static gdouble projectd(gfloat value, gfloat min, gfloat max, int norm)
{
  gdouble t;

  if (norm <= 1)
    return 0.0;

  if (max == min)
    return 0.0;

  t = ((gdouble)value - (gdouble)min) / ((gdouble)max - (gdouble)min);

  if (t < 0.0)
    t = 0.0;
  else if (t > 1.0)
    t = 1.0;

  return ((gdouble)norm - 1.0) * t;
}

static gfloat unproject(gint value, gfloat min, gfloat max, int norm)
{
  return unprojectd((gdouble)value, min, max, norm);
}

static gfloat unprojectd(gdouble value, gfloat min, gfloat max, int norm)
{
  if (norm <= 1)
    return min;

  if (max == min)
    return min;

  if (value < 0.0)
    value = 0.0;
  else if (value > (gdouble)(norm - 1))
    value = (gdouble)(norm - 1);

  return (gfloat)((value / (gdouble)(norm - 1)) * ((gdouble)max - (gdouble)min) + (gdouble)min);
}

static gint
gtk3_curve_project_cpoint_x(Gtk3CurvePrivate *priv, gfloat value, gint width)
{
  return project(value, priv->min_x, priv->max_x, width + 1);
}

static gint
gtk3_curve_project_cpoint_y(Gtk3CurvePrivate *priv, gfloat value, gint height)
{
  return project(value, priv->min_y, priv->max_y, height + 1);
}

static gfloat
gtk3_curve_unproject_cpoint_x(Gtk3CurvePrivate *priv, gint value, gint width)
{
  return unproject(value, priv->min_x, priv->max_x, width + 1);
}

static gfloat
gtk3_curve_unproject_cpoint_y(Gtk3CurvePrivate *priv, gint value, gint height)
{
  return unproject(value, priv->min_y, priv->max_y, height + 1);
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

static void
gtk3_curve_set_range_internal(GtkWidget *widget,
                              gfloat    min_x,
                              gfloat    max_x,
                              gfloat    min_y,
                              gfloat    max_y,
                              gboolean  update_timeline,
                              gboolean  rescale_cpoints)
{
    Gtk3Curve *curve = GTK3_CURVE(widget);
    Gtk3CurvePrivate *priv = curve->priv;

    gfloat old_min_x = priv->min_x;
    gfloat old_max_x = priv->max_x;
    gfloat old_min_y = priv->min_y;
    gfloat old_max_y = priv->max_y;
    gfloat old_timeline_min_x = priv->timeline_min_x;
    gfloat old_timeline_max_x = priv->timeline_max_x;

    gboolean had_curve =
        ((priv->curve_data.d_point &&
          priv->curve_data.n_points > 0) ||
         (priv->curve_data.d_cpoints &&
          priv->curve_data.n_cpoints > 0));

    if (max_x <= min_x)
        max_x = min_x + 1.0f;

    if (max_y <= min_y)
        max_y = min_y + 1.0f;

    if (update_timeline) {
        priv->timeline_min_x = min_x;
        priv->timeline_max_x = max_x;
    } else {
        if (priv->timeline_max_x <= priv->timeline_min_x) {
            priv->timeline_min_x = min_x;
            priv->timeline_max_x = max_x;
        }

        if (min_x < priv->timeline_min_x)
            min_x = priv->timeline_min_x;

        if (max_x > priv->timeline_max_x)
            max_x = priv->timeline_max_x;

        if (max_x <= min_x)
            max_x = min_x + 1.0f;

        if (max_x > priv->timeline_max_x) {
            max_x = priv->timeline_max_x;
            min_x = max_x - 1.0f;
        }
    }

    gint old_x_len = (gint)(fabsf(old_max_x - old_min_x) + 0.5f);
    gint new_x_len = (gint)(fabsf(max_x - min_x) + 0.5f);

    gboolean x_len_changed = (old_x_len != new_x_len);
    gboolean timeline_changed =
        update_timeline &&
        (old_timeline_min_x != min_x ||
         old_timeline_max_x != max_x);

    gboolean range_changed =
        old_min_x != min_x ||
        old_max_x != max_x ||
        old_min_y != min_y ||
        old_max_y != max_y;

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

    priv->y_grid_resolution =
        gtk3_curve_auto_y_grid_resolution(priv->min_y, priv->max_y);

    gtk3_curve_update_x_grid(priv);

    if (timeline_changed && priv->live_trace_enabled)
        gtk3_curve_live_trace_clear(widget);

    if (had_curve && range_changed) {
        gint width  = gtk3_curve_graph_width_from_allocation(gtk_widget_get_allocated_width(widget));
        gint height = gtk3_curve_graph_height_from_allocation(widget, gtk_widget_get_allocated_height(widget));

        if (width > 1 && height > 1) {
            if (rescale_cpoints &&
                x_len_changed &&
                priv->curve_data.d_cpoints &&
                priv->curve_data.n_cpoints > 0)
            {
                gfloat old_span = old_max_x - old_min_x;
                gfloat new_span = priv->max_x - priv->min_x;

                if (old_span != 0.0f && new_span != 0.0f) {
                    for (int i = 0; i < priv->curve_data.n_cpoints; i++) {
                        gfloat t =
                            (priv->curve_data.d_cpoints[i].x - old_min_x) /
                            old_span;

                        if (t < 0.0f)
                            t = 0.0f;
                        else if (t > 1.0f)
                            t = 1.0f;

                        priv->curve_data.d_cpoints[i].x =
                            priv->min_x + (t * new_span);
                    }
                }
            }

            gtk3_curve_interpolate(widget, width, height);
        }
    }

    if (gtk_widget_is_visible(widget))
        gtk_widget_queue_draw(widget);
}

void
gtk3_curve_set_range(GtkWidget *widget,
                     gfloat    min_x,
                     gfloat    max_x,
                     gfloat    min_y,
                     gfloat    max_y)
{
    gtk3_curve_set_range_internal(widget,
                                  min_x,
                                  max_x,
                                  min_y,
                                  max_y,
                                  TRUE,
                                  TRUE);
}

void
gtk3_curve_set_x_timeline(GtkWidget *widget, gfloat min_x, gfloat max_x)
{
    Gtk3Curve *curve;
    Gtk3CurvePrivate *priv;

    if (!widget)
        return;

    curve = GTK3_CURVE(widget);
    priv = curve->priv;

    if (max_x <= min_x)
        max_x = min_x + 1.0f;

    gboolean timeline_changed =
        (priv->timeline_min_x != min_x ||
         priv->timeline_max_x != max_x);

    priv->timeline_min_x = min_x;
    priv->timeline_max_x = max_x;

    if (timeline_changed && priv->live_trace_enabled)
        gtk3_curve_live_trace_clear(widget);

    gtk3_curve_set_x_view(widget, priv->min_x, priv->max_x);
}

void
gtk3_curve_get_x_timeline(GtkWidget *widget, gfloat *min_x, gfloat *max_x)
{
    Gtk3Curve *curve;
    Gtk3CurvePrivate *priv;

    if (!widget)
        return;

    curve = GTK3_CURVE(widget);
    priv = curve->priv;

    if (min_x)
        *min_x = priv->timeline_min_x;

    if (max_x)
        *max_x = priv->timeline_max_x;
}

void
gtk3_curve_set_x_view(GtkWidget *widget, gfloat min_x, gfloat max_x)
{
    Gtk3Curve *curve;
    Gtk3CurvePrivate *priv;
    gfloat span;
    gfloat domain_span;

    if (!widget)
        return;

    curve = GTK3_CURVE(widget);
    priv = curve->priv;

    if (gtk3_curve_live_trace_use_local_axis(priv)) {
        gfloat domain_min = priv->live_trace_local_min_x;
        gfloat domain_max = priv->live_trace_local_max_x;
        gfloat span;
        gfloat domain_span;

        if (!isfinite(domain_min))
            domain_min = 0.0f;
        if (!isfinite(domain_max) || domain_max <= domain_min)
            domain_max = domain_min + 1.0f;
        if (max_x <= min_x)
            max_x = min_x + 1.0f;

        domain_span = domain_max - domain_min;
        span = max_x - min_x;

        if (span >= domain_span) {
            min_x = domain_min;
            max_x = domain_max;
        } else {
            if (min_x < domain_min) {
                max_x += domain_min - min_x;
                min_x = domain_min;
            }
            if (max_x > domain_max) {
                min_x -= max_x - domain_max;
                max_x = domain_max;
            }
            if (min_x < domain_min)
                min_x = domain_min;
            if (max_x > domain_max)
                max_x = domain_max;
        }

        priv->live_trace_view_min_x = min_x;
        priv->live_trace_view_max_x = max_x;
        if (gtk_widget_is_visible(widget))
            gtk_widget_queue_draw(widget);
        return;
    }

    if (priv->timeline_max_x <= priv->timeline_min_x) {
        priv->timeline_min_x = priv->min_x;
        priv->timeline_max_x = priv->max_x;
    }

    if (max_x <= min_x)
        max_x = min_x + 1.0f;

    domain_span = priv->timeline_max_x - priv->timeline_min_x;
    span = max_x - min_x;

    if (domain_span <= 0.0f)
        domain_span = 1.0f;

    if (span > domain_span) {
        min_x = priv->timeline_min_x;
        max_x = priv->timeline_max_x;
    } else {
        if (min_x < priv->timeline_min_x) {
            max_x += priv->timeline_min_x - min_x;
            min_x = priv->timeline_min_x;
        }

        if (max_x > priv->timeline_max_x) {
            min_x -= max_x - priv->timeline_max_x;
            max_x = priv->timeline_max_x;
        }

        if (min_x < priv->timeline_min_x)
            min_x = priv->timeline_min_x;

        if (max_x > priv->timeline_max_x)
            max_x = priv->timeline_max_x;
    }

    gtk3_curve_set_range_internal(widget,
                                  min_x,
                                  max_x,
                                  priv->min_y,
                                  priv->max_y,
                                  FALSE,
                                  FALSE);
}

void
gtk3_curve_get_x_view(GtkWidget *widget, gfloat *min_x, gfloat *max_x)
{
    Gtk3Curve *curve;
    Gtk3CurvePrivate *priv;

    if (!widget)
        return;

    curve = GTK3_CURVE(widget);
    priv = curve->priv;

    if (gtk3_curve_live_trace_use_local_axis(priv)) {
        if (min_x)
            *min_x = priv->live_trace_view_min_x;
        if (max_x)
            *max_x = priv->live_trace_view_max_x;
        return;
    }

    if (min_x)
        *min_x = priv->min_x;

    if (max_x)
        *max_x = priv->max_x;
}

gboolean
gtk3_curve_get_x_load_range(GtkWidget *widget, gint *start, gint *end)
{
    Gtk3Curve *curve;
    Gtk3CurvePrivate *priv;
    gint s;
    gint e;

    if (!widget || !start || !end)
        return FALSE;

    curve = GTK3_CURVE(widget);
    priv = curve->priv;

    s = (gint) floorf(priv->min_x + 0.5f);
    e = (gint) floorf(priv->max_x + 0.5f);

    if (e < s) {
        gint t = s;
        s = e;
        e = t;
    }

    if (s < (gint) floorf(priv->timeline_min_x + 0.5f))
        s = (gint) floorf(priv->timeline_min_x + 0.5f);

    if (e > (gint) floorf(priv->timeline_max_x + 0.5f))
        e = (gint) floorf(priv->timeline_max_x + 0.5f);

    if (e < s)
        return FALSE;

    *start = s;
    *end = e;
    return TRUE;
}

gboolean
gtk3_curve_is_x_zoomed(GtkWidget *widget)
{
    Gtk3Curve *curve;
    Gtk3CurvePrivate *priv;

    if (!widget)
        return FALSE;

    curve = GTK3_CURVE(widget);
    priv = curve->priv;

    if (gtk3_curve_live_trace_use_local_axis(priv))
        return (priv->live_trace_view_min_x > priv->live_trace_local_min_x + 0.5f ||
                priv->live_trace_view_max_x < priv->live_trace_local_max_x - 0.5f);

    return (priv->min_x > priv->timeline_min_x + 0.5f ||
            priv->max_x < priv->timeline_max_x - 0.5f);
}

void
gtk3_curve_zoom_x(GtkWidget *widget, gfloat center_x, gfloat factor)
{
    Gtk3Curve *curve;
    Gtk3CurvePrivate *priv;
    gfloat old_min;
    gfloat old_max;
    gfloat domain_min;
    gfloat domain_max;
    gfloat old_span;
    gfloat new_span;
    gfloat domain_span;
    gfloat t;
    gfloat new_min;
    gfloat new_max;

    if (!widget)
        return;

    curve = GTK3_CURVE(widget);
    priv = curve->priv;

    if (factor <= 0.0f || factor == 1.0f)
        return;

    if (gtk3_curve_live_trace_use_local_axis(priv)) {
        old_min = priv->live_trace_view_min_x;
        old_max = priv->live_trace_view_max_x;
        domain_min = priv->live_trace_local_min_x;
        domain_max = priv->live_trace_local_max_x;
    } else {
        if (priv->timeline_max_x <= priv->timeline_min_x) {
            priv->timeline_min_x = priv->min_x;
            priv->timeline_max_x = priv->max_x;
        }
        old_min = priv->min_x;
        old_max = priv->max_x;
        domain_min = priv->timeline_min_x;
        domain_max = priv->timeline_max_x;
    }

    old_span = old_max - old_min;
    domain_span = domain_max - domain_min;

    if (old_span <= 1.0f)
        old_span = 1.0f;

    if (domain_span <= 1.0f)
        domain_span = 1.0f;

    new_span = old_span / factor;

    if (new_span < 1.0f)
        new_span = 1.0f;
    else if (new_span > domain_span)
        new_span = domain_span;

    if (center_x < old_min)
        center_x = old_min;
    else if (center_x > old_max)
        center_x = old_max;

    t = (old_span > 0.0f) ? ((center_x - old_min) / old_span) : 0.5f;

    if (t < 0.0f)
        t = 0.0f;
    else if (t > 1.0f)
        t = 1.0f;

    new_min = center_x - (new_span * t);
    new_max = new_min + new_span;

    gtk3_curve_set_x_view(widget, new_min, new_max);
}

void
gtk3_curve_pan_x(GtkWidget *widget, gfloat delta)
{
    gfloat view_min;
    gfloat view_max;

    if (!widget || delta == 0.0f)
        return;

    gtk3_curve_get_x_view(widget, &view_min, &view_max);
    gtk3_curve_set_x_view(widget,
                          view_min + delta,
                          view_max + delta);
}

void
gtk3_curve_reset_x_zoom(GtkWidget *widget)
{
    Gtk3Curve *curve;
    Gtk3CurvePrivate *priv;

    if (!widget)
        return;

    curve = GTK3_CURVE(widget);
    priv = curve->priv;

    if (gtk3_curve_live_trace_use_local_axis(priv)) {
        gtk3_curve_set_x_view(widget,
                              priv->live_trace_local_min_x,
                              priv->live_trace_local_max_x);
        return;
    }

    gtk3_curve_set_x_view(widget,
                          priv->timeline_min_x,
                          priv->timeline_max_x);
}

void
gtk3_curve_get_vector(GtkWidget *widget, int veclen, gfloat vector[])
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;
  gfloat rx, ry, dx, min_x;
  gfloat *mem, *xv, *yv, *y2v, prev;
  gint dst, i, x;
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

      if (priv->frame_vector && priv->frame_vector_len > 0) {
        gfloat src_min = priv->frame_vector_min_x;
        gfloat src_max = priv->frame_vector_max_x;
        gfloat src_span = src_max - src_min;
        gfloat view_span = priv->max_x - priv->min_x;

        if (src_span <= 0.0f)
          src_span = (priv->frame_vector_len > 1) ?
            (gfloat)(priv->frame_vector_len - 1) : 1.0f;

        if (view_span <= 0.0f)
          view_span = 1.0f;

        for (x = 0; x < veclen; ++x) {
          gfloat frame;
          gfloat src_pos;
          gint idx0;
          gint idx1;
          gfloat frac;
          gfloat y0;
          gfloat y1;

          if (veclen > 1)
            frame = priv->min_x + (view_span * (gfloat)x) / (gfloat)(veclen - 1);
          else
            frame = priv->min_x;

          src_pos = ((frame - src_min) / src_span) *
                    (gfloat)(priv->frame_vector_len - 1);

          if (src_pos < 0.0f)
            src_pos = 0.0f;
          else if (src_pos > (gfloat)(priv->frame_vector_len - 1))
            src_pos = (gfloat)(priv->frame_vector_len - 1);

          idx0 = (gint) floorf(src_pos);
          idx1 = idx0 + 1;
          frac = src_pos - (gfloat) idx0;

          if (idx0 < 0)
            idx0 = 0;
          else if (idx0 >= priv->frame_vector_len)
            idx0 = priv->frame_vector_len - 1;

          if (idx1 < 0)
            idx1 = 0;
          else if (idx1 >= priv->frame_vector_len)
            idx1 = priv->frame_vector_len - 1;

          y0 = priv->frame_vector[idx0];
          y1 = priv->frame_vector[idx1];
          ry = y0 + ((y1 - y0) * frac);

          if (ry < priv->min_y)
            ry = priv->min_y;
          else if (ry > priv->max_y)
            ry = priv->max_y;

          vector[x] = ry;
        }
      }
      else if (priv->curve_data.d_point &&
          priv->curve_data.n_points > 0 &&
          priv->height > 1)
      {
        if (veclen == 1 || priv->curve_data.n_points == 1) {
          gint idx = 0;

          ry = unproject(RADIUS + priv->height - priv->curve_data.d_point[idx].y,
                         priv->min_y,
                         priv->max_y,
                         priv->height);

          if (ry < priv->min_y)
            ry = priv->min_y;
          else if (ry > priv->max_y)
            ry = priv->max_y;

          vector[0] = ry;
        } else {
          for (x = 0; x < veclen; ++x) {
            gfloat src_pos = ((gfloat)x *
                              (gfloat)(priv->curve_data.n_points - 1)) /
                             (gfloat)(veclen - 1);
            gint idx0 = (gint) floorf(src_pos);
            gint idx1 = idx0 + 1;
            gfloat frac = src_pos - (gfloat) idx0;
            gfloat y0, y1;

            if (idx0 < 0)
              idx0 = 0;
            else if (idx0 >= priv->curve_data.n_points)
              idx0 = priv->curve_data.n_points - 1;

            if (idx1 < 0)
              idx1 = 0;
            else if (idx1 >= priv->curve_data.n_points)
              idx1 = priv->curve_data.n_points - 1;

            y0 = unproject(RADIUS + priv->height - priv->curve_data.d_point[idx0].y,
                           priv->min_y,
                           priv->max_y,
                           priv->height);
            y1 = unproject(RADIUS + priv->height - priv->curve_data.d_point[idx1].y,
                           priv->min_y,
                           priv->max_y,
                           priv->height);

            ry = y0 + ((y1 - y0) * frac);

            if (ry < priv->min_y)
              ry = priv->min_y;
            else if (ry > priv->max_y)
              ry = priv->max_y;

            vector[x] = ry;
          }
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

}


Gtk3CurveData
gtk3_curve_load(gchar *filename) {

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

  g_free(priv->frame_vector);
  priv->frame_vector = NULL;
  priv->frame_vector_len = 0;
  priv->frame_vector_min_x = priv->min_x;
  priv->frame_vector_max_x = priv->max_x;

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

  width = gtk3_curve_graph_width_from_allocation(gtk_widget_get_allocated_width(widget));
  height = gtk3_curve_graph_height_from_allocation(widget, gtk_widget_get_allocated_height(widget));

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


static gfloat
gtk3_curve_live_trace_prepare_x(Gtk3CurvePrivate *priv,
                                gfloat            input_x)
{
  gfloat min_x;
  gfloat max_x;
  gfloat x;

  if (!priv)
    return 0.0f;

  min_x = gtk3_curve_live_domain_min_x(priv);
  max_x = gtk3_curve_live_domain_max_x(priv);

  if (!isfinite(min_x))
    min_x = 0.0f;
  if (!isfinite(max_x) || max_x <= min_x)
    max_x = min_x + 1.0f;

  x = input_x;
  if (!isfinite(x))
    x = (gfloat) priv->current_position;
  if (!isfinite(x))
    x = min_x;

  if (x < min_x)
    x = min_x;
  else if (x > max_x)
    x = max_x;

  if (priv->live_trace_pending_valid &&
      fabsf(x - priv->live_trace_pending_x) <= 0.0001f)
    return priv->live_trace_pending_x;

  priv->live_trace_auto_x = x;
  priv->live_trace_pending_x = x;
  priv->live_trace_pending_valid = TRUE;
  priv->live_trace_clear_on_next_push = FALSE;

  return x;
}

static gboolean
gtk3_curve_live_trace_slot_is_in_range(gint slot,
                                       gint first,
                                       gint last)
{
  if (first > last) {
    gint tmp = first;
    first = last;
    last = tmp;
  }

  return slot >= first && slot <= last;
}

static void
gtk3_curve_live_trace_clear_slot_range(Gtk3CurvePrivate *priv,
                                       gint              first,
                                       gint              last)
{
  if (!priv || !priv->live_trace_slot_used)
    return;

  if (first > last) {
    gint tmp = first;
    first = last;
    last = tmp;
  }

  if (first < 0)
    first = 0;
  if (last >= GTK3_CURVE_LIVE_TRACE_LEN)
    last = GTK3_CURVE_LIVE_TRACE_LEN - 1;

  for (gint idx = first; idx <= last; idx++) {
    if (!priv->live_trace_slot_used[idx])
      continue;

    for (gint t = 0; t < GTK3_CURVE_LIVE_TRACE_MAX; t++) {
      if (priv->live_trace_point_used)
        priv->live_trace_point_used[t][idx] = FALSE;
      if (priv->live_trace_values)
        priv->live_trace_values[t][idx] = 0.0f;
      if (priv->live_trace_x)
        priv->live_trace_x[t][idx] = 0.0f;
      if (priv->live_trace_last_slot_for[t] == idx)
        priv->live_trace_last_slot_for[t] = -1;
    }

    priv->live_trace_slot_used[idx] = FALSE;
    if (priv->live_trace_count > 0)
      priv->live_trace_count--;
  }

  if (gtk3_curve_live_trace_slot_is_in_range(priv->live_trace_last_slot, first, last))
    priv->live_trace_last_slot = -1;
}

static gfloat
gtk3_curve_live_trace_jump_threshold(Gtk3CurvePrivate *priv)
{
  gfloat min_x;
  gfloat max_x;
  gfloat span;
  gfloat threshold;

  if (!priv)
    return 2.0f;

  min_x = gtk3_curve_live_domain_min_x(priv);
  max_x = gtk3_curve_live_domain_max_x(priv);

  if (!isfinite(min_x))
    min_x = 0.0f;
  if (!isfinite(max_x) || max_x <= min_x)
    max_x = min_x + 1.0f;

  span = max_x - min_x;
  if (span < 1.0f)
    span = 1.0f;

  threshold = MAX(2.0f, span * 0.02f);
  if (threshold > 96.0f)
    threshold = 96.0f;

  return threshold;
}

static gfloat
gtk3_curve_live_trace_draw_jump_threshold(Gtk3CurvePrivate *priv)
{
  gfloat min_x;
  gfloat max_x;
  gfloat span;
  gfloat threshold;

  if (!priv)
    return 8.0f;

  min_x = gtk3_curve_live_domain_min_x(priv);
  max_x = gtk3_curve_live_domain_max_x(priv);

  if (!isfinite(min_x))
    min_x = 0.0f;
  if (!isfinite(max_x) || max_x <= min_x)
    max_x = min_x + 1.0f;

  span = max_x - min_x;
  if (span < 1.0f)
    span = 1.0f;

  if (gtk3_curve_live_trace_use_local_axis(priv)) {
    threshold = MAX(4.0f, span * 0.06f);
    if (threshold > 96.0f)
      threshold = 96.0f;
  }
  else {
    if (span <= 64.0f)
      threshold = span + 1.0f;
    else
      threshold = MAX(16.0f, span * 0.15f);

    if (threshold > 384.0f)
      threshold = 384.0f;
  }

  if (threshold < 1.0f)
    threshold = 1.0f;

  return threshold;
}

static void
gtk3_curve_live_trace_clear_overwrite(Gtk3CurvePrivate *priv,
                                      gfloat            x_value)
{
  gfloat delta;
  gfloat jump_threshold;
  gint previous_slot;
  gint current_slot;

  if (!priv || !isfinite(x_value))
    return;

  current_slot = gtk3_curve_live_trace_slot_for_x(priv, x_value);

  if (!priv->live_trace_have_source_x) {
    gtk3_curve_live_trace_clear_slot_range(priv, current_slot, current_slot);
    priv->live_trace_last_source_x = x_value;
    priv->live_trace_have_source_x = TRUE;
    return;
  }

  delta = x_value - priv->live_trace_last_source_x;

  if (gtk3_curve_live_trace_use_local_axis(priv) && delta < -0.0001f) {
    gtk3_curve_live_trace_clear_slot_range(priv, current_slot, current_slot);
    priv->live_trace_last_source_x = x_value;
    priv->live_trace_have_source_x = TRUE;
    return;
  }

  if (fabsf(delta) <= 0.0001f)
    return;

  previous_slot = gtk3_curve_live_trace_slot_for_x(priv, priv->live_trace_last_source_x);
  jump_threshold = gtk3_curve_live_trace_jump_threshold(priv);

  if (delta < -jump_threshold)
    gtk3_curve_live_trace_clear_slot_range(priv, current_slot, GTK3_CURVE_LIVE_TRACE_LEN - 1);
  else if (delta < -0.0001f)
    gtk3_curve_live_trace_clear_slot_range(priv, current_slot, current_slot);
  else if (delta > jump_threshold)
    gtk3_curve_live_trace_clear_slot_range(priv, previous_slot + 1, current_slot);
  else
    gtk3_curve_live_trace_clear_slot_range(priv, current_slot, current_slot);

  priv->live_trace_last_source_x = x_value;
  priv->live_trace_have_source_x = TRUE;
}

static gint
gtk3_curve_live_trace_clock_write_slot(Gtk3CurvePrivate *priv,
                                       gfloat            x_value)
{
  gint capacity;
  gint write_pos;
  gboolean new_sample;

  if (!priv)
    return 0;

  capacity = gtk3_curve_live_trace_history_capacity(priv);
  new_sample = (!priv->live_trace_have_source_x ||
                fabsf(x_value - priv->live_trace_last_source_x) > 0.0001f);

  if (!new_sample &&
      priv->live_trace_last_slot >= 0 &&
      priv->live_trace_last_slot < capacity &&
      priv->live_trace_slot_used[priv->live_trace_last_slot])
    return priv->live_trace_last_slot;

  write_pos = priv->live_trace_pos;
  if (write_pos < 0 || write_pos >= capacity)
    write_pos = 0;

  gtk3_curve_live_trace_clear_slot_range(priv, write_pos, write_pos);

  priv->live_trace_slot_used[write_pos] = TRUE;
  if (priv->live_trace_count < capacity)
    priv->live_trace_count++;

  priv->live_trace_last_slot = write_pos;
  priv->live_trace_pos = (write_pos + 1) % capacity;
  priv->live_trace_last_source_x = x_value;
  priv->live_trace_have_source_x = TRUE;

  return write_pos;
}

static gboolean G_GNUC_UNUSED
gtk3_curve_live_trace_is_explicit_wrap(Gtk3CurvePrivate *priv,
                                      gint              trace,
                                      gfloat            x_value)
{
  gfloat min_x;
  gfloat max_x;
  gboolean wrapped;

  (void) trace;

  if (!priv || !isfinite(x_value))
    return FALSE;

  min_x = gtk3_curve_live_domain_min_x(priv);
  max_x = gtk3_curve_live_domain_max_x(priv);

  if (!isfinite(min_x))
    min_x = 0.0f;
  if (!isfinite(max_x) || max_x <= min_x)
    max_x = min_x + 1.0f;

  if (x_value < min_x)
    x_value = min_x;
  else if (x_value > max_x)
    x_value = max_x;

  if (!priv->live_trace_have_last_input) {
    priv->live_trace_last_input_x = x_value;
    priv->live_trace_have_last_input = TRUE;
    return FALSE;
  }

  wrapped = FALSE;

  if (x_value < priv->live_trace_last_input_x - 0.0001f) {
    gfloat span = max_x - min_x;
    gfloat margin = span * 0.02f;

    if (margin < 2.0f)
      margin = span > 4.0f ? 2.0f : span * 0.25f;
    else if (margin > 24.0f)
      margin = 24.0f;

    if (margin > span * 0.25f)
      margin = span * 0.25f;

    wrapped = (priv->live_trace_last_input_x >= max_x - margin &&
               x_value <= min_x + margin);
  }

  priv->live_trace_last_input_x = x_value;

  return wrapped;
}

static Gtk3CurvePrivate *
gtk3_curve_live_trace_priv(GtkWidget *widget)
{
  Gtk3Curve *curve;

  if (!widget || !GTK_IS_WIDGET(widget) || !GTK3_IS_CURVE(widget))
    return NULL;

  curve = GTK3_CURVE(widget);
  if (!curve || !curve->priv)
    return NULL;

  return curve->priv;
}

static void
gtk3_curve_live_trace_sanitize(Gtk3CurvePrivate *priv)
{
  if (!priv)
    return;

  if (priv->live_trace_pos < 0 ||
      priv->live_trace_pos >= GTK3_CURVE_LIVE_TRACE_LEN)
    priv->live_trace_pos = 0;

  if (priv->live_trace_last_slot < -1 ||
      priv->live_trace_last_slot >= GTK3_CURVE_LIVE_TRACE_LEN)
    priv->live_trace_last_slot = -1;

  for (gint t = 0; t < GTK3_CURVE_LIVE_TRACE_MAX; t++) {
    if (priv->live_trace_last_slot_for[t] < -1 ||
        priv->live_trace_last_slot_for[t] >= GTK3_CURVE_LIVE_TRACE_LEN)
      priv->live_trace_last_slot_for[t] = -1;
  }

  if (priv->live_trace_count < 0)
    priv->live_trace_count = 0;
  else if (priv->live_trace_count > GTK3_CURVE_LIVE_TRACE_LEN)
    priv->live_trace_count = GTK3_CURVE_LIVE_TRACE_LEN;
}

void
gtk3_curve_live_trace_clear(GtkWidget *widget)
{
  Gtk3CurvePrivate *priv = gtk3_curve_live_trace_priv(widget);

  if (!priv)
    return;

  gtk3_curve_live_trace_clear_samples(priv);
  priv->live_trace_auto_x = NAN;
  priv->live_trace_last_input_x = 0.0f;
  priv->live_trace_pending_x = NAN;
  priv->live_trace_have_last_input = FALSE;
  priv->live_trace_pending_valid = FALSE;
  priv->live_trace_clear_on_next_push = FALSE;
  priv->live_trace_have_source_x = FALSE;
  priv->live_trace_last_source_x = 0.0f;
  priv->live_trace_dot_enabled = FALSE;
  priv->live_trace_dot_x = 0.0f;
  priv->live_trace_dot_base_value = 0.0f;
  priv->live_trace_dot_value = 0.0f;
  memset(&priv->live_trace_dot_color, 0, sizeof(priv->live_trace_dot_color));
  priv->live_trace_dot_label[0] = '\0';

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}


gboolean
gtk3_curve_live_trace_get_user_override(GtkWidget *widget)
{
  Gtk3CurvePrivate *priv = gtk3_curve_live_trace_priv(widget);

  return priv ? priv->live_trace_user_override : FALSE;
}

void
gtk3_curve_live_trace_set_user_override(GtkWidget *widget, gboolean enabled)
{
  Gtk3CurvePrivate *priv = gtk3_curve_live_trace_priv(widget);

  if (!priv)
    return;

  enabled = enabled ? TRUE : FALSE;

  if (priv->live_trace_user_override == enabled)
    return;

  priv->live_trace_user_override = enabled;

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}

void
gtk3_curve_live_trace_set_enabled(GtkWidget *widget, gboolean enabled)
{
  Gtk3CurvePrivate *priv = gtk3_curve_live_trace_priv(widget);

  if (!priv)
    return;

  enabled = enabled ? TRUE : FALSE;

  if (priv->live_trace_enabled == enabled) {
    if (!enabled && priv->live_trace_domain != GTK3_CURVE_LIVE_TRACE_DOMAIN_FRAME) {
      priv->live_trace_domain = GTK3_CURVE_LIVE_TRACE_DOMAIN_FRAME;
      priv->live_trace_local_min_x = 0.0f;
      priv->live_trace_local_max_x = (gfloat)(GTK3_CURVE_LIVE_TRACE_LEN - 1);
      priv->live_trace_view_min_x = priv->live_trace_local_min_x;
      priv->live_trace_view_max_x = priv->live_trace_local_max_x;
      if (gtk_widget_is_visible(widget))
        gtk_widget_queue_draw(widget);
    }
    return;
  }

  priv->live_trace_enabled = enabled;

  if (!enabled) {
    priv->live_trace_domain = GTK3_CURVE_LIVE_TRACE_DOMAIN_FRAME;
    priv->live_trace_local_min_x = 0.0f;
    priv->live_trace_local_max_x = (gfloat)(GTK3_CURVE_LIVE_TRACE_LEN - 1);
    priv->live_trace_view_min_x = priv->live_trace_local_min_x;
    priv->live_trace_view_max_x = priv->live_trace_local_max_x;
    gtk3_curve_live_trace_clear(widget);
  }
  else if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}

void
gtk3_curve_live_trace_set_domain(GtkWidget              *widget,
                                  Gtk3CurveLiveTraceDomain domain,
                                  gfloat                  min_x,
                                  gfloat                  max_x)
{
  Gtk3CurvePrivate *priv = gtk3_curve_live_trace_priv(widget);
  gboolean changed;

  if (!priv)
    return;

  if (domain != GTK3_CURVE_LIVE_TRACE_DOMAIN_CLOCK)
    domain = GTK3_CURVE_LIVE_TRACE_DOMAIN_FRAME;

  if (!isfinite(min_x))
    min_x = 0.0f;
  if (!isfinite(max_x) || max_x <= min_x)
    max_x = min_x + 1.0f;

  changed = (priv->live_trace_domain != domain ||
             fabsf(priv->live_trace_local_min_x - min_x) > 0.0001f ||
             fabsf(priv->live_trace_local_max_x - max_x) > 0.0001f);

  priv->live_trace_domain = domain;
  priv->live_trace_local_min_x = min_x;
  priv->live_trace_local_max_x = max_x;

  if (changed) {
    priv->live_trace_view_min_x = min_x;
    priv->live_trace_view_max_x = max_x;
    gtk3_curve_live_trace_clear(widget);
  }
  else if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}

void
gtk3_curve_live_trace_push_at(GtkWidget   *widget,
                              gint         trace,
                              gfloat       x_value,
                              gfloat       value,
                              const gchar *label,
                              gfloat       red,
                              gfloat       green,
                              gfloat       blue,
                              gfloat       alpha)
{
  Gtk3CurvePrivate *priv = gtk3_curve_live_trace_priv(widget);

  if (!priv)
    return;

  if (trace < 0 || trace >= GTK3_CURVE_LIVE_TRACE_MAX)
    return;

  gtk3_curve_live_trace_sanitize(priv);

  if (!priv->live_trace_enabled)
    return;

  if (!isfinite(x_value))
    x_value = gtk3_curve_live_trace_prepare_x(priv, NAN);

  {
    gfloat min_x = gtk3_curve_live_domain_min_x(priv);
    gfloat max_x = gtk3_curve_live_domain_max_x(priv);

    if (!isfinite(min_x))
      min_x = 0.0f;
    if (!isfinite(max_x) || max_x <= min_x)
      max_x = min_x + 1.0f;

    if (x_value < min_x)
      x_value = min_x;
    else if (x_value > max_x)
      x_value = max_x;
  }

  value = gtk3_curve_live_clamp_value(priv, value);

  priv->live_trace_clear_on_next_push = FALSE;

  gint write_pos;

  if (gtk3_curve_live_trace_use_local_axis(priv)) {
    write_pos = gtk3_curve_live_trace_clock_write_slot(priv, x_value);
  }
  else {
    gboolean replace_previous;

    gtk3_curve_live_trace_clear_overwrite(priv, x_value);

    write_pos = gtk3_curve_live_trace_slot_for_x(priv, x_value);
    replace_previous = priv->live_trace_slot_used[write_pos];

    priv->live_trace_slot_used[write_pos] = TRUE;
    priv->live_trace_last_slot = write_pos;
    priv->live_trace_pos = write_pos;

    if (!replace_previous && priv->live_trace_count < GTK3_CURVE_LIVE_TRACE_LEN)
      priv->live_trace_count++;
  }

  if (priv->live_trace_point_used)
    priv->live_trace_point_used[trace][write_pos] = TRUE;
  priv->live_trace_last_slot_for[trace] = write_pos;

  priv->live_trace_x[trace][write_pos] = x_value;
  priv->live_trace_values[trace][write_pos] = value;
  priv->live_trace_active[trace] = TRUE;
  priv->live_trace_color[trace].red = CLAMP(red, 0.0f, 1.0f);
  priv->live_trace_color[trace].green = CLAMP(green, 0.0f, 1.0f);
  priv->live_trace_color[trace].blue = CLAMP(blue, 0.0f, 1.0f);
  priv->live_trace_color[trace].alpha = CLAMP(alpha, 0.0f, 1.0f);

  if (label && label[0] != '\0')
    g_strlcpy(priv->live_trace_label[trace], label, sizeof(priv->live_trace_label[trace]));
  else
    priv->live_trace_label[trace][0] = '\0';

  if (trace == GTK3_CURVE_LIVE_TRACE_MAX - 1 && gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}

void
gtk3_curve_live_trace_push(GtkWidget   *widget,
                           gint         trace,
                           gfloat       value,
                           const gchar *label,
                           gfloat       red,
                           gfloat       green,
                           gfloat       blue,
                           gfloat       alpha)
{
  Gtk3CurvePrivate *priv = gtk3_curve_live_trace_priv(widget);
  gfloat x_value = priv ? gtk3_curve_live_trace_prepare_x(priv, (gfloat)priv->current_position) : 0.0f;

  gtk3_curve_live_trace_push_at(widget,
                                trace,
                                x_value,
                                value,
                                label,
                                red,
                                green,
                                blue,
                                alpha);

  if (priv && trace == GTK3_CURVE_LIVE_TRACE_MAX - 1)
    priv->live_trace_pending_valid = FALSE;
}

void
gtk3_curve_live_trace_set_dot(GtkWidget   *widget,
                              gboolean     enabled,
                              gfloat       x_value,
                              gfloat       base_value,
                              gfloat       value,
                              const gchar *label,
                              gfloat       red,
                              gfloat       green,
                              gfloat       blue,
                              gfloat       alpha)
{
  Gtk3CurvePrivate *priv = gtk3_curve_live_trace_priv(widget);

  if (!priv)
    return;

  enabled = enabled ? TRUE : FALSE;

  priv->live_trace_dot_enabled = enabled;

  if (!enabled) {
    priv->live_trace_dot_label[0] = '\0';
    if (gtk_widget_is_visible(widget))
      gtk_widget_queue_draw(widget);
    return;
  }

  {
    gfloat domain_min_x = gtk3_curve_live_domain_min_x(priv);
    gfloat domain_max_x = gtk3_curve_live_domain_max_x(priv);

    if (!isfinite(domain_min_x))
      domain_min_x = 0.0f;
    if (!isfinite(domain_max_x) || domain_max_x <= domain_min_x)
      domain_max_x = domain_min_x + 1.0f;

    if (isfinite(x_value)) {
      if (x_value < domain_min_x)
        x_value = domain_min_x;
      else if (x_value > domain_max_x)
        x_value = domain_max_x;
    }
  }

  if (!isfinite(x_value)) {
    if (priv->live_trace_count > 0) {
      gint ring_len = gtk3_curve_live_trace_use_local_axis(priv) ?
        gtk3_curve_live_trace_history_capacity(priv) :
        GTK3_CURVE_LIVE_TRACE_LEN;
      gint idx = priv->live_trace_pos - 1;
      if (idx < 0)
        idx += ring_len;
      x_value = priv->live_trace_x[GTK3_CURVE_LIVE_TRACE_MAX - 1][idx];
    } else {
      x_value = gtk3_curve_live_trace_use_local_axis(priv) ? 0.0f : (gfloat) priv->current_position;
    }
  }

  if (!isfinite(base_value))
    base_value = 0.0f;
  if (!isfinite(value))
    value = base_value;

  priv->live_trace_dot_x = x_value;
  priv->live_trace_dot_base_value = gtk3_curve_live_clamp_value(priv, base_value);
  priv->live_trace_dot_value = gtk3_curve_live_clamp_value(priv, value);
  priv->live_trace_dot_color.red = CLAMP(red, 0.0f, 1.0f);
  priv->live_trace_dot_color.green = CLAMP(green, 0.0f, 1.0f);
  priv->live_trace_dot_color.blue = CLAMP(blue, 0.0f, 1.0f);
  priv->live_trace_dot_color.alpha = CLAMP(alpha, 0.0f, 1.0f);

  if (label && label[0] != '\0')
    g_strlcpy(priv->live_trace_dot_label, label, sizeof(priv->live_trace_dot_label));
  else
    priv->live_trace_dot_label[0] = '\0';

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}

void
gtk3_curve_set_position(GtkWidget *widget, gdouble pos)
{
  Gtk3Curve *curve = GTK3_CURVE(widget);
  Gtk3CurvePrivate *priv = curve->priv;

  if (priv->current_position == pos)
    return;

  priv->current_position = pos;

  if (gtk_widget_is_visible(widget))
    gtk_widget_queue_draw(widget);
}
