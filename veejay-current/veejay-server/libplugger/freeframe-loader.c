/* veejay - Linux VeeJay - libplugger utility
 *           (C) 2010      Niels Elburg <nwelburg@gmail.com> ported from veejay-ng
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <libhash/hash.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvevo/libvevo.h>
#include <libvje/vje.h>
#include <libplugger/defs.h>
#include <libplugger/ldefs.h>
#include <libyuv/yuvconv.h>
#include <libavutil/avutil.h>
#include <libplugger/portdef.h>
#define LINUX 1 
#include <libplugger/specs/FreeFrame.h>
#define V_BITS 24
#include <libplugger/freeframe-loader.h>

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

typedef struct
{
	int w;
	int h;
	uint8_t *rgb;
} ff_frame_t;


/** \defgroup freeframe FreeFrame Host
 *
 * Provides a host implementation for FreeFrame plugins, see http://freeframe.sourceforge.net
 */

//#if (V_BITS == 32)
#define FF_CAP_V_BITS_VIDEO     FF_CAP_32BITVIDEO
static	int	freeframe_signature_ = VEVO_PLUG_FF;
static void *rgb_conv_ = NULL;
static void *yuv_conv_ = NULL;
/*#elif (V_BITS == 24)
#define FF_CAP_V_BITS_VIDEO     FF_CAP_24BITVIDEO
#else // V_BITS = 16
#define FF_CAP_V_BITS_VIDEO     FF_CAP_16BITVIDEO
#endif*/

void	freeframe_destroy( ) {
	if( rgb_conv_ ) {
		yuv_free_swscaler( rgb_conv_ );
	}
	if( yuv_conv_ ) {
		yuv_free_swscaler( yuv_conv_ );
	}
}

void*	deal_with_ff( void *handle, char *name )
{
	void *port = vpn( VEVO_FF_PORT );
	char *plugin_name = NULL;
	plugMainType *q = (plugMainType*) dlsym( handle, "plugMain" );

	if( q == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBad FreeFrame plugin '%s': %s", name, dlerror());
		vpf( port );
		return NULL;
	}

	PlugInfoStruct *pis = (q(FF_GETINFO, NULL, 0)).PISvalue;

	if ((q(FF_GETPLUGINCAPS, (LPVOID)FF_CAP_V_BITS_VIDEO, 0)).ivalue != FF_TRUE)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "FreeFrame plugin '%s' cannot handle 32 bit",name);
		vpf(port);
		return NULL;
	}

	
	if (pis->APIMajorVersion < 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cowardly refusing FreeFrame API version < 1.0 (%s)",name );
		vpf(port);
		return NULL;
	}

	plugin_name = strdup( pis->pluginName ); 
	if ( (q(FF_INITIALISE, NULL, 0 )).ivalue == FF_FAIL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Plugin '%s' unable to initialize", name);
		vpf(port);	
		if(plugin_name) free(plugin_name);
		return NULL;
	}

	int n_params = q( FF_GETNUMPARAMETERS, NULL, 0 ).ivalue;
	if( n_params == FF_FAIL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot get number of parameters for plugin %s",name);
		vpf(port);
		if(plugin_name) free(plugin_name);
		return NULL;
	}
	void *base = (void*) q;

	int n_inputs = (q(FF_GETPLUGINCAPS, (LPVOID)FF_CAP_MINIMUMINPUTFRAMES, 0)).ivalue;

	vevo_property_set( port, "handle", VEVO_ATOM_TYPE_VOIDPTR,1, &handle );
	vevo_property_set( port, "name", VEVO_ATOM_TYPE_STRING,1, &plugin_name );
	vevo_property_set( port, "base", VEVO_ATOM_TYPE_VOIDPTR, 1, &base );
	vevo_property_set( port, "instance", VEVO_ATOM_TYPE_INT, 0, NULL );
	vevo_property_set( port, "num_params", VEVO_ATOM_TYPE_INT, 1,&n_params );
	vevo_property_set( port, "num_inputs", VEVO_ATOM_TYPE_INT,1, &n_inputs );
	vevo_property_set( port, "HOST_plugin_type", VEVO_ATOM_TYPE_INT, 1, &freeframe_signature_ );
	
//	veejay_msg(VEEJAY_MSG_INFO, "FF Load: '%s' , %d params, %d inputs", plugin_name, n_params, n_inputs );

	n_params = 0; //@ FIXME: parameter initialization

	int p;
	for( p=  0; p < n_params; p ++ )
	{
	
		int type = q( FF_GETPARAMETERTYPE, (LPVOID) p, 0 ).ivalue;
		
		double min = 0;
		double max = 1.0;
		int kind = -1;

		switch( type )
		{
			case 0:
				kind = HOST_PARAM_SWITCH;
				break;
			case FF_TYPE_RED:
			case FF_TYPE_BLUE:
			case FF_TYPE_GREEN:
				kind  = HOST_PARAM_NUMBER;
				max   = 255.0;
				min   = 0.0;
				break;
			case FF_TYPE_XPOS:
				kind = HOST_PARAM_NUMBER;
			case FF_TYPE_YPOS:
				kind = HOST_PARAM_NUMBER;
			case FF_TYPE_STANDARD:
				kind = HOST_PARAM_NUMBER;
				min  = 0.0;
				max  = 100.0;
				break;

			default:
				break;	
		}
	
		if(kind == -1) {
#ifdef STRICT_CHECKING
			veejay_msg( VEEJAY_MSG_DEBUG, "Dont know about parameter type %d", type );
#endif
			continue;	
		}

		void *parameter = vpn( VEVO_FF_PARAM_PORT );
		double ivalue = (double)q( FF_GETPARAMETERDEFAULT, (LPVOID) p, 0).fvalue;
		
		char pname[32];
		q( FF_GETPARAMETERNAME, (LPVOID) p,pname );

		vevo_property_set( parameter, "default", VEVO_ATOM_TYPE_DOUBLE,1 ,&ivalue );
		vevo_property_set( parameter, "value"  , VEVO_ATOM_TYPE_DOUBLE,1, &ivalue );
		vevo_property_set( parameter, "min", VEVO_ATOM_TYPE_DOUBLE, 1, &min );
		vevo_property_set( parameter, "max", VEVO_ATOM_TYPE_DOUBLE,1, &max );
		vevo_property_set( parameter, "HOST_kind", VEVO_ATOM_TYPE_INT,1,&kind );
#ifdef STRICT_CHECKING
	
		veejay_msg( VEEJAY_MSG_DEBUG, "parameter %d is %s, defaults to %g, range is %g - %g, kind is %d",
				p, pname, ivalue, min, max, kind );
#endif

		char key[20];	
		snprintf(key,20, "p%02d", p );
		vevo_property_set( port, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &parameter );
	}
	free(plugin_name);
	return port;
}

void	freeframe_plug_retrieve_default_values( void *instance, void *fx_values )
{
	void *base = NULL;
	int error = vevo_property_get( instance, "base", 0, &base);
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 

	int n = q( FF_GETNUMPARAMETERS, NULL, 0 ).ivalue;
	int i;
	
	for( i = 0; i < n; i ++ )
	{
		char vkey[64];
		double ivalue = (double)q( FF_GETPARAMETERDEFAULT, (LPVOID) i, 0).fvalue;
		sprintf(vkey, "p%02d",i);
		vevo_property_set( fx_values, vkey, VEVO_ATOM_TYPE_DOUBLE,1, &ivalue );
	}
}

void	freeframe_plug_retrieve_current_values( void *instance, void *fx_values )
{
	void *base = NULL;
	int error = vevo_property_get( instance, "base", 0, &base);
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 

	int n = q( FF_GETNUMPARAMETERS, NULL, 0 ).ivalue;
	int i;
	
	for( i = 0; i < n; i ++ )
	{
		char vkey[64];
		double ivalue = (double)q( FF_GETPARAMETER, (LPVOID) i, 0).fvalue;
		sprintf(vkey, "p%02d",i);
		vevo_property_set( fx_values, vkey, VEVO_ATOM_TYPE_DOUBLE,1, &ivalue );
	}
}

void	freeframe_reverse_clone_parameter( void *instance, int seq, void *fx_values )
{
	void *base = NULL;
	int error = vevo_property_get( instance, "base", 0, &base);
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 

	int n = q( FF_GETNUMPARAMETERS, NULL, 0 ).ivalue;
	int i;
	
	for( i = 0; i < n; i ++ )
	{
		char vkey[64];
		double ivalue = (double)q( FF_GETPARAMETER, (LPVOID) i, 0).fvalue;
		sprintf(vkey, "p%02d",i);
		vevo_property_set( fx_values, vkey, VEVO_ATOM_TYPE_DOUBLE,1, &ivalue );
	}
}



void	freeframe_clone_parameter( void *instance, int seq, void *fx_values )
{
	// put fx_values to freeframe
	void *base = NULL;
	int error = vevo_property_get( instance, "base", 0, &base);
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 

	int n = q( FF_GETNUMPARAMETERS, NULL, 0 ).ivalue;
	int i;
	
	for( i = 0; i < n; i ++ )
	{
		char key[64];
		sprintf(key, "p%02d",i);
	
		float   		value = 0.0;
		SetParameterStruct	v;

		vevo_property_get( fx_values, key, 0, &value );
		
		v.value = value;
		v.index = i;
		
		q( FF_SETPARAMETER, &v, instance );
	}

}


int	freeframe_set_parameter_from_string( void *instance, int p, const char *str, void *fx_values )
{
	void *base = NULL;
	int error = vevo_property_get( instance, "base", 0, &base);
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif
	
	int kind = 0;
	error = vevo_property_get( instance, "HOST_kind",0,&kind );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif


	plugMainType *q = (plugMainType*) base; 
	int instance_id = 0;
	error = vevo_property_get( instance, "instance",0, &instance_id );	
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif
	int res = 0;
	char vkey[64];
	sprintf(vkey, "p%02d", p );

	switch(kind)
	{
		case HOST_PARAM_INDEX:
			res = vevo_property_from_string( fx_values,str, vkey,1, VEVO_ATOM_TYPE_INT );
			break;
		case HOST_PARAM_NUMBER:
			res = vevo_property_from_string( fx_values,str, vkey,1, VEVO_ATOM_TYPE_DOUBLE );
			break;
		case HOST_PARAM_SWITCH:
			res = vevo_property_from_string( fx_values,str, vkey,1, VEVO_ATOM_TYPE_BOOL );
			break;
		case HOST_PARAM_COORD:
			res = vevo_property_from_string( fx_values ,str, vkey,2, VEVO_ATOM_TYPE_DOUBLE );
			break;
		case HOST_PARAM_COLOR:
			res = vevo_property_from_string( fx_values,str, vkey,3, VEVO_ATOM_TYPE_DOUBLE );
			break;
		case HOST_PARAM_TEXT:
			res = vevo_property_from_string( fx_values,str, vkey,1, VEVO_ATOM_TYPE_STRING );
			break;
	}
	return res;
}



void *freeframe_plug_init( void *plugin, int w, int h )
{
	VideoInfoStruct v;
	v.frameWidth = w;
	v.frameHeight = h;
	v.orientation = 1;
	v.bitDepth = FF_CAP_V_BITS_VIDEO;

#ifdef STRICT_CHECKING
	assert( v.bitDepth == FF_CAP_32BITVIDEO ); 
#endif	

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

	int num_channels = 0;
	int i;
	vevo_property_get( plugin, "num_inputs", 0, &num_channels );

	uint8_t *space = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h * 4 * num_channels);
	error		   = vevo_property_set( plugin , "HOST_buffer", VEVO_ATOM_TYPE_VOIDPTR, 1, &space );
	if( error != VEVO_NO_ERROR ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create HOST_buffer");
		return 0;
	}
	
	generic_process_f	gpf = freeframe_plug_process;
	vevo_property_set( plugin,
			"HOST_plugin_process_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&gpf );

	generic_push_channel_f	gpu = freeframe_push_channel;
	vevo_property_set( plugin,
			"HOST_plugin_push_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&gpu );

	generic_clone_parameter_f	gcc = freeframe_clone_parameter;
	vevo_property_set( plugin,
			"HOST_plugin_param_clone_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&gcc );

	generic_reverse_clone_parameter_f grc = freeframe_reverse_clone_parameter;
	vevo_property_set( plugin,
			"HOST_plugin_param_reverse_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&grc );

	generic_default_values_f	gdb = freeframe_plug_retrieve_default_values;
	vevo_property_set( plugin,
			"HOST_plugin_defaults_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&gdb );

	generic_deinit_f		gin = freeframe_plug_deinit;
	vevo_property_set( plugin,
			"HOST_plugin_deinit_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&gin );
	
	
	return plugin;
}


void	freeframe_plug_deinit( void *plugin )
{
	void *base = NULL;
	int error = vevo_property_get( plugin, "base", 0, &base);
#ifdef STRICT_CHECING
	assert( error == VEVO_NO_ERROR );
#endif
	plugMainType *q = (plugMainType*) base; 

	int instance = 0;
	error = vevo_property_get( plugin, "instance", 0, &instance );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	if(! instance )
		return;

	q( FF_DEINSTANTIATE, NULL, instance );

	uint8_t *space = NULL;
	error = vevo_property_get( plugin, "HOST_buffer",0,&space);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	if( error == VEVO_NO_ERROR ) {
		free(space);
	}
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

void	freeframe_push_channel( void *instance, const char *key,int n, VJFrame *frame )
{
	char inkey[10];
	int i;
	void *chan = NULL;
	uint8_t *space = NULL;	
	int error;
	if(key[0] == 'o' )
	{
		vevo_property_set( instance, "HOST_output", VEVO_ATOM_TYPE_VOIDPTR,1,&frame );
	}
	else
	{
		error = vevo_property_get( instance, "HOST_buffer",0,&space );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		uint32_t chan_offset = frame->width * frame->height * 4 * n;
			
		VJFrame *dst1 = yuv_rgb_template( space + chan_offset, frame->width,frame->height, PIX_FMT_RGB32 );

		if( yuv_conv_ == NULL ) {
			sws_template templ;
			templ.flags = 1;
			yuv_conv_ = yuv_init_swscaler( frame,dst1, &templ, yuv_sws_get_cpu_flags() );	
		}	

		yuv_convert_and_scale_rgb( yuv_conv_, frame, dst1 );

		free(dst1);
	}
	
}


int	freeframe_plug_process( void *plugin, double timecode )
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
	uint8_t *space = NULL;
	error = vevo_property_get( plugin, "HOST_buffer",0,&space );
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif
	
	q( FF_PROCESSFRAME, space, instance );

	VJFrame *output_frame = NULL;
	
	error = vevo_property_get( plugin, "HOST_output", 0,&output_frame );
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif
	
	VJFrame *src1 = yuv_rgb_template( space, output_frame->width, output_frame->height, PIX_FMT_RGB32 );

//	yuv_convert_any_ac( src1, output_frame, src1->format, output_frame->format );

	if( rgb_conv_ == NULL ) {
		sws_template templ;
		templ.flags = 1;
		rgb_conv_ = yuv_init_swscaler( src1,output_frame, &templ, yuv_sws_get_cpu_flags() );	
	}	

	yuv_convert_and_scale_from_rgb( rgb_conv_, src1, output_frame );

	free(src1);

	return 1;
}


void	freeframe_plug_param_f( void *plugin, int seq_no, void *dargs )
{
	int instance = 0;
	int error = vevo_property_get( plugin, "instance",0, &instance );	
#ifdef STRICT_CHECING
	assert( error == LIVIDO_NO_ERROR );
#endif


}
