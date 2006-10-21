/* veejay - Linux VeeJay Unicap interface
 * 	     (C) 2002-2006 Niels Elburg <nelburg@looze.net> 
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
#ifdef USE_UNICAP
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // for memset
#include <unicap.h>
#include <unicap_status.h>
#include <veejay/vj-global.h>
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <libyuv/yuvconv.h>
#include <libvevo/libvevo.h>
#include <libstream/vj-unicap.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

typedef struct
{
	unicap_handle_t handle;
	unicap_device_t device;
	unicap_format_t format_spec;
	unicap_format_t format;
	unicap_data_buffer_t buffer;
	unicap_data_buffer_t *returned_buffer;
	int	 deviceID;
	int	 sizes[3];
	int	active;
	int	deinterlace;
	int	rgb;
	int	pixfmt;
	int	shift;
	void	*sampler;
	char 	*ctrl[16];
	int	 option[16];

} vj_unicap_t;


typedef struct
{
	unicap_handle_t handle;
	unicap_device_t device;
	unicap_format_t format_spec;
	unicap_format_t format;
	unicap_data_buffer_t buffer;
	unicap_data_buffer_t *returned_buffer;
	void	*device_list;
	int	 num_devices;
} unicap_driver_t;

static int	vj_unicap_scan_enumerate_devices(void *unicap)
{
	int i;
	unicap_driver_t *ud = (unicap_driver_t*) unicap;
	char key[64];

	for( i = 0; SUCCESS( unicap_enumerate_devices( NULL, &(ud->device), i ) ); i++ )
	{
		unicap_property_t property;
		unicap_format_t format;
		int property_count = 0;
		int format_count = 0;
		int j;

		if( !SUCCESS( unicap_open( &(ud->handle), &(ud->device) ) ) )
		{
			veejay_msg(0, "Failed to open: %s\n", &(ud->device.identifier) );
			continue;
		}
		unicap_lock_properties( ud->handle );

	
		
		unicap_reenumerate_properties( ud->handle, &property_count );
		unicap_reenumerate_formats( ud->handle, &format_count );
		char *device_name = strdup( ud->device.identifier );

		void	*device_port = vpn( VEVO_ANONYMOUS_PORT );
		char *device_location = strdup( ud->device.device );
		
		int error = vevo_property_set( device_port,
					       "name",
					       VEVO_ATOM_TYPE_STRING,
					       1,
					       &device_name);

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

		
		veejay_msg(2, "\t'%s' at device %s",device_name, device_location ); 
	
		free( device_location );
		free( device_name );
		
#ifdef STRICT_CHECKING
		assert( error ==  VEVO_NO_ERROR );
#endif
		unicap_unlock_properties( ud->handle );

		unicap_close( ud->handle );
	}
	return i;
}


char **vj_unicap_get_devices(void *unicap)
{
	int i;
	unicap_driver_t *ud = (unicap_driver_t*) unicap;
	char **result = NULL;
	for( i = 0; SUCCESS( unicap_enumerate_devices( NULL, &(ud->device), i ) ); i++ )
	{
	}
	if( i <= 0 )
			return NULL;

	result = (char**) malloc(sizeof(char*) * (i+1));
	result[i] = NULL;
	
	for( i = 0; SUCCESS( unicap_enumerate_devices( NULL, &(ud->device), i ) ); i++ )
	{
		char tmp[1024];
		unicap_property_t property;
		unicap_format_t format;
		int property_count = 0;
		int format_count = 0;
		int j;

		if( !SUCCESS( unicap_open( &(ud->handle), &(ud->device) ) ) )
		{
			veejay_msg(0, "Failed to open: %s\n", &(ud->device.identifier) );
			continue;
		}
		unicap_lock_properties( ud->handle );

	
		
		unicap_reenumerate_properties( ud->handle, &property_count );
		unicap_reenumerate_formats( ud->handle, &format_count );
		char *device_name = strdup( ud->device.identifier );
		char *device_location = strdup( ud->device.device );
		

	    sprintf(tmp, "%03d%s%03d%s", strlen( device_name ), device_name,strlen( device_location ),
						device_location );
		result[i] = strndup( tmp, 1024 );	
		
		free( device_location );
		free( device_name );
		
		unicap_unlock_properties( ud->handle );

		unicap_close( ud->handle );
	}
	return result;
}



void	*vj_unicap_init(void)
{
	unicap_driver_t *ud = (unicap_driver_t*) vj_malloc(sizeof(unicap_driver_t));
	memset( ud,0,sizeof(unicap_driver_t));
	ud->device_list = vpn( VEVO_ANONYMOUS_PORT );
	ud->num_devices = vj_unicap_scan_enumerate_devices( (void*) ud );
	veejay_msg(2, "Found %d capture devices on this system", ud->num_devices);
	return ud;
}

void	vj_unicap_deinit(void *dud )
{
	unicap_driver_t *ud = (unicap_driver_t*) dud;
	vevo_port_recursive_free( ud->device_list );
	free(ud);
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

	if(! vut->ctrl[i] )
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
				int n = property.menu.menu_item_count;
				int idx  = vut->option[ key ];
				veejay_msg(0, "To menu item %d, cur = '%s', new = '%s'",
					idx,
					property.menu_item,
				       	property.menu.menu_items[idx] ); 
				strcpy( property.menu_item, property.menu.menu_items[idx]  );
				unicap_set_property( vut->handle, &property );
				veejay_msg(0,"changed menu item %d to %s", idx, property.menu_item );
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
				veejay_msg(0, "Changed range value to %f", property.value );
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
			veejay_msg(VEEJAY_MSG_DEBUG, " '%s'", res[i]);
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
	
	if( device_id < 0 || device_id >= ud->num_devices )
	{
		veejay_msg(0, "I only found %d devices, requested: %d", ud->num_devices,device_id );
		return NULL;
	}
	
	vj_unicap_t *vut = (vj_unicap_t*) vj_malloc(sizeof(vj_unicap_t));
	memset(vut,0,sizeof(vj_unicap_t));
	memset(vut->ctrl, 0 , sizeof(char*) *16 );
	memset(vut->option,0, sizeof(int) * 16 );
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
	}
	veejay_msg(2, "Using device '%s'", vut->device.identifier);
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
			return 0;
		default:
			break;
	}
	return 0;
}
int	vj_unicap_configure_device( void *ud, int pixel_format, int w, int h )
{
	vj_unicap_t *vut = (vj_unicap_t*) ud;
	unicap_lock_properties( vut->handle );

	unicap_void_format( &(vut->format_spec));

	unsigned int fourcc = 0;
	vut->sizes[0] = w * h;
	
	switch(pixel_format)
	{
		case FMT_420:
		case FMT_420F:
			fourcc = get_fourcc( "YU12" );
			vut->sizes[1] = (w*h)/4;
			vut->sizes[2] = vut->sizes[1];
			break;
		case FMT_422:
		case FMT_422F:
			fourcc = get_fourcc( "422P" );
			vut->sizes[1] = (w*h)/2;
			vut->sizes[2] = vut->sizes[1];
			break;
#ifdef STRICT_CHECKING
		default:
			veejay_msg(0,
					"Unknown pixel format used to configure device: %d", pixel_format);
			assert(0);
			break;
#endif
	}	
	
	vut->pixfmt = get_ffmpeg_pixfmt( pixel_format );
	vut->shift    = get_shift_size(pixel_format);
	int i;
	int j;
	int found_native = 0;
	for( i = 0;  SUCCESS( unicap_enumerate_formats( vut->handle, &(vut->format_spec), &(vut->format), i ) ); i ++ )
	{
		if( fourcc == vut->format.fourcc )
		{
	   	  found_native = 1;
		  break;
		}
	}

	if( found_native )
	{
		vut->format.size.width = w;
		vut->format.size.height = h;

		veejay_msg(2, "Capture device supports '%s'", vut->format.identifier );
		if (!SUCCESS(unicap_set_format( vut->handle, &(vut->format) )))
		{
			veejay_msg(0, "Unable to set video size %d x %d in format %s",
					w,h,vut->format.identifier );
			unicap_unlock_properties( vut->handle );

			return 0;
		}
		vut->deinterlace = 1;
	}
	else
	{
		unsigned int rgb_fourcc = get_fourcc( "RGB4" );
		unicap_format_t rgb_spec, rgb_format;
		unicap_void_format( &rgb_spec);
		veejay_msg(1, "Unable to select native pixel format, trying RGB32");
		
		for( i = 0;
	         	SUCCESS( unicap_enumerate_formats( vut->handle, &rgb_spec, &rgb_format, i ) ); i ++ )
		{
			if( rgb_fourcc == rgb_format.fourcc )
			{
				veejay_msg(0, "Camera can capture in RGB32");
				vut->rgb = 1;
				rgb_format.size.width = w;
				rgb_format.size.height = h;
				break;
			}
		}
		
		if(!vut->rgb)
		{
			veejay_msg(0, "No matching formats found. Camera not supported.");
			unicap_unlock_properties( vut->handle );

			return 0;
		}
		else
			if( !SUCCESS( unicap_set_format( vut->handle, &rgb_format ) ) )
			{
				veejay_msg(0, "Cannot set size %d x %d or format %s", w,h,rgb_format.identifier);
				unicap_unlock_properties( vut->handle );

				return 0;
			}
	}

	unicap_format_t test;
	memset(&test, 0,sizeof(unicap_format_t));
	if(! SUCCESS( unicap_get_format( vut->handle, &test ) ) )
	{
		veejay_msg(0, "Failed to get video format");
	}

	veejay_msg(0, "Capture video from '%s' in %d x %d pixels, %d bpp using %s",
		 vut->device.identifier,
		 test.size.width,
		 test.size.height,
		 test.bpp,
		 test.identifier );

	if ( test.size.width != w || test.size.height != h ) 
	{
		veejay_msg(0, "Video size mismatch, retrying...");
		vut->format.size.width = w;
		vut->format.size.height = h;
		if( !SUCCESS( unicap_set_format( vut->handle, &(vut->format) ) ) )
		{
			veejay_msg(0, "Cannot set size %d x %d or format %s", w,h,vut->format.identifier);
			unicap_unlock_properties( vut->handle );

			return 0;
		}
		veejay_msg(2, "Capture size set to %d x %d (%s)", w,h,vut->format.identifier);
		
	}
	
/*
	char *comp = "Composite1";
	if(vj_unicap_set_property( vut, "video source", VEVO_ATOM_TYPE_STRING, &comp ) )
	{
		veejay_msg(2, "Changed channel to Composite1");
	}*/
	veejay_msg(2,"Using %d bytes for device buffer",
			test.size.width * test.size.height * 4 );
			
	vut->buffer.data = vj_malloc( test.size.width * test.size.height * 4 );
	vut->buffer.buffer_size = (sizeof(unsigned char) * 4 * test.size.width * test.size.height );


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
			strcpy( p.identifier, properties[i]);
			unicap_get_property( vut->handle, &p );
			for(j=0;j<p.menu.menu_item_count;j++)
			{
				vut->option[UNICAP_SOURCE0+j] = j;
				vut->ctrl[UNICAP_SOURCE0+j] = strdup(properties[i]);
			}		
		}else if (strncasecmp( properties[i], "video norm",10) == 0 ) {
			unicap_property_t p;
			unicap_void_property(  &p  );
			strcpy( p.identifier, properties[i]);
			unicap_get_property( vut->handle, &p );
			for(j=0;j<p.menu.menu_item_count;j++)
			{
				veejay_msg(VEEJAY_MSG_DEBUG, "Norm = '%s' -> %d",
						p.menu.menu_items[j], j );
				if(  strncasecmp( p.menu.menu_items[j], "pal", 3  ) == 0 )
				{
					vut->ctrl[UNICAP_PAL] = strdup(properties[i]);
					vut->option[UNICAP_PAL] = j;
				}
				else if( strncasecmp( p.menu.menu_items[j], "ntsc", 4 ) == 0 )
				{
					vut->ctrl[UNICAP_NTSC] = strdup(properties);
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

int	vj_unicap_start_capture( void *vut )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;

	if( !SUCCESS( unicap_start_capture( v->handle ) ) )
   	{
      		veejay_msg( 0, "Failed to start capture on device: %s\n", v->device.identifier );
		return 0;
     	}
	if( !SUCCESS( unicap_queue_buffer( v->handle, &(v->buffer)))) 
	{
		veejay_msg(0, "Failed to queue buffer on device");
		return 0;
	}
	v->active = 1;
	veejay_msg(VEEJAY_MSG_DEBUG, "Started capture on device %s",
			v->device.identifier );
	return 1;
}

int	vj_unicap_grab_frame( void *vut, uint8_t *buffer[3], const int width, const int height )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;
	unicap_lock_properties( v->handle );

	if(!v->active)
		veejay_msg(VEEJAY_MSG_ERROR, "Capture not started!");
  	
	if( !SUCCESS( unicap_wait_buffer( v->handle, &(v->returned_buffer )) )) 
	{
		veejay_msg(0,"Failed to wait for buffer on device: %s\n", v->device.identifier );
		unicap_unlock_properties( v->handle );
		return 0;
	}
	
	if( !SUCCESS( unicap_queue_buffer( v->handle, &(v->buffer) ) ) )
   	{
		veejay_msg( 0, "Failed to queue a buffer on device: %s\n", v->device.identifier );
		unicap_unlock_properties( v->handle );
		return 0;
	}

	if( v->deinterlace )
	{
		yuv_deinterlace(
			buffer,
			width,
			height,
			v->pixfmt,
			v->shift,
			v->buffer.data,
			v->buffer.data + v->sizes[0],
			v->buffer.data +v->sizes[0] + v->sizes[1]
			);
	}
	else
	{
		if(!v->rgb)
		{
			veejay_memcpy( buffer[0], v->buffer.data, v->sizes[0] );
			veejay_memcpy( buffer[1], v->buffer.data + v->sizes[0], v->sizes[1] );
			veejay_memcpy( buffer[2], v->buffer.data + v->sizes[0] +v->sizes[1] , v->sizes[2]);
		}
		else
		{
			util_convertsrc( v->buffer.data, width,height,v->pixfmt,v->shift, buffer );
		}
	}	
	unicap_unlock_properties( v->handle );
	return 1;
}


int	vj_unicap_stop_capture( void *vut )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;
   	 
	if( !SUCCESS( unicap_stop_capture( v->handle ) ) )
   	{
   	 	veejay_msg(0,"Failed to stop capture on device: %s\n", v->device.identifier );
		return 0;
	}
	v->active = 0;
	veejay_msg(VEEJAY_MSG_DEBUG, "Stopped capture on device %s",
			v->device.identifier );
	return 1;
}

void	vj_unicap_free_device( void *vut )
{
	vj_unicap_t *v = (vj_unicap_t*) vut;
   
	if( v->active )
		vj_unicap_stop_capture( vut );
	
	if( !SUCCESS( unicap_close( v->handle ) ) )
   	{
   	 	veejay_msg(0, "Failed to close the device: %s\n", v->device.identifier );
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
