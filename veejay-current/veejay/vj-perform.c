/* 
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
#include <libsample/sampleadm.h>  
#include <libstream/vj-tag.h>
#include <libvjnet/vj-server.h>
#include <libvje/vje.h>
#include <veejay/vj-lib.h>
#include <libel/vj-el.h>
#include <math.h>
#include <libel/vj-avcodec.h>
#include <veejay/vj-event.h>
//#ifdef SUPPORT_READ_DV2
//#include "vj-dv.h"
//#endif
#include <liblavjpeg/jpegutils.h>
#include <libyuv/yuvconv.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vj-perform.h>
#include <veejay/libveejay.h>
#include <libsamplerec/samplerecord.h>
#include <jpeglib.h>
#ifdef HAVE_SAMPLERATE
#include <samplerate.h>
#endif

#define RECORDERS 1
#ifdef HAVE_JACK
#include <veejay/vj-jack.h>
#endif
#include <libvje/internal.h>
#include <libvjmem/vjmem.h>

#define PERFORM_AUDIO_SIZE 16384
static int simple_frame_duplicator;

static int performer_framelen = 0;

struct ycbcr_frame {
    uint8_t *Y;
    uint8_t *Cb;
    uint8_t *Cr;
    uint8_t *alpha;
};

// audio buffer is 16 bit signed integer

static void 	*effect_sampler = NULL;
static void	*crop_sampler = NULL;

static struct ycbcr_frame **video_output_tmp_buffer; 
static struct ycbcr_frame **video_output_buffer; /* scaled video output */
static int	video_output_buffer_convert = 0;
static struct ycbcr_frame **frame_buffer;	/* chain */
static struct ycbcr_frame **primary_buffer;	/* normal */
static VJFrame *crop_frame = NULL;
static int cached_tag_frames[2][CLIP_MAX_EFFECTS];	/* cache a frame into the buffer only once */
static int cached_clip_frames[2][CLIP_MAX_EFFECTS]; 
static int frame_info[64][CLIP_MAX_EFFECTS];	/* array holding frame lengths  */
static int primary_frame_len[1];		/* array holding length of top frame */
static uint8_t *audio_buffer[CLIP_MAX_EFFECTS];	/* the audio buffer */
static uint8_t *top_audio_buffer;
static uint8_t *tmp_audio_buffer;
static uint8_t *x_audio_buffer;
static uint8_t *temp_buffer[3];
static uint8_t *socket_buffer;
struct ycbcr_frame *record_buffer;	// needed for recording invisible streams
static	short	*priv_audio[2];
static VJFrame *helper_frame;

static int vj_perform_record_buffer_init();
static void vj_perform_record_buffer_free();

static ReSampleContext *resample_context[MAX_SPEED];
static ReSampleContext *resample_jack;

#define MLIMIT(var, low, high) \
if((var) < (low)) { var = (low); } \
if((var) > (high)) { var = (high); }

//forward

void	vj_perform_free_plugin_frame(VJFrameInfo *f );


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
	if(!info->continuous)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Reached end of video - stopping ... ");
		veejay_change_state(info, LAVPLAY_STATE_STOP);
	}
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

    settings->current_playback_speed = speed;

    simple_frame_duplicator++;
	if (simple_frame_duplicator >= info->sfd) {
		settings->current_frame_num += num;
    	simple_frame_duplicator = 0;	
    }

    if (speed >= 0) {		/* forward play */
	if (settings->current_frame_num > end || settings->current_frame_num < start) {
	    switch (looptype) {
		    case 2:
			info->uc->direction = -1;
			clip_apply_loop_dec( info->uc->clip_id, info->edit_list->video_fps);
			veejay_set_frame(info, end);
			veejay_set_speed(info, (-1 * speed));
			break;
		    case 1:
			if(clip_get_loop_dec(info->uc->clip_id)) {
		 	  clip_apply_loop_dec( info->uc->clip_id, info->edit_list->video_fps);
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
		  clip_apply_loop_dec( info->uc->clip_id, info->edit_list->video_fps);
		  start = clip_get_startFrame(info->uc->clip_id);
		}
		veejay_set_frame(info, start);
		veejay_set_speed(info, (-1 * speed));
		break;

	    case 1:
	  	clip_apply_loop_dec( info->uc->clip_id, info->edit_list->video_fps);
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


static int vj_perform_alloc_row(veejay_t *info, int frame, int c, int frame_len)
{
	frame_buffer[c]->Y =
	    (uint8_t *) vj_malloc(sizeof(uint8_t) * helper_frame->len);
	if(!frame_buffer[c]->Y) return 0;
	veejay_memset( frame_buffer[c]->Y , 16, helper_frame->len );
	frame_buffer[c]->Cb =
	    (uint8_t *) vj_malloc(sizeof(uint8_t) * helper_frame->len);
	if(!frame_buffer[c]->Cb) return 0;
	veejay_memset( frame_buffer[c]->Cb, 128, helper_frame->len );
	frame_buffer[c]->Cr =
	    (uint8_t *) vj_malloc(sizeof(uint8_t) * helper_frame->len);
	if(!frame_buffer[c]->Cr) return 0;
	veejay_memset( frame_buffer[c]->Cr, 128, helper_frame->len );
	return (3 * helper_frame->len);
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
	int w = info->edit_list->video_width;
	int h = info->edit_list->video_height;
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
		
	  } 
	}
        if(kilo_bytes > 0)
	{
		veejay_msg(VEEJAY_MSG_DEBUG,"(Performer) Acquired %4.2f Kb for processing effect chain",
			kilo_bytes);
	}
	return has_rows;
}


static int vj_perform_record_buffer_init()
{
	if(record_buffer->Cb==NULL)
	        record_buffer->Cb = (uint8_t*)vj_malloc(sizeof(uint8_t) * helper_frame->uv_len );
	if(!record_buffer->Cb) return 0;
	if(record_buffer->Cr==NULL)
	        record_buffer->Cr = (uint8_t*)vj_malloc(sizeof(uint8_t) * helper_frame->uv_len );
	if(!record_buffer->Cr) return 0;

	if(record_buffer->Y == NULL)
		record_buffer->Y = (uint8_t*)vj_malloc(sizeof(uint8_t) * helper_frame->len);
	if(!record_buffer->Y) return 0;

	veejay_memset( record_buffer->Y , 16, helper_frame->len );
	veejay_memset( record_buffer->Cb, 128, helper_frame->uv_len );
 	veejay_memset( record_buffer->Cr, 128, helper_frame->uv_len );

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
/*
int lock_mem( uint8_t *addr, size_t size )
{
	unsigned long page_offset,page_size;
	page_size = sysconf( _SC_PAGE_SIZE);
	page_offset = (unsigned long) addr % page_size; 
	addr -= page_offset;
	size += page_offset;
	return (mlock(addr,size) );
} 

int unlock_mem( uint8_t *addr, size_t size)
{
	unsigned long page_offset,page_size;
	page_size = sysconf( _SC_PAGE_SIZE );
	page_offset = ( unsigned long) addr % page_size;
	addr -= page_offset;
	size += page_offset;
	return ( munlock(addr,size) );
}
*/

int vj_perform_init(veejay_t * info)
{
    const int w = info->edit_list->video_width;
    const int h = info->edit_list->video_height;
    const int frame_len = ((w * h)/7) * 8;
    int c;
    // buffer used to store encoded frames (for plain and clip mode)
    performer_framelen = frame_len *2;
	;
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
    veejay_memset(primary_buffer[0]->Y, 16, frame_len);
    primary_buffer[0]->Cb = (uint8_t*) vj_malloc(sizeof(uint8_t) * frame_len );
    if(!primary_buffer[0]->Cb) return 0;
    veejay_memset(primary_buffer[0]->Cb, 128, frame_len);
    primary_buffer[0]->Cr = (uint8_t*) vj_malloc(sizeof(uint8_t) * frame_len );
    if(!primary_buffer[0]->Cr) return 0;
    veejay_memset(primary_buffer[0]->Cr,128, frame_len);
    video_output_buffer_convert = 0;
    video_output_buffer =
	(struct ycbcr_frame**) vj_malloc(sizeof(struct ycbcr_frame**) * 2 );
    if(!video_output_buffer)
	return 0;

    for(c=0; c < 2; c ++ )
	{
	    video_output_buffer[c] = (struct ycbcr_frame*) vj_malloc(sizeof(struct ycbcr_frame));
	    video_output_buffer[c]->Y = NULL;
	    video_output_buffer[c]->Cb = NULL;
	    video_output_buffer[c]->Cr = NULL; 
	}

    clip_record_init(frame_len);
    vj_tag_record_init(w,h);
    // to render fading of effect chain:
    temp_buffer[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len );
    if(!temp_buffer[0]) return 0;
	veejay_memset( temp_buffer[0], 16, frame_len );
    temp_buffer[1] = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len );
    if(!temp_buffer[1]) return 0;
	veejay_memset( temp_buffer[1], 128, frame_len );
    temp_buffer[2] = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len );
    if(!temp_buffer[2]) return 0;
	veejay_memset( temp_buffer[2], 128, frame_len );
    // to render fading of effect chain:
    socket_buffer = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len * 4 ); // large enough !!
    veejay_memset( socket_buffer, 16, frame_len * 4 );
    // to render fading of effect chain:

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

    helper_frame = (VJFrame*) vj_malloc(sizeof(VJFrame));
    memcpy(helper_frame, info->effect_frame1, sizeof(VJFrame));

    vj_perform_record_buffer_init();

	effect_sampler = subsample_init( w );

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
	if(x_audio_buffer) free(x_audio_buffer);
	if(priv_audio[0]) free(priv_audio[0]);
	if(priv_audio[1]) free(priv_audio[1]);
	if(resample_context)
	{
		int i;
		for(i=1; i <= MAX_SPEED; i ++)
		{
			if(resample_context[(i-1)])
				audio_resample_close( resample_context[(i-1)] );
		}
	}
	if(resample_jack)
		audio_resample_close(resample_jack);
	/* temporary buffer */
	//if(bad_audio) free(bad_audio);

}

int vj_perform_init_audio(veejay_t * info)
{
  //  video_playback_setup *settings = info->settings;
    int i;
#ifndef HAVE_JACK
	return 1;
#endif
	if(info->audio==AUDIO_PLAY)
	{
 	//vj_jack_start();
	}

	/* top audio frame */
	top_audio_buffer =
	    (uint8_t *) vj_malloc(sizeof(uint8_t) * 2 * PERFORM_AUDIO_SIZE);
	if(!top_audio_buffer)
		return 0;
	x_audio_buffer = (uint8_t*) vj_malloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE);
	if(!x_audio_buffer) return 0;
	veejay_memset( x_audio_buffer,0,PERFORM_AUDIO_SIZE);

	veejay_memset( top_audio_buffer, 0 ,2 *  PERFORM_AUDIO_SIZE );
	/* chained audio */
	for (i = 0; i < CLIP_MAX_EFFECTS; i++) {
	    audio_buffer[i] =
		(uint8_t *) vj_malloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE);
		veejay_memset(audio_buffer[i], 0, PERFORM_AUDIO_SIZE);
	}
	/* temporary buffer */
//	bad_audio =
//	    (uint8_t *) vj_malloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE *
//			       (2 * CLIP_MAX_EFFECTS ));
//	memset(bad_audio, 0, PERFORM_AUDIO_SIZE * (2 * CLIP_MAX_EFFECTS));
  	tmp_audio_buffer =
 	   (uint8_t *) vj_malloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * 10);
	if(!tmp_audio_buffer) return 0;
 	veejay_memset(tmp_audio_buffer, 0, PERFORM_AUDIO_SIZE * 10);

	for(i=0; i < 2; i ++)
	{
		priv_audio[i] = (short*) vj_malloc(sizeof(short) * PERFORM_AUDIO_SIZE * 10 );
		if(!priv_audio[i]) return 0;
		veejay_memset( priv_audio[i], 0, PERFORM_AUDIO_SIZE * 10 );
	}

	if(!info->audio) return 0;
#ifdef HAVE_JACK
	for(i=1; i <= MAX_SPEED; i++)
	{
		//veejay_msg(VEEJAY_MSG_DEBUG, "Resampler at %d Hz", (info->edit_list->audio_rate * i));
		int out_rate = info->edit_list->audio_rate * i;
		resample_context[(i-1)] = audio_resample_init(
					info->edit_list->audio_chans,
					info->edit_list->audio_chans, 
					info->edit_list->audio_rate,
					out_rate);
				//	(i * (info->edit_list->audio_rate/2)));
		
		if(!resample_context[(i-1)])
		{
			resample_context[(i-1)] = NULL;
			veejay_msg(VEEJAY_MSG_WARNING, "Cannot initialize resampler");
		}
		veejay_msg(VEEJAY_MSG_DEBUG, "Speed %d -> %d Hz ", i, out_rate );
		//(i * info->edit_list->audio_rate));
	}
#endif
    return 0;
}

void vj_perform_free(veejay_t * info)
{
    int fblen = CLIP_MAX_EFFECTS; // mjpg buf
    int c;

    if(info->edit_list->has_audio)
	    vj_perform_close_audio();

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
   if(socket_buffer) free(socket_buffer);

	if(crop_frame)
	{
		if(crop_frame->data[0]) free(crop_frame->data[0]);
		if(crop_frame->data[1]) free(crop_frame->data[1]);
		if(crop_frame->data[2]) free(crop_frame->data[2]);
	}
   if(crop_sampler)
	subsample_free(crop_sampler);
   if(effect_sampler)
	subsample_free(effect_sampler);

   for(c=0; c < 3; c ++)
   {
      if(temp_buffer[c]) free(temp_buffer[c]);
   }
   vj_perform_record_buffer_free();

	for( c = 0 ; c < 2 ; c ++ )
	{
		if(video_output_buffer[c])
		{
			if(video_output_buffer[c]->Y )
				free(video_output_buffer[c]->Y);
			if(video_output_buffer[c]->Cb )
				free(video_output_buffer[c]->Cb );
			if(video_output_buffer[c]->Cr )
				free(video_output_buffer[c]->Cr );
    			free(video_output_buffer[c]);
		}
	}
}

/***********************************************************************
 * return the chain entry where a cached frame is located or -1 if none
 */

// rate * channels * 2 = 16 bits             
int vj_perform_audio_start(veejay_t * info)
{
    int res;
    editlist *el = info->edit_list;	
    if (info->edit_list->has_audio) {
	
#ifdef HAVE_JACK
	vj_jack_initialize();

 	res = vj_jack_init( info->edit_list );
	if( res <= 0)
	{
		info->audio=NO_AUDIO;
		veejay_msg(VEEJAY_MSG_WARNING,
			"Audio playback disabled (unable to connect to jack)");
		return 0;
	}
	if ( res == 2 )
	{
		// setup resampler context
		veejay_msg(VEEJAY_MSG_WARNING, "Jack plays at %d Hz, resampling audio from %d -> %d",el->play_rate,el->audio_rate,el->play_rate);
		resample_jack = audio_resample_init( el->audio_chans,el->audio_chans, el->play_rate, el->audio_rate);

		if(!resample_jack)
		{
			resample_jack = NULL;
			veejay_msg(VEEJAY_MSG_WARNING, "Cannot initialize resampler for %d -> %d audio rate conversion ",
				el->audio_rate,el->play_rate);
		}
		return 0;

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
    if (info->edit_list->has_audio) {
	//audio_shutdown();
#ifdef HAVE_JACK
	vj_jack_stop();
#endif
	info->audio = NO_AUDIO;
    }
}


void	vj_perform_update_plugin_frame(VJFrame *frame)
{
	frame->data[0] = (uint8_t*) primary_buffer[0]->Y;
	frame->data[1] = (uint8_t*) primary_buffer[0]->Cb;
	frame->data[2] = (uint8_t*) primary_buffer[0]->Cr;
}

VJFrame *vj_perform_init_plugin_frame(veejay_t *info)
{
	editlist *el = info->edit_list;
	VJFrame *frame = (VJFrame*) vj_malloc(sizeof(VJFrame));
	if(!frame) return 0;
	if(info->pixel_format == FMT_422)
		vj_el_init_422_frame( el, frame );
	else
		vj_el_init_420_frame( el, frame ); 

	return frame;
}

VJFrameInfo *vj_perform_init_plugin_frame_info(veejay_t *info)
{
	editlist *el = info->edit_list;
	VJFrameInfo *frame_info = (VJFrameInfo*) vj_malloc(sizeof(VJFrame));
	if(!frame_info) return NULL;
	frame_info->width = el->video_width;
	frame_info->height = el->video_height;
	frame_info->fps = el->video_fps;
	frame_info->timecode = 0;
	frame_info->inverse = 0;
	// todo: add timestamp info
	return frame_info;
}

void	vj_perform_free_plugin_frame(VJFrameInfo *f )
{
	if(f)
		free(f);
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


void	vj_perform_get_output_frame( veejay_t *info, uint8_t **frame )
{
	frame[0] = video_output_buffer[0]->Y;
	frame[1] = video_output_buffer[0]->Cb;
	frame[2] = video_output_buffer[0]->Cr;
}

void	vj_perform_get_crop_dimensions(veejay_t *info, int *w, int *h)
{
	*w = info->edit_list->video_width - info->settings->viewport.left - info->settings->viewport.right;
	*h = info->edit_list->video_height - info->settings->viewport.top - info->settings->viewport.bottom;

}

int	vj_perform_get_cropped_frame( veejay_t *info, uint8_t **frame, int crop )
{
	if(crop)
	{
		VJFrame src;
		memset( &src, 0, sizeof(VJFrame));

		vj_get_yuv_template( &src,
				info->edit_list->video_width,
				info->edit_list->video_height,
				info->pixel_format );

		src.data[0] = primary_buffer[0]->Y;
		src.data[1] = primary_buffer[0]->Cb;
		src.data[2] = primary_buffer[0]->Cr;

		// yuv crop needs supersampled data
		chroma_supersample( info->settings->sample_mode,effect_sampler, src.data, src.width,src.height );
		yuv_crop( &src, crop_frame, &(info->settings->viewport));
		chroma_subsample( info->settings->sample_mode,crop_sampler, crop_frame->data, crop_frame->width, crop_frame->height );
	}

	frame[0] = crop_frame->data[0];
	frame[1] = crop_frame->data[1];
	frame[2] = crop_frame->data[2];

	return 1;
}

int	vj_perform_init_cropped_output_frame(veejay_t *info, VJFrame *src, int *dw, int *dh )
{
	video_playback_setup *settings = info->settings;
	if( crop_frame )
		free(crop_frame);
	crop_frame = yuv_allocate_crop_image( src, &(settings->viewport) );
	if(!crop_frame)
		return 0;

	*dw = crop_frame->width;
	*dh = crop_frame->height;

	crop_sampler = subsample_init( *dw );

	/* enough space to supersample*/
	int i;
	for( i = 0; i < 3; i ++ )
	{
		crop_frame->data[i] = (uint8_t*) vj_malloc(sizeof(uint8_t) * crop_frame->len );
		if(!crop_frame->data[i])
			return 0;
	}
	return 1;
}

void vj_perform_init_output_frame( veejay_t *info, uint8_t **frame,
				int dst_w, int dst_h )
{
    int i;
    for(i = 0; i < 2; i ++ )
	{
	if( video_output_buffer[i]->Y != NULL )
 		free(video_output_buffer[i]->Y );
	if( video_output_buffer[i]->Cb != NULL )
		free(video_output_buffer[i]->Cb );
	if( video_output_buffer[i]->Cr != NULL )
		free(video_output_buffer[i]->Cr );

	video_output_buffer[i]->Y = (uint8_t*)
			vj_malloc(sizeof(uint8_t) * dst_w * dst_h );
	memset( video_output_buffer[i]->Y, 16, dst_w * dst_h );
	video_output_buffer[i]->Cb = (uint8_t*)
			vj_malloc(sizeof(uint8_t) * dst_w * dst_h );
	memset( video_output_buffer[i]->Cb, 128, dst_w * dst_h );
	video_output_buffer[i]->Cr = (uint8_t*)
			vj_malloc(sizeof(uint8_t) * dst_w * dst_h );
	memset( video_output_buffer[i]->Cr, 128, dst_w * dst_h );

	}
	frame[0] = video_output_buffer[0]->Y;
	frame[1] = video_output_buffer[0]->Cb;
	frame[2] = video_output_buffer[0]->Cr;

}




int	vj_perform_send_primary_frame_s(veejay_t *info, int mcast)
{

//	if(!info->settings->use_vims_mcast)
//		return 1;

	if( info->settings->use_vims_mcast &&
		!info->settings->mcast_frame_sender )
	{
		/* dont send frames if nobody is interested */
		return 1; 
	}

	int w = info->edit_list->video_width;
	int h = info->edit_list->video_height;
	int l = (w*h)/4;
	int len = 0;
	int total_len = helper_frame->len + helper_frame->uv_len + helper_frame->uv_len;

	if( !mcast )
	{
		/* peer to peer connection */
		unsigned char info_line[12];
		sprintf(info_line, "%04d %04d %1d", w,h, info->edit_list->pixel_format );
		len = strlen(info_line );
		veejay_memcpy( socket_buffer, info_line, len );
	}
	veejay_memcpy( socket_buffer + len, primary_buffer[0]->Y, helper_frame->len );
	veejay_memcpy( socket_buffer + len + helper_frame->len,
					    primary_buffer[0]->Cb, helper_frame->uv_len );
	veejay_memcpy( socket_buffer + len + helper_frame->len + helper_frame->uv_len ,
					    primary_buffer[0]->Cr, helper_frame->uv_len );

	
	int id = (mcast ? 2: 0);

	if(vj_server_send_frame( info->vjs[id], info->uc->current_link, socket_buffer, len +total_len,
				helper_frame, info->effect_frame_info, info->real_fps )<=0)
	{
		/* frame send error handling */
		veejay_msg(VEEJAY_MSG_ERROR,
		  "Error sending frame");
		/* uncomment below to end veejay session */
	}

	return 1;
}

void	vj_perform_get_output_frame_420p( veejay_t *info, uint8_t **frame, int w, int h )
{
	if(info->pixel_format == FMT_422)
	{
		frame[0] = video_output_buffer[1]->Y;
		frame[1] = video_output_buffer[1]->Cb;
		frame[2] = video_output_buffer[1]->Cr;
			
		uint8_t *src_frame[3];
		src_frame[0] = video_output_buffer[0]->Y;
		src_frame[1] = video_output_buffer[0]->Cb;
		src_frame[2] = video_output_buffer[0]->Cr;

		yuv422p_to_yuv420p2(
				src_frame, frame,w, h );
	}
	else
	{
		frame[0] = video_output_buffer[0]->Y;
		frame[1] = video_output_buffer[0]->Cb;
		frame[2] = video_output_buffer[0]->Cr;
	}
}

int	vj_perform_is_ready(veejay_t *info)
{
	if( info->settings->zoom )
	{
		if( video_output_buffer[0]->Y == NULL ) return 0;
		if( video_output_buffer[0]->Cb == NULL ) return 0;
		if( video_output_buffer[0]->Cr == NULL ) return 0;
	}
	if( primary_buffer[0]->Y == NULL ) return 0;
	if( primary_buffer[0]->Cb == NULL ) return 0;
	if( primary_buffer[0]->Cr == NULL ) return 0;
	return 1;
}

void	vj_perform_unlock_primary_frame( void )
{
	video_output_buffer_convert = 0;
	// call this every cycle
}

void vj_perform_get_primary_frame_420p(veejay_t *info, uint8_t **frame )   
{
	editlist *el = info->edit_list;
	if(info->pixel_format==FMT_422)
	{
		if( video_output_buffer_convert == 0 )
		{
			uint8_t *pframe[3];
			 pframe[0] = primary_buffer[0]->Y;
			 pframe[1] = primary_buffer[0]->Cb;
			 pframe[2] = primary_buffer[0]->Cr;
			 yuv422p_to_yuv420p2( pframe,temp_buffer, el->video_width, el->video_height);
	//		ss_422_to_420( primary_buffer[0]->Cb,
	//			el->video_width/2,
	//			el->video_height );
	//		ss_422_to_420( primary_buffer[0]->Cr,	
	//			el->video_width/2,
	//			el->video_height );	
			video_output_buffer_convert = 1;
		}
		frame[0] = temp_buffer[0];
		frame[1] = temp_buffer[1];
		frame[2] = temp_buffer[2];
		
	}
	else
	{
		frame[0] = primary_buffer[0]->Y;
		frame[1] = primary_buffer[0]->Cb;
		frame[2] = primary_buffer[0]->Cr;
	}
}

int	vj_perform_apply_first(veejay_t *info, vjp_kf *todo_info,
	VJFrame **frames, VJFrameInfo *frameinfo, int e , int c, int n_frame)
{
	int n_a = vj_effect_get_num_params(e);
	int entry = e;
	int err = 0;
	int args[16];

	memset( args, 0 , 16 );

	if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
	{
		if(!vj_tag_get_all_effect_args(todo_info->ref, c, args, n_a ))
			return 1;
	}
	else
	{
		if(!clip_get_all_effect_arg( todo_info->ref, c, args, n_a, n_frame))
			return 1;
	}
	err = vj_effect_apply( frames, frameinfo, todo_info,entry, args );
	
	return err;
}

void vj_perform_reverse_audio_frame(veejay_t * info, int len,
				    uint8_t * buf)
{
    int i;
    int bps = info->edit_list->audio_bps;
    uint8_t clip[bps];
    int x=len*bps;
    for( i = 0; i < x/2 ; i += bps ) {
		veejay_memcpy(clip,buf+i,bps);	
		veejay_memcpy(buf+i ,buf+(x-i-bps),bps);
		veejay_memcpy(buf+(x-i-bps), clip,bps);
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
		veejay_msg(VEEJAY_MSG_WARNING, "Boing");
		return;
	}
	//todo: it would help to know if we would super sample or not
	if(centry == CLIP_MAX_EFFECTS)
	{
		veejay_memcpy(frame_buffer[chain_entry]->Y,
			      primary_buffer[0]->Y,
			      helper_frame->len );
		veejay_memcpy(frame_buffer[chain_entry]->Cb,
			      primary_buffer[0]->Cb,
				helper_frame->uv_len );
		veejay_memcpy(frame_buffer[chain_entry]->Cr,
			      primary_buffer[0]->Cr, 
				helper_frame->uv_len );
	}	
	else
	{
		veejay_memcpy(frame_buffer[chain_entry]->Y,
		       frame_buffer[centry]->Y,helper_frame->len);
		veejay_memcpy(frame_buffer[chain_entry]->Cb,
		       frame_buffer[centry]->Cb,
		        helper_frame->uv_len);
		veejay_memcpy(frame_buffer[chain_entry]->Cr,
		       frame_buffer[centry]->Cr,
		       helper_frame->uv_len);
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


int vj_perform_get_subframe_tag(veejay_t * info, int sub_clip,
			    int chain_entry, const int skip_incr)

{
    video_playback_setup *settings = (video_playback_setup*) info->settings;
    //int trim_val = clip_get_trimmer(a, chain_entry);

    int clip[4];

    int offset = clip_get_offset(sub_clip, chain_entry);	
    int nset = offset;
    int len;
     
	if(clip_get_short_info(sub_clip,&clip[0],&clip[1],&clip[2],&clip[3])!=0) return -1;

	len = clip[1] - clip[0];
 
	/* offset + start >= end */
	if(clip[3] >= 0) /* sub clip plays forward */
	{
		if(!skip_incr)
		{
			if( settings->current_playback_speed != 0)
	   	 		offset += clip[3]; /* speed */
	
			/* offset reached clip end */
    		if(  offset > len )
			{
				if(clip[2] == 2) /* clip is in pingpong loop */
				{
					/* then set speed in reverse and set offset to clip end */
					//offset = clip_b[1] - clip_b[0];
					offset = 0;
					clip_set_speed( sub_clip, (-1 * clip[3]) );
					clip_set_offset( sub_clip,chain_entry,offset);
					return clip[1];
				}
				if(clip[2] == 1)
				{
					offset = 0;
				}
				if(clip[2] == 0)
				{
					offset = 0;	
					clip_set_speed( sub_clip,0);
				}
			}


			clip_set_offset(sub_clip,chain_entry,offset);
			return (clip[0] + nset);
		}
		else
		{
			return clip[0] + nset;
		}
	}
	else
	{	/* sub clip plays reverse */
		if(!skip_incr)
		{
			if(settings->current_playback_speed != 0)
	    			offset += clip[3]; /* speed */

			if ( offset < -(len)  )
			{
				/* reached start position */
				if(clip[2] == 2)
				{
					//offset = clip_b[1] - clip_b[0];
					offset = 0;
					clip_set_speed( sub_clip, (-1 * clip[3]));
					clip_set_offset( sub_clip,chain_entry,offset);
					return clip[0];
				}
				if(clip[2] == 1)
				{
					//offset = clip_b[1] - clip_b[0];
					offset = 0;
				}	
				if(clip[2]== 0)
				{
					clip_set_speed( sub_clip , 0);
					offset = 0;
				}
			}
			clip_set_offset(sub_clip, chain_entry, offset);
	
			return (clip[1] + nset);
		}
		else
		{ 
		       return clip[1] + nset;
		}
	}
	return 0;
}
/*
int vj_perform_new_audio_frame(veejay_t * info, char *dst_buf, int nframe,
			       int speed)
{

    int n = nframe;
    int p = speed;
    int j = 0;
    int len = 0;
    int size = 0;
    if ((n - p) > 0) {	
	if (p > 0) {
	    int s = n - p;
	    int r = 0;
	    int offset = 0;
	    int i = 0;
	    int inc_val = (p % 2) ? 0 : 1;
	    j = 0;
	    for (r = s; r < n; r++) {
		len = vj_mlt_get_audio_frame(info->edit_list, r, bad_audio+offset);	
		if(len < 0) {
			len = (info->edit_list->audio_rate / info->edit_list->video_fps);
			veejay_memset(bad_audio+offset, 0, (len * info->edit_list->audio_bps) );
		}
		size = len * info->edit_list->audio_bps;

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
	    
	} 
	else {
            int p2 = p * -1;
	    int s = n - p2;
	 
	    int r = 0;
	    int offset = 0;
	    int i = 0;
	    int inc_val = (p2 % 2) ? 0 : 1;
	    j = 0;
	    for (r = s; r < n; r++) {
		len = vj_mlt_get_audio_frame(info->edit_list, r, bad_audio+offset);
		//vj_perform_reverse_audio_frame( info, len, bad_audio+offset);
		if(len < 0) {
			len = (info->edit_list->audio_rate / info->edit_list->video_fps);
			veejay_memest(bad_audio+offset, 0, (len * info->edit_list->audio_bps) );
		}
		size = len * info->edit_list->audio_bps;

		offset += size;
	    }
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
*/

#define ARRAY_LEN(x) ((int)(sizeof(x)/sizeof((x)[0])))
int vj_perform_fill_audio_buffers(veejay_t * info, uint8_t *audio_buf)
{
    video_playback_setup *settings = info->settings;
    int len = 0;
    //int i;
    int top_clip = info->uc->clip_id;
    int top_speed = clip_get_speed(top_clip);
    //int vol_a = clip_get_audio_volume(top_clip);
    /* take top frame */


    if (top_speed > 1 || top_speed < -1)
	{
		//len = info->edit_list->audio_rate / info->edit_list->video_fps;
		//mymemset_generic(audio_buf, 0, (len * info->edit_list->audio_bps));
		uint8_t *tmp_buf;
		int	alen = 0;
		int	n_frames = (top_speed < 0 ? -1 * top_speed : top_speed);
		int i;    
		int n_samples = 0;    
		int blen = 0;
		int bps = info->edit_list->audio_bps;
		double fl,cl;
		int pred_len = 0;
		tmp_buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * n_frames * PERFORM_AUDIO_SIZE);
		if(!tmp_buf) return 0;

		for ( i = 0; i <= n_frames; i ++ )	
		{
			alen = vj_el_get_audio_frame(info->edit_list,settings->current_frame_num + i, tmp_buf + blen);
			if( alen < 0 ) return 0;
			n_samples += alen;
			blen += (alen * bps);
		}


		fl = floor( n_samples - n_frames );
		cl = ceil( n_samples + n_frames );
		if( n_samples > 0 )
		{
			pred_len = info->edit_list->audio_rate / info->edit_list->video_fps;
			len = audio_resample( resample_context[n_frames], audio_buf, tmp_buf, n_samples );
			if( len < pred_len )
			{
				veejay_memset(audio_buf + (len * bps), 0 , (pred_len-len)*bps);
			}
		}
		if(tmp_buf) free(tmp_buf);
	
	}
	else
	{
		if (top_speed == 0)
		{
		    len = info->edit_list->audio_rate / info->edit_list->video_fps;
		    veejay_memset(audio_buf, 0, (len * info->edit_list->audio_bps));
		    return len;
		}
		else
		{
		   	 len =
			vj_el_get_audio_frame(info->edit_list,
					   settings->current_frame_num,
					   audio_buf);
			
		//	veejay_msg(VEEJAY_MSG_DEBUG, "[%d] samples ", len );
		}
    }

    /*
       if(info->sfd) {
       len = vj_perform_new_audio_slowframe( info, top_audio_buffer, settings->current_frame_num ); 
       } */

    if (len <= 0)
	{
		veejay_memset(audio_buf,0,PERFORM_AUDIO_SIZE);
		return (info->edit_list->audio_rate / info->edit_list->video_fps);
	}

    if (top_speed < 0)
		vj_perform_reverse_audio_frame(info, len, audio_buf);

    return len;

}

int vj_perform_apply_secundary_tag(veejay_t * info, int clip_id,
				   int type, int chain_entry, int entry, const int skip_incr)
{				/* second clip */
    int width = info->edit_list->video_width;
    int height = info->edit_list->video_height;
    int error = 1;
    int nframe;
    int len = 0;
    int centry = -2;

    helper_frame->data[0] = frame_buffer[chain_entry]->Y;
    helper_frame->data[1] = frame_buffer[chain_entry]->Cb;
    helper_frame->data[2] = frame_buffer[chain_entry]->Cr;

    switch (type) {		

    case VJ_TAG_TYPE_YUV4MPEG:	/* playing from stream */
    case VJ_TAG_TYPE_V4L:
    case VJ_TAG_TYPE_VLOOPBACK:
    case VJ_TAG_TYPE_AVFORMAT:
    case VJ_TAG_TYPE_NET:
    case VJ_TAG_TYPE_MCAST:
	case VJ_TAG_TYPE_COLOR:
	centry = vj_perform_tag_is_cached(chain_entry, entry, clip_id);
	if (centry == -1) {	/* not cached */
	    if( (type == VJ_TAG_TYPE_NET||type==VJ_TAG_TYPE_MCAST) && vj_tag_get_active(clip_id)==0)
		vj_tag_set_active(clip_id, 1);

	    if (vj_tag_get_active(clip_id) == 1 )
		{
			int res = 
				 vj_tag_get_frame(clip_id, helper_frame->data,
				audio_buffer[chain_entry]);
			if(res==1) {
		  		error = 0;
		  	 	cached_tag_frames[0][chain_entry] = clip_id;
	        }
			else
			{
				if(res == 2 )
				{
					// missed frame!!
   					return res;
				}
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
   case VJ_TAG_TYPE_NONE:

	    nframe = vj_perform_get_subframe_tag(info, clip_id, chain_entry, skip_incr); // get exact frame number to decode
 	    centry = vj_perform_clip_is_cached(clip_id, chain_entry);
        if(centry == -2)
	    {
		//not cached
	        len =
			vj_el_get_video_frame(info->edit_list, nframe,helper_frame->data, info->pixel_format); // and the length of that frame
		if(len > 0)
		{
			error = 0;
			cached_clip_frames[1][0] = clip_id;
		}
	    }
	    if(centry==-1)
		{
			// find in primary buffer
			veejay_memcpy( helper_frame->data[0], primary_buffer[0]->Y, helper_frame->len);
			veejay_memcpy( helper_frame->data[1], primary_buffer[0]->Cb, helper_frame->uv_len);
			veejay_memcpy( helper_frame->data[2], primary_buffer[0]->Cr, helper_frame->uv_len);
			cached_clip_frames[0][chain_entry] = clip_id;
			error =  0;
		}
	    if(centry >= 0 && centry <= CLIP_MAX_EFFECTS)
		{
			veejay_memcpy(helper_frame->data[0], frame_buffer[centry]->Y, helper_frame->len);
			veejay_memcpy(helper_frame->data[1], frame_buffer[centry]->Cb,helper_frame->uv_len);
			veejay_memcpy(helper_frame->data[2], frame_buffer[centry]->Cr,helper_frame->uv_len);
			cached_clip_frames[0][chain_entry] = clip_id;
			error =0;
		}

		break;

    }

    if (error == 1) {
	dummy_apply( helper_frame, width, height, VJ_EFFECT_COLOR_BLACK);
    }


    return 0;
}


static int vj_perform_get_frame_(veejay_t *info, int s1, long nframe, uint8_t *img[3])
{
	int entry = clip_get_render_entry( s1 );
	if(entry == 0)
		return vj_el_get_video_frame(
					info->edit_list,
					nframe,
					img,
					info->pixel_format );
	if(entry > 0)
	{
		editlist **el_list;
		void	 *data = NULL;
		data = clip_get_user_data( s1 );

		if(data == NULL)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "No editlist at this entry!");
			return 0;
		}

		el_list = (editlist**) data;
		return vj_el_get_video_frame(
				el_list[ entry ],
				nframe,
				img,
				info->pixel_format );
	}
	return 1;
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


    int width = info->edit_list->video_width;
    int height = info->edit_list->video_height;
    int error = 1;
    int nframe;
    int len;
    int res = 1;
    int centry = -2;
    if(chain_entry < 0 || chain_entry >= CLIP_MAX_EFFECTS) return -1;

    helper_frame->data[0] = frame_buffer[chain_entry]->Y;
    helper_frame->data[1] = frame_buffer[chain_entry]->Cb;
    helper_frame->data[2] = frame_buffer[chain_entry]->Cr;

    switch (type) {	// what source ?	
    case VJ_TAG_TYPE_YUV4MPEG:	 // streams
    case VJ_TAG_TYPE_V4L:
    case VJ_TAG_TYPE_VLOOPBACK:
    case VJ_TAG_TYPE_AVFORMAT:
    case VJ_TAG_TYPE_NET:
    case VJ_TAG_TYPE_MCAST:
	case VJ_TAG_TYPE_COLOR:
	centry = vj_perform_tag_is_cached(chain_entry, entry, clip_id); // is it cached?
	if (centry == -1) { // no it is not
	    if( (type == VJ_TAG_TYPE_NET||type==VJ_TAG_TYPE_MCAST) && vj_tag_get_active(clip_id)==0)
			vj_tag_set_active(clip_id, 1 );
	    if (vj_tag_get_active(clip_id) == 1)
		{ // if it is active (playing)
			res = vj_tag_get_frame(clip_id, helper_frame->data,
					    audio_buffer[chain_entry]);
			if(res==1) { // get a ycbcr frame
				error = 0;                               
				cached_tag_frames[0][chain_entry] = clip_id; // frame is cached now , admin it 
			}	
			else
			{
				if(res == 2)
					return res;
				error = 1; // something went wrong
				cached_tag_frames[0][chain_entry] = 0;
				vj_tag_set_active(clip_id, 0); // stop stream
			}
	    }
	} else {
		// it is cached, copy from frame buffer to this chain entry
	    vj_perform_use_cached_ycbcr_frame(entry, centry, width,
					      height, chain_entry);
	    error = 0;
	}	
	break;
    case VJ_TAG_TYPE_NONE:

	    nframe = vj_perform_get_subframe(info, clip_id, chain_entry, skip_incr); // get exact frame number to decode
 	    centry = vj_perform_clip_is_cached(clip_id, chain_entry);
        if(centry == -2)
	    {
		//not cached
		len = vj_perform_get_frame_( info, clip_id, nframe, helper_frame->data );	
	    //    len =
		
	//	vj_el_get_video_frame(info->edit_list,nframe,helper_frame->data, info->pixel_format); // and the length of that frame

			if(len > 0 )
			{
				error = 0;
				cached_clip_frames[1][0] = clip_id;//??
			}
	    }

	    if(centry==-1)
		{
			// find in primary buffer
			veejay_memcpy( helper_frame->data[0], primary_buffer[0]->Y, helper_frame->len);
			veejay_memcpy( helper_frame->data[1], primary_buffer[0]->Cb, helper_frame->uv_len);
			veejay_memcpy( helper_frame->data[2], primary_buffer[0]->Cr, helper_frame->uv_len);
			cached_clip_frames[0][chain_entry] = clip_id;
			error =  0;
		}
	    if(centry >= 0 && centry <= CLIP_MAX_EFFECTS)
		{
			veejay_memcpy( helper_frame->data[0], frame_buffer[centry]->Y, helper_frame->len);
			veejay_memcpy( helper_frame->data[1], frame_buffer[centry]->Cb,helper_frame->uv_len);
			veejay_memcpy( helper_frame->data[2], frame_buffer[centry]->Cr,helper_frame->uv_len);
			cached_clip_frames[0][chain_entry] = clip_id;
			error =0;
		}

		break;

    }

    if (error == 1) {
		dummy_apply(helper_frame, width, height, 1);
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
static int	vj_perform_tag_next_entry_subsample(veejay_t *info, int chain_entry)
{
	int i;
	for( i = chain_entry+1; i < CLIP_MAX_EFFECTS; i ++ )
	{
		int effect_id = vj_tag_get_effect_any( info->uc->clip_id, i );
		if(effect_id > 0 )
		{
			if(vj_effect_get_subformat(effect_id))
			{
				return 1;
			}
			return 0;
		}
	}
	return -1;	
}

static int	vj_perform_tag_render_chain_entry(veejay_t *info, int chain_entry, const int skip_incr, int sampled)
{
	int result = -1;
	VJFrame *frames[2];
	VJFrameInfo *frameinfo;
	video_playback_setup *settings = info->settings;
	frames[0] = info->effect_frame1;
	frames[1] = info->effect_frame2;
    	frameinfo = info->effect_frame_info;
    	// setup pointers to ycbcr 4:2:0 or 4:2:2 data
    	frames[0]->data[0] = primary_buffer[0]->Y;
   	frames[0]->data[1] = primary_buffer[0]->Cb;
    	frames[0]->data[2] = primary_buffer[0]->Cr;
	vjp_kf *setup;
    	setup = info->effect_info;
    	setup->ref = info->uc->clip_id;

	if (vj_tag_get_chain_status(info->uc->clip_id, chain_entry))
	{
	    int effect_id =
		vj_tag_get_effect_any(info->uc->clip_id, chain_entry); // what effect is enabled

		if (effect_id > 0)
		{
 			int sub_mode = vj_effect_get_subformat(effect_id);
			if (vj_effect_get_extra_frame(effect_id))
			{
		    		int sub_id =
					vj_tag_get_chain_channel(info->uc->clip_id,
						 chain_entry); // what id
		    		int source =
					vj_tag_get_chain_source(info->uc->clip_id, // what source type
						chain_entry);
		    		int n = 
					vj_perform_apply_secundary_tag(info,sub_id,source,chain_entry,0, skip_incr); // get it

			 	frames[1]->data[0] = frame_buffer[chain_entry]->Y;
	   	 		frames[1]->data[1] = frame_buffer[chain_entry]->Cb;
		    		frames[1]->data[2] = frame_buffer[chain_entry]->Cr;

				// sample B
	   			if(sub_mode)
					chroma_supersample(
						settings->sample_mode,
						effect_sampler,
						frames[1]->data,
						frameinfo->width,
						frameinfo->height );
			}

			if( sub_mode && !sampled )
			{
				chroma_supersample(
					settings->sample_mode,
					effect_sampler,
					frames[0]->data,
					frameinfo->width,
					frameinfo->height );
				result = 1; // sampled !
			}			
			vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,
				(int) settings->current_frame_num );
			if( sub_mode )
				result = 0;
	    } // if
	} // for
	return result;
}



static int	vj_perform_next_entry_subsample(veejay_t *info, int chain_entry)
{
	int i;
	for( i = chain_entry+1; i < CLIP_MAX_EFFECTS; i ++ )
	{
		int effect_id = clip_get_effect_any( info->uc->clip_id, i );
		if(effect_id > 0 )
		{
			if(vj_effect_get_subformat(effect_id))
			{
				return 1;
			}
			return 0;
		}
	}
	return -1;	
}



static int	vj_perform_render_chain_entry(veejay_t *info, int chain_entry, const int skip_incr, int sampled)
{
	int result = -1;
	VJFrame *frames[2];
	VJFrameInfo *frameinfo;
	video_playback_setup *settings = info->settings;
	frames[0] = info->effect_frame1;
	frames[1] = info->effect_frame2;
    	frameinfo = info->effect_frame_info;
    	// setup pointers to ycbcr 4:2:0 or 4:2:2 data
    	frames[0]->data[0] = primary_buffer[0]->Y;
   	frames[0]->data[1] = primary_buffer[0]->Cb;
    	frames[0]->data[2] = primary_buffer[0]->Cr;
	vjp_kf *setup;
    	setup = info->effect_info;
    	setup->ref = info->uc->clip_id;

	if (clip_get_chain_status(info->uc->clip_id, chain_entry))
	{
	    int effect_id =
		clip_get_effect_any(info->uc->clip_id, chain_entry); // what effect is enabled

		if (effect_id > 0)
		{
 			int sub_mode = vj_effect_get_subformat(effect_id);
			if (vj_effect_get_extra_frame(effect_id))
			{
		    		int sub_id =
					clip_get_chain_channel(info->uc->clip_id,
						 chain_entry); // what id
		    		int source =
					clip_get_chain_source(info->uc->clip_id, // what source type
						chain_entry);
		    		int n = 
					vj_perform_apply_secundary(info,sub_id,source,chain_entry,0, skip_incr); // get it

			 	frames[1]->data[0] = frame_buffer[chain_entry]->Y;
	   	 		frames[1]->data[1] = frame_buffer[chain_entry]->Cb;
		    		frames[1]->data[2] = frame_buffer[chain_entry]->Cr;

				// sample B
	   			if(sub_mode)
					chroma_supersample(
						settings->sample_mode,
						effect_sampler,	
						frames[1]->data,
						frameinfo->width,
						frameinfo->height );
			}

			if( sub_mode && !sampled)
			{
				chroma_supersample(
					settings->sample_mode,
					effect_sampler,
					frames[0]->data,
					frameinfo->width,
					frameinfo->height );
				result = 1; // sampled !
			}			
			vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,
				(int) settings->current_frame_num );

			if( sub_mode )
				result = 0; // hint to downsample
	    	} // if
	} // status
	return result;
}


int vj_perform_clip_complete_buffers(veejay_t * info, int entry, const int skip_incr)
{
	int chain_entry;
	vjp_kf *setup;
	VJFrame *frames[2];
	VJFrameInfo *frameinfo;
	video_playback_setup *settings = info->settings;
    	int chain_fade =0;
    	if (clip_get_effect_status(info->uc->clip_id)!=1)
		return 0;		/* nothing to do */
    	setup = info->effect_info;
	frames[0] = info->effect_frame1;
	frames[1] = info->effect_frame2;
    	frameinfo = info->effect_frame_info;
    	setup->ref = info->uc->clip_id;
    	// setup pointers to ycbcr 4:2:0 or 4:2:2 data
    	frames[0]->data[0] = primary_buffer[0]->Y;
   	frames[0]->data[1] = primary_buffer[0]->Cb;
    	frames[0]->data[2] = primary_buffer[0]->Cr;

   	chain_fade = clip_get_fader_active(info->uc->clip_id);
   	if(chain_fade)
		vj_perform_pre_chain( info, frames[0] );

	int subsample = 0;
	int tmpsample = 0;
	for(chain_entry = 0; chain_entry < CLIP_MAX_EFFECTS; chain_entry++)
	{
		tmpsample = vj_perform_render_chain_entry(	
				info, chain_entry, skip_incr, subsample);
		if(tmpsample == 0 )
		{ // should be subsampled
			if( vj_perform_next_entry_subsample(
				info,chain_entry ) <= 0 )
			{
				// next is downsampled or 420
				chroma_subsample( settings->sample_mode,
						effect_sampler,
					frames[0]->data,frameinfo->width,
					frameinfo->height );
				subsample = 0;
			}
			else
			{	// next is super sampled format, keep 444
				subsample = 1;
			}
		}
	}

    	if (chain_fade)
		vj_perform_post_chain(info,frames[0]);

    	return 1;
}


/********************************************************************
 * int vj_perform_tag_complete(veejay_t *info)
 *
 * this function assumes the data is ready and waiting to be processed
 * it reads the effect chain and performs associated actions.
 *
 * returns 0 on success 
 */
int vj_perform_tag_complete_buffers(veejay_t * info, int entry, const int skip_incr  )
{
    	if (vj_tag_get_effect_status(info->uc->clip_id)!=1)
			return 0;		/* nothing to do */
  	video_playback_setup *settings = info->settings;  
	int chain_entry;
	int chain_fade = 0; 
	VJFrame *frames[2];

	VJFrameInfo *frameinfo = info->effect_frame_info;
	frames[0] = info->effect_frame1;
	frames[1] = info->effect_frame2;
	frames[0]->data[0] = primary_buffer[0]->Y;
	frames[0]->data[1] = primary_buffer[0]->Cb;
	frames[0]->data[2] = primary_buffer[0]->Cr;

	chain_fade = vj_tag_get_fader_active(info->uc->clip_id);
   	if(chain_fade)
		vj_perform_pre_chain( info, frames[0] );

	int subsample = 0;
	int tmpsample = 0;
	for(chain_entry = 0; chain_entry < CLIP_MAX_EFFECTS; chain_entry++)
	{
		// do an entry
		tmpsample = vj_perform_tag_render_chain_entry(	
				info, chain_entry, skip_incr, subsample);
		// entry is SUP or SUB
		if(tmpsample == 0 )
		{
			if(vj_perform_tag_next_entry_subsample(
				info, chain_entry) <= 0)
			{
				// no more entries, or entry needs subsampling anyway
				chroma_subsample( settings->sample_mode,
					effect_sampler,
					frames[0]->data,frameinfo->width,
						frameinfo->height );		
				subsample = 0;
			}
			else
			{
				subsample = 1;
			}

		}
	}

    	if (chain_fade)
		vj_perform_post_chain(info,frames[0]);
	return 1;
}


/********************************************************************
 * decodes plain video, does not touch frame_buffer
 *
 */

void vj_perform_plain_fill_buffer(veejay_t * info, int entry)
{
    video_playback_setup *settings = (video_playback_setup*)  info->settings;
    uint8_t *frame[3];
    int ret = 0;
    frame[0] = primary_buffer[0]->Y;
    frame[1] = primary_buffer[0]->Cb;
    frame[2] = primary_buffer[0]->Cr;

	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_CLIP)
		ret = vj_perform_get_frame_(info, info->uc->clip_id, settings->current_frame_num,frame );
	else
		ret = vj_el_get_video_frame(info->edit_list,settings->current_frame_num,frame,info->pixel_format);
	

    if (ret <= 0 )
	{
		veejay_memset(frame[0], 16, helper_frame->len);
		veejay_memset(frame[1], 128,helper_frame->uv_len);
		veejay_memset(frame[2], 128,helper_frame->uv_len);
	}
    if(ret == 2)
    {
	veejay_msg(VEEJAY_MSG_WARNING, "There is no plain video to play!");
	veejay_change_state(info, LAVPLAY_STATE_STOP);
    }
}


static long last_rendered_frame = 0;
int vj_perform_render_clip_frame(veejay_t *info, uint8_t *frame[3])
{
	int audio_len = 0;
	//uint8_t buf[16384];
	long nframe = info->settings->current_frame_num;
	uint8_t *_audio_buffer;
	if(last_rendered_frame == nframe) return 0; // skip frame 
	
	last_rendered_frame = info->settings->current_frame_num;

	if(info->edit_list->has_audio)
	{
		_audio_buffer = x_audio_buffer;
		audio_len = (info->edit_list->audio_rate / info->edit_list->video_fps);
	}

	return (int)(clip_record_frame( info->uc->clip_id,frame,
				_audio_buffer,audio_len));
}	
int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[3])
{
	long nframe = info->settings->current_frame_num;
	int clip_id = info->uc->clip_id;
	if(last_rendered_frame == nframe) return 0; // skip frame 
	
	last_rendered_frame = info->settings->current_frame_num;
	if(info->settings->offline_record)
		clip_id = info->settings->offline_tag_id;

	if(info->settings->offline_record)
	{
		if (!vj_tag_get_frame(clip_id, frame, NULL))
	   	{
			veejay_msg(VEEJAY_MSG_ERROR, "cannot get frame ?! (skip)" ); 
			return 0;//skip
		}
	}

	return vj_tag_record_frame( clip_id, frame, NULL, 0);
}	

void vj_perform_record_commit_clip(veejay_t *info,long start_el_pos,int len)
{
 //video_playback_setup *settings = info->settings;
 clip_info *skel;

 long el_pos = start_el_pos;


 if(start_el_pos==-1)
 {
 	el_pos = (info->edit_list->video_frames - 1);//+1 // + len ??
	if(!info->edit_list->has_video)
	  el_pos = 0;
	else
	  el_pos -= len;
 }


 skel = clip_skeleton_new( el_pos, len + el_pos -1  );//-1
 // figure out true starting position
 if(skel)
 {
	if(clip_store(skel)==0)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Created new clip [%d] [%d-%d], total frames in el:%ld",
		skel->clip_id, clip_get_startFrame(skel->clip_id), clip_get_endFrame(skel->clip_id),
			info->edit_list->video_frames  );
		clip_set_looptype( skel->clip_id, 1 );
	} 

  }

}

long vj_perform_record_commit_single(veejay_t *info, int entry)
{
  //video_playback_setup *settings = info->settings;

  char filename[512];
  //int n_files = 0;
  long start_el_pos = info->edit_list->video_frames;

  if(info->uc->playback_mode == VJ_PLAYBACK_MODE_CLIP)
  {
 	if(clip_get_encoded_file(info->uc->clip_id, filename))
  	{

		if( info->settings->render_list == 0)
		{
			long dest = info->edit_list->video_frames;
			if( !veejay_edit_addmovie(info, filename, -1, dest,dest))
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Adding file %s", filename);
				return -1;
			}
		}
		else
		{
			long dest = 0;
			if( veejay_add_el_entry( info, info->uc->clip_id, filename, 
					info->settings->render_list ))
			{
				veejay_msg(VEEJAY_MSG_INFO, "New playable render entry %d",
					info->settings->render_list );
			}
		    	return -1; // dont commit
		}
		return start_el_pos;
 	 }
  }

  if(info->uc->playback_mode==VJ_PLAYBACK_MODE_TAG)
  {
	 int stream_id = (info->settings->offline_record ? info->settings->offline_tag_id : info->uc->clip_id);
 	 if(vj_tag_get_encoded_file(stream_id, filename))
  	 {
		long dest = info->edit_list->video_frames;
		if( !veejay_edit_addmovie(info, filename, -1, dest,dest))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Adding file %s", filename);
			return -1;
		}
		return start_el_pos;
	}
  }

  return -1;

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
     settings->render_list = 0;
 }

 if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
 {
	int stream_id = (settings->offline_record ? settings->offline_tag_id : info->uc->clip_id);
	int play = settings->tag_record_switch;
	vj_tag_reset_encoder(stream_id);
        vj_tag_reset_autosplit(stream_id);
	if(settings->offline_record)
	{
		play = settings->offline_created_clip;
		settings->offline_record = 0;
		settings->offline_created_clip = 0;
		settings->offline_tag_id = 0;
	}
	else 
	{
		settings->tag_record = 0;
		settings->tag_record_switch = 0;
	}

	if(play)
	{
		info->uc->playback_mode = VJ_PLAYBACK_MODE_CLIP;
		veejay_set_clip(info ,clip_size()-1);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Not autoplaying new clip");
	}
 
  }

}


void vj_perform_record_clip_frame(veejay_t *info, int entry) {
	video_playback_setup *settings = info->settings;
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
		int n;
		veejay_msg(VEEJAY_MSG_INFO, "Creating new file (reached 2gb AVI limit)");
		clip_stop_encoder( info->uc->clip_id );
		// close file, add to editlist
		


		n = vj_perform_record_commit_single( info, entry );
		// clear encoder
		clip_reset_encoder( info->uc->clip_id );
		// initialize a encoder
		if(frames_left > 0 )
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "continue, %d frames left to record", frames_left);
			// todo: add to other editlist .. ?
			if( clip_init_encoder( info->uc->clip_id, NULL,
				df, info->edit_list, frames_left)==-1)
			{
				veejay_msg(VEEJAY_MSG_ERROR,
				"Error while auto splitting "); 
			}
		}
		else
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Added new clip of %d frames",len);
			if(n!=-1) vj_perform_record_commit_clip(info,-1,len);
		}
	 }

	
	 if( res == 1)
	 {
		int len = clip_get_total_frames(info->uc->clip_id);
		clip_stop_encoder(info->uc->clip_id);
		if(vj_perform_record_commit_single( info, entry ) != -1)
		{	    
			vj_perform_record_commit_clip(info, -1,len);
		}
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
	int stream_id = info->uc->clip_id;
	if( settings->offline_record )
	  stream_id = settings->offline_tag_id;

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
	// not done?! by offline?1	
	res = vj_perform_render_tag_frame(info, frame);
	if( res == 2)
	{
		/* auto split file */
		int df = vj_event_get_video_format();
		int len = vj_tag_get_duration(stream_id);
		long frames_left = vj_tag_get_frames_left(stream_id) ;
		// stop encoder
		vj_tag_stop_encoder( stream_id );
		// close file, add to editlist
		vj_perform_record_commit_single( info, entry );
		// clear encoder
		vj_tag_reset_encoder( stream_id );
		// initialize a encoder
		if(frames_left > 0 )
		{
			if( vj_tag_init_encoder( stream_id, NULL,
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
		int len = vj_tag_get_encoded_frames(stream_id);
		vj_tag_stop_encoder(stream_id);
		vj_perform_record_commit_single( info, entry );	    
		vj_perform_record_commit_clip(info, -1,len);
		vj_perform_record_stop(info);
	 }

	 if( res == -1)
	{
		vj_tag_stop_encoder(stream_id);
		vj_perform_record_stop(info);
 	}

}


int vj_perform_tag_fill_buffer(veejay_t * info, int entry)
{
    int error = 1;
    uint8_t *frame[3];
	int type;
	int active;
    frame[0] = primary_buffer[0]->Y;
    frame[1] = primary_buffer[0]->Cb;
    frame[2] = primary_buffer[0]->Cr;

	type = vj_tag_get_type( info->uc->clip_id );
	active = vj_tag_get_active(info->uc->clip_id );

	if( (type == VJ_TAG_TYPE_NET || type == VJ_TAG_TYPE_MCAST ) && active == 0)
	{	
		vj_tag_enable( info->uc->clip_id );	
	}

    if (vj_tag_get_active(info->uc->clip_id) == 1)
    {
	int tag_id = info->uc->clip_id;
	video_playback_setup *settings = info->settings;
	// get the frame
	if (vj_tag_get_frame(tag_id, frame, NULL))
	{
	    error = 0;
	    cached_tag_frames[1][0] = tag_id;
   	}
   }         

	
  if (error == 1 && info->video_out != 5)
  {
	VJFrame dumb;
	if( info->pixel_format == FMT_420 )
		vj_el_init_422_frame( info->edit_list, &dumb );
	else
		vj_el_init_420_frame( info->edit_list, &dumb );

	dumb.data[0] = frame[0];
	dumb.data[1] = frame[1];
	dumb.data[2] = frame[2];

	dummy_apply(&dumb,
	    info->edit_list->video_width,
	    info->edit_list->video_height, VJ_EFFECT_COLOR_BLACK);

//	veejay_msg(VEEJAY_MSG_DEBUG, "Error grabbing frame! Playing dummy (black)");



  }  
  return (error == 1 ? -1 : 0);
}

/* vj_perform_pre_fade:
   prior to fading, we copy the orginal (unaffected) image to a tempory buffer */
void vj_perform_pre_chain(veejay_t *info, VJFrame *frame) {
	veejay_memcpy( temp_buffer[0] ,frame->data[0], helper_frame->len );
	veejay_memcpy( temp_buffer[1], frame->data[1], helper_frame->uv_len);
	veejay_memcpy( temp_buffer[2], frame->data[2], helper_frame->uv_len ); /* /4 */
}
/* vj_perform_post_chain:
   after completing the effect chain , we blend a with b
   and update the internal variables
  */
void vj_perform_post_chain(veejay_t *info, VJFrame *frame)
{
	unsigned int opacity; 
	int i;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

  	if(info->uc->playback_mode==VJ_PLAYBACK_MODE_CLIP)
   	{
   		int op_b;
		int mode = clip_get_fader_active( info->uc->clip_id );

 	       if(mode == 2 ) // manual fade
			opacity = (int) clip_get_fader_val(info->uc->clip_id);
		else	// fade in/fade out
		    	opacity = (int) clip_apply_fader_inc(info->uc->clip_id);

	   	if(mode != 2)
		{
		    	int dir =clip_get_fader_direction(info->uc->clip_id);

			if((dir<0) &&(opacity == 0))
			{
				clip_set_effect_status(info->uc->clip_id, 1);
     				clip_reset_fader(info->uc->clip_id);
	      			veejay_msg(VEEJAY_MSG_INFO, "Clip Chain Auto Fade Out done");
			}
			if((dir>0) && (opacity==255))
			{
				clip_set_effect_status(info->uc->clip_id,0);
				clip_reset_fader(info->uc->clip_id);
				veejay_msg(VEEJAY_MSG_INFO, "Clip Chain Auto fade In done");
			}
    		}

	op_b = 255 - opacity;
	for(i=0; i < helper_frame->len;i ++ )
	{
		Y[i]= (( op_b * Y[i] ) + ( opacity * temp_buffer[0][i] )) >> 8;
	} 
	for(i=0; i < helper_frame->uv_len;i ++ )
	{
		Cb[i]= (( op_b * Cb[i] ) + ( opacity * temp_buffer[1][i] )) >> 8;
		Cr[i]= (( op_b * Cr[i] ) + ( opacity * temp_buffer[2][i] )) >> 8;
	}
   }
   else
   {
	int op_b;
	int mode = vj_tag_get_fader_active( info->uc->clip_id );

	if(mode == 2)
		opacity = (int) vj_tag_get_fader_val(info->uc->clip_id);
	else
     		opacity = (int) vj_tag_apply_fader_inc(info->uc->clip_id);
   
	if(mode != 2)
	{
 		int dir = vj_tag_get_fader_direction(info->uc->clip_id);
		if((dir < 0) && (opacity == 0))
		{
			vj_tag_set_effect_status(info->uc->clip_id,1);
			vj_tag_reset_fader(info->uc->clip_id);
			veejay_msg(VEEJAY_MSG_INFO, "Stream Chain Auto Fade done");
		}
		if((dir > 0) && (opacity == 255))
		{
			vj_tag_set_effect_status(info->uc->clip_id,0);
			vj_tag_reset_fader(info->uc->clip_id);
			veejay_msg(VEEJAY_MSG_INFO, "Stream Chain Auto Fade done");
		}
		
    	}

	op_b = 255 - opacity;
	for(i=0; i < frame->len;i ++ )
	{
		Y[i]= (( op_b * Y[i] ) + ( opacity * temp_buffer[0][i] )) >> 8;
	} 
	for(i=0; i < helper_frame->uv_len;i ++ )
	{
		Cb[i]= (( op_b * Cb[i] ) + ( opacity * temp_buffer[1][i] )) >> 8;
		Cr[i]= (( op_b * Cr[i] ) + ( opacity * temp_buffer[2][i] )) >> 8;
	} 


   }

}

int vj_perform_queue_audio_frame(veejay_t *info, int frame)
{

	

#ifdef HAVE_JACK
  video_playback_setup *settings = info->settings;
  long this_frame = settings->current_frame_num;
  int num_samples = 0;
  int tmplen = 0;
  editlist *el = info->edit_list;
	uint8_t *a_buf = top_audio_buffer;

  if(info->edit_list->has_audio == 0 ) return 1;

  if(a_buf == NULL)
  {
	veejay_msg(VEEJAY_MSG_DEBUG, "Woops this sucks!");
	veejay_change_state(info,LAVPLAY_STATE_STOP);
	return -1;
  }


     /* First, get the audio */
  if (info->audio && el->has_audio)
  {
	switch (info->uc->playback_mode)
	{
		case VJ_PLAYBACK_MODE_CLIP:
			num_samples = vj_perform_fill_audio_buffers(info,a_buf);
		    break;

		case VJ_PLAYBACK_MODE_PLAIN:
		    if (settings->current_playback_speed == 0)
		    {
		    	veejay_memset( a_buf, 0, PERFORM_AUDIO_SIZE);
			num_samples = (el->audio_rate/el->video_fps);
		    }
		    else
		    {
			num_samples =
			    vj_el_get_audio_frame(el, this_frame,a_buf );

			if(num_samples < 0)
			{
				veejay_memset(a_buf,0,PERFORM_AUDIO_SIZE);
				num_samples = (el->audio_rate/el->video_fps);
			}
		    }
	    	    if (settings->current_playback_speed < 0)
		    {
			vj_perform_reverse_audio_frame(info, num_samples,a_buf);
					       
	    	    }
	    	    break;
		case VJ_PLAYBACK_MODE_TAG:
		    	num_samples = vj_tag_get_audio_frame(info->uc->clip_id, a_buf);
			if(num_samples <= 0)
			{
				num_samples = (el->audio_rate/el->video_fps);
			}
			break;

		default:
			    veejay_memset( a_buf, 0 , PERFORM_AUDIO_SIZE);
	    	    break;
	}

 	/* dump audio frame if required */
        if(info->stream_enabled==1)
	{
	    vj_yuv_put_aframe(a_buf, el, num_samples * el->audio_bps);
	}

	if(settings->audio_mute)
	{
		veejay_memset( a_buf, 0, num_samples * el->audio_bps);
	}

	if(info->audio != AUDIO_PLAY) return 1;

	if( el->play_rate != el->audio_rate && el->play_rate != 0)
	{
		veejay_memcpy( x_audio_buffer, a_buf, num_samples * el->audio_bps);
		int r = audio_resample( resample_jack, (short*)top_audio_buffer,(short*)a_buf, num_samples );
		vj_jack_play( top_audio_buffer, ( r * el->audio_bps ));
	}
	else
	{
		vj_jack_play( a_buf, (num_samples * el->audio_bps ));
	}
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

	if(settings->offline_record)	
		vj_perform_record_tag_frame(info,0);


	switch (info->uc->playback_mode) {
		case VJ_PLAYBACK_MODE_CLIP:
		    	vj_perform_plain_fill_buffer(info, frame);	/* primary frame */
		    	cached_clip_frames[1][0] = info->uc->clip_id;
		    	if(vj_perform_verify_rows(info,frame))
		    	{		
		   	 		vj_perform_clip_complete_buffers(info, frame, skip_incr);
		    	}
		    	if(!skip_incr)
		  			if(clip_encoder_active(info->uc->clip_id))
		    		{
						vj_perform_record_clip_frame(info,frame);
			    	}	 
		    return 1;
		    break;
		case VJ_PLAYBACK_MODE_PLAIN:
		    vj_perform_plain_fill_buffer(info, frame);
		    return 1;
 		    break;
		case VJ_PLAYBACK_MODE_TAG:
		    	if (vj_perform_tag_fill_buffer(info, frame) == 0)
		    	{	/* primary frame */
					if(vj_perform_verify_rows(info,frame))
					{
						vj_perform_tag_complete_buffers(info, frame, skip_incr);
					}
					if(!skip_incr)
					if(vj_tag_encoder_active(info->uc->clip_id))
					{
						vj_perform_record_tag_frame(info,frame);
					}
				return 1;
		    	}
			return 1;
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
		 		vj_perform_increase_clip_frame(info,settings->current_playback_speed);
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


