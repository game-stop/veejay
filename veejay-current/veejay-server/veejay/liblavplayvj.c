/*liblavplayvj - a extended librarified Linux Audio Video playback/Editing
 * VJ'fied by	Niels Elburg <nwelburg@gmail.com>
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
#include <sys/sysinfo.h>
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
#ifdef HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#endif

#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-perform.h>
#include <veejay/vj-plug.h>
#include <veejay/vj-lib.h>
#include <libel/vj-avcodec.h>
#include <libel/pixbuf.h>
#ifdef HAVE_JACK
#include <veejay/vj-jack.h>
#endif
#include <libyuv/yuvconv.h>
#include <veejay/vj-composite.h>
#include <veejay/vj-viewport.h>
#include <veejay/vj-OSC.h>
#include <veejay/vj-task.h>
#include <libplugger/plugload.h>
#include <libstream/vj-vloopback.h>
#ifdef HAVE_SDL_TTF
#include <veejay/vj-sdl-font.h>
#endif
#define QUEUE_LEN 1
#include <veejay/vims.h>
#include <libqrwrap/qrwrapper.h>
/*
#ifdef HAVE_GL
#include <veejay/gl.h>
#endif
*/
#include <sched.h>
#include <veejay/vj-shm.h>

static	veejay_t	*veejay_instance_ = NULL;
static	int		best_performance_ = 0;

void	veejay_set_instance( veejay_t *info )
{
	veejay_instance_ = info;
}


static void	veejay_schedule_fifo( veejay_t *info, int pid );

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
extern void GoMultiCast(const char *groupname);
extern void set_pixel_range(uint8_t Yhi,uint8_t Uhi, uint8_t Ylo, uint8_t Ulo);
 
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
int	veejay_set_yuv_range(veejay_t *info) {
	switch(info->pixel_format) {
		case FMT_422:
		//	rgb_parameter_conversion_type_ = 3; //JPEG/JFIF
		//	rgb_parameter_conversion_type_ = 1; //CCIR601_RGB;
			set_pixel_range( 235,240,16,16 );
			veejay_msg(VEEJAY_MSG_DEBUG, "Using CCIR601 RGB <-> YUV ");
			return 0;
			break;
		default:
		//	rgb_parameter_conversion_type_ = 0; //GIMP_RGB;
			set_pixel_range( 255, 255,0,0 );
			veejay_msg(VEEJAY_MSG_DEBUG, "Using GIMP RGB <-> YUV ");
			break;
	}
	return 1;
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

//	pthread_mutex_lock(&(settings->valid_mutex));
        settings->state = new_state;
//	pthread_mutex_unlock(&(settings->valid_mutex));
}

void veejay_change_state_save(veejay_t * info, int new_state)
{
	if(new_state == LAVPLAY_STATE_STOP )
	{
		char recover_samples[1024];
		char recover_edl[1024];
		pid_t my_pid = getpid();
		snprintf(recover_samples,1024, "%s/recovery/recovery_samplelist_p%d.sl", info->homedir, (int) my_pid);
		snprintf(recover_edl, 1024, "%s/recovery/recovery_editlist_p%d.edl", info->homedir, (int) my_pid);

		int rs = sample_writeToFile( recover_samples,info->composite,info->seq,info->font,
				info->uc->sample_id, info->uc->playback_mode );
		int re = veejay_save_all( info, recover_edl, 0, 0 );
		if(rs)
			veejay_msg(VEEJAY_MSG_WARNING, "Saved samplelist to %s", recover_samples );
		if(re)
			veejay_msg(VEEJAY_MSG_WARNING, "Saved Editlist to %s", recover_edl );
	}

	report_bug();

	veejay_change_state( info, new_state );
}

int veejay_set_framedup(veejay_t *info, int n) {
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
			if( abs(speed) <= info->current_edit_list->total_frames )
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


    return 1;
}
int veejay_hold_frame(veejay_t * info, int rel_resume_pos, int hold_pos) {
  video_playback_setup *settings = (video_playback_setup *) info->settings;
  

  if( settings->hold_status == 1 ) {
	  settings->hold_pos += rel_resume_pos;
	  settings->hold_resume ++;
	  if(settings->hold_resume < hold_pos )
		  settings->hold_resume = hold_pos;
  	} else {
	  //@first press aprox
	  settings->hold_pos = hold_pos + rel_resume_pos;
	  settings->hold_resume = hold_pos;
  }

  settings->hold_status = 1;
  return 1;
}
static void	veejay_sample_resume_at( veejay_t *info, int cur_id )
{
	long pos = 0;
	if( info->settings->sample_restart )
		pos = sample_get_startFrame( cur_id );
	else
		pos = sample_get_resume( cur_id );
	veejay_set_frame(info, pos );
	veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d continues with frame %d", cur_id, pos );
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
		if((settings->current_frame_num + num) < settings->min_frame_num) return 0;
		if((settings->current_frame_num + num) > settings->max_frame_num) return 0;
   }
   else   if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
   {
		if ((settings->current_frame_num + num) <
		    sample_get_startFrame(info->uc->sample_id)) return 0;
		if((settings->current_frame_num + num) >
		    sample_get_endFrame(info->uc->sample_id)) return 0;
    
    }

    settings->current_frame_num += num;

    return 1;
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

	vj_mem_threaded_stop();

	vj_event_destroy();

	vj_tag_free();
   	
	sample_free(info->edit_list);

	if( info->plain_editlist )
		vj_el_free(info->plain_editlist);

	vj_el_deinit();

	vj_avcodec_free();

	vj_effect_shutdown();

	task_destroy();

	if( info->settings->composite2 )
		composite_destroy( info->composite2 );
	if( info->composite )
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

    free(info->status_what);
	free(info->homedir);  
    free(info->uc);
	free(info->status_line);	
	if(info->cpumask) free(info->cpumask);
	if(info->mask) free(info->mask);
	if(info->rlinks) free(info->rlinks);
    if(info->rmodes) free(info->rmodes );
	
	free(settings);
    free(info);

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

void veejay_quit(veejay_t * info)
{
	vj_lock(info);
    veejay_change_state(info, LAVPLAY_STATE_STOP);
    vj_unlock(info);
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

    if(framenum < settings->min_frame_num)
	framenum = settings->min_frame_num;

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
		if(framenum == start || framenum == end ) 
			sample_set_framedups(info->uc->sample_id,0);
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
/*	if (info->current_edit_list->has_audio && (info->audio==AUDIO_PLAY || info->audio==AUDIO_RENDER))
        	settings->spas = 1.0 / (double) info->current_edit_list->audio_rate;
   	else
        	settings->spas = 0;*/

        settings->usec_per_frame = vj_el_get_usec_per_frame(fps);
}


int veejay_init_editlist(veejay_t * info)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    editlist *el = info->edit_list;

    /* Set min/max options so that it runs like it should */
    settings->min_frame_num = 0;
    settings->max_frame_num = el->total_frames;
    settings->current_frame_num = settings->min_frame_num;

    /* Seconds per audio sample: */
    if (info->audio==AUDIO_PLAY && info->dummy->arate > 0)
    {
	settings->spas = 1.0 / (double) info->dummy->arate;
    }
    else
    {
 	settings->spas = 0.0;
    }
    return 0;
}

static	int	veejay_stop_playing_sample( veejay_t *info, int new_sample_id )
{
	if(!sample_stop_playing( info->uc->sample_id, new_sample_id ) )
	{
		veejay_msg(0, "Error while stopping sample %d", new_sample_id );
		return 0;
	}
	
	if( info->composite ) {
		if( info->settings->composite == 2 ) {
			info->settings->composite = 1; // back to top
		} 
	}

	sample_chain_free( info->uc->sample_id );
	sample_set_framedups(info->uc->sample_id,0);
	sample_set_resume(info->uc->sample_id, info->settings->current_frame_num );
	return 1;
}
static  void	veejay_stop_playing_stream( veejay_t *info, int new_stream_id )
{
	vj_tag_disable( info->uc->sample_id );
	if( info->composite ) {
		if( info->settings->composite == 2 ) {
			info->settings->composite = 1;
		}
	}

	vj_tag_chain_free( info->uc->sample_id );
}
static	int	veejay_start_playing_sample( veejay_t *info, int sample_id )
{
	int looptype,speed,start,end;
	video_playback_setup *settings = info->settings;

	editlist *E = sample_get_editlist( sample_id );
	info->current_edit_list = E;
	veejay_reset_el_buffer(info);

	sample_start_playing( sample_id, info->no_caching );
	int tmp = sample_chain_malloc( sample_id );

   	sample_get_short_info( sample_id , &start,&end,&looptype,&speed);

	settings->min_frame_num = 0;
	settings->max_frame_num = sample_video_length( sample_id );
	settings->first_frame = 1;
	
	sample_reset_loopcount( sample_id );

#ifdef HAVE_FREETYPE
	if(info->font && info->uc->sample_id != sample_id)
	{
		void *dict = sample_get_dict( sample_id );
		vj_font_set_dict( info->font, dict );
		vj_font_prepare( info->font,start,end );

		veejay_msg(VEEJAY_MSG_DEBUG, "Subtitling sample %d: %ld - %ld", sample_id,
			  start, end );

	}
#endif
	if(info->composite )
	{
		int has_cfg = sample_load_composite_config( info->composite , sample_id );
		if( has_cfg && info->settings->composite == 1 ) {
			void *cur = sample_get_composite_view(sample_id);
			if( cur ) composite_set_backing(info->composite,cur);
		}
	}

	 info->uc->sample_id = sample_id;
	 info->last_sample_id = sample_id;

	 info->sfd = sample_get_framedup(sample_id);

	 if( info->settings->sample_restart )
		 sample_reset_offset( sample_id );	/* reset mixing offsets */
	
	 veejay_sample_resume_at( info, sample_id );

     veejay_set_speed(info, speed);
 	 
	 veejay_msg(VEEJAY_MSG_INFO, "Playing sample %d (FX=%x, Sl=%d, Speed=%d, Start=%d, Loop=%d)",
			sample_id, tmp,info->sfd, speed, start, looptype );
	 
	 return 1;
}

static	int	veejay_start_playing_stream(veejay_t *info, int stream_id )
{
	video_playback_setup *settings = info->settings;
	
	if(vj_tag_enable( stream_id ) <= 0 )
	{
		veejay_msg(0, "Unable to activate stream ?");
		return 0;
	}

	vj_tag_set_active( stream_id, 1 );

	int	tmp = vj_tag_chain_malloc( stream_id);

	settings->min_frame_num = 1;
	settings->max_frame_num = vj_tag_get_n_frames( stream_id );
#ifdef HAVE_FREETYPE
	  if(info->font )
	  {
		  void *dict = vj_tag_get_dict( stream_id );	
		  vj_font_set_dict( info->font, dict );

		  vj_font_prepare( info->font, settings->min_frame_num,
				   settings->max_frame_num );

	  }
#endif
	info->last_tag_id = stream_id;
	info->uc->sample_id = stream_id;

  	if(info->composite )
	{
		int has_cfg = vj_tag_load_composite_config( info->composite , stream_id );
		if( has_cfg && info->settings->composite == 1 ) {
			void *cur = vj_tag_get_composite_view(stream_id);
			if( cur ) composite_set_backing(info->composite,cur);
		}
	}

	veejay_msg(VEEJAY_MSG_INFO,"Playing stream %d (FX=%x) (Ff=%d)", stream_id, tmp, settings->max_frame_num );

	info->current_edit_list = info->edit_list;
  	 
	veejay_reset_el_buffer(info);
	
	return 1;
}

void veejay_change_playback_mode( veejay_t *info, int new_pm, int sample_id )
{
	if( new_pm == VJ_PLAYBACK_MODE_SAMPLE ) {
		if(!sample_exists(sample_id)) {
			veejay_msg(0,"Sample %d does not exist!");
			return;
		}
	} else if (new_pm == VJ_PLAYBACK_MODE_TAG ) {
		if(!vj_tag_exists(sample_id)) {
			veejay_msg(0,"Stream %d does not exist!");
			return;
		}
	} else if ( new_pm == VJ_PLAYBACK_MODE_PLAIN ) {
		if( info->edit_list->video_frames < 1 ) {
			veejay_msg( 0, "No video frames in EditList!");
			return;	
		}
	}

	if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
	{
		int cur_id = info->uc->sample_id;
		if( cur_id == sample_id && new_pm == VJ_PLAYBACK_MODE_SAMPLE )
		{
			if( info->settings->sample_restart )
			{
				long pos = sample_get_startFrame( cur_id );

				veejay_set_frame(info, pos );
			
				veejay_msg(VEEJAY_MSG_INFO, "Sample %d starts playing from frame %d",sample_id,pos);
			} 
			else {
				veejay_msg(VEEJAY_MSG_INFO, "Already playing sample (continous mode is on)");
			}
			return;
		}
		else
		{
			veejay_stop_playing_sample(info, cur_id );
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
			veejay_stop_playing_stream(info, cur_id );
		}
	}

	if(new_pm == VJ_PLAYBACK_MODE_PLAIN )
	{
		if(info->uc->playback_mode==VJ_PLAYBACK_MODE_TAG) 
			veejay_stop_playing_stream( info , info->uc->sample_id);
		if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
			veejay_stop_playing_sample( info,  info->uc->sample_id );
		info->uc->playback_mode = new_pm;
		info->current_edit_list = info->edit_list;
		video_playback_setup *settings = info->settings;
		settings->min_frame_num = 0;
		settings->max_frame_num = info->edit_list->total_frames;
		veejay_msg(VEEJAY_MSG_INFO, "Playing plain video, frames %ld - %ld", (long)settings->min_frame_num,  (long)settings->max_frame_num );

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
	if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
	{
		if( info->uc->sample_id == sample_id )
		{
			int start = sample_get_startFrame( info->uc->sample_id );
			veejay_set_frame(info,start+offset);
			veejay_msg(VEEJAY_MSG_INFO, "Sample %d starts playing from frame %d",sample_id,start);
			return;
		}
	}


	veejay_change_playback_mode( info, VJ_PLAYBACK_MODE_SAMPLE, sample_id );
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
			veejay_sample_resume_at( info, sampleid );
		}
		else
		{
			veejay_start_playing_sample(info,sampleid );
		}
	}
}

/******************************************************
 * veejay_create_sample
 *  create a new sample
 * return value: 1 on success, -1 on error
 ******************************************************/
int veejay_create_tag(veejay_t * info, int type, char *filename,
			int index, int channel, int device)
{
	if( type == VJ_TAG_TYPE_NET || type == VJ_TAG_TYPE_MCAST ) {
		if( (filename != NULL) && ((strcasecmp( filename, "localhost" ) == 0)  || (strcmp( filename, "127.0.0.1" ) == 0)) ) {
			if( channel == info->uc->port )	{
				veejay_msg(VEEJAY_MSG_ERROR, "It makes no sense to connect to myself (%s - %d)",
					filename,channel);
				return 0;
			}	   
		}
	}

	int id = vj_tag_new(type, filename, index, info->edit_list, info->pixel_format, channel, device,info->settings->composite );
	char descr[200];
	veejay_memset(descr,0,200);
	vj_tag_get_by_type(type,descr);
	if(id > 0 )	{
		info->nstreams++;
		veejay_msg(VEEJAY_MSG_INFO, "New Input Stream '%s' with ID %d created",descr, id );
		return id;
	} else	{
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
    }

    /*pthread_cancel( settings->playback_thread ); */
 	veejay_msg(VEEJAY_MSG_DEBUG, "Waiting for playback_thread ...");
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
    info->current_edit_list = info->edit_list;
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
#ifdef HAVE_DIRECTFB
	uint8_t *c_frame[3];
#endif
	int i = 0;
	int skip_update = 0;

	video_playback_setup *settings = info->settings;
	int check_vp = settings->composite;

	if( info->settings->unicast_frame_sender )
	{
		vj_perform_send_primary_frame_s2(info, 0, info->uc->current_link );
		vj_perform_done_s2(info);
	}

	if( info->settings->mcast_frame_sender && info->settings->use_vims_mcast )
	{
		vj_perform_send_primary_frame_s2(info, 1, info->uc->current_link);
		vj_perform_done_s2(info);
	}

	vj_perform_get_primary_frame(info,frame);

#ifdef HAVE_SDL
	if(check_vp)
	{
		if( info->video_out == 0 ) {
			if(!vj_sdl_lock( info->sdl[0] ) )
				return 0;
			composite_blit_yuyv( info->composite,frame, vj_sdl_get_yuv_overlay(info->sdl[0]),settings->composite);
			if(!vj_sdl_unlock( info->sdl[0]) )
				return 0;
		} 
		if( info->video_out != 4 ) {
			skip_update = 1;
		}
	} 
#endif
	if( info->shm && vj_shm_get_status(info->shm) == 1 )
	{
		int plane_sizes[4] = { info->effect_frame1->len, info->effect_frame1->uv_len,
		   		info->effect_frame1->uv_len,0 };	
		if( vj_shm_write(info->shm, frame,plane_sizes) == -1 ) {
			veejay_msg(0, "failed to write to shared resource!");
		}
	}
      
	if( info->vloopback )
	{
		vj_vloopback_fill_buffer( info->vloopback , frame );		
		vj_vloopback_write_pipe( info->vloopback );
	}

#ifdef HAVE_JPEG
#ifdef USE_GDK_PIXBUF 
        if (info->uc->hackme == 1)
        {
                info->uc->hackme = 0;
#ifdef USE_GDK_PIXBUF
                if(!vj_picture_save( info->settings->export_image, frame, 
                                info->video_output_width, info->video_output_height,
                                get_ffmpeg_pixfmt( info->pixel_format )) )
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
		
#ifdef HAVE_SDL	
	if(skip_update) {
		if(info->video_out == 0 ) { 
		   for(i = 0 ; i < MAX_SDL_OUT; i ++ )
			if( info->sdl[i] )
				vj_sdl_flip(info->sdl[i]);
		}
		return 1;
	}
#endif
    	switch (info->video_out)
	{
#ifdef HAVE_SDL
		case 0:
			for(i = 0 ; i < MAX_SDL_OUT; i ++ )
			if( info->sdl[i] )
				if(!vj_sdl_update_yuv_overlay( info->sdl[i], frame ) )  return 0;  
	   	break;
#endif
		case 1:
#ifdef HAVE_DIRECTFB
	   		vj_perform_get_primary_frame_420p(info,c_frame);
	    		if (vj_dfb_update_yuv_overlay(info->dfb, c_frame) != 0)
			{
				return 0;
	    		}
#endif
	    	break;
		case 2:
#ifdef HAVE_DIRECTFB
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
#endif
			 break;
		case 3:
			break;	
		case 4:
			if( vj_yuv_put_frame( info->y4m, frame ) == -1 ) {
				veejay_msg(0, "Failed to write a frame!");
				veejay_change_state(info,LAVPLAY_STATE_STOP);
				return 0;
			}
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
		struct timespec now;
		struct timespec nsecsleep;

		int usec_since_lastframe=0;
		for (;;) {
			clock_gettime( CLOCK_REALTIME, &now );
		
			usec_since_lastframe = (now.tv_nsec - settings->lastframe_completion.tv_nsec)/1000;
		
			if (usec_since_lastframe < 0)
				usec_since_lastframe += 1000000;
	  		if (now.tv_sec > settings->lastframe_completion.tv_sec + 1)
				usec_since_lastframe = 1000000;

	   		if (settings->first_frame || (frame_periods * settings->usec_per_frame - usec_since_lastframe) < (1000000 / HZ)) {
				break;	
			}
	
			nsecsleep.tv_nsec = (settings->usec_per_frame - usec_since_lastframe -  1000000 / HZ) * 1000;    	
	    		nsecsleep.tv_sec = 0;
	    		clock_nanosleep(CLOCK_REALTIME,0, &nsecsleep, NULL);
		}
    }

    settings->first_frame = 0;
    clock_gettime( CLOCK_REALTIME, &(settings->lastframe_completion) );
    settings->syncinfo[settings->currently_processed_frame].timestamp = settings->lastframe_completion;
}

void veejay_pipe_write_status(veejay_t * info)
{
    video_playback_setup *settings = (video_playback_setup *) info->settings;
    int d_len = 0;
    int pm = info->uc->playback_mode;
    int total_slots = sample_size()-1;
    int tags = vj_tag_true_size() -1;
	int cache_used = 0;
	int mstatus = vj_event_macro_status();
	int curfps  = (int) ( 100.0f / settings->spvf );
	
	if(tags>0)
		total_slots+=tags;
    
	switch (info->uc->playback_mode) {
    	case VJ_PLAYBACK_MODE_SAMPLE:
			cache_used = sample_cache_used(0);

		if( info->settings->randplayer.mode == RANDMODE_SAMPLE)
			pm = VJ_PLAYBACK_MODE_PATTERN;

		if( sample_chain_sprint_status
			(info->uc->sample_id,cache_used,info->seq->size,info->seq->current,info->real_fps,settings->current_frame_num, pm, total_slots,info->seq->rec_id,curfps,settings->cycle_count[0],settings->cycle_count[1],mstatus,info->status_what ) != 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Fatal error, tried to collect properties of invalid sample");
			veejay_change_state( info, LAVPLAY_STATE_STOP );
		}
		break;
       	case VJ_PLAYBACK_MODE_PLAIN:
		// 26 status symbols
			{
				char *ptr = info->status_what;
				*ptr++ = ' ';
				ptr = vj_sprintf( ptr, info->real_fps ); *ptr++= ' ';
				ptr = vj_sprintf( ptr, settings->current_frame_num ); *ptr++ = ' ';
				ptr = vj_sprintf( ptr, info->uc->playback_mode ); *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				ptr = vj_sprintf( ptr, settings->min_frame_num ); *ptr++ = ' ';
				ptr = vj_sprintf( ptr, settings->max_frame_num ); *ptr++ = ' ';
				ptr = vj_sprintf( ptr, settings->current_playback_speed ); *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				ptr = vj_sprintf( ptr, total_slots ); *ptr++ = ' ';
				ptr = vj_sprintf( ptr, cache_used ); *ptr++ = ' ';
				ptr = vj_sprintf( ptr, curfps ); *ptr++ = ' ';
				ptr = vj_sprintf( ptr, settings->cycle_count[0] ); *ptr++ = ' ';
				ptr = vj_sprintf( ptr, settings->cycle_count[1] ); *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				*ptr++ = '0'; *ptr++ = ' ';
				ptr = vj_sprintf( ptr, mstatus );
			}
		break;
    	case VJ_PLAYBACK_MODE_TAG:
		if( vj_tag_sprint_status( info->uc->sample_id,cache_used,info->seq->size,info->seq->current, info->real_fps,
			settings->current_frame_num, info->uc->playback_mode,total_slots,curfps,settings->cycle_count[0],settings->cycle_count[1],mstatus, info->status_what ) != 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid status!");
		}
		break;
    }
    
	d_len = strlen(info->status_what);
	info->status_line_len = d_len + 5;
	snprintf( info->status_line, MESSAGE_SIZE, "V%03dS%s", d_len, info->status_what );

    if (info->uc->chain_changed == 1)
		info->uc->chain_changed = 0;
}
static	char	*veejay_concat_paths(char *path, char *suffix)
{
	int n = strlen(path) + strlen(suffix) + 2;
	char *str = vj_calloc( n * sizeof(char));
	sprintf(str, "%s/%s", path,suffix);
	return str;
}

static int	veejay_is_dir(char *path)
{
	struct stat s;
	if( stat( path, &s ) == -1 )
	{
		veejay_msg(0, "%s (%s)", strerror(errno),path);
		return 0;
	}
	if( !S_ISDIR( s.st_mode )) {
		veejay_msg(0, "%s is not a valid path.");
		return 0;
	}
	return 1;
}	
static	int	veejay_valid_homedir(char *path)
{
	char *recovery_dir = veejay_concat_paths( path, "recovery" );
	char *theme_dir    = veejay_concat_paths( path, "theme" );
	int sum = veejay_is_dir( recovery_dir );
	sum += veejay_is_dir( theme_dir );
	sum += veejay_is_dir( path );
	free(theme_dir);
	free(recovery_dir);
	if( sum == 3 ) 
		return 1;
	return 0;
}
static	int	veejay_create_homedir(char *path)
{
	if( mkdir(path,0700 ) == -1 )
	{
		if( errno != EEXIST )
		{
			veejay_msg(0, "Unable to create %s - No veejay home setup (%s)", strerror(errno));
			return 0;	
		}
	}

	char *recovery_dir = veejay_concat_paths( path, "recovery" );
	if( mkdir(recovery_dir,0700) == -1 ) {
		if( errno != EEXIST )
		{
			veejay_msg(0, "%s (%s)", strerror(errno), path);
			free(recovery_dir);
			return 0;
		}
	}
	free(recovery_dir);

	char *theme_dir = veejay_concat_paths( path, "theme" );
	if( mkdir(theme_dir,0700) == -1 ) {
		if( errno != EEXIST )
		{
			veejay_msg(0, "%s (%s)", strerror(errno), path);
			free(theme_dir);
			return 0;
		}
	}
	free(theme_dir);
	
	char *font_dir = veejay_concat_paths( path, "fonts" );
	if( mkdir(font_dir,0700) == -1 ) {
		if( errno != EEXIST )
		{
			veejay_msg(0, "%s (%s)", strerror(errno), path);
			free(font_dir);
			return 0;
		}
	}
	free(font_dir);
	return 1;
}
void	veejay_check_homedir(void *arg)
{
	veejay_t *info = (veejay_t *) arg;
	char path[1024];
	char tmp[1024];
	char *home = getenv("HOME");
	if(!home)
	{
		veejay_msg(VEEJAY_MSG_ERROR,
				"HOME environment variable not set.");
		return;
	}
	snprintf(path,sizeof(path), "%s/.veejay", home );
	info->homedir = vj_strndup( path, 1024 );

	if( veejay_valid_homedir(path) == 0)
	{
		if( veejay_create_homedir(path) == 0 )
		{	
			veejay_msg(VEEJAY_MSG_ERROR,
				"Can't create %s",path);
			return;
		}
	}

	snprintf(tmp,sizeof(tmp), "%s/plugins.cfg", path );
	struct statfs ts;
	if( statfs( tmp, &ts ) != 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING,"No plugins.cfg found (see DOC/HowtoPlugins)");
	}
	snprintf(tmp,sizeof(tmp), "%s/viewport.cfg", path);
	if( statfs( tmp, &ts ) != 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING,"No viewport.cfg found (start veejay with -D -w -h and press CTRL-V to setup viewport)");
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
		if( sig == SIGSEGV || sig == SIGBUS || sig == SIGPWR || sig == SIGABRT || sig == SIGFPE )
		{
			if(info->homedir)
				veejay_change_state_save(info,LAVPLAY_STATE_STOP);
			else
				veejay_change_state( info, LAVPLAY_STATE_STOP );
			signal( sig, SIG_DFL );
		}
		else if( sig == SIGPIPE ) {
			veejay_change_state( info, LAVPLAY_STATE_STOP );
		}
	}
}


static void veejay_handle_callbacks(veejay_t *info) {

	/* check for OSC events */
	vj_osc_get_packet(info->osc);

	/*  update network */
	vj_event_update_remote( (void*)info );

	veejay_pipe_write_status( info );

	/* create status message and write to clients */
//	int status_line_len = strlen( info->status_line );
	int i;
	for( i = 0; i < VJ_MAX_CONNECTIONS ; i ++ ) {
		if( !vj_server_link_can_write( info->vjs[VEEJAY_PORT_STA],  i ) ) 
			continue;
		int res = vj_server_send( info->vjs[VEEJAY_PORT_STA], i, (uint8_t*)info->status_line, info->status_line_len);
		if( res < 0 ) {
			_vj_server_del_client( info->vjs[VEEJAY_PORT_CMD], i );
			_vj_server_del_client( info->vjs[VEEJAY_PORT_STA], i );
			_vj_server_del_client( info->vjs[VEEJAY_PORT_DAT], i );
		}
	}
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
	if( info->video_out == 0 || info->video_out == 2)
	{
		SDL_Event event;
		int ctrl_pressed = 0;
		int shift_pressed = 0;
		int alt_pressed = 0;
		int mouse_x=0,mouse_y=0,but=0;
		while(SDL_PollEvent(&event) == 1) 
		{
			int mod = SDL_GetModState();
			if( event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN)
			{
				vj_event_single_fire( (void*) info, event, 0);
			}
			if( event.type == SDL_MOUSEMOTION )
			{
				mouse_x = event.button.x;
				mouse_y = event.button.y;
			}

			if( info->use_mouse && event.type == SDL_MOUSEBUTTONDOWN )
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

			if( info->use_mouse && event.type == SDL_MOUSEBUTTONUP )
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
				else if (mev->button == SDL_BUTTON_WHEELUP  )  
				{	
					but = 15;
				}	
				else if (mev->button == SDL_BUTTON_WHEELDOWN  )
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
}

static void *veejay_mjpeg_playback_thread(void *arg)
{
    veejay_t *info = (veejay_t *) arg;
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
   /* Allow easy shutting down by other processes... */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	
	vj_get_relative_time();

    vj_osc_set_veejay_t(info); 
    vj_tag_set_veejay_t(info);

#ifdef HAVE_SDL
    if( info->settings->repeat_delay > 0 && info->settings->repeat_interval ) {
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
	}
#endif
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
		veejay_msg(VEEJAY_MSG_DEBUG,"Veejay was told to exit");
		pthread_mutex_unlock(&(settings->valid_mutex));
		pthread_exit(NULL);
	 	return NULL;
	    }
	}
	pthread_mutex_unlock(&(settings->valid_mutex));

        if( settings->state != LAVPLAY_STATE_PAUSED && settings->currently_processed_entry != settings->buffer_entry[settings->currently_processed_frame] &&
		!veejay_screen_update(info)  )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Error playing frame %d. I won't give up yet!", settings->current_frame_num);
	}

	settings->currently_processed_entry = 
		settings->buffer_entry[settings->currently_processed_frame];

	// timestamp frame after sync
	veejay_mjpeg_software_frame_sync(info,
					  settings->valid[settings->
							  currently_processed_frame]);
	settings->syncinfo[settings->currently_processed_frame].frame = settings->currently_processed_frame;

	pthread_mutex_lock(&(settings->valid_mutex));
	settings->valid[settings->currently_processed_frame] = 0;
	pthread_cond_broadcast(&(settings->buffer_done[settings->currently_processed_frame]));
	pthread_mutex_unlock(&(settings->valid_mutex));

	settings->currently_processed_frame = 
	    (settings->currently_processed_frame + 1) % 1;
    }
    veejay_msg( VEEJAY_MSG_INFO, "Playback thread: was told to exit");
    pthread_exit(NULL);

    return NULL;
}


char	*veejay_title(veejay_t *info)
{
	char tmp[128];
	snprintf(tmp,sizeof(tmp), "Veejay %s on port %d in %dx%d@%2.2f", VERSION,
	      info->uc->port, info->video_output_width,info->video_output_height,info->dummy->fps );
	return strdup(tmp);
}


int veejay_open(veejay_t * info)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    int i;
    veejay_msg(VEEJAY_MSG_DEBUG, "Initializing the threading system");

    veejay_memset( &(settings->lastframe_completion), 0, sizeof(struct timeval));

    pthread_mutex_init(&(settings->valid_mutex), NULL);
    pthread_mutex_init(&(settings->syncinfo_mutex), NULL);
    
	/* Invalidate all buffers, and initialize the conditions */
    for(i = 0; i < QUEUE_LEN ;i ++ ) {
		settings->valid[i] = 0;
		settings->buffer_entry[i] = 0;
		pthread_cond_init(&(settings->buffer_filled[i]), NULL);
		pthread_cond_init(&(settings->buffer_done[i]), NULL);

		veejay_memset( &(settings->syncinfo[i]), 0, sizeof(struct mjpeg_sync));
    }

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
    
	return 1;
}

static int veejay_get_norm( char n )
{
	if( n == 'p' )
		return VIDEO_MODE_PAL;
	if( n == 's' )
		return VIDEO_MODE_SECAM;
	if( n == 'n' )
		return VIDEO_MODE_NTSC;

	return VIDEO_MODE_PAL;
}

static int veejay_mjpeg_set_playback_rate(veejay_t * info, float video_fps, int norm)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    settings->spvf = 1.0 / video_fps;

    settings->usec_per_frame = vj_el_get_usec_per_frame(video_fps);

    return 1;
}

/******************************************************
 * veejay_mjpeg_queue_buf()
 *   queue a buffer
 *
 * return value: 1 on success, 0 on error
 ******************************************************/

static void veejay_mjpeg_queue_buf(veejay_t * info,int frame,   int frame_periods)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    /* mark this buffer as playable and tell the software playback thread to wake up if it sleeps */
    pthread_mutex_lock(&(settings->valid_mutex));
    settings->valid[frame] = frame_periods;
    pthread_cond_broadcast(&(settings->buffer_filled[frame]));
    pthread_mutex_unlock(&(settings->valid_mutex));
}


/******************************************************
 * veejay_mjpeg_sync_buf()
 *   sync on a buffer
 *
 * return value: 1 on success, 0 on error
 ******************************************************/

static int veejay_mjpeg_sync_buf(veejay_t * info, struct mjpeg_sync *bs)
{
    video_playback_setup *settings = (video_playback_setup *) info->settings;
     /* Wait until this buffer has been played */
	
    pthread_mutex_lock(&(settings->valid_mutex));
    while (settings->valid[settings->currently_synced_frame] != 0) {
		pthread_cond_wait(&
			  (settings->
			   buffer_done[settings->currently_synced_frame]),
			  &(settings->valid_mutex));
    }
    pthread_mutex_unlock(&(settings->valid_mutex));
    veejay_memcpy(bs, &(settings->syncinfo[settings->currently_synced_frame]),sizeof(struct mjpeg_sync));

    settings->currently_synced_frame = (settings->currently_synced_frame + 1) % QUEUE_LEN;
	
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

    veejay_msg(VEEJAY_MSG_DEBUG, "Closing down the threading system ");

    pthread_cancel(settings->software_playback_thread);
    if (pthread_join(settings->software_playback_thread, NULL)) {
	veejay_msg(VEEJAY_MSG_ERROR,"Failure deleting software playback thread");
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


int veejay_init(veejay_t * info, int x, int y,char *arg, int def_tags, int gen_tags )
{
	editlist *el = NULL;
	video_playback_setup *settings = info->settings;

	available_diskspace();

	int id=0;

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

	vj_event_init((void*)info);

	switch (info->uc->use_timer)
	{
		case 0:
			veejay_msg(VEEJAY_MSG_WARNING, "Not timing audio/video");
		break;
    		default:
			veejay_msg(VEEJAY_MSG_DEBUG, "Using high-resolution clock to time audio/video");
		break;
    	}    

 	if (veejay_init_editlist(info) != 0) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
			    "Cannot initialize the EditList");
		return -1;
	}

	el = info->edit_list;

	if(!vj_mem_threaded_init( info->video_output_width, info->video_output_height ) )
		return 0;


	vj_tag_set_veejay_t(info);


	int driver = 1;
	if (vj_tag_init(info->video_output_width, info->video_output_height, info->pixel_format,driver) != 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing Stream Manager");
		return -1;
    	}

	info->font = vj_font_init( info->video_output_width,info->video_output_height,el->video_fps,0 );

	if(!info->font) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing font system.");
		return -1;
	}


	if(info->settings->composite)
	{
		info->osd = vj_font_single_init( info->video_output_width,info->video_output_height,el->video_fps,info->homedir  );

	}
	else
	{	
		info->osd = vj_font_single_init( info->video_output_width,info->video_output_height,el->video_fps,info->homedir );
	}
	

	if(!info->osd) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing font system for OSD.");
		return -1;
	}


 	sample_init( (info->video_output_width * info->video_output_height), info->font, info->plain_editlist ); 

	sample_set_project( info->pixel_format,
			    info->auto_deinterlace,
			    info->preserve_pathnames,
				0,
			    el->video_norm,
		       	    info->video_output_width,
			    info->video_output_height);

	int full_range = veejay_set_yuv_range( info );

	info->settings->sample_mode = SSM_422_444;
	
	veejay_msg(VEEJAY_MSG_DEBUG, "Internal YUV format is 4:2:2 Planar, %d x %d",
				info->video_output_width,
				info->video_output_height);
	veejay_msg(VEEJAY_MSG_DEBUG, "FX Frame Info: %d x %d, ssm=%d, format=%d",
			info->effect_frame1->width,info->effect_frame1->height,
			info->effect_frame1->ssm,
			info->effect_frame1->format );
	veejay_msg(VEEJAY_MSG_DEBUG, "               %d x %d (h=%d,v=%d)",
			info->effect_frame1->uv_width,info->effect_frame1->uv_height,
			info->effect_frame1->shift_v, info->effect_frame1->shift_h );
	veejay_msg(VEEJAY_MSG_DEBUG, "               Y=%d bytes, UV=%d bytes",
			info->effect_frame1->len,
			info->effect_frame1->uv_len );
	
	if(!vj_perform_init(info))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize Veejay Performer");
		return -1;
    }

	if( vj_el_get_mem_size() == 0 )
		prepare_cache_line( info->uc->max_cached_mem,info->uc->max_cached_slots );

	info->shm = vj_shm_new_master( info->homedir,info->effect_frame1 );
	if( !info->shm ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Unable to initialize shared resource!");
	}


	if( info->settings->composite )
	{
		int comp_mode = info->settings->composite;
		info->composite = composite_init( info->video_output_width, info->video_output_height,
						  info->video_output_width, info->video_output_height,
						  info->homedir,
						  info->settings->sample_mode,
						  yuv_which_scaler(),
						  info->pixel_format,
			       			  &comp_mode	);
		if(!info->composite) {
			return -1;
		}

		info->composite2 = info->composite; // save ptr, composite

		info->settings->zoom = 0;
		info->settings->composite = ( comp_mode == 1 ? 1: 0);
	}

	if(!info->bes_width)
		info->bes_width = info->video_output_width;
	if(!info->bes_height)
		info->bes_height = info->video_output_height;	

	if( el->has_audio && vj_perform_init_audio(info) && info->audio == AUDIO_PLAY)	{
		veejay_msg(VEEJAY_MSG_INFO, "Initialized Audio Task");
	} else {
		info->audio = NO_AUDIO;
	}

  	veejay_msg(VEEJAY_MSG_INFO, 
		"Initialized %d Image- and Video Effects", vj_effect_max_effects());
    	vj_effect_initialize( info->video_output_width,info->video_output_height,full_range);
   
    	if(info->dump) vj_effect_dump(); 	
	
	if( info->settings->action_scheduler.sl && info->settings->action_scheduler.state )
	{
		if(sample_readFromFile( info->settings->action_scheduler.sl,
				info->composite,
				info->seq, info->font, el, &(info->uc->sample_id), &(info->uc->playback_mode) ) )
			veejay_msg(VEEJAY_MSG_INFO, "Loaded sample list %s from actionfile - ",
					info->settings->action_scheduler.sl );
	}
	
   
	if( settings->action_scheduler.state )
	{
		settings->action_scheduler.state = 0; 
	}

	int instances = 0;

	char *title = NULL;

	while( (instances < 4 ) && !vj_server_setup(info))
	{
		int port = info->uc->port;
		int new_port = info->uc->port + 1000;
		instances ++;
		veejay_msg(VEEJAY_MSG_ERROR,"Port %d -~ %d in use, trying to start on port %d (%d/%d attempts)", port,port+6, new_port , 4 - instances, instances);
		info->uc->port = new_port;
	}

	if( instances >= 4 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to start network server. Most likely, there is already a veejay running");
		veejay_msg(VEEJAY_MSG_ERROR,"If you want to run multiple veejay's on the same machine, use the '-p/--port'");
		veejay_msg(VEEJAY_MSG_ERROR,"commandline option. For example: $ veejay -p 4490 -d");
		return -1;
	}

	char *my_ip = (info->qrcode == 1 ? vj_server_my_ip() : "http://veejayhq.net/contributing/" );
	if( my_ip != NULL ) {
		char outfile[1024];
		char connectionStr[1024];
		if( info->qrcode == 1 ) {
			snprintf( connectionStr, sizeof(connectionStr),"%s:%d",my_ip, info->uc->port );
		} else {
			snprintf( connectionStr, sizeof(connectionStr),"%s", my_ip);
		}
		snprintf( outfile, sizeof(outfile), "%s/QR-%d.png",info->homedir, info->uc->port );
		if(info->qrcode ==1 )
			free(my_ip);
		if( qrwrap_encode_string( outfile , connectionStr ) ) {
			veejay_msg(VEEJAY_MSG_INFO,"Connection info encoded to QR (%s -> %s)", connectionStr, outfile);
		}
	} else {
		veejay_msg(VEEJAY_MSG_WARNING,"Unable to get external IP address" );
	}

    	/* now setup the output driver */
    	switch (info->video_out)
	 {
		 /*
 		case 3:
#ifdef HAVE_GL
			veejay_msg(VEEJAY_MSG_INFO, "Using output driver OpenGL");
			info->gl = (void*) x_display_init(info);
			x_display_open(info->gl, el->video_width, el->video_height );
#endif
			break;
			*/
		case 0:
			veejay_msg(VEEJAY_MSG_INFO, "Using output driver SDL");
#ifdef HAVE_SDL
			info->sdl[0] =
			    (vj_sdl *) vj_sdl_allocate( info->video_output_width,info->video_output_height,info->pixel_format, info->use_keyb, info->use_mouse,info->show_cursor);
			if( !info->sdl[0] )
				return -1;

			if( x != -1 && y != -1 )
				vj_sdl_set_geometry(info->sdl[0],x,y);

			title = veejay_title( info );

			if (!vj_sdl_init(info->settings->ncpu, info->sdl[0], info->bes_width, info->bes_height, title,1,info->settings->full_screen,el->video_fps))
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Error initializing SDL");
				free(title);
		    		return -1;
			}
			free(title);
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
			info->sdl[0] = 	(vj_sdl *) vj_sdl_allocate(info->video_output_width,info->video_output_height, info->pixel_format, info->use_keyb,info->use_mouse,info->show_cursor);
			if(!info->sdl[0])		
				return -1;
			
			title = veejay_title(info);	
			if (!vj_sdl_init(info->settings->ncpu, info->sdl[0], info->bes_width, info->bes_height,title,1,info->settings->full_screen, el->video_fps)) {
				free(title);
	   	 		return -1;
			}
			free(title);
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
			veejay_msg(VEEJAY_MSG_INFO, "Entering headless mode (no visual output)");
		break;

		case 4:
			veejay_msg(VEEJAY_MSG_INFO, "Entering Y4M streaming mode (420JPEG)");
			info->y4m = vj_yuv4mpeg_alloc( info->video_output_width,info->video_output_height,info->effect_frame1->fps, info->pixel_format );
			if( vj_yuv_stream_start_write( info->y4m, info->effect_frame1, info->y4m_file, Y4M_CHROMA_420JPEG ) == -1 ) {
				return -1;
			}	
			break;
		case 5:
			veejay_msg(VEEJAY_MSG_INFO, "Entering vloopback streaming mode. ");
			info->vloopback = vj_vloopback_open( info->y4m_file,
				el->video_norm == 'p' ? 1: 0, 1,
				info->video_output_width,
				info->video_output_height,
				info->pixel_format );
			if( info->vloopback == NULL ) {
				veejay_msg(0, "Cannot open %s as vloopback.",
					info->y4m_file);
				return -1;
			}
			if( vj_vloopback_start_pipe( info->vloopback ) <= 0 )
			{
				veejay_msg(0, "Unable to setup vloopback");
				vj_vloopback_close( info->vloopback );
				return -1;
				
			}

			break;

		case 6:
			veejay_msg(VEEJAY_MSG_INFO, "Entering Y4M streaming mode (422)");
			info->y4m = vj_yuv4mpeg_alloc( info->video_output_width,info->video_output_height,info->effect_frame1->fps, info->pixel_format );
			if( vj_yuv_stream_start_write( info->y4m, info->effect_frame1, info->y4m_file, Y4M_CHROMA_422 ) == -1 ) {
				return -1;
			}	
			break;

	default:
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode. Use -O [012345]");
		return -1;
	break;
    }

	if( gen_tags > 0 ) {
		int total  = 0;
		int *world = plug_find_all_generator_plugins( &total );
		if( total == 0 ) {
			veejay_msg(0,"No generator plugins found!");
			return -1;
		}
		int i;
		int plugrdy = 0;
		for ( i = 0; i < total; i ++ ) {
			int plugid = world[i];
			if( vj_tag_new( VJ_TAG_TYPE_GENERATOR, NULL,-1, el, info->pixel_format,
					plugid, 0, 0 ) > 0 )
				plugrdy++;
		}
		if( plugrdy > 0 ) {
			veejay_msg(VEEJAY_MSG_INFO, "Initialized %d generators.", plugrdy);
			info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
			info->uc->sample_id = ( gen_tags <= plugrdy ? gen_tags : 1 );
		} else {
			return -1;
		}
	} 
	
	if(def_tags && id <= 0)
	{
		char vidfile[1024];
		int default_chan = 1;
		char *chanid = getenv("VEEJAY_DEFAULT_CHANNEL");
		if(chanid != NULL )
			default_chan = atoi(chanid);
		else 
			veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_DEFAULT_CHANNEL=channel not set (defaulting to 1)");

		snprintf(vidfile,sizeof(vidfile),"/dev/video%d", (def_tags-1));
		int nid =	veejay_create_tag( info, VJ_TAG_TYPE_V4L, vidfile, info->nstreams, default_chan, (def_tags-1) );
		if( nid> 0)
		{
		 	   veejay_msg(VEEJAY_MSG_INFO, "Requested capture device available as stream %d", nid );
		}
		else
		{
			return -1;
		}
		info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
		info->uc->sample_id = nid;
	}
	else if( info->uc->file_as_sample && id <= 0)
	{
		long i,n=el->num_video_files;
		for(i = 0; i < n; i ++ )
		{
			long start=0,end=2;
			if(vj_el_get_file_entry( info->edit_list, &start,&end, i ))
			{
				editlist *sample_el = veejay_edit_copy_to_new(	info,info->edit_list,start,end );
				if(!el)
				{
					veejay_msg(0, "Unable to start from file, Abort");
					return -1;
				}
				sample_info *skel = sample_skeleton_new( 0, info->edit_list->total_frames );
				if(skel)
				{
					skel->edit_list = sample_el;
					sample_store(skel);
				}
			}	
		}
		info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
		info->uc->sample_id = 1;
	}
	else if(info->dummy->active && id <= 0)
	{
	 	int dummy_id;
		/* Use dummy mode, action file could have specified something */
		if( vj_tag_size()-1 <= 0 )
			dummy_id = vj_tag_new( VJ_TAG_TYPE_COLOR, "Solid", -1, el,info->pixel_format,-1,0,0);
		else
			dummy_id = vj_tag_size()-1;
		
		if( info->uc->sample_id <= 0 ) {
			info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
			info->uc->sample_id = dummy_id;
	
		}	
	}

	/* After we have fired up the audio and video threads system (which
     	* are assisted if we're installed setuid root, we want to set the
     	* effective user id to the real user id
    	 */

	if (seteuid(getuid()) < 0)
	{
		/* fixme: get rid of sys_errlist and use sys_strerror */
		veejay_msg(VEEJAY_MSG_ERROR, "Can't set effective user-id: %s", strerror(errno));
		return -1;
    	}

       	if (!veejay_mjpeg_set_playback_rate(info, info->dummy->fps, veejay_get_norm(info->dummy->norm) ))
	{
		return -1;
    	}

//	veejay_change_state( info, LAVPLAY_STATE_PLAYING );  

   	if(veejay_open(info) != 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize the threading system");
		return -1;   
      	}

	
    	return 0;
}

/*static	int	sched_ncpus() {
	return sysconf( _SC_NPROCESSORS_ONLN );
}*/

static	void	veejay_schedule_fifo(veejay_t *info, int pid )
{
	struct sched_param schp;
	veejay_memset( &schp, 0, sizeof(schp));
	schp.sched_priority = (int)( sched_get_priority_max( SCHED_FIFO ) * 0.85f);

	if( sched_setscheduler( pid, SCHED_FIFO, &schp ) != 0 )
	{
		if( info->audio ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Cannot set First-In-First-Out scheduling for process %d: %s",pid, strerror(errno));
		}
		else
			veejay_msg(VEEJAY_MSG_INFO, "Using default scheduling for process %d", pid );
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Using First-In-First-Out II scheduling for process %d", pid);
		veejay_msg(VEEJAY_MSG_DEBUG, "Priority is set to %d (RT)", schp.sched_priority );
	}
}
#include <bio2jack/bio2jack.h>
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
    struct timespec time_now;
	struct timespec time_last;
    double tdiff1=0.0, tdiff2=0.0;
    int first_free, skipv, skipa, skipi, nvcorr,frame;
    long ts, te;
    int n;
	const int spvf_c = (const int) (1000 * settings->spvf);
	int pace_warning = 0;
    veejay_set_instance( info );
    stats.tdiff = 0.0;
    stats.num_corrs_a = 0;
    stats.num_corrs_b = 0;
    stats.nsync = 0;
    stats.audio = 0;
    stats.norm = el->video_norm == 'n' ? 1 : 0;
    nvcorr = 0;
    stats.audio = 0;

    if(info->current_edit_list->has_audio && info->audio == AUDIO_PLAY)
    {
#ifdef HAVE_JACK
        info->audio_running = vj_perform_audio_start(info);
  		stats.audio = 1;
#endif
   }
    
	veejay_set_speed(info,1);

	switch(info->uc->playback_mode) {
		case VJ_PLAYBACK_MODE_PLAIN:
			info->current_edit_list = info->edit_list;
			video_playback_setup *settings = info->settings;
			settings->min_frame_num = 0;
			settings->max_frame_num = info->edit_list->total_frames;
			veejay_msg(VEEJAY_MSG_INFO, "Playing plain video, frames %d - %d",
				(int)settings->min_frame_num,  (int)settings->max_frame_num );
			settings->current_playback_speed = 1;
			break;
		case VJ_PLAYBACK_MODE_TAG:
			veejay_start_playing_stream(info,info->uc->sample_id);	
			veejay_msg(VEEJAY_MSG_INFO, "Playing stream %d", info->uc->sample_id);
			break;
		case VJ_PLAYBACK_MODE_PATTERN: //@ randomizer
			info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
		case VJ_PLAYBACK_MODE_SAMPLE:
			veejay_start_playing_sample(info, info->uc->sample_id);
			veejay_msg(VEEJAY_MSG_INFO, "Playing sample %d", info->uc->sample_id);
			break;
	}
    	if(info->load_action_file)
	{
		if(veejay_load_action_file(info, info->action_file[0] ))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Loaded configuration file %s", info->action_file[0] );
		} else {
			veejay_msg(VEEJAY_MSG_WARNING, "File %s is not an action file", info->action_file[0]);
		}
	}

	if(info->load_action_file ) {
	  if(sample_readFromFile( info->action_file[1],info->composite,info->seq,info->font,info->edit_list,
			 &(info->uc->sample_id), &(info->uc->playback_mode)  ))
	   {
			veejay_msg(VEEJAY_MSG_INFO, "Loaded samplelist %s", info->action_file[1]);
	    }
	}


    vj_perform_queue_video_frame(info,0);
    vj_perform_queue_audio_frame(info);
     
    if (vj_perform_queue_frame(info, 0) != 0) {
	   veejay_msg(VEEJAY_MSG_ERROR,"Unable to queue frame");
           return;
    }

    veejay_msg(VEEJAY_MSG_DEBUG, "Output dimensions: %dx%d, backend scaler: %dx%d",
		info->video_output_width,info->video_output_height,info->bes_width,info->bes_height );

    for(n = 0; n < QUEUE_LEN ; n ++ ) {
        veejay_mjpeg_queue_buf(info, n,1 );
    }

    stats.nqueue = QUEUE_LEN;
    settings->spas = 1.0 / (double) el->audio_rate;
    veejay_msg(VEEJAY_MSG_DEBUG, "Output 1.0/%2.2f seconds per video frame: %4.4f",settings->output_fps,1.0 / settings->spvf);


    while (settings->state != LAVPLAY_STATE_STOP) {
		int current_speed = settings->current_playback_speed;
	 	tdiff1 = 0.;
   		tdiff2 = 0.;

		first_free = stats.nsync;

		do {
	    		if (settings->state == LAVPLAY_STATE_STOP) {
				goto FINISH;
			}

			if (!veejay_mjpeg_sync_buf(info, &bs)) {
				veejay_change_state_save(info, LAVPLAY_STATE_STOP);
				goto FINISH;
	    		}

			veejay_event_handle(info);
	   		
			frame = bs.frame;

	   		stats.nsync++;
	   		clock_gettime( CLOCK_REALTIME, &time_now);

	  		long  d1 = (time_now.tv_sec * 1000000000) + time_now.tv_nsec;
	   		long  n1 = (bs.timestamp.tv_sec * 1000000000) +  bs.timestamp.tv_nsec;

	   		double  dn = ( (double) (d1 - n1) )/10000000.0; // * 1.e7;

	   		stats.tdiff = dn; 
	
		} 
		while (stats.tdiff > settings->spvf && (stats.nsync - first_free) < (QUEUE_LEN-1));
	
	//	veejay_event_handle(info);

#ifdef HAVE_JACK
		if ( info->audio==AUDIO_PLAY  ) 
		{
			struct timespec audio_tmstmp;
	   		long num_audio_bytes_written = vj_jack_get_status( &(audio_tmstmp.tv_sec),&(audio_tmstmp.tv_nsec));
	   		if( audio_tmstmp.tv_sec ) {
				tdiff1 = settings->spvf * (stats.nsync - nvcorr) - settings->spas * num_audio_bytes_written;
				tdiff2 = (bs.timestamp.tv_sec - audio_tmstmp.tv_sec) + (bs.timestamp.tv_nsec - audio_tmstmp.tv_nsec) * 1.e-9;
			}
		}
#endif
		stats.tdiff = (tdiff1 - tdiff2);
	
	/* Fill and queue free buffers again */
	for (n = first_free; n < stats.nsync;) {
		/* Audio/Video sync correction */
		skipv = 0;
		skipa = 0;
	   	skipi = 0;
		if (info->sync_correction) {
			if (stats.tdiff > settings->spvf) {
				skipa = 1; 
		    		if (info->sync_ins_frames && current_speed != 0) {
						skipi = 1;
				   	}
					nvcorr++;
		   			stats.num_corrs_a++;
		    		stats.tdiff -= settings->spvf;
			} 

			if (stats.tdiff < -settings->spvf) {
		    		/* Video is behind audio */
		    		skipv = 1;
   		    		if (!info->sync_skip_frames && current_speed != 0) {
						skipi = 1;
 		   			} 
		    		nvcorr--;
		    		stats.num_corrs_b++;
		   			stats.tdiff += settings->spvf;
			}
	    }
	  	
	    frame  = n % QUEUE_LEN;
#ifdef HAVE_SDL
	    ts= SDL_GetTicks();
#endif
		if( info->pause_render ) {
			int hti = (settings->current_playback_speed == 0 ? 0 : 1);
			if( hti == 0 ) 
					hti = info->sfd ? 1: 0;
			if( hti ) {
				settings->buffer_entry[frame] = (settings->buffer_entry[frame]+1)%2; //@!
			} else {
				settings->buffer_entry[frame] = settings->current_frame_num;
			} 
		} else {
			settings->buffer_entry[frame] = (settings->buffer_entry[frame] + 1 ) % 2;
		}


	    if( settings->state != LAVPLAY_STATE_PAUSED ) {

		  if (!skipa) 
			vj_perform_queue_audio_frame(info);

		  if (!skipv)
			vj_perform_queue_video_frame(info,skipi);
		
		  if(!skipi)	
		 	  vj_perform_queue_frame(info,skipi);

		  if(!skipa)
		  	vj_perform_record_video_frame(info);

	     } 
#ifdef HAVE_SDL	
	    te = SDL_GetTicks();
		info->real_fps = (int)( te - ts );
#else
	    info->real_fps = 0;
#endif
		//@ add pace correction
		if( info->audio == AUDIO_PLAY ){
			info->real_fps += settings->pace_correction;
		}

	    if( (fabs(stats.tdiff) > settings->spvf || info->real_fps > spvf_c) && info->real_fps && info->audio == AUDIO_PLAY) {
			if( pace_warning == 0 ) {
				veejay_msg(VEEJAY_MSG_WARNING, "Can't keep pace with audio! Rendering audio/video frame takes too long (measured %-4ld ms, out of sync by %-2.4f ms)",info->real_fps,
					(spvf_c - info->real_fps));
			}
			pace_warning = (pace_warning + 1) % spvf_c;
			if(!settings->auto_mute)
				settings->auto_mute = 1;
		} else {
			if( info->audio == AUDIO_PLAY && settings->auto_mute ) {
				veejay_msg(VEEJAY_MSG_WARNING, "Back in pace with audio, audio is now unmuted." );
				settings->auto_mute = 0;
			}
		}

		if( skipv ) { 
			continue;
		} 

	    veejay_mjpeg_queue_buf(info,frame, 1 );
	
	    stats.nqueue ++;
	    n++;

		break;
	}
		/* output statistics */
	if (el->has_audio && (info->audio==AUDIO_PLAY))
	    stats.audio = settings->audio_mute ? 0 : 1;
    }

  FINISH:

    /* All buffers are queued, sync on the outstanding buffers
     * Never try to sync on the last buffer, it is a hostage of
     * the codec since it is played over and over again
     */
    if (info->audio_running || info->audio == AUDIO_PLAY)
		vj_perform_audio_stop(info);
}

/******************************************************
 * veejay_playback_thread()
 *   The main playback thread
 ******************************************************/

static void Welcome(veejay_t *info)
{
	veejay_msg(VEEJAY_MSG_WARNING, "Video project settings: %ldx%ld, Norm: [%s], fps [%2.2f], %s",
			info->video_output_width,
			info->video_output_height,
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
  
	if(info->settings->composite )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Software composite - projection screen is %d x %d",
			info->video_output_width, info->video_output_height );
	}

	veejay_msg(VEEJAY_MSG_INFO,"Type 'man veejay' in a shell to learn more about veejay");
	veejay_msg(VEEJAY_MSG_INFO,"For a list of events, type 'veejay -u |less' in a shell");
	veejay_msg(VEEJAY_MSG_INFO,"Use 'reloaded' to enter interactive mode");
	veejay_msg(VEEJAY_MSG_INFO,"Alternatives are OSC applications or 'sendVIMS' extension for PD"); 

	int k = verify_working_dir();
	if( k > 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING,
			"Found %d veejay project files in current working directory (.edl,.sl, .cfg,.avi).",k);
		veejay_msg(VEEJAY_MSG_WARNING,
			"If you want to start a new project, start veejay in an empty directory");
	}
}
static	void *veejay_playback_thread(void *data)
{
    veejay_t *info = (veejay_t *) data;
    int i;
	sigset_t mask;
	struct sigaction act;
	sigemptyset(&mask);
	sigaddset( &mask, SIGPIPE );
	act.sa_handler = donothing2;
	act.sa_flags = SA_SIGINFO | SA_ONESHOT;
	sigemptyset(&act.sa_mask);


    pthread_sigmask( SIG_BLOCK, &mask, NULL );

    veejay_schedule_fifo( info, getpid());

    Welcome(info);
    veejay_playback_cycle(info);
    veejay_close(info);
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
    if( info->y4m ) {
	    vj_yuv_stream_stop_write( info->y4m );
	    vj_yuv4mpeg_free(info->y4m );
	    info->y4m = NULL;
	   }
	if( info->vloopback ) {
		vj_vloopback_close( info->vloopback );
		info->vloopback = NULL;

	}
/*
#ifdef HAVE_GL
#ifndef X_DISPLAY_MISSING
	if( info->video_out == 3 )
	{
		x11_enable_screensaver( x_get_display(info->gl) );
		x_display_close( info->gl );
    	}
#endif
#endif
*/


#ifdef HAVE_FREETYPE
	vj_font_destroy( info->font );
	vj_font_destroy( info->osd );
#endif

	if( info->shm ) {
		vj_shm_stop(info->shm);
		vj_shm_free(info->shm);
	}

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

	size_t recv_len   = (info->edit_list->video_width * info->edit_list->video_height * 3); // max
	size_t status_len = 4096 * 16; //@ 16x 4kb 

	info->vjs[VEEJAY_PORT_CMD] = vj_server_alloc(info->uc->port, NULL, V_CMD, recv_len);
	if(!info->vjs[VEEJAY_PORT_CMD]) {
		return 0;
	}

	info->vjs[VEEJAY_PORT_STA] = vj_server_alloc(info->uc->port, NULL, V_STATUS, status_len);
	if(!info->vjs[VEEJAY_PORT_STA])
	{
		vj_server_shutdown(info->vjs[VEEJAY_PORT_CMD]);
		return 0;
	}
	//@ second VIMS control port
	info->vjs[VEEJAY_PORT_DAT] = vj_server_alloc(info->uc->port + 5, NULL, V_CMD, recv_len);
	if(!info->vjs[VEEJAY_PORT_DAT]) {
		vj_server_shutdown(info->vjs[VEEJAY_PORT_CMD]);
		vj_server_shutdown(info->vjs[VEEJAY_PORT_STA]);
		return 0;
	}

	info->vjs[VEEJAY_PORT_MAT] = NULL;
	if( info->settings->use_vims_mcast ) //FIXME mcast
	{
		info->vjs[VEEJAY_PORT_MAT] =
			vj_server_alloc(info->uc->port, info->settings->vims_group_name, V_CMD, recv_len );
		if(!info->vjs[VEEJAY_PORT_MAT])
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize mcast sender");
			return 0;
		}
	}
	if(info->settings->use_mcast)
	{
		GoMultiCast( info->settings->group_name );
	}

	info->osc = (void*) vj_osc_allocate(info->uc->port+6);

    if(!info->osc) 
	{
		veejay_msg(VEEJAY_MSG_ERROR,  "Unable to start OSC server at port %d", info->uc->port + 6 );
		vj_server_shutdown(info->vjs[VEEJAY_PORT_CMD]);
		vj_server_shutdown(info->vjs[VEEJAY_PORT_STA]);
		vj_server_shutdown(info->vjs[VEEJAY_PORT_DAT]);
		return 0;
	}

	if( info->settings->use_mcast )
		veejay_msg(VEEJAY_MSG_INFO, "UDP multicast OSC channel ready at port %d (group '%s')",
			info->uc->port + 6, info->settings->group_name );
	else
		veejay_msg(VEEJAY_MSG_INFO, "UDP unicast OSC channel ready at port %d",
			info->uc->port + 6 );

	if(vj_osc_setup_addr_space(info->osc) == 0)
		veejay_msg(VEEJAY_MSG_DEBUG, "Initialized OSC (http://www.cnmat.berkeley.edu/OpenSoundControl/)");

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
	double p = (double) perc * 0.01f;
	long max_memory = (p * total);
	long mmap_memory = (long) (0.005f * (float) total);

	char *user_defined_mmap = getenv( "VEEJAY_MMAP_PER_FILE" );
	if( user_defined_mmap ) {
		max_memory = atoi( user_defined_mmap ) * 1024;
		veejay_msg(VEEJAY_MSG_DEBUG, "User-defined %2.2f Kb mmap size per AVI file",(float) (max_memory / 1024.0f));
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "You can define mmap size per AVI file with VEEJAY_MMAP_PER_FILE=Kb");
	}

	max_memory -= mmap_memory;
	max_memory -= (vj_perform_fx_chain_size()/1024);

	if( perc > 0 && max_memory <= 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Please enter a larger value for -m");
		veejay_msg(VEEJAY_MSG_ERROR, "Need a minimum of %ld MB RAM to run if -M is not specified", vj_perform_fx_chain_size()/(1024*1024));
		veejay_msg(VEEJAY_MSG_WARNING, "Memory frame cache disabled");
		return 1;
	}

	if( n_slots <= 0)
		n_slots = 1;

	int chunk_size = (int) (max_memory <= 0 ? 0: max_memory / n_slots ); 

	chunk_size_ = chunk_size;
	n_cache_slots_ = n_slots;

	vj_el_set_mmap_size( mmap_memory );	

	total_mem_mb_ = total / 1024;
	if(chunk_size > 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO, "%d Kb total system RAM , Consuming up to %2.2f Mb",total, (float)max_memory / 1024.0f );
		veejay_msg(VEEJAY_MSG_INFO, "Cache line size is %d Kb (%2.2f Mb) per sample",chunk_size, (float) chunk_size/1024.0);
		vj_el_init_chunk( chunk_size );
	}
	else {
		veejay_msg(VEEJAY_MSG_INFO, "Memory frame cache disabled");
	}
	veejay_msg(VEEJAY_MSG_INFO, "Memory cache size per EDL is %2.2f Mb",(float) mmap_memory / 1024.0f);

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
    info->settings->sample_restart = 1; //@ default to on
    veejay_memset( &(info->settings->action_scheduler), 0, sizeof(vj_schedule_t));
    veejay_memset( &(info->settings->viewport ), 0, sizeof(VJRectangle)); 

    info->status_what = (char*) vj_calloc(sizeof(char) * MESSAGE_SIZE );

	info->uc = (user_control *) vj_calloc(sizeof(user_control));
    if (!(info->uc)) 
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
	
	info->seq->samples = (int*) vj_calloc(sizeof(int) * (MAX_SEQUENCES+1) ); //@ SL contains 100 sequence items
	
    info->audio = AUDIO_PLAY;
    info->continuous = 1;
    info->sync_correction = 1;
    info->sync_ins_frames = 1;
    info->sync_skip_frames = 0;
    info->double_factor = 1;
    info->no_bezerk = 1;
    info->nstreams = 1;
    info->stream_outformat = -1;
    info->rlinks = (int*) vj_calloc(sizeof(int) * VJ_MAX_CONNECTIONS );
    info->rmodes = (int*) vj_calloc(sizeof(int) * VJ_MAX_CONNECTIONS );
    info->settings->currently_processed_entry = -1;
    info->settings->first_frame = 1;
    info->settings->state = LAVPLAY_STATE_PLAYING;
    info->settings->composite = 1;
    info->uc->playback_mode = VJ_PLAYBACK_MODE_PLAIN;
    info->uc->use_timer = 2;
    info->uc->sample_key = 1;
    info->uc->direction = 1;	/* pause */
    info->uc->sample_start = 0;
    info->uc->sample_end = 0;
	info->uc->ram_chain = 1; /* enable, keep FX chain buffers in memory (reduces the number of malloc/free of frame buffers) */
	info->net = 1;
	info->status_line = (char*) vj_calloc(sizeof(char) * MESSAGE_SIZE );
	info->status_line_len = 0;
    for( i =0; i < VJ_MAX_CONNECTIONS ; i ++ ) {
		info->rlinks[i] = -1;
		info->rmodes[i] = -1;
	}

    veejay_memset(info->action_file[0],0,sizeof(info->action_file[0])); 
    veejay_memset(info->action_file[1],0,sizeof(info->action_file[1])); 

#ifdef HAVE_SDL
    info->video_out = 0;
#else
#ifdef HAVE_DIRECTFB
    info->video_out = 1;
#else
    info->video_out = 3;
#endif
#endif


#ifdef HAVE_SDL
	info->sdl = (vj_sdl**) vj_calloc(sizeof(vj_sdl*) * MAX_SDL_OUT ); 
#endif

	info->pixel_format = FMT_422F; //@default 
	info->settings->ncpu = smp_check();

	int status = 0;
	int acj    = 0;
	char *interpolate_chroma = getenv("VEEJAY_INTERPOLATE_CHROMA");
	if( interpolate_chroma ) {
		sscanf( interpolate_chroma, "%d", &status );
		}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_INTERPOLATE_CHROMA=[0|1] not set");
	}

	char *auto_ccir_jpeg = getenv("VEEJAY_AUTO_SCALE_PIXELS");
	if( auto_ccir_jpeg ) {
		sscanf( auto_ccir_jpeg, "%d", &acj );
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_AUTO_SCALE_PIXELS=[0|1] not set");
	}

	char *key_repeat_interval = getenv("VEEJAY_SDL_KEY_REPEAT_INTERVAL");
	char *key_repeat_delay    = getenv("VEEJAY_SDL_KEY_REPEAT_DELAY");
	if(key_repeat_interval) {
		sscanf( key_repeat_interval, "%d", &(info->settings->repeat_interval));
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_SDL_KEY_REPEAT_INTERVAL=[Num] not set");
	}
	if( key_repeat_delay) {
		sscanf( key_repeat_delay, "%d", &(info->settings->repeat_delay));
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_SDL_KEY_REPEAT_DELAY=[Num] not set");
	}

	char *best_performance = getenv( "VEEJAY_PERFORMANCE");
	int default_zoomer = 1;
	if(!best_performance) {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_PERFORMANCE=[fastest|quality] not set");
	}

	char *max_cache = getenv( "VEEJAY_PLAYBACK_CACHE");
	if( max_cache ) {
		long mb = 0;
		if( sscanf( max_cache,"%ld",&mb ) )  {
			veejay_msg(VEEJAY_MSG_WARNING, "Maximum memory for sample cache is %ld Mb, per sample %ld", mb );
			vj_el_set_caching(1);
			vj_el_init_chunk( (mb * 1024) / 4 );
		}
		if( mb == 0 )
			info->no_caching = 0;
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_PLAYBACK_CACHE=[megabytes] not set");
	}

	char *sdlfs = getenv("VEEJAY_FULLSCREEN");
	if( sdlfs ) {
		int val = 0;
		if( sscanf( sdlfs, "%d", &val ) ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Playing in %s mode",
				(val== 1 ? "fullscreen" : "windowed" ) );
			info->settings->full_screen = val;
		}
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_FULLSCREEN=[0|1] not set");
	}

	info->pause_render = 1;// old behaviour was always to pause everything on speed=0
	char *pausefl = getenv( "VEEJAY_PAUSE_EVERYTHING" );
	if( pausefl ) {
		int val = 0;
		if( sscanf( pausefl, "%d", &val) ) {
			veejay_msg(VEEJAY_MSG_WARNING,
					"Playback engine will %s",
					(val == 0 ? "only stop top sample on pause" :
					 		    "stop rendering on pause" ) );
			info->pause_render = val;
		}
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_PAUSE_EVERYTHING=[0|1] (continues rendering is ON)");
	}

	if( best_performance) {
		if (strncasecmp( best_performance, "quality", 7 ) == 0 ) {
			best_performance_ = 1;
			default_zoomer = 2;
			status = 1;
			veejay_msg(VEEJAY_MSG_WARNING, "Performance set to maximum quality");
		}
		else if( strncasecmp( best_performance, "fastest", 7) == 0 ) {
			best_performance_ = 0;
			veejay_msg(VEEJAY_MSG_WARNING, "Performance set to maximum speed");
			if( acj ) {
				veejay_msg(VEEJAY_MSG_WARNING, "\tdisabling flag VEEJAY_AUTO_SCALE_PIXELS");
				acj = 0;
			}
			if( status ) {
				veejay_msg(VEEJAY_MSG_WARNING, "\tdisabling flag VEEJAY_INTERPOLATE_CHROMA");
				status = 0;
			}
			default_zoomer = 1;
		}
	}

	yuv_init_lib( status ,acj, default_zoomer);

	if(!vj_avcodec_init( info->pixel_format, info->verbose))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize encoders!");
		return 0;
	}
	

	
    return info;
}




/******************************************************
 * veejay_main()
 *   the whole video-playback cycle
 * return value: 1 on succes, 0 on error
 ******************************************************/

int veejay_main(veejay_t * info)
{
	video_playback_setup *settings = (video_playback_setup *) info->settings;
	pthread_attr_t attr;	
	cpu_set_t cpuset;

	memset( &attr, 0 , sizeof(pthread_attr_t));

    	/* Flush the Linux File buffers to disk */
    	//sync();
   
	if( task_num_cpus() > 1 ) {
		CPU_ZERO( &cpuset );
		CPU_SET ( 0, &cpuset ); /* run on the first cpu */

		int err = pthread_attr_init( &attr );
		if( err == ENOMEM ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Out of memory error.");
			return 0;

		}
		if( pthread_attr_setaffinity_np( &attr, sizeof(cpuset) , &cpuset ) != 0 ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Unable to pin playback timer to cpu #1");
		}
		else {
			veejay_msg(VEEJAY_MSG_INFO,"Pinned playback timer thread to cpu #1");
		}
	}

        int err = pthread_create(&(settings->playback_thread),&attr,
		       veejay_playback_thread, (void *) info);
    	if( err != 0 ) {
		switch( err ) {
		 case EAGAIN:
			veejay_msg(VEEJAY_MSG_ERROR, "Insufficient resources to create playback timer thread.");
			break;
	  	default:
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to create playback timer thread, code %d ", err);
    		}
        	return 0;
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
	uint64_t n1 = (uint64_t) start;
	uint64_t n2 = (uint64_t) end;

	long len = end - start + 1;
  
	if(el->is_empty)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No frames in EDL to copy");
		return 0;
	}

	if( n2 > el->total_frames)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample end is outside of editlist");
		return NULL;
	}

	if(len < 1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample too short!");
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

	veejay_memcpy( new_el->frame_list , el->frame_list + n1, sizeof(uint64_t) * len );
	new_el->video_frames = len;
	new_el->total_frames = len - 1;
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

	if (n2 < n1 || n1 > el->total_frames || n2 > el->total_frames )
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
	el->total_frames -= (n2 - n1 + 1);
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
		if (destination < 0 || destination > el->total_frames)
		{
			if(destination < 0)
				veejay_msg(VEEJAY_MSG_ERROR, 
					    "Destination cannot be negative");
			if(destination > el->total_frames)
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
    	for (i = el->total_frames; i >= destination && i > 0; i--)
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

	el->total_frames += settings->save_list_len -1;

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
	
    if (destination > el->total_frames || destination < 0
		|| start < 0 || end < 0 || start >= el->total_frames
		|| end > el->total_frames || end < start)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid parameters for moving video from %ld - %ld to position %ld",
			start,end,destination);
		veejay_msg(VEEJAY_MSG_ERROR, "Range is 0 - %ld", el->total_frames);   
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
			veejay_msg(0, "Sample %d is a picture ...", id ); //FIXME check if this can be removed
			return -1;
		}

		sample_edl = sample_get_editlist( id );
	}

	// if both, append it to sample's edit list 
	if(sample_edl && sample)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Adding video file to existing sample %d", id );
		long endpos = sample_get_endFrame( id );
		
		int res = veejay_edit_addmovie( info, sample_edl, movie, endpos );

		if(files[0]) 
			free(files[0]);

		if( res > 0 ) {
			sample_set_endframe( id, sample_edl->total_frames );
			veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d new ending position is %ld",id, sample_edl->video_frames );
			return id;
		}
		return -1;
	}

	// create initial edit list for sample (is currently playing)
	if(!sample_edl) 
		sample_edl = vj_el_init_with_args( files,1,info->preserve_pathnames,info->auto_deinterlace,0,
				info->edit_list->video_norm , info->pixel_format, info->video_output_width, info->video_output_height);
	// if that fails, bye
	if(!sample_edl)
	{
		veejay_msg(0, "Error while creating EDL");
		if(files[0]) free(files[0]);

		return -1;
	}

	// check audio properties 
	if( info->edit_list->has_audio && info->audio == AUDIO_PLAY ) {
		if( sample_edl->audio_rate != info->edit_list->audio_rate ||
		    sample_edl->audio_chans != info->edit_list->audio_chans ||
		    sample_edl->audio_bits != info->edit_list->audio_bits ||
		    sample_edl->audio_bps != info->edit_list->audio_bps ) {

			veejay_msg(VEEJAY_MSG_WARNING, "Silencing this sample (mismatching audio properties)" );
			sample_edl->has_audio = 0;
			sample_edl->audio_chans = 0;
			sample_edl->audio_rate = 0;
			sample_edl->audio_bps = 0;
			sample_edl->audio_bits = 0;
		}
	}


	// the sample is not there yet,create it
	if(!sample)
	{
		sample = sample_skeleton_new( 0, sample_edl->total_frames );
		if(sample)
		{
			sample->edit_list = sample_edl;
			sample_store(sample);
			sample->speed = info->settings->current_playback_speed;
			veejay_msg(VEEJAY_MSG_INFO,"Created new sample %d from file %s",sample->sample_id,	files[0]);
		}
		else {
			veejay_msg(VEEJAY_MSG_ERROR,"Failed to create new sample from file '%s'", files[0]);
			return -1;
		}
	}

	// free temporary values
   	if(files[0]) free(files[0]);


    return sample->sample_id;
}

int veejay_edit_addmovie(veejay_t * info, editlist *el, char *movie, long start )
{
	video_playback_setup *settings =
		(video_playback_setup *) info->settings;
	uint64_t n, i;
	uint64_t c = el->video_frames;
	long end   = c;
	if( el->is_empty )
		c -= 2;

	n = el->num_video_files;

	int res = open_video_file(movie, el, info->preserve_pathnames, info->auto_deinterlace,1,
		info->edit_list->video_norm, info->pixel_format, info->video_output_width, info->video_output_height );

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
	settings->max_frame_num = el->total_frames;
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
	info->audio = NO_AUDIO;
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
	editlist *e = info->edit_list;
	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ) {
		e = info->current_edit_list;
	}
	if( e->num_video_files <= 0 )
		return 0;
		
	if(n1 == 0 && n2 == 0 )
		n2 = e->total_frames;

	if( vj_el_write_editlist( filename, n1,n2, e ) )
		veejay_msg(VEEJAY_MSG_INFO, "Saved EDL to file %s", filename);
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error while saving EDL to %s", filename);
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
static int	veejay_open_video_files(veejay_t *info, char **files, int num_files, int force , char override_norm)
{
	if(num_files<=0 || files == NULL)
	{
		info->dummy->active = 1;
	}

	if( info->dummy->active )
	{
		info->plain_editlist = vj_el_dummy( 0, 
				info->auto_deinterlace,
				info->dummy->chroma,
				info->dummy->norm,
				info->dummy->width,
				info->dummy->height,
				info->dummy->fps,
				info->pixel_format 
				);

		if( info->dummy->arate )
		{
			editlist *el = info->plain_editlist;
			el->has_audio = 1;
			el->audio_rate = info->dummy->arate;
			el->audio_chans = 2;
			el->audio_bits = 16;
			el->audio_bps = 4;
			veejay_msg(VEEJAY_MSG_DEBUG, "Dummy Audio: %f KHz, %d channels, %d bps, %d bit audio",
				(float)el->audio_rate/1000.0,el->audio_chans,el->audio_bps,el->audio_bits);
		}
		
		veejay_msg(VEEJAY_MSG_DEBUG,"Dummy Video: %dx%d, chroma %x, framerate %2.2f, norm %s",
					info->dummy->width,info->dummy->height, info->dummy->chroma,info->dummy->fps,
					(info->dummy->norm == 'n' ? "NTSC" :"PAL"));

		info->video_output_width = info->dummy->width;
		info->video_output_height = info->dummy->height;
	}
	else
	{
		int tmp_wid = info->video_output_width;
		int tmp_hei = info->video_output_height;

	    info->plain_editlist = 
			vj_el_init_with_args(
					files,
					num_files,
					info->preserve_pathnames,
					info->auto_deinterlace,
				    force,
					override_norm,
					info->pixel_format, tmp_wid, tmp_hei);
		if(!info->plain_editlist ) 
			return 0;
	}	
	info->edit_list = info->plain_editlist;
	//@ set current
	info->current_edit_list = info->edit_list;

	info->effect_frame_info->width = info->video_output_width;
	info->effect_frame_info->height= info->video_output_height;

	return 1;
}


static int configure_dummy_defaults(veejay_t *info, char override_norm, float fps, char **files, int n_files)
{
	int default_dw = 720;
	int default_dh = (override_norm == 'n' ? 480 : 576);
	int default_norm = (override_norm == '\0' ? VIDEO_MODE_PAL : veejay_get_norm( override_norm ) );
	float default_fps = vj_el_get_default_framerate(default_norm);
	
	if( has_env_setting( "VEEJAY_RUN_MODE", "CLASSIC" ) ) {
	       default_dw = (default_norm == VIDEO_MODE_PAL || default_norm == VIDEO_MODE_SECAM ? 352 : 360 );
	       default_dh = (default_norm == VIDEO_MODE_PAL || default_norm == VIDEO_MODE_SECAM ? 288 : 240 );
	}

	int dw = default_dw;
	int dh = default_dh;

	float dfps = (fps <= 0.0f ? default_fps : fps );
	float tmp_fps = 0.0f;
	long tmp_arate = 0;

	if( n_files > 0  ) {
		int in_w = 0, in_h = 0;

		vj_el_scan_video_file( files[0], &in_w, &in_h, &tmp_fps, &tmp_arate );

		if( in_w <= 0 || in_h <= 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to determine video properties" );
			return 0;
		}

		if(info->video_output_width<=0)
			dw = in_w;
		if(info->video_output_height<=0)
			dh = in_h;
		if( tmp_fps > 0.0f && ( dfps <= 0 || dfps == default_fps) ) 
			dfps = tmp_fps;

		if( dw == default_dw && in_w > 0 )
			dw = in_w;
		if( dh == default_dh && in_h > 0 )
			dh = in_h;

		default_norm = (override_norm == '\0' ? veejay_get_norm(vj_el_get_default_norm(tmp_fps)) : veejay_get_norm(override_norm));

		veejay_msg(VEEJAY_MSG_DEBUG, "Video source is: %dx%d %2.2f fps norm %d",in_w,in_h,tmp_fps, default_norm);
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "Dummy source is: %dx%d %2.2f fps norm %d",dw,dh,dfps, default_norm );
	}
	   
	if( info->video_output_width <= 0 ) 
		info->video_output_width = dw;
	else
		dw = info->video_output_width;

	if( info->video_output_height <= 0 ) 
		info->video_output_height = dh;
	else
		dh = info->video_output_height;
	
	
	if( override_norm != '\0' ) {
		info->dummy->norm = override_norm;
	} else {
		info->dummy->norm = vj_el_get_default_norm( dfps );
	}


	if( info->dummy->width <= 0 )
		info->dummy->width  = dw;
	if( info->dummy->height <= 0)
		info->dummy->height = dh;
	if( info->dummy->fps <= 0.0f)
		info->dummy->fps = dfps;
	
	info->dummy->chroma = get_chroma_from_pixfmt( vj_to_pixfmt( info->pixel_format ) );
	info->settings->output_fps = dfps;
	
	if( info->audio ) {

		if( tmp_arate > 0 && info->audio == AUDIO_PLAY ) {
			/* just warn if user customizes framerate */
			veejay_msg(VEEJAY_MSG_WARNING, "Going to run with user specified FPS. This will affect audio playback");
		}

		if( tmp_arate == 0 && info->audio == AUDIO_PLAY ) 
			tmp_arate = 48000;

		if( info->dummy->arate <= 0)
			info->dummy->arate = tmp_arate;

		if( fps > 0.0f ) {
			veejay_msg(VEEJAY_MSG_WARNING,"Going to run with audio but user defined frames per second ...");
		}
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "Video output is %dx%d pixels, %2.2f fps", info->video_output_width, info->video_output_height, dfps );
	return 1;
}

int veejay_open_files(veejay_t * info, char **files, int num_files, float ofps, int force,int force_pix_fmt, char override_norm, int switch_jpeg)
{
	int ret = 0;

	switch( force_pix_fmt ) {
			case 1: info->pixel_format = FMT_422;break;
			case 2: info->pixel_format = FMT_422F;break;
			default:
				break;
	}

	char text[24];
	switch(info->pixel_format) {
		case FMT_422:
			sprintf(text, "4:2:2 [16-235][16-240]");
			break;
		case FMT_422F:	
			sprintf(text, "4:2:2 [0-255]");
			break;
		default:
			veejay_msg(VEEJAY_MSG_ERROR, "Unsupported pixel format selected"); 
			return 0;
	}

	if(force_pix_fmt > 0 ) {
	  	veejay_msg(VEEJAY_MSG_WARNING , "Output pixel format set to %s by user", text );
	}
	else
		veejay_msg(VEEJAY_MSG_DEBUG, "Processing set to YUV %s", text );

	if(!configure_dummy_defaults(info,override_norm, ofps,files,num_files))
		return 0;

	vj_el_init( info->pixel_format, switch_jpeg, info->dummy->width,info->dummy->height, info->dummy->fps );
#ifdef USE_GDK_PIXBUF
	vj_picture_init( &(info->settings->sws_templ));
#endif

	info->effect_frame1 = yuv_yuv_template( NULL,NULL,NULL, info->dummy->width, info->dummy->height, vj_to_pixfmt(info->pixel_format) );
	info->effect_frame1->fps = info->settings->output_fps;
	info->effect_frame2 = yuv_yuv_template( NULL,NULL,NULL, info->dummy->width, info->dummy->height, vj_to_pixfmt(info->pixel_format) );
	info->effect_frame2->fps = info->settings->output_fps;

	if(num_files == 0)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Trying to start without video");
		ret = veejay_open_video_files( info, NULL, 0 , force, override_norm );
	}
	else
	{
		ret = veejay_open_video_files( info, files, num_files, force, override_norm );
	}

	return ret;
}

