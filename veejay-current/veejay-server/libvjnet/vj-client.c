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
#include <veejay/vims.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvjnet/cmd.h>
#include <libvjnet/mcastreceiver.h>
#include <libvjnet/mcastsender.h>
#include <libavutil/pixfmt.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <liblzo/lzo.h>
#define VJC_OK 0
#define VJC_NO_MEM 1
#define VJC_SOCKET 2
#define VJC_BAD_HOST 3

#define PACKET_LEN (65535*2)

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
	v->blob = (unsigned char*) vj_calloc(sizeof(unsigned char) * PACKET_LEN ); 
	if(!v->blob ) {
		veejay_msg(0, "Memory allocation error.");
		free(v);
		return NULL;
	}
	v->mcast = 0;
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
	int tmp = sizeof(int);
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


	long total = lzo_decompress( t->lzo, ( in == NULL ? t->space: in ), data_len, dst, UV,s1,s2,s3 );
	if( total != (Y+UV+UV) )
		veejay_msg(0, "Error decompressing: expected %d bytes got %d.", (Y+UV+UV),total);

	return total;
}

static	uint32_t	getint(uint8_t *in, int len ) {
	char *ptr, *word = strndup( in, len+1 );
	word[len] = '\0';
	long v = strtol( word, &ptr, 10 );
	free(word);
	return (uint32_t) v;
}
/*
#define LIVES_HEADER_LENGTH 1024
#define SALSAMAN_DATASIZE 3
#define SALSAMAN_HSIZE 5
#define SALSAMAN_VSIZE 6
#define SALSAMAN_SUBSPACE 11
static int	vj_client_parse_salsaman( vj_client *v,uint8_t *partial_header, int partial_size, int *dst, uint8_t *header )
{
	uint8_t header[LIVES_HEADER_LENGTH];
	memset( header,0,sizeof(LIVES_HEADER_LENGTH ));
	memcpy( header, partial_header, partial_size );

	uint8_t *header_ptr = header + partial_size;

	//@ read rest of LiVES header
	int rest = sock_t_recv( v->c[0]->fd, header_ptr, LIVES_HEADER_LENGTH - partial_header );

	//@ values are space seperated
	gchar **tokens = g_strsplit( header, " ", -1 );
	if( tokens == NULL  || tokens[0] == NULL ) {
		if( tokens != NULL ) free(tokens);
		return 0;
	}
	
	//@ fetch the values into vars
	dst[0]	= atoi( header[0] );
	dst[1]	= 0; // 

	dst[2]  = atoi( header[2] ); // flags uint32
	dst[SALSAMAN_DATASIZE]  = atoi( header[SALSAMAN_DATASIZE] ); // dsize size_t

	dst[4]  = 0;

	dst[5]  = atoi( header[5] ); //@ hsize int
	dst[6]  = atoi( header[6] ); //@ vsize int
	dst[7]  = g_strtod( header[7], NULL ); // fps double
	dst[8]  = atoi( header[8] ); // palette int
	dst[9]  = atoi( header[9] ); // yuv sampling int
	dst[10] = atoi( header[10] ); // clamp int
	dst[11] = atoi( header[11] ); // subspace int
	dst[12] = atoi( header[12] ); // compression type int

	g_strfreev( tokens );
	return 1;
}
*/

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

	int n = sscanf( line, "%04d%04d%04d%08d%08d%08d%08d", 
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


int	vj_client_read_i( vj_client *v, uint8_t *dst, int dstlen )
{
	uint8_t line[128];
	uint32_t p[4] = {0, 0,0,0 };
	uint32_t strides[4] = { 0,0,0,0 };

	int	tokens[16];

	int n = 0;
	int plen = 0;
	int conv = 1;
	int y_len = 0;
	int uv_len = 0;
	
	memset( tokens,0,sizeof(tokens));

	if( v->mcast == 1)
	{
		uint8_t *in = mcast_recv_frame( v->r, &p[0],&p[1], &p[2], &plen );
		if( in == NULL )
			return 0;
		
		v->in_width = p[0];
		v->in_height = p[1];
		v->in_fmt = p[2];

		uv_len = 0;
		y_len = p[0]  * p[1];
		switch(v->in_fmt )
		{
			case PIX_FMT_YUV422P:
			case PIX_FMT_YUVJ422P:
				uv_len = y_len / 2; 
				break;
			case PIX_FMT_YUV420P:
			case PIX_FMT_YUVJ420P:
				uv_len = y_len / 4;break;
			default:
				uv_len = y_len; break;
				veejay_msg(VEEJAY_MSG_WARNING, "Unknown data format: %02x, assuming equal plane sizes.");
				break;
		}
		
		strides[0] = getint( in + 12 + 8, 8 );
		strides[1] = getint( in + 12 + 16, 8 );
		strides[2] = getint( in + 12 + 24, 8 );

		if( vj_client_decompress( v,in, dst, plen, y_len, uv_len , plen, strides[0],strides[1],strides[2]) == 0 )
			return 0;

		if( p[0] != v->cur_width || p[1] != v->cur_height )
			return 2;


		return 1; //@ caller will memcpy it to destination buffer
	}
	else 
	{
		//@ result returns software package id
		int result = vj_client_packet_negotiate( v, tokens );
		if( result == 0 ) { 
		  	veejay_msg(0, "Unable to read frame header.");
			return -1;
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
	
		if( v->space == NULL || v->in_width != v->orig_width || v->in_height != v->orig_height ) {
			if( v->space ) free(v->space);
			v->space = vj_calloc(sizeof(uint8_t) * v->in_width * v->in_height * 4 );
			if(!v->space) {
				veejay_msg(0,"Could not allocate memory for network stream.");
				return -1;
			}
			v->orig_width = v->in_width;
			v->orig_height = v->in_height;
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
					veejay_msg(VEEJAY_MSG_WARNING, "Unknown data format: %02x, assuming equal plane sizes.", v->in_fmt);
					break;
			}

			int n = sock_t_recv( v->fd[0],v->space,p[3] );

			if( n <= 0 ) {
				if( n == -1 ) {
					veejay_msg(VEEJAY_MSG_ERROR, "Error '%s' while reading socket", strerror(errno));
				} else {
					veejay_msg(VEEJAY_MSG_DEBUG,"Remote closed connection");
				}
				return -1;
			}

			if( n != p[3] && n > 0 )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Broken video packet , got %d out of %d bytes",n, p[3] );	
				return -1;
			}
			
			//@ decompress YUV buffer
			vj_client_decompress( v,v->space, dst, p[3], y_len, uv_len , 0, strides[0],strides[1],strides[2]);

			if( v->in_fmt == v->cur_fmt && v->cur_width == p[0] && v->cur_height == p[1] )
			       return 1;	

			return 2; //@ caller will scale frame in dst
		} else {
		
			int n = sock_t_recv( v->fd[0], dst, strides[0] + strides[1] + strides[2] );
			if( n !=  (strides[0] + strides[1] + strides[2] ) )
			{
				veejay_msg(0, "Broken video packet");
				return -1;
			}

			if( v->in_fmt == v->cur_fmt && v->cur_width == p[0] && v->cur_height == p[1] )
			       return 1;	


			return 2;

		}

		return 0;
	}
	return 0;
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
	int len = strlen(buf);
	
	if( v->mcast ) {
		sprintf( v->blob, "V%03dD", len );
		memcpy( v->blob + 5, buf, len );
		return mcast_send( v->s, (void*) v->blob, len + 5, v->ports[sock_type ] );
	}

	sprintf( v->blob, "V%03dD", len );
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
