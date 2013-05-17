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
 *
 *
 */
#include <config.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <libhash/hash.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvje/vje.h>
#include <libvevo/libvevo.h>
#include <libplugger/defs.h>
#include <libyuv/yuvconv.h>
#include <libavutil/pixfmt.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <libplugger/portdef.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
/** \defgroup freior Freior Host
 *
 * This module provides a Frei0r Host
 */
#include <libplugger/specs/frei0r.h>

static int frei0r_signature_ = VEVO_PLUG_FR;

typedef f0r_instance_t (*f0r_construct_f)(unsigned int width, unsigned int height);

typedef void (*f0r_destruct_f)(f0r_instance_t instance);

typedef void (*f0r_deinit_f)(void);

typedef int (*f0r_init_f)(void);

typedef void (*f0r_get_plugin_info_f)(f0r_plugin_info_t *info);

typedef void (*f0r_get_param_info_f)(f0r_param_info_t *info, int param_index);

typedef void (*f0r_update_f)(f0r_instance_t instance, double time, const uint32_t *inframe, uint32_t *outframe);

typedef void (*f0r_update2_f)(f0r_instance_t instance, double time, const uint32_t *inframe1, const uint32_t *inframe2, const uint32_t *inframe3, uint32_t *outframe);

typedef void (*f0r_set_param_value_f)(f0r_instance_t *instance, f0r_param_t *param, int param_index);
void	frei0r_plug_param_f( void *port, int seq_no, void *value );
int	frei0r_push_frame_f( void *plugin, int dir, int seqno, VJFrame *in );
int	frei0r_process_frame_f( void *plugin );
int	frei0r_get_param_f( void *port, void *dst );

typedef struct
{
	uint8_t *buf;
	VJFrame *in[4];
	VJFrame *out;
	VJFrame *dst;
	VJFrame	*last;
} fr0_conv_t;


static	void	*in_scaler__ = NULL;
static 	void	*out_scaler__ = NULL;

int	frei0r_get_param_f( void *port, void *dst )
{
	void *instance = NULL;
	int  err = vevo_property_get(port, "frei0r",0,&instance );
	if( err != VEVO_NO_ERROR )
		return 0;

	int np = 0;
	err = vevo_property_get(port, "num_params",0,&np);
	if( err != VEVO_NO_ERROR || np == 0 )
		return 0;

	int i;
	for( i = 0;i < np; i ++ ) {
		char key[20];
		snprintf(key,sizeof(key),"p%02d",i );
		void *param = NULL;
		err = vevo_property_get(port,key,0,&param);
		if( err != VEVO_NO_ERROR) continue;	
		
		vevo_property_clone( param, dst, "value", "value");
	}
	return 1;
}

int	frei0r_push_frame_f( void *plugin, int seqno, int dir, VJFrame *in )
{
	void *instance = NULL;
	int  err = vevo_property_get(plugin, "frei0r",0,&instance );
	if( err != VEVO_NO_ERROR )
		return 0;

	int i = 0;

	fr0_conv_t *fr = NULL;
	err = vevo_property_get(plugin, "HOST_conv",0,&fr);
	if( err != VEVO_NO_ERROR )
		return 0;

	if( dir == 1 ) { //@ output channel push
		if( seqno == 0 ) 	
			fr->last = in; //@ 1 output channel 
	

		return 1;	
	}
	else if ( dir == 0  ) {
		if( seqno < 0 || seqno > 1 ) {
			return 0;
		}
		yuv_convert_and_scale_rgb( in_scaler__, in, fr->in[seqno]); //@ yuv -> rgb
		if(seqno == 0)
			fr->last = in;
	}

	return 1;
}

static	int	init_param_fr( void *port, int p,f0r_param_info_t *info, int hint, int pcount)
{
	void *parameter = vpn( VEVO_FR_PARAM_PORT );
	int min[] = { 0,0,0,0};
	int max[] = { 100,100,100,100 };
	int dv[] = { 50,50,50,50};
	int n_values = 0;

	switch(hint)
	{
		case F0R_PARAM_DOUBLE:
			n_values = 1;
			break;
		case F0R_PARAM_BOOL:
			max[0] = 1;
			dv[0] = 0;
			n_values = 1;
			break;
		case F0R_PARAM_COLOR:
			n_values = 3;
			break;
		case F0R_PARAM_POSITION:
			n_values = 2;
			break;
		default:
			return 0;
			break;
	}

	if( n_values > 0 )
	{
		int values[n_values];
		int k;
		for( k = 0; k < n_values; k ++ ) 
			values[k] = dv[k];
	
		vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_INT, n_values, &values );	
	}

	vevo_property_set( parameter, "name", VEVO_ATOM_TYPE_STRING,1,&(info->name));
	vevo_property_set( parameter, "min", VEVO_ATOM_TYPE_INT,n_values, (n_values==1? &(min[0]): min) );
	vevo_property_set( parameter, "max", VEVO_ATOM_TYPE_INT,n_values, (n_values==1? &(max[0]):max) );
	vevo_property_set( parameter, "default", VEVO_ATOM_TYPE_INT,n_values, (n_values==1?&dv[0]: dv) );
	vevo_property_set( parameter, "hint", VEVO_ATOM_TYPE_INT,1, &hint );
	vevo_property_set( parameter, "seqno", VEVO_ATOM_TYPE_INT,1,&pcount);

	char key[20];	
	snprintf(key,20, "p%02d", p );
	vevo_property_set( port, key, VEVO_ATOM_TYPE_PORTPTR, 1, &parameter );

#ifdef STRICT_CHECKING
	veejay_msg(VEEJAY_MSG_DEBUG, " -> Parameter %d '%s' %d - %d ", pcount,info->name,min[0],max[0] );
#endif

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
		vpf( port );
		return NULL;
	}

	f0r_deinit_f	f0r_deinit	= dlsym( handle, "f0r_deinit" );
	if( f0r_deinit == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBorked frei0r plugin '%s': %s", name, dlerror());
		vpf( port );
		return NULL;
	}

	f0r_get_plugin_info_f	f0r_info = dlsym( handle, "f0r_get_plugin_info");
	if( f0r_info == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBorked frei0r plugin '%s': %s", name, dlerror());
		vpf( port );
		return NULL;
	}

	f0r_get_param_info_f	f0r_param= dlsym( handle, "f0r_get_param_info" );
	if( f0r_param == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBorked frei0r plugin '%s': %s", name, dlerror());
		vpf( port );
		return NULL;
	}

	void	*f0r_construct	= dlsym( handle, "f0r_construct" );
	void	*f0r_destruct	= dlsym( handle, "f0r_destruct" );
	void	*processf	= dlsym( handle, "f0r_update" );
	void	*processm	= dlsym( handle, "f0r_update2" );
	void	*set_params	= dlsym( handle, "f0r_set_param_value" );

	vevo_property_set( port, "handle", VEVO_ATOM_TYPE_VOIDPTR,1, &handle );
	vevo_property_set( port, "init", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_init );
	vevo_property_set( port, "deinit", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_deinit );
	vevo_property_set( port, "info", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_info );
	
	vevo_property_set( port, "parameters", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_param );
	
	vevo_property_set( port, "construct", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_construct );
	
	if( f0r_destruct != NULL )
		vevo_property_set( port, "destruct", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_destruct );
	
	if( processf != NULL)
		vevo_property_set( port, "process", VEVO_ATOM_TYPE_VOIDPTR, 1, &processf);
	
	if( processm != NULL )
		vevo_property_set( port, "process_mix", VEVO_ATOM_TYPE_VOIDPTR, 1, &processm);
	if( set_params != NULL )
		vevo_property_set( port, "set_params", VEVO_ATOM_TYPE_VOIDPTR,1,&set_params);	

    	f0r_plugin_info_t finfo;
	f0r_param_info_t pinfo;

	memset( &finfo,0,sizeof(f0r_plugin_info_t));
	memset( &pinfo,0,sizeof(f0r_param_info_t));


	if( (*f0r_init)() == 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tBorked frei0r plugin '%s': ", name);
		vpf( port );
		return NULL;
	}

	(*f0r_info)(&finfo);

	if( finfo.frei0r_version != FREI0R_MAJOR_VERSION )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tI am using frei0r version %d but plugin requires %d",
				FREI0R_MAJOR_VERSION, finfo.frei0r_version );
		(*f0r_deinit)();	
		vpf(port);
		return NULL;	
	}
	int extra = 0;
	int n_inputs = 0;
	int n_outputs = 0;
	if( finfo.plugin_type == F0R_PLUGIN_TYPE_MIXER2 ) {
		extra = 1;
		n_inputs = 2;
		if( processm == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Supposed to be mixer plugin (2 sources) but no f0r_update2");
			(*f0r_deinit)();
			vpf(port);
			return NULL;
		}
	} else if ( finfo.plugin_type == F0R_PLUGIN_TYPE_FILTER ) {
		n_inputs = 1;
		if( processf == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Supposed to be filter plugin (1 input source) but no f0r_update");
			(*f0r_deinit)();
			vpf(port);
			return NULL;
		}
	} else if ( finfo.plugin_type == F0R_PLUGIN_TYPE_SOURCE ) {
		n_inputs = 0;
		n_outputs = 1;
		if( processf == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Supposed to be generator plugin (1 output source) but no f0r_update");
			(*f0r_deinit)();
			vpf(port);
			return NULL;
		}
	} else {
		veejay_msg(VEEJAY_MSG_ERROR, "Some strange frei0r plugin, would not be able to eat that.");
		(*f0r_deinit)();
		vpf(port);
		return NULL;
	}


	//@ cheap check for insane frei0r plugins
	if( (finfo.plugin_type == F0R_PLUGIN_TYPE_FILTER && processf == NULL) ||
	     (finfo.plugin_type == F0R_PLUGIN_TYPE_MIXER2 && processm == NULL) ) {
		veejay_msg(0, "Frei0r plugin %s behaves badly",name);
		(*f0r_deinit)();
		vpf(port);
		return NULL;
	}

	//@ bang, if plug behaves badly. veejay crashes. is it blacklisted?
	veejay_msg(VEEJAY_MSG_DEBUG, "Frei0r plugin '%s' version %d.%d",
			name, finfo.major_version, finfo.minor_version );
	//@FIXME: blacklist
	
	int n_params = finfo.num_params;
	int r_params = 0;
	int p = 0;

	if( set_params == NULL )
		n_params = 0; // lol

	for ( p = 0; p < n_params; p ++ )
	{
		(*f0r_param)(&pinfo,p);
	
		int fr_params = init_param_fr(port,p,&pinfo,pinfo.type,p);


		r_params += fr_params;
	}



	if( r_params > 8 ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Maximum parameter count reached, allowing %d/%d parameters.",
				8,r_params);
		r_params = 8;
	}


	char tmp_name[1024];
	memset( tmp_name,0,sizeof(tmp_name));
	int len = (finfo.name == NULL ? 0 : strlen(finfo.name));
	if( len > 1023 )
		len = 1023;
	strncpy( tmp_name, finfo.name, len );

	char *plug_name = strdup(tmp_name);

	vevo_property_set( port, "num_params", VEVO_ATOM_TYPE_INT, 1, &r_params );
	vevo_property_set( port, "name", VEVO_ATOM_TYPE_STRING,1, &plug_name );
	vevo_property_set( port, "mixer", VEVO_ATOM_TYPE_INT,1, &extra );
	vevo_property_set( port, "HOST_plugin_type", VEVO_ATOM_TYPE_INT,1,&frei0r_signature_);
	vevo_property_set( port, "num_inputs", VEVO_ATOM_TYPE_INT,1, &n_inputs );
	vevo_property_set( port, "num_outputs", VEVO_ATOM_TYPE_INT,1, &n_outputs );
	
	int pixfmt = PIX_FMT_RGB24;
	free(plug_name);

	switch( finfo.color_model ) {
		case F0R_COLOR_MODEL_BGRA8888:
			pixfmt = PIX_FMT_BGRA;
			break;
		case F0R_COLOR_MODEL_RGBA8888:
		case F0R_COLOR_MODEL_PACKED32: //lol
			pixfmt = PIX_FMT_RGBA;
			break;
		default:
			break;
	}

	vevo_property_set( port, "format", VEVO_ATOM_TYPE_INT,1,&pixfmt);

#ifdef STRICT_CHECKING
	veejay_msg(VEEJAY_MSG_DEBUG, "\tparameters: %d, input channels: %d, output channels: %d, format: %x",
			r_params,n_inputs,n_outputs, pixfmt );
#endif	
	return port;
}

void	frei0r_plug_deinit( void *plugin )
{
	int state = 0;
	
	void *parent = NULL;
	int err	     = vevo_property_get( plugin, "parent",0, &parent );	
	if( err != VEVO_NO_ERROR ) {
		veejay_msg(0,"Unable to free plugin.");
		return;
	}
#ifdef STRICT_CHECKING
	vevo_port_dump( parent, 0 );
#endif
	f0r_destruct_f base = NULL;
	err = vevo_property_get( parent, "destruct", 0, &base);

	if( err == VEVO_NO_ERROR ) {
		f0r_instance_t instance = NULL;
		if( err == VEVO_NO_ERROR )
			err = vevo_property_get( plugin, "frei0r", 0, &instance );

		if( err == VEVO_NO_ERROR && instance != NULL && base != NULL )
			(*base)(instance);
	}


	fr0_conv_t *fr = NULL;
	err = vevo_property_get( plugin, "HOST_conv",0,&fr);
	if( fr && err == VEVO_NO_ERROR ){
		if(fr->buf) free(fr->buf);
		if(fr->in[0]) free(fr->in[0]);
		if(fr->in[1]) free(fr->in[1]);
		if(fr->in[2]) free(fr->in[2]);
		if(fr->out) free(fr->out);
		free(fr);
		fr = NULL;
	}
	
	vpf(plugin);
	plugin = NULL;
}

void	frei0r_destroy()
{
	if( out_scaler__ )
		yuv_free_swscaler( out_scaler__ );
	if( in_scaler__ )
		yuv_free_swscaler( in_scaler__ );
}


void *frei0r_plug_init( void *plugin , int w, int h, int pf )
{
	void *instance = vpn( VEVO_ANONYMOUS_PORT );
	f0r_construct_f base;
	vevo_property_get( plugin, "construct", 0, &base);
	f0r_instance_t k = (*base)(w,h);
	if( k == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize plugin");
		vpf(instance);
		return NULL;
	}

	vevo_property_set(instance, "frei0r", VEVO_ATOM_TYPE_PORTPTR, 1, &k);
	
	vevo_property_set( instance, "parent", VEVO_ATOM_TYPE_VOIDPTR, 1,&plugin);


	generic_push_channel_f gpc = frei0r_push_frame_f;
	generic_process_f      gpf = frei0r_process_frame_f;
	generic_push_parameter_f gpp = frei0r_plug_param_f;
	generic_deinit_f	 gdd = frei0r_plug_deinit;

	vevo_property_set( instance, "HOST_plugin_param_f", VEVO_ATOM_TYPE_VOIDPTR, 1, &gpp);
	vevo_property_set( instance, "HOST_plugin_push_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpc);
	vevo_property_set( instance, "HOST_plugin_process_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpf);
	vevo_property_set( instance, "HOST_plugin_deinit_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gdd);

	int frfmt = 0;
	vevo_property_get( plugin, "format",0,&frfmt ); 

	sws_template templ; 
	memset(&templ,0,sizeof(sws_template));
	templ.flags = yuv_which_scaler();

	int n_in = 0;
	int n_out = 0;
	vevo_property_get( plugin, "num_inputs",0,&n_in );
	vevo_property_get( plugin, "num_outputs",0,&n_out );
	
	int n_planes = 4 * (n_out + n_in);

	fr0_conv_t *fr = (fr0_conv_t*) vj_calloc(sizeof(fr0_conv_t));
	int i;
	fr->buf        = (uint8_t*) vj_calloc((sizeof(uint8_t) * w * h * n_planes) * (n_in+1) );
	uint8_t *bufx  = fr->buf;

	for( i = 0; i < (n_in+1); i ++ ) { //@ extra buffer for rgb output
		fr->in[i] = yuv_rgb_template(bufx, w,h, frfmt );
		bufx   += (w*h*4);
	}

	fr->out        = yuv_yuv_template(bufx, bufx+(w*h), bufx+(w*h*2), w,h,pf );
	
	if( n_in == 0 ) {
		fr->in[0] = yuv_rgb_template( fr->buf, w, h, frfmt );
		//fr->in[0] = yuv_yuv_template( NULL,NULL,NULL,w,h,pf );
	}

	if( out_scaler__ == NULL ) {
		out_scaler__	= yuv_init_swscaler( fr->in[0], 	fr->out, 	&templ, yuv_sws_get_cpu_flags()); // rgb -> yuv
		//@ allocate a scaler and assume rest of scalers is same. (haha)
	}

	if( n_in > 0 && in_scaler__ == NULL) { 
		in_scaler__  = yuv_init_swscaler( fr->out,	fr->in[0], 	&templ, yuv_sws_get_cpu_flags());  // yuv -> rgb
	}

	void *frptr    = (void*) fr;
	vevo_property_set( instance, "HOST_conv", VEVO_ATOM_TYPE_VOIDPTR,1,&frptr);

	int n_inputs = 0;
	int err = vevo_property_get(plugin, "num_inputs", 0, &n_inputs );
		err = vevo_property_set( instance, "num_inputs", VEVO_ATOM_TYPE_INT, 1, &n_inputs );
		err = vevo_property_get( plugin, "num_outputs", 0, &n_inputs );
		err = vevo_property_set( instance, "num_outputs", VEVO_ATOM_TYPE_INT,1,&n_inputs );

	return instance;
}

void	frei0r_plug_free( void *plugin )
{
	int n = 0;
	//FIXME: not used
	f0r_deinit_f base;
	if( vevo_property_get( plugin, "deinit", 0, &base) == VEVO_NO_ERROR )
		(*base)();
}

int	frei0r_process_frame_f( void *plugin )
{
	void *parent  = NULL;
	int err       = vevo_property_get( plugin, "parent",0,&parent );
	if( err != VEVO_NO_ERROR ) {
		veejay_msg(0, "unable to process frei0r plugin.");
		return 0;
	}

	f0r_update_f base;
	vevo_property_get( parent, "process", 0, &base );
	f0r_instance_t instance;
	f0r_update2_f base2;
	vevo_property_get( parent, "process_mix", 0, &base2 );



	vevo_property_get( plugin, "frei0r",0, &instance );		
	
	fr0_conv_t *fr = NULL;
	err = vevo_property_get(plugin, "HOST_conv",0,&fr);
	if( err != VEVO_NO_ERROR )
		return 0;
	
	VJFrame *dst = fr->last;

	int n_inputs = 0;
	err = vevo_property_get(plugin, "num_inputs", 0, &n_inputs );

	int n_outputs = 0;
	err = vevo_property_get(plugin, "num_outputs",0, &n_outputs );

	if( n_inputs == 0 && n_outputs == 1 ) {
		
		(*base)( instance, rand(), fr->buf, fr->buf );
		
		yuv_convert_and_scale_from_rgb( out_scaler__, fr->in[0], dst );
		
		return 1;

	} else if( n_inputs == 1 ) {

		(*base)( instance, rand(), fr->in[0]->data[0],fr->in[1]->data[0] );
	
		yuv_convert_and_scale_from_rgb( out_scaler__, fr->in[1], dst );

	} else if ( n_inputs == 2 ) {

		
		(*base2)( instance, rand(), fr->in[0]->data[0],fr->in[1]->data[0],NULL,fr->in[2]->data[0] );

		yuv_convert_and_scale_from_rgb( out_scaler__, fr->in[2], dst );

	}

	return 1;
}


void	frei0r_plug_param_f( void *port, int seq_no, void *dargs )
{
	void *plugin = NULL;
	int err	     = vevo_property_get(port, "frei0r",0,&plugin);
	if( err != VEVO_NO_ERROR ) {
		return;
	}
	f0r_set_param_value_f q;
	f0r_get_param_info_f  inf = NULL;
	f0r_param_position_t pos;	
	f0r_param_color_t col;
	f0r_param_t *fparam = NULL;
	int np = 0;
	err = vevo_property_get(port, "num_params",0,&np);
	
	if( seq_no > np || np == 0 ) {
		return; //@ plug accepts no params but set param is called anyway 
	}

	void *parent = NULL;
	err 	     = vevo_property_get(port, "parent",0,&parent);
	if( err != VEVO_NO_ERROR)  {
		return;
	}

	double value = 0.0;
	int *args = (int*) dargs;

	err = vevo_property_get( parent, "set_params", 0, &q);
       
	if( err != VEVO_NO_ERROR ) {
		veejay_msg(0, "dont know how to set parameter %d",seq_no);
		return;
	}

	f0r_param_info_t finfo;
		
	if( vevo_property_get( parent, "parameters",0,&inf ) != VEVO_NO_ERROR )
		return;
	


	(*inf)( &finfo, seq_no );

	char key[20];
	snprintf(key, sizeof(key)-1, "p%02d", seq_no );
	void *param = NULL;
	
	if(vevo_property_get( parent, key, 0, &param )!=VEVO_NO_ERROR)
		param = NULL;

	switch( finfo.type ) {
		case F0R_PARAM_BOOL:
			value = ( (int) args[0]);
			fparam = &value;
			if(param) vevo_property_set( param, "value", VEVO_ATOM_TYPE_INT, 1,&args[0]);
			break;
		case F0R_PARAM_DOUBLE:
			value = ((double)args[0]*0.01);
			fparam=&value;
			if(param) vevo_property_set( param,"value", VEVO_ATOM_TYPE_INT,1,&args[0]);
			break;
		case F0R_PARAM_POSITION:
			pos.x = ( (float) args[0] * 0.01);
			pos.y = ( (float) args[1] * 0.01);
			fparam = &pos;
			if(param) vevo_property_set( param,"value",VEVO_ATOM_TYPE_INT,2, args );
			break;
		case F0R_PARAM_COLOR:
			col.r = ( (double) args[0] * 0.01);
			col.g = ( (double) args[1] * 0.01);
			col.b = ( (double) args[2] * 0.01);
			fparam = &col;
			if(param) vevo_property_set( param, "value", VEVO_ATOM_TYPE_INT,3, args);
			break;
		default:
			veejay_msg(VEEJAY_MSG_DEBUG, "Parameter type %d not supported.",finfo.type);
			break;
	}
	
	if( fparam && param ) {
		int fr0_seq_no = 0;
		vevo_property_get(param, "seqno", 0,&fr0_seq_no);
		(*q)( plugin, fparam, seq_no );
	}
}

void	frei0r_plug_control( void *port, int *args )
{
}

