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
#include <netinet/in.h>
#include <sys/socket.h>
#ifndef VJ_CLIENT_H
#define VJ_CLIENT_H


typedef struct
{
	struct hostent *he;
	struct sockaddr_in serv_addr;
    struct sockaddr_in stat_addr;
	int handle;
	int status;
	int planes[3];
	fd_set master;
	fd_set current;
	fd_set exceptions;
	int cur_width;
	int cur_height;
	int cur_fmt;
} vj_client;

int vj_client_connect( vj_client *v, char *host, int port_id, int *error );

int vj_client_read( vj_client *v, uint8_t *dst );

int	vj_client_close( vj_client *v );

int vj_client_flush(vj_client *v);

int vj_client_send( vj_client *v, char *buf);

vj_client *vj_client_alloc(int w , int h, int f);

#endif

