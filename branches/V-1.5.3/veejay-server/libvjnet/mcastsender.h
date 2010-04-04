/* vjnet - low level network I/O for VeeJay
 *
 *           (C) 2005-2007 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef MCASTSENDER_HH
#define MCASTSENDER_HH
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>  
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <libvje/vje.h>
#include <stdint.h>
typedef struct
{
	char 	*group;
	int	sock_fd;
	int	addr_len;
	struct sockaddr_in	addr; 
	int	send_buf_size;
	uint32_t stamp;
} mcast_sender;

mcast_sender *mcast_new_sender( const char *group_name );
void	mcast_set_interface( mcast_sender *s, const char *interface );

int	mcast_send( mcast_sender *s, const void *buf, int len, int port_num );

int	mcast_send_frame( mcast_sender *s, const VJFrame *frame , uint8_t *buf, int total_len,long ms, int port_num ,int mode);

void	mcast_close_sender(mcast_sender *s );

int	mcast_sender_set_peer( mcast_sender *v, const char *hostname );

#endif
