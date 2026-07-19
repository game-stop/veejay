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
  - Left click + drag on the track continuously edits IN.
  - Right click on the track sets OUT to the clicked frame.
  - Right click + drag on the track continuously edits OUT.
  - Middle click inside an existing selection toggles bound/scratch movement.
  - In scratch mode, the marker is translated as a rigid block.
  - In scratch mode, left/right edge edits and status in/out echoes are ignored.
  - Double left click clears the local selection.
  - Mouse wheel never cancels scratch/bind mode.
  - Mouse wheel with any button held is ignored.
  - Mouse wheel steps the playhead by the default scroll step.
  - Ctrl + mouse wheel steps the playhead by about one second.
  - Shift + mouse wheel steps the playhead by about two seconds.
  - While the middle-click scratch box is active, mouse wheel resizes the marker.
  - The playhead remains clamped to the active selection marker.
 */

/*
 * Modified by d.j.a.y , 2018
 * - gtk3 compliant
 */

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
  AUDIO_OFFSET_CHANGED,
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
  action_audio_offset,
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
  TimelineAction    hover_action;
  gdouble           fps;
  gint              display_mode;
  gint              current_id;
  gint              source_start;
  gint              source_end;
  gint              loop_mode;
  gint              play_speed;
  gboolean          audio_grid_active;
  gboolean          audio_grid_locked;
  gint              audio_bpm_x10;
  gint              audio_phase_pct;
  gint              audio_pulse_pct;
  gint              audio_gate_pct;
  gboolean          audio_lane_active;
  gint              audio_lane_source;
  gint              audio_lane_profile;
  gint              audio_lane_mode;
  gint              audio_lane_video_anchor;
  gint              audio_lane_wav_anchor_ms;
  gint              audio_lane_length_ms;
  gboolean          audio_lane_loop;
  gboolean          audio_lane_loop_override_active;
  gboolean          audio_lane_drag;
  gboolean          audio_lane_drag_anchor;
  gboolean          audio_lane_drag_wav;
  gdouble           audio_lane_drag_zero;
  gint              audio_lane_drag_base_video_anchor;
  gint              audio_lane_drag_base_wav_ms;
  gint              audio_lane_status_hold;
  GdkRectangle      audio_lane_rect;
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
static gint timeline_clamp_playhead_i(TimelineSelection *te, gint frame);
static void timeline_guard_playhead(GtkWidget *widget, gboolean emit_signal);
static gboolean timeline_point_in_audio_lane(TimelineSelection *te, gdouble x, gdouble y);
static gboolean timeline_point_near_audio_anchor(TimelineSelection *te, gdouble x, gdouble width);
static void timeline_audio_lane_apply_at_x(GtkWidget *widget, gdouble x, gdouble width);
static void timeline_audio_lane_emit_changed(GtkWidget *widget);

static void timeline_class_init(TimelineSelectionClass *class);
static void timeline_init(TimelineSelection *te);
static gboolean timeline_draw(GtkWidget *widget, cairo_t *cr);
static void timeline_style_updated(GtkWidget *widget);

static GObjectClass *parent_class = NULL;
static gint timeline_signals[LAST_SIGNAL] = { 0 };

static GtkWidgetClass *timeline_parent_widget_class = NULL;

static double timeline_font_points(GtkWidget *widget)
{
  PangoContext *context;
  const PangoFontDescription *font;
  int size;
  double points;

  if(!widget)
    return 10.0;

  context = gtk_widget_get_pango_context(widget);
  font = context ? pango_context_get_font_description(context) : NULL;
  size = font ? pango_font_description_get_size(font) : 0;
  if(size <= 0)
    return 10.0;

  points = (double)size / PANGO_SCALE;
  if(pango_font_description_get_size_is_absolute(font))
    points *= 72.0 / 96.0;
  return CLAMP(points, 6.0, 32.0);
}

static double timeline_font_px(GtkWidget *widget, double scale)
{
  return MAX(7.0,
             timeline_font_points(widget) *
             (96.0 / 72.0) * scale);
}

static const char *timeline_font_family(GtkWidget *widget)
{
  PangoContext *context;
  const PangoFontDescription *font;
  const char *family;

  if(!widget)
    return "Sans";

  context = gtk_widget_get_pango_context(widget);
  font = context ? pango_context_get_font_description(context) : NULL;
  family = font ? pango_font_description_get_family(font) : NULL;
  return (family && family[0]) ? family : "Sans";
}

static double timeline_label_font_size(TimelineSelection *te)
{
  return timeline_font_px(te ? te->widget : NULL, 0.90);
}

static double timeline_info_font_size(TimelineSelection *te)
{
  return timeline_font_px(te ? te->widget : NULL, 0.86);
}

static double timeline_ruler_font_size(TimelineSelection *te)
{
  return timeline_font_px(te ? te->widget : NULL, 0.82);
}

static int timeline_min_height(GtkWidget *widget)
{
  double delta = MAX(0.0, timeline_font_px(widget, 0.90) - 12.0);
  return MAX(128,
             (int)ceil(128.0 + delta * 3.0));
}

static void timeline_update_font_metrics(TimelineSelection *te)
{
  double label_size;

  if(!te || !te->widget)
    return;

  label_size = timeline_label_font_size(te);
  te->frame_height = MAX(14.0, ceil(label_size * 1.15));
  te->font_line = MAX(24.0, ceil(label_size * 2.0));
  gtk_widget_set_size_request(te->widget,
                              200,
                              timeline_min_height(te->widget));
}

#define TIMELINE_STEPPER_HIT_PX     28.0
#define TIMELINE_SELECTION_HIT_PX   8.0
#define TIMELINE_MIN_FRAMES         1.0
#define TIMELINE_DEFAULT_SCROLL_STEP_FRAMES 13
#define TIMELINE_CTRL_SCROLL_STEP_FRAMES    25
#define TIMELINE_SHIFT_SCROLL_STEP_FRAMES   50
#define TIMELINE_SCRATCH_SCROLL_EDGE_STEP_FRAMES 5
#define TIMELINE_BEAT_GRID_MIN_PX   10.0

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
        case action_audio_offset: return "audio";
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

static gboolean timeline_active_selection_bounds(TimelineSelection *te, gint *in_f, gint *out_f)
{
    gint i;
    gint o;

    if (!te->has_selection)
        return FALSE;

    i = timeline_clamp_frame_i(te, (gint) llround(te->in));
    o = timeline_clamp_frame_i(te, (gint) llround(te->out));

    if (o < i)
        return FALSE;

    if (te->clear && i == 0 && o <= 1)
        return FALSE;

    if (in_f)
        *in_f = i;
    if (out_f)
        *out_f = o;

    return TRUE;
}

static gint timeline_clamp_playhead_i(TimelineSelection *te, gint frame)
{
    gint in_f;
    gint out_f;

    frame = timeline_clamp_frame_i(te, frame);

    if (timeline_active_selection_bounds(te, &in_f, &out_f)) {
        if (frame < in_f)
            frame = in_f;
        else if (frame > out_f)
            frame = out_f;
    }

    return frame;
}

static void timeline_guard_playhead(GtkWidget *widget, gboolean emit_signal)
{
    TimelineSelection *te = TIMELINE_SELECTION(widget);
    gint old_pos = timeline_clamp_frame_i(te, (gint) llround(te->frame_num));
    gint new_pos = timeline_clamp_playhead_i(te, old_pos);

    if (new_pos == old_pos)
        return;

    te->frame_num = (gdouble) new_pos;

    TL_EVT(te, "guard-playhead", "old=%d new=%d in=%.3f out=%.3f selection=%d clear=%d",
           old_pos, new_pos, te->in, te->out, te->has_selection, te->clear);

    if (emit_signal)
        g_signal_emit(te->widget, timeline_signals[POS_CHANGED], 0);

    gtk_widget_queue_draw(widget);
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

    if (te->has_selection && te->out >= te->in && width > 1.0) {
        gdouble in_f = timeline_clamp_frame(te, te->in);
        gdouble out_f = timeline_clamp_frame(te, te->out);
        gdouble in_x = timeline_snap_fill_x(timeline_frame_boundary_to_x(te, in_f, width), width);
        gdouble out_x = timeline_snap_fill_x(timeline_frame_boundary_to_x(te, out_f + 1.0, width), width);

        (void) y;

        if (out_x < in_x) {
            gdouble tmp = in_x;
            in_x = out_x;
            out_x = tmp;
        }

        if (timeline_point_near_x(x, in_x, TIMELINE_SELECTION_HIT_PX))
            return action_in_point;

        if (timeline_point_near_x(x, out_x, TIMELINE_SELECTION_HIT_PX))
            return action_out_point;

        if (x >= in_x && x <= out_x)
            return action_atomic;
    }

    return action_point;
}

struct _TimelineSelectionClass
{
  GtkWidgetClass parent_class;
  void (*pos_changed) (TimelineSelection *te);
  void (*point_changed) (TimelineSelection *te);
  void (*audio_offset_changed) (TimelineSelection *te);
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
      gint v = timeline_clamp_playhead_i(te, (gint) llround(g_value_get_double(value)));
      TL_DBG(te, "set_property(pos): %.3f -> %d", te->frame_num, v);
      te->frame_num = (gdouble) v;
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
      timeline_guard_playhead(GTK_WIDGET(te), FALSE);
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
      timeline_guard_playhead(GTK_WIDGET(te), FALSE);
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
      timeline_guard_playhead(GTK_WIDGET(te), FALSE);
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
  timeline_parent_widget_class = GTK_WIDGET_CLASS(g_type_class_peek_parent(class));
  widget_class->draw = timeline_draw;
  widget_class->style_updated = timeline_style_updated;

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

  timeline_signals[AUDIO_OFFSET_CHANGED] = g_signal_new("audio_offset_changed",
                                                        G_TYPE_FROM_CLASS(gobject_class),
                                                        G_SIGNAL_RUN_LAST,
                                                        G_STRUCT_OFFSET(TimelineSelectionClass, audio_offset_changed),
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


static void timeline_style_updated(GtkWidget *widget)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  if(timeline_parent_widget_class &&
     timeline_parent_widget_class->style_updated)
    timeline_parent_widget_class->style_updated(widget);

  timeline_update_font_metrics(te);
  gtk_widget_queue_resize(widget);
  gtk_widget_queue_draw(widget);
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
  te->step_size = TIMELINE_DEFAULT_SCROLL_STEP_FRAMES;
  te->frame_width = 0.0;
  te->frame_height = 14;
  te->font_line = 24;
  te->has_selection = FALSE;
  te->move_x = 0.0;
  te->point = 0.0;
  te->drag_latched = FALSE;
  te->scratch_span = 0;
  te->has_ghost_selection = FALSE;
  te->ghost_in = 0.0;
  te->ghost_out = 0.0;
  te->hover_action = action_none;
  te->fps = 0.0;
  te->display_mode = 0;
  te->current_id = 0;
  te->source_start = 0;
  te->source_end = 0;
  te->loop_mode = 0;
  te->play_speed = 1;
  te->audio_grid_active = FALSE;
  te->audio_grid_locked = FALSE;
  te->audio_bpm_x10 = 0;
  te->audio_phase_pct = 0;
  te->audio_pulse_pct = 0;
  te->audio_gate_pct = 0;
  te->audio_lane_active = FALSE;
  te->audio_lane_source = 0;
  te->audio_lane_profile = 0;
  te->audio_lane_mode = 0;
  te->audio_lane_video_anchor = 0;
  te->audio_lane_wav_anchor_ms = 0;
  te->audio_lane_length_ms = 0;
  te->audio_lane_loop = FALSE;
  te->audio_lane_loop_override_active = FALSE;
  te->audio_lane_drag = FALSE;
  te->audio_lane_drag_anchor = FALSE;
  te->audio_lane_drag_wav = FALSE;
  te->audio_lane_drag_zero = 0.0;
  te->audio_lane_drag_base_video_anchor = 0;
  te->audio_lane_drag_base_wav_ms = 0;
  te->audio_lane_status_hold = 0;
  te->audio_lane_rect.x = 0;
  te->audio_lane_rect.y = 0;
  te->audio_lane_rect.width = 0;
  te->audio_lane_rect.height = 0;
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
  gdouble raw = pos;
  (void) raw;

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
  timeline_guard_playhead(widget, TRUE);

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
  te->hover_action = action_none;
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
  gdouble raw = pos;
  (void) raw;

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
  timeline_guard_playhead(widget, TRUE);

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
  gdouble raw_start = start;
  gdouble raw_end = end;
  gdouble old_in = te->in;
  gdouble old_out = te->out;
  (void) raw_start;
  (void) raw_end;
  (void) old_in;
  (void) old_out;

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
  timeline_guard_playhead(widget, TRUE);

  TL_EVT(te, "set_in_and_out", "raw=(%.3f,%.3f) snapped=(%.3f,%.3f) old=(%.3f,%.3f) bind=%d action=%s span=%.3f",
         raw_start, raw_end, start, end, old_in, old_out, te->bind, timeline_action_name(te->action), end - start + 1.0);

  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

void timeline_set_selection(GtkWidget *widget, gboolean active)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  gboolean old = te->has_selection;
  (void) old;

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

  timeline_guard_playhead(widget, TRUE);
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
  gint new_pos = timeline_clamp_playhead_i(te, (gint) llround(pos));

  if ((gint)llround(te->frame_num) == new_pos)
      return;

  te->frame_num = (gdouble) new_pos;

  //TL_EVT(te, "set_pos", "raw=%.3f clamped=%d in=%.3f out=%.3f selection=%d", pos, new_pos, te->in, te->out, te->has_selection);
  //TL_EVT(te, "emit", "pos_changed");
  g_signal_emit(te->widget, timeline_signals[POS_CHANGED], 0);
  if (gtk_widget_get_mapped(GTK_WIDGET(te->widget)))
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

static inline gdouble timeline_audio_fps(TimelineSelection *te)
{
  return (te && te->fps > 0.0) ? te->fps : 25.0;
}

static inline gdouble timeline_audio_ms_to_frames(TimelineSelection *te, gint ms)
{
  if (ms <= 0)
      return 0.0;
  return ((gdouble) ms * timeline_audio_fps(te)) / 1000.0;
}

static inline gint timeline_audio_frames_to_ms(TimelineSelection *te, gdouble frames)
{
  gdouble fps = timeline_audio_fps(te);
  gint ms;

  if (fps <= 0.0)
      fps = 25.0;

  ms = (gint) llround((frames * 1000.0) / fps);
  return MAX(0, ms);
}

static inline gdouble timeline_audio_zero_frame(TimelineSelection *te)
{
  return (gdouble) te->audio_lane_video_anchor -
         timeline_audio_ms_to_frames(te, te->audio_lane_wav_anchor_ms);
}

static gboolean timeline_point_in_audio_lane(TimelineSelection *te, gdouble x, gdouble y)
{
  if (!te || !te->audio_lane_active)
      return FALSE;

  const gdouble x0 = (gdouble) te->audio_lane_rect.x;
  const gdouble x1 = (gdouble) (te->audio_lane_rect.x + te->audio_lane_rect.width);
  const gdouble y0 = (gdouble) te->audio_lane_rect.y - 8.0;
  const gdouble y1 = (gdouble) (te->audio_lane_rect.y + te->audio_lane_rect.height) + 18.0;

  return x >= x0 && x < x1 && y >= y0 && y < y1;
}

static gboolean timeline_point_near_audio_anchor(TimelineSelection *te, gdouble x, gdouble width)
{
  if (!te || !te->audio_lane_active || width <= 1.0)
      return FALSE;

  gint nframes = timeline_frame_count_i(te);
  gdouble start_frame = (gdouble) timeline_clamp_frame_i(te, te->audio_lane_video_anchor);
  gdouble start_x = timeline_snap_line_x((start_frame / (gdouble) nframes) * width, width);

  return fabs(x - start_x) <= 16.0;
}
static void timeline_audio_lane_apply_at_x(GtkWidget *widget, gdouble x, gdouble width)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  gdouble mouse_frame;
  gint old_anchor;
  gint old_ms;
  gint wav_ms;
  gint nframes;

  if (!te->audio_lane_active || width <= 1.0)
      return;

  mouse_frame = timeline_x_to_frame_cont(te, x, width);
  old_anchor = te->audio_lane_video_anchor;
  old_ms = te->audio_lane_wav_anchor_ms;
  nframes = timeline_frame_count_i(te);

  if (te->audio_lane_drag_wav) {
      gdouble delta_frames = mouse_frame - te->move_x;
      wav_ms = te->audio_lane_drag_base_wav_ms;

      if (delta_frames >= 0.0)
          wav_ms += timeline_audio_frames_to_ms(te, delta_frames);
      else
          wav_ms -= timeline_audio_frames_to_ms(te, -delta_frames);

      if (wav_ms < 0)
          wav_ms = 0;
      if (te->audio_lane_length_ms > 0 && wav_ms > te->audio_lane_length_ms)
          wav_ms = te->audio_lane_length_ms;

      if (wav_ms == old_ms)
          return;

      te->audio_lane_wav_anchor_ms = wav_ms;
      gtk_widget_queue_draw(GTK_WIDGET(te->widget));
      return;
  }

  gint anchor_i = (gint) llround(mouse_frame - te->move_x);

  if (anchor_i < 0)
      anchor_i = 0;
  if (anchor_i >= nframes)
      anchor_i = nframes - 1;

  anchor_i = timeline_clamp_frame_i(te, anchor_i);

  if (anchor_i == old_anchor)
      return;

  te->audio_lane_video_anchor = anchor_i;
  gtk_widget_queue_draw(GTK_WIDGET(te->widget));
}

static void timeline_audio_lane_emit_changed(GtkWidget *widget)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  if (!te->audio_lane_active)
      return;

  te->audio_lane_status_hold = 50;
  g_signal_emit(te->widget, timeline_signals[AUDIO_OFFSET_CHANGED], 0);
}

gint timeline_get_audio_lane_wav_anchor_ms(TimelineSelection *te)
{
  return te ? MAX(0, te->audio_lane_wav_anchor_ms) : 0;
}

gint timeline_get_audio_lane_video_anchor_frame(TimelineSelection *te)
{
  return te ? timeline_clamp_frame_i(te, te->audio_lane_video_anchor) : 0;
}

void timeline_set_audio_lane_loop(GtkWidget *widget, gboolean loop)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  gboolean next = loop ? TRUE : FALSE;

  if (!te || !te->audio_lane_active || te->audio_lane_source != 1)
      return;

  te->audio_lane_loop_override_active = TRUE;

  if (te->audio_lane_loop == next)
      return;

  te->audio_lane_loop = next;
  if (gtk_widget_get_mapped(widget))
      gtk_widget_queue_draw(widget);
}

void timeline_clear_audio_lane(GtkWidget *widget)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  if (te->audio_lane_drag || te->action == action_audio_offset)
      return;

  if (te->audio_lane_status_hold > 0 && te->audio_lane_active) {
      te->audio_lane_status_hold--;
      return;
  }

  if (!te->audio_lane_active &&
      te->audio_lane_source == 0 &&
      te->audio_lane_profile == 0 &&
      te->audio_lane_mode == 0 &&
      te->audio_lane_video_anchor == 0 &&
      te->audio_lane_wav_anchor_ms == 0 &&
      te->audio_lane_length_ms == 0 &&
      !te->audio_lane_loop &&
      !te->audio_lane_loop_override_active &&
      !te->audio_lane_drag &&
      !te->audio_lane_drag_anchor &&
      !te->audio_lane_drag_wav &&
      te->audio_lane_status_hold == 0)
      return;

  te->audio_lane_active = FALSE;
  te->audio_lane_source = 0;
  te->audio_lane_profile = 0;
  te->audio_lane_mode = 0;
  te->audio_lane_video_anchor = 0;
  te->audio_lane_wav_anchor_ms = 0;
  te->audio_lane_length_ms = 0;
  te->audio_lane_loop = FALSE;
  te->audio_lane_loop_override_active = FALSE;
  te->audio_lane_drag = FALSE;
  te->audio_lane_drag_anchor = FALSE;
  te->audio_lane_drag_wav = FALSE;
  te->audio_lane_drag_zero = 0.0;
  te->audio_lane_drag_base_video_anchor = 0;
  te->audio_lane_drag_base_wav_ms = 0;
  te->audio_lane_status_hold = 0;
  te->audio_lane_rect.x = 0;
  te->audio_lane_rect.y = 0;
  te->audio_lane_rect.width = 0;
  te->audio_lane_rect.height = 0;
  if (gtk_widget_get_mapped(widget))
      gtk_widget_queue_draw(widget);
}

void timeline_set_audio_lane(GtkWidget *widget,
                             gboolean active,
                             gint source,
                             gint profile,
                             gint mode,
                             gint video_anchor_frame,
                             gint wav_anchor_ms,
                             gint length_ms,
                             gboolean loop)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  gint max_frame = MAX(0, timeline_frame_count_i(te) - 1);
  gboolean incoming_loop = loop ? TRUE : FALSE;
  gboolean same_identity;

  if (te->audio_lane_drag || te->action == action_audio_offset)
      return;

  video_anchor_frame = CLAMP(video_anchor_frame, 0, max_frame);
  wav_anchor_ms = MAX(0, wav_anchor_ms);
  length_ms = MAX(0, length_ms);

  same_identity = te->audio_lane_active &&
                  active &&
                  source == te->audio_lane_source &&
                  profile == te->audio_lane_profile &&
                  mode == te->audio_lane_mode;

  if (te->audio_lane_loop_override_active) {
      if (!same_identity || source != 1)
          te->audio_lane_loop_override_active = FALSE;
      else
          incoming_loop = te->audio_lane_loop;
  }

  if (te->audio_lane_status_hold > 0) {
      gboolean same_binding = te->audio_lane_active &&
                             source == te->audio_lane_source &&
                             profile == te->audio_lane_profile &&
                             mode == te->audio_lane_mode;
      gboolean same_mapping = same_binding &&
                             active && source > 0 &&
                             te->audio_lane_video_anchor == video_anchor_frame &&
                             te->audio_lane_wav_anchor_ms == wav_anchor_ms &&
                             te->audio_lane_length_ms == length_ms &&
                             te->audio_lane_loop == incoming_loop;

      if (same_mapping)
          te->audio_lane_status_hold = 0;
      else if (same_binding || !active || source <= 0) {
          te->audio_lane_status_hold--;
          return;
      }
      else
          te->audio_lane_status_hold = 0;
  }

  if (!active || source <= 0) {
      timeline_clear_audio_lane(widget);
      return;
  }

  if (te->audio_lane_active == TRUE &&
      te->audio_lane_source == source &&
      te->audio_lane_profile == profile &&
      te->audio_lane_mode == mode &&
      te->audio_lane_video_anchor == video_anchor_frame &&
      te->audio_lane_wav_anchor_ms == wav_anchor_ms &&
      te->audio_lane_length_ms == length_ms &&
      te->audio_lane_loop == incoming_loop)
  {
      return;
  }

  te->audio_lane_active = TRUE;
  te->audio_lane_source = source;
  te->audio_lane_profile = profile;
  te->audio_lane_mode = mode;
  te->audio_lane_video_anchor = video_anchor_frame;
  te->audio_lane_wav_anchor_ms = wav_anchor_ms;
  te->audio_lane_length_ms = length_ms;
  te->audio_lane_loop = incoming_loop;
  gtk_widget_queue_draw(widget);
}

void timeline_set_display_info_full(GtkWidget *widget,
                                    gint display_mode,
                                    gint current_id,
                                    gint source_start,
                                    gint source_end,
                                    gint loop_mode,
                                    gint play_speed,
                                    gdouble fps)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  gint nframes;

  if (!te)
      return;

  nframes = timeline_frame_count_i(te);

  if (source_end < source_start)
      source_end = source_start + nframes - 1;

  if (fps < 0.0)
      fps = 0.0;

  if (te->display_mode == display_mode &&
      te->current_id == current_id &&
      te->source_start == source_start &&
      te->source_end == source_end &&
      te->loop_mode == loop_mode &&
      te->play_speed == play_speed &&
      fabs(te->fps - fps) < 0.0001)
  {
      return;
  }

  te->display_mode = display_mode;
  te->current_id = current_id;
  te->source_start = source_start;
  te->source_end = source_end;
  te->loop_mode = loop_mode;
  te->play_speed = play_speed;
  te->fps = fps;

  gtk_widget_queue_draw(widget);
}

void timeline_set_display_info(GtkWidget *widget,
                               gint display_mode,
                               gint source_start,
                               gint source_end,
                               gint loop_mode,
                               gdouble fps)
{
  timeline_set_display_info_full(widget,
                                 display_mode,
                                 0,
                                 source_start,
                                 source_end,
                                 loop_mode,
                                 1,
                                 fps);
}

void timeline_set_audio_grid(GtkWidget *widget,
                             gboolean active,
                             gboolean locked,
                             gint bpm_x10,
                             gint phase_pct,
                             gint pulse_pct,
                             gint gate_pct)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  if (!te)
      return;

  if (bpm_x10 <= 0)
      active = FALSE;

  phase_pct = CLAMP(phase_pct, 0, 100);
  pulse_pct = CLAMP(pulse_pct, 0, 100);
  gate_pct = CLAMP(gate_pct, 0, 100);

  if (te->audio_grid_active == active &&
      te->audio_grid_locked == locked &&
      te->audio_bpm_x10 == bpm_x10 &&
      te->audio_phase_pct == phase_pct &&
      te->audio_pulse_pct == pulse_pct &&
      te->audio_gate_pct == gate_pct)
  {
      return;
  }

  te->audio_grid_active = active ? TRUE : FALSE;
  te->audio_grid_locked = locked ? TRUE : FALSE;
  te->audio_bpm_x10 = bpm_x10;
  te->audio_phase_pct = phase_pct;
  te->audio_pulse_pct = pulse_pct;
  te->audio_gate_pct = gate_pct;

  gtk_widget_queue_draw(widget);
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
  timeline_guard_playhead(widget, TRUE);

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

static void resize_scratch_selection(GtkWidget *widget,
                                     gint direction,
                                     gdouble mouse_x,
                                     gdouble width)
{
  TimelineSelection *te = TIMELINE_SELECTION(widget);
  gint old_in;
  gint old_out;
  gint new_in;
  gint new_out;
  gint nframes;
  gint span;
  gint center;

  if (!timeline_latch_visible(te) ||
      !timeline_active_selection_bounds(te, &old_in, &old_out))
      return;

  nframes = timeline_frame_count_i(te);

  if (direction > 0) {
      new_in = old_in - TIMELINE_SCRATCH_SCROLL_EDGE_STEP_FRAMES;
      new_out = old_out + TIMELINE_SCRATCH_SCROLL_EDGE_STEP_FRAMES;

      if (new_in < 0)
          new_in = 0;
      if (new_out >= nframes)
          new_out = nframes - 1;
  }
  else {
      new_in = old_in + TIMELINE_SCRATCH_SCROLL_EDGE_STEP_FRAMES;
      new_out = old_out - TIMELINE_SCRATCH_SCROLL_EDGE_STEP_FRAMES;

      if (new_in > new_out) {
          center = (old_in + old_out) / 2;
          new_in = center;
          new_out = center;
      }
  }

  new_in = timeline_clamp_frame_i(te, new_in);
  new_out = timeline_clamp_frame_i(te, new_out);

  if (new_out < new_in)
      new_out = new_in;

  if (new_in == old_in && new_out == old_out)
      return;

  te->in = (gdouble) new_in;
  te->out = (gdouble) new_out;
  te->has_selection = TRUE;
  te->clear = FALSE;

  span = new_out - new_in + 1;
  te->scratch_span = span;

  if (width > 1.0) {
      gdouble mouse_frame = timeline_x_to_frame_cont(te, mouse_x, width);
      te->move_x = mouse_frame - (gdouble) new_in;
  }

  timeline_guard_playhead(widget, TRUE);

  TL_EVT(te, "resize-scratch",
         "direction=%d old=(%d,%d) new=(%d,%d) span=%d move_x=%.3f",
         direction, old_in, old_out, new_in, new_out, span, te->move_x);

  g_signal_emit(te->widget, timeline_signals[IN_CHANGED], 0);
  g_signal_emit(te->widget, timeline_signals[OUT_CHANGED], 0);

  gtk_widget_queue_draw(widget);
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
    te->hover_action = action_none;
    te->audio_lane_drag = FALSE;
    te->audio_lane_drag_anchor = FALSE;
    te->audio_lane_drag_wav = FALSE;
    te->audio_lane_drag_base_video_anchor = 0;
    te->audio_lane_drag_base_wav_ms = 0;
    timeline_clear_selection_ghost(te);

    timeline_update_cursor(widget, action_none);
    gtk_widget_queue_draw(widget);
}

static gint timeline_scroll_step(GdkEventScroll *ev)
{
    GdkScrollDirection direction;

    if (!gdk_event_get_scroll_direction((GdkEvent *) ev, &direction))
        return 0;

    switch (direction) {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_RIGHT:
            return 1;

        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_LEFT:
            return -1;

        case GDK_SCROLL_SMOOTH: {
            gdouble dx = 0.0;
            gdouble dy = 0.0;

            if (!gdk_event_get_scroll_deltas((GdkEvent *) ev, &dx, &dy))
                return 0;

            if (fabs(dy) >= fabs(dx)) {
                if (dy < 0.0) return 1;
                if (dy > 0.0) return -1;
            }
            else {
                if (dx > 0.0) return 1;
                if (dx < 0.0) return -1;
            }

            return 0;
        }

        default:
            break;
    }

    return 0;
}

static gint timeline_scroll_frame_step(TimelineSelection *te, GdkEventScroll *ev)
{
    gint step;

    if (ev->state & GDK_SHIFT_MASK)
        step = TIMELINE_SHIFT_SCROLL_STEP_FRAMES;
    else if (ev->state & GDK_CONTROL_MASK)
        step = TIMELINE_CTRL_SCROLL_STEP_FRAMES;
    else
        step = MAX(1, te->step_size);

    return step;
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

        case action_in_point:
            timeline_set_in_point(widget, timeline_x_to_marker_frame(te, x, width));
            break;

        case action_out_point:
            timeline_set_out_point(widget, timeline_x_to_marker_frame(te, x, width));
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
  GtkAllocation all;
  gint direction;
  gint step;

  (void) user_data;

  TL_EVT(te, "scroll", "bind=%d action=%s drag_latched=%d state=0x%x",
         te->bind, timeline_action_name(te->action), te->drag_latched, ev->state);

  direction = timeline_scroll_step(ev);
  if (direction == 0)
      return TRUE;

  if (timeline_point_in_audio_lane(te, ev->x, ev->y)) {
      gint old_ms = te->audio_lane_wav_anchor_ms;
      gint step_ms = 100;

      if (ev->state & GDK_SHIFT_MASK)
          step_ms = 20;
      else if (ev->state & GDK_CONTROL_MASK)
          step_ms = 1000;

      te->audio_lane_wav_anchor_ms += direction * step_ms;
      if (te->audio_lane_wav_anchor_ms < 0)
          te->audio_lane_wav_anchor_ms = 0;
      if (te->audio_lane_length_ms > 0 && te->audio_lane_wav_anchor_ms > te->audio_lane_length_ms)
          te->audio_lane_wav_anchor_ms = te->audio_lane_length_ms;

      if (te->audio_lane_wav_anchor_ms != old_ms)
          timeline_audio_lane_emit_changed(widget);

      gtk_widget_queue_draw(widget);
      return TRUE;
  }

  if (ev->state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)) {
      TL_EVT(te, "scroll", "ignored while mouse button is held");
      return TRUE;
  }

  if (timeline_latch_visible(te)) {
      gtk_widget_get_allocation(widget, &all);
      resize_scratch_selection(widget, direction, ev->x, (gdouble) all.width);
      return TRUE;
  }

  step = direction * timeline_scroll_frame_step(te, ev);

  TL_EVT(te, "scroll-playhead", "direction=%d step=%d ctrl=%d shift=%d",
         direction, step,
         (ev->state & GDK_CONTROL_MASK) != 0,
         (ev->state & GDK_SHIFT_MASK) != 0);

  timeline_set_pos(widget, timeline_get_pos(te) + (gdouble) step);
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

  if (ev->button == 1 && timeline_point_in_audio_lane(te, ev->x, ev->y)) {
      gboolean near_anchor;

      if (te->bind || te->drag_latched)
          timeline_cancel_drag_mode(widget);

      near_anchor = timeline_point_near_audio_anchor(te, ev->x, width);

      te->grab_button = 1;
      te->current_location = MOUSE_WIDGET;
      te->action = action_audio_offset;
      te->audio_lane_drag = TRUE;
      te->audio_lane_drag_anchor = near_anchor ? TRUE : FALSE;
      te->audio_lane_drag_wav = (ev->state & GDK_SHIFT_MASK) ? TRUE : FALSE;
      te->audio_lane_drag_base_video_anchor = te->audio_lane_video_anchor;
      te->audio_lane_drag_base_wav_ms = te->audio_lane_wav_anchor_ms;
      te->drag_latched = FALSE;
      te->move_x = timeline_x_to_frame_cont(te, ev->x, width);

      if (!te->audio_lane_drag_wav)
          te->move_x = near_anchor ? te->move_x - (gdouble) te->audio_lane_video_anchor : 0.0;

      timeline_audio_lane_apply_at_x(widget, ev->x, width);
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

  if (te->action == action_audio_offset) {
      te->audio_lane_drag = FALSE;
      te->audio_lane_drag_anchor = FALSE;
      te->audio_lane_drag_wav = FALSE;
      te->grab_button = 0;
      timeline_audio_lane_emit_changed(widget);
      te->action = action_none;
      te->current_location = MOUSE_WIDGET;
      timeline_update_cursor(widget, action_none);
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

  if (te->action == action_audio_offset) {
    timeline_audio_lane_apply_at_x(widget, ev->x, width);
    return TRUE;
  }

  if (b1 && te->action == action_pos) {
    timeline_apply_action_at_x(widget, action_pos, ev->x, width);
    return TRUE;
  }

  if (b1 && te->action == action_in_point) {
    timeline_apply_action_at_x(widget, action_in_point, ev->x, width);
    timeline_set_point(widget, timeline_x_to_pos(te, ev->x, width));
    return TRUE;
  }

  if (b3 && te->action == action_out_point) {
    timeline_apply_action_at_x(widget, action_out_point, ev->x, width);
    timeline_set_point(widget, timeline_x_to_pos(te, ev->x, width));
    return TRUE;
  }

  if (!b1 && !b2 && !b3) {
    TimelineAction hover = timeline_pick_action(te, ev->x, ev->y, width);

    te->current_location = MOUSE_WIDGET;
    te->hover_action = hover;
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
    te->hover_action = action_none;

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

static const gchar *timeline_loop_label(gint loop_mode)
{

    switch (loop_mode) {
        case 1: return "loop";
        case 2: return "pingpong";
        case 3: return "random";
        case 4: return "once";
        case 0:
        default: return "";
    }
}

static const gchar *timeline_hover_hint(TimelineSelection *te)
{
    if (timeline_latch_visible(te))
        return "Grabbed loop: move mouse | wheel resize | middle-click release";

    switch (te->hover_action) {
        case action_in_point:  return "Drag IN";
        case action_out_point: return "Drag OUT";
        case action_atomic:    return "Middle-click: grab loop";
        case action_pos:       return "Drag PLAY";
        case action_audio_offset: return "Audio lane: drag switch | Shift-drag/wheel WAV time";
        case action_point:
            return "Wheel: step playhead | Ctrl: 1s | Shift: 2s";
        case action_none:
        default:
            return "";
    }
}

static void timeline_format_clock(TimelineSelection *te,
                                  gint frame,
                                  gchar *buf,
                                  size_t len)
{
    gint fps_i;
    gint total_seconds;
    gint h;
    gint m;
    gint s;
    gint f;

    if (!buf || len == 0)
        return;

    if (te->fps <= 0.01) {
        snprintf(buf, len, "%d", frame);
        return;
    }

    fps_i = (gint) lrint(te->fps);
    fps_i = CLAMP(fps_i, 1, 240);

    if (frame < 0)
        frame = 0;

    total_seconds = frame / fps_i;
    f = frame - (total_seconds * fps_i);
    h = total_seconds / 3600;
    m = (total_seconds / 60) % 60;
    s = total_seconds % 60;

    snprintf(buf, len, "%d:%02d:%02d:%02d", h, m, s, f);
}
static void timeline_format_ms(gint ms, gchar *buf, size_t len)
{
    gint total_seconds;
    gint h;
    gint m;
    gint s;
    gint rem;

    if (!buf || len == 0)
        return;

    if (ms < 0)
        ms = 0;

    total_seconds = ms / 1000;
    rem = ms % 1000;
    h = total_seconds / 3600;
    m = (total_seconds / 60) % 60;
    s = total_seconds % 60;

    if (h > 0)
        snprintf(buf, len, "%d:%02d:%02d.%03d", h, m, s, rem);
    else
        snprintf(buf, len, "%02d:%02d.%03d", m, s, rem);
}


static void timeline_draw_plain_text(cairo_t *cr,
                                     const gchar *text,
                                     gdouble x,
                                     gdouble y,
                                     const GdkRGBA *fg,
                                     gdouble alpha)
{
    if (!text || !*text)
        return;

    cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, alpha);
    cairo_move_to(cr, floor(x), floor(y));
    cairo_show_text(cr, text);
}


static void timeline_draw_right_text(cairo_t *cr,
                                     const gchar *text,
                                     gdouble right_x,
                                     gdouble y,
                                     const GdkRGBA *fg,
                                     gdouble alpha)
{
    cairo_text_extents_t ext;

    if (!text || !*text)
        return;

    cairo_text_extents(cr, text, &ext);
    timeline_draw_plain_text(cr,
                             text,
                             right_x - ext.width - ext.x_bearing,
                             y,
                             fg,
                             alpha);
}

static const gchar *timeline_mode_label(gint display_mode)
{
    switch (display_mode) {
        case 0: return "SAMPLE";
        case 1: return "STREAM";
        case 2: return "PLAIN";
        default: return "SRC";
    }
}

static void timeline_draw_beat_grid(TimelineSelection *te,
                                    cairo_t *cr,
                                    gdouble width,
                                    gdouble track_y,
                                    gdouble track_h,
                                    const GdkRGBA *fg)
{
    gdouble beat_frames;
    gdouble beat_px;
    gdouble stride_frames;
    gdouble phase;
    gdouble first;
    gdouble f;
    gdouble ruler_y;
    gdouble tick_top;
    gdouble tick_mid;
    gdouble tick_deep;
    gint nframes;
    gint guard;
    gint stride_beats = 1;

    if (!te->audio_grid_active || te->audio_bpm_x10 <= 0 || te->fps <= 0.01)
        return;

    nframes = timeline_frame_count_i(te);
    if (nframes <= 1 || width < 24.0)
        return;

    beat_frames = (te->fps * 600.0) / (gdouble) te->audio_bpm_x10;
    if (beat_frames < 1.0)
        return;

    beat_px = (beat_frames / (gdouble) nframes) * width;

    while (beat_px > 0.0 && beat_px < TIMELINE_BEAT_GRID_MIN_PX) {
        stride_beats++;
        beat_px = ((beat_frames * (gdouble) stride_beats) / (gdouble) nframes) * width;

        if ((beat_frames * (gdouble) stride_beats) > (gdouble) nframes)
            break;
    }

    stride_frames = beat_frames * (gdouble) stride_beats;

    phase = ((gdouble) CLAMP(te->audio_phase_pct, 0, 100)) / 100.0;
    first = -phase * beat_frames;

    guard = 0;
    while (first < 0.0 && guard++ < 8192)
        first += stride_frames;

    ruler_y = floor(track_y + 2.0) + 0.5;
    tick_top = ruler_y;
    tick_mid = floor(track_y + MAX(5.0, track_h * 0.38)) + 0.5;
    tick_deep = floor(track_y + MAX(7.0, track_h * 0.62)) + 0.5;

    cairo_save(cr);

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue,
                          te->audio_grid_locked ? 0.16 : 0.08);
    cairo_move_to(cr, 0.0, ruler_y);
    cairo_line_to(cr, width, ruler_y);
    cairo_stroke(cr);

    for (f = first; f < (gdouble) nframes; f += stride_frames) {
        gdouble x = timeline_snap_line_x(timeline_frame_boundary_to_x(te, f, width), width);
        gint beat_index = (gint) floor((f / beat_frames) + 0.5);
        gboolean downbeat = ((beat_index % 4) == 0);
        gdouble tick_bottom = downbeat ? tick_deep : tick_mid;
        gdouble alpha = downbeat ? 0.30 : 0.18;

        if (!te->audio_grid_locked)
            alpha *= 0.62;

        cairo_set_line_width(cr, downbeat ? 1.15 : 0.75);
        cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, alpha);
        cairo_move_to(cr, x, tick_top);
        cairo_line_to(cr, x, tick_bottom);
        cairo_stroke(cr);
    }

    if (te->audio_grid_locked && te->has_stepper) {
        gdouble pulse = ((gdouble) MAX(te->audio_pulse_pct, te->audio_gate_pct)) / 100.0;
        gdouble x = timeline_snap_line_x(timeline_frame_to_x(te, te->frame_num, width), width);
        gdouble alpha = 0.24 + pulse * 0.42;

        cairo_set_line_width(cr, 1.0 + pulse * 1.4);
        cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, alpha);
        cairo_move_to(cr, x, tick_top);
        cairo_line_to(cr, x, tick_deep + 1.0);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, 0.26 + pulse * 0.46);
        cairo_arc(cr, x, tick_top, 2.0 + pulse * 2.0, 0.0, M_PI * 2.0);
        cairo_fill(cr);
    }

    cairo_restore(cr);
}

static gint timeline_frame_ruler_step(gint nframes, gdouble width)
{
    gint max_labels;
    gint target;
    gint pow10 = 1;
    gint base;
    gint step;

    if (nframes <= 1)
        return 1;

    /* Small clips are where this matters most: show 1,5,10,15,20 for 20f. */
    if (nframes <= 30 && width >= 170.0)
        return 5;

    max_labels = (gint) floor(width / 56.0);
    if (max_labels < 2)
        max_labels = 2;

    target = (nframes + max_labels - 2) / (max_labels - 1);
    if (target < 1)
        target = 1;

    while ((pow10 * 10) < target)
        pow10 *= 10;

    base = (target + pow10 - 1) / pow10;

    if (base <= 1)
        step = 1 * pow10;
    else if (base <= 2)
        step = 2 * pow10;
    else if (base <= 5)
        step = 5 * pow10;
    else
        step = 10 * pow10;

    return MAX(1, step);
}

static gboolean timeline_frame_ruler_label_bounds(TimelineSelection *te,
                                                   cairo_t *cr,
                                                   gint frame_ord,
                                                   gint nframes,
                                                   gdouble width,
                                                   gdouble *left,
                                                   gdouble *right,
                                                   gdouble *x_pos)
{
    gchar label[32];
    cairo_text_extents_t ext;
    gdouble x;
    gdouble tx;

    if (frame_ord < 1 || frame_ord > nframes)
        return FALSE;

    snprintf(label, sizeof(label), "%d", frame_ord);

    x = timeline_snap_line_x(timeline_frame_to_x(te, (gdouble) (frame_ord - 1), width), width);

    cairo_text_extents(cr, label, &ext);
    tx = x - (ext.width * 0.5) - ext.x_bearing;

    if (tx < 2.0)
        tx = 2.0;
    if ((tx + ext.width) > (width - 2.0))
        tx = width - 2.0 - ext.width;

    if (left)
        *left = tx;
    if (right)
        *right = tx + ext.width;
    if (x_pos)
        *x_pos = x;

    return TRUE;
}

static void timeline_draw_frame_ruler_label(TimelineSelection *te,
                                            cairo_t *cr,
                                            gint frame_ord,
                                            gint nframes,
                                            gdouble width,
                                            gdouble baseline_y,
                                            gdouble tick_top,
                                            gdouble tick_bottom,
                                            gdouble *last_right,
                                            gboolean force,
                                            const GdkRGBA *fg)
{
    gchar label[32];
    gdouble x;
    gdouble tx;
    gdouble right;
    gdouble alpha;

    if (!timeline_frame_ruler_label_bounds(te, cr, frame_ord, nframes, width, &tx, &right, &x))
        return;

    if (!force && last_right && tx < (*last_right + 5.0))
        return;

    snprintf(label, sizeof(label), "%d", frame_ord);

    alpha = force ? 0.56 : 0.42;

    cairo_set_line_width(cr, force ? 1.0 : 0.75);
    cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, force ? 0.24 : 0.16);
    cairo_move_to(cr, x, tick_top);
    cairo_line_to(cr, x, tick_bottom);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, alpha);
    cairo_move_to(cr, floor(tx), floor(baseline_y));
    cairo_show_text(cr, label);

    if (last_right)
        *last_right = right;
}

static void timeline_draw_frame_ruler(TimelineSelection *te,
                                      cairo_t *cr,
                                      gdouble width,
                                      gdouble track_y,
                                      gdouble track_h,
                                      const GdkRGBA *fg)
{
    gint nframes = timeline_frame_count_i(te);
    gint step;
    gint f;
    gdouble baseline_y;
    gdouble tick_top;
    gdouble tick_bottom;
    gdouble last_right = -9999.0;

    if (nframes <= 1 || width < 120.0 || track_h < 10.0)
        return;

    step = timeline_frame_ruler_step(nframes, width);

    baseline_y = floor(track_y + track_h - 3.0);
    tick_bottom = floor(track_y + track_h - 2.0) + 0.5;
    tick_top = MAX(track_y + 2.5, tick_bottom - 8.0);

    cairo_save(cr);
    cairo_select_font_face(cr,
                           timeline_font_family(te->widget),
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, timeline_ruler_font_size(te));

    /* Human-facing local frame ruler: 1..N, not the internal 0..N-1 indices.
     * The final label is always shown, so intermediate labels near the right
     * edge must yield to it instead of being overprinted by the forced end. */
    gdouble end_left = width + 9999.0;
    gdouble end_right = width + 9999.0;

    timeline_frame_ruler_label_bounds(te, cr, nframes, nframes, width, &end_left, &end_right, NULL);

    timeline_draw_frame_ruler_label(te,
                                    cr,
                                    1,
                                    nframes,
                                    width,
                                    baseline_y,
                                    tick_top,
                                    tick_bottom,
                                    &last_right,
                                    TRUE,
                                    fg);

    for (f = step; f < nframes; f += step) {
        gdouble label_left = 0.0;
        gdouble label_right = 0.0;

        if (f == 1)
            continue;

        if (timeline_frame_ruler_label_bounds(te, cr, f, nframes, width, &label_left, &label_right, NULL) &&
            label_right > (end_left - 5.0))
        {
            continue;
        }

        timeline_draw_frame_ruler_label(te,
                                        cr,
                                        f,
                                        nframes,
                                        width,
                                        baseline_y,
                                        tick_top,
                                        tick_bottom,
                                        &last_right,
                                        FALSE,
                                        fg);
    }

    timeline_draw_frame_ruler_label(te,
                                    cr,
                                    nframes,
                                    nframes,
                                    width,
                                    baseline_y,
                                    tick_top,
                                    tick_bottom,
                                    &last_right,
                                    TRUE,
                                    fg);

    cairo_restore(cr);
}

static void timeline_format_signed_clock(TimelineSelection *te,
                                         gint frame_delta,
                                         gchar *buf,
                                         size_t len)
{
    gint abs_delta;

    if (!buf || len == 0)
        return;

    buf[0] = '\0';

    if (len < 2)
        return;

    abs_delta = (frame_delta < 0) ? -frame_delta : frame_delta;

    buf[0] = (frame_delta < 0) ? '-' : '+';
    timeline_format_clock(te, abs_delta, buf + 1, len - 1);
}

static void timeline_draw_edit_tooltip(TimelineSelection *te,
                                       cairo_t *cr,
                                       gdouble width,
                                       gdouble track_y,
                                       gdouble track_bottom,
                                       const GdkRGBA *fg,
                                       const GdkRGBA *bg)
{
    gchar text[96];
    gchar tc[32];
    gint frame;
    gint len_frames = 0;
    gdouble x;
    gdouble y;

    if (!te->has_selection)
        return;

    if (!((te->action == action_in_point && te->grab_button == 1) ||
          (te->action == action_out_point && te->grab_button == 3)))
        return;

    if (width < 150.0)
        return;

    if (te->action == action_in_point) {
        frame = timeline_clamp_frame_i(te, (gint) llround(te->in));
        timeline_format_clock(te, frame, tc, sizeof(tc));

        if (width > 285.0)
            snprintf(text, sizeof(text), "IN %d  %s", frame, tc);
        else
            snprintf(text, sizeof(text), "IN %d", frame);

        x = timeline_frame_boundary_to_x(te, (gdouble) frame, width);
    }
    else {
        gint in_f = timeline_clamp_frame_i(te, (gint) llround(te->in));

        frame = timeline_clamp_frame_i(te, (gint) llround(te->out));
        len_frames = MAX(1, frame - in_f + 1);
        timeline_format_clock(te, frame, tc, sizeof(tc));

        if (width > 360.0)
            snprintf(text, sizeof(text), "OUT %d  %s  LEN %d", frame, tc, len_frames);
        else if (width > 250.0)
            snprintf(text, sizeof(text), "OUT %d  LEN %d", frame, len_frames);
        else
            snprintf(text, sizeof(text), "OUT %d", frame);

        x = timeline_frame_boundary_to_x(te, (gdouble) frame + 1.0, width);
    }

    x = timeline_snap_line_x(x, width);

    y = track_y - 6.0;
    if (y < 10.0)
        y = track_bottom - 4.0;

    timeline_draw_text_pill(cr,
                            text,
                            x,
                            y,
                            width,
                            fg,
                            0.95,
                            bg,
                            0.44);
}

static gboolean timeline_draw(GtkWidget *widget, cairo_t *cr)
{
  gchar text[160];
  gchar aux[64];
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
  GdkRGBA dim_fg = color;

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_FAST);
  cairo_select_font_face(cr,
                         timeline_font_family(widget),
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, timeline_label_font_size(te));

  gdouble tri_h = te->has_stepper ? te->stepper_draw_size : 0.0;
  gdouble track_h = marker_height;
  gdouble audio_lane_gap = te->audio_lane_active ? 7.0 : 0.0;
  gdouble audio_lane_h = te->audio_lane_active ? 12.0 : 0.0;
  gdouble audio_lane_extra = te->audio_lane_active ? (audio_lane_gap + audio_lane_h + 4.0) : 0.0;
  gdouble row_gap = 6.0;

  gdouble row_step = MAX(20.0, timeline_label_font_size(te) + 8.0);
  gdouble wav_info_extra = te->audio_lane_active ? row_step : 0.0;
  gdouble label_rows_h = row_gap + (row_step * 2.0) + wav_info_extra + 12.0;
  gdouble group_h = tri_h + track_h + audio_lane_extra + label_rows_h;

  gdouble group_y = floor((height - group_h) * 0.5);
  if (group_y < 0.0)
      group_y = 0.0;

  gdouble tri_y = group_y;
  gdouble track_y = floor(group_y + tri_h);
  gdouble track_bottom = track_y + track_h;
  gdouble audio_lane_y = track_bottom + audio_lane_gap;
  gdouble audio_lane_bottom = audio_lane_y + audio_lane_h;
  gdouble label_base_y = te->audio_lane_active ? audio_lane_bottom : track_bottom;
  gdouble marker_row_y = label_base_y + row_gap + 10.0;
  gdouble bottom_row_y = marker_row_y + row_step;
  gdouble wav_info_row_y = te->audio_lane_active ? bottom_row_y + row_step : bottom_row_y;
  gdouble handle_w = 4.0;

  if (wav_info_row_y > height - 3.0) {
      wav_info_row_y = height - 3.0;
      bottom_row_y = wav_info_row_y - (te->audio_lane_active ? row_step : 0.0);
      marker_row_y = MAX(track_bottom + 12.0, bottom_row_y - row_step);
  }

  if (track_bottom > height) {
      track_y = MAX(0.0, height - track_h - label_rows_h);
      track_bottom = track_y + track_h;
      audio_lane_y = track_bottom + audio_lane_gap;
      audio_lane_bottom = audio_lane_y + audio_lane_h;
      label_base_y = te->audio_lane_active ? audio_lane_bottom : track_bottom;
      tri_y = MAX(0.0, track_y - tri_h);
      marker_row_y = MIN(height - ((te->audio_lane_active ? row_step : 0.0) + row_step) - 3.0, label_base_y + row_gap + 10.0);
      bottom_row_y = MIN(height - (te->audio_lane_active ? row_step : 0.0) - 3.0, marker_row_y + row_step);
      wav_info_row_y = te->audio_lane_active ? MIN(height - 3.0, bottom_row_y + row_step) : bottom_row_y;
  }

  cairo_set_source_rgba(cr, col2.red, col2.green, col2.blue, 0.18);
  cairo_rectangle_round(cr, 0.0, track_y, width, track_h, 10.0);

  timeline_draw_beat_grid(te, cr, width, track_y, track_h, &color);

  if (te->audio_grid_active && te->audio_bpm_x10 > 0 && width > 96.0) {
      gint bpm10 = te->audio_bpm_x10;
      gdouble pill_y = MAX(9.0, track_y - 3.0);

      snprintf(text, sizeof(text), "BPM %d.%d", bpm10 / 10, bpm10 % 10);
      cairo_set_font_size(cr, timeline_info_font_size(te));

      timeline_draw_text_pill(cr,
                              text,
                              width - 42.0,
                              pill_y,
                              width,
                              &info_fg,
                              0.95,
                              &info_bg,
                              te->audio_grid_locked ? 0.72 : 0.42);
      cairo_set_font_size(cr, timeline_label_font_size(te));
  }


  if (te->audio_lane_active) {
      gdouble start_frame = (gdouble) timeline_clamp_frame_i(te, te->audio_lane_video_anchor);
      gdouble start_x = timeline_snap_line_x((start_frame / nframes) * width, width);
      gdouble visible_x = CLAMP(start_x, 0.0, width);
      gdouble lane_end_x = width;
      gdouble fill_w;
      gint display_wav_ms = MAX(0, te->audio_lane_wav_anchor_ms);
      gint remaining_wav_ms = 0;
      gboolean wav_has_finite_end = FALSE;

      if (te->audio_lane_source == 1 && te->audio_lane_length_ms > 0) {
          remaining_wav_ms = te->audio_lane_length_ms - display_wav_ms;
          if (remaining_wav_ms < 0)
              remaining_wav_ms = 0;

          if (!te->audio_lane_loop) {
              gdouble end_frame = start_frame + timeline_audio_ms_to_frames(te, remaining_wav_ms);
              lane_end_x = CLAMP((end_frame / nframes) * width, 0.0, width);
              wav_has_finite_end = TRUE;
          }
      }

      if (lane_end_x < visible_x)
          lane_end_x = visible_x;

      fill_w = MAX(1.0, lane_end_x - visible_x);

      te->audio_lane_rect.x = 0;
      te->audio_lane_rect.y = (gint) floor(audio_lane_y - 8.0);
      te->audio_lane_rect.width = (gint) width;
      te->audio_lane_rect.height = (gint) ceil(audio_lane_h + 20.0);

      cairo_set_source_rgba(cr, 0.05, 0.20, 0.24, 0.28);
      cairo_rectangle_round(cr, 0.0, audio_lane_y, width, audio_lane_h, 6.0);

      if (fill_w > 0.0) {
          cairo_set_source_rgba(cr, 0.08, 0.55, 0.65, te->audio_lane_drag ? 0.92 : 0.68);
          cairo_rectangle_round(cr, visible_x, audio_lane_y, fill_w, audio_lane_h, 6.0);
      }

      if (wav_has_finite_end && lane_end_x < width - 0.5) {
          cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.24);
          cairo_rectangle(cr, lane_end_x, audio_lane_y, width - lane_end_x, audio_lane_h);
          cairo_fill(cr);

          cairo_set_source_rgba(cr, 1.0, 0.78, 0.08, 0.62);
          cairo_set_line_width(cr, 1.0);
          cairo_move_to(cr, timeline_snap_line_x(lane_end_x, width), audio_lane_y - 2.0);
          cairo_line_to(cr, timeline_snap_line_x(lane_end_x, width), audio_lane_bottom + 2.0);
          cairo_stroke(cr);
      }

      if (te->audio_lane_source == 1) {
          gint wav_len_ms = te->audio_lane_length_ms;
          gint step_ms = 1000;
          gint max_ticks = 220;
          gint tick;

          if (wav_len_ms > 0 && wav_len_ms < 5000)
              step_ms = 500;

          for (tick = 0; tick < max_ticks; tick++) {
              gdouble delta_ms = (gdouble) tick * (gdouble) step_ms;
              gdouble wav_pos_ms = (gdouble) display_wav_ms + delta_ms;
              gdouble f = start_frame + (delta_ms * timeline_audio_fps(te)) / 1000.0;
              gdouble x;
              gboolean loop_boundary = FALSE;

              if (f < 0.0)
                  continue;
              if (f > nframes)
                  break;
              if (!te->audio_lane_loop && wav_len_ms > 0 && wav_pos_ms > (gdouble) wav_len_ms + 0.001)
                  break;

              if (wav_len_ms > 0 && te->audio_lane_loop) {
                  gint a = (gint) floor((wav_pos_ms - (gdouble) step_ms) / (gdouble) wav_len_ms);
                  gint b = (gint) floor(wav_pos_ms / (gdouble) wav_len_ms);
                  loop_boundary = tick > 0 && b > a;
              }

              x = timeline_snap_line_x((f / nframes) * width, width);
              cairo_set_line_width(cr, loop_boundary ? 1.3 : ((tick % 5) == 0 ? 1.0 : 0.6));
              cairo_set_source_rgba(cr,
                                    1.0,
                                    1.0,
                                    1.0,
                                    loop_boundary ? 0.44 : (((tick % 5) == 0) ? 0.28 : 0.14));
              cairo_move_to(cr, x, audio_lane_y + 1.0);
              cairo_line_to(cr, x, audio_lane_bottom - 1.0);
              cairo_stroke(cr);
          }
      }

      cairo_set_source_rgba(cr, 1.0, 0.78, 0.08, 0.98);
      cairo_set_line_width(cr, te->audio_lane_drag && !te->audio_lane_drag_wav ? 2.4 : 1.5);
      cairo_move_to(cr, start_x, audio_lane_y - 5.0);
      cairo_line_to(cr, start_x, audio_lane_bottom + 5.0);
      cairo_stroke(cr);

      if (te->audio_lane_drag_wav) {
          cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.75);
          cairo_set_line_width(cr, 1.0);
          cairo_rectangle_round(cr, visible_x + 2.0, audio_lane_y + 2.0, MAX(1.0, fill_w - 4.0), MAX(1.0, audio_lane_h - 4.0), 4.0);
          cairo_stroke(cr);
      }

  }


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

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.18);
    cairo_rectangle_round(cr, ghost_in_x, track_y, ghost_w, track_h, 10.0);

    cairo_save(cr);
    double ghost_dash[] = { 2.0, 2.0 };
    cairo_set_dash(cr, ghost_dash, 2, 0.0);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.38);
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
  te->selection.height = (gint) (bottom_row_y - tri_y + 4.0);

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
    gboolean grabbed = timeline_latch_visible(te);

    cairo_set_source_rgba(cr, col2.red, col2.green, col2.blue, grabbed ? 0.98 : 0.86);
    cairo_rectangle_round(cr, in_x, track_y, sel_w, track_h, 10.0);

    cairo_set_source_rgba(cr, color.red, color.green, color.blue, grabbed ? 0.98 : 0.84);
    cairo_rectangle_round(cr,
                          in_x - (handle_w * 0.5),
                          track_y - 1.0,
                          handle_w,
                          track_h + 2.0,
                          1.5);

    cairo_rectangle_round(cr,
                          out_x - (handle_w * 0.5),
                          track_y - 1.0,
                          handle_w,
                          track_h + 2.0,
                          1.5);

    if (grabbed) {
        gdouble center_x = in_x + (sel_w * 0.5);
        gdouble center_y = track_y + (track_h * 0.5);
        gdouble pulse = ((gdouble) MAX(te->audio_pulse_pct, te->audio_gate_pct)) / 100.0;

        cairo_set_source_rgba(cr, 1.0, 0.78, 0.08, 0.22 + pulse * 0.25);
        cairo_arc(cr, center_x, center_y, 8.0 + pulse * 3.0, 0.0, M_PI * 2.0);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 1.0, 0.90, 0.18, 0.95);
        cairo_arc(cr, center_x, center_y, 3.0, 0.0, M_PI * 2.0);
        cairo_fill(cr);
    }

    {
      gint f1 = timeline_clamp_frame_i(te, (gint) llround(in_f));
      gint f2 = timeline_clamp_frame_i(te, (gint) llround(out_f));
      gint span = MAX(1, f2 - f1 + 1);
      const gchar *loop = timeline_loop_label(te->loop_mode);
      gboolean compact = width < 255.0;

      if (grabbed) {
          gint delta = te->has_ghost_selection ? (f1 - timeline_clamp_frame_i(te, (gint) llround(te->ghost_in))) : 0;
          gchar delta_tc[32];

          timeline_format_signed_clock(te, delta, delta_tc, sizeof(delta_tc));

          if (!compact && delta != 0)
              snprintf(text, sizeof(text), "IN %d   OUT %d   LEN %d   GRAB %+df  %s", f1, f2, span, delta, delta_tc);
          else if (!compact)
              snprintf(text, sizeof(text), "IN %d   OUT %d   LEN %d   GRAB", f1, f2, span);
          else if (delta != 0)
              snprintf(text, sizeof(text), "IN %d  OUT %d  LEN %d  %+df", f1, f2, span, delta);
          else
              snprintf(text, sizeof(text), "IN %d  OUT %d  LEN %d  GRAB", f1, f2, span);
      }
      else if (loop && *loop && !compact)
          snprintf(text, sizeof(text), "IN %d   OUT %d   LEN %d   %s", f1, f2, span, loop);
      else if (!compact)
          snprintf(text, sizeof(text), "IN %d   OUT %d   LEN %d", f1, f2, span);
      else
          snprintf(text, sizeof(text), "IN %d  OUT %d  LEN %d", f1, f2, span);

      timeline_draw_text_pill(cr,
                              text,
                              width * 0.5,
                              marker_row_y,
                              width,
                              &color,
                              0.84,
                              &col2,
                              grabbed ? 0.36 : 0.24);
    }

    te->selection.x = (gint) in_x;
    te->selection.y = (gint) tri_y;
    te->selection.width = (gint) sel_w;
    te->selection.height = (gint) (bottom_row_y - tri_y + 4.0);

    if (grabbed || te->loop_mode > 0) {
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
        if (grabbed)
            cairo_set_dash(cr, dash, 2, 0.0);

        cairo_set_line_width(cr, grabbed ? 2.0 : 1.0);
        cairo_set_source_rgba(cr, 1.0, 0.78, 0.08, grabbed ? 0.98 : 0.35);
        cairo_rectangle_round_stroke(cr,
                                     floor(box_x) + 0.5,
                                     floor(box_y) + 0.5,
                                     floor(box_w),
                                     floor(box_h),
                                     3.0);

        cairo_restore(cr);

    }
  }
  else {

      timeline_draw_plain_text(cr,
                               "IN -   OUT -   LEN -",
                               4.0,
                               marker_row_y,
                               &dim_fg,
                               0.42);
  }



  if (te->has_ghost_selection && te->ghost_out >= te->ghost_in) {
    gdouble ghost_in_f = timeline_clamp_frame(te, te->ghost_in);
    gdouble ghost_out_f = timeline_clamp_frame(te, te->ghost_out);
    gdouble ghost_in_x = timeline_snap_line_x(timeline_frame_boundary_to_x(te, ghost_in_f, width), width);
    gdouble ghost_out_x = timeline_snap_line_x(timeline_frame_boundary_to_x(te, ghost_out_f + 1.0, width), width);

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.42);
    cairo_move_to(cr, ghost_in_x, track_y);
    cairo_line_to(cr, ghost_in_x, track_bottom);
    cairo_move_to(cr, ghost_out_x, track_y);
    cairo_line_to(cr, ghost_out_x, track_bottom);
    cairo_stroke(cr);
  }


  if (te->has_selection)
      timeline_draw_beat_grid(te, cr, width, track_y, track_h, &color);

  timeline_draw_frame_ruler(te, cr, width, track_y, track_h, &color);

  timeline_draw_edit_tooltip(te,
                             cr,
                             width,
                             track_y,
                             track_bottom,
                             &info_fg,
                             &info_bg);

  if (te->has_stepper) {
    gdouble pos_x = timeline_snap_line_x(timeline_frame_to_x(te, te->frame_num, width), width);
    gint pos_f = timeline_clamp_frame_i(te, (gint) llround(te->frame_num));
    gint abs_f = te->source_start + pos_f;
    gdouble pulse = te->audio_grid_locked ? ((gdouble) MAX(te->audio_pulse_pct, te->audio_gate_pct)) / 100.0 : 0.0;

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
                          0.88 + pulse * 0.12);

    cairo_set_line_width(cr, 1.0 + pulse * 1.4);
    cairo_move_to(cr, pos_x, tri_y + tri_h);
    cairo_line_to(cr, pos_x, track_bottom);
    cairo_stroke(cr);

    timeline_format_clock(te, pos_f, aux, sizeof(aux));

    {
        const gchar *state = (te->play_speed == 0) ? "PAUSE" : ((te->play_speed < 0) ? "REV" : "PLAY");

        if (te->source_start > 0 && width > 360.0)
            snprintf(text, sizeof(text), "%s %d/%d  ABS %d  %s", state, pos_f, nframes_i - 1, abs_f, aux);
        else
            snprintf(text, sizeof(text), "%s %d/%d  %s", state, pos_f, nframes_i - 1, aux);
    }

    cairo_set_font_size(cr, timeline_info_font_size(te));
    timeline_draw_plain_text(cr, text, 4.0, bottom_row_y, &color, 0.80 + pulse * 0.15);
    cairo_set_font_size(cr, timeline_label_font_size(te));
  }

  {
      gint src_a = te->source_start;
      gint src_b = te->source_end;
      const gchar *mode = timeline_mode_label(te->display_mode);
      gchar total_tc[32];

      if (src_b < src_a)
          src_b = src_a + nframes_i - 1;

      timeline_format_clock(te, nframes_i, total_tc, sizeof(total_tc));

      if (width > 510.0 && te->current_id > 0)
          snprintf(text, sizeof(text), "%s %d  SRC %d..%d  LEN %d  TOTAL %s", mode, te->current_id, src_a, src_b, nframes_i, total_tc);
      else if (width > 390.0 && te->current_id > 0)
          snprintf(text, sizeof(text), "%s %d  LEN %d / %s", mode, te->current_id, nframes_i, total_tc);
      else if (width > 300.0)
          snprintf(text, sizeof(text), "SRC %d..%d  LEN %d / %s", src_a, src_b, nframes_i, total_tc);
      else if (width > 210.0)
          snprintf(text, sizeof(text), "LEN %d / %s", nframes_i, total_tc);
      else
          snprintf(text, sizeof(text), "%s", total_tc);

      cairo_set_font_size(cr, timeline_info_font_size(te));
      timeline_draw_right_text(cr, text, width - 4.0, bottom_row_y, &dim_fg, 0.58);
      cairo_set_font_size(cr, timeline_label_font_size(te));
  }


  if (te->audio_lane_active) {
      gint display_start_frame = timeline_clamp_frame_i(te, te->audio_lane_video_anchor);
      gint display_wav_ms = MAX(0, te->audio_lane_wav_anchor_ms);
      gchar wav_tc[32];
      const gchar *mode_name = te->audio_lane_mode == 2 ? "Track Align seed" : "Queue/Follow";
      const gchar *edit_hint = NULL;

      timeline_format_ms(display_wav_ms, wav_tc, sizeof(wav_tc));

      if (te->audio_lane_drag_wav)
          edit_hint = "editing WAV time";
      else if (te->audio_lane_drag)
          edit_hint = "moving switch frame";
      else
          edit_hint = "drag switch | Shift-drag/wheel WAV time";

      if (te->audio_lane_source == 1) {
          if (te->audio_lane_length_ms > 0) {
              gchar len_tc[32];
              timeline_format_ms(te->audio_lane_length_ms, len_tc, sizeof(len_tc));
              snprintf(text, sizeof(text), "Audio: WAV slot %d  %s  switch %dfr  wav %s / %s%s",
                       te->audio_lane_profile,
                       mode_name,
                       display_start_frame,
                       wav_tc,
                       len_tc,
                       te->audio_lane_loop ? "  loop" : "");
          }
          else {
              snprintf(text, sizeof(text), "Audio: WAV slot %d  %s  switch %dfr  wav %s",
                       te->audio_lane_profile, mode_name, display_start_frame, wav_tc);
          }
      }
      else
          snprintf(text, sizeof(text), "Audio: JACK external  %s  switch %dfr",
                   mode_name, display_start_frame);

      cairo_set_font_size(cr, timeline_info_font_size(te));
      timeline_draw_plain_text(cr, text, 4.0, wav_info_row_y, &color, 0.74);

      if (width > 455.0)
          timeline_draw_right_text(cr, edit_hint, width - 4.0, wav_info_row_y, &dim_fg, te->audio_lane_drag ? 0.72 : 0.46);

      cairo_set_font_size(cr, timeline_label_font_size(te));
  }

  if (te->current_location != MOUSE_OUTSIDE && !timeline_latch_visible(te)) {
      const gchar *hint = timeline_hover_hint(te);

      if (hint && *hint && width > 210.0) {
          cairo_set_font_size(cr, timeline_info_font_size(te));
          timeline_draw_text_pill(cr,
                                  hint,
                                  width * 0.5,
                                  MAX(10.0, tri_y + 9.0),
                                  width,
                                  &color,
                                  0.76,
                                  &col2,
                                  0.16);
          cairo_set_font_size(cr, timeline_label_font_size(te));
      }
  }

  return FALSE;
}


GtkWidget *timeline_new(void)
{
  GtkWidget *widget = GTK_WIDGET(g_object_new(timeline_get_type(), NULL));
  TimelineSelection *te = TIMELINE_SELECTION(widget);

  te->widget = widget;
  timeline_update_font_metrics(te);
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

  TL_EVT(te, "new", "timeline widget created");

  return widget;
}
