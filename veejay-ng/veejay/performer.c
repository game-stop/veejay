/* veejay - Linux VeeJay
 *           (C) 2002-2006 Niels Elburg <nelburg@looze.net> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <libvjmem/vjmem.h>
#include <veejay/veejay.h>
#include <veejay/defs.h>
#include <veejay/performer.h>
#include <libvevo/libvevo.h>
#include <libvjmsg/vj-common.h>
#include <vevosample/vevosample.h>
#include <libyuv/yuvconv.h>
#include <lo/lo.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#define PERFORM_AUDIO_SIZE 16384
#define PERFORM_FX 20
#define PERFORM_NS 10

#ifdef HAVE_JACK
#include <veejay/vj-bjack.h>
#include <libvjaudio/audio.h>
#endif
#include <ffmpeg/avutil.h>
#include <ffmpeg/avcodec.h>
/** \defgroup performer Performer
 *
 * The performer is the central pool of audio and video frames.
 * During initialization, the performer will allocate a series of linear
 * buffers, each buffer represents a plane. All buffers have
 * the same size. The performer tries to lock this memory to
 * be resident in RAM, but continious if it fails.
 * The maximum number of chunks can be changed on the commandline.
 *     
 * The linear buffer is divided into N chunks,
 * each one big enough to hold 1 video frame.
 * This chunk is stored, with some frame information,
 * in a Port. The properties in this port are used later
 * to push frame pointers to a Sample's FX entry.
 *       
 * When a video frame is queued, the performer will pre-process the 
 * Sample's FX Chain to build a list of Samples to collect.
 * This list is then iterated to fetch a frame from each Sample.
 * Each of these frames is stored in the linear buffer
 * 
 * The performer will push all frames a Sample needs for processing
 * a single FX entry. Once the Sample has finished processing,
 * the resulting output frame will be displayed. The current frame
 * is stored as input channel for the next entry to process.
 *  
 */

//! \typedef performer_t Performer runtime data
typedef struct
{
	uint8_t **frame_buffer;			//<<! Linear buffer, locked in RAM
	VJFrame **ref_buffer;			//<<! Chunks, pointers to frame_buffer
	VJFrame *out_buffers[2];		//<<! Temporary output buffer if a Plugin requires one
	VJFrame *step_buffer;			//<<! Stepped result buffer in this render step
	VJFrame **fx_buffer;			//<<! Chunks, pointer to resulting fx
	VJFrame *preview_bw;			//<<! Grayscale preview 
	VJFrame *preview_col;			//<<! Color preview
	VJFrame *display;			//<<! Pointer to last rendered frame
	void	*in_frames;			//<<! Port, list of frames needed for this cycle
	void	*out_frames;			//<<! Port, list of output frames accumulated
	uint8_t *audio_buffer;			//<<! Buffer used for audio data
	AFrame **audio_buffers;			//<<! Linear buffer, locked in RAM
	void	*resampler;			//<<! On the fly audio resampling
	void 	*sampler;			//<<! YUV sampler
	int	sample_mode;			//<<! YUV sample mode
	int	n_fb;				//<<! Maximum number of Chunks
	int	n_fetched;			//<<! Total Chunks consumed
	int	last;				//<<! Last rendered entry
	int	dlast;
} performer_t;
#ifdef STRICT_CHECKING
static	int	performer_verify_frame( VJFrame *f );
#endif
//! Allocate a new Chunk depending on output pixel format
/**!
 \param info Veejay Object
 \param p0 Pointer to Plane 0
 \param p1 Pointer to Plane 1
 \param p2 Pointer to Plane 2
 \param p3 Pointer to Plane 3
 \return New Chunk
 */
static	VJFrame	*performer_alloc_frame( veejay_t *info, uint8_t *p0, uint8_t *p1, uint8_t *p2, uint8_t *p3 )
{
	int i;
	sample_video_info_t *svit = (sample_video_info_t*) info->video_info;
	VJFrame *f = (VJFrame*) vj_malloc(sizeof(VJFrame));
	memset( f,0, sizeof( VJFrame ));

	veejay_msg(0, "%s:%d : %dx%d, fmt=%d",__FUNCTION__,__LINE__,svit->w,svit->h, svit->fmt );

	switch(svit->fmt)
	{
		case FMT_420:
		case FMT_420F:
			f->uv_width = svit->w / 2;
			f->uv_height = svit->h / 2;
			f->shift_h = 1;
			f->shift_v = 1;
			f->pixfmt = PIX_FMT_YUV420P;
			break;
		case FMT_422:
		case FMT_422F:
			f->uv_width = svit->w/2;
			f->uv_height = svit->h;
			f->shift_h = 0;
			f->shift_v = 1;
			f->pixfmt = PIX_FMT_YUV422P;
			break;
		case FMT_444:
		case FMT_444F:
			f->uv_width = svit->w;
			f->uv_height = svit->h;
			f->pixfmt = PIX_FMT_YUV444P;
			break;
		default:
			veejay_msg(0, "Unknown pixel format");
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;
			
	}	
	
	f->uv_len = f->uv_width * f->uv_height;
	f->len    = svit->w * svit->h;
	f->width  = svit->w;
	f->height = svit->h;
	f->format    = svit->fmt;
	f->sampling = 0;
	f->data[0] = p0;
	f->data[1] = p1;
	f->data[2] = p2;
	f->data[3] = p3;
/*
#ifdef STRICT_CHECKING
	assert( performer_verify_frame( f) );
#endif
*/	
	return f;
}

//! Initialize Performer. Initialize memory for audio and video rendering
/**!
  \param info Veejay Object
  \param max_fx Maximum Chunks
  \return Performer Object
  */
 
void	*performer_init( veejay_t *info, const int max_fb )
{
	int i;
#ifdef STRICT_CHECKING
	assert( info->video_info != NULL );
#endif
	sample_video_info_t *svit = (sample_video_info_t*) info->video_info;
	
	performer_t *p = (performer_t*) vj_malloc(sizeof(performer_t));
	memset(p,0,sizeof(performer_t));
	p->n_fb = max_fb;
	p->ref_buffer = (VJFrame**) vj_malloc(sizeof(VJFrame*) * p->n_fb);
	p->fx_buffer  = (VJFrame**) vj_malloc(sizeof(VJFrame*) * SAMPLE_CHAIN_LEN );
#ifdef HAVE_JACK
	p->audio_buffers = (AFrame**) vj_malloc(sizeof(AFrame*) * 3);
	p->audio_buffer = (uint8_t*) vj_malloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * 32 *3);
	memset( p->audio_buffer, 0, sizeof(uint8_t) * PERFORM_AUDIO_SIZE * 32*3 );
#endif
	p->frame_buffer = (uint8_t**) vj_malloc(sizeof(uint8_t*) * p->n_fb );
	p->sampler = subsample_init( svit->w );
	
	size_t tot_len = (size_t) ( svit->w * svit->h * (p->n_fb+5+SAMPLE_CHAIN_LEN)) * sizeof(uint8_t);
	int error=0;

	size_t audio_offset = 0;
#ifdef HAVE_JACK
	for( i = 0; i < 3; i ++ )
	{
		p->audio_buffers[i] = (AFrame*) vj_malloc(sizeof(AFrame));
		memset( p->audio_buffers[i], 0, sizeof(AFrame) );
		p->audio_buffers[i]->data = p->audio_buffer + audio_offset;
		audio_offset += (PERFORM_AUDIO_SIZE * 32);
	}
#endif
	
	for( i = 0; i < 4; i ++ )
	{
		p->frame_buffer[i] = (uint8_t*) vj_malloc( tot_len );
		error = mlock( p->frame_buffer[i], tot_len );
		switch(error)
		{
			case ENOMEM:
				veejay_msg(0, "Specified address range does not fit address space of Process. Did you set RLIMIT_MEMLOCK ?");
				break;
			case EPERM:
				veejay_msg(0, "Insufficient privileges. Are you root ?");
				break;
			case EINVAL:
				veejay_msg(0, "Did you already tell us you are not running Linux ?");
				break;
			default:

				break;
		}
	}
	subsample_clear_plane( 16, p->frame_buffer[0], tot_len );
	subsample_clear_plane( 128,p->frame_buffer[1], tot_len );
	subsample_clear_plane( 128,p->frame_buffer[2], tot_len );
	subsample_clear_plane( 0,  p->frame_buffer[3], tot_len );

	size_t offset = 0;
	for( i = 0; i < p->n_fb ; i ++ )
	{
		p->ref_buffer[i] = performer_alloc_frame(
				info,
				p->frame_buffer[0] + offset,
				p->frame_buffer[1] + offset,
				p->frame_buffer[2] + offset,
				p->frame_buffer[3] + offset );
		offset += (size_t) ( svit->w * svit->h * sizeof(uint8_t));
	}	
	
	p->out_buffers[0] = performer_alloc_frame(
			info,
			p->frame_buffer[0] + offset,
			p->frame_buffer[1] + offset,
			p->frame_buffer[2] + offset,
			p->frame_buffer[3] + offset );
	offset += (size_t) (svit->w * svit->h * sizeof(uint8_t));

	p->out_buffers[1] = performer_alloc_frame(
			info,
			p->frame_buffer[0] + offset,
			p->frame_buffer[1] + offset,
			p->frame_buffer[2] + offset,
			p->frame_buffer[3] + offset );
	offset += (size_t) (svit->w * svit->h * sizeof(uint8_t));

	p->step_buffer = performer_alloc_frame(
			info,
			p->frame_buffer[0] + offset,
			p->frame_buffer[1] + offset,
			p->frame_buffer[2] + offset,
			p->frame_buffer[3] + offset );
	
	offset += (size_t) (svit->w * svit->h * sizeof(uint8_t));
	for( i = 0; i < SAMPLE_CHAIN_LEN ; i ++ )
	{
		p->fx_buffer[i] = performer_alloc_frame(
				info,
				p->frame_buffer[0] + offset,
				p->frame_buffer[1] + offset,
				p->frame_buffer[2] + offset,
				p->frame_buffer[3] + offset );
		offset += (size_t) ( svit->w * svit->h * sizeof(uint8_t));
	}	
	
	p->preview_bw = performer_alloc_frame(
			info,
			p->frame_buffer[0] + offset,
			p->frame_buffer[1] + offset,
			p->frame_buffer[2] + offset,
			p->frame_buffer[3] + offset );
	offset += (size_t) (svit->w * svit->h * sizeof(uint8_t));

	p->preview_col = performer_alloc_frame(
			info,
			p->frame_buffer[0] + offset,
			p->frame_buffer[1] + offset,
			p->frame_buffer[2] + offset,
			p->frame_buffer[3] + offset );
	char type[100];
#ifdef HAVE_JACK
	sprintf(type, "Audio and Video");
	vj_jack_initialize();

#else
	sprintf(type, "Video Only");
#endif
	
	veejay_msg(2,"Performer: %s", type);
	veejay_msg(2,"\tFrame buffer length:\t%d Frames",max_fb);
	veejay_msg(2,"\tFrame buffer size  :\t%2.2f MB", (float)(tot_len*4)/(1024*1024));
	veejay_msg(2,"\tFrame buffer memory:\t%s",
		      !error? "Resident in RAM" : "Swappable");	
	char nfmat[100];

	switch(svit->fmt )
	{
		case FMT_420:
			sprintf(nfmat,"%s", "YCbCr 4:2:0 planar");
			p->sample_mode = SSM_420_JPEG_TR;
			break;
		case FMT_422:
			sprintf(nfmat,"%s", "YCbCr 4:2:2 planar");
			p->sample_mode = SSM_422_444;
			break;
		case FMT_444:
			sprintf(nfmat,"%s", "YCbCr 4:4:4 planar");
			p->sample_mode = SSM_444;
			break;
	}
	
	veejay_msg(2,"\tProcessing in      :\t%s", nfmat );
	


	return (void*) p;
}
//! Start audio task
/**!
 \param info Veejay Object
 \return Error code or audio rate
 */
long	performer_audio_start( veejay_t *info )
{
#ifdef HAVE_JACK
	int bps = 0;
	int chans = 0;
	long rate = 0;
	int bits = 0;
	int error = 0;
	if(sample_get_audio_properties( 
			info->current_sample,
			&bits,
			&bps,
			&chans,
			&rate )<= 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Sample without Audio");
		info->audio = NO_AUDIO;
		return 0;
	}

	performer_t *p =  info->performer;
	
#ifdef STRICT_CHECKING
	assert( bps > 0 );
	assert( chans > 0 );
	assert( rate > 0 );
	assert( bits > 0 );
#endif

	
	int res = vj_jack_init( bits, chans, rate );
	if( res <= 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING,
				"Unable to connect to Jack. Not playing Audio");
		info->audio = NO_AUDIO;
	}
	else
	{
		info->audio = AUDIO_PLAY;
		p->resampler =	vj_audio_init( PERFORM_AUDIO_SIZE * 32 , chans,1 );
		veejay_msg(0, "Playing Audio %d, %d channels, %d bits, %d bps",
				rate,chans,bits,bps );
#ifdef STRICT_CHECKING
		veejay_msg(0, "Audio support is broken");
		assert(0);
#endif
	}

	return rate;
#else
	return 0;
#endif
}
//! Continue playing audio
/**!
 \param info Veejay Object
 */
void	performer_audio_continue( veejay_t *info )
{
#ifdef HAVE_JACK
	performer_t *p = info->performer;

	int speed = sample_get_speed( info->current_sample );

	vj_jack_continue( speed );
#endif
}

//! Stop playing audio
/**!
 \param info Veejay Object
 \return Error code
 */
int	performer_audio_stop( veejay_t *info )
{
	int i;
#ifdef HAVE_JACK
	performer_t *p = info->performer;
	vj_jack_stop();
	if(info->audio == AUDIO_PLAY )
	{
		vj_audio_free( p->resampler );
		for( i = 0; i < 3; i ++ )
		{
			memset( p->audio_buffers[i]->data, 0, sizeof(uint8_t) * PERFORM_AUDIO_SIZE * 32 );
		}
	}
#endif	
	info->audio = NO_AUDIO;
	return 1;
}
//! Reset audio buffers
/**!
 \param info Veejay Object
 */
void	performer_audio_restart( veejay_t *info )
{
	int i;
#ifdef HAVE_JACK
	performer_t *p = info->performer;
	for( i = 0; i < 3; i ++ )
	{
		memset( p->audio_buffers[i]->data, 0, sizeof(uint8_t) * PERFORM_AUDIO_SIZE * 32 );
	}
#endif	
}

//! Destroy Performer
/**!
 \param info Veejay Object
 */
void	performer_destroy( veejay_t *info )
{
	performer_t *p = (performer_t*) info->performer;
#ifdef STRICT_CHECKING
	assert( p != NULL );
#endif
	if(p->sampler)
		subsample_free( p->sampler );
  	if(p->in_frames)
                vevo_port_free( p->in_frames );
	int i;
	for( i = 0; i < 4; i ++ )
	{
		//munlock( p->frame_buffer[i] );
		free( p->frame_buffer[i] );
	}
	for( i = 0; i < p->n_fb; i ++ )
		free( p->ref_buffer[i]);
	for( i = 0; i < SAMPLE_CHAIN_LEN; i ++ )
		free( p->fx_buffer[i]);
#ifdef HAVE_JACK
	free( p->audio_buffer );
	for( i = 0; i < 3; i ++ )
		free( p->audio_buffers[i]);
	free( p->audio_buffers );
#endif
	free( p->preview_col );
	free( p->preview_bw  );
	free( p->step_buffer );
	free( p->out_buffers[0] );
	free( p->out_buffers[1] );
	free( p->fx_buffer );
	free( p->frame_buffer );
	free( p->ref_buffer );
	free( p );
}

//! Get pointers to final Output Frame
/**!
 \param info Veejay Object
 \param frame Array of Pointers
 */

void	*performer_get_output_frame( veejay_t *info )
{
	performer_t *p = (performer_t*) info->performer;
	return p->display;
	//	VJFrame *res = p->out_buffers[p->last];
//	return res;		
}


void	performer_clean_output_frame( veejay_t *info )
{
	performer_t *p = (performer_t*) info->performer;
	memset( p->display->data[0],16,p->display->len );
	memset( p->display->data[1],128,p->display->uv_len );
	memset( p->display->data[2],128,p->display->uv_len );
}

//! Queue sufficient audio samples for immediate playback
/**!
 \param info Veejay Object
 \return Error code
 */
#ifdef HAVE_JACK
static	uint8_t *performer_fetch_audio_frames( veejay_t *info, int *gen_samples )
{
	performer_t *p = info->performer;
	AFrame *f = p->audio_buffers[0];
	AFrame *k = p->audio_buffers[1];
	AFrame *q = p->audio_buffers[2];
	int n_samples = f->samples;

	int speed = sample_get_speed(info->current_sample);
	int res = 0;
	int has_audio = sample_has_audio( info->current_sample);	
	if(!has_audio)
	{
		n_samples = 0;
	}

	veejay_msg("%s: has_audio=%d, n_samples=%d, speed=%d",__FUNCTION__,has_audio,n_samples, speed );
	
	if( n_samples == 0 )
	{
		sample_get_property_ptr(info->current_sample, "audio_spas", &n_samples );
		if(!has_audio)
		{
			veejay_msg(0, "%s: Not playing audio, faking samples to 1764", __FUNCTION__);
			*gen_samples = 1764;
			memset( f->data,0 , PERFORM_AUDIO_SIZE);
			return f->data;
		}
	}
		
	if( speed == 0 )
	{
		memset( f->data,0,PERFORM_AUDIO_SIZE );
	}
	else
	{
		res = sample_get_audio_frame( info->current_sample,f, abs(speed) );
		veejay_msg(0, "\tsample_get_audio_frame: %d result to buf %p", res, f->data);
#ifdef STRICT_CHECKING
		assert( f->rate == 44100 );
		assert( f->num_chans = 2 );
		assert( f->bps = 4 );
		assert( f->bits = 16 );
#endif
	}

	uint8_t *out = (speed == 1 || speed == -1 ? f->data : k->data );

	k->rate = f->rate;
	k->num_chans = f->num_chans;
	k->bps = f->bps;
	k->bits = f->bits;
	
	uint8_t *in  = f->data;
		
	veejay_msg(0, "\tOut buffer is %p, inbuffer is %p", out, in);

	if( speed < 0 )
	{
		vj_audio_sample_reverse( in, q->data, n_samples, f->bps );
		in = q->data;
		out = q->data;
	}

	int slow = sample_get_repeat( info->current_sample );
	if( (speed > 1 || speed < -1 || slow) && has_audio)
	{
		int b_samples = n_samples;

		veejay_msg(0, "\tInput buffer: %x,%x,%x,%x,%x",
				f->data[0],f->data[1],f->data[2],f->data[3],f->data[4]);
		
		
		n_samples = vj_audio_resample_data( p->resampler,
				in,
				k->data,
				f->bps,
				f->num_chans,
				abs(speed),
				slow,
				res,
				n_samples );
		out = k->data;
			veejay_msg(0, "\tOutput buffer: %x,%x,%x,%x,%x",
				out[0],out[1],out[2],out[3],out[4]);
		
		

		veejay_msg(0, "\tn_samples: %d, f->bps=%d, f->chans=%d, f->samples=%d, n_samples=%d",
				n_samples, f->bps, f->num_chans, f->samples, b_samples );
	}
	*gen_samples = n_samples;
	
	return out;
}
#endif
//! Record and encode video 
/**!
 \param info Veejay Object
 */
void	performer_save_frame( veejay_t *info )
{
	performer_t *p = (performer_t*) info->performer;

	if(sample_is_recording( info->current_sample ))
		if(sample_record_frame(
			info->current_sample,
			p->display,
			NULL,
			0 )==2)
		{
			char *file = sample_get_recorded_file( info->current_sample );
			//@ open and add to samplelist	
			void *sample = sample_new( 0 );
			if( sample_open( sample, file ,0, info->video_info ) <= 0 )
			{
				veejay_msg(0, "Unable to add recorded file '%s' to samplebank", file );
			}
			else
			{
				int id = samplebank_add_sample(sample);
				veejay_msg(0, "Added '%s' to samplebank as Sample %d", file, id );
			}
			free(file);
		}
			
}

//! Grab audio frames
/**!
 \param info Veejay Object
 \param skipa Skip audio frames
 \return Error code
 */
int	performer_queue_audio_frame( veejay_t *info, int skipa )
{
	if(info->audio != AUDIO_PLAY || skipa)
		return 1;

	
	static uint8_t *buffer_ = NULL;
	static int	j_samples_ = 0;
	static int	samples_played_ =0;
#ifdef HAVE_JACK
	video_playback_setup *settings = info->settings;
	sample_video_info_t *svit = (sample_video_info_t*) info->video_info;
	
	performer_t *p = info->performer;
	AFrame *f = p->audio_buffers[0];
	AFrame *k = p->audio_buffers[1];
	AFrame *q = p->audio_buffers[2];
	int res = 0;
	int n_samples_ = 0;
	int frame_repeat = sample_get_repeat_count( info->current_sample );
	int nf		 = sample_get_repeat( info->current_sample );
	if( frame_repeat == 0 )
	{
		buffer_ =	performer_fetch_audio_frames( info,  &n_samples_ );
		j_samples_ = (nf>0 ? n_samples_ / (nf+1) : n_samples_);
		samples_played_ = 0;
	}

	veejay_msg(0, "\tSkipa=%d,nf=%d,j_samples=%d, n_samples_=%d,samples_played=%d",
			skipa,nf,j_samples_, n_samples_, samples_played_ );
	
	
	if(nf == 0)
	{
		veejay_msg(0,"\tplaying %d samples, %d bytes, buffer %p", n_samples_, f->bps * n_samples_, buffer_ );
		vj_jack_play( buffer_, f->bps * n_samples_ );
	}

	if( nf > 0)
	{
		vj_jack_play( buffer_ + (samples_played_ * f->bps), f->bps * j_samples_ );
		samples_played_ += j_samples_;
	}
#endif	
	return 1;
}

//! Fetch all video frames needed for this run. 
/**
 \param info veejay_t
 \param skip_incr Skip video yes/no
 */

static int	performer_fetch_frames( veejay_t *info, void *samples_needed)
{
	performer_t *p = info->performer;
	char **fetch_list = vevo_list_properties( samples_needed );
	if(!fetch_list)
		return 0;
	int k = 0;
	int n_fetched = 0;
	char key[64];
	for( k = 0; fetch_list[k] != NULL && n_fetched < p->n_fb; k ++ )
	{
		void *Sk = NULL;
		void *value = NULL;
		int error = vevo_property_get( samples_needed,fetch_list[k],0, &Sk);
#ifdef STRICT_CHECKING
		if( error != VEVO_NO_ERROR )
			veejay_msg(0, "Error fetching '%s' , error code %d", fetch_list[k], error );
		assert( error == VEVO_NO_ERROR );
#endif
		value = (void*) p->ref_buffer[ n_fetched ];
		error = sample_get_frame( Sk, value  );
		if(error == VEVO_NO_ERROR )
		{ // Error recovery
			VJFrame *f = (VJFrame*) value;
			sample_increase_frame( Sk );
			sprintf(key,"%p", Sk );
			vevo_property_set( p->in_frames, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &value );
		}
		
		n_fetched++;
		
		free(fetch_list[k]);
	}	
	free(fetch_list);
	return n_fetched;
}


static	void	performer_down_scale_plane1x2( uint8_t *src_plane, unsigned int len, uint8_t *dst_plane)
{
	unsigned int k,n = 0;
	for( k = 0; k < len ; k+= 2, n ++ )
		dst_plane[n] = 	(src_plane[k] + src_plane[k+1]) >> 1;	
}
static	void	performer_down_scale_plane1x4( uint8_t *src_plane, unsigned int len, uint8_t *dst_plane)
{
	unsigned int k,n = 0;
	unsigned int l = (len/4)*4;
	for( k = 0; k < l; k+= 4, n ++ )
		dst_plane[n] = 	(src_plane[k] + src_plane[k+1] + src_plane[k+2] + src_plane[k+3]) >> 2;	
}
static	void	performer_down_scale_plane1x8( uint8_t *src_plane, unsigned int len, uint8_t *dst_plane)
{
	unsigned int k,n = 0;
	unsigned int l = (len/8)*8;
	for( k = 0; k < l; k+= 8, n ++ )
		dst_plane[n] = 	(
			src_plane[k]   + src_plane[k+1] +
			src_plane[k+2] + src_plane[k+3] +
		       	src_plane[k+4] + src_plane[k+5] + 
			src_plane[k+6] + src_plane[k+7]
				 ) >> 4;	
}

//! Generate a color or grayscale preview image
static	void	performer_preview_frame_greyscale( VJFrame *src, VJFrame *dst, int reduce)
{
	switch(reduce)
	{
		case PREVIEW_50:
			performer_down_scale_plane1x2( src->data[0], src->len, dst->data[0]);
			dst->len = src->len / 2;
			break;
		case PREVIEW_25:
			performer_down_scale_plane1x4( src->data[0], src->len, dst->data[0]);
			dst->len = src->len / 4;
			break;
		case PREVIEW_125:
			performer_down_scale_plane1x8( src->data[0], src->len, dst->data[0]);
			dst->len = src->len / 8;
			break;
		default:
			break;	
	}
}
//! Generate a color or grayscale preview image
static	void	performer_preview_frame_color( VJFrame *src, VJFrame *dst, int reduce)
{
	switch(reduce)
	{
		case PREVIEW_50:
			performer_down_scale_plane1x2( src->data[0], src->len, dst->data[0]);
			performer_down_scale_plane1x2( src->data[1], src->uv_len, dst->data[1]);
			performer_down_scale_plane1x2( src->data[2], src->uv_len, dst->data[2]);
			dst->len = src->len / 2;
			dst->uv_len = src->uv_len / 2;
			break;
		case PREVIEW_25:
			performer_down_scale_plane1x4( src->data[0], src->len, dst->data[0]);
			performer_down_scale_plane1x4( src->data[1], src->uv_len, dst->data[1]);
			performer_down_scale_plane1x4( src->data[2], src->uv_len, dst->data[2]);
			dst->len = src->len / 4;
			dst->uv_len = src->uv_len / 4;
			break;
		case PREVIEW_125:
			performer_down_scale_plane1x8( src->data[0], src->len, dst->data[0]);
			performer_down_scale_plane1x8( src->data[1], src->uv_len, dst->data[1]);
			performer_down_scale_plane1x8( src->data[2], src->uv_len, dst->data[2]);
			dst->len = src->len / 8;
			dst->uv_len = src->uv_len / 8;

			break;
		default:
			// veejay_change_state(info,stop)
			break;	
	}
}
//! Configure preview image properties 
/**
 \param info veejay_t
 \param p performer_t
 \param reduce Reduce width x height
 \param preview_mode Preview mode
 \return Error code
 */

int		performer_setup_preview( veejay_t *info,int reduce, int preview_mode )
{
     //@ use LZO for fast compression
     video_playback_setup *settings = info->settings;
     performer_t *p = info->performer;

     switch( preview_mode )
     {
		case PREVIEW_NONE:
			veejay_msg(VEEJAY_MSG_INFO, "Preview off");
			break;
		case PREVIEW_GREYSCALE:
			veejay_msg(VEEJAY_MSG_INFO, "Preview in greyscale only");
			break;
		case PREVIEW_COLOR:
			veejay_msg(VEEJAY_MSG_INFO, "Preview in full color");
			break;
		defult:
			veejay_msg(VEEJAY_MSG_ERROR, "Wrong preview mode");
			return 0;
			break;
     }
     
     if( reduce < PREVIEW_50 || reduce > PREVIEW_125 )
     {
	     veejay_msg(VEEJAY_MSG_ERROR, "Wrong reduce value");
	     return 0;
     }
     settings->preview	    = preview_mode;
     info->preview_size     = reduce;
     
     return 1; 
}

static	int	performer_push_out_frames( void *sample, performer_t *p, int i )
{
	int n = sample_scan_out_channels( sample, i );
	if( n <= 0 )
	{
	       	//@ There are no output channels
		return 0; 
	}
	
#ifdef STRICT_CHECKING
	assert(  n == 1 );
#endif
	sample_fx_push_out_channel( sample,i,0, p->out_buffers[p->last] );

	return n;
}

static	int	performer_push_in_frames( void *sample, performer_t *p, int i )
{
	int ni = 0,k;
	int error = 0;
	int n_channels = 0;
	void *channels = sample_scan_in_channels( sample, i,&n_channels );
	if(!channels || n_channels <= 0)
		return 0;

	char **list = vevo_list_properties( channels );
	if( list == NULL )
		return 0;

	for( k = 0; list[k] != NULL ; k ++ )		
	{
		char key[64];
		if( vevo_property_atom_type( channels, list[k] ) == VEVO_ATOM_TYPE_VOIDPTR)
		{
			void	*refsample = NULL;
			error = vevo_property_get( channels, list[k], 0, &refsample );
#ifdef STRICT_CHECKING
			assert(error == VEVO_NO_ERROR);
#endif
			int seq_num = atoi( list[k]+4 );
#ifdef STRICT_CHECKING
			assert(error == VEVO_NO_ERROR);
#endif
			void *frame = NULL;
			sprintf(key, "%p", refsample); 
			error = vevo_property_get( p->in_frames, key, 0, &frame );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif
			VJFrame *v = (VJFrame*) frame;
			sample_fx_push_in_channel( sample,i, seq_num, frame );
		}
		free(list[k]);
	}
	free(list);
	
	return n_channels;
}

#ifdef STRICT_CHECKING
static	int	performer_verify_frame( VJFrame *f )
{
	int i;
	int u = f->uv_len - f->uv_width;
	int un = f->uv_len;
	int y = f->len - f->width;
	int yn = f->len;
	int fu = 0;
	int fy = 0;
	long avg = 0;
	int avg_;

	veejay_msg(0, "%s:%d, frame %p , len=%d,uv_len=%d, w=%d,h=%d, uw=%d, uh=%d fmt=%d",
		__FUNCTION__,__LINE__, f, f->len,f->uv_len,f->width,f->height,
		f->uv_width,f->uv_height, f->format );


	return 1;
}
#endif

static	int	performer_render_entry( veejay_t *info, void *sample, performer_t *p, int i)
{
#ifdef STRICT_CHECKING
	assert( sample != NULL );
#endif
	double opacity = sample_get_fx_alpha( sample, i );
	char key[64];
	int error = 0;
	sprintf(key, "%p",sample);
		
	VJFrame *A = NULL;
	VJFrame *pass1 = p->fx_buffer[i];
	//@ Keep backup of original frame
	if( opacity > 0.0 )
	{
		VJFrame *A = NULL;
		error = vevo_property_get( p->in_frames, key, 0, &A );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		veejay_memcpy( pass1->data[0],A->data[0],A->len);
		veejay_memcpy( pass1->data[1],A->data[1],A->uv_len );
		veejay_memcpy( pass1->data[2],A->data[2],A->uv_len );
	}
	
	p->last++;

	if(p->last >1  )
		p->last = 0;
	
	int ni = performer_push_in_frames(sample, p, i );
	int no = performer_push_out_frames(sample,p, i );
	
	int idx = p->last;
	
	error = sample_process_fx( sample, i );
#ifdef STRICT_CHECKING
	assert(error == VEVO_NO_ERROR);
#endif

//	subsample_ycbcr_clamp_itu601_copy( out, p->ref_buffer[i] );

	if( opacity > 0.0 )
	{
		yuv_blend_opacity( pass1, p->out_buffers[idx],(uint8_t) (opacity * 0xff) );
	}


	if( info->itu601 )
	{
		subsample_ycbcr_itu601( p->sampler, p->out_buffers[idx] );
	}

	error = vevo_property_set( p->in_frames, key, VEVO_ATOM_TYPE_VOIDPTR,
			1, &(p->out_buffers[idx]));
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	return 1;	
}

//! After building a list of frames, push frames to plugins and render
/**!
 \param p Performer Object
 */
static void	performer_render_frame( veejay_t *info, int i )
{
	void *cs = info->current_sample;
#ifdef STRICT_CHECKING
	assert( cs != NULL );
#endif
	performer_t *p = (performer_t*) info->performer;
	p->display = p->ref_buffer[0];
#ifdef STRICT_CHECKING
	assert( performer_verify_frame(p->fx_buffer[0]) );
#endif
	for( i = 0; i < SAMPLE_CHAIN_LEN; i ++ )
	{ 
		if( sample_get_fx_status( cs, i ))
		{
			if(performer_render_entry( info, cs,p, i ))
				p->display = p->out_buffers[p->last];
		}
	}
}

static	void	performer_render_preview( veejay_t *info )
{
	performer_t *p = (performer_t*) info->performer;
 	video_playback_setup *settings = info->settings;
	int n_planes = 0;
	lo_blob planes[3];

	void *sender = veejay_get_osc_sender( info );
	if(!sender)
		return;
	
	switch(settings->preview )
	{
		case PREVIEW_NONE:
			return;
		case PREVIEW_GREYSCALE:
			performer_preview_frame_greyscale(
				p->display, p->preview_bw, info->preview_size );
			planes[0] = lo_blob_new( p->preview_bw->len, p->preview_bw->data[0] );
			veejay_bundle_add_blob( sender, "/update/preview", planes[0] );
			break;
		case PREVIEW_COLOR:
			performer_preview_frame_color( p->display, p->preview_col, info->preview_size );
			planes[0] = lo_blob_new( p->preview_col->len, p->preview_col->data[0] );
			planes[1] = lo_blob_new( p->preview_col->uv_len, p->preview_col->data[1] );
			planes[2] = lo_blob_new( p->preview_col->uv_len, p->preview_col->data[2] );
			veejay_bundle_add_blobs( sender, "/update/preview", planes[0],planes[1],planes[2]);
			break;
	}

	

}

//! Build a list of frames to fetch and fetch it
/**!
 \param info Veejay Object
 \param skip_incr Skip Increment
 */
int	performer_queue_frame( veejay_t *info, int skip_incr )
{
	video_playback_setup *settings = info->settings;

	performer_t *p = (info->performer);
#ifdef STRICT_CHECKING
	assert( info->current_sample != NULL );
#endif
	veejay_lock( info,__FUNCTION__ );
	
	void *queue_list =
		vpn( VEVO_ANONYMOUS_PORT );//ll
	int i;
	int error = 0;
	int dummy = 0;

	if(!skip_incr)
	{
		char key[64];
		sprintf(key, "%p", info->current_sample );
		error = vevo_property_set( queue_list, key, VEVO_ATOM_TYPE_VOIDPTR,1,&(info->current_sample));
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
	}

	//@ lock
	for( i = 0; i < SAMPLE_CHAIN_LEN; i ++ )
	{ 
		if( sample_get_fx_status( info->current_sample, i ))
		{
			void *channels = sample_scan_in_channels( info->current_sample, i , &dummy);
			if(channels)
			{
				error = vevo_special_union_ports( channels, queue_list );
#ifdef STRICT_CHECKING
				if( error != VEVO_NO_ERROR )
					veejay_msg(0,"Internal error while intersecting ports: %d", error );
				assert( error == VEVO_NO_ERROR );
#endif
			}
		}
	}
	/* Build reference list */
	p->in_frames = vpn( VEVO_ANONYMOUS_PORT );
	performer_fetch_frames( info, queue_list  );
	
	performer_render_frame(info, p);
	
	performer_render_preview( info );	
	
	samplebank_flush_osc(info, info->clients);

	vevo_port_free( queue_list );
	vevo_port_free( p->in_frames );

	p->in_frames = NULL;

	veejay_unlock( info, __FUNCTION__);
	return 0;
}


