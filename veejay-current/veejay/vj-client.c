/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
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
#include <sys/types.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <veejay/vj-lib.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vj-client.h>
#include <stdlib.h>
#include <string.h>
#define VJC_OK 0
#define VJC_NO_MEM 1
#define VJC_SOCKET 2
#define VJC_BAD_HOST 3

vj_client *vj_client_alloc( int w, int h, int f )
{
	vj_client *v = (vj_client*) vj_malloc(sizeof(vj_client));
	if(!v)
	{
		return NULL;
	}
	v->planes[0] = 0;
	v->planes[1] = 0;
	v->planes[2] = 0;
	// recv. format / w/h 
	v->cur_width = w;
	v->cur_height = h;
	v->cur_fmt = f;
	return v;
}

int vj_client_connect(vj_client *v, char *host, int port_id, int *error  )
{
	v->he = gethostbyname( host );
	if(v->he == NULL )
	{
		*error = VJC_BAD_HOST;
		return 0;
	}
	v->handle = socket( AF_INET, SOCK_STREAM, 0 );

	if(v->handle < 0)
	{
		*error = VJC_SOCKET;	
		return 0;
	}

	v->serv_addr.sin_family = AF_INET;
	v->serv_addr.sin_port = htons( port_id);
	v->serv_addr.sin_addr = *( (struct in_addr*) v->he->h_addr);

	v->status = socket( AF_INET, SOCK_STREAM, 0 );
	if(v->status < 0 )
	{
		*error = VJC_SOCKET;
		return 0;
	}

	v->stat_addr.sin_family = AF_INET;
	v->stat_addr.sin_port = htons( port_id + 1);
	v->stat_addr.sin_addr = *( (struct in_addr*) v->he->h_addr);

	if( connect( v->handle, (struct sockaddr*) &v->serv_addr,
		sizeof(struct sockaddr)) == -1 )
	{
		return 0;
	}

	if( connect( v->status, (struct sockaddr*) &v->stat_addr,
		sizeof(struct sockaddr)) == -1)
	{
		return 1;
	}

	FD_ZERO( &(v->master) );
	FD_ZERO( &(v->current) );
	FD_ZERO( &(v->exceptions) );
	FD_SET( v->handle, &(v->master) );
	return 1;
}

static int _vj_client_poll( vj_client *v )
{
	int status;
	fd_set fds;
	struct timeval no_wait;
	memset( &no_wait, 0, sizeof(no_wait ));
	FD_ZERO( &fds );
	FD_SET( v->handle, &fds );
	status = select( v->handle + 1, &fds, 0,0, &no_wait );
	if( status <= 0)
		return status;
	if(FD_ISSET( v->handle, &fds ) )
		return 1;
	return 0;
}

int vj_client_flush(vj_client *v)
{
	int nbytes = 100;
	int n  = 0;
	char stat[100];
	int status;
	fd_set fds;
	struct timeval no_wait;
	memset( &no_wait, 0, sizeof(no_wait));
	FD_ZERO( &fds );
	FD_SET( v->status, &fds);
	status = select( v->status + 1, &fds, 0,0, &no_wait );
	if(status <= 0 )
	{
		//veejay_msg(VEEJAY_MSG_DEBUG, "Missed status line!!");
		return 0;
	}
	if(status > 0 )
	if( FD_ISSET(v->status, &fds ))
	{
		//veejay_msg(VEEJAY_MSG_DEBUG, "OK, accepting status from sock %d",
		//	v->status);
		n = recv( v->status, stat, nbytes,0 );
		//veejay_msg(VEEJAY_MSG_DEBUG, "Flushed %d : [%s]", n,stat );
		return n;
	}
	return 0;
}

int vj_client_flush_block(vj_client *v)
{
	int nbytes = 100;
	char stat[100];
	int n = recv( v->status, stat, nbytes, MSG_WAITALL );
	if( n <= 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Remote closed connection");
	}
//	if( n > 0 )
//		veejay_msg(VEEJAY_MSG_DEBUG, "Flushed %d : [%s]", n,stat);
	return n;
}


// todo: while bytes left do a recv , if it returns -1 , flush the data
int	vj_client_read(vj_client *v, uint8_t *dst )
{
	int nb = 0;
	int len = v->planes[0] + v->planes[1] + v->planes[2];
	int n;
	char line[12];
	int w = 0, h = 0, f = 0;
	bzero(line,12);
	if( _vj_client_poll( v ) <= 0 )
		return 0;
	
	n = recv( v->handle, line, 11, MSG_WAITALL);
	if( n <= 0 )
	{
		return 0;
	}	
	n = sscanf( line, "%d %d %d", &w, &h , &f );
	if (n != 3 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error parsing frame info");
		return 0;
	}
	if( v->cur_width != w || v->cur_height != h || v->cur_fmt != f )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Mismatched frame info: Remote sends %dx%d:%d, Need %dx%d:%d",
			w,h,f,v->cur_width,v->cur_height,v->cur_fmt );
		return 0;
	}


	n = recv( v->handle, dst, len, MSG_WAITALL );
	if( n == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Remote closed connection");
		return 0;
	}

	if( n < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Nothing to read");
		 return 0;
	}

	nb += n;
	if( len != nb )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Partial read %d bytes", nb);
		return nb;
	}
	return nb;
}

int vj_client_send(vj_client *v, char *buf )
{
   int len = 0;
   char buf2[15];
   sprintf(buf2, "V%03dD%s",strlen(buf),buf);
   len = strlen(buf2);   
   if ((send(v->handle, buf2, len, 0)) == -1)
   { /* send the command */
		return 0;
   }
   return 1;
}

int vj_client_close( vj_client *v )
{
	close( v->handle );
	close( v->status );
	return 1;
}

