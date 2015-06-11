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

int vj_jack_stop();

int vj_jack_start();

int vj_jack_pause();

int vj_jack_resume();

int	vj_jack_play(void *data, int len);

int	vj_jack_set_volume(int volume);

int	vj_jack_pause();

int	vj_jack_resume();

long	vj_jack_get_status(long int *sec, long int *usec);

void	vj_jack_enable();

void	vj_jack_disable();

int	vj_jack_get_space();

int vj_jack_continue(int speed);

int vj_jack_initialize();

int  vj_jack_rate();

#endif
#endif
