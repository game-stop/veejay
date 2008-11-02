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
#include <config.h>
#include <stdint.h>
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
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>
#include <libvjmem/vjmem.h>
#include <veejay/vims.h>
#include "packet.h"
#include <libyuv/yuvconv.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

static 	void	print_error(char *msg)
{
	veejay_msg(VEEJAY_MSG_ERROR,"%s: %s\n", msg,strerror(errno));
}

typedef struct
{
	packet_header_t	hdr;
	frame_info_t	inf;
	uint8_t		*buf;
	uint16_t	*packets;
} packet_buffer_t;

static	void	*mcast_packet_buffer_new( packet_header_t *header, frame_info_t *info, uint8_t *data )
{
#ifdef STRICT_CHECKING
	assert( header->length > 0 );
#endif
	packet_buffer_t *pb = (packet_buffer_t*) vj_malloc(sizeof(packet_buffer_t));
	veejay_memcpy( &(pb->hdr), header, sizeof(packet_header_t));
	veejay_memcpy( &(pb->inf), info,   sizeof(frame_info_t));
	pb->buf = vj_malloc( sizeof(uint8_t) * CHUNK_SIZE * header->length );
	pb->packets = vj_malloc( sizeof(uint16_t) * header->length );
	veejay_memcpy( pb->buf + ( header->seq_num * CHUNK_SIZE), data + (
				sizeof(packet_header_t) + sizeof(frame_info_t)), CHUNK_SIZE );
#ifdef STRICT_CHECKING
	assert( (sizeof(packet_header_t) + sizeof(frame_info_t) + CHUNK_SIZE ) == PACKET_PAYLOAD_SIZE);
#endif
	pb->packets[ header->seq_num ] = 1;
	return (void*) pb;
}

static	void	mcast_packet_buffer_release( void *dat )
{
	packet_buffer_t *pb = (packet_buffer_t*) dat;
	if(pb)
	{
		if(pb->buf) free(pb->buf);
		if(pb->packets) free(pb->packets);
		free(pb);
	}
	pb = NULL;
}

static	int		mcast_packet_buffer_next( void *dat, packet_header_t *hdr )
{
	packet_buffer_t *pb = (packet_buffer_t*) dat;
	if( pb->hdr.usec == hdr->usec )
		return 1;
	return 0;
}

static	int		mcast_packet_buffer_full(void *dst)
{
	packet_buffer_t *pb = (packet_buffer_t*) dst;
	unsigned int i;	
	int res = 0;
	for(i = 0; i < pb->hdr.length; i ++ )
		if( pb->packets[i]) res ++;
	return ( res >= pb->hdr.length ? 1 : 0 );
}

static	void		mcast_packet_buffer_store( void *dat,packet_header_t *hdr, uint8_t *chunk )
{
	packet_buffer_t *pb = (packet_buffer_t*) dat;
	veejay_memcpy( pb->buf + (CHUNK_SIZE * hdr->seq_num ), chunk +
			( sizeof(packet_header_t) + sizeof(frame_info_t) ),
			CHUNK_SIZE );
	pb->packets[ hdr->seq_num ] = 1;
}
static	int		mcast_packet_buffer_fill( void *dat, int *packet_len, uint8_t *buf )
{	
	packet_buffer_t *pb = (packet_buffer_t*) dat;
	unsigned int i;
	unsigned int packet = 0;
	for( i = 0; i < pb->hdr.length ; i ++ )
	{
		if( pb->packets[i] )
		{
			veejay_memcpy( buf + (CHUNK_SIZE * i ), pb->buf + (CHUNK_SIZE *i), CHUNK_SIZE );
			packet++;
		}
	}
	*packet_len = pb->inf.len;

	return packet;
}

mcast_receiver	*mcast_new_receiver( const char *group_name, int port )
{
	mcast_receiver *v = (mcast_receiver*) vj_calloc(sizeof(mcast_receiver));
	if(!v) return NULL;
	int	on = 1;
	struct ip_mreq mcast_req;
	
	veejay_memset( &mcast_req, 0, sizeof(mcast_req ));
	veejay_memset( &(v->addr), 0, sizeof(struct sockaddr_in) );
	v->group = (char*) strdup( group_name );
	v->port = port;

	v->sock_fd	= socket( AF_INET, SOCK_DGRAM, 0 );
	if(v->sock_fd < 0)
	{
		veejay_msg(0, "Unable to get a datagram socket: %s", strerror(errno));
		if(v->group) free(v->group);
		if(v) free(v);
		return NULL;
	}

#ifdef SO_REUSEADDR
	if ( setsockopt( v->sock_fd, SOL_SOCKET, SO_REUSEADDR, &on,sizeof(on))<0)
	{
		veejay_msg(0, "Unable to set SO_REUSEADDR: %s", strerror(errno));
		if(v->group) free(v->group);
		if(v) free(v);
		return NULL;
	}
#endif
#ifdef SO_REUSEPORT
	if ( setsockopt( v->sock_fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))<0)
	{
		veejay_msg(0, "Unable to set SO_REUSEPORT: %s", strerror(errno));
		if(v->group) free(v->group);
		if(v) free(v);
		return NULL;
	}
#endif

	v->addr.sin_addr.s_addr = htonl( INADDR_ANY );
	v->addr.sin_port = htons( v->port );

	if( bind( v->sock_fd, (struct sockaddr*) &(v->addr), sizeof(struct sockaddr_in))<0)
	{
		veejay_msg(0, "Unable to bind to port %d : %s", v->port, strerror(errno));
		if(v->group) free(v->group);
		if(v) free(v);
		return NULL;
	}
	mcast_req.imr_multiaddr.s_addr = inet_addr( v->group );
	mcast_req.imr_interface.s_addr = htonl( INADDR_ANY );
	if( setsockopt( v->sock_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mcast_req,
		sizeof(mcast_req) ) < 0 )
	{
		veejay_msg(0, "Unable to join multicast group %s (port=%d)",group_name,port, strerror(errno));
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
			veejay_msg(0, "Invalid host '%s'", hostname );
                        return 0;
                }
        }
        return 1;
}

int	mcast_poll( mcast_receiver *v )
{
#ifdef STRICT_CHECKING
	assert( v != NULL );
#endif
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
	tv.tv_usec = timeout; // 0.05 seconds
	FD_ZERO( &fds );	
	FD_SET( v->sock_fd, &fds );

	if( timeout == 0 )
		n = select( v->sock_fd + 1, &fds, NULL,NULL, NULL );
	else
		n = select( v->sock_fd + 1, &fds, 0,0, &tv );
	if(n == -1)
		veejay_msg(0, "Multicast receiver select error: %s", strerror(errno));

	if( n <= 0)
		return 0;

	return 1;
}



int	mcast_recv( mcast_receiver *v, void *buf, int len )
{
	int n = recv( v->sock_fd, buf, len, 0 );
	if ( n == -1 )
		veejay_msg(0, "Multicast receive error: %s", strerror(errno));

	return n;
}

#define dequeue_packet()\
{\
res = recv(v->sock_fd, chunk, PACKET_PAYLOAD_SIZE, 0 );\
if( res == -1)\
{\
	veejay_msg(0, "mcast receiver: %s", strerror(errno));\
	return 0;\
}\
}

int	mcast_recv_frame( mcast_receiver *v, uint8_t *linear_buf, int total_len, int cw, int ch, int cfmt,
		int *dw, int *dh, int *dfmt )
{
	uint32_t sec,usec;
	int	 i=0;
	int	 tb=0;
	uint8_t  chunk[PACKET_PAYLOAD_SIZE];
	packet_header_t header;
	frame_info_t 	info;

	veejay_memset(&header,0,sizeof(packet_header_t));

	int res = 0;
	int n_packet = 0;
	int total_recv = 0;
	int packet_len = CHUNK_SIZE;
	int nos =0;

	if( mcast_poll_timeout( v, header.timeout ) == 0 )
	{
		return 0;
	}

	packet_buffer_t *queued_packets = (packet_buffer_t*) v->next;

	while( total_recv < packet_len )
	{
		int put_data = 1;
		res = recv(v->sock_fd, chunk, PACKET_PAYLOAD_SIZE, MSG_PEEK );
		if( res <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error receiving multicast packet:%s", strerror(errno));
			return 0;
		}	
#ifdef STRICT_CHECKING
		assert( res == PACKET_PAYLOAD_SIZE );
#endif	
		packet_header_t hdr = packet_get_header( chunk );
		frame_info_t    inf;
		packet_get_info(&inf,chunk );

		if( n_packet == 0 )	// is this the first packet we get?
		{
			if( queued_packets ) // if there are queued (future) packets,
			{	
				// empty next packet buffer
				if( queued_packets->hdr.usec == hdr.usec )
				{
					n_packet = mcast_packet_buffer_fill(v->next, &packet_len, linear_buf);
					total_recv = n_packet * CHUNK_SIZE;
					veejay_memcpy(&header, &(queued_packets->hdr), sizeof(packet_header_t));
					veejay_memcpy(&info,   &(queued_packets->inf), sizeof(frame_info_t));
			//	veejay_msg(VEEJAY_MSG_DEBUG, "%s:%d dequeuing packet with timestamp %x in next buffer (ts=%x,length=%d,len=%d, packets=%d)",
			//		__FUNCTION__,__LINE__, hdr.usec, queued_packets->hdr.usec, queued_packets->hdr.length, queued_packets->inf.len, n_packet );
				}
				else
				{
					//@ there are queued packets, but not the expected ones.
					//@ destroy packet buffer
		//		veejay_msg(VEEJAY_MSG_DEBUG, "%s:%d packet with timestamp %x arrived (queued=%x, reset. grab new)",
		//			__FUNCTION__,__LINE__, hdr.usec, queued_packets->hdr.usec );
					mcast_packet_buffer_release(v->next);
					queued_packets = NULL;
					v->next = NULL;
					packet_len = info.len;
					veejay_memcpy( &header,&hdr, sizeof(packet_header_t));
					veejay_memcpy( &info, &inf, sizeof(frame_info_t));
					total_recv = 0;
				}
			}
			else
			{
		//	veejay_msg(VEEJAY_MSG_DEBUG, "%s:%d Queuing first packet %d/%d, data_len=%d",
		//		__FUNCTION__,__LINE__, n_packet, hdr.length, info.len );
				packet_len = inf.len;
				veejay_memcpy(&header,&hdr,sizeof(packet_header_t));
				veejay_memcpy(&info, &inf, sizeof(frame_info_t));
				total_recv = 0;
			}
		}


		if( header.usec != hdr.usec )
		{
			if( hdr.usec < header.usec )
			{
				put_data = 0;
		//		veejay_msg(VEEJAY_MSG_DEBUG, "%s:%d dropped packet (too old timestamp %x)", __FUNCTION__,__LINE__,header.usec);
			}
			else
			{
				//@ its newer!
				//
				if(!v->next) // nothing stored yet
				{
					v->next = mcast_packet_buffer_new( &hdr, &inf, chunk );
	
				//	veejay_msg(VEEJAY_MSG_DEBUG,"%s:%d Stored packet with timestamp %x (processing %x)",
				//		__FUNCTION__,__LINE__, hdr.usec, header.usec );
	
				}
				else
				{
					// store packet if next buffer has identical timestamp
					if( mcast_packet_buffer_next( v->next, &hdr ) )
					{
			//		veejay_msg(VEEJAY_MSG_DEBUG, "%s:%d packet buffer STORE future frame (ts=%x)", __FUNCTION__,__LINE__, hdr.usec );
						mcast_packet_buffer_store( v->next, &hdr,chunk );
						put_data = 0;
					}
					else
					{
						// release packet buffer and start queueing new frames only
				//		veejay_msg(VEEJAY_MSG_DEBUG, "%s:%d packet buffer release, storing newest packets",__FUNCTION__,__LINE__ );

						if( mcast_packet_buffer_full( v->next ))
						{
							n_packet = mcast_packet_buffer_fill(v->next, &packet_len, linear_buf);
							total_recv = n_packet * CHUNK_SIZE;
							return packet_len;
						}

						mcast_packet_buffer_release(v->next);
						v->next = NULL;
						total_recv = 0; n_packet = 0; packet_len = inf.len;
						veejay_memcpy(&header,&hdr,sizeof(packet_header_t));
						put_data = 1;
					}	

				}
			}
		}

		dequeue_packet();


		if( put_data )
		{
			uint8_t *dst;
			dst = linear_buf + (CHUNK_SIZE  * hdr.seq_num );
			packet_get_data( &hdr, chunk, dst );
			total_recv += CHUNK_SIZE;
			n_packet ++;
		}

		if( n_packet >= header.length )
		{
//			veejay_msg(VEEJAY_MSG_DEBUG, "%s:%d Have full frame",__FUNCTION__,__LINE__);
			break;
		}

	}

#ifdef STRICT_CHECKING
	assert( total_recv >=  packet_len );
#endif
	*dw = info.width;
	*dh = info.height;
	*dfmt = info.fmt;
		
	return packet_len;
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
