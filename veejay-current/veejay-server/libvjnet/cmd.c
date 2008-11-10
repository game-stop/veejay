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
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
vj_sock_t	*alloc_sock_t(void)
{
	vj_sock_t *s = (vj_sock_t*) malloc(sizeof(vj_sock_t));
	if(!s) return NULL;
	return s;
}

void		sock_t_free(vj_sock_t *s )
{
	if(s) free(s);
}

int			sock_t_connect( vj_sock_t *s, char *host, int port )
{
	s->he = gethostbyname( host );
	if(s->he==NULL)
		return 0;
	s->sock_fd = socket( AF_INET, SOCK_STREAM , 0);
	if(s->sock_fd < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Socket error with Veejay host %s:%d %s ", host,port,strerror(errno));
		return 0;
	}
	s->port_num = port;
	s->addr.sin_family = AF_INET;
	s->addr.sin_port   = htons( port );
	s->addr.sin_addr   = *( (struct in_addr*) s->he->h_addr );	

	if( connect( s->sock_fd, (struct sockaddr*) &s->addr,
			sizeof( struct sockaddr )) == -1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Connection error with Veejay host %s:%d %s",
				host, port, strerror(errno));
		return 0;
	}
	unsigned int tmp = sizeof(int);
	if( getsockopt( s->sock_fd , SOL_SOCKET, SO_SNDBUF, (unsigned char*) &(s->send_size), &tmp) < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to get buffer size for output: %s", strerror(errno));
		return 0;
	}
	if( getsockopt( s->sock_fd, SOL_SOCKET, SO_RCVBUF, (unsigned char*) &(s->recv_size), &tmp) < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to get buffer size for input: %s", strerror(errno));
		return 0;
	}

	return 1;
}

int			sock_t_poll_w(vj_sock_t *s )
{
	int	status;
	fd_set	fds;
	struct timeval no_wait;
	memset( &no_wait, 0, sizeof(no_wait) );

	FD_ZERO( &fds );
	FD_SET( s->sock_fd, &fds );

	status = select( s->sock_fd + 1,NULL, &fds, NULL, &no_wait );
	if( status < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to poll socket for immediate write: %s", strerror(errno));
		return 0;
	}
	if( status == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Timeout occured");
		return 0;
	}

	if( FD_ISSET( s->sock_fd, &fds ))
	{
		return 1;
	}

	return 0;
}


int			sock_t_poll( vj_sock_t *s )
{
	int	status;
	fd_set	fds;
	struct timeval no_wait;
	memset( &no_wait, 0, sizeof(no_wait) );

	FD_ZERO( &fds );
	FD_SET( s->sock_fd, &fds );

	status = select( s->sock_fd + 1, &fds, 0, 0, &no_wait );
	if( status < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to poll socket for immediate read: %s", strerror(errno));
		return 0;
	}
	if( FD_ISSET( s->sock_fd, &fds ) )
	{
		return 1;
	}
	return 0;
}

int			sock_t_recv_w( vj_sock_t *s, void *dst, int len )
{
	int n = 0;
	if( len < s->recv_size )
	{
		n = recv( s->sock_fd, dst, len, MSG_WAITALL );
		if(n==-1)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
			return -1;
		}
		return n;
	}
	else
	{
		int done = 0;
		int bytes_left = s->recv_size;

		while( done < len )
		{
			n = recv( s->sock_fd, dst + done,bytes_left,MSG_WAITALL );
			if( n == -1)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "%s",strerror(errno));
				return -1;
			}
			done += n;
			
			if( (len-done) < s->recv_size)
				bytes_left = len - done;
		}
		return done;
	}
	return 0;
}

int			sock_t_recv( vj_sock_t *s, void *dst, int len )
{
	int n = recv( s->sock_fd, dst, len , 0  );
	if(n==-1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
		return -1;
	}
	return n;
}

int			sock_t_send( vj_sock_t *s, unsigned char *buf, int len )
{
	int n; 
#ifdef STRICT_CHECKING
	assert( buf != NULL );
#endif
	//@ send in reasonable chunks
	if( len < s->send_size )
	{
		n = send(s->sock_fd,buf,len, 0 );
		if( n == -1 )
		{
			veejay_msg(0, "Send error: %s",strerror(errno));
			veejay_msg(0, "\t[%s], %d bytes", buf,len );
			return 0;
		}
		return n;
	}
	else
	{	
		int done = 0;
		int bs   = s->send_size;
		while( done < len )
		{
			n = send( s->sock_fd, buf + done, bs , 0 );
			if(n == -1)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "TCP send error: %s", strerror(errno));
				return 0;
			}
			done += n;
		}
		return done;
	}
	return 0;
}

int			sock_t_send_fd( int fd, int send_size, unsigned char *buf, int len )
{
	int n; 
#ifdef STRICT_CHECKING
	assert( buf != NULL );
#endif
	
	//@ send in reasonable chunks
	if( len < send_size )
	{
		n = send(fd,buf,len, 0 );
		if( n == -1 )
		{
			veejay_msg(0, "Send error: %s",strerror(errno));
			return 0;
		}
		return n;
	}
	else
	{	
		int done = 0;
		int bytes_left = len;
		while( done < len )
		{
			n = send( fd, buf + done, bytes_left , 0 );
			if(n == -1)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "TCP send error: %s", strerror(errno));
				return 0;
			}
			bytes_left -= n;
			done += n;
		}
#ifdef STRICT_CHECKING
		assert( done == len );
#endif
		return done;
	}
	return 0;
}

void			sock_t_close( vj_sock_t *s )
{
	if(s)
	{
		close(s->sock_fd);
	}
}
