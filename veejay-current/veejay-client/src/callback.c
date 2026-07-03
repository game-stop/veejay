/* gveejay - Linux VeeJay - GVeejay GTK+-2/Glade User Interface
dlclose( handle );	
 *           (C) 2002-2015 Niels Elburg <nwelburg@gmail.com>
 *           (C)      2006 Matthijs van Henten <matthijs.vanhenten@gmail.com>
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

#include <ctype.h>
#include <math.h>
#include <string.h>
#include <veejaycore/vj-msg.h>
#include <gtktimeselection.h>
#include <gtk3curve.h>
#include <veejaycore/vims.h>
#include "callback.h"
#include "curve.h"

#ifndef VJ_KF_ENTRY_CHAIN_FADE
#define VJ_KF_ENTRY_CHAIN_FADE 0
#endif
#ifndef VJ_KF_PARAM_CHAIN_OPACITY
#define VJ_KF_PARAM_CHAIN_OPACITY 99
#endif


#define AUDIO_MASTER_JACK_UI_VALUE 1

#define AUDIO_MIX_MODE_FOLLOW 0
#define AUDIO_MIX_MODE_ORIGINAL 1
#define AUDIO_MIX_MODE_EXTERNAL 2
#define AUDIO_MIX_MODE_ORIGINAL_EXTERNAL 3
#define AUDIO_MIX_MODE_JACK AUDIO_MIX_MODE_EXTERNAL
#define AUDIO_MIX_MODE_ORIGINAL_JACK AUDIO_MIX_MODE_ORIGINAL_EXTERNAL

#define SAMPLE_AUDIO_SYNC_UI_SOURCE_NONE (-1)

static gboolean curve_live_preview_user_override_state = FALSE;

static void curve_live_preview_user_override(gboolean enabled)
{
    curve_live_preview_user_override_state = enabled ? TRUE : FALSE;

    if(info && info->curve && GTK3_IS_CURVE(info->curve)) {
        gtk3_curve_live_trace_set_user_override(info->curve, enabled);
        gtk_widget_queue_draw(info->curve);
    }
}

static gboolean G_GNUC_UNUSED curve_live_preview_user_is_overridden(void)
{
    return curve_live_preview_user_override_state;
}

static gboolean curve_editor_local_dirty_state = FALSE;

static void curve_editor_mark_local_dirty(void)
{
    curve_editor_local_dirty_state = TRUE;
}

static void curve_editor_clear_local_dirty(void)
{
    curve_editor_local_dirty_state = FALSE;
}

static gboolean curve_editor_is_local_dirty(void)
{
    return curve_editor_local_dirty_state;
}

static int audio_input_selector_active_from_ui(void);
static int audio_global_source_controls_allowed(void);
static int audio_input_selector_external_from_ui(void);

extern void vj_midi_learning_vims_toggle(void *vv, char *widget, int id);
extern void vj_midi_learning_vims_toggle2(void *vv, char *widget, int id, int arg);
extern void vj_midi_learning_vims_toggle3(void *vv, char *widget, int id, int arg0, int arg1);
extern void vj_midi_learning_vims_dual_toggle(void *vv, char *widget, int off_id, int on_id, int arg);


static int current_stream_selected(void)
{
    return info &&
           info->status_tokens[PLAY_MODE] == MODE_STREAM &&
           info->status_tokens[CURRENT_ID] > 0;
}

static int current_stream_buffer_supported(void)
{
    return current_stream_selected() &&
           info->status_tokens[STREAM_BUFFER_STATE] != STREAM_BUFFER_STATE_UNSUPPORTED;
}

static int current_stream_buffer_ready(void)
{
    return current_stream_buffer_supported() &&
           info->status_tokens[STREAM_BUFFER_ENABLED] > 0 &&
           info->status_tokens[STREAM_BUFFER_FILLED] > 0;
}

static void current_stream_buffer_warn_not_ready(void)
{
    if(!current_stream_selected())
        return;

    if(info->status_tokens[STREAM_BUFFER_STATE] == STREAM_BUFFER_STATE_UNSUPPORTED)
        vj_msg(VEEJAY_MSG_INFO, "This stream type does not expose a renderable trickplay buffer");
    else if(info->status_tokens[STREAM_BUFFER_CAPACITY] <= 0)
        vj_msg(VEEJAY_MSG_INFO, "Set a trickplay buffer length for this stream first");
    else
        vj_msg(VEEJAY_MSG_INFO, "Waiting for stream frames to fill the trickplay buffer");
}

static int current_stream_id(void)
{
    return info ? info->status_tokens[CURRENT_ID] : 0;
}

static int config_file_status = 0;
static gchar *config_file = NULL;

#define FRAMERATE_SEND_MIN_US 40000

static int framerate_last_sent_x100 = -1;
static int framerate_pending_x100 = -1;
static gint64 framerate_last_sent_us = 0;
static guint framerate_timeout_id = 0;

static inline int ui_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline double ui_clampd(double v, double lo, double hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static int framerate_slider_value_to_ceil_x100(gdouble slider_val)
{
    slider_val = ui_clampd(slider_val, 1.0, 240.0);

    int fps = (int)slider_val;
    fps += (slider_val > ((gdouble)fps + 0.000001));

    return ui_clampi(fps, 1, 240) * 100;
}


static void framerate_snap_widget_to_integer(GtkWidget *w, int value_x100)
{
    GtkAdjustment *a;
    gdouble snapped;
    int old_lock;

    if(!w || !GTK_IS_RANGE(w))
        return;

    a = gtk_range_get_adjustment(GTK_RANGE(w));
    snapped = ((gdouble)value_x100) / 100.0;

    if(gtk_adjustment_get_value(a) == snapped)
        return;

    old_lock = info->status_lock;
    info->status_lock = 1;
    gtk_adjustment_set_value(a, snapped);
    info->status_lock = old_lock;
}

static void framerate_send_x100(int value_x100, int midi_learn)
{
    value_x100 = ui_clampi(value_x100, 100, 24000);

    if(value_x100 == framerate_last_sent_x100)
        return;

    multi_vims(VIMS_FRAMERATE, "%d", value_x100);

    if(midi_learn)
        vj_midi_learning_vims_simple(info->midi, "framerate", VIMS_FRAMERATE);

    framerate_last_sent_x100 = value_x100;
    framerate_last_sent_us = g_get_monotonic_time();
}

static gboolean framerate_flush_pending_cb(gpointer data)
{
    (void)data;

    if(info && info->status_lock)
        return TRUE;

    framerate_timeout_id = 0;

    if(framerate_pending_x100 >= 0) {
        int value_x100 = framerate_pending_x100;
        framerate_pending_x100 = -1;
        framerate_send_x100(value_x100, 0);
    }

    return FALSE;
}

static inline int sample_calctime(int nframes);
static int sample_calctime_selection();
static int sample_calctime_mulloop();

static gboolean timeline_marker_mode_is_sample(void);
static gboolean timeline_marker_can_send(void);
static void timeline_marker_disable_selection_guarded(GtkWidget *widget);

static void toggle_siamese_widget(GtkWidget *widget, GtkWidget *first, GtkWidget *second, int signal_suppression)
{
    if (!widget || !first || !second)
        return;

    GtkWidget *siamese = (widget == second) ? first : second;

    if (!GTK_IS_TOGGLE_BUTTON(widget) || !GTK_IS_TOGGLE_BUTTON(siamese))
        return;

    gboolean active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(siamese)) == active) {
        return;
	}

	gulong handler_id = 0;

	if( signal_suppression) {
		guint signal_id = g_signal_lookup("toggled", GTK_TYPE_TOGGLE_BUTTON);
		handler_id = g_signal_handler_find(
			siamese,
			G_SIGNAL_MATCH_ID,
			signal_id,
			0,
			NULL,
			NULL,
			NULL
		);

		if (handler_id)
			g_signal_handler_block(siamese, handler_id);
	}

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(siamese), active);


	if( signal_suppression ) {
		if (handler_id)
			g_signal_handler_unblock(siamese, handler_id);
	}
}

void	on_button_085_clicked(GtkWidget *widget, gpointer user_data)
{
	if(current_stream_buffer_ready()) {
		multi_vims(VIMS_STREAM_BUFFER_SKIP_SECOND, "%d %d", current_stream_id(), 1);
		vj_midi_learning_vims_msg2(info->midi, NULL, VIMS_STREAM_BUFFER_SKIP_SECOND, current_stream_id(), 1);
		return;
	}
	if(current_stream_selected()) {
		current_stream_buffer_warn_not_ready();
		return;
	}
	single_vims(VIMS_VIDEO_SKIP_SECOND);
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_SKIP_SECOND );
}
void	on_button_086_clicked(GtkWidget *widget, gpointer user_data)
{
	if(current_stream_buffer_ready()) {
		multi_vims(VIMS_STREAM_BUFFER_PREV_SECOND, "%d %d", current_stream_id(), 1);
		vj_midi_learning_vims_msg2(info->midi, NULL, VIMS_STREAM_BUFFER_PREV_SECOND, current_stream_id(), 1);
		return;
	}
	if(current_stream_selected()) {
		current_stream_buffer_warn_not_ready();
		return;
	}
	single_vims(VIMS_VIDEO_PREV_SECOND );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_PREV_SECOND );

}
void	on_button_080_clicked(GtkWidget *widget, gpointer user_data)
{
	if(current_stream_buffer_ready()) {
		multi_vims(VIMS_STREAM_BUFFER_FORWARD, "%d", current_stream_id());
		vj_midi_learning_vims_msg(info->midi, NULL, VIMS_STREAM_BUFFER_FORWARD, current_stream_id());
		return;
	}
	if(current_stream_selected()) {
		current_stream_buffer_warn_not_ready();
		return;
	}
	single_vims(VIMS_VIDEO_PLAY_FORWARD);
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_PLAY_FORWARD );

}
void	on_button_081_clicked(GtkWidget *widget, gpointer user_data)
{
	if(current_stream_buffer_ready()) {
		multi_vims(VIMS_STREAM_BUFFER_BACKWARD, "%d", current_stream_id());
		vj_midi_learning_vims_msg(info->midi, NULL, VIMS_STREAM_BUFFER_BACKWARD, current_stream_id());
		return;
	}
	if(current_stream_selected()) {
		current_stream_buffer_warn_not_ready();
		return;
	}
	single_vims(VIMS_VIDEO_PLAY_BACKWARD);
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_PLAY_BACKWARD );
}
void	on_button_082_clicked(GtkWidget *widget, gpointer user_data)
{
	if(current_stream_buffer_ready()) {
		multi_vims(VIMS_STREAM_BUFFER_STOP, "%d", current_stream_id());
		vj_midi_learning_vims_msg(info->midi, NULL, VIMS_STREAM_BUFFER_STOP, current_stream_id());
		return;
	}
	if(current_stream_selected()) {
		current_stream_buffer_warn_not_ready();
		return;
	}
	single_vims( VIMS_VIDEO_PLAY_STOP );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_PLAY_STOP );
}
void	on_button_083_clicked(GtkWidget *widget, gpointer user_data)
{
	if(current_stream_buffer_ready()) {
		multi_vims(VIMS_STREAM_BUFFER_SKIP_FRAME, "%d %d", current_stream_id(), 1);
		vj_midi_learning_vims_msg2(info->midi, NULL, VIMS_STREAM_BUFFER_SKIP_FRAME, current_stream_id(), 1);
		return;
	}
	if(current_stream_selected()) {
		current_stream_buffer_warn_not_ready();
		return;
	}
	single_vims( VIMS_VIDEO_SKIP_FRAME );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_SKIP_FRAME );
}
void 	on_button_084_clicked(GtkWidget *widget, gpointer user_data)
{
	if(current_stream_buffer_ready()) {
		multi_vims(VIMS_STREAM_BUFFER_SKIP_FRAME, "%d %d", current_stream_id(), -1);
		vj_midi_learning_vims_msg2(info->midi, NULL, VIMS_STREAM_BUFFER_SKIP_FRAME, current_stream_id(), -1);
		return;
	}
	if(current_stream_selected()) {
		current_stream_buffer_warn_not_ready();
		return;
	}
	single_vims( VIMS_VIDEO_PREV_FRAME );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_PREV_FRAME );
}
void	on_button_087_clicked(GtkWidget *widget, gpointer user_data)
{
	if(current_stream_buffer_ready()) {
		multi_vims(VIMS_STREAM_BUFFER_SET_FRAME, "%d %d", current_stream_id(), 0);
		vj_midi_learning_vims_msg2(info->midi, NULL, VIMS_STREAM_BUFFER_SET_FRAME, current_stream_id(), 0);
		return;
	}
	if(current_stream_selected()) {
		current_stream_buffer_warn_not_ready();
		return;
	}
	single_vims( VIMS_VIDEO_GOTO_START );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_GOTO_START );
}
void	on_button_088_clicked(GtkWidget *widget, gpointer user_data)
{
	if(current_stream_buffer_ready()) {
		multi_vims(VIMS_STREAM_BUFFER_SET_FRAME, "%d %d", current_stream_id(), -1);
		vj_midi_learning_vims_msg2(info->midi, NULL, VIMS_STREAM_BUFFER_SET_FRAME, current_stream_id(), -1);
		return;
	}
	if(current_stream_selected()) {
		current_stream_buffer_warn_not_ready();
		return;
	}
	single_vims( VIMS_VIDEO_GOTO_END);
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_GOTO_END );
}

void	on_videobar_value_changed(GtkWidget *widget, gpointer user_data)
{
  if(!info->status_lock)
  {
    GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( widget ));
    gdouble slider_val = gtk_adjustment_get_value (a);
    gint val = 0;
    switch(info->status_tokens[PLAY_MODE])
    {
      case MODE_PLAIN:
        val = slider_val * info->status_tokens[TOTAL_FRAMES];
        break;
      case MODE_SAMPLE:
        val = slider_val * (info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START]);
        val += info->status_tokens[SAMPLE_START];
        break;
      case MODE_STREAM:
        if(!current_stream_buffer_ready())
            return;
        val = slider_val * MAX(1, info->status_tokens[STREAM_BUFFER_FILLED]);
        if(val >= info->status_tokens[STREAM_BUFFER_FILLED])
            val = info->status_tokens[STREAM_BUFFER_FILLED] - 1;
        if(val < 0)
            val = 0;
        multi_vims(VIMS_STREAM_BUFFER_SET_FRAME, "%d %d", current_stream_id(), val);
        vj_midi_learning_vims_msg2(info->midi, "videobar", VIMS_STREAM_BUFFER_SET_FRAME, current_stream_id(), val);
        return;
      default:
        return;
    }
    multi_vims( VIMS_VIDEO_SET_FRAME, "%d", val );

    vj_midi_learning_vims_simple( info->midi, "videobar", VIMS_VIDEO_SET_FRAME );
  }
}

void	on_subrender_toggled(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock) {
		multi_vims( VIMS_SUB_RENDER,"%d",0);
		vj_msg(VEEJAY_MSG_INFO, "Subrender request sent for the current chain");
	}
}

void	on_button_001_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_SET_PLAIN_MODE );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_SET_PLAIN_MODE );
}

void	on_feedbackbutton_toggled( GtkWidget *widget, gpointer data )
{
	if(!info->status_lock) {
		int val = is_button_toggled( "feedbackbutton" ) ? 1:0;
		multi_vims( VIMS_FEEDBACK, "%d", val );
		vj_midi_learning_vims_toggle(info->midi, "feedbackbutton", VIMS_FEEDBACK);
		vj_msg(VEEJAY_MSG_INFO, "Feedback rendering %s requested", val ? "enabled" : "disabled");
	}
}

static int follow_return_id = 0;
static int follow_return_type = 0;

void	on_fx_followfade_toggled( GtkWidget *widget, gpointer data )
{
	int val = is_button_toggled( "fx_followfade" ) ? 1:0;
	follow_return_id = info->status_tokens[CURRENT_ID];
	follow_return_type = info->status_tokens[PLAY_MODE];
	multi_vims( VIMS_CHAIN_FOLLOW_FADE,"%d", val );
	vj_midi_learning_vims_toggle(info->midi, "fx_followfade", VIMS_CHAIN_FOLLOW_FADE);
}

void	on_button_return_clicked( GtkWidget *widget, gpointer data)
{
	multi_vims( (follow_return_type == 0 ? VIMS_SAMPLE_SELECT: VIMS_STREAM_SELECT),"%d", follow_return_id );
}

void	on_button_252_clicked( GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_DEBUG_LEVEL );
	if(is_button_toggled( "button_252" ))
		vims_verbosity = 1;
	else
		vims_verbosity = 0;
	vj_msg(VEEJAY_MSG_INFO, "%s debug information",
		vims_verbosity ? "Displaying" : "Not displaying" );
}

void	on_button_251_clicked( GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_BEZERK );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_BEZERK );
	vj_msg(VEEJAY_MSG_INFO, "Bezerk mode toggled");
}

void	on_entry_samplename_button_clicked( GtkWidget *widget, gpointer user_data )
{
	gchar *title = get_text( "entry_samplename" );
	multi_vims( VIMS_SAMPLE_SET_DESCRIPTION, "%d %s", 0,title );

	int id = info->status_tokens[CURRENT_ID];
	int pm = info->status_tokens[PLAY_MODE];
	int type = (pm == MODE_SAMPLE ? 0 : info->status_tokens[STREAM_TYPE]);

	if(id > 0 && (pm == MODE_SAMPLE || pm == MODE_STREAM))
		samplebank_store_title_override(id, type, title);

	int idx = -1;
	int page = find_bank_by_sample_existing( id, type, &idx);

	if(page >= 0 && idx >= 0 && info->sample_banks &&
	   info->sample_banks[page] && info->sample_banks[page]->slot &&
	   info->sample_banks[page]->slot[idx])
	{
		sample_slot_t *slot = info->sample_banks[page]->slot[idx];
		const char *timecode = slot->timecode ? slot->timecode : NULL;

		slot = update_sample_slot_data(page,
		                        idx,
		                        id,
		                        type,
		                        title,
		                        (gchar *) timecode);

		if(slot && (pm == MODE_SAMPLE || pm == MODE_STREAM))
			info->selected_slot = slot;
	}

}

void	on_button_054_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *ext = get_text( "screenshotformat" );
	if(ext)
	{
		gchar filename[100];
		snprintf(filename, sizeof(filename), "frame-%d.%s", info->status_tokens[FRAME_NUM] + 1, ext);
		gint w = get_nums("screenshot_width");
		gint h = get_nums("screenshot_height");
		multi_vims( VIMS_SCREENSHOT,"%d %d %s",w,h,filename );
		vj_msg(VEEJAY_MSG_INFO, "Requested screenshot '%s' of frame %d",
			filename, info->status_tokens[FRAME_NUM] + 1 );
	}
}
void	on_button_200_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_EFFECT_SET_BG );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_EFFECT_SET_BG );
	vj_msg(VEEJAY_MSG_INFO,
		"Requested background mask of frame %d",
			info->status_tokens[FRAME_NUM] + 1 );
}

void on_beat_entry_toggle_toggled(GtkWidget *widget, gpointer user_data) {

	if(info->status_lock)
		return;

    if(info->uc.selected_chain_entry < 0) {
        vj_msg(VEEJAY_MSG_INFO, "Select an FX chain entry before enabling beat control");
        return;
    }

	int status = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

 	multi_vims( VIMS_CHAIN_ENTRY_BEAT_TOGGLE,"%d %d %d", 0, info->uc.selected_chain_entry, status );
    info->uc.reload_hint[HINT_ENTRY] = 1;
    info->uc.reload_hint[HINT_CHAIN] = 1;
    vj_msg(VEEJAY_MSG_INFO, "Beat control %s for FX chain entry %d",
           status ? "enabled" : "disabled",
           info->uc.selected_chain_entry);
}

void    on_button_audio_mute_toggled(GtkWidget *widget, gpointer user_data) {

    if(info->status_lock)
        return;

    if(!widget || !GTK_IS_TOGGLE_BUTTON(widget))
        return;

    int status = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? 1 : 0;

    multi_vims( VIMS_AUDIO_TOGGLE_MUTE, "%d", status );
    vj_midi_learning_vims_toggle(info->midi, "button_audio_mute", VIMS_AUDIO_TOGGLE_MUTE);
    vj_msg(VEEJAY_MSG_INFO, "Audio output %s requested", status ? "mute" : "unmute");

}


static int audio_beat_widget_int_value(GtkWidget *widget)
{
    if(!widget)
        return 0;

    if(GTK_IS_RANGE(widget))
        return (int) gtk_range_get_value(GTK_RANGE(widget));

    if(GTK_IS_SPIN_BUTTON(widget))
        return (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

    if(GTK_IS_COMBO_BOX(widget))
        return gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

    return 0;
}

static void audio_beat_send_int(GtkWidget *widget, int vims_id)
{
    if(info->status_lock)
        return;

    multi_vims(vims_id, "%d", audio_beat_widget_int_value(widget));
}

#define SAMPLE_VOLUME_UI_CACHE_SIZE 16385

static int sample_volume_ui_cache_initialized = 0;
static int sample_volume_ui_cache[SAMPLE_VOLUME_UI_CACHE_SIZE];

static void sample_volume_ui_cache_init(void)
{
    if(sample_volume_ui_cache_initialized)
        return;

    for(int i = 0; i < SAMPLE_VOLUME_UI_CACHE_SIZE; i++)
        sample_volume_ui_cache[i] = -1;

    sample_volume_ui_cache_initialized = 1;
}

static int volume_widget_int_value(GtkWidget *widget)
{
    int value = 100;

    if(widget && GTK_IS_RANGE(widget))
        value = (int)(gtk_range_get_value(GTK_RANGE(widget)) + 0.5);
    else if(widget && GTK_IS_SPIN_BUTTON(widget))
        value = (int)(gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);

    return ui_clampi(value, 0, 100);
}

static void volume_set_adjustment_guarded(int scale_id, int spin_id, int volume)
{
    GtkWidget *w = widget_cache[scale_id];
    GtkAdjustment *adj = NULL;
    int old_lock;

    volume = ui_clampi(volume, 0, 100);

    if(w && GTK_IS_RANGE(w))
        adj = gtk_range_get_adjustment(GTK_RANGE(w));
    else if(widget_cache[spin_id] && GTK_IS_SPIN_BUTTON(widget_cache[spin_id]))
        adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(widget_cache[spin_id]));

    if(!adj)
        return;

    if((int)(gtk_adjustment_get_value(adj) + 0.5) == volume)
        return;

    old_lock = info->status_lock;
    info->status_lock = 1;
    gtk_adjustment_set_value(adj, (gdouble)volume);
    info->status_lock = old_lock;
}

static void sample_volume_set_guarded(int volume)
{
    volume_set_adjustment_guarded(WIDGET_SAMPLE_VOLUME_SCALE,
                                  WIDGET_SAMPLE_VOLUME_SPIN,
                                  volume);
}


static void sample_volume_sync_from_current(int pm)
{
    int volume = 100;
    int sample_id;

    if(pm != MODE_SAMPLE)
        return;

    sample_id = info->status_tokens[CURRENT_ID];
    sample_volume_ui_cache_init();

    if(sample_id > 0 && sample_id < SAMPLE_VOLUME_UI_CACHE_SIZE &&
       sample_volume_ui_cache[sample_id] >= 0)
        volume = sample_volume_ui_cache[sample_id];

    sample_volume_set_guarded(volume);
}

static void sample_volume_sync_from_status(int *history, int force)
{
    int volume;
    int sample_id;

    if(info->status_tokens[PLAY_MODE] != MODE_SAMPLE)
        return;

    if(VIMS_STATUS_TOKENS <= SAMPLE_AUDIO_VOLUME) {
        sample_volume_sync_from_current(info->status_tokens[PLAY_MODE]);
        return;
    }

    if(!force && history &&
       history[PLAY_MODE] == info->status_tokens[PLAY_MODE] &&
       history[CURRENT_ID] == info->status_tokens[CURRENT_ID] &&
       history[SAMPLE_AUDIO_VOLUME] == info->status_tokens[SAMPLE_AUDIO_VOLUME])
        return;

    volume = ui_clampi(info->status_tokens[SAMPLE_AUDIO_VOLUME], 0, 100);
    sample_id = info->status_tokens[CURRENT_ID];

    sample_volume_ui_cache_init();
    if(sample_id > 0 && sample_id < SAMPLE_VOLUME_UI_CACHE_SIZE)
        sample_volume_ui_cache[sample_id] = volume;

    sample_volume_set_guarded(volume);
}

void on_sample_volume_value_changed(GtkWidget *widget, gpointer user_data)
{
    int sample_id;
    int volume;

    (void)user_data;

    if(info->status_lock)
        return;

    if(info->status_tokens[PLAY_MODE] != MODE_SAMPLE)
        return;

    sample_id = info->status_tokens[CURRENT_ID];
    if(sample_id <= 0)
        return;

    volume = volume_widget_int_value(widget);

    sample_volume_ui_cache_init();
    if(sample_id < SAMPLE_VOLUME_UI_CACHE_SIZE)
        sample_volume_ui_cache[sample_id] = volume;

    multi_vims(VIMS_SAMPLE_SET_VOLUME, "%d %d", 0, volume);
    vj_midi_learning_vims_simple(info->midi, "sample_volume", VIMS_SAMPLE_SET_VOLUME);
}

void on_audio_master_jack_volume_value_changed(GtkWidget *widget, gpointer user_data)
{
    int volume;

    (void)user_data;

    if(info->status_lock)
        return;

    if(audio_input_selector_active_from_ui() != AUDIO_MASTER_JACK_UI_VALUE)
        return;

    volume = volume_widget_int_value(widget);

    multi_vims(VIMS_SET_VOLUME, "%d", volume);
    vj_midi_learning_vims_simple(info->midi, "audio_master_jack_volume", VIMS_SET_VOLUME);
}

static int audio_mixer_mode_from_ui(void)
{
    GtkWidget *override = widget_cache[WIDGET_AUDIO_MIXER_OVERRIDE_TOGGLE];
    GtkWidget *original = widget_cache[WIDGET_AUDIO_MIXER_ORIGINAL_RADIO];
    GtkWidget *external = widget_cache[WIDGET_AUDIO_MIXER_EXTERNAL_RADIO];
    GtkWidget *mix = widget_cache[WIDGET_AUDIO_MIXER_MIX_RADIO];

    if(!override || !GTK_IS_TOGGLE_BUTTON(override))
        return AUDIO_MIX_MODE_FOLLOW;

    if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(override)))
        return AUDIO_MIX_MODE_FOLLOW;

    if(original && GTK_IS_TOGGLE_BUTTON(original) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(original)))
        return AUDIO_MIX_MODE_ORIGINAL;
    if(external && GTK_IS_TOGGLE_BUTTON(external) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(external)))
        return AUDIO_MIX_MODE_JACK;
    if(mix && GTK_IS_TOGGLE_BUTTON(mix) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mix)))
        return AUDIO_MIX_MODE_ORIGINAL_JACK;

    return AUDIO_MIX_MODE_ORIGINAL_JACK;
}

static int audio_mixer_crossfade_from_ui(void)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_MIXER_CROSSFADE_SCALE];

    if(!w)
        w = widget_cache[WIDGET_AUDIO_MIXER_CROSSFADE_SPIN];

    return volume_widget_int_value(w);
}

static void audio_mixer_set_crossfade_sensitive(int sensitive)
{
    GtkWidget *w;

    w = widget_cache[WIDGET_AUDIO_MIXER_CROSSFADE_LABEL];
    if(w)
        gtk_widget_set_sensitive(w, sensitive ? TRUE : FALSE);

    w = widget_cache[WIDGET_AUDIO_MIXER_CROSSFADE_SCALE];
    if(w)
        gtk_widget_set_sensitive(w, sensitive ? TRUE : FALSE);

    w = widget_cache[WIDGET_AUDIO_MIXER_CROSSFADE_SPIN];
    if(w)
        gtk_widget_set_sensitive(w, sensitive ? TRUE : FALSE);
}

static void audio_mixer_update_crossfade_sensitivity(void)
{
    const int blend = (audio_mixer_mode_from_ui() == AUDIO_MIX_MODE_ORIGINAL_EXTERNAL);

    audio_mixer_set_crossfade_sensitive(audio_global_source_controls_allowed() && blend);
}

static void audio_mixer_set_mode_guarded(int mode)
{
    GtkWidget *override = widget_cache[WIDGET_AUDIO_MIXER_OVERRIDE_TOGGLE];
    GtkWidget *original = widget_cache[WIDGET_AUDIO_MIXER_ORIGINAL_RADIO];
    GtkWidget *external = widget_cache[WIDGET_AUDIO_MIXER_EXTERNAL_RADIO];
    GtkWidget *mix = widget_cache[WIDGET_AUDIO_MIXER_MIX_RADIO];
    int old_lock;

    mode = ui_clampi(mode, AUDIO_MIX_MODE_FOLLOW, AUDIO_MIX_MODE_ORIGINAL_JACK);

    old_lock = info->status_lock;
    info->status_lock = 1;


    if(override && GTK_IS_TOGGLE_BUTTON(override))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(override), mode != AUDIO_MIX_MODE_FOLLOW);

    if(mode == AUDIO_MIX_MODE_ORIGINAL && original && GTK_IS_TOGGLE_BUTTON(original))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(original), TRUE);
    else if(mode == AUDIO_MIX_MODE_JACK && external && GTK_IS_TOGGLE_BUTTON(external))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(external), TRUE);
    else if(mode == AUDIO_MIX_MODE_ORIGINAL_JACK && mix && GTK_IS_TOGGLE_BUTTON(mix))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mix), TRUE);

    if(mode == AUDIO_MIX_MODE_FOLLOW && mix && GTK_IS_TOGGLE_BUTTON(mix))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mix), TRUE);

    if(original && GTK_IS_WIDGET(original))
        gtk_widget_set_sensitive(original, mode != AUDIO_MIX_MODE_FOLLOW);
    if(external && GTK_IS_WIDGET(external))
        gtk_widget_set_sensitive(external, mode != AUDIO_MIX_MODE_FOLLOW);
    if(mix && GTK_IS_WIDGET(mix))
        gtk_widget_set_sensitive(mix, mode != AUDIO_MIX_MODE_FOLLOW);

    info->status_lock = old_lock;
}

static void audio_mixer_set_crossfade_guarded(int crossfade)
{
    volume_set_adjustment_guarded(WIDGET_AUDIO_MIXER_CROSSFADE_SCALE,
                                  WIDGET_AUDIO_MIXER_CROSSFADE_SPIN,
                                  crossfade);
}

static void audio_mixer_mode_sync_from_status(int *history, int force)
{
    int mode;

    if(VIMS_STATUS_TOKENS <= AUDIO_MIX_MODE)
        return;

    if(!force && history &&
       history[AUDIO_MIX_MODE] == info->status_tokens[AUDIO_MIX_MODE])
        return;

    mode = ui_clampi(info->status_tokens[AUDIO_MIX_MODE],
                     AUDIO_MIX_MODE_FOLLOW,
                     AUDIO_MIX_MODE_ORIGINAL_EXTERNAL);
    audio_mixer_set_mode_guarded(mode);
    audio_mixer_update_crossfade_sensitivity();
}

static void audio_mixer_crossfade_sync_from_status(int *history, int force)
{
    int crossfade;

    if(VIMS_STATUS_TOKENS <= AUDIO_MIX_CROSSFADE)
        return;

    if(!force && history &&
       history[AUDIO_MIX_CROSSFADE] == info->status_tokens[AUDIO_MIX_CROSSFADE])
        return;

    crossfade = ui_clampi(info->status_tokens[AUDIO_MIX_CROSSFADE], 0, 100);
    audio_mixer_set_crossfade_guarded(crossfade);
}

static void audio_mixer_reset_to_follow_route(void)
{
    audio_mixer_set_mode_guarded(AUDIO_MIX_MODE_FOLLOW);
    audio_mixer_update_crossfade_sensitivity();
    multi_vims(VIMS_AUDIO_MIX_MODE, "%d", AUDIO_MIX_MODE_FOLLOW);
}

static void audio_mixer_send_manual_state(int mode, int crossfade)
{
    mode = ui_clampi(mode, AUDIO_MIX_MODE_FOLLOW, AUDIO_MIX_MODE_ORIGINAL_JACK);
    crossfade = ui_clampi(crossfade, 0, 100);

    if(mode == AUDIO_MIX_MODE_ORIGINAL_EXTERNAL)
        multi_vims(VIMS_AUDIO_MIX_CROSSFADE, "%d", crossfade);

    multi_vims(VIMS_AUDIO_MIX_MODE, "%d", mode);
}

static void audio_mixer_commit_mode_from_ui(const char *midi_widget)
{
    int mode;
    int crossfade;

    if(info->status_lock)
        return;

    if(!audio_global_source_controls_allowed())
        return;

    mode = audio_mixer_mode_from_ui();
    crossfade = audio_mixer_crossfade_from_ui();

    switch(mode) {
        case AUDIO_MIX_MODE_ORIGINAL:
            crossfade = 0;
            audio_mixer_set_crossfade_guarded(crossfade);
            break;
        case AUDIO_MIX_MODE_JACK:
            crossfade = 100;
            audio_mixer_set_crossfade_guarded(crossfade);
            break;
        case AUDIO_MIX_MODE_ORIGINAL_JACK:
            if(crossfade <= 0 || crossfade >= 100) {
                crossfade = 50;
                audio_mixer_set_crossfade_guarded(crossfade);
            }
            break;
        case AUDIO_MIX_MODE_FOLLOW:
        default:
            break;
    }

    audio_mixer_set_mode_guarded(mode);
    audio_mixer_update_crossfade_sensitivity();
    audio_mixer_send_manual_state(mode, crossfade);

    if(midi_widget)
        vj_midi_learning_vims_simple(info->midi, (char *) midi_widget, VIMS_AUDIO_MIX_MODE);
}

void on_audio_mixer_override_toggled(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;

    if(info->status_lock)
        return;

    if(!widget || !GTK_IS_TOGGLE_BUTTON(widget))
        return;

    audio_mixer_commit_mode_from_ui("audio_mixer_override");
}

void on_audio_mixer_route_toggled(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;

    if(info->status_lock)
        return;

    if(!widget || !GTK_IS_TOGGLE_BUTTON(widget))
        return;

    if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
        return;

    audio_mixer_commit_mode_from_ui("audio_mixer_route");
}

void on_audio_mixer_mode_combo_changed(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;
}

void on_audio_mixer_crossfade_value_changed(GtkWidget *widget, gpointer user_data)
{
    int crossfade;
    int mode;

    (void)user_data;

    if(info->status_lock)
        return;

    if(!audio_global_source_controls_allowed())
        return;

    mode = audio_mixer_mode_from_ui();
    if(mode != AUDIO_MIX_MODE_ORIGINAL_EXTERNAL)
        return;

    crossfade = volume_widget_int_value(widget);

    multi_vims(VIMS_AUDIO_MIX_CROSSFADE, "%d", crossfade);
    vj_midi_learning_vims_simple(info->midi, "audio_mixer_crossfade", VIMS_AUDIO_MIX_CROSSFADE);
}

static int audio_beat_action_sanitize(int action)
{
    switch(action) {
        case 2:
        case 3:
        case 4:
            return action;
        case 0:
        case 1:
        default:
            return 0;
    }
}

static int audio_beat_action_from_combo_index(int idx)
{
    switch(idx) {
        case 1: return 2;
        case 2: return 3;
        case 3: return 4;
        case 0:
        default: return 0;
    }
}

static int audio_beat_combo_index_from_action(int action)
{
    switch(audio_beat_action_sanitize(action)) {
        case 2: return 1;
        case 3: return 2;
        case 4: return 3;
        case 0:
        default: return 0;
    }
}

static int audio_beat_current_action_from_combo(void)
{
    return audio_beat_action_from_combo_index(
        audio_beat_widget_int_value(widget_cache[WIDGET_AUDIO_BEAT_ACTION_COMBO]));
}

static const char *audio_beat_action_name(int mode)
{
    switch(audio_beat_action_sanitize(mode)) {
        case 2: return "auto FX";
        case 3: return "break beat (Auto FX)";
        case 4: return "break beat";
        case 0:
        default: return "none";
    }
}

static const char *audio_beat_auto_mode_name(int mode)
{
    switch(mode) {
        case 1: return "primary";
        case 2: return "primary + motion";
        case 3: return "motion + memory";
        case 4: return "chaos";
        case 0:
        default: return "off";
    }
}

static void audio_beat_set_widget_sensitive(int widget_id, int sensitive)
{
    GtkWidget *w = widget_cache[widget_id];

    if(w)
        gtk_widget_set_sensitive(w, sensitive ? TRUE : FALSE);
}

static void audio_beat_set_label_text(int widget_id, const char *text)
{
    GtkWidget *w = widget_cache[widget_id];

    if(w && GTK_IS_LABEL(w))
        gtk_label_set_text(GTK_LABEL(w), text);
}

static void audio_beat_set_widget_tooltip(int widget_id, const char *text)
{
    GtkWidget *w = widget_cache[widget_id];

    if(w)
        gtk_widget_set_tooltip_text(w, text);
}

static void audio_beat_set_control_tooltip(int label_id, int scale_id, int spin_id, const char *text)
{
    audio_beat_set_widget_tooltip(label_id, text);
    audio_beat_set_widget_tooltip(scale_id, text);
    audio_beat_set_widget_tooltip(spin_id, text);
}

static void audio_beat_update_action_labels(int action)
{
    const char *threshold = "Detect Threshold";
    const char *cooldown = "Detect Cooldown";
    const char *hold = "Hold Window (inactive)";
    const char *pulse = "Pulse Width";
    const char *gate = "Gate Hold";
    const char *threshold_tip = "Minimum transient strength needed to register a beat event.";
    const char *cooldown_tip = "Minimum time between accepted beat events. Higher values suppress double triggers.";
    const char *hold_tip = "Beat hold duration. Used by Break Beat only.";
    const char *pulse_tip = "Length of the short beat pulse sent to beat-aware FX.";
    const char *gate_tip = "Length of the held beat gate sent to beat-aware FX.";

    switch(audio_beat_action_sanitize(action)) {
        case 2:
            threshold = "FX Threshold";
            cooldown = "FX Cooldown";
            hold = "Hold Window (inactive)";
            pulse = "FX Pulse";
            gate = "FX Gate";
            threshold_tip = "Transient threshold that drives automatic FX modulation.";
            cooldown_tip = "Minimum time between Auto FX modulation hits.";
            hold_tip = "Hold window is not used by Auto FX only.";
            pulse_tip = "Length of the Auto FX pulse envelope.";
            gate_tip = "Length of the Auto FX gate envelope.";
            break;
        case 3:
            threshold = "Break FX Threshold";
            cooldown = "Break FX Cooldown";
            hold = "Hold Window";
            pulse = "Scratch Pulse";
            gate = "Scratch Gate";
            threshold_tip = "Hit threshold for Break Beat transport while Auto FX remains active.";
            cooldown_tip = "Minimum turn/hit spacing for Break Beat. Auto FX modulation still follows beat hints.";
            hold_tip = "Open/hold window used for Break Beat transport bursts.";
            pulse_tip = "Scratch pulse/slice duration for Break Beat and Auto FX pulse modulation.";
            gate_tip = "Scratch gate and repeat-memory window; Auto FX gate modulation stays active.";
            break;
        case 4:
            threshold = "Hit Threshold";
            cooldown = "Turn Cooldown";
            hold = "Hold Window";
            pulse = "Scratch Pulse";
            gate = "Scratch Gate";
            threshold_tip = "Hit threshold for Break Beat transport and scratch events.";
            cooldown_tip = "Minimum turn/hit spacing for Break Beat. Lower values admit faster scratching.";
            hold_tip = "Open/hold window used for Break Beat transport bursts.";
            pulse_tip = "Scratch pulse/slice emphasis duration for visual and transport response.";
            gate_tip = "Scratch gate and repeat-memory window for held Break Beat response.";
            break;
        case 0:
        default:
            break;
    }

    audio_beat_set_label_text(WIDGET_AUDIO_BEAT_THRESHOLD_LABEL, threshold);
    audio_beat_set_label_text(WIDGET_AUDIO_BEAT_COOLDOWN_LABEL, cooldown);
    audio_beat_set_label_text(WIDGET_AUDIO_BEAT_FREEZE_LABEL, hold);
    audio_beat_set_label_text(WIDGET_AUDIO_BEAT_PULSE_LABEL, pulse);
    audio_beat_set_label_text(WIDGET_AUDIO_BEAT_GATE_LABEL, gate);

    audio_beat_set_control_tooltip(WIDGET_AUDIO_BEAT_THRESHOLD_LABEL, WIDGET_AUDIO_BEAT_THRESHOLD_SCALE, WIDGET_AUDIO_BEAT_THRESHOLD_SPIN, threshold_tip);
    audio_beat_set_control_tooltip(WIDGET_AUDIO_BEAT_COOLDOWN_LABEL, WIDGET_AUDIO_BEAT_COOLDOWN_SCALE, WIDGET_AUDIO_BEAT_COOLDOWN_SPIN, cooldown_tip);
    audio_beat_set_control_tooltip(WIDGET_AUDIO_BEAT_FREEZE_LABEL, WIDGET_AUDIO_BEAT_FREEZE_SCALE, WIDGET_AUDIO_BEAT_FREEZE_SPIN, hold_tip);
    audio_beat_set_control_tooltip(WIDGET_AUDIO_BEAT_PULSE_LABEL, WIDGET_AUDIO_BEAT_PULSE_SCALE, WIDGET_AUDIO_BEAT_PULSE_SPIN, pulse_tip);
    audio_beat_set_control_tooltip(WIDGET_AUDIO_BEAT_GATE_LABEL, WIDGET_AUDIO_BEAT_GATE_SCALE, WIDGET_AUDIO_BEAT_GATE_SPIN, gate_tip);
}

static int audio_beat_monitor_latency_auto_active(void)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_BEAT_MONITOR_LATENCY_AUTO_TOGGLE];

    if(!w || !GTK_IS_TOGGLE_BUTTON(w))
        return 1;

    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)) ? 1 : 0;
}

static void audio_beat_update_monitor_latency_sensitivity(int action)
{
    const int active = (action == 3 || action == 4);
    const int manual = active && !audio_beat_monitor_latency_auto_active();

    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_MONITOR_LATENCY_AUTO_TOGGLE, active);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_MONITOR_LATENCY_SCALE, manual);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_MONITOR_LATENCY_SPIN, manual);
}

static void audio_beat_update_action_sensitivity(int action)
{
    action = audio_beat_action_sanitize(action);

    const int uses_break = (action == 3 || action == 4);
    const int uses_hold = uses_break;
    const int uses_auto = (action == 2 || action == 3);

    audio_beat_update_action_labels(action);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_FREEZE_SCALE, uses_hold);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_FREEZE_SPIN, uses_hold);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_AUTO_MODE_COMBO, uses_auto);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_AUTO_AMOUNT_SCALE, uses_auto);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_AUTO_AMOUNT_SPIN, uses_auto);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_AUTO_RESET_BUTTON, uses_auto);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_SCRATCH_SENSITIVITY_SCALE, uses_break);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_SCRATCH_SENSITIVITY_SPIN, uses_break);
    audio_beat_set_widget_sensitive(WIDGET_AUDIO_BEAT_SOURCE_LOSS_PAUSE_TOGGLE, uses_break);
    audio_beat_update_monitor_latency_sensitivity(action);
}

static void audio_beat_enable_chain_entry_toggle_guarded(int active)
{
    GtkWidget *w = widget_cache[WIDGET_CHAIN_ENTRY_BEAT_TOGGLE];

    if(!w || !GTK_IS_TOGGLE_BUTTON(w))
        return;

    active = active ? 1 : 0;

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)) == active)
        return;

    int osl = info->status_lock;
    info->status_lock = 1;

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), active);

    info->status_lock = osl;
}

enum {
    AUDIO_MASTER_ORIGINAL = 0,
    AUDIO_MASTER_JACK     = 1,
    AUDIO_MASTER_WAV      = 2,
    AUDIO_MASTER_SILENCE  = 3
};

static int audio_input_selector_active_from_ui(void);
static const char *audio_input_selector_name_from_active(int active);

static int audio_current_sample_has_own_audio_source(void)
{
    int source;
    int mode;

    if(!info || info->status_tokens[PLAY_MODE] != MODE_SAMPLE)
        return 0;

    if(VIMS_STATUS_TOKENS <= SAMPLE_AUDIO_SYNC_MODE)
        return 0;

    source = info->status_tokens[SAMPLE_AUDIO_SYNC_SOURCE];
    mode = info->status_tokens[SAMPLE_AUDIO_SYNC_MODE];

    if(mode == SAMPLE_AUDIO_SYNC_OFF)
        return 0;

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_ORIGINAL)
        return 1;

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_JACK)
        return 1;

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_WAV)
        return info->status_tokens[SAMPLE_AUDIO_SYNC_PROFILE] > 0;

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_SILENCE)
        return 1;

    return 0;
}

static int audio_global_source_controls_allowed(void)
{
    return 1;
}

static int audio_input_selector_external_from_ui(void)
{
    int active = audio_input_selector_active_from_ui();

    return active == AUDIO_MASTER_JACK || active == AUDIO_MASTER_WAV;
}
static void audio_sync_set_mode_combo_guarded(int mode);
static void audio_sync_set_enable_toggle_guarded(int enabled);
static void audio_sync_set_master_wav_options_visible(int visible);
static void audio_sync_deactivate_playback(void);

static int audio_beat_action_allowed_for_master(int master, int action)
{
    action = audio_beat_action_sanitize(action);

    switch(master) {
        case AUDIO_MASTER_JACK:
        case AUDIO_MASTER_WAV:
            return (action == 0 || action == 2 || action == 3 || action == 4);
        case AUDIO_MASTER_ORIGINAL:
            return (action == 0 || action == 2);
        case AUDIO_MASTER_SILENCE:
        default:
            return (action == 0);
    }
}

static int audio_beat_safe_action_for_master(int master, int action)
{
    if(audio_beat_action_allowed_for_master(master, action))
        return action;

    return (master == AUDIO_MASTER_ORIGINAL) ? 2 : 0;
}

static void audio_beat_set_action_combo_guarded(int action)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_BEAT_ACTION_COMBO];
    int old_lock;

    if(!w || !GTK_IS_COMBO_BOX(w))
        return;

    const int combo_index = audio_beat_combo_index_from_action(action);

    if(gtk_combo_box_get_active(GTK_COMBO_BOX(w)) == combo_index)
        return;

    old_lock = info->status_lock;
    info->status_lock = 1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), combo_index);
    info->status_lock = old_lock;
}

static void audio_beat_enforce_action_for_master(int master, int notify)
{
    int action = audio_beat_current_action_from_combo();
    int safe_action = audio_beat_safe_action_for_master(master, action);

    if(action == safe_action) {
        audio_beat_update_action_sensitivity(action);
        return;
    }

    audio_beat_set_action_combo_guarded(safe_action);
    audio_beat_update_action_sensitivity(safe_action);
    multi_vims(VIMS_AUDIO_BEAT_ACTION, "%d", safe_action);

    if(notify)
        vj_msg(VEEJAY_MSG_WARNING,
               "%s cannot use %s; using %s",
               audio_input_selector_name_from_active(master),
               audio_beat_action_name(action),
               audio_beat_action_name(safe_action));
}

void on_audio_beat_enable_toggle_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
    if(info->status_lock)
        return;

    const int enabled = gtk_toggle_button_get_active(togglebutton) ? 1 : 0;

    if(enabled) {
        const int active = audio_input_selector_active_from_ui();

        if(active == AUDIO_MASTER_SILENCE) {
            int old_lock = info->status_lock;
            info->status_lock = 1;
            gtk_toggle_button_set_active(togglebutton, FALSE);
            info->status_lock = old_lock;
            vj_msg(VEEJAY_MSG_WARNING,
                   "Audio beat detector cannot analyze Silence; choose Original video audio, JACK external, or WAV file");
            return;
        }

        if(active == AUDIO_MASTER_ORIGINAL) {
            audio_sync_set_mode_combo_guarded(0);
            audio_sync_set_enable_toggle_guarded(0);
            audio_sync_deactivate_playback();
            audio_sync_set_master_wav_options_visible(0);
            multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", VJ_RECORD_AUDIO_SOURCE_ORIGINAL);
        }

        audio_beat_enforce_action_for_master(active, 1);
        audio_beat_enable_chain_entry_toggle_guarded(1);
    }

    audio_beat_update_action_sensitivity(audio_beat_current_action_from_combo());

    multi_vims(VIMS_AUDIO_BEAT_STATUS, "%d", enabled);
    vj_midi_learning_vims_toggle(info->midi, "audio_beat_enable_toggle", VIMS_AUDIO_BEAT_STATUS);
    vj_msg(VEEJAY_MSG_INFO, "Audio beat detector %s requested", enabled ? "enabled" : "disabled");
}
void on_audio_beat_action_combo_changed(GtkWidget *widget, gpointer user_data)
{
    int combo_index;
    int action;

    if(info->status_lock)
        return;

    combo_index = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
    if(combo_index < 0)
        return;

    action = audio_beat_action_from_combo_index(combo_index);

    const int master = audio_input_selector_active_from_ui();
    const int safe_action = audio_beat_safe_action_for_master(master, action);

    if(action != safe_action) {
        audio_beat_set_action_combo_guarded(safe_action);
        audio_beat_update_action_sensitivity(safe_action);
        multi_vims(VIMS_AUDIO_BEAT_ACTION, "%d", safe_action);
        vj_msg(VEEJAY_MSG_WARNING,
               "%s cannot use %s; using %s",
               audio_input_selector_name_from_active(master),
               audio_beat_action_name(action),
               audio_beat_action_name(safe_action));
        return;
    }

    multi_vims(VIMS_AUDIO_BEAT_ACTION, "%d", action);
    audio_beat_update_action_sensitivity(action);
    vj_msg(VEEJAY_MSG_INFO, "Audio beat action: %s", audio_beat_action_name(action));
}

void on_audio_beat_channels_spin_value_changed(GtkWidget *widget, gpointer user_data)
{
    audio_beat_send_int(widget, VIMS_AUDIO_BEAT_CHANNELS);
}

void on_audio_beat_threshold_value_changed(GtkWidget *widget, gpointer user_data)
{
    audio_beat_send_int(widget, VIMS_AUDIO_BEAT_THRESHOLD);
}

void on_audio_beat_cooldown_value_changed(GtkWidget *widget, gpointer user_data)
{
    audio_beat_send_int(widget, VIMS_AUDIO_BEAT_COOLDOWN);
}

void on_audio_beat_freeze_value_changed(GtkWidget *widget, gpointer user_data)
{
    audio_beat_send_int(widget, VIMS_AUDIO_BEAT_FREEZE);
}

void on_audio_beat_pulse_value_changed(GtkWidget *widget, gpointer user_data)
{
    audio_beat_send_int(widget, VIMS_AUDIO_BEAT_PULSE);
}

void on_audio_beat_gate_value_changed(GtkWidget *widget, gpointer user_data)
{
    audio_beat_send_int(widget, VIMS_AUDIO_BEAT_GATE);
}

void on_audio_beat_auto_mode_combo_changed(GtkWidget *widget, gpointer user_data)
{
    int mode;

    if(info->status_lock)
        return;

    mode = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
    if(mode < 0)
        return;

    multi_vims(VIMS_AUDIO_BEAT_AUTO_MODE, "%d", mode);
    vj_msg(VEEJAY_MSG_INFO, "Audio beat Auto FX mode: %s", audio_beat_auto_mode_name(mode));
}

void on_audio_beat_auto_amount_value_changed(GtkWidget *widget, gpointer user_data)
{
    audio_beat_send_int(widget, VIMS_AUDIO_BEAT_AUTO_AMOUNT);
}

void on_audio_beat_scratch_sensitivity_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    audio_beat_send_int(widget, VIMS_AUDIO_BEAT_SCRATCH_SENSITIVITY);
}

void on_audio_beat_source_loss_pause_toggled(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;

    if(info->status_lock)
        return;

    if(!widget || !GTK_IS_TOGGLE_BUTTON(widget))
        return;

    multi_vims(VIMS_AUDIO_BEAT_SOURCE_LOSS_PAUSE,
               "%d",
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? 1 : 0);
    vj_midi_learning_vims_toggle(info->midi, "audio_beat_source_loss_pause_toggle", VIMS_AUDIO_BEAT_SOURCE_LOSS_PAUSE);
}

void on_audio_beat_monitor_latency_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;

    if(info->status_lock)
        return;

    if(audio_beat_monitor_latency_auto_active())
        return;

    multi_vims(VIMS_AUDIO_BEAT_MONITOR_LATENCY, "%d", audio_beat_widget_int_value(widget));
}

void on_audio_beat_monitor_latency_auto_toggled(GtkWidget *widget, gpointer user_data)
{
    int action;

    (void)user_data;

    if(info->status_lock)
        return;

    if(!widget || !GTK_IS_TOGGLE_BUTTON(widget))
        return;

    action = audio_beat_current_action_from_combo();
    audio_beat_update_monitor_latency_sensitivity(action);

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
        multi_vims(VIMS_AUDIO_BEAT_MONITOR_LATENCY, "%d", -1);
    else
        multi_vims(VIMS_AUDIO_BEAT_MONITOR_LATENCY, "%d",
                   audio_beat_widget_int_value(widget_cache[WIDGET_AUDIO_BEAT_MONITOR_LATENCY_SPIN]));
}

void on_audio_beat_refresh_button_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return;

    single_vims(VIMS_AUDIO_BEAT_PRINT);
    vj_msg(VEEJAY_MSG_INFO, "Requested Audio Beat status refresh");
}

void on_audio_beat_auto_reset_button_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return;

    single_vims(VIMS_AUDIO_BEAT_AUTO_RESET);
    vj_msg(VEEJAY_MSG_INFO, "Requested Audio Beat Auto FX mapping reset");
}


static int audio_sync_last_non_jack_master = VJ_RECORD_AUDIO_SOURCE_ORIGINAL;

static void audio_input_selector_set_guarded(int active)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_INPUT_SELECTOR_COMBO];
    int old_lock;

    if(!w || !GTK_IS_COMBO_BOX(w))
        return;

    if(gtk_combo_box_get_active(GTK_COMBO_BOX(w)) == active)
        return;

    old_lock = info->status_lock;
    info->status_lock = 1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), active);
    info->status_lock = old_lock;
}

static int audio_input_selector_record_source_from_active(int active)
{
    switch(active) {
        case AUDIO_MASTER_JACK:
        case AUDIO_MASTER_WAV:


            return VJ_RECORD_AUDIO_SOURCE_BEAT_JACK;
        case AUDIO_MASTER_SILENCE:
            return VJ_RECORD_AUDIO_SOURCE_SILENCE;
        case AUDIO_MASTER_ORIGINAL:
        default:
            return VJ_RECORD_AUDIO_SOURCE_ORIGINAL;
    }
}

static int audio_input_selector_active_from_record_source(int source)
{
    switch(source) {
        case VJ_RECORD_AUDIO_SOURCE_BEAT_JACK: return AUDIO_MASTER_JACK;
        case VJ_RECORD_AUDIO_SOURCE_SILENCE:   return AUDIO_MASTER_SILENCE;
        case VJ_RECORD_AUDIO_SOURCE_AUTO:
        case VJ_RECORD_AUDIO_SOURCE_ORIGINAL:
        default: return AUDIO_MASTER_ORIGINAL;
    }
}

static int audio_input_selector_active_from_ui(void)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_INPUT_SELECTOR_COMBO];

    if(!w || !GTK_IS_COMBO_BOX(w))
        return AUDIO_MASTER_ORIGINAL;

    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
    return (active < 0) ? AUDIO_MASTER_ORIGINAL : active;
}

static int audio_input_selector_wav_from_ui(void)
{
    return audio_input_selector_active_from_ui() == AUDIO_MASTER_WAV;
}

static const char *audio_input_selector_name_from_active(int active)
{
    switch(active) {
        case AUDIO_MASTER_JACK:    return "JACK external";
        case AUDIO_MASTER_WAV:     return "WAV file";
        case AUDIO_MASTER_SILENCE: return "Silence";
        case AUDIO_MASTER_ORIGINAL:
        default: return "Original video audio";
    }
}

static void audio_sync_remember_non_jack_master(int record_source)
{
    if(record_source == VJ_RECORD_AUDIO_SOURCE_ORIGINAL ||
       record_source == VJ_RECORD_AUDIO_SOURCE_SILENCE)
        audio_sync_last_non_jack_master = record_source;
}

static int audio_sync_non_jack_master_from_status(void)
{
    int source = info->status_tokens[RECORD_AUDIO_SOURCE];

    if(source == VJ_RECORD_AUDIO_SOURCE_ORIGINAL ||
       source == VJ_RECORD_AUDIO_SOURCE_SILENCE)
    {
        audio_sync_last_non_jack_master = source;
        return source;
    }

    return audio_sync_last_non_jack_master;
}

static int audio_sync_get_combo_active(int widget_id, int fallback)
{
    GtkWidget *w = widget_cache[widget_id];

    if(!w || !GTK_IS_COMBO_BOX(w))
        return fallback;

    int v = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
    return (v < 0) ? fallback : v;
}

static int audio_sync_mode_combo_to_mode(int active)
{
    switch(active) {
        case 1: return VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL;
        case 2: return VJ_AUDIO_SYNC_MODE_MONITOR;
        case 3: return VJ_AUDIO_SYNC_MODE_MONITOR_TRICKPLAY;
        case 4: return VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW;
        case 5: return VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE;
        case 6: return VJ_AUDIO_SYNC_MODE_TRACK_ALIGN;
        case 0:
        default: return 0;
    }
}

static int audio_sync_mode_combo_from_mode(int mode)
{
    switch(mode) {
        case VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL:      return 1;
        case VJ_AUDIO_SYNC_MODE_MONITOR:            return 2;
        case VJ_AUDIO_SYNC_MODE_MONITOR_TRICKPLAY:  return 3;
        case VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW:       return 4;
        case VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE:       return 5;
        case VJ_AUDIO_SYNC_MODE_TRACK_ALIGN:        return 6;
        case 0:
        default: return 0;
    }
}

static int audio_sync_mode_supports_wav(int mode)
{
    return (mode == VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL ||
            mode == VJ_AUDIO_SYNC_MODE_MONITOR ||
            mode == VJ_AUDIO_SYNC_MODE_MONITOR_TRICKPLAY ||
            mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW ||
            mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE ||
            mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN);
}

static int audio_sync_mode_is_control_only(int mode)
{
    return (mode == VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL);
}

static int audio_sync_mode_is_tempo_follow(int mode)
{
    return (mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW);
}

static int audio_sync_mode_is_provider_only(int mode)
{
    return audio_sync_mode_is_control_only(mode) || audio_sync_mode_is_tempo_follow(mode);
}

static void audio_sync_set_mode_combo_guarded(int mode)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_SYNC_MODE_COMBO];
    int active = audio_sync_mode_combo_from_mode(mode);
    int old_lock;

    if(!w || !GTK_IS_COMBO_BOX(w))
        return;

    if(gtk_combo_box_get_active(GTK_COMBO_BOX(w)) == active)
        return;

    old_lock = info->status_lock;
    info->status_lock = 1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), active);
    info->status_lock = old_lock;
}

static void audio_sync_set_enable_toggle_guarded(int enabled)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_SYNC_ENABLE_TOGGLE];
    int old_lock;

    if(!w || !GTK_IS_TOGGLE_BUTTON(w))
        return;

    enabled = enabled ? 1 : 0;

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)) == enabled)
        return;

    old_lock = info->status_lock;
    info->status_lock = 1;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), enabled ? TRUE : FALSE);
    info->status_lock = old_lock;
}

static int audio_sync_mode_from_ui(void)
{
    return audio_sync_mode_combo_to_mode(
        audio_sync_get_combo_active(WIDGET_AUDIO_SYNC_MODE_COMBO, 0));
}

static int audio_sync_channels_from_ui(void)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_SYNC_CHANNELS_SPIN];
    int ch = 2;

    if(w && GTK_IS_SPIN_BUTTON(w) && gtk_widget_get_sensitive(w))
        ch = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));

    return ui_clampi(ch, 1, 2);
}

static int audio_sync_spin_int(int widget_id, int fallback)
{
    GtkWidget *w = widget_cache[widget_id];

    if(!w || !GTK_IS_SPIN_BUTTON(w))
        return fallback;

    return (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));
}

static int audio_sync_bpm_x10_from_ui(void)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_SYNC_TARGET_BPM_SPIN];
    double bpm = 128.0;

    if(w && GTK_IS_SPIN_BUTTON(w))
        bpm = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));

    bpm = ui_clampd(bpm, 40.0, 240.0);

    return (int)((bpm * 10.0) + 0.5);
}

static int audio_sync_loop_from_ui(void)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_SYNC_WAV_LOOP_TOGGLE];

    if(!w || !GTK_IS_TOGGLE_BUTTON(w))
        return 0;

    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)) ? 1 : 0;
}

static void audio_sync_set_master_wav_options_visible(int visible)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_MASTER_WAV_OPTIONS_GRID];

    if(w)
        gtk_widget_set_visible(w, visible ? TRUE : FALSE);
}

static void audio_sync_set_external_source_guarded(int use_wav)
{
    audio_input_selector_set_guarded(use_wav ? AUDIO_MASTER_WAV : AUDIO_MASTER_JACK);
    audio_sync_set_master_wav_options_visible(use_wav);
}

static int audio_sync_wav_path_vims_safe(const char *path)
{
    const unsigned char *p;

    if(!path || path[0] == '\0')
        return 0;



    for(p = (const unsigned char *)path; *p; p++) {
        if(*p == ';' || *p == '\n' || *p == '\r')
            return 0;
    }

    return 1;
}

static int audio_sync_wav_path_ready(int allow_dialog)
{
    gchar *path = get_text("audio_sync_wav_path");

    if(path && path[0] != '\0') {
        if(audio_sync_wav_path_vims_safe(path))
            return 1;

        vj_msg(VEEJAY_MSG_WARNING,
               "WAV sync provider path contains a VIMS command separator/control character; choose another WAV file");
        return 0;
    }

    if(allow_dialog) {
        gchar *filename = dialog_open_file("Select sync provider WAV audio track", FILE_FILTER_WAV);
        if(filename && filename[0] != '\0') {
            if(!audio_sync_wav_path_vims_safe(filename)) {
                vj_msg(VEEJAY_MSG_WARNING,
                       "WAV sync provider path contains a VIMS command separator/control character; choose another WAV file");
                g_free(filename);
                return 0;
            }

            put_text("audio_sync_wav_path", filename);
            g_free(filename);
            return 1;
        }
        if(filename)
            g_free(filename);
    }

    return 0;
}

static void audio_sync_send_target_clock(void)
{
    int bpm_x10 = audio_sync_bpm_x10_from_ui();
    int phase = audio_sync_spin_int(WIDGET_AUDIO_SYNC_PHASE_SPIN, 0);
    int confidence = audio_sync_spin_int(WIDGET_AUDIO_SYNC_CONFIDENCE_SPIN, 100);

    phase = ui_clampi(phase, 0, 100);
    confidence = ui_clampi(confidence, 0, 100);

    multi_vims(VIMS_AUDIO_SYNC_TARGET, "%d %d %d", bpm_x10, phase, confidence);
}

static void audio_sync_send_current_clip_target(void)
{
    multi_vims(VIMS_AUDIO_SYNC_TARGET, "%d %d %d", -1, 0, 100);
}

static void audio_sync_assert_tempo_bridge_active(void);
static int audio_sync_activate_provider_only(int mode, int allow_wav_dialog);
static int audio_sync_clamped_correction_from_ui(void);

static void audio_sync_set_target_controls_guarded(int bpm_x10, int phase, int confidence)
{
    GtkWidget *bpm_w = widget_cache[WIDGET_AUDIO_SYNC_TARGET_BPM_SPIN];
    GtkWidget *phase_w = widget_cache[WIDGET_AUDIO_SYNC_PHASE_SPIN];
    GtkWidget *conf_w = widget_cache[WIDGET_AUDIO_SYNC_CONFIDENCE_SPIN];
    int old_lock = info->status_lock;

    info->status_lock = 1;

    if(bpm_w && GTK_IS_SPIN_BUTTON(bpm_w) && bpm_x10 > 0)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(bpm_w), ((gdouble)bpm_x10) / 10.0);
    if(phase_w && GTK_IS_SPIN_BUTTON(phase_w))
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(phase_w), (gdouble)ui_clampi(phase, 0, 100));
    if(conf_w && GTK_IS_SPIN_BUTTON(conf_w))
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(conf_w), (gdouble)ui_clampi(confidence, 0, 100));

    info->status_lock = old_lock;
}

static int audio_sync_status_target_bpm_x10(void)
{
    int bpm_x10 = info->status_tokens[AUDIO_SYNC_TARGET_BPM_X10];

    return (bpm_x10 > 0) ? bpm_x10 : 0;
}

static int audio_sync_tempo_bend_base_bpm_x10 = 0;

static GtkWidget *audio_sync_named_widget(const char *name)
{
    return name ? glade_xml_get_widget_(info->main_window, name) : NULL;
}

static int audio_sync_named_combo_active(const char *name, int fallback)
{
    GtkWidget *w = audio_sync_named_widget(name);

    if(!w || !GTK_IS_COMBO_BOX(w))
        return fallback;

    return gtk_combo_box_get_active(GTK_COMBO_BOX(w));
}

static int audio_sync_named_spin_int(const char *name, int fallback)
{
    GtkWidget *w = audio_sync_named_widget(name);

    if(!w || !GTK_IS_SPIN_BUTTON(w))
        return fallback;

    return (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));
}

static int audio_sync_profile_slot_from_combo(const char *name, int off_allowed)
{
    int active = audio_sync_named_combo_active(name, off_allowed ? 0 : 0);

    if(off_allowed)
        return ui_clampi(active, 0, 4);

    return ui_clampi(active + 1, 1, 4);
}

void on_audio_sync_wav_profile_store_button_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *path;
    int profile;
    int loop;

    (void)widget;
    (void)user_data;

    profile = audio_sync_profile_slot_from_combo("audio_sync_wav_profile_combo", 0);
    loop = audio_sync_loop_from_ui();
    path = get_text("audio_sync_wav_path");

    if(!path || !audio_sync_wav_path_vims_safe(path)) {
        vj_msg(VEEJAY_MSG_WARNING,
               "Choose a VIMS-safe WAV path before storing a WAV profile");
        return;
    }

    multi_vims(VIMS_AUDIO_SYNC_WAV_PROFILE_SET, "%d %d %s", profile, loop, path);
    vj_msg(VEEJAY_MSG_INFO,
           "Stored WAV sync profile slot %d (%s)",
           profile,
           loop ? "loop" : "one-shot");
}

void on_audio_sync_wav_profile_clear_button_clicked(GtkWidget *widget, gpointer user_data)
{
    int profile;

    (void)widget;
    (void)user_data;

    profile = audio_sync_profile_slot_from_combo("audio_sync_wav_profile_combo", 0);
    multi_vims(VIMS_AUDIO_SYNC_WAV_PROFILE_CLEAR, "%d", profile);
    vj_msg(VEEJAY_MSG_INFO, "Cleared WAV sync profile slot %d", profile);
}
static int sample_audio_sync_source_from_label(const char *label, int fallback)
{
    char *lower;
    int result = fallback;

    if(!label || label[0] == '\0')
        return fallback;

    lower = g_ascii_strdown(label, -1);
    if(!lower)
        return fallback;

    if(strstr(lower, "wav"))
        result = SAMPLE_AUDIO_SYNC_SOURCE_WAV;
    else if(strstr(lower, "jack"))
        result = SAMPLE_AUDIO_SYNC_SOURCE_JACK;
    else if(strstr(lower, "silence"))
        result = SAMPLE_AUDIO_SYNC_SOURCE_SILENCE;
    else if(strstr(lower, "original"))
        result = SAMPLE_AUDIO_SYNC_SOURCE_ORIGINAL;
    else if(strstr(lower, "none") || strstr(lower, "follow"))
        result = SAMPLE_AUDIO_SYNC_UI_SOURCE_NONE;

    g_free(lower);
    return result;
}

static int sample_audio_sync_source_from_active_index(int active)
{
    switch(active) {
        case 1: return SAMPLE_AUDIO_SYNC_SOURCE_ORIGINAL;
        case 2: return SAMPLE_AUDIO_SYNC_SOURCE_WAV;
        case 3: return SAMPLE_AUDIO_SYNC_SOURCE_JACK;
        case 4: return SAMPLE_AUDIO_SYNC_SOURCE_SILENCE;
        case 0:
        default:
            return SAMPLE_AUDIO_SYNC_UI_SOURCE_NONE;
    }
}

static int sample_audio_sync_source_from_ui(void)
{
    GtkWidget *w = audio_sync_named_widget("sample_audio_sync_source_combo");
    int active;

    if(w && GTK_IS_COMBO_BOX_TEXT(w)) {
        gchar *label = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(w));
        if(label) {
            int source = sample_audio_sync_source_from_label(label, SAMPLE_AUDIO_SYNC_UI_SOURCE_NONE);
            g_free(label);
            return source;
        }
    }

    active = audio_sync_named_combo_active("sample_audio_sync_source_combo", 0);
    return sample_audio_sync_source_from_active_index(active);
}


static int sample_audio_sync_mode_from_ui(void)
{
    int active = audio_sync_named_combo_active("sample_audio_sync_mode_combo", 0);

    switch(active) {
        case 1: return SAMPLE_AUDIO_SYNC_LIVE_EXTERNAL;
        case 2: return SAMPLE_AUDIO_SYNC_MONITOR;
        case 3: return SAMPLE_AUDIO_SYNC_MONITOR_TRICKPLAY;
        case 4: return SAMPLE_AUDIO_SYNC_TEMPO_FOLLOW;
        case 5: return SAMPLE_AUDIO_SYNC_TEMPO_BRIDGE;
        case 6: return SAMPLE_AUDIO_SYNC_TRACK_ALIGN;
        case 0:
        default:
            return SAMPLE_AUDIO_SYNC_QUEUE;
    }
}

static const char *sample_audio_sync_mode_name(int mode)
{
    switch(mode) {
        case SAMPLE_AUDIO_SYNC_LIVE_EXTERNAL:       return "Analyze External Tempo";
        case SAMPLE_AUDIO_SYNC_MONITOR:             return "Clean Monitor";
        case SAMPLE_AUDIO_SYNC_MONITOR_TRICKPLAY:   return "Monitor + Trickplay";
        case SAMPLE_AUDIO_SYNC_TEMPO_FOLLOW:        return "Visual Tempo Follow";
        case SAMPLE_AUDIO_SYNC_TEMPO_BRIDGE:        return "Tempo Match Bridge";
        case SAMPLE_AUDIO_SYNC_TRACK_ALIGN:         return "Track Align";
        case SAMPLE_AUDIO_SYNC_QUEUE:
        default:                                    return "Queue/Monitor";
    }
}

static void sample_audio_sync_set_named_sensitive(const char *name, int sensitive)
{
    GtkWidget *w = audio_sync_named_widget(name);

    if(w)
        gtk_widget_set_sensitive(w, sensitive ? TRUE : FALSE);
}
static void sample_audio_sync_update_ui_sensitivity(void)
{
    int source = sample_audio_sync_source_from_ui();
    int profile = audio_sync_profile_slot_from_combo("sample_audio_sync_profile_combo", 1);
    int is_wav = (source == SAMPLE_AUDIO_SYNC_SOURCE_WAV);
    int is_external = (source == SAMPLE_AUDIO_SYNC_SOURCE_WAV || source == SAMPLE_AUDIO_SYNC_SOURCE_JACK);
    int is_silence = (source == SAMPLE_AUDIO_SYNC_SOURCE_SILENCE);
    int can_arm = (source == SAMPLE_AUDIO_SYNC_SOURCE_JACK) || is_silence || (is_wav && profile > 0);

    sample_audio_sync_set_named_sensitive("sample_audio_sync_profile_label", 1);
    sample_audio_sync_set_named_sensitive("sample_audio_sync_profile_combo", 1);
    sample_audio_sync_set_named_sensitive("sample_audio_sync_wav_anchor_label", is_wav);
    sample_audio_sync_set_named_sensitive("sample_audio_sync_wav_anchor_ms", is_wav);
    sample_audio_sync_set_named_sensitive("sample_audio_sync_mode_label", is_external);
    sample_audio_sync_set_named_sensitive("sample_audio_sync_mode_combo", is_external);
    sample_audio_sync_set_named_sensitive("sample_audio_sync_set_here_button", can_arm);
    sample_audio_sync_set_named_sensitive("sample_audio_sync_rearm_button", is_external);
}
static int sample_audio_sync_apply_selection_preserve_anchor(const char *reason)
{
    int sample_id;
    int source;
    int profile = 0;
    int mode;
    int video_anchor;
    int wav_anchor_ms;

    if(!info || info->status_tokens[PLAY_MODE] != MODE_SAMPLE || info->status_tokens[CURRENT_ID] <= 0)
        return 0;

    sample_id = info->status_tokens[CURRENT_ID];
    source = sample_audio_sync_source_from_ui();
    sample_audio_sync_update_ui_sensitivity();

    if(source == SAMPLE_AUDIO_SYNC_UI_SOURCE_NONE) {
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_CLEAR, "%d", sample_id);
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);
        if(reason)
            vj_msg(VEEJAY_MSG_INFO, "Sample %d audio route: follow global/default (%s)", sample_id, reason);
        else
            vj_msg(VEEJAY_MSG_INFO, "Sample %d audio route: follow global/default", sample_id);
        return 1;
    }

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_ORIGINAL) {
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_SET, "%d %d %d %d %d %d",
                   sample_id, source, 0, SAMPLE_AUDIO_SYNC_QUEUE, 0, 0);
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);
        vj_msg(VEEJAY_MSG_INFO, "Sample %d audio route: saved original override", sample_id);
        return 1;
    }

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_SILENCE) {
        mode = SAMPLE_AUDIO_SYNC_QUEUE;
        if(info->status_tokens[SAMPLE_AUDIO_SYNC_MODE] != SAMPLE_AUDIO_SYNC_OFF)
            video_anchor = info->status_tokens[SAMPLE_AUDIO_SYNC_VIDEO_ANCHOR];
        else
            video_anchor = info->status_tokens[FRAME_NUM];
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_SET, "%d %d %d %d %d %d", sample_id, source, 0, mode, video_anchor, 0);
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);
        vj_msg(VEEJAY_MSG_INFO, "Sample %d audio route: saved silence frame=%d", sample_id, video_anchor);
        return 1;
    }

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_WAV) {
        profile = audio_sync_profile_slot_from_combo("sample_audio_sync_profile_combo", 1);
        if(profile <= 0)
            return 0;
    }

    mode = sample_audio_sync_mode_from_ui();

    if(info->status_tokens[SAMPLE_AUDIO_SYNC_MODE] != SAMPLE_AUDIO_SYNC_OFF)
        video_anchor = info->status_tokens[SAMPLE_AUDIO_SYNC_VIDEO_ANCHOR];
    else
        video_anchor = info->status_tokens[FRAME_NUM];

    wav_anchor_ms = audio_sync_named_spin_int("sample_audio_sync_wav_anchor_ms", 0);
    if(wav_anchor_ms < 0)
        wav_anchor_ms = 0;

    multi_vims(VIMS_SAMPLE_AUDIO_SYNC_SET,
               "%d %d %d %d %d %d",
               sample_id,
               source,
               profile,
               mode,
               video_anchor,
               wav_anchor_ms);
    multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);

    vj_msg(VEEJAY_MSG_INFO,
           "Sample %d audio route: saved %s profile=%d frame=%d wav=%dms (%s%s%s)",
           sample_id,
           source == SAMPLE_AUDIO_SYNC_SOURCE_WAV ? "WAV" : (source == SAMPLE_AUDIO_SYNC_SOURCE_JACK ? "JACK" : "Silence"),
           profile,
           video_anchor,
           wav_anchor_ms,
           sample_audio_sync_mode_name(mode),
           reason ? ", " : "",
           reason ? reason : "");

    return 1;
}


void on_sample_audio_sync_source_combo_changed(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;

    if(info && info->status_lock)
        return;

    sample_audio_sync_apply_selection_preserve_anchor("source change");
}

void on_sample_audio_sync_profile_combo_changed(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;

    if(info && info->status_lock)
        return;

    sample_audio_sync_update_ui_sensitivity();

    if(sample_audio_sync_source_from_ui() != SAMPLE_AUDIO_SYNC_SOURCE_WAV)
        return;

    sample_audio_sync_apply_selection_preserve_anchor("profile change");
}

void on_sample_audio_sync_mode_combo_changed(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;

    if(info && info->status_lock)
        return;

    sample_audio_sync_apply_selection_preserve_anchor("mode change");
}

void on_sample_audio_sync_set_here_button_clicked(GtkWidget *widget, gpointer user_data)
{
    int sample_id;
    int source;
    int profile;
    int mode;
    int video_anchor;
    int wav_anchor_ms;

    (void)widget;
    (void)user_data;

    if(info->status_tokens[PLAY_MODE] != MODE_SAMPLE || info->status_tokens[CURRENT_ID] <= 0) {
        vj_msg(VEEJAY_MSG_WARNING,
               "Select/play a sample before binding an audio sync source");
        return;
    }

    sample_id = info->status_tokens[CURRENT_ID];
    source = sample_audio_sync_source_from_ui();
    sample_audio_sync_update_ui_sensitivity();

    if(source == SAMPLE_AUDIO_SYNC_UI_SOURCE_NONE) {
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_CLEAR, "%d", sample_id);
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);
        vj_msg(VEEJAY_MSG_INFO, "Sample %d audio route set to follow global/default", sample_id);
        return;
    }

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_ORIGINAL) {
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_SET, "%d %d %d %d %d %d",
                   sample_id, source, 0, SAMPLE_AUDIO_SYNC_QUEUE, 0, 0);
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);
        vj_msg(VEEJAY_MSG_INFO, "Sample %d audio route set to saved original override", sample_id);
        return;
    }

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_SILENCE) {
        video_anchor = info->status_tokens[FRAME_NUM];
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_SET, "%d %d %d %d %d %d", sample_id, source, 0, SAMPLE_AUDIO_SYNC_QUEUE, video_anchor, 0);
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);
        vj_msg(VEEJAY_MSG_INFO, "Sample %d audio source set to silence from frame %d", sample_id, video_anchor);
        return;
    }

    profile = 0;
    if(source == SAMPLE_AUDIO_SYNC_SOURCE_WAV) {
        profile = audio_sync_profile_slot_from_combo("sample_audio_sync_profile_combo", 1);
        if(profile <= 0) {
            multi_vims(VIMS_SAMPLE_AUDIO_SYNC_CLEAR, "%d", sample_id);
            vj_msg(VEEJAY_MSG_INFO, "Sample %d WAV sync disabled", sample_id);
            return;
        }
    }

    mode = sample_audio_sync_mode_from_ui();
    video_anchor = info->status_tokens[FRAME_NUM];
    wav_anchor_ms = audio_sync_named_spin_int("sample_audio_sync_wav_anchor_ms", 0);
    if(wav_anchor_ms < 0)
        wav_anchor_ms = 0;

    multi_vims(VIMS_SAMPLE_AUDIO_SYNC_SET,
               "%d %d %d %d %d %d",
               sample_id,
               source,
               profile,
               mode,
               video_anchor,
               wav_anchor_ms);

    vj_msg(VEEJAY_MSG_INFO,
           "Sample %d audio sync source=%d profile=%d frame=%d -> %dms (%s)",
           sample_id,
           source,
           profile,
           video_anchor,
           wav_anchor_ms,
           sample_audio_sync_mode_name(mode));
}

void on_sample_audio_sync_rearm_button_clicked(GtkWidget *widget, gpointer user_data)
{
    int sample_id;
    int source;
    int profile;
    int mode;
    int video_anchor;
    int wav_anchor_ms;

    (void)widget;
    (void)user_data;

    sample_id = (info->status_tokens[PLAY_MODE] == MODE_SAMPLE) ? info->status_tokens[CURRENT_ID] : 0;
    if(sample_id <= 0)
        return;

    source = sample_audio_sync_source_from_ui();
    sample_audio_sync_update_ui_sensitivity();
    if(source == SAMPLE_AUDIO_SYNC_UI_SOURCE_NONE) {
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_CLEAR, "%d", sample_id);
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);
        vj_msg(VEEJAY_MSG_INFO, "Sample %d audio route follows global/default", sample_id);
        return;
    }

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_ORIGINAL) {
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_SET, "%d %d %d %d %d %d",
                   sample_id, source, 0, SAMPLE_AUDIO_SYNC_QUEUE, 0, 0);
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);
        vj_msg(VEEJAY_MSG_INFO, "Sample %d audio route rearmed as saved original override", sample_id);
        return;
    }

    if(source == SAMPLE_AUDIO_SYNC_SOURCE_SILENCE) {
        if(info->status_tokens[SAMPLE_AUDIO_SYNC_SOURCE] == SAMPLE_AUDIO_SYNC_SOURCE_SILENCE)
            video_anchor = info->status_tokens[SAMPLE_AUDIO_SYNC_VIDEO_ANCHOR];
        else
            video_anchor = info->status_tokens[FRAME_NUM];
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_SET, "%d %d %d %d %d %d", sample_id, source, 0, SAMPLE_AUDIO_SYNC_QUEUE, video_anchor, 0);
        multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);
        vj_msg(VEEJAY_MSG_INFO, "Sample %d audio sync source set to silence", sample_id);
        return;
    }

    profile = 0;
    if(source == SAMPLE_AUDIO_SYNC_SOURCE_WAV) {
        profile = audio_sync_profile_slot_from_combo("sample_audio_sync_profile_combo", 1);
        if(profile <= 0) {
            multi_vims(VIMS_SAMPLE_AUDIO_SYNC_CLEAR, "%d", sample_id);
            multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);
            vj_msg(VEEJAY_MSG_INFO, "Sample %d WAV sync disabled", sample_id);
            return;
        }
    }

    mode = sample_audio_sync_mode_from_ui();
    if(info->status_tokens[SAMPLE_AUDIO_SYNC_SOURCE] == SAMPLE_AUDIO_SYNC_SOURCE_ORIGINAL)
        video_anchor = info->status_tokens[FRAME_NUM];
    else
        video_anchor = info->status_tokens[SAMPLE_AUDIO_SYNC_VIDEO_ANCHOR];

    wav_anchor_ms = audio_sync_named_spin_int("sample_audio_sync_wav_anchor_ms", 0);
    if(wav_anchor_ms < 0)
        wav_anchor_ms = 0;

    multi_vims(VIMS_SAMPLE_AUDIO_SYNC_SET,
               "%d %d %d %d %d %d",
               sample_id,
               source,
               profile,
               mode,
               video_anchor,
               wav_anchor_ms);

    multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);

    vj_msg(VEEJAY_MSG_INFO,
           "Sample %d audio sync rearm source=%d profile=%d frame=%d -> %dms (%s)",
           sample_id,
           source,
           profile,
           video_anchor,
           wav_anchor_ms,
           sample_audio_sync_mode_name(mode));
}

void on_sample_audio_sync_clear_button_clicked(GtkWidget *widget, gpointer user_data)
{
    int sample_id;

    (void)widget;
    (void)user_data;

    sample_id = (info->status_tokens[PLAY_MODE] == MODE_SAMPLE) ? info->status_tokens[CURRENT_ID] : 0;
    multi_vims(VIMS_SAMPLE_AUDIO_SYNC_CLEAR, "%d", sample_id);
    {
        GtkWidget *combo = audio_sync_named_widget("sample_audio_sync_source_combo");
        int old_lock = info->status_lock;
        info->status_lock = 1;
        if(combo && GTK_IS_COMBO_BOX(combo))
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
        info->status_lock = old_lock;
    }
    sample_audio_sync_update_ui_sensitivity();
}


static double audio_sync_tempo_bend_pct_from_ui(void)
{
    GtkWidget *w = audio_sync_named_widget("audio_sync_tempo_bend_scale");

    if(!w || !GTK_IS_RANGE(w))
        return 0.0;

    return ui_clampd(gtk_range_get_value(GTK_RANGE(w)), -12.0, 12.0);
}

static void audio_sync_set_tempo_bend_guarded(double bend_pct)
{
    GtkWidget *w = audio_sync_named_widget("audio_sync_tempo_bend_scale");
    int old_lock;

    if(!w || !GTK_IS_RANGE(w))
        return;

    bend_pct = ui_clampd(bend_pct, -12.0, 12.0);

    old_lock = info->status_lock;
    info->status_lock = 1;
    gtk_range_set_value(GTK_RANGE(w), bend_pct);
    info->status_lock = old_lock;
}

static int audio_sync_bend_base_bpm_x10(void)
{
    int base_x10 = audio_sync_tempo_bend_base_bpm_x10;

    if(base_x10 <= 0)
        base_x10 = audio_sync_status_target_bpm_x10();
    if(base_x10 <= 0)
        base_x10 = audio_sync_bpm_x10_from_ui();

    base_x10 = ui_clampi(base_x10, 400, 2400);
    audio_sync_tempo_bend_base_bpm_x10 = base_x10;
    return base_x10;
}

static int audio_sync_bend_target_bpm_x10(double bend_pct)
{
    int base_x10 = audio_sync_bend_base_bpm_x10();
    double target = ((double)base_x10) * ((100.0 + bend_pct) / 100.0);

    target = ui_clampd(target, 400.0, 2400.0);

    return (int)(target + 0.5);
}

static void audio_sync_send_manual_target_x10(int bpm_x10)
{
    int phase = audio_sync_spin_int(WIDGET_AUDIO_SYNC_PHASE_SPIN, 0);
    int confidence = audio_sync_spin_int(WIDGET_AUDIO_SYNC_CONFIDENCE_SPIN, 100);

    bpm_x10 = ui_clampi(bpm_x10, 400, 2400);
    phase = ui_clampi(phase, 0, 100);
    confidence = ui_clampi(confidence, 0, 100);

    multi_vims(VIMS_AUDIO_SYNC_TARGET, "%d %d %d", bpm_x10, phase, confidence);
}

static void audio_sync_activate_tempo_target_mode(int mode)
{
    if(mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
        audio_sync_assert_tempo_bridge_active();
    else if(mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        (void) audio_sync_activate_provider_only(mode, 0);
}


static void audio_sync_send_mode_control(int mode)
{
    mode = ui_clampi(mode, 0, VJ_AUDIO_SYNC_MODE_MAX);

    multi_vims(VIMS_AUDIO_SYNC_MODE, "%d", mode);
}

static int audio_sync_send_selected_source(void)
{
    int mode = audio_sync_mode_from_ui();

    if(!audio_global_source_controls_allowed()) {
        vj_msg(VEEJAY_MSG_INFO, "Current sample has its own audio source; clear it to use the global audio provider");
        return 0;
    }

    if(mode == 0) {
        audio_sync_send_mode_control(0);
        return 1;
    }

    audio_sync_send_mode_control(mode);

    if(audio_sync_mode_supports_wav(mode) && audio_input_selector_wav_from_ui()) {
        gchar *path;

        if(!audio_sync_wav_path_ready(0)) {
            vj_msg(VEEJAY_MSG_WARNING,
                   "No WAV sync provider file selected; choose a WAV file first");
            return 0;
        }

        path = get_text("audio_sync_wav_path");
        if(!path || path[0] == '\0')
            return 0;



        multi_vims(VIMS_AUDIO_SYNC_WAV, "%d %d %s",
                   mode, audio_sync_loop_from_ui(), path);
        return 1;
    }

    multi_vims(VIMS_AUDIO_SYNC_JACK, "%d %d", mode, audio_sync_channels_from_ui());
    return 1;
}

static void audio_sync_assert_tempo_bridge_active(void)
{
    if(audio_sync_mode_from_ui() != VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
        return;

    if(!audio_global_source_controls_allowed())
        return;

    if(audio_input_selector_active_from_ui() != AUDIO_MASTER_JACK &&
       audio_input_selector_active_from_ui() != AUDIO_MASTER_WAV)
        return;

    multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", VJ_RECORD_AUDIO_SOURCE_BEAT_JACK);
    if(audio_sync_send_selected_source())
        multi_vims(VIMS_AUDIO_SYNC_STATUS, "%d", 1);
}

static int audio_sync_clamped_correction_from_ui(void)
{
    int correction = audio_sync_spin_int(WIDGET_AUDIO_SYNC_CORRECTION_SPIN, 4);

    return ui_clampi(correction, 0, 25);
}

static void audio_sync_set_correction_guarded(int correction)
{
    GtkWidget *w = widget_cache[WIDGET_AUDIO_SYNC_CORRECTION_SPIN];
    int old_lock;

    if(!w || !GTK_IS_SPIN_BUTTON(w))
        return;

    correction = ui_clampi(correction, 0, 25);

    old_lock = info->status_lock;
    info->status_lock = 1;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), (gdouble)correction);
    info->status_lock = old_lock;
}

static int audio_sync_correction_for_bend(double bend_pct)
{
    int correction = audio_sync_clamped_correction_from_ui();
    int need = (int)(fabs(bend_pct) + 0.999);

    if(need > 25)
        need = 25;
    if(correction < need) {
        correction = need;
        audio_sync_set_correction_guarded(correction);
    }

    return correction;
}

static void audio_sync_tempo_bridge_enable_without_target_reset(void)
{
    if(audio_sync_mode_from_ui() != VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
        return;

    if(!audio_global_source_controls_allowed())
        return;

    if(audio_input_selector_active_from_ui() != AUDIO_MASTER_JACK &&
       audio_input_selector_active_from_ui() != AUDIO_MASTER_WAV)
        return;

    multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", VJ_RECORD_AUDIO_SOURCE_BEAT_JACK);
    audio_sync_send_mode_control(VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE);
    multi_vims(VIMS_AUDIO_SYNC_STATUS, "%d", 1);
}

static void audio_sync_send_mode_settings_if_needed(int mode)
{
    if(mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE ||
       mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        audio_sync_send_current_clip_target();
    else
        return;

    multi_vims(VIMS_AUDIO_SYNC_CORRECTION, "%d", audio_sync_clamped_correction_from_ui());
}


#define audio_sync_send_bridge_settings_if_needed audio_sync_send_mode_settings_if_needed

static void audio_sync_deactivate_playback(void)
{
    audio_sync_send_mode_control(0);
    multi_vims(VIMS_AUDIO_SYNC_STATUS, "%d", 0);
}

static int audio_sync_activate_provider_only(int mode, int allow_wav_dialog)
{
    int fallback = audio_sync_non_jack_master_from_status();
    int active = audio_input_selector_active_from_ui();
    int use_wav;

    if(!audio_global_source_controls_allowed()) {
        vj_msg(VEEJAY_MSG_INFO, "Current sample has its own audio source; clear it to use the global audio provider");
        return 0;
    }

    if(mode != VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL &&
       mode != VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        return 0;

    if(active != AUDIO_MASTER_JACK && active != AUDIO_MASTER_WAV) {
        vj_msg(VEEJAY_MSG_WARNING,
               "Select JACK external or WAV file before enabling %s",
               audio_sync_mode_name(mode));
        return 0;
    }

    use_wav = (active == AUDIO_MASTER_WAV);
    if(use_wav && !audio_sync_wav_path_ready(allow_wav_dialog)) {
        audio_input_selector_set_guarded(AUDIO_MASTER_JACK);
        audio_sync_set_master_wav_options_visible(0);
        use_wav = 0;
    }

    audio_sync_set_master_wav_options_visible(use_wav);
    multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", fallback);
    audio_sync_send_mode_control(mode);

    if(use_wav) {
        gchar *path = get_text("audio_sync_wav_path");
        if(!path || path[0] == '\0')
            return 0;
        multi_vims(VIMS_AUDIO_SYNC_WAV, "%d %d %s",
                   mode, audio_sync_loop_from_ui(), path);
    } else {
        multi_vims(VIMS_AUDIO_SYNC_JACK, "%d %d", mode, audio_sync_channels_from_ui());
    }

    audio_sync_send_mode_settings_if_needed(mode);
    multi_vims(VIMS_AUDIO_SYNC_STATUS, "%d", 1);
    return 1;
}

static int audio_input_selector_use_external_playback(int use_wav, int allow_wav_dialog)
{
    int mode = audio_sync_mode_from_ui();

    if(!audio_global_source_controls_allowed()) {
        vj_msg(VEEJAY_MSG_INFO, "Current sample has its own audio source; clear it to use the global audio provider");
        return 0;
    }

    if(mode == 0) {
        mode = VJ_AUDIO_SYNC_MODE_MONITOR;
        audio_sync_set_mode_combo_guarded(mode);
    }

    if(audio_sync_mode_is_provider_only(mode))
        return audio_sync_activate_provider_only(mode, allow_wav_dialog);

    if(!audio_sync_mode_supports_wav(mode))
        use_wav = 0;

    audio_sync_set_external_source_guarded(use_wav);

    if(use_wav && !audio_sync_wav_path_ready(allow_wav_dialog))
        return 0;

    if(!audio_sync_send_selected_source())
        return 0;

    multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", VJ_RECORD_AUDIO_SOURCE_BEAT_JACK);

    audio_sync_send_bridge_settings_if_needed(mode);
    multi_vims(VIMS_AUDIO_SYNC_STATUS, "%d", 1);
    return 1;
}

static void audio_input_selector_use_jack_playback(void)
{
    (void) audio_input_selector_use_external_playback(0, 0);
}

static void audio_input_selector_use_wav_playback(int allow_dialog)
{
    (void) audio_input_selector_use_external_playback(1, allow_dialog);
}

void on_audio_input_selector_combo_changed(GtkWidget *widget, gpointer user_data)
{
    int active;
    int record_source;

    if(info->status_lock)
        return;

    if(!widget || !GTK_IS_COMBO_BOX(widget))
        return;

    if(!audio_global_source_controls_allowed()) {
        vj_msg(VEEJAY_MSG_INFO, "Current sample has its own audio source; clear Sample audio source to use the global provider");
        audio_mixer_update_crossfade_sensitivity();
        return;
    }

    active = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
    if(active < 0)
        return;

    record_source = audio_input_selector_record_source_from_active(active);

    switch(active)
    {
        case AUDIO_MASTER_ORIGINAL:
        case AUDIO_MASTER_SILENCE:
            audio_mixer_reset_to_follow_route();
            audio_sync_remember_non_jack_master(record_source);
            audio_sync_set_mode_combo_guarded(0);
            audio_sync_set_enable_toggle_guarded(0);
            multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", record_source);
            audio_sync_deactivate_playback();
            audio_sync_set_master_wav_options_visible(0);
            audio_beat_enforce_action_for_master(active, 1);
            vj_msg(VEEJAY_MSG_INFO, "Audio source / sync provider: %s",
                   audio_input_selector_name_from_active(active));
            break;

        case AUDIO_MASTER_JACK:
            audio_mixer_update_crossfade_sensitivity();
            if(audio_sync_mode_from_ui() == 0)
                audio_sync_set_mode_combo_guarded(VJ_AUDIO_SYNC_MODE_MONITOR);
            audio_input_selector_use_jack_playback();
            vj_msg(VEEJAY_MSG_INFO, "Audio source / sync provider: JACK input, mode: %s",
                   audio_sync_mode_name(audio_sync_mode_from_ui()));
            break;

        case AUDIO_MASTER_WAV:
            audio_mixer_update_crossfade_sensitivity();
            if(audio_sync_mode_from_ui() == 0)
                audio_sync_set_mode_combo_guarded(VJ_AUDIO_SYNC_MODE_MONITOR);
            if(audio_input_selector_use_external_playback(1, 1)) {
                vj_msg(VEEJAY_MSG_INFO, "Audio source / sync provider: WAV file, mode: %s%s",
                       audio_sync_mode_name(audio_sync_mode_from_ui()),
                       audio_sync_loop_from_ui() ? " (loop)" : "");
            } else {
                int fallback = audio_sync_non_jack_master_from_status();
                audio_input_selector_set_guarded(audio_input_selector_active_from_record_source(fallback));
                multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", fallback);
                audio_sync_deactivate_playback();
                vj_msg(VEEJAY_MSG_WARNING,
                       "No WAV sync provider file selected; audio source restored to %s",
                       audio_sync_master_track_name(fallback));
            }
            break;

        default:
            break;
    }
}

void on_audio_sync_enable_toggle_toggled(GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    if(!widget || !GTK_IS_TOGGLE_BUTTON(widget))
        return;

    int enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? 1 : 0;

    if(!audio_global_source_controls_allowed()) {
        int old_lock = info->status_lock;
        info->status_lock = 1;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
        info->status_lock = old_lock;
        vj_msg(VEEJAY_MSG_INFO,
               "Global audio sync enable ignored; current sample owns its audio route");
        return;
    }

    if(enabled) {
        int active = audio_input_selector_active_from_ui();
        int use_wav = (active == AUDIO_MASTER_WAV);

        if(active != AUDIO_MASTER_JACK && active != AUDIO_MASTER_WAV) {
            int old_lock = info->status_lock;
            info->status_lock = 1;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
            info->status_lock = old_lock;
            vj_msg(VEEJAY_MSG_WARNING,
                   "Select JACK external or WAV file before enabling external audio sync");
            return;
        }

        if(audio_sync_mode_from_ui() == 0)
            audio_sync_set_mode_combo_guarded(VJ_AUDIO_SYNC_MODE_MONITOR);

        if(audio_sync_mode_is_provider_only(audio_sync_mode_from_ui())) {
            int mode = audio_sync_mode_from_ui();
            if(audio_sync_activate_provider_only(mode, use_wav)) {
                vj_msg(VEEJAY_MSG_INFO, "%s enabled (%s, video/audio output untouched)",
                       audio_sync_mode_name(mode),
                       audio_input_selector_wav_from_ui() ? "WAV" : "JACK");
            }
            return;
        }

        if(!audio_sync_mode_supports_wav(audio_sync_mode_from_ui())) {
            active = AUDIO_MASTER_JACK;
            use_wav = 0;
        }

        audio_input_selector_set_guarded(active);
        if(audio_input_selector_use_external_playback(use_wav, use_wav)) {
            vj_msg(VEEJAY_MSG_INFO, "%s audio sync enabled (%s)",
                   use_wav ? "WAV" : "JACK",
                   audio_sync_mode_name(audio_sync_mode_from_ui()));
        } else if(use_wav) {
            int fallback = audio_sync_non_jack_master_from_status();
            int old_lock;

            audio_sync_set_mode_combo_guarded(0);
            audio_input_selector_set_guarded(audio_input_selector_active_from_record_source(fallback));
            multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", fallback);
            audio_sync_deactivate_playback();
            audio_sync_set_master_wav_options_visible(0);

            old_lock = info->status_lock;
            info->status_lock = 1;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
            info->status_lock = old_lock;

            vj_msg(VEEJAY_MSG_WARNING,
                   "WAV audio sync was not enabled; audio source restored to %s",
                   audio_sync_master_track_name(fallback));
        }
    } else {
        int fallback = audio_sync_non_jack_master_from_status();
        audio_sync_set_mode_combo_guarded(0);
        audio_input_selector_set_guarded(audio_input_selector_active_from_record_source(fallback));
        multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", fallback);
        audio_sync_deactivate_playback();
        audio_sync_set_master_wav_options_visible(0);
        vj_msg(VEEJAY_MSG_INFO, "Audio sync disabled, audio source: %s",
               audio_sync_master_track_name(fallback));
    }
}

void on_audio_sync_mode_combo_changed(GtkWidget *widget, gpointer user_data)
{
    int mode;

    if(info->status_lock)
        return;

    if(!audio_global_source_controls_allowed()) {
        vj_msg(VEEJAY_MSG_INFO,
               "Global audio mode change ignored; current sample owns its audio route");
        return;
    }

    mode = audio_sync_mode_from_ui();

    if(mode == 0) {
        int fallback = audio_sync_non_jack_master_from_status();
        audio_sync_set_enable_toggle_guarded(0);
        audio_input_selector_set_guarded(audio_input_selector_active_from_record_source(fallback));
        multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", fallback);
        audio_sync_deactivate_playback();
        vj_msg(VEEJAY_MSG_INFO, "Audio sync mode: None, audio source: %s",
               audio_sync_master_track_name(fallback));
        return;
    }

    if(audio_input_selector_active_from_ui() != AUDIO_MASTER_JACK &&
       audio_input_selector_active_from_ui() != AUDIO_MASTER_WAV)
    {
        audio_sync_set_mode_combo_guarded(0);
        audio_sync_deactivate_playback();
        vj_msg(VEEJAY_MSG_WARNING,
               "Select JACK external or WAV file before choosing an external audio sync mode");
        return;
    }

    if(audio_sync_mode_is_provider_only(mode)) {
        if(audio_sync_activate_provider_only(mode, 0))
            vj_msg(VEEJAY_MSG_INFO, "Audio sync mode: %s (%s provider, no external audio playback)",
                   audio_sync_mode_name(mode),
                   audio_input_selector_wav_from_ui() ? "WAV" : "JACK");
        return;
    }

    if(!audio_sync_mode_supports_wav(mode) ||
       audio_input_selector_active_from_ui() != AUDIO_MASTER_WAV)
        audio_input_selector_set_guarded(AUDIO_MASTER_JACK);

    multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", VJ_RECORD_AUDIO_SOURCE_BEAT_JACK);
    if(audio_sync_send_selected_source()) {
        audio_sync_send_bridge_settings_if_needed(mode);
        multi_vims(VIMS_AUDIO_SYNC_STATUS, "%d", 1);
        vj_msg(VEEJAY_MSG_INFO, "%s mode: %s",
               audio_input_selector_wav_from_ui() ? "WAV" : "JACK",
               audio_sync_mode_name(mode));
    }
}

void on_audio_sync_source_combo_changed(GtkWidget *widget, gpointer user_data)
{
    int mode;
    int active = 0;

    if(info->status_lock)
        return;

    mode = audio_sync_mode_from_ui();
    if(mode == 0)
        return;

    if(widget && GTK_IS_COMBO_BOX(widget))
        active = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

    if(!audio_sync_mode_supports_wav(mode)) {
        audio_sync_set_external_source_guarded(0);
        vj_msg(VEEJAY_MSG_INFO, "%s uses JACK input as external sync provider", audio_sync_mode_name(mode));
    }
    else if(active == VJ_AUDIO_SYNC_SOURCE_WAV_FILE)
        audio_sync_set_external_source_guarded(1);
    else if(active == VJ_AUDIO_SYNC_SOURCE_JACK)
        audio_sync_set_external_source_guarded(0);

    if(audio_sync_mode_is_provider_only(mode)) {
        if(audio_sync_activate_provider_only(mode, active == VJ_AUDIO_SYNC_SOURCE_WAV_FILE))
            vj_msg(VEEJAY_MSG_INFO, "External sync provider: %s",
                   audio_input_selector_wav_from_ui() ? "WAV file" : "JACK input");
        return;
    }

    if(audio_sync_send_selected_source()) {
        audio_sync_send_bridge_settings_if_needed(mode);
        multi_vims(VIMS_AUDIO_SYNC_STATUS, "%d", 1);
        vj_msg(VEEJAY_MSG_INFO, "External sync provider: %s",
               audio_input_selector_wav_from_ui() ? "WAV file" : "JACK input");
    }
}

void on_audio_sync_channels_spin_value_changed(GtkWidget *widget, gpointer user_data)
{
    int mode;

    if(info->status_lock)
        return;

    mode = audio_sync_mode_from_ui();

    if(mode == 0)
        return;

    if(!audio_input_selector_wav_from_ui())
    {
        int ch = audio_sync_channels_from_ui();
        audio_sync_send_mode_control(mode);
        multi_vims(VIMS_AUDIO_SYNC_JACK, "%d %d", mode, ch);
        audio_sync_send_bridge_settings_if_needed(mode);
        multi_vims(VIMS_AUDIO_SYNC_STATUS, "%d", 1);
        vj_msg(VEEJAY_MSG_INFO, "JACK audio sync channels: %d", ch);
    }
}

static int audio_sync_mode_uses_manual_target_controls(int mode)
{
    return mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE;
}

static void audio_sync_apply_manual_target_for_mode(int mode)
{
    if(mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE) {
        audio_sync_tempo_bridge_enable_without_target_reset();
        audio_sync_send_target_clock();
        multi_vims(VIMS_AUDIO_SYNC_CORRECTION, "%d", audio_sync_clamped_correction_from_ui());
    }
}

void on_audio_sync_use_clip_bpm_button_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return;

    int mode = audio_sync_mode_from_ui();
    if(!audio_sync_mode_uses_manual_target_controls(mode))
        return;

    audio_sync_tempo_bend_base_bpm_x10 = 0;
    audio_sync_set_tempo_bend_guarded(0.0);
    if(mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
        audio_sync_tempo_bridge_enable_without_target_reset();
    else
        audio_sync_activate_tempo_target_mode(mode);
    audio_sync_send_current_clip_target();
    multi_vims(VIMS_AUDIO_SYNC_CORRECTION, "%d", audio_sync_clamped_correction_from_ui());

    {
        int bpm_x10 = audio_sync_status_target_bpm_x10();
        if(bpm_x10 > 0)
            audio_sync_set_target_controls_guarded(bpm_x10, 0, 100);
    }

    vj_msg(VEEJAY_MSG_INFO, "%s using current clip BPM target%s",
           "Tempo Match Bridge",
           audio_sync_status_target_bpm_x10() > 0 ? "" : "; waiting for clip BPM detection");
}

void on_audio_sync_latch_clip_bpm_button_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return;

    int mode = audio_sync_mode_from_ui();
    if(!audio_sync_mode_uses_manual_target_controls(mode))
        return;

    int bpm_x10 = audio_sync_status_target_bpm_x10();
    if(bpm_x10 <= 0) {
        vj_msg(VEEJAY_MSG_WARNING, "No detected clip BPM available to latch yet");
        return;
    }

    audio_sync_tempo_bend_base_bpm_x10 = bpm_x10;
    audio_sync_set_tempo_bend_guarded(0.0);
    audio_sync_set_target_controls_guarded(bpm_x10, 0, 100);
    if(mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
        audio_sync_tempo_bridge_enable_without_target_reset();
    else
        audio_sync_activate_tempo_target_mode(mode);
    audio_sync_send_target_clock();
    multi_vims(VIMS_AUDIO_SYNC_CORRECTION, "%d", audio_sync_clamped_correction_from_ui());

    vj_msg(VEEJAY_MSG_INFO, "%s target latched at %d.%d BPM",
           "Tempo Match Bridge",
           bpm_x10 / 10, bpm_x10 % 10);
}

void on_audio_sync_tempo_bend_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return;

    int mode = audio_sync_mode_from_ui();
    if(mode != VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
        return;

    double bend_pct = audio_sync_tempo_bend_pct_from_ui();

    if(bend_pct > -0.05 && bend_pct < 0.05) {
        audio_sync_tempo_bend_base_bpm_x10 = 0;
        audio_sync_tempo_bridge_enable_without_target_reset();
        audio_sync_send_current_clip_target();
        multi_vims(VIMS_AUDIO_SYNC_CORRECTION, "%d", audio_sync_clamped_correction_from_ui());
        vj_msg(VEEJAY_MSG_INFO, "Tempo Match Bridge pitch bend reset to current clip BPM target");
        return;
    }

    int target_x10 = audio_sync_bend_target_bpm_x10(bend_pct);
    int correction = audio_sync_correction_for_bend(bend_pct);

    audio_sync_set_target_controls_guarded(target_x10, 0, 100);
    audio_sync_tempo_bridge_enable_without_target_reset();
    audio_sync_send_manual_target_x10(target_x10);
    multi_vims(VIMS_AUDIO_SYNC_CORRECTION, "%d", correction);

    vj_msg(VEEJAY_MSG_INFO,
           "Tempo Match Bridge pitch bend: %+0.1f%% -> manual target %d.%d BPM, correction cap %d%%",
           bend_pct, target_x10 / 10, target_x10 % 10, correction);
}

void on_audio_sync_tempo_bend_reset_button_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return;

    int mode = audio_sync_mode_from_ui();
    if(mode != VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
        return;

    audio_sync_tempo_bend_base_bpm_x10 = 0;
    audio_sync_set_tempo_bend_guarded(0.0);
    audio_sync_tempo_bridge_enable_without_target_reset();
    audio_sync_send_current_clip_target();
    multi_vims(VIMS_AUDIO_SYNC_CORRECTION, "%d", audio_sync_clamped_correction_from_ui());

    vj_msg(VEEJAY_MSG_INFO, "Tempo Match Bridge target reset to current clip BPM");
}

void on_audio_sync_target_bpm_value_changed(GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    int mode = audio_sync_mode_from_ui();
    if(audio_sync_mode_uses_manual_target_controls(mode)) {
        int bpm_x10 = audio_sync_bpm_x10_from_ui();
        audio_sync_tempo_bend_base_bpm_x10 = 0;
        audio_sync_set_tempo_bend_guarded(0.0);
        audio_sync_apply_manual_target_for_mode(mode);
        vj_msg(VEEJAY_MSG_INFO, "%s target BPM override: %d.%d",
               "Tempo Match Bridge",
               bpm_x10 / 10, bpm_x10 % 10);
    }
}

void on_audio_sync_phase_value_changed(GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    int mode = audio_sync_mode_from_ui();
    if(audio_sync_mode_uses_manual_target_controls(mode)) {
        int phase = audio_sync_spin_int(WIDGET_AUDIO_SYNC_PHASE_SPIN, 0);
        audio_sync_apply_manual_target_for_mode(mode);
        vj_msg(VEEJAY_MSG_INFO, "%s target phase override: %d%%",
               "Tempo Match Bridge",
               phase);
    }
}

void on_audio_sync_confidence_value_changed(GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    int mode = audio_sync_mode_from_ui();
    if(audio_sync_mode_uses_manual_target_controls(mode)) {
        int confidence = audio_sync_spin_int(WIDGET_AUDIO_SYNC_CONFIDENCE_SPIN, 100);
        audio_sync_apply_manual_target_for_mode(mode);
        vj_msg(VEEJAY_MSG_INFO, "%s target confidence override: %d%%",
               "Tempo Match Bridge",
               confidence);
    }
}

void on_audio_sync_correction_value_changed(GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    {
        int mode = audio_sync_mode_from_ui();
        if(mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE ||
           mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        {
            int correction = audio_sync_clamped_correction_from_ui();
            if(mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
                audio_sync_assert_tempo_bridge_active();
            else
                (void) audio_sync_activate_provider_only(mode, 0);
            multi_vims(VIMS_AUDIO_SYNC_CORRECTION, "%d", correction);
            vj_msg(VEEJAY_MSG_INFO, "%s max correction: %d%%",
                   mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW ? "Visual Tempo Follow" : "Tempo match",
                   correction);
        }
    }
}

void on_audio_sync_jack_button_clicked(GtkWidget *widget, gpointer user_data)
{
    int mode;

    if(info->status_lock)
        return;

    if(widget && GTK_IS_TOGGLE_BUTTON(widget) &&
       !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
        return;

    if(!audio_global_source_controls_allowed()) {
        vj_msg(VEEJAY_MSG_INFO,
               "Global JACK provider ignored; current sample owns its audio route");
        return;
    }

    mode = audio_sync_mode_from_ui();
    if(mode == 0)
        return;

    if(audio_sync_mode_is_provider_only(mode)) {
        audio_input_selector_set_guarded(AUDIO_MASTER_JACK);
        audio_sync_activate_provider_only(mode, 0);
        vj_msg(VEEJAY_MSG_INFO, "External sync provider: JACK input");
        return;
    }

    audio_input_selector_use_jack_playback();
    vj_msg(VEEJAY_MSG_INFO, "External sync provider: JACK input");
}

void on_audio_sync_wav_button_clicked(GtkWidget *widget, gpointer user_data)
{
    int mode;

    if(info->status_lock)
        return;

    if(widget && GTK_IS_TOGGLE_BUTTON(widget) &&
       !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
        return;

    if(!audio_global_source_controls_allowed()) {
        audio_sync_set_master_wav_options_visible(1);
        vj_msg(VEEJAY_MSG_INFO,
               "Current sample has its own audio route; WAV provider button only exposes profile controls");
        return;
    }

    mode = audio_sync_mode_from_ui();

    if(mode != 0 && !audio_sync_mode_supports_wav(mode)) {
        audio_input_selector_use_jack_playback();
        vj_msg(VEEJAY_MSG_INFO, "%s uses JACK input as external sync provider", audio_sync_mode_name(mode));
        return;
    }

    if(mode == 0)
        audio_sync_set_mode_combo_guarded(VJ_AUDIO_SYNC_MODE_MONITOR);

    if(audio_sync_mode_is_provider_only(audio_sync_mode_from_ui()))
        audio_sync_activate_provider_only(audio_sync_mode_from_ui(), 1);
    else
        audio_input_selector_use_wav_playback(1);
    vj_msg(VEEJAY_MSG_INFO, "External sync provider: WAV file%s",
           audio_sync_loop_from_ui() ? " (loop)" : "");
}

void on_audio_sync_wav_browse_button_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return;

    if(!audio_sync_wav_path_ready(1))
        return;

    audio_sync_set_master_wav_options_visible(1);

    if(!audio_global_source_controls_allowed()) {
        vj_msg(VEEJAY_MSG_INFO,
               "WAV file selected for profile storage; current sample keeps its own audio route");
        return;
    }

    if(audio_sync_mode_from_ui() != 0 && !audio_sync_mode_supports_wav(audio_sync_mode_from_ui())) {
        vj_msg(VEEJAY_MSG_INFO, "%s uses JACK input; WAV path stored but not activated",
               audio_sync_mode_name(audio_sync_mode_from_ui()));
        return;
    }

    audio_sync_set_external_source_guarded(1);
    if(audio_sync_mode_is_provider_only(audio_sync_mode_from_ui()))
        audio_sync_activate_provider_only(audio_sync_mode_from_ui(), 0);
    else
        audio_input_selector_use_wav_playback(0);
    vj_msg(VEEJAY_MSG_INFO, "WAV sync provider file selected");
}

void on_audio_sync_wav_path_activate(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return;

    audio_sync_set_master_wav_options_visible(1);

    if(!audio_global_source_controls_allowed()) {
        vj_msg(VEEJAY_MSG_INFO,
               "WAV path updated for profile storage; current sample keeps its own audio route");
        return;
    }

    if(audio_sync_mode_from_ui() != 0 && !audio_sync_mode_supports_wav(audio_sync_mode_from_ui())) {
        vj_msg(VEEJAY_MSG_INFO, "%s uses JACK input; WAV path stored but not activated",
               audio_sync_mode_name(audio_sync_mode_from_ui()));
        return;
    }

    audio_sync_set_external_source_guarded(1);
    if(audio_sync_mode_is_provider_only(audio_sync_mode_from_ui()))
        audio_sync_activate_provider_only(audio_sync_mode_from_ui(), 0);
    else
        audio_input_selector_use_wav_playback(0);
    vj_msg(VEEJAY_MSG_INFO, "WAV sync provider path applied");
}

void on_audio_sync_wav_loop_toggle_toggled(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return;

    if(info && info->tl)
        timeline_set_audio_lane_loop(info->tl, audio_sync_loop_from_ui() ? TRUE : FALSE);

    if(!audio_global_source_controls_allowed()) {
        vj_msg(VEEJAY_MSG_INFO,
               "WAV loop setting updated for profile storage; current sample keeps its own audio route");
        return;
    }

    if(audio_input_selector_wav_from_ui()) {
        if(audio_sync_mode_is_provider_only(audio_sync_mode_from_ui()))
            audio_sync_activate_provider_only(audio_sync_mode_from_ui(), 0);
        else
            audio_input_selector_use_wav_playback(0);
        vj_msg(VEEJAY_MSG_INFO, "WAV sync provider loop %s requested",
               audio_sync_loop_from_ui() ? "enabled" : "disabled");
    }
}

void on_audio_sync_refresh_button_clicked(GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    single_vims(VIMS_AUDIO_SYNC_PRINT);
    vj_msg(VEEJAY_MSG_INFO, "Requested Audio panel status refresh");
}


void	on_button_samplestart_clicked(GtkWidget *widget, gpointer user_data)
{
	info->sample[0] = info->status_tokens[FRAME_NUM];
	vj_msg(VEEJAY_MSG_INFO, "New sample startings position is %d",
		info->sample[0] );
}
void	on_button_sampleend_clicked(GtkWidget *widget, gpointer user_data)
{
	info->sample[1] = info->status_tokens[FRAME_NUM];
	multi_vims( VIMS_SAMPLE_NEW, "%d %d", info->sample[0],info->sample[1]);
	vj_msg(VEEJAY_MSG_INFO, "New sample from EditList %d - %d",
		info->sample[0], info->sample[1]);
}

void	on_button_veejay_clicked(GtkWidget *widget, gpointer user_data)
{
#ifdef STRICT_CHECKING

#endif
	info->watch.state = STATE_CONNECT;
    int do_sync = 0;
    is_alive(&do_sync);
}
void	on_button_sendvims_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *text = get_text("vimsmessage");
	if(strncasecmp( text, "600:;",5 ) == 0)
		veejay_quit();
	vj_msg(VEEJAY_MSG_INFO, "User defined VIMS message sent '%s'",text );
	msg_vims( text );
}
void	on_vimsmessage_activate(GtkWidget *widget, gpointer user_data)
{
	msg_vims( get_text( "vimsmessage") );
	vj_midi_learning_vims( info->midi, NULL, get_text("vimsmessage"),0);
	vj_msg(VEEJAY_MSG_INFO, "User defined VIMS message sent '%s'", get_text("vimsmessage"));
}

void	on_button_fadedur_value_changed(GtkWidget *widget, gpointer user_data)
{

}

void	on_toggle_fademethod_toggled(GtkWidget *w, gpointer user_data)
{
	if(info->status_lock)
		return;

	multi_vims( VIMS_CHAIN_FADE_ALPHA,"%d %d",0, is_button_toggled("toggle_fademethod") );
	vj_midi_learning_vims_toggle2(info->midi, "toggle_fademethod", VIMS_CHAIN_FADE_ALPHA, 0);
}


void	on_fx_m2_toggled(GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;
    if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ) {
	    multi_vims( VIMS_CHAIN_FADE_METHOD, "%d %d",0, 2 );
	    multi_vims( VIMS_CHAIN_FADE_ENTRY,"%d %d", 0, info->uc.selected_chain_entry );
    }
}

void	on_fx_m1_toggled(GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;
    if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ) {
	    multi_vims( VIMS_CHAIN_FADE_METHOD, "%d %d",0, 1);
	    multi_vims( VIMS_CHAIN_FADE_ENTRY,"%d %d",0, info->uc.selected_chain_entry);
    }
}

void	on_fx_m3_toggled(GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;
    if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ) {
	    multi_vims( VIMS_CHAIN_FADE_METHOD, "%d %d",0, 3);
	    multi_vims( VIMS_CHAIN_FADE_ENTRY,"%d %d", 0, info->uc.selected_chain_entry );
    }
}

void	on_fx_m4_toggled(GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;
    if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ) {
	    multi_vims( VIMS_CHAIN_FADE_METHOD, "%d %d",0, 4);
	    multi_vims( VIMS_CHAIN_FADE_ENTRY,"%d %d", 0, info->uc.selected_chain_entry );
    }
}
void	on_fx_mnone_toggled(GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;
    if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ) {
	    multi_vims( VIMS_CHAIN_FADE_METHOD, "%d %d",0, 0);
	    multi_vims( VIMS_CHAIN_FADE_ENTRY,"%d %d", 0, -1);
    }
}

void	on_button_fadeout_clicked(GtkWidget *w, gpointer user_data)
{
	gint num = (gint)get_numd( "button_fadedur");
	char *timenow = format_time( num, info->el.fps );
	int vims_id = is_button_toggled( "toggle_fademethod" ) ? VIMS_CHAIN_FADE_OUT: VIMS_CHAIN_FADE_IN;
	multi_vims( vims_id, "0 %d", num );
	vj_midi_learning_vims_complex( info->midi, "button_fadedur", vims_id, 0, 5 );
	vj_msg(VEEJAY_MSG_INFO, "Fade out duration %s (frames %d)",timenow,num );
	if(timenow) free(timenow);
}

void	on_button_fadein_clicked(GtkWidget *w, gpointer user_data)
{
	gint num = (gint)get_numd( "button_fadedur");
	char *timenow = format_time( num, info->el.fps );
	int vims_id = is_button_toggled( "toggle_fademethod" ) ? VIMS_CHAIN_FADE_IN: VIMS_CHAIN_FADE_OUT;
	multi_vims( vims_id, "0 %d", num );
	vj_midi_learning_vims_complex( info->midi, "button_fadedur",vims_id, 0, 5 );
	vj_msg(VEEJAY_MSG_INFO, "Fade in duration %s (frames %d)",timenow,num );
	if(timenow) free(timenow);
}

void	on_manualopacity_value_changed(GtkWidget *w, gpointer user_data)
{
	if(info->status_lock)
		return;

	GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gdouble val = gtk_adjustment_get_value (a);

	int mode = is_button_toggled("toggle_fademethod");
	int value = ( mode == 1 ? 0xff - (int) val : (int) val );

	multi_vims( VIMS_CHAIN_MANUAL_FADE, "0 %d", value );

	vj_midi_learning_vims_complex( info->midi, "manualopacity", VIMS_CHAIN_MANUAL_FADE, 0,1 );

	vj_msg(VEEJAY_MSG_INFO, "FX Opacity set to %1.2f", val );
}

static void	el_selection_update(void)
{
	gchar *text = format_selection_time(info->selection[0], info->selection[1]);
	update_label_str( "label_el_selection", text );
	vj_msg( VEEJAY_MSG_INFO, "Updated EditList selection %d - %d Timecode: %s",
		info->selection[0], info->selection[1], text );
	g_free(text);
}

void	on_button_el_selstart_value_changed(GtkWidget *w, gpointer user_data)
{
	info->selection[0] = get_nums("button_el_selstart");
	if( info->selection[0] > info->selection[1])
		update_spin_value( "button_el_selend", info->selection[0]);
	char *text = format_time( info->selection[0], info->el.fps );
	update_label_str( "label_el_startpos", text);
	free(text);
	el_selection_update();
}

void	on_button_el_selend_value_changed(GtkWidget *w, gpointer user_data)
{
	info->selection[1] = get_nums( "button_el_selend" );
	if(info->selection[1] < info->selection[0])
		update_spin_value( "button_el_selstart", info->selection[1]);
	char *text = format_time( info->selection[1], info->el.fps);
	update_label_str( "label_el_endpos", text);
	free(text);
	el_selection_update();
}

static	gboolean verify_selection(void)
{
	if( (info->selection[1] - info->selection[0] ) <= 0)
	{
		vj_msg(VEEJAY_MSG_ERROR, "Invalid EditList selection %d - %d",
			info->selection[0], info->selection[1]);
		return FALSE;
	}
	return TRUE;
}

void	on_button_el_cut_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{	multi_vims( VIMS_EDITLIST_CUT, "%d %d",
			info->selection[0], info->selection[1]);
		char *time1 = format_time( info->selection[0], info->el.fps );
		char *time2 = format_time( info->selection[1], info->el.fps );
		vj_msg(VEEJAY_MSG_INFO, "Cut %s - %s from EditList to buffer",
			time1, time2 );
		free(time1);
		free(time2);
	}
}
void	on_button_el_del_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_DEL, "%d %d",
			info->selection[0], info->selection[1]);
        multi_vims( VIMS_SAMPLE_CLEAR_MARKER, "%d", 0);
		char *time1 = format_time( info->selection[0],info->el.fps );
		char *time2 = format_time( info->selection[1],info->el.fps );
		vj_msg(VEEJAY_MSG_INFO, "Delete %s - %s from EditList",
			time1, time2 );
		free(time1);
		free(time2);
		update_spin_value( "button_el_selstart", 0 );
		update_spin_value( "button_el_selend", 0);
	}
}
void	on_button_el_crop_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_CROP, "%d %d",
			info->selection[0], info->selection[1]);
        multi_vims( VIMS_SAMPLE_CLEAR_MARKER, "%d", 0);
		char *total = format_time( info->status_tokens[TOTAL_FRAMES],info->el.fps );
		char *time2 = format_time( info->selection[1],info->el.fps );
		char *time1 = format_time( info->selection[0],info->el.fps );
		vj_msg(VEEJAY_MSG_INFO, "Delete 00:00:00 - %s and %s - %s from EditList",
			time1, time2, total );
		free(time1);
		free(time2);
		free(total);

	}
}
void	on_button_el_copy_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_COPY, "%d %d",
			info->selection[0], info->selection[1] );
		char *time1 = format_time( info->selection[0],info->el.fps );
		char *time2 = format_time( info->selection[1],info->el.fps );
		vj_msg(VEEJAY_MSG_INFO, "Copy %s - %s to buffer",
			time1,time2);
		free(time1);
		free(time2);
	}
}

void	on_button_el_newclip_clicked(GtkWidget *w, gpointer *user)
{
	if(verify_selection())
	{
		multi_vims( VIMS_SAMPLE_NEW, "%d %d",
			info->selection[0], info->selection[1] );
		vj_msg(VEEJAY_MSG_INFO, "New sample from EditList %d - %d" ,
			info->selection[0], info->selection[1] );
	}
}

void    on_button_el_takepastepos_clicked(GtkWidget *w, gpointer *user_data)
{
    update_spin_value ("button_el_selpaste",
                        info->status_tokens[FRAME_NUM] );
    vj_msg(VEEJAY_MSG_INFO, "Set current frame %d as destination position",
           info->status_tokens[FRAME_NUM]);
}

void	on_button_el_pasteat_clicked(GtkWidget *w, gpointer *user_data)
{
	gint val = get_nums( "button_el_selpaste" );
	info->selection[2] = val;
    multi_vims( VIMS_SAMPLE_CLEAR_MARKER, "%d", 0 );

	multi_vims( VIMS_EDITLIST_PASTE_AT, "%d",
		info->selection[2]);
	char *time1 = format_time( info->selection[2],info->el.fps );
	vj_msg(VEEJAY_MSG_INFO, "Paste contents from buffer to frame %d (timecode %s)",
		info->selection[2], time1);
	free(time1);
	info->uc.reload_hint[HINT_EL] = 1;

}
void	on_button_el_save_clicked(GtkWidget *w, gpointer *user_data)
{
	gchar *filename = dialog_save_file( "Save EditList", "veejay-editlist.edl");
	if(filename)
	{
		multi_vims( VIMS_EDITLIST_SAVE, "%s %d %d",
			filename, 0, info->el.num_frames );
		vj_msg(VEEJAY_MSG_INFO, "Saved EditList to %s", filename);
		g_free(filename);
	}
}
void	on_button_el_savesel_clicked(GtkWidget *w, gpointer *user_data)
{
	gchar *filename = dialog_save_file( "Save EditList Selection", "veejay-editlist.edl");
	if(filename)
	{
		gint start = get_nums( "button_el_selstart" );
		gint end = get_nums( "button_el_selend" );
		multi_vims( VIMS_EDITLIST_SAVE, "%s %d %d",
			filename, start, end );
		vj_msg(VEEJAY_MSG_INFO,"Save EditList selection to %s", filename);
		g_free(filename);
	}
}

void	on_button_el_add_clicked(GtkWidget *w, gpointer *user_data)
{
	gchar *filename = dialog_open_file( "Append videofile to EditList", FILE_FILTER_DEFAULT );
	if(filename)
	{
		multi_vims( VIMS_EDITLIST_ADD, "%s",
		filename );
		vj_msg(VEEJAY_MSG_INFO, "Try to add file '%s' to EditList", filename);
		g_free(filename);
	}
}
void	on_button_el_addsample_clicked(GtkWidget *w, gpointer *user_data)
{
	gchar *filename = dialog_open_file( "Append videofile (and create sample)",FILE_FILTER_DEFAULT);
	if( filename )
    {
        multi_vims( VIMS_EDITLIST_ADD_SAMPLE, "%d %s", 0, filename );
        g_free(filename);
    }
}

void	on_button_el_delfile_clicked(GtkWidget *w, gpointer *user_data)
{
	int frame = _el_ref_start_frame( info->uc.selected_el_entry );
	int first_frame = frame;
	int last_frame = _el_ref_end_frame( info->uc.selected_el_entry );
	multi_vims( VIMS_EDITLIST_DEL, "%d %d", first_frame, last_frame );
	char *time1 = format_time( first_frame,info->el.fps );
	char *time2 = format_time( last_frame,info->el.fps );
	vj_msg(VEEJAY_MSG_INFO, "Delete %s - %s",
		time1,time2);
	free(time1);
	free(time2);
}
void	on_button_fx_clearchain_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_CHAIN_CLEAR, "%d",0);
	info->uc.reload_hint[HINT_CHAIN] = 1;
	info->uc.reload_hint[HINT_ENTRY] = 1;
    info->uc.reload_hint[HINT_KF] = 1;
	vj_midi_learning_vims_msg( info->midi, NULL, VIMS_CHAIN_CLEAR,0 );
	vj_msg(VEEJAY_MSG_INFO, "Clear FX Chain");
}

void	on_button_entry_toggle_clicked(GtkWidget *w, gpointer user_data)
{
	if(!info->status_lock && !info->parameter_lock)
	{
		gint val = is_button_toggled( "button_entry_toggle" );
		int vims_id = VIMS_CHAIN_ENTRY_SET_VIDEO_OFF;
		if(val)
			vims_id = VIMS_CHAIN_ENTRY_SET_VIDEO_ON;
		multi_vims( vims_id,"%d %d", 0, info->uc.selected_chain_entry );

		vj_midi_learning_vims_msg2( info->midi, NULL, vims_id, 0, info->uc.selected_chain_entry );
		vj_msg(VEEJAY_MSG_INFO, "Chain Entry %d is %s",
			info->uc.selected_chain_entry,
			(val ? "Enabled" : "Disabled" ));
		info->uc.reload_hint[HINT_ENTRY] = 1;
	}
}

static void update_fx_chain(int val) {
    GtkTreeView *view = GTK_TREE_VIEW(glade_xml_get_widget_(info->main_window, "tree_chain"));
    GtkTreeModel *model = gtk_tree_view_get_model( view );

    gtk_tree_model_foreach( model, chain_update_row, (gpointer*) info );

    GtkTreePath *path = gtk_tree_path_new_from_indices(val, -1);
    gtk_tree_view_set_cursor (view, path, NULL, FALSE);
    gtk_tree_path_free (path);
}

void	on_button_fx_entry_value_changed(GtkWidget *w, gpointer user_data)
{
	if(!info->status_lock)
	{
        int val = (gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(w));
		multi_vims( VIMS_CHAIN_SET_ENTRY, "%d", val );
		vj_midi_learning_vims_spin( info->midi, "button_fx_entry", VIMS_CHAIN_SET_ENTRY );
        update_fx_chain(val);
	}
}

void	on_button_fx_del_clicked(GtkWidget *w, gpointer user_data)
{
    int entry = info->uc.selected_chain_entry;

	multi_vims( VIMS_CHAIN_ENTRY_CLEAR, "%d %d", 0, entry );

    clear_chain_row_in_tree(entry);
    info->uc.reload_hint_checksums[HINT_CHAIN] = -1;
    info->uc.reload_hint_checksums[HINT_ENTRY] = -1;
	info->uc.reload_hint[HINT_ENTRY] = 1;
	info->uc.reload_hint[HINT_CHAIN] = 1;
    info->uc.reload_hint[HINT_KF] = 1;
	vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_CHAIN_ENTRY_CLEAR, 0, entry );
	vj_msg(VEEJAY_MSG_INFO, "Clear Effect from Entry %d", entry);
}

static void gen_changed( int num, int value )
{
	int i;
	int values[16];

	if(!info->status_lock && !info->parameter_lock)
	{
		info->parameter_lock = 1;

		for( i = 0; i < GENERATOR_PARAMS; i ++ ) {
			if( num == i ) {
				values[num] = value;
			}
			else {
				GtkWidget *w = glade_xml_get_widget_( info->main_window, gen_names_[i].text );
                GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
                values[i] = gtk_adjustment_get_value (a);
			}
		}

		char line[255];
		snprintf(line,sizeof(line), "%d:0 %d %d %d %d %d %d %d %d %d %d;",
				VIMS_STREAM_SET_ARG,
				values[0],values[1],values[2],values[3],values[4],values[5],
				values[6],values[7],values[8],values[9]);
		msg_vims(line);

		info->parameter_lock = 0;
	}

}

static void genv_changed( int num, int value, const char *selected )
{
	int i;
	int values[16];

	if(!info->status_lock && !info->parameter_lock)
	{
		info->parameter_lock = 1;

		for( i = 0; i < GENERATOR_PARAMS; i ++ ) {
			GtkWidget *w = glade_xml_get_widget_( info->main_window, gen_names_[i].text );
            if( w == NULL ) {
                veejay_msg(0, "No such widget: %s", gen_names_[i].text);
                return;
            }
            GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
			if( num == i ) {
				update_slider_value( gen_names_[i].text, (get_slider_val(gen_names_[i].text) + value), 0 );
				values[i] = (gint) gtk_adjustment_get_value (a);
			}
			else {
				values[i] = (gint) gtk_adjustment_get_value (a);
			}
		}

		char line[255];
		snprintf(line,sizeof(line), "%d:0 %d %d %d %d %d %d %d %d %d %d;",
				VIMS_STREAM_SET_ARG,
				values[0],values[1],values[2],values[3],values[4],values[5],
				values[6],values[7],values[8],values[9]);
		msg_vims(line);

		info->parameter_lock = 0;
	}
}

void	on_slider_p0_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 0, (gint)gtk_adjustment_get_value (a) );
}
void	on_slider_p1_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 1, (gint)gtk_adjustment_get_value (a) );
}
void	on_slider_p2_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 2, (gint)gtk_adjustment_get_value (a) );
}

void	on_slider_p3_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 3, (gint)gtk_adjustment_get_value (a) );
}
void	on_slider_p4_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 4, (gint)gtk_adjustment_get_value (a) );
}

void	on_slider_p5_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
		SLIDER_CHANGED( 5, (gint)gtk_adjustment_get_value (a) );
}
void	on_slider_p6_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 6, (gint)gtk_adjustment_get_value (a) );
}

void	on_slider_p7_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 7, (gint)gtk_adjustment_get_value (a) );
}

void	on_slider_p8_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 8, (gint)gtk_adjustment_get_value (a) );
}

void	on_slider_p9_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 9, (gint)gtk_adjustment_get_value (a) );
}

void	on_slider_p10_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 10, (gint)gtk_adjustment_get_value (a) );
}
void	on_slider_p11_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 11, (gint)gtk_adjustment_get_value (a) );
}
void	on_slider_p12_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 12, (gint)gtk_adjustment_get_value (a) );
}
void	on_slider_p13_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 13, (gint)gtk_adjustment_get_value (a) );
}
void	on_slider_p14_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 14, (gint)gtk_adjustment_get_value (a) );
}
void	on_slider_p15_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	SLIDER_CHANGED( 15, (gint)gtk_adjustment_get_value (a) );
}

void    on_inc_p0_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P0], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P0], handler_id);

    PARAM_CHANGED( 0, 1 , "slider_p0" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P0], handler_id);
}

void    on_dec_p0_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P0], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P0], handler_id);

    PARAM_CHANGED( 0, -1, "slider_p0");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P0], handler_id);
}

void    on_inc_p1_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P1], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P1], handler_id);

    PARAM_CHANGED( 1, 1 , "slider_p1" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P1], handler_id);
}

void    on_dec_p1_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P1], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P1], handler_id);

    PARAM_CHANGED( 1, -1, "slider_p1");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P1], handler_id);
}

void    on_inc_p2_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P2], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P2], handler_id);

    PARAM_CHANGED( 2, 1 , "slider_p2" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P2], handler_id);
}

void    on_dec_p2_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P2], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P2], handler_id);

    PARAM_CHANGED( 2, -1, "slider_p2");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P2], handler_id);
}

void    on_inc_p3_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P3], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P3], handler_id);

    PARAM_CHANGED( 3, 1 , "slider_p3" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P3], handler_id);
}

void    on_dec_p3_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P3], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P3], handler_id);

    PARAM_CHANGED( 3, -1, "slider_p3");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P3], handler_id);
}

void    on_inc_p4_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P4], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P4], handler_id);

    PARAM_CHANGED(4, 1 , "slider_p4" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P4], handler_id);
}

void    on_dec_p4_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P4], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P4], handler_id);

    PARAM_CHANGED( 4, -1, "slider_p4");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P4], handler_id);
}

void    on_inc_p5_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P5], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P5], handler_id);

    PARAM_CHANGED(5, 1 , "slider_p5" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P5], handler_id);
}

void    on_dec_p5_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P5], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P5], handler_id);

    PARAM_CHANGED(5, -1, "slider_p5");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P5], handler_id);
}

void    on_inc_p6_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P6], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P6], handler_id);

    PARAM_CHANGED(6, 1 , "slider_p6" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P6], handler_id);
}

void    on_dec_p6_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P6], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P6], handler_id);

    PARAM_CHANGED( 6, -1, "slider_p6");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P6], handler_id);
}

void    on_inc_p7_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P7], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P7], handler_id);

    PARAM_CHANGED(7, 1 , "slider_p7" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P7], handler_id);
}

void    on_dec_p7_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P7], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P7], handler_id);

    PARAM_CHANGED( 7, -1, "slider_p7");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P7], handler_id);
}

void    on_inc_p8_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P8], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P8], handler_id);

    PARAM_CHANGED(8, 1 , "slider_p8" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P8], handler_id);
}

void    on_dec_p8_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P8], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P8], handler_id);

    PARAM_CHANGED( 8, -1, "slider_p8");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P8], handler_id);
}
void    on_inc_p9_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P9], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P9], handler_id);

    PARAM_CHANGED(9, 1 , "slider_p9" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P9], handler_id);
}
void    on_dec_p9_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P9], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P9], handler_id);

    PARAM_CHANGED( 9, -1, "slider_p9");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P9], handler_id);
}
void    on_inc_p10_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P10], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P10], handler_id);

    PARAM_CHANGED(10, 1 , "slider_p10" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P10], handler_id);
}
void    on_dec_p10_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P10], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P10], handler_id);

    PARAM_CHANGED( 10, -1, "slider_p10");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P10], handler_id);
}
void    on_inc_p11_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P11], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P11], handler_id);

    PARAM_CHANGED(11, 1 , "slider_p11" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P11], handler_id);
}
void    on_dec_p11_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P11], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P11], handler_id);

    PARAM_CHANGED( 11, -1, "slider_p11");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P11], handler_id);
}
void    on_inc_p12_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P12], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P12], handler_id);

    PARAM_CHANGED(12, 1 , "slider_p12" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P12], handler_id);
}
void    on_dec_p12_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P12], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P12], handler_id);

    PARAM_CHANGED( 12, -1, "slider_p12");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P12], handler_id);
}
void    on_inc_p13_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P13], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P13], handler_id);

    PARAM_CHANGED(13, 1 , "slider_p13" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P13], handler_id);
}
void    on_dec_p13_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P13], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P13], handler_id);

    PARAM_CHANGED( 13, -1, "slider_p13");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P13], handler_id);
}
void    on_inc_p14_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P14], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P14], handler_id);

    PARAM_CHANGED(14, 1 , "slider_p14" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P14], handler_id);
}
void    on_dec_p14_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P14], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P14], handler_id);

    PARAM_CHANGED( 14, -1, "slider_p14");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P14], handler_id);
}
void    on_inc_p15_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P15], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P15], handler_id);

    PARAM_CHANGED(15, 1 , "slider_p15" );

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P15], handler_id);
}
void    on_dec_p15_clicked(GtkWidget *w, gpointer user_data)
{

    guint signal_id=g_signal_lookup("value_changed", GTK_TYPE_RANGE);
    gulong handler_id=handler_id=g_signal_handler_find( widget_cache[WIDGET_SLIDER_P15], G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL );

    if (handler_id)
        g_signal_handler_block(widget_cache[WIDGET_SLIDER_P15], handler_id);

    PARAM_CHANGED( 15, -1, "slider_p15");

    if (handler_id)
        g_signal_handler_unblock(widget_cache[WIDGET_SLIDER_P15], handler_id);
}

void	slider_g0_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 0, (gint)gtk_adjustment_get_value (a) );
}
void	slider_g1_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 1, (gint)gtk_adjustment_get_value (a) );
}
void	slider_g2_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 2, (gint)gtk_adjustment_get_value (a) );
}
void	slider_g3_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 3, (gint)gtk_adjustment_get_value (a) );
}
void	slider_g4_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 4, (gint)gtk_adjustment_get_value (a) );
}
void	slider_g5_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 5, (gint)gtk_adjustment_get_value (a) );
}
void	slider_g6_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 6, (gint)gtk_adjustment_get_value (a) );
}
void	slider_g7_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 7, (gint)gtk_adjustment_get_value (a) );
}

void	slider_g8_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 8, (gint)gtk_adjustment_get_value (a) );
}
void	slider_g9_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 9, (gint)gtk_adjustment_get_value (a) );
}
void	slider_g10_value_changed(GtkWidget *w, gpointer user_data)
{
  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gen_changed( 10, (gint)gtk_adjustment_get_value (a) );
}

void	on_inc_g0_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(0, 1 , "slider_g0" );
}
void	on_dec_g0_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(0, -1, "slider_g0");
}

void	on_inc_g1_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(1, 1 , "slider_g1" );
}
void	on_dec_g1_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(1, -1, "slider_g1");
}

void	on_inc_g2_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(2, 1 , "slider_g2" );
}
void	on_dec_g2_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(2, -1, "slider_g2");
}
void	on_inc_g3_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(3, 1 , "slider_g3" );
}
void	on_dec_g3_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(3, -1, "slider_g3");
}
void	on_inc_g4_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(4, 1 , "slider_g4" );
}
void	on_dec_g4_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(4, -1, "slider_g4");
}
void	on_inc_g5_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(5, 1 , "slider_g5" );
}
void	on_dec_g5_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(5, -1, "slider_g5");
}
void	on_inc_g6_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(6, 1 , "slider_g6" );
}
void	on_dec_g6_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(6, -1, "slider_g6");
}
void	on_inc_g7_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(7, 1 , "slider_g7" );
}
void	on_dec_g7_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(7, -1, "slider_g7");
}
void	on_inc_g8_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(8, 1 , "slider_g8" );
}
void	on_dec_g8_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(8, -1, "slider_g8");
}
void	on_inc_g9_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(9, 1 , "slider_g9" );
}
void	on_dec_g9_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(9, -1, "slider_g9");
}
void	on_inc_g10_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(10, 1 , "slider_g9" );
}
void	on_dec_g10_clicked(GtkWidget *w, gpointer user_data)
{
	genv_changed(10, -1, "slider_g9");
}

void    on_button_stoplaunch_clicked(GtkWidget *widget, gpointer user_data)
{

}

void	on_button_sample_play_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->selection_slot)
	{
		multi_vims( VIMS_SET_MODE_AND_GO , "%d %d" ,
			info->selection_slot->sample_id,
			(info->selection_slot->sample_type == MODE_SAMPLE ? MODE_SAMPLE : MODE_STREAM));

		vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_SET_MODE_AND_GO,
			info->selection_slot->sample_id,
			(info->selection_slot->sample_type == MODE_SAMPLE ? MODE_SAMPLE : MODE_STREAM ));

		vj_msg(VEEJAY_MSG_INFO, "Start playing %s %d (type %d) in slot %d",
			(info->selection_slot->sample_type == MODE_SAMPLE ? "sample" : "stream"),
			info->selection_slot->sample_id,
			info->selection_slot->sample_type);
	}
}
void	on_button_sample_del_clicked(GtkWidget *widget, gpointer user_data)
{
	if( info->selection_slot )
		remove_sample_from_slot();
}
void	on_button_samplelist_load_clicked(GtkWidget *widget, gpointer user_data)
{
	gint erase_all = 0;
	if(info->status_tokens[TOTAL_SLOTS] > 0 )
	{
		if(prompt_dialog("Load samplelist",
			"Loading a samplelist will delete any existing samples" ) == GTK_RESPONSE_REJECT)
			return;
		else
			erase_all = 1;
	}

	gchar *filename = dialog_open_file( "Open samplelist", FILE_FILTER_SL);
	if(filename)
	{
		if(erase_all)
		{
			single_vims( VIMS_SAMPLE_DEL_ALL );
		}
		multi_vims( VIMS_SAMPLE_LOAD_SAMPLELIST, "%s", filename );

		g_free(filename );
	}
}
static char samplelist_name[1024] = { 0 };
static int  as_samplelist_name = 0;

void	on_button_samplelist_save_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_save_file( "Save samplelist", "veejay-samplelist.sl");
	if(filename)
	{
		multi_vims( VIMS_SAMPLE_SAVE_SAMPLELIST, "%s", filename );
		vj_msg(VEEJAY_MSG_INFO, "Saved samples to %s", filename);
		strlcpy( samplelist_name, filename,strlen(filename));
		as_samplelist_name = 1;
		g_free(filename);
	}
}

gboolean	on_button_samplelist_qsave_clicked(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{

	if(event && ((GdkEventButton *) event)->state & GDK_SHIFT_MASK) {
		as_samplelist_name = 0;
	}

	if( as_samplelist_name == 0 ) {
		gchar *filename = dialog_save_file( "Save samplelist", "veejay-samplelist.sl");
		if(filename)
		{
			multi_vims( VIMS_SAMPLE_SAVE_SAMPLELIST, "%s", filename );
			vj_msg(VEEJAY_MSG_INFO, "Saved samples to %s", filename);
			strlcpy( samplelist_name, filename, strlen(filename));
			g_free(filename);
			as_samplelist_name = 1;
		}
	}
	else {
		multi_vims( VIMS_SAMPLE_SAVE_SAMPLELIST, "%s" , samplelist_name );
		vj_msg(VEEJAY_MSG_INFO, "Quick saved samples to %s" , samplelist_name );
	}
	return TRUE;
}

gboolean on_sync_samplelist_clicked(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    if (samplelist_name[0] != '\0') {
        multi_vims(VIMS_SAMPLE_SAVE_SAMPLELIST, "%s", samplelist_name);
        vj_msg(VEEJAY_MSG_INFO, "Saved samplelist to %s", samplelist_name);
    } else {
        gchar *filename = dialog_save_file("Save samplelist", "veejay-samplelist.sl");
        if (filename) {
            multi_vims(VIMS_SAMPLE_SAVE_SAMPLELIST, "%s", filename);
            vj_msg(VEEJAY_MSG_INFO, "Saved samples to %s", filename);
            strlcpy(samplelist_name, filename, strlen(filename) + 1);
            as_samplelist_name = 1;
            g_free(filename);
        }
    }
    return TRUE;
}

void	on_spin_samplestart_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value = (gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );
		char *time1 = format_time(value,info->el.fps);
		multi_vims(VIMS_SAMPLE_SET_START, "%d %d",0, value );
		vj_msg(VEEJAY_MSG_INFO, "Set sample's starting position to %d (timecode %s)",
			value, time1);
		free(time1);
	}
}

void	on_spin_sampleend_value_changed( GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value = (gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );
		char *time1 = format_time(value,info->el.fps);

		multi_vims(VIMS_SAMPLE_SET_END, "%d %d", 0, value );
		vj_msg(VEEJAY_MSG_INFO, "Set sample's ending position to %d (timecode %s)",
			value, time1);
		free(time1);

	}
}

void
on_button_samplestart_value_refresh_clicked ( GtkWidget *widget, gpointer user_data )
{
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(widget_cache[WIDGET_SPIN_SAMPLESTART]), 0 );
}

void
on_button_sampleend_value_reset_clicked ( GtkWidget *widget, gpointer user_data )
{
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(widget_cache[WIDGET_SPIN_SAMPLEEND]), G_MAXDOUBLE);
}

void	on_slow_slider_value_changed( GtkWidget *widget, gpointer user_data )
{
	if(!info->status_lock) {
		gint value = (gint) get_slider_val("slow_slider");
		if(current_stream_buffer_ready()) {
			multi_vims(VIMS_STREAM_BUFFER_SET_SLOW, "%d %d", current_stream_id(), value);
			vj_midi_learning_vims_msg2(info->midi, "slow_slider", VIMS_STREAM_BUFFER_SET_SLOW, current_stream_id(), value);
			return;
		}
		if(current_stream_selected()) {
			current_stream_buffer_warn_not_ready();
			return;
		}
		multi_vims(VIMS_VIDEO_SET_SLOW, "%d", value );
		value ++;
		vj_msg(VEEJAY_MSG_INFO, "Slow video to %2.2f fps",
			info->el.fps / (float) value );
		vj_midi_learning_vims_simple(info->midi, "slow_slider",VIMS_VIDEO_SET_SLOW);
	}
}

gboolean on_slow_slider_click(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return FALSE;

    if(event && (event->state & GDK_CONTROL_MASK))
    {
        update_slider_gvalue("slow_slider", 1);
        return TRUE;
    }

    return FALSE;
}


void	on_speed_slider_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value = (gint) get_slider_val( "speed_slider" );

		if(current_stream_buffer_ready()) {
			multi_vims(VIMS_STREAM_BUFFER_SET_SPEED, "%d %d", current_stream_id(), value);
			vj_midi_learning_vims_msg2(info->midi, "speed_slider", VIMS_STREAM_BUFFER_SET_SPEED, current_stream_id(), value);
			return;
		}
		if(current_stream_selected()) {
			current_stream_buffer_warn_not_ready();
			return;
		}
		multi_vims( VIMS_VIDEO_SET_SPEED, "%d", value );
			vj_msg(VEEJAY_MSG_INFO, "Change video playback speed to %d",
			value );
		vj_midi_learning_vims_simple( info->midi, "speed_slider", VIMS_VIDEO_SET_SPEED );
	}
}


gboolean on_speed_slider_click(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if(info->status_lock)
        return FALSE;

    if(event && (event->state & GDK_CONTROL_MASK))
    {
        update_slider_gvalue("speed_slider", 1);
        return TRUE;
    }

    return FALSE;
}

void	on_spin_samplespeed_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value = (gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );
		value *= info->play_direction;
		multi_vims( VIMS_SAMPLE_SET_SPEED, "%d %d",0, value );
		vj_midi_learning_vims_complex( info->midi, "spin_samplespeed", VIMS_SAMPLE_SET_SPEED,0,2 );
		vj_msg(VEEJAY_MSG_INFO, "Change video playback speed to %d",
			value );
	}
}


static int v4l_selected_stream_id(void)
{
    if(!info || !info->selected_slot)
        return 0;

    return info->selected_slot->sample_id;
}

static int v4l_widget_value_65535(GtkWidget *widget)
{
    int value = 0;

    if(widget && GTK_IS_RANGE(widget))
        value = (int)(gtk_range_get_value(GTK_RANGE(widget)) + 0.5);

    return ui_clampi(value, 0, 65535);
}

static void v4l_send_legacy_slider(GtkWidget *widget, int vims_id, const char *midi_name)
{
    int stream_id;

    if(info->status_lock)
        return;

    stream_id = v4l_selected_stream_id();
    if(stream_id <= 0)
        return;

    multi_vims(vims_id, "%d %d", stream_id, v4l_widget_value_65535(widget));

    if(midi_name)
        vj_midi_learning_vims_complex(info->midi, (char *) midi_name, vims_id, stream_id, 1);
}

static void v4l_send_named_slider(GtkWidget *widget, const char *control_name)
{
    int stream_id;

    if(info->status_lock)
        return;

    stream_id = v4l_selected_stream_id();
    if(stream_id <= 0 || !control_name)
        return;

    multi_vims(VIMS_STREAM_SET_V4LCTRL, "%d %d %s",
               stream_id,
               v4l_widget_value_65535(widget),
               control_name);
}

static void v4l_send_named_toggle(GtkWidget *widget, const char *control_name)
{
    int stream_id;
    int value;

    if(info->status_lock)
        return;

    stream_id = v4l_selected_stream_id();
    if(stream_id <= 0 || !control_name)
        return;

    if(!widget || !GTK_IS_TOGGLE_BUTTON(widget))
        return;

    value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? 1 : 0;

    multi_vims(VIMS_STREAM_SET_V4LCTRL, "%d %d %s", stream_id, value, control_name);
}

void on_v4l_brightness_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_legacy_slider(widget, VIMS_STREAM_SET_BRIGHTNESS, "v4l_brightness");
}

void on_v4l_contrast_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_legacy_slider(widget, VIMS_STREAM_SET_CONTRAST, "v4l_contrast");
}

void on_v4l_hue_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_legacy_slider(widget, VIMS_STREAM_SET_HUE, "v4l_hue");
}

void on_v4l_gamma_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "gamma");
}

void on_v4l_color_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "saturation");
}

void on_v4l_saturation_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_legacy_slider(widget, VIMS_STREAM_SET_SATURATION, "v4l_saturation");
}

void on_v4l_gain_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "gain");
}

void on_v4l_redbalance_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "red_balance");
}

void on_v4l_bluebalance_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "blue_balance");
}

void on_v4l_greenbalance_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "green_balance");
}

void on_v4l_sharpness_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "sharpness");
}

void on_v4l_backlightcompensation_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "bl_compensate");
}

void on_v4l_temperature_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "temperature");
}

void on_v4l_exposure_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "exposure");
}

void on_v4l_whiteness_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "whiteness");
}

void on_v4l_black_level_value_changed(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_slider(widget, "black_level");
}

void on_check_autogain_toggled(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_toggle(widget, "auto_gain");
}

void on_check_autohue_toggled(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_toggle(widget, "auto_hue");
}

void on_check_flip_toggled(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_toggle(widget, "fliph");
}

void on_check_flipv_toggled(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_toggle(widget, "flipv");
}

void on_check_autowhitebalance_toggled(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    v4l_send_named_toggle(widget, "auto_white");
}

void on_button_seq_clearall_clicked(GtkWidget *w, gpointer data)
{
    if(prompt_dialog("Clear All Sequence Banks",
                     "Clear all sequence banks?\nThis cannot be undone.") != GTK_RESPONSE_ACCEPT)
        return;

    single_vims(VIMS_SEQUENCE_CLEAR_ALL);

    if(info->sequencer_view && info->sequencer_view->gui_slot)
    {
        const int n_slots = info->sequencer_col * info->sequencer_row;

        if(info->sequence_playing >= 0 &&
           info->sequence_playing < n_slots &&
           info->sequencer_view->gui_slot[info->sequence_playing])
        {
            indicate_sequence(FALSE,
                info->sequencer_view->gui_slot[info->sequence_playing]);
        }

        info->sequence_playing = -1;

        int slot;
        for(slot = 0; slot < n_slots; slot++)
        {
            if(info->sequencer_view->gui_slot[slot])
            {
                gtk_label_set_text(
                    GTK_LABEL(info->sequencer_view->gui_slot[slot]->image),
                    NULL);
            }
        }
    }

	int old_lock = info->status_lock;
    info->status_lock = 1;

    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(widget_cache[WIDGET_SEQACTIVE]),
        FALSE);

	info->status_lock = old_lock;

    info->uc.reload_hint_checksums[HINT_SEQ_ACT] = -1;
    info->uc.reload_hint[HINT_SEQ_ACT] = 1;

    vj_msg(VEEJAY_MSG_INFO, "All sequence banks cleared");

	info->uc.reload_hint[HINT_SEQ_ACT] = 1;
}

void	on_seq_rec_stop_clicked( GtkWidget *w, gpointer data )
{
	single_vims( VIMS_SAMPLE_REC_STOP );
}

void	on_rec_seq_start_clicked( GtkWidget *w, gpointer data )
{
	GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT( glade_xml_get_widget_(info->main_window,"combo_samplecodec"));
	gchar *gformat = (gchar*)gtk_combo_box_text_get_active_text(combo) ;
	gchar *format = gformat;
	if(format != NULL && strlen(format) > 2)
	{
		multi_vims( VIMS_RECORD_DATAFORMAT,"%s",
			format );
	}
	else
	{
		format = NULL;
	}

	multi_vims( VIMS_SAMPLE_REC_START,
		"%d %d",
		0,
		0 );

	vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_SAMPLE_REC_START, 0, 0 );

	if(gformat)
		g_free(gformat);
}

void	on_stream_recordstart_clicked(GtkWidget *widget, gpointer user_data)
{
	gint nframes = get_nums( "spin_streamduration");
	gint autoplay = is_button_toggled("button_stream_autoplay");
	GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT( glade_xml_get_widget_(info->main_window,"combo_streamcodec"));
	gchar *gformat = (gchar*)gtk_combo_box_text_get_active_text(combo);
	gchar *format = gformat;

	if(nframes <= 0) {
		vj_msg(VEEJAY_MSG_WARNING, "Stream recorder needs a positive duration");
		if(gformat)
			g_free(gformat);
		return;
	}

	if(format != NULL && strlen(format) > 2)
	{
		multi_vims( VIMS_RECORD_DATAFORMAT,"%s",
			format );
	}
	else
	{
		format = NULL;
	}

	multi_vims( VIMS_STREAM_REC_START,
		"%d %d",
		nframes,
		autoplay );
	vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_STREAM_REC_START, nframes, autoplay );

	char *time1 = format_time( nframes,info->el.fps );
	if(format)
		vj_msg(VEEJAY_MSG_INFO, "Record in %s,  duration: %s",format, time1);
	else
		vj_msg(VEEJAY_MSG_INFO, "Recording in default format (MJPEG) , duration: %s", time1);

	free(time1);
	if(gformat)
		g_free(gformat);
}

void	on_stream_recordstop_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_STREAM_REC_STOP );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_STREAM_REC_STOP );
	vj_msg(VEEJAY_MSG_INFO, "Stream record stop");
}

void	on_spin_streamduration_value_changed(GtkWidget *widget , gpointer user_data)
{
	gint n_frames = get_nums( "spin_streamduration" );
	char *time = format_time(n_frames,info->el.fps);
	update_label_str( "label_streamrecord_duration", time );
	free(time);
}

void	on_new_avformat_stream_clicked(GtkWidget *wid, gpointer data)
{
	char *url = get_text("inputstream_filename");
	multi_vims(VIMS_STREAM_NEW_AVFORMAT, "%s", url);
}

void	on_shm_3490_clicked(GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_STREAM_NEW_SHARED, "%d", 3490 );
}

void	on_shm_4490_clicked(GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_STREAM_NEW_SHARED, "%d", 4490 );
}
void	on_shm_5490_clicked(GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_STREAM_NEW_SHARED, "%d", 5490 );
}
void	on_shm_6490_clicked(GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_STREAM_NEW_SHARED, "%d", 6490 );
}
void	on_shm_7490_clicked(GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_STREAM_NEW_SHARED, "%d", 7490 );
}

void	on_button_sample_recordstart_clicked(GtkWidget *widget, gpointer user_data)
{

	gint autoplay = is_button_toggled("button_sample_autoplay");
	GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT( glade_xml_get_widget_(info->main_window,"combo_samplecodec"));

	gchar *format = (gchar*) gtk_combo_box_text_get_active_text(combo);
	gint n_frames = 0;

	gint dur_val = get_nums( "spin_sampleduration" );

	if( is_button_toggled( "sample_mulloop" ) )
	{
		int base = sample_calctime_mulloop();
		n_frames = base * dur_val;
        multi_vims( VIMS_SAMPLE_CLEAR_MARKER, "%d", 0 );
	}
	else if( is_button_toggled( "sample_mulframes" ))
	{
		n_frames = dur_val;
	}
    else if ( is_button_toggled("sample_markerloop" ))
    {
        n_frames = 0;
    }

	if(format != NULL)
	{
		multi_vims( VIMS_RECORD_DATAFORMAT,"%s",
			format );
	}

	multi_vims( VIMS_SAMPLE_REC_START,
		"%d %d",
		n_frames,
		autoplay );

	vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_SAMPLE_REC_START, n_frames, autoplay );

	char *time1 = format_time(n_frames,info->el.fps);
	if( autoplay ) {
		vj_msg(VEEJAY_MSG_INFO, "Recording %s from current sample, autoplaying	 new sample when finished.",time1);
	} else {
		vj_msg(VEEJAY_MSG_INFO,"Recording %s from current sample",time1);
	}
	free(time1);
	g_free(format);
}

void	on_button_sample_recordstop_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_SAMPLE_REC_STOP );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_SAMPLE_REC_STOP );
	vj_msg(VEEJAY_MSG_INFO, "Sample record stop");
}

static inline int sample_calctime (int nframes)
{
    if( info->status_tokens[SAMPLE_LOOP] == 2 )
        nframes *= 2;
    if( info->status_tokens[FRAME_DUP] > 0 )
        nframes *= info->status_tokens[FRAME_DUP];
    int speed = info->status_tokens[SAMPLE_SPEED];
    if( speed == 0 )
       speed = 1;
    nframes = nframes / abs(speed);

    return nframes;
}

static int sample_calctime_selection(void)
{
    int n_frames = info->status_tokens[SAMPLE_MARKER_END] - info->status_tokens[SAMPLE_MARKER_START];
    if (n_frames == 0 )
        n_frames = info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START];

    return sample_calctime(n_frames);
}

static int sample_calctime_mulloop(void)
{
    int n_frames = info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START];

    return sample_calctime(n_frames);
}

void	on_spin_sampleduration_value_changed(GtkWidget *widget , gpointer user_data)
{
    int n_frames = 0;

	if( is_button_toggled( "sample_mulloop" )) {
		n_frames = sample_calctime_mulloop();
        n_frames *= get_nums( "spin_sampleduration" );
    }
	else if ( is_button_toggled( "sample_mulframes" ) ) {
		n_frames = get_nums( "spin_sampleduration" );
    }
    else if ( is_button_toggled( "sample_markerloop" ) )
    {
        n_frames = sample_calctime_selection();
    }

	char *time = format_time( n_frames,info->el.fps );
	update_label_str( "label_samplerecord_duration", time );
	free(time);
}

void	on_sample_mulloop_clicked(GtkWidget *w, gpointer user_data)
{
    if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)) == FALSE ) {
        return;
    }
    if(!gtk_widget_is_sensitive(widget_cache[WIDGET_SPIN_SAMPLEDURATION])) {
        gtk_widget_set_sensitive(widget_cache[WIDGET_SPIN_SAMPLEDURATION], TRUE );
    }
    update_spin_range( "spin_sampleduration", 1, 1000000, 1 );
}

void	on_sample_mulframes_clicked(GtkWidget *w, gpointer user_data)
{
    if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)) == FALSE ) {
        return;
    }
    if(!gtk_widget_is_sensitive(widget_cache[WIDGET_SPIN_SAMPLEDURATION])) {
        gtk_widget_set_sensitive(widget_cache[WIDGET_SPIN_SAMPLEDURATION], TRUE );
    }

    update_spin_range( "spin_sampleduration", 2, 1000000, 2 );
}

void	on_sample_markerloop_clicked(GtkWidget *w, gpointer user_data)
{
    if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)) == FALSE ) {
        return;
    }
    if(gtk_widget_is_sensitive(widget_cache[WIDGET_SPIN_SAMPLEDURATION])) {
        gtk_widget_set_sensitive(widget_cache[WIDGET_SPIN_SAMPLEDURATION], FALSE );
    }

    update_spin_range( "spin_sampleduration", 0, 1000000, 0 );
}


void	on_spin_mudplay_value_changed(GtkWidget *widget, gpointer user_data)
{
}

void on_check_samplefx_toggled(GtkWidget *widget , gpointer user_data)
{
	if(!info->status_lock)
	{
		int vims_id = VIMS_SAMPLE_CHAIN_DISABLE;

        if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget) ) == TRUE )
			vims_id = VIMS_SAMPLE_CHAIN_ENABLE;
		multi_vims( vims_id, "%d", 0 );

		vj_midi_learning_vims_dual_toggle(info->midi, "check_samplefx", VIMS_SAMPLE_CHAIN_DISABLE, VIMS_SAMPLE_CHAIN_ENABLE, 0);
        vj_msg(VEEJAY_MSG_INFO, "Sample FX chain %s requested",
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? "enabled" : "disabled");
	}

    GtkWidget *check_samplefx = GTK_WIDGET(glade_xml_get_widget_( info->main_window, "check_samplefx"));
    GtkWidget *curve_chain_togglechain = GTK_WIDGET(glade_xml_get_widget_( info->main_window, "curve_chain_togglechain"));

    toggle_siamese_widget(widget, check_samplefx, curve_chain_togglechain, 1);
}

void on_check_streamfx_toggled(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		int vims_id = VIMS_STREAM_CHAIN_DISABLE;

        if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget) ) == TRUE )
			vims_id = VIMS_STREAM_CHAIN_ENABLE;
		multi_vims( vims_id, "%d", 0 );

		vj_midi_learning_vims_dual_toggle(info->midi, "check_streamfx", VIMS_STREAM_CHAIN_DISABLE, VIMS_STREAM_CHAIN_ENABLE, 0);
        vj_msg(VEEJAY_MSG_INFO, "Stream FX chain %s requested",
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? "enabled" : "disabled");
	}

    GtkWidget *check_streamfx = GTK_WIDGET(glade_xml_get_widget_( info->main_window, "check_streamfx"));
    GtkWidget *curve_chain_togglechain = GTK_WIDGET(glade_xml_get_widget_( info->main_window, "curve_chain_togglechain"));

    toggle_siamese_widget(widget, check_streamfx, curve_chain_togglechain, 1);

}

void on_chain_togglechain_toggled( GtkWidget *widget, gpointer user_data)
{
    switch(info->status_tokens[PLAY_MODE])
    {
      case MODE_STREAM:
            on_check_streamfx_toggled( widget, user_data);
        break;
      case MODE_SAMPLE:
            on_check_samplefx_toggled( widget, user_data);
        break;
      default:
        return;
    }
}

void on_button_globalchaincopy_clicked( GtkWidget *widget, gpointer user_data) {
	int after = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(widget_cache[WIDGET_GLOBALCHAINLEVEL])
    );
	int state = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(widget_cache[WIDGET_GLOBALCHAINTOGGLE])
    );

	int value = ( state == 0 ? 0 : state + after );
	int sample_id = 0;

	if(info->selected_slot && info->selected_slot->sample_id > 0)
		sample_id = info->selected_slot->sample_id;

	multi_vims( VIMS_GLOBAL_CHAIN_COPY, "%d %d", 0, value);
	vj_msg(VEEJAY_MSG_INFO,"Copied sample %d FX chain to Global Chain", sample_id );
}

void on_globalchain_toggled(GtkWidget *widget, gpointer user_data)
{
    if (!info->status_lock)
    {
        int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		int mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_GLOBALCHAINLEVEL]));
		int value = ( state == 0 ? 0 : state + mode );
		int sample_id = 0;

		if(info->selected_slot && info->selected_slot->sample_id > 0)
			sample_id = info->selected_slot->sample_id;

        multi_vims(VIMS_GLOBAL_CHAIN, "%d %d", sample_id, value);

        vj_msg(VEEJAY_MSG_INFO,
               "Global FX chain is %s",
               (value == 0 ? "Disabled" :
               (value == 1 ? "Enabled (Before)" : "Enabled (After)")));
    }
}

void on_globalchainlevel_toggled(GtkWidget *widget, gpointer user_data)
{
    if (!info->status_lock)
    {
		int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_GLOBALCHAINTOGGLE]));
		int mode = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget));
		vj_msg(VEEJAY_MSG_INFO, "Global FX chain renders %s the current FX chain",(mode == 1 ? "after" : "before"));

		int sample_id = 0;

		if(info->selected_slot && info->selected_slot->sample_id > 0)
			sample_id = info->selected_slot->sample_id;

		if(state > 0) {
			int value = 1 + mode;
			multi_vims(VIMS_GLOBAL_CHAIN, "%d %d", sample_id, value);
		}
	}
}

void	on_loop_none_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_SAMPLE_SET_LOOPTYPE,
			"%d %d", 0, 0 );
		vj_midi_learning_vims_msg2(info->midi,NULL,VIMS_SAMPLE_SET_LOOPTYPE,0,0 );
	}
}

void	on_loop_normal_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_SAMPLE_SET_LOOPTYPE,
			"%d %d", 0, 1 );
		vj_midi_learning_vims_msg2(info->midi,NULL,VIMS_SAMPLE_SET_LOOPTYPE,0,1 );
	}
}
void	on_loop_random_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_SAMPLE_SET_LOOPTYPE,
			"%d %d", 0,3 );
		vj_midi_learning_vims_msg2(info->midi,NULL,VIMS_SAMPLE_SET_LOOPTYPE,0,3 );

	}
}

void	on_loop_pingpong_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{	multi_vims( VIMS_SAMPLE_SET_LOOPTYPE,
			"%d %d", 0,2 );
		vj_midi_learning_vims_msg2(info->midi,NULL,VIMS_SAMPLE_SET_LOOPTYPE,0,2 );

	}
}

void	on_loop_oncenop_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims(VIMS_SAMPLE_SET_LOOPTYPE,"%d %d",0,4);
		vj_midi_learning_vims_msg2(info->midi,NULL,VIMS_SAMPLE_SET_LOOPTYPE,0,4);
	}
}


void	on_button_clearmarker_clicked(GtkWidget *widget, gpointer user_data)
{
    if(!timeline_marker_can_send()) {
        if(info && !info->status_lock && !timeline_marker_mode_is_sample())
            timeline_marker_disable_selection_guarded(info ? info->tl : NULL);
        return;
    }

	multi_vims( VIMS_SAMPLE_CLEAR_MARKER, "%d", 0 );
	vj_midi_learning_vims_msg( info->midi, NULL, VIMS_SAMPLE_CLEAR_MARKER, 0 );
	char *dur = format_framenum( 0 );
	update_label_str( "label_markerduration", dur );
	free(dur);
}


void	on_check_audio_mute_clicked(GtkWidget *widget, gpointer user_data)
{
}
void	on_button_samplelist_open_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->status_tokens[TOTAL_SLOTS] > 0 )
	{
		vj_msg(VEEJAY_MSG_WARNING, "Any existing samples will be deleted.");
	}

	gchar *filename = dialog_open_file( "Open samplelist",FILE_FILTER_SL);
	if(filename)
	{
		single_vims( VIMS_SAMPLE_DEL_ALL );
		multi_vims( VIMS_SAMPLE_LOAD_SAMPLELIST, "%s", filename );
		g_free(filename );
	}
}
void	on_button_samplelist_append_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Append a samplelist",FILE_FILTER_SL);
	if(filename)
	{
		multi_vims( VIMS_SAMPLE_LOAD_SAMPLELIST, "%s", filename );
		g_free(filename );
	}
}
void	on_veejay_expander_activate(GtkWidget *exp, gpointer user_data)
{
}
void	on_button_el_takestart_clicked(GtkWidget *widget, gpointer user_data)
{
	update_spin_value( "button_el_selstart",
			info->status_tokens[FRAME_NUM] );
	vj_msg(VEEJAY_MSG_INFO, "Set current frame %d as editlist starting position",
		info->status_tokens[FRAME_NUM]);
}
void	on_button_el_takeend_clicked(GtkWidget *widget, gpointer user_data)
{
	update_spin_value ("button_el_selend",
			info->status_tokens[FRAME_NUM] );
	vj_msg(VEEJAY_MSG_INFO, "Set current frame %d as editlist ending position",
		info->status_tokens[FRAME_NUM]);
}
void	on_button_el_paste_clicked(GtkWidget *widget, gpointer user_data)
{
	multi_vims( VIMS_EDITLIST_PASTE_AT, "%d",
		info->status_tokens[FRAME_NUM] );
	char *time1 = format_time( info->status_tokens[FRAME_NUM],info->el.fps );
	vj_msg(VEEJAY_MSG_INFO, "Paste contents of buffer at Frame %d (Tiemcode %s)",
		info->status_tokens[FRAME_NUM], time1);
	free(time1);
}
void	on_new_colorstream_clicked(GtkWidget *widget, gpointer user_data)
{
	GdkColor current_color;
	GtkWidget *colorsel = glade_xml_get_widget_(info->main_window,
			"colorselection" );
	gtk_color_selection_get_current_color(
		GTK_COLOR_SELECTION( colorsel ),
		&current_color );


	gint red = current_color.red / 255.0;
	gint green = current_color.green / 255.0;
	gint blue = current_color.blue / 255.0;
	multi_vims( VIMS_STREAM_NEW_COLOR, "%d %d %d",red,green,blue );
}

#define atom_aspect_ratio(name,type) {\
info->uc.priout_lock=1;\
gint value = (type == 0 ? resize_primary_ratio_x() : resize_primary_ratio_y() );\
update_spin_value(name, value);\
info->uc.priout_lock=0;\
}
void	on_priout_width_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock && !info->uc.priout_lock)
	if( is_button_toggled( "priout_ratio" ))
		atom_aspect_ratio( "priout_height", 1 );
}
void	on_priout_height_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock && !info->uc.priout_lock)
	if( is_button_toggled( "priout_ratio" ))
		atom_aspect_ratio( "priout_width", 0 );
}
void	on_priout_apply_clicked(GtkWidget *widget, gpointer user_data)
{
	gint width = get_nums( "priout_width" );
	gint height = get_nums( "priout_height" );
	gint x = get_nums("priout_x" );
	gint y = get_nums("priout_y" );

	if( width > 0 && height > 0 )
	{
		multi_vims( VIMS_RESIZE_SDL_SCREEN, "%d %d %d %d",
			width,height,x , y );
		vj_msg(VEEJAY_MSG_INFO, "Resize Video Window to %dx%d", width,height);
	}

}


void	on_vims_take_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_BUNDLE_CAPTURE );
	info->uc.reload_hint[HINT_BUNDLES] = 1;
	vj_msg(VEEJAY_MSG_INFO, "Create a new bundle from FX Chain. See View -> VIMS Bundles");
}
void	on_button_key_detach_clicked(GtkWidget *widget, gpointer user)
{
#ifdef HAVE_SDL
	int key_val  = info->uc.pressed_key;
	int key_mod  = info->uc.pressed_mod;

	if( key_val > 0 )
	{
		multi_vims(
			VIMS_BUNDLE_ATTACH_KEY,
			"1 %d %d %s",
			key_val, key_mod, "dummy" );
		info->uc.reload_hint[HINT_BUNDLES] = 1;
		char *strmod = sdlmod_by_id( key_mod );
		char *strkey = sdlkey_by_id( key_val);
		if(key_mod)
			vj_msg(VEEJAY_MSG_INFO, "Key binding %s + %s released",strmod,strkey);
		else
			vj_msg(VEEJAY_MSG_INFO, "Key binding %s released", strkey );
	}
#endif
}

void	on_vims_key_clicked( GtkWidget *widget, gpointer user_data)
{
#ifdef HAVE_SDL
	char which_vims[128];
	snprintf(which_vims, sizeof(which_vims), "Press a key to bind VIMS %03d",
			info->uc.selected_vims_entry );

	int n = prompt_keydialog(
			which_vims,
			"Key combination" );

	if( n == GTK_RESPONSE_ACCEPT )
	{
		int event_id = info->uc.selected_vims_entry;
		int key_val  = info->uc.pressed_key;
		int mod	     = info->uc.pressed_mod;
		char *buf    = info->uc.selected_vims_args;

		if( event_id > 0 && key_val > 0 )
		{
			multi_vims(
				VIMS_BUNDLE_ATTACH_KEY,
				"%d %d %d %s",
				event_id, key_val, mod, buf ? buf : "dummy" );
			info->uc.reload_hint[HINT_BUNDLES] = 1;
			char *strmod = sdlmod_by_id( mod );
			if(strmod)
				vj_msg(VEEJAY_MSG_INFO, "VIMS %d attached to key combination %s + %s",
				event_id, strmod, sdlkey_by_id( key_val));
			else
				vj_msg(VEEJAY_MSG_INFO, "VIMS %d attached to key %s",
					event_id, sdlkey_by_id(key_val) );
		}
	}
#endif
}


void	on_button_vimsupdate_clicked(GtkWidget *widget, gpointer user_data)
{
	if(count_textview_buffer( "vimsview" ) > 0 )
	{
		gchar *buf = get_textview_buffer( "vimsview" );

		if( info->uc.selected_vims_type == 0 )
		{
			multi_vims( VIMS_BUNDLE_ADD, "%d %s",
				info->uc.selected_vims_entry, buf );
		}
		else
		{
			multi_vims(
				VIMS_BUNDLE_ATTACH_KEY,
				"2 %d %d %s",
				info->uc.selected_vims_accel[1],
				info->uc.selected_vims_accel[0],
				info->uc.selected_vims_args );
		}
		info->uc.reload_hint[HINT_BUNDLES] = 1;
	}

}

void	on_vims_clear_clicked(GtkWidget *widget, gpointer user_data)
{
	clear_textview_buffer( "vimsview" );
}

void	on_vims_delete_clicked(GtkWidget *widget, gpointer user_data)
{
	if( info->uc.selected_vims_entry >= VIMS_BUNDLE_START &&
	    info->uc.selected_vims_entry < VIMS_BUNDLE_END )
	{
		multi_vims( VIMS_BUNDLE_DEL, "%d", info->uc.selected_vims_entry );
		info->uc.reload_hint[HINT_BUNDLES] = 1;
		vj_msg(VEEJAY_MSG_INFO, "Delete bundle %d from VIMS event list",
			info->uc.selected_vims_entry );
	}
	else
	{
		vj_msg(VEEJAY_MSG_ERROR, "VIMS %d is not a bundle.", info->uc.selected_vims_entry );
	}
}

void	on_button_saveactionfile_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_save_file( "Save Bundles", "veejay-vimsbundle.xml");
	if(filename)
	{
		multi_vims( VIMS_BUNDLE_SAVE, "%d %s",0, filename );
		vj_msg(VEEJAY_MSG_INFO, "Save Bundles and Keybindings to %s", filename );
		g_free(filename);
	}
}

void	on_button_loadconfigfile_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Load liveset / configfile",FILE_FILTER_XML);

	if(!filename)
		return;

	multi_vims( VIMS_BUNDLE_FILE, "%s", filename );

	g_free(filename);
}

void	on_button_saveconfigfile_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_save_file( "Save liveset / configfile", "veejay-liveset.xml");
	if(filename)
	{
		multi_vims( VIMS_BUNDLE_SAVE, "%d %s", 1, filename );
		g_free(filename);
	}
}


void	on_button_newbundle_clicked(GtkWidget *widget, gpointer user_data)
{
	if(count_textview_buffer( "vimsview" ) > 0 )
	{
		gchar *buf = get_textview_buffer( "vimsview" );
		multi_vims( VIMS_BUNDLE_ADD, "%d %s", 0, buf );
		info->uc.reload_hint[HINT_BUNDLES] = 1;
		vj_msg(VEEJAY_MSG_INFO, "If your VIMS document was valid,you'll find it in the list.");
	}
	else
	{
		vj_msg(VEEJAY_MSG_ERROR, "VIMS document is empty, type text first.");
	}
}

void	on_button_openactionfile_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Load Bundles", FILE_FILTER_XML);
	if(filename)
	{
		multi_vims( VIMS_BUNDLE_FILE, "%s", filename );
		info->uc.reload_hint[HINT_BUNDLES] = 1;
		vj_msg(VEEJAY_MSG_INFO ,"Tried to load '%s'",filename);
	    g_free(filename);
    }
}

static	void	load_server_files(char *buf, int len)
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "server_files");
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));
	GtkListStore *store = GTK_LIST_STORE(model);
#ifdef STRICT_CHECKING
	assert(tree != NULL );
#endif
	int i = 0;
	int idx = 0;
	char *ptr = buf;
	while( i < len ) {
		int filelen = 0;
		char name[1024];
		char header[5];
		memset(header,0,sizeof(header));
		memset(name, 0,sizeof(name));
		strncpy(header,ptr, 4 );
		if(sscanf(header,"%04d", &filelen)==1) {
			strncpy( name, ptr+4, filelen);
			gchar *filename = _utf8str( name );
			gtk_list_store_append(store, &iter);
			gtk_list_store_set(store, &iter, 0,filename,-1);
			gtk_tree_view_set_model(GTK_TREE_VIEW(tree),
					GTK_TREE_MODEL(store));
			idx ++;
			ptr += filelen;
		}
		ptr += 4;
		i+=4;
		i+=filelen;
	}
}

void	on_button_browse_clicked(GtkWidget *widget, gpointer user_data)
{



	single_vims(VIMS_WORKINGDIR);

	gint len = 0;
	gchar *test = recv_vims( 8, &len );

	if(!test || len <= 0 ) {
		return ;
	}

	reset_tree( "server_files");

	load_server_files( test,len);
free(test);
}

void on_button_offline_start_clicked(GtkWidget *widget, gpointer user_data)
{
	int stream_id = 0;
	int n_frames = get_nums("spin_offlineduration1");
	int autoplay = is_button_toggled("button_offline_autoplay1");

	if( info->selection_slot ) {
		stream_id = info->selection_slot->sample_type != 0 ? info->selection_slot->sample_id : 0;
	}
	else if (info->selected_slot ) {
		stream_id = info->selected_slot->sample_type != 0 ? info->selected_slot->sample_id : 0;
	}

	if( stream_id <= 0 ) {
		vj_msg(VEEJAY_MSG_INFO, "Select a stream slot for offline recording; samples are not valid here");
		return;
	}

	if( n_frames <= 0 ) {
		vj_msg(VEEJAY_MSG_WARNING, "Offline recorder needs a positive duration");
		return;
	}

	multi_vims( VIMS_STREAM_OFFLINE_REC_START, "%d %d %d %d", stream_id, n_frames, autoplay, -1);
	vj_msg(VEEJAY_MSG_INFO, "Started video-only offline recording: stream %d, %d frames", stream_id, n_frames );
}
void on_button_offline_stop_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_STREAM_OFFLINE_REC_STOP );
}

void	on_button_clipcopy_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->selection_slot )
	{
		if( info->selection_slot->sample_type != 0 ) {
			multi_vims( VIMS_STREAM_NEW_CLONE, "%d", info->selection_slot->sample_id );
		}
		else {
			multi_vims( VIMS_SAMPLE_COPY, "%d", info->selection_slot->sample_id );
		}
	}
	else if (info->selected_slot )
	{
		if( info->selected_slot->sample_type != 0 ) {
			multi_vims( VIMS_STREAM_NEW_CLONE, "%d", info->selected_slot->sample_id );
		}
		else {
			multi_vims( VIMS_SAMPLE_COPY, "%d", info->selected_slot->sample_id );
		}
	}
}

void	on_check_priout_fullscreen_clicked(
		GtkWidget *widget, gpointer user_data)
{
	gint on = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget) );
	multi_vims ( VIMS_FULLSCREEN, "%d", on );
}


void	on_inputstream_button_clicked(GtkWidget *widget, gpointer user_data)
{
	gint mcast = is_button_toggled( "inputstream_networktype" );
	gchar *remote_ = get_text( "inputstream_remote" );
	gint port = get_nums( "inputstream_portnum" );

	gsize bw = 0;
	gsize br = 0;

	if(!remote_)
	{
		error_dialog("Error", "Not a valid hostname. Try 'localhost' or '127.0.0.1'");
		GtkWidget *dialog = glade_xml_get_widget_( info->main_window, "inputstream_window" );
		gtk_widget_hide( dialog );
		return;
	}

	gchar *remote = g_locale_from_utf8(
			remote_ , -1, &br, &bw, NULL );

	if( !remote || strlen(remote) <= 1 )
	{
		GtkWidget *dialog = glade_xml_get_widget_( info->main_window, "inputstream_window" );
		gtk_widget_hide( dialog );
		error_dialog("Error", "Not a valid hostname. Try 'localhost' or '127.0.0.1'");
		return;
	}

	remote[strlen(remote)] = '\0';

	if(bw == 0 || br == 0 || port <= 0 )
	{
		GtkWidget *dialog = glade_xml_get_widget_( info->main_window, "inputstream_window" );
		gtk_widget_hide( dialog );
		error_dialog("Error", "You must enter a valid remote address and/or port number");
		return;
	}


	if(mcast)
		multi_vims( VIMS_STREAM_NEW_MCAST,"%d %s", port, remote );
	else
		multi_vims( VIMS_STREAM_NEW_UNICAST, "%d %s", port, remote );

	if(remote) g_free(remote);

	GtkWidget *dialog = glade_xml_get_widget_( info->main_window, "inputstream_window" );
	gtk_widget_hide( dialog );

}

void	on_inputstream_filebrowse_clicked(GtkWidget *w, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Select Action File",FILE_FILTER_XML );
	if(filename)
	{
		put_text( "inputstream_filename", filename );
		g_free(filename);
	}
}

void	on_y4m_new_clicked(GtkWidget *w, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Select YUV4MPEG input (fifo) file", FILE_FILTER_YUV);
	if(filename)
    {
        multi_vims( VIMS_STREAM_NEW_Y4M, "%s", filename );
        g_free(filename);
    }
}

void	on_samplerand_toggled(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		int arg = is_button_toggled( "freestyle" );
		int start = is_button_toggled( "samplerand" );

		int vims_id = VIMS_SAMPLE_RAND_START;
		if( start == 0 )
			vims_id = VIMS_SAMPLE_RAND_STOP;

		if( vims_id == VIMS_SAMPLE_RAND_START )
		{
			multi_vims( vims_id,"%d", arg );
			vj_midi_learning_vims_msg( info->midi,NULL, vims_id, arg );
		}
		else
		{
			single_vims( vims_id );
			vj_midi_learning_vims_simple(info->midi, NULL, vims_id );
		}

		vj_msg(VEEJAY_MSG_INFO, "You should restart the sample randomizer now.");

	}
}

void	on_sample_hold_button_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		int frames = get_nums("sample_hold_frames");

		if(frames < 0)
			frames = 0;

		multi_vims(VIMS_SAMPLE_HOLD_FRAME, "%d %d %d", 0, 0, frames);
		vj_midi_learning_vims_msg(info->midi, NULL, VIMS_SAMPLE_HOLD_FRAME, frames);
		vj_msg(VEEJAY_MSG_INFO, "Requested full output hold for %d frame%s",
		       frames, frames == 1 ? "" : "s");
	}
}


void on_openConnection_activate (GtkMenuItem     *menuitem,
                                 gpointer         user_data)
{
  if(!info->status_lock)
  {
    GtkWidget *veejay_connection_window = glade_xml_get_widget_(info->main_window, "veejay_connection");
    gtk_widget_show(veejay_connection_window);
    gtk_window_set_keep_above( GTK_WINDOW(veejay_connection_window), TRUE );

  }
}


void on_veejay_connection_close(GtkDialog *dialog, gpointer user_data)
{
    (void)dialog;
    (void)user_data;

    info->watch.state = STATE_QUIT;
}

gboolean on_veejay_connection_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    (void)event;

    on_veejay_connection_close(GTK_DIALOG(widget), user_data);
    return TRUE;
}


void on_video_settings_activate (GtkMenuItem     *menuitem,
                                 gpointer         user_data)
{
  if(!info->status_lock)
  {
    GtkWidget *veejay_settings_window = glade_xml_get_widget_(info->main_window, "video_options");
    GtkWidget *mainw = glade_xml_get_widget_(info->main_window,"gveejay_window" );
    gtk_window_set_transient_for (GTK_WINDOW(veejay_settings_window),GTK_WINDOW (mainw));
    gtk_window_set_keep_above( GTK_WINDOW(veejay_settings_window), TRUE );

    gtk_window_present(GTK_WINDOW(veejay_settings_window));
  }
}


void on_image_calibration_activate (GtkMenuItem    *menuitem,
                                    gpointer        data)
{
    GtkWidget *calibration_window = glade_xml_get_widget_(info->main_window,"calibration_window" );
    GtkWidget *mainw = glade_xml_get_widget_(info->main_window,"gveejay_window" );
    gtk_window_set_transient_for (GTK_WINDOW(calibration_window),GTK_WINDOW (mainw));
    gtk_window_set_keep_above( GTK_WINDOW(calibration_window), TRUE );

    gtk_window_present(GTK_WINDOW(calibration_window));

    cali_onoff = 1;
}


void on_video_options_close(GtkDialog *dialog, gpointer user_data)
{
    (void)dialog;
    (void)user_data;

    if(!info->status_lock)
    {
        GtkWidget *veejay_settings_window = glade_xml_get_widget_(info->main_window, "video_options");
        if(veejay_settings_window)
            gtk_widget_hide(veejay_settings_window);
    }
}

gboolean on_video_options_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    (void)event;

    on_video_options_close(GTK_DIALOG(widget), user_data);
    return TRUE;
}


void on_video_options_apply_clicked         (GtkButton       *button,
					     gpointer         user_data)
{
	gint width = get_nums( "priout_width" );
	gint height = get_nums( "priout_height" );
	gint x = get_nums("priout_x" );
	gint y = get_nums("priout_y" );

	if( width > 0 && height > 0 )
	{
		multi_vims( VIMS_RESIZE_SDL_SCREEN, "%d %d %d %d",
			width,height,x , y );
		vj_msg(VEEJAY_MSG_INFO, "Resize Video Window to %dx%d", width,height);
	}
}

void	on_cali_save_button_clicked( GtkButton *button, gpointer user_data)
{
    int stream_id = cali_selected_stream_id();

    if(stream_id <= 0) {
        error_dialog("Error", "No calibration stream selected");
        return;
    }

	gchar *filename = dialog_save_file( "Save calibration to file", "veejay-calibration.xml");
	if( filename ) {
		multi_vims( VIMS_V4L_CALI, "%d %s", stream_id, filename );
        g_free(filename);
	}
}

void	on_load_calibration_activate( GtkMenuItem     *menuitem,
					     gpointer         user_data)
{
	gchar	*filename = dialog_open_file("Select calibration file to load",FILE_FILTER_XML);
	if(filename)
	{
		multi_vims( VIMS_STREAM_NEW_CALI, "%s", filename );
		vj_msg(VEEJAY_MSG_INFO ,"Loaded calibration file %s",filename);
		g_free(filename);
	}
}

void	on_cali_take_button_clicked(	GtkButton *button, gpointer data )
{
	gint method = 0;
	gint kernel = 0;
    gint duration = 0;
    int stream_id = cali_selected_stream_id();

	if( info->uc.cali_stage == 1 )
		method = 1;

	if( info->uc.cali_duration > 0 ) {
		error_dialog( "Error", "Already taking calibration images");
		return;
	}

    if(stream_id <= 0) {
		error_dialog( "Error", "No source selected to calibrate. Play a Live stream or double click one in the List");
		return;
    }

    if(!cali_selected_stream_can_capture()) {
        error_dialog("Error", "Select a live V4L2 or vloopback stream for calibration capture");
        return;
    }

	if( is_button_toggled( "cali_method_median" ))
	{
		kernel = get_nums( "cali_kernelsize_spin");
	}
	duration = get_nums( "cali_duration_spin" );
    if(duration <= 0) {
        error_dialog("Error", "Calibration duration must be at least 1 frame");
        return;
    }

	multi_vims( VIMS_V4L_BLACKFRAME, "%d %d %d %d",
			stream_id,
			duration,
			kernel,
			method );

	info->uc.cali_duration = duration;
}

void	on_cali_darkframe_clicked( GtkButton *button, gpointer data )
{
    if(!cali_selected_stream_can_inspect()) {
        error_dialog("Error", "No calibration source selected");
        return;
    }
    cali_set_stream_preview_mode(1, 1);
	get_and_draw_frame( 0, "image_darkframe" );
}

void	on_cali_lightframe_clicked( GtkButton *button, gpointer data )
{
    if(!cali_selected_stream_can_inspect()) {
        error_dialog("Error", "No calibration source selected");
        return;
    }
    cali_set_stream_preview_mode(2, 1);
	get_and_draw_frame( 1, "image_lightframe" );
}

void	on_cali_flatframe_clicked( GtkButton *button, gpointer data )
{
    if(!cali_selected_stream_can_inspect()) {
        error_dialog("Error", "No calibration source selected");
        return;
    }
    cali_set_stream_preview_mode(3, 1);
	get_and_draw_frame( 2, "image_flatframe" );
}

void	on_cali_image_clicked( GtkButton *button, gpointer data )
{
    cali_set_stream_preview_mode(0, 1);
}

void	on_cali_reset_button_clicked( 	GtkButton *button, gpointer data )
{
    int stream_id = cali_selected_stream_id();

	if( stream_id <= 0 ) {
		error_dialog( "Error", "No source selected to calibrate. Play a Live stream or double click one in the List");

		return;
	}

	info->uc.cali_stage = 0;
	info->uc.cali_duration = 0;
	update_label_str("current_step_label","Step 1: cover the lens and take the dark frames.");

	multi_vims( VIMS_V4L_BLACKFRAME, "%d 0 0 0", stream_id );
    cali_set_stream_preview_mode(0, 1);

	reset_cali_images(0, "image_darkframe");
	reset_cali_images(1, "image_lightframe");
	reset_cali_images(2, "image_flatframe");

	GtkWidget *tb = glade_xml_get_widget_( info->main_window, "cali_take_button");
	gtk_button_set_label( GTK_BUTTON(tb), "Take Dark Frames");

    if(gtk_widget_is_sensitive(widget_cache[ WIDGET_CALI_SAVE_BUTTON ] ))
        gtk_widget_set_sensitive(widget_cache[ WIDGET_CALI_SAVE_BUTTON ], FALSE);
}


void on_vims_bundles_activate (GtkMenuItem     *menuitem,
                               gpointer         user_data)
{
    gtk_window_present(GTK_WINDOW(info->vims_bundle_dialog));
}



void on_vims_bundles_close(GtkDialog *dialog, gpointer user_data)
{
    (void)dialog;
    (void)user_data;

    if(info->vims_bundle_dialog)
        gtk_widget_hide(info->vims_bundle_dialog);
}

gboolean on_vims_bundles_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    (void)widget;
    (void)event;

    on_vims_bundles_close(NULL, user_data);
    return TRUE;
}


void on_quit_activate( GtkWidget *w, gpointer user_data )
{
  gveejay_quit(NULL,NULL);
}


void	on_open2_activate( GtkWidget *w, gpointer user_data)
{
	gchar *filename = NULL;
	switch( info->watch.state )
	{
		case STATE_STOPPED:
			filename = dialog_open_file( "Open Action file / Liveset",FILE_FILTER_XML );
			if(filename)
			{
				if(config_file)
				    g_free(config_file);
				config_file = g_strdup( filename );
				config_file_status = 1;
				g_free(filename);
			}
			break;
		case STATE_PLAYING:
			filename = dialog_open_file( "Open Samplelist ", FILE_FILTER_SL);
			if(filename)
			{
				single_vims( VIMS_SAMPLE_DEL_ALL );
				multi_vims( VIMS_SAMPLE_LOAD_SAMPLELIST, "%s", filename);
				g_free(filename);
			}
			break;
		default:
			vj_msg(VEEJAY_MSG_INFO, "Invalid state !");
			break;
	}
}

void	on_save1_activate( GtkWidget *w, gpointer user_data )
{
	if(info->watch.state == STATE_PLAYING)
		on_button_samplelist_save_clicked( NULL, NULL );
	else
		vj_msg(VEEJAY_MSG_ERROR, "Nothing to save (start or connect to veejay first)");
}


void on_about_activate(GtkWidget *widget, gpointer user_data)
{
  about_dialog();
}

void on_report_a_bug_activate(GtkWidget *w, gpointer user_data )
{
    reportbug();
}

void on_donate_activate( GtkWidget *w, gpointer user_data ) {
}

void	on_curve_togglerun_toggled(GtkWidget *widget , gpointer user_data)
{
}

void	on_stream_length_value_changed( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	int end_pos = (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON( widget ) );
    gfloat max_x;

    if(end_pos < 1)
        end_pos = 1;

    max_x = (gfloat)((end_pos > 1) ? (end_pos - 1) : 1);

	multi_vims( VIMS_STREAM_SET_LENGTH, "%d", end_pos );

    if(info->uc.selected_parameter_id < 0) {
        gtk3_curve_live_trace_clear(info->curve);
        gtk3_curve_set_range(info->curve, 0.0f, max_x, 0.0f, 100.0f);
        gtk3_curve_set_x_timeline(info->curve, 0.0f, max_x);
        gtk3_curve_set_x_view(info->curve, 0.0f, max_x);
    } else {
	    gtk3_curve_set_x_hi( info->curve, max_x );
    }
}

void on_stream_buffer_length_value_changed(GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    int n_frames = (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
    if(n_frames < 0)
        n_frames = 0;

    int stream_id = current_stream_id();
    if(info->status_tokens[PLAY_MODE] != MODE_STREAM || stream_id <= 0) {
        vj_msg(VEEJAY_MSG_WARNING, "Select a stream before changing the trickplay buffer length");
        return;
    }
    if(!current_stream_buffer_supported()) {
        current_stream_buffer_warn_not_ready();
        return;
    }

    multi_vims(VIMS_STREAM_SET_BUFFER_LENGTH, "%d %d", stream_id, n_frames);
    vj_midi_learning_vims_msg2(info->midi, "stream_buffer_length", VIMS_STREAM_SET_BUFFER_LENGTH, stream_id, n_frames);

    if(n_frames > 0)
        vj_msg(VEEJAY_MSG_INFO, "Requested %d-frame trickplay buffer for stream %d", n_frames, stream_id);
    else
        vj_msg(VEEJAY_MSG_INFO, "Requested trickplay buffer disable for stream %d", stream_id);
}

int	on_curve_buttontime_clicked(void)
{
	return 1;
}


void	on_framerate_inc_clicked( GtkWidget *w, gpointer data )
{
	double cur = get_slider_val( "framerate" );
	cur += 1.0;
	update_slider_value( "framerate", (int) cur, 0 );
}

void    on_framerate_dec_clicked( GtkWidget *w, gpointer data )
{
        double cur = get_slider_val( "framerate" );
        cur -= 1.0;
        update_slider_value( "framerate", (int) cur, 0 );
}


void	on_frameratenormal_clicked( GtkWidget *w, gpointer data )
{
    int fps_x100 = framerate_slider_value_to_ceil_x100((gdouble)info->el.fps);

    update_slider_gvalue("framerate", ((gdouble)fps_x100) / 100.0);
    vj_msg(VEEJAY_MSG_INFO, "Playback framerate reset to %d FPS", fps_x100 / 100);
}

void	on_framerate_value_changed( GtkWidget *w, gpointer data )
{
	if(info->status_lock)
		return;

    GtkAdjustment *a = gtk_range_get_adjustment(GTK_RANGE(w));
	gdouble slider_val = gtk_adjustment_get_value(a);
	int value = framerate_slider_value_to_ceil_x100(slider_val);
    gint64 now_us = g_get_monotonic_time();

    framerate_snap_widget_to_integer(w, value);

    if(value == framerate_last_sent_x100)
        return;

    if(framerate_last_sent_us == 0 ||
       (now_us - framerate_last_sent_us) >= FRAMERATE_SEND_MIN_US)
    {
        framerate_pending_x100 = -1;
        framerate_send_x100(value, 1);
        return;
    }

    framerate_pending_x100 = value;
    if(framerate_timeout_id == 0)
        framerate_timeout_id = g_timeout_add(
            (guint)((FRAMERATE_SEND_MIN_US + 999) / 1000),
            framerate_flush_pending_cb,
            NULL);
}

void	on_sync_correction_clicked( GtkWidget *w, gpointer data )
{
	int status = is_button_toggled( "sync_correction" );

	multi_vims( VIMS_SYNC_CORRECTION, "%d", status );
	vj_midi_learning_vims_msg( info->midi, NULL, VIMS_SYNC_CORRECTION, status );
}

static inline void put_le32(unsigned char *p, int v)
{
    uint32_t u = (uint32_t) v;

    p[0] = (unsigned char) (u & 0xff);
    p[1] = (unsigned char) ((u >> 8) & 0xff);
    p[2] = (unsigned char) ((u >> 16) & 0xff);
    p[3] = (unsigned char) ((u >> 24) & 0xff);
}

static inline int clamp_int_to_range(int v, int lo, int hi)
{
    if (lo > hi) {
        int t = lo;
        lo = hi;
        hi = t;
    }

    if (v < lo)
        return lo;
    if (v > hi)
        return hi;

    return v;
}

static Gtk3CurveType curve_selected_type(void)
{
    if (is_button_toggled("curve_typefreehand"))
        return GTK3_CURVE_TYPE_FREE;

    if (is_button_toggled("curve_typespline"))
        return GTK3_CURVE_TYPE_SPLINE;

    if (is_button_toggled("curve_typelinear"))
        return GTK3_CURVE_TYPE_LINEAR;

    if (info && info->curve && GTK3_IS_CURVE(info->curve))
        return gtk3_curve_get_curve_type(info->curve);

    return GTK3_CURVE_TYPE_LINEAR;
}

void on_curve_clear_parameter_clicked(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;

    if (info->status_lock)
        return;

    int entry = info->uc.selected_chain_entry;
    int param = info->uc.selected_parameter_id;
    int kf_entry;

    if (param < 0) {
        vj_msg(VEEJAY_MSG_INFO, "No FX parameter selected for animation");
        return;
    }

    kf_entry = (param == VJ_KF_PARAM_CHAIN_OPACITY) ? VJ_KF_ENTRY_CHAIN_FADE : entry;

    if (kf_entry < 0) {
        vj_msg(VEEJAY_MSG_INFO, "No FX entry selected for animation");
        return;
    }

    multi_vims(VIMS_SAMPLE_KF_CLEAR, "%d %d", kf_entry, param);
    curve_live_preview_user_override(FALSE);
    curve_editor_clear_local_dirty();
    info->uc.reload_hint[HINT_KF] = 1;
}

#define KF_STORE_HEADER_LEN 44
#define KF_STORE_HEADER_BUFSZ (KF_STORE_HEADER_LEN + 1)

#define KF_PACKED_HEADER_SCAN_FMT  "key%2d%2d%8d%8d%2d%8d%2d"
#define KF_STORE_HEADER_FMT        "K%08dkey%02d%02d%08d%08d%02d%08d%02d"

void on_curve_buttonstore_clicked(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;

    if (info->status_lock)
        return;

    const int entry = info->uc.selected_chain_entry;
    const int param = info->uc.selected_parameter_id;
    const int chain_opacity = (param == VJ_KF_PARAM_CHAIN_OPACITY);
    const int kf_entry = chain_opacity ? VJ_KF_ENTRY_CHAIN_FADE : entry;

    if (param < 0) {
        vj_msg(VEEJAY_MSG_INFO, "No parameter selected for animation");
        return;
    }

    if (kf_entry < 0) {
        vj_msg(VEEJAY_MSG_INFO, "No FX entry selected for animation");
        return;
    }

    const int fx_id = info->uc.entry_tokens[ENTRY_FXID];
    const int curve_fx_id = chain_opacity ? 0 : fx_id;

    if (!chain_opacity && fx_id <= 0) {
        vj_msg(VEEJAY_MSG_INFO, "No FX set on entry %d", entry);
        return;
    }

    int start = get_nums("curve_spinstart");
    int end   = get_nums("curve_spinend");
	int shape = gtk_combo_box_get_active(GTK_COMBO_BOX(widget_cache[WIDGET_CURVE_COMBO_ANIMATION]));

    if (start == end) {
        vj_msg(VEEJAY_MSG_ERROR,
               "Start and end position are the same, there is nothing to do");
        return;
    }

    if (end < start) {
        int t = start;
        start = end;
        end = t;
    }

    const int length = end - start + 1;

    if (length <= 0) {
        vj_msg(VEEJAY_MSG_INFO, "Length of animation is 0");
        return;
    }

    Gtk3CurveType type = curve_selected_type();
    int status = 1;

    int min = 0;
    int max = 0;
    curve_param_minmax(curve_fx_id, param, &min, &max);

    float *data = (float *) vj_calloc(sizeof(float) * length);
    if (!data) {
        vj_msg(VEEJAY_MSG_ERROR,
               "Unable to allocate animation buffer for %d points", length);
        return;
    }

    get_points_from_curve(info->curve, length, data);

    const int payload = 3 + 2 + 2 + 8 + 8 + 2 + 8 + 2 + (4 * length);
    const int tr_len = 9;
    const size_t bufsize = (size_t) tr_len + (size_t) payload;

    unsigned char *buf = (unsigned char *) vj_malloc(bufsize);
    if (!buf) {
        vj_msg(VEEJAY_MSG_ERROR,
               "Unable to allocate VIMS animation packet of %zu bytes", bufsize);
        free(data);
        return;
    }

	char header[KF_STORE_HEADER_BUFSZ];

	int hdr_len = snprintf(header,
                       sizeof(header),
                       KF_STORE_HEADER_FMT,
                       payload,
                       kf_entry,
                       param,
                       start,
                       end,
                       (int) type,
                       shape,
                       status);

	if (hdr_len != KF_STORE_HEADER_LEN) {
		veejay_msg(VEEJAY_MSG_ERROR,
				"[FX Anim] invalid keyframe store header length %d expected %d",
				hdr_len,
				KF_STORE_HEADER_LEN);
		return;
	}

    memcpy(buf, header, (size_t) hdr_len);

    unsigned char *ptr = buf + hdr_len;

    for (int k = 0; k < length; k++) {
        int pval = (int) data[k];
        pval = clamp_int_to_range(pval, min, max);

        put_le32(ptr, pval);
        ptr += 4;
    }

    vj_client_send_buf(info->client, V_CMD, buf, bufsize);
    multi_vims(VIMS_SAMPLE_KF_STATUS_PARAM,
               "0 %d %d %d",
               kf_entry,
               param,
               1);

    {
        int old_lock = info->status_lock;
        info->status_lock = 1;
        if(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM] &&
           GTK_IS_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM]))
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM]), TRUE);
        info->status_lock = old_lock;
    }

    update_slider_state(param, TRUE);
    curve_editor_clear_local_dirty();

    vj_msg(VEEJAY_MSG_INFO,
           "Saved and enabled animation for parameter %d on entry %d, start at frame %d and end at frame %d",
           param,
           kf_entry,
           start,
           end);

    curve_live_preview_user_override(TRUE);
    info->uc.reload_hint[HINT_KF] = 1;

    free(buf);
    free(data);
}

void on_curve_buttonclear_clicked(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;

    if (info->status_lock)
        return;

    int entry = info->uc.selected_chain_entry;

    if (entry < 0) {
        vj_msg(VEEJAY_MSG_INFO, "No FX entry selected for animation");
        return;
    }

    multi_vims(VIMS_SAMPLE_KF_RESET, "%d", entry);
    curve_live_preview_user_override(FALSE);
    info->uc.reload_hint[HINT_KF] = 1;
}

void update_curve_shape(void)
{
    if (!info->curve)
        return;

    int fx_id = info->uc.entry_tokens[ENTRY_FXID];
    int param = get_vj_kf_active_parameter();
    int chain_opacity = (param == VJ_KF_PARAM_CHAIN_OPACITY);
    int curve_fx_id = chain_opacity ? 0 : fx_id;

    if ((!chain_opacity && fx_id <= 0) || param < 0) {
		veejay_msg(0, "FX ID or Parameter ID not set %d %d",fx_id, param);
		return;
	}

    GtkWidget *shape_combo = widget_cache[ WIDGET_CURVE_ANIMATION_LIST ];
    GtkWidget *shape_param_spin = widget_cache[ WIDGET_CURVE_SPIN_ANIMATION_SHAPE ];
    GtkWidget *shape_seed_spin = widget_cache[ WIDGET_CURVE_SPIN_ANIMATION_SEED ];
    GtkWidget *shape_detail_spin = widget_cache[ WIDGET_CURVE_SPIN_ANIMATION_DETAIL ];
    GtkWidget *shape_param_reverse = widget_cache[ WIDGET_CURVE_TOGGLE_ANIMATION_SHAPE ];
    GtkWidget *shape_bound_min = widget_cache[ WIDGET_CURVE_BUTTON_BOUND_MIN ];
    GtkWidget *shape_bound_max = widget_cache[ WIDGET_CURVE_BUTTON_BOUND_MAX ];

    if (!shape_combo ||
        !shape_param_spin ||
        !shape_param_reverse ||
        !shape_bound_min ||
        !shape_bound_max)
    {
		veejay_msg(0, "Required widget not found");
        return;
    }

    gint selected_shape = gtk_combo_box_get_active(GTK_COMBO_BOX(shape_combo));

    if (selected_shape < 0)
        selected_shape = 0;

    int lo = 0;
    int hi = 0;

    if (info->status_tokens[PLAY_MODE] == MODE_SAMPLE) {
        lo = info->status_tokens[SAMPLE_START];
        hi = info->status_tokens[SAMPLE_END];
    } else {
        lo = 0;
        hi = info->status_tokens[SAMPLE_MARKER_END];
    }

    if (hi < lo) {
        int t = lo;
        lo = hi;
        hi = t;
    }

    if (hi <= lo) {
		veejay_msg(0,"End before start");
		return;
	}

    int steps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(shape_param_spin));
    if (steps < 1)
        steps = 1;

    int seed = 0;
    if (shape_seed_spin && GTK_IS_SPIN_BUTTON(shape_seed_spin)) {
        seed = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(shape_seed_spin));
        if (seed < 0)
            seed = 0;
    }

    int detail = 8;
    if (shape_detail_spin && GTK_IS_SPIN_BUTTON(shape_detail_spin)) {
        detail = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(shape_detail_spin));
        if (detail < 1)
            detail = 1;
    }

    gboolean reverse_shape =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shape_param_reverse));

    int minb = (int) gtk_scale_button_get_value(GTK_SCALE_BUTTON(shape_bound_min));
    int maxb = (int) gtk_scale_button_get_value(GTK_SCALE_BUTTON(shape_bound_max));

    if (maxb < minb) {
        int t = minb;
        minb = maxb;
        maxb = t;
    }

    if (widget_cache[WIDGET_CURVE_TYPEFREEHAND])
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TYPEFREEHAND]), TRUE);

    curve_live_preview_user_override(TRUE);

    curve_set_predefined_shape(info->curve,
                               curve_fx_id,
                               param,
                               lo,
                               hi,
                               selected_shape,
                               minb,
                               maxb,
                               steps,
                               seed,
                               detail,
                               reverse_shape,
							   info->el.fps);
    curve_editor_mark_local_dirty();
}

static gboolean curve_shape_seed_sensitive(int shape)
{
    switch(shape) {
        case FX_ANIM_SHAPE_NOISE:
        case FX_ANIM_SHAPE_SMOOTH_NOISE:
        case FX_ANIM_SHAPE_RANDOMWALK:
        case FX_ANIM_SHAPE_RANDOMWALK_BURST:
        case FX_ANIM_SHAPE_RANDOMWALK_INERTIA:
        case FX_ANIM_SHAPE_RANDOMWALK_MEAN:
        case FX_ANIM_SHAPE_RANDOMWALK_QUANTIZED:
        case FX_ANIM_SHAPE_RANDOMWALK_SMOOTH:
            return TRUE;
        default:
            return FALSE;
    }
}

static int curve_shuffle_seed_candidate(int current, guint64 salt, int attempt)
{
    guint32 x = (guint32) current;

    x ^= (guint32) salt;
    x ^= (guint32)(salt >> 32);
    x += 0x9e3779b9u * (guint32)(attempt + 1);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;

    int next = (int)(x % 1000000u);

    if(next == current)
        next = (current + 1 + attempt) % 1000000;

    return next;
}

static gboolean curve_current_animation_range(int *lo, int *hi)
{
    if(!lo || !hi)
        return FALSE;

    if(info->status_tokens[PLAY_MODE] == MODE_SAMPLE) {
        *lo = info->status_tokens[SAMPLE_START];
        *hi = info->status_tokens[SAMPLE_END];
    } else {
        *lo = 0;
        *hi = info->status_tokens[SAMPLE_MARKER_END];
    }

    if(*hi < *lo) {
        int t = *lo;
        *lo = *hi;
        *hi = t;
    }

    return (*hi > *lo);
}

static gboolean curve_vector_changed_visibly(const float *a, const float *b, int n)
{
    float max_delta = 0.0f;
    float sum_delta = 0.0f;
    int changed = 0;

    if(!a || !b || n <= 0)
        return TRUE;

    for(int i = 0; i < n; i++) {
        float d = fabsf(a[i] - b[i]);
        if(d > max_delta)
            max_delta = d;
        sum_delta += d;
        if(d > 0.05f)
            changed++;
    }

    return max_delta > 0.25f && changed > (n / 32) && (sum_delta / (float)n) > 0.03f;
}

static void curve_set_seed_guarded(GtkWidget *seed_spin, int seed)
{
    int old_lock = info->status_lock;

    info->status_lock = 1;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(seed_spin), (gdouble) seed);
    info->status_lock = old_lock;
}

void on_curve_button_animation_shuffle_clicked(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;

    if(info->status_lock)
        return;

    GtkWidget *seed_spin = widget_cache[ WIDGET_CURVE_SPIN_ANIMATION_SEED ];

    if(!seed_spin || !GTK_IS_SPIN_BUTTON(seed_spin)) {
        update_curve_shape();
        return;
    }

    GtkWidget *shape_combo = widget_cache[ WIDGET_CURVE_ANIMATION_LIST ];
    int selected_shape = shape_combo && GTK_IS_COMBO_BOX(shape_combo)
        ? gtk_combo_box_get_active(GTK_COMBO_BOX(shape_combo))
        : -1;

    int current = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(seed_spin));
    int lo = 0;
    int hi = 0;
    int len = 0;
    float *before = NULL;
    float *after = NULL;
    gboolean check_visual_change = FALSE;

    if(selected_shape >= 0 &&
       curve_shape_seed_sensitive(selected_shape) &&
       info->curve &&
       curve_current_animation_range(&lo, &hi))
    {
        len = hi - lo + 1;
        if(len > 1) {
            before = g_new0(float, len);
            after = g_new0(float, len);
            if(before && after) {
                gtk3_curve_get_vector(info->curve, len, before);
                check_visual_change = TRUE;
            }
        }
    }

    static guint64 shuffle_counter = 0;
    guint64 now = (guint64) g_get_monotonic_time();
    guint64 salt = now ^ (++shuffle_counter * 0x9e3779b97f4a7c15ULL);
    int accepted = current;

    for(int attempt = 0; attempt < 32; attempt++) {
        int next = curve_shuffle_seed_candidate(current, salt, attempt);

        curve_set_seed_guarded(seed_spin, next);
        update_curve_shape();
        accepted = next;

        if(!check_visual_change)
            break;

        gtk3_curve_get_vector(info->curve, len, after);

        if(curve_vector_changed_visibly(before, after, len))
            break;
    }

    if(check_visual_change) {
        gtk3_curve_get_vector(info->curve, len, after);
        if(!curve_vector_changed_visibly(before, after, len))
            vj_msg(VEEJAY_MSG_INFO, "Shuffle changed seed to %d, but the current random motif remained visually very close", accepted);
    }

    if(before)
        g_free(before);
    if(after)
        g_free(after);
}

void    on_curve_animation_changed (GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    update_curve_shape();
}

static void curve_set_type_from_toggle(GtkWidget *widget,
                                       const char *button_name,
                                       Gtk3CurveType type)
{
    (void) widget;

    if (info->status_lock || info->parameter_lock)
        return;

    if (!is_button_toggled(button_name))
        return;

    if (!info->selected_slot || !info->curve)
        return;

    set_points_in_curve(type, info->curve);
    curve_editor_mark_local_dirty();
}

void on_curve_typelinear_toggled(GtkWidget *widget, gpointer user_data)
{
    (void) user_data;
    curve_set_type_from_toggle(widget, "curve_typelinear", GTK3_CURVE_TYPE_LINEAR);
}

void on_curve_typespline_toggled(GtkWidget *widget, gpointer user_data)
{
    (void) user_data;
    curve_set_type_from_toggle(widget,"curve_typespline",GTK3_CURVE_TYPE_SPLINE);
}

void on_curve_typefreehand_toggled(GtkWidget *widget, gpointer user_data)
{
    (void) user_data;
    curve_set_type_from_toggle(widget,"curve_typefreehand", GTK3_CURVE_TYPE_FREE);
}

void on_curve_toggleentry_param_toggled(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;

    if (info->status_lock)
        return;

    int entry = info->uc.selected_chain_entry;
    int param = info->uc.selected_parameter_id;
    int kf_entry;

    if (param < 0) {
        vj_msg(VEEJAY_MSG_INFO, "No FX animation parameter selected");
        return;
    }

    kf_entry = (param == VJ_KF_PARAM_CHAIN_OPACITY) ? VJ_KF_ENTRY_CHAIN_FADE : entry;

    if (kf_entry < 0) {
        vj_msg(VEEJAY_MSG_INFO, "No FX entry selected for animation");
        return;
    }

    int active = is_button_toggled("curve_toggleentry_param") ? 1 : 0;

    multi_vims(VIMS_SAMPLE_KF_STATUS_PARAM,
               "0 %d %d %d",
               kf_entry,
               param,
               active);

    curve_live_preview_user_override(active ? TRUE : FALSE);
    update_slider_state(param, active);

    vj_msg(VEEJAY_MSG_INFO,
           "%s FX parameter %d",
           active ? "Enabled" : "Disabled",
           param);
}

void curve_toggleentry_activate(int selected_chain_entry, int active)
{
    if (selected_chain_entry < 0)
        return;

    int curve_type = (int) curve_selected_type();

    multi_vims(VIMS_SAMPLE_KF_STATUS,
               "%d %d %d",
               selected_chain_entry,
               active ? 1 : 0,
               curve_type);

    GtkTreeView *view =
        GTK_TREE_VIEW(glade_xml_get_widget_(info->main_window, "tree_chain"));

    if (!view)
        return;

    GtkTreeModel *model = gtk_tree_view_get_model(view);

    if (!model)
        return;

    GtkTreeIter iter;
    GtkTreePath *path = gtk_tree_path_new_from_indices(selected_chain_entry, -1);

    if (!path)
        return;

    if (gtk_tree_model_get_iter(model, &iter, path)) {
        GdkPixbuf *kf_toggle = update_pixmap_entry(active ? 1 : 0);

        gtk_list_store_set(GTK_LIST_STORE(model),
                           &iter,
                           FXC_KF,
                           kf_toggle,
                           FXC_KF_STATUS,
                           active ? 1 : 0,
                           -1);

        if (kf_toggle)
            g_object_unref(kf_toggle);
    }

    gtk_tree_path_free(path);
}

void curve_toggleentry_toggled(GtkWidget *widget, gpointer user_data)
{
    (void) user_data;

    if (info->status_lock)
        return;

    if (!GTK_IS_TOGGLE_BUTTON(widget))
        return;

    int entry = info->uc.selected_chain_entry;

    if (entry < 0) {
        vj_msg(VEEJAY_MSG_INFO, "No FX entry selected for animation");
        return;
    }

    int active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? 1 : 0;

    curve_toggleentry_activate(entry, active);
}

void curve_panel_toggleentry_toggled(GtkWidget *widget, gpointer user_data)
{
    curve_toggleentry_toggled(widget, user_data);

    GtkWidget *panel_toggleentry =
        GTK_WIDGET(glade_xml_get_widget_(info->main_window, "curve_panel_toggleentry"));

    GtkWidget *chain_toggleentry = widget_cache[WIDGET_CURVE_CHAIN_TOGGLEENTRY];

    toggle_siamese_widget(widget, panel_toggleentry, chain_toggleentry, 1);
}

static void
curve_show_live_beat_overview_now(void)
{
    vj_kf_refresh(TRUE);
}

void on_curve_fx_param_changed(GtkComboBox *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    gint active_kf_id = get_vj_kf_active_parameter();

    vj_kf_select_parameter(active_kf_id);

    if(!gtk_widget_is_sensitive(widget_cache[WIDGET_CURVECONTAINER]))
        gtk_widget_set_sensitive(widget_cache[WIDGET_CURVECONTAINER], TRUE);

    if(active_kf_id < 0) {
        reset_curve(info->curve);
        if(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM] &&
           gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM])))
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM]), FALSE);
        info->uc.reload_hint_checksums[HINT_KF] = -1;
        curve_show_live_beat_overview_now();
    } else {
        vj_kf_refresh(TRUE);
    }

    if(active_kf_id < 0 && widget_cache[WIDGET_CURVE_COMBO_ANIMATION]) {
        int osl = info->status_lock;
        info->status_lock = 1;

        gtk_combo_box_set_active(
            GTK_COMBO_BOX(widget_cache[WIDGET_CURVE_COMBO_ANIMATION]),
            0
        );

        info->status_lock = osl;
    }
}

void	on_samplepage_clicked(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *m = glade_xml_get_widget_(info->main_window , "notebook18");
	gtk_notebook_set_current_page( GTK_NOTEBOOK(m), 6 );

	GtkWidget *n = glade_xml_get_widget_( info->main_window, "panels" );

	gint page = gtk_notebook_get_current_page( GTK_NOTEBOOK(n) );

	gint page_needed = 2;

	switch( info->status_tokens[PLAY_MODE] )
	{
		case MODE_SAMPLE:
			page_needed =0 ; break;
		case MODE_STREAM:
			page_needed = 1; break;
		case MODE_PLAIN:
			page_needed = 2; break;
		default:
			break;
	}

	if( page_needed != page )
		gtk_notebook_set_current_page(
				GTK_NOTEBOOK(n),
				page_needed );
}

static gint timeline_marker_last_sent_sample = -1;
static gint timeline_marker_last_sent_center = -1;
static gint timeline_marker_last_sent_start  = -1;
static gint timeline_marker_last_sent_end    = -1;

static void timeline_marker_move_reset_cache(void)
{
    timeline_marker_last_sent_sample = -1;
    timeline_marker_last_sent_center = -1;
    timeline_marker_last_sent_start  = -1;
    timeline_marker_last_sent_end    = -1;
}

static gboolean timeline_marker_mode_is_sample(void)
{
    return info && info->tl && info->status_tokens[PLAY_MODE] == MODE_SAMPLE;
}

static gboolean timeline_marker_can_send(void)
{
    return timeline_marker_mode_is_sample() && !info->status_lock;
}

static void timeline_marker_disable_selection_guarded(GtkWidget *widget)
{
    GtkWidget *tl = widget ? widget : (info ? info->tl : NULL);
    int old_lock;

    if(!info || !tl)
        return;

    old_lock = info->status_lock;
    info->status_lock = 1;

    timeline_set_selection(tl, FALSE);
    timeline_clear_points(tl);

    info->selection[0] = -1;
    info->selection[1] = -1;

    info->status_lock = old_lock;
    timeline_marker_move_reset_cache();
}

static gboolean timeline_read_current_marker(gint *marker_start_out,
                                             gint *marker_end_out,
                                             gint *marker_center_backend_out)
{
    if (!timeline_marker_can_send())
        return FALSE;

    TimelineSelection *tl = TIMELINE_SELECTION(info->tl);

    if (!timeline_get_selection(tl))
        return FALSE;

    const gint sample_start = info->status_tokens[SAMPLE_START];
    const gint sample_end   = info->status_tokens[SAMPLE_END];
    const gint sample_len   = MAX(1, sample_end - sample_start + 1);

    gint marker_start = (gint) (timeline_get_in_point(tl) + 0.5);
    gint marker_end   = (gint) (timeline_get_out_point(tl) + 0.5);

    marker_start = CLAMP(marker_start, 0, sample_len - 1);
    marker_end   = CLAMP(marker_end,   0, sample_len - 1);

    if (marker_end < marker_start) {
        const gint tmp = marker_start;
        marker_start = marker_end;
        marker_end = tmp;
    }

    const gint marker_center_local =
        marker_start + ((marker_end - marker_start) / 2);

    const gint marker_center_backend =
        sample_start + marker_center_local;

    if (marker_start_out)
        *marker_start_out = marker_start;

    if (marker_end_out)
        *marker_end_out = marker_end;

    if (marker_center_backend_out)
        *marker_center_backend_out = marker_center_backend;

    return TRUE;
}

static gboolean timeline_send_marker_move_now(void)
{
    if (!timeline_marker_can_send())
        return FALSE;

    gint marker_start = 0;
    gint marker_end = 0;
    gint marker_center_backend = 0;

    if (!timeline_read_current_marker(&marker_start,
                                      &marker_end,
                                      &marker_center_backend))
    {
        return FALSE;
    }

    const gint sample_id = 0;

    if (timeline_marker_last_sent_sample == sample_id &&
        timeline_marker_last_sent_center == marker_center_backend &&
        timeline_marker_last_sent_start  == marker_start &&
        timeline_marker_last_sent_end    == marker_end)
    {
        return FALSE;
    }

    const gint sample_start = info->status_tokens[SAMPLE_START];

    info->selection[1] = sample_start + marker_start;
    info->selection[0] = sample_start + marker_end;

    const gint marker_start_backend = sample_start + marker_start;
    const gint marker_end_backend = sample_start + marker_end;

    multi_vims(VIMS_SAMPLE_SET_MARKER,
        "%d %d %d",
        sample_id,
        marker_start_backend,
        marker_end_backend);

    timeline_marker_last_sent_sample = sample_id;
    timeline_marker_last_sent_center = marker_center_backend;
    timeline_marker_last_sent_start  = marker_start;
    timeline_marker_last_sent_end    = marker_end;

    return TRUE;
}

void on_timeline_move_selection(void)
{
    if (!timeline_marker_can_send()) {
        if(info && !info->status_lock && !timeline_marker_mode_is_sample())
            timeline_marker_disable_selection_guarded(NULL);
        return;
    }

    TimelineSelection *tl = TIMELINE_SELECTION(info->tl);

    if (!timeline_get_bind(tl))
        return;

    if (!timeline_get_selection(tl))
        return;

    timeline_send_marker_move_now();
}

void on_timeline_selection_changed(GtkWidget *widget, gpointer user_data)
{
    (void) user_data;

    on_timeline_move_selection();
}

void on_timeline_audio_offset_changed(GtkWidget *widget, gpointer user_data)
{
    gint sample_id;
    gint source;
    gint profile;
    gint mode;
    gint video_anchor;
    gint local_video_anchor;
    gint wav_anchor_ms;
    gint sample_start;
    gint sample_end;
    gint old_lock;
    GtkWidget *spin;

    (void) user_data;

    if(!info || info->status_lock || !widget)
        return;

    if(info->status_tokens[PLAY_MODE] != MODE_SAMPLE || info->status_tokens[CURRENT_ID] <= 0)
        return;

    sample_id = info->status_tokens[CURRENT_ID];
    source = sample_audio_sync_source_from_ui();

    if(source != SAMPLE_AUDIO_SYNC_SOURCE_WAV)
        return;

    profile = audio_sync_profile_slot_from_combo("sample_audio_sync_profile_combo", 1);
    if(profile <= 0)
        profile = info->status_tokens[SAMPLE_AUDIO_SYNC_PROFILE];
    if(profile <= 0)
        return;

    mode = sample_audio_sync_mode_from_ui();

    sample_start = info->status_tokens[SAMPLE_START];
    sample_end = info->status_tokens[SAMPLE_END];
    local_video_anchor = timeline_get_audio_lane_video_anchor_frame(TIMELINE_SELECTION(widget));
    video_anchor = sample_start + local_video_anchor;
    if(video_anchor < sample_start)
        video_anchor = sample_start;
    if(video_anchor > sample_end)
        video_anchor = sample_end;

    wav_anchor_ms = timeline_get_audio_lane_wav_anchor_ms(TIMELINE_SELECTION(widget));
    if(wav_anchor_ms < 0)
        wav_anchor_ms = 0;

    spin = audio_sync_named_widget("sample_audio_sync_wav_anchor_ms");
    if(spin && GTK_IS_SPIN_BUTTON(spin)) {
        old_lock = info->status_lock;
        info->status_lock = 1;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), (gdouble) wav_anchor_ms);
        info->status_lock = old_lock;
    }

    multi_vims(VIMS_SAMPLE_AUDIO_SYNC_SET,
               "%d %d %d %d %d %d",
               sample_id,
               source,
               profile,
               mode,
               video_anchor,
               wav_anchor_ms);

    multi_vims(VIMS_SAMPLE_AUDIO_SYNC_REARM, "%d", sample_id);

    vj_msg(VEEJAY_MSG_INFO,
           "Sample %d WAV lane offset -> profile %d frame %d -> %dms (%s)",
           sample_id,
           profile,
           video_anchor,
           wav_anchor_ms,
           sample_audio_sync_mode_name(mode));
}


void on_timeline_cleared(GtkWidget *widget, gpointer user_data)
{
    (void) user_data;

    timeline_marker_move_reset_cache();

    if(!info || info->status_lock)
        return;

    if(!timeline_marker_mode_is_sample()) {
        timeline_marker_disable_selection_guarded(widget);
        return;
    }

    multi_vims(VIMS_SAMPLE_CLEAR_MARKER, "%d", 0);
    vj_midi_learning_vims_msg(info->midi, NULL, VIMS_SAMPLE_CLEAR_MARKER, 0);
}

void on_timeline_bind_toggled(GtkWidget *widget, gpointer user_data)
{
    (void) user_data;

    timeline_marker_move_reset_cache();

    if(info && !info->status_lock && !timeline_marker_mode_is_sample())
        timeline_marker_disable_selection_guarded(widget);
}
void	on_timeline_value_changed( GtkWidget *widget, gpointer user_data )
{
	if(!info->status_lock)
	{
		gdouble pos = timeline_get_pos( TIMELINE_SELECTION(widget) );
		multi_vims( VIMS_VIDEO_SET_FRAME, "%d", (gint)pos );
		vj_midi_learning_vims_msg2_extra(info->midi, VIMS_VIDEO_SET_FRAME, 0, 4 );
	}
}

static gint clamp_marker_frame(gint frame, gint nframes)
{
	if (nframes < 1)
		nframes = 1;

	if (frame < 0)
		return 0;

	if (frame >= nframes)
		return nframes - 1;

	return frame;
}

static void set_marker_span_centered(GtkWidget *tl, gint new_span)
{
	gdouble pos1 = timeline_get_in_point(TIMELINE_SELECTION(tl));
	gdouble pos2 = timeline_get_out_point(TIMELINE_SELECTION(tl));
	gdouble len  = timeline_get_length(TIMELINE_SELECTION(tl));

	gint nframes = (gint) llround(len);
	gint in_f;
	gint out_f;
	gint old_span;
	gint center2;
	gint new_in;
	gint new_out;

	if (nframes < 1)
		nframes = 1;

	in_f = clamp_marker_frame((gint) llround(pos1), nframes);
	out_f = clamp_marker_frame((gint) llround(pos2), nframes);

	if (out_f < in_f) {
		gint tmp = in_f;
		in_f = out_f;
		out_f = tmp;
	}

	old_span = out_f - in_f + 1;
	if (old_span < 1)
		old_span = 1;

	if (new_span < 1)
		new_span = 1;
	if (new_span > nframes)
		new_span = nframes;

	center2 = in_f + out_f;

	new_in = (center2 - new_span + 1) / 2;
	new_out = new_in + new_span - 1;

	if (new_in < 0) {
		new_in = 0;
		new_out = new_span - 1;
	}
	else if (new_out >= nframes) {
		new_out = nframes - 1;
		new_in = new_out - new_span + 1;
	}

	new_in = clamp_marker_frame(new_in, nframes);
	new_out = clamp_marker_frame(new_out, nframes);

	if (new_out < new_in)
		new_out = new_in;

	timeline_set_in_and_out_point(tl, (gdouble) new_in, (gdouble) new_out);
}

void	on_len_div_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!timeline_marker_can_send()) {
		if(info && !info->status_lock && !timeline_marker_mode_is_sample())
			timeline_marker_disable_selection_guarded(info ? info->tl : NULL);
		return;
	}

	gdouble pos1 = timeline_get_in_point(TIMELINE_SELECTION(info->tl));
	gdouble pos2 = timeline_get_out_point(TIMELINE_SELECTION(info->tl));
	gint in_f = (gint) llround(pos1);
	gint out_f = (gint) llround(pos2);
	gint span;

	if (out_f < in_f) {
		gint tmp = in_f;
		in_f = out_f;
		out_f = tmp;
	}

	span = out_f - in_f + 1;
	if (span < 1)
		span = 1;

	set_marker_span_centered(info->tl, (span + 1) / 2);

	multi_vims(VIMS_SAMPLE_SHRINK_MARKER, "%d", 0);

	vj_midi_learning_vims_msg(info->midi, NULL, VIMS_SAMPLE_SHRINK_MARKER, 0);
}

void	on_len_mul_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!timeline_marker_can_send()) {
		if(info && !info->status_lock && !timeline_marker_mode_is_sample())
			timeline_marker_disable_selection_guarded(info ? info->tl : NULL);
		return;
	}

	gdouble pos1 = timeline_get_in_point(TIMELINE_SELECTION(info->tl));
	gdouble pos2 = timeline_get_out_point(TIMELINE_SELECTION(info->tl));
	gint in_f = (gint) llround(pos1);
	gint out_f = (gint) llround(pos2);
	gint span;

	if (out_f < in_f) {
		gint tmp = in_f;
		in_f = out_f;
		out_f = tmp;
	}

	span = out_f - in_f + 1;
	if (span < 1)
		span = 1;

	set_marker_span_centered(info->tl, span * 2);

	multi_vims(VIMS_SAMPLE_GROW_MARKER, "%d", 0);

	vj_midi_learning_vims_msg(info->midi, NULL, VIMS_SAMPLE_GROW_MARKER, 0);
}

void on_timeline_out_point_changed(GtkWidget *widget, gpointer user_data)
{
    if(!info || info->status_lock)
        return;

    if(!timeline_marker_mode_is_sample()) {
        timeline_marker_disable_selection_guarded(widget);
        return;
    }

    const gint sample_start = info->status_tokens[SAMPLE_START];
    const gint sample_end   = info->status_tokens[SAMPLE_END];
    const gint sample_len   = MAX(1, sample_end - sample_start + 1);

    gint out_b = (gint)(timeline_get_out_point(TIMELINE_SELECTION(widget)) + 0.5);
    out_b = CLAMP(out_b, 1, sample_len);

    const gint abs_end = sample_start + out_b - 1;

    gint abs_start = info->status_tokens[SAMPLE_MARKER_START];

    if (abs_start < sample_start || abs_start > sample_end || abs_start > abs_end)
        abs_start = sample_start;

    multi_vims(VIMS_SAMPLE_SET_MARKER, "%d %d %d",
        0,
        abs_start,
        abs_end);

    info->selection[0] = abs_end;
    info->selection[1] = abs_start;

    vj_midi_learning_vims_msg2_extra(info->midi,
        VIMS_SAMPLE_SET_MARKER_END,
        0,
        4);
}

void on_timeline_in_point_changed(GtkWidget *widget, gpointer user_data)
{
    if(!info || info->status_lock)
        return;

    if(!timeline_marker_mode_is_sample()) {
        timeline_marker_disable_selection_guarded(widget);
        return;
    }

    const gint sample_start = info->status_tokens[SAMPLE_START];
    const gint sample_end   = info->status_tokens[SAMPLE_END];
    const gint sample_len   = MAX(1, sample_end - sample_start + 1);

    gint in_b = (gint)(timeline_get_in_point(TIMELINE_SELECTION(widget)) + 0.5);
    in_b = CLAMP(in_b, 0, sample_len - 1);

    const gint abs_start = sample_start + in_b;

    gint abs_end = info->status_tokens[SAMPLE_MARKER_END];

    if (abs_end <= 0 || abs_end < sample_start || abs_end > sample_end || abs_end < abs_start)
        abs_end = sample_end;

    multi_vims(VIMS_SAMPLE_SET_MARKER, "%d %d %d",
        0,
        abs_start,
        abs_end);

    info->selection[0] = abs_end;
    info->selection[1] = abs_start;

    vj_midi_learning_vims_msg2_extra(info->midi,
        VIMS_SAMPLE_SET_MARKER_START,
        0,
        4);
}

void	on_sampleadd_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Add videofile as new sample", FILE_FILTER_DEFAULT);
	if(filename)
	{
		multi_vims( VIMS_EDITLIST_ADD_SAMPLE, "%d %s", 0, filename );
		g_free(filename);
	}
}

void	on_streamnew_clicked(GtkWidget *widget, gpointer user_data)
{

    scan_devices( "tree_v4ldevices" );

    GtkWidget *inputstream_window = glade_xml_get_widget_(info->main_window, "inputstream_window");
    GtkWidget *mainw = glade_xml_get_widget_(info->main_window,"gveejay_window" );
    gtk_window_set_transient_for (GTK_WINDOW(inputstream_window),GTK_WINDOW (mainw));
    gtk_window_set_keep_above( GTK_WINDOW(inputstream_window), TRUE );

    gtk_window_present(GTK_WINDOW(inputstream_window));
}

void	on_generatornew_clicked(GtkWidget *widget, gpointer user_data)
{
    scan_generators( "generators" );

    GtkWidget *generator_window = glade_xml_get_widget_(info->main_window, "generator_window");
    GtkWidget *mainw = glade_xml_get_widget_(info->main_window,"gveejay_window" );
    gtk_window_set_transient_for (GTK_WINDOW(generator_window),GTK_WINDOW (mainw));
    gtk_window_set_keep_above( GTK_WINDOW(generator_window), TRUE );

    gtk_window_present(GTK_WINDOW(generator_window));
}

void	on_inputstream_close_clicked(GtkWidget *w,  gpointer user_data)
{
	GtkWidget *wid = glade_xml_get_widget_(info->main_window, "inputstream_window");
	gtk_widget_hide(wid);
}

void	on_generators_close_clicked(GtkWidget *w, gpointer user_data)
{
	GtkWidget *wid = glade_xml_get_widget_(info->main_window, "generator_window");
	gtk_widget_hide(wid);
}

void 	on_button_sdlclose_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_RESIZE_SDL_SCREEN, "%d %d %d %d",
			0,0,0,0 );

}


void	on_quicklaunch_clicked(GtkWidget *widget, gpointer user_data)
{


}

gboolean on_inputstream_window_delete_event(GtkWidget *w, GdkEvent *event, gpointer user_data)
{
    (void)w;
    (void)event;
    (void)user_data;

    GtkWidget *vs = glade_xml_get_widget_(info->main_window, "inputstream_window");
    if(vs)
        gtk_widget_hide(vs);

    return TRUE;
}

gboolean on_generator_window_delete_event(GtkWidget *w, GdkEvent *event, gpointer user_data)
{
    (void)w;
    (void)event;
    (void)user_data;

    GtkWidget *vs = glade_xml_get_widget_(info->main_window, "generator_window");
    if(vs)
        gtk_widget_hide(vs);

    return TRUE;
}

gboolean on_calibration_window_delete_event(GtkWidget *w, GdkEvent *event, gpointer data)
{
    (void)w;
    (void)event;
    (void)data;

    GtkWidget *win = glade_xml_get_widget_(info->main_window, "calibration_window");
    cali_onoff = 0;
    if(win)
        gtk_widget_hide(win);

    return TRUE;
}

void	on_quit_veejay1_activate( GtkWidget *w, gpointer user_data)
{
	veejay_quit();
}

void	on_curve_spinend_value_changed(GtkWidget *w, gpointer user_data)
{
	int end_pos = get_nums( "curve_spinend" );
	char *end_time = format_time(
			end_pos,info->el.fps );
	update_label_str( "curve_endtime", end_time );
	free(end_time);

    gtk3_curve_set_x_hi( info->curve, (gfloat) end_pos );

    vj_msg(VEEJAY_MSG_INFO, "Click the FX store button to save the new values");
}

void	on_curve_spinstart_value_changed(GtkWidget *w, gpointer user_data)
{
	int start_pos = get_nums( "curve_spinstart" );

	char *start_time = format_time(start_pos,info->el.fps );
	update_label_str( "curve_starttime", start_time );
	free(start_time);

    gtk3_curve_set_x_lo( info->curve, (gfloat) start_pos );

    vj_msg(VEEJAY_MSG_INFO, "Click the FX store button to save the new values");
}

gboolean on_veejayevent_enter_notify_event(GtkWidget *w, GdkEventCrossing *event, gpointer user_data)
{
    (void)w;
    (void)event;
    (void)user_data;

    info->key_now = TRUE;
    return FALSE;
}

gboolean on_veejayevent_leave_notify_event(GtkWidget *w, GdkEventCrossing *event, gpointer user_data)
{
    (void)w;
    (void)event;
    (void)user_data;

    info->key_now = FALSE;
    return FALSE;
}

void 	on_spin_framedelay_value_changed(GtkWidget *w, gpointer user_data)
{
	if( info->status_lock )
		return;

	multi_vims(VIMS_VIDEO_SET_SLOW, "%d", get_nums("spin_framedelay"));

	vj_midi_learning_vims_spin( info->midi, "spin_framedelay", VIMS_VIDEO_SET_SLOW );
}

void	on_alpha_effects_toggled(GtkWidget *w, gpointer user_data)
{
	 GtkWidget *n = glade_xml_get_widget_( info->main_window, "effectspanel" );
	 gint page = gtk_notebook_get_current_page( GTK_NOTEBOOK(n) );
     if(page != 2)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(n), 2);
}

void	on_toggle_alpha255_toggled(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_ALPHA_COMPOSITE,"%d %d", is_button_toggled( "alphacomposite"), is_button_toggled("toggle_alpha255") ? 255: 0 );
}

void	on_alphacomposite_toggled(GtkWidget *widget, gpointer user_data)
{
	int alpha_value = 0;
	if (is_button_toggled( "toggle_alpha255"))
		alpha_value = 255;

	multi_vims( VIMS_ALPHA_COMPOSITE,"%d %d", is_button_toggled( "alphacomposite" ), alpha_value );
}


void	on_mixing_effects_toggled(GtkWidget *w, gpointer user_data)
{
	 GtkWidget *n = glade_xml_get_widget_( info->main_window, "effectspanel" );
	 gint page = gtk_notebook_get_current_page( GTK_NOTEBOOK(n) );
	 if(page != 0 )
		 gtk_notebook_set_current_page(GTK_NOTEBOOK(n), 0 );
}

void	on_image_effects_toggled(GtkWidget *w, gpointer user_data)
{
	 GtkWidget *n = glade_xml_get_widget_( info->main_window, "effectspanel" );
	 gint page = gtk_notebook_get_current_page( GTK_NOTEBOOK(n) );
	 if(page != 1)
		 gtk_notebook_set_current_page(GTK_NOTEBOOK(n),1);
}

void	on_filter_effects_activate(GtkWidget *widget, gpointer user_data)
{
}

void	on_filter_effects_changed( GtkWidget *w, effectlist_data *user_data)
{
	if(user_data != NULL) {
		gchar* fx_txt = get_text("filter_effects");
		int filterlen = strlen(fx_txt);

		if(filterlen) {
			vj_msg(VEEJAY_MSG_INFO, "filtering effects '%s'", fx_txt);
			user_data->filter_string = g_new0(gchar, filterlen+1);
			if(user_data->filter_string != NULL) {
				strcpy(user_data->filter_string, fx_txt);
				gtk_tree_model_filter_refilter (user_data->stores[0].filtered);
				gtk_tree_model_filter_refilter (user_data->stores[1].filtered);
				gtk_tree_model_filter_refilter (user_data->stores[2].filtered);
				g_free(user_data->filter_string);
			}
			user_data->filter_string = NULL;
		} else {
			vj_msg(VEEJAY_MSG_DEBUG, "");

			gtk_tree_model_filter_refilter (user_data->stores[0].filtered);
			gtk_tree_model_filter_refilter (user_data->stores[1].filtered);
			gtk_tree_model_filter_refilter (user_data->stores[2].filtered);
		}
	}
}

void	on_console1_activate(GtkWidget *w, gpointer user_data)
{
	GtkWidget *n = glade_xml_get_widget_( info->main_window, "panels" );
	gint page = gtk_notebook_get_current_page( GTK_NOTEBOOK( n ) );

	if( page == MODE_PLAIN )
		gtk_notebook_set_current_page( GTK_NOTEBOOK(n),
				info->status_tokens[PLAY_MODE] );
	else
		gtk_notebook_set_current_page( GTK_NOTEBOOK(n),
				MODE_PLAIN );
}

gboolean	on_entry_hostname_focus_in_event( GtkWidget *w, gpointer user_data)
{
	update_label_str( "runlabel", "Connect");
	return FALSE;
}

gboolean	on_entry_hostname_focus_out_event( GtkWidget *w, gpointer user_data)
{

	return FALSE;
}


gboolean 	on_entry_filename_focus_in_event( GtkWidget *w, gpointer user_data)
{

	return FALSE;
}

void		on_previewbw_toggled( GtkWidget *w , gpointer user_data)
{
	single_vims( VIMS_PREVIEW_BW );
    vj_msg(VEEJAY_MSG_INFO,"Changed preview to greyscale");
}

int alphaonly_view = 0;
void		on_previewalphaonly_toggled( GtkWidget *w, gpointer user_data)
{
	alphaonly_view = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w) );
	vj_msg(VEEJAY_MSG_INFO, "Live viewing %s", (alphaonly_view ? "Alpha only" : "Preview" ) );
}

void		on_previewtoggle_toggled(GtkWidget *w, gpointer user_data)
{
    int enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    multitrack_toggle_preview( info->mt, -1, enabled,glade_xml_get_widget_(info->main_window, "imageA") );
    vj_msg(VEEJAY_MSG_INFO,"Live view is %s", (enabled ? "enabled" : "disabled" ));
    gveejay_preview(enabled);
}

void		on_previewspeed_value_changed( GtkWidget *widget, gpointer user_data)
{
}

void		on_previewscale_value_changed( GtkWidget *widget, gpointer user_data)
{
}

void		on_preview_width_value_changed( GtkWidget *w, gpointer user_data)
{
}
void		on_preview_height_value_changed( GtkWidget *w, gpointer user_data)
{
}

void	on_mt_new_activate( GtkWidget *w, gpointer user_data)
{
	multitrack_add_track( info->mt );
}

void	on_mt_delete_activate( GtkWidget *w, gpointer user_data)
{
	multitrack_close_track( info->mt );
}


void	on_mt_sync_start_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_start( info->mt );
}

void	on_mt_sync_stop_clicked( GtkWidget *w , gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_PLAY_STOP,0 );
}
void	on_mt_sync_play_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_PLAY_FORWARD,0 );
}
void	on_mt_sync_backward_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_PLAY_BACKWARD,0);
}
void	on_mt_sync_gotostart_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_GOTO_START,0 );
}
void	on_mt_sync_gotoend_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_GOTO_END,0 );
}
void	on_mt_sync_decspeed_clicked( GtkWidget *w, gpointer user_data)
{
	int n = info->status_tokens[SAMPLE_SPEED];
	if( n < 0 ) n += 1;
	if( n > 0 ) n -= 1;
	multitrack_sync_simple_cmd2( info->mt, VIMS_VIDEO_SET_SPEED, n );

}
void	on_mt_sync_incspeed_clicked( GtkWidget *w, gpointer user_data)
{
	int n = info->status_tokens[SAMPLE_SPEED];
	if( n < 0 ) n -= 1;
	if( n > 0 ) n += 1;
	multitrack_sync_simple_cmd2( info->mt, VIMS_VIDEO_SET_SPEED, n );
}
void	on_mt_sync_prev_clicked( GtkWidget *w , gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_PREV_FRAME ,0 );
}
void	on_mt_sync_next_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_SKIP_FRAME, 0 );
}

void	on_delete1_activate(GtkWidget *w, gpointer user_data)
{
}
void	on_new_source1_activate( GtkWidget *w , gpointer data )
{
}
void	on_add_file1_activate(GtkWidget *w, gpointer user_data)
{
}


gboolean on_effectchain_button_pressed (GtkWidget *tree, GdkEventButton *event, gpointer userdata)
{

    int state_modifier = event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK);

    if (state_modifier && (event->type == GDK_BUTTON_PRESS) && (event->button == 1))
    {
        GtkTreePath *path;
        GtkTreeViewColumn *column;
        gint cell_x, cell_y;

        if(gtk_tree_view_get_path_at_pos( GTK_TREE_VIEW( tree ),
                            (gint) event->x,
                            (gint) event->y,
                            &path, &column, &cell_x, &cell_y ))

        {

            GtkTreeIter iter;
            gint fxcid = 0;
            GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW( tree ));

            gtk_tree_model_get_iter(model, &iter, path);
            gtk_tree_model_get(model,&iter, FXC_ID, &fxcid, -1 );

            if (state_modifier & GDK_SHIFT_MASK)
			{
				multi_vims(VIMS_CHAIN_ENTRY_SET_STATE, "%d %d", 0, fxcid);
				info->uc.reload_hint[HINT_ENTRY] = 1;
				info->uc.reload_hint[HINT_CHAIN] = 1;
			}
			else if (state_modifier & GDK_CONTROL_MASK)
			{
			   int active = 0;
			   gtk_tree_model_get ( model, &iter, FXC_KF_STATUS, &active, -1);

			   multi_vims( VIMS_SAMPLE_KF_STATUS, "%d %d %d", 0, !active,0);
			   info->uc.reload_hint[HINT_ENTRY] = 1;
			   info->uc.reload_hint[HINT_CHAIN] = 1;
			}
        }
    }
    return FALSE;
}

static gchar *get_clipboard_fx_parameter_buffer(
        int *mixing_src,
        int *mixing_cha,
        int *enabled,
        int *beat_flag,
        int *fx_id)
{
    char rest[2048 + 22 + 1];
    int len = 0;
    int n_params = 0;
    int fid = 0;

    int is_video = 0;
    int kf_type = 0;
    int kf_status = 0;
    int transition_enabled = 0;
    int transition_loop = 0;
    int chain_source = 0;
    int chain_channel = 0;
    int video_on = 0;
    int beat_on = 0;
    int subrender_entry = 0;

    veejay_memset(rest, 0, sizeof(rest));

    multi_vims(VIMS_CHAIN_GET_ENTRY, "%d %d", 0, info->uc.selected_chain_entry);

    gchar *answer = recv_vims(3, &len);
    if (len <= 0 || answer == NULL) {
        gveejay_popup_err("Error", "Nothing in FX clipboard");
        if (answer)
            g_free(answer);
        return NULL;
    }

    int n = sscanf(answer,
        "%d %d %d %d %d %d %d %d %d %d %d %d %2048[0-9 ]",
        &fid,
        &is_video,
        &n_params,
        &kf_type,
        &kf_status,
        &transition_enabled,
        &transition_loop,
        &chain_source,
        &chain_channel,
        &video_on,
        &beat_on,
        &subrender_entry,
        rest
    );

    if (n != 13) {
        g_free(answer);
        return NULL;
    }

    *mixing_src = chain_source;
    *mixing_cha = chain_channel;
    *enabled = video_on;
    *beat_flag = beat_on;
    *fx_id = fid;

    g_free(answer);

    return strdup(rest);
}

typedef struct
{
	char *parameters;
	int	 fx_id;
	int	 src;
	int	 cha;
	int	 enabled;
	int beat_flag;
} clipboard_t;

static clipboard_t	*get_new_clipboard(void)
{
	clipboard_t *c = (clipboard_t*) vj_calloc( sizeof(clipboard_t) );

	c->parameters = get_clipboard_fx_parameter_buffer( &(c->src), &(c->cha), &(c->enabled),&(c->beat_flag), &(c->fx_id) );
	if( c->parameters == NULL ) {
		free(c);
		return NULL;
	}
	return c;
}

static void			del_clipboard(clipboard_t *c)
{
	if(c) {
		if(c->parameters)
			free(c->parameters);
		free(c);
	}
	c = NULL;
}

static clipboard_t *last_clipboard = NULL;

static void			do_clipboard(clipboard_t *c, int id, int entry_id)
{
	char msg[1024];
	snprintf( msg, sizeof(msg), "%03d:%d %d %d %d %s;",
			VIMS_CHAIN_ENTRY_SET_PRESET,
			id,
			entry_id,
			c->fx_id,
			c->enabled,
			c->parameters
			);

	msg_vims(msg);

	snprintf( msg, sizeof(msg), "%03d:%d %d;",
			( c->enabled ? VIMS_CHAIN_ENTRY_SET_VIDEO_ON : VIMS_CHAIN_ENTRY_SET_VIDEO_OFF ),
		    id,
			entry_id
			);
	msg_vims(msg);

	if( last_clipboard->cha > 0 ) {
		snprintf( msg, sizeof(msg), "%03d:%d %d %d %d;",
			VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
			id,
			entry_id,
			c->src,
			c->cha
			);
		msg_vims(msg);
	}
}

void	on_button_fx_cut_clicked( GtkWidget *w, gpointer user_data)
{
	if(last_clipboard)
		del_clipboard( last_clipboard );

	last_clipboard = get_new_clipboard();

	on_button_fx_del_clicked( NULL,NULL );
}

void	on_button_fx_paste_clicked( GtkWidget *w, gpointer user_data)
{
	sample_slot_t *s = info->selected_slot;

	if( last_clipboard == NULL ) {
		vj_msg(VEEJAY_MSG_INFO, "Nothing in FX clipboard");
		return;
	}

	if( s == NULL ) {
		vj_msg(VEEJAY_MSG_INFO, "No FX entry selected");
		return;
	}

	do_clipboard( last_clipboard, s->sample_id, info->uc.selected_chain_entry );

	info->uc.reload_hint[HINT_ENTRY]=1;
}

void	on_button_fx_copy_clicked(GtkWidget *w, gpointer user_data)
{
	if(last_clipboard)
		del_clipboard(last_clipboard);

	last_clipboard = get_new_clipboard();
}
void	on_copy1_activate( GtkWidget *w, gpointer user_data)
{
}
void	on_new_color1_activate(GtkWidget *w , gpointer user_data)
{
}
void	on_delete2_activate( GtkWidget *w, gpointer user_data)
{
}
void
on_spin_samplebank_select_value_changed
                                        (GtkSpinButton   *spinbutton,
                                        gpointer         user_data)
{
    (void)user_data;
    samplebank_goto_page(gtk_spin_button_get_value_as_int(spinbutton));
}
void
on_button_samplebank_prev_clicked      (GtkButton       *button,
                                        gpointer         user_data)
{
    (void)button;
    (void)user_data;
    samplebank_step_page(-1);
}


void
on_button_samplebank_next_clicked      (GtkButton       *button,
                                        gpointer         user_data)
{
    (void)button;
    (void)user_data;
    samplebank_step_page(1);
}

void
on_vims_messenger_rewind_clicked( GtkButton *togglebutton, gpointer user_data)
{
	info->vims_line = 0;
	vj_msg(VEEJAY_MSG_INFO, "Start from line 0 in vims messenger editor");
}

void
on_vims_messenger_clear_clicked( GtkButton *togglebutton, gpointer user_data)
{
	clear_textview_buffer( "vims_messenger_textview");
}

static void set_transition(int active, int shape, int length)
{
    if(info->status_lock)
        return;

    multi_vims(
            VIMS_SET_TRANSITION,
            "%d %d %d %d %d",
            info->status_tokens[PLAY_MODE],
            info->status_tokens[CURRENT_ID],
            active,
            shape,
            length );

}

void
on_transition_length_value_changed( GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    int length = (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );

    set_transition(
            info->status_tokens[ SAMPLE_TRANSITION_ACTIVE ],
            info->status_tokens[ SAMPLE_TRANSITION_SHAPE ],
            length
            );
    vj_msg(VEEJAY_MSG_INFO, "Transition length requested: %d frames", length);
}

void
on_transition_shape_value_changed( GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    int shape = (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );

    set_transition(
            info->status_tokens[ SAMPLE_TRANSITION_ACTIVE ],
            shape,
            info->status_tokens[ SAMPLE_TRANSITION_LENGTH ]
            );
    vj_msg(VEEJAY_MSG_INFO, "Transition shape requested: %d", shape);
}

void
on_transition_active_toggled( GtkWidget *widget, gpointer user_data)
{
    if(info->status_lock)
        return;

    int active = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget) ) ? 1 : 0;

    set_transition(
            active,
            info->status_tokens[ SAMPLE_TRANSITION_SHAPE ],
            info->status_tokens[ SAMPLE_TRANSITION_LENGTH ]
            );
    vj_msg(VEEJAY_MSG_INFO, "Transition %s requested for current source", active ? "enabled" : "disabled");
}

void
on_toggle_transitions_toggled( GtkWidget *widget, gpointer user_data )
{
	if(info->status_lock)
		return;

	int active = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget) );

	multi_vims( VIMS_TOGGLE_TRANSITIONS, "%d", active );
	vj_midi_learning_vims_toggle(info->midi, "toggle_transitions", VIMS_TOGGLE_TRANSITIONS);

	if(active) {
		vj_msg(VEEJAY_MSG_INFO, "Shape transition on sample switch enabled. Setup your transition in the properties panel");
	}
	else {
		vj_msg(VEEJAY_MSG_INFO, "Shape transition on sample switch disabled.");
	}

}

void
on_vims_messenger_single_clicked( void )
{
	GtkTextView *t= GTK_TEXT_VIEW(GTK_WIDGET(
				glade_xml_get_widget_(
					info->main_window,
					"vims_messenger_textview"))
			);

	GtkTextBuffer* buffer =  gtk_text_view_get_buffer(t);
  	int lc = gtk_text_buffer_get_line_count(buffer);

	if(info->vims_line > lc )
		info->vims_line = 0;

	while(info->vims_line < lc )
	{
		GtkTextIter start, end;
       		gtk_text_buffer_get_iter_at_line(buffer, &start, info->vims_line);

 	 	end = start;

	        gtk_text_iter_forward_sentence_end(&end);
       		gchar *str = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

		info->vims_line++;

		if(strlen(str) <= 0)
			continue;

	        if(str[0] != '+')
		{
	               	msg_vims( str );
       		 	vj_msg(VEEJAY_MSG_INFO, "Sent VIMS message '%s' (line %d)",str, info->vims_line-1 );
			break;
        	}
	}
}

void	on_osdbutton_clicked(GtkWidget *w, gpointer data )
{
	single_vims(VIMS_OSD);
}

extern void sequence_ui_set_play_grid_requested(int enabled);
void on_seqactive_toggled(GtkWidget *w, gpointer data)
{
    if(info->status_lock)
        return;

    int enabled = is_button_toggled("seqactive");

    sequence_ui_set_play_grid_requested(enabled);

    multi_vims(VIMS_SEQUENCE_STATUS, "%d", enabled);
    vj_midi_learning_vims_toggle(info->midi, "seqactive", VIMS_SEQUENCE_STATUS);

	if(enabled)
		info->uc.reload_hint[HINT_SEQ_ACT] = 1;

	vj_msg(VEEJAY_MSG_INFO,
           "Sample sequencer is %s",
           enabled ? "enabled" : "disabled");
}
void	on_hqbutton_clicked( GtkWidget *w, gpointer data )
{
	multitrack_set_quality( info->mt, 0 );
    vj_msg(VEEJAY_MSG_INFO, "Live view quality set to best");
}
void	on_lqbutton_clicked( GtkWidget *w, gpointer data )
{
	multitrack_set_quality( info->mt, 1 );
    vj_msg(VEEJAY_MSG_INFO, "Live view quality set to half resolution");
}
void	on_bq_button_clicked( GtkWidget *w, gpointer data )
{
	multitrack_set_quality( info->mt, 2 );
    vj_msg(VEEJAY_MSG_INFO, "Live view quality set to a quarter resolution");
}
void	on_uq_button_clicked( GtkWidget *w, gpointer data )
{
	multitrack_set_quality( info->mt, 3 );
    vj_msg(VEEJAY_MSG_INFO,"Live view quality set to an eighth resolution");
}

void    on_subrender_entry_toggle_toggled(GtkWidget *w, gpointer data)
{
    if(info->status_lock || info->parameter_lock)
		return;
    int enabled = is_button_toggled( "subrender_entry_toggle" );
    multi_vims( VIMS_SUB_RENDER_ENTRY,"%d %d %d", 0,-1,enabled);
    vj_midi_learning_vims_toggle3(info->midi, "subrender_entry_toggle", VIMS_SUB_RENDER_ENTRY, 0, -1);
    info->uc.reload_hint[HINT_ENTRY] = 1;
    info->uc.reload_hint[HINT_CHAIN] = 1;
    vj_msg(VEEJAY_MSG_INFO, "Sub rendering is %s", (enabled ? "enabled" : "disabled"));
}

void    on_transition_enabled_toggled(GtkWidget *w, gpointer data)
{
    if(info->status_lock || info->parameter_lock)
		return;
    int enabled = is_button_toggled( "transition_enabled" );
    int loop = get_nums("transition_loop");

    multi_vims( VIMS_SAMPLE_MIX_TRANSITION, "%d %d %d %d", 0, -1, enabled, loop );
    if(enabled) {
        vj_msg(VEEJAY_MSG_INFO, "Transitioning to mixing source at loop %d", loop);
    }
    else {
        vj_msg(VEEJAY_MSG_INFO, "Disabled transition to mixing source");
    }
}

void    on_transition_loop_value_changed(GtkWidget *w, gpointer data)
{
    if(info->status_lock || info->parameter_lock)
		return;

    int enabled = is_button_toggled( "transition_enabled" );
    int loop = get_nums("transition_loop");

    multi_vims( VIMS_SAMPLE_MIX_TRANSITION, "%d %d %d %d", 0, -1, enabled, loop );
}

void	on_macroplay_toggled( GtkWidget *w, gpointer data )
{
	if(info->status_lock)
		return;
	if( is_button_toggled( "macroplay" ) || is_button_toggled("macroplay1"))
	{
		multi_vims( VIMS_MACRO, "%d", 2 );
		vj_midi_learning_vims_msg( info->midi,NULL,VIMS_MACRO,2 );
		info->uc.reload_hint[HINT_MACRO] = 1;
        vj_msg( VEEJAY_MSG_INFO, "Started macro playback");
	}
}

void	on_macrorecord_toggled( GtkWidget *w, gpointer data  )
{
	if(info->status_lock)
		return;

    int delay = get_nums("spin_macrodelay");

	if( (is_button_toggled( "macrorecord") || is_button_toggled("macrorecord1")) && delay == 0)
	{
		multi_vims( VIMS_MACRO, "%d", 1 );
		vj_midi_learning_vims_msg( info->midi,NULL,VIMS_MACRO,1 );
		info->uc.reload_hint[HINT_MACRO] = 1;
        vj_msg(VEEJAY_MSG_INFO, "Started macro record");
	}
    else {
        info->uc.reload_hint[HINT_MACRODELAY] = delay;
        vj_msg(VEEJAY_MSG_INFO, "Delayed start of macro recording");
    }
}

void	on_macrostop_toggled( GtkWidget *w, gpointer data )
{
	if(info->status_lock)
		return;
	if( is_button_toggled( "macrostop") || is_button_toggled("macrostop1"))
	{
		multi_vims( VIMS_MACRO, "%d", 0 );
		vj_midi_learning_vims_msg( info->midi,NULL,VIMS_MACRO,0 );
		info->uc.reload_hint[HINT_MACRO] = 1;
        vj_msg(VEEJAY_MSG_INFO, "Stopped macro playback/record");
	}
}

void	on_macroclear_clicked( GtkWidget *w, gpointer data )
{
	if(info->status_lock)
		return;

	multi_vims( VIMS_MACRO, "%d", 3 );
	vj_midi_learning_vims_msg( info->midi,NULL, VIMS_MACRO, 3);
    info->uc.reload_hint[HINT_MACRO] = 1;
    vj_msg(VEEJAY_MSG_INFO, "Reset macro playback/record");
}


void	on_macro_loop_position_value_changed( GtkWidget *w, gpointer data)
{
	if(!info->status_lock)
		macro_line[2] = (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON(w) );
}

void	on_macro_dup_position_value_changed( GtkWidget *w, gpointer data )
{
	if(!info->status_lock)
		macro_line[1] = (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON(w) );
}

void	on_macro_frame_position_value_changed( GtkWidget *w, gpointer data)
{
	if(!info->status_lock)
		macro_line[0] = (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON(w) );
}

void	on_macro_button_clear_bank_clicked( GtkWidget *w, gpointer data)
{
	int num = get_nums("macro_bank_select");
	multi_vims(VIMS_CLEAR_MACRO_BANK, "%d", num);
        info->uc.reload_hint[HINT_MACRO] = 1;
	vj_msg(VEEJAY_MSG_INFO, "Macro bank %d cleared", num);
}

void 	on_macro_save_button_clicked( GtkWidget *w, gpointer data)
{
	macro_line[0] = get_nums("macro_frame_position");
	macro_line[1] = get_nums("macro_dup_position");
	macro_line[2] = get_nums("macro_loop_position");

	char *message = get_text("macro_vims_message");
	multi_vims( VIMS_PUT_MACRO, "%d %d %d %s", macro_line[0],macro_line[1],macro_line[2], message );
        info->uc.reload_hint[HINT_MACRO] = 1;
	vj_msg(VEEJAY_MSG_INFO, "Saved new event at frame %ld.%d, loop %d", macro_line[0],macro_line[1],macro_line[2]);
}

void	on_macro_delete_button_clicked( GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_DEL_MACRO,"%d %d %d %d", macro_line[0], macro_line[1], macro_line[2], macro_line[3] );
        info->uc.reload_hint[HINT_MACRO] = 1;
	vj_msg(VEEJAY_MSG_INFO, "Removed event at frame %ld.%d, loop %d #%d", macro_line[0],macro_line[1],macro_line[2],macro_line[3]);
}

void    on_macro_refresh_button_clicked( GtkWidget *w, gpointer data)
{
	info->uc.reload_hint[HINT_MACRO] = 1;
}

void    on_macro_delete_all_button_clicked( GtkWidget *w, gpointer data)
{
        multi_vims( VIMS_MACRO, "%d", 3 );
        info->uc.reload_hint[HINT_MACRO] = 1;
	vj_msg(VEEJAY_MSG_INFO, "Removed all events from all banks");
}

void    on_macro_bank_select_value_changed( GtkWidget *w, gpointer data)
{
	int bank = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));
        multi_vims( VIMS_MACRO_SELECT, "%d",bank);
        info->uc.reload_hint[HINT_MACRO] = 1;
	vj_msg(VEEJAY_MSG_INFO, "Selected macro bank %d",bank);
}

void	on_midilearn_toggled( GtkWidget *w, gpointer data )
{
    vj_midi_learn(info->midi,
                  gtk_check_menu_item_get_active( GTK_CHECK_MENU_ITEM(w) )
                 );
}

void	on_midievent_toggled( GtkWidget *w, gpointer data )
{
    int midi_play = gtk_check_menu_item_get_active( GTK_CHECK_MENU_ITEM(w));
    GtkWidget *midi_learn = glade_xml_get_widget_( info->main_window, "midi_learn");
    gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM(midi_learn), FALSE);
    gtk_widget_set_sensitive( midi_learn, !midi_play );
    vj_midi_play( info->midi, midi_play);
}

void	on_load_midi_layout_activate( GtkWidget *w , gpointer data )
{
	gchar *filename = dialog_open_file( "Select MIDI configuration file to load",FILE_FILTER_CFG);
	if( filename ) {
		vj_midi_load( info->midi, filename );
		g_free(filename);
	}
}
void	on_save_midi_layout_activate( GtkWidget *w, gpointer data )
{
	gchar *filename = dialog_save_file( "Save MIDI configuration to file", "veejay-midi.cfg");
	if(filename){
		vj_midi_save( info->midi, filename );
        g_free(filename);
    }
}

void on_clear_midi_layout_activate( GtkWidget *w, gpointer data )
{
	vj_midi_reset(info->midi);
}

void	on_button_vloop_stop_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VLOOPBACK_STOP );
}

void	on_button_vloop_start_clicked(GtkWidget *widget, gpointer user_data)
{
	multi_vims( VIMS_VLOOPBACK_START, "%d", get_nums( "spin_vloop" ) );
}

void on_toggle_vims_forwarding_toggled(GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;
	int is_active = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget));
	multi_vims( VIMS_MESSAGE_FORWARDING, "%d", is_active);
	vj_midi_learning_vims_toggle(info->midi, "toggle_vims_forwarding", VIMS_MESSAGE_FORWARDING);
}

void on_stream_loopstop_value_changed( GtkWidget *widget, gpointer user_data )
{
    if(info->status_lock)
        return;

    multi_vims( VIMS_SAMPLE_SET_LOOPS, "%d %d", 0, get_nums("stream_loopstop"));
}

void on_sample_loopstop_value_changed( GtkWidget *widget, gpointer user_data )
{
    if(info->status_lock)
        return;

    multi_vims( VIMS_SAMPLE_SET_LOOPS, "%d %d", 0, get_nums("sample_loopstop"));
}

void on_sample_panel_switch_page(GtkNotebook *notebook,
                                GtkWidget   *page,
                                guint        page_num,
                                gpointer     user_data)
{
    if( page_num == 1 ) {
        update_spin_value("button_el_selstart", info->selection[0] );
        update_spin_value("button_el_selend", info->selection[1] );
    }
}

void on_notebook18_switch_page (GtkNotebook *notebook,
                                GtkWidget   *page,
                                guint        page_num,
                                gpointer     user_data)
{
    if( page_num == 1 ) {
        vj_kf_refresh(TRUE);
    }
}

void on_button_offline_stop_sample_clicked(GtkWidget *widget, gpointer user_data)
{
    single_vims( VIMS_STREAM_OFFLINE_REC_STOP );
}

void on_button_offline_start_sample_clicked(GtkWidget *widget, gpointer user_data)
{
    int stream_id = (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON( widget_cache[ WIDGET_BUFFEREDSTREAMID ] ));
    int n_frames = (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON( widget_cache[ WIDGET_BUFFEREDSTREAMLENGTH ] ));
    int cur_id = info->status_tokens[ CURRENT_ID ];

    if(info->status_tokens[PLAY_MODE] != MODE_SAMPLE || cur_id <= 0) {
        vj_msg(VEEJAY_MSG_WARNING, "Select the sample that should receive the offline stream recording first");
        return;
    }

    if(stream_id <= 0) {
        vj_msg(VEEJAY_MSG_WARNING, "Offline sample recorder needs a valid source stream ID");
        return;
    }

    if(n_frames <= 0) {
        vj_msg(VEEJAY_MSG_WARNING, "Offline sample recorder needs a positive duration");
        return;
    }

    multi_vims( VIMS_STREAM_OFFLINE_REC_START, "%d %d %d %d", stream_id, n_frames, 0, cur_id );

    vj_msg(VEEJAY_MSG_INFO, "Signalled video-only offline recorder to loop-record %d frames from stream %d and append the recording to sample %d",
            n_frames, stream_id, cur_id );
}

void on_spin_bufferedstreamid_value_changed(GtkWidget *widget, gpointer user_data)
{
}

void on_spin_bufferedstreamlength_value_changed(GtkWidget *widget, gpointer user_data)
{
}

static int record_audio_source_enable_external_provider(void)
{
    int active = audio_input_selector_active_from_ui();
    int use_wav;
    int mode;

    if(active != AUDIO_MASTER_JACK && active != AUDIO_MASTER_WAV) {
        active = AUDIO_MASTER_JACK;
        audio_input_selector_set_guarded(active);
    }

    mode = audio_sync_mode_from_ui();
    if(mode == 0) {
        mode = VJ_AUDIO_SYNC_MODE_MONITOR;
        audio_sync_set_mode_combo_guarded(mode);
    }

    use_wav = (active == AUDIO_MASTER_WAV);
    if(use_wav && !audio_sync_mode_supports_wav(mode)) {
        use_wav = 0;
        audio_input_selector_set_guarded(AUDIO_MASTER_JACK);
    }

    audio_sync_set_master_wav_options_visible(use_wav);
    audio_sync_set_external_source_guarded(use_wav);

    if(use_wav && !audio_sync_wav_path_ready(1)) {
        audio_input_selector_set_guarded(AUDIO_MASTER_JACK);
        audio_sync_set_master_wav_options_visible(0);
        use_wav = 0;
        audio_sync_set_external_source_guarded(0);
    }

    multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", VJ_RECORD_AUDIO_SOURCE_BEAT_JACK);

    if(!audio_sync_send_selected_source())
        return 0;

    audio_sync_send_mode_settings_if_needed(mode);
    multi_vims(VIMS_AUDIO_SYNC_STATUS, "%d", 1);

    vj_msg(VEEJAY_MSG_INFO, "Recording audio from external %s provider (%s)",
           use_wav ? "WAV" : "JACK",
           audio_sync_mode_name(mode));
    return 1;
}

static void record_audio_source_changed(GtkWidget *widget, int source)
{
    if(info->status_lock)
        return;

    if(!widget || !GTK_IS_TOGGLE_BUTTON(widget))
        return;

    if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
        return;

    if(source == VJ_RECORD_AUDIO_SOURCE_BEAT_JACK) {
        record_audio_source_sync_groups(source);
        if(!record_audio_source_enable_external_provider()) {
            source = VJ_RECORD_AUDIO_SOURCE_ORIGINAL;
            audio_sync_set_mode_combo_guarded(0);
            audio_sync_set_enable_toggle_guarded(0);
            audio_sync_deactivate_playback();
            audio_sync_set_master_wav_options_visible(0);
            record_audio_source_sync_groups(source);
            multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", source);
            vj_msg(VEEJAY_MSG_WARNING, "External recorder provider was not available; restored Original audio");
        }
        return;
    }

    if(source == VJ_RECORD_AUDIO_SOURCE_ORIGINAL ||
       source == VJ_RECORD_AUDIO_SOURCE_SILENCE)
        audio_sync_remember_non_jack_master(source);

    audio_sync_set_mode_combo_guarded(0);
    audio_sync_set_enable_toggle_guarded(0);
    audio_sync_deactivate_playback();
    audio_sync_set_master_wav_options_visible(0);

    record_audio_source_sync_groups(source);

    multi_vims(VIMS_RECORD_AUDIO_SOURCE, "%d", source);
}

void on_record_audio_source_sample_auto_toggled(GtkWidget *widget, gpointer user_data)
{
    record_audio_source_changed(widget, VJ_RECORD_AUDIO_SOURCE_AUTO);
}

void on_record_audio_source_sample_original_toggled(GtkWidget *widget, gpointer user_data)
{
    record_audio_source_changed(widget, VJ_RECORD_AUDIO_SOURCE_ORIGINAL);
}

void on_record_audio_source_sample_beat_jack_toggled(GtkWidget *widget, gpointer user_data)
{
    record_audio_source_changed(widget, VJ_RECORD_AUDIO_SOURCE_BEAT_JACK);
}

void on_record_audio_source_stream_auto_toggled(GtkWidget *widget, gpointer user_data)
{
    record_audio_source_changed(widget, VJ_RECORD_AUDIO_SOURCE_AUTO);
}

void on_record_audio_source_stream_original_toggled(GtkWidget *widget, gpointer user_data)
{
    record_audio_source_changed(widget, VJ_RECORD_AUDIO_SOURCE_ORIGINAL);
}

void on_record_audio_source_stream_beat_jack_toggled(GtkWidget *widget, gpointer user_data)
{
    record_audio_source_changed(widget, VJ_RECORD_AUDIO_SOURCE_BEAT_JACK);
}


void on_record_audio_source_sample_silence_toggled(GtkWidget *widget, gpointer user_data)
{
    record_audio_source_changed(widget, VJ_RECORD_AUDIO_SOURCE_SILENCE);
}

void on_record_audio_source_stream_silence_toggled(GtkWidget *widget, gpointer user_data)
{
    record_audio_source_changed(widget, VJ_RECORD_AUDIO_SOURCE_SILENCE);
}
