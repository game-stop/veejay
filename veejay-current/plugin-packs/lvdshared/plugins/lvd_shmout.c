/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2011 Niels Elburg <nwelburg@gmail.com>
 * See COPYING for software license and distribution details
 */

/*
   
   simple sink plugin - reads input frames and write them to a shared memory resource 

	prints resource ID and errors to stdout (fix livido)
	writes resource ID to some path, configured by env var LIVIDO_PLUGIN_SHMOUT_TMP

	how to setup:


	0. put lvd_shmin.so, lvd_shmout.so in some path and have it listed as the first line in ~/.veejay/plugins.cfg
	1. open 2x xterms
	2. in term A, export LIVIDO_PLUGIN_SHMOUT_TMP=/tmp
	3. in term A, launch veejay -v /path/to/video
	4. in veejay A, create sample and play
	5. add shared memory writer to FX chain
	6. note VEEJAY_SHMID

	7. in term B, export VEEJAY_SHMID=<number>
	8. in term B, launch veejay -v 




 *
 */

#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include 	"livido.h"
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define SHM_ADDR_OFFSET 4096

typedef struct
{
	int					resource_id;
	pthread_rwlock_t	rwlock;
	int					header[8];
} vj_shared_data;

typedef struct
{
	int	shm_id;
	char *sms;
	key_t key;
	char *filename;
} shared_video_t;

static int shmout_supported_palette(int palette)
{
	if(palette == LIVIDO_PALETTE_YUV420P || palette == LIVIDO_PALETTE_YVU420P ||
	   palette == LIVIDO_PALETTE_I420 || palette == LIVIDO_PALETTE_YUV422P ||
	   palette == LIVIDO_PALETTE_YV16 || palette == LIVIDO_PALETTE_YUV444P ||
	   palette == LIVIDO_PALETTE_YUV4444P)
		return 1;

	return 0;
}

static int shmout_uv_stride(int palette, int w)
{
	if(palette == LIVIDO_PALETTE_YUV444P || palette == LIVIDO_PALETTE_YUV4444P)
		return w;

	return (w + 1) >> 1;
}

static int shmout_uv_height(int palette, int h)
{
	if(palette == LIVIDO_PALETTE_YUV420P || palette == LIVIDO_PALETTE_YVU420P || palette == LIVIDO_PALETTE_I420)
		return (h + 1) >> 1;

	return h;
}

static long shmout_max_frame_size(int w, int h)
{
	return SHM_ADDR_OFFSET + ((long)w * (long)h * 3L);
}

static void shmout_fill_header(vj_shared_data *data, int w, int h, int palette)
{
	int uv_stride = shmout_uv_stride(palette, w);

	data->resource_id = 0;
	data->header[0] = w;
	data->header[1] = h;
	data->header[2] = w;
	data->header[3] = uv_stride;
	data->header[4] = uv_stride;
	data->header[5] = palette;
	data->header[6] = shmout_uv_height(palette, h);
	data->header[7] = 0;
}

static int vj_shm_file_ref_use_this(char *path)
{
	struct stat inf;
	int res = stat(path, &inf);
	if(res == 0) {
		return 0; //@ no
	}
	return 1; //@ try anyway
}

static int shmout_join_path(char *dst, size_t dst_len, const char *dir, int tries)
{
	char suffix[32];
	int n = snprintf(suffix, sizeof(suffix), "/lvd_shmout-%d.shm_id", tries);
	if(n < 0 || n >= (int)sizeof(suffix))
		return 0;

	size_t dir_len = strlen(dir);
	size_t suffix_len = (size_t)n;
	if(dir_len + suffix_len >= dst_len)
		return 0;

	livido_memcpy(dst, dir, dir_len);
	livido_memcpy(dst + dir_len, suffix, suffix_len + 1);
	return 1;
}

static int shmout_home_dir(char *dst, size_t dst_len, const char *home)
{
	static const char suffix[] = "/.veejay";
	size_t home_len = strlen(home);
	size_t suffix_len = sizeof(suffix) - 1;
	if(home_len + suffix_len >= dst_len)
		return 0;

	livido_memcpy(dst, home, home_len);
	livido_memcpy(dst + home_len, suffix, suffix_len + 1);
	return 1;
}

static int vj_shm_file_ref(shared_video_t *v, const char *homedir)
{
	char path[PATH_MAX];
	int tries = 0;
	while(tries < 0xff) {
		if(!shmout_join_path(path, sizeof(path), homedir, tries)) {
			printf("%s: shared-memory reference path too long for '%s'\n", __FILE__, homedir);
			return 0;
		}

		if(vj_shm_file_ref_use_this(path))
			break;
		tries++;
	}

	if(tries == 0xff) {
		printf("%s: all %s is consumed\n", __FILE__, path);
		return 0;
	}

	FILE *f = fopen(path, "w+");
	if(!f) {
		printf("%s: cannot open '%s' for writing\n", __FILE__, path);
		return 0;
	}

	key_t key = ftok(path, tries); //@ whatever
	if(key == (key_t)-1) {
		printf("%s: cannot create shm key from '%s': %s\n", __FILE__, path, strerror(errno));
		fclose(f);
		remove(path);
		return 0;
	}

	fprintf(f, "lvd_shmout-%d: shm_id=%d\n", tries, key);
	fclose(f);

	v->key = key;
	const size_t path_len = strlen(path) + 1;
	v->filename = (char*) livido_malloc(path_len);
	if(!v->filename) {
		remove(path);
		return 0;
	}
	livido_memcpy(v->filename, path, path_len);
	
	printf("saved resource id of shared memory segment to '%s'\n", path);

	return 1;
}

static void shared_video_cleanup(shared_video_t *v, int remove_segment)
{
	if(!v)
		return;

	if(v->sms && v->sms != (char*) (-1)) {
		if(shmdt(v->sms))
			printf("error detaching from shm: %s\n", strerror(errno));
		v->sms = NULL;
	}

	if(remove_segment && v->shm_id > 0) {
		if(shmctl(v->shm_id, IPC_RMID, NULL))
			printf("error removing shm: %s\n", strerror(errno));
		v->shm_id = -1;
	}

	if(v->filename) {
		remove(v->filename);
		livido_free(v->filename);
		v->filename = NULL;
	}

	livido_free(v);
}

int init_instance(livido_port_t *my_instance)
{
	shared_video_t *v = (shared_video_t*) livido_malloc(sizeof(shared_video_t));
	if(!v)
		return LIVIDO_ERROR_MEMORY_ALLOCATION;

	livido_memset(v, 0, sizeof(shared_video_t));
	v->shm_id = -1;

	char plugin_dir_buf[PATH_MAX];
	char *plugin_dir = getenv("LIVIDO_PLUGIN_SHMOUT_TMP");
	if(!plugin_dir) {
		char *homedir = getenv("HOME");
		if(!homedir) {
			printf("HOME not set!\n");
			shared_video_cleanup(v, 0);
			return LIVIDO_ERROR_ENVIRONMENT;
		}
		if(!shmout_home_dir(plugin_dir_buf, sizeof(plugin_dir_buf), homedir)) {
			printf("HOME path too long for shared-memory writer\n");
			shared_video_cleanup(v, 0);
			return LIVIDO_ERROR_ENVIRONMENT;
		}
		plugin_dir = plugin_dir_buf;
	}

	if(!vj_shm_file_ref(v, plugin_dir)) {
		shared_video_cleanup(v, 0);
		return LIVIDO_ERROR_RESOURCE;
	}

	int w = 0;
	int h = 0;
	int palette = LIVIDO_PALETTE_YUV422P;
	livido_port_t *channel = NULL;

	lvd_extract_dimensions(my_instance, "in_channels", &w, &h);
	if(livido_property_get(my_instance, "in_channels", 0, &channel) == LIVIDO_NO_ERROR)
		livido_property_get(channel, "current_palette", 0, &palette);

	if(w <= 0 || h <= 0) {
		shared_video_cleanup(v, 0);
		return LIVIDO_ERROR_HARDWARE;
	}

	if(!shmout_supported_palette(palette))
		palette = LIVIDO_PALETTE_YUV422P;

	long shm_size = shmout_max_frame_size(w, h);

	v->shm_id = shmget(v->key, shm_size, IPC_CREAT | 0666);
	if(v->shm_id == -1) {
		shared_video_cleanup(v, 0);
		return LIVIDO_ERROR_RESOURCE;
	}	

	v->sms = shmat(v->shm_id, NULL, 0);
	if(v->sms == (char*) (-1)) {
		v->sms = NULL;
		shared_video_cleanup(v, 1);
		return LIVIDO_ERROR_RESOURCE;
	}

	pthread_rwlockattr_t rw_lock_attr;
	livido_memset(v->sms, 0, shm_size);

	vj_shared_data *data = (vj_shared_data*) &(v->sms[0]);
	shmout_fill_header(data, w, h, palette);
	data->resource_id = v->key;

	int res = pthread_rwlockattr_init(&rw_lock_attr);
	if(res != 0) {
		shared_video_cleanup(v, 1);
		return LIVIDO_ERROR_RESOURCE;
	}

	res = pthread_rwlockattr_setpshared(&rw_lock_attr, PTHREAD_PROCESS_SHARED);
	if(res != 0) {
		printf("cant use PTHREAD_PROCESS_SHARED: %s", strerror(res));
		pthread_rwlockattr_destroy(&rw_lock_attr);
		shared_video_cleanup(v, 1);
		return LIVIDO_ERROR_RESOURCE;
	}
	
	res = pthread_rwlock_init(&data->rwlock, &rw_lock_attr);
	pthread_rwlockattr_destroy(&rw_lock_attr);
	if(res != 0) {
		shared_video_cleanup(v, 1);
		return LIVIDO_ERROR_RESOURCE;
	}

	int error = livido_property_set(my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 1, &v);
	if(error != LIVIDO_NO_ERROR) {
		pthread_rwlock_destroy(&data->rwlock);
		shared_video_cleanup(v, 1);
		return LIVIDO_ERROR_INTERNAL;
	}

	printf("Shared Resource ID = %d, VEEJAY_SHMID = %d\n", v->shm_id, v->key);
	printf("Starting Address: %p, Data at %p\n", v->sms, v->sms + SHM_ADDR_OFFSET);

	return LIVIDO_NO_ERROR;
}

int deinit_instance(livido_port_t *my_instance)
{
	shared_video_t *v = NULL;
	int error = livido_property_get(my_instance, "PLUGIN_private", 0, &v);

	if(error == LIVIDO_NO_ERROR && v) {
		vj_shared_data *data = (vj_shared_data*) &(v->sms[0]);
		pthread_rwlock_destroy(&data->rwlock);
		shared_video_cleanup(v, 1);
	}
	
	livido_property_set(my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 0, NULL);

	return LIVIDO_NO_ERROR;
}

int process_instance(livido_port_t *my_instance, double timecode)
{
	(void) timecode;
	uint8_t *I[4] = {NULL,NULL,NULL,NULL};
	int palette = 0;
	int w = 0;
	int h = 0;
	
	//@ get input channel details
	int error = lvd_extract_channel_values(my_instance, "in_channels", 0, &w, &h, I, &palette);
	if(error != LIVIDO_NO_ERROR)
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS; //@ error codes in livido flanky

	if(!shmout_supported_palette(palette))
		return LIVIDO_ERROR_HARDWARE;

	int uv_len = shmout_uv_stride(palette, w) * shmout_uv_height(palette, h);
	int len = w * h;

	shared_video_t *sv = NULL;
	error = livido_property_get(my_instance, "PLUGIN_private", 0, &sv);
	if(error != LIVIDO_NO_ERROR || !sv)
		return LIVIDO_ERROR_INTERNAL;

	char *addr = (char*) sv->sms;
	vj_shared_data *data = (vj_shared_data*) sv->sms;
	
	int res = pthread_rwlock_wrlock(&data->rwlock);
	if(res != 0)
		return LIVIDO_ERROR_RESOURCE;

	if(data->header[1] != h || data->header[0] != w) {
		printf("shared resource in %d x %d, your frame in %d x %d\n",
				data->header[0], data->header[1], w, h);
		pthread_rwlock_unlock(&data->rwlock);
		return LIVIDO_ERROR_RESOURCE;
	}

	shmout_fill_header(data, w, h, palette);
	data->resource_id = sv->key;

	uint8_t *ptr = (uint8_t*) addr + SHM_ADDR_OFFSET;
	uint8_t *y = ptr;
	uint8_t *u = ptr + len;
	uint8_t *v = u + uv_len;

	livido_memcpy(y, I[0], len);
	livido_memcpy(u, I[1], uv_len);
	livido_memcpy(v, I[2], uv_len);

	res = pthread_rwlock_unlock(&data->rwlock);
	if(res != 0)
		return LIVIDO_ERROR_RESOURCE;

	return LIVIDO_NO_ERROR;
}

livido_port_t *livido_setup(livido_setup_t list[], int version)
{
	(void) version;
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_chans[1];
	livido_port_t *info = NULL;
	livido_port_t *filter = NULL;

	//@ setup root node, plugin info
	info = livido_port_new(LIVIDO_PORT_TYPE_PLUGIN_INFO);
	port = info;

	livido_set_string_value(port, "maintainer", "Niels");
	livido_set_string_value(port, "version", "1");
	
	filter = livido_port_new(LIVIDO_PORT_TYPE_FILTER_CLASS);
	livido_set_int_value(filter, "api_version", LIVIDO_API_VERSION);

	//@ setup function pointers
	livido_set_voidptr_value(filter, "deinit_func", &deinit_instance);
	livido_set_voidptr_value(filter, "init_func", &init_instance);
	livido_set_voidptr_value(filter, "process_func", &process_instance);
	port = filter;

	//@ meta information
	livido_set_string_value(port, "name", "Shared Memory Writer");	
	livido_set_string_value(port, "description", "Write input frame to shared resource (owned by plugin)");
	livido_set_string_value(port, "author", "Niels Elburg");
	livido_set_int_value(port, "flags", 0);
	livido_set_string_value(port, "license", "GPL2");
	livido_set_int_value(port, "version", 1);
	
	//@ some palettes veejay-classic uses
	int palettes0[] = {
		LIVIDO_PALETTE_YUV420P,
		LIVIDO_PALETTE_YUV422P,
		LIVIDO_PALETTE_YUV444P,
		0
	};
	
	//@ setup input channel
	in_chans[0] = livido_port_new(LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE);
	port = in_chans[0];
	livido_set_string_value(port, "name", "Input Channel");
	livido_set_int_array(port, "palette_list", 3, palettes0);
	livido_set_int_value(port, "flags", 0);
	
	//@ setup the nodes
	livido_set_portptr_array(filter, "in_parameter_templates", 0, NULL);
	livido_set_portptr_array(filter, "in_channel_templates", 0, NULL);
	livido_set_portptr_array(filter, "in_channel_templates", 1, in_chans);
	livido_set_portptr_array(filter, "out_channel_templates", 0, NULL);
	livido_set_portptr_value(info, "filters", filter);
	return info;
}
