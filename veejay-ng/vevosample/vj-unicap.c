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
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // for memset
#include <unicap.h>
#include <unicap_status.h>

#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <libyuv/yuvconv.h>
#include <libvevo/libvevo.h>
#include <vevosample/vj-unicap.h>
#include <veejay/defs.h>
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
	void	*sampler;
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
	
	
		
		unicap_reenumerate_properties( ud->handle, &property_count );
		unicap_reenumerate_formats( ud->handle, &format_count );
		char *device_name = strdup( ud->device.identifier );

#ifdef STRICT_CHECKING
		void	*device_port = vevo_port_new( VEVO_ANONYMOUS_PORT, device_name, i );
#else
		void	*device_port = vevo_port_new( VEVO_ANONYMOUS_PORT );
#endif
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
		unicap_close( ud->handle );
	}
	return i;
}

void	*vj_unicap_init(void)
{
	unicap_driver_t *ud = (unicap_driver_t*) vj_malloc(sizeof(unicap_driver_t));
	memset( ud,0,sizeof(unicap_driver_t));
#ifdef STRICT_CHECKING
	ud->device_list = vevo_port_new( VEVO_ANONYMOUS_PORT, __FUNCTION__, __LINE__ );
#else
	ud->device_list = vevo_port_new( VEVO_ANONYMOUS_PORT );
#endif
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
/*
int	vj_unicap_set_property( void *ud, char *key, int atom_type, void *val )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	if(atom_type != VEVO_ATOM_TYPE_STRING && atom_type != VEVO_ATOM_TYPE_DOUBLE )
		return 0;
	
	unicap_void_property( &property_spec );
	vj_unicap_t *vut = (vj_unicap_t*) ud;

	for( i = 0; SUCCESS( unicap_enumerate_properties( vut->handle,
					&property_spec, &property, i ) ); i ++ )
	{
		unicap_get_property( vut->handle, &property);

		if( strcmp(property.identifier, key) == 0 )
		{	
			if(atom_type == VEVO_ATOM_TYPE_STRING)
			{
				char *str = (char*) val;
				sprintf( property.menu_item , "%s", str);
			}
			else
			{	
				if(atom_type == VEVO_ATOM_TYPE_DOUBLE)
				{
					memcpy(&property.value, val, sizeof(double));
				}
			}
				
			unicap_set_property( vut->handle, &property );
			break;
		}
	}
	return 1;
}
*/
int	vj_unicap_select_value( void *ud, char *key, int atom_type, void *val )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	unicap_void_property( &property_spec );
	vj_unicap_t *vut = (vj_unicap_t*) ud;
	for( i = 0; SUCCESS( unicap_enumerate_properties( vut->handle,
					&property_spec, &property, i ) ); i ++ )
	{
		unicap_get_property( vut->handle, &property);
		if( strcmp( property.identifier, key ) == 0 )
		{
			
		if( property.type == UNICAP_PROPERTY_TYPE_MENU )
		{
			int n = property.menu.menu_item_count;
#ifdef STRICT_CHECKING
			assert( atom_type == VEVO_ATOM_TYPE_DOUBLE );
#endif
			int idx = (int) *( (double*) val );
			veejay_msg(0, "To menu item %d",idx);
			strcpy( property.menu_item, property.menu.menu_items[idx] );

			unicap_set_property( vut->handle, &property );

			
			veejay_msg(0,"changed menu item %d to %s", n, property.menu_item );
			return 1;
		}
		if( property.type == UNICAP_PROPERTY_TYPE_RANGE )
		{
#ifdef STRICT_CHECKING
			assert( atom_type == VEVO_ATOM_TYPE_DOUBLE) ;
#endif
			double fval = (double) *( (double*) val);
			if(fval < property.range.min)
				 fval = property.range.min;
			 else if(fval > property.range.max) 
				 fval = property.range.max;
			 property.value = (double) *((double*) val);
			 unicap_set_property( vut->handle, &property );
			veejay_msg(0, "Changed range value to %f", property.value );
			return 1;
		}
		}
	}
	return 0;
}

int	vj_unicap_get_range( void *ud, char *key, double *min , double *max )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	unicap_void_property( &property_spec );
	vj_unicap_t *vut = (vj_unicap_t*) ud;

	for( i = 0; SUCCESS( unicap_enumerate_properties( vut->handle,
					&property_spec, &property, i ) ); i ++ )
	{
		unicap_get_property( vut->handle, &property);

		if( strcmp( property.identifier, key ) == 0 )
			continue;
		
		if( property.type == UNICAP_PROPERTY_TYPE_MENU )
		{
			*min = 0.0;
			*max = (double) property.menu.menu_item_count;
			return 1;
		}
		if( property.type == UNICAP_PROPERTY_TYPE_RANGE )
		{
			*min = property.range.min;
			*max = property.range.max;
			return 1;
		}
	}
	return 0;
}

char	**vj_unicap_get_list( void *ud )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	unicap_void_property( &property_spec );
	vj_unicap_t *vut = (vj_unicap_t*) ud;

	for( i = 0; SUCCESS( unicap_enumerate_properties( vut->handle,
					&property_spec, &property, i ) ); i ++ )
	{
	}

	int n = i;

	char **res = (char**) malloc(sizeof(char*) * (n+1) );
	memset(res, 0,sizeof(char*) * (n+1));
	
	for( i = 0;i < n; i ++ )
	{
		if( SUCCESS( unicap_enumerate_properties(vut->handle,
						&property_spec,&property,i ) ) )
		{
			res[i] = strdup( property.identifier );
		}
	}
	return res;
}


int	vj_unicap_get_value( void *ud, char *key, int atom_type, void *value )
{
	unicap_property_t property;
        unicap_property_t property_spec;
	int i;
	unicap_void_property( &property_spec );
	vj_unicap_t *vut = (vj_unicap_t*) ud;

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
			return 1;
		}
	}
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
		veejay_msg(0, "I only found %d devices", ud->num_devices );
		return NULL;
	}
	
	vj_unicap_t *vut = (vj_unicap_t*) vj_malloc(sizeof(vj_unicap_t));
	memset(vut,0,sizeof(vj_unicap_t));

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
int	vj_unicap_configure_device( void *ud, int pixel_format, int w, int h )
{
	vj_unicap_t *vut = (vj_unicap_t*) ud;
	
	unicap_void_format( &(vut->format_spec));

	unsigned int fourcc = 0;
	vut->sizes[0] = w * h;
	
	switch(pixel_format)
	{
		case FMT_420:
			fourcc = get_fourcc( "YU12" );
			vut->sizes[1] = (w*h)/4;
			vut->sizes[2] = vut->sizes[1];
			break;
		case FMT_422:
			fourcc = get_fourcc( "422P" );
			vut->sizes[1] = (w*h)/2;
			vut->sizes[2] = vut->sizes[1];
			break;
		case FMT_444:
			fourcc = get_fourcc( "422P" );
			vut->sampler = subsample_init( w );
			vut->sizes[1] = (w*h)/2;
			vut->sizes[2] = vut->sizes[1];
			vut->sampler = subsample_init( w );
			break;
#ifdef STRICT_CHECKING
		default:
			assert(0);
			break;
#endif
	}	
	
	int i;
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
			return 0;
		}
		else
			if( !SUCCESS( unicap_set_format( vut->handle, &rgb_format ) ) )
			{
				veejay_msg(0, "Cannot set size %d x %d or format %s", w,h,rgb_format.identifier);
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
	
	vut->buffer.data = vj_malloc( test.size.width * test.size.height * 4 );
	vut->buffer.buffer_size = (sizeof(unsigned char) * 4 * test.size.width * test.size.height );
	return 1;
}

int	vj_unicap_start_capture( void *vut, void *slot )
{
	VJFrame *f = (VJFrame*)slot;
	vj_unicap_t *v = (vj_unicap_t*) vut;

	if( !SUCCESS( unicap_start_capture( v->handle ) ) )
   	{
      		veejay_msg( 0, "Failed to start capture on device: %s\n", v->device.identifier );
		return 0;
     	}
	v->active = 1;
	return 1;
}

int	vj_unicap_grab_frame( void *vut, void *slot )
{
	VJFrame *f = (VJFrame*)slot;
	vj_unicap_t *v = (vj_unicap_t*) vut;
	if(!v->active)
	{
		if(!vj_unicap_start_capture( vut, slot ))
			return 0;	
	}
	
   	if( !SUCCESS( unicap_queue_buffer( v->handle, &(v->buffer) ) ) )
   	{
		veejay_msg( 0, "Failed to queue a buffer on device: %s\n", v->device.identifier );
		return 0;
	}
		
	if( !SUCCESS( unicap_wait_buffer( v->handle, &(v->returned_buffer )) ) )
	{
		veejay_msg(0,"Failed to wait for buffer on device: %s\n", v->device.identifier );
		return 0;
	}

	if( v->deinterlace )
	{
		yuv_deinterlace( f, v->buffer.data,v->buffer.data+v->sizes[0],v->buffer.data+v->sizes[0]+
			v->sizes[1] );
		if( v->sampler )
			chroma_supersample( SSM_422_444, v->sampler, f->data, f->width,f->height );
	}
	else
	{
		if(!v->rgb)
		{
			veejay_memcpy( f->data[0], v->buffer.data, v->sizes[0] );
			veejay_memcpy( f->data[1], v->buffer.data + v->sizes[0], v->sizes[1] );
			veejay_memcpy( f->data[2], v->buffer.data + v->sizes[0] +v->sizes[1] , v->sizes[2]);
			if( v->sampler )
				chroma_supersample( SSM_422_444, v->sampler, f->data, f->width,f->height );

		}
		else
		{
			util_convertsrc( v->buffer.data, f->width,f->height,f->pixfmt, f->data );
		}
	}	
	
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

}

