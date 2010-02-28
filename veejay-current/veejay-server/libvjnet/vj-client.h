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
	void	*r;
	void   	*s;
	void	*fd;
	int	type;
} conn_type_t;
	
typedef struct
{
	int planes[3];
	int cur_width;
	int cur_height;
	int cur_fmt;
	int in_width;
	int in_height;
	int in_fmt;
	uint8_t *space;
	conn_type_t **c;
	int ports[3];
	int mcast;
	void *lzo;
	unsigned char *blob;
} vj_client;

int	vj_client_link_can_write(vj_client *v, int s);

int 	vj_client_link_can_read(vj_client *v,int s );

int vj_client_connect( vj_client *v, char *host, char *group_name, int port_id );
  
int	vj_client_get_status_fd(vj_client *v, int sock_type );

void	vj_client_flush( vj_client *v, int delay );

int	vj_client_poll( vj_client *v, int sock_type );

int	vj_client_read_i(vj_client *v, uint8_t *dst, int len );

int vj_client_read( vj_client *v, int sock_type, uint8_t *dst, int bytes );

int vj_client_read_no_wait( vj_client *v, int sock_type, uint8_t *dst, int bytes );

int	vj_client_close( vj_client *v );

int vj_client_send( vj_client *v, int sock_type, char *buf);

vj_client *vj_client_alloc(int w , int h, int f);

void	vj_client_free(vj_client *v);

int	vj_client_test(char *addr, int port );

int vj_client_send_bufX(vj_client *v, int sock_type,unsigned char *buf, int len );

int	vj_client_window_sizes( int socket_fd, int *r, int *s );

int vj_client_connect_dat(vj_client *v, char *host, int port_id  );

int	vj_client_setup_timeout( vj_client *v, int sock_type, int timeout );
#endif

