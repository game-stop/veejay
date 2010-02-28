/* vjnet - low level network I/O for VeeJay
 *
 *           (C) 2005 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef CMD_H_INCLUDED
#define CMD_H_INCLUDED
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
typedef struct
{
	struct hostent *he;	
	struct sockaddr_in addr;	
	int	sock_fd;	
	int port_num;
	unsigned char *sbuf;
	int send_size;
	int recv_size;
	fd_set rds;
	fd_set wds;
} vj_sock_t;

vj_sock_t	*alloc_sock_t(void);
void		sock_t_free(vj_sock_t *s);
int		sock_t_wds_isset(vj_sock_t *s);
int		sock_t_rds_isset(vj_sock_t *s);
int		sock_t_connect( vj_sock_t *s, char *host, int port );
int		sock_t_poll( vj_sock_t *s );
int		sock_t_recv( vj_sock_t *s, void *dst, int len );
int		sock_t_send( vj_sock_t *s, unsigned char *buf, int len );
int		sock_t_send_fd( int fd, int sndsize, unsigned char *buf, int len );
void		sock_t_close( vj_sock_t *s );
int		sock_t_connect_and_send_http( vj_sock_t *s, char *host, int port, char *buf, int buf_len );
void		sock_t_set_timeout( vj_sock_t *s, int t );
#endif
