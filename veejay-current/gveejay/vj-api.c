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
#include <gveejay/vj-api.h>
#include <fcntl.h>
#include <utils/mpegconsts.h>
#include <utils/mpegtimecode.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <widgets/cellrendererspin.h>
#include <libgen.h>
#include <gveejay/keyboard.h>
#include <gtk/gtkversion.h>


//if gtk2_6 is not defined, 2.4 is assumed.
#ifdef GTK_CHECK_VERSION
#if GTK_MINOR_VERSION >= 6
  #define HAVE_GTK2_6 1
#endif  
#endif

static int	TIMEOUT_SECONDS = 0;
#define STATUS_BYTES 	100
#define STATUS_TOKENS 	16

/* Status bytes */

#define ELAPSED_TIME	0
#define PLAY_MODE	2
#define CURRENT_ID	3
#define SAMPLE_FX	4
#define SAMPLE_START	5
#define SAMPLE_END	6
#define SAMPLE_SPEED	7
#define SAMPLE_LOOP	8
#define FRAME_NUM	1
#define TOTAL_FRAMES	6
#define TOTAL_SAMPLES	12
#define	MODE_PLAIN	2
#define MODE_SAMPLE	0
#define MODE_STREAM	1
#define STREAM_COL_R	5
#define STREAM_COL_G	6
#define STREAM_COL_B	7

#define STREAM_RECORDED  11
#define STREAM_DURATION  10
#define STREAM_RECORDING 9

/* Stream type identifiers */

void	vj_gui_set_timeout(int timer)
{
	TIMEOUT_SECONDS = timer;
}

enum
{
	STREAM_RED = 9,
	STREAM_GREEN = 8,
	STREAM_YELLOW = 7,
	STREAM_BLUE = 6,
	STREAM_BLACK = 5,
	STREAM_WHITE = 4,
	STREAM_VIDEO4LINUX = 2,
	STREAM_DV1394 = 17,
	STREAM_NETWORK = 13,
	STREAM_MCAST = 14,
	STREAM_YUV4MPEG = 1,
	STREAM_AVFORMAT = 12,
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
	HINT_CLIPLIST = 3,
	HINT_ENTRY = 4,
	HINT_CLIP = 5,
	HINT_SLIST = 6,
	HINT_V4L = 7,
	HINT_RECORDING = 8,
	HINT_RGBSOLID = 9,
	HINT_BUNDLES = 10,
	HINT_HISTORY = 11,
	HINT_MARKER = 12,
	NUM_HINTS = 13
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
	int bind;
	int start;
	int end;
	int lock;
	int lower_bound;
	int upper_bound;
} sample_marker_t;

typedef struct
{
	int	selected_chain_entry;
	int	selected_el_entry;
	int	selected_vims_entry;
	int	selected_sample_id;
	int	selected_stream_id;
	int	selected_history_entry;
	int	selected_key_mod;
	int	selected_key_sym;
	int	selected_vims_id;
	int	render_record; 
	int	entry_tokens[STATUS_TOKENS];
	int	entry_history[STATUS_TOKENS];
	int	iterator;
	int	selected_effect_id;
	int	reload_hint[NUM_HINTS];
	int	playmode;
	int	sample_rec_duration;
	int	previous_playmode;
	int	streams[4096];
	int	current_sample_id;
	int	current_stream_id;
	int	list_length[2];
	int	last_list_length[2];
	int	recording[2];
	int	selected_mix_sample_id;
	int	selected_mix_stream_id;
	int	selected_rgbkey;
	int	priout_lock;
	int	pressed_key;
	int	pressed_mod;
	int     keysnoop;
	char	*selected_arg_buf;
	stream_templ_t	strtmpl[2]; // v4l, dv1394
	sample_marker_t marker;
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

static struct
{
	int pm;
	const char *name;
} notepad_widgets[] =
{ 
	{MODE_SAMPLE, "frame_sampleproperties"},
	{MODE_SAMPLE, "frame_samplerecord"},
	{MODE_SAMPLE, "tree_history"},
	{MODE_SAMPLE, "button_historymove"},
	{MODE_SAMPLE, "button_historyrec"},
	{MODE_STREAM, "frame_streamproperties"},
	{MODE_STREAM, "frame_streamrecord"},	
	{-1	,	"vbox_fxtree" },
	{ 0,	NULL	},
};

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

static	vims_t	vj_event_list[VIMS_MAX];
static  vims_verbosity = 0;

typedef struct
{
	GladeXML *main_window;
	vj_client	*client;
	char		status_msg[STATUS_BYTES];
	int		status_tokens[STATUS_TOKENS]; 	/* current status tokens */
	int		*history_tokens[3];		/* list last known status tokens */
	int		status_lock;
	int		slider_lock;
   	int		parameter_lock;
	int		entry_lock;
	int		sample[2];
	int		selection[3];
	gint		status_pipe;
	int		state;
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
	veejay_el_t	el;
	veejay_user_ctrl_t uc;
	GList		*effect_info;
	GList		*devlist;
	GList		*chalist;
	GList		*editlist;
	GList		*elref;
	long		window_id;
	int		run_state;
} vj_gui_t;

enum
{
 	STATE_STOPPED = 0,
	STATE_RECONNECT = 1,
	STATE_PLAYING  = 2,
	STATE_IDLE	= 3
};

enum
{
	FXC_ID = 0,
	FXC_FXID,
	FXC_FXSTATUS,
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

static	int	get_slider_val(const char *name);
static  void    vj_msg(int type, const char format[], ...);
static  void    vj_msg_detail(int type, const char format[], ...);
static	void	msg_vims(char *message);
static  void    multi_vims(int id, const char format[],...);
static  void 	single_vims(int id);
static	gdouble	get_numd(const char *name);
static  int     get_nums(const char *name);
static  gchar   *get_text(const char *name);
static	void	put_text(const char *name, char *text);
static  int     is_button_toggled(const char *name);
static	void	set_toggle_button(const char *name, int status);
static  void	update_slider_gvalue(const char *name, gdouble value );
static  void    update_slider_value(const char *name, gint value, gint scale);
static  void    update_slider_range(const char *name, gint min, gint max, gint value, gint scaled);
static	void	update_spin_range(const char *name, gint min, gint max, gint val);
static	void	update_spin_value(const char *name, gint value);
static  void    update_label_i(const char *name, int num, int prefix);
static	void	update_label_f(const char *name, float val);
static	void	update_label_str(const char *name, gchar *text);
static	void	update_globalinfo();
static  void    update_sampleinfo();
static  void    update_streaminfo();
static  void    update_plaininfo();
static	void	load_parameter_info();
static	void	load_v4l_info();
static	void	reload_editlist_contents();
static  void    load_effectchain_info();
static  void    load_effectlist_info();
static  void    load_samplelist_info(const char *name);
static  void    load_editlist_info();
static	int	get_page(const char *name);
static	void	set_page(const char *name, int p);
static	void	disable_widget( const char *name );
static	void	enable_widget(const char *name );
static	void	setup_tree_spin_column(const char *tree_name, int type, const char *title);
static	void	setup_tree_text_column( const char *tree_name, int type, const char *title );
static	void	setup_tree_pixmap_column( const char *tree_name, int type, const char *title );
static	gchar	*_utf8str( char *c_str );
static gchar	*recv_vims(int len, int *bytes_written);
void	vj_gui_stop_launch();
static	void	get_gd(char *buf, char *suf, const char *filename);

int	resize_primary_ratio_y();
int	resize_primary_ratio_x();
static	void	setup_tree_texteditable_column( const char *tree_name, int type, const char *title, void (*callbackfunction)() );
static	void	update_rgbkey();
static	int	count_textview_buffer(const char *name);
static	void	clear_textview_buffer(const char *name);
static	void	init_recorder(int total_frames, gint mode);
static	void	reload_bundles();
static void	reload_hislist();
static	void	update_rgbkey_from_slider();
void	vj_launch_toggle(gboolean value);
static	gchar	*get_textview_buffer(const char *name);

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
/*

	todo:
	* generalize tree view usage (cleans up a lot of code)
	

 */
void	on_samplelist_edited(GtkCellRendererText *cell,
		gchar *path_string,
		gchar *new_text,
		gpointer user_data);

static	gchar	*_utf8str(char *c_str)
{
	gint	bytes_read;
	gint 	bytes_written;
	GError	*error = NULL;
	if(!c_str)
		return NULL;
	gchar	*result = g_locale_to_utf8( c_str, -1, &bytes_read, &bytes_written, &error );

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
		fprintf(stderr,"gveejay fatal: widget %s does not exist\n",name);
		exit(0);
	
	}
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
			info->uc.reload_hint[HINT_SLIST] = 1;
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



static	gchar*	format_time(int frame_num);
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
		for(i = 0; i <= n; i ++ )
			_el_ref_free( g_list_nth_data( info->elref, i ) );
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

gboolean	is_alive(gpointer data);

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
				parent_window,
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


gchar *dialog_open_file(const char *title)
{
	GtkWidget *parent_window = glade_xml_get_widget_(
			info->main_window, "gveejay_window" );
	GtkWidget *dialog = 
		gtk_file_chooser_dialog_new( title,
				parent_window,
				GTK_FILE_CHOOSER_ACTION_OPEN,
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



void	about_dialog()
{
    const gchar *artists[] = { 
      "Matthijs v. Henten (glade, pixmaps) <cola@looze.net>", 
      "Dursun Koca (logo)",
      NULL 
    };

    const gchar *authors[] = { 
      "Niels Elburg <nelburg@looze.net>", 
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
		"name", "GVeejay",
		"version", VERSION,
		"copyright", "(C) 2004 - 2005 N. Elburg et all.",
		"comments", "A graphical interface for Veejay",
		"authors", authors,
		"artists", artists,
		"license", license,
		"logo", pixbuf, NULL );
	g_object_unref(pixbuf);

	g_signal_connect( about , "response", G_CALLBACK( gtk_widget_destroy),NULL);
	gtk_window_present( GTK_WINDOW( about ) );
#else
	int i;
	vj_msg( VEEJAY_MSG_INFO, "%s", license );
	for(i = 0; artists[i] != NULL ; i ++ )
		vj_msg_detail( VEEJAY_MSG_INFO, "%s", artists[i] );
	for(i = 0; authors[i] != NULL ; i ++ )
		vj_msg_detail( VEEJAY_MSG_INFO, "%s", authors[i] );
	vj_msg_detail( VEEJAY_MSG_INFO,
		"Copyright (C) 2004 - 2005. N. Elburg et all." );
	vj_msg_detail( VEEJAY_MSG_INFO,
		"GVeejay - A graphical interface for Veejay");

#endif

}

gboolean	dialogkey_snooper( GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
	GtkWidget *entry = (GtkWidget*) user_data;
 	if(gtk_widget_is_focus(entry) == FALSE )
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

gboolean	gveejay_quit( GtkWidget *widget, gpointer user_data)
{
	if( prompt_dialog("Quit gveejay", "Are you sure?" ) == GTK_RESPONSE_REJECT)
		return TRUE;

	if(info->run_state == RUN_STATE_LOCAL)
		single_vims( VIMS_QUIT );
	
	vj_gui_disconnect();
	vj_gui_free();

	return FALSE;
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
	int nr,nw;
        gchar *text = g_locale_to_utf8( buf, -1, &nr, &nw, NULL);
        text[strlen(text)-1] = '\0';
        put_text( "lastmessage", text );

	fprintf(stderr, "%s", buf);
	g_free( text );
	va_end(args);
}
static  void	vj_msg_detail(int type, const char format[], ...)
{
	GtkWidget *view = glade_xml_get_widget_( info->main_window,(type==4 ? "veejaytext": "gveejaytext"));
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
		snprintf(buf,sizeof(buf),"%s",tmp);
	else
		snprintf(buf, sizeof(buf), "%s %s\n",prefix,tmp );
	int nr,nw;

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
	//vj_msg(VEEJAY_MSG_DEBUG, " %s: %s", __FUNCTION__, message );
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
	//vj_msg(VEEJAY_MSG_DEBUG, " %s: %s", __FUNCTION__, block );
	int error = vj_client_send( info->client, V_CMD, block); 
}

static	void single_vims(int id)
{
	char block[10];
	if(!info->client)
		return;
	sprintf(block, "%03d:;",id);
	//vj_msg(VEEJAY_MSG_DEBUG, " %s: %s", __FUNCTION__ , block );
	vj_client_send( info->client, V_CMD, block );
}

static gchar	*recv_vims(int slen, int *bytes_written)
{
	gchar tmp[slen+1];
	bzero(tmp,slen+1);
	int ret = vj_client_read( info->client, V_CMD, tmp, slen );
	int len = atoi(tmp);
	gchar *result = NULL;
	int n = 0;

	if(len > 0)
	{
		result = g_new( gchar, len+1 );
		n = vj_client_read( info->client, V_CMD, result, len );
		*bytes_written = n;
		result[len] = '\0';
		vj_msg(VEEJAY_MSG_DEBUG, " %s: %s", __FUNCTION__, result );
		return result;
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

static	void	update_slider_rel_value(const char *name, gint value, gint minus, gint scale)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w)
		return;
	gdouble gvalue = (gdouble)(value - minus);
	if(scale)
		gvalue = (100.0 / (gdouble)scale) * gvalue;

	gtk_adjustment_set_value(
		GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment), gvalue );	
}


static	void	update_slider_value(const char *name, gint value, gint scale)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w)
		return;
	gdouble gvalue;
	if(scale)
		gvalue = (100.0 / (gdouble)scale) * value;
	else
		gvalue = (gdouble) value;
	gtk_adjustment_set_value(
		GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment), gvalue );	
}

static	void	update_spin_range(const char *name, gint min, gint max, gint val)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	if(!w) return;
	gtk_spin_button_set_range( GTK_SPIN_BUTTON(w), (gdouble)min, (gdouble) max );
	gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), (gdouble)val);
}

static	void	update_spin_value(const char *name, gint value )
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
	if(w)
	{
		gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), (gdouble) value );
	}
}

static  void	update_slider_range(const char *name, gint min, gint max, gint value, gint scaled)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	GtkRange *range = GTK_RANGE(w);
	if(!w)
		return;
	if( min >= max )
	{
		fprintf(stderr, "gveejay fatal : %s , %d - %d", name,min,max );
	}

	if(!scaled)
	{
		gtk_range_set_range(range, (gdouble) min, (gdouble) max );
		gtk_range_set_value(range, value );
	}
	else
	{
		gdouble perc = 100.0  / max;
		gtk_range_set_range(range, perc * (gdouble)min, perc * (gdouble)max);
		gtk_range_set_value(range, perc * (gdouble)value );
		
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
		g_snprintf( str,20, "%011d", num );
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
	if(!label) return;
	gchar *utf8_text = _utf8str( text );
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

static gboolean
chain_update_row(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,
             gpointer data)
{

	vj_gui_t *gui = (vj_gui_t*) data;
	int entry = gui->uc.selected_chain_entry;
	int effect_id = gui->uc.entry_tokens[ ENTRY_FXID ];
	guint gentry;

    /* Note: here we use 'iter' and not '&iter', because we did not allocate
     *  the iter on the stack and are already getting the pointer to a tree iter */

	gtk_tree_model_get (model, iter,
                        FXC_ID, &gentry, -1);

	if( gentry == entry && effect_id <= 0 )
	{
		gtk_list_store_set( GTK_LIST_STORE(model), iter, 0 , FALSE, - 1);
	}	
	if( gentry == entry && effect_id > 0)
	{
		gchar *descr = _utf8str( _effect_get_description( effect_id ));
		gchar toggle[5];
		sprintf(toggle, "%s", ( gui->uc.entry_tokens[ ENTRY_FXSTATUS ] == 1 ? "on" : "off" ));
		gtk_list_store_set( GTK_LIST_STORE(model),iter,
			FXC_ID, entry,
			FXC_FXID, descr,
			FXC_FXSTATUS, toggle, -1 );
		g_free(descr);
	}


  return FALSE;
}

static void 	update_globalinfo()
{

	if( info->uc.playmode == MODE_STREAM )
		info->status_tokens[FRAME_NUM] = 0;

	update_label_i( "label_curframe", info->status_tokens[FRAME_NUM] , 1 );

	gchar *ctime = format_time( info->status_tokens[FRAME_NUM] );
	update_label_str( "label_curtime", ctime );
	g_free(ctime);

	int pm = info->status_tokens[PLAY_MODE];

	int *history = info->history_tokens[pm];
	int	stream_changed = 0;
	int	sample_changed = 0;
	gint	i;

	info->uc.playmode = pm;

	if(info->uc.previous_playmode != info->uc.playmode )
	{
		if( pm == MODE_STREAM ) stream_changed = 1;
		if( pm == MODE_SAMPLE ) sample_changed = 1;

		if( pm == MODE_PLAIN )
		{
			for(i =0; notepad_widgets[i].name != NULL; i ++ )
			 disable_widget( notepad_widgets[i].name );	
		}
		if( pm == MODE_STREAM )
		{
			enable_widget("frame_streamproperties");
			enable_widget("frame_streamrecord");
			disable_widget("tree_history");
			disable_widget("button_historyrec");
			disable_widget("button_historymove");  
			disable_widget("frame_samplerecord");
			disable_widget("frame_sampleproperties");
		}
		if( pm == MODE_SAMPLE )
		{
			enable_widget("frame_samplerecord");
			enable_widget("frame_sampleproperties");
			enable_widget("tree_history");
			enable_widget("button_historyrec");
			enable_widget("button_historymove");  
			disable_widget("frame_streamproperties");
			disable_widget("frame_streamrecord");
		}
		if( pm == MODE_SAMPLE || pm == MODE_STREAM)
			enable_widget("vbox_fxtree");


		if( pm == MODE_SAMPLE || pm == MODE_STREAM)
		{
			info->uc.reload_hint[HINT_CHAIN] = 1;
			info->uc.reload_hint[HINT_ENTRY] = 1;
		}
	}

	if( pm != MODE_PLAIN)
		info->uc.list_length[pm] = info->status_tokens[TOTAL_SAMPLES];


	if( !info->slider_lock )
	{
		if( history[FRAME_NUM] != info->status_tokens[FRAME_NUM])
		{
			if(pm == MODE_STREAM)
				update_slider_value( "videobar", 0, 0 );
			if(pm == MODE_PLAIN )
				update_slider_value( "videobar", info->status_tokens[FRAME_NUM], 
					         info->status_tokens[TOTAL_FRAMES] );
			if(pm == MODE_SAMPLE)
			{
				update_slider_rel_value( "videobar",
						info->status_tokens[FRAME_NUM],
						info->status_tokens[SAMPLE_START],
						0);
				update_label_i( "label_samplepos",
						info->status_tokens[FRAME_NUM] - info->status_tokens[SAMPLE_START] , 1);
			}
		}
		if(pm == MODE_SAMPLE )
		{
			gchar *time = format_time( info->status_tokens[FRAME_NUM] - info->status_tokens[SAMPLE_START]);

			update_label_str( "label_sampleposition", time);
			g_free(time); 
		}
		if(pm == MODE_STREAM)
		{
			gchar *time = format_time( info->status_tokens[FRAME_NUM]);

			update_label_str( "label_sampleposition", time);
			g_free(time); 
			update_label_str( "label_samplelength", "infinite");
		}
	}

	if( history[TOTAL_FRAMES] !=
		info->status_tokens[TOTAL_FRAMES])
	{
		gint tf = info->status_tokens[TOTAL_FRAMES];
		if( pm == MODE_PLAIN ) 
		{
			for( i = 0; i < 3; i ++)
				if(info->selection[i] > tf ) info->selection[i] = tf;
		
			update_spin_range(
				"button_el_selstart", 0, tf, info->selection[0]);
			update_spin_range(
				"button_el_selend", 0, tf, info->selection[1]);
			update_spin_range(
				"button_el_selpaste", 0, tf, info->selection[2]);
			// dont update in mode sample
			update_spin_range(
				"spin_samplestart", 0, tf, 0 );
			update_spin_range(
				"spin_sampleend", 0, tf, 0 );
				
			info->uc.reload_hint[HINT_EL] = 1;
		}
		if( pm == MODE_STREAM )
			tf = 1;
		update_spin_range(
			"button_fadedur", 0, tf, 0 );
		if( pm == MODE_SAMPLE )
		{
			update_slider_range( "videobar", 0, (info->status_tokens[SAMPLE_END] -
			info->status_tokens[SAMPLE_START]), (info->status_tokens[FRAME_NUM] - 
			info->status_tokens[SAMPLE_START]), 0);
		}
		else
		{
			update_slider_range( "videobar", 0, tf, 
				info->status_tokens[FRAME_NUM], 1 );	
		}	
		update_label_i( "label_totframes", tf, 1 );
			
		gchar *time = format_selection_time( 1, tf );
		update_label_str( "label_totaltime", time );
		g_free(time);
	}

	if( history[CURRENT_ID] != info->status_tokens[CURRENT_ID] )
	{
		if(pm == MODE_SAMPLE || pm == MODE_STREAM)
		{
			info->uc.reload_hint[HINT_CLIP] = 1;
			info->uc.reload_hint[HINT_ENTRY] = 1;
			sample_changed = 1;
			stream_changed = 1;		
			update_label_i( "label_currentid", info->status_tokens[CURRENT_ID] ,0);
		}
	}

	if( history[STREAM_RECORDING] != info->status_tokens[STREAM_RECORDING] )
	{
		if(pm == MODE_SAMPLE || pm == MODE_STREAM)
		{
			info->uc.reload_hint[HINT_RECORDING] = 1;
			vj_msg(VEEJAY_MSG_INFO, "Veejay is recording");
		}
	}

	if( pm == MODE_STREAM )
	{
		if( ( history[STREAM_COL_R] != info->status_tokens[STREAM_COL_R] ) ||
		    ( history[STREAM_COL_G] != info->status_tokens[STREAM_COL_G] ) ||
		    ( history[STREAM_COL_B] != info->status_tokens[STREAM_COL_B] ) )
		{
			info->uc.reload_hint[HINT_RGBSOLID] = 1;
		}
	}

	if( pm == MODE_SAMPLE )
	{

		if( history[SAMPLE_START] != info->status_tokens[SAMPLE_START] )
		{
			update_spin_value( "spin_samplestart", info->status_tokens[SAMPLE_START]);
			info->uc.marker.lower_bound = 0;
			sample_changed = 1;
			update_slider_range("slider_m0", 0, 
				info->status_tokens[SAMPLE_END]-info->status_tokens[SAMPLE_START],
				0,
				0 );
		}

		if( history[SAMPLE_END] != info->status_tokens[SAMPLE_END])
		{
			update_spin_value( "spin_sampleend", info->status_tokens[SAMPLE_END]);
			info->uc.marker.upper_bound = (info->status_tokens[SAMPLE_END] -
							info->status_tokens[SAMPLE_START]);

			update_slider_range("slider_m1", 0,
					info->uc.marker.upper_bound,
					info->uc.marker.lower_bound,
					0);

			sample_changed = 1;
		}
		if( history[SAMPLE_LOOP] != info->status_tokens[SAMPLE_LOOP])
		{
			switch( get_loop_value() )
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
		if( history[SAMPLE_SPEED] != info->status_tokens[SAMPLE_SPEED] && !sample_changed)
		{
			update_spin_value( "spin_samplespeed", info->status_tokens[SAMPLE_SPEED]);
		}
	}

	if( pm == MODE_PLAIN || pm == MODE_SAMPLE)
	{
		if( history[SAMPLE_SPEED] != info->status_tokens[SAMPLE_SPEED] && !sample_changed)
		{
			int plainspeed =  info->status_tokens[SAMPLE_SPEED];
			if( plainspeed < -64 ) plainspeed = -64;
			if( plainspeed > 64 ) plainspeed = 64;
			update_slider_value( "speedslider", plainspeed, 0);
		}

	}

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


	if( (info->uc.last_list_length[0] != info->uc.list_length[0])  ||
	    (info->uc.last_list_length[1] != info->uc.list_length[1]) )
	{
		info->uc.reload_hint[HINT_SLIST] = 1;
		if(info->uc.last_list_length[0] != info->uc.list_length[0] )
			vj_msg(VEEJAY_MSG_INFO, "A new sample was created");
		else
			vj_msg(VEEJAY_MSG_INFO, "A new stream was created");
	}

	int	*entry_history = &(info->uc.entry_history[0]);
	int	*entry_tokens = &(info->uc.entry_tokens[0]);

	
	if( entry_history[ENTRY_FXID] !=
		entry_tokens[ENTRY_FXID] )
	{
		info->uc.reload_hint[HINT_ENTRY] = 1;
	}
	if(info->uc.reload_hint[HINT_V4L] == 1 )
	{
		load_v4l_info();
		vj_msg(VEEJAY_MSG_INFO, "Video4Linux color setup available");
	}
	if( info->uc.reload_hint[HINT_RGBSOLID] == 1 )
		update_colorselection();

	if(info->uc.reload_hint[HINT_ENTRY] == 1)
	{
		char slider_name[10];
		char button_name[10];
		gint np = 0;
		/* update effect description */
		load_parameter_info();
		
		if( entry_tokens[ENTRY_FXID] == 0)
		{
			put_text( "entry_effectname" ,"" );
			disable_widget( "button_entry_toggle" );
		}
		else
		{
			put_text( "entry_effectname", _effect_get_description( entry_tokens[ENTRY_FXID] ));
			enable_widget( "button_entry_toggle");
			set_toggle_button( "button_entry_toggle", entry_tokens[ENTRY_FXSTATUS] );
			np = _effect_get_np( entry_tokens[ENTRY_FXID] );
			GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(glade_xml_get_widget_(
					info->main_window, "tree_chain") ));

  			gtk_tree_model_foreach(
                        	model,
				chain_update_row, (gpointer*) info );
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
		}

	}

	if( info->uc.reload_hint[HINT_EL] ==  1 )
	{
		load_editlist_info();
		reload_editlist_contents();
		vj_msg(VEEJAY_MSG_WARNING, "EditList has changed");
	}
	if( info->uc.reload_hint[HINT_CHAIN] == 1 || info->uc.reload_hint[HINT_CLIP] == 1)
	{
		load_effectchain_info(); 
	}

	if( info->uc.reload_hint[HINT_SLIST] == 1 )
	{
		load_samplelist_info("tree_samples");
		load_samplelist_info("tree_sources");
	}

	if( info->uc.reload_hint[HINT_RECORDING] == 1 )
	{
		if(info->status_tokens[STREAM_RECORDING])
		{
			if(!info->uc.recording[pm])
			{
				init_recorder( info->status_tokens[STREAM_DURATION], pm );
			}
		}	
	}

	if(sample_changed)
	{
		gint len = info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START];
		update_spin_range( "spin_samplespeed", -1 * len, len, info->status_tokens[SAMPLE_SPEED]);

		gchar *time = format_selection_time( 0, len );
		update_label_str( "label_samplelength", time );
		g_free(time);	

		if( get_page( "veejaypanel" ) == PAGE_SAMPLEEDIT )
		{
			set_page( "sample_stream_pad", 0 );
		}
		update_label_str( "label_currentsource", "Sample");


		info->uc.reload_hint[HINT_HISTORY] = 1;

		gint n_frames = sample_calctime();
		time = format_time( n_frames );
		update_label_str( "label_samplerecord_duration", time );
		info->uc.sample_rec_duration = n_frames;
		g_free(time);

	}

	if(stream_changed)
	{
		if( pm == MODE_STREAM )
		{
			// pop up stream notepad
			int k;
			info->uc.current_stream_id = info->status_tokens[CURRENT_ID];
			info->uc.current_sample_id = 0;
			if( info->uc.streams[ info->uc.current_stream_id ] == STREAM_VIDEO4LINUX )
			{
				info->uc.reload_hint[HINT_V4L] = 1;
				for(k = 0; k < 5; k ++ )
					enable_widget( capt_card_set[k].name );
				if( get_page("veejaypanel" ) == PAGE_SAMPLEEDIT)
					set_page ("sample_stream_pad", 1 );

				v4l_expander_toggle(1);
			}
			else
			{
				for(k = 0; k < 5; k ++ )
					disable_widget( capt_card_set[k].name );

				v4l_expander_toggle(0);
				if(info->uc.streams[ info->uc.current_stream_id] == STREAM_WHITE )
				{
					info->uc.reload_hint[HINT_RGBSOLID] = 1;
					enable_widget( "colorselection" );
					if( get_page("veejaypanel" ) == PAGE_SAMPLEEDIT)
						set_page ("sample_stream_pad", 2 );
				}
				else
				{
					disable_widget( "colorselection");
				}
			}
			update_label_str( "label_currentsource", "Stream" );
		}
		// pop up edit notepad
		if( pm == MODE_SAMPLE )
		{
			info->uc.current_sample_id = info->status_tokens[CURRENT_ID];
			info->uc.current_stream_id = 0;
		}
	}

	if(info->uc.reload_hint[HINT_HISTORY] == 1 )
	{
		reload_hislist();
	}


	if(info->uc.reload_hint[HINT_BUNDLES] == 1 )
	{
		reload_bundles();
	}

	if(info->uc.reload_hint[HINT_MARKER] == 1 )
	{
		info->uc.marker.lower_bound = 0;
		info->uc.marker.upper_bound = 0;
		update_slider_value( "slider_m0", info->uc.marker.lower_bound, 0);
		update_slider_value( "slider_m1", info->uc.marker.upper_bound, 0 );
	}


	memset( info->uc.reload_hint, 0, sizeof(info->uc.reload_hint ));	
	info->uc.previous_playmode = pm;
	for( i = 0; i < 2; i ++)
		info->uc.last_list_length[i] = info->uc.list_length[i];
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

static	void	update_sampleinfo()
{
}
static	void	update_streaminfo()
{

}
static	void	update_plaininfo()
{
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
      gchar *toggle = NULL;	

      gtk_tree_model_get(model, &iter, FXC_ID, &name, -1);
      gtk_tree_model_get(model, &iter, FXC_FXSTATUS, &toggle, -1 );

      if (!path_currently_selected)
      {
	multi_vims( VIMS_CHAIN_SET_ENTRY, "%d", name );
	info->uc.reload_hint[HINT_ENTRY] = 1;
     	info->uc.selected_chain_entry = name;
      }
      if(toggle) g_free(toggle);
    }

    return TRUE; /* allow selection state to change */
  }
gboolean
  view_sample_selection_func (GtkTreeSelection *selection,
                       GtkTreeModel     *model,
                       GtkTreePath      *path,
                       gboolean          path_currently_selected,
                       gpointer          userdata)
  {
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
      gchar *name = NULL;
      gchar *toggle = NULL;	

      gtk_tree_model_get(model, &iter, FXC_ID, &name, -1);
      gtk_tree_model_get(model, &iter, FXC_FXSTATUS, &toggle, -1 );

      if (!path_currently_selected)
      {
	gint id = 0;
	sscanf(name+1, "%d", &id);
	if(name[0] == 'S')
	{
	   	info->uc.selected_sample_id = id;
		info->uc.selected_stream_id = 0;
	}
	else
	{
		info->uc.selected_sample_id = 0;
		info->uc.selected_stream_id = id;
	}
	}
	if(name) g_free(name);
	if(toggle) g_free(toggle);

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
      gchar *toggle = NULL;	

      gtk_tree_model_get(model, &iter, FXC_ID, &name, -1);
      gtk_tree_model_get(model, &iter, FXC_FXSTATUS, &toggle, -1 );

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
	if(toggle) g_free(toggle);
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
	GtkListStore *store = gtk_list_store_new( 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING );
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "tree_chain", FXC_ID, "Entry" );
	setup_tree_text_column( "tree_chain", FXC_FXID, "Effect" );
	setup_tree_text_column( "tree_chain", FXC_FXSTATUS, "Status"); // todo: could be checkbox!!
	
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
	multi_vims( VIMS_STREAM_GET_V4L, "%d", info->uc.current_stream_id );
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

static	void	load_parameter_info()
{
	int	*p = &(info->uc.entry_tokens[0]);
	int	len = 0;

	multi_vims( VIMS_CHAIN_GET_ENTRY, "%d %d", 0, 
		info->uc.selected_chain_entry );

	gchar *answer = recv_vims(3,&len);
	if(len > 0 )
	{
		int res = sscanf( answer,
			"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
			p+0,p+1,p+2,p+3,p+4,p+5,p+6,p+7,p+8,p+9,p+10,
			p+11,p+12,p+13,p+14,p+15);

		if( res <= 0 )
			memset( p, 0, 16 ); 

		info->uc.selected_rgbkey = _effect_get_rgb( p[0] );
		if(info->uc.selected_rgbkey)
		{
			enable_widget( "rgbkey");
			// update values
			update_rgbkey();
			
		 // enable rgb
		}  
     		else
		{
		 // disable rgb
			disable_widget( "rgbkey");
			if( get_page( "veejaypanel") == 3 )
			{
				if(get_page( "fxcontrolpanel" ) == 1 )
					set_page( "fxcontrolpanel", 0 );
			}
		} 
		g_free(answer);
	}
}

// load effect chain
static	void	load_effectchain_info()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_chain");
	GtkListStore *store;
	
	GtkTreeIter iter;
	gint offset=0;
	
	
	gint fxlen = 0;
	single_vims( VIMS_CHAIN_LIST );
	gchar *fxtext = recv_vims(3,&fxlen);

	reset_tree( "tree_chain" );

	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	if(fxlen == 5)
	{
		offset = fxlen;
	}

	gint last_index =0;

	while( offset < fxlen )
	{
		gchar toggle[4];
		guint arr[6];
		bzero(toggle,4);
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
			gchar *utf8_toggle = _utf8str( toggle );
			gtk_list_store_append( store, &iter );
			gtk_list_store_set( store, &iter,
				FXC_ID, arr[0],
				FXC_FXID, utf8_name,
				FXC_FXSTATUS, utf8_toggle, -1 );
			last_index ++;
			g_free(utf8_name);
			g_free(utf8_toggle);
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
	FX_ID = 0,
	FX_PIXMAP = 1,
	FX_STRING =2,
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
      gint name;

      gtk_tree_model_get(model, &iter, FX_ID, &name, -1);

      if (!path_currently_selected)
      {
	info->uc.selected_effect_id = name;
      }

    }

    return TRUE; /* allow selection state to change */
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
		gtk_tree_model_get(model,&iter, FX_ID, &gid, -1);

		if(gid)
		{
			multi_vims(VIMS_CHAIN_ENTRY_SET_EFFECT, "%d %d %d",
				0, info->uc.selected_chain_entry,gid );
			info->uc.reload_hint[HINT_CHAIN] = 1;
			info->uc.reload_hint[HINT_ENTRY] = 1;
		}

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
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_effectlist");
	GtkListStore *store = gtk_list_store_new( 3, G_TYPE_INT, GDK_TYPE_PIXBUF, G_TYPE_STRING );

	GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);

	gtk_tree_sortable_set_sort_func(
		sortable, FX_STRING, sort_iter_compare_func,
			GINT_TO_POINTER(FX_STRING),NULL);

	gtk_tree_sortable_set_sort_column_id( 
		sortable, FX_STRING, GTK_SORT_ASCENDING);

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "tree_effectlist", FX_ID, "id" );
	setup_tree_pixmap_column( "tree_effectlist", FX_PIXMAP , "type" );
	setup_tree_text_column( "tree_effectlist", FX_STRING, "effect" );
	GtkTreeSelection *selection; 

	g_signal_connect( tree, "row-activated",
		(GCallback) on_effectlist_row_activated, NULL );

	tree = glade_xml_get_widget_( info->main_window, "tree_effectlist");
  	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    	gtk_tree_selection_set_select_function(selection, view_fx_selection_func, NULL, NULL);
    	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);


}


void
on_samplelist_row_activated(GtkTreeView *treeview,
		GtkTreePath *path,
		GtkTreeViewColumn *col,
		gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(treeview);
	gchar *what = (gchar*) user_data;

	if(gtk_tree_model_get_iter(model,&iter,path))
	{
		gchar *idstr = NULL;
		gtk_tree_model_get(model,&iter, SL_ID, &idstr, -1);
		gint id = 0;
		if( sscanf( idstr+1, "%04d", &id ) )
		{
			if(strcasecmp( what, "tree_samples") == 0 )
			{
				// play sample / stream
				multi_vims( VIMS_SET_MODE_AND_GO, "%d %d",
					( idstr[0] == 'T' ? 1 : 0 ), id );
			}
			else
			if(strcasecmp( what, "tree_sources") == 0 )
			{
				// set source / channel
				multi_vims( VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
					"%d %d %d %d",
					0,
					info->uc.selected_chain_entry,
					( idstr[0] == 'T' ? 1 : 0 ),
					id );
			}	
		}
		if(idstr) g_free(idstr);
	}

}

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
			multi_vims( VIMS_CLIP_SET_DESCRIPTION,
				"%d %s", sample_id, descr );
		else
			multi_vims( VIMS_STREAM_SET_DESCRIPTION,
				"%d %s", sample_id, descr );

		info->uc.reload_hint[HINT_SLIST] = 1;
		if(sysstr) g_free( sysstr );
	}

	if(sysid) g_free(sysid);
	if(id) g_free(id);
}

void	setup_samplelist_info(const char *name)
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, name);
	GtkListStore *store = gtk_list_store_new( 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING );
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( name, SL_ID, "Id" );
	setup_tree_texteditable_column( name, SL_DESCR , "Title" ,G_CALLBACK(on_samplelist_edited));
	setup_tree_text_column( name, SL_TIMECODE, "Length" );

	GtkTreeSelection *selection;

	if(strcasecmp(name, "tree_samples") == 0)
	{
 	 	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
 	   	gtk_tree_selection_set_select_function(selection, view_sample_selection_func, NULL, NULL);
    		gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
	}
	if(strcasecmp(name, "tree_sources") == 0 )
	{
  		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    		gtk_tree_selection_set_select_function(selection, view_sources_selection_func, NULL, NULL);
    		gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

	}

//	g_signal_connect( tree, "edited", 
//		(GCallback) on_samplelist_edited, (gpointer*) name );

	g_signal_connect( tree, "row-activated",
			(GCallback) on_samplelist_row_activated, (gpointer*)name );


}

void	load_effectlist_info()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_effectlist");
	GtkListStore *store;
	
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
			char pixmap[512];
			bzero(pixmap,512);
			get_gd( pixmap, NULL, (_effect_get_mix(ec->id) ? "bg_blue.png": "bg_yellow.png"));
			GError *error = NULL;
			GdkPixbuf *icon = gdk_pixbuf_new_from_file(pixmap, &error);
			if(error == NULL)
			{
				gtk_list_store_append( store, &iter );
				gtk_list_store_set( store, &iter, FX_ID,(guint) ec->id, FX_PIXMAP, icon, FX_STRING, name, -1 );
			}
		}
		g_free(name);
	}


	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
//	g_object_unref( G_OBJECT( store ) );

	g_free(fxtext);
	
}

// execute after sample/stream/mixing sources list update
// same for load_mixlist_info(), only different widget !!
static	void	load_samplelist_info(const char *name)
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window,name);
	GtkListStore *store;
	
	GtkTreeIter iter;
	gint offset=0;
	
	
	int	values[10];

	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	int has_samples = 0;
	int has_streams = 0;

	memset(values,0,sizeof(values));

	reset_tree( name );

	single_vims( VIMS_CLIP_LIST );
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
				gchar id[10];
				bzero( line, 300 );
				bzero( descr, 255 );
				strncpy( line, fxtext + offset, len );
				// add to tree
				int values[4];
				sscanf( line, "%05d%09d%09d%03d%s",
					&values[0], &values[1], &values[2], &values[3],	
					descr );

				gchar *title = _utf8str( descr );
				gchar *timecode = format_selection_time( 0,(values[2]-values[1]) );
				sprintf( id, "S%04d", values[0]);
				gtk_list_store_append( store, &iter );
				
				gtk_list_store_set( store, &iter, SL_ID, id,
					SL_DESCR, title, SL_TIMECODE , timecode,-1 );
				g_free(timecode);
				g_free(title);
			}
			offset += len;
		}
		offset = 0;
		memset(values,0,sizeof(values));
	}

	if( fxtext ) g_free(fxtext);
	fxlen = 0;

	single_vims( VIMS_STREAM_LIST );
	fxtext = recv_vims(5, &fxlen);

	if(fxtext == NULL || fxlen <= 5 )
	{
		if(has_samples)
			gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
		return;
	}

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
			gchar id[10];
			bzero( line, 300 );
			bzero( descr, 255 );
			bzero( source, 255 ); 
			strncpy( line, fxtext + offset, len );
			// add to tree
			int values[4];

			sscanf( line, "%05d%02d%03d%03d%03d%03d%03d%03d",
				&values[0], &values[1], &values[2], 
				&values[3], &values[4], &values[5],
				&values[6], &values[7]
			);
			strncpy( descr, line + 22, values[6] );
			switch( values[1] )
			{
				case STREAM_VIDEO4LINUX: sprintf(source,"(Streaming from Video4Linux)");break;
				case STREAM_WHITE	:sprintf(source,"(Streaming from Solid)"); break;
				case STREAM_MCAST	:sprintf(source,"(Multicast stream");break;
				case STREAM_NETWORK	:sprintf(source,"(Unicast stream");break;
				case STREAM_YUV4MPEG	:sprintf(source,"(Streaming from Yuv4Mpeg file)");break;
				case STREAM_AVFORMAT	:sprintf(source,"(Streaming from libavformat");break;
				case STREAM_DV1394	:sprintf(source,"(Streaming from DV1394 Camera");break;
				default:
					sprintf(source,"(Streaming from unknown)");	
			}
			gchar *gsource = _utf8str( descr );
			gchar *gtype = _utf8str( source );
			info->uc.streams[ (values[0]) ] = values[1];

			sprintf( id, "T%04d", values[0]);
			gtk_list_store_append( store, &iter );
			gtk_list_store_set( store, &iter, SL_ID, id,
					SL_DESCR, gsource, SL_TIMECODE ,gtype,-1 );
			g_free(gsource);
			g_free(gtype);
		}
		offset += len;
	}

	if(has_samples || has_streams)
		gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_free(fxtext);
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

gboolean
  view_history_selection_func (GtkTreeSelection *selection,
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
		info->uc.selected_history_entry = num;
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
on_history_row_activated(GtkTreeView *treeview,
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
		//gint frame_num = _el_ref_start_frame( num );

		multi_vims( VIMS_CLIP_RENDER_SELECT, "%d %d", 0, num );

	}

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

		if(info->uc.playmode != MODE_PLAIN)
			multi_vims( VIMS_SET_PLAIN_MODE, "%d" ,MODE_PLAIN );

		multi_vims( VIMS_VIDEO_SET_FRAME, "%d", (int) frame_num );
	}

}

void
on_stream_color_changed(GtkColorSelection *colorsel, gpointer user_data)
{
	if(!info->status_lock)
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
		info->uc.current_stream_id,
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

static void	reload_hislist()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_history");
	GtkListStore *store;
	GtkTreeIter iter;
	gint offset=0;
	gint hislen = 0;
	single_vims( VIMS_CLIP_RENDERLIST );
	gchar *fxtext = recv_vims(3,&hislen);

 	reset_tree( "tree_history");
	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	store = GTK_LIST_STORE(model);

	while( offset < hislen )
	{
		gchar *timecode;
		int values[3];
		char tmp_len[22];
		bzero(tmp_len, 22);
		strncpy(tmp_len, fxtext + offset, 22 );
		int n = sscanf( tmp_len, "%02d%010d%010d",&values[0],&values[1],&values[2]);
		if(n>1)
		{
			
		}	

		if(offset == 0)
			timecode = strdup( "original sample" );
		else
			timecode = format_time( values[2] - values[1] );
		offset += 22;
		gtk_list_store_append( store, &iter );
		gtk_list_store_set( store, &iter, COLUMN_INT, (guint) values[0],
			COLUMN_STRING0, timecode, -1 );

		g_free(timecode);
	}

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_free(fxtext);
}

static	void	setup_hislist_info()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_history");
	GtkListStore *store = gtk_list_store_new( 2,G_TYPE_INT, G_TYPE_STRING );
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "tree_history", COLUMN_INT, "Seq");
	setup_tree_text_column( "tree_history", COLUMN_STRING0, "Duration" );

	g_signal_connect( tree, "row-activated",
		(GCallback) on_history_row_activated, NULL );

  	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
 	gtk_tree_selection_set_select_function(selection, view_history_selection_func, NULL, NULL);
    	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
}
static	void	setup_editlist_info()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "editlisttree");
	GtkListStore *store = gtk_list_store_new( 5,G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING ,G_TYPE_STRING);
	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	g_object_unref( G_OBJECT( store ));

	setup_tree_text_column( "editlisttree", COLUMN_INT, "Nr");
	setup_tree_text_column( "editlisttree", COLUMN_STRING0, "Timecode" );
	setup_tree_text_column( "editlisttree", COLUMN_STRINGA, "Filename");
	setup_tree_text_column( "editlisttree", COLUMN_STRINGB, "Duration");
	setup_tree_text_column( "editlisttree", COLUMN_STRINGC, "FOURCC");

	g_signal_connect( tree, "row-activated",
		(GCallback) on_editlist_row_activated, NULL );

  	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
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

			vj_event_list[ val[0] ].event_id = val[0];
			vj_event_list[ val[0] ].params   = 0;
			vj_event_list[ val[0] ].format   = NULL;
			vj_event_list[ val[0] ].descr    = _utf8str( "custom event (fixme: set bundle title)" );
		}


		if( vj_event_list[ val[0] ].event_id != 0 )
			descr = vj_event_list[ val[0] ].descr;
		if( vj_event_list[ val[0] ].event_id != 0 )
			format = vj_event_list[ val[0] ].format;

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

	char	str_nf[4];
	
	strncpy( str_nf, eltext , sizeof(str_nf));
	sscanf( str_nf, "%04d", &num_files );

	offset += 4;
	int n = 0;
	el_constr *el;
	for( i = 0; i < num_files ; i ++ )	
	{
		int itmp =0;
		char *tmp = (char*) strndup( eltext+offset, 4 );
		int line_len = 0;
		char fourcc[4];
		bzero(fourcc,4);
		n = sscanf( tmp, "%04d", &line_len ); // line len
		free(tmp);
		if(line_len>0)
		{
			offset += 4;
			char *line = (char*)strndup( eltext + offset, line_len );
			offset += line_len;
			tmp = (char*) strndup( line, 3 );
			sscanf(tmp, "%03d",&itmp );
			char *file = strndup( line + 3, itmp );
			free(tmp);
			tmp = (char*) strndup( line + 3 + itmp, 16 );
			int a,b,c;
			int n = sscanf(tmp, "%04d%010d%02d", &a,&b,&c);
			free(tmp);
			strncpy(fourcc, line + 3 + itmp + 16, c );
			el = _el_entry_new( i, file,   b, fourcc );

			info->editlist = g_list_append( info->editlist, el );

			free(line);
			free(file);
		}
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
		gtk_list_store_append( store, &iter );
		gtk_list_store_set( store, &iter,
				COLUMN_INT, (guint) row_num, // <- start timecode?! (timecode of offset)
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
	memset(values, 0, 10);
	single_vims( VIMS_VIDEO_INFORMATION );
	gint len = 0;
	gchar *res = recv_vims(3,&len);

	if(len > 0 )
	{
		sscanf( res, "%d %d %d %c %f %d %d %ld %d %ld %ld",
			&values[0], &values[1], &values[2], &norm,&fps,
			&values[4], &values[5], &rate, &values[7],
			&dum[0], &dum[1]);
			char tmp[15];
		snprintf( tmp, sizeof(tmp)-1, "%dx%d", values[0],values[1]);

		info->el.width = values[0];
		info->el.height = values[1];

		update_spin_value( "priout_height", values[1] );
		update_spin_value( "priout_width", values[0] );
	
		// lock pri out and update values!

		update_label_str( "label_el_wh", tmp );
		snprintf( tmp, sizeof(tmp)-1, "%s",
			(norm == 'p' ? "PAL" : "NTSC" ) );
		update_label_str( "label_el_norm", tmp);
		update_label_f( "label_el_fps", fps );
		info->el.fps = fps;
		info->el.num_files = dum[0];
		
		if(info->el.num_files > 0)
		{
//			if(info->el.offsets)
//				free(info->el.offsets);
			
//			info->el.offsets = (int*)malloc(sizeof(int) * info->el.num_files );
//			memset(info->el.offsets, 0, info->el.num_files);
		}

		snprintf( tmp, sizeof(tmp)-1, "%s",
			( values[2] == 0 ? "progressive" : (values[2] == 1 ? "top first" : "bottom first" ) ) );
		update_label_str( "label_el_inter", tmp );
		update_label_i( "label_el_arate", (int)rate, 0);
		update_label_i( "label_el_achans", values[7], 0);
		update_label_i( "label_el_abits", values[5], 0);
	
		if( values[4] == 0 )
		{
			disable_widget( "button_5_4");
			disable_widget( "audiovolume");
		}
		else
		{
			enable_widget( "button_5_4");
			enable_widget( "audiovolume");
		}
		g_free(res);
	}

}

static	int	get_page(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window, name);
	return gtk_notebook_get_current_page( GTK_NOTEBOOK(w) );
}

static	void	set_page(const char *name , int pg)
{
	GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
	gtk_notebook_set_current_page( GTK_NOTEBOOK(w), pg);
}
static	void	disable_widget(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
	if(w)
	{
		 gtk_widget_set_sensitive( GTK_WIDGET(w), FALSE );
	}
}
static	void	enable_widget(const char *name)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
	if(w)
	{
		gtk_widget_set_sensitive( GTK_WIDGET(w), TRUE );
	}
}
static	gchar	*format_time(int pos)
{
	MPEG_timecode_t	tc;
	//int	tf = info->status_tokens[TOTAL_FRAMES];
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
}
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

static	void	init_progress()
{
	//GtkWidget *w = glade_xml_get_widget_( info->main_window, "connecting");
	info->connecting = g_timeout_add( 100 , update_progress_timeout, (gpointer*) info );

}

static	void	init_recorder(int total_frames, gint mode)
{
	if(mode == MODE_STREAM)
	{
		info->streamrecording = g_timeout_add(100, update_stream_record_timeout, (gpointer*) info );
	}
	if(mode == MODE_SAMPLE)
	{
		info->samplerecording = g_timeout_add(100, update_sample_record_timeout, (gpointer*) info );
	}
	info->uc.recording[mode] = 1;
}

static	void	init_cpumeter()
{
	info->cpumeter = g_timeout_add(100,update_cpumeter_timeout,
			(gpointer*) info );
}

static void	update_gui()
{

	int pm = info->status_tokens[PLAY_MODE];
	if(pm < 0 || pm > 2)
	{
		return;
		//exit(0);
	}
	update_globalinfo();

	switch(info->status_tokens[0])
	{
		case MODE_SAMPLE:
			update_sampleinfo();
			break;
		case MODE_STREAM:
			update_streaminfo();
			break;
		case MODE_PLAIN:
			update_plaininfo();
			break;
	}

	int *history = info->history_tokens[pm];
	int i;
	int *entry_history = &(info->uc.entry_history[0]);
	int *entry_tokens = &(info->uc.entry_tokens[0]);

	for( i = 0; i < STATUS_TOKENS; i ++ )
	{
		history[i] = info->status_tokens[i];
		entry_history[i] = entry_tokens[i];
	}
}

static	void	get_gd(char *buf, char *suf, const char *filename)
{

	if(filename !=NULL && suf != NULL)
		sprintf(buf, "%s/%s/%s", GVEEJAY_DATADIR,suf, filename );
	if(filename !=NULL && suf==NULL)
		sprintf(buf, "%s/%s", GVEEJAY_DATADIR, filename);
	if(filename == NULL && suf != NULL)
		sprintf(buf, "%s/%s/" , GVEEJAY_DATADIR, suf);
}

static	void		veejay_untick(gpointer data)
{
	vj_msg(VEEJAY_MSG_INFO, "Ending veejay session");
	if(info->state == STATE_PLAYING)
	{
		info->state = STATE_STOPPED;
	}
}

static	gboolean	veejay_tick( GIOChannel *source, GIOCondition condition, gpointer data)
{
	vj_gui_t *gui = (vj_gui_t*) data;

	if( (condition&G_IO_ERR) ) return FALSE; 
	if( (condition&G_IO_HUP) ) return FALSE; 
	if( (condition&G_IO_NVAL) ) return FALSE; 

	if(gui->state==STATE_PLAYING && (condition & G_IO_IN) )
	{	//vj_client_poll( gui->client, V_STATUS ))
		int nb = 0;

		bzero( gui->status_msg, STATUS_BYTES ); 

		gui->status_lock = 1;
		nb = vj_client_read( gui->client, V_STATUS, gui->status_msg, STATUS_BYTES );
		if(nb > 0)
		{
			// is a status message ? 
			if(gui->status_msg[4] != 'S')
			{
				while(vj_client_poll(gui->client,V_STATUS))
				{
					vj_client_read(gui->client,V_STATUS,gui->status_msg,STATUS_BYTES);
				}
				return TRUE;
			}
			// parse
			int n = sscanf( gui->status_msg+5, "%d %d %d %d %d %d %d %d %d %d %d %d %d",
				gui->status_tokens + 0,
				gui->status_tokens + 1,
				gui->status_tokens + 2,
				gui->status_tokens + 3,
				gui->status_tokens + 4,
				gui->status_tokens + 5,
				gui->status_tokens + 6,
				gui->status_tokens + 7,
				gui->status_tokens + 8,
				gui->status_tokens + 9,
				gui->status_tokens + 10,
				gui->status_tokens + 11,
				gui->status_tokens + 12 );

			if( n != 13 )
			{
				// restore status (to prevent gui from going bezerk)
				int *history = info->history_tokens[ info->uc.playmode ];
				int i;
				for(i = 0; i < STATUS_TOKENS; i ++ )
				{
					gui->status_tokens[i] = history[i];
				}
			}
			update_gui();
		}
		gui->status_lock = 0;
		if(nb <= 0)
		{
			return FALSE;
		}
	}
	return TRUE;
}

void	vj_gui_stop_launch()
{
	switch( info->state )
	{
		case STATE_IDLE:
			break;
		case STATE_RECONNECT:
			info->state = STATE_IDLE;
			memset( &(info->timer), 0, sizeof(struct timeval));
			memset( &(info->alarm), 0, sizeof(struct timeval));
			if(info->connecting)
			{
				gtk_progress_bar_set_fraction(
					GTK_PROGRESS_BAR (glade_xml_get_widget_(info->main_window, "connecting")),0.0);
				g_source_remove( info->connecting );
				info->connecting = 0;
			}
			break;
		case STATE_PLAYING:
			if( info->run_state  == RUN_STATE_LOCAL )
			{
			  if( prompt_dialog("Quit Veejay ?" , "Are you sure?" ) !=
				 GTK_RESPONSE_REJECT )
				{
				  single_vims( VIMS_QUIT );
				  vj_gui_disconnect();
				}
			}
			if( info->run_state == RUN_STATE_REMOTE )
			{
			  vj_gui_disconnect();
			}
			break;
		default:
			break;
			// stop timer
	}
}


void	vj_fork_or_connect_veejay(char *configfile)
{
	char	*remote = get_text( "entry_hostname" );
	char	*files  = get_text( "entry_filename" );
	int	port	= get_nums( "button_portnum" );
	gchar	**args;
	int	n_args = 0;
	char	port_str[15];
	char	config[512];
	int 	i = 0;

	args = g_new ( gchar *, 7 );

	args[0] = g_strdup("veejay");

	sprintf(port_str, "-p%d", port);
	if(configfile)
		sprintf(config,   "-l%s", configfile);

	args[1] = g_strdup("-v");
	args[2] = g_strdup("-n");
	args[3] = g_strdup(port_str);

	if(files == NULL || strlen(files)<= 0)
		args[4] = g_strdup("-d");
	else
		args[4] = g_strdup(files);	

	if(configfile)
		args[5] = g_strdup( config );
	else
		args[5] = NULL;

	args[6] = NULL;

	if( info->state == STATE_IDLE )
	{
		// start local veejay
		if(strncasecmp(remote, "localhost", strlen(remote)) == 0 || strncasecmp(remote, "127.0.0.1", strlen(remote))==0)
		{

			// another veejay may be locally running at host:port
			if(!vj_gui_reconnect( remote, NULL, port ))
			{
				GError *error = NULL;
				int pid;
				init_progress();
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
					vj_msg(VEEJAY_MSG_INFO,
						"Spawning Veejay ...!"); 
				}
				
			}
			else
			{
				info->run_state = RUN_STATE_REMOTE;
			}
		}
		else
		{	
			if(!vj_gui_reconnect(remote,NULL,port ))
			{
				vj_msg(VEEJAY_MSG_ERROR, "Cannot establish connection with %s : %d",
					remote, port );
			}
		}
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

	gtk_main_quit();
}

static	void	vj_init_style( const char *name, const char *font )
{
	GtkWidget *window = glade_xml_get_widget_(info->main_window, "gveejay_window");
	GtkWidget *widget = glade_xml_get_widget_(info->main_window, name );
	gtk_text_view_set_wrap_mode( GTK_TEXT_VIEW(widget), GTK_WRAP_WORD_CHAR );

	GtkStyle *style = gtk_style_copy( gtk_widget_get_style(GTK_WIDGET(window)));
	PangoFontDescription *desc = pango_font_description_from_string( font );
	pango_font_description_set_style( desc, PANGO_STYLE_NORMAL );
	style->font_desc = desc;
	gtk_widget_set_style( widget, style );
	gtk_style_ref(style);
}	

void	vj_gui_style_setup()
{
	if(!info) return;
	info->color_map = gdk_colormap_get_system();
	vj_init_style( "veejaytext", "Monospace, 8" );
	vj_init_style( "gveejaytext", "Monospace, 8");
}

void	vj_gui_theme_setup()
{
	char path[MAX_PATH_LEN];
	bzero(path,MAX_PATH_LEN);
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

void	vj_gui_set_debug_level(int level)
{
	veejay_set_debug_level( level );

	vims_verbosity = level;
	if(level)
		veejay_msg(VEEJAY_MSG_INFO, "Be verbose");
}



void 	vj_gui_init(char *glade_file)
{
	char path[MAX_PATH_LEN];
	int i;

	vj_mem_init();
	
	

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
	memset( &(gui->el), 0, sizeof(veejay_el_t));
	for( i = 0 ; i < 3 ; i ++ )
	{
		gui->history_tokens[i] = (int*) vj_malloc(sizeof(int) * STATUS_TOKENS);
		if(!gui->history_tokens[i])
			return;
		memset( gui->history_tokens[i], 0, STATUS_TOKENS);
	}
	gui->uc.previous_playmode = -1;

	if(info)
	{
		vj_gui_free();
	}	

	memset( vj_event_list, 0, sizeof(vj_event_list));

	get_gd( path, NULL, glade_file);
	gui->client = NULL;
	gui->main_window = glade_xml_new(path,NULL,NULL);
	gui->state = STATE_IDLE;
	if(!gui->main_window)
	{
		free(gui);
	}
	info = gui;

	glade_xml_signal_autoconnect( gui->main_window );


	g_timeout_add_full( G_PRIORITY_DEFAULT_IDLE, 500, is_alive, (gpointer*) info,NULL);

	GtkWidget *mainw = glade_xml_get_widget_(info->main_window,
		"gveejay_window" );
    /* Make this run after any internal handling of the client event happened
     * to make sure that all changes implicated by it are already in place and
     * we thus can make our own adjustments.
     */
	g_signal_connect_after( GTK_OBJECT(mainw), "client_event",
		GTK_SIGNAL_FUNC( G_CALLBACK(gui_client_event_signal) ), NULL );

	g_signal_connect( GTK_OBJECT(mainw), "destroy",
			G_CALLBACK( gtk_main_quit ),
			NULL );
	g_signal_connect( GTK_OBJECT(mainw), "delete-event",
			G_CALLBACK( gveejay_quit ),
			NULL );

	setup_vimslist();
	setup_effectchain_info();
	setup_effectlist_info();
	setup_editlist_info();
	setup_samplelist_info("tree_samples");
	setup_samplelist_info("tree_sources");
	setup_hislist_info();
	setup_v4l_devices();
	setup_colorselection();
	setup_rgbkey();
	setup_bundles();

	set_toggle_button( "button_252", vims_verbosity );

	vj_gui_disable();

}
static	gboolean	update_log(gpointer data)
{
	single_vims( VIMS_LOG );
	int len =0;
	gchar *buf = recv_vims(6, &len );
	if(len > 0 )
	{	
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
	}
	return TRUE;
}


int	vj_gui_reconnect(char *hostname,char *group_name, int port_num)
{
	info->client = vj_client_alloc(0,0,0);

	if(!vj_client_connect( info->client, hostname, group_name, port_num ) )
	{
		if(info->client)
			vj_client_free(info->client);
		info->client = NULL;
		info->run_state = 0;
		vj_msg(VEEJAY_MSG_INFO, "Cannot establish connection with %s:%d",
			(hostname == NULL ? group_name: hostname ),port_num);
		return 0;
	}
	vj_msg(VEEJAY_MSG_INFO, "New connection established with Veejay running on %s port %d",
		(group_name == NULL ? hostname : group_name), port_num );

	info->channel = g_io_channel_unix_new( vj_client_get_status_fd( info->client, V_STATUS));



	load_editlist_info();

	load_effectlist_info();
	reload_vimslist();
	//reload_editlist_contents();
	reload_bundles();
	load_effectchain_info();
	load_samplelist_info("tree_samples");
	load_samplelist_info("tree_sources");
	
	info->state = STATE_PLAYING;

	g_io_add_watch_full(
			info->channel,
			G_PRIORITY_DEFAULT,
			G_IO_IN| G_IO_ERR | G_IO_NVAL | G_IO_HUP,
			veejay_tick,
			(gpointer*) info,
			veejay_untick
		);
	info->logging = g_timeout_add( 100, update_log,(gpointer*) info );

	
	// we can set the expanded of the ABC expander
	GtkWidget *exp = glade_xml_get_widget_(
			info->main_window, "veejay_expander");
	gtk_expander_set_expanded( GTK_EXPANDER(exp), FALSE );

	init_cpumeter();

	update_slider_range( "speedslider",(-1 * 64),68, info->status_tokens[SAMPLE_SPEED], 0);
	return 1;
}

static	void	veejay_stop_connecting(vj_gui_t *gui)
{
	if(!gui->sensitive)
		vj_gui_enable();
	vj_launch_toggle(TRUE);
	gtk_progress_bar_set_fraction(
		GTK_PROGRESS_BAR (glade_xml_get_widget_(info->main_window, "connecting")),0.0);
	g_source_remove( info->connecting );

	info->connecting = 0;
}

gboolean	is_alive(gpointer data)
{
	vj_gui_t *gui = (vj_gui_t*) data;
	
	if( gui->state == STATE_STOPPED )
		vj_gui_disconnect();

	if( gui->state == STATE_RECONNECT )
	{
		struct timeval timenow;
		gettimeofday(&timenow, NULL);
		/* at least 2 seconds before trying to connect !*/
		if( (timenow.tv_sec - info->alarm.tv_sec) < TIMEOUT_SECONDS ) 
		{
			if( (timenow.tv_sec - info->timer.tv_sec) > 1.0)
			{
				char	*remote = get_text( "entry_hostname" );
				int	port	= get_nums( "button_portnum" );
				if(!vj_gui_reconnect( remote, NULL, port ))
				{
					gui->state = STATE_RECONNECT;
					memcpy(&(info->timer),&timenow,sizeof(timenow));
				}
				else
				{	/* veejay connected */
					veejay_stop_connecting(gui);
				}	
			}
		}
		else
		{
			vj_gui_stop_launch();	
		}	
	}

	if( gui->state == STATE_PLAYING )
	{
		if(!gui->sensitive)
			vj_gui_enable();
	}

	if( gui->state == STATE_IDLE )
	{
		if(gui->sensitive)
			vj_gui_disable();
		if(!gui->launch_sensitive)
			vj_launch_toggle(TRUE);
	}
	return TRUE;
}

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
	{"button_088"},
	{"button_084"},
	{"button_083"},
	{"button_084"},
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
	{"speedslider"},
	{"new_colorstream"},
	{"audiovolume"},
	{"manualopacity"},
	{"button_fadedur"},
	{"veejaypanel"},
	{"vimsmessage"},
	{NULL} 
};

void	vj_gui_disconnect()
{
	info->state = STATE_IDLE;
	if(info->client)
	{
		g_io_channel_shutdown(info->channel, FALSE, NULL);
		g_io_channel_unref(info->channel);
		g_source_remove( info->logging );

		vj_client_close(info->client);
		vj_client_free(info->client);
		info->client = NULL;
		info->run_state = 0;

		GtkWidget *exp = glade_xml_get_widget_(
			info->main_window, "veejay_expander");
		gtk_expander_set_expanded( GTK_EXPANDER(exp), TRUE );
			vj_msg(VEEJAY_MSG_INFO, "Disconnected - Use Launcher"); 
	}
	/* reset all trees */
	reset_tree("tree_effectlist");
	reset_tree("tree_chain");
	reset_tree("tree_samples");
	reset_tree("tree_sources");
	reset_tree("editlisttree");
	
	/* clear console text */
	clear_textview_buffer("veejaytext");
	clear_textview_buffer("gveejaytext");


}

void	vj_launch_toggle(gboolean value)
{
	GtkWidget *w = glade_xml_get_widget_(info->main_window, "button_veejay" );
	gtk_widget_set_sensitive( GTK_WIDGET(w), value );
	info->launch_sensitive = ( value == TRUE ? 1 : 0);
}

void	vj_gui_disable()
{
	int i = 0;
	while( gwidgets[i].name != NULL )
	{
	 GtkWidget *w = glade_xml_get_widget_( 
				info->main_window, gwidgets[i].name);
	 gtk_widget_set_sensitive( GTK_WIDGET(w), FALSE );
	 i++;
	}
	gtk_widget_set_sensitive( GTK_WIDGET(
			glade_xml_get_widget_(info->main_window, "button_loadconfigfile") ), TRUE );
	info->sensitive = 0;
}

void	vj_gui_enable()
{
	int i =0;
	while( gwidgets[i].name != NULL)
	{
	 GtkWidget *w = glade_xml_get_widget_(
				info->main_window, gwidgets[i].name );
	 gtk_widget_set_sensitive( GTK_WIDGET(w), TRUE );
	 i++;
	}

	// disable loadconfigfile
	gtk_widget_set_sensitive( GTK_WIDGET(
			glade_xml_get_widget_(info->main_window, "button_loadconfigfile") ), FALSE );

	info->sensitive = 1;
}
