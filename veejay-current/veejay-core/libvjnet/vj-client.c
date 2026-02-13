/* libvjnet - Linux VeeJay
 * 	     (C) 2002-2016 Niels Elburg <nwelburg@gmail.com> 
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
#include <sys/types.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <veejaycore/defs.h>
#include <libvjnet/vj-client.h>
#include <veejaycore/avhelper.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvjnet/cmd.h>
#include <libvjnet/mcastreceiver.h>
#include <libvjnet/mcastsender.h>
#include <libavutil/pixfmt.h>
#include <pthread.h>
#include <veejaycore/avcommon.h>
#define VJC_OK 0
#define VJC_NO_MEM 1
#define VJC_SOCKET 2
#define VJC_BAD_HOST 3

extern int get_ffmpeg_pixfmt( int p);

vj_client *vj_client_alloc(void)
{
	vj_client *v = (vj_client*) vj_calloc(sizeof(vj_client));
	if(!v)
	{
		return NULL;
	}
	v->decoder = NULL;
	return v;
}

vj_client *vj_client_alloc_stream(VJFrame *output) 
{
	vj_client *v = (vj_client*) vj_calloc(sizeof(vj_client));
	if(!v)
	{
		return NULL;
	}

	v->decoder = avhelper_get_mjpeg_decoder(output);
	if(v->decoder == NULL) {
		veejay_msg(0,"Failed to initialize MJPEG decoder");
		free(v);
		return NULL;
	}
	return v;
}

void		vj_client_free(vj_client *v)
{
	if(v)
	{
		if( v->fd[0] ) {
			sock_t_free( v->fd[0] );
		} 
		if( v->fd[1] ) {
			sock_t_free( v->fd[1] );
		}

		if( v->decoder ) {
			avhelper_close_decoder( v->decoder );
		}

		v->fd[0] = NULL;
		v->fd[1] = NULL;

		free(v);
		v = NULL;
	}
}

int	vj_client_window_sizes( int socket_fd, int *r, int *s )
{
	unsigned int tmp = sizeof(int);
	if( getsockopt( socket_fd, SOL_SOCKET, SO_SNDBUF,(unsigned char*) s, &tmp) == -1 ) {
		veejay_msg(0, "Cannot read socket buffer size: %s", strerror(errno));
		return 0;
	} 
    	if( getsockopt( socket_fd, SOL_SOCKET, SO_RCVBUF, (unsigned char*) r, &tmp) == -1 )
    	{
         	veejay_msg(0, "Cannot read socket buffer receive size %s" , strerror(errno));
        	 return 0;
    	}
	return 1;
}

int vj_client_connect_dat(vj_client *v, char *host, int port_id  )
{
	int error = 0;
	if(host == NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid host name (cannot be empty)");
		return 0;
	}
	if(port_id < 1 || port_id > 65535)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid port number. Use [1-65535]");
		return 0;
	}

	v->fd[0]   = alloc_sock_t();

	if( sock_t_connect( v->fd[0], host, (port_id + 5)  ) )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Connect to DAT port %d", port_id + 5);	
		return 1;
	}

	v->fd[1] = NULL; // no status port
	v->ports[0] = port_id + 5;
	v->ports[1] = -1;

	return error;
}

int vj_client_connect(vj_client *v, char *host, char *group_name, int port_id  )
{
	int error = 0;
	if(port_id <= 0 || port_id > 65535)
	{
		veejay_msg(0, "Invalid port number '%d'", port_id );
		return error;
	}

	if( group_name == NULL )
	{
		if(host == NULL)
			return error;

		v->fd[0] = alloc_sock_t();
		if(!v->fd[0]) {
			return error;
		}
		v->fd[1] = alloc_sock_t();
		if(!v->fd[1] ) {
			free(v->fd[0]);
			v->fd[0] = NULL;
			return error;
		}

		if( sock_t_connect( v->fd[0], host, port_id + VJ_CMD_PORT ) ) {
			if( sock_t_connect( v->fd[1], host, port_id + VJ_STA_PORT ) ) {	
				v->ports[0] = port_id + VJ_CMD_PORT;
				v->ports[1] = port_id + VJ_STA_PORT;
				return 1;

			} else {
				veejay_msg(0, "Failed to connect to status port");
			}
		} else {
			veejay_msg(0, "Failed to connect to command port");
		}

		free(v->fd[0]);
		free(v->fd[1]);
		v->fd[0] = NULL;
		v->fd[1] = NULL;
	}
	else
	{
		v->r    = mcast_new_receiver( group_name, port_id + VJ_CMD_MCAST ); 
		if(!v->r ) {
			veejay_msg(0 ,"Unable to setup multicast receiver on group %s", group_name );
			return error;
		}

		v->s    = mcast_new_sender( group_name );
		if(!v->s ) {
			veejay_msg(0, "Unable to setup multicast sender on group %s", group_name );
			return error;
		}
		v->ports[0] = port_id + VJ_CMD_MCAST;
		v->ports[1] = port_id + VJ_CMD_MCAST_IN;

		v->mcast = 1;

		return 1;
	}
	return error;
}

int	vj_client_link_can_write( vj_client *v, int sock_type ){
	return sock_t_wds_isset( v->fd[sock_type]);
}

int	vj_client_link_can_read( vj_client *v, int sock_type ) {
	return sock_t_rds_isset( v->fd[sock_type] );
}

int	vj_client_poll( vj_client *v, int sock_type )
{
	if( v->mcast )
		return mcast_poll( v->r );

	return sock_t_poll( v->fd[sock_type ]);
}

void vj_client_rescale_video( vj_client *v, uint8_t *data[4] ) {
	
	if(v->decoder == NULL) {
		veejay_msg(0, "No decoder initialized");
		return;
	}

	avhelper_rescale_video( v->decoder, data );		
}

int	vj_client_read_mcast_data( vj_client *v, int max_len )
{
	if( v->space == NULL ) {
		v->space = vj_calloc( sizeof(uint8_t) * max_len );
		if(v->space == NULL)
			return 0;
	}

	if( v->decoder == NULL ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "MJPEG decoder is not initialized");
		return 0;
	}

	int space_len = 0;
	int hdr_len = 0;
	uint8_t *space = mcast_recv_frame( v->r, &space_len, &hdr_len, v->space );

	if( space_len <= 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Nothing received from network");
		return 0;
	}

	return avhelper_decode_video( v->decoder, space, space_len );
}

int vj_client_read_frame_hdr( vj_client *v )
{
	char header[16];
	memset(header,0,sizeof(header));
	int n = sock_t_recv( v->fd[0], header, 10 );
	if( n <= 0 ) {
		if( n == -1 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Error '%s' while reading socket", strerror(errno));
		} else {
			veejay_msg(VEEJAY_MSG_DEBUG,"Remote closed connection");
		}
		return n;
	}
	
	int data_len = 0;
	if( n != 10 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Bad header");
		return -1;
	}

	if(sscanf(header, "F%08dD", &data_len ) != 1 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Expected header information");
		return -1;
	}
	
	return data_len;
}

int vj_client_read_frame_data( vj_client *v, int datalen)
{
	if(v->space_len < datalen || v->space == NULL) {
		v->space_len = datalen;
		v->space = (uint8_t*) realloc( v->space, v->space_len );
		if(v->space == NULL)
		  return 0;
	}	

	int n = sock_t_recv( v->fd[0],v->space,datalen );
		
	if( n <= 0 || (n!=datalen)) {
		if( n == -1 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Error '%s' while reading socket", strerror(errno));
		} else if( n == 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG,"Remote closed connection");
		} else {
            veejay_msg(VEEJAY_MSG_ERROR, "Incomplete frame received from remote");
        }
		return 0;
	}

	return avhelper_decode_video_buffer( v->decoder, v->space, n );
}

int	vj_client_read_no_wait(vj_client *v, int sock_type, uint8_t *dst, int bytes )
{
	if( v->mcast )
		return mcast_recv( v->r, dst, bytes );

	return sock_t_recv( v->fd[ sock_type ], dst, bytes );
}

int	vj_client_read(vj_client *v, int sock_type, uint8_t *dst, int bytes )
{
	if( v->mcast )
		return mcast_recv( v->r, dst, bytes );

	return sock_t_recv( v->fd[ sock_type ], dst, bytes );
}

int vj_client_send_buf(vj_client *v, int sock_type,unsigned char *buf, int len) {
	if( v->mcast ) {
		return mcast_send( v->s, (void*) buf, len , v->ports[sock_type ] );
	}
	return sock_t_send( v->fd[ sock_type ], buf, len );
}

#define HDR_LEN 5

int vj_client_send(vj_client *v, int sock_type,unsigned char *buf) {
	
	int len = strlen( (const char*)buf);
	int ret = -1;
	unsigned char *blob = (unsigned char*) vj_malloc(sizeof(unsigned char) * (len + HDR_LEN));
	if(!blob) {
		veejay_msg(0, "Out of memory" );
		return -1;
	}

	snprintf( (char*) blob,(len+HDR_LEN), "V%03dD", len );
	memcpy( blob + HDR_LEN, buf, len );

	if( v->mcast ) {
		ret = mcast_send( v->s, (void*) blob, len + HDR_LEN, v->ports[sock_type ] );
	}
	else {
		ret = sock_t_send( v->fd[ sock_type ], blob, len + HDR_LEN );
	}

	free(blob);

	return ret;
}

void vj_client_close( vj_client *v )
{
	if( v->mcast == 1 ) {
		mcast_close_receiver( v->r );
		mcast_close_sender( v->s );
		v->s = NULL;
		v->r = NULL;
	} else {
		if( v->fd[0] ) {
			sock_t_close( v->fd[0] );
			sock_t_free( v->fd[0] );
			v->fd[0] = NULL;
		}
		if( v->fd[1] ) {
			sock_t_close( v->fd[1] );
			sock_t_free( v->fd[1] );
			v->fd[1] = NULL;
		}
	}

}

int	vj_client_test(char *host, int port)
{
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
