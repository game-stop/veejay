/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef VJ_JACK_H
#define VJ_JACK_H

#include <config.h>
#ifdef HAVE_JACK
#include <libel/vj-el.h>

int vj_jack_init(editlist *el);

int vj_jack_update_buffer( uint8_t *buff, int bps, int num_channels, int buf_len);

int vj_jack_stop(void);

int vj_jack_start(void);

int vj_jack_pause(void);

int vj_jack_resume(void);

int	vj_jack_play(void *data, int len);

int	vj_jack_set_volume(int volume);

int	vj_jack_pause(void);

int	vj_jack_resume(void);

long	vj_jack_get_status(long int *sec, long int *usec);

void	vj_jack_enable(void);

void	vj_jack_disable(void);

int	vj_jack_get_space(void);

int vj_jack_initialize(void);

int  vj_jack_get_client_samplerate(void);

int  vj_jack_get_rate(void);

void	vj_jack_reset(void);

void vj_jack_debug(int skipv, int skipa);

double vj_jack_get_played_position(void);

int     vj_jack_get_ringbuffer_frames_free(void);

int     vj_jack_get_ringbuffer_size(void);

int     vj_jack_get_period_size(void);

int vj_jack_client_to_jack_frames(int client_frames);

long    vj_jack_get_written_frames(void);

long vj_jack_get_required_free_frames(int client_frames);

long vj_jack_get_ringbuffer_used(void);

unsigned long vj_jack_get_played_frames(void);

int vj_jack_is_stopped(void);
int vj_jack_is_playing(void);

unsigned long vj_jack_get_bytes_per_frame(void);

int vj_jack_is_callback_active(void);

void vj_jack_flush(void);

double vj_jack_get_total_latency(void);

long vj_jack_underruns(void);

long vj_jack_get_bytes_stored_from_driver(void);

int vj_jack_is_running(void);

int vj_jack_xrun_flag(void);

#endif
#endif
