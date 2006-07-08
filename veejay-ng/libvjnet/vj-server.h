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
#define VJ_MAX_CONNECTIONS 16
#define VJ_PORT 3490

#include <libvjnet/common.h>
void *vj_server_alloc(int port, char *mcast_group_name, int type);

int vj_server_update( void *data, int link_id);

void vj_server_shutdown( void *data);

int vj_server_retrieve_msg( void *data, int link_id, char *dst);

int vj_server_poll( void *data);

int vj_server_send( void *data, int link_id, uint8_t *buf, int len);

int vj_server_send_frame( void *data, int link_id, uint8_t *buf, int total_len, VJFrame *frame, long ms);

int	vj_server_init(void);

void	vj_server_close_connection( void *data, int link_id );

int	vj_server_new_connection( void *data);

int	vj_server_client_promoted( void *data, int link_id);

void	vj_server_client_promote( void *data, int link_id);

int	vj_server_link_used(void *data , int link_id);

#endif
