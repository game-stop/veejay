/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
 *           (C) 2005      Thomas Rheinhold 
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
#include <math.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdarg.h>
#include <glib.h>
#include <errno.h>
#include <glade/glade.h>
#include <gtk/gtk.h>
#include <string.h>
#include <sys/time.h>
#include <libvjnet/vj-client.h>
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <veejay/vims.h>
#include <gveejay-reloaded/vj-api.h>
#include <fcntl.h>
#include <mjpegtools/mpegconsts.h>
#include <mjpegtools/mpegtimecode.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <widgets/cellrendererspin.h>
#include <widgets/gtkknob.h>
#include <libgen.h>
#include <gveejay-reloaded/keyboard.h>
#include <gtk/gtkversion.h>
#include <gdk/gdk.h>
#include <gveejay-reloaded/curve.h>
#include <gveejay-reloaded/multitrack.h>
#include <gveejay-reloaded/common.h>
#include <gveejay-reloaded/utils.h>
#include <libvevo/vevo.h>
#include <veejay/vevo.h>
//if gtk2_6 is not defined, 2.4 is assumed.
#ifdef GTK_CHECK_VERSION
#if GTK_MINOR_VERSION >= 6
  #define HAVE_GTK2_6 1
#endif  
#endif
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

static int	TIMEOUT_SECONDS = 0;

/* Stream type identifiers */

void	vj_gui_set_timeout(int timer)
{
	TIMEOUT_SECONDS = timer;
}

enum
{
	STREAM_NO_STREAM = 0,
	STREAM_RED = 9,
	STREAM_GREEN = 8,
	STREAM_YELLOW = 7,
	STREAM_BLUE = 6,
	STREAM_WHITE = 4,
	STREAM_VIDEO4LINUX = 2,
	STREAM_DV1394 = 17,
	STREAM_NETWORK = 13,
	STREAM_MCAST = 14,
	STREAM_YUV4MPEG = 1,
	STREAM_AVFORMAT = 12,
	STREAM_PICTURE = 5
};

enum
{
	COLUMN_INT = 0,
	COLUMN_STRING0,
	COLUMN_STRINGA ,
	COLUMN_STRINGB,
	COLUMN_STRINGC,
	N_COLUMNS	
};
enum
{
	ENTRY_FXID = 0,
	ENTRY_ISVIDEO = 1,
	ENTRY_NUM_PARAMETERS = 2,
	ENTRY_P0 = 3,
	ENTRY_P1 = 4,
	ENTRY_P2 = 5,
	ENTRY_P3 = 6,
	ENTRY_P4 = 7,
	ENTRY_P5 = 8,
	ENTRY_P6 = 9,
	ENTRY_P7 = 10,
	ENTRY_UNUSED = 11,
	ENTRY_FXSTATUS = 12,
	ENTRY_UNUSED2 = 13,
	ENTRY_SOURCE = 14,
	ENTRY_CHANNEL = 15
};

enum
{
	SL_ID = 0,
	SL_DESCR = 1,
	SL_TIMECODE = 2
};

enum
{
	HINT_CHAIN = 0,
	HINT_EL = 1,
	HINT_MIXLIST = 2,
	HINT_SAMPLELIST = 3,
	HINT_ENTRY = 4,
	HINT_SAMPLE = 5,
	HINT_SLIST = 6,
	HINT_V4L = 7,
	HINT_RECORDING = 8,
	HINT_RGBSOLID = 9,
	HINT_BUNDLES = 10,
	HINT_HISTORY = 11,
	HINT_MARKER = 12,
	HINT_KF = 13,
	NUM_HINTS = 14
};

enum
{
	PAGE_CONSOLE =0,
	PAGE_FX = 3,
	PAGE_EL = 1,
	PAGE_SAMPLEEDIT = 2,
};

typedef struct
{
	int channel;
	int dev;
} stream_templ_t;

enum
{
	V4L_DEVICE=0,
	DV1394_DEVICE=1,
};

typedef struct
{
	int	selected_chain_entry;
	int	selected_el_entry;
	int	selected_vims_entry;
	int	selected_key_mod;
	int	selected_key_sym;
	int	selected_vims_id;
	int	render_record; 
	int	entry_tokens[STATUS_TOKENS];
	int	iterator;
	int	selected_effect_id;
	int	reload_hint[NUM_HINTS];
	gboolean	reload_force_avoid; 	
	int	playmode;
	int	sample_rec_duration;
	int	streams[4096];
	int	recording[2];
	int	selected_mix_sample_id;
	int	selected_mix_stream_id;
	int	selected_rgbkey;
	int	priout_lock;
	int	pressed_key;
	int	pressed_mod;
	int     keysnoop;
	int	randplayer;
	char	*selected_arg_buf;
	int	expected_slots;
	stream_templ_t	strtmpl[2]; // v4l, dv1394
	int	selected_parameter_id; // current kf
} veejay_user_ctrl_t;

typedef struct
{
	float	fps;
	int	num_files;
	int	*offsets;
	int	num_frames;
	int	width;
	int 	height;
} veejay_el_t;

enum 
{
	RUN_STATE_LOCAL = 1,
	RUN_STATE_REMOTE = 2,
};

typedef struct
{
	gint event_id;
	gint params;
	gchar *format;
	gchar *descr;
} vims_t;

typedef struct
{
	gint keyval;
	gint state;
	gchar *args;
	gint event_id;
} vims_keys_t;

static int skin__ = 0;
// Have room for only 2 * 120 samples
//#define NUM_BANKS 20   
//#define NUM_PAGES 10
//#define NUM_SAMPLES_PER_PAGE 12
//#define NUM_SAMPLES_PER_COL 2
//#define NUM_SAMPLES_PER_ROW 6

static	int	NUM_BANKS =	30;
static	int	NUM_PAGES =	16;
static 	int	NUM_SAMPLES_PER_PAGE = 12;
static	int	NUM_SAMPLES_PER_COL = 2;
static	int	NUM_SAMPLES_PER_ROW = 6;

#define MOD_OFFSET 200
#define SEQUENCE_LENGTH 1024
#define MEM_SLOT_SIZE 32

static	vims_t	vj_event_list[VIMS_MAX];
static  vims_keys_t vims_keys_list[VIMS_MAX];

static  int vims_verbosity = 0;
#define   livido_port_t vevo_port_t

static	vevo_port_t *fx_list_ = NULL;
static  vevo_port_t *fx_clipboard_ = NULL;

typedef struct
{
	GtkLabel *title;
	GtkLabel *timecode;
	GtkLabel *hotkey;
//	GtkWidget *edit_button;
	GtkTooltips *tips;
	GtkWidget *image;
	GtkFrame  *frame;
	GtkWidget *event_box;
	GtkWidget *main_vbox;
	GtkWidget *upper_hbox;
	GtkWidget *upper_vbox;
} sample_gui_slot_t;

typedef struct
{
	gint w;
	gint h;
	gdouble fps;
	gint pixel_format;
	gint sampling;
	gint audio_rate;
	gint norm;
	gint sync;
	gint timer;
	gint deinter;
	gchar *mcast_osc;
	gchar *mcast_vims;
	gint osc;
	gint vims;
} config_settings_t;

typedef struct
{
	GtkFrame *frame;
	GtkWidget *image;
	GtkWidget *event_box;
	GtkWidget *main_vbox;
	GdkPixbuf *pixbuf_ref;
	gint	   sample_id;
	gint	   sample_type;
} sequence_gui_slot_t;

typedef struct
{
	gint slot_number;
	gint sample_id;
	gint sample_type;
	gchar *title;
	gchar *timecode;
	gint refresh_image;
	GdkPixbuf *pixbuf;
	guchar *rawdata;
	key_chain_t	*ec;
} sample_slot_t;

typedef struct
{
	gint seq_start;
	gint seq_end;	
	gint w;	
	gint h;
	sequence_gui_slot_t **gui_slot;
	sample_slot_t *selected;
	gint envelope_size;
} sequence_envelope;

typedef struct
{
	sample_slot_t *sample;	
} sequence_slot_t;


typedef struct
{
	gint bank_number;
	gint page_num;
	sample_slot_t **slot;
	sample_gui_slot_t **gui_slot;
} sample_bank_t;

#define WATCHDOG_STATE_ON 0  // disconnected
#define WATCHDOG_STATE_OFF 1  // connected
typedef struct
{
	char	*hostname;
	int	port_num;
	int	state;    // IDLE, PLAYING, RECONNECT, STOPPED 
	int	p_state;
	struct timeval p_time;
	int	w_state; // watchdog state 
	int	w_delay;
} watchdog_t;

typedef struct
{
	GladeXML *main_window;
	vj_client	*client;
	char		status_msg[STATUS_BYTES];
	int		status_tokens[STATUS_TOKENS]; 	/* current status tokens */
	int		*history_tokens[3];		/* list last known status tokens */
	int		status_passed;
	int		status_lock;
	int		slider_lock;
   	int		parameter_lock;
	int		entry_lock;
	int		sample[2];
	int		selection[3];
	gint		status_pipe;
	int 		sensitive;
	int		launch_sensitive;
	struct	timeval	alarm;
	struct	timeval	timer;
	GIOChannel	*channel;
	GdkColormap	*color_map;
	gint		connecting;
	gint		logging;
	gint		streamrecording;
	gint		samplerecording;
	gint		cpumeter;
	gint		cachemeter;
	gint		image_w;
	gint		image_h;
	veejay_el_t	el;
	veejay_user_ctrl_t uc;
	GList		*effect_info;
	GList		*devlist;
	GList		*chalist;
	GList		*editlist;
	GList		*elref;
	long		window_id;
	int		run_state;
	int		play_direction;
	int		load_image_slot;
	GtkNotebook	*sample_bank_pad;	
	GtkWidget	*quick_select;
	sample_bank_t	**sample_banks;
	sample_slot_t	*selected_slot;
	sample_slot_t 	*selection_slot;
	sample_gui_slot_t *selected_gui_slot;
	sample_gui_slot_t *selection_gui_slot;
	sequence_envelope *sequence_view;
	gint		current_sequence_slot;
	GtkKnob		*audiovolume_knob;
//	GtkKnob		*speed_knob;	
	int		image_dimensions[2];
	guchar		*rawdata;
	int		prev_mode;
	GtkWidget	*tl;
	config_settings_t	config;
	int		status_frame;
	int		key_id;
	int		preview_size_w;
	int		preview_size_h;
	GdkColor	*fg_;
	gboolean	key_now;	
	void		*mt;
	watchdog_t	watch;
	int		vims_line;
} vj_gui_t;

enum
{
 	STATE_STOPPED = 0,
	STATE_RECONNECT = 1,
	STATE_PLAYING  = 2,
	STATE_CONNECT	= 3,
	STATE_DISCONNECT = 4,
	STATE_BUSY      = 5,
	STATE_LOADING	= 6,
};

enum
{
	FXC_ID = 0,
	FXC_FXID = 1,
	FXC_FXSTATUS = 2,
	FXC_KF =3, 
	FXC_N_COLS,
};

enum
{
	V4L_NUM=0,
	V4L_NAME=1,
	V4L_SPINBOX=2,
	V4L_BUTTON=3,
};

enum
{
	VIMS_ID=0,
	VIMS_DESCR=1,
	VIMS_KEY=2,
	VIMS_MOD=3,
	VIMS_PARAMS=4,
	VIMS_FORMAT=5,
	VIMS_CONTENTS=6,
};

#define MAX_PATH_LEN 1024
#define VEEJAY_MSG_OUTPUT	4

static	vj_gui_t	*info = NULL;

/* global pointer to the sample-bank */

/* global pointer to the effects-source-list */
static	GtkWidget *effect_sources_tree = NULL;
static 	GtkListStore *effect_sources_store = NULL;
static 	GtkTreeModel *effect_sources_model = NULL;
static int 		num_tracks_ = 0;
static int		default_preview_width_ = 0;
static int		default_preview_height_ = 0;
/* global pointer to the editlist-tree */
static 	GtkWidget *editlist_tree = NULL;
static	GtkListStore *editlist_store = NULL;
static  GtkTreeModel *editlist_model = NULL;	

/* global pointer to the actual selected slot in the sample_bank */

gboolean	is_alive( void );
static	int	get_slider_val(const char *name);
static  void    vj_msg(int type, const char format[], ...);
static  void    vj_msg_detail(int type, const char format[], ...);
static	void	msg_vims(char *message);
static  void    multi_vims(int id, const char format[],...);
static  void 	single_vims(int id);
static	gdouble	get_numd(const char *name);
static void	vj_kf_delete_parameter(int idx);
static void	vj_akf_delete(void);
static void	vj_kf_select_parameter(int id);
static	int	interpolate_parameters(void);
static  int     get_nums(const char *name);
static  gchar   *get_text(const char *name);
static	void	put_text(const char *name, char *text);
static  int     is_button_toggled(const char *name);
static	void	set_toggle_button(const char *name, int status);
static  void	update_slider_gvalue(const char *name, gdouble value );
static  void    update_slider_value(const char *name, gint value, gint scale);
static  void    update_slider_range(const char *name, gint min, gint max, gint value, gint scaled);
//static  void	update_knob_range( GtkWidget *w, gdouble min, gdouble max, gdouble value, gint scaled );
static	void	update_spin_range(const char *name, gint min, gint max, gint val);
static	void	update_spin_incr(const char *name, gdouble step, gdouble page);
//static	void	update_knob_value(GtkWidget *w, gdouble value, gdouble scale );
static	void	update_spin_value(const char *name, gint value);
static  void    update_label_i(const char *name, int num, int prefix);
static	void	update_label_f(const char *name, float val);
static	void	update_label_str(const char *name, gchar *text);
static	void	update_globalinfo();
static	gint	load_parameter_info();
static	void	load_v4l_info();
static	void	reload_editlist_contents();
static  void    load_effectchain_info();
static  void    load_effectlist_info();
static  void    load_samplelist_info(gboolean with_reset_slotselection);
static  void    load_editlist_info();
static	void	disable_widget( const char *name );
static	void	enable_widget(const char *name );
static	void	setup_tree_spin_column(const char *tree_name, int type, const char *title);
static	void	setup_tree_text_column( const char *tree_name, int type, const char *title );
static	void	setup_tree_pixmap_column( const char *tree_name, int type, const char *title );
static	gchar	*_utf8str( const char *c_str );
static gchar	*recv_vims(int len, int *bytes_written);
static	void	disable_widget_by_pointer(GtkWidget *w);
static	void	enable_widget_by_pointer(GtkWidget *w);
static  GdkPixbuf *	update_pixmap_kf( int status );
static  GdkPixbuf *	update_pixmap_entry( int status );
static gboolean
chain_update_row(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,
             gpointer data);
void	get_gd(char *buf, char *suf, const char *filename);
static	void	update_cached_slots(void);
int	resize_primary_ratio_y();
int	resize_primary_ratio_x();
static	void	setup_tree_texteditable_column( const char *tree_name, int type, const char *title, void (*callbackfunction)() );
static	void	update_rgbkey();
static	int	count_textview_buffer(const char *name);
static	void	clear_textview_buffer(const char *name);
static	void	init_recorder(int total_frames, gint mode);
static	void	reload_bundles();
static	void	update_rgbkey_from_slider();
void	vj_launch_toggle(gboolean value);
static	gchar	*get_textview_buffer(const char *name);
static void 	  create_slot(gint bank_nr, gint slot_nr, gint w, gint h);
static void 	  setup_samplebank(gint c, gint r);
static int 	  add_sample_to_sample_banks( int bank_page,sample_slot_t *slot );
static void 	  update_sample_slot_data(int bank_num, int slot_num, int id, gint sample_type, gchar *title, gchar *timecode);
static gboolean   on_slot_activated_by_key (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static gboolean   on_slot_activated_by_mouse (GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static sample_slot_t *vj_gui_get_sample_info(gint which_one, gint mode );
static void 	  add_sample_to_effect_sources_list(gint id, gint type, gchar *title, gchar *timecode);
static void 	  add_sample_to_editlist(guint row_number, gchar *timeline, gchar *fname, gchar *timecode, gchar *gfourcc);
static void 	  set_activation_of_slot_in_samplebank(gboolean activate);
int		gveejay_new_slot(int stream);
static	int	selected_is_playing();
static	int	bank_exists( int bank_page, int slot_num );
static	int	find_bank_by_sample(int sample_id, int sample_type, int *slot );
static int	add_bank( gint bank_num  );
static void    set_selection_of_slot_in_samplebank(gboolean active);
static void    remove_sample_from_slot();
static void create_ref_slots(int envelope_size);
void setup_knobs();
void   free_samplebank(void);
void   reset_samplebank(void);
int   verify_bank_capacity(int *bank_page_, int *slot_, int sample_id, int sample_type );
static void clone_clipboard_entry(void);
static void widget_get_rect_in_screen (GtkWidget *widget, GdkRectangle *r);
static void    preset_from_clipboard(char *mkey);
static void online_update(int keyp, int value);
static  void   update_curve_widget(const char *name);
static void    update_curve_accessibility(const char *name);
static void	vj_preview_draw(void);

void	interrupt_cb();

static struct
{
	const char *name;
} capt_card_set[] = 
{
	{ "v4l_expander" },
	{ "v4l_brightness" },
	{ "v4l_hue"  },
	{ "v4l_contrast" },
	{ "v4l_color" },
	{ "v4l_white" }
};

static	int	no_preview_ = 0;
static int	no_draw_ = 0;


G_LOCK_DEFINE(preview_buffer__);

static	void	lock_preview__()
{
	G_LOCK(preview_buffer__ );
}
static void 	unlock_preview__()
{
	G_UNLOCK(preview_buffer__ );
}

static	void	lock_preview_(const char *f, int line)
{
//	veejay_msg(0, "%s called by %s: %d (%dx%d)",__FUNCTION__,f,line,
//			default_preview_width_,default_preview_height_);
	lock_preview__();
}
static void 	unlock_preview_(const char *f, int line)
{
//	veejay_msg(0, "%s called by %s: %d (%dx%d)", __FUNCTION__, f, line,
//		 	default_preview_width_, default_preview_height_ );
	unlock_preview__();
}

static	void	gtk_image_set_from_pixbuf__( GtkImage *w, GdkPixbuf *p, const char *f, int l )
{
#ifdef STRICT_CHECKING
	veejay_msg(0, "%s called by %s: %d" , __FUNCTION__, f, l );

	assert( GTK_IS_IMAGE(w) );
#endif
	gtk_image_set_from_pixbuf(w, p);
}

static	void	gtk_widget_set_sensitive___( GtkWidget *w, gboolean state, const char *f, int l )
{
#ifdef STRICT_CHECKING
	veejay_msg(0, "%s called by %s: %d", __FUNCTION__, f, l );

	assert( GTK_IS_WIDGET(w) );
#endif
	gtk_widget_set_sensitive(w, state );
}

static	void	select_slot_(int pm, const char *f, int line);
static	void	select_slot__(int pm);

#ifdef STRICT_CHECKING
#define gtk_widget_set_sensitive_( w,p ) gtk_widget_set_sensitive___( w,p,__FUNCTION__,__LINE__ )
#define gtk_image_set_from_pixbuf_(w,p) gtk_image_set_from_pixbuf__( w,p, __FUNCTION__,__LINE__ )
#define lock_preview() lock_preview_( __FUNCTION__,__LINE__ )
#define unlock_preview() unlock_preview_( __FUNCTION__,__LINE__ )
#define	select_slot(a) select_slot_(a,__FUNCTION__,__LINE__ )
#else
#define gtk_widget_set_sensitive_( w,p ) gtk_widget_set_sensitive( w,p )
#define gtk_image_set_from_pixbuf_(w,p) gtk_image_set_from_pixbuf( w,p )
#define lock_preview() lock_preview__()
#define unlock_preview() unlock_preview__()
#define	select_slot(a) select_slot__(a)
#endif

static struct
{
	const char *name;
} gwidgets[] = {
	{"button_sendvims"},
	{"button_087"},
	{"button_086"},
	{"button_081"},
	{"button_082"},
	{"button_080"},
	{"button_085"},
	{"button_084"},
	{"button_083"},
	{"button_084"},
	{"button_088"},
	{"button_samplestart"},
	{"button_sampleend"},
	{"button_fadeout"},
	{"button_fadein"},
	{"button_5_4"},
	{"button_200"},
	{"button_001"},
	{"button_252"},
	{"button_251"},
	{"button_054"}, 
	{"new_colorstream"},
	{"manualopacity"},
	{"button_fadedur"},
	{"vimsmessage"},
	{"frame_fxtree"},
	{"button_clipcopy"},
	{"button_sample_play"},
	{"button_samplelist_open"},
	{"button_samplelist_save"},
	{"button_sample_del"},
	{"samplerand"},
	{"freestyle"},
	{"frame_sampleproperties"},
	{"frame_samplerecord"},
	{"expander10"}, // edl
	{"colorselection"},
	{"frame_streamproperties"},
	{"frame_streamrecord"},
	{"loglinear"},
	{"curve"},
	{"curve_toggleglobal"},
	{"curve_table"},
	{"curve_toggleentry"},
	{"streamnew"},
	{"sampleadd"},	
	{"samplepage"},
	{NULL} 
};
static struct
{
	const char *name;
} videowidgets[] = {
	{"button_sendvims"},
	{"button_087"},
	{"button_086"},
	{"button_081"},
	{"button_082"},
	{"button_080"},
	{"button_085"},
	{"button_084"},
	{"button_083"},
	{"button_084"},
	{"button_088"},
	{"button_samplestart"},
	{"button_sampleend"},
	{NULL}
};

static struct
{
	const char *name;
} plainwidgets[] = 
{
	{"manualopacity"},
	{"loglinear"},
	{"frame_fxtree"},
	{NULL}
};

enum
{
	TC_SAMPLE_L = 0,
	TC_SAMPLE_F = 1,
	TC_SAMPLE_S = 2,
	TC_SAMPLE_M = 3,
	TC_SAMPLE_H = 4,
	TC_STREAM_F = 5,	
	TC_STREAM_M = 6,
	TC_STREAM_H = 7
};

/* Function to see if selected is playing */
static	int	selected_is_playing()
{
	int *history = info->history_tokens[ (info->status_tokens[PLAY_MODE]) ];
	if(!info->selected_slot)
	{
		return 0;
	}
	if(info->selected_slot->sample_id == info->status_tokens[CURRENT_ID]
		&& info->selected_slot->sample_type == info->status_tokens[PLAY_MODE] &&
		history[CURRENT_ID] == info->status_tokens[CURRENT_ID])
	{
		return 1;
	}
	return 0;
}

static	gchar	*_utf8str(const char *c_str)
{
	gsize	bytes_read = 0;
	gsize 	bytes_written = 0;
	GError	*error = NULL;
	if(!c_str)
		return NULL;
	char	*result = (char*) g_locale_to_utf8( c_str, -1, &bytes_read, &bytes_written, &error );

	if(error)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "%s cannot convert [%s] : %s",__FUNCTION__ , c_str, error->message);
		g_free(error);
		if( result )
			g_free(result);
		result = NULL;
	}

	return result;
}

GdkColor	*widget_get_fg(GtkWidget *w )
{
	if(!w)		
		return NULL;
	GdkColor *c = (GdkColor*)vj_malloc(sizeof(GdkColor));
	memset(c, 0, sizeof(GdkColor));
	GtkStyle *s = gtk_widget_get_style( w);
	c->red   = s->fg[0].red;
	c->green = s->fg[0].green;
	c->blue	 = s->fg[0].blue;
	return c;
}

// dirty function to get name or channel
static	int	read_file(const char *filename, int what, void *dst)
{
	int fd = open (filename, O_RDONLY );
	if(fd <= 0)
		return 0;
	char buf[256];
	bzero(buf,256);
	
	int n = read( fd, buf, 256 );
	if(n > 0)
	{
		if(what == 0)
		{
			char *dst_= (char*) dst;
			snprintf(dst_, n, "%s", buf );
			close(fd);
			return n; 
		}
		if(what == 1)
		{
			int	*cha = (int*) dst;
			int 	major=0,minor=0;
			char	delim;
			int 	nt = sscanf( buf, "%2d%c%d",&major,&delim,&minor );
			if(nt > 0)
			{
				*(cha) = minor;
				return 1;
			}
			close(fd);
		}
	}
	close(fd);
	return 0;
}
GtkWidget	*glade_xml_get_widget_( GladeXML *m, const char *name )
{
	GtkWidget *widget = glade_xml_get_widget( m , name );
	if(!widget)
	{
		veejay_msg(0,"%s DANGER:%s",__FUNCTION__,name);
		return NULL;
	}
#ifdef STRICT_CHECKING
	if(widget == NULL )
		printf("Fatal: %s\n",name);
	assert( widget != NULL );
#endif
	return widget;		
}


static void scan_devices( const char *name)
{
	struct stat	v4ldir;
	int	n;
	GtkWidget *tree = glade_xml_get_widget_(info->main_window,name);
	GtkListStore *store;
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_tree_view_get_model
		(GTK_TREE_VIEW(tree));
	store = GTK_LIST_STORE(model);

	// kernel 2.6
	const char *v4lpath = "/sys/class/video4linux/";
	n = stat( v4lpath, &v4ldir );
	if(n < 0) return;

	if( S_ISDIR(v4ldir.st_mode))
	{
		char dirname[20];
		char abspath[1024];
		char filename[1024];
		int i = 0;
		for( i = 0; i < 8 ; i ++ )
		{
			struct stat v4ldev;
			sprintf(dirname, "video%d/", i );
			sprintf(abspath, "%s%s",v4lpath,dirname);
			n = stat(abspath, &v4ldev );
			if(n < 0)
				continue;
 
			if( S_ISDIR( v4ldev.st_mode )) 
			{
				bzero(filename,1024);
				sprintf(filename, "%sname",abspath);
				char devicename[1024];
				bzero(devicename,1024);
				read_file(filename, 0, devicename );

				gdouble gchannel = 1.0; // default to composite
				gchar *thename = _utf8str( devicename );
				gtk_list_store_append( store, &iter);
				gtk_list_store_set(
					store, &iter,
						V4L_NUM, i,
					 	V4L_NAME, thename, 
						V4L_SPINBOX, gchannel, -1);
				g_free(thename);
			}
		}
	}
	else
	{
	// if on linux ,scan proc for devices.
	// on 2.6 , /sys/class/video4linux/video0/dev , name
	// if reading fails, fill a bogus list and test for /dev/video0
		char filename[1024];
		int i;
		for( i = 0; i < 8 ; i ++ )
		{
			sprintf(filename, "/dev/video%d", i);
			struct stat v4lfile;
			int n = stat( filename, &v4lfile );
			if( n > 0)
				continue;
			if( S_ISREG( v4lfile.st_mode ) )
			{
				/* add device*/
			}
		
		}
		
	}

/*	TODO:
		Put DV 1394 device here too !!
		har har must do cleanup
		refactor all gtk tree related code into a more generic
		function set
*/ 
//	const char *dvpath = "/proc/bus/ieee1394/dv";



	gtk_tree_view_set_model(GTK_TREE_VIEW(tree), model );
}

void	on_devicelist_row_activated(GtkTreeView *treeview, 
		GtkTreePath *path,
		GtkTreeViewColumn *col,
		gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	model = gtk_tree_view_get_model(treeview);
	if(gtk_tree_model_get_iter(model,&iter,path))
	{
		gint num = 0;
		gtk_tree_model_get(model,&iter, V4L_NUM, &num, -1);
		if( num == info->uc.strtmpl[V4L_DEVICE].dev )
		{
			multi_vims( VIMS_STREAM_NEW_V4L,"%d %d",
				info->uc.strtmpl[V4L_DEVICE].dev,
				info->uc.strtmpl[V4L_DEVICE].channel );
			gveejay_new_slot(MODE_STREAM);
		}
	}
}

static void	setup_v4l_devices()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_v4ldevices");
	GtkListStore *store = gtk_list_store_new( 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_FLOAT );

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));
	setup_tree_text_column( "tree_v4ldevices", V4L_NUM, "num" );
	setup_tree_text_column( "tree_v4ldevices", V4L_NAME, "Device name");
	setup_tree_spin_column( "tree_v4ldevices", V4L_SPINBOX, "Channel");
	g_signal_connect( tree, "row-activated",
		(GCallback) on_devicelist_row_activated, NULL );

	scan_devices( "tree_v4ldevices" );
	

}



static	gchar*  format_selection_time(int start, int end);

typedef struct
{
	int id;
	int nl;
	long n1;
	long n2;
	int tf;
} el_ref;

typedef struct
{
	int pos;
	char *filename;
	char *fourcc;
	int num_frames;	
} el_constr;

typedef struct {
	int defaults[10];
	int min[10];
	int max[10];
	char description[150];
	int  id;
	int  is_video;
	int num_arg;
	int has_rgb;
} effect_constr;

int   _effect_get_mix(int effect_id)
{
	int n = g_list_length(info->effect_info);
	int i;
	if(effect_id < 0) return -1;
	for(i=0; i <= n; i++)
	{
		effect_constr *ec = g_list_nth_data( info->effect_info, i );
		if(ec != NULL)
		{
			if(ec->id == effect_id) return ec->is_video;
		}
	}
	return 0;
}



int	_effect_get_rgb(int effect_id)
{
	int n = g_list_length(info->effect_info);
	int i;
	if(effect_id < 0) return -1;
	for(i=0; i <= n; i++)
	{
		effect_constr *ec = g_list_nth_data( info->effect_info, i );
		if(ec != NULL)
		{
			if(ec->id == effect_id) return ec->has_rgb;
		}
	}
	return 0;

}

int   _effect_get_np(int effect_id)
{
	int n = g_list_length(info->effect_info);
	int i;
	if(effect_id < 0) return -1;
	for(i=0; i <= n; i++)
	{
		effect_constr *ec = g_list_nth_data( info->effect_info, i );
		if(ec != NULL)
		{
			if(ec->id == effect_id) return ec->num_arg;
		}
	}
	return 0;
}

int	_effect_get_minmax( int effect_id, int *min, int *max, int index )
{
	int n = g_list_length(info->effect_info);
	int i;
	if(effect_id < 0) return 0;
	for(i=0; i <= n; i++)
	{
		effect_constr *ec = g_list_nth_data( info->effect_info, i );
		if(ec != NULL)
		{
			if(ec->id == effect_id)
			{
				if( index > ec->num_arg )
					return 0;
				*min = ec->min[index];
				*max = ec->max[index];
				return 1;
			}
		}
	}
	return 0;

}


char *_effect_get_description(int effect_id)
{
	int n = g_list_length( info->effect_info );
	int i;
	for(i = 0;i <= n ; i++)
	{	
		effect_constr *ec = g_list_nth_data(info->effect_info, i);
		if(ec != NULL)
		{
			if(effect_id == ec->id )
				return ec->description;
		}
	}
	return "<none>";  
}

el_constr *_el_entry_new( int pos, char *file, int nf , char *fourcc)
{
	el_constr *el = g_new( el_constr , 1 );
	el->filename = strdup( file );
	el->num_frames = nf;
	el->pos = pos;
	el->fourcc = strdup(fourcc);
	return el;
}

void	_el_entry_free( el_constr *entry )
{
	if(entry)
	{
		if(entry->filename) free(entry->filename);
		if(entry->fourcc) free(entry->fourcc);
		free(entry);
	}
}

void	_el_entry_reset( )
{
	if(info->editlist != NULL)
	{
		int n = g_list_length( info->editlist );
		int i;
		for( i = 0; i <= n ; i ++)
			_el_entry_free( g_list_nth_data( info->editlist, i ) );
		g_list_free(info->editlist);
		info->editlist=NULL;
	}
}

int		_el_get_nframes( int pos )
{
	int n = g_list_length( info->editlist );
	int i;
	for( i = 0; i <= n ; i ++)
	{ 
		el_constr *el = g_list_nth_data( info->editlist, i );
		if(!el) return 0;
		if(el->pos == pos)
			return el->num_frames;
	}
	return 0;
}

el_ref *_el_ref_new( int row_num,int nl, long n1, long n2, int tf)
{
	el_ref *el = g_new( el_ref , 1 );
	el->id = row_num;
	el->nl = nl;
	el->n1 = n1;
	el->n2 = n2;
	el->tf = tf;
	return el;
}

void	_el_ref_free( el_ref *entry )
{
	if(entry) free(entry);
}

void	_el_ref_reset()
{
	if(info->elref != NULL)
	{
		int i;
		int n = g_list_length( info->elref );
	//	for(i = 0; i < n; i ++ )
		
	//		free( g_list_nth_data( info->elref, i ));
		g_list_free(info->elref);
		info->elref = NULL;
	}
}

int	_el_ref_end_frame( int row_num )
{
	int n = g_list_length( info->elref );
	int i;
	for ( i = 0 ; i <= n; i ++ )
	{
		el_ref *el  = g_list_nth_data( info->elref, i );
		if(el->id == row_num )
		{
//			int offset = info->el.offsets[ el->nl ];
//			return (offset + el->n1 + el->n2 );
			return (el->tf + el->n2 - el->n1);
		}
	}
	return 0;
}
int	_el_ref_start_frame( int row_num )
{
	int n = g_list_length( info->elref );
	int i;
	for ( i = 0 ; i <= n; i ++ )
	{
		el_ref *el  = g_list_nth_data( info->elref, i );
		if(el->id == row_num )
		{
//			int offset = info->el.offsets[ el->nl ];
//			return (offset + el->n1 );
//			printf("Start pos of row %d : %d = n1, %d = n2, %d = tf\n",
//				row_num,el->n1,el->n2, el->tf );
			return (el->tf);
		}
	}
	return 0;
}


char	*_el_get_fourcc( int pos )
{
	int n = g_list_length( info->editlist );
	int i;
	for( i = 0; i <= n; i ++ )
	{
		el_constr *el = g_list_nth_data( info->editlist, i );
		if(el->pos == pos)
			return el->fourcc;
	}
	return NULL;
}


char	*_el_get_filename( int pos )
{
	int n = g_list_length( info->editlist );
	int i;
	for( i = 0; i <= n; i ++ )
	{
		el_constr *el = g_list_nth_data( info->editlist, i );
		if(el->pos == pos)
			return el->filename;
	}
	return NULL;
}

effect_constr* _effect_new( char *effect_line )
{
	effect_constr *ec;
	int descr_len = 0;
	int p;
	int tokens = 0;
	char len[4];
	char line[100];
	int offset = 0;

	bzero( len, 4 );
	bzero( line, 100 );

	if(!effect_line) return NULL;

	strncpy(len, effect_line, 3);
	sscanf(len, "%03d", &descr_len);
	if(descr_len <= 0) return NULL;

	ec = g_new( effect_constr, 1);
	bzero( ec->description, 150 );
	strncpy( ec->description, effect_line+3, descr_len );
	tokens = sscanf(effect_line+(descr_len+3), "%03d%1d%1d%02d", &(ec->id),&(ec->is_video),
		&(ec->has_rgb), &(ec->num_arg));
	offset = descr_len + 10;
	for(p=0; p < ec->num_arg; p++)
	{
		sscanf(effect_line+offset,"%06d%06d%06d",
			&(ec->min[p]), &(ec->max[p]),&(ec->defaults[p]) );
		offset+=18; 
	}
	return ec;
}

void	_effect_free( effect_constr *effect )
{
	if(effect)
	{
		free(effect);
	}

}
void	_effect_reset(void)
{
	if( info->effect_info != NULL)
	{
		int n = g_list_length(info->effect_info);
		int i;
		for( i = 0; i <=n ; i ++ )
			_effect_free( g_list_nth_data( info->effect_info , i ) );
		g_list_free( info->effect_info );
		info->effect_info = NULL;
	}
}

static		gchar *get_relative_path(char *path)
{
	return _utf8str( basename( path ));
}

gchar *dialog_save_file(const char *title )
{
	GtkWidget *parent_window = glade_xml_get_widget_(
			info->main_window, "gveejay_window" );
	GtkWidget *dialog = 
		gtk_file_chooser_dialog_new( title,
				GTK_WINDOW(parent_window),
				GTK_FILE_CHOOSER_ACTION_SAVE,
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				NULL);

	if( gtk_dialog_run( GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *file = gtk_file_chooser_get_filename(
				GTK_FILE_CHOOSER(dialog) );

		gtk_widget_destroy(dialog);
		return file;
	}

	gtk_widget_destroy(dialog);
	return NULL;
}

static	void	clear_progress_bar( const char *name, gdouble val )
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(w), val );
}

static struct
{
	const char *descr;
	const char *filter;
} content_file_filters[] = {
	{ "AVI Files (*.avi)", "*.avi", },
	{ "Digital Video Files (*.dv)", "*.dv" },
	{ "Edit Decision List Files (*.edl)", "*.edl" },
	{ "PNG (Portable Network Graphics) (*.png)", "*.png" },
	{ "JPG (Joint Photographic Experts Group) (*.jpg)", "*.jpg" },
	{ NULL, NULL },
	
};

static void add_file_filters(GtkWidget *dialog, int type )
{
	GtkFileFilter *filter = NULL;
		
	if(type == 0 )
	{
		int i;
		for( i = 0; content_file_filters[i].descr != NULL ; i ++ )
		{
			filter = gtk_file_filter_new();
			gtk_file_filter_set_name( filter, content_file_filters[i].descr);
			gtk_file_filter_add_pattern( filter, content_file_filters[i].filter);
			gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter );
		}
	}
	if(type == 1 )
	{
		filter = gtk_file_filter_new();
		gtk_file_filter_set_name( filter, "Sample List Files (*.sl)");
		gtk_file_filter_add_pattern( filter, "*.sl");
		gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter);	
	}
	if(type == 2 )
	{
		filter = gtk_file_filter_new();
		gtk_file_filter_set_name( filter, "Action Files (*.xml)");
		gtk_file_filter_add_pattern( filter, "*.xml");
		gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter);	
	}
	if(type == 3 )
	{
		//ffmpeg 
	}

	filter = gtk_file_filter_new();
	gtk_file_filter_set_name( filter, "All Files (*.*)");
	gtk_file_filter_add_pattern( filter, "*");
	gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter);	
}


gchar *dialog_open_file(const char *title, int type)
{
	static gchar *_file_path = NULL;


	GtkWidget *parent_window = glade_xml_get_widget_(
			info->main_window, "gveejay_window" );
	GtkWidget *dialog = 
		gtk_file_chooser_dialog_new( title,
				parent_window,
				GTK_FILE_CHOOSER_ACTION_OPEN,
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				NULL);

	add_file_filters(dialog, type );
	gchar *file = NULL;
	if( _file_path )
	{
		gtk_file_chooser_set_current_folder_uri(GTK_FILE_CHOOSER(dialog), _file_path);	
		g_free(_file_path);
	}

	if( gtk_dialog_run( GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		file = gtk_file_chooser_get_filename(
				GTK_FILE_CHOOSER(dialog) );
		_file_path = gtk_file_chooser_get_current_folder_uri(GTK_FILE_CHOOSER(dialog));
	}

	gtk_widget_destroy(GTK_WIDGET(dialog));
	return file;
}



void	about_dialog()
{
    const gchar *artists[] = { 
      "Matthijs v. Henten (glade, pixmaps) <cola@looze.net>", 
      "Dursun Koca (logo)",
      NULL 
    };

    const gchar *authors[] = { 
      "Niels Elburg <nelburg@looze.net>",
      "Thomas Reinhold <stan@jf-chemnitz.de>",
      NULL 
    };

	const gchar *license = 
	{
		"This program is Free Software; You can redistribute it and/or modify\n" \
		"under the terms of the GNU General Public License as published by\n" \
		"the Free Software Foundation; either version 2, or (at your option)\n"\
		"any later version.\n\n"\
		"This program is distributed in the hope it will be useful,\n"\
		"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"\
		"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"\
		"See the GNU General Public License for more details.\n\n"\
		"For more information , see also: http://www.gnu.org\n"
	};

#ifdef HAVE_GTK2_6
	char path[MAX_PATH_LEN];
	bzero(path,MAX_PATH_LEN);
	get_gd( path, NULL,  "veejay-logo.png" );
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file
		( path, NULL );
	GtkWidget *about = g_object_new(
		GTK_TYPE_ABOUT_DIALOG,
		"name", "GVeejay Reloaded",
		"version", VERSION,
		"copyright", "(C) 2004 - 2005 N. Elburg et all.",
		"comments", "Another graphical interface for Veejay",
		"authors", authors,
		"artists", artists,
		"license", license,
		"logo", pixbuf, NULL );
	g_object_unref(pixbuf);

	g_signal_connect( about , "response", G_CALLBACK( gtk_widget_destroy),NULL);
	gtk_window_present( GTK_WINDOW( about ) );
#else
	int i;
	vj_msg_detail( VEEJAY_MSG_INFO, "Gveejay Reloaded %s %d%d%d", VEEJAY_CODENAME,VEEJAY_MAJOR_VERSION,VEEJAY_MINOR_VERSION,VEEJAY_MICRO_VERSION );
	vj_msg_detail( VEEJAY_MSG_INFO, "%s", license );
	for(i = 0; artists[i] != NULL ; i ++ )
		vj_msg_detail( VEEJAY_MSG_INFO, "%s", artists[i] );
	for(i = 0; authors[i] != NULL ; i ++ )
		vj_msg_detail( VEEJAY_MSG_INFO, "%s", authors[i] );
	vj_msg_detail( VEEJAY_MSG_INFO,
		"Copyright (C) 2004 - 2005. N. Elburg et all." );
	vj_msg_detail( VEEJAY_MSG_INFO,
		"GVeejay Reloaded - Another graphical interface for Veejay");

#endif

}

gboolean	dialogkey_snooper( GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
	GtkWidget *entry = (GtkWidget*) user_data;
 	if(gtk_widget_is_focus(GTK_WIDGET(entry)) == FALSE )
	{	// entry doesnt have focus, bye!
		return FALSE;
	}
	
	if(event->type == GDK_KEY_PRESS)
	{
		gchar tmp[100];
		bzero(tmp,100);
		info->uc.pressed_key = event->keyval;
		info->uc.pressed_mod = event->state;
		gchar *text = gdkkey_by_id( event->keyval );
		gchar *mod  = gdkmod_by_id( event->state );

		if( mod != NULL )
		{
			if(strlen(mod) < 2 )
				sprintf(tmp, "%s", text );
			else
				sprintf(tmp, "%s + %s", text,mod);
			gchar *utf8_text = _utf8str( tmp );
			gtk_entry_set_text( GTK_ENTRY(entry), utf8_text);
			g_free(utf8_text);
		}
	}
	
	return FALSE;
}

gboolean	key_handler( GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
	if(!info->key_now)
		return FALSE;	

	if(event->type != GDK_KEY_PRESS)
		return FALSE;

	/* translate GDK keys to veejay's SDL set */
	int gdk_keyval = gdk2sdl_key( event->keyval );
	int gdk_state  = gdk2sdl_mod( event->state );

	if(gdk_keyval == 0)
		return FALSE;

	int i;
	for( i = 0; i <VIMS_MAX; i ++ )
	 if( vims_keys_list[i].keyval == gdk_keyval && vims_keys_list[i].state == gdk_state )
	{
		gchar message[100];
		if( vims_keys_list[i].args != NULL )
			sprintf(message, "%03d:%s;", vims_keys_list[i].event_id, vims_keys_list[i].args );
		else
			sprintf(message, "%03d:;", vims_keys_list[i].event_id );
		msg_vims( message );
//		fprintf(stderr, "Send message '%s'\n", message );
	}


	return FALSE;
}


int
prompt_keydialog(const char *title, char *msg)
{
	char pixmap[512];
	bzero(pixmap,512);
	get_gd( pixmap, NULL, "icon_key.png");

	GtkWidget *mainw = glade_xml_get_widget_(info->main_window, "gveejay_window");
	GtkWidget *dialog = gtk_dialog_new_with_buttons( title,
				GTK_WINDOW( mainw ),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_STOCK_NO,
				GTK_RESPONSE_REJECT,
				GTK_STOCK_YES,	
				GTK_RESPONSE_ACCEPT,
				NULL);



	GtkWidget *keyentry = gtk_entry_new();
	gtk_entry_set_text( GTK_ENTRY(keyentry), "<press a key>");
	gtk_editable_set_editable( GTK_ENTRY(keyentry), FALSE );  
	gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_REJECT );
	gtk_window_set_resizable( GTK_WINDOW(dialog), FALSE );

	g_signal_connect( G_OBJECT(dialog), "response", 
		G_CALLBACK( gtk_widget_hide ), G_OBJECT(dialog ) );

	GtkWidget *hbox1 = gtk_hbox_new( FALSE, 12 );
	gtk_container_set_border_width( GTK_CONTAINER( hbox1 ), 6 );
	GtkWidget *hbox2 = gtk_hbox_new( FALSE, 12 );
	gtk_container_set_border_width( GTK_CONTAINER( hbox2 ), 6 );

	GtkWidget *icon = gtk_image_new_from_file( pixmap );

	GtkWidget *label = gtk_label_new( msg );
	gtk_container_add( GTK_CONTAINER( hbox1 ), icon );
	gtk_container_add( GTK_CONTAINER( hbox1 ), label );
	gtk_container_add( GTK_CONTAINER( hbox1 ), keyentry );

	if( info->uc.selected_vims_entry &&
			(info->uc.selected_key_mod || 
			info->uc.selected_key_sym ))
	{
		char tmp[100];
		sprintf(tmp,"VIMS %d : %s + %s",
			info->uc.selected_vims_entry,
			sdlmod_by_id( info->uc.selected_key_mod ),
			sdlkey_by_id( info->uc.selected_key_sym ) );

		GtkWidget *current = gtk_label_new( tmp );
		gtk_container_add( GTK_CONTAINER( hbox1 ), current );
	
		if( vj_event_list[ info->uc.selected_vims_entry ].params > 0 )
		{
			GtkWidget *arglabel = gtk_label_new("Enter arguments for VIMS");
			GtkWidget *argentry = gtk_entry_new();
			gtk_entry_set_text( 
				GTK_ENTRY(argentry), 
				info->uc.selected_arg_buf
			);
			gtk_container_add( GTK_CONTAINER( hbox2 ), arglabel );
			gtk_container_add( GTK_CONTAINER( hbox2 ), argentry );
		}

	}


	gtk_container_add( GTK_CONTAINER( GTK_DIALOG( dialog )->vbox ), hbox1 );
	gtk_container_add( GTK_CONTAINER( GTK_DIALOG( dialog )->vbox ), hbox2 );

	gtk_widget_show_all( dialog );

	int id = gtk_key_snooper_install( dialogkey_snooper, (gpointer*) keyentry );
	int n = gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_key_snooper_remove( id );
	gtk_widget_destroy(dialog);

	return n;
}

void	
message_dialog( const char *title, char *msg )
{
	GtkWidget *mainw = glade_xml_get_widget_(info->main_window, "gveejay_window");
	GtkWidget *dialog = gtk_dialog_new_with_buttons( title,
				GTK_WINDOW( mainw ),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_STOCK_OK,
				GTK_RESPONSE_NONE,
				NULL);
	GtkWidget *label = gtk_label_new( msg );
	g_signal_connect_swapped( dialog, "response",
			G_CALLBACK(gtk_widget_destroy),dialog);
	gtk_container_add( GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
		label );
	gtk_widget_show_all(dialog);
}


int
prompt_dialog(const char *title, char *msg)
{
	GtkWidget *mainw = glade_xml_get_widget_(info->main_window, "gveejay_window");
	GtkWidget *dialog = gtk_dialog_new_with_buttons( title,
				GTK_WINDOW( mainw ),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_STOCK_NO,
				GTK_RESPONSE_REJECT,
				GTK_STOCK_YES,	
				GTK_RESPONSE_ACCEPT,
				NULL);
	gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_REJECT );
	gtk_window_set_resizable( GTK_WINDOW(dialog), FALSE );
	g_signal_connect( G_OBJECT(dialog), "response", 
		G_CALLBACK( gtk_widget_hide ), G_OBJECT(dialog ) );
	GtkWidget *hbox1 = gtk_hbox_new( FALSE, 12 );
	gtk_container_set_border_width( GTK_CONTAINER( hbox1 ), 6 );
	GtkWidget *icon = gtk_image_new_from_stock( GTK_STOCK_DIALOG_QUESTION,
		GTK_ICON_SIZE_DIALOG );
	GtkWidget *label = gtk_label_new( msg );
	gtk_container_add( GTK_CONTAINER( hbox1 ), icon );
	gtk_container_add( GTK_CONTAINER( hbox1 ), label );
	gtk_container_add( GTK_CONTAINER( GTK_DIALOG( dialog )->vbox ), hbox1 );
	gtk_widget_show_all( dialog );

	int n = gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);

	return n;
}

void		veejay_quit( )
{
        if( prompt_dialog("Quit veejay", "Close Veejay ? All unsaved work will be lost." )
		 == GTK_RESPONSE_REJECT )
                return;
       single_vims( 600 );

	clear_progress_bar( "cpumeter",0.0 );
	clear_progress_bar( "cachemeter", 0.0);
	clear_progress_bar( "connecting",0.0 );
	clear_progress_bar( "samplerecord_progress",0.0 );
	clear_progress_bar( "streamrecord_progress",0.0 );

	exit(0);
}

static	int	running_g_ = 1;

gboolean	gveejay_running()
{
	if(!running_g_)
		return FALSE;
	return TRUE;
}

gboolean	gveejay_quit( GtkWidget *widget, gpointer user_data)
{
	char *text = get_text( "entry_hostname" );

	no_preview_ = 0;
	if(!running_g_)
		return FALSE;

	if( info->watch.state == STATE_PLAYING)
	{
		if( prompt_dialog("Quit gveejay", "Are you sure?" ) == GTK_RESPONSE_REJECT)
			return TRUE;
	}
	multitrack_quit( info->mt );
	
	info->watch.w_state = WATCHDOG_STATE_OFF;
//	g_usleep(40000);
//	vj_gui_free();

	running_g_ = 0;

	return FALSE;
}

/* Free the slot */
static	void	free_slot( sample_slot_t *slot )
{
	if(slot)
	{
		if(slot->title) free(slot->title);
		if(slot->timecode) free(slot->timecode);
		if(slot->ec)
			del_chain( slot->ec );
		free(slot);
	}
}

/* Allocate some memory and create a temporary slot */
sample_slot_t 	*create_temporary_slot( gint slot_id, gint id, gint type, gchar *title, gchar *timecode )
{
	sample_slot_t *slot = (sample_slot_t*) vj_malloc(sizeof(sample_slot_t));
	memset(slot, 0, sizeof(sample_slot_t));
	if(id>0)
	{
		slot->sample_id = id;
		slot->sample_type = type;
		slot->timecode = strdup(timecode);
		slot->title = strdup(title);
		slot->slot_number = slot_id;
	}
	return slot;
}

int	is_current_track(char *host, int port )
{
	char	*remote = get_text( "entry_hostname" );
	int 	num  = get_nums( "button_portnum" );
	if( strncasecmp( remote, host, strlen(host)) == 0 && port == num )
		return 1;
	return 0;
}

/* Create a new slot in the sample bank, This function is called by
   all VIMS commands that create a new stream or a new sample */
int		gveejay_new_slot(int mode)
{
	int id = 0;
	int result_len = 0;
	gchar *result = recv_vims( 3, &result_len );
	if(result_len > 0 )
	{
		sscanf( result, "%d", &id );
		g_free(result);
		if(id > 0 )
			vj_msg(VEEJAY_MSG_INFO, "Created new %s %d", (mode == MODE_SAMPLE ? "Sample" : "Stream"), id);
		else
		{
			char message[100];
			sprintf(message, "Failed to create new %s",
				(mode == MODE_SAMPLE ? "Sample" : "Stream" ) );
			message_dialog( "Error", message );
		}
		if( id > 0 )
		{ /* Add the sample/stream to a sample bank */


			int poke_slot = 0;
			int bank_page = 0;
			if(verify_bank_capacity( &bank_page, &poke_slot, id, mode ))
			{
				sample_slot_t *tmp_slot = vj_gui_get_sample_info(id, mode );
				if(tmp_slot)
				{
					tmp_slot->slot_number = poke_slot;
					add_sample_to_sample_banks( bank_page, tmp_slot );
					free_slot( tmp_slot );
					info->uc.expected_slots =
					 info->status_tokens[TOTAL_SLOTS] + 1;
				}
			}


		}
	}

	return id;
}
#include "callback.c"
enum
{
	COLOR_RED=0,
	COLOR_BLUE=1,
	COLOR_GREEN=2,
	COLOR_BLACK=3,
	COLOR_NUM
};

static	int	line_count = 1;
static  void	vj_msg(int type, const char format[], ...)
{
	if( type == VEEJAY_MSG_DEBUG && vims_verbosity == 0 )
		return;
	
	char tmp[1024];
	char buf[1024];
	char prefix[20];
	va_list args;
	gchar level[6];
	bzero(level,0);	
	bzero(tmp, 1024);
	bzero(buf, 1024);

	va_start( args,format );
	vsnprintf( tmp, sizeof(tmp), format, args );
	
	switch(type)
	{
		case 2: sprintf(prefix,"Info   : ");break;
		case 1: sprintf(prefix,"Warning: ");break;
		case 0: sprintf(prefix,"Error  : ");break;
		case 3:
			sprintf(prefix,"Debug  : ");break;
		case 4:
			sprintf(prefix, " ");break;
	}

	snprintf(buf, sizeof(buf), "%s %s\n",prefix,tmp );
	gsize nr,nw;
        gchar *text = g_locale_to_utf8( buf, -1, &nr, &nw, NULL);
        text[strlen(text)-1] = '\0';
//        put_text( "lastmessage", text );

	GtkWidget *sb = glade_xml_get_widget_( info->main_window, "statusbar");
	gtk_statusbar_push( GTK_STATUSBAR(sb),0, text ); 

	g_free( text );
	va_end(args);
}
static  void	vj_msg_detail(int type, const char format[], ...)
{
	GtkWidget *view = glade_xml_get_widget_( info->main_window,"veejaytext");
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter iter;


	//if( type == VEEJAY_MSG_DEBUG && vims_verbosity == 0 )
	//	return;

	char tmp[1024];
	char buf[1024];
	char prefix[20];
	va_list args;
	gchar level[6];
	bzero(level,0);	
	int color = -1;
	bzero(tmp, 1024);
	bzero(buf, 1024);
	va_start( args,format );
	vsnprintf( tmp, sizeof(tmp), format, args );
	
	gtk_text_buffer_get_end_iter(buffer, &iter);
	
	switch(type)
	{
		case 2: sprintf(prefix,"Info   : ");sprintf(level, "infomsg"); color=COLOR_GREEN; break;
		case 1: sprintf(prefix,"Warning: ");sprintf(level, "warnmsg"); color=COLOR_RED; break;
		case 0: sprintf(prefix,"Error  : ");sprintf(level, "errormsg"); color=COLOR_RED; break;
		case 3:
			sprintf(prefix,"Debug  : ");sprintf(level, "debugmsg"); color=COLOR_BLUE; break;
	}

	if(type==4)
		snprintf(buf,sizeof(buf),"gveejay: %s",tmp);
	else
		snprintf(buf, sizeof(buf), "%s %s\n",prefix,tmp );
	gsize nr,nw;

	gchar *text = g_locale_to_utf8( buf, -1, &nr, &nw, NULL);
	gtk_text_buffer_insert( buffer, &iter, text, nw );

	GtkTextIter enditer;
	gtk_text_buffer_get_end_iter(buffer, &enditer);
	gtk_text_view_scroll_to_iter(
		GTK_TEXT_VIEW(view),
		&enditer,
		0.0,
		FALSE,
		0.0,
		0.0 );
		


	g_free( text );
	va_end(args);
}

static	void	msg_vims(char *message)
{
	if(!info->client)
		return;
	vj_client_send(info->client, V_CMD, message);
}

int	get_loop_value()
{
	if( is_button_toggled( "loop_none" ) )
	{
		return 0;
	}
	else
	{
		if( is_button_toggled( "loop_normal" ))
			return 1;
		else
			if( is_button_toggled( "loop_pingpong" ))
				return 2;
	}
	return 1;
}


static	void	multi_vims(int id, const char format[],...)
{
	char block[1024];
	char tmp[1024];
	va_list args;
	if(!info->client)
		return;
	va_start(args, format);
	vsnprintf(tmp, sizeof(tmp)-1, format, args );
	snprintf(block, sizeof(block)-1, "%03d:%s;",id,tmp);
	va_end(args);
	vj_client_send( info->client, V_CMD, block); 
}

static	void single_vims(int id)
{
	char block[10];
	if(!info->client)
		return;
	sprintf(block, "%03d:;",id);
	vj_client_send( info->client, V_CMD, block );
}

static gchar	*recv_vims(int slen, int *bytes_written)
{
	int tmp_len = slen+1;
	char tmp[tmp_len];
	bzero(tmp,tmp_len);
	int ret = vj_client_read( info->client, V_CMD, tmp, slen );
	int len = 0;
	sscanf( tmp, "%d", &len );
	gchar *result = NULL;
	if( ret <= 0 || len <= 0 || slen <= 0)
	{
		return result;
	}
	result = (gchar*) malloc(sizeof(gchar) * (len + 1) );

	bzero(result, (len+1));

	int bytes_left = len;
	*bytes_written = 0;

	while( bytes_left > 0)
	{
		int n = vj_client_read( info->client, V_CMD, result + (*bytes_written), bytes_left );
		if( n <= 0 )
		{
			bytes_left = 0;
		}
		if( n > 0 )
		{
			*bytes_written +=n;
			bytes_left -= n;
		}
	}
	return result;
}

static gchar	*recv_log_vims(int slen, int *bytes_written)
{
	gchar tmp[slen+1];
	bzero(tmp,slen+1);

	vj_client_read( info->client, V_MSG, tmp, slen );
	int len = atoi(tmp);
	gchar *result = NULL;
	int n = 0;
	if(len > 0)
	{
		result = g_new( gchar, len+1 );
		n = vj_client_read( info->client, V_MSG, result, len );
		*bytes_written = n;
		result[len] = '\0';
	}	
	return result;
}

/*
	read playmode, parse tokens in status_tokens
*/
static	gdouble	get_numd(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name);
	if(!w) return 0;
	return (gdouble) gtk_spin_button_get_value( GTK_SPIN_BUTTON( w ) );
}

static	int	get_slider_val(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) return 0;
	return ((gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value); 
}
static	void	vj_kf_delete_parameter(int idx)
{
	sample_slot_t *s = info->selected_slot;
	if(!s || !s->ec )
		return;

	int i;
	for( i = 0;i < MAX_PARAMETERS; i ++ )
	{
		key_parameter_t *key = 	s->ec->effects[idx]->parameters[i];
  		clear_parameter_values ( key );
	}
}
static	void	vj_akf_delete()
{
	sample_slot_t *s = info->selected_slot;
	if(!s || !s->ec)
		return;
	int i;
	for(i = 0; i < MAX_CHAIN_LEN; i ++)
		vj_kf_delete_parameter(i);
}

static	void	vj_kf_select_parameter(int num)
{
	sample_slot_t *s = info->selected_slot;
	if(!s || !s->ec)
		return;

	info->uc.selected_parameter_id = num;
#ifdef STRICT_CHECKING
	assert( s->ec );
	assert( s->ec->effects );
	assert( s->ec->effects[(info->uc.selected_chain_entry)] );
	assert( s->ec->effects[(info->uc.selected_chain_entry)]->parameters );
#endif
	key_parameter_t *key = s->ec->effects[(info->uc.selected_chain_entry)]->parameters[(info->uc.selected_parameter_id)];
	GtkWidget *curve = glade_xml_get_widget_(info->main_window, "curve");

	reset_curve( key, curve );

	update_curve_accessibility("curve");
	update_curve_widget("curve");

	char name[20];	
	sprintf(name, "P%d", info->uc.selected_parameter_id);
	update_label_str( "curve_parameter", name );
}

static	int	interpolate_parameters(void)
{
	sample_slot_t *s = info->selected_slot;
	if(!s || !s->ec)
		return 0;
	int i,j;
	int res = 0;
	char params[255];
	bzero(params,255);
	info->parameter_lock = 1;
	for( i = 0; i < MAX_CHAIN_LEN; i ++ )
	{
		int values[MAX_PARAMETERS];
		int skip = 1;
		int id = 0;
		char slider_name[20];
		memset( values, 0, MAX_PARAMETERS * sizeof(int) );
		for( j = 0; j < MAX_PARAMETERS; j ++ )
		{
			key_parameter_t *p = s->ec->effects[i]->parameters[j];
			if( info->uc.selected_chain_entry == i )
			{
				sprintf(slider_name, "slider_p%d", j );
	      	                values[j] = get_slider_val ( slider_name );
			}
			if( p->running == 1 && parameter_for_frame(p, info->status_frame) )
			{
				int min,max;
				if(_effect_get_minmax( p->parameter_id, &min, &max, j ))
				{
					float scale = 0.0;
					if(get_parameter_key_value( p,
						info->status_frame, &scale ) )
					{
						float min_value = (float)min;	
						float max_value = (float)max;
						float max_range = fabs( min_value ) + fabs( max_value );
						float value = scale * max_range;
						value += min_value;
						values[j] =(int) value;
						skip = 0;
						id = p->parameter_id;
						if(info->uc.selected_chain_entry == i )
						{ // Update slider values of current selected chain entry
							sprintf(slider_name, "slider_p%d", j );
							update_slider_value( slider_name, values[j],0 );
						}
					}
				}
			}
	
			if(!skip) // Put KF to veejay
			{ // sample, chain entry, effect_id, arg i .. arg n
				int np = _effect_get_np( id );
				char token[10];
				sprintf(params, "%03d:%d %d %d ",
					VIMS_CHAIN_ENTRY_SET_PRESET,s->sample_id,i,id );
				int k;
				for(k = 0; k < np; k ++ )
				{
					sprintf(token, "%d ", values[k] );
					strncat( params, token, strlen(token));
				}
				int len = strlen( params );
				params[len-1] = ';';	
				params[len] = '\0';
				msg_vims( params );
				res ++;
			}
		}
	}
	info->parameter_lock = 0;
	
	return res;
}

static  void	update_curve_widget(const char *name)
{
	GtkWidget *curve = glade_xml_get_widget_( info->main_window,name);
	sample_slot_t *s = info->selected_slot;
	if(!s || !s->ec)
		return;
	int i = info->uc.selected_chain_entry; /* chain entry */
	int j = info->uc.selected_parameter_id;

	key_parameter_t *key = s->ec->effects[i]->parameters[j];
	set_points_in_curve( key, curve );
}

static	void	update_curve_accessibility(const char *name)
{
	
	sample_slot_t *s = info->selected_slot;
	if(!s || !s->ec)
		return;

	int i = info->uc.selected_chain_entry; /* chain entry */
	int nl = 0;

	if( info->status_tokens[PLAY_MODE] == MODE_PLAIN )
	{
		update_spin_range( "curve_spinstart", 0,0,0);
		update_spin_range( "curve_spinend", 0,0,0);
		disable_widget( "curve" );
		disable_widget( "curve_table" );
	}
	else
	{
		int k = 0;
		if( info->uc.entry_tokens[ENTRY_FXID] <= 0 )
		  for( k = 0; k < 8; k ++ )
			clear_parameter_values(  s->ec->effects[i]->parameters[k] );

		enable_widget( "curve_table" );
		enable_widget( "curve" );
		if(info->status_tokens[PLAY_MODE] == MODE_SAMPLE )
		{
			update_spin_range( "curve_spinstart", 
				info->status_tokens[SAMPLE_START], 
				info->status_tokens[SAMPLE_END], info->status_tokens[SAMPLE_START] );
			update_spin_range( "curve_spinend", info->status_tokens[SAMPLE_START],
				info->status_tokens[SAMPLE_END] ,  info->status_tokens[SAMPLE_END] );
		}
		else
		{
			nl = get_nums("stream_length") + 1 ;
			update_spin_range( "curve_spinstart", 0, nl-1, 0 );
			update_spin_range( "curve_spinend", 0,nl-1, nl-1);
		}	

	}	

	if(s->ec)
	{
		set_toggle_button( "curve_toggleglobal", s->ec->enabled );
		set_toggle_button( "curve_toggleentry", s->ec->effects[i]->enabled  );
	}
/*	if( key->type == GTK_CURVE_TYPE_LINEAR )
		set_toggle_button( tog_w[0].name,1 );
	else
		if(key->type == GTK_CURVE_TYPE_SPLINE )
			set_toggle_button( tog_w[1].name ,1);
		else
			set_toggle_button( tog_w[2].name ,1);
*/	
	
}

static	int	get_nums(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name);
	if(!w) return 0;
	return (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON( w ) );
}

static	int	count_textview_buffer(const char *name)
{
	GtkWidget *view = glade_xml_get_widget_( info->main_window, name );
	if(view)
	{
		GtkTextBuffer *tb = NULL;
		tb = gtk_text_view_get_buffer( GTK_TEXT_VIEW(view) );
		return gtk_text_buffer_get_char_count( tb );
	}
	return 0;

}

static	void	clear_textview_buffer(const char *name)
{
	GtkWidget *view = glade_xml_get_widget_( info->main_window, name );
	if(view)
	{
		GtkTextBuffer *tb = NULL;
		tb = gtk_text_view_get_buffer( GTK_TEXT_VIEW(view) );
		GtkTextIter iter1,iter2;
		gtk_text_buffer_get_start_iter( tb, &iter1 );
		gtk_text_buffer_get_end_iter( tb, &iter2 );
		gtk_text_buffer_delete( tb, &iter1, &iter2 );
	}
}

static	gchar	*get_textview_buffer(const char *name)
{
	GtkWidget *view = glade_xml_get_widget_( info->main_window,name );
	if(view)
	{
		GtkTextBuffer *tb = NULL;
		tb = gtk_text_view_get_buffer( GTK_TEXT_VIEW(view) );
		GtkTextIter iter1,iter2;

		gtk_text_buffer_get_start_iter(tb, &iter1);
		gtk_text_buffer_get_end_iter( tb, &iter2);
		gchar *res = gtk_text_buffer_get_text( tb, &iter1,&iter2 , TRUE );
		return res;
	}
	return NULL;
}

static	void set_textview_buffer(const char *name, gchar *utf8text)
{
	GtkWidget *view = glade_xml_get_widget_( info->main_window, name );	
	if(view)
	{
		GtkTextBuffer *tb = gtk_text_view_get_buffer(
					GTK_TEXT_VIEW(view) );
		gtk_text_buffer_set_text( tb, utf8text, -1 );
	}		
}

static	gchar	*get_text(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
	if(!w) return NULL;
	return (gchar*) gtk_entry_get_text( GTK_ENTRY(w));
}

static	void	put_text(const char *name, char *text)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
	if(w)
	{
		gchar *utf8_text = _utf8str( text );
		gtk_entry_set_text( GTK_ENTRY(w), utf8_text );
		g_free(utf8_text);
	}
}

static	int	is_button_toggled(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name);
	if(!w) return 0;
	if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w) ) == TRUE )
		return 1;
	return 0;
}
static	void	set_toggle_button(const char *name, int status)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
	if(w)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), (status==1 ? TRUE: FALSE));
	}
}


static	void	update_slider_gvalue(const char *name, gdouble value)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w)
		return;
	gtk_adjustment_set_value(
		GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment), value );	
}

static	void	update_slider_value(const char *name, gint value, gint scale)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w)
		return;
	gdouble gvalue;
	if(scale)
		gvalue = (gdouble) value / (gdouble) scale;
	else
		gvalue = (gdouble) value;

	gtk_adjustment_set_value(
		GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment), gvalue );	
}
/*
static void 	update_knob_value(GtkWidget *w, gdouble value, gdouble scale)
{
	GtkAdjustment *adj = gtk_knob_get_adjustment(GTK_KNOB(w));
	gdouble gvalue;

	if(scale) gvalue = (gdouble) value / (gdouble) scale;
	else gvalue = (gdouble) value;

	gtk_adjustment_set_value(adj, gvalue );	
}*/

/*
static  void	update_knob_range(GtkWidget *w, gdouble min, gdouble max, gdouble value, gint scaled)
{
	GtkAdjustment *adj = gtk_knob_get_adjustment(GTK_KNOB(w));

	if(!scaled)
	{
	    adj->lower = min;
	    adj->upper = max;
	    adj->value = value;	
	}
	else
	{
	    gdouble gmin =0.0;
	    gdouble gmax =100.0;
	    gdouble gval = gmax / value;
	    adj->lower = gmin;
	    adj->upper = gmax;
	    adj->value = gval;		    
	}	
}*/
static	void	update_spin_incr( const char *name, gdouble step, gdouble page )
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) return;
	gtk_spin_button_set_increments(GTK_SPIN_BUTTON(w),step,page );
}

static	void	update_spin_range(const char *name, gint min, gint max, gint val)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) return;
	
	gtk_spin_button_set_range( GTK_SPIN_BUTTON(w), (gdouble)min, (gdouble) max );
	gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), (gdouble)val);
}
/*static	int	get_mins(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) return 0;
	GtkAdjustment *adj = gtk_spin_button_get_adjustment( GTK_SPIN_BUTTON(w) );
	return (int) adj->lower;
}


static	int	get_maxs(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) return 0;
	GtkAdjustment *adj = gtk_spin_button_get_adjustment( GTK_SPIN_BUTTON(w) );
	return (int) adj->upper;
}*/

static	void	update_spin_value(const char *name, gint value )
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
	if(!w) return;
	gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), (gdouble) value );
}

static  void	update_slider_range(const char *name, gint min, gint max, gint value, gint scaled)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) return;
	GtkRange *range = GTK_RANGE(w);
	if(!scaled)
	{
		gtk_range_set_range(range, (gdouble) min, (gdouble) max );
		gtk_range_set_value(range, value );
	}
	else
	{
		gdouble gmin =0.0;
		gdouble gmax =100.0;
		gdouble gval = gmax / value;
		gtk_range_set_range(range, gmin, gmax);
		gtk_range_set_value(range, gval );
	}

	gtk_range_set_adjustment(range, GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment ) );
}

static	void	update_label_i(const char *name, int num, int prefix)
{
	GtkWidget *label = glade_xml_get_widget_(
				info->main_window, name);
	if(!label) return;
	char	str[20];
	if(prefix)
		g_snprintf( str,20, "%09d", num );
	else
		g_snprintf( str,20, "%d",    num );
	gchar *utf8_value = _utf8str( str );
	gtk_label_set_text( GTK_LABEL(label), utf8_value);
	g_free( utf8_value );
}
static	void	update_label_f(const char *name, float val )
{
	GtkWidget *label = glade_xml_get_widget_(
				info->main_window, name);
	if(!label) return;
	char value[10];
	snprintf( value, sizeof(value)-1, "%2.2f", val );

	gchar *utf8_value = _utf8str( value );
	gtk_label_set_text( GTK_LABEL(label), utf8_value );
	g_free(utf8_value);	
}
static	void	update_label_str(const char *name, gchar *text)
{
	GtkWidget *label = glade_xml_get_widget_(
				info->main_window, name);
	if(!label ||!text) return;
	gchar *utf8_text = _utf8str( text );
	if(!utf8_text) return;
	gtk_label_set_text( GTK_LABEL(label), utf8_text);
	g_free(utf8_text);
}	

static void selection_get_paths(GtkTreeModel *model, GtkTreePath *path,
				GtkTreeIter *iter, gpointer data)
{
	GSList **paths = data;

	*paths = g_slist_prepend(*paths, gtk_tree_path_copy(path));
}


GSList *gui_tree_selection_get_paths(GtkTreeView *view)
{
	GtkTreeSelection *sel;
	GSList *paths;

	/* get paths of selected rows */
	paths = NULL;
	sel = gtk_tree_view_get_selection(view);
	gtk_tree_selection_selected_foreach(sel, selection_get_paths, &paths);

	return paths;
}

static	void	update_colorselection()
{
	GtkWidget *colorsel = glade_xml_get_widget_( info->main_window, 
				"colorselection");
	GdkColor color;

	color.red = 255 * info->status_tokens[STREAM_COL_R];
	color.green = 255 * info->status_tokens[STREAM_COL_G];
	color.blue = 255 * info->status_tokens[STREAM_COL_B];

	gtk_color_selection_set_current_color(
		GTK_COLOR_SELECTION( colorsel ),
		&color );
}

int	resize_primary_ratio_y()
{	
	float ratio = (float)info->el.width / (float)info->el.height;
	float result = (float) get_nums( "priout_width" ) / ratio; 
	return (int) result;	
}
 
int	resize_primary_ratio_x()
{
	float ratio = (float)info->el.height / (float)info->el.width;
	float result = (float) get_nums( "priout_height" ) / ratio;
	return (int) result;
}

static	void	update_rgbkey()
{
	if(!info->entry_lock)
	{
		info->entry_lock =1;
		GtkWidget *colorsel = glade_xml_get_widget_( info->main_window, 
				"rgbkey");
		GdkColor color;
		/* update from entry tokens (delivered by GET_CHAIN_ENTRY */
		int	*p = &(info->uc.entry_tokens[0]);
		/* 0 = effect_id, 1 = has second input, 2 = num parameters,
			3 = p0 , 4 = p1, 5 = p2, 6 = p3 ... */


		color.red = 255 * p[3];
		color.green = 255 * p[4];
		color.blue = 255 * p[5];

		gtk_color_selection_set_current_color(
			GTK_COLOR_SELECTION( colorsel ),
			&color );
		info->entry_lock = 0;
	}
}

static	void	update_rgbkey_from_slider()
{
	if(!info->entry_lock)
	{
		GtkWidget *colorsel = glade_xml_get_widget_( info->main_window,
				"rgbkey");
		info->entry_lock = 1;
		GdkColor color;

		color.red = 255 * ( get_slider_val( "slider_p1" ) );
		color.green = 255 * ( get_slider_val( "slider_p2" ) );
		color.blue = 255 * ( get_slider_val( "slider_p3" ) );

		gtk_color_selection_set_current_color(
			GTK_COLOR_SELECTION( colorsel ),
			&color );
		info->entry_lock = 0;
	}
}

static	void	v4l_expander_toggle(int mode)
{
	// we can set the expanded of the ABC expander
	GtkWidget *exp = glade_xml_get_widget_(
			info->main_window, "v4l_expander");
	GtkExpander *e = GTK_EXPANDER(exp);
	gtk_expander_set_expanded( e ,(mode==0 ? FALSE : TRUE) );
}



static  GdkPixbuf	*update_pixmap_kf( int status )
{
	char path[MAX_PATH_LEN];
	char filename[MAX_PATH_LEN];
	bzero(path,MAX_PATH_LEN);

	sprintf(filename, "fx_entry_%s.png", ( status == 1 ? "on" : "off" ));
	get_gd(path,NULL, filename);
		
	GError *error = NULL;
	GdkPixbuf *toggle = gdk_pixbuf_new_from_file( path , &error);
	if(error)
		return NULL;
	return toggle;
}	
static  GdkPixbuf	*update_pixmap_entry( int status )
{
	char path[MAX_PATH_LEN];
	char filename[MAX_PATH_LEN];
	bzero(path,MAX_PATH_LEN);

	sprintf(filename, "fx_entry_%s.png", ( status == 1 ? "on" : "off" ));
	get_gd(path,NULL, filename);

	GError *error = NULL;
	GdkPixbuf *icon = gdk_pixbuf_new_from_file(path, &error);
	if(error)
		return 0;
	return icon;
}	

static gboolean
chain_update_row(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,
             gpointer data)
{

	vj_gui_t *gui = (vj_gui_t*) data;
	int entry = info->uc.selected_chain_entry;
	gint gentry = 0;
	gtk_tree_model_get (model, iter,
                        FXC_ID, &gentry, -1);
	sample_slot_t *s = info->selected_slot;
	if(!s || !s->ec )
		return FALSE;
	
	if(gentry == entry)
	{
		int effect_id = gui->uc.entry_tokens[ ENTRY_FXID ];
		if( effect_id <= 0 )
		{
			gtk_list_store_set( GTK_LIST_STORE(model),iter, FXC_ID, entry, -1 );
		}
		else
		{
			gchar *descr = _utf8str( _effect_get_description( effect_id ));
			int on = s->ec->effects[entry]->enabled;
			GdkPixbuf *toggle = update_pixmap_entry( gui->uc.entry_tokens[ENTRY_FXSTATUS] );
			GdkPixbuf *kf_toggle = update_pixmap_kf( on );
			gtk_list_store_set( GTK_LIST_STORE(model),iter,
				FXC_ID, entry,
				FXC_FXID, descr,
				FXC_FXSTATUS, toggle,
				FXC_KF, kf_toggle, -1 );
			g_free(descr);
			g_object_unref( kf_toggle );
			g_object_unref( toggle );
		}
	}

 	return FALSE;
}

static	void	update_status_accessibility(int pm)
{
	int i;

	if( pm != info->prev_mode )
	{
		/* If mode changed, enable/disable widgets etc first */
		if( info->status_tokens[PLAY_MODE] != info->prev_mode )
		{
			if( pm == MODE_STREAM )
			{
				enable_widget("frame_streamproperties");
				enable_widget("frame_streamrecord");
				disable_widget("frame_samplerecord");
				disable_widget("frame_sampleproperties");
				for(i=0; videowidgets[i].name != NULL; i++)
					disable_widget( videowidgets[i].name);
				//disable_widget_by_pointer(info->audio_knob);
				disable_widget( "speed_slider" );			
			}
			else
			{
				for(i=0; videowidgets[i].name != NULL; i++)
					enable_widget( videowidgets[i].name);
				//enable_widget_by_pointer(info->speed_knob);	
				enable_widget( "speed_slider" );
			}

			if( pm == MODE_SAMPLE )
			{
				enable_widget("expander10");
				enable_widget("frame_samplerecord");
				enable_widget("frame_sampleproperties");
				disable_widget("frame_streamproperties");
				disable_widget("frame_streamrecord");
				enable_widget( "samplerand" );
				enable_widget( "freestyle" );
			}
			else
			{
				disable_widget( "expander10");
				disable_widget( "samplerand" );
				disable_widget( "freestyle" );
			}

			if( pm == MODE_PLAIN)
			{
				enable_widget( "button_samplestart" );
				enable_widget( "button_sampleend" );
			}
			else
			{
				disable_widget( "button_samplestart");
				disable_widget( "button_sampleend" );
			}

			if( pm == MODE_PLAIN)
				for( i = 0; plainwidgets[i].name != NULL ;i++ )
					disable_widget( plainwidgets[i].name );
			else
				for( i = 0; plainwidgets[i].name != NULL ; i ++ )
					enable_widget( plainwidgets[i].name );
		}
	}
}

/* Cut from global_info()
   This function updates the sample/stream editor if the current playing stream/sample
   matches with the selected sample slot */

static	void	update_record_tab(int pm)
{
	if(pm == MODE_STREAM)
	{
		update_spin_value( "spin_streamduration" , 1 );
		gint n_frames = get_nums( "spin_streamduration" );
		gchar *time = format_time(n_frames, info->el.fps);
		update_label_str( "label_streamrecord_duration", time );
		g_free(time);
	}
	if(pm == MODE_SAMPLE)
	{
		update_spin_value( "spin_sampleduration", 1 );
		// combo_samplecodec
		gint n_frames = sample_calctime();
		gchar *time = format_time( n_frames,info->el.fps );
		update_label_str( "label_samplerecord_duration", time );
		g_free(time);
	}
}

static void	update_current_slot(int pm)
{
//	gchar *time = format_time( (pm == MODE_STREAM ? info->status_frame : info->status_tokens[FRAME_NUM]) );
	int *history = info->history_tokens[pm];
//	update_label_str( "label_sampleposition", time);
//	g_free(time); 
	gint update = 0;
	gint hm = info->prev_mode;
	/* Mode changed or ID changed, 
	   Reload FX Chain, Reload current entry and disable widgets based on stream type */

	if( pm != info->prev_mode || info->status_tokens[PLAY_MODE] != history[PLAY_MODE] || info->status_tokens[CURRENT_ID] != history[CURRENT_ID] )
	{
		int k;
		info->uc.reload_hint[HINT_ENTRY] = 1;
		info->uc.reload_hint[HINT_CHAIN] = 1;
		info->uc.reload_hint[HINT_KF] = 1;
		update = 1;
		update_record_tab( pm );

		if( info->status_tokens[STREAM_TYPE] == STREAM_WHITE )
		{
			enable_widget( "colorselection" );
		}
		else
		{
			disable_widget( "colorselection" );
		}

		if( info->status_tokens[STREAM_TYPE] == STREAM_VIDEO4LINUX )
		{
			info->uc.reload_hint[HINT_V4L] = 1;
			for(k = 0; k < 5; k ++ )
				enable_widget( capt_card_set[k].name );
			v4l_expander_toggle(1);
		}
		else
		{ /* not v4l, disable capt card */
			for(k = 0; k < 5; k ++ )
				disable_widget( capt_card_set[k].name );

			v4l_expander_toggle(0);
		}

	}
	if( info->status_tokens[CURRENT_ENTRY] != history[CURRENT_ENTRY] ||
		info->uc.reload_hint[HINT_ENTRY] == 1 )
	{
		info->uc.selected_chain_entry = info->status_tokens[CURRENT_ENTRY];
		info->uc.reload_hint[HINT_ENTRY] = 1;
		load_parameter_info();
		info->uc.reload_hint[HINT_KF]  = 1;
	}

	/* Actions for stream */
	if( pm != hm && pm == MODE_STREAM )
	{
		// pop up stream notepad
		
		/* Is a solid color stream */	
		if( info->status_tokens[STREAM_TYPE] == STREAM_WHITE )
		{
			if( ( history[STREAM_COL_R] != info->status_tokens[STREAM_COL_R] ) ||
			    ( history[STREAM_COL_G] != info->status_tokens[STREAM_COL_G] ) ||
		 	    ( history[STREAM_COL_B] != info->status_tokens[STREAM_COL_B] ) )
			 {
				info->uc.reload_hint[HINT_RGBSOLID] = 1;
			 }

		}

		update_label_str( "label_currentsource", "Stream" );
		gchar *time = format_time( info->status_frame,info->el.fps );
		update_label_str( "label_curtime", time );
		g_free(time); 
	}

	/* Actions for sample */
	if(hm != pm && pm == MODE_SAMPLE )
	{
		/* Total frame change, update start,end and */
//		if( history[TOTAL_FRAMES] != info->status_tokens[TOTAL_FRAMES])
//		{
	//HERE	
//printf("Start / end changed: %d - %d\n", 0 , info->el.num_frames );
//printf("Current = %d - %d\n", info->status_tokens[SAMPLE_START], info->status_tokens[SAMPLE_END]);
//		}

		/* Update label and video slider*/
		update_label_i( "label_sampleposition",
			info->status_tokens[FRAME_NUM] - info->status_tokens[SAMPLE_START] , 1);


		int marker_go = 0;
		/* Update marker bounds */
		if( (history[SAMPLE_MARKER_START] != info->status_tokens[SAMPLE_MARKER_START]) )
		{
			update = 1;
			gint nm =  info->status_tokens[SAMPLE_MARKER_START];
			if(nm >= 0)
			{
				gdouble in = (1.0 / (gdouble)info->status_tokens[TOTAL_FRAMES]) * nm;
				timeline_set_in_point( info->tl, in );
				marker_go = 1;
			}
			else
			{
				if(pm == MODE_SAMPLE)
				{
					timeline_set_in_point( info->tl, 0.0 );
					marker_go = 1;
				}
			}
			gchar *dur = format_time( info->status_tokens[SAMPLE_MARKER_END] - info->status_tokens[SAMPLE_MARKER_START],
				info->el.fps );
			update_label_str( "label_markerduration", dur );
			g_free(dur);
		}

		if( (history[SAMPLE_MARKER_END] != info->status_tokens[SAMPLE_MARKER_END]) )
		{
			gint nm = info->status_tokens[SAMPLE_MARKER_END];
			if(nm > 0 )
			{
				gdouble out = (1.0/ (gdouble)info->status_tokens[TOTAL_FRAMES]) * nm;
		
				timeline_set_out_point( info->tl, out );
				marker_go = 1;
			}
			else
			{
				if(pm == MODE_SAMPLE)
				{
					timeline_set_out_point(info->tl, 1.0 );
					marker_go = 1;
				}
			}
			update = 1;
		}
			
		if( (history[SAMPLE_START] != info->status_tokens[SAMPLE_START] ))
		{
			update_spin_value( "spin_samplestart", info->status_tokens[SAMPLE_START] );
			update = 1;
		}
		if( (history[SAMPLE_END] != info->status_tokens[SAMPLE_END] ))
		{
			update_spin_value( "spin_sampleend", info->status_tokens[SAMPLE_END]);
			update = 1;
		}
		
		if( marker_go )
		{
			info->uc.reload_hint[HINT_MARKER] = 1;	
		}

		if( history[SAMPLE_LOOP] != info->status_tokens[SAMPLE_LOOP])
		{
			switch( info->status_tokens[SAMPLE_LOOP] )
			{
				case 0:
					set_toggle_button( "loop_none", 1 );
				break;
				case 1:
					set_toggle_button( "loop_normal", 1 );
				break;
				case 2:
					set_toggle_button("loop_pingpong", 1 );
				break;
			}	
		}
	
		gint speed = 0;

		if( history[SAMPLE_SPEED] != info->status_tokens[SAMPLE_SPEED] && !update)
		{	
			speed = info->status_tokens[SAMPLE_SPEED];
			update_slider_value( "speed_slider", speed, 0 );
			update_label_i( "speed_label", speed,0 );


			if( speed < 0 ) info->play_direction = -1; else info->play_direction = 1;
			if( speed < 0 ) speed *= -1;
			update_spin_value( "spin_samplespeed", speed);
	//		update_knob_value(info->speed_knob, speed, 0);
		}

		if(update)
		{
			speed = info->status_tokens[SAMPLE_SPEED];
			if(speed < 0 ) info->play_direction = -1; else info->play_direction = 1;
	
			gint len = info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START];
	
			int speed = info->status_tokens[SAMPLE_SPEED];
			if(speed < 0 ) info->play_direction = -1; else info->play_direction = 1;
			if(speed < 0 ) speed *= -1;
		
			update_spin_range( "spin_samplespeed", 0, len, speed );
	
			gchar *time = format_selection_time( 0, len );
//			update_label_str( "label_samplelength", time );
			g_free(time);	

			update_label_str( "label_currentsource", "Sample");

			update_spin_value( "spin_samplestart", info->status_tokens[SAMPLE_START]);
			update_spin_value( "spin_sampleend", info->status_tokens[SAMPLE_END]);
	
			info->uc.reload_hint[HINT_HISTORY] = 1;
		
			gint n_frames = sample_calctime();
			time = format_time( n_frames, info->el.fps );
	
			timeline_set_length( info->tl,
				(gdouble) n_frames , info->status_tokens[FRAME_NUM]- info->status_tokens[SAMPLE_START] );


			
		}
	}

}

static void
on_vims_messenger       (void)
{
        GtkTextIter start, end;
        GtkTextBuffer* buffer;
        GtkTextView* t= NULL;
        gchar *str = NULL;
        // static gint line = 0; make this global (...)
        static int wait = 0;
        t =
		GTK_TEXT_VIEW(GTK_WIDGET(glade_xml_get_widget(info->main_window,"vims_messenger_textview")));
      
	buffer = gtk_text_view_get_buffer(t);

        if(info->vims_line >= gtk_text_buffer_get_line_count(buffer)){
                info->vims_line = 0;
                if(!is_button_toggled( "vims_messenger_loop"))
		{
                        set_toggle_button( "vims_messenger_play", 0 );
			return; 
	 	}
	}

        if(is_button_toggled( "vims_messenger_play" )){
                if(wait){
                        wait--;
                }
                else{
                        gtk_text_buffer_get_iter_at_line(buffer, &start, info->vims_line);
                        end = start;
                
                        gtk_text_iter_forward_sentence_end(&end);
                        str = gtk_text_buffer_get_text (buffer, &start, &end, TRUE);
                       
                        if(str[0] == '+'){
                                str[0] = ' ';
                                g_strstrip(str);
                                wait = atoi(str);                
                        }
                        else{
                                vj_msg(VEEJAY_MSG_INFO, "User defined VIMS message sent '%s'",str );
                                msg_vims( str );
                                printf("\nSent VIMS: %s", str);
                        }
                        info->vims_line++;
                }
        }
}



static void 	update_globalinfo()
{
	int pm = info->status_tokens[PLAY_MODE];

	int *history = info->history_tokens[info->uc.playmode];

	gint	i;

#ifdef STRICT_CHECKING
	assert( info->el.fps > 0 );
#endif
	update_status_accessibility(pm);

	if( info->status_tokens[CURRENT_ID] != history[CURRENT_ID] ||
		info->status_tokens[PLAY_MODE] != history[PLAY_MODE]  )
	{

		if( pm == MODE_SAMPLE || pm == MODE_STREAM )
		{
			info->uc.reload_hint[HINT_ENTRY] = 1;	
			info->uc.reload_hint[HINT_CHAIN] = 1;
		}
		if( pm != MODE_STREAM )
			info->uc.reload_hint[HINT_EL] = 1;
		if( pm != MODE_PLAIN )
			info->uc.reload_hint[HINT_KF] = 1;

		if( pm == MODE_SAMPLE )
			timeline_set_selection( info->tl, TRUE );
		else
			timeline_set_selection( info->tl, FALSE );

		select_slot( info->status_tokens[PLAY_MODE] );
	}
	
	on_samplepage_clicked(NULL,NULL);

	if( info->status_tokens[TOTAL_SLOTS] !=
		history[TOTAL_SLOTS] 
			|| info->status_tokens[TOTAL_SLOTS] != info->uc.expected_slots )
	{
		info->uc.reload_hint[HINT_SLIST] = 1;
	}

	gint total_frames_ = (pm == MODE_STREAM ? info->status_tokens[SAMPLE_MARKER_END] : info->status_tokens[TOTAL_FRAMES] );
	gint history_frames_ = (pm == MODE_STREAM ? history[SAMPLE_MARKER_END] : history[TOTAL_FRAMES] ); 
	gint current_frame_ = (pm == MODE_STREAM ? info->status_frame : info->status_tokens[FRAME_NUM] );

	if( total_frames_ != history_frames_ )
	{
//fprintf(stderr, "%d - %f\n",total_frames_, info->el.fps );

		gchar *time = format_time( total_frames_, info->el.fps );
		if( pm == MODE_STREAM )
		{
			update_spin_value( "stream_length", info->status_tokens[SAMPLE_MARKER_END] );
			update_label_str( "stream_length_label", time );
		}

		update_spin_range("button_fadedur", 0, total_frames_, 0 );
		update_label_i( "label_totframes", total_frames_, 1 );
		if( pm == MODE_PLAIN )
		{
			for( i = 0; i < 3; i ++)
				if(info->selection[i] > total_frames_ ) info->selection[i] = total_frames_;
			update_spin_range(
				"button_el_selstart", 0, total_frames_, info->selection[0]);
			update_spin_range(
				"button_el_selend", 0, total_frames_, info->selection[1]);
			update_spin_range(
				"button_el_selpaste", 0, total_frames_, info->selection[2]);
		}	

		update_label_i( "label_totframes", total_frames_, 1 );
		update_label_str( "label_totaltime", time );
		if(pm == MODE_SAMPLE)
			update_label_str( "label_samplelength", time );
		else
			update_label_str( "label_samplelength", "0:00:00:00" );


		timeline_set_length( info->tl,
				(gdouble) total_frames_ , current_frame_);

		
	//	vj_kf_select_parameter( info->uc.selected_parameter_id );

		g_free(time);
	}

	if( pm == MODE_STREAM )
	{
		info->status_frame ++;
		if(info->status_frame > info->status_tokens[ SAMPLE_MARKER_END ])
			info->status_frame = 0;
		
	}
	else info->status_frame = info->status_tokens[FRAME_NUM];

	timeline_set_pos( info->tl, (gdouble) info->status_frame );
	gchar *current_time_ = format_time( info->status_frame,info->el.fps );
	update_label_i(   "label_curframe", info->status_frame ,1 );
	update_label_str( "label_curtime", current_time_ );
	g_free(current_time_);

	if( pm == MODE_SAMPLE )
		update_label_i( "label_samplepos",
			info->status_frame , 1);
	else
		update_label_i( "label_samplepos" , 0 , 1 );

	if( history[CURRENT_ID] != info->status_tokens[CURRENT_ID] )
	{
		if(pm == MODE_SAMPLE || pm == MODE_STREAM)
			update_label_i( "label_currentid", info->status_tokens[CURRENT_ID] ,0);
	}

	if( history[STREAM_RECORDING] != info->status_tokens[STREAM_RECORDING] )
	{
		if(pm == MODE_SAMPLE || pm == MODE_STREAM)
		{
			if( history[CURRENT_ID] == info->status_tokens[CURRENT_ID] )
				info->uc.reload_hint[HINT_RECORDING] = 1;
			if( info->status_tokens[STREAM_RECORDING])
				vj_msg(VEEJAY_MSG_INFO, "Veejay is recording");
			else
				vj_msg(VEEJAY_MSG_INFO, "Recording has stopped");
		}
	}

	if( pm == MODE_PLAIN )
	{
		if( history[SAMPLE_SPEED] != info->status_tokens[SAMPLE_SPEED] )
		{
			int plainspeed =  info->status_tokens[SAMPLE_SPEED];

			update_slider_value( "speed_slider", plainspeed, 0);
	//		update_knob_value( info->speed_knob, plainspeed, 0 );
			update_label_i( "speed_label", plainspeed, 0 );
			if( plainspeed < 0 ) info->play_direction = -1; else info->play_direction = 1;
			if( plainspeed < 0 ) plainspeed *= -1;
		}
	}

	/* Update current playing sample in dialog window */
	update_current_slot(pm);
	info->uc.playmode = pm;

}	


static void	process_reload_hints(void)
{
	int pm = info->status_tokens[PLAY_MODE];
	int *history = info->history_tokens[pm];

	int	*entry_tokens = &(info->uc.entry_tokens[0]);

	if(info->uc.reload_hint[HINT_V4L] == 1 && pm == MODE_STREAM)
	{
		load_v4l_info();
	}

	if( info->uc.reload_hint[HINT_RGBSOLID] == 1 && pm == MODE_STREAM )
		update_colorselection();

	if( info->uc.reload_hint[HINT_EL] ==  1 )
	{
		load_editlist_info();
		reload_editlist_contents();
		update_spin_range(
		 	"spin_samplestart", 0, info->el.num_frames, info->status_tokens[SAMPLE_START] );
		update_spin_range(
			"spin_sampleend", 0, info->el.num_frames , info->status_tokens[SAMPLE_END] );
	}
	if( info->uc.reload_hint[HINT_SLIST] == 1 )
	{
		load_samplelist_info(FALSE);
		info->uc.expected_slots = info->status_tokens[TOTAL_SLOTS];
	}

	if( info->uc.reload_hint[HINT_RECORDING] == 1 && pm != MODE_PLAIN)
	{
		if(info->status_tokens[STREAM_RECORDING])
		{
			if(!info->uc.recording[pm]) init_recorder( info->status_tokens[STREAM_DURATION], pm );
		}	
	}

	if(info->uc.reload_hint[HINT_BUNDLES] == 1 )
		reload_bundles();

	if( info->selected_slot && info->selected_slot->sample_id == info->status_tokens[CURRENT_ID] &&
			info->selected_slot->sample_type == 0 && pm == MODE_PLAIN)
	{
		if( history[SAMPLE_FX] != info->status_tokens[SAMPLE_FX])
		{
			//also for stream (index is equivalent)
			if(pm == MODE_SAMPLE)
				set_toggle_button( "check_samplefx",
					info->status_tokens[SAMPLE_FX]);
			if(pm == MODE_STREAM)	
				set_toggle_button( "check_streamfx",
					info->status_tokens[SAMPLE_FX]);
		}
	}
	if( info->uc.reload_hint[HINT_CHAIN] == 1 && pm != MODE_PLAIN)
	{
		load_effectchain_info(); 
	}


	info->parameter_lock = 1;
	if(info->uc.reload_hint[HINT_ENTRY] == 1 && pm != MODE_PLAIN)
	{
		char slider_name[10];
		char button_name[10];
		gint np = 0;
		gint i;
		/* update effect description */
		info->uc.reload_hint[HINT_KF] = 1;
		if( entry_tokens[ENTRY_FXID] == 0)
		{
			put_text( "entry_effectname" ,"" );
			disable_widget( "FXframe" );
		}
		else
		{
			put_text( "entry_effectname", _effect_get_description( entry_tokens[ENTRY_FXID] ));
			enable_widget( "FXframe");
			set_toggle_button( "button_entry_toggle", entry_tokens[ENTRY_FXSTATUS] );
			np = _effect_get_np( entry_tokens[ENTRY_FXID] );
			for( i = 0; i < np ; i ++ )
			{
				sprintf(slider_name, "slider_p%d",i);
				enable_widget( slider_name );
				sprintf(button_name, "inc_p%d", i);
				enable_widget( button_name );
				sprintf(button_name, "dec_p%d", i );
				enable_widget( button_name );
				gint min,max,value;
				value = entry_tokens[3 + i];
				if( _effect_get_minmax( entry_tokens[ENTRY_FXID], &min,&max, i ))
				{
					update_slider_range( slider_name,min,max, value, 0);
				}
				sprintf(button_name, "kf_p%d", i );
				enable_widget( button_name );
			}
		}
		update_spin_value( "button_fx_entry", info->uc.selected_chain_entry);	

		for( i = np; i < 8 ; i ++ )
		{
			sprintf(slider_name, "slider_p%d",i);
			gint min = 0, max = 1, value = 0;
			update_slider_range( slider_name, min,max, value, 0 );
			disable_widget( slider_name );
			sprintf( button_name, "inc_p%d", i);
			disable_widget( button_name );
			sprintf( button_name, "dec_p%d", i);
			disable_widget( button_name );
			sprintf( button_name, "kf_p%d", i );
			disable_widget( button_name );
		}
		GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(glade_xml_get_widget_(
				info->main_window, "tree_chain") ));

  		gtk_tree_model_foreach(
                       	model,
			chain_update_row, (gpointer*) info );
	}
	info->parameter_lock = 0;

	/* Curve needs update (start/end changed, effect id changed */
	if ( info->uc.reload_hint[HINT_KF]  )
		vj_kf_select_parameter( info->uc.selected_parameter_id );
	
	memset( info->uc.reload_hint, 0, sizeof(info->uc.reload_hint ));	
}

static	void	reset_tree(const char *name)
{
	GtkWidget *tree_widget = glade_xml_get_widget_( info->main_window,name );
	GtkTreeModel *tree_model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree_widget) );
	
	// get a GList of all TreeViewColumns
/*
	GList *columns =gtk_tree_view_get_columns(GTK_TREE_VIEW( tree_widget ) );
	while( columns != NULL )
	{
		GList *renderers;
		// get single column
		column = GTK_TREE_VIEW_COLUMN( columns->data );
*/

	/* renderers = gtk_tree_view_column_get_cell_renderers( GTK_TREE_VIEW_COLUMN(column) );
		while( renderers != NULL)
		{
			GtkCellRenderer *cell = GTK_CELL_RENDERER( renderers->data );  
			gtk_tree_view_column_clear_attributes( GTK_TREE_VIEW_COLUMN(column ), 
							       GTK_CELL_RENDERER( cell ) );
			renderers = renderers->next;
		}
		g_list_free(renderers); */

/*
		gtk_tree_view_column_clear( column );
		columns = columns->next;
	}
	g_list_free(columns);
*/
	gtk_list_store_clear( GTK_LIST_STORE( tree_model ) );
//	gtk_tree_view_set_model( GTK_TREE_VIEW(tree_widget), GTK_TREE_MODEL(tree_model));
	
}


// load effect controls

gboolean
  view_entry_selection_func (GtkTreeSelection *selection,
                       GtkTreeModel     *model,
                       GtkTreePath      *path,
                       gboolean          path_currently_selected,
                       gpointer          userdata)
  {
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
      gint name = 0;

      gtk_tree_model_get(model, &iter, FXC_ID, &name, -1);
      if (!path_currently_selected && name != info->uc.selected_chain_entry)
      {
	multi_vims( VIMS_CHAIN_SET_ENTRY, "%d", name );
      }
    }

    return TRUE; /* allow selection state to change */
  }

gboolean
  view_sources_selection_func (GtkTreeSelection *selection,
                       GtkTreeModel     *model,
                       GtkTreePath      *path,
                       gboolean          path_currently_selected,
                       gpointer          userdata)
  {
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
      gchar *name = NULL;

      gtk_tree_model_get(model, &iter, FXC_ID, &name, -1);

      if (!path_currently_selected)
      {
	gint id = 0;
	sscanf(name+1, "%d", &id);
	if(name[0] == 'S')
	{
	   	info->uc.selected_mix_sample_id = id;
		info->uc.selected_mix_stream_id = 0;
	}
	else
	{
		info->uc.selected_mix_sample_id = 0;
		info->uc.selected_mix_stream_id = id;
	}
	 }

	if(name) g_free(name);
    }

    return TRUE; /* allow selection state to change */
  }


static void
cell_data_func_dev (GtkTreeViewColumn *col,
                    GtkCellRenderer   *cell,
                    GtkTreeModel      *model,
                    GtkTreeIter       *iter,
                    gpointer           data)
{
        gchar   buf[32];
        GValue  val = {0, };
	gint id = 0;
        gtk_tree_model_get_value(model, iter, V4L_SPINBOX, &val);
	gtk_tree_model_get(model, iter, V4L_NUM, &id,-1 );
	info->uc.strtmpl[0].channel = (gint) g_value_get_float(&val);
	info->uc.strtmpl[0].dev = id;
 	g_snprintf(buf, sizeof(buf), "%.0f",g_value_get_float(&val));
        g_object_set(cell, "text", buf, NULL);
}

static void
on_dev_edited (GtkCellRendererText *celltext,
               const gchar         *string_path,
               const gchar         *new_text,
               gpointer             data)
{
        GtkTreeModel *model = GTK_TREE_MODEL(data);
        GtkTreeIter   iter;
        gfloat        oldval = 0.0;
        gfloat        newval = 0.0;

        gtk_tree_model_get_iter_from_string(model, &iter, string_path);

        gtk_tree_model_get(model, &iter, V4L_SPINBOX, &oldval, -1);
      	if (sscanf(new_text, "%f", &newval) != 1)
                g_warning("in %s: problem converting string '%s' into float.\n", __FUNCTION__, new_text);

        gtk_list_store_set(GTK_LIST_STORE(model), &iter, V4L_SPINBOX, newval, -1);
	
}


static	void	setup_tree_spin_column( const char *tree_name, int type, const char *title)
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, tree_name );
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	renderer = gui_cell_renderer_spin_new(0.0, 3.0 , 1.0, 1.0, 1.0, 1.0, 0.0);
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, title );
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func( column, renderer,
			cell_data_func_dev, NULL,NULL );
	gtk_tree_view_append_column( GTK_TREE_VIEW(tree), column);
	g_object_set(renderer, "editable", TRUE, NULL);

	GtkTreeModel *model =  gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	g_signal_connect(renderer, "edited", G_CALLBACK(on_dev_edited), model );

}

static void 	setup_tree_texteditable_column(
		const char *tree_name,
		int type,
		const char *title,
		void (*callbackfunction)()
		)
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, tree_name );
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	renderer = gtk_cell_renderer_text_new();

	column = gtk_tree_view_column_new_with_attributes( title, renderer, "text", type, NULL );
	gtk_tree_view_append_column( GTK_TREE_VIEW( tree ), column );

	g_object_set(renderer, "editable", TRUE, NULL );
	GtkTreeModel *model =  gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));
	g_signal_connect( renderer, "edited", G_CALLBACK( callbackfunction ), model );
}

static	void	setup_tree_text_column( const char *tree_name, int type, const char *title )
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, tree_name );
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes( title, renderer, "text", type, NULL );
	gtk_tree_view_append_column( GTK_TREE_VIEW( tree ), column );
}

static	void	setup_tree_pixmap_column( const char *tree_name, int type, const char *title )
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, tree_name );
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	renderer = gtk_cell_renderer_pixbuf_new();
    	column = gtk_tree_view_column_new_with_attributes( title, renderer, "pixbuf", type, NULL );
	gtk_tree_view_append_column( GTK_TREE_VIEW( tree ), column );
}



static void	setup_effectchain_info( void )
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_chain");
	GtkListStore *store = gtk_list_store_new( 4, G_TYPE_INT, G_TYPE_STRING, GDK_TYPE_PIXBUF,GDK_TYPE_PIXBUF );
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "tree_chain", FXC_ID, "Entry" );
	setup_tree_text_column( "tree_chain", FXC_FXID, "Effect" );
	setup_tree_pixmap_column( "tree_chain", FXC_FXSTATUS, "Status"); // todo: could be checkbox!!
	setup_tree_pixmap_column( "tree_chain", FXC_KF , "KF" ); // parameter interpolation on/off per entry
  	GtkTreeSelection *selection; 

	tree = glade_xml_get_widget_( info->main_window, "tree_chain");
  	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE); 
   	gtk_tree_selection_set_select_function(selection, view_entry_selection_func, NULL, NULL);
}



static	void	load_v4l_info()
{
	int values[5];
	int len = 0;
	multi_vims( VIMS_STREAM_GET_V4L, "%d", (info->selected_slot == NULL ? 0 : info->selected_slot->sample_id ));
	gchar *answer = recv_vims(3, &len);
	if(len > 0 )
	{
		int res = sscanf( answer, "%05d%05d%05d%05d%05d", 
			&values[0],&values[1],&values[2],&values[3],&values[4]);
		if(res == 5)
		{
			int i;
			for(i = 0; i < 5; i ++ )
			{
				update_slider_gvalue( capt_card_set[i].name, (gdouble)values[i]/65535.0 );
			}	
		}	
		g_free(answer);
	}
}

static void online_update(int keyp, int value)
{
	char mkey[16];
	sprintf( mkey, "i%d", info->uc.selected_chain_entry );
	vevo_port_t *cur = NULL;
	vevo_property_get( fx_clipboard_, mkey, 0, &cur );
	if(!cur)
		return;
	char key[16];
	sprintf(key, "parameter%d", keyp);
	vevo_property_set( cur, key, LIVIDO_ATOM_TYPE_INT,1,&value );
}

static void update_clipboard(int fxid, int np, int *args)
{
	int i;
	char mkey[5];
	sprintf( mkey, "i%d", info->uc.selected_chain_entry );
	vevo_port_t *cur = NULL;
	vevo_property_get( fx_clipboard_, mkey, 0, &cur );

	if( !cur )
	{
		cur = (vevo_port_t*) vpn( 400 );
		vevo_property_set( fx_clipboard_, mkey, LIVIDO_ATOM_TYPE_VOIDPTR,1,&cur);
	}

	int id = fxid;
	int nn = np;

	vevo_property_set( cur, "fxid",LIVIDO_ATOM_TYPE_INT, 1, &id );

	for( i =  0; i < nn; i ++ )
	{
		char pname[16];
		sprintf(pname, "parameter%d",i);
		int value = args[i];
		vevo_property_set(cur, pname, LIVIDO_ATOM_TYPE_INT,1, &value );
	}

	vevo_property_set( cur, "num_param", LIVIDO_ATOM_TYPE_INT,1, &i );
}

static void clone_clipboard_entry(void)
{
	char mkey[5];
	sprintf( mkey, "i%d", info->uc.selected_chain_entry );
	vevo_port_t *cur = NULL;
	vevo_port_t *buf = NULL;
	int fxid = 0;
	int np = 0;
	int i;

	vevo_property_get( fx_clipboard_, mkey, 0, &cur );
	if( !cur )
		return;

 	vevo_property_get( cur, "fxid", 0, &fxid );
 	vevo_property_get( cur, "num_param", 0, &np );

	vevo_property_get( fx_clipboard_, "buffer", 0, &buf );
	if( !buf )
	{
		buf = (vevo_port_t*) vpn( 500 );
		vevo_property_set( fx_clipboard_, "buffer", LIVIDO_ATOM_TYPE_VOIDPTR,1,&buf);
	}

	vevo_property_set( buf, "fxid", LIVIDO_ATOM_TYPE_INT,1,&fxid );
	vevo_property_set( buf, "num_param", LIVIDO_ATOM_TYPE_INT, 1, &np );

	for( i =  0; i < np; i ++ )
	{
		char pname[5];
		sprintf(pname, "parameter%d",i);
		int value = 0;
		vevo_property_get(cur, pname,0, &value);
		vevo_property_set(buf, pname, LIVIDO_ATOM_TYPE_INT,1, &value );
	}
}

static	void	preset_from_clipboard(char *mkey)
{
	vevo_port_t *cur = NULL;
	vevo_property_get( fx_clipboard_, mkey, 0, &cur );
	
	if(!cur)
		return;

	int fxid = 0;
	int np = 0;
	int i;
	char msg[100];

	vevo_property_get( cur, "fxid", 0, &fxid );
	vevo_property_get( cur, "num_param", 0, &np );

	sprintf( msg, "%03d:%d %d %d ", VIMS_CHAIN_ENTRY_SET_PRESET,0, 
			info->uc.selected_chain_entry, fxid );

	for( i =  0; i < np; i ++ )
	{
		char pname[5];
		sprintf(pname, "parameter%d",i);
		int value = 0;
		vevo_property_get(cur, pname,0, &value);
		char token[10];
		sprintf(token, "%d ", value);
		strcat(msg,token);
	}

	int len = strlen(msg);
	msg[len-1] = ';';
	msg[len]   = '\0';
	msg_vims( msg );
	info->uc.reload_hint[HINT_ENTRY] = 1;
}

static	gint load_parameter_info()
{
	int	*st = &(info->uc.entry_tokens[0]);
	int	len = 0;
	int	p[16];
	int	*q = &p[3];
	int 	i;
		
	multi_vims( VIMS_CHAIN_GET_ENTRY, "%d %d", 0, 
		info->uc.selected_chain_entry );

	gchar *answer = recv_vims(3,&len);
	if(len <= 0 || answer == NULL )
	{
		if(answer) g_free(answer);
		for( i = 0; i < 16; i ++ )
			st[i] = 0;
		if(info->uc.selected_rgbkey && skin__ == 0)
			disable_widget("rgbkey");
		return 0;
	}
	int res = sscanf( answer,
		"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
		p+0,p+1,p+2,p+3,p+4,p+5,p+6,p+7,p+8,p+9,p+10,
		p+11,p+12,p+13,p+14,p+15);
	if( res <= 0 )
	{
		for( i = 0; i < 16; i ++ )
			st[i] = 0;
		return 0;
	}

	if( skin__ == 0 )
	{
		info->uc.selected_rgbkey = _effect_get_rgb( p[0] );
		if(info->uc.selected_rgbkey)
		{
			enable_widget( "rgbkey");
			update_rgbkey();
		}  
	     	else
		{
			disable_widget( "rgbkey");
			info->uc.selected_rgbkey = 0;
		}	 
	}
		
	for( i = 0; i < 16; i ++ )
		st[i] = p[i];

	update_clipboard( p[0],p[2], q);

	g_free(answer);

	return 1;
}	  

// load effect chain
static	void	load_effectchain_info()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_chain");
	GtkListStore *store;
	
	GtkTreeIter iter;
	gint offset=0;

	gint fxlen = 0;
	multi_vims( VIMS_CHAIN_LIST,"%d",0 );
	gchar *fxtext = recv_vims(3,&fxlen);

	reset_tree( "tree_chain" );

	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	if(fxlen <= 0 )
	{
		int i;
		for( i = 0; i < 20; i ++ )
		{
			gtk_list_store_append(store,&iter);
			gtk_list_store_set(store,&iter, FXC_ID, i ,-1);	
			gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
		}
		return;
	}

	if(fxlen == 5 )
		offset = fxlen;
	

	gint last_index =0;

	while( offset < fxlen )
	{
		gchar toggle[4];
		gchar kf_toggle[4];
		guint arr[6];
		bzero(toggle,4);
		bzero(kf_toggle,4);
		memset(arr,0,sizeof(arr));
		char line[12];
		bzero(line,12);
		strncpy( line, fxtext + offset, 8 );
		sscanf( line, "%02d%03d%1d%1d%1d",
			&arr[0],&arr[1],&arr[2],&arr[3],&arr[4]);

		
		char *name = _effect_get_description( arr[1] );
		sprintf(toggle,"%s",
			arr[3] == 1 ? "on" : "off" );

		while( last_index < arr[0] )
		{
			gtk_list_store_append( store, &iter );
			gtk_list_store_set( store, &iter, FXC_ID, last_index,-1);
			last_index ++;
		}

		if( last_index == arr[0])
		{
			gchar *utf8_name = _utf8str( name );
			sample_slot_t *s = info->selected_slot;
			int on = 0;
			if(s && s->ec) 
				on = s->ec->effects[last_index]->enabled;

			gtk_list_store_append( store, &iter );
		/*	gtk_list_store_set( store, &iter,
				FXC_ID, arr[0],
				FXC_FXID, utf8_name,
				FXC_FXSTATUS, utf8_toggle,
				FXC_KF, utf8_kf, -1 );*/
			GdkPixbuf *toggle = update_pixmap_entry( arr[3] );
			GdkPixbuf *kf_toggle = update_pixmap_kf( on );
			gtk_list_store_set( store, &iter,
				FXC_ID, arr[0],
				FXC_FXID, utf8_name,
				FXC_FXSTATUS, toggle,
				FXC_KF, kf_toggle, -1 );
			last_index ++;
			g_free(utf8_name);
			g_object_unref( toggle );
			g_object_unref( kf_toggle );
		}
		offset += 8;
	}
	while( last_index < 20 )
	{
		gtk_list_store_append( store, &iter );
		gtk_list_store_set( store, &iter,
			FXC_ID, last_index , -1 );
		last_index ++;
	}
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_free(fxtext);

}

enum 
{
//	FX_ID = 0,
	FX_STRING = 0,
	FX_NUM,
};

gboolean
  view_fx_selection_func (GtkTreeSelection *selection,
                       GtkTreeModel     *model,
                       GtkTreePath      *path,
                       gboolean          path_currently_selected,
                       gpointer          userdata)
  {
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path))
    {
  //    gint name;
      gchar *name = NULL;
      gtk_tree_model_get(model, &iter, FX_STRING, &name, -1);

      if (!path_currently_selected)
      {
	int value = 0;
	vevo_property_get( fx_list_, name, 0, &value );
	if(value) info->uc.selected_effect_id = value;
      }
      g_free(name);

    }

    return TRUE; /* allow selection state to change */
  }
void
on_effectmixlist_row_activated(GtkTreeView *treeview,
		GtkTreePath *path,
		GtkTreeViewColumn *col,
		gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(treeview);
	if(gtk_tree_model_get_iter(model,&iter,path))
	{
		gint gid =0;
		gchar *name = NULL;
		gtk_tree_model_get(model,&iter, FX_STRING, &name, -1); // FX_ID

		if(vevo_property_get( fx_list_, name, 0,&gid ) == 0 )
		{
			multi_vims(VIMS_CHAIN_ENTRY_SET_EFFECT, "%d %d %d",
				0, info->uc.selected_chain_entry,gid );
			info->uc.reload_hint[HINT_ENTRY] = 1;
		}
		g_free(name);
	}
}
void
on_effectlist_row_activated(GtkTreeView *treeview,
		GtkTreePath *path,
		GtkTreeViewColumn *col,
		gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(treeview);

	if(gtk_tree_model_get_iter(model,&iter,path))
	{
		gint gid =0;
		gchar *name = NULL;
		gtk_tree_model_get(model,&iter, FX_STRING, &name, -1);

		if(vevo_property_get( fx_list_, name, 0, &gid ) == 0 )
		{
			multi_vims(VIMS_CHAIN_ENTRY_SET_EFFECT, "%d %d %d",
				0, info->uc.selected_chain_entry,gid );
			if(info->status_tokens[PLAY_MODE] != MODE_PLAIN)
				vj_kf_delete_parameter(info->uc.selected_chain_entry);
			info->uc.reload_hint[HINT_ENTRY] = 1;
		}
		g_free(name);
	}

}
gint
sort_iter_compare_func( GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
		gpointer userdata)
{
	gint sortcol = GPOINTER_TO_INT(userdata);
	gint ret = 0;

	if(sortcol == FX_STRING)
	{
		gchar *name1=NULL;
		gchar *name2=NULL;
		gtk_tree_model_get(model,a, FX_STRING, &name1, -1 );
		gtk_tree_model_get(model,b, FX_STRING, &name2, -1 );
		if( name1 == NULL || name2 == NULL )
		{
			if( name1==NULL && name2==NULL)
			{
				return 0;
			}
			ret = (name1 == NULL) ? -1 : 1;
		}
		else
		{
			ret = g_utf8_collate(name1,name2);
		}
		if(name1) g_free(name1);
		if(name2) g_free(name2);
	}
	return ret;
}



gint
sort_vims_func( GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
		gpointer userdata)
{
	gint sortcol = GPOINTER_TO_INT(userdata);
	gint ret = 0;
	if(sortcol == VIMS_ID)
	{
		gchar *name1 = NULL;
		gchar *name2 = NULL;
	
		gtk_tree_model_get(model,a, VIMS_ID, &name1, -1 );
		gtk_tree_model_get(model,b, VIMS_ID, &name2, -1 );
		if( name1 == NULL || name2 == NULL )
		{
			if( name1==NULL && name2== NULL)
			{
				return 0;
			}
			ret = (name1==NULL) ? -1 : 1;
		} 
		else
		{
			ret = g_utf8_collate(name1,name2);
		}
		if(name1) g_free(name1);
		if(name2) g_free(name2);
	}
	return ret;
}

// load effectlist from veejay
void	setup_effectlist_info()
{
	int i;
	GtkWidget *trees[2];
	trees[0] = glade_xml_get_widget_( info->main_window, "tree_effectlist");
	trees[1] = glade_xml_get_widget_( info->main_window, "tree_effectmixlist");
	GtkListStore *stores[2];
	stores[0] = gtk_list_store_new( 1, G_TYPE_STRING );
	stores[1] = gtk_list_store_new( 1, G_TYPE_STRING );


	fx_list_ = (vevo_port_t*) vpn( 200 );

	for(i = 0; i < 2; i ++ )
	{
		GtkTreeSortable *sortable = GTK_TREE_SORTABLE(stores[i]);
		gtk_tree_sortable_set_sort_func(
			sortable, FX_STRING, sort_iter_compare_func,
				GINT_TO_POINTER(FX_STRING),NULL);

		gtk_tree_sortable_set_sort_column_id( 
			sortable, FX_STRING, GTK_SORT_ASCENDING);

		gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(trees[i]), FALSE );

		gtk_tree_view_set_model( GTK_TREE_VIEW(trees[i]), GTK_TREE_MODEL(stores[i]));
		g_object_unref( G_OBJECT( stores[i] ));
	}


//	setup_tree_text_column( "tree_effectlist", FX_ID, "id" );
	setup_tree_text_column( "tree_effectlist", FX_STRING, "effect" );

//	setup_tree_text_column( "tree_effectmixlist", FX_ID, "id" );
	setup_tree_text_column( "tree_effectmixlist", FX_STRING, "effect" );

	g_signal_connect( trees[0], "row-activated",
		(GCallback) on_effectlist_row_activated, NULL );

	g_signal_connect( trees[1] ,"row-activated",
		(GCallback) on_effectmixlist_row_activated, NULL );

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(trees[0]));
    	gtk_tree_selection_set_select_function(selection, view_fx_selection_func, NULL, NULL);
    	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
	
	selection = gtk_tree_view_get_selection( GTK_TREE_VIEW(trees[1] ));
	gtk_tree_selection_set_select_function( selection, view_fx_selection_func, NULL,NULL );
	gtk_tree_selection_set_mode( selection, GTK_SELECTION_SINGLE );

}


void
on_effectlist_sources_row_activated(GtkTreeView *treeview,
		GtkTreePath *path,
		GtkTreeViewColumn *col,
		gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(treeview);


	if(gtk_tree_model_get_iter(model,&iter,path))
	{
		gchar *idstr = NULL;
		gtk_tree_model_get(model,&iter, SL_ID, &idstr, -1);
		gint id = 0;
		if( sscanf( idstr+1, "%d", &id ) )
		{
		    // set source / channel
		    multi_vims( VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
			"%d %d %d %d",
			0,
			info->uc.selected_chain_entry,
			( idstr[0] == 'T' ? 1 : 0 ),
			id );	
		    vj_msg(VEEJAY_MSG_INFO, "Set source channel to %d, %d", info->uc.selected_chain_entry,id );
		}
		if(idstr) g_free(idstr);
	}
}
/*
void	on_samplelist_edited(GtkCellRendererText *cell,
		gchar *path_string,
		gchar *new_text,
		gpointer user_data)
{
	// welke sample id is klikked?
	GtkWidget *tree = glade_xml_get_widget_(info->main_window,
				"tree_samples");
	GtkTreeIter iter;
	gchar *id = NULL;
	int n;
	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	n = gtk_tree_model_get_iter_from_string(
		model,
		&iter,
		path_string);

	gtk_tree_model_get(
		model,
		&iter,
		SL_ID,
		&id,
		-1 );	

	int br=0; int bw=0;
	gchar *sysid = g_locale_from_utf8( id, -1, &br, &bw,NULL);	
	unsigned  int sample_id =  (unsigned int )atoi(sysid+1);
	if(sample_id > 0)
	{
		char digits[2] = { 48, 57 };
		char alphauc[2] = { 65,90 };  
		char alphalw[2] = { 97,122};
		char specials[3] = { 95,46,45 };

		// convert spaces,tabs to ..
		gint bytes_read = 0;
		gint bytes_written = 0;
		GError *error = NULL;
		gchar *sysstr = g_locale_from_utf8( new_text, -1, &bytes_read, &bytes_written,&error);	
	
		if(error)
		{
			vj_msg_detail(VEEJAY_MSG_ERROR,"Invalid string: %s", error->message );
			if(sysstr) g_free(sysstr);
			if(sysid) g_free(sysid);
			if(id)	g_free(id);
			return;
		}
		
		int i;
		char descr[150];
		bzero(descr,150);
		char *res = &descr[0];
		for( i = 0; i < bytes_written; i ++ )
		{
			char c = sysstr[i];
			int  j;
			if(c >= digits[0] && c <= digits[1])
				*(res)++  = c;
			if(c >= alphauc[0] && c <= alphauc[1])
				*(res)++ = c;
			if(c >= alphalw[0] && c <= alphalw[1]) 
				*(res)++ = c;
			if( c == 32)
				*(res)++ = '_';
			for( j = 0; j < 3; j ++ )
				if( specials[j]  == c )
					*(res)++ = c;

		}

		if( id[0] == 'S' )
			multi_vims( VIMS_SAMPLE_SET_DESCRIPTION,
				"%d %s", sample_id, descr );
		else
			multi_vims( VIMS_STREAM_SET_DESCRIPTION,
				"%d %s", sample_id, descr );		

		if(info->selected_slot->sample_id == sample_id )
		{
			if(info->selected_slot->title )
				free(info->selected_slot->title );	
			info->selected_slot->title = strdup( descr );
			gtk_label_set_text( GTK_LABEL( info->selected_gui_slot->title ),
				info->selected_slot->title );
		}
		

		if(sysstr) g_free( sysstr );
	}

	if(sysid) g_free(sysid);
	if(id) g_free(id);
}
*/

/* Return a bank page and slot number to place sample in */

int	verify_bank_capacity(int *bank_page_, int *slot_, int sample_id, int sample_type )
{
	int poke_slot = 0;
	int bank_page = find_bank_by_sample( sample_id, sample_type, &poke_slot );
			
	if(bank_page == -1)
		return 0;

	if( !bank_exists(bank_page, poke_slot))
		add_bank( bank_page );

	*bank_page_ = bank_page;
	*slot_      = poke_slot;

	return 1;
}


void	setup_samplelist_info()
{
	effect_sources_tree = glade_xml_get_widget_( info->main_window, "tree_sources");
	effect_sources_store = gtk_list_store_new( 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING );

	gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(effect_sources_tree), FALSE );

	gtk_tree_view_set_model( GTK_TREE_VIEW(effect_sources_tree), GTK_TREE_MODEL(effect_sources_store));
	g_object_unref( G_OBJECT( effect_sources_store ));	
	effect_sources_model = gtk_tree_view_get_model( GTK_TREE_VIEW(effect_sources_tree ));	
	effect_sources_store = GTK_LIST_STORE(effect_sources_model);

	setup_tree_text_column( "tree_sources", SL_ID, "Id" );
//	setup_tree_texteditable_column( "tree_sources", SL_DESCR , "Title" ,G_CALLBACK(on_samplelist_edited));
	setup_tree_text_column( "tree_sources", SL_TIMECODE, "Length" );

	GtkTreeSelection *selection;
  	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(effect_sources_tree));
    	gtk_tree_selection_set_select_function(selection, view_sources_selection_func, NULL, NULL);
    	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

//	g_signal_connect( effect_sources_tree, "edited", 
//		(GCallback) on_samplelist_edited, (gpointer*) "tree_sources" );

	g_signal_connect( effect_sources_tree, "row-activated", (GCallback) on_effectlist_sources_row_activated, (gpointer*)"tree_sources");
}


void	load_effectlist_info()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_effectlist");
	GtkWidget *tree2 = glade_xml_get_widget_( info->main_window, "tree_effectmixlist");
	GtkListStore *store,*store2;
	
	GtkTreeIter iter;
	gint i,offset=0;
	
	
	gint fxlen = 0;
	single_vims( VIMS_EFFECT_LIST );
	gchar *fxtext = recv_vims(5,&fxlen);
	_effect_reset();
 	reset_tree( "tree_effectlist");
//	store = gtk_list_store_new( 3, G_TYPE_INT,GDK_TYPE_PIXBUF , G_TYPE_STRING );
	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	GtkTreeModel *model2 = gtk_tree_view_get_model( GTK_TREE_VIEW(tree2));
	store2 = GTK_LIST_STORE(model2);
	while( offset < fxlen )
	{
		char tmp_len[4];
		bzero(tmp_len, 4);
		strncpy(tmp_len, fxtext + offset, 3 );
		int  len = atoi(tmp_len);
		offset += 3;
		if(len > 0)
		{
			effect_constr *ec;
			char line[255];
			bzero( line, 255 );
			strncpy( line, fxtext + offset, len );
			ec = _effect_new(line);
			if(ec) info->effect_info = g_list_append( info->effect_info, ec );
		}
		offset += len;
	}

	fxlen = g_list_length( info->effect_info );
	for( i = 0; i < fxlen; i ++)
	{	

		effect_constr *ec = g_list_nth_data( info->effect_info, i );
		gchar *name = _utf8str( _effect_get_description( ec->id ) );
		if( name != NULL)
		{
			if( _effect_get_mix(ec->id) > 0 )
			{
				gtk_list_store_append( store2, &iter );
	//			gtk_list_store_set( store2, &iter, FX_ID,(guint) ec->id, FX_STRING, name, -1 );
				gtk_list_store_set( store2, &iter, FX_STRING, name, -1 );
				vevo_property_set( fx_list_, name, LIVIDO_ATOM_TYPE_INT, 1, &(ec->id));
			}
			else
			{
				gtk_list_store_append( store, &iter );
			//	gtk_list_store_set( store, &iter, FX_ID,(guint) ec->id, FX_STRING, name, -1 );
				gtk_list_store_set( store, &iter, FX_STRING, name, -1 );
				vevo_property_set( fx_list_, name, LIVIDO_ATOM_TYPE_INT, 1, &(ec->id));

			}

		}
		g_free(name);
	}


	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
//	g_object_unref( G_OBJECT( store ) );
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree2), GTK_TREE_MODEL(store2));
	g_free(fxtext);
	
}

static	void	select_slot__( int pm )
{
	lock_preview();

	int *history = info->history_tokens[ pm ];
	if( pm != MODE_PLAIN )
	{
		if(info->status_tokens[CURRENT_ID] != history[CURRENT_ID] ||
			pm != history[PLAY_MODE] )
		{
			if(info->selected_slot)
			{
				set_activation_of_slot_in_samplebank(FALSE);
				info->selected_slot= NULL;
				info->selected_gui_slot=NULL;
			}
		}
		if(info->selected_slot == NULL || info->selected_slot->sample_id !=
			info->status_tokens[CURRENT_ID] || pm != info->selected_slot->sample_type)
		{
			int b = 0; int p = 0;
			/* falsify activation */
			if(info->status_tokens[CURRENT_ID] > 0)
			{
				if(verify_bank_capacity( &b, &p, info->status_tokens[CURRENT_ID],
					info->status_tokens[PLAY_MODE] ))
				{
					info->selected_slot = info->sample_banks[b]->slot[p];
					info->selected_gui_slot = info->sample_banks[b]->gui_slot[p];
					set_activation_of_slot_in_samplebank(TRUE);
					no_preview_ = 0;
				}
			}	
		}
	}
	else
	{
		if(info->selected_slot)	 
			set_activation_of_slot_in_samplebank(FALSE);
		info->selected_slot = NULL;
		info->selected_gui_slot = NULL;
	}
	unlock_preview();

}

static	void	select_slot_(int pm, const char *f, int line)
{
//	veejay_msg(0, "%s called by %s %d", __FUNCTION__,f,line);
	select_slot__(pm);
}



/* execute after sample/stream/mixing sources list update
 * same for load_mixlist_info(), only different widget !! 
 * with_reset_slotselection should be TRUE when the reset
 * of the samplebanks that is used here should also reset the
 * current selection of a slot */
static	void	load_samplelist_info(gboolean with_reset_slotselection)
{
	gint offset=0;	
	int has_samples = 0;
	int has_streams = 0;
	int n_slots = 0;
	reset_tree( "tree_sources" );

	if( with_reset_slotselection )
	{
	//	reset_tree( "tree_sources" );
		reset_samplebank();
	}
	multi_vims( VIMS_SAMPLE_LIST,"%d", 0 );
	gint fxlen = 0;
	gchar *fxtext = recv_vims(5,&fxlen);
	if(fxlen > 0 && fxtext != NULL)
	{
		has_samples = 1;
		while( offset < fxlen )
		{
			char tmp_len[4];
			bzero(tmp_len, 4);
			strncpy(tmp_len, fxtext + offset, 3 );
			int  len = atoi(tmp_len);
			offset += 3;
			if(len > 0)
			{
				char line[300];
				char descr[255];
				bzero( line, 300 );
				bzero( descr, 255 );
				strncpy( line, fxtext + offset, len );
				
				int values[4];
				sscanf( line, "%05d%09d%09d%03d%s",
					&values[0], &values[1], &values[2], &values[3],	
					descr );
				gchar *title = _utf8str( descr );
				gchar *timecode = format_selection_time( 0,(values[2]-values[1]) );

				int int_id = values[0];
				int poke_slot= 0; int bank_page = 0;
				verify_bank_capacity( &bank_page , &poke_slot, int_id, 0);
				if(bank_page >= 0 )
				{			
					if( info->sample_banks[bank_page]->slot[poke_slot]->sample_id <= 0 )
					{
						sample_slot_t *tmp_slot = create_temporary_slot(poke_slot,int_id,0, title,timecode );
						add_sample_to_sample_banks(bank_page, tmp_slot );					
						free_slot(tmp_slot);	
						n_slots ++;			
					}
					else
					{
						   update_sample_slot_data( bank_page, poke_slot, int_id,0,title,timecode);
					}				
				}
				g_free(timecode);
				g_free(title);
			}
			offset += len;
		}
		offset = 0;
	}

	if( fxtext ) g_free(fxtext);
	fxlen = 0;

	multi_vims( VIMS_STREAM_LIST,"%d",0 );
	fxtext = recv_vims(5, &fxlen);
	if( fxlen > 0 && fxtext != NULL)
	{
		has_streams = 1;
		while( offset < fxlen )
		{
			char tmp_len[4];
			bzero(tmp_len, 4);
			strncpy(tmp_len, fxtext + offset, 3 );
	
			int  len = atoi(tmp_len);
			offset += 3;
			if(len > 0)
			{
				char line[300];
				char source[255];
				char descr[255];
				bzero( line, 300 );
				bzero( descr, 255 );
				bzero( source, 255 ); 
				strncpy( line, fxtext + offset, len );
	
				int values[4];
	
				sscanf( line, "%05d%02d%03d%03d%03d%03d%03d%03d",
					&values[0], &values[1], &values[2], 
					&values[3], &values[4], &values[5],
					&values[6], &values[7]
				);
				strncpy( descr, line + 22, values[6] );
				switch( values[1] )
				{
					case STREAM_VIDEO4LINUX :sprintf(source,"Video4Linux stream");break;
					case STREAM_WHITE	:sprintf(source,"Solid stream"); 
								 sprintf(descr,"infinite"); 
								 break;
					case STREAM_MCAST	:sprintf(source,"Multicast stream");break;
					case STREAM_NETWORK	:sprintf(source,"Unicast stream");break;
					case STREAM_YUV4MPEG	:sprintf(source,"Yuv4Mpeg file stream");break;
					case STREAM_AVFORMAT	:sprintf(source,"libavformat stream");break;
					case STREAM_DV1394	:sprintf(source,"DV1394 Camera stream");break;
					case STREAM_PICTURE	:sprintf(source,"Image stream");break;
					default:
					sprintf(source,"Streaming from unknown");	
				}
				gchar *gsource = _utf8str( descr );
				gchar *gtype = _utf8str( source );

				int bank_page = 0; int poke_slot = 0;
				int full = verify_bank_capacity( &bank_page , &poke_slot, values[0], 1);
				if(bank_page >= 0 && full == 0 )
				{			
					if( info->sample_banks[bank_page]->slot[poke_slot] <= 0 )
					{				
						sample_slot_t *tmp_slot = create_temporary_slot(poke_slot,values[0],1, gtype,gsource );
						add_sample_to_sample_banks(bank_page, tmp_slot );								n_slots ++;
						free_slot(tmp_slot);	
					}
					else
					{
					update_sample_slot_data( bank_page, poke_slot, values[0],1,gsource,gtype);
					}
				}
				g_free(gsource);
				g_free(gtype);
			}
			offset += len;
		}

	}

	if(fxtext) g_free(fxtext);
}

gboolean
  view_el_selection_func (GtkTreeSelection *selection,
                       GtkTreeModel     *model,
                       GtkTreePath      *path,
                       gboolean          path_currently_selected,
                       gpointer          userdata)
  {
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path))
    {
      gint num = 0;

      gtk_tree_model_get(model, &iter, COLUMN_INT, &num, -1);

      if (!path_currently_selected)
      {
		info->uc.selected_el_entry = num;
		gint frame_num =0;
		frame_num = _el_ref_start_frame( num );
		update_spin_value( "button_el_selstart",
			frame_num);
		update_spin_value( "button_el_selend",
			_el_ref_end_frame( num ) );
      }

    }
    return TRUE; /* allow selection state to change */
  }

void
on_vims_row_activated(GtkTreeView *treeview,
		GtkTreePath *path,
		GtkTreeViewColumn *col,
		gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(treeview);

	if(gtk_tree_model_get_iter(model,&iter,path))
	{
		gchar *vimsid = NULL;
		gint event_id =0;
		gtk_tree_model_get(model,&iter, VIMS_ID, &vimsid, -1);

		if(sscanf( vimsid, "%d", &event_id ))
		{
			if(event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END)
			{
				multi_vims( VIMS_BUNDLE, "%d", event_id );
				info->uc.reload_hint[HINT_CHAIN] = 1;
			}
			else
			{
				gchar *args = NULL;
				gchar *format = NULL;
				gtk_tree_model_get(model,&iter, VIMS_FORMAT,  &format, -1);
				gtk_tree_model_get(model,&iter, VIMS_CONTENTS, &args, -1 );
	
				if( event_id == VIMS_QUIT )
				{
					if( prompt_dialog("Stop Veejay", "Are you sure  ? (All unsaved work will be lost)" ) ==	
							GTK_RESPONSE_REJECT )
					return;	
				}
				if( (format == NULL||args==NULL) || (strlen(format) <= 0) )
					single_vims( event_id );
				else
				{
					if( args == NULL || strlen(args) <= 0 )
						vj_msg_detail(VEEJAY_MSG_ERROR,"VIMS %d requires arguments!", event_id);
					else
						multi_vims( event_id, format, args );
				}
			}
		}
		if( vimsid ) g_free( vimsid );
	}
}
void
on_vimslist_row_activated(GtkTreeView *treeview,
		GtkTreePath *path,
		GtkTreeViewColumn *col,
		gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(treeview);

	if(gtk_tree_model_get_iter(model,&iter,path))
	{
		gchar *vimsid = NULL;
		gint event_id = 0;
		gtk_tree_model_get(model,&iter, VIMS_ID, &vimsid, -1);


		if(!sscanf(vimsid, "%d", &event_id))
		{
			return;
		}	
		info->uc.selected_vims_entry = event_id;

		info->uc.selected_key_mod = 0;
		info->uc.selected_key_sym = 0;

		if(sscanf( vimsid, "%d", &event_id ) && event_id > 0)
		{
			char msg[100];
			sprintf(msg, "Press a key for VIMS %03d", event_id);
			// prompt for key!
			int n = prompt_keydialog("Attach key to VIMS event", msg);
			if( n == GTK_RESPONSE_ACCEPT )
			{
				int key_val = gdk2sdl_key( info->uc.pressed_key );
				int key_mod = gdk2sdl_mod( info->uc.pressed_mod );
				multi_vims(
					VIMS_BUNDLE_ATTACH_KEY,
					"%d %d %d",  // 4th = string arguments
					event_id, key_val, key_mod );	
				info->uc.reload_hint[HINT_BUNDLES] = 1;
			}		
		}
		if( vimsid ) g_free(vimsid);
	}
}

gboolean
  view_vimslist_selection_func (GtkTreeSelection *selection,
                       GtkTreeModel     *model,
                       GtkTreePath      *path,
                       gboolean          path_currently_selected,
                       gpointer          userdata)
  {
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path))
    {
	gchar *vimsid = NULL;
 	gint event_id = 0;

        gtk_tree_model_get(model, &iter, VIMS_ID, &vimsid, -1);

	if(sscanf( vimsid, "%d", &event_id ))
	{
		info->uc.selected_vims_id = event_id;
    	}
	if(vimsid) g_free(vimsid);
    }

    return TRUE; /* allow selection state to change */
  }


gboolean
  view_vims_selection_func (GtkTreeSelection *selection,
                       GtkTreeModel     *model,
                       GtkTreePath      *path,
                       gboolean          path_currently_selected,
                       gpointer          userdata)
  {
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path))
    {
	gchar *vimsid = NULL;
 	gint event_id = 0;
	gchar *text = NULL;

        gtk_tree_model_get(model, &iter, VIMS_ID, &vimsid, -1);
	gtk_tree_model_get(model, &iter, VIMS_CONTENTS, &text, -1 );
	int k=0; 
	int m=0;
	gchar *key = NULL;
	gchar *mod = NULL;

	gtk_tree_model_get(model,&iter, VIMS_KEY, &key, -1);
	gtk_tree_model_get(model,&iter, VIMS_MOD, &mod, -1);

	if(sscanf( vimsid, "%d", &event_id ))
	{
		k = sdlkey_by_name( key );
		m = sdlmod_by_name( mod );	

		if( event_id > 500 && event_id < 600 )
			set_textview_buffer( "vimsview", text );

		if( info->uc.selected_arg_buf != NULL )
			free(info->uc.selected_arg_buf );
		info->uc.selected_arg_buf = NULL;
		if( vj_event_list[event_id].params > 0 )
			info->uc.selected_arg_buf = (text == NULL ? NULL: strdup( text ));

		info->uc.selected_vims_entry = event_id;
		if(k > 0)
		{
			info->uc.selected_key_mod = m;
			info->uc.selected_key_sym = k;
		}
    	}
	if(vimsid) g_free( vimsid );
	if(text) g_free( text );
	if(key) g_free( key );
	if(mod) g_free( mod );
    }

    return TRUE; /* allow selection state to change */
  }

void 
on_editlist_row_activated(GtkTreeView *treeview,
		GtkTreePath *path,
		GtkTreeViewColumn *col,
		gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	model = gtk_tree_view_get_model(treeview);
	if(gtk_tree_model_get_iter(model,&iter,path))
	{
		gint num = 0;
		gtk_tree_model_get(model,&iter, COLUMN_INT, &num, -1);
		gint frame_num = _el_ref_start_frame( num );

		multi_vims( VIMS_VIDEO_SET_FRAME, "%d", (int) frame_num );
	}
}

void
on_stream_color_changed(GtkColorSelection *colorsel, gpointer user_data)
{
	if(!info->status_lock && info->selected_slot)
	{
	GdkColor current_color;
	GtkWidget *colorsel = glade_xml_get_widget_(info->main_window,
			"colorselection" );
	gtk_color_selection_get_current_color(
		GTK_COLOR_SELECTION( colorsel ),
		&current_color );

	// scale to 0 - 255
	gint red = current_color.red / 256.0;
	gint green = current_color.green / 256.0;
	gint blue = current_color.blue / 256.0;

	multi_vims( VIMS_STREAM_COLOR, "%d %d %d %d",
		info->selected_slot->sample_id,
		red,
		green,
		blue
		);
	}

}



static	void	setup_colorselection()
{
	GtkWidget *sel = glade_xml_get_widget_(info->main_window, "colorselection");
	g_signal_connect( sel, "color-changed",
		(GCallback) on_stream_color_changed, NULL );

}

void
on_rgbkey_color_changed(GtkColorSelection *colorsel, gpointer user_data)
{
	if(!info->entry_lock)
	{
		GdkColor current_color;
		GtkWidget *colorsel = glade_xml_get_widget_(info->main_window,
			"rgbkey" );
		gtk_color_selection_get_current_color(
			GTK_COLOR_SELECTION( colorsel ),
			&current_color );

		// scale to 0 - 255
		gint red = current_color.red / 256.0;
		gint green = current_color.green / 256.0;
		gint blue = current_color.blue / 256.0;

		// send p1,p2,p3 to veejay
		// (if effect currently on entry has rgb, this will be enabled but not here) 
		multi_vims( 
			VIMS_CHAIN_ENTRY_SET_ARG_VAL, "%d %d %d %d",
			0, info->uc.selected_chain_entry, 1, red );
		multi_vims(
			VIMS_CHAIN_ENTRY_SET_ARG_VAL, "%d %d %d %d",
			0, info->uc.selected_chain_entry, 2, green );
		multi_vims(
			VIMS_CHAIN_ENTRY_SET_ARG_VAL, "%d %d %d %d",
			0, info->uc.selected_chain_entry, 3, blue );

		info->parameter_lock = 1;
		update_slider_value(
			"slider_p1", red, 0 );
		update_slider_value(
			"slider_p2", green, 0 );
		update_slider_value(
			"slider_p3", blue, 0 );	
	
		info->parameter_lock = 0;
	}

}


static	void	setup_rgbkey()
{
	if( skin__ == 0 )
	{
		GtkWidget *sel = glade_xml_get_widget_(info->main_window, "rgbkey");
		g_signal_connect( sel, "color-changed",
			(GCallback) on_rgbkey_color_changed, NULL );
	}
}

static	void	setup_vimslist()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_vims");
	GtkListStore *store = gtk_list_store_new( 2,G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "tree_vims", VIMS_ID, 		"VIMS ID");
	setup_tree_text_column( "tree_vims", VIMS_DESCR,	"Description" );

	GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);

	gtk_tree_sortable_set_sort_func(
		sortable, VIMS_ID, sort_vims_func,
			GINT_TO_POINTER(VIMS_ID),NULL);

	gtk_tree_sortable_set_sort_column_id( 
		sortable, VIMS_ID, GTK_SORT_ASCENDING);

	g_signal_connect( tree, "row-activated",
		(GCallback) on_vimslist_row_activated, NULL );

  	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    	gtk_tree_selection_set_select_function(selection, view_vimslist_selection_func, NULL, NULL);
   	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
}


void	on_vimslist_edited(GtkCellRendererText *cell,
		gchar *path_string,
		gchar *new_text,
		gpointer user_data
		)
{
	// welke sample id is klikked?
	GtkWidget *tree = glade_xml_get_widget_(info->main_window,
				"tree_bundles");
	GtkTreeIter iter;
	gchar *id = NULL;
	gchar *contents = NULL;
	gchar *format = NULL;
	gchar *key_sym = NULL;
	gchar *key_mod = NULL;
	gint event_id = 0;
	int n;
	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	n = gtk_tree_model_get_iter_from_string(
		model,
		&iter,
		path_string);

	// error !!

	gtk_tree_model_get(
		model,
		&iter,
		VIMS_ID,
		&id,
		-1);
	gtk_tree_model_get(
		model,
		&iter,
		VIMS_FORMAT,
		&format,
		-1);
	gtk_tree_model_get(
		model,
		&iter,
		VIMS_CONTENTS,
		&contents
		-1 );	
	gtk_tree_model_get(
		model,
		&iter,
		VIMS_KEY,
		&key_sym,
		-1);
	gtk_tree_model_get(
		model,
		&iter,
		VIMS_MOD,
		&key_mod
		-1 );	

	sscanf( id, "%d", &event_id );

	gint bytes_read = 0;
	gint bytes_written = 0;
	GError *error = NULL;
	gchar *sysstr = g_locale_from_utf8( new_text, -1, &bytes_read, &bytes_written,&error);	
	
	if(sysstr == NULL || error != NULL)
	{
		goto vimslist_error;
	}

	if( event_id < VIMS_BUNDLE_START || event_id > VIMS_BUNDLE_END ) 	
	{
		int np = vj_event_list[ event_id ].params; 
		char *c = format+1;
		int i;	
		int tmp_val = 0;
		int k = sdlkey_by_name( key_sym );
		int m = sdlmod_by_name( key_mod );

		for( i =0 ; i < np; i ++ )
		{
			if(*(c) == 'd')
			  if( sscanf( sysstr, "%d", &tmp_val ) != 1 )	 
				goto vimslist_error;
			c+=2;
		}

		multi_vims( VIMS_BUNDLE_ATTACH_KEY, "%d %d %d %s",
			event_id, k, m, sysstr );
		info->uc.reload_hint[HINT_BUNDLES]=1;
		
	}	

	vimslist_error:

	if(sysstr) g_free(sysstr);
	if(id) g_free(id);
	if(contents) g_free(contents);
	if(key_sym) g_free(key_sym);
	if(key_mod) g_free(key_mod);
	if(format) g_free(format);

}

static	void	setup_bundles()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_bundles");
	GtkListStore *store = gtk_list_store_new( 7,G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING ,G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

	gtk_widget_set_size_request( tree, 300, -1 );

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);

	gtk_tree_sortable_set_sort_func(
		sortable, VIMS_ID, sort_vims_func,
			GINT_TO_POINTER(VIMS_ID),NULL);

	gtk_tree_sortable_set_sort_column_id( 
		sortable, VIMS_ID, GTK_SORT_ASCENDING);

	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "tree_bundles", VIMS_ID, 	"VIMS");
	setup_tree_text_column( "tree_bundles", VIMS_DESCR,     "Description" );
	setup_tree_text_column( "tree_bundles", VIMS_KEY, 	"Key");
	setup_tree_text_column( "tree_bundles", VIMS_MOD, 	"Mod");
	setup_tree_text_column( "tree_bundles", VIMS_PARAMS,	"Max args");
	setup_tree_text_column( "tree_bundles", VIMS_FORMAT,	"Format" );
	setup_tree_texteditable_column( "tree_bundles", VIMS_CONTENTS,	"Content", G_CALLBACK(on_vimslist_edited) );

	g_signal_connect( tree, "row-activated",
		(GCallback) on_vims_row_activated, NULL );

  	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    	gtk_tree_selection_set_select_function(selection, view_vims_selection_func, NULL, NULL);
    	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

	GtkWidget *tv = glade_xml_get_widget_( info->main_window, "vimsview" );
	gtk_text_view_set_wrap_mode( GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR );
	
}

static	void	setup_editlist_info()
{
	editlist_tree = glade_xml_get_widget_( info->main_window, "editlisttree");
	editlist_store = gtk_list_store_new( 5,G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING ,G_TYPE_STRING);
	gtk_tree_view_set_model( GTK_TREE_VIEW(editlist_tree), GTK_TREE_MODEL(editlist_store));
	g_object_unref( G_OBJECT( editlist_store ));
	editlist_model = gtk_tree_view_get_model( GTK_TREE_VIEW(editlist_tree ));	
	editlist_store = GTK_LIST_STORE(editlist_model);

	setup_tree_text_column( "editlisttree", COLUMN_INT, "Nr");
	setup_tree_text_column( "editlisttree", COLUMN_STRING0, "Timecode" );
	setup_tree_text_column( "editlisttree", COLUMN_STRINGA, "Filename");
	setup_tree_text_column( "editlisttree", COLUMN_STRINGB, "Duration");
	setup_tree_text_column( "editlisttree", COLUMN_STRINGC, "FOURCC");

	g_signal_connect( editlist_tree, "row-activated",
		(GCallback) on_editlist_row_activated, NULL );

  	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(editlist_tree));
    	gtk_tree_selection_set_select_function(selection, view_el_selection_func, NULL, NULL);
    	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
}

/*
	el format

	[4] : number of rows
	[16],[16],[16],[3],[filename] [16]

*/

static	void	reload_bundles()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_bundles");
	GtkListStore *store;
	GtkTreeIter iter;
	
	gint len = 0;
	single_vims( VIMS_BUNDLE_LIST );
	gchar *eltext = recv_vims(5,&len); // msg len
	gint 	offset = 0;

	reset_tree("tree_bundles");

	if(len == 0 || eltext == NULL )
	{
		return;
	}

	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	int k;
	for( k = 0; k < sizeof(vims_keys_t); k ++ )
		if( vims_keys_list[k].args ) g_free( vims_keys_list[k].args );
	memset( &vims_keys_list, 0, sizeof(vims_keys_t));


	while( offset < len )
	{
		char *message = NULL;
		char *line = strndup( eltext + offset,
				      14 );
		int val[4];
		int n = sscanf( line, "%04d%03d%03d%04d",
			&val[0],&val[1],&val[2],&val[3]);

		if(n < 4)
		{
printf("Unexpected exit %d\n",__LINE__);
			exit(0);
		}
		offset += 14;

		if(val[3] > 0)
		{
			message = strndup( eltext + offset , val[3] );
			offset += val[3];
		}

		if( val[0] < 400 || val[0] >= VIMS_BUNDLE_START )
		{ // query VIMS ! (ignore in userlist) 

		gchar *content = (message == NULL ? NULL : _utf8str( message ));
		gchar *keyname = sdlkey_by_id( val[1] );
		gchar *keymod = sdlmod_by_id( val[2] );
		gchar *descr = NULL;
		gchar *format = NULL;
		char vimsid[5];
		bzero(vimsid,5);
		sprintf(vimsid, "%03d", val[0]);


		if( val[0] >= VIMS_BUNDLE_START && val[0] < VIMS_BUNDLE_END )
		{
			if( vj_event_list[ val[0] ].event_id != val[0] && vj_event_list[val[0]].event_id != 0)
			{
				if( vj_event_list[ val[0] ].format )
					g_free( vj_event_list[ val[0] ].format );
				if( vj_event_list[ val[0] ].descr )
					g_free( vj_event_list[ val[0] ].descr  );
			}

			vj_event_list[ val[0] ].params   = 0;
			vj_event_list[ val[0] ].format   = NULL;
			vj_event_list[ val[0] ].descr    = _utf8str( "custom event (fixme: set bundle title)" );
			vj_event_list[ val[0] ].event_id = val[0];
		}/*
		else
		{
			if( vj_event_list[ val[0] ].event_id != 0 )
				descr = vj_event_list[ val[0] ].descr;
			if( vj_event_list[ val[0] ].event_id != 0 )
				format = vj_event_list[ val[0] ].format;

		}*/
	
		vims_keys_list[ (val[2] * MOD_OFFSET) + val[1] ].keyval = val[1];
		vims_keys_list[ (val[2] * MOD_OFFSET) + val[1] ].state = val[2];
		vims_keys_list[ (val[2] * MOD_OFFSET) + val[1] ].args = (content == NULL ? NULL :strdup(message));
		vims_keys_list[ (val[2] * MOD_OFFSET) + val[1] ].event_id = val[0];	

		gtk_list_store_append( store, &iter );

		gtk_list_store_set(store, &iter,
			VIMS_ID, vimsid,
			VIMS_DESCR, descr,
			VIMS_KEY, keyname,
			VIMS_MOD, keymod,
			VIMS_PARAMS, vj_event_list[ val[0] ].params,
			VIMS_FORMAT, format,
			VIMS_CONTENTS, content,
			-1 );
		if(content) g_free(content);
		if(message) free(message);
		}
		free( line );
	}
	/* entry, start frame, end frame */ 

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));

	g_free( eltext );

}

static	void	reload_vimslist()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_vims");
	GtkListStore *store;
	GtkTreeIter iter;
	
	gint len = 0;
	single_vims( VIMS_VIMS_LIST );
	gchar *eltext = recv_vims(5,&len); // msg len
	gint 	offset = 0;
	reset_tree("tree_vims");

	if(len == 0 || eltext == NULL )
	{
		return;
	}

	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	while( offset < len )
	{
		char *format = NULL;
		char *descr = NULL;
		char *line = strndup( eltext + offset,
				      14 );
		int val[4];
		sscanf( line, "%04d%02d%03d%03d",
			&val[0],&val[1],&val[2],&val[3]);

		char vimsid[5];

		offset += 12;
		if(val[2] > 0)
		{
			format = strndup( eltext + offset, val[2] );	
			offset += val[2];
		}

		if(val[3] > 0 )
		{
			descr = strndup( eltext + offset, val[3] );
			offset += val[3];
		}


		gchar *g_format = (format == NULL ? NULL :_utf8str( format ));
		gchar *g_descr  = (descr == NULL ? NULL :_utf8str( descr  ));


		gtk_list_store_append( store, &iter );

		vj_event_list[ val[0] ].event_id = val[0];
		vj_event_list[ val[0] ].params   = val[1];
		vj_event_list[ val[0] ].format   = (format == NULL ? NULL :_utf8str( format ));
		vj_event_list[ val[0] ].descr	 = (descr == NULL ? NULL : _utf8str( descr ));

		sprintf(vimsid, "%03d", val[0] );
		gtk_list_store_set( store, &iter,
				VIMS_ID, vimsid, 
				VIMS_DESCR, g_descr,-1 );
	

		if(g_format) g_free(g_format);
		if(g_descr) g_free(g_descr);

		if(format) free(format);
		if(descr) free(descr);
		
		free( line );
	}
	/* entry, start frame, end frame */ 

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_free( eltext );

}


static	void	reload_editlist_contents()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "editlisttree");
	GtkListStore *store;
	GtkTreeIter iter;

	gint i;
	gint len = 0;
	single_vims( VIMS_EDITLIST_LIST );
	gchar *eltext = recv_vims(6,&len); // msg len
	gint 	offset = 0;
	gint	num_files=0;
	reset_tree("editlisttree");
	_el_ref_reset();
	_el_entry_reset();

	if( eltext == NULL || len < 0 )
	{
		return;
	}
	char	str_nf[4];

	strncpy( str_nf, eltext , sizeof(str_nf));
	sscanf( str_nf, "%04d", &num_files );

	offset += 4;
	int n = 0;
	el_constr *el;

	for( i = 0; i < num_files ; i ++ )	
	{
		int itmp =0;
		char *tmp1 = (char*) strndup( eltext+offset, 4 );
		int line_len = 0;
		char fourcc[4];
		bzero(fourcc,4);
		n = sscanf( tmp1, "%04d", &line_len ); // line len
		if(line_len>0)
		{
			offset += 4;
			char *line = (char*)strndup( eltext + offset, line_len );
			offset += line_len;
			char *tmp = (char*) strndup( line, 3 );
			sscanf(tmp, "%03d",&itmp );
			char *file = strndup( line + 3, itmp );
			free(tmp);
			tmp = (char*) strndup( line + 3 + itmp, 16 );
			int a,b,c;
			int n = sscanf(tmp, "%04d%010d%02d", &a,&b,&c);
			free(tmp);
			if( n == 3 )
			{
				strncpy(fourcc, line + 3 + itmp + 16, c );
				el = _el_entry_new( i, file,   b, fourcc );
				info->editlist = g_list_append( info->editlist, el );
			}
			free(line);
			free(file);
		}
		free(tmp1);
	}
	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	int total_frames = 0; // running total of frames
	int row_num = 0;
	while( offset < len )
	{
		char *tmp = (char*)strndup( eltext + offset, (3*16) );
		offset += (3*16);
		long nl=0, n1=0,n2=0;

		sscanf( tmp, "%016ld%016ld%016ld",
			&nl,&n1,&n2 );

		if(nl < 0 || nl >= num_files)
		{
			printf("exceed max files\n");
			return;
		}
		int file_len = _el_get_nframes( nl );
	  	if(file_len <= 0)
		{
			row_num++;
			continue;
		}
		if(n1 < 0 )
			n1 = 0;
		if(n2 >= file_len)
			n2 = file_len;

		if(n2 <= n1 )
		{
			row_num++;
			continue;
		}

		info->elref = g_list_append( info->elref, _el_ref_new( row_num,(int) nl,n1,n2,total_frames ) ) ;
		char *tmpname = _el_get_filename(nl);
		gchar *fname = get_relative_path(tmpname);
		gchar *timecode = format_selection_time( n1,n2 );
		gchar *gfourcc = _utf8str( _el_get_fourcc(nl) );
		gchar *timeline = format_selection_time( 0, total_frames );

		// insert values to editlist
		//add_sample_to_editlist((guint)row_num,timeline,fname,timecode,gfourcc);

		gtk_list_store_append( store, &iter );
		gtk_list_store_set( store, &iter,
				COLUMN_INT, (guint) row_num,
				COLUMN_STRING0, timeline,
				COLUMN_STRINGA, fname,
				COLUMN_STRINGB, timecode,
				COLUMN_STRINGC, gfourcc,-1 );		
				
		g_free(timecode);
		g_free(gfourcc);
		g_free(fname);
		g_free(timeline);
		free(tmp);
		
		total_frames = total_frames + (n2-n1) + 1;
		row_num ++;
	}
	info->el.num_frames = total_frames;
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
		
	g_free( eltext );

}

// execute after el change:
static	void	load_editlist_info()
{
	char norm;
	float fps;
	int values[10];
	long rate = 0;
	long dum[2];
	memset(values, 0, sizeof(values));
	single_vims( VIMS_VIDEO_INFORMATION );
	int len = 0;
	gchar *res = recv_vims(3,&len);
	if( len <= 0 || res==NULL)
	{
		fprintf(stderr, "cannot read VIDEO INFORMATION\n");
		return;
	}
	sscanf( res, "%d %d %d %c %f %d %d %ld %d %ld %ld",
		&values[0], &values[1], &values[2], &norm,&fps,
		&values[4], &values[5], &rate, &values[7],
		&dum[0], &dum[1]);
		char tmp[15];
	snprintf( tmp, sizeof(tmp)-1, "%dx%d", values[0],values[1]);

	info->el.width = values[0];
	info->el.height = values[1];

	update_label_str( "label_el_wh", tmp );
	snprintf( tmp, sizeof(tmp)-1, "%s",
		(norm == 'p' ? "PAL" : "NTSC" ) );
	update_label_str( "label_el_norm", tmp);
	update_label_f( "label_el_fps", fps );

	update_spin_value( "screenshot_width", info->el.width );
	update_spin_value( "screenshot_height", info->el.height );	

	info->el.fps = fps;
	info->el.num_files = dum[0];
	snprintf( tmp, sizeof(tmp)-1, "%s",
		( values[2] == 0 ? "progressive" : (values[2] == 1 ? "top first" : "bottom first" ) ) );
	update_label_str( "label_el_inter", tmp );
	update_label_i( "label_el_arate", (int)rate, 0);
	update_label_i( "label_el_achans", values[7], 0);
	update_label_i( "label_el_abits", values[5], 0);
	
	if( rate == 0 )
	{
		disable_widget( "button_5_4");
		disable_widget_by_pointer(GTK_WIDGET(info->audiovolume_knob));	
		disable_widget( "audio_knobframe");
	}
	else
	{
		enable_widget( "button_5_4");
		enable_widget_by_pointer(GTK_WIDGET(info->audiovolume_knob));
		enable_widget( "audio_knobframe");
	}
	g_free(res);
}

void vj_gui_set_preview_window( int w , int h )
{
	if( w < 0 || w > 1568 || h < 0 || h > 1024 )
	{
		fprintf(stderr, "Please use a sane range for width X height\n");
		return;
	}
	info->preview_size_w = w;
	info->preview_size_h = h;
}


static	void	disable_widget(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
	if(!w) return ;
	gtk_widget_set_sensitive_( GTK_WIDGET(w), FALSE );
}

static	void	disable_widget_by_pointer(GtkWidget *w)
{
	if(!w) return;
        gtk_widget_set_sensitive_( GTK_WIDGET(w), FALSE );
}
static	void	enable_widget_by_pointer(GtkWidget *w)
{
	if(!w) return;
	gtk_widget_set_sensitive_( GTK_WIDGET(w), TRUE );
}
static	void	enable_widget(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
	veejay_msg(0, "%s :   %s", __FUNCTION__, name );
	if(!w) return;
	gtk_widget_set_sensitive_( GTK_WIDGET(w), TRUE );
}/*
static	gchar	*format_time(int pos)
{
	MPEG_timecode_t	tc;
	if(pos==0)
		memset(&tc, 0, sizeof(tc));
	else
		mpeg_timecode( &tc, pos,
			mpeg_framerate_code(
				mpeg_conform_framerate( info->el.fps )), info->el.fps );

	gchar *tmp = g_new( gchar, 20);
	snprintf(tmp, 20, "%2d:%2.2d:%2.2d:%2.2d",
		tc.h, tc.m, tc.s, tc.f );

	return tmp;
}*/
static	gchar	*format_selection_time(int start, int end)
{
	MPEG_timecode_t tc;
	memset( &tc, 0,sizeof(tc));
	if( (end-start) <= 0)
		memset( &tc, 0, sizeof(tc));
	else
		mpeg_timecode( &tc, (end-start), mpeg_framerate_code(	
			mpeg_conform_framerate( info->el.fps ) ), info->el.fps );

	gchar *tmp = g_new( gchar, 20);
	snprintf( tmp, 20, "%2d:%2.2d:%2.2d:%2.2d",	
		tc.h, tc.m, tc.s, tc.f );
	return tmp;
}

static	void		set_color_fg(const char *name, GdkColor *col)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window,name );
	if(w)
	{
 		GtkStyle *style;
		style = gtk_style_copy(gtk_widget_get_style(w));
		style->fg[GTK_STATE_NORMAL].pixel = col->pixel;
		style->fg[GTK_STATE_NORMAL].red = col->red;
		style->fg[GTK_STATE_NORMAL].green = col->green;
		style->fg[GTK_STATE_NORMAL].blue = col->blue;
		gtk_widget_set_style(w, style);
	}
}

static	gboolean	update_cpumeter_timeout( gpointer data )
{
	GtkWidget *w = glade_xml_get_widget_(
			info->main_window, "cpumeter");
	gdouble ms   = (gdouble)info->status_tokens[ELAPSED_TIME]; 
	gdouble max  = ((1.0/info->el.fps)*1000);
	gdouble frac =  1.0 / max;
	gdouble invert = 0.0;

	invert = 1.0 - (frac * (ms > max ? max : ms));
	if(invert < 0.0)
		invert = 0.0;
	

	if(ms > max)
	{
		frac = 1.0;
		// set progres bar red
		GdkColor color;
		color.red = 0xffff;
		color.green = 0x0000;
		color.blue = 0x0000;
//		if(gdk_colormap_alloc_color(info->color_map,
//			&color, TRUE, TRUE ))
		set_color_fg( "cpumeter", &color );
	}
	else
	{
		GdkColor color;
		color.red = 0x0000;
		color.green = 0xffff;
		color.blue = 0x0000;
//		if(gdk_colormap_alloc_color(info->color_map,
//			&color, TRUE, TRUE))
		set_color_fg( "cpumeter", &color);
		// progress bar green
	}

	gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(w), invert );

	return TRUE;
}
static	gboolean	update_cachemeter_timeout( gpointer data )
{
	GtkWidget *w = glade_xml_get_widget_(
			info->main_window, "cachemeter");
	gint	   v = info->status_tokens[TOTAL_MEM];
	gdouble ms   = v > 0 ? (double) v / 1000.0 : 0.0; 
	if(ms < 0)
		ms = 0.0;
	else if(ms > 1.0 )ms = 1.0;
	GdkColor color;

	color.red = 0x0000;
	color.green = 0xffff;
	color.blue = 0x0000;
	if(gdk_colormap_alloc_color(info->color_map,
			&color, TRUE, TRUE))
		set_color_fg( "cachemeter", &color);
	gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(w), ms );

	return TRUE;
}

static	gboolean	update_sample_record_timeout(gpointer data)
{
	if( info->uc.playmode == MODE_SAMPLE )
	{
		GtkWidget *w = glade_xml_get_widget_( info->main_window, 
			"samplerecord_progress" );
	
		gdouble	tf = info->status_tokens[STREAM_DURATION];
		gdouble cf = info->status_tokens[STREAM_RECORDED];

		gdouble fraction = cf / tf;
	
		if(!info->status_tokens[STREAM_RECORDING] )
		{
			gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(w), 0.0);
			info->samplerecording = 0;
			info->uc.recording[MODE_SAMPLE] = 0;	
			if(info->uc.render_record)
			{
				info->uc.reload_hint[HINT_HISTORY]=1;
				info->uc.render_record = 0;  // render list has private edl
			}
			else
			{
 				info->uc.reload_hint[HINT_EL] = 1; 
			}
			return FALSE;
		}
		else
		{
			gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(w),
				fraction );
		}
	}
	return TRUE;
}
static	gboolean	update_stream_record_timeout(gpointer data)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, 
			"streamrecord_progress" );
	if( info->uc.playmode == MODE_STREAM )
	{
		gdouble	tf = info->status_tokens[STREAM_DURATION];
		gdouble cf = info->status_tokens[STREAM_RECORDED];

		gdouble fraction = cf / tf;
		if(!info->status_tokens[STREAM_RECORDING] )
		{
			gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(w), 0.0);
			info->streamrecording = 0;
			info->uc.recording[MODE_STREAM] = 0;
			info->uc.reload_hint[HINT_EL] = 1; // recording finished, reload edl 
			return FALSE;
		}
		else
			gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(w),
				fraction );

	}
	return TRUE;
}
static gboolean	update_progress_timeout(gpointer data)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, "connecting");
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (w));
	return TRUE;
}

static	void	init_recorder(int total_frames, gint mode)
{
	if(mode == MODE_STREAM)
	{
		info->streamrecording = g_timeout_add(300, update_stream_record_timeout, (gpointer*) info );
	}
	if(mode == MODE_SAMPLE)
	{
		info->samplerecording = g_timeout_add(300, update_sample_record_timeout, (gpointer*) info );
	}
	info->uc.recording[mode] = 1;
}

static	void	init_cpumeter()
{
	info->cpumeter = g_timeout_add(300,update_cpumeter_timeout,
			(gpointer*) info );
	info->cachemeter = g_timeout_add(300,update_cachemeter_timeout,
			(gpointer*) info );
}


static void	update_gui()
{
	int pm = info->status_tokens[PLAY_MODE];
	int hm = info->prev_mode;

	if( pm == MODE_SAMPLE && info->uc.randplayer && hm != pm )
	{
		info->uc.randplayer = 0;
		set_toggle_button( "samplerand", 0 );
	}
	if( pm == MODE_PATTERN && hm != pm)
	{
		if(!info->uc.randplayer )
		{
			info->uc.randplayer = 1;
			enable_widget( "freestyle" );
			enable_widget( "samplerand" );
			set_toggle_button( "samplerand", 1 );
		}
		info->status_tokens[PLAY_MODE] = MODE_SAMPLE;
		pm = MODE_SAMPLE;
	}
#ifdef STRICT_CHECKING
	assert( pm >= 0 && pm <= 2 );
#endif
//	select_slot(pm);

	update_globalinfo();

	process_reload_hints();

	if( pm != MODE_PLAIN)
		interpolate_parameters();

	//vj_preview_draw();


	on_vims_messenger();
}

void	get_gd(char *buf, char *suf, const char *filename)
{

	if(filename !=NULL && suf != NULL)
		sprintf(buf, "%s/%s/%s", GVEEJAY_DATADIR,suf, filename );
	if(filename !=NULL && suf==NULL)
		sprintf(buf, "%s/%s", GVEEJAY_DATADIR, filename);
	if(filename == NULL && suf != NULL)
		sprintf(buf, "%s/%s/" , GVEEJAY_DATADIR, suf);
}
/*
static	void		veejay_untick(gpointer data)
{
	printf("Ending veejay session untick: %d, %d\n", info->watch.state, info->watch.p_state);
	if(info->watch.state == STATE_PLAYING)
	{
		printf("FATAL\n");
		exit(0);
	}
}*/

static	gboolean	veejay_tick( GIOChannel *source, GIOCondition condition, gpointer data)
{
	vj_gui_t *gui = (vj_gui_t*) data;
	if( (condition&G_IO_ERR) ) {
		return FALSE; }
	if( (condition&G_IO_HUP) ) {
		return FALSE; }
	if( (condition&G_IO_NVAL) ) {
		return FALSE; 
	}
	if(gui->watch.state==STATE_PLAYING && (condition & G_IO_IN)&& gui->watch.p_state == 0 )
	{
		gui->status_lock = 1;
		int nb = 0;
		unsigned char sta_len[6];
		bzero(sta_len,6);
		nb = vj_client_read( gui->client, V_STATUS, sta_len, 5 );
		
		if(sta_len[0] != 'V' || nb <= 0 )
		{
			gui->status_lock = 0;
			return FALSE;
		}
		int n_bytes = 0;
		sscanf( sta_len+1, "%03d", &n_bytes );
		if( n_bytes == 0 || n_bytes >= STATUS_BYTES )
		{
			gui->status_lock = 0;
			return FALSE;
		}
		bzero( gui->status_msg, STATUS_BYTES ); 

		nb = vj_client_read( gui->client, V_STATUS, gui->status_msg, n_bytes );
		if(nb <= 0 )
		{
			gui->status_lock = 0;
			return FALSE;
 		}
                while(vj_client_poll( gui->client, V_STATUS ))
                {
			// is there a more recent message ?
			unsigned char tmp[6];
			bzero(tmp,6);
			bzero( gui->status_msg, STATUS_BYTES ); 
		        nb = vj_client_read( gui->client,V_STATUS,tmp, 5);
			int bb = 0;
			sscanf( tmp+1, "%03d", &bb );
			if(bb>0)
				nb = vj_client_read(gui->client, V_STATUS, gui->status_msg, bb );
		}

		if(nb > 0)
		{
			int n = status_to_arr( gui->status_msg, gui->status_tokens );
#ifdef STRICT_CHECKING
			assert(n == 18);
#endif
			if( n != 18 )
			{
				// restore status (to prevent gui from going bezerk)
				int *history = info->history_tokens[ info->uc.playmode ];
				int i;
				for(i = 0; i <= 18; i ++ )
					gui->status_tokens[i] = history[i];
			}
			gdk_threads_enter();
			update_gui();
			gdk_threads_leave();
		}
		gui->status_lock = 0;

		int pm = info->status_tokens[PLAY_MODE];
		int *history = info->history_tokens[pm];
		int i;

		for( i = 0; i < STATUS_TOKENS; i ++ )
			history[i] = info->status_tokens[i];

		info->prev_mode = pm;
		char tmp[5];
                while(vj_client_poll( gui->client, V_STATUS ))
                       nb = vj_client_read( gui->client,V_STATUS,tmp, 1); 
	}
	return TRUE;
}

void	vj_fork_or_connect_veejay(char *configfile)
{
	char	*files  = get_text( "entry_filename" );
	int	port	= get_nums( "button_portnum" );
	gchar	**args;
	int	n_args = 0;
	char	port_str[15];
	char	config[512];
	char	tmp[20];
	int 	i = 0;

	int arglen = vims_verbosity ? 15 :14 ;
	arglen += (info->config.deinter);
	arglen += (info->config.osc);
	arglen += (info->config.vims);
	args = g_new ( gchar *, arglen );

	args[0] = g_strdup("veejay");

	sprintf(port_str, "-p%d", port);
	args[1] = g_strdup( port_str );

	if(configfile)
		sprintf(config,   "-l%s", configfile);

	if( config_file_status == 0 )
	{
		if(files == NULL || strlen(files)<= 0)
			args[2] = g_strdup("-d");
		else
			args[2] = g_strdup(files);	
	}
	else
	{
		printf("FIXME: Cant guess port num / hostname yet !\n");
		args[2] = g_strdup( config );
	}

	args[3] = g_strdup( "-O5" );
	sprintf(tmp, "-W%d", info->config.w );
	args[4] = g_strdup( tmp );
	sprintf(tmp, "-H%d", info->config.h );
	args[5] = g_strdup( tmp );
	sprintf(tmp, "-R%g", info->config.fps );
	args[6] = g_strdup( tmp );
	sprintf(tmp, "-N%d", info->config.norm );
	args[7] = g_strdup( tmp );
	sprintf(tmp, "-Y%d", info->config.pixel_format );
	args[8] = g_strdup( tmp );
	sprintf(tmp, "-m%d", info->config.sampling );
	args[9] = g_strdup( tmp );
	sprintf(tmp, "-c%d", info->config.sync );
	args[10] = g_strdup( tmp );
	sprintf(tmp, "-t%d", info->config.timer  == 0 ? 0 : 2);  
	args[11] = g_strdup( tmp );
	sprintf(tmp, "-r%d", info->config.audio_rate );
	args[12] = g_strdup( tmp );
	args[13] = NULL;
	int k=13;
	while( k <= (arglen-1))
		args[k++] = NULL;

	if( vims_verbosity )
		args[13] = g_strdup( "-v" );	

	if( info->config.deinter )
	{
		if(args[13]==NULL)
			args[13] = g_strdup( "-I"); 
		else args[14] = g_strdup( "-I" );
	}
	if( info->config.osc)
	{
		gchar osc_token[20];
		sprintf(osc_token , "-M %s", info->config.mcast_osc );
		int f = 13;
		while(args[f] != NULL ) f ++;
		args[f] = g_strdup( osc_token ); 
	}
	if( info->config.vims)
	{
		gchar vims_token[20];
		sprintf(vims_token, "-V %s", info->config.mcast_vims );
		int f = 13;
		while(args[f] != NULL) f++;
		args[f] = g_strdup( vims_token );
	}
	if( info->watch.state == STATE_STOPPED)
	{
		// start local veejay
	//	if(strncasecmp(remote, "localhost", strlen(remote)) == 0 || strncasecmp(remote, "127.0.0.1", strlen(remote))==0)
	//	{
			// lock state
			info->watch.state = STATE_CONNECT;
			info->run_state = RUN_STATE_REMOTE;

			/* now, how to see to stop connecting ? for now we do not 
			// another veejay may be locally running at host:port
			if(!vj_gui_reconnect( remote, NULL, port ))
			{
				GError *error = NULL;
				int pid;
				info->connecting = g_timeout_add( 300 , update_progress_timeout, (gpointer*) info );

				gettimeofday( &(info->timer) , NULL );
				memcpy( &(info->alarm), &(info->timer), sizeof(struct timeval));


				gboolean ret = g_spawn_async_with_pipes( 
							NULL,
							args,
							NULL,
							G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
							NULL,
							NULL,
							&pid,
							NULL,
							NULL,
							NULL, //&(iot.stderr_pipe),
							&error );
				g_strfreev( args );

				if(error)
				{
					vj_msg_detail(VEEJAY_MSG_ERROR, "There was an error: [%s]\n", error->message );
					ret = FALSE;
					g_error_free(error);
				}

				if( ret == FALSE )
				{
					info->state = STATE_IDLE;
					vj_msg(VEEJAY_MSG_ERROR,
					 "Failed to start veejay");
				}
				else
				{
					info->run_state = RUN_STATE_LOCAL;
					info->state = STATE_RECONNECT;
					vj_launch_toggle(FALSE);
				}
				
			}
			else
			{
				info->run_state = RUN_STATE_REMOTE;
			}*/
//		}
//		else
//		{	
	//		printf("Implement me: something else then localhost\n");
		/*	if(!vj_gui_reconnect(remote,NULL,port ))
			{
				vj_msg(VEEJAY_MSG_ERROR, "Cannot establish connection with %s : %d",
					remote, port );
			}*/
	//	}
	}
/*	else
	{
		if( info->state == STATE_PLAYING )
		{
			vj_gui_disconnect( );                
			vj_msg(VEEJAY_MSG_ERROR, "Disconnected.");
			info->state = STATE_RECONNECT;
		}
	}*/

	for( i = 0; i < n_args; i ++)
	{
		g_free(args[i]);
	}
}

void	vj_gui_free()
{
	if(info)
	{
		int i;
		//vj_gui_disconnect();
		for( i = 0; i < 3 ;  i ++ )
		{
			if(info->history_tokens[i])
				free(info->history_tokens[i]);
		}
		free(info);
	}
	info = NULL;

	vevo_port_free( fx_clipboard_ );
	vevo_port_free( fx_list_ );
}

static	void	vj_init_style( const char *name, const char *font )
{
	GtkWidget *widget = glade_xml_get_widget_(info->main_window, name );
	gtk_text_view_set_wrap_mode( GTK_TEXT_VIEW(widget), GTK_WRAP_WORD_CHAR );
	GdkColor red;
	gdk_color_parse( "red", &red);
//	GtkStyle *style = gtk_style_copy( gtk_widget_get_style(GTK_WIDGET(window)));
//	PangoFontDescription *desc = pango_font_description_from_string( font );
//	pango_font_description_set_style( desc, PANGO_STYLE_NORMAL );
//	style->font_desc = desc;
//	gtk_widget_set_style( widget, style );
//	gtk_style_ref(style);
	gtk_widget_modify_bg( widget, GTK_STATE_NORMAL, &red );
}	

void	vj_gui_style_setup()
{
	if(!info) return;
	info->color_map = gdk_colormap_get_system();
	vj_init_style( "veejaytext", "Monospace, 8" );
}

void	vj_gui_theme_setup(int default_theme)
{
	char path[MAX_PATH_LEN];
	bzero(path,MAX_PATH_LEN);
	if(!default_theme)
		get_gd(path,NULL, "gveejay-default.rc");
	else
		get_gd(path,NULL, "gveejay.rc");
	gtk_rc_parse(path);
}

gint
gui_client_event_signal(GtkWidget *widget, GdkEventClient *event,
	void *data)
{
	static GdkAtom atom_rcfiles = GDK_NONE;
	if(!atom_rcfiles)
		atom_rcfiles = gdk_atom_intern("_GTK_READ_RCFILES", FALSE);

	if(event->message_type == atom_rcfiles)
	{
		char path[MAX_PATH_LEN];
		get_gd(path,NULL, "gveejay.rc");

		gtk_rc_parse(path);

		return TRUE;
	}
	return FALSE;
}

void	vj_gui_set_debug_level(int level, int preview_p, int pw, int ph )
{
	veejay_set_debug_level( level );

	vims_verbosity = level;
	if(level)
		veejay_msg(VEEJAY_MSG_INFO, "Be verbosive");

	num_tracks_ = preview_p;
	default_preview_width_ = pw;
	default_preview_height_ = ph;
	veejay_msg(VEEJAY_MSG_INFO, "Preview size %d x %d",pw ,ph);
}
int	vj_gui_get_preview_priority(void)
{
	return 1;
}

void	default_bank_values(int *col, int *row )
{
	NUM_BANKS =	20;
	NUM_PAGES =	10;

	if( *col == 0 && *row == 0 )
	{
		if(skin__ == 0)
		{
			NUM_SAMPLES_PER_COL = 3;
			NUM_SAMPLES_PER_ROW = 4;
		}
		else
		{
			NUM_SAMPLES_PER_COL = 2;
			NUM_SAMPLES_PER_ROW = 8;
		}
	}
	else
	{
		NUM_SAMPLES_PER_ROW = *col;
		NUM_SAMPLES_PER_COL = *row;
	}
	NUM_SAMPLES_PER_PAGE = NUM_SAMPLES_PER_COL * NUM_SAMPLES_PER_ROW;
}

void	set_skin(int skin)
{
	skin__ = skin;
	timeline_theme_colors( skin ? 1: 0 );
}

int	vj_gui_cb_locked()
{
	return info->watch.p_state;
}

int	vj_img_cb(GdkPixbuf *img, GdkPixbuf *ir, GtkImage *image )
{
#ifdef STRICT_CHECKING
	assert( img != NULL );
	assert( ir != NULL );
	assert( image != NULL );
	assert( GTK_IS_IMAGE(image) );
#endif
	lock_preview();

	int ms = info->status_tokens[ ELAPSED_TIME ];
	ms *= 1000;

	if( no_preview_ ||  !info->selected_slot || !info->selected_gui_slot )
	{
		unlock_preview();
		return ms;
	}

	int sample_id = info->status_tokens[ CURRENT_ID ];
	int sample_type = info->status_tokens[ PLAY_MODE ]; 
	if( sample_type == MODE_SAMPLE || sample_type == MODE_STREAM )
	{
		int poke_slot = -1;	
		int bank_num = -1;
		bank_num = find_bank_by_sample( sample_id, sample_type, &poke_slot );
		if( bank_num >= 0 && sample_id == info->selected_slot->sample_id && sample_type ==
			info->selected_slot->sample_type )
		{
			sample_slot_t *slot = info->sample_banks[bank_num]->slot[poke_slot];
			sample_gui_slot_t *gui_slot = info->sample_banks[bank_num]->gui_slot[poke_slot];
			int i;
			if(slot->pixbuf)
			{
				gdk_pixbuf_unref( slot->pixbuf );
				slot->pixbuf = NULL;
			}
			for( i = 0; i < info->sequence_view->envelope_size; i ++ )
			{
				sequence_gui_slot_t *g = info->sequence_view->gui_slot[i];
                	        if(g->pixbuf_ref && g->sample_id == sample_id && g->sample_type == sample_type)
                        	{
                               		gdk_pixbuf_unref(g->pixbuf_ref);
                             		g->pixbuf_ref = NULL;
                        	}
			}
			slot->pixbuf = gdk_pixbuf_scale_simple( img, info->image_dimensions[0],
				info->image_dimensions[1], GDK_INTERP_NEAREST);

			//gtk_image_set_from_pixbuf_( gui_slot->image , slot->pixbuf );
			gtk_widget_queue_draw( gui_slot->image );
		}
	}

	//@redraw
	update_cached_slots();
	
	gtk_widget_queue_draw( image );
	
	unlock_preview();
	return ms;
}

int	gveejay_busy(void)
{
	if( info->watch.p_state != 0 )
		return 1;
	return 0;
}

void	vj_gui_cb(int state, char *hostname, int port_num)
{
//	info->watch.hostname = strdup(hostname);
//	info->watch.port_num = port_num;
	info->watch.state = STATE_DISCONNECT;
	info->watch.p_state = STATE_RECONNECT;
	info->run_state = RUN_STATE_REMOTE;
	put_text( "entry_hostname", hostname );
	update_spin_value( "button_portnum", port_num );
}

void	interrupt_cb()
{
   info->watch.state = STATE_STOPPED; 
}

//@ reloaded options !
//@ cleanup, total mess!
void	vj_gui_setup_defaults( vj_gui_t *gui )
{
	gui->config.w = 352;
	gui->config.h = 288;
	gui->config.fps = 25.0;
	gui->config.sampling = 1;
	gui->config.pixel_format = 1;
	gui->config.sync = 1;
	gui->config.timer = 1;
	gui->config.deinter = 1;
	gui->config.norm = 0;
	gui->config.audio_rate = 0;
	gui->config.osc = 0;
	gui->config.vims = 0;
	gui->config.mcast_osc = g_strdup( "224.0.0.32" );
	gui->config.mcast_vims = g_strdup( "224.0.0.33" );
}

int	vj_gui_yield()
{
 	if( is_button_toggled("previewtoggle"))
		return 1;
	return 0;
}

void 	vj_gui_init(char *glade_file, int launcher, char *hostname, int port_num)
{
	char path[MAX_PATH_LEN];
	int i;

	vj_gui_t *gui = (vj_gui_t*)vj_malloc(sizeof(vj_gui_t));
	if(!gui)
	{
		return;
	}
	memset( gui, 0, sizeof(vj_gui_t));
	memset( gui->status_tokens, 0, STATUS_TOKENS );
	memset( gui->sample, 0, 2 );
	memset( gui->selection, 0, 3 );
	memset( &(gui->uc), 0, sizeof(veejay_user_ctrl_t));
	gui->prev_mode = -1; 
	memset( &(gui->el), 0, sizeof(veejay_el_t));
	gui->sample_banks = (sample_bank_t**) vj_malloc(sizeof(sample_bank_t*) * NUM_BANKS );
	memset( gui->sample_banks, 0 , sizeof(sample_bank_t*) * NUM_BANKS );
			
	for( i = 0 ; i < 3 ; i ++ )
	{
		gui->history_tokens[i] = (int*) vj_malloc(sizeof(int) * STATUS_TOKENS);
		memset( gui->history_tokens[i], 0xffff, sizeof(int) *STATUS_TOKENS);
	}

	gui->uc.reload_force_avoid = FALSE;

	memset( vj_event_list, 0, sizeof(vj_event_list));

	get_gd( path, NULL, glade_file);

	gui->client = NULL;
	gui->main_window = glade_xml_new(path,NULL,NULL);
	if(!gui->main_window)
	{
		free(gui);
	}
	info = gui;

	glade_xml_signal_autoconnect( gui->main_window );
	GtkWidget *frame = glade_xml_get_widget_( info->main_window, "markerframe" );
	info->tl = timeline_new();
	gtk_widget_set_size_request(frame, 200,14 );
	g_signal_connect( info->tl, "pos_changed",
		(GCallback) on_timeline_value_changed, NULL );
	g_signal_connect( info->tl, "in_point_changed",
		(GCallback) on_timeline_in_point_changed, NULL );
	g_signal_connect( info->tl, "out_point_changed",
		(GCallback) on_timeline_out_point_changed, NULL );
	g_signal_connect( info->tl, "bind_toggled",
		(GCallback) on_timeline_bind_toggled, NULL );	
	g_signal_connect( info->tl, "cleared",
		(GCallback) on_timeline_cleared, NULL );

	gtk_widget_show(frame);
	gtk_container_add( GTK_CONTAINER(frame), info->tl );
	gtk_widget_show(info->tl);


	GtkWidget *mainw = glade_xml_get_widget_(info->main_window,"gveejay_window" );
    /* Make this run after any internal handling of the client event happened
     * to make sure that all changes implicated by it are already in place and
     * we thus can make our own adjustments.
     */
	if( !skin__ )
		gtk_label_set_text( GTK_LABEL(glade_xml_get_widget_(info->main_window, "build_revision")), VEEJAY_CODENAME );

	g_signal_connect_after( GTK_OBJECT(mainw), "client_event",
		GTK_SIGNAL_FUNC( G_CALLBACK(gui_client_event_signal) ), NULL );

	g_signal_connect( GTK_OBJECT(mainw), "destroy",
			G_CALLBACK( gveejay_quit ),
			NULL );
	g_signal_connect( GTK_OBJECT(mainw), "delete-event",
			G_CALLBACK( gveejay_quit ),
			NULL );
;
    	GtkWidget *box = glade_xml_get_widget_( info->main_window, "sample_bank_hbox" );
	info->sample_bank_pad = gtk_notebook_new();
	gtk_notebook_set_tab_pos( GTK_NOTEBOOK(info->sample_bank_pad), GTK_POS_BOTTOM );
	gtk_notebook_set_show_tabs( GTK_NOTEBOOK( info->sample_bank_pad ), FALSE );
	gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET(info->sample_bank_pad), TRUE, TRUE, 0);
	setup_samplebank( NUM_SAMPLES_PER_COL, NUM_SAMPLES_PER_ROW );

//@ FIXME: to setup_defaults
	create_ref_slots(skin__ == 0 ? MEM_SLOT_SIZE/4: MEM_SLOT_SIZE);
	
	gtk_widget_show( info->sample_bank_pad );

	fx_clipboard_ = vpn( 300 );

	info->elref = NULL;
	info->effect_info = NULL;
	info->devlist = NULL;
	info->chalist = NULL;
	info->editlist = NULL;

	vj_gui_setup_defaults(gui);
	setup_vimslist();
	setup_effectchain_info();
	setup_effectlist_info();
	setup_editlist_info();
	setup_samplelist_info();
	setup_v4l_devices();
	setup_colorselection();
	setup_rgbkey();
	setup_bundles();


	set_toggle_button( "button_252", vims_verbosity );

//@ after connect:

	setup_geometry( info->el.width, info->el.height, num_tracks_, default_preview_width_,
		     default_preview_height_ );
	GtkWidget *img_wid = (skin__ == 0 ? glade_xml_get_widget_( info->main_window, "imageA") : NULL );


	multitrack_configure_preview( default_preview_width_,
				      default_preview_height_,
			      	      360,
				      288 );	      

	gui->mt = multitrack_new(
			(void(*)(int,char*,int)) vj_gui_cb,
			(void(*)(GdkPixbuf *)) vj_img_cb, 
			glade_xml_get_widget_( info->main_window, "gveejay_window" ),
			glade_xml_get_widget_( info->main_window, "mt_box" ),
			glade_xml_get_widget_( info->main_window, "statusbar") ,
			360,
			288,
			img_wid);

	veejay_msg(VEEJAY_MSG_INFO, "Configure preview size %d x %d, maximum %d x %d",
			default_preview_width_, default_preview_height_,
		 	360,288 );
	
	memset( &info->watch, 0, sizeof(watchdog_t));
	info->watch.state = STATE_STOPPED; //
	memset(&(info->watch.p_time),0,sizeof(struct timeval));
	GtkWidget *w = glade_xml_get_widget_(info->main_window, "veejay_connection" );
	if( launcher )
	{
		gtk_widget_hide( w );
		put_text( "entry_hostname", hostname );
		update_spin_value( "button_portnum", port_num ); 
		gui->watch.state = STATE_CONNECT; 
	}
	else
	{
		gtk_widget_show( w );
	}
}



static	gboolean	update_log(gpointer data)
{
	if(info->watch.state != STATE_PLAYING && info->watch.p_state == 0)
		return TRUE;
	gint len = 0;
	single_vims( VIMS_LOG );
	gchar *buf = recv_log_vims(6, &len );
	if(len <= 0)
		return TRUE;
	
	GtkWidget *view = glade_xml_get_widget_( info->main_window, "veejaytext");
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter iter,enditer;

	if(line_count > 100)
	{
		clear_textview_buffer( "veejaytext" );
		line_count = 1;
	}
	gtk_text_buffer_get_end_iter(buffer, &iter);
	
	int nr,nw;
	gchar *text = g_locale_to_utf8( buf, -1, &nr, &nw, NULL);
	gtk_text_buffer_insert( buffer, &iter, text, nw );

	line_count ++;	
	gtk_text_buffer_get_end_iter(buffer, &enditer);
	gtk_text_view_scroll_to_iter(
		GTK_TEXT_VIEW(view),
		&enditer,
		0.0,
		FALSE,
		0.0,
		0.0 );
	
	g_free( text );
	g_free(buf);

	g_usleep( 20000 );
	
	return TRUE;
}

gboolean	enter_videobook(GtkWidget *w, gpointer user_data)
{
	fprintf(stderr, "enter\n");
	return FALSE;
}
gboolean	leave_videobook(GtkWidget *w, gpointer user_data)
{
	fprintf(stderr, "leave\n");
	return FALSE;
}

void	vj_gui_preview(void)
{

	gint w = info->el.width;
	gint h = info->el.height;

	update_spin_value( "priout_width", w );
	update_spin_value( "priout_height", h );

	if( w > 352 )
		w = 352;
	if( h > 288 )
		h = 288;

	update_spin_range( "preview_width", 16, w,
		(info->run_state == RUN_STATE_REMOTE ? (default_preview_width_==0 ? w/4: default_preview_width_) : w ) ); 
	update_spin_range( "preview_height", 16, h,
		(info->run_state == RUN_STATE_REMOTE ? (default_preview_height_ == 0 ? h/4 : default_preview_height_) : h ) );	

	update_spin_incr( "preview_width", 16, 0 );
	update_spin_incr( "preview_height", 16, 0 );
	update_spin_incr( "priout_width", 16,0 );
	update_spin_incr( "priout_height", 16, 0 );

	info->image_w = w;
	info->image_h = h;

	GdkRectangle result;
	widget_get_rect_in_screen(
		glade_xml_get_widget_(info->main_window, "quickselect"),
		&result
	);
	gdouble ratio = (gdouble) h / (gdouble) w;
	gint image_width = result.width / (skin__ == 0 ? MEM_SLOT_SIZE/4: MEM_SLOT_SIZE);
	gint image_height = image_width * ratio;

	info->sequence_view->w = image_width;
	info->sequence_view->h = image_height;
	gtk_widget_set_size_request(info->quick_select,
		image_width * (skin__ == 0 ? MEM_SLOT_SIZE/4:MEM_SLOT_SIZE),
		image_height );
}

int	vj_gui_reconnect(char *hostname,char *group_name, int port_num)
{
	if(info->client)
			vj_client_free(info->client);
		
	info->client = vj_client_alloc(0,0,0);
	if(!vj_client_connect( info->client, hostname, group_name, port_num ) )
	{
		if(info->client)
			vj_client_free(info->client);
		info->client = NULL;
		printf("UNVERIFIED ERROR: Unable to connect %s:%d\n",hostname,port_num);
		exit(0);
		return 0;
	}
	vj_msg(VEEJAY_MSG_INFO, "New connection with Veejay running on %s port %d",
		(group_name == NULL ? hostname : group_name), port_num );
	int k = 0;
	for( k = 0; k < 3; k ++ )
		memset( info->history_tokens[k] , 0, (sizeof(int) * STATUS_TOKENS) );

	info->channel = g_io_channel_unix_new( vj_client_get_status_fd( info->client, V_STATUS));
	g_io_add_watch_full(
			info->channel,
		//	G_PRIORITY_HIGH_IDLE,
			G_PRIORITY_LOW,
			G_IO_IN| G_IO_ERR | G_IO_NVAL | G_IO_HUP,
			veejay_tick,
			(gpointer*) info,
			NULL 
		);
	load_editlist_info();
	if(info->rawdata)
		free(info->rawdata);
	info->rawdata = (guchar*) vj_malloc(sizeof(guchar) * info->el.width * info->el.height * 3);
	memset(info->rawdata,0,sizeof(guchar) * info->el.width * info->el.height * 3 ); 
	memset( &vims_keys_list, 0, sizeof(vims_keys_t));

	load_effectlist_info();
	reload_vimslist();
	reload_editlist_contents();
	reload_bundles();

	GtkWidget *w = glade_xml_get_widget_(info->main_window, "gveejay_window" );
	gtk_widget_show( w );

	info->uc.reload_hint[HINT_SLIST] = 1;
	info->uc.reload_hint[HINT_CHAIN] = 1;
	info->uc.reload_hint[HINT_ENTRY] = 1;
 
	int speed = info->status_tokens[SAMPLE_SPEED];
	if( speed < 0 ) info->play_direction = -1; else info->play_direction=1;
	if( speed < 0 ) speed *= -1;

	update_spin_range( "spin_framedelay", 0, 9, 0);
	update_slider_range( "speed_slider", -25,25,speed,0);
	update_label_i( "speed_label", speed,0);

	update_label_str( "label_hostnamex",
		(hostname == NULL ? group_name: hostname ) );
	update_label_i( "label_portx",port_num,0);

	vj_gui_preview();

	return 1;
}

static	void	veejay_stop_connecting(vj_gui_t *gui)
{
	GtkWidget *veejay_conncection_window;

	if(!gui->sensitive)
		vj_gui_enable();

	vj_launch_toggle(TRUE);
	/*gtk_progress_bar_set_fraction(
		GTK_PROGRESS_BAR (glade_xml_get_widget_(info->main_window, "connecting")),0.0);
	g_source_remove( info->connecting );

	info->connecting = 0;*/
	
	veejay_conncection_window = glade_xml_get_widget(info->main_window, "veejay_connection");
	gtk_widget_hide(veejay_conncection_window);		
}

gboolean		is_alive( void )
{

	void *data = info;
	
	vj_gui_t *gui = (vj_gui_t*) data;
	if( gui->watch.state == STATE_PLAYING && gui->watch.p_state == 0)
		return TRUE;

	if(gui->watch.w_state == WATCHDOG_STATE_OFF )
	{
		multitrack_restart( gui->mt );
		vj_gui_disconnect();
		return TRUE;
	}

	if(gui->watch.state == STATE_DISCONNECT )
	{
		gui->watch.state = STATE_STOPPED;
		vj_gui_disconnect();
		return TRUE;
	}

	if( gui->watch.state == STATE_STOPPED )
	{ 
		if(gui->sensitive)
		 	vj_gui_disable(); 
		if(!gui->launch_sensitive)
			vj_launch_toggle(TRUE);
		if( gui->watch.p_state == STATE_RECONNECT )
			gui->watch.state = STATE_CONNECT;
	}

	if( gui->watch.state == STATE_CONNECT )
	{
		char	*remote;
		int	port;
		remote = get_text( "entry_hostname" );
		port	= get_nums( "button_portnum" );

		if(!vj_gui_reconnect( remote, NULL, port ))
		{
			info->watch.state = STATE_STOPPED;
		}
		else
		{
			info->watch.state = STATE_PLAYING;
			info->key_id = gtk_key_snooper_install( key_handler , NULL);
			init_cpumeter();
			info->logging = g_timeout_add( G_PRIORITY_LOW, update_log,(gpointer*) info );
			veejay_stop_connecting(gui);
			info->watch.state = STATE_PLAYING;
			if(info->watch.p_state == 0)
				multrack_audoadd( info->mt, remote, port );
			info->watch.p_state = 0; 
		}
	}

	return TRUE;
}


void	vj_gui_disconnect()
{

	g_source_remove( info->logging );
	g_source_remove( info->cpumeter );
	g_source_remove( info->cachemeter );
	g_io_channel_shutdown(info->channel, FALSE, NULL);
	g_io_channel_unref(info->channel);
	gtk_key_snooper_remove( info->key_id );
	free_samplebank();

	if(info->client)
	{
		vj_client_close(info->client);
		vj_client_free(info->client);
		info->client = NULL;
	}
	/* reset all trees */
	reset_tree("tree_effectlist");
	reset_tree("tree_effectmixlist");
	reset_tree("tree_chain");
	reset_tree("tree_sources");
	reset_tree("editlisttree");
	
	/* clear console text */
	clear_textview_buffer("veejaytext");
}

void	vj_launch_toggle(gboolean value)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window, "button_veejay" );
	if(!w) return;
	gtk_widget_set_sensitive_( GTK_WIDGET(w), value );
	info->launch_sensitive = ( value == TRUE ? 1 : 0);
}

void	vj_gui_disable()
{

	int i = 0;

	while( gwidgets[i].name != NULL )
	{
	 disable_widget( gwidgets[i].name );
	 i++;
	}

	disable_widget_by_pointer(info->audiovolume_knob);		

	info->sensitive = 0;
	enable_widget ("vs_box" );

	set_toggle_button( "previewtoggle", 0 );
}	



void	vj_gui_enable()
{
	int i =0;
	while( gwidgets[i].name != NULL)
	{
		enable_widget( gwidgets[i].name );
		 i++;
	}

	enable_widget_by_pointer(info->audiovolume_knob);		
//	enable_widget_by_pointer(info->speed_knob);		
	enable_widget( "speed_slider");
	// disable loadconfigfile
//	gtk_widget_set_sensitive_( GTK_WIDGET(
//			glade_xml_get_widget_(info->main_window, "button_loadconfigfile") ), FALSE );

	info->sensitive = 1;

	disable_widget( "vs_box" );

}
/*
void	vj_gui_put_image(void)
{
	GtkWidget *pixbuf_widget = glade_xml_get_widget_( info->main_window, "//imageA" );
	GdkPixbuf *pixbuf = NULL;
	gint w = 100;
	gint h = 100;
	gint row_strides = 3 * w;
	gint bw = 0;

	// veejay sends current frame as image in RGB, 8 bytes per sample 
	// send 'get image' message
	multi_vims( VIMS_RGB24_IMAGE, "%d %d", w ,h );
	// read image from socket, store length of image in bw
	gchar *rawdata = recv_vims( 5, &bw );

	if(bw<=0 ) { if (rawdata) free(rawdata); return ; }

	GdkPixbuf *old_image = gtk_image_get_pixbuf( pixbuf_widget );
	// create a new picture from memory
	pixbuf = gdk_pixbuf_new_from_data( rawdata,GDK_COLORSPACE_RGB, FALSE,8,w,h,row_strides,NULL,NULL );
	// set picture
	gtk_image_set_from_pixbuf_(pixbuf_widget,pixbuf);
	// free ?
	//@ gdk_pixbuf_unref(pixbuf);
	g_free(rawdata);
}
*/

/* --------------------------------------------------------------------------------------------------------------------------
 * Returns infos to a given sample/stream... use as follows
 * which_one decides what sample/stream do you mean: 0 = current playing, -1 = last created, > 0 = sample number
 * mode decides if you gather informations of a sample (== VJ_PLAYBACK_MODE_SAMPLE) or a stream (== VJ_PLAYBACK_MODE_TAG)
		to see, why this is made see comments in veejay/vj-event.c in void vj_event_send_sample_info(..) 
 * s_type, descr and timecode are pointers to return the loaded values of the selected sample/stream
 * This function loads the SAMPLE_INFO to a new temporary sample_slot you should free after use
  -------------------------------------------------------------------------------------------------------------------------- */
sample_slot_t *vj_gui_get_sample_info(gint which_one, gint mode )
{
	sample_slot_t *tmp_slot = (sample_slot_t*) vj_malloc(sizeof(sample_slot_t));
	memset( tmp_slot, 0, sizeof(sample_slot_t));

	multi_vims( VIMS_SAMPLE_INFO, "%d %d", which_one, mode ); 

	gint sample_info_len = 0;
	gchar *sample_info = recv_vims( 5, &sample_info_len);
	gchar *ptr = sample_info; // 
	gint descr_len = 0;
	gchar *p = sample_info;

	if(sample_info_len <= 0 )
	{
		if(sample_info) g_free(sample_info);
		if(tmp_slot) free(tmp_slot);
		return NULL;
	}

	sscanf(sample_info, "%d",&descr_len );
	sample_info += 3;
	tmp_slot->title = g_strndup( sample_info, descr_len );
	sample_info += descr_len;

	gint  timecode_len = 0;
	sscanf( sample_info,"%d", &timecode_len );
	sample_info += 3;
	tmp_slot->timecode = g_strndup( sample_info, timecode_len );
	sample_info += timecode_len;

	tmp_slot->sample_id = which_one;
	tmp_slot->sample_type = mode;

	if(p) g_free(p);	

	return tmp_slot;
}


static void
widget_get_rect_in_screen (GtkWidget *widget, GdkRectangle *r)
{
//GdkRectangle extents;
//GdkWindow *window;
//window = GDK_WINDOW(gtk_widget_get_parent_window(widget)); /* getting parent window */
//gdk_window_get_root_origin(window, &x,&y); /* parent's left-top screen coordinates */
//gdk_drawable_get_size(window, &w,&h); /* parent's width and height */
//gdk_window_get_frame_extents(window, &extents); /* parent's extents (including decorations) */
//r->x = x + (extents.width-w)/2 + widget->allocation.x; /* calculating x (assuming: left border size == right border size) */
//r->y = y + (extents.height-h)-(extents.width-w)/2 + widget->allocation.y; /* calculating y (assuming: left border size == right border size == bottom border size) */
r->x = 0;
r->y = 0;
r->width = widget->allocation.width;
r->height = widget->allocation.height;
}


/* --------------------------------------------------------------------------------------------------------------------------
 *  Function that creates the sample-bank initially, just add the widget to the GUI and create references for the 
 *  sample_banks-structure so that the widgets are easiely accessable
 *  The GUI componenets are in sample_bank[i]->gui_slot[j]
 *
  -------------------------------------------------------------------------------------------------------------------------- */ 

int	power_of_2(int x)
{
	int p = 1;
	while( p < x )
		p <<= 1;
	return p;
}

/* Add a page to the notebook and initialize slots */
static int	add_bank( gint bank_num  )
{
	gchar str_label[5];
	gchar frame_label[20];
	sprintf(str_label, "%d", bank_num );
	sprintf(frame_label, "Samples %d to %d", (bank_num * NUM_SAMPLES_PER_PAGE), (bank_num * NUM_SAMPLES_PER_PAGE) + NUM_SAMPLES_PER_PAGE  );
	/* Check image dimensions */

	setup_samplebank( NUM_SAMPLES_PER_COL, NUM_SAMPLES_PER_ROW );

	/* Add requested bank num */
	if(info->sample_banks[bank_num] )
	{
		exit(0);
	}

	info->sample_banks[bank_num] = (sample_bank_t*) vj_malloc(sizeof(sample_bank_t));
	memset(info->sample_banks[bank_num],0,sizeof(sample_bank_t));
	info->sample_banks[bank_num]->bank_number = bank_num;
	sample_slot_t **slot = (sample_slot_t**) vj_malloc(sizeof(sample_slot_t*) * NUM_SAMPLES_PER_PAGE);
	sample_gui_slot_t **gui_slot = (sample_gui_slot_t**) vj_malloc(sizeof(sample_gui_slot_t*) * NUM_SAMPLES_PER_PAGE );

	int j;
	for(j = 0;j < NUM_SAMPLES_PER_PAGE; j ++ ) 
	{
		slot[j] = (sample_slot_t*) vj_malloc(sizeof(sample_slot_t) );	
		gui_slot[j] = (sample_gui_slot_t*) vj_malloc(sizeof(sample_gui_slot_t));
		memset(slot[j], 0 , sizeof(sample_slot_t) );	
		memset(gui_slot[j], 0, sizeof(sample_gui_slot_t));
		slot[j]->rawdata = (guchar*) vj_malloc(sizeof(guchar) * 3 * 256 * 256 );
		slot[j]->slot_number = j;
		slot[j]->sample_id = -1;
		slot[j]->sample_type = -1;
		slot[j]->ec = new_chain();
	}

	info->sample_banks[bank_num]->slot = slot;
	info->sample_banks[bank_num]->gui_slot = gui_slot;

	GtkWidget *sb = info->sample_bank_pad;
	GtkFrame *frame = GTK_FRAME(gtk_frame_new(frame_label));
	GtkWidget *label = gtk_label_new( str_label );
//	gtk_widget_set_size_request(frame, 200,200 );
	gtk_widget_show(frame);
	info->sample_banks[bank_num]->page_num = gtk_notebook_append_page(GTK_NOTEBOOK(info->sample_bank_pad), frame, label);

	GtkWidget *table = gtk_table_new( NUM_SAMPLES_PER_COL, NUM_SAMPLES_PER_ROW, TRUE );	
	gtk_container_add( GTK_CONTAINER(frame), table );
	gtk_widget_show(table);
	gtk_widget_show(sb );


	gint col, row;
	for( col = 0; col < NUM_SAMPLES_PER_COL; col ++ )
	{
		for( row = 0; row < NUM_SAMPLES_PER_ROW; row ++ )
		{
			int slot_nr = col * NUM_SAMPLES_PER_ROW + row;
			if(slot_nr < NUM_SAMPLES_PER_PAGE)
			{
				create_slot( bank_num, slot_nr ,info->image_dimensions[0], info->image_dimensions[1]);
				sample_gui_slot_t *gui_slot = info->sample_banks[bank_num]->gui_slot[slot_nr];
	    			gtk_table_attach_defaults ( table, gui_slot->event_box, row, row+1, col, col+1);   
			}
		}
	}


	if(!info->fg_)
	{
		info->fg_ = widget_get_fg( info->sample_banks[bank_num]->gui_slot[0]->frame );
	}
	return bank_num;
}


void	reset_samplebank(void)
{
	lock_preview();
	info->selection_slot = NULL;
	info->selection_gui_slot = NULL;
	info->selected_slot = NULL;
	info->selected_gui_slot = NULL;
	int i,j;
	for( i = 0; i < NUM_BANKS; i ++ )
	{
		if(info->sample_banks[i])
		{
			/* free memory in use */
			for(j = 0; j < NUM_SAMPLES_PER_PAGE ; j ++ )
			{
				sample_slot_t *slot = info->sample_banks[i]->slot[j];
				
				if(slot->title) free(slot->title);
				if(slot->timecode) free(slot->timecode);
				if(slot->pixbuf) gdk_pixbuf_unref( slot->pixbuf );
				if(slot->ec) del_chain( slot->ec );
				slot->ec = NULL;
				slot->title = NULL;
				slot->timecode = NULL;
				slot->sample_id = 0;
				slot->sample_type = 0;
				   update_sample_slot_data( i,j, slot->sample_id,slot->sample_type,slot->title,slot->timecode);


			}
		}
	}
	unlock_preview();
}
		
void	free_samplebank(void)
{
	int i,j;
	while( gtk_notebook_get_n_pages(GTK_NOTEBOOK(info->sample_bank_pad) ) > 0 )
		gtk_notebook_remove_page( GTK_NOTEBOOK(info->sample_bank_pad), -1 );


	info->selection_slot = NULL;
	info->selection_gui_slot = NULL;
	info->selected_slot = NULL;
	info->selected_gui_slot = NULL;
	
	for( i = 0; i < NUM_BANKS; i ++ )
	{

		if(info->sample_banks[i])
		{
			/* free memory in use */
			for(j = 0; j < NUM_SAMPLES_PER_PAGE ; j ++ )
			{
				sample_slot_t *slot = info->sample_banks[i]->slot[j];
				sample_gui_slot_t *gslot = info->sample_banks[i]->gui_slot[j];
				if(slot->title) free(slot->title);
				if(slot->timecode) free(slot->timecode);
				if(slot->pixbuf) gdk_pixbuf_unref(slot->pixbuf);
				if(slot->rawdata) free(slot->rawdata);
				if(slot->ec) del_chain( slot->ec );
				free(slot);
			//	if(gslot->title) g_object_unref( gslot->title );
			//	if(gslot->timecode) g_object_unref(gslot->title);
			//	if(gslot->image) g_object_unref( gslot->image );
			//	if(gslot->event_box) g_object_unref( gslot->event_box );		
			//	if(gslot->main_vbox) g_object_unref( gslot->main_vbox );
			//	if(gslot->upper_vbox) g_object_unref( gslot->upper_vbox );
			//	if(gslot->upper_hbox) g_object_unref( gslot->upper_hbox );
			//	if(gslot->frame ) g_object_unref( gslot->frame );
				if(gslot)
					free(gslot);
			}			
			free(info->sample_banks[i]);
		}
	}
	memset( info->sample_banks, 0, sizeof(sample_bank_t*) * NUM_BANKS );

/*	GtkWidget *imgA = glade_xml_get_widget_ (info->main_window, "imageA" );
	if(imgA)
	{
		char path[MAX_PATH_LEN];
		bzero(path,MAX_PATH_LEN);
		get_gd(path,NULL, "veejay-logo.png");
		GdkPixbuf *buf = gdk_pixbuf_new_from_file( path,NULL );
		gtk_image_set_from_pixbuf_( imgA, buf );
		gdk_pixbuf_unref( buf );
	}*/
}

void setup_samplebank(gint num_cols, gint num_rows)
{
	GdkRectangle result;
	if(info->el.width > 0 && info->el.height > 0 )
	{
		widget_get_rect_in_screen(
			info->sample_bank_pad,
			&result
		);
		result.width -= ( num_rows * 16);	
		result.height -= ( num_cols * 16);
		gint image_width = result.width / num_rows;
		gint image_height = result.height / num_cols;
		float ratio = (float) info->el.height / (float) info->el.width;
		image_height = image_width * ratio;
		info->image_dimensions[0] = image_width ;
		info->image_dimensions[1] = image_height;
	}
	else
	{
		info->image_dimensions[0] = 0 ;
		info->image_dimensions[1] = 0;
	}
}

/* --------------------------------------------------------------------------------------------------------------------------
 *  Function that resets the visualized sample-informations of the samplebanks, it does this by going through all
 *  slots that allready used and resets them (which means cleaning the shown infos as well as set them free for further use)
 *  with_selection should be TRUE when the actual selection of a sample-bank-slot should also be reseted
 *  (what is for instance necessary when vj reconnected)
   -------------------------------------------------------------------------------------------------------------------------- */ 

static	int	bank_exists( int bank_page, int slot_num )
{

	if(!info->sample_banks[bank_page])
		return 0;
	return 1;
}

static	int	find_bank_by_sample(int sample_id, int sample_type, int *slot )
{
	int i;

	/* See if ID is somewhere in the samplebank */
	for( i = 0; i < NUM_BANKS; i ++ )
	{
		int j;
		if(info->sample_banks[i])
		for( j = 0; j < NUM_SAMPLES_PER_PAGE; j ++ )
		{
			if(info->sample_banks[i]->slot[j]->sample_id == sample_id &&
			   info->sample_banks[i]->slot[j]->sample_type == sample_type )
			{
				*slot = j;
				return i;
			}
		}
	}

	/* Fall back to suggest a bank number to add the new id */
	for( i = 0; i < NUM_BANKS; i ++ )
	{
		if(!info->sample_banks[i]) /* First slot in bank */
		{
			*slot = 0;
			return i;
		}
		else
		{
			int j; /* Scan for available slots in this bank page */
			for( j = 0; j < NUM_SAMPLES_PER_PAGE; j ++ )
			{
				if( info->sample_banks[i]->slot[j]->sample_id <= 0 )
				{
					*slot = j;
					return i;
				}
			}
		}
	}
	/* Everything is full ?? TODO: Error checking/handling*/
	*slot = -1;
	return -1;
}

static	int	find_bank(int page_nr)
{
	int i = 0;	
	for ( i = 0 ; i < NUM_BANKS; i ++ )
		if( info->sample_banks[i] && info->sample_banks[i]->page_num == page_nr )
		{
			return info->sample_banks[i]->bank_number;
		}
	return -1;
}/*
static int 	find_free_bank_slot(void)
{
	int i = 0;
	for( i = 0; i < NUM_BANKS; i++ )
		if(info->sample_banks[i])
		{
			int j;
			for(j = 0; j < NUM_SAMPLES_PER_PAGE; j ++ )
				if(info->sample_banks[i]->slot[j]->sample_id == 0 )
					return i;

				
		}  
}
*/


static gboolean
image_expose_event (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	guchar *pixels;
	int rowstride;
	// ha ha redraw whole page ! 
 	GtkWidget *notepad = info->sample_bank_pad;
	gint bank_nr = find_bank( gtk_notebook_get_current_page( GTK_NOTEBOOK(notepad)) );

	if(no_draw_)
		return FALSE;
	if(bank_nr < 0 )
		return FALSE;

	lock_preview();
	
	if ( info->sample_banks[bank_nr] == NULL )
	{
		veejay_msg(0, "Internal error in %s", __FUNCTION__);
		unlock_preview();
		return FALSE;
	}

	int i = (gint*) data;
	sample_slot_t *slot = info->sample_banks[bank_nr]->slot[i];

	if(!slot->pixbuf || slot->sample_id  <= 0 )
	{
		unlock_preview();
		return FALSE;
	}
	
	if(slot->pixbuf != NULL && slot->sample_id > 0 
		 && is_button_toggled("previewtoggle"))
	{	
		rowstride = gdk_pixbuf_get_rowstride( slot->pixbuf );
#ifdef STRICT_CHECKING
		assert( slot->pixbuf != NULL );
#endif
		guchar *pixels = gdk_pixbuf_get_pixels( slot->pixbuf ) + rowstride * event->area.y + event->area.x * 3;
		if(pixels && rowstride > 0 && info->image_dimensions[0] > 0)
			gdk_draw_rgb_image_dithalign( widget->window,
					widget->style->black_gc,
				//		event->area.x, event->area.y,
						0,0,info->image_dimensions[0],info->image_dimensions[1],
					//	event->area.width, event->area.height,
					GDK_RGB_DITHER_NORMAL,
					pixels,
					rowstride,
					event->area.x , event->area.y );
	}

	unlock_preview();
	return FALSE;

}
// TODO: merge with image_expose event ? 
static gboolean
image_expose_seq_event (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	guchar *pixels;
	int rowstride;
	int j = (gint*) data;  // sequence slot number
	if(no_draw_)
		return FALSE;

	lock_preview();

	sequence_gui_slot_t *g = info->sequence_view->gui_slot[j];
	GdkRectangle result;

	widget_get_rect_in_screen( g->frame, &result );	
	info->sequence_view->w = result.width;
	info->sequence_view->h = result.height;

	if( info->sequence_view->gui_slot[j]->pixbuf_ref  
		&& is_button_toggled("previewtoggle"))
	{
		sequence_gui_slot_t *g = info->sequence_view->gui_slot[j];
		if(!g->sample_id || !g->pixbuf_ref)
		{
			unlock_preview();
			return FALSE;
		}
		rowstride = gdk_pixbuf_get_rowstride( g->pixbuf_ref );
		guchar *pixels = gdk_pixbuf_get_pixels( g->pixbuf_ref ) + rowstride * event->area.y + event->area.x * 3;

		if(pixels && info->sequence_view->w > 0 && info->sequence_view->h > 0)
			gdk_draw_rgb_image_dithalign( widget->window,
						widget->style->black_gc,
							0,0,info->sequence_view->w, info->sequence_view->h,
						GDK_RGB_DITHER_NORMAL,
						pixels,
						rowstride,
						event->area.x , event->area.y );

	}
	unlock_preview();
	return FALSE;
}
	
static void set_activation_of_cache_slot_in_samplebank( sequence_gui_slot_t *gui_slot, gboolean activate)
{
	if (activate)
		{
			gtk_frame_set_shadow_type(gui_slot->frame,GTK_SHADOW_IN);
		}
	else 
		gtk_frame_set_shadow_type(gui_slot->frame,GTK_SHADOW_ETCHED_IN);
}	
static	void	update_cached_slots(void)
{
	int i;

	if(info->sequence_view->w == 0 || info->sequence_view->h == 0 )
	{
		return;
	}
	for( i = 0; i < info->sequence_view->envelope_size; i ++ )
	{
		sequence_gui_slot_t *g = info->sequence_view->gui_slot[i];
		sample_slot_t *s = info->selected_slot;
		if(g->sample_id == info->selected_slot->sample_id && g->sample_type == info->selected_slot->sample_type && s->pixbuf)
		{
			if(!g->pixbuf_ref
				&& is_button_toggled("previewtoggle"))
			{
				g->pixbuf_ref = gdk_pixbuf_scale_simple(
				s->pixbuf,
				info->sequence_view->w,
				info->sequence_view->h,
				GDK_INTERP_NEAREST );
			}
		}
	}
	gtk_widget_queue_draw( info->quick_select );
}


static gboolean on_cacheslot_activated_by_mouse (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	gint bank_nr = -1;
	gint slot_nr = -1;
	if(info->status_tokens[PLAY_MODE] == MODE_PLAIN )
		return FALSE;

//	lock_preview();
	
	slot_nr = (gint *)user_data;
	set_activation_of_cache_slot_in_samplebank( info->sequence_view->gui_slot[slot_nr], FALSE );

	if( event->type == GDK_BUTTON_PRESS && (event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK )
	{
		info->current_sequence_slot = slot_nr;
		sample_slot_t *s = info->selected_slot;
		sequence_gui_slot_t *g = info->sequence_view->gui_slot[slot_nr];
		g->sample_id = s->sample_id;	
		g->sample_type = s->sample_type;
		vj_msg(VEEJAY_MSG_INFO, "Placed %s %d in Memory slot %d",
		 	(g->sample_type == 0 ? "Sample" : "Stream" ), g->sample_id, slot_nr );
	}
	else
	if(event->type == GDK_BUTTON_PRESS)
	{
		sequence_gui_slot_t *g = info->sequence_view->gui_slot[slot_nr];
		if(g->sample_id <= 0)
		{
			vj_msg(VEEJAY_MSG_ERROR, "Memory slot %d empty, put with SHIFT + mouse button1",slot_nr);
			unlock_preview();
			return FALSE;

		}
		multi_vims(VIMS_SET_MODE_AND_GO, "%d %d", g->sample_type, g->sample_id );
		no_preview_ = 1;
	}

//	unlock_preview();
	
	return FALSE;

}		


static void create_ref_slots(int envelope_size)
{
	gchar frame_label[50];
	GtkWidget *vbox = glade_xml_get_widget_ (info->main_window, "quickselect");
	info->quick_select = gtk_frame_new(NULL);
	gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET(info->quick_select), TRUE, TRUE, 0);
	gtk_widget_show(info->quick_select);

	/* Allocate */
	info->sequence_view = (sequence_envelope*) vj_malloc(sizeof(sequence_envelope) );
	memset( info->sequence_view, 0, sizeof( sequence_envelope ) );

	info->sequence_view->gui_slot = (sequence_gui_slot_t**) vj_malloc(sizeof(sequence_gui_slot_t*) * envelope_size );
	memset( info->sequence_view->gui_slot, 0, sizeof( sequence_gui_slot_t*) * envelope_size );
	/* Setup table for slots */


	sprintf(frame_label, "Last played" );

	GtkWidget *table = gtk_table_new( 1, envelope_size, TRUE );	
	gtk_container_add( GTK_CONTAINER(info->quick_select), table );
	gtk_widget_show(table);

	gint col=0;
	gint row=0;
	for( row = 0; row < envelope_size; row ++ )
	{
		
		sequence_gui_slot_t *gui_slot = (sequence_gui_slot_t*)vj_malloc(sizeof(sequence_gui_slot_t));

		memset(gui_slot, 0, sizeof(sequence_gui_slot_t));
		info->sequence_view->gui_slot[row] = gui_slot;
		gui_slot->event_box = gtk_event_box_new();
		gtk_event_box_set_visible_window(gui_slot->event_box, TRUE);
		GTK_WIDGET_SET_FLAGS(gui_slot->event_box,GTK_CAN_FOCUS);	
		/* Right mouse button is popup menu, click = play */
		g_signal_connect( G_OBJECT(gui_slot->event_box),
			"button_press_event",
			G_CALLBACK(on_cacheslot_activated_by_mouse),
			(gpointer)row
			);	    
		gtk_widget_show(GTK_WIDGET(gui_slot->event_box));	
		/* the surrounding frame for each slot */
		gui_slot->frame = GTK_FRAME(gtk_frame_new(NULL));
	//	gui_slot->frame = gtk_aspect_frame_new( NULL, 0.5, 0.5, 1.0, TRUE );
		gtk_container_set_border_width (GTK_CONTAINER(gui_slot->frame),1);
		gtk_widget_show(GTK_WIDGET(gui_slot->frame));
		gtk_container_add (GTK_CONTAINER (gui_slot->event_box), gui_slot->frame);

		/* the slot main container */
		gui_slot->main_vbox = gtk_vbox_new(FALSE,0);
		gtk_container_add (GTK_CONTAINER (gui_slot->frame), gui_slot->main_vbox);
		gtk_widget_show( GTK_WIDGET(gui_slot->main_vbox) );
		
		/* The sample's image */
		gui_slot->image = gtk_drawing_area_new();
		gtk_box_pack_start (GTK_BOX (gui_slot->main_vbox), GTK_WIDGET(gui_slot->image), TRUE, TRUE, 0);
		gtk_widget_set_size_request( gui_slot->image, info->sequence_view->w,info->sequence_view->h );
		g_signal_connect( gui_slot->image, "expose_event",
			G_CALLBACK(image_expose_seq_event), 
			(gpointer) row  );
		gtk_widget_show( GTK_WIDGET(gui_slot->image));

		gtk_table_attach_defaults ( table, gui_slot->event_box, row, row+1, col, col+1);   



	}
//	info->sequence_view->w = 0;
//	info->sequence_view->h = 0;
	info->sequence_view->envelope_size = envelope_size;
}

static void create_slot(gint bank_nr, gint slot_nr, gint w, gint h)
{
	gchar hotkey[3];

	sample_bank_t **sample_banks = info->sample_banks;	
	sample_gui_slot_t *gui_slot = sample_banks[bank_nr]->gui_slot[slot_nr];
	
	if(gui_slot->event_box != NULL )
	{
		fprintf(stderr, "Already created ! %p\n", gui_slot->event_box );
		exit(0);
	}

	if( slot_nr < 0 || slot_nr > NUM_SAMPLES_PER_PAGE)
		exit(0);

	// to reach clicks on the following GUI-Elements of one slot, they are packed into an event_box
	gui_slot->event_box = gtk_event_box_new();
	gtk_event_box_set_visible_window(gui_slot->event_box, TRUE);


	GTK_WIDGET_SET_FLAGS(gui_slot->event_box,GTK_CAN_FOCUS);	
	g_signal_connect( G_OBJECT(gui_slot->event_box),
		"button_press_event",
		G_CALLBACK(on_slot_activated_by_mouse),
		(gpointer)slot_nr
		);	    
	g_signal_connect( G_OBJECT(gui_slot->event_box),
		"key_press_event",
		G_CALLBACK(on_slot_activated_by_key),
		(gpointer)slot_nr
		);	    	
	gtk_widget_show(GTK_WIDGET(gui_slot->event_box));	
	/* the surrounding frame for each slot */
	gui_slot->frame = gtk_frame_new(NULL);
//	gui_slot->frame = gtk_aspect_frame_new( NULL, 0.5, 0.5, 1.0, TRUE );
		
	gtk_container_set_border_width (GTK_CONTAINER(gui_slot->frame),0);
	gtk_widget_show(GTK_WIDGET(gui_slot->frame));
	gtk_container_add (GTK_CONTAINER (gui_slot->event_box), gui_slot->frame);


	/* the slot main container */
	gui_slot->main_vbox = gtk_vbox_new(FALSE,0);
	gtk_container_add (GTK_CONTAINER (gui_slot->frame), gui_slot->main_vbox);
	gtk_widget_show( GTK_WIDGET(gui_slot->main_vbox) );


//	gui_slot->image = gtk_image_new();
	gui_slot->image = gtk_drawing_area_new();
	gtk_box_pack_start (GTK_BOX (gui_slot->main_vbox), GTK_WIDGET(gui_slot->image), TRUE, TRUE, 0);
//	gtk_widget_show(GTK_WIDGET(gui_slot->image));
	gtk_widget_set_size_request( gui_slot->image, info->image_dimensions[0],info->image_dimensions[1] );
	g_signal_connect( gui_slot->image, "expose_event",
			G_CALLBACK(image_expose_event), 
			(gpointer) info->sample_banks[bank_nr]->slot[slot_nr]->slot_number  );
	gtk_widget_show( GTK_WIDGET(gui_slot->image));

	/* the upper container for all slot-informations */
	gui_slot->upper_hbox = gtk_hbox_new(FALSE,0);
	gtk_box_pack_start (GTK_BOX (gui_slot->main_vbox), gui_slot->upper_hbox, FALSE, TRUE, 0);
	gtk_widget_show(GTK_WIDGET(gui_slot->upper_hbox));


	if( sample_banks[bank_nr]->slot[slot_nr]->sample_type >= 0 )
	{ 
		/* the hotkey that is assigned to this slot */
		sprintf(hotkey, "F-%d", (slot_nr+1));	
		gui_slot->hotkey = GTK_LABEL(gtk_label_new(hotkey));
	}
	else
	{
		gui_slot->hotkey = GTK_LABEL(gtk_label_new(""));
	}
	gtk_misc_set_alignment(GTK_MISC(gui_slot->hotkey), 0.0, 0.0);
	gtk_misc_set_padding (GTK_MISC(gui_slot->hotkey), 0, 0);	
	gtk_box_pack_start (GTK_BOX (gui_slot->upper_hbox), GTK_WIDGET(gui_slot->hotkey), FALSE, FALSE, 0);
	gtk_widget_show(GTK_WIDGET(gui_slot->hotkey));
	gui_slot->upper_vbox = gtk_vbox_new(FALSE,0);
	gtk_box_pack_start (GTK_BOX (gui_slot->upper_hbox), gui_slot->upper_vbox, TRUE, TRUE, 0);
	gtk_widget_show(GTK_WIDGET(gui_slot->upper_vbox));
	gui_slot->title = GTK_LABEL(gtk_label_new(""));
//	gtk_misc_set_alignment(GTK_MISC(gui_slot->title), 0.00, 0.00);
//	gtk_misc_set_padding (GTK_MISC(gui_slot->title), 0, 0);	
//	gtk_box_pack_start (GTK_BOX (gui_slot->upper_vbox), GTK_WIDGET(gui_slot->title), FALSE, FALSE, 0);
//	gtk_widget_show(GTK_WIDGET(gui_slot->title));	
	
	gui_slot->timecode = GTK_LABEL(gtk_label_new(""));
	gtk_misc_set_alignment(GTK_MISC(gui_slot->timecode), 0.0, 0.0);
	gtk_misc_set_padding (GTK_MISC(gui_slot->timecode), 0,0 );	
	gtk_box_pack_start (GTK_BOX (gui_slot->upper_vbox), GTK_WIDGET(gui_slot->timecode), FALSE, FALSE, 0);
	gtk_widget_show(GTK_WIDGET(gui_slot->timecode));		

}


/* --------------------------------------------------------------------------------------------------------------------------
 *  Handler of mouse clicks on the GUI-elements of one slot
 *  single-click activates the slot and the loaded sample (if there is one)
 *  double-click or tripple-click activates it and plays it immediatelly
   -------------------------------------------------------------------------------------------------------------------------- */ 
static gboolean on_slot_activated_by_mouse (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	gint bank_nr = -1;
	gint slot_nr = -1;
	
	bank_nr = find_bank( gtk_notebook_get_current_page(GTK_NOTEBOOK(info->sample_bank_pad)));
	if(bank_nr < 0 )
		return FALSE;

	slot_nr = (gint *)user_data;
	sample_bank_t **sample_banks = info->sample_banks;

	/* Dont select slot if nothing is there */
	if( info->sample_banks[ bank_nr ]->slot[ slot_nr ]->sample_id <= 0 )
		return FALSE;

//	if(info->selected_gui_slot != NULL || info->selected_slot != NULL )
//		set_activation_of_slot_in_samplebank( FALSE );
	if( event->type == GDK_2BUTTON_PRESS )
	{
		sample_slot_t *s = sample_banks[bank_nr]->slot[slot_nr];
//	info->selected_slot = sample_banks[bank_nr]->slot[slot_nr];	
//	info->selected_gui_slot = sample_banks[bank_nr]->gui_slot[slot_nr];
//	fprintf(stderr, "New slot = %p\n",info->selected_slot);
//	set_activation_of_slot_in_samplebank(FALSE);	
		multi_vims( VIMS_SET_MODE_AND_GO, "%d %d", s->sample_type, s->sample_id);
		no_preview_ = 1;
	}
	else if(event->type == GDK_BUTTON_PRESS )
	{
		if(info->selection_slot)
			set_selection_of_slot_in_samplebank(FALSE);
		info->selection_slot = sample_banks[bank_nr]->slot[slot_nr];
		info->selection_gui_slot = sample_banks[bank_nr]->gui_slot[slot_nr];
		set_selection_of_slot_in_samplebank(TRUE );
	
	}
//	redraw bank page?
	return FALSE;

}
/* --------------------------------------------------------------------------------------------------------------------------
 *  Handler of key presses on the GUI-elements of one slot
   -------------------------------------------------------------------------------------------------------------------------- */
static gboolean on_slot_activated_by_key (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	// test
	printf("%s\n","press");	
	return FALSE;
}		


/* --------------------------------------------------------------------------------------------------------------------------
 *  Function that handles to select/activate a special slot in the samplebank
   -------------------------------------------------------------------------------------------------------------------------- */
static void set_activation_of_slot_in_samplebank( gboolean activate)
{
	if(!info->selected_gui_slot || !info->selected_slot )
		return;
	GdkColor color;
	color.red = info->fg_->red;
	color.green = info->fg_->green;
	color.blue = info->fg_->blue;
	
	if(info->selected_slot->sample_id <= 0 )
	{
		gtk_frame_set_shadow_type( info->selected_gui_slot->frame, GTK_SHADOW_ETCHED_IN );
	}
	else
	{
		if (activate)
		{
			color.green = 0xffff;
			color.red = 0;
			color.blue =0;
			gtk_frame_set_shadow_type(info->selected_gui_slot->frame,GTK_SHADOW_IN);
			gtk_widget_grab_focus(GTK_WIDGET(info->selected_gui_slot->frame));
		}
		else 
		{
			gtk_frame_set_shadow_type(info->selected_gui_slot->frame,GTK_SHADOW_ETCHED_IN);
		}
	}

	gtk_widget_modify_fg ( info->selected_gui_slot->timecode,
		GTK_STATE_NORMAL, &color );
	gtk_widget_modify_fg ( gtk_frame_get_label_widget( info->selected_gui_slot->frame ),
		GTK_STATE_NORMAL, &color );
	
}

static	void	set_selection_of_slot_in_samplebank(gboolean active)
{
	if(!info->selection_slot)
		return;
	if(info->selection_slot->sample_id <= 0 )
		return;
	GdkColor color;
	color.red = info->fg_->red;
	color.green = info->fg_->green;
	color.blue = info->fg_->blue;	
	if(active)
	{
		color.blue = 0xffff;
		color.green = 0;
		color.red =0;
	}

	if(info->selected_slot == info->selection_slot)
	{
		color.green = 0xffff;
		color.red = 0;	
		color.blue = 0;
	}
//	gtk_widget_modify_fg ( info->selection_gui_slot->title,
//		GTK_STATE_NORMAL, &color );
	gtk_widget_modify_fg ( info->selection_gui_slot->timecode,
		GTK_STATE_NORMAL, &color );
	gtk_widget_modify_fg ( gtk_frame_get_label_widget( info->selection_gui_slot->frame ),
		GTK_STATE_NORMAL, &color );

}

static int add_sample_to_sample_banks(int bank_page,sample_slot_t *slot)
{
	/* Add the temporary sample */
	// FIXME : slot loading
	int bp = 0; int s = 0;

       if(verify_bank_capacity( &bp, &s, slot->sample_id, slot->sample_type ))
	       update_sample_slot_data( bp, s, slot->sample_id,slot->sample_type,slot->title,slot->timecode);

 
       return 1;
}


/* --------------------------------------------------------------------------------------------------------------------------
 *  Removes a selected sample from the specific sample-bank-slot and update the free_slots-GList as well as
   -------------------------------------------------------------------------------------------------------------------------- */

static	void	remove_sample_from_slot()
{
	gint bank_nr = -1;
	gint slot_nr = -1;

	bank_nr = find_bank( gtk_notebook_get_current_page(
		GTK_NOTEBOOK( info->sample_bank_pad ) ) );
	if(bank_nr < 0 )
		return;
	if(!info->selection_slot)
		return;

	slot_nr = info->selection_slot->slot_number;

	if( info->selection_slot->sample_id == info->status_tokens[CURRENT_ID] &&
		info->selection_slot->sample_type == info->status_tokens[PLAY_MODE] )
	{
		gchar error_msg[100];
		sprintf(error_msg, "Cannot delete %s %d while playing",
			(info->selection_slot->sample_type == MODE_SAMPLE ? "Sample" : "Stream" ),
			info->selection_slot->sample_id );
		message_dialog( "Error while deleting", error_msg );

		return;
	}

	multi_vims( (info->selection_slot->sample_type == 0 ? VIMS_SAMPLE_DEL :
		     VIMS_STREAM_DELETE ),
			"%d",
			info->selection_slot->sample_id );
	gint id_len = 0;
	gint deleted_sample = 0;
	gchar *deleted_id = recv_vims( 3, &id_len );
	sscanf( deleted_id, "%d", &deleted_sample );
	if( deleted_sample )
	{
		// decrement history of delete type
		int *his = info->history_tokens[ (info->status_tokens[PLAY_MODE]) ];
		
		his[TOTAL_SLOTS] = his[TOTAL_SLOTS] - 1;

		update_sample_slot_data( bank_nr, slot_nr, 0, -1, NULL, NULL); 	 	

		set_selection_of_slot_in_samplebank( FALSE );
		info->selection_gui_slot = NULL;
		info->selection_slot = NULL;
	}
}


/* --------------------------------------------------------------------------------------------------------------------------
 *  Function adds the given infos to the list of effect-sources
   -------------------------------------------------------------------------------------------------------------------------- */ 
static void add_sample_to_effect_sources_list(gint id, gint type, gchar *title, gchar *timecode)
{
	gchar id_string[10];
	GtkTreeIter iter;	

	if (type == STREAM_NO_STREAM) sprintf( id_string, "S%04d", id);    
	else sprintf( id_string, "T%04d", id);

	gtk_list_store_append( effect_sources_store, &iter );
	gtk_list_store_set( effect_sources_store, &iter, SL_ID, id_string, SL_DESCR, title, SL_TIMECODE , timecode,-1 );

}


/* --------------------------------------------------------------------------------------------------------------------------
 *  Function adds the given infos to Editlist
   -------------------------------------------------------------------------------------------------------------------------- 
static void add_sample_to_editlist(guint row_number, gchar *timeline, gchar *fname, gchar *timecode, gchar *gfourcc)
{
    GtkTreeIter iter;
    
    gtk_list_store_append( editlist_store, &iter );
    gtk_list_store_set( editlist_store, &iter,
	COLUMN_INT, row_number, // <- start timecode?! (timecode of offset)
	COLUMN_STRING0, timeline,
	COLUMN_STRINGA, fname,
	COLUMN_STRINGB, timecode,
	COLUMN_STRINGC, gfourcc,-1 );	

}
*/

/*
	Update a slot, either set from function arguments or clear it
 */
static void update_sample_slot_data(int page_num, int slot_num, int sample_id, gint sample_type, gchar *title, gchar *timecode)
{
	//@! 
	lock_preview();
	
	sample_slot_t *slot = info->sample_banks[page_num]->slot[slot_num];
	sample_gui_slot_t *gui_slot = info->sample_banks[page_num]->gui_slot[slot_num];

	if(slot->timecode) free(slot->timecode);
	if(slot->title) free(slot->title);
	
    	slot->sample_id = sample_id;
    	slot->sample_type = sample_type;
	slot->timecode = timecode == NULL ? strdup("") : strdup( timecode );
	slot->title = title == NULL ? strdup("") : strdup( title );
	// add sample/stream to sources
	if(sample_id > 0)
		add_sample_to_effect_sources_list(sample_id, sample_type, title, timecode);
	

	if(gui_slot)
	{
//		if(gui_slot->title)
//			gtk_label_set_text( GTK_LABEL( gui_slot->title ), slot->title );
		if(gui_slot->timecode)
			gtk_label_set_text( GTK_LABEL( gui_slot->timecode ), slot->timecode );

		if(sample_id > 0 )
		{
			gchar frame_title[20];
			sprintf(frame_title, "%s", slot->title );
			gtk_frame_set_label( gui_slot->frame, frame_title );
		}
		else
		{
			gtk_frame_set_label(gui_slot->frame, NULL );
		}
	}

	if( sample_id == 0 )
	{
		if(slot->pixbuf)
		{
			gdk_pixbuf_unref( slot->pixbuf );
			slot->pixbuf = NULL;
		}
	}

	unlock_preview();
}


/*
      Adding a video clip as sample; the VIMS command editlist add sample returns
      a new ID, which we read by gveejay_new_slot()
      then, we create a temporary sample and use this object to update the bank.
 */

/* -------------------------------------------------------------------------------------------------------------------------- 
 * Function that is used for the VJ-GUI to show the options-dailog for a selected sample/stream
 * and decides if the selected slot contains an video-sample or an stream
 * -------------------------------------------------------------------------------------------------------------------------- */ 
/* -------------------------------------------------------------------------------------------------------------------------- 
 * Function that is used to show in the options dialog the values of the selected sample... This function decides wether to 
 * take these values from the current loaded sample or by using a different way if the selected sample is not the currently
 * loaded on. The gboolean-Flag determines if the options-dialog is filled with actual data even if the selected sample
 * isn't currently played ... this is useful to avoid constantly reloading of unchanged values if the dialog currently
 * shows data of a non-played sample
   -------------------------------------------------------------------------------------------------------------------------- */ 

/* -------------------------------------------------------------------------------------------------------------------------- 
 * Function that is used for the VJ-GUI to apply all changes that are made within the sample/stream-options dialog
 * to the sample/stream that is shown within this dialog (which is the selected slot of the sample bank)
 * -------------------------------------------------------------------------------------------------------------------------- */



/* -------------------------------------------------------------------------------------------------------------------------- 
 *
 * Function initialize the knobs to regulate the global speed and the audio volume 
 *  --------------------------------------------------------------------------------------------------------------------------*/
void setup_knobs()
{		
    GtkAdjustment *speed_adj;       
    char path[MAX_PATH_LEN];
    bzero(path,MAX_PATH_LEN);
    get_gd( path, NULL,  "knob.png" );
    // audio volume 
	GtkAdjustment *adj = GTK_ADJUSTMENT(gtk_adjustment_new( 100.0,0.0,100.0,0.1,1.0,0.0 ));

    info->audiovolume_knob = (GtkKnob *) gtk_knob_new(adj, path);
    gtk_widget_show(GTK_WIDGET(info->audiovolume_knob));
    gtk_widget_set_sensitive_( GTK_WIDGET(info->audiovolume_knob), TRUE);	
    gtk_container_add (GTK_CONTAINER (glade_xml_get_widget_( info->main_window, "audio_knobframe")), GTK_WIDGET(info->audiovolume_knob));    

  //  g_signal_connect( adj, "value_changed", (GCallback) on_audiovolume_knob_value_changed, NULL );
    update_label_i( "volume_label", 100,0); 

    // speed
/*    info->speed_knob = (GtkKnob *) gtk_knob_new(NULL,path);
    gtk_widget_show(GTK_WIDGET(info->speed_knob));
    gtk_widget_set_sensitive_( GTK_WIDGET(info->speed_knob), TRUE);	
    gtk_container_add (GTK_CONTAINER (glade_xml_get_widget_( info->main_window, "speed_knobframe")), GTK_WIDGET(info->speed_knob));    

    speed_adj = gtk_knob_get_adjustment(info->speed_knob);
    speed_adj->lower = 0;
    speed_adj->upper = 12;
    speed_adj->step_increment = 1;    
    speed_adj->page_size = 0;
    speed_adj->page_increment = 1;    
    g_signal_connect( speed_adj, "value_changed", (GCallback) on_speed_knob_value_changed, NULL );
 */
}


void	veejay_release_track(int id, int release_this)
{
	multitrack_release_track( info->mt, id, release_this );
}

void	veejay_bind_track( int id, int bind_this )
{
	multitrack_bind_track(info->mt, id, bind_this ); 
}
