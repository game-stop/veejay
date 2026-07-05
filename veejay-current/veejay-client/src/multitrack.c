/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2006 Niels Elburg <nwelburg@gmail.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/defs.h>
#include <veejaycore/vj-client.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vims.h>
#include <gtk/gtk.h>
#include <string.h>
#include <gdk/gdkx.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <veejaycore/libvevo.h>
#include <src/vj-api.h>
#include "sequence.h"
#include "tracksources.h"
#include "common.h"

#define SEQ_BUTTON_CLOSE 0
#define SEQ_BUTTON_RULE  1

#include <src/common.h>
#include <src/utils.h>
#include <src/gtktimeselection.h>
#include <src/vj-api.h>
#include <src/multitrack.h>

typedef struct
{
	GtkWidget *event_box;
	GtkWidget *frame;
	GtkWidget *main_vbox;
	GtkWidget *panel;
	GtkWidget *hbox;
	GtkWidget *area;
	GtkWidget *sub_frame;
	GtkWidget *sub_hbox;
	GtkWidget *toggle;
	GtkWidget *buttons[9];
	GtkWidget *icons[9];
	GtkWidget *button_box;
	GtkWidget *timeline_;
	GtkWidget *labels_[4];
	GtkWidget *sliders_[4];
	GtkWidget *button_box2;
	GtkWidget *buttons2[9];
	void *tracks;
	gint dim[2];
	int  num;
	int  status_lock;
	void *backlink;
	int status_cache[VJ_STATUS_ARRAY_SIZE];
	int history[4][VJ_STATUS_ARRAY_SIZE];
} sequence_view_t;

typedef struct
{
	sequence_view_t **view;
	void		 *preview;
	GtkWidget *main_window;
	GtkWidget *main_box;
	GtkWidget *status_bar;
	GtkWidget *scroll;
	void	  *data;
	int	  selected;
	int	  sensitive;
	float	  fps;
	float	  aspect_ratio;
	int	  width;
	int	  height;
	int	  master_track;
	GdkPixbuf	*logo;
	GtkWidget	*preview_toggle;
	int	  pw;
	int  	  ph;
    int   track_status[__MAX_TRACKS];
} multitracker_t;

static int	MAX_TRACKS = 4;
static void		*parent__ = NULL;

static char	*mt_new_connection_dialog(multitracker_t *mt, int *port_num, int *error);
static void	multitrack_set_preview_toggle_state(multitracker_t *mt, int track, int active);
static void	multitrack_update_track_label(multitracker_t *mt, int track, int current);
static void	add_buttons( sequence_view_t *p, sequence_view_t *seqv , GtkWidget *w);
static void	add_buttons2( sequence_view_t *p, sequence_view_t *seqv , GtkWidget *w);
static sequence_view_t *new_sequence_view( void *vp, int num );
static void	update_pos( void *data, gint total, gint current );
static gboolean seqv_mouse_press_event ( GtkWidget *w, GdkEventButton *event, gpointer user_data);

extern GdkPixbuf       *vj_gdk_pixbuf_scale_simple( GdkPixbuf *src, int dw, int dh, GdkInterpType inter_type );
extern void		gtk_widget_set_size_request__( GtkWidget *w, gint iw, gint h, const char *f, int line );
extern void    vj_msg(int type, const char format[], ...);

#define gtk_widget_set_size_request_(a,b,c) gtk_widget_set_size_request(a,b,c)

int mt_set_max_tracks(int mt)
{
	if( mt < 1 || mt > __MAX_TRACKS)
		return 0;
	MAX_TRACKS = mt;
	return 1;
}

int	mt_get_max_tracks(void)
{
	return  __MAX_TRACKS;
}

static const char *multitrack_default_host(multitracker_t *mt)
{
	const char *host = NULL;

	if(mt && mt->preview)
		host = gvr_track_get_hostname(mt->preview, 0);

	return (host && *host) ? host : "localhost";
}

static int multitrack_next_port_hint(multitracker_t *mt, const char *host)
{
	int base = DEFAULT_PORT_NUM;
	int current = 0;
	int p;

	if(mt && mt->preview) {
		current = gvr_track_get_portnum(mt->preview, 0);
		if(current > 0)
			base = current;
	}

	if(!mt || !mt->preview)
		return base;

	for(p = (current > 0 ? base + 1000 : base); p <= 65535; p += 1000)
		if(!gvr_track_already_open(mt->preview, host, p))
			return p;

	for(p = DEFAULT_PORT_NUM; p <= 65535; p += 1000)
		if(!gvr_track_already_open(mt->preview, host, p))
			return p;

	return base;
}

static void multitrack_set_preview_toggle_state(multitracker_t *mt, int track, int active)
{
	if(!mt || track < 0 || track >= MAX_TRACKS || !mt->view[track])
		return;

	mt->view[track]->status_lock = 1;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mt->view[track]->toggle), active ? TRUE : FALSE);
	gtk_button_set_label(GTK_BUTTON(mt->view[track]->toggle), active ? "Preview on" : "Preview off");
	mt->view[track]->status_lock = 0;
}
#define gtk_widget_set_sensitive_( w,p ) gtk_widget_set_sensitive(w,p)
#define gtk_image_set_from_pixbuf_(w,p) gtk_image_set_from_pixbuf(w,p)
static	void	status_print(multitracker_t *mt, const char format[], ... )
{
	char buf[1024];
	va_list args;
	va_start(args,format);
	vsnprintf( buf,sizeof(buf), format, args );
	gsize nr,nw;
	gchar *text = g_locale_to_utf8( buf, -1, &nr, &nw, NULL );
	if(text)
	{
		gsize len = strlen(text);
		if(len > 0 && text[len - 1] == '\n')
			text[len - 1] = '\0';

		if(mt && mt->status_bar)
		{
			if(GTK_IS_STATUSBAR(mt->status_bar))
				gtk_statusbar_push( GTK_STATUSBAR(mt->status_bar), 0, text);
			else if(GTK_IS_LABEL(mt->status_bar))
				gtk_label_set_text(GTK_LABEL(mt->status_bar), text);
		}
		g_free(text);
	}
	va_end(args);
}

static	GdkPixbuf	*load_logo_image(int dw, int dh )
{
	char path[1024];
	veejay_memset(path,0,sizeof(path));
	get_gd(path,NULL, "veejay-logo.png");
	return gdk_pixbuf_new_from_file( path,NULL );
}

void		multitrack_get_preview_dimensions( int w , int h, int *dst_w, int *dst_h )
{
	int tmp_w = w;
	int tmp_h = h;

	float ratio = (float)tmp_w / (float) tmp_h;

	if( tmp_h > MAX_PREVIEW_HEIGHT ) {
		tmp_h = MAX_PREVIEW_HEIGHT;
		tmp_w  = (int) ( (float) tmp_h * ratio );
	}
	if( tmp_w > MAX_PREVIEW_WIDTH ) {
		tmp_w = MAX_PREVIEW_WIDTH;
		tmp_h = tmp_w / ratio;
	}

	*dst_w = tmp_w;
	*dst_h = tmp_h;
}

static void	calculate_img_dimension(int w, int h, int *dst_w, int *dst_h, float *result, int max_w, int max_h, int quality)
{
	int tmp_w = w;
	int tmp_h = h;

	float ratio = (float)tmp_w / (float) tmp_h;
	*result = ratio;

	if( quality > 0 ) {
		int qdown = quality;
		while( (qdown > 0) ) {
			tmp_h = tmp_h / 2;
			tmp_w = tmp_w / 2;
			qdown--;
		}
	}

	if( tmp_h > max_h ) {
		tmp_h = max_h;
		tmp_w  = (int) ( (float) tmp_h * ratio );
	} else if( tmp_w > max_w ) {
		tmp_w = max_w;
		tmp_h = tmp_w / ratio;
	}

	*dst_w = tmp_w;
	*dst_h = tmp_h;
}


int		multitrack_get_sequence_view_id( void *data )
{
	sequence_view_t *s = (sequence_view_t*) data;
	return s->num;
}

void	multitrack_sync_start(void *data)
{
	multitracker_t *mt = (multitracker_t*)data;
	gvr_queue_vims( mt->preview,-1,VIMS_VIDEO_PLAY_STOP );
	gvr_queue_vims( mt->preview,-1,VIMS_VIDEO_GOTO_START );
	gvr_queue_vims( mt->preview,-1,VIMS_VIDEO_PLAY_FORWARD );
}

void	multitrack_sync_simple_cmd( void *data, int vims, int arg )
{
	multitracker_t *mt = (multitracker_t*)data;
	gvr_queue_vims(mt->preview,-1, vims);
}

void    multitrack_sync_simple_cmd2( void *data, int vims, int arg )
{
        multitracker_t *mt = (multitracker_t*)data;
        gvr_queue_mvims(mt->preview,-1, vims, arg);
}

static int seq_stream_buffer_supported_status(const int *status)
{
	return status &&
	       status[PLAY_MODE] == MODE_STREAM &&
	       status[CURRENT_ID] > 0 &&
	       status[STREAM_BUFFER_STATE] != STREAM_BUFFER_STATE_UNSUPPORTED;
}

static int seq_stream_buffer_ready_status(const int *status)
{
	return seq_stream_buffer_supported_status(status) &&
	       status[STREAM_BUFFER_ENABLED] > 0 &&
	       status[STREAM_BUFFER_FILLED] > 0;
}

static int seq_stream_buffer_ready(sequence_view_t *v)
{
	return v && seq_stream_buffer_ready_status(v->status_cache);
}

static int seq_stream_id(sequence_view_t *v)
{
	int stream_id = v ? v->status_cache[CURRENT_ID] : 0;
	return stream_id > 0 ? stream_id : 0;
}

static int seq_stream_buffer_length(sequence_view_t *v)
{
	int len = v ? v->status_cache[STREAM_BUFFER_FILLED] : 0;
	return len > 0 ? len : 1;
}

static int seq_stream_effective_speed(sequence_view_t *v)
{
	int speed = v ? v->status_cache[STREAM_BUFFER_SPEED] : 1;
	if(v && v->status_cache[STREAM_BUFFER_DIRECTION] < 0 && speed > 0)
		speed = -speed;
	return speed;
}

static int seq_stream_transport_length_status(const int *status)
{
	int len;

	if(seq_stream_buffer_ready_status(status)) {
		len = status[STREAM_BUFFER_FILLED];
		return len > 0 ? len : 1;
	}

	len = status[SAMPLE_MARKER_END];
	if(len <= 0)
		len = status[TOTAL_FRAMES];

	return len > 0 ? len : 1;
}

static int seq_stream_transport_position_status(const int *status)
{
	int len = seq_stream_transport_length_status(status);
	int pos = seq_stream_buffer_ready_status(status) ?
		status[STREAM_BUFFER_POSITION] :
		status[FRAME_NUM];

	if(pos < 0)
		pos = 0;
	else if(pos >= len)
		pos = len - 1;

	return pos;
}

static const char *seq_stream_buffer_state_name(int state)
{
	switch(state) {
		case STREAM_BUFFER_STATE_UNSUPPORTED: return "no buffer";
		case STREAM_BUFFER_STATE_OFF: return "buffer off";
		case STREAM_BUFFER_STATE_EMPTY: return "buffer empty";
		case STREAM_BUFFER_STATE_LIVE: return "buffer live";
		case STREAM_BUFFER_STATE_PLAYING: return "buffer play";
		case STREAM_BUFFER_STATE_PAUSED: return "buffer paused";
		default: return "buffer ?";
	}
}

static void seq_set_label_text(GtkWidget *label, const char *text)
{
	if(label && GTK_IS_LABEL(label))
		gtk_label_set_text(GTK_LABEL(label), text ? text : "");
}

static void seq_update_stream_status_label(sequence_view_t *v, const int *status)
{
	char text[96];
	int capacity = 0;
	int filled = 0;
	int pos = 0;
	int speed = 0;
	int direction = 0;

	if(!v || !status)
		return;

	capacity = status[STREAM_BUFFER_CAPACITY];
	filled = status[STREAM_BUFFER_FILLED];
	pos = status[STREAM_BUFFER_POSITION];
	speed = status[STREAM_BUFFER_SPEED];
	direction = status[STREAM_BUFFER_DIRECTION];

	if(!seq_stream_buffer_supported_status(status)) {
		seq_set_label_text(v->labels_[1], "live / no buffer");
		return;
	}

	if(capacity < 0)
		capacity = 0;
	if(filled < 0)
		filled = 0;
	if(pos < 0)
		pos = 0;
	if(filled > 0 && pos >= filled)
		pos = filled - 1;

	if(!status[STREAM_BUFFER_ENABLED] || filled <= 0) {
		if(capacity > 0)
			snprintf(text, sizeof(text), "%s %d",
				seq_stream_buffer_state_name(status[STREAM_BUFFER_STATE]), capacity);
		else
			snprintf(text, sizeof(text), "%s",
				seq_stream_buffer_state_name(status[STREAM_BUFFER_STATE]));
		seq_set_label_text(v->labels_[1], text);
		return;
	}

	if(direction < 0 && speed > 0)
		speed = -speed;

	snprintf(text, sizeof(text), "buf %d/%d x%d", pos + 1, filled, speed);
	seq_set_label_text(v->labels_[1], text);
}


static	void	seq_gotostart(GtkWidget *w, gpointer data )
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	if(seq_stream_buffer_ready(v))
		gvr_queue_mmvims(mt->preview, v->num, VIMS_STREAM_BUFFER_SET_FRAME, seq_stream_id(v), 0);
	else
		gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_GOTO_START );
}

static void seq_reverse(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	if(seq_stream_buffer_ready(v))
		gvr_queue_mvims(mt->preview, v->num, VIMS_STREAM_BUFFER_BACKWARD, seq_stream_id(v));
	else
		gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_PLAY_BACKWARD );
}

static void seq_pause(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	if(seq_stream_buffer_ready(v))
		gvr_queue_mvims(mt->preview, v->num, VIMS_STREAM_BUFFER_STOP, seq_stream_id(v));
	else
		gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_PLAY_STOP );
}

static void seq_play( GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	if(seq_stream_buffer_ready(v))
		gvr_queue_mvims(mt->preview, v->num, VIMS_STREAM_BUFFER_FORWARD, seq_stream_id(v));
	else
		gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_PLAY_FORWARD );
}

static void seq_gotoend(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	if(seq_stream_buffer_ready(v))
		gvr_queue_mmvims(mt->preview, v->num, VIMS_STREAM_BUFFER_SET_FRAME, seq_stream_id(v), -1);
	else
		gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_GOTO_END );
}

static	void	seq_speeddown(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;
	gint n = seq_stream_buffer_ready(v) ? seq_stream_effective_speed(v) : v->status_cache[SAMPLE_SPEED];

	if( n < 0 ) n += 1;
	if( n > 0 ) n -= 1;

	if(seq_stream_buffer_ready(v))
		gvr_queue_mmvims(mt->preview, v->num, VIMS_STREAM_BUFFER_SET_SPEED, seq_stream_id(v), n);
	else
		gvr_queue_mvims( mt->preview, v->num ,VIMS_VIDEO_SET_SPEED , n );
}

static	void	seq_speedup(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;
	gint n = seq_stream_buffer_ready(v) ? seq_stream_effective_speed(v) : v->status_cache[SAMPLE_SPEED];

	if( n < 0 ) n -= 1;
	if( n > 0 ) n += 1;

	if(seq_stream_buffer_ready(v))
		gvr_queue_mmvims(mt->preview, v->num, VIMS_STREAM_BUFFER_SET_SPEED, seq_stream_id(v), n);
	else
		gvr_queue_mvims( mt->preview, v->num ,VIMS_VIDEO_SET_SPEED , n );
}

static	void	seq_prevframe(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	if(seq_stream_buffer_ready(v))
		gvr_queue_mmvims(mt->preview, v->num, VIMS_STREAM_BUFFER_SKIP_FRAME, seq_stream_id(v), -1);
	else
		gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_PREV_FRAME );
}

static	void	seq_nextframe(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	if(seq_stream_buffer_ready(v))
		gvr_queue_mmvims(mt->preview, v->num, VIMS_STREAM_BUFFER_SKIP_FRAME, seq_stream_id(v), 1);
	else
		gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_SKIP_FRAME );
}

static	void	seq_speed( GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = v->backlink;
	if(v->status_lock)
		return;

  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gdouble value = gtk_adjustment_get_value (a);
	gint speed = (gint) value;

	if(seq_stream_buffer_ready(v))
		gvr_queue_mmvims(mt->preview, v->num, VIMS_STREAM_BUFFER_SET_SPEED, seq_stream_id(v), speed);
	else
		gvr_queue_mvims( mt->preview, v->num ,VIMS_VIDEO_SET_SPEED , speed );
}

static	void	seq_opacity( GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = v->backlink;

	if(v->status_lock)
		return;

  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
	gdouble value = gtk_adjustment_get_value (a);
	gint opacity = (gint)( value * 255.0);
	gvr_queue_mmvims( mt->preview, v->num ,VIMS_CHAIN_MANUAL_FADE, 0, opacity);
}


static	void	update_pos( void *user_data, gint total, gint current )
{
	sequence_view_t *v = (sequence_view_t*) user_data;
	multitracker_t *mt = v->backlink;
	int safe_total = total > 0 ? total : 1;

	if(current < 0)
		current = 0;
	else if(current >= safe_total)
		current = safe_total - 1;

  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( v->timeline_ ));
  gtk_adjustment_set_value (a, (gdouble) current / (gdouble) safe_total );

	char *now = format_time( current , mt->fps);
	seq_set_label_text(v->labels_[0], now);
	free(now);

	char *end = format_time( safe_total, mt->fps);
	seq_set_label_text(v->labels_[1], end);
	free(end);
}

static	void	update_speed( void *user_data, gint speed )
{
	sequence_view_t *v = (sequence_view_t*) user_data;

  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( v->sliders_[0] ));
  gtk_adjustment_set_value( a, (gdouble) speed );
}

#define FIRST_ROW_END 5
static struct
{
	const char *name;
	int vims_id;
	const char *file;
	void (*f)(GtkWidget *, gpointer);
} button_template_t[] =
{
	{ "button_gotostart", VIMS_VIDEO_GOTO_START, "button_gotostart.png", seq_gotostart },
	{ "button_reverse", VIMS_VIDEO_PLAY_BACKWARD, "button_reverse.png" , seq_reverse },
	{ "button_pauseplay", VIMS_VIDEO_PLAY_STOP, "button_pause.png", seq_pause},
	{ "button_play", VIMS_VIDEO_PLAY_FORWARD, "button_play.png", seq_play },
	{ "button_gotoend", VIMS_VIDEO_GOTO_END, "button_gotoend.png",seq_gotoend },

	{ "button_speeddown", VIMS_VIDEO_SET_SPEED, "button_down.png", seq_speeddown },
	{ "button_speedup", VIMS_VIDEO_SET_SPEED, "button_up.png", seq_speedup },
	{ "button_prevframe", VIMS_VIDEO_PREV_FRAME, "button_prev.png", seq_prevframe },
	{ "button_nextframe", VIMS_VIDEO_SKIP_FRAME, "button_skip.png", seq_nextframe },
	{ NULL	,	 0 , NULL },
};

static	void	add_buttons( sequence_view_t *p, sequence_view_t *seqv , GtkWidget *w)
{
	int i;
	for( i = 0; i < FIRST_ROW_END;i ++ )
	{
		char path[1024];
		veejay_memset(path,0,sizeof(path));
		get_gd(path,NULL, button_template_t[i].file );
		seqv->icons[i] = gtk_image_new_from_file( path );
		seqv->buttons[i] = gtk_button_new();
		//gtk_widget_set_size_request_( seqv->icons[i],24,20 );
		gtk_button_set_image( GTK_BUTTON(seqv->buttons[i]), seqv->icons[i] );
		//gtk_widget_set_size_request_( seqv->buttons[i],24,20 );
		gtk_box_pack_start( GTK_BOX(w), seqv->buttons[i], TRUE,TRUE, 0 );
		g_signal_connect( G_OBJECT( seqv->buttons[i] ), "clicked", G_CALLBACK( button_template_t[i].f),
				(gpointer)p );
		gtk_widget_show( seqv->buttons[i] );

	}
}

static	void	add_buttons2( sequence_view_t *p, sequence_view_t *seqv , GtkWidget *w)
{
	int i;
	for( i = FIRST_ROW_END; button_template_t[i].name != NULL ;i ++ )
	{
		char path[1024];
		veejay_memset(path,0,sizeof(path));
		get_gd(path,NULL, button_template_t[i].file );
		seqv->icons[i] = gtk_image_new_from_file( path );
		seqv->buttons2[i] = gtk_button_new();
		gtk_widget_set_size_request_( seqv->icons[i],24,20 );

		gtk_button_set_image( GTK_BUTTON(seqv->buttons2[i]), seqv->icons[i] );
		gtk_widget_set_size_request_( seqv->buttons2[i],24,20 );
		gtk_box_pack_start( GTK_BOX(w), seqv->buttons2[i], TRUE,TRUE, 0 );
		g_signal_connect( G_OBJECT( seqv->buttons2[i] ), "clicked", G_CALLBACK( button_template_t[i].f),
				(gpointer*)p );
		gtk_widget_show( seqv->buttons2[i] );

	}
}



static void set_first_row_sensitive(sequence_view_t *p, int sensitive)
{
	int i;
	for(i = 0; i < FIRST_ROW_END; i++)
		gtk_widget_set_sensitive_(GTK_WIDGET(p->buttons[i]), sensitive);
}

static void set_stream_button_row_sensitive(sequence_view_t *p, const int *status)
{
	int ready = seq_stream_buffer_ready_status(status);

	gtk_widget_set_sensitive_(GTK_WIDGET(p->button_box2), ready);
	gtk_widget_set_sensitive_(GTK_WIDGET(p->button_box), ready);
	gtk_widget_set_sensitive_(GTK_WIDGET(p->sliders_[0]), ready);
	gtk_widget_set_sensitive_(GTK_WIDGET(p->timeline_), ready);
	gtk_widget_set_sensitive_(GTK_WIDGET(p->sliders_[1]), TRUE);
	set_first_row_sensitive(p, ready);
}

static int stream_button_sensitivity_changed(const int *old_status, const int *new_status)
{
	return old_status[PLAY_MODE] != new_status[PLAY_MODE] ||
	       old_status[CURRENT_ID] != new_status[CURRENT_ID] ||
	       old_status[STREAM_BUFFER_STATE] != new_status[STREAM_BUFFER_STATE] ||
	       old_status[STREAM_BUFFER_ENABLED] != new_status[STREAM_BUFFER_ENABLED] ||
	       old_status[STREAM_BUFFER_FILLED] != new_status[STREAM_BUFFER_FILLED];
}

static	void	playmode_sensitivity(sequence_view_t *p, const int *status)
{
	int pm = status[PLAY_MODE];

	if( pm == MODE_STREAM || pm == MODE_PLAIN || pm == MODE_SAMPLE )
	{
		if(p->num > 0)
		    gtk_widget_set_sensitive_( GTK_WIDGET( p->toggle ), TRUE );
		gtk_widget_set_sensitive_( GTK_WIDGET( p->panel ), TRUE );
	}

	if( pm == MODE_STREAM )
	{
		set_stream_button_row_sensitive(p, status);
	}
	else
	{
		if( pm == MODE_SAMPLE || pm == MODE_PLAIN )
		{
			gtk_widget_set_sensitive_( GTK_WIDGET( p->button_box2 ), TRUE );
			gtk_widget_set_sensitive_( GTK_WIDGET( p->button_box ), TRUE );
			gtk_widget_set_sensitive_( GTK_WIDGET( p->sliders_[0] ), TRUE );
			gtk_widget_set_sensitive_( GTK_WIDGET( p->timeline_ ), TRUE );
			set_first_row_sensitive(p, TRUE);
		}
		if( pm == MODE_SAMPLE )
			gtk_widget_set_sensitive_( GTK_WIDGET( p->sliders_[1] ), TRUE );
		else
			gtk_widget_set_sensitive_( GTK_WIDGET( p->sliders_[1] ), FALSE );
	}
}


static	void	update_widgets(int *status, sequence_view_t *p, int pm)
{
	multitracker_t *mt = (multitracker_t*) p->backlink;
	int *h = p->history[pm];
	if(stream_button_sensitivity_changed(h, status))
		playmode_sensitivity(p, status);

	if( pm == MODE_STREAM )
	{
		update_pos(p,
			seq_stream_transport_length_status(status),
			seq_stream_transport_position_status(status));

		if(seq_stream_buffer_ready_status(status)) {
			int speed = status[STREAM_BUFFER_SPEED];
			if(status[STREAM_BUFFER_DIRECTION] < 0 && speed > 0)
				speed = -speed;
			update_speed(p, speed);
		}
		else {
			update_speed( p, 1 );
		}
		seq_update_stream_status_label(p, status);
	}
	else
	if( pm == MODE_SAMPLE || pm == MODE_PLAIN )
	{
		if( h[FRAME_NUM] != status[FRAME_NUM] )
			update_pos( p, status[TOTAL_FRAMES],status[FRAME_NUM] );
		if( h[SAMPLE_SPEED] != status[SAMPLE_SPEED] )
			update_speed( p, status[SAMPLE_SPEED] );
	}

	if( h[TOTAL_SLOTS] != status[TOTAL_SLOTS])
	{
		gvr_need_track_list( mt->preview, p->num );
		update_track_view( MAX_TRACKS, get_track_tree( p->tracks ), (void*)p );
	}
}


int		update_multitrack_widgets( void *data, int *array, int track )
{
	multitracker_t *mt = (multitracker_t*) data;
	sequence_view_t *p = mt->view[ track ];

	p->status_lock = 1;
	int pm = array[PLAY_MODE];
	int i;
	for( i  =  0; i < VJ_STATUS_ARRAY_SIZE; i ++ )
		p->status_cache[i] = array[i];
	update_widgets(array, p, pm);

	int *his = p->history[ pm ];
	for( i  =  0; i < VJ_STATUS_ARRAY_SIZE; i ++ )
		his[i] = array[i];
	p->status_lock = 0;
	return 1;
}

static	void	sequence_preview_size(multitracker_t *mt, int track_num)
{
	float ratio = 0.0f;
	int tmp_w = 0;
	int tmp_h = 0;

	calculate_img_dimension(mt->width,mt->height, &tmp_w, &tmp_h, &ratio, 160, 120,0);
	if(!gvr_track_configure( mt->preview, track_num,tmp_w,tmp_h ) )
	{
		veejay_msg(0, "Unable to configure preview %dx%d",tmp_w,tmp_h );
	}

}

static void sequence_preview_cb(GtkWidget *widget, gpointer user_data)
{
	sequence_view_t *v = (sequence_view_t*) user_data;
	multitracker_t *mt = v->backlink;

	if(v->status_lock)
		return;

    int status = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget) );

	if(!gvr_track_toggle_preview( mt->preview, v->num,status )) {
		multitrack_set_preview_toggle_state(mt, v->num, gvr_get_preview_status(mt->preview, v->num));
		return;
	}

	multitrack_set_preview_toggle_state(mt, v->num, status);
	sequence_preview_size( mt, v->num );

	if( !status )
		gtk_image_clear( GTK_IMAGE(v->area ) );
	
}

static	void	sequence_set_current_frame(GtkWidget *w, gpointer user_data)
{

	sequence_view_t *v = (sequence_view_t*) user_data;
	multitracker_t *mt = v->backlink;
	if(v->status_lock)
		return;

    GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
    gdouble pos = gtk_adjustment_get_value (a);
	gint frame;

	if(seq_stream_buffer_ready(v)) {
		int len = seq_stream_buffer_length(v);
		frame = pos * len;
		if(frame >= len)
			frame = len - 1;
		if(frame < 0)
			frame = 0;
		gvr_queue_mmvims(mt->preview, v->num, VIMS_STREAM_BUFFER_SET_FRAME, seq_stream_id(v), frame);
		return;
	}

	frame = pos * v->status_cache[TOTAL_FRAMES];
	gvr_queue_mvims( mt->preview, v->num, VIMS_VIDEO_SET_FRAME, frame );
}

static sequence_view_t *new_sequence_view( void *vp, int num )
{
    char track_title[50];
	sequence_view_t *seqv = (sequence_view_t*) vj_calloc(sizeof(sequence_view_t));

	seqv->num = num;
	seqv->backlink = vp;

	seqv->event_box = gtk_event_box_new();
	gtk_event_box_set_visible_window( GTK_EVENT_BOX(seqv->event_box), TRUE );
    gtk_widget_set_can_focus(seqv->event_box, TRUE);
    gtk_widget_set_tooltip_text(GTK_WIDGET(seqv->event_box),
        "Empty track. Use Add Track to connect another Veejay instance.");

	g_signal_connect( G_OBJECT( seqv->event_box ),
				"button_press_event",
				G_CALLBACK( seqv_mouse_press_event ),
				(gpointer*) seqv );
	gtk_widget_show( GTK_WIDGET( seqv->event_box ) );


	snprintf(track_title,sizeof(track_title), "Track %d", num );
	seqv->frame = gtk_frame_new( track_title );
    gtk_widget_set_tooltip_text(GTK_WIDGET(seqv->frame),
        "Empty track. Use Add Track to connect another Veejay instance.");

	gtk_container_set_border_width( GTK_CONTAINER( seqv->frame) , 1 );
	gtk_widget_show( GTK_WIDGET( seqv->frame ) );
	gtk_container_add( GTK_CONTAINER( seqv->event_box), seqv->frame );

	seqv->main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
	gtk_container_add( GTK_CONTAINER( seqv->frame ), seqv->main_vbox );
	gtk_widget_show( GTK_WIDGET( seqv->main_vbox ) );

	seqv->area = gtk_image_new();
    gtk_widget_set_tooltip_text(GTK_WIDGET(seqv->area),
        "Empty track. Use Add Track to connect another Veejay instance.");

	gtk_box_pack_start( GTK_BOX(seqv->main_vbox),GTK_WIDGET( seqv->area), FALSE,FALSE,0);
	gtk_widget_set_size_request_( seqv->area, 176,144  ); 
	seqv->panel = gtk_frame_new(NULL);

	seqv->toggle = gtk_toggle_button_new_with_label( "Preview off" );
    gtk_widget_set_tooltip_text(GTK_WIDGET(seqv->toggle),
        "Preview is unavailable until a Veejay instance is connected to this track.");

    gtk_toggle_button_set_active(
		GTK_TOGGLE_BUTTON(seqv->toggle), FALSE  );
	g_signal_connect( G_OBJECT( seqv->toggle ), "toggled", G_CALLBACK(sequence_preview_cb),
		(gpointer)seqv );
	gtk_box_pack_start( GTK_BOX(seqv->main_vbox), seqv->toggle,FALSE,FALSE, 0 );

	gtk_widget_set_sensitive_( GTK_WIDGET( seqv->toggle ), FALSE );
    gtk_widget_show( seqv->toggle );

	GtkWidget *vvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	seqv->button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	gtk_box_pack_start( GTK_BOX(vvbox), seqv->button_box ,FALSE,FALSE, 0 );
	add_buttons( seqv,seqv,seqv->button_box );

	gtk_widget_show( seqv->button_box );
	gtk_container_add( GTK_CONTAINER( seqv->main_vbox ), seqv->panel );

	seqv->button_box2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start( GTK_BOX(vvbox), seqv->button_box2, FALSE,FALSE, 0 );
	add_buttons2( seqv,seqv,seqv->button_box2 );
	gtk_widget_show( seqv->button_box2 );
	gtk_container_add( GTK_CONTAINER( seqv->panel ), vvbox );
	gtk_widget_show(vvbox);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
	seqv->timeline_ = gtk_scale_new_with_range( GTK_ORIENTATION_HORIZONTAL, 
                                              0.0, 1.0, 0.1);
    gtk_widget_set_tooltip_text(GTK_WIDGET(seqv->timeline_), "Set the frame position");
	gtk_scale_set_draw_value( GTK_SCALE(seqv->timeline_), FALSE );
	//gtk_widget_set_size_request_( seqv->panel,180 ,180);
    GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( seqv->timeline_ ));
    gtk_adjustment_set_value( a , 0.0 );
	gtk_widget_show( seqv->panel );
 	gtk_box_pack_start( GTK_BOX( box ), seqv->timeline_, FALSE,FALSE, 0 );
	gtk_box_pack_start( GTK_BOX( vvbox ), box , FALSE,FALSE,0);
	gtk_widget_show(seqv->timeline_);
	g_signal_connect( seqv->timeline_, "value_changed",
       	         (GCallback) sequence_set_current_frame, (gpointer*) seqv );

	GtkWidget *scroll = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_ETCHED_IN );
	gtk_widget_set_size_request_(scroll,30,140);
	gtk_container_set_border_width(GTK_CONTAINER(scroll),0);
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
	GtkWidget *vvvbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	seqv->tracks = create_track_view(seqv->num, MAX_TRACKS, (void*) seqv, (void*) vp );
	gtk_tree_view_set_headers_visible( GTK_TREE_VIEW( get_track_tree(seqv->tracks)) , FALSE );
	gtk_widget_set_size_request_( get_track_tree(seqv->tracks),20,80 );
	gtk_widget_show(scroll);

	gtk_scrolled_window_add_with_viewport(
		GTK_SCROLLED_WINDOW( scroll ), get_track_tree(seqv->tracks) );
	gtk_widget_show( get_track_tree(seqv->tracks));
	gtk_box_pack_start( GTK_BOX(vvvbox), scroll, TRUE,TRUE, 0);

	GtkWidget *hhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);

	seqv->sliders_[0] = gtk_scale_new_with_range( GTK_ORIENTATION_VERTICAL,
                                                -12.0,12.0,1.0 );

    gtk_widget_set_tooltip_text( GTK_WIDGET(seqv->sliders_[0]), "Speed" );

	seqv->sliders_[1] = gtk_scale_new_with_range( GTK_ORIENTATION_VERTICAL,
                                                0.0, 1.0, 0.01 );

    gtk_widget_set_tooltip_text( GTK_WIDGET(seqv->sliders_[1]), "Opacity");

    a = gtk_range_get_adjustment( GTK_RANGE( seqv->sliders_[0]));
    gtk_adjustment_set_value( a, 1.0 );
    a = gtk_range_get_adjustment( GTK_RANGE( seqv->sliders_[1]));
    gtk_adjustment_set_value( a, 0.0 );


	gtk_scale_set_digits( GTK_SCALE(seqv->sliders_[1]), 2 );
	g_signal_connect( G_OBJECT( seqv->sliders_[0] ), "value_changed", G_CALLBACK( seq_speed ),
				(gpointer*)seqv );
	g_signal_connect( G_OBJECT( seqv->sliders_[1] ), "value_changed", G_CALLBACK( seq_opacity ),
				(gpointer*)seqv );

	gtk_box_pack_start( GTK_BOX( hhbox ), seqv->sliders_[0], TRUE, TRUE, 0 );
	gtk_box_pack_start( GTK_BOX( hhbox ), seqv->sliders_[1], TRUE, TRUE, 0 );
	gtk_widget_show( seqv->sliders_[0] );
	gtk_widget_show( seqv->sliders_[1] );
	gtk_box_pack_start( GTK_BOX(vvvbox), hhbox, TRUE,TRUE, 0 );
	gtk_widget_show( hhbox );
	gtk_container_add( GTK_CONTAINER( box ), vvvbox );
	gtk_widget_show( vvvbox );
	gtk_widget_show( box );


	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	gtk_box_set_spacing( GTK_BOX(hbox), 10 );
	seqv->labels_[0] = gtk_label_new( "00:00:00:00" );
	seqv->labels_[1] = gtk_label_new( "--" );
	gtk_widget_set_tooltip_text(GTK_WIDGET(seqv->labels_[0]), "Current transport position");
	gtk_widget_set_tooltip_text(GTK_WIDGET(seqv->labels_[1]), "Duration or stream buffer state");
	gtk_label_set_width_chars(GTK_LABEL(seqv->labels_[0]), 11);
	gtk_label_set_width_chars(GTK_LABEL(seqv->labels_[1]), 16);
	gtk_label_set_ellipsize(GTK_LABEL(seqv->labels_[1]), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start( GTK_BOX( hbox ), seqv->labels_[0], FALSE, FALSE, 0 );
	gtk_box_pack_start( GTK_BOX( hbox ), seqv->labels_[1], FALSE, FALSE, 0 );
	gtk_widget_show( seqv->labels_[0] );
	gtk_widget_show( seqv->labels_[1] );
	gtk_box_pack_start( GTK_BOX(seqv->main_vbox), hbox, FALSE,FALSE, 0 );
	gtk_widget_show( hbox );


	gtk_widget_set_sensitive_(GTK_WIDGET(seqv->panel), FALSE );

    add_class( GTK_WIDGET(seqv->frame), "track");
	gtk_widget_show( GTK_WIDGET( seqv->area ) );


	return seqv;
}


void		*multitrack_sync( void * mt )
{
	multitracker_t *m = (multitracker_t*) mt;
	sync_info *s = gvr_sync( m->preview,mt );
	if(!s)
		return NULL;
	s->master = m->master_track;
	return (void*)s;
}

static char *mt_new_connection_dialog(multitracker_t *mt, int *port_num, int *error)
{
	GtkWidget *dialog = gtk_dialog_new_with_buttons(
				"Connect to Veejay",
				GTK_WINDOW( mt->main_window ),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_STOCK_CANCEL,
				GTK_RESPONSE_REJECT,
				GTK_STOCK_OK,
				GTK_RESPONSE_ACCEPT,
				NULL );

    add_class( dialog, "reloaded" );
	GtkWidget *text_entry = gtk_entry_new();
	const char *default_host = multitrack_default_host(mt);
	gint p = multitrack_next_port_hint(mt, default_host);
	gtk_entry_set_text( GTK_ENTRY(text_entry), default_host );
	gtk_editable_set_editable( GTK_EDITABLE(text_entry), TRUE );
	gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_REJECT );
	gtk_window_set_resizable( GTK_WINDOW( dialog ), FALSE );

	GtkAdjustment *adj = gtk_adjustment_new( p,1024,65535,5,10,0);
	GtkWidget *num_entry = gtk_spin_button_new( GTK_ADJUSTMENT(adj), 5.0, 0 );

	GtkWidget *text_label = gtk_label_new( "Hostname" );
	GtkWidget *num_label  = gtk_label_new( "Port" );
	g_signal_connect( G_OBJECT(dialog), "response",
			G_CALLBACK( gtk_widget_hide ), G_OBJECT( dialog ) );

	GtkWidget *vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, 4 );
	gtk_container_add( GTK_CONTAINER( vbox ), text_label );
	gtk_container_add( GTK_CONTAINER( vbox ), text_entry );
	gtk_container_add( GTK_CONTAINER( vbox ), num_label );
	gtk_container_add( GTK_CONTAINER( vbox ), num_entry );
    GtkWidget* content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_container_add( GTK_CONTAINER (content_area), vbox );
	gtk_widget_show_all( dialog );

	gint res = gtk_dialog_run( GTK_DIALOG(dialog) );

	if( res == GTK_RESPONSE_ACCEPT )
	{
		const char *host = gtk_entry_get_text( GTK_ENTRY( text_entry ) );
		gint   port = gtk_spin_button_get_value( GTK_SPIN_BUTTON(num_entry ));
		if(!host || !*host)
			host = "localhost";
		char *result = strdup(host);
		gtk_widget_destroy( dialog );
		*port_num = port;
		*error    = 0;
		return result;
	}

	gtk_widget_destroy( dialog );

	*error = res;
	*port_num = 0;
	return NULL;
}

void		*multitrack_new(
		void (*f)(int,char*,int),
		int (*g)(GdkPixbuf *, GdkPixbuf *, GtkImage *),
		GtkWidget *win,
		GtkWidget *box,
		GtkWidget *msg,
		GtkWidget *preview_toggle,
		gint max_w,
		gint max_h,
		GtkWidget *main_preview_area, //FIXME Not used
		void *infog,
		int threads)
{
	multitracker_t *mt = (multitracker_t*) vj_calloc(sizeof(multitracker_t));
	mt->view 	= (sequence_view_t**) vj_calloc(sizeof(sequence_view_t*) * MAX_TRACKS );
	mt->preview	= NULL;
	mt->main_window = win;
	mt->main_box    = box;
	mt->status_bar  = msg;
 	mt->logo = load_logo_image(0,0);
	mt->preview_toggle = preview_toggle;
	mt->scroll = gtk_scrolled_window_new(NULL,NULL);
	gtk_widget_set_size_request(mt->scroll, 50 + max_w * 2, max_h + 40);
	gtk_container_set_border_width(GTK_CONTAINER(mt->scroll),1);
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(mt->scroll),GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	GtkWidget *grid = gtk_grid_new();

	gtk_box_pack_start( GTK_BOX( mt->main_box ), mt->scroll , TRUE,TRUE, 0 );
    gtk_widget_show(mt->scroll);
	int c = 0;
	for( c = 0; c < MAX_TRACKS; c ++ )
	{
		mt->view[c] = new_sequence_view( mt,  c );
		gtk_grid_attach( GTK_GRID(grid), mt->view[c]->event_box, c, 0, 1, 1 );
	}

	gtk_scrolled_window_add_with_viewport(
			GTK_SCROLLED_WINDOW( mt->scroll ), grid );

	gtk_widget_show(grid);

	mt->master_track = 0;
	mt->preview = gvr_preview_init( MAX_TRACKS, threads );

	for( c = 0; c < MAX_TRACKS; c ++ )
		multitrack_update_track_label(mt, c, c == 0);

	parent__ = infog;

	return (void*) mt;
}


int		multitrack_add_track( void *data )
{
	multitracker_t *mt = (multitracker_t*) data;
	int res = 0;
	int port_num = 0;
	int error = 0;

	char *hostname = mt_new_connection_dialog( mt,&port_num,&error );
	if( error || hostname == NULL ) {
		return res;
	}

	int track = -1;

	if( gvr_track_find_open( mt->preview, hostname, port_num, &track ) )
	{
		status_print( mt,
			"Veejay %s:%d is already connected on track %d. Double-click that track to focus it.",
			hostname, port_num, track );
		multitrack_update_track_label(mt, track, track == 0);
		free( hostname );
		return 0;
	}

	if( gvr_track_connect( mt->preview, hostname, port_num, &track ) )
	{
		status_print( mt, "Connection established with Veejay running on %s, port %d", hostname, port_num );
		gvr_track_configure(mt->preview, track, mt->pw, mt->ph);
		gvr_track_toggle_preview(mt->preview, track, gveejay_user_preview());
		multitrack_set_preview_toggle_state(mt, track, gvr_get_preview_status(mt->preview, track));
		gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[track]->panel), TRUE );
		gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[track]->toggle), (track == 0 ? FALSE : TRUE) );
        mt->track_status[ track ] = 1;
		multitrack_update_track_label(mt, track, track == 0);
		res = 1;
	}
	else
	{
		status_print( mt, "Unable to open connection to %s:%d", hostname, port_num );
	}

	free( hostname );

	return res;
}


int         multitrack_get_track_status(void *data, int track )
{
    multitracker_t *mt = (multitracker_t*) data;

    if(!mt || track < 0 || track >= __MAX_TRACKS)
        return 0;

    return mt->track_status[ track ];
}

void		multitrack_cleanup_track( void *data, int track )
{
	multitracker_t *mt = (multitracker_t*) data;

	if(!mt || track < 0 || track >= MAX_TRACKS || !mt->view[track])
		return;

	multitrack_set_preview_toggle_state(mt, track, 0);
	mt->view[track]->status_lock = 1;
	gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[track]->panel), FALSE );
	gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[track]->toggle), FALSE );
	gtk_image_clear( GTK_IMAGE(mt->view[track]->area ) );
	mt->view[track]->status_lock = 0;
    mt->track_status[ track ] = 0;
	multitrack_update_track_label(mt, track, track == 0);
}

void		multitrack_close_track( void *data )
{
	multitracker_t *mt = (multitracker_t*) data;

	if( mt->selected > 0 && mt->selected < MAX_TRACKS )
	{
		gvr_track_disconnect( mt->preview, mt->selected );
        multitrack_cleanup_track(data, mt->selected );
	}
}

void        multitrack_close_tracks(void *data)
{
    multitracker_t *mt = (multitracker_t*) data;
    int i;
    for( i = 0; i < MAX_TRACKS; i ++ ){
        gvr_track_disconnect(mt->preview,i);
        multitrack_cleanup_track(data, i);
    }
    mt->master_track = 0;
    mt->selected = 0;
}

void		multitrack_disconnect(void *data)
{
	multitracker_t *mt = (multitracker_t*) data;
	//release connection to veejay
	gvr_track_disconnect( mt->preview, 0 );
}

static void multitrack_update_track_tooltip(multitracker_t *mt, int track, int current)
{
	char tip[256];
	char *host;
	int port;

	if(!mt || track < 0 || track >= MAX_TRACKS || !mt->view[track])
		return;

	host = gvr_track_get_hostname(mt->preview, track);
	port = gvr_track_get_portnum(mt->preview, track);

	if(host && port > 0)
	{
		if(current)
			snprintf(tip, sizeof(tip),
				"Current Reloaded connection: %s:%d",
				host, port);
		else
			snprintf(tip, sizeof(tip),
				"Double-click this track to connect Reloaded to %s:%d and make it the current Veejay instance.",
				host, port);
	}
	else
		snprintf(tip, sizeof(tip),
			"Empty track. Use Add Track to connect another Veejay instance.");

	gtk_widget_set_tooltip_text(GTK_WIDGET(mt->view[track]->event_box), tip);
	gtk_widget_set_tooltip_text(GTK_WIDGET(mt->view[track]->frame), tip);
	gtk_widget_set_tooltip_text(GTK_WIDGET(mt->view[track]->area), tip);

	if(mt->view[track]->toggle)
	{
		if(host && port > 0)
			gtk_widget_set_tooltip_text(GTK_WIDGET(mt->view[track]->toggle),
				current ?
				"Preview follows the main Live View setting for the current connection." :
				"Toggle live preview for this multitrack connection.");
		else
			gtk_widget_set_tooltip_text(GTK_WIDGET(mt->view[track]->toggle),
				"Preview is unavailable until a Veejay instance is connected to this track.");
	}
}

static void multitrack_update_track_label(multitracker_t *mt, int track, int current)
{
	char track_title[64];
	int port;

	if(!mt || track < 0 || track >= MAX_TRACKS || !mt->view[track])
		return;

	port = gvr_track_get_portnum(mt->preview, track);
	if(port > 0)
		snprintf(track_title, sizeof(track_title), "Track %d (%d)", mt->view[track]->num, port);
	else
		snprintf(track_title, sizeof(track_title), "Track %d", mt->view[track]->num);

	gtk_frame_set_label(GTK_FRAME(mt->view[track]->frame), track_title);
	multitrack_update_track_tooltip(mt, track, current);
}


static void multitrack_mark_current_track(multitracker_t *mt)
{
	int i;

	if(!mt)
		return;

	gvr_set_master(mt->preview, 0);
	mt->master_track = 0;
	mt->selected = 0;

	for(i = 0; i < MAX_TRACKS; i++)
	{
		if(!mt->view[i])
			continue;
		multitrack_update_track_label(mt, i, i == 0);
		if(gvr_track_test(mt->preview, i))
		{
			multitrack_set_preview_toggle_state(mt, i, gvr_get_preview_status(mt->preview, i));
			gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[i]->panel), TRUE);
			gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[i]->toggle), (i == 0 ? FALSE : TRUE));
		}
	}
}

static int multitrack_promote_track_to_current(multitracker_t *mt, int track)
{
	int tmp_status;

	if(!mt || track < 0 || track >= MAX_TRACKS)
		return 0;
	if(!gvr_track_test(mt->preview, track))
		return 0;

	if(track != 0)
	{
		if(!gvr_track_swap(mt->preview, 0, track))
			return 0;

		tmp_status = mt->track_status[0];
		mt->track_status[0] = mt->track_status[track];
		mt->track_status[track] = tmp_status;

		gvr_track_configure(mt->preview, 0, mt->pw, mt->ph);
		gvr_track_configure(mt->preview, track, mt->pw, mt->ph);
		gvr_track_toggle_preview(mt->preview, 0, gveejay_user_preview());
		multitrack_set_preview_toggle_state(mt, 0, gvr_get_preview_status(mt->preview, 0));
		multitrack_set_preview_toggle_state(mt, track, gvr_get_preview_status(mt->preview, track));

		if(mt->view[0] && mt->view[track])
		{
			gtk_image_clear(GTK_IMAGE(mt->view[0]->area));
			gtk_image_clear(GTK_IMAGE(mt->view[track]->area));
		}
	}

	multitrack_mark_current_track(mt);
	return 1;
}

void    multitrack_set_master_track(void *data, int track)
{
	multitracker_t *mt = (multitracker_t*) data;
	multitrack_promote_track_to_current(mt, track);
}

int		multrack_audoadd( void *data, char *hostname, int port_num )
{
	multitracker_t *mt = (multitracker_t*) data;
	int track = -1;
	int res;

	if( gvr_track_find_open( mt->preview, hostname, port_num, &track ) )
	{
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Reusing existing multitrack connection %s:%d on track %d",
			hostname, port_num, track);
	}
	else
	{
		res = gvr_track_connect( mt->preview, hostname, port_num, &track );
		if(res <= 0) {
			veejay_msg(VEEJAY_MSG_DEBUG,"Failed to open track in the multitracker");
			return -1;
		}
	}

	gvr_track_configure(mt->preview, track, mt->pw,mt->ph);

    mt->view[track]->status_lock = 1;

    gvr_track_toggle_preview( mt->preview, track, gveejay_user_preview() );
    int preview = gvr_get_preview_status( mt->preview, track );
	multitrack_set_preview_toggle_state(mt, track, preview);

    gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[track]->panel), TRUE );
    gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[track]->toggle), (track == 0 ? FALSE : TRUE) );
    mt->track_status[track] = 1;
    mt->view[track]->status_lock = 0;

	gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[track]->panel), TRUE );
	multitrack_update_track_label(mt, track, track == 0);

    veejay_msg(VEEJAY_MSG_DEBUG, "Connected to %s:%d on track %d", hostname, port_num, track);

	return track;
}


int		multitrack_locked( void *data)
{
	multitracker_t *mt = (multitracker_t*) data;

	if(!mt || mt->master_track < 0 || mt->master_track >= MAX_TRACKS || !mt->view[mt->master_track])
		return 1;

	return mt->view[mt->master_track]->status_lock;
}

void		multitrack_configure( void *data, float fps, int video_width, int video_height, int *box_w, int *box_h )
{
	multitracker_t *mt = (multitracker_t*) data;
	mt->fps = fps;

	calculate_img_dimension(video_width,video_height,&(mt->width),&(mt->height),&(mt->aspect_ratio),vj_get_preview_box_w(),vj_get_preview_box_h(),-1);

	*box_w = mt->width;
	*box_h = mt->height;

	veejay_msg(VEEJAY_MSG_DEBUG, "Multitrack %d x %d, %2.2f, ratio %f", mt->width,mt->height,mt->fps,mt->aspect_ratio);
}

void		multitrack_set_quality( void *data , int quality )
{
	multitracker_t *mt = (multitracker_t*) data;
	float ratio = 0.0f;
	int w = 0;
	int h = 0;
    int i;

	calculate_img_dimension(mt->width,mt->height,&w,&h,&ratio,vj_get_preview_box_w(),vj_get_preview_box_h(),quality);

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Preview image dimensions changed to %d x %d",w,h);

    for( i = 0; i < MAX_TRACKS; i ++ ) {
	    if(!gvr_track_configure( mt->preview, i,w,h ) )
	    {
		    veejay_msg(0, "Unable to configure preview %dx%d",w , h );
	    }
    }

	mt->pw = w;
	mt->ph = h;
}

void		multitrack_set_logo(void *data , GtkWidget *img)
{
	multitracker_t *mt = (multitracker_t*) data;

	if(!mt || !img)
		return;

	gtk_image_set_from_pixbuf_( GTK_IMAGE(img), mt->logo );
}

void		multitrack_toggle_preview( void *data, int track_id, int status, GtkWidget *img )
{
	multitracker_t *mt = (multitracker_t*) data;
	int applied = 0;

	if(!mt)
		return;

	if(track_id == -1 )
		applied = gvr_track_toggle_preview( mt->preview, mt->master_track, status );
	else
		applied = gvr_track_toggle_preview( mt->preview, track_id, status );

	if(applied) {
		int target = (track_id == -1 ? mt->master_track : track_id);
		multitrack_set_preview_toggle_state(mt, target, status);
		veejay_msg(VEEJAY_MSG_INFO, "Veejay grabber: preview %s", (status ? "enabled" : "disabled") );
	}

	if( status == 0 )
		multitrack_set_logo( data, img );
}

void		multitrack_release_track(void *data, int id, int release_this )
{
	multitracker_t *mt = (multitracker_t*) data;
	int stream_id = 0;

	//release this: track um

	stream_id = gvr_get_stream_id( mt->preview, release_this );
	if(stream_id > 0)
		gvr_queue_mvims( mt->preview, id, VIMS_STREAM_DELETE,stream_id );
}

void		multitrack_bind_track( void *data, int id, int bind_this )
{
	multitracker_t *mt = (multitracker_t*) data;

	if( bind_this < 0 || bind_this >= MAX_TRACKS )
		return;

	if( id < 0 || id >= MAX_TRACKS )
		return;

	char *host = gvr_track_get_hostname( mt->preview, bind_this );
	int   port = gvr_track_get_portnum ( mt->preview, bind_this );

	if( host != NULL && port > 0 )
		gvr_queue_cxvims( mt->preview, id, VIMS_STREAM_NEW_UNICAST, port, (unsigned char*)host );
}

void		multitrack_update_sequence_image( void *data , int track, GdkPixbuf *img )
{
	multitracker_t *mt = (multitracker_t*) data;
	float ratio = 0.0f;
	int w = 0;
	int h = 0;

	calculate_img_dimension(mt->width,mt->height, &w, &h, &ratio, 160, 120,0);

	GdkPixbuf *scaled = vj_gdk_pixbuf_scale_simple( img, w, h, GDK_INTERP_BILINEAR );
	gtk_image_set_from_pixbuf( GTK_IMAGE(mt->view[track]->area), scaled);

	g_object_unref( scaled );
}

/*! \brief Multi track sequence view button_press_event callback.
 *
 *  \sa new_sequence_view
 *
 *  \param w A pointer of calling widget
 *  \param event A pointer of the current event
 *  \param user_data A pointer of the current \c sequence_view_t
 *  \return Always \c FALSE to propagate the event.
 */
static gboolean seqv_mouse_press_event ( GtkWidget *w, GdkEventButton *event, gpointer user_data )
{
    sequence_view_t *v = (sequence_view_t*) user_data;
    multitracker_t *mt = v->backlink;

    if(event->type == GDK_2BUTTON_PRESS)
    {
      if( !gvr_track_test( mt->preview , v->num ) )
        return FALSE;

      if( v->num == 0 ) {
        vj_msg(VEEJAY_MSG_INFO, "Track 0 already is the current Reloaded connection");
        return FALSE;
      }

      char *host_src = gvr_track_get_hostname( mt->preview, v->num );
      int   port = gvr_track_get_portnum ( mt->preview, v->num );

      if(!host_src || port <= 0 )
        return FALSE;

      char *host = strdup(host_src);
      if(!host)
        return FALSE;

      vj_gui_disable();

      if(!multitrack_promote_track_to_current(mt, v->num))
      {
        free(host);
        vj_gui_enable();
        return FALSE;
      }

      vj_gui_cb( 1, host, port );
      vj_gui_enable();

      vj_msg(VEEJAY_MSG_INFO, "Switched current Reloaded connection to track 0 (%s:%d)", host, port );
      free(host);
    }
    return FALSE;
}
