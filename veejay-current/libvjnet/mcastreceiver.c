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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "mcastreceiver.h"
#include <libvjmsg/vj-common.h>
#include <libvje/vje.h>
#include "packet.h"
#include "common.h"
static 	void	print_error(char *msg)
{
	veejay_msg(VEEJAY_MSG_ERROR,"%s: %s\n", msg,strerror(errno));
}

mcast_receiver	*mcast_new_receiver( const char *group_name, int port )
{
	mcast_receiver *v = (mcast_receiver*) malloc(sizeof(mcast_receiver));
	if(!v) return NULL;
	int	on = 1;
	struct ip_mreq mcast_req;
	
	memset( &mcast_req, 0, sizeof(mcast_req ));
	memset( &(v->addr), 0, sizeof(struct sockaddr_in) );
	v->group = (char*) strdup( group_name );
	v->port = port;

	v->sock_fd	= socket( AF_INET, SOCK_DGRAM, 0 );
	if(v->sock_fd < 0)
	{
		print_error("socket");
		if(v->group) free(v->group);
		if(v) free(v);
		return NULL;
	}

#ifdef SO_REUSEADDR
	if ( setsockopt( v->sock_fd, SOL_SOCKET, SO_REUSEADDR, &on,sizeof(on))<0)
	{
		print_error("SO_REUSEADDR");
		if(v->group) free(v->group);
		if(v) free(v);
		return NULL;
	}
#endif
#ifdef SO_REUSEPORT
	if ( setsockopt( v->sock_fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))<0)
	{
		print_error("SO_REUSEPORT");
		if(v->group) free(v->group);
		if(v) free(v);
		return NULL;
	}
#endif

	v->addr.sin_addr.s_addr = htonl( INADDR_ANY );
	v->addr.sin_port = htons( v->port );

	if( bind( v->sock_fd, (struct sockaddr*) &(v->addr), sizeof(struct sockaddr_in))<0)
	{
		print_error("bind");
		if(v->group) free(v->group);
		if(v) free(v);
		return NULL;
	}
	mcast_req.imr_multiaddr.s_addr = inet_addr( v->group );
	mcast_req.imr_interface.s_addr = htonl( INADDR_ANY );
	if( setsockopt( v->sock_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mcast_req,
		sizeof(mcast_req) ) < 0 )
	{
		print_error("IP_ADD_MEMBERSHIP");
		if(v->group) free(v->group);
		if(v) free(v);
		return NULL;
	}

	return v;
}
int     mcast_receiver_set_peer( mcast_receiver *v, const char *hostname )
{
        struct hostent *host;
        host = gethostbyname( hostname );
        if(host)
        {
                v->addr.sin_family = host->h_addrtype;
                if( host->h_length > (int) sizeof(v->addr.sin_addr))
                        host->h_length = sizeof( v->addr.sin_addr );
                memcpy( &(v->addr.sin_addr), host->h_addr, host->h_length );
        }
        else
        {
                v->addr.sin_family = AF_INET;
                if( !inet_aton( hostname, &(v->addr.sin_addr) ) )
                {
                        print_error(" unknown host");
                        return 0;
                }
        }
        return 1;
}

int	mcast_poll( mcast_receiver *v )
{
	fd_set fds;
	struct timeval tv;
	memset( &tv, 0, sizeof(tv) );
	FD_ZERO( &fds );	
	FD_SET( v->sock_fd, &fds );

	if( select( v->sock_fd + 1, &fds, 0,0, &tv ) <= 0 )
		return 0;
	return 1;
}

static int	mcast_poll_timeout( mcast_receiver *v, long timeout )
{
	fd_set fds;
	struct timeval tv;
	int n = 0;
	tv.tv_sec = 0;
	tv.tv_usec = 80000 + timeout; // 0.05 seconds
	FD_ZERO( &fds );	
	FD_SET( v->sock_fd, &fds );

	n = select( v->sock_fd + 1, &fds, 0,0, &tv );
	if(n == -1)
		print_error("timeout select");

	if( n <= 0)
		return 0;

	return 1;
}



int	mcast_recv( mcast_receiver *v, void *buf, int len )
{
	int n = recv( v->sock_fd, buf, len, 0 );
	if ( n == -1 )
		print_error("recv");

	return n;
}


int	mcast_recv_frame( mcast_receiver *v, uint8_t *linear_buf, int total_len )
{
	uint32_t sec,usec;
	int	 n_packets = ( total_len / CHUNK_SIZE);
	int	 i=0;
	int	 tb=0;
	uint8_t  chunk[PACKET_PAYLOAD_SIZE];
	packet_header_t header;
	frame_info_t 	info;

	header.timeout = 20000; // first time timeout

	while( i <= n_packets )
	{
		uint8_t *dst = linear_buf;
		int n;

		retry:

		/* there may not be any data (packet never arrived) */
		/* Bah... we must take note that some packets will never arrive */
		if( mcast_poll_timeout(v, header.timeout) == 0 )
		{
			return 0;
		}
		/* peek , dont remove data from queue */
		n = recv( v->sock_fd, chunk, PACKET_PAYLOAD_SIZE, MSG_PEEK );	
		if( n <= 0) 
		{
			veejay_msg(VEEJAY_MSG_ERROR, "cannot read more data, only got %d bytes (%d)", tb,n );
			print_error("recv frame");
			goto error;
		}
		/* read header */
		header = packet_get_header( chunk );

		/* first packet received, set timestamp */
		if( i == 0 )
		{
			sec = header.sec;
			usec = header.usec;
		}

		if( sec != header.sec || usec != header.usec )
		{
			/* ignore old frames */
			if( header.usec < usec || header.sec < sec )
			{
				n = recv( v->sock_fd, chunk, PACKET_PAYLOAD_SIZE,0 );
				if( n <= 0 )
				{
					veejay_msg(VEEJAY_MSG_ERROR, "error flushing data!");
				}
				i = 0;
	
				goto retry;
			}
			/* got newer packet, take this packet (ignore 'current') */
			if( header.usec > usec || header.sec > sec )
			{	
				i = 0;
				goto retry;
			}

			goto error; 
		}
		if( header.seq_num > n_packets )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "invalid sequence number! %d / %d", header.seq_num, n_packets);
			goto error;
		}

		/* get the data */      
		packet_get_data( &header, &info, chunk, dst );

		/* remove data from receive queue */
		n = recv( v->sock_fd, chunk, PACKET_PAYLOAD_SIZE, 0  );
		if( n <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "error removing data from receive queue");
		}
		tb += n;
		i++;
	}	

	return 1;

	error:

	return 0;
}


void	mcast_close_receiver( mcast_receiver *v )
{
	if(v)
	{
		close(v->sock_fd);
		if(v->group) free(v->group);
		v->group = NULL;
	}
}
