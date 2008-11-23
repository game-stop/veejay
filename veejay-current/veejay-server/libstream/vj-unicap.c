/* veejay - Linux VeeJay Unicap interface
 * 	     (C) 2002-2006 Niels Elburg <nwelburg@gmail.com> 
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
#ifdef HAVE_UNICAP
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h> // for memset
#include <unicap.h>
#include <unicap_status.h>
#include <veejay/vims.h>
#include <libvje/vje.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libyuv/yuvconv.h>
#include <libvevo/libvevo.h>
#include <libstream/vj-unicap.h>
#include AVUTIL_INC
#include <pthread.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#define BUFFERS 4

typedef struct
{
	unicap_handle_t handle;
	unicap_device_t device;
	unicap_format_t format_spec;
	unicap_format_t format;
	unicap_data_buffer_t buffer;	
	unicap_data_buffer_t mbuf[BUFFERS];
	pthread_mutex_t mutex;
	pthread_t	thread;
	pthread_attr_t	attr;
	uint8_t		*priv_buf;
	int		state;
	int	 deviceID;
	int	 sizes[3];
	int	active;
	int	rgb;
	void	*sampler;
	char 	*ctrl[16];
	int	 option[16];
	int 	ready;
	int	frame_size;
	int	pause;
	int	dst_width;
	int	dst_height;
	int	src_width;
	int	src_height;
	int	dst_fmt;
	int	src_fmt;
	int	src_sizes[3];
	int	dst_sizes[3];
	sws_template	template;
	void	*scaler;
	int	composite;
} vj_unicap_t;

static struct {
	int i;
	char *s;
} pixstr[] = {
	{PIX_FMT_YUV420P, "YUVPIX_FMT_YUV420P"},
{	PIX_FMT_YUV422P, "4:2:2 planar, Y-Cb-Cr ( 422P )"},
{	PIX_FMT_YUVJ420P, "4:2:0 planar, Y-U-V (420P JPEG)"},
{	PIX_FMT_YUVJ422P, "4:2:2 planar, Y-U-V (422P JPEG)"},
{	PIX_FMT_RGB24,	  "RGB 24 bit"},
{	PIX_FMT_BGR24,	  "BGR 24 bit"},
{	PIX_FMT_YUV444P,  "YUV 4:4:4 planar, Y-Cb-Cr (444P)"},
{	PIX_FMT_YUVJ444P, "YUV 4:4:4 planar, Y-U-V (444P JPEG)"},
{	PIX_FMT_RGB32,	  "RGB 32 bit"},
{	PIX_FMT_BGR32,	  "BGR 32 bit"},
{	PIX_FMT_GRAY8,	  "Greyscale"},
{	PIX_FMT_RGB32_1,  "RGB 32 bit LE"},
{	0	,		NULL}

};


static const	char	*unicap_pf_str(int i)
{
	int j;
	for( j = 0; pixstr[j].s != NULL ; j ++ ) {
		if( i == pixstr[j].i )
			return pixstr[j].s;
	}
	return NULL;
}

static void	unicap_report_error( vj_unicap_t *v, char *err_msg ) 
{

	veejay_msg(0,"%s: %s", err_msg, v->device.identifier );
	veejay_msg(VEEJAY_MSG_DEBUG,
		"Capture device delivers in %s, %dx%d strides=%d,%d,%d",
			unicap_pf_str(v->src_fmt), v->src_width,v->src_height,v->src_sizes[0],v->src_sizes[1],v->src_sizes[2]);
	veejay_msg(VEEJAY_MSG_DEBUG,
		"System expects %s, %dx%d, strides=%d,%d,%d",
			unicap_pf_str(v->dst_fmt), v->dst_width,v->dst_height,v->dst_sizes[0],v->dst_sizes[1],v->dst_sizes[2]);

}

typedef struct
{
	unicap_handle_t handle;
	unicap_device_t device;
	unicap_format_t format_spec;
	unicap_format_t format;
	unicap_data_buffer_t buffer;
	void	*device_list;
	int	 num_devices;
	int	devices[16];
} unicap_driver_t;

static void	*unicap_reader_thread(void *data);
static int	vj_unicap_start_capture_( void *vut );
static int	vj_unicap_stop_capture_( void *vut );
extern int	get_ffmpeg_pixfmt( int id );
static	void	vj_unicap_new_frame_cb( unicap_event_t event, unicap_handle_t handle, 
			unicap_data_buffer_t *ready_buffer, vj_unicap_t *v );

#ifdef STRICT_CHECKING
static void lock__(vj_unicap_t *t, const char *f, int line) {
	pthread_mutex_lock(&(t->mutex));
}
static void unlock__(vj_unicap_t *t, const char *f, int line) {
	pthread_mutex_unlock(&(t->mutex));
}
#define lock_(t) lock__(t,__FUNCTION__,__LINE__)
#define unlock_(t) unlock__(t, __FUNCTION__ ,__LINE__)

#else
static void lock_(vj_unicap_t *t)
{
        pthread_mutex_lock( &(t->mutex ));
}
static void unlock_(vj_unicap_t *t)
{
        pthread_mutex_unlock( &(t->mutex ));
}
#endif

static int	vj_unicap_scan_enumerate_devices(void *unicap)
{
	int i=0;
	unicap_driver_t *ud = (unicap_driver_t*) unicap;
	char key[64];

	memset( &(ud->device) , 0, sizeof(unicap_device_t));

	unicap_void_device(&(ud->device));

	
	while( SUCCESS( unicap_enumerate_devices( NULL, &(ud->device), i )) )
	{
		char *device_name = strdup( ud->device.identifier );
		void	*device_port = vpn( VEVO_ANONYMOUS_PORT );
		char *device_location = strdup( ud->device.device );
		int error = vevo_property_set( device_port,
					       "name",
					       VEVO_ATOM_TYPE_STRING,
					       1,
					       &device_name);

		veejay_msg( VEEJAY_MSG_DEBUG, "\tDevice %d: %s (%s)", i, ud->device.identifier, device_location );

#ifdef STRICT_CHECKING
		assert( error ==  VEVO_NO_ERROR );
#endif
		sprintf(key ,"%d", i );
		error = vevo_property_set( ud->device_list, key, VEVO_ATOM_TYPE_PORTPTR,1,&device_port );
		
#ifdef STRICT_CHECKING
		assert( error ==  VEVO_NO_ERROR );
#endif
		error = vevo_property_set( device_port,
						"device",
						VEVO_ATOM_TYPE_STRING,
						1,
						&device_location );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		
		free( device_location );
		free(device_name);
		i++;
	}
	return i;
}


char **vj_unicap_get_devices(void *unicap, int *n_dev)
{
	int i,j=0;
	unicap_driver_t *ud = (unicap_driver_t*) unicap;
	char **result = NULL;

	if( ud->num_devices <= 0 )
	{
		veejay_msg(0, "I didn't find any capture devices");
		return NULL;
	}

	char **items = vevo_list_properties( ud->device_list );
	if(! items )
	{
		veejay_msg(0, "Empty list of capture devices");
		return NULL;
	}
	
	int len = 1;
	int error = 0;
	for ( i = 0; items[i] != NULL ; i ++ )
	{
		error = vevo_property_get( ud->device_list,items[i], 0, NULL );
		if( error == VEVO_NO_ERROR )
			len ++;
	}
	
	result = vj_calloc( sizeof(char*) * len );
	for( i = 0; items[i] != NULL ; i ++ )
	{
		void *port = NULL;
		error = vevo_property_get( ud->device_list, items[i], 0, &port );
		if( error == VEVO_NO_ERROR )
		{
			size_t name_len = vevo_property_element_size( port, "name", 0 );
			char *name = (char*) vj_calloc( name_len );
			vevo_property_get( port, "name",0,&name );
			name_len = vevo_property_element_size( port, "device", 0 );
			char *loc  = (char*) vj_calloc( name_len );
			vevo_property_get( port, "device", 0, &loc );
			int new_len = strlen(loc) + strlen(name) + 8;
	
			char *text = vj_calloc( new_len );
			snprintf(text, new_len, "%03d%s%03d%s",strlen(name), name,strlen(loc), loc );
			
			free(name);
			free(loc);

			result[j] = strdup(text );
			free(text);
			j++;
		}
		free(items[i]);
	}
	free(items );

	*n_dev = j;
	return result;
}

void	*vj_unicap_init(void)
{
	unicap_driver_t *ud = (unicap_driver_t*) vj_calloc(sizeof(unicap_driver_t));
	ud->device_list = vpn( VEVO_ANONYMOUS_PORT );
	ud->num_devices = vj_unicap_scan_enumerate_devices( (void*) ud );
	veejay_msg(1, "Found %d capture devices on this system", ud->num_devices);
	return ud;
}

void	vj_unicap_deinit(void *dud )
{
	unicap_driver_t *ud = (unicap_driver_t*) dud;
	if( ud )
	{
		//vevo_port_recursive_free( ud->device_list );
		free(ud);
	}
	dud = NULL;
}

int	vj_unicap_property_is_menu( void *ud, char *key )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	unicap_void_property( &property_spec );
	unicap_void_property( &property );
	vj_unicap_t *vut = (vj_unicap_t*) ud;
	unicap_lock_properties( vut->handle );
	
	for( i = 0; SUCCESS( unicap_enumerate_properties( vut->handle,
					&property_spec, &property, i ) ); i ++ )
	{
		unicap_void_property( &property );

		unicap_get_property( vut->handle, &property);
		if( strcmp( property.identifier, key ) == 0 )
		{
			unicap_unlock_properties( vut->handle);
			if( property.type == UNICAP_PROPERTY_TYPE_MENU )
					return 1;
			else
					return 0;
		}
	}
	unicap_unlock_properties( vut->handle);
	return 0;
}
int	vj_unicap_property_is_range( void *ud, char *key )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	unicap_void_property( &property_spec );
	unicap_void_property( &property );
	vj_unicap_t *vut = (vj_unicap_t*) ud;
	unicap_lock_properties( vut->handle );
	
	for( i = 0; SUCCESS( unicap_enumerate_properties( vut->handle,
					&property_spec, &property, i ) ); i ++ )
	{
		unicap_get_property( vut->handle, &property);
		if( strcmp( property.identifier, key ) == 0 )
		{
			unicap_unlock_properties( vut->handle);
			if( property.type == UNICAP_PROPERTY_TYPE_RANGE )
					return 1;
			else
					return 0;
		}
	}
	unicap_unlock_properties( vut->handle);
	return 0;
}

int	vj_unicap_select_value( void *ud, int key, double attr )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	unicap_void_property( &property_spec );
	unicap_void_property( &property );
	vj_unicap_t *vut = (vj_unicap_t*) ud;

	if(! vut->ctrl[key] )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Capture device %s has no property %x",
				vut->device.identifier, key );
		return 0;
	}

	unicap_lock_properties( vut->handle );
	
	for( i = 0; SUCCESS( unicap_enumerate_properties( vut->handle,
					&property_spec, &property, i ) ); i ++ )
	{
		unicap_get_property( vut->handle, &property);
		if( strcmp( property.identifier, vut->ctrl[key] ) == 0 )
		{
			if( property.type == UNICAP_PROPERTY_TYPE_MENU )
			{
				int idx  = vut->option[ key ];
				veejay_strncpy( property.menu_item, property.menu.menu_items[idx], strlen( property.menu.menu_items[idx])  );
				unicap_set_property( vut->handle, &property );
				unicap_unlock_properties( vut->handle );
				return 1;
			}
			if( property.type == UNICAP_PROPERTY_TYPE_RANGE )
			{
				double fval = attr;
				if(fval < property.range.min)
					 fval = property.range.min;
				else if(fval > property.range.max) 
					 fval = property.range.max;
				property.value = fval;
				unicap_set_property( vut->handle, &property );
				unicap_unlock_properties( vut->handle );
				return 1;
			}
		}
	}
	unicap_unlock_properties( vut->handle );

	return 0;
}

int	vj_unicap_get_range( void *ud, char *key, double *min , double *max )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	unicap_void_property( &property_spec );
	unicap_void_property( &property );
	vj_unicap_t *vut = (vj_unicap_t*) ud;
	unicap_lock_properties( vut->handle );

	for( i = 0; SUCCESS( unicap_enumerate_properties( vut->handle,
					&property_spec, &property, i ) ); i ++ )
	{
	//	memset( &property,0,sizeof(unicap_property_t));
		unicap_get_property( vut->handle, &property);

		if( strcasecmp( property.identifier, key ) == 0 )
		{
		
		if( property.type == UNICAP_PROPERTY_TYPE_MENU )
		{
			*min = 0.0;
			*max = (double) property.menu.menu_item_count;
			unicap_unlock_properties( vut->handle );

			return 1;
		}
		if( property.type == UNICAP_PROPERTY_TYPE_RANGE )
		{
			*min = property.range.min;
			*max = property.range.max;
			unicap_unlock_properties( vut->handle );

			return 1;
		}
		}
	}
	unicap_unlock_properties( vut->handle );

	return 0;
}

char	**vj_unicap_get_list( void *ud )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	unicap_void_property( &property_spec );
	vj_unicap_t *vut = (vj_unicap_t*) ud;
	unicap_lock_properties( vut->handle );

	for( i = 0; SUCCESS( unicap_enumerate_properties( vut->handle,
					&property_spec, &property, i ) ); i ++ )
	{
	}

	int n = i;

	char **res = (char**) vj_malloc(sizeof(char*) * (n+1) );
	memset(res, 0,sizeof(char*) * (n+1));
	
	for( i = 0;i < n; i ++ )
	{
		if( SUCCESS( unicap_enumerate_properties(vut->handle,
						&property_spec,&property,i ) ) )
		{
			res[i] = strdup( property.identifier );
		}
	}

	unicap_unlock_properties( vut->handle );

	return res;
}


int	vj_unicap_get_value( void *ud, char *key, int atom_type, void *value )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	unicap_void_property( &property_spec );
	vj_unicap_t *vut = (vj_unicap_t*) ud;
	unicap_lock_properties( vut->handle );

	for( i = 0; SUCCESS( unicap_enumerate_properties( vut->handle,
					&property_spec, &property, i ) ); i ++ )
	{
		unicap_get_property( vut->handle, &property);

		if( strcmp( property.identifier, key ) != 0 )
			continue;
		
		if( property.type == UNICAP_PROPERTY_TYPE_MENU )
		{
#ifdef STRICT_CHECKING
			assert( atom_type == VEVO_ATOM_TYPE_DOUBLE );
#endif
			int n = property.menu.menu_item_count;
			int j;
			for( j =0; j < n; j ++ )
			{
				if( strcmp( property.menu_item, property.menu.menu_items[j] ) == 0 )
				{
					double *dval = value;
					*dval = (double) j;
					unicap_unlock_properties( vut->handle );

					return 1;	
				}
			}
		}
		if( property.type == UNICAP_PROPERTY_TYPE_RANGE )
		{
#ifdef STRICT_CHECKING
			assert( atom_type == VEVO_ATOM_TYPE_DOUBLE );
#endif
			double *dval = value;
			*dval = property.value;
			unicap_unlock_properties( vut->handle );

			return 1;
		}
	}
	unicap_unlock_properties( vut->handle );

	return 0;
}

int	vj_unicap_num_capture_devices( void *dud )
{
	unicap_driver_t *ud = (unicap_driver_t*) dud;
	return ud->num_devices;
}

void	*vj_unicap_new_device( void *dud, int device_id )
{
	unicap_driver_t *ud = (unicap_driver_t*) dud;
	if( ud->num_devices <= 0 )
	{
		veejay_msg(0, "I didn't find any capture devices");
		return NULL;
	}
	if( ud->devices[ device_id ] )
	{
		veejay_msg(0, "Device ID %d already openened in use by Veejay");
		return NULL;
	}
	
	if( device_id < 0 || device_id >= ud->num_devices )
	{
		veejay_msg(0, "I only found %d devices, requested: %d", ud->num_devices,device_id );
		return NULL;
	}
	
	veejay_msg(VEEJAY_MSG_DEBUG, "Trying to open Capture Device %d", device_id);

	vj_unicap_t *vut = (vj_unicap_t*) vj_calloc(sizeof(vj_unicap_t));
	veejay_memset(vut->ctrl, 0 , sizeof(char*) *16 );
	veejay_memset(vut->option,0, sizeof(int) * 16 );
	vut->deviceID = device_id;

	if( !SUCCESS( unicap_enumerate_devices( NULL, &(vut->device), device_id ) ) )
	{
		veejay_msg(0, "Failed to get info for device '%s'\n", vut->device.identifier );
		free(vut);
		return NULL;
	}

	if( !SUCCESS( unicap_open( &(vut->handle), &(vut->device) ) ) )
	{
		veejay_msg(0, "Failed to open capture device '%s'\n", vut->device.identifier );
		free(vut);	
		return NULL;
	}

	ud->devices[ vut->deviceID ] = 1;
	veejay_msg(2, "Using device '%s'", vut->device.identifier);

	pthread_mutex_init( &(vut->mutex), NULL );
	
	return (void*) vut;
}
static unsigned int
get_fourcc(char * fourcc)
{
  return ((((unsigned int)(fourcc[0])<<0)|
           ((unsigned int)(fourcc[1])<<8)|
           ((unsigned int)(fourcc[2])<<16)|
           ((unsigned int)(fourcc[3])<<24)));
}

static inline int	get_shift_size(int fmt)
{
	switch(fmt)
	{
		case FMT_420:
		case FMT_420F:
			return 1;
		case FMT_422:
		case FMT_422F:
			return 1;
		default:
			break;
	}
	return 0;
}

int	vj_unicap_composite_status(void *ud )
{
	vj_unicap_t *vut = (vj_unicap_t*) ud;
	return vut->composite;
}


int	vj_unicap_configure_device( void *ud, int pixel_format, int w, int h, int composite )
{
	vj_unicap_t *vut = (vj_unicap_t*) ud;
	unsigned int fourcc = 0;
	int found_native    = 0;
	int i;
	unicap_lock_properties( vut->handle );

	unicap_void_format( &(vut->format_spec));
	unicap_void_format( &(vut->format) );

	vut->composite  = composite;
	vut->dst_fmt = get_ffmpeg_pixfmt( pixel_format );
	vut->src_fmt = get_ffmpeg_pixfmt( pixel_format );
	vut->format.buffer_type = UNICAP_BUFFER_TYPE_USER;
	vut->template.flags = 1;
	vut->scaler = NULL;

	switch( pixel_format ) {	
		case FMT_420:
		case FMT_420F:
			fourcc = get_fourcc("YU12" );break;
		case FMT_422:
		case FMT_422F:
			fourcc = get_fourcc("422P");break;
	}

/*	if( vut->composite ) {
		switch(vut->composite) {
			case 1:
				vut->dst_fmt = PIX_FMT_YUV444P;	
				break;
			case 2:
				vut->dst_fmt = PIX_FMT_GRAY8;
				break;
		}
	}*/
	
	for( i = 0;  SUCCESS( unicap_enumerate_formats( vut->handle, NULL, &(vut->format), i ) ); i ++ )
	{
		if( fourcc == vut->format.fourcc )
		{
		  veejay_msg(VEEJAY_MSG_INFO, "Found native colorspace '%s'", vut->format.identifier);
	   	  found_native = 1;
		  break;
		}
		if( vut->format.fourcc == get_fourcc("RGB3") || vut->format.fourcc == get_fourcc("BGR3") )
			break;
	}

	int maxw = vut->format.max_size.width;
	int maxh = vut->format.max_size.height;
	int minw = vut->format.min_size.width;
	int minh = vut->format.min_size.height;
	int search = 0;
	int j = 0;

	if( w > maxw || h > maxh ) {
		veejay_msg(VEEJAY_MSG_WARNING, "%s does not support %dx%d, using %dx%d instead with a software scaler.",
			vut->device.identifier, w,h,maxw,maxh );
		search = 1;
	} else {
		vut->src_width = w;
		vut->src_height= h;
		vut->dst_width = w;
		vut->dst_height = h;
		vut->dst_sizes[0] = vut->dst_width * vut->dst_height;	

		if ( vut->format.fourcc == get_fourcc("422P" ) ) {
			vut->src_sizes[0] = vut->src_width * vut->src_height;	
			vut->src_sizes[1] = vut->src_sizes[0]/2;
			vut->src_sizes[2] = vut->src_sizes[1]/2;
			vut->src_fmt = PIX_FMT_YUV422P;
			vut->frame_size = vut->src_sizes[0] + vut->src_sizes[1] + vut->src_sizes[2];
		} else if ( vut->format.fourcc == get_fourcc("420P")) {
			vut->src_sizes[0] = vut->src_width * vut->src_height;	
			vut->src_sizes[1] = vut->src_sizes[0]/4;
			vut->src_sizes[2] = vut->src_sizes[1]/4;
			vut->src_fmt = PIX_FMT_YUV420P;
			vut->frame_size = vut->src_sizes[0] + vut->src_sizes[1] + vut->src_sizes[2];
		}
		vut->format.buffer_size = vut->frame_size;
		vut->format.size.width = vut->src_width;
		vut->format.size.height = vut->src_height;

		if(vut->frame_size<=0 )
			search = 1;
	}

	if( search || !SUCCESS( unicap_set_format( vut->handle, &(vut->format) ) )  )
	{
		int try_format[10] = { maxw,maxh,720,576,640,480,320,240, minw,minh };
		int good = 0;
		for( i = 0; i < 10 ; i +=2 ) {
			vut->src_width = try_format[i];
			vut->src_height= try_format[i+1];
			vut->dst_width = w;
			vut->dst_height = h;
			vut->dst_sizes[0] = vut->dst_width * vut->dst_height;	
		
			if ( vut->format.fourcc == get_fourcc("422P" ) ) {
				vut->src_sizes[0] = vut->src_width * vut->src_height;	
				vut->src_sizes[1] = vut->src_sizes[0]/2;
				vut->src_sizes[2] = vut->src_sizes[1]/2;
				vut->src_fmt = PIX_FMT_YUV422P;
				vut->frame_size = vut->src_sizes[0] + vut->src_sizes[1] + vut->src_sizes[2];
			} else if ( vut->format.fourcc == get_fourcc("420P")) {
				vut->src_sizes[0] = vut->src_width * vut->src_height;	
				vut->src_sizes[1] = vut->src_sizes[0]/4;
				vut->src_sizes[2] = vut->src_sizes[1]/4;
				vut->src_fmt = PIX_FMT_YUV420P;
				vut->frame_size = vut->src_sizes[0] + vut->src_sizes[1] + vut->src_sizes[2];
			} else if( ( vut->format.fourcc == get_fourcc("RGB3") ) 
				  || (vut->format.fourcc == get_fourcc("BGR3") ) ) {
				vut->src_sizes[0] = vut->src_width * 3;
				vut->src_sizes[1] = 0; vut->src_sizes[2] = 0;
				vut->src_fmt = PIX_FMT_RGB24;	
				vut->rgb = 1;
				if(vut->format.fourcc == get_fourcc("BGR3") ) 
					vut->src_fmt = PIX_FMT_BGR24;
				vut->frame_size = vut->src_width * vut->src_height * 3;
			}/* else if ( (vut->format.fourcc == get_fourcc("RGB4") ) ||
					(vut->format.fourcc == get_fourcc("BGR4") ) ) {
				vut->src_sizes[0] = vut->src_width * 4;
				vut->src_sizes[1] = 0; vut->src_sizes[2] = 0;
				vut->src_fmt = PIX_FMT_RGB32;
				vut->rgb = 1;
				if(vut->format.fourcc == get_fourcc("BGR4") )
					vut->src_fmt = PIX_FMT_BGR32;
				vut->frame_size = vut->src_width * vut->src_height * 4;
			} */
#ifdef STRICT_CHECKING
			else {
				veejay_msg(0,  "Invalid pixel format:  %s", vut->format.identifier);
				assert( 0 );
			}
#endif

			vut->format.buffer_size = vut->frame_size;
			vut->format.size.width = vut->src_width;
			vut->format.size.height = vut->src_height;
		
			if( SUCCESS( unicap_set_format( vut->handle, &(vut->format)) ) )
			{
				veejay_msg(VEEJAY_MSG_WARNING, 
					"(OK)    Source %dx%d in %s , Dest %dx%d in %s (%d bytes framebuffer)",
					vut->src_width,vut->src_height,vut->format.identifier,vut->dst_width,
					vut->dst_height, unicap_pf_str(vut->dst_fmt),
					vut->format.buffer_size );

						good = 1;
				break;
			}	
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, 
					"(FAIL)  Source %dx%d in %s , Dest %dx%d in %s (%d bytes framebuffer)",
					vut->src_width,vut->src_height,vut->format.identifier,vut->dst_width,
					vut->dst_height, unicap_pf_str(vut->dst_fmt),
					vut->format.buffer_size );
			}
		}
		
		if(!good) {
			veejay_msg(VEEJAY_MSG_ERROR,"Cannot configure capture device %s", vut->format.identifier );
			unicap_unlock_properties( vut->handle );
			return 0;
		}
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Okay, Capture device delivers %s image, %d x %d  %d bytes buffer",
			vut->format.identifier, vut->format.size.width,vut->format.size.height,
			vut->format.buffer_size );

		vut->src_width = vut->format.size.width;
		vut->src_height= vut->format.size.height;
		vut->dst_width = w;
		vut->dst_height = h;
		vut->dst_sizes[0] = vut->dst_width * vut->dst_height;	
		if ( vut->format.fourcc == get_fourcc("422P" ) ) {
			vut->src_sizes[0] = vut->src_width * vut->src_height;	
			vut->src_sizes[1] = vut->src_sizes[0]/2;
			vut->src_sizes[2] = vut->src_sizes[1]/2;
			vut->src_fmt = PIX_FMT_YUV422P;
			vut->frame_size = vut->src_sizes[0] + vut->src_sizes[1] + vut->src_sizes[2];
		} else if ( vut->format.fourcc == get_fourcc("420P")) {
			vut->src_sizes[0] = vut->src_width * vut->src_height;	
			vut->src_sizes[1] = vut->src_sizes[0]/4;
			vut->src_sizes[2] = vut->src_sizes[1]/4;
			vut->src_fmt = PIX_FMT_YUV420P;
			vut->frame_size = vut->src_sizes[0] + vut->src_sizes[1] + vut->src_sizes[2];
		} else if( ( vut->format.fourcc == get_fourcc("RGB3") ) 
			  || (vut->format.fourcc == get_fourcc("BGR3") ) ) {
			vut->src_sizes[0] = vut->src_width * 3;
			vut->src_sizes[1] = 0; vut->src_sizes[2] = 0;
			vut->src_fmt = PIX_FMT_RGB24;	
			vut->rgb = 1;
			if(vut->format.fourcc == get_fourcc("BGR3") ) 
				vut->src_fmt = PIX_FMT_BGR24;
			vut->frame_size = vut->src_width * vut->src_height * 3;
		}
#ifdef STRICT_CHECKING
		else {
			veejay_msg(0,  "Invalid pixel format:  %s", vut->format.identifier);
			assert( 0 );
		}
#endif

		vut->format.buffer_size = vut->frame_size;
		vut->format.size.width = vut->src_width;
		vut->format.size.height = vut->src_height;
		
		if( SUCCESS( unicap_set_format( vut->handle, &(vut->format)) ) )
		{
			veejay_msg(VEEJAY_MSG_WARNING, 
				"(OK)    Source %dx%d in %s , Dest %dx%d in %s (%d bytes framebuffer)",
				vut->src_width,vut->src_height,vut->format.identifier,vut->dst_width,
				vut->dst_height, unicap_pf_str(vut->dst_fmt),
				vut->format.buffer_size );
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, 
				"(FAIL)  Source %dx%d in %s , Dest %dx%d in %s (%d bytes framebuffer)",
				vut->src_width,vut->src_height,vut->format.identifier,vut->dst_width,
				vut->dst_height, unicap_pf_str(vut->dst_fmt),
				vut->format.buffer_size );
			return 0;
		}



	}

/*	if(vut->composite) {
		vut->dst_sizes[1] = vut->dst_sizes[0];
		vut->dst_sizes[2] = vut->dst_sizes[0];
	} else */
	if(pixel_format == FMT_420F || pixel_format == FMT_420 ) {
		vut->dst_sizes[1] = vut->dst_sizes[0]/4;
		vut->dst_sizes[2] = vut->dst_sizes[1];
	} else {
		vut->dst_sizes[1] = vut->dst_sizes[0]/2;
		vut->dst_sizes[2] = vut->dst_sizes[1];
	} 


// buffer alloced	
	char **properties = vj_unicap_get_list( vut );
	if(!properties)
	{
		veejay_msg(0, "No properties for this capture device ?!");
		unicap_unlock_properties( vut->handle  );
		return 1;
	}

	for( i = 0; properties[i] != NULL && i < 16; i ++ )
	{
		if(strncasecmp( properties[i], "brightness",10 ) == 0  ) {
			vut->ctrl[UNICAP_BRIGHTNESS] = strdup( properties[i] );
		} else if (strncasecmp( properties[i], "color", 5  ) == 0 ) {
			vut->ctrl[UNICAP_COLOR] = strdup( properties[i]);
		} else if (strncasecmp( properties[i], "saturation", 10  )  == 0 ) {
			vut->ctrl[UNICAP_SATURATION] = strdup( properties[i] );
		} else if (strncasecmp( properties[i], "hue", 3 ) ==  0 )  {
			vut->ctrl[UNICAP_HUE] = strdup( properties[i] );
		} else if(strncasecmp( properties[i], "white", 5) == 0 ) {
		  	vut->ctrl[UNICAP_WHITE] = strdup(properties[i]);     
		} else if (strncasecmp( properties[i], "contrast", 8 ) == 0 ) {
			vut->ctrl[UNICAP_CONTRAST] = strdup( properties[i] );
		} else if (strncasecmp( properties[i], "video source",12) == 0 ) {
			unicap_property_t p;
			unicap_void_property(  &p  );
			strcpy( p.identifier, properties[i] );//, strlen( properties[i]));
			unicap_get_property( vut->handle, &p );
			for(j=0;j<p.menu.menu_item_count;j++)
			{
				vut->option[UNICAP_SOURCE0+j] = j;
				vut->ctrl[UNICAP_SOURCE0+j] = strdup(properties[i]);
			}		
		}else if (strncasecmp( properties[i], "video norm",10) == 0 ) {
			unicap_property_t p;
			unicap_void_property(  &p  );
			strcpy( p.identifier, properties[i] ); //, strlen(i));
			unicap_get_property( vut->handle, &p );
			for(j=0;j<p.menu.menu_item_count;j++)
			{
				if(  strcasecmp( p.menu.menu_items[j], "pal"  ) == 0 )
				{
					vut->ctrl[UNICAP_PAL] = strdup(properties[i]);
					vut->option[UNICAP_PAL] = j;
				}
				else if( strncasecmp( p.menu.menu_items[j], "ntsc", 4 ) == 0 )
				{
					vut->ctrl[UNICAP_NTSC] = strdup(properties[i]);
					vut->option[UNICAP_NTSC] = j;
				}
			}		
		}

		free( properties[i]);	
	}
	free(properties);


	unicap_unlock_properties( vut->handle );

	return 1;
}

int		vj_unicap_start_capture( void *vut )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;
#ifdef STRICT_CHECKING
	assert( v->priv_buf == NULL );
	assert( v->active == 0 );
#endif

	veejay_msg(VEEJAY_MSG_INFO,"Preparing to capture from device %s", v->device.identifier );
#ifndef USE_UNICAP_CB
	pthread_attr_init(&(v->attr) );
	pthread_attr_setdetachstate(&(v->attr), PTHREAD_CREATE_DETACHED );
	
	int err = pthread_create( &(v->thread), NULL,
			unicap_reader_thread, vut );

	pthread_attr_destroy( &(v->attr) );

	if( err == 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Spawned new capture thread for device %s", v->device.identifier );
		return 1;
	}
	v->state = 0;
	veejay_msg(VEEJAY_MSG_ERROR,
			"Unable to start capture thread for device %s: %s", v->device.identifier, strerror(err));
	return 0;
#else
	if(!vj_unicap_start_capture_(vut))
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to start capture thread for device %s",
			v->device.identifier );
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Capturing from device %s", v->device.identifier );
		return 1;
	}

	return 0;
#endif
}

int		vj_unicap_stop_capture( void *vut )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;

	veejay_msg(VEEJAY_MSG_INFO, "Stopping capture from device %s", v->device.identifier );
#ifndef USE_UNICAP_CB
	lock_ ( vut );
	v->state = 0;
	unlock_( vut );

	usleep( 200 * 1000 ); //@ bad way to sync threads

	lock_( vut );
	if( v->active )
		veejay_msg(VEEJAY_MSG_INFO, "Capture thread still running. Cancel %s", v->device.identifier);
	unlock_(vut);
	pthread_cancel( v->thread );

	if(v->active)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Cleaning up capture thread from device %s",v->device.identifier);
		vj_unicap_stop_capture_( vut );
	}
#else
	//@todo
#endif
	return 1;
}




static int	vj_unicap_start_capture_( void *vut )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;
#ifdef STRICT_CHECKING
	assert( v->priv_buf == NULL );
	assert( v->buffer.data == NULL );
	assert( v->active == 0 );
	assert( v->state == 0 );
#endif
	int i;

	if( v->src_width == v->dst_width && v->src_height == v->dst_height ) {
		v->priv_buf = (uint8_t*) vj_calloc( v->dst_width * v->dst_height * 4 * sizeof(uint8_t) );
		v->buffer.data = vj_malloc( v->frame_size * sizeof(uint8_t) );
		if(!v->rgb){
			veejay_memset( v->buffer.data,    0, v->src_sizes[1] );
			veejay_memset( v->buffer.data + v->src_sizes[0], 128, v->src_sizes[1]  + v->src_sizes[2]);
		} else  {
			veejay_memset( v->buffer.data, 0, v->src_sizes[0]);
		}
		for( i = 0; i < BUFFERS; i ++ )
		{
			veejay_memset( &(v->mbuf[i]),0x0, sizeof(unicap_data_buffer_t));	
			v->mbuf[i].data = vj_malloc(v->frame_size * sizeof(uint8_t));
		}
		
	} else {
#ifdef STRICT_CHECKING
		assert( v->dst_width > 0 );
		assert( v->dst_height > 0 );
		assert( v->frame_size > 0 );
#endif
		v->priv_buf = (uint8_t*) vj_calloc( v->dst_width * v->dst_height * 4 * sizeof(uint8_t) );
		v->buffer.data = vj_calloc( v->format.buffer_size * sizeof(uint8_t) * 2  );
		for( i = 0; i < BUFFERS; i ++ )
		{
			veejay_memset( &(v->mbuf[i]),0x0, sizeof(unicap_data_buffer_t));	
			v->mbuf[i].data = vj_calloc(v->format.buffer_size * sizeof(uint8_t));
		}


	}


	v->buffer.buffer_size = v->format.buffer_size;

#ifdef USE_UNICAP_CB
	unicap_register_callback( v->handle, UNICAP_EVENT_NEW_FRAME,
			(unicap_callback_t) vj_unicap_new_frame_cb, &v );
#endif
	if( !SUCCESS( unicap_start_capture( v->handle ) ) )
   	{
		free(v->priv_buf);
		v->priv_buf = NULL;
		unicap_report_error(v, "Failed to start capture on device" );
		return 0;
     	}
#ifndef USE_UNICAP_CB
/*	if( ! SUCCESS( unicap_queue_buffer( v->handle, &(v->buffer) )) )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "%s:%d Failed to queue buffer ",__FUNCTION__,__LINE__);
		free(v->priv_buf);
		v->priv_buf = NULL;
		return 0;
	}
*/
	for( i = 0; i < BUFFERS; i ++ )  {

		if( ! SUCCESS( unicap_queue_buffer( v->handle, &(v->mbuf[i]) )) )
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "%s:%d Failed to queue buffer ",__FUNCTION__,__LINE__);
			free(v->priv_buf);
			v->priv_buf = NULL;
			return 0;
		}
	}
#endif
	v->active = 1;
#ifdef USE_UNICAP_CB
	v->state = 1;
	veejay_msg(VEEJAY_MSG_INFO,"Started capture from device %s", v->device.identifier );
#else	
	veejay_msg(VEEJAY_MSG_INFO, "Thread: started capture from device %s",	v->device.identifier );
#endif
	return 1;
}

int	vj_unicap_get_pause( void *vut ) {
	vj_unicap_t *v = (vj_unicap_t*) vut;
	return v->pause;
}

void	vj_unicap_set_pause( void *vut , int status ) {
	vj_unicap_t *v = (vj_unicap_t*) vut;
	lock_(vut);
	v->pause = status;
	unlock_(vut);
}

int	vj_unicap_grab_frame( void *vut, uint8_t *buffer[3], const int width, const int height )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;
//	lock_( vut );

	if(v->active == 0 || v->state == 0 )
	{
//		unlock_(vut);
		return 0;
	}
	
	if( v->pause )
	{
//		unlock_(vut);
		return 1;
	}	

#ifdef STRICT_CHECKING
	assert(v->priv_buf != NULL );
#endif	
//veejay_msg(0, "Copy captured frame:%dx%d, sizes=%d,%d,%d,dst=%d,%d,%d,rgb=%d",
//	v->src_width,v->src_height,v->sizes[0],v->sizes[1],v->sizes[2],
//		v->dst_sizes[0],v->dst_sizes[1],v->dst_sizes[2],v->rgb );
	lock_(vut);
	if( v->src_width == v->dst_width && v->src_height == v->dst_height )
	{
		uint8_t *src[3];
		int y = v->sizes[0];
		int u = v->sizes[1];
		int uu = v->sizes[2];

		if( !v->rgb ) {
			src[0] = v->priv_buf;
			src[1] = v->priv_buf + v->sizes[0];
			src[2] = v->priv_buf + v->sizes[0] + v->sizes[1]; 
		} else {
			src[0] = v->priv_buf;
			src[1] = v->priv_buf + v->dst_sizes[0];
			src[2] = v->priv_buf + v->dst_sizes[0]  + v->dst_sizes[1];
			u = v->dst_sizes[1];
			uu = v->dst_sizes[2];
			y = v->dst_sizes[0];
		}
		veejay_memcpy( buffer[0], src[0], y );
		veejay_memcpy( buffer[1], src[1], u );
		veejay_memcpy( buffer[2], src[2], uu );
	} else {
		uint8_t *src[3] = {	
			v->priv_buf,	
			v->priv_buf + v->dst_sizes[0],
			v->priv_buf + v->dst_sizes[0]  + v->dst_sizes[1] };
		veejay_memcpy( buffer[0], src[0], v->dst_sizes[0] );
		veejay_memcpy( buffer[1], src[1], v->dst_sizes[1] );
		veejay_memcpy( buffer[2], src[2], v->dst_sizes[2] );
	}

	unlock_(vut);
	return 1;
}
#ifndef USE_UNICAP_CB

static void print_fps( )
{
   static struct timeval next,current;
   static int count = 0;
   suseconds_t usec;

   if( count == 0 )
   {
      gettimeofday(&next, NULL);
      next.tv_sec += 1;
   }

   count++;
	
   gettimeofday(&current, NULL);

   if( timercmp( &next, &current, <= ) )
   {
      printf( "FPS: %d\n", count );
      count = 0;
   }
}	

int	vj_unicap_grab_a_frame( void *vut )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;
	unicap_data_buffer_t *ready_buffer = NULL;
	uint8_t *buffer[3];
	int i;
	if( v->src_width == v->dst_width && v->src_height == v->dst_height ) 
	{
		buffer[0] = v->priv_buf;
		buffer[1] = v->priv_buf + v->sizes[0];
		buffer[2] = v->priv_buf + v->sizes[0] + v->sizes[1];
	} else {
		buffer[0] = v->priv_buf;
		buffer[1] =v->priv_buf + v->dst_sizes[0];
	 	buffer[2] =v->priv_buf + v->dst_sizes[0]  + v->dst_sizes[1];
	}

	if(!v->active)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Capture not started on device %d", v->deviceID);
 		return 0;
	}

	int buffers_ready = 0;

	if( v->state == 0 || v->active == 0 )
	{
		return 0;
	}


	if(!SUCCESS(unicap_poll_buffer( v->handle, &buffers_ready ) ) ) {
		buffers_ready = 1;
	}

	if( buffers_ready == 0 )
		return 0;

	for( i = 0; i < buffers_ready-1; i ++ ) {
		if(! SUCCESS(unicap_wait_buffer(v->handle, &ready_buffer ) ))
		{
			veejay_msg(VEEJAY_MSG_WARNING, "Failed to wait for buffer from device %s",
					v->device.identifier );
			return 1;
		}
		unicap_queue_buffer( v->handle, ready_buffer );
	}

	if( (!SUCCESS(unicap_wait_buffer(v->handle,&ready_buffer ) ) ) ) {
		veejay_msg(0, "Failed to wait for buffer on device");
		return 1;
	}

	if( ready_buffer->buffer_size <= 0 )
	{
		veejay_msg(0, "Unicap returned a buffer of size 0!");	
		return 0;
	}

	if(!ready_buffer->data )
	{
		veejay_msg(0, "Unicap returned a NULL buffer!");	
		return 0;
	}
	//print_fps();
	lock_(vut);
	if(!v->rgb)
	{
		if( v->src_width == v->dst_width && v->src_height == v->dst_height )
		{
			veejay_memcpy( buffer[0], ready_buffer->data, v->sizes[0] );
			veejay_memcpy( buffer[1], ready_buffer->data + v->sizes[0], v->sizes[1] );
			veejay_memcpy( buffer[2], ready_buffer->data + v->sizes[0] +v->sizes[1] , v->sizes[2]);
		}
		else {
			VJFrame *srci = yuv_yuv_template( ready_buffer->data,
						          ready_buffer->data + v->src_sizes[0],
							  ready_buffer->data + v->src_sizes[0] + v->src_sizes[1],
							  v->src_width,
							  v->src_height, v->src_fmt );
			VJFrame *dsti = yuv_yuv_template( buffer[0],buffer[1],buffer[2], 
							  v->dst_width,v->dst_height, v->dst_fmt ); 
			if(!v->scaler) {
				v->scaler = yuv_init_swscaler( srci,dsti,&(v->template),yuv_sws_get_cpu_flags());
			}
	
			yuv_convert_and_scale( v->scaler, srci,dsti );
			free(srci);
			free(dsti);
		}
	}
	else
	{
		if( v->src_width == v->dst_width && v->src_height == v->dst_height )
		{
			VJFrame *srci = yuv_rgb_template( ready_buffer->data, v->src_width,v->src_height, v->src_fmt );
			VJFrame *dsti = yuv_yuv_template( v->priv_buf,
							  v->priv_buf + v->dst_sizes[0],
							  v->priv_buf + v->dst_sizes[0] + v->dst_sizes[1],
							  v->dst_width,
                                                          v->dst_height,
							  v->dst_fmt ); 
			if(!v->scaler) {
				v->scaler = yuv_init_swscaler( srci,dsti,&(v->template),yuv_sws_get_cpu_flags());
			}
			yuv_convert_and_scale_from_rgb( v->scaler, srci,dsti );


			free(srci);
			free(dsti);
		}
		else {
			VJFrame *srci = yuv_rgb_template( ready_buffer->data, v->src_width,v->src_height, v->src_fmt );
			VJFrame *dsti = yuv_yuv_template( v->priv_buf,
							  v->priv_buf + v->dst_sizes[0],
							  v->priv_buf + v->dst_sizes[0] + v->dst_sizes[1],
							  v->dst_width,v->dst_height, v->dst_fmt ); 
			if(!v->scaler) {
				v->scaler = yuv_init_swscaler( srci,dsti,&(v->template),yuv_sws_get_cpu_flags());
			}
			yuv_convert_and_scale_from_rgb( v->scaler, srci,dsti );
			free(srci);
			free(dsti);
		}
	}
	unlock_(vut);

	if( ! SUCCESS( unicap_queue_buffer( v->handle, ready_buffer) )) 
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "%s:%d Failed to queue buffer ",__FUNCTION__,__LINE__);
		return 0;
	}
	if(v->dst_fmt == PIX_FMT_GRAY8) {	
		veejay_memset( buffer[1], 128, v->dst_sizes[0]);
		veejay_memset( buffer[2], 128, v->dst_sizes[0]);
	}

	return 1;
}
#else
static	void	vj_unicap_new_frame_cb( unicap_event_t event, unicap_handle_t handle, 
			unicap_data_buffer_t *ready_buffer, vj_unicap_t *v )
{
veejay_msg(0, "%s: wait",__FUNCTION__);
	lock_(v);

	uint8_t *buffer[3] = 
	{
		v->priv_buf,
		v->priv_buf + v->sizes[0],
		v->priv_buf + v->sizes[0] + v->sizes[1] 
	};

	if( v->deinterlace )
	{
	}
	else
	{
		if(!v->rgb)
		{
			veejay_memcpy( buffer[0], ready_buffer->data, v->sizes[0] );
			veejay_memcpy( buffer[1], ready_buffer->data + v->sizes[0], v->sizes[1] );
			veejay_memcpy( buffer[2], ready_buffer->data + v->sizes[0] +v->sizes[1] , v->sizes[2]);
		}
		else
		{
			VJFrame *srci = yuv_rgb_template( ready_buffer->data, v->src_width,v->src_height, vut->src_fmt );
			VJFrame *dsti = yuv_yuv_template( buffer[0],buffer[1],buffer[2], v->dst_width,v->dst_height, vut->dst_fmt ); 

			yuv_convert_any_ac(srci,dsti, srci->format, dsti->format );

			free(srci);
			free(dsti);
		}
	}
	unlock_(v);
}
#endif


static int	vj_unicap_stop_capture_( void *vut )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;
 

	if( !SUCCESS( unicap_stop_capture( v->handle ) ) )
   	{
   	 	veejay_msg(0,"Failed to stop capture on device: %s\n", v->device.identifier );
	}

	if(v->priv_buf)
		free(v->priv_buf);
	if(v->buffer.data)
		free(v->buffer.data);
	v->priv_buf = NULL;
	v->buffer.data = NULL;
	v->active = 0;
	v->state = 0;
	veejay_msg(VEEJAY_MSG_INFO, "Stopped capture from device %s",
			v->device.identifier );
	return 1;
}

int		vj_unicap_status(void *vut)
{
	if(!vut) return 0;
	vj_unicap_t *v = (vj_unicap_t*) vut;
	return v->active;
}

#ifndef USE_UNICAP_CB
static  void    unicap_delay(long nsec )
{
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = nsec * 1000000;
        nanosleep( &ts, NULL );
}


static void	*unicap_reader_thread(void *data)
{
	vj_unicap_t *v = (vj_unicap_t*) data;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);


	if(! vj_unicap_start_capture_( data ) )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Thread:Unable to start capture from device %s", v->device.identifier);
		if(v->priv_buf)
			free(v->priv_buf);
		if(v->buffer.data)
			free(v->buffer.data);
		if(v->scaler)
			yuv_free_swscaler(v->scaler);
		v->priv_buf = NULL;
		v->buffer.data = NULL;
		v->active = 0;
		v->state = 0;
		v->scaler = NULL;
		pthread_exit(NULL);
		return NULL;
	}
	
	v->state = 1; // thread run
	//long s = 85000;
	while( v->state )
	{
		if( v->active )
		{	
			if(vj_unicap_grab_a_frame( data )==0)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Unable to grab a frame from capture device %d", v->deviceID);
				v->state = 0;
			}
		//	unicap_delay( s );
		}
	}

	veejay_msg(VEEJAY_MSG_INFO, "Thread was told to stop capturing from device %d", v->deviceID);
	vj_unicap_stop_capture_(data);

	pthread_exit(NULL);
	
	return NULL;
}
#endif
void	vj_unicap_free_device( void *dud, void *vut )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;
	unicap_driver_t *d = (unicap_driver_t*) dud; 

	if( v->active )
		vj_unicap_stop_capture( vut );
	
	d->devices[ v->deviceID ] = 0;

	if( v->handle )	
	{
		if( !SUCCESS( unicap_close( v->handle ) ) )
   		{
   		 	veejay_msg(0, "Failed to close the device: %s\n", v->device.identifier );
		}
	}

	int i = 0;
	for( i = 0 ; i < 16 ; i ++ )
	{
		if(v->ctrl[i])
			free(v->ctrl[i]);
	}

	free( v );
	v = NULL;
}
#endif
