/* veejay - Linux VeeJay
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
#ifndef VJ_CLIENT_H
#define VJ_CLIENT_H

typedef struct
{
	uint8_t *space;
	ssize_t space_len;
	int mcast;

	void	*r;
	void	*s;
	
	void	*fd[2];
	int	ports[2];

	void *decoder;
} vj_client;

int	vj_client_link_can_write(vj_client *v, int s);

int 	vj_client_link_can_read(vj_client *v,int s );

int vj_client_connect( vj_client *v, char *host, char *group_name, int port_id );
  
void	vj_client_flush( vj_client *v, int delay );

int	vj_client_poll( vj_client *v, int sock_type );

int vj_client_read( vj_client *v, int sock_type, uint8_t *dst, int bytes );

int vj_client_read_no_wait( vj_client *v, int sock_type, uint8_t *dst, int bytes );

void	vj_client_close( vj_client *v );

int vj_client_send( vj_client *v, int sock_type,unsigned char *buf);

int vj_client_send_buf( vj_client *v, int sock_type,unsigned char *buf, int len);

vj_client *vj_client_alloc();

vj_client *vj_client_alloc_stream(VJFrame *info);

void	vj_client_free(vj_client *v);

int	vj_client_test(char *addr, int port );

int	vj_client_window_sizes( int socket_fd, int *r, int *s );

int vj_client_connect_dat(vj_client *v, char *host, int port_id  );

int	vj_client_read_mcast_data( vj_client *v, int buflen );

int vj_client_read_frame_data( vj_client *v, int datalen);

void vj_client_rescale_video( vj_client *v, uint8_t *data[4] );

int vj_client_read_frame_hdr( vj_client *v );

#endif

