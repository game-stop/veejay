/*
 * 
 * veejay  
 *
 * Copyright (C) 2000 Niels Elburg <nelburg@looze.net>
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

#include <config.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include "sampleadm.h"
#include "vj-tag.h"
#include "vj-effect.h"
#include "vj-effman.h"
#include "vj-lib.h"
#include "editlist.h"
#include "vj-ffmpeg.h"
#ifdef SUPPORT_READ_DV2
#include "vj-dv.h"
#endif
#include "jpegutils.h"
#include "subsample.h"
#include "vj-common.h"
#include "vj-audio.h"
#include "vj-perform.h"
#include "libveejay.h"
#include "samplerecord.h"
#include <jpeglib.h>
#define RECORDERS 1
#ifdef HAVE_JACK
#include "vj-bjack.h"
#endif
extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;

#define PERFORM_AUDIO_SIZE 16384
static int simple_frame_duplicator;

struct ycbcr_frame {
    uint8_t *Y;
    uint8_t *Cb;
    uint8_t *Cr;
    uint8_t *alpha;
};

typedef struct el_row_t 
{
	uint8_t *row[CLIP_MAX_EFFECTS];
} el_row; 

struct ycbcr_frame **frame_buffer;	/* chain */
struct ycbcr_frame **primary_buffer;	/* normal */
static int cached_tag_frames[2][CLIP_MAX_EFFECTS];	/* cache a frame into the buffer only once */
static int cached_clip_frames[2][CLIP_MAX_EFFECTS]; 
static int frame_info[64][CLIP_MAX_EFFECTS];	/* array holding frame lengths  */
static int primary_frame_len[1];		/* array holding length of top frame */
static uint8_t *audio_buffer[CLIP_MAX_EFFECTS];	/* the audio buffer */
static uint8_t *top_audio_buffer;
static uint8_t *tmp_audio_buffer;
static uint8_t *bad_audio;
static uint8_t *temp_buffer[3];
struct ycbcr_frame *record_buffer;	// needed for recording invisible streams
static uint8_t *top_buff;

static el_row **sec_buff;

#define MLIMIT(var, low, high) \
if((var) < (low)) { var = (low); } \
if((var) > (high)) { var = (high); }

int vj_perform_tag_is_cached(int chain_entry, int entry, int tag_id)
{
    int c = 0;
    if( cached_tag_frames[1][0] == tag_id ) return CLIP_MAX_EFFECTS;
    for (c = 0; c <  chain_entry; c++) {	/* in cache by tag_id */
	if (cached_tag_frames[0][c] == tag_id)
	    return c;
    }
    return -1;
}
int vj_perform_clip_is_cached(int nframe, int chain_entry)
{
  int c;
  if(cached_clip_frames[1][0] == nframe) return -1;
  for(c=0; c < chain_entry; c++)
  {
    if(cached_clip_frames[0][c] == nframe) return c;
  }
  return -2;
}

/**********************************************************************
 * return the chain entry where a cached frame is located or -1 if none
 */

void vj_perform_clear_frame_info(int entry)
{
    int c = 0;
    for (c = 0; c < CLIP_MAX_EFFECTS; c++) {
	frame_info[0][c] = 0;
    }
}

/**********************************************************************
 * clear the cache contents pre queuing frames
 */

void vj_perform_clear_cache(int entry)
{
    memset(cached_tag_frames[0], 0 , CLIP_MAX_EFFECTS);
    memset(cached_clip_frames[0], 0, CLIP_MAX_EFFECTS);
    cached_tag_frames[1][0] = 0;
    cached_clip_frames[1][0] = 0;
}

/********************************************************************
 * int vj_perform_increase_tag_frame(...)
 *
 * get ready for next frame, check boundaries 
 * actually fakes the queuing mechanism, it never touches the disk.
 * returns 0 on sucess, -1 on error
 */
int vj_perform_increase_tag_frame(veejay_t * info, long num)
{
    video_playback_setup *settings = info->settings;
    settings->current_frame_num += num;
    if (settings->current_frame_num < settings->min_frame_num) {
	settings->current_frame_num = settings->min_frame_num;
	if (settings->current_playback_speed < 0) {
	    settings->current_playback_speed =
		+(settings->current_playback_speed);
	}
	return -1;
    }
    if (settings->current_frame_num > settings->max_frame_num) {
	settings->current_frame_num = settings->min_frame_num;
	return -1;
    }
    return 0;
}


/********************************************************************
 * int vj_perform_increase_plain_frame(...)
 *
 * get ready for next frame, check boundaries 
 *
 * returns 0 on sucess, -1 on error
 */
int vj_perform_increase_plain_frame(veejay_t * info, long num)
{
    video_playback_setup *settings = info->settings;
    //settings->current_frame_num += num;
    simple_frame_duplicator+=2;
    if (simple_frame_duplicator >= info->sfd) {
	settings->current_frame_num += num;
	simple_frame_duplicator = 0;
    }


    if (settings->current_frame_num < settings->min_frame_num) {
	settings->current_frame_num = settings->min_frame_num;
	        	
	return 0;
    }
    if (settings->current_frame_num > settings->max_frame_num) {
	settings->current_frame_num = settings->max_frame_num;
	return 0;
    }
    return 0;
}

/********************************************************************
 * int vj_perform_increase_clip_frame(...)
 *
 * get read for next frame, check boundaries and loop type
 *
 * returns 0 on sucess, -1 on error
 */
int vj_perform_increase_clip_frame(veejay_t * info, long num)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    int start,end,looptype,speed;
    int ret_val = 1;
    if(num == 0 ) return 1;

    if(clip_get_short_info(info->uc->clip_id,&start,&end,&looptype,&speed)!=0) return -1;

    settings->freeze_mode = clip_get_freeze_mode(info->uc->clip_id);
    settings->freeze_frames_left = clip_get_freeze_nframes(info->uc->clip_id);
    settings->play_frames_left = clip_get_freeze_pframes(info->uc->clip_id);
    settings->current_playback_speed = speed;

    simple_frame_duplicator++;
    switch( settings->freeze_mode ) {
	case CLIP_FREEZE_NONE:
        if (simple_frame_duplicator >= info->sfd) {
	  settings->current_frame_num += num;
  	  simple_frame_duplicator = 0;	
        }
	break;
        case CLIP_FREEZE_PAUSE:
	/* stil frames to play ? */
	if(settings->play_frames_left > 0) {
	  /* yes, increment frame number */
	  settings->play_frames_left --;
	  if(simple_frame_duplicator >= info->sfd) { 
	    settings->current_frame_num += num;
	    simple_frame_duplicator = 0;
	  }
	  clip_set_freeze_pframes(info->uc->clip_id, settings->play_frames_left);
	}
	else { /* no, we must freeze */
	   /* decrement frames left to freeze */
	   settings->freeze_frames_left --;
	   clip_set_freeze_nframes(info->uc->clip_id, settings->freeze_frames_left);
	   if(settings->freeze_frames_left == 0) {
		/* freezing is done , update internal variables */
		clip_reset_freeze(info->uc->clip_id);
		/* jump */
		settings->current_frame_num += clip_get_freeze_nframes(info->uc->clip_id);
	   }
	}
	break;
	case CLIP_FREEZE_BLACK:
	/* stil frames to play ? */
	if(settings->play_frames_left > 0) {
	  /* yes, increment frame number */
	  settings->play_frames_left --;
	  if(simple_frame_duplicator >= info->sfd) { 
	    settings->current_frame_num += num;
	    simple_frame_duplicator = 0;
	  }
	  clip_set_freeze_pframes(info->uc->clip_id, settings->play_frames_left);
	}
	else { /* no, we must freeze */
	   /* decrement frames left to freeze */
	   settings->freeze_frames_left --;
	   clip_set_freeze_nframes(info->uc->clip_id, settings->freeze_frames_left);
	   if(settings->freeze_frames_left == 0) {
		/* freezing is done , update internal variables */
		clip_reset_freeze(info->uc->clip_id);
		/* jump */
	        settings->current_frame_num += clip_get_freeze_nframes(info->uc->clip_id);
	   }
	   
	   ret_val = 2;
	}
	break;

    }

    if (speed >= 0) {		/* forward play */
	if (settings->current_frame_num > end || settings->current_frame_num < start) {
	    switch (looptype) {
		    case 2:
			info->uc->direction = -1;
			clip_apply_loop_dec( info->uc->clip_id, info->editlist->video_fps);
			veejay_set_frame(info, end);
			veejay_set_speed(info, (-1 * speed));
			break;
		    case 1:
			if(clip_get_loop_dec(info->uc->clip_id)) {
		 	  clip_apply_loop_dec( info->uc->clip_id, info->editlist->video_fps);
			  start = clip_get_startFrame(info->uc->clip_id);
			}
			veejay_set_frame(info, start);
			break;
		    default:
			veejay_set_frame(info, end);
			veejay_set_speed(info, 0);
	    }
	}
    } else {			/* reverse play */
	if (settings->current_frame_num < start || settings->current_frame_num > end || settings->current_frame_num < 0) {
	    switch (looptype) {
	    case 2:
		info->uc->direction = 1;
		if(clip_get_loop_dec(info->uc->clip_id)) {
		  clip_apply_loop_dec( info->uc->clip_id, info->editlist->video_fps);
		  start = clip_get_startFrame(info->uc->clip_id);
		}
		veejay_set_frame(info, start);
		veejay_set_speed(info, (-1 * speed));
		break;

	    case 1:
	  	clip_apply_loop_dec( info->uc->clip_id, info->editlist->video_fps);
		veejay_set_frame(info, end);
		break;
	    default:
		veejay_set_frame(info, start);
		veejay_set_speed(info, 0);
	    }
	}
    }
    clip_update_offset( info->uc->clip_id, settings->current_frame_num );	

    return ret_val;
}



/********************************************************************
 * int vj_perform_init( veejay_t *info )
 * 
 * intializes frame_buffer, this buffer is used to put frames and 
 * render those frames into a final buffer.
 *
 * the decode_buffer holds video data from file, in case of
 * v4l /vloopback/yuv4mpeg we write directly to frame_buffer 
 *
 * initializes dv decoder and ffmpeg mjpeg decoder
 *
 * returns 0 on success, -1 on error
 */

static int vj_perform_el_buff_alloc(veejay_t *info,int frame,int c)
{
    sec_buff[0]->row[c] = (uint8_t*) vj_malloc (sizeof(uint8_t) * (EL_MIN_BUF));
    memset( sec_buff[0]->row[c], 0, EL_MIN_BUF);
    if(!sec_buff[0]->row[c]) return 0;
    return (EL_MIN_BUF);
}
static void vj_perform_el_buff_free(veejay_t *info,int c) 
{
	if(sec_buff[0]->row[c]) free(sec_buff[0]->row[c]);
	sec_buff[0]->row[c] = NULL;
}
static int vj_perform_el_buff_used(int frame, int c)
{
	if(sec_buff[frame]->row[c] == NULL) return 0;
        return 1;
}


static int vj_perform_alloc_row(veejay_t *info, int frame, int c, int frame_len)
{
	frame_buffer[c]->Y =
	    (uint8_t *) vj_malloc(sizeof(uint8_t) * frame_len);
	if(!frame_buffer[c]->Y) return 0;
	memset( frame_buffer[c]->Y , 16, frame_len );
	frame_buffer[c]->Cb =
	    (uint8_t *) vj_malloc(sizeof(uint8_t) * frame_len);
	if(!frame_buffer[c]->Cb) return 0;
	memset( frame_buffer[c]->Cb, 128, frame_len );
	frame_buffer[c]->Cr =
	    (uint8_t *) vj_malloc(sizeof(uint8_t) * frame_len);
	if(!frame_buffer[c]->Cr) return 0;
	memset( frame_buffer[c]->Cr, 128, frame_len );
	return (frame_len*3);
}

static void vj_perform_free_row(int frame,int c)
{
//	int c = frame * CHAIN_BUFFER_SIZE + cc;
	if(frame_buffer[c]->Y) free(frame_buffer[c]->Y);
	if(frame_buffer[c]->Cb) free(frame_buffer[c]->Cb);
	if(frame_buffer[c]->Cr) free(frame_buffer[c]->Cr);
	frame_buffer[c]->Y = NULL;
	frame_buffer[c]->Cb = NULL;
	frame_buffer[c]->Cr = NULL;
//	also, update cache if appropriate
	cached_clip_frames[0][c] = 0;
	cached_tag_frames[0][c] = 0;
}

static int	vj_perform_row_used(int frame, int c)
{
//	int c = frame * CHAIN_BUFFER_SIZE + cc;
	if(frame_buffer[c]->Y != NULL ) return 1;
	return 0;
}


static int	vj_perform_verify_rows(veejay_t *info, int frame)
{
	int c;
	int w = info->editlist->video_width;
	int h = info->editlist->video_height;
	int has_rows = 0;
        float kilo_bytes = 0;
	if( info->uc->playback_mode == VJ_PLAYBACK_MODE_CLIP)
	{
		if(!clip_get_effect_status(info->uc->clip_id)) return 0;
	}
	else
	{
		if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
		{
			if(!vj_tag_get_effect_status(info->uc->clip_id)) return 0;
		}
		else
		{
			return 0;
		}
	}


	for(c=0; c < CLIP_MAX_EFFECTS; c++)
	{
	  int need_row = 0;
	  int v = (info->uc->playback_mode == VJ_PLAYBACK_MODE_CLIP ? 
			clip_get_effect_any(info->uc->clip_id,c) : vj_tag_get_effect_any(info->uc->clip_id,c));
	  if(v>0)
	  {
		//if(vj_effect_get_extra_frame(v))
		need_row = 1;
	  }

	  if( need_row ) 
	  {
		int t=0,s=0,changed=0;
		if(!vj_perform_el_buff_used(frame,c))
		{
			t = vj_perform_el_buff_alloc(info,frame,c);
			changed = 1;
			if(t <= 0) return -1;
		}

		if(!vj_perform_row_used(frame,c))
		{
			s = vj_perform_alloc_row(info,frame,c,w*h);
			changed = 1;
			if(s <= 0) return -1;
		}
	
		if(changed) kilo_bytes += (float) (t + s) / 1024.0 ;

		has_rows ++;
	  }
	  else
	  {
		// row not needed anymore ??
		int changed = 0;
		if(vj_perform_row_used(frame,c))
		{
			vj_perform_free_row(frame,c);	
			changed = 1;
		}
		if(vj_perform_el_buff_used(frame,c))
		{
			vj_perform_el_buff_free(info,c);
			changed = 1;
		}
	//	if(changed) veejay_msg(VEEJAY_MSG_DEBUG, "(Performer) Freed chain buffer entry %d",c);
		
	  } 
	}
        if(kilo_bytes > 0)
	{
		veejay_msg(VEEJAY_MSG_DEBUG,"(Performer) Acquired %4.2f Kb for processing effect chain",
			kilo_bytes);
	}
	return has_rows;
}


static int vj_perform_record_buffer_init(int entry, int frame_len)
{
	if(record_buffer->Cb==NULL)
	        record_buffer->Cb = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len );
	if(!record_buffer->Cb) return 0;
	if(record_buffer->Cr==NULL)
	        record_buffer->Cr = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len );
	if(!record_buffer->Cr) return 0;

	if(record_buffer->Y == NULL)
		record_buffer->Y = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len);
	if(!record_buffer->Y) return 0;

	memset( record_buffer->Y , 16, frame_len );
	memset( record_buffer->Cb, 128, frame_len );
 	memset( record_buffer->Cr, 128, frame_len );

	return 1;
}

static void vj_perform_record_buffer_free()
{

	if(record_buffer->Y) free(record_buffer->Y);
	record_buffer->Y = NULL;
	if(record_buffer->Cb) free(record_buffer->Cb);
	record_buffer->Cb = NULL;
	if(record_buffer->Cr) free(record_buffer->Cr);
	record_buffer->Cr = NULL;
}


int vj_perform_init(veejay_t * info)
{
    const int w = info->editlist->video_width;
    const int h = info->editlist->video_height;
    const int frame_len = ((w * h)/7) * 8;
    int i,c;

    // buffer used to store encoded frames (for plain and clip mode)
    top_buff = (uint8_t*)   vj_malloc( sizeof(uint8_t) * EL_MIN_BUF); 
    if(!top_buff) return 0;
    memset( top_buff, 0, EL_MIN_BUF );

    // chain entry buffers used to store encoded frames
    sec_buff = (el_row**) vj_malloc(sizeof(el_row*) );
    if(!sec_buff) return 0;
    sec_buff[0] = (el_row*) vj_malloc(sizeof(el_row));
    if(!sec_buff[0]) return 0;

    // make sure sec_buff only contains NULL     
    for(i=0; i < CLIP_MAX_EFFECTS; i++)
	sec_buff[0]->row[i] = NULL;

    primary_frame_len[0] = 0;

    frame_buffer = (struct ycbcr_frame **) vj_malloc(sizeof(struct ycbcr_frame *) * CLIP_MAX_EFFECTS);
    if(!frame_buffer) return 0;

    record_buffer = (struct ycbcr_frame*) vj_malloc(sizeof(struct ycbcr_frame) );
    if(!record_buffer) return 0;
    record_buffer->Y = NULL;
    record_buffer->Cb = NULL;
    record_buffer->Cr = NULL;
    // storing resulting frames
    primary_buffer =
	(struct ycbcr_frame **) vj_malloc(sizeof(struct ycbcr_frame **) * 1); 
    if(!primary_buffer) return 0;
    primary_buffer[0] = (struct ycbcr_frame*) vj_malloc(sizeof(struct ycbcr_frame));
    if(!primary_buffer[0]) return 0;
    primary_buffer[0]->Y = (uint8_t*) vj_malloc(sizeof(uint8_t) * frame_len );
    if(!primary_buffer[0]->Y) return 0;
    memset(primary_buffer[0]->Y, 16, frame_len);
    primary_buffer[0]->Cb = (uint8_t*) vj_malloc(sizeof(uint8_t) * frame_len );
    if(!primary_buffer[0]->Cb) return 0;
    memset(primary_buffer[0]->Cb, 128, frame_len);
    primary_buffer[0]->Cr = (uint8_t*) vj_malloc(sizeof(uint8_t) * frame_len );
    if(!primary_buffer[0]->Cr) return 0;
    memset(primary_buffer[0]->Cr,128, frame_len);

    clip_record_init(frame_len);
    vj_tag_record_init(w,h);

    // to render fading of effect chain:
    temp_buffer[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len );
    if(!temp_buffer[0]) return 0;
	memset( temp_buffer[0], 16, frame_len );
    temp_buffer[1] = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len );
    if(!temp_buffer[1]) return 0;
	memset( temp_buffer[1], 128, frame_len );
    temp_buffer[2] = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len );
    if(!temp_buffer[2]) return 0;
	memset( temp_buffer[2], 128, frame_len );

    /* allocate space for frame_buffer, the place we render effects  in */
    for (c = 0; c < CLIP_MAX_EFFECTS; c++) {
	frame_buffer[c] = (struct ycbcr_frame *) vj_malloc(sizeof(struct ycbcr_frame));
        if(!frame_buffer[c]) return 0;

	frame_buffer[c]->Y = NULL;
	frame_buffer[c]->Cb = NULL;
	frame_buffer[c]->Cr = NULL;
    }
    // clear the cache information
    for(c=0; c < 2; c++)
    {
	memset( cached_tag_frames[c], 0, CLIP_MAX_EFFECTS);
	memset( cached_clip_frames[c], 0, CLIP_MAX_EFFECTS);
    }
	memset( frame_info[0],0,CLIP_MAX_EFFECTS);
	
    return 1;
}


static void vj_perform_close_audio() {
	int i;
	for(i=0; i < CLIP_MAX_EFFECTS; i++)
	{
		if(audio_buffer[i]) free(audio_buffer[i]);
	}	

	if(tmp_audio_buffer) free(tmp_audio_buffer);
	if(top_audio_buffer) free(top_audio_buffer);

	/* temporary buffer */
	if(bad_audio) free(bad_audio);

}

int vj_perform_init_audio(veejay_t * info)
{
  //  video_playback_setup *settings = info->settings;
    int i;

	if(info->audio==AUDIO_PLAY)
	{
 	//vj_jack_start();
	}

	/* top audio frame */
	top_audio_buffer =
	    (uint8_t *) vj_malloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE);
	memset( top_audio_buffer, 0 , PERFORM_AUDIO_SIZE );
	/* chained audio */
	for (i = 0; i < CLIP_MAX_EFFECTS; i++) {
	    audio_buffer[i] =
		(uint8_t *) vj_malloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE);
		memset(audio_buffer[i], 0, PERFORM_AUDIO_SIZE);
	}
	/* temporary buffer */
	bad_audio =
	    (uint8_t *) vj_malloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE *
			       (2 * CLIP_MAX_EFFECTS ));
	memset(bad_audio, 0, PERFORM_AUDIO_SIZE * (2 * CLIP_MAX_EFFECTS));
  	tmp_audio_buffer =
   	   (uint8_t *) vj_malloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE);
  	memset(tmp_audio_buffer, 0, PERFORM_AUDIO_SIZE);



    return 0;
}

void vj_perform_free(veejay_t * info)
{
    int fblen = CLIP_MAX_EFFECTS; // mjpg buf
    int c;
    vj_perform_close_audio();

    if(primary_frame_len) free(primary_frame_len);

    for (c = 0; c < fblen; c++) {
	if(vj_perform_row_used(0,c)) vj_perform_free_row(0,c);
	if(frame_buffer[c])
	{
	 	if(frame_buffer[c]->Y) free(frame_buffer[c]->Y);
		if(frame_buffer[c]->Cb) free(frame_buffer[c]->Cb);
		if(frame_buffer[c]->Cr) free(frame_buffer[c]->Cr);
		free(frame_buffer[c]);
	}
    }

   if(frame_buffer) free(frame_buffer);

   if(primary_buffer[0]->Y) free(primary_buffer[0]->Y);
   if(primary_buffer[0]->Cb) free(primary_buffer[0]->Cb);
   if(primary_buffer[0]->Cr) free(primary_buffer[0]->Cr);
   if(primary_buffer[0]) free(primary_buffer[0]);
   if(primary_buffer) free(primary_buffer);

   for(c=0; c < 3; c ++)
   {
      if(temp_buffer[c]) free(temp_buffer[c]);
   }

   if(sec_buff[0])
   {
	int i;
	for( i = 0; i < CLIP_MAX_EFFECTS; i++)
	{
		if(vj_perform_el_buff_used(0,i)) vj_perform_el_buff_free(info,i);
	}
	free(sec_buff[0]);
    }
   if(top_buff) free(top_buff);
   if(sec_buff) free(sec_buff);

}

/***********************************************************************
 * return the chain entry where a cached frame is located or -1 if none
 */


int vj_perform_audio_start(veejay_t * info)
{
    if (info->editlist->has_audio) {
#ifdef HAVE_JACK
	vj_jack_initialize();

 	if(!vj_jack_init(info->editlist->audio_rate, info->editlist->audio_chans,
		info->editlist->audio_bps))
	{
		info->audio=NO_AUDIO;
		veejay_msg(VEEJAY_MSG_WARNING,
			"Audio playback disabled");
	}
 	return 1;
#else
	veejay_msg(VEEJAY_MSG_INFO, "Jack support not compiled in (no audio)");
	return 0;
#endif
    }
    return 0;
}

void vj_perform_audio_status(struct timeval tmpstmp, unsigned int nb_out,
			     unsigned int nb_err)
{
}

void vj_perform_audio_stop(veejay_t * info)
{
    if (info->editlist->has_audio) {
	//audio_shutdown();
#ifdef HAVE_JACK
	vj_jack_stop();
#endif
	info->audio = NO_AUDIO;
    }
}



/********************************************************************
 * vj_perform_get_primary_frame :
 * sets the pointers to the primary frame into **frame
 */
void vj_perform_get_primary_frame(veejay_t * info, uint8_t ** frame,
				  int entry)
{
    frame[0] = primary_buffer[0]->Y;
    frame[1] = primary_buffer[0]->Cb;
    frame[2] = primary_buffer[0]->Cr;
}


void vj_perform_reverse_audio_frame(veejay_t * info, int len,
				    uint8_t * buf)
{
    int i;
    int bps = info->editlist->audio_bps;
    uint8_t clip[bps];
    int x=len*bps;
    for( i = 0; i < x/2 ; i += bps ) {
		veejay_memcpy(clip,buf+i,bps);	
		veejay_memcpy(buf+i ,buf+(x-i),bps);
		veejay_memcpy(buf+(x-i), clip,bps);
	}
/*
    for (i = 0; i < (len * bps); i += bps) {
	veejay_memcpy(clip, buf + i, bps);
	veejay_memcpy(tmp_audio_buffer + (len * bps) - bps - i, clip, bps);
    }
    veejay_memcpy(buf, tmp_audio_buffer, (len * bps));
	*/
}


int vj_perform_get_subtagframe(veejay_t * info, int sub_clip,
			       int chain_entry)
{

	int a = info->uc->clip_id;
	int b = sub_clip;
	int offset = vj_tag_get_offset(a, chain_entry);
	int clip_b[4];
	int len_b;  

	if(clip_get_short_info(b,&clip_b[0],&clip_b[1],&clip_b[2],&clip_b[3])!=0) return -1;

	len_b = clip_b[1] - clip_b[0];

	if(clip_b[3] >= 0)
	{
		offset += clip_b[3];
		if( offset >= len_b )
		{
			if(clip_b[2]==2)
			{
				offset = 0;
				clip_set_speed(b, (-1 * clip_b[3]));
				vj_tag_set_offset(a, chain_entry, offset);
				return clip_b[1];
			}
			if(clip_b[2]==1)
			{
				offset = 0;
			}
			if(clip_b[2] == 0)
			{
				offset = 0;
				clip_set_speed(b, 0);
			} 
		}
		vj_tag_set_offset(a, chain_entry, offset);
		return (clip_b[0] + offset);
	}
	else
	{
		offset += clip_b[3];
		if ( offset < -(len_b))
		{
			if(clip_b[2] == 2)
			{
				offset = 0;
				clip_set_speed(b, (-1 * clip_b[3]));
				vj_tag_set_offset(a,chain_entry,offset);
				return clip_b[0];
			}
  			if(clip_b[2] == 1)
			{
				offset = 0;
			}
			if(clip_b[2]== 0)
			{
				clip_set_speed(b,0);
				offset = 0;
			}
		}
		vj_tag_set_offset(a,chain_entry, offset);
		return (clip_b[1] + offset);

	}

    return -1;
}
/********************************************************************
 * int vj_perform_use_cached_frame( ... )
 *
 * simply copy the cached frame into the position in the frame
 * frame buffer for further processing.
 */
void vj_perform_use_cached_ycbcr_frame(int entry, int centry, int width,
				       int height, int chain_entry)
{
	// centry = source 
	// chain_entry = destination 
	if(centry == chain_entry)
	{ //dont fell over your own feet
		return;
	}

	if(centry == CLIP_MAX_EFFECTS)
	{
		veejay_memcpy(frame_buffer[chain_entry]->Y,
			      primary_buffer[0]->Y,
			      width * height );
		veejay_memcpy(frame_buffer[chain_entry]->Cb,
			      primary_buffer[0]->Cb,
				(width*height)/4 );
		veejay_memcpy(frame_buffer[chain_entry]->Cr,
			      primary_buffer[0]->Cr, 
				(width*height)/4 );
	}	
	else
	{
		veejay_memcpy(frame_buffer[chain_entry]->Y,
		       frame_buffer[centry]->Y, width * height);
		veejay_memcpy(frame_buffer[chain_entry]->Cb,
		       frame_buffer[centry]->Cb,
		       (width * height/4 ));
		veejay_memcpy(frame_buffer[chain_entry]->Cr,
		       frame_buffer[centry]->Cr,
		       (width * height/4 ));
   	 }
}

int vj_perform_get_subframe(veejay_t * info, int sub_clip,
			    int chain_entry, const int skip_incr)

{
    video_playback_setup *settings = (video_playback_setup*) info->settings;
    int a = info->uc->clip_id;
    int b = sub_clip;
    //int trim_val = clip_get_trimmer(a, chain_entry);

    int clip_a[4];
    int clip_b[4];

    int offset = clip_get_offset(a, chain_entry);	
    int nset = offset;
    int len_a, len_b;
     
	if(clip_get_short_info(b,&clip_b[0],&clip_b[1],&clip_b[2],&clip_b[3])!=0) return -1;

	if(clip_get_short_info(a,&clip_a[0],&clip_a[1],&clip_a[2],&clip_a[3])!=0) return -1;

	len_a = clip_a[1] - clip_a[0];
	len_b = clip_b[1] - clip_b[0];
 
	/* offset + start >= end */
	if(clip_b[3] >= 0) /* sub clip plays forward */
	{
		if(!skip_incr)
		{
			if( settings->current_playback_speed != 0)
	   	 		offset += clip_b[3]; /* speed */
	
			/* offset reached clip end */
    			if(  offset > len_b )
			{
				if(clip_b[2] == 2) /* clip is in pingpong loop */
				{
					/* then set speed in reverse and set offset to clip end */
					//offset = clip_b[1] - clip_b[0];
					offset = 0;
					clip_set_speed( b, (-1 * clip_b[3]) );
					clip_set_offset(a,chain_entry,offset);
					return clip_b[1];
				}
				if(clip_b[2] == 1)
				{
					offset = 0;
				}
				if(clip_b[2] == 0)
				{
					offset = 0;	
					clip_set_speed(b,0);
				}
			}


			clip_set_offset(a,chain_entry,offset);
			return (clip_b[0] + nset);
		}
		else
		{
			return clip_b[0] + nset;
		}
	}
	else
	{	/* sub clip plays reverse */
		if(!skip_incr)
		{
			if(settings->current_playback_speed != 0)
	    			offset += clip_b[3]; /* speed */

			if ( offset < -(len_b)  )
			{
				/* reached start position */
				if(clip_b[2] == 2)
				{
					//offset = clip_b[1] - clip_b[0];
					offset = 0;
					clip_set_speed( b, (-1 * clip_b[3]));
					clip_set_offset(a,chain_entry,offset);
					return clip_b[0];
				}
				if(clip_b[2] == 1)
				{
					//offset = clip_b[1] - clip_b[0];
					offset = 0;
				}	
				if(clip_b[2]== 0)
				{
					clip_set_speed(b , 0);
					offset = 0;
				}
			}
			clip_set_offset(a, chain_entry, offset);
	
			return (clip_b[1] + nset);
		}
		else
		{ 
		       return clip_b[1] + nset;
		}
	}
	return 0;
}

int vj_perform_new_audio_frame(veejay_t * info, char *dst_buf, int nframe,
			       int speed)
{

    int n = nframe;
    int p = speed;
    int j = 0;
    int len = 0;
    int size = 0;
    if ((n - p) > 0) {		/* fast forward, read skipped audio frames */
	if (p > 0) {
	    int s = n - p;
	    int r = 0;
	    int offset = 0;
	    int i = 0;
	    int inc_val = (p % 2) ? 0 : 1;
	    j = 0;
	    for (r = s; r < n; r++) {
		len = el_get_audio_data2(bad_audio + offset, r, info->editlist);	/* 7680 */
		if(len < 0) {
			len = (info->editlist->audio_rate / info->editlist->video_fps);
			mymemset_generic(bad_audio+offset, 0, (len * info->editlist->audio_bps) );
		}
		size = len * info->editlist->audio_bps;

		offset += size;
	    }
	    if (!inc_val) {
		for (i = 0; i < offset; i += p) {
		    dst_buf[j] = bad_audio[i];
		    j++;
		}
	    } else {
		for (i = 1; i < offset; i += p) {
		    dst_buf[j] = bad_audio[i];
		    j++;
		}
	    }
	    
	} /* play reverse, read skipped */
	else {
            int p2 = p * -1;
	    int s = n - p2;
	 
	    int r = 0;
	    int offset = 0;
	    int i = 0;
	    int inc_val = (p2 % 2) ? 0 : 1;
	    j = 0;
	    for (r = s; r < n; r++) {
		len = el_get_audio_data2(bad_audio+offset, r, info->editlist);	/* 7680 */
		//vj_perform_reverse_audio_frame( info, len, bad_audio+offset);
		if(len < 0) {
			len = (info->editlist->audio_rate / info->editlist->video_fps);
			mymemset_generic(bad_audio+offset, 0, (len * info->editlist->audio_bps) );
		}
		size = len * info->editlist->audio_bps;

		offset += size;
		/* read big_audio and put bytes to dst_buf */
	    }
		// this puts some of bad_audio to dst_buf, this is wrong but is nice enough 
	    if (!inc_val) {
		for (i = 0; i < offset; i += p2) {
		    dst_buf[j] = bad_audio[i];
		    j++;
		}
	    } else {
		for (i = 1; i < offset; i += p2) {
		    dst_buf[j] = bad_audio[i];
		    j++;
		}
	    }
	    vj_perform_reverse_audio_frame(info,len,bad_audio+offset);

	}
    }
    return len;
}



int vj_perform_fill_audio_buffers(veejay_t * info, char *audio_buf)
{
    video_playback_setup *settings = info->settings;
    int len = 0;
    int i;
    int top_clip = info->uc->clip_id;
    int top_speed = clip_get_speed(top_clip);
    int vol_a = clip_get_audio_volume(top_clip);
    /* take top frame */
    if (top_speed > 1 || top_speed < -1) {
	len =
	    vj_perform_new_audio_frame(info, audio_buf,
				       settings->current_frame_num,
				       top_speed);
    } else {
	if (top_speed == 0) {
	    len = info->editlist->audio_rate / info->editlist->video_fps;
	    mymemset_generic(audio_buf, 0, (len * info->editlist->audio_bps));
	    return len;
	} else {
	    len =
		el_get_audio_data2(audio_buf,
				   settings->current_frame_num,
				   info->editlist);
	}
    }
    /*
       if(info->sfd) {
       len = vj_perform_new_audio_slowframe( info, top_audio_buffer, settings->current_frame_num ); 
       } */

    if (len <= 0)
	{
		mymemset_generic(audio_buf,0,PERFORM_AUDIO_SIZE);
		return (info->editlist->audio_rate / info->editlist->video_fps);
	}
    if (top_speed < 0)
	vj_perform_reverse_audio_frame(info, len, audio_buf);

    return len;

}

/********************************************************************
 * int vj_perform_decode_frame
 *
 * decodes buffer from disk 'buff' of size 'len' to yuv 4:2:0 format
 * into Y, Cb, Cr.
 *
 * buff is a pointer to where you want the data.
 * returns 0 on sucess, -1 on error
 */

int vj_perform_decode_frame(veejay_t * info, uint8_t * buff, int len,
			    uint8_t *Y, uint8_t *Cb, uint8_t *Cr,
			    int data_format, int sub_clip)
{
    int w = info->editlist->video_width;
    int h = info->editlist->video_height;
    int inter = 0;
    int res = -1;
    //int tmp = 0;
    uint8_t *frame[3];
    frame[0] = Y;
    frame[1] = Cb;
    frame[2] = Cr;

    video_playback_setup *settings = info->settings; 
    if(Y==NULL || Cb==NULL || Cr==NULL ) {
 		veejay_msg(VEEJAY_MSG_ERROR,"Fatal error: Lost pointer to destination buffer,");
		veejay_change_state(info, LAVPLAY_STATE_STOP);
		return -1;
	}
   
    switch (data_format) {
	case DATAFORMAT_MJPG:
	 	if(info->no_ffmpeg==1)
		   res = decode_jpeg_raw(buff,len,inter,420,settings->dct_method,w,h,Y,Cb,Cr);
		else
		   res = vj_ffmpeg_decode_frame(info->decoder,buff,len,Y,Cb,Cr);
	break;
	case DATAFORMAT_YUV420:
		if(len != ( (w * h) + ((w*h)/2) ))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unexpected frame length (%d / %d expected Kbytes)",
				len, ( (w*h)+((w*h)/2)));
 			return -1;
		}

		veejay_memcpy(frame[0], buff, (w*h) );
		veejay_memcpy(frame[1], buff+(w*h), (w*h)/4);
		veejay_memcpy(frame[2], buff+((w*h*5)/4), (w*h)/4);	    
		res = 1;
		break;
	case DATAFORMAT_DIVX:
		res = vj_ffmpeg_decode_frame(info->divx_decoder,buff,len,Y,Cb,Cr);
		break;
	case DATAFORMAT_MPEG4:
		res = vj_ffmpeg_decode_frame(info->mpeg4_decoder,buff,len,Y,Cb,Cr);
		break;
    case DATAFORMAT_DV2:
	vj_dv_decode_frame(buff, Y, Cb, Cr, w, h);
	break;
        default:
 	veejay_msg(VEEJAY_MSG_ERROR, "Unsupported video codec, aborting ...");
	veejay_change_state(info, LAVPLAY_STATE_STOP);

    }
    if( info->editlist->video_inter != LAV_NOT_INTERLACED && info->auto_deinterlace)
    {	
	deinterlace( Y, w,h,0);
	deinterlace( Cb,w/2,h/2);
	deinterlace( Cr,w/2,h/2);
    }

    if(res > 0) return 0;
    return -1;
}




void vj_perform_use_cached_encoded_frame(veejay_t * info, int entry,
					 int centry, int chain_entry)
{
    video_playback_setup *settings = info->settings;

    if (centry == 0) {
	uint8_t *src = top_buff;
	uint8_t *dst =
	    sec_buff[0]->row[chain_entry];
	veejay_memcpy(dst, src, primary_frame_len[0]);
	frame_info[0][chain_entry] = primary_frame_len[0];
    } else {
	uint8_t *dst =
	    sec_buff[0]->row[chain_entry];
	uint8_t *src =
	    sec_buff[0]->row[(chain_entry-centry)];
	int len = frame_info[0][(chain_entry - centry)];
	veejay_memcpy(dst, src, len);
	frame_info[0][chain_entry] = len;
    }
}


int vj_perform_apply_secundary_tag(veejay_t * info, int clip_id,
				   int type, int chain_entry, int entry, const int skip_incr)
{				/* second clip */
    int width = info->editlist->video_width;
    int height = info->editlist->video_height;
    int error = 1;
    int nframe;
    int len = 0;
    uint8_t *frame2[3];
    uint8_t *encoded_frame;
    int df;
    int centry = -2;
    frame2[0] = frame_buffer[chain_entry]->Y;
    frame2[1] = frame_buffer[chain_entry]->Cb;
    frame2[2] = frame_buffer[chain_entry]->Cr;

    switch (type) {		

    case VJ_TAG_TYPE_YUV4MPEG:	/* playing from stream */
    case VJ_TAG_TYPE_V4L:
    case VJ_TAG_TYPE_VLOOPBACK:
    case VJ_TAG_TYPE_RAW:
	centry = vj_perform_tag_is_cached(chain_entry, entry, clip_id);
	
	if (centry == -1) {	/* not cached */
	    if (vj_tag_get_active(clip_id) == 1 ) {
		if(
		 vj_tag_get_frame(clip_id, frame2,
				    audio_buffer[chain_entry])==1) {
		   error = 0;
		   cached_tag_frames[0][chain_entry] = clip_id;
	        }
		else {
		   veejay_msg(VEEJAY_MSG_ERROR, "Cannot read from tag. I will disable tag %d now",clip_id);
		   error = 1;
		   cached_tag_frames[0][chain_entry] = 0;
		   vj_tag_set_active(clip_id, 0);
		}
	     }
	} else {		/* cached, centry has source frame  */
	    vj_perform_use_cached_ycbcr_frame(entry, centry, width, height,
					      chain_entry);
	    error = 0;
	}
	break;
    case VJ_TAG_TYPE_RED:
		dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_RED);
		error = 0;
		break;
    case VJ_TAG_TYPE_WHITE: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_WHITE); error = 0; break;
    case VJ_TAG_TYPE_BLACK: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_BLACK); error = 0; break;
    case VJ_TAG_TYPE_YELLOW: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_YELLOW); error=0; break;
    case VJ_TAG_TYPE_BLUE: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_BLUE); error=0; break;
    case VJ_TAG_TYPE_GREEN: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_GREEN); error=0; break;	
    case VJ_TAG_TYPE_NONE:

 	encoded_frame =
     		sec_buff[0]->row[chain_entry]; // buffer holding encoded image

	    nframe = vj_perform_get_subframe(info, clip_id, chain_entry, skip_incr); // get exact frame number to decode
 	    centry = vj_perform_clip_is_cached(clip_id, chain_entry);
            if(centry == -2)
	    {
		//not cached
	        len =
			el_get_video_frame(encoded_frame, nframe, info->editlist); // and the length of that frame

		    if(len <= 0)
		    {
			error = 1;
			frame_info[0][chain_entry] = 0;  
		    }
		    else
		    {
		    	frame_info[0][chain_entry] = len; 
			error = 0;
		    }
		    df = el_video_frame_data_format( nframe,info->editlist); 
		
    		    if( vj_perform_decode_frame(info,
					     encoded_frame,
					     len,
					     frame2[0],
					     frame2[1],
					     frame2[2],
					     df,
					     el_get_sub_clip_format(info->editlist, nframe)
			) == 0)
			{
				error = 0;
				cached_clip_frames[1][0] = clip_id;
			}
	    }
	    if(centry==-1)
		{
			// find in primary buffer
			veejay_memcpy( frame2[0], primary_buffer[0]->Y, width*height);
			veejay_memcpy( frame2[1], primary_buffer[0]->Cb, (width*height)/4);
			veejay_memcpy( frame2[2], primary_buffer[0]->Cr, (width*height)/4);
			cached_clip_frames[0][chain_entry] = clip_id;
			error =  0;
		}
	    if(centry >= 0 && centry <= CLIP_MAX_EFFECTS)
		{
			veejay_memcpy(frame2[0], frame_buffer[centry]->Y, width*height);
			veejay_memcpy(frame2[1], frame_buffer[centry]->Cb,(width*height)/4);
			veejay_memcpy(frame2[2], frame_buffer[centry]->Cr, (width*height)/4);
			cached_clip_frames[0][chain_entry] = clip_id;
			error =0;
		}

		break;

    }

    if (error == 1) {
	dummy_apply(frame2, width, height, VJ_EFFECT_COLOR_BLACK);
    }


    return 0;
}

/********************************************************************
 * int vj_perform_apply_nsecundary( ... )
 *
 * determines secundary frame by type v4l/yuv/file/vloopback
 * and decodes buffers directly to frame_buffer.
 *
 * returns 0 on success, -1 on error 
 */

int vj_perform_apply_secundary(veejay_t * info, int clip_id, int type,
			       int chain_entry, int entry, const int skip_incr)
{				/* second clip */
    video_playback_setup *settings = info->settings;

    int width = info->editlist->video_width;
    int height = info->editlist->video_height;
    int error = 1;
    int nframe;
    int len;
    int df;
    uint8_t *frame2[3];
    uint8_t *encoded_frame;
    int centry = -2;
    if(chain_entry < 0 || chain_entry >= CLIP_MAX_EFFECTS) return -1;

    frame2[0] = frame_buffer[chain_entry]->Y;
    frame2[1] = frame_buffer[chain_entry]->Cb;
    frame2[2] = frame_buffer[chain_entry]->Cr;


    switch (type) {	// what source ?	
    case VJ_TAG_TYPE_YUV4MPEG:	 // streams
    case VJ_TAG_TYPE_V4L:
    case VJ_TAG_TYPE_VLOOPBACK:
    case VJ_TAG_TYPE_RAW:
	centry = vj_perform_tag_is_cached(chain_entry, entry, clip_id); // is it cached?
	if (centry == -1) { // no it is not
	    if (vj_tag_get_active(clip_id) == 1) { // if it is active (playing)
		if(vj_tag_get_frame(clip_id, frame2,
				    audio_buffer[chain_entry])==1) { // get a ycbcr frame
			error = 0;                               
			cached_tag_frames[0][chain_entry] = clip_id; // frame is cached now , admin it 
		}	
		else {
			error = 1; // something went wrong
			cached_tag_frames[0][chain_entry] = 0;
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot get frame from stream %d, disabling now",clip_id);
			vj_tag_set_active(clip_id, 0); // stop stream
		}
	    }
	} else {
		// it is cached, copy from frame buffer to this chain entry
	    vj_perform_use_cached_ycbcr_frame(entry, centry, width,
					      height, chain_entry);
	    error = 0;
	}
    case VJ_TAG_TYPE_NONE:
	 encoded_frame =
     		sec_buff[0]->row[chain_entry]; // buffer holding encoded image

	    nframe = vj_perform_get_subframe(info, clip_id, chain_entry, skip_incr); // get exact frame number to decode
 	    centry = vj_perform_clip_is_cached(clip_id, chain_entry);
            if(centry == -2)
	    {
		//not cached
	        len =
			el_get_video_frame(encoded_frame, nframe, info->editlist); // and the length of that frame

		    if(len <= 0)
		    {
			error = 1;
			frame_info[0][chain_entry] = 0;  
		    }
		    else
		    {
		    	frame_info[0][chain_entry] = len; 
			error = 0;
		    }
		    df = el_video_frame_data_format( nframe,info->editlist); 
		
    		    if( vj_perform_decode_frame(info,
					     encoded_frame,
					     len,
					     frame2[0],
					     frame2[1],
					     frame2[2],
					     df,
					     el_get_sub_clip_format(info->editlist, nframe)
			) == 0)
			{
				error = 0;
				cached_clip_frames[1][0] = clip_id;
			}
	    }
	    if(centry==-1)
		{
			// find in primary buffer
			veejay_memcpy( frame2[0], primary_buffer[0]->Y, width*height);
			veejay_memcpy( frame2[1], primary_buffer[0]->Cb, (width*height)/4);
			veejay_memcpy( frame2[2], primary_buffer[0]->Cr, (width*height)/4);
			cached_clip_frames[0][chain_entry] = clip_id;
			error =  0;
		}
	    if(centry >= 0 && centry <= CLIP_MAX_EFFECTS)
		{
			veejay_memcpy(frame2[0], frame_buffer[centry]->Y, width*height);
			veejay_memcpy(frame2[1], frame_buffer[centry]->Cb,(width*height)/4);
			veejay_memcpy(frame2[2], frame_buffer[centry]->Cr, (width*height)/4);
			cached_clip_frames[0][chain_entry] = clip_id;
			error =0;
		}

		break;
    case VJ_TAG_TYPE_RED: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_RED); error = 0; break;
    case VJ_TAG_TYPE_WHITE: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_WHITE); error = 0; break;
    case VJ_TAG_TYPE_BLACK: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_BLACK); error = 0; break;
    case VJ_TAG_TYPE_YELLOW: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_YELLOW); error=0; break;
    case VJ_TAG_TYPE_BLUE: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_BLUE); error=0; break;
    case VJ_TAG_TYPE_GREEN: dummy_apply(frame2,width,height,VJ_EFFECT_COLOR_GREEN);error=0;break;	

    }

    if (error == 1) {
	dummy_apply(frame2, width, height, 1);
    }


    return 0;
}

/********************************************************************
 * int vj_perform_clip_complete(veejay_t *info)
 *
 * this function assumes the data is ready and waiting to be processed
 * it reads the effect chain and performs associated actions.
 *
 * returns 0 on success 
 */



int vj_perform_clip_complete_buffers(veejay_t * info, int entry, const int skip_incr)
{
    int chain_entry;
    if (clip_get_effect_status(info->uc->clip_id)!=1)
	return 0;		/* nothing to do */
    int num_frames = 0;
    /* fill frame_buffer with secundary clip or tag frames */

    for (chain_entry = 0; chain_entry < CLIP_MAX_EFFECTS; chain_entry++) {
	if (clip_get_chain_status(info->uc->clip_id, chain_entry) !=0) {  // effect is enabled on chain entry
	    int effect_id =
		clip_get_effect_any(info->uc->clip_id, chain_entry); // what effect is enabled
	    if (effect_id > 0) {
		if (vj_effect_get_extra_frame(effect_id)) { // does it require more sources
		    int sub_id =
			clip_get_chain_channel(info->uc->clip_id,
						 chain_entry); // what id
		    int source =
			clip_get_chain_source(info->uc->clip_id, // what source type
						chain_entry);
		    vj_perform_apply_secundary(info,sub_id,source,chain_entry,entry, skip_incr); // get it
		}
		num_frames++;
	    }
	}
    }
    return num_frames;
}


/********************************************************************
 * int vj_perform_tag_complete(veejay_t *info)
 *
 * this function assumes the data is ready and waiting to be processed
 * it reads the effect chain and performs associated actions.
 *
 * returns 0 on success 
 */
int vj_perform_tag_complete_buffers(veejay_t * info, int entry, const int skip_incr)
{
    int chain_entry;
    int sub_mode = 0;
    int num_frames = 0;
    if (vj_tag_get_effect_status(info->uc->clip_id)!=1)
	return num_frames;		/* nothing to do */
    /* fill frame_buffer with secundary clip or tag frames */

    for (chain_entry = 0; chain_entry < CLIP_MAX_EFFECTS; chain_entry++) {
	if (vj_tag_get_chain_status(info->uc->clip_id, chain_entry) != 0) {
	    int effect_id =
		vj_tag_get_effect(info->uc->clip_id, chain_entry);
	    if (effect_id > 0) {
		sub_mode = vj_effect_get_subformat(effect_id);
		if (vj_effect_get_extra_frame(effect_id)) {	/* yes, extra frame to decode */
		    int sub_id =
			vj_tag_get_chain_channel(info->uc->clip_id,
						 chain_entry);
		    int source =
			vj_tag_get_chain_source(info->uc->clip_id,
						chain_entry);
		    vj_perform_apply_secundary_tag
			(info, sub_id, source, chain_entry, entry, skip_incr);
			
		}
		num_frames++;
	    }
	}
    }

    return num_frames;
}


/********************************************************************
 * decodes plain video, does not touch frame_buffer
 *
 */

void vj_perform_plain_fill_buffer(veejay_t * info, int entry)
{
    video_playback_setup *settings = info->settings;
    const int buf_len =
	el_get_video_frame(top_buff ,
			   settings->current_frame_num, info->editlist);
    primary_frame_len[0] = buf_len;
    if (buf_len <= 0) {
	el_debug_this_frame(info->editlist, settings->current_frame_num);
    }
}

/********************************************************************
 * decodes primary tag video. 
 * no decode needed for this operation.
 */


static long last_rendered_frame = 0;
int vj_perform_render_clip_frame(veejay_t *info, uint8_t *frame[3])
{
	int res = -1;
	int audio_len = 0;
	//uint8_t buf[16384];
	long nframe = info->settings->current_frame_num;
	uint8_t *_audio_buffer;
	if(last_rendered_frame == nframe) return 0; // skip frame 
	
	last_rendered_frame = info->settings->current_frame_num;


	if( clip_use_external(info->uc->clip_id) )
	{
		_audio_buffer = tmp_audio_buffer;
		audio_len = clip_read_external(info->uc->clip_id, _audio_buffer);
	}
	else
	{
		_audio_buffer = top_audio_buffer;
		if(info->editlist->has_audio) 
			audio_len = (info->editlist->audio_rate / info->editlist->video_fps);
	}

	switch( clip_get_encoder_format(info->uc->clip_id)) 
	{
		case DATAFORMAT_DV2:
		case DATAFORMAT_MJPG:
			res = clip_record_frame( info->encoder, info->uc->clip_id, frame,
				_audio_buffer,
				audio_len 
				);
			break;
		case DATAFORMAT_DIVX:
			res = clip_record_frame( info->divx_encoder,
				info->uc->clip_id,frame,_audio_buffer,audio_len);
			break;
		case DATAFORMAT_MPEG4:
			res = clip_record_frame( info->mpeg4_encoder,
				info->uc->clip_id,frame,_audio_buffer,audio_len);
			break;
		case DATAFORMAT_YUV420:
			res = clip_record_frame( NULL, info->uc->clip_id, frame,_audio_buffer, audio_len );
			break;
		default:
			veejay_msg(VEEJAY_MSG_ERROR,"<internal> : invalid data format");
			break;
	}
	return res;
}	
int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[3])
{
	long nframe = info->settings->current_frame_num;

	if(last_rendered_frame == nframe) return 0; // skip frame 
	
	last_rendered_frame = info->settings->current_frame_num;

	return vj_tag_record_frame( info->uc->clip_id, frame, NULL, 0);
}	

void vj_perform_record_commit_clip(veejay_t *info,long start_el_pos,int len)
{
 //video_playback_setup *settings = info->settings;
 clip_info *skel;

 long el_pos = start_el_pos;

 if(start_el_pos==-1)
 {
 	el_pos = info->editlist->video_frames - len;//+1
 }

 skel = clip_skeleton_new( el_pos, len + el_pos -1 );//-1
 // figure out true starting position
 if(skel)
 {
	if(clip_store(skel)==0)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Created new clip [%d] [%d-%d], total frames in el:%ld",
		skel->clip_id, clip_get_startFrame(skel->clip_id), clip_get_endFrame(skel->clip_id),
			info->editlist->video_frames - 1 );
		clip_set_looptype( skel->clip_id, 1 );
	} 

  }

}

long vj_perform_record_commit_single(veejay_t *info, int entry)
{
  //video_playback_setup *settings = info->settings;

  char filename[255];
  //int n_files = 0;
  long start_el_pos = info->editlist->video_frames;

  if(info->uc->playback_mode == VJ_PLAYBACK_MODE_CLIP)
  {
 	 if(clip_get_encoded_file(info->uc->clip_id, filename))
  	{
		long dest = info->editlist->video_frames;
		if( veejay_edit_addmovie(info, filename, -1, dest,dest))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Added file %s", filename);
		}
 	 }
	  return start_el_pos;
  }
  if(info->uc->playback_mode==VJ_PLAYBACK_MODE_TAG)
  {
 	 if(vj_tag_get_encoded_file(info->uc->clip_id, filename))
  	 {
		long dest = info->editlist->video_frames;
		if( veejay_edit_addmovie(info, filename, -1, dest,dest))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Added file %s", filename);
		}
	}
  }
  return start_el_pos;

}

void vj_perform_record_stop(veejay_t *info)
{
 video_playback_setup *settings = info->settings;
 if(info->uc->playback_mode==VJ_PLAYBACK_MODE_CLIP)
 {
	 clip_reset_encoder(info->uc->clip_id);
	 clip_reset_autosplit(info->uc->clip_id);
 	 if( settings->clip_record && settings->clip_record_switch)
	 {
		settings->clip_record_switch = 0;
		veejay_set_clip( info,clip_size()-1);
	 }
	 else
         {
 		veejay_msg(VEEJAY_MSG_INFO,"Not autoplaying new clip");
         }

	 settings->clip_record = 0;
	 settings->clip_record_id = 0;
	 settings->clip_record_switch =0;
 }

 if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
 {
	vj_tag_reset_encoder(info->uc->clip_id);
        vj_tag_reset_autosplit(info->uc->clip_id);
	if( settings->tag_record && settings->tag_record_switch )
	{
		settings->tag_record_switch = 0;
		info->uc->playback_mode = VJ_PLAYBACK_MODE_CLIP;
		settings->tag_record = 0;
		veejay_set_clip(info ,clip_size()-1);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Not autoplaying new clip");
	}
 }

 if( settings->offline_record && settings->offline_created_clip )
 {
	info->uc->playback_mode = VJ_PLAYBACK_MODE_CLIP;
        settings->offline_record = 0;
	settings->offline_tag_id = 0;
	settings->offline_created_clip = 0;
	veejay_set_clip(info, clip_size()-1);
 }

}


void vj_perform_record_clip_frame(veejay_t *info, int entry) {
	//video_playback_setup *settings = info->settings;
	uint8_t *frame[3];
	int res = 0;
	frame[0] = primary_buffer[entry]->Y;
	frame[1] = primary_buffer[entry]->Cb;
	frame[2] = primary_buffer[entry]->Cr;

	
	res = vj_perform_render_clip_frame(info, frame);

	if( res == 2)
	{
		/* auto split file */
		int df = vj_event_get_video_format();
		int len = clip_get_total_frames(info->uc->clip_id);
		long frames_left = clip_get_frames_left(info->uc->clip_id) ;
		// stop encoder
		veejay_msg(VEEJAY_MSG_INFO, "Creating new file (reached 2gb AVI limit)");
		clip_stop_encoder( info->uc->clip_id );
		// close file, add to editlist
		vj_perform_record_commit_single( info, entry );
		// clear encoder
		clip_reset_encoder( info->uc->clip_id );
		// initialize a encoder
		if(frames_left > 0 )
		{
			if( clip_init_encoder( info->uc->clip_id, NULL,
				df, info->editlist, frames_left)==-1)
			{
				veejay_msg(VEEJAY_MSG_ERROR,
				"Error while auto splitting "); 
			}
		}
		else
		{
			vj_perform_record_commit_clip(info,-1,len);
		}
	 }

	
	 if( res == 1)
	 {
		int len = clip_get_total_frames(info->uc->clip_id);
		clip_stop_encoder(info->uc->clip_id);
		vj_perform_record_commit_single( info, entry );	    
		vj_perform_record_commit_clip(info, -1,len);
		vj_perform_record_stop(info);
	 }

	 if( res == -1)
	{
		clip_stop_encoder(info->uc->clip_id);
		vj_perform_record_stop(info);
 	}
}


void vj_perform_record_tag_frame(veejay_t *info, int entry) {
	video_playback_setup *settings = info->settings;
	uint8_t *frame[3];
	int res = 0;

        if(settings->offline_record)
	{
		frame[0] = record_buffer->Y;
		frame[1] = record_buffer->Cb;
		frame[2] = record_buffer->Cr;
	}
	else
	{
		frame[0] = primary_buffer[entry]->Y;
		frame[1] = primary_buffer[entry]->Cb;
		frame[2] = primary_buffer[entry]->Cr;
	}
	
	res = vj_perform_render_tag_frame(info, frame);

	if( res == 2)
	{
		/* auto split file */
		int df = vj_event_get_video_format();
		int len = vj_tag_get_total_frames(info->uc->clip_id);
		long frames_left = vj_tag_get_frames_left(info->uc->clip_id) ;
		// stop encoder
		vj_tag_stop_encoder( info->uc->clip_id );
		// close file, add to editlist
		vj_perform_record_commit_single( info, entry );
		// clear encoder
		vj_tag_reset_encoder( info->uc->clip_id );
		// initialize a encoder
		if(frames_left > 0 )
		{
			if( vj_tag_init_encoder( info->uc->clip_id, NULL,
				df, frames_left)==-1)
			{
				veejay_msg(VEEJAY_MSG_INFO,
				"Error while auto splitting "); 
			}
		}
		else
		{
			vj_perform_record_commit_clip(info,-1,len);
		}
	 }

	
	 if( res == 1)
	 {
		int len = vj_tag_get_total_frames(info->uc->clip_id);
		vj_tag_stop_encoder(info->uc->clip_id);
		vj_perform_record_commit_single( info, entry );	    
		vj_perform_record_commit_clip(info, -1,len);
		vj_perform_record_stop(info);
	 }

	 if( res == -1)
	{
		vj_tag_stop_encoder(info->uc->clip_id);
		vj_perform_record_stop(info);
 	}

}


int vj_perform_tag_fill_buffer(veejay_t * info, int entry)
{
    int error = 1;
    uint8_t *frame[3];
    frame[0] = primary_buffer[entry]->Y;
    frame[1] = primary_buffer[entry]->Cb;
    frame[2] = primary_buffer[entry]->Cr;

     if (vj_tag_get_active(info->uc->clip_id) == 1) {
	int tag_id = info->uc->clip_id;
	video_playback_setup *settings = info->settings;
	if(settings->offline_tag_id == info->uc->clip_id && settings->offline_record) {
	    uint8_t *rframe[3];
	    int w = info->editlist->video_width;
	    int h = info->editlist->video_height;

	    if(!settings->offline_ready)  
	    {
		vj_perform_record_buffer_init(entry, (w*h) ); 
		settings->offline_ready = 1;
		veejay_msg(VEEJAY_MSG_DEBUG, "Allocated memory for recording non visible stream");
	    }   

	    rframe[0] = record_buffer->Y;
	    rframe[1] = record_buffer->Cb;
	    rframe[2] = record_buffer->Cr;
	    veejay_memcpy( frame[0], rframe[0], (w*h));
	    veejay_memcpy( frame[1], rframe[1], (w*h)>>2);
	    veejay_memcpy( frame[2], rframe[2], (w*h)>>2);
	    error = 0; //entry
	    cached_tag_frames[1][0] = tag_id;
	}
        else {
	  if(settings->offline_ready)
	  {
		vj_perform_record_buffer_free();
		settings->offline_ready = 0;
          }
	  if (vj_tag_get_frame(tag_id, frame, top_audio_buffer))
	  {
	    error = 0;
	    cached_tag_frames[1][0] = tag_id;
   	  }
             }

	}
    if (error == 1 && info->video_out != 5) {
	dummy_apply(frame,
		    info->editlist->video_width,
		    info->editlist->video_height, VJ_EFFECT_COLOR_BLACK);
    }
    return (error == 1 ? -1 : 0);
}

/* vj_perform_pre_fade:
   prior to fading, we copy the orginal (unaffected) image to a tempory buffer */
void vj_perform_pre_chain(veejay_t *info, uint8_t *frame[3], int w, int h) {
        int frame_size = w * h;
	veejay_memcpy( temp_buffer[0] , frame[0], frame_size );
	veejay_memcpy( temp_buffer[1], frame[1], frame_size/4 );
	veejay_memcpy( temp_buffer[2], frame[2], frame_size/4 ); /* /4 */
}
/* vj_perform_post_chain:
   after completing the effect chain , we blend a with b
   and update the internal variables
  */
void vj_perform_post_chain(veejay_t *info, uint8_t *dst[3], int w, int h) {
   //video_playback_setup *settings = info->settings;
   unsigned int opacity; 
   if(info->uc->playback_mode==VJ_PLAYBACK_MODE_CLIP) {
     opacity = (int) clip_apply_fader_inc(info->uc->clip_id);
     opacity_apply(dst,temp_buffer,w,h,opacity);
     if(opacity >= 255) {
      if(clip_get_fader_direction(info->uc->clip_id)) {
	clip_set_effect_status(info->uc->clip_id, 0);
        }
      clip_reset_fader(info->uc->clip_id);
      veejay_msg(VEEJAY_MSG_INFO, "Clip Chain Fade done");
     }
   }
   else {
     opacity = (int) vj_tag_apply_fader_inc(info->uc->clip_id);
     opacity_apply(dst, temp_buffer, w, h, opacity);
     if(opacity >= 255) {
 	if (vj_tag_get_fader_direction(info->uc->clip_id)) {
		vj_tag_set_effect_status(info->uc->clip_id,0);
	  }
	vj_tag_reset_fader(info->uc->clip_id);
	veejay_msg(VEEJAY_MSG_INFO, "Stream Chain Fade done");
     }
   }
}


/********************************************************************
 *
 * vj_perform_tag_render_buffers( .... )
 *
 * walks through the effect chain and performs actions on
 * data in frame_buffer.
 *
 * when required, it superclips or subclips a buffer.
 *
 * returns 0 on success, -1 on error
 */
int vj_perform_tag_render_buffers(veejay_t * info, int processed_entry)
{
    vj_clip_instr *setup = info->effect_info;
    vj_video_block *data = info->effect_data;
    int chain_entry, sub_mode = 0, super_clipd = 0;
    uint8_t *frame_a[3];
    int chain_fade = vj_tag_get_fader_active(info->uc->clip_id);
    video_playback_setup *settings = info->settings;
    if (vj_tag_get_effect_status(info->uc->clip_id)!=1)
	return 0;

    setup->clip_id = info->uc->clip_id;
    setup->is_tag = 1;

    data->width = info->editlist->video_width;
    data->height = info->editlist->video_height;

    frame_a[0] = primary_buffer[processed_entry]->Y;
    frame_a[1] = primary_buffer[processed_entry]->Cb;
    frame_a[2] = primary_buffer[processed_entry]->Cr;

    data->src2 = data->src1 = frame_a;

    if ( chain_fade ) {
	vj_perform_pre_chain(info, frame_a, data->width, data->height);
     }
    
    for (chain_entry = 0; chain_entry < CLIP_MAX_EFFECTS; chain_entry++) {
	if (vj_tag_get_chain_status(info->uc->clip_id, chain_entry) != 0) {
	    int effect_id =
		vj_tag_get_effect(info->uc->clip_id, chain_entry);
	    if (effect_id > 0) {
		sub_mode = vj_effect_get_subformat(effect_id);
		if (sub_mode == 1 && super_clipd == 0) {
		    chroma_supersample( settings->sample_mode , data->src1,
				       data->width, data->height);
		    super_clipd = 1;
		} else if (sub_mode == 0 && super_clipd == 1) {
		    chroma_subsample( settings->sample_mode , data->src1, data->width,
				     data->height);
		    super_clipd = 0;
		}

		if (vj_effect_get_extra_frame(effect_id)) {
		    uint8_t *frame[3];	/* get the pointers right */
		    frame[0] = frame_buffer[chain_entry]->Y;
		    frame[1] = frame_buffer[chain_entry]->Cb;
		    frame[2] = frame_buffer[chain_entry]->Cr;
		    data->src4 = data->src3 = frame;	/* fixme: this can be more simplistic and better */

		    if (sub_mode == 1) {
			chroma_supersample(settings->sample_mode, data->src3,
					   data->width, data->height);
		    }
		}
		vj_effman_apply_first(setup, data, effect_id, chain_entry, (int) settings->current_frame_num);
		    }
	}
    }

    if (super_clipd == 1) {
	chroma_subsample(settings->sample_mode, data->src1, data->width,
			 data->height);
    }

    if(chain_fade) {
       vj_perform_post_chain(info, data->src1, data->width, data->height );
    }

    vj_perform_clear_frame_info(processed_entry);

    return 0;
}


int vj_perform_clip_render_buffers(veejay_t * info, int processed_entry)
{
    vj_clip_instr *setup = info->effect_info;
    vj_video_block *data = info->effect_data;
    video_playback_setup *settings = info->settings;
    int chain_entry;
    int sub_mode = 0;
    int super_clipd = 0;
    int chain_fade = 0; 
    uint8_t *frame_a[3];
    if (clip_get_effect_status(info->uc->clip_id)==0)
	return 0;
    if (!clip_exists(info->uc->clip_id))
	return -1;

    setup->clip_id = info->uc->clip_id;
    setup->is_tag = 0;

    data->width = info->editlist->video_width;
    data->height = info->editlist->video_height;
    /* primary buffer */
    frame_a[0] = primary_buffer[0]->Y;
    frame_a[1] = primary_buffer[0]->Cb;
    frame_a[2] = primary_buffer[0]->Cr;
    data->src2 = data->src1 = frame_a;
    chain_fade = clip_get_fader_active(info->uc->clip_id);
    if(chain_fade) {
	vj_perform_pre_chain( info, frame_a, data->width,data->height);
	}

    for (chain_entry = 0; chain_entry < CLIP_MAX_EFFECTS; chain_entry++) {
	if (clip_get_chain_status(info->uc->clip_id, chain_entry) != 0) {
	    int effect_id =
		clip_get_effect_any(info->uc->clip_id, chain_entry);
	    if (effect_id > 0) {
		sub_mode = vj_effect_get_subformat(effect_id);
		if (sub_mode == 1 && super_clipd == 0) {
		    chroma_supersample(settings->sample_mode, data->src1,
				       data->width, data->height);
		    super_clipd = 1;
		} else if (sub_mode == 0 && super_clipd == 1) {
		    chroma_subsample(settings->sample_mode, data->src1, data->width,
				     data->height);
		    super_clipd = 0;
		}

		if (vj_effect_get_extra_frame(effect_id)) {
		    uint8_t *frame[3];
		    frame[0] = frame_buffer[chain_entry]->Y;
		    frame[1] = frame_buffer[chain_entry]->Cb;
		    frame[2] = frame_buffer[chain_entry]->Cr;
		    data->src4 = data->src3 = frame;
		    if (sub_mode == 1) {
			chroma_supersample(settings->sample_mode, data->src3,
					   data->width, data->height);
		    }
		}
		//fprintf(stderr, "Apply [%d] (%d)\n",effect_id,sub_mode);
		vj_effman_apply_first(setup, data, effect_id, chain_entry, (int) settings->current_frame_num);

	    }
	}
    }

    if (super_clipd == 1) {
	chroma_subsample(settings->sample_mode, data->src1, data->width,
			 data->height);
    }
    if (chain_fade) {
	vj_perform_post_chain(info,data->src1, data->width, data->height);
	}
    return 0;
}


int vj_perform_decode_primary(veejay_t * info, int entry)
{

    video_playback_setup *settings = info->settings;
    int error = 1;
    if(entry!=0) veejay_msg(VEEJAY_MSG_ERROR, "This is no good at all");
    if(entry < 0) return -1;
    if (primary_frame_len[0] <= 0) {
	error = 1;
    } else {
	if(primary_buffer[0]->Cr==NULL || primary_buffer[0]->Cb == NULL ||
		primary_buffer[0]->Y==NULL) {
		veejay_msg(VEEJAY_MSG_ERROR, "This should never happen: NULL pointer in primary_buffer");
		veejay_change_state(info,LAVPLAY_STATE_STOP);
		return -1;
		}
	
	if(settings->pure_black) {
		mymemset_generic( primary_buffer[0]->Y , 16, (info->editlist->video_width*info->editlist->video_height));
		mymemset_generic( primary_buffer[0]->Cb, 128,(info->editlist->video_width*info->editlist->video_height) >> 2);
		mymemset_generic( primary_buffer[0]->Cr, 128,(info->editlist->video_width*info->editlist->video_height) >> 2);
		error=0;
	}
	else {
	if (vj_perform_decode_frame(info,
				    top_buff ,
				    primary_frame_len[0], primary_buffer[0]->Y,
				    primary_buffer[0]->Cb, primary_buffer[0]->Cr,
				    el_video_frame_data_format(settings->
							       current_frame_num,
							       info->editlist),
				    el_get_sub_clip_format(info->editlist,settings->current_frame_num)
	    ) == 0) {
	    error = 0;
	
	  	}
	}
    }
    if (error != 0) {
	return -1;
    }

 
    return 1;
}
int vj_perform_decode_tag_secundary(veejay_t * info, int entry,
				    int chain_entry, int type,
				    int clip_id)
{
    video_playback_setup *settings = info->settings;


    int error = 1;

    int len;

    uint8_t *encoded_frame;
    uint8_t *frame2[3];
	// FIXME
    int result;
    long sec_frame;
    int df;

/*
    if (type == VJ_TAG_TYPE_NONE) {
	if (clip_exists(clip_id) != 1)
	    return -1;
    } else {
	if (vj_tag_exists(clip_id) != 1)
	    return -1;
	else
	    return 0;
    }
*/
    sec_frame = clip_get_startFrame(clip_id) + clip_get_offset(clip_id, chain_entry);
    df = el_video_frame_data_format( sec_frame, info->editlist );

    len = frame_info[0][chain_entry];
    //HERE
    encoded_frame =
	sec_buff[0]->row[chain_entry];

    frame2[0] = frame_buffer[chain_entry]->Y;
    frame2[1] = frame_buffer[chain_entry]->Cb;
    frame2[2] = frame_buffer[chain_entry]->Cr;
    result = vj_perform_decode_frame(info,
				     encoded_frame,
				     len,
				     frame2[0],
				     frame2[1],
				     frame2[2],
				     df,	
				     el_get_sub_clip_format(info->editlist,settings->current_frame_num)
	);
    if (result == 0)
    	error = 0;

    return 0;
}

int vj_perform_decode_secundary(veejay_t * info, int entry,
				int chain_entry, int type, int clip_id)
{
    video_playback_setup *settings = info->settings;

    
    int error = 1;
	//FIXME
    int len;

    uint8_t *encoded_frame;
    uint8_t *frame2[3];

    int result;
    long sec_frame = 0;
    int df = 0;


    sec_frame = clip_get_startFrame( clip_id ) + clip_get_offset(clip_id, chain_entry);
    df = el_video_frame_data_format( sec_frame,info->editlist); 

    len = frame_info[0][chain_entry];

	//HERE 
   if (info->uc->clip_id == clip_id) {
	encoded_frame = top_buff;
    } else {
	encoded_frame =
	    sec_buff[0]->row[chain_entry];
    }
//  df = el_video_frame_data_format( sec_frame,info->editlist); 


    frame2[0] = frame_buffer[chain_entry]->Y;
    frame2[1] = frame_buffer[chain_entry]->Cb;
    frame2[2] = frame_buffer[chain_entry]->Cr;


    result = vj_perform_decode_frame(info,
				     encoded_frame,
				     len,
				     frame2[0],
				     frame2[1],
				     frame2[2],
					df,
				     //el_video_frame_data_format(settings->
				//				current_frame_num,
				//				info->
				//				editlist),
				     el_get_sub_clip_format(info->editlist, settings->current_frame_num) 
	);

    if (result == 0) {
	error = 0;
    } 

    return 0;
}
int vj_perform_clip_decode_buffers(veejay_t * info, int entry)
{
    int chain_entry;
    /* decode primary frame */
      /* decode secundary frames if using effect chain */
    if (clip_get_effect_status(info->uc->clip_id)==0)
	return 0;		/* nothing to do */
    if (!clip_exists(info->uc->clip_id))
	return -1;

    for (chain_entry = 0; chain_entry < CLIP_MAX_EFFECTS; chain_entry++) {
	if (clip_get_chain_status(info->uc->clip_id, chain_entry) != 0) {
	    int effect_id =
		clip_get_effect_any(info->uc->clip_id, chain_entry);
	
	    if (effect_id > 0) {
		if (vj_effect_get_extra_frame(effect_id)) {
		    int sub_id =
			clip_get_chain_channel(info->uc->clip_id,
						 chain_entry);
		    int source =
			clip_get_chain_source(info->uc->clip_id,
						chain_entry);
			//fprintf(stderr, "decode_buffers: Using channel %d source %d\n",sub_id,source);
			if( source == VJ_TAG_TYPE_NONE)
		  	  vj_perform_decode_secundary(info,entry,chain_entry,source,sub_id);
		}
	    }
	}
    }
    return 0;
}

int vj_perform_tag_decode_buffers(veejay_t * info, int entry)
{
    int chain_entry;

    /* primary is not encoded */
    if (vj_tag_get_effect_status(info->uc->clip_id)==0)
	return 0;		/* nothing to do */

    for (chain_entry = 0; chain_entry < CLIP_MAX_EFFECTS; chain_entry++) {
	if (vj_tag_get_chain_status(info->uc->clip_id, chain_entry) != 0) {
	    int effect_id =
		vj_tag_get_effect(info->uc->clip_id, chain_entry);
	    if (effect_id > 0) {
		if (vj_effect_get_extra_frame(effect_id)) {	/* yes, extra frame to decode */
		    int sub_id =
			vj_tag_get_chain_channel(info->uc->clip_id,
						 chain_entry);
		    int source =
			vj_tag_get_chain_source(info->uc->clip_id,
						chain_entry);
		    if (source == VJ_TAG_TYPE_NONE) {
			vj_perform_decode_tag_secundary
			    (info, entry, chain_entry, source,
			     sub_id);
			 
			
		    }
		}
	    }
	}
    }

    return 0;
}

int vj_perform_queue_audio_frame(veejay_t *info, int frame)
{
  video_playback_setup *settings = info->settings;
  long this_frame = settings->current_frame_num;
  int len1 = 0;
  int tmplen = 0;

#ifdef HAVE_JACK

     /* First, get the audio */
  if (info->audio && info->editlist->has_audio) {
	switch (info->uc->playback_mode) {
		case VJ_PLAYBACK_MODE_CLIP:
		    tmplen = vj_perform_fill_audio_buffers(info,top_audio_buffer);
		    if (tmplen > 0) {
			len1 = tmplen * info->editlist->audio_bps;
		    }
		    break;
		case VJ_PLAYBACK_MODE_PLAIN:
		    if (settings->current_playback_speed == 0)
		    {
		    	memset( top_audio_buffer, 0, PERFORM_AUDIO_SIZE);
			tmplen = (info->editlist->audio_rate/info->editlist->video_fps);
		    }
		    else
		    {
			tmplen =
			    el_get_audio_data2(top_audio_buffer, this_frame,
					       info->editlist);
			if(tmplen < 0)
			{
				memset(top_audio_buffer,0,PERFORM_AUDIO_SIZE);
				tmplen = (info->editlist->audio_rate/info->editlist->video_fps);
			}
		    }
	    	    if (settings->current_playback_speed < 0) {
			vj_perform_reverse_audio_frame(info, tmplen,
					       top_audio_buffer);
	    	    }
	    	    if (tmplen > 0) {
			len1 = tmplen * info->editlist->audio_bps;
	    	    }
	    	break;
		default:
	    		len1 = (info->editlist->audio_rate / info->editlist->video_fps) *
				info->editlist->audio_bps;
			    memset( top_audio_buffer, 0 , len1);
	    	break;
	}

 	/* dump audio frame if required */
        if(info->stream_enabled==1) {
	    vj_yuv_put_aframe(top_audio_buffer, info->editlist, len1);
	}
	if(settings->audio_mute) {
		memset(top_audio_buffer, 0, len1);
	}
	/* play audio to /dev/dsp */
	if(info->audio == AUDIO_PLAY) {
		if(len1>0) {
			 vj_jack_play( top_audio_buffer, len1 );
		}
	}
        return 1;
     }
#endif
     return 1;
}

int vj_perform_queue_video_frame(veejay_t *info, int frame, const int skip_incr)
{
	video_playback_setup *settings = info->settings;
	//long this_frame = settings->current_frame_num;
	//int len1 = 0;
	//int tmplen = 0;
      
	if(frame != 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Fatal, garanteeing segmentation faults");
	}

	primary_frame_len[frame] = 0;
	if(info->video_out==5 && info->render_now == 0) return 1;

        if(settings->offline_record) {
		vj_perform_record_tag_frame(info, frame) ;
	}
		
	switch (info->uc->playback_mode) {
		case VJ_PLAYBACK_MODE_CLIP:
		    vj_perform_plain_fill_buffer(info, frame);	/* primary frame */
		    vj_perform_decode_primary(info,frame);	
		    cached_clip_frames[1][0] = info->uc->clip_id;
		    if(vj_perform_verify_rows(info,frame))
		    {		
		   	 if(	vj_perform_clip_complete_buffers(info, frame, skip_incr)>0)
		   	 {
		    		vj_perform_clip_render_buffers(info,frame);
		    	}
		    }
		    if(clip_encoder_active(info->uc->clip_id))
		    {
			vj_perform_record_clip_frame(info,frame);
		    } 
		    return 1;
		    break;
		case VJ_PLAYBACK_MODE_PLAIN:
		    vj_perform_plain_fill_buffer(info, frame);
		    vj_perform_decode_primary(info,frame);
		    return 1;
 		    break;
		case VJ_PLAYBACK_MODE_TAG:
		   // vj_perform_clear_cache(frame);
		    if (vj_perform_tag_fill_buffer(info, frame) == 0)
		    {	/* primary frame */
			if(vj_perform_verify_rows(info,frame))
			{
				if(vj_perform_tag_complete_buffers(info, frame, skip_incr)>0)
				{
					//vj_perform_tag_decode_buffers(info,frame);
			     		vj_perform_tag_render_buffers(info,frame);
				}
			}
			if(vj_tag_encoder_active(info->uc->clip_id))
			{
				vj_perform_record_tag_frame(info,frame);
			}
			return 1;
		    }
		    break;
		default:
			return 0;
	}


	return 0;
}


int vj_perform_queue_frame(veejay_t * info, int skip_incr, int frame )
{
	video_playback_setup *settings = (video_playback_setup*) info->settings;
	//long this_frame = settings->current_frame_num;
	
	if(!skip_incr)
	{
		switch(info->uc->playback_mode) {
			case VJ_PLAYBACK_MODE_TAG:
				vj_perform_increase_tag_frame(info, settings->current_playback_speed);
				break;
			case VJ_PLAYBACK_MODE_CLIP: 
		 		if(vj_perform_increase_clip_frame(info,settings->current_playback_speed)==2) {
					settings->pure_black = 1;
		  		}
                  		else {
					settings->pure_black = 0;
		  		}
		  		break;
			case VJ_PLAYBACK_MODE_PLAIN:
					vj_perform_increase_plain_frame(info,settings->current_playback_speed);
				break;
			default:
				veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode");
				veejay_change_state(info, LAVPLAY_STATE_STOP);
				break;
		}
  	        vj_perform_clear_cache(frame);

	
        }	
	return 0;
}


