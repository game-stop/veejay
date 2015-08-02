/*
 * Copyright (C) 2002 Niels Elburg <elburg@hio.hen.nl>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef VJ_LIB_H
#define VJ_LIB_H
#include <config.h>
#include <sys/time.h>
#include <libsample/sampleadm.h>
#include <libvjnet/vj-server.h>
#include <libvjnet/vj-client.h>
#include <libyuv/yuvconv.h>
#include <libstream/vj-yuv4mpeg.h>
#include <veejay/vj-sdl.h>
#include <libel/lav_io.h>
#include <libel/vj-el.h>

enum {
  NO_AUDIO = 0,
  AUDIO_PLAY = 1
};

enum {
    LAVPLAY_STATE_STOP = 0,	/* uninitialized state */
    LAVPLAY_STATE_PAUSED = 1,	/* also known as: speed = 0 */
    LAVPLAY_STATE_PLAYING = 2	/* speed != 0 */
};

/* nmacro recorder, 5 lines code for play back of what you changed at navigation */
enum {
    VJ_MACRO_PLAIN_RECORD = 0,
    VJ_MACRO_PLAIN_PLAY = 1,
};

#define MJPEG_MAX_BUF 2
#define VJ_AUDIO_BUF_SIZE 16384

//classic vars
#define DUMMY_DEFAULT_WIDTH 352
#define DUMMY_DEFAULT_HEIGHT 288
#define DUMMY_DEFAULT_FPS 25

/* Video Playback Setup, necessary items for reading and playing video */
struct mjpeg_sync
{
   unsigned long frame;      /* Frame (0 - n) for double buffer */
   unsigned long length;     /* number of code bytes in buffer (capture only) */
   unsigned long seq;        /* frame sequence number */
   struct timespec timestamp; /* timestamp */
};
#define VJ_SCHED_NONE (1<<0)
#define VJ_SCHED_SL   (1<<1)
#define VJ_SCHED_EL   (1<<2)

typedef struct {
	int state;
	char *sl;
} vj_schedule_t;

#define RANDMODE_INACTIVE 0
#define RANDMODE_SAMPLE 1
#define RANDTYPE_NOFX 0
#define RANDTYPE_PIXEL 1
#define RANDTYPE_GEO 2
#define RANDTYPE_MIXED 3 
#define RANDTIMER_FRAME 1
#define RANDTIMER_LENGTH 0

typedef struct
{
	int mode;
	int type;
	int timer;
	int min_delay;
	int max_delay;
} vj_rand_player;

typedef struct
{
	int chroma;
	char norm; 
	int width;
	int height;
	float fps;
	int active;
	long arate;
} dummy_t;

typedef struct
{
	int   active;
	int   current;
	int   size;
	int	*samples;
	int	rec_id;
} sequencer_t;

typedef struct {
    pthread_t software_playback_thread;
    pthread_t playback_thread;	
    pthread_attr_t playback_attr;
    pthread_t geo_stat;
    pthread_mutex_t valid_mutex;
    pthread_cond_t buffer_filled[MJPEG_MAX_BUF];
    pthread_cond_t buffer_done[MJPEG_MAX_BUF];
    pthread_mutex_t syncinfo_mutex;
    pthread_t signal_thread;
    sigset_t signal_set;
    struct timespec lastframe_completion;	/* software sync variable */
    long old_field_len;
    uint64_t save_list_len;		/* for editing purposes */
    double spvf;		/* seconds per video frame */
    int usec_per_frame;		/* milliseconds per frame */
    int min_frame_num;		/* the lowest frame to be played back - normally 0 */
    int max_frame_num;		/* the latest frame to be played back - normally num_frames - 1 */
    int current_frame_num;	/* the current frame */
    int current_playback_speed;	/* current playback speed */
    int currently_processed_frame;	/* changes constantly */
    int currently_synced_frame;	/* changes constantly */
    int first_frame;		/* software sync variable */
    int valid[MJPEG_MAX_BUF];	/* num of frames to be played */
    long buffer_entry[MJPEG_MAX_BUF];
    int render_entry;
    int render_list;
    int last_rendered_frame;
    long rendered_frames;
    long currently_processed_entry;
    struct mjpeg_sync syncinfo[MJPEG_MAX_BUF];	/* synchronization info */
    uint64_t *save_list;	/* for editing purposes */
    double spas;		/* seconds per audio sample */
    int audio_mute;		/* controls whether to currently play audio or not */
    int state;			/* playing, paused or stoppped */
    int offline_ready;
    int offline_record;
    int offline_tag_id;
    int offline_created_sample;
    int sample_record;
    int sample_record_id;
    int sample_record_switch;
    int full_screen;
    int tag_record_switch;
    int tag_record;
    int dct_method;
    subsample_mode_t sample_mode;
	int unicast_link_id;
	int unicast_frame_sender;
	int is_dat;
	int mcast_mode;
	int mcast_frame_sender;
	int use_mcast;
	char *group_name;
	int use_vims_mcast;
	char *vims_group_name;
	int zoom;
	int composite;
	int composite2;
	sws_template sws_templ;
	vj_schedule_t action_scheduler;
	float	output_fps;
	int crop;
	VJRectangle viewport;
	vj_rand_player randplayer;
	void	*export_image;
	int	links[VJ_MAX_CONNECTIONS]; 
	int	ncpu;
	int	vp_rec;
	int	late[2];
	int	cy;
	int	cx;
	int	cn;
	int	ca;
	int	fxrow[3];
	int	fxdepth;
	int	repeat_delay;
	int	repeat_interval;
	int	simple_frame_dup;
	uint32_t cycle_count[2]; //@ required for veejay-radar 
	int	sample_restart;
	int	feedback;
	int feedback_stage;
	int hold_pos;
	int	hold_resume;
	int hold_status;
    int auto_mute;
	int pace_correction;
} video_playback_setup;

typedef struct {
    unsigned int frame;		/* current frame which is being played back          */
    unsigned int num_corrs_a;	/* Number of corrections because video ahead audio   */
    unsigned int num_corrs_b;	/* Number of corrections because video behind audio  */
    unsigned int num_aerr;	/* Number of audio buffers in error                  */
    unsigned int num_asamps;
    unsigned int nsync;		/* Number of syncs                                   */
    unsigned int nqueue;	/* Number of frames queued                           */
    int play_speed;		/* current playback speed                            */
    int audio;			/* whether audio is currently turned on              */
    int norm;			/* [0-2] playback norm: 0 = PAL, 1 = NTSC, 2 = SECAM */
    double tdiff;		/* video/audio time difference (sync debug purposes) */
} video_playback_stats;

/* User Control , it keeps track of user's actions */
typedef struct {
    int playback_mode;		/* playing plain,sample,tag or pattern */
    int sample_id;		/* which sample or tag is beeing played */
    char *filename;
    int hackme;
    int take_bg;
    int direction;		/* forward, reverse or pause */
    int looptype;		/* loop setting depending on playmode */
    long sample_end;		/* end of sample */
    long sample_start;		/* start of sample */
    int play_sample;		/* playing sample or not */
    int key_effect;		/* selected effect */
    int effect_id;		/* current effect id */
    int loops;
    int next;
    int sample_key;		/* sample by key */
    int sample_select;		/* selected sample */
    int sample_pressed;		/* which sample key was pressed */
    int chain_changed;
    int use_timer;
    int rtc_fd;
    int current_link;
    int port;
    float rtc_delay;
    int is_server;
    int input_device;
    int geox;
    int geoy;
    int file_as_sample;
    int scene_detection;
    int mouse[4];
    char *osd_extra;
    int ram_chain;		/* keep fx chain buffers in RAM (1) or dynamic pattern (0) */
	int max_cached_mem;
	int	max_cached_slots;
} user_control;

typedef struct {
    int video_output_width;		/* width of the SDL playback window in case of software playback */
    int video_output_height;		/* height of the SDL playback window in case of software playback */
    int double_factor;		/* while playing, duplicate each frame double_factor times */
    int preserve_pathnames;
    int audio;			/* [0-1] Whether to play audio, 0 = no, 1 = yes */
    int continuous;		/* [0-1] 0 = quit when the video has been played, 1 = continue cycle */
    int sync_correction;	/* [0-1] Whether to enable sync correction, 0 = no, 1 = yes */
    int sync_skip_frames;	/* [0-1] If video is behind audio: 1 = skip video, 0 = insert audio */
    int sync_ins_frames;	/* [0-1] If video is ahead of audio: 1 = insert video, 0 = skip audio */
    int auto_deinterlace;
    int load_action_file;
    editlist *current_edit_list;
    editlist *edit_list;		/* the playing editlist */
    editlist *plain_editlist;	/* editlist loaded from command line */
	user_control *uc;		/* user control */
    void *osc;
    VJFrame *plugin_frame;
    VJFrameInfo *plugin_frame_info; 
    VJFrame *effect_frame1;
	VJFrame *effect_frame2;
	VJFrameInfo *effect_frame_info;
    vjp_kf *effect_info;	/* effect dependent variables */
#ifdef HAVE_DIRECTFB
    void *dfb;
#endif
    int video_out;
#ifdef HAVE_GL
    void	*gl;
#endif
    void	*y4m;
#ifdef HAVE_SDL
    vj_sdl **sdl;		/* array of SDL windows */
#endif
    vj_yuv *output_stream;	/* output stream for dumping video */
    void *vloopback; // vloopback output
    void *video_out_scaler;
    int render_now;	        /* write RGB */
    int render_continous;
    char action_file[2][1024];
    char y4m_file[1024];
    int stream_outformat;
    int stream_enabled;
    int last_sample_id;
    int last_tag_id;
    int nstreams;
    int sfd;
    vj_server *vjs[4]; /* 0=cmd, 1 = sta, 2 = mcast, 3 = msg */
    int net;
    int render_entry;
    int render_continue;
    video_playback_setup *settings;	/* private info - don't touch :-) (type UNKNOWN) */
    int real_fps;
    int dump;
    int verbose;
    int no_bezerk;
    int pixel_format;
	dummy_t *dummy;
	int bes_width;
	int bes_height;
	char *status_what;
	char *status_msg;
	char *status_line;
	int status_line_len;
	char *homedir;
	void *font;
	void *osd;
	int  use_osd;
	sequencer_t *seq;
	int  no_caching;
	void	*viewport;
	void	*composite;
	void	*composite2;
	int	 use_vp;
	int	 use_proj;
	int	frontback;
	int	out_buf;
	unsigned long *mask;
	unsigned long *cpumask;
	int	ncpus;
	int	sz;
	int	audio_running;
	int	*rlinks;
	int	*rmodes;
	int pause_render;
	void	*shm;
	int	use_keyb;
	int	use_mouse;
	int	show_cursor;
	int 	qrcode;
} veejay_t;

typedef struct {
    int arg;
    int val;
    int press;
    int increment;
    int lock;
    int minterpolate;
    int interpolate;
} vj_key;

#endif
