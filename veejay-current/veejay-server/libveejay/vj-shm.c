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
#include <unistd.h>
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
#include <libveejay/vj-shm.h>
#include <dirent.h>
#include <signal.h>
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
    size_t segment_size;
} vj_shm_t;


typedef struct
{
    int resource_id;
    pthread_rwlock_t rwlock;
    int header[8];
    int plane_size[4];
    uint64_t sequence;
} vj_shared_data;

static	int	just_a_shmid = 0;
static  int simply_my_shmkey  = 0;
static	key_t	simply_my_shmid = 0;

static FILE *vj_shm_open_discovery_file(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0)
        return NULL;

    struct stat st;
    if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid()) {
        close(fd);
        errno = EPERM;
        return NULL;
    }

    FILE *f = fdopen(fd, "r");
    if(!f)
        close(fd);
    return f;
}

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

static void vj_shm_cleanup_stale_files(const char *dirpath) {
    DIR *dir = opendir(dirpath);
    if (!dir) return;

    struct dirent *entry;
    char full_path[PATH_MAX];

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "shm_", 4) == 0 && strstr(entry->d_name, ".dat")) {
            int pid_to_check = 0;
            if (sscanf(entry->d_name, "shm_%d.dat", &pid_to_check) == 1) {
                if (kill(pid_to_check, 0) == -1 && errno == ESRCH) {
                    snprintf(full_path, sizeof(full_path), "%s/%s", dirpath, entry->d_name);
                    
                    FILE *f = vj_shm_open_discovery_file(full_path);
                    if (f) {
                        int key_val = -1;
                        if (fscanf(f, "pid=%*d\nkey=%d", &key_val) == 1) {
                            int shm_id = shmget(key_val, 0, 0);
                            if (shm_id != -1) {
                                shmctl(shm_id, IPC_RMID, NULL);
                            }
                        }
                        fclose(f);
                    }
                    
                    veejay_msg(VEEJAY_MSG_DEBUG, "Cleaning up stale SHM file: %s", full_path);
                    remove(full_path);
                }
            }
        }
    }
    closedir(dir);
}

static int vj_shm_file_ref_use_this(char *path) {
    int pid_in_file = -1;
    int key_in_file = -1;

    FILE *f = vj_shm_open_discovery_file(path);
    if (!f)
        return errno == ENOENT ? 1 : 0;

    if (fscanf(f, "pid=%d\nkey=%d", &pid_in_file, &key_in_file) == 2 &&
        kill(pid_in_file, 0) == -1 && errno == ESRCH) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "SHM: Reclaiming stale discovery file for dead PID %d",
                   pid_in_file);

        int shm_id = shmget(key_in_file, 0, 0);
        if (shm_id != -1)
            shmctl(shm_id, IPC_RMID, NULL);

        fclose(f);
        remove(path);
        return 1;
    }

    fclose(f);
    return 0;
}

void vj_shm_free(void *vv)
{
    vj_shm_t *v = (vj_shm_t*) vv;
    if (!v) return;

    if(v->sms && v->sms != (void*)-1) {
        shmdt(v->sms);
        v->sms = NULL;
    }

    if (v->shm_id > 0) {
        shmctl(v->shm_id, IPC_RMID, NULL);
    }

    if (v->file) {
        char *dir_ptr = strdup(v->file);
        char *last_slash = strrchr(dir_ptr, '/');
        
        remove(v->file);

        if (last_slash) {
            *last_slash = '\0';
            vj_shm_cleanup_stale_files(dir_ptr);
        }

        free(dir_ptr);
        free(v->file);
    }

    free(v);
}

void	vj_shm_set_status( void *vv, int status )
{
	vj_shm_t *v = (vj_shm_t*) vv;
    if(!v)
        return;
    __atomic_store_n(&v->status, status ? 1 : 0, __ATOMIC_RELEASE);
	if(__atomic_load_n(&v->status, __ATOMIC_ACQUIRE) == 0) {
		veejay_msg(VEEJAY_MSG_WARNING, "Stopped writing frames to SHM %d", v->key );
	} else {
		veejay_msg(VEEJAY_MSG_INFO,  "Started writing frames to SHM %d", v->key );
	}
}

int		vj_shm_get_status( void *vv )
{
	vj_shm_t *v = (vj_shm_t*) vv;
	return v ? __atomic_load_n(&v->status, __ATOMIC_ACQUIRE) : 0;
}


int		vj_shm_stop( void *vv )
{
	vj_shm_t *v = (vj_shm_t*) vv;
    if(!v)
        return -1;
    __atomic_store_n(&v->status, 0, __ATOMIC_RELEASE);
	return 0;
}

int		vj_shm_get_shm_id( void *vv )
{
	vj_shm_t *v = (vj_shm_t*) vv;
	return v->shm_id;
}

int vj_shm_get_frame_info(void *vv, vj_shm_frame_info *info)
{
    vj_shm_t *v = (vj_shm_t*)vv;
    if(!v || !v->sms || !info)
        return 0;
    vj_shared_data *data = (vj_shared_data*)v->sms;
    if(pthread_rwlock_rdlock(&data->rwlock) != 0)
        return 0;
    info->width = data->header[0];
    info->height = data->header[1];
    info->palette = data->header[5];
    info->protocol_version = data->header[6];
    for(int i = 0; i < 4; i++)
        info->plane_size[i] = data->plane_size[i];
    if(info->plane_size[0] <= 0)
        info->plane_size[0] = info->width * info->height;
    if(info->plane_size[1] <= 0)
        info->plane_size[1] = info->plane_size[0];
    if(info->plane_size[2] <= 0)
        info->plane_size[2] = info->plane_size[1];
    info->sequence = data->sequence;
    pthread_rwlock_unlock(&data->rwlock);
    return info->width > 0 && info->height > 0;
}

static int vj_shm_copy_locked(vj_shared_data *data, void *sms,
                              size_t segment_size,
                              uint8_t *dst[4], const int capacity[4])
{
    int sizes[4];
    for(int i = 0; i < 4; i++)
        sizes[i] = data->plane_size[i];
    if(sizes[0] <= 0)
        sizes[0] = data->header[0] * data->header[1];
    if(sizes[1] <= 0)
        sizes[1] = sizes[0];
    if(sizes[2] <= 0)
        sizes[2] = sizes[1];
    if(sizes[3] < 0)
        sizes[3] = 0;

    size_t total = 0;
    for(int i = 0; i < 4; i++) {
        if(sizes[i] < 0 || (size_t)sizes[i] > SIZE_MAX - total)
            return -1;
        total += (size_t)sizes[i];
    }
    if(segment_size <= HEADER_LENGTH || total > segment_size - HEADER_LENGTH)
        return -1;

    for(int i = 0; i < 4; i++) {
        if(sizes[i] > 0 && (!dst[i] || (capacity && capacity[i] < sizes[i])))
            return -1;
    }

    uint8_t *ptr = ((uint8_t*)sms) + HEADER_LENGTH;
    uint8_t *in[4] = { ptr, ptr + sizes[0], ptr + sizes[0] + sizes[1], NULL };
    if(sizes[3] > 0)
        in[3] = in[2] + sizes[2];
    vj_frame_copy(in, dst, sizes);
    return 1;
}

int vj_shm_read(void *vv, uint8_t *dst[4])
{
    vj_shm_t *v = (vj_shm_t*)vv;
    if(!v || !v->sms || !dst)
        return -1;
    vj_shared_data *data = (vj_shared_data*)v->sms;
    int res = pthread_rwlock_rdlock(&data->rwlock);
    if(res != 0)
        return -1;
    res = vj_shm_copy_locked(data, v->sms, v->segment_size, dst, NULL);
    pthread_rwlock_unlock(&data->rwlock);
    return res > 0 ? 0 : -1;
}

int vj_shm_read_latest(void *vv, uint8_t *dst[4], const int capacity[4], uint64_t *sequence)
{
    vj_shm_t *v = (vj_shm_t*)vv;
    if(!v || !v->sms || !dst || !sequence)
        return -1;
    vj_shared_data *data = (vj_shared_data*)v->sms;
    if(pthread_rwlock_rdlock(&data->rwlock) != 0)
        return -1;
    if(data->sequence == *sequence) {
        pthread_rwlock_unlock(&data->rwlock);
        return 0;
    }
    int res = vj_shm_copy_locked(data, v->sms, v->segment_size, dst, capacity);
    if(res > 0)
        *sequence = data->sequence;
    pthread_rwlock_unlock(&data->rwlock);
    return res;
}

int rot_val =0;

int vj_shm_write(void *vv, uint8_t *frame[4], int plane_sizes[4])
{
    vj_shm_t *v = (vj_shm_t*)vv;
    if(!v || !v->sms || !frame || !plane_sizes)
        return -1;

    int sizes[4] = { plane_sizes[0], plane_sizes[1], plane_sizes[2], 0 };
    if(sizes[0] < 0 || sizes[1] < 0 || sizes[2] < 0)
        return -1;
    if(v->alpha)
        sizes[3] = sizes[0];

    size_t total = 0;
    for(int i = 0; i < 4; i++) {
        if((size_t)sizes[i] > SIZE_MAX - total)
            return -1;
        total += (size_t)sizes[i];
    }
    if(v->segment_size <= HEADER_LENGTH || total > v->segment_size - HEADER_LENGTH) {
        veejay_msg(VEEJAY_MSG_ERROR, "SHM frame exceeds allocated segment");
        return -1;
    }

    vj_shared_data *data = (vj_shared_data*)v->sms;
    int res = pthread_rwlock_wrlock(&data->rwlock);
    if(res != 0) {
        veejay_msg(0, "SHM locking error: %s", strerror(res));
        return -1;
    }

    uint8_t *ptr = ((uint8_t*)v->sms) + HEADER_LENGTH;
    uint8_t *dst[4] = {
        ptr,
        ptr + sizes[0],
        ptr + sizes[0] + sizes[1],
        NULL
    };
    if(sizes[3] > 0)
        dst[3] = ptr + sizes[0] + sizes[1] + sizes[2];

    vj_frame_copy(frame, dst, sizes);
    for(int i = 0; i < 4; i++)
        data->plane_size[i] = sizes[i];
    data->sequence++;

    res = pthread_rwlock_unlock(&data->rwlock);
    if(res != 0) {
        veejay_msg(0, "SHM locking error: %s", strerror(res));
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

void	*vj_shm_new_slave(int shm_key)
{ 
	veejay_msg(VEEJAY_MSG_DEBUG, "Trying to attach to shared memory key %d", shm_key );

	int r = shmget( (key_t)shm_key, 0, 0400 );
	if( r == -1 ) {
		veejay_msg(0, "Unable to get shared memory key '%d': %s", shm_key, strerror(errno));
		return NULL;
	}

	char *ptr = shmat( r, NULL, 0 );

	if( ptr == (char*) (-1) ) {
		veejay_msg(0, "Failed to attach to shared memory segment %d", r );
		return NULL;
	}

	vj_shm_t *v = (vj_shm_t*) vj_calloc(sizeof(vj_shm_t));
    if(!v) {
        shmdt(ptr);
        return NULL;
    }
	v->sms = ptr;
    v->key = (key_t)shm_key;
    v->shm_id = r;
    struct shmid_ds ds;
    if(shmctl(r, IPC_STAT, &ds) != 0) {
        shmdt(ptr);
        free(v);
        return NULL;
    }
    v->segment_size = ds.shm_segsz;
	vj_shared_data *data = (vj_shared_data*) &(ptr[0]);

	int palette = data->header[5];
	int width   = data->header[0];
	int height  = data->header[1];

	veejay_msg(VEEJAY_MSG_DEBUG, "Veejay shared resource publish information: %dx%d@%d", width, height, palette);

	veejay_msg(VEEJAY_MSG_INFO, "Attached to shared memory key %d (segment %d)", shm_key, r );

	return v;
}

void *vj_shm_new_slave_by_pid(const char *homedir, int pid) 
{
    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/.veejay_shm/shm_%d.dat", homedir, pid);
    
    FILE *f = vj_shm_open_discovery_file(filepath);
    if (!f) return NULL;

    int key_val;
    if (fscanf(f, "pid=%*d\nkey=%d", &key_val) != 1) {
        fclose(f);
        return NULL;
    }
    fclose(f);

    return vj_shm_new_slave(key_val);
}

static int vj_shm_file_ref(vj_shm_t *v, const char *homedir)
{
    char dirpath[PATH_MAX];
    char filepath[PATH_MAX + 64];
    
    snprintf(dirpath, sizeof(dirpath), "%s/.veejay_shm", homedir);
	if (mkdir(dirpath, 0700) == -1 && errno != EEXIST) {
        veejay_msg(0, "SHM: Failed to create directory %s", dirpath);
        return 0;
    }

    pid_t my_pid = getpid();
    snprintf(filepath, sizeof(filepath), "%s/shm_%d.dat", dirpath, my_pid);

    int ref_fd = open(filepath,
                      O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                      S_IRUSR | S_IWUSR);
    FILE *f = ref_fd >= 0 ? fdopen(ref_fd, "w") : NULL;
    if (!f) {
        if(ref_fd >= 0)
            close(ref_fd);
        veejay_msg(0, "SHM Error: Could not create discovery file %s", filepath);
        return 0;
    }

    key_t key = ftok(filepath, 64);
    if (key == -1) {
        fclose(f);
        return 0;
    }

    fprintf(f, "pid=%d\nkey=%d\n", my_pid, key);
    fclose(f);

    v->key = key;
    v->file = strdup(filepath);
    
    veejay_msg(VEEJAY_MSG_INFO, "SHM Discovery file created: %s", filepath);
    return 1;
}

static 	void	failed_init_cleanup( vj_shm_t *v )
{
    if(v->sms && v->sms != (void*)-1) {
        shmdt(v->sms);
        v->sms = NULL;
    }
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

void	*vj_shm_new_master( const char *homedir, VJFrame *frame)
{
	char dirpath[PATH_MAX];
    snprintf(dirpath, sizeof(dirpath), "%s/.veejay_shm", homedir);

    if (mkdir(dirpath, 0700) == -1 && errno != EEXIST) {
        veejay_msg(0, "SHM: Failed to create directory %s", dirpath);
        return NULL;
    }

    vj_shm_cleanup_stale_files(dirpath);	

	vj_shm_t *v = (vj_shm_t*) vj_calloc(sizeof(vj_shm_t));
	v->parent   = 1;

	if( vj_shm_file_ref( v, homedir ) == 0 ) {
        free(v);
        return NULL;
    }

    if(!frame || frame->width <= 0 || frame->height <= 0)
        return NULL;
    const size_t pixels = (size_t)frame->width * (size_t)frame->height;
    if(pixels > (SIZE_MAX - HEADER_LENGTH) / 4u) {
        failed_init_cleanup(v);
        return NULL;
    }
	size_t size = HEADER_LENGTH + pixels * 4u;

	//@ create
	v->shm_id = shmget( v->key, size, IPC_CREAT | 0600 );

	if( v->shm_id == -1 ) {
		veejay_msg(0,"Error while allocating shared memory segment of size %ld: %s",size, strerror(errno));
		failed_init_cleanup( v );
		return NULL;
	}

	//@ attach
	v->sms 	    =  shmat( v->shm_id, NULL , 0 );
    v->segment_size = size;
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
    data->header[6]      = VJ_SHM_PROTOCOL_VERSION;
    data->plane_size[0]  = frame->len;
    data->plane_size[1]  = frame->uv_len;
    data->plane_size[2]  = frame->uv_len;
    data->plane_size[3]  = 0;
    data->sequence       = 0;

/*	veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource:  Starting address: %p", data );
	veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource:  Frame data      : %p", data + HEADER_LENGTH );
	veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource:  Static resolution of %d x %d, YUV 4:2:2 planar",
			data->header[0],data->header[1] );
	veejay_msg(VEEJAY_MSG_DEBUG,"Shared Resource:  Planes {%d,%d,%d,X} LVD pixel format %d",
			data->header[2],data->header[3],data->header[4],data->header[5]);
*/
	//v->alpha = 0;

	if(v->alpha) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Shared Resource: includes alpha channel information");
	}

	int	res	= pthread_rwlockattr_init( &rw_lock_attr );
	if(res != 0) {
		veejay_msg(0, "Failed to create rw lock: %s", strerror(res));
		shmctl( v->shm_id, IPC_RMID, NULL );
		free(v);
		return NULL;
	}

	res	    = pthread_rwlockattr_setpshared( &rw_lock_attr, PTHREAD_PROCESS_SHARED );
	if(res != 0) {
		veejay_msg(0, "Failed to set PTHREAD_PROCESS_SHARED: %s", strerror(res));
		shmctl( v->shm_id, IPC_RMID, NULL );
		free(v);
		return NULL;
	}

	res	    = pthread_rwlock_init( &data->rwlock, &rw_lock_attr );
	if(res != 0) {
		shmctl(v->shm_id, IPC_RMID , NULL );
		veejay_msg(0, "Failed to initialize rw lock: %s", strerror(res));
		free(v);
		return NULL;
	}

    pthread_rwlockattr_destroy(&rw_lock_attr);
	veejay_msg( VEEJAY_MSG_DEBUG, "Initialized Shared Resource (%x)", v->key );

	simply_my_shmid = v->key;
	simply_my_shmkey = v->shm_id;

	return v;
}

