/* vjnet - low level network I/O for VeeJay
 *
 *           (C) 2005-2007 Niels Elburg <nelburg@looze.net> 
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
#include <libyuv/yuvconv.h>
#include <veejay/vj-global.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

typedef struct
{
	uint8_t *packet;
	uint32_t *len;
	packet_header_t hdr;
	frame_info_t    inf;
	uint32_t      packets;
	uint32_t	length;
} frame_next_t;

static 	void	print_error(char *msg)
{
	veejay_msg(VEEJAY_MSG_ERROR,"%s: %s\n", msg,strerror(errno));
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
	tv.tv_usec = timeout; // 0.05 seconds
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

int	mcast_store_next_packet( mcast_receiver *v , packet_header_t *h, frame_info_t *i, uint8_t *payload)
{
	uint8_t *chunk = payload + (sizeof(packet_header_t) + sizeof(frame_info_t));
	if(v->next == NULL )
	  v->next = (void*) vj_calloc(sizeof(frame_next_t));
        frame_next_t *n = (frame_next_t*) v->next;
	int res = 1;
	if(n->packet == NULL)
	{
	 	n->packet = vj_malloc( CHUNK_SIZE + ( h->length * CHUNK_SIZE) );
		n->length = h->length;
		veejay_memcpy( &(n->hdr), h, sizeof(packet_header_t));
		veejay_memcpy( &(n->inf), i, sizeof(frame_info_t));
	}
	else
	{
		//@ must have same timestamp
		if( h->usec != n->hdr.usec )
		{
			veejay_msg(0,"Cannot keep pace with sending veejay? Next frame:[tdiff=%d], In frame: [tdiff=%d], have %d/%d",
				n->hdr.usec, h->usec, n->packets,h->length );
			return 0;
		}

	}
	

	//@ store compressed data
	veejay_memcpy( n->packet + (CHUNK_SIZE * h->seq_num), chunk, CHUNK_SIZE);
	n->packets ++;

	if( n->packets >= n->length )
		res = 2;

	return res;
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

	//@ there is a packet, get it.

/*	if(v->next)
	{	
		frame_next_t *pn = (frame_next_t*) v->next;
		packet_len = pn->inf.len;
		total_recv = (pn->packets * CHUNK_SIZE);
		veejay_memcpy( &header, &(pn->hdr), sizeof(packet_header_t));
		n_packet = pn->packets;	
		nos = 1;
	}
*/

	while( total_recv < packet_len )
	{
		int put_data = 1;
		res = recv(v->sock_fd, chunk, PACKET_PAYLOAD_SIZE, 0 );
		if( res <= 0 )
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Nothing there to read!");
			return 0;
		}	
	
		packet_header_t hdr = packet_get_header( chunk );
//		packet_dump_header(&hdr);
		packet_get_info(&info,chunk );
	
		//@ do the first packet
/*if( n_packet==0 )
		{
#ifdef STRICT_CHECKING
			assert( hdr.flag == 1 );
#endif
			*dw = info.width;
			*dh = info.height;
			*dfmt = info.fmt;
			packet_len = info.len;
			veejay_memcpy( &header,&hdr, sizeof(packet_header_t));
		}
		else
		{
			//@ more packets follow
			if( hdr.usec != header.usec )
			{
				//@ this packet is older!
				if( hdr.usec < header.usec )
				{
					//@ check for rollover
					if(hdr.usec <= 1 && header.usec >= 0xffff )
					{
					  veejay_msg(0, "ROLLOVER INTEGER");
				//	  header.usec = 0;
				//	  if( hdr.usec <= (header.usec + 1 ) )
				//	    mcast_store_next_packet( v, &hdr,&info,chunk);
					}
				
					put_data = 0;		
					veejay_msg(0, "Dropped old packet: time=%d, current=%d", hdr.usec, header.usec);
				}
				else
				{ 
					veejay_msg(0, "DROP OLD header %d, take %d", header.usec, hdr.usec );
					packet_len = info

					


					if(!nos)
					{
						int mr = mcast_store_next_packet(v, &hdr, &info, chunk );
						if(mr <= 0)
						{
							packet_len = info.len;
							veejay_memcpy( &header, &hdr, sizeof(packet_header_t));
							n_packet = 0;
							put_data = 1;
							veejay_msg(0, "Out of Pace, take newest packets");
						}
						else
						{
							if( mr == 2 )
							{
							uint8_t *dst = linear_buf;
							frame_next_t *pn = v->next;
veejay_msg(0, "NEWEST BUFFER FULL, DROP TIMECODE %d, continue with %d", header.usec, pn->hdr.usec);		
			*dw = pn->inf.width;
			*dh = pn->inf.height;
			*dfmt = pn->inf.fmt;
		
							veejay_memcpy( dst, pn->packet, pn->inf.len );
							free(pn->packet);
							free(pn);
							v->next=NULL;
							return pn->inf.len;
							}
							else
								put_data = 0;
						}
					}
				}
			}
		}
		*/

		if( n_packet == 0 )
		{
			packet_len = info.len;
			veejay_memcpy(&header,&hdr,sizeof(packet_header_t));
			total_recv = 0;
		}		

		if( header.usec != hdr.usec )
		{
			if( hdr.usec < header.usec )
				put_data = 0;
			else
			{
				total_recv = 0;
				n_packet = 0;
				packet_len = info.len;
				veejay_memcpy( &header,&hdr,sizeof(packet_header_t));
			}
		}


		if( put_data )
		{
			uint8_t *dst;
			dst = linear_buf + (CHUNK_SIZE  * hdr.seq_num );
			packet_get_data( &hdr, chunk, dst );
			total_recv += CHUNK_SIZE;
		}

		if( n_packet >= hdr.length )
		{
			break;
		}

		if(put_data)
			n_packet ++;

	}

#ifdef STRICT_CHECKING
	assert( total_recv >=  packet_len );
#endif
/*
	if(nos)
	{
		frame_next_t *pn = v->next;
		free(pn->packet);
		free(pn);
		v->next = NULL;
	}*/
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
		if(v->next)
		{
			frame_next_t *pn = v->next;
			free(pn->packet);
			free(pn);
		}
		v->next = NULL;
	}
}
