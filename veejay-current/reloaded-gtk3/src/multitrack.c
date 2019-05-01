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
#include <veejay/vjmem.h>
#include <veejay/vje.h>
#include <veejay/vj-client.h>
#include <veejay/vj-msg.h>
#include <veejay/vims.h>
#include <gtk/gtk.h>
#include <string.h>
#include <gdk/gdkx.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <veejay/libvevo.h>
#include <src/vj-api.h>
#include "sequence.h"
#include "tracksources.h"

#define SEQ_BUTTON_CLOSE 0
#define SEQ_BUTTON_RULE  1

#include <src/common.h>
#include <src/utils.h>
#include <src/gtktimeselection.h>
#include <src/vj-api.h>
#include <src/multitrack.h>
#define __MAX_TRACKS 64
#define RUP8(num)(num/8*8)

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
	GtkWidget *buttons[8];
	GtkWidget *icons[8];
	GtkWidget *button_box;
	GtkWidget *timeline_;
	GtkWidget *labels_[4];
	GtkWidget *sliders_[4];
	GtkWidget *button_box2;
	GtkWidget *buttons2[8];
	void *tracks;
	gint dim[2];
	int  num;
	int  status_lock;
	void *backlink;
	int status_cache[32];
	int history[4][32];
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
} multitracker_t;

static int	MAX_TRACKS = 8; /* MASTER (current) + Track 1 to 6 */
static void		*parent__ = NULL;

static char	*mt_new_connection_dialog(multitracker_t *mt, int *port_num, int *error);
static void	add_buttons( sequence_view_t *p, sequence_view_t *seqv , GtkWidget *w);
static void	add_buttons2( sequence_view_t *p, sequence_view_t *seqv , GtkWidget *w);
static sequence_view_t *new_sequence_view( void *vp, int num );
static void	update_pos( void *data, gint total, gint current );
static gboolean seqv_mouse_press_event ( GtkWidget *w, GdkEventButton *event, gpointer user_data);

extern GdkPixbuf       *vj_gdk_pixbuf_scale_simple( GdkPixbuf *src, int dw, int dh, GdkInterpType inter_type );
extern void		gtk_widget_set_size_request__( GtkWidget *w, gint iw, gint h, const char *f, int line );

#define gtk_widget_set_size_request_(a,b,c) gtk_widget_set_size_request(a,b,c)

int mt_set_max_tracks(int mt)
{
	if( mt < 0 || mt > __MAX_TRACKS)
		return 0;
	MAX_TRACKS = mt;
	return 1;
}

int	mt_get_max_tracks()
{
	return  __MAX_TRACKS;
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
	text[strlen(text)-1] = '\0';
	gtk_statusbar_push( GTK_STATUSBAR(mt->status_bar), 0, text);
	g_free(text);
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

	*dst_w = RUP8(tmp_w);
	*dst_h = RUP8(tmp_h);
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

	*dst_w = RUP8(tmp_w);
	*dst_h = RUP8(tmp_h);
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


static	void	seq_gotostart(GtkWidget *w, gpointer data )
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_GOTO_START );
}

static void seq_reverse(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_PLAY_BACKWARD );
}

static void seq_pause(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_PLAY_STOP );
}

static void seq_play( GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_PLAY_FORWARD );
}

static void seq_gotoend(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_GOTO_END );
}

static	void	seq_speeddown(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	gint n = v->status_cache[ SAMPLE_SPEED ];

	if( n < 0 ) n += 1;
	if( n > 0 ) n -= 1;
	gvr_queue_mvims( mt->preview, v->num ,VIMS_VIDEO_SET_SPEED , n );
}

static	void	seq_speedup(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	gint n = v->status_cache[ SAMPLE_SPEED ];

	if( n < 0 ) n -= 1;
	if( n > 0 ) n += 1;
	gvr_queue_mvims( mt->preview, v->num ,VIMS_VIDEO_SET_SPEED , n );
}

static	void	seq_prevframe(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

	gvr_queue_vims( mt->preview, v->num ,VIMS_VIDEO_PREV_FRAME );

}

static	void	seq_nextframe(GtkWidget *w, gpointer data)
{
	sequence_view_t *v = (sequence_view_t*) data;
	multitracker_t *mt = (multitracker_t*)v->backlink;

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
	if(v->status_lock)
		return;

  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( v->timeline_ ));
  gtk_adjustment_set_value (a, 1.0 / (gdouble) total * current );

	char *now = format_time( current , mt->fps);
	gtk_label_set_text( GTK_LABEL(v->labels_[0]), now );
	free(now);
}

static	void	update_speed( void *user_data, gint speed )
{
	sequence_view_t *v = (sequence_view_t*) user_data;
	if(v->status_lock)
		return;

  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( v->sliders_[0] ));
  gtk_adjustment_set_value( a, (gdouble) speed );
}

#define FIRST_ROW_END 5
static struct
{
	const char *name;
	int vims_id;
	const char *file;
	void (*f)();
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
		seqv->buttons[i] = gtk_button_new_with_label(" ");
		gtk_widget_set_size_request_( seqv->icons[i],24,20 );
		gtk_button_set_image( GTK_BUTTON(seqv->buttons[i]), seqv->icons[i] );
		gtk_widget_set_size_request_( seqv->buttons[i],24,20 );
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
		seqv->buttons2[i] = gtk_button_new_with_label(" ");
		gtk_widget_set_size_request_( seqv->icons[i],24,20 );

		gtk_button_set_image( GTK_BUTTON(seqv->buttons2[i]), seqv->icons[i] );
		gtk_widget_set_size_request_( seqv->buttons2[i],24,20 );
		gtk_box_pack_start( GTK_BOX(w), seqv->buttons2[i], TRUE,TRUE, 0 );
		g_signal_connect( G_OBJECT( seqv->buttons2[i] ), "clicked", G_CALLBACK( button_template_t[i].f),
				(gpointer*)p );
		gtk_widget_show( seqv->buttons2[i] );

	}
}



static	void	playmode_sensitivity( sequence_view_t *p, gint pm )
{
	int i;
	if( pm == MODE_STREAM || pm == MODE_PLAIN || pm == MODE_SAMPLE )
	{
		if(p->num > 0)
			gtk_widget_set_sensitive_( GTK_WIDGET( p->toggle ), TRUE );
		gtk_widget_set_sensitive_( GTK_WIDGET( p->panel ), TRUE );
	}

	if( pm == MODE_STREAM )
	{
		gtk_widget_set_sensitive_( GTK_WIDGET( p->button_box2 ), FALSE );
		gtk_widget_set_sensitive_( GTK_WIDGET( p->button_box ), FALSE );
		gtk_widget_set_sensitive_( GTK_WIDGET( p->sliders_[0] ), FALSE );
		gtk_widget_set_sensitive_( GTK_WIDGET( p->timeline_ ), FALSE );
		gtk_widget_set_sensitive_( GTK_WIDGET( p->sliders_[1] ), TRUE );
		for( i = 0; i < FIRST_ROW_END;i ++ )
		{
			gtk_widget_set_sensitive_( GTK_WIDGET( p->buttons[i] ), FALSE );

		}
	}
	else
	{
		if( pm == MODE_SAMPLE || pm == MODE_PLAIN )
		{
			gtk_widget_set_sensitive_( GTK_WIDGET( p->button_box2 ), TRUE );
			gtk_widget_set_sensitive_( GTK_WIDGET( p->button_box ), TRUE );
			gtk_widget_set_sensitive_( GTK_WIDGET( p->sliders_[0] ), TRUE );
			gtk_widget_set_sensitive_( GTK_WIDGET( p->timeline_ ), TRUE );
			for( i = 0; i < FIRST_ROW_END;i ++ )
			{
				gtk_widget_set_sensitive_( GTK_WIDGET( p->buttons[i] ), TRUE );
			}
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
	if( h[PLAY_MODE] != pm )
		playmode_sensitivity( p, pm );

	if( pm == MODE_STREAM )
	{
		update_pos( p, status[TOTAL_FRAMES], 0 );
		update_speed( p, 1 );
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
	for( i  =  0; i < 20; i ++ )
		p->status_cache[i] = array[i];
	update_widgets(array, p, pm);

	int *his = p->history[ pm ];
	for( i  =  0; i < 20; i ++ )
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
		veejay_msg(0, "Unable to configure preview %d x %d",tmp_w,tmp_h );
	}

}

static void sequence_preview_cb(GtkWidget *widget, gpointer user_data)
{
	sequence_view_t *v = (sequence_view_t*) user_data;
	multitracker_t *mt = v->backlink;
	int status = 0;

	if(v->status_lock)
		return;

	if(v->num != mt->master_track )
	{
		 status = (gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget) ) == TRUE ? 1 : 0 );

		gvr_track_toggle_preview( mt->preview, v->num,status );

		sequence_preview_size( mt, v->num );

		if( !status )
			gtk_image_clear( GTK_IMAGE(v->area ) );
	}
}

static	void	sequence_set_current_frame(GtkWidget *w, gpointer user_data)
{

	sequence_view_t *v = (sequence_view_t*) user_data;
	multitracker_t *mt = v->backlink;
	if(v->status_lock)
		return;

  GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
  gdouble pos = gtk_adjustment_get_value (a);
	gint frame = pos * v->status_cache[TOTAL_FRAMES];

	gvr_queue_mvims( mt->preview, v->num, VIMS_VIDEO_SET_FRAME, frame );
}

static sequence_view_t *new_sequence_view( void *vp, int num )
{
	sequence_view_t *seqv = (sequence_view_t*) vj_calloc(sizeof(sequence_view_t));

	seqv->num = num;
	seqv->backlink = vp;

	seqv->event_box = gtk_event_box_new();
	gtk_event_box_set_visible_window( GTK_EVENT_BOX(seqv->event_box), TRUE );
  gtk_widget_set_can_focus(seqv->event_box, TRUE);

	g_signal_connect( G_OBJECT( seqv->event_box ),
				"button_press_event",
				G_CALLBACK( seqv_mouse_press_event ),
				(gpointer*) seqv );
	gtk_widget_show( GTK_WIDGET( seqv->event_box ) );


	gchar *track_title = g_new0( gchar, 20 );
	sprintf(track_title, "Track %d", num );
	seqv->frame = gtk_frame_new( track_title );
	g_free(track_title);

	gtk_container_set_border_width( GTK_CONTAINER( seqv->frame) , 1 );
	gtk_widget_show( GTK_WIDGET( seqv->frame ) );
	gtk_container_add( GTK_CONTAINER( seqv->event_box), seqv->frame );

	seqv->main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
	gtk_container_add( GTK_CONTAINER( seqv->frame ), seqv->main_vbox );
	gtk_widget_show( GTK_WIDGET( seqv->main_vbox ) );

	seqv->area = gtk_image_new();


	gtk_box_pack_start( GTK_BOX(seqv->main_vbox),GTK_WIDGET( seqv->area), FALSE,FALSE,0);
	gtk_widget_set_size_request_( seqv->area, 176,144  ); //FIXME
	seqv->panel = gtk_frame_new(NULL);

	seqv->toggle = gtk_toggle_button_new_with_label( "preview" );

	if(num>0) {
		gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(seqv->toggle), FALSE );
		g_signal_connect( G_OBJECT( seqv->toggle ), "toggled", G_CALLBACK(sequence_preview_cb),
			(gpointer)seqv );
		gtk_box_pack_start( GTK_BOX(seqv->main_vbox), seqv->toggle,FALSE,FALSE, 0 );

		gtk_widget_set_sensitive_( GTK_WIDGET( seqv->toggle ), FALSE );

		gtk_widget_show( seqv->toggle );
	} else {
		gtk_box_pack_start( GTK_BOX(seqv->main_vbox), seqv->toggle,FALSE,FALSE, 0 );
		gtk_widget_show( seqv->toggle );
		gtk_widget_set_sensitive_( GTK_WIDGET( seqv->toggle ), FALSE );
	}

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
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );
	GtkWidget *vvvbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	seqv->tracks = create_track_view(seqv->num, MAX_TRACKS, (void*) seqv );
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
	seqv->sliders_[1] = gtk_scale_new_with_range( GTK_ORIENTATION_VERTICAL,
                                                0.0, 1.0, 0.01 );

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
	seqv->labels_[1] = gtk_label_new( "00:00:00:00" );
	gtk_box_pack_start( GTK_BOX( hbox ), seqv->labels_[0], FALSE, FALSE, 0 );
	gtk_box_pack_start( GTK_BOX( hbox ), seqv->labels_[1], FALSE, FALSE, 0 );
	gtk_widget_show( seqv->labels_[0] );
	gtk_widget_show( seqv->labels_[1] );
	gtk_box_pack_start( GTK_BOX(seqv->main_vbox), hbox, FALSE,FALSE, 0 );
	gtk_widget_show( hbox );


	gtk_widget_set_sensitive_(GTK_WIDGET(seqv->panel), FALSE );

	gtk_widget_show( GTK_WIDGET( seqv->area ) );

	return seqv;
}


static	int	vt__[16];
static	int	vt___ = 0;
void		*multitrack_sync( void * mt )
{
	multitracker_t *m = (multitracker_t*) mt;
	sync_info *s = gvr_sync( m->preview );
	if(!s)
		return NULL;

	if(!vt___)
	{
		veejay_memset(vt__,0,sizeof(vt__));
		vt___ = 1;
	}

	int i;
	for( i =0; i < MAX_TRACKS ;i ++ )
	{
		if(!vt__[i] && s->status_list[i] == NULL )
		{
			//gtk_widget_set_sensitive_(GTK_WIDGET(m->view[i]), FALSE );
			vt__[i] = 1;
		}
		else if( s->status_list[i] && vt__[i] )
		{
			//gtk_widget_set_sensitive_(GTK_WIDGET(m->view[i]), TRUE );
			vt__[i] = 0;
		}
	}
	s->master = m->master_track;
	return (void*)s;
}

static char *mt_new_connection_dialog(multitracker_t *mt, int *port_num, int *error)
{
	GtkWidget *dialog = gtk_dialog_new_with_buttons(
				"Connect to a Veejay",
				GTK_WINDOW( mt->main_window ),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_STOCK_CANCEL,
				GTK_RESPONSE_REJECT,
				GTK_STOCK_OK,
				GTK_RESPONSE_ACCEPT,
				NULL );


	GtkWidget *text_entry = gtk_entry_new();
	gtk_entry_set_text( GTK_ENTRY(text_entry), "localhost" );
	gtk_editable_set_editable( GTK_EDITABLE(text_entry), TRUE );
	gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_REJECT );
	gtk_window_set_resizable( GTK_WINDOW( dialog ), FALSE );

	gint   base = DEFAULT_PORT_NUM;

	gint   p = (1000 * (mt->selected)) + base;

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
		*port_num = port;
		*error    = 0;
		return strdup(host);
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
//	gtk_widget_set_size_request(mt->scroll,50+max_w*2, max_h);
	gtk_container_set_border_width(GTK_CONTAINER(mt->scroll),1);
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(mt->scroll),GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
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
//	gvr_set_master( mt->preview, mt->master_track );


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

	int track = 0;

	if( gvr_track_connect( mt->preview, hostname, port_num, &track ) )
	{
		status_print( mt, "Connection established with veejay runnning on %s port %d", hostname, port_num );
		if( gveejay_user_preview() )
			gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(mt->view[track]->toggle), TRUE );
		gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[track]->panel), TRUE );
		gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[track]->toggle), TRUE );

		res = 1;
	}
	else
	{
		status_print( mt, "Unable to open connection with %s : %d", hostname, port_num );
	}

	free( hostname );

	return res;
}

void		multitrack_close_track( void *data )
{
	multitracker_t *mt = (multitracker_t*) data;

	if( mt->selected > 0 && mt->selected < MAX_TRACKS )
	{
		gvr_track_disconnect( mt->preview, mt->selected );
		mt->view[mt->selected]->status_lock = 1;
		gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(mt->view[mt->selected]->toggle), FALSE );
		gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[mt->selected]->panel), FALSE );
		gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[mt->selected]->toggle), FALSE );
		gtk_image_clear( GTK_IMAGE(mt->view[mt->selected]->area ) );
		mt->view[mt->selected]->status_lock = 0;
	}
}

void		multitrack_disconnect(void *data)
{
	multitracker_t *mt = (multitracker_t*) data;
	//release connection to veejay
	gvr_track_disconnect( mt->preview, 0 );
}

int		multrack_audoadd( void *data, char *hostname, int port_num )
{
	multitracker_t *mt = (multitracker_t*) data;

	int track = 0;

	if(!gvr_track_connect( mt->preview, hostname, port_num, &track ) )
	{
		if(!gvr_track_already_open( mt->preview, hostname,port_num))
			return -1;
	}

	if(mt->pw > 0 && mt->ph > 0 )
	{
		//sequence_preview_size( mt, mt->master_track );

		/* configure master preview size */
		if(!gvr_track_configure( mt->preview, track, mt->pw,mt->ph) )
		{
			veejay_msg(0, "Unable to configure preview %d x %d",mt->pw , mt->ph );
		}


		int preview = gvr_get_preview_status( mt->preview, mt->master_track );

		/* set status of preview toggle button in trackview */
		if( track == 0 )
		{
		//	mt->view[track]->status_lock=1;
		//	gtk_toggle_button_set_active(
		//		GTK_TOGGLE_BUTTON( mt->preview_toggle), (preview ? TRUE: FALSE ) );
		//	mt->view[track]->status_lock=0;
		}
		else
		{
			mt->view[track]->status_lock=1;
			gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON( mt->view[track]->toggle ), (preview ? TRUE: FALSE ) );
			mt->view[track]->status_lock=0;

		}
	}

//	mt->master_track = track;
	gvr_set_master( mt->preview, track );

	gtk_widget_set_sensitive_(GTK_WIDGET(mt->view[track]->panel), TRUE );


	return track;
}

int		multitrack_locked( void *data)
{
	multitracker_t *mt = (multitracker_t*) data;

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

	calculate_img_dimension(mt->width,mt->height,&w,&h,&ratio,vj_get_preview_box_w(),vj_get_preview_box_h(),quality);

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Preview image dimensions changed to %d x %d",w,h);

	if(!gvr_track_configure( mt->preview, mt->master_track,w,h ) )
	{
		veejay_msg(0, "Unable to configure preview %d x %d",w , h );
	}

	mt->pw = w;
	mt->ph = h;
}

void		multitrack_set_logo(void *data , GtkWidget *img)
{
	multitracker_t *mt = (multitracker_t*) data;
	gtk_image_set_from_pixbuf_( GTK_IMAGE(img), mt->logo );
}

void		multitrack_toggle_preview( void *data, int track_id, int status, GtkWidget *img )
{
	multitracker_t *mt = (multitracker_t*) data;
	if(track_id == -1 )
	{
		gvr_track_toggle_preview( mt->preview, mt->master_track, status );
		veejay_msg(VEEJAY_MSG_INFO, "VeejayGrabber: master preview %s", (status ? "enabled" : "disabled") );
		if( status == 0 )
			multitrack_set_logo( data, img );
	}
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

	if( bind_this < 0 || bind_this > MAX_TRACKS )
		return;

	if( id < 0 || id > MAX_TRACKS )
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

    if(event->type == GDK_BUTTON_PRESS)
    {
      if( !gvr_track_test( mt->preview , v->num ) )
        return FALSE;

      int last_selected = mt->selected;
      mt->selected = v->num;
      vj_gui_disable();

      // hostname, port_num from gvr
      char *host = gvr_track_get_hostname( mt->preview, v->num );
      int   port = gvr_track_get_portnum ( mt->preview, v->num );

      if(!host || port <= 0 )
      {
        vj_gui_enable();
        return FALSE;
      }

      vj_gui_cb( 0, host, port );

      gvr_set_master( mt->preview, v->num );
      if(!gvr_track_configure( mt->preview, v->num, mt->pw,mt->ph) )
      {
        veejay_msg(0, "Unable to configure preview %d x %d",mt->pw , mt->ph );
      }
      veejay_msg(VEEJAY_MSG_INFO, "Set master to track %d", mt->master_track );
      mt->master_track = v->num;
      if( last_selected >= 0 && last_selected < MAX_TRACKS )
      {
        gtk_widget_set_state_flags( mt->view[last_selected]->event_box,
                                    GTK_STATE_FLAG_NORMAL, TRUE);
      }
      gtk_widget_set_state_flags(w, GTK_STATE_FLAG_SELECTED, TRUE);

      vj_gui_enable();
    }
    return FALSE;
}
