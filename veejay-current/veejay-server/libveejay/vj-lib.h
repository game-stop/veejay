/*
 * Copyright (C) 2002 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vj-server.h>
#include <veejaycore/vj-client.h>
#include <veejaycore/yuvconv.h>
#include <libstream/vj-yuv4mpeg.h>
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

#define AUDIO_MODE_SILENCE_FILL 0
#define AUDIO_MODE_CONTENT 1

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
#define VIDEO_QUEUE_LEN 2
typedef struct
{
	int mode;
	int type;
	int timer;
	int min_delay;
	int max_delay;
	unsigned long long seed;
	int next_id;
	int next_mode;
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
	int sample_id;
	int type;
} seq_sample_t;

typedef struct
{
	int   active;
	int   current;
	int   size;
	seq_sample_t	samples[MAX_SEQUENCES];
	int	rec_id;
} sequencer_t;

typedef struct
{
    volatile int active;
    int shape;
    int next_id;
    int next_type;
    double timecode;
    volatile long long start;
    volatile long long end;
	volatile long long pos_sample_b;
    void *ptr;
    int ready;
    volatile int global_state;
	int skip_audio_edge;
	int seq_index;
} transition_t;

typedef enum {
    AUDIO_EDGE_NONE = 0,
    AUDIO_EDGE_DIRECTION, //1
    AUDIO_EDGE_JUMP, // 2
    AUDIO_EDGE_RESET,     // frame 0 / start
    AUDIO_EDGE_SILENCE,   // crossfade from zero
} audio_edge_type_t;


typedef struct {
	int    buflen;
	int16_t *fwdL;
	int16_t *fwdR;


	int16_t *silenceR;
	int16_t *silenceL;

	int16_t *history;
	int history_len;

	int heuristic_applied;

	volatile int    last_direction;
	int    xfade_active;
	
	volatile int pending_edge;
	int edge_linger;
	
	volatile int	fwd_history_valid;
	volatile int	rev_history_valid;

	float *fade_lut;

	int16_t last_sample[8];
	int16_t last_output_sample[8];
	int16_t prev_output_sample[8];

	int     last_sample_valid;
	int		last_best_offset;
	long long		ticks_since_last_flip;

} audio_edge_t; // FIXME delete this whole thing

typedef enum {
	BUFFER_FREE = 0,
	BUFFER_RESERVED,
	BUFFER_FILLED,
	BUFFER_IN_RENDER
} buffer_state_t;

typedef struct {
	uint8_t *pixels[VIDEO_QUEUE_LEN];
	volatile long long seq;
	volatile int current_write;
} display_frame_t;

typedef struct {
	pthread_attr_t playback_attr;
	pthread_t signal_thread;

	VJFrame *buffers[VIDEO_QUEUE_LEN];
	buffer_state_t states[VIDEO_QUEUE_LEN];
	uint64_t frame_seq[VIDEO_QUEUE_LEN];
	uint64_t next_seq;
	pthread_mutex_t mutex;
	pthread_mutex_t control_mutex;
	pthread_mutex_t start_mutex;
	pthread_cond_t start_cond;
	int video_out_ready;

	pthread_cond_t producer_wait_cv;
	pthread_cond_t renderer_wait_cv;
	pthread_cond_t data_ready_cv;
	pthread_t producer_thread;
	pthread_t renderer_thread;
	pthread_t audio_playback_thread;

	display_frame_t display_frame;

	int producer_index;
	int renderer_index;
	volatile int warmup_active;
	int warmup_frames;
	volatile int frames_available;

	volatile int audio_mode;
	volatile int state;
	volatile double audio_master_s;
	volatile double audio_start_offset;
	volatile long long anchor_frame;
	
	volatile int audio_producer_mode;
	volatile int first_audio_frame_ready;
	volatile int audio_slice;
	volatile int audio_slice_len;
	volatile int audio_needs_refill;
	volatile int audio_pending_fade_in;
	volatile int audio_direction_changed;
	volatile int audio_flush_request;
	volatile int audio_mute;
	volatile int audio_last_stretched_samples;
	volatile long long audio_wrap_offset;
	int audio_needs_fade_in;

	double vsync_interval_s;
	double smoothed_drift_us;

	sigset_t signal_set;
	long slept_last_iteration;
	struct timespec lastframe_completion;
	long old_field_len;
	uint64_t save_list_len;
	double spvf;
	int usec_per_frame;
	volatile long long min_frame_num;
	volatile long long max_frame_num;
	volatile long long current_frame_num;
	volatile long long master_frame_num;
	volatile long long audio_target_frame;
	volatile int xruns;
	int current_playback_speed;
	int previous_playback_speed;
	int currently_processed_frame;
	int currently_synced_frame; // FIXME cleanup
	int first_frame;
	int valid[MJPEG_MAX_BUF];
	long buffer_entry[MJPEG_MAX_BUF];
	int render_entry;
	int render_list;
	int last_rendered_frame;
	int sfd;
	long rendered_frames;
	long currently_processed_entry;
	struct mjpeg_sync syncinfo[MJPEG_MAX_BUF];	/* synchronization info */
	uint64_t *save_list;	/* for editing purposes */
	double spas;		/* seconds per audio sample */
	int offline_ready;
	int offline_record;
	int offline_tag_id;
	int offline_created_sample;
    int offline_linked_sample_id;
	int sample_record;
	int sample_record_id;
	int sample_record_switch;
	int full_screen;
	int tag_record_switch;
	int tag_record;
	int dct_method;
	subsample_mode_t sample_mode;
	int unicast_link_id;
	volatile int unicast_frame_sender;
	int is_dat;
	int mcast_mode;
	int mcast_frame_sender;
	int use_mcast;
	char *group_name;
	int use_vims_mcast;
	char *vims_group_name;
	int zoom;
	int composite;
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
	int	fxdepth;
	int	repeat_delay;
	int	repeat_interval;
	uint32_t cycle_count[2]; //@ required for veejay-radar 
	int	sample_restart;
	int	feedback;
	int feedback_stage;
	int hold_fx;
	int hold_fx_prev;
	int hold_pos;
	int	hold_resume;
	int hold_status;
	int auto_mute;
	int pace_correction;
	long pace_correction_us;
	int	splitscreen;
	int clear_alpha;
	int alpha_value;
	int audiostats;
    transition_t transition;
	int is_rt_kernel;
	long long clock_overshoot;
	double pause_cost_ns;
} video_playback_setup;

typedef struct {
    long long current_frame;
    long long skipped_frames;
	long long dropped_frames;
    long long queued_frames;
    double audio_anchor_s;
    double last_audio_master_s;
    double last_pts_s;
    double delta_s;

    double playback_speed;
    long long total_frames_produced;
    long long total_frames_skipped;
    long long warmup_frames_done;
	double render_duration;

    int underruns;
    int overruns;
	int xruns;
} video_playback_stats;


typedef struct {
    int playback_mode;
    int sample_id;
    char *filename;
    int take_screenshot;
    int take_bg;
    int direction;
    int looptype;
    long sample_end;
    long sample_start;
    int play_sample;
    int key_effect;
    int effect_id;
    int loops;
    int next;
    int sample_key;
    int sample_select;
    int sample_pressed;
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
	int drawmode;
	int drawsize;
} user_control;

typedef struct {
	sample_eff_chain *fx_chain[SAMPLE_MAX_EFFECTS];
	int enabled;
	int origin_id;
	int origin_mode;
 } global_chain_t;

#define VIDEO_OUT_SDL 0 
#define VIDEO_OUT_DFB 1
#define VIDEO_OUT_Y4M 3
#define VIDEO_OUT_NONE 4

typedef struct {
    int video_output_width;
    int video_output_height;
    int double_factor;
    int preserve_pathnames;
    int audio;
    int continuous;
    int sync_correction; // FIXME remove
    int sync_skip_frames;
    int sync_ins_frames;
    int auto_deinterlace;
    int load_action_file;
    int load_sample_file;
    editlist *current_edit_list;
    editlist *edit_list;
    editlist *plain_editlist;
	user_control *uc;
    void *osc;
    VJFrame *plugin_frame;
    VJFrameInfo *plugin_frame_info; 
    VJFrame *effect_frame1;
	VJFrame *effect_frame2;
	VJFrameInfo *effect_frame_info;
    VJFrame *effect_frame3;
	VJFrame *effect_frame4;
	VJFrameInfo *effect_frame_info2;
    vjp_kf *effect_info2;	/* effect dependent variables */
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
    void *sdl;
#endif
    vj_yuv *output_stream;
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
    video_playback_setup *settings;
	video_playback_stats stats;
    int real_fps;
    int dump;
    int verbose;
    int bezerk;
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
	void	*splitter;
	int	 use_vp;
	int	 use_proj;
	int	frontback;
	int	out_buf;
	unsigned long *mask;
	unsigned long *cpumask;
	int	ncpus;
	int	sz;
	volatile int audio_running;
	int	*rlinks;
	int *splitted_screens;
	int	*rmodes;
	int pause_render;
	void	*shm;
	int	use_keyb;
	int	use_mouse;
	int	show_cursor;
    int borderless;
	int 	qrcode;
    int read_plug_cfg;
    void *performer;
	global_chain_t *global_chain;
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
