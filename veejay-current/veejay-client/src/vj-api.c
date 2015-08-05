
/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
 *  with contributions by  Thomas Rheinhold (2005)
 * 	                   (initial sampledeck representation in GTK) 
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
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <stdarg.h>
#include <glib.h>
#include <errno.h>
#include <glade/glade.h>
#include <gtk/gtk.h>
#include <string.h>
#include <sys/time.h>
#include <stdint.h>
#include <veejay/vjmem.h>
#include <veejay/vje.h>
#include <veejay/vj-client.h>
#include <veejay/vj-msg.h>
#include <veejay/vims.h>
#include <src/vj-api.h>
#include <fcntl.h>
#include "mpegconsts.h"
#include "mpegtimecode.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cellrendererspin.h>
#include <gtkknob.h>
#include <gtktimeselection.h>
#include <libgen.h>
#ifdef HAVE_SDL
#include <src/keyboard.h>
#endif
#include <gtk/gtkversion.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <src/curve.h>
#include <src/multitrack.h>
#include <src/common.h>
#include <src/utils.h>
#include <src/sequence.h>
#include <veejay/yuvconv.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
#include <veejay/vevo.h>
#include <veejay/libvevo.h>
#include <veejay/vevo.h>
#include <src/vmidi.h>
//if gtk2_6 is not defined, 2.4 is assumed.
#ifdef GTK_CHECK_VERSION
#if GTK_MINOR_VERSION >= 6
  #define HAVE_GTK2_6 1
#endif  
#if GTK_MINOR_VERSION >= 8
 #define HAVE_GTK2_8 1
#endif
#endif
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#ifdef ARCH_X86_64
static gpointer castIntToGpointer( int val ) {
	int64_t g_int64_val = (int64_t) val;
	return (gpointer) g_int64_val;
}
#else
static gpointer castIntToGpointer( int val) {
	return (gpointer) val;
}
#endif
static int ui_skin_ = 0;
static struct
{
	const int id;
	const char *name;
	const int page;
} crappy_design[] =
{
	{ 1,"notebook18", 3 },    // On which notebook page is the multitrack view
	{ 0,"vjdeck", 2 },
	{ 0,NULL, 0 }
};

#define MAX_SLOW 25 

static struct
{
	const char *text;
} tooltips[] =
{
	{"Mouse left: Set in point, Mouse right: Set out point, Double click: Clear selected, Mouse middle: Drag selection"},
	{"Mouse left/right: Play slot, Shift + Mouse left: Put sample in slot. You can also put selected samples."},
	{"Mouse left click: Select slot (sample in slot), Mouse double click: Play sample in slot, Mouse left + SHIFT: Set slot as mixing current mixing channel"},
	{"Select a SRT sequence to edit"},
	{NULL},
};

enum {
	TOOLTIP_TIMELINE = 0,
	TOOLTIP_QUICKSELECT = 1,
	TOOLTIP_SAMPLESLOT = 2,
	TOOLTIP_SRTSELECT = 3
};

#define FX_PARAMETER_DEFAULT_NAME "<none>"

enum
{
	STREAM_NO_STREAM = 0,
	STREAM_RED = 9,
	STREAM_GREEN = 8,
	STREAM_GENERATOR = 7,
	STREAM_CALI = 6,
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
	ENTRY_KF_TYPE = 3,
	ENTRY_KF_STATUS = 4,
	ENTRY_KF_START = 5,
	ENTRY_KF_END = 6,
	ENTRY_SOURCE = 7,
	ENTRY_CHANNEL = 8,
	ENTRY_VIDEO_ENABLED = 9,
	ENTRY_AUDIO_ENABLED = 10,
	ENTRY_P0 = 11,
	ENTRY_P1 = 12,
	ENTRY_P2 = 13,
	ENTRY_P3 = 14,
	ENTRY_P4 = 15,
	ENTRY_P5 = 16,
	ENTRY_P6 = 17,
	ENTRY_P8 = 18,
	ENTRY_P9 = 19,
	ENTRY_P10 = 20,
	ENTRY_P11 = 21,
	ENTRY_P12 = 22,
	ENTRY_P13 = 23,
	ENTRY_P14 = 24,
	ENTRY_P15 = 25,
	ENTRY_LAST = 26
};

#define ENTRY_PARAMSET ENTRY_P0

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
	HINT_SEQ_ACT = 14,
	HINT_SEQ_CUR = 15,
	NUM_HINTS = 16
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
	int	selected_vims_accel[2];
	int	render_record; 
	int	entry_tokens[ENTRY_LAST];
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
	int	expected_slots;
	stream_templ_t	strtmpl[2]; // v4l, dv1394
	int	selected_parameter_id; // current kf
	int	selected_vims_type;
	char    *selected_vims_args;
	int	cali_duration;
	int	cali_stage;
} veejay_user_ctrl_t;

typedef struct
{
	float	fps;
	float 	ratio;
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
	gchar *args;
} vims_t;

typedef struct
{
	gint keyval;
	gint state;
	gchar *args;
	gchar *vims;
	gint event_id;
} vims_keys_t;

static  int	user_preview = 0;
static	int	NUM_BANKS =	50;
static 	int	NUM_SAMPLES_PER_PAGE = 12;
static	int	NUM_SAMPLES_PER_COL = 6;
static	int	NUM_SAMPLES_PER_ROW = 2;
static int use_key_snoop = 0;

#define G_MOD_OFFSET 200
#define SEQUENCE_LENGTH 1024
#define MEM_SLOT_SIZE 32

static	vims_t	vj_event_list[VIMS_MAX];
static  vims_keys_t vims_keys_list[VIMS_MAX];
static  int vims_verbosity = 0;
#define   livido_port_t vevo_port_t
static	int	cali_stream_id = 0;
static	int	cali_onoff     = 0;
static int geo_pos_[2] = { -1,-1 };
static	vevo_port_t *fx_list_ = NULL;

typedef struct
{
	GtkWidget *title;
	GtkWidget *timecode;
	GtkWidget *hotkey;
//	GtkWidget *edit_button;
	GtkTooltips *tips;
	GtkWidget *image;
	GtkWidget  *frame;
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
	GtkWidget *frame;
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

typedef struct
{
	char	*hostname;
	int	port_num;
	int	state;    // IDLE, PLAYING, RECONNECT, STOPPED 
	struct timeval p_time;
	int	w_state; // watchdog state 
	int	w_delay;
} watchdog_t;

typedef struct
{
	GladeXML *main_window;
	vj_client	*client;
	int		status_tokens[32]; 	/* current status tokens */
	int		*history_tokens[4];		/* list last known status tokens */
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
//	GIOChannel	*channel;
	GdkColormap	*color_map;
	gint		connecting;
//	gint		logging;
	gint		streamrecording;
	gint		samplerecording;
//	gint		cpumeter;
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
	GtkWidget	*sample_bank_pad;
	GtkWidget	*quick_select;
	GtkWidget	*sample_sequencer;
	sample_bank_t	**sample_banks;
	sample_slot_t	*selected_slot;
	sample_slot_t 	*selection_slot;
	sample_gui_slot_t *selected_gui_slot;
	sample_gui_slot_t *selection_gui_slot;
	sequence_envelope *sequence_view;
	sequence_envelope *sequencer_view;
	int		   sequence_playing;
	gint		current_sequence_slot;
//	GtkKnob		*audiovolume_knob;
//	GtkKnob		*speed_knob;	
	int		image_dimensions[2];
//	guchar		*rawdata;
	int		prev_mode;
	GtkWidget	*tl;
	config_settings_t	config;
	int		status_frame;
	int		key_id;
	GdkColor	*fg_;
	gboolean	key_now;	
	void		*mt;
	watchdog_t	watch;
	int		vims_line;
	void		*midi;
	struct timeval	time_last;
	uint8_t 	*cali_buffer;
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
	STATE_WAIT_FOR_USER = 7,
	STATE_QUIT = 8,
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
	V4L_LOCATION=3,
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

typedef struct 
{
	const char *text;
} slider_name_t;

static slider_name_t *slider_names_ = NULL;

#define MAX_PATH_LEN 1024
#define VEEJAY_MSG_OUTPUT	4

static	vj_gui_t	*info = NULL;
void	reloaded_restart();
void 	*get_ui_info() { return (void*) info; }
void	reloaded_schedule_restart();
/* global pointer to the sample-bank */

/* global pointer to the effects-source-list */
static	GtkWidget *effect_sources_tree = NULL;
static 	GtkListStore *effect_sources_store = NULL;
static 	GtkTreeModel *effect_sources_model = NULL;


static	GtkWidget	*cali_sourcetree = NULL;
static	GtkListStore	*cali_sourcestore = NULL;
static  GtkTreeModel    *cali_sourcemodel = NULL;

static int 		num_tracks_ = 2;
/* global pointer to the editlist-tree */
static 	GtkWidget *editlist_tree = NULL;
static	GtkListStore *editlist_store = NULL;
static  GtkTreeModel *editlist_model = NULL;	
//void    gtk_configure_window_cb( GtkWidget *w, GdkEventConfigure *ev, gpointer data );
static	int	get_slider_val(const char *name);
void    vj_msg(int type, const char format[], ...);
//static  void    vj_msg_detail(int type, const char format[], ...);
void	msg_vims(char *message);
static  void    multi_vims(int id, const char format[],...);
static  void 	single_vims(int id);
static	gdouble	get_numd(const char *name);
static void	vj_kf_select_parameter(int id);
static  int     get_nums(const char *name);
static  gchar   *get_text(const char *name);
static	void	put_text(const char *name, char *text);
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
static	void	update_globalinfo(int *his, int p, int k);
static	gint	load_parameter_info();
static	void	load_v4l_info();
static	void	reload_editlist_contents();
static  void    load_effectchain_info();
static  void    load_effectlist_info();
static	void	load_sequence_list();
static  void    load_samplelist_info(gboolean with_reset_slotselection);
static  void    load_editlist_info();
static	void	set_pm_page_label(int sample_id, int type);
#ifndef STRICT_CHECKING
static	void	disable_widget_( const char *name );
static	void	enable_widget_(const char *name );
#define enable_widget(a) enable_widget_(a)
#define disable_widget(a) disable_widget_(a)
#else
static	void	disable_widget_( const char *name, const char *function, int line );
static	void	enable_widget_(const char *name, const char *function, int line );
#define enable_widget(a) enable_widget_(a,__FUNCTION__,__LINE__ )
#define disable_widget(a) disable_widget_(a,__FUNCTION__,__LINE__)
#endif
static	void	setup_tree_spin_column(const char *tree_name, int type, const char *title);
static	void	setup_tree_text_column( const char *tree_name, int type, const char *title, int expand );
static	void	setup_tree_pixmap_column( const char *tree_name, int type, const char *title );
gchar	*_utf8str( const char *c_str );
static gchar	*recv_vims(int len, int *bytes_written);
static  GdkPixbuf *	update_pixmap_kf( int status );
static  GdkPixbuf *	update_pixmap_entry( int status );
static gboolean
chain_update_row(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,
             gpointer data);
int	resize_primary_ratio_y();
int	resize_primary_ratio_x();
static	void	update_rgbkey();
static	int	count_textview_buffer(const char *name);
static	void	clear_textview_buffer(const char *name);
static	void	init_recorder(int total_frames, gint mode);
static	void	reload_bundles();
static	void	update_rgbkey_from_slider();
static	gchar	*get_textview_buffer(const char *name);
static void 	  create_slot(gint bank_nr, gint slot_nr, gint w, gint h);
static void 	  setup_samplebank(gint c, gint r, GtkWidget *pad, gint *image_w, gint *image_h);
static int 	  add_sample_to_sample_banks( int bank_page,sample_slot_t *slot );
static void 	  update_sample_slot_data(int bank_num, int slot_num, int id, gint sample_type, gchar *title, gchar *timecode);
static gboolean   on_slot_activated_by_mouse (GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static void 	  add_sample_to_effect_sources_list(gint id, gint type, gchar *title, gchar *timecode);
static void 	  set_activation_of_slot_in_samplebank(gboolean activate);
int		gveejay_new_slot(int stream);
static	int	bank_exists( int bank_page, int slot_num );
static	int	find_bank_by_sample(int sample_id, int sample_type, int *slot );
static int	add_bank( gint bank_num  );
static void    set_selection_of_slot_in_samplebank(gboolean active);
static void    remove_sample_from_slot();
static void create_ref_slots(int envelope_size);
static void create_sequencer_slots(int x, int y);
//void setup_knobs();
void   free_samplebank(void);
void   reset_samplebank(void);
int   verify_bank_capacity(int *bank_page_, int *slot_, int sample_id, int sample_type );
static void widget_get_rect_in_screen (GtkWidget *widget, GdkRectangle *r);
static  void   update_curve_widget(const char *name);
static void    update_curve_accessibility(const char *name);
static	void	reset_tree(const char *name);
static	void	reload_srt();
static	void	reload_fontlist();
static	void	indicate_sequence( gboolean active, sequence_gui_slot_t *slot );
static	void set_textview_buffer(const char *name, gchar *utf8text);
void	interrupt_cb();
int	get_and_draw_frame(int type, char *wid_name);
void reset_cali_images( int type, char *wid_name );

GtkWidget	*glade_xml_get_widget_( GladeXML *m, const char *name )
{
	GtkWidget *widget = glade_xml_get_widget( m , name );
	if(!widget)
	{
#ifdef STRICT_CHECKING
		veejay_msg(0,"Missing widget: %s %s ",__FUNCTION__,name);
#endif
		return NULL;
	}
#ifdef STRICT_CHECKING
	assert( widget != NULL );
#endif
	return widget;		
}
void		gtk_notebook_set_current_page__( GtkWidget *w, gint num, const char *f, int line )
{
#ifdef STRICT_CHECKING
	veejay_msg(0, "%s: %d from %s:%d", __FUNCTION__, num,f,line);
#endif
	gtk_notebook_set_current_page( GTK_NOTEBOOK(w), num );
}


void		gtk_widget_set_size_request__( GtkWidget *w, gint iw, gint h, const char *f, int line )
{
#ifdef STRICT_CHECKING
//	veejay_msg(0, "%s: %dx%d from %s:%d", __FUNCTION__, iw,h,f,line);
#endif
	gtk_widget_set_size_request(w, iw, h );
}

#ifndef STRICT_CHECKING
#define gtk_widget_set_size_request_(a,b,c) gtk_widget_set_size_request(a,b,c)
#define gtk_notebook_set_current_page_(a,b) gtk_notebook_set_current_page(a,b)
#else
#define gtk_widget_set_size_request_(a,b,c) gtk_widget_set_size_request__(a,b,c,__FUNCTION__,__LINE__)
#define gtk_notebook_set_current_page_(a,b) gtk_notebook_set_current_page__(a,b,__FUNCTION__,__LINE__)
#endif

static struct
{
	gchar *text;
} text_msg_[] =
{
	{	"Running realtime" 	},
	{	NULL			},
};

enum {
	TEXT_REALTIME = 0
};

static struct
{
	const char *name;
} capt_card_set[] = 
{
	{ "v4l_expander" },
	{ "v4l_brightness" },
	{ "v4l_contrast" },
	{ "v4l_hue"  },
	{ "v4l_saturation" },
	{ "v4l_color" },
	{ "v4l_white" },
	{ NULL },
};

static	int	preview_box_w_ = MAX_PREVIEW_WIDTH;
static  int	preview_box_h_ = MAX_PREVIEW_HEIGHT;


static	void		*bankport_ = NULL;

int	vj_get_preview_box_w()
{
	return preview_box_w_;
}

int	vj_get_preview_box_h()
{
	return preview_box_h_;
}

#ifdef STRICT_CHECKING
static	void	gtk_image_set_from_pixbuf__( GtkImage *w, GdkPixbuf *p, const char *f, int l )
{
	assert( GTK_IS_IMAGE(w) );
	gtk_image_set_from_pixbuf(w, p);
}

static	void	gtk_widget_set_sensitive___( GtkWidget *w, gboolean state, const char *f, int l )
{
	assert( GTK_IS_WIDGET(w) );
	gtk_widget_set_sensitive(w, state );
}
#endif

static	void	select_slot(int pm);

#ifdef STRICT_CHECKING
#define gtk_widget_set_sensitive_( w,p ) gtk_widget_set_sensitive___( w,p,__FUNCTION__,__LINE__ )
#define gtk_image_set_from_pixbuf_(w,p) gtk_image_set_from_pixbuf__( w,p, __FUNCTION__,__LINE__ )
#else
#define gtk_widget_set_sensitive_( w,p ) gtk_widget_set_sensitive( w,p )
#define gtk_image_set_from_pixbuf_(w,p) gtk_image_set_from_pixbuf( w,p )
#endif

static	struct
{
	const char *name;
} uiwidgets[] =
{
	{"veejay_box"},
	{NULL}
};

static struct
{
	const char *name;
} plainwidgets[] = 
{
	{"video_navigation_buttons"},
	{"button_084"}, 
	{"button_083"}, 
	{"button_samplestart"},
	{"button_sampleend"},
	{"speed_slider"},
	{"slow_slider"},
	{"vjframerate"},
	{"markerframe"},
	{NULL}
};

static	struct
{	
	const char *name;
} samplewidgets[] = 
{
	{"sample_loop_box"},
	{"button_084"}, 
	{"button_083"}, 
	{"video_navigation_buttons"},
	{"button_samplestart"},
	{"button_sampleend"},
	{"speed_slider"},
	{"slow_slider"},
	{"button_200"}, // mask button
	{"frame_fxtree"},
	{"frame_fxtree3"},
	{"fxpanel"},
	{"panels"},
	{"vjframerate"},
	{"scrolledwindow49"}, // srt stuff
	{"samplegrid_frame"},	
	{"markerframe"},
	{NULL}
};

static	struct
{
	const char *name;
} streamwidgets[] =
{
	{"button_200"}, // mask button
	{"frame_fxtree"},
	{"frame_fxtree3"},
	{"fxpanel"},
	{"panels"},
	{"scrolledwindow49"}, // srt stuff
	{NULL},
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

static  sample_slot_t *find_slot_by_sample( int sample_id , int sample_type );
static  sample_gui_slot_t *find_gui_slot_by_sample( int sample_id , int sample_type );

gchar	*_utf8str(const char *c_str)
{
	gsize	bytes_read = 0;
	gsize 	bytes_written = 0;
	GError	*error = NULL;
	if(!c_str)
		return NULL;
	char	*result = (char*) g_locale_to_utf8( c_str, -1, &bytes_read, &bytes_written, &error );

	if(error)
	{
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
	GdkColor *c = (GdkColor*)vj_calloc(sizeof(GdkColor));
	GtkStyle *s = gtk_widget_get_style( w);
	c->red   = s->fg[0].red;
	c->green = s->fg[0].green;
	c->blue	 = s->fg[0].blue;
	return c;
}

static void scan_devices( const char *name)
{
	GtkWidget *tree = glade_xml_get_widget_(info->main_window,name);
	GtkListStore *store;
	GtkTreeIter iter;

	reset_tree(name);
	gint len = 0;
	single_vims( VIMS_DEVICE_LIST );
	gchar *text = recv_vims(6,&len);
	if(len <= 0|| !text )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "No capture devices found on veejay server");
		return;
	}
	GtkTreeModel *model = gtk_tree_view_get_model
		(GTK_TREE_VIEW(tree));

	store = GTK_LIST_STORE(model);

	gint offset =0;
	gint i = 0;
	gchar *ptr = text + offset;
	while( offset < len )
	{
		char tmp[4];

		gchar *name = NULL;
		gdouble gchannel = 1.0;
		gchar *loca = NULL;
		
		gint name_len=0;
		gint loc_len=0;
		
		strncpy(tmp,ptr+offset,3);
		tmp[3] = '\0';
		offset += 3;
		name_len = atoi( tmp );
		if(name_len <=  0 )
		{
			veejay_msg(0, "Reading name of capture device: '%s'",ptr+offset );
			return;
		}
		name = strndup( ptr + offset, name_len );
		offset += name_len;
		strncpy( tmp, ptr + offset, 3 );
		tmp[3] = '\0';
		offset += 3;
	
		loc_len = atoi( tmp );
		if( loc_len <= 0 )
		{
			veejay_msg(0, "Reading location of capture device");
			return;
		}	
		loca = strndup( ptr + offset, loc_len );
		offset += loc_len;
		gchar *thename = _utf8str( name );
		gchar *theloca = _utf8str( loca );
		
		gtk_list_store_append( store, &iter);
		gtk_list_store_set(
				store, &iter,
					V4L_NUM, i,
				 	V4L_NAME, thename, 
					V4L_SPINBOX, gchannel,
				    V4L_LOCATION, theloca,
					-1);
		
		g_free(thename);
		g_free(theloca);

		free(loca);
		free(name);
		i ++;
	}
	free(text);

	gtk_tree_view_set_model(GTK_TREE_VIEW(tree), model );
}

static	void	set_tooltip_by_widget(GtkWidget *w, const char *text)
{
	gtk_widget_set_tooltip_text( w,text );
}

static	void	set_tooltip(const char *name, const char *text)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
	if(!w) {
#ifdef STRICT_CHECKING
		veejay_msg(0, "Widget '%s' not found",name);
#endif
		return;
	}
	gtk_widget_set_tooltip_text(	w,text );
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
		gint channel =	info->uc.strtmpl[0].channel;
		gint	num = info->uc.strtmpl[0].dev;
	
		multi_vims( VIMS_STREAM_NEW_V4L,"%d %d",
				num,
				channel
				);
		gveejay_new_slot(MODE_STREAM);

	}
}

gboolean	device_selection_func( GtkTreeSelection *sel, 
					GtkTreeModel *model,
					GtkTreePath  *path,
					gboolean path_currently_selected,
					gpointer userdata )
{
	GtkTreeIter iter;
	GValue val = { 0, };
	if( gtk_tree_model_get_iter( model, &iter, path ) )
	{
		gint num = 0;
		//gtk_tree_model_get(model, &iter, V4L_NUM,&num, -1 );
		gchar *file = NULL;
		gtk_tree_model_get( model, &iter, V4L_LOCATION, &file, -1 );
		sscanf( file, "/dev/video%d", &num );
		if(! path_currently_selected )
		{
			gtk_tree_model_get_value(model, &iter, V4L_SPINBOX, &val);
			info->uc.strtmpl[0].dev = num;
			info->uc.strtmpl[0].channel = (int) g_value_get_float(&val);
		}
		g_free(file);
	}
	return TRUE;
}

static void	setup_v4l_devices()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_v4ldevices");
	GtkListStore *store = gtk_list_store_new( 4, G_TYPE_INT, G_TYPE_STRING, G_TYPE_FLOAT,
				   G_TYPE_STRING	);

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	GtkTreeSelection *sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( tree ) );
	gtk_tree_selection_set_mode( sel, GTK_SELECTION_SINGLE );
	gtk_tree_selection_set_select_function( sel, device_selection_func, NULL,NULL );

//	gtk_tree_view_set_fixed_height_mode( GTK_TREE_VIEW(tree), TRUE );

	g_object_unref( G_OBJECT( store ));
	setup_tree_text_column( "tree_v4ldevices", V4L_NUM, "#",0 );
	setup_tree_text_column( "tree_v4ldevices", V4L_NAME, "Device Name",0);
	setup_tree_spin_column( "tree_v4ldevices", V4L_SPINBOX, "Channel");
	setup_tree_text_column( "tree_v4ldevices", V4L_LOCATION, "Location",0);

	g_signal_connect( tree, "row-activated",
		(GCallback) on_devicelist_row_activated, NULL );


	

	//scan_devices( "tree_v4ldevices" );
	

}

#define SAMPLE_MAX_PARAMETERS 32

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
	int defaults[SAMPLE_MAX_PARAMETERS];
	int min[SAMPLE_MAX_PARAMETERS];
	int max[SAMPLE_MAX_PARAMETERS];
	char description[150];
	char *param_description[SAMPLE_MAX_PARAMETERS];
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

char *_effect_get_param_description(int effect_id, int param)
{
	int n = g_list_length( info->effect_info );
	int i;
	for(i = 0;i <= n ; i++)
	{	
		effect_constr *ec = g_list_nth_data(info->effect_info, i);
		if(ec != NULL)
		{
			if(effect_id == ec->id )
				return ec->param_description[param];
		}
	}
	return FX_PARAMETER_DEFAULT_NAME;
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
	return FX_PARAMETER_DEFAULT_NAME;
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
	el_ref *el = vj_malloc(sizeof(el_ref));
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
		int n = g_list_length( info->elref );
		int i;
		for(i = 0; i < n; i ++ )
		{
			el_ref *edl = g_list_nth_data(info->elref, i );
			if(edl)
				free(edl);
		}	
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
	char len[4];
	//char line[100];
	int offset = 0;

	veejay_memset(len,0,sizeof(len));

	if(!effect_line) return NULL;

	strncpy(len, effect_line, 3);
	sscanf(len, "%03d", &descr_len);
	if(descr_len <= 0) return NULL;

	ec = vj_calloc( sizeof(effect_constr));
	strncpy( ec->description, effect_line+3, descr_len );
	sscanf(effect_line+(descr_len+3), "%03d%1d%1d%02d", &(ec->id),&(ec->is_video),&(ec->has_rgb), &(ec->num_arg));
	offset = descr_len + 10;
	for(p=0; p < ec->num_arg; p++)
	{
		int len = 0;
		int n = sscanf(effect_line+offset,"%06d%06d%06d%03d",
			&(ec->min[p]), &(ec->max[p]),&(ec->defaults[p]),&len );
		if( n <= 0 )
		{
			veejay_msg(0,"Parse error in FX list" );	       
			break;
		}
		ec->param_description[p] = (char*) vj_calloc(sizeof(char) * (len+1) );
		strncpy( ec->param_description[p], effect_line + offset + 6 + 6 + 6 + 3, len );
		offset += 3;
		offset += len;
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
				GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
				NULL);
#ifdef HAVE_GTK2_8
	gtk_file_chooser_set_do_overwrite_confirmation( GTK_FILE_CHOOSER(dialog), TRUE );
#endif
	gtk_file_chooser_set_filename( GTK_FILE_CHOOSER(dialog), "veejay-samplelist.sl" );

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
				GTK_WINDOW(parent_window),
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
		_file_path = NULL;
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

static	char *	produce_os_str()
{
	char os_str[512];
	char cpu_type[32];
	char *simd = vj_calloc( 128 );
#ifdef ARCH_X86
	sprintf(cpu_type,"x86");
#endif
#ifdef ARCH_X86_64
	sprintf(cpu_type, "x86-64");
#endif
#ifdef ARCH_PPC
	sprintf(cpu_type, "ppc");
#endif
#ifdef ARCH_MIPS
	sprintf(cpu_type, "mips");
#endif
#ifdef HAVE_ASM_MMX
	strcat( simd, "MMX ");
#endif
#ifdef HAVE_ASM_MMX2
	strcat( simd, "MMX2 ");
#endif
#ifdef HAVE_ASM_SSE
	strcat( simd, "SSE " );
#endif
#ifdef HAVE_ASM_SSE2
	strcat( simd, "SSE2" );
#endif
#ifdef HAVE_ASM_CMOV
	strcat( simd, "cmov" );
#endif
#ifdef HAVE_ASM_3DNOW
	strcat( simd, "3DNow");
#endif
#ifdef ARCH_PPC
#ifdef HAVE_ALTIVEC
	strcat( simd, "altivec");
#else
	strcat( simd, "no optimizations");
#endif
#endif
#ifdef ARCH_MIPS
	strcat( simd, "no optimizations");
#endif
	sprintf(os_str,"Arch: %s with %s",
		cpu_type, simd );

	return strdup( os_str );
}

void	about_dialog()
{
    const gchar *artists[] = { 
      "Matthijs v. Henten (glade, pixmaps) <matthijs.vanhenten@gmail.com>", 
      "Dursun Koca (V-logo)",
      NULL 
    };

    const gchar *authors[] = { 
        "Developed by:",
	"Matthijs v. Henten <matthijs.vanhenten@gmail.com>",
	"Dursun Koca",
	"Niels Elburg <nwelburg@gmail.com>",
	"\n",
	"Contributions by:",
	"Thomas Reinhold <stan@jf-chemnitz.de>",
	"Toni <oc2pus@arcor.de>",
	"d/j/a/y <d.j.a.y@free.fr>",
      	NULL 
    };

	const gchar *web = {
		"http://www.veejayhq.net" 
	};

	char blob[1024];
	char *os_str = produce_os_str();
	const gchar *donate =
{
	"You can donate cryptocoins!\n"\
	"Bitcoin: 1PUNRsv8vDt1upTx9tTpY5sH8mHW1DTrKJ\n"
	"or via PayPal: veejayhq@gmail.com\n"
};

	sprintf(blob, "Veejay - A visual instrument and realtime video sampler for GNU/Linux\n%s\n%s", os_str, donate );
	
	free(os_str);



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
	veejay_memset( path,0, sizeof(path));
	get_gd( path, NULL,  "veejay-logo.png" );
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file
		( path, NULL );
	GtkWidget *about = g_object_new(
		GTK_TYPE_ABOUT_DIALOG,
		"program_name", "reloaded",   
		"name", VEEJAY_CODENAME,
		"version", VERSION,
		"copyright", "(C) 2004 - 2015 N. Elburg et all.",
		"comments", "The graphical interface for Veejay",
		"website", web,
		"authors", authors,
		"artists", artists,
		"comments", blob,
		"license", license,
		"logo", pixbuf, NULL );
	g_object_unref(pixbuf);

	g_signal_connect( about , "response", G_CALLBACK( gtk_widget_destroy),NULL);
	gtk_window_present( GTK_WINDOW( about ) );
#endif

}

gboolean	dialogkey_snooper( GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
	GtkWidget *entry = (GtkWidget*) user_data;

	if( !gtk_widget_is_focus( entry ) )
	{
		return FALSE;
	}
#ifdef HAVE_SDL
	if(event->type == GDK_KEY_PRESS)
	{
		gchar tmp[100];
		info->uc.pressed_key = gdk2sdl_key( event->keyval );
		info->uc.pressed_mod = gdk2sdl_mod( event->state );
		gchar *text = gdkkey_by_id( event->keyval );
		gchar *mod  = gdkmod_by_id( event->state );

		if( text )
		{
			if(!mod || strncmp(mod, " ", 1 ) == 0 )
				snprintf(tmp, sizeof(tmp),"%s", text );
			else
				snprintf(tmp, sizeof(tmp), "%s + %s", mod,text);

			gchar *utf8_text = _utf8str( tmp );
			gtk_entry_set_text( GTK_ENTRY(entry), utf8_text);
			g_free(utf8_text);
		}
	}
#endif
	return FALSE;
}
#ifdef HAVE_SDL
static gboolean	key_handler( GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
	if(event->type != GDK_KEY_PRESS)
		return FALSE;

	int gdk_keyval = gdk2sdl_key( event->keyval );
	int gdk_state  = gdk2sdl_mod( event->state );
	if( gdk_keyval >= 0 && gdk_state >= 0 )
	{
		char *message = vims_keys_list[(gdk_state * G_MOD_OFFSET)+gdk_keyval].vims;
		if(message)
			msg_vims(message);	
	}
	return FALSE;
}
#endif
static int	check_format_string( char *args, char *format )
{
	if(!format || !args )
		return 0;
	char dirty[128];
	int n = sscanf( args, format, &dirty,&dirty, &dirty,&dirty, &dirty,&dirty, &dirty,&dirty, &dirty,&dirty );
	return n;
}

int
prompt_keydialog(const char *title, char *msg)
{
	if(!info->uc.selected_vims_entry )
		return 0;
	info->uc.pressed_mod = 0;
	info->uc.pressed_key = 0; 

	char pixmap[1024];
	veejay_memset(pixmap,0,sizeof(pixmap));
	get_gd( pixmap, NULL, "icon_keybind.png");

	GtkWidget *mainw = glade_xml_get_widget_(info->main_window, "gveejay_window");
	GtkWidget *dialog = gtk_dialog_new_with_buttons( title,
				GTK_WINDOW( mainw ),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_STOCK_CANCEL,
				GTK_RESPONSE_REJECT,
				GTK_STOCK_OK,	
				GTK_RESPONSE_ACCEPT,
				NULL);

	GtkWidget *keyentry = gtk_entry_new();
	gtk_entry_set_text( GTK_ENTRY(keyentry), "<press any key>");
	gtk_editable_set_editable( GTK_EDITABLE(keyentry), FALSE );  
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
	
	GtkWidget *pentry = NULL;

	if(vj_event_list[ info->uc.selected_vims_entry ].params)
	{
		//@ put in default args
		char *arg_str = vj_event_list[ info->uc.selected_vims_entry ].args;
		pentry = gtk_entry_new();
		GtkWidget *arglabel = gtk_label_new("Arguments:");
				
		if(arg_str)
			gtk_entry_set_text( GTK_ENTRY(pentry), arg_str );
		gtk_editable_set_editable( GTK_EDITABLE(pentry), TRUE );
		gtk_container_add( GTK_CONTAINER(hbox1), arglabel );
		gtk_container_add( GTK_CONTAINER(hbox1), pentry );
	} 
#ifdef HAVE_SDL
	if( info->uc.selected_vims_entry  )
	{
		char tmp[100];
		char *str_mod = sdlmod_by_id( info->uc.pressed_mod );
		char *str_key = sdlkey_by_id( info->uc.pressed_key );
		int key_combo_ok = 0;

		if(str_mod && str_key ) {
			snprintf(tmp,100,"VIMS %d : %s + %s",
				info->uc.selected_vims_entry, str_mod, str_key );
			key_combo_ok = 1;
		 } else if ( str_key ) {
			snprintf(tmp, 100,"VIMS %d: %s", info->uc.selected_vims_entry,str_key);
			key_combo_ok = 1;
		} 

		if( key_combo_ok )
		{
			gtk_entry_set_text( GTK_ENTRY(keyentry), tmp );
		}
	}
#endif

	gtk_container_add( GTK_CONTAINER( GTK_DIALOG( dialog )->vbox ), hbox1 );
	gtk_container_add( GTK_CONTAINER( GTK_DIALOG( dialog )->vbox ), hbox2 );

	gtk_widget_show_all( dialog );

	int id = gtk_key_snooper_install( dialogkey_snooper, keyentry);
	int n = gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_key_snooper_remove( id );

	if(pentry)
	{
		gchar *args =  (gchar*) gtk_entry_get_text( GTK_ENTRY(pentry));
		int np = check_format_string( args, vj_event_list[ info->uc.selected_vims_entry  ].format );
		
		if( np == vj_event_list[ info->uc.selected_vims_entry ].params )
		{
			if(info->uc.selected_vims_args )
				free(info->uc.selected_vims_args );

			info->uc.selected_vims_args = strdup( args );	
		}
	}

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


int
error_dialog(const char *title, char *msg)
{
	GtkWidget *mainw = glade_xml_get_widget_(info->main_window, "gveejay_window");
	GtkWidget *dialog = gtk_dialog_new_with_buttons( title,
				GTK_WINDOW( mainw ),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_STOCK_OK,	
				GTK_RESPONSE_OK,
				NULL);
	gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK );
	gtk_window_set_resizable( GTK_WINDOW(dialog), FALSE );
	g_signal_connect( G_OBJECT(dialog), "response", 
		G_CALLBACK( gtk_widget_hide ), G_OBJECT(dialog ) );
	GtkWidget *hbox1 = gtk_hbox_new( FALSE, 12 );
	gtk_container_set_border_width( GTK_CONTAINER( hbox1 ), 6 );
	GtkWidget *icon = gtk_image_new_from_stock( GTK_STOCK_DIALOG_ERROR,
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

//	clear_progress_bar( "cpumeter",0.0 );
	clear_progress_bar( "connecting",0.0 );
	clear_progress_bar( "samplerecord_progress",0.0 );
	clear_progress_bar( "streamrecord_progress",0.0 );
	clear_progress_bar( "seq_rec_progress",0.0);
	exit(0);
}

static	int	running_g_ = 1;
static  int	restart_   = 0;

int		gveejay_restart()
{
	return restart_;
}

gboolean	gveejay_running()
{
	if(!running_g_)
		return FALSE;
	return TRUE;
}

gboolean	gveejay_quit( GtkWidget *widget, gpointer user_data)
{
	if(!running_g_)
		return FALSE;

	if( info->watch.state == STATE_PLAYING)
	{
		if( prompt_dialog("Quit Reloaded", "Are you sure?" ) == GTK_RESPONSE_REJECT)
			return TRUE;
	}
	
	running_g_ = 0;
	info->watch.state = STATE_QUIT;

	return FALSE;
}

/* Free the slot */
static	void	free_slot( sample_slot_t *slot )
{
	if(slot)
	{
		if(slot->title) free(slot->title);
		if(slot->timecode) free(slot->timecode);
		free(slot);
	}
	slot = NULL;
}

/* Allocate some memory and create a temporary slot */
sample_slot_t 	*create_temporary_slot( gint slot_id, gint id, gint type, gchar *title, gchar *timecode )
{
	sample_slot_t *slot = (sample_slot_t*) vj_calloc(sizeof(sample_slot_t));
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
/*	int id = 0;
	int result_len = 0;
	gchar *result = recv_vims( 3, &result_len );

	veejay_msg(0 ,"  -> EXPECT NEW SLOT in [%s]" ,result );

	if(result_len <= 0 )
	{
		veejay_msg(0, "Maybe you should restart me");
		return 0;
	}

	sscanf( result, "%d", &id );

	free(result);

	if( id <= 0 )
	{
	//	gveejay_error_slot( mode );
		return 0;
	}
*/
	return 1;
}

void	gveejay_popup_err( const char *type, char *msg )
{
	message_dialog( type, msg );
}
void	donatenow();
void	reportbug();
void	update_gui();

#include "callback.c"
enum
{
	COLOR_RED=0,
	COLOR_BLUE=1,
	COLOR_GREEN=2,
	COLOR_BLACK=3,
	COLOR_NUM
};

void	vj_msg(int type, const char format[], ...)
{
	if( type == VEEJAY_MSG_DEBUG && vims_verbosity == 0 )
		return;
	
	char tmp[1024];
	char buf[1024];
	char prefix[20];
	va_list args;

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

	GtkWidget *sb = glade_xml_get_widget_( info->main_window, "statusbar");
	gtk_statusbar_push( GTK_STATUSBAR(sb),0, text ); 

	g_free( text );
	va_end(args);
}

void	msg_vims(char *message)
{
	if(!info->client)
		return;
	int n = vj_client_send(info->client, V_CMD, (unsigned char*)message);
	if( n <= 0 )
		reloaded_schedule_restart();
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
			else if (is_button_toggled("loop_random"))
				return 3;
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

	if(vj_client_send( info->client, V_CMD, (unsigned char*) block)<=0 )
		reloaded_schedule_restart();
}

static	void single_vims(int id)
{
	char block[10];
	if(!info->client)
		return;
	sprintf(block, "%03d:;",id);
	if(vj_client_send( info->client, V_CMD, (unsigned char*) block)<=0 )
		reloaded_schedule_restart();

}


static gchar	*recv_vims(int slen, int *bytes_written)
{
	int tmp_len = slen+1;
	unsigned char tmp[tmp_len];
	veejay_memset(tmp,0,sizeof(tmp));
	int ret = vj_client_read( info->client, V_CMD, tmp, slen );
	if( ret == -1 )
		reloaded_schedule_restart();
	int len = 0;
	if( sscanf( (char*)tmp, "%d", &len ) != 1 )
		return NULL;
	unsigned char *result = NULL;
	if( ret <= 0 || len <= 0 || slen <= 0)
		return (gchar*)result;
	result = (unsigned char*) vj_calloc(sizeof(unsigned char) * (len + 1) );
	*bytes_written = vj_client_read( info->client, V_CMD, result, len );
	if( *bytes_written == -1 )
		reloaded_schedule_restart();
	return (gchar*) result;
}

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

static	void	vj_kf_refresh()
{
	GtkWidget *curve = glade_xml_get_widget_(info->main_window, "curve");

	reset_curve( curve );

	update_curve_accessibility("curve");
	update_curve_widget("curve");
	int	*entry_tokens = &(info->uc.entry_tokens[0]);

	gchar *name = _utf8str(_effect_get_param_description(entry_tokens[ENTRY_FXID],info->uc.selected_parameter_id));
	update_label_str( "curve_parameter", name );
	g_free(name);
}

static	void	vj_kf_select_parameter(int num)
{
	sample_slot_t *s = info->selected_slot;
	if(!s) 
	{
		update_label_str( "curve_parameter", FX_PARAMETER_DEFAULT_NAME);
		return;
	}

	info->uc.selected_parameter_id = num;

	vj_kf_refresh();
}

static  void	update_curve_widget(const char *name)
{
	GtkWidget *curve = glade_xml_get_widget_( info->main_window,name);
	sample_slot_t *s = info->selected_slot;
	if(!s ) 	return;
	int i = info->uc.selected_chain_entry; /* chain entry */
	int id = info->uc.entry_tokens[ENTRY_FXID];
	int blen = 0;
	int lo = 0, hi = 0, curve_type=0;
	int p = -1;
	multi_vims( VIMS_SAMPLE_KF_GET, "%d %d",i,info->uc.selected_parameter_id );

	unsigned char *blob = (unsigned char*) recv_vims( 8, &blen );
	if( blob && blen > 0 )
	{
		p = set_points_in_curve_ext( curve, blob,id,i, &lo,&hi, &curve_type );
		if( p >= 0 )
		{
			char but[25];
			sprintf(but, "kf_p%d", p);
			set_toggle_button( but, 1 );
			info->uc.selected_parameter_id = p;
			switch( curve_type ) {
				case GTK_CURVE_TYPE_SPLINE: set_toggle_button( "curve_typespline", 1 );break;
				case GTK_CURVE_TYPE_FREE: set_toggle_button( "curve_typefree",1 ); break;
				default: set_toggle_button( "curve_typelinear", 1 ); break;
			}
		}
	}

	if( lo == hi && hi == 0 )
	{
		if( info->status_tokens[PLAY_MODE] == MODE_SAMPLE )
		{
			lo = info->status_tokens[SAMPLE_START];
			hi = info->status_tokens[SAMPLE_END];
		}
		else
		{
			lo = 0;
			hi = info->status_tokens[SAMPLE_MARKER_END]; 
		}
	}
	update_spin_range( "curve_spinstart", lo, hi, lo );
	update_spin_range( "curve_spinend", lo, hi, hi );


	if(blob)	free(blob);
}

static	void	update_curve_accessibility(const char *name)
{
	sample_slot_t *s = info->selected_slot;
	if(!s ) return;

	if( info->status_tokens[PLAY_MODE] == MODE_PLAIN )
	{
		disable_widget( "frame_fxtree3" );
	}
	else
	{
		enable_widget( "frame_fxtree3" );
	}	
}

static	int	get_nums(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name);
	if(!w) {
		veejay_msg(0, "No such widget (spin): '%s'",name);
		return 0;
	}
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
	if(!view) {
		veejay_msg(0, "No such widget (textview): '%s'",name);
		return;
	}
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
	if(!view) {
		veejay_msg(0, "No such widget (textview): '%s'",name);
		return NULL;
	}
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
	if(!view) {
		veejay_msg(0, "No such widget (textview): '%s'",name);
		return;
	}
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
	if(!w) {
		veejay_msg(0, "No such widget (text): '%s'",name);
		return NULL;
	}
	return (gchar*) gtk_entry_get_text( GTK_ENTRY(w));
}

static	void	put_text(const char *name, char *text)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
	if(!w) {
		veejay_msg(0, "No such widget (text): '%s'",name);
		return;
	}
	if(w)
	{
		gchar *utf8_text = _utf8str( text );
		gtk_entry_set_text( GTK_ENTRY(w), utf8_text );
		g_free(utf8_text);
	}
}

int	is_button_toggled(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name);
	if(!w) {
		veejay_msg(0, "No such widget (togglebutton): '%s'",name);
		return 0;
	}

	if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w) ) == TRUE )
		return 1;
	return 0;
}
static	void	set_toggle_button(const char *name, int status)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
	if(!w) {
		veejay_msg(0, "No such widget (togglebutton): '%s'",name);
		return;
	}
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), (status==1 ? TRUE: FALSE));
	
}


static	void	update_slider_gvalue(const char *name, gdouble value)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) {
		veejay_msg(0, "No such widget (slider): '%s'",name);
		return;
	}
	gtk_adjustment_set_value(
		GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment), value );	
}

static	void	update_slider_value(const char *name, gint value, gint scale)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) {
		veejay_msg(0, "No such widget (slider): '%s'",name);
		return;
	}
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
	if(!w) {
		veejay_msg(0, "No such widget (spin): '%s'",name);
		return;
	}
#ifdef STRICT_CHECKING
	veejay_msg(VEEJAY_MSG_DEBUG, "SpinButton: %s, step=%g,page=%g",name,step,page);
#endif
	gtk_spin_button_set_increments(GTK_SPIN_BUTTON(w),step,page );
}

static	void	update_spin_range(const char *name, gint min, gint max, gint val)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) {
		veejay_msg(0, "No such widget (spin): '%s'",name);
		return;
	}

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
	if(!w) {
		veejay_msg(0, "No such widget (spin): '%s'",name);
		return;
	}
#ifdef STRICT_CHECKING
	veejay_msg(VEEJAY_MSG_DEBUG, "SpinButton: %s, value=%d",name,value);
#endif

	gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), (gdouble) value );
}

static  void	update_slider_range(const char *name, gint min, gint max, gint value, gint scaled)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) {
		veejay_msg(0, "No such widget (slider): '%s'",name);
		return;
	}
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
	if(!label) {
		veejay_msg(0, "No such widget (label): '%s'",name);
		return;
	}
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
	if(!label) {
		veejay_msg(0, "No such widget (label): '%s'",name);
		return;
	}
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
#ifdef STRICT_CHECKING
	if(!label) veejay_msg(0, "No such widget (label): '%s'",name);
	assert( label != NULL );
#else
	if(!label ||!text) return;
#endif
	gchar *utf8_text = _utf8str( text );
	if(!utf8_text) return;
	gtk_label_set_text( GTK_LABEL(label), utf8_text);
	g_free(utf8_text);
}	

static	void 	label_set_markup(const char *name, gchar *str)
{
	GtkWidget *label = glade_xml_get_widget_(
				info->main_window, name);
	if(!label)
		return;

	gtk_label_set_markup( GTK_LABEL(label), str );
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


		color.red = 255 * p[ENTRY_P0];
		color.green = 255 * p[ENTRY_P1];
		color.blue = 255 * p[ENTRY_P2];

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

int		update_gveejay()
{
	return vj_midi_handle_events( info->midi );
}

static  GdkPixbuf	*update_pixmap_kf( int status )
{
	char path[MAX_PATH_LEN];
	char filename[MAX_PATH_LEN];
	veejay_memset( filename, 0,sizeof(filename));

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
	veejay_memset( filename,0,sizeof(filename));

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
	if(!gui->selected_slot)
		return FALSE;
	int entry = info->uc.selected_chain_entry;
	gint gentry = 0;
	gtk_tree_model_get (model, iter,
                        FXC_ID, &gentry, -1);

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
			int on = gui->uc.entry_tokens[ENTRY_VIDEO_ENABLED];
			GdkPixbuf *toggle = update_pixmap_entry( gui->uc.entry_tokens[ENTRY_VIDEO_ENABLED] );
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

/* Cut from global_info()
   This function updates the sample/stream editor if the current playing stream/sample
   matches with the selected sample slot */

static	void	update_record_tab(int pm)
{
	if(pm == MODE_STREAM)
	{
		update_spin_value( "spin_streamduration" , 1 );
		gint n_frames = get_nums( "spin_streamduration" );
		char *time = format_time(n_frames, (double) info->el.fps);
		update_label_str( "label_streamrecord_duration", time );
		free(time);
	}
	if(pm == MODE_SAMPLE)
	{
		update_spin_value( "spin_sampleduration", 1 );
		// combo_samplecodec
		gint n_frames = sample_calctime();
		char *time = format_time( n_frames,(double) info->el.fps );
		update_label_str( "label_samplerecord_duration", time );
		free(time);
	}
}

static void	update_current_slot(int *history, int pm, int last_pm)
{
	gint update = 0;

	if( pm != last_pm || info->status_tokens[CURRENT_ID] != history[CURRENT_ID] )
	{
		int k;
		info->uc.reload_hint[HINT_ENTRY] = 1;
		info->uc.reload_hint[HINT_CHAIN] = 1;
		info->uc.reload_hint[HINT_KF] = 1;
		update = 1;
		update_record_tab( pm );

		if( info->status_tokens[STREAM_TYPE] == STREAM_WHITE ||
		 	info->status_tokens[STREAM_TYPE] == STREAM_GENERATOR )
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
			for(k = 1; capt_card_set[k].name != NULL; k ++ )
				enable_widget( capt_card_set[k].name );
			v4l_expander_toggle(1);
		}
		else
		{ /* not v4l, disable capt card */
			for(k = 1; capt_card_set[k].name != NULL ; k ++ )
				disable_widget( capt_card_set[k].name );

			v4l_expander_toggle(0);
		}

		info->uc.reload_hint[HINT_HISTORY] = 1;

		put_text( "entry_samplename", "" );
		set_pm_page_label( info->status_tokens[CURRENT_ID], pm );

		//HERE

	}
	if( info->status_tokens[CURRENT_ENTRY] != history[CURRENT_ENTRY] ||
		info->uc.reload_hint[HINT_ENTRY] == 1 )
	{
		info->uc.selected_chain_entry = info->status_tokens[CURRENT_ENTRY];
		if(info->uc.selected_chain_entry < 0 || info->uc.selected_chain_entry >= MAX_CHAIN_LEN  )
			info->uc.selected_chain_entry = 0;
		info->uc.reload_hint[HINT_ENTRY] = 1;
		load_parameter_info();
		info->uc.reload_hint[HINT_KF]  = 1;
	}

	/* Actions for stream */
	if( ( info->status_tokens[CURRENT_ID] != history[CURRENT_ID] || pm != last_pm ) && pm == MODE_STREAM )
	{
		/* Is a solid color stream */	
		if( info->status_tokens[STREAM_TYPE] == STREAM_WHITE  ||
			info->status_tokens[STREAM_TYPE] == STREAM_GENERATOR)
		{
			if( ( history[STREAM_COL_R] != info->status_tokens[STREAM_COL_R] ) ||
			    ( history[STREAM_COL_G] != info->status_tokens[STREAM_COL_G] ) ||
		 	    ( history[STREAM_COL_B] != info->status_tokens[STREAM_COL_B] ) )
			 {
				info->uc.reload_hint[HINT_RGBSOLID] = 1;
			 }

		}

		char *time = format_time( info->status_frame,(double)info->el.fps );
		update_label_str( "label_curtime", time );
		free(time); 

		update_label_str( "playhint", "Streaming");
	}

	/* Actions for sample */
	if( ( info->status_tokens[CURRENT_ID] != history[CURRENT_ID] || last_pm != pm) && pm == MODE_SAMPLE )
	{
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
			char *dur = format_time( info->status_tokens[SAMPLE_MARKER_END] - info->status_tokens[SAMPLE_MARKER_START],
				(double)info->el.fps );
			update_label_str( "label_markerduration", dur );
			free(dur);
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
	//		update_spin_value( "spin_samplestart", info->status_tokens[SAMPLE_START] );
			update = 1;
		}
		if( (history[SAMPLE_END] != info->status_tokens[SAMPLE_END] ))
		{
	//		update_spin_value( "spin_sampleend", info->status_tokens[SAMPLE_END]);
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
				case 3:
					set_toggle_button("loop_random", 1 );
				break;
			}	
		}
	
		gint speed = info->status_tokens[SAMPLE_SPEED];

	
		if( history[SAMPLE_SPEED] != info->status_tokens[SAMPLE_SPEED] )
		{	
			speed = info->status_tokens[SAMPLE_SPEED];
			update_slider_value( "speed_slider", speed, 0 );

			if( speed < 0 ) info->play_direction = -1; else info->play_direction = 1;
			if( speed < 0 ) speed *= -1;
			update_spin_value( "spin_samplespeed", speed);

			if( pm == MODE_SAMPLE ) {
				if( speed == 0 ) 	
					update_label_str( "playhint", "Paused" );
				else
					update_label_str( "playhint", "Playing");
			}
		}

		if( history[FRAME_DUP] != info->status_tokens[FRAME_DUP] )
		{
			update_spin_value( "spin_framedelay", info->status_tokens[FRAME_DUP]);
			update_slider_value("slow_slider", info->status_tokens[FRAME_DUP],0);
		}


		if(update)
		{
			speed = info->status_tokens[SAMPLE_SPEED];
			if(speed < 0 ) info->play_direction = -1; else info->play_direction = 1;
	
			gint len = info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START];
	
			int speed = info->status_tokens[SAMPLE_SPEED];
			if(speed < 0 ) info->play_direction = -1; else info->play_direction = 1;
			if(speed < 0 ) speed *= -1;
		
			update_spin_range( "spin_samplespeed", -1 * len, len, speed );
			
			update_spin_value( "spin_samplestart", info->status_tokens[SAMPLE_START]);
			update_spin_value( "spin_sampleend", info->status_tokens[SAMPLE_END]);
		
			gint n_frames = sample_calctime();
	
			timeline_set_length( info->tl,
				(gdouble) n_frames , info->status_tokens[FRAME_NUM]- info->status_tokens[SAMPLE_START] );


			update_spin_range( "spin_text_start", 0, n_frames ,0);
			update_spin_range( "spin_text_end", 0, n_frames,n_frames );

			info->uc.reload_hint[HINT_KF] = 1;
		}
	}


	if( pm == MODE_SAMPLE|| pm == MODE_STREAM )
	if( history[CHAIN_FADE] != info->status_tokens[CHAIN_FADE] )
	{
		double val = (double) info->status_tokens[CHAIN_FADE];	
		update_slider_value( "manualopacity", val,0 );
	}

}


static void
on_vims_messenger       (void)
{
        GtkTextIter start, end;
        GtkTextBuffer* buffer;
        GtkTextView* t= NULL;
        gchar *str = NULL;
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

static int total_frames_ = 0;

int	get_total_frames()
{
	return total_frames_;
}
/*
static char *bugbuffer_ = NULL;
static int   bugoffset_ = 0;

gboolean	capture_data	(GIOChannel *source, GIOCondition condition, gpointer data )
{
	int fd = g_io_channel_unix_get_fd( source );
	GIOStatus ret;
        GError *err = NULL;
        gchar *msg;
        gsize len;

        if (condition & G_IO_HUP)
                g_error ("Read end of pipe died!\n");

        ret = g_io_channel_read_line (source, &msg, &len, NULL, &err);
        if (ret == G_IO_STATUS_ERROR)
                g_error ("Error reading: %s\n", err->message);

	memcpy( bugbuffer_ + (sizeof(char) * bugoffset_) , msg , len );
	
	bugoffset_ += len;

        g_free (msg);
	return TRUE;
}
*/
void	reportbug()
{
	char l[3] = { 'e','n', '\0'};
	char *lang = getenv("LANG");
	char URL[1024];

	if(lang) {
		l[0] = lang[0];
		l[1] = lang[1];
	}	
/*	char veejay_homedir[1024];
	char body[1024];
	char subj[100];
	gchar **argv = (gchar**) malloc ( sizeof(gchar*) * 5 );
	int i;
	argv[0] = malloc( sizeof(char) * 100 );
	memset( argv[0], 0, sizeof(char) * 100 );
	argv[2] = NULL;

//	snprintf(subj,sizeof(subj),"reloaded %s has a problem", VERSION);
	snprintf(veejay_homedir, sizeof(veejay_homedir),"%s/.veejay/", home );
	sprintf(argv[0], "%s/report_problem.sh" ,veejay_homedir);
	argv[1] = strdup( veejay_homedir );

	if( bugoffset_ > 0 ) 	{
		free(bugbuffer_);
		bugoffset_= 0;
		bugbuffer_ = NULL;
	}

//	GError		error = NULL;
	gint		stdout_pipe = 0;
	gint		pid =0;
	gboolean	ret = 	g_spawn_async_with_pipes( 
					NULL,
					argv,
					NULL,
					 G_SPAWN_LEAVE_DESCRIPTORS_OPEN & G_SPAWN_STDERR_TO_DEV_NULL,
					NULL,
					NULL,
					&pid,
					NULL,
					&stdout_pipe,
					NULL,
					NULL );
	if( !ret ) {
		veejay_msg(0, "Error executing bug report tool");
		return;
	}

	GIOChannel	*chan	= g_io_channel_unix_new( stdout_pipe );
	bugbuffer_ = (char*) malloc(sizeof(char) * 32000 );
	memset(bugbuffer_, 0, sizeof(char) * 32000);
	guint	retb = g_io_add_watch( chan, G_IO_IN, capture_data, NULL );
*/
//	if( prompt_dialog("Report a problem", "" )
//		 == GTK_RESPONSE_ACCEPT )
	snprintf(URL , sizeof(URL),	
		"firefox \"http://groups.google.com/group/veejay-discussion/post?hl=%s\"",l );

	printf(URL);

	system(URL);
}

void	donatenow()
{
	char URL[512];
	snprintf(URL , sizeof(URL),	
	 "firefox \"http://www.veejayhq.net/contributing\"" );
	printf(URL);

	system(URL);
}

static	void	reset_tree(const char *name)
{
	GtkWidget *tree_widget = glade_xml_get_widget_( info->main_window,name );
	GtkTreeModel *tree_model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree_widget) );
	
	gtk_list_store_clear( GTK_LIST_STORE( tree_model ) );
	
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
	vj_midi_learning_vims_msg( info->midi, NULL, VIMS_CHAIN_SET_ENTRY, info->uc.selected_chain_entry );
      }
    }

    return TRUE; /* allow selection state to change */
  }

gboolean
  cali_sources_selection_func (GtkTreeSelection *selection,
                       GtkTreeModel     *model,
                       GtkTreePath      *path,
                       gboolean          path_currently_selected,
                       gpointer          userdata)
  {
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
      gchar *name = NULL;

	if( info->uc.cali_stage != 0 ) {
		veejay_msg(0, "%d", info->uc.cali_stage);
		return TRUE;
	}

      gtk_tree_model_get(model, &iter, FXC_ID, &name, -1);

      if (!path_currently_selected)
      {
	gint id = 0;
	sscanf(name+1, "[ %d]", &id);
	if(name[0] != 'S')
	{
		cali_stream_id = id;
		update_label_str("current_step_label","Please take an image with the cap on the lens.");
		GtkWidget *nb = glade_xml_get_widget_(info->main_window, "cali_notebook");
		gtk_notebook_next_page( GTK_NOTEBOOK(nb));
	}
	if(name) g_free(name);
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
	sscanf(name+1, "[ %d]", &id);
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
        gtk_tree_model_get_value(model, iter, V4L_SPINBOX, &val);
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

static	void	setup_tree_text_column( const char *tree_name, int type, const char *title,int len )
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, tree_name );
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes( title, renderer, "text", type, NULL );
	gtk_tree_view_append_column( GTK_TREE_VIEW( tree ), column );

	if(len)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Tree %s ,Title %s, width=%d", tree_name,title, len );
		gtk_tree_view_column_set_min_width( column, len);
	}
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
void
 server_files_selection_func (GtkTreeView *treeview,
                GtkTreePath *path,
                GtkTreeViewColumn *col,
                gpointer user_data)
{
        GtkTreeModel *model;
        GtkTreeIter iter;
        model = gtk_tree_view_get_model(treeview);

        if(gtk_tree_model_get_iter(model,&iter,path))
    {
      gchar *name = NULL;
      gtk_tree_model_get(model, &iter, 0, &name, -1);

	multi_vims(VIMS_EDITLIST_ADD_SAMPLE, "0 %s" , name );
	vj_msg(VEEJAY_MSG_INFO, "Tried to open %s",name);
	gveejay_new_slot(MODE_SAMPLE);       
      g_free(name);

    }

  }
static void 	setup_server_files(void)
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "server_files");
	GtkListStore *store = gtk_list_store_new( 1,  G_TYPE_STRING );
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "server_files", 0, "Filename",0 );
 //  	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE); 
   //	gtk_tree_selection_set_select_function(selection, server_files_selection_func, NULL, NULL);

	g_signal_connect( tree, "row-activated", (GCallback) server_files_selection_func, NULL);

}

static void	setup_effectchain_info( void )
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_chain");
	GtkListStore *store = gtk_list_store_new( 4, G_TYPE_INT, G_TYPE_STRING, GDK_TYPE_PIXBUF,GDK_TYPE_PIXBUF );
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "tree_chain", FXC_ID, "#",0 );
	setup_tree_text_column( "tree_chain", FXC_FXID, "Effect",0 ); //FIXME
	setup_tree_pixmap_column( "tree_chain", FXC_FXSTATUS, "Run"); // todo: could be checkbox!!
	setup_tree_pixmap_column( "tree_chain", FXC_KF , "Anim" ); // parameter interpolation on/off per entry
  	GtkTreeSelection *selection; 

	tree = glade_xml_get_widget_( info->main_window, "tree_chain");
  	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE); 
   	gtk_tree_selection_set_select_function(selection, view_entry_selection_func, NULL, NULL);
}



static	void	load_v4l_info()
{
	int values[6] = { 0 };
	int len = 0;
	multi_vims( VIMS_STREAM_GET_V4L, "%d", (info->selected_slot == NULL ? 0 : info->selected_slot->sample_id ));
	gchar *answer = recv_vims(3, &len);
	if(len > 0 && answer )
	{
		int res = sscanf( answer, "%05d%05d%05d%05d%05d%05d", 
			&values[0],&values[1],&values[2],&values[3],&values[4],&values[5]);
		if(res == 6)
		{
			int i;
			for(i = 1; i < 7; i ++ )
			{
				update_slider_gvalue( capt_card_set[i].name, (gdouble)values[i-1]/65535.0 );
			}	
		}	
		free(answer);
	}
}

static	gint load_parameter_info()
{
	int	*p = &(info->uc.entry_tokens[0]);
	int	len = 0;
	int 	i = 0;

	veejay_memset( p, 0, sizeof(info->uc.entry_tokens));
		
	multi_vims( VIMS_CHAIN_GET_ENTRY, "%d %d", 0, info->uc.selected_chain_entry );

	gchar *answer = recv_vims(3,&len);
	if(len <= 0 || answer == NULL )
	{
		if(answer) free(answer);
		veejay_memset(p,0,sizeof(info->uc.entry_tokens));
		if(info->uc.selected_rgbkey )
			disable_widget("rgbkey");
		return 0;
	}

	char *ptr;
	char *token = strtok_r( answer," ", &ptr );
	if(!token) {
		veejay_msg(0,"Invalid reply from %d", VIMS_CHAIN_GET_ENTRY );
		return 0;
	}
	p[i] = atoi(token);
	while( (token = strtok_r( NULL, " ", &ptr ) ) != NULL )
	{
		i++;
		p[i] = atoi( token );
	}

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
		
	set_toggle_button( "curve_toggleentry", p[ENTRY_KF_STATUS] );

	if(info->status_tokens[PLAY_MODE] == MODE_SAMPLE )
	{
		update_spin_range( "curve_spinstart", 
			info->status_tokens[SAMPLE_START], 
			info->status_tokens[SAMPLE_END], p[ENTRY_KF_START] );
		update_spin_range( "curve_spinend", info->status_tokens[SAMPLE_START],
			info->status_tokens[SAMPLE_END] ,p[ENTRY_KF_END] );
	}
	else
	{
		int nl = info->status_tokens[SAMPLE_MARKER_END];
		update_spin_range( "curve_spinstart", 0, nl, p[ENTRY_KF_START] );
		update_spin_range( "curve_spinend", 0,nl, p[ENTRY_KF_END] );
	}

	update_label_str( "curve_parameter", FX_PARAMETER_DEFAULT_NAME);

	switch( p[ENTRY_KF_TYPE] )
	{
		case 1: set_toggle_button( "curve_typespline", 1 ); break;
		case 2: set_toggle_button( "curve_typefree",1 ); break;
		default:
			case GTK_CURVE_TYPE_LINEAR: set_toggle_button( "curve_typelinear", 1 ); break;
		break;
	}
			
	free(answer);

	return 1;
}	  


// load effect chain
static	void	load_effectchain_info()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_chain");
	GtkListStore *store;
	gchar toggle[4];
	gchar kf_toggle[4];
	guint arr[6];	
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
		veejay_memset(toggle,0,sizeof(toggle));
		veejay_memset(kf_toggle,0,sizeof(kf_toggle));
		veejay_memset(arr,0,sizeof(arr));
		char line[12];
		veejay_memset(line,0,sizeof(line));
		strncpy( line, fxtext + offset, 8 );
		sscanf( line, "%02d%03d%1d%1d%1d",
			&arr[0],&arr[1],&arr[2],&arr[3],&arr[4]);

		char *name = _effect_get_description( arr[1] );
		sprintf(toggle,"%s",arr[3] == 1 ? "on" : "off" );

		while( last_index < arr[0] )
		{
			gtk_list_store_append( store, &iter );
			gtk_list_store_set( store, &iter, FXC_ID, last_index,-1);
			last_index ++;
		}

		if( last_index == arr[0])
		{
			gchar *utf8_name = _utf8str( name );
			int on = info->uc.entry_tokens[ENTRY_VIDEO_ENABLED];
			gtk_list_store_append( store, &iter );
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
	free(fxtext);
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
			
			char trip[100];
			snprintf(trip,sizeof(trip), "%03d:%d %d %d;", VIMS_CHAIN_ENTRY_SET_EFFECT,0,info->uc.selected_chain_entry, gid );
			vj_midi_learning_vims( info->midi, NULL, trip, 0 );
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
			info->uc.reload_hint[HINT_ENTRY] = 1;
			char trip[100];
			snprintf(trip,sizeof(trip), "%03d:%d %d %d;", VIMS_CHAIN_ENTRY_SET_EFFECT,0,info->uc.selected_chain_entry, gid );
			vj_midi_learning_vims( info->midi, NULL, trip, 0 );

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


	setup_tree_text_column( "tree_effectlist", FX_STRING, "Effect",0 );

	setup_tree_text_column( "tree_effectmixlist", FX_STRING, "Effect",0 );

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
		if( sscanf( idstr+1, "[ %d]", &id ) )
		{
		    // set source / channel
		    multi_vims( VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
			"%d %d %d %d",
			0,
			info->uc.selected_chain_entry,
			( idstr[0] == 'T' ? 1 : 0 ),
			id );	
		    vj_msg(VEEJAY_MSG_INFO, "Set source channel to %d, %d", info->uc.selected_chain_entry,id );


			char trip[100];
			snprintf(trip, sizeof(trip), "%03d:%d %d %d %d",VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
				0,
				info->uc.selected_chain_entry,
				( idstr[0] == 'T' ? 1 : 0 ),
				id );	
			vj_midi_learning_vims( info->midi, NULL, trip, 0 );
		}
		if(idstr) g_free(idstr);
	}
}

/* Return a bank page and slot number to place sample in */

int	verify_bank_capacity(int *bank_page_, int *slot_, int sample_id, int sample_type )
{
	int poke_slot = 0;
	int bank_page = find_bank_by_sample( sample_id, sample_type, &poke_slot );

	if(bank_page == -1) {
		veejay_msg(0, "No slot found for (%d,%d)",sample_id,sample_type);
		return 0;
	}

	if( !bank_exists(bank_page, poke_slot))
		add_bank( bank_page );

	*bank_page_ = bank_page;
	*slot_      = poke_slot;

#ifdef STRICT_CHECKING
	veejay_msg(VEEJAY_MSG_DEBUG, "(type=%d,id=%d) needs new slot, suggesting page %d, slot %d",
			sample_type, sample_id, bank_page, poke_slot );

//	if( info->sample_banks[bank_page] )
//		assert( info->sample_banks[bank_page]->slot[poke_slot]->sample_id <= 0 );
	

#endif	

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

	setup_tree_text_column( "tree_sources", SL_ID, "Id",0 );
	setup_tree_text_column( "tree_sources", SL_TIMECODE, "Length" ,0);

	GtkTreeSelection *selection;
  	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(effect_sources_tree));
    	gtk_tree_selection_set_select_function(selection, view_sources_selection_func, NULL, NULL);
    	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

	g_signal_connect( effect_sources_tree, "row-activated", (GCallback) on_effectlist_sources_row_activated, (gpointer*)"tree_sources");



	cali_sourcetree = glade_xml_get_widget_(info->main_window, "cali_sourcetree");
	cali_sourcestore= gtk_list_store_new( 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING );

	gtk_tree_view_set_headers_visible( GTK_TREE_VIEW( cali_sourcetree), FALSE );
	gtk_tree_view_set_model( GTK_TREE_VIEW(cali_sourcetree), GTK_TREE_MODEL(cali_sourcestore));
	g_object_unref( G_OBJECT(cali_sourcestore));

	cali_sourcemodel = gtk_tree_view_get_model( GTK_TREE_VIEW(cali_sourcetree ));	
	cali_sourcestore = GTK_LIST_STORE(cali_sourcemodel);

	setup_tree_text_column( "cali_sourcetree", SL_ID, "Id",0 );
	setup_tree_text_column( "cali_sourcetree", SL_TIMECODE, "Length" ,0);

  	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cali_sourcetree));
    	gtk_tree_selection_set_select_function(sel, cali_sources_selection_func, NULL, NULL);
    	gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);

//	g_signal_connect( cali_sourcetree, "row-activated", (GCallback) on_effectlist_sources_row_activated, (gpointer*)"tree_sources");


}

static	uint8_t *ref_trashcan[3] = { NULL,NULL,NULL };
static  GdkPixbuf *pix_trashcan[3] = { NULL,NULL,NULL };

void	reset_cali_images( int type, char *wid_name )
{
	GtkWidget *dstImage = glade_xml_get_widget( 
				info->main_window, wid_name );

	if( pix_trashcan[type] != NULL ) {
		g_object_unref( pix_trashcan[type] );
		pix_trashcan[type] = NULL;
	}
	if( ref_trashcan[type] != NULL  ) {
		free( ref_trashcan[type] );
		ref_trashcan[type] = NULL;
	}
	gtk_image_clear( GTK_IMAGE(dstImage) );

}

int	get_and_draw_frame(int type, char *wid_name)
{
	GtkWidget *dstImage = glade_xml_get_widget( 
				info->main_window, wid_name );
	if(dstImage == 0 ) {
		veejay_msg(0, "No widget '%s'",wid_name);
		return 0;
	}

	multi_vims( VIMS_CALI_IMAGE, "%d %d", cali_stream_id,type);
	
	int bw = 0;
	gchar *buf = recv_vims( 3, &bw );
	
	if( bw <= 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to get calibration image.");
		return 0;
	}
	
	int len = 0;
	int uvlen = 0;
	int w = 0;
	int h = 0;
	int tlen = 0;
	if( sscanf(buf,"%08d%06d%06d%06d%06d",&tlen, &len, &uvlen,&w,&h) != 5 ) {
		free(buf);
		veejay_msg(0,"Error reading calibration data header" );
		return 0;
	}

	uint8_t *out = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w*h*3));
	uint8_t *srcbuf = (uint8_t*) vj_malloc(sizeof(uint8_t) * len );

	int res = vj_client_read(info->client, V_CMD, srcbuf, tlen );
	if( res <= 0 ) {
		free(out);
		free(srcbuf);
		free(buf);
		veejay_msg(0, "Error while receiving calibration image.");
		return 0;
	}	

	VJFrame *src = yuv_yuv_template( srcbuf,
					 srcbuf,
					 srcbuf,
					 w,
					 h,
					 PIX_FMT_GRAY8 );

	VJFrame *dst = yuv_rgb_template( out, w,h,PIX_FMT_BGR24 );

	yuv_convert_any_ac( src,dst, src->format, dst->format );

	GdkPixbuf *pix = gdk_pixbuf_new_from_data(
				out,
				GDK_COLORSPACE_RGB,
				FALSE,
				8,
				w,
				h,
				w*3,
				NULL,
				NULL );

	if( ref_trashcan[type] != NULL ) {
		free(ref_trashcan[type]);
		ref_trashcan[type]=NULL;
	}
	if( pix_trashcan[type] != NULL ) {
		g_object_unref( pix_trashcan[type] );
		pix_trashcan[type] = NULL;
	}

	gtk_image_set_from_pixbuf_( GTK_IMAGE( dstImage ), pix );
	
//	gdk_pixbuf_unref( pix );

	free(src);
	free(dst);
	free(buf);
//	free(out);
	free(srcbuf);

	ref_trashcan[type] = out;
	pix_trashcan[type] = pix;

	return 1;
}

void	load_effectlist_info()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_effectlist");
	GtkWidget *tree2 = glade_xml_get_widget_( info->main_window, "tree_effectmixlist");
	GtkListStore *store,*store2;
	char line[4096];

	GtkTreeIter iter;
	gint i,offset=0;
	
	
	gint fxlen = 0;
	single_vims( VIMS_EFFECT_LIST );
	gchar *fxtext = recv_vims(6,&fxlen);
	_effect_reset();
 	reset_tree( "tree_effectlist");
	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	GtkTreeModel *model2 = gtk_tree_view_get_model( GTK_TREE_VIEW(tree2));
	store2 = GTK_LIST_STORE(model2);
	while( offset < fxlen )
	{
		char tmp_len[4];
		veejay_memset(tmp_len,0,sizeof(tmp_len));
		strncpy(tmp_len, fxtext + offset, 3 );
		int  len = atoi(tmp_len);
		offset += 3;
		if(len > 0)
		{
			effect_constr *ec;
			veejay_memset( line,0,sizeof(line));
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
				gtk_list_store_set( store2, &iter, FX_STRING, name, -1 );
				vevo_property_set( fx_list_, name, LIVIDO_ATOM_TYPE_INT, 1, &(ec->id));
			}
			else
			{
				gtk_list_store_append( store, &iter );
				gtk_list_store_set( store, &iter, FX_STRING, name, -1 );
				vevo_property_set( fx_list_, name, LIVIDO_ATOM_TYPE_INT, 1, &(ec->id));
			}

		}
		g_free(name);
	}


	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree2), GTK_TREE_MODEL(store2));
	free(fxtext);
}

static	void	select_slot( int pm )
{
	if( pm == MODE_SAMPLE || pm == MODE_STREAM  )
	{
		int b = 0; int p = 0;
		/* falsify activation */
		if(info->status_tokens[CURRENT_ID] > 0)
		{
			if(verify_bank_capacity( &b, &p, info->status_tokens[CURRENT_ID],pm ))
			{	
				if( info->selected_slot ) {			
					if ( info->selected_slot->sample_type != pm || info->selected_slot->sample_id !=
						info->selected_slot->sample_id ) {
					set_activation_of_slot_in_samplebank(FALSE);
					}
				}
				info->selected_slot = info->sample_banks[b]->slot[p];
				info->selected_gui_slot = info->sample_banks[b]->gui_slot[p];
				set_activation_of_slot_in_samplebank(TRUE);
			}
			/*bank_page =  find_bank_by_sample( info->status_tokens[CURRENT_ID], pm, &poke_slot );

			info->selected_slot=  info->sample_banks[bank_page]->slot[poke_slot];
			info->selected_gui_slot= info->sample_banks[bank_page]->gui_slot[poke_slot];*/

		}	
	}
	else {
		set_activation_of_slot_in_samplebank(FALSE);
		info->selected_slot = NULL;
		info->selected_gui_slot = NULL;
	}
}

static	void	load_sequence_list()
{
	single_vims( VIMS_SEQUENCE_LIST );
	gint len = 0;
	gchar *text = recv_vims( 6, &len );
	if( len <= 0 || text == NULL )
		return;

	int playing=0;
	int size =0;
	int active=0;

	sscanf( text, "%04d%04d%4d",&playing,&size,&active );
	int nlen = len - 12;
	int offset = 0;
	int id = 0;
	gchar *in = text + 12;
	while( offset < nlen )
	{
		int sample_id = 0;
		char seqtext[32];
		sscanf( in + offset, "%04d", &sample_id );
		offset += 4;
		if( sample_id > 0 )
		{
			sprintf(seqtext,"%d",sample_id);	
			gtk_label_set_text(
				GTK_LABEL(info->sequencer_view->gui_slot[id]->image),
				seqtext );
		}
		else
		{
			gtk_label_set_text(
					GTK_LABEL(info->sequencer_view->gui_slot[id]->image),
					NULL );
		}
			
		id ++;
	}
	free(text);
}

static	void	load_samplelist_info(gboolean with_reset_slotselection)
{
	gint offset=0;	
	int n_slots = 0;
	reset_tree( "tree_sources" );
	if( cali_onoff == 1 )
		reset_tree( "cali_sourcetree");

	if( with_reset_slotselection ) {
		reset_samplebank();
		}
	char line[300];
	char source[255];
	char descr[255];

	multi_vims( VIMS_SAMPLE_LIST,"%d", 0 );
	gint fxlen = 0;

	gchar *fxtext = recv_vims(8,&fxlen);

	if(fxlen > 0 && fxtext != NULL)
	{
		while( offset < fxlen )
		{
			char tmp_len[8] = { 0 };
			strncpy(tmp_len, fxtext + offset, 3 );
			int  len = atoi(tmp_len);
			offset += 3;
			if(len > 0)
			{
				veejay_memset( line,0,sizeof(line));
				veejay_memset( descr,0,sizeof(descr));
				strncpy( line, fxtext + offset, len );
				
				int values[4] = { 0,0,0,0 };
#ifdef STRICT_CHECKING
				veejay_msg( VEEJAY_MSG_DEBUG, "[%s]", line);
				int res = 	sscanf( line, "%05d%09d%09d%03d",
					&values[0], &values[1], &values[2], &values[3]);
				veejay_msg(VEEJAY_MSG_DEBUG,
						"%d , %d, %d, %d res=%d",values[0],values[1],
								values[2],values[3],res );
				assert( res == 4 );
#else
				sscanf( line, "%05d%09d%09d%03d",
					&values[0], &values[1], &values[2], &values[3]);
#endif
				strncpy( descr, line + 5 + 9 + 9 + 3 , values[3] );	
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
				if( info->status_tokens[CURRENT_ID] == values[0] && info->status_tokens[PLAY_MODE] == 0 )
					put_text( "entry_samplename", title );
				free(timecode);
				g_free(title);
			}
			offset += len;
		}
		offset = 0;
	}

	if( fxtext ) free(fxtext);
	fxlen = 0;

	multi_vims( VIMS_STREAM_LIST,"%d",0 );
	fxtext = recv_vims(5, &fxlen);
	if( fxlen > 0 && fxtext != NULL)
	{
		while( offset < fxlen )
		{
			char tmp_len[4];
			veejay_memset(tmp_len,0,sizeof(tmp_len));
			strncpy(tmp_len, fxtext + offset, 3 );
	
			int  len = atoi(tmp_len);
			offset += 3;
			if(len > 0)
			{
				veejay_memset(line,0,sizeof(line));
				veejay_memset(descr,0,sizeof(descr));
				strncpy( line, fxtext + offset, len );
	
				int values[10];

				veejay_memset(values,0, sizeof(values));
				
				sscanf( line, "%05d%02d%03d%03d%03d%03d%03d%03d",
					&values[0], &values[1], &values[2], 
					&values[3], &values[4], &values[5],
					&values[6], &values[7]
				);
				strncpy( descr, line + 22, values[6] );
				switch( values[1] )
				{
					case STREAM_CALI	:snprintf(source,sizeof(source),"calibrate %d",values[0]);
								 break;
					case STREAM_VIDEO4LINUX :snprintf(source,sizeof(source),"capture %d",values[0]);break;
					case STREAM_WHITE	:snprintf(source,sizeof(source),"solid %d",values[0]); 
								 break;
					case STREAM_MCAST	:snprintf(source,sizeof(source),"multicast %d",values[0]);break;
					case STREAM_NETWORK	:snprintf(source,sizeof(source),"unicast %d",values[0]);break;
					case STREAM_YUV4MPEG	:snprintf(source,sizeof(source),"y4m %d",values[0]);break;
					case STREAM_DV1394	:snprintf(source,sizeof(source),"dv1394 %d",values[0]);break;
					case STREAM_PICTURE	:snprintf(source,sizeof(source),"image %d",values[0]);break;
					case STREAM_GENERATOR   :snprintf(source,sizeof(source),"Z%d",values[0]);break;
					default:
						snprintf(source,sizeof(source),"??? %d", values[0]);	
				}
				gchar *gsource = _utf8str( descr );
				gchar *gtype = _utf8str( source );

				int bank_page = 0;
				int poke_slot = 0;

				verify_bank_capacity( &bank_page , &poke_slot, values[0], 1);
       
				if(bank_page >= 0)
				{			
					if( info->sample_banks[bank_page]->slot[poke_slot] <= 0 )
					{				
						sample_slot_t *tmp_slot = create_temporary_slot(poke_slot,values[0],1, gtype,gsource );
						add_sample_to_sample_banks(bank_page, tmp_slot );											     n_slots ++;
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

	if(fxtext) free(fxtext);
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
					if( args != NULL && strlen(args) > 0 )
					{
						char msg[100];
						sprintf(msg, "%03d:%s;", event_id, args );
						msg_vims(msg);
					}
				}
			}
		}
		if( vimsid ) g_free( vimsid );
	}
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
	gint n_params = 0;
        gtk_tree_model_get(model, &iter, VIMS_ID, &vimsid, -1);
	gtk_tree_model_get(model, &iter, VIMS_CONTENTS, &text, -1 );
	gtk_tree_model_get(model, &iter, VIMS_PARAMS, &n_params, -1);
	int k=0; 
	int m=0;
	gchar *key = NULL;
	gchar *mod = NULL;
#ifdef HAVE_SDL
	gtk_tree_model_get(model,&iter, VIMS_KEY, &key, -1);
	gtk_tree_model_get(model,&iter, VIMS_MOD, &mod, -1);
#endif
	if(sscanf( vimsid, "%d", &event_id ))
	{
#ifdef HAVE_SDL
		k = sdlkey_by_name( key );
		m = sdlmod_by_name( mod );	
#endif
		info->uc.selected_vims_entry = event_id;

		if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END )
			info->uc.selected_vims_type = 0;
		else
			info->uc.selected_vims_type = 1;

		if(info->uc.selected_vims_args )
			free(info->uc.selected_vims_args);
		info->uc.selected_vims_args = NULL;

		if( n_params > 0 && text )
			info->uc.selected_vims_args = strdup( text );
		
		info->uc.selected_vims_accel[0] = m;
		info->uc.selected_vims_accel[1] = k;

		clear_textview_buffer( "vimsview" );	
		if( info->uc.selected_vims_type == 1 && text)
			set_textview_buffer( "vimsview", text );
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
	GtkWidget *sel = glade_xml_get_widget_(info->main_window, "rgbkey");
	g_signal_connect( sel, "color-changed",
		(GCallback) on_rgbkey_color_changed, NULL );
	
}

static	void	setup_vimslist()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_vims");
	GtkListStore *store = gtk_list_store_new( 2,G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "tree_vims", VIMS_ID, 		"VIMS ID",0);
	setup_tree_text_column( "tree_vims", VIMS_DESCR,	"Description",0 );

	GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);

	gtk_tree_sortable_set_sort_func(
		sortable, VIMS_ID, sort_vims_func,
			GINT_TO_POINTER(VIMS_ID),NULL);

	gtk_tree_sortable_set_sort_column_id( 
		sortable, VIMS_ID, GTK_SORT_ASCENDING);
}


static	void	setup_bundles()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_bundles");
	GtkListStore *store = gtk_list_store_new( 7,G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING ,G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

	gtk_widget_set_size_request_( tree, 300, -1 );

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);

	gtk_tree_sortable_set_sort_func(
		sortable, VIMS_ID, sort_vims_func,
			GINT_TO_POINTER(VIMS_ID),NULL);

	gtk_tree_sortable_set_sort_column_id( 
		sortable, VIMS_ID, GTK_SORT_ASCENDING);

	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "tree_bundles", VIMS_ID, 	"VIMS",0);
	setup_tree_text_column( "tree_bundles", VIMS_DESCR,     "Description",0 );
	setup_tree_text_column( "tree_bundles", VIMS_KEY, 	"Key",0);
	setup_tree_text_column( "tree_bundles", VIMS_MOD, 	"Mod",0);
	setup_tree_text_column( "tree_bundles", VIMS_PARAMS,	"Max args",0);
	setup_tree_text_column( "tree_bundles", VIMS_FORMAT,	"Format",0 );
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

	setup_tree_text_column( "editlisttree", COLUMN_INT, "#",0);
	setup_tree_text_column( "editlisttree", COLUMN_STRING0, "Timecode",0 );
	setup_tree_text_column( "editlisttree", COLUMN_STRINGA, "Filename",0);
	setup_tree_text_column( "editlisttree", COLUMN_STRINGB, "Duration",0);
	setup_tree_text_column( "editlisttree", COLUMN_STRINGC, "FOURCC",0);

	g_signal_connect( editlist_tree, "row-activated",
		(GCallback) on_editlist_row_activated, NULL );

  	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(editlist_tree));
    	gtk_tree_selection_set_select_function(selection, view_el_selection_func, NULL, NULL);
    	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
}

static	void	reload_keys()
{
	gint len = 0;
	single_vims( VIMS_KEYLIST );
	gchar *text = recv_vims( 6, &len );
	gint offset = 0;

	if( len == 0 || text == NULL )
		return;

	gint k,index;
	for( k = 0; k < VIMS_MAX  ; k ++ )
	{
		vims_keys_t *p = &vims_keys_list[k];
		if(p->vims)
			free(p->vims);
		p->keyval = 0;
		p->state = 0;
		p->event_id = 0;
		p->vims = NULL;
	}

	char *ptr = text;

	while( offset < len )
	{
		int val[6];
		veejay_memset(val,0,sizeof(val));
		int n = sscanf( ptr + offset, "%04d%03d%03d%03d", &val[0],&val[1],&val[2],&val[3]);
		if( n != 4 )
		{
			free(text);
			return;
		}

		offset += 13;
		char *message = strndup( ptr + offset , val[3] );

		offset += val[3];

		
		index = (val[1] * G_MOD_OFFSET) + val[2];

		if( index < 0 || index >= VIMS_MAX )
			continue;

		vims_keys_list[ index ].keyval 		= val[2];
		vims_keys_list[ index ].state 		= val[1];
		vims_keys_list[ index ].event_id 	= val[0];	
		vims_keys_list[ index ].vims		= message;
	}
	free(text);
}


static	void	reload_bundles()
{
	reload_keys();

	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_bundles");
	GtkListStore *store;
	GtkTreeIter iter;
	
	gint len = 0;
	single_vims( VIMS_BUNDLE_LIST );
	gchar *eltext = recv_vims(6,&len); // msg len
	gint 	offset = 0;

	reset_tree("tree_bundles");

	if(len == 0 || eltext == NULL )
	{
#ifdef STRICT_CHECKING
		assert(eltext != NULL && len > 0);
#endif
		return;
	}

	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	char *ptr = eltext;

	while( offset < len )
	{
		char *message = NULL;
		char *format  = NULL;
		char *args    = NULL;
		int val[6];

		veejay_memset(val,0,sizeof(val));

		sscanf( ptr + offset, "%04d%03d%03d%04d", &val[0],&val[1],&val[2],&val[3]);

		offset += 14;

		message = strndup( ptr + offset , val[3] );
		offset += val[3];
		
		sscanf( ptr + offset, "%03d%03d", &val[4], &val[5] );

		offset += 6;

		if(val[4]) // format string
		{
			format = strndup( ptr + offset, val[4] );
			offset += val[4];
		}

		if(val[5]) // argument string
		{
			args   = strndup( ptr + offset, val[5] );
			offset += val[5];
		}

		gchar *g_descr 	= NULL;
		gchar *g_format	= NULL; 
		gchar *g_content = NULL;
#ifdef HAVE_SDL
		gchar *g_keyname  = sdlkey_by_id( val[1] );
		gchar *g_keymod   = sdlmod_by_id( val[2] );
#else
		gchar *g_keyname = "N/A";
		gchar *g_keymod = "";
#endif
		gchar *g_vims[5];	

		sprintf( (char*) g_vims, "%03d", val[0] );

		if( val[0] >= VIMS_BUNDLE_START && val[0] < VIMS_BUNDLE_END )
		{
			g_content = _utf8str( message );
		}
		else
		{
			g_descr = _utf8str( message );
			if( format )
				g_format = _utf8str( format );
			if( args )
			{
				g_content = _utf8str( args );
		//@ set default VIMS argument:
				if(vj_event_list[val[0]].args )	
				{
					free(vj_event_list[val[0]].args );
					vj_event_list[val[0]].args = NULL;
				}
				vj_event_list[ val[0] ].args     = strdup( args );
			}

		}

		gtk_list_store_append( store, &iter );
		gtk_list_store_set(store, &iter,
			VIMS_ID, 	g_vims,
			VIMS_DESCR, 	g_descr,
			VIMS_KEY, 	g_keyname,
			VIMS_MOD, 	g_keymod,
			VIMS_PARAMS, 	vj_event_list[ val[0] ].params,
			VIMS_FORMAT, 	g_format,
			VIMS_CONTENTS,  g_content,
			-1 );

		if(message) free(message);
		if(format)  free(format);
		if(args)    free(args);


		if( g_descr ) g_free(g_descr );
		if( g_format ) g_free(g_format );
		if( g_content) g_free(g_content );

	}
	/* entry, start frame, end frame */ 

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));

	free( eltext );
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
#ifdef STRICT_CHECKING
		assert(eltext != NULL && len > 0);
#endif
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

		if(vj_event_list[val[0]].format )
			free(vj_event_list[val[0]].format);
		if(vj_event_list[val[0]].descr )
			free(vj_event_list[val[0]].descr);

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

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	free( eltext );
}
static void remove_all (GtkComboBox *combo_box)
{
	GtkTreeModel* model = gtk_combo_box_get_model (combo_box);
	gtk_list_store_clear (GTK_LIST_STORE(model));
}

static	char *tokenize_on_space( char *q )
{
	int n = 0;
	char *r = NULL;
	char *p = q;
	while( *p != '\0' && !isblank( *p ) && *p != ' ' && *p != 20)
	{
		(*p)++;
		n++;
	}
	if( n <= 0 )
		return NULL;
	r = vj_calloc( n+1 );
	strncpy( r, q, n );
	return r;
}

static int have_srt_ = 0;
static	void	init_srt_editor()
{
	reload_fontlist();
	update_spin_range( "spin_text_x", 0, info->el.width-1 , 0 );
	update_spin_range( "spin_text_y", 0, info->el.height-1, 0 );
	update_spin_range( "spin_text_size", 10, 500, 40 );
	update_spin_range( "spin_text_start", 0, total_frames_, 0 );
}


static	void	reload_fontlist()
{
	GtkWidget *box = glade_xml_get_widget( info->main_window, "combobox_fonts");
	remove_all( GTK_COMBO_BOX( box ) );
	single_vims( VIMS_FONT_LIST );
	gint len = 0;
	gchar *srts = recv_vims(6,&len );
	gint i = 0;
	gchar *p = srts;

	while( i < len )
	{
		char tmp[4];
		veejay_memset(tmp,0,sizeof(tmp));
		strncpy(tmp, p, 3 );
		int slen = atoi(tmp);
		p += 3;
		gchar *seq_str = strndup( p, slen );
		gtk_combo_box_append_text( GTK_COMBO_BOX(box),	seq_str );
		p += slen;
		free(seq_str);
		i += (slen + 3);
	}
	free(srts);
}

static	void	reload_srt()
{
	if(!have_srt_)
	{
		init_srt_editor();
		have_srt_ = 1;
	}

	GtkWidget *box = glade_xml_get_widget( info->main_window, "combobox_textsrt");
	remove_all( GTK_COMBO_BOX( box ) );
	
	clear_textview_buffer( "textview_text");
	
	single_vims( VIMS_SRT_LIST );
	gint i=0, len = 0;

	gchar *srts = recv_vims(6,&len );
	if( srts == NULL || len <= 0 )
	{
	//	disable_widget( "SRTframe" );
		return;
	}

	gchar *p = srts;
	gchar *token = NULL;

	while(  i < len )
	{
		token = tokenize_on_space( p );
		if(!token)
		 break;
		if(token)
		{
			gtk_combo_box_append_text( GTK_COMBO_BOX(box),token );
			i += strlen(token) + 1;
			free(token);
		}
		else
			i++;
		p = srts + i;
	}
	free(srts);	
}
void	_edl_reset(void)
{
	if( info->elref != NULL)
	{
		int n = g_list_length(info->elref);
		int i;
		for( i = 0; i <=n ; i ++ )
		{
			void *ptr = g_list_nth_data( info->elref , i );
			if(ptr)
				free(ptr);
		}
		g_list_free( info->elref );
	}
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
	_edl_reset();
	
	if( eltext == NULL || len < 0 )
	{
		return;
	}
	
	el_constr *el;

	char *tmp = strndup( eltext + offset, 4 );
	if( sscanf( tmp,"%d",&num_files ) != 1 ) {
		free(tmp);
		free(eltext);
		return;
	}
	free(tmp);

	offset += 4;

	for( i = 0; i < num_files ; i ++ )	
	{
		int name_len = 0;
		tmp = strndup( eltext + offset, 3 );
		if( sscanf( tmp,"%d", &name_len ) != 1 ) {
			free(tmp);
			free(eltext);
			return;
		}
		offset += 3;
		free(tmp);
		char *file = strndup( eltext + offset, name_len );
		
		offset += name_len;
		int iter = 0;
		tmp = strndup( eltext + offset, 4 );
		if( sscanf( tmp, "%d", &iter ) != 1 ) {
			free(tmp);
			free(eltext);
			return;
		}
		free(tmp);
		offset += 4;
		
		long num_frames = 0;
		tmp = strndup( eltext + offset, 10 );
		if( sscanf(tmp, "%ld", &num_frames ) != 1 ) {
			free(tmp);
			free(eltext);
			return;
		}
		free(tmp);
		offset += 10;
		
		int fourcc_len = 0;
		tmp = strndup( eltext + offset, 2 );
		if( sscanf( tmp, "%d", &fourcc_len) != 1 ) {
			free(tmp);
			free(eltext);
			return;
		}
		offset += fourcc_len;
		char *fourcc = strndup( eltext + offset - 1, fourcc_len );
		
		el = _el_entry_new( iter, file, num_frames, fourcc );
		info->editlist = g_list_append( info->editlist, el );
		
		offset += 2;

		free(file);
		free(fourcc);
		free(tmp);
	}
	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	int total_frames = 0; // running total of frames
	int row_num = 0;
	while( offset < len )
	{
		tmp = (char*)strndup( eltext + offset, (3*16) );
		offset += (3*16);
		long nl=0, n1=0,n2=0;

		sscanf( tmp, "%016ld%016ld%016ld",
			&nl,&n1,&n2 );

		if(nl < 0 || nl >= num_files)
		{
			free(tmp);
			free(eltext);
			return;
		}
		int file_len = _el_get_nframes( nl );
	  	if(file_len <= 0)
		{
			free(tmp);
			row_num++;
			continue;
		}
		if(n1 < 0 )
			n1 = 0;
		if(n2 >= file_len)
			n2 = file_len;

		if(n2 <= n1 )
		{
			free(tmp);
			row_num++;
			continue;
		}

		info->elref = g_list_append( info->elref, _el_ref_new( row_num,(int) nl,n1,n2,total_frames ) ) ;
		char *tmpname = _el_get_filename(nl);
		gchar *fname = get_relative_path(tmpname);
		gchar *timecode = format_selection_time( n1,n2 );
		gchar *gfourcc = _utf8str( _el_get_fourcc(nl) );
		gchar *timeline = format_selection_time( 0, total_frames );

		gtk_list_store_append( store, &iter );
		gtk_list_store_set( store, &iter,
				COLUMN_INT, (guint) row_num,
				COLUMN_STRING0, timeline,
				COLUMN_STRINGA, fname,
				COLUMN_STRINGB, timecode,
				COLUMN_STRINGC, gfourcc,-1 );		
				
		free(timecode);
		g_free(gfourcc);
		g_free(fname);
		free(timeline);
		free(tmp);
		
		total_frames = total_frames + (n2-n1) + 1;
		row_num ++;
	}

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
		
	free( eltext );
}

// execute after el change:
static	void	load_editlist_info()
{
	char norm;
	float fps;
	int values[10] = { 0 };
	long rate = 0;
	long dum[2];
	char tmp[16];
	int len = 0;

	single_vims( VIMS_VIDEO_INFORMATION );
	gchar *res = recv_vims(3,&len);
	if( len <= 0 || res==NULL)
	{
#ifdef STRICT_CHECKING
		assert(len > 0 && res != NULL);
#endif
		return;
	}
	sscanf( res, "%d %d %d %c %f %d %d %ld %d %ld %ld %d",
		&values[0], &values[1], &values[2], &norm,&fps,
		&values[4], &values[5], &rate, &values[7],
		&dum[0], &dum[1], &values[8]);
	snprintf( tmp, sizeof(tmp)-1, "%dx%d", values[0],values[1]);

	info->el.width = values[0];
	info->el.height = values[1];
	info->el.num_frames = dum[1];
	update_label_str( "label_el_wh", tmp );
	snprintf( tmp, sizeof(tmp)-1, "%s",
		(norm == 'p' ? "PAL" : "NTSC" ) );
	update_label_str( "label_el_norm", tmp);
	update_label_f( "label_el_fps", fps );

	update_spin_value( "screenshot_width", info->el.width );
	update_spin_value( "screenshot_height", info->el.height );	

	info->el.fps = fps;
#ifdef STRICT_CHECKING
	assert( info->el.fps > 0 );
#endif
	info->el.num_files = dum[0];
	snprintf( tmp, sizeof(tmp)-1, "%s",
		( values[2] == 0 ? "progressive" : (values[2] == 1 ? "top first" : "bottom first" ) ) );
	update_label_str( "label_el_inter", tmp );
	update_label_i( "label_el_arate", (int)rate, 0);
	update_label_i( "label_el_achans", values[7], 0);
	update_label_i( "label_el_abits", values[5], 0);

	info->el.ratio = (float)info->el.width / (float) info->el.height;

	if( values[4] == 0 )
	{
		disable_widget( "button_5_4" );
	}
	else
	{
		set_toggle_button( "button_5_4", values[8]);
		enable_widget( "button_5_4" );
	}

	free(res);
}



#ifndef STRICT_CHECKING
static	void	disable_widget_(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
	if(!w) {
		veejay_msg(0, "Widget '%s' not found",name);
		return;
	}
	gtk_widget_set_sensitive_( GTK_WIDGET(w), FALSE );
}

static	void	enable_widget_(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
	if(!w) {
		veejay_msg(0, "Widget '%s' not found",name);
		return;
	}
	gtk_widget_set_sensitive_( GTK_WIDGET(w), TRUE );
}
#else
static	void	disable_widget_(const char *name, const char *s, int line)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
	if(!w) {
		veejay_msg(0, "Widget '%s' not found, caller is %s:%d",name,s,line);
		return;
	}
	gtk_widget_set_sensitive_( GTK_WIDGET(w), FALSE );
}

static	void	enable_widget_(const char *name, const char *s, int line)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
	if(!w) {
		veejay_msg(0, "Widget '%s' not found, caller is %s:%d",name,s,line);
		return;
	}
	gtk_widget_set_sensitive_( GTK_WIDGET(w), TRUE );
}
#endif


static	gchar	*format_selection_time(int start, int end)
{
	double fps = (double) info->el.fps;
	int   pos = (end-start);
	
	return format_time( pos, fps );
}


static	gboolean	update_cpumeter_timeout( gpointer data )
{
	gdouble ms   = (gdouble)info->status_tokens[ELAPSED_TIME]; 
	gdouble fs   = (gdouble)get_slider_val( "framerate" );
	gdouble lim  = (1.0f/fs)*1000.0;

	if( ms < lim ) {
		update_label_str( "cpumeter", text_msg_[TEXT_REALTIME].text );
	} else {
		char text[32];
		sprintf(text, "%2.2f FPS", ( 1.0f / ms ) * 1000.0 );

		update_label_str( "cpumeter", text );	
	}
	return TRUE;
}
static	gboolean	update_cachemeter_timeout( gpointer data )
{
	char text[32];
	gint	   v = info->status_tokens[TOTAL_MEM];
	sprintf(text,"%d MB cached",v);
	update_label_str( "cachemeter", text );	
	
	return TRUE;
}

static	gboolean	update_sample_record_timeout(gpointer data)
{
	if( info->uc.playmode == MODE_SAMPLE )
	{
		GtkWidget *w;
		if( is_button_toggled("seqactive" ) )
		{
			w = glade_xml_get_widget_( info->main_window, 
				"rec_seq_progress" );
		}
		else
		{
			w = glade_xml_get_widget_( info->main_window, 
				"samplerecord_progress" );

		}	
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

static char theme_path[1024];
static char glade_path[1024];
static char theme_file[1024];
static char theme_dir[1024];
static int  use_default_theme_ = 0;
static char **theme_list = NULL;
GtkSettings *theme_settings = NULL;
static int	select_f(const struct dirent *d )
{
	if ((strcmp(d->d_name, ".") == 0) ||
		(strcmp(d->d_name, "..") == 0))
		 return 0;
	return 1;
}

static void set_default_theme()
{
	sprintf( theme_path, "%s", RELOADED_DATADIR);
	sprintf( theme_file, "%s/gveejay.rc", RELOADED_DATADIR );
	use_default_theme_ = 1;
}

void	find_user_themes(int theme)
{
	char *home = getenv("HOME");
	char data[1024];
	char location[1024];
	char path[1024];
	veejay_memset( theme_path, 0, sizeof(theme_path));
	veejay_memset( theme_file, 0, sizeof(theme_file));

	theme_settings = gtk_settings_get_default();
//	snprintf( glade_path, sizeof(glade_path), "%s/gveejay.reloaded.glade",RELOADED_DATADIR);


	if(!home)
	{
		if(theme) set_default_theme();
		return;
	}

	if(!theme)
	{
		veejay_msg(VEEJAY_MSG_INFO,"Not loading veejay themes");
		return;
	}
	snprintf( path, sizeof(path),"%s/.veejay/theme/theme.config", home );

	int sloppy = open( path,O_RDONLY );
	if( sloppy < 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Theme config '%s' not found, creating default.", path );
		veejay_msg(VEEJAY_MSG_WARNING, "Please setup symbolic links from %s/theme/", RELOADED_DATADIR );
		veejay_msg(VEEJAY_MSG_WARNING, "                            to %s/.veejay/theme/",home);
		veejay_msg(VEEJAY_MSG_WARNING, "and set the name of the theme in theme.config" );
		set_default_theme();
		int wd=	open( path, O_WRONLY );
		if(wd)
		{	
			char text[7] = "Default";
			write(wd,text, sizeof(text));
			close(wd);
		}
		return;
	}

	veejay_memset(data,0,sizeof(data));
	veejay_memset(location,0,sizeof(location));
	char *dst = location;

	if( read( sloppy, data, sizeof(data) ) > 0 )
	{
		int str_len = strlen( data );
		int i;
		for( i = 0; i < str_len ; i ++ )
		{
			if( data[i] == '\0' || data[i] == '\n' ) break;
			*dst = data[i];
			(*dst)++;
		}
	}

	if( sloppy )	
		close( sloppy );

	if( strcmp( location, "Default" ) == 0 )	
	{
		veejay_msg(VEEJAY_MSG_INFO, "Using default theme.");
		set_default_theme();
	}
	else
	{
		snprintf(theme_path, sizeof(theme_path), "%s/.veejay/theme/%s", home, location );
		snprintf(theme_file, sizeof(theme_file), "%s/gveejay.rc", theme_path );
		use_default_theme_ = 0;
		veejay_msg(VEEJAY_MSG_INFO, "\tRC-style '%s'", theme_file );
		veejay_msg(VEEJAY_MSG_INFO, "\tTheme location: '%s'", theme_path);
	}

	struct dirent **files = NULL;
	struct stat sbuf;
	snprintf(theme_dir,sizeof(theme_dir), "%s/.veejay/theme/", home );

	veejay_memset( &sbuf,0,sizeof(struct stat));

	int n_files = scandir( theme_dir, &files, select_f, alphasort );
	if( n_files <= 0 )
	{
		veejay_msg(0, "No themes found in %s", theme_dir );
		return;
	}

	theme_list = (char**) vj_calloc(sizeof(char*) * (n_files+2) );
	int i,k=0;
	for( i = 0; i < n_files; i ++ )
	{
		char *name = files[i]->d_name;
		if( name && strcmp(name, "Default" ) != 0)
		{
			snprintf(location, sizeof(location), "%s/%s", theme_dir, name );
			veejay_memset( &sbuf,0, sizeof(struct stat ));
			if( lstat( location, &sbuf ) == 0 )
			{
				if( S_ISLNK( sbuf.st_mode ))
				{
					veejay_memset( &sbuf,0,sizeof(struct stat));
					stat(location, &sbuf);
				}
				if( S_ISDIR( sbuf.st_mode  ))
				{
					//@ test for gveejay.rc
					struct stat inf;
					gchar *test_file = g_strdup_printf( "%s/%s/gveejay.rc",theme_dir,name );
					if( stat( test_file, &inf) == 0 && (S_ISREG(inf.st_mode) || S_ISLNK( inf.st_mode)))
					{ 
						theme_list[k] = strdup( name );
						k++;
					}
					else
					{
						veejay_msg(VEEJAY_MSG_WARNING, "%s/%s does not contain a gveejay.rc file", theme_dir,name);
					}
					g_free(test_file);
				}
			}
		}
	}

	if( k == 0 )
	{
		free(theme_list);
		theme_list = NULL;
		return;
	}

	theme_list[ k ] = strdup("Default");
	for( k = 0; theme_list[k] != NULL ; k ++ )
		veejay_msg(VEEJAY_MSG_INFO, "Added Theme #%d %s", k, theme_list[k]);
//	veejay_msg(VEEJAY_MSG_INFO, "Loading %s", theme_file );


}

void	gui_load_theme()
{
	gtk_rc_parse( theme_file );
}

char	*get_glade_path()
{
	return glade_path;
}

char	*get_gveejay_dir()
{
	return RELOADED_DATADIR;
}

void	get_gd(char *buf, char *suf, const char *filename)
{
	const char *dir = RELOADED_DATADIR;
	
	if(filename !=NULL && suf != NULL)
		sprintf(buf, "%s/%s/%s",dir,suf, filename );
	if(filename !=NULL && suf==NULL)
		sprintf(buf, "%s/%s", dir, filename);
	if(filename == NULL && suf != NULL)
		sprintf(buf, "%s/%s/" , dir, suf);
}

GdkPixbuf	*vj_gdk_pixbuf_scale_simple( GdkPixbuf *src, int dw, int dh, GdkInterpType inter_type )
{
	return gdk_pixbuf_scale_simple( src,dw,dh,inter_type );
/*
	GdkPixbuf *res = gdk_pixbuf_new( GDK_COLORSPACE_RGB, FALSE, 8, dw, dh );
#ifdef STRICT_CHECKING
	assert( GDK_IS_PIXBUF( res ) );
#endif
	uint8_t *res_out = gdk_pixbuf_get_pixels( res );
	uint8_t *src_in  = gdk_pixbuf_get_pixels( src );
	uint32_t src_w   = gdk_pixbuf_get_width( src );
	uint32_t src_h   = gdk_pixbuf_get_height( src );
	int dst_w = gdk_pixbuf_get_width( res );
	int dst_h = gdk_pixbuf_get_height( res );
	VJFrame *src1 = yuv_rgb_template( src_in, src_w, src_h, PIX_FMT_BGR24 );
	VJFrame *dst1 = yuv_rgb_template( res_out, dst_w, dst_h, PIX_FMT_BGR24 );

	veejay_msg(0, "%s: %dx%d -> %dx%d", __FUNCTION__, src_w,src_h,dst_w,dst_h );

	yuv_convert_any_ac( src1,dst1, src1->format, dst1->format );

	free(src1);
	free(dst1);

	return res;*/
}

void		gveejay_sleep( void *u )
{
	struct timespec nsecsleep;
//	nsecsleep.tv_nsec = 1000000 * 4; //@ too long
	nsecsleep.tv_nsec = 500000; 
	nsecsleep.tv_sec = 0;	
	nanosleep( &nsecsleep, NULL ); 	
}


int		gveejay_time_to_sync( void *ptr )
{
	vj_gui_t *ui = (vj_gui_t*) ptr;
	struct timeval time_now;
	gettimeofday( &time_now, 0 );

	double diff = time_now.tv_sec - ui->time_last.tv_sec +
			(time_now.tv_usec - ui->time_last.tv_usec ) * 1.e-6;
	float fps = 0.0;

	struct timespec nsecsleep;
	if ( ui->watch.state == STATE_PLAYING )
	{
		fps = ui->el.fps;	
		float spvf = 1.0 / fps;
		float ela  = ( info->status_tokens[ELAPSED_TIME] / 1000.0 );	
		spvf += ela; //@ add elapsed time 
		
		if( diff > spvf ) {
			ui->time_last.tv_sec = time_now.tv_sec;
			ui->time_last.tv_usec = time_now.tv_usec;
			return 1;
		}
		int usec = 0;
		int uspf = (int)(1000000.0 / fps);
		if( ela )
			uspf += (int)(1000000.0 / ela );
		usec = time_now.tv_usec - ui->time_last.tv_usec;
		if( usec < 0 )
			usec += 1000000;
		if( time_now.tv_sec > ui->time_last.tv_sec + 1 )
			usec = 1000000;
		if( (uspf - usec) < (1000000 / 100)) {
			return 0;
		}

//veejay_msg(0 , "%d", (uspf - usec - 1000000 / 100 ) * 1000);
		nsecsleep.tv_nsec = 3200000;
//		nsecsleep.tv_nsec =(uspf - usec - 1000000 / 100 ) * 1000;
		nsecsleep.tv_sec = 0;	
		nanosleep( &nsecsleep, NULL ); 	
		return 0;
	} else if ( ui->watch.state == STATE_STOPPED ) 
	{
		reloaded_restart();
	}
	nsecsleep.tv_nsec = 1600000;
	nsecsleep.tv_sec = 0;
	nanosleep( &nsecsleep, NULL );
	return 0;
}


// skin 0: notebook18, page 3
// skin 1: vjdeck , page 2
int		veejay_update_multitrack( void *data )
{
	vj_gui_t *gui = (vj_gui_t*) data;
	sync_info *s = multitrack_sync( gui->mt );

	if( s->status_list[s->master] == NULL ) {
		info->watch.w_state = STATE_STOPPED;
		return 1;
	}

	GtkWidget *maintrack = glade_xml_get_widget( info->main_window, "imageA");
	int i;
	GtkWidget *ww = glade_xml_get_widget_( info->main_window, crappy_design[ui_skin_].name );
	int deckpage = gtk_notebook_get_current_page(GTK_NOTEBOOK(ww));

#ifdef STRICT_CHECKING
	assert( s->status_list[s->master] != NULL );
#endif

	int tmp = 0;
	for ( i = 0; i < 32; i ++ )
	{	
		tmp += s->status_list[s->master][i];
		info->status_tokens[i] = s->status_list[s->master][i];
	}
	if( tmp == 0 )
	{
		free(s->status_list);
		free(s->img_list );
		free(s->widths);
		free(s->heights);
		free(s);
		return 0;
	}

	info->status_lock = 1;
	info->uc.playmode = gui->status_tokens[ PLAY_MODE ];
	update_gui();
	info->prev_mode = gui->status_tokens[ PLAY_MODE ];
	gui->status_lock = 0;
	int pm = info->status_tokens[PLAY_MODE];
#ifdef STRICT_CHECKING
	assert( pm >= 0 && pm < 4 );
#endif
	int *history = info->history_tokens[pm];

	for( i = 0; i < STATUS_TOKENS; i ++ )
		history[i] = info->status_tokens[i];

	for( i = 0; i < s->tracks ; i ++ )
	{
		if( s->status_list[i] )
		{
			update_multitrack_widgets( info->mt, s->status_list[i], i );

			free(s->status_list[i]);
		}
		if( s->img_list[i] )
		{
			if( i == s->master )
			{
#ifdef STRICT_CHECKING
				assert( s->widths[i] > 0 );
				assert( s->heights[i] > 0 );
				assert( GDK_IS_PIXBUF( s->img_list[i] ) );
#endif
				if( gdk_pixbuf_get_height(s->img_list[i]) == preview_box_w_ &&
				    gdk_pixbuf_get_width(s->img_list[i]) == preview_box_h_  )
					gtk_image_set_from_pixbuf_( GTK_IMAGE( maintrack ), s->img_list[i] );
				else {
					GdkPixbuf *result = vj_gdk_pixbuf_scale_simple( s->img_list[i],preview_box_w_,preview_box_h_, GDK_INTERP_NEAREST );
					gtk_image_set_from_pixbuf_( GTK_IMAGE( maintrack ), result );
					g_object_unref(result);

				}
				

				vj_img_cb( s->img_list[i] );
			} 
			
			if(deckpage == crappy_design[ui_skin_].page)
				multitrack_update_sequence_image( gui->mt, i, s->img_list[i] );

			g_object_unref( s->img_list[i] );
		} else {
			if( i == s->master ) {
				multitrack_set_logo( gui->mt, maintrack );
			}
		}
	}
	free(s->status_list);
	free(s->img_list );
	free(s->widths);
	free(s->heights);
	free(s);
	return 1;
}

static	void	update_status_accessibility(int old_pm, int new_pm)
{
	int i;

	if( old_pm == new_pm )
		return;

	if( new_pm == MODE_STREAM )
	{
		for(i=0; samplewidgets[i].name != NULL; i++)
			disable_widget( samplewidgets[i].name);
		for(i=0; plainwidgets[i].name != NULL; i++)
			disable_widget( plainwidgets[i].name);
		for(i=0; streamwidgets[i].name != NULL; i++)
			enable_widget( streamwidgets[i].name);

	}

	if( new_pm == MODE_SAMPLE )
	{
		for(i=0; streamwidgets[i].name != NULL; i++)
			disable_widget( streamwidgets[i].name);
		for(i=0; plainwidgets[i].name != NULL; i++)
			disable_widget( plainwidgets[i].name);
		for(i=0; samplewidgets[i].name != NULL; i++)
			enable_widget( samplewidgets[i].name);
	}

	if( new_pm == MODE_PLAIN)
	{
		for(i=0; streamwidgets[i].name != NULL; i++)
			disable_widget( streamwidgets[i].name);
		for(i=0; samplewidgets[i].name != NULL; i++)
			disable_widget( samplewidgets[i].name);
		for(i=0; plainwidgets[i].name != NULL; i++)
			enable_widget( plainwidgets[i].name);

	}
	GtkWidget *n = glade_xml_get_widget_( info->main_window, "panels" );
	int page_needed = 0;
	switch( new_pm )
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
	
	gtk_notebook_set_page( GTK_NOTEBOOK(n),	page_needed );

}

static	void	set_pm_page_label(int sample_id, int type)
{
	gchar ostitle[100];
	gchar ftitle[100];
	switch(type) {	
		case 0: 
				snprintf(ostitle, sizeof(ostitle), "Sample %d",sample_id);break;
		case 1: snprintf(ostitle, sizeof(ostitle), "Stream %d",sample_id);break;
		default:
			snprintf(ostitle,sizeof(ostitle), "Plain");break;
	}
	gchar *title = _utf8str(ostitle);
	snprintf(ftitle,sizeof(ftitle), "<b>%s</b>", ostitle);
	label_set_markup( "label_current_mode", ftitle);
	update_label_str( "label_currentsource", title );
	g_free(title);
}

static void 	update_globalinfo(int *history, int pm, int last_pm)
{
	int i;

	if( last_pm != pm )
		update_status_accessibility( last_pm, pm);

	if( info->status_tokens[MACRO] != history[MACRO] )
	{
		switch(info->status_tokens[MACRO])
		{
			case 1:
				set_toggle_button( "macrorecord",1); break;
			case 2:
				set_toggle_button( "macroplay",1 ); break;
			default:
				set_toggle_button( "macrostop",1); break;
		}
		
	}

	if( info->status_tokens[CURRENT_ID] != history[CURRENT_ID] || last_pm != pm )
	{
		// slot changed
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

		
#ifdef STRICT_CHECKING
		if( pm != MODE_PLAIN )
		assert( info->selected_slot != NULL );
#endif
	}

	if( info->status_tokens[TOTAL_SLOTS] !=
		history[TOTAL_SLOTS] 
			|| info->status_tokens[TOTAL_SLOTS] != info->uc.expected_slots )
	{
		info->uc.reload_hint[HINT_SLIST] = 1;
	}

	if( info->status_tokens[SEQ_ACT] != history[SEQ_ACT] )
	{
		info->uc.reload_hint[HINT_SEQ_ACT] = 1;
	}
	if( info->status_tokens[SEQ_CUR] != history[SEQ_CUR] )
	{
		int in = info->status_tokens[SEQ_CUR];
		if( in ) {
			set_toggle_button( "seqactive" , 1 );
		} else {
			set_toggle_button( "seqactive" , 0 );
		}
		if(info->sequence_playing >= 0)
			indicate_sequence( FALSE, info->sequencer_view->gui_slot[ info->sequence_playing ] );
		info->sequence_playing = in;
		indicate_sequence( TRUE, info->sequencer_view->gui_slot[ info->sequence_playing ] );
	}

	total_frames_ = (pm == MODE_STREAM ? info->status_tokens[SAMPLE_MARKER_END] : info->status_tokens[TOTAL_FRAMES] );
	gint history_frames_ = (pm == MODE_STREAM ? history[SAMPLE_MARKER_END] : history[TOTAL_FRAMES] ); 
	gint current_frame_ = info->status_tokens[FRAME_NUM];

	if( total_frames_ != history_frames_ || total_frames_ != (int) timeline_get_length(TIMELINE_SELECTION(info->tl)))
	{
		char *time = format_time( total_frames_,(double) info->el.fps );
		if( pm == MODE_STREAM )
		{
			update_spin_value( "stream_length", info->status_tokens[SAMPLE_MARKER_END] );
			update_label_str( "stream_length_label", time );
		}
		update_spin_range("button_fadedur", 0, total_frames_, 0 );
		update_label_i( "label_totframes", total_frames_, 1 );
		update_label_str( "label_samplelength",time);
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
			update_label_str( "sample_length_label", time );
		else
			update_label_str( "sample_length_label", "0:00:00:00" );


		timeline_set_length( info->tl,
				(gdouble) total_frames_ , current_frame_);

		if( pm != MODE_STREAM )
			info->uc.reload_hint[HINT_EL] = 1;

		free(time);
	}

	info->status_frame = info->status_tokens[FRAME_NUM];
	timeline_set_pos( info->tl, (gdouble) info->status_frame );
	char *current_time_ = format_time( info->status_frame, (double) info->el.fps );
	update_label_i(   "label_curframe", info->status_frame ,1 );
	update_label_str( "label_curtime", current_time_ );
	update_label_str( "label_sampleposition", current_time_);
	free(current_time_);

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
			if( plainspeed < 0 ) 
				info->play_direction = -1;
			else
				info->play_direction = 1;
			if( plainspeed < 0 ) plainspeed *= -1;
			if( plainspeed == 0 ) {
				update_label_str( "playhint", "Paused");
			} else {
				update_label_str( "playhint", "Playing");
			}

		}
	}

	if( pm == MODE_STREAM ) {
		if( info->status_tokens[STREAM_TYPE] == STREAM_VIDEO4LINUX ) {
			if(info->uc.cali_duration > 0 ) {
				GtkWidget *tb = glade_xml_get_widget_( info->main_window, "cali_take_button");
				info->uc.cali_duration--;
				vj_msg(VEEJAY_MSG_INFO, "Calibrate step %d of %d",
						info->uc.cali_duration,
						info->uc.cali_stage);
				if(info->uc.cali_duration == 0) {
					info->uc.cali_stage ++; //@ cali_stage = 1, done capturing black frames

					switch(info->uc.cali_stage) {
						case 1: //@ capturing black frames
							update_label_str( "current_step_label",
							"Please take an image of a uniformly lit area in placed in front of your lens.");
							gtk_button_set_label( GTK_BUTTON(tb), "Take White Frames");
							break;
						case 2:
						case 3:
							update_label_str( "current_step_label",
							"Image calibrated. You may need to adjust brightness.");
							enable_widget( "cali_save_button");
							break;
						default:
							update_label_str( "current_step_label","Image calibrated. You may need to adjust brightness.");
							gtk_button_set_label( GTK_BUTTON(tb), "Take Black Frames");
							veejay_msg(0, "Warning, mem leak if not reset first.");
							break;
						
					}
					veejay_msg(0, "Label update for case %d", info->uc.cali_stage);
					
					if(info->uc.cali_stage >= 2 ) {
						info->uc.cali_stage = 0;
					}
				}
			}
		}

	}

	update_current_slot(history, pm, last_pm);
//	info->uc.playmode = pm;
}	


static void	process_reload_hints(int *history, int pm)
{
	int	*entry_tokens = &(info->uc.entry_tokens[0]);

	if( pm == MODE_STREAM )
	{
		if(info->uc.reload_hint[HINT_V4L])
			load_v4l_info();
	
		if( info->uc.reload_hint[HINT_RGBSOLID])
			update_colorselection();

	}

	if( info->uc.reload_hint[HINT_EL] )
	{
		load_editlist_info();
		reload_editlist_contents();
	}

	if( info->uc.reload_hint[HINT_SLIST] )
	{
		load_samplelist_info(FALSE);
		info->uc.expected_slots = info->status_tokens[TOTAL_SLOTS];
	}

	if( info->uc.reload_hint[HINT_SEQ_ACT] == 1 )
	{
		load_sequence_list();
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
			disable_widget( "frame_fxtree2" );
			disable_widget( "frame_fxtree4" );
			disable_widget( "tree_sources");
			disable_widget( "rgbkey");
		}
		else
		{
			put_text( "entry_effectname", _effect_get_description( entry_tokens[ENTRY_FXID] ));
			enable_widget( "frame_fxtree2");
			enable_widget( "frame_fxtree4");
			enable_widget( "tree_sources");
			enable_widget( "rgbkey" );
			set_toggle_button( "button_entry_toggle", entry_tokens[ENTRY_VIDEO_ENABLED] );
			np = _effect_get_np( entry_tokens[ENTRY_FXID] );
			for( i = 0; i < np ; i ++ )
			{
				sprintf(slider_name, "slider_p%d",i);
				enable_widget( slider_name );
				sprintf(button_name, "inc_p%d", i);
				enable_widget( button_name );
				sprintf(button_name, "dec_p%d", i );

				gchar *tt1 = _utf8str(_effect_get_param_description(entry_tokens[ENTRY_FXID],i));
				gtk_widget_set_tooltip_text(	glade_xml_get_widget_(info->main_window, slider_name), tt1 );
				enable_widget( button_name );
				gint min,max,value;
				value = entry_tokens[ENTRY_PARAMSET + i];
				if( _effect_get_minmax( entry_tokens[ENTRY_FXID], &min,&max, i ))
				{
					update_slider_range( slider_name,min,max, value, 0);
				}
				sprintf(button_name, "kf_p%d", i );
				enable_widget( button_name );
				set_tooltip( button_name, tt1 );
				g_free(tt1);
			}
			
		}
		update_spin_value( "button_fx_entry", info->uc.selected_chain_entry);	

		for( i = np; i < MAX_UI_PARAMETERS; i ++ )
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
			set_tooltip( button_name, NULL );
			disable_widget( button_name );
			gtk_widget_set_tooltip_text( glade_xml_get_widget_(info->main_window, slider_name), NULL );
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
		vj_kf_refresh();
	
	if( info->uc.reload_hint[HINT_HISTORY] )
		reload_srt();
	
	veejay_memset( info->uc.reload_hint, 0, sizeof(info->uc.reload_hint ));	
}
void	update_gui()
{
	int pm = info->status_tokens[PLAY_MODE];
	int last_pm = info->prev_mode;
	
	int *history = NULL;

	if( last_pm < 0 ) 
		history = info->history_tokens[0];
	else
		history = info->history_tokens[ last_pm ];

	if( info->uc.randplayer && pm != last_pm )
	{
		info->uc.randplayer = 0;
		set_toggle_button( "samplerand", 0 );
	}

	if( pm == MODE_PATTERN && last_pm != pm)
	{
		if(!info->uc.randplayer )
		{
			info->uc.randplayer = 1;
			set_toggle_button( "samplerand", 1 );
		}
		info->status_tokens[PLAY_MODE] = MODE_SAMPLE;
		pm = MODE_SAMPLE;
	}

	update_globalinfo(history, pm, last_pm);

	process_reload_hints(history, pm);
	on_vims_messenger();

	update_cpumeter_timeout(NULL);
	update_cachemeter_timeout(NULL);

}
/*
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
		info->watch.state = STATE_CONNECT;
		info->run_state = RUN_STATE_REMOTE;
	}

	for( i = 0; i < n_args; i ++)
		g_free(args[i]);
}
*/
void	vj_gui_free()
{
	if(info)
	{
		int i;
		if(info->client)
			vj_client_free(info->client);

		for( i = 0; i < 4;  i ++ )
		{
			if(info->history_tokens[i])
				free(info->history_tokens[i]);
		}
		free(info);
	}
	info = NULL;

	vpf( fx_list_ );
	vpf( bankport_ );
}

void	vj_gui_style_setup()
{
	if(!info) return;
	info->color_map = gdk_colormap_get_system();
}

void	vj_gui_theme_setup(int default_theme)
{
	gtk_rc_parse(theme_file);
}

void
send_refresh_signal(void)
{
        GdkEventClient event;
        event.type = GDK_CLIENT_EVENT;
        event.send_event = TRUE;
        event.window = NULL;
        event.message_type = gdk_atom_intern("_GTK_READ_RCFILES", FALSE);
        event.data_format = 8;
        gdk_event_send_clientmessage_toall((GdkEvent *)&event);
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
		gtk_rc_parse( theme_file );

                gtk_widget_reset_rc_styles( 
			glade_xml_get_widget_(info->main_window, "gveejay_window") );

		gtk_rc_reparse_all();

		veejay_msg(VEEJAY_MSG_WARNING,
			"Loaded GTK theme %s (catched _GTK_READ_RCFILES)", theme_file );
		veejay_msg(VEEJAY_MSG_INFO,
			"If the new theme is using an engine that modifies the internal structure");
		veejay_msg(VEEJAY_MSG_INFO,
			"of the widgets, there is no way for me to undo those changes and display");
		veejay_msg(VEEJAY_MSG_INFO,
			"%s correctly", theme_file );
		return TRUE;
	}
	return FALSE;
}

void	vj_gui_set_debug_level(int level, int preview_p, int pw, int ph )
{
	veejay_set_debug_level( level );

	vims_verbosity = level;
	num_tracks_ = preview_p;
}

int	vj_gui_get_preview_priority(void)
{
	return 1;
}

void	default_bank_values(int *col, int *row )
{
	int nsc = 2;
	int nsy = 6;
	if( ui_skin_ == 1 ) {
		nsc = 5;
		nsy = 4;
	}
	if( *col == 0 && *row == 0 )
	{
		NUM_SAMPLES_PER_COL = nsc;
		NUM_SAMPLES_PER_ROW = nsy;
	}
	else
	{
		NUM_SAMPLES_PER_ROW = *col;
		NUM_SAMPLES_PER_COL = *row;
	}
	NUM_SAMPLES_PER_PAGE = NUM_SAMPLES_PER_COL * NUM_SAMPLES_PER_ROW;
	NUM_BANKS = (4096 / NUM_SAMPLES_PER_PAGE );
}

void	set_skin(int skin, int invert)
{
	ui_skin_ = skin;
	timeline_theme_colors( invert );
}

int	vj_gui_sleep_time( void )
{
	float f =  (float) info->status_tokens[ELAPSED_TIME];
	float t =  info->el.fps;

	if( t <= 0.0 || t>= 200.0 )
		t = 25.0;
	float n = (1.0 / t) * 1000.0f;
	
	if( f < n )
		return (int)( n - f );
	return (int) n;
}

int	vj_img_cb(GdkPixbuf *img )
{
	int i;
	if( !info->selected_slot || !info->selected_gui_slot )
	{
//DM
		return 0;
	}
	int sample_id = info->status_tokens[ CURRENT_ID ];
	int sample_type = info->status_tokens[ PLAY_MODE ]; 
	
	if( info->selected_slot->sample_type != sample_type || info->selected_slot->sample_id !=
			sample_id ) {
		return 0;
	}
	if( sample_type == MODE_SAMPLE || sample_type == MODE_STREAM )
	{
		sample_slot_t *slot = find_slot_by_sample( sample_id, sample_type );
		sample_gui_slot_t *gui_slot = find_gui_slot_by_sample( sample_id, sample_type );

		if( slot && gui_slot )
		{
			slot->pixbuf = vj_gdk_pixbuf_scale_simple(img,
				info->image_dimensions[0],info->image_dimensions[1], GDK_INTERP_NEAREST);
			gtk_image_set_from_pixbuf_( GTK_IMAGE( gui_slot->image ), slot->pixbuf );
			g_object_unref( slot->pixbuf );
		}

	}

	for( i = 0; i < info->sequence_view->envelope_size; i ++ )
	{
		sequence_gui_slot_t *g = info->sequence_view->gui_slot[i];
		sample_slot_t *s = info->selected_slot;
		if(g->sample_id == info->selected_slot->sample_id && g->sample_type == info->selected_slot->sample_type && s->pixbuf)
		{
			g->pixbuf_ref = vj_gdk_pixbuf_scale_simple(
				img,
				info->sequence_view->w,
				info->sequence_view->h,
				GDK_INTERP_NEAREST ); 

			gtk_image_set_from_pixbuf_( GTK_IMAGE( g->image ), g->pixbuf_ref );
			g_object_unref( g->pixbuf_ref );
		}
	}

	return 1;
}

void	vj_gui_cb(int state, char *hostname, int port_num)
{
	info->watch.state = STATE_RECONNECT;
	put_text( "entry_hostname", hostname );
	update_spin_value( "button_portnum", port_num );

	//@ clear status
	int i;
	for( i = 0; i < 4; i ++ ) {
		int *h = info->history_tokens[i];
		veejay_memset( h, 0, sizeof(int) * STATUS_TOKENS );
	}
}

void	vj_gui_setup_defaults( vj_gui_t *gui )
{
	gui->config.w = MAX_PREVIEW_WIDTH;
	gui->config.h = MAX_PREVIEW_HEIGHT;
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

static	void	theme_response( gchar *string )
{
	char theme_config[1024];
	snprintf(theme_config,sizeof(theme_config), "%stheme.config", theme_dir );
	snprintf(theme_file,sizeof(theme_file), "%s/%s/gveejay.rc", theme_dir, string );
	int fd = open( theme_config , O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
	
	if(fd > 0)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Setting up theme %s in %s", string, theme_file );
		write( fd, string, strlen(string));
		close(fd);
		vj_msg(VEEJAY_MSG_INFO, "Restart GveejayReloaded for changes to take effect");
		if( prompt_dialog("Restart GveejayReloaded", "For changes to take effect, you should restart now" ) == GTK_RESPONSE_ACCEPT)
		{
			info->watch.w_state = STATE_DISCONNECT;
			running_g_ = 0;
			restart_   = 1;
		}
	}
	else
	{
		vj_msg(VEEJAY_MSG_ERROR, "Unable to write to %s", theme_file );
	}

}
static void reloaded_sighandler(int x) 
{
	veejay_msg(VEEJAY_MSG_WARNING, "Caught signal %x", x);

	if( x == SIGPIPE ) {
		reloaded_schedule_restart();
	}
	else if  ( x == SIGINT || x == SIGABRT  ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Stopping reloaded");
		exit(0);
	} else if ( x == SIGSEGV ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Found Gremlins in your system.");
		veejay_msg(VEEJAY_MSG_WARNING, "No fresh ale found in the fridge.");
		veejay_msg(VEEJAY_MSG_INFO, "Running with sub-atomic precision...");
	        veejay_msg(VEEJAY_MSG_ERROR, "Bugs compromised the system.");
		exit(0);
	}
}
static void	sigsegfault_handler(void) {
	struct sigaction sigst;
	sigst.sa_sigaction = veejay_backtrace_handler;
	sigemptyset(&sigst.sa_mask);
	sigaddset(&sigst.sa_mask, SIGSEGV );
	sigst.sa_flags = SA_SIGINFO | SA_ONESHOT;
	if( sigaction(SIGSEGV, &sigst, NULL) == - 1 )
		veejay_msg(0,"%s", strerror(errno));
}

void	register_signals()
{
	signal( SIGINT,  reloaded_sighandler );
	signal( SIGPIPE, reloaded_sighandler );
	signal( SIGQUIT, reloaded_sighandler );
//	signal( SIGSEGV, reloaded_sighandler );
	signal( SIGABRT, reloaded_sighandler );

	sigsegfault_handler();
}




void	vj_gui_wipe()
{
	int i;
	veejay_memset( info->status_tokens, 0, sizeof(int) * STATUS_TOKENS );
	veejay_memset( info->uc.entry_tokens,0, sizeof(int) * ENTRY_LAST);
	for( i = 0 ; i < 4; i ++ )
	{
		veejay_memset(info->history_tokens[i],0, sizeof(int) * (STATUS_TOKENS+1));
	}

}

GtkWidget	*new_bank_pad(GtkWidget *box, int type)
{
	GtkWidget *pad = info->sample_bank_pad = gtk_notebook_new();
	gtk_notebook_set_tab_pos( GTK_NOTEBOOK(pad), GTK_POS_BOTTOM );
	gtk_notebook_set_show_tabs( GTK_NOTEBOOK(pad ), FALSE );
	gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET(pad), TRUE, TRUE, 0);

	if( type == 0 )  {
		setup_samplebank( NUM_SAMPLES_PER_COL, NUM_SAMPLES_PER_ROW, pad, &(info->image_dimensions[0]),
				&(info->image_dimensions[1]) );
	}

	return pad;
}

gboolean	slider_scroll_event( GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
	gint i = GPOINTER_TO_INT(user_data);
	if(ev->direction == GDK_SCROLL_UP ) {
		param_changed( i, 1, slider_names_[i].text );
	} else if (ev->direction == GDK_SCROLL_DOWN ) {
		param_changed( i, -1, slider_names_[i].text );
	}
	return FALSE;
}

gboolean	speed_scroll_event(  GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
	int plainspeed =  info->status_tokens[SAMPLE_SPEED];
	if(ev->direction == GDK_SCROLL_UP ) {
		plainspeed = plainspeed + 1;
	} else if (ev->direction == GDK_SCROLL_DOWN ) {
		plainspeed = plainspeed - 1;
	}
	update_slider_value( "speed_slider", plainspeed, 0 );
	return FALSE;
}

gboolean	slow_scroll_event(  GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
	int plainspeed =  get_slider_val("slow_slider");
	if(ev->direction == GDK_SCROLL_DOWN ) {
		plainspeed = plainspeed - 1;
	} else if (ev->direction == GDK_SCROLL_UP ) {
		plainspeed = plainspeed + 1;
	}
	if(plainspeed < 1 )
		plainspeed = 1;
	update_slider_value("slow_slider",plainspeed,0);
	vj_msg(VEEJAY_MSG_INFO, "Slow video to %2.2f fps",
		info->el.fps / (float) plainspeed );
	return FALSE;
}

void	vj_gui_set_geom( int x, int y )
{
	geo_pos_[0] = x;
	geo_pos_[1] = y;
}

void 	vj_gui_init(char *glade_file, int launcher, char *hostname, int port_num, int use_threads, int load_midi , char *midi_file)
{
	int i;
	char text[100];
	vj_gui_t *gui = (vj_gui_t*)vj_calloc(sizeof(vj_gui_t));
	if(!gui)
	{
		return;
	}
	snprintf( glade_path, sizeof(glade_path), "%s/%s",RELOADED_DATADIR,glade_file);

	veejay_memset( gui->status_tokens, 0, sizeof(int) * STATUS_TOKENS );
	veejay_memset( gui->sample, 0, 2 );
	veejay_memset( gui->selection, 0, 3 );
	veejay_memset( &(gui->uc), 0, sizeof(veejay_user_ctrl_t));
	veejay_memset( gui->uc.entry_tokens,0, sizeof(int) * ENTRY_LAST);
	gui->prev_mode = -1; 
	veejay_memset( &(gui->el), 0, sizeof(veejay_el_t));
	gui->sample_banks = (sample_bank_t**) vj_calloc(sizeof(sample_bank_t*) * NUM_BANKS );
			
	for( i = 0 ; i < 4; i ++ ) {
		gui->history_tokens[i] = (int*) vj_calloc(sizeof(int) * (STATUS_TOKENS+1));
	}

	slider_names_ = (slider_name_t*) vj_calloc(sizeof(slider_name_t) * MAX_UI_PARAMETERS );
	for( i = 0; i < MAX_UI_PARAMETERS; i ++ ) {
		snprintf(text,sizeof(text)," slider_p%d" , i );
		slider_names_[i].text = strdup( text );
	}

	gui->uc.reload_force_avoid = FALSE;

	veejay_memset( vj_event_list, 0, sizeof(vj_event_list));

	gui->client = NULL;
	gui->main_window = glade_xml_new(glade_path,NULL,NULL);
	if(gui->main_window == NULL)
	{
		free(gui);
		veejay_msg( 0, "Cannot find '%s'", glade_path );
		return;
	}
	info = gui;

	glade_xml_signal_autoconnect( gui->main_window );
	GtkWidget *frame = glade_xml_get_widget_( info->main_window, "markerframe" );
	info->tl = timeline_new();

	set_tooltip_by_widget(info->tl, tooltips[TOOLTIP_TIMELINE].text );

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


	bankport_ = vpn( VEVO_ANONYMOUS_PORT );

	gtk_widget_show(frame);
	gtk_container_add( GTK_CONTAINER(frame), info->tl );
	gtk_widget_show(info->tl);


	GtkWidget *mainw = glade_xml_get_widget_(info->main_window,"gveejay_window" );

#ifdef STRICT_CHECKING
	debug_spinboxes();
#endif

	snprintf(text, sizeof(text), "Reloaded - version %s",VERSION);
	gtk_label_set_text( GTK_LABEL(glade_xml_get_widget_(info->main_window, "build_revision")), text);

	g_signal_connect_after( GTK_OBJECT(mainw), "client_event",
		GTK_SIGNAL_FUNC( G_CALLBACK(gui_client_event_signal) ), NULL );

	g_signal_connect( GTK_OBJECT(mainw), "destroy",
			G_CALLBACK( gveejay_quit ),
			NULL );
	g_signal_connect( GTK_OBJECT(mainw), "delete-event",
			G_CALLBACK( gveejay_quit ),
			NULL );

    	GtkWidget *box = glade_xml_get_widget_( info->main_window, "sample_bank_hbox" );
	info->sample_bank_pad = new_bank_pad( box,0 );

	//QuickSelect slots
	create_ref_slots( 10 );


	//SEQ
	create_sequencer_slots( 10,10 );

	veejay_memset( vj_event_list, 0, sizeof( vj_event_list ));
	veejay_memset( vims_keys_list, 0, sizeof( vims_keys_list) );
	
	gtk_widget_show( info->sample_bank_pad );

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
	setup_server_files();

	text_defaults();

	GtkWidget *fgb = glade_xml_get_widget( 
			  info->main_window, "boxtext" );
	GtkWidget *bgb = glade_xml_get_widget(
			  info->main_window, "boxbg" );
	GtkWidget *rb = glade_xml_get_widget(
			  info->main_window, "boxred" );
	GtkWidget *gb = glade_xml_get_widget(
			  info->main_window, "boxgreen" );
	GtkWidget *bb = glade_xml_get_widget(
			  info->main_window, "boxblue" );
	GtkWidget *lnb = glade_xml_get_widget(
				info->main_window,"boxln" );
	g_signal_connect( G_OBJECT( bgb ), "expose_event",
			G_CALLBACK( boxbg_expose_event ), NULL);
	g_signal_connect( G_OBJECT( fgb ), "expose_event",
			G_CALLBACK( boxfg_expose_event ), NULL);
	g_signal_connect( G_OBJECT( lnb ), "expose_event",
			G_CALLBACK( boxln_expose_event ), NULL);
	g_signal_connect( G_OBJECT( rb ), "expose_event",
			G_CALLBACK( boxred_expose_event ), NULL);
	g_signal_connect( G_OBJECT( gb ), "expose_event",
			G_CALLBACK( boxgreen_expose_event ), NULL);
	g_signal_connect( G_OBJECT( bb ), "expose_event",
			G_CALLBACK( boxblue_expose_event ), NULL);


	set_toggle_button( "button_252", vims_verbosity );


	int pw = MAX_PREVIEW_WIDTH;
	int ph = MAX_PREVIEW_HEIGHT;
	
	GtkWidget *img_wid = glade_xml_get_widget_( info->main_window, "imageA");

	gui->mt = multitrack_new(
			(void(*)(int,char*,int)) vj_gui_cb,
			NULL,
			glade_xml_get_widget_( info->main_window, "gveejay_window" ),
			glade_xml_get_widget_( info->main_window, "mt_box" ),
			glade_xml_get_widget_( info->main_window, "statusbar") ,
			glade_xml_get_widget_( info->main_window, "previewtoggle"),
			pw,
			ph,
			img_wid,
			(void*) gui,
			use_threads,
			num_tracks_);

	if( theme_list )
	{
		GtkWidget *menu = gtk_menu_new();
		for( i = 0; theme_list[i] != NULL ; i ++ )
		{
			GtkWidget *mi = gtk_menu_item_new_with_label( theme_list[i] );	
			gtk_menu_shell_append( GTK_MENU_SHELL( menu ), mi );
	
			g_signal_connect_swapped( G_OBJECT(mi), "activate", G_CALLBACK(theme_response),
						(gpointer) g_strdup( theme_list[i] ));

			gtk_widget_show( mi );
		}

		GtkWidget *root_menu = gtk_menu_item_new_with_label( "Themes" );
		gtk_menu_item_set_submenu( GTK_MENU_ITEM( root_menu ),  menu );

		GtkWidget *menu_bar = glade_xml_get_widget_(info->main_window, "menubar1");
		gtk_menu_shell_append( GTK_MENU_SHELL(menu_bar), root_menu);

		gtk_widget_show( root_menu );

	}
	veejay_memset( &info->watch, 0, sizeof(watchdog_t));
	info->watch.state = STATE_WAIT_FOR_USER; //
	veejay_memset(&(info->watch.p_time),0,sizeof(struct timeval));
	info->midi =  vj_midi_new( info->main_window );
	gettimeofday( &(info->time_last) , 0 );

	GtkWidget *srtbox = glade_xml_get_widget( info->main_window, "combobox_textsrt");
	set_tooltip_by_widget( srtbox, tooltips[TOOLTIP_SRTSELECT].text);  


	update_spin_range( "spin_framedelay", 1, MAX_SLOW, 0);
	update_spin_range( "spin_samplespeed", -25,25,1);
	update_slider_range( "speed_slider", -25,25,1,0);
	update_slider_range( "slow_slider",1,MAX_SLOW,1,0);


	if( load_midi )
			vj_midi_load(info->midi,midi_file);

	char slider_name[16];
	for( i = 0 ; i < MAX_UI_PARAMETERS; i ++ ) {
		snprintf(slider_name,sizeof(slider_name), "slider_p%d",i);
		GtkWidget *slider = glade_xml_get_widget( info->main_window, slider_name );
		g_signal_connect( GTK_OBJECT(slider), "scroll-event", G_CALLBACK(slider_scroll_event), (gpointer) castIntToGpointer(i) );
		update_slider_range( slider_name, 0,1,0,0);
	}

	g_signal_connect( GTK_OBJECT( glade_xml_get_widget(info->main_window, "speed_slider") ), "scroll-event",
				G_CALLBACK(speed_scroll_event), NULL );
	g_signal_connect( GTK_OBJECT( glade_xml_get_widget(info->main_window, "slow_slider") ), "scroll-event",
				G_CALLBACK(slow_scroll_event), NULL );

	GtkWidget *lw = glade_xml_get_widget_( info->main_window, "veejay_connection");

	if( geo_pos_[0] >= 0 && geo_pos_[1] >= 0 )
	
	gtk_window_move( GTK_WINDOW(lw), geo_pos_[0], geo_pos_[1] );



	char *have_snoop = getenv( "RELOADED_KEY_SNOOP" );
	if( have_snoop == NULL ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Use setenv RELOADED_KEY_SNOOP=1 to mirror veejay server keyb layout" );
	}else {
		use_key_snoop = atoi(have_snoop);
		if( use_key_snoop < 0 || use_key_snoop > 1 )
			use_key_snoop = 0;
	}
}

void	vj_gui_preview(void)
{
	gint w = 0;
	gint h = 0;
	gint tmp_w = info->el.width;
	gint tmp_h = info->el.height;

	multitrack_get_preview_dimensions( tmp_w,tmp_h, &w, &h );
	
	update_spin_value( "priout_width", w );
	update_spin_value( "priout_height", h );

	update_spin_range( "preview_width", 16, w, w);
	update_spin_range( "preview_height", 16, h, h );

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

	gint image_width = 32;
	gint image_height = 32 *ratio;
	
	info->sequence_view->w = image_width;
	info->sequence_view->h = image_height;
	gtk_widget_set_size_request_(info->quick_select, image_width, image_height );
}

void	gveejay_preview( int p )
{
	user_preview = p;
}

int	gveejay_user_preview()
{
	return user_preview;
}

int	vj_gui_reconnect(char *hostname,char *group_name, int port_num)
{
	int k = 0;
	for( k = 0; k < 4; k ++ )
		veejay_memset( info->history_tokens[k] , 0, (sizeof(int) * STATUS_TOKENS) );

	veejay_memset( info->status_tokens, 0, sizeof(int) * STATUS_TOKENS );


	if(!hostname && !group_name )
	{
		veejay_msg(0,"Invalid host/group name given");
		return 0;
	}

	if(info->client )
	{
		error_dialog("Warning", "You should disconnect first");
		return 0;
	}
	
	if(!info->client)
	{
		info->client = vj_client_alloc(0,0,0);
		if(!info->client)
		{
			return 0;
		}	
	}

	if(!vj_client_connect( info->client, hostname, group_name, port_num ) )
	{
		if(info->client)
			vj_client_free(info->client);
		info->client = NULL;
		return 0;
	}
	

	vj_msg(VEEJAY_MSG_INFO, "New connection with Veejay running on %s port %d",
		(group_name == NULL ? hostname : group_name), port_num );
	
	veejay_msg(VEEJAY_MSG_INFO, "Connection established with %s:%d (Track 0)",hostname,port_num);

	sleep(1); //@ give it some time to settle ( at least 1 frame period )

	info->status_lock = 1;
	
	single_vims( VIMS_PROMOTION );
	
	load_editlist_info();

	update_slider_value( "framerate", info->el.fps,  0 );

	veejay_memset( vims_keys_list, 0 , sizeof(vims_keys_list));
	veejay_memset( vj_event_list,  0, sizeof( vj_event_list));

	load_effectlist_info();
	reload_vimslist();
	reload_editlist_contents();
	reload_bundles();

	GtkWidget *w = glade_xml_get_widget_(info->main_window, "gveejay_window" );
	gtk_widget_show( w );

	if( geo_pos_[0] >= 0 && geo_pos_[1] >= 0 )
		gtk_window_move(GTK_WINDOW(w), geo_pos_[0], geo_pos_[1] );

	
/*	int speed = info->status_tokens[SAMPLE_SPEED];
	if( speed < 0 ) 
		info->play_direction = -1; else info->play_direction=1;
	if( speed < 0 ) speed *= -1;*/
	update_label_str( "label_hostnamex", (hostname == NULL ? group_name: hostname ) );
	update_label_i( "label_portx",port_num,0);

	info->status_lock = 0;

	multitrack_configure( info->mt,
			      info->el.fps, info->el.width, info->el.height, &preview_box_w_, &preview_box_h_ );

	vj_gui_preview();

	info->uc.reload_hint[HINT_SLIST] = 1;
	info->uc.reload_hint[HINT_CHAIN] = 1;
	info->uc.reload_hint[HINT_ENTRY] = 1;
	info->uc.reload_hint[HINT_SEQ_ACT] = 1;
	info->uc.reload_hint[HINT_HISTORY] = 1;       

	return 1;
}

static	void	veejay_stop_connecting(vj_gui_t *gui)
{
	GtkWidget *veejay_conncection_window;

	if(!gui->sensitive)
		vj_gui_enable();

	info->launch_sensitive = 0;

	veejay_conncection_window = glade_xml_get_widget(info->main_window, "veejay_connection");
	gtk_widget_hide(veejay_conncection_window);		
	GtkWidget *mw = glade_xml_get_widget_(info->main_window,"gveejay_window" );

	gtk_widget_show( mw );
	if( geo_pos_[0] >= 0 && geo_pos_[1] >= 0 )
		gtk_window_move( GTK_WINDOW(mw), geo_pos_[0], geo_pos_[1] );


}

void	reloaded_launcher(char *hostname, int port_num)
{
	info->watch.state = STATE_RECONNECT;
	put_text( "entry_hostname", hostname );
	update_spin_value( "button_portnum", port_num );
}



void			reloaded_schedule_restart()
{
	info->watch.state = STATE_STOPPED;
}

void			reloaded_restart()
{
	GtkWidget *mw = glade_xml_get_widget_(info->main_window,"gveejay_window" );
	// disable and hide mainwindow
	if(info->sensitive)
		vj_gui_disable();
	gtk_widget_hide( mw );

	vj_gui_wipe();
/*	
	//@ bring up the launcher window
	gtk_widget_show( cd );
//	info->watch.state = STATE_CONNECT;
	info->watch.state = STATE_WAIT_FOR_USER;
	info->launch_sensitive = TRUE;

	veejay_msg(VEEJAY_MSG_INFO, "Ready to make a connection to a veejay server");*/

}

gboolean		is_alive( int *do_sync )
{
	void *data = info;
	vj_gui_t *gui = (vj_gui_t*) data;

	if( gui->watch.state == STATE_PLAYING )
	{
		*do_sync = 1;
		return TRUE;
	}

	if( gui->watch.state == STATE_RECONNECT )
	{
		vj_gui_disconnect();
		gui->watch.state = STATE_CONNECT;
	}
	
	if(gui->watch.state == STATE_DISCONNECT )
	{
		gui->watch.state = STATE_STOPPED;
		vj_gui_disconnect();
		return FALSE;
	}

	if( gui->watch.state == STATE_STOPPED )
	{
		if(info->client)
			vj_gui_disconnect();
	//	reloaded_schedule_restart();
		reloaded_restart();
		*do_sync = 0;
		if( 	info->launch_sensitive == 0 ) {
			return FALSE;
		}

		return TRUE;
	//	return FALSE; 
	}

	if( gui->watch.state == STATE_QUIT )
	{
		if(info->client) vj_gui_disconnect();
		return FALSE;
	}

	if( gui->watch.state == STATE_CONNECT )
	{
		char	*remote;
		int	port;
		remote = get_text( "entry_hostname" );
		port	= get_nums( "button_portnum" );

		veejay_msg(VEEJAY_MSG_INFO, "Connecting to %s: %d", remote,port );
		if(!vj_gui_reconnect( remote, NULL, port ))
		{
			reloaded_schedule_restart();
		}
		else
		{
			info->watch.state = STATE_PLAYING;

			if( use_key_snoop ) {

#ifdef HAVE_SDL
				info->key_id = gtk_key_snooper_install( key_handler , NULL);
#endif
			}
			multrack_audoadd( info->mt, remote, port );
			multitrack_set_quality( info->mt, 1 );
			
			*do_sync = 1;
			if( user_preview ) {
				set_toggle_button( "previewtoggle", 1 );
			}
			veejay_stop_connecting(gui);
		}
	}

	if( gui->watch.state == STATE_WAIT_FOR_USER )
	{
		*do_sync = 0;
	}

	return TRUE;
}


void	vj_gui_disconnect()
{
	if(info->key_id)
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

	multitrack_close_track(info->mt);

	reloaded_schedule_restart();
	info->key_id = 0;
}

void	vj_gui_disable()
{

	int i = 0;

	while( uiwidgets[i].name != NULL )
	{
	 disable_widget( uiwidgets[i].name );
	 i++;
	}

	info->sensitive = 0;
}	



void	vj_gui_enable()
{
	int i =0;
	while( uiwidgets[i].name != NULL)
	{
		enable_widget( uiwidgets[i].name );
		 i++;
	}
	info->sensitive = 1;
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
	sprintf(frame_label, "Slots %d to %d",
		(bank_num * NUM_SAMPLES_PER_PAGE), (bank_num * NUM_SAMPLES_PER_PAGE) + NUM_SAMPLES_PER_PAGE  );

	setup_samplebank( NUM_SAMPLES_PER_COL, NUM_SAMPLES_PER_ROW, info->sample_bank_pad, &(info->image_dimensions[0]),
				&(info->image_dimensions[1]) );

	info->sample_banks[bank_num] = (sample_bank_t*) vj_calloc(sizeof(sample_bank_t));
	info->sample_banks[bank_num]->bank_number = bank_num;
	sample_slot_t **slot = (sample_slot_t**) vj_calloc(sizeof(sample_slot_t*) * NUM_SAMPLES_PER_PAGE);
	sample_gui_slot_t **gui_slot = (sample_gui_slot_t**) vj_calloc(sizeof(sample_gui_slot_t*) * NUM_SAMPLES_PER_PAGE );

	int j;
	for(j = 0;j < NUM_SAMPLES_PER_PAGE; j ++ ) 
	{
		slot[j] = (sample_slot_t*) vj_calloc(sizeof(sample_slot_t) );	
		gui_slot[j] = (sample_gui_slot_t*) vj_calloc(sizeof(sample_gui_slot_t));
//		slot[j]->rawdata = (guchar*) vj_calloc(sizeof(guchar) * 3 * 128 * 128 ); 
		slot[j]->slot_number = j;
		slot[j]->sample_id = -1;
		slot[j]->sample_type = -1;
	}

	info->sample_banks[bank_num]->slot = slot;
	info->sample_banks[bank_num]->gui_slot = gui_slot;

	GtkWidget *sb = info->sample_bank_pad;
	GtkWidget *frame = gtk_frame_new(frame_label);
	GtkWidget *label = gtk_label_new( str_label );

	gtk_container_set_border_width( GTK_CONTAINER( frame), 0 );

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
	    			gtk_table_attach_defaults ( GTK_TABLE(table), gui_slot->event_box, row, row+1, col, col+1); 
				set_tooltip_by_widget( gui_slot->frame, tooltips[TOOLTIP_SAMPLESLOT].text);  
			}
		}
	}


	if(!info->fg_)
	{
		info->fg_ = widget_get_fg( GTK_WIDGET(info->sample_banks[bank_num]->gui_slot[0]->frame) );
	}
	return bank_num;
}


void	reset_samplebank(void)
{
	info->selection_slot = NULL;
	info->selection_gui_slot = NULL;
	info->selected_slot = NULL;
	info->selected_gui_slot = NULL;
	int i,j;
	for( i = 0; i < NUM_BANKS; i ++ )
	{
		if(info->sample_banks[i])
		{
			/* clear memory in use */
			for(j = 0; j < NUM_SAMPLES_PER_PAGE ; j ++ )
			{
				sample_slot_t *slot = info->sample_banks[i]->slot[j];
			
				if(slot->sample_id)
				{		
					if(slot->title) free(slot->title);
					if(slot->timecode) free(slot->timecode);
		//			if(slot->pixbuf) g_object_unref( slot->pixbuf );
					slot->title = NULL;
					slot->timecode = NULL;
					slot->sample_id = 0;
					slot->sample_type = 0;
				}
				update_sample_slot_data( i,j, slot->sample_id,slot->sample_type,slot->title,slot->timecode);
			}
		}
	}
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
	//			if(slot->pixbuf) g_object_unref(slot->pixbuf);
	//			if(slot->rawdata) free(slot->rawdata);
				free(slot);
				free(gslot);
				info->sample_banks[i]->slot[j] = NULL;
				info->sample_banks[i]->gui_slot[j] = NULL;
			}			
			free(info->sample_banks[i]);
			info->sample_banks[i] = NULL;

		}
	}
	veejay_memset( info->sample_banks, 0, sizeof(sample_bank_t*) * NUM_BANKS );
}
#define RUP8(num)(((num)+8)&~8)


//@ OK
void setup_samplebank(gint num_cols, gint num_rows, GtkWidget *pad, int *idx, int *idy)
{
	GdkRectangle result;
	if(info->el.width <= 0 || info->el.height <= 0 ) {
		*idx = 0;
		*idy = 0;
		return;
	}
	else {
		widget_get_rect_in_screen(
			pad,
			&result
		);
		result.width -= ( num_rows * 16);	
		result.height -= ( num_cols * 16);
		gint image_width = result.width / num_rows;

		float ratio = (float) info->el.height / (float) info->el.width;

		gfloat w = image_width;
		gfloat h = image_width * ratio;

		*idx = (int)w;
		*idy = (int)h;

	}
	veejay_msg(VEEJAY_MSG_INFO, "Sample bank image dimensions: %dx%d", *idx,*idy);
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

static	sample_slot_t *find_slot_by_sample( int sample_id , int sample_type )
{
	char key[32];
	sprintf(key, "S%04d%02d",sample_id, sample_type );

	void *slot = NULL;
	vevo_property_get( bankport_, key, 0,&slot );
	if(!slot)
		return NULL;
	return (sample_slot_t*) slot;
}
static	sample_gui_slot_t *find_gui_slot_by_sample( int sample_id , int sample_type )
{
	char key[32];
	sprintf(key, "G%04d%02d",sample_id, sample_type );

	void *slot = NULL;
	vevo_property_get( bankport_, key, 0,&slot );
	if(!slot)
		return NULL;
	return (sample_gui_slot_t*) slot;
}

static	int	find_bank_by_sample(int sample_id, int sample_type, int *slot )
{
	int i,j;

	for( i = 0; i < NUM_BANKS; i ++ )
	{
		if(!info->sample_banks[i]) {
			continue;
		}

		for( j = 0; j < NUM_SAMPLES_PER_PAGE; j ++ )
		{
			if(info->sample_banks[i]->slot[j]->sample_id == sample_id &&
			   info->sample_banks[i]->slot[j]->sample_type == sample_type) 
			{
				*slot = j;
#ifdef STRICT_CHECKING
				veejay_msg(VEEJAY_MSG_DEBUG, "using existing slot (%d,%d)",
						sample_id,sample_type );
#endif
				return i;
			}
		}
	}

	for( i = 0; i < NUM_BANKS; i ++ )
	{
		if(!info->sample_banks[i]) {
			*slot = 0;
			return i;
		}

		for( j = 0; j < NUM_SAMPLES_PER_PAGE; j ++ )
		{
			 if ( info->sample_banks[i]->slot[j]->sample_id <= 0) 
			{
				*slot = j;
#ifdef STRICT_CHECKING
				veejay_msg(VEEJAY_MSG_DEBUG, "using new slot (%d,%d)",
							sample_id,sample_type);
#endif
				return i;
			}
		}
	}

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
}

static void set_activation_of_cache_slot_in_samplebank( sequence_gui_slot_t *gui_slot, gboolean activate)
{
	if (activate)
	{
		gtk_frame_set_shadow_type(GTK_FRAME(gui_slot->frame),GTK_SHADOW_IN);
	}
	else {
		gtk_frame_set_shadow_type(GTK_FRAME(gui_slot->frame),GTK_SHADOW_ETCHED_IN);
	}
}	

static gboolean on_sequencerslot_activated_by_mouse(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	gint slot_nr = GPOINTER_TO_INT(user_data);
	
	if( event->type == GDK_BUTTON_PRESS && (event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK )
	{
		multi_vims( VIMS_SEQUENCE_DEL, "%d", slot_nr );
		gtk_label_set_text( GTK_LABEL(info->sequencer_view->gui_slot[slot_nr]->image),
				NULL );
	}
	else
	if(event->type == GDK_BUTTON_PRESS)
	{
		int id = info->status_tokens[CURRENT_ID];
		if( info->selection_slot )
			id = info->selection_slot->sample_id;
		multi_vims( VIMS_SEQUENCE_ADD, "%d %d", slot_nr, id );
		info->uc.reload_hint[HINT_SEQ_ACT] = 1;
	}
	return FALSE;
}		

static gboolean on_cacheslot_activated_by_mouse (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	gint slot_nr = -1;
	if(info->status_tokens[PLAY_MODE] == MODE_PLAIN )
		return FALSE;

	slot_nr =GPOINTER_TO_INT( user_data );
	set_activation_of_cache_slot_in_samplebank( info->sequence_view->gui_slot[slot_nr], FALSE );

	if( event->type == GDK_BUTTON_PRESS && (event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK )
	{
		info->current_sequence_slot = slot_nr;
		sample_slot_t *s = info->selected_slot;
		sequence_gui_slot_t *g = info->sequence_view->gui_slot[slot_nr];
#ifdef STRICT_CHECKING
		assert( s != NULL );
		assert( g != NULL );
#endif
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
			return FALSE;

		}
		multi_vims(VIMS_SET_MODE_AND_GO, "%d %d", g->sample_type, g->sample_id );
		vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_SET_MODE_AND_GO, g->sample_type,g->sample_id );
	}
	return FALSE;
}		



static void create_sequencer_slots(int nx, int ny)
{
	GtkWidget *vbox = glade_xml_get_widget_ (info->main_window, "SampleSequencerBox");
	info->sample_sequencer = gtk_frame_new(NULL);
	gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET(info->sample_sequencer), TRUE, TRUE, 0);
	gtk_widget_show(info->sample_sequencer);

	info->sequencer_view = (sequence_envelope*) vj_calloc(sizeof(sequence_envelope) );
	info->sequencer_view->gui_slot = (sequence_gui_slot_t**) vj_calloc(sizeof(sequence_gui_slot_t*) * ( nx * ny + 1 ) );

	GtkWidget *table = gtk_table_new( nx, ny, TRUE );	
	
	gtk_container_add( GTK_CONTAINER(info->sample_sequencer), table );
	gtk_widget_show(table);

	gint col=0;
	gint row=0;
	gint k = 0;
	for( col = 0; col < ny; col ++ )
	for( row = 0; row < nx; row ++ )
	{
		sequence_gui_slot_t *gui_slot = (sequence_gui_slot_t*)vj_calloc(sizeof(sequence_gui_slot_t));
		info->sequencer_view->gui_slot[k] = gui_slot;

		gui_slot->event_box = gtk_event_box_new();
		gtk_event_box_set_visible_window(GTK_EVENT_BOX(gui_slot->event_box), TRUE);
		GTK_WIDGET_SET_FLAGS(gui_slot->event_box,GTK_CAN_FOCUS);	
		
		g_signal_connect( G_OBJECT(gui_slot->event_box),
			"button_press_event",
			G_CALLBACK(on_sequencerslot_activated_by_mouse), //@@@@
			(gpointer) castIntToGpointer(k)
			);	    
		gtk_widget_show(GTK_WIDGET(gui_slot->event_box));	

		gui_slot->frame = gtk_frame_new(NULL);
		gtk_container_set_border_width (GTK_CONTAINER(gui_slot->frame),0);
		gtk_frame_set_shadow_type(GTK_FRAME( gui_slot->frame), GTK_SHADOW_IN );
		gtk_widget_show(GTK_WIDGET(gui_slot->frame));
		gtk_container_add (GTK_CONTAINER (gui_slot->event_box), gui_slot->frame);

		/* the slot main container */
		gui_slot->main_vbox = gtk_vbox_new(FALSE,0);
		gtk_container_add (GTK_CONTAINER (gui_slot->frame), gui_slot->main_vbox);
		gtk_widget_show( GTK_WIDGET(gui_slot->main_vbox) );
		
		gui_slot->image = gtk_label_new(NULL);
		gtk_box_pack_start (GTK_BOX (gui_slot->main_vbox), GTK_WIDGET(gui_slot->image), TRUE, TRUE, 0);
		gtk_widget_show( gui_slot->image);
		gtk_table_attach_defaults ( GTK_TABLE(table), gui_slot->event_box, row, row+1, col, col+1);  
		k++;
	}
//	gtk_widget_set_size_request_( table, 300,300);
//	info->sequencer_view->envelope_size = envelope_size;
}


static void create_ref_slots(int envelope_size)
{
	gchar frame_label[50];
	GtkWidget *vbox = glade_xml_get_widget_ (info->main_window, "quickselect");
	info->quick_select = gtk_frame_new(NULL);
	gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET(info->quick_select), TRUE, TRUE, 0);
	gtk_widget_show(info->quick_select);
	info->sequence_view = (sequence_envelope*) vj_calloc(sizeof(sequence_envelope) );
	info->sequence_view->gui_slot = (sequence_gui_slot_t**) vj_calloc(sizeof(sequence_gui_slot_t*) * envelope_size );
	sprintf(frame_label, "Last played" );
	GtkWidget *table = gtk_table_new( 1, envelope_size, TRUE );	
	gtk_container_add( GTK_CONTAINER(info->quick_select), table );
	gtk_widget_show(table);

	gint col=0;
	gint row=0;
	for( row = 0; row < envelope_size; row ++ )
	{
		sequence_gui_slot_t *gui_slot = (sequence_gui_slot_t*)vj_calloc(sizeof(sequence_gui_slot_t));
		info->sequence_view->gui_slot[row] = gui_slot;
		gui_slot->event_box = gtk_event_box_new();
		gtk_event_box_set_visible_window(GTK_EVENT_BOX(gui_slot->event_box), TRUE);
		GTK_WIDGET_SET_FLAGS(gui_slot->event_box,GTK_CAN_FOCUS);	
		/* Right mouse button is popup menu, click = play */
		g_signal_connect( G_OBJECT(gui_slot->event_box),
			"button_press_event",
			G_CALLBACK(on_cacheslot_activated_by_mouse),
			(gpointer) castIntToGpointer(row)
			);	    
		gtk_widget_show(GTK_WIDGET(gui_slot->event_box));	
		/* the surrounding frame for each slot */
		gui_slot->frame = gtk_frame_new(NULL);
		set_tooltip_by_widget(gui_slot->frame, tooltips[TOOLTIP_QUICKSELECT].text );
		gtk_container_set_border_width (GTK_CONTAINER(gui_slot->frame),1);
		gtk_widget_show(GTK_WIDGET(gui_slot->frame));
		gtk_container_add (GTK_CONTAINER (gui_slot->event_box), gui_slot->frame);

		/* the slot main container */
		gui_slot->main_vbox = gtk_vbox_new(FALSE,0);
		gtk_container_add (GTK_CONTAINER (gui_slot->frame), gui_slot->main_vbox);
		gtk_widget_show( GTK_WIDGET(gui_slot->main_vbox) );
		
		/* The sample's image */
		gui_slot->image = gtk_image_new();
		gtk_box_pack_start (GTK_BOX (gui_slot->main_vbox), GTK_WIDGET(gui_slot->image), TRUE, TRUE, 0);
//		gtk_widget_set_size_request_( gui_slot->image, info->sequence_view->w,info->sequence_view->h );
		gtk_widget_show( GTK_WIDGET(gui_slot->image));

		gtk_table_attach_defaults ( GTK_TABLE(table), gui_slot->event_box, row, row+1, col, col+1);   



	}
	info->sequence_view->envelope_size = envelope_size;
}

static void create_slot(gint bank_nr, gint slot_nr, gint w, gint h)
{
	gchar hotkey[3];

	sample_bank_t **sample_banks = info->sample_banks;	
	sample_gui_slot_t *gui_slot = sample_banks[bank_nr]->gui_slot[slot_nr];
	
	// to reach clicks on the following GUI-Elements of one slot, they are packed into an event_box
	gui_slot->event_box = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(gui_slot->event_box), TRUE);

	GTK_WIDGET_SET_FLAGS(gui_slot->event_box,GTK_CAN_FOCUS);	
	g_signal_connect( G_OBJECT(gui_slot->event_box),
		"button_press_event",
		G_CALLBACK(on_slot_activated_by_mouse),
		(gpointer) castIntToGpointer(slot_nr)
		);	    
	gtk_widget_show(GTK_WIDGET(gui_slot->event_box));	
	/* the surrounding frame for each slot */
	gui_slot->frame = gtk_frame_new(NULL);
		
	gtk_container_set_border_width (GTK_CONTAINER(gui_slot->frame),0);
	gtk_widget_show(GTK_WIDGET(gui_slot->frame));
	gtk_container_add (GTK_CONTAINER (gui_slot->event_box), GTK_WIDGET(gui_slot->frame));


	/* the slot main container */
	gui_slot->main_vbox = gtk_vbox_new(FALSE,0);
	gtk_container_add (GTK_CONTAINER (gui_slot->frame), gui_slot->main_vbox);
	gtk_widget_show( GTK_WIDGET(gui_slot->main_vbox) );


	gui_slot->image = gtk_image_new();
//	gui_slot->image = gtk_drawing_area_new();
	gtk_box_pack_start (GTK_BOX (gui_slot->main_vbox), GTK_WIDGET(gui_slot->image), TRUE, TRUE, 0);
//	gtk_widget_show(GTK_WIDGET(gui_slot->image));
	gtk_widget_set_size_request_( gui_slot->image, info->image_dimensions[0],info->image_dimensions[1] );
/*	g_signal_connect( gui_slot->image, "expose_event",
			G_CALLBACK(image_expose_event), 
			(gpointer) info->sample_banks[bank_nr]->slot[slot_nr]->slot_number  );
*/	gtk_widget_show( GTK_WIDGET(gui_slot->image));

	/* the upper container for all slot-informations */
	gui_slot->upper_hbox = gtk_hbox_new(FALSE,0);
	gtk_box_pack_start (GTK_BOX (gui_slot->main_vbox), gui_slot->upper_hbox, FALSE, TRUE, 0);
	gtk_widget_show(GTK_WIDGET(gui_slot->upper_hbox));


	if( sample_banks[bank_nr]->slot[slot_nr]->sample_type >= 0 )
	{ 
		/* the hotkey that is assigned to this slot */
		sprintf(hotkey, "F-%d", (slot_nr+1));	
		gui_slot->hotkey = gtk_label_new(hotkey);
	}
	else
	{
		gui_slot->hotkey = gtk_label_new("");
	}
	gtk_misc_set_alignment(GTK_MISC(gui_slot->hotkey), 0.0, 0.0);
	gtk_misc_set_padding (GTK_MISC(gui_slot->hotkey), 0, 0);	
	gtk_box_pack_start (GTK_BOX (gui_slot->upper_hbox), GTK_WIDGET(gui_slot->hotkey), FALSE, FALSE, 0);
	gtk_widget_show(GTK_WIDGET(gui_slot->hotkey));
	gui_slot->upper_vbox = gtk_vbox_new(FALSE,0);
	gtk_box_pack_start (GTK_BOX (gui_slot->upper_hbox), gui_slot->upper_vbox, TRUE, TRUE, 0);
	gtk_widget_show(GTK_WIDGET(gui_slot->upper_vbox));
	gui_slot->title = gtk_label_new("");
	
	gui_slot->timecode = gtk_label_new("");
	gtk_misc_set_alignment(GTK_MISC(gui_slot->timecode), 0.5, 0.0);
	gtk_misc_set_alignment(GTK_MISC(gui_slot->title), 0.5, 0.0);

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

	slot_nr = GPOINTER_TO_INT(user_data);
	sample_bank_t **sample_banks = info->sample_banks;

	if( info->sample_banks[ bank_nr ]->slot[ slot_nr ]->sample_id <= 0 )
		return FALSE;

	if( event->type == GDK_2BUTTON_PRESS )
	{
		sample_slot_t *s = sample_banks[bank_nr]->slot[slot_nr];
		multi_vims( VIMS_SET_MODE_AND_GO, "%d %d", (s->sample_type==0? 0:1), s->sample_id);
		vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_SET_MODE_AND_GO, s->sample_type, s->sample_id );
		vj_msg(VEEJAY_MSG_INFO, "Start playing %s %d",
				(s->sample_type==0 ? "Sample" : "Stream" ), s->sample_id );
	}
	else if(event->type == GDK_BUTTON_PRESS )
	{
		if( (event->state & GDK_SHIFT_MASK ) == GDK_SHIFT_MASK ) {
			sample_slot_t *x = sample_banks[bank_nr]->slot[slot_nr];
		   	multi_vims( VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
				"%d %d %d %d",
				0,
				info->uc.selected_chain_entry,
				x->sample_type,
				x->sample_id );

			if(x->sample_id == 1 ) {
				vj_msg(VEEJAY_MSG_INFO, "Set mixing channel %d to Stream %d", info->uc.selected_chain_entry,
				x->sample_id );	
			} else {
			    	 vj_msg(VEEJAY_MSG_INFO, "Set mixing channel %d to Sample %d", info->uc.selected_chain_entry,
				x->sample_id);
			}

			char trip[100];
			snprintf(trip, sizeof(trip), "%03d:%d %d %d %d",VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
				0,
				info->uc.selected_chain_entry,
				x->sample_type,
				x->sample_id );

			vj_midi_learning_vims( info->midi, NULL, trip, 0 );
		} else {
			if(info->selection_slot)
				set_selection_of_slot_in_samplebank(FALSE);
			info->selection_slot = sample_banks[bank_nr]->slot[slot_nr];
			info->selection_gui_slot = sample_banks[bank_nr]->gui_slot[slot_nr];
			set_selection_of_slot_in_samplebank(TRUE );
		}
	}
	return FALSE;

}


static	void	indicate_sequence( gboolean active, sequence_gui_slot_t *slot )
{
	if(!active)
		gtk_frame_set_shadow_type( GTK_FRAME(slot->frame), GTK_SHADOW_IN );
	else
		gtk_frame_set_shadow_type( GTK_FRAME(slot->frame), GTK_SHADOW_OUT );
}

/* --------------------------------------------------------------------------------------------------------------------------
 *  Function that handles to select/activate a special slot in the samplebank
   -------------------------------------------------------------------------------------------------------------------------- */

void 	set_widget_color( GtkWidget *widget , int red, int green, int blue, int def )
{
	GdkColor color;
	if( def ) {
		color.red = info->fg_->red;
		color.green = info->fg_->green;
		color.blue = info->fg_->blue;
	} else {
		color.red = red;
		color.green = green;
		color.blue = blue;
	}
	gtk_widget_modify_fg ( GTK_WIDGET(widget),GTK_STATE_NORMAL, &color );
}

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
		gtk_frame_set_shadow_type( GTK_FRAME(info->selected_gui_slot->frame), GTK_SHADOW_ETCHED_IN );
	}
	else
	{
		if (activate)
		{
			color.green = 0xffff;
			color.red = 0;
			color.blue =0;
			gtk_frame_set_shadow_type(GTK_FRAME(info->selected_gui_slot->frame),GTK_SHADOW_IN);
			gtk_widget_grab_focus(GTK_WIDGET(info->selected_gui_slot->frame));
		}
		else 
		{
			gtk_frame_set_shadow_type(GTK_FRAME(info->selected_gui_slot->frame),GTK_SHADOW_ETCHED_IN);
		}
	}

	gtk_widget_modify_fg ( GTK_WIDGET(info->selected_gui_slot->timecode),
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
//	gtk_widget_modify_fg ( GTK_WIDGET(info->selection_gui_slot->title),
//		GTK_STATE_NORMAL, &color );
	gtk_widget_modify_fg ( GTK_WIDGET(info->selection_gui_slot->timecode),
		GTK_STATE_NORMAL, &color );
//	gtk_widget_modify_fg ( gtk_frame_get_label_widget( info->selection_gui_slot->frame ),
//		GTK_STATE_NORMAL, &color );

}

static int add_sample_to_sample_banks(int bank_page,sample_slot_t *slot)
{
	int bp = 0; int s = 0;
#ifdef STRICT_CHECKING
	
	int result = verify_bank_capacity( &bp, &s, slot->sample_id, slot->sample_type );

	
	veejay_msg(VEEJAY_MSG_DEBUG, "add slot on page %d: type=%d id=%d. result=%d", bank_page,slot->sample_type,slot->sample_id,result );

	if( result ) 
		update_sample_slot_data( bp, s, slot->sample_id,slot->sample_type,slot->title,slot->timecode);

#else
       if(verify_bank_capacity( &bp, &s, slot->sample_id, slot->sample_type ))
	       update_sample_slot_data( bp, s, slot->sample_id,slot->sample_type,slot->title,slot->timecode);
#endif	
 
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
	// decrement history of delete type
	int *his = info->history_tokens[ (info->status_tokens[PLAY_MODE]) ];
	
	his[TOTAL_SLOTS] = his[TOTAL_SLOTS] - 1;
	update_sample_slot_data( bank_nr, slot_nr, 0, -1, NULL, NULL); 	 	

	set_selection_of_slot_in_samplebank( FALSE );
	info->selection_gui_slot = NULL;
	info->selection_slot = NULL;
}


/* --------------------------------------------------------------------------------------------------------------------------
 *  Function adds the given infos to the list of effect-sources
   -------------------------------------------------------------------------------------------------------------------------- */ 
static void add_sample_to_effect_sources_list(gint id, gint type, gchar *title, gchar *timecode)
{
	gchar id_string[512];
	GtkTreeIter iter;	

	if (type == STREAM_NO_STREAM) 
		sprintf( id_string, "S[%4d] %s", id, title);    
	else sprintf( id_string, "T[%4d]", id);

	gtk_list_store_append( effect_sources_store, &iter );
	gtk_list_store_set( effect_sources_store, &iter, SL_ID, id_string, SL_DESCR, title, SL_TIMECODE , timecode,-1 );

	GtkTreeIter iter2;
	if(type == 1 && strncmp("bogus",title, 7)==0) {
		gtk_list_store_append( cali_sourcestore,&iter2);
		gtk_list_store_set( cali_sourcestore,&iter2,SL_ID, id_string,SL_DESCR,title,SL_TIMECODE,timecode,-1);
	}
}

/*
	Update a slot, either set from function arguments or clear it
 */
static void update_sample_slot_data(int page_num, int slot_num, int sample_id, gint sample_type, gchar *title, gchar *timecode)
{
	sample_slot_t *slot = info->sample_banks[page_num]->slot[slot_num];
	sample_gui_slot_t *gui_slot = info->sample_banks[page_num]->gui_slot[slot_num];

	if(slot->timecode) free(slot->timecode);
	if(slot->title) free(slot->title);
	
    	slot->sample_id = sample_id;
    	slot->sample_type = sample_type;
	slot->timecode = timecode == NULL ? strdup("") : strdup( timecode );
	slot->title = title == NULL ? strdup("") : strdup( title );

	if( sample_id )
	{
		char sample_key[32];
		sprintf(sample_key, "S%04d%02d", sample_id, sample_type );
		vevo_property_set( bankport_, sample_key, VEVO_ATOM_TYPE_VOIDPTR,1, &slot );
		sprintf(sample_key, "G%04d%02d", sample_id, sample_type );
		vevo_property_set( bankport_, sample_key, VEVO_ATOM_TYPE_VOIDPTR,1,&gui_slot);
		add_sample_to_effect_sources_list(sample_id, sample_type, title, timecode);
	}

	if(gui_slot)
	{
		if(gui_slot->title)
			gtk_label_set_text( GTK_LABEL( gui_slot->title ), slot->title );
		if(gui_slot->timecode)
			gtk_label_set_text( GTK_LABEL( gui_slot->timecode ), slot->timecode );

		if(sample_id > 0 )
		{
			gtk_frame_set_label( GTK_FRAME(gui_slot->frame),slot->title );
		}
		else
		{
			gtk_frame_set_label(GTK_FRAME(gui_slot->frame), NULL );
		}
	}

	if( sample_id == 0 )
	{
/*		if(slot->pixbuf)
		{
			g_object_unref( slot->pixbuf );
			slot->pixbuf = NULL;
		} */
	}
}

void	veejay_release_track(int id, int release_this)
{
	multitrack_release_track( info->mt, id, release_this );
}

void	veejay_bind_track( int id, int bind_this )
{
	multitrack_bind_track(info->mt, id, bind_this );
	info->uc.reload_hint[HINT_SLIST]  =1;

 
}
