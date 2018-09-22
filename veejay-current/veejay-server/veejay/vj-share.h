/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */
#ifndef VJSHARE
#define VJSHARE
int32_t vj_share_pull_master( void *shm, char *master_host, int master_port );
int	vj_share_get_info( char *host, int port, int *width, int *height, int *format, int *key, int screen_id );
int	vj_share_start_slave( char *host, int port, int shm_id);

int	vj_share_start_net( char *host, int port, char *master_host, int master_port);
int	vj_share_play_last( char *host, int port );

#endif

