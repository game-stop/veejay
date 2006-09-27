#include <config.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <libhash/hash.h>
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <libvevo/libvevo.h>
#include <libplugger/defs.h>
#include <libyuv/yuvconv.h>
#include <ffmpeg/avcodec.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <veejay/portdef.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
/** \defgroup freior Freior Host
 *
 * This module provides a Frei0r Host
 */
#include <libplugger/specs/frei0r.h>

typedef f0r_instance_t (*f0r_construct_f)(unsigned int width, unsigned int height);

typedef void (*f0r_destruct_f)(f0r_instance_t instance);

typedef void (*f0r_deinit_f)(void);

typedef int (*f0r_init_f)(void);

typedef void (*f0r_get_plugin_info_f)(f0r_plugin_info_t *info);

typedef void (*f0r_get_param_info_f)(f0r_param_info_t *info, int param_index);

typedef void (*f0r_update_f)(f0r_instance_t instance, double time, const uint32_t *inframe, uint32_t *outframe);

typedef void (*f0r_update2_f)(f0r_instance_t instance, double time, const uint32_t *inframe1, const uint32_t *inframe2, const uint32_t *inframe3, uint32_t *outframe);

typedef void (*f0r_set_param_value_f)(f0r_instance_t *instance, f0r_param_t *param, int param_index);


static	int	init_param_fr( void *port, int p, int hint)
{
	void *parameter = vpn( VEVO_FR_PARAM_PORT );
	int min = 0;
	int max = 100;
	int dv = 50;
	int n_values = 0;

	switch(hint)
	{
		case F0R_PARAM_DOUBLE:
			n_values = 1;
			break;
		case F0R_PARAM_BOOL:
			max = 1;
			dv = 0;
			n_values = 1;
			break;
		case F0R_PARAM_COLOR:
			n_values = 3;
			break;
		case F0R_PARAM_POSITION:
			n_values = 2;
			break;
		default:
			break;
	}

	if( n_values > 0 )
	{
		int values[n_values];
		int k;
		for( k = 0; k < n_values; k ++ ) values[k] = 0;
		vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_INT, n_values, &values );	
	}

	vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_INT, 0, NULL );

	vevo_property_set( parameter, "min", VEVO_ATOM_TYPE_INT,1, &min );
	vevo_property_set( parameter, "max", VEVO_ATOM_TYPE_INT,1, &max );
	vevo_property_set( parameter, "default", VEVO_ATOM_TYPE_INT,1, &dv );
	vevo_property_set( parameter, "hint", VEVO_ATOM_TYPE_INT,1, &hint );


	char key[20];	
	snprintf(key,20, "p%02d", p );
	vevo_property_set( port, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &parameter );

	return n_values;
}
void* 	deal_with_fr( void *handle, char *name)
{
	void *port = vpn( VEVO_FR_PORT );
	char *plugin_name = NULL;
	f0r_init_f	f0r_init	= dlsym( handle, "f0r_init" );
	if( f0r_init == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBorked frei0r plugin '%s': %s", name, dlerror());
		vevo_port_free( port );
		return NULL;
	}

	f0r_deinit_f	f0r_deinit	= dlsym( handle, "f0r_deinit" );
	if( f0r_deinit == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBorked frei0r plugin '%s': %s", name, dlerror());
		vevo_port_free( port );
		return NULL;
	}

	f0r_get_plugin_info_f	f0r_info = dlsym( handle, "f0r_get_plugin_info");
	if( f0r_info == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBorked frei0r plugin '%s': %s", name, dlerror());
		vevo_port_free( port );
		return NULL;
	}

	f0r_get_param_info_f	f0r_param= dlsym( handle, "f0r_get_param_info" );
	if( f0r_param == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBorked frei0r plugin '%s': %s", name, dlerror());
		vevo_port_free( port );
		return NULL;
	}
//@ gamble
	void	*f0r_construct	= dlsym( handle, "f0r_construct" );
	void	*f0r_destruct	= dlsym( handle, "f0r_destruct" );
	void	*processf	= dlsym( handle, "f0r_update" );
//	void	*processm	= dlsym( handle, "f0r_update2" );
	void	*set_params	= dlsym( handle, "f0r_set_param_value" );


	vevo_property_set( port, "handle", VEVO_ATOM_TYPE_VOIDPTR,1, &handle );
	vevo_property_set( port, "init", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_init );
	vevo_property_set( port, "deinit", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_deinit );
	vevo_property_set( port, "info", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_info );
	vevo_property_set( port, "parameters", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_param );
	vevo_property_set( port, "construct", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_construct );
	vevo_property_set( port, "destruct", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_destruct );
	vevo_property_set( port, "process", VEVO_ATOM_TYPE_VOIDPTR, 1, &processf);
//	vevo_property_set( port, "process_mix", VEVO_ATOM_TYPE_VOIDPTR, 1, &processm);
	vevo_property_set( port, "set_params", VEVO_ATOM_TYPE_VOIDPTR,1,&set_params);	

    	f0r_plugin_info_t finfo;
	f0r_param_info_t pinfo;

	memset( &finfo,0,sizeof(f0r_plugin_info_t));
	memset( &pinfo,0,sizeof(f0r_param_info_t));


	if( (*f0r_init)() == 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBorked frei0r plugin '%s': ", name);
		vevo_port_free( port );
		return NULL;
	}

	(*f0r_info)(&finfo);

	if( finfo.frei0r_version != FREI0R_MAJOR_VERSION )
	{
		(*f0r_deinit)();	
		vevo_port_free(port);
		return NULL;	
	}
	int extra = 0;
//@ fixme
//	if( finfo.plugin_type == F0R_PLUGIN_TYPE_MIXER2 )
//		extra = 1;
	
	int n_params = finfo.num_params;
	int r_params = 0;
	int p = 0;
	for ( p = 0; p < n_params; p ++ )
	{
		(*f0r_param)(&pinfo,p);
		r_params += init_param_fr( port, p, pinfo.type );
	}

	if( r_params > 8 )
		r_params = 8;

	char *plug_name = strdup( finfo.name );
	vevo_property_set( port, "n_params", VEVO_ATOM_TYPE_INT, 1, &r_params );
	vevo_property_set( port, "f0r_p", VEVO_ATOM_TYPE_INT,1, &n_params );
	vevo_property_set( port, "name", VEVO_ATOM_TYPE_STRING,1, &plug_name );
	vevo_property_set( port, "mixer", VEVO_ATOM_TYPE_INT,1, &extra );
	free(plug_name);
	return port;
}


int	frei0r_plug_init( void *plugin , int w, int h )
{
	f0r_construct_f base;
	vevo_property_get( plugin, "construct", 0, &base);
	f0r_instance_t k = (*base)(w,h);
	vevo_property_set(plugin, "instance", VEVO_ATOM_TYPE_VOIDPTR, 1, &k);
	if( k == NULL )
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize plugin");
	// store instance running status
	int state = 1;
	vevo_property_set( plugin, "running", VEVO_ATOM_TYPE_INT, 1, &state );
	return state;
}

void	frei0r_plug_deinit( void *plugin, int w, int h )
{
	int state = 0;
	vevo_property_get( plugin, "state", 0, &state );
	if(!state)
		return;

	f0r_destruct_f base;
	vevo_property_get( plugin, "destruct", 0, &base);
	f0r_instance_t instance;
	vevo_property_get( plugin, "instance", 0, &instance );
	(*base)(instance);
}

void	frei0r_plug_free( void *plugin )
{
	int n = 0;
	//@@ clear parameters
	f0r_deinit_f base;
	vevo_property_get( plugin, "deinit", 0, &base);
	(*base)();
	vevo_property_get( plugin, "f0r_p", 0, &n );
}

int	frei0r_plug_process( void *plugin, void *buffer, void *out_buffer )
{
	f0r_update_f base;
	vevo_property_get( plugin, "process", 0, &base );
	f0r_instance_t instance;
	vevo_property_get( plugin, "instance",0, &instance );		
	(*base)( instance, rand(), buffer, out_buffer );
	return 1;
}

void	frei0r_plug_control( void *port, int *args )
{
	int p,num_params=0;
	vevo_property_get( port, "n_params", 0, &num_params);
	int v_params = 0;

	f0r_set_param_value_f q;

	vevo_property_get( port, "set_params", 0, &q);
	for( p = 0; p < num_params; p ++ )
	{
		char key[20];
		sprintf(key, "p%02d", p );
		void *param = NULL;
		vevo_property_get( port, key, 0, &param );
		if( param ) continue;

		int n = vevo_property_element_size( param, "value", p );

		f0r_param_position_t pos;	
		f0r_param_color_t col;
						
		double value = 0.0;
		int instance = 0;
		vevo_property_get( port, "instance", 0, &instance );
		int max = 0;
		vevo_property_get( param, "max",0,&max);

		void *fparam = NULL;
		switch(n)
		{
			case 0:
			case 1:
				value = ( (double) args[v_params] * (max == 100 ? 0.01: 1));
				fparam = &value;
				v_params ++;
				break;
			case 2:
				pos.x = ( (double) args[v_params] * 0.01 );
				v_params ++;
				pos.y = ( (double) args[v_params] * 0.01 );
				v_params ++;
				fparam = &pos;
				break;	
			case 3:
				col.r = ( (double) args[v_params] * 0.01 );
				v_params ++;
				col.g = ( (double) args[v_params] * 0.01 );
				v_params ++;
				col.b = ( (double) args[v_params] * 0.01 );
				v_params ++;
				fparam = &col;
				break;
			default:
				break;
		}
		if( fparam )
			(*q)( instance, fparam, p );
	}
}


void	frei0r_plug_process_ext( void *plugin, void *in0, void *in1, void *out)
{
	f0r_update2_f base;
	vevo_property_get( plugin, "process_mix", 0, &base );
	f0r_instance_t instance;
	vevo_property_get( plugin, "instance",0, &instance );		
	(*base)( instance, rand(), in0, in1, NULL, out );
}
