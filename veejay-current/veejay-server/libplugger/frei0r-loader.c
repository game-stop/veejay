/* veejay - Linux VeeJay - libplugger utility
 *           (C) 2010      Niels Elburg <nwelburg@gmail.com> ported from veejay-ng
 * 	     (C) 2002-2015 Niels Elburg <nwelburg@gmail.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <veejaycore/hash.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <veejaycore/libvevo.h>
#include <libplugger/defs.h>
#include <veejaycore/yuvconv.h>
#include <libavutil/pixfmt.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <libplugger/portdef.h>
#include <libplugger/defaults.h>
#include <libplugger/specs/frei0r.h>
#include <libplugger/frei0r-loader.h>
#include <veejaycore/avcommon.h>

#define    RUP8(num)(((num)+8)&~8)
#define _VJ_MAX_PARAMS 32

static int read_plugin_configuration = 0;
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

typedef void (*f0r_get_param_value_f)(f0r_instance_t *instance, f0r_param_t *param, int param_index );

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

static inline int frei0r_to_vj_np( int hint )
{
	switch(hint) {
		case F0R_PARAM_BOOL: return 1;
		case F0R_PARAM_DOUBLE: return 1;
		case F0R_PARAM_POSITION: return 2;
		case F0R_PARAM_COLOR: return 3;
	}
	return 0;
}

static struct  {
	const char *name;
	int	major;
	int	minor;
} frei0r_black_list[] = { /* plugins that crash in f0r_update() */
//	{ "Scale0Tilt", 0, 1 }, 
	{ "opencvfacedetect", 0, 1 }, /* default initialization fails */ 
	{ "Curves", 0, 1 },
	{ "scanline0r",0, 1 },
	{ "RGB-Parade", 0, 1 },
	{ "pr0file",0,2 },
	{ "NDVI filter", 0, 1},
    { "bgsubtract0r",0, 3},
	{ NULL, 0, 0 },
};

static inline int frei0r_param_get_double(f0r_get_param_value_f q,void *plugin, int seq_no)
{
	double d = 0.0;
	(*q)( plugin, (void**) &d, seq_no );

	return (int) d;
}

static inline int frei0r_param_set_double(f0r_set_param_value_f q,void *plugin, int seq_no,int offset, int *args )
{
	double value = (double) args[offset] / 100.0;
	f0r_param_t *fparam = (f0r_param_t*) &value;
	(*q)( plugin, fparam, seq_no );

	return 1;
}

static inline int frei0r_param_get_bool(f0r_get_param_value_f q, void *plugin, int seq_no)
{
	double b;
	(*q)( plugin, (void **) &b, seq_no );
	if( b!= 0.0 && b!=1. ) {
		b = 0.0;
	}
	if(b <= 0.5 )
		b = 0.0;
	else
		b = 1.0;

	return (int) b;
}

static inline int frei0r_param_set_bool(f0r_set_param_value_f q,void *plugin, int seq_no, int offset, int *args )
{
	double value = (args[offset] == 1 ? 1.0: 0.0 );
	f0r_param_t *fparam = (f0r_param_t*) &value;
	(*q)( plugin, fparam, seq_no );

	return 1;
}
static inline int frei0r_param_get_position(f0r_get_param_value_f q, void *plugin, int seq_no, int *x, int *y, int w, int h)
{
	f0r_param_position_t param;
	param.x = 0.0;
	param.y = 0.0;

	(*q)( plugin, (void **) &param, seq_no );
	
	if( param.x < 0.0 ) param.x = 0.0;
	if( param.x > 1.0 ) param.x = 1.0;

	if( param.y < 0.0 ) param.y = 0.0;
	if( param.y > 1.0 ) param.y = 1.0;

	*x = (w * param.x);
	*y = (h * param.y);

	return 1;
}

static inline int frei0r_param_set_position(f0r_set_param_value_f q,void *plugin, int seq_no,int offset, int *args, int width, int height )
{
	f0r_param_position_t pos;
	f0r_param_t *fparam = NULL;

	pos.x = (args[offset] / width );
	pos.y = (args[offset+1] / height);

	fparam = (f0r_param_t*) &pos;
	(*q)( plugin, fparam, seq_no );

	return 2;
}

static inline int frei0r_param_get_color( f0r_get_param_value_f q,void *plugin, int seq_no, int *r, int *g, int *b )
{
	f0r_param_color_t param;
	param.r = 0.0;
	param.g = 0.0;
	param.b = 0.0;

	(*q)( plugin, (void**) &param, seq_no );

	if( param.r < 0. ) param.r = 0.0;
	if( param.r > 1.0 ) param.r = 1.0;
	if( param.g < 0. ) param.g = 0.0;
	if( param.g > 1.0 ) param.g = 1.0;
	if( param.b < 0. ) param.b = 0.0;
	if( param.b > 1.0 ) param.b = 1.0;

	*r = 255 * param.r;
	*g = 255 * param.g;
	*b = 255 * param.b;

	return 1;
}

static inline int frei0r_param_set_color(f0r_set_param_value_f q,void *plugin, int seq_no,int offset, int *args)
{
	f0r_param_color_t col;
	f0r_param_t *fparam = NULL;
	col.r = ( args[offset] / 255.0f);
	col.g = ( args[offset+1] / 255.0f);
	col.b = ( args[offset+2] / 255.0f);

	fparam = (f0r_param_t*) &col;
	(*q)( plugin, fparam, seq_no );
	
	return 3;
}

static	void	*frei0r_plug_get_parameter_port(void *plugin, int seq_no)
{
	char key[20];
	snprintf(key, sizeof(key), "p%02d", seq_no );
	void *param = NULL;
	
	if(vevo_property_get( plugin, key, 0, &param ) != VEVO_NO_ERROR) {
		return NULL;
	}
	return param;
}

static int frei0r_plug_get_default_param(void *plugin, int k)
{
	void *param = frei0r_plug_get_parameter_port( plugin, k );
	int v = 0.0;
	if( param ) {
		vevo_property_get( param, "default", 0, &v );
	}
	return v;
}

static void frei0r_param_set_default(void *plugin, int k, int value)
{
	void *param = frei0r_plug_get_parameter_port( plugin, k );
	int v = value;
	if( param ) {
		vevo_property_set( param, "default",VEVO_ATOM_TYPE_INT, 1, &v );
	}
}

static inline void *frei0r_plug_get_param( void *parent, int vj_seq_no, int *hint )
{
	void *param = frei0r_plug_get_parameter_port( parent, vj_seq_no );
	int type = -1;
	if( param == NULL )
		return NULL;
	vevo_property_get( param, "hint", 0, &type );
	
	*hint = type;

	return param;
}

void	frei0r_plug_param_f( void *port, int num_args, int *args )
{
	void *plugin = NULL;
	int err	     = vevo_property_get(port, "frei0r",0,&plugin);
	if( err != VEVO_NO_ERROR ) {
		return;
	}

	void *parent = NULL;
	err 	     = vevo_property_get(port, "parent",0,&parent);
	if( err != VEVO_NO_ERROR)  {
		return;
	}

	int np = 0;
	err = vevo_property_get( parent, "num_params",0,&np);
	
	if( np == 0 ) {
		return; //@ plug accepts no params but set param is called anyway 
	}

	f0r_set_param_value_f q = NULL;

	err = vevo_property_get( parent, "set_params", 0, &q);
       
	if( err != VEVO_NO_ERROR ) {
		return;
	}
	int width = 1;
	int height = 1;

	vevo_property_get( port, "HOST_plugin_width", 0, &width );
	vevo_property_get( port, "HOST_plugin_height", 0, &height );

	int vj_seq_no = 0;
	int done = 0;

	while( done == 0 ) {
		int hint = 0;
		void *param = frei0r_plug_get_param(parent, vj_seq_no, &hint );
		if(param == NULL ) {
			vj_seq_no ++;
			if( vj_seq_no >= num_args )
				done = 1;
			continue;
		}

		int seq_no = 0;
		vevo_property_get( param, "seqno", 0, &seq_no );
		
		switch( hint ) {
			case F0R_PARAM_BOOL: 
				frei0r_param_set_bool(q,plugin, seq_no, vj_seq_no, args );
				break;
			case F0R_PARAM_DOUBLE:
				frei0r_param_set_double(q,plugin, seq_no, vj_seq_no, args );
				break;
			case F0R_PARAM_POSITION: 
				frei0r_param_set_position(q,plugin,seq_no,vj_seq_no,args,width,height);
				break;
			case F0R_PARAM_COLOR:
				frei0r_param_set_color(q,plugin,seq_no,vj_seq_no,args);
				break;
		}

		vj_seq_no += frei0r_to_vj_np( hint );
		if( vj_seq_no >= num_args )
			done = 1;

	}
}

int frei0r_get_params_f( void *port, int *args )
{

	void *plugin = NULL;
	int err	     = vevo_property_get(port, "frei0r",0,&plugin);
	if( err != VEVO_NO_ERROR ) {
		return 0;
	}

	void *parent = NULL;
	err 	     = vevo_property_get(port, "parent",0,&parent);
	if( err != VEVO_NO_ERROR)  {
		return 0;
	}

	int np = 0;
	err = vevo_property_get( parent, "fre_params",0,&np);
	if( np == 0 ) {
		return 0; //@ plug accepts no params but set param is called anyway 
	}

	int width = 1;
	int height = 1;

	int i;
	int vj_seq_no = 0;

	f0r_get_param_value_f q = NULL;
	err = vevo_property_get( parent, "get_params", 0, &q);
	if( err || q == NULL )
		return 0;

	for( i = 0;i < np; i ++ ) {
		int hint = 0;
		void *param = frei0r_plug_get_param(parent, i, &hint );

		int seq_no = 0;
		vevo_property_get( param, "seqno", 0, &seq_no );
		
		switch( hint ) {
			case F0R_PARAM_BOOL: 
				args[vj_seq_no] = frei0r_param_get_bool(q,plugin, seq_no);
				vj_seq_no ++;
				break;
			case F0R_PARAM_DOUBLE:
				args[vj_seq_no] = frei0r_param_get_double(q,plugin, seq_no);
				vj_seq_no++;	

				break;
			case F0R_PARAM_POSITION: 
				vevo_property_get( port, "HOST_plugin_width", 0, &width );
				vevo_property_get( port, "HOST_plugin_height", 0, &height );
				frei0r_param_get_position(q,plugin,seq_no,&(args[vj_seq_no]), &(args[vj_seq_no+1]), width,height);
				vj_seq_no += 2;
				break;
			case F0R_PARAM_COLOR:
				frei0r_param_get_color(q,plugin,seq_no,&(args[vj_seq_no]), &(args[vj_seq_no+1]), &(args[vj_seq_no+2]));
				vj_seq_no += 3;	
				break;
		}
	}

	return vj_seq_no;
}

int	frei0r_get_param_count( void *port)
{	
	void *plugin = NULL;
	int err	     = vevo_property_get(port, "frei0r",0,&plugin);
	if( err != VEVO_NO_ERROR ) {
		return 0;
	}

	void *parent = NULL;
	err 	     = vevo_property_get(port, "parent",0,&parent);
	if( err != VEVO_NO_ERROR)  {
		return 0;
	}

	int np = 0;
	err = vevo_property_get(parent, "num_params",0,&np);
	if( err != VEVO_NO_ERROR || np == 0 )
		return 0;

	return np;
}

int	frei0r_get_param_f( void *port, void *dst )
{
	void *parent = NULL;
	int err      = vevo_property_get(port, "parent",0,&parent);
	if( err != VEVO_NO_ERROR)  {
		return 0;
	}

	int np = 0;
	err = vevo_property_get(parent, "num_params",0,&np);
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
	fr0_conv_t *fr = NULL;
	int err = vevo_property_get(plugin, "HOST_conv",0,&fr);
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

static char 	*split_parameter_name( const char *name, const char *vj_name ) 
{
	int len = strlen(name) + strlen(vj_name) + 4;
	char *str = malloc(len);
	snprintf(str,len, "%s_(%s)", name,vj_name );
	return str;
}

static void 	*init_parameter_port( int min, int max, int def,const char *name, int seq_no, int type )
{
	void *parameter = vpn( VEVO_FR_PARAM_PORT );
	char *dname = vj_strdup(name);

	int n = 0;
	while( dname[n] != '\0' ) {
		if(dname[n] == ' ')
			dname[n] = '_';
		n++;
	}

	vevo_property_set( parameter, "name", VEVO_ATOM_TYPE_STRING,1,&dname );
	vevo_property_set( parameter, "min", VEVO_ATOM_TYPE_INT, 1, &min);
	vevo_property_set( parameter, "seqno", VEVO_ATOM_TYPE_INT,1,&seq_no);
	vevo_property_set( parameter, "max", VEVO_ATOM_TYPE_INT,1 , &max);
	vevo_property_set( parameter, "default", VEVO_ATOM_TYPE_INT,1, &def );
	vevo_property_set( parameter, "hint", VEVO_ATOM_TYPE_INT,1, &type );	

	if( type == F0R_PARAM_COLOR ) {
		int rgb = 1;
		vevo_property_set( parameter, "rgb_conv", VEVO_ATOM_TYPE_INT,1 , &rgb );
	}

	free(dname);
	return parameter;
}

static void store_parameter_port( void *port, int seq_no, void *parameter_port )
{
	char key[20];	
	snprintf(key,sizeof(key), "p%02d", seq_no );
	vevo_property_set( port, key, VEVO_ATOM_TYPE_PORTPTR, 1, &parameter_port );
}

static int init_param_fr( void *port, f0r_param_info_t *info, int offset, int frei0r_param_count)
{
	int np = 0;
	int size = frei0r_to_vj_np( info->type );
	switch(info->type)
	{
		case F0R_PARAM_DOUBLE:
			if( (offset+size) < _VJ_MAX_PARAMS ) {
				store_parameter_port( port, offset, init_parameter_port( 0, 100,10, info->name,frei0r_param_count, info->type ) );
				np = size;
			}
			break;
		case F0R_PARAM_BOOL:
			if( (offset+size) < _VJ_MAX_PARAMS ) {
				store_parameter_port( port, offset, init_parameter_port( 0, 1,0, info->name,frei0r_param_count, info->type ) );
				np = size;
			}
			break;
		case F0R_PARAM_COLOR:
			if( (offset+size) < _VJ_MAX_PARAMS ) {
				char *red = split_parameter_name( info->name, "Red" );
				store_parameter_port( port, offset, init_parameter_port(0, 255,255, red,frei0r_param_count, info->type) );
				char *green = split_parameter_name( info->name, "Green" );
				store_parameter_port( port, offset+1, init_parameter_port(0, 255,255, green,frei0r_param_count, info->type) );
				char *blue = split_parameter_name( info->name, "Blue" );
				store_parameter_port( port, offset+2, init_parameter_port(0, 255,255, blue,frei0r_param_count, info->type) );
				np = size;
				free(red); free(green); free(blue);
			}
			break;
		case F0R_PARAM_POSITION:
			if( (offset+size) < _VJ_MAX_PARAMS ) {
				char *x = split_parameter_name( info->name, "X" );
				store_parameter_port( port, offset+1, init_parameter_port(0,100,100, x,frei0r_param_count, info->type ));
				char *y = split_parameter_name( info->name, "Y" );
				store_parameter_port( port, offset+2, init_parameter_port(0,100,100, y,frei0r_param_count, info->type ));
				np = size;	
				free(x);
				free(y);
			}
			break;
		default:
			veejay_msg(VEEJAY_MSG_DEBUG, "frei0r %d '%s' not supported" , frei0r_param_count,
					info->name );
			break;
	}

	return np;
}

static int is_bad_frei0r_plugin( f0r_plugin_info_t *info )
{
	int i;
	for( i = 0; frei0r_black_list[i].name != NULL; i ++ ) {
		if(strcasecmp( info->name, frei0r_black_list[i].name ) == 0 ) {
			if( info->major_version <= frei0r_black_list[i].major &&
			    info->minor_version <= frei0r_black_list[i].minor ) {
				return 1;
			}
		}
	}
	return 0;
}

static void	frei0r_read_plug_configuration(void *plugin, const char *name)
{
	FILE *f = plug_open_config( "frei0r", name, "r",0 );
	if(!f) {
		veejay_msg(VEEJAY_MSG_DEBUG, "No configuration file for frei0r plugin %s", name);
		FILE *cfg = plug_open_config( "frei0r", name, "w",1 );
		if( cfg ) {
			int i;
			int n_params = 0;
			vevo_property_get( plugin, "num_params", 0, &n_params );
			for( i = 0; i < n_params; i ++ ) 
			{	
				fprintf(cfg,"%d ", frei0r_plug_get_default_param(plugin, i)); //write out all default values
			}
		
			fclose(cfg);
		}
		return;
	}

	int p = 0;
	int dbl = 0.0;

	while( (fscanf( f, "%d", &dbl )) == 1 ) {
		frei0r_param_set_default( plugin, p, dbl );
		p++;
	}

	fclose(f);
}


void* 	deal_with_fr( void *handle, char *name)
{
	void *port = vpn( VEVO_FR_PORT );
	f0r_init_f	f0r_init_func	= dlsym( handle, "f0r_init" );
	if( f0r_init_func == NULL )
	{
		vpf( port );
		return NULL;
	}

	f0r_deinit_f	f0r_deinit_func	= dlsym( handle, "f0r_deinit" );
	if( f0r_deinit_func == NULL )
	{
		vpf( port );
		return NULL;
	}

	f0r_get_plugin_info_f	f0r_info = dlsym( handle, "f0r_get_plugin_info");
	if( f0r_info == NULL )
	{
		vpf( port );
		return NULL;
	}

	f0r_get_param_info_f	f0r_param= dlsym( handle, "f0r_get_param_info" );
	if( f0r_param == NULL )
	{
		vpf( port );
		return NULL;
	}

	void	*f0r_construct	= dlsym( handle, "f0r_construct" );
	void	*f0r_destruct	= dlsym( handle, "f0r_destruct" );
	void	*processf	= dlsym( handle, "f0r_update" );
	void	*processm	= dlsym( handle, "f0r_update2" );
	void	*set_params	= dlsym( handle, "f0r_set_param_value" );
	void	*get_params	= dlsym( handle, "f0r_get_param_value" );

	vevo_property_set( port, "handle", VEVO_ATOM_TYPE_VOIDPTR,1, &handle );
	vevo_property_set( port, "init", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_init_func );
	vevo_property_set( port, "deinit", VEVO_ATOM_TYPE_VOIDPTR, 1, &f0r_deinit_func );
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
	if( get_params != NULL )
		vevo_property_set( port, "get_params", VEVO_ATOM_TYPE_VOIDPTR, 1, &get_params );

    	f0r_plugin_info_t finfo;
	f0r_param_info_t pinfo;

	memset( &finfo,0,sizeof(f0r_plugin_info_t));

	if( (*f0r_init_func)() == 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Failed to initialize frei0r plugin '%s': ", name);
		vpf( port );
		return NULL;
	}

	(*f0r_info)(&finfo);

	if( finfo.frei0r_version != FREI0R_MAJOR_VERSION )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"I am using frei0r version %d but plugin requires %d",
				FREI0R_MAJOR_VERSION, finfo.frei0r_version );
		(*f0r_deinit_func)();	
		vpf(port);
		return NULL;	
	}

	if( is_bad_frei0r_plugin( &finfo ) ) { 
		veejay_msg(VEEJAY_MSG_ERROR, "Frei0r %s-%d.%d is blacklisted. Please upgrade this plug-in to a newer version",
				finfo.name, finfo.major_version, finfo.minor_version);
		(*f0r_deinit_func)();
		vpf(port);
		return NULL;
	}

	char plugin_name[512];
	snprintf( plugin_name, sizeof(plugin_name) , "frei0r %s", finfo.name ); 

	char *plug_name = vj_strdup( plugin_name );

	int extra = 0;
	int n_inputs = 0;
	int n_outputs = 0;
	if( finfo.plugin_type == F0R_PLUGIN_TYPE_MIXER2 ) {
		extra = 1;
		n_inputs = 2;
		if( processm == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Supposed to be mixer plugin (2 sources) but no f0r_update2");
			(*f0r_deinit_func)();
			vpf(port);
			if(plug_name) free(plug_name);
			return NULL;
		}
	} else if ( finfo.plugin_type == F0R_PLUGIN_TYPE_FILTER ) {
		n_inputs = 1;
		if( processf == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Supposed to be filter plugin (1 input source) but no f0r_update");
			(*f0r_deinit_func)();
			vpf(port);
			if(plug_name) free(plug_name);
			return NULL;
		}
	} else if ( finfo.plugin_type == F0R_PLUGIN_TYPE_SOURCE ) {
		n_inputs = 0;
		n_outputs = 1;
		if( processf == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Supposed to be generator plugin (1 output source) but no f0r_update");
			(*f0r_deinit_func)();
			vpf(port);
			if(plug_name) free(plug_name);
			return NULL;
		}
	} else {
		veejay_msg(VEEJAY_MSG_ERROR, "Frei0r plugin '%s' (%s) unsupported type", finfo.name, plugin_name );
		(*f0r_deinit_func)();
		vpf(port);
		if(plug_name) free(plug_name);
		return NULL;
	}


	//@ cheap check for insane frei0r plugins
	if( (finfo.plugin_type == F0R_PLUGIN_TYPE_FILTER && processf == NULL) ||
	     (finfo.plugin_type == F0R_PLUGIN_TYPE_MIXER2 && processm == NULL) ) {
		veejay_msg(0, "Frei0r plugin %s behaves badly",name);
		(*f0r_deinit_func)();
		vpf(port);
		return NULL;
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "Frei0r plugin '%s' version %d.%d by %s (%d in parameters, %d in channels)",
			plugin_name, finfo.major_version, finfo.minor_version, finfo.author, finfo.num_params, n_inputs );
	
	int n_params = finfo.num_params;
	int r_params = 0;
	int p = 0;
	if( set_params == NULL )
		n_params = 0; 

	for ( p = 0; p < n_params; )
	{
		veejay_memset( &pinfo,0,sizeof(f0r_param_info_t));

		(*f0r_param)(&pinfo,p);
		
		int vj_args = frei0r_to_vj_np( pinfo.type );
		if(vj_args == 0 ) {
			p ++;
			continue;
		}
		
		if( (r_params + vj_args) < _VJ_MAX_PARAMS )
		{
			init_param_fr(port, &pinfo, r_params, p );
			r_params += vj_args;
		}

		p ++;
	}


	vevo_property_set( port, "num_params", VEVO_ATOM_TYPE_INT, 1, &r_params );
	vevo_property_set( port, "fre_params", VEVO_ATOM_TYPE_INT, 1, &n_params );
	vevo_property_set( port, "name", VEVO_ATOM_TYPE_STRING,1, &plug_name );
	vevo_property_set( port, "mixer", VEVO_ATOM_TYPE_INT,1, &extra );
	vevo_property_set( port, "HOST_plugin_type", VEVO_ATOM_TYPE_INT,1,&frei0r_signature_);
	vevo_property_set( port, "num_inputs", VEVO_ATOM_TYPE_INT,1, &n_inputs );
	vevo_property_set( port, "num_outputs", VEVO_ATOM_TYPE_INT,1, &n_outputs );
	
	int pixfmt = PIX_FMT_RGB24;

	switch( finfo.color_model ) {
		case F0R_COLOR_MODEL_BGRA8888:
			pixfmt = PIX_FMT_BGRA;
			break;
		case F0R_COLOR_MODEL_RGBA8888:
		case F0R_COLOR_MODEL_PACKED32: 
			pixfmt = PIX_FMT_RGBA;
			break;
		default:
			break;
	}

	vevo_property_set( port, "format", VEVO_ATOM_TYPE_INT,1,&pixfmt);

	free( plug_name );

    if( read_plugin_configuration ) {
	    frei0r_read_plug_configuration(port, name);
    }

	return port;
}

void	frei0r_plug_deinit( void *plugin )
{
	void *parent = NULL;
	int err	     = vevo_property_get( plugin, "parent",0, &parent );	
	if( err != VEVO_NO_ERROR ) {
		veejay_msg(0,"Unable to free plugin");
		return;
	}

	f0r_destruct_f base = NULL;
	err = vevo_property_get( parent, "destruct", 0, &base);

	if( err == VEVO_NO_ERROR ) {
		f0r_instance_t instance = NULL;
		if( err == VEVO_NO_ERROR )
			err = vevo_property_get( plugin, "frei0r", 0, &instance );

		if( err == VEVO_NO_ERROR && instance != NULL && base != NULL )
			(*base)(instance);
	}

	int n_in = 0;
	int i = 0;
	vevo_property_get( plugin, "num_inputs",0,&n_in );
	
	fr0_conv_t *fr = NULL;
	err = vevo_property_get( plugin, "HOST_conv",0,&fr);
	if( fr && err == VEVO_NO_ERROR ){
		if(fr->buf) free(fr->buf);
		for(i = 0;i < (n_in+1); i++ ){
			if(fr->in[i]) free(fr->in[i]);
		}
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


void *frei0r_plug_init( void *plugin , int w, int h, int pf, int read_plug_cfg )
{
    read_plugin_configuration = read_plug_cfg;

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

	vevo_property_set(instance, "frei0r", VEVO_ATOM_TYPE_VOIDPTR, 1, &k);
	
	vevo_property_set( instance, "parent", VEVO_ATOM_TYPE_VOIDPTR, 1,&plugin);


	generic_push_channel_f gpc = (generic_push_channel_f) frei0r_push_frame_f;
	generic_process_f      gpf = (generic_process_f) frei0r_process_frame_f;
	generic_push_parameter_f gpp = (generic_push_parameter_f) frei0r_plug_param_f;
	generic_deinit_f	 gdd = (generic_deinit_f) frei0r_plug_deinit;

	vevo_property_set( instance, "HOST_plugin_param_f", VEVO_ATOM_TYPE_VOIDPTR, 1, &gpp);
	vevo_property_set( instance, "HOST_plugin_push_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpc);
	vevo_property_set( instance, "HOST_plugin_process_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpf);
	vevo_property_set( instance, "HOST_plugin_deinit_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gdd);

	vevo_property_set( instance, "HOST_plugin_width",VEVO_ATOM_TYPE_INT,1,&w );
	vevo_property_set( instance, "HOST_plugin_height",VEVO_ATOM_TYPE_INT,1,&h );


	int frfmt = 0;
	vevo_property_get( plugin, "format",0,&frfmt ); 

	sws_template templ; 
	memset(&templ,0,sizeof(sws_template));
	templ.flags = yuv_which_scaler();

	int n_in = 0;
	int n_out = 0;
	vevo_property_get( plugin, "num_inputs",0,&n_in );
	vevo_property_get( plugin, "num_outputs",0,&n_out );
	
	int n_planes = 4 * (n_out + n_in + 1);

	fr0_conv_t *fr = (fr0_conv_t*) vj_calloc(sizeof(fr0_conv_t));
	int i;
	fr->buf        = (uint8_t*) vj_malloc((sizeof(uint8_t) * RUP8( w * h * n_planes ) ));
	uint8_t *bufx  = fr->buf;

	for( i = 0; i < (n_in+1); i ++ ) { //@ extra buffer for rgb output
		fr->in[i] = yuv_rgb_template(bufx, w,h, frfmt );
		bufx   += RUP8(w*h*4);
	}

	fr->out          = yuv_yuv_template(bufx, bufx+RUP8(w*h), bufx+RUP8(w*h*2), w,h,pf );
	fr->out->data[3] = bufx + (fr->out->len + fr->out->uv_len + fr->out->uv_len);

	if( out_scaler__ == NULL ) {
		out_scaler__	= yuv_init_swscaler( fr->in[0],fr->out,&templ,yuv_sws_get_cpu_flags()); // rgb -> yuv
	}

	if( n_in > 0 && in_scaler__ == NULL) { 
		in_scaler__  = yuv_init_swscaler( fr->out,fr->in[0],&templ,yuv_sws_get_cpu_flags());  // yuv -> rgb
	}

	void *frptr    = (void*) fr;
	vevo_property_set( instance, "HOST_conv", VEVO_ATOM_TYPE_VOIDPTR,1,&frptr);

	int n_inputs = 0;
	vevo_property_get(plugin, "num_inputs", 0, &n_inputs );
	vevo_property_set( instance, "num_inputs", VEVO_ATOM_TYPE_INT, 1, &n_inputs );
	vevo_property_get( plugin, "num_outputs", 0, &n_inputs );
	vevo_property_set( instance, "num_outputs", VEVO_ATOM_TYPE_INT,1,&n_inputs );

	return instance;
}

void	frei0r_plug_free( void *plugin )
{
	f0r_deinit_f base;
	if( vevo_property_get( plugin, "deinit", 0, &base) == VEVO_NO_ERROR )
		(*base)();
}

int	frei0r_process_frame_f( void *plugin )
{
	void *parent  = NULL;
	int err       = vevo_property_get( plugin, "parent",0,&parent );
	if( err != VEVO_NO_ERROR ) {
		veejay_msg(0, "unable to process frei0r plugin");
		return 0;
	}

	f0r_instance_t instance;
	vevo_property_get( plugin, "frei0r",0, &instance );		
	
	fr0_conv_t *fr = NULL;
	err = vevo_property_get(plugin, "HOST_conv",0,&fr);
	if( err != VEVO_NO_ERROR )
		return 0;
	
	int n_inputs = 0;
	err = vevo_property_get(plugin, "num_inputs", 0, &n_inputs );
	if( err != VEVO_NO_ERROR ) {
		n_inputs = 0;
	}

	int n_outputs = 0;
	err = vevo_property_get(plugin, "num_outputs",0, &n_outputs );
	if( err != VEVO_NO_ERROR ) {
		n_outputs = 0;
	}

	if( n_inputs == 0 && n_outputs == 1 ) {
		f0r_update_f base;
		vevo_property_get( parent, "process", 0, &base );
	
		(*base)( instance, rand(), (const uint32_t*) fr->buf, (uint32_t*) fr->in[0]->data[0] );
		
		yuv_convert_and_scale_from_rgb( out_scaler__, fr->in[0], fr->last );
		
	} else if( n_inputs == 1 ) {
		f0r_update_f base;
		vevo_property_get( parent, "process", 0, &base );
	
		(*base)( instance, rand(), (const uint32_t*) fr->in[0]->data[0], (uint32_t*) fr->in[1]->data[0] );
	
		yuv_convert_and_scale_from_rgb( out_scaler__, fr->in[1], fr->last );

	} else if ( n_inputs == 2 ) {
		f0r_update2_f base2;
		vevo_property_get( parent, "process_mix", 0, &base2 );
		
		(*base2)( instance, rand(), (const uint32_t*) fr->in[0]->data[0],(const uint32_t*) fr->in[1]->data[0],NULL, (uint32_t*) fr->in[2]->data[0] );

		yuv_convert_and_scale_from_rgb( out_scaler__, fr->in[2], fr->last );
	}

	return 1;
}
