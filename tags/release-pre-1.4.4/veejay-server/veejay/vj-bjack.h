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
#ifndef VJ_BIOJACK_H
#define VJ_BIOJACK_H
int	vj_jack_initialize();
int 	vj_jack_init(int rate_hz, int channels, int bps);
int	vj_jack_stop();
int	vj_jack_reset();
int	vj_jack_play(void *data, int len);
int	vj_jack_pause();
int	vj_jack_resume();
int	vj_jack_rate();
int	vj_jack_get_space();
long 	vj_jack_get_status(long int *sec, long int *usec);
int	vj_jack_set_volume(int v);
int	vj_jack_c_play(void *data, int len, int entry);

#endif 
