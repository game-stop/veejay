/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
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
#include <libvjnet/vj-client.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vims.h>
#include <gtk/gtk.h>
#include <string.h>
#include <gdk/gdkx.h>
#include <glib.h>
#include <gdk/gdk.h>
#include "sequence.h"
#include "tracksources.h"

#define SEQ_BUTTON_CLOSE 0
#define SEQ_BUTTON_RULE  1

#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#include <gveejay-reloaded/multitrack.h>
#include <gveejay-reloaded/common.h>
#include <gveejay-reloaded/utils.h>
#include <gveejay-reloaded/widgets/gtktimeselection.h>
#define WTIME 50000 
#define G_ERRORCHECK_MUTEXES 1
#define G_DEBUG_LOCKS 1

G_LOCK_DEFINE(mt_lock);

extern int	_Xdebug;


#define __MAX_TRACKS 64
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
} sequence_view_t;

typedef struct
{
	int num;
	sequence_view_t *view;
	void *sequence;
	char *hostname;
	int port_num;
	void *backlink;
	int preview;
	int active;
	int used;
	gint timeout;
	int status_lock;
	int history[4][20];
	int status_cache[20];
	char *tracks[__MAX_TRACKS];
} mt_priv_t;

typedef struct
{
	mt_priv_t *pt[__MAX_TRACKS+1];
	int	   	stop_mt;
	GThread		*thread;
} all_priv_t;

typedef struct
{
	GtkWidget *main_window;
	GtkWidget *main_box;
	GtkWidget *status_bar;
	GtkWidget *scroll;
	void	  *data;
	int	  selected;
	int	  sensitive;
	int	quit;
} multitracker_t;

static  int	(*img_cb)(GdkPixbuf *p, GdkPixbuf *b, GtkImage *img);
static	void	(*gui_cb)(int, char*, int);
static	int	mt_new_connection_dialog(multitracker_t *mt, char *hostname,int len, int *port_num);
static	void	add_buttons( mt_priv_t *p, sequence_view_t *seqv , GtkWidget *w);
static	void	add_buttons2( mt_priv_t *p, sequence_view_t *seqv , GtkWidget *w);
static int	num_tracks_active( multitracker_t * mt );
void 		*mt_preview( gpointer user_data );
static	void	set_logo(GtkWidget *area);
static sequence_view_t *new_sequence_view( mt_priv_t *p,gint w, gint h, gint last, GtkWidget *main_area  );
static int	find_track( multitracker_t *mt, const char *host, int port );

static	int	preview_width_ = 0;
static  int     preview_height_ = 0;

static	int	mpreview_width_ = 0;
static  int	mpreview_height_ = 0;
static  float	fps_ = 25.0;

static volatile int	MAX_TRACKS = 4;
static volatile int	LAST_TRACK = 0;
static int     sta_w = 112;
static int     sta_h = 96;

static	GdkPixbuf	*logo_img_ = NULL;
static	float		logo_step_ = 0.1;
static	float		logo_value_ = 1.0;

static	void	update_pos( mt_priv_t *p, gint total, gint current );

static	void	gtk_image_set_from_pixbuf__( GtkImage *w, GdkPixbuf *p, const char *f, int l )
{
	gtk_image_set_from_pixbuf(w, p);
}


static	void	gtk_widget_set_sensitive__( GtkWidget *w, gboolean state, const char *f, int l )
{
#ifdef STRICT_CHECKING
	assert( GTK_IS_WIDGET(w) );
#endif
	gtk_widget_set_sensitive(w, state );
}

#ifdef STRICT_CHECKING
#define gtk_image_set_from_pixbuf_(w,p) gtk_image_set_from_pixbuf__( w,p, __FUNCTION__,__LINE__ );
#define gtk_widget_set_sensitive_( w,p ) gtk_widget_set_sensitive__( w,p,__FUNCTION__,__LINE__ )
#else
#define gtk_image_set_from_pixbuf_(w,p) gtk_image_set_from_pixbuf(w,p)
#define gtk_widget_set_sensitive_( w,p ) gtk_widget_set_sensitive(w,p)
#endif

static int	restore__[32];


void	multitrack_preview_master(void *data, int status)
{
	G_LOCK( mt_lock);
	multitracker_t *mt = (multitracker_t*) data;
	all_priv_t *pt = (all_priv_t*) mt->data;
	mt_priv_t *last = pt->pt[LAST_TRACK];

	if(status == last->preview)
	{
		G_UNLOCK( mt_lock );
		return;
	}

	last->preview = status;
	restore__[LAST_TRACK] = status;

	int n = find_track( mt, last->hostname, last->port_num );
	if ( n >= 0 )
	{
		mt_priv_t *p = pt->pt[ n ];
		gtk_toggle_button_set_active( 	
			GTK_TOGGLE_BUTTON( p->view->toggle ), status == 0 ? FALSE: TRUE);
	}
	
	G_UNLOCK(mt_lock);
}

static	void	status_print(multitracker_t *mt, const char format[], ... )
{
	char buf[1024];
	va_list args;
	bzero(buf,1024);
	va_start(args,format);
	vsnprintf( buf,sizeof(buf), format, args );
	int nr,nw;
	gchar *text = g_locale_to_utf8( buf, -1, &nr, &nw, NULL );
	text[strlen(text)-1] = '\0';
	gtk_statusbar_push( GTK_STATUSBAR(mt->status_bar), 0, text);
	g_free(text);
	va_end(args);
}

void	multitrack_sync_start(void *data)
{
	multitracker_t *mt = (multitracker_t*)data;
	all_priv_t *pt = (all_priv_t*)mt->data;
	gint i;
	G_LOCK(mt_lock);
	for( i = 0;i < MAX_TRACKS; i ++ )
	{
		mt_priv_t *p = pt->pt[i];
		if(p->active)
		{
			veejay_sequence_send( p->sequence,VIMS_VIDEO_PLAY_STOP, NULL,NULL);
			veejay_sequence_send( p->sequence,VIMS_VIDEO_GOTO_START , NULL ,NULL);
			veejay_sequence_send(p->sequence,  VIMS_VIDEO_PLAY_FORWARD, NULL ,NULL);
		}
	}
	G_UNLOCK(mt_lock);
}
void	multitrack_sync_simple_cmd(void *data, int vims_id, int value)
{
	multitracker_t *mt = (multitracker_t*)data;
	all_priv_t *pt = (all_priv_t*)mt->data;
	gint i;
	G_LOCK(mt_lock);
	for( i = 0;i < MAX_TRACKS; i ++ )
	{
		mt_priv_t *p = pt->pt[i];
		if(p->active)
			veejay_sequence_send( p->sequence, vims_id, (value > 0 ? "%d": NULL),
					(value > 0 ? value :NULL));
	}
	G_UNLOCK(mt_lock);
}

int 	sequence_get_track_id(void *priv)
{
	mt_priv_t *p = (mt_priv_t*) priv;
	return p->num;
}

int *	sequence_get_track_status(void *priv)
{
	mt_priv_t *p = (mt_priv_t*) priv;
	multitracker_t *mt = (multitracker_t*) p->backlink;
	all_priv_t *a = (all_priv_t*) mt->data;
	gint i;
	int *result = (int*) vj_calloc(sizeof(int) * MAX_TRACKS );

	for( i = 0;i < MAX_TRACKS ; i ++ )
	{
		mt_priv_t *q = a->pt[i];
		result[i] = 0; // reset it
		if(q->active)
		{
			// find in tracklist
			gint j;
			gint num = -1;
			for( j = 0; j < MAX_TRACKS; j ++ )
			{
				if(p->tracks[j])
				{
					char hostname[255];
					int  port_num = 0;
					char *str = p->tracks[j];
					int  tag_id = 0;
					if(sscanf(str, "%s %d %d", hostname, &port_num, &tag_id ))
					{
						if(strncasecmp( hostname, q->hostname,strlen(hostname)) == 0 && port_num ==
									q->port_num )
						{
							num = i;
							break;
						}
					}	
				}
			}
			if( num >= 0 )
				result[i] = 1;
		}
	}
	return result;
}

static	void	seq_gotostart(GtkWidget *w, gpointer user_data )
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	if(p->active)	
		veejay_sequence_send( p->sequence,VIMS_VIDEO_GOTO_START , NULL ,NULL);
}
static void seq_reverse(GtkWidget *w, gpointer user_data)
{	
	mt_priv_t *p = (mt_priv_t*) user_data;
	if(p->active)
		veejay_sequence_send(p->sequence,  VIMS_VIDEO_PLAY_BACKWARD, NULL ,NULL);
} 
static void seq_pause(GtkWidget *w, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	if(p->active)
		veejay_sequence_send(p->sequence,  VIMS_VIDEO_PLAY_STOP, NULL ,NULL);
}
static void seq_play( GtkWidget *w, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	if(p->active)
		veejay_sequence_send(p->sequence,  VIMS_VIDEO_PLAY_FORWARD, NULL ,NULL);
}
static void seq_gotoend(GtkWidget *w, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	if(p->active)	
		veejay_sequence_send(p->sequence, VIMS_VIDEO_GOTO_END , NULL,NULL );
}
static	void	seq_speeddown(GtkWidget *w, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	int n = p->status_cache[SAMPLE_SPEED];
	if( n < 0 ) n += 1;
	if( n > 0 ) n -= 1;
	if(p->active)
		veejay_sequence_send( p->sequence, VIMS_VIDEO_SET_SPEED, "%d", n );
}
static	void	seq_speedup(GtkWidget *w, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	int n = p->status_cache[SAMPLE_SPEED];
	if( n < 0 ) n -= 1;
	if( n > 0 ) n += 1;
	if(p->active)
		veejay_sequence_send( p->sequence, VIMS_VIDEO_SET_SPEED, "%d", n );
}
static	void	seq_prevframe(GtkWidget *w, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	if(p->active) 
		veejay_sequence_send( p->sequence, VIMS_VIDEO_PREV_FRAME, NULL,NULL );
}
static	void	seq_nextframe(GtkWidget *w, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	if(p->active) 
		veejay_sequence_send( p->sequence, VIMS_VIDEO_SKIP_FRAME, NULL,NULL );
}

static	void	seq_speed( GtkWidget *w, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;

	if(p->status_lock)
		return;
	gdouble value = GTK_ADJUSTMENT( GTK_RANGE(w)->adjustment )->value;
	gint speed = (gint) value;
	if(p->active && speed != 0)
		veejay_sequence_send( p->sequence, VIMS_VIDEO_SET_SPEED, "%d", speed );
}

static	void	seq_opacity( GtkWidget *w, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;

	if(p->status_lock)
		return;
	gdouble value = GTK_ADJUSTMENT( GTK_RANGE(w)->adjustment )->value;
	gint opacity = (gint)( value * 255.0);
	if(p->active)
		veejay_sequence_send( p->sequence, VIMS_CHAIN_MANUAL_FADE, "%d %d",0, opacity );
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

static	void	add_buttons( mt_priv_t *p, sequence_view_t *seqv , GtkWidget *w)
{
	int i;
	for( i = 0; i < FIRST_ROW_END;i ++ )
	{
		char path[1024];
		bzero(path,1024);
		get_gd(path,NULL, button_template_t[i].file );
		seqv->icons[i] = gtk_image_new_from_file( path );
		seqv->buttons[i] = gtk_button_new_with_label(" ");
		gtk_widget_set_size_request( seqv->icons[i],24,20 );
		
		gtk_button_set_image( GTK_BUTTON(seqv->buttons[i]), seqv->icons[i] );
		gtk_widget_set_size_request( seqv->buttons[i],24,20 );
	//	gtk_container_add( GTK_CONTAINER( w ), seqv->buttons[i] );
		gtk_box_pack_start( GTK_BOX(w), seqv->buttons[i], TRUE,TRUE, 0 );
		g_signal_connect( G_OBJECT( seqv->buttons[i] ), "clicked", G_CALLBACK( button_template_t[i].f),
				(gpointer*)p );		
		gtk_widget_show( seqv->buttons[i] );

	}
//	gtk_widget_set_sensitive_( w, FALSE );
}
static	void	add_buttons2( mt_priv_t *p, sequence_view_t *seqv , GtkWidget *w)
{
	int i;
	for( i = FIRST_ROW_END; button_template_t[i].name != NULL ;i ++ )
	{
		char path[1024];
		bzero(path,1024);
		get_gd(path,NULL, button_template_t[i].file );
		seqv->icons[i] = gtk_image_new_from_file( path );
		seqv->buttons2[i] = gtk_button_new_with_label(" ");
		gtk_widget_set_size_request( seqv->icons[i],24,20 );
		
		gtk_button_set_image( GTK_BUTTON(seqv->buttons2[i]), seqv->icons[i] );
		gtk_widget_set_size_request( seqv->buttons2[i],24,20 );
		gtk_box_pack_start( GTK_BOX(w), seqv->buttons2[i], TRUE,TRUE, 0 );
		g_signal_connect( G_OBJECT( seqv->buttons2[i] ), "clicked", G_CALLBACK( button_template_t[i].f),
				(gpointer*)p );		
		gtk_widget_show( seqv->buttons2[i] );

	}
//	gtk_widget_set_sensitive_( w, FALSE );
}

static	void	update_pos( mt_priv_t *p, gint total, gint current )
{
//	timeline_set_pos( p->view->timeline_, current );
   	gtk_adjustment_set_value(
                GTK_ADJUSTMENT(GTK_RANGE(p->view->timeline_)->adjustment), 1.0 / (gdouble) total * current );     

	char *now = format_time( current ,25.0f);
	gtk_label_set_text( p->view->labels_[0], now );
	g_free(now);
}
static	void	update_speed( mt_priv_t *p, gint speed )
{
	gtk_adjustment_set_value( GTK_ADJUSTMENT( GTK_RANGE( p->view->sliders_[0] )->adjustment), (gdouble) speed );
}

static	gboolean	update_track_list( mt_priv_t *p )
{
	if(p->active)
	{	
		int len = 0;
		gchar *buf = veejay_sequence_get_track_list( p->sequence, 5, &len );
		int i = 0;
		int it = 0;
		char *ptr = buf;
	// clear existing buffer
		for( i = 0; i < MAX_TRACKS ; i ++ )
		{
			if( p->tracks[i] )
				free(p->tracks[i]);
			p->tracks[i] = NULL;
		}

		if( !buf )
			return FALSE;

		i = 0;	
		while( i < len )
		{
			int dlen = 0;
			char tmp_len[4];
			bzero( tmp_len, 4 );
			strncpy( tmp_len, ptr , 3 );
			sscanf( tmp_len , "%d", &dlen );
			if(dlen>0)
			{
				ptr += 3;
				p->tracks[it] = strndup( ptr, dlen );
				it++;

				ptr += dlen;
			}
			i += ( 3 + dlen );
		}
		free( buf );
		return TRUE;
	}
	return FALSE;
}



static	void	playmode_sensitivity( mt_priv_t *p, gint pm )
{
	if( pm == MODE_STREAM )
	{
		gtk_widget_set_sensitive_( GTK_WIDGET( p->view->button_box2 ), FALSE );
		gtk_widget_set_sensitive_( GTK_WIDGET( p->view->button_box ), FALSE );
		gtk_widget_set_sensitive_( GTK_WIDGET( p->view->sliders_[0] ), FALSE );
		gtk_widget_set_sensitive_( GTK_WIDGET( p->view->timeline_ ), FALSE );
		gtk_widget_set_sensitive_( GTK_WIDGET( p->view->sliders_[1] ), TRUE );
	}
	else
	{
		if( pm == MODE_SAMPLE || pm == MODE_PLAIN )
		{
			gtk_widget_set_sensitive_( GTK_WIDGET( p->view->button_box2 ), TRUE );
			gtk_widget_set_sensitive_( GTK_WIDGET( p->view->button_box ), TRUE );
			gtk_widget_set_sensitive_( GTK_WIDGET( p->view->sliders_[0] ), TRUE );
			gtk_widget_set_sensitive_( GTK_WIDGET( p->view->timeline_ ), TRUE );
		}
		if( pm == MODE_SAMPLE )
			gtk_widget_set_sensitive_( GTK_WIDGET( p->view->sliders_[1] ), TRUE );
		else
			gtk_widget_set_sensitive_( GTK_WIDGET( p->view->sliders_[1] ), FALSE );
	}
}

static	void	update_widgets(int *status, mt_priv_t *p, int pm)
{
	int *h = p->history[pm];
//	gdk_threads_enter();
//	mt_update_gui(status);	
	if( h[PLAY_MODE] != pm )
	playmode_sensitivity( p, pm );

	if( pm == MODE_SAMPLE || pm == MODE_PLAIN )
	{
		if( h[FRAME_NUM] != status[FRAME_NUM] )
			update_pos( p, status[TOTAL_FRAMES],status[FRAME_NUM] );
		if( h[SAMPLE_SPEED] != status[SAMPLE_SPEED] )
			update_speed( p, status[SAMPLE_SPEED] );
	}
	if( h[TOTAL_SLOTS] != status[TOTAL_SLOTS])
	{
		if(update_track_list( p ))
			update_track_view( MAX_TRACKS, get_track_tree( p->view->tracks ), (void*)p );
	}
	
//	gdk_threads_leave();
}

static gboolean	update_sequence_widgets( gpointer data )
{
	mt_priv_t *p = (mt_priv_t*) data;
	char status[108];
	int  array[101];

	if( !p->active )
		return TRUE;
	p->status_lock = 1;
	veejay_get_status( p->sequence, status );
	int n = status_to_arr( status, array );
	if( n<= 0 )
	{
		p->status_lock = 0;
		return TRUE;
	}
#ifdef STRICT_CHECKING
	assert( n == 20 );
#endif

	int pm = array[PLAY_MODE];
	int i;
	for( i  =  0; i < 20; i ++ )
		p->status_cache[i] = array[i];

	
	update_widgets(array, p, pm);

	int *his = p->history[ pm ];	
	for( i  =  0; i < 20; i ++ )
		his[i] = array[i];
	p->status_lock = 0;
	return TRUE;
}

static		int	free_slot(void *data)
{
	int i = 0;
	all_priv_t *pt = (all_priv_t*)data;
	for( i = 0; i < MAX_TRACKS; i ++ )
	{ 
		mt_priv_t *p = pt->pt[i];
		if(p->used == 0)
			return i;
	}
	return -1;
}

static void	store_data( void *data, int index,char *hostname, int port_num )
{
	all_priv_t *pt = (all_priv_t*) data;
	if(pt->pt[index]->hostname) free(pt->pt[index]->hostname);
	pt->pt[index]->hostname = strdup(hostname);
	pt->pt[index]->port_num = port_num;
}

static	void	free_data( mt_priv_t *p  )
{
	sequence_view_t *v = p->view;
	void 		*b = p->backlink;
	int		 n = p->num;
	
	if( p->timeout )
		g_source_remove( p->timeout );
	p->timeout = 0;

	veejay_abort_sequence( p->sequence );

	if( p->hostname )
		free( p->hostname );
	
	memset( p, 0,sizeof(mt_priv_t));
	p->view = v;
	p->backlink = b;
	p->num = n;
}

static	void	delete_data( void *data, int index )
{
	all_priv_t *pt = (all_priv_t*)data;
	char track_title[100];
	mt_priv_t *p = pt->pt[index];

	if(p->used)
	{
//		gtk_widget_set_sensitive_( GTK_WIDGET(p->view->toggle), FALSE );
		gtk_widget_set_sensitive_( GTK_WIDGET(p->view->panel),FALSE);
		if(index!=LAST_TRACK) set_logo( p->view->area );
		free_data( p );
	}
	
}

static	GdkPixbuf	*load_logo_image( )
{
	char path[1024];
	bzero(path,1024);
	get_gd(path,NULL, "veejay-logo.png");
	return gdk_pixbuf_new_from_file( path,NULL );
}

static	void	set_logo(GtkWidget *area)
{
	GdkPixbuf *buf2 = gdk_pixbuf_scale_simple( logo_img_,preview_width_,preview_height_, GDK_INTERP_BILINEAR );
	gtk_image_set_from_pixbuf_( GTK_IMAGE(area), buf2 );
	gdk_pixbuf_unref( buf2 );
}

static	sequence_view_t	*get_sequence_view(void *data,int index)
{
	all_priv_t *pt = (all_priv_t*)data;
	return pt->pt[index]->view;
}


static		int	mt_new_connection_dialog(multitracker_t *mt, char *hostname,int len, int *port_num)
{
	GtkWidget *dialog = gtk_dialog_new_with_buttons( 
				"New Track",
				GTK_WINDOW( mt->main_box ),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_STOCK_CANCEL,
				GTK_RESPONSE_REJECT,	
				GTK_STOCK_OK,
				GTK_RESPONSE_ACCEPT,
				NULL );

	
	GtkWidget *text_entry = gtk_entry_new();
	gtk_entry_set_text( GTK_ENTRY(text_entry), "localhost" );
	gtk_editable_set_editable( GTK_ENTRY(text_entry), TRUE );
	gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_REJECT );
	gtk_window_set_resizable( GTK_WINDOW( dialog ), FALSE );
	gint   base = 3490;
	gint   dport = base + (1000 * num_tracks_active( mt ));

	GtkObject *adj = gtk_adjustment_new( dport,1024,65535,5,10,0);
	GtkWidget *num_entry = gtk_spin_button_new( adj, 5.0, 0 );

	GtkWidget *text_label = gtk_label_new( "Hostname" );
	GtkWidget *num_label  = gtk_label_new( "Port" );
	g_signal_connect( G_OBJECT(dialog), "response",
			G_CALLBACK( gtk_widget_hide ), G_OBJECT( dialog ) );

	GtkWidget *vbox = gtk_vbox_new( FALSE, 4 );
	gtk_container_add( GTK_CONTAINER( vbox ), text_label );
	gtk_container_add( GTK_CONTAINER( vbox ), text_entry );
	gtk_container_add( GTK_CONTAINER( vbox ), num_label );
	gtk_container_add( GTK_CONTAINER( vbox ), num_entry );
	gtk_container_add( GTK_CONTAINER( GTK_DIALOG(dialog)->vbox), vbox );
	gtk_widget_show_all( dialog );

	gint res = gtk_dialog_run( GTK_DIALOG(dialog) );

	if( res == GTK_RESPONSE_ACCEPT )
	{
		gchar *host = gtk_entry_get_text( GTK_ENTRY( text_entry ) );
		gint   port = gtk_spin_button_get_value( GTK_SPIN_BUTTON(num_entry ));
		strncpy( hostname, host, len );
		*port_num = port;
	}

	gtk_widget_destroy( dialog );

	return res;
}

void		setup_geometry( int w, int h, int n_tracks,int pw, int ph )
{
	LAST_TRACK = n_tracks + 1;
	MAX_TRACKS = n_tracks;
	sta_w = pw;
	sta_h = ph;
}

void		multitrack_set_framerate( float fps )
{
	fps_ = fps;
}

void		multitrack_configure_preview(int w, int h, int hw, int hh, float fps )
{
	preview_width_ = w;
	preview_height_ = h;
	mpreview_width_ = hw;
	mpreview_height_ = hh;
	fps_ = fps;
}

void		*multitrack_new(
		void (*f)(int,char*,int),
		int (*g)(GdkPixbuf *, GdkPixbuf *, GtkImage *),
		GtkWidget *win,
		GtkWidget *box,
		GtkWidget *msg,
		gint max_w,
		gint max_h,
		GtkWidget *main_preview_area)
{
	multitracker_t *mt = NULL;
	all_priv_t *pt = NULL;

	_Xdebug = 1;

	
	logo_img_ = load_logo_image();
	
	mt = (multitracker_t*) malloc(sizeof(multitracker_t));
	memset( mt, 0, sizeof(multitracker_t));

	mt->main_window = win; 
	mt->main_box    = box;
	mt->status_bar = msg; 
	gui_cb = f;
	img_cb = g;

	pt = (all_priv_t*) malloc(sizeof(all_priv_t));
	memset(pt,0,sizeof(pt));

#ifdef STRICT_CHECKING
	assert( preview_width_ != 0 );
	assert( preview_height_ !=  0 );
	assert( max_w > 0 && max_w < 1024 );
	assert( max_h > 0 && max_h < 1024 );
#endif
	
	mt->scroll = gtk_scrolled_window_new(NULL,NULL);

	int minw = preview_width_ + 50;
	if(minw < 240)
		minw = 240;

	GtkRequisition req;
	gtk_widget_size_request( mt->main_window, &req );
	gtk_widget_set_size_request(mt->scroll,minw, req.height-100);

	gtk_container_set_border_width(GTK_CONTAINER(mt->scroll),2);
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(mt->scroll),GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS );
	GtkWidget *table = gtk_table_new( 1, MAX_TRACKS, FALSE );
	gtk_box_pack_start( GTK_BOX( mt->main_box ), mt->scroll , FALSE,FALSE, 0 );
	gtk_widget_show(mt->scroll);

	int c = 0;
	for( c = 0; c < MAX_TRACKS; c ++ ) 
	{
		mt_priv_t *p = (mt_priv_t*) malloc(sizeof( mt_priv_t));
		memset( p, 0, sizeof(mt_priv_t));
		p->num = c;
		p->view = new_sequence_view( p,0,0,0, main_preview_area );
		p->backlink = (void*) mt;
		p->tracks[c] = NULL;
		pt->pt[c] = p;
//		gtk_table_attach_defaults( table, p->view->event_box, c, c+1, 0, 1 );
		gtk_table_attach_defaults( table, p->view->event_box, 0, 1, c, c+1 );
		
		restore__[c] = 0;

	}

	max_w = 352;
	max_h = 288;

	gtk_scrolled_window_add_with_viewport(
			GTK_SCROLLED_WINDOW( mt->scroll ), table );

	gtk_widget_show(table);


	mt_priv_t *lt =  (mt_priv_t*) malloc(sizeof( mt_priv_t));
	memset( lt, 0, sizeof(mt_priv_t));
	lt->num = LAST_TRACK;
	lt->backlink = (void*) mt;
	lt->view = new_sequence_view( lt, 0, 0,1 , main_preview_area);
	pt->pt[LAST_TRACK] = lt;
	gtk_container_add( GTK_CONTAINER( mt->main_box ), lt->view->event_box );
	mt->data = (void*) pt;

	GError *err = NULL;
	pt->thread = g_thread_create( (GThreadFunc) mt_preview, (gpointer*) mt ,FALSE,&err);
	if(!pt->thread)
	{
		status_print( mt, "%s while starting image thread", err->message);
		return NULL;
	}

	

	return (void*) mt;
}


void		multitrack_open(void *data)
{
	multitracker_t *mt = (multitracker_t*) data;
	all_priv_t *a = (all_priv_t*)mt->data;

	G_LOCK(mt_lock);
	mt->sensitive = 1;
	int i;
	for( i = 0; i < MAX_TRACKS ; i ++ )
	{
		mt_priv_t *p = a->pt[i];
		p->preview = restore__[i];
	}

	G_UNLOCK(mt_lock);
}

void		multitrack_close( void *data )
{
	multitracker_t *mt = (multitracker_t*) data;
	all_priv_t *a = (all_priv_t*)mt->data;

	G_LOCK(mt_lock);
	mt->sensitive = 0;
	int i;
	for( i = 0; i < MAX_TRACKS ; i ++ )
	{
		mt_priv_t *p = a->pt[i];
		restore__[i] = p->preview;
	}
	G_UNLOCK(mt_lock);
}

void		multitrack_quit( void *data )
{
	multitracker_t *mt = (multitracker_t*) data;

	G_LOCK(mt_lock);
	mt->quit = 1;
	G_UNLOCK(mt_lock);
	all_priv_t *a = (all_priv_t*) mt->data;
	int i;
	for( i = 0; i <MAX_TRACKS; i ++ )
	{
		mt_priv_t *p = a->pt[i];
		if(p) free_data(p);
	}
}

int		multitrack_add_track( void *data )
{
	multitracker_t *mt = (multitracker_t*) data;
	if(!mt)
		return 0;
	int res = 0;
	// open input dialog, query hostname and postnum etc
	char *hostname = g_new0(char , 100 );
	int   port_num = 0;
	if( mt_new_connection_dialog( mt, hostname, 100, &port_num ) == GTK_RESPONSE_ACCEPT )
	{
		G_LOCK(mt_lock);
		all_priv_t *pt = (all_priv_t*)mt->data;
		int track = free_slot( mt->data );
		if( track == -1 )
		{	
			status_print(mt, "No free Tracks available!");
			g_free(hostname);
			G_UNLOCK(mt_lock);	
			return 0;
		}

		int i;
		int found = 0;
		for( i = 0; i < MAX_TRACKS ; i ++ )
		{
			mt_priv_t *p = pt->pt[i];
			if(p->active)
			{
				if(strncasecmp(hostname,p->hostname,strlen(hostname)) == 0 && port_num == p->port_num )
				{
					found = 1;	
					break;	
				}
			}
		}

		void *seq = NULL;
		if(found)
		{
			status_print( mt, "Track %d: '%s' '%d' already in Track %d\n",
				track, hostname,port_num, i );
			g_free(hostname);
			G_UNLOCK( mt_lock );
			return 0;
		}
		seq = veejay_sequence_init( port_num, hostname, mpreview_width_, mpreview_height_, fps_  );
		if(seq == NULL )
		{
			status_print( mt, "Error while connecting to '%s' : '%d'", hostname, port_num );
			g_free(hostname);
			pt->pt[track]->sequence = NULL;
			pt->pt[track]->active = 0;
			pt->pt[track]->used = 0;
			G_UNLOCK( mt_lock );
			return 0;
		}
		veejay_configure_sequence( seq, preview_width_, preview_height_ );
		pt->pt[track]->sequence = seq;
		pt->pt[track]->active = 1;	
		pt->pt[track]->used = 1;
		store_data( mt->data, track, hostname, port_num );
		mt_priv_t *p = pt->pt[track];
		//	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( p->view->toggle ), 1 );
		status_print( mt, "Track %d: Connection established with '%s' port %d\n",
				track, hostname, port_num );
		pt->pt[track]->timeout = 
			gtk_timeout_add( 300, update_sequence_widgets, (gpointer*) pt->pt[track] );
		
		gtk_widget_set_sensitive_( GTK_WIDGET(p->view->panel),TRUE);
		res = 1;
	}
	g_free(hostname);
	G_UNLOCK(mt_lock);
	return res;
}

void		multitrack_close_track( void *data )
{
	multitracker_t *mt = (multitracker_t*) data;

}

int		multrack_audoadd( void *data, char *hostname, int port_num )
{
	multitracker_t *mt = (multitracker_t*) data;
	all_priv_t *a = (all_priv_t*)mt->data;
	G_LOCK(mt_lock);
	int track = free_slot( mt->data );
	void *seq = veejay_sequence_init( port_num, hostname, mpreview_width_, mpreview_height_ , fps_ );
			
	if(seq == NULL )
	{
		status_print( mt, "Error while connecting to '%s' : '%d'", hostname, port_num );
		g_free(hostname);
		a->pt[track]->sequence = NULL;
		a->pt[track]->active = 0;
		a->pt[track]->used = 0;
		G_UNLOCK(mt_lock);
		return 0;
	}
	
	a->pt[track]->timeout = gtk_timeout_add( 300, update_sequence_widgets, (gpointer*) a->pt[track] );
	veejay_configure_sequence( seq, preview_width_, preview_height_ );

	a->pt[track]->sequence = seq;
	a->pt[track]->active = 1;	
	a->pt[track]->used = 1;

	store_data( mt->data, track, hostname, port_num );


	multitrack_set_current( data, hostname, port_num , mpreview_width_, mpreview_height_);
//	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( a->pt[track]->view->toggle ), 1 );
	status_print( mt, "Track %d: Connection established with '%s' port %d\n",
		track, hostname, port_num );
	gtk_widget_set_sensitive_( GTK_WIDGET(a->pt[track]->view->panel),TRUE);
//	gtk_widget_set_sensitive_( GTK_WIDGET(a->pt[track]->view->toggle),TRUE);

	G_UNLOCK(mt_lock);
	return track;
}

int		multitrack_autoload( void *data, char *hostname, int port_num )
{
	multitrack_set_current( data, hostname, port_num , mpreview_width_, mpreview_height_);

	return 1;
}
void		multitrack_set_current2( void *data, char *hostname, int port_num , int width, int height)
{
}

static int	num_tracks_active( multitracker_t * mt )
{
	int i;
	int sum = 0;
	all_priv_t *a = (all_priv_t*) mt->data;
	for( i = 0; i < MAX_TRACKS ; i ++ )
	{
		mt_priv_t *p = a->pt[i];
		if(p->active)
			sum ++;
	}
	return sum;
}

static int	find_track( multitracker_t *mt, const char *host, int port )
{
	int i;
	all_priv_t *a = (all_priv_t*) mt->data;
	if(!host || port <= 0 || port > 65535 )
		return -1;

	for( i = 0; i < MAX_TRACKS ; i ++ )
	{
		mt_priv_t *p = a->pt[i];
		if(p->active)
		{
			if(strncasecmp( p->hostname, host, strlen(host)) == 0 && port ==
				p->port_num )
				return i;
		}
	}
	return -1;
}

static	int	find_sequence( all_priv_t *a )
{
	mt_priv_t *c = a->pt[LAST_TRACK];
	if(!c->active)
		return -1;
	int i;
	for( i= 0; i < MAX_TRACKS; i ++ )
	{//	if( c == a->pt[i]->sequence ) return i;
		if(a->pt[i]->active)
		{
			if(strncasecmp(c->hostname, a->pt[i]->hostname, strlen(a->pt[i]->hostname) ) == 0 &&
			c->port_num == a->pt[i]->port_num )
			return i;
		}
	}
	return -1;
}

static	int	find_vtrack_id( mt_priv_t *p, mt_priv_t *q, int *got_tag_id )
{
	if(!q->active)
		return -1;
	if(!p->active)
		return -1;
	char hostname[255];
	int  port_num = 0;
	int  tag_id = 0;
	int i;
	for( i = 0; i < MAX_TRACKS; i++ )
	{
		if(p->tracks[i])
		{
			char *str = p->tracks[ i ];
			if(sscanf(str, "%s %d %d", hostname, &port_num, &tag_id ))
			{
				if(strncasecmp( q->hostname,hostname,strlen(hostname) ) == 0 &&
					port_num == q->port_num )
				{
					*got_tag_id = tag_id;
					return i;
				}				
			}
		}
	}
	return -1;
}

void		multitrack_release_track(void *data, int id, int release_this )
{
	char hostname[255];
	int  port_num = 0;
	int  tag_id = 0;
		
	if( release_this < 0 || release_this > MAX_TRACKS )
		return;

	if( id < 0 || id > MAX_TRACKS )
		return;
	G_LOCK(mt_lock);

	multitracker_t *mt = (multitracker_t*) data;
	all_priv_t *a = (all_priv_t*) mt->data;
	mt_priv_t *p = a->pt[id];
	mt_priv_t *q = a->pt[release_this];

	int stream_id = find_vtrack_id( p,q, &tag_id );
	if( stream_id >= 0 )
		veejay_sequence_send( p->sequence , VIMS_STREAM_DELETE, "%d", tag_id);
	G_UNLOCK(mt_lock);
}

void		multitrack_bind_track( void *data, int id, int bind_this )
{
	if( bind_this < 0 || bind_this > MAX_TRACKS )
		return;

	if( id < 0 || id > MAX_TRACKS )
		return;


	G_LOCK(mt_lock);

	multitracker_t *mt = (multitracker_t*) data;
	all_priv_t *a = (all_priv_t*) mt->data;
	mt_priv_t *p = a->pt[id];

	mt_priv_t *q = a->pt[bind_this];

	if(!q->active)
	{
		G_UNLOCK(mt_lock);
		status_print(mt,"Track %d is empty\n", bind_this );
		return;
	}
	if(!p->active)
	{
		//@@@ fatal
		G_UNLOCK(mt_lock);
		return;
	}

	if( strncasecmp( q->hostname, p->hostname, strlen(q->hostname)) == 0 &&
		q->port_num == p->port_num )
	{
		G_UNLOCK(mt_lock);
		status_print(mt, "Track %d: Cannot bind to myself", bind_this);
		return;
	}

	// connect q to p
	veejay_sequence_send( p->sequence, VIMS_STREAM_NEW_UNICAST, "%d %s", q->port_num,q->hostname );
	
	G_UNLOCK(mt_lock);

	status_print(mt, "Veejay '%s:%d' retrieving frames from Veejay '%s:%d",
		p->hostname,p->port_num,q->hostname,q->port_num );

}

void		multitrack_set_preview_speed( void *data , double value )
{
	multitracker_t *mt = (multitracker_t*) data;
	all_priv_t *a = (all_priv_t*) mt->data;
	mt_priv_t *lt = a->pt[LAST_TRACK];
	if(lt->active)
		veejay_sequence_preview_delay( lt->sequence, value );
}

// set_current opens Main preview
void		multitrack_set_current( void *data, char *hostname, int port_num , int width, int height)
{
	multitracker_t *mt = (multitracker_t*)data;

	all_priv_t *a = (all_priv_t*) mt->data;
	mt_priv_t *last_track = a->pt[LAST_TRACK];
	if( last_track->active )
	{
		// make sure to reset width/height back to small
		veejay_configure_sequence( last_track->sequence, mpreview_width_, mpreview_height_ );
	}
		
	int id = find_track( mt, hostname, port_num );
	if(id >= 0 )
	{
		last_track->used = 1;
		last_track->active = 1;
		last_track->sequence = a->pt[id]->sequence;
		if(last_track->hostname)
			free(last_track->hostname);
		last_track->hostname = strdup(hostname);
		last_track->port_num = port_num;
#ifdef STRICT_CHECKING
		assert( last_track->sequence != NULL );
#endif
		veejay_configure_sequence( last_track->sequence, preview_width_, preview_height_ );
		gtk_widget_set_size_request( GTK_WIDGET( last_track->view->area ), 360,290 );
	}
	else
	{
		all_priv_t *a = (all_priv_t*) mt->data;
		mt_priv_t *last_track = a->pt[LAST_TRACK];
		last_track->active = 0;
	}
}

void		multitrack_restart(void *data)
{	
}

static	gboolean seqv_mouse_press_event ( GtkWidget *w, GdkEventButton *event, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	multitracker_t *mt = (multitracker_t*) p->backlink;
	all_priv_t *a = (all_priv_t*) mt->data;

	if( p == NULL)
	{
		status_print((multitracker_t*)p->backlink,"Track %d is empty\n", p->num);
		return FALSE;
	}
	if( gveejay_busy() )
		{
			status_print( (multitracker_t*)p->backlink,	
				"Already connecting to Track %d", mt->selected );
			return FALSE;
		}
	if( !p->active  || !p->hostname || !p->port_num )
	{
		status_print((multitracker_t*)p->backlink,"Track %d is not active\n", p->num);
		return FALSE;
	}

//	mt->selected = p->num;

	if(event->type == GDK_2BUTTON_PRESS)
	{
		mt->selected = p->num;
G_LOCK(mt_lock);
		int tmp[MAX_TRACKS],i;
		for( i = 0; i < MAX_TRACKS ; i ++ )
		{
			mt_priv_t *p = a->pt[i];
			tmp[i] = p->preview;
			p->preview = 0;
		}
G_UNLOCK(mt_lock);

		
		gui_cb( 0, strdup(p->hostname), p->port_num );
G_LOCK(mt_lock);
		
		multitrack_set_current( (void*) mt,  p->hostname, p->port_num ,mpreview_width_,mpreview_height_ );
/*
		all_priv_t *a = (all_priv_t*) mt->data;
		mt_priv_t *last_track = a->pt[LAST_TRACK];
		last_track->used = 1;
		last_track->active = 1;
		last_track->sequence = p->sequence;
		assert( last_track->sequence != NULL );
		veejay_configure_sequence( last_track->sequence, 352, 288 );*/
		for( i = 0; i < MAX_TRACKS ; i ++ )
		{
			mt_priv_t *p = a->pt[i];
			p->preview = tmp[i];
		}
G_UNLOCK(mt_lock);
	}
	
	if( event->type == GDK_BUTTON_PRESS )
	{
		status_print((multitracker_t*)p->backlink,"Selected Track %d\n", p->num);
		mt->selected = p->num;
	}
	return FALSE;
}

static void sequence_preview_cb(GtkWidget *widget, gpointer user_data)
{
    mt_priv_t *p = (mt_priv_t*) user_data;
    gint status = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
    veejay_toggle_image_loader( p->sequence, status );
    p->preview = status;
}

static gint seqv_image_expose( GtkWidget *w, gpointer user_data )
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	return FALSE;
}

static	void	sequence_set_current_frame(GtkWidget *w, gpointer user_data)
{
	mt_priv_t *p = (mt_priv_t*) user_data;
	if(!p->status_lock)
	{
	//	gdouble pos = timeline_get_pos( TIMELINE_SELECTION(w) );
		gdouble pos = GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value;
		gint frame = pos * p->status_cache[TOTAL_FRAMES];
		veejay_sequence_send(p->sequence,  VIMS_VIDEO_SET_FRAME, "%d" , frame);
	}
}

static sequence_view_t *new_sequence_view( mt_priv_t *p,gint w, gint h, gint last, GtkWidget *main_area  )
{
	sequence_view_t *seqv = (sequence_view_t*) malloc(sizeof(sequence_view_t));
	memset( seqv, 0,sizeof( sequence_view_t ));


	if(!last || main_area == NULL)
	{
		seqv->event_box = gtk_event_box_new();
		gtk_event_box_set_visible_window( seqv->event_box, TRUE );
		GTK_WIDGET_SET_FLAGS( seqv->event_box, GTK_CAN_FOCUS );
		if( w == 0 && h == 0 )
		g_signal_connect( G_OBJECT( seqv->event_box ),
					"button_press_event",
					G_CALLBACK( seqv_mouse_press_event ),
					(gpointer*) p );
		gtk_widget_show( GTK_WIDGET( seqv->event_box ) );


		gchar *track_title = g_new0( gchar, 20 );
		sprintf(track_title, "Track %d", p->num );
		seqv->frame = gtk_frame_new( track_title );
		g_free(track_title);
		gtk_container_set_border_width( GTK_CONTAINER( seqv->frame) , 1 );
		gtk_widget_show( GTK_WIDGET( seqv->frame ) );
		gtk_container_add( GTK_CONTAINER( seqv->event_box), seqv->frame );
		seqv->main_vbox = gtk_vbox_new(FALSE,0);
		gtk_container_add( GTK_CONTAINER( seqv->frame ), seqv->main_vbox );
		gtk_widget_show( GTK_WIDGET( seqv->main_vbox ) );
		seqv->area = gtk_image_new();
		set_logo( seqv->area );
	}
	else	
		seqv->area = main_area;


/*	g_signal_connect( G_OBJECT( seqv->area ), 
				"expose_event",
				G_CALLBACK( seqv_image_expose ),	
				(gpointer*) p );*/

	if(!last || !main_area)
	{
		gtk_box_pack_start( GTK_BOX(seqv->main_vbox),GTK_WIDGET( seqv->area), FALSE,FALSE,0);
		gtk_widget_set_size_request( seqv->area, w == 0 ? preview_width_ : w, h == 0 ? preview_height_: h  );
	}

	if(!last)
	{
		seqv->panel = gtk_frame_new(NULL);
		seqv->toggle = gtk_toggle_button_new_with_label( "preview" );
		gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(seqv->toggle), FALSE );
		g_signal_connect( G_OBJECT( seqv->toggle ), "toggled", G_CALLBACK(sequence_preview_cb),
				(gpointer*)p );
		gtk_box_pack_start( GTK_BOX(seqv->main_vbox), seqv->toggle,FALSE,FALSE, 0 );
		gtk_widget_show( seqv->toggle );

		GtkWidget *vvbox = gtk_vbox_new(FALSE, 0);

		seqv->button_box = gtk_hbox_new(FALSE,0);
		gtk_box_pack_start( GTK_BOX(vvbox), seqv->button_box ,FALSE,FALSE, 0 );	
		if( w== 0 && h == 0 )
			add_buttons( p,seqv,seqv->button_box );
		gtk_widget_show( seqv->button_box );	
		gtk_container_add( GTK_CONTAINER( seqv->main_vbox ), seqv->panel );

		seqv->button_box2 = gtk_hbox_new(FALSE, 0);
		gtk_box_pack_start( GTK_BOX(vvbox), seqv->button_box2, FALSE,FALSE, 0 );
		if( w== 0 && h == 0)
			add_buttons2( p,seqv,seqv->button_box2 );
		gtk_widget_show( seqv->button_box2 );
		gtk_container_add( GTK_CONTAINER( seqv->panel ), vvbox );
		gtk_widget_show(vvbox);
	
		GtkWidget *box = gtk_vbox_new(FALSE,0);
	//	seqv->timeline_ = timeline_new();
		seqv->timeline_ = gtk_hscale_new_with_range( 0.0,1.0,0.1 );
		gtk_scale_set_draw_value( seqv->timeline_, FALSE );
		gtk_widget_set_size_request( seqv->panel,preview_width_ ,14);
		gtk_widget_show( seqv->panel );
		gtk_box_pack_start( GTK_BOX( box ), seqv->timeline_, FALSE,FALSE, 0 );
		//gtk_container_add( GTK_CONTAINER(seqv->panel), box );
		gtk_box_pack_start( GTK_BOX( vvbox ), box , FALSE,FALSE,0);
	       	gtk_widget_show(seqv->timeline_);
			 g_signal_connect( seqv->timeline_, "value_changed",
       		         (GCallback) sequence_set_current_frame, (gpointer*) p );

	/* tree */
		GtkWidget *scroll = gtk_scrolled_window_new(NULL,NULL);
	//	gtk_srolled_window_set_placement( GTK_SCROLLED_WINDOW(scroll), GTK_CORNER_TOP_RIGHT );
		gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_ETCHED_IN );
		gtk_widget_set_size_request(scroll,30,70);
		gtk_container_set_border_width(GTK_CONTAINER(scroll),0);
		gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );
		GtkWidget *vvvbox = gtk_hbox_new(FALSE,0);
		seqv->tracks = create_track_view(p->num, MAX_TRACKS, (void*) p );
		gtk_tree_view_set_headers_visible( GTK_TREE_VIEW( get_track_tree(seqv->tracks)) , FALSE );
		gtk_widget_set_size_request( get_track_tree(seqv->tracks),20,80 );
		gtk_widget_show(scroll);

		gtk_scrolled_window_add_with_viewport(
			GTK_SCROLLED_WINDOW( scroll ), get_track_tree(seqv->tracks) );
		gtk_widget_show( get_track_tree(seqv->tracks));
		gtk_box_pack_start( GTK_BOX(vvvbox), scroll, TRUE,TRUE, 0);

		GtkWidget *hhbox = gtk_hbox_new(FALSE,0);
		seqv->sliders_[0] = gtk_vscale_new_with_range( -12.0,12.0,1.0 );
		seqv->sliders_[1] = gtk_vscale_new_with_range( 0.0, 1.0, 0.01 );
		gtk_scale_set_digits( GTK_SCALE(seqv->sliders_[1]), 2 );
		g_signal_connect( G_OBJECT( seqv->sliders_[0] ), "value_changed", G_CALLBACK( seq_speed ),
					(gpointer*)p );		
		g_signal_connect( G_OBJECT( seqv->sliders_[1] ), "value_changed", G_CALLBACK( seq_opacity ),
					(gpointer*)p );		

		gtk_box_pack_start( GTK_BOX( hhbox ), seqv->sliders_[0], TRUE, TRUE, 0 );
		gtk_box_pack_start( GTK_BOX( hhbox ), seqv->sliders_[1], TRUE, TRUE, 0 );
		gtk_widget_show( seqv->sliders_[0] );
		gtk_widget_show( seqv->sliders_[1] );
//		gtk_container_add( GTK_CONTAINER( vvvbox ), hhbox );
		gtk_box_pack_start( GTK_BOX(vvvbox), hhbox, TRUE,TRUE, 0 );
		gtk_widget_show( hhbox ); 

		gtk_container_add( GTK_CONTAINER( box ), vvvbox );
		gtk_widget_show( vvvbox );
		gtk_widget_show( box );	

	
		GtkWidget *hbox = gtk_hbox_new(FALSE,0);
		gtk_box_set_spacing( hbox, 10 );
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
	}

	seqv->dim[0] = w;
	seqv->dim[1] = h;

	return seqv;
}

GdkPixbuf 	*dummy_image()
{
	GdkPixbuf *src = logo_img_;
#ifdef STRICT_CHECKING
	assert( logo_img_ != NULL );
#endif
	GdkPixbuf *dst = gdk_pixbuf_copy(src);

/*	float 	val = logo_value_;
	if( val > 2.0 || val <= 0.0)
		logo_step_ *= -1;
	val += logo_step_;
	logo_value_ = val;

	gdk_pixbuf_saturate_and_pixelate(src,dst, val, FALSE );

	g_usleep( 100000 );*/
	g_usleep( 500000 );
	return dst; 
}
			

void 	*mt_preview( gpointer user_data )
{
	multitracker_t *mt = (multitracker_t*) user_data;
	all_priv_t *a = (all_priv_t*) mt->data;
	gint i = 0;
	GdkPixbuf *cache[MAX_TRACKS+2];
	
	GdkPixbuf *nopreview = dummy_image();
	
	long sleepy = 34000;
	for( ;; )
	{
		G_LOCK( mt_lock );
		mt_priv_t *lt = a->pt[LAST_TRACK];
		gint error = 0;
		memset( cache, 0, (MAX_TRACKS+2) * sizeof(GdkPixbuf*));
		if(mt->quit)
		{
			G_UNLOCK( mt_lock );
			break;	
		}

		if(!lt->preview )
		{
			cache[LAST_TRACK] = nopreview;
#ifdef STRICT_CHECKING
			assert( cache[LAST_TRACK] != NULL );
#endif
		}
		else
		{
			cache[LAST_TRACK] = veejay_get_image( lt->sequence, &error );	
			if( error )
			{
				delete_data( mt->data, LAST_TRACK ); 
				cache[LAST_TRACK] = NULL;
			}
		}

		int ref = find_sequence( a );

		if( mt->sensitive) 
		{
			for( i = 0; i < MAX_TRACKS ; i ++ )
		{
			mt_priv_t *p = a->pt[i];
			if( p->active && ref != i && p->preview)
			{
				cache[i] = veejay_get_image(
						p->sequence, 
						&error );
				if( error )
					cache[i] = 0;
			}
		}
		}
		//@ scale image
		
		if( ref >= 0  && lt->preview && cache[LAST_TRACK] ) 
		{
			cache[ref] = gdk_pixbuf_scale_simple(
					cache[LAST_TRACK],
					preview_width_,
					preview_height_,
					GDK_INTERP_NEAREST );
		}


		GdkPixbuf *ir = NULL;
		if(lt->active && cache[LAST_TRACK] && lt->preview)
		{
			ir = gdk_pixbuf_scale_simple( cache[LAST_TRACK],
					352,288,GDK_INTERP_NEAREST );
		}
		G_UNLOCK(mt_lock );
		
		if(lt->preview)
		{
			gdk_threads_enter();
			if( mt->sensitive )
			for( i = 0; i < MAX_TRACKS ; i ++ )
			{
				mt_priv_t *p = a->pt[i];
				if(cache[i])
				{
					GtkImage *image = GTK_IMAGE( p->view->area );
					gtk_image_set_from_pixbuf_( image, cache[i] );
				}
			}

			if(lt->active && ir)
			{
				gtk_image_set_from_pixbuf_( GTK_IMAGE(lt->view->area), ir );
				sleepy = img_cb( cache[LAST_TRACK], ir, GTK_IMAGE( lt->view->area ) );
		//	gtk_widget_queue_draw(GTK_IMAGE( lt->view->area ));
			}

			for( i = 0; i < MAX_TRACKS ; i ++ )
			{
				mt_priv_t *p = a->pt[i];
				if(cache[i])
					gdk_pixbuf_unref(cache[i]);
				cache[i] = NULL;
					
			}
			if(cache[LAST_TRACK])
			{
				gdk_pixbuf_unref(cache[LAST_TRACK]);
				cache[LAST_TRACK] = NULL;
			}
			if( ir ) 
			{
				gdk_pixbuf_unref(ir);	
			}
			ir = NULL;
			gdk_threads_leave();
		}
		g_usleep(sleepy);
		//@ clear our buffer
	}
	
	gdk_pixbuf_unref( nopreview );
	g_thread_exit(NULL);
}



