/* veejay - Linux VeeJay
 *       (C) 2002-2015 Niels Elburg <nwelburg@gmail.com>
 *
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
  Implements a new slider type widget with selection markers

 */

 /*
 * Modified by d.j.a.y , 2018
 * - gtk3 compliant
 */

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <cairo.h>
#include "gtktimeselection.h"
#include "utils-gtk.h"

//G_DEFINE_TYPE(TimelineSelectionClass, timeline, GTK_TYPE_DRAWING_AREA );

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
  MOUSE_WIDGET /* inside widget but not in any of the above */
} mouse_location;


/* Slider with 2 bars



*/

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
  gdouble           stepper_size;   /* size of triangle */
  gdouble           stepper_draw_size;
  gdouble           stepper_length; /* length from top to bottom */
  gint              step_size;  /* step frames 1,2,4,8,16, ... */
  gdouble           frame_width;
  gdouble           frame_height;
  gdouble           font_line;
  gboolean          has_selection;  /* use in/out points for selection */
  gdouble           move_x;
  gdouble           point;
};

static void get_property( GObject *object,
                          guint id,
                          GValue *value ,
                          GParamSpec *pspec );

static  void  set_property ( GObject *object,
                             guint id,
                             const GValue * value,
                             GParamSpec *pspec );

static gboolean event_press (GtkWidget *widget, GdkEventButton *bev, gpointer user_data);

static gboolean event_release (GtkWidget *widget, GdkEventButton *bev, gpointer user_data);

static gboolean event_motion (GtkWidget *widget, GdkEventMotion *mev, gpointer user_data);

static gboolean event_scroll (GtkWidget *widget, GdkEventScroll *mev, gpointer user_data);

static  void  timeline_class_init( TimelineSelectionClass *class );

static  void  timeline_init(TimelineSelection *te );

static  gboolean  timeline_draw(GtkWidget *widget, cairo_t *cr );

static  GObjectClass  *parent_class = NULL;
static  gint  timeline_signals[LAST_SIGNAL] = { 0 };

#define TIMELINE_HANDLE_HIT_PX   10.0
#define TIMELINE_STEPPER_HIT_PX  28.0
#define TIMELINE_MIN_FRAMES      1.0

static inline gdouble
timeline_clamp01(gdouble v)
{
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static inline gdouble
timeline_frame_count(TimelineSelection *te)
{
    return MAX(te->num_video_frames, TIMELINE_MIN_FRAMES);
}

static inline gdouble
timeline_x_to_norm(TimelineSelection *te, gdouble x, gdouble width)
{
    if (width <= 1.0)
        return 0.0;

    return timeline_clamp01(x / width);
}

static inline gdouble
timeline_norm_to_frame(TimelineSelection *te, gdouble n)
{
    return floor(timeline_clamp01(n) * timeline_frame_count(te));
}

static inline gdouble
timeline_frame_to_x(TimelineSelection *te, gdouble frame, gdouble width)
{
    gdouble nframes = timeline_frame_count(te);

    if (nframes <= 0.0)
        return 0.0;

    return CLAMP((frame / nframes) * width, 0.0, width);
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

static inline gdouble
timeline_snap_norm(TimelineSelection *te, gdouble n)
{
    return snap_to_nearest_valid_position(timeline_clamp01(n),
                                          (int) timeline_frame_count(te));
}

static gboolean
timeline_point_near_x(gdouble x, gdouble target, gdouble radius)
{
    return fabs(x - target) <= radius;
}

static void
timeline_update_cursor(GtkWidget *widget, TimelineAction action)
{
    GdkWindow *window = gtk_widget_get_window(widget);

    if (!window)
        return;

    GdkDisplay *display = gdk_window_get_display(window);
    GdkCursor *cursor = NULL;

    switch (action) {
        case action_pos:
        case action_in_point:
        case action_out_point:
            cursor = gdk_cursor_new_for_display(display, GDK_SB_H_DOUBLE_ARROW);
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

static TimelineAction
timeline_pick_action(TimelineSelection *te,
                     gdouble x,
                     gdouble y,
                     gdouble width)
{
    if (te->has_stepper && POINT_IN_RECT(x, y, te->stepper))
        return action_pos;

    if (!te->has_selection)
        return action_none;

    gdouble in_x  = te->in  * width;
    gdouble out_x = te->out * width;

    if (timeline_point_near_x(x, in_x, TIMELINE_HANDLE_HIT_PX))
        return action_in_point;

    if (timeline_point_near_x(x, out_x, TIMELINE_HANDLE_HIT_PX))
        return action_out_point;

    if (POINT_IN_RECT(x, y, te->selection)) {
        if (te->bind)
            return action_atomic;

        if (fabs(x - in_x) < fabs(x - out_x))
            return action_in_point;

        return action_out_point;
    }

    return action_point;
}

extern  void on_timeline_move_selection();

struct _TimelineSelectionClass
{
  GtkWidgetClass  parent_class;
  void  (*pos_changed) (TimelineSelection *te);
  void  (*point_changed) (TimelineSelection *te);
  void  (*in_point_changed) (TimelineSelection *te);
  void  (*out_point_changed) (TimelineSelection *te);
  void  (*bind_toggled) (TimelineSelection *te);
  void  (*cleared) (TimelineSelection *te);
};
static  void  set_property  (GObject *object,
  guint id, const GValue *value, GParamSpec *pspec)
{
  TimelineSelection *te = TIMELINE_SELECTION(object);
  switch(id)
  {
    case MIN:
    if(te->min != g_value_get_double(value))
    {
      te->min = g_value_get_double(value);
    }
    break;
    case MAX:
    if(te->max != g_value_get_double(value))
    {
      te->max = g_value_get_double(value);
    }
    break;
    case POS:
    if(te->frame_num != g_value_get_double(value))
    {
      te->frame_num = g_value_get_double(value);
    }
    break;
    case POINT:
    if(te->point != g_value_get_double(value))
    {
        te->point = g_value_get_double(value);
    }
    break;
    case LENGTH:
    if(te->num_video_frames != g_value_get_double(value))
    {
      te->num_video_frames = g_value_get_double(value);
    }
    break;
    case IN_POINT:
    if(te->in != g_value_get_double(value))
    {
        te->in = g_value_get_double(value);
    }
    break;
    case OUT_POINT:
    if(te->out != g_value_get_double(value))
    {
        te->out = g_value_get_double(value);
    }
    break;
    case SEL:
    if(te->has_selection != g_value_get_boolean(value))
    {
      te->has_selection = g_value_get_boolean(value);
    }
    break;
    case BIND:
    if(te->bind != g_value_get_boolean(value))
    {
      te->bind = g_value_get_boolean(value);
    }
    break;
    case CLEARED:
    if(te->clear != g_value_get_boolean(value))
    {
      te->clear = g_value_get_boolean(value);
    }
    break;
    default:
      g_assert(FALSE);
    break;
  }
  gtk_widget_queue_draw( GTK_WIDGET( te ));

}

static void get_property( GObject *object,
  guint id, GValue *value , GParamSpec *pspec )
{
  TimelineSelection *te = TIMELINE_SELECTION(object);
  switch( id )
  {
    case MIN: g_value_set_double( value, te->min );break;
    case MAX: g_value_set_double( value, te->max );break;
    case POS: g_value_set_double( value, te->frame_num ); break;
    case POINT: g_value_set_double( value, te->point ); break;
    case LENGTH: g_value_set_double( value, te->num_video_frames ); break;
    case IN_POINT: g_value_set_double( value, te->in ); break;
    case OUT_POINT: g_value_set_double( value, te->out ); break;
    case SEL: g_value_set_boolean(value, te->has_selection) ; break;
    case BIND: g_value_set_boolean(value, te->bind ); break;
    case CLEARED: g_value_set_boolean(value,te->clear );break;
  }
}

static  void  finalize  (GObject *object)
{
  parent_class->finalize( object );
}

static  void  timeline_class_init( TimelineSelectionClass *class )
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);
  widget_class->draw = timeline_draw;

  GObjectClass *gobject_class;
  gobject_class = G_OBJECT_CLASS( class );
  parent_class = g_type_class_peek( GTK_TYPE_DRAWING_AREA );
  gobject_class->finalize = finalize;
  gobject_class->get_property = get_property;
  gobject_class->set_property = set_property;

  g_object_class_install_property(gobject_class,
                                  MIN,
                                  g_param_spec_double( "min",
                                                       "left",
                                                       "left",
                                                       0.0,
                                                       1.0,
                                                       0.0,
                                                       G_PARAM_READWRITE )
                                  );

  g_object_class_install_property(gobject_class,
                                  MAX,
                                  g_param_spec_double( "max",
                                                       "right",
                                                       "right",
                                                       0.0,
                                                       1.0,
                                                       1.0,
                                                       G_PARAM_READWRITE )
                                 );

  g_object_class_install_property( gobject_class,
      POS,
      g_param_spec_double( "pos",
        "current position", "current position", 0.0,9999999.0, 0.0,
        G_PARAM_READWRITE ));

  g_object_class_install_property( gobject_class,
      POINT,
      g_param_spec_double( "point",
        "point at position", "point at position", 0.0,9999999.0, 0.0,
        G_PARAM_READWRITE ));

  g_object_class_install_property( gobject_class,
      LENGTH,
      g_param_spec_double( "length",
        "Length (in frames)", "Length (in frames) ",0.0,9999999.0, 1.0,
        G_PARAM_READWRITE ));

  g_object_class_install_property( gobject_class,
      IN_POINT,
      g_param_spec_double( "in",
        "In point", "(in frames) ",0.0,1.0, 0.0,
        G_PARAM_READWRITE ));

  g_object_class_install_property( gobject_class,
      OUT_POINT,
      g_param_spec_double( "out",
        "Out point", "(in frames) ",0.0,1.0, 1.0,
        G_PARAM_READWRITE ));

  g_object_class_install_property( gobject_class,
      SEL,
      g_param_spec_boolean( "selection",
        "Marker", "(in frames) ",FALSE,
        G_PARAM_READWRITE ));

  g_object_class_install_property( gobject_class,
      BIND,
      g_param_spec_boolean( "bind", "Bind marker", "Bind In/Out points",  FALSE, G_PARAM_READWRITE));

  g_object_class_install_property( gobject_class,
      CLEARED,
      g_param_spec_boolean( "clear", "Clear marker", "Clear in/out points", FALSE, G_PARAM_READWRITE ));

  timeline_signals[ SELECTION_CHANGED_SIGNAL ] = g_signal_new( "selection_changed",
                        G_TYPE_FROM_CLASS(gobject_class),
                        G_SIGNAL_RUN_FIRST,0,NULL,NULL,
                        g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0 , NULL );

  timeline_signals[ POS_CHANGED ] = g_signal_new( "pos_changed",
                        G_TYPE_FROM_CLASS(gobject_class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET( TimelineSelectionClass, pos_changed ),
                        NULL,NULL,
                        g_cclosure_marshal_VOID__VOID,
                        G_TYPE_NONE, 0, NULL);

  timeline_signals[ POINT_CHANGED ] = g_signal_new( "point_changed",
                        G_TYPE_FROM_CLASS(gobject_class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET( TimelineSelectionClass, point_changed ),
                        NULL,NULL,
                        g_cclosure_marshal_VOID__VOID,
                        G_TYPE_NONE, 0, NULL);

  timeline_signals[ IN_CHANGED ] = g_signal_new( "in_point_changed",
                        G_TYPE_FROM_CLASS(gobject_class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET( TimelineSelectionClass, in_point_changed ),
                        NULL,NULL,
                        g_cclosure_marshal_VOID__VOID,
                        G_TYPE_NONE, 0, NULL);

  timeline_signals[ OUT_CHANGED ] = g_signal_new( "out_point_changed",
                        G_TYPE_FROM_CLASS(gobject_class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET( TimelineSelectionClass, out_point_changed ),
                        NULL,NULL,
                        g_cclosure_marshal_VOID__VOID,
                        G_TYPE_NONE, 0, NULL);

  timeline_signals[ CLEAR_CHANGED ] = g_signal_new( "cleared", G_TYPE_FROM_CLASS(gobject_class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET( TimelineSelectionClass, cleared ),
                        NULL,NULL,
                        g_cclosure_marshal_VOID__VOID,
                        G_TYPE_NONE, 0, NULL );

  timeline_signals[ BIND_CHANGED ] = g_signal_new( "bind_toggled",
                        G_TYPE_FROM_CLASS(gobject_class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET( TimelineSelectionClass, bind_toggled ),
                        NULL,NULL,
                        g_cclosure_marshal_VOID__VOID,
                        G_TYPE_NONE, 0, NULL );

}

static void timeline_init( TimelineSelection *te )
{
  te->min      = 0.0;
  te->max      = 0.0;
  te->action   = action_none;
  te->in       = 0.0;
  te->out      = 1.0;
  te->num_video_frames = 1.0;
  te->frame_num = 0.0;
  te->grab_location = MOUSE_OUTSIDE;
  te->current_location = MOUSE_OUTSIDE;
  te->grab_button = 0;
  te->has_stepper = TRUE;
  te->has_selection = FALSE;
  te->stepper_size = 24;
  te->stepper_draw_size = 12;
  te->stepper_length = 0;
  te->frame_height = 8;
  te->font_line = 24;
  te->point = 0.0;
  te->move_x = 0;
}

GType timeline_get_type(void)
{
  static GType gtype = 0;
  if(!gtype)
  {
    static const GTypeInfo ginfo = {sizeof( TimelineSelectionClass),
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
    gtype = g_type_register_static( GTK_TYPE_DRAWING_AREA, "Timeline", &ginfo, 0 );
  }
  return gtype;
}

gdouble timeline_get_in_point( TimelineSelection *te )
{
  gdouble result = 0.0;
  g_object_get( G_OBJECT(te), "in", &result, NULL );
  return result;
}

gdouble timeline_get_out_point( TimelineSelection *te )
{
  gdouble result = 0.0;
  g_object_get( G_OBJECT(te), "out", &result, NULL );
  return result;
}

gboolean timeline_get_selection( TimelineSelection *te )
{
  gboolean result = FALSE;
  g_object_get( G_OBJECT(te), "selection", &result, NULL );
  return result;
}

gboolean timeline_get_bind( TimelineSelection *te )
{
  gboolean result = FALSE;
  g_object_get( G_OBJECT(te), "bind", &result, NULL );
  return result;
}

void timeline_set_bind(GtkWidget *widget, gboolean active)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  g_object_set( G_OBJECT(te), "bind", active, NULL );
  g_signal_emit( te->widget, timeline_signals[BIND_CHANGED], 0);
}

void timeline_set_out_point(GtkWidget *widget, gdouble pos)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  if(te->bind)
    return;

  pos = timeline_snap_norm(te, pos);

  if (pos < te->in)
      pos = te->in;

  g_object_set(G_OBJECT(te), "out", pos, NULL);
  g_signal_emit(te->widget, timeline_signals[OUT_CHANGED], 0);
  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

void timeline_clear_points( GtkWidget *widget )
{
  gboolean cleared = TRUE;
  gdouble  pos = 0.0;
  gdouble  pos2 = 1.0;
  gboolean bind = FALSE;
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  g_object_set( G_OBJECT(te), "bind", bind, NULL );
  g_object_set( G_OBJECT(te), "clear", cleared, NULL );
  g_object_set( G_OBJECT(te), "in", pos, NULL );
  g_object_set( G_OBJECT(te), "out", pos2,  NULL );
  g_signal_emit(te->widget, timeline_signals[CLEAR_CHANGED], 0 );
  gtk_widget_queue_draw(GTK_WIDGET(te->widget) );
}

void timeline_set_in_point(GtkWidget *widget, gdouble pos)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  if(te->bind)
    return;

  pos = timeline_snap_norm(te, pos);

  if (pos > te->out)
      pos = te->out;

  g_object_set(G_OBJECT(te), "in", pos, NULL);
  g_signal_emit(te->widget, timeline_signals[IN_CHANGED], 0);
  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

void timeline_set_in_and_out_point(GtkWidget *widget, gdouble start, gdouble end)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  start = timeline_snap_norm(te, start);
  end   = timeline_snap_norm(te, end);

  if (start > end) {
      gdouble tmp = start;
      start = end;
      end = tmp;
  }

  g_object_set(G_OBJECT(te), "in", start, NULL);
  g_object_set(G_OBJECT(te), "out", end, NULL);

  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

void timeline_set_selection( GtkWidget *widget, gboolean active)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  g_object_set( G_OBJECT(te), "selection", active, NULL );
  gtk_widget_queue_draw( GTK_WIDGET(te->widget) );
}

void  timeline_set_length( GtkWidget *widget, gdouble length, gdouble pos)
{
  TimelineSelection *te = TIMELINE_SELECTION( widget );
  if( pos < 0.0 ) pos = 0.0;
  g_object_set( G_OBJECT(te), "length", length, NULL );
  timeline_set_pos( GTK_WIDGET(te->widget), pos );
}

void timeline_set_pos(GtkWidget *widget, gdouble pos)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  gdouble nframes = timeline_frame_count(te);

  if (pos < 0.0)
      pos = 0.0;
  else if (pos > nframes)
      pos = nframes;

  g_object_set(G_OBJECT(te), "pos", pos, NULL);
  g_signal_emit(te->widget, timeline_signals[POS_CHANGED], 0);
  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}
gdouble timeline_get_pos( TimelineSelection *te )
{
  gdouble result = 0.0;
  g_object_get( G_OBJECT(te), "pos", &result, NULL );
  return result;
}

void  timeline_set_point( GtkWidget *widget,gdouble point)
{
  TimelineSelection *te = TIMELINE_SELECTION( widget );
  if( point < 0.0 ) point = 0.0;
  g_object_set( G_OBJECT(te), "point", point, NULL );
  g_signal_emit( te->widget, timeline_signals[POINT_CHANGED], 0);
  gtk_widget_queue_draw( GTK_WIDGET(te->widget) );
}

gdouble timeline_get_point( TimelineSelection *te )
{
  gdouble result = 0.0;
  g_object_get( G_OBJECT(te), "point", &result, NULL );
  return result;
}

gdouble timeline_get_length( TimelineSelection *te )
{
  gdouble result = 0.0;
  g_object_get( G_OBJECT(te), "length", &result, NULL );
  return result;
}

static void move_selection(GtkWidget *widget, gdouble x, gdouble width)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  gdouble range = te->out - te->in;

  if (range <= 0.0 || width <= 1.0)
      return;

  gdouble center_norm = timeline_x_to_norm(te, x, width);

  gdouble new_in = center_norm - te->move_x;
  gdouble new_out = new_in + range;

  if (new_in < 0.0) {
      new_in = 0.0;
      new_out = range;
  }
  else if (new_out > 1.0) {
      new_out = 1.0;
      new_in = 1.0 - range;
  }

  new_in  = timeline_snap_norm(te, new_in);
  new_out = timeline_snap_norm(te, new_out);

  if (new_out < new_in) {
      gdouble tmp = new_in;
      new_in = new_out;
      new_out = tmp;
  }

  te->in = new_in;
  te->out = new_out;

  g_signal_emit(te->widget, timeline_signals[IN_CHANGED], 0);
  g_signal_emit(te->widget, timeline_signals[OUT_CHANGED], 0);

  gtk_widget_queue_draw(widget);

  on_timeline_move_selection();
}

static gboolean event_scroll(GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  GdkScrollDirection direction;

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

  gtk_widget_get_allocation(widget, &all);

  gdouble width = (gdouble) all.width;

  te->grab_button = ev->button;
  te->current_location = MOUSE_WIDGET;
  te->action = action_none;

  gtk_widget_grab_focus(widget);

  if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1) {
    timeline_clear_points(widget);
    return TRUE;
  }

  TimelineAction picked = timeline_pick_action(te, ev->x, ev->y, width);
  te->action = picked;

  if (ev->button == 1) {
    switch (picked) {
      case action_pos:
        te->current_location = MOUSE_STEPPER;
        timeline_set_pos(widget, timeline_norm_to_frame(te,
                         timeline_x_to_norm(te, ev->x, width)));
        return TRUE;

      case action_atomic:
        te->current_location = MOUSE_SELECTION;

        {
          gdouble mouse_norm = timeline_x_to_norm(te, ev->x, width);
          te->move_x = mouse_norm - te->in;
        }

        return TRUE;

      case action_in_point:
        te->bind = FALSE;
        timeline_set_in_point(widget, timeline_x_to_norm(te, ev->x, width));
        return TRUE;

      case action_out_point:
        te->bind = FALSE;
        timeline_set_out_point(widget, timeline_x_to_norm(te, ev->x, width));
        return TRUE;

      case action_point:
        timeline_set_point(widget, timeline_norm_to_frame(te,
                           timeline_x_to_norm(te, ev->x, width)));
        return TRUE;

      default:
        break;
    }
  }

  if (ev->button == 3 && te->has_selection) {
    te->bind = FALSE;
    te->action = action_out_point;
    timeline_set_out_point(widget, timeline_x_to_norm(te, ev->x, width));
    return TRUE;
  }

  if (ev->button == 2 && te->has_selection) {
    if (POINT_IN_RECT(ev->x, ev->y, te->selection)) {
      timeline_set_bind(widget, te->bind ? FALSE : TRUE);

      if (te->bind) {
        gdouble mouse_norm = timeline_x_to_norm(te, ev->x, width);
        te->move_x = mouse_norm - te->in;
        te->action = action_atomic;
        te->current_location = MOUSE_SELECTION;
      }

      return TRUE;
    }
  }

  gtk_widget_queue_draw(widget);

  return TRUE;
}

static gboolean event_release(GtkWidget *widget, GdkEventButton *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  te->action = action_none;
  te->current_location = MOUSE_WIDGET;
  te->grab_button = 0;
  te->move_x = 0.0;

  timeline_update_cursor(widget, action_none);
  gtk_widget_queue_draw(widget);

  return TRUE;
}

static gboolean event_motion(GtkWidget *widget, GdkEventMotion *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  GtkAllocation all;

  gtk_widget_get_allocation(widget, &all);

  gdouble width = (gdouble) all.width;

  if (width <= 1.0)
      return TRUE;

  gboolean b1 = (ev->state & GDK_BUTTON1_MASK) != 0;
  gboolean b2 = (ev->state & GDK_BUTTON2_MASK) != 0;
  gboolean b3 = (ev->state & GDK_BUTTON3_MASK) != 0;

  if (!b1 && !b2 && !b3) {
    TimelineAction hover = timeline_pick_action(te, ev->x, ev->y, width);
    timeline_update_cursor(widget, hover);

    timeline_set_point(widget,
        timeline_norm_to_frame(te, timeline_x_to_norm(te, ev->x, width)));

    return TRUE;
  }

  if (b1) {
    switch (te->action) {
      case action_pos:
        timeline_set_pos(widget,
            timeline_norm_to_frame(te, timeline_x_to_norm(te, ev->x, width)));
        return TRUE;

      case action_atomic:
        if (te->has_selection && te->bind)
            move_selection(widget, ev->x, width);
        return TRUE;

      case action_in_point:
        if (te->has_selection && !te->bind)
            timeline_set_in_point(widget, timeline_x_to_norm(te, ev->x, width));
        return TRUE;

      case action_out_point:
        if (te->has_selection && !te->bind)
            timeline_set_out_point(widget, timeline_x_to_norm(te, ev->x, width));
        return TRUE;

      case action_point:
        timeline_set_point(widget,
            timeline_norm_to_frame(te, timeline_x_to_norm(te, ev->x, width)));
        return TRUE;

      default:
        break;
    }
  }

  if (b2 && te->has_selection && te->bind) {
    move_selection(widget, ev->x, width);
    return TRUE;
  }

  if (b3 && te->has_selection && !te->bind) {
    timeline_set_out_point(widget, timeline_x_to_norm(te, ev->x, width));
    return TRUE;
  }

  return TRUE;
}

/*!
 *  draw a rounded rectangle
*/ 
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

static gboolean timeline_draw(GtkWidget *widget, cairo_t *cr)
{
  gchar text[64];

  TimelineSelection *te = TIMELINE_SELECTION(widget);

  double width  = gtk_widget_get_allocated_width(widget);
  double height = gtk_widget_get_allocated_height(widget);

  if (width <= 1.0 || height <= 1.0)
      return FALSE;

  gdouble nframes = timeline_frame_count(te);
  gdouble marker_width = width / nframes;
  gdouble marker_height = te->frame_height;

  te->frame_width = marker_width;

  GtkStyleContext *sc = gtk_widget_get_style_context(widget);

  GdkRGBA color;
  gtk_style_context_get_color(sc, gtk_style_context_get_state(sc), &color);

  GdkRGBA col2;
  vj_gtk_context_get_color(sc, "border-color", GTK_STATE_FLAG_NORMAL, &col2);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_FAST);
  cairo_select_font_face(cr,
                         "Sans",
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);

  gdouble tri_h = te->has_stepper ? te->stepper_draw_size : 0.0;
  gdouble track_h = marker_height;
  gdouble group_h = tri_h + track_h;

  gdouble group_y = floor((height - group_h) * 0.5);
  if (group_y < 0.0)
      group_y = 0.0;

  gdouble tri_y = group_y;
  gdouble track_y = group_y + tri_h;
  gdouble track_bottom = track_y + track_h;
  gdouble handle_w = 3.0;

  if (track_bottom > height) {
      track_y = MAX(0.0, height - track_h);
      track_bottom = track_y + track_h;
      tri_y = MAX(0.0, track_y - tri_h);
  }

  cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.25);
  cairo_rectangle_round(cr, 0.0, track_y, width, track_h, 10.0);

  if (te->has_selection) {
    gdouble in_x  = timeline_clamp01(te->in)  * width;
    gdouble out_x = timeline_clamp01(te->out) * width;

    if (out_x < in_x) {
        gdouble tmp = in_x;
        in_x = out_x;
        out_x = tmp;
    }

    gdouble sel_w = MAX(0.0, out_x - in_x);

    cairo_set_source_rgba(cr, col2.red, col2.green, col2.blue, 0.90);
    cairo_rectangle_round(cr, in_x, track_y, sel_w, track_h, 10.0);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.85);

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

    if (te->bind) {
        gdouble cx = in_x + (sel_w * 0.5);

        cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.55);
        cairo_arc(cr,
                  cx,
                  track_y + (track_h * 0.5),
                  3.0,
                  0.0,
                  2.0 * M_PI);
        cairo_fill(cr);
    }

    te->selection.x = (gint) in_x;
    te->selection.y = (gint) tri_y;
    te->selection.width = (gint) sel_w;
    te->selection.height = (gint) (track_bottom - tri_y + 4.0);

    if ((te->grab_button == 1 || te->grab_button == 3) &&
        te->current_location != MOUSE_STEPPER)
    {
      gdouble f1 = te->in  * nframes;
      gdouble f2 = te->out * nframes;

      gdouble text_y = track_bottom + 10.0;
      if (text_y > height - 2.0)
          text_y = MAX(10.0, tri_y + 10.0);

      cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.75);
      cairo_move_to(cr, in_x, text_y);
      snprintf(text, sizeof(text), "%d - %d", (gint) f1, (gint) f2);
      cairo_show_text(cr, text);
    }
  }

  {
    gdouble point_x = timeline_frame_to_x(te, te->point, width);

    gdouble text_y = track_bottom + 10.0;
    if (text_y > height - 2.0)
        text_y = MAX(10.0, tri_y + 10.0);

    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.30);
    cairo_move_to(cr, point_x, text_y);
    snprintf(text, sizeof(text), "%d", (gint) te->point);
    cairo_show_text(cr, text);
  }

  if (te->has_stepper) {
    gdouble pos_x = timeline_frame_to_x(te, te->frame_num, width);

    te->stepper.x = (gint) (pos_x - (TIMELINE_STEPPER_HIT_PX * 0.5));
    te->stepper.y = (gint) tri_y;
    te->stepper.width = (gint) TIMELINE_STEPPER_HIT_PX;
    te->stepper.height = (gint) ((track_bottom - tri_y) + 4.0);

    cairo_set_source_rgba(cr,
                          col2.red   * 0.45,
                          col2.green * 0.45,
                          col2.blue  * 0.45,
                          1.0);

    cairo_move_to(cr, pos_x - te->stepper_draw_size, tri_y);
    cairo_line_to(cr, pos_x, tri_y + tri_h);
    cairo_line_to(cr, pos_x + te->stepper_draw_size, tri_y);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_set_source_rgba(cr,
                          col2.red   * 0.45,
                          col2.green * 0.45,
                          col2.blue  * 0.45,
                          0.95);

    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, pos_x, tri_y + tri_h);
    cairo_line_to(cr, pos_x, track_bottom);
    cairo_stroke(cr);

    if (te->grab_button == 1 && te->current_location == MOUSE_STEPPER) {
      gdouble text_y = track_bottom + 10.0;
      if (text_y > height - 2.0)
          text_y = MAX(10.0, tri_y + 10.0);

      cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.75);
      cairo_move_to(cr, pos_x, text_y);
      snprintf(text, sizeof(text), "%d", (gint) te->frame_num);
      cairo_show_text(cr, text);
    }
  }

  return FALSE;
}

GtkWidget *timeline_new(void)
{
  GtkWidget *widget = GTK_WIDGET(g_object_new(timeline_get_type(), NULL));
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  gtk_widget_set_size_request(widget, 200, 24);

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
      GDK_LEAVE_NOTIFY_MASK);

  g_signal_connect(G_OBJECT(widget), "draw",
                   G_CALLBACK(timeline_draw), NULL);

  g_signal_connect(G_OBJECT(widget), "motion_notify_event",
                   G_CALLBACK(event_motion), NULL);

  g_signal_connect(G_OBJECT(widget), "button_press_event",
                   G_CALLBACK(event_press), NULL);

  g_signal_connect(G_OBJECT(widget), "button_release_event",
                   G_CALLBACK(event_release), NULL);

  g_signal_connect(G_OBJECT(widget), "scroll_event",
                   G_CALLBACK(event_scroll), NULL);

  te->widget = widget;

  return widget;
}
