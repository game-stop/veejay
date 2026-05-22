/* veejay - Linux VeeJay
 *       (C) 2002-2015 Niels Elburg <nwelburg@gmail.com>
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

/*
  Implements a slider type widget with selection markers.

  Frame-index contract:
  - Timeline length is the number of visible frames in the sample.
  - Position/point are local frame indices: 0 .. length - 1.
  - Selection IN and OUT are inclusive local frame indices: 0 .. length - 1.
  - A marker covering frames 0..20 is stored as in=0, out=20.
  - Nothing is normalized to 0.0..1.0.
  - Drawing uses frame boundaries internally only for pixel placement.

  Mouse contract:
  - Left click on the playhead triangle moves the playhead.
  - Left click on the track sets IN to the clicked frame.
  - Right click on the track sets OUT to the clicked frame.
  - Middle click inside an existing selection toggles bound/scratch movement.
  - In scratch mode, the marker is translated as a rigid block.
  - In scratch mode, left/right edge edits and status in/out echoes are ignored.
  - Double left click clears the local selection.
 */

/*
 * Modified by d.j.a.y , 2018
 * - gtk3 compliant
 */

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <cairo.h>
#include <veejaycore/vj-msg.h>
#include "gtktimeselection.h"
#include "utils-gtk.h"

//#ifndef TIMELINE_DEBUG
//#define TIMELINE_DEBUG 1
//#endif

#if TIMELINE_DEBUG
#define TL_DBG(te, fmt, ...) \
    veejay_msg(VEEJAY_MSG_DEBUG, "[timeline:%p] " fmt "", (void *)(te), ##__VA_ARGS__)
#define TL_EVT(te, name, fmt, ...) \
    veejay_msg(VEEJAY_MSG_DEBUG, "[timeline:%p] %s: " fmt "", (void *)(te), (name), ##__VA_ARGS__)
#else
#define TL_DBG(te, fmt, ...) do { } while (0)
#define TL_EVT(te, name, fmt, ...) do { } while (0)
#endif

enum
{
  POS_CHANGED,
  IN_CHANGED,
  OUT_CHANGED,
  BIND_CHANGED,
  CLEAR_CHANGED,
  SELECTION_CHANGED_SIGNAL,
  POINT_CHANGED,
  LAST_SIGNAL
};

enum
{
  MIN = 1,
  MAX = 2,
  POS = 3,
  LENGTH = 4,
  IN_POINT = 5,
  OUT_POINT = 6,
  SEL = 7,
  BIND = 8,
  CLEARED = 9,
  POINT = 10,
};

typedef enum {
  MOUSE_OUTSIDE,
  MOUSE_STEPPER,
  MOUSE_SELECTION,
  MOUSE_WIDGET
} mouse_location;

typedef enum TimelineAction
{
  action_none = 0,
  action_in_point,
  action_out_point,
  action_pos,
  action_atomic,
  action_point,
} TimelineAction;

#define POINT_IN_RECT(xcoord, ycoord, rect) \
 ((xcoord) >= (rect).x &&                   \
  (xcoord) <  ((rect).x + (rect).width) &&  \
  (ycoord) >= (rect).y &&                   \
  (ycoord) <  ((rect).y + (rect).height))

struct _TimelineSelection
{
  GtkDrawingArea    cr;
  GtkWidget         *widget;
  gdouble           min;
  gdouble           max;
  gdouble           value;
  gdouble           frame_num;
  gdouble           num_video_frames;
  gdouble           in;
  gdouble           out;
  gboolean          bind;
  gint              grab_button;
  TimelineAction    action;
  mouse_location    grab_location;
  mouse_location    current_location;
  GdkRectangle      stepper;
  GdkRectangle      selection;
  gboolean          has_stepper;
  gboolean          clear;
  gdouble           stepper_size;
  gdouble           stepper_draw_size;
  gdouble           stepper_length;
  gint              step_size;
  gdouble           frame_width;
  gdouble           frame_height;
  gdouble           font_line;
  gboolean          has_selection;
  gdouble           move_x;
  gdouble           point;
  gboolean          drag_latched;
  gint              scratch_span;
  gboolean          has_ghost_selection;
  gdouble           ghost_in;
  gdouble           ghost_out;
};

static void get_property(GObject *object,
                         guint id,
                         GValue *value,
                         GParamSpec *pspec);

static void set_property(GObject *object,
                         guint id,
                         const GValue *value,
                         GParamSpec *pspec);

static gboolean event_press(GtkWidget *widget, GdkEventButton *bev, gpointer user_data);
static gboolean event_release(GtkWidget *widget, GdkEventButton *bev, gpointer user_data);
static gboolean event_motion(GtkWidget *widget, GdkEventMotion *mev, gpointer user_data);
static gboolean event_scroll(GtkWidget *widget, GdkEventScroll *mev, gpointer user_data);
static gboolean event_focus_out(GtkWidget *widget, GdkEventFocus *ev, gpointer user_data);
static gboolean event_leave(GtkWidget *widget, GdkEventCrossing *ev, gpointer user_data);
static void timeline_cancel_drag_mode(GtkWidget *widget);
static void timeline_clear_selection_ghost(TimelineSelection *te);
static void timeline_capture_selection_ghost(TimelineSelection *te);

static void timeline_class_init(TimelineSelectionClass *class);
static void timeline_init(TimelineSelection *te);
static gboolean timeline_draw(GtkWidget *widget, cairo_t *cr);

static GObjectClass *parent_class = NULL;
static gint timeline_signals[LAST_SIGNAL] = { 0 };

#define TIMELINE_STEPPER_HIT_PX     28.0
#define TIMELINE_SELECTION_HIT_PX   8.0
#define TIMELINE_MIN_FRAMES         1.0
#define TIMELINE_MIN_HEIGHT_PX      34
#define TIMELINE_LABEL_FONT_SIZE    10.0
#define TIMELINE_INFO_FONT_SIZE     10.0

#ifdef TIMELINE_DEBUG
static const char *timeline_action_name(TimelineAction action)
{
    switch (action) {
        case action_none:      return "none";
        case action_in_point:  return "in";
        case action_out_point: return "out";
        case action_pos:       return "pos";
        case action_atomic:    return "scratch";
        case action_point:     return "point";
        default:               return "?";
    }
}
#endif

static inline gdouble timeline_clamp01(gdouble v)
{
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static inline gdouble timeline_frame_count(TimelineSelection *te)
{
    return MAX(te->num_video_frames, TIMELINE_MIN_FRAMES);
}

static inline gint timeline_frame_count_i(TimelineSelection *te)
{
    gdouble nframes = timeline_frame_count(te);

    if (nframes < 1.0)
        return 1;

    return MAX(1, (gint) llround(nframes));
}

static inline gint timeline_clamp_frame_i(TimelineSelection *te, gint frame)
{
    gint nframes = timeline_frame_count_i(te);

    if (frame < 0)
        return 0;
    if (frame >= nframes)
        return nframes - 1;

    return frame;
}

static inline gdouble timeline_clamp_frame(TimelineSelection *te, gdouble frame)
{
    gint f = (gint) llround(frame);
    return (gdouble) timeline_clamp_frame_i(te, f);
}

static inline gdouble timeline_clamp_pos(TimelineSelection *te, gdouble frame)
{
    return timeline_clamp_frame(te, frame);
}

static inline gdouble timeline_x_to_norm(TimelineSelection *te, gdouble x, gdouble width)
{
    (void) te;

    if (width <= 1.0)
        return 0.0;

    return timeline_clamp01(x / width);
}

static inline gdouble timeline_x_to_frame_cont(TimelineSelection *te, gdouble x, gdouble width)
{
    return timeline_x_to_norm(te, x, width) * (gdouble) timeline_frame_count_i(te);
}

static inline gint timeline_x_to_frame_i(TimelineSelection *te, gdouble x, gdouble width)
{
    gint nframes = timeline_frame_count_i(te);
    gint frame = (gint) floor(timeline_x_to_frame_cont(te, x, width));

    if (frame < 0)
        return 0;
    if (frame >= nframes)
        return nframes - 1;

    return frame;
}

static inline gdouble timeline_x_to_pos(TimelineSelection *te, gdouble x, gdouble width)
{
    return (gdouble) timeline_x_to_frame_i(te, x, width);
}

static inline gdouble timeline_x_to_marker_frame(TimelineSelection *te, gdouble x, gdouble width)
{
    return (gdouble) timeline_x_to_frame_i(te, x, width);
}

static inline gdouble timeline_frame_boundary_to_x(TimelineSelection *te, gdouble boundary, gdouble width)
{
    gdouble nframes = (gdouble) timeline_frame_count_i(te);

    if (nframes <= 0.0)
        return 0.0;

    return CLAMP((boundary / nframes) * width, 0.0, width);
}

static inline gdouble timeline_frame_to_x(TimelineSelection *te, gdouble frame, gdouble width)
{
    gint nframes_i = timeline_frame_count_i(te);
    gdouble nframes = (gdouble) nframes_i;

    if (nframes_i <= 1)
        return width * 0.5;

    frame = timeline_clamp_frame(te, frame);

    return CLAMP(((frame + 0.5) / nframes) * width, 0.0, width);
}

static inline gdouble timeline_snap_fill_x(gdouble x, gdouble width)
{
    return CLAMP(floor(x + 0.5), 0.0, width);
}

static inline gdouble timeline_snap_line_x(gdouble x, gdouble width)
{
    if (width <= 1.0)
        return 0.5;

    return CLAMP(floor(x) + 0.5, 0.5, width - 0.5);
}

gdouble snap_to_nearest_valid_position(gdouble pos, int num_video_frames)
{
    if (num_video_frames <= 1)
        return timeline_clamp01(pos);

    pos = timeline_clamp01(pos);

    gdouble frame_interval = 1.0 / (gdouble) num_video_frames;
    gdouble snapped = round(pos / frame_interval) * frame_interval;

    return timeline_clamp01(snapped);
}

static gboolean timeline_point_near_x(gdouble x, gdouble target, gdouble radius)
{
    return fabs(x - target) <= radius;
}

static gboolean timeline_point_in_stepper_triangle(TimelineSelection *te,
                                                   gdouble x,
                                                   gdouble y,
                                                   gdouble width)
{
    if (!te->has_stepper || width <= 1.0)
        return FALSE;

    gdouble pos_x = timeline_snap_line_x(timeline_frame_to_x(te, te->frame_num, width), width);
    gdouble top_y = (gdouble) te->stepper.y;
    gdouble bottom_y = top_y + te->stepper_draw_size + 4.0;
    gdouble radius = te->stepper_draw_size + 4.0;

    if (y < top_y || y > bottom_y)
        return FALSE;

    return timeline_point_near_x(x, pos_x, radius);
}

static gboolean timeline_latch_visible(TimelineSelection *te)
{
    return te->bind && te->drag_latched && te->action == action_atomic;
}

static gboolean timeline_is_clear_echo(TimelineSelection *te, gdouble start, gdouble end)
{
    gint s = timeline_clamp_frame_i(te, (gint) llround(start));
    gint e = timeline_clamp_frame_i(te, (gint) llround(end));

    return te->clear && s == 0 && e <= 1;
}

static gboolean timeline_current_empty_clear_range(TimelineSelection *te)
{
    gint s = timeline_clamp_frame_i(te, (gint) llround(te->in));
    gint e = timeline_clamp_frame_i(te, (gint) llround(te->out));

    return te->clear && s == 0 && e <= 1;
}

static void timeline_update_cursor(GtkWidget *widget, TimelineAction action)
{
    GdkWindow *window = gtk_widget_get_window(widget);

    if (!window)
        return;

    GdkDisplay *display = gdk_window_get_display(window);
    GdkCursor *cursor = NULL;

    switch (action) {
        case action_pos:
            /* The playhead triangle is draggable, not a range-resize handle. */
            cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
            break;

        case action_atomic:
            cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
            break;

        default:
            break;
    }

    gdk_window_set_cursor(window, cursor);

    if (cursor)
        g_object_unref(cursor);
}

static gboolean timeline_point_in_selection_span(TimelineSelection *te,
                                                gdouble x,
                                                gdouble width)
{
    if (!te->has_selection || width <= 1.0)
        return FALSE;

    if (te->out < te->in)
        return FALSE;

    gdouble in_f = timeline_clamp_frame(te, te->in);
    gdouble out_f = timeline_clamp_frame(te, te->out);
    gdouble in_x = timeline_snap_fill_x(timeline_frame_boundary_to_x(te, in_f, width), width);
    gdouble out_x = timeline_snap_fill_x(timeline_frame_boundary_to_x(te, out_f + 1.0, width), width);

    if (out_x < in_x) {
        gdouble tmp = in_x;
        in_x = out_x;
        out_x = tmp;
    }

    return x >= (in_x - TIMELINE_SELECTION_HIT_PX) &&
           x <= (out_x + TIMELINE_SELECTION_HIT_PX);
}

static TimelineAction timeline_pick_action(TimelineSelection *te,
                                           gdouble x,
                                           gdouble y,
                                           gdouble width)
{
    if (timeline_point_in_stepper_triangle(te, x, y, width))
        return action_pos;

    if (te->has_selection && te->bind && timeline_point_in_selection_span(te, x, width)) {
        (void) y;
        return action_atomic;
    }

    return action_point;
}

struct _TimelineSelectionClass
{
  GtkWidgetClass parent_class;
  void (*pos_changed) (TimelineSelection *te);
  void (*point_changed) (TimelineSelection *te);
  void (*in_point_changed) (TimelineSelection *te);
  void (*out_point_changed) (TimelineSelection *te);
  void (*bind_toggled) (TimelineSelection *te);
  void (*cleared) (TimelineSelection *te);
};

static void timeline_sanitize_points(TimelineSelection *te)
{
    te->in = timeline_clamp_frame(te, te->in);
    te->out = timeline_clamp_frame(te, te->out);

    if (te->out < te->in)
        te->out = te->in;

    if (te->has_selection && te->out < te->in)
        te->has_selection = FALSE;

    te->point = timeline_clamp_pos(te, te->point);
    te->frame_num = timeline_clamp_pos(te, te->frame_num);
}

static void set_property(GObject *object,
                         guint id,
                         const GValue *value,
                         GParamSpec *pspec)
{
  TimelineSelection *te = TIMELINE_SELECTION(object);

  switch(id)
  {
    case MIN: {
      gdouble v = g_value_get_double(value);
      TL_DBG(te, "set_property(min): %.6f -> %.6f", te->min, v);
      te->min = v;
      break;
    }

    case MAX: {
      gdouble v = g_value_get_double(value);
      TL_DBG(te, "set_property(max): %.6f -> %.6f", te->max, v);
      te->max = v;
      break;
    }

    case POS: {
      gdouble v = timeline_clamp_pos(te, g_value_get_double(value));
      TL_DBG(te, "set_property(pos): %.3f -> %.3f", te->frame_num, v);
      te->frame_num = v;
      break;
    }

    case POINT: {
      gdouble v = timeline_clamp_pos(te, g_value_get_double(value));
      TL_DBG(te, "set_property(point): %.3f -> %.3f", te->point, v);
      te->point = v;
      break;
    }

    case LENGTH: {
      gdouble v = g_value_get_double(value);
      if (v < TIMELINE_MIN_FRAMES)
          v = TIMELINE_MIN_FRAMES;
      TL_DBG(te, "set_property(length): %.3f -> %.3f", te->num_video_frames, v);
      te->num_video_frames = v;
      timeline_sanitize_points(te);
      break;
    }

    case IN_POINT: {
      gint old_out = timeline_clamp_frame_i(te, (gint) llround(te->out));
      gint v = timeline_clamp_frame_i(te, (gint) llround(g_value_get_double(value)));
      if (te->has_selection && v > old_out)
          v = old_out;
      TL_DBG(te, "set_property(in): %.3f -> %d", te->in, v);
      te->in = (gdouble) v;
      timeline_clear_selection_ghost(te);
      break;
    }

    case OUT_POINT: {
      gint old_in = timeline_clamp_frame_i(te, (gint) llround(te->in));
      gint v = timeline_clamp_frame_i(te, (gint) llround(g_value_get_double(value)));
      if (te->has_selection && v < old_in)
          v = old_in;
      TL_DBG(te, "set_property(out): %.3f -> %d", te->out, v);
      te->out = (gdouble) v;
      timeline_clear_selection_ghost(te);
      break;
    }

    case SEL: {
      gboolean v = g_value_get_boolean(value);
      TL_DBG(te, "set_property(selection): %d -> %d", te->has_selection, v);
      if (v && timeline_current_empty_clear_range(te)) {
          te->has_selection = FALSE;
          timeline_clear_selection_ghost(te);
      }
      else {
          te->has_selection = v ? TRUE : FALSE;
          if (!te->has_selection)
              timeline_clear_selection_ghost(te);
      }
      break;
    }

    case BIND: {
      gboolean v = g_value_get_boolean(value);
      TL_DBG(te, "set_property(bind/scratch): %d -> %d", te->bind, v);
      te->bind = v;
      if (!te->bind) {
          te->action = action_none;
          te->drag_latched = FALSE;
          te->move_x = 0.0;
          timeline_clear_selection_ghost(te);
      }
      break;
    }

    case CLEARED: {
      gboolean v = g_value_get_boolean(value);
      TL_DBG(te, "set_property(clear): %d -> %d", te->clear, v);
      te->clear = v;
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, pspec);
      break;
  }

  gtk_widget_queue_draw(GTK_WIDGET(te));
}

static void get_property(GObject *object,
                         guint id,
                         GValue *value,
                         GParamSpec *pspec)
{
  TimelineSelection *te = TIMELINE_SELECTION(object);

  switch(id)
  {
    case MIN:       g_value_set_double(value, te->min); break;
    case MAX:       g_value_set_double(value, te->max); break;
    case POS:       g_value_set_double(value, te->frame_num); break;
    case POINT:     g_value_set_double(value, te->point); break;
    case LENGTH:    g_value_set_double(value, te->num_video_frames); break;
    case IN_POINT:  g_value_set_double(value, te->in); break;
    case OUT_POINT: g_value_set_double(value, te->out); break;
    case SEL:       g_value_set_boolean(value, te->has_selection); break;
    case BIND:      g_value_set_boolean(value, te->bind); break;
    case CLEARED:   g_value_set_boolean(value, te->clear); break;
    default:        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, pspec); break;
  }
}

static void finalize(GObject *object)
{
  parent_class->finalize(object);
}

static void timeline_class_init(TimelineSelectionClass *class)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);
  widget_class->draw = timeline_draw;

  GObjectClass *gobject_class = G_OBJECT_CLASS(class);
  parent_class = G_OBJECT_CLASS(g_type_class_peek(GTK_TYPE_DRAWING_AREA));
  gobject_class->finalize = finalize;
  gobject_class->get_property = get_property;
  gobject_class->set_property = set_property;

  g_object_class_install_property(gobject_class,
                                  MIN,
                                  g_param_spec_double("min",
                                                       "left",
                                                       "left",
                                                       0.0,
                                                       9999999.0,
                                                       0.0,
                                                       G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  MAX,
                                  g_param_spec_double("max",
                                                       "right",
                                                       "right",
                                                       0.0,
                                                       9999999.0,
                                                       1.0,
                                                       G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  POS,
                                  g_param_spec_double("pos",
                                                      "current position",
                                                      "current position",
                                                      0.0,
                                                      9999999.0,
                                                      0.0,
                                                      G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  POINT,
                                  g_param_spec_double("point",
                                                      "point at position",
                                                      "point at position",
                                                      0.0,
                                                      9999999.0,
                                                      0.0,
                                                      G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  LENGTH,
                                  g_param_spec_double("length",
                                                      "Length in frames",
                                                      "Length in frames",
                                                      1.0,
                                                      9999999.0,
                                                      1.0,
                                                      G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  IN_POINT,
                                  g_param_spec_double("in",
                                                      "In point",
                                                      "Inclusive in point as frame index",
                                                      0.0,
                                                      9999999.0,
                                                      0.0,
                                                      G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  OUT_POINT,
                                  g_param_spec_double("out",
                                                      "Out point",
                                                      "Inclusive out point as frame index",
                                                      0.0,
                                                      9999999.0,
                                                      0.0,
                                                      G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  SEL,
                                  g_param_spec_boolean("selection",
                                                       "Marker",
                                                       "Marker",
                                                       FALSE,
                                                       G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  BIND,
                                  g_param_spec_boolean("bind",
                                                       "Bind marker",
                                                       "Bind In/Out points",
                                                       FALSE,
                                                       G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  CLEARED,
                                  g_param_spec_boolean("clear",
                                                       "Clear marker",
                                                       "Clear in/out points",
                                                       FALSE,
                                                       G_PARAM_READWRITE));

  timeline_signals[SELECTION_CHANGED_SIGNAL] = g_signal_new("selection_changed",
                                                            G_TYPE_FROM_CLASS(gobject_class),
                                                            G_SIGNAL_RUN_FIRST,
                                                            0,
                                                            NULL,
                                                            NULL,
                                                            g_cclosure_marshal_VOID__VOID,
                                                            G_TYPE_NONE,
                                                            0,
                                                            NULL);

  timeline_signals[POS_CHANGED] = g_signal_new("pos_changed",
                                               G_TYPE_FROM_CLASS(gobject_class),
                                               G_SIGNAL_RUN_LAST,
                                               G_STRUCT_OFFSET(TimelineSelectionClass, pos_changed),
                                               NULL,
                                               NULL,
                                               g_cclosure_marshal_VOID__VOID,
                                               G_TYPE_NONE,
                                               0,
                                               NULL);

  timeline_signals[POINT_CHANGED] = g_signal_new("point_changed",
                                                 G_TYPE_FROM_CLASS(gobject_class),
                                                 G_SIGNAL_RUN_LAST,
                                                 G_STRUCT_OFFSET(TimelineSelectionClass, point_changed),
                                                 NULL,
                                                 NULL,
                                                 g_cclosure_marshal_VOID__VOID,
                                                 G_TYPE_NONE,
                                                 0,
                                                 NULL);

  timeline_signals[IN_CHANGED] = g_signal_new("in_point_changed",
                                              G_TYPE_FROM_CLASS(gobject_class),
                                              G_SIGNAL_RUN_LAST,
                                              G_STRUCT_OFFSET(TimelineSelectionClass, in_point_changed),
                                              NULL,
                                              NULL,
                                              g_cclosure_marshal_VOID__VOID,
                                              G_TYPE_NONE,
                                              0,
                                              NULL);

  timeline_signals[OUT_CHANGED] = g_signal_new("out_point_changed",
                                               G_TYPE_FROM_CLASS(gobject_class),
                                               G_SIGNAL_RUN_LAST,
                                               G_STRUCT_OFFSET(TimelineSelectionClass, out_point_changed),
                                               NULL,
                                               NULL,
                                               g_cclosure_marshal_VOID__VOID,
                                               G_TYPE_NONE,
                                               0,
                                               NULL);

  timeline_signals[CLEAR_CHANGED] = g_signal_new("cleared",
                                                 G_TYPE_FROM_CLASS(gobject_class),
                                                 G_SIGNAL_RUN_LAST,
                                                 G_STRUCT_OFFSET(TimelineSelectionClass, cleared),
                                                 NULL,
                                                 NULL,
                                                 g_cclosure_marshal_VOID__VOID,
                                                 G_TYPE_NONE,
                                                 0,
                                                 NULL);

  timeline_signals[BIND_CHANGED] = g_signal_new("bind_toggled",
                                                G_TYPE_FROM_CLASS(gobject_class),
                                                G_SIGNAL_RUN_LAST,
                                                G_STRUCT_OFFSET(TimelineSelectionClass, bind_toggled),
                                                NULL,
                                                NULL,
                                                g_cclosure_marshal_VOID__VOID,
                                                G_TYPE_NONE,
                                                0,
                                                NULL);
}

static void timeline_init(TimelineSelection *te)
{
  te->min = 0.0;
  te->max = 0.0;
  te->value = 0.0;
  te->frame_num = 0.0;
  te->num_video_frames = 1.0;
  te->in = 0.0;
  te->out = 0.0;
  te->bind = FALSE;
  te->grab_button = 0;
  te->action = action_none;
  te->grab_location = MOUSE_OUTSIDE;
  te->current_location = MOUSE_OUTSIDE;
  te->stepper.x = 0;
  te->stepper.y = 0;
  te->stepper.width = 0;
  te->stepper.height = 0;
  te->selection.x = 0;
  te->selection.y = 0;
  te->selection.width = 0;
  te->selection.height = 0;
  te->has_stepper = TRUE;
  te->clear = TRUE;
  te->stepper_size = 24;
  te->stepper_draw_size = 12;
  te->stepper_length = 0;
  te->step_size = 1;
  te->frame_width = 0.0;
  te->frame_height = 8;
  te->font_line = 24;
  te->has_selection = FALSE;
  te->move_x = 0.0;
  te->point = 0.0;
  te->drag_latched = FALSE;
  te->scratch_span = 0;
  te->has_ghost_selection = FALSE;
  te->ghost_in = 0.0;
  te->ghost_out = 0.0;
}

GType timeline_get_type(void)
{
  static GType gtype = 0;

  if(!gtype)
  {
    static const GTypeInfo ginfo = {
      sizeof(TimelineSelectionClass),
      NULL,
      NULL,
      (GClassInitFunc) timeline_class_init,
      NULL,
      NULL,
      sizeof(TimelineSelection),
      0,
      (GInstanceInitFunc) timeline_init,
      NULL
    };

    gtype = g_type_register_static(GTK_TYPE_DRAWING_AREA, "Timeline", &ginfo, 0);
  }

  return gtype;
}

gdouble timeline_get_in_point(TimelineSelection *te)
{
  gdouble result = 0.0;
  g_object_get(G_OBJECT(te), "in", &result, NULL);
  return result;
}

gdouble timeline_get_out_point(TimelineSelection *te)
{
  gdouble result = 0.0;
  g_object_get(G_OBJECT(te), "out", &result, NULL);
  return result;
}

gboolean timeline_get_selection(TimelineSelection *te)
{
  gboolean result = FALSE;
  g_object_get(G_OBJECT(te), "selection", &result, NULL);
  return result;
}

gboolean timeline_get_bind(TimelineSelection *te)
{
  gboolean result = FALSE;
  g_object_get(G_OBJECT(te), "bind", &result, NULL);
  return result;
}

void timeline_set_bind(GtkWidget *widget, gboolean active)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  TL_EVT(te, "set_bind", "request=%d old=%d action=%s drag_latched=%d in=%.3f out=%.3f",
         active, te->bind, timeline_action_name(te->action), te->drag_latched, te->in, te->out);

  te->bind = active ? TRUE : FALSE;

  if (!te->bind) {
      te->action = action_none;
      te->current_location = MOUSE_WIDGET;
      te->grab_button = 0;
      te->move_x = 0.0;
      te->drag_latched = FALSE;
      te->scratch_span = 0;
      timeline_clear_selection_ghost(te);
      timeline_update_cursor(widget, action_none);
  }

  TL_EVT(te, "emit", "bind_toggled");
  g_signal_emit(te->widget, timeline_signals[BIND_CHANGED], 0);
  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

void timeline_set_out_point(GtkWidget *widget, gdouble pos)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  gint old_in = timeline_clamp_frame_i(te, (gint) llround(te->in));
  gint old_out = timeline_clamp_frame_i(te, (gint) llround(te->out));
  gint new_out = timeline_clamp_frame_i(te, (gint) llround(pos));

  if (te->bind && te->action == action_atomic) {
      TL_EVT(te, "set_out",
             "ignored while scratch is active: raw=%.3f old=(%d,%d) requested=%d",
             raw, old_in, old_out, new_out);
      return;
  }

  if (te->has_selection && new_out < old_in) {
      TL_EVT(te, "set_out",
             "ignored crossing OUT: raw=%.3f old=(%d,%d) requested=%d",
             raw, old_in, old_out, new_out);
      return;
  }

  if (!te->has_selection && new_out < old_in)
      old_in = new_out;

  if (new_out == old_out && te->has_selection)
      return;

  te->in = (gdouble) old_in;
  te->out = (gdouble) new_out;
  te->has_selection = TRUE;
  te->clear = FALSE;
  timeline_clear_selection_ghost(te);

  TL_EVT(te, "set_out", "raw=%.3f old=(%d,%d) new=(%d,%d) bind=%d action=%s",
         raw, old_in, old_out, old_in, new_out, te->bind, timeline_action_name(te->action));

  TL_EVT(te, "emit", "out_point_changed");
  g_signal_emit(te->widget, timeline_signals[OUT_CHANGED], 0);

  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

void timeline_clear_points(GtkWidget *widget)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  TL_EVT(te, "clear", "old in=%.3f out=%.3f bind=%d action=%s drag_latched=%d",
         te->in, te->out, te->bind, timeline_action_name(te->action), te->drag_latched);

  te->bind = FALSE;
  te->clear = TRUE;
  te->has_selection = FALSE;
  te->in = 0.0;
  te->out = 0.0;
  te->drag_latched = FALSE;
  te->scratch_span = 0;
  timeline_clear_selection_ghost(te);
  te->action = action_none;
  te->move_x = 0.0;
  te->grab_button = 0;
  te->current_location = MOUSE_WIDGET;

  te->selection.x = 0;
  te->selection.y = 0;
  te->selection.width = 0;
  te->selection.height = 0;

  timeline_update_cursor(widget, action_none);

  TL_EVT(te, "emit", "cleared");
  g_signal_emit(te->widget, timeline_signals[CLEAR_CHANGED], 0);
  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

void timeline_set_in_point(GtkWidget *widget, gdouble pos)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  gint old_in = timeline_clamp_frame_i(te, (gint) llround(te->in));
  gint old_out = timeline_clamp_frame_i(te, (gint) llround(te->out));
  gint new_in = timeline_clamp_frame_i(te, (gint) llround(pos));
  gboolean initialized = FALSE;

  if (te->bind && te->action == action_atomic) {
      TL_EVT(te, "set_in",
             "ignored while scratch is active: raw=%.3f old=(%d,%d) requested=%d",
             raw, old_in, old_out, new_in);
      return;
  }

  if (!te->has_selection) {
      old_out = new_in;
      initialized = TRUE;
  }
  else if (new_in > old_out) {
      TL_EVT(te, "set_in",
             "ignored crossing IN: raw=%.3f old=(%d,%d) requested=%d",
             raw, old_in, old_out, new_in);
      return;
  }

  if (!initialized && new_in == old_in)
      return;

  te->in = (gdouble) new_in;
  te->out = (gdouble) old_out;
  te->has_selection = TRUE;
  te->clear = FALSE;
  timeline_clear_selection_ghost(te);

  TL_EVT(te, "set_in", "raw=%.3f old=(%d,%d) new=(%d,%d) bind=%d action=%s initialized=%d",
         raw, old_in, old_out, new_in, old_out, te->bind, timeline_action_name(te->action), initialized);

  TL_EVT(te, "emit", "%s", initialized ? "in_point_changed + out_point_changed" : "in_point_changed");
  g_signal_emit(te->widget, timeline_signals[IN_CHANGED], 0);

  if (initialized)
      g_signal_emit(te->widget, timeline_signals[OUT_CHANGED], 0);

  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

void timeline_set_in_and_out_point(GtkWidget *widget, gdouble start, gdouble end)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  start = timeline_clamp_frame(te, start);
  end = timeline_clamp_frame(te, end);

  if (start > end) {
      gdouble tmp = start;
      start = end;
      end = tmp;
  }

  if (te->bind && te->action == action_atomic) {
      TL_EVT(te, "set_in_and_out",
             "ignored while scratch is active: raw=(%.3f,%.3f) snapped=(%.3f,%.3f) old=(%.3f,%.3f) fixed_span=%d",
             raw_start, raw_end, start, end, old_in, old_out, te->scratch_span);
      gtk_widget_queue_draw(GTK_WIDGET(te->widget));
      return;
  }

  if (timeline_is_clear_echo(te, start, end)) {
      te->in = 0.0;
      te->out = 0.0;
      te->has_selection = FALSE;
      timeline_clear_selection_ghost(te);

      TL_EVT(te, "set_in_and_out", "suppressed clear echo raw=(%.3f,%.3f) snapped=(%.3f,%.3f) old=(%.3f,%.3f)",
             raw_start, raw_end, start, end, old_in, old_out);

      gtk_widget_queue_draw(GTK_WIDGET(te->widget));
      return;
  }

  te->in = start;
  te->out = end;
  te->has_selection = TRUE;
  te->clear = FALSE;
  timeline_clear_selection_ghost(te);

  TL_EVT(te, "set_in_and_out", "raw=(%.3f,%.3f) snapped=(%.3f,%.3f) old=(%.3f,%.3f) bind=%d action=%s span=%.3f",
         raw_start, raw_end, start, end, old_in, old_out, te->bind, timeline_action_name(te->action), end - start + 1.0);

  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

void timeline_set_selection(GtkWidget *widget, gboolean active)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  if (te->bind && te->action == action_atomic) {
      TL_EVT(te, "set_selection", "ignored while scratch is active: request=%d old=%d in=%.3f out=%.3f fixed_span=%d",
             active, old, te->in, te->out, te->scratch_span);
      gtk_widget_queue_draw(GTK_WIDGET(te->widget));
      return;
  }

  if (active && timeline_current_empty_clear_range(te)) {
      te->has_selection = FALSE;
      timeline_clear_selection_ghost(te);
      TL_EVT(te, "set_selection", "request=%d old=%d suppressed clear echo", active, old);
  }
  else {
      te->has_selection = active ? TRUE : FALSE;
      if (!active) {
          te->clear = TRUE;
          timeline_clear_selection_ghost(te);
      }
      else
          te->clear = FALSE;
      TL_EVT(te, "set_selection", "request=%d old=%d new=%d in=%.3f out=%.3f",
             active, old, te->has_selection, te->in, te->out);
  }

  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

void timeline_set_length(GtkWidget *widget, gdouble length, gdouble pos)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  gint old_frames = timeline_frame_count_i(te);
  gint new_frames;

  if (length < TIMELINE_MIN_FRAMES)
      length = TIMELINE_MIN_FRAMES;

  new_frames = MAX(1, (gint) llround(length));
  length = (gdouble) new_frames;

  if (pos < 0.0)
      pos = 0.0;

  //TL_EVT(te, "set_length", "length %.3f -> %.3f pos=%.3f", te->num_video_frames, length, pos);

  if (old_frames != new_frames &&
      (te->bind || te->drag_latched || te->grab_button || te->action != action_none))
  {
      timeline_cancel_drag_mode(widget);
  }

  te->num_video_frames = length;
  if (old_frames != new_frames)
      timeline_clear_selection_ghost(te);
  timeline_sanitize_points(te);
  timeline_set_pos(GTK_WIDGET(te->widget), pos);
}

void timeline_set_pos(GtkWidget *widget, gdouble pos)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  pos = timeline_clamp_pos(te, pos);

  te->frame_num = pos;

  //TL_EVT(te, "set_pos", "raw=%.3f clamped=%.3f old=%.3f nframes=%.3f", raw, pos, old, nframes);
  //TL_EVT(te, "emit", "pos_changed");
  g_signal_emit(te->widget, timeline_signals[POS_CHANGED], 0);
  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

gdouble timeline_get_pos(TimelineSelection *te)
{
  gdouble result = 0.0;
  g_object_get(G_OBJECT(te), "pos", &result, NULL);
  return result;
}

void timeline_set_point(GtkWidget *widget, gdouble point)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  point = timeline_clamp_pos(te, point);

  if (te->point == point)
      return;

  te->point = point;
  g_signal_emit(te->widget, timeline_signals[POINT_CHANGED], 0);
  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

gdouble timeline_get_point(TimelineSelection *te)
{
  gdouble result = 0.0;
  g_object_get(G_OBJECT(te), "point", &result, NULL);
  return result;
}

gdouble timeline_get_length(TimelineSelection *te)
{
  gdouble result = 0.0;
  g_object_get(G_OBJECT(te), "length", &result, NULL);
  return result;
}

static void timeline_clear_selection_ghost(TimelineSelection *te)
{
    te->has_ghost_selection = FALSE;
    te->ghost_in = 0.0;
    te->ghost_out = 0.0;
}

static void timeline_capture_selection_ghost(TimelineSelection *te)
{
    if (!te->has_selection || te->out < te->in) {
        timeline_clear_selection_ghost(te);
        return;
    }

    te->ghost_in = timeline_clamp_frame(te, te->in);
    te->ghost_out = timeline_clamp_frame(te, te->out);

    if (te->ghost_out < te->ghost_in) {
        timeline_clear_selection_ghost(te);
        return;
    }

    te->has_ghost_selection = TRUE;
}

static void move_selection(GtkWidget *widget, gdouble x, gdouble width)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  gint old_in = timeline_clamp_frame_i(te, (gint) llround(te->in));
  gint old_out = timeline_clamp_frame_i(te, (gint) llround(te->out));
  gint current_span = old_out - old_in + 1;
  gint span = te->scratch_span > 0 ? te->scratch_span : current_span;
  gint nframes = timeline_frame_count_i(te);

  if (!te->has_selection || span <= 0 || width <= 1.0) {
      TL_EVT(te, "move_selection", "ignored: has_selection=%d span=%d width=%.3f in=%.3f out=%.3f",
             te->has_selection, span, width, te->in, te->out);
      return;
  }

  gdouble mouse_frame = timeline_x_to_frame_cont(te, x, width);
  gint new_in = (gint) llround(mouse_frame - te->move_x);
  gint new_out = new_in + span - 1;

  if (new_in < 0) {
      new_in = 0;
      new_out = span - 1;
  }
  else if (new_out >= nframes) {
      new_out = nframes - 1;
      new_in = new_out - span + 1;
  }

  new_in = timeline_clamp_frame_i(te, new_in);
  new_out = timeline_clamp_frame_i(te, new_out);

  if (new_in == old_in && new_out == old_out)
      return;

  te->in = (gdouble) new_in;
  te->out = (gdouble) new_out;
  te->clear = FALSE;

  TL_EVT(te, "move_selection",
        "x=%.2f width=%.2f move_x=%.3f old=(%d,%d) new=(%d,%d) fixed_span=%d current_span=%d no_edge_signals=1",
        x, width, te->move_x,
        old_in, old_out,
        new_in, new_out,
        span, current_span);

  gtk_widget_queue_draw(widget);

  TL_EVT(te, "emit", "selection_changed");
  g_signal_emit(te->widget, timeline_signals[SELECTION_CHANGED_SIGNAL], 0);
}

static void timeline_cancel_drag_mode(GtkWidget *widget)
{
    TimelineSelection *te = TIMELINE_SELECTION(widget);

    if (!te->bind && !te->drag_latched && te->action == action_none)
        return;

    TL_EVT(te, "cancel_mode", "old bind=%d action=%s drag_latched=%d grab_button=%d move_x=%.3f",
           te->bind, timeline_action_name(te->action), te->drag_latched, te->grab_button, te->move_x);

    te->bind = FALSE;
    te->action = action_none;
    te->current_location = MOUSE_WIDGET;
    te->grab_button = 0;
    te->move_x = 0.0;
    te->drag_latched = FALSE;
    te->scratch_span = 0;
    timeline_clear_selection_ghost(te);

    timeline_update_cursor(widget, action_none);
    gtk_widget_queue_draw(widget);
}

static void timeline_apply_action_at_x(GtkWidget *widget,
                                       TimelineAction action,
                                       gdouble x,
                                       gdouble width)
{
    TimelineSelection *te = TIMELINE_SELECTION(widget);

    TL_EVT(te, "apply_action", "action=%s x=%.2f width=%.2f bind=%d drag_latched=%d",
           timeline_action_name(action), x, width, te->bind, te->drag_latched);

    switch (action) {
        case action_pos:
            timeline_set_pos(widget, timeline_x_to_pos(te, x, width));
            break;

        case action_atomic:
            if (te->has_selection && te->bind)
                move_selection(widget, x, width);
            else
                TL_EVT(te, "apply_action", "scratch ignored: has_selection=%d bind=%d", te->has_selection, te->bind);
            break;

        case action_point:
            timeline_set_point(widget, timeline_x_to_pos(te, x, width));
            break;

        default:
            TL_EVT(te, "apply_action", "ignored action=%s", timeline_action_name(action));
            break;
    }
}

static gboolean event_scroll(GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  GdkScrollDirection direction;

  (void) user_data;

  TL_EVT(te, "scroll", "bind=%d action=%s drag_latched=%d", te->bind, timeline_action_name(te->action), te->drag_latched);

  if (te->bind && te->action == action_atomic) {
      TL_EVT(te, "scroll", "ignored while scratch is active; middle click toggles scratch off");
      return TRUE;
  }

  if (te->drag_latched || te->bind)
      timeline_cancel_drag_mode(widget);

  if (gdk_event_get_scroll_direction((GdkEvent*)ev, &direction)) {
    gdouble cur_pos = timeline_get_pos(te);

    if (direction == GDK_SCROLL_UP)
      timeline_set_pos(widget, cur_pos + 1.0);
    else if (direction == GDK_SCROLL_DOWN)
      timeline_set_pos(widget, cur_pos - 1.0);
  }

  return TRUE;
}

static gboolean event_press(GtkWidget *widget, GdkEventButton *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  GtkAllocation all;

  (void) user_data;

  gtk_widget_get_allocation(widget, &all);

  gdouble width = (gdouble) all.width;

  gtk_widget_grab_focus(widget);

  TL_EVT(te, "press", "button=%u type=%d x=%.2f y=%.2f width=%.2f in=%.3f out=%.3f bind=%d action=%s drag_latched=%d selection_rect=(%d,%d %dx%d) stepper_rect=(%d,%d %dx%d)",
         ev->button, ev->type, ev->x, ev->y, width,
         te->in, te->out, te->bind, timeline_action_name(te->action), te->drag_latched,
         te->selection.x, te->selection.y, te->selection.width, te->selection.height,
         te->stepper.x, te->stepper.y, te->stepper.width, te->stepper.height);

  if (width <= 1.0) {
      TL_EVT(te, "press", "ignored: width <= 1");
      return TRUE;
  }

  if (te->bind && te->action == action_atomic && ev->button != 2) {
    TL_EVT(te, "press", "ignored button=%u while scratch is active; middle click toggles scratch off", ev->button);
    return TRUE;
  }

  if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1) {
    TL_EVT(te, "press", "double-left -> clear selection");
    timeline_clear_points(widget);
    return TRUE;
  }

  if (ev->button == 1) {
    gboolean in_triangle = timeline_point_in_stepper_triangle(te, ev->x, ev->y, width);

    TL_EVT(te, "press-left", "triangle_hit=%d", in_triangle);

    if (in_triangle) {
        if (te->bind || te->drag_latched)
            timeline_cancel_drag_mode(widget);

        te->grab_button = 1;
        te->current_location = MOUSE_STEPPER;
        te->action = action_pos;
        te->drag_latched = FALSE;
        te->move_x = 0.0;
        timeline_apply_action_at_x(widget, action_pos, ev->x, width);
        return TRUE;
    }

    if (te->bind || te->drag_latched)
        timeline_cancel_drag_mode(widget);

    te->grab_button = 1;
    te->current_location = MOUSE_WIDGET;
    te->action = action_in_point;
    te->drag_latched = FALSE;
    te->move_x = 0.0;

    gdouble frame = timeline_x_to_marker_frame(te, ev->x, width);
    TL_EVT(te, "press-left", "set IN directly frame=%.3f", frame);
    timeline_set_in_point(widget, frame);
    return TRUE;
  }

  if (ev->button == 3) {
    te->bind = FALSE;
    te->drag_latched = FALSE;
    te->action = action_out_point;
    te->grab_button = 3;
    te->current_location = MOUSE_WIDGET;
    te->move_x = 0.0;

    timeline_update_cursor(widget, action_none);

    gdouble frame = timeline_x_to_marker_frame(te, ev->x, width);
    TL_EVT(te, "press-right", "set OUT directly frame=%.3f", frame);
    timeline_set_out_point(widget, frame);
    return TRUE;
  }

  if (ev->button == 2) {
    const gboolean selection_hit = timeline_point_in_selection_span(te, ev->x, width);

    if (te->bind && te->action == action_atomic) {
      TL_EVT(te, "press-middle", "toggle scratch/bind %d -> 0", te->bind);
      timeline_set_bind(widget, FALSE);
      return TRUE;
    }

    if (te->has_selection && selection_hit) {
      gint old_in = timeline_clamp_frame_i(te, (gint) llround(te->in));
      gint old_out = timeline_clamp_frame_i(te, (gint) llround(te->out));
      gint span = old_out - old_in + 1;

      if (span <= 0) {
          TL_EVT(te, "press-middle", "ignored invalid span=%d in=%d out=%d", span, old_in, old_out);
          return TRUE;
      }

      TL_EVT(te, "press-middle", "inside selection span -> toggle scratch/bind %d -> 1", te->bind);

      timeline_set_bind(widget, TRUE);
      timeline_capture_selection_ghost(te);

      gdouble mouse_frame = timeline_x_to_frame_cont(te, ev->x, width);
      te->move_x = mouse_frame - (gdouble) old_in;
      te->scratch_span = span;
      te->action = action_atomic;
      te->current_location = MOUSE_SELECTION;
      te->grab_button = 2;
      te->drag_latched = TRUE;
      timeline_update_cursor(widget, action_atomic);
      TL_EVT(te, "scratch", "activated mouse_frame=%.3f move_x=%.3f in=%d out=%d fixed_span=%d",
             mouse_frame, te->move_x, old_in, old_out, te->scratch_span);

      gtk_widget_queue_draw(widget);
      return TRUE;
    }

    TL_EVT(te, "press-middle", "ignored: has_selection=%d selection_hit=%d in=%.3f out=%.3f",
           te->has_selection, selection_hit, te->in, te->out);
    return TRUE;
  }

  TL_EVT(te, "press", "ignored button=%u", ev->button);
  return TRUE;
}

static gboolean event_release(GtkWidget *widget, GdkEventButton *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  (void) user_data;

  TL_EVT(te, "release", "button=%u action=%s bind=%d drag_latched=%d grab_button=%d",
         ev->button, timeline_action_name(te->action), te->bind, te->drag_latched, te->grab_button);

  /*
   * Middle-button marker movement is intentionally latched.
   *
   * The first middle click inside a selection enters scratch mode.
   * The user may then release the middle button and move the mouse freely.
   * A second middle click toggles scratch mode off in event_press().
   *
   * Backend updates are produced while the marker moves, via
   * move_selection() -> on_timeline_move_selection().
   * Release must therefore NOT commit and must NOT disable bind.
   */
  if (te->bind && te->drag_latched && te->action == action_atomic) {
      te->grab_button = 0;
      timeline_update_cursor(widget, action_atomic);
      gtk_widget_queue_draw(widget);
      return TRUE;
  }

  te->action = action_none;
  te->current_location = MOUSE_WIDGET;
  te->grab_button = 0;
  te->move_x = 0.0;
  te->drag_latched = FALSE;
  te->scratch_span = 0;

  timeline_update_cursor(widget, action_none);
  gtk_widget_queue_draw(widget);

  return TRUE;
}

static gboolean event_motion(GtkWidget *widget, GdkEventMotion *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  GtkAllocation all;

  (void) user_data;

  gtk_widget_get_allocation(widget, &all);

  gdouble width = (gdouble) all.width;

  if (width <= 1.0)
      return TRUE;

  gboolean b1 = (ev->state & GDK_BUTTON1_MASK) != 0;
  gboolean b2 = (ev->state & GDK_BUTTON2_MASK) != 0;
  gboolean b3 = (ev->state & GDK_BUTTON3_MASK) != 0;

  /*
   * Latched scratch mode:
   * - If bind/drag_latched/action_atomic is active, mouse motion moves the
   *   marker even when the middle button is no longer physically held.
   * - The b2 path is kept for the press-drag case before release has arrived.
   */
  if ((te->bind && te->drag_latched && te->action == action_atomic) ||
      (b2 && te->action == action_atomic && te->drag_latched))
  {
    timeline_apply_action_at_x(widget, action_atomic, ev->x, width);
    return TRUE;
  }

  if (b1 && te->action == action_pos) {
    timeline_apply_action_at_x(widget, action_pos, ev->x, width);
    return TRUE;
  }

  if (!b1 && !b2 && !b3) {
    TimelineAction hover = timeline_pick_action(te, ev->x, ev->y, width);

    te->current_location = MOUSE_WIDGET;
    timeline_update_cursor(widget, hover);

    timeline_set_point(widget, timeline_x_to_pos(te, ev->x, width));

    return TRUE;
  }

  return TRUE;
}

static gboolean event_leave(GtkWidget *widget, GdkEventCrossing *ev, gpointer user_data)
{
    TimelineSelection *te = TIMELINE_SELECTION(widget);
    (void) ev;
    (void) user_data;

    /*
     * Hide the passive current-frame hover label when the pointer leaves
     * the widget. Do not cancel latched scratch mode here; middle click
     * still toggles that mode off explicitly.
     */
    te->current_location = MOUSE_OUTSIDE;

    if (!timeline_latch_visible(te))
        timeline_update_cursor(widget, action_none);

    gtk_widget_queue_draw(widget);
    return FALSE;
}

static gboolean event_focus_out(GtkWidget *widget, GdkEventFocus *ev, gpointer user_data)
{
    TimelineSelection *te = TIMELINE_SELECTION(widget);
    (void) ev;
    (void) user_data;

    if (te->bind && te->action == action_atomic) {
        TL_EVT(te, "focus-out", "kept scratch active; middle click toggles scratch off");
        timeline_update_cursor(widget, action_atomic);
        gtk_widget_queue_draw(widget);
        return FALSE;
    }

    TL_EVT(te, "focus-out", "cancel transient action state");
    timeline_cancel_drag_mode(widget);
    return FALSE;
}

void cairo_rectangle_round(cairo_t *cr, double x0, double y0, double width, double height, double radius)
{
    if (width <= 0.001 || height <= 0.001) {
        return;
    }

    double x1 = x0 + width;
    double y1 = y0 + height;

    radius = fmin(fmin(radius, width / 2), height / 2);

    cairo_new_sub_path(cr);
    cairo_arc(cr, x1 - radius, y0 + radius, radius, -M_PI_2, 0);
    cairo_arc(cr, x1 - radius, y1 - radius, radius, 0, M_PI_2);
    cairo_arc(cr, x0 + radius, y1 - radius, radius, M_PI_2, M_PI);
    cairo_arc(cr, x0 + radius, y0 + radius, radius, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);

    cairo_fill(cr);
}

static void cairo_rectangle_round_stroke(cairo_t *cr,
                                         double x0,
                                         double y0,
                                         double width,
                                         double height,
                                         double radius)
{
    if (width <= 0.001 || height <= 0.001)
        return;

    double x1 = x0 + width;
    double y1 = y0 + height;

    radius = fmin(fmin(radius, width / 2), height / 2);

    cairo_new_sub_path(cr);
    cairo_arc(cr, x1 - radius, y0 + radius, radius, -M_PI_2, 0);
    cairo_arc(cr, x1 - radius, y1 - radius, radius, 0, M_PI_2);
    cairo_arc(cr, x0 + radius, y1 - radius, radius, M_PI_2, M_PI);
    cairo_arc(cr, x0 + radius, y0 + radius, radius, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);

    cairo_stroke(cr);
}

static gdouble timeline_text_width(cairo_t *cr, const gchar *text)
{
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    return ext.width;
}

static void timeline_draw_text_pill(cairo_t *cr,
                                    const gchar *text,
                                    gdouble center_x,
                                    gdouble baseline_y,
                                    gdouble width,
                                    const GdkRGBA *fg,
                                    gdouble fg_alpha,
                                    const GdkRGBA *bg,
                                    gdouble bg_alpha)
{
    cairo_text_extents_t ext;
    gdouble pad_x = 3.0;
    gdouble pad_y = 1.0;
    gdouble text_x;
    gdouble min_text_x;
    gdouble max_text_x;
    gdouble box_x;
    gdouble box_y;
    gdouble box_w;
    gdouble box_h;

    cairo_text_extents(cr, text, &ext);

    text_x = center_x - (ext.width * 0.5) - ext.x_bearing;
    min_text_x = 2.0 - ext.x_bearing;
    max_text_x = width - 2.0 - ext.x_bearing - ext.width;

    if (max_text_x < min_text_x)
        max_text_x = min_text_x;

    text_x = CLAMP(text_x, min_text_x, max_text_x);

    box_x = floor(text_x + ext.x_bearing - pad_x);
    box_y = floor(baseline_y + ext.y_bearing - pad_y);
    box_w = ceil(ext.width + (pad_x * 2.0));
    box_h = ceil(ext.height + (pad_y * 2.0));

    cairo_set_source_rgba(cr, bg->red, bg->green, bg->blue, bg_alpha);
    cairo_rectangle_round(cr, box_x, box_y, box_w, box_h, 3.0);

    cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, fg_alpha);
    cairo_move_to(cr, floor(text_x), floor(baseline_y));
    cairo_show_text(cr, text);
}

static gboolean timeline_draw(GtkWidget *widget, cairo_t *cr)
{
  gchar text[64];
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  double width = gtk_widget_get_allocated_width(widget);
  double height = gtk_widget_get_allocated_height(widget);

  if (width <= 1.0 || height <= 1.0)
      return FALSE;

  gint nframes_i = timeline_frame_count_i(te);
  gdouble nframes = (gdouble) nframes_i;
  gdouble marker_width = width / nframes;
  gdouble marker_height = te->frame_height;

  te->frame_width = marker_width;

  GtkStyleContext *sc = gtk_widget_get_style_context(widget);

  GdkRGBA color;
  gtk_style_context_get_color(sc, gtk_style_context_get_state(sc), &color);

  GdkRGBA col2;
  vj_gtk_context_get_color(sc, "border-color", GTK_STATE_FLAG_NORMAL, &col2);

  GdkRGBA info_fg = { 0.06, 0.05, 0.03, 1.0 };
  GdkRGBA info_bg = { 1.00, 0.78, 0.10, 1.0 };

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_FAST);
  cairo_select_font_face(cr,
                         "Sans",
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, TIMELINE_LABEL_FONT_SIZE);

  gdouble tri_h = te->has_stepper ? te->stepper_draw_size : 0.0;
  gdouble track_h = marker_height;
  gdouble label_rows_h = 12.0;
  gdouble group_h = tri_h + track_h + label_rows_h;

  gdouble group_y = floor((height - group_h) * 0.5);
  if (group_y < 0.0)
      group_y = 0.0;

  gdouble tri_y = group_y;
  gdouble track_y = floor(group_y + tri_h);
  gdouble track_bottom = track_y + track_h;
  gdouble marker_label_y = track_bottom + 10.0;
  gdouble info_label_y = track_y + track_h - 1.0;
  gdouble handle_w = 3.0;

  if (marker_label_y > height - 2.0)
      marker_label_y = height - 2.0;

  if (track_bottom > height) {
      track_y = MAX(0.0, height - track_h);
      track_bottom = track_y + track_h;
      tri_y = MAX(0.0, track_y - tri_h);
      marker_label_y = MIN(height - 2.0, track_bottom + 9.0);
      info_label_y = track_y + track_h - 1.0;
  }

  cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.25);
  cairo_rectangle_round(cr, 0.0, track_y, width, track_h, 10.0);

  if (te->has_ghost_selection && te->ghost_out >= te->ghost_in) {
    gdouble ghost_in_f = timeline_clamp_frame(te, te->ghost_in);
    gdouble ghost_out_f = timeline_clamp_frame(te, te->ghost_out);
    gdouble ghost_in_x = timeline_snap_fill_x(timeline_frame_boundary_to_x(te, ghost_in_f, width), width);
    gdouble ghost_out_x = timeline_snap_fill_x(timeline_frame_boundary_to_x(te, ghost_out_f + 1.0, width), width);

    if (ghost_out_x < ghost_in_x) {
        gdouble tmp = ghost_in_x;
        ghost_in_x = ghost_out_x;
        ghost_out_x = tmp;
    }

    if (ghost_out_x <= ghost_in_x)
        ghost_out_x = MIN(width, ghost_in_x + 1.0);

    gdouble ghost_w = MAX(1.0, ghost_out_x - ghost_in_x);

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.22);
    cairo_rectangle_round(cr, ghost_in_x, track_y, ghost_w, track_h, 10.0);

    cairo_save(cr);
    double ghost_dash[] = { 2.0, 2.0 };
    cairo_set_dash(cr, ghost_dash, 2, 0.0);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.42);
    cairo_rectangle_round_stroke(cr,
                                 floor(ghost_in_x) + 0.5,
                                 floor(track_y) + 0.5,
                                 floor(ghost_w),
                                 floor(track_h),
                                 3.0);
    cairo_restore(cr);
  }

  te->selection.x = 0;
  te->selection.y = (gint) tri_y;
  te->selection.width = 0;
  te->selection.height = (gint) (marker_label_y - tri_y + 4.0);

  if (te->has_selection && te->out >= te->in) {
    gdouble in_f = timeline_clamp_frame(te, te->in);
    gdouble out_f = timeline_clamp_frame(te, te->out);
    gdouble in_x = timeline_snap_fill_x(timeline_frame_boundary_to_x(te, in_f, width), width);
    gdouble out_x = timeline_snap_fill_x(timeline_frame_boundary_to_x(te, out_f + 1.0, width), width);

    if (out_x < in_x) {
        gdouble tmp = in_x;
        in_x = out_x;
        out_x = tmp;
    }

    if (out_x <= in_x)
        out_x = MIN(width, in_x + 1.0);

    gdouble sel_w = MAX(1.0, out_x - in_x);

    cairo_set_source_rgba(cr, col2.red, col2.green, col2.blue, 0.90);
    cairo_rectangle_round(cr, in_x, track_y, sel_w, track_h, 10.0);

    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.90);
    cairo_rectangle_round(cr,
                          in_x - (handle_w * 0.5),
                          track_y,
                          handle_w,
                          track_h,
                          1.5);

    cairo_rectangle_round(cr,
                          out_x - (handle_w * 0.5),
                          track_y,
                          handle_w,
                          track_h,
                          1.5);

    {
      gint f1 = timeline_clamp_frame_i(te, (gint) llround(in_f));
      gint f2 = timeline_clamp_frame_i(te, (gint) llround(out_f));
      gchar in_text[32];
      gchar out_text[32];
      gchar both_text[64];
      gdouble in_tw;
      gdouble out_tw;
      gboolean split_labels;

      snprintf(in_text, sizeof(in_text), "%d", f1);
      snprintf(out_text, sizeof(out_text), "%d", f2);

      in_tw = timeline_text_width(cr, in_text);
      out_tw = timeline_text_width(cr, out_text);

      split_labels = (fabs(out_x - in_x) >
                      ((in_tw + out_tw) * 0.5 + 10.0));

      if (split_labels) {
          timeline_draw_text_pill(cr,
                                  in_text,
                                  in_x,
                                  marker_label_y,
                                  width,
                                  &color,
                                  0.82,
                                  &col2,
                                  0.25);

          timeline_draw_text_pill(cr,
                                  out_text,
                                  out_x,
                                  marker_label_y,
                                  width,
                                  &color,
                                  0.82,
                                  &col2,
                                  0.25);
      }
      else {
          snprintf(both_text, sizeof(both_text), "%d - %d", f1, f2);
          timeline_draw_text_pill(cr,
                                  both_text,
                                  in_x + (sel_w * 0.5),
                                  marker_label_y,
                                  width,
                                  &color,
                                  0.82,
                                  &col2,
                                  0.25);
      }
    }

    /*
     * Active scratch/move mode is indicated by the dashed outline below.
     * Do not draw an unlabeled center dot here; it looks like a stray
     * marker and has no independent interaction meaning.
     */

    te->selection.x = (gint) in_x;
    te->selection.y = (gint) tri_y;
    te->selection.width = (gint) sel_w;
    te->selection.height = (gint) (marker_label_y - tri_y + 4.0);

    if (timeline_latch_visible(te)) {
        gdouble box_x = in_x;
        gdouble box_w = sel_w;
        gdouble box_y = tri_y;
        gdouble box_h = (track_bottom - tri_y) + 4.0;

        if (box_w < 6.0) {
            box_x -= 3.0;
            box_w = 6.0;
        }

        if (box_x < 1.0)
            box_x = 1.0;
        if ((box_x + box_w) > (width - 1.0))
            box_w = MAX(1.0, (width - 1.0) - box_x);

        cairo_save(cr);

        double dash[] = { 4.0, 3.0 };
        cairo_set_dash(cr, dash, 2, 0.0);
        cairo_set_line_width(cr, 2.0);
        cairo_set_source_rgba(cr, 1.0, 0.78, 0.08, 0.98);
        cairo_rectangle_round_stroke(cr,
                                     floor(box_x) + 0.5,
                                     floor(box_y) + 0.5,
                                     floor(box_w),
                                     floor(box_h),
                                     3.0);

        cairo_restore(cr);
    }
  }

  if (te->has_ghost_selection && te->ghost_out >= te->ghost_in) {
    gdouble ghost_in_f = timeline_clamp_frame(te, te->ghost_in);
    gdouble ghost_out_f = timeline_clamp_frame(te, te->ghost_out);
    gdouble ghost_in_x = timeline_snap_line_x(timeline_frame_boundary_to_x(te, ghost_in_f, width), width);
    gdouble ghost_out_x = timeline_snap_line_x(timeline_frame_boundary_to_x(te, ghost_out_f + 1.0, width), width);

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.46);
    cairo_move_to(cr, ghost_in_x, track_y);
    cairo_line_to(cr, ghost_in_x, track_bottom);
    cairo_move_to(cr, ghost_out_x, track_y);
    cairo_line_to(cr, ghost_out_x, track_bottom);
    cairo_stroke(cr);
  }

  /*
   * The hover/current-frame pill is useful in normal mode, but while
   * scratch/move is latched the mouse is controlling the marker block.
   * Keeping the old hover point visible then looks like a ghost artifact.
   */
  if (te->current_location != MOUSE_OUTSIDE &&
      !(te->grab_button == 1 && te->current_location == MOUSE_STEPPER) &&
      !timeline_latch_visible(te))
  {
    gdouble point_x = timeline_snap_line_x(timeline_frame_to_x(te, te->point, width), width);

    cairo_set_font_size(cr, TIMELINE_INFO_FONT_SIZE);
    snprintf(text, sizeof(text), "%d", (gint) llround(te->point));
    timeline_draw_text_pill(cr,
                            text,
                            point_x,
                            info_label_y,
                            width,
                            &info_fg,
                            0.95,
                            &info_bg,
                            0.72);
    cairo_set_font_size(cr, TIMELINE_LABEL_FONT_SIZE);
  }

  if (te->has_stepper) {
    gdouble pos_x = timeline_snap_line_x(timeline_frame_to_x(te, te->frame_num, width), width);

    te->stepper.x = (gint) (pos_x - (TIMELINE_STEPPER_HIT_PX * 0.5));
    te->stepper.y = (gint) tri_y;
    te->stepper.width = (gint) TIMELINE_STEPPER_HIT_PX;
    te->stepper.height = (gint) ((track_bottom - tri_y) + 4.0);

    cairo_set_source_rgba(cr,
                          col2.red * 0.45,
                          col2.green * 0.45,
                          col2.blue * 0.45,
                          1.0);

    cairo_move_to(cr, pos_x - te->stepper_draw_size, tri_y);
    cairo_line_to(cr, pos_x, tri_y + tri_h);
    cairo_line_to(cr, pos_x + te->stepper_draw_size, tri_y);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_source_rgba(cr,
                          col2.red * 0.45,
                          col2.green * 0.45,
                          col2.blue * 0.45,
                          0.95);

    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, pos_x, tri_y + tri_h);
    cairo_line_to(cr, pos_x, track_bottom);
    cairo_stroke(cr);

    if (te->grab_button == 1 && te->current_location == MOUSE_STEPPER) {
      cairo_set_font_size(cr, TIMELINE_INFO_FONT_SIZE);
      snprintf(text, sizeof(text), "%d", (gint) llround(te->frame_num));
      timeline_draw_text_pill(cr,
                              text,
                              pos_x,
                              info_label_y,
                              width,
                              &info_fg,
                              0.98,
                              &info_bg,
                              0.84);
      cairo_set_font_size(cr, TIMELINE_LABEL_FONT_SIZE);
    }
  }

  return FALSE;
}

GtkWidget *timeline_new(void)
{
  GtkWidget *widget = GTK_WIDGET(g_object_new(timeline_get_type(), NULL));
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  gtk_widget_set_size_request(widget, 200, TIMELINE_MIN_HEIGHT_PX);
  gtk_widget_set_can_focus(widget, TRUE);

  gtk_widget_set_events(widget,
      GDK_EXPOSURE_MASK |
      GDK_POINTER_MOTION_MASK |
      GDK_BUTTON1_MOTION_MASK |
      GDK_BUTTON2_MOTION_MASK |
      GDK_BUTTON3_MOTION_MASK |
      GDK_BUTTON_PRESS_MASK |
      GDK_BUTTON_RELEASE_MASK |
      GDK_SCROLL_MASK |
      GDK_ENTER_NOTIFY_MASK |
      GDK_LEAVE_NOTIFY_MASK |
      GDK_FOCUS_CHANGE_MASK);

  g_signal_connect(G_OBJECT(widget), "motion_notify_event",
                   G_CALLBACK(event_motion), NULL);

  g_signal_connect(G_OBJECT(widget), "button_press_event",
                   G_CALLBACK(event_press), NULL);

  g_signal_connect(G_OBJECT(widget), "button_release_event",
                   G_CALLBACK(event_release), NULL);

  g_signal_connect(G_OBJECT(widget), "scroll_event",
                   G_CALLBACK(event_scroll), NULL);

  g_signal_connect(G_OBJECT(widget), "leave-notify-event",
                   G_CALLBACK(event_leave), NULL);

  g_signal_connect(G_OBJECT(widget), "focus-out-event",
                   G_CALLBACK(event_focus_out), NULL);

  te->widget = widget;

  TL_EVT(te, "new", "timeline widget created");

  return widget;
}
