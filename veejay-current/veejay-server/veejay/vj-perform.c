/* 
 * veejay  
 *
 * Copyright (C) 2000-2008 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <libsample/sampleadm.h>  
#include <libstream/vj-tag.h>
#include <libvjnet/vj-server.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-lib.h>
#include <libel/vj-el.h>
#include <math.h>
#include <libel/vj-avcodec.h>
#include <veejay/vj-event.h>
#include <mjpegtools/mpegconsts.h>
#include <mjpegtools/mpegtimecode.h>
#include <libyuv/yuvconv.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vj-perform.h>
#include <veejay/libveejay.h>
#include <libsamplerec/samplerecord.h>
#include <libel/pixbuf.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-task.h>
#include <liblzo/lzo.h>
#include <veejay/vj-viewport.h>
#include <veejay/vj-composite.h>
#ifdef HAVE_FREETYPE
#include <veejay/vj-font.h>
#endif
#define RECORDERS 1
#ifdef HAVE_JACK
#include <veejay/vj-jack.h>
#endif
#include <libvje/internal.h>
#include <libvjmem/vjmem.h>
#include <libvje/effects/opacity.h>
#include <libqrwrap/qrwrapper.h>

#define PERFORM_AUDIO_SIZE 16384

#ifndef SAMPLE_FMT_S16
#define SAMPLE_FMT_S16 AV_SAMPLE_FMT_S16
#endif

typedef struct {
    uint8_t *Y;
    uint8_t *Cb;
    uint8_t *Cr;
    uint8_t *alpha;
    uint8_t *P0;
    uint8_t *P1;
    int	     ssm;
    char     padding[12];
} ycbcr_frame; //@ TODO: drop this structure and replace for veejay-next 's VJFrame

typedef struct {
	int	fader_active;
	int	follow_fade;
	int	follow_now[2];
	int	follow_run;
	int	fx_status;
	int	enc_active;
	int	type;
	int	active;
	char	padding[12];
} varcache_t;


#define	RUP8(num)(((num)+8)&~8)
static long performer_frame_size_ = 0;

extern int pixel_Y_lo_;

static	varcache_t	pvar_;
static	void		*lzo_;
static VJFrame *crop_frame = NULL;
static ycbcr_frame **video_output_buffer = NULL; /* scaled video output */
static int	video_output_buffer_convert = 0;
static ycbcr_frame **frame_buffer = NULL;	/* chain */
static ycbcr_frame **primary_buffer = NULL;	/* normal */
static ycbcr_frame *preview_buffer = NULL;
static int preview_max_w;
static int preview_max_h;

#define CACHE_TOP 0
#define CACHE 1
#define CACHE_SIZE (SAMPLE_MAX_EFFECTS+CACHE)
static int cached_tag_frames[CACHE_SIZE];	/* cache a frame into the buffer only once */
static int cached_sample_frames[CACHE_SIZE];
static int frame_info[64][SAMPLE_MAX_EFFECTS];	/* array holding frame lengths  */
static uint8_t *audio_buffer[SAMPLE_MAX_EFFECTS];	/* the audio buffer */
static uint8_t *audio_silence_ = NULL;
static uint8_t *lin_audio_buffer_ = NULL;
static uint8_t *top_audio_buffer = NULL;
static uint8_t *audio_rec_buffer = NULL;
static uint8_t *audio_render_buffer = NULL;
static uint8_t *down_sample_buffer = NULL;
static uint8_t *temp_buffer[4];
static uint8_t *feedback_buffer[4];
static uint8_t *pribuf_area = NULL;
static size_t pribuf_len = 0;
static uint8_t *socket_buffer = NULL;
static uint8_t *fx_chain_buffer = NULL;
static	size_t fx_chain_buflen = 0;
static int		sbic = 0;
static ycbcr_frame *record_buffer = NULL;	// needed for recording invisible streams
static VJFrame *helper_frame = NULL;
static int vj_perform_record_buffer_init();
static void vj_perform_record_buffer_free();
#ifdef HAVE_JACK
static ReSampleContext *resample_context[(MAX_SPEED+1)];
static ReSampleContext *downsample_context[(MAX_SPEED+1)];
static int last_rate_[2] = { 0, 0 };
#endif
static const char *intro = 
	"A visual instrument for GNU/Linux\n";
static const char *license =
	"This program is licensed as\nFree Software (GNU/GPL version 2)\n\nFor more information see:\nhttp://veejayhq.net\n";
static const char *copyr =
	"(C) 2002-2015 Copyright N.Elburg et all (nwelburg@gmail.com)\n";

static const char *donateaddr = 
	"Veejay's BTC donation address: 1PUNRsv8vDt1upTx9tTpY5sH8mHW1DTrKJ\n";

#define MLIMIT(var, low, high) \
if((var) < (low)) { var = (low); } \
if((var) > (high)) { var = (high); }

//forward
static	int	vj_perform_preprocess_secundary( veejay_t *info, int id, int mode,int current_ssm,int chain_entry, VJFrame **frames, VJFrameInfo *frameinfo );
static	int	vj_perform_preprocess_has_ssm( veejay_t *info, int id, int mode);
static int vj_perform_get_frame_fx(veejay_t *info, int s1, long nframe, uint8_t *frame[4], uint8_t *p0plane, uint8_t *p1plane);
static void vj_perform_pre_chain(veejay_t *info, VJFrame *frame);
static int vj_perform_post_chain_sample(veejay_t *info, VJFrame *frame);
static int vj_perform_post_chain_tag(veejay_t *info, VJFrame *frame);
static void seq_play_sample( veejay_t *info, int n);
static void vj_perform_plain_fill_buffer(veejay_t * info);
static int vj_perform_tag_fill_buffer(veejay_t * info);
static void vj_perform_clear_cache(void);
static int vj_perform_increase_tag_frame(veejay_t * info, long num);
static int vj_perform_increase_plain_frame(veejay_t * info, long num);
static void vj_perform_apply_secundary_tag(veejay_t * info, int sample_id,int type, int chain_entry);
static void vj_perform_apply_secundary(veejay_t * info, int sample_id,int type, int chain_entry);
static int vj_perform_tag_complete_buffers(veejay_t * info, int *h);
static int vj_perform_increase_sample_frame(veejay_t * info, long num);
static int vj_perform_sample_complete_buffers(veejay_t * info, int *h);
static void vj_perform_use_cached_ycbcr_frame(veejay_t *info, int centry, int chain_entry);
static int vj_perform_apply_first(veejay_t *info, vjp_kf *todo_info, VJFrame **frames, VJFrameInfo *frameinfo, int e, int c, int n_frames, void *ptr, int playmode );
static int vj_perform_render_sample_frame(veejay_t *info, uint8_t *frame[4], int sample);
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4]);
static int vj_perform_record_commit_single(veejay_t *info);
static int vj_perform_get_subframe(veejay_t * info, int sub_sample,int chain_entyr );
static int vj_perform_get_subframe_tag(veejay_t * info, int sub_sample,int chain_entry );
#ifdef HAVE_JACK
static void vj_perform_reverse_audio_frame(veejay_t * info, int len, uint8_t *buf );
#endif

extern int  pixel_Y_lo_;

static	void	vj_perform_copy( ycbcr_frame *src, ycbcr_frame *dst, int Y_len, int UV_len )
{
	uint8_t *input[4] = { src->Y, src->Cb, src->Cr,NULL };
	uint8_t *output[4] = { dst->Y, dst->Cb, dst->Cr,NULL };
	int 	strides[4] = { Y_len, UV_len, UV_len,0 };
	vj_frame_copy(input,output,strides);
}
static	void	vj_perform_copy2( ycbcr_frame *src, uint8_t **output, int Y_len, int UV_len )
{
	uint8_t *input[4] = { src->Y, src->Cb, src->Cr,NULL };
	int 	strides[4] = { Y_len, UV_len, UV_len,0 };
	vj_frame_copy(input,output,strides);
}
static	void	vj_perform_copy3( uint8_t **input, uint8_t **output, int Y_len, int UV_len )
{
	int 	strides[4] = { Y_len, UV_len, UV_len,0 };
	vj_frame_copy(input,output,strides);
}

#ifdef HAVE_JACK
static inline void vj_perform_play_audio( video_playback_setup *settings, uint8_t *source, int len )
{
	// if auto mute is enabled, play muted data 
	// audio_silence_ is allocated once and played in-place of, thus recording chain is not affected by auto-mute
	if( settings->auto_mute ) {
		vj_jack_play( audio_silence_, len );
	} else {
		vj_jack_play( source, len );
	}
}
#endif

static int vj_perform_previously_sampled(int chain_entry, int sub_id, int *ref )
{
	int i = 0;
	for( i = chain_entry; i > 0; i -- ) {
		if( frame_buffer[i]->ssm == -1 )
			continue;
		if( ref[i] == sub_id )
			return frame_buffer[ i ]->ssm;

		return frame_buffer[i]->ssm;
	}
	return 0;
}

static int vj_perform_tag_is_cached(veejay_t *info, int chain_entry, int tag_id)
{
 	int c;
	int res = -1;

	if( cached_tag_frames[0] == tag_id ) 
		return 0;

	for(c=0; c < CACHE_SIZE; c++)
  	{
	    if(cached_tag_frames[c] == tag_id) 
	    {
			if( info->settings->feedback && info->uc->sample_id == tag_id &&
						info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
				return -1; //@ feedback cannot be cached source
			}
			res = c;
			break;	
	    }
  	}

	return res;
}
static int vj_perform_sample_is_cached(veejay_t *info,int sample_id, int chain_entry)
{
	int c;
	int res = -1;

	if( sample_id == cached_sample_frames[0] )  
		return 0;

	for(c=1; c < CACHE_SIZE ; c++)
  	{
	    if(cached_sample_frames[c] == sample_id) 
	    {
		if( info->settings->feedback && info->uc->sample_id == sample_id  &&
					info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
			return -1;
		}
		res = c;
		break;
	    }
  	}
	return res;
}

void vj_perform_clear_frame_info()
{
    int c = 0;
    for (c = 0; c < SAMPLE_MAX_EFFECTS; c++) {
	frame_info[0][c] = 0;
    }
}


static void vj_perform_clear_cache()
{
    veejay_memset(cached_tag_frames, 0 , sizeof(cached_tag_frames));
    veejay_memset(cached_sample_frames, 0, sizeof(cached_sample_frames));
}

static int vj_perform_increase_tag_frame(veejay_t * info, long num)
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
    if (settings->current_frame_num >= settings->max_frame_num) {
	settings->current_frame_num = settings->min_frame_num;
	return -1;
    }
    return 0;
}


static int vj_perform_increase_plain_frame(veejay_t * info, long num)
{
    video_playback_setup *settings = info->settings;

    settings->simple_frame_dup ++;
    if (settings->simple_frame_dup >= info->sfd) {
	settings->current_frame_num += num;
	settings->simple_frame_dup = 0;
    }

// auto loop plain mode
    if (settings->current_frame_num < settings->min_frame_num) {
	settings->current_frame_num = settings->max_frame_num;
	        	
	return 0;
    }
    if (settings->current_frame_num > settings->max_frame_num) {
	if(!info->continuous)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Reached end of video - Ending veejay session ... ");
		veejay_change_state(info, LAVPLAY_STATE_STOP);
	}
	settings->current_frame_num = settings->min_frame_num;
	return 0;
    }
    return 0;
}


static	int	vj_perform_valid_sequence( veejay_t *info )
{
	int cur = info->seq->current + 1;
	int cycle = 0;
	while( info->seq->samples[ cur ] == 0 )
	{
		cur ++;
		if( cur >= MAX_SEQUENCES && !cycle)
		{
			cur = 0;
			cycle = 1;
		}
		else if ( cur >= MAX_SEQUENCES && cycle )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "No valid sequence to play. Sequence Play disabled");
			info->seq->active = 0;
			return -1;
		}
	}
	return cur;
}

static	int	vj_perform_try_sequence( veejay_t *info )
{
	if(! info->seq->active )
		return 0;
	
	if( sample_get_loop_dec( info->uc->sample_id ) >= 1 )
	{
		int n = vj_perform_valid_sequence( info );
		if( n >= 0 )
		{
			sample_set_loop_dec( info->uc->sample_id, 0 ); //reset loop
		
			veejay_msg(0, "Sequence play selects sample %d", info->seq->samples[info->seq->current]);
		
			seq_play_sample(info, n );
			return 1;
		}
		return 0;
	}		

	return 0;
}

static	void	seq_play_sample( veejay_t *info, int n)
{
	info->seq->current = n;
//	int id = info->seq->samples[n];
	int which_sample = 0;
	int offset = sample_get_first_mix_offset( info->uc->sample_id, &which_sample, info->seq->samples[info->seq->current] );

	veejay_set_sample_f( info, info->seq->samples[ info->seq->current ] , offset );
}

static int vj_perform_increase_sample_frame(veejay_t * info, long num)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    int start,end,looptype,speed;
    int ret_val = 1;

    if(sample_get_short_info(info->uc->sample_id,&start,&end,&looptype,&speed)!=0) return -1;

    settings->current_playback_speed = speed;

    int cur_sfd = sample_get_framedups( info->uc->sample_id );
    int max_sfd = sample_get_framedup( info->uc->sample_id );
	
	if( num )
		cur_sfd ++;

	if( max_sfd > 0 ) {
		if( cur_sfd >= max_sfd )
		{
		//	settings->current_frame_num += num;
			cur_sfd = 0;
		}
		sample_set_framedups( info->uc->sample_id , cur_sfd);
		
		if( cur_sfd != 0 ) 
			return 1;

	}
	settings->current_frame_num += num;

	if( num == 0 )
		return 1;

    if (speed >= 0) {		/* forward play */

	if(looptype==3)
	{
		int range = end - start;
		int n2   = start + ((int) ( (double)range * rand()/(RAND_MAX)));
		settings->current_frame_num = n2;
	}

	if (settings->current_frame_num > end || settings->current_frame_num < start) {
	    switch (looptype) {
		    case 2:
			sample_loopcount( info->uc->sample_id);
			info->uc->direction = -1;
			if(!vj_perform_try_sequence( info ) )
			{
				editlist *E = sample_get_editlist(info->uc->sample_id);
				sample_apply_loop_dec( info->uc->sample_id,E->video_fps);
				veejay_set_frame(info, end);
				veejay_set_speed(info, (-1 * speed));
			}
			sample_loopcount( info->uc->sample_id);
			break;
		    case 1:
			sample_loopcount( info->uc->sample_id);
			if(! info->seq->active )
				veejay_set_frame(info, start);
			else
			{
				int n = vj_perform_valid_sequence( info );
				if( n >= 0 )
				{
					seq_play_sample( info, n );				
				}
				else
					veejay_set_frame(info,start);
			}
			break;
		    case 3:
			sample_loopcount( info->uc->sample_id);
			veejay_set_frame(info, start);
			break;
		    default:
			veejay_set_frame(info, end);
			veejay_set_speed(info, 0);
	    }
	}
    } else {			/* reverse play */
	if( looptype == 3)
	{
		int range = end - start;
		int n2   = end - ((int) ( (double)range*rand()/(RAND_MAX)));
		settings->current_frame_num = n2;
	}

	if (settings->current_frame_num < start || settings->current_frame_num >= end ) {
	    switch (looptype) {
	    case 2:
		sample_loopcount( info->uc->sample_id);

		info->uc->direction = 1;
		if(!vj_perform_try_sequence(info) )
		{
			editlist *E = sample_get_editlist(info->uc->sample_id);
		  	sample_apply_loop_dec( info->uc->sample_id, E->video_fps);
			veejay_set_frame(info, start);
			veejay_set_speed(info, (-1 * speed));
		}
		break;
	    case 1:
		sample_loopcount( info->uc->sample_id);

		if(!info->seq->active)
			veejay_set_frame(info, end);
		else
		{
			int n = vj_perform_valid_sequence( info );
			if( n >= 0 )
			{
				seq_play_sample(info, n );
			}
			else
				veejay_set_frame(info,end);
		}
		break;
	    case 3:
		sample_loopcount( info->uc->sample_id);

		veejay_set_frame(info,end);
		break;
	    default:
		veejay_set_frame(info, start);
		veejay_set_speed(info, 0);
	    }
	}
    }
 //   sample_update_offset( info->uc->sample_id, settings->current_frame_num );	
   	sample_set_resume( info->uc->sample_id, settings->current_frame_num );
   	vj_perform_rand_update( info );

    return ret_val;
}

static long vj_perform_alloc_row(veejay_t *info, int c, int plane_len)
{
	if( fx_chain_buffer )
		return 1;

	size_t frame_len = RUP8( ((plane_len+helper_frame->width)/7)*8 );
	uint8_t *buf = vj_malloc(sizeof(uint8_t) * frame_len * 3 * 3);

	if(!buf)
		return 0;

	mlock( buf, frame_len * 3 * 3 * sizeof(uint8_t));

	frame_buffer[c]->Y = buf;
	frame_buffer[c]->Cb = frame_buffer[c]->Y + frame_len;
	frame_buffer[c]->Cr = frame_buffer[c]->Cb + frame_len;
	frame_buffer[c]->P0  = buf + (frame_len * 3);
	frame_buffer[c]->P1  = frame_buffer[c]->P0 + (frame_len*3);
	return (frame_len * 3 * 3);
}

static void vj_perform_free_row(int c)
{
	if( fx_chain_buffer )
		return;
	
	size_t frame_len = RUP8( (((helper_frame->len)+helper_frame->width)/7)*8 );

	if(frame_buffer[c]->Y)
	{
		munlock( frame_buffer[c]->Y, frame_len * 3 * 3 * sizeof(uint8_t));
		free( frame_buffer[c]->Y );
	}
	frame_buffer[c]->Y = NULL;
	frame_buffer[c]->Cb = NULL;
	frame_buffer[c]->Cr = NULL;
	frame_buffer[c]->P0 = NULL;
	frame_buffer[c]->P1 = NULL;
	cached_sample_frames[c+1] = 0;
	cached_tag_frames[c+1] = 0;
}

#define vj_perform_row_used(c) ( frame_buffer[c]->Y == NULL ? 0 : 1 )
static int	vj_perform_verify_rows(veejay_t *info )
{
	if( pvar_.fx_status == 0 )
		return 0;

	if( fx_chain_buffer )
		return 1;
	
	int c,v,has_rows = 0;
	const int w = info->video_output_width;
	const int h = info->video_output_height;
	for(c=0; c < SAMPLE_MAX_EFFECTS; c++)
	{
	  	v = (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ? 
			sample_get_effect_any(info->uc->sample_id,c) : vj_tag_get_effect_any(info->uc->sample_id,c));
	
		if( v > 0)
	  	{
			if( !vj_perform_row_used(c))
			{
			    if( vj_perform_alloc_row( info, c, w * h ) <= 0 ) {
					veejay_msg(0, "Unable to allocate memory for FX entry %d",c );
					veejay_change_state( info, LAVPLAY_STATE_STOP );
					return -1;
				}
			}
			has_rows ++;
		}
	  	else
	  	{
			if(vj_perform_row_used(c))
				vj_perform_free_row(c);	
		}
	}
	return has_rows;
}


static int vj_perform_record_buffer_init()
{
	if(record_buffer->Cb==NULL)
	        record_buffer->Cb = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(helper_frame->uv_len) );
	if(!record_buffer->Cb) return 0;
	if(record_buffer->Cr==NULL)
	        record_buffer->Cr = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(helper_frame->uv_len) );
	if(!record_buffer->Cr) return 0;

	if(record_buffer->Y == NULL)
		record_buffer->Y = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(helper_frame->len));
	if(!record_buffer->Y) return 0;

	veejay_memset( record_buffer->Y , pixel_Y_lo_, helper_frame->len );
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
	if(record_buffer)
		free(record_buffer);
}

int vj_perform_init(veejay_t * info)
{
	#define PRIMARY_FRAMES 8
	const int w = info->video_output_width;
	const int h = info->video_output_height;

	const long frame_len = RUP8( ((w*h)+w+w) );
    unsigned int c;
	long total_used = 0;
    performer_frame_size_ = frame_len * 3;

    // buffer used to store encoded frames (for plain and sample mode)
    frame_buffer = (ycbcr_frame **) vj_calloc(sizeof(ycbcr_frame*) * SAMPLE_MAX_EFFECTS);
    if(!frame_buffer) return 0;

    record_buffer = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame) );
    if(!record_buffer) return 0;

	primary_buffer = (ycbcr_frame **) vj_calloc(sizeof(ycbcr_frame **) * PRIMARY_FRAMES); 
    if(!primary_buffer) return 0;

    size_t buf_len = performer_frame_size_ * sizeof(uint8_t);
	int mlock_success =1 ;

	pribuf_len = PRIMARY_FRAMES * performer_frame_size_;
	pribuf_area = vj_hmalloc( pribuf_len, "in primary buffers" );
	if( !pribuf_area ) {
		return 0;
	}
	
	for( c = 0; c < PRIMARY_FRAMES; c ++ )
	{
		primary_buffer[c] = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
		primary_buffer[c]->Y = pribuf_area + (performer_frame_size_ * c);
		primary_buffer[c]->Cb = primary_buffer[c]->Y  + frame_len;
		primary_buffer[c]->Cr = primary_buffer[c]->Cb + frame_len;
	
		veejay_memset( primary_buffer[c]->Y, pixel_Y_lo_,frame_len);
		veejay_memset( primary_buffer[c]->Cb,128,frame_len);
		veejay_memset( primary_buffer[c]->Cr,128,frame_len);

		total_used += buf_len;
	}

	preview_buffer = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
	preview_max_w = w * 2;
	preview_max_h = h * 2;

	preview_buffer->Y = (uint8_t*) vj_calloc( RUP8(preview_max_w * preview_max_h * 3) );

	video_output_buffer_convert = 0;
	video_output_buffer = (ycbcr_frame**) vj_calloc(sizeof(ycbcr_frame*) * 2 );

    if(!video_output_buffer)
		return 0;

    for(c=0; c < 2; c ++ )
	{
	    video_output_buffer[c] = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
	}

    sample_record_init(frame_len);
    vj_tag_record_init(w,h);

	temp_buffer[0] = (uint8_t*) vj_malloc( buf_len );
	if(!temp_buffer[0]) {
		return 0;
	}
	temp_buffer[1] = temp_buffer[0] + frame_len;
	temp_buffer[2] = temp_buffer[1] + frame_len;
    if(mlock( temp_buffer[0], buf_len ) != 0 )
		mlock_success = 0;

	veejay_memset( temp_buffer[0], pixel_Y_lo_,frame_len);
	veejay_memset( temp_buffer[1],128,frame_len);
	veejay_memset( temp_buffer[2],128,frame_len);

	feedback_buffer[0] = (uint8_t*) vj_malloc( buf_len );
	feedback_buffer[1] = feedback_buffer[0] + frame_len;
	feedback_buffer[2] = feedback_buffer[1] + frame_len;

	if(mlock( feedback_buffer[c], buf_len )!=0)
		mlock_success = 0;

	veejay_memset( feedback_buffer[0], pixel_Y_lo_,frame_len);
	veejay_memset( feedback_buffer[1],128,frame_len);
	veejay_memset( feedback_buffer[2],128,frame_len);

	total_used += buf_len; //temp_buffer
	total_used += buf_len; //feedback_buffer


	helper_frame = (VJFrame*) vj_malloc(sizeof(VJFrame));
	veejay_memcpy(helper_frame, info->effect_frame1, sizeof(VJFrame));

	veejay_memset( &pvar_, 0, sizeof( varcache_t));

	size_t tmp1 = 0;
	if( info->uc->ram_chain ) {
		//allocate fx_chain_buffer
		tmp1 = buf_len * 3 * sizeof(uint8_t) * SAMPLE_MAX_EFFECTS;
		fx_chain_buffer = vj_hmalloc( tmp1, "in fx chain buffers" );
		if(fx_chain_buffer == NULL ) {
			veejay_msg(VEEJAY_MSG_WARNING,"Unable to allocate sufficient memory to keep all FX chain buffers in RAM");
		} else {
			total_used += tmp1;
			fx_chain_buflen = tmp1;
		}
		/*  tmp1 = [ secundary source on entry X ] [ slow motion buffer A ] [ slow motion buffer B ]
		 */
	}

   	 /* allocate space for frame_buffer pointers */
   	 for (c = 0; c < SAMPLE_MAX_EFFECTS; c++) {
		frame_buffer[c] = (ycbcr_frame *) 
			vj_calloc(sizeof(ycbcr_frame));
   	     if(!frame_buffer[c]) return 0;

		 if(fx_chain_buffer != NULL ) {
				uint8_t *ptr = fx_chain_buffer + (c * frame_len * 3 * 3 );
				frame_buffer[c]->Y = ptr;
				frame_buffer[c]->Cb = frame_buffer[c]->Y + frame_len;
				frame_buffer[c]->Cr = frame_buffer[c]->Cb + frame_len;
				frame_buffer[c]->P0  = ptr + (frame_len * 3);
				frame_buffer[c]->P1  = frame_buffer[c]->P0 + (frame_len*3);
		 }
   	 }

	vj_perform_clear_cache();
	veejay_memset( frame_info[0],0,SAMPLE_MAX_EFFECTS);

	veejay_msg(VEEJAY_MSG_INFO,
	 	"Using %.2f MB RAM in performer (memory %s paged to the swap area, %.2f MB pre-allocated for fx-chain)",
		 	((float)total_used/1048576.0f),
			( mlock_success ? "is not going to be" : "may be" ),
			((float)tmp1/1048576.0f)
		); 

	if( info->uc->scene_detection ) {
		vj_el_auto_detect_scenes( info->edit_list, temp_buffer, w,h, info->uc->scene_detection );
	}

	return 1;
}

size_t	vj_perform_fx_chain_size()
{
	return fx_chain_buflen;
}


static void vj_perform_close_audio() {
	if( lin_audio_buffer_ )
		free(lin_audio_buffer_ );
	if( audio_silence_ )
		free(audio_silence_);
	veejay_memset( audio_buffer, 0, sizeof(uint8_t*) * SAMPLE_MAX_EFFECTS );

#ifdef HAVE_JACK
	if(top_audio_buffer) free(top_audio_buffer);
	if(audio_rec_buffer) free(audio_rec_buffer);
	if(audio_render_buffer) free( audio_render_buffer );
	if(down_sample_buffer) free( down_sample_buffer );
	
	int i;
	for(i=0; i <= MAX_SPEED; i ++)
	{
		if(resample_context[i])
			audio_resample_close( resample_context[i] );
		if(downsample_context[i])
			audio_resample_close( downsample_context[i]);
		resample_context[i] = NULL;
		downsample_context[i] = NULL;
	}

#endif
	veejay_msg(VEEJAY_MSG_INFO, "Stopped Audio playback task");
}

int vj_perform_init_audio(veejay_t * info)
{
#ifndef HAVE_JACK
	return 0;
#else
	int i;

	top_audio_buffer =
	    (uint8_t *) vj_calloc(sizeof(uint8_t) * 8 * PERFORM_AUDIO_SIZE);
	if(!top_audio_buffer)
		return 0;

	audio_rec_buffer =
	    (uint8_t *) vj_calloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE);
	if(!audio_rec_buffer)
		return 0;

	down_sample_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * MAX_SPEED *4 );
	if(!down_sample_buffer)
		return 0;
	audio_render_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * MAX_SPEED * 4 );
	if(!audio_render_buffer)
		return 0;

	lin_audio_buffer_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * PERFORM_AUDIO_SIZE * SAMPLE_MAX_EFFECTS );
	if(!lin_audio_buffer_)
		return 0;

	for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
	    audio_buffer[i] = lin_audio_buffer_ + (PERFORM_AUDIO_SIZE * i);
	}
 
	audio_silence_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * PERFORM_AUDIO_SIZE * SAMPLE_MAX_EFFECTS );
	if(!audio_silence_)
		return 0;

	/* 
	 * The simplest way to time stretch the audio is to resample it and then playback the waveform at the original sampling frequency
	 * This also lowers or raises the pitch, making it just like speeding up or down a tape recording. Perfect!
	 */

	for( i = 0; i <= MAX_SPEED; i ++ )
	{
		int out_rate = (info->edit_list->audio_rate * (i+2));
		int down_rate = (info->edit_list->audio_rate / (i+2));

		resample_context[i] = av_audio_resample_init(
					info->edit_list->audio_chans,
					info->edit_list->audio_chans, 
					info->edit_list->audio_rate,
					out_rate,
					SAMPLE_FMT_S16,
					SAMPLE_FMT_S16,
					MAX_SPEED,
					MAX_SPEED,
					0,
					0.8
					);
		if(!resample_context[i])
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize audio upsampler for speed %d", i);
			return 0;
		}
		downsample_context[i] = av_audio_resample_init(
					info->edit_list->audio_chans,
					info->edit_list->audio_chans,
					info->edit_list->audio_rate,
					down_rate,
					SAMPLE_FMT_S16,
					SAMPLE_FMT_S16,
					MAX_SPEED,
					MAX_SPEED,
					0,
					0.8 );
       
		if(!downsample_context[i])
		{
			veejay_msg(VEEJAY_MSG_WARNING, "Cannot initialize audio downsampler for dup %d",i);
			return 0;
		}
	}
	
	return 1;
#endif
}

void vj_perform_free(veejay_t * info)
{
    int fblen = SAMPLE_MAX_EFFECTS; // mjpg buf
    int c;

	munlockall();

    sample_record_free();

	if( socket_buffer ) 
		free( socket_buffer );

    if(info->edit_list->has_audio)
	    vj_perform_close_audio();

    if( fx_chain_buffer == NULL ) {
 	   for (c = 0; c < fblen; c++) {
		if(vj_perform_row_used(c))
			vj_perform_free_row(c);
	   }
    }

    for(c = 0; c < fblen; c ++ )
	{
		if(frame_buffer[c])
			free(frame_buffer[c]);
	}

   	if(frame_buffer) free(frame_buffer);

	for( c = 0;c < PRIMARY_FRAMES; c++ )
	{
		free(primary_buffer[c] );
	}

  	if(primary_buffer) free(primary_buffer);
	

	if(crop_frame)
	{
		if(crop_frame->data[0]) free(crop_frame->data[0]);
		if(crop_frame->data[1]) free(crop_frame->data[1]);
		if(crop_frame->data[2]) free(crop_frame->data[2]);
	}

   if(temp_buffer[0]) free(temp_buffer[0]);
   if(feedback_buffer[0]) free(feedback_buffer[0]);
   vj_perform_record_buffer_free();

	for( c = 0 ; c < 2 ; c ++ )
	{
		if(video_output_buffer[c]->Y )
			free(video_output_buffer[c]->Y);
		if(video_output_buffer[c]->Cb )
			free(video_output_buffer[c]->Cb );
		if(video_output_buffer[c]->Cr )
			free(video_output_buffer[c]->Cr );
		free(video_output_buffer[c]);
	}
	free(video_output_buffer);
	free(helper_frame);

    free(preview_buffer->Y);
	free(preview_buffer);

	if(fx_chain_buffer)
	{
		munlock(fx_chain_buffer, fx_chain_buflen);
		free(fx_chain_buffer);
	}
	if(pribuf_area)
	{
		munlock(pribuf_area, pribuf_len);
		free(pribuf_area);
	}

	if(lzo_)
		lzo_free(lzo_);
}

int vj_perform_preview_max_width() {
		return preview_max_w;
}

int vj_perform_preview_max_height() {
		return preview_max_h;
}

int vj_perform_audio_start(veejay_t * info)
{
	editlist *el = info->edit_list;	

	if (el->has_audio)
	{
#ifdef HAVE_JACK
		vj_jack_initialize();
		int res = vj_jack_init(el);
		if( res <= 0 ) {	
			veejay_msg(0, "Audio playback disabled");
			info->audio = NO_AUDIO;
			return 0;
		}

		if ( res == 2 )
		{
			vj_jack_stop();
			info->audio = NO_AUDIO;
			veejay_msg(VEEJAY_MSG_ERROR,"Please run jackd with a sample rate of %ld",el->audio_rate );
			return 0;
		}

		veejay_msg(VEEJAY_MSG_DEBUG,"Jack audio playback enabled");
		return 1;
#else
		veejay_msg(VEEJAY_MSG_WARNING, "Jack support not compiled in (no audio)");
		return 0;
#endif
	}
	return 0;
}

void vj_perform_audio_stop(veejay_t * info)
{
    if (info->edit_list->has_audio) {
#ifdef HAVE_JACK
	vj_jack_stop();
#endif
	info->audio = NO_AUDIO;
    }
}

void vj_perform_get_primary_frame(veejay_t * info, uint8_t ** frame)
{
    frame[0] = primary_buffer[info->out_buf]->Y;
    frame[1] = primary_buffer[info->out_buf]->Cb;
    frame[2] = primary_buffer[info->out_buf]->Cr;
}

uint8_t	*vj_perform_get_a_work_buffer( )
{
	return primary_buffer[4]->Y;
}
void vj_perform_get_space_buffer( uint8_t *d[4] )
{
        d[0] = primary_buffer[5]->Y;
	d[1] = primary_buffer[5]->Cb;
	d[2] = primary_buffer[5]->Cr;
}

uint8_t *vj_perform_get_preview_buffer()
{
		return preview_buffer->Y;
}

void	vj_perform_get_output_frame( uint8_t **frame )
{
	frame[0] = video_output_buffer[0]->Y;
	frame[1] = video_output_buffer[0]->Cb;
	frame[2] = video_output_buffer[0]->Cr;
}
void	vj_perform_get_crop_dimensions(veejay_t *info, int *w, int *h)
{
	*w = info->video_output_width - info->settings->viewport.left - info->settings->viewport.right;
	*h = info->video_output_height - info->settings->viewport.top - info->settings->viewport.bottom;

}

int	vj_perform_get_cropped_frame( veejay_t *info, uint8_t **frame, int crop )
{
	if(crop)
	{
		VJFrame src;
		veejay_memset( &src, 0, sizeof(VJFrame));

		vj_get_yuv_template( &src,
				info->video_output_width,
				info->video_output_height,
				info->pixel_format );

		src.data[0] = primary_buffer[0]->Y;
		src.data[1] = primary_buffer[0]->Cb;
		src.data[2] = primary_buffer[0]->Cr;

		// yuv crop needs supersampled data
		chroma_supersample( info->settings->sample_mode,&src, src.data);
		yuv_crop( &src, crop_frame, &(info->settings->viewport));
		chroma_subsample( info->settings->sample_mode,crop_frame, crop_frame->data);
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

	/* enough space to supersample*/
	int i;
	for( i = 0; i < 3; i ++ )
	{
		crop_frame->data[i] = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(crop_frame->len) );
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
			vj_malloc(sizeof(uint8_t) * RUP8( dst_w * dst_h) );
	veejay_memset( video_output_buffer[i]->Y, pixel_Y_lo_, dst_w * dst_h );
	video_output_buffer[i]->Cb = (uint8_t*)
			vj_malloc(sizeof(uint8_t) * RUP8( dst_w * dst_h) );
	veejay_memset( video_output_buffer[i]->Cb, 128, dst_w * dst_h );
	video_output_buffer[i]->Cr = (uint8_t*)
			vj_malloc(sizeof(uint8_t) * RUP8(dst_w * dst_h) );
	veejay_memset( video_output_buffer[i]->Cr, 128, dst_w * dst_h );

	}
	frame[0] = video_output_buffer[0]->Y;
	frame[1] = video_output_buffer[0]->Cb;
	frame[2] = video_output_buffer[0]->Cr;
}

static void long2str(uint8_t *dst, uint32_t n)
{
   dst[0] = (n    )&0xff;
   dst[1] = (n>> 8)&0xff;
   dst[2] = (n>>16)&0xff;
   dst[3] = (n>>24)&0xff;
}

static	uint32_t	vj_perform_compress_frame( veejay_t *info, uint8_t *dst, uint32_t *p1_len, uint32_t *p2_len, uint32_t *p3_len)
{
	const int len = info->effect_frame1->width * info->effect_frame1->height;
	const int uv_len = info->effect_frame1->uv_len;
	uint8_t *dstI = dst + 16;

	if(lzo_ == NULL ) {
		lzo_ = lzo_new();
		if( lzo_ == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize lzo encoder :(");
			return 0;
		}
	}

	int i = lzo_compress( lzo_ , primary_buffer[info->out_buf]->Y, dstI, p1_len, len );
	if( i == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to compress Y plane");
		return 0;
	}
	uint32_t size1 = ( *p1_len );
	dstI = dst + 16 + (sizeof(uint8_t) * size1 );
	
	i = lzo_compress( lzo_, primary_buffer[info->out_buf]->Cb, dstI, p2_len, uv_len );
	if( i == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to compress U plane");
		return 0;
	}

	uint32_t size2 = ( *p2_len );
	dstI = dst + 16 + size1 + size2;

	i = lzo_compress( lzo_, primary_buffer[info->out_buf]->Cr, dstI, p3_len, uv_len );
	if( i == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to compress V plane");
		return 0;
	}	

	uint32_t size3 = ( *p3_len );

	long2str( dst,size1);
	long2str( dst+4, size2 );
	long2str( dst+8, size3 );
	long2str( dst+12,info->settings->mcast_mode );

	return 1;
}

void	vj_perform_done_s2( veejay_t *info ) {

	sbic = 0;
	info->settings->unicast_frame_sender = 0;
}

int	vj_perform_send_primary_frame_s2(veejay_t *info, int mcast, int to_mcast_link_id)
{
	char info_line[128];
	int compr_ok = 0;
	uint32_t planes[3] = {0};
	int data_len = 44;  

	memset(info_line, 0, sizeof(info_line) );

	if(socket_buffer == NULL )
		socket_buffer = vj_malloc( 
					RUP8( info->effect_frame1->width * info->effect_frame1->height * 3 )
					);

	uint8_t *sbuf = socket_buffer + (sizeof(uint8_t) * data_len );

	if( sbic == 0 ) {
		compr_ok = vj_perform_compress_frame(info,sbuf, &planes[0], &planes[1], &planes[2]);
		int total = planes[0] + planes[1] + planes[2] + 16;
		if( compr_ok == 0 ) {
			planes[0] = info->effect_frame1->width * info->effect_frame1->height;
			planes[1] = info->effect_frame1->uv_len;
			planes[2] = planes[1];
			veejay_msg(VEEJAY_MSG_WARNING, "Failed to compress frame, sending raw planes: [%d,%d,%d]",
					planes[0],planes[1],planes[2] );
			total = 0;
		}
		sbic = 1;
		/* peer to peer connection */
		sprintf(info_line,
			"%04d%04d%04d%08d%08d%08d%08d", 
			info->effect_frame1->width,
			info->effect_frame1->
			height,info->effect_frame1->format,
			total,
			planes[0],
			planes[1],
			planes[2] );

		veejay_memcpy( socket_buffer, info_line, 44 );
		if( compr_ok == 0 )
		{
			veejay_memcpy( socket_buffer + 44 ,  primary_buffer[info->out_buf]->Y, info->effect_frame1->len );
			veejay_memcpy( socket_buffer + 44 + info->effect_frame1->len,
							     primary_buffer[info->out_buf]->Cb, info->effect_frame1->uv_len );
			veejay_memcpy( socket_buffer + 44 + info->effect_frame1->len + info->effect_frame1->uv_len,
							     primary_buffer[info->out_buf]->Cr, info->effect_frame1->uv_len );
			data_len += 16 + info->effect_frame1->len + (info->effect_frame1->uv_len * 2);
		} else {
			data_len += 16 + planes[0] + planes[1] + planes[2];
		}
	}

	int id = (mcast ? 2: 3);
	int __socket_len = data_len;
	int i;

	if(!mcast) 
	{
		for( i = 0; i < 8 ; i++ ) {
			if( info->rlinks[i] != -1 ) {
				if(vj_server_send_frame( info->vjs[id], info->rlinks[i], socket_buffer, __socket_len,
						info->effect_frame1, info->real_fps )<=0)
				{
					_vj_server_del_client( info->vjs[id], info->rlinks[i] );
				}
				info->rlinks[i] = -1;
			}	
		}
	}
	else
	{		
		if(vj_server_send_frame( info->vjs[id], to_mcast_link_id, socket_buffer, __socket_len,
					info->effect_frame1, info->real_fps )<=0)
		{
			veejay_msg(VEEJAY_MSG_DEBUG,  "Error sending multicast frame.");
		}
	}


	return 1;
}

void	vj_perform_get_output_frame_420p( veejay_t *info, uint8_t **frame, int w, int h )
{
	if(info->pixel_format == FMT_422 || info->pixel_format == FMT_422F)
	{
		frame[0] = video_output_buffer[1]->Y;
		frame[1] = video_output_buffer[1]->Cb;
		frame[2] = video_output_buffer[1]->Cr;
		
		if( video_output_buffer_convert == 0 )
		{
			VJFrame *srci = yuv_yuv_template( video_output_buffer[0]->Y, video_output_buffer[0]->Cb,
						 video_output_buffer[0]->Cr, w, h, 
						 get_ffmpeg_pixfmt(info->pixel_format));
			VJFrame *dsti = yuv_yuv_template( video_output_buffer[1]->Y, video_output_buffer[1]->Cb,
						  video_output_buffer[1]->Cr, w, h,
						  PIX_FMT_YUV420P );

			yuv_convert_any_ac( srci,dsti, srci->format, dsti->format );
			free(srci);
			free(dsti);

			video_output_buffer_convert = 1;
			return;
		}
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
	return 1;
}

void vj_perform_get_primary_frame_420p(veejay_t *info, uint8_t **frame )   
{
	uint8_t *pframe[3];
	pframe[0] = primary_buffer[info->out_buf]->Y;
	pframe[1] = primary_buffer[info->out_buf]->Cb;
	pframe[2] = primary_buffer[info->out_buf]->Cr;
	yuv422to420planar( pframe, temp_buffer, info->effect_frame1->width, info->effect_frame1->height );
	veejay_memcpy( pframe[0],frame[0], info->effect_frame1->len );

	frame[0] = temp_buffer[0];
	frame[1] = temp_buffer[1];
	frame[2] = temp_buffer[2];
}


static int	vj_perform_apply_first(veejay_t *info, vjp_kf *todo_info,
	VJFrame **frames, VJFrameInfo *frameinfo, int e , int c, int n_frame, void *ptr, int playmode)
{
	int n_a = vj_effect_get_num_params(e);
	int entry = e;
	int args[SAMPLE_MAX_PARAMETERS];
	veejay_memset( args, 0 , sizeof(args) );
	
	if( playmode == VJ_PLAYBACK_MODE_TAG )
	{
		if(!vj_tag_get_all_effect_args(todo_info->ref, c, args, n_a, n_frame ))
			return 1;
	}
	else
	{
		if(!sample_get_all_effect_arg( todo_info->ref, c, args, n_a, n_frame))
			return 1;
	}

	return vj_effect_apply( frames, frameinfo, todo_info,entry, args, ptr );
}

#ifdef HAVE_JACK
static void vj_perform_reverse_audio_frame(veejay_t * info, int len,
				    uint8_t * buf)
{
    int i;
    int bps = info->current_edit_list->audio_bps;
    uint8_t sample[bps];
    int x=len*bps;
    for( i = 0; i < x/2 ; i += bps ) {
		veejay_memcpy(sample,buf+i,bps);	
		veejay_memcpy(buf+i ,buf+(x-i-bps),bps);
		veejay_memcpy(buf+(x-i-bps), sample,bps);
	}
}
#endif

static void vj_perform_use_cached_ycbcr_frame(veejay_t *info, int centry, int chain_entry)
{
	int len1 = info->effect_frame1->len;
	
	if( centry == 0 )
	{
		int len2 = ( info->effect_frame1->ssm == 1 ? len1 : 
			     info->effect_frame1->uv_len );
		vj_perform_copy( primary_buffer[0], frame_buffer[chain_entry], len1, len2 );
		frame_buffer[chain_entry]->ssm = info->effect_frame1->ssm;
	}
	else
	{
		int c = centry - 1;
		int len2 = ( frame_buffer[c]->ssm == 1 ? len1 : info->effect_frame2->uv_len );

		vj_perform_copy( primary_buffer[c], frame_buffer[chain_entry], len1, len2 );
		frame_buffer[chain_entry]->ssm = frame_buffer[c]->ssm;
   	 }
}



static int vj_perform_get_subframe(veejay_t * info, int sub_sample,int chain_entry)

{
    int a = info->uc->sample_id;
    int b = sub_sample;
    int sample_a[4];
    int sample_b[4];

    int offset = sample_get_offset(a, chain_entry);	
    int len_b;
	if(sample_get_short_info(b,&sample_b[0],&sample_b[1],&sample_b[2],&sample_b[3])!=0) return -1;

	if(sample_get_short_info(a,&sample_a[0],&sample_a[1],&sample_a[2],&sample_a[3])!=0) return -1;

	len_b = sample_b[1] - sample_b[0];

	int max_sfd = sample_get_framedup( b );
	int cur_sfd = sample_get_framedups( b );

	cur_sfd ++;

	if( max_sfd > 0 ) {
		if( cur_sfd > max_sfd )
		{
			cur_sfd = 0;
		}
		if( sub_sample != a ) sample_set_framedups( b , cur_sfd);
		if( cur_sfd != 0 ) {
			return 1;
		}
	}

	/* offset + start >= end */
	if(sample_b[3] >= 0) /* sub sample plays forward */
	{
	   	offset += sample_b[3]; /* speed */

		if( sample_b[2] == 3 )
			offset = sample_b[0] + ( (int) ( (double) len_b * rand()/RAND_MAX) );
	
		/* offset reached sample end */
    	if(  offset > len_b )
		{
			if(sample_b[2] == 2) /* sample is in pingpong loop */
			{
				/* then set speed in reverse and set offset to sample end */
				//offset = sample_b[1] - sample_b[0];
				offset = len_b;
				sample_set_speed( b, (-1 * sample_b[3]) );
				sample_set_offset(a,chain_entry,offset);
				return sample_b[1];
			}
			if(sample_b[2] == 1)
			{
				offset = 0;
			}
			if(sample_b[2] == 0)
			{
				offset = 0;	
				sample_set_speed(b,0);
			}
			if(sample_b[2] == 3 )
				offset = 0;
		}
		sample_set_offset(a,chain_entry,offset);
		return (sample_b[0] + offset);
	}
	else
	{	/* sub sample plays reverse */
	    offset += sample_b[3]; /* speed */

		if( sample_b[2] == 3 )
			offset = sample_b[0] + ( (int) ( (double) len_b * rand()/RAND_MAX));

		if (  (sample_b[0] + offset ) <=0  ) // < -(len_b)  )
		{
			/* reached start position */
			if(sample_b[2] == 2)
			{
				//offset = sample_b[1] - sample_b[0];
				offset = 0;
				sample_set_speed( b, (-1 * sample_b[3]));
				sample_set_offset(a,chain_entry,offset);
				return sample_b[0];
			}
			if(sample_b[2] == 1)
			{
				offset = len_b;
				//offset = sample_b[1]; // play from end to start
			}	
			if(sample_b[2]== 0)
			{
				sample_set_speed(b , 0);
				offset = 0; // stop playing
			}
			if(sample_b[2] == 3 )
				offset = 0;
		}
		sample_set_offset(a, chain_entry, offset);

		return (sample_b[0] + offset); //1
	}
	return 0;
}


static int vj_perform_get_subframe_tag(veejay_t * info, int sub_sample, int chain_entry)

{
    int sample[4];
    int offset = sample_get_offset(sub_sample, chain_entry);	
    int len;
    
	if(sample_get_short_info(sub_sample,&sample[0],&sample[1],&sample[2],&sample[3])!=0) return -1;

	len = sample[1] - sample[0];
	int max_sfd = sample_get_framedup( sub_sample );
	int cur_sfd = sample_get_framedups( sub_sample );

	cur_sfd ++;

	if( max_sfd > 0 ) {
		if( cur_sfd >= max_sfd )
		{
			cur_sfd = 0;
		}
		sample_set_framedups( sub_sample, cur_sfd);
		if( cur_sfd != 0 )
			return 1;
	}
 
	/* offset + start >= end */
	if(sample[3] >= 0) /* sub sample plays forward */
	{
	   	offset += sample[3]; /* speed */

		if( sample[2] == 3  )
			offset = sample[0] + ( (int) ( (double) len * rand()/RAND_MAX));
	
		/* offset reached sample end */
    		if(  offset > len )
		{
			if(sample[2] == 2) /* sample is in pingpong loop */
			{
				/* then set speed in reverse and set offset to sample end */
				//offset = sample_b[1] - sample_b[0];
				offset = len;
				sample_set_speed( sub_sample, (-1 * sample[3]) );
				sample_set_offset( sub_sample,chain_entry,offset);
				return sample[1];
			}
			if(sample[2] == 1)
			{
				offset = 0;
			}
			if(sample[2] == 0)
			{
				offset = 0;	
				sample_set_speed( sub_sample,0);
			}
			if(sample[2] == 3 )
				offset = 0;
		}

		sample_set_offset(sub_sample,chain_entry,offset);
		return (sample[0] + offset);
	}
	else
	{	/* sub sample plays reverse */
	    	offset += sample[3]; /* speed */
		
		if( sample[2] == 3  )
			offset = sample[0] + ( (int) ( (double) len * rand()/RAND_MAX));
	
		if ( (sample[0] + offset) <= 0  )
		{
			/* reached start position */
			if(sample[2] == 2)
			{
				//offset = sample_b[1] - sample_b[0];
				offset = 0;
				sample_set_speed( sub_sample, (-1 * sample[3]));
				sample_set_offset( sub_sample,chain_entry,offset);
				return sample[0];
			}
			if(sample[2] == 1)
			{
				offset = len;
			//	offset = sample_b[1];
			}	
			if(sample[2]== 0)
			{
				sample_set_speed( sub_sample , 0);
				offset = 0;
			}
			if(sample[2] == 3 )
				offset = 0;
		}
		sample_set_offset(sub_sample, chain_entry, offset);
	
		return (sample[0] + offset);
	}
	return 0;
}

#define ARRAY_LEN(x) ((int)(sizeof(x)/sizeof((x)[0])))
int vj_perform_fill_audio_buffers(veejay_t * info, uint8_t *audio_buf, uint8_t *temporary_buffer, int *sampled_down)
{
#ifdef HAVE_JACK
	video_playback_setup *settings = info->settings;
	int len = 0;
	int speed = sample_get_speed(info->uc->sample_id);
	int bps   =  info->current_edit_list->audio_bps;
	int pred_len = (info->current_edit_list->audio_rate / info->current_edit_list->video_fps );
        int rs = 0;
	int n_samples = 0;

	int cur_sfd = sample_get_framedups( info->uc->sample_id );
	int max_sfd = sample_get_framedup( info->uc->sample_id );

	if( last_rate_[0] != speed || last_rate_[1] != max_sfd ) {
		veejay_memset( temporary_buffer, 0, (2* PERFORM_AUDIO_SIZE * MAX_SPEED) );
		if( max_sfd > 0 )
			veejay_memset( down_sample_buffer,0,PERFORM_AUDIO_SIZE * MAX_SPEED * 4 );
		last_rate_[0] = speed;
		last_rate_[1] = max_sfd;
	}

	if( cur_sfd <= 0 )
	{
		if (speed > 1 || speed < -1) 
		{
			int	a_len = 0;
			int	n_frames = abs(speed);
			uint8_t *tmp = temporary_buffer;
			uint8_t *sambuf = tmp;
			long i,start,end;

			if( n_frames >= MAX_SPEED )
				n_frames = MAX_SPEED - 1;

			if( speed < 0 )
			{
				start = settings->current_frame_num - n_frames;
				end   = settings->current_frame_num;
			}
			else
			{
				start = settings->current_frame_num;
				end   = settings->current_frame_num + n_frames;
			}

			for ( i = start; i < end; i ++ )	
			{
				a_len = vj_el_get_audio_frame(info->current_edit_list,i, tmp);
				if( a_len <= 0 )
				{
					n_samples += pred_len;
					veejay_memset( tmp, 0, pred_len * bps );
					tmp += (pred_len*bps);
				}
				else
				{
					n_samples += a_len;
					tmp += (a_len* bps );
					rs = 1;
				}
			}
			
			if( rs )
			{
				if( speed < 0 )
					vj_perform_reverse_audio_frame(info, n_samples, sambuf );

				int sc = n_frames - 2;
				
				//@ clip sc into range, there isn't a resampler for every speed
				if( sc < 0 ) sc = 0; else if ( sc > MAX_SPEED ) sc = MAX_SPEED;

				n_samples = audio_resample( resample_context[sc],(short*) audio_buf, (short*) sambuf, n_samples );
			}
		} else if( speed == 0 ) {
			n_samples = len = pred_len;
			veejay_memset( audio_buf, 0, pred_len * bps );
		} else	{
			n_samples = vj_el_get_audio_frame( info->current_edit_list, settings->current_frame_num, audio_buf );
			if(n_samples <= 0 )
			{
				veejay_memset( audio_buf,0, pred_len * bps );
				n_samples = pred_len;
			}
			else
			{
				rs = 1;
			}
			if( speed < 0 && rs)
				vj_perform_reverse_audio_frame(info,n_samples,audio_buf);
		}
	
		if( n_samples < pred_len )
		{
			veejay_memset( audio_buf + (n_samples * bps ) , 0, (pred_len- n_samples) * bps );
			n_samples = pred_len;
		} 

	}
	
	if( cur_sfd <= max_sfd  && max_sfd > 1)
	{
		int val = *sampled_down;
		if( cur_sfd == 0 )
		{
			int sc = max_sfd - 2;
			
			if( sc < 0 ) sc = 0; else if ( sc > MAX_SPEED ) sc = MAX_SPEED; 
			// @ resample buffer
			n_samples = audio_resample( downsample_context[ sc ], 
					(short*) down_sample_buffer,(short*) audio_buf, n_samples  );

			*sampled_down = n_samples / max_sfd;
			val = n_samples / max_sfd;
			n_samples = val;
		}
		else
		{
			n_samples = *sampled_down;
		}

		veejay_memcpy( audio_buf, down_sample_buffer + (cur_sfd * val * bps ), val * bps );

	} else {
		*sampled_down = 0;
	}

	return n_samples;
#else
	return 0;
#endif
}

static void vj_perform_apply_secundary_tag(veejay_t * info, int sample_id, int type, int chain_entry )
{				/* second sample */
    int error = 1;
    int nframe;
    int len = 0;
    int centry = -1;
    uint8_t *fb[4] = {
		frame_buffer[chain_entry]->Y,
		frame_buffer[chain_entry]->Cb,
		frame_buffer[chain_entry]->Cr,
   		NULL };

    switch (type)
    {		
	case VJ_TAG_TYPE_YUV4MPEG:	/* playing from stream */
	case VJ_TAG_TYPE_V4L:
	case VJ_TAG_TYPE_VLOOPBACK:
	case VJ_TAG_TYPE_AVFORMAT:
	case VJ_TAG_TYPE_NET:
	case VJ_TAG_TYPE_MCAST:
	case VJ_TAG_TYPE_PICTURE:
	case VJ_TAG_TYPE_COLOR:
	
	centry = vj_perform_tag_is_cached(info,chain_entry, sample_id);

	if (centry == -1)
	{
		if(! vj_tag_get_active( sample_id ) )
		{
			// active stream if neccesaary
			vj_tag_set_active(sample_id, 1 );
		}
		
	  	if (vj_tag_get_active(sample_id) == 1 )
		{	
			int res = vj_tag_get_frame(sample_id, fb, audio_buffer[chain_entry]);
			if(res==1)	
		  		error = 0;
			else
		   		vj_tag_set_active(sample_id, 0); 
		
		}
	}
	else
	{	
		vj_perform_use_cached_ycbcr_frame(info, centry, chain_entry);
	    	error = 0;
	}
	if(!error)
		cached_tag_frames[1 + chain_entry ] = sample_id;	
	break;
	
   case VJ_TAG_TYPE_NONE:
	    nframe = vj_perform_get_subframe_tag(info, sample_id, chain_entry); // get exact frame number to decode
 	    centry = vj_perform_sample_is_cached(info,sample_id, chain_entry);
        if(centry == -1 || info->no_caching)
	    {
			len = vj_perform_get_frame_fx( info, sample_id, nframe, fb, frame_buffer[chain_entry]->P0, frame_buffer[chain_entry]->P1 );	
		    if(len > 0 )
			   error = 0;
	    }
	    else
	    {
			vj_perform_use_cached_ycbcr_frame( info, centry, chain_entry);
			cached_sample_frames[ 1 + chain_entry ] = sample_id;
			error = 0;
	    }
	    if(!error)
	  	 cached_sample_frames[chain_entry+1] = sample_id;

	   break;

    }
}

static	int	vj_perform_get_frame_( veejay_t *info, int s1, long nframe, uint8_t *img[4], uint8_t *p0_buffer[4], uint8_t *p1_buffer[4], int check_sample )
{
	int max_sfd = (s1 ? sample_get_framedup( s1 ) : info->sfd );
	
	if(check_sample && info->settings->feedback == 0) {
		if(info->uc->sample_id == s1 && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ) {

			if( max_sfd <= 1 ) {
				
				vj_perform_copy2( primary_buffer[0], img, info->effect_frame1->len, 	(info->effect_frame1->ssm ? info->effect_frame1->len : info->effect_frame1->uv_len ) );
				
				return 1;
			}

			vj_perform_copy2( primary_buffer[7], img,  info->effect_frame1->len, 	(info->effect_frame1->ssm ? info->effect_frame1->len : info->effect_frame1->uv_len ) );
			return 1;
		}
	}

	editlist *el = ( s1 ? sample_get_editlist(s1) : info->edit_list);
	if( el == NULL ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Selected mixing source and ID does not exist, Use / to toggle mixing type!" );
		if( info->edit_list == NULL ) {
			veejay_msg(VEEJAY_MSG_WARNING, "No plain source playing");
			return 0;
		} else {
			veejay_msg(VEEJAY_MSG_WARNING, "Fetching frame %d from plain source", nframe );
			el = info->edit_list;
		}
	}

	if( max_sfd <= 1 )
		return vj_el_get_video_frame( el, nframe, img );
	
	int cur_sfd = (s1 ? sample_get_framedups(s1 ) : info->settings->simple_frame_dup );
	int speed = sample_get_speed(s1);
	int uv_len = (info->effect_frame1->ssm ? info->effect_frame1->len : info->effect_frame1->uv_len );

	long p0_frame = 0;
	long p1_frame = 0;

	long	start = ( s1 ? sample_get_startFrame(s1) : info->settings->min_frame_num);
	long	end   = ( s1 ? sample_get_endFrame(s1) : info->settings->max_frame_num );

	if( cur_sfd == 0 ) {
		p0_frame = nframe;
		vj_el_get_video_frame( el, p0_frame, p0_buffer );
		p1_frame = nframe + speed;

		if( speed > 0 && p1_frame > end )
			p1_frame = end;
		else if ( speed < 0 && p1_frame < start )
			p1_frame = start;

		if( p1_frame != p0_frame )
			vj_el_get_video_frame( el, p1_frame, p1_buffer );
		
		vj_perform_copy3( p0_buffer, img, info->effect_frame1->len, uv_len );

	} else {
		const uint32_t N = max_sfd;
		const uint32_t n1 = cur_sfd;
		const float frac = 1.0f / (float) N * n1;

		vj_frame_slow_threaded( p0_buffer, p1_buffer, img , info->effect_frame1->len, uv_len, frac );
		
		if( (n1 + 1 ) == N ) {
			vj_perform_copy3( img, p0_buffer, info->effect_frame1->len,uv_len );
		}
	}
	
	return 1;
}


static int vj_perform_get_frame_fx(veejay_t *info, int s1, long nframe, uint8_t *frame[4], uint8_t *p0plane, uint8_t *p1plane)
{
	const int uv_len = (info->effect_frame1->ssm ? info->effect_frame1->uv_len : info->effect_frame1->len );

	uint8_t *p0_buffer[4] = {
		p0plane,
		p0plane + info->effect_frame1->len,
		p0plane + info->effect_frame1->len + uv_len,
		NULL,
	};
	uint8_t *p1_buffer[4] = {
		p1plane,
		p1plane + info->effect_frame1->len,
		p1plane + info->effect_frame1->len + uv_len,
		NULL
	};

	return vj_perform_get_frame_(info, s1, nframe,frame, p0_buffer, p1_buffer,1 );
}


static void vj_perform_apply_secundary(veejay_t * info, int sample_id, int type,
			       int chain_entry)
{				/* second sample */
    int error = 1;
    int nframe;
    int len;

    int res = 1;
    int centry = -1;

    uint8_t *fb[4] = {
		frame_buffer[chain_entry]->Y,
		frame_buffer[chain_entry]->Cb,
		frame_buffer[chain_entry]->Cr,
   		NULL };

    switch (type)
    {
	case VJ_TAG_TYPE_YUV4MPEG:	
	case VJ_TAG_TYPE_V4L:
	case VJ_TAG_TYPE_VLOOPBACK:
	case VJ_TAG_TYPE_AVFORMAT:
	case VJ_TAG_TYPE_NET:
	case VJ_TAG_TYPE_MCAST:
	case VJ_TAG_TYPE_COLOR:
	case VJ_TAG_TYPE_PICTURE:
	case VJ_TAG_TYPE_GENERATOR:

	centry = vj_perform_tag_is_cached(info,chain_entry, sample_id); // is it cached?
	//@ not cached
	if (centry == -1 )
	{
		if(! vj_tag_get_active( sample_id ) )
		{
			// active stream if neccesaary
			vj_tag_set_active(sample_id, 1 );
		}

		if (vj_tag_get_active(sample_id) == 1)
		{ 
			res = vj_tag_get_frame(sample_id, fb,  audio_buffer[chain_entry]);
			if(res)
				error = 0;                               
			else
				vj_tag_set_active(sample_id, 0); // stop stream

	    }
	}
	else
	{
	    vj_perform_use_cached_ycbcr_frame(info,centry, chain_entry);
	    error = 0;
	}
	if(!error )
		cached_tag_frames[1 + chain_entry ] = sample_id;	
	break;
    case VJ_TAG_TYPE_NONE:
	    	nframe = vj_perform_get_subframe(info, sample_id, chain_entry); // get exact frame number to decode
 	  	centry = vj_perform_sample_is_cached(info,sample_id, chain_entry);
	    	if(centry == -1 || info->no_caching) {
				len = vj_perform_get_frame_fx( info, sample_id, nframe, fb, frame_buffer[chain_entry]->P0, frame_buffer[chain_entry]->P1 );	
			    if(len > 0 )
				    error = 0;
			}
			else
			{
				vj_perform_use_cached_ycbcr_frame(info,centry, chain_entry);	
				cached_sample_frames[ 1 + chain_entry ] = sample_id;
				error = 0;
			}	
			if(!error)
				cached_sample_frames[chain_entry+1] = sample_id;
			break;
    }
}

//@ TODO: Render all image effects in subchain
static void	vj_perform_tag_render_chain_entry(veejay_t *info, int chain_entry)
{
	if (vj_tag_get_chain_status(info->uc->sample_id, chain_entry))
	{
		VJFrame *frames[2];
		VJFrameInfo *frameinfo;
		video_playback_setup *settings = info->settings;
		frames[0] = info->effect_frame1;
		frames[1] = info->effect_frame2;
		frameinfo = info->effect_frame_info;
   		frames[0]->data[0] = primary_buffer[0]->Y;
   		frames[0]->data[1] = primary_buffer[0]->Cb;
  		frames[0]->data[2] = primary_buffer[0]->Cr;
		vjp_kf *setup;
    		setup = info->effect_info;
   	 	setup->ref = info->uc->sample_id;

		int effect_id = vj_tag_get_effect_any(info->uc->sample_id, chain_entry); // what effect is enabled
		if (effect_id > 0)
		{
 			int sub_mode = vj_effect_get_subformat(effect_id);
			int ef = vj_effect_get_extra_frame(effect_id);
			int res = 0;
			if(ef)
			{
		    		int sub_id = vj_tag_get_chain_channel(info->uc->sample_id, chain_entry); // what id
		    		int source = vj_tag_get_chain_source(info->uc->sample_id,chain_entry); // what source type chain_entry);
		    		
					vj_perform_apply_secundary_tag(info,sub_id,source,chain_entry ); // get it
			 		frames[1]->data[0] = frame_buffer[chain_entry]->Y;
	   	 			frames[1]->data[1] = frame_buffer[chain_entry]->Cb;
		    			frames[1]->data[2] = frame_buffer[chain_entry]->Cr;

					int done   = 0;
					int do_ssm =  vj_perform_preprocess_has_ssm( info, sub_id, source);
					int	subrender = vj_tag_get_subrender( info->uc->sample_id );

					if( subrender ) {

					//			if(do_ssm >= 0 ) {
		//				if( (frames[1]->ssm == 0 && do_ssm == 0) || (frames[1]->ssm == 1 && do_ssm == 1 ) || (frames[1]->ssm == 0 && do_ssm == 1 )) {
							//@ call render now
							frames[1]->ssm = vj_perform_preprocess_secundary( info, sub_id,source,sub_mode,chain_entry, frames, frameinfo );
							done = 1;
		//				}
					}


					if( chain_entry >= 1) {
						frames[1]->ssm = (source != 0 ? vj_perform_previously_sampled(chain_entry,sub_id,cached_tag_frames) : 0);
					}

					// sample B
	   				if(sub_mode && frames[1]->ssm == 0)
					{
						chroma_supersample(settings->sample_mode,frames[1],frames[1]->data );
						frames[1]->ssm = 1;
						frame_buffer[chain_entry]->ssm = 1;	
					}
					else if (!sub_mode && frames[1]->ssm == 1 )
					{
					  chroma_subsample( settings->sample_mode,frames[1],frames[1]->data );
					  frames[1]->ssm = 0;
					  frame_buffer[chain_entry]->ssm = 0;
					}

					if(!done && do_ssm >= 0) {
						if( (do_ssm == 1 && frames[1]->ssm == 1) || (do_ssm == 0 && frames[1]->ssm == 0) ) {
						vj_perform_preprocess_secundary( info, sub_id,source,sub_mode,chain_entry, frames, frameinfo );

					}

				}
			}
		
			if( sub_mode && frames[0]->ssm == 0 )
			{
				chroma_supersample(settings->sample_mode,frames[0],frames[0]->data );
				frames[0]->ssm = 1;	
			}
			else if(!sub_mode && frames[0]->ssm == 1)
   			{
                		chroma_subsample(settings->sample_mode,frames[0],frames[0]->data);
                 		frames[0]->ssm = 0;
           		}

			if(vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,
						(int) settings->current_frame_num, 
						vj_tag_get_plugin(info->uc->sample_id,chain_entry,NULL),info->uc->playback_mode) == -2) {
				void *pfx = vj_effect_activate( effect_id, &res );
				if( res )  {
					settings->fxrow[chain_entry] = effect_id;
					if( pfx ) {
						vj_tag_get_plugin(info->uc->sample_id,chain_entry,pfx);
						vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,(int) settings->current_frame_num,pfx,info->uc->playback_mode );
					}
				}
			}
	    } // if
	} // for
}

static	int	vj_perform_preprocess_has_ssm( veejay_t *info, int id, int mode)
{
	int n;

	if(info->settings->fxdepth==0)
		return -1;

	switch( mode ) {
		case VJ_PLAYBACK_MODE_SAMPLE:	
			for( n=0; n < 3; n ++ ) {
				if( sample_get_chain_status(id, n ) ) {
					int fx_id = sample_get_effect_any(id,n );
					if( fx_id > 0 ) 
						return vj_effect_get_subformat(fx_id);
				}
			}
			break;			
		case VJ_PLAYBACK_MODE_TAG:	
			for( n=0; n < 3; n ++ ) {
				if( vj_tag_get_chain_status(id, n ) ) {
					int fx_id = vj_tag_get_effect_any(id,n );
					if( fx_id > 0 ) 
						return vj_effect_get_subformat(fx_id);
				}
			}
			break;	
	}
	return -1;		
}

static	int	vj_perform_preprocess_secundary( veejay_t *info, int id, int mode,int current_ssm,int chain_entry, VJFrame **frames, VJFrameInfo *frameinfo )
{
	int n;
	int cur_id = info->effect_info->ref;
	int ssm = current_ssm;
	video_playback_setup *settings = info->settings;
	vjp_kf *setup = NULL;
		
	VJFrame a,b;
	veejay_memcpy(&a, frames[1], sizeof(VJFrame));
	veejay_memcpy(&b, frames[0], sizeof(VJFrame));
	
	VJFrame *F[2];
	F[0] = &a;	
	F[1] = &b;

	switch( mode ) {
		case VJ_PLAYBACK_MODE_SAMPLE:	
			setup = info->effect_info;
			setup->ref    = id;	
			for( n=0; n < 3; n ++ ) {
				if( sample_get_chain_status(id, n ) ) {
					int fx_id = sample_get_effect_any(id,n );
					if( fx_id > 0 ) {
						int sm = vj_effect_get_subformat(fx_id);
						int ef = vj_effect_get_extra_frame(fx_id);
						if( ef ) continue;
						if( sm ) {
						   if( !ssm ) {
							if(a.ssm == 0 )
								chroma_supersample( settings->sample_mode,F[0],F[0]->data );
							F[0]->ssm = 1;
							frame_buffer[chain_entry]->ssm = 1;
							ssm = 1;
							}
						} else if ( ssm ) {
							if(a.ssm == 1 ) 
								chroma_subsample( settings->sample_mode,F[0],F[0]->data);
							F[0]->ssm = 0;
							frame_buffer[chain_entry]->ssm = 1;
							ssm = 0;
							}	
						
						if(vj_perform_apply_first(info,setup,F,frameinfo,fx_id,n,(int) settings->current_frame_num,
								       sample_get_plugin(id,n,NULL),mode	)==-2) {
							int res = 0;
							void *pfx = vj_effect_activate( fx_id, &res );
							if( res )  {
								settings->fxrow[n] = fx_id;
								if( pfx ) {
									sample_get_plugin(id,n,pfx);
								
									
									vj_perform_apply_first(info,setup,F,frameinfo,fx_id,n,(int) settings->current_frame_num,pfx,mode );
								}
							}
						}
					}
				}	
			}
			break;
		case VJ_PLAYBACK_MODE_TAG:
			setup = info->effect_info;
			setup->ref    = id;	
			for( n=0; n < 3; n ++ ) {
				if( vj_tag_get_chain_status(id, n ) ) {
					int fx_id = vj_tag_get_effect_any(id,n );
					if( fx_id > 0 ) {
						int sm = vj_effect_get_subformat(fx_id);
						int ef = vj_effect_get_extra_frame(fx_id);
						if( ef ) continue;
						if( sm ) {
  						 if( !ssm ) {
							chroma_supersample( settings->sample_mode,F[0],F[0]->data );
							F[0]->ssm = 1;
							frame_buffer[chain_entry]->ssm = 1;
							ssm = 1;
							}
						} else if ( ssm ) {
							chroma_subsample( settings->sample_mode,F[0],F[0]->data);
							F[0]->ssm = 0;
							frame_buffer[chain_entry]->ssm = 1;
							ssm = 0;
							}	

							// logic to super/sub sample
						}
						if(vj_perform_apply_first(info,setup,F,frameinfo,fx_id,n,(int) settings->current_frame_num,
								       vj_tag_get_plugin(id,n,NULL),mode	) == -2 ) {
							int res = 0;
							void *pfx = vj_effect_activate( fx_id, &res);
							if( res ) {
								settings->fxrow[n] = fx_id; 
								if(pfx) {
									vj_tag_get_plugin( id,n, pfx );
									vj_perform_apply_first(info,setup,F,frameinfo,fx_id,n,(int) settings->current_frame_num, pfx,mode );
								}
							}
						}

					}
				}	
			break;
	}
	info->effect_info->ref = cur_id;
	
	return ssm;
}

static void	vj_perform_render_chain_entry(veejay_t *info, int chain_entry)
{
	if (sample_get_chain_status(info->uc->sample_id, chain_entry))
	{
		VJFrame *frames[2];
		VJFrameInfo *frameinfo;
		video_playback_setup *settings = info->settings;
		
		frames[0] = info->effect_frame1;
		frames[1] = info->effect_frame2;
    		
		frameinfo = info->effect_frame_info;
		frameinfo->timecode = settings->current_frame_num;
    	
		frames[0]->data[0] = primary_buffer[0]->Y;
   		frames[0]->data[1] = primary_buffer[0]->Cb;
    		frames[0]->data[2] = primary_buffer[0]->Cr;

		vjp_kf *setup = info->effect_info;
    		setup->ref = info->uc->sample_id;

		int effect_id =
			sample_get_effect_any(info->uc->sample_id, chain_entry); // what effect is enabled

		if (effect_id > 0)
		{
 			int sub_mode = vj_effect_get_subformat(effect_id);
			int ef = vj_effect_get_extra_frame(effect_id);
			if(ef)
			{
		    		int sub_id =
					sample_get_chain_channel(info->uc->sample_id,
						 chain_entry); // what id
		    		int source =
					sample_get_chain_source(info->uc->sample_id, // what source type
						chain_entry);
				vj_perform_apply_secundary(info,sub_id,source,chain_entry); // get it

			 	frames[1]->data[0] = frame_buffer[chain_entry]->Y;
	   	 		frames[1]->data[1] = frame_buffer[chain_entry]->Cb;
		    		frames[1]->data[2] = frame_buffer[chain_entry]->Cr;
				
				int done   = 0;
				int do_ssm =  vj_perform_preprocess_has_ssm( info, sub_id, source);
				int	subrender = sample_get_subrender( info->uc->sample_id );

				if( subrender ) {
		//		if(do_ssm >= 0 ) {
		//			if( (frames[1]->ssm == 0 && do_ssm == 0) || (frames[1]->ssm == 1 && do_ssm == 1 )
		///					|| (frames[1]->ssm == 0 && do_ssm == 1 )) {
						//@ call render now
						frames[1]->ssm = vj_perform_preprocess_secundary( info, sub_id,source,sub_mode,chain_entry, frames, frameinfo );
						done = 1;
		//			}
				}

				if( chain_entry >= 1) {
					frames[1]->ssm = (source == 0 ? vj_perform_previously_sampled(chain_entry,sub_id,cached_sample_frames) : 0);
				}

				if(frames[1]->ssm == 0 && sub_mode)
				{
					chroma_supersample(
						settings->sample_mode,
						frames[1],
						frames[1]->data );
					frames[1]->ssm = 1;
					frame_buffer[chain_entry]->ssm = 1;
				}
				else if(frames[1]->ssm == 1 && !sub_mode )
				{	
					chroma_subsample(
						settings->sample_mode,
						frames[1],
						frames[1]->data );
					frames[1]->ssm = 0;
					frame_buffer[chain_entry]->ssm = 0;
				}
				
				if(!done && do_ssm >= 0) {
					if( (do_ssm == 1 && frames[1]->ssm == 1) || (do_ssm == 0 && frames[1]->ssm == 0) ) {
						vj_perform_preprocess_secundary( info, sub_id,source,sub_mode,chain_entry, frames, frameinfo );
					//FIXME possible dead code
					}
				}
			}

			if( sub_mode && frames[0]->ssm == 0)
			{
				chroma_supersample(
					settings->sample_mode,
					frames[0],
					frames[0]->data );

				frames[0]->ssm = 1;
			}
			else if(!sub_mode && frames[0]->ssm == 1)
			{
				chroma_subsample(
					settings->sample_mode,
					frames[0],
					frames[0]->data );
				frames[0]->ssm = 0;
			}
		
			if( vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,
				(int) settings->current_frame_num, sample_get_plugin(info->uc->sample_id,chain_entry,NULL),info->uc->playback_mode   ) == -2 ) {
					int res = 0;
					void *pfx = vj_effect_activate( effect_id, &res );
					if( res )  {
						settings->fxrow[chain_entry] = effect_id;
						if( pfx ) {
							sample_get_plugin(info->uc->sample_id,chain_entry,pfx);
							vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,(int) settings->current_frame_num,pfx,info->uc->playback_mode  );
						}
					}
				}
	    	} // if
	} // status
}

static int vj_perform_sample_complete_buffers(veejay_t * info, int *hint444)
{
	int chain_entry;
	vjp_kf *setup;
	VJFrame *frames[2];
	setup = info->effect_info;

	frames[0] = info->effect_frame1;
	frames[1] = info->effect_frame2;
    	setup->ref = info->uc->sample_id;
    	frames[0]->data[0] = primary_buffer[0]->Y;
   	frames[0]->data[1] = primary_buffer[0]->Cb;
    	frames[0]->data[2] = primary_buffer[0]->Cr;
	if(pvar_.fader_active)
		vj_perform_pre_chain( info, frames[0] );

	for(chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
	{
		vj_perform_render_chain_entry(info, chain_entry);
	}
	*hint444 = frames[0]->ssm;

	return 1;
}

static int vj_perform_tag_complete_buffers(veejay_t * info,int *hint444  )
{
	int chain_entry;
	VJFrame *frames[2];

	frames[0] = info->effect_frame1;
	frames[1] = info->effect_frame2;
	frames[0]->data[0] = primary_buffer[0]->Y;
	frames[0]->data[1] = primary_buffer[0]->Cb;
	frames[0]->data[2] = primary_buffer[0]->Cr;

	if( pvar_.fader_active )
		vj_perform_pre_chain( info, frames[0] );


	for(chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
		vj_perform_tag_render_chain_entry( info, chain_entry);

	*hint444 = frames[0]->ssm;
	return 1;
}

static void vj_perform_plain_fill_buffer(veejay_t * info)
{
	video_playback_setup *settings = (video_playback_setup*)  info->settings;
	uint8_t *frame[4];
	int ret = 0;
	frame[0] = primary_buffer[0]->Y;
	frame[1] = primary_buffer[0]->Cb;
	frame[2] = primary_buffer[0]->Cr;
	frame[3] = NULL;

	uint8_t *p0_buffer[4] = {
		primary_buffer[7]->Y,
		primary_buffer[7]->Cb,
		primary_buffer[7]->Cr,
       		NULL	};

	uint8_t *p1_buffer[4]= {
		primary_buffer[4]->Y,
		primary_buffer[4]->Cb,
		primary_buffer[4]->Cr,
       		NULL	};

	if( info->settings->feedback && info->settings->feedback_stage > 1 ) {
		vj_perform_copy3( feedback_buffer, frame, info->effect_frame1->len, info->effect_frame1->uv_len );
		return;
	}


	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
	{
		ret = vj_perform_get_frame_(info, info->uc->sample_id, settings->current_frame_num,frame, p0_buffer, p1_buffer,0 );
	} else if ( info->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN ) {
		ret = vj_perform_get_frame_(info, 0, settings->current_frame_num,frame, p0_buffer, p1_buffer,0 );

	}

	if(ret <= 0)
	{
		veejay_msg(0, "Unable to queue video frame %d", settings->current_frame_num );
	}
}
static int rec_audio_sample_ = 0;
static int vj_perform_render_sample_frame(veejay_t *info, uint8_t *frame[4], int sample)
{
	int audio_len = 0;
	int res = 0;

	if( info->current_edit_list->has_audio )
	{
		audio_len = vj_perform_fill_audio_buffers(info, audio_rec_buffer, audio_render_buffer + (2* PERFORM_AUDIO_SIZE * MAX_SPEED), &rec_audio_sample_ );
	} 

	res = sample_record_frame( sample,frame,audio_rec_buffer,audio_len,info->pixel_format );

	return res;
}

	
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4])
{
	int sample_id = info->uc->sample_id;
	
	if(info->settings->offline_record)
		sample_id = info->settings->offline_tag_id;

	if(info->settings->offline_record)
	{
		if (!vj_tag_get_frame(sample_id, frame, NULL))
	   	{
			return 0;//skip and dont care
		}
	}

	return vj_tag_record_frame( sample_id, frame, NULL, 0, info->pixel_format);
}	

static int vj_perform_record_commit_single(veejay_t *info)
{
   char filename[512];
  //int n_files = 0;
  if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
  {
 	if(sample_get_encoded_file(info->uc->sample_id, filename))
  	{
		int df = vj_event_get_video_format();
		int id = 0;
		if ( df == ENCODER_YUV4MPEG || df == ENCODER_YUV4MPEG420 ) {
			id = veejay_create_tag( info, VJ_TAG_TYPE_YUV4MPEG,filename,info->nstreams,0,0 );
  		}
		else 
		{
			id = veejay_edit_addmovie_sample(info,filename, 0 );
		}
		if(id <= 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error trying to add %s as sample or stream", filename);
			return 0;
		}
		return id;
 	 }
  }

  if(info->uc->playback_mode==VJ_PLAYBACK_MODE_TAG)
  {
	 int stream_id = (info->settings->offline_record ? info->settings->offline_tag_id : info->uc->sample_id);
 	 if(vj_tag_get_encoded_file(stream_id, filename))
  	 {
		int df = vj_event_get_video_format();
		int id = 0;
		if( df == ENCODER_YUV4MPEG || df == ENCODER_YUV4MPEG420 ) {
			id = veejay_create_tag( info, VJ_TAG_TYPE_YUV4MPEG,filename,info->nstreams,0,0 );
		} else {
			id = veejay_edit_addmovie_sample(info, filename, 0);
		}
		if( id <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Adding file %s to new sample", filename);
			return 0;
		}
		return id;
	}
  }
  return 0;
}

void vj_perform_record_stop(veejay_t *info)
{
 video_playback_setup *settings = info->settings;
 int df = vj_event_get_video_format();

 if(info->uc->playback_mode==VJ_PLAYBACK_MODE_SAMPLE)
 {
	 sample_reset_encoder(info->uc->sample_id);
	 sample_reset_autosplit(info->uc->sample_id);
 	 if( settings->sample_record && settings->sample_record_switch)
	 {
		settings->sample_record_switch = 0;
		if( df != ENCODER_YUV4MPEG && df != ENCODER_YUV4MPEG420 ) {
			veejay_set_sample( info,sample_size()-1);
			veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d", sample_size()-1);
		} else {
			veejay_msg(VEEJAY_MSG_WARNING, "Not autoplaying new streams.");
		}
	 }
	 settings->sample_record = 0;
	 settings->sample_record_id = 0;
	 settings->sample_record_switch =0;
	 settings->render_list = 0;
 } else if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
 {
	int stream_id = (settings->offline_record ? settings->offline_tag_id : info->uc->sample_id);
	int play = settings->tag_record_switch;
	vj_tag_reset_encoder(stream_id);
        vj_tag_reset_autosplit(stream_id);
	if(settings->offline_record)
	{
		play = settings->offline_created_sample;
		settings->offline_record = 0;
		settings->offline_created_sample = 0;
		settings->offline_tag_id = 0;
	}
	else 
	{
		settings->tag_record = 0;
		settings->tag_record_switch = 0;
	}

	if(play)
	{
		if(df != ENCODER_YUV4MPEG && df != ENCODER_YUV4MPEG420)
		{
			info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
			veejay_set_sample(info ,sample_size()-1);
			veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d", sample_size()-1);
		}
		else {

			veejay_msg(VEEJAY_MSG_WARNING, "Not autoplaying new streams.");
		}
 	}
  }

}


void vj_perform_record_sample_frame(veejay_t *info, int sample) {
	uint8_t *frame[4];
	int res = 1;
	int n = 0;
	frame[0] = primary_buffer[0]->Y;
	frame[1] = primary_buffer[0]->Cb;
	frame[2] = primary_buffer[0]->Cr;
	frame[3] = NULL;	
	if( available_diskspace() )
		res = vj_perform_render_sample_frame(info, frame, sample);

	if( res == 2)
	{
		int df = vj_event_get_video_format();
		long frames_left = sample_get_frames_left(sample) ;
		
		sample_stop_encoder( sample );
		
		n = vj_perform_record_commit_single( info );
		
		if(frames_left > 0 )
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Continue, %d frames left to record", frames_left);
			if( sample_init_encoder( sample, NULL,
				df, info->effect_frame1, info->current_edit_list, frames_left)==-1)
			{
				veejay_msg(VEEJAY_MSG_ERROR,
				"Error while auto splitting "); 
				report_bug();
			}
		}
		else
		{
			int len = sample_get_total_frames(sample);
	
			veejay_msg(VEEJAY_MSG_DEBUG, "Added new sample %d of %d frames",n,len);
			sample_reset_encoder( sample );
			sample_reset_autosplit( sample );
		}
	 }
	
	 if( res == 1)
	 {
		if( info->seq->active && info->seq->rec_id )
			info->seq->rec_id = 0;
		sample_stop_encoder(sample);
		vj_perform_record_commit_single( info );
		vj_perform_record_stop(info);
	 }

	 if( res == -1)
	 {
		if( info->seq->active && info->seq->rec_id )
			info->seq->rec_id = 0;

		sample_stop_encoder(sample);
		vj_perform_record_stop(info);
 	 }
}


void vj_perform_record_tag_frame(veejay_t *info) {
	video_playback_setup *settings = info->settings;
	uint8_t *frame[4];
	int res = 1;
	int stream_id = info->uc->sample_id;
	if( settings->offline_record )
	  stream_id = settings->offline_tag_id;

        if(settings->offline_record)
	{
		if( record_buffer->Y == NULL )
			vj_perform_record_buffer_init();

		frame[0] = record_buffer->Y;
		frame[1] = record_buffer->Cb;
		frame[2] = record_buffer->Cr;
		frame[3] = NULL;
	}
	else
	{
		frame[0] = primary_buffer[0]->Y;
		frame[1] = primary_buffer[0]->Cb;
		frame[2] = primary_buffer[0]->Cr;
		frame[3] = NULL;
	}

	if(available_diskspace())
		res = vj_perform_render_tag_frame(info, frame);

	if( res == 2)
	{
		int df = vj_event_get_video_format();
		long frames_left = vj_tag_get_frames_left(stream_id) ;
		vj_tag_stop_encoder( stream_id );
		int n = vj_perform_record_commit_single( info );
		vj_tag_reset_encoder( stream_id );
		if(frames_left > 0 )
		{
			if( vj_tag_init_encoder( stream_id, NULL,
				df, frames_left)==-1)
			{
				veejay_msg(VEEJAY_MSG_WARNING,
					"Error while auto splitting."); 
				report_bug();
			}
		}
		else
		{
			long len = vj_tag_get_total_frames(stream_id);
	
			veejay_msg(VEEJAY_MSG_DEBUG, "Added new sample %d of %ld frames",n,len);
			vj_tag_reset_encoder( stream_id );
			vj_tag_reset_autosplit( stream_id );
		}
	 }
	
	 if( res == 1)
	 {
		vj_tag_stop_encoder(stream_id);
		vj_perform_record_commit_single( info );	    
		vj_perform_record_stop(info);
	 }

	 if( res == -1)
	{
		vj_tag_stop_encoder(stream_id);
		vj_perform_record_stop(info);
 	}

}

static int vj_perform_tag_fill_buffer(veejay_t * info)
{
    int error = 1;
    uint8_t *frame[3];
    int type = pvar_.type;
    int active = pvar_.active;
    frame[0] = primary_buffer[0]->Y;
    frame[1] = primary_buffer[0]->Cb;
    frame[2] = primary_buffer[0]->Cr;

	if( info->settings->feedback && info->settings->feedback_stage > 1 ) {
		vj_perform_copy3( feedback_buffer, frame, info->effect_frame1->len, info->effect_frame1->uv_len );
		return 1;
	}
    
    if(!active)
    {
	if (type == VJ_TAG_TYPE_V4L || type == VJ_TAG_TYPE_NET || type == VJ_TAG_TYPE_MCAST || type == VJ_TAG_TYPE_PICTURE ) 
		vj_tag_enable( info->uc->sample_id );	
    }
    else
    {

	if (vj_tag_get_frame(info->uc->sample_id, frame, NULL))
	{
	    error = 0;
	    cached_tag_frames[0] = info->uc->sample_id;
   	}

   }         

	if (error == 1)
 	{
		VJFrame dumb;
		veejay_memcpy( &dumb, info->effect_frame1, sizeof(VJFrame));

		dumb.data[0] = frame[0];
		dumb.data[1] = frame[1];
		dumb.data[2] = frame[2];

		dummy_apply(&dumb,info->video_output_width,info->video_output_height,VJ_EFFECT_COLOR_BLACK );
  	}
 	 return 1;      	
}

static void vj_perform_pre_chain(veejay_t *info, VJFrame *frame)
{
	vj_perform_copy3( frame->data, temp_buffer, frame->len, frame->uv_len );
}

static	inline	void	vj_perform_supersample_chain( veejay_t *info, VJFrame *frame )
{
	chroma_supersample(
		info->settings->sample_mode,
		frame,
		temp_buffer );
	frame->ssm = 1;
}


void	vj_perform_follow_fade(int status) {
	pvar_.follow_fade = status;
}

static int vj_perform_post_chain_sample(veejay_t *info, VJFrame *frame)
{
	unsigned int opacity; 
	int mode   = pvar_.fader_active;
	int follow = 0;

	if( frame->ssm )
		vj_perform_supersample_chain( info, frame );
	
 	if(mode == 2 ) // manual fade
		opacity = (int) sample_get_fader_val(info->uc->sample_id);
	else	// fade in/fade out
	    	opacity = (int) sample_apply_fader_inc(info->uc->sample_id);

	if(mode != 2)
	{
	    	int dir =sample_get_fader_direction(info->uc->sample_id);

		if((dir<0) &&(opacity == 0))
		{
			sample_set_effect_status(info->uc->sample_id, 1);
     			sample_reset_fader(info->uc->sample_id);
			if( pvar_.follow_fade ) {
		 	  follow = 1;
			}

	      		veejay_msg(VEEJAY_MSG_DEBUG, "Sample Chain Auto Fade Out done");
		}
		if((dir>0) && (opacity==255))
		{
			sample_set_effect_status(info->uc->sample_id,0);
			sample_reset_fader(info->uc->sample_id);
			veejay_msg(VEEJAY_MSG_DEBUG, "Sample Chain Auto fade In done");
			if( pvar_.follow_fade ) {
		 	  follow = 1;
			}
		}
    	} else if(mode) {
		if( pvar_.follow_fade ) {
		 	  follow = 1;
		}
	}

	int len2 = ( frame->ssm == 1 ? helper_frame->len : helper_frame->uv_len );

	opacity_blend_apply( frame->data ,temp_buffer,frame->len, len2, opacity );

	if( follow ) {
		//@ find first to jump to
		int i,k;
		int tmp = 0;	
		for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++) {
			k = sample_get_chain_channel(info->uc->sample_id, i );
			tmp = sample_get_chain_source( info->uc->sample_id, i );
			if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k) )) {
				pvar_.follow_now[1] = tmp;	
				pvar_.follow_now[0] = k;
				break;
			}
		}
		
	}

	return follow;
}


static int vj_perform_post_chain_tag(veejay_t *info, VJFrame *frame)
{
	unsigned int opacity; 
	int mode = pvar_.fader_active;
	int follow = 0;

	if( !frame->ssm )
		vj_perform_supersample_chain( info, frame );

	if(mode == 2)
		opacity = (int) vj_tag_get_fader_val(info->uc->sample_id);
	else
     		opacity = (int) vj_tag_apply_fader_inc(info->uc->sample_id);

	if( opacity == 0 ) {
		if( pvar_.follow_fade ) {
		   follow = 1;
		}
	}

	if(mode != 2)
	{
 		int dir = vj_tag_get_fader_direction(info->uc->sample_id);
		if((dir < 0) && (opacity == 0))
		{
			vj_tag_set_effect_status(info->uc->sample_id,1);
			vj_tag_reset_fader(info->uc->sample_id);
			if( pvar_.follow_fade ) {
			   follow = 1;
			}

			veejay_msg(VEEJAY_MSG_DEBUG, "Stream Chain Auto Fade done");
		}
		if((dir > 0) && (opacity == 255))
		{
			vj_tag_set_effect_status(info->uc->sample_id,0);
			vj_tag_reset_fader(info->uc->sample_id);
			if( pvar_.follow_fade ) {
			   follow = 1;
			}
			veejay_msg(VEEJAY_MSG_DEBUG, "Stream Chain Auto Fade done");
		}
    	} else if(mode){
		if( pvar_.follow_fade ) {
		   follow = 1;
		}
	}

	int len2 = ( frame->ssm == 1 ? helper_frame->len : helper_frame->uv_len );
	opacity_blend_apply( frame->data ,temp_buffer,frame->len, len2, opacity );

	if( follow ) {
		//@ find first to jump to
		int i;
		int tmp=0,k;
		for( i = 0; i < SAMPLE_MAX_EFFECTS - 1; i ++ ) {
			k = vj_tag_get_chain_channel(info->uc->sample_id, i );
			tmp = vj_tag_get_chain_source( info->uc->sample_id, i );
			if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k)) ) {
				pvar_.follow_now[1] = tmp;	
				pvar_.follow_now[0] = k;
				break;
			}
		}
	}
	return follow;

}
#ifdef HAVE_JACK
static int play_audio_sample_ = 0;
#endif
int vj_perform_queue_audio_frame(veejay_t *info)
{
	if( info->audio == NO_AUDIO )
		return 1;
#ifdef HAVE_JACK
	editlist *el = info->current_edit_list;
	video_playback_setup *settings = info->settings;

	if( settings->audio_mute || !el->has_audio || settings->current_playback_speed == 0) {
		int num_samples = (info->edit_list->audio_rate / info->edit_list->video_fps);
		int bps = info->edit_list->audio_bps;
		veejay_memset( top_audio_buffer, 0, num_samples * bps);
		vj_perform_play_audio( settings, top_audio_buffer, (num_samples * bps ));
		return 1;
	}

	long this_frame = settings->current_frame_num;
	int num_samples =  (el->audio_rate/el->video_fps);
	int pred_len = num_samples;
	int bps		=   el->audio_bps;
	uint8_t *a_buf = top_audio_buffer;
	if (info->audio == AUDIO_PLAY)
  	{
		switch (info->uc->playback_mode)
		{
			case VJ_PLAYBACK_MODE_SAMPLE:
				if( el->has_audio )
					num_samples = vj_perform_fill_audio_buffers(info,a_buf,	audio_render_buffer + (2* PERFORM_AUDIO_SIZE * MAX_SPEED), &play_audio_sample_);
				if(num_samples <= 0 )
				{
					num_samples = pred_len;
					veejay_memset( a_buf, 0, num_samples * bps );
				}
				break;
			case VJ_PLAYBACK_MODE_PLAIN:
				if( el->has_audio )
				{
					int rs = 0;
					if (settings->current_frame_num <= settings->max_frame_num) 
						num_samples =	vj_el_get_audio_frame(el, this_frame,a_buf );
					else
						num_samples = 0;
					if( num_samples <= 0 )
					{
						num_samples = pred_len;
						veejay_memset( a_buf, 0, num_samples * bps );
					}
					else
						rs = 1;

					if( settings->current_playback_speed < 0 && rs )
						vj_perform_reverse_audio_frame(info,num_samples,a_buf);
				}
	    	    		break;
			case VJ_PLAYBACK_MODE_TAG:
				if(el->has_audio)
				{
				    	num_samples = vj_tag_get_audio_frame(info->uc->sample_id, a_buf);
					if(num_samples <= 0 )
					{
						num_samples = pred_len;
						veejay_memset(a_buf, 0, num_samples * bps );
					}
				}
				break;
		}
		vj_jack_continue( settings->current_playback_speed );
		
		vj_perform_play_audio( settings, a_buf, (num_samples * bps ));

     }	
#endif
   	return 1;
}

int	vj_perform_get_width( veejay_t *info )
{
#ifdef HAVE_GL
	if(info->video_out == 3 )
		return  x_display_width(info->gl);
#endif
#ifdef HAVE_SDL
	if(info->video_out <= 1 )
		return vj_sdl_screen_w(info->sdl[0]);
#endif
	return info->video_output_width;
}

int	vj_perform_get_height( veejay_t *info )
{
#ifdef HAVE_GL
	if(info->video_out == 3 )
		return x_display_height(info->gl);
#endif
#ifdef HAVE_SDL
	if(info->video_out <= 1 )
		return vj_sdl_screen_h(info->sdl[0]);
#endif
	return info->video_output_height;
}

static	char	*vj_perform_print_credits( veejay_t *info ) 
{
	char text[1024];
	snprintf(text, 1024,"This is Veejay version %s\n%s\n%s\n%s\n%s",VERSION,intro,copyr,license,donateaddr);
	return vj_strdup(text);
}

static	char	*vj_perform_osd_status( veejay_t *info )
{
	video_playback_setup *settings = info->settings;
	char *more = NULL;
	if(info->composite ) {
		void *vp = composite_get_vp( info->composite );
		if(viewport_get_mode(vp)==1 ) {
			more = viewport_get_my_status( vp );
		}
	}
	MPEG_timecode_t tc;
	char timecode[64];
	char tmp[64];
	char buf[256];
	veejay_memset(&tc,0,sizeof(MPEG_timecode_t));
        y4m_ratio_t ratio = mpeg_conform_framerate( (double)info->current_edit_list->video_fps );
        int n = mpeg_framerate_code( ratio );

        mpeg_timecode(&tc, settings->current_frame_num, n, info->current_edit_list->video_fps );

        snprintf(timecode, sizeof(timecode), "%2d:%2.2d:%2.2d:%2.2d",
                tc.h, tc.m, tc.s, tc.f );

	char *extra = NULL;
	if( info->composite ) {
		int status = -1;
		if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ) {
			status = sample_get_composite( info->uc->sample_id );
		} else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
			status = vj_tag_get_composite( info->uc->sample_id );
		}
		if( status == 0 ) {
			snprintf(tmp,sizeof(tmp), "Proj");
			extra = vj_strdup(tmp);
		}
		else if(status == 1 ) {
			snprintf(tmp,sizeof(tmp), "Cam");
			extra = vj_strdup(tmp);
		} 
	}
	
	switch(info->uc->playback_mode) {
		case VJ_PLAYBACK_MODE_SAMPLE:
			snprintf(buf,256, "%s %d of %d Cache=%dMb Cost=%dms",
					timecode,
					info->uc->sample_id,
					sample_size()-1,
					sample_cache_used(0),
					info->real_fps );
			break;
		case VJ_PLAYBACK_MODE_TAG:
			snprintf(buf,256, "%s %d of %d Cost=%dms",
					timecode,
					info->uc->sample_id,
					vj_tag_size(),
					info->real_fps );
			break;
		default:	
			snprintf(buf,256, "(P) %s", timecode );
			break;
	}

	int total_len = strlen(buf) + ( extra == NULL ? 0 : strlen(extra)) + 1;
	if( more )
		total_len += strlen(more);
	char *status_str = (char*) malloc(sizeof(char) * total_len);
	if( extra && more )
		snprintf(status_str,total_len,"%s %s %s",more, buf,extra);
	else if ( more ) 
		snprintf(status_str, total_len, "%s %s", more,buf );
	else
		strncpy( status_str, buf, total_len );

	if(extra)
		free(extra);
	if(more)
		free(more);

	return status_str;
}
	

static	void	vj_perform_render_osd( veejay_t *info, video_playback_setup *settings, int destination )
{
	if(info->use_osd <= 0 ) // || info->settings->composite )
		return;

	VJFrame *frame = info->effect_frame1;
	frame->data[0] = primary_buffer[destination]->Y;
	frame->data[1] = primary_buffer[destination]->Cb;
	frame->data[2] = primary_buffer[destination]->Cr;

	if( !frame->ssm)
	{
		chroma_supersample(
			settings->sample_mode,
			frame,
			frame->data );
		frame->ssm = 1;
	}
	
	char *osd_text = NULL;
	int placement = 0;
	if( info->use_osd == 2 ) {
		placement = 1;
		osd_text = vj_perform_print_credits(info);
	} else if (info->use_osd == 1 ) {
		osd_text = vj_perform_osd_status(info);
	} else if (info->use_osd == 3 && info->composite ) {
		placement = 1;
		osd_text = viewport_get_my_help( composite_get_vp(info->composite ) );
	}
	if(osd_text) {
		vj_font_render_osd_status(info->osd,frame,osd_text,placement);
		free(osd_text);
	}
}

static	void 	vj_perform_finish_chain( veejay_t *info )
{
	VJFrame *frame = info->effect_frame1;
    	frame->data[0] = primary_buffer[0]->Y;
   	frame->data[1] = primary_buffer[0]->Cb;
    	frame->data[2] = primary_buffer[0]->Cr;

	int result = 0;

	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
	{
		if( pvar_.fader_active )
			result = vj_perform_post_chain_tag(info,frame);
	}
	else if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
	{
		if( pvar_.fader_active)
			result = vj_perform_post_chain_sample(info,frame);
	}

	if( result ) {
		pvar_.follow_run = result;
	}
}

static	void	vj_perform_finish_render( veejay_t *info, video_playback_setup *settings, int destination )
{
	VJFrame *frame = info->effect_frame1;
	VJFrame *frame2= info->effect_frame2;
	uint8_t *pri[4];
	char *osd_text = NULL;
	char *more_text = NULL;
	int   placement= 0;

	pri[0] = primary_buffer[destination]->Y;
	pri[1] = primary_buffer[destination]->Cb;
	pri[2] = primary_buffer[destination]->Cr;
	pri[3] = NULL;
	if( settings->composite  )
	{ //@ scales in software
		if( settings->ca ) {
			settings->ca = 0;
		}

		//@ focus on projection screen
		if(composite_event( info->composite, pri, info->uc->mouse[0],info->uc->mouse[1],info->uc->mouse[2],	
			vj_perform_get_width(info), vj_perform_get_height(info)) ) {
			//@ save config to playing sample/stream
			if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
				void *cur = vj_tag_get_composite_view(info->uc->sample_id);
				if(cur == NULL )
				{
					cur = composite_clone(info->composite);
					vj_tag_set_composite_view(info->uc->sample_id, cur );
				}
				composite_set_backing(info->composite,cur);
				vj_tag_reload_config( info->composite,info->uc->sample_id, 1);

			} else if ( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
				void *cur = sample_get_composite_view(info->uc->sample_id);
				if(cur==NULL) {
					cur = composite_clone(info->composite);
					sample_set_composite_view(info->uc->sample_id,cur);
				}
				composite_set_backing(info->composite,cur);
				sample_reload_config( info->composite,info->uc->sample_id, 1 );
			}
#ifdef HAVE_SDL
			if( info->video_out == 0 ) {
				//@ release focus
				vj_sdl_grab( info->sdl[0], 0 );
			}
#endif
		}

#ifdef HAVE_SDL
		if( info->use_osd == 2 ) {
			osd_text = vj_perform_print_credits(info);	
			placement= 1;
		} else if (info->use_osd == 1 ) {
			osd_text = vj_perform_osd_status(info);
			placement= 0;
		} else if (info->use_osd == 3 && info->composite ) {
			placement = 1;
			osd_text = viewport_get_my_help( composite_get_vp(info->composite ) );
			more_text = vj_perform_osd_status(info);
		}
#endif
	}

	if( settings->composite  ) {
		VJFrame out;
		veejay_memcpy( &out, info->effect_frame1, sizeof(VJFrame));
		if( out.ssm ) 
			out.format = (info->pixel_format == FMT_422F ? PIX_FMT_YUVJ444P : PIX_FMT_YUV444P );
			
		if(!frame->ssm) {
          	  chroma_supersample(settings->sample_mode,frame,pri );
          	  frame->ssm = 1;
        	}

		composite_process(info->composite,&out,info->effect_frame1,settings->composite,frame->format); 
		// draw font/qr in transformed image
		if(osd_text ) {
			VJFrame *tst = composite_get_draw_buffer( info->composite );
			if(tst) {
			        if( info->use_osd == 3 ) {	
					vj_font_render_osd_status(info->osd,tst,osd_text,placement);
					if(more_text)
						vj_font_render_osd_status(info->osd,tst,more_text,0);
				}
				free(tst);
			}
		} 
	} 
	else {
		if( osd_text ) 
			vj_font_render_osd_status(info->osd,frame,osd_text,placement);
		if(more_text)
			vj_font_render_osd_status(info->osd,frame,more_text,0);
		if(info->use_osd == 2 ){
			/* draw qr picture if present */
			qrwrap_draw( frame, info->uc->port, info->homedir, frame->height/4,frame->height/4, frame->format );
			qrbitcoin_draw( frame, info->homedir, frame->height/4,frame->height/4, frame->format );
		}
	}
	if( osd_text)
		free(osd_text);
	if( more_text)	
		free(more_text);


	if (info->uc->take_bg==1)
    	{
        	info->uc->take_bg = vj_perform_take_bg(info,frame,0);
    	} 

	if( frame->ssm == 1 )
	{
		chroma_subsample(settings->sample_mode,frame,pri);
		frame->ssm = 0;
	}

	if( frame2->ssm == 1 )
	{
		frame2->ssm=0;
	}

	if (info->uc->take_bg==1)
    	{
        	vj_perform_take_bg(info,frame,1);
        	info->uc->take_bg = 0;
    	} 
	
	if( frame2->data[0] == frame->data[0] )
		frame->ssm = 0;
}

static	void	vj_perform_render_font( veejay_t *info, video_playback_setup *settings )
{
	VJFrame	*frame = info->effect_frame1;

    	frame->data[0] = primary_buffer[0]->Y;
   	frame->data[1] = primary_buffer[0]->Cb;
    	frame->data[2] = primary_buffer[0]->Cr;

#ifdef HAVE_FREETYPE
	int n = vj_font_norender( info->font, settings->current_frame_num );
	if( n > 0 )
	{
		if( !frame->ssm )
		{
			chroma_supersample(
				settings->sample_mode,
				frame,
				frame->data );
			frame->ssm = 1;
		}
	
		vj_font_render( info->font, frame , settings->current_frame_num );
	}
#endif
}

static	void	vj_perform_record_frame( veejay_t *info )
{
	VJFrame	*frame = info->effect_frame1;
	int which_sample = info->uc->sample_id;
	if( info->seq->active && info->seq->rec_id && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE  )		
		which_sample = info->seq->rec_id;

	if(frame->ssm == 1)
	{ 
		video_playback_setup *settings = info->settings;
		uint8_t *dst[3] = { NULL, primary_buffer[3]->Cb,
					  primary_buffer[3]->Cr };
		chroma_subsample_cp( settings->sample_mode, frame,  frame->data,dst );
		uint8_t *chroma[2] = { primary_buffer[0]->Cb, primary_buffer[0]->Cr };
		primary_buffer[0]->Cb = dst[1];
		primary_buffer[0]->Cr = dst[2];

		if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
			vj_perform_record_tag_frame(info);
		else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
			vj_perform_record_sample_frame(info, which_sample );

		primary_buffer[0]->Cb = chroma[0];
		primary_buffer[0]->Cr = chroma[1];
	}
	else
	{
		if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
			vj_perform_record_tag_frame(info);
		else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
			vj_perform_record_sample_frame(info, which_sample );
	}
}

static	int	vj_perform_render_magic( veejay_t *info, video_playback_setup *settings )
{
	int deep = 0;
	vj_perform_finish_chain( info );

	vj_perform_render_font( info, settings);
	//@ record frame 
//	if( pvar_.enc_active )
//		vj_perform_record_frame(info);

	if(!info->settings->composite)
		vj_perform_render_osd( info, settings, deep );

	//@ Do the subsampling 
	vj_perform_finish_render( info, settings,deep );

	return deep;
}

void	vj_perform_record_video_frame(veejay_t *info)
{
	//@ record frame 
	if( pvar_.enc_active )
		vj_perform_record_frame(info);
}

int vj_perform_queue_video_frame(veejay_t *info, const int skip_incr)
{
	video_playback_setup *settings = info->settings;
	if(skip_incr)
		return 1;

	int is444 = 0;
	int res = 0;
	int i = 0;
	int safe_ff = pvar_.follow_fade;
	veejay_memset( &pvar_, 0, sizeof(varcache_t));
	pvar_.follow_fade = safe_ff;

	if(settings->offline_record)	
		vj_perform_record_tag_frame(info);

	int cur_out = info->out_buf;

	info->effect_frame1->ssm = 0;
	info->effect_frame2->ssm = 0;

	for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
		frame_buffer[i]->ssm = -1;
	}

	switch (info->uc->playback_mode)
	{
		case VJ_PLAYBACK_MODE_SAMPLE:

			sample_var( info->uc->sample_id, &(pvar_.type),
					    &(pvar_.fader_active),
					    &(pvar_.fx_status),
					    &(pvar_.enc_active),
					    &(pvar_.active)
				  );

			if( info->seq->active && info->seq->rec_id )
				pvar_.enc_active = 1;
			
			vj_perform_plain_fill_buffer(info);
				 
			cached_sample_frames[0] = info->uc->sample_id;

			if(vj_perform_verify_rows(info))
				vj_perform_sample_complete_buffers(info, &is444);

			cur_out = vj_perform_render_magic( info, info->settings );
		   	res = 1;

     			 break;
		    
		case VJ_PLAYBACK_MODE_PLAIN:

			   vj_perform_plain_fill_buffer(info);

			   cur_out = vj_perform_render_magic( info, info->settings );
			   res = 1;
 		    break;
		case VJ_PLAYBACK_MODE_TAG:

			vj_tag_var( info->uc->sample_id,
					    &(pvar_.type),
					    &(pvar_.fader_active),
					    &(pvar_.fx_status),
					    &(pvar_.enc_active),
					    &(pvar_.active)
					    
					    );

			 if (vj_perform_tag_fill_buffer(info))
			 {
				if(vj_perform_verify_rows(info))
					vj_perform_tag_complete_buffers(info, &is444);
				
				cur_out = vj_perform_render_magic( info, info->settings );
			 }
			 res = 1;	 
		   	 break;
		default:
			return 0;
	}

	info->out_buf = cur_out;
	
	if( info->settings->feedback && info->settings->feedback_stage > 0 ) 
	{
		int idx = info->out_buf;
		vj_perform_copy2( primary_buffer[idx], feedback_buffer, info->effect_frame1->len, info->effect_frame1->uv_len );

		info->settings->feedback_stage = 2;
	}

	return res;
}


int vj_perform_queue_frame(veejay_t * info, int skip )
{
	video_playback_setup *settings = (video_playback_setup*) info->settings;

	if( pvar_.follow_run ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Following to [%d,%d]", pvar_.follow_now[0], pvar_.follow_now[1] );

		if( pvar_.follow_now[1] == 0 ) {
			info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
			veejay_set_sample( info, pvar_.follow_now[0] );
		} else {
			info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
			veejay_set_sample( info, pvar_.follow_now[0] );
		}

		pvar_.follow_run    = 0;
		pvar_.follow_now[0] = 0;
		pvar_.follow_now[1] = 0;
	}

	if(!skip)
	{
		int speed = settings->current_playback_speed;
		if( settings->hold_status == 1 ) {
			speed = 0;
			if(settings->hold_pos == 0 ) {
				settings->hold_status = 0;
				long was_at_pos = settings->current_frame_num;
				veejay_increase_frame(info, settings->hold_resume );
				veejay_msg(VEEJAY_MSG_DEBUG, "Reached hold position, skip from frame %ld to frame %ld",
						was_at_pos,
						settings->current_frame_num );

				if( speed == 0 )
					speed = settings->current_playback_speed;
			}
			settings->hold_pos --;
		}

		switch(info->uc->playback_mode) 
		{
			case VJ_PLAYBACK_MODE_TAG:
				vj_perform_increase_tag_frame(info, speed);
				break;
			case VJ_PLAYBACK_MODE_SAMPLE: 
	 			vj_perform_increase_sample_frame(info, speed);
	  			break;
			case VJ_PLAYBACK_MODE_PLAIN:
				vj_perform_increase_plain_frame(info, speed);
				break;
			default:
				break;
		}

		//@ increase tick
	  	vj_perform_clear_cache();
	
		settings->cycle_count[0] ++;
		if( settings->cycle_count[0] == 0 )
			settings->cycle_count[1] ++;
	}

	return 0;
}


static int track_dup = 0;
void	vj_perform_randomize(veejay_t *info)
{
	video_playback_setup *settings = info->settings;
	if(settings->randplayer.mode == RANDMODE_INACTIVE)
		return;

	double n_sample = (double) (sample_size()-1);

	if( settings->randplayer.mode == RANDMODE_SAMPLE )
	track_dup = info->uc->sample_id;

	if( n_sample == 1.0 )
		track_dup = 0;

	int take_n   = 1 + (int) (n_sample * rand() / (RAND_MAX+1.0));
	int min_delay = 1;
	int max_delay = 0;
	char timecode[15];
	int use = ( take_n == track_dup ? 0: 1 );

	while(!sample_exists(take_n)  || !use)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, 
		 "Sample to play (at random) %d does not exist",
			take_n);
		take_n = 1 + (int) ( n_sample * rand()/(RAND_MAX+1.0));
		use = (track_dup == take_n ? 0:1 );
	}

	int start,end;
	start = sample_get_startFrame( take_n);
	end   = sample_get_endFrame( take_n );
	
	if( settings->randplayer.timer == RANDTIMER_FRAME )
	{
		max_delay = (end-start) + 1;
		max_delay = min_delay + (int) ((double)max_delay * rand() / (RAND_MAX+1.0));
	}
	else
	{
		max_delay = (end-start);	
	}

	settings->randplayer.max_delay = max_delay;
	settings->randplayer.min_delay = min_delay;	

	MPEG_timecode_t tc;
	y4m_ratio_t ratio = mpeg_conform_framerate( (double)info->current_edit_list->video_fps );
	mpeg_timecode(&tc,max_delay,mpeg_framerate_code(ratio),info->current_edit_list->video_fps );
	sprintf(timecode, "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f);
	veejay_msg(VEEJAY_MSG_DEBUG, "Sample randomizer trigger in %s",	timecode );

	veejay_set_sample( info, take_n );
}

int	vj_perform_rand_update(veejay_t *info)
{
	video_playback_setup *settings = info->settings;
	if(settings->randplayer.mode == RANDMODE_INACTIVE)
		return 0;
	if(settings->randplayer.mode == RANDMODE_SAMPLE)
	{
		settings->randplayer.max_delay --;
		if(settings->randplayer.max_delay <= 0 )
			vj_perform_randomize(info);
		return 1;
	}
	return 0;	
}

