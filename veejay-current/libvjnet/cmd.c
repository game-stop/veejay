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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "cmd.h"
#include <libvjmsg/vj-common.h>

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
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
		return 0;
	}
	s->port_num = port;
	s->addr.sin_family = AF_INET;
	s->addr.sin_port   = htons( port );
	s->addr.sin_addr   = *( (struct in_addr*) s->he->h_addr );	

	if( connect( s->sock_fd, (struct sockaddr*) &s->addr,
			sizeof( struct sockaddr )) == -1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
		return 0;
	}
	return 1;
}

int                     sock_t_poll_w( vj_sock_t *s )
{
        int     status;
        fd_set  fds;
        struct timeval no_wait;
        memset( &no_wait, 0, sizeof(no_wait) );

        FD_ZERO( &fds );
        FD_SET( s->sock_fd, &fds );

        status = select( s->sock_fd + 1, 0,&fds, 0, &no_wait );
        if( status < 0 )
        {
                veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
                return 0;
        }
	
        if( FD_ISSET( s->sock_fd, &fds ) )
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
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
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
	int n = recv( s->sock_fd, dst, len, MSG_WAITALL );
	if(n==-1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
		return 0;
	}
	return n;
}

int			sock_t_recv( vj_sock_t *s, void *dst, int len )
{
	int n = recv( s->sock_fd, dst, len , 0  );
	if(n==-1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
		return 0;
	}
	return n;
}

int			sock_t_send( vj_sock_t *s, unsigned char *buf, int len )
{
	int n; 
	if(!buf) return 0;

	n = send( s->sock_fd, buf, len , 0 );
	if(n == -1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "send: %s", strerror(errno));
		return 0;
	}
	return n;
}

void			sock_t_close( vj_sock_t *s )
{
	if(s)
	{
		close(s->sock_fd);
	}
}
