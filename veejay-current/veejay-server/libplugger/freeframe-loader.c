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

static int bug_workarround1 = 0;

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
	sprintf(plugname, "FreeFrame %s", pis->pluginName  );

	plugin_name = strdup( plugname ); 
	
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

	int n_outputs = 0;

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
	int randp = 0;
	for( p=  0; p < n_params; p ++ )
	{
		int type = q( FF_GETPARAMETERTYPE, (LPVOID) p, 0 ).ivalue;
		char *pname = q( FF_GETPARAMETERNAME, (LPVOID) p, 0 ).svalue;
		
		//@ for some reason, FF_GETPARAMETERDEFAULT .fvalue returns invalid values. 
		randp = 0;
		int weirdo1 = q(FF_GETPARAMETERDEFAULT, (LPVOID) p, 0).ivalue;
		if( weirdo1 == FF_FAIL || weirdo1 == 139 ) { //@ magic value seen 
			randp = 1; 
			if(!bug_workarround1) bug_workarround1 = 1;
		} 
		float weirdo2 = q(FF_GETPARAMETERDEFAULT,(LPVOID) p,0).fvalue;
		if( weirdo2 < 0.0f ) {
			randp = 1;
			if(!bug_workarround1) bug_workarround1 = 1;
		}

		// q(FF_GETPARAMTERDEFAULT,p,0).svalue gives garbage. FIXME

		int min = 0;
		int max = 100;
		int kind = -1;
		int dvalue = 0;

		if(!randp)  //@ scale plugin's default to vje integer scale
			dvalue = (int) ( 100.0f * weirdo2);

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
				if(randp)
					dvalue = 0 + (int)(255.0 * (rand() / (RAND_MAX + 1.0)));
				break;
			case FF_TYPE_XPOS:
				kind = HOST_PARAM_NUMBER;
				min  = 0;
				max  = w;
				if(randp)
					dvalue = 0 + (int)(w * (rand() / (RAND_MAX + 1.0)));

				break;
			case FF_TYPE_YPOS:
				kind = HOST_PARAM_NUMBER;
				min  = 0;
				max = h;
				if(randp)
					dvalue = 0 + (int)(h * (rand() / (RAND_MAX + 1.0)));
				break;
			case FF_TYPE_STANDARD:
				kind = HOST_PARAM_NUMBER;
				min  = 0;
				max  = 100.0;
				if(randp)
					dvalue = 0 + (int)(10.0 * (rand() / (RAND_MAX + 1.0))); //@ initialize with low value
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
#ifdef STRICT_CHECKING
		if(randp)	
			veejay_msg(VEEJAY_MSG_WARNING, "Randomized default value to %d for '%s'", dvalue, pname );
#endif
		char key[20];	
		snprintf(key,20, "p%02d", p );
		vevo_property_set( port, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &parameter );
	}

	if( bug_workarround1==1 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "FreeFrame: garbage in value returning from FF_GETPARAMETERDEFAULT.");
		veejay_msg(VEEJAY_MSG_WARNING, "FreeFrame: apply workarround and initialize parameters with random values.");
		bug_workarround1++;
	}

	if(randp) {
		veejay_msg(VEEJAY_MSG_WARNING , "Randomized parameter values for '%s'", plugin_name);
	}

	free(plugin_name);

	return port;
}

void	freeframe_plug_retrieve_default_values( void *instance, void *fx_values )
{
	void *base = NULL;
	int error = vevo_property_get( instance, "base", 0, &base);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 

	int n = 0;
	int i;
	
	error = vevo_property_get( instance, "num_params",0,&n);

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
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 

	int n = 0;
	int i;
	
	error = vevo_property_get( instance, "num_params",0,&n);

	for( i = 0; i < n; i ++ )
	{
		char vkey[16];
		sprintf(vkey, "p%02d",i);
		void *param = NULL;
		error = vevo_property_get( instance ,vkey,0,&param );
		if( error != VEVO_NO_ERROR )
			continue;
		double ivalue = 0.0;
		error = vevo_property_get( param,"value",0,&ivalue);
		if( error != VEVO_NO_ERROR )
			continue;
		vevo_property_set( fx_values, vkey, VEVO_ATOM_TYPE_DOUBLE,1, &ivalue );
	}
}

void	freeframe_reverse_clone_parameter( void *instance, int seq, void *fx_values )
{
	void *base = NULL;
	int error = vevo_property_get( instance, "base", 0, &base);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 

	int n = 0;
	int i;
	error = vevo_property_get( instance, "num_params",0,&n);

	for( i = 0; i < n; i ++ )
	{
		char vkey[16];
		sprintf(vkey, "p%02d",i);
		void *param = NULL;
		error = vevo_property_get( instance ,vkey,0,&param );
		if( error != VEVO_NO_ERROR )
			continue;
		double ivalue = 0.0;
		error = vevo_property_get( param,"value",0,&ivalue);
		if( error != VEVO_NO_ERROR )
			continue;
		vevo_property_set( fx_values, vkey, VEVO_ATOM_TYPE_DOUBLE,1, &ivalue );
	}
}



void	freeframe_clone_parameter( void *instance, int seq, void *fx_values )
{
	// put fx_values to freeframe
	void *base = NULL;
	int error = vevo_property_get( instance, "base", 0, &base);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
	assert(0); //@ this function must be dropped
#endif

	plugMainType *q = (plugMainType*) base; 

	int n = 0;
	int i;
	
	error = vevo_property_get( instance, "num_params",0,&n);

	for( i = 0; i < n; i ++ )
	{
		char key[64];
		sprintf(key, "p%02d",i);
	
		double   		value = 0.0;
		SetParameterStruct	v;

		vevo_property_get( fx_values, key, 0, &value );
		
		v.value =  (float) value;
		v.index = i;
		
		q( FF_SETPARAMETER, &v, instance );
	}
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
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
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

	generic_push_parameter_f gpp = freeframe_plug_param_f;
	vevo_property_set( plugin, "HOST_plugin_param_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpp);

	int n_params = 0;
	error = vevo_property_get( plugin, "num_params",0,&n_params );

	if( n_params > 0 ) {
		int p;
		for( p=  0; p < n_params; p ++ )
		{
			void *parameter = NULL;
			char key[20];	
			snprintf(key,20, "p%02d", p );
		
			error = vevo_property_get( plugin, key, 0, &parameter );
			if( error != VEVO_NO_ERROR )
				continue;

			//this returns garbage:
			//float value = (float)q( FF_GETPARAMETERDEFAULT, (LPVOID) p, 0).fvalue;
				
/*
			float value = 0.00f + (float) ( 1.0 * (rand()/(RAND_MAX+1.0f)));
			double dval = (double) value;

			error = vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_DOUBLE,1,&dval);
	
			SetParameterStruct sps;
			sps.index = p;
			sps.value  = value;

			q( FF_SETPARAMETER, &sps, instance );


			veejay_msg(VEEJAY_MSG_INFO, " feed parameter %d with random value %2.2f", p, value );*/
		}
	}

	return plugin;
}


void	freeframe_plug_deinit( void *plugin )
{
	void *base = NULL;
	int error = vevo_property_get( plugin, "base", 0, &base);
#ifdef STRICT_CHECKING
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
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	plugMainType *q = (plugMainType*) base; 
	q( FF_DEINITIALISE, NULL, 0 );
}

void	freeframe_push_channel( void *instance, int n,int dir, VJFrame *frame )
{
	char inkey[10];
	int i;
	void *chan = NULL;
	uint8_t *space = NULL;	
	int error;
	if(dir == 1)
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
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	plugMainType *q = (plugMainType*) base; 
	int instance = 0;
	error = vevo_property_get( plugin, "instance",0, &instance );	
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	uint8_t *space = NULL;
	error = vevo_property_get( plugin, "HOST_buffer",0,&space );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	
	q( FF_PROCESSFRAME, space, instance );

	VJFrame *output_frame = NULL;
	
	error = vevo_property_get( plugin, "HOST_output", 0,&output_frame );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	
	VJFrame *src1 = yuv_rgb_template( space, output_frame->width, output_frame->height, PIX_FMT_RGB32 );

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
	char pkey[32];
	snprintf(pkey, sizeof(pkey), "p%02d",seq_no);
	// fetch parameter port
	void *port = NULL;
	int error = vevo_property_get( plugin, pkey, 0, &port );
	if( error != VEVO_NO_ERROR ) 
		return;

	int *args = (int*) dargs;
	int in_val = args[0];
	int max    = 0;
	error = vevo_property_get( port, "max",0,&max );

	float  v = ((float) in_val / (float) max );

	SetParameterStruct sps;
	sps.index = seq_no;
	sps.value  = v;

	void *base = NULL;
	error = vevo_property_get( plugin, "base", 0, &base);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	int instance = 0;
	error = vevo_property_get( plugin, "instance", 0, &instance );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	plugMainType *q = (plugMainType*) base; 

	q( FF_SETPARAMETER, &sps, instance );
}
