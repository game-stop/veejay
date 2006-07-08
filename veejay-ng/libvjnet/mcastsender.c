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

/** \defgroup mcastsend Multicast UDP sender
 */

#include <errno.h>
#include <stdio.h> 
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>  
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <veejay/defs.h>
#include <libvjmsg/vj-common.h>
#include "common.h" 
#include "packet.h"
#include "mcastsender.h"
typedef struct
{
	char 	*group;
	int	sock_fd;
	int	addr_len;
	struct sockaddr_in	addr; 
	int	send_buf_size;
} mcast_sender;

static void print_error(char *msg)
{
	veejay_msg(VEEJAY_MSG_ERROR, "%s: %s", msg,strerror(errno));
}

void	*mcast_new_sender( const char *group_name )
{
	int on = 1;
	uint8_t ttl = 1;
	mcast_sender *v = (mcast_sender*) malloc(sizeof(mcast_sender));
	if(!v) return NULL;
	v->group	= (char*)strdup( group_name );
	v->addr.sin_addr.s_addr = inet_addr( v->group );
	v->addr.sin_port 	= htons( 0 );
	v->addr_len = sizeof( struct sockaddr_in );	
	v->sock_fd = socket( AF_INET, SOCK_DGRAM, 0 );
	v->send_buf_size = 240 * 1024;

	if( v->sock_fd == -1 )
	{
		print_error( "socket");
		if(v) free(v);
		return NULL;
	}

#ifdef SO_REUSEADDR
	if( setsockopt( v->sock_fd, SOL_SOCKET, SO_REUSEADDR, &on,sizeof(on))<0 )
	{
		print_error("SO_REUSEADDR");
		if(v) free(v);
		return NULL;
	}
#endif
#ifdef SO_REUSEPORT
	if( setsockopt( v->sock_fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0 )
	{
		print_error("SO_REUSEPORT");
		if(v) free(v);
		return NULL;
	}
#endif
	if( setsockopt( v->sock_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0 )
	{
		print_error("IP_MULTICAST_TTL");
		if(v) free(v);
		return NULL;
	}

//	if( setsockopt( v->sock_fd, SOL_SOCKET, SO_SNDBUF, &(v->send_buf_size), sizeof(int)) < 0 )
//	{
//		print_error("so_sndbuf");
//	}

//	if( getsockopt( v->sock_fd, SOL_SOCKET, SO_SNDBUF, &(v->send_buf_size),
//		sizeof(int)) < 0 )
//		print_error();

	return (void*)v;
}	

int	mcast_sender_set_peer( void *data, const char *hostname )
{
	mcast_sender *v = (mcast_sender*) data;
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

void		mcast_set_interface( void *data, const char *interface )
{
	mcast_sender *v = (mcast_sender*) data;

	struct sockaddr_in	if_addr;
	memset( &if_addr, 0, sizeof(if_addr) );
	
	v->addr.sin_addr.s_addr = inet_addr( interface );
	v->addr.sin_family = AF_INET;

	if( setsockopt( v->sock_fd, IPPROTO_IP, IP_MULTICAST_IF, &if_addr, sizeof(if_addr) ) < 0 )
		print_error("IP_MULTICAST_IF");
}

int		mcast_send( void *data, const void *buf, int len, int port_num )
{
	int n ;
	mcast_sender *v = (mcast_sender*) data;

	v->addr.sin_port = htons( port_num );
	
	n =  sendto( v->sock_fd, buf, len, 0, (struct sockaddr*) &(v->addr), v->addr_len );

	if( n == -1 )
	{
		char msg[100];
		sprintf(msg, "mcast send -> %d",
			port_num );
		print_error(msg);
	}

	return n;
} 


int		mcast_send_frame( void *data, const VJFrame *frame, 
				uint8_t *buf, int total_len, long ms,int port_num)
{
	mcast_sender *v = (mcast_sender*) data;

	int n_chunks = total_len / CHUNK_SIZE;
	int i;
	int tb = 0;
	packet_header_t header = packet_construct_header( 1 );
	frame_info_t	info;
	/* 1 = 4:2:2 planer, 0 = 4:2:0 planar */
	info.fmt = (frame->shift_v == 0 ? 1 : 0);
	info.width = frame->width;
	info.height = frame->height;
	header.timeout = ms * 10000;
//	header.timeout = 22000;
	uint8_t	chunk[PACKET_PAYLOAD_SIZE];

	for ( i = 0 ; i <= n_chunks ; i ++ )
	{
		const uint8_t *data = buf + (i * CHUNK_SIZE );
		int n;
		header.seq_num = i;
		packet_put_data( &header, &info, chunk, data );
		n = mcast_send( v, chunk, PACKET_PAYLOAD_SIZE, port_num );
		if(n <= 0)
		{
			return -1;
		}
		tb += n;
	} 

	return 1;
}

void		mcast_close_sender( void *data )
{
	mcast_sender *v = (mcast_sender*) data;

	if(v)
	{
		close(v->sock_fd);
		if(v->group) free(v->group);
		v->group = NULL;
	}
}
