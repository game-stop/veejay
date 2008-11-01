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
#ifdef HAVE_JACK
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-common.h>
#include <veejay/defs.h>
#include <samplerate.h>
#include <libvjaudio/audio.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
typedef struct
{
	float *in;
	float *out;
	float *tmp;
	SRC_STATE *state;
	SRC_DATA data;
	double last_ratio;
	int    resampler;
	int  size;
	int  pos;
	int  channels;
} audio_resample_t;

//@ Lavplay liblavplay.c MJPEGTOOLS project
void	vj_audio_sample_reverse( uint8_t *in, uint8_t *out, int n_samples, int bytes_per_sample )
{
	uint8_t sample[bytes_per_sample];
	unsigned int i;
	unsigned int n = (n_samples * bytes_per_sample);

	for( i = 0; i < n/2; i += bytes_per_sample )
	{
		memcpy(sample,in+i,bytes_per_sample);
		memcpy(out+i, in+(n-i-bytes_per_sample), bytes_per_sample);
		memcpy(out+(n-i-bytes_per_sample), sample, bytes_per_sample);
	}
	
}
void	vj_audio_free( void *dar )
{
	audio_resample_t *ar = (audio_resample_t*) dar;
#ifdef STRICT_CHECKING
	assert( ar != NULL );
	if( ar->resampler)
	{
		assert( ar->in != NULL );
		assert( ar->out != NULL );
	}
	assert( ar->state != NULL );
#endif
	if( ar->resampler )
	{
		free(ar->in);
		free(ar->out);
		src_delete( ar->state );

	}
	else
	{	
		free(ar->tmp);
	}
	free(ar);
	ar = NULL;
}

static inline float	audio_new_sample_( float freq, float amp, int n, long rate )
{
	float sample = amp * sin( 2.0 * M_PI * freq * n / rate);
	return sample;
}
static inline void uint8_t_to_float( float *dst, uint8_t *src, int n_samples, int bytes_per_sample )
{
	uint8_t *in = src;
	int i;
	for( i = 0; i < n_samples; i ++ )
	{
		int32_t sample_mem = 0;
		uint8_t *sample = (uint8_t*) &sample_mem;
		memcpy( sample, in, bytes_per_sample );
		in += bytes_per_sample;
		*dst++ = sample_mem / (8.0 * 0x10000000);
	}
}

static inline void float_to_uint8_t( float *src, uint8_t *dst , int n_samples, int bytes_per_sample)
{
	int i;
	for( i = 0; i < n_samples; i ++ )
	{
		float sample_value = src[i] * (8.0 * 0x10000000);
		
		int32_t sample;
		if( sample_value >= ( 1.0 * 0x7FFFFFFF)) {
			sample = 0x7FFFFFFF;
		} else if ( sample_value <= (-8.0 * 0x10000000)) {
			sample = -0x80000000;
		} else {
			sample = lrintf( sample_value );
		}

		sample >>= 8 * ((sizeof(int32_t)) - bytes_per_sample);
		
		uint8_t *sample_ptr = (uint8_t*) &sample;
		memcpy( dst, sample_ptr, bytes_per_sample );
		dst += bytes_per_sample;
	}
}

int	vj_audio_gen_tone( void *dar, double seconds, long rate, float freq, float amp )
{
	audio_resample_t *ar = (audio_resample_t*) dar;

	int n;
	float *buf = ar->tmp;
	int k=0;
	int c =0;
	int samples = (rate * seconds)/ar->channels;
	for( n = 0; n < samples; n ++ )
	{
		for( c = 0; c < ar->channels; c ++ )
		{
			buf[k++] = audio_new_sample_( freq, amp, n, rate );
		}
	}
	ar->size = rate * seconds;
	return k;
}

int	vj_audio_noise_pack( void *dar, void *audio_frame, int n_samples, int bytes_per_sample, int n_packets )
{
	AFrame *af = (AFrame*) audio_frame;
	audio_resample_t *ar = (audio_resample_t*) dar;
	
	float_to_uint8_t( ar->tmp, af->data , n_samples, bytes_per_sample);
	af->samples = n_samples * n_packets;
	af->bps     = bytes_per_sample;
//	ar->pos     += n_samples;
	af->num_chans = ar->channels;
//	if(ar->pos > ar->size)
//		ar->pos = 0;
	return af->samples;
}	

void	*vj_audio_init( int max_buffer_size, int channels, int resampler )
{
	audio_resample_t *ar = (audio_resample_t*) vj_malloc(sizeof(audio_resample_t));
	int error = 0;

	memset(ar,0,sizeof(audio_resample_t));

	if(!resampler)
	{
		ar->tmp = (float*) vj_malloc(sizeof( float ) * max_buffer_size );
		memset( ar->tmp,0,sizeof(float) * max_buffer_size );
	}
	else
	{
		ar->state = src_new( SRC_SINC_FASTEST, channels,&error );
		ar->in = (float*) vj_malloc( sizeof( float ) * max_buffer_size );
		ar->out = (float*) vj_malloc(sizeof( float ) * max_buffer_size );
		memset( ar->in,0, sizeof(float) * max_buffer_size );
		memset( ar->out,0,sizeof(float) * max_buffer_size );
	}

	if(error)
	{
		veejay_msg(0, "Error while initializing resampler");
		vj_audio_free( (void*) ar);
		return NULL;
	}
	ar->resampler = resampler;
	ar->size = max_buffer_size;
	ar->pos  = 0;
	ar->channels = channels;
	return (void*) ar;
}

int	vj_audio_resample_data( void *dar,
	       			uint8_t *input,
				uint8_t *output,
				int bps,
				int channels,
				int factor,
				int slow,
				int in_samples,
				int out_samples)
{
	audio_resample_t *ar = (audio_resample_t*) dar;
#ifdef STRICT_CHECKING
	assert( ar->resampler == 1 );
	assert( bps == 4 );
	assert( channels == 2 );
#endif
	SRC_DATA *d = &(ar->data);

	uint8_t_to_float( ar->in, input, in_samples, bps );

	d->data_in = ar->in;
	d->data_out = ar->out;
//	d->input_frames = in_samples / channels;
	if(slow)
	{	
		d->output_frames = d->input_frames * (slow+1);
		d->src_ratio = d->output_frames / (double) d->input_frames;
	}
	else
	{
//		d->output_frames = d->input_frames / factor;	
//		d->output_frames = d->input_frames;
	//	d->src_ratio = d->output_frames / (double) d->input_frames;
//		d->src_ratio = 2.0;
		d->input_frames = (in_samples / channels) * factor;
		d->output_frames = (in_samples / channels);
		d->src_ratio = d->output_frames / (double) d->input_frames;
	}
	d->end_of_input = 0;

	if( ar->last_ratio != d->src_ratio )
	{
		src_set_ratio( ar->state, d->src_ratio );
		ar->last_ratio = d->src_ratio;
	}
	veejay_msg( 0,"ratio is %g or %g, output frames = %d in %d", (double) ar->last_ratio,
		 (double) d->input_frames / d->output_frames, d->output_frames, d->input_frames );

	if( src_process( ar->state,d ) != 0 )
	{
		veejay_msg(0, "failed to resmample");
		return 0;
	}
	int gen_out_samples = d->output_frames * channels;

	float_to_uint8_t( ar->out, output, gen_out_samples, bps );

	return gen_out_samples;

	
}
#endif
