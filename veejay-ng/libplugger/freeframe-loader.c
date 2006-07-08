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
#include <veejay/portdef.h>
#include <ffmpeg/avcodec.h>
#define LINUX 1 
#include <libplugger/specs/FreeFrame.h>
#define V_BITS 32

/** \defgroup freeframe FreeFrame Host
 *
 * Provides a host implementation for FreeFrame plugins, see http://freeframe.sourceforge.net
 */

#if (V_BITS == 32)
#define FF_CAP_V_BITS_VIDEO     FF_CAP_32BITVIDEO
#elif (V_BITS == 24)
#define FF_CAP_V_BITS_VIDEO     FF_CAP_24BITVIDEO
#else // V_BITS = 16
#define FF_CAP_V_BITS_VIDEO     FF_CAP_16BITVIDEO
#endif

void*	deal_with_ff( void *handle, char *name )
{
	void *port = vevo_port_new( VEVO_FF_PORT );
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
	int mix = 0;
	void *base = (void*) q;
	vevo_property_set( port, "handle", VEVO_ATOM_TYPE_VOIDPTR,1, &handle );
	vevo_property_set( port, "name", VEVO_ATOM_TYPE_STRING,1, &plugin_name );
	vevo_property_set( port, "base", VEVO_ATOM_TYPE_VOIDPTR, 1, &base );
	vevo_property_set( port, "instance", VEVO_ATOM_TYPE_INT, 0, NULL );
	vevo_property_set( port, "n_params", VEVO_ATOM_TYPE_INT, 1,&n_params );
	vevo_property_set( port, "mixer", VEVO_ATOM_TYPE_INT, 1,&mix );
	
	int p;
	for( p=  0; p < n_params; p ++ )
	{
		void *parameter = vevo_port_new( VEVO_FF_PARAM_PORT );
		
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
		snprintf(key,20, "p%02d", p );
		vevo_property_set( port, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &parameter );
	}
	free(plugin_name);
	return port;
}

int	freeframe_plug_init( void *plugin, int w, int h )
{
	VideoInfoStruct v;
	v.frameWidth = w;
	v.frameHeight = h;
	v.orientation = 1;
	v.bitDepth = FF_CAP_V_BITS_VIDEO;

	void *base = NULL;
	int error = vevo_property_get( plugin, "base", 0, &base);
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 
	int instance = q( FF_INSTANTIATE, &v, 0).ivalue;
	if( instance == FF_FAIL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize plugin");
		return 0;
	}
	vevo_property_set( plugin, "instance", VEVO_ATOM_TYPE_INT, 1, &instance );
	return 1;
}


void	freeframe_plug_deinit( void *plugin )
{
	void *base = NULL;
	int error = vevo_property_get( plugin, "base", 0, &base);
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 

	int instance = 0;
	error = vevo_property_get( plugin, "instance", 0, &instance );
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif

	if( instance )
		q( FF_DEINSTANTIATE, NULL, instance );
}

void	freeframe_plug_free( void *plugin )
{
	int n = 0;
	void *base = NULL;
	int error = vevo_property_get( plugin, "base", 0, &base);
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif
	plugMainType *q = (plugMainType*) base; 
	q( FF_DEINITIALISE, NULL, 0 );
}

int	freeframe_plug_process( void *plugin, void *in )
{
	void *base = NULL;
	int error = vevo_property_get( plugin, "base", 0, &base);
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 
	int instance = 0;
	error = vevo_property_get( plugin, "instance",0, &instance );	
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif

	q( FF_PROCESSFRAME, in, instance );
	return 1;
}

void	freeframe_plug_control( void *port, int *args )
{
	SetParameterStruct	v;
	void *base = NULL;
	vevo_property_get( port, "base", 0, &base);
	plugMainType *q = (plugMainType*) base; 
	int p,num_params=0;
	int instance = 0;
	int error = 0;
	error = vevo_property_get( port, "n_params", 0, &num_params);
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif
	error = vevo_property_get( port, "instance", 0, &instance );
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif

	for( p = 0; p < num_params; p ++ )
	{
		v.value = ((float) args[p]) * 0.01;
		v.index = p;
		q( FF_SETPARAMETER, &v, instance );
	}
}

void	freeframe_plug_process_ext( void *port, void *in0, void *in1, void *out)
{

#ifdef STRICT_CHECING
	assert(0);
#endif
	/*	void *base = NULL;
		vevo_property_get( plugin, "base", 0, &base);
		plugMainType *q = (plugMainType*) base; 
		int instance = 0;
		vevo_property_get( plugin, "instance",0, &instance );	
		q( FF_PROCESSFRAME, buffer, instance );*/

}
