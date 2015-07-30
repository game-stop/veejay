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

	note: no frame format negotation yet


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

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include 	"../libplugger/specs/livido.h"
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

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

static	int		vj_shm_file_ref_use_this( char *path ) {
	struct stat inf;
	int res = stat( path, &inf );
	if( res == 0 ) {
		return 0; //@ no
	}
	return 1; //@ try anyway
}

static	int		vj_shm_file_ref( shared_video_t *v, const char *homedir )
{
	char path[PATH_MAX];
	int  tries = 0;
	while( tries < 0xff ) {
		snprintf(path, sizeof(path) - 1, "%s/lvd_shmout-%d.shm_id", homedir, tries );
		if( vj_shm_file_ref_use_this( path ) )	
			break;
		tries ++;
	}

	if(tries == 0xff) {
		printf("%s: all %s is consumed\n", __FILE__, path );
		return 0;
	}

	FILE *f = fopen( path,"w+" );
	if(!f ) {
		printf("%s: cannot open '%s' for writing\n", __FILE__, path );
		return 0;
	}

	key_t key = ftok( path, tries ); //@ whatever 

	fprintf( f, "lvd_shmout-%d: shm_id=%d\n", tries,key );
	fclose(f );

	v->key = key;
	v->filename = strdup( path );
	
	printf( "saved resource id of shared memory segment to '%s'\n", path );

	return 1;
}

static 	void	failed_init_cleanup( shared_video_t *v )
{
	if(v->filename)
		free(v->filename);
	if( v->sms && v->shm_id > 0)
		shmctl( v->shm_id, IPC_RMID, NULL );
	free(v);
}

livido_init_f	init_instance( livido_port_t *my_instance )
{
	shared_video_t *v = (shared_video_t*) livido_malloc( sizeof( shared_video_t ) );

	char *plugin_dir  = getenv("LIVIDO_PLUGIN_SHMOUT_TMP");
	if(!plugin_dir) {
		char path[1024];
		char *homedir = getenv("HOME");
		if(!homedir) {
			printf("HOME not set!\n");
			return LIVIDO_ERROR_ENVIRONMENT;
		}
		snprintf( path, sizeof(path)-1, "%s/.veejay", homedir );

		plugin_dir = path;
	}

	if(!vj_shm_file_ref( v, plugin_dir ) ) {
		return LIVIDO_ERROR_RESOURCE;
	}

	int w = 0, h = 0;
	int depth = 1;

	lvd_extract_dimensions( my_instance, "in_channels", &w, &h );

	if(w <=0 || h <=0 )
		return LIVIDO_ERROR_HARDWARE;

	long shm_size = (w * h * 4); //@ fixme bitdepth
	long offset   = 4096;

	v->shm_id = shmget( v->key, shm_size, IPC_CREAT | 0666 );
	if( v->shm_id == -1 ) {
		failed_init_cleanup(v);
		return LIVIDO_ERROR_RESOURCE;
	  }	

	v->sms 	  = shmat( v->shm_id, NULL, 0 );
	if( v->sms == NULL || v->sms == (char*) (-1) ) {
		failed_init_cleanup(v);
		return LIVIDO_ERROR_RESOURCE;
	}

	pthread_rwlockattr_t rw_lock_attr;
	
	memset( v->sms, 0, shm_size );

	//@ setup frame info fixme, incomplete
	
	vj_shared_data *data = (vj_shared_data*) &(v->sms[0]);

	data->header[0] = w;
	data->header[1] = h;
	data->header[2] = w;
	data->header[3] = w / 2;
	data->header[4] = w / 2;
	data->header[5] = LIVIDO_PALETTE_YUV422P; //@ fixme, more more!

//	printf("start addr: %p, frame data: %p, res %d x %d\n",data,data+4096,w,h);

	int res = pthread_rwlockattr_init( &rw_lock_attr );
	if( res == -1 ) {
		failed_init_cleanup(v);
		return LIVIDO_ERROR_RESOURCE;
	}

	res	    = pthread_rwlockattr_setpshared( &rw_lock_attr, PTHREAD_PROCESS_SHARED );
	if( res == -1 ) {
		printf("cant use PTHREAD_PROCESS_SHARED: %s",strerror(errno));
		failed_init_cleanup(v);
		return LIVIDO_ERROR_RESOURCE;
	}
	
	res		= pthread_rwlock_init( &data->rwlock, &rw_lock_attr );
	if( res == -1 ) {
		failed_init_cleanup(v);
		return LIVIDO_ERROR_RESOURCE;
	}
  	livido_property_set( my_instance , "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 1, &v );

	printf("Shared Resource ID = %d, VEEJAY_SHMID = %d\n", v->shm_id,v->key );
	printf("Starting Address: %p, Data at %p\n", v->sms, v->sms + 4096 );

	return LIVIDO_NO_ERROR;
}

livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	shared_video_t *v = NULL;
    	int error = livido_property_get( my_instance, "PLUGIN_private",0, &v );

	if( error == LIVIDO_NO_ERROR ) {
		vj_shared_data *data = (vj_shared_data*) &(v->sms[0]);
		
		pthread_rwlock_destroy( &data->rwlock );

		if( v->sms != NULL ) {
			if( (shmdt( v->sms ))) {
				printf("error detaching from shm.");
			}
		}

		if( v->filename ) {
			remove( v->filename );
		}

		failed_init_cleanup(v);
		v = NULL;
	}
	
  	livido_property_set( my_instance , "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 0, NULL );

	return LIVIDO_NO_ERROR;
}

int		process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	int i = 0;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *I[4]= {NULL,NULL,NULL,NULL};

	int palette;
	int w;
	int h;
	
	//@ get output channel details
	int error	  = lvd_extract_channel_values( my_instance, "in_channels", 0, &w,&h, I,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS; //@ error codes in livido flanky

	int uv_len = lvd_uv_plane_len( palette,w,h );
	len = w * h;

	shared_video_t *v = NULL;
	error = livido_property_get( my_instance, "PLUGIN_private", 0, &v );
	if( error != LIVIDO_NO_ERROR ) 
		return LIVIDO_ERROR_INTERNAL;

	char	*addr = (char*)v->sms;
	vj_shared_data *data = (vj_shared_data*) v->sms;
	
	int res = pthread_rwlock_wrlock( &data->rwlock );
	if( res == -1 ) {
		return LIVIDO_ERROR_RESOURCE;
	}

	uint8_t *ptr = addr + 4096; 
	uint8_t *y = ptr;
	uint8_t *u = ptr + len;
	uint8_t *v1 = u+ uv_len;

	if( data->header[1] != h || data->header[0] != w ) {
		printf("shared resource in %d x %d, your frame in %d x %d\n",
				data->header[0],data->header[1],w,h);
		pthread_rwlock_unlock( &data->rwlock );
		return LIVIDO_ERROR_RESOURCE;
	}
	livido_memcpy( y, I[0], len );
	livido_memcpy( u, I[1], uv_len );
	livido_memcpy( v1, I[2], uv_len );

	res = pthread_rwlock_unlock( &data->rwlock );
	if( res == -1 ) {
		return LIVIDO_ERROR_RESOURCE;
	}

	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[3];
	livido_port_t *in_chans[1];
	livido_port_t *info = NULL;
	livido_port_t *filter = NULL;

	//@ setup root node, plugin info
	info = livido_port_new( LIVIDO_PORT_TYPE_PLUGIN_INFO );
	port = info;

		livido_set_string_value( port, "maintainer", "Niels");
		livido_set_string_value( port, "version","1");
	
	filter = livido_port_new( LIVIDO_PORT_TYPE_FILTER_CLASS );
	livido_set_int_value( filter, "api_version", LIVIDO_API_VERSION );

	//@ setup function pointers
	livido_set_voidptr_value( filter, "deinit_func", &deinit_instance );
	livido_set_voidptr_value( filter, "init_func", &init_instance );
	livido_set_voidptr_value( filter, "process_func", &process_instance );
	port = filter;

	//@ meta information
		livido_set_string_value( port, "name", "Shared Memory Writer");	
		livido_set_string_value( port, "description", "Write input frame to shared resource (owned by plugin)");
		livido_set_string_value( port, "author", "Niels Elburg");
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	//@ some palettes veejay-classic uses
	int palettes0[] = {
           	LIVIDO_PALETTE_YUV422P,
            0
	};
	
	//@ setup output channel
	in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = in_chans[0];
	
	    livido_set_string_value( port, "name", "Input Channel");
		livido_set_int_array( port, "palette_list", 2, palettes0);
		livido_set_int_value( port, "flags", 0);
	

	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_channel_templates",0, NULL );
	livido_set_portptr_array( filter, "in_channel_templates", 1, in_chans );
	livido_set_portptr_array( filter, "out_channel_templates", 0, NULL );
	livido_set_portptr_value(info, "filters", filter);
	return info;
}
