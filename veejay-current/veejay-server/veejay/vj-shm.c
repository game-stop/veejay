/* veejay - Linux VeeJay
 * 	     (C) 2011 Niels Elburg <nwelburg@gmail.com> 
 *       shared memory segment 
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
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>

typedef struct {
	int shm_id;
	char *sms;
	char	*env_shm_id;
	int parent;
	key_t key;
	char *file;
} vj_shm_t;


typedef struct
{
	int					resource_id;
	pthread_rwlock_t	rwlock;
	int					header[8];
} vj_shared_data;

void	vj_shm_free(void *vv)
{
	vj_shm_t *v = (vj_shm_t*) vv;
	
	vj_shared_data *data = (vj_shared_data*) v->sms;

	int res     = pthread_rwlock_destroy( &data->rwlock );

	res = shmdt( data );
	if(res ) {
		veejay_msg(0, "failed to detach shared memory: %s",strerror(errno));
	}
	res = shmctl( v->shm_id, IPC_RMID, NULL );
	if( res ) {
		veejay_msg(0, "failed to remove shared memory %d: %s", v->shm_id, strerror(errno));
	}

	if( v->file ) {
		remove(v->file);
		free(v->file);
	}

	free(v);
}

int		vj_shm_stop( void *vv )
{
	vj_shm_t *v = (vj_shm_t*) vv;
	
	vj_shared_data *data = (vj_shared_data*) v->sms;


	int res = shmdt( data );
	if( res ) {
		veejay_msg(0,"failed to detach shared memory: %s",strerror(errno));
		return -1;
	}
	return 0;
}

int		vj_shm_read( void *vv )
{
	vj_shm_t *v         = (vj_shm_t*) vv;
	vj_shared_data *data = (vj_shared_data*) v->sms;
#ifdef STRICT_CHECKING
	assert( v->parent == 0 );
#endif
	int res = pthread_rwlock_rdlock( &data->rwlock );
	if( res == -1 ) {
		veejay_msg(0, "%s",strerror(errno));
		return -1;
	}

	//@ resource protected
	veejay_msg(0, "Reading resource!");
	sleep(1);
	veejay_msg(0, "Done reading!");

	res = pthread_rwlock_unlock( &data->rwlock );
	if( res == -1 ) {
		veejay_msg(0, "%s",strerror(errno));
		return -1;
	}


	return 0;
}

int rot_val =0;

int		vj_shm_write( void *vv, uint8_t *frame[3], int plane_sizes[3] )
{
	vj_shm_t *v         = (vj_shm_t*) vv;
	vj_shared_data *data = (vj_shared_data*) v->sms;
#ifdef STRICT_CHECKING
	assert( v->parent == 1 );
#endif

	//@ call wrlock_wrlock N times before giving up ..

	int res = pthread_rwlock_wrlock( &data->rwlock );
	if( res == -1 ) {
		veejay_msg(0, "%s",strerror(errno));
		return -1;
	}

	uint8_t *ptr = (uint8_t*) v->sms + 4096;

	veejay_memcpy( ptr , frame[0], plane_sizes[0] );

	veejay_memcpy( ptr + plane_sizes[0], frame[1], plane_sizes[1] );
	veejay_memcpy( ptr + plane_sizes[0] +
					plane_sizes[1], frame[2], plane_sizes[2] );

	res = pthread_rwlock_unlock( &data->rwlock );
	if( res == -1 ) {
		veejay_msg(0, "%s",strerror(errno));
		return -1;
	}

	return 0;
}

void	*vj_shm_new_slave(int shm_id)
{ //@ incomplete!
	int rc = 0;

	if( shm_id <= 0 ) {
		char *env_id = getenv( "VEEJAY_SHMID" );
		if( env_id == NULL ) {
			veejay_msg(0, "Failed to get SHMID, set VEEJAY_SHMID");
			return NULL;
		}
		shm_id = atoi( env_id );
	}

	veejay_msg(VEEJAY_MSG_INFO, "Trying SHM_ID %d", shm_id );

	void *ptr = shmat( shm_id, NULL, 0 );

	if( ptr == (char*) (-1) ) {
		veejay_msg(0, "failed to attach to shared memory %d", shm_id );
		shmctl( shm_id, IPC_RMID, NULL );
		return NULL;
	}

	vj_shm_t *v = (vj_shm_t*) vj_calloc(sizeof( vj_shm_t*));
	v->sms = ptr;
	vj_shared_data *data = (vj_shared_data*) ptr;

	v->shm_id = shm_id;

	return v;
}

//@ create a file in HOME/.veejay to store SHM ID
//@ is read by plugin 'lvd_shmin.so'
//
static	int		vj_shm_file_ref( vj_shm_t *v,const char *homedir )
{
	char path[1024];
	snprintf(path, sizeof(path) - 1, "%s/veejay.shm", homedir );

	struct stat inf;
	int res = stat( path, &inf );
	if( res == 0 ) {
		veejay_msg(0, "Only 1 shared resource for now, remove resource with ipcrm <master>.");
		veejay_msg(0, "and delete '%s'", path );
		return -1;
	}

	FILE *f = fopen( path,"w+" );
	if(!f ) {
		veejay_msg(0,"Unable to open %s: %s", path, strerror(errno) );
		return -1;
	}

	key_t key = ftok( path, getpid());

	fprintf( f, "master: %d", key );
	fclose(f );

	v->key = key;
	v->file = strdup( path );
	
	veejay_msg(VEEJAY_MSG_INFO, "Shared Resource:  saved in '%s'", path );

	return 0;
}

//@ new producer, puts frame in shm
void	*vj_shm_new_master( const char *homedir, VJFrame *frame)
{
	vj_shm_t *v = (vj_shm_t*) vj_calloc(sizeof(vj_shm_t));
	v->parent   = 1;

	if( vj_shm_file_ref( v, homedir ) == -1 ) {
		free(v);
		return NULL;
	}

	long size = 2 * 1048576;
	long size1 = 4096;

	//@ create
	v->shm_id = shmget( v->key,size, IPC_CREAT |0666 );

	if( v->shm_id == -1 ) {
		veejay_msg(0,"Error while allocating shared memory segment: %s", strerror(errno));
		free(v);
		return NULL;
	}

	//@ attach
	v->sms 	    = shmat( v->shm_id, NULL , 0 );
	if( v->sms == NULL ) {
		shmctl( v->shm_id, IPC_RMID, NULL );
		veejay_msg(0, "Failed to attach to shared memory:%s",strerror(errno));
		free(v);
		return NULL;
	}

	pthread_rwlockattr_t	rw_lock_attr;
	memset( v->sms, 0, size );

	//@ set up frame info (fixme, incomplete)
	vj_shared_data *data = (vj_shared_data*) &(v->sms[0]);
	data->resource_id    = v->shm_id;
	data->header[0]      = frame->width;
	data->header[1]      = frame->height;
	data->header[2]      = frame->stride[0];
	data->header[3]      = frame->stride[1];
	data->header[4]      = frame->stride[2];
	data->header[5]      = frame->format; 

	veejay_msg(VEEJAY_MSG_INFO, "Shared Resource:  Starting address: %p", data );
	veejay_msg(VEEJAY_MSG_INFO, "Shared Resource:  Frame data      : %p", data + 4096 );
	veejay_msg(VEEJAY_MSG_INFO, "Shared Resource:  Static resolution of %d x %d, YUV 4:2:2 planar",
			data->header[0],data->header[1] );

	int	res	= pthread_rwlockattr_init( &rw_lock_attr );
	if( res == -1 ) {
		veejay_msg(0, "Failed to create rw lock: %s",strerror(errno));
		shmctl( v->shm_id, IPC_RMID, NULL );
		free(v);
		return NULL;
	}

	res	    = pthread_rwlockattr_setpshared( &rw_lock_attr, PTHREAD_PROCESS_SHARED );
	if( res == -1 ) {
		veejay_msg(0, "Failed to set PTHREAD_PROCESS_SHARED: %s",strerror(errno));
		shmctl( v->shm_id, IPC_RMID, NULL );
		free(v);
		return NULL;
	}

	res	    = pthread_rwlock_init( &data->rwlock, &rw_lock_attr );
	if( res == -1 ) {
		shmctl(v->shm_id, IPC_RMID , NULL );
		veejay_msg(0, "Failed to initialize rw lock:%s",strerror(errno));
		free(v);
		return NULL;
	}

	char tmp_name[1024];
	snprintf( tmp_name,sizeof(tmp_name)-1, "VEEJAY_SHMID=%d", v->shm_id );

	v->env_shm_id = strdup( tmp_name );

	res		= putenv( v->env_shm_id );

	return v;
}

