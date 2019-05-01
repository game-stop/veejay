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

#include "gtktimeselection.h"

//G_DEFINE_TYPE(TimelineSelectionClass, timeline, GTK_TYPE_DRAWING_AREA );

enum
{
  POS_CHANGED,
  IN_CHANGED,
  OUT_CHANGED,
  BIND_CHANGED,
  CLEAR_CHANGED,
  SELECTION_CHANGED_SIGNAL,
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

struct _TimelineSelectionClass
{
  GtkWidgetClass  parent_class;
  void  (*pos_changed) (TimelineSelection *te);
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
      te->min = g_value_get_double(value);
    }
    break;
    case POS:
    if(te->frame_num != g_value_get_double(value))
    {
      te->frame_num = g_value_get_double(value);
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
      if( g_value_get_double(value) < te->out )
        te->in = g_value_get_double(value);
    }
    break;
    case OUT_POINT:
    if(te->out != g_value_get_double(value))
    {
      if( g_value_get_double(value) > te->in )
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
  te->stepper_draw_size = 16;
  te->stepper_length = 0;
  te->frame_height = 10;
  te->font_line = 18;
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

void timeline_set_out_point( GtkWidget *widget, gdouble pos )
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  if( pos < 0.0 ) pos = 0.0; else if (pos > 1.0 ) pos = 1.0;
  g_object_set( G_OBJECT(te), "out", pos, NULL );
  g_signal_emit(te->widget, timeline_signals[OUT_CHANGED], 0);
  gtk_widget_queue_draw( GTK_WIDGET(te->widget) );
}

void timeline_clear_points( GtkWidget *widget )
{
  gboolean cleared = TRUE;
  gdouble  pos = 0.0;
  gdouble  pos2 = 1.0;
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  g_object_set( G_OBJECT(te), "clear", cleared, NULL );
  g_object_set( G_OBJECT(te), "in", pos, NULL );
  g_object_set( G_OBJECT(te), "out", pos2,  NULL );
  g_signal_emit(te->widget, timeline_signals[CLEAR_CHANGED], 0 );
  gtk_widget_queue_draw(GTK_WIDGET(te->widget) );
}

void timeline_set_in_point( GtkWidget *widget, gdouble pos )
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  if( pos < 0.0 ) pos = 0.0; else if (pos > 1.0 ) pos = 1.0;
  g_object_set( G_OBJECT(te), "in", pos, NULL );
  g_signal_emit(te->widget, timeline_signals[IN_CHANGED], 0);
  gtk_widget_queue_draw( GTK_WIDGET(te->widget) );
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

void  timeline_set_pos( GtkWidget *widget,gdouble pos)
{
  TimelineSelection *te = TIMELINE_SELECTION( widget );
  if( pos < 0.0 ) pos = 0.0;
  g_object_set( G_OBJECT(te), "pos", pos, NULL );
  g_signal_emit( te->widget, timeline_signals[POS_CHANGED], 0);
  gtk_widget_queue_draw( GTK_WIDGET(te->widget) );
}

gdouble timeline_get_pos( TimelineSelection *te )
{
  gdouble result = 0.0;
  g_object_get( G_OBJECT(te), "pos", &result, NULL );
  return result;
}

gdouble timeline_get_length( TimelineSelection *te )
{
  gdouble result = 0.0;
  g_object_get( G_OBJECT(te), "length", &result, NULL );
  return result;
}

static  void  move_selection( GtkWidget *widget, gdouble x, gdouble width )
{
  TimelineSelection *te = TIMELINE_SELECTION( widget );

  gdouble dx3  = (0.5 * (te->out - te->in)) * width;

  gdouble dx1  = x - dx3;
  gdouble dx2  = x + dx3;

  te->in = (1.0/width) * dx1;
  te->out = (1.0/width ) * dx2;

  timeline_set_out_point(widget, te->out );
  timeline_set_in_point(widget, te->in );
  te->move_x = x;
}

static gboolean
event_scroll (GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION (widget);

  GdkScrollDirection direction;
  gboolean scroll_status = gdk_event_get_scroll_direction((GdkEvent*)ev, &direction);
  if(scroll_status)
  {
    if( direction == GDK_SCROLL_UP ) {
      gdouble cur_pos = timeline_get_pos(te);
      timeline_set_pos( widget, cur_pos + 1 );
    }
    else if( direction == GDK_SCROLL_DOWN ) {
      gdouble cur_pos = timeline_get_pos(te);
      timeline_set_pos( widget, cur_pos - 1 );
    }
  }
  gtk_widget_queue_draw( widget );

  return FALSE;
}

static  gboolean event_press(GtkWidget *widget, GdkEventButton *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION( widget );
  GtkAllocation all;
  gtk_widget_get_allocation(widget, &all);
  gdouble width   = all.width;

  te->grab_button = ev->button;
  te->current_location = MOUSE_WIDGET;

  if( ev->type == GDK_2BUTTON_PRESS && te->grab_button == 1 )
  {
    timeline_clear_points( widget );
    return FALSE;
  }

  if(te->grab_button == 1 && POINT_IN_RECT( ev->x, ev->y, te->stepper ) )
  {
    if(te->has_stepper)
    {
      te->current_location = MOUSE_STEPPER;
      te->action = action_pos;
    }
    return FALSE;
  }

  if(te->grab_button == 1 && te->has_selection)
  {
    if( POINT_IN_RECT( ev->x, ev->y, te->selection ) && te->bind )
    {
      te->current_location = MOUSE_SELECTION;
    }
    if(!te->bind)
    {
      gdouble val = (1.0 / width) * ev->x;
      timeline_set_in_point( widget, val );
    }
  }
  else if(te->grab_button == 3 && te->has_selection )
  {
    if( POINT_IN_RECT( ev->x, ev->y, te->selection ) && te->bind )
    {
      te->current_location = MOUSE_SELECTION;
    }
    if(!te->bind)
    {
      gdouble val = (1.0/width) * ev->x;
      timeline_set_out_point( widget, val );
    }
  }
  else if(te->grab_button == 2 && te->has_selection)
  {
    gint dx = ev->x;
    gint dy = ev->y;
    if( POINT_IN_RECT( dx, dy, te->selection ) )
    {
      timeline_set_bind( widget,  (te->bind ? FALSE: TRUE ));
      te->move_x = (gdouble) ev->x;
    }
  }


  gtk_widget_queue_draw( widget );

  return FALSE;
}

static gboolean event_release (GtkWidget *widget, GdkEventButton *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION (widget);
  te->action = action_none;
  te->current_location = MOUSE_WIDGET;
//  te->grab_button = 0;
//  te->move_x = 0;
  return FALSE;
}

static gboolean event_motion (GtkWidget *widget, GdkEventMotion *ev, gpointer user_data)
{
  TimelineSelection *te = TIMELINE_SELECTION (widget);
  GtkAllocation all;
  gtk_widget_get_allocation(widget, &all);
  gdouble width = (gdouble) all.width;
  gint x,y;
  GdkModifierType state;
  gdk_window_get_device_position ( ev->window, ev->device, &x, &y, &state );

  if( te->has_stepper && te->current_location == MOUSE_STEPPER && ev->state & GDK_BUTTON1_MASK)
  {
    gdouble rel_pos = ((gdouble)ev->x / width) * te->num_video_frames;
    gdouble new_pos = (gdouble) ((gint) rel_pos );

    timeline_set_pos( widget, new_pos );
    return FALSE;
  }

  if( te->has_selection && te->current_location != MOUSE_STEPPER)
  {
    if(!te->bind)
    {
      gdouble gx = (1.0 / width) * x;
      if(te->grab_button == 1  && ev->state & GDK_BUTTON1_MASK)
        timeline_set_in_point(widget, gx );
      else if(te->grab_button == 3 && ev->state & GDK_BUTTON3_MASK)
        timeline_set_out_point( widget, gx );
    }
  }

  if(te->has_selection && te->bind && te->grab_button == 2 )
    move_selection( widget, x, width );

  gtk_widget_queue_draw( widget );

  return FALSE;
}

/*!
 *  draw a rounded rectangle
 */
void cairo_rectangle_round ( cairo_t * cr,
                             double x0,
                             double y0,
                             double width,
                             double height,
                             double radius)
{
  double    x1, y1;

  x1 = x0 + width;
  y1 = y0 + height;
  if (width <= 0.001 || height <= 0.001)
    return;
  if (width / 2 < radius)
    {
      if (height / 2 < radius)
        {
          cairo_move_to (cr, x0, (y0 + y1) / 2);
          cairo_curve_to (cr, x0, y0, x0, y0, (x0 + x1) / 2, y0);
          cairo_curve_to (cr, x1, y0, x1, y0, x1, (y0 + y1) / 2);
          cairo_curve_to (cr, x1, y1, x1, y1, (x1 + x0) / 2, y1);
          cairo_curve_to (cr, x0, y1, x0, y1, x0, (y0 + y1) / 2);
        }
      else
        {
          cairo_move_to (cr, x0, y0 + radius);
          cairo_curve_to (cr, x0, y0, x0, y0, (x0 + x1) / 2, y0);
          cairo_curve_to (cr, x1, y0, x1, y0, x1, y0 + radius);
          cairo_line_to (cr, x1, y1 - radius);
          cairo_curve_to (cr, x1, y1, x1, y1, (x1 + x0) / 2, y1);
          cairo_curve_to (cr, x0, y1, x0, y1, x0, y1 - radius);
        }
    }
  else
{
      if (height / 2 < radius)
        {
          cairo_move_to (cr, x0, (y0 + y1) / 2);
          cairo_curve_to (cr, x0, y0, x0, y0, x0 + radius, y0);
          cairo_line_to (cr, x1 - radius, y0);
          cairo_curve_to (cr, x1, y0, x1, y0, x1, (y0 + y1) / 2);
          cairo_curve_to (cr, x1, y1, x1, y1, x1 - radius, y1);
          cairo_line_to (cr, x0 + radius, y1);
          cairo_curve_to (cr, x0, y1, x0, y1, x0, (y0 + y1) / 2);
        }
      else
        {
          cairo_move_to (cr, x0, y0 + radius);
          cairo_curve_to (cr, x0, y0, x0, y0, x0 + radius, y0);
          cairo_line_to (cr, x1 - radius, y0);
          cairo_curve_to (cr, x1, y0, x1, y0, x1, y0 + radius);
          cairo_line_to (cr, x1, y1 - radius);
          cairo_curve_to (cr, x1, y1, x1, y1, x1 - radius, y1);
          cairo_line_to (cr, x0 + radius, y1);
          cairo_curve_to (cr, x0, y1, x0, y1, x0, y1 - radius);
        }
    }
  cairo_close_path (cr);
}

static gboolean timeline_draw (GtkWidget *widget, cairo_t *cr )
{
  TimelineSelection *te = TIMELINE_SELECTION( widget );
  double width = gtk_widget_get_allocated_width (widget);
  double height = gtk_widget_get_allocated_height (widget);

  gdouble marker_width = width/ te->num_video_frames;
//  gdouble marker_height = height / te->num_video_frames;
  gdouble marker_height = te->frame_height;

  te->frame_width = marker_width;

  GtkStyleContext *sc = gtk_widget_get_style_context(widget);
  GdkRGBA color;
  gtk_style_context_get_color ( sc, gtk_style_context_get_state (sc), &color );

/* Draw stepper */
  if( te->has_stepper )
  {
    cairo_set_source_rgba( cr, 1.0,0.0,0.0,1.0); //FIXME use context state color ?
    double x1 = marker_width * te->frame_num;
    te->stepper.x = x1 - 8;
    te->stepper.y = 0;
    te->stepper.width = te->stepper_size + 8;
    te->stepper.height = te->stepper_size + 2;

    cairo_move_to( cr, x1 - te->stepper_draw_size, 0.0 * height );
    cairo_rel_line_to( cr, te->stepper_draw_size, te->stepper_draw_size  );
    cairo_rel_line_to( cr, te->stepper_draw_size, -te->stepper_draw_size  );
    cairo_rel_line_to( cr, -2.0 * te->stepper_draw_size, 0 );
    cairo_set_line_join( cr, CAIRO_LINE_JOIN_MITER);
    cairo_move_to(cr, x1, te->stepper_draw_size   );
    cairo_rel_line_to( cr, 0.0, te->stepper_length );
    cairo_stroke(cr);
    if( te->grab_button == 1 && te->current_location == MOUSE_STEPPER )
    {
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_BOLD );

      cairo_move_to(cr, x1 + (te->stepper_size * 0.5),te->font_line );
      gchar text[40];
      sprintf(text, "%d",  (gint)te->frame_num );
      cairo_text_path( cr, text );
      cairo_set_font_size( cr, 0.2 );
      cairo_set_source_rgba( cr, color.red,color.green,color.blue,0.7 );

      cairo_fill(cr);
    }
  }
/* Draw selection */
  if( te->has_selection )
  {
    gdouble in = te->in * width;
    gdouble out = te->out * width;

    /* If user is editing in_point */
    if( te->grab_button == 1 && te->current_location != MOUSE_STEPPER )
    {
      gdouble f = te->in * te->num_video_frames;

      cairo_set_source_rgba( cr, 0.0, color.green, color.blue,0.3 );
      cairo_move_to( cr, in, 0.0 );
      cairo_rel_line_to( cr, 0.0 , te->stepper_length );
      cairo_stroke(cr);

      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_BOLD );

      cairo_move_to(cr, in , te->font_line  );
      gchar text[40];
      sprintf(text, "%d",(gint) f );
      cairo_text_path( cr, text );
      cairo_set_font_size( cr, 0.2 );
      cairo_set_source_rgba( cr, color.red,color.green,color.blue,0.7 );
      cairo_fill(cr);

    }
    if( te->grab_button == 3 && te->current_location != MOUSE_STEPPER )
    {
      gdouble f = te->out * te->num_video_frames;
      cairo_set_source_rgba( cr, 0.0,color.green,color.blue,0.3 );
      cairo_move_to( cr, out , 0.0 );
      cairo_rel_line_to( cr, 0.0 , te->stepper_length );
      cairo_stroke(cr);

      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_BOLD );

      cairo_move_to(cr, out ,te->font_line );
      gchar text[40];
      sprintf(text, "%d", (gint) f );
      cairo_text_path( cr, text );
      cairo_set_font_size( cr, 0.2 );
      cairo_set_source_rgba( cr,color.red,color.green,color.blue,0.7 );
      cairo_fill(cr);
    }

    cairo_set_source_rgba( cr, color.red,color.green,color.blue, 0.3 );
    cairo_rectangle_round(cr, in,
      0.095 * height,
      (out - in),
      marker_height,
      10);
    te->selection.x = in;
    te->selection.y = 0;
    te->selection.width = out;
    te->selection.height = te->font_line;
    cairo_fill_preserve(cr);
  }

  return FALSE;
}

GtkWidget *timeline_new(void)
{
  GtkWidget *widget = GTK_WIDGET( g_object_new( timeline_get_type(), NULL ));
  TimelineSelection *te = TIMELINE_SELECTION( widget );

  gtk_widget_set_size_request(widget, 200,24 );

  gtk_widget_set_events( widget,
      GDK_EXPOSURE_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_POINTER_MOTION_MASK |
      GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
      GDK_BUTTON3_MOTION_MASK | GDK_2BUTTON_PRESS | GDK_SCROLL_MASK );

  g_signal_connect( G_OBJECT(widget), "draw",
                    G_CALLBACK(timeline_draw), NULL );

  g_signal_connect( G_OBJECT(widget), "motion_notify_event",
                    G_CALLBACK(event_motion), NULL );

  g_signal_connect( G_OBJECT(widget), "button_press_event",
                    G_CALLBACK(event_press), NULL );

  g_signal_connect( G_OBJECT(widget), "button_release_event",
                    G_CALLBACK(event_release), NULL );

  g_signal_connect( G_OBJECT(widget), "scroll_event",
                    G_CALLBACK( event_scroll ), NULL );

  te->widget = widget;

  return widget;
}
