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
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#define VJC_OK 0
#define VJC_NO_MEM 1
#define VJC_SOCKET 2
#define VJC_BAD_HOST 3

#define PACKET_LEN (65535*32)

vj_client *vj_client_alloc( int w, int h, int f )
{
	vj_client *v = (vj_client*) malloc(sizeof(vj_client));
	memset(v, 0, sizeof(vj_client));
	if(!v)
	{
		return NULL;
	}
	v->cur_width = w;
	v->cur_height = h;
	v->cur_fmt = get_ffmpeg_pixfmt(f);
	v->space = NULL;
	v->c = (conn_type_t**) malloc(sizeof(conn_type_t*) * 3);
	v->c[0] = (conn_type_t*) malloc(sizeof(conn_type_t));
	v->c[1] = (conn_type_t*) malloc(sizeof(conn_type_t));
	v->blob = (unsigned char*) malloc(sizeof(unsigned char) * PACKET_LEN ); 
	if(!v->blob ) {
		veejay_msg(0, "Memory allocation error.");
		free(v);
		return NULL;
	}
	memset( v->blob,0,sizeof(unsigned char) * PACKET_LEN );
	v->mcast = 0;
	if( w > 0 && h > 0 ) {
		v->space = (uint8_t*) malloc( sizeof(uint8_t) * w * h * 4 );
		memset(v->space,0,sizeof(uint8_t)*w*h*4);
	}

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
		if(v->c) 
			free(v->c);
		if(v->blob)
			free(v->blob);
		if(v->lzo)
			free(v->lzo);
		if(v->space)
			free(v->space);
		free(v);
		v = NULL;
	}
}

#ifdef STRICT_CHECKING
static	int	verify_integrity( char *buf, int len ) {
	if( len < 0 || buf == NULL ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "nothing to send");
		return 0;
	}
	int i;
	for ( i = 0; i < len; i ++ ) {
		if( buf[i] == '\0' ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "end of string at pos %d, should have been pos %d",
					i, len );
			return 0;
		}

		if( buf[i] == '\n' || buf[i] == '\r' ) 
			continue;

		if( !isprint(buf[i]) ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "character '%x' is not alphanumeric at pos %d/%d",
					buf[i], i, len );
			return 0;
		}
	}	
	return 1;
}
#endif

int	vj_client_window_sizes( int socket_fd, int *r, int *s )
{
	int tmp = sizeof(int);
	if( getsockopt( socket_fd, SOL_SOCKET, SO_SNDBUF,(unsigned char*) s, &tmp) == -1 )
        {
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


	v->c[0]->type = VSOCK_C;
	v->c[0]->fd   = alloc_sock_t();
        
	if(v->c[1])	
      		free(v->c[1]);
	
	v->c[1] = NULL;
	
	if( sock_t_connect( v->c[0]->fd, host, (port_id + 5)  ) )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Connect to DAT port %d", port_id + 5);	
		return 1;
	}
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

		v->c[0]->type = VSOCK_C;
		v->c[0]->fd   = alloc_sock_t();
		v->c[1]->type = VSOCK_S;
		v->c[1]->fd   = alloc_sock_t();
		if(!v->c[0]->fd || !v->c[1]->fd )
		{	
			veejay_msg(0, "Error opening socket");
			return error;
		}

		if( sock_t_connect( v->c[0]->fd, host, port_id + VJ_CMD_PORT ) )
		{
			if( sock_t_connect( v->c[1]->fd, host, port_id + VJ_STA_PORT ) )
			{		
				return 1;
			} else {
				veejay_msg(0, "Failed to connect to status port.");
			}
		} else {
			veejay_msg(0, "Failed to connect to command port.");
		}
	}
	else
	{
		v->c[0]->type = VMCAST_C;
		v->c[0]->r    = mcast_new_receiver( group_name, port_id + VJ_CMD_MCAST ); 
		if(!v->c[0]->r ) {
			veejay_msg(0 ,"Unable to setup multicast receiver on group %s", group_name );
			return error;
		}

		v->c[0]->s    = mcast_new_sender( group_name );
		if(!v->c[0]->s ) {
			veejay_msg(0, "Unable to setup multicast sender on group %s", group_name );
			return error;
		}
		v->ports[0] = port_id + VJ_CMD_MCAST;
		v->ports[1] = port_id + VJ_CMD_MCAST_IN;

		mcast_sender_set_peer( v->c[0]->s , group_name );
		v->mcast = 1;
//		mcast_receiver_set_peer( v->c[0]->r, group_name);
		veejay_msg(VEEJAY_MSG_DEBUG, "Client is interested in packets from group %s : %d, send to %d",
			group_name, port_id + VJ_CMD_MCAST , port_id + VJ_CMD_MCAST_IN);

		return 1;
	}
	return error;
}

int	vj_client_link_can_write( vj_client *v, int sock_type ){
	if(sock_type==VSOCK_S) {
		return sock_t_wds_isset( v->c[1]->fd );
	} else if (sock_type == VSOCK_C) {
		return sock_t_rds_isset( v->c[0]->fd );
	}
	return 0;
}

int	vj_client_link_can_read( vj_client *v, int sock_type ) {
	if(sock_type==VSOCK_S) { 
		return sock_t_rds_isset(v->c[1]->fd);
	} else if (sock_type == VSOCK_C) {
		return sock_t_rds_isset( v->c[0]->fd);
	}
	return 0;
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
	return 0;
}

static	void	vj_client_decompress( vj_client *t,uint8_t *in, uint8_t *out, int data_len, int Y, int UV , int header_len,
		uint32_t s1, uint32_t s2, uint32_t s3)
{
	uint8_t *d[3] = {
			out,
			out + Y,
			out + Y + UV };
	lzo_decompress( t->lzo, ( in == NULL ? t->space: in ), data_len, d, UV,s1,s2,s3 );
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
	uint8_t line[45];

	//@ 1. read 44 bytes and see if its us
	veejay_memset( line,0, sizeof(line));
	int plen = sock_t_recv( v->c[0]->fd, line, 44 );
	if( plen == 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Remote closed connection.");
		return -1;
	}

	if( plen < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Network I/O Error while reading header: %s", strerror(errno));
		return -1;
	}

	//@ there is 44 bytes read, identify producer software here,
	//@             the function below parses the header in LiVES
	/*
		int res = vj_client_parse_salsaman( v, line, 44, tokens );
		if( res == 0 ) {
			veejay_msg(0, "Error parsing salsaman header!");
			return -1;
		}
		return 0;
	*/
	


	// default: packet is from veejay
	tokens[0] = getint( line , 4 );
	tokens[1] = getint( line + 4, 4 );
	tokens[2] = getint( line + 8, 4 );
	tokens[3] = getint( line + 12, 8 );

	tokens[4] = getint( line + 12 + 8, 8 );
	tokens[5] = getint( line + 12 + 16, 8 );
	tokens[6] = getint( line + 12 + 24, 8 );


	return 1;
}


int	vj_client_read_i( vj_client *v, uint8_t *dst, int len )
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
	if( v->c[0]->type == VMCAST_C )
	{
		uint8_t *in = mcast_recv_frame( v->c[0]->r, &p[0],&p[1], &p[2], &plen );
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
				veejay_msg(VEEJAY_MSG_ERROR, "Unknown data format: %02x", v->in_fmt);
				break;
		}

		strides[0] = getint( in + 12 + 8, 8 );
		strides[1] = getint( in + 12 + 16, 8 );
		strides[2] = getint( in + 12 + 24, 8 );

		vj_client_decompress( v,in, dst, p[3], y_len, uv_len , plen, strides[0],strides[1],strides[2]);

		if( p[0] != v->cur_width || p[1] != v->cur_height )
			return 2;


		return 1; //@ caller will memcpy it to destination buffer
	}
	else if ( v->c[0]->type == VSOCK_C )
	{
		//@ result returns software package id
		int result = vj_client_packet_negotiate( v, tokens );
		if( result == 0 ) { //@ ids are out of thin air
			
			//@ header is from lives
			/*
			if( v->cur_width != tokens[ SALSAMAN_VSIZE ] || v->cur_height != tokens[SALSAMAN_HSIZE] ) {
				//@ no changing of sizes on the fly	
				veejay_msg(VEEJAY_MSG_ERROR, "Unexpected video frame format");
				int n;
				for( n = 0;n < 16; n ++ ) {
					veejay_msg(VEEJAY_MSG_DEBUG, "%d: [%d] %02x ", n,tokens[n],tokens[n]);
				}
				return 0;
			} 

			//@ read information from the header and figure out which pixelformat it is
			v->in_fmt = weed_palette2_pixfmt( tokens[SALSAMAN_PALETTE] , tokens[SALSAMAN_SUBSPACE ] );
			if( v->in_fmt == -1 ) {
				veejay_msg(VEEJAY_MSG_ERROR, "No support for weed palette %d", tokens[SALSAMAN_PALETTE]);
				return 0;
			}

			//@ assign required values for veejay (format,width,height)
			v->in_width = tokens[SALSAMAN_VSIZE];
			v->in_height= tokens[SALSAMAN_HSIZE];
			
			//@ start reading rest of frame, assuming size is given by SALSAMAN_DATASIZE
			int n = sock_t_recv( v->c[0]->fd,v->space, tokens[SALSAMAN_DATASIZE] );
			if( n <= 0 ) {
				if( n == -1 ) {
					veejay_msg(VEEJAY_MSG_ERROR, "Error '%s' while reading socket", strerror(errno));
				} else {
					veejay_msg(VEEJAY_MSG_DEBUG,"Remote closed connection");
				}
				return -1;
			}
			//@ ready
			//@ vj-net.c 
			
			//@ returning 2 means caller will scale 	
			return 2;
			*/
			return 0;
		} else if ( result == 1 ) {
			//@ header is veejay
			p[0] = tokens[0];
			p[1] = tokens[1];
			p[2] = tokens[2];
			p[3] = tokens[3];
			strides[0] = tokens[4];
			strides[1] = tokens[5];
			strides[2] = tokens[6];

			v->in_width = p[0];
			v->in_height = p[1];
			v->in_fmt = p[2];
	
			uv_len = 0;
			y_len = p[0]  * p[1];

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
					veejay_msg(VEEJAY_MSG_ERROR, "Unknown data format: %02x", v->in_fmt);
					break;
			}

			int n = sock_t_recv( v->c[0]->fd,v->space,p[3] );
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
			vj_client_decompress( v,NULL, dst, p[3], y_len, uv_len , plen, strides[0],strides[1],strides[2]);

			return 2; //@ caller will scale frame in dst
		} else {
			return 0;
		}

		return 0;
	}
	return 0;
}

int	vj_client_get_status_fd(vj_client *v, int sock_type )
{	
	if(sock_type == V_STATUS)
	{
		vj_sock_t *c = v->c[1]->fd;
		return c->sock_fd;
	}
	if(sock_type == V_CMD )
	{
		if(!v->mcast)
		{
			vj_sock_t *c = v->c[0]->fd;
			return	c->sock_fd;
		}
		else
		{
			mcast_receiver *c = v->c[0]->r;
			return  c->sock_fd;
		}
	}
	return 0;	
}

int	vj_client_setup_timeout( vj_client *v, int sock_type, int timeout )
{
	if( sock_type == V_STATUS )
	{
		sock_t_set_timeout( v->c[1], timeout );
	}
	if( sock_type == V_CMD )
	{
		sock_t_set_timeout( v->c[0], timeout );
	}
}

int	vj_client_read_no_wait(vj_client *v, int sock_type, uint8_t *dst, int bytes )
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
	return 0;
}

int	vj_client_read(vj_client *v, int sock_type, uint8_t *dst, int bytes )
{
	if( sock_type == V_STATUS )
	{
		if(v->c[1]->type == VSOCK_S) {
			return( sock_t_recv( v->c[1]->fd, dst, bytes ) );
		}
	}
	if( sock_type == V_CMD )
	{
		if(v->c[0]->type == VSOCK_C) {
			return ( sock_t_recv( v->c[0]->fd, dst, bytes ) );
		}
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
#ifdef STRICT_CHECKING
		if(!verify_integrity(v->blob,strlen(v->blob)) ) {
			veejay_msg(0,"VIMS validation error in buf of %d bytes: '%s'", len,buf);
		}
#endif
		if(v->c[0]->type == VSOCK_C)
			return ( sock_t_send( v->c[0]->fd, v->blob, len + 5 ));
		if(v->c[0]->type == VMCAST_C)
			return ( mcast_send( v->c[0]->s, (void*) v->blob, len + 5,
					v->ports[1] ));
	}
   	return 1;
}

int vj_client_send_buf(vj_client *v, int sock_type,unsigned char *buf, int len )
{
	if( sock_type == V_CMD )
	{
		// format msg
		sprintf(v->blob, "V%03dD", len);
		veejay_memcpy( v->blob+5, buf, len );
#ifdef STRICT_CHECKING
		if(!verify_integrity( v->blob, strlen(v->blob) ) ) {
			veejay_msg(0,"VIMS validation error in buf of %d bytes: '%s'", len+5,buf);
		}
#endif
						
		if(v->c[0]->type == VSOCK_C)
			return ( sock_t_send( v->c[0]->fd, v->blob, len + 5 ));
		
		if(v->c[0]->type == VMCAST_C)
			return ( mcast_send( v->c[0]->s, (void*) v->blob, len + 5,
					v->ports[1] ));
	}
   	return 1;
}

int vj_client_send_bufX(vj_client *v, int sock_type,unsigned char *buf, int len )
{
	if( sock_type == V_CMD )
	{
		// format msg
		sprintf(v->blob, "K%08d", len);
		veejay_memcpy( v->blob+9, buf, len );
		
		if(v->c[0]->type == VSOCK_C)
			return ( sock_t_send( v->c[0]->fd, v->blob, len + 9 ));
		if(v->c[0]->type == VMCAST_C)
			return ( mcast_send( v->c[0]->s, (void*) v->blob, len + 9,
					v->ports[1] ));
	}
   	return 1;
}

int vj_client_close( vj_client *v )
{
	if(v)
	{
		if(v->c[0])
		{
			if(v->c[0]->type == VSOCK_C)
				sock_t_close(v->c[0]->fd );
			else if ( v->c[0]->type == VMCAST_C )
			{
				mcast_close_receiver( v->c[0]->r );
				mcast_close_sender( v->c[0]->s );
			}
		}
		if(v->c[1])
		{
			if(v->c[1]->type == VSOCK_S)
				sock_t_close(v->c[1]->fd );
		}

		return 1;
	}
	return 0;
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
