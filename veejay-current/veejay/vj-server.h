/* veejay - Linux VeeJay
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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>

#define VJ_PORT 3490
#define VJ_MAX_CONNECTIONS 8

typedef struct vj_server_t {
    fd_set master;
    fd_set current;
    struct sockaddr_in myself;
    struct sockaddr_in remote;
    int handle;
    int nr_of_connections;
    int nr_of_links;
} vj_server;

int vj_server_init();

vj_server *vj_server_alloc(int port, int type);
int vj_server_send(int link_id, const char *buf, int len);
int vj_server_update(vj_server * vje);

void vj_server_shutdown(vj_server *vje, int type);

int vj_server_adv_poll(vj_server *vje,int *links);

void vj_server_list_clients();

int vj_server_retrieve_msg(int link_id, char *dst);

int vj_server_status_check(vj_server * vje);

int vj_server_status_send(vj_server *vje, char *buf, int len);

void vj_server_close_link(vj_server * vje, int link_id, int type);

int vj_server_poll(vj_server * vje);

int vj_server_must_send(); 

int		vj_server_raw_send(int link_id, uint8_t *buf, int len);

int	vj_server_init(void);

#endif
