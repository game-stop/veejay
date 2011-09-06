/* 
 * veejay  
 *
 * Copyright (C) 2000-2011 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef VJ_SHM
#define VJ_SHM
int		vj_shm_write( void *vv, uint8_t *frame[3], int plane_sizes[3] );
void	*vj_shm_new_slave(int shm_id);
void	*vj_shm_new_master(const char *homedir, VJFrame *frame);
int		vj_shm_read( void *vv, uint8_t *dst[3] );
int		vj_shm_stop( void *vv );
void	vj_shm_free(void *vv);
int		vj_shm_get_status( void *vv );
void	vj_shm_set_status( void *vv, int status );
int		vj_shm_get_shm_id( void *vv );
int		vj_shm_get_id();
int		vj_shm_get_my_shmid();
void	vj_shm_set_id(int v);
int		vj_shm_get_my_id();
#endif
