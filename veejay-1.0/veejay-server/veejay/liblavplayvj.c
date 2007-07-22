/* libveejayvj - a extended librarified Linux Audio Video playback/Editing
 *supports: 
 *		sample based editing
 *		pattern based editing 
 *		throughput of v4l / fifo
 *		
 *		Niels Elburg <nielselburg@yahoo.de>
 *
 *
 * libveejay - a librarified Linux Audio Video PLAYback
 *
 * Copyright (C) 2000 Rainer Johanni <Rainer@Johanni.de>
 * Extended by:   Gernot Ziegler  <gz@lysator.liu.se>
 *                Ronald Bultje   <rbultje@ronald.bitfreak.net>
 *              & many others
 *
 * A library for playing back MJPEG video via softwastre MJPEG
 * decompression (using SDL) 
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or at your option) any later version.
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

/* this is a junkyard, need more modular structure
   input / output modules for pulling/pushing of video frames
   codecs for encoding/decoding video frames
   fancy dlopen and family here
*/

#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/statfs.h>
#include <time.h>
#include "jpegutils.h"
#include "vj-event.h"
#ifndef X_DISPLAY_MISSING
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#ifdef HAVE_FREETYPE
#include <veejay/vj-font.h>
#endif
#ifndef X_DISPLAY_MISSING
#include <veejay/x11misc.h>
#endif
#include <libvjnet/vj-client.h>
#include <veejay/vj-jack.h>
#include <veejay/vj-misc.h>
#ifdef HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#endif
#define VIDEO_MODE_PAL		0
#define VIDEO_MODE_NTSC		1
#define VIDEO_MODE_SECAM	2
#define VIDEO_MODE_AUTO		3

#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvje/vje.h>
#include <veejay/vj-perform.h>
#include <veejay/vj-plug.h>
#include <veejay/vj-lib.h>
#include <libel/vj-avcodec.h>
#include <libel/pixbuf.h>
#include <libyuv/yuvconv.h>
#include <veejay/vj-composite.h>
#include <veejay/vj-viewport.h>
#include <veejay/vj-OSC.h>

#include <veejay/vims.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#ifdef USE_GL
#include <veejay/gl.h>
#endif

#include <sched.h>

static int	veejay_pin_cpu( veejay_t *info, int cpu_num );
static void	veejay_schedule_fifo( veejay_t *info, int pid );

// following struct copied from ../utils/videodev.h

/* This is identical with the mgavideo internal params struct, 
   please tell me if you change this struct here ! <gz@lysator.liu.se) */
struct mjpeg_params
{

   /* The following parameters can only be queried */

   int major_version;            /* Major version number of driver */
   int minor_version;            /* Minor version number of driver */

   /* Main control parameters */

   int input;                    /* Input channel: 0 = Composite, 1 = S-VHS */
   int norm;                     /* Norm: VIDEO_MODE_PAL or VIDEO_MODE_NTSC */
   int decimation;               /* decimation of captured video,
                                    enlargement of video played back.
                                    Valid values are 1, 2, 4 or 0.
                                    0 is a special value where the user
                                    has full control over video scaling */

   /* The following parameters only have to be set if decimation==0,
      for other values of decimation they provide the data how the image is captured */

   int HorDcm;                    /* Horizontal decimation: 1, 2 or 4 */
   int VerDcm;                    /* Vertical decimation: 1 or 2 */
   int TmpDcm;                    /* Temporal decimation: 1 or 2,
                                     if TmpDcm==2 in capture every second frame is dropped,
                                     in playback every frame is played twice */
   int field_per_buff;            /* Number of fields per buffer: 1 or 2 */
   int img_x;                     /* start of image in x direction */
   int img_y;                     /* start of image in y direction */
   int img_width;                 /* image width BEFORE decimation,
                                     must be a multiple of HorDcm*16 */
   int img_height;                /* image height BEFORE decimation,
                                     must be a multiple of VerDcm*8 */

   /* --- End of parameters for decimation==0 only --- */

   /* JPEG control parameters */

   int  quality;                  /* Measure for quality of compressed images.
                                     Scales linearly with the size of the compressed images.
                                     Must be beetween 0 and 100, 100 is a compression
                                     ratio of 1:4 */

   int  odd_even;                 /* Which field should come first ???
                                     This is more aptly named "top_first",
                                     i.e. (odd_even==1) --> top-field-first */

   int  APPn;                     /* Number of APP segment to be written, must be 0..15 */
   int  APP_len;                  /* Length of data in JPEG APPn segment */
   char APP_data[60];             /* Data in the JPEG APPn segment. */

   int  COM_len;                  /* Length of data in JPEG COM segment */
   char COM_data[60];             /* Data in JPEG COM segment */

   unsigned long jpeg_markers;    /* Which markers should go into the JPEG output.
                                     Unless you exactly know what you do, leave them untouched.
                                     Inluding less markers will make the resulting code
                                     smaller, but there will be fewer aplications
                                     which can read it.
                                     The presence of the APP and COM marker is
                                     influenced by APP0_len and COM_len ONLY! */
#define JPEG_MARKER_DHT (1<<3)    /* Define Huffman Tables */
#define JPEG_MARKER_DQT (1<<4)    /* Define Quantization Tables */
#define JPEG_MARKER_DRI (1<<5)    /* Define Restart Interval */
#define JPEG_MARKER_COM (1<<6)    /* Comment segment */
#define JPEG_MARKER_APP (1<<7)    /* App segment, driver will allways use APP0 */

   int  VFIFO_FB;                 /* Flag for enabling Video Fifo Feedback.
                                     If this flag is turned on and JPEG decompressing
                                     is going to the screen, the decompress process
                                     is stopped every time the Video Fifo is full.
                                     This enables a smooth decompress to the screen
                                     but the video output signal will get scrambled */

   /* Misc */

	char reserved[312];  /* Makes 512 bytes for this structure */
};

//#include <videodev_mjpeg.h>
#include <pthread.h>
#include <signal.h>
#ifdef HAVE_SDL
#include <SDL/SDL.h>
#define MAX_SDL_OUT	2
#endif
#include <mpegconsts.h>
#include <mpegtimecode.h>
#include <libstream/vj-tag.h>
#include "libveejay.h"
#include <mjpegtools/mjpeg_types.h>
#include "vj-perform.h"
#include <libvjnet/vj-server.h>
#include "mjpeg_types.h"
#ifdef HAVE_DIRECTFB
#include <veejay/vj-dfb.h>
#endif

#ifdef HAVE_V4L
#include <libstream/vj-vloopback.h>
#endif

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ( (caddr_t) -1 )
#endif
#define HZ 100
#include <libel/vj-el.h>
#define VALUE_NOT_FILLED -10000

extern void vj_osc_set_veejay_t(veejay_t *t);

#ifdef HAVE_SDL
extern int vj_event_single_fire(void *ptr, SDL_Event event, int pressed);
#endif
static int	total_mem_mb_ = 0;
static int 	chunk_size_ = 0;
static int	n_cache_slots_ = 0;
int	get_num_slots(void)
{
	return n_cache_slots_;
}
int	get_total_mem(void)
{
	return total_mem_mb_;
}
int	get_chunk_size(void)
{
	return chunk_size_;
}

int veejay_get_state(veejay_t *info) {
	video_playback_setup *settings = (video_playback_setup*)info->settings;
	return settings->state;
}
/******************************************************
 * veejay_change_state()
 *   change the state
 ******************************************************/
static void	veejay_reset_el_buffer( veejay_t *info );

void veejay_change_state(veejay_t * info, int new_state)
{
    	video_playback_setup *settings =
		(video_playback_setup *) info->settings;
        settings->state = new_state;
}
void veejay_change_state_save(veejay_t * info, int new_state)
{
	if(new_state == LAVPLAY_STATE_STOP )
	{
		char recover_samples[1024];
		char recover_edl[1024];
		pid_t my_pid = getpid();
		snprintf(recover_samples,1024, "%s/recovery/recovery_samplelist_%d.sl", info->homedir, (int) my_pid);
		snprintf(recover_edl, 1024, "%s/recovery/recovery_editlist_%d.edl", info->homedir, (int) my_pid);

		int rs = sample_writeToFile( recover_samples,info->seq,info->font,
				info->uc->sample_id, info->uc->playback_mode );
		int re = veejay_save_all( info, recover_edl, 0, 0 );
		if(rs)
			veejay_msg(VEEJAY_MSG_WARNING, "Saved samplelist to %s", recover_samples );
		if(re)
			veejay_msg(VEEJAY_MSG_WARNING, "Saved Editlist to %s", recover_edl );
	}
	veejay_change_state( info, new_state );
}

int veejay_set_framedup(veejay_t *info, int n) {
	video_playback_setup *settings = (video_playback_setup*) settings;

	switch(info->uc->playback_mode) {
	  case VJ_PLAYBACK_MODE_PLAIN: 
			info->sfd = n; 
			break;
	  case VJ_PLAYBACK_MODE_SAMPLE: 
			sample_set_framedup(info->uc->sample_id,n);
			sample_set_framedups(info->uc->sample_id,0);
		break;
	  default:
		return -1;
	}
        return 1;
}

/******************************************************
 * veejay_set_speed()
 *   set the playback speed (<0 is play backwards)
 *
 * return value: 1 on success, 0 on error
 ******************************************************/
int veejay_set_speed(veejay_t * info, int speed)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    int len=0;
	

    if( speed > MAX_SPEED )
		speed = MAX_SPEED;
    if( speed < -(MAX_SPEED))
		speed = -(MAX_SPEED);

    switch (info->uc->playback_mode)
	{

	case VJ_PLAYBACK_MODE_PLAIN:
		len = info->current_edit_list->video_frames - 1;
		if( abs(speed) <= len )
			settings->current_playback_speed = speed;	
		else
			veejay_msg(VEEJAY_MSG_DEBUG, "Speed %d too high to set!", speed);

		break;
    case VJ_PLAYBACK_MODE_SAMPLE:
		len = sample_get_endFrame(info->uc->sample_id) - sample_get_startFrame(info->uc->sample_id);
		if( speed < 0)
		{
			if ( (-1*len) > speed )
			{
				veejay_msg(VEEJAY_MSG_ERROR,"Speed %d too high to set!",speed);
				return 1;
			}
		}
		else
		{
			if(speed >= 0)
			{
				if( len < speed )
				{
					veejay_msg(VEEJAY_MSG_ERROR, "Speed %d too high to set",speed);
					return 1;
				}
			}
		}
		if(sample_set_speed(info->uc->sample_id, speed) != -1)
			settings->current_playback_speed = speed;
		break;

    case VJ_PLAYBACK_MODE_TAG:
		
		settings->current_playback_speed = 1;
		break;
    default:
		veejay_msg(VEEJAY_MSG_ERROR, "Unknown playback mode");
		break;
    }
#ifdef HAVE_JACK

    if(info->audio == AUDIO_PLAY )
		vj_jack_continue( settings->current_playback_speed );
#endif

    return 1;
}


/******************************************************
 * veejay_increase_frame()
 *   increase (or decrease) a num of frames
 *
 * return value: 1 on succes, 0 if we had to change state
 ******************************************************/
int veejay_increase_frame(veejay_t * info, long num)
{

    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

   if( info->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN)
   {
		if(settings->current_frame_num < settings->min_frame_num) return 0;
		if(settings->current_frame_num > settings->max_frame_num) return 0;
   }
   else   if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
   {
		if ((settings->current_frame_num + num) <=
		    sample_get_startFrame(info->uc->sample_id)) return 0;
		if((settings->current_frame_num + num) >=
		    sample_get_endFrame(info->uc->sample_id)) return 0;
    
    }
    settings->current_frame_num += num;

    return 1;
}

/******************************************************
 * veejay_busy()
 *   Wait until playback is finished
 ******************************************************/

void veejay_busy(veejay_t * info)
{
    pthread_join( ((video_playback_setup*)(info->settings))->playback_thread, NULL );
}


/******************************************************
 * veejay_free()
 *   free() the struct
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/
int veejay_free(veejay_t * info)
{
	video_playback_setup *settings =
	(video_playback_setup *) info->settings;

	veejay_reap_messages();

	vj_event_stop();

	sample_del_all();

	vj_effect_shutdown();

     	vj_tag_free();
   	vj_el_free(info->current_edit_list);
   	vj_avcodec_free();

	vj_el_deinit();	

	sample_free();

//	vj_tag_free();

  	if( info->settings->zoom )
		yuv_free_swscaler( info->video_out_scaler );
	if( info->settings->composite )
		composite_destroy( info->composite );
	if( info->settings->action_scheduler.state )
	{
		if(info->settings->action_scheduler.sl )
			free(info->settings->action_scheduler.sl );
		info->settings->action_scheduler.state = 0;
	}

//	if( info->plugin_frame) vj_perform_free_plugin_frame(info->plugin_frame);
//	if( info->plugin_frame_info) free(info->plugin_frame_info);
	if( info->effect_frame1) free(info->effect_frame1);
	if( info->effect_frame_info) free(info->effect_frame_info);
	if( info->effect_frame2) free(info->effect_frame2);
	if( info->effect_info) free( info->effect_info );
	if( info->dummy ) free(info->dummy );
#ifdef HAVE_SDL
	free(info->sdl);
#endif
	free( info->seq->samples );
	free( info->seq );

        free(info->status_msg);
        free(info->status_what);
	free(info->homedir);  
        free(info->uc);
	if(info->cpumask)
	  free(info->cpumask);
	if(info->mask)
          free(info->mask);
        free(settings);
        free(info);
    return 1;
}

void veejay_quit(veejay_t * info)
{
    veejay_change_state(info, LAVPLAY_STATE_STOP);
}



/******************************************************
 * veejay_set_frame()
 *   set the current framenum
 *
 * return value: 1 on success, 0 if we had to change state
 ******************************************************/
int veejay_set_frame(veejay_t * info, long framenum)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    if(framenum < 0)
		return -1;

    if( framenum > settings->max_frame_num )
		framenum = settings->max_frame_num;

    if(info->uc->playback_mode==VJ_PLAYBACK_MODE_SAMPLE)
	{
		int start,end,loop,speed;	
		sample_get_short_info(info->uc->sample_id,&start,&end,&loop,&speed);
		if(framenum < start)
		  framenum = start;
		if(framenum > end) 
		  framenum = end;
    	}
	else if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
	{
		if( framenum > settings->max_frame_num )
			framenum = settings->max_frame_num;
	}

    settings->current_frame_num = framenum;


    return 1;  
}

int	veejay_composite_active( veejay_t *info )
{
	return info->settings->composite;
}

void	veejay_composite_transform_points( veejay_t *info, int *in_x, int *in_y, int points, int blob_id )
{
	composite_transform_points( info->composite, in_x, in_y, points, blob_id);
}

void	veejay_auto_loop(veejay_t *info)
{
	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN)
	{
		char sam[32];
		sprintf(sam, "%03d:0 -1;", VIMS_SAMPLE_NEW);
		vj_event_parse_msg(info, sam,strlen(sam));
		sprintf(sam, "%03d:-1;", VIMS_SAMPLE_SELECT);
		vj_event_parse_msg(info,sam,strlen(sam));
	}
}
/******************************************************
 * veejay_init()
 * check the given settings and initialize almost
 * everything
 * return value: 0 on success, -1 on error
 ******************************************************/

void	veejay_set_framerate( veejay_t *info , float fps )
{
	video_playback_setup *settings = (video_playback_setup*) info->settings;
	settings->spvf = 1.0 / fps;
	settings->msec_per_frame = 1000 / settings->spvf;
	if (info->current_edit_list->has_audio && (info->audio==AUDIO_PLAY || info->audio==AUDIO_RENDER))
        	settings->spas = 1.0 / (double) info->current_edit_list->audio_rate;
   	else
        	settings->spas = 0;

        settings->usec_per_frame = (int)(1000000.0 / fps);
}


int veejay_init_editlist(veejay_t * info)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    editlist *el = info->current_edit_list;

    /* Set min/max options so that it runs like it should */
    settings->min_frame_num = 0;
    settings->max_frame_num = el->video_frames - 1;

    settings->current_frame_num = settings->min_frame_num;
    settings->previous_frame_num = 1;
    settings->spvf = 1.0 / el->video_fps;
    settings->msec_per_frame = 1000 / settings->spvf;
    veejay_msg(VEEJAY_MSG_DEBUG, 
		"1.0/Seconds per video Frame = %4.4f",
		1.0 / settings->spvf);

    /* Seconds per audio sample: */
 
   if( !el->has_audio )
	veejay_msg(VEEJAY_MSG_DEBUG, "EditList has no audio");

   if (el->has_audio && (info->audio==AUDIO_PLAY || info->audio==AUDIO_RENDER))
   {
	settings->spas = 1.0 / (double) el->audio_rate;
   }
   else
   {
	settings->spas = 0;
   }

   vj_el_set_image_output_size( el );

   return 0;
}

static	int	veejay_stop_playing_sample( veejay_t *info, int new_sample_id )
{
	if(!sample_stop_playing( info->uc->sample_id, new_sample_id ) )
	{
		veejay_msg(0, "There is no sample %d", new_sample_id );
		return 0;
	}
	sample_chain_free( info->uc->sample_id );	
	veejay_reset_el_buffer(info);
	return 1;
}
static  void	veejay_stop_playing_stream( veejay_t *info, int new_stream_id )
{
	vj_tag_disable( info->uc->sample_id );
	vj_tag_chain_free( info->uc->sample_id );
}
static	int	veejay_start_playing_sample( veejay_t *info, int sample_id )
{
	int looptype,speed,start,end;
	video_playback_setup *settings = info->settings;
	if(!sample_exists(sample_id) )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample %d does not exist", sample_id);
		return 0;
	}

	info->edit_list = sample_get_editlist( sample_id );
	veejay_reset_el_buffer(info);

	sample_start_playing( sample_id, info->no_caching );
	int tmp = sample_chain_malloc( sample_id );

   	sample_get_short_info( sample_id , &start,&end,&looptype,&speed);

	settings->min_frame_num = 0;
	settings->max_frame_num = sample_video_length( sample_id );

#ifdef HAVE_FREETYPE
	if(info->font && info->uc->sample_id != sample_id)
	{
		void *dict = sample_get_dict( sample_id );
		vj_font_set_constraints_and_dict(
				info->font,
				settings->min_frame_num,
				settings->max_frame_num,
				info->edit_list->video_fps,
				dict
			);

		veejay_msg(VEEJAY_MSG_DEBUG, "Subtitling sample %d: %ld - %ld", sample_id,
			  settings->min_frame_num, settings->max_frame_num );

	}
#endif

	 info->uc->sample_id = sample_id;
	 info->last_sample_id = sample_id;

	 info->sfd = sample_get_framedup(sample_id);

	 info->uc->render_changed = 1; /* different render list */
    	
	 sample_reset_offset( sample_id );	/* reset mixing offsets */
    	 veejay_set_frame(info, start);
    	 veejay_set_speed(info, speed);
 	 veejay_msg(VEEJAY_MSG_INFO, "Playing sample %d (FX=%x, Sl=%d, Speed=%d, Start=%d, Loop=%d)",
			sample_id, tmp,info->sfd, speed, start, looptype );


	return 1;
}

static	int	veejay_start_playing_stream(veejay_t *info, int stream_id )
{
	video_playback_setup *settings = info->settings;
	if(!vj_tag_exists(stream_id))
        {
		   veejay_msg(VEEJAY_MSG_ERROR, "Stream %d does not exist", stream_id);
	     	   return 0;
        }
	
	if(vj_tag_enable( stream_id ) <= 0 )
	{
		veejay_msg(0, "Unable to activate stream ?");
		return 0;
	}

	vj_tag_set_active( stream_id, 1 );

	int	tmp = vj_tag_chain_malloc( stream_id);

	info->uc->render_changed = 1;
	settings->min_frame_num = 0;
	settings->max_frame_num = vj_tag_get_n_frames( stream_id );

#ifdef HAVE_FREETYPE
	  if(info->font )
	  {
		  void *dict = vj_tag_get_dict( stream_id );
		  vj_font_set_constraints_and_dict( info->font, settings->min_frame_num,
				  settings->max_frame_num, info->edit_list->video_fps, dict );
		  veejay_msg(VEEJAY_MSG_DEBUG, "Subtitling stream %d: %ld - %ld", stream_id,
				  settings->min_frame_num, settings->max_frame_num );
	  }
#endif
	  info->last_tag_id = stream_id;
	  info->uc->sample_id = stream_id;
	
	 veejay_msg(VEEJAY_MSG_INFO,"Playing stream %d (FX=%x) (Ff=%d)", stream_id, tmp,
			settings->max_frame_num );

	 //@ use edl of plain/dummy
	  info->edit_list = info->current_edit_list;
  	  veejay_reset_el_buffer(info);
	
	return 1;
}

void veejay_change_playback_mode( veejay_t *info, int new_pm, int sample_id )
{
	if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
	{
		int cur_id = info->uc->sample_id;
		if( cur_id == sample_id && new_pm == VJ_PLAYBACK_MODE_SAMPLE )
		{
			int start = sample_get_startFrame( cur_id );
			veejay_set_frame(info,start);
			veejay_msg(VEEJAY_MSG_INFO, "Sample %d starts playing from frame %d",sample_id,start);
			return;
		}
		else
		{
			if(!veejay_stop_playing_sample(info, sample_id ))
				return;
		}
	}
	if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
	{
		int cur_id = info->uc->sample_id;
		if( cur_id == sample_id && new_pm == VJ_PLAYBACK_MODE_TAG )
		{
			veejay_msg(0, "Already playing stream %d", cur_id );
			return;
		}
		else
		{
			veejay_stop_playing_stream(info, sample_id );
		}
	}

	if(new_pm == VJ_PLAYBACK_MODE_PLAIN )
	{
		if(info->uc->playback_mode==VJ_PLAYBACK_MODE_TAG) 
			veejay_stop_playing_stream( info , 0);
		if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
			veejay_stop_playing_sample( info, 0 );
		info->uc->playback_mode = new_pm;
		info->edit_list = info->current_edit_list;
		video_playback_setup *settings = info->settings;
		settings->min_frame_num = 0;
		settings->max_frame_num = info->edit_list->video_frames-1;
		veejay_msg(VEEJAY_MSG_INFO, "Playing plain video, frames %d - %d",
			(int)settings->min_frame_num,  (int)settings->max_frame_num );
	}
	if(new_pm == VJ_PLAYBACK_MODE_TAG)
	{
		info->uc->playback_mode = new_pm;
		veejay_start_playing_stream(info,sample_id);
	}
	if(new_pm == VJ_PLAYBACK_MODE_SAMPLE) 
	{
		info->uc->playback_mode = new_pm;
		veejay_start_playing_sample(info,sample_id );
	}
}

void	veejay_set_sample_f(veejay_t *info, int sample_id, int offset )
{
    	if ( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
	{
		veejay_start_playing_stream(info,sample_id );
     	}
     	else if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
	{
		if( info->uc->sample_id == sample_id )
		{
			int start = sample_get_startFrame( info->uc->sample_id );
			veejay_set_frame(info,start+offset);
			veejay_msg(VEEJAY_MSG_INFO, "Sample %d starts playing from frame %d",sample_id,start);
		}
		else
		{
			veejay_start_playing_sample(info,sample_id );
			veejay_set_frame( info, sample_get_startFrame(sample_id)+offset );
		}
	}
}

void veejay_set_sample(veejay_t * info, int sampleid)
{
    	if ( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
	{
		veejay_start_playing_stream(info,sampleid );
     	}
     	else if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
	{
		if( info->uc->sample_id == sampleid )
		{
			int start = sample_get_startFrame( info->uc->sample_id );
			veejay_set_frame(info,start);
			veejay_msg(VEEJAY_MSG_INFO, "Sample %d starts playing from frame %d",sampleid,start);
		}
		else
			veejay_start_playing_sample(info,sampleid );
	}
}

/******************************************************
 * veejay_create_sample
 *  create a new sample
 * return value: 1 on success, -1 on error
 ******************************************************/
int veejay_create_tag(veejay_t * info, int type, char *filename,
			int index, int palette, int channel)
{
	if( type == VJ_TAG_TYPE_NET || type == VJ_TAG_TYPE_MCAST )
	{
		if( (filename != NULL) && ((strcasecmp( filename, "localhost" ) == 0)  || (strcmp( filename, "127.0.0.1" ) == 0)) )
		{
			if( channel == info->uc->port )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "It makes no sense to connect to myself (%s - %d)",
					filename,channel);
				return 0;
			}	   
		}
	}
//@ palette is used for other things
	int id = vj_tag_new(type, filename, index, info->current_edit_list, info->pixel_format, palette, channel );

	char descr[200];
	veejay_memset(descr,0,200);
	vj_tag_get_by_type(type,descr);
	if(id > 0 )
	{
		info->nstreams++;
		veejay_msg(VEEJAY_MSG_INFO, "New Input Stream '%s' with ID %d created",descr, id );
		return id;
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to create new Input Stream '%s'", descr );
    	}
 	return 0;
}

/******************************************************
 * veejay_stop()
 *   stop playing
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/

int veejay_stop(veejay_t * info)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    if (settings->state == LAVPLAY_STATE_STOP) {
	if(info->uc->playback_mode==VJ_PLAYBACK_MODE_TAG) {
		vj_tag_set_active(info->uc->sample_id,0);
	}
	if(info->stream_enabled) {
	  info->stream_enabled = 0;
	  vj_yuv_stream_stop_write(info->output_stream);
	}
    }

    veejay_change_state(info, LAVPLAY_STATE_STOP);

    /*pthread_cancel( settings->playback_thread ); */
    pthread_join(settings->playback_thread, NULL);

    return 1;
}

/* stop playing a sample, continue with video */
void veejay_stop_sampling(veejay_t * info)
{
    info->uc->playback_mode = VJ_PLAYBACK_MODE_PLAIN;
    info->uc->sample_id = 0;
    info->uc->sample_start = 0;
    info->uc->sample_end = 0;
}

/******************************************************
 * veejay_SDL_update()
 *   when using software playback - there's a new frame
 *   new frame can enter by body, or be put in info->vb->yuv.
 *   this will probably change.
 * return value: 1 on success, 0 on error
 ******************************************************/
static int veejay_screen_update(veejay_t * info )
{
	uint8_t *frame[3];
	uint8_t *c_frame[3];
	int i = 0;
	int skip_update = 0;
	vj_perform_unlock_primary_frame();

	video_playback_setup *settings = info->settings;

	if(settings->composite==1)
	{
		if(!vj_sdl_lock( info->sdl[0] ) )
			return 0;
	
		uint8_t *yuyv = vj_sdl_get_yuv_overlay(info->sdl[0]);
	
		composite_blit( info->composite, yuyv );

		if(!vj_sdl_unlock( info->sdl[0]) )
			return 0;
	
		skip_update = 1;
	}
	else if(settings->zoom )
	{
		VJFrame src,dst;
		memset(&src,0,sizeof(VJFrame));
		memset(&dst,0,sizeof(VJFrame));

		if(info->settings->crop)
		{
			int w = 0;
			int h = 0;
			vj_perform_get_crop_dimensions( info, &w, &h );
			vj_get_yuv_template( &src, w,h, info->pixel_format );
		}	
		else
		{
			vj_get_yuv_template( &src, info->current_edit_list->video_width,
						   info->current_edit_list->video_height,
						   info->pixel_format );
		}

		vj_get_yuv_template( &dst, info->video_output_width,
					   info->video_output_height,
					   info->pixel_format );

		if(info->settings->crop)
			vj_perform_get_cropped_frame(info, src.data, 1);
		else
			vj_perform_get_primary_frame(info, src.data );

		vj_perform_get_output_frame( dst.data );

		yuv_convert_and_scale( info->video_out_scaler, src.data, dst.data );	

		vj_perform_get_output_frame(  frame );
	}
 	else
		vj_perform_get_primary_frame(info,frame);


#ifdef HAVE_JPEG
#ifdef USE_GDK_PIXBUF 
    	/* dirty hack to save a frame to image */
	if (info->uc->hackme == 1)
	{
		info->uc->hackme = 0;

#ifdef USE_GDK_PIXBUF
		if(!vj_picture_save( info->settings->export_image, frame, 
				info->video_output_width, info->video_output_height,
				get_ffmpeg_pixfmt( info->current_edit_list->pixel_format )) )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
				"Unable to write frame %ld to image as '%s'",
					info->settings->current_frame_num, info->settings->export_image );
		}
#else
#ifdef HAVE_JPEG
		vj_perform_screenshot2(info, frame);
		if(info->uc->filename) free(info->uc->filename);
#endif
#endif
	}
#endif
#endif

      
	/* hack to write YCbCr data to stream*/
	if (info->stream_enabled == 1)
	{
		vj_perform_get_primary_frame_420p(info,c_frame );
		if (vj_yuv_put_frame(info->output_stream, c_frame) == -1)
		{
		    veejay_msg(VEEJAY_MSG_ERROR, 
			"Error writing Y4M frame. Aborting Y4M output stream!");
		    vj_yuv_stream_stop_write(info->output_stream);
		    info->stream_enabled = 0;
		}
    	}
	
#ifdef HAVE_V4L
	if( info->vloopback )
	{
		vj_vloopback_fill_buffer( info->vloopback , frame );		
		if( vj_vloopback_get_mode( info->vloopback ))
			vj_vloopback_write_pipe( info->vloopback );
	}
#endif

	if( info->settings->unicast_frame_sender )
	{
		vj_perform_send_primary_frame_s2(info, 0, info->settings->unicast_link_id);
		info->settings->unicast_frame_sender = 0;
	}
	if( info->settings->mcast_frame_sender && info->settings->use_vims_mcast )
	{
		vj_perform_send_primary_frame_s2(info, 1, info->uc->current_link);
	}


	if( skip_update )
	{
		vj_sdl_flip(info->sdl[0]);
		return 1;
	}

    	switch (info->video_out)
	{
#ifdef HAVE_SDL
		case 0:
			for(i = 0 ; i < MAX_SDL_OUT; i ++ )
			if( info->sdl[i] )
				if(!vj_sdl_update_yuv_overlay( info->sdl[i], frame ) )  return 0;  
	   	break;
#endif
#ifdef HAVE_DIRECTFB
		case 1:
	   		vj_perform_get_primary_frame_420p(info,c_frame);
	    		if (vj_dfb_update_yuv_overlay(info->dfb, c_frame) != 0)
			{
				return 0;
	    		}
	    	break;
		case 2:
#ifdef HAVE_SDL
			for( i = 0; i < MAX_SDL_OUT; i ++ )
				if( info->sdl[i] ) 	
		  			if(!vj_sdl_update_yuv_overlay( info->sdl[i], frame ) )
					       	return 0;
#endif
	    		vj_perform_get_primary_frame_420p(info,c_frame);
	    		if (vj_dfb_update_yuv_overlay(info->dfb, c_frame) != 0)
			{
				return 0;
	    		}
	   		 break;
#endif
		case 3:
			if(settings->zoom)
	     			vj_perform_get_output_frame_420p(info, c_frame, info->video_output_width, info->video_output_height );
	     		else
				vj_perform_get_primary_frame_420p(info,c_frame);
	   
       			if (vj_yuv_put_frame(info->render_stream, c_frame) == -1)
	     		{
				veejay_msg(VEEJAY_MSG_ERROR, 
				    "Error writing Y4M frame to output stream. Ending veejay session...");
				vj_yuv_stream_stop_write(info->render_stream);
				veejay_change_state_save(info, LAVPLAY_STATE_STOP);
				return 0;
	     		}
	    		 break;
		case 4:
#ifdef USE_GL
			x_display_push( info->gl, frame , info->current_edit_list->video_width,
					 info->current_edit_list->video_height, 
					info->current_edit_list->pixel_format 	);
#endif
			break;
		case 5:
			break;	
	default:
		veejay_change_state(info,LAVPLAY_STATE_STOP);
		return 0;
		break;
    }
  	
    return 1;
}




/******************************************************
 * veejay_mjpeg_software_frame_sync()
 *   Try to keep in sync with nominal frame rate,
 *     timestamp frame with actual completion time
 *     (after any deliberate sleeps etc)
 *
 * return value: 1 on success, 0 on error
 ******************************************************/

static void veejay_mjpeg_software_frame_sync(veejay_t * info,
					      int frame_periods)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

	if (info->uc->use_timer ) {

    /* I really *wish* the timer was higher res on x86 Linux... 10mSec
     * is a *pain*.  Sooo wasteful here...
     */

	struct timeval now;
	struct timespec nsecsleep;
	int usec_since_lastframe=0;

	for (;;) {
	//fixme?	
	    gettimeofday(&now, 0);
		
	    usec_since_lastframe =
		now.tv_usec - settings->lastframe_completion.tv_usec;
	     //usec_since_lastframe = vj_get_relative_time();
	    if (usec_since_lastframe < 0)
		usec_since_lastframe += 1000000;
	    if (now.tv_sec > settings->lastframe_completion.tv_sec + 1)
		usec_since_lastframe = 1000000;

	    if (settings->first_frame ||
		(frame_periods * settings->usec_per_frame -
		usec_since_lastframe) < (1000000 / HZ))
		break;
	    	
	    /* Assume some other process will get a time-slice before
	     * we do... and hence the worst-case delay of 1/HZ after
	     * sleep timer expiry will apply. Reasonable since X will
	     * probably do something...
	     */
	    nsecsleep.tv_nsec = (frame_periods * settings->usec_per_frame - usec_since_lastframe - 1000000 / HZ) * 1000;
	    nsecsleep.tv_sec = 0;
	    nanosleep(&nsecsleep, NULL);
	}
    }

    settings->first_frame = 0;

    /* We are done with writing the picture - Now update all surrounding info */
	gettimeofday(&(settings->lastframe_completion), 0);
        settings->syncinfo[settings->currently_processed_frame].timestamp =
  	  settings->lastframe_completion;


}


void veejay_pipe_write_status(veejay_t * info, int link_id)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    int d_len = 0;
    int res = 0;
    int pm = info->uc->playback_mode;
    int total_slots = sample_size()-1;
    int tags = vj_tag_true_size() -1;
	int cache_used = 0;
	if(tags>0)
		total_slots+=tags;
   if(total_slots < 0)
	total_slots = 0;

   int mstatus = vj_event_macro_status();

    switch (info->uc->playback_mode) {
    	case VJ_PLAYBACK_MODE_SAMPLE:
		cache_used = sample_cache_used(0);

		if( info->settings->randplayer.mode ==
			RANDMODE_SAMPLE)
			pm = VJ_PLAYBACK_MODE_PATTERN;
		if( sample_chain_sprint_status
			(info->uc->sample_id,cache_used,info->seq->active,info->seq->current,info->real_fps,settings->current_frame_num, pm, total_slots,info->seq->rec_id,mstatus,info->status_what ) != 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Fatal error, tried to collect properties of invalid sample");
#ifdef STRICT_CHECKING
			assert(0);
#else
			veejay_change_state( info, LAVPLAY_STATE_STOP );
#endif	
		}
		break;
       	case VJ_PLAYBACK_MODE_PLAIN:
		veejay_sprintf(info->status_what,1024, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
			info->real_fps,
			settings->current_frame_num,
			info->uc->playback_mode,
			0,
			0,
			settings->min_frame_num,
			settings->max_frame_num,
			settings->current_playback_speed,
			0, 
			0,
			0,
			0,
			0,
			0,
			0,	
			0,
			total_slots,
			cache_used,
		      	0,
		        0,
			0,
			0,
			mstatus );
		break;
    	case VJ_PLAYBACK_MODE_TAG:
		if( vj_tag_sprint_status( info->uc->sample_id,cache_used,info->seq->active,info->seq->current, info->real_fps,
			settings->current_frame_num, info->uc->playback_mode,total_slots,mstatus, info->status_what ) != 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid status!");
		}
		break;
    }
    
	d_len = strlen(info->status_what);
	snprintf(info->status_msg,MESSAGE_SIZE, "V%03dS", d_len );

    	res = vj_server_send(info->vjs[VEEJAY_PORT_STA],link_id, info->status_msg, 5);
    	if( res <= 0)
	{ /* close command socket */
		veejay_msg(VEEJAY_MSG_DEBUG ,"Error sending status message , closing connection(s)");
		_vj_server_del_client(info->vjs[VEEJAY_PORT_CMD], link_id );
		_vj_server_del_client(info->vjs[VEEJAY_PORT_STA], link_id );
	}
    	res = vj_server_send(info->vjs[VEEJAY_PORT_STA], link_id, info->status_what, d_len );
    	if( res <= 0)
	{ /* close command socket */
		veejay_msg(VEEJAY_MSG_DEBUG ,"Error sending status message , closing connection(s)");
		_vj_server_del_client(info->vjs[VEEJAY_PORT_CMD], link_id );
		_vj_server_del_client(info->vjs[VEEJAY_PORT_STA], link_id );
	}

    if (info->uc->chain_changed == 1)
		info->uc->chain_changed = 0;
    if (info->uc->render_changed == 1)
		info->uc->render_changed = 0;
}

void	veejay_check_homedir(void *arg)
{
	veejay_t *info = (veejay_t *) arg;
	char path[1024];
	char tmp[1024];
	struct stat s;
	char *home = getenv("HOME");
	if(!home)
	{
		veejay_msg(VEEJAY_MSG_ERROR,
				"HOME environment variable not set.");
		return;
	}

	snprintf(path,1024, "%s/.veejay", home);
	int ret = stat( path, &s );
	int error = 0;
	if( ret < 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "No veejay config file in %s", path );
		ret = mkdir(path,0777);
		if( ret != 0 )
		{
			error = 1;	
		}
		else
		{
			snprintf(path,1024, "%s/.veejay/recovery", home );
			mkdir(path,0777);
			snprintf(path,1024, "%s/.veejay/theme", home );
			mkdir(path,0777);
		}
		if(error)
		switch(errno)
		{
			case 0:
				break;
			case EACCES:
				veejay_msg( VEEJAY_MSG_ERROR, "\tPermission denied");break;
			case EEXIST:
				veejay_msg( VEEJAY_MSG_ERROR, "\tPath already exists and may not be a directory");break;
			case ELOOP:
				veejay_msg( VEEJAY_MSG_ERROR, "\tToo many symbolic links"); break;
			case ENOSPC:
				veejay_msg( VEEJAY_MSG_ERROR, "\tOut of available diskpace. Delete some files and try again"); break;
			default:
				veejay_msg( VEEJAY_MSG_ERROR, "\tSome other error occured");
				break;
		}
	}

	stat( path, &s );
	if(!error)
	{
		if( !S_ISDIR( s.st_mode )) 
		{
			veejay_msg(VEEJAY_MSG_ERROR, "%s is not a directory");
			error = 1;
		}
	}
	
	if(!error)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Veejay's configuration file is %s", path);
		info->homedir = strndup( path, 1024 );
	}

	sprintf(tmp, "%s/plugins.cfg", path );
	struct statfs ts;
	if( statfs( tmp, &ts ) != 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO,"\tNo plugins.cfg found (see DOC/HowtoPlugins)");
	}
	sprintf(tmp, "%s/viewport.cfg", path);
	memset( &ts,0,sizeof(struct statfs));
	if( statfs( tmp, &ts ) != 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO,"\tNo viewport.cfg found (press CTRL-V to setup viewport)");
	}

}

/******************************************************
 * veejay_mjpeg_playback_thread()
 *   the main (software) video playback thread
 *
 * return value: 1 on success, 0 on error
 ******************************************************/

void veejay_handle_signal(void *arg, int sig)
{
	veejay_t *info = (veejay_t *) arg;
	if (sig == SIGINT || sig == SIGQUIT )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Veejay interrupted by user. Bye!");
		veejay_change_state(info, LAVPLAY_STATE_STOP);
	}
	else 
	{
		if( sig == SIGPIPE || sig == SIGSEGV || sig == SIGBUS || sig == SIGPWR || sig == SIGABRT || sig == SIGFPE )
		{
			if(info->homedir)
				veejay_change_state_save(info,LAVPLAY_STATE_STOP);
			else
				veejay_change_state( info, LAVPLAY_STATE_STOP );
			
			signal( sig, SIG_DFL );
		}
	}
}


static void veejay_handle_callbacks(veejay_t *info) {

	/* check for OSC events */
	vj_osc_get_packet(info->osc);

	/*  update network */
	vj_event_update_remote( (void*)info );
}


void vj_lock(veejay_t *info)
{
	video_playback_setup *settings = info->settings;
	pthread_mutex_lock(&(settings->valid_mutex));
}
void vj_unlock(veejay_t *info)
{
	video_playback_setup *settings = info->settings;
	pthread_mutex_unlock(&(settings->valid_mutex));
}	 

static void donothing2(int sig)
{
	veejay_msg(VEEJAY_MSG_WARNING,"Catched signal %x (ignored)",sig );
}

static	void	veejay_event_handle(veejay_t *info)
{
	veejay_handle_callbacks(info);
#ifdef HAVE_SDL
	if( info->video_out == 0 )
	{
		SDL_Event event;
		int ctrl_pressed = 0;
		int shift_pressed = 0;
		int alt_pressed = 0;
		int mouse_x=0,mouse_y=0,but=0;
		int res = 0;
		while(SDL_PollEvent(&event) == 1) 
		{
			SDL_KeyboardEvent *k = &event.key;
			int mod = SDL_GetModState();
			if( event.type == SDL_KEYDOWN)
			{
				res = vj_event_single_fire( (void*) info, event, 0);
			}
			if( event.type == SDL_MOUSEMOTION )
			{
				mouse_x = event.button.x;
				mouse_y = event.button.y;
			}

			if( event.type == SDL_MOUSEBUTTONDOWN )
			{
				mouse_x = event.button.x;
				mouse_y = event.button.y;
				shift_pressed = (mod & KMOD_LSHIFT );
				alt_pressed = (mod & KMOD_RSHIFT );
				if( mod == 0x1080 || mod == 0x1040 || (mod & KMOD_LCTRL) || (mod & KMOD_RCTRL) )
					ctrl_pressed = 1; 
				else
					ctrl_pressed = 0;

				SDL_MouseButtonEvent *mev = &(event.button);

				if( mev->button == SDL_BUTTON_LEFT && shift_pressed)
				{
					but = 6;
					info->uc->mouse[3] = 1;
				} else if( mev->button == SDL_BUTTON_LEFT && ctrl_pressed )
				{
					but = 10;
					info->uc->mouse[3] = 4;
				}
				if (mev->button == SDL_BUTTON_MIDDLE && shift_pressed )
				{
					but = 7;
					info->uc->mouse[3] = 2;
				}
				if( mev->button == SDL_BUTTON_LEFT && alt_pressed )
				{
					but = 11;
					info->uc->mouse[3] = 11;
				}
			}

			if( event.type == SDL_MOUSEBUTTONUP )
			{
				SDL_MouseButtonEvent *mev = &(event.button);
				alt_pressed = (mod & KMOD_RSHIFT );
				shift_pressed = (mod & KMOD_LSHIFT );
				if( mod == 0x1080 || mod == 0x1040 || (mod & KMOD_LCTRL) || (mod & KMOD_RCTRL) )
					ctrl_pressed = 1; 
				else
					ctrl_pressed = 0;

				if( mev->button == SDL_BUTTON_LEFT )
				{
					if( info->uc->mouse[3] == 1 )
					{
						but = 6;
						info->uc->mouse[3] = 0;
					}
					else if (info->uc->mouse[3] == 4 )
					{	
						but = 10;
						info->uc->mouse[3] = 0;
					} else if (info->uc->mouse[3] == 0 )
					{
						but = 1;
					} else if ( info->uc->mouse[3] == 11 )
					{	
						but = 12;
						info->uc->mouse[3] = 0;
					}
				}
				else if (mev->button == SDL_BUTTON_RIGHT ) {
					but = 2;
				}
				else if (mev->button == SDL_BUTTON_MIDDLE ) {
					if( info->uc->mouse[3] == 2 )
					{	but = 0;
						info->uc->mouse[3] = 0;
					}
					else {if( info->uc->mouse[3] == 0 )
						but = 3;}
				}
				else if ((mev->button == SDL_BUTTON_WHEELUP ) && !alt_pressed && !ctrl_pressed)
				{
					but = 4;
				}
				else if ((mev->button == SDL_BUTTON_WHEELDOWN ) && !alt_pressed && !ctrl_pressed)
				{
					but = 5;
				}
				else if ((mev->button == SDL_BUTTON_WHEELUP) && alt_pressed && !ctrl_pressed )
				{
					but = 13;
				}
				else if ((mev->button == SDL_BUTTON_WHEELDOWN) && alt_pressed && !ctrl_pressed )
				{
					but = 14;
				}
				else if (mev->button == SDL_BUTTON_WHEELUP && ctrl_pressed )  
				{	
					but = 15;
				}	
				else if (mev->button == SDL_BUTTON_WHEELDOWN && ctrl_pressed )
				{
					but = 16;
				}
				mouse_x = event.button.x;
				mouse_y = event.button.y;
			}
		
		}
		info->uc->mouse[0] = mouse_x;
		info->uc->mouse[1] = mouse_y;
		info->uc->mouse[2] = but;
	}
#endif
#ifdef USE_GL
	if(info->video_out == 4 )
	{
		x_display_mouse_grab( info->gl, info->uc->mouse[0],info->uc->mouse[1],info->uc->mouse[2],
					info->uc->mouse[3] );

		x_display_event( info->gl, info->current_edit_list->video_width, info->current_edit_list->video_height );

		x_display_mouse_update( info->gl, &(info->uc->mouse[0]), &(info->uc->mouse[1]), &(info->uc->mouse[2]),
						&(info->uc->mouse[3]));
	}
#endif

}


static void *veejay_mjpeg_playback_thread(void *arg)
{
    veejay_t *info = (veejay_t *) arg;
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
   /* Allow easy shutting down by other processes... */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);


  /* schedule FIFO */

    veejay_schedule_fifo( info, getpid());

    vj_get_relative_time();

    vj_osc_set_veejay_t(info); 
    vj_tag_set_veejay_t(info);

//#ifdef HAVE_SDL
//	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
//#endif
    while (settings->state != LAVPLAY_STATE_STOP) {
	pthread_mutex_lock(&(settings->valid_mutex));
	while (settings->valid[settings->currently_processed_frame] == 0) {
	    pthread_cond_wait(&
			      (settings->
			       buffer_filled[settings->
					     currently_processed_frame]),
			      &(settings->valid_mutex));
	    if (settings->state == LAVPLAY_STATE_STOP) {
		// Ok, we shall exit, that's the reason for the wakeup 
		veejay_msg(VEEJAY_MSG_DEBUG,
			    "Veejay was told to exit");
		pthread_exit(NULL);
	 	return NULL;
	    }
	}
	pthread_mutex_unlock(&(settings->valid_mutex));


        if( settings->currently_processed_entry != settings->buffer_entry[settings->currently_processed_frame] &&
		!veejay_screen_update(info)  )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Error playing frame %d", settings->current_frame_num);
	}
	
	//@ here callbacks fixme was

	settings->currently_processed_entry = 
		settings->buffer_entry[settings->currently_processed_frame];
	/* sync timestamp */

	veejay_mjpeg_software_frame_sync(info,
					  settings->valid[settings->
							  currently_processed_frame]);
	settings->syncinfo[settings->currently_processed_frame].frame =
	    settings->currently_processed_frame;




	pthread_mutex_lock(&(settings->valid_mutex));
	settings->valid[settings->currently_processed_frame] = 0;
	pthread_mutex_unlock(&(settings->valid_mutex));

	/* Broadcast & wake up the waiting processes */
	pthread_cond_broadcast(&
			       (settings->
				buffer_done[settings->
					    currently_processed_frame]));

	/* Now update the internal variables */
 	// settings->previous_frame_num = settings->current_frame_num;

	settings->currently_processed_frame =
	    (settings->currently_processed_frame + 1) % 1;
    }

    veejay_msg(VEEJAY_MSG_DEBUG, 
		"Veejay was told to exit");
    return NULL;
}


char	*veejay_title( )
{
	char tmp[64];
	sprintf(tmp, "Veejay %s", VERSION );
	return strdup(tmp);
}


/******************************************************
 * veejay_mjpeg_open()
 *   hardware: opens the device and allocates buffers
 *   software: inits threads and allocates buffers
 *
 * return value: 1 on success, 0 on error
 ******************************************************/

int veejay_open(veejay_t * info)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    veejay_msg(VEEJAY_MSG_DEBUG, 
		"Initializing the threading system");

    memset( &(settings->lastframe_completion), 0, sizeof(struct timeval));

    pthread_mutex_init(&(settings->valid_mutex), NULL);
    pthread_mutex_init(&(settings->syncinfo_mutex), NULL);
    /* Invalidate all buffers, and initialize the conditions */

	settings->valid[0] = 0;
	settings->buffer_entry[0] = 0;
	pthread_cond_init(&(settings->buffer_filled[0]), NULL);
	pthread_cond_init(&(settings->buffer_done[0]), NULL);

	veejay_memset( &(settings->syncinfo[0]), 0, sizeof(struct mjpeg_sync));

    /* Now do the thread magic */
    settings->currently_processed_frame = 0;
    settings->currently_processed_entry = -1;

      veejay_msg(VEEJAY_MSG_DEBUG,"Starting software playback thread"); 


     if( pthread_create(&(settings->software_playback_thread), NULL,
		       veejay_mjpeg_playback_thread, (void *) info)) {
	veejay_msg(VEEJAY_MSG_ERROR, 
		    "Could not create software playback thread");
	return 0;


    }

    //settings->usec_per_frame = 0;

    return 1;
}


static int veejay_mjpeg_get_params(veejay_t * info,
				    struct mjpeg_params *bp)
{
    int i;
    /* Set some necessary params */
    bp->decimation = 1;
    bp->quality = 50;		/* default compression factor 8 */
    bp->odd_even = 1;
    bp->APPn = 0;
    bp->APP_len = 0;		/* No APPn marker */
    for (i = 0; i < 60; i++)
	bp->APP_data[i] = 0;
    bp->COM_len = 0;		/* No COM marker */
    for (i = 0; i < 60; i++)
	bp->COM_data[i] = 0;
    bp->VFIFO_FB = 1;
    veejay_memset( bp->reserved, 0, sizeof(bp->reserved));

    return 1;
}


/******************************************************
 * veejay_mjpeg_set_params()
 *   set the parameters
 *
 * return value: 1 on success, 0 on error
 ******************************************************/

/******************************************************
 * veejay_mjpeg_set_frame_rate()
 *   set the frame rate
 *
 * return value: 1 on success, 0 on error
 ******************************************************/


static int veejay_mjpeg_set_playback_rate(veejay_t * info,
					   double video_fps, int norm)
{
    int norm_usec_per_frame = 0;
    int target_usec_per_frame;
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    switch (norm) {
    case VIDEO_MODE_PAL:
    case VIDEO_MODE_SECAM:
	norm_usec_per_frame = 1000000 / 25;	/* 25Hz */
	break;
    case VIDEO_MODE_NTSC:
	norm_usec_per_frame = 1001000 / 30;	/* 30ish Hz */
	break;
    default:
	    veejay_msg(VEEJAY_MSG_ERROR, 
			"Unknown video norm! Use PAL , SECAM or NTSC");
	    return 0;
	}

    if (video_fps != 0.0)
	target_usec_per_frame = (int) (1000000.0 / video_fps);
    else
	target_usec_per_frame = norm_usec_per_frame;


    settings->usec_per_frame = target_usec_per_frame;
    return 1;
}


/******************************************************
 * veejay_mjpeg_queue_buf()
 *   queue a buffer
 *
 * return value: 1 on success, 0 on error
 ******************************************************/

static int veejay_mjpeg_queue_buf(veejay_t * info,   int frame_periods)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    /* mark this buffer as playable and tell the software playback thread to wake up if it sleeps */
    pthread_mutex_lock(&(settings->valid_mutex));
    settings->valid[0] = frame_periods;
    pthread_cond_broadcast(&(settings->buffer_filled[0]));
    pthread_mutex_unlock(&(settings->valid_mutex));
    return 1;
}


/******************************************************
 * veejay_mjpeg_sync_buf()
 *   sync on a buffer
 *
 * return value: 1 on success, 0 on error
 ******************************************************/

static int veejay_mjpeg_sync_buf(veejay_t * info, struct mjpeg_sync *bs)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    /* Wait until this buffer has been played */
	
    pthread_mutex_lock(&(settings->valid_mutex));
    while (settings->valid[settings->currently_synced_frame] != 0) {
	pthread_cond_wait(&
			  (settings->
			   buffer_done[settings->currently_synced_frame]),
			  &(settings->valid_mutex));
    }
    pthread_mutex_unlock(&(settings->valid_mutex));
	
    memcpy(bs, &(settings->syncinfo[settings->currently_synced_frame]),
	   sizeof(struct mjpeg_sync));
    settings->currently_synced_frame =
	(settings->currently_synced_frame + 1) % 1;
	
    return 1;
}


/******************************************************
 * veejay_mjpeg_close()
 *   close down
 *
 * return value: 1 on success, 0 on error
 ******************************************************/

 int veejay_close(veejay_t * info)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    veejay_msg(VEEJAY_MSG_DEBUG, 
		"Closing down the threading system ");

    //pthread_cancel(settings->software_playback_thread);
    if (pthread_join(settings->software_playback_thread, NULL)) {
	veejay_msg(VEEJAY_MSG_ERROR, 
		    "Failure deleting software playback thread");
	return 0;
    }

    return 1;
}



/******************************************************
 * veejay_init()
 *   check the given settings and initialize everything
 *
 * return value: 0 on success, -1 on error
 ******************************************************/


int veejay_init(veejay_t * info, int x, int y,char *arg, int def_tags, int full_range)
{
	editlist *el = info->current_edit_list;
	video_playback_setup *settings = info->settings;

	available_diskspace();

	if(info->video_out<0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No video output driver selected (see man veejay)");
		return -1;
    	}
    	// override geometry set in config file   
	if( info->uc->geox != 0 && info->uc->geoy != 0 )
	{
		x = info->uc->geox;
		y = info->uc->geoy;
	}

	vj_event_init();

	int id = 0, mode = 0;

	switch (info->uc->use_timer)
	{
		case 0:
			veejay_msg(VEEJAY_MSG_WARNING, "Not timing audio/video");
		break;
    		default:
			veejay_msg(VEEJAY_MSG_INFO, "Using nanosleep timer");
		break;
    	}    

 	if (veejay_init_editlist(info) != 0) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
			    "Cannot initialize the EditList");
		return -1;
	}
	vj_tag_set_veejay_t(info);

	if (vj_tag_init(info->current_edit_list->video_width, info->current_edit_list->video_height, info->pixel_format) != 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing Stream Manager");
		return -1;
    	}
	
#ifdef HAVE_FREETYPE
	info->font = vj_font_init( info->current_edit_list->video_width,
				   info->current_edit_list->video_height,
				   info->current_edit_list->video_fps,0 );


	if(info->settings->composite)
	{
		info->osd = vj_font_init( info->video_output_width,info->video_output_height,
					  info->current_edit_list->video_fps ,1  );
	}
	else
	{	
		info->osd = vj_font_init( info->current_edit_list->video_width,
				   info->current_edit_list->video_height,
				   info->current_edit_list->video_fps,1 );
	}
	
#endif


 	sample_init( (info->current_edit_list->video_width * info->current_edit_list->video_height) ,
		info->font ); 

	sample_set_project( info->pixel_format,
			    info->auto_deinterlace,
			    info->preserve_pathnames,
				0,
			    info->edit_list->video_norm );

	if(info->pixel_format == FMT_422  || info->pixel_format == FMT_422F)
	{
		if(!vj_el_init_422_frame( info->current_edit_list, info->effect_frame1)) return 0;
		if(!vj_el_init_422_frame( info->current_edit_list, info->effect_frame2)) return 0;
		info->settings->sample_mode = SSM_422_444;
		veejay_msg(VEEJAY_MSG_INFO, "Internal YUV format is 4:2:2 Planar, %d x %d",
				info->current_edit_list->video_width,
				info->current_edit_list->video_height);
	}
	else 
	{
		if(!vj_el_init_420_frame( info->current_edit_list, info->effect_frame1)) return 0;
		if(!vj_el_init_420_frame( info->current_edit_list, info->effect_frame2)) return 0;
		info->settings->sample_mode = SSM_420_JPEG_TR;
		veejay_msg(VEEJAY_MSG_INFO, "Internal YUV format is 4:2:0 Planar, %d x %d",
				info->current_edit_list->video_width,
				info->current_edit_list->video_height);
	}
	
	if(!vj_perform_init(info))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize Veejay Performer");
		return -1;
    	}

	if( info->settings->crop && info->settings->zoom)
	{
		VJFrame src;
		memset( &src,0,sizeof(VJFrame));
		int w = 0; int h = 0;
		vj_get_yuv_template( &src,
			info->current_edit_list->video_width,
			info->current_edit_list->video_height,
			info->pixel_format );

		int res = vj_perform_init_cropped_output_frame(
				info,
				&src,
				&w,
				&h
				);
		if( res == 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR ,"Invalid crop parameters: %d:%d:%d:%d (%dx%d)",
				info->settings->viewport.top,
				info->settings->viewport.bottom,
				info->settings->viewport.left,
				info->settings->viewport.right,w,h);
			return -1; 
		}
		veejay_msg(VEEJAY_MSG_INFO, "Crop video %dx%d to %dx%d (top %d, bottom %d, left %d, right %d",
				info->current_edit_list->video_width, info->current_edit_list->video_height,
				w,h,
				info->settings->viewport.top,
				info->settings->viewport.bottom,
				info->settings->viewport.left,
				info->settings->viewport.right );
		
	}
	if( info->settings->zoom )
	{
		VJFrame src;
		VJFrame dst;
		memset( &src, 0, sizeof(VJFrame));
		memset( &dst, 0, sizeof(VJFrame));

		if(info->settings->crop)
		{
			int w = 0;
			int h = 0;
			vj_perform_get_crop_dimensions( info, &w, &h );
			vj_get_yuv_template( &src, w,h, info->pixel_format );
		}	
		else
		{
			vj_get_yuv_template( &src,
					info->current_edit_list->video_width,
					info->current_edit_list->video_height,
					info->pixel_format );
		}

		vj_get_yuv_template( &dst,
					info->video_output_width,
					info->video_output_height,
					info->pixel_format );

		if(info->settings->crop)
			vj_perform_get_cropped_frame(info, &src.data, 0);
		else
			vj_perform_get_primary_frame(info, &src.data  );
	
		vj_perform_init_output_frame(info, &(dst.data),
			info->video_output_width, info->video_output_height );

		info->settings->sws_templ.flags  = info->settings->zoom;
	        info->video_out_scaler = (void*)
			yuv_init_swscaler(
				&src,
				&dst,
				&(info->settings->sws_templ),
				yuv_sws_get_cpu_flags()
			);

		if(!info->video_out_scaler)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize SwScaler");
			return -1;
		}


		if(info->settings->sws_templ.use_filter)
		{
			sws_template *t = &(info->settings->sws_templ);
			veejay_msg(VEEJAY_MSG_DEBUG, "Using software scaler options:");
			veejay_msg(VEEJAY_MSG_DEBUG, "lgb=%f, cgb=%f, ls=%f, cs=%f, chs=%d, cvs=%d",
				t->lumaGBlur,t->chromaGBlur,t->lumaSarpen,
				t->chromaSharpen,t->chromaHShift,t->chromaVShift);
		}
	}

	if( info->settings->composite )
	{
		if(info->settings->zoom <= 0 || info->settings->zoom > 11 )
			info->settings->zoom = 1;
		info->composite = composite_init( info->video_output_width, info->video_output_height,
			el->video_width, el->video_height, info->homedir, info->settings->sample_mode,
				info->settings->zoom, info->pixel_format );
		info->settings->zoom = 0;
		info->which_vp = 1;
	//	info->bes_width  = info->video_output_width;
	//	info->bes_height = info->video_output_height;
	}
	else if(!info->settings->zoom)
	{
	    info->video_output_width = el->video_width;
	    info->video_output_height = el->video_height;
	    info->settings->sws_templ.flags  = 1; // fast bicubic
	}

	if(!info->bes_width)
		info->bes_width = info->video_output_width;
	if(!info->bes_height)
		info->bes_height = info->video_output_height;	
	
	if(info->current_edit_list->has_audio)
	{
		if (vj_perform_init_audio(info))
			veejay_msg(VEEJAY_MSG_INFO, "Initialized Audio Task");
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, 
				"Unable to initialize Audio Task");
			info->audio = NO_AUDIO;
		}
	}

  	veejay_msg(VEEJAY_MSG_INFO, 
		"Initialized %d Image- and Video Effects", vj_effect_max_effects());
    	vj_effect_initialize(info->current_edit_list->video_width, info->current_edit_list->video_height,
						full_range);
	veejay_msg(VEEJAY_MSG_INFO,
		"BES %d x %d, Video %d x %d , Screen %d x %d",
		info->bes_width,
		info->bes_height,	
		el->video_width,
		el->video_height,
		info->video_output_width,
		info->video_output_height);
   
    if(info->dump) vj_effect_dump(); 	
    info->output_stream = vj_yuv4mpeg_alloc(info->current_edit_list, info->video_output_width,
		info->video_output_height );
	if(!info->output_stream)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot setup output stream?");
		return -1;
	}

	if( info->settings->action_scheduler.sl && info->settings->action_scheduler.state )
	{
	
		if(sample_readFromFile( info->settings->action_scheduler.sl,
				info->seq, info->font, info->edit_list, &(info->uc->sample_id), &(info->uc->playback_mode) ) )
			veejay_msg(VEEJAY_MSG_INFO, "Loaded sample list %s from actionfile",
					info->settings->action_scheduler.sl );
	}
	
   
	if( settings->action_scheduler.state )
	{
		settings->action_scheduler.state = 0; 
	}

	if( !vj_server_setup(info) )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to start network server. Most likely, there is already a veejay running");
		veejay_msg(VEEJAY_MSG_ERROR,"If you want to run multiple veejay's on the same machine, use the '-p/--port'");
		veejay_msg(VEEJAY_MSG_ERROR,"commandline option. For example: $ veejay -p 4490 -d");
		return -1;
	}

    	/* now setup the output driver */
    	switch (info->video_out)
	 {
 		case 4:
#ifdef USE_GL
			veejay_msg(VEEJAY_MSG_INFO, "Using output driver OpenGL");
			info->gl = (void*) x_display_init(info);
			x_display_open(info->gl, info->current_edit_list->video_width, info->current_edit_list->video_height );
#endif
			break;
		case 0:
			veejay_msg(VEEJAY_MSG_INFO, "Using output driver SDL");
#ifdef HAVE_SDL
			info->sdl[0] =
			    (vj_sdl *) vj_sdl_allocate( info->video_output_width,info->video_output_height,info->pixel_format);
			if( !info->sdl[0] )
				return -1;

			if( x != -1 && y != -1 )
				vj_sdl_set_geometry(info->sdl[0],x,y);

			if (!vj_sdl_init(info->settings->ncpu, info->sdl[0], info->bes_width, info->bes_height, "Veejay",1,info->settings->full_screen))
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Error initializing SDL");
		    		return -1;
			}
#endif
			break;
		case 1:

			veejay_msg(VEEJAY_MSG_INFO, "Using output driver DirectFB");
#ifdef HAVE_DIRECTFB			
			info->dfb =(vj_dfb *) vj_dfb_allocate(info->video_output_width,info->video_output_height,
					       	el->video_norm);
			if( !info->dfb )
				return -1;
			if (vj_dfb_init(info->dfb) != 0)
	    			return -1;
#endif
		break;


		case 2:
			veejay_msg(VEEJAY_MSG_INFO, 
			    "Using output driver SDL & DirectFB");
#ifdef HAVE_SDL
			info->sdl[0] = 	(vj_sdl *) vj_sdl_allocate(info->video_output_width,info->video_output_height, info->pixel_format);
			if(!info->sdl[0])		
				return -1;
	
			if (!vj_sdl_init(info->settings->ncpu, info->sdl[0], info->bes_width, info->bes_height,"Veejay",1,info->settings->full_screen))
	   	 		return -1;
#endif
#ifdef HAVE_DIRECTFB
			info->dfb =  (vj_dfb *) vj_dfb_allocate( info->video_output_width, info->video_output_height, el->video_norm);
			if(!info->dfb)
				return -1;

			if (vj_dfb_init(info->dfb) != 0)
			    return -1;
#endif
		break;
	    case 3:
			veejay_msg(VEEJAY_MSG_INFO, "Entering render mode (no visual output)");
        
			info->render_stream = vj_yuv4mpeg_alloc(info->current_edit_list, info->video_output_width,info->video_output_height);

			if (vj_yuv_stream_start_write (info->render_stream, info->stream_outname, info->current_edit_list) == 0) {
	   	 		veejay_msg(VEEJAY_MSG_INFO, "Rendering to [%s]",info->stream_outname);
 			}
	 		else {
	   			veejay_msg(VEEJAY_MSG_ERROR, "Cannot create render stream. Aborting");
	    			return -1;
			}
		break;
	
		case 5:
			veejay_msg(VEEJAY_MSG_INFO, "Entering headless mode (no visual output)");
		break;

	default:
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode. Use -O [012345]");
		return -1;
	break;
    }


#ifdef HAVE_XML2
	if(info->load_action_file)
	{
		if(veejay_load_action_file(info, info->action_file )==0)
		{
			if(sample_readFromFile( info->action_file,
				info->seq, 
				info->font, 
				info->edit_list,
				 &id,
				 &mode ))
			{
				veejay_msg(VEEJAY_MSG_INFO, "Loaded samplelist %s", info->action_file);
				if( id > 0 )
				{
					veejay_change_playback_mode(info, mode, id );
				}
			}
		}
		else
		{
			veejay_msg(VEEJAY_MSG_INFO, "Loaded configuration file %s", info->action_file );
		}
	}
#endif



	if(def_tags && id <= 0)
	{
		int n = vj_tag_num_devices();
		veejay_msg(0, "create_tag: nstreams=%d,pf=%d, %d",info->nstreams,1, (def_tags-1));
		int nid =	veejay_create_tag( info, VJ_TAG_TYPE_V4L, "bogus", info->nstreams, el->pixel_format, (def_tags-1) );
		if( nid> 0)
		{
		 	   veejay_msg(VEEJAY_MSG_INFO, "Requested capture device available as stream %d", nid );
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to open capture device, Use -A[%d-%d]",
				1, n );
			return -1;
		}
		info->settings->late[0] = VJ_PLAYBACK_MODE_TAG;
		info->settings->late[1] = nid;
	//	veejay_change_playback_mode(info,VJ_PLAYBACK_MODE_TAG,nid);
	}
	else if(info->dummy->active && id <= 0)
	{
	 	int dummy_id;
		/* Use dummy mode, action file could have specified something */
		if( vj_tag_size()-1 <= 0 )
			dummy_id = vj_tag_new( VJ_TAG_TYPE_COLOR, "Solid", -1, info->current_edit_list,info->pixel_format,-1,0);
		else
			dummy_id = vj_tag_size()-1;

		if(dummy_id > 0)
		{
			veejay_msg(VEEJAY_MSG_INFO, "Activating dummy mode (Stream %d)", dummy_id);
			veejay_change_playback_mode(info,VJ_PLAYBACK_MODE_TAG,dummy_id);
		}
		else
		{
			veejay_msg(VEEJAY_MSG_INFO, "Failed to create dummy stream");
			return -1;
		}
	}
	else if( info->uc->file_as_sample && id <= 0)
	{
		long i,n=info->current_edit_list->num_video_files;
		for(i = 0; i < n; i ++ )
		{
			long start,end;
			if(vj_el_get_file_entry( info->current_edit_list, &start,&end, i ))
			{
				editlist *el = veejay_edit_copy_to_new(
					info,info->current_edit_list,
					start,end );
				if(!el)
				{
					veejay_msg(0, "Unable to start from file, Abort");
					return -1;
				}
				sample_info *skel = sample_skeleton_new( 0, el->video_frames-1 );
				if(skel)
				{
					skel->edit_list = el;
					sample_store(skel);
				}
			}	
		}
	}

	/* After we have fired up the audio and video threads system (which
     	* are assisted if we're installed setuid root, we want to set the
     	* effective user id to the real user id
    	 */

	if (seteuid(getuid()) < 0)
	{
		/* fixme: get rid of sys_errlist and use sys_strerror */
		veejay_msg(VEEJAY_MSG_ERROR, "Can't set effective user-id: %s", sys_errlist[errno]);
		return -1;
    	}


    	veejay_change_state( info, LAVPLAY_STATE_PLAYING );  

    	if (!veejay_mjpeg_set_playback_rate(info, el->video_fps, el->video_norm == 'p' ? VIDEO_MODE_PAL : VIDEO_MODE_NTSC)) {
		return -1;
    	}

   	if(veejay_open(info) != 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize the threading system");
		return -1;   
      	}
    	return 0;
}

static	int	sched_ncpus() {
	return sysconf( _SC_NPROCESSORS_ONLN );
}

static	void	veejay_schedule_fifo(veejay_t *info, int pid )
{
	struct sched_param schp;
	veejay_memset( &schp, 0, sizeof(schp));
	schp.sched_priority = sched_get_priority_max( SCHED_FIFO );

	if( sched_setscheduler( pid, SCHED_FIFO, &schp ) != 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Cannot set First-In-First-Out scheduling for process %d",pid);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Using First-In-First-Out II scheduling for process %d", pid);
		veejay_msg(VEEJAY_MSG_INFO, "\tPriority is set to %d (RT)", schp.sched_priority );
	}
}

/*
 * pin_cpu: snapped from lm-sensors project
 *
 *
 */

static int	veejay_pin_cpu( veejay_t *info, int cpu_num )
{
	uint32_t st = sizeof(cpu_set_t);
	
	int i,j,retval;
	if( info->cpumask == NULL )
	{
		info->sz = 1 + (2 * sched_ncpus()) / (8 * sizeof(unsigned long));
		info->mask = (unsigned long*) vj_calloc( st );
		info->cpumask = (unsigned long*) vj_calloc( st);
		retval = sched_getaffinity(0, st, info->cpumask );
		if( retval < 0 )
		{
			veejay_msg(0,"sched_getaffinity()");
			return retval;
		}

		for( i = 0; i < st; ++ i )
		{
			int word = i / (8 * sizeof(unsigned long));
			int bit  = i % (8 * sizeof(unsigned long));
			if( info->cpumask[word] & (1 << bit))
				info->ncpus++;
		}
	}
	

	cpu_num %= info->ncpus;

	veejay_memset( info->mask, 0, st );

	for ( i = 0, j = 0; i < st; ++ i )
	{
		int word = i / (8 * sizeof(unsigned long));
		int bit  = i % (8 * sizeof(unsigned long));
		if( info->cpumask[word] & (1 << bit ))
		{
			if( j >= cpu_num ) {
				info->mask[word] |= ( 1 << bit );
				break;
			}
			j++;
		}
	}

	int pi = (int) getpid();

	retval = sched_setaffinity( pi, st, info->mask );

	return retval;
}

/******************************************************
 * veejay_playback_cycle()
 *   the playback cycle
 ******************************************************/
static void veejay_playback_cycle(veejay_t * info)
{
    video_playback_stats stats;
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    editlist *el = info->edit_list;
    struct mjpeg_sync bs;
    
    struct timeval time_now;
    double tdiff1, tdiff2;
    int first_free, skipv, skipa, skipi, nvcorr;
    struct mjpeg_params bp;
    long ts, te;

    int   which_cpu = 1;
    char *env_value = getenv( "VEEJAY_SET_CPU" );
    if( env_value )
    {
	if( sscanf( env_value, "%d", &which_cpu ))
	{
		if( which_cpu < 0 || which_cpu > settings->ncpu )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "VEEJAY_SET_CPU valid values are [%d ... %d]", 0, settings->ncpu );
			which_cpu = 1;
		}
		if(!which_cpu)
			veejay_msg(VEEJAY_MSG_INFO, "VEEJAY_SET_CPU set to 0 , render thread not locked on a single CPU.");
		else
			veejay_msg(VEEJAY_MSG_INFO, "VEEJAY_SET_CPU set to %d", which_cpu );
	}
    }

    if( settings->ncpu > 1 && which_cpu)
    {
	veejay_msg(VEEJAY_MSG_INFO, "Running on multiprocessor. Locking CPU %d for rendering purposes", which_cpu);
	veejay_pin_cpu( info, which_cpu );
    }

    if( info->settings->late[1] )
   	 veejay_change_playback_mode(info,info->settings->late[0],info->settings->late[1]);

    vj_perform_queue_audio_frame(info);
    vj_perform_queue_video_frame(info,0);
 
    if (vj_perform_queue_frame(info, 0) != 0)
    {
	   veejay_msg(VEEJAY_MSG_ERROR,"Unable to queue frame");
           return;
    }


    bp.input = 0;
    bp.norm =
	(el->video_norm == 'n') ? VIDEO_MODE_NTSC : VIDEO_MODE_PAL;

    veejay_msg(VEEJAY_MSG_DEBUG, "Output norm: %s",
		bp.norm == VIDEO_MODE_NTSC ? "NTSC" : "PAL");

    bp.norm = el->video_norm == VIDEO_MODE_NTSC ? 480 : 576;

    veejay_msg(VEEJAY_MSG_DEBUG, 
		"Output dimensions: %ldx%ld",
		el->video_width, el->video_height);

    bp.odd_even = (el->video_inter == LAV_INTER_TOP_FIRST);

    if (!veejay_mjpeg_get_params(info, &bp)) {
	veejay_msg(VEEJAY_MSG_ERROR, "Uhm?");
	return ;
    }

    stats.stats_changed = 0;
    stats.num_corrs_a = 0;
    stats.num_corrs_b = 0;
    stats.nsync = 0;
    stats.audio = 0;
    stats.norm = el->video_norm == 'n' ? 1 : 0;
    tdiff1 = 0.;
    tdiff2 = 0.;
    nvcorr = 0;

    if(el->has_audio && info->audio == AUDIO_PLAY)
    {
	stats.audio = 1;
    }
    veejay_mjpeg_queue_buf(info, 1);
    
    while (settings->state != LAVPLAY_STATE_STOP) {
	first_free = stats.nsync;

	int current_speed = settings->current_playback_speed;

    	do {
	    if (settings->state == LAVPLAY_STATE_STOP) {
		goto FINISH;
		}
	    if (!veejay_mjpeg_sync_buf(info, &bs)) {
		veejay_change_state_save(info, LAVPLAY_STATE_STOP);
		goto FINISH;
	    }
	
	    stats.nsync++;
	    gettimeofday(&time_now, 0);
	    stats.tdiff = time_now.tv_sec - bs.timestamp.tv_sec +
		(time_now.tv_usec - bs.timestamp.tv_usec)*1.e-6;
	} 
	while (stats.tdiff > settings->spvf && (stats.nsync - first_free) < 0);

        veejay_event_handle(info);


#ifdef HAVE_JACK
	if ( info->audio==AUDIO_PLAY ) 
	{
	   struct timeval audio_tmstmp;	
	   long int sec=0;
	   long int usec=0;
	   long num_audio_bytes_written = vj_jack_get_status( &sec,&usec);
	   audio_tmstmp.tv_sec = sec;
	   audio_tmstmp.tv_usec = usec;

	   if (audio_tmstmp.tv_sec)
 	   {
         	    tdiff1 = settings->spvf * (stats.nsync - nvcorr) -  
				settings->spas * num_audio_bytes_written;
             	    tdiff2 = (bs.timestamp.tv_sec - audio_tmstmp.tv_sec) + (bs.timestamp.tv_usec - audio_tmstmp.tv_usec) * 1.e-6;
           }
	}
#endif
	stats.tdiff = tdiff1 - tdiff2;

	/* Fill and queue free buffers again */
	int n;
	for (n = first_free; n < stats.nsync;) {
	    /* Audio/Video sync correction */
	    skipv = 0;
	    skipa = 0;
	    skipi = 0;
 
	    if (info->sync_correction) {
		if (stats.tdiff > settings->spvf) {
		    /* Video is ahead audio */
		    skipa = 1;
		    if (info->sync_ins_frames && current_speed != 0)
			skipi = 1;
		    nvcorr++;
		    stats.num_corrs_a++;
		    stats.tdiff -= settings->spvf;
		    stats.stats_changed = 1;
		}
		if (stats.tdiff < -settings->spvf) {
		    /* Video is behind audio */
		    skipv = 1;
   		    if (!info->sync_skip_frames && current_speed != 0)
			skipi = 1;

 		    nvcorr--;
		    stats.num_corrs_b++;
		    stats.tdiff += settings->spvf;
		    stats.stats_changed = 1;
		}
	    }
#ifdef HAVE_SDL
	    ts= SDL_GetTicks();
#endif
	    settings->buffer_entry[0] ++;

	    if (!skipa) 
			vj_perform_queue_audio_frame(info);

	    if (!skipv)
			vj_perform_queue_video_frame(info,skipi);
  
	    vj_perform_queue_frame( info, skipi );

#ifdef HAVE_SDL	
	    te = SDL_GetTicks();
            info->real_fps = (int)( te - ts );
#else
	    info->real_fps = 0;
#endif
	    if(skipv ) continue;

	    if (!veejay_mjpeg_queue_buf(info, 1)) {
		veejay_msg(VEEJAY_MSG_ERROR ,"Error queuing a frame");
		veejay_change_state_save(info, LAVPLAY_STATE_STOP);
		goto FINISH;
	    }

	    n++;
	}
//            veejay_event_handle(info);


		/* output statistics */
	if (el->has_audio && (info->audio==AUDIO_PLAY))
	    stats.audio = settings->audio_mute ? 0 : 1;
	stats.stats_changed = 0;
    }

  FINISH:

    /* All buffers are queued, sync on the outstanding buffers
     * Never try to sync on the last buffer, it is a hostage of
     * the codec since it is played over and over again
     */
    if (info->audio_running || info->audio ==AUDIO_PLAY)
	vj_perform_audio_stop(info);
}

/******************************************************
 * veejay_playback_thread()
 *   The main playback thread
 ******************************************************/

static void Welcome(veejay_t *info)
{
	veejay_msg(VEEJAY_MSG_WARNING, "Video project settings: %ldx%ld, Norm: [%s], fps [%2.2f], %s",
			info->current_edit_list->video_width,
			info->current_edit_list->video_height,
			info->current_edit_list->video_norm == 'n' ? "NTSC" : "PAL",
			info->current_edit_list->video_fps, 
			info->current_edit_list->video_inter==0 ? "Not interlaced" : "Interlaced" );
	if(info->audio==AUDIO_PLAY && info->edit_list->has_audio)
	veejay_msg(VEEJAY_MSG_WARNING, "                        %ldHz %d Channels %dBps (%d Bit) %s %s",
			info->current_edit_list->audio_rate,
			info->current_edit_list->audio_chans,
			info->current_edit_list->audio_bps,
			info->current_edit_list->audio_bits,
			(info->no_bezerk==0?"[Bezerk]" : " " ),
			(info->verbose==0?" " : "[Debug]")  );
  
	if(info->settings->zoom)
	{
		veejay_msg(VEEJAY_MSG_INFO,"Software scaler - output stream dimensions %d x %d ",
			info->video_output_width, info->video_output_height );
	}
	else if(info->settings->composite )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Software composite - projection screen is %d x %d",
			info->video_output_width, info->video_output_height );
	}
	

	veejay_msg(VEEJAY_MSG_INFO,"Type 'man veejay' in a shell to learn more about veejay");
	veejay_msg(VEEJAY_MSG_INFO,"For a list of events, type 'veejay -u |less' in a shell");
	veejay_msg(VEEJAY_MSG_INFO,"Use 'gveejayreloaded' to enter interactive mode");
	veejay_msg(VEEJAY_MSG_INFO,"Alternatives are OSC applications or 'sendVIMS' extension for PD"); 

}

static void *veejay_playback_thread(void *data)
{
    veejay_t *info = (veejay_t *) data;
    int i;
   
	sigset_t mask;
	struct sigaction act;
	sigemptyset(&mask);
	sigaddset( &mask, SIGPIPE );
	act.sa_handler = donothing2;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);


    pthread_sigmask( SIG_BLOCK, &mask, NULL );
 

    Welcome(info);
   
    veejay_playback_cycle(info);

      veejay_close(info); 

    veejay_msg(VEEJAY_MSG_DEBUG,"Exiting playback thread");
    if(info->uc->is_server) {
	for(i = 0; i < 4; i ++ )
	  if(info->vjs[i]) vj_server_shutdown(info->vjs[i]); 
    }
    if(info->osc) vj_osc_free(info->osc);
      
#ifdef HAVE_SDL
    for ( i = 0; i < MAX_SDL_OUT ; i ++ )
		if( info->sdl[i] )
		{
#ifndef X_DISPLAY_MISSING
			 if(info->sdl[i]->display)
				 x11_enable_screensaver( info->sdl[i]->display);
#endif
			 vj_sdl_free(info->sdl[i]);
			 free(info->sdl[i]);
		}

	vj_sdl_quit();
#endif
#ifdef HAVE_DIRECTFB
    if( info->dfb )
	{
		vj_dfb_free(info->dfb);
		free(info->dfb);
	}
#endif
     vj_yuv4mpeg_free(info->output_stream); 
    free(info->output_stream);


    if( info->video_out == 3 )
	{
		vj_yuv_stream_stop_write(info->render_stream);
		veejay_msg(VEEJAY_MSG_DEBUG, "Stopped rendering to [%s]",
			    info->stream_outname);
	}
#ifdef USE_GL
#ifndef X_DISPLAY_MISSING
	if( info->video_out == 4 )
	{
		x11_enable_screensaver( x_get_display(info->gl) );
		x_display_close( info->gl );
    	}
#endif
#endif
#ifdef HAVE_FREETYPE
	vj_font_destroy( info->font );
	vj_font_destroy( info->osd );
#endif
    veejay_msg(VEEJAY_MSG_DEBUG,"Exiting playback thread");
    vj_perform_free(info);

    pthread_exit(NULL);

    return NULL;
}

/*
	port 3490 = command, 3491 = status
	port 3492 = OSC
	port 3493 = mcast frame sender (optional)
	port 3494 = mcast command receiver (optional)
 */

int vj_server_setup(veejay_t * info)
{
	if (info->uc->port == 0)
		info->uc->port = VJ_PORT;
	info->vjs[VEEJAY_PORT_CMD] = vj_server_alloc(info->uc->port, NULL, V_CMD);
	if(!info->vjs[VEEJAY_PORT_CMD])
		return 0;

	info->vjs[VEEJAY_PORT_STA] = vj_server_alloc(info->uc->port, NULL, V_STATUS);
	if(!info->vjs[VEEJAY_PORT_STA])
		return 0;

	//@ second VIMS control port
	info->vjs[VEEJAY_PORT_DAT] = vj_server_alloc(info->uc->port + 5, NULL, V_CMD );
	if(!info->vjs[VEEJAY_PORT_DAT])
		return 0;

	info->vjs[VEEJAY_PORT_MAT] = NULL;
	if( info->settings->use_vims_mcast )
	{
		info->vjs[VEEJAY_PORT_MAT] =
			vj_server_alloc(info->uc->port, info->settings->vims_group_name, V_CMD );
		if(!info->vjs[VEEJAY_PORT_MAT])
		{
			veejay_msg(VEEJAY_MSG_ERROR,
		  		 "Unable to initialize mcast sender");
			return 0;
		}
		else
			veejay_msg(VEEJAY_MSG_INFO, "UDP multicast frame sender socket ready at port %d group %s",
				info->uc->port, info->settings->vims_group_name );
	}
	if(info->settings->use_mcast)
	{
		GoMultiCast( info->settings->group_name );
	}
	info->osc = (void*) vj_osc_allocate(info->uc->port+4);

    	if(!info->osc) 
	{
		veejay_msg(VEEJAY_MSG_ERROR,
		  "Unable to start OSC server at port %d",
			info->uc->port + 4 );
		return 0;
	}

	if( info->settings->use_mcast )
		veejay_msg(VEEJAY_MSG_INFO, "UDP multicast OSC channel ready at port %d (group '%s')",
			info->uc->port + 2, info->settings->group_name );
	else
		veejay_msg(VEEJAY_MSG_INFO, "UDP unicast OSC channel ready at port %d",
			info->uc->port + 2 );

	if(vj_osc_setup_addr_space(info->osc) == 0)
		veejay_msg(VEEJAY_MSG_INFO, "Initialized OSC (http://www.cnmat.berkeley.edu/OpenSoundControl/)");

    	if (info->osc == NULL || info->vjs[VEEJAY_PORT_CMD] == NULL || info->vjs[VEEJAY_PORT_STA] == NULL) 
	{
		veejay_msg(0, "Unable to setup basic network I/O. Abort");
		return 0;
    	}
    	info->uc->is_server = 1;

	return 1;
}

/******************************************************
 * veejay_malloc()
 *   malloc the pointer and set default options
 *
 * return value: a pointer to an allocated veejay_t
 ******************************************************/
int	prepare_cache_line(int perc, int n_slots)
{
	int total = 0; 
	char line[128];
	FILE *file = fopen( "/proc/meminfo","r");
	if(!file)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant open proc, memory size cannot be determined");
		veejay_msg(VEEJAY_MSG_ERROR, "Cache disabled");
		return 1;
	}

	fgets(line, 128, file );
	sscanf( line, "%*s %i", &total );
	fclose(file);
/*	fgets( line,128, file );
	fgets( line,128, file );
	fclose( file );
	sscanf( line, "%*s %i %i %i %i", &total,&avail,&buffer,&cache );
*/
	double p = (double) perc * 0.01;
	int max_memory = (p * total);
	if( n_slots <= 0)
	 n_slots = 1;

	int chunk_size = (max_memory <= 0 ? 0: max_memory / n_slots ); 

	chunk_size_ = chunk_size;
	n_cache_slots_ = n_slots;

	total_mem_mb_ = total / 1024;
	if(chunk_size > 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO, "%d Kb total system RAM , Consuming up to %2.2f Mb",
				total, (float)max_memory / 1024.0 );
		veejay_msg(VEEJAY_MSG_INFO, "Cache line size is %d Mb per Sample",
				chunk_size);
		vj_el_init_chunk( chunk_size );
	}
	else
		veejay_msg(VEEJAY_MSG_INFO, "Memory cache disabled");

	return 1;
}

int smp_check()
{
	return get_nprocs();
}

veejay_t *veejay_malloc()
{
    
    veejay_t *info;
    int i;

      info = (veejay_t *) vj_calloc(sizeof(veejay_t));
    if (!info)
		return NULL;

    info->settings = (video_playback_setup *) vj_calloc(sizeof(video_playback_setup));
    if (!(info->settings)) 
		return NULL;
    veejay_memset( &(info->settings->action_scheduler), 0, sizeof(vj_schedule_t));
    veejay_memset( &(info->settings->viewport ), 0, sizeof(VJRectangle)); 

    info->status_what = (char*) vj_calloc(sizeof(char) * MESSAGE_SIZE );
    info->status_msg = (char*) vj_calloc(sizeof(char) * MESSAGE_SIZE+5);

	info->uc = (user_control *) vj_calloc(sizeof(user_control));
    if (!(info->uc)) 
		return NULL;

    info->effect_frame1 = (VJFrame*) vj_calloc(sizeof(VJFrame));
	if(!info->effect_frame1)
		return NULL;

    info->effect_frame2 = (VJFrame*) vj_calloc(sizeof(VJFrame));
	if(!info->effect_frame2)
		return NULL;

    info->effect_frame_info = (VJFrameInfo*) vj_calloc(sizeof(VJFrameInfo));
	if(!info->effect_frame_info)
		return NULL;

    info->effect_info = (vjp_kf*) vj_calloc(sizeof(vjp_kf));
	if(!info->effect_info) 
		return NULL;   

	info->dummy = (dummy_t*) vj_calloc(sizeof(dummy_t));
    if(!info->dummy)
		return NULL;
	memset( info->dummy, 0, sizeof(dummy_t));

	memset(&(info->settings->sws_templ), 0, sizeof(sws_template));

	info->seq = (sequencer_t*) vj_calloc(sizeof( sequencer_t) );
	if(!info->seq)
		return NULL;
	
	info->seq->samples = (int*) vj_calloc(sizeof(int) * MAX_SEQUENCES );
	
    info->audio = AUDIO_PLAY;
    info->continuous = 1;
    info->sync_correction = 1;
    info->sync_ins_frames = 1;
    info->sync_skip_frames = 1;
    info->double_factor = 1;
    info->no_bezerk = 1;
    info->nstreams = 1;
    info->stream_outformat = -1;

    info->settings->currently_processed_entry = -1;
    info->settings->first_frame = 1;
    info->settings->state = LAVPLAY_STATE_STOP;
    info->uc->playback_mode = VJ_PLAYBACK_MODE_PLAIN;
    info->uc->use_timer = 2;
    info->uc->sample_key = 1;
    info->uc->direction = 1;	/* pause */
    info->uc->sample_start = 0;
    info->uc->sample_end = 0;
    info->net = 1;

    veejay_memset(info->action_file,0,256); 
    veejay_memset(info->stream_outname,0,256);

    for (i = 0; i < SAMPLE_MAX_PARAMETERS; i++)
		info->effect_info->tmp[i] = 0;

#ifdef HAVE_SDL
    info->video_out = 0;
#else
#ifdef HAVE_DIRECTFB
    info->video_out = 1;
#else
    info->video_out = 3;
    sprintf(info->stream_outname, "%s", "stdout");
#endif
#endif


#ifdef HAVE_SDL
	info->sdl = (vj_sdl**) vj_calloc(sizeof(vj_sdl*) * MAX_SDL_OUT ); 
#endif


	info->settings->ncpu = smp_check();

	yuv_init_lib();
	
    return info;
}




/******************************************************
 * veejay_main()
 *   the whole video-playback cycle
 *
 * Basic setup:
 *   * this function initializes the devices,
 *       sets up the whole thing and then forks
 *       the main task and returns control to the
 *       main app. It can then start playing by
 *       setting playback speed and such. Stop
 *       by calling veejay_stop()
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/

int veejay_main(veejay_t * info)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    /* Flush the Linux File buffers to disk */
    sync();
    if(info->current_edit_list->has_audio && info->audio == AUDIO_PLAY)
    {
        info->audio_running = vj_perform_audio_start(info);
    }
    veejay_msg(VEEJAY_MSG_INFO, "Starting playback thread. Giving control to main app");

    /* fork ourselves to return control to the main app */
    if (pthread_create(&(settings->playback_thread), NULL,
		       veejay_playback_thread, (void *) info)) {
	veejay_msg(VEEJAY_MSG_ERROR, "Failed to create thread");
	return -1;
    }

    return 1;
}



/*** Methods for simple video editing (cut/paste) ***/

/******************************************************
 * veejay_edit_copy()
 *   copy a number of frames into a buffer
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/

static void	veejay_reset_el_buffer( veejay_t *info )
{
	
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    if (settings->save_list)
	free(settings->save_list);

    settings->save_list = NULL;
    settings->save_list_len = 0;

}

int veejay_edit_copy(veejay_t * info, editlist *el, long start, long end)
{

    if(el->is_empty)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No frames in EDL to copy");
		return 0;
	}

    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    uint64_t k, i;
    uint64_t n1 = (uint64_t) start;
    uint64_t n2 = (uint64_t) end;
    if (settings->save_list)
		free(settings->save_list);

    settings->save_list =
		(uint64_t *) vj_calloc((n2 - n1 + 1) * sizeof(uint64_t));

	if (!settings->save_list)
	{
		veejay_change_state_save(info, LAVPLAY_STATE_STOP);
		return 0;
	}

    k = 0;

    for (i = n1; i <= n2; i++)
		settings->save_list[k++] = el->frame_list[i];
  
    settings->save_list_len = k;

    veejay_msg(VEEJAY_MSG_DEBUG, "Copied frames %d - %d to buffer (of size %d)",n1,n2,k );

    return 1;
}
editlist *veejay_edit_copy_to_new(veejay_t * info, editlist *el, long start, long end)
{
	if( el->is_empty)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "EDL is empty");
		return NULL;
	}

	uint64_t k, i;
	uint64_t n1 = (uint64_t) start;
	uint64_t n2 = (uint64_t) end;

	long len = end - start + 1;

	if( n2 >= el->video_frames)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample end is outside of editlist");
		return NULL;
	}

	if(len <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample too short");
		return NULL;
	}

	/* Copy edl */
	editlist *new_el = vj_el_soft_clone( el );
 
	if(!new_el)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot soft clone EDL");
		return NULL;
	}

    /* copy edl frames */
   	new_el->frame_list = (uint64_t *) vj_malloc(  sizeof(uint64_t) * len );

	if (!new_el->frame_list)
	{
		veejay_msg(0, "Out of memory, unable to allocate editlist of %ld bytes", len);
		veejay_change_state_save(info, LAVPLAY_STATE_STOP);
		return NULL;
   	}

    	k = 0;


//veejay_msg(0, "start of framelist: %p, end = %p", &(el->frame_list[n1]), &(el->frame_list[n2+1]) );
//veejay_msg(0, "memcpy %p, %p", el->frame_list + n1, el->frame_list + n1 + len );
	veejay_memcpy( new_el->frame_list , el->frame_list + n1, sizeof(uint64_t) * len );
	new_el->video_frames = len;

//	for (i = n1; i <= n2; i++)
//		new_el->frame_list[k++] = el->frame_list[i];

//    	new_el->video_frames = k;
	return new_el;
}

/******************************************************
 * veejay_edit_delete()
 *   delete a number of frames from the current movie
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/

int veejay_edit_delete(veejay_t * info, editlist *el, long start, long end)
{
	if(el->is_empty)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Nothing in EDL to delete");
		return 0;
	}

	video_playback_setup *settings =
		(video_playback_setup *) info->settings;


	uint64_t i;
	uint64_t n1 =  (uint64_t) start;
	uint64_t n2 =  (uint64_t) end;

	if(info->dummy->active)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Playing dummy video!");
		return 0;
	}

	if (n2 < n1 || n1 >= el->video_frames|| n2 >= el->video_frames )
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
			    "Incorrect parameters for deleting frames");
		return 0;
    	}

    	for (i = n2 + 1; i < el->video_frames; i++)
		el->frame_list[i - (n2 - n1 + 1)] = el->frame_list[i];

	if (n1 - 1 < settings->min_frame_num)
	{
		if (n2 < settings->min_frame_num)
		    settings->min_frame_num -= (n2 - n1 + 1);
		else
		    settings->min_frame_num = n1;
    	}

    	if (n1 - 1 < settings->max_frame_num)
	{
		if (n2 <= settings->max_frame_num)
		    settings->max_frame_num -= (n2 - n1 + 1);
		else
		    settings->max_frame_num = n1 - 1;
    	}
    	
	if (n1 <= settings->current_frame_num) {

		if (settings->current_frame_num <= n2)
		{
		    settings->current_frame_num = n1;
		}
		else
		{
		    settings->current_frame_num -= (n2 - n1 + 1);
		}
    	}

    	el->video_frames -= (n2 - n1 + 1);

    	return 1;
}




/******************************************************
 * veejay_edit_cut()
 *   cut a number of frames into a buffer
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/

int veejay_edit_cut(veejay_t * info, editlist *el, long start, long end)
{
	if( el->is_empty )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Nothing to cut in EDL");
		return 0;
	}
    if (!veejay_edit_copy(info, el,start, end))
	return 0;
    if (!veejay_edit_delete(info, el,start, end))
	return 0;

    return 1;
}


/******************************************************
 * veejay_edit_paste()
 *   paste frames from the buffer into a certain position
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/

int veejay_edit_paste(veejay_t * info, editlist *el, long destination)
{
	video_playback_setup *settings =
		(video_playback_setup *) info->settings;
	uint64_t i, k;

	if (!settings->save_list_len || !settings->save_list)
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
			    "No frames in the buffer to paste");
		return 0;
	 }

	if(el->is_empty)
	{
		destination = 0;
	}
	else
	{
		if (destination < 0 || destination >= el->video_frames)
		{
			if(destination < 0)
				veejay_msg(VEEJAY_MSG_ERROR, 
					    "Destination cannot be negative");
			if(destination >= el->video_frames)
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot paste beyond Edit List!");
			return 0;
    		}
	}

        el->frame_list = (uint64_t*)realloc(el->frame_list,
				   ((el->is_empty ? 0 :el->video_frames) +
				    settings->save_list_len) *
				   sizeof(uint64_t));
	if (!el->frame_list)
	{
		veejay_change_state_save(info, LAVPLAY_STATE_STOP);
		return 0;
    }

   	k = (uint64_t)settings->save_list_len;
    	for (i = el->video_frames - 1; i >= destination && i > 0; i--)
		el->frame_list[i + k] = el->frame_list[i];
    	k = destination;
	for (i = 0; i < settings->save_list_len; i++)
	{
		if (k <= settings->min_frame_num)
		    settings->min_frame_num++;
		if (k < settings->max_frame_num)
		    settings->max_frame_num++;

		el->frame_list[k] = settings->save_list[i];
		k++;
	}
	el->video_frames += settings->save_list_len;

	if(el->is_empty)
		el->is_empty = 0;
    	veejay_increase_frame(info, 0);


	veejay_msg(VEEJAY_MSG_DEBUG,
		"Pasted %lld frames from buffer into position %ld in movie",
			settings->save_list_len, destination );
	return 1;
}


/******************************************************
 * veejay_edit_move()
 *   move a number of frames to a different position
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/

int veejay_edit_move(veejay_t * info,editlist *el, long start, long end,
		      long destination)
{
    long dest_real;
    if( el->is_empty )
		return 0;
	
    if (destination >= el->video_frames || destination < 0
		|| start < 0 || end < 0 || start >= el->video_frames
		|| end >= el->video_frames || end < start)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid parameters for moving video from %ld - %ld to position %ld",
			start,end,destination);
		veejay_msg(VEEJAY_MSG_ERROR, "Range is 0 - %ld", el->video_frames);   
		return 0;
    }

    if (destination < start)
		dest_real = destination;
    else if (destination > end)
		dest_real = destination - (end - start + 1);
    else
		dest_real = start;

    if (!veejay_edit_cut(info, el, start, end))
		return 0;

    if (!veejay_edit_paste(info, el,dest_real))
		return 0;


	return 1;
}


/******************************************************
 * veejay_edit_addmovie()
 *   add a number of frames from a new movie to a
 *     certain position in the current movie
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/

int veejay_edit_addmovie_sample(veejay_t * info, char *movie, int id )
{
	char *files[1];

	files[0] = strdup(movie);
	sample_info *sample = NULL;
	editlist *sample_edl = NULL;
	// if sample exists, get it for update
	if(sample_exists(id) )
		sample = sample_get(id);

	// if sample exists, it could have a edit list */
	if( sample )
	{
		if( !sample_usable_edl( id ) )
		{
			veejay_msg(0, "Sample %d has no EDL (its a picture!)", id );
			return -1;
		}

		sample_edl = sample_get_editlist( id );
	}

	// if both, append it to sample's edit list 
	if(sample_edl && sample)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Adding video file to existing sample %d", id );
		if(veejay_edit_addmovie_sample( info, movie, id))
			return 0;
		return -1;
	}

	// create initial edit list for sample (is currently playing)
	if(!sample_edl) 
		sample_edl = vj_el_init_with_args( files,1,info->preserve_pathnames,info->auto_deinterlace,0,
				info->edit_list->video_norm , info->pixel_format);
	// if that fails, bye
	if(!sample_edl)
	{
		veejay_msg(0, "Error while creating EDL");
		return -1;
	}
	// the editlist dimensions must match (there's more)
	if( sample_edl->video_width != info->edit_list->video_width ||
	    sample_edl->video_height != info->edit_list->video_height )
	{
		if(sample_edl) 
			vj_el_free(sample_edl);
		veejay_msg(0, "Frame dimensions do not match. Abort");
		return -1;
	}

	// the sample is not there yet,create it
	if(!sample)
	{
		sample = sample_skeleton_new( 0, sample_edl->video_frames - 1 );
		if(sample)
		{
			sample->edit_list = sample_edl;
			sample_store(sample);
			//	sample_set_editlist( sample->sample_id , sample_edl );

			veejay_msg(VEEJAY_MSG_INFO,
				"Created new sample %d from file %s",sample->sample_id,
					files[0]);
		}
		else
			veejay_msg(VEEJAY_MSG_ERROR,
			"Failed to create new sample from file '%s'",
			 files[0]);

	}

	// free temporary values
   	if(files[0]) free(files[0]);

	// return new id
        return sample->sample_id;
}

int veejay_edit_addmovie(veejay_t * info, editlist *el, char *movie, long start,long end )
{
	video_playback_setup *settings =
		(video_playback_setup *) info->settings;
	uint64_t n, i;
	uint64_t c = el->video_frames;
	if( el->is_empty )
		c -= 2;

	int res = open_video_file(movie, el, info->preserve_pathnames, info->auto_deinterlace,1,
		info->edit_list->video_norm );

	if (res < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Error adding file '%s' to EDL", movie );
		return 0;
	}

	end = el->video_frames;

	el->frame_list = (uint64_t *) realloc(el->frame_list, (end + el->num_frames[n])*sizeof(uint64_t));
	if (el->frame_list==NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
		vj_el_free(el);
		return 0;
	}

	for (i = 0; i < el->num_frames[n]; i++)
	{
		el->frame_list[c] = EL_ENTRY(n, i);
		c++;
	}
 
	el->video_frames = c;

	settings->max_frame_num = el->video_frames - 1;
	settings->min_frame_num = 0;

	return 1;
}



/******************************************************
 * veejay_toggle_audio()
 *   mutes or unmutes audio (1 = on, 0 = off)
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/


int veejay_toggle_audio(veejay_t * info, int audio)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    editlist *el = info->current_edit_list;

    if( !(el->has_audio) ) {
	veejay_msg(VEEJAY_MSG_WARNING, 
		    "Audio playback has not been enabled");
	info->audio = 0;
	return 0;
    }

    settings->audio_mute = !settings->audio_mute;

    veejay_msg(VEEJAY_MSG_DEBUG, 
		"Audio playback was %s", audio == 0 ? "muted" : "unmuted");
    
 
    return 1;
}



/*** Methods for saving the currently played movie to editlists or open new movies */

/******************************************************
 * veejay_save_selection()
 *   save a certain range of frames to an editlist
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/


/******************************************************
 * veejay_save_all()
 *   save the whole current movie to an editlist
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/
int veejay_save_all(veejay_t * info, char *filename, long n1, long n2)
{
	if( info->edit_list->num_video_files <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "EditList has no contents!");
		return 0;
	}
	if(n1 == 0 && n2 == 0 )
		n2 = info->edit_list->video_frames - 1;
	if( vj_el_write_editlist( filename, n1,n2, info->edit_list ) )
		veejay_msg(VEEJAY_MSG_INFO, "Saved editlist to file [%s]", filename);
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error while saving editlist!");
		return 0;
	}	

    return 1;
}

/******************************************************
 * veejay_open()
 *   open a new (series of) movie
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/
/******************************************************
 * veejay_open()
 *   open a new (series of) movie
 *
 * return value: 1 on succes, 0 on error
 ******************************************************/
// open_video_files is called BEFORE init
static int	veejay_open_video_files(veejay_t *info, char **files, int num_files, int force_pix_fmt, int force , char override_norm)
{
	vj_el_frame_cache(info->seek_cache );

	if(info->auto_deinterlace)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Auto deinterlacing (for playback on monitor / beamer with vga input");
	}

	if(num_files<=0 || files == NULL)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Fallback to dummy - no video files given at commandline");
		info->dummy->active = 1;
	}

	if(force_pix_fmt >= 0)
	{
			info->pixel_format = force_pix_fmt;
			veejay_msg(VEEJAY_MSG_INFO, "Processing video in pixel format %s %s",
						(force_pix_fmt >1  ? "JPEG YUV" : "YCbCr" ),
			((info->pixel_format == FMT_422 || info->pixel_format == FMT_422F) ? "4:2:2" : "4:2:0"));
	}

	if(!vj_avcodec_init( info->pixel_format ))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize encoders!");
		return 0;
	}
	

	//TODO: pass yuv sampling to dummy
	if( info->dummy->active )
	{
		if( !info->dummy->norm )
			info->dummy->norm = 'p'; 
		if( !info->dummy->fps )
			info->dummy->fps = 25.0;
		if( !info->dummy->width )
			info->dummy->width = 352;
		if( !info->dummy->height)
			info->dummy->height = 288;
		if( !force_pix_fmt)
			info->dummy->chroma = CHROMA420;
		else
			info->dummy->chroma = CHROMA422;	
		if( !info->dummy->arate)
			info->dummy->arate = 48000;
	
		info->current_edit_list = vj_el_dummy( 0, info->auto_deinterlace, info->dummy->chroma,
				info->dummy->norm, info->dummy->width, info->dummy->height, info->dummy->fps,
				force_pix_fmt );

		if( info->dummy->arate )
		{
			editlist *el = info->current_edit_list;
			el->has_audio = 1;
			el->audio_rate = info->dummy->arate;
			el->audio_chans = 2;
			el->audio_bits = 16;
			el->audio_bps = 4;
			veejay_msg(VEEJAY_MSG_DEBUG, "Dummy Audio: %f KHz, %d channels, %d bps, %d bit audio",
				(float)el->audio_rate/1000.0,el->audio_chans,el->audio_bps,el->audio_bits);
		}
	}
	else
	{
	    	info->current_edit_list = 
			vj_el_init_with_args(
					files,
					num_files,
					info->preserve_pathnames,
					info->auto_deinterlace,
				       	force,
					override_norm,
					force_pix_fmt);
	}
	info->edit_list = info->current_edit_list;

	if(info->edit_list==NULL)
	{
		return 0;
	}

	info->effect_frame_info->width = info->current_edit_list->video_width;
	info->effect_frame_info->height= info->current_edit_list->video_height;

	if(info->settings->output_fps > 0.0)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Overriding Framerate with %2.2f", 
			info->settings->output_fps);
		info->current_edit_list->video_fps = info->settings->output_fps;
	}	
	else
	{
		info->settings->output_fps = info->current_edit_list->video_fps;
	}
// init tags , samples ?

	return 1;
}

int veejay_open_files(veejay_t * info, char **files, int num_files, float ofps, int force,int force_pix_fmt, char override_norm)
{
	int ret = 0;
   	video_playback_setup *settings =
		(video_playback_setup *) info->settings;

	vj_el_init( force_pix_fmt );
	
	/* override options */
	if(ofps<=0.0)
		ofps = settings->output_fps;

	settings->output_fps = ofps;

	if(num_files == 0)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Trying to start without video");
		ret = veejay_open_video_files( info, NULL, 0 , force_pix_fmt, force,
			override_norm );
	}
	else
	{
		ret = veejay_open_video_files( info, files, num_files, force_pix_fmt, force,
			override_norm );
	}

	return ret;
}

