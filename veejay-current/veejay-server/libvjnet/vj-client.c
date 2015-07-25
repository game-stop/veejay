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
#include <sys/types.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <libvjnet/vj-client.h>
#include <veejay/vims.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvjnet/cmd.h>
#include <libvjnet/mcastreceiver.h>
#include <libvjnet/mcastsender.h>
#include <libavutil/pixfmt.h>
#include <pthread.h>
#include <liblzo/lzo.h>
#define VJC_OK 0
#define VJC_NO_MEM 1
#define VJC_SOCKET 2
#define VJC_BAD_HOST 3
#define RUP8(num)(((num)+8)&~8)
#define PACKET_LEN 4096

extern int get_ffmpeg_pixfmt( int p);

vj_client *vj_client_alloc( int w, int h, int f )
{
	vj_client *v = (vj_client*) vj_calloc(sizeof(vj_client));
	if(!v)
	{
		return NULL;
	}
	v->orig_width = w;
	v->orig_height = h;
	v->cur_width = w;
	v->cur_height = h;
	v->cur_fmt = get_ffmpeg_pixfmt(f);
	v->orig_fmt = v->cur_fmt;
	v->blob = (unsigned char*) vj_calloc(sizeof(unsigned char) * PACKET_LEN ); 
	if(!v->blob ) {
		veejay_msg(0, "Memory allocation error.");
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

		v->fd[0] = NULL;
		v->fd[1] = NULL;

		if(v->blob)
			free(v->blob);
		if(v->lzo)
			lzo_free(v->lzo);
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
		v->fd[1] = alloc_sock_t();
		if(!v->fd[0]) {
			return error;
		}
		if(!v->fd[1] ) {
			free(v->fd[0]);
			return error;
		}

		if( sock_t_connect( v->fd[0], host, port_id + VJ_CMD_PORT ) ) {
			if( sock_t_connect( v->fd[1], host, port_id + VJ_STA_PORT ) ) {		
				return 1;

			} else {
				veejay_msg(0, "Failed to connect to status port.");
			}
		} else {
			veejay_msg(0, "Failed to connect to command port.");
		}
		v->ports[0] = port_id + VJ_CMD_PORT;
		v->ports[1] = port_id + VJ_STA_PORT;
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

		mcast_sender_set_peer( v->s , group_name );
		v->mcast = 1;
//		mcast_receiver_set_peer( v->c[0]->r, group_name);
		veejay_msg(VEEJAY_MSG_DEBUG, "Client is interested in packets from group %s : %d, send to %d",
			group_name, port_id + VJ_CMD_MCAST , port_id + VJ_CMD_MCAST_IN);

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
	return sock_t_poll( v->fd[sock_type ]);
}

static	long	vj_client_decompress( vj_client *t,uint8_t *in, uint8_t *out, int data_len, int Y, int UV , int header_len,
		uint32_t s1, uint32_t s2, uint32_t s3)
{
	uint8_t *dst[3] = {
			out,
			out + Y,
			out + Y + UV };


	long total = lzo_decompress( t->lzo, in, data_len, dst, UV,s1,s2,s3 );
	if( total != (Y+UV+UV) )
		veejay_msg(0, "Error decompressing: expected %d bytes got %d.", (Y+UV+UV),total);

	return total;
}

/* packet negotation.
 * read a small portion (44 bytes for veejay, its veejay's full header size) 
 * and try to identify which software is sending frames
 *
 */

static	int	vj_client_packet_negotiate( vj_client *v, int *tokens )
{
	uint8_t line[44];

	veejay_memset( line,0, sizeof(line));
	int plen = sock_t_recv( v->fd[0], line, 44 );
	
	if( plen == 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Remote closed connection.");
		return -1;
	}

	if( plen < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Network I/O Error while reading header: %s", strerror(errno));
		return -1;
	}

	int n = sscanf( (char*) line, "%04d%04d%04d%08d%08d%08d%08d", 
			&tokens[0],
			&tokens[1],
			&tokens[2],
			
			&tokens[3],
			&tokens[4],
			&tokens[5],
			&tokens[6]);

	if( n != 7 ) {
		veejay_msg(0, "Unable to parse header data: '%s'", line );
		return 0;
	}

	if( tokens[3] > 0  ) {
		if( v->lzo == NULL ) {
			v->lzo = lzo_new();
		}
	}

	return 1;
}

int	vj_client_read_frame_header( vj_client *v, int *w, int *h, int *fmt, int *compr_len, int *stride1,int *stride2, int *stride3 )
{
	int	tokens[16];
	veejay_memset( tokens,0,sizeof(tokens));

	int result = vj_client_packet_negotiate( v, tokens );
	if( result == 0 ) { 
		return 0;
	}

	if( tokens[0] <= 0 || tokens[1] <= 0 ) {
		return 0;
	}

	*w = tokens[0];
	*h = tokens[1];
	*fmt=tokens[2];
	*compr_len=tokens[3];
	*stride1=tokens[4];
	*stride2=tokens[5];
	*stride3=tokens[6];
	v->in_width = *w;
	v->in_height = *h;
	v->in_fmt = *fmt;

	return 1;
}

int vj_client_read_frame_data( vj_client *v, int compr_len, int stride1,int stride2, int stride3, uint8_t *dst )
{
	int datalen = (compr_len > 0 ? compr_len : stride1+stride2+stride3);
	if( (compr_len > 0) && ( v->space == NULL || v->space_len < compr_len) ) {
		if( v->space ) {
			free(v->space);
			v->space = NULL;
		}
		v->space_len = RUP8( compr_len );
		v->space = vj_calloc(sizeof(uint8_t) * v->space_len );
		if(!v->space) {
			veejay_msg(0,"Could not allocate memory for network stream.");
			return 0;
		}
	}

	if( compr_len > 0 )  {
		int n = sock_t_recv( v->fd[0],v->space,datalen );
		if( n <= 0 ) {
			if( n == -1 ) {
				veejay_msg(VEEJAY_MSG_ERROR, "Error '%s' while reading socket", strerror(errno));
			} else {
				veejay_msg(VEEJAY_MSG_DEBUG,"Remote closed connection");
			}
			return 0;
		}

		if( n != compr_len && n > 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Broken video packet , got %d out of %d bytes",n, compr_len );	
			return 0;
		}

		return 2;
	}
	else {
		int n = sock_t_recv( v->fd[0], dst, datalen );
		if( n != (stride1 + stride2 + stride3)  )
		{
			return 0;
		}

		return 1;
	}
	return 0;
}

void vj_client_decompress_frame_data( vj_client *v, uint8_t *dst, int fmt, int w, int h, int compr_len, int stride1, int stride2, int stride3  )
{
	int y_len = w * h;
	int uv_len = 0;
	switch(fmt) //@ veejay is sending compressed YUV data, calculate UV size
	{
		case PIX_FMT_YUV422P:
		case PIX_FMT_YUVJ422P:
			uv_len = y_len / 2; 
			break;
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUVJ420P:
			uv_len = y_len / 4;
			break;
		default:
			uv_len = y_len;
			break;
	}
	

	//@ decompress YUV buffer
	vj_client_decompress( v, v->space, dst, compr_len, y_len, uv_len ,0, stride1,stride2,stride3);
}



uint8_t *vj_client_read_i( vj_client *v, uint8_t *dst, ssize_t *dstlen, int *ret )
{
	uint32_t p[4] = {0, 0,0,0 };
	uint32_t strides[4] = { 0,0,0,0 };

	int	tokens[16];

	int y_len = 0;
	int uv_len = 0;
	
	memset( tokens,0,sizeof(tokens));
	
	int result = vj_client_packet_negotiate( v, tokens );
	if( result == 0 ) { 
	  	veejay_msg(0, "Unable to read frame header.");
		*ret = -1;
		return dst;
	}
	p[0] = tokens[0]; //w
	p[1] = tokens[1]; //h
	p[2] = tokens[2]; //fmt
	p[3] = tokens[3]; //compr len
	strides[0] = tokens[4]; // 0
	strides[1] = tokens[5]; // 1
	strides[2] = tokens[6]; // 2
	v->in_width = p[0];
	v->in_height = p[1];
	v->in_fmt = p[2];
	if( p[0] <= 0 || p[1] <= 0 ) {
		veejay_msg(0, "Invalid values in network frame header");
		*ret = -1;
		return dst;
	}

	if( v->space == NULL || v->space_len < p[3] ) {
		if( v->space ) {
			free(v->space);
			v->space = NULL;
		}
		v->space_len = RUP8( p[3] );
		v->space = vj_calloc(sizeof(uint8_t) * v->space_len );
		if(!v->space) {
			veejay_msg(0,"Could not allocate memory for network stream.");
			*ret = -1;
			return dst;
		}
	}
		
	uv_len = 0;
	y_len = p[0] * p[1];

	if( p[3] > 0 )  {
		switch(v->in_fmt ) //@ veejay is sending compressed YUV data, calculate UV size
		{
			case PIX_FMT_YUV422P:
			case PIX_FMT_YUVJ422P:
				uv_len = y_len / 2; 
				break;
			case PIX_FMT_YUV420P:
			case PIX_FMT_YUVJ420P:
				uv_len = y_len / 4;break;
			default:
				uv_len = y_len;
				veejay_msg(VEEJAY_MSG_ERROR, "Unknown pixel format: %02x", v->in_fmt);
				*ret = -1;
				return dst;
				break;
		}
		int n = sock_t_recv( v->fd[0],v->space,p[3] );
		if( n <= 0 ) {
			if( n == -1 ) {
				veejay_msg(VEEJAY_MSG_ERROR, "Error '%s' while reading socket", strerror(errno));
			} else {
				veejay_msg(VEEJAY_MSG_DEBUG,"Remote closed connection");
			}
			*ret = -1;
			return dst;
		}

		if( n != p[3] && n > 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Broken video packet , got %d out of %d bytes",n, p[3] );	
			*ret = -1;
			return dst;
		}


		if( *dstlen < (y_len*3) || dst == NULL ) {
			dst = realloc( dst, RUP8( y_len * 3) );
			*dstlen = y_len  * 3;
		} 

		//@ decompress YUV buffer
		vj_client_decompress( v, v->space, dst, p[3], y_len, uv_len , 0, strides[0],strides[1],strides[2]);
		
		if( v->in_fmt == v->cur_fmt && v->cur_width == p[0] && v->cur_height == p[1] ) {
			*ret = 1;
		  	return dst;	
		}


		*ret = 2;
		return dst; //@ caller will scale frame in dst
	} else {
			
		if( *dstlen < (strides[0] + strides[1] + strides[2]) || dst == NULL ) {
			dst = realloc( dst, RUP8( strides[0]+strides[1]+strides[2]) );
			*dstlen = strides[0] + strides[1] + strides[2];
		}

		int n = sock_t_recv( v->fd[0], dst, strides[0] + strides[1] + strides[2] );
		if( n !=  (strides[0] + strides[1] + strides[2] ) )
		{
			*ret = -1;
			return dst;
		}

		if( v->in_fmt == v->cur_fmt && v->cur_width == p[0] && v->cur_height == p[1] ) {
			*ret = 1;
		        return dst;
		}

		*ret = 2;
		return dst;
	}
	return dst;
}


int	vj_client_read_no_wait(vj_client *v, int sock_type, uint8_t *dst, int bytes )
{
	return sock_t_recv( v->fd[ sock_type ], dst, bytes );
}

int	vj_client_read(vj_client *v, int sock_type, uint8_t *dst, int bytes )
{
	return sock_t_recv( v->fd[ sock_type ], dst, bytes );
}

int vj_client_send_buf(vj_client *v, int sock_type,unsigned char *buf, int len) {
	if( v->mcast ) {
		return mcast_send( v->s, (void*) buf, len , v->ports[sock_type ] );
	}
	return sock_t_send( v->fd[ sock_type ], buf, len );
}

int vj_client_send(vj_client *v, int sock_type,unsigned char *buf) {
	int len = strlen( (const char*)buf);
	
	if( v->mcast ) {
		sprintf( (char*) v->blob, "V%03dD", len );
		memcpy( v->blob + 5, buf, len );
		return mcast_send( v->s, (void*) v->blob, len + 5, v->ports[sock_type ] );
	}

	sprintf( (char*) v->blob, "V%03dD", len );
	memcpy( v->blob + 5, buf, len );

	return sock_t_send( v->fd[ sock_type ], v->blob, len + 5 );
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
