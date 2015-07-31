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
#include <unistd.h>
#include <string.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>

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


static	inline int	lvd_to_ffmpeg( int lvd, int fr ) {
	switch( lvd ) {
		case LIVIDO_PALETTE_YUV422P:
			if( fr )
				return PIX_FMT_YUVJ422P;
			return PIX_FMT_YUV422P;
		case LIVIDO_PALETTE_RGB24:
			return PIX_FMT_RGB24;
		case LIVIDO_PALETTE_RGBA32:
			return PIX_FMT_RGBA;
		default:
			if( fr ) 
				return PIX_FMT_YUVJ422P;
			return PIX_FMT_YUV422P;
	}
	return PIX_FMT_YUV422P;
}

livido_init_f	init_instance( livido_port_t *my_instance )
{
	int shm_id = 0;
	char *env_id = getenv( "VEEJAY_SHMID" );
	
 	if ( livido_property_get( my_instance, "HOST_shmid", 0, &shm_id ) == LIVIDO_NO_ERROR ) {
		if( shm_id != 0 )
			env_id = NULL; //@ use shm_id from HOST instead.
	}

	if( env_id == NULL && shm_id == 0) {
		//@ try veejay homedir last resort.
		char path[1024];
		char *home = getenv("HOME");
		snprintf(path,sizeof(path)-1, "%s/.veejay/veejay.shm", home );
		int fd = open( path, O_RDWR );
		if(fd <= 0) {
			printf("no env var VEEJAY_SHMID set and no file '%s' found!\n",path );
			return LIVIDO_ERROR_ENVIRONMENT;
		}
		char buf[256];
		livido_memset(buf,0,sizeof(buf));
		read( fd, buf, 256 );
		if(sscanf(buf, "master: %d", &shm_id ) != 1 )
			return LIVIDO_ERROR_ENVIRONMENT;
		close(fd);
	} 
	else if( env_id != NULL ) {
		shm_id = atoi( env_id );
	}
	
	int r = shmget( shm_id, 0, 0400 );
	if( r == -1 ) {
		printf("error: %s for shm_id %d\n", strerror(errno),shm_id);
		return LIVIDO_ERROR_ENVIRONMENT;
	}

	char *ptr = (char*) shmat( r, NULL , 0 );

	vj_shared_data *data = (vj_shared_data*) &(ptr[0]);

	if( data == (char*) (-1) ) {
		return LIVIDO_ERROR_RESOURCE;
	}

	int dst_w = 0;
	int dst_h = 0;
  
   	lvd_extract_dimensions( my_instance, "out_channels", &dst_w, &dst_h );

	//@ read format and dimensions from shared memory
	int lvd_shm_palette = data->header[5]; //@ read livido palette format 
	int	lvd_shm_width	= data->header[0]; //@ read width of frame in shm
	int lvd_shm_height  = data->header[1]; //@ ... height ...
	int cpu_flags		= 0;
	cpu_flags		    = cpu_flags | SWS_FAST_BILINEAR;

//@ uncomment this to enable some runtime optimizations

//  cpu_flags			= cpu_flags | SWS_CPU_CAPS_MMX;
//  cpu_flags		    = cpu_flags | SWS_CPU_CAPS_SSE;


//@ intialize ffmpeg's libswscale context

	int fullrange = 0;
	
	livido_property_get( my_instance, "HOST_fullrange",0,&fullrange);


	struct	SwsContext	*sws = NULL;

	sws					= sws_getContext(
							lvd_shm_width,
							lvd_shm_height,
							lvd_to_ffmpeg( lvd_shm_palette, fullrange ),
							dst_w,
							dst_h,
							/*
							  PIX_FMT_YUVJ422P or PIX_FMT_YUV422P

							*/
							( fullrange == 1 ? PIX_FMT_YUVJ422P :  PIX_FMT_YUV422P ), 
							cpu_flags,
							NULL,
							NULL,
							NULL );

	if( !sws ) {
		return LIVIDO_ERROR_HARDWARE;
	}

 	int error = livido_property_set( my_instance, "PLUGIN_private", 
                        LIVIDO_ATOM_TYPE_VOIDPTR, 1, &ptr );

	error	  = livido_property_set( my_instance, "PLUGIN_scaler",
						LIVIDO_ATOM_TYPE_VOIDPTR,1, &sws );

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

	struct SwsContext *sws = NULL;

	error	= livido_property_get( my_instance, "PLUGIN_scaler",0, &sws );

	if( error == LIVIDO_NO_ERROR ) {
		sws_freeContext( sws ); //@ destroy it
	}
	
  	livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 0, NULL );
	livido_property_set( my_instance, "PLUGIN_scaler", LIVIDO_ATOM_TYPE_VOIDPTR,0,NULL );
	

	return LIVIDO_NO_ERROR;
}

livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

	int palette;
	int w;
	int h;
	
	//@ get output channel details
	int error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS; //@ error codes in livido flanky

	int uv_len = lvd_uv_plane_len( palette,w,h );

    char  *addr = NULL; 
	error = livido_property_get( my_instance, "PLUGIN_private", 0, &addr );
	if( error != LIVIDO_NO_ERROR ) 
		return LIVIDO_ERROR_INTERNAL;

	struct SwsContext *sws = NULL;
	error = livido_property_get( my_instance, "PLUGIN_scaler", 0, &sws );
	if( error != LIVIDO_NO_ERROR ) 
		return LIVIDO_ERROR_INTERNAL;

	vj_shared_data *v =(vj_shared_data*) &(addr[0]);
	
	int res = pthread_rwlock_rdlock( &v->rwlock );
	if( res == -1 ) {
		return LIVIDO_ERROR_RESOURCE;
	}

	uint8_t *start_addr = addr + 4096; //v->memptr

	int fullrange = 0;
	error = livido_property_get( my_instance, "HOST_fullrange",0,&fullrange );

	int srcFormat = lvd_to_ffmpeg( v->header[5], fullrange );
	int srcW      = v->header[0];
	int srcH	  = v->header[1];

	int	 n = 1; //@ stride width
	if( srcFormat == PIX_FMT_RGB24 )
		n = 3;
	if( srcFormat == PIX_FMT_RGB32 )
		n = 4;

	int  strides[4] = 		{ srcW * n, 0, 0, 0 };
	int  dst_strides[4] = 	{ w, w>>1, w>>1, 0 }; //@ width >> 1 

	uint8_t *in[4] = { //@ pointers to planes in shm
		start_addr, 
		NULL,
		NULL,
		NULL };

	if( srcFormat == PIX_FMT_YUVJ422P || srcFormat == PIX_FMT_YUV422P ) {
		strides[0] = srcW;
		strides[1] = strides[0] >> 1;
		strides[2] = strides[1];
		in[1] = in[0] + ( srcW * srcH );
		in[2] = in[1] + ( (srcW>>1) * srcH);
	}

	sws_scale( sws, (const uint8_t *const *)in, strides,0, srcH,(uint8_t * const*) O, dst_strides );

	res = pthread_rwlock_unlock( &v->rwlock );
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
	livido_port_t *info = NULL;
	livido_port_t *filter = NULL;
	livido_port_t *out_chans[1];

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

		livido_set_int_value( port, "HOST_shmid", 0 ); //@ hint host 
	
	//@ some palettes veejay-classic uses
	int palettes0[] = {
           	LIVIDO_PALETTE_YUV422P,
			LIVIDO_PALETTE_RGB24,
			LIVIDO_PALETTE_RGBA32,
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
