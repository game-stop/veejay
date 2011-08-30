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

#define TIMEOUT 3

int			sock_t_connect_and_send_http( vj_sock_t *s, char *host, int port, char *buf, int buf_len )
{
	s->he = gethostbyname( host );
	if(s->he==NULL)
		return 0;
	s->sock_fd = socket( AF_INET, SOCK_STREAM , 0);
	if(s->sock_fd < 0)
	{
		return 0;
	}
	s->port_num = port;
	s->addr.sin_family = AF_INET;
	s->addr.sin_port   = htons( port );
	s->addr.sin_addr   = *( (struct in_addr*) s->he->h_addr );	
	if( connect( s->sock_fd, (struct sockaddr*) &s->addr,
			sizeof( struct sockaddr )) == -1 )
	{
		return 0;
	}

	struct sockaddr_in sinfo;
	socklen_t sinfolen=0;
	char server_name[1024];
	if( getsockname(s->sock_fd,(struct sockaddr*) &sinfo,&sinfolen)==0) {
		char *tmp = inet_ntoa( sinfo.sin_addr );
		strncpy( server_name, tmp, 1024);
	} else {
		return 0;
	}

	int len = strlen(server_name) + 128 + buf_len;
	char *msg = (char*) malloc(sizeof(char) * len );
	struct utsname name;
	if( uname(&name) == -1 ) {
		snprintf(msg,len,"%s%s/veejay-%s\n\n",buf,server_name,PACKAGE_VERSION);
	} else {
		snprintf(msg,len,"%s%s/veejay-%s/%s-%s\n\n",buf,server_name,PACKAGE_VERSION,name.sysname,name.release );
	}
	int msg_len = strlen(msg);
	int n = send(s->sock_fd,msg,msg_len, 0 );
	free(msg);
	
	if( n == -1 )
	{
		return 0;
	}

	return n;
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

	veejay_msg(VEEJAY_MSG_DEBUG, "Connected to host '%s' port %d", host,port );

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
	memset( &no_wait, 0, sizeof(no_wait) );

	FD_ZERO( &(s->rds) );
	FD_ZERO( &(s->wds) );

	FD_SET( s->sock_fd, &(s->rds) );
	FD_SET( s->sock_fd, &(s->wds) );

	status = select( s->sock_fd + 1, &(s->rds),&(s->wds), 0, &no_wait );
	
	if( status == -1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to poll socket for immediate read: %s", strerror(errno));
		return -1;
	}
//	if( status == 0 )
//		return -1;

	if( sock_t_rds_isset( s ) )
		return 1;

	return 0;
}

/*
static		int	timed_recv( int fd, void *buf, const int len, int timeout )
{
	fd_set fds;
	int	n;

	struct timeval tv;
	memset( &tv, 0,sizeof(timeval));
	FD_ZERO(&fds);
	FD_SET( fd,&fds );

	tv.tv_sec  = TIMEOUT;

	n	  = select( fd + 1, &fds, NULL, NULL, &tv );
	if( n == 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "\tsocket %x :: requested %d bytes", fd, len );
	}

	if( n == -1 )
		return -1;

	if( n == 0 )
		return -5;

	return recv( fd, buf, len, 0 );
}*/

void			sock_t_set_timeout( vj_sock_t *s, int t )
{
	int opt = t;
	setsockopt( s->sock_fd, SOL_SOCKET, SO_SNDTIMEO, (char*) &opt, sizeof(int));
	setsockopt( s->sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char*) &opt, sizeof(int));
}

int			sock_t_recv( vj_sock_t *s, void *dst, int len )
{
	int done = 0;
	int bytes_left = s->recv_size;
	int n;

	if( len < bytes_left )
		bytes_left = len;

	while( done < len )
	{	
		//@ setup socket with SO_RCVTIMEO
		n = recv( s->sock_fd, dst+done,bytes_left, 0 );
		if ( n < 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
			return -1;
		} else if ( n == 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "Remote closed connection.");
			return -1;
		}

		done += n;

		if( (len-done) < s->recv_size )
			bytes_left = len - done;
	}
	return done;
}

int			sock_t_send( vj_sock_t *s, unsigned char *buf, int len )
{
	int n; 
#ifdef STRICT_CHECKING
	assert( buf != NULL );
#endif
/*	if( sock_t_wds_isset( s ) == 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "%s", __FUNCTION__);
		return 0;
	}*/

	int length = len;
	int bw = 0;
	int done = 0;
	while( length > 0 ) {
		bw = length;
		n = send( s->sock_fd, buf, length , 0 );
		if( n == -1 ) {
#ifdef STRICT_CHECKING
			veejay_msg(0, "Error sending buffer:%s",strerror(errno));
#endif
			return -1;
		}
		if( n == 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "Remote closed connection.");
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
#ifdef STRICT_CHECKING
	assert( buf != NULL );
#endif

	int length = len;
	int bw = 0;
	int done = 0;
	while( length > 0 ) {
		bw = length;
		n = send( fd, buf, length , 0 );
		if( n == -1 ) {
#ifdef STRICT_CHECKING
			veejay_msg(0, "Error sending buffer:%s", strerror(errno));
			return -1;
#endif
		}
		if( n == 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "Remote closed connection.");
			return 0;
		}
		buf += n;
		length -= n;
		done += n;
	}
	return done;

}

void			sock_t_close( vj_sock_t *s )
{
	if(s)
	{
		close(s->sock_fd);
		s->sock_fd = 0;
		FD_ZERO(&(s->rds));
		FD_ZERO(&(s->wds));
	}
}
