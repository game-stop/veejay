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
#include <time.h>
#include "jpegutils.h"
#include "vj-event.h"
#include <libstream/vj-shm.h>
#ifndef X_DISPLAY_MISSING
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#include <libvjnet/vj-client.h>
#include <veejay/vj-misc.h>
#ifdef HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#endif
#define VIDEO_MODE_PAL		0
#define VIDEO_MODE_NTSC		1
#define VIDEO_MODE_SECAM	2
#define VIDEO_MODE_AUTO		3

#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <libvje/vje.h>
#include <veejay/vj-perform.h>
#include <veejay/vj-plugin.h>
#include <veejay/vj-plug.h>
#include <veejay/vj-lib.h>
#include <libel/vj-avcodec.h>
#include <libyuv/yuvconv.h>
#include <veejay/vj-OSC.h>

#include <veejay/vims.h>
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
#ifdef HAVE_SDL
#include <SDL/SDL.h>
#define MAX_SDL_OUT	2
#endif
#include <mpegconsts.h>
#include <mpegtimecode.h>
//#include "vj-common.h"
#include <libstream/vj-tag.h>
#include "libveejay.h"
#include <utils/mjpeg_types.h>
#include "vj-perform.h"
#include <libvjnet/vj-server.h>
#include "mjpeg_types.h"
//#include "lav_common.h"
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
#ifdef HAVE_SDL
extern void vj_event_single_fire(void *ptr, SDL_Event event, int pressed);
#endif

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

void veejay_set_sampling(veejay_t *info, subsample_mode_t m)
{
	video_playback_setup *settings = (video_playback_setup*) info->settings;
        if(m == SSM_420_JPEG_TR )
	{
veejay_msg(VEEJAY_MSG_ERROR,"Pixel format is %d", info->pixel_format);
		if(info->pixel_format == FMT_420)
			settings->sample_mode = SSM_420_JPEG_TR;
		else
			settings->sample_mode = SSM_422_444;
	}
	else
	{
		if(info->pixel_format == FMT_420)
			settings->sample_mode = SSM_420_JPEG_BOX;
		else
			settings->sample_mode = SSM_422_444;
	}
	switch(settings->sample_mode)
	{
		case SSM_420_JPEG_BOX:
		veejay_msg(VEEJAY_MSG_WARNING, "Using box filter for 4:2:0 -> 4:4:4");
		break;
		case SSM_420_JPEG_TR:
		veejay_msg(VEEJAY_MSG_WARNING, "Using triangle filter for 4:2:0 -> 4:4:4");
		break;
		case SSM_422_444:
		veejay_msg(VEEJAY_MSG_WARNING, "Using box filter for 4:2:2 -> 4:4:4");
		break;
		case SSM_UNKNOWN:
		case SSM_420_MPEG2:
		case SSM_420_422:
		case SSM_COUNT:
		veejay_msg(VEEJAY_MSG_WARNING, "Bogus sample mode");
		break;
	}
}

int veejay_set_framedup(veejay_t *info, int n) {
	video_playback_setup *settings = (video_playback_setup*) settings;
	switch(info->uc->playback_mode) {
	  case VJ_PLAYBACK_MODE_PLAIN: info->sfd = n; break;
	  case VJ_PLAYBACK_MODE_SAMPLE: info->sfd = n; sample_set_framedup(info->uc->sample_id,n);break;
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
			veejay_msg(VEEJAY_MSG_DEBUG, "Speed too high to set!");

		break;
    case VJ_PLAYBACK_MODE_SAMPLE:
		len = sample_get_endFrame(info->uc->sample_id) - sample_get_startFrame(info->uc->sample_id);
		if( speed < 0)
		{
			if ( (-1*len) > speed )
			{
				veejay_msg(VEEJAY_MSG_ERROR,"Speed %d too high to set! (not enough frames)",speed);
				return 1;
			}
		}
		else
		{
			if(speed >= 0)
			{
				if( len < speed )
				{
					veejay_msg(VEEJAY_MSG_ERROR, "Speed %d too high to set (not enought frames)",speed);
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
		veejay_msg(VEEJAY_MSG_ERROR, "insanity, unknown playback mode");
		break;
    }
#ifdef HAVE_JACK

    if(info->audio == AUDIO_PLAY )
		vj_jack_continue( settings->current_playback_speed );
#endif

/*    if( settings->current_playback_speed != 0 &&
	settings->state != LAVPLAY_STATE_PLAYING)
	veejay_change_state( info, LAVPLAY_STATE_PLAYING );*/

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

	/*
    simple_frame_duplicator++;

    if (simple_frame_duplicator >= info->sfd) {
	settings->current_frame_num += num;
	simple_frame_duplicator = 0;
    }
	*/
   if( info->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN)
	{
		if(settings->current_frame_num < settings->min_frame_num) return 0;
		if(settings->current_frame_num > settings->max_frame_num) return 0;
   }

   if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
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

	vj_tag_free();
#ifdef USE_SWSCALER
  	if( info->settings->zoom )
		yuv_free_swscaler( info->video_out_scaler );
#endif
	if( info->settings->action_scheduler.state )
	{
		if(info->settings->action_scheduler.sl )
			free(info->settings->action_scheduler.sl );
		info->settings->action_scheduler.state = 0;
	}

	if( info->plugin_frame) vj_perform_free_plugin_frame(info->plugin_frame);
	if( info->plugin_frame_info) free(info->plugin_frame_info);
	if( info->effect_frame1) free(info->effect_frame1);
	if( info->effect_frame_info) free(info->effect_frame_info);
	if( info->effect_frame2) free(info->effect_frame2);
	if( info->effect_info) free( info->effect_info );
	if( info->dummy ) free(info->dummy );
    free(info->status_msg);
    free(info->status_what);
    free(info->uc);
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
	{
		veejay_msg(VEEJAY_MSG_ERROR ,"Negative frame numbers are bogus",framenum);
		return -1;
	}

    if(framenum > (info->edit_list->video_frames-1))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot set frame %ld",framenum);
//	  	return -1;
	}

    if(info->uc->playback_mode==VJ_PLAYBACK_MODE_SAMPLE)
	{
		int start,end,loop,speed;	
		sample_get_short_info(info->uc->sample_id,&start,&end,&loop,&speed);
		if(framenum < start)
		  framenum = start;
		if(framenum > end) 
		  framenum = end;
    }

    settings->current_frame_num = framenum;


    return 1;  
}


void	veejay_auto_loop(veejay_t *info)
{
	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN)
	{
		char sam[20];
		sprintf(sam, "%03d:1 0;", VIMS_SAMPLE_NEW);
		vj_event_parse_msg(info, sam);
		sprintf(sam, "%03d:-1;", VIMS_SAMPLE_SELECT);
		vj_event_parse_msg(info,sam);
	}
}
/******************************************************
 * veejay_init()
 * check the given settings and initialize almost
 * everything
 * return value: 0 on success, -1 on error
 ******************************************************/
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
 
   if (el->has_audio && info->audio == AUDIO_PLAY) {
	if (vj_perform_audio_start(info)) {
	    veejay_msg(VEEJAY_MSG_INFO, "Started Audio Task");
	//    stats.audio = 1;
	} else {
	    veejay_msg(VEEJAY_MSG_WARNING, "Could not start Audio Task, is Jack running ?");
	    veejay_change_state(info,LAVPLAY_STATE_STOP );
	}
    }
   if( !el->has_audio )
	veejay_msg(VEEJAY_MSG_DEBUG, "EditList contains no audio");
   if( info->audio != AUDIO_PLAY)
	veejay_msg(VEEJAY_MSG_DEBUG, "Not configured for audio playback");

   if (el->has_audio && (info->audio==AUDIO_PLAY || info->audio==AUDIO_RENDER))
   {
	settings->spas = 1.0 / (double) el->play_rate;
   }
   else
   {
	settings->spas = 0;
   }

    return 0;

}

void veejay_change_playback_mode( veejay_t *info, int new_pm, int sample_id )
{
	// if current is stream and playing network stream, close connection
	if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
	{
		int cur_id = info->uc->sample_id;
		int type = vj_tag_get_type( cur_id );
		if( (type == VJ_TAG_TYPE_NET||type==VJ_TAG_TYPE_PICTURE) && cur_id != sample_id )
		{
			vj_tag_disable(cur_id);
		}	
	}

	if(new_pm == VJ_PLAYBACK_MODE_PLAIN )
	{
          int n = 0;
	  if(info->uc->playback_mode==VJ_PLAYBACK_MODE_TAG) 
		n = vj_tag_chain_free( info->uc->sample_id );
	  if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
		n = sample_chain_free( info->uc->sample_id);
	  info->uc->playback_mode = new_pm;
/*	  if(n > 0)
	  {
		veejay_msg(VEEJAY_MSG_WARNING, "Deactivated %d effect%s", n, (n==1 ? " " : "s" ));
	  }*/
	  veejay_msg(VEEJAY_MSG_INFO, "Playing plain video now (set %p) ", info->current_edit_list);
	  info->edit_list = info->current_edit_list;
	  video_playback_setup *settings = info->settings;
	  settings->min_frame_num = 0;
	  settings->max_frame_num = info->edit_list->video_frames-1;
	}
	if(new_pm == VJ_PLAYBACK_MODE_TAG)
	{
		int tmp=0;
		// new mode is stream, see if sample_id is a network stream (if so, connect!)
		if( vj_tag_get_type( sample_id ) == VJ_TAG_TYPE_NET ||
			vj_tag_get_type( sample_id) == VJ_TAG_TYPE_MCAST )
		{
			if(vj_tag_enable( sample_id )<= 0 )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Unable to activate network stream!");
				return;
			}
		}	
			
		if(info->uc->playback_mode==VJ_PLAYBACK_MODE_SAMPLE)
		{
			tmp = sample_chain_free(info->uc->sample_id);
			veejay_msg(VEEJAY_MSG_DEBUG, "Deactivated %d effect%s", tmp, (tmp==1 ? " " : "s"));
		}
		if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
		{
			if(sample_id == info->uc->sample_id) return;

			tmp = vj_tag_chain_free(info->uc->sample_id);
		//	veejay_msg(VEEJAY_MSG_DEBUG, "Deactivated %d effect%s", tmp,(tmp==1 ? " " : "s"));
		}
		tmp = vj_tag_chain_malloc( sample_id);
	/*	if(tmp > 0 )
		{
			veejay_msg(VEEJAY_MSG_WARNING, "Activated %d effect%s", tmp, (tmp==1? " " : "s") );
		}*/
		info->uc->playback_mode = new_pm;
		veejay_set_sample(info,sample_id);
	}
	if(new_pm == VJ_PLAYBACK_MODE_SAMPLE) 
	{
		int tmp =0;
		
		if(info->uc->playback_mode==VJ_PLAYBACK_MODE_TAG)
		{
			tmp = vj_tag_chain_free(info->uc->sample_id);
		//	veejay_msg(VEEJAY_MSG_DEBUG, "Deactivated %d effect%s", tmp, (tmp==1 ? " " : "s"));
		}
		if(info->uc->playback_mode==VJ_PLAYBACK_MODE_SAMPLE)	
		{
			if(sample_id != info->uc->sample_id)
			{
				tmp = sample_chain_free( info->uc->sample_id );
	//			veejay_msg(VEEJAY_MSG_DEBUG, "Deactivated %d effect%s", tmp, (tmp==1 ? " " : "s"));
			}
		}
		tmp = sample_chain_malloc( sample_id );
	/*	if(tmp > 0)
		{
			veejay_msg(VEEJAY_MSG_WARNING, "Activated %d effect%s", tmp,tmp==0 ? " " : "s" );
		}*/
		info->uc->playback_mode = new_pm;
		veejay_set_sample(info, sample_id);
	}
}


void veejay_set_sample(veejay_t * info, int sampleid)
{
    int start,end,speed,looptype;
    if ( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
    {
	if(!vj_tag_exists(sampleid))
        {
		    veejay_msg(VEEJAY_MSG_ERROR, "Stream %d does not exist", sampleid);
	     	   return;
        }
	info->last_tag_id = sampleid;
	info->uc->sample_id = sampleid;

	if(info->settings->current_playback_speed==0) 
		veejay_set_speed(info, 1);

 	veejay_msg(VEEJAY_MSG_INFO, "Playing stream %d",
		sampleid);

	info->edit_list = info->current_edit_list;
	veejay_reset_el_buffer(info);
	return;
     }

     if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
     {
		if(!sample_exists(sampleid))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample %d does not exist", sampleid);
			return;
		}

		editlist *edl = 
			sample_get_editlist( sampleid );
		info->edit_list = sample_get_editlist( sampleid );

		veejay_reset_el_buffer(info);

	   	sample_get_short_info( sampleid , &start,&end,&looptype,&speed);

 		 veejay_msg(VEEJAY_MSG_INFO, "Playing sample %d (frames %d - %d) at speed %d",
			sampleid, start,end,speed);

		 info->uc->sample_id = sampleid;
		 info->last_sample_id = sampleid;
		 info->sfd = sample_get_framedup(sampleid);

		 info->uc->render_changed = 1; /* different render list */
    		 sample_reset_offset( sampleid );	/* reset mixing offsets */
    		 veejay_set_frame(info, start);
    		 veejay_set_speed(info, speed);
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
				return -1;
			}	   
		}
	}

	int id = vj_tag_new(type, filename, index, info->current_edit_list, info->pixel_format, channel);

	char descr[200];
	bzero(descr,200);
	vj_tag_get_by_type(type,descr);
	if(id > 0 )
	{
		info->nstreams++;
		if(type == VJ_TAG_TYPE_V4L || type == VJ_TAG_TYPE_MCAST || type== VJ_TAG_TYPE_NET)
			vj_tag_set_active( id, 1 );
		veejay_msg(VEEJAY_MSG_INFO, "New stream %s with ID %d created",descr, id );
		return id;
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to create %s stream", descr );
    	}
 	return -1;
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
    info->uc->sample_start = -1;
    info->uc->sample_end = -1;
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

	vj_perform_unlock_primary_frame();
    // get the frame to output, in 420 or 422
    if (info->uc->take_bg==1)
    {
		// fixme: 
		vj_perform_get_primary_frame(info,frame,0);
        vj_perform_take_bg(info,frame);
        info->uc->take_bg = 0;
    } 
#ifdef USE_SWSCALER
	// scale the image if wanted
	video_playback_setup *settings = info->settings;
	if(settings->zoom )
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
			vj_perform_get_cropped_frame(info, &(src.data), 1);
		else
			vj_perform_get_primary_frame(info, src.data, 0 );

		vj_perform_get_output_frame(info, dst.data );

		yuv_convert_and_scale( info->video_out_scaler, src.data, dst.data );	

		vj_perform_get_output_frame( info, frame );
	}
 	else
#endif
		vj_perform_get_primary_frame(info,frame,0);
	

#ifdef HAVE_JPEG
#ifdef USE_GDK_PIXBUF 
    /* dirty hack to save a frame to jpeg */
    if (info->uc->hackme == 1)
	{
		info->uc->hackme = 0;

#ifdef USE_GDK_PIXBUF
		if(vj_picture_save( info->settings->export_image, frame, 
				info->video_output_width, info->video_output_height,
				info->current_edit_list->pixel_format ) )
		{
			veejay_msg(VEEJAY_MSG_INFO,
				"Saved frame %ld to image", info->settings->current_frame_num );
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR,
				"Error writing frame %ld to image",
					info->settings->current_frame_num );
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
    if (info->stream_enabled == 1) {
	// Y4m is always 4:2:0 , this function ensures it 
	vj_perform_get_primary_frame_420p(
				info, c_frame );
	if (vj_yuv_put_frame(info->output_stream, c_frame) == -1)
	{
	    veejay_msg(VEEJAY_MSG_ERROR, 
			"Error stopping YUV4MPEG output stream ");
	    vj_yuv_stream_stop_write(info->output_stream);
	    info->stream_enabled = 0;
	}
    }
#ifdef HAVE_V4L
	if( info->vloopback )
	{
		// push buffer
		vj_vloopback_fill_buffer( info->vloopback , frame );		
		// if piping 
		if( vj_vloopback_get_mode( info->vloopback ))
			vj_vloopback_write_pipe( info->vloopback );
	}
#endif

	//vj_perform_get_p_data( info->plugin_frame );
	vj_perform_update_plugin_frame( info->plugin_frame );

	plugins_process( (void*) info->plugin_frame_info, (void*) info->plugin_frame );

	plugins_process_video_out( (void*) info->plugin_frame_info, (void*) info->plugin_frame );

	// send a frame to all participants when using mcast
	// (activated after sending VIMS MCAST SENDER START/STOP)
	if( info->settings->use_vims_mcast)
		vj_perform_send_primary_frame_s(info, 1);

	//todo: this sucks, have it modular.( video out drivers )
    switch (info->video_out) {
#ifdef HAVE_SDL
	case 0:
	    for(i = 0 ; i < MAX_SDL_OUT; i ++ )
		if( info->sdl[i] )
			if(!vj_sdl_update_yuv_overlay( info->sdl[i], frame ) )  return 0;  
	    break;
#endif
#ifdef HAVE_DIRECTFB
	case 1:
	    // directfb always blits to i420 , if we setup another display it also uses img_convert
	    vj_perform_get_primary_frame_420p(info,c_frame);
	    if (vj_dfb_update_yuv_overlay(info->dfb, c_frame) != 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error updating iamge");
		return 0;
	    }
	    break;
	case 2:
#ifdef HAVE_SDL
	    for( i = 0; i < MAX_SDL_OUT; i ++ )
		if( info->sdl[i] ) 	
		  if(!vj_sdl_update_yuv_overlay( info->sdl[i], frame ) ) return 0;
#endif
	    // again, directfb blits to i420
	    vj_perform_get_primary_frame_420p(info,c_frame);
	    if (vj_dfb_update_yuv_overlay(info->dfb, c_frame) != 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error updating iamge");
		return 0;
	    }
	    break;
#endif
	case 3:
#ifdef USE_SWSCALER
	     if(settings->zoom)
	     	vj_perform_get_output_frame_420p(info, c_frame, info->video_output_width, info->video_output_height );
	     else
#endif
		vj_perform_get_primary_frame_420p(info,c_frame);

	     if (vj_yuv_put_frame(info->render_stream, c_frame) == -1)
	     {
		veejay_msg(VEEJAY_MSG_ERROR, 
			    "Error writing to stream. Stopping...");
		vj_yuv_stream_stop_write(info->render_stream);
		veejay_change_state(info, LAVPLAY_STATE_STOP);
		return 0;
	     }
	     break;
	case 4:
		// fixme: audio in shared memory reader/writer
		produce( 
			info->segment,
			frame,
			info->current_edit_list->video_width*info->current_edit_list->video_height);	

		break;
	case 5:
		break;	
	default:
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode");

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

    /* I really *wish* the timer was higher res on x86 Linux... 10mSec
     * is a *pain*.  Sooo wasteful here...
     */
    if (info->uc->use_timer == 2) {
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
	    nsecsleep.tv_nsec =
		(frame_periods * settings->usec_per_frame -
		 usec_since_lastframe - 1000000 / HZ) * 1000;
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
    int total_slots = (sample_size() - 1 ) + (vj_tag_size() -1 );
    if(total_slots < 0)
	total_slots = 0;
    switch (info->uc->playback_mode) {
    	case VJ_PLAYBACK_MODE_SAMPLE:
			if( info->settings->randplayer.mode ==
				RANDMODE_SAMPLE)
				pm = VJ_PLAYBACK_MODE_PATTERN;
			if( sample_chain_sprint_status
				(info->uc->sample_id, info->real_fps,settings->current_frame_num, pm, total_slots,info->status_what ) != 0)
				{
				veejay_msg(VEEJAY_MSG_ERROR, "Invalid status!");
			}
		break;
       	case VJ_PLAYBACK_MODE_PLAIN:
		sprintf(info->status_what, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
			(int) info->real_fps,
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
			total_slots );
		break;
    	case VJ_PLAYBACK_MODE_TAG:
		if( vj_tag_sprint_status( info->uc->sample_id, (int) info->real_fps,
			settings->current_frame_num, info->uc->playback_mode,total_slots, info->status_what ) != 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid status!");
		}
		break;
    }
    
    d_len = strlen(info->status_what);
    snprintf(info->status_msg,MESSAGE_SIZE, "V%03dS%s", d_len, info->status_what);
    res = vj_server_send(info->vjs[1],link_id, info->status_msg, strlen(info->status_msg));

   if( res <= 0) { /* close command socket */
		_vj_server_del_client(info->vjs[1], link_id );
		_vj_server_del_client(info->vjs[0], link_id );
		_vj_server_del_client(info->vjs[3], link_id );
	}

    if (info->uc->chain_changed == 1)
		info->uc->chain_changed = 0;
    if (info->uc->render_changed == 1)
		info->uc->render_changed = 0;
}

/******************************************************
 * veejay_mjpeg_playback_thread()
 *   the main (software) video playback thread
 *
 * return value: 1 on success, 0 on error
 ******************************************************/
void veejay_signal_loop(void *arg)
{
    veejay_t *info = (veejay_t *) arg;
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    //struct sigaction act;
    int sig=0;
    pthread_sigmask(SIG_UNBLOCK, &(settings->signal_set), NULL);
    while (1) {
	sigwait(&(settings->signal_set), &sig);
	if (sig == SIGINT) {
	    veejay_change_state(info, LAVPLAY_STATE_STOP);
	    break;
	}
    }
    pthread_exit(NULL);
}


static void veejay_handle_callbacks(veejay_t *info) {

	/* check for OSC events */
	if(vj_osc_get_packet(info->osc)) {
		veejay_msg(VEEJAY_MSG_DEBUG, "(VIMS) Accepted OSC message bundle");
	}

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

static void *veejay_mjpeg_playback_thread(void *arg)
{
    veejay_t *info = (veejay_t *) arg;
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
   /* Allow easy shutting down by other processes... */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    vj_osc_set_veejay_t(info); 
    vj_tag_set_veejay_t(info);


#ifdef HAVE_SDL
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
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
		veejay_msg(VEEJAY_MSG_DEBUG,
			    "Playback thread: was told to exit");
		pthread_exit(NULL);
	 	return NULL;
	    }
	}
	pthread_mutex_unlock(&(settings->valid_mutex));
        if( settings->currently_processed_entry != settings->buffer_entry[settings->currently_processed_frame] &&
		!veejay_screen_update(info) )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Error playing frame %d", settings->current_frame_num);
		veejay_change_state( info, LAVPLAY_STATE_STOP);
	}

	settings->currently_processed_entry = 
		settings->buffer_entry[settings->currently_processed_frame];
	/* sync timestamp */

	veejay_mjpeg_software_frame_sync(info,
					  settings->valid[settings->
							  currently_processed_frame]);
	settings->syncinfo[settings->currently_processed_frame].frame =
	    settings->currently_processed_frame;


	veejay_handle_callbacks(info);
#ifdef HAVE_SDL
    SDL_Event event;
	while(SDL_PollEvent(&event) == 1) 
	{
			
		if( event.type == SDL_KEYDOWN)
		{
			vj_event_single_fire( (void*) info, event, 0);
		}
		
	}
#endif

//#endif
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
		"Playback thread: was told to exit");
    return NULL;
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
	mymemset_generic(&(settings->syncinfo[0]), 0, sizeof(struct mjpeg_sync));

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
    mymemset_generic(bp->reserved, 0, sizeof(bp->reserved));

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
			"Unknown video norm!");
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

static int veejay_mjpeg_queue_buf(veejay_t * info, int frame,
				   int frame_periods)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    /* mark this buffer as playable and tell the software playback thread to wake up if it sleeps */
    pthread_mutex_lock(&(settings->valid_mutex));
    settings->valid[frame] = frame_periods;
    pthread_cond_broadcast(&(settings->buffer_filled[frame]));
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


int veejay_init(veejay_t * info, int x, int y,char *arg, int def_tags)
{
	editlist *el = info->current_edit_list;
	video_playback_setup *settings = info->settings;
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


#ifdef HAVE_XML2
	if(info->load_action_file)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Loading configuaration file %s", info->action_file);
		veejay_load_action_file(info, info->action_file );
	}
#endif

	switch (info->uc->use_timer)
	{
		case 0:
			veejay_msg(VEEJAY_MSG_WARNING, "Not timing audio/video");
		break;
  		case 1:
			veejay_msg(VEEJAY_MSG_DEBUG, 
			    "RTC /dev/rtc hardware timer is broken!");
			info->uc->use_timer = 2;
			return -1;
		break;
    		case 2:
			veejay_msg(VEEJAY_MSG_DEBUG, "Using nanosleep timer");
		break;
    	}    

 	if (veejay_init_editlist(info) != 0) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
			    "Cannot initialize the EditList");
		return -1;
	}
	/* initialize tags (video4linux/yuv4mpeg stream ... ) */
	veejay_msg(VEEJAY_MSG_DEBUG, 
	 "Editlist is supposed to be ready %d, %d\n", info->current_edit_list->video_width,
			info->current_edit_list->video_height );

	if (vj_tag_init(info->current_edit_list->video_width, info->current_edit_list->video_height, info->pixel_format) != 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing stream manager");
		return -1;
    	}

 	sample_init( (info->current_edit_list->video_width * info->current_edit_list->video_height)  ); 

	sample_set_project( info->pixel_format,
			    info->auto_deinterlace,
			    info->preserve_pathnames,
				0,
			    info->edit_list->video_norm );


	if(!vj_perform_init(info))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize Performer");
		return -1;
    	}
#ifdef USE_SWSCALER
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
			vj_perform_get_primary_frame(info, &src.data ,0 );
	
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
	else
	{
#endif
	    /* setup output dimensions */
	    info->video_output_width = el->video_width;
	    info->video_output_height = el->video_height;
#ifdef USE_SWSCALER
	}
#endif

	if(!info->bes_width)
		info->bes_width = info->video_output_width;
	if(!info->bes_height)
		info->bes_height = info->video_output_height;		
	


	plugins_allocate();

	if(info->current_edit_list->has_audio) {
		if (vj_perform_init_audio(info) == 0)
			veejay_msg(VEEJAY_MSG_INFO, "Initialized Audio Task");
		else
			veejay_msg(VEEJAY_MSG_ERROR, 
				"Unable to initialize Audio Task");
	}

  	veejay_msg(VEEJAY_MSG_INFO, 
		"Initialized %d Image- and Video Effects", MAX_EFFECTS);
    	vj_effect_initialize(info->current_edit_list->video_width, info->current_edit_list->video_height);
   
	info->plugin_frame = vj_perform_init_plugin_frame(info);
	info->plugin_frame_info = vj_perform_init_plugin_frame_info(info);



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
		if(sample_readFromFile( info->settings->action_scheduler.sl ) )
			veejay_msg(VEEJAY_MSG_INFO, "Loaded sample list %s from actionfile",
					info->settings->action_scheduler.sl );
	}
	
   
	if( settings->action_scheduler.state )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Finish pending actions from configuration file ...");
		veejay_finish_action_file(info,info->action_file);
		settings->action_scheduler.state = 0; 
	}
	if( !vj_server_setup(info) )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Setting up server");
		return -1;
	}

    /* now setup the output driver */
    switch (info->video_out) {
 		case 4:
		info->segment = new_segment( info->video_output_width * info->video_output_height * 3);
		if(!info->segment)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize shared memory");
			return -1;
		}
		attach_segment(info->segment, 1);
		veejay_msg(VEEJAY_MSG_DEBUG , "Shared memory key = %d, Semaphore = %d",
			get_segment_id(info->segment), get_semaphore_id(info->segment));
		break;
#ifdef HAVE_SDL
    case 0:

		info->sdl[0] =
		    (vj_sdl *) vj_sdl_allocate( info->video_output_width,
					      info->video_output_height,
						info->pixel_format);

		if( x != -1 && y != -1 )
			vj_sdl_set_geometry(info->sdl[0],x,y);

		if (!vj_sdl_init(info->sdl[0], info->bes_width, info->bes_height, "Veejay",1,
			info->settings->full_screen[0]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error initializing SDL");
		    return -1;
		}
		break;
#endif
#ifdef HAVE_DIRECTFB
    case 1:
		veejay_msg(VEEJAY_MSG_DEBUG, "Initializing DirectFB");
		info->dfb =
		    (vj_dfb *) vj_dfb_allocate(
							info->video_output_width,
					       	info->video_output_height,
					       	el->video_norm);
		if (vj_dfb_init(info->dfb) != 0)
	    	return -1;
		break;
	case 2:
		veejay_msg(VEEJAY_MSG_DEBUG, 
		    "Initializing cloned output (if both SDL/DirectFB are compiled in)");
#ifdef HAVE_SDL
		info->sdl[0] =
	    	(vj_sdl *) vj_sdl_allocate(info->video_output_width,
				      info->video_output_height, info->pixel_format);

		if (!vj_sdl_init(info->sdl[0], info->bes_width, info->bes_height,"Veejay",1,
			info->settings->full_screen[0]))
	   	 return -1;
#endif
		info->dfb =
		    (vj_dfb *) vj_dfb_allocate( info->video_output_width,
					       info->video_output_height,
					       el->video_norm);
		if (vj_dfb_init(info->dfb) != 0)
		    return -1;
		break;
#endif
    case 3:
		veejay_msg(VEEJAY_MSG_INFO, 
		    "Entering render mode (no visual output)");
        
		info->render_stream = vj_yuv4mpeg_alloc(info->current_edit_list, info->video_output_width,info->video_output_height);



		if (vj_yuv_stream_start_write
		  	  (info->render_stream, info->stream_outname,
	  		   info->current_edit_list) == 0) {
	   	 		veejay_msg(VEEJAY_MSG_INFO, 
					"Rendering to [%s].",
					info->stream_outname);
 		}
	 	else {
	   		veejay_msg(VEEJAY_MSG_ERROR, 
				"Cannot create render stream. Aborting");
	    	return -1;
		}
		break;
	
	case 5:
		break;
    default:
		veejay_msg(VEEJAY_MSG_ERROR,
		    "Invalid playback mode. Use -O [012345]");
		return -1;
	break;
    }




	if(info->dummy->active)
	{
	 	int dummy_id;
		/* Use dummy mode, action file could have specified something */
		if( vj_tag_size()-1 <= 0 )
			dummy_id = vj_tag_new( VJ_TAG_TYPE_COLOR, "Solid", -1, info->current_edit_list,info->pixel_format,-1);
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

    /* After we have fired up the audio and video threads system (which
     * are assisted if we're installed setuid root, we want to set the
     * effective user id to the real user id
     */
    if (seteuid(getuid()) < 0) {
	/* fixme: get rid of sys_errlist and use sys_strerror */
	veejay_msg(VEEJAY_MSG_ERROR, 
		    "Can't set effective user-id: %s", sys_errlist[errno]);
	return -1;
    }

    veejay_change_state( info, LAVPLAY_STATE_PLAYING );  

    if (!veejay_mjpeg_set_playback_rate(info, el->video_fps,
					 el->video_norm ==
					 'p' ? VIDEO_MODE_PAL : VIDEO_MODE_NTSC)) {
	return -1;
    }

    if(veejay_open(info) != 1) {
	veejay_msg(VEEJAY_MSG_ERROR, 
	    "Unable to initialize the threading system");
	return -1;   
      	}
    return 0;
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
    int n;
    int first_free, frame, skipv, skipa, skipi, nvcorr;
    long frame_number[2];	/* Must be at least as big as the number of buffers used */
    struct mjpeg_params bp;
    long ts, te;
    vj_perform_queue_audio_frame(info,0);
    vj_perform_queue_video_frame(info,0,0);
    if (vj_perform_queue_frame(info, 0, 0) != 0)
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
    stats.nqueue = 0;
    stats.nsync = 0;
    stats.audio = 0;
    stats.norm = el->video_norm == 'n' ? 1 : 0;
    frame = 0;
    tdiff1 = 0.;
    tdiff2 = 0.;
    nvcorr = 0;
    if(el->has_audio && info->audio == AUDIO_PLAY) stats.audio = 1;

//   if (el->has_audio && info->audio == AUDIO_PLAY) {
//	if (vj_perform_audio_start(info)) {
//	    stats.audio = 1;
//	} else {
//	    veejay_msg(VEEJAY_MSG_ERROR, "Could not start Audio Task");
//	}
  //  }
 /* Queue the buffers read, this triggers video playback */
  

  
    frame_number[0] = settings->current_frame_num;
    veejay_mjpeg_queue_buf(info, 0, 1);
    
    stats.nqueue = 1;

   //	 settings->br.count;
     while (settings->state != LAVPLAY_STATE_STOP) {
	/* Sync to get a free buffer. We want to have all free buffers,
	 * which have been played so far. Normally there should be a function
	 * in the kernel API to get the number of all free buffers.
	 * I don't want to change this API at the moment, therefore
	 * we look on the clock to see if there are more buffers ready
	 */
	first_free = stats.nsync;

    	do {
	    if (settings->state == LAVPLAY_STATE_STOP) {
		goto FINISH;
		}
	    if (!veejay_mjpeg_sync_buf(info, &bs)) {
		veejay_change_state(info, LAVPLAY_STATE_STOP);
		goto FINISH;
	    }
	
	    frame = bs.frame;
	    /* Since we queue the frames in order, we have to get them back in order */
	    if (frame != stats.nsync % 1) {
		veejay_msg(VEEJAY_MSG_ERROR, 
			    "**INTERNAL ERROR: Bad frame order on sync: frame = %d, nsync = %d, br.count = %ld sould be %ld",
			    frame, stats.nsync, 1, (stats.nsync % 1));
		veejay_change_state(info, LAVPLAY_STATE_STOP);
		goto FINISH;
	    }
	   
	    stats.nsync++;
	    /* Lock on clock */
	    gettimeofday(&time_now, 0);
	    stats.tdiff = time_now.tv_sec - bs.timestamp.tv_sec +
		(time_now.tv_usec - bs.timestamp.tv_usec)*1.e-6;
	} while (stats.tdiff > settings->spvf && (stats.nsync - first_free) < (1 - 1));

#ifdef HAVE_JACK
	if ( el->has_audio && info->audio==AUDIO_PLAY ) {
	    //audio_get_output_status(&audio_tmstmp, &(stats.num_asamps),
		//			    &(stats.num_aerr));
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
             tdiff2 = (bs.timestamp.tv_sec - audio_tmstmp.tv_sec) +
                (bs.timestamp.tv_usec - audio_tmstmp.tv_usec) * 1.e-6;
          }

	}
#endif
	stats.tdiff = tdiff1 - tdiff2;
	/* Fill and queue free buffers again */
	for (n = first_free; n < stats.nsync;) {
	    /* Audio/Video sync correction */
	    skipv = 0;
	    skipa = 0;
	    skipi = 0;
 
	    if (info->sync_correction) {
		if (stats.tdiff > settings->spvf) {
		    /* Video is ahead audio */
		    skipa = 1;
		    if (info->sync_ins_frames)
			skipi = 1;
		    nvcorr++;
		    stats.num_corrs_a++;
		    stats.tdiff -= settings->spvf;
		    stats.stats_changed = 1;
		}
		if (stats.tdiff < -settings->spvf) {
		    /* Video is behind audio */
		    skipv = 1;
   		    if (!info->sync_skip_frames)
			skipi = 1;

		    //veejay_msg(VEEJAY_MSG_INFO, "video behind audio, skip video frame = %s tdiff = %4.2f , spvf = %4.2f",
		//	(skipi==0?"NO":"YES"), stats.tdiff, settings->spvf);
				    nvcorr--;
		    stats.num_corrs_b++;
		    stats.tdiff += settings->spvf;
		    stats.stats_changed = 1;
		}
	    }

	    /* Read one frame, break if EOF is reached */
#ifdef HAVE_SDL
	    ts= SDL_GetTicks();
#endif
	    // actually measure duration of render in ms */
	    frame = n % 1;
	    frame_number[frame] = settings->current_frame_num;
	    settings->buffer_entry[frame] = settings->current_frame_num;
//		el->frame_list[settings->current_frame_num];
	    if (!skipa) 
        {
			vj_perform_queue_audio_frame(info,frame);
	    }

	    if (!skipv)
        { 
				vj_perform_queue_video_frame(info,frame,skipi);
	   	}
	    
	    vj_perform_queue_frame(info,skipi,frame);
#ifdef HAVE_SDL	
	    te = SDL_GetTicks();
#endif
	    if(info->real_fps == -1)
	    {
		float elapsed = (float)(te-ts)/1000.0;
		float spvf = info->settings->spvf;
		int level = VEEJAY_MSG_INFO;
			if(elapsed > (spvf*0.8) && elapsed < spvf)
			{
			level = VEEJAY_MSG_WARNING;
			}
			else
			{
				if( elapsed > spvf)
				level = VEEJAY_MSG_ERROR;
			}
			veejay_msg( level,
			"Seconds per video frame: %2.2f, seconds elapsed: %2.4f ",
			(float)info->settings->spvf,
			((float)(te-ts) )/ 1000.0 );

	    }
#ifdef HAVE_SDL
            info->real_fps = te - ts;
#else
	    info->real_fps = 0;
#endif

	    if(skipv && (info->video_out!=4)) continue;
	    if (!veejay_mjpeg_queue_buf(info, frame, 1)) {
		veejay_msg(VEEJAY_MSG_ERROR ,"Error queuing a frame");
		veejay_change_state(info, LAVPLAY_STATE_STOP);
		goto FINISH;
	    }
	    stats.nqueue++;
	    n++;
	}

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
    if (info->audio==AUDIO_PLAY)
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
  
#ifdef USE_SWSCALER  
	if(info->settings->zoom)
	{
		veejay_msg(VEEJAY_MSG_INFO,"Software scaler - output stream dimensions %d x %d ",
			info->video_output_width, info->video_output_height );
	}
#endif
	veejay_msg(VEEJAY_MSG_INFO,"Your best friends are 'man' and 'vi'");
	veejay_msg(VEEJAY_MSG_INFO,"Type 'man veejay' in a shell to learn more about veejay");
	veejay_msg(VEEJAY_MSG_INFO,"For a list of events, type 'veejay -u |less' in a shell");
	veejay_msg(VEEJAY_MSG_INFO,"Use 'sayVIMS -i' or gveejay to enter interactive mode");
	veejay_msg(VEEJAY_MSG_INFO,"Alternatives are OSC applications or 'sendVIMS' extension for PD"); 

}

static void *veejay_playback_thread(void *data)
{
    veejay_t *info = (veejay_t *) data;
    int i;
    

    Welcome(info);
   
    veejay_playback_cycle(info);

      veejay_close(info); 

    veejay_msg(VEEJAY_MSG_DEBUG,"Exiting playback thread");
    if(info->uc->is_server) {
	for(i = 0; i < 4; i ++ )
	  if(info->vjs[i]) vj_server_shutdown(info->vjs[i]); 
    }
    if(info->osc) vj_osc_free(info->osc);

    vj_yuv4mpeg_free(info->output_stream); 
    free(info->output_stream);
    
#ifdef HAVE_SDL
    for ( i = 0; i < MAX_SDL_OUT ; i ++ )
		if( info->sdl[i] )
		{
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

    if( info->video_out == 3 )
	{
		vj_yuv_stream_stop_write(info->render_stream);
		veejay_msg(VEEJAY_MSG_DEBUG, "Stopped rendering to [%s]",
			    info->stream_outname);
	}
	if( info->video_out == 4 )
	{
		del_segment(info->segment);
		veejay_msg(VEEJAY_MSG_DEBUG,  "Deleted shared memory");
    }
    
    vj_perform_free(info);
    vj_effect_shutdown();
    vj_tag_close_all();
    vj_el_free(info->current_edit_list);

    veejay_msg(VEEJAY_MSG_DEBUG,"Exiting playback thread");
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
	info->vjs[0] = vj_server_alloc(info->uc->port, NULL, V_CMD);

	if(!info->vjs[0])
		return 0;

	info->vjs[1] = vj_server_alloc(info->uc->port, NULL, V_STATUS);
	if(!info->vjs[1])
		return 0;

	info->vjs[3] = vj_server_alloc(info->uc->port, NULL, V_MSG );
	if(!info->vjs[3])
		return 0;

	info->vjs[2] = NULL;
	if( info->settings->use_vims_mcast )
	{
		info->vjs[2] =
			vj_server_alloc(info->uc->port, info->settings->vims_group_name, V_CMD );
		if(!info->vjs[2])
		{
			veejay_msg(VEEJAY_MSG_ERROR,
		  		 "Unable to initialize mcast sender");
			return 0;
		}
		//info->settings->mcast_frame_sender = 1;
	}
	if(info->settings->use_mcast)
		GoMultiCast( info->settings->group_name );

	info->osc = (void*) vj_osc_allocate(info->uc->port+2);

    	if(!info->osc) 
	{
		veejay_msg(VEEJAY_MSG_ERROR,
		  "Unable to start OSC server at port %d",
			info->uc->port + 2 );
		return 0;
	}

	// see libvjnet/common.h

	if( info->settings->use_mcast )
		veejay_msg(VEEJAY_MSG_INFO, "UDP multicast OSC channel ready at port %d (group '%s')",
			info->uc->port + 2, info->settings->group_name );
	else
		veejay_msg(VEEJAY_MSG_INFO, "UDP unicast OSC channel ready at port %d",
			info->uc->port + 2 );

	if(vj_osc_setup_addr_space(info->osc) == 0)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Initialized OSC (http://www.cnmat.berkeley.edu/OpenSoundControl/)");
	}




    	if (info->osc == NULL || info->vjs[0] == NULL || info->vjs[1] == NULL) 
		return 0;
    	
    	info->uc->is_server = 1;

	return 1;
}

/******************************************************
 * veejay_malloc()
 *   malloc the pointer and set default options
 *
 * return value: a pointer to an allocated veejay_t
 ******************************************************/

veejay_t *veejay_malloc()
{
    
    veejay_t *info;
    int i;

    info = (veejay_t *) vj_malloc(sizeof(veejay_t));
    if (!info)
		return NULL;
	memset(info,0,sizeof(veejay_t));

    info->settings = (video_playback_setup *) vj_malloc(sizeof(video_playback_setup));
    if (!(info->settings)) 
		return NULL;
   	memset( info->settings, 0, sizeof(video_playback_setup));
	memset( &(info->settings->action_scheduler), 0, sizeof(vj_schedule_t));
	memset( &(info->settings->viewport ), 0, sizeof(VJRectangle)); 

    info->status_what = (char*) vj_malloc(sizeof(char) * MESSAGE_SIZE );
    info->status_msg = (char*) vj_malloc(sizeof(char) * MESSAGE_SIZE+5);
    bzero(info->status_what,MESSAGE_SIZE);
    bzero(info->status_what,MESSAGE_SIZE);

	info->uc = (user_control *) vj_malloc(sizeof(user_control));
    if (!(info->uc)) 
		return NULL;
	memset( info->uc, 0, sizeof(user_control));

    info->effect_frame1 = (VJFrame*) vj_malloc(sizeof(VJFrame));
	if(!info->effect_frame1)
		return NULL;
	memset( info->effect_frame1, 0, sizeof(VJFrame));

    info->effect_frame2 = (VJFrame*) vj_malloc(sizeof(VJFrame));
	if(!info->effect_frame2)
		return NULL;
	memset( info->effect_frame2, 0, sizeof(VJFrame));


    info->effect_frame_info = (VJFrameInfo*) vj_malloc(sizeof(VJFrameInfo));
	if(!info->effect_frame_info)
		return NULL;
	memset( info->effect_frame_info, 0, sizeof(VJFrameInfo));


    info->effect_info = (vjp_kf*) vj_malloc(sizeof(vjp_kf));
	if(!info->effect_info) 
		return NULL;   
	memset( info->effect_info, 0, sizeof(vjp_kf)); 

	info->dummy = (dummy_t*) vj_malloc(sizeof(dummy_t));
    if(!info->dummy)
		return NULL;
	memset( info->dummy, 0, sizeof(dummy_t));

#ifdef USE_SWSCALER
	memset(&(info->settings->sws_templ), 0, sizeof(sws_template));
#endif

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
    info->uc->sample_start = -1;
    info->uc->sample_end = -1;
    info->net = 1;

    bzero(info->action_file,256); 
    bzero(info->stream_outname,256);

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
	info->sdl = (vj_sdl**) vj_malloc(sizeof(vj_sdl*) * MAX_SDL_OUT ); 
	for( i = 0; i < MAX_SDL_OUT;i++ )
		info->sdl[i] = NULL;
#endif


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
//    veejay_change_state(info, LAVPLAY_STATE_PAUSED);
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
		(uint64_t *) vj_malloc((n2 - n1 + 1) * sizeof(uint64_t));

    if (!settings->save_list)
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
		    "Malloc error, you\'re probably out of memory");
		veejay_change_state(info, LAVPLAY_STATE_STOP);
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

    uint64_t len = n2 - n1 + 1;

    if( n1 < 0 || n2 > el->video_frames-1)
    {
	veejay_msg(VEEJAY_MSG_ERROR, "Sample start and end are outside of editlist");
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
	}

    /* copy edl frames */
    new_el->frame_list =
		(uint64_t *) vj_malloc(  sizeof(uint64_t) * len );

    if (!new_el->frame_list)
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
		    "Malloc error, you\'re probably out of memory");
		veejay_change_state(info, LAVPLAY_STATE_STOP);
		return NULL;
    	}

    k = 0;

    for (i = n1; i <= n2; i++)
		new_el->frame_list[k++] = el->frame_list[i];
	veejay_msg(VEEJAY_MSG_DEBUG, "Copied %d frames to new EDL %p from %p", k, new_el,el);
    // set length
    new_el->video_frames = len;


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

    if (end < start || start > el->video_frames
		|| end >= el->video_frames || end < 0 || start < 0) {
		veejay_msg(VEEJAY_MSG_WARNING, 
			    "Incorrect parameters for deleting frames");
		return 0;
    }

    for (i = n2 + 1; i < el->video_frames; i++)
		el->frame_list[i - (n2 - n1 + 1)] =
	    	el->frame_list[i];

    if (start - 1 < settings->min_frame_num)
	{
		if (end < settings->min_frame_num)
		    settings->min_frame_num -= (end - start + 1);
		else
		    settings->min_frame_num = start;
    }

    if (start - 1 < settings->max_frame_num)
	{
		if (end <= settings->max_frame_num)
		    settings->max_frame_num -= (end - start + 1);
		else
		    settings->max_frame_num = start - 1;
    }
    if (start <= settings->current_frame_num) {

		if (settings->current_frame_num <= end)
		{
		    settings->current_frame_num = start;
		}
		else
		{
		    settings->current_frame_num -= (end - start);
		}
    }

    el->video_frames -= (end - start + 1);

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
		veejay_msg(VEEJAY_MSG_ERROR,
			    "Malloc error, you\'re probably out of memory");
		veejay_change_state(info, LAVPLAY_STATE_STOP);
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

	veejay_msg(VEEJAY_MSG_DEBUG, "K = %lld, NVF = %ld", k, el->video_frames );

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
	if(sample)
		sample_edl = sample_get_editlist( id );
	
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
		return -1;

	// the editlist dimensions must match (there's more)
	if( sample_edl->video_width != info->edit_list->video_width ||
	    sample_edl->video_height != info->edit_list->video_height )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot add this video file");
		if(sample_edl) vj_el_free(sample_edl);
		return -1;
	}

	// the sample is not there yet,create it
	if(!sample)
	{
		sample = sample_skeleton_new( 0, sample_edl->video_frames - 1 );
		sample_store(sample);
		sample_set_editlist( sample->sample_id , sample_edl );
		veejay_msg(VEEJAY_MSG_INFO,
			"Created new sample %d from file %s",sample->sample_id,
				files[0]);

	}

	// free temporary values
   	if(files[0]) free(files[0]);

	// return new id
        return sample->sample_id;
}

int veejay_edit_addmovie(veejay_t * info, editlist *el, char *movie, long start,long end, long destination )
{
	video_playback_setup *settings =
		(video_playback_setup *) info->settings;
	uint64_t n, i;
	uint64_t c = el->video_frames;
	if( el->is_empty )
		c -= 2;

	n = open_video_file(movie, el, info->preserve_pathnames, info->auto_deinterlace,1,
		info->edit_list->video_norm );

	if (n < 0)
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


int veejay_save_selection(veejay_t * info, char *filename, long start,
			   long end)
{
    
	/*
    if (info->uc->playback_mode == VJ_PLAYBACK_MODE_PATTERN) {
	editlist = info->pattern_info->edit_list;
    } else {
	editlist = info->edit_list;
    }
	*/
/*
    editlist = info->edit_list;

    if (write_edit_list(filename, start, end, editlist))
	return 0;

    veejay_msg(VEEJAY_MSG_DEBUG, 
		"TODO: Saved frames %ld-%ld to editlist %s", start, end,
		filename);
*/
    return 1;
}

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
	{
		n2 = info->edit_list->video_frames - 1;
	}	
	if( vj_el_write_editlist( filename, n1,n2, info->edit_list ) )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Saved editlist to file [%s]", filename);
	}
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
    	vj_avformat_init();

 
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
		info->pixel_format = (force_pix_fmt == 1 ? FMT_422 : FMT_420);
		veejay_msg(VEEJAY_MSG_WARNING, "Pixel format forced to YCbCr %s",
			(info->pixel_format == FMT_422 ? "4:2:2" : "4:2:0"));
	
	}
	else
	{
		info->pixel_format = -1;
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
			el->play_rate = el->audio_rate = info->dummy->arate;
			el->audio_chans = 2;
			el->audio_bits = 16;
			el->audio_bps = 4;
			veejay_msg(VEEJAY_MSG_DEBUG, "Dummy Audio: %f KHz, %d channels, %d bps, %d bit audio",
				(float)el->audio_rate/1000.0,el->audio_chans,el->audio_bps,el->audio_bits);
		}
	}
	else
	{
	    	info->current_edit_list = vj_el_init_with_args(files, num_files, info->preserve_pathnames, info->auto_deinterlace, force, override_norm, info->pixel_format);
	}
	info->edit_list = info->current_edit_list;

	if(info->edit_list==NULL)
	{
		return 0;
	}
	if(info->pixel_format == -1)
		info->pixel_format = info->edit_list->pixel_format;

	veejay_msg(VEEJAY_MSG_DEBUG, "Initialized with pixel format %d", info->pixel_format );

	vj_avcodec_init(info->current_edit_list ,   info->pixel_format);
    	if(info->pixel_format == FMT_422 )
	{
		if(!vj_el_init_422_frame( info->current_edit_list, info->effect_frame1)) return 0;
		if(!vj_el_init_422_frame( info->current_edit_list, info->effect_frame2)) return 0;
		info->settings->sample_mode = SSM_422_444;
	}
	else 
	{
		if(!vj_el_init_420_frame( info->current_edit_list, info->effect_frame1)) return 0;
		if(!vj_el_init_420_frame( info->current_edit_list, info->effect_frame2)) return 0;
		info->settings->sample_mode = SSM_420_JPEG_TR;
	}

	info->effect_frame_info->width = info->current_edit_list->video_width;
	info->effect_frame_info->height= info->current_edit_list->video_height;

	if(info->settings->output_fps > 0.0)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Overriding frame rate with %2.2f", 
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

	if(!ret)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to start veejay");
		return ret;
	}
	/* create samples from EDL */
	if( info->uc->file_as_sample)
	{
		long i,n=info->current_edit_list->num_video_files;
		for(i = 0; i < n; i ++ )
		{
			long start,end;
			if(vj_el_get_file_entry( info->current_edit_list, &start,&end, i ))
			{
				sample_info *skel = sample_skeleton_new( start,end );
				sample_store(skel);
			}	
		}
	}

	return ret;
}

