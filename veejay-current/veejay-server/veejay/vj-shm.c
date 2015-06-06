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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>
#include <libvevo/vevo.h>

#define HEADER_LENGTH 4096
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

typedef struct {
	int shm_id;
	void *sms;
	char	*env_shm_id;
	int parent;
	key_t key;
	char *file;
	int status;
} vj_shm_t;


typedef struct
{
	int					resource_id;
	pthread_rwlock_t	rwlock;
	int					header[8];
} vj_shared_data;

static	int	just_a_shmid = 0;
static  int simply_my_shmkey  = 0;
static	key_t	simply_my_shmid = 0;

int		vj_shm_get_my_shmid() {
	return simply_my_shmkey;
}

int		vj_shm_get_my_id() {
	return simply_my_shmid;
}

int		vj_shm_get_id(){ 
	return just_a_shmid;
}

void	vj_shm_set_id(int v) {
	just_a_shmid = v;
}

void	vj_shm_free(void *vv)
{
	vj_shm_t *v = (vj_shm_t*) vv;
	
	vj_shared_data *data = (vj_shared_data*) v->sms;

	int res     = pthread_rwlock_destroy( &data->rwlock );

	res = shmctl( v->shm_id, IPC_RMID, NULL );
	if( res==-1 ) {
		veejay_msg(0, "Failed to remove shared memory %d: %s", v->shm_id, strerror(errno));
	}

	if( v->file ) {
		res = remove(v->file);
		if( res == -1 ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Unable to remove file %s", v->file);
		}
		free(v->file);
	}

	free(v);
}


void	vj_shm_set_status( void *vv, int status )
{
	vj_shm_t *v = (vj_shm_t*) vv;
	v->status = status;
	if( v->status == 0 ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Stopped writing frames to SHM %d", v->key );
	} else {
		veejay_msg(VEEJAY_MSG_INFO,  "Started writing frames to SHM %d", v->key );
	}
}

int		vj_shm_get_status( void *vv )
{
	vj_shm_t *v = (vj_shm_t*) vv;
	return v->status;
}


int		vj_shm_stop( void *vv )
{
	vj_shm_t *v = (vj_shm_t*) vv;

	int res = shmdt(v->sms);
	if( res ) {
		veejay_msg(0,"failed to detach shared memory: %s",strerror(errno));
		return -1;
	}
	return 0;
}

int		vj_shm_get_shm_id( void *vv )
{
	vj_shm_t *v = (vj_shm_t*) vv;
	return v->shm_id;
}

int		vj_shm_read( void *vv , uint8_t *dst[4] )
{
	vj_shm_t *v         = (vj_shm_t*) vv;
	vj_shared_data *data = (vj_shared_data*) v->sms;
	int res = pthread_rwlock_rdlock( &data->rwlock );
	if( res == -1 ) {
	//	veejay_msg(0, "%s",strerror(errno));
		return -1;
	}
	uint8_t *ptr = ( (uint8_t*) v->sms ) + HEADER_LENGTH;
	
	int len = data->header[0] * data->header[1]; //@ 
	int uv_len = len / 2;

	uint8_t *in[4] = { ptr, ptr + len, ptr + len + uv_len,NULL };
	int strides[4]    = { len, uv_len, uv_len,0 };
	vj_frame_copy( in, dst, strides );

//	veejay_memcpy( dst[0], ptr, len );
//	veejay_memcpy( dst[1], ptr + len, uv_len );
//	veejay_memcpy( dst[2], ptr + len + uv_len, uv_len );
	
	res = pthread_rwlock_unlock( &data->rwlock );
	if( res == -1 ) {
	//	veejay_msg(0, "%s",strerror(errno));
		return -1;
	}

	return 0;
}

int rot_val =0;

int		vj_shm_write( void *vv, uint8_t *frame[4], int plane_sizes[4] )
{
	vj_shm_t *v         = (vj_shm_t*) vv;
	vj_shared_data *data = (vj_shared_data*) v->sms;

	//@ call wrlock_wrlock N times before giving up ..

	int res = pthread_rwlock_wrlock( &data->rwlock );
	if( res == -1 ) {
		veejay_msg(0, "SHM locking error: %s",strerror(errno));
		return -1;
	}

	uint8_t *ptr = ( (uint8_t*) v->sms) + HEADER_LENGTH;
	
	uint8_t *dst[4] = { ptr, ptr + plane_sizes[0], ptr + plane_sizes[0] + plane_sizes[1], NULL };
	plane_sizes[3] = 0;
	vj_frame_copy( frame, dst, plane_sizes );


//	veejay_memcpy( ptr , frame[0], plane_sizes[0] );
//	veejay_memcpy( ptr + plane_sizes[0], frame[1], plane_sizes[1] );
//	veejay_memcpy( ptr + plane_sizes[0] +
//					plane_sizes[1], frame[2], plane_sizes[2] );

	res = pthread_rwlock_unlock( &data->rwlock );
	if( res == -1 ) {
		veejay_msg(0, "SHM locking error: %s",strerror(errno));
		return -1;
	}

	return 0;
}
/*
void	*vj_shm_new_slave(int shm_id)
{ 
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

	int r = shmget( shm_id, 0, 0666 );
	if( r == -1 ) {
		veejay_msg(0, "SHM ID '%d' gives error: %s", shm_id, strerror(errno));
		return NULL;
	}

	void *ptr = shmat( r, NULL, 0 );

	if( ptr == (char*) (-1) ) {
		veejay_msg(0, "failed to attach to shared memory %d", shm_id );
		shmctl( shm_id, IPC_RMID, NULL );
		return NULL;
	}

	vj_shm_t *v = (vj_shm_t*) vj_calloc(sizeof( vj_shm_t*));
	v->sms = ptr;
	//vj_shared_data *data = (vj_shared_data*) &(ptr[0]);


	v->shm_id = shm_id;

	veejay_msg(VEEJAY_MSG_INFO, "Attached to shared memory segment %d", shm_id );

	return v;
}*/

static	int		vj_shm_file_ref_use_this( char *path ) {
	struct stat inf;
	int res = stat( path, &inf );
	if( res == 0 ) {
		return 0; //@ no
	}
	return 1; //@ try anyway
}

static	int		vj_shm_file_ref( vj_shm_t *v, const char *homedir )
{
	char path[PATH_MAX];
	int  tries = 0;
	while( tries < 0xff ) {
		snprintf(path, sizeof(path) - 1, "%s/veejay_shm_out-%d.shm_id", homedir, tries );
		if( vj_shm_file_ref_use_this( path ) )	
			break;

		tries ++;
	}

	if(tries == 0xff) {
		veejay_msg(0, "Run out of veejay_shm_out files, cat the files and ipcs/ipcrm any shared memory resources left over by previous processes");
		veejay_msg(0, " --> %s", homedir );
		return 0;
	}

	FILE *f = fopen( path,"w+" );
	if(!f ) {
		veejay_msg(0, "I used to be able to write here but can't anymore: %s", homedir );
		return 0;
	}

	key_t key = ftok( path, tries ); //@ whatever 
	if( key == -1 ) {
		return 0;
	}

	fprintf( f, "veejay_shm_out-%d: shm_id=%d\n", tries,key );
	fclose(f );

	v->key = key;
	v->file = strdup( path );
	
	veejay_msg(VEEJAY_MSG_DEBUG, " --> %s ", path );

	return 1;
}

static 	void	failed_init_cleanup( vj_shm_t *v )
{
	if(v->file) {
		if( vj_shm_file_ref_use_this(v->file) == 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "Removed shared resource file %s", v->file );
			remove(v->file);
		}
		free(v->file);
	}
	if( v->sms && v->shm_id > 0)
		shmctl( v->shm_id, IPC_RMID, NULL );
	free(v);
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

	long size = (frame->width * frame->height * 4);

	//@ create
	v->shm_id = shmget( v->key,size, IPC_CREAT |0666 );

	if( v->shm_id == -1 ) {
		veejay_msg(0,"Error while allocating shared memory segment: %s", strerror(errno));
		failed_init_cleanup( v );
		return NULL;
	}

	//@ attach
	v->sms 	    =  shmat( v->shm_id, NULL , 0 );
	if( v->sms == NULL || v->sms == (char*) (-1) ) {
		shmctl( v->shm_id, IPC_RMID, NULL );
		veejay_msg(0, "Failed to attach to shared memory:%s",strerror(errno));
		failed_init_cleanup(v);
		return NULL;
	}

	pthread_rwlockattr_t	rw_lock_attr;
	veejay_memset( v->sms, 0, size );

	uint8_t *Y = v->sms + HEADER_LENGTH;
	uint8_t *U = Y + (frame->width * frame->height);
	uint8_t *V = U + ( (frame->width * frame->height)/2);
	
	veejay_memset( U, 128, frame->uv_len);
	veejay_memset( V, 128, frame->uv_len);

	//@ set up frame info (fixme, incomplete)
//	vj_shared_data *data = (vj_shared_data*) &(v->sms[0]);

	vj_shared_data *data = (vj_shared_data*) v->sms;
	data->resource_id    = v->shm_id;
	data->header[0]      = frame->width;
	data->header[1]      = frame->height;
	data->header[2]      = frame->stride[0];
	data->header[3]      = frame->stride[1];
	data->header[4]      = frame->stride[2];
	data->header[5]      = 513; // format LIVIDO_PALETTE_YUV422P 

	veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource:  Starting address: %p", data );
	veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource:  Frame data      : %p", data + HEADER_LENGTH );
	veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource:  Static resolution of %d x %d, YUV 4:2:2 planar",
			data->header[0],data->header[1] );
	veejay_msg(VEEJAY_MSG_DEBUG,"Shared Resource:  Planes {%d,%d,%d} format %d",
			data->header[2],data->header[3],data->header[4],data->header[5]);

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

	veejay_msg( VEEJAY_MSG_DEBUG, "Shared Memory ID = %d", v->shm_id );
	veejay_msg( VEEJAY_MSG_INFO, "(SHM) Shared Memory consumer key is %d", v->key );
	veejay_msg( VEEJAY_MSG_DEBUG, "Starting Address: %p, Frame starts at: %p, Lock at %p",
			v->sms, v->sms + HEADER_LENGTH, &(data->rwlock));


	simply_my_shmid = v->key;
	simply_my_shmkey = v->shm_id;


	return v;
}

