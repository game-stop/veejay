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
#include <config.h>
#include <unistd.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <stdint.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vj-shm.h>
#include <libvjmem/vjmem.h>
#include <stdlib.h>

#define SEM_EMPTY 0
#define SEM_FULL 1

static	struct sembuf wait_until_empty	= { SEM_EMPTY,	-1,	SEM_UNDO};
static	struct sembuf signal_on_empty	= { SEM_EMPTY,	1,	IPC_NOWAIT};	
static	struct sembuf wait_until_full	= { SEM_FULL,	1,	IPC_NOWAIT};
static	struct sembuf signal_on_full	= { SEM_FULL,	-1,	SEM_UNDO};

void	del_segment(video_segment *vs)
{

	shmctl( vs->segment_id, IPC_RMID, &(vs->my_shmid_ds));
	
	semctl( vs->semaphore_id, SEM_EMPTY, IPC_RMID, vs->my_semun); 

	if(vs)
		free(vs);
}      

int	get_segment_id(void *arg)
{
	video_segment *vs = (video_segment*) arg;
	return vs->segment_id; 
}

int	get_semaphore_id(void *arg)
{
	video_segment *vs = (video_segment*) arg;
	return vs->semaphore_id;
}

video_segment *new_c_segment(int semid, int shmid)
{
	video_segment *vs = (video_segment*) malloc(sizeof(video_segment));
	if(!vs) return NULL;
	vs->semaphore_id = semid;
	vs->segment_id = shmid;
	vs->data = NULL;
	vs->memory_size = 0;
	return vs;
}

video_segment *new_segment(int len)
{
	video_segment *vs = (video_segment*) malloc(sizeof(video_segment));
	int ps	 	  = getpagesize();
	int size	  = ((ps + len)/ps) * ps;

	if(!vs)
	{	
		printf("Insufficient memory for segment\n");
	 	return NULL;
	}

	vs->semaphore_id = semget( IPC_PRIVATE,2,0666 | IPC_CREAT);
	if(vs->semaphore_id < 0)
	{
		return NULL;
	}
	// initialize number of elements in memory
	vs->my_semun.val = 1;
	semctl( vs->semaphore_id, SEM_EMPTY, SETVAL, vs->my_semun );
	
	// init shared memory	
	vs->segment_id = shmget( IPC_PRIVATE, size, 0666 | IPC_CREAT );
	if(vs->segment_id < 0)
	{
	 	return NULL;
	}
	vs->memory_size = size;
	printf("Shared memory ID %d , sema %d \n", vs->segment_id,vs->semaphore_id);
	return vs;
}

void	attach_segment(video_segment *vs, int flag)
{
		// attach shared memory to process
	vs->data = (void*) shmat( vs->segment_id, 0, (flag==1 ? SHM_W :SHM_R ));

}

void	produce(void *arg, uint8_t *src[3], int len) 
{
//	int timeout_ms = 10;
//	struct timespec timeout;
//	timeout.tv_sec = 0;
//	timeout.tv_nsec = (timeout_ms % 1000) * 1000000L;
	video_segment *vs = (video_segment*) arg;
	uint8_t *dst;
	// wait until segment is empty
	if(semop( vs->semaphore_id, &wait_until_empty,1 )==-1)
	{
		return;
	}

	// critical section
	dst = (uint8_t*) vs->data;

	veejay_memcpy( dst, src[0], len);
	veejay_memcpy( dst+len, src[1], len/4);
	veejay_memcpy( dst+(len*5)/4, src[2], len/4);

	// signal frame ready
	semop( vs->semaphore_id, &signal_on_full,1);
}

void	consume(void *arg, uint8_t *dst[3], int len)
{
//	int timeout_ms = 10;
//	struct timespec timeout;	
	video_segment *vs = (video_segment*) arg;
	uint8_t *src;
//	timeout.tv_sec = 0;
//	timeout.tv_nsec = (timeout_ms % 1000) * 1000000L;
	// wait until segment is empty
	if(semop( vs->semaphore_id, &wait_until_full,1 )==-1)
	{
		return;
	}
	// critical section
	src = (uint8_t*) vs->data;

	veejay_memcpy( dst[0], src,len);
	veejay_memcpy( dst[1], src+len, len/4);
	veejay_memcpy( dst[2], src+(len*5)/4, len/4);
	// signal segment read
	if(semop( vs->semaphore_id, &signal_on_empty,1)==-1)
	{
		return;
	}
}


