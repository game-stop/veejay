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
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <errno.h>

#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>

#include <libvjmsg/vj-msg.h>
#include <veejaycore/veejaycore.h>
#include <libvjmem/vjmem.h>
#include <libvjnet/mcastreceiver.h>
#include <libvjnet/mcastsender.h>
#include <libvjnet/vj-server.h>
#include <libvjnet/cmd.h>


#define __INVALID 0
#define __SENDER 1
#define __RECEIVER 2

typedef struct
{
	char *msg;
	int   len;
    int   allocated;
} vj_message;

typedef struct {
	int handle;
	int in_use;
	int promote;
	vj_message **m_queue;
	vj_message *lin_queue;
	int n_queued;
	int n_retrieved;
	void *pool;
} vj_link;

typedef struct
{
	mcast_sender	*s;  // for sending frames only
	mcast_receiver	*r; // for taking commands
	int type;
} vj_proto;

static int default_timeout_sec = 10;

#define VJ_MAX_PENDING_MSG 128
#define RECV_SIZE 4096 
#define MSG_POOL_SIZE (VJ_MAX_PENDING_MSG * 1024)

static	void	printbuf( FILE *f, uint8_t *buf , int len )
{
	int i;
	for( i = 0; i < len ; i ++ ) {
		if( (i%32) == 0 ) {
			fprintf(f, "\n");
		}
		fprintf(f, "%02x ", buf[i]);
	}
	fprintf(f, "\ntext:\n");
	for( i = 0; i < len ; i ++ ) {
		if( (i%32) == 0 ) {
			fprintf(f, "\n");
		}
		fprintf(f, "%c ", buf[i]);
	}

	fprintf(f, "\n");
}
int		_vj_server_free_slot(vj_server *vje);
int		_vj_server_new_client(vj_server *vje, int socket_fd);
int		_vj_server_parse_msg(vj_server *vje,int link_id, char *buf, int buf_len, int priority );
static  void _vj_put_kf_msg(vj_server *vje, int link_id, char *buf, int buf_len, int num_msg);

static void		_vj_server_empty_queue(vj_server *vje, int link_id);
/*
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
	// Inactive
	char request[128];
	snprintf(request,sizeof(request),"GET /veejay-15 HTTP/1.1\nHost: www.veejayhq.net\nReferrer: http://");

	//@ knock veejay.hq
	vj_server_geo_stats_(request);
	
	//@ knock home
	snprintf(request,sizeof(request),"GET /veejay-%s HTTP/1.1\nHost: c0ntrol.dyndns.org\n",VERSION );
	vj_server_geo_stats_(request);

}
*/

void	vj_server_geo_stats()
{
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
		if(!proto[0]->s) {
			free(proto);
			return 0;
		}

		proto[0]->r = mcast_new_receiver( group_name , port + VJ_CMD_MCAST_IN );
		if(!proto[0]->r ) {
			free(proto[0]->s);
			free(proto[0]);
			free(proto);
			return 0;
		}

		v->ports[0] = port + VJ_CMD_MCAST;
		v->ports[1] = port + VJ_CMD_MCAST_IN;
	}

	v->protocol = (void**) proto;
	link = (vj_link **) vj_malloc(sizeof(vj_link *) * VJ_MAX_CONNECTIONS);

	if(!link)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Out of memory");
		if(proto[0]) {
			if(proto[0]->s) free(proto[0]->s);
			if(proto[0]->r) free(proto[0]->r);
			free(proto[0]);
			free(proto);
		}
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
	veejay_memset(&(vjs->myself.sin_zero), 0, sizeof(vjs->myself.sin_zero));
	
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

	if(vjs->logfd) {
		fprintf( vjs->logfd, "selected port %d, maximum connections is %d", port_num, VJ_MAX_CONNECTIONS );
	}

	int send_size = 1024 * 1024;
	if( setsockopt( vjs->handle, SOL_SOCKET, SO_SNDBUF, (const char*) &send_size, sizeof(send_size) ) == - 1)
	{
		veejay_msg(0, "Cannot set send buffer size: %s", strerror(errno));
	}
	unsigned int tmp = sizeof(int);
	if( getsockopt( vjs->handle, SOL_SOCKET, SO_SNDBUF,(unsigned char*) &(vjs->send_size), &tmp) == -1 )
	{
		veejay_msg(0, "Cannot read socket buffer size: %s", strerror(errno));
		return 0;
	}
	if(vjs->logfd) {
		fprintf( vjs->logfd, "socket send buffer size is %d bytes", vjs->send_size );
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
	if(vjs->logfd) {
		fprintf( vjs->logfd, "socket recv buffer size is %d bytes", vjs->recv_size );
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "Port: %d [ receive buffer is %d bytes, send buffer is %d bytes ]", port_num, vjs->recv_size, vjs->send_size );

	int flag = 1;
	if( setsockopt( vjs->handle, IPPROTO_TCP, TCP_NODELAY, (char*) &flag, sizeof(int)) == -1 )
	{
		veejay_msg(0, "Cannot disable Nagle buffering algorithm: %s", strerror(errno));
		return 0;
	}

	struct timeval timeout;
	timeout.tv_sec = default_timeout_sec;
	timeout.tv_usec = 0;

	if( setsockopt( vjs->handle, SOL_SOCKET, SO_RCVTIMEO, (char*) &timeout, sizeof(timeout) ) == -1)
	{
		veejay_msg(0, "Cannot set receive timeout");
	}

	if( setsockopt( vjs->handle, SOL_SOCKET, SO_SNDTIMEO, (char*) &timeout, sizeof(timeout) ) == -1)
	{
		veejay_msg(0,"Cannot set send timeout");
	}

	link = (vj_link **) vj_malloc(sizeof(vj_link *) * VJ_MAX_CONNECTIONS);
	if(!link)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Server: Out of memory");
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
	if(vjs->logfd) {
		fprintf( vjs->logfd, "allocated queue for max %d connctions", VJ_MAX_CONNECTIONS );
	}

	switch(vjs->server_type )
	{
		case V_STATUS:
			veejay_msg(VEEJAY_MSG_INFO,"TCP/IP unicast VIMS status socket ready at port %d (R:%d, S:%d)",
				port_num, vjs->recv_size, vjs->send_size );
			break;
		case V_CMD:
			veejay_msg(VEEJAY_MSG_INFO,"TCP/IP unicast VIMS control socket ready at port %d (R:%d, S:%d)",
				port_num, vjs->recv_size, vjs->send_size );
			break;
		default:
		break;
	}

	return 1;
}
vj_server *vj_server_alloc(int port_offset, char *mcast_group_name, int type, size_t buflen)
{
	vj_server *vjs = (vj_server *) vj_calloc(sizeof(struct vj_server_t));
	
	if (!vjs)
		return NULL;

	size_t bl = buflen;
	if( bl < RECV_SIZE ) {
		bl = RECV_SIZE; //@ Ensure that receive buffer is minimally 4kb
	}

	vjs->recv_bufsize = bl;
	vjs->recv_buf = (char*) vj_calloc(sizeof(char) * vjs->recv_bufsize);

	if(!vjs->recv_buf)
	{
		free(vjs);
		return NULL;
	}	

	vjs->server_type = type;

	char *str_rbufsize = getenv( "VEEJAY_SERVER_RECEIVE_BUFFER_SIZE" );
	if( str_rbufsize != NULL ) {
		vjs->recv_bufsize = atoi( str_rbufsize );
		free(vjs->recv_buf);
		vjs->recv_buf = (char*) vj_calloc(sizeof(char) * vjs->recv_bufsize );
		if( vjs->recv_buf == NULL ) {
			free(vjs);
			return NULL;
		}
		veejay_msg(VEEJAY_MSG_INFO, "Changed receive buffer size to %d bytes (%2.2fKb)",
				vjs->recv_bufsize, (float) vjs->recv_bufsize / 1024.0f );
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG,"env VEEJAY_SERVER_RECEIVE_BUFFER_SIZE=[num bytes] not set");
	}

	char *netlog = getenv("VEEJAY_LOG_NET_IO" );

	if( netlog != NULL && strncasecmp("ON",netlog, 2) == 0 ) {
		char logpath[1024];
		sprintf( logpath, "%s.%d", VEEJAY_SERVER_LOG, port_offset );
		vjs->logfd       = fopen( logpath, "w" );
		if(!vjs->logfd) {
			veejay_msg(VEEJAY_MSG_WARNING, "Unable to open %s for logging Network I/O\n", VEEJAY_SERVER_LOG );
			vjs->logfd=0;
		}
		else {
			fprintf( vjs->logfd, "Server setup: port %d, name %s type %d\n", port_offset,mcast_group_name,type);
			fprintf( vjs->logfd, "receive buffer size: %d bytes\n", vjs->recv_bufsize);
		}
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_LOG_NET_IO=logfile not set");
	}

	char *timeout = getenv("VEEJAY_NET_TIMEOUT");
	if(timeout != NULL ) {
		default_timeout_sec = atoi(timeout);
		if( default_timeout_sec < 0 || default_timeout_sec > 65535 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "env VEEJAY_NET_TIMEOUT value must be between 0 and 65535" );
			default_timeout_sec = 10;
		}
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG,"env VEEJAY_NET_TIMEOUT=seconds not set");
	}
	
	if( mcast_group_name != NULL )
	{	/* setup multicast socket */
		vjs->use_mcast = 1;
		if ( _vj_server_multicast(vjs, mcast_group_name, port_offset) )
			return vjs;
	}
	
	/* setup peer to peer socket */
	vjs->use_mcast = 0;
	if ( _vj_server_classic( vjs,port_offset ) )
		return vjs;

	//running in mcast mode
	free( vjs->recv_buf );
	free( vjs );

 	return NULL;
}


int	vj_server_link_can_write( vj_server *vje, int link_id );

int vj_server_send( vj_server *vje, int link_id, uint8_t *buf, int len )
{
	unsigned int total = 0;
	unsigned int bytes_left = len;
	vj_link **Link = (vj_link**) vje->link;
	
	if( !Link[link_id]->in_use ) { 
		return 0;
	}


	if( !vj_server_link_can_write( vje,link_id ) ) {
		veejay_msg(0,"Not ready for sending");
		if( vje->logfd ) {
			fprintf(vje->logfd, "failed to send buf of len %d to link_id %d, not ready for writing!\n", len, link_id );
			printbuf(vje->logfd,buf,len);
		}
		return -1;
	}

	if( !vje->use_mcast)
	{
		total  = sock_t_send_fd( Link[link_id]->handle, vje->send_size, buf, len);
		if( vje->logfd ) {
			fprintf(vje->logfd, "sent %d of %d bytes to handle %d (link %d) %s\n", total,len, Link[link_id]->handle,link_id,(char*)(inet_ntoa(vje->remote.sin_addr)) );
			printbuf( vje->logfd, buf, len );
		}
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
		if( vje->server_type == V_CMD ) {
			return mcast_send( proto[0]->s, buf, bytes_left, vje->ports[0] );
		}
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

	vj_link **Link = (vj_link**) vje->link;

	/* write size of data to header */
	char hdr_buf[16];
	snprintf(hdr_buf, sizeof(hdr_buf), "F%08dD", len );
	if( sock_t_send_fd( Link[link_id]->handle, vje->send_size, (unsigned char*) hdr_buf, 10 ) <= 0 ) {
		veejay_msg(0, "Unable to send header to %s: %s", (char*)(inet_ntoa(vje->remote.sin_addr)),strerror(errno));
		return 0;
	}

	total  = sock_t_send_fd( Link[link_id]->handle, vje->send_size, buf, len);
	if( vje->logfd ) {
		fprintf(vje->logfd, "sent frame %d of %d bytes to handle %d (link %d) %s\n", total,len, Link[link_id]->handle,link_id,(char*)(inet_ntoa(vje->remote.sin_addr)) );
	}
		
	if( total <= 0 )
	{
		veejay_msg(0,"Unable to send buffer to %s: %s",
			(char*)(inet_ntoa(vje->remote.sin_addr)),strerror(errno));
		return 0;
	}

   	return total;
}

int		vj_server_send_frame( vj_server *vje, int link_id, uint8_t *buf, int len, VJFrame *frame )
{
	if(!vje->use_mcast )
	{
		if( vj_server_link_can_write( vje, link_id ))
		{
			return vj_server_send_frame_now( vje, link_id, buf, len );
		} 
		else {
			veejay_msg(VEEJAY_MSG_ERROR, "Link %d's socket not ready for immediate send: %s", link_id, strerror(errno));
		}
		return 0;
	}
	else
	{
		vj_proto **proto = (vj_proto**) vje->protocol;
		if( vje->server_type == V_CMD  )
			return mcast_send_frame( proto[0]->s, frame, buf,len,vje->ports[0],vje->mcast_gray );
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
	if( vje->logfd ) {
		fprintf(vje->logfd, "new socket %d (link %d)\n", socket_fd,entry );
	}
		
    return entry;
}

int _vj_server_del_client(vj_server * vje, int link_id)
{
	vj_link **Link = (vj_link**) vje->link;
	if(!Link[link_id]->in_use) {
		return 0;
	}

	Link[link_id]->in_use = 0;

	if(Link[link_id]->handle > 0)
	{
		int res = close(Link[link_id]->handle);
		if( res == -1 ) {
			veejay_msg(0,"Error closing connection with socket %d:%s", Link[link_id]->handle, strerror(errno));
		}

		FD_CLR( Link[link_id]->handle, &(vje->fds) );
		FD_CLR( Link[link_id]->handle, &(vje->wds) );
		if( vje->logfd ) {
			fprintf(vje->logfd, "closing link %d\n",link_id );
		}

	}
	Link[link_id]->handle = -1;
	Link[link_id]->promote = 0;
	Link[link_id]->n_queued = 0;
	Link[link_id]->n_retrieved = 0;

	if( Link[link_id]->pool ) {
		vj_simple_pool_free( Link[link_id]->pool );
		Link[link_id]->pool = NULL;
	}

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
	Link[link_id]->promote = 1;	
	if( vje->logfd ) {
		fprintf(vje->logfd, "promote link %d\n", link_id );
	}

}


int vj_server_poll(vj_server * vje)
{
	int status = 0;
	struct timeval t = (struct timeval) { 0 };
	int i;

	if( vje->use_mcast )
	{
		vj_proto **proto = (vj_proto**) vje->protocol;
		return mcast_poll( proto[0]->r );
	}

	FD_ZERO( &(vje->fds) );
    	FD_ZERO( &(vje->wds) );

	FD_SET( vje->handle, &(vje->fds) );
	FD_SET( vje->handle, &(vje->wds) );

	for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
	{
		vj_link **Link= (vj_link**) vje->link;
	    if( Link[i]->handle <= 0 || !Link[i]->in_use )
			continue;	
		FD_SET( Link[i]->handle, &(vje->fds) );
		FD_SET( Link[i]->handle, &(vje->wds) );	
		FD_SET( Link[i]->handle, &(vje->eds) );
	}		

	status = select(vje->nr_of_connections + 1, &(vje->fds), &(vje->wds), NULL, &t);

	if( status == -1 ) {
		veejay_msg(0, "Error while polling socket: %s", strerror(errno));
	} 

	return status;
}

static void	_vj_server_empty_queue(vj_server *vje, int link_id)
{
	vj_link **Link = (vj_link**) vje->link;
	
	veejay_memset( Link[link_id]->lin_queue, 0, sizeof(vj_message) * VJ_MAX_PENDING_MSG );

	vj_simple_pool_reset( Link[link_id]->pool );

    int n = Link[link_id]->n_queued;
    int i;
    for( i = 0; i < n; i ++ ) {
        if( Link[link_id]->m_queue[i]->allocated )
            free( Link[link_id]->m_queue[i]->msg );
    }

	Link[link_id]->n_queued = 0;
	Link[link_id]->n_retrieved = 0;

}

void		vj_server_init_msg_pool(vj_server *vje, int link_id )
{
	vj_link **Link = (vj_link**) vje->link;
	if( Link[link_id]->pool == NULL ) {
		Link[link_id]->pool = vj_simple_pool_init( MSG_POOL_SIZE * sizeof(char) );
	}
}

static  void _vj_put_kf_msg(vj_server *vje, int link_id, char *buf, int buf_len, int num_msg)
{
  	vj_link **Link = (vj_link**) vje->link;
	vj_message **v = Link[link_id]->m_queue;

    v[num_msg]->msg = buf;
    v[num_msg]->len = buf_len;
}

int	vj_server_new_connection(vj_server *vje)
{
	if( FD_ISSET( vje->handle, &(vje->fds) ) )
	{
		unsigned int addr_len = sizeof(vje->remote);
		int n = 0;
		int fd = accept( vje->handle, (struct sockaddr*) &(vje->remote), &addr_len );
		if(fd == -1)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error accepting connection: %s",
						strerror(errno));

			return 0;
		}	

		char *host = inet_ntoa( vje->remote.sin_addr ); 

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

		veejay_msg(VEEJAY_MSG_INFO, "Link: %d connected with %s on port %d", n,host,vje->remote.sin_port);		

		if( vje->logfd ) {
			fprintf(vje->logfd, "new connection, socket=%d, max connections=%d\n",
				fd, vje->nr_of_connections );
		}

		FD_CLR( fd, &(vje->fds) );
		return 1;
	}
	return 0;
}

static void vj_server_flush(int sock_fd)
{
	char buffer[RECV_SIZE];
	int n;
	
flushmore_lbl:
	n = recv( sock_fd, buffer, RECV_SIZE, 0 ); //@ read 4 kb
	if( n == - 1) {
		if( errno == EAGAIN ) {
			goto flushmore_lbl;
		}
		veejay_msg( 0, "Error: %s", strerror(errno) );
		return;		
	}

	if( n == RECV_SIZE && errno == EAGAIN) //@ read 4kb, but still data left
	{ 
		goto flushmore_lbl;
	}

}

#define V_TYPE_VIMS_DATA 1
#define V_TYPE_VIMS_VIDEO 2
#define V_TYPE_VIMS_KF 3
#define KF_HEADER_LEN 8
#define VIMS_HEADER_LEN 4

static  void vj_server_log_msg(vj_server *vje, int link_id, char *buf, int buf_len)
{ 
  	vj_link **Link = (vj_link**) vje->link;
		
    fprintf(vje->logfd, "received %d bytes from handle %d (link %d)\n", buf_len,Link[link_id]->handle, link_id );
    printbuf( vje->logfd, (uint8_t*) buf, buf_len );
}

static int vj_server_socket_consume(vj_server *vje, int sock_fd, int link_id, char *buffer, int buf_size, int flag)
{
    ssize_t n = 0;
    
    if(!vje->use_mcast) {
        n = recv( sock_fd, buffer, buf_size, flag );
    } else {
        vj_proto **proto = (vj_proto**) vje->protocol;
        n = mcast_recv( proto[0]->r, (void*) buffer, buf_size );
    }
	
    if( n == - 1) {
		if( errno == EAGAIN ) {
            return 0;
        }
	    veejay_msg( 0, "Error: %s", strerror(errno) );
	    return -1;
    }
    else if ( n == 0 ) {
        veejay_msg( 0, "Remote has disconnected");
        return -1;
    }

    if( vje->logfd ) {
        vj_server_log_msg( vje, link_id, buffer, buf_size );
    }
    return (int) n;
}


static int vj_server_update_get_msg_kf(vj_server *vje, int sock_fd, int link_id, int *num_msg)
{
    char t_hdr[KF_HEADER_LEN];
    int buf_size = vj_server_socket_consume( vje, sock_fd, link_id, t_hdr, KF_HEADER_LEN, 0 );
    if( buf_size <= 0 )
        return -1;

    // KF messages have no null termination, their payload is packed ints

    if(sscanf(t_hdr, "%8d", &buf_size ) != 1 ) {
        veejay_msg(VEEJAY_MSG_ERROR, "VIMS K-message is corrupted");
        return -1;
    }
    
    char *buf = (char*) vj_calloc( sizeof(char) * buf_size );
    if(!buf) {
        veejay_msg(VEEJAY_MSG_ERROR, "Out of memory error");
        return -1;
    }

    int msg_size = vj_server_socket_consume( vje, sock_fd, link_id, buf, buf_size, MSG_WAITALL );
    if( msg_size <= 0 ) {
	free(buf);
        return -1;
    }

    _vj_put_kf_msg(vje, link_id, buf, msg_size, *num_msg);

    *num_msg = *num_msg + 1;
    
    return 1;
}

static int vj_server_update_get_msg_vd(vj_server *vje, int sock_fd, int link_id, int *num_msg)
{
	vj_link **Link = (vj_link**) vje->link;
    char v_hdr[VIMS_HEADER_LEN];
    int buf_size = vj_server_socket_consume( vje, sock_fd, link_id, v_hdr, VIMS_HEADER_LEN, 0 );
    if( buf_size <= 0)
        return -1;

    if( sscanf(v_hdr, "%3dD", &buf_size) != 1 ) {
        veejay_msg(VEEJAY_MSG_ERROR, "VIMS V-message is corrupted");
        return -1;
    }

    int msg_buf_size = buf_size + 1; // messages are null terminated but not sent with null termination

    char *buf = (char*) vj_simple_pool_alloc( Link[link_id]->pool, msg_buf_size );
    if(!buf) {
        veejay_msg(VEEJAY_MSG_ERROR, "Not enough space in pool to queue received VIMS message");
        return -1;
    }
    
    int msg_size = vj_server_socket_consume( vje, sock_fd, link_id, buf, buf_size, MSG_WAITALL );
    if( msg_size <= 0) {
        return -1;
    }

    int net_id = 0;
    if( sscanf(buf, "%d:", &net_id ) != 1 ) {
        veejay_msg(VEEJAY_MSG_ERROR,"Expected VIMS identifier followed by semicolon");
        return -1;
    }  

    if( buf[ msg_size - 1] != ';' ) {
       veejay_msg(VEEJAY_MSG_ERROR, "Expected VIMS message end marker ';'");
       return -1;
    } 

    buf[ msg_size ] = '\0';

    _vj_put_kf_msg(vje, link_id, buf, msg_size, *num_msg);

    *num_msg = *num_msg + 1;

    return 1;
}

static int vj_server_update_get_msg_type(vj_server *vje, int sock_fd, int link_id)
{
    char ptr[1];

    int result = vj_server_socket_consume( vje, sock_fd, link_id, ptr, 1, MSG_DONTWAIT );
    if( result == -1 )
        return -1;
    if( result == 0 )
        return 0;

    switch( ptr[0] ) {
        case 'D':
            return V_TYPE_VIMS_DATA;
        case 'V':
            return V_TYPE_VIMS_VIDEO;
        case 'K':
            return V_TYPE_VIMS_KF;
   }

   veejay_msg(VEEJAY_MSG_ERROR, "Expected VIMS header token D,V or K"); 

   return -1; 
}

static int vj_server_msg_pending(vj_server *vje, int id)
{
	int sock_fd = vje->handle;
	vj_link **Link = (vj_link**) vje->link;

	if(!Link[id]->in_use)
		return 0;

    if(!vje->use_mcast)
	{
		sock_fd = Link[id]->handle;

		if(!FD_ISSET( sock_fd, &(vje->fds)) )
			return 0; //@ nothing to receive, fall through
	}

    return sock_fd;
}

static int vj_server_queue_vims_messages( vj_server *vje, int sock_fd, int id )
{
	vj_link **Link = (vj_link**) vje->link;
    int msg_type = -1;
    int num_msg = 0;

    // consume the whole buffer and prepare the message queue
    while( (msg_type=vj_server_update_get_msg_type(vje,sock_fd, id)) > 0 ) {

        if( num_msg >= VJ_MAX_PENDING_MSG ) {
            veejay_msg(VEEJAY_MSG_ERROR, "Queue too small to handle all VIMS messages");
            vj_server_flush(sock_fd);
            goto gremlin_pool;
        }

        switch( msg_type ) {
            case V_TYPE_VIMS_KF:
                if( vj_server_update_get_msg_kf(vje,sock_fd, id, &num_msg) == -1 )
                    goto gremlin_pool;
                break;
            case V_TYPE_VIMS_DATA:
            case V_TYPE_VIMS_VIDEO:
                if( vj_server_update_get_msg_vd(vje,sock_fd, id, &num_msg) == -1 )
                  goto gremlin_pool;
                break;
        }
    }     

gremlin_pool:
        
	Link[id]->n_queued = num_msg;
	Link[id]->n_retrieved = 0;

    return num_msg;
}

int	vj_server_update( vj_server *vje, int id )
{
	vj_link **Link = (vj_link**) vje->link;
	if( Link[id]->pool )
		_vj_server_empty_queue(vje, id);

    int sock_fd = vj_server_msg_pending( vje,id );
    if( sock_fd == 0 )
        return 0;

    return vj_server_queue_vims_messages(vje, sock_fd, id );
}

void vj_server_shutdown(vj_server *vje)
{
	int i;
	vj_link **Link = (vj_link**) vje->link;
	int k = VJ_MAX_CONNECTIONS;

	if( vje->logfd ) {
		fclose( vje->logfd );
	}

	if(vje->use_mcast) k = 1;
    
	for(i=0; i < k; i++)
	{
		if( Link[i]->in_use) 
			close(Link[i]->handle);
		if( Link[i]->pool)
			vj_simple_pool_free( Link[i]->pool );
		if( Link[i]->lin_queue)
			free( Link[i]->lin_queue );
		if( Link[i]->m_queue )
			free( Link[i]->m_queue );
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

	free(vje->link);
	free(vje);
}

int	vj_server_link_used( vj_server *vje, int link_id)
{
	vj_link **Link = (vj_link**) vje->link;
	return Link[link_id]->in_use;
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
	int len = Link[id]->m_queue[index]->len;

	index ++;
	Link[id]->n_retrieved = index;

	*str_len = len;

	//msg is in pool, no need to free it
	return msg;
}


char *vj_server_my_ip()
{
	struct addrinfo h;

	char hostname[512];
	if( gethostname(hostname,sizeof(hostname)) < 0 ) {
		return NULL;
	}

	const char *target = "8.8.8.8"; //google public dns
	const char *port = "53";

	veejay_memset(&h,0,sizeof(h));
	h.ai_family = AF_INET;
	h.ai_socktype = SOCK_STREAM;

	struct addrinfo* info;
	int ret = 0;
	if((ret = getaddrinfo( target, port, &h, &info )) != 0 ) {
		return NULL;
	}

	if( info->ai_family == AF_INET6 ) {
		return NULL;
	}

	int sock = socket( info->ai_family, info->ai_socktype, info->ai_protocol);
	if( sock <= 0 )
		return NULL;

	if( connect(sock, info->ai_addr, info->ai_addrlen ) < 0 ) {
		close(sock);
		return NULL;
	}

	struct sockaddr_in local;
	veejay_memset(&local,0,sizeof(local));
	socklen_t len = sizeof(local);
	if( getsockname( sock, (struct sockaddr*)&local, &len ) < 0 ) {
		close(sock);
		return NULL;
	}

	char tmp[INET_ADDRSTRLEN ];
	if( inet_ntop( local.sin_family, &(local.sin_addr), tmp, sizeof(tmp))==NULL) {
		return NULL;
	}

	close(sock);

	return vj_strdup(tmp);
}
