/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2011 Niels Elburg <nwelburg@gmail.com>
 * See COPYING for software license and distribution details
 */

/*
   
   simple generator plugin - reads frames from veejay's shared memory resource 

   set this plugin up as the first generator plugin to be loaded in veejay
 
   there is
     1) env var VEEJAY_SHMID that lists the shmid of the resource we can attach to
	 or
	 2) $HOME/.veejay/veejay.shm that lists the shmid...
 

	
	note: no frame format negotation yet


 *
 */

#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif

#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include 	"../libplugger/specs/livido.h"
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

typedef struct
{
	int					resource_id;
	pthread_rwlock_t	rwlock;
	int					header[8];
} vj_shared_data;

livido_init_f	init_instance( livido_port_t *my_instance )
{
	int shm_id = 0;
	//@ did use set shared memory id ?
	//@ use ipcs to find out, or read $HOME/.veejay/veejay.shm
	char *env_id = getenv( "VEEJAY_SHMID" );
	if( env_id == NULL ) {
		//@ try veejay homedir
		char path[1024];
		char *home = getenv("HOME");
		snprintf(path,sizeof(path)-1, "%s/.veejay/veejay.shm", home );
		int fd = open( path, O_RDWR );
		if(!fd) {
			printf("no env var VEEJAY_SHMID set and no file '%s' found!",path );
			return LIVIDO_ERROR_HARDWARE;
		}
		char buf[256];
		livido_memset(buf,0,sizeof(buf));
		read( fd, buf, 256 );
		if(sscanf(buf, "master: %d", &shm_id ) != 1 )
			return LIVIDO_ERROR_HARDWARE;
		printf(" '%s' -> shm id %d\n", path, shm_id );
		close(fd);
	} else {
		shm_id = atoi( env_id );
	}
	
	int r = shmget( shm_id, 0, 0666 );
	if( r == -1 ) {
		printf("error: %s for shm_id %d\n", strerror(errno),shm_id);
		return LIVIDO_ERROR_HARDWARE;
	}

	char *ptr = (char*) shmat( r, NULL , 0 );

	vj_shared_data *data = (vj_shared_data*) &(ptr[0]);

	if( data == (char*) (-1) ) {
		return LIVIDO_ERROR_HARDWARE;
	}

 	int error = livido_property_set( my_instance, "PLUGIN_private", 
                        LIVIDO_ATOM_TYPE_VOIDPTR, 1, &ptr );

	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	vj_shared_data *v = NULL;
    int error = livido_property_get( my_instance, "PLUGIN_private",
                        0, &v );

	if( error == LIVIDO_NO_ERROR ) {
		if( (shmdt( v ))) {
			printf("error detaching from shm.");
		}
	}
	
  	livido_property_set( my_instance , "PLUGIN_private", 
                        LIVIDO_ATOM_TYPE_VOIDPTR, 0, NULL );
	return LIVIDO_NO_ERROR;
}

livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	int i = 0;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

	int palette;
	int w;
	int h;
	
	//@ get output channel details
	int error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	int uv_len = lvd_uv_plane_len( palette,w,h );
	len = w * h;

    char  *addr = NULL; 
	error = livido_property_get( my_instance, "PLUGIN_private", 0, &addr );

	vj_shared_data *v =(vj_shared_data*) &(addr[0]);

	if( error != LIVIDO_NO_ERROR ) 
		return LIVIDO_ERROR_HARDWARE;
	
	int res = pthread_rwlock_rdlock( &v->rwlock );
	if( res == -1 ) {
		return LIVIDO_ERROR_HARDWARE;
	}

	uint8_t *ptr = addr + 4096; //v->memptr

	uint8_t *y = ptr;
	uint8_t *u = ptr + len;
	uint8_t *v1 = u+ uv_len;

	if( v->header[0] != w ) {
		printf("oops, width != width of shared resource\n");
	}
	if( v->header[1] != h ) {
		printf("oops, height != height of shared resource\n");
	}


	livido_memcpy( O[0], y, len );
	livido_memcpy( O[1], u, uv_len );
	livido_memcpy( O[2], v1, uv_len );

	res = pthread_rwlock_unlock( &v->rwlock );
	if( res == -1 ) {
		return LIVIDO_ERROR_HARDWARE;
	}


	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[3];
	livido_port_t *in_chans[3];
	livido_port_t *out_chans[1];
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
		livido_set_string_value( port, "name", "Shared Memory Reader Veejay");	
		livido_set_string_value( port, "description", "Read frame from shared resource");
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
	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	    livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 2, palettes0);
		livido_set_int_value( port, "flags", 0);
	

	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_channel_templates",0, NULL );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
