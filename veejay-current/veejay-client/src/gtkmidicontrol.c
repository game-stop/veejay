/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2026 Niels Elburg <nwelburg@gmail.com> 
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
#include <config.h>
#include "gtkmidicontrol.h"
#include "vj-api.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pango/pangocairo.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vims.h>
#include <gtktimeselection.h>

extern GtkWidget *glade_xml_get_widget_(GtkBuilder *m, const char *name);
extern void msg_vims(char *message);
extern void vj_msg(int type, const char format[], ...);

#define MIDI_MONITOR_EVENTS 48
#define MIDI_ACTIVE_TILES 12
#define MIDI_RECENT_VISIBLE 10
#define MIDI_RECENT_COLUMNS 5
#define MIDI_HELD_NOTE_TILE_W 72.0
#define MIDI_HELD_NOTE_TILE_H 48.0
#define MIDI_HELD_NOTE_GAP 8.0
#define MIDI_HELD_NOTE_MIN_COLUMNS 8
#define MIDI_HELD_NOTE_MAX_COLUMNS 10
#define MIDI_HELD_NOTE_ROWS 2
#define MIDI_LIVE_TILE_W 184.0
#define MIDI_LIVE_TILE_H 92.0
#define MIDI_LIVE_TILE_GAP 12.0
#define MIDI_EXTRA_TOGGLE 6
#define MIDI_EXTRA_DUAL_TOGGLE 7
#define MIDI_SOURCE_WATCH_INTERVAL_MS 1000
#define MIDI_ROUTE_CARD_W 220.0
#define MIDI_ROUTE_CARD_H 112.0
#define MIDI_ROUTE_CARD_GAP 12.0
#define MIDI_ROUTE_MARGIN 14.0
#define MIDI_ROUTE_AREA_H 210
#define MIDI_ROUTE_ACTIVITY_US 900000
#define MIDI_ROUTE_COLUMNS 5
#define MIDI_ROUTE_ROW_GAP 12.0
#define MIDI_ROUTE_VIEW_TOP 30.0
#define MIDI_ROUTE_MIN_ZOOM 0.38
#define MIDI_ROUTE_MAX_ZOOM 1.60
#define MIDI_ROUTE_FIT_PADDING 18.0

typedef struct {
    VjMidiEvent event;
    gint64 update_us;
} MidiLiveState;

typedef struct {
    guint id;
    VjMidiEvent event;
    int output;
    gboolean output_valid;
    gint64 update_us;
} MidiRouteActivity;

typedef struct _VjMidiContext {
    GtkBuilder *builder;
    void *timeline;
    VjMidiEngine *engine;
    VjMidiMap *map;
    GtkWidget *panel;
    gboolean learning;
    gboolean dispatch_enabled;
    gboolean have_last_event;
    gboolean pending_learn_event;
    VjMidiEvent last_event;
    int special_center_fps_x100;
    char *filename;
} VjMidiContext;

enum {
    MAP_COL_ID = 0,
    MAP_COL_ENABLED,
    MAP_COL_DEVICE,
    MAP_COL_INPUT,
    MAP_COL_MODE,
    MAP_COL_TARGET,
    MAP_N_COLUMNS
};

struct _GvrMidiControl {
    GtkBox parent_instance;
    VjMidiContext *context;

    GtkWidget *device_area;
    GtkWidget *monitor_area;
    GtkWidget *recent_area;
    GtkWidget *route_area;
    GtkWidget *curve_area;
    GtkWidget *route_live_area;
    GtkWidget *signal_paned;
    GtkWidget *mapping_paned;
    GtkWidget *editor_revealer;
    GtkWidget *editor_stack;
    GtkWidget *editor_toggle;
    GtkWidget *selected_summary;
    GtkWidget *tree;
    GtkListStore *store;

    GtkWidget *learn_button;
    GtkWidget *capture_button;
    GtkWidget *connect_button;
    GtkWidget *disconnect_button;
    GtkWidget *source_combo;
    GtkWidget *mapping_stack;
    GtkWidget *mapping_empty_label;
    GtkWidget *device_combo;
    GtkWidget *event_combo;
    GtkWidget *channel_spin;
    GtkWidget *control_spin;
    GtkWidget *mode_combo;
    GtkWidget *input_min_spin;
    GtkWidget *input_max_spin;
    GtkWidget *out_min_spin;
    GtkWidget *out_max_spin;
    GtkWidget *deadzone_spin;
    GtkWidget *invert_check;
    GtkWidget *enabled_check;
    GtkWidget *action_combo;
    GtkWidget *target_spin;
    GtkWidget *args_entry;
    GtkWidget *controlled_arg_combo;
    GtkWidget *name_entry;

    guint selected_id;
    int selected_device_index;
    int hover_device_index;
    int hover_wire_delete_index;
    gboolean wire_dragging;
    gboolean wire_sink_hover;
    int wire_drag_source_index;
    double wire_pointer_x;
    double wire_pointer_y;
    VjMidiDeviceInfo *devices;
    int device_count;
    VjMidiEvent recent[MIDI_MONITOR_EVENTS];
    gint64 recent_time_us[MIDI_MONITOR_EVENTS];
    int recent_count;
    GArray *held_notes;
    GArray *active_controls;
    GArray *route_activity;
    int hover_route_index;
    gboolean route_panning;
    guint route_pan_button;
    double route_pan_start_x;
    double route_pan_start_y;
    double route_pan_origin_x;
    double route_pan_origin_y;
    double route_view_x;
    double route_view_y;
    double route_view_scale;
    gboolean route_view_valid;
    gboolean signal_split_initialized;
    gboolean mapping_split_initialized;
    guint monitor_decay_timer;
    guint source_watch_timer;
    guint transform_preview_idle;
    gboolean live_capture_hover;
    gboolean controlled_arg_syncing;
};

struct _GvrMidiControlClass {
    GtkBoxClass parent_class;
};

G_DEFINE_TYPE(GvrMidiControl, gvr_midi_control, GTK_TYPE_BOX)

static GtkWidget *midi_icon_image(const char *icon_name);
static void midi_button_set_icon(GtkWidget *button, const char *icon_name);
static void midi_set_tooltip(GtkWidget *widget, const char *text);
static void midi_enabled_toggle_sync_icon(GtkToggleButton *button, gpointer data);

static void midi_context_refresh(VjMidiContext *context);
static void midi_control_rescan(GvrMidiControl *self);
static gboolean midi_source_watch_tick(gpointer data);
static void midi_capture_mapping(GtkButton *button, gpointer data);
static void midi_control_fill_editor(GvrMidiControl *self, VjMidiMapping *m);
static void midi_editor_set_expanded(GvrMidiControl *self, gboolean expanded);
static void midi_selected_route_summary_update(GvrMidiControl *self, const VjMidiMapping *m);
static gchar *midi_mapping_input_text(const VjMidiMapping *m);
static int midi_mapping_special_center(VjMidiMapping *m);
static void midi_context_refresh_special_centers(VjMidiContext *context);
static gchar *midi_mapping_target_text(const VjMidiMapping *m);
static int midi_route_preview_output(const VjMidiMapping *m, int raw);
static int midi_vims_controlled_arg_index(const char *args);
static int combo_get_int(GtkComboBox *combo, int fallback);

static int midi_vims_message_sane(const char *message)
{
    size_t len;

    if(!message)
        return 0;
    len = strlen(message);
    if(len < 5 || len > 4096)
        return 0;
    if(!g_ascii_isdigit(message[0]) || !g_ascii_isdigit(message[1]) ||
       !g_ascii_isdigit(message[2]) || message[3] != ':')
        return 0;
    if(message[len - 1] != ';')
        return 0;
    if(strchr(message, ';') != message + len - 1)
        return 0;
    if(strchr(message, '\n') || strchr(message, '\r'))
        return 0;
    int selector = (message[0] - '0') * 100 +
                   (message[1] - '0') * 10 +
                   (message[2] - '0');
    if(selector < VJ_MIDI_VIMS_MIN_ID || selector > VJ_MIDI_VIMS_MAX_ID)
        return 0;
    return 1;
}

static void midi_context_send(const char *message, void *user_data)
{
    (void) user_data;
    if(!message || !message[0])
        return;
    if(!midi_vims_message_sane(message)) {
        vj_msg(VEEJAY_MSG_ERROR, "MIDI: rejected malformed VIMS message '%s'", message);
        return;
    }
    msg_vims((char *) message);
    veejay_msg(VEEJAY_MSG_DEBUG, "MIDI: VIMS %s", message);
}

static void cairo_get_fg(GtkWidget *widget, GdkRGBA *fg)
{
    GtkStyleContext *style = gtk_widget_get_style_context(widget);
    gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, fg);
}

static double midi_interface_font_px(GtkWidget *widget)
{
    PangoContext *context = widget ? gtk_widget_get_pango_context(widget) : NULL;
    const PangoFontDescription *font = context ? pango_context_get_font_description(context) : NULL;
    int size = font ? pango_font_description_get_size(font) : 0;
    double value;

    if(size <= 0)
        return 13.333333;

    value = (double)size / PANGO_SCALE;
    if(!pango_font_description_get_size_is_absolute(font)) {
        double dpi = context ? pango_cairo_context_get_resolution(context) : -1.0;
        if(dpi <= 0.0)
            dpi = 96.0;
        value *= dpi / 72.0;
    }
    return CLAMP(value, 8.0, 64.0);
}

static double midi_font_size(GtkWidget *widget, double design_size)
{
    const double base = midi_interface_font_px(widget);
    const double relative = 1.0 + MAX(0.0, design_size - 7.2) / 11.0;
    return base * relative;
}

static void midi_select_font(GtkWidget *widget, cairo_t *cr,
                             cairo_font_weight_t weight, double design_size)
{
    PangoContext *context = widget ? gtk_widget_get_pango_context(widget) : NULL;
    const PangoFontDescription *font = context ? pango_context_get_font_description(context) : NULL;
    const char *family = font ? pango_font_description_get_family(font) : NULL;

    cairo_select_font_face(cr, family && family[0] ? family : "Sans",
                           CAIRO_FONT_SLANT_NORMAL, weight);
    cairo_set_font_size(cr, midi_font_size(widget, design_size));
}

static int midi_recent_overlay_height(GtkWidget *widget)
{
    const double title = midi_font_size(widget, 9.8);
    const double chip = midi_font_size(widget, 9.2);
    const double top = MAX(24.0, title + 10.0);
    const double chip_h = MAX(25.0, chip + 12.0);
    return (int)ceil(top + chip_h * 2.0 + 13.0);
}

static void cairo_text(GtkWidget *widget, cairo_t *cr, const GdkRGBA *fg,
                       double x, double y, double size, const char *text)
{
    cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, fg->alpha);
    midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_NORMAL, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text ? text : "");
}

static void cairo_round_rect(cairo_t *cr, double x, double y,
                             double w, double h, double radius)
{
    double r = MIN(radius, MIN(w, h) * 0.5);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -(G_PI / 2.0), 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, (G_PI / 2.0));
    cairo_arc(cr, x + r, y + h - r, r, (G_PI / 2.0), G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3.0 * (G_PI / 2.0));
    cairo_close_path(cr);
}

#define MIDI_PATCH_SOURCE_X 32.0
#define MIDI_PATCH_SOURCE_Y 82.0
#define MIDI_PATCH_SOURCE_W 248.0
#define MIDI_PATCH_SOURCE_H 82.0
#define MIDI_PATCH_ROW_GAP 104.0
#define MIDI_PATCH_SINK_X 340.0
#define MIDI_PATCH_SINK_W 300.0
#define MIDI_PATCH_SINK_H 118.0
#define MIDI_PATCH_PADDING 54.0
#define MIDI_PATCH_MIN_WIDTH 704
#define MIDI_PATCH_MIN_HEIGHT 320

static gboolean midi_device_card_rect(GvrMidiControl *self, const GtkAllocation *a,
                                      int index, double *x, double *y,
                                      double *w, double *h)
{
    (void) a;
    if(index < 0 || index >= self->device_count)
        return FALSE;

    *x = MIDI_PATCH_SOURCE_X;
    *y = MIDI_PATCH_SOURCE_Y + index * MIDI_PATCH_ROW_GAP;
    *w = MIDI_PATCH_SOURCE_W;
    *h = MIDI_PATCH_SOURCE_H;
    return TRUE;
}

static void midi_patch_sink_rect(GvrMidiControl *self,
                                 double *x, double *y, double *w, double *h)
{
    double center = MIDI_PATCH_SOURCE_Y + MIDI_PATCH_SOURCE_H * 0.5;
    if(self->device_count > 1)
        center += (self->device_count - 1) * MIDI_PATCH_ROW_GAP * 0.5;

    *x = MIDI_PATCH_SINK_X;
    *y = MAX(MIDI_PATCH_SOURCE_Y, center - MIDI_PATCH_SINK_H * 0.5);
    *w = MIDI_PATCH_SINK_W;
    *h = MIDI_PATCH_SINK_H;
}

static void midi_patch_update_canvas_size(GvrMidiControl *self)
{
    double sink_x, sink_y, sink_w, sink_h;
    midi_patch_sink_rect(self, &sink_x, &sink_y, &sink_w, &sink_h);

    double source_bottom = MIDI_PATCH_SOURCE_Y + MIDI_PATCH_SOURCE_H;
    if(self->device_count > 1)
        source_bottom += (self->device_count - 1) * MIDI_PATCH_ROW_GAP;
    double bottom = MAX(source_bottom, sink_y + sink_h);
    int height = MAX(MIDI_PATCH_MIN_HEIGHT, (int)ceil(bottom + MIDI_PATCH_PADDING));
    int width = MAX(MIDI_PATCH_MIN_WIDTH, (int)ceil(sink_x + sink_w + MIDI_PATCH_PADDING));
    gtk_widget_set_size_request(self->device_area, width, height);
}

static void midi_update_device_actions(GvrMidiControl *self)
{
    gboolean have = self->selected_device_index >= 0 &&
                    self->selected_device_index < self->device_count;
    gboolean connected = have ? self->devices[self->selected_device_index].connected : FALSE;
    if(self->connect_button)
        gtk_widget_set_sensitive(self->connect_button, have && !connected);
    if(self->disconnect_button)
        gtk_widget_set_sensitive(self->disconnect_button, have && connected);
}

static void midi_select_device_index(GvrMidiControl *self, int index)
{
    if(index < 0 || index >= self->device_count)
        return;
    self->selected_device_index = index;
    if(self->source_combo && gtk_combo_box_get_active(GTK_COMBO_BOX(self->source_combo)) != index)
        gtk_combo_box_set_active(GTK_COMBO_BOX(self->source_combo), index);
    midi_update_device_actions(self);
}

static gboolean midi_patch_source_socket_hit(GvrMidiControl *self,
                                               const GtkAllocation *a,
                                               int index,
                                               double px, double py)
{
    double x, y, w, h;
    if(!midi_device_card_rect(self, a, index, &x, &y, &w, &h))
        return FALSE;
    const double dx = px - (x + w);
    const double dy = py - (y + h * 0.5);
    return dx * dx + dy * dy <= 15.0 * 15.0;
}

static gboolean midi_patch_sink_socket_hit(GvrMidiControl *self,
                                             double px, double py)
{
    double x, y, w, h;
    midi_patch_sink_rect(self, &x, &y, &w, &h);
    const double dx = px - x;
    const double dy = py - (y + h * 0.5);
    return dx * dx + dy * dy <= 17.0 * 17.0;
}

static void midi_patch_wire_point(GvrMidiControl *self,
                                  const GtkAllocation *a,
                                  int index,
                                  double t,
                                  double *px, double *py)
{
    double x, y, w, h;
    double sink_x, sink_y, sink_w, sink_h;
    if(!midi_device_card_rect(self, a, index, &x, &y, &w, &h)) {
        if(px)
            *px = 0.0;
        if(py)
            *py = 0.0;
        return;
    }
    midi_patch_sink_rect(self, &sink_x, &sink_y, &sink_w, &sink_h);

    const double x0 = x + w;
    const double y0 = y + h * 0.5;
    const double x3 = sink_x;
    const double y3 = sink_y + sink_h * 0.5;
    const double dx = MAX(44.0, (x3 - x0) * 0.52);
    const double x1 = x0 + dx;
    const double y1 = y0;
    const double x2 = x3 - dx;
    const double y2 = y3;
    const double u = 1.0 - t;

    if(px)
        *px = u*u*u*x0 + 3.0*u*u*t*x1 + 3.0*u*t*t*x2 + t*t*t*x3;
    if(py)
        *py = u*u*u*y0 + 3.0*u*u*t*y1 + 3.0*u*t*t*y2 + t*t*t*y3;
}

static gboolean midi_patch_wire_delete_hit(GvrMidiControl *self,
                                             const GtkAllocation *a,
                                             int index,
                                             double px, double py)
{
    if(index < 0 || index >= self->device_count || !self->devices[index].connected)
        return FALSE;
    double x, y;
    midi_patch_wire_point(self, a, index, 0.52, &x, &y);
    const double dx = px - x;
    const double dy = py - y;
    return dx * dx + dy * dy <= 11.0 * 11.0;
}

static void midi_patch_cancel_wire_drag(GvrMidiControl *self)
{
    self->wire_dragging = FALSE;
    self->wire_sink_hover = FALSE;
    self->wire_drag_source_index = -1;
}

static gboolean midi_device_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    const gint64 now = g_get_monotonic_time();

    cairo_set_source_rgb(cr, 0.018, 0.026, 0.038);
    cairo_paint(cr);

    const double overlay_x = 14.0;
    const double overlay_y = 12.0;
    const double overlay_w = 626.0;
    const double overlay_h = 54.0;
    cairo_round_rect(cr, overlay_x, overlay_y, overlay_w, overlay_h, 10.0);
    cairo_set_source_rgba(cr, 0.025, 0.035, 0.050, 0.96);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.28, 0.36, 0.46, 0.78);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.84);
    midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_BOLD, 11.0);
    cairo_move_to(cr, overlay_x + 12.0, overlay_y + 19.0);
    cairo_show_text(cr, "MIDI PATCHBAY");
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.56);
    midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_NORMAL, 9.3);
    cairo_move_to(cr, overlay_x + 12.0, overlay_y + 38.0);
    cairo_show_text(cr, "Drag a source socket to Reloaded to connect  ·  click × on a wire to disconnect");

    double sink_x, sink_y, sink_w, sink_h;
    midi_patch_sink_rect(self, &sink_x, &sink_y, &sink_w, &sink_h);
    const double sink_port_x = sink_x;
    const double sink_port_y = sink_y + sink_h * 0.5;

    if(self->device_count == 0) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.80);
        midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_BOLD, 12.0);
        cairo_move_to(cr, MIDI_PATCH_SOURCE_X, MIDI_PATCH_SOURCE_Y + 28.0);
        cairo_show_text(cr, "No ALSA MIDI source ports detected");
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.52);
        midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_NORMAL, 9.5);
        cairo_move_to(cr, MIDI_PATCH_SOURCE_X, MIDI_PATCH_SOURCE_Y + 49.0);
        cairo_show_text(cr, "Start/connect a controller, then use Rescan.");
    }

    for(int i = 0; i < self->device_count; i++) {
        VjMidiDeviceInfo *d = &self->devices[i];
        double x, y, w, h;
        if(!midi_device_card_rect(self, &a, i, &x, &y, &w, &h) || !d->connected)
            continue;
        const gboolean active = d->connected && self->context->have_last_event &&
                                self->recent_count > 0 &&
                                now - self->recent_time_us[0] < 1300000 &&
                                self->context->last_event.source_client == d->client &&
                                self->context->last_event.source_port == d->port;
        const double sx = x + w;
        const double sy = y + h * 0.5;
        const double dx = MAX(44.0, (sink_port_x - sx) * 0.52);
        cairo_set_source_rgba(cr,
                              active ? 0.62 : 0.24,
                              active ? 0.92 : 0.78,
                              0.98,
                              active ? 0.98 : 0.72);
        cairo_set_line_width(cr, active ? 3.2 : 1.8);
        cairo_move_to(cr, sx, sy);
        cairo_curve_to(cr, sx + dx, sy,
                       sink_port_x - dx, sink_port_y,
                       sink_port_x, sink_port_y);
        cairo_stroke(cr);

        double delete_x, delete_y;
        midi_patch_wire_point(self, &a, i, 0.52, &delete_x, &delete_y);
        const gboolean delete_hover = i == self->hover_wire_delete_index;
        cairo_arc(cr, delete_x, delete_y, delete_hover ? 9.0 : 7.5, 0, 2.0 * G_PI);
        cairo_set_source_rgba(cr, 0.055, 0.070, 0.090, delete_hover ? 0.98 : 0.90);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, delete_hover ? 1.0 : 0.78,
                              delete_hover ? 0.48 : 0.62,
                              delete_hover ? 0.30 : 0.68, 0.98);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, 1.0, 0.82, 0.76, 0.96);
        cairo_set_line_width(cr, 1.6);
        cairo_move_to(cr, delete_x - 3.0, delete_y - 3.0);
        cairo_line_to(cr, delete_x + 3.0, delete_y + 3.0);
        cairo_move_to(cr, delete_x + 3.0, delete_y - 3.0);
        cairo_line_to(cr, delete_x - 3.0, delete_y + 3.0);
        cairo_stroke(cr);
    }

    if(self->wire_dragging && self->wire_drag_source_index >= 0 &&
       self->wire_drag_source_index < self->device_count) {
        double x, y, w, h;
        if(midi_device_card_rect(self, &a, self->wire_drag_source_index, &x, &y, &w, &h)) {
            const double sx = x + w;
            const double sy = y + h * 0.5;
            const double ex = self->wire_sink_hover ? sink_port_x : self->wire_pointer_x;
            const double ey = self->wire_sink_hover ? sink_port_y : self->wire_pointer_y;
            const double drag_dx = MAX(44.0, fabs(ex - sx) * 0.52);
            cairo_set_source_rgba(cr,
                                  self->wire_sink_hover ? 0.34 : 0.50,
                                  self->wire_sink_hover ? 0.94 : 0.78,
                                  self->wire_sink_hover ? 0.70 : 0.98,
                                  0.96);
            cairo_set_line_width(cr, 2.4);
            cairo_move_to(cr, sx, sy);
            cairo_curve_to(cr, sx + drag_dx, sy,
                           ex - drag_dx, ey, ex, ey);
            cairo_stroke(cr);
        }
    }

    cairo_round_rect(cr, sink_x, sink_y, sink_w, sink_h, 9.0);
    cairo_set_source_rgba(cr, 0.07, 0.20, 0.18, 0.98);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.34, 0.88, 0.70, 0.96);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.96);
    midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_BOLD, 10.8);
    cairo_move_to(cr, sink_x + 16.0, sink_y + 27.0);
    cairo_show_text(cr, "RELOADED");
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.64);
    midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_NORMAL, 9.5);
    cairo_move_to(cr, sink_x + 16.0, sink_y + 49.0);
    cairo_show_text(cr, "ALSA SEQUENCER  ·  MIDI INPUT");
    cairo_set_font_size(cr, midi_font_size(widget, 8.8));
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.48);
    cairo_move_to(cr, sink_x + 16.0, sink_y + 72.0);
    cairo_show_text(cr, self->device_count > 0 ? "CONNECTED SOURCES ROUTE HERE" : "WAITING FOR MIDI SOURCES");

    cairo_arc(cr, sink_port_x, sink_port_y, 9.0, 0, 2.0 * G_PI);
    cairo_set_source_rgba(cr, 0.34, 0.88, 0.70, 1.0);
    cairo_fill(cr);
    if(self->wire_dragging && self->wire_sink_hover) {
        cairo_arc(cr, sink_port_x, sink_port_y, 17.0, 0, 2.0 * G_PI);
        cairo_set_source_rgba(cr, 0.34, 0.94, 0.70, 0.22);
        cairo_fill(cr);
        cairo_arc(cr, sink_port_x, sink_port_y, 10.0, 0, 2.0 * G_PI);
        cairo_set_source_rgba(cr, 0.52, 1.0, 0.78, 1.0);
        cairo_fill(cr);
    }
    cairo_set_source_rgba(cr, 0.025, 0.035, 0.050, 1.0);
    cairo_arc(cr, sink_port_x, sink_port_y, 3.0, 0, 2.0 * G_PI);
    cairo_fill(cr);

    for(int i = 0; i < self->device_count; i++) {
        VjMidiDeviceInfo *d = &self->devices[i];
        double x, y, w, h;
        if(!midi_device_card_rect(self, &a, i, &x, &y, &w, &h))
            continue;
        const gboolean selected = i == self->selected_device_index;
        const gboolean hovered = i == self->hover_device_index;
        const gboolean active = d->connected && self->context->have_last_event &&
                                self->recent_count > 0 &&
                                now - self->recent_time_us[0] < 1300000 &&
                                self->context->last_event.source_client == d->client &&
                                self->context->last_event.source_port == d->port;

        cairo_round_rect(cr, x, y, w, h, 9.0);
        cairo_set_source_rgba(cr,
                              d->connected ? 0.07 : 0.10,
                              d->connected ? 0.20 : 0.12,
                              d->connected ? 0.18 : 0.17,
                              0.98);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr,
                              active ? 1.00 : (selected ? 0.90 : (hovered ? 0.62 : 0.34)),
                              active ? 0.74 : (selected ? 0.72 : (hovered ? 0.92 : 0.44)),
                              active ? 0.18 : (selected ? 0.20 : (hovered ? 0.98 : 0.62)),
                              0.96);
        cairo_set_line_width(cr, active ? 3.0 : (selected ? 2.2 : (hovered ? 1.8 : 1.1)));
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, d->connected ? 0.34 : 0.54,
                              d->connected ? 0.88 : 0.40,
                              d->connected ? 0.70 : 0.88,
                              0.96);
        cairo_arc(cr, x + 15.0, y + 18.0, 4.5, 0, 2.0 * G_PI);
        cairo_fill(cr);

        char title[96];
        g_strlcpy(title, d->name, sizeof(title));
        const int max_chars = 29;
        if((int)strlen(title) > max_chars) {
            title[max_chars - 3] = '.';
            title[max_chars - 2] = '.';
            title[max_chars - 1] = '.';
            title[max_chars] = '\0';
        }
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.96);
        midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_BOLD, 9.3);
        cairo_move_to(cr, x + 27.0, y + 22.0);
        cairo_show_text(cr, title);

        char detail[96];
        g_snprintf(detail, sizeof(detail), "ALSA %d:%d  ·  %s",
                   d->client, d->port, d->connected ? "CONNECTED" : "AVAILABLE");
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.62);
        midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_NORMAL, 9.2);
        cairo_move_to(cr, x + 14.0, y + 45.0);
        cairo_show_text(cr, detail);

        cairo_set_font_size(cr, midi_font_size(widget, 8.5));
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.46);
        cairo_move_to(cr, x + 14.0, y + 65.0);
        cairo_show_text(cr, active ? "MIDI ACTIVITY" : (d->connected ? "ROUTED TO RELOADED" : "DRAG SOCKET TO CONNECT"));

        if(hovered) {
            cairo_arc(cr, x + w, y + h * 0.5, 16.0, 0, 2.0 * G_PI);
            cairo_set_source_rgba(cr, 0.24, 0.78, 0.98, 0.22);
            cairo_fill(cr);
        }
        cairo_set_source_rgba(cr,
                              hovered ? 0.62 : (d->connected ? 0.24 : 0.22),
                              hovered ? 0.92 : (d->connected ? 0.78 : 0.56),
                              hovered ? 0.98 : (d->connected ? 0.98 : 0.70),
                              1.0);
        cairo_arc(cr, x + w, y + h * 0.5, active ? 10.5 : (hovered ? 10.0 : 9.0), 0, 2.0 * G_PI);
        cairo_fill(cr);
    }

    if(self->selected_device_index >= 0 && self->selected_device_index < self->device_count) {
        VjMidiDeviceInfo *d = &self->devices[self->selected_device_index];
        char selected[224];
        g_snprintf(selected, sizeof(selected), "SELECTED  ·  %s [%d:%d]  ·  %s",
                   d->name, d->client, d->port,
                   d->connected ? "connected" : "available");
        cairo_set_source_rgba(cr, 0.42, 0.92, 0.74, 0.88);
        midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_BOLD, 8.8);
        cairo_move_to(cr, MIDI_PATCH_SOURCE_X, a.height - 18.0);
        cairo_show_text(cr, selected);
    }
    return FALSE;
}

static gboolean midi_device_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);

    if(event->button != 1)
        return FALSE;

    for(int i = 0; i < self->device_count; i++) {
        if(midi_patch_wire_delete_hit(self, &a, i, event->x, event->y)) {
            midi_select_device_index(self, i);
            VjMidiDeviceInfo d = self->devices[i];
            vj_midi_engine_disconnect(self->context->engine, d.client, d.port);
            midi_control_rescan(self);
            return TRUE;
        }
    }

    for(int i = 0; i < self->device_count; i++) {
        if(midi_patch_source_socket_hit(self, &a, i, event->x, event->y)) {
            midi_select_device_index(self, i);
            self->wire_dragging = TRUE;
            self->wire_sink_hover = FALSE;
            self->wire_drag_source_index = i;
            self->wire_pointer_x = event->x;
            self->wire_pointer_y = event->y;
            gtk_widget_queue_draw(self->device_area);
            return TRUE;
        }
    }

    for(int i = 0; i < self->device_count; i++) {
        double x, y, w, h;
        if(!midi_device_card_rect(self, &a, i, &x, &y, &w, &h))
            continue;
        if(event->x >= x && event->x <= x + w && event->y >= y && event->y <= y + h) {
            midi_select_device_index(self, i);
            gtk_widget_queue_draw(self->device_area);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean midi_device_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(event->button != 1 || !self->wire_dragging)
        return FALSE;

    const int source_index = self->wire_drag_source_index;
    const gboolean connect = midi_patch_sink_socket_hit(self, event->x, event->y);
    midi_patch_cancel_wire_drag(self);

    if(connect && source_index >= 0 && source_index < self->device_count) {
        VjMidiDeviceInfo d = self->devices[source_index];
        midi_select_device_index(self, source_index);
        if(!d.connected)
            vj_midi_engine_connect(self->context->engine, d.client, d.port);
        midi_control_rescan(self);
    }
    else {
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static gboolean midi_device_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);

    if(self->wire_dragging) {
        self->wire_pointer_x = event->x;
        self->wire_pointer_y = event->y;
        self->wire_sink_hover = midi_patch_sink_socket_hit(self, event->x, event->y);
        gtk_widget_queue_draw(self->device_area);
        return TRUE;
    }

    int hover = -1;
    int delete_hover = -1;
    gboolean source_socket_hover = FALSE;
    for(int i = 0; i < self->device_count; i++) {
        if(delete_hover < 0 && midi_patch_wire_delete_hit(self, &a, i, event->x, event->y))
            delete_hover = i;
        if(midi_patch_source_socket_hit(self, &a, i, event->x, event->y))
            source_socket_hover = TRUE;
        double x, y, w, h;
        if(!midi_device_card_rect(self, &a, i, &x, &y, &w, &h))
            continue;
        if(event->x >= x && event->x <= x + w && event->y >= y && event->y <= y + h)
            hover = i;
    }

    const gboolean changed = hover != self->hover_device_index ||
                             delete_hover != self->hover_wire_delete_index;
    self->hover_device_index = hover;
    self->hover_wire_delete_index = delete_hover;

    GdkWindow *window = gtk_widget_get_window(widget);
    if(window) {
        GdkCursor *cursor = NULL;
        if(delete_hover >= 0)
            cursor = gdk_cursor_new_for_display(gdk_window_get_display(window), GDK_HAND2);
        else if(source_socket_hover)
            cursor = gdk_cursor_new_for_display(gdk_window_get_display(window), GDK_CROSSHAIR);
        gdk_window_set_cursor(window, cursor);
        if(cursor)
            g_object_unref(cursor);
    }

    if(changed)
        gtk_widget_queue_draw(self->device_area);
    return FALSE;
}

static gboolean midi_device_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
    (void) event;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(!self->wire_dragging &&
       (self->hover_device_index != -1 || self->hover_wire_delete_index != -1)) {
        self->hover_device_index = -1;
        self->hover_wire_delete_index = -1;
        gtk_widget_queue_draw(self->device_area);
    }
    GdkWindow *window = gtk_widget_get_window(widget);
    if(window && !self->wire_dragging)
        gdk_window_set_cursor(window, NULL);
    return FALSE;
}

static double midi_live_norm(const VjMidiEvent *ev)
{
    if(!ev || ev->value_max == ev->value_min)
        return 0.0;
    return CLAMP((double)(ev->value - ev->value_min) /
                 (double)(ev->value_max - ev->value_min), 0.0, 1.0);
}

static VjMidiEventType midi_live_key_type(VjMidiEventType type)
{
    return type == VJ_MIDI_EVENT_NOTE_OFF ? VJ_MIDI_EVENT_NOTE_ON : type;
}

static gboolean midi_live_same_control(const VjMidiEvent *a, const VjMidiEvent *b)
{
    if(!a || !b ||
       a->source_client != b->source_client ||
       a->source_port != b->source_port ||
       a->channel != b->channel ||
       midi_live_key_type(a->type) != midi_live_key_type(b->type))
        return FALSE;

    if(midi_live_key_type(a->type) == VJ_MIDI_EVENT_PROGRAM_CHANGE)
        return TRUE;
    return a->control == b->control;
}

static gboolean midi_live_is_note(const VjMidiEvent *ev)
{
    return ev && (ev->type == VJ_MIDI_EVENT_NOTE_ON ||
                  ev->type == VJ_MIDI_EVENT_NOTE_OFF);
}

static int midi_live_held_note_find(GvrMidiControl *self, const VjMidiEvent *event)
{
    if(!self->held_notes || !event)
        return -1;
    for(guint i = 0; i < self->held_notes->len; i++) {
        MidiLiveState *state = &g_array_index(self->held_notes, MidiLiveState, i);
        if(state->event.source_client == event->source_client &&
           state->event.source_port == event->source_port &&
           state->event.channel == event->channel &&
           state->event.control == event->control)
            return (int)i;
    }
    return -1;
}

static gint midi_live_held_note_compare(gconstpointer pa, gconstpointer pb)
{
    const MidiLiveState *a = pa;
    const MidiLiveState *b = pb;
    if(a->event.channel != b->event.channel)
        return a->event.channel < b->event.channel ? -1 : 1;
    if(a->event.control != b->event.control)
        return a->event.control < b->event.control ? -1 : 1;
    if(a->event.source_client != b->event.source_client)
        return a->event.source_client < b->event.source_client ? -1 : 1;
    if(a->event.source_port != b->event.source_port)
        return a->event.source_port < b->event.source_port ? -1 : 1;
    return 0;
}

static void midi_live_update_held_note(GvrMidiControl *self,
                                       const VjMidiEvent *event,
                                       gint64 now)
{
    if(!midi_live_is_note(event))
        return;
    int index = midi_live_held_note_find(self, event);
    const gboolean on = event->type == VJ_MIDI_EVENT_NOTE_ON && event->value > 0;
    if(!on) {
        if(index >= 0)
            g_array_remove_index(self->held_notes, (guint)index);
        return;
    }

    MidiLiveState state;
    state.event = *event;
    state.update_us = now;
    if(index >= 0)
        g_array_index(self->held_notes, MidiLiveState, (guint)index) = state;
    else
        g_array_append_val(self->held_notes, state);
    g_array_sort(self->held_notes, midi_live_held_note_compare);
}

static int midi_live_active_control_find(GvrMidiControl *self,
                                         const VjMidiEvent *event)
{
    if(!self->active_controls || !event)
        return -1;
    for(guint i = 0; i < self->active_controls->len; i++) {
        MidiLiveState *state = &g_array_index(self->active_controls, MidiLiveState, i);
        if(midi_live_same_control(&state->event, event))
            return (int)i;
    }
    return -1;
}

static void midi_live_update_active_control(GvrMidiControl *self,
                                            const VjMidiEvent *event,
                                            gint64 now)
{
    if(!event || midi_live_is_note(event))
        return;

    int index = midi_live_active_control_find(self, event);
    MidiLiveState state;
    state.event = *event;
    state.update_us = now;
    if(index >= 0)
        g_array_remove_index(self->active_controls, (guint)index);
    g_array_prepend_val(self->active_controls, state);
}

static gboolean midi_live_source_connected(const VjMidiDeviceInfo *devices,
                                           int count, int client, int port)
{
    for(int i = 0; i < count; i++) {
        if(devices[i].client == client && devices[i].port == port)
            return devices[i].connected;
    }
    return FALSE;
}

static gboolean midi_live_event_source_connected(GvrMidiControl *self,
                                                 const VjMidiEvent *event)
{
    return self && event &&
           midi_live_source_connected(self->devices, self->device_count,
                                      event->source_client, event->source_port);
}

static gboolean midi_live_prune_disconnected_state(GvrMidiControl *self,
                                                    const VjMidiDeviceInfo *devices,
                                                    int count)
{
    gboolean changed = FALSE;

    if(self->held_notes) {
        for(gint i = (gint)self->held_notes->len - 1; i >= 0; i--) {
            MidiLiveState *state = &g_array_index(self->held_notes, MidiLiveState, (guint)i);
            if(!midi_live_source_connected(devices, count,
                                           state->event.source_client,
                                           state->event.source_port)) {
                g_array_remove_index(self->held_notes, (guint)i);
                changed = TRUE;
            }
        }
    }

    if(self->active_controls) {
        for(gint i = (gint)self->active_controls->len - 1; i >= 0; i--) {
            MidiLiveState *state = &g_array_index(self->active_controls, MidiLiveState, (guint)i);
            if(!midi_live_source_connected(devices, count,
                                           state->event.source_client,
                                           state->event.source_port)) {
                g_array_remove_index(self->active_controls, (guint)i);
                changed = TRUE;
            }
        }
    }

    return changed;
}

static int midi_live_held_columns(int width)
{
    int columns = (int)((width - 32.0 + MIDI_HELD_NOTE_GAP) /
                        (MIDI_HELD_NOTE_TILE_W + MIDI_HELD_NOTE_GAP));
    return CLAMP(columns, MIDI_HELD_NOTE_MIN_COLUMNS, MIDI_HELD_NOTE_MAX_COLUMNS);
}

static int midi_live_active_columns(int width)
{
    return MAX(1, MIN(5, (int)((width - 32.0 + MIDI_LIVE_TILE_GAP) /
                              (MIDI_LIVE_TILE_W + MIDI_LIVE_TILE_GAP))));
}

static int midi_live_required_height(GvrMidiControl *self, int width)
{
    const int held_columns = midi_live_held_columns(width);
    const int held_capacity = held_columns * MIDI_HELD_NOTE_ROWS;
    int held_drawn = self->held_notes ? MIN((int)self->held_notes->len, held_capacity) : 0;
    if(self->held_notes && (int)self->held_notes->len > held_capacity)
        held_drawn = held_capacity;
    int held_rows = held_drawn > 0 ? (held_drawn + held_columns - 1) / held_columns : 1;

    const int active_columns = midi_live_active_columns(width);
    const int active_count = self->active_controls ?
        MIN((int)self->active_controls->len, MIDI_ACTIVE_TILES) : 0;
    const int active_rows = active_count > 0 ?
        (active_count + active_columns - 1) / active_columns : 1;

    double y = 16.0 + 126.0 + 30.0;
    y += 22.0 + held_rows * (MIDI_HELD_NOTE_TILE_H + MIDI_HELD_NOTE_GAP) + 24.0;
    y += 24.0 + active_rows * (MIDI_LIVE_TILE_H + MIDI_LIVE_TILE_GAP) + 28.0;
    if(self->context && self->context->learning)
        y += 26.0;
    y += midi_recent_overlay_height(self->recent_area ? self->recent_area : GTK_WIDGET(self)) + 22.0;
    return MAX(520, (int)ceil(y));
}

static void midi_live_update_canvas_size(GvrMidiControl *self, int width)
{
    if(!self || !self->monitor_area)
        return;
    width = MAX(width, 720);
    gtk_widget_set_size_request(self->monitor_area, 720,
                                midi_live_required_height(self, width));
}

static void midi_live_scroll_size_allocate(GtkWidget *widget,
                                           GtkAllocation *allocation,
                                           gpointer data)
{
    (void)widget;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    midi_live_update_canvas_size(self, allocation ? allocation->width : 720);
}

static void midi_live_capture_rect(const GtkAllocation *a,
                                   double *x, double *y, double *w, double *h)
{
    const double hero_w = MIN(980.0, MAX(420.0, a->width - 32.0));
    *w = 168.0;
    *h = 30.0;
    *x = 16.0 + hero_w - *w - 16.0;
    *y = 101.0;
}

static void midi_live_draw_label(GtkWidget *widget, cairo_t *cr, double x, double y,
                                 double size, double alpha,
                                 cairo_font_weight_t weight,
                                 const char *text)
{
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, alpha);
    midi_select_font(widget, cr, weight, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text ? text : "");
}

static void midi_live_draw_meter(cairo_t *cr, double x, double y,
                                 double w, double value,
                                 gboolean bipolar)
{
    cairo_round_rect(cr, x, y, w, 8.0, 4.0);
    cairo_set_source_rgba(cr, 0.35, 0.42, 0.50, 0.24);
    cairo_fill(cr);

    if(bipolar) {
        const double cx = x + w * 0.5;
        cairo_set_source_rgba(cr, 0.52, 0.72, 0.82, 0.52);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, cx, y - 3.0);
        cairo_line_to(cr, cx, y + 11.0);
        cairo_stroke(cr);
        const double px = x + w * value;
        const double lo = MIN(cx, px);
        const double hi = MAX(cx, px);
        cairo_round_rect(cr, lo, y, MAX(2.0, hi - lo), 8.0, 4.0);
        cairo_set_source_rgba(cr, 0.34, 0.88, 0.70, 0.92);
        cairo_fill(cr);
        cairo_arc(cr, px, y + 4.0, 5.0, 0, 2.0 * G_PI);
        cairo_set_source_rgba(cr, 0.68, 0.98, 0.84, 1.0);
        cairo_fill(cr);
    }
    else {
        cairo_round_rect(cr, x, y, MAX(2.0, w * value), 8.0, 4.0);
        cairo_set_source_rgba(cr, 0.34, 0.88, 0.70, 0.92);
        cairo_fill(cr);
    }
}

static void midi_live_draw_tile(GtkWidget *widget, cairo_t *cr,
                                const VjMidiEvent *ev,
                                gint64 event_us,
                                gint64 now,
                                double x, double y)
{
    const double age = event_us > 0 ? (double)(now - event_us) / 1000000.0 : 9.0;
    const double hot = CLAMP(1.0 - age / 1.15, 0.0, 1.0);
    const gboolean pitch = ev->type == VJ_MIDI_EVENT_PITCH_BEND;
    const gboolean program = ev->type == VJ_MIDI_EVENT_PROGRAM_CHANGE;
    const double n = midi_live_norm(ev);

    cairo_round_rect(cr, x, y, MIDI_LIVE_TILE_W, MIDI_LIVE_TILE_H, 9.0);
    cairo_set_source_rgba(cr, 0.035, 0.055, 0.075, 0.98);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr,
                          hot > 0.0 ? 0.98 : 0.24,
                          hot > 0.0 ? 0.70 : 0.42,
                          hot > 0.0 ? 0.20 : 0.54,
                          0.54 + 0.42 * hot);
    cairo_set_line_width(cr, 1.1 + 1.9 * hot);
    cairo_stroke(cr);

    char title[64];
    if(pitch)
        g_snprintf(title, sizeof(title), "PITCH BEND");
    else if(ev->type == VJ_MIDI_EVENT_CC14)
        g_snprintf(title, sizeof(title), "CC14 %d", ev->control);
    else if(ev->type == VJ_MIDI_EVENT_CC)
        g_snprintf(title, sizeof(title), "CC %d", ev->control);
    else if(ev->type == VJ_MIDI_EVENT_NRPN)
        g_snprintf(title, sizeof(title), "NRPN %d", ev->control);
    else if(ev->type == VJ_MIDI_EVENT_RPN)
        g_snprintf(title, sizeof(title), "RPN %d", ev->control);
    else
        g_snprintf(title, sizeof(title), "%s", vj_midi_event_type_name(ev->type));

    midi_live_draw_label(widget, cr, x + 12.0, y + 20.0, 10.6, 0.95,
                         CAIRO_FONT_WEIGHT_BOLD, title);
    char channel[24];
    g_snprintf(channel, sizeof(channel), "CH %d", ev->channel + 1);
    midi_live_draw_label(widget, cr, x + MIDI_LIVE_TILE_W - 42.0, y + 20.0, 8.4, 0.52,
                         CAIRO_FONT_WEIGHT_NORMAL, channel);

    if(program) {
        char value[32];
        g_snprintf(value, sizeof(value), "%d", ev->value);
        midi_live_draw_label(widget, cr, x + 12.0, y + 60.0, 26.0, 0.98,
                             CAIRO_FONT_WEIGHT_BOLD, value);
        midi_live_draw_label(widget, cr, x + 64.0, y + 60.0, 8.8, 0.48,
                             CAIRO_FONT_WEIGHT_NORMAL, "PROGRAM");
    }
    else {
        char value[64];
        if(pitch)
            g_snprintf(value, sizeof(value), "%+d", ev->value);
        else
            g_snprintf(value, sizeof(value), "%d", ev->value);
        midi_live_draw_label(widget, cr, x + 12.0, y + 51.0, 18.0, 0.98,
                             CAIRO_FONT_WEIGHT_BOLD, value);
        char range[64];
        if(pitch)
            g_snprintf(range, sizeof(range), "CENTER 0  ·  %d…%d", ev->value_min, ev->value_max);
        else
            g_snprintf(range, sizeof(range), "%.1f%%  ·  %d…%d", n * 100.0, ev->value_min, ev->value_max);
        midi_live_draw_label(widget, cr, x + 12.0, y + 69.0, 8.3, 0.48,
                             CAIRO_FONT_WEIGHT_NORMAL, range);
        midi_live_draw_meter(cr, x + 12.0, y + 76.0,
                             MIDI_LIVE_TILE_W - 24.0, n, pitch);
    }
}

static void midi_live_draw_note_pad(GtkWidget *widget, cairo_t *cr,
                                    const MidiLiveState *state,
                                    gint64 now,
                                    double x, double y)
{
    const VjMidiEvent *ev = &state->event;
    const double age = state->update_us > 0 ?
        (double)(now - state->update_us) / 1000000.0 : 9.0;
    const double hot = CLAMP(1.0 - age / 0.85, 0.0, 1.0);
    const double velocity = CLAMP((double)ev->value / 127.0, 0.0, 1.0);

    cairo_round_rect(cr, x, y, MIDI_HELD_NOTE_TILE_W, MIDI_HELD_NOTE_TILE_H, 8.0);
    cairo_set_source_rgba(cr, 0.050, 0.060, 0.070, 0.98);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.98, 0.70, 0.20, 0.56 + 0.34 * hot);
    cairo_set_line_width(cr, 1.2 + 1.8 * hot);
    cairo_stroke(cr);

    char note[20];
    char channel[16];
    char velocity_text[20];
    g_snprintf(note, sizeof(note), "N%d", ev->control);
    g_snprintf(channel, sizeof(channel), "CH%d", ev->channel + 1);
    g_snprintf(velocity_text, sizeof(velocity_text), "v%d", ev->value);
    midi_live_draw_label(widget, cr, x + 8.0, y + 19.0, 11.5, 0.98,
                         CAIRO_FONT_WEIGHT_BOLD, note);
    midi_live_draw_label(widget, cr, x + MIDI_HELD_NOTE_TILE_W - 28.0, y + 18.0, 7.2, 0.46,
                         CAIRO_FONT_WEIGHT_NORMAL, channel);
    midi_live_draw_label(widget, cr, x + 8.0, y + 35.0, 8.0, 0.62,
                         CAIRO_FONT_WEIGHT_NORMAL, velocity_text);

    cairo_round_rect(cr, x + 8.0, y + MIDI_HELD_NOTE_TILE_H - 7.0,
                     MIDI_HELD_NOTE_TILE_W - 16.0, 3.0, 1.5);
    cairo_set_source_rgba(cr, 0.30, 0.34, 0.38, 0.42);
    cairo_fill(cr);
    cairo_round_rect(cr, x + 8.0, y + MIDI_HELD_NOTE_TILE_H - 7.0,
                     MAX(1.0, (MIDI_HELD_NOTE_TILE_W - 16.0) * velocity), 3.0, 1.5);
    cairo_set_source_rgba(cr, 0.98, 0.70, 0.20, 0.92);
    cairo_fill(cr);
}

static void midi_live_history_text(const VjMidiEvent *ev,
                                   char *dst, size_t dst_size)
{
    if(ev->type == VJ_MIDI_EVENT_NOTE_ON || ev->type == VJ_MIDI_EVENT_NOTE_OFF)
        g_snprintf(dst, dst_size, "N%d %s v%d", ev->control,
                   ev->type == VJ_MIDI_EVENT_NOTE_OFF ? "OFF" : "ON", ev->value);
    else if(ev->type == VJ_MIDI_EVENT_PITCH_BEND)
        g_snprintf(dst, dst_size, "PITCH %+d", ev->value);
    else if(ev->type == VJ_MIDI_EVENT_CC14)
        g_snprintf(dst, dst_size, "CC14 %d  %d", ev->control, ev->value);
    else if(ev->type == VJ_MIDI_EVENT_CC)
        g_snprintf(dst, dst_size, "CC %d  %d", ev->control, ev->value);
    else if(ev->type == VJ_MIDI_EVENT_PROGRAM_CHANGE)
        g_snprintf(dst, dst_size, "PROGRAM %d", ev->value);
    else
        g_snprintf(dst, dst_size, "%s %d", vj_midi_event_type_name(ev->type), ev->value);
}

static gboolean midi_recent_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);

    cairo_set_source_rgba(cr, 0.018, 0.026, 0.038, 0.97);
    cairo_paint(cr);
    cairo_set_source_rgba(cr, 0.24, 0.42, 0.54, 0.75);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 0.0, 0.5);
    cairo_line_to(cr, a.width, 0.5);
    cairo_stroke(cr);

    midi_live_draw_label(widget, cr, 16.0, 6.0 + midi_font_size(widget, 9.8), 9.8, 0.76,
                         CAIRO_FONT_WEIGHT_BOLD, "RECENT ACTIVITY  ·  HISTORY");

    const double margin = 16.0;
    const double gap = 6.0;
    const double chip_font = midi_font_size(widget, 9.2);
    const double chip_h = MAX(25.0, chip_font + 12.0);
    const double first_y = MAX(24.0, midi_font_size(widget, 9.8) + 16.0);
    const double chip_w = MAX(76.0,
        (a.width - margin * 2.0 - gap * (MIDI_RECENT_COLUMNS - 1)) /
        MIDI_RECENT_COLUMNS);
    for(int i = 0; i < MIN(self->recent_count, MIDI_RECENT_VISIBLE); i++) {
        const int row = i / MIDI_RECENT_COLUMNS;
        const int col = i % MIDI_RECENT_COLUMNS;
        const double x = margin + col * (chip_w + gap);
        const double y = first_y + row * (chip_h + 5.0);
        char text[96];
        midi_live_history_text(&self->recent[i], text, sizeof(text));

        cairo_round_rect(cr, x, y, chip_w, chip_h, 6.0);
        cairo_set_source_rgba(cr, 0.035, 0.055, 0.075, 0.98);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.24, 0.42, 0.54, i == 0 ? 0.92 : 0.48);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        midi_live_draw_label(widget, cr, x + 8.0, y + chip_h * 0.5 + chip_font * 0.35, 9.2,
                             i == 0 ? 0.90 : 0.58,
                             CAIRO_FONT_WEIGHT_NORMAL, text);
    }
    return FALSE;
}

static gboolean midi_monitor_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    const gint64 now = g_get_monotonic_time();

    cairo_set_source_rgb(cr, 0.018, 0.026, 0.038);
    cairo_paint(cr);

    const double hero_x = 16.0;
    const double hero_y = 16.0;
    const double hero_w = MIN(980.0, MAX(420.0, a.width - 32.0));
    const double hero_h = 126.0;

    cairo_round_rect(cr, hero_x, hero_y, hero_w, hero_h, 10.0);
    cairo_set_source_rgba(cr, 0.025, 0.035, 0.050, 0.98);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.28, 0.36, 0.46, 0.78);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    midi_live_draw_label(widget, cr, hero_x + 14.0, hero_y + 21.0, 10.5, 0.82,
                         CAIRO_FONT_WEIGHT_BOLD, "LAST EVENT");

    if(self->context->have_last_event) {
        const VjMidiEvent *ev = &self->context->last_event;
        const gboolean source_live = midi_live_event_source_connected(self, ev);
        const char *source_status = source_live ? "SOURCE LIVE" : "SOURCE OFFLINE  ·  HISTORY";
        const double status_x = MAX(hero_x + 116.0, hero_x + hero_w - (source_live ? 94.0 : 166.0));
        cairo_round_rect(cr, status_x, hero_y + 9.0,
                         source_live ? 80.0 : 152.0, 20.0, 6.0);
        cairo_set_source_rgba(cr,
                              source_live ? 0.07 : 0.16,
                              source_live ? 0.24 : 0.13,
                              source_live ? 0.20 : 0.06,
                              0.98);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr,
                              source_live ? 0.34 : 0.98,
                              source_live ? 0.88 : 0.70,
                              source_live ? 0.70 : 0.20,
                              0.82);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        midi_live_draw_label(widget, cr, status_x + 9.0, hero_y + 23.0, 7.8, 0.88,
                             CAIRO_FONT_WEIGHT_BOLD, source_status);
        char headline[128];
        if(ev->type == VJ_MIDI_EVENT_NOTE_ON || ev->type == VJ_MIDI_EVENT_NOTE_OFF)
            g_snprintf(headline, sizeof(headline), "%s %d  ·  CH %d",
                       ev->type == VJ_MIDI_EVENT_NOTE_OFF ? "NOTE OFF" : "NOTE ON",
                       ev->control, ev->channel + 1);
        else if(ev->type == VJ_MIDI_EVENT_CC || ev->type == VJ_MIDI_EVENT_CC14)
            g_snprintf(headline, sizeof(headline), "%s %d  ·  CH %d",
                       ev->type == VJ_MIDI_EVENT_CC14 ? "CC14" : "CC",
                       ev->control, ev->channel + 1);
        else
            g_snprintf(headline, sizeof(headline), "%s  ·  CH %d",
                       vj_midi_event_type_name(ev->type), ev->channel + 1);

        midi_live_draw_label(widget, cr, hero_x + 14.0, hero_y + 49.0, 15.0, 0.98,
                             CAIRO_FONT_WEIGHT_BOLD, headline);
        char source[160];
        g_snprintf(source, sizeof(source), "%s  ·  ALSA %d:%d",
                   ev->device_name[0] ? ev->device_name : "MIDI source",
                   ev->source_client, ev->source_port);
        midi_live_draw_label(widget, cr, hero_x + 14.0, hero_y + 69.0, 8.8, 0.50,
                             CAIRO_FONT_WEIGHT_NORMAL, source);

        char value[64];
        if(ev->type == VJ_MIDI_EVENT_PITCH_BEND)
            g_snprintf(value, sizeof(value), "%+d", ev->value);
        else
            g_snprintf(value, sizeof(value), "%d", ev->value);
        midi_live_draw_label(widget, cr, hero_x + 14.0, hero_y + 104.0, 25.0, 0.98,
                             CAIRO_FONT_WEIGHT_BOLD, value);

        const double meter_x = hero_x + 104.0;
        const double meter_w = MAX(120.0, hero_w - 314.0);
        midi_live_draw_meter(cr, meter_x, hero_y + 93.0, meter_w,
                             midi_live_norm(ev), ev->type == VJ_MIDI_EVENT_PITCH_BEND);
        char normalized[64];
        g_snprintf(normalized, sizeof(normalized), "%.1f%%", midi_live_norm(ev) * 100.0);
        midi_live_draw_label(widget, cr, meter_x, hero_y + 117.0, 8.5, 0.48,
                             CAIRO_FONT_WEIGHT_NORMAL, normalized);

        double bx, by, bw, bh;
        midi_live_capture_rect(&a, &bx, &by, &bw, &bh);
        cairo_round_rect(cr, bx, by, bw, bh, 7.0);
        cairo_set_source_rgba(cr,
                              self->live_capture_hover ? 0.12 : 0.07,
                              self->live_capture_hover ? 0.34 : 0.22,
                              self->live_capture_hover ? 0.28 : 0.22,
                              0.98);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.34, 0.88, 0.70,
                              self->live_capture_hover ? 1.0 : 0.76);
        cairo_set_line_width(cr, self->live_capture_hover ? 1.8 : 1.1);
        cairo_stroke(cr);
        midi_live_draw_label(widget, cr, bx + 14.0, by + 20.0, 9.2, 0.94,
                             CAIRO_FONT_WEIGHT_BOLD, "CAPTURE AS MAPPING");
    }
    else {
        midi_live_draw_label(widget, cr, hero_x + 14.0, hero_y + 58.0, 18.0, 0.88,
                             CAIRO_FONT_WEIGHT_BOLD, "Move a controller");
        midi_live_draw_label(widget, cr, hero_x + 14.0, hero_y + 82.0, 9.2, 0.48,
                             CAIRO_FONT_WEIGHT_NORMAL,
                             "The latest MIDI gesture will appear here and can be captured directly.");
    }

    double y = hero_y + hero_h + 30.0;

    const int held_count = self->held_notes ? (int)self->held_notes->len : 0;
    const int held_columns = midi_live_held_columns(a.width);
    const int held_capacity = held_columns * MIDI_HELD_NOTE_ROWS;
    const gboolean held_overflow = held_count > held_capacity;
    const int held_note_slots = MIN(held_count, held_capacity);
    const int held_draw_count = held_note_slots;
    const int held_rows = held_draw_count > 0 ?
        (held_draw_count + held_columns - 1) / held_columns : 1;

    char held_title[64];
    g_snprintf(held_title, sizeof(held_title), "HELD NOTES  ·  %d", held_count);
    midi_live_draw_label(widget, cr, 18.0, y, 10.5, 0.82,
                         CAIRO_FONT_WEIGHT_BOLD, held_title);
    if(held_overflow) {
        char hidden[40];
        g_snprintf(hidden, sizeof(hidden), "+%d MORE", held_count - held_capacity);
        midi_live_draw_label(widget, cr, 142.0, y, 9.0, 0.72,
                             CAIRO_FONT_WEIGHT_BOLD, hidden);
    }
    y += 12.0;

    for(int i = 0; i < held_note_slots; i++) {
        const int col = i % held_columns;
        const int row = i / held_columns;
        const double tx = 16.0 + col * (MIDI_HELD_NOTE_TILE_W + MIDI_HELD_NOTE_GAP);
        const double ty = y + 8.0 + row * (MIDI_HELD_NOTE_TILE_H + MIDI_HELD_NOTE_GAP);
        MidiLiveState *state = &g_array_index(self->held_notes, MidiLiveState, i);
        midi_live_draw_note_pad(widget, cr, state, now, tx, ty);
    }
    if(held_count == 0)
        midi_live_draw_label(widget, cr, 18.0, y + 36.0, 9.3, 0.34,
                             CAIRO_FONT_WEIGHT_NORMAL,
                             "Held notes appear here together; releasing a note removes only that note.");

    y += 16.0 + held_rows * (MIDI_HELD_NOTE_TILE_H + MIDI_HELD_NOTE_GAP) + 18.0;
    midi_live_draw_label(widget, cr, 18.0, y, 10.5, 0.82,
                         CAIRO_FONT_WEIGHT_BOLD, "ACTIVE CONTROLS");
    y += 14.0;

    const int active_count = self->active_controls ?
        MIN((int)self->active_controls->len, MIDI_ACTIVE_TILES) : 0;
    const int columns = midi_live_active_columns(a.width);
    for(int i = 0; i < active_count; i++) {
        const int col = i % columns;
        const int row = i / columns;
        const double tx = 16.0 + col * (MIDI_LIVE_TILE_W + MIDI_LIVE_TILE_GAP);
        const double ty = y + 10.0 + row * (MIDI_LIVE_TILE_H + MIDI_LIVE_TILE_GAP);
        MidiLiveState *state = &g_array_index(self->active_controls, MidiLiveState, i);
        midi_live_draw_tile(widget, cr, &state->event, state->update_us, now, tx, ty);
    }
    if(active_count == 0)
        midi_live_draw_label(widget, cr, 18.0, y + 42.0, 10.0, 0.38,
                             CAIRO_FONT_WEIGHT_NORMAL,
                             "Knobs, faders, encoders, pressure and program state appear here.");

    const int active_rows = active_count > 0 ?
        (active_count + columns - 1) / columns : 1;
    y += 18.0 + active_rows * (MIDI_LIVE_TILE_H + MIDI_LIVE_TILE_GAP) + 18.0;
    if(self->context->learning)
        midi_live_draw_label(widget, cr, 18.0, y, 9.0, 0.82,
                             CAIRO_FONT_WEIGHT_BOLD,
                             "LEARN ARMED  ·  now operate a Reloaded control to complete the route");

    return FALSE;
}

static gboolean midi_monitor_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(event->button != 1 || !self->context->have_last_event)
        return FALSE;
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    double x, y, w, h;
    midi_live_capture_rect(&a, &x, &y, &w, &h);
    if(event->x >= x && event->x <= x + w && event->y >= y && event->y <= y + h) {
        midi_capture_mapping(NULL, self);
        return TRUE;
    }
    return FALSE;
}

static gboolean midi_monitor_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    double x, y, w, h;
    midi_live_capture_rect(&a, &x, &y, &w, &h);
    const gboolean hover = self->context->have_last_event &&
                           event->x >= x && event->x <= x + w &&
                           event->y >= y && event->y <= y + h;
    if(hover != self->live_capture_hover) {
        self->live_capture_hover = hover;
        gtk_widget_queue_draw(widget);
    }
    GdkWindow *window = gtk_widget_get_window(widget);
    if(window) {
        GdkCursor *cursor = hover ?
            gdk_cursor_new_for_display(gdk_window_get_display(window), GDK_HAND2) : NULL;
        gdk_window_set_cursor(window, cursor);
        if(cursor)
            g_object_unref(cursor);
    }
    return FALSE;
}

static gboolean midi_monitor_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
    (void)event;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(self->live_capture_hover) {
        self->live_capture_hover = FALSE;
        gtk_widget_queue_draw(widget);
    }
    GdkWindow *window = gtk_widget_get_window(widget);
    if(window)
        gdk_window_set_cursor(window, NULL);
    return FALSE;
}

static gboolean midi_monitor_decay_tick(gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(!self->monitor_area || self->recent_count == 0) {
        self->monitor_decay_timer = 0;
        return G_SOURCE_REMOVE;
    }
    gtk_widget_queue_draw(self->monitor_area);
    if(self->recent_area)
        gtk_widget_queue_draw(self->recent_area);
    if(self->device_area)
        gtk_widget_queue_draw(self->device_area);
    if(self->route_area)
        gtk_widget_queue_draw(self->route_area);
    if(self->curve_area)
        gtk_widget_queue_draw(self->curve_area);
    if(self->route_live_area)
        gtk_widget_queue_draw(self->route_live_area);
    if(g_get_monotonic_time() - self->recent_time_us[0] > 1300000) {
        self->monitor_decay_timer = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}


static int midi_route_event_matches(const VjMidiMapping *mapping,
                                    const VjMidiEvent *event,
                                    gboolean require_enabled)
{
    if(!mapping || !event)
        return 0;
    if(require_enabled && !mapping->enabled)
        return 0;
    if(mapping->device && strcmp(mapping->device, VJ_MIDI_ANY_DEVICE) != 0 &&
       g_strcmp0(mapping->device, event->device_name) != 0)
        return 0;
    if(mapping->event_type != event->type) {
        if(!(mapping->event_type == VJ_MIDI_EVENT_NOTE_ON &&
             event->type == VJ_MIDI_EVENT_NOTE_OFF &&
             mapping->mode == VJ_MIDI_MODE_MOMENTARY))
            return 0;
    }
    if(mapping->channel != VJ_MIDI_ANY_CHANNEL && mapping->channel != event->channel)
        return 0;
    if(mapping->control != VJ_MIDI_ANY_CONTROL && mapping->control != event->control)
        return 0;
    return 1;
}

static MidiRouteActivity *midi_route_activity_find(GvrMidiControl *self, guint id)
{
    if(!self || !self->route_activity || id == 0)
        return NULL;
    for(guint i = 0; i < self->route_activity->len; i++) {
        MidiRouteActivity *activity = &g_array_index(self->route_activity, MidiRouteActivity, i);
        if(activity->id == id)
            return activity;
    }
    return NULL;
}

static void midi_route_activity_note(GvrMidiControl *self, const VjMidiEvent *event)
{
    if(!self || !self->context || !self->context->map || !event)
        return;
    const gint64 now = g_get_monotonic_time();
    for(guint i = 0; i < vj_midi_map_count(self->context->map); i++) {
        VjMidiMapping *mapping = vj_midi_map_get_nth(self->context->map, i);
        if(!midi_route_event_matches(mapping, event, FALSE))
            continue;
        const gboolean dispatched = mapping->enabled && self->context->dispatch_enabled &&
                                      !self->context->learning;
        const int output = dispatched ? mapping->last_output :
                                        midi_route_preview_output(mapping, event->value);
        MidiRouteActivity *activity = midi_route_activity_find(self, mapping->id);
        if(activity) {
            activity->event = *event;
            activity->output = output;
            activity->output_valid = TRUE;
            activity->update_us = now;
        } else {
            MidiRouteActivity item;
            item.id = mapping->id;
            item.event = *event;
            item.output = output;
            item.output_valid = TRUE;
            item.update_us = now;
            g_array_append_val(self->route_activity, item);
        }
    }
    if(self->route_area)
        gtk_widget_queue_draw(self->route_area);
    if(self->curve_area)
        gtk_widget_queue_draw(self->curve_area);
    if(self->route_live_area)
        gtk_widget_queue_draw(self->route_live_area);
}

static void midi_route_activity_prune(GvrMidiControl *self)
{
    if(!self || !self->route_activity)
        return;
    for(gint i = (gint)self->route_activity->len - 1; i >= 0; i--) {
        MidiRouteActivity *activity = &g_array_index(self->route_activity, MidiRouteActivity, (guint)i);
        if(!vj_midi_map_get(self->context->map, activity->id))
            g_array_remove_index(self->route_activity, (guint)i);
    }
}

static void midi_route_area_update_size(GvrMidiControl *self)
{
    if(!self || !self->route_area)
        return;
    gtk_widget_set_size_request(self->route_area, -1, MIDI_ROUTE_AREA_H);
    gtk_widget_queue_draw(self->route_area);
}

static void midi_route_world_bounds(GvrMidiControl *self,
                                    double *min_x, double *min_y,
                                    double *max_x, double *max_y)
{
    const guint count = self && self->context && self->context->map ?
                        vj_midi_map_count(self->context->map) : 0;
    const guint cols = count > 0 ? MIN((guint)MIDI_ROUTE_COLUMNS, count) : 1;
    const guint rows = count > 0 ? (count + MIDI_ROUTE_COLUMNS - 1) / MIDI_ROUTE_COLUMNS : 1;
    const double width = MIDI_ROUTE_MARGIN * 2.0 +
                         cols * MIDI_ROUTE_CARD_W +
                         (cols > 1 ? (cols - 1) * MIDI_ROUTE_CARD_GAP : 0.0);
    const double height = MIDI_ROUTE_MARGIN * 2.0 +
                          rows * MIDI_ROUTE_CARD_H +
                          (rows > 1 ? (rows - 1) * MIDI_ROUTE_ROW_GAP : 0.0);
    if(min_x) *min_x = 0.0;
    if(min_y) *min_y = 0.0;
    if(max_x) *max_x = width;
    if(max_y) *max_y = height;
}

static gboolean midi_route_card_rect(GvrMidiControl *self, int index,
                                     double *x, double *y, double *w, double *h)
{
    if(!self || index < 0 || index >= (int)vj_midi_map_count(self->context->map))
        return FALSE;
    const int col = index % MIDI_ROUTE_COLUMNS;
    const int row = index / MIDI_ROUTE_COLUMNS;
    if(x) *x = MIDI_ROUTE_MARGIN + col * (MIDI_ROUTE_CARD_W + MIDI_ROUTE_CARD_GAP);
    if(y) *y = MIDI_ROUTE_MARGIN + row * (MIDI_ROUTE_CARD_H + MIDI_ROUTE_ROW_GAP);
    if(w) *w = MIDI_ROUTE_CARD_W;
    if(h) *h = MIDI_ROUTE_CARD_H;
    return TRUE;
}

static void midi_route_view_fit(GvrMidiControl *self, GtkWidget *widget)
{
    if(!self || !widget)
        return;
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    if(a.width < 2 || a.height <= (int)MIDI_ROUTE_VIEW_TOP + 2)
        return;

    double min_x, min_y, max_x, max_y;
    midi_route_world_bounds(self, &min_x, &min_y, &max_x, &max_y);
    const double span_x = MAX(1.0, max_x - min_x);
    const double span_y = MAX(1.0, max_y - min_y);
    const double usable_w = MAX(100.0, a.width - MIDI_ROUTE_FIT_PADDING * 2.0);
    const double usable_h = MAX(80.0, a.height - MIDI_ROUTE_VIEW_TOP - MIDI_ROUTE_FIT_PADDING * 2.0);
    const double fit = MIN(1.0, MIN(usable_w / span_x, usable_h / span_y));
    self->route_view_scale = CLAMP(fit, MIDI_ROUTE_MIN_ZOOM, MIDI_ROUTE_MAX_ZOOM);
    self->route_view_x = min_x -
        (((double)a.width / self->route_view_scale) - span_x) * 0.5;
    self->route_view_y = min_y -
        (((double)(a.height - MIDI_ROUTE_VIEW_TOP) / self->route_view_scale) - span_y) * 0.5;
    self->route_view_valid = TRUE;
}

static void midi_route_view_one_to_one(GvrMidiControl *self, GtkWidget *widget)
{
    if(!self || !widget)
        return;
    self->route_view_scale = 1.0;
    self->route_view_x = 0.0;
    self->route_view_y = 0.0;
    self->route_view_valid = TRUE;
    gtk_widget_queue_draw(widget);
}

static void midi_route_widget_to_world(GvrMidiControl *self,
                                       double x, double y,
                                       double *wx, double *wy)
{
    const double scale = self && self->route_view_scale > 0.0 ? self->route_view_scale : 1.0;
    if(wx) *wx = (self ? self->route_view_x : 0.0) + x / scale;
    if(wy) *wy = (self ? self->route_view_y : 0.0) + (y - MIDI_ROUTE_VIEW_TOP) / scale;
}

static void midi_route_zoom_at(GvrMidiControl *self, GtkWidget *widget,
                               double factor, double anchor_x, double anchor_y)
{
    if(!self || !widget || anchor_y < MIDI_ROUTE_VIEW_TOP)
        return;
    if(!self->route_view_valid)
        midi_route_view_fit(self, widget);
    const double old_scale = self->route_view_scale > 0.0 ? self->route_view_scale : 1.0;
    const double local_y = anchor_y - MIDI_ROUTE_VIEW_TOP;
    const double world_x = self->route_view_x + anchor_x / old_scale;
    const double world_y = self->route_view_y + local_y / old_scale;
    const double new_scale = CLAMP(old_scale * factor, MIDI_ROUTE_MIN_ZOOM, MIDI_ROUTE_MAX_ZOOM);
    if(fabs(new_scale - old_scale) < 0.00001)
        return;
    self->route_view_scale = new_scale;
    self->route_view_x = world_x - anchor_x / new_scale;
    self->route_view_y = world_y - local_y / new_scale;
    self->route_view_valid = TRUE;
}

static void midi_route_ensure_visible(GvrMidiControl *self, int index)
{
    if(!self || !self->route_area || index < 0)
        return;
    if(!self->route_view_valid)
        midi_route_view_fit(self, self->route_area);

    GtkAllocation a;
    gtk_widget_get_allocation(self->route_area, &a);
    double x, y, w, h;
    if(!midi_route_card_rect(self, index, &x, &y, &w, &h))
        return;
    const double scale = self->route_view_scale > 0.0 ? self->route_view_scale : 1.0;
    const double visible_w = a.width / scale;
    const double visible_h = MAX(1.0, a.height - MIDI_ROUTE_VIEW_TOP) / scale;
    const double pad = 18.0 / scale;
    if(x < self->route_view_x + pad)
        self->route_view_x = x - pad;
    else if(x + w > self->route_view_x + visible_w - pad)
        self->route_view_x = x + w + pad - visible_w;
    if(y < self->route_view_y + pad)
        self->route_view_y = y - pad;
    else if(y + h > self->route_view_y + visible_h - pad)
        self->route_view_y = y + h + pad - visible_h;
}

static int midi_route_hit(GvrMidiControl *self, double px, double py)
{
    if(!self || py < MIDI_ROUTE_VIEW_TOP)
        return -1;
    double wx, wy;
    midi_route_widget_to_world(self, px, py, &wx, &wy);
    for(guint i = 0; i < vj_midi_map_count(self->context->map); i++) {
        double x, y, w, h;
        if(midi_route_card_rect(self, (int)i, &x, &y, &w, &h) &&
           wx >= x && wx <= x + w && wy >= y && wy <= y + h)
            return (int)i;
    }
    return -1;
}

static const char *midi_route_device_short(const char *device, char *buffer, size_t size)
{
    const char *src = (device && device[0]) ? device : VJ_MIDI_ANY_DEVICE;
    if(strlen(src) < 27)
        return src;
    g_snprintf(buffer, size, "%.24s…", src);
    return buffer;
}

static gboolean midi_route_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    GtkAllocation a;
    GdkRGBA fg;
    gtk_widget_get_allocation(widget, &a);
    cairo_get_fg(widget, &fg);
    if(!self->route_view_valid)
        midi_route_view_fit(self, widget);

    cairo_set_source_rgb(cr, 0.018, 0.026, 0.038);
    cairo_paint(cr);
    cairo_text(widget, cr, &fg, 14, 18, 9.5, "MAPPING ROUTES");

    const guint count = vj_midi_map_count(self->context->map);
    guint enabled_count = 0;
    for(guint i = 0; i < count; i++) {
        VjMidiMapping *m = vj_midi_map_get_nth(self->context->map, i);
        if(m && m->enabled)
            enabled_count++;
    }
    char status[96];
    g_snprintf(status, sizeof(status), "%u route%s  ·  %u enabled  ·  %.0f%%",
               count, count == 1 ? "" : "s", enabled_count,
               self->route_view_scale * 100.0);
    cairo_text(widget, cr, &fg, 150.0, 18, 8.0, status);
    if(a.width > 720)
        cairo_text(widget, cr, &fg, a.width - 350.0, 18, 7.5,
                   "Wheel zoom · drag empty space / middle-button pan");

    if(count == 0) {
        cairo_text(widget, cr, &fg, 22, 72, 12.0, "No MIDI mappings yet");
        cairo_text(widget, cr, &fg, 22, 97, 8.5,
                   "Move a controller and Capture Input, or arm Learn and operate a Reloaded control.");
        return FALSE;
    }

    cairo_save(cr);
    cairo_rectangle(cr, 0.0, MIDI_ROUTE_VIEW_TOP,
                    a.width, MAX(1.0, a.height - MIDI_ROUTE_VIEW_TOP));
    cairo_clip(cr);
    cairo_translate(cr, -self->route_view_x * self->route_view_scale,
                    MIDI_ROUTE_VIEW_TOP - self->route_view_y * self->route_view_scale);
    cairo_scale(cr, self->route_view_scale, self->route_view_scale);

    const gint64 now = g_get_monotonic_time();
    for(guint i = 0; i < count; i++) {
        VjMidiMapping *m = vj_midi_map_get_nth(self->context->map, i);
        if(!m)
            continue;
        double x, y, w, h;
        if(!midi_route_card_rect(self, (int)i, &x, &y, &w, &h))
            continue;
        MidiRouteActivity *activity = midi_route_activity_find(self, m->id);
        const gboolean hot = activity && now - activity->update_us < MIDI_ROUTE_ACTIVITY_US;
        const gboolean selected = m->id == self->selected_id;
        const gboolean hover = (int)i == self->hover_route_index;

        if(hot) {
            cairo_round_rect(cr, x - 3.0, y - 3.0, w + 6.0, h + 6.0, 11.0);
            cairo_set_source_rgba(cr, 0.18, 0.78, 0.95, m->enabled ? 0.20 : 0.09);
            cairo_fill(cr);
        }

        cairo_round_rect(cr, x, y, w, h, 9.0);
        if(!m->enabled)
            cairo_set_source_rgba(cr, 0.055, 0.065, 0.080, 0.76);
        else if(selected)
            cairo_set_source_rgba(cr, 0.075, 0.105, 0.135, 0.98);
        else
            cairo_set_source_rgba(cr, 0.035, 0.047, 0.062, 0.96);
        cairo_fill_preserve(cr);
        if(selected)
            cairo_set_source_rgba(cr, 0.18, 0.76, 0.94, 0.95);
        else if(hot && m->enabled)
            cairo_set_source_rgba(cr, 0.95, 0.68, 0.20, 0.92);
        else if(hover)
            cairo_set_source_rgba(cr, 0.30, 0.56, 0.72, 0.82);
        else
            cairo_set_source_rgba(cr, 0.26, 0.33, 0.41, m->enabled ? 0.82 : 0.42);
        cairo_set_line_width(cr, selected ? 2.0 : 1.2);
        cairo_stroke(cr);

        cairo_arc(cr, x + 14.0, y + 15.0, 4.0, 0, 2.0 * G_PI);
        if(m->enabled)
            cairo_set_source_rgba(cr, hot ? 0.96 : 0.20, hot ? 0.68 : 0.78, hot ? 0.18 : 0.92, 0.96);
        else
            cairo_set_source_rgba(cr, 0.35, 0.38, 0.42, 0.70);
        cairo_fill(cr);

        gchar *input = midi_mapping_input_text(m);
        gchar *target = midi_mapping_target_text(m);
        char devbuf[64];
        const char *device = midi_route_device_short(m->device, devbuf, sizeof(devbuf));
        const char *title = (m->name && m->name[0]) ? m->name : input;
        char titlebuf[64];
        if(strlen(title) > 29) {
            g_snprintf(titlebuf, sizeof(titlebuf), "%.26s…", title);
            title = titlebuf;
        }
        cairo_text(widget, cr, &fg, x + 25.0, y + 19.0, 9.5, title);
        cairo_text(widget, cr, &fg, x + 12.0, y + 41.0, 7.7, device);
        cairo_text(widget, cr, &fg, x + 12.0, y + 63.0, 8.2, input);

        cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, m->enabled ? 0.28 : 0.15);
        cairo_move_to(cr, x + 12.0, y + 75.0);
        cairo_line_to(cr, x + w - 12.0, y + 75.0);
        cairo_stroke(cr);
        cairo_text(widget, cr, &fg, x + 12.0, y + 96.0, 7.8, vj_midi_mode_name(m->mode));
        cairo_text(widget, cr, &fg, x + w - 88.0, y + 96.0, 7.8, target);

        g_free(input);
        g_free(target);
    }
    cairo_restore(cr);
    return FALSE;
}

static void midi_editor_set_expanded(GvrMidiControl *self, gboolean expanded)
{
    if(self->editor_revealer)
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->editor_revealer), expanded);
    if(self->editor_toggle) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->editor_toggle), expanded);
        midi_button_set_icon(self->editor_toggle, expanded ? "button_inc.png" : "button_dec.png");
        midi_set_tooltip(self->editor_toggle, expanded ?
                         "Hide the Input / Transform / Target editor." :
                         "Show the Input / Transform / Target editor.");
    }
}

static void midi_editor_toggle_toggled(GtkToggleButton *button, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    gboolean expanded = gtk_toggle_button_get_active(button);
    if(self->editor_revealer)
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->editor_revealer), expanded);
    midi_button_set_icon(GTK_WIDGET(button), expanded ? "button_inc.png" : "button_dec.png");
    midi_set_tooltip(GTK_WIDGET(button), expanded ?
                     "Hide the Input / Transform / Target editor." :
                     "Show the Input / Transform / Target editor.");
}

static void midi_selected_route_summary_update(GvrMidiControl *self, const VjMidiMapping *m)
{
    if(!self->selected_summary)
        return;
    if(!m) {
        gtk_label_set_text(GTK_LABEL(self->selected_summary), "No route selected");
        return;
    }

    gchar *input = midi_mapping_input_text(m);
    gchar *target = midi_mapping_target_text(m);
    gchar *text = g_strdup_printf("%s  ·  %s  →  %s  →  %s",
                                  input ? input : "MIDI",
                                  (m->device && m->device[0]) ? m->device : VJ_MIDI_ANY_DEVICE,
                                  vj_midi_mode_name(m->mode),
                                  target ? target : "Target");
    gtk_label_set_text(GTK_LABEL(self->selected_summary), text);
    g_free(text);
    g_free(input);
    g_free(target);
}

static void midi_route_select_index(GvrMidiControl *self, int index)
{
    VjMidiMapping *m = (index >= 0) ? vj_midi_map_get_nth(self->context->map, (guint)index) : NULL;
    if(!m)
        return;
    self->selected_id = m->id;
    midi_control_fill_editor(self, m);
    midi_selected_route_summary_update(self, m);
    midi_route_ensure_visible(self, index);
    if(self->route_area)
        gtk_widget_queue_draw(self->route_area);
    if(self->curve_area)
        gtk_widget_queue_draw(self->curve_area);
    if(self->route_live_area)
        gtk_widget_queue_draw(self->route_live_area);
}

static void midi_route_pan_begin(GvrMidiControl *self, GtkWidget *widget,
                                 guint button, double x, double y)
{
    if(!self->route_view_valid)
        midi_route_view_fit(self, widget);
    self->route_panning = TRUE;
    self->route_pan_button = button;
    self->route_pan_start_x = x;
    self->route_pan_start_y = y;
    self->route_pan_origin_x = self->route_view_x;
    self->route_pan_origin_y = self->route_view_y;
}

static gboolean midi_route_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(event->button == 2) {
        midi_route_pan_begin(self, widget, event->button, event->x, event->y);
        return TRUE;
    }
    if(event->button != 1)
        return FALSE;

    int hit = midi_route_hit(self, event->x, event->y);
    if(hit >= 0) {
        midi_route_select_index(self, hit);
        if(event->type == GDK_2BUTTON_PRESS)
            midi_editor_set_expanded(self, TRUE);
        return TRUE;
    }
    if(event->y >= MIDI_ROUTE_VIEW_TOP) {
        midi_route_pan_begin(self, widget, event->button, event->x, event->y);
        return TRUE;
    }
    return FALSE;
}

static gboolean midi_route_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(self->route_panning && event->button == self->route_pan_button) {
        self->route_panning = FALSE;
        self->route_pan_button = 0;
        return TRUE;
    }
    return FALSE;
}

static gboolean midi_route_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(self->route_panning) {
        const double scale = self->route_view_scale > 0.0 ? self->route_view_scale : 1.0;
        self->route_view_x = self->route_pan_origin_x -
                             (event->x - self->route_pan_start_x) / scale;
        self->route_view_y = self->route_pan_origin_y -
                             (event->y - self->route_pan_start_y) / scale;
        self->route_view_valid = TRUE;
        gtk_widget_queue_draw(widget);
        GdkWindow *window = gtk_widget_get_window(widget);
        if(window) {
            GdkCursor *cursor = gdk_cursor_new_for_display(gdk_window_get_display(window), GDK_FLEUR);
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
        }
        return TRUE;
    }

    int hit = midi_route_hit(self, event->x, event->y);
    if(hit != self->hover_route_index) {
        self->hover_route_index = hit;
        gtk_widget_queue_draw(widget);
    }
    GdkWindow *window = gtk_widget_get_window(widget);
    if(window) {
        GdkCursor *cursor = hit >= 0 ?
            gdk_cursor_new_for_display(gdk_window_get_display(window), GDK_HAND2) : NULL;
        gdk_window_set_cursor(window, cursor);
        if(cursor)
            g_object_unref(cursor);
    }
    return FALSE;
}

static gboolean midi_route_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    double factor = 1.0;
    if(event->direction == GDK_SCROLL_UP)
        factor = 1.14;
    else if(event->direction == GDK_SCROLL_DOWN)
        factor = 1.0 / 1.14;
    else if(event->direction == GDK_SCROLL_SMOOTH) {
        double dx = 0.0, dy = 0.0;
        if(!gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy))
            return FALSE;
        (void)dx;
        factor = pow(1.14, -CLAMP(dy, -4.0, 4.0));
    } else {
        return FALSE;
    }
    midi_route_zoom_at(self, widget, factor, event->x, event->y);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean midi_route_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
    (void)event;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    self->hover_route_index = -1;
    if(self->route_panning) {
        self->route_panning = FALSE;
        self->route_pan_button = 0;
    }
    gtk_widget_queue_draw(widget);
    GdkWindow *window = gtk_widget_get_window(widget);
    if(window)
        gdk_window_set_cursor(window, NULL);
    return FALSE;
}

static void midi_route_fit_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    midi_route_view_fit(self, self->route_area);
    gtk_widget_queue_draw(self->route_area);
}

static void midi_route_one_to_one_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    midi_route_view_one_to_one(self, self->route_area);
    if(self->selected_id) {
        for(guint i = 0; i < vj_midi_map_count(self->context->map); i++) {
            VjMidiMapping *m = vj_midi_map_get_nth(self->context->map, i);
            if(m && m->id == self->selected_id) {
                midi_route_ensure_visible(self, (int)i);
                break;
            }
        }
    }
    gtk_widget_queue_draw(self->route_area);
}

static void midi_route_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data)
{
    (void)allocation;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(!self->route_view_valid) {
        midi_route_view_fit(self, widget);
        gtk_widget_queue_draw(widget);
    }
}

static void midi_curve_node(GtkWidget *widget, cairo_t *cr, const GdkRGBA *fg,
                            double x, double y, double w, double h,
                            const char *title, const char *detail)
{
    cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, 0.07);
    cairo_round_rect(cr, x, y, w, h, 7.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, 0.28);
    cairo_stroke(cr);
    cairo_text(widget, cr, fg, x + 10, y + 20, 9.5, title);
    cairo_text(widget, cr, fg, x + 10, y + 39, 7.8, detail);
}

static void midi_route_connector(cairo_t *cr, const GdkRGBA *fg, gboolean live,
                                 double x, double y0, double y1)
{
    const double arrow_h = 7.0;
    const double arrow_w = 5.0;
    cairo_set_line_width(cr, live ? 3.0 : 1.6);
    if(live)
        cairo_set_source_rgba(cr, 0.18, 0.78, 0.95, 0.95);
    else
        cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, 0.42);
    cairo_move_to(cr, x, y0);
    cairo_line_to(cr, x, y1 - arrow_h);
    cairo_stroke(cr);

    if(live)
        cairo_set_source_rgba(cr, 0.18, 0.78, 0.95, 0.95);
    else
        cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, 0.50);
    cairo_move_to(cr, x, y1 - 1.0);
    cairo_line_to(cr, x - arrow_w, y1 - arrow_h - 1.0);
    cairo_line_to(cr, x + arrow_w, y1 - arrow_h - 1.0);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static double midi_route_input_position(const VjMidiMapping *m, int raw)
{
    if(!m || m->input_max == m->input_min)
        return 0.0;
    raw = CLAMP(raw, MIN(m->input_min, m->input_max), MAX(m->input_min, m->input_max));
    return CLAMP((double)(raw - m->input_min) /
                 (double)(m->input_max - m->input_min), 0.0, 1.0);
}

static void midi_route_native_input_range(const VjMidiMapping *m, int *lo, int *hi)
{
    int native_lo = 0, native_hi = 127;
    if(m)
        vj_midi_event_value_range(m->event_type, &native_lo, &native_hi);
    if(lo) *lo = native_lo;
    if(hi) *hi = native_hi;
}

static double midi_route_plot_input_position(const VjMidiMapping *m, int raw)
{
    int lo = 0, hi = 127;
    midi_route_native_input_range(m, &lo, &hi);
    if(hi == lo)
        return 0.0;
    raw = CLAMP(raw, lo, hi);
    return CLAMP((double)(raw - lo) / (double)(hi - lo), 0.0, 1.0);
}

static void midi_route_plot_output_range(const VjMidiMapping *m, int *lo, int *hi)
{
    int out_lo = 0, out_hi = 127;
    if(m) {
        if(m->output_limit_enabled) {
            out_lo = m->output_limit_min;
            out_hi = m->output_limit_max;
        } else {
            out_lo = MIN(m->output_min, m->output_max);
            out_hi = MAX(m->output_min, m->output_max);
        }
    }
    if(lo) *lo = out_lo;
    if(hi) *hi = out_hi;
}

static double midi_route_plot_output_position(const VjMidiMapping *m, int out)
{
    int lo = 0, hi = 127;
    midi_route_plot_output_range(m, &lo, &hi);
    if(hi == lo)
        return out >= hi ? 1.0 : 0.0;
    return CLAMP((double)(out - lo) / (double)(hi - lo), 0.0, 1.0);
}

static int midi_route_bipolar_center(const VjMidiMapping *m)
{
    if(m && m->input_min < 0 && m->input_max > 0)
        return 0;
    if(!m)
        return 0;
    return MIN(m->input_min, m->input_max) +
           (MAX(m->input_min, m->input_max) - MIN(m->input_min, m->input_max)) / 2;
}

static double midi_route_preview_normalized(const VjMidiMapping *m, int raw)
{
    if(!m || m->input_max == m->input_min)
        return 0.0;
    const int lo = MIN(m->input_min, m->input_max);
    const int hi = MAX(m->input_min, m->input_max);
    raw = CLAMP(raw, lo, hi);
    double n = (double)(raw - m->input_min) / (double)(m->input_max - m->input_min);
    n = CLAMP(n, 0.0, 1.0);
    if(m->invert)
        n = 1.0 - n;

    if(m->deadzone > 0) {
        if(m->input_min < 0 && m->input_max > 0) {
            if(ABS(raw - midi_route_bipolar_center(m)) <= m->deadzone)
                n = 0.5;
        } else if(ABS(raw - m->input_min) <= m->deadzone) {
            n = m->invert ? 1.0 : 0.0;
        }
    }
    return n;
}

static int midi_route_relative_delta(VjMidiMode mode, int value)
{
    if(mode == VJ_MIDI_MODE_RELATIVE_2C)
        return value <= 63 ? value : value - 128;
    if(value == 64 || value == 0)
        return 0;
    return value < 64 ? value : -(128 - value);
}

static int midi_mapping_special_center(VjMidiMapping *m)
{
    if(!m)
        return 0;

    m->output_center_enabled = 0;
    m->output_center = 0;
    if(m->mode != VJ_MIDI_MODE_ABSOLUTE ||
       m->event_type != VJ_MIDI_EVENT_PITCH_BEND ||
       m->action.type != VJ_MIDI_ACTION_VIMS ||
       m->action.vims_id != VIMS_FRAMERATE)
        return 1;

    const double fps = vj_gui_video_fps();
    if(fps <= 0.0)
        return 0;

    m->output_center = (int)llround(fps * 100.0);
    if(m->output_center < MIN(m->output_min, m->output_max) ||
       m->output_center > MAX(m->output_min, m->output_max))
        return 0;

    m->output_center_enabled = 1;
    return 1;
}

static void midi_context_refresh_special_centers(VjMidiContext *context)
{
    if(!context)
        return;

    const double fps = vj_gui_video_fps();
    const int fps_x100 = fps > 0.0 ? (int)llround(fps * 100.0) : 0;
    if(fps_x100 <= 0 || fps_x100 == context->special_center_fps_x100)
        return;

    for(guint i = 0; i < vj_midi_map_count(context->map); i++) {
        VjMidiMapping *m = vj_midi_map_get_nth(context->map, i);
        if(!midi_mapping_special_center(m) && m &&
           m->mode == VJ_MIDI_MODE_ABSOLUTE &&
           m->event_type == VJ_MIDI_EVENT_PITCH_BEND &&
           m->action.type == VJ_MIDI_ACTION_VIMS &&
           m->action.vims_id == VIMS_FRAMERATE) {
            vj_msg(VEEJAY_MSG_WARNING,
                   "MIDI mapping %u cannot center VIMS 335 on edit-list FPS %.2f inside output range %d..%d",
                   m->id, fps, m->output_min, m->output_max);
        }
    }
    context->special_center_fps_x100 = fps_x100;
}

static int midi_route_preview_output(const VjMidiMapping *m, int raw)
{
    if(!m)
        return 0;
    if(m->mode == VJ_MIDI_MODE_ABSOLUTE) {
        if(m->output_center_enabled && m->input_min < 0 && m->input_max > 0) {
            const int in_lo = MIN(m->input_min, m->input_max);
            const int in_hi = MAX(m->input_min, m->input_max);
            const int left_out = m->invert ? m->output_max : m->output_min;
            const int right_out = m->invert ? m->output_min : m->output_max;
            raw = CLAMP(raw, in_lo, in_hi);
            if(m->deadzone > 0 && ABS(raw) <= m->deadzone)
                return m->output_center;
            if(raw <= 0) {
                const double t = in_lo < 0 ?
                    (double)(raw - in_lo) / (double)(-in_lo) : 1.0;
                return (int)llround((double)left_out +
                                    CLAMP(t, 0.0, 1.0) *
                                    (double)(m->output_center - left_out));
            }
            const double t = in_hi > 0 ? (double)raw / (double)in_hi : 1.0;
            return (int)llround((double)m->output_center +
                                CLAMP(t, 0.0, 1.0) *
                                (double)(right_out - m->output_center));
        }
        return (int)llround((double)m->output_min +
                            midi_route_preview_normalized(m, raw) *
                            (double)(m->output_max - m->output_min));
    }
    if(m->mode == VJ_MIDI_MODE_RELATIVE || m->mode == VJ_MIDI_MODE_RELATIVE_2C) {
        int delta = midi_route_relative_delta(m->mode, raw);
        if(m->invert)
            delta = -delta;
        return CLAMP(m->last_output + delta,
                     MIN(m->output_min, m->output_max),
                     MAX(m->output_min, m->output_max));
    }
    if(m->mode == VJ_MIDI_MODE_TRIGGER)
        return m->output_max;
    if(m->mode == VJ_MIDI_MODE_MOMENTARY)
        return raw <= m->input_min ? m->output_min : m->output_max;
    return m->last_output;
}

static double midi_route_target_position(const VjMidiMapping *m, int out)
{
    if(!m || m->output_max == m->output_min)
        return out == (m ? m->output_max : 0) ? 1.0 : 0.0;
    return CLAMP((double)(out - m->output_min) /
                 (double)(m->output_max - m->output_min), 0.0, 1.0);
}

static void midi_editor_preview_mapping(GvrMidiControl *self,
                                        const VjMidiMapping *stored,
                                        VjMidiMapping *preview)
{
    *preview = *stored;
    if(!self->mode_combo || !self->input_min_spin || !self->input_max_spin ||
       !self->out_min_spin || !self->out_max_spin || !self->deadzone_spin ||
       !self->invert_check)
        return;

    preview->event_type = combo_get_int(GTK_COMBO_BOX(self->event_combo), stored->event_type);
    preview->mode = combo_get_int(GTK_COMBO_BOX(self->mode_combo), stored->mode);
    preview->input_min = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->input_min_spin));
    preview->input_max = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->input_max_spin));
    preview->output_min = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->out_min_spin));
    preview->output_max = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->out_max_spin));
    preview->deadzone = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->deadzone_spin));
    preview->invert = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->invert_check));

    if(self->action_combo && self->target_spin && self->args_entry) {
        const int action = combo_get_int(GTK_COMBO_BOX(self->action_combo), stored->action.type);
        const int target = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->target_spin));
        const char *args = gtk_entry_get_text(GTK_ENTRY(self->args_entry));
        preview->action.type = action;
        preview->action.vims_id = target;
        preview->action.bundle_id = target;
        if(action == VJ_MIDI_ACTION_RAW)
            preview->action.raw_message = (char *)args;
        else if(action == VJ_MIDI_ACTION_VIMS)
            preview->action.args_template = (char *)args;
    }
    midi_mapping_special_center(preview);
}

static gboolean midi_transform_preview_dirty(const VjMidiMapping *stored,
                                             const VjMidiMapping *preview)
{
    return stored && preview &&
           (stored->event_type != preview->event_type ||
            stored->mode != preview->mode ||
            stored->input_min != preview->input_min ||
            stored->input_max != preview->input_max ||
            stored->output_min != preview->output_min ||
            stored->output_max != preview->output_max ||
            stored->output_center_enabled != preview->output_center_enabled ||
            stored->output_center != preview->output_center ||
            stored->deadzone != preview->deadzone ||
            stored->invert != preview->invert);
}

static void midi_transform_preview_redraw(GvrMidiControl *self)
{
    if(self->curve_area)
        gtk_widget_queue_draw(self->curve_area);
    if(self->route_live_area)
        gtk_widget_queue_draw(self->route_live_area);
}

static gboolean midi_transform_preview_idle_cb(gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    self->transform_preview_idle = 0;
    midi_transform_preview_redraw(self);
    return G_SOURCE_REMOVE;
}

static void midi_transform_preview_changed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    midi_transform_preview_redraw(self);
}

static void midi_transform_adjustment_changed(GtkAdjustment *adjustment, gpointer data)
{
    (void)adjustment;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    midi_transform_preview_redraw(self);
    if(self->transform_preview_idle == 0)
        self->transform_preview_idle = g_idle_add(midi_transform_preview_idle_cb, self);
}

static void midi_transform_spin_connect(GvrMidiControl *self, GtkWidget *spin)
{
    gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_ALWAYS);
    GtkAdjustment *adjustment = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin));
    g_signal_connect_after(adjustment, "value-changed",
                           G_CALLBACK(midi_transform_adjustment_changed), self);
}

static gboolean midi_curve_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    VjMidiMapping *stored = vj_midi_map_get(self->context->map, self->selected_id);
    VjMidiMapping preview;
    VjMidiMapping *m = stored;
    if(stored) {
        midi_editor_preview_mapping(self, stored, &preview);
        m = &preview;
    }
    GtkAllocation a;
    GdkRGBA fg;
    gtk_widget_get_allocation(widget, &a);
    cairo_get_fg(widget, &fg);

    cairo_set_source_rgb(cr, 0.018, 0.026, 0.038);
    cairo_paint(cr);
    cairo_text(widget, cr, &fg, 14, 19, 9.5, "TRANSFER CURVE");

    if(!m) {
        cairo_text(widget, cr, &fg, 22, 58, 10.5, "Select a mapping to inspect its transform");
        return FALSE;
    }

    MidiRouteActivity *activity = midi_route_activity_find(self, m->id);
    const gint64 now = g_get_monotonic_time();
    const gboolean live = activity && now - activity->update_us < MIDI_ROUTE_ACTIVITY_US;
    const VjMidiEvent *event = activity ? &activity->event : NULL;
    int plot_input_min = 0, plot_input_max = 127;
    midi_route_native_input_range(m, &plot_input_min, &plot_input_max);
    const gboolean bipolar = plot_input_min < 0 && plot_input_max > 0;

    const gboolean framerate_target =
        m->action.type == VJ_MIDI_ACTION_VIMS && m->action.vims_id == VIMS_FRAMERATE;
    char subtitle[192];
    if(framerate_target) {
        g_snprintf(subtitle, sizeof(subtitle),
                   "%s · %s MIDI window %d…%d → %.2f…%.2f FPS · VIMS ×100",
                   vj_midi_event_type_name(m->event_type),
                   bipolar ? "bipolar" : "unipolar",
                   m->input_min, m->input_max,
                   (double)m->output_min / 100.0, (double)m->output_max / 100.0);
    } else {
        g_snprintf(subtitle, sizeof(subtitle), "%s · %s MIDI window %d…%d → VIMS window %d…%d",
                   vj_midi_event_type_name(m->event_type),
                   bipolar ? "bipolar" : "unipolar",
                   m->input_min, m->input_max, m->output_min, m->output_max);
    }
    cairo_text(widget, cr, &fg, 14, 39, 7.8, subtitle);
    if(stored && midi_transform_preview_dirty(stored, m)) {
        cairo_set_source_rgba(cr, 0.95, 0.68, 0.20, 0.92);
        midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_BOLD, 7.4);
        cairo_move_to(cr, MAX(14.0, a.width - 112.0), 20.0);
        cairo_show_text(cr, "EDITOR PREVIEW");
    }

    const double gx = 50.0;
    const double gy = 58.0;
    const double gw = MAX(80.0, a.width - gx - 20.0);
    const double gh = MAX(64.0, a.height - gy - 38.0);

    if(m->mode == VJ_MIDI_MODE_ABSOLUTE) {
        const double t0 = midi_route_plot_input_position(m, m->input_min);
        const double t1 = midi_route_plot_input_position(m, m->input_max);
        const double wx = gx + MIN(t0, t1) * gw;
        const double ww = fabs(t1 - t0) * gw;
        cairo_set_source_rgba(cr, 0.18, 0.78, 0.95, 0.055);
        cairo_rectangle(cr, wx, gy, ww, gh);
        cairo_fill(cr);
    }

    if(bipolar) {
        const double center_t = midi_route_plot_input_position(m, midi_route_bipolar_center(m));
        const double center_x = gx + center_t * gw;
        cairo_set_source_rgba(cr, 0.10, 0.34, 0.48, 0.10);
        cairo_rectangle(cr, gx, gy, MAX(0.0, center_x - gx), gh);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 0.10, 0.54, 0.68, 0.08);
        cairo_rectangle(cr, center_x, gy, MAX(0.0, gx + gw - center_x), gh);
        cairo_fill(cr);

        if(m->deadzone > 0) {
            const int center = midi_route_bipolar_center(m);
            const double t0 = midi_route_plot_input_position(m, center - m->deadzone);
            const double t1 = midi_route_plot_input_position(m, center + m->deadzone);
            const double zx = gx + MIN(t0, t1) * gw;
            const double zw = fabs(t1 - t0) * gw;
            cairo_set_source_rgba(cr, 0.95, 0.68, 0.20, 0.10);
            cairo_rectangle(cr, zx, gy, zw, gh);
            cairo_fill(cr);
        }
    } else if(m->deadzone > 0 && m->input_max != m->input_min) {
        const double dz = CLAMP((double)m->deadzone /
                                (double)ABS(m->input_max - m->input_min), 0.0, 1.0);
        cairo_set_source_rgba(cr, 0.95, 0.68, 0.20, 0.08);
        cairo_rectangle(cr, gx, gy, gw * dz, gh);
        cairo_fill(cr);
    }

    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.10);
    cairo_set_line_width(cr, 1.0);
    for(int i = 1; i < 4; i++) {
        const double xx = gx + gw * (double)i / 4.0;
        const double yy = gy + gh * (double)i / 4.0;
        cairo_move_to(cr, xx, gy);
        cairo_line_to(cr, xx, gy + gh);
        cairo_move_to(cr, gx, yy);
        cairo_line_to(cr, gx + gw, yy);
    }
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.34);
    cairo_move_to(cr, gx, gy + gh);
    cairo_line_to(cr, gx + gw, gy + gh);
    cairo_move_to(cr, gx, gy + gh);
    cairo_line_to(cr, gx, gy);
    cairo_stroke(cr);

    if(bipolar) {
        const int center_raw = midi_route_bipolar_center(m);
        const double center_t = midi_route_plot_input_position(m, center_raw);
        const double center_x = gx + center_t * gw;
        const int center_out = midi_route_preview_output(m, center_raw);
        const double center_y = gy +
            (1.0 - midi_route_plot_output_position(m, center_out)) * gh;
        cairo_set_source_rgba(cr, 0.18, 0.78, 0.95, 0.48);
        cairo_set_line_width(cr, 1.2);
        cairo_move_to(cr, center_x, gy);
        cairo_line_to(cr, center_x, gy + gh);
        cairo_move_to(cr, gx, center_y);
        cairo_line_to(cr, gx + gw, center_y);
        cairo_stroke(cr);
        cairo_text(widget, cr, &fg, center_x - 5.0, gy + gh + 20.0, 7.2, "0");
        if(m->mode == VJ_MIDI_MODE_ABSOLUTE) {
            char center_value[48];
            const int center_output =
                midi_route_preview_output(m, midi_route_bipolar_center(m));
            if(framerate_target)
                g_snprintf(center_value, sizeof(center_value), "%.2f FPS",
                           (double)center_output / 100.0);
            else
                g_snprintf(center_value, sizeof(center_value), "%d", center_output);
            cairo_text(widget, cr, &fg, 8.0, center_y + 3.0, 7.2, center_value);
            cairo_text(widget, cr, &fg, center_x + 7.0, center_y - 7.0, 6.8, "CENTER");
        }
    }

    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.82);
    cairo_set_line_width(cr, 2.0);
    if(m->mode == VJ_MIDI_MODE_ABSOLUTE) {
        const int samples = 128;
        for(int i = 0; i <= samples; i++) {
            const double t = (double)i / (double)samples;
            const int raw = (int)llround((double)plot_input_min +
                                         t * (double)(plot_input_max - plot_input_min));
            const int out = midi_route_preview_output(m, raw);
            const double out_n = midi_route_plot_output_position(m, out);
            const double px = gx + t * gw;
            const double py = gy + (1.0 - out_n) * gh;
            if(i == 0)
                cairo_move_to(cr, px, py);
            else
                cairo_line_to(cr, px, py);
        }
        cairo_stroke(cr);
    } else if(m->mode == VJ_MIDI_MODE_RELATIVE || m->mode == VJ_MIDI_MODE_RELATIVE_2C) {
        cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.24);
        cairo_move_to(cr, gx, gy + gh * 0.5);
        cairo_line_to(cr, gx + gw, gy + gh * 0.5);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.82);
        for(int raw = 0; raw <= 127; raw++) {
            int delta = midi_route_relative_delta(m->mode, raw);
            if(m->invert)
                delta = -delta;
            const double dn = CLAMP((double)delta / 64.0, -1.0, 1.0);
            const double px = gx + ((double)raw / 127.0) * gw;
            const double py = gy + (0.5 - dn * 0.5) * gh;
            if(raw == 0)
                cairo_move_to(cr, px, py);
            else
                cairo_line_to(cr, px, py);
        }
        cairo_stroke(cr);
    } else if(m->mode == VJ_MIDI_MODE_TOGGLE) {
        const double low_y = gy + gh * 0.78;
        const double high_y = gy + gh * 0.22;
        cairo_move_to(cr, gx, low_y);
        cairo_line_to(cr, gx + gw * 0.42, low_y);
        cairo_move_to(cr, gx + gw * 0.58, high_y);
        cairo_line_to(cr, gx + gw, high_y);
        cairo_stroke(cr);
        cairo_text(widget, cr, &fg, gx + gw * 0.43, gy + gh * 0.53, 7.2, "PRESS ↕");
    } else if(m->mode == VJ_MIDI_MODE_MOMENTARY) {
        const double step = gx + gw * 0.08;
        cairo_move_to(cr, gx, gy + gh);
        cairo_line_to(cr, step, gy + gh);
        cairo_line_to(cr, step, gy);
        cairo_line_to(cr, gx + gw, gy);
        cairo_stroke(cr);
    } else {
        cairo_move_to(cr, gx, gy);
        cairo_line_to(cr, gx + gw, gy);
        cairo_stroke(cr);
    }

    char lo[32], hi[32], out_lo[48], out_hi[48];
    int plot_output_min = 0, plot_output_max = 127;
    midi_route_plot_output_range(m, &plot_output_min, &plot_output_max);
    g_snprintf(lo, sizeof(lo), "%d", plot_input_min);
    g_snprintf(hi, sizeof(hi), "%d", plot_input_max);
    if(framerate_target) {
        g_snprintf(out_lo, sizeof(out_lo), "%.2f", (double)plot_output_min / 100.0);
        g_snprintf(out_hi, sizeof(out_hi), "%.2f FPS", (double)plot_output_max / 100.0);
    } else {
        g_snprintf(out_lo, sizeof(out_lo), "%d", plot_output_min);
        g_snprintf(out_hi, sizeof(out_hi), "%d", plot_output_max);
    }
    cairo_text(widget, cr, &fg, gx, gy + gh + 20.0, 7.2, lo);
    cairo_text(widget, cr, &fg, gx + gw - 38.0, gy + gh + 20.0, 7.2, hi);
    cairo_text(widget, cr, &fg, 8.0, gy + gh, 7.2, out_lo);
    cairo_text(widget, cr, &fg, 8.0, gy + 8.0, 7.2, out_hi);

    if(live && event) {
        const int out = midi_route_preview_output(m, event->value);
        const double raw_n = midi_route_plot_input_position(m, event->value);
        const double out_n = midi_route_plot_output_position(m, out);
        const double px = gx + raw_n * gw;
        const double py = gy + (1.0 - out_n) * gh;
        cairo_arc(cr, px, py, 6.5, 0, G_PI * 2.0);
        cairo_set_source_rgba(cr, 0.95, 0.68, 0.20, 1.0);
        cairo_fill(cr);
        char current[144];
        if(m->mode == VJ_MIDI_MODE_RELATIVE || m->mode == VJ_MIDI_MODE_RELATIVE_2C) {
            int delta = midi_route_relative_delta(m->mode, event->value);
            if(m->invert)
                delta = -delta;
            g_snprintf(current, sizeof(current), "RAW %d · DELTA %+d · VALUE %d",
                       event->value, delta, out);
        } else if(framerate_target) {
            g_snprintf(current, sizeof(current),
                       "RAW %d · NORM %.3f · %.2f FPS · VIMS %d",
                       event->value, out_n, (double)out / 100.0, out);
        } else {
            g_snprintf(current, sizeof(current), "RAW %d · NORM %.3f · VALUE %d",
                       event->value, out_n, out);
        }
        cairo_text(widget, cr, &fg, gx + 8.0, gy + 18.0, 8.0, current);
    }
    return FALSE;
}

static gboolean midi_route_live_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    VjMidiMapping *stored = vj_midi_map_get(self->context->map, self->selected_id);
    VjMidiMapping preview;
    VjMidiMapping *m = stored;
    if(stored) {
        midi_editor_preview_mapping(self, stored, &preview);
        m = &preview;
    }
    GtkAllocation a;
    GdkRGBA fg;
    gtk_widget_get_allocation(widget, &a);
    cairo_get_fg(widget, &fg);

    cairo_set_source_rgb(cr, 0.018, 0.026, 0.038);
    cairo_paint(cr);
    cairo_text(widget, cr, &fg, 14, 19, 9.5, "LIVE ROUTE");

    if(!m) {
        cairo_text(widget, cr, &fg, 22, 58, 10.5, "Select a mapping to inspect live values");
        return FALSE;
    }

    MidiRouteActivity *activity = midi_route_activity_find(self, m->id);
    const gint64 now = g_get_monotonic_time();
    const gboolean live = activity && now - activity->update_us < MIDI_ROUTE_ACTIVITY_US;
    const VjMidiEvent *event = activity ? &activity->event : NULL;
    const int out = live && event ? midi_route_preview_output(m, event->value) : m->last_output;
    const double n = midi_route_target_position(m, out);

    const gboolean framerate_target =
        m->action.type == VJ_MIDI_ACTION_VIMS && m->action.vims_id == VIMS_FRAMERATE;
    char value[64];
    if(framerate_target)
        g_snprintf(value, sizeof(value), "%.2f FPS", (double)out / 100.0);
    else
        g_snprintf(value, sizeof(value), "%d", out);
    midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_BOLD, 23.0);
    cairo_set_source_rgba(cr, live ? 0.95 : fg.red,
                          live ? 0.68 : fg.green,
                          live ? 0.20 : fg.blue,
                          live ? 1.0 : 0.74);
    cairo_move_to(cr, 16.0, 51.0);
    cairo_show_text(cr, value);
    char state[112];
    if(framerate_target) {
        if(live)
            g_snprintf(state, sizeof(state), "VIMS WIRE %d   ·   %.1f%%", out, n * 100.0);
        else
            g_snprintf(state, sizeof(state), "VIMS WIRE %d   ·   waiting for input", out);
    } else {
        if(live)
            g_snprintf(state, sizeof(state), "CURRENT TARGET VALUE   ·   %.1f%%", n * 100.0);
        else
            g_strlcpy(state, "LAST TARGET VALUE   ·   waiting for input", sizeof(state));
    }
    cairo_text(widget, cr, &fg, framerate_target ? 132.0 : 72.0, 48.0, 7.8, state);
    if(stored && midi_transform_preview_dirty(stored, m)) {
        cairo_set_source_rgba(cr, 0.95, 0.68, 0.20, 0.92);
        midi_select_font(widget, cr, CAIRO_FONT_WEIGHT_BOLD, 7.4);
        cairo_move_to(cr, MAX(14.0, a.width - 112.0), 20.0);
        cairo_show_text(cr, "EDITOR PREVIEW");
    }

    gchar *input = midi_mapping_input_text(m);
    gchar *target = midi_mapping_target_text(m);
    char input_detail[128];
    char transform_detail[128];
    char target_detail[128];
    if(live && event)
        g_snprintf(input_detail, sizeof(input_detail), "%s   ·   %d", input, event->value);
    else
        g_snprintf(input_detail, sizeof(input_detail), "%s   ·   %d…%d", input, m->input_min, m->input_max);
    if(live && event && (m->mode == VJ_MIDI_MODE_RELATIVE || m->mode == VJ_MIDI_MODE_RELATIVE_2C)) {
        int delta = midi_route_relative_delta(m->mode, event->value);
        if(m->invert)
            delta = -delta;
        g_snprintf(transform_detail, sizeof(transform_detail), "%s   ·   delta %+d   ·   %.3f",
                   vj_midi_mode_name(m->mode), delta, n);
    } else if(live && event && m->mode == VJ_MIDI_MODE_TRIGGER) {
        g_snprintf(transform_detail, sizeof(transform_detail),
                   "Trigger   ·   fixed output %d", out);
    } else if(live && event) {
        g_snprintf(transform_detail, sizeof(transform_detail), "%s   ·   %.3f",
                   vj_midi_mode_name(m->mode), n);
    } else {
        g_snprintf(transform_detail, sizeof(transform_detail), "%s   ·   %d…%d → %d…%d",
                   vj_midi_mode_name(m->mode), m->input_min, m->input_max, m->output_min, m->output_max);
    }
    const int controlled_arg =
        m->action.type == VJ_MIDI_ACTION_VIMS
            ? midi_vims_controlled_arg_index(m->action.args_template)
            : -1;
    if(controlled_arg >= 0) {
        if(m->action.args_template && strstr(m->action.args_template, "$NORM")) {
            g_snprintf(target_detail, sizeof(target_detail),
                       "%s   ·   ARG %d = %.3f",
                       target, controlled_arg + 1, n);
        } else if(m->action.args_template && strstr(m->action.args_template, "$RAW") &&
                  live && event) {
            g_snprintf(target_detail, sizeof(target_detail),
                       "%s   ·   ARG %d = %d",
                       target, controlled_arg + 1, event->value);
        } else if(framerate_target) {
            g_snprintf(target_detail, sizeof(target_detail),
                       "%s   ·   ARG %d = %d   ·   %.2f fps",
                       target, controlled_arg + 1, out, (double)out / 100.0);
        } else {
            g_snprintf(target_detail, sizeof(target_detail),
                       "%s   ·   ARG %d = %d",
                       target, controlled_arg + 1, out);
        }
    } else {
        g_snprintf(target_detail, sizeof(target_detail), "%s   ·   %d", target, out);
    }

    const double x = 16.0;
    const double w = MAX(120.0, a.width - 32.0);
    const double h = 44.0;
    const double y0 = 64.0;
    const double available_gap = ((double)a.height - y0 - 3.0 * h) * 0.5;
    const double gap = CLAMP(available_gap, 22.0, 34.0);
    const double y1 = y0 + h + gap;
    const double y2 = y1 + h + gap;
    midi_curve_node(widget, cr, &fg, x, y0, w, h, "MIDI INPUT", input_detail);
    midi_curve_node(widget, cr, &fg, x, y1, w, h, "TRANSFORM", transform_detail);
    midi_curve_node(widget, cr, &fg, x, y2, w, h, "TARGET", target_detail);

    const double cx = x + w * 0.5;
    midi_route_connector(cr, &fg, live, cx, y0 + h, y1);
    midi_route_connector(cr, &fg, live, cx, y1 + h, y2);

    g_free(input);
    g_free(target);
    return FALSE;
}

static void midi_signal_paned_size_allocate(GtkWidget *widget,
                                             GtkAllocation *allocation,
                                             gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(self->signal_split_initialized || allocation->width < 300)
        return;
    gtk_paned_set_position(GTK_PANED(widget), (gint)lrint(allocation->width * 0.58));
    self->signal_split_initialized = TRUE;
}

static void midi_mapping_paned_size_allocate(GtkWidget *widget,
                                              GtkAllocation *allocation,
                                              gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(self->mapping_split_initialized || allocation->height < 520)
        return;

    const gint lower_height =
        MIN(430, MAX(300, allocation->height - MIDI_ROUTE_AREA_H));
    gtk_paned_set_position(GTK_PANED(widget),
                           MAX(MIDI_ROUTE_AREA_H, allocation->height - lower_height));
    self->mapping_split_initialized = TRUE;
}

static void midi_control_recent_push(GvrMidiControl *self, const VjMidiEvent *event)
{
    const gint64 now = g_get_monotonic_time();
    midi_live_update_held_note(self, event, now);
    midi_live_update_active_control(self, event, now);

    int move = MIN(self->recent_count, MIDI_MONITOR_EVENTS - 1);
    if(move > 0) {
        memmove(&self->recent[1], &self->recent[0], (size_t) move * sizeof(VjMidiEvent));
        memmove(&self->recent_time_us[1], &self->recent_time_us[0],
                (size_t) move * sizeof(gint64));
    }
    self->recent[0] = *event;
    self->recent_time_us[0] = now;
    self->recent_count = MIN(self->recent_count + 1, MIDI_MONITOR_EVENTS);
    if(self->monitor_decay_timer == 0)
        self->monitor_decay_timer = g_timeout_add(40, midi_monitor_decay_tick, self);
    midi_live_update_canvas_size(self, gtk_widget_get_allocated_width(self->monitor_area));
}

void gvr_midi_control_midi_event(GtkWidget *widget, const VjMidiEvent *event)
{
    if(!GVR_IS_MIDI_CONTROL(widget) || !event)
        return;
    GvrMidiControl *self = GVR_MIDI_CONTROL(widget);
    midi_control_recent_push(self, event);
    midi_route_activity_note(self, event);
    if(self->capture_button)
        gtk_widget_set_sensitive(self->capture_button, TRUE);
    if(self->mapping_empty_label) {
        if(self->context && self->context->learning)
            gtk_label_set_text(GTK_LABEL(self->mapping_empty_label),
                               "MIDI input captured for Learn. Now operate a Reloaded control; the completed route will appear here.");
        else
            gtk_label_set_text(GTK_LABEL(self->mapping_empty_label),
                               "MIDI input detected. Use Capture Input to create a disabled editable route.");
    }
    gtk_widget_queue_draw(self->monitor_area);
    if(self->recent_area)
        gtk_widget_queue_draw(self->recent_area);
    gtk_widget_queue_draw(self->device_area);
}

void gvr_midi_control_set_learning(GtkWidget *widget, gboolean learning)
{
    if(!GVR_IS_MIDI_CONTROL(widget))
        return;
    GvrMidiControl *self = GVR_MIDI_CONTROL(widget);
    if(GTK_IS_TOGGLE_BUTTON(self->learn_button) &&
       gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->learn_button)) != learning)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->learn_button), learning);
    if(self->mapping_empty_label && vj_midi_map_count(self->context->map) == 0) {
        gtk_label_set_text(GTK_LABEL(self->mapping_empty_label),
                           learning ?
                           "Learn is armed. Move/press a MIDI control, then operate a Reloaded control." :
                           "Move/press a controller, then use Capture Input — or enable Learn and operate a Reloaded control.");
    }
    gtk_widget_queue_draw(self->monitor_area);
}

static gchar *midi_mapping_input_text(const VjMidiMapping *m)
{
    if(!m)
        return g_strdup("");
    if(m->channel == VJ_MIDI_ANY_CHANNEL)
        return g_strdup_printf("%s / %d", vj_midi_event_type_name(m->event_type), m->control);
    return g_strdup_printf("%s Ch%d / %d", vj_midi_event_type_name(m->event_type),
                           m->channel + 1, m->control);
}

static gchar *midi_mapping_target_text(const VjMidiMapping *m)
{
    if(!m)
        return g_strdup("");
    switch(m->action.type) {
        case VJ_MIDI_ACTION_VIMS:
            return g_strdup_printf("VIMS %03d", m->action.vims_id);
        case VJ_MIDI_ACTION_BUNDLE:
            return g_strdup_printf("Bundle %03d", m->action.bundle_id);
        case VJ_MIDI_ACTION_RAW:
            return g_strdup("Raw VIMS");
        default:
            return g_strdup("");
    }
}

void gvr_midi_control_refresh(GtkWidget *widget)
{
    if(!GVR_IS_MIDI_CONTROL(widget))
        return;
    GvrMidiControl *self = GVR_MIDI_CONTROL(widget);
    gtk_list_store_clear(self->store);
    GtkTreeIter selected_iter;
    gboolean have_selected_iter = FALSE;

    for(guint i = 0; i < vj_midi_map_count(self->context->map); i++) {
        VjMidiMapping *m = vj_midi_map_get_nth(self->context->map, i);
        GtkTreeIter iter;
        gchar *input = midi_mapping_input_text(m);
        gchar *target = midi_mapping_target_text(m);
        gtk_list_store_append(self->store, &iter);
        gtk_list_store_set(self->store, &iter,
                           MAP_COL_ID, m->id,
                           MAP_COL_ENABLED, m->enabled,
                           MAP_COL_DEVICE, m->device ? m->device : VJ_MIDI_ANY_DEVICE,
                           MAP_COL_INPUT, input,
                           MAP_COL_MODE, vj_midi_mode_name(m->mode),
                           MAP_COL_TARGET, target,
                           -1);
        if(m->id == self->selected_id) {
            selected_iter = iter;
            have_selected_iter = TRUE;
        }
        g_free(input);
        g_free(target);
    }
    if(have_selected_iter) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->tree));
        gtk_tree_selection_select_iter(selection, &selected_iter);
    }
    if(self->mapping_stack)
        gtk_stack_set_visible_child_name(GTK_STACK(self->mapping_stack),
                                         vj_midi_map_count(self->context->map) > 0 ? "routes" : "empty");

    if(vj_midi_map_count(self->context->map) > 0 &&
       !vj_midi_map_get(self->context->map, self->selected_id)) {
        VjMidiMapping *first = vj_midi_map_get_nth(self->context->map, 0);
        if(first) {
            self->selected_id = first->id;
            midi_control_fill_editor(self, first);
        }
    } else if(vj_midi_map_count(self->context->map) == 0) {
        self->selected_id = 0;
    }

    midi_selected_route_summary_update(self,
        self->selected_id ? vj_midi_map_get(self->context->map, self->selected_id) : NULL);
    midi_route_activity_prune(self);
    midi_route_area_update_size(self);
    if(self->route_area && self->selected_id) {
        for(guint i = 0; i < vj_midi_map_count(self->context->map); i++) {
            VjMidiMapping *m = vj_midi_map_get_nth(self->context->map, i);
            if(m && m->id == self->selected_id) {
                midi_route_ensure_visible(self, (int)i);
                break;
            }
        }
    }
    if(self->route_area)
        gtk_widget_queue_draw(self->route_area);
    if(self->curve_area)
        gtk_widget_queue_draw(self->curve_area);
    if(self->route_live_area)
        gtk_widget_queue_draw(self->route_live_area);
}

static void combo_set_int(GtkComboBox *combo, int value)
{
    char id[32];
    g_snprintf(id, sizeof(id), "%d", value);
    gtk_combo_box_set_active_id(combo, id);
}

static int combo_get_int(GtkComboBox *combo, int fallback)
{
    const char *id = gtk_combo_box_get_active_id(combo);
    return id ? atoi(id) : fallback;
}

static void midi_show_error(GvrMidiControl *self, const char *title, const char *message)
{
    GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(self));
    GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_CLOSE,
                                                "%s", message ? message : "Invalid MIDI mapping");
    if(title)
        gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}


static gboolean midi_primary_value_token(const char *token)
{
    return token &&
           (g_strcmp0(token, "$VALUE") == 0 ||
            g_strcmp0(token, "$NORM") == 0 ||
            g_strcmp0(token, "$RAW") == 0);
}

static GPtrArray *midi_vims_argument_tokens(const char *args)
{
    GPtrArray *tokens = g_ptr_array_new_with_free_func(g_free);
    const char *p = args ? args : "";

    while(*p) {
        while(*p && g_ascii_isspace(*p))
            p++;
        if(!*p)
            break;

        const char *start = p;
        char quote = 0;
        gboolean escaped = FALSE;
        while(*p) {
            const char c = *p;
            if(escaped) {
                escaped = FALSE;
                p++;
                continue;
            }
            if(c == '\\') {
                escaped = TRUE;
                p++;
                continue;
            }
            if(quote) {
                if(c == quote)
                    quote = 0;
                p++;
                continue;
            }
            if(c == '\'' || c == '"') {
                quote = c;
                p++;
                continue;
            }
            if(g_ascii_isspace(c))
                break;
            p++;
        }

        g_ptr_array_add(tokens, g_strndup(start, (gsize)(p - start)));
    }

    return tokens;
}


static int midi_vims_controlled_arg_index(const char *args)
{
    GPtrArray *tokens = midi_vims_argument_tokens(args);
    int index = -1;
    int count = 0;

    for(guint i = 0; i < tokens->len; i++) {
        const char *token = g_ptr_array_index(tokens, i);
        if(midi_primary_value_token(token)) {
            index = (int)i;
            count++;
        }
    }

    g_ptr_array_free(tokens, TRUE);
    return count == 1 ? index : -1;
}

static char *midi_vims_argument_join(GPtrArray *tokens)
{
    GString *out = g_string_new(NULL);
    for(guint i = 0; tokens && i < tokens->len; i++) {
        const char *token = g_ptr_array_index(tokens, i);
        if(i)
            g_string_append_c(out, ' ');
        g_string_append(out, token ? token : "");
    }
    return g_string_free(out, FALSE);
}

static const char *midi_controlled_value_token_for_target(GvrMidiControl *self,
                                                           GPtrArray *tokens)
{
    const int target =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->target_spin));
    if(target == VIMS_CHAIN_ENTRY_SET_NARG_VAL)
        return "$NORM";

    for(guint i = 0; tokens && i < tokens->len; i++) {
        const char *token = g_ptr_array_index(tokens, i);
        if(midi_primary_value_token(token))
            return token;
    }
    return "$VALUE";
}

static char *midi_controlled_arg_frozen_value(GvrMidiControl *self,
                                               const VjMidiMapping *mapping,
                                               const char *token)
{
    MidiRouteActivity *activity =
        mapping ? midi_route_activity_find(self, mapping->id) : NULL;
    const int output = activity && activity->output_valid
        ? activity->output
        : (mapping ? mapping->last_output : 0);

    if(g_strcmp0(token, "$NORM") == 0) {
        const double n = mapping
            ? midi_route_target_position(mapping, output)
            : 0.0;
        return g_strdup_printf("%.6f", n);
    }

    if(g_strcmp0(token, "$RAW") == 0) {
        int raw = 0;
        if(activity)
            raw = activity->event.value;
        else if(mapping && self->context && self->context->have_last_event &&
                midi_route_event_matches(mapping,
                                         &self->context->last_event,
                                         FALSE))
            raw = self->context->last_event.value;
        return g_strdup_printf("%d", raw);
    }

    return g_strdup_printf("%d", output);
}

static gboolean midi_mode_requires_dynamic_argument(GvrMidiControl *self)
{
    const int mode = combo_get_int(GTK_COMBO_BOX(self->mode_combo),
                                   VJ_MIDI_MODE_ABSOLUTE);
    return mode == VJ_MIDI_MODE_ABSOLUTE ||
           mode == VJ_MIDI_MODE_RELATIVE ||
           mode == VJ_MIDI_MODE_RELATIVE_2C ||
           mode == VJ_MIDI_MODE_MOMENTARY ||
           mode == VJ_MIDI_MODE_TOGGLE;
}

static void midi_controlled_arg_refresh(GvrMidiControl *self)
{
    if(!self || !self->controlled_arg_combo || !self->args_entry)
        return;

    const int action =
        combo_get_int(GTK_COMBO_BOX(self->action_combo), VJ_MIDI_ACTION_VIMS);
    const int target =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->target_spin));
    const char *args = gtk_entry_get_text(GTK_ENTRY(self->args_entry));
    int advertised_params = 0;
    const char *advertised_format = "";
    const char *advertised_description = "";
    const gboolean have_metadata =
        action == VJ_MIDI_ACTION_VIMS &&
        vj_gui_vims_get_event_metadata(target,
                                       &advertised_params,
                                       &advertised_format,
                                       &advertised_description);
    GPtrArray *tokens = midi_vims_argument_tokens(args);
    int controlled = -1;
    int controlled_count = 0;

    for(guint i = 0; i < tokens->len; i++) {
        const char *token = g_ptr_array_index(tokens, i);
        if(midi_primary_value_token(token)) {
            controlled = (int)i;
            controlled_count++;
        }
    }

    const int expected_params = have_metadata ? advertised_params : (int)tokens->len;

    if(action == VJ_MIDI_ACTION_VIMS && controlled_count == 1) {
        const char *required = NULL;
        if(target == VIMS_CHAIN_ENTRY_SET_NARG_VAL)
            required = "$NORM";
        else if(target == VIMS_CHAIN_ENTRY_SET_ARG_VAL || target == VIMS_FRAMERATE)
            required = "$VALUE";

        if(required) {
            char *token = g_ptr_array_index(tokens, controlled);
            if(g_strcmp0(token, required) != 0) {
                g_free(token);
                g_ptr_array_index(tokens, controlled) = g_strdup(required);
                char *rewritten = midi_vims_argument_join(tokens);
                self->controlled_arg_syncing = TRUE;
                gtk_entry_set_text(GTK_ENTRY(self->args_entry), rewritten);
                self->controlled_arg_syncing = FALSE;
                g_free(rewritten);
            }
        }
    }

    if(action == VJ_MIDI_ACTION_VIMS &&
       midi_mode_requires_dynamic_argument(self) &&
       controlled_count == 0 && expected_params > 0) {
        if(expected_params == 1 && tokens->len == 0) {
            g_ptr_array_add(tokens,
                            g_strdup(target == VIMS_CHAIN_ENTRY_SET_NARG_VAL
                                     ? "$NORM" : "$VALUE"));
            controlled = 0;
            controlled_count = 1;
        } else if((int)tokens->len == expected_params) {
            const int default_index = expected_params - 1;
            char *old = g_ptr_array_index(tokens, default_index);
            g_free(old);
            g_ptr_array_index(tokens, default_index) =
                g_strdup(target == VIMS_CHAIN_ENTRY_SET_NARG_VAL
                         ? "$NORM" : "$VALUE");
            controlled = default_index;
            controlled_count = 1;
        }

        if(controlled_count == 1) {
            char *rewritten = midi_vims_argument_join(tokens);
            self->controlled_arg_syncing = TRUE;
            gtk_entry_set_text(GTK_ENTRY(self->args_entry), rewritten);
            self->controlled_arg_syncing = FALSE;
            g_free(rewritten);
        }
    }

    self->controlled_arg_syncing = TRUE;
    gtk_combo_box_text_remove_all(
        GTK_COMBO_BOX_TEXT(self->controlled_arg_combo));
    gtk_combo_box_text_append(
        GTK_COMBO_BOX_TEXT(self->controlled_arg_combo),
        "none", "None / fixed arguments");

    for(guint i = 0; i < tokens->len; i++) {
        const char *token = g_ptr_array_index(tokens, i);
        char id[16];
        char *label = NULL;
        g_snprintf(id, sizeof(id), "%u", i);
        label = g_strdup_printf("Argument %u · %s%s",
                                i + 1,
                                token,
                                (int)i == controlled ? "   ← MIDI" : "");
        gtk_combo_box_text_append(
            GTK_COMBO_BOX_TEXT(self->controlled_arg_combo),
            id, label);
        g_free(label);
    }

    if(controlled_count == 1) {
        char id[16];
        g_snprintf(id, sizeof(id), "%d", controlled);
        gtk_combo_box_set_active_id(
            GTK_COMBO_BOX(self->controlled_arg_combo), id);
    } else {
        gtk_combo_box_set_active_id(
            GTK_COMBO_BOX(self->controlled_arg_combo), "none");
    }

    const gboolean can_choose =
        action == VJ_MIDI_ACTION_VIMS && tokens->len > 0;
    const gboolean forced_single =
        can_choose && expected_params == 1 && tokens->len == 1 && controlled_count == 1;
    gtk_widget_set_sensitive(self->controlled_arg_combo,
                             can_choose && !forced_single);

    char *tooltip = NULL;
    if(action != VJ_MIDI_ACTION_VIMS) {
        tooltip = g_strdup(
            "Controlled argument selection applies to parameterized VIMS Event actions.");
    } else if(tokens->len == 0) {
        tooltip = have_metadata && advertised_params > 0
            ? g_strdup_printf("VIMS %03d advertises %d argument%s%s%s. Enter the fixed arguments; MIDI defaults to the last argument.",
                              target, advertised_params,
                              advertised_params == 1 ? "" : "s",
                              advertised_format && *advertised_format ? " · " : "",
                              advertised_format && *advertised_format ? advertised_format : "")
            : g_strdup("Enter the VIMS arguments first; each argument then becomes selectable.");
    } else if(forced_single) {
        tooltip = g_strdup_printf(
            "VIMS %03d has one argument. This continuous MIDI mapping necessarily controls Argument 1.",
            target);
    } else if(have_metadata && advertised_params != (int)tokens->len) {
        tooltip = g_strdup_printf(
            "VIMS %03d advertises %d argument%s, but this template currently contains %u. Fix the argument list before applying the mapping.",
            target, advertised_params, advertised_params == 1 ? "" : "s", tokens->len);
    } else if(have_metadata) {
        tooltip = g_strdup_printf(
            "%s%sVIMS %03d · %d argument%s%s%s. Choose which argument receives the MIDI value; the last argument is the default for newly parameterized mappings.",
            advertised_description && *advertised_description ? advertised_description : "",
            advertised_description && *advertised_description ? " · " : "",
            target, advertised_params, advertised_params == 1 ? "" : "s",
            advertised_format && *advertised_format ? " · " : "",
            advertised_format && *advertised_format ? advertised_format : "");
    } else {
        tooltip = g_strdup(
            "Choose which VIMS argument receives the MIDI-controlled value. Moving it freezes the previous dynamic argument at its current value.");
    }
    gtk_widget_set_tooltip_text(self->controlled_arg_combo, tooltip);
    g_free(tooltip);

    self->controlled_arg_syncing = FALSE;
    g_ptr_array_free(tokens, TRUE);
}

static void midi_controlled_arg_changed(GtkComboBox *combo, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(!self || self->controlled_arg_syncing)
        return;
    if(combo_get_int(GTK_COMBO_BOX(self->action_combo),
                     VJ_MIDI_ACTION_VIMS) != VJ_MIDI_ACTION_VIMS)
        return;

    const char *active_id = gtk_combo_box_get_active_id(combo);
    const int new_index =
        active_id && g_strcmp0(active_id, "none") != 0
            ? atoi(active_id)
            : -1;
    const char *args = gtk_entry_get_text(GTK_ENTRY(self->args_entry));
    GPtrArray *tokens = midi_vims_argument_tokens(args);
    if(new_index >= (int)tokens->len) {
        g_ptr_array_free(tokens, TRUE);
        return;
    }

    VjMidiMapping *mapping =
        self->context
            ? vj_midi_map_get(self->context->map, self->selected_id)
            : NULL;
    char *dynamic_token =
        g_strdup(midi_controlled_value_token_for_target(self, tokens));

    for(guint i = 0; i < tokens->len; i++) {
        char *token = g_ptr_array_index(tokens, i);
        if((int)i == new_index) {
            if(!midi_primary_value_token(token) ||
               g_strcmp0(token, dynamic_token) != 0) {
                g_free(token);
                g_ptr_array_index(tokens, i) = g_strdup(dynamic_token);
            }
            continue;
        }

        if(midi_primary_value_token(token)) {
            char *frozen =
                midi_controlled_arg_frozen_value(self, mapping, token);
            g_free(token);
            g_ptr_array_index(tokens, i) = frozen;
        }
    }

    char *rewritten = midi_vims_argument_join(tokens);
    g_free(dynamic_token);
    self->controlled_arg_syncing = TRUE;
    gtk_entry_set_text(GTK_ENTRY(self->args_entry), rewritten);
    self->controlled_arg_syncing = FALSE;
    g_free(rewritten);
    g_ptr_array_free(tokens, TRUE);
    midi_controlled_arg_refresh(self);
}

static void midi_args_entry_changed(GtkEditable *editable, gpointer data)
{
    (void)editable;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(!self || self->controlled_arg_syncing)
        return;
    midi_controlled_arg_refresh(self);
    midi_transform_preview_changed(GTK_WIDGET(editable), self);
}

static void midi_action_editor_sync(GvrMidiControl *self)
{
    int action = combo_get_int(GTK_COMBO_BOX(self->action_combo), VJ_MIDI_ACTION_VIMS);
    GtkAdjustment *adjustment = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(self->target_spin));

    if(action == VJ_MIDI_ACTION_BUNDLE) {
        gtk_adjustment_set_lower(adjustment, VJ_MIDI_BUNDLE_MIN_ID);
        gtk_adjustment_set_upper(adjustment, VJ_MIDI_BUNDLE_MAX_ID);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->target_spin),
                                  CLAMP((int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->target_spin)),
                                        VJ_MIDI_BUNDLE_MIN_ID, VJ_MIDI_BUNDLE_MAX_ID));
        combo_set_int(GTK_COMBO_BOX(self->mode_combo), VJ_MIDI_MODE_TRIGGER);
        gtk_widget_set_sensitive(self->mode_combo, FALSE);
        gtk_widget_set_sensitive(self->target_spin, TRUE);
        gtk_widget_set_sensitive(self->args_entry, FALSE);
        gtk_widget_set_sensitive(self->out_min_spin, FALSE);
        gtk_widget_set_sensitive(self->out_max_spin, FALSE);
        gtk_widget_set_sensitive(self->deadzone_spin, FALSE);
        gtk_widget_set_sensitive(self->invert_check, FALSE);
        gtk_widget_set_tooltip_text(self->target_spin, "Existing Reloaded VIMS bundle selector (500-599).");
        gtk_entry_set_placeholder_text(GTK_ENTRY(self->args_entry), "Bundle actions do not use arguments");
    } else if(action == VJ_MIDI_ACTION_RAW) {
        gtk_widget_set_sensitive(self->mode_combo, TRUE);
        gtk_widget_set_sensitive(self->out_min_spin, TRUE);
        gtk_widget_set_sensitive(self->out_max_spin, TRUE);
        gtk_widget_set_sensitive(self->deadzone_spin, TRUE);
        gtk_widget_set_sensitive(self->invert_check, TRUE);
        gtk_adjustment_set_lower(adjustment, VJ_MIDI_VIMS_MIN_ID);
        gtk_adjustment_set_upper(adjustment, VJ_MIDI_VIMS_MAX_ID);
        gtk_widget_set_sensitive(self->target_spin, FALSE);
        gtk_widget_set_sensitive(self->args_entry, TRUE);
        gtk_widget_set_tooltip_text(self->target_spin, "Unused for Raw VIMS actions.");
        gtk_entry_set_placeholder_text(GTK_ENTRY(self->args_entry), "Example: 361:0 2 1 $VALUE;");
    } else {
        gtk_widget_set_sensitive(self->mode_combo, TRUE);
        gtk_widget_set_sensitive(self->out_min_spin, TRUE);
        gtk_widget_set_sensitive(self->out_max_spin, TRUE);
        gtk_widget_set_sensitive(self->deadzone_spin, TRUE);
        gtk_widget_set_sensitive(self->invert_check, TRUE);
        gtk_adjustment_set_lower(adjustment, VJ_MIDI_VIMS_MIN_ID);
        gtk_adjustment_set_upper(adjustment, VJ_MIDI_VIMS_MAX_ID);
        gtk_widget_set_sensitive(self->target_spin, TRUE);
        gtk_widget_set_sensitive(self->args_entry, TRUE);
        gtk_widget_set_tooltip_text(self->target_spin, "Three-digit selector in the current VIMS protocol range.");
        gtk_entry_set_placeholder_text(GTK_ENTRY(self->args_entry), "Example: 0 2 1 $VALUE  — tokens: $VALUE $NORM $RAW $CHANNEL $CHANNEL0 $CONTROL");
    }
    midi_controlled_arg_refresh(self);
}

static void midi_target_hint_update(GvrMidiControl *self)
{
    if(combo_get_int(GTK_COMBO_BOX(self->action_combo), VJ_MIDI_ACTION_VIMS) != VJ_MIDI_ACTION_VIMS)
        return;

    const int target = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->target_spin));
    int params = 0;
    const char *format = "";
    const char *description = "";
    const gboolean have_metadata =
        vj_gui_vims_get_event_metadata(target, &params, &format, &description);

    if(target == VIMS_FRAMERATE) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(self->args_entry),
                                       "Framerate wire value: $VALUE  (FPS ×100)");
        char *tip = g_strdup_printf("%s%sVIMS 335 expects framerate multiplied by 100; 25 fps is sent as 2500. MIDI Learn from Reloaded's framerate control scales the GTK range to VIMS wire units automatically.",
                                    description && *description ? description : "",
                                    description && *description ? " · " : "");
        gtk_widget_set_tooltip_text(self->args_entry, tip);
        g_free(tip);
    } else if(target == VIMS_CHAIN_ENTRY_SET_ARG_VAL) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(self->args_entry),
                                       "FX parameter: 0 <entry> <parameter> $VALUE");
        gtk_widget_set_tooltip_text(self->args_entry,
                                    "$VALUE is replaced by the transformed MIDI value. The VIMS output range must match the FX parameter range.");
    } else if(target == VIMS_CHAIN_ENTRY_SET_NARG_VAL) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(self->args_entry),
                                       "Normalized FX parameter: 0 <entry> <parameter> $NORM");
        gtk_widget_set_tooltip_text(self->args_entry,
                                    "$NORM is replaced by the transformed normalized MIDI value from 0.000000 to 1.000000.");
    } else if(have_metadata && params == 0) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(self->args_entry),
                                       "This VIMS event has no arguments");
        char *tip = g_strdup_printf("%s%sVIMS %03d takes no arguments and is normally used as a Trigger mapping.",
                                    description && *description ? description : "",
                                    description && *description ? " · " : "",
                                    target);
        gtk_widget_set_tooltip_text(self->args_entry, tip);
        g_free(tip);
    } else if(have_metadata && params == 1) {
        char *placeholder = g_strdup_printf("1 argument · MIDI controls Argument 1%s%s",
                                            format && *format ? " · " : "",
                                            format && *format ? format : "");
        gtk_entry_set_placeholder_text(GTK_ENTRY(self->args_entry), placeholder);
        g_free(placeholder);
        char *tip = g_strdup_printf("%s%sVIMS %03d has one argument. Continuous MIDI mappings use $VALUE here by default.",
                                    description && *description ? description : "",
                                    description && *description ? " · " : "",
                                    target);
        gtk_widget_set_tooltip_text(self->args_entry, tip);
        g_free(tip);
    } else if(have_metadata && params > 1) {
        char *placeholder = g_strdup_printf("%d arguments · MIDI defaults to Argument %d%s%s",
                                            params, params,
                                            format && *format ? " · " : "",
                                            format && *format ? format : "");
        gtk_entry_set_placeholder_text(GTK_ENTRY(self->args_entry), placeholder);
        g_free(placeholder);
        char *tip = g_strdup_printf("%s%sVIMS %03d advertises %d arguments. Learned continuous controls default to the last argument; use 'MIDI controls' to move $VALUE/$NORM/$RAW to another argument.",
                                    description && *description ? description : "",
                                    description && *description ? " · " : "",
                                    target, params);
        gtk_widget_set_tooltip_text(self->args_entry, tip);
        g_free(tip);
    } else {
        gtk_entry_set_placeholder_text(GTK_ENTRY(self->args_entry),
                                       "Example: 0 2 1 $VALUE  — tokens: $VALUE $NORM $RAW $CHANNEL $CHANNEL0 $CONTROL");
        gtk_widget_set_tooltip_text(self->args_entry,
                                    "VIMS arguments may use $VALUE, $NORM, $RAW, $CHANNEL, $CHANNEL0 and $CONTROL.");
    }
    midi_controlled_arg_refresh(self);
}

static void midi_action_type_changed(GtkComboBox *combo, gpointer data)
{
    (void) combo;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    midi_action_editor_sync(self);
    midi_target_hint_update(self);
    midi_transform_preview_changed(GTK_WIDGET(combo), self);
}

static void midi_target_selector_changed(GtkSpinButton *spin, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    midi_target_hint_update(self);
    midi_transform_preview_changed(GTK_WIDGET(spin), self);
}

static void midi_event_type_changed(GtkComboBox *combo, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    int type = combo_get_int(combo, VJ_MIDI_EVENT_CC);
    int value_min = 0, value_max = 127;
    int control_min = 0, control_max = 127;

    vj_midi_event_value_range((VjMidiEventType) type, &value_min, &value_max);
    vj_midi_event_control_range((VjMidiEventType) type, &control_min, &control_max);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(self->input_min_spin), value_min, value_max);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(self->input_max_spin), value_min, value_max);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->input_min_spin), value_min);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->input_max_spin), value_max);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(self->control_spin),
                              control_max == 0 ? 0 : -1, control_max);
    if(control_max == 0)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->control_spin), 0);
}

static void midi_control_fill_editor(GvrMidiControl *self, VjMidiMapping *m)
{
    if(!m)
        return;
    gtk_entry_set_text(GTK_ENTRY(self->name_entry), m->name ? m->name : "");

    gboolean found = FALSE;
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(self->device_combo));
    GtkTreeIter iter;
    if(gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gchar *text = NULL;
            gtk_tree_model_get(model, &iter, 0, &text, -1);
            if(g_strcmp0(text, m->device) == 0) {
                gtk_combo_box_set_active_iter(GTK_COMBO_BOX(self->device_combo), &iter);
                found = TRUE;
                g_free(text);
                break;
            }
            g_free(text);
        } while(gtk_tree_model_iter_next(model, &iter));
    }
    if(!found) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(self->device_combo),
                                       m->device ? m->device : VJ_MIDI_ANY_DEVICE);
        model = gtk_combo_box_get_model(GTK_COMBO_BOX(self->device_combo));
        int n = gtk_tree_model_iter_n_children(model, NULL);
        if(n > 0)
            gtk_combo_box_set_active(GTK_COMBO_BOX(self->device_combo), n - 1);
    }

    combo_set_int(GTK_COMBO_BOX(self->event_combo), m->event_type);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->channel_spin), m->channel + 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->control_spin), m->control);
    combo_set_int(GTK_COMBO_BOX(self->mode_combo), m->mode);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->input_min_spin), m->input_min);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->input_max_spin), m->input_max);
    if(m->output_limit_enabled) {
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(self->out_min_spin),
                                  m->output_limit_min, m->output_limit_max);
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(self->out_max_spin),
                                  m->output_limit_min, m->output_limit_max);
    } else {
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(self->out_min_spin), -1000000, 1000000);
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(self->out_max_spin), -1000000, 1000000);
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->out_min_spin), m->output_min);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->out_max_spin), m->output_max);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->deadzone_spin), m->deadzone);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->invert_check), m->invert);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->enabled_check), m->enabled);
    combo_set_int(GTK_COMBO_BOX(self->action_combo), m->action.type);
    midi_action_editor_sync(self);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->target_spin),
                              m->action.type == VJ_MIDI_ACTION_BUNDLE ?
                              m->action.bundle_id : m->action.vims_id);
    midi_target_hint_update(self);
    const char *args = m->action.type == VJ_MIDI_ACTION_RAW ? m->action.raw_message : m->action.args_template;
    gtk_entry_set_text(GTK_ENTRY(self->args_entry), args ? args : "");
    midi_controlled_arg_refresh(self);
    midi_selected_route_summary_update(self, m);
}

static void midi_tree_selection_changed(GtkTreeSelection *selection, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    if(!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;
    guint id = 0;
    gtk_tree_model_get(model, &iter, MAP_COL_ID, &id, -1);
    self->selected_id = id;
    VjMidiMapping *mapping = vj_midi_map_get(self->context->map, id);
    midi_control_fill_editor(self, mapping);
    midi_selected_route_summary_update(self, mapping);
    gtk_widget_queue_draw(self->curve_area);
    if(self->route_live_area)
        gtk_widget_queue_draw(self->route_live_area);
}

static gchar *combo_active_text(GtkWidget *combo)
{
    return gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
}

static void midi_editor_apply(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    VjMidiMapping *current = vj_midi_map_get(self->context->map, self->selected_id);
    if(!current)
        return;

    VjMidiMapping *candidate = vj_midi_mapping_copy(current);
    g_free(candidate->name);
    const char *name = gtk_entry_get_text(GTK_ENTRY(self->name_entry));
    candidate->name = name[0] ? g_strdup(name) : NULL;

    gchar *device = combo_active_text(self->device_combo);
    g_free(candidate->device);
    candidate->device = device ? device : g_strdup(VJ_MIDI_ANY_DEVICE);
    candidate->event_type = combo_get_int(GTK_COMBO_BOX(self->event_combo), VJ_MIDI_EVENT_CC);
    candidate->channel = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->channel_spin)) - 1;
    candidate->control = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->control_spin));
    candidate->mode = combo_get_int(GTK_COMBO_BOX(self->mode_combo), VJ_MIDI_MODE_ABSOLUTE);
    candidate->input_min = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->input_min_spin));
    candidate->input_max = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->input_max_spin));
    candidate->output_min = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->out_min_spin));
    candidate->output_max = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->out_max_spin));
    candidate->deadzone = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->deadzone_spin));
    candidate->invert = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->invert_check));
    candidate->enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->enabled_check));
    candidate->action.type = combo_get_int(GTK_COMBO_BOX(self->action_combo), VJ_MIDI_ACTION_VIMS);
    int target = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->target_spin));
    candidate->action.vims_id = target;
    candidate->action.bundle_id = target;

    const char *args = gtk_entry_get_text(GTK_ENTRY(self->args_entry));
    g_free(candidate->action.args_template); candidate->action.args_template = NULL;
    g_free(candidate->action.raw_message); candidate->action.raw_message = NULL;
    g_free(candidate->action.raw_off_message); candidate->action.raw_off_message = NULL;
    g_free(candidate->action.raw_on_message); candidate->action.raw_on_message = NULL;
    if(candidate->action.type == VJ_MIDI_ACTION_RAW)
        candidate->action.raw_message = g_strdup(args);
    else if(candidate->action.type == VJ_MIDI_ACTION_VIMS)
        candidate->action.args_template = g_strdup(args);

    if(candidate->mode == VJ_MIDI_MODE_ABSOLUTE &&
       candidate->input_min >= candidate->input_max) {
        midi_show_error(self, "Invalid MIDI mapping",
                        "MIDI range requires minimum < maximum. Use Invert to reverse the mapping direction.");
        vj_midi_mapping_free(candidate);
        return;
    }
    if(candidate->output_min > candidate->output_max) {
        midi_show_error(self, "Invalid MIDI mapping",
                        "VIMS range requires minimum <= maximum. Use Invert to reverse the mapping direction.");
        vj_midi_mapping_free(candidate);
        return;
    }
    if(candidate->output_limit_enabled &&
       (candidate->output_min < candidate->output_limit_min ||
        candidate->output_max > candidate->output_limit_max)) {
        char error[256];
        g_snprintf(error, sizeof(error),
                   "VIMS range %d..%d must stay inside the learned Reloaded control range %d..%d.",
                   candidate->output_min, candidate->output_max,
                   candidate->output_limit_min, candidate->output_limit_max);
        midi_show_error(self, "Invalid MIDI mapping", error);
        vj_midi_mapping_free(candidate);
        return;
    }

    if(!midi_mapping_special_center(candidate)) {
        midi_show_error(self, "Invalid MIDI mapping",
                        "The edit-list FPS is outside the selected VIMS output range for centered Pitch Bend → VIMS 335.");
        vj_midi_mapping_free(candidate);
        return;
    }

    if(candidate->action.type == VJ_MIDI_ACTION_VIMS) {
        int advertised_params = 0;
        const char *advertised_format = "";
        if(vj_gui_vims_get_event_metadata(candidate->action.vims_id,
                                          &advertised_params,
                                          &advertised_format,
                                          NULL)) {
            GPtrArray *tokens = midi_vims_argument_tokens(candidate->action.args_template);
            const int actual_params = (int)tokens->len;
            g_ptr_array_free(tokens, TRUE);
            if(actual_params != advertised_params) {
                char error[320];
                g_snprintf(error, sizeof(error),
                           "VIMS %03d expects %d argument%s%s%s, but this mapping contains %d.",
                           candidate->action.vims_id,
                           advertised_params,
                           advertised_params == 1 ? "" : "s",
                           advertised_format && *advertised_format ? " matching '" : "",
                           advertised_format && *advertised_format ? advertised_format : "",
                           actual_params);
                if(advertised_format && *advertised_format)
                    g_strlcat(error, "'", sizeof(error));
                midi_show_error(self, "Invalid MIDI mapping", error);
                vj_midi_mapping_free(candidate);
                return;
            }
        }
    }

    char validation[256];
    if(!vj_midi_mapping_validate(candidate, validation, sizeof(validation))) {
        midi_show_error(self, "Invalid MIDI mapping", validation);
        vj_midi_mapping_free(candidate);
        return;
    }

    guint id = current->id;
    candidate->id = id;
    candidate->last_output = candidate->output_min;
    vj_midi_map_remove(self->context->map, id);
    if(!vj_midi_map_add(self->context->map, candidate)) {
        midi_show_error(self, "MIDI mapping", "Unable to store the mapping");
        vj_midi_mapping_free(candidate);
        self->selected_id = 0;
        midi_context_refresh(self->context);
        return;
    }
    self->selected_id = id;
    midi_context_refresh(self->context);
}

static void midi_store_new_mapping(GvrMidiControl *self, VjMidiMapping *m)
{
    if(!self || !m)
        return;
    guint id = vj_midi_map_add(self->context->map, m);
    if(id == 0) {
        midi_show_error(self, "MIDI mapping", "Unable to store another MIDI mapping");
        vj_midi_mapping_free(m);
        return;
    }
    self->selected_id = id;
    midi_context_refresh(self->context);
    midi_control_fill_editor(self, m);
    midi_editor_set_expanded(self, TRUE);
}

static void midi_new_mapping(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    VjMidiMapping *m = vj_midi_mapping_new();
    m->enabled = 0;
    m->action.vims_id = VJ_MIDI_VIMS_MIN_ID;
    m->action.args_template = g_strdup("$VALUE");
    midi_store_new_mapping(self, m);
}

static void midi_capture_mapping(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(!self->context->have_last_event) {
        midi_show_error(self, "Capture MIDI input",
                        "No MIDI input has been received yet. Move or press a controller first.");
        return;
    }

    const VjMidiEvent *ev = &self->context->last_event;
    VjMidiMapping *m = vj_midi_mapping_new();
    g_free(m->device);
    m->device = g_strdup(ev->device_name);
    m->event_type = ev->type;
    m->channel = ev->channel;
    m->control = ev->control;
    m->input_min = ev->value_min;
    m->input_max = ev->value_max;
    m->enabled = 0;
    m->action.vims_id = VJ_MIDI_VIMS_MIN_ID;
    m->action.args_template = g_strdup("$VALUE");
    m->name = g_strdup_printf("%s Ch%d %d",
                              vj_midi_event_type_name(ev->type),
                              ev->channel + 1, ev->control);
    midi_store_new_mapping(self, m);
    vj_msg(VEEJAY_MSG_INFO, "Captured MIDI %s Ch%d/%d as a new disabled mapping",
           vj_midi_event_type_name(ev->type), ev->channel + 1, ev->control);
}

static void midi_duplicate_mapping(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    VjMidiMapping *src = vj_midi_map_get(self->context->map, self->selected_id);
    if(!src)
        return;
    VjMidiMapping *copy = vj_midi_mapping_copy(src);
    copy->id = 0;
    g_free(copy->name);
    copy->name = src->name ? g_strdup_printf("%s copy", src->name) : g_strdup("Mapping copy");
    guint id = vj_midi_map_add(self->context->map, copy);
    if(id == 0) {
        midi_show_error(self, "MIDI mapping", "Unable to duplicate the selected mapping");
        vj_midi_mapping_free(copy);
        return;
    }
    self->selected_id = id;
    midi_context_refresh(self->context);
}

static void midi_delete_mapping(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(self->selected_id && vj_midi_map_remove(self->context->map, self->selected_id)) {
        self->selected_id = 0;
        midi_context_refresh(self->context);
    }
}

static int midi_test_event_matches(const VjMidiMapping *m, const VjMidiEvent *event)
{
    if(!m || !event)
        return 0;
    if(m->event_type != event->type)
        return 0;
    if(m->channel != VJ_MIDI_ANY_CHANNEL && m->channel != event->channel)
        return 0;
    if(m->control != VJ_MIDI_ANY_CONTROL && m->control != event->control)
        return 0;
    if(m->device && strcmp(m->device, VJ_MIDI_ANY_DEVICE) != 0 &&
       g_strcmp0(m->device, event->device_name) != 0)
        return 0;
    return 1;
}

static void midi_test_mapping(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    VjMidiMapping *m = vj_midi_map_get(self->context->map, self->selected_id);
    if(!m)
        return;

    char validation[256];
    if(!vj_midi_mapping_validate(m, validation, sizeof(validation))) {
        midi_show_error(self, "Invalid MIDI mapping", validation);
        return;
    }

    int value = m->input_min + (m->input_max - m->input_min) / 2;
    if(self->context->have_last_event && midi_test_event_matches(m, &self->context->last_event))
        value = self->context->last_event.value;
    vj_midi_map_test(self->context->map, m, value);
}

static void midi_learn_toggled(GtkToggleButton *button, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    gboolean active = gtk_toggle_button_get_active(button);
    vj_midi_learn(self->context, active);
    if(active)
        midi_editor_set_expanded(self, TRUE);
}

static gboolean midi_device_list_contains(const VjMidiDeviceInfo *devices,
                                          int count,
                                          int client,
                                          int port)
{
    for(int i = 0; i < count; i++) {
        if(devices[i].client == client && devices[i].port == port)
            return TRUE;
    }
    return FALSE;
}

static gboolean midi_device_lists_differ(const VjMidiDeviceInfo *a, int a_count,
                                         const VjMidiDeviceInfo *b, int b_count)
{
    if(a_count != b_count)
        return TRUE;

    for(int i = 0; i < a_count; i++) {
        int match = -1;
        for(int j = 0; j < b_count; j++) {
            if(a[i].client == b[j].client && a[i].port == b[j].port) {
                match = j;
                break;
            }
        }
        if(match < 0 ||
           a[i].connected != b[match].connected ||
           g_strcmp0(a[i].name, b[match].name) != 0)
            return TRUE;
    }
    return FALSE;
}

static void midi_disconnect_missing_sources(GvrMidiControl *self,
                                            const VjMidiDeviceInfo *old_devices,
                                            int old_count,
                                            const VjMidiDeviceInfo *new_devices,
                                            int new_count)
{
    for(int i = 0; i < old_count; i++) {
        if(!old_devices[i].connected)
            continue;
        if(midi_device_list_contains(new_devices, new_count,
                                     old_devices[i].client,
                                     old_devices[i].port))
            continue;
        vj_midi_engine_disconnect(self->context->engine,
                                  old_devices[i].client,
                                  old_devices[i].port);
    }
}

static gboolean midi_source_watch_tick(gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    if(!self->context || !vj_midi_engine_available(self->context->engine))
        return G_SOURCE_CONTINUE;

    VjMidiDeviceInfo *devices = NULL;
    int count = vj_midi_engine_scan_devices(self->context->engine, &devices);
    const gboolean changed = midi_device_lists_differ(self->devices,
                                                       self->device_count,
                                                       devices,
                                                       count);
    vj_midi_engine_free_devices(devices);

    if(changed)
        midi_control_rescan(self);
    return G_SOURCE_CONTINUE;
}

static void midi_control_rescan(GvrMidiControl *self)
{
    int old_client = -1;
    int old_port = -1;
    VjMidiDeviceInfo *new_devices = NULL;
    if(self->selected_device_index >= 0 && self->selected_device_index < self->device_count) {
        old_client = self->devices[self->selected_device_index].client;
        old_port = self->devices[self->selected_device_index].port;
    }

    int new_count = vj_midi_engine_scan_devices(self->context->engine, &new_devices);
    midi_disconnect_missing_sources(self, self->devices, self->device_count,
                                    new_devices, new_count);
    vj_midi_engine_free_devices(self->devices);
    self->devices = new_devices;
    self->device_count = new_count;
    const gboolean live_state_changed =
        midi_live_prune_disconnected_state(self, self->devices, self->device_count);
    if(self->context->pending_learn_event && self->context->have_last_event &&
       !midi_live_event_source_connected(self, &self->context->last_event))
        self->context->pending_learn_event = FALSE;
    self->selected_device_index = -1;
    self->hover_device_index = -1;
    self->hover_wire_delete_index = -1;
    self->wire_drag_source_index = -1;
    for(int i = 0; i < self->device_count; i++) {
        if(self->devices[i].client == old_client && self->devices[i].port == old_port) {
            self->selected_device_index = i;
            break;
        }
    }
    if(self->selected_device_index < 0 && self->device_count == 1)
        self->selected_device_index = 0;

    if(self->source_combo) {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(self->source_combo));
        for(int i = 0; i < self->device_count; i++) {
            char label[192];
            g_snprintf(label, sizeof(label), "%s  [%d:%d]%s",
                       self->devices[i].name, self->devices[i].client, self->devices[i].port,
                       self->devices[i].connected ? "  • connected" : "");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(self->source_combo), label);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(self->source_combo), self->selected_device_index);
        gtk_widget_set_sensitive(self->source_combo, self->device_count > 0);
    }

    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(self->device_combo));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(self->device_combo), VJ_MIDI_ANY_DEVICE);
    for(int i = 0; i < self->device_count; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(self->device_combo), self->devices[i].name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->device_combo), 0);

    VjMidiMapping *selected = vj_midi_map_get(self->context->map, self->selected_id);
    if(selected)
        midi_control_fill_editor(self, selected);
    midi_update_device_actions(self);
    midi_patch_update_canvas_size(self);
    gtk_widget_queue_draw(self->device_area);
    if(self->monitor_area) {
        if(live_state_changed)
            midi_live_update_canvas_size(self, gtk_widget_get_allocated_width(self->monitor_area));
        gtk_widget_queue_draw(self->monitor_area);
    }
    if(self->recent_area)
        gtk_widget_queue_draw(self->recent_area);
}

static void midi_source_combo_changed(GtkComboBox *combo, gpointer data)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    int active = gtk_combo_box_get_active(combo);
    if(active < 0 || active >= self->device_count)
        self->selected_device_index = -1;
    else
        self->selected_device_index = active;
    midi_update_device_actions(self);
    gtk_widget_queue_draw(self->device_area);
}

static void midi_rescan_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    midi_control_rescan(GVR_MIDI_CONTROL(data));
}

static VjMidiDeviceInfo *midi_selected_device(GvrMidiControl *self)
{
    if(!self || self->selected_device_index < 0 ||
       self->selected_device_index >= self->device_count)
        return NULL;
    return &self->devices[self->selected_device_index];
}

static void midi_connect_selected_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    VjMidiDeviceInfo *d = midi_selected_device(self);
    if(!d)
        return;
    vj_midi_engine_connect(self->context->engine, d->client, d->port);
    midi_control_rescan(self);
}

static void midi_disconnect_selected_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    VjMidiDeviceInfo *d = midi_selected_device(self);
    if(!d)
        return;
    vj_midi_engine_disconnect(self->context->engine, d->client, d->port);
    midi_control_rescan(self);
}

static void midi_connect_all_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    for(int i = 0; i < self->device_count; i++)
        vj_midi_engine_connect(self->context->engine,
                               self->devices[i].client, self->devices[i].port);
    midi_control_rescan(self);
}

static void midi_disconnect_all_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    for(int i = 0; i < self->device_count; i++)
        vj_midi_engine_disconnect(self->context->engine,
                                  self->devices[i].client, self->devices[i].port);
    midi_control_rescan(self);
}

static void midi_save_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(self));
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Save MIDI mapping",
        GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "veejay-midi-v2.cfg");
    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        vj_midi_save(self->context, filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void midi_open_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    GvrMidiControl *self = GVR_MIDI_CONTROL(data);
    GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(self));
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open MIDI mapping",
        GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        vj_midi_load(self->context, filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static GtkWidget *midi_icon_image(const char *icon_name)
{
    if(!icon_name || !icon_name[0])
        return NULL;
    char *path = g_build_filename(RELOADED_DATADIR, icon_name, NULL);
    GtkWidget *image = gtk_image_new_from_file(path);
    g_free(path);
    return image;
}

static void midi_button_set_icon(GtkWidget *button, const char *icon_name)
{
    GtkWidget *image;
    if(!button || !GTK_IS_BUTTON(button))
        return;
    image = midi_icon_image(icon_name);
    if(!image)
        return;
    gtk_button_set_label(GTK_BUTTON(button), NULL);
    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
}

static void midi_enabled_toggle_sync_icon(GtkToggleButton *button, gpointer data)
{
    (void)data;
    midi_button_set_icon(GTK_WIDGET(button),
                         gtk_toggle_button_get_active(button) ?
                         "fx_entry_on.png" : "fx_entry_off.png");
    midi_set_tooltip(GTK_WIDGET(button),
                     gtk_toggle_button_get_active(button) ?
                     "Mapping enabled. Click to disable it without deleting it." :
                     "Mapping disabled. Click to enable it.");
}

static GtkWidget *toolbar_button(const char *label, const char *icon_name,
                                 const char *tooltip, GCallback cb, gpointer data)
{
    GtkWidget *button = icon_name ? gtk_button_new() : gtk_button_new_with_label(label);
    if(icon_name)
        midi_button_set_icon(button, icon_name);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "vims-history-button");
    if(tooltip)
        gtk_widget_set_tooltip_text(button, tooltip);
    g_signal_connect(button, "clicked", cb, data);
    return button;
}

static void midi_set_tooltip(GtkWidget *widget, const char *text)
{
    if(widget && text)
        gtk_widget_set_tooltip_text(widget, text);
}

static void midi_editor_field(GtkWidget *grid, int column, int row,
                              const char *label, GtkWidget *control,
                              int control_span)
{
    GtkWidget *l = gtk_label_new(label);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_valign(l, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), l, column, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), control, column + 1, row, MAX(1, control_span), 1);
    gtk_widget_set_hexpand(control, TRUE);
}

static GtkWidget *midi_editor_page_new(const char *subtitle, GtkWidget **grid_out)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 6);
    if(subtitle && subtitle[0]) {
        GtkWidget *sub = gtk_label_new(subtitle);
        gtk_label_set_xalign(GTK_LABEL(sub), 0.0);
        gtk_widget_set_halign(sub, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box), sub, FALSE, FALSE, 0);
    }
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 7);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 9);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 1);
    if(grid_out)
        *grid_out = grid;
    return box;
}

static GtkWidget *midi_range_box(GtkWidget *minimum, GtkWidget *maximum)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *arrow = gtk_label_new("→");
    gtk_widget_set_hexpand(minimum, TRUE);
    gtk_widget_set_hexpand(maximum, TRUE);
    gtk_box_pack_start(GTK_BOX(box), minimum, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), arrow, FALSE, FALSE, 1);
    gtk_box_pack_start(GTK_BOX(box), maximum, TRUE, TRUE, 0);
    return box;
}

static GtkWidget *combo_enum_new(const char *const *labels, int first, int last)
{
    GtkWidget *combo = gtk_combo_box_text_new();
    for(int i = first; i <= last; i++) {
        char id[16];
        g_snprintf(id, sizeof(id), "%d", i);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), id, labels[i - first]);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    return combo;
}

static GtkWidget *midi_section_label(const char *title, const char *subtitle)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *main = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped("<b>%s</b>", title ? title : "");
    gtk_label_set_markup(GTK_LABEL(main), markup);
    g_free(markup);
    gtk_widget_set_halign(main, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), main, FALSE, FALSE, 0);
    if(subtitle && subtitle[0]) {
        GtkWidget *sub = gtk_label_new(subtitle);
        gtk_widget_set_halign(sub, GTK_ALIGN_START);
        gtk_label_set_xalign(GTK_LABEL(sub), 0.0);
        gtk_box_pack_start(GTK_BOX(box), sub, FALSE, FALSE, 0);
    }
    return box;
}

static void gvr_midi_control_init(GvrMidiControl *self)
{
    self->held_notes = g_array_new(FALSE, FALSE, sizeof(MidiLiveState));
    self->active_controls = g_array_new(FALSE, FALSE, sizeof(MidiLiveState));
    self->route_activity = g_array_new(FALSE, FALSE, sizeof(MidiRouteActivity));
    self->selected_device_index = -1;
    self->hover_device_index = -1;
    self->hover_route_index = -1;
    self->route_view_scale = 1.0;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(self), 7);
    gtk_container_set_border_width(GTK_CONTAINER(self), 7);

    GtkWidget *titlebar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>MIDI CONTROL</b>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(titlebar), title, FALSE, FALSE, 0);
    GtkWidget *subtitle = gtk_label_new("ALSA sequencer routing · normalized MIDI · VIMS mappings");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(titlebar), subtitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(self), titlebar, FALSE, TRUE, 0);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(notebook), "midi-control-notebook");
    gtk_box_pack_start(GTK_BOX(self), notebook, TRUE, TRUE, 0);

    /* Sources ----------------------------------------------------------- */
    GtkWidget *sources_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(sources_page), 8);
    gtk_box_pack_start(GTK_BOX(sources_page),
                       midi_section_label("MIDI SOURCES",
                                          "Select and patch ALSA MIDI source ports into Reloaded."),
                       FALSE, FALSE, 0);

    GtkWidget *patch_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(patch_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(patch_scroll), GTK_SHADOW_NONE);
    gtk_widget_set_vexpand(patch_scroll, TRUE);
    gtk_widget_set_hexpand(patch_scroll, TRUE);

    self->device_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(self->device_area, MIDI_PATCH_MIN_WIDTH, MIDI_PATCH_MIN_HEIGHT);
    midi_set_tooltip(self->device_area,
                     "MIDI patchbay. Drag a source socket onto the Reloaded input socket to connect. Click the × on a wire to disconnect it. Click a node to select it.");
    gtk_widget_add_events(self->device_area, GDK_BUTTON_PRESS_MASK |
                                                GDK_BUTTON_RELEASE_MASK |
                                                GDK_POINTER_MOTION_MASK |
                                                GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(self->device_area, "draw", G_CALLBACK(midi_device_draw), self);
    g_signal_connect(self->device_area, "button-press-event", G_CALLBACK(midi_device_button_press), self);
    g_signal_connect(self->device_area, "button-release-event", G_CALLBACK(midi_device_button_release), self);
    g_signal_connect(self->device_area, "motion-notify-event", G_CALLBACK(midi_device_motion), self);
    g_signal_connect(self->device_area, "leave-notify-event", G_CALLBACK(midi_device_leave), self);
    gtk_container_add(GTK_CONTAINER(patch_scroll), self->device_area);
    gtk_box_pack_start(GTK_BOX(sources_page), patch_scroll, TRUE, TRUE, 0);

    GtkWidget *device_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *rescan = toolbar_button("Rescan", "icon_refresh.png",
                                       "Rescan ALSA sequencer source ports.",
                                       G_CALLBACK(midi_rescan_clicked), self);
    self->source_combo = gtk_combo_box_text_new();
    gtk_widget_set_size_request(self->source_combo, 280, -1);
    midi_set_tooltip(self->source_combo,
                     "Select any detected ALSA MIDI source. This stays in sync with the source cards above.");
    gtk_style_context_add_class(gtk_widget_get_style_context(self->source_combo), "vims-history-button");
    g_signal_connect(self->source_combo, "changed", G_CALLBACK(midi_source_combo_changed), self);
    gtk_widget_set_sensitive(self->source_combo, FALSE);
    self->connect_button = toolbar_button("Connect", "icon_connect.png",
                                          "Connect the selected MIDI source to Reloaded.",
                                          G_CALLBACK(midi_connect_selected_clicked), self);
    self->disconnect_button = toolbar_button("Disconnect", "icon_disconnect.png",
                                             "Disconnect the selected MIDI source from Reloaded.",
                                             G_CALLBACK(midi_disconnect_selected_clicked), self);
    GtkWidget *connect_all = toolbar_button("Connect All", "icon_connect.png",
                                            "Connect Reloaded to all detected ALSA MIDI sources.",
                                            G_CALLBACK(midi_connect_all_clicked), self);
    GtkWidget *disconnect_all = toolbar_button("Disconnect All", "icon_disconnect.png",
                                               "Disconnect Reloaded from all detected ALSA MIDI sources.",
                                               G_CALLBACK(midi_disconnect_all_clicked), self);
    gtk_box_pack_start(GTK_BOX(device_toolbar), rescan, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(device_toolbar), self->source_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(device_toolbar), self->connect_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(device_toolbar), self->disconnect_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(device_toolbar), disconnect_all, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(device_toolbar), connect_all, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(sources_page), device_toolbar, FALSE, TRUE, 0);
    midi_update_device_actions(self);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), sources_page, gtk_label_new("Sources"));

    /* Mappings ---------------------------------------------------------- */
    GtkWidget *mappings_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_container_set_border_width(GTK_CONTAINER(mappings_page), 8);

    GtkWidget *mapping_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    self->learn_button = gtk_toggle_button_new();
    midi_button_set_icon(self->learn_button, "icon_keybind.png");
    gtk_style_context_add_class(gtk_widget_get_style_context(self->learn_button), "vims-history-button");
    midi_set_tooltip(self->learn_button,
                     "Classic MIDI Learn: touch a MIDI control, then operate a Reloaded control. The completed mapping appears in Mapping Routes.");
    g_signal_connect(self->learn_button, "toggled", G_CALLBACK(midi_learn_toggled), self);
    gtk_box_pack_start(GTK_BOX(mapping_toolbar), self->learn_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mapping_toolbar),
                       toolbar_button("New", "icon_new.png", "Create a blank mapping for manual configuration.",
                                      G_CALLBACK(midi_new_mapping), self), FALSE, FALSE, 0);
    self->capture_button = toolbar_button("Capture Input", "icon_record.png",
                                          "Create a new disabled mapping from the most recently received MIDI event.",
                                          G_CALLBACK(midi_capture_mapping), self);
    gtk_widget_set_sensitive(self->capture_button, FALSE);
    gtk_box_pack_start(GTK_BOX(mapping_toolbar), self->capture_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mapping_toolbar),
                       toolbar_button("Duplicate", "icon_copy.png", "Duplicate the selected MIDI mapping.",
                                      G_CALLBACK(midi_duplicate_mapping), self), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mapping_toolbar),
                       toolbar_button("Delete", "icon_trash.png", "Delete the selected MIDI mapping.",
                                      G_CALLBACK(midi_delete_mapping), self), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mapping_toolbar),
                       toolbar_button("Test", "icon_launch.png", "Send one synthetic event through the selected mapping and VIMS target.",
                                      G_CALLBACK(midi_test_mapping), self), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mapping_toolbar),
                       toolbar_button("Fit", NULL, "Fit all mapping route cards into the Mapping Routes canvas.",
                                      G_CALLBACK(midi_route_fit_clicked), self), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mapping_toolbar),
                       toolbar_button("1:1", NULL, "Reset Mapping Routes zoom to 100% and keep the selected route visible.",
                                      G_CALLBACK(midi_route_one_to_one_clicked), self), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(mapping_toolbar),
                     toolbar_button("Save", "icon_save.png", "Save the current MIDI mappings.",
                                    G_CALLBACK(midi_save_clicked), self), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(mapping_toolbar),
                     toolbar_button("Open", "icon_open.png", "Load a MIDI mapping file (v2 GKeyFile format).",
                                    G_CALLBACK(midi_open_clicked), self), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mappings_page), mapping_toolbar, FALSE, TRUE, 0);

    self->store = gtk_list_store_new(MAP_N_COLUMNS,
        G_TYPE_UINT, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    self->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(self->store));
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->tree));
    g_signal_connect(selection, "changed", G_CALLBACK(midi_tree_selection_changed), self);

    self->mapping_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(self->mapping_paned, TRUE);
    gtk_widget_set_hexpand(self->mapping_paned, TRUE);
    gtk_paned_set_wide_handle(GTK_PANED(self->mapping_paned), TRUE);
    midi_set_tooltip(self->mapping_paned,
                     "Drag the horizontal divider to balance Mapping Routes against the route editor and signal inspector.");
    g_signal_connect(self->mapping_paned, "size-allocate",
                     G_CALLBACK(midi_mapping_paned_size_allocate), self);
    gtk_box_pack_start(GTK_BOX(mappings_page), self->mapping_paned, TRUE, TRUE, 0);

    GtkWidget *mapping_detail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_widget_set_hexpand(mapping_detail, TRUE);
    gtk_widget_set_vexpand(mapping_detail, FALSE);

    self->route_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(self->route_area, -1, MIDI_ROUTE_AREA_H);
    gtk_widget_set_hexpand(self->route_area, TRUE);
    gtk_widget_add_events(self->route_area, GDK_BUTTON_PRESS_MASK |
                                           GDK_BUTTON_RELEASE_MASK |
                                           GDK_POINTER_MOTION_MASK |
                                           GDK_LEAVE_NOTIFY_MASK |
                                           GDK_SCROLL_MASK |
                                           GDK_SMOOTH_SCROLL_MASK);
    midi_set_tooltip(self->route_area,
                     "Mapping routes canvas. Click a card to inspect it; double-click to open its editor. Drag empty space or middle-button drag to pan; wheel to zoom; use Fit or 1:1 to reset the view.");
    g_signal_connect(self->route_area, "draw", G_CALLBACK(midi_route_draw), self);
    g_signal_connect(self->route_area, "button-press-event", G_CALLBACK(midi_route_button_press), self);
    g_signal_connect(self->route_area, "button-release-event", G_CALLBACK(midi_route_button_release), self);
    g_signal_connect(self->route_area, "motion-notify-event", G_CALLBACK(midi_route_motion), self);
    g_signal_connect(self->route_area, "scroll-event", G_CALLBACK(midi_route_scroll), self);
    g_signal_connect(self->route_area, "leave-notify-event", G_CALLBACK(midi_route_leave), self);
    g_signal_connect(self->route_area, "size-allocate", G_CALLBACK(midi_route_size_allocate), self);
    gtk_paned_pack1(GTK_PANED(self->mapping_paned), self->route_area, TRUE, FALSE);

    GtkWidget *meta = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *selected_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(selected_title), "<b>SELECTED ROUTE</b>");
    gtk_widget_set_halign(selected_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(meta), selected_title, FALSE, FALSE, 4);

    self->selected_summary = gtk_label_new("No route selected");
    gtk_label_set_xalign(GTK_LABEL(self->selected_summary), 0.0);
    gtk_widget_set_hexpand(self->selected_summary, TRUE);
    gtk_box_pack_start(GTK_BOX(meta), self->selected_summary, TRUE, TRUE, 0);

    GtkWidget *name_label = gtk_label_new("Name");
    gtk_widget_set_halign(name_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(meta), name_label, FALSE, FALSE, 0);
    self->name_entry = gtk_entry_new();
    gtk_widget_set_size_request(self->name_entry, 180, -1);
    midi_set_tooltip(self->name_entry, "Optional human-readable mapping name.");
    gtk_box_pack_start(GTK_BOX(meta), self->name_entry, FALSE, FALSE, 0);

    self->enabled_check = gtk_toggle_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(self->enabled_check), "vims-history-button");
    midi_button_set_icon(self->enabled_check, "fx_entry_off.png");
    midi_set_tooltip(self->enabled_check, "Mapping disabled. Click to enable it.");
    g_signal_connect(self->enabled_check, "toggled", G_CALLBACK(midi_enabled_toggle_sync_icon), self);
    gtk_box_pack_start(GTK_BOX(meta), self->enabled_check, FALSE, FALSE, 0);

    GtkWidget *apply = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(apply), "vims-history-button");
    midi_button_set_icon(apply, "icon_apply.png");
    midi_set_tooltip(apply, "Validate and apply the edited MIDI/VIMS mapping.");
    g_signal_connect(apply, "clicked", G_CALLBACK(midi_editor_apply), self);
    gtk_box_pack_start(GTK_BOX(meta), apply, FALSE, FALSE, 0);

    self->editor_toggle = gtk_toggle_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(self->editor_toggle), "vims-history-button");
    midi_button_set_icon(self->editor_toggle, "button_dec.png");
    midi_set_tooltip(self->editor_toggle, "Show the Input / Transform / Target editor.");
    g_signal_connect(self->editor_toggle, "toggled", G_CALLBACK(midi_editor_toggle_toggled), self);
    gtk_box_pack_end(GTK_BOX(meta), self->editor_toggle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mapping_detail), meta, FALSE, TRUE, 0);

    self->editor_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_duration(GTK_REVEALER(self->editor_revealer), 140);

    GtkWidget *editor_shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *editor_switcher = gtk_stack_switcher_new();
    gtk_widget_set_halign(editor_switcher, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(editor_shell), editor_switcher, FALSE, FALSE, 0);

    self->editor_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(self->editor_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(self->editor_stack), 100);
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(editor_switcher), GTK_STACK(self->editor_stack));
    gtk_widget_set_hexpand(self->editor_stack, TRUE);

    GtkWidget *input_grid = NULL;
    GtkWidget *input_page = midi_editor_page_new("Which MIDI gesture owns this route.", &input_grid);
    self->device_combo = gtk_combo_box_text_new();
    midi_editor_field(input_grid, 0, 0, "Device", self->device_combo, 1);
    midi_set_tooltip(self->device_combo, "Match a specific detected MIDI source or * for Any device.");
    const char *events[] = { "Unknown", "CC", "CC14", "NRPN", "RPN", "Pitch Bend",
                             "Note On", "Note Off", "Key Pressure", "Channel Pressure", "Program Change" };
    self->event_combo = combo_enum_new(events, 0, 10);
    midi_editor_field(input_grid, 2, 0, "Event", self->event_combo, 1);
    midi_set_tooltip(self->event_combo, "MIDI event type. Changing it resets the canonical input value/control ranges.");
    self->channel_spin = gtk_spin_button_new_with_range(0, 16, 1);
    midi_editor_field(input_grid, 0, 1, "Channel (0=Any)", self->channel_spin, 1);
    midi_set_tooltip(self->channel_spin, "MIDI channel 1-16; use 0 to match any channel.");
    self->control_spin = gtk_spin_button_new_with_range(-1, 16383, 1);
    midi_editor_field(input_grid, 2, 1, "Control (-1=Any)", self->control_spin, 1);
    midi_set_tooltip(self->control_spin, "CC/note/parameter identifier. Use -1 to match any identifier where supported.");
    gtk_stack_add_titled(GTK_STACK(self->editor_stack), input_page, "input", "Input");

    GtkWidget *transform_grid = NULL;
    GtkWidget *transform_page = midi_editor_page_new("Normalize, shape and scale the MIDI value.", &transform_grid);
    const char *modes[] = { "Absolute", "Relative", "Relative 2C", "Trigger", "Momentary", "Toggle" };
    self->mode_combo = combo_enum_new(modes, 0, 5);
    midi_editor_field(transform_grid, 0, 0, "Mode", self->mode_combo, 1);
    midi_set_tooltip(self->mode_combo, "How incoming MIDI values are interpreted: continuous, relative encoder, trigger, momentary or toggle.");
    self->deadzone_spin = gtk_spin_button_new_with_range(0, 16383, 1);
    midi_editor_field(transform_grid, 2, 0, "Deadzone", self->deadzone_spin, 1);
    midi_set_tooltip(self->deadzone_spin, "Raw MIDI deadzone.");
    self->invert_check = gtk_check_button_new();
    midi_editor_field(transform_grid, 4, 0, "Invert", self->invert_check, 1);
    midi_set_tooltip(self->invert_check, "Reverse the normalized input direction before producing $VALUE/$NORM.");

    self->input_min_spin = gtk_spin_button_new_with_range(-1000000, 1000000, 1);
    self->input_max_spin = gtk_spin_button_new_with_range(-1000000, 1000000, 1);
    GtkWidget *midi_range = midi_range_box(self->input_min_spin, self->input_max_spin);
    midi_editor_field(transform_grid, 0, 1, "MIDI range", midi_range, 1);
    midi_set_tooltip(self->input_min_spin, "Raw MIDI minimum used for normalization.");
    midi_set_tooltip(self->input_max_spin, "Raw MIDI maximum used for normalization.");

    self->out_min_spin = gtk_spin_button_new_with_range(-1000000, 1000000, 1);
    self->out_max_spin = gtk_spin_button_new_with_range(-1000000, 1000000, 1);
    GtkWidget *vims_range = midi_range_box(self->out_min_spin, self->out_max_spin);
    midi_editor_field(transform_grid, 2, 1, "VIMS range", vims_range, 3);
    midi_set_tooltip(self->out_min_spin, "Minimum value emitted by $VALUE. Learned numeric controls keep this inside the source Reloaded widget range.");
    midi_set_tooltip(self->out_max_spin, "Maximum value emitted by $VALUE. Learned numeric controls keep this inside the source Reloaded widget range.");
    gtk_stack_add_titled(GTK_STACK(self->editor_stack), transform_page, "transform", "Transform");

    GtkWidget *target_grid = NULL;
    GtkWidget *target_page = midi_editor_page_new("Parameterize a VIMS event, bundle or raw command.", &target_grid);
    const char *actions[] = { "VIMS Event", "VIMS Bundle", "Raw VIMS" };
    self->action_combo = combo_enum_new(actions, 0, 2);
    midi_editor_field(target_grid, 0, 0, "Action", self->action_combo, 1);
    midi_set_tooltip(self->action_combo, "Send a parameterized VIMS event, execute an existing bundle, or use expert raw VIMS syntax.");
    self->target_spin = gtk_spin_button_new_with_range(VJ_MIDI_VIMS_MIN_ID, VJ_MIDI_VIMS_MAX_ID, 1);
    g_signal_connect(self->target_spin, "value-changed", G_CALLBACK(midi_target_selector_changed), self);
    midi_editor_field(target_grid, 2, 0, "VIMS / Bundle", self->target_spin, 1);
    midi_set_tooltip(self->target_spin, "VIMS selector in the current core protocol range, or bundle selector 500-599.");
    self->args_entry = gtk_entry_new();
    midi_editor_field(target_grid, 0, 1, "Arguments / Raw", self->args_entry, 3);
    midi_set_tooltip(self->args_entry, "VIMS arguments may use $VALUE, $NORM, $RAW, $CHANNEL, $CHANNEL0 and $CONTROL.");
    self->controlled_arg_combo = gtk_combo_box_text_new();
    midi_editor_field(target_grid, 4, 1, "MIDI controls", self->controlled_arg_combo, 1);
    gtk_widget_set_size_request(self->controlled_arg_combo, 190, -1);
    g_signal_connect(self->controlled_arg_combo, "changed",
                     G_CALLBACK(midi_controlled_arg_changed), self);
    g_signal_connect(self->args_entry, "changed",
                     G_CALLBACK(midi_args_entry_changed), self);
    gtk_stack_add_titled(GTK_STACK(self->editor_stack), target_page, "target", "Target");

    gtk_box_pack_start(GTK_BOX(editor_shell), self->editor_stack, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(self->editor_revealer), editor_shell);
    gtk_box_pack_start(GTK_BOX(mapping_detail), self->editor_revealer, FALSE, TRUE, 0);
    midi_editor_set_expanded(self, FALSE);

    g_signal_connect(self->event_combo, "changed", G_CALLBACK(midi_event_type_changed), self);
    g_signal_connect(self->action_combo, "changed", G_CALLBACK(midi_action_type_changed), self);
    g_signal_connect(self->mode_combo, "changed", G_CALLBACK(midi_transform_preview_changed), self);
    midi_transform_spin_connect(self, self->input_min_spin);
    midi_transform_spin_connect(self, self->input_max_spin);
    midi_transform_spin_connect(self, self->out_min_spin);
    midi_transform_spin_connect(self, self->out_max_spin);
    midi_transform_spin_connect(self, self->deadzone_spin);
    g_signal_connect(self->invert_check, "toggled", G_CALLBACK(midi_transform_preview_changed), self);
    midi_event_type_changed(GTK_COMBO_BOX(self->event_combo), self);
    midi_action_editor_sync(self);

    gtk_box_pack_start(GTK_BOX(mapping_detail),
                       midi_section_label("ROUTE SIGNAL",
                                          "Transfer curve and live MIDI → transform → target values."),
                       FALSE, FALSE, 0);
    self->signal_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(self->signal_paned, TRUE);
    gtk_widget_set_hexpand(self->signal_paned, TRUE);
    gtk_widget_set_size_request(self->signal_paned, -1, 250);

    self->curve_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(self->curve_area, 360, 240);
    gtk_widget_set_vexpand(self->curve_area, TRUE);
    gtk_widget_set_hexpand(self->curve_area, TRUE);
    midi_set_tooltip(self->curve_area,
                     "Transfer curve for the selected route. The amber marker shows the current live input/output point.");
    g_signal_connect(self->curve_area, "draw", G_CALLBACK(midi_curve_draw), self);

    self->route_live_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(self->route_live_area, 280, 240);
    gtk_widget_set_vexpand(self->route_live_area, TRUE);
    gtk_widget_set_hexpand(self->route_live_area, TRUE);
    midi_set_tooltip(self->route_live_area,
                     "Live route inspector showing MIDI input, transform and resulting target value.");
    g_signal_connect(self->route_live_area, "draw", G_CALLBACK(midi_route_live_draw), self);

    gtk_paned_pack1(GTK_PANED(self->signal_paned), self->curve_area, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(self->signal_paned), self->route_live_area, TRUE, FALSE);
    g_signal_connect(self->signal_paned, "size-allocate",
                     G_CALLBACK(midi_signal_paned_size_allocate), self);
    gtk_box_pack_start(GTK_BOX(mapping_detail), self->signal_paned, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(self->mapping_paned), mapping_detail, FALSE, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), mappings_page, gtk_label_new("Mappings"));

    /* Live MIDI --------------------------------------------------------- */
    GtkWidget *live_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(live_page), 8);
    gtk_box_pack_start(GTK_BOX(live_page),
                       midi_section_label("LIVE MIDI",
                                          "Move a controller to inspect it. Active gestures remain visible as performance controls."),
                       FALSE, FALSE, 0);

    GtkWidget *live_overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(live_overlay, TRUE);
    gtk_widget_set_hexpand(live_overlay, TRUE);

    GtkWidget *live_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(live_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(live_scroll), GTK_SHADOW_NONE);
    gtk_widget_set_vexpand(live_scroll, TRUE);
    gtk_widget_set_hexpand(live_scroll, TRUE);
    g_signal_connect(live_scroll, "size-allocate",
                     G_CALLBACK(midi_live_scroll_size_allocate), self);

    self->monitor_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(self->monitor_area, 720, 720);
    midi_set_tooltip(self->monitor_area,
                     "Live MIDI performance view. Held notes, persistent controls and event history are separated; click Capture as Mapping on the Last Event card to create an editable route.");
    gtk_widget_add_events(self->monitor_area, GDK_BUTTON_PRESS_MASK |
                                             GDK_POINTER_MOTION_MASK |
                                             GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(self->monitor_area, "draw", G_CALLBACK(midi_monitor_draw), self);
    g_signal_connect(self->monitor_area, "button-press-event", G_CALLBACK(midi_monitor_button_press), self);
    g_signal_connect(self->monitor_area, "motion-notify-event", G_CALLBACK(midi_monitor_motion), self);
    g_signal_connect(self->monitor_area, "leave-notify-event", G_CALLBACK(midi_monitor_leave), self);
    gtk_container_add(GTK_CONTAINER(live_scroll), self->monitor_area);
    gtk_container_add(GTK_CONTAINER(live_overlay), live_scroll);

    self->recent_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(self->recent_area, -1, midi_recent_overlay_height(self->recent_area));
    gtk_widget_set_halign(self->recent_area, GTK_ALIGN_FILL);
    gtk_widget_set_valign(self->recent_area, GTK_ALIGN_END);
    midi_set_tooltip(self->recent_area,
                     "Chronological MIDI history. The ten newest incoming events remain pinned to the bottom of the Live MIDI view.");
    g_signal_connect(self->recent_area, "draw", G_CALLBACK(midi_recent_draw), self);
    gtk_overlay_add_overlay(GTK_OVERLAY(live_overlay), self->recent_area);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(live_overlay), self->recent_area, TRUE);

    gtk_box_pack_start(GTK_BOX(live_page), live_overlay, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), live_page, gtk_label_new("Live MIDI"));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
}

static void gvr_midi_control_finalize(GObject *object)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(object);
    if(self->monitor_decay_timer != 0) {
        g_source_remove(self->monitor_decay_timer);
        self->monitor_decay_timer = 0;
    }
    if(self->source_watch_timer != 0) {
        g_source_remove(self->source_watch_timer);
        self->source_watch_timer = 0;
    }
    if(self->transform_preview_idle != 0) {
        g_source_remove(self->transform_preview_idle);
        self->transform_preview_idle = 0;
    }
    if(self->held_notes)
        g_array_free(self->held_notes, TRUE);
    if(self->active_controls)
        g_array_free(self->active_controls, TRUE);
    if(self->route_activity)
        g_array_free(self->route_activity, TRUE);
    vj_midi_engine_free_devices(self->devices);
    G_OBJECT_CLASS(gvr_midi_control_parent_class)->finalize(object);
}

static void gvr_midi_control_style_updated(GtkWidget *widget)
{
    GvrMidiControl *self = GVR_MIDI_CONTROL(widget);
    GtkWidgetClass *parent = GTK_WIDGET_CLASS(gvr_midi_control_parent_class);
    GtkWidget *areas[] = {
        self->device_area, self->monitor_area, self->recent_area,
        self->route_area, self->curve_area, self->route_live_area
    };

    if(parent->style_updated)
        parent->style_updated(widget);
    if(self->recent_area)
        gtk_widget_set_size_request(self->recent_area, -1,
                                    midi_recent_overlay_height(self->recent_area));
    if(self->monitor_area)
        midi_live_update_canvas_size(self, gtk_widget_get_allocated_width(self->monitor_area));
    for(unsigned int i = 0; i < G_N_ELEMENTS(areas); i++) {
        if(areas[i]) {
            gtk_widget_queue_resize(areas[i]);
            gtk_widget_queue_draw(areas[i]);
        }
    }
}

static void gvr_midi_control_class_init(GvrMidiControlClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    object_class->finalize = gvr_midi_control_finalize;
    widget_class->style_updated = gvr_midi_control_style_updated;
}

GtkWidget *gvr_midi_control_new(void *midi_context)
{
    GvrMidiControl *self = g_object_new(GVR_TYPE_MIDI_CONTROL, NULL);
    self->context = (VjMidiContext *) midi_context;
    midi_control_rescan(self);
    self->source_watch_timer = g_timeout_add(MIDI_SOURCE_WATCH_INTERVAL_MS,
                                             midi_source_watch_tick, self);
    return GTK_WIDGET(self);
}

static void midi_context_refresh(VjMidiContext *context)
{
    if(context && GVR_IS_MIDI_CONTROL(context->panel))
        gvr_midi_control_refresh(context->panel);
}

static void midi_context_engine_event(const VjMidiEvent *event, void *user_data)
{
    VjMidiContext *context = (VjMidiContext *) user_data;
    context->last_event = *event;
    context->have_last_event = TRUE;

    if(context->learning)
        context->pending_learn_event = TRUE;
    else if(context->dispatch_enabled) {
        midi_context_refresh_special_centers(context);
        vj_midi_map_process_event(context->map, event);
    }

    if(GVR_IS_MIDI_CONTROL(context->panel))
        gvr_midi_control_midi_event(context->panel, event);
}

static void midi_context_free(gpointer data)
{
    VjMidiContext *context = (VjMidiContext *) data;
    if(!context)
        return;
    vj_midi_engine_free(context->engine);
    vj_midi_map_free(context->map);
    g_free(context->filename);
    g_free(context);
}

static void midi_hide_legacy_ui(GtkBuilder *builder)
{
    static const char *legacy_ids[] = {
        "midi_play", "midi_event", "midi_enabled",
        NULL
    };
    if(!builder)
        return;
    for(int i = 0; legacy_ids[i]; i++) {
        GtkWidget *obj = glade_xml_get_widget_(builder, legacy_ids[i]);
        if(obj && GTK_IS_WIDGET(obj)) {
            gtk_widget_set_no_show_all(obj, TRUE);
            gtk_widget_hide(obj);
        }
    }
}

static void midi_context_sync_learn_menu(VjMidiContext *context)
{
    GtkWidget *item;

    if(!context || !context->builder)
        return;
    item = glade_xml_get_widget_(context->builder, "midi_learn");
    if(!item || !GTK_IS_CHECK_MENU_ITEM(item))
        return;
    if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)) != context->learning)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), context->learning);
}

void *vj_midi_new(void *mw, void *timeline)
{
    VjMidiContext *context = g_new0(VjMidiContext, 1);
    context->builder = (GtkBuilder *) mw;
    context->timeline = timeline;
    context->engine = vj_midi_engine_new();
    context->map = vj_midi_map_new();
    context->dispatch_enabled = TRUE;
    vj_midi_map_set_send_callback(context->map, midi_context_send, context);
    vj_midi_engine_set_event_callback(context->engine, midi_context_engine_event, context);

    context->panel = gvr_midi_control_new(context);
    GtkWidget *notebook = context->builder ? glade_xml_get_widget_(context->builder, "notebook18") : NULL;
    if(notebook && GTK_IS_NOTEBOOK(notebook)) {
        GtkWidget *label = gtk_label_new("MIDI");
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), context->panel, label);
        gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), context->panel, TRUE);
        gtk_widget_show_all(context->panel);
        gtk_widget_show(label);
    } else {
        veejay_msg(VEEJAY_MSG_WARNING, "MIDI: main Panels notebook not found; MIDI engine remains available");
    }

    midi_hide_legacy_ui(context->builder);
    g_object_set_data_full(G_OBJECT(context->panel), "gvr-midi-context", context, midi_context_free);
    return context;
}

int vj_midi_handle_events(void *vv)
{
    VjMidiContext *context = (VjMidiContext *) vv;
    return context ? vj_midi_engine_handle_events(context->engine) : 0;
}

void vj_midi_show_control(void *vv)
{
    VjMidiContext *context = (VjMidiContext *) vv;
    GtkWidget *notebook;
    int page;

    if(!context || !context->builder || !context->panel)
        return;
    notebook = glade_xml_get_widget_(context->builder, "notebook18");
    if(!notebook || !GTK_IS_NOTEBOOK(notebook))
        return;
    page = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), context->panel);
    if(page >= 0)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), page);
}

void vj_midi_play(void *vv, int play)
{
    VjMidiContext *context = (VjMidiContext *) vv;
    if(!context)
        return;
    context->dispatch_enabled = play ? TRUE : FALSE;
    if(!play)
        vj_midi_learn(context, 0);
}

void vj_midi_learn(void *vv, int start)
{
    VjMidiContext *context = (VjMidiContext *) vv;
    const gboolean learning = start ? TRUE : FALSE;

    if(!context)
        return;
    if(context->learning == learning) {
        midi_context_sync_learn_menu(context);
        if(GVR_IS_MIDI_CONTROL(context->panel))
            gvr_midi_control_set_learning(context->panel, learning);
        return;
    }

    context->learning = learning;
    if(learning)
        context->pending_learn_event = FALSE;
    midi_context_sync_learn_menu(context);
    if(GVR_IS_MIDI_CONTROL(context->panel))
        gvr_midi_control_set_learning(context->panel, context->learning);
    vj_msg(VEEJAY_MSG_INFO, learning ?
           "MIDI Learn: move/press a controller, then operate a Reloaded control" :
           "MIDI Learn disabled");
}

void vj_midi_load(void *vv, const char *filename)
{
    VjMidiContext *context = (VjMidiContext *) vv;
    if(!context || !filename)
        return;
    GError *error = NULL;
    if(!vj_midi_map_load(context->map, filename, &error)) {
        vj_msg(VEEJAY_MSG_ERROR, "Unable to load MIDI mapping %s: %s",
               filename, error ? error->message : "invalid file");
        g_clear_error(&error);
        return;
    }
    context->special_center_fps_x100 = 0;
    midi_context_refresh_special_centers(context);
    g_free(context->filename);
    context->filename = g_strdup(filename);
    midi_context_refresh(context);
    vj_msg(VEEJAY_MSG_INFO, "Loaded %u MIDI mappings from %s",
           vj_midi_map_count(context->map), filename);
}

void vj_midi_save(void *vv, const char *filename)
{
    VjMidiContext *context = (VjMidiContext *) vv;
    if(!context || !filename)
        return;
    GError *error = NULL;
    if(!vj_midi_map_save(context->map, filename, &error)) {
        vj_msg(VEEJAY_MSG_ERROR, "Unable to save MIDI mapping %s: %s",
               filename, error ? error->message : "write failed");
        g_clear_error(&error);
        return;
    }
    g_free(context->filename);
    context->filename = g_strdup(filename);
    vj_msg(VEEJAY_MSG_INFO, "Saved %u MIDI mappings to %s",
           vj_midi_map_count(context->map), filename);
}

void vj_midi_reset(void *vv)
{
    VjMidiContext *context = (VjMidiContext *) vv;
    if(!context)
        return;
    vj_midi_map_clear(context->map);
    midi_context_refresh(context);
    vj_msg(VEEJAY_MSG_INFO, "Cleared MIDI mappings");
}

static void midi_legacy_parameter_text(const VjMidiMapping *m, char *buf, size_t size)
{
    if(m->channel == VJ_MIDI_ANY_CHANNEL)
        g_snprintf(buf, size, "%s %d", vj_midi_event_type_name(m->event_type), m->control);
    else
        g_snprintf(buf, size, "Channel %d / %s %d", m->channel + 1,
                   vj_midi_event_type_name(m->event_type), m->control);
}

int vj_midi_foreach_mapping(void *vv, vj_midi_mapping_func callback, void *user_data)
{
    VjMidiContext *context = (VjMidiContext *) vv;
    if(!context || !callback)
        return 0;
    int count = 0;
    for(guint i = 0; i < vj_midi_map_count(context->map); i++) {
        VjMidiMapping *m = vj_midi_map_get_nth(context->map, i);
        char key[32], param[96];
        g_snprintf(key, sizeof(key), "%u", m->id);
        midi_legacy_parameter_text(m, param, sizeof(param));
        char *target = midi_mapping_target_text(m);
        int legacy_extra = 0;
        if(m->mode == VJ_MIDI_MODE_TOGGLE || m->mode == VJ_MIDI_MODE_MOMENTARY)
            legacy_extra = MIDI_EXTRA_TOGGLE;
        else if(m->mode == VJ_MIDI_MODE_ABSOLUTE ||
                m->mode == VJ_MIDI_MODE_RELATIVE ||
                m->mode == VJ_MIDI_MODE_RELATIVE_2C)
            legacy_extra = 1;
        callback(key, m->event_type, m->control, legacy_extra,
                 vj_midi_event_type_name(m->event_type), param,
                 NULL, target, user_data);
        g_free(target);
        count++;
    }
    return count;
}

int vj_midi_unbind(void *vv, const char *mapping_key)
{
    VjMidiContext *context = (VjMidiContext *) vv;
    if(!context || !mapping_key)
        return 0;
    char *end = NULL;
    unsigned long id = strtoul(mapping_key, &end, 10);
    if(end == mapping_key || *end != '\0')
        return 0;
    int removed = vj_midi_map_remove(context->map, (guint) id);
    if(removed)
        midi_context_refresh(context);
    return removed;
}

static int midi_widget_output_range(VjMidiContext *context, const char *widget_name, int extra,
                                    int wire_scale, int *out_min, int *out_max)
{
    if(!context || !out_min || !out_max)
        return 0;

    if(extra == 3 && context->timeline) {
        *out_min = timeline_get_in_point(context->timeline) * wire_scale;
        *out_max = timeline_get_out_point(context->timeline) * wire_scale;
        return 1;
    }
    if(extra == 4 && context->timeline) {
        *out_min = 0;
        *out_max = timeline_get_length(context->timeline) * wire_scale;
        return 1;
    }
    if(!context->builder || !widget_name)
        return 0;

    GtkWidget *obj = glade_xml_get_widget_(context->builder, widget_name);
    gdouble lo = 0.0;
    gdouble hi = 0.0;
    if(obj && GTK_IS_RANGE(obj)) {
        GtkAdjustment *a = gtk_range_get_adjustment(GTK_RANGE(obj));
        lo = gtk_adjustment_get_lower(a);
        hi = gtk_adjustment_get_upper(a);
    } else if(obj && GTK_IS_SPIN_BUTTON(obj)) {
        gtk_spin_button_get_range(GTK_SPIN_BUTTON(obj), &lo, &hi);
    } else {
        return 0;
    }

    *out_min = (int)llround(lo * (double)wire_scale);
    *out_max = (int)llround(hi * (double)wire_scale);
    return 1;
}

static int midi_vims_wire_scale(int vims_id)
{
    return vims_id == VIMS_FRAMERATE ? 100 : 1;
}

static char *midi_dynamic_vims_message(const char *prefix)
{
    if(!prefix)
        return NULL;

    const char *token = "$VALUE";
    const size_t n = strlen(prefix);
    if(n >= 4 && g_ascii_isdigit(prefix[0]) && g_ascii_isdigit(prefix[1]) &&
       g_ascii_isdigit(prefix[2]) && prefix[3] == ':') {
        const int id = (prefix[0] - '0') * 100 +
                       (prefix[1] - '0') * 10 +
                       (prefix[2] - '0');
        if(id == VIMS_CHAIN_ENTRY_SET_NARG_VAL)
            token = "$NORM";
    }

    if(n > 0 && (prefix[n - 1] == ':' || prefix[n - 1] == ' '))
        return g_strdup_printf("%s%s;", prefix, token);
    return g_strdup_printf("%s %s;", prefix, token);
}

static char *midi_complete_vims_message(const char *message)
{
    if(!message)
        return NULL;
    size_t n = strlen(message);
    if(n > 0 && message[n - 1] == ';')
        return g_strdup(message);
    return g_strdup_printf("%s;", message);
}

static int midi_action_from_vims_message(VjMidiAction *action, const char *message)
{
    if(!action || !message || strlen(message) < 4)
        return 0;
    if(!g_ascii_isdigit(message[0]) || !g_ascii_isdigit(message[1]) ||
       !g_ascii_isdigit(message[2]) || message[3] != ':')
        return 0;

    int id = (message[0] - '0') * 100 + (message[1] - '0') * 10 + (message[2] - '0');
    const char *args = message + 4;
    size_t len = strlen(args);
    while(len > 0 && (args[len - 1] == ';' || g_ascii_isspace(args[len - 1])))
        len--;

    char *templ = g_strndup(args, len);
    g_strstrip(templ);
    if(id == VIMS_CHAIN_ENTRY_SET_NARG_VAL && strstr(templ, "$VALUE")) {
        char **parts = g_strsplit(templ, "$VALUE", -1);
        char *normalized = g_strjoinv("$NORM", parts);
        g_strfreev(parts);
        g_free(templ);
        templ = normalized;
    }

    action->type = VJ_MIDI_ACTION_VIMS;
    action->vims_id = id;
    g_free(action->args_template);
    action->args_template = templ[0] ? templ : NULL;
    if(!templ[0])
        g_free(templ);
    return 1;
}

void vj_midi_learning_vims(void *vv, char *widget, char *msg, int extra)
{
    VjMidiContext *context = (VjMidiContext *) vv;
    if(!context || !context->learning || !context->pending_learn_event || !msg)
        return;

    VjMidiMapping *m = vj_midi_mapping_new();
    m->event_type = context->last_event.type;
    m->channel = context->last_event.channel;
    m->control = context->last_event.control;
    m->input_min = context->last_event.value_min;
    m->input_max = context->last_event.value_max;
    g_free(m->name);
    m->name = widget ? g_strdup(widget) : NULL;

    char *message = NULL;
    if(extra == MIDI_EXTRA_TOGGLE) {
        m->mode = VJ_MIDI_MODE_TOGGLE;
        m->output_min = 0;
        m->output_max = 1;
        message = midi_dynamic_vims_message(msg);
        if(!midi_action_from_vims_message(&m->action, message)) {
            m->action.type = VJ_MIDI_ACTION_RAW;
            m->action.raw_message = g_strdup(message);
        }
    } else if(extra == MIDI_EXTRA_DUAL_TOGGLE) {
        int off_id = 0, off_arg = 0, on_id = 0, on_arg = 0;
        m->mode = VJ_MIDI_MODE_TOGGLE;
        m->output_min = 0;
        m->output_max = 1;
        m->action.type = VJ_MIDI_ACTION_RAW;
        if(sscanf(msg, "%d:%d;%d:%d;", &off_id, &off_arg, &on_id, &on_arg) == 4 &&
           off_id >= VJ_MIDI_VIMS_MIN_ID && off_id <= VJ_MIDI_VIMS_MAX_ID &&
           on_id >= VJ_MIDI_VIMS_MIN_ID && on_id <= VJ_MIDI_VIMS_MAX_ID) {
            m->action.raw_off_message = g_strdup_printf("%03d:%d;", off_id, off_arg);
            m->action.raw_on_message = g_strdup_printf("%03d:%d;", on_id, on_arg);
        } else {
            m->action.raw_message = midi_complete_vims_message(msg);
        }
    } else if(extra > 0) {
        m->mode = VJ_MIDI_MODE_ABSOLUTE;
        message = midi_dynamic_vims_message(msg);
        if(!midi_action_from_vims_message(&m->action, message)) {
            m->action.type = VJ_MIDI_ACTION_RAW;
            m->action.raw_message = g_strdup(message);
        }

        const int wire_scale = m->action.type == VJ_MIDI_ACTION_VIMS ?
            midi_vims_wire_scale(m->action.vims_id) : 1;
        if(!midi_widget_output_range(context, widget, extra, wire_scale,
                                     &m->output_min, &m->output_max)) {
            vj_msg(VEEJAY_MSG_ERROR,
                   "MIDI Learn could not read the numeric range of Reloaded control '%s'",
                   widget ? widget : "(unknown)");
            g_free(message);
            vj_midi_mapping_free(m);
            return;
        }
        m->output_limit_enabled = 1;
        m->output_limit_min = MIN(m->output_min, m->output_max);
        m->output_limit_max = MAX(m->output_min, m->output_max);
        m->output_min = m->output_limit_min;
        m->output_max = m->output_limit_max;
        if(!midi_mapping_special_center(m)) {
            vj_msg(VEEJAY_MSG_ERROR,
                   "MIDI Learn cannot center VIMS 335 on the edit-list FPS inside the Reloaded control range");
            g_free(message);
            vj_midi_mapping_free(m);
            return;
        }
    } else {
        m->mode = VJ_MIDI_MODE_TRIGGER;
        message = midi_complete_vims_message(msg);
        if(!midi_action_from_vims_message(&m->action, message)) {
            m->action.type = VJ_MIDI_ACTION_RAW;
            m->action.raw_message = g_strdup(message);
        }
    }
    g_free(message);

    if(m->mode == VJ_MIDI_MODE_ABSOLUTE ||
       m->mode == VJ_MIDI_MODE_MOMENTARY ||
       m->mode == VJ_MIDI_MODE_TRIGGER)
        m->last_output = midi_route_preview_output(m, context->last_event.value);

    char validation[256];
    if(!vj_midi_mapping_validate(m, validation, sizeof(validation))) {
        vj_msg(VEEJAY_MSG_ERROR, "MIDI Learn rejected VIMS mapping: %s", validation);
        vj_midi_mapping_free(m);
        return;
    }

    if(!vj_midi_map_add(context->map, m)) {
        vj_msg(VEEJAY_MSG_ERROR, "MIDI Learn could not store the mapping");
        vj_midi_mapping_free(m);
        return;
    }
    context->pending_learn_event = FALSE;
    midi_context_refresh(context);
    vj_msg(VEEJAY_MSG_INFO, "MIDI learned %s Ch%d/%d → %s",
           vj_midi_event_type_name(m->event_type), m->channel + 1, m->control,
           m->name ? m->name : "VIMS action");
}

void vj_midi_learning_vims_simple(void *vv, char *widget, int id)
{
    char message[16];
    g_snprintf(message, sizeof(message), widget ? "%03d:" : "%03d:;", id);
    vj_midi_learning_vims(vv, widget, message, widget ? 1 : 0);
}

void vj_midi_learning_vims_toggle(void *vv, char *widget, int id)
{
    char message[16];
    g_snprintf(message, sizeof(message), "%03d:", id);
    vj_midi_learning_vims(vv, widget, message, MIDI_EXTRA_TOGGLE);
}

void vj_midi_learning_vims_toggle2(void *vv, char *widget, int id, int arg)
{
    char message[32];
    g_snprintf(message, sizeof(message), "%03d:%d ", id, arg);
    vj_midi_learning_vims(vv, widget, message, MIDI_EXTRA_TOGGLE);
}

void vj_midi_learning_vims_toggle3(void *vv, char *widget, int id, int arg0, int arg1)
{
    char message[40];
    g_snprintf(message, sizeof(message), "%03d:%d %d ", id, arg0, arg1);
    vj_midi_learning_vims(vv, widget, message, MIDI_EXTRA_TOGGLE);
}

void vj_midi_learning_vims_dual_toggle(void *vv, char *widget, int off_id, int on_id, int arg)
{
    char message[48];
    g_snprintf(message, sizeof(message), "%03d:%d;%03d:%d;", off_id, arg, on_id, arg);
    vj_midi_learning_vims(vv, widget, message, MIDI_EXTRA_DUAL_TOGGLE);
}

void vj_midi_learning_vims_spin(void *vv, char *widget, int id)
{
    char message[16];
    g_snprintf(message, sizeof(message), widget ? "%03d:" : "%03d:;", id);
    vj_midi_learning_vims(vv, widget, message, widget ? 2 : 0);
}

void vj_midi_learning_vims_complex(void *vv, char *widget, int id, int first, int extra)
{
    char message[32];
    g_snprintf(message, sizeof(message), "%03d:%d", id, first);
    vj_midi_learning_vims(vv, widget, message, extra);
}

void vj_midi_learning_vims_complex_msg(void *vv, char *widget, int id, int first, int extra, char *name)
{
    char message[96];
    g_snprintf(message, sizeof(message), "%03d:0 %d %s", id, first, name ? name : "");
    vj_midi_learning_vims(vv, widget, message, extra);
}

void vj_midi_learning_vims_msg(void *vv, char *widget, int id, int arg)
{
    char message[32];
    g_snprintf(message, sizeof(message), "%03d:%d;", id, arg);
    vj_midi_learning_vims(vv, widget, message, 0);
}

void vj_midi_learning_vims_msg2(void *vv, char *widget, int vims_id, int id, int arg)
{
    char message[40];
    g_snprintf(message, sizeof(message), "%03d:%d %d;", vims_id, id, arg);
    vj_midi_learning_vims(vv, widget, message, 0);
}

void vj_midi_learning_vims_msg2_extra(void *vv, int id, int a, int extra)
{
    char message[32];
    g_snprintf(message, sizeof(message), "%03d:%d", id, a);
    vj_midi_learning_vims(vv, NULL, message, extra);
}

void vj_midi_learning_vims_fx(void *vv, int widget, int id, int a, int b, int c, int extra)
{
    char message[48];
    char name[32];
    g_snprintf(message, sizeof(message), "%03d:%d %d %d", id, a, b, c);
    g_snprintf(name, sizeof(name), "slider_p%d", widget);
    vj_midi_learning_vims(vv, name, message, extra);
}
