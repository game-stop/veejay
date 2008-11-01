/* vjnet - low level network I/O for VeeJay
 *
 *           (C) 2005 Niels Elburg <nelburg@looze.net> 
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
#ifndef MCASTRECEIVER_H
#define MCASTRECEIVER_H


void *mcast_new_receiver( const char *group_name, int port );

int		mcast_poll( void *v );

int		mcast_recv( void *v, void *dst, int len );

int		mcast_recv_frame( void *v, uint8_t *linear_buf , int total_len);

void		mcast_close_receiver( void *v );

int		mcast_receiver_set_peer( void *v, const char *hostname );

int		mcast_get_fd( void *data);

#endif
