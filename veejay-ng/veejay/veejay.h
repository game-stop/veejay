/*
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg < elburg@hio.hen.nl>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 *
 *
 */


#ifndef VEEJAY_H
#define VEEJAY_H
#include <mjpegtools/mpegconsts.h>
#include <mpegtimecode.h>
struct mjpeg_sync
{
   unsigned long frame;      /* Frame (0 - n) for double buffer */
   struct timeval timestamp; /* timestamp */
};

typedef struct
{
    pthread_t playback_thread;
    pthread_t software_playback_thread;//OK	/* the thread for software playback */
    pthread_mutex_t valid_mutex;
    pthread_cond_t buffer_filled[2];
    pthread_cond_t buffer_done[2];
    pthread_mutex_t syncinfo_mutex;
    pthread_t signal_thread;
    pthread_attr_t stattr;
    pthread_attr_t qtattr;
    sigset_t signal_set;
    struct timeval lastframe_completion;	/* software sync variable  OK*/

    uint64_t save_list_len;		/* for editing purposes */

    double spvf;		/* seconds per video frame */
    int usec_per_frame;		/* milliseconds per frame */
    int msec_per_frame;

  //  int min_frame_num;		/* the lowest frame to be played back - normally 0 */
  //  int max_frame_num;		/* the latest frame to be played back - normally num_frames - 1 */
  //  int current_frame_num;	/* the current frame */
  //  int previous_frame_num;	/* previous frame num */
 //   int current_playback_speed;	/* current playback speed */
    uint64_t currently_processed_entry;// OK
    int currently_processed_frame;// OK	/* changes constantly */
    int currently_synced_frame;	/* changes constantly */

    int first_frame;		/* software sync variable */
    int valid[2];	/* num of frames to be played */
    uint64_t buffer_entry[2];
//    int render_entry;
 //   int render_list;
//    int last_rendered_frame;
//    long rendered_frames;
    int preview;
    
    struct mjpeg_sync syncinfo[2];	/* synchronization info */
    uint64_t *save_list;	/* for editing purposes */
//    int abuf_len;
    double spas;		/* seconds per audio sample */
    int audio_mute;		/* controls whether to currently play audio or not */
    
    int state;			/* playing, paused or stoppped   OK */
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


typedef struct
{
	video_playback_setup *settings;
	void		     *current_sample; // settings cached from sample info
	void		     *video_info;
	void		     *performer;
	void			*seek_cache;
	void			*rtc;
	int			preserve_pathnames;
	int			no_bezerk;
	int			timer;
	int			sync_correction;
	int			sync_ins_frames;
	int			sync_skip_frames;
	int			continuous;
	int			verbose;
	int			preview_size;
	int			audio;	
	void			*display;

	void			*status_socket;
	void			*command_socket;
	void			*frame_socket;
	void			*mcast_socket;

	int			current_link;
	int			port_offset;
	int			use_display;
	void			*sdl_display;
	  pthread_mutex_t display_mutex;

	int			itu601;
} veejay_t;

#endif

