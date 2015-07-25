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
#ifndef VJ_SERVER_H
#define VJ_SERVER_H
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#define VJ_PORT 3490
#define VJ_MAX_CONNECTIONS 16

#define VEEJAY_SERVER_LOG "/tmp/veejay.net.log"

typedef struct vj_server_t {
    struct sockaddr_in myself;
    struct sockaddr_in remote;
    int handle;
    int nr_of_connections;
    void **link;
    fd_set	fds;
    fd_set	wds;
    fd_set	eds;
    int	 use_mcast;
    int	 server_type;
    int  ports[2];
    void	**protocol;
    char	*recv_buf;
    unsigned int	send_size;
    unsigned int	recv_size;
    int mcast_gray;
    FILE *logfd; 
    unsigned int recv_bufsize;
} vj_server;

vj_server *vj_server_alloc(int port, char *mcast_group_name, int type, size_t recv_max_len);

int vj_server_update(vj_server * vje, int link_id);

void vj_server_shutdown(vj_server *vje);

char *vj_server_retrieve_msg(vj_server *vje, int link_id, char *dst, int *res);

int vj_server_poll(vj_server * vje);

int vj_server_send(vj_server *vje, int link_id, uint8_t *buf, int len);

int vj_server_send_frame(vj_server *vje, int link_id, uint8_t *buf, int total_len, VJFrame *frame, long ms
		);

int	vj_server_init(void);

int _vj_server_del_client(vj_server * vje, int link_id);

void	vj_server_set_mcast_mode( vj_server *vje, int mode );

void	vj_server_close_connection( vj_server *vje, int link_id );

int	vj_server_new_connection(vj_server *vje);

int	vj_server_client_promoted( vj_server *vje, int link_id);

void	vj_server_client_promote( vj_server *vje, int link_id);

int	vj_server_link_used(vj_server *vje , int link_id);

void vj_server_init_msg_pool(vj_server *vje, int link_id );

int	vj_server_link_can_write( vj_server *vje, int link_id );

int	vj_server_link_can_read( vj_server *vje, int link_id);

void	vj_server_geo_stats();

char   *vj_server_my_ip();

#endif
