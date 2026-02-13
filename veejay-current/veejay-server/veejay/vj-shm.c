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
#include <veejaycore/defs.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <libvje/vje.h>
#include <veejaycore/vevo.h>
#include <libavutil/pixfmt.h>
#include <libavutil/avutil.h>
#include <libplugger/specs/livido.h>
#include <veejaycore/avcommon.h>
#include <veejay/vj-shm.h>
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
	int alpha;
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

int		vj_shm_get_my_shmid(void) {
	return simply_my_shmkey;
}

int		vj_shm_get_my_id(void) {
	return simply_my_shmid;
}

int		vj_shm_get_id(void){ 
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
		veejay_msg(0, "Unable to acquire lock: %s",strerror(errno));
		return 0;
	}
	uint8_t *ptr = ( (uint8_t*) v->sms ) + HEADER_LENGTH;
	
	int len = data->header[0] * data->header[1]; 
	uint8_t *in[4] = { ptr, ptr + len, ptr + len + len,NULL };
	int strides[4]    = { len, len, len,0 };


	if(data->header[5] == LIVIDO_PALETTE_YUVA8888 || data->header[5] == LIVIDO_PALETTE_YUVA422) {
		strides[3] = len;
		in[3] = in[2] + len;
	}

	vj_frame_copy( in, dst, strides );

	res = pthread_rwlock_unlock( &data->rwlock );
	if( res == -1 ) {
		veejay_msg(0, "Unable to release lock: %s",strerror(errno));
		return 0;
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

	if( v->alpha ) {
		dst[3] = ptr + plane_sizes[0] + plane_sizes[1] + plane_sizes[2];
		plane_sizes[3] = plane_sizes[0];
	}

	vj_frame_copy( frame, dst, plane_sizes );

	res = pthread_rwlock_unlock( &data->rwlock );
	if( res == -1 ) {
		veejay_msg(0, "SHM locking error: %s",strerror(errno));
		return -1;
	}

	return 0;
}

void	vj_shm_free_slave(void *slave)
{
	vj_shm_t *v = (vj_shm_t*) slave;

	if( shmdt( v->sms ) ) {
		veejay_msg(0, "Error detaching from shared resource" );
	}

	free(v);
}

void	*vj_shm_new_slave(int shm_id)
{ 
	veejay_msg(VEEJAY_MSG_DEBUG, "Trying to attach to shared memory segment %d", shm_id );

	int r = shmget( shm_id, 0, 0400 );
	if( r == -1 ) {
		veejay_msg(0, "Unable to get shared memory segment '%d': %s", shm_id, strerror(errno));
		return NULL;
	}

	char *ptr = shmat( r, NULL, 0 );

	if( ptr == (char*) (-1) ) {
		veejay_msg(0, "Failed to attach to shared memory segment %d", shm_id );
		shmctl( shm_id, IPC_RMID, NULL );
		return NULL;
	}

	vj_shm_t *v = (vj_shm_t*) vj_calloc(sizeof( vj_shm_t*));
	v->sms = ptr;
	vj_shared_data *data = (vj_shared_data*) &(ptr[0]);

	int palette = data->header[5];
	int width   = data->header[0];
	int height  = data->header[1];

	veejay_msg(VEEJAY_MSG_DEBUG, "Veejay shared resource publish information: %dx%d@d",width,height,palette);

	v->shm_id = shm_id;

	veejay_msg(VEEJAY_MSG_INFO, "Attached to shared memory segment %d", shm_id );

	return v;
}

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
		veejay_msg(0,"Ran out of veejay_shm_out files, please remove them from %s", homedir);
		return 0;
	}

	FILE *f = fopen( path,"w+" );
	if(!f ) {
		veejay_msg(0, "I used to be able to write here but can't anymore: %s", homedir );
		return 0;
	}

	key_t key = ftok( path, 128 );
	if( key == -1 ) {
		return 0;
	}

	fprintf( f, "veejay_shm_out-%d: shm_id=%d\n", tries,key );
	fclose(f );

	v->key = key;
	v->file = strdup( path );
	
	veejay_msg(VEEJAY_MSG_DEBUG, "SHM resource file written to %s ", path );

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

static	int	ffmpeg_to_lvd(int fmt)
{
	int res;
	switch(fmt) {
		case PIX_FMT_YUVA422P: res = LIVIDO_PALETTE_YUV422P; break;
		case PIX_FMT_YUVA444P: res = LIVIDO_PALETTE_YUV444P; break;
		case PIX_FMT_YUVJ422P: 
		case PIX_FMT_YUV422P: res = LIVIDO_PALETTE_YUV422P; break;
		case PIX_FMT_YUVJ444P: 
		case PIX_FMT_YUV444P: res = LIVIDO_PALETTE_YUV444P; break;
		default:
			res = LIVIDO_PALETTE_YUV422P;
			break;
	}
	return res;
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

	size_t size = (HEADER_LENGTH + (frame->width * frame->height * 4));

	//@ create
	v->shm_id = shmget( v->key,size, IPC_CREAT |0666 );

	if( v->shm_id == -1 ) {
		veejay_msg(0,"Error while allocating shared memory segment of size %ld: %s",size, strerror(errno));
		failed_init_cleanup( v );
		return NULL;
	}

	//@ attach
	v->sms 	    =  shmat( v->shm_id, NULL , 0 );
	if( v->sms == NULL || v->sms == (char*) (-1) ) {
		shmctl( v->shm_id, IPC_RMID, NULL );
		veejay_msg(0, "Failed to attach to shared memory segment: %s",strerror(errno));
		failed_init_cleanup(v);
		return NULL;
	}

	pthread_rwlockattr_t	rw_lock_attr;
	veejay_memset( v->sms, 0, size );
	uint8_t *sms_addr = (uint8_t*) v->sms;
		
	uint8_t *Y = sms_addr + HEADER_LENGTH;
	uint8_t *U = Y + frame->len;
	uint8_t *V = U + frame->uv_len;
	
	veejay_memset( U, 128, frame->uv_len);
	veejay_memset( V, 128, frame->uv_len);

	vj_shared_data *data = (vj_shared_data*) v->sms;
	data->resource_id    = v->shm_id;
	data->header[0]      = frame->width;
	data->header[1]      = frame->height;
	data->header[2]      = frame->stride[0];
	data->header[3]      = frame->stride[1];
	data->header[4]      = frame->stride[2];
	data->header[5]      = LIVIDO_PALETTE_YUV422P;

/*	veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource:  Starting address: %p", data );
	veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource:  Frame data      : %p", data + HEADER_LENGTH );
	veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource:  Static resolution of %d x %d, YUV 4:2:2 planar",
			data->header[0],data->header[1] );
	veejay_msg(VEEJAY_MSG_DEBUG,"Shared Resource:  Planes {%d,%d,%d,X} LVD pixel format %d",
			data->header[2],data->header[3],data->header[4],data->header[5]);
*/
	v->alpha = 0;

	if(v->alpha) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource: includes alpha channel information");
	}

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

	veejay_msg( VEEJAY_MSG_DEBUG, "Initialized Shared Resource (%x)", v->key );

	simply_my_shmid = v->key;
	simply_my_shmkey = v->shm_id;

	return v;
}

