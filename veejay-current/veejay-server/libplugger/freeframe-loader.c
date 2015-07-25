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
#include <stdlib.h>
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
#include <libavutil/pixfmt.h>
#include <libplugger/portdef.h>
#define LINUX 1 
#include <libplugger/specs/FreeFrame.h>
#define V_BITS 24
#include <libplugger/freeframe-loader.h>
#include <libvje/vje.h>

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
static  VJFrame *freeframe_frame_[4] = { NULL, NULL, NULL, NULL};
//static  VJFrame *freeframe_outframe_ = NULL;
static	uint8_t *freeframe_space_ = NULL;
static	int	freeframe_ready_ = 0;
static void *rgb_conv_ = NULL;
static void *yuv_conv_ = NULL;
#define MAX_IN_CHANNELS 1
/*#elif (V_BITS == 24)
#define FF_CAP_V_BITS_VIDEO     FF_CAP_24BITVIDEO
#else // V_BITS = 16
#define FF_CAP_V_BITS_VIDEO     FF_CAP_16BITVIDEO
#endif*/

static void 		freeframe_copy_parameters( void *srcPort, void *dst, int n_params );


void	freeframe_destroy( ) {
	int i;
	yuv_free_swscaler( rgb_conv_ );
	yuv_free_swscaler( yuv_conv_ );
	
	free( freeframe_space_ );
//	free( freeframe_outframe_ );
	for( i = 0; i < MAX_IN_CHANNELS; i ++ ) 
		free( freeframe_frame_[i] );
}

static	void	freeframe_init( VJFrame *frame )
{
	if( freeframe_ready_ == 0 ) {
		int n = MAX_IN_CHANNELS;
		int w = frame->width;
		int h = frame->height;
		int i;
		freeframe_space_ = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h * 4 * n);
		
		veejay_memset( freeframe_space_, 0 , sizeof( w * h * 4 * n ) );
		for( i = 0; i < n ; i ++ ) {
			int offs = w * h * 4 * i;
			freeframe_frame_[i] = yuv_rgb_template( 
					freeframe_space_ + offs, w, h, PIX_FMT_RGB32 );
			
		}	

		sws_template templ;
		templ.flags = 1;
		yuv_conv_ = yuv_init_swscaler( frame,freeframe_frame_[0], &templ, yuv_sws_get_cpu_flags() );
		rgb_conv_ = yuv_init_swscaler( freeframe_frame_[0],frame, &templ, yuv_sws_get_cpu_flags() );		
		
		freeframe_ready_ = 1;	
	}
}

void*	deal_with_ff( void *handle, char *name, int w, int h )
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

	char plugname[512];
	char key[32];
	snprintf(plugname,sizeof(plugname), "FreeFrame %s", pis->pluginName  );

	plugin_name = vj_strdup( plugname ); 
	
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
	int m_inputs = (q(FF_GETPLUGINCAPS, (LPVOID)FF_CAP_MAXIMUMINPUTFRAMES, 0)).ivalue;

	if( n_inputs == 1 && m_inputs > 1 ) {
		n_inputs = 2; 
	}

	int n_outputs = 1;

	vevo_property_set( port, "handle", VEVO_ATOM_TYPE_VOIDPTR,1, &handle );
	vevo_property_set( port, "name", VEVO_ATOM_TYPE_STRING,1, &plugin_name );
	vevo_property_set( port, "base", VEVO_ATOM_TYPE_VOIDPTR, 1, &base );
	vevo_property_set( port, "instance", VEVO_ATOM_TYPE_INT, 0, NULL );
	vevo_property_set( port, "num_params", VEVO_ATOM_TYPE_INT, 1,&n_params );
	vevo_property_set( port, "num_inputs", VEVO_ATOM_TYPE_INT,1, &n_inputs );
	vevo_property_set( port, "max_inputs", VEVO_ATOM_TYPE_INT,1,&m_inputs );
	vevo_property_set( port, "num_outputs", VEVO_ATOM_TYPE_INT,1,&n_outputs );
	vevo_property_set( port, "HOST_plugin_type", VEVO_ATOM_TYPE_INT, 1, &freeframe_signature_ );
	
	int p;
	for( p=  0; p < n_params; p ++ )
	{
		unsigned int type = q( FF_GETPARAMETERTYPE, (LPVOID) p, 0 ).ivalue;
		char *pname = q( FF_GETPARAMETERNAME, (LPVOID) p, 0 ).svalue;

		plugMainUnion v = q(FF_GETPARAMETERDEFAULT, (LPVOID) p, 0 );

		float defval    = v.fvalue;

		int min = 0;
		int max = 100;
		int kind = -1;
		int dvalue = (int) ( 100.0f * defval );

		switch( type )
		{
			case 0:
				kind = HOST_PARAM_SWITCH;
				max  = 1;
				break;
			case FF_TYPE_RED:
			case FF_TYPE_BLUE:
			case FF_TYPE_GREEN:
				kind  = HOST_PARAM_NUMBER;
				max   = 255;
				min   = 0;
				dvalue = (int) ( max * defval );
				break;
			case FF_TYPE_XPOS:
				kind = HOST_PARAM_NUMBER;
				min  = 0;
				max  = w;
				dvalue = 0 + (int)(w * defval);

				break;
			case FF_TYPE_YPOS:
				kind = HOST_PARAM_NUMBER;
				min  = 0;
				max = h;
				dvalue = 0 + (int)(h * defval);
				break;
			case FF_TYPE_STANDARD:
				kind = HOST_PARAM_NUMBER;
				break;

			default:
				veejay_msg(VEEJAY_MSG_WARNING, "\tParameter type %d unknown (%s)", type, pname );
				continue;
				break;	
		}
	
		void *parameter = vpn( VEVO_FF_PARAM_PORT );
		
		vevo_property_set( parameter, "min", VEVO_ATOM_TYPE_INT, 1, &min );
		vevo_property_set( parameter, "max", VEVO_ATOM_TYPE_INT,1, &max );
		vevo_property_set( parameter, "default",VEVO_ATOM_TYPE_INT,1,&dvalue);
		vevo_property_set( parameter, "HOST_kind", VEVO_ATOM_TYPE_INT,1,&kind );
		snprintf(key,sizeof(key), "p%02d", p );
		vevo_property_set( port, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &parameter );
	}

	free(plugin_name);

	return port;
}

void	freeframe_plug_retrieve_default_values( void *instance, void *fx_values )
{
	void *base = NULL;
	if( vevo_property_get( instance, "base", 0, &base) != VEVO_NO_ERROR )
		return;

	plugMainType *q = (plugMainType*) base; 

	int n = 0;
	int i;
	
	if( vevo_property_get( instance, "num_params",0,&n) != VEVO_NO_ERROR )
		return;

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
	int n = 0;
	int i;
	
	if( vevo_property_get( instance, "num_params",0,&n) != VEVO_NO_ERROR )
		return;

	for( i = 0; i < n; i ++ )
	{
		char vkey[16];
		sprintf(vkey, "p%02d",i);
		void *param = NULL;
		if( vevo_property_get( instance ,vkey,0,&param ) != VEVO_NO_ERROR)
			continue;
		double ivalue = 0.0;
		if( vevo_property_get( param,"value",0,&ivalue) != VEVO_NO_ERROR )
			continue;
		vevo_property_set( fx_values, vkey, VEVO_ATOM_TYPE_DOUBLE,1, &ivalue );
	}
}

void	freeframe_reverse_clone_parameter( void *instance, int seq, void *fx_values )
{
	int n = 0;
	int i;
	
	if( vevo_property_get( instance, "num_params",0,&n) != VEVO_NO_ERROR )
		return;

	for( i = 0; i < n; i ++ )
	{
		char vkey[16];
		sprintf(vkey, "p%02d",i);
		void *param = NULL;
		if( vevo_property_get( instance ,vkey,0,&param ) != VEVO_NO_ERROR )
			continue;
		double ivalue = 0.0;
		if( vevo_property_get( param,"value",0,&ivalue) != VEVO_NO_ERROR )
			continue;
		vevo_property_set( fx_values, vkey, VEVO_ATOM_TYPE_DOUBLE,1, &ivalue );
	}
}



void	freeframe_clone_parameter( void *instance, int seq, void *fx_values )
{
	// put fx_values to freeframe
	void *base = NULL;
	if( vevo_property_get( instance, "base", 0, &base) != VEVO_NO_ERROR )
		return;

	plugMainType *q = (plugMainType*) base; 

	int n = 0;
	int i;
	DWORD inp = 0;
	if( vevo_property_get( instance, "instance", 0, &inp ) != VEVO_NO_ERROR )
		return;

	if( vevo_property_get( instance, "num_params",0,&n) != VEVO_NO_ERROR )
		return;

	for( i = 0; i < n; i ++ )
	{
		char key[64];
		sprintf(key,"p%02d",i);
	
		double   		value = 0.0;
		SetParameterStruct	v;

		vevo_property_get( fx_values, key, 0, &value );
		
		v.value =  (float) value;
		v.index = i;
		
		q( FF_SETPARAMETER, &v, inp );
	}
}


void *freeframe_plug_init( void *plugin, int w, int h )
{
	void *pluginstance;
	VideoInfoStruct v;
	v.frameWidth = w;
	v.frameHeight = h;
	v.orientation = 1;
	v.bitDepth = FF_CAP_V_BITS_VIDEO;

	void *base = NULL;
	if( vevo_property_get( plugin, "base", 0, &base) != VEVO_NO_ERROR )
		return 0;

	plugMainType *q = (plugMainType*) base; 

	DWORD instance = q( FF_INSTANTIATE, &v, 0).ivalue;
	if( instance == FF_FAIL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize plugin");
		return NULL;
	}

	pluginstance = vpn( VEVO_ANONYMOUS_PORT );

	vevo_property_set( pluginstance, "base", VEVO_ATOM_TYPE_VOIDPTR, 1, &base );
	vevo_property_set( pluginstance, "instance", VEVO_ATOM_TYPE_INT, 1, &instance );

	int num_channels = 0;
	
	vevo_property_get( plugin, "num_inputs", 0, &num_channels );

	if( num_channels > 4 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "too many input channels: %d", num_channels);
		return NULL;
	}

	generic_process_f	gpf = (generic_process_f) freeframe_plug_process;
	vevo_property_set( pluginstance,
			"HOST_plugin_process_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&gpf );

	generic_push_channel_f	gpu = freeframe_push_channel;
	vevo_property_set( pluginstance,
			"HOST_plugin_push_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&gpu );

	generic_clone_parameter_f	gcc = freeframe_clone_parameter;
	vevo_property_set( pluginstance,
			"HOST_plugin_param_clone_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&gcc );

	generic_reverse_clone_parameter_f grc = freeframe_reverse_clone_parameter;
	vevo_property_set( pluginstance,
			"HOST_plugin_param_reverse_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&grc );

	generic_default_values_f	gdb = freeframe_plug_retrieve_default_values;
	vevo_property_set( pluginstance,
			"HOST_plugin_defaults_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&gdb );

	generic_deinit_f		gin = freeframe_plug_deinit;
	vevo_property_set( pluginstance,
			"HOST_plugin_deinit_f",
			VEVO_ATOM_TYPE_VOIDPTR,
			1,
			&gin );

	generic_push_parameter_f gpp = freeframe_plug_param_f;
	vevo_property_set( pluginstance, "HOST_plugin_param_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpp);

	int n_params = 0;
	vevo_property_get( plugin, "num_params",0,&n_params );

	if( n_params > 0 ) {
		int p;
		for( p=  0; p < n_params; p ++ )
		{
			void *parameter = NULL;
			char key[20];	
			sprintf(key,"p%02d", p );
		
			if( vevo_property_get( plugin, key, 0, &parameter ) != VEVO_NO_ERROR)
				continue;

			//float value = (float)q( FF_GETPARAMETERDEFAULT, (LPVOID) p, 0).fvalue;
				
			float value = 0.00f + (float) ( 1.0 * (rand()/(RAND_MAX+1.0f)));
			double dval = (double) value;

			vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_DOUBLE,1,&dval);

			SetParameterStruct sps;
			sps.index = p;
			sps.value  = value;

			q( FF_SETPARAMETER, &sps, instance );


			veejay_msg(VEEJAY_MSG_INFO, " feed parameter %d with random value %2.2f", p, value );
		}
		freeframe_copy_parameters( plugin, pluginstance, n_params );
	}

	return pluginstance;
}


void	freeframe_plug_deinit( void *pluginstance )
{
	void *base = NULL;
	if( vevo_property_get( pluginstance, "base", 0, &base) != VEVO_NO_ERROR )
		return;

	plugMainType *q = (plugMainType*) base; 

	DWORD instance = 0;
	if( vevo_property_get( pluginstance, "instance", 0, &instance ) != VEVO_NO_ERROR )
		return;

	q( FF_DEINSTANTIATE, NULL, instance );

	vpf( pluginstance );
}

void	freeframe_plug_free( void *plugin )
{
	
	void *base = NULL;
	if( vevo_property_get( plugin, "base", 0, &base) != VEVO_NO_ERROR )
		return;

	plugMainType *q = (plugMainType*) base;
	
	q( FF_DEINITIALISE, NULL, 0 );
}


void	freeframe_push_channel( void *instance, int n,int dir, VJFrame *frame )
{
	freeframe_init( frame );

	if(dir == 1){
		vevo_property_set( instance, "HOST_output", VEVO_ATOM_TYPE_VOIDPTR,1,&frame );
	} else  {
		yuv_convert_and_scale_rgb( yuv_conv_, frame, freeframe_frame_[ n ] );
	}
}


int	freeframe_plug_process( void *plugin, double timecode )
{
	void *base = NULL;
	if( vevo_property_get( plugin, "base", 0, &base) != VEVO_NO_ERROR )
		return 0;

	plugMainType *q = (plugMainType*) base; 
	DWORD instance = 0;
	if( vevo_property_get( plugin, "instance",0, &instance ) != VEVO_NO_ERROR )
		return 0;
	
	q( FF_PROCESSFRAME, freeframe_space_, instance );

	VJFrame *output_frame = NULL;
	
	if( vevo_property_get( plugin, "HOST_output", 0,&output_frame ) != VEVO_NO_ERROR )
		return 0;
	
	//@ output frame in [0]
	yuv_convert_and_scale_from_rgb( rgb_conv_, freeframe_frame_[0], output_frame );

	return 1;
}


void	freeframe_plug_param_f( void *plugin, int seq_no, void *dargs )
{
	char pkey[32];
	snprintf(pkey, sizeof(pkey), "p%02d",seq_no);
	// fetch parameter port
	void *port = NULL;
	int error = vevo_property_get( plugin, pkey, 0, &port );
	if( error != VEVO_NO_ERROR ) { 
		return;
	}

	int *args = (int*) dargs;
	int in_val = args[0];
	int max    = 0;
	error = vevo_property_get( port, "max",0,&max );

	float  v = ((float) in_val / (float) max );

	SetParameterStruct sps;
	sps.index = seq_no;
	sps.value  = v;

	void *base = NULL;
	if( vevo_property_get( plugin, "base", 0, &base) != VEVO_NO_ERROR )
		return;

	DWORD instance = 0;
	if( vevo_property_get( plugin, "instance", 0, &instance ) != VEVO_NO_ERROR )
		return;

	plugMainType *q = (plugMainType*) base; 

	q( FF_SETPARAMETER, &sps, instance );
}



static void 		freeframe_copy_parameters( void *srcPort, void *dst, int n_params )
{
	int p;
	for( p = 0; p < n_params; p ++ ) {
		void *src = NULL;
		char pname[32];
		sprintf(pname, "p%02d", p );
		if( vevo_property_get( srcPort, pname, 0, &src ) != VEVO_NO_ERROR )	
			continue;

		char **keys = vevo_list_properties(src);
		if( keys == NULL )
			continue;
		
		void *dport = vpn( VEVO_ANONYMOUS_PORT );
		int i;
		for( i = 0; keys[i] != NULL ; i ++ ) {
			vevo_property_clone( src, dport, keys[i] ,keys[i] );
			free(keys[i]);	
		}	
		free(keys);
		vevo_property_set( dst,pname, VEVO_ATOM_TYPE_PORTPTR, 1,&dport );
	}
}

