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

/* CHANGES:
	added aclib from mplayer (fast memcpy)
	added vj-tags
	added vj-v4lvideo for multiple v4l devices support
	added yuv4mpeg stream to read from named pipe 
	moved sdl code to vj-sdl
*/

#ifndef VJ_LIB_H
#define VJ_LIB_H
#include <config.h>
#include <sys/time.h>
#include "vj-keyframe.h"
#include "sampleadm.h"
#include "vj-v4lvideo.h"
#include "vj-yuv4mpeg.h"
//#include "vj-dv.h"
#include "vj-dfb.h"
#include "vj-sdl.h"
#include "vj-server.h"
#include "vj-OSC.h"
#include "lav_io.h"
#include "vj-shm.h"
#include "subsample.h"
#include "vj-el.h"

enum {
  NO_AUDIO = 0,
  AUDIO_PLAY = 1,
  AUDIO_RENDER = 2,
};

enum {
    VEEJAY_MSG_INFO = 2,
    VEEJAY_MSG_WARNING = 1,
    VEEJAY_MSG_ERROR = 0,
    VEEJAY_MSG_PRINT = 3,
    VEEJAY_MSG_DEBUG = 4,
};


enum {
    LAVPLAY_STATE_STOP = 0,	/* uninitialized state */
    LAVPLAY_STATE_PAUSED = 1,	/* also known as: speed = 0 */
    LAVPLAY_STATE_PLAYING = 2,	/* speed != 0 */
    LAVPLAY_STATE_RENDER_READY = 3, /* render mode */
};

/* nmacro recorder, 5 lines code for play back of what you changed at navigation */
enum {
    VJ_MACRO_PLAIN_RECORD = 0,
    VJ_MACRO_PLAIN_PLAY = 1,
};

#define MJPEG_MAX_BUF 64
#define VJ_AUDIO_BUF_SIZE 16384



#define DUMMY_DEFAULT_WIDTH 352
#define DUMMY_DEFAULT_HEIGHT 288
#define DUMMY_DEFAULT_FPS 25

/* Video Playback Setup, necessary items for reading and playing video */
struct mjpeg_sync
{
   unsigned long frame;      /* Frame (0 - n) for double buffer */
   unsigned long length;     /* number of code bytes in buffer (capture only) */
   unsigned long seq;        /* frame sequence number */
   struct timeval timestamp; /* timestamp */
};


typedef struct {
    pthread_t software_playback_thread;	/* the thread for software playback */
    pthread_mutex_t valid_mutex;
    pthread_cond_t buffer_filled[MJPEG_MAX_BUF];
    pthread_cond_t buffer_done[MJPEG_MAX_BUF];
    pthread_mutex_t syncinfo_mutex;
    pthread_t signal_thread;
    sigset_t signal_set;
    struct timeval lastframe_completion;	/* software sync variable */

    long old_field_len;
    long save_list_len;		/* for editing purposes */

    double spvf;		/* seconds per video frame */
    int usec_per_frame;		/* milliseconds per frame */
    int msec_per_frame;
    int min_frame_num;		/* the lowest frame to be played back - normally 0 */
    int max_frame_num;		/* the latest frame to be played back - normally num_frames - 1 */
    int current_frame_num;	/* the current frame */
    int previous_frame_num;	/* previous frame num */
    int previous_playback_speed;
    int current_playback_speed;	/* current playback speed */
    int currently_processed_frame;	/* changes constantly */
    int currently_synced_frame;	/* changes constantly */
    int first_frame;		/* software sync variable */
    int valid[MJPEG_MAX_BUF];	/* num of frames to be played */
    long buffer_entry[MJPEG_MAX_BUF];
    int last_rendered_frame;
    long rendered_frames;
    long currently_processed_entry;
    struct mjpeg_sync syncinfo[MJPEG_MAX_BUF];	/* synchronization info */
    unsigned long *save_list;	/* for editing purposes */
    int abuf_len;
    double spas;		/* seconds per audio clip */
    int audio_mute;		/* controls whether to currently play audio or not */
    int state;			/* playing, paused or stoppped */
    int effect;			/* realtime effect during play */
    int video_fd;
    pthread_t playback_thread;	/* the thread for the whole playback-library */
    int freeze_frames_left;
    int play_frames_left;
    int freeze_mode;
    int pure_black;
    int offline_ready;
    int offline_record;
    int offline_tag_id;
    int offline_created_clip;
    int clip_record;
    int clip_record_id;
    int clip_record_switch;
    int tag_record_switch;
    int tag_record;
    int dct_method;
    subsample_mode_t sample_mode;
} video_playback_setup;


typedef struct {
    int stats_changed;		/* has anything bad happened?                        */
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
    int playback_mode;		/* playing plain,clip,tag or pattern */
    int clip_id;		/* which clip or tag is beeing played */
    int hackme;
    int take_bg;
    int direction;		/* forward, reverse or pause */
    int speed;			/* seperate speed control. */
    int looptype;		/* loop setting depending on playmode */
    long clip_end;		/* end of clip */
    long clip_start;		/* start of clip */
    int play_clip;		/* playing clip or not */
    int key_effect;		/* selected effect */
    int effect_id;		/* current effect id */
    int loops;
    int next;
    int clip_key;		/* clip by key */
    int clip_select;		/* selected clip */
    int clip_pressed;		/* which clip key was pressed */
    int chain_changed;
    int use_timer;
    int current_link;
    int port;
    float rtc_delay;
    int is_server;
    int render_changed;
    int input_device;
} user_control;

typedef struct {
    char playback_mode;		/* [HSC] H = hardware/on-screen, C = hardware/on-card, S = software (SDL) */
    int sdl_width;		/* width of the SDL playback window in case of software playback */
    int sdl_height;		/* height of the SDL playback window in case of software playback */
    int soft_full_screen;	/* [0-1] set software-driven full-screen/screen-output, 1 = yes, 0 = no */
    int double_factor;		/* while playing, duplicate each frame double_factor times */
    int preserve_pathnames;
    const char *display;	/* the X-display (only important for -H) */
    int audio;			/* [0-1] Whether to play audio, 0 = no, 1 = yes */
    int continuous;		/* [0-1] 0 = quit when the video has been played, 1 = continue cycle */
    int sync_correction;	/* [0-1] Whether to enable sync correction, 0 = no, 1 = yes */
    int sync_skip_frames;	/* [0-1] If video is behind audio: 1 = skip video, 0 = insert audio */
    int sync_ins_frames;	/* [0-1] If video is ahead of audio: 1 = insert video, 0 = skip audio */
    int auto_deinterlace;
    int load_action_file;
    editlist *edit_list;		/* the playing editlist */
    user_control *uc;		/* user control */
    v4l_video *vj[4];		/* v4l input */
    vj_osc *osc;
    VJFrame *plugin_frame;
    VJFrameInfo *plugin_frame_info; 
    VJFrame *effect_frame1;
	VJFrame *effect_frame2;
	VJFrameInfo *effect_frame_info;
    vj_clip_instr *effect_info;	/* effect dependent variables */
#ifdef HAVE_DIRECTFB
    vj_dfb *dfb;
#endif
    //vj_ladspa_instance *vli;
    //int vli_enabled;
    int video_out;
#ifdef HAVE_SDL
    vj_sdl *sdl;		/* SDL window */
#endif
    int gui_screen;
#ifdef HAVE_SDL
    vj_sdl *sdl_gui;		/* SDL gui window */
#endif
    vj_yuv *output_stream;	/* output stream for dumping video */
    vj_yuv *render_stream;
    video_segment *segment;
    video_segment *client;
    int render_now;	        /* write RGB */
    int render_continous;
    char action_file[256];
    char stream_outname[256];
    int stream_outformat;
    int stream_enabled;
    int last_clip_id;
    int last_tag_id;
    int nstreams;
    int sfd;
    vj_server *vjs;
    vj_server *status;
    int net;
    int no_ffmpeg;		/* use libjpeg for decoding of video */
    int render_entry;
    int render_continue;
    video_playback_setup *settings;	/* private info - don't touch :-) (type UNKNOWN) */
    int real_fps;
    int dump;
    int verbose;
    int no_bezerk;
    int pixel_format;
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
