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

vj_client *vj_client_connect( char *host, int port_id, int *error  )
{
	vj_client *v = (vj_client*) vj_malloc(sizeof(vj_client));
	if(!v)
	{
		*error = VJC_NO_MEM;
		return NULL;
	}
	v->he = gethostbyname( host );
	if(v->he == NULL )
	{
		*error = VJC_BAD_HOST;
		if(v) free(v);
		return NULL;
	}
	v->handle = socket( AF_INET, SOCK_STREAM, 0 );

	if(v->handle < 0)
	{
		*error = VJC_SOCKET;	
		free(v);
		return NULL;
	}

	v->serv_addr.sin_family = AF_INET;
	v->serv_addr.sin_port = htons( port_id);
	v->serv_addr.sin_addr = *( (struct in_addr*) v->he->h_addr);

	v->status = socket( AF_INET, SOCK_STREAM, 0 );
	if(v->status < 0 )
	{
		*error = VJC_SOCKET;
		free(v);
		return NULL;
	}

	v->stat_addr.sin_family = AF_INET;
	v->stat_addr.sin_port = htons( port_id + 1);
	v->stat_addr.sin_addr = *( (struct in_addr*) v->he->h_addr);

	if( connect( v->handle, (struct sockaddr*) &v->serv_addr,
		sizeof(struct sockaddr)) == -1 )
	{
		free(v);
		return NULL;
	}

	if( connect( v->status, (struct sockaddr*) &v->stat_addr,
		sizeof(struct sockaddr)) == -1)
	{
		free(v);
		return NULL;
	}

	v->planes[0] = 0;
	v->planes[1] = 0;
	v->planes[2] = 0;
	// recv. format / w/h 

 	FD_ZERO( &(v->master) );
	FD_ZERO( &(v->current) );
	FD_SET( v->handle, &(v->master) );

	return v;
}

int	vj_client_poll(vj_client *v)
{	
	struct timeval tv;
	v->current = v->master;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	if ( select ( v->handle, &(v->current), NULL,NULL,&tv) == -1 )
		return 0;
	return 1;
}

int vj_client_flush(vj_client *v)
{
	int nbytes = 100;
	int n  = 0;
	char status[100];
	n = recv( v->status, status, nbytes,0 );
	return n;
}

// todo: while bytes left do a recv , if it returns -1 , flush the data
int	vj_client_read(vj_client *v, uint8_t *dst )
{
	int nb = 0;
	int len = v->planes[0] + v->planes[1] + v->planes[2];
	int n = recv( v->handle, dst, len, MSG_WAITALL );
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
		veejay_msg(VEEJAY_MSG_ERROR, "Error sending [%s]", buf2);
		return 0;
   }
   return 1;
}

int vj_client_close( vj_client *v )
{
	close( v->handle );
	return 1;
}

