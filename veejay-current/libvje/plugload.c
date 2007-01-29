/*
 * Copyright (C) 2002-2006 Niels Elburg <nelburg@looze.net>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

/*
	Plugin Loader
		* FreeFrame
		* frei0r
		* Livido (pending)
 */

/*
     inspired by http://onsight.id.gu.se/~gabor/
                 http://www.gephex.org/

 */
#include <config.h>
#include <string.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <libhash/hash.h>
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <libvevo/vevo.h>
#include <libyuv/yuvconv.h>
#include <ffmpeg/avutil.h>
#include <ffmpeg/avcodec.h>
#include <libvevo/vevo.h>
#include <libvje/plugload.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define LINUX 1 
#include <libvje/specs/FreeFrame.h>
#include <libvje/specs/frei0r.h>

#define V_BITS 32
#include <assert.h>

#define   livido_port_t vevo_port_t
typedef f0r_instance_t (*f0r_construct_f)(unsigned int width, unsigned int height);
typedef void (*f0r_destruct_f)(f0r_instance_t instance);
typedef void (*f0r_deinit_f)(void);
typedef int (*f0r_init_f)(void);
typedef void (*f0r_get_plugin_info_f)(f0r_plugin_info_t *info);
typedef void (*f0r_get_param_info_f)(f0r_param_info_t *info, int param_index);
typedef void (*f0r_update_f)(f0r_instance_t instance, double time, const uint32_t *inframe, uint32_t *outframe);
typedef void (*f0r_update2_f)(f0r_instance_t instance, double time, const uint32_t *inframe1, const uint32_t *inframe2, const uint32_t *inframe3, uint32_t *outframe);
typedef void (*f0r_set_param_value_f)(f0r_instance_t *instance, f0r_param_t *param, int param_index);

#define VEVO_FF_PORT 10			// free frame port
#define VEVO_FF_PARAM_PORT 11		// free frame parameter port

#define	VEVO_FR_PORT	20		// frei0r port
#define	VEVO_FR_PARAM_PORT 21		// frei0r parameter port


//@@ missing specification on http://livido.dyne.org ! livido ignored for now
#define VEVO_LIVIDO_PORT	30	// livido port
#define VEVO_LIVIDO_PARAM_PORT	31	// livido parameter port

#define VEVO_ILLEGAL 100

#if (V_BITS == 32)
#define FF_CAP_V_BITS_VIDEO     FF_CAP_32BITVIDEO
#elif (V_BITS == 24)
#define FF_CAP_V_BITS_VIDEO     FF_CAP_24BITVIDEO
#else // V_BITS = 16
#define FF_CAP_V_BITS_VIDEO     FF_CAP_16BITVIDEO
#endif

static	vevo_port_t **index_map_ = NULL;
static  vevo_port_t *illegal_plugins_ =NULL;
static  int	index_     = 0;
static	void	*buffer_	 = NULL;
static	void	*buffer2_  = NULL;
static	void	*buffer_b_ = NULL;
static	int	base_width_	=0;
static	int	base_height_	=0;
static  int	n_ff_ = 0;
static	int	n_fr_ = 0;
static  int	n_lvd_ = 0;

static	int	select_f( const struct dirent *d )
{
	return ( strstr( d->d_name, ".so" ) != NULL );
}

static	int	init_param_livido( void *port, int p, int hint )
{
	return 0;
}

static	void	free_parameters( void *port, int n )
{
	int i;

	for ( i = 0; i < n ; i ++ )
	{
		char key[10];
		void *param = NULL;
		sprintf(key, "p%d", i );
		vevo_property_get(port, key,0, param );
		if( param )
			vevo_port_free( param );
	}
}

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
	snprintf(key,20, "p%d", p );
	vevo_property_set( port, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &parameter );

	return n_values;
}


static	void*	deal_with_livido( void *handle, char *name )
{
/*	void *port = vevo_port_new( VEVO_LIVIDO_PORT );
	char *plugin_name = NULL;

	livido_setup_f livido_setup = dlsym( handle, "livido_setup" );

	livido_setup_t setup[] = {
		{	(void(*)()) vj_malloc 	},	
		{	(void(*)()) free	},
		{	(void(*)())memset	},
                {	(void(*)())memcpy	},
                {	(void(*)())vevo_port_new	},
                {	(void(*)())vevo_port_free	},
                {	(void(*)())vevo_property_set	},
                {	(void(*)())vevo_property_get	},
                {	(void(*)())vevo_property_num_elements	},
                {	(void(*)())vevo_property_atom_type	},
                {	(void(*)())vevo_property_element_size	},
                {	(void(*)())vevo_list_properties	}
	};

	void *livido_plugin = livido_setup( setup, 100 );
	if(!livido_plugin)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error setting up '%s'",name );
		vevo_port_free( port );
		return NULL;
	}

	vevo_property_set( port, "lvd", LIVIDO_ATOM_TYPE_VOIDPTR, 1,&livido_plugin );
	vevo_property_set( port, "handle", LIVIDO_ATOM_TYPE_VOIDPTR,1,&handle );

	return port;
*/
	return NULL;
}

static	void* 	deal_with_fr( void *handle, char *name)
{
	void *port = vpn( VEVO_FR_PORT );
	
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

static	void*	deal_with_ff( void *handle, char *name )
{
	void *port = vpn( VEVO_FF_PORT );
	char *plugin_name = NULL;
	plugMainType *q = (plugMainType*) dlsym( handle, "plugMain" );

	if( q == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBorked FF plugin '%s': %s", name, dlerror());
		vevo_port_free( port );
		return NULL;
	}

	PlugInfoStruct *pis = (q(FF_GETINFO, NULL, 0)).PISvalue;

	if ((q(FF_GETPLUGINCAPS, (LPVOID)FF_CAP_V_BITS_VIDEO, 0)).ivalue != FF_TRUE)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s:%d", __FUNCTION__,__LINE__ );
		vevo_port_free(port);
		return NULL;
	}

	
	if (pis->APIMajorVersion < 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cowardly refusing FF API version < 1.0" );
		vevo_port_free(port);
		return NULL;
	}

	plugin_name = strdup( pis->pluginName ); 
	if ( (q(FF_INITIALISE, NULL, 0 )).ivalue == FF_FAIL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot call init()");
		vevo_port_free(port);	
		if(plugin_name) free(plugin_name);
		return NULL;
	}

	int n_params = q( FF_GETNUMPARAMETERS, NULL, 0 ).ivalue;
	if( n_params == FF_FAIL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot get number of parameters");
		vevo_port_free(port);
		if(plugin_name) free(plugin_name);
		return NULL;
	}

	void *base = (void*) q;
	vevo_property_set( port, "handle", VEVO_ATOM_TYPE_VOIDPTR,1, &handle );
	vevo_property_set( port, "name", VEVO_ATOM_TYPE_STRING,1, &plugin_name );
	vevo_property_set( port, "base", VEVO_ATOM_TYPE_VOIDPTR, 1, &base );
	vevo_property_set( port, "instance", VEVO_ATOM_TYPE_INT, 0, NULL );
	vevo_property_set( port, "n_params", VEVO_ATOM_TYPE_INT, 1,&n_params );
	
	int p;
	for( p=  0; p < n_params; p ++ )
	{
		void *parameter = vpn( VEVO_FF_PARAM_PORT );
		
		int type = q( FF_GETPARAMETERTYPE, (LPVOID) p, 0 ).ivalue;
		// name, kind, flags, description, min,max,default,transition
		vevo_property_set( parameter, "type", VEVO_ATOM_TYPE_INT, 1, &type);

		int min = 0;
		int max = 100;

		if( type == FF_TYPE_BOOLEAN )
		{
			min = 0;
			max = 1;
		}
		else if( type == FF_TYPE_TEXT )
		{
			min = 0;	
			max = 0;
		}

		vevo_property_set( parameter, "min", VEVO_ATOM_TYPE_INT,1, &min );
		vevo_property_set( parameter, "max", VEVO_ATOM_TYPE_INT,1, &max );

		float dvalue = 0.0;
		dvalue = q( FF_GETPARAMETERDEFAULT, (LPVOID) p, 0).fvalue;
		int ivalue = (int)(dvalue * 100.0);
		vevo_property_set( parameter, "default", VEVO_ATOM_TYPE_INT,1 ,&ivalue );

		char key[20];	
		snprintf(key,20, "p%d", p );
		vevo_property_set( port, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &parameter );
	}
	free(plugin_name);
	return port;
}

static	int	instantiate_plugin( void *plugin, int w , int h )
{

	int type = 0;
	assert( plugin != NULL );
	vevo_property_get( plugin, "type", 0, &type);
	if( type == VEVO_FF_PORT )
	{	
		VideoInfoStruct v;
		v.frameWidth = w;
		v.frameHeight = h;
		v.orientation = 1;
		v.bitDepth = FF_CAP_V_BITS_VIDEO;

		void *base = NULL;
		vevo_property_get( plugin, "base", 0, &base);
		plugMainType *q = (plugMainType*) base; 
		void *instance = q( FF_INSTANTIATE, &v, 0).ivalue;
		if( instance == FF_FAIL )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize plugin");
			return 0;
		}
		vevo_property_set( plugin, "instance", VEVO_ATOM_TYPE_INT, 1, &instance );
		return 1;
	}
	else if( type == VEVO_FR_PORT )
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
	else if ( type == VEVO_LIVIDO_PORT )
	{
		return 0;
	}
	return 0;
}

static	void	deinstantiate_plugin( void *plugin )
{
	if(plugin == NULL)
		return;

	int type = 0;
	assert( plugin != NULL);
	vevo_property_get( plugin, "type", 0, &type);

	if( type == VEVO_FF_PORT )
	{	
		void *base = NULL;
		vevo_property_get( plugin, "base", 0, &base);
		plugMainType *q = (plugMainType*) base; 

		void *instance = NULL;
		vevo_property_get( plugin, "instance", 0, &instance );
		if( instance )
			q( FF_DEINSTANTIATE, NULL, instance );
	}
	else if ( type == VEVO_FR_PORT )
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
	else if ( type == VEVO_LIVIDO_PORT )
	{

	}
}

static	void	add_to_plugin_list( const char *path )
{
	if(!path)
		return;

	int i;
	char fullname[PATH_MAX];
	struct	dirent	**files = NULL;
	struct stat sbuf;
	int	res = 0;

	memset( &sbuf,0 ,sizeof(struct stat));
	res = stat( path, &sbuf );

	if( res != 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "File or directory '%s' does not exist (skip)", path);
		return;
	}
	
	if( S_ISREG( sbuf.st_mode ) )
	{
		vevo_property_set( illegal_plugins_, path, VEVO_ATOM_TYPE_STRING, 0, NULL );
		return;
	}

	if( !S_ISDIR( sbuf.st_mode ) )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Not a directory : '%s'", path );
		return;
	}
	int n_files = scandir( path, &files, select_f, alphasort );
	if( n_files <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No FF plugins found in %s", path );
		return;
	}

	for( i = 0 ; i < n_files; i ++ )
	{
		char *name = files[i]->d_name;

		if( vevo_property_get( illegal_plugins_, name, 0 , NULL ) == 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "'%s' marked as bad", name);
			continue; 
		}
		bzero(fullname , PATH_MAX);
		sprintf(fullname, "%s/%s", path,name );

		void *handle = dlopen(fullname, RTLD_NOW );

		if(!handle) continue;

		if(dlsym( handle, "plugMain" ))
		{
			void *plugin = deal_with_ff( handle, name );
			if( plugin )
			{
				index_map_[ index_ ] = plugin;
				index_ ++;
				n_ff_ ++;
			}
			else
				dlclose( handle );	
		}
	
		if(dlsym( handle, "f0r_construct" ))
		{
			void *plugin = deal_with_fr( handle, name );
			if( plugin )
			{
				index_map_[ index_ ] = plugin;	
				index_ ++;
				n_fr_ ++;
			}
			else
				dlclose( handle );
		}

	/*	if(dlsym( handle, "livido_setup" ))
		{
			void *plugin = deal_with_livido( handle , name );
veejay_msg(VEEJAY_MSG_DEBUG, "Load livido plugin '%s'",name);
			if( plugin )
			{
				index_map_[index_] = plugin;
				index_ ++;
				n_lvd_ ++;
			}
			else
				dlclose(handle);
		}
		//@FIXME: backport livido from veejay-ng
		*/

	}

	for( i = 0; i < n_files; i ++ )
		free( files[i] );
	free(files);
}

static	void	free_plugin(void *plugin)
{
	if(plugin == NULL)
		return;

	int type = 0;
	vevo_property_get( plugin, "type", 0, &type);

	int n = 0;

	if( type == VEVO_FF_PORT )
	{		
		void *base = NULL;
		vevo_property_get( plugin, "base", 0, &base);
		plugMainType *q = (plugMainType*) base; 
		q( FF_DEINITIALISE, NULL, 0 );
		vevo_property_get( plugin, "n_params", 0, &n );
	}
	else if ( type == VEVO_FR_PORT )
	{
		//@@ clear parameters
		f0r_deinit_f base;
		vevo_property_get( plugin, "deinit", 0, &base);
		(*base)();
		vevo_property_get( plugin, "f0r_p", 0, &n );
	}
	else if ( type == VEVO_LIVIDO_PORT )
	{

	}

	free_parameters(plugin,n);


	void *handle = NULL;
	vevo_property_get( plugin, "handle", 0 , &handle );
	if( handle ) dlclose( handle );
	vevo_port_free( plugin );

}

static	void	free_plugins()
{
	int i;
	for( i = 0; i < index_ ; i ++ )
	{
		deinstantiate_plugin( index_map_[i] );
		free_plugin( index_map_[i]);
	}
	free( index_map_ );
	index_ = 0;
}

#define CONFIG_FILE_LEN 65535

static	void	scan_plugins()
{
	char *home = getenv( "HOME" );
	char path[PATH_MAX];
	char data[CONFIG_FILE_LEN];
	if(!home) return;
	
	sprintf( path , "%s/.veejay/plugins" , home );

	int fd = open( path, O_RDONLY );
	if( fd < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant open '~/.veejay/plugins. No plugins loaded");
		return;
	}
	veejay_msg(VEEJAY_MSG_INFO, "Parsing %s", path );

	bzero( data, CONFIG_FILE_LEN );

	if( read( fd, data, CONFIG_FILE_LEN ) > 0 )
	{
		int len = strlen(data);
		int j;
		int k = 0;
		char value[PATH_MAX];
	
		bzero( value, PATH_MAX );
		for( j=0; j < len; j ++ )
		{	
			if(data[j] == '\0' )
				break;
			if( data[j] == '\n' )
			 { add_to_plugin_list( value ); bzero(value,PATH_MAX); k =0;}

			if( isascii( data[j] ) && data[j] != '\n')
			  { value[k] = data[j]; if( k < PATH_MAX) k ++; }
		
		}
	}
}

static	void	process_mix_plugin( void *plugin, void *buffera , void *bufferb, void *out_buffer)
{
	int type = 0;
	vevo_property_get( plugin, "type", 0, &type);
	if( type == VEVO_FF_PORT )
	{	
	/*	void *base = NULL;
		vevo_property_get( plugin, "base", 0, &base);
		plugMainType *q = (plugMainType*) base; 
		int instance = 0;
		vevo_property_get( plugin, "instance",0, &instance );	
		q( FF_PROCESSFRAME, buffer, instance );*/
	}
	else if (type == VEVO_FR_PORT )
	{
		f0r_update2_f base;
		vevo_property_get( plugin, "process_mix", 0, &base );
		f0r_instance_t instance;
		vevo_property_get( plugin, "instance",0, &instance );		
		(*base)( instance, rand(), buffera, bufferb, NULL, out_buffer );
	}
	else if ( type == VEVO_LIVIDO_PORT )
	{

	}
}

static	void	process_plug_plugin( void *plugin, void *buffer , void *out_buffer)
{
	int type = 0;
	vevo_property_get( plugin, "type", 0, &type);
	if( type == VEVO_FF_PORT )
	{	
		void *base = NULL;
		vevo_property_get( plugin, "base", 0, &base);
		plugMainType *q = (plugMainType*) base; 
		void *instance = NULL;
		vevo_property_get( plugin, "instance",0, &instance );	
		q( FF_PROCESSFRAME, buffer, instance );
	}
	else if (type == VEVO_FR_PORT )
	{
		f0r_update_f base;
		vevo_property_get( plugin, "process", 0, &base );
		f0r_instance_t instance;
		vevo_property_get( plugin, "instance",0, &instance );		
		(*base)( instance, rand(), buffer, out_buffer );
	}
	else if (type == VEVO_LIVIDO_PORT )
	{

	}
}

/* public IF */

void	plug_free(void)
{
	free_plugins();
	if( buffer_ )
		free( buffer_ );
	if( buffer2_ )
		free( buffer2_ );
	if( buffer_b_)
		free( buffer_b_ );
}

void	plug_init( int w, int h )
{
	buffer_ = (void*) malloc( w * h * (V_BITS >> 3));
	memset( buffer_, 0, w * h * (V_BITS >> 3));
	buffer2_ = (void*) malloc( w * h * (V_BITS >> 3));
	memset( buffer2_, 0, w * h  * (V_BITS >> 3 ));
	buffer_b_ = (void*) malloc( w * h * (V_BITS >> 3));
	memset( buffer_b_, 0, w * h  * (V_BITS >> 3 ));
	
	base_width_ = w;
	base_height_ = h;
}

int	plug_detect_plugins(void)
{
	index_map_ = (vevo_port_t**) malloc(sizeof(vevo_port_t*) * 256 );
	memset( index_map_ , 0, sizeof( vevo_port_t*) * 256 );
	
	illegal_plugins_ = vpn( VEVO_ILLEGAL );

	scan_plugins();
	//@ display copyright notice in binary form
	if(n_ff_ > 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO, "FreeFrame Copyright (c) 2002, Marcus Clements www.freeframe.org. All Rights reserved.");

		veejay_msg(VEEJAY_MSG_INFO, "http://freeframe.sourceforge.net");
		veejay_msg(VEEJAY_MSG_INFO,"Loaded %d FreeFrame %s",
			n_ff_ , n_ff_ == 1 ? "plugin" : "plugins" );
	}
	if(n_fr_ > 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO, "frei0r - a minimalistic plugin API for video effects");
		veejay_msg(VEEJAY_MSG_INFO, "http://www.piksel.org/frei0r");
		veejay_msg(VEEJAY_MSG_INFO, "Loaded %d frei0r %s",
			n_fr_ , n_fr_ == 1 ? "plugin" : "plugins" );
	}
	if(n_lvd_ > 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Livido - (Linux) Video Objects" );
		veejay_msg(VEEJAY_MSG_INFO, "Loaded %d Livido %s",
			n_lvd_, n_lvd_ == 1 ? "plugin" :"plugins" );
	}

	vevo_port_free( illegal_plugins_ );

	return index_;
}

vj_effect	*plug_get_plugin( int n )
{
	vj_effect *vje = (vj_effect*) malloc(sizeof(vj_effect));
	memset( vje, 0,sizeof(vj_effect));
	vevo_port_t *port = index_map_[n];
	size_t name_len = vevo_property_element_size( port, "name", 0 );
	vje->description = (char*) malloc( name_len );
	vevo_property_get( port, "name", 0, &(vje->description));
	vevo_property_get( port, "n_params", 0, &(vje->num_params));
	vevo_property_get( port, "mixer", 0, &(vje->extra_frame));
	if( vje->num_params > 0 )
	{
		if( vje->num_params > 8 )
		{
			veejay_msg(VEEJAY_MSG_WARNING, "%s has %d parameters, clip to 8",
				vje->description, vje->num_params );
			vje->num_params = 8;
		}	

		vje->defaults = (int*) malloc(sizeof(int) * vje->num_params );
		vje->limits[0] = (int*) malloc(sizeof(int) * vje->num_params );
		vje->limits[1] = (int*) malloc(sizeof(int) * vje->num_params );

		memset( vje->defaults,0,sizeof(int) * vje->num_params );
		memset( vje->limits[0],0,sizeof(int) * vje->num_params );
		memset( vje->limits[1],0, sizeof(int) * vje->num_params );

		int k = 0;
		int valid_p = 0;
		for( k = 0; k < vje->num_params;k++ )
		{
			char key[20];
			sprintf(key, "p%d", k );
			void *parameter = NULL;
			vevo_property_get( port, key, 0, &parameter );
			if(parameter)
			{	
				vevo_property_get( parameter, "min", 0, &(vje->limits[0][k]));
				vevo_property_get( parameter, "max", 0, &(vje->limits[1][k]));	
				vevo_property_get( parameter, "default", 0,&(vje->defaults[k]));
				valid_p ++;
			}
		}		
		vevo_property_set( port, "n_params",VEVO_ATOM_TYPE_INT, 1,&valid_p);
		vje->num_params = valid_p;
	}

	return vje;
}


int	plug_activate( int fx_id )
{
	return	instantiate_plugin( index_map_[fx_id], base_width_, base_height_ );
}
void	plug_deactivate( int fx_id )
{
	deinstantiate_plugin( index_map_[fx_id] );	
}

void	plug_control( int fx_id, int *args )
{
	vevo_port_t *port = index_map_[ fx_id ];
		
	int type = 0;
	vevo_property_get( port, "type", 0, &type);
	if( type == VEVO_FF_PORT )
	{	
		SetParameterStruct	v;
		void *base = NULL;
		vevo_property_get( port, "base", 0, &base);
		plugMainType *q = (plugMainType*) base; 
		int p,num_params=0;
		void *instance = NULL;
		vevo_property_get( port, "n_params", 0, &num_params);
		vevo_property_get( port, "instance", 0, &instance );
		for( p = 0; p < num_params; p ++ )
		{
			v.value = ((float) args[p]) * 0.01;
			v.index = p;
			q( FF_SETPARAMETER, &v, instance );
		}
	}
	else if ( type == VEVO_FR_PORT )
	{
		int p,num_params=0;
		vevo_property_get( port, "n_params", 0, &num_params);
		int v_params = 0;

		f0r_set_param_value_f q;

		vevo_property_get( port, "set_params", 0, &q);
		for( p = 0; p < num_params; p ++ )
		{
			char key[20];
			sprintf(key, "p%d", p );
			void *param = NULL;
			vevo_property_get( port, key, 0, &param );
			if( param ) continue;

			int n = vevo_property_element_size( param, "value", p );

			f0r_param_position_t pos;	
			f0r_param_color_t col;
							
			double value = 0.0;
		//nt instance = 0;
			void *instance = NULL;
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
	else if ( type == VEVO_LIVIDO_PORT )
	{

	}
}

void	plug_process_mix( VJFrame *frame, VJFrame *frame_b,int fx_id, int src_fmt )
{
	AVPicture p1,p2;
	AVPicture o1,o2;
	AVPicture b1,b2 ;
	void	*plugin = index_map_[fx_id];
	
	memset( &p1, 0, sizeof(p1));
	memset( &p2, 0, sizeof(p2));
	memset( &o1, 0, sizeof(o1));
	memset( &o2, 0, sizeof(o2));
	memset( &b1, 0, sizeof(b1));
	memset( &b2, 0, sizeof(b2));

	p1.data[0] = buffer_;
	p1.linesize[0] = base_width_ * 4;

	p2.data[0] = frame->data[0];
	p2.data[1] = frame->data[1];
	p2.data[2] = frame->data[2];

	b1.data[0] = buffer_b_;
	b1.linesize[0] = base_width_ * 4;

	b2.data[0] = frame_b->data[0];
	b2.data[1] = frame_b->data[1];
	b2.data[2] = frame_b->data[2];

	p2.linesize[0] = base_width_;
	p2.linesize[1] = (src_fmt < 2 ? base_width_ >> 1 : base_width_);
	p2.linesize[2] = (src_fmt < 2 ? base_width_ >> 1 : base_width_); 

	int srcf = 0;
	if( src_fmt == 1 )
		srcf = PIX_FMT_YUV422P;
	else if( src_fmt == 0 )
		srcf = PIX_FMT_YUV420P;
		else if ( src_fmt == 2 )
			srcf = PIX_FMT_YUVJ420P;
		   else srcf = PIX_FMT_YUVJ422P;

	img_convert( &p1, PIX_FMT_RGBA32,
		     &p2, srcf,
			base_width_, base_height_ );
	img_convert( &b1, PIX_FMT_RGBA32,
		     &b2, srcf,
			base_width_, base_height_ );


	process_mix_plugin( plugin, p1.data[0],buffer_b_, buffer2_ );
	int type = 0;
	vevo_property_get( plugin, "type", 0, &type );
	if( type == VEVO_FF_PORT )
	{
		o1.data[0] = p1.data[0];
		o1.linesize[0] = base_width_ * 4;
	}
	else if (type == VEVO_FR_PORT )
	{
		o1.data[0] = buffer2_;
		o1.linesize[0] = base_width_ * 4;
	}

	o2.data[0] = frame->data[0];
	o2.data[1] = frame->data[1];
	o2.data[2] = frame->data[2];

	o2.linesize[0] = base_width_;
	o2.linesize[1] = (src_fmt < 2 ? base_width_ >> 1 : base_width_);
	o2.linesize[2] = (src_fmt < 2 ? base_width_ >> 1 : base_width_); 

	img_convert( &o2, srcf,
		     &o1, PIX_FMT_RGBA32,
 			base_width_, base_height_ );
	
}
void	plug_process( VJFrame *frame,VJFrame *b, int fx_id, int src_fmt )
{
	AVPicture p1,p2;
	AVPicture o1,o2;
	void	*plugin = index_map_[fx_id];

	int	is_mix = 0;
	vevo_property_get( plugin, "mixer", 0, &is_mix );
	if( is_mix ) 
	{
		plug_process_mix( frame, b, fx_id, src_fmt );
		return;
	}
	
	memset( &p1, 0, sizeof(p1));
	memset( &p2, 0, sizeof(p2));
	memset( &o1, 0, sizeof(o1));
	memset( &o2, 0, sizeof(o2));

	p1.data[0] = buffer_;
	p1.linesize[0] = base_width_ * 4;

	p2.data[0] = frame->data[0];
	p2.data[1] = frame->data[1];
	p2.data[2] = frame->data[2];

	p2.linesize[0] = base_width_;
	p2.linesize[1] = (src_fmt < 2 ? base_width_ >> 1 : base_width_);
	p2.linesize[2] = (src_fmt < 2 ? base_width_ >> 1 : base_width_); 

	int srcf = 0;
	if( src_fmt == 1 )
		srcf = PIX_FMT_YUV422P;
	else if( src_fmt == 0 )
		srcf = PIX_FMT_YUV420P;
		else if( src_fmt == 2 )
			srcf = PIX_FMT_YUVJ420P;
	else srcf = PIX_FMT_YUVJ422P;

	img_convert( &p1, PIX_FMT_RGBA32,
		     &p2, srcf,
			base_width_, base_height_ );

	process_plug_plugin( plugin, p1.data[0], buffer2_ );
	int type = 0;
	vevo_property_get( plugin, "type", 0, &type );
	if( type == VEVO_FF_PORT )
	{
		o1.data[0] = p1.data[0];
		o1.linesize[0] = base_width_ * 4;
	}
	else if (type == VEVO_FR_PORT )
	{
		o1.data[0] = buffer2_;
		o1.linesize[0] = base_width_ * 4;
	}
	else if (type == VEVO_LIVIDO_PORT )
	{

	}

	o2.data[0] = frame->data[0];
	o2.data[1] = frame->data[1];
	o2.data[2] = frame->data[2];

	o2.linesize[0] = base_width_;
	o2.linesize[1] = (src_fmt < 2 ? base_width_ >> 1 : base_width_);
	o2.linesize[2] = (src_fmt < 2 ? base_width_ >> 1 : base_width_); 

	img_convert( &o2, srcf,
		     &o1, PIX_FMT_RGBA32,
 			base_width_, base_height_ );
	
}

