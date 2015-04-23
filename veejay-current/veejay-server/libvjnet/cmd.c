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

struct host_list {
  struct hostent hostent;
  char h_addr_space[1024]; 
};

static int ref_count = 0;
static pthread_key_t ghbn_key;
static pthread_once_t ghbn_key_once = PTHREAD_ONCE_INIT;

static void ghbn_cleanup(void *data) {
  struct host_list *current = (struct host_list *) data;
  ref_count--;
  free(current);
}

static void create_ghbn_key() {
  pthread_key_create(&ghbn_key, ghbn_cleanup);
}

struct hostent *sock_gethostbyname(const char *name) {
	struct hostent *result;
	int local_errno;

  	pthread_once(&ghbn_key_once, create_ghbn_key);

	struct host_list *current = (struct host_list *) pthread_getspecific(ghbn_key);
	
	if (!current) {
 	   	current = (struct host_list *) calloc(1, sizeof(struct host_list));
 		current->hostent.h_name = "busy";
	    	ref_count++;
   		pthread_setspecific(ghbn_key, current);
	}
  
	if (gethostbyname_r(name, &(current->hostent), current->h_addr_space,sizeof(current->h_addr_space),&result, &local_errno)) {
		h_errno = local_errno;
	}
	return result;
}

vj_sock_t	*alloc_sock_t(void)
{
	vj_sock_t *s = (vj_sock_t*) malloc(sizeof(vj_sock_t));
	memset( s, 0, sizeof(vj_sock_t));
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
	s->he = sock_gethostbyname( host );
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
	s->he = sock_gethostbyname( host );
	
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
	memset( &no_wait, 0, sizeof(no_wait) );

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
}

void			sock_t_set_timeout( vj_sock_t *s, int t )
{
	int opt = t;
	setsockopt( s->sock_fd, SOL_SOCKET, SO_SNDTIMEO, (char*) &opt, sizeof(int));
	setsockopt( s->sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char*) &opt, sizeof(int));
}
*/

int			sock_t_recv( vj_sock_t *s, void *dst, int len )
{
	int bytes_left = len;
	int n;
	int bytes_done = 0;

	while( bytes_left > 0 )
	{	
sock_t_recv_lbl:		
		//@ setup socket with SO_RCVTIMEO
		n = recv( s->sock_fd, dst + bytes_done, bytes_left, MSG_WAITALL );
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
	unsigned int length = len;
	unsigned int done = 0;
	unsigned char *ptr = buf;
	while( length > 0 ) {
		n = send( fd, ptr, length , MSG_NOSIGNAL );
		if( n == -1 ) {
			veejay_msg(0, "Error sending buffer:%s", strerror(errno));
			return -1;
		}

		if( n == 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "Remote closed connection.");
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
