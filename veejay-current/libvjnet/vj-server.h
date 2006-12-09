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
#ifndef VJ_SERVER_H
#define VJ_SERVER_H

#include "mcastreceiver.h"
#include "mcastsender.h"
#include "common.h"

#define VJ_PORT 3490
#define VJ_MAX_CONNECTIONS 16

typedef struct vj_server_t {
    struct sockaddr_in myself;
    struct sockaddr_in remote;
    int handle;
    int nr_of_connections;
	void **link;
	fd_set	fds;
	fd_set	wds;
	int	 use_mcast;
	int	 server_type;
	int  ports[2];
	void	**protocol;
	char	*recv_buf;
} vj_server;

vj_server *vj_server_alloc(int port, char *mcast_group_name, int type);

int vj_server_update(vj_server * vje, int link_id);

void vj_server_shutdown(vj_server *vje);

int vj_server_retrieve_msg(vj_server *vje, int link_id, char *dst);

int vj_server_poll(vj_server * vje);

int vj_server_send(vj_server *vje, int link_id, uint8_t *buf, int len);

int vj_server_send_frame(vj_server *vje, int link_id, uint8_t *buf, int total_len, VJFrame *frame, VJFrameInfo *info,long ms);

int	vj_server_init(void);

void	vj_server_close_connection( vj_server *vje, int link_id );

int	vj_server_new_connection(vj_server *vje);

int	vj_server_client_promoted( vj_server *vje, int link_id);

void	vj_server_client_promote( vj_server *vje, int link_id);

int	vj_server_link_used(vj_server *vje , int link_id);

#endif
