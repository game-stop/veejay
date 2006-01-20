/* libvjnet - Linux VeeJay
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
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdio.h>
#include <libvjnet/vj-client.h>
#include <libvjmsg/vj-common.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#define VJC_OK 0
#define VJC_NO_MEM 1
#define VJC_SOCKET 2
#define VJC_BAD_HOST 3

#define PACKET_LEN (65535*32)

vj_client *vj_client_alloc( int w, int h, int f )
{
	vj_client *v = (vj_client*) malloc(sizeof(vj_client));
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
	v->c = (conn_type_t**) malloc(sizeof(conn_type_t*) * 3);
	v->c[0] = (conn_type_t*) malloc(sizeof(conn_type_t));
	v->c[1] = (conn_type_t*) malloc(sizeof(conn_type_t));
	v->c[2] = (conn_type_t*) malloc(sizeof(conn_type_t));
	v->blob = (unsigned char*) malloc(sizeof(unsigned char) * PACKET_LEN ); 
	v->mcast = 0;
	if(!v->blob)
		return NULL;
	bzero( v->blob, PACKET_LEN );   
	return v;
}

void		vj_client_free(vj_client *v)
{
	if(v)
	{
		if(v->c[0])
			free(v->c[0]);
		if(v->c[1])
			free(v->c[1]);
		if(v->c[2])
			free(v->c[2]);
		if(v->c) 
			free(v->c);
		if(v->blob)
			free(v->blob);
		free(v);
	}
}


void	vj_client_flush(vj_client *v, int num_frames)
{
	if(vj_client_poll(v, V_STATUS ))
	{
		char status[100];
		int bytes = 100;
		int n = vj_client_read( v, V_STATUS,status,bytes);
		if( n > 0 )
		{	num_frames --;	
		}
	
	}
}

int vj_client_connect(vj_client *v, char *host, char *group_name, int port_id  )
{
	int error = 0;
	if( group_name == NULL )
	{

		if(host == NULL)
			return 0;
		if(port_id <= 0 || port_id > 65535)
			return 0;

		v->c[0]->type = VSOCK_C;
		v->c[0]->fd   = alloc_sock_t();
		v->c[1]->type = VSOCK_S;
		v->c[1]->fd   = alloc_sock_t();
		v->c[2]->type = VSOCK_C;
		v->c[2]->fd   = alloc_sock_t();

		if(!v->c[0]->fd || !v->c[1]->fd || !v->c[2] )
		{
			return 0;
		}
		if( sock_t_connect( v->c[0]->fd, host, port_id + VJ_CMD_PORT ) )
		{
			if( sock_t_connect( v->c[1]->fd, host, port_id + VJ_STA_PORT ) )
			{		
				if( sock_t_connect( v->c[2]->fd, host, port_id + VJ_MSG_PORT ) )	
					return 1;
			}
		}
	}
	else
	{
		if(port_id <= 0 || port_id > 65535 )
			return 0;

		v->c[0]->type = VMCAST_C;
		v->c[0]->r    = mcast_new_receiver( group_name, port_id + VJ_CMD_MCAST ); 
		v->c[0]->s    = mcast_new_sender( group_name );
		v->ports[0] = port_id + VJ_CMD_MCAST;
		v->ports[1] = port_id + VJ_CMD_MCAST_IN;
		mcast_sender_set_peer( v->c[0]->s , group_name );
		v->mcast = 1;
//		mcast_receiver_set_peer( v->c[0]->r, group_name);
//		veejay_msg(VEEJAY_MSG_DEBUG, "Added peer");
		veejay_msg(VEEJAY_MSG_DEBUG, "Client is interested in packets from group %s : %d, send to %d",
			group_name, port_id + VJ_CMD_MCAST , port_id + VJ_CMD_MCAST_IN);

		return 1;
	}
	return error;
}



int	vj_client_poll( vj_client *v, int sock_type )
{
	if(sock_type == V_STATUS )
	{
		if(v->c[1]->type == VSOCK_S)
			return ( sock_t_poll(v->c[1]->fd ) );
	}
	if(sock_type == V_CMD )
	{
		if(v->c[0]->type == VSOCK_C)
			return	( sock_t_poll( v->c[0]->fd ));
		if(v->c[0]->type == VMCAST_C )
			return  ( mcast_poll( v->c[0]->r ));
	}
	if(sock_type == V_MSG )
	{
		if(v->c[2]->type == VSOCK_C)
			return	( sock_t_poll( v->c[2]->fd ));
	}

	return 0;
}

int	vj_client_read_i( vj_client *v, uint8_t *dst )
{
	int len = v->planes[0] + v->planes[1] + v->planes[2];
	char line[12];
	int p[3] = { 0,0,0 };
	int n = 0;

	if( v->c[0]->type == VMCAST_C )
	{
		return mcast_recv_frame( v->c[0]->r, dst, len );
	}

	bzero(line,12);
	if( vj_client_poll( v, V_CMD ) <= 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Frame not ready");
		return 0;
	}
	if( v->c[0]->type == VSOCK_C )
		n = sock_t_recv_w( v->c[0]->fd, line, 11 );	

	if( n <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Frame header error");
		return 0;
	}
	n = sscanf( line, "%d %d %d", p + 0 , p + 1, p + 2 );
	if( n != 3)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Frame header invalid %s",line);
		return 0;
	}
	if( v->cur_width != p[0] || v->cur_height != p[1] || v->cur_fmt != p[2])
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Frame contents invalid");
		return 0;
	}
	if( v->c[0]->type == VSOCK_C) 
		n = sock_t_recv_w( v->c[0]->fd, dst, len );

	return n;
}

int	vj_client_get_status_fd(vj_client *v, int sock_type )
{	
	if(sock_type == V_STATUS)
	{
		return v->c[1]->fd->sock_fd;
	}
	if(sock_type == V_CMD )
	{
		if(!v->mcast)
			return	v->c[0]->fd->sock_fd;
		else
			return  v->c[0]->r->sock_fd;
	}
	if(sock_type == V_MSG )
	{
		return  v->c[2]->fd->sock_fd;
	}

	return 0;	
}

int	vj_client_read(vj_client *v, int sock_type, uint8_t *dst, int bytes )
{
	if( sock_type == V_STATUS )
	{
		if(v->c[1]->type == VSOCK_S)
			return( sock_t_recv( v->c[1]->fd, dst, bytes ) );
	}
	if( sock_type == V_CMD )
	{
		if(v->c[0]->type == VSOCK_C)
			return ( sock_t_recv( v->c[0]->fd, dst, bytes ) );
	}
	if( sock_type == V_MSG )
	{
		if(v->c[2]->type == VSOCK_C)
			return ( sock_t_recv( v->c[2]->fd, dst, bytes ) );
	}
	return 0;
}

int vj_client_send(vj_client *v, int sock_type,char *buf )
{
	if( sock_type == V_CMD )
	{

		// format msg
		int len = strlen( buf );
		sprintf(v->blob, "V%03dD%s", len, buf);
		if(v->c[0]->type == VSOCK_C)
			return ( sock_t_send( v->c[0]->fd, v->blob, len + 5 ));
		if(v->c[0]->type == VMCAST_C)
			return ( mcast_send( v->c[0]->s, (void*) v->blob, len + 5,
					v->ports[1] ));
	}
	if( sock_type == V_MSG )
	{

		// format msg
		int len = strlen( buf );
		sprintf(v->blob, "V%03dD%s", len, buf);
		if(v->c[2]->type == VSOCK_C)
			return ( sock_t_send( v->c[2]->fd, v->blob, len + 5 ));
	}

   	return 1;
}


int vj_client_close( vj_client *v )
{
	if(v)
	{
		if(v->c[0]->type == VSOCK_C)
			sock_t_close(v->c[0]->fd );
		if(v->c[1]->type == VSOCK_S)
			sock_t_close(v->c[1]->fd );
		if(v->c[2]->type == VSOCK_C )
			sock_t_close(v->c[2]->fd );
		if(v->c[0]->type == VMCAST_C)
		{
			mcast_close_receiver( v->c[0]->r );
			mcast_close_sender( v->c[0]->s );
		}
		return 1;
	}
	return 0;
}

int	vj_client_test(char *host, int port)
{
	struct hostent *he = gethostbyname( host );

	if( h_errno == HOST_NOT_FOUND )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Specified host '%s':'%d' is unknown", host,port );
		return 0;
	}

	if( h_errno == NO_ADDRESS || h_errno == NO_DATA )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Specified host '%s' is valid but does not have IP address",
			host );
		return 0;
	}
	if( h_errno == NO_RECOVERY )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Non recoverable name server error occured");
		return 0;
	}
	if( h_errno == TRY_AGAIN )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Temporary error occurred on an authoritative name. Try again later");
		return 0;
	}
	return 1;
}
