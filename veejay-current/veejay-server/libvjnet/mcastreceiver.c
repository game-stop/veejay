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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
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

typedef struct
{
	packet_header_t	hdr;
	frame_info_t	inf;
	uint8_t		*buf;
	int		len;
	int		count;
	int		rdy;
} packet_buffer_t;

#define PACKET_SLOTS 3
typedef struct
{
	packet_buffer_t	**slot;
	int		 in_slot;
	long		 last;
} packet_slot_t;

mcast_receiver	*mcast_new_receiver( const char *group_name, int port )
{
	mcast_receiver *v = (mcast_receiver*) vj_calloc(sizeof(mcast_receiver));
	if(!v) return NULL;
	int	on = 1;
	struct ip_mreq mcast_req;
	int i;
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

	packet_slot_t *q = (packet_slot_t*) vj_calloc(sizeof(packet_slot_t));
	q->slot          = (packet_buffer_t**) vj_calloc(sizeof(packet_buffer_t*) * PACKET_SLOTS );
	for( i = 0; i < PACKET_SLOTS ; i ++ )
		q->slot[i] = (packet_buffer_t*) vj_calloc(sizeof(packet_buffer_t));
	v->next = (void*)q;

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
	fd_set fds;
	struct timeval tv;
	memset( &tv, 0, sizeof(tv) );
	FD_ZERO( &fds );	
	FD_SET( v->sock_fd, &fds );

	if( select( v->sock_fd + 1, &fds, 0,0, &tv ) <= 0 )
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

int	mcast_recv_packet_frame( mcast_receiver *v )
{
	uint8_t  chunk[PACKET_PAYLOAD_SIZE];
	packet_slot_t *q = (packet_slot_t*) v->next;

	packet_header_t	hdr;
	frame_info_t	inf;

	int res = recv(v->sock_fd, chunk, PACKET_PAYLOAD_SIZE, 0 );
        if( res <= 0 )
        {
		if(res == - 1)
               	 veejay_msg(VEEJAY_MSG_ERROR, "Error receiving multicast packet:%s", strerror(errno));
		
                return 0;
        }
        hdr = packet_get_header(chunk);
        packet_get_info( &inf, chunk );

	//@ choose slot to fill
	int i;
	int d_slot = -1;

	for(i = 0; i < PACKET_SLOTS; i ++ ) 
	{
		if( q->slot[i]->hdr.usec == hdr.usec ) {
			d_slot = i;
			break;
		}
	}

	if( d_slot == -1) {
		//@ find slot with count == 0 (unused)
		for(i = 0; i < PACKET_SLOTS; i ++ ) {
			if(q->slot[i]->count == 0 ) {
				d_slot = i;
				break;
			}
		}
	}

	//@ no slots available
	if( d_slot == -1) {
		veejay_msg(VEEJAY_MSG_WARNING, "All packet slots in use, cannot keep pace! Dropping oldest in queue.");
		//@ drop oldest packet in slot
		long oldest = LONG_MAX;
		int  o      = 0;
		for(i = 0; i < PACKET_SLOTS; i ++ ) {
			if(q->slot[i]->hdr.usec < oldest ) {
				o = i;
				oldest = q->slot[i]->hdr.usec;
			}
		}

		d_slot = o;
		free(q->slot[d_slot]->buf);
		q->slot[d_slot]->buf = NULL;
		q->slot[d_slot]->count = 0;
		veejay_memset( &(q->slot[d_slot]->hdr), 0,sizeof(packet_header_t));
		veejay_memset( &(q->slot[d_slot]->inf), 0,sizeof(frame_info_t));
		q->slot[d_slot]->rdy = 0;
	}

	//@ destination slot
	packet_buffer_t	*pb = q->slot[d_slot];
	if(pb->buf == NULL) { //@ allocate buffer if needed
		pb->buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * inf.width * inf.height * 3);
	}

	pb->len = inf.len;
	uint8_t *dst = pb->buf + (CHUNK_SIZE * hdr.seq_num );
        packet_get_data( &hdr, chunk, dst );
	pb->count ++;

	//@ save info/hdr
	veejay_memcpy( &(pb->hdr), &hdr, sizeof(packet_header_t));
	veejay_memcpy( &(pb->inf), &inf, sizeof(frame_info_t));

	if( pb->count >= hdr.length )
	{
		pb->rdy = 1;
		q->last = hdr.usec;
		return 2;
	}

	return 1;
}

uint8_t *mcast_recv_frame( mcast_receiver *v, int *dw, int *dh, int *dfmt, int *len )
{
	packet_slot_t *q = (packet_slot_t*) v->next;
	int i,n;
	for(i = 0; i < PACKET_SLOTS; i ++ ) 
	{
		//@ find rdy frames or too-old-frames and free them
		if( q->slot[i]->rdy == 1 || q->slot[i]->hdr.usec < q->last ) {
			free(q->slot[i]->buf);
			q->slot[i]->buf = NULL;
			q->slot[i]->count = 0;
			veejay_memset( &(q->slot[i]->hdr), 0,sizeof(packet_header_t));
			veejay_memset( &(q->slot[i]->inf), 0,sizeof(frame_info_t));
			q->slot[i]->rdy = 0;
		}
	}

	//@ is there something todo
//	if( mcast_poll_timeout( v, 1000 ) == 0 )
//		return NULL;

	while( (n = mcast_recv_packet_frame(v) ) )
	{
		if( n == 2 ) {
			break; //@ full frame
		}
	}
		
	int d_slot = -1;
	//@ find packet buffer with complete frame
	int full_frame = 0;
	long t1 = 0;
	for(i = 0; i < PACKET_SLOTS; i ++ ) 
	{
		if( q->slot[i]->rdy == 1 ) {
			full_frame = 1;
			t1 = q->slot[i]->hdr.usec;
			d_slot = i;
			break;
		}
	}

	//@ find newer packet buffer with complete frame
	for(i = 0; i < PACKET_SLOTS; i ++ ) 
	{
		if( q->slot[i]->rdy == 1 && q->slot[i]->hdr.usec > t1) {
			full_frame = 1;
			d_slot = i;
		}
	}
	/*
	*/
	//@ return newest full frame
	if( full_frame ) {
		packet_buffer_t *pb = q->slot[d_slot];
		*dw = pb->inf.width;
		*dh = pb->inf.height;
		*dfmt=pb->inf.fmt;
		*len =pb->len;
		return pb->buf;
	}

	return NULL;
}


void	mcast_close_receiver( mcast_receiver *v )
{
	if(v)
	{
		close(v->sock_fd);
		if(v->group) free(v->group);
		v->group = NULL;
		int i;
		packet_slot_t *q = (packet_slot_t*) v->next;
		if( q ) {
		  for( i = 0; i < PACKET_SLOTS; i ++ ){
			packet_buffer_t *r = q->slot[i];
			if( r->buf ) {
				free(r->buf);
			}
			free(r);
		  }
		  free(q->slot);
		  free(q);
		}
	}
}
