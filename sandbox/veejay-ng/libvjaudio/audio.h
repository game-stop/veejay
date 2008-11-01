/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
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
#ifndef AUDIOUTILS_H
#define AUDIOUTILS_H

extern void	vj_audio_sample_reverse( uint8_t *in, uint8_t *out, int n_samples, int bytes_per_sample );

extern void	*vj_audio_init( int max_buffer_size, int channels, int resampler );
extern void	vj_audio_free( void *dar );

extern int	vj_audio_gen_tone( void *dar, double seconds, long rate, float freq, float amp );

extern int	vj_audio_noise_pack( void *dar, void *af, int n_samples, int bytes_per_sample, int n_packets );

extern int	vj_audio_resample_data( void *dar,
	       			uint8_t *input,
				uint8_t *output,
				int bps,
				int channels,
				int factor,
				int slow,
				int in_samples,
				int out_samples);

#endif
