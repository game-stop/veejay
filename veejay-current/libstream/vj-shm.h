/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <elburg@hio.hen.nl> 
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
#ifndef VJ_SHM
#define VJ_SHM
#include <config.h>
#include <stdint.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

union semun 
{
	int 	val;	
	struct	semid_ds *buf;
	unsigned short *array;
	struct seminfo *__buf;
};

typedef struct video_segment_t
{
	int	semaphore_id;
	int	segment_id;
	void	*data;
	union	semun	my_semun;
	struct	shmid_ds my_shmid_ds;
	int 	memory_size;
} video_segment;


void		del_segment( video_segment *seg);
int		get_segment_id(void *arg);
int		get_semaphore_id(void *arg);
video_segment 	*new_segment( int len );
video_segment   *new_c_segment(int semid, int shmid);
void		attach_segment(video_segment *seg, int flag );
void		produce(void *arg, uint8_t *src[3], int len);
void		consume(void *arg, uint8_t *dst[3], int len);



#endif
