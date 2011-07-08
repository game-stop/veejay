/* libvjnet - Linux VeeJay
 * 	     (C) 2002-2007 Niels Elburg <nwelburg@gmail.com> 
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
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vims.h>
#include <libvjnet/mcastreceiver.h>
#include <libvjnet/mcastsender.h>
#include <libvjnet/vj-server.h>
#include <libvjmem/vjmem.h>
#include <netinet/tcp.h>
#include <libvjnet/cmd.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#define __INVALID 0
#define __SENDER 1
#define __RECEIVER 2

typedef struct
{
	char *msg;
	int   len;
} vj_message;

typedef struct {
	int handle;
	int in_use;
	int promote;
	vj_message **m_queue;
	vj_message *lin_queue;
	int n_queued;
	int n_retrieved;
} vj_link;

typedef struct
{
	mcast_sender	*s;  // for sending frames only
	mcast_receiver	*r; // for taking commands
	int type;
} vj_proto;

#define VJ_MAX_PENDING_MSG 128
#define RECV_SIZE (32000) 

int		_vj_server_free_slot(vj_server *vje);
int		_vj_server_new_client(vj_server *vje, int socket_fd);
int		_vj_server_parse_msg(vj_server *vje,int link_id, char *buf, int buf_len, int priority );
int		_vj_server_empty_queue(vj_server *vje, int link_id);

static		int geo_stat_ = 0;

static void		vj_server_geo_stats_(char *request)
{
	if(geo_stat_)
		return;

	//@ send 1 time http request
	vj_sock_t *dyne = alloc_sock_t();
	if(dyne) {
		sock_t_connect_and_send_http( dyne, "www.veejayhq.net",80, request,strlen(request));
		sock_t_close( dyne );
		free(dyne);
	}

	geo_stat_ = 1;
}

void	vj_server_geo_stats()
{
	char request[128];
	snprintf(request,sizeof(request),"GET /veejay-15 HTTP/1.1\nHost: www.veejayhq.net\nReferrer: http://");

	//@ knock veejay.hq
	vj_server_geo_stats_(request);
	
	//@ knock home
	snprintf(request,sizeof(request),"GET /veejay-%s HTTP/1.1\nHost: c0ntrol.dyndns.org\n",VERSION );
	vj_server_geo_stats_(request);
}

void		vj_server_set_mcast_mode( vj_server *v , int mode )
{
	v->mcast_gray = mode;
	veejay_msg(VEEJAY_MSG_DEBUG, "Sending in %s", (mode==0 ? "Color" : "Grayscale" ) );
}

static	int	_vj_server_multicast( vj_server *v, char *group_name, int port )
{
	vj_link **link;
	int i;

	vj_proto	**proto  = (vj_proto**) malloc(sizeof( vj_proto* ) * 2);
	
	proto[0] = (vj_proto*) vj_malloc(sizeof( vj_proto ) );
	if( v->server_type == V_CMD )
	{
		proto[0]->s = mcast_new_sender( group_name );
		if(!proto[0]->s)
			return 0;

		proto[0]->r = mcast_new_receiver( group_name , port + VJ_CMD_MCAST_IN );
		if(!proto[0]->r )
			return 0;

		v->ports[0] = port + VJ_CMD_MCAST;
		v->ports[1] = port + VJ_CMD_MCAST_IN;
	}

	v->protocol = (void**) proto;
	link = (vj_link **) vj_malloc(sizeof(vj_link *) * VJ_MAX_CONNECTIONS);

	if(!link)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Out of memory");
		return 0;
	}

	for( i = 0; i < 1; i ++ ) /* only 1 link needed for multicast:
					the link hold all messages received  */
	{
		int j;
		link[i] = (vj_link*) vj_malloc(sizeof(vj_link));
		if(!link[i])
		{
			return 0;
		}
		link[i]->in_use = 1;
		link[i]->promote = 0;
		link[i]->m_queue = (vj_message**) vj_malloc(sizeof( vj_message * ) * VJ_MAX_PENDING_MSG );
		if(!link[i]->m_queue)
			return 0;
		link[i]->lin_queue = (vj_message*) vj_calloc(sizeof(vj_message) * VJ_MAX_PENDING_MSG );
		if(!link[i]->lin_queue)
			return 0;

		for( j = 0; j < VJ_MAX_PENDING_MSG; j ++ )
		{
		/*	link[i]->m_queue[j] = (vj_message*) vj_malloc(sizeof(vj_message));
			link[i]->m_queue[j]->len = 0;
			link[i]->m_queue[j]->msg = NULL;*/
			link[i]->m_queue[j] = &(link[i]->lin_queue[j]);
		}
		link[i]->n_queued = 0;
		link[i]->n_retrieved = 0;
	}
	v->link = (void**) link;

	veejay_msg(VEEJAY_MSG_INFO, "UDP multicast frame sender ready at (group '%s')",
	  	 group_name );

	return 1;
}

static int	_vj_server_classic(vj_server *vjs, int port_offset)
{
	int on = 1;
	int port_num = 0;   
	vj_link **link;
	int i = 0;
	if ((vjs->handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create a socket: %s", strerror(errno));
		return 0;
    	}
    	if (setsockopt( vjs->handle, SOL_SOCKET, SO_REUSEADDR, (const char*) &on, sizeof(on) )== -1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
		return 0;
   	}
	vjs->myself.sin_family = AF_INET;
	vjs->myself.sin_addr.s_addr = INADDR_ANY;

	if( vjs->server_type == V_CMD )
		port_num = port_offset + VJ_CMD_PORT;
	if( vjs->server_type == V_STATUS )
		port_num = port_offset + VJ_STA_PORT;

	vjs->myself.sin_port = htons(port_num);
	veejay_memset(&(vjs->myself.sin_zero), 0, 8);
	if (bind(vjs->handle, (struct sockaddr *) &(vjs->myself), sizeof(vjs->myself) ) == -1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
		return 0;
	}
	if (listen(vjs->handle, VJ_MAX_CONNECTIONS) == -1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strerror(errno));
		return 0;
   	}


	int send_size = 1024 * 1024;
	if( setsockopt( vjs->handle, SOL_SOCKET, SO_SNDBUF, (const char*) &send_size, sizeof(send_size) ) == - 1)
	{
		veejay_msg(0, "Cannot set send buffer size: %s", strerror(errno));
	}
	int tmp = sizeof(int);
	if( getsockopt( vjs->handle, SOL_SOCKET, SO_SNDBUF,(unsigned char*) &(vjs->send_size), &tmp) == -1 )
	{
		veejay_msg(0, "Cannot read socket buffer size: %s", strerror(errno));
		return 0;
	}
	if( setsockopt( vjs->handle, SOL_SOCKET, SO_RCVBUF, (const char*) &send_size, sizeof(send_size)) == 1 )
	{
		veejay_msg(0, "Cannot set recv buffer sze:%s", strerror(errno));
		return 0;
	}
	if( getsockopt( vjs->handle, SOL_SOCKET, SO_RCVBUF, (unsigned char*) &(vjs->recv_size), &tmp) == -1 )
	{
		veejay_msg(0, "Cannot read socket buffer receive size %s" , strerror(errno));
		return 0;
	}

	int flag = 1;
	if( setsockopt( vjs->handle, IPPROTO_TCP, TCP_NODELAY, (char*) &flag, sizeof(int)) == -1 )
	{
		veejay_msg(0, "Cannot disable Nagle buffering algorithm: %s", strerror(errno));
		return 0;
	}

	link = (vj_link **) vj_malloc(sizeof(vj_link *) * VJ_MAX_CONNECTIONS);
	if(!link)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Out of memory");
		return 0;
	}

	for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
	{
		int j;
		link[i] = (vj_link*) vj_calloc(sizeof(vj_link));
		if(!link[i])
			return 0;
		link[i]->in_use = 0;
		link[i]->promote = 0;
		link[i]->m_queue = (vj_message**) vj_calloc(sizeof( vj_message * ) * VJ_MAX_PENDING_MSG );
		if(!link[i]->m_queue)	return 0;
		link[i]->lin_queue = (vj_message*) vj_calloc(sizeof(vj_message) * VJ_MAX_PENDING_MSG );
		if(!link[i]->lin_queue)
			return 0;
		for( j = 0; j < VJ_MAX_PENDING_MSG; j ++ )
			link[i]->m_queue[j] = &(link[i]->lin_queue[j]);
		link[i]->n_queued = 0;
		link[i]->n_retrieved = 0;		
	}
	vjs->link = (void**) link;
	vjs->nr_of_connections = vjs->handle;

	switch(vjs->server_type )
	{
		case V_STATUS:
			veejay_msg(VEEJAY_MSG_INFO,"TCP/IP unicast VIMS status socket ready at port %d, (R:%d, S:%d)",
				port_num, vjs->recv_size, vjs->send_size );
			break;
		case V_CMD:
			veejay_msg(VEEJAY_MSG_INFO,"TCP/IP unicast VIMS control socket ready at port %d (R:%d, S:%d)",
				port_num, vjs->recv_size, vjs->send_size );
			break;
		default:
#ifdef STRICT_CHECKING
		assert(0);
#endif
		break;
	}

	return 1;
}
vj_server *vj_server_alloc(int port_offset, char *mcast_group_name, int type)
{
	vj_server *vjs = (vj_server *) vj_calloc(sizeof(struct vj_server_t));
	
	if (!vjs)
		return NULL;

	vjs->recv_buf = (char*) vj_calloc(sizeof(char) * RECV_SIZE );
	if(!vjs->recv_buf)
	{
		if(vjs) free(vjs);
		return NULL;
	}	

	vjs->server_type = type;

	/* setup peer to peer socket */
	if( mcast_group_name == NULL )
	{
		vjs->use_mcast = 0;
		if ( _vj_server_classic( vjs,port_offset ) )
			return vjs;
	}
 	else
	{	/* setup multicast socket */
		vjs->use_mcast = 1;
		if ( _vj_server_multicast(vjs, mcast_group_name, port_offset) )
			return vjs;
	}
	  
    	return NULL;
}


int	vj_server_link_can_write( vj_server *vje, int link_id );

int vj_server_send( vj_server *vje, int link_id, uint8_t *buf, int len )
{
	unsigned int total = 0;
	unsigned int bytes_left = len;
	int n;
#ifdef STRICT_CHECKING
	assert( vje->send_size > 0 );
	assert( len > 0 );
	assert( buf != NULL );
	assert( link_id >= 0 );
#endif
	vj_link **Link = (vj_link**) vje->link;
	
	if( !Link[link_id]->in_use ) 
		return 0;


	if( !vj_server_link_can_write( vje,link_id ) ) {
		veejay_msg(0,"Not ready for sending.");
		return -1;
	}

	if( !vje->use_mcast)
	{
		total  = sock_t_send_fd( Link[link_id]->handle, vje->send_size, buf, len);
		if( total <= 0 )
		{
			veejay_msg(0,"Unable to send buffer to %s:%s ",
				(char*)(inet_ntoa(vje->remote.sin_addr)),strerror(errno));
			return -1;
		}
		if( total < len )
			return -1;
	}
	else
	{
		vj_proto **proto = (vj_proto**) vje->protocol;
		if( vje->server_type == V_CMD )
			return mcast_send( proto[0]->s, buf, bytes_left, vje->ports[0] );
	}
	return total;
}

int	vj_server_link_can_write( vj_server *vje, int link_id)
{
	vj_link **Link = (vj_link**) vje->link;
	if( !Link[link_id]->in_use )
		return 0;

	if( FD_ISSET( Link[link_id]->handle, &(vje->wds) ) )
		return 1;
	return 0;
}

int	vj_server_link_can_read( vj_server *vje, int link_id)
{
	vj_link **Link = (vj_link**) vje->link;

	if( !Link[link_id]->in_use )
		return 0;

	if( FD_ISSET( Link[link_id]->handle, &(vje->fds) ) )
		return 1;
	return 0;
}

static int vj_server_send_frame_now( vj_server *vje, int link_id, uint8_t *buf, int len )
{
    unsigned int total = 0;
    unsigned int bytes_left = len;
    int n;
#ifdef STRICT_CHECKING
	assert( vje->send_size > 0 );
	assert( len > 0 );
	assert( buf != NULL );
	assert( link_id >= 0 );
#endif

	vj_link **Link = (vj_link**) vje->link;
#ifdef STRICT_CHECKING
	assert( Link[link_id]->in_use == 1 );
#endif
	total  = sock_t_send_fd( Link[link_id]->handle, vje->send_size, buf, len);
#ifdef STRICT_CHECKING
	if( total != len )
		veejay_msg(VEEJAY_MSG_ERROR, "Only sent %d out of %d bytes", total,len);
#endif
	if( total <= 0 )
	{
		veejay_msg(0,"Unable to send buffer to %s: %s",
			(char*)(inet_ntoa(vje->remote.sin_addr)),strerror(errno));
		return 0;
	}

    	return total;
}

int		vj_server_send_frame( vj_server *vje, int link_id, uint8_t *buf, int len, 
					VJFrame *frame, long ms )
{
	if(!vje->use_mcast )
	{
		if( vj_server_link_can_write( vje, link_id ))
		{
			return vj_server_send_frame_now( vje, link_id, buf, len );
		} else {
			veejay_msg(VEEJAY_MSG_DEBUG, "%s: not ready to write.");
		}
		return 0;
	}
	else
	{
		vj_proto **proto = (vj_proto**) vje->protocol;
		if( vje->server_type == V_CMD  )
			return mcast_send_frame( proto[0]->s, frame, buf,len,ms, vje->ports[0],vje->mcast_gray );
	}
	return 0;
}

int _vj_server_free_slot(vj_server *vje)
{
	vj_link **Link = (vj_link**) vje->link;
    	unsigned int i;
	for (i = 0; i < VJ_MAX_CONNECTIONS; i++)
	{
	    if (!Link[i]->in_use)
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

    return entry;
}

int _vj_server_del_client(vj_server * vje, int link_id)
{
	vj_link **Link = (vj_link**) vje->link;
	Link[link_id]->in_use = 0;
	if(Link[link_id]->handle)
	{
		int res = close(Link[link_id]->handle);
		if( res == -1 ) {
			veejay_msg(0,"Error closing connection with socket %d:%s", Link[link_id]->handle, strerror(errno));
		}

		FD_CLR( Link[link_id]->handle, &(vje->fds) );
		FD_CLR( Link[link_id]->handle, &(vje->wds) );
	}
	Link[link_id]->handle = 0;
	Link[link_id]->promote = 0;
	Link[link_id]->n_queued = 0;
	Link[link_id]->n_retrieved = 0;
	return 1;
}

void	vj_server_close_connection(vj_server *vje, int link_id )
{
	
	_vj_server_del_client( vje, link_id );

}
int	vj_server_client_promoted( vj_server *vje, int link_id)
{
	vj_link **Link= (vj_link**) vje->link;
	return Link[link_id]->promote;	
}
void	vj_server_client_promote( vj_server *vje, int link_id)
{
	
	vj_link **Link= (vj_link**) vje->link; 
#ifdef STRICT_CHECKING
	assert( Link[link_id]->in_use == 1 );
#endif 
	Link[link_id]->promote = 1;	
#ifdef STRICT_CHECKING
	veejay_msg(VEEJAY_MSG_DEBUG, "Knock knock %x",
			Link[link_id]->handle );
#endif
}


int vj_server_poll(vj_server * vje)
{
	int status = 0;
	struct timeval t;
	int i;

	if( vje->use_mcast )
	{
		vj_proto **proto = (vj_proto**) vje->protocol;
		return mcast_poll( proto[0]->r );
	}

	veejay_memset( &t, 0, sizeof(t));

	FD_ZERO( &(vje->fds) );
    	FD_ZERO( &(vje->wds) );

	FD_SET( vje->handle, &(vje->fds) );
	FD_SET( vje->handle, &(vje->wds) );

	for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
	{
		vj_link **Link= (vj_link**) vje->link;
	        if( Link[i]->handle <= 0 )
			continue;	
	//	if( Link[i]->in_use )
	//	{
			FD_SET( Link[i]->handle, &(vje->fds) );
			FD_SET( Link[i]->handle, &(vje->wds) );	
//			FD_SET( Link[i]->handle, &(vje->eds) );
	//	} 
	}		

	status = select(vje->nr_of_connections + 1, &(vje->fds), &(vje->wds), NULL, &t);

	if( status == -1 ) {
		veejay_msg(0, "Error while polling socket: %s", strerror(errno));
	} 

	return status;
}

int	_vj_server_empty_queue(vj_server *vje, int link_id)
{
	vj_link **Link = (vj_link**) vje->link;
	vj_message **v = Link[link_id]->m_queue;
	int i;
	for( i = 0; i < VJ_MAX_PENDING_MSG; i ++ )
	{
		if( v[i]->msg ) 
			free(v[i]->msg);
		v[i]->msg = NULL;
		v[i]->len = 0;
	}
	Link[link_id]->n_queued = 0;
	Link[link_id]->n_retrieved = 0;
	
	veejay_memset( vje->recv_buf, 0, RECV_SIZE );
	
	return 1;
}

#define _vj_malfunction(msg,content,buflen, index)\
{ \
if(msg != NULL)\
veejay_msg(VEEJAY_MSG_DEBUG,"%s",msg);\
veejay_msg(VEEJAY_MSG_DEBUG,"Message content");\
veejay_msg(VEEJAY_MSG_DEBUG,"msg [%s] (%d bytes) at position %d",\
 content, buflen, index);\
int _foobar;\
for ( _foobar = 0; _foobar < (index+4); _foobar++);\
veejay_msg(VEEJAY_MSG_PRINT, " ");\
veejay_msg(VEEJAY_MSG_PRINT, "^");\
veejay_msg(VEEJAY_MSG_DEBUG, "VIMS (v) - dropped message");\
}
static  int	_vj_verify_msg(vj_server *vje,int link_id, char *buf, int buf_len )
{
	int i = 0;
	char *s = buf;
	int num_msg = 0;
	//vj_link **Link = (vj_link**) vje->link;

	if( (i+4) > buf_len )
	  _vj_malfunction("VIMS (v) Message too small", buf, buf_len, i );

	while( (i+4) < buf_len )
	{
		if( !(s[i]=='V' && s[i+4] == 'D') )
		{	
			if(s[i] != 'K')
			{
			_vj_malfunction( "VIMS (v) Cannot identify message as VIMS message", buf, buf_len, i);
			return 0;
			}
		}

		if( s[i] != 'K' )
		{
			char tmp_len[4];
			char net_id[4];
			int  slen = 0;
			int	netid = 0;

			bzero(tmp_len,4);
			bzero(net_id,4 );

			char *str_ptr = &s[i];
			str_ptr ++;  // skip 'V'

			veejay_strncpy( tmp_len,str_ptr, 3 ); // header length   
	
			if( sscanf(tmp_len, "%03d", &slen) <= 0 )
			{
				_vj_malfunction( "VIMS (v) Cannot read header length", buf, buf_len, i + 1);
				return 0;
			}
			if( slen > buf_len )
			{
				char msg[256];
				veejay_memset(msg,0,256);
				snprintf(msg, 256,"VIMS (v) Remote %s is sending corrupted packets", (char*) (inet_ntoa( vje->remote.sin_addr ) ) );
				_vj_malfunction( NULL, buf, buf_len, i + 1 );
				vj_server_close_connection( vje, link_id );
				return 0;
			}
			if ( slen > 999 )
			{
				char msg[256];
				veejay_memset(msg,0,256);
				snprintf(msg, 256, "VIMS (v) Remote %s is acting very suspiciously", (char* )( inet_ntoa( vje->remote.sin_addr ) ));
				_vj_malfunction( msg, buf, buf_len, i + 1);  
				vj_server_close_connection( vje, link_id );
				return 0;
			}

			i += 4; // advance to message content
			str_ptr += 4;
			veejay_strncpy( net_id, str_ptr, 3 );

			if( sscanf( net_id, "%03d", &netid ) <= 0 )
			{
				_vj_malfunction( "VIMS (v) Corrupt VIMS selector", buf, buf_len, i );
				return 0;
			}
			if( netid < 0 && netid > 600 )
			{
				_vj_malfunction( "VIMS (v) selector out of range", buf,buf_len, i );
				return 0;
			}
			if( vje->use_mcast )
				if( netid >= 400 && netid < 500 )
				{
					_vj_malfunction( "VIMS (v) multicast doesnt allow querying of data",buf,buf_len,i);
					return 0;
				}


			//FIXME: malformed endings
			int last_char = slen - 1;
			int failed = 1;
			if( last_char > 1 )
			{
				if(str_ptr[last_char] == '\n' &&
					str_ptr[last_char-1] == ';' )
						failed = 0;
				else if(str_ptr[last_char] == ';' )
					failed = 0;
				if(!failed)
				{
					num_msg ++;
					i += slen + 1;
				}
			}
			if(failed)
			{
				_vj_malfunction("VIMS (v) message does not end with ';'", buf, buf_len , i);
				return 0;
			}
		}
		else
		{
			int len = 0;
			char *str_ptr = &s[i];
		
			if(sscanf(str_ptr+1, "%8d",&len ))
			{
				i += len;
				num_msg ++;
				return num_msg;
			}
			else
			{
				_vj_malfunction("VIMS (v) keyframe packet length not set", str_ptr, buf_len, i );
			}

		}
	}
	if(num_msg > 0 )
		return num_msg;
	return 0;
}


static  int	_vj_parse_msg(vj_server *vje,int link_id, char *buf, int buf_len, int priority )
{
	int i = 0;
	char *s = buf;
	int num_msg = 0;
	vj_link **Link = (vj_link**) vje->link;
	vj_message **v = Link[link_id]->m_queue;

	while( (i+4) < buf_len )
	{
		char tmp_len[8];
		char net_id[4];
		int  slen = 0;
		int	netid = 0;
		bzero(tmp_len,8);
		bzero(net_id,4 );

		char *str_ptr = &s[i];
		
		if(s[i] == 'V')
		{
			str_ptr ++;
	
			veejay_strncpy( tmp_len,str_ptr, 3 ); // header length   
			if( sscanf(tmp_len, "%03d", &slen) <= 0 )
			{
				veejay_msg(0, "Error parsing header in '%s' (%d bytes)", buf, buf_len );
				return 0;
			}
			i += 4; // advance to message content
			str_ptr += 4;
			veejay_strncpy( net_id, str_ptr, 3 );
	
			if( sscanf( net_id, "%03d", &netid ) <= 0 )
			{
				if( strncasecmp(net_id, "key",3) != 0 )
				{
					veejay_msg(0, "Error parsing VIMS ID in '%s' (%d bytes)",buf,buf_len);
					return 0;
				}
				else
				{
					v[num_msg]->msg = (char*) vj_malloc( slen );
					veejay_memcpy( v[num_msg]->msg, str_ptr, slen );
					v[num_msg]->len = slen;
					num_msg++;
				}
			}
			else
			{
				if(! priority )
				{
					// store message anyway
					int n_len = slen;
					v[num_msg]->msg = (char*)strndup( str_ptr , slen );
					veejay_chomp_str( v[num_msg]->msg , &n_len );
					v[num_msg]->len = n_len;
					num_msg++;
				}
				if(priority && netid > 255 )
				{
					// store high priority only (reduce load) -         
					int n_len = slen;
					v[num_msg]->msg = (char*)strndup( str_ptr , slen );
					veejay_chomp_str( v[num_msg]->msg, &n_len );
					v[num_msg]->len = n_len;
					num_msg++;
				}
			}
	
			if(num_msg >= (VJ_MAX_PENDING_MSG-1) )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "VIMS server queue full - got %d/%d messages.",
					num_msg,VJ_MAX_PENDING_MSG );	
				return VJ_MAX_PENDING_MSG-1; // cant take more
			}
	//		i += slen;
			i += (slen+1); // try next message

		}
		else if (s[i] == 'K' )
		{
			str_ptr ++;
			veejay_memcpy( tmp_len,str_ptr, 8 );    
			if( sscanf(tmp_len, "%08d", &slen) <= 0 )
			{
				veejay_msg(0, "Error parsing KF header in '%s' (%d bytes)", buf, buf_len );
				return 0;
			}

			i += 8; // advance to message content
			str_ptr += 8;
	
			v[num_msg]->msg = (char*) vj_calloc( slen );
			veejay_memcpy( v[num_msg]->msg, str_ptr, slen );
			v[num_msg]->len = slen;
			num_msg++;

			i += slen;
		}
	}

	if( ! priority )
	{
		Link[link_id]->n_queued = num_msg;
		Link[link_id]->n_retrieved = 0;
	}
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
			veejay_msg(VEEJAY_MSG_ERROR, "Error accepting connection: %s",
						strerror(errno));
			return 0;
		}	

		char *host = inet_ntoa( vje->remote.sin_addr ); 
		veejay_msg(VEEJAY_MSG_INFO, "Connection with %s on port %d", host,
			vje->remote.sin_port);		
		if( vje->nr_of_connections < fd )
			vje->nr_of_connections = fd;

		n = _vj_server_new_client(vje, fd); 
		if( n >= VJ_MAX_CONNECTIONS )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					"No more connections allowed");
			close(fd);
			return 0;
		}
		return 1;
	}
	return 0;
}

//@ return 0 when work is done
int	vj_server_update( vj_server *vje, int id )
{
	int sock_fd = vje->handle;
	int n = 0;
	_vj_server_empty_queue(vje, id);

	if(!vje->use_mcast)
	{
		vj_link **Link = (vj_link**) vje->link;
		sock_fd = Link[id]->handle;

		if(!FD_ISSET( sock_fd, &(vje->fds)) )
			return 0; //@ nothing to receive, fall through
	}

	int bytes_received = 0;
	if(!vje->use_mcast) {
		bytes_received = recv( sock_fd, vje->recv_buf, RECV_SIZE, 0 );
	}
	else
	{
		vj_proto **proto = (vj_proto**) vje->protocol;
		bytes_received = mcast_recv( proto[0]->r, (void*) vje->recv_buf, RECV_SIZE );
	}

	if( bytes_received <= 0 ) {
		if( bytes_received == -1 ) {
			veejay_msg(0, "Networking error with socket %d: %s",sock_fd,strerror(errno));
		}
		if( bytes_received == 0 ) {
			veejay_msg(0, "Socket %d closed connection, terminating client connection.");
		}
		return -1; // close client now
	}


	char *msg_buf = vje->recv_buf;
	int bytes_left = bytes_received;

	int n_msg = _vj_verify_msg( vje, id, msg_buf, bytes_left ); 

	if(n_msg == 0 && bytes_received > 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid instruction '%s'", msg_buf );
		return -1; //@ close client now
	}


	if( n_msg < VJ_MAX_PENDING_MSG )
	{
		int nn = _vj_parse_msg( vje, id, msg_buf, bytes_left,0 );
		if(nn != n_msg)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Veejay's message queue corrupted (end session!)");
			return 0; 
		}
		return nn;
	} else {
		veejay_msg(VEEJAY_MSG_ERROR, "Too many messages in queue. Abort client");
		return -1;
	}
	return 0;
}

void vj_server_shutdown(vj_server *vje)
{
	int j,i;
	vj_link **Link = (vj_link**) vje->link;
	int k = VJ_MAX_CONNECTIONS;

	if(vje->use_mcast) k = 1;
    
	for(i=0; i < k; i++)
	{
		if(Link[i]->in_use) 
			close(Link[i]->handle);
		for( j = 0; j < VJ_MAX_PENDING_MSG; j ++ )
		{
			if(Link[i]->m_queue[j]->msg )
				free( Link[i]->m_queue[j]->msg );
			//if(Link[i]->m_queue[j] ) free( Link[i]->m_queue[j] );
		}
		if( Link[i]->lin_queue)
			free( Link[i]->lin_queue );
	
		if( Link[i]->m_queue ) free( Link[i]->m_queue );
		if( Link[i] ) free( Link[i] );
    	}

	if(!vje->use_mcast)	
	{
	    close(vje->handle);
	}
	else
	{
		vj_proto **proto = (vj_proto**) vje->protocol;
		mcast_close_sender( proto[0]->s );
		mcast_close_receiver( proto[0]->r );
		if(proto[0])
			free(proto[0]);
		if(proto) free(proto);
	}

	if( vje->recv_buf )
		free(vje->recv_buf);

	free(Link);
	if(vje) free(vje);
}

int	vj_server_link_used( vj_server *vje, int link_id)
{
	vj_link **Link = (vj_link**) vje->link;
#ifdef STRICT_CHECKING
	assert( link_id >= 0 && link_id < VJ_MAX_CONNECTIONS );
#endif
	return Link[link_id]->in_use;
/*
	if( link_id < 0 || link_id >= VJ_MAX_CONNECTIONS )
		return 0;
   	if (Link[link_id]->in_use)
		return 1;
	return 0;*/
}


int	vj_server_min_bufsize( vj_server *vje, int id )
{
	vj_link **Link = (vj_link**) vje->link;
   	if (!Link[id]->in_use)
		return 0;

	int index = Link[id]->n_retrieved;
	if( index >= Link[id]->n_queued )
		return 0; // done

	return Link[id]->m_queue[index]->len;
}


char *vj_server_retrieve_msg(vj_server *vje, int id, char *dst, int *str_len )
{
	vj_link **Link = (vj_link**) vje->link;
   	if (!Link[id]->in_use)
		return NULL;

	int index = Link[id]->n_retrieved;

	if( index >= Link[id]->n_queued )
		return NULL; // done

	char *msg = Link[id]->m_queue[index]->msg;
#ifdef STRICT_CHECKING
	assert( msg != NULL );
#endif
	int len = Link[id]->m_queue[index]->len;

#ifdef STRICT_CHECKING
	veejay_msg(VEEJAY_MSG_DEBUG, "L%2d queue [%4d]/[%d] [%4d] '%s'",
			id, index, Link[id]->n_queued, len, msg );
			
#endif

	index ++;
	Link[id]->n_retrieved = index;

	*str_len = len;
	return msg;
}

