/* veejay - Linux VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <string.h>
#include <veejay/vj-lib.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vj-server.h>
#include "vj-global.h"

typedef struct
{
	char *msg;
	int   len;
} vj_message;

typedef struct {
	int handle;
	int in_use;
	vj_message **m_queue;
	int n_queued;
	int n_retrieved;
} vj_link;

#define VJ_MAX_PENDING_MSG 64
#define RECV_SIZE (1024*256)

int		_vj_server_free_slot(vj_server *vje);
int		_vj_server_new_client(vj_server *vje, int socket_fd);
int		_vj_server_del_client(vj_server *vje, int link_id);
int		_vj_server_parse_msg(vj_server *vje,int link_id, char *buf, int buf_len );

static char *recv_buffer = NULL;

int vj_server_init()
{
	// 1 mb buffer
	recv_buffer = (char*) malloc(sizeof(char) * RECV_SIZE);
	if(!recv_buffer) 
		return 0;
	bzero( recv_buffer, RECV_SIZE );
	return 1;
}

vj_server *vj_server_alloc(int port)
{
    int i;
    int on = 1;
	vj_link **link;
    vj_server *vjs = (vj_server *) vj_malloc(sizeof(struct vj_server_t));
    if (!vjs)
		return NULL;

    if ((vjs->handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to create a socket");
		free(vjs);
		return NULL;
    }

    if (setsockopt( vjs->handle, SOL_SOCKET, SO_REUSEADDR, (const char*) &on, sizeof(on) )== -1)
	{
		veejay_msg(VEEJAY_MSG_ERROR,
			   "Cannot turn off bind addres checking");
    }

    vjs->myself.sin_family = AF_INET;
    vjs->myself.sin_addr.s_addr = INADDR_ANY;
    vjs->myself.sin_port = htons(port);

    memset(&(vjs->myself.sin_zero), '\0', 8);

    if (bind(vjs->handle, (struct sockaddr *) &(vjs->myself), sizeof(vjs->myself) ) == -1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Bind error - Port %d in use ?", port);
		return NULL;
    }

    if (listen(vjs->handle, VJ_MAX_CONNECTIONS) == -1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Listen error.");
		return NULL;
    }

	link = (vj_link **) vj_malloc(sizeof(vj_link *) * VJ_MAX_CONNECTIONS);
	if(!link)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Out of memory");
		free(vjs);
		return NULL;
	}

	for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
	{
		int j;
		link[i] = (vj_link*) vj_malloc(sizeof(vj_link));
		if(!link[i])
		{
			free(vjs);
			free(link);
			return NULL;
		}
		link[i]->in_use = 0;
		link[i]->m_queue = (vj_message**) vj_malloc(sizeof( vj_message * ) * VJ_MAX_PENDING_MSG );
		if(!link[i]->m_queue)
		{
			free(vjs);
			free(link[i]);
			return NULL;
		}
		for( j = 0; j < VJ_MAX_PENDING_MSG; j ++ )
		{
			link[i]->m_queue[j] = (vj_message*) vj_malloc(sizeof(vj_message));
			link[i]->m_queue[j]->len = 0;
			link[i]->m_queue[j]->msg = NULL;
		}
		link[i]->n_queued = 0;
		link[i]->n_retrieved = 0;		
	}
	vjs->link = (void**) link;
	vjs->nr_of_links = 0;

	FD_ZERO( &(vjs->fds) );
	FD_ZERO( &(vjs->wds) );
	FD_SET( vjs->handle, &(vjs->fds) );
	//FD_SET( vjs->handle, &(vjs->wds) );

	vjs->nr_of_connections = vjs->handle;


	veejay_msg(VEEJAY_MSG_DEBUG,"Connection ready at socket %d", vjs->handle );

    return vjs;
}


int vj_server_send( vj_server *vje, int link_id, uint8_t *buf, int len )
{
    unsigned int total = 0;
    unsigned int bytes_left = len;
    int n;
	vj_link **Link = (vj_link **) vje->link;

	if (len <= 0 || Link[link_id]->in_use==0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "nothing to send");
		return 0;
	}

	if(!FD_ISSET( Link[link_id]->handle, &(vje->wds) ) )
	{
		return 0;
	}

	while (total < len)
	{
		n = send(Link[link_id]->handle, buf + total, bytes_left, 0);
		if (n == -1)
		{
		    return 0;
		}
		if ( n == 0)
		{
			return 0;
		}
		total += n;
		bytes_left -= n;
   	}
    return total;
}

int _vj_server_free_slot(vj_server *vje)
{
	vj_link **Link = (vj_link**) vje->link;
    int i;
	for (i = 0; i < VJ_MAX_CONNECTIONS; i++)
	{
	    if (Link[i]->in_use == 0)
			return i;
    }
    return VJ_MAX_CONNECTIONS;
}

int _vj_server_new_client(vj_server *vje, int socket_fd)
{
    int entry = _vj_server_free_slot(vje);
	vj_link **Link = (vj_link**) vje->link;
    if (entry == VJ_MAX_CONNECTIONS)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot take more connections (max %d allowed)", VJ_MAX_CONNECTIONS);
		return VJ_MAX_CONNECTIONS;
	}
    Link[entry]->handle = socket_fd;
    Link[entry]->in_use = 1;
    FD_SET( socket_fd, &(vje->fds) );
	FD_SET( socket_fd, &(vje->wds) );
	vje->nr_of_links ++;

    return entry;
}

int _vj_server_del_client(vj_server * vje, int link_id)
{
	vj_link **Link = (vj_link**) vje->link;
	if(Link[link_id]->in_use )
	{
		Link[link_id]->in_use = 0;
		FD_CLR( Link[link_id]->handle, &(vje->fds) );
		FD_CLR( Link[link_id]->handle, &(vje->wds) );
		close(Link[link_id]->handle);
		vje->nr_of_links --;
		return 1;
    }
    return 0;
}

void	vj_server_close_connection(vj_server *vje, int link_id )
{
	_vj_server_del_client( vje, link_id );
}

int vj_server_poll(vj_server * vje)
{
	int status;
    	struct timeval t;
	memset( &t, 0, sizeof(t));

	FD_ZERO( &(vje->fds) );
    	FD_ZERO( &(vje->wds) );
	FD_SET( vje->handle, &(vje->fds) );
	FD_SET( vje->handle, &(vje->wds) );

	int i;
	for( i = 0; i < vje->nr_of_links ; i ++ )
	{
		vj_link **Link= (vj_link**) vje->link; 
		FD_SET( Link[i]->handle, &(vje->fds) );
		FD_SET( Link[i]->handle, &(vje->wds) );
	}

    	status = select(vje->nr_of_connections + 1, &(vje->fds), &(vje->wds), 0, &t);
	if( status > 0 )
	{
		return 1;
	}
	return 0;
}

int	_vj_server_parse_msg( vj_server *vje,int link_id, char *buf, int buf_len )
{
	int i = 0;
	int num_msg = 0;
	vj_link **Link = (vj_link**) vje->link;
	vj_message **v = Link[link_id]->m_queue;
	while( i < buf_len )
	{
		if( buf[i] == 'V' && buf[i+4] == 'D' )
		{
			int len, n;
			n = sscanf( buf + ( i + 1 ), "%03d", &len );
			if( n == 1)
			{
				i += 5; // skip header
				v[num_msg]->len = len;

				if(v[num_msg]->msg != NULL)
					free( v[num_msg]->msg );

				v[num_msg]->msg = (char*)strndup( buf + i , len );
				i += len;
				num_msg ++; 
			}
		}
		i ++;		
	}
	Link[link_id]->n_queued = num_msg;
	Link[link_id]->n_retrieved = 0;
	return num_msg;
}

int	vj_server_new_connection(vj_server *vje)
{
	if( FD_ISSET( vje->handle, &(vje->fds) ) )
	{
		int addr_len = sizeof(vje->remote);
		int n = 0;
		int fd = accept( vje->handle, (struct sockaddr*) &(vje->remote), &addr_len );
		if(fd == -1)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error accepting connection");
			return -1;
		}	

		if( vje->nr_of_connections < fd ) vje->nr_of_connections = fd;

		n = _vj_server_new_client(vje, fd); 
		if( n == VJ_MAX_CONNECTIONS )
		{
			close(fd);
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot establish connection");
			return -1;
		}
	//	veejay_msg(VEEJAY_MSG_DEBUG, "New connection socket %d", fd);
		return n;
	}
	return -1;
}

int	vj_server_update( vj_server *vje, int id )
{
	// new connection ?
	vj_link **Link = (vj_link**) vje->link;
	int sock_fd = Link[id]->handle;

	if( FD_ISSET( sock_fd, &(vje->fds)) )
	{
		int n;
		if( !Link[id]->in_use )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Link %d not in use but socket changed!", id);
			return 0;
		}

		n = recv( sock_fd, recv_buffer, RECV_SIZE, 0 );
		if( n <= 0 )
		{
			veejay_msg(VEEJAY_MSG_INFO, "Closing connection num %d", id );
			_vj_server_del_client( vje, id );
			return 0;
		}

		return _vj_server_parse_msg( vje, id, recv_buffer,n );	
	}
	return 0;	
}

void vj_server_shutdown(vj_server *vje)
{
	int j,i;
	vj_link **Link = (vj_link**) vje->link;

	for(i=0; i < VJ_MAX_CONNECTIONS; i++)
	{
		if(Link[i]->in_use) 
			close(Link[i]->handle);
		for( j = 0; j < VJ_MAX_PENDING_MSG; j ++ )
		{
			if(Link[i]->m_queue[j]->msg ) free( Link[i]->m_queue[j]->msg );
			if(Link[i]->m_queue[j] ) free( Link[i]->m_queue[j] );
		}
		if( Link[i]->m_queue ) free( Link[i]->m_queue );
		if( Link[i] ) free( Link[i] );
    }
    close(vje->handle);
	free(Link);
}

int vj_server_retrieve_msg(vj_server *vje, int id, char *dst )
{
	vj_link **Link = (vj_link**) vje->link;
   	if (Link[id]->in_use == 0)
		return 0;

	int index = Link[id]->n_retrieved;
	char *msg;
	int   len;

	if( index == Link[id]->n_queued )
	{
		return 0; // done
	}
	msg = Link[id]->m_queue[index]->msg;
	len = Link[id]->m_queue[index]->len;

	strncpy( dst, msg, len );

	index ++;

	Link[id]->n_retrieved = index;

    return 1;			
}

