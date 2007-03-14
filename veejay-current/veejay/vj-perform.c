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
#include <mjpegtools/mpegconsts.h>
#include <mjpegtools/mpegtimecode.h>
#include <liblavjpeg/jpegutils.h>
#include <libyuv/yuvconv.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vj-perform.h>
#include <veejay/libveejay.h>
#include <libsamplerec/samplerecord.h>
#include <libel/pixbuf.h>
#include <veejay/vj-misc.h>
#include <jpeglib.h>
#include <liblzo/lzo.h>
#include <veejay/vj-viewport.h>
#include <veejay/vj-composite.h>
#ifdef HAVE_SAMPLERATE
#include <samplerate.h>
#endif
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
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#define PERFORM_AUDIO_SIZE 16384
static int simple_frame_duplicator =0;

static int performer_framelen = 0;

typedef struct {
    uint8_t *Y;
    uint8_t *Cb;
    uint8_t *Cr;
    uint8_t *alpha;
    int	     ssm;
    char     padding[12];
} ycbcr_frame;

typedef struct {
	int	fader_active;
	int	fx_status;
	int	enc_active;
	int	type;
	int	active;
	char	padding[12];
} varcache_t;

#define	RUP8(num)(((num)+8)&~8)

static	varcache_t	pvar_;
static	void		*lzo_;
static void 	*effect_sampler = NULL;
static void	*crop_sampler = NULL;
static VJFrame *crop_frame = NULL;
static ycbcr_frame **video_output_buffer = NULL; /* scaled video output */
static int	video_output_buffer_convert = 0;
static ycbcr_frame **frame_buffer;	/* chain */
static ycbcr_frame **primary_buffer;	/* normal */
#define CACHE_TOP 0
#define CACHE 1
#define CACHE_SIZE (SAMPLE_MAX_EFFECTS+CACHE)
static int cached_tag_frames[CACHE_SIZE];	/* cache a frame into the buffer only once */
static int cached_sample_frames[CACHE_SIZE];

static int frame_info[64][SAMPLE_MAX_EFFECTS];	/* array holding frame lengths  */
static uint8_t *audio_buffer[SAMPLE_MAX_EFFECTS];	/* the audio buffer */
static uint8_t *lin_audio_buffer_ = NULL;
static uint8_t *top_audio_buffer = NULL;
static uint8_t *resample_audio_buffer = NULL;
static uint8_t *audio_render_buffer = NULL;
static uint8_t *down_sample_buffer = NULL;
static uint8_t *temp_buffer[4];
static uint8_t *socket_buffer = NULL;
static ycbcr_frame *record_buffer = NULL;	// needed for recording invisible streams
static VJFrame *helper_frame = NULL;
static int vj_perform_record_buffer_init();
static void vj_perform_record_buffer_free();
#ifdef HAVE_JACK
static int jack_rate_ = 0;
static ReSampleContext *resample_context[(MAX_SPEED+1)];
static ReSampleContext *downsample_context[(MAX_SPEED+1)];
static ReSampleContext *resample_jack = NULL;
#endif

#define MLIMIT(var, low, high) \
if((var) < (low)) { var = (low); } \
if((var) > (high)) { var = (high); }

//forward

static void vj_perform_pre_chain(veejay_t *info, VJFrame *frame);
static void vj_perform_post_chain_sample(veejay_t *info, VJFrame *frame);
static void vj_perform_post_chain_tag(veejay_t *info, VJFrame *frame);
static void seq_play_sample( veejay_t *info, int n);
static void vj_perform_plain_fill_buffer(veejay_t * info, int entry);
static int vj_perform_tag_fill_buffer(veejay_t * info, int entry);
static void vj_perform_clear_cache(void);
static int vj_perform_increase_tag_frame(veejay_t * info, long num);
static int vj_perform_increase_plain_frame(veejay_t * info, long num);
static int vj_perform_apply_secundary_tag(veejay_t * info, int sample_id,int type, int chain_entry, int entry);
static int vj_perform_apply_secundary(veejay_t * info, int sample_id,int type, int chain_entry, int entry);
static int vj_perform_tag_complete_buffers(veejay_t * info, int entry,int *h);
static int vj_perform_increase_sample_frame(veejay_t * info, long num);
static int vj_perform_sample_complete_buffers(veejay_t * info, int entry, int *h);
static void vj_perform_use_cached_ycbcr_frame(veejay_t *info, int centry, int chain_entry, int width, int height);
static int vj_perform_apply_first(veejay_t *info, vjp_kf *todo_info, VJFrame **frames, VJFrameInfo *frameinfo, int e, int c, int n_frames );
static int vj_perform_render_sample_frame(veejay_t *info, uint8_t *frame[4]);
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4]);
static int vj_perform_record_commit_single(veejay_t *info, int entry);
static int vj_perform_get_subtagframe(veejay_t * info, int sub_sample, int chain_entry );
static int vj_perform_get_subframe(veejay_t * info, int sub_sample,int chain_entyr );
static int vj_perform_get_subframe_tag(veejay_t * info, int sub_sample, int chain_entry );
static void vj_perform_reverse_audio_frame(veejay_t * info, int len, uint8_t *buf );

static int vj_perform_tag_is_cached(int chain_entry, int tag_id)
{
 	int c;
	int res = -1;
	for(c=CACHE_SIZE; c >= 0 ; c--)
  	{
	    if(chain_entry != c && cached_tag_frames[c] == tag_id) 
	    {
		res = c;
		break;	
	    }
  	}

	return res;
}
static int vj_perform_sample_is_cached(int nframe, int chain_entry)
{
	int c;
	int res = -1;
	for(c=CACHE_SIZE; c >= 0 ; c--)
  	{
	    	if(chain_entry != c && cached_sample_frames[c] == nframe) 
		{
			res = c;
			break;
		}
  	}
	return res;
}

/**********************************************************************
 * return the chain entry where a cached frame is located or -1 if none
 */

void vj_perform_clear_frame_info(int entry)
{
    int c = 0;
    for (c = 0; c < SAMPLE_MAX_EFFECTS; c++) {
	frame_info[0][c] = 0;
    }
}

/**********************************************************************
 * clear the cache contents pre queuing frames
 */

static void vj_perform_clear_cache()
{
    veejay_memset(cached_tag_frames, 0 , sizeof(cached_tag_frames));
    veejay_memset(cached_sample_frames, 0, sizeof(cached_sample_frames));
}

/********************************************************************
 * int vj_perform_increase_tag_frame(...)
 *
 * get ready for next frame, check boundaries 
 * actually fakes the queuing mechanism, it never touches the disk.
 * returns 0 on sucess, -1 on error
 */
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
static int vj_perform_increase_plain_frame(veejay_t * info, long num)
{
    video_playback_setup *settings = info->settings;

    simple_frame_duplicator++;

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
		veejay_msg(VEEJAY_MSG_DEBUG, "Reached end of video - Ending veejay session ... ");
		veejay_change_state(info, LAVPLAY_STATE_STOP);
	}
	settings->current_frame_num = settings->max_frame_num;
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
	int cycle = 0;
	if(! info->seq->active )
		return 0;
	
	if( sample_get_loop_dec( info->uc->sample_id ) >= 1 )
	{
		int n = vj_perform_valid_sequence( info );
		if( n >= 0 )
		{
			sample_set_loop_dec( info->uc->sample_id, 0 ); //reset loop
		
			veejay_msg(0, "Sequence play selects sample %d", info->seq->samples[info->seq->current]);
		
		//	veejay_change_playback_mode( info, VJ_PLAYBACK_MODE_SAMPLE, info->seq->current );

		/*	if( n != info->uc->sample_id )
			{
				int tmp = sample_chain_free( info->uc->sample_id );
				tmp = sample_chain_malloc( n );
			}

				*/		
		//	veejay_change_playback_mode( info, VJ_PLAYBACK_MODE_SAMPLE, info->seq->current );
			seq_play_sample(info, n );

	/*		veejay_set_sample( info, info->seq->samples[ info->seq->current ] );*/
			return 1;
		}
		return 0;
	}		

	return 0;
}

static	void	seq_play_sample( veejay_t *info, int n)
{

	info->seq->current = n;

	int id = info->seq->samples[n];

	if(id)
	{
		sample_chain_free( info->uc->sample_id );
		sample_chain_malloc( id );
	}

	veejay_set_sample( info, info->seq->samples[ info->seq->current ] );


}

/********************************************************************
 * int vj_perform_increase_sample_frame(...)
 *
 * get read for next frame, check boundaries and loop type
 *
 * returns 0 on sucess, -1 on error
 */
static int vj_perform_increase_sample_frame(veejay_t * info, long num)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    int start,end,looptype,speed;
    int ret_val = 1;

    if(num == 0 ) return 1;

    if(sample_get_short_info(info->uc->sample_id,&start,&end,&looptype,&speed)!=0) return -1;

    settings->current_playback_speed = speed;

    int cur_sfd = sample_get_framedups( info->uc->sample_id );
    int max_sfd = sample_get_framedup( info->uc->sample_id );

	cur_sfd ++;

	if( cur_sfd >= max_sfd )
	{
		settings->current_frame_num += num;
		cur_sfd = 0;
	}

	sample_set_framedups( info->uc->sample_id , cur_sfd);


    if (speed >= 0) {		/* forward play */

	if(looptype==3)
	{
		int range = end - start;
		int num   = start + ((int) ( (double)range * rand()/(RAND_MAX)));
		settings->current_frame_num = num;
	}

	if (settings->current_frame_num > end || settings->current_frame_num < start) {
	    switch (looptype) {
		    case 2:
			info->uc->direction = -1;
			if(!vj_perform_try_sequence( info ) )
			{
				sample_apply_loop_dec( info->uc->sample_id, info->edit_list->video_fps);
				veejay_set_frame(info, end);
				veejay_set_speed(info, (-1 * speed));
			}
			break;
		    case 1:
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
			veejay_set_frame(info, start);
			break;
		    default:
			veejay_set_frame(info, end);
			veejay_set_speed(info, 0);
	    }
	}
    } else {			/* reverse play */
	if( looptype == 3 )
	{
		int range = end - start;
		int num   = end - ((int) ( (double)range*rand()/(RAND_MAX)));
		settings->current_frame_num = num;
	}

	if (settings->current_frame_num < start || settings->current_frame_num > end || settings->current_frame_num < 0) {
	    switch (looptype) {
	    case 2:
		info->uc->direction = 1;
		if(!vj_perform_try_sequence(info) )
		{
		  	sample_apply_loop_dec( info->uc->sample_id, info->edit_list->video_fps);
			veejay_set_frame(info, start);
			veejay_set_speed(info, (-1 * speed));
		}
		break;
	    case 1:
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
		veejay_set_frame(info,end);
		break;
	    default:
		veejay_set_frame(info, start);
		veejay_set_speed(info, 0);
	    }
	}
    }
    sample_update_offset( info->uc->sample_id, settings->current_frame_num );	
    vj_perform_rand_update( info );

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
	uint8_t *buf = vj_malloc(sizeof(uint8_t) * RUP8(helper_frame->len * 3));
#ifdef STRICT_CHECKING
	assert ( buf != NULL );
#endif
	if(!buf)
		return 0;

	frame_buffer[c]->Y = buf;
	frame_buffer[c]->Cb = frame_buffer[c]->Y + RUP8(frame_len);
	frame_buffer[c]->Cr = frame_buffer[c]->Cb + RUP8(frame_len);

	frame_buffer[c]->ssm = info->effect_frame1->ssm;

	return 1;
}

static void vj_perform_free_row(int c)
{
	if(frame_buffer[c]->Y)
		free( frame_buffer[c]->Y );
	frame_buffer[c]->Y = NULL;
	frame_buffer[c]->Cb = NULL;
	frame_buffer[c]->Cr = NULL;
	frame_buffer[c]->ssm = 0;
	cached_sample_frames[c+1] = 0;
	cached_tag_frames[c+1] = 0;
}

#define vj_perform_row_used(c) ( frame_buffer[c]->Y == NULL ? 0 : 1 )
/*
static int	vj_perform_row_used(int frame, int c)
{
	if(frame_buffer[c]->Y != NULL ) return 1;
	return 0;
}*/


static int	vj_perform_verify_rows(veejay_t *info, int frame)
{
	if( pvar_.fx_status == 0 )
		return 0;

	int c,v,has_rows = 0;
	const int w = info->edit_list->video_width;
	const int h = info->edit_list->video_height;

	for(c=0; c < SAMPLE_MAX_EFFECTS; c++)
	{
	  	v = (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ? 
			sample_get_effect_any(info->uc->sample_id,c) : vj_tag_get_effect_any(info->uc->sample_id,c));
		if( v > 0)
	  	{
			int t=0,s=0,changed=0;
			if(!vj_perform_row_used(c))
			{
				if ( !vj_perform_alloc_row( info, frame, c, w*h)  )
				{
					veejay_msg(VEEJAY_MSG_ERROR, "Unable to allocate memory for FX entry %d",c);
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
	if(record_buffer)
		free(record_buffer);
}
static	char ppm_path[1024];

int vj_perform_init(veejay_t * info, int use_vp)
{
    const int w = info->edit_list->video_width;
    const int h = info->edit_list->video_height;
    const int frame_len = ((w * h)/7) * 8;
    int c;
    // buffer used to store encoded frames (for plain and sample mode)
    performer_framelen = frame_len *2;
    frame_buffer = (ycbcr_frame **) vj_malloc(sizeof(ycbcr_frame*) * SAMPLE_MAX_EFFECTS);
    if(!frame_buffer) return 0;

    record_buffer = (ycbcr_frame*) vj_malloc(sizeof(ycbcr_frame) );
    if(!record_buffer) return 0;
    record_buffer->Y = NULL;
    record_buffer->Cb = NULL;
    record_buffer->Cr = NULL;
  
	/*
 *
 *	Allocate primary buffers for veejay
 *
 */


    primary_buffer =
	(ycbcr_frame **) vj_malloc(sizeof(ycbcr_frame **) * 8); 
    if(!primary_buffer) return 0;
	for( c = 0; c < 6; c ++ )
	{
		primary_buffer[c] = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
		primary_buffer[c]->Y = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8((frame_len+w) * 3) );
#ifdef STRICT_CHECKING
		assert( primary_buffer[c] != NULL );
		assert( primary_buffer[c]->Y != NULL );
#endif
		primary_buffer[c]->Cb = primary_buffer[c]->Y + (frame_len + w);
		primary_buffer[c]->Cr = primary_buffer[c]->Cb + (frame_len +w);

		veejay_memset( primary_buffer[c]->Y,   0, (frame_len+w));
		veejay_memset( primary_buffer[c]->Cb,128, (frame_len+w));
		veejay_memset( primary_buffer[c]->Cr,128, (frame_len+w));
	}


	primary_buffer[6] = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
	primary_buffer[6]->Y = (uint8_t*) vj_calloc( sizeof(uint8_t) * RUP8(512 * 512 * 3));
	primary_buffer[7] = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
	primary_buffer[7]->Y = (uint8_t*) vj_calloc( sizeof(uint8_t) * RUP8(w * h * 3));
	primary_buffer[7]->Cb = primary_buffer[7]->Y + ( RUP8(w*h));
	primary_buffer[7]->Cr = primary_buffer[7]->Cb + ( RUP8(w*h));


    video_output_buffer_convert = 0;
    video_output_buffer =
	(ycbcr_frame**) vj_malloc(sizeof(ycbcr_frame*) * 2 );
    if(!video_output_buffer)
	return 0;

    veejay_memset( ppm_path,0,sizeof(ppm_path));

    for(c=0; c < 2; c ++ )
	{
	    video_output_buffer[c] = (ycbcr_frame*) vj_malloc(sizeof(ycbcr_frame));
	    video_output_buffer[c]->Y = NULL;
	    video_output_buffer[c]->Cb = NULL;
	    video_output_buffer[c]->Cr = NULL; 
	}

    sample_record_init(frame_len);
    vj_tag_record_init(w,h);
    // to render fading of effect chain:
    temp_buffer[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(frame_len) );
    if(!temp_buffer[0]) return 0;
	veejay_memset( temp_buffer[0], 16, frame_len );
    temp_buffer[1] = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(frame_len) );
    if(!temp_buffer[1]) return 0;
	veejay_memset( temp_buffer[1], 128, frame_len );
    temp_buffer[2] = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(frame_len) );
    if(!temp_buffer[2]) return 0;
	veejay_memset( temp_buffer[2], 128, frame_len );
    // to render fading of effect chain:
    socket_buffer = (uint8_t*)vj_malloc(sizeof(uint8_t) * frame_len * 4 ); // large enough !!
    veejay_memset( socket_buffer, 16, frame_len * 4 );
    // to render fading of effect chain:

    /* allocate space for frame_buffer, the place we render effects  in */
    for (c = 0; c < SAMPLE_MAX_EFFECTS; c++) {
	frame_buffer[c] = (ycbcr_frame *) vj_malloc(sizeof(ycbcr_frame));
        if(!frame_buffer[c]) return 0;
	veejay_memset( frame_buffer[c],0, sizeof(ycbcr_frame));
    }
    // clear the cache information
	vj_perform_clear_cache();
	veejay_memset( frame_info[0],0,SAMPLE_MAX_EFFECTS);

	    helper_frame = (VJFrame*) vj_malloc(sizeof(VJFrame));
	    veejay_memcpy(helper_frame, info->effect_frame1, sizeof(VJFrame));
 
    vj_perform_record_buffer_init();
//#ifdef HAVE_FREETYPE
//	font_frame = vj_perform_init_plugin_frame(info);
//#endif
    
	effect_sampler = subsample_init( w );

#ifdef USE_GDK_PIXBUF
	vj_picture_init( &(info->settings->sws_templ));
#endif

	lzo_ = lzo_new();

	int vp = 0;  int frontback = 0;
	int pvp = 0; int pfb = 0;

	info->viewport = viewport_init( w, h, info->homedir, &vp, &frontback,0 );
	veejay_memset( &pvar_, 0, sizeof( varcache_t));

	if( pvp )
		veejay_msg(VEEJAY_MSG_INFO, "Initialized Projection");
 	if( vp )
		veejay_msg(VEEJAY_MSG_INFO, "Initialized Viewport");

	if( use_vp )
	{
		info->use_vp = vp;
		info->frontback = frontback;
		info->use_proj = pvp;
		veejay_msg(VEEJAY_MSG_INFO, "Loaded viewport and projection configuration");
	}

    return 1;
}


static void vj_perform_close_audio() {
	int i;

	if( lin_audio_buffer_ )
		free(lin_audio_buffer_ );
	veejay_memset( audio_buffer, 0, sizeof(uint8_t*) * SAMPLE_MAX_EFFECTS );

	/*
	for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
		if(audio_buffer[i]) free(audio_buffer[i]);
	*/
	if(top_audio_buffer) free(top_audio_buffer);
	if(resample_audio_buffer) free(resample_audio_buffer);
	if(audio_render_buffer) free( audio_render_buffer );
	if(down_sample_buffer) free( down_sample_buffer );
#ifdef HAVE_JACK
	for(i=0; i <= MAX_SPEED; i ++)
	{
		if(resample_context[i])
			audio_resample_close( resample_context[i] );
		if(downsample_context[i])
			audio_resample_close( downsample_context[i]);
	}

	if(resample_jack)
		audio_resample_close(resample_jack);
#endif
	veejay_msg(VEEJAY_MSG_INFO, "Stopped Audio playback task");
}

int vj_perform_init_audio(veejay_t * info)
{
  //  video_playback_setup *settings = info->settings;
#ifndef HAVE_JACK
	return 0;
#else
	int i;

	if(!info->audio )
	{
		veejay_msg(0,"No audio found");
		return 0;
	}
	/* top audio frame */
	top_audio_buffer =
	    (uint8_t *) vj_calloc(sizeof(uint8_t) * 2 * PERFORM_AUDIO_SIZE);
	if(!top_audio_buffer)
		return 0;

	down_sample_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * MAX_SPEED *4 );
	if(!down_sample_buffer)
		return 0;
	audio_render_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * MAX_SPEED * 4 );
	if(!audio_render_buffer)
		return 0;

	resample_audio_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * 2);
	if(!resample_audio_buffer) 
		return 0;

	/* chained audio */
	lin_audio_buffer_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * PERFORM_AUDIO_SIZE * SAMPLE_MAX_EFFECTS );
	if(!lin_audio_buffer_)
		return 0;

	for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
	    audio_buffer[i] = lin_audio_buffer_ + (PERFORM_AUDIO_SIZE * i);
	}
 
	for( i = 0; i <= MAX_SPEED; i ++ )
	{
		int out_rate = (info->edit_list->audio_rate * (i+2));
		int down_rate = (info->edit_list->audio_rate / (i+2));

		resample_context[i] = audio_resample_init(
					info->edit_list->audio_chans,
					info->edit_list->audio_chans, 
					info->edit_list->audio_rate,
					out_rate);
		if(!resample_context[i])
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize audio upsampler for speed %d", i);
			return 0;
		}
		downsample_context[i] = audio_resample_init(
					info->edit_list->audio_chans,
					info->edit_list->audio_chans,
					info->edit_list->audio_rate,
					down_rate );
		if(!downsample_context[i])
		{
			veejay_msg(VEEJAY_MSG_WARNING< "Cannot initialize audio downsampler for dup %d",i);
			return 0;
		}

		veejay_msg(VEEJAY_MSG_DEBUG, "Resampler %d: Speed %d resamples audio to %d Hz, Slow %d to %d Hz ", i,i+2,out_rate,
			i+2, down_rate );
	}
	return 1;
#endif
}

void vj_perform_free(veejay_t * info)
{
    int fblen = SAMPLE_MAX_EFFECTS; // mjpg buf
    int c;

    sample_record_free();

    if(info->edit_list->has_audio)
	    vj_perform_close_audio();

    for (c = 0; c < fblen; c++) {
	if(vj_perform_row_used(c))
		vj_perform_free_row(c);
	if(frame_buffer[c])
	{
	 	if(frame_buffer[c]->Y) free(frame_buffer[c]->Y);
		if(frame_buffer[c]->Cb) free(frame_buffer[c]->Cb);
		if(frame_buffer[c]->Cr) free(frame_buffer[c]->Cr);
		free(frame_buffer[c]);
	}
    }

   if(frame_buffer) free(frame_buffer);

	for( c = 0;c < 8; c++ )
	{
		if(primary_buffer[c]->Y) free(primary_buffer[c]->Y );
		free(primary_buffer[c] );
	}
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


   if( info->viewport )
	viewport_destroy( info->viewport );

   for(c=0; c < 3; c ++)
   {
      if(temp_buffer[c]) free(temp_buffer[c]);
   }
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

	if(lzo_)
		lzo_free(lzo_);
}

/***********************************************************************
 * return the chain entry where a cached frame is located or -1 if none
 */

// rate * channels * 2 = 16 bits             
int vj_perform_audio_start(veejay_t * info)
{
	int res;
	editlist *el = info->current_edit_list;	

	if (el->has_audio)
	{
#ifdef HAVE_JACK
		vj_jack_initialize();

 		res = vj_jack_init( info->current_edit_list );
		if( res <= 0)
		{
			info->audio=NO_AUDIO;
			veejay_msg(VEEJAY_MSG_WARNING,
				"Audio playback disabled (unable to connect to jack)");
			return 1;
		}

		jack_rate_ = vj_jack_rate();

		if ( res == 2 )
		{
			veejay_msg(VEEJAY_MSG_WARNING, "Jack plays at %d Hz, resampling audio from %d -> %d",jack_rate_,el->audio_rate,jack_rate_);
			resample_jack = audio_resample_init( el->audio_chans,el->audio_chans, jack_rate_, el->audio_rate);

			if(!resample_jack)
			{
				resample_jack = NULL;
				veejay_msg(VEEJAY_MSG_WARNING, "Cannot initialize resampler for %d -> %d audio rate conversion ",
					el->audio_rate,jack_rate_);
			}
	
			return 1;
		}
		else
		{
			veejay_msg(VEEJAY_MSG_INFO, "Jack is running the same samplerate as Veejay");
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
#ifdef HAVE_JACK
	vj_jack_stop();
	if(resample_jack)
	{
		audio_resample_close(resample_jack);
		resample_jack = NULL;
	}
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
	return primary_buffer[6]->Y;
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
		veejay_memset( &src, 0, sizeof(VJFrame));

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
	veejay_memset( video_output_buffer[i]->Y, 16, dst_w * dst_h );
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



//static int __global_frame = 0; 
//static int __socket_len = 0;
//static int __send_frame = 0;


static void long2str(unsigned char *dst, int32_t n)
{
   dst[0] = (n    )&0xff;
   dst[1] = (n>> 8)&0xff;
   dst[2] = (n>>16)&0xff;
   dst[3] = (n>>24)&0xff;
}

static	int	vj_perform_compress_frame( veejay_t *info, uint8_t *dst)
{
	const int len = info->effect_frame1->width * info->effect_frame1->height;
	const int uv_len = info->effect_frame1->uv_width * info->effect_frame1->uv_height;
	uint8_t *dstI = dst + (3*4);
	int size1=0,size2=0,size3=0;

	int i = lzo_compress( lzo_ , primary_buffer[info->out_buf]->Y, dstI, &size1, len );
	if( i == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to compress Y plane");
#ifdef STRICT_CHECKING
		assert(0);
#endif
		return 0;
	}
	dstI += size1;

	i = lzo_compress( lzo_, primary_buffer[info->out_buf]->Cb, dstI, &size2, uv_len );
	if( i == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to compress U plane");
#ifdef STRICT_CHECKING
		assert(0);
#endif
		return 0;
	}
	dstI += size2;

	i = lzo_compress( lzo_, primary_buffer[info->out_buf]->Cr, dstI, &size3, uv_len );
	if( i == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to compress V plane");
#ifdef STRICT_CHECKING
		assert(0);
#endif
		return 0;
	}	

	long2str( dst,size1);
	long2str( dst+4, size2 );
	long2str( dst+8, size3 );

	return (size1+size2+size3+12);
}

int	vj_perform_send_primary_frame_s2(veejay_t *info, int mcast, int to_link_id)
{
	const int len = info->effect_frame1->width * info->effect_frame1->height;
	const int uv_len = info->effect_frame1->uv_width * info->effect_frame1->uv_height;
	int hlen =0;

	int compr_len = 0;
	if( !mcast )
	{
		compr_len = vj_perform_compress_frame(info,socket_buffer+20);
#ifdef STRICT_CHECKING
		assert( compr_len > 0 );
#endif
		/* peer to peer connection */
		unsigned char info_line[32];
		sprintf(info_line, "%04d %04d %1d %08d", info->effect_frame1->width,
				info->effect_frame1->height, info->effect_frame1->format,
		      		compr_len );
		hlen = strlen(info_line );
#ifdef STRICT_CHECKING
		assert( hlen == 20 );
#endif
		veejay_memcpy( socket_buffer, info_line, hlen );
	}
	else	
	{
		compr_len = vj_perform_compress_frame(info,socket_buffer );
#ifdef STRICT_CHECKING
		assert(compr_len > 0 );
#endif
	}

	//@ was 0, but collides with VIMS . use seperate socket

	int id = (mcast ? 2: 3);
	
	int __socket_len = hlen + compr_len;
#ifdef STRICT_CHECKING
	assert( info->effect_frame1->ssm == 0 );
#endif

	if(vj_server_send_frame( info->vjs[id], to_link_id, socket_buffer, __socket_len,
				info->effect_frame1, info->real_fps )<=0)
	{
		/* frame send error handling */
		veejay_msg(VEEJAY_MSG_ERROR,
		  "Error sending frame to remote");
		//__send_frame=0;
		if(!mcast )
			_vj_server_del_client( info->vjs[id], to_link_id );
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
			uint8_t *src_frame[3];
			src_frame[0] = video_output_buffer[0]->Y;
			src_frame[1] = video_output_buffer[0]->Cb;
			src_frame[2] = video_output_buffer[0]->Cr;

			VJFrame *srci = yuv_yuv_template( video_output_buffer[0]->Y, video_output_buffer[0]->Cb,
						 video_output_buffer[0]->Cr, w, h, 
						 get_ffmpeg_pixfmt(info->pixel_format));
			VJFrame *dsti = yuv_yuv_template( video_output_buffer[1]->Y, video_output_buffer[1]->Cb,
						  video_output_buffer[1]->Cr, w, h,
						  PIX_FMT_YUV420P );

			yuv_convert_any( srci,dsti, srci->format, dsti->format );
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

void	vj_perform_unlock_primary_frame( void )
{
	video_output_buffer_convert = 0;
	// call this every cycle
}

void vj_perform_get_primary_frame_420p(veejay_t *info, uint8_t **frame )   
{
	editlist *el = info->edit_list;
	if(info->pixel_format==FMT_422 || info->pixel_format == FMT_422F)
	{
		uint8_t *pframe[3];
		pframe[0] = primary_buffer[info->out_buf]->Y;
		pframe[1] = primary_buffer[info->out_buf]->Cb;
		pframe[2] = primary_buffer[info->out_buf]->Cr;
	
		VJFrame *srci = yuv_yuv_template( pframe[0],pframe[1],pframe[2],el->video_width,el->video_height,
					get_ffmpeg_pixfmt( info->pixel_format));
		VJFrame *dsti = yuv_yuv_template( temp_buffer[0],temp_buffer[1], temp_buffer[2],el->video_width,
					el->video_height, (info->pixel_format == FMT_420 ? PIX_FMT_YUV420P:
						PIX_FMT_YUVJ420P) );
	
		yuv_convert_any(srci,dsti, srci->format,dsti->format );

		free(srci);
		free(dsti);

		frame[0] = temp_buffer[0];
		frame[1] = temp_buffer[1];
		frame[2] = temp_buffer[2];
	}
	else
	{
		frame[0] = primary_buffer[info->out_buf]->Y;
		frame[1] = primary_buffer[info->out_buf]->Cb;
		frame[2] = primary_buffer[info->out_buf]->Cr;
	}
}

static int	vj_perform_apply_first(veejay_t *info, vjp_kf *todo_info,
	VJFrame **frames, VJFrameInfo *frameinfo, int e , int c, int n_frame)
{
	int n_a = vj_effect_get_num_params(e);
	int entry = e;
	int err = 0;
	int args[16];

	veejay_memset( args, 0 , sizeof(args) );

	if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
	{
		if(!vj_tag_get_all_effect_args(todo_info->ref, c, args, n_a, n_frame ))
			return 1;
	}
	else
	{
		if(!sample_get_all_effect_arg( todo_info->ref, c, args, n_a, n_frame))
			return 1;
	}

	err = vj_effect_apply( frames, frameinfo, todo_info,entry, args );
	
	return err;
}

static void vj_perform_reverse_audio_frame(veejay_t * info, int len,
				    uint8_t * buf)
{
    int i;
    int bps = info->edit_list->audio_bps;
    uint8_t sample[bps];
    int x=len*bps;
    for( i = 0; i < x/2 ; i += bps ) {
		veejay_memcpy(sample,buf+i,bps);	
		veejay_memcpy(buf+i ,buf+(x-i-bps),bps);
		veejay_memcpy(buf+(x-i-bps), sample,bps);
	}
}


static int vj_perform_get_subtagframe(veejay_t * info, int sub_sample,
			       int chain_entry)
{

	int a = info->uc->sample_id;
	int b = sub_sample;
	int offset = vj_tag_get_offset(a, chain_entry);
	int sample_b[4];
	int len_b;  

	if(sample_get_short_info(b,&sample_b[0],&sample_b[1],&sample_b[2],&sample_b[3])!=0) return -1;

	len_b = sample_b[1] - sample_b[0];

	if(sample_b[3] >= 0)
	{
		offset += sample_b[3];
		if( offset >= len_b )
		{
			if(sample_b[2]==2)
			{
				offset = 0;
				sample_set_speed(b, (-1 * sample_b[3]));
				vj_tag_set_offset(a, chain_entry, offset);
				return sample_b[1];
			}
			if(sample_b[2]==1)
			{
				offset = 0;
			}
			if(sample_b[2] == 0)
			{
				offset = 0;
				sample_set_speed(b, 0);
			} 
		}
		if( sample_b[2] == 3  )
		{
			offset = sample_b[0] + ( (int) ( (double) len_b * rand()/RAND_MAX));
		}


		vj_tag_set_offset(a, chain_entry, offset);
		return (sample_b[0] + offset);
	}
	else
	{
		offset += sample_b[3];
		if ( offset < -(len_b))
		{
			if(sample_b[2] == 2)
			{
				offset = 0;
				sample_set_speed(b, (-1 * sample_b[3]));
				vj_tag_set_offset(a,chain_entry,offset);
				return sample_b[0];
			}
  			if(sample_b[2] == 1)
			{
				offset = 0;
			}
			if(sample_b[2]== 0)
			{
				sample_set_speed(b,0);
				offset = 0;
			}
		}

		if( sample_b[2] == 3  )
		{
			offset = sample_b[0] + ( (int) ( (double) len_b * rand()/RAND_MAX));
		}

		vj_tag_set_offset(a,chain_entry, offset);
		return (sample_b[1] + offset);

	}

    return -1;
}
/********************************************************************
 * int vj_perform_use_cached_frame( ... )
 *
 * simply copy the cached frame into the position in the frame
 * frame buffer for further processing.
 */
static void vj_perform_use_cached_ycbcr_frame(veejay_t *info, int centry, int chain_entry, int width, int height)
{
	int len1 = (width*height);

	if( centry == 0 )
	{
		int len2 = ( info->effect_frame1->ssm == 1 ? len1 : 
			     info->effect_frame1->uv_len );

		veejay_memcpy(frame_buffer[chain_entry]->Y,primary_buffer[0]->Y,  len1 );
		veejay_memcpy(frame_buffer[chain_entry]->Cb,primary_buffer[0]->Cb,len2 );
		veejay_memcpy(frame_buffer[chain_entry]->Cr,primary_buffer[0]->Cr,len2);
	}
	else
	{
		int c = centry - 1;
		int len2 = ( frame_buffer[c]->ssm == 1 ? len1 : info->effect_frame2->uv_len );

		veejay_memcpy(	frame_buffer[chain_entry]->Y,frame_buffer[c]->Y,   len1);
		veejay_memcpy(	frame_buffer[chain_entry]->Cb,frame_buffer[c]->Cb, len2);
		veejay_memcpy(	frame_buffer[chain_entry]->Cr,frame_buffer[c]->Cr, len2);
   	 }
}



static int vj_perform_get_subframe(veejay_t * info, int sub_sample,
			    int chain_entry)

{
    video_playback_setup *settings = (video_playback_setup*) info->settings;
    int a = info->uc->sample_id;
    int b = sub_sample;
    //int trim_val = sample_get_trimmer(a, chain_entry);

    int sample_a[4];
    int sample_b[4];

    int offset = sample_get_offset(a, chain_entry);	
    int len_a, len_b;
	if(sample_get_short_info(b,&sample_b[0],&sample_b[1],&sample_b[2],&sample_b[3])!=0) return -1;

	if(sample_get_short_info(a,&sample_a[0],&sample_a[1],&sample_a[2],&sample_a[3])!=0) return -1;

	len_a = sample_a[1] - sample_a[0];
	len_b = sample_b[1] - sample_b[0];
 
	/* offset + start >= end */
	if(sample_b[3] >= 0) /* sub sample plays forward */
	{
		if( settings->current_playback_speed != 0)
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
				offset = 0;
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
		if(settings->current_playback_speed != 0)
	    		offset += sample_b[3]; /* speed */

		if( sample_b[2] == 3 )
			offset = sample_b[0] + ( (int) ( (double) len_b * rand()/RAND_MAX));

		if ( offset < -(len_b)  )
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
				//offset = sample_b[1] - sample_b[0];
				offset = 0;
			}	
			if(sample_b[2]== 0)
			{
				sample_set_speed(b , 0);
				offset = 0;
			}
			if(sample_b[2] == 3 )
				offset = 0;
		}
		sample_set_offset(a, chain_entry, offset);
	
		return (sample_b[1] + offset);
	}
	return 0;
}


static int vj_perform_get_subframe_tag(veejay_t * info, int sub_sample,
			    int chain_entry)

{
    video_playback_setup *settings = (video_playback_setup*) info->settings;
    //int trim_val = sample_get_trimmer(a, chain_entry);

    int sample[4];

    int offset = sample_get_offset(sub_sample, chain_entry);	
    int nset = offset;
    int len;
     
	if(sample_get_short_info(sub_sample,&sample[0],&sample[1],&sample[2],&sample[3])!=0) return -1;

	len = sample[1] - sample[0];
 
	/* offset + start >= end */
	if(sample[3] >= 0) /* sub sample plays forward */
	{
		if( settings->current_playback_speed != 0)
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
				offset = 0;
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
		return (sample[0] + nset);
	}
	else
	{	/* sub sample plays reverse */
		if(settings->current_playback_speed != 0)
	    		offset += sample[3]; /* speed */
		if( sample[2] == 3  )
			offset = sample[0] + ( (int) ( (double) len * rand()/RAND_MAX));
	
		if ( offset < -(len)  )
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
				//offset = sample_b[1] - sample_b[0];
				offset = 0;
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
	
		return (sample[1] + nset);
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
int vj_perform_fill_audio_buffers(veejay_t * info, uint8_t *audio_buf, uint8_t *temporary_buffer)
{
#ifdef HAVE_JACK
	video_playback_setup *settings = info->settings;
	int len = 0;
	int speed = sample_get_speed(info->uc->sample_id);
	int bps   =  info->edit_list->audio_bps;
	int pred_len = (info->edit_list->audio_rate / info->edit_list->video_fps );
        int rs = 0;
	int n_samples = 0;

	int cur_sfd = sample_get_framedups( info->uc->sample_id );
	int max_sfd = sample_get_framedup( info->uc->sample_id );


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
				a_len = vj_el_get_audio_frame(info->edit_list,i, tmp);
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
#ifdef STRICT_CHECKING
			assert( resample_context[ n_frames ] != NULL );
#endif
			if( rs )
			{
				if( speed < 0 )
					vj_perform_reverse_audio_frame(info, n_samples, sambuf );
				n_samples = audio_resample( resample_context[n_frames-2], audio_buf, sambuf, n_samples );
			}
		} else if( speed == 0 ) {
			n_samples = len = pred_len;
			veejay_memset( audio_buf, 0, pred_len * bps );
		} else	{
			n_samples = vj_el_get_audio_frame( info->edit_list, settings->current_frame_num, audio_buf );
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
		if( cur_sfd == 0 )
		{
			// @ resample buffer
			int down_sample = audio_resample( downsample_context[ max_sfd-2 ], 
					down_sample_buffer, audio_buf, n_samples  );
		}
		else
		{
			n_samples = pred_len;
		}
		veejay_memcpy( audio_buf, down_sample_buffer + (cur_sfd * pred_len *bps ), n_samples * bps );
	}

	return n_samples;
#else
	return 0;
#endif
}

static int vj_perform_apply_secundary_tag(veejay_t * info, int sample_id,
				   int type, int chain_entry, int entry )
{				/* second sample */
    int width = info->edit_list->video_width;
    int height = info->edit_list->video_height;
    int error = 1;
    int nframe;
    int len = 0;
    int centry = -2;
#ifdef STRICT_CHECKING
	assert( frame_buffer[chain_entry]->Cb != frame_buffer[chain_entry]->Cr );
#endif
    uint8_t *fb[3] = {
		frame_buffer[chain_entry]->Y,
		frame_buffer[chain_entry]->Cb,
		frame_buffer[chain_entry]->Cr };

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
	
	centry = vj_perform_tag_is_cached(chain_entry, sample_id);

	if (centry == -1)
	{
		if(! vj_tag_get_active( sample_id ) )
		{
			// active stream if neccesaary
			vj_tag_set_active(sample_id, 1 );
		}
		
	  	if (vj_tag_get_active(sample_id) == 1 )
		{
			int res =  vj_tag_get_frame(sample_id, fb, audio_buffer[chain_entry]);

			if(res==1)	
			{
		  		error = 0;
		  	 	cached_tag_frames[1 + chain_entry] = sample_id;
	        	}
			else
			{
				error = 1;
		   		vj_tag_set_active(sample_id, 0);
			}
			cached_tag_frames[1 + chain_entry ] = sample_id;
			frame_buffer[chain_entry]->ssm = 0;
	     	}
	}
	else
	{	
		vj_perform_use_cached_ycbcr_frame(info, centry, chain_entry, width,height);
	    	cached_tag_frames[1 + chain_entry ] = sample_id;
	    	error = 0;
	}
	
	break;
	
   case VJ_TAG_TYPE_NONE:
	    nframe = vj_perform_get_subframe_tag(info, sample_id, chain_entry); // get exact frame number to decode
 	    centry = vj_perform_sample_is_cached(sample_id, chain_entry);
            if(centry == -1 || info->no_caching)
	    {
		editlist *el = sample_get_editlist( sample_id );
		if(el)
			len = vj_el_get_video_frame(el, nframe, fb); 
		if(len > 0)
			error = 0;
		
		frame_buffer[chain_entry]->ssm = 0;
	    }
	    else
	    {
	   	 if(centry == 0)
		 {
			int len = info->effect_frame1->len;
			int len2 = ( info->effect_frame1->ssm == 1  ? len : info->effect_frame1->uv_len );
			veejay_memcpy( frame_buffer[chain_entry]->Y, primary_buffer[0]->Y,  len);
			veejay_memcpy( frame_buffer[chain_entry]->Cb, primary_buffer[0]->Cb, len2);
			veejay_memcpy( frame_buffer[chain_entry]->Cr, primary_buffer[0]->Cr, len2);
			frame_buffer[chain_entry]->ssm = info->effect_frame1->ssm;
			error =  0;

		//	veejay_msg(0, "len1=%d, len2=%d,  ssm top = %d, ssm %d = %d",
		//		       len,len2, info->effect_frame1->ssm, chain_entry, frame_buffer[chain_entry]->ssm );
		 }
		else
		{
			int len = (width*height);
			int len2 = ( frame_buffer[centry-1]->ssm == 1 ? len : info->effect_frame2->uv_len );
			veejay_memcpy(frame_buffer[chain_entry]->Y, frame_buffer[centry-1]->Y, len);
			veejay_memcpy(frame_buffer[chain_entry]->Cb, frame_buffer[centry-1]->Cb,len2);
			veejay_memcpy(frame_buffer[chain_entry]->Cr, frame_buffer[centry-1]->Cr,len2);
			frame_buffer[chain_entry]->ssm = frame_buffer[centry-1]->ssm;
			error =0;
		//		veejay_msg(0, "len1=%d, len2=%d,  chain (read %d) = %d, ssm %d",
		//		       len,len2, chain_entry, centry-1,frame_buffer[chain_entry]->ssm,
		//		       frame_buffer[centry-1]->ssm );

		}
	   }
	   if(!error)
	  	 cached_sample_frames[chain_entry+1] = sample_id;

	   break;

    }

    if (error == 1) {
	dummy_apply( fb, width, height, VJ_EFFECT_COLOR_BLACK);
    }


    return 0;
}

static	int	vj_perform_get_frame_( veejay_t *info, int s1, long nframe, uint8_t *img[3] )
{
	editlist *el = ( s1 ? sample_get_editlist(s1) : info->current_edit_list);
	int speed = info->settings->current_playback_speed;
	int max_sfd = (s1 ? sample_get_framedup( s1 ) : info->sfd );
	int cur_sfd = (s1 ? sample_get_framedups(s1 ) : simple_frame_duplicator );
	if( cur_sfd <= max_sfd && max_sfd > 1 )
	{
		uint8_t *p0_buffer[3] = {
			primary_buffer[7]->Y,
			primary_buffer[7]->Cb,
			primary_buffer[7]->Cr };

		uint8_t *p1_buffer[3]= {
			primary_buffer[4]->Y,
			primary_buffer[4]->Cb,
			primary_buffer[4]->Cr };

		int speed = info->settings->current_playback_speed;
		int start = ( s1 ? sample_get_startFrame(s1) : info->settings->min_frame_num );
		int end   = ( s1 ? sample_get_endFrame(s1)  : info->settings->max_frame_num );	

		if( cur_sfd == 0 )
		{
			int p0,p1, n2frame;
			p0 = vj_el_get_video_frame( el, nframe, p0_buffer );
			n2frame = nframe + speed;
			if( n2frame < start ) n2frame = start; else if (n2frame > end ) n2frame = end;
			p1 = vj_el_get_video_frame( el, n2frame, p1_buffer );
			int uv_len = (info->effect_frame1->ssm ? info->effect_frame1->len : info->effect_frame1->uv_len );
			veejay_memcpy( img[0], p0_buffer[0], info->effect_frame1->len );
			veejay_memcpy( img[1], p0_buffer[1], uv_len);
			veejay_memcpy( img[2], p0_buffer[2], uv_len );
			return 1;
		}
		else
		{
			uint32_t i;
			const uint32_t N = max_sfd;
			const uint32_t n1 = cur_sfd;
			const float frac = 1.0f / (float) N * n1;
			const uint32_t len = el->video_width * el->video_height;
			for( i  = 0; i < len ; i ++ )
			{
				img[0][i] = p0_buffer[0][i] + ( frac * (p1_buffer[0][i] - p0_buffer[0][i]));
				img[1][i] = p0_buffer[1][i] + ( frac * (p1_buffer[1][i] - p0_buffer[1][i]));
				img[2][i] = p0_buffer[2][i] + ( frac * (p1_buffer[2][i] - p0_buffer[2][i]));
			}
			return 1;
		}
	}

	return vj_el_get_video_frame( el, nframe, img );

}


static int vj_perform_get_frame_fx(veejay_t *info, int s1, long nframe, uint8_t *img[3])
{
	return vj_el_get_video_frame(
				//info->edit_list,
				sample_get_editlist( s1 ),
				nframe,
				img );
}

/********************************************************************
 * int vj_perform_apply_nsecundary( ... )
 *
 * determines secundary frame by type v4l/yuv/file/vloopback
 * and decodes buffers directly to frame_buffer.
 *
 * returns 0 on success, -1 on error 
 */

static int vj_perform_apply_secundary(veejay_t * info, int sample_id, int type,
			       int chain_entry, int entry)
{				/* second sample */


    int width = info->edit_list->video_width;
    int height = info->edit_list->video_height;
    int error = 1;
    int nframe;
    int len;
    int res = 1;
    int centry = -2;

    uint8_t *fb[3] = {
		frame_buffer[chain_entry]->Y,
		frame_buffer[chain_entry]->Cb,
		frame_buffer[chain_entry]->Cr };

 //   if(chain_entry < 0 || chain_entry >= SAMPLE_MAX_EFFECTS) return -1;

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

	centry = vj_perform_tag_is_cached(chain_entry, sample_id); // is it cached?
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
			if(res==1)
			{ 
				error = 0;                               
				cached_tag_frames[1 + chain_entry] = sample_id; // frame is cached now , admin it 
			}	
			else
			{
				error = 1; // something went wrong
				vj_tag_set_active(sample_id, 0); // stop stream
			}
			frame_buffer[chain_entry]->ssm = 0;
	    	}
	}
	else
	{
	    vj_perform_use_cached_ycbcr_frame(info,centry, chain_entry, width, height);
	    cached_tag_frames[1 + chain_entry] = sample_id;
	    error = 0;
	}	
	break;
    case VJ_TAG_TYPE_NONE:
	    	nframe = vj_perform_get_subframe(info, sample_id, chain_entry); // get exact frame number to decode
 	  	centry = vj_perform_sample_is_cached(sample_id, chain_entry);

	    	if(centry == -1)
	    	{
			len = vj_perform_get_frame_fx( info, sample_id, nframe, fb );	
			if(len > 0 )
				error = 0;
			frame_buffer[chain_entry]->ssm = 0;
		}
		else
		{
			if(centry==0)
			{
				int len = (width * height);
				int len2 = ( info->effect_frame1->ssm == 1  ? len : info->effect_frame1->uv_len );
			
				veejay_memcpy( frame_buffer[chain_entry]->Y, primary_buffer[0]->Y,  len);
				veejay_memcpy( frame_buffer[chain_entry]->Cb, primary_buffer[0]->Cb, len2);
				veejay_memcpy( frame_buffer[chain_entry]->Cr, primary_buffer[0]->Cr,len2);
				error =  0;
				frame_buffer[chain_entry]->ssm = info->effect_frame1->ssm;
			}
		    	else
			{
				int len = (width * height);
				int len2 = ( frame_buffer[centry-1]->ssm == 1  ? len : info->effect_frame2->uv_len );
				
				veejay_memcpy( frame_buffer[chain_entry]->Y, frame_buffer[centry-1]->Y, len);
				veejay_memcpy( frame_buffer[chain_entry]->Cb, frame_buffer[centry-1]->Cb,len2);
				veejay_memcpy( frame_buffer[chain_entry]->Cr, frame_buffer[centry-1]->Cr,len2);
				error =0;

				frame_buffer[chain_entry]->ssm = frame_buffer[centry-1]->ssm;
			}
		}
		if(!error)
			cached_sample_frames[chain_entry+1] = sample_id;
	break;

    }

    if (error == 1)
	veejay_msg(0,"Unable to retrieve source for FX entry %d", chain_entry );
    return 0;
}

/********************************************************************
 * int vj_perform_sample_complete(veejay_t *info)
 *
 * this function assumes the data is ready and waiting to be processed
 * it reads the effect chain and performs associated actions.
 *
 * returns 0 on success 
 */

static int	vj_perform_tag_render_chain_entry(veejay_t *info, int chain_entry)
{
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
	frames[0]->format  = info->pixel_format;
	vjp_kf *setup;
    	setup = info->effect_info;
    	setup->ref = info->uc->sample_id;

	if (vj_tag_get_chain_status(info->uc->sample_id, chain_entry))
	{
		int effect_id = vj_tag_get_effect_any(info->uc->sample_id, chain_entry); // what effect is enabled
		if (effect_id > 0)
		{
 			int sub_mode = vj_effect_get_subformat(effect_id);
			int ef = vj_effect_get_extra_frame(effect_id);
			if(ef)
			{
		    		int sub_id =
					vj_tag_get_chain_channel(info->uc->sample_id,
						 chain_entry); // what id
		    		int source =
					vj_tag_get_chain_source(info->uc->sample_id, // what source type
						chain_entry);
		    		
				vj_perform_apply_secundary_tag(info,sub_id,source,chain_entry,0 ); // get it
			 	frames[1]->data[0] = frame_buffer[chain_entry]->Y;
	   	 		frames[1]->data[1] = frame_buffer[chain_entry]->Cb;
		    		frames[1]->data[2] = frame_buffer[chain_entry]->Cr;
				frames[1]->format = info->pixel_format;
				// sample B
	   			if(sub_mode && frames[1]->ssm == 0)
				{
					chroma_supersample(
						settings->sample_mode,
						effect_sampler,
						frames[1]->data,
						frameinfo->width,
						frameinfo->height );
					frames[1]->ssm = 1;
				}
				else if (!sub_mode && frames[1]->ssm == 1 )
				{
					  chroma_subsample(
                                	        settings->sample_mode,
                                      		effect_sampler,
                                       		frames[1]->data,
						frameinfo->width,
                                    		frameinfo->height
                                		);
					frames[1]->ssm = 0;
				}
			}

			if( sub_mode && frames[0]->ssm == 0 )
			{
				chroma_supersample(
					settings->sample_mode,
					effect_sampler,
					frames[0]->data,
					frameinfo->width,
					frameinfo->height );
				frames[0]->ssm = 1;
			}
			else if(!sub_mode && frames[0]->ssm == 1)
                        {
                                chroma_subsample(
                                        settings->sample_mode,
                                        effect_sampler,
                                        frames[0]->data,frameinfo->width,
                                        frameinfo->height
                                );
                                frames[0]->ssm = 0;
                        }
			
			vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,
				(int) settings->current_frame_num );
	    } // if
	} // for
	return 0;
}

static int	vj_perform_render_chain_entry(veejay_t *info, int chain_entry)
{
	int result = 0;
	VJFrame *frames[2];
	VJFrameInfo *frameinfo;
	video_playback_setup *settings = info->settings;
	frames[0] = info->effect_frame1;
	frames[1] = info->effect_frame2;
	frames[1]->format = info->pixel_format;
    	frameinfo = info->effect_frame_info;
    	frames[0]->data[0] = primary_buffer[0]->Y;
   	frames[0]->data[1] = primary_buffer[0]->Cb;
    	frames[0]->data[2] = primary_buffer[0]->Cr;
	frames[0]->format  = info->pixel_format;

	vjp_kf *setup;
    	setup = info->effect_info;
    	setup->ref = info->uc->sample_id;

	if (sample_get_chain_status(info->uc->sample_id, chain_entry))
	{
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
				vj_perform_apply_secundary(info,sub_id,source,chain_entry,0); // get it

			 	frames[1]->data[0] = frame_buffer[chain_entry]->Y;
	   	 		frames[1]->data[1] = frame_buffer[chain_entry]->Cb;
		    		frames[1]->data[2] = frame_buffer[chain_entry]->Cr;
				frames[1]->ssm     = frame_buffer[chain_entry]->ssm;	
				if(frames[1]->ssm == 0 && sub_mode)
				{
					chroma_supersample(
						settings->sample_mode,
						effect_sampler,	
						frames[1]->data,
						frameinfo->width,
						frameinfo->height );
					frames[1]->ssm = 1;
				}
				else if(frames[1]->ssm == 1 && !sub_mode )
				{
					frames[1]->ssm = 0;
					chroma_subsample(
						settings->sample_mode,
						effect_sampler,
						frames[1]->data,
						frameinfo->width,
						frameinfo->height
						);
				}
			}

			if( sub_mode && frames[0]->ssm == 0)
			{
				chroma_supersample(
					settings->sample_mode,
					effect_sampler,
					frames[0]->data,
					frameinfo->width,
					frameinfo->height );

				frames[0]->ssm = 1;
			}
			else if(!sub_mode && frames[0]->ssm == 1)
			{
				chroma_subsample(
					settings->sample_mode,
					effect_sampler,
					frames[0]->data,
					frameinfo->width,
					frameinfo->height
				);
				frames[0]->ssm = 0;
			}
		
			vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,
				(int) settings->current_frame_num );

	    	} // if
	} // status
	return 0;
}

static int vj_perform_sample_complete_buffers(veejay_t * info, int entry, int *hint444)
{
	int chain_entry;
	vjp_kf *setup;
	VJFrame *frames[2];
	VJFrameInfo *frameinfo;
	video_playback_setup *settings = info->settings;
    	int chain_fade =0;
    	setup = info->effect_info;

	frames[0] = info->effect_frame1;
	frames[1] = info->effect_frame2;
    	frameinfo = info->effect_frame_info;
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


/********************************************************************
 * int vj_perform_tag_complete(veejay_t *info)
 *
 * this function assumes the data is ready and waiting to be processed
 * it reads the effect chain and performs associated actions.
 *
 * returns 0 on success 
 */
static int vj_perform_tag_complete_buffers(veejay_t * info, int entry, int *hint444  )
{
//    	if (vj_tag_get_effect_status(info->uc->sample_id)!=1)
//			return 0;		/* nothing to do */
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

//	chain_fade = vj_tag_get_fader_active(info->uc->sample_id);
 //  	if(chain_fade)
	if( pvar_.fader_active )
		vj_perform_pre_chain( info, frames[0] );


	for(chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
	{
		vj_perform_tag_render_chain_entry( info, chain_entry);
	}

	*hint444 = frames[0]->ssm;
	return 1;
}


static void vj_perform_plain_fill_buffer(veejay_t * info, int entry)
{
	video_playback_setup *settings = (video_playback_setup*)  info->settings;
	uint8_t *frame[3];
	int ret = 0;
	frame[0] = primary_buffer[0]->Y;
	frame[1] = primary_buffer[0]->Cb;
	frame[2] = primary_buffer[0]->Cr;

	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
	{
		ret = vj_perform_get_frame_(info, info->uc->sample_id, settings->current_frame_num,frame );
	}
	else
	{
		ret = vj_el_get_video_frame(info->current_edit_list,settings->current_frame_num,frame);
	}

	if(ret <= 0)
	{
		veejay_msg(0, "Unable to queue video frae %d, stopping Veejay", 
			settings->current_frame_num );
		veejay_change_state_save(info, LAVPLAY_STATE_STOP);
	}
}

static int vj_perform_render_sample_frame(veejay_t *info, uint8_t *frame[3])
{
	int audio_len = 0;
	long nframe = info->settings->current_frame_num;
	uint8_t *buf = NULL;

	if( info->current_edit_list->has_audio )
	{
		buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE );
		audio_len = vj_perform_fill_audio_buffers(info, buf, audio_render_buffer );
	}
	int res = sample_record_frame( info->uc->sample_id,frame,buf,audio_len );

	if( buf )
		free(buf);

	return res;
}

	
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[3])
{
	long nframe = info->settings->current_frame_num;
	int sample_id = info->uc->sample_id;
	
	if(info->settings->offline_record)
		sample_id = info->settings->offline_tag_id;

	if(info->settings->offline_record)
	{
		if (!vj_tag_get_frame(sample_id, frame, NULL))
	   	{
			return 0;//skip
		}
	}

	return vj_tag_record_frame( sample_id, frame, NULL, 0);
}	

static int vj_perform_record_commit_single(veejay_t *info, int entry)
{
  //video_playback_setup *settings = info->settings;

  char filename[512];
  //int n_files = 0;
  if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
  {
 	if(sample_get_encoded_file(info->uc->sample_id, filename))
  	{
// does file exist ?
		int id = veejay_edit_addmovie_sample(info,filename, 0 );
		if(id <= 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Adding file %s to new sample", filename);
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
		int id = veejay_edit_addmovie_sample(info, filename, 0);
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
 if(info->uc->playback_mode==VJ_PLAYBACK_MODE_SAMPLE)
 {
	 sample_reset_encoder(info->uc->sample_id);
	 sample_reset_autosplit(info->uc->sample_id);
 	 if( settings->sample_record && settings->sample_record_switch)
	 {
		settings->sample_record_switch = 0;
		veejay_set_sample( info,sample_size()-1);
	 }
	 else
         {
 		veejay_msg(VEEJAY_MSG_INFO,"Not autoplaying new sample");
         }

	 settings->sample_record = 0;
	 settings->sample_record_id = 0;
	 settings->sample_record_switch =0;
     settings->render_list = 0;
 }

 if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
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
		info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
		veejay_set_sample(info ,sample_size()-1);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Not autoplaying new sample");
	}
 
  }

}


void vj_perform_record_sample_frame(veejay_t *info, int entry) {
	video_playback_setup *settings = info->settings;
	uint8_t *frame[3];
	int res = 1;
	int n = 0;
	frame[0] = primary_buffer[entry]->Y;
	frame[1] = primary_buffer[entry]->Cb;
	frame[2] = primary_buffer[entry]->Cr;
	
	if( available_diskspace() )
		res = vj_perform_render_sample_frame(info, frame);

	if( res == 2)
	{
		/* auto split file */
		int df = vj_event_get_video_format();
		int len = sample_get_total_frames(info->uc->sample_id);
		long frames_left = sample_get_frames_left(info->uc->sample_id) ;
		// stop encoder
		sample_stop_encoder( info->uc->sample_id );
		// close file, add to editlist
		n = vj_perform_record_commit_single( info, entry );
		// clear encoder
		sample_reset_encoder( info->uc->sample_id );
		// initialize a encoder
		if(frames_left > 0 )
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Continue, %d frames left to record", frames_left);
			if( sample_init_encoder( info->uc->sample_id, NULL,
				df, info->edit_list, frames_left)==-1)
			{
				veejay_msg(VEEJAY_MSG_ERROR,
				"Error while auto splitting "); 
			}
		}
		else
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Added new sample %d of %d frames",n,len);
		}
	 }

	
	 if( res == 1)
	 {
		sample_stop_encoder(info->uc->sample_id);
		vj_perform_record_commit_single( info, entry );
		vj_perform_record_stop(info);
	 }

	 if( res == -1)
	{
		sample_stop_encoder(info->uc->sample_id);
		vj_perform_record_stop(info);
 	}
}


void vj_perform_record_tag_frame(veejay_t *info, int entry) {
	video_playback_setup *settings = info->settings;
	uint8_t *frame[3];
	int res = 1;
	int stream_id = info->uc->sample_id;
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

	if(available_diskspace())
		res = vj_perform_render_tag_frame(info, frame);

	if( res == 2)
	{
		/* auto split file */
		int df = vj_event_get_video_format();
		
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
	 }

	
	 if( res == 1)
	 {
		vj_tag_stop_encoder(stream_id);
		vj_perform_record_commit_single( info, entry );	    
		vj_perform_record_stop(info);
	 }

	 if( res == -1)
	{
		vj_tag_stop_encoder(stream_id);
		vj_perform_record_stop(info);
 	}

}


static int vj_perform_tag_fill_buffer(veejay_t * info, int entry)
{
    int error = 1;
    uint8_t *frame[3];
    int type = pvar_.type;
    int active = pvar_.active;
    //	int type;
//	int active;
    frame[0] = primary_buffer[0]->Y;
    frame[1] = primary_buffer[0]->Cb;
    frame[2] = primary_buffer[0]->Cr;

//	type = vj_tag_get_type( info->uc->sample_id );
//	active = vj_tag_get_active(info->uc->sample_id );

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
		if( info->pixel_format == FMT_422 || info->pixel_format == FMT_422F )
			vj_el_init_422_frame( info->edit_list, &dumb );
		else
			vj_el_init_420_frame( info->edit_list, &dumb );

		dumb.data[0] = frame[0];
		dumb.data[1] = frame[1];
		dumb.data[2] = frame[2];

		dummy_apply(&dumb,
		    info->edit_list->video_width,
		    info->edit_list->video_height, VJ_EFFECT_COLOR_BLACK);
  	}
  return 1;      	
//  return (error == 1 ? -1 : 0);
}

/* vj_perform_pre_fade:
   prior to fading, we copy the orginal (unaffected) image to a tempory buffer */
static void vj_perform_pre_chain(veejay_t *info, VJFrame *frame)
{
	veejay_memcpy( temp_buffer[0] ,frame->data[0], frame->len );
	veejay_memcpy( temp_buffer[1], frame->data[1], frame->uv_len);
	veejay_memcpy( temp_buffer[2], frame->data[2], frame->uv_len ); /* /4 */
}

static	inline	void	vj_perform_supersample_chain( veejay_t *info, VJFrame *frame )
{
	chroma_supersample(
		info->settings->sample_mode,
		effect_sampler,
		temp_buffer,
		frame->width,
		frame->height
	);
	frame->ssm = 1;
}

static void vj_perform_post_chain_sample(veejay_t *info, VJFrame *frame)
{
	unsigned int opacity; 
	int i;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

   	int op_b;
//	int mode = sample_get_fader_active( info->uc->sample_id );
	int mode = pvar_.fader_active;
	
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
	      		veejay_msg(VEEJAY_MSG_INFO, "Sample Chain Auto Fade Out done");
		}
		if((dir>0) && (opacity==255))
		{
			sample_set_effect_status(info->uc->sample_id,0);
			sample_reset_fader(info->uc->sample_id);
			veejay_msg(VEEJAY_MSG_INFO, "Sample Chain Auto fade In done");
		}
    	}

/*	op_b = 255 - opacity;
	for(i=0; i < helper_frame->len;i ++ )
	{
		Y[i]= (( op_b * Y[i] ) + ( opacity * temp_buffer[0][i] )) >> 8;
	}
       	
	int len2 = ( frame->ssm == 1 ? helper_frame->len : helper_frame->uv_len );
	for(i=0; i < len2;i ++ )
	{
		Cb[i]= (( op_b * Cb[i] ) + ( opacity * temp_buffer[1][i] )) >> 8;
		Cr[i]= (( op_b * Cr[i] ) + ( opacity * temp_buffer[2][i] )) >> 8;
	}*/

	int len2 = ( frame->ssm == 1 ? helper_frame->len : helper_frame->uv_len );
	opacity_blend_apply( frame->data ,temp_buffer,frame->len, len2, opacity );
}


static void vj_perform_post_chain_tag(veejay_t *info, VJFrame *frame)
{
	unsigned int opacity; 
	int i;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	int op_b;
//	int mode = vj_tag_get_fader_active( info->uc->sample_id );

	int mode = pvar_.fader_active;
	
	if( !frame->ssm )
		vj_perform_supersample_chain( info, frame );

	if(mode == 2)
		opacity = (int) vj_tag_get_fader_val(info->uc->sample_id);
	else
     		opacity = (int) vj_tag_apply_fader_inc(info->uc->sample_id);
   
	if(mode != 2)
	{
 		int dir = vj_tag_get_fader_direction(info->uc->sample_id);
		if((dir < 0) && (opacity == 0))
		{
			vj_tag_set_effect_status(info->uc->sample_id,1);
			vj_tag_reset_fader(info->uc->sample_id);
			veejay_msg(VEEJAY_MSG_INFO, "Stream Chain Auto Fade done");
		}
		if((dir > 0) && (opacity == 255))
		{
			vj_tag_set_effect_status(info->uc->sample_id,0);
			vj_tag_reset_fader(info->uc->sample_id);
			veejay_msg(VEEJAY_MSG_INFO, "Stream Chain Auto Fade done");
		}
		
    	}

/*	op_b = 255 - opacity;
	for(i=0; i < frame->len;i ++ )
	{
		Y[i]= (( op_b * Y[i] ) + ( opacity * temp_buffer[0][i] )) >> 8;
	} 
	int len2 = ( frame->ssm == 1 ? helper_frame->len : helper_frame->uv_len );
	
	for(i=0; i < len2;i ++ )
	{
		Cb[i]= (( op_b * Cb[i] ) + ( opacity * temp_buffer[1][i] )) >> 8;
		Cr[i]= (( op_b * Cr[i] ) + ( opacity * temp_buffer[2][i] )) >> 8;
	}*/

	int len2 = ( frame->ssm == 1 ? helper_frame->len : helper_frame->uv_len );
	opacity_blend_apply( frame->data ,temp_buffer,frame->len, len2, opacity );
 
}

int vj_perform_queue_audio_frame(veejay_t *info, int frame)
{
	if( info->audio == NO_AUDIO || !info->current_edit_list->has_audio)
		return 1;

#ifdef HAVE_JACK
	editlist *el = info->current_edit_list;
	video_playback_setup *settings = info->settings;
	long this_frame = settings->current_frame_num;
	int num_samples =  (el->audio_rate/el->video_fps);
	int pred_len = num_samples;
	int bps		=   el->audio_bps;
	uint8_t *a_buf = top_audio_buffer;
	int rs = 0;
	if (info->audio == AUDIO_PLAY)
  	{
		if(settings->audio_mute || settings->current_playback_speed == 0 )
		{
			veejay_memset( a_buf, 0, num_samples * bps);
                        vj_jack_play( a_buf, num_samples * bps  );
			return 1;
		}

		switch (info->uc->playback_mode)
		{
			case VJ_PLAYBACK_MODE_SAMPLE:
				if( el->has_audio )
					num_samples = vj_perform_fill_audio_buffers(info,a_buf,	audio_render_buffer + (2* PERFORM_AUDIO_SIZE * MAX_SPEED));
				if(num_samples <= 0 )
				{
					num_samples = pred_len;
					veejay_memset( a_buf, 0, num_samples * bps );
				}
				else
					rs = 1;
				break;
			case VJ_PLAYBACK_MODE_PLAIN:
				if( el->has_audio )
				{
					num_samples =	vj_el_get_audio_frame(el, this_frame,a_buf );
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
			//@ pfffff there is no stream that delivers audio anyway yet
			case VJ_PLAYBACK_MODE_TAG:
				if(el->has_audio)
				{
				    	num_samples = vj_tag_get_audio_frame(info->uc->sample_id, a_buf);
					if(num_samples <= 0 )
					{
						num_samples = pred_len;
						veejay_memset(a_buf, 0, num_samples * bps );
					}
					else
						rs = 1;
				}
				break;
		}
		if( jack_rate_ != el->audio_rate && rs)
		{
#ifdef STRICT_CHECKING
			assert( resample_jack != NULL );
			assert( num_samples > 0 );
#endif
			veejay_memcpy( resample_audio_buffer, a_buf, num_samples * bps);
			num_samples = audio_resample( resample_jack, (short*)top_audio_buffer,(short*)a_buf, num_samples );
			vj_jack_play( top_audio_buffer, num_samples * bps );
		}
		else
		{
			vj_jack_play( a_buf, (num_samples * bps ));
		}
     }	
#endif
     return 1;
}

static	int	vj_perform_get_width( veejay_t *info )
{
#ifdef USE_GL
	if(info->video_out == 4 )
		return  x_display_width(info->gl);
#endif
#ifdef HAVE_SDL
	if(info->video_out <= 1 )
		return vj_sdl_screen_w(info->sdl[0]);
#endif
	return info->video_output_width;
}

static	int	vj_perform_get_height( veejay_t *info )
{
#ifdef USE_GL
	if(info->video_out == 4 )
		return x_display_height(info->gl);
#endif
#ifdef HAVE_SDL
	if(info->video_out <= 1 )
		return vj_sdl_screen_h(info->sdl[0]);
#endif
	return info->video_output_height;
}

static	int	vj_perform_render_viewport( veejay_t *info, video_playback_setup *settings )
{
	VJFrame *frame = info->effect_frame1;

	uint8_t *in[3];
	uint8_t *out[3];
	
	int deep = 0;

	in[0]  = primary_buffer[0]->Y;
	in[1]  = primary_buffer[0]->Cb;
	in[2]  = primary_buffer[0]->Cr;

	out[0] = primary_buffer[2]->Y;
	out[1] = primary_buffer[2]->Cb;
	out[2] = primary_buffer[2]->Cr;

	if(info->use_vp && info->which_vp == 0 && !info->settings->composite)
	{
		viewport_external_mouse( info->viewport, info->uc->mouse[0], info->uc->mouse[1], info->uc->mouse[2], info->frontback,
			vj_perform_get_width(info), vj_perform_get_height(info) );
	}

	if( viewport_render_ssm( info->viewport ))
	{
		if(!frame->ssm)
		{
			chroma_supersample(
				settings->sample_mode,
				effect_sampler,
				in,
				frame->width,
				frame->height
			       	);
			frame->ssm = 1;
		}
		deep = 2;
#ifdef STRICT_CHECKING
		assert( frame->ssm == 1 );
#endif
	}

	viewport_render(
		info->viewport,
		in,
		out,
		frame->width,
		frame->height,
		(frame->ssm == 1 ? frame->len : frame->uv_len) );

	if( info->frontback )
	{
		if( frame->ssm )
		{
			chroma_subsample( settings->sample_mode, effect_sampler, out, frame->width,frame->height );
			frame->ssm = 0;
		}
		veejay_memcpy( primary_buffer[0]->Y, out[0], frame->len );
		veejay_memcpy( primary_buffer[0]->Cb,out[1], frame->uv_len);
		veejay_memcpy( primary_buffer[0]->Cr,out[2], frame->uv_len);
		deep = 0;
	}

	return deep;
}

static	void	vj_perform_render_osd( veejay_t *info, video_playback_setup *settings, int destination )
{
	if(info->use_osd <= 0)
		return;

	VJFrame *frame = info->effect_frame1;
	frame->data[0] = primary_buffer[destination]->Y;
	frame->data[1] = primary_buffer[destination]->Cb;
	frame->data[2] = primary_buffer[destination]->Cr;

	if( !frame->ssm)
	{
		chroma_supersample(
			settings->sample_mode,
			effect_sampler,
			frame->data,
			frame->width,
			frame->height
		       	);
		frame->ssm = 1;
	}

	//@ Viewport is not enabled, do not set osd_extra
	//
	//
	if( info->which_vp == 0 )
	{
		if( viewport_active( info->viewport ) == 1 )
			info->uc->osd_extra = viewport_get_help( info->viewport );
		else
			info->uc->osd_extra = NULL;
	}
	else
	{
		info->uc->osd_extra = NULL;
	}

//	if(!info->settings->composite)
//	{
		vj_font_customize_osd(info->osd, info, info->use_osd, info->use_vp,info->which_vp );

		vj_font_render( info->osd, frame , settings->current_frame_num,info->uc->osd_extra );
//	}
}

static	void 	vj_perform_finish_chain( veejay_t *info )
{
	VJFrame *frame = info->effect_frame1;
    	frame->data[0] = primary_buffer[0]->Y;
   	frame->data[1] = primary_buffer[0]->Cb;
    	frame->data[2] = primary_buffer[0]->Cr;

	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
	{
		if( pvar_.fader_active )
			vj_perform_post_chain_tag(info,frame);
	}
	else if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
	{
		if( pvar_.fader_active)
			vj_perform_post_chain_sample(info,frame);
	}
}

static	void	vj_perform_finish_render( veejay_t *info, video_playback_setup *settings, int destination )
{
	VJFrame *frame = info->effect_frame1;
	VJFrame *frame2= info->effect_frame2;
	uint8_t *pri[3];

	pri[0] = primary_buffer[destination]->Y;
	pri[1] = primary_buffer[destination]->Cb;
	pri[2] = primary_buffer[destination]->Cr;

	if( settings->composite )
	{ //@ scales in software

		if(info->which_vp == 1 )
		{	//@ focus on projection screen
			composite_event( info->composite, info->uc->mouse[0],info->uc->mouse[1],info->uc->mouse[2],	
				vj_perform_get_width(info), vj_perform_get_height(info));
		}
		else if(info->use_vp)
		{	//@ focus on viewport screen
			viewport_external_mouse( info->viewport, info->uc->mouse[0], info->uc->mouse[1], info->uc->mouse[2], info->frontback,
				vj_sdl_screen_w( info->sdl[0] ), vj_sdl_screen_h(info->sdl[0]) );
		}

		composite_process( info->composite, pri, frame, info->use_vp, info->which_vp,
			viewport_active(info->viewport) );
	}

	if( frame->ssm == 1 )
	{
		chroma_subsample(
		settings->sample_mode,
			effect_sampler,
			pri,
			frame->width,
			frame->height
			);
		frame->ssm = 0;
	}

	if( frame2->ssm == 1 )
	{
/*		chroma_subsample(
			settings->sample_mode,
				effect_sampler,
				frame2->data,
				frame2->width,
			frame2->height);
		*/
		frame2->ssm=0;
	}

	if (info->uc->take_bg==1)
    	{
        	vj_perform_take_bg(info,pri);
        	info->uc->take_bg = 0;
		veejay_msg(VEEJAY_MSG_INFO, "Snapped background image");
    	} 


}

static	void	vj_perform_render_font( veejay_t *info, video_playback_setup *settings )
{
//	VJFrame *frame = font_frame;
	VJFrame	*frame = info->effect_frame1;

    	frame->data[0] = primary_buffer[0]->Y;
   	frame->data[1] = primary_buffer[0]->Cb;
    	frame->data[2] = primary_buffer[0]->Cr;

#ifdef HAVE_FREETYPE
	if(vj_font_norender( info->font, settings->current_frame_num ))
	{
		if( !frame->ssm )
		{
			chroma_supersample(
				settings->sample_mode,
				effect_sampler,
				frame->data,
				frame->width,
				frame->height
			       	);
			frame->ssm = 1;
		}
	
		vj_font_render( info->font, frame , settings->current_frame_num,NULL );
	}
#endif
}

static	void	vj_perform_record_frame( veejay_t *info, int n )
{
	VJFrame	*frame = info->effect_frame1;
	
	if(frame->ssm == 1)
	{ //@ do some nasty things to speed up downsampling
		video_playback_setup *settings = info->settings;
		uint8_t *dst[3] = { NULL, primary_buffer[3]->Cb,
					  primary_buffer[3]->Cr };
		chroma_subsample_cp( settings->sample_mode, effect_sampler,
				  frame->data,dst, frame->width,frame->height );
		uint8_t *chroma[2] = { primary_buffer[0]->Cb, primary_buffer[0]->Cr };
		primary_buffer[0]->Cb = dst[1];
		primary_buffer[0]->Cr = dst[2];

		if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
			vj_perform_record_tag_frame(info,n);
		else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
			vj_perform_record_sample_frame(info,n);

		primary_buffer[0]->Cb = chroma[0];
		primary_buffer[0]->Cr = chroma[1];
	}
	else
	{
		if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
			vj_perform_record_tag_frame(info,n);
		else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
			vj_perform_record_sample_frame(info,n);
	}
}

static	int	vj_perform_render_magic( veejay_t *info, video_playback_setup *settings, int frame )
{
	int deep = 0;
	//@ Finalize the FX chain (Could leave the FX chain supersampled)
	vj_perform_finish_chain( info );
   	
	//@ Render any subtitles in sample/stream (Leaves FX chain supersampled)
	vj_perform_render_font( info, settings );

	//@ record frame before rendering viewport (default)
	if( settings->vp_rec == 0 && pvar_.enc_active )
	{
		vj_perform_record_frame(info, frame );
	}

	//@ Render viewport
	if(info->use_vp && info->frontback==0)
		deep = vj_perform_render_viewport( info, settings );

	//@ record frame after rendering viewport
	if( settings->vp_rec == 1 && pvar_.enc_active )
	{
		vj_perform_record_frame(info,frame);
	}

	//@ Render OSD menu
	vj_perform_render_osd( info, settings, deep );

	//@ Do the subsampling 
	vj_perform_finish_render( info, settings,deep );

	return deep;
}


int vj_perform_queue_video_frame(veejay_t *info, int frame, const int skip_incr)
{
	video_playback_setup *settings = info->settings;

	if(skip_incr)
		return 1;

	int is444 = 0;
	int res = 0;


	veejay_memset( &pvar_, 0, sizeof(varcache_t));

	if(settings->offline_record)	
		vj_perform_record_tag_frame(info,0);
	
	switch (info->uc->playback_mode)
	{
		case VJ_PLAYBACK_MODE_SAMPLE:

			sample_var( info->uc->sample_id, &(pvar_.type),
					    &(pvar_.fader_active),
					    &(pvar_.fx_status),
					    &(pvar_.enc_active),
					    &(pvar_.active)
				  );

			
			vj_perform_plain_fill_buffer(info, frame);
		   
			cached_sample_frames[0] = info->uc->sample_id;
			//@ Render viewport
			if(info->use_vp && info->frontback)
				vj_perform_render_viewport( info, settings );

			if(vj_perform_verify_rows(info,frame))
		   	 	vj_perform_sample_complete_buffers(info, frame, &is444);

			
			info->out_buf = vj_perform_render_magic( info, info->settings,frame );
		   	res = 1;

     			 break;
		    
		case VJ_PLAYBACK_MODE_PLAIN:

			   vj_perform_plain_fill_buffer(info, frame);

			   if(info->use_vp && info->frontback)
				vj_perform_render_viewport( info,settings);
			   info->out_buf = vj_perform_render_magic( info, info->settings,frame );
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

			 if (vj_perform_tag_fill_buffer(info, frame))
			 {
				//@ Render viewport
				if(info->use_vp && info->frontback)
					vj_perform_render_viewport( info, settings );

				if(vj_perform_verify_rows(info,frame))
					vj_perform_tag_complete_buffers(info, frame, &is444);
				info->out_buf = vj_perform_render_magic( info, info->settings,frame );
			 }
			 res = 1;	 
		   	 break;
		default:
			return 0;
	}

	//FIXME: copy from 2 or 0 buffer
/*	if( __send_frame )
	{
		veejay_memcpy( primary_buffer[1]->Y, primary_buffer[info->out_buf]->Y, 
			info->effect_frame1->len );
		veejay_memcpy( primary_buffer[1]->Cb,primary_buffer[info->out_buf]->Cb,
			info->effect_frame1->uv_len);
		veejay_memcpy( primary_buffer[1]->Cr,primary_buffer[info->out_buf]->Cr,
			info->effect_frame1->uv_len);
		__send_frame = 0;
	}*/



	return res;
}


int vj_perform_queue_frame(veejay_t * info, int frame, int skip )
{
	video_playback_setup *settings = (video_playback_setup*) info->settings;
	if(!skip)
	{
		switch(info->uc->playback_mode) 
		{
			case VJ_PLAYBACK_MODE_TAG:
				vj_perform_increase_tag_frame(info, settings->current_playback_speed);
				break;
			case VJ_PLAYBACK_MODE_SAMPLE: 
	 			vj_perform_increase_sample_frame(info,settings->current_playback_speed);
	  			break;
			case VJ_PLAYBACK_MODE_PLAIN:
				vj_perform_increase_plain_frame(info,settings->current_playback_speed);
				break;
			default:
				break;
		}
	}
  	vj_perform_clear_cache();
	//__global_frame = 0;

	return 0;
}



int	vj_perform_randomize(veejay_t *info)
{
	video_playback_setup *settings = info->settings;
	if(settings->randplayer.mode == RANDMODE_INACTIVE)
		return 0;
	if(settings->randplayer.mode == RANDMODE_SAMPLE)
	{
		double n_sample = (double) (sample_size()-1);
		int take_n   = 1 + (int) (n_sample * rand() / (RAND_MAX+1.0));
		int min_delay = 1;
		int max_delay = 0;
		char timecode[15];
		if(!sample_exists(take_n))
		{
			veejay_msg(VEEJAY_MSG_DEBUG, 
			 "Sample to play (at random) %d does not exist",
				take_n);
			take_n = info->uc->sample_id;
			max_delay = min_delay + (int) ((double)max_delay * rand() / (RAND_MAX+1.0));
		settings->randplayer.max_delay = max_delay;
		settings->randplayer.min_delay = min_delay;	

		MPEG_timecode_t tc;
		
		
		mpeg_timecode(&tc, max_delay,
	                mpeg_framerate_code(mpeg_conform_framerate(
				info->edit_list->video_fps)),
				info->edit_list->video_fps );

		sprintf(timecode, "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f);

		veejay_msg(VEEJAY_MSG_DEBUG,
		 "Sample randomizer trigger in %s",
			timecode );

		veejay_set_sample( info, take_n );

		return 1;
		}
	}
	return 0;
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
		{
			if(!vj_perform_randomize(info))
			{
			  veejay_msg(VEEJAY_MSG_ERROR,
			   "Woops cant start randomizer");
			  settings->randplayer.mode = RANDMODE_INACTIVE;
			}
		}
		return 1;
	}
	return 0;	
}

