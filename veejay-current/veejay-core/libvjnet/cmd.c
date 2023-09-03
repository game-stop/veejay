/* vjnet - low level network I/O for VeeJay
 *
 *           (C) 2005 Niels Elburg <nwelburg@gmail.com> 
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "cmd.h"
#include <libvjmsg/vj-msg.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <netinet/in.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static int sock_connect(const char *name, int port) {

    char service[5];
    snprintf(service,sizeof(service), "%d", port );

    int sock_fd;
    struct addrinfo hints, *servinfo, *p;

    memset( &hints, 0, sizeof(hints) );

    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM;

    if( getaddrinfo( name, service, &hints, &servinfo ) != 0 ) {
        veejay_msg(0, "Failed to resolve %s:%d :%s", name, port, strerror(errno));
        return -1;
    }

    for( p = servinfo; p != NULL; p = p->ai_next ) {
        if (( sock_fd = socket( p->ai_family, p->ai_socktype, p->ai_protocol )) == -1 ) {
            veejay_msg(VEEJAY_MSG_DEBUG, "Socket error: %s", strerror(errno));
            continue;
        }

        if ( connect( sock_fd, p->ai_addr, p->ai_addrlen ) == -1 ) {
            veejay_msg(VEEJAY_MSG_DEBUG, "Connect failed to %s:%d :%s", name, port, strerror(errno));
            close(sock_fd);
            continue;
        }

        break;
    }

    if( p == NULL ) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to connect to %s:%d :%s", name, port, strerror(errno));
        return -1;
    }

    veejay_msg(VEEJAY_MSG_DEBUG, "Established connection with %s:%d", name, port );

    freeaddrinfo(servinfo);

    return sock_fd;
}

vj_sock_t	*alloc_sock_t(void)
{
	vj_sock_t *s = (vj_sock_t*) malloc(sizeof(vj_sock_t));
	if(!s) return NULL;
	memset( s, 0, sizeof(vj_sock_t));
	return s;
}

void		sock_t_free(vj_sock_t *s )
{
	if(s) free(s);
}

int			sock_t_connect( vj_sock_t *s, char *host, int port )
{
	s->sock_fd = sock_connect( host, port );
	if(s->sock_fd < 0)
		return 0;
	
    unsigned int tmp = sizeof(int);
	if( getsockopt( s->sock_fd , SOL_SOCKET, SO_SNDBUF, (unsigned char*) &(s->send_size), &tmp) < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to get buffer size for output: %s", strerror(errno));
        close( s->sock_fd );
		return 0;
	}
	if( getsockopt( s->sock_fd, SOL_SOCKET, SO_RCVBUF, (unsigned char*) &(s->recv_size), &tmp) < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to get buffer size for input: %s", strerror(errno));
        close( s->sock_fd );
		return 0;
	}

#ifdef SO_NOSIGPIPE
    int opt = 1;
    if( setsockopt( s->sock_fd, SOL_SOCKET, SO_NOSIGPIPE, (void*) &opt, sizeof(int)) < 0 ) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to set SO_NOSIGPIPE: %s", strerror(errno));
        close(s->sock_fd);
        return 0;
    }
#endif

	veejay_msg(VEEJAY_MSG_DEBUG, "Connected to host '%s' port %d, fd %d", host,port,s->sock_fd );
	veejay_msg(VEEJAY_MSG_DEBUG, "Receive buffer size is %d bytes, send buffer size is %d bytes", s->recv_size, s->send_size );

	return 1;
}

int			sock_t_wds_isset( vj_sock_t *s ) {
	return FD_ISSET( s->sock_fd, &(s->wds));
}

int			sock_t_rds_isset( vj_sock_t *s ) {
	return FD_ISSET( s->sock_fd, &(s->rds) );
}

int			sock_t_poll( vj_sock_t *s )
{
	int	status;
	struct timeval no_wait;
	//memset( &no_wait, 0, sizeof(no_wait) );
    no_wait.tv_sec = 0;
    no_wait.tv_usec = 0;
        
	FD_ZERO( &(s->rds) );
	FD_ZERO( &(s->wds) );

	FD_SET( s->sock_fd, &(s->rds) );
	FD_SET( s->sock_fd, &(s->wds) );

	status = select( s->sock_fd + 1, &(s->rds),&(s->wds), 0, &no_wait );
	
	if( status < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to poll socket for immediate read: %s", strerror(errno));
		return -1;
	}

	if( sock_t_rds_isset( s ) )
		return 1;

	return 0;
}

int			sock_t_recv( vj_sock_t *s, void *dst, int len )
{
	int bytes_left = len;
	int n;
	int bytes_done = 0;
	char *addr = (char*) dst;

	while( bytes_left > 0 )
	{	
sock_t_recv_lbl:		
		n = recv( s->sock_fd, addr + bytes_done, bytes_left, MSG_WAITALL );
		if ( n <= 0 ) {
			if( n == -1 ) {
				if( errno == EAGAIN ) { 
					veejay_msg(VEEJAY_MSG_ERROR, "Strange things happen in strange places. EAGAIN but socket is MSG_WAITALL");
					goto sock_t_recv_lbl;

				}
				veejay_msg(0, "Error while receiving from network: %s", strerror(errno));
			} 
			
			return n;
		} 
	
		bytes_done += n;
		bytes_left -= n;
	}

	return bytes_done;
}

int			sock_t_send( vj_sock_t *s, unsigned char *buf, int len )
{
	int n; 
	int length = len;
	int done = 0;
	while( length > 0 ) {
		n = send( s->sock_fd, buf, length , MSG_NOSIGNAL );
		if( n == -1 ) {
			return -1;
		}
		if( n == 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "Remote closed connection");
			return -1;
		}
		buf += n;
		length -= n;
		done += n;
	}
	return done;
}

int			sock_t_send_fd( int fd, int send_size, unsigned char *buf, int len )
{
	int n; 
	unsigned int length = len;
	unsigned int done = 0;
	unsigned char *ptr = buf;
	while( length > 0 ) {
		n = send( fd, ptr, length , MSG_NOSIGNAL );
		if( n == -1 ) {
            if(errno == EPIPE ) {
                veejay_msg(VEEJAY_MSG_DEBUG, "The local end has been shut down,someone just hang up");
                return -1;
            }
			veejay_msg(0, "Error sending buffer:%s", strerror(errno));
			return -1;
		}

		if( n == 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "Remote closed connection");
			return -1;
		}

		ptr += n; //@ advance ptr by bytes send
		length -= n; //@ decrement length by bytes send
		done += n; //@ keep count of bytes done
	}
	return done;
}

void			sock_t_close( vj_sock_t *s )
{
	if(s)
	{
		if( s->sock_fd ) {
			close(s->sock_fd);
			s->sock_fd = -1;
		}

		FD_ZERO(&(s->rds));
		FD_ZERO(&(s->wds));
	}
}
