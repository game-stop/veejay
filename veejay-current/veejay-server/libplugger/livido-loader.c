/* veejay - Linux VeeJay - libplugger utility
 *           (C) 2010      Niels Elburg <nwelburg@gmail.com> ported from veejay-ng
 * 	     (C) 2002-2006 Niels Elburg <nwelburg@gmail.com> 
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

/** \defgroup livido Livido Host
 *
 * See livido specification at http://livido.dyne.org
 *
 * This implements an almost complete and hopefully "correct" livido host.
 */
#include <config.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
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
#include <libplugger/ldefs.h>
#include <libplugger/specs/livido.h>
#include <libplugger/portdef.h>
#include <libyuv/yuvconv.h>
#include <libavutil/avutil.h>
#include <stdlib.h>
//#include <veejay/oscservit.h>
#include <libplugger/utility.h>
#include <libplugger/livido-loader.h>

#define LIVIDO_COPY 1

#define IS_RGB_PALETTE( p ) ( p < 512 ? 1 : 0 )

void    livido_dummy_function() {

}
   
static	char	make_valid_char_( const char c )
{
	const char *invalid = " #*,?[]{}";
	int k = 0;
	char o = '_';
	char r = c;
	for( k = 0; k < 8 ; k ++ )
	{
		if ( c == invalid[k] || isspace((unsigned char)c))
			return o;
		char l = tolower(c);
		if(l)
			r = l;
	}
	return r;
}

char	*veejay_valid_osc_name( const char *in )
{
	int n = strlen( in );
	int k;
	char *res = strdup( in );
	for( k = 0; k < n ; k ++ )
	{
		res[k] = make_valid_char_( in[k] );
	}
	return res;
}

static	int	pref_palette_ = 0;
static	int	pref_palette_ffmpeg_ = 0;
static	int	livido_signature_ = VEVO_PLUG_LIVIDO;

typedef	int	(*livido_set_parameter_f)( void *parameter, void *value );

static struct
{
	int	lp;
	int	pf;
} palette_list_[] = 
{
	 { LIVIDO_PALETTE_RGB888, PIX_FMT_RGB24 },
	 { LIVIDO_PALETTE_BGR888, PIX_FMT_BGR24 },
	 { LIVIDO_PALETTE_YUV422P,PIX_FMT_YUV422P },
	 { LIVIDO_PALETTE_YUV420P,PIX_FMT_YUV420P },
	 { LIVIDO_PALETTE_YUV444P,PIX_FMT_YUV444P },
//	 { LIVIDO_PALETTE_RGBA32, PIX_FMT_RGBA32  },
	 { -1, -1 }
};

static	struct
{
	int it;
	int pf;
} img_palettes_[] = 
{
	{	0,	PIX_FMT_YUV420P },
	{	1,	PIX_FMT_YUV422P },
	{	2,	PIX_FMT_YUV444P },
	{	-1,	-1 },
};

static	struct
{
	int it;
	int lp;
} vj_palettes_[] = 
{
	{	0,	LIVIDO_PALETTE_YUV420P },
	{	1,	LIVIDO_PALETTE_YUV422P },
	{	2,	LIVIDO_PALETTE_YUV444P },
	{	-1,	-1 },
};

static	int	select_ffmpeg_palette(int lvd_palette )
{
	int i = 0;
	for( i = 0; palette_list_[i].pf != -1 ; i ++ )
	  if( lvd_palette == palette_list_[i].lp )
		return palette_list_[i].pf;
	return -1;	
}
static	int	select_livido_palette(int palette )
{
	int i = 0;
	for( i = 0; palette_list_[i].pf != -1 ; i ++ )
	  if( palette == palette_list_[i].pf )
		return palette_list_[i].lp;
	return -1;	
}

static	int	configure_channel( void *instance, const char *name, int channel_id, VJFrame *frame )
{
	void *channel = NULL;
	int error = 0;
	void *pd[4];

	error = vevo_property_get( instance, name, channel_id, &channel );
#ifdef STRICT_CHECKING
	if( error != LIVIDO_NO_ERROR )
		veejay_msg(0, "Key '%s' element %d does not exist in fx instance", name, channel_id );
	assert( error == LIVIDO_NO_ERROR );
#endif
	error = vevo_property_set( channel  , "fps"	, LIVIDO_ATOM_TYPE_DOUBLE,1, &(frame->fps));
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	int	rowstrides[4] = { frame->width, frame->uv_width, frame->uv_width, 0 };
	error = vevo_property_set( channel  , "rowstrides", LIVIDO_ATOM_TYPE_INT,4, &rowstrides );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	error = vevo_property_set( channel  , "timecode", LIVIDO_ATOM_TYPE_DOUBLE,1, &(frame->timecode));
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	pd[0] = (void*) frame->data[0];
	pd[1] = (void*) frame->data[1];
	pd[2] = (void*) frame->data[2];
	pd[3] = (void*) frame->data[3];

	error = vevo_property_set( channel, "pixel_data",LIVIDO_ATOM_TYPE_VOIDPTR, 4, &pd);	
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	int hsampling = 0;
	if( vevo_property_get( channel, "HOST_sampling", 0, &hsampling ) == LIVIDO_NO_ERROR && name[0] == 'i')
	{
		void *sampler = NULL;
		error = vevo_property_get(channel, "HOST_sampler", 0, &sampler );
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif
		chroma_supersample( hsampling, sampler, pd, frame->width,
				frame->height );
	}
	
	return 1;
}

int	livido_plug_parameter_set_text( void *parameter, void *value )
{
	veejay_msg(0,"%s: value = '%s'", __FUNCTION__, *((char*) value ));
	char *new_val = *( (char*) value );
	int   len     = (new_val == NULL ? 0 : strlen( new_val ));
	if( len > 0 )
	{
		int error = vevo_property_set( parameter, "value", LIVIDO_ATOM_TYPE_STRING, 1, value );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		return 1;
	}
	return 0;
}

int	livido_plug_get_index_parameter_as_dbl( void *fx_instance, const char *key,int k, double *res )
{
	void *parameter = NULL;
	int error = vevo_property_get( fx_instance, key, k, &parameter );
	if(error != VEVO_NO_ERROR )
		return 0;
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
	assert( parameter != NULL );
#endif
	int value = 0;
	error = vevo_property_get( parameter, "value", 0, &value );
	if( error == VEVO_NO_ERROR )
	{
		*res = value;
		return 0;
	}	
	return 1;
}
int	livido_plug_get_number_parameter_as_dbl( void *fx_instance,const char *key, int k, double *res )
{
	void *parameter = NULL;
	int error = vevo_property_get( fx_instance, key, k, &parameter );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
	assert( parameter != NULL );
#endif
	double value = 0.0;
	error = vevo_property_get( parameter, "value", 0, &value );
	if( error == VEVO_NO_ERROR )
	{
		*res = value;
		return 0;
	}
	return 1;
}
int	livido_plug_get_coord_parameter_as_dbl( void *fx_instance,const char *key, int k, double *res_x, double *res_y )
{
	void *parameter = NULL;
	int error = vevo_property_get( fx_instance, key, k, &parameter );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
	assert( parameter != NULL );
#endif
	double value[2];
	error = vevo_property_get( parameter, "value", 0, &value );
	if( error == VEVO_NO_ERROR )
	{
		*res_x = value[0];
		*res_y = value[1];
		return 0;
	}
	return 1;
}
int	livido_plug_parameter_get_range_dbl( void *fx_instance,const char *key, int k, double *min, double *max, int *dkind )
{
	void *parameter = NULL;
	int error = vevo_property_get( fx_instance, key, k, &parameter );
	if(error != VEVO_NO_ERROR )
		return 0;
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
	assert( parameter != NULL );
#endif
	void *parameter_templ = NULL;
	error = vevo_property_get( parameter, "parent_template", 0, &parameter_templ );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
/*	if(error  != VEVO_NO_ERROR )
	{
		*min = 0.0;
		*max = 1.0;
		*dkind =HOST_PARAM_NUMBER;
		veejay_msg(0, "No parent template in output parameter, working arround");
		return VEVO_NO_ERROR;
	}*/
	int kind = 0;
	error = vevo_property_get( parameter_templ, "HOST_kind", 0, &kind );
#ifdef STRICT_CHECING
	assert( error == VEVO_NO_ERROR );
#endif
	int irange[2];

	if (kind == HOST_PARAM_NUMBER )
	{
		error = vevo_property_get( parameter_templ, "min",0, min );
		error = vevo_property_get( parameter_templ, "max",0, max );
		*dkind = HOST_PARAM_NUMBER;
		return VEVO_NO_ERROR;
	}	
	else if(kind == HOST_PARAM_INDEX )
	{
		error = vevo_property_get( parameter_templ, "min", 0 , &(irange[0]) );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		error = vevo_property_get( parameter_templ, "max", 0, &(irange[1]) );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		*min = (double) irange[0];
		*max = (double) irange[1];
		*dkind = HOST_PARAM_INDEX;
		return VEVO_NO_ERROR;
	}
		
	return 1;
}

int	livido_plug_parameter_set_number( void *parameter, void *value )
{
	double range[2];
	void *templ = NULL;
	int error = vevo_property_get( parameter, "parent_template",0, &templ );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	error = vevo_property_get( templ, "min", 0 , &(range[0]) );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	error = vevo_property_get( templ, "max", 0, &(range[1]) );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	double new_val = *((double*) value);
	if( new_val >= range[0] && new_val <= range[1] )
	{
		error = vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_DOUBLE, 1, value );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		return 1;
	}
	else
	{
		char *name = get_str_vevo(templ, "name");
		veejay_msg(0, "Parameter '%s' value %g out of range %g - %g", name,new_val, range[0],range[1]);
		free(name);
	}
	return 0;
}

int	livido_plug_parameter_set_index( void *parameter, void *value)
{
	int range[2];
	void *templ = NULL;
	int error = vevo_property_get( parameter, "parent_template",0, &templ );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	error = vevo_property_get( templ, "min", 0 , &(range[0]) );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	error = vevo_property_get( templ, "max", 0, &(range[1]) );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	int new_val = *((int*) value);
	if( new_val >= range[0] && new_val <= range[1] )
	{
		error = vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_INT, 1, value );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		return 1;
	}
	else
	{
		char *name = get_str_vevo(templ, "name");
		veejay_msg(0, "Parameter '%s' value %d out of range %d - %d", name,new_val, range[0],range[1]);
		free(name);
	}
	return 0;
}

int	livido_plug_parameter_set_bool( void *parameter, void *value )
{	
	int range[2];
	void *templ = NULL;
	int error; 
	int new_val = *((int*) value);

	if( new_val >= 0 && new_val <= 1 )
	{
		error = vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_BOOL, 1, value );
#ifdef STRICT_CHECKING
		if( error != VEVO_NO_ERROR )
			 veejay_msg(0, "%s: error code %x", __FUNCTION__, error );
		assert( error == VEVO_NO_ERROR );
#endif
		return 1;
	}
	else
	{
		int error = vevo_property_get( parameter, "parent_template",0, &templ );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		char *name = get_str_vevo(templ, "name");
		veejay_msg(0, "Parameter '%s' value %d out of range %d - %d", name,new_val, range[0],range[1]);
		free(name);
	}
	return 0;
}

int	livido_plug_parameter_set_color( void *parameter,void *value )
{
	veejay_msg(0,"%s: array", __FUNCTION__);
//	vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_DOUBLE, 4, value );
	return 0;
}

int	livido_plug_parameter_set_coord( void *parameter, void *value )
{
	veejay_msg(0,"%s: array", __FUNCTION__);
//	vevo_property_set( parameter, "value", LIVIDO_ATOM_TYPE_DOUBLE, 2, value );
	return 0;
}

static	int	livido_pname_to_host_kind( const char *str )
{
	if (strcasecmp( str, "NUMBER" ) == 0 ) {
		return HOST_PARAM_NUMBER;
	}
	else if(strcasecmp(str, "INDEX" ) == 0 ) {
		return HOST_PARAM_INDEX;
	}
	else if(strcasecmp(str, "SWITCH") == 0 ) {
		return HOST_PARAM_SWITCH;
	}
	else if(strcasecmp(str, "COORD") == 0 ) {
		return HOST_PARAM_COORD;
	}
	else if(strcasecmp(str, "COLOR") == 0 ) {
		return HOST_PARAM_COLOR;
	}
	else if(strcasecmp(str, "TEXT") == 0 ) {
		return HOST_PARAM_TEXT;
	}
	return 0;
}

static	int	livido_scan_out_parameters( void *plugin , void *plugger_port)
{
	int n = 0;
	int vj_np = 0;
	int NP = vevo_property_num_elements( plugin , "out_parameter_templates");
	int error = 0;

	if( NP <= 0 )
		return 0;

	for( n = 0; n < NP; n ++ )
	{
		char key[20];
		void *param = NULL;

		error = vevo_property_get( plugin, "out_parameter_templates", n, &param );
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif

		sprintf(key, "p%02d", n );

		int ikind = 0;
		char *kind = vevo_property_get_string( param, "kind" );
		//get_str_vevo( param, "kind" );

#ifdef STRICT_CHECKING
		if( kind == NULL )
			veejay_msg(0, "\tParameter %d of %d has no property kind", n,NP );
		assert( kind != NULL );
#endif


		ikind = livido_pname_to_host_kind(kind);

		vevo_property_set( param, "HOST_kind", VEVO_ATOM_TYPE_INT,1,&ikind );	
		void *vje_port = vpn( VEVO_VJE_PORT );
		vevo_property_set( plugger_port, key, LIVIDO_ATOM_TYPE_PORTPTR,1, &vje_port );


		free(kind);
	}
	return NP;
}

static	int	livido_scan_parameters( void *plugin, void *plugger_port )
{
	int n = 0;
	int vj_np = 0;
	int NP = vevo_property_num_elements( plugin , "in_parameter_templates");
	int error = 0;

	if( NP <= 0 )
		return 0;

	for( n = 0; n < NP; n ++ )
	{
		char key[20];
		void *param = NULL;

		error = vevo_property_get( plugin, "in_parameter_templates", n, &param );
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif

		sprintf(key, "p%02d", n );

		int ikind = 0;
		char *kind = get_str_vevo( param, "kind" );

#ifdef STRICT_CHECKING
		assert( kind != NULL );
#endif	
		void *vje_port = vpn( VEVO_VJE_PORT );
		int tmp[4];  
		double dtmp[4];

		error = vevo_property_set( plugger_port, key, LIVIDO_ATOM_TYPE_PORTPTR,1, &vje_port );
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif

		if(strcasecmp(kind, "NUMBER") == 0 ) {
			ikind = HOST_PARAM_NUMBER; vj_np ++;
			clone_prop_vevo( param, vje_port, "default", "value" );
			clone_prop_vevo( param, vje_port, "default", "default" );
			clone_prop_vevo( param, vje_port, "min", "min" );
			clone_prop_vevo( param, vje_port, "max", "max" );
	/*		double a = 0.0;
			double b = 0.0;
			error = vevo_property_get( vje_port,"value" ,0, &a );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif
			error = vevo_property_get( param,"default" ,0, &b );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif
#ifdef STRICT_CHECKING
			if( a != b )
			{
				veejay_msg(0,
					"value is '%g', should be %g",a,b);
			}
			assert( a == b );
#endif			*/
		} else if (strcasecmp(kind, "INDEX") == 0 ) {
			ikind = HOST_PARAM_INDEX; vj_np ++;
			clone_prop_vevo( param, vje_port, "default", "value" );
			clone_prop_vevo( param, vje_port, "default", "default" );
			clone_prop_vevo( param, vje_port, "min", "min" );
			clone_prop_vevo( param, vje_port, "max", "max" );
		} else if (strcasecmp(kind, "SWITCH") == 0 ) {
			ikind = HOST_PARAM_SWITCH; vj_np ++;
			clone_prop_vevo( param, vje_port, "default", "value" );
			clone_prop_vevo( param, vje_port, "default", "default" );
			tmp[0] = 0; tmp[1] = 1;
			vevo_property_set(vje_port, "min", VEVO_ATOM_TYPE_BOOL,1, &tmp[0] );
			vevo_property_set(vje_port, "max", VEVO_ATOM_TYPE_BOOL,1, &tmp[1] );
		} else if (strcasecmp(kind, "COORD" ) == 0 ) {
			ikind = HOST_PARAM_COORD; vj_np += 2;
			dtmp[0] = 0.0; dtmp[1] = 0.0;
			vevo_property_set(vje_port, "min", VEVO_ATOM_TYPE_DOUBLE,1, &dtmp[0] );
			dtmp[1] = 1.0; dtmp[0] = 1.0;
			vevo_property_set(vje_port, "max", VEVO_ATOM_TYPE_DOUBLE,1, &dtmp[1] );
			double *dv = get_dbl_arr_vevo( vje_port, "default" );
			vevo_property_set(vje_port, "default", VEVO_ATOM_TYPE_DOUBLE,2,&dv );
			vevo_property_set(vje_port, "value", VEVO_ATOM_TYPE_DOUBLE,2,&dv );
			free(dv);
		} else if (strcasecmp(kind, "COLOR" ) == 0 ) {
			ikind = HOST_PARAM_COLOR; vj_np += 3; // fixme, should be 4
			dtmp[0] = 0.0; dtmp[1] = 0.0; dtmp[2] = 0.0; dtmp[3] = 0.0;
			vevo_property_set(vje_port, "min", VEVO_ATOM_TYPE_DOUBLE,4, &dtmp );
			dtmp[0] = 1.0; dtmp[1] = 1.0; dtmp[2] = 1.0; dtmp[3] = 1.0;
			vevo_property_set(vje_port, "max", VEVO_ATOM_TYPE_DOUBLE,4, &dtmp );
			double *dv = get_dbl_arr_vevo( vje_port, "default" );
			vevo_property_set(vje_port, "default", VEVO_ATOM_TYPE_DOUBLE,2,&dv );
			free(dv);
		} else if (strcasecmp(kind, "TEXT" ) == 0 ) {
			ikind = HOST_PARAM_TEXT; 
			vj_np ++;  
#ifdef STRICT_CHECKING
			assert(0); //@implement me
#endif
		}
		vevo_property_set( param, "HOST_kind", VEVO_ATOM_TYPE_INT,1,&ikind );	
		vevo_property_set( vje_port, "HOST_kind", VEVO_ATOM_TYPE_INT,1,&ikind );

		free(kind);	

	}
	return vj_np;
}

static	int	init_parameter_port(livido_port_t *ptr, livido_port_t *in_param )
{
	int kind = 0;
	int error = vevo_property_get( ptr, "HOST_kind", 0, &kind );

	if( error != VEVO_NO_ERROR )
	{
		veejay_msg(0, "\tProperty 'HOST_kind' not set in parameter");
		return 0;
	}
	livido_set_parameter_f pctrl;
				
	switch(kind)
	{
		case HOST_PARAM_INDEX:
		case HOST_PARAM_NUMBER:
		case HOST_PARAM_SWITCH:
			pctrl = livido_plug_parameter_set_index; 
			break;
//		case HOST_PARAM_NUMBER:
//			pctrl = livido_plug_parameter_set_number; break;
//		case HOST_PARAM_SWITCH:
//			pctrl = livido_plug_parameter_set_bool; break;
//@ FIXME: these are not yet supported
//		case HOST_PARAM_COORD:
//			pctrl = livido_plug_parameter_set_coord; break;
//		case HOST_PARAM_COLOR:
//			pctrl = livido_plug_parameter_set_color; break;
//		case HOST_PARAM_TEXT:
//			pctrl = livido_plug_parameter_set_text; break;
#ifdef STRICT_CHECKING
		default:
			veejay_msg(0, "Invalid kind : '%d'", kind );
			return 0;
			break;
#endif
	}
	
	vevo_property_set( in_param, "HOST_parameter_func", LIVIDO_ATOM_TYPE_VOIDPTR,1,&pctrl );
	return 1;
}

static	int	match_palette(livido_port_t *ptr, int palette )
{
	int p;
	int np = vevo_property_num_elements( ptr, "palette_list" );
	int error = 0;
	for( p = 0; p < np; p ++ )
	{
		int ppalette = 0;
		error = vevo_property_get( ptr, "palette_list", p, &ppalette );
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif
		if( palette == ppalette )
			return 1;
	}
	return 0;
}

static	int	find_cheap_palette(livido_port_t *c, livido_port_t *ptr , int w)
{
	int palette = LIVIDO_PALETTE_YUV444P;
	if( match_palette(ptr,palette ))
	{
		void *sampler = subsample_init(w);
		int   mode    = (pref_palette_ == LIVIDO_PALETTE_YUV422P ? SSM_422_444 : SSM_420_JPEG_BOX);
		
		vevo_property_set( c, "HOST_sampler", LIVIDO_ATOM_TYPE_VOIDPTR,
				1,&sampler);
		vevo_property_set( c, "HOST_sampling", LIVIDO_ATOM_TYPE_INT,
				1, &mode );
		vevo_property_set( c, "current_palette", LIVIDO_ATOM_TYPE_INT,
				1, &palette );
		return 1;
	}
	else {
		veejay_msg(0, "Support me :%s",__FUNCTION__);
	}
	return 0;
}

static	int	init_channel_port(livido_port_t *ptr, livido_port_t *in_channel, int w, int h)
{
	int np = vevo_property_num_elements( ptr, "palette_list" );
	int p = 0;
	int palette = 0;
	int plug_pp = 0;
	int flags = 0;
	int error = vevo_property_get( ptr, "palette_list", 0, &plug_pp );
	
	if( np < 0 )
		return 0;
	
	if( match_palette( ptr, pref_palette_ ))
		vevo_property_set( in_channel, "current_palette", LIVIDO_ATOM_TYPE_INT,1,&pref_palette_ );
	else
	{
		if(!find_cheap_palette(in_channel ,ptr,w))
		{
			veejay_msg(0, "No support for any palette in plugin");
			return 0;
		}
	}	

	error = vevo_property_get( ptr, "flags", 0, &flags );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	livido_property_set( in_channel, "width",  LIVIDO_ATOM_TYPE_INT,1,&w );
	livido_property_set( in_channel, "height", LIVIDO_ATOM_TYPE_INT,1,&h );
	livido_property_set( in_channel, "flags",  LIVIDO_ATOM_TYPE_INT,1,&flags );


	error = vevo_property_get( in_channel, "current_palette",0,NULL );
	if( error != LIVIDO_NO_ERROR )
	{
		veejay_msg(0, "No suitable palette found.");
		return 0;
	}
	
	return 1;
}

//@ w x h as HOST_width, HOST_height
/* utility function to instantiate a port from a template, only sets parent_template */
static	int	init_ports_from_template( livido_port_t *filter_instance, livido_port_t *template, int id, const char *name, const char *iname, int w, int h, int host_palette )
{
	int num = 0;
        int i;
	int error = 0;
	error = livido_property_get( template, name, 0, NULL);

	if( error != VEVO_NO_ERROR )
		return 0;
	
        num = livido_property_num_elements( template, name );

        if(num <= 0)
                return 0;

        livido_port_t *in_channels[num];

	for( i = 0; i < num;  i ++ )
        {
                livido_port_t *ptr = NULL;
                error = livido_property_get( template, name, i, &ptr );
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif
                in_channels[i] = vpn( id ); //@ is this ever freed?!
		livido_property_set( in_channels[i], "parent_template",LIVIDO_ATOM_TYPE_PORTPTR,1, &ptr);
		livido_property_soft_reference( in_channels[i], "parent_template" );
;
		if( id == LIVIDO_PORT_TYPE_CHANNEL )
		{
			if(!init_channel_port( ptr,in_channels[i],w,h))
			{
			       veejay_msg(0,
					       "Unable to intialize output channel %d ",i );
			       return -1;	
			}
		}
		else if( id == LIVIDO_PORT_TYPE_PARAMETER )
		{
			if(!init_parameter_port( ptr, in_channels[i] ))
			{
				veejay_msg(0, "Unable to initialize output parameter %d", i);
				return -1;
			}
		}
#ifdef STRICT_CHECKING
		else
		{ 
			assert(0);
		}
#endif
	}

        
	livido_property_set( filter_instance, iname, LIVIDO_ATOM_TYPE_PORTPTR,num, in_channels );

//	veejay_msg(0, "%s --> %s",__FUNCTION__, name);
//	livido_property_soft_reference( filter_instance, name );

	return num;
}

char	*livido_describe_parameter_format_osc( void *instance, int p )
{
	void *param = NULL;
	void *param_templ = NULL;
	int error = vevo_property_get( instance, "in_parameters", p, &param );
	if(error != VEVO_NO_ERROR )
	{
		veejay_msg(0, "Input parameter %d does not exist ", p );
		return NULL;
	}
	error = vevo_property_get( param, "parent_template",0,&param_templ);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	int kind = 0;
	error = vevo_property_get( param_templ, "HOST_kind",0,&kind );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	int n_elems = vevo_property_num_elements( param_templ, "default" );

#ifdef STRICT_CHECKING
	assert( n_elems > 0 );
#endif
	
	char fmt[5];

	bzero(fmt,5);
	
	switch(kind)
	{
		case HOST_PARAM_INDEX:
			fmt[0] = 'i';
			break;
		case HOST_PARAM_NUMBER:
			fmt[0] = 'd';
			break;
		case HOST_PARAM_SWITCH:
			fmt[0] = 'i';
			break;
		case HOST_PARAM_COORD:
			fmt[0] = 'd';
			fmt[1] = 'd';
			break;
		case HOST_PARAM_COLOR:
			fmt[0] = 'd';
			fmt[1] = 'd';
			fmt[2] = 'd';
			break;
		case HOST_PARAM_TEXT:
			fmt[0] = 's';
			break;
		default:
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;
	}

	char *res = strdup( fmt );
	return res;
}

void	livido_plug_free_namespace( void *fx_instance , void *data )
{
	void *osc_namespace = NULL;
	int error = vevo_property_get( fx_instance, "HOST_osc",0,&osc_namespace);
//@FIXME
//	if( error == VEVO_NO_ERROR)
//		veejay_osc_del_methods( data,osc_namespace,fx_instance, fx_instance );
}

int	livido_plug_build_namespace( void *plugin_template , int entry_id, void *fx_instance , void *data, int sample_id,
		generic_osc_cb_f osc_cb_f, void *osc_data)
{
	void *plug_info = NULL;
	void *filter_templ = NULL;
	int flags =0;
	int error = vevo_property_get( plugin_template, "instance", 0, &plug_info);

#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	error = vevo_property_get( plug_info, "filters",0,&filter_templ);
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	error = vevo_property_set( fx_instance, "HOST_osc_cb", VEVO_ATOM_TYPE_VOIDPTR,1,&osc_cb_f );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	error = vevo_property_set( fx_instance, "HOST_data", VEVO_ATOM_TYPE_VOIDPTR,1,&osc_data );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif


	int n_in = vevo_property_num_elements( filter_templ, "in_parameter_templates" );
	int i;
	if( n_in <= 0)
	{
		return 0; // no namespace needed yet
	}
	
	char *plug_name = get_str_vevo( filter_templ, "name" );

	char base[256];
	char mpath[256];
	void *osc_namespace = vpn(VEVO_ANONYMOUS_PORT);
	
	for( i = 0; i < n_in ; i ++ )
	{
		void *parameter = NULL;
		vevo_property_get( fx_instance, "in_parameters", i, &parameter );	
	
		void *param_templ = NULL;
		error = vevo_property_get( filter_templ, "in_parameter_templates", i, &param_templ );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		char *param_name = get_str_vevo( param_templ, "name" );
		char *descrip    = get_str_vevo( param_templ, "description" );
		
		sprintf(base, "/sample_%d/fx_%d/%s",
				sample_id,
				entry_id,
				plug_name );
		sprintf(mpath, "/sample_%d/fx_%d/%s/%s",
				sample_id,
				entry_id,
				plug_name,
		      		param_name );


		char *format = livido_describe_parameter_format_osc( fx_instance ,i);
		
		char *ppo = veejay_valid_osc_name( mpath );
		error = vevo_property_set( parameter, "HOST_osc_path",VEVO_ATOM_TYPE_STRING, 1, &ppo );	
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		error = vevo_property_set( parameter, "HOST_osc_types", VEVO_ATOM_TYPE_STRING,1,&format );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif

		free(ppo);
/*		plugin_new_event(
				data,
				osc_namespace,
				fx_instance,
				base,
				param_name,
			        format,
				NULL,
				descrip,
			       	NULL,
				i,
			       	param_templ	);
		*/
		//@ FIXME: OMC
		free(param_name);
		free(format);
		free(descrip);

	}	

	error = vevo_property_set( fx_instance, "HOST_osc", LIVIDO_ATOM_TYPE_PORTPTR,1,&osc_namespace);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	free(plug_name);
	
	veejay_msg(0, "End of OSC namespace");	
	return n_in;
}

void	*livido_get_name_space( void *instance )
{
	void *space = NULL;
	int error = vevo_property_get( instance, "HOST_osc", 0, &space );
	if( error != VEVO_NO_ERROR )
		return NULL;
	return space;
}

/* initialize a plugin */
void	*livido_plug_init(void *plugin,int w, int h )
{
	void *plug_info = NULL;
	void *filter_templ = NULL;
	int flags =0;
	int error = vevo_property_get( plugin, "instance", 0, &plug_info);

#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	error = vevo_property_get( plug_info, "filters",0,&filter_templ);
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	void *filter_instance = vpn( LIVIDO_PORT_TYPE_FILTER_INSTANCE );
	int num_in_channels = init_ports_from_template(
			filter_instance, filter_templ,
			LIVIDO_PORT_TYPE_CHANNEL,
			"in_channel_templates", "in_channels",
			w,h, 0);

	if( num_in_channels < 0 )
	{
		veejay_msg(0 ,"Not dealing with generator plugins yet");
		return NULL;
	}
	
	int num_out_channels = init_ports_from_template( 
			filter_instance, filter_templ,
			LIVIDO_PORT_TYPE_CHANNEL,
			"out_channel_templates", "out_channels",
			w,h, 0 );
	
	if( num_out_channels < 0 )
	{
		veejay_msg(0, "Require at least 1 output channel");
		return  NULL;
	}
	int num_in_params = init_ports_from_template( 
			filter_instance, filter_templ,
			LIVIDO_PORT_TYPE_PARAMETER,
			"in_parameter_templates", "in_parameters",
			w,h, 0 );

	if( num_in_params < 0 )
	{
		veejay_msg(0, "Require at least 0 input parameter");
		return NULL;
	}
	int num_out_params = init_ports_from_template(
				filter_instance, filter_templ,
				LIVIDO_PORT_TYPE_PARAMETER,
				"out_parameter_templates", "out_parameters",
				w,h,0 );

	if( num_out_params < 0 )
	{
		veejay_msg(0, "Require at least 0 output parameters (%d)",
				num_out_params);
		return NULL;
	}
#ifdef STRICT_CHECKING
	assert( num_in_params >= 0 );
	assert( num_out_params >= 0 );
	assert( num_in_channels >= 0 );
	assert( num_out_channels >= 0 );
#endif

	//@ call livido init
	livido_init_f init_f;
	error = vevo_property_get( filter_templ, "init_func", 0, &init_f );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	error = (*init_f)( (livido_port_t*) filter_instance );
	if( error != LIVIDO_NO_ERROR ) {
		return NULL;
		//@ FIXME: leak
	}

#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	//@ ok, finish
	vevo_property_set( filter_instance, "filter_templ", VEVO_ATOM_TYPE_PORTPTR,1, &filter_templ );
	vevo_property_soft_reference( filter_instance, "filter_templ" );


	//@ prepare function pointers for plugloader to call
	generic_process_f	gpf = livido_plug_process;
	vevo_property_set( filter_instance, "HOST_plugin_process_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpf ); 	
	generic_push_channel_f		gpu = livido_push_channel;
	vevo_property_set( filter_instance, "HOST_plugin_push_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpu );
	generic_default_values_f	gdv = livido_plug_retrieve_values;
	vevo_property_set( filter_instance, "HOST_plugin_defaults_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gdv );
	generic_push_parameter_f	gpp = livido_set_parameter;
	vevo_property_set( filter_instance, "HOST_plugin_param_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpp );
	generic_clone_parameter_f	gcc = livido_clone_parameter;
	vevo_property_set( filter_instance, "HOST_plugin_param_clone_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gcc );	
	generic_reverse_clone_parameter_f grc = livido_reverse_clone_parameter;
	vevo_property_set( filter_instance, "HOST_plugin_param_reverse_f", VEVO_ATOM_TYPE_VOIDPTR,1,&grc );
	generic_reverse_clone_out_parameter_f gro = livido_plug_read_output_parameters;
	vevo_property_set( filter_instance, "HOST_plugin_out_param_reverse_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gro );

	
	generic_deinit_f		gin = livido_plug_deinit;
	vevo_property_set( filter_instance, "HOST_plugin_deinit_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gin);

	return filter_instance;
}


static void	livido_push_channel_local( void *instance,const char *key, int n, VJFrame *frame ) // in_channels / out_channels
{
	int error;
#ifdef STRICT_CHECKING
	int num_in_channels = vevo_property_num_elements( instance, key);
	if( n < 0 || n >= num_in_channels )
		veejay_msg(0, "%s: Cannot push channel %d", __FUNCTION__ , n );
	assert( n >= 0 && n < num_in_channels );
	assert( frame != NULL );
#endif	
	configure_channel( instance, key, n, frame );
}

void	livido_push_channel( void *instance,int n,int dir, VJFrame *frame ) // in_channels / out_channels
{

	char *key = (dir == 0 ? "in_channels" : "out_channels" );
	livido_push_channel_local(instance, key, n, frame );

}

void	livido_plug_process( void *instance, double time_code )
{
	void *filter_templ = NULL;
	int error = vevo_property_get( instance, "filter_templ",0,&filter_templ);
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	livido_process_f process;
	error = vevo_property_get( filter_templ, "process_func", 0, &process );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
	assert( (*process) != NULL );
#endif

	(*process)( instance, 0.0 );

	//see if output channel needs downsampling
	void *channel = NULL;
	int hsampling = 0;

	error = vevo_property_get( instance, "out_channels", 0, &channel );

	if( error == LIVIDO_NO_ERROR &&  vevo_property_get( channel, "HOST_sampling",0,&hsampling ) ==
			LIVIDO_NO_ERROR )
	{
		void *sampler = NULL;
		error = vevo_property_get( channel, "HOST_sampler",0,&sampler);
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif
		uint8_t *pd[4];
		int n = 0;
		int w = 0;
		int h = 0;
		for( n = 0; n < 4; n ++ )
			vevo_property_get( channel, "pixel_data",n,&pd[n]);
		vevo_property_get( channel, "width", 0, &w );
		vevo_property_get( channel, "height", 0, &h );
		
		chroma_subsample( hsampling,sampler,pd,w,h );
	}
}

void	livido_plug_deinit( void *instance )
{
	void *filter_templ = NULL;	
	int error = vevo_property_get( instance, "filter_templ", 0, &filter_templ );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	livido_deinit_f deinit;
	error = vevo_property_get( filter_templ, "HOST_plugin_deinit_func", 0, &deinit );
#ifdef STRICT_CHECKING
	char *plugin_name =  get_str_vevo( filter_templ, "name" );
	veejay_msg(VEEJAY_MSG_DEBUG, "Destroy '%s'", plugin_name );
	free(plugin_name);
#endif

	if( error == VEVO_NO_ERROR )
		(*deinit)( instance );
	
	int n;
	int np = vevo_property_num_elements( instance, "in_channels" );
	
	for( n = 0; n < np ; n ++ )
	{
		void *ic = NULL;
		int   hs = 0;
		error = vevo_property_get( instance, "in_channels",n, &ic );
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif
		if( vevo_property_get( ic, "HOST_sampling",0,&hs ) == LIVIDO_NO_ERROR )
		{
			void *sampler = NULL;
			error = vevo_property_get( ic, "HOST_sampler", 0, &sampler );
#ifdef STRICT_CHECKING
			assert( error == LIVIDO_NO_ERROR );
#endif
			subsample_free(sampler);
		}
			
	}

	void *channel = NULL;
	int hsampling = 0;
	error = vevo_property_get( instance, "out_channels", 0, &channel );
	
	if( vevo_property_get( channel, "HOST_sampling",0,&hsampling ) ==
			LIVIDO_NO_ERROR )
	{
		void *sampler = NULL;
		error = vevo_property_get(channel, "HOST_sampler",0,&sampler );
		if(error == VEVO_NO_ERROR )
			subsample_free(sampler);
	}

	livido_port_recursive_free( instance );

//	instance = NULL;
}
//get plugin defaults
void	livido_plug_retrieve_values( void *instance, void *fx_values )
{
	int vj_np = vevo_property_num_elements( instance, "in_parameters" );
	int i;
	for( i = 0; i < vj_np; i ++ )
	{
		char	vkey[10];
		void *param = NULL;
		void *param_templ = NULL;
		int error = vevo_property_get( instance, "in_parameters", i, &param);
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif

		error = vevo_property_get( param, "parent_template", 0, &param_templ );
#ifdef STRICT_CHECKING
		if( error != LIVIDO_NO_ERROR )
			veejay_msg(0,"%s: parent_template not found in Parameter %d/%d, error code %d",  __FUNCTION__, i, vj_np,error );
		assert( error == LIVIDO_NO_ERROR );
#endif

		sprintf(vkey, "p%02d", i );
		clone_prop_vevo( param_templ, fx_values, "default", vkey );
	}
}

int	livido_plug_read_output_parameters( void *instance, void *fx_values )
{
	int np = vevo_property_num_elements( instance, "out_parameters" );
	int i;

	if(np <= 0)
		return 0;

	for( i = 0; i < np ; i ++ )
	{
		char	vkey[10];
		void *param = NULL;
		void *param_templ = NULL;

		int error = vevo_property_get( instance, "out_parameters", i, &param );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		sprintf(vkey, "p%02d", i );
		clone_prop_vevo( param, fx_values, "value", vkey);

	}
	return 1;
}
char	*livido_describe_parameter_format( void *instance, int p )
{
	void *param = NULL;
	void *param_templ = NULL;
	int error = vevo_property_get( instance, "in_parameters", p, &param );
	if(error != VEVO_NO_ERROR )
		return 0;
	error = vevo_property_get( param, "parent_template",0,&param_templ);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	int kind = 0;
	error = vevo_property_get( param_templ, "HOST_kind",0,&kind );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	int n_elems = vevo_property_num_elements( param, "value" );

#ifdef STRICT_CHECKING
	assert( n_elems > 0 );
#endif
	
	char fmt[5];

	bzero(fmt,5);
	
	switch(kind)
	{
		case HOST_PARAM_INDEX:
			fmt[0] = 'd';
			break;
		case HOST_PARAM_NUMBER:
			fmt[0] = 'g';
			break;
		case HOST_PARAM_SWITCH:
			fmt[0] = 'd';
			break;
		case HOST_PARAM_COORD:
			fmt[0] = 'g';
			fmt[1] = 'g';
			break;
		case HOST_PARAM_COLOR:
			fmt[0] = 'g';
			fmt[1] = 'g';
			fmt[2] = 'g';
			break;
		case HOST_PARAM_TEXT:
			fmt[0] = 's';
			break;
	}

	char *res = strdup( fmt );
	return res;
}


int	livido_set_parameter_from_string( void *instance, int p, const char *str, void *fx_values )
{
	void *param = NULL;
	void *param_templ = NULL;
	int error = vevo_property_get( instance, "in_parameters", p, &param );
	if(error != VEVO_NO_ERROR )
		return 0;
	error = vevo_property_get( param, "parent_template",0,&param_templ);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	int kind = 0;
	error = vevo_property_get( param_templ, "HOST_kind",0,&kind );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
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

// set plugin defaults
void	livido_reverse_clone_parameter( void *instance, int seq, void *fx_value_port )
{
	int vj_np = vevo_property_num_elements( instance, "in_parameters" );
	int i;

	for( i = 0; i < vj_np; i ++ )
	{
		char	vkey[10];
		void *param = NULL;
		int error = vevo_property_get( instance, "in_parameters", i, &param);
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif
		sprintf(vkey, "p%02d", i );
		clone_prop_vevo( param, fx_value_port, vkey, "value"  );
	}
}

void	livido_clone_parameter( void *instance, int seq, void *fx_value_port )
{
	int vj_np = vevo_property_num_elements( instance, "in_parameters" );
	int i;
	for( i = 0; i < vj_np; i ++ )
	{
		char	vkey[10];
		void *param = NULL;
		int error = vevo_property_get( instance, "in_parameters", i, &param);
#ifdef STRICT_CHECKING
		assert( error == LIVIDO_NO_ERROR );
#endif
		sprintf(vkey, "p%02d", i );
		clone_prop_vevo( fx_value_port, param,vkey, "value"  );
	//	clone_prop_vevo( param, fx_value_port, vkey, "value" );
	}

	
}

static void	livido_notify_parameter_update( void *instance ,void *param, void *value )
{
	generic_osc_cb_f cbf;
	int error = vevo_property_get( instance, "HOST_osc_cb", 0, &cbf );
	if( error == VEVO_NO_ERROR )
	{
		void *userdata = NULL;
		error = vevo_property_get( instance, "HOST_data",0, &userdata);
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		(*cbf)( userdata, param, value );
	}
}

void	livido_set_parameter( void *instance, int seq, void *value )
{
	void *param = NULL;
	void *param_templ = NULL;
	int error = vevo_property_get( instance, "in_parameters", seq, &param);
	if( error == LIVIDO_NO_ERROR )
	{
		livido_set_parameter_f pctrl;
		error = vevo_property_get( param, "HOST_parameter_func", 0, &pctrl );
#ifdef STRICT_CHECKING
		assert( error == 0 );
#endif
		if( (*pctrl)( param, value ) )
			livido_notify_parameter_update( instance,param, value );
	}
}

#ifdef STRICT_CHECKING
void	*livido_plugin_port_new( int id )
{
	return vpn(id);
}
#endif

void*	deal_with_livido( void *handle, const char *name )
{
	void *port = vpn( VEVO_LIVIDO_PORT );
	char *plugin_name = NULL;
	int lvd = 1;
	int type = VEVO_LIVIDO_PORT;

	livido_setup_f livido_setup = dlsym( handle, "livido_setup" );

#ifndef STRICT_CHECKING
	livido_setup_t setup[] = {
		{	(void(*)())vj_malloc 			},	
		{	(void(*)())free			},
		{	(void(*)())veejay_memset			},
        {	(void(*)())veejay_memcpy			},
        {	(void(*)())vevo_port_new		},
        {	(void(*)())vevo_port_free		},
        {	(void(*)())vevo_property_set		},
        {	(void(*)())vevo_property_get		},
        {	(void(*)())vevo_property_num_elements	},
        {	(void(*)())vevo_property_atom_type	},
        {	(void(*)())vevo_property_element_size	},
        {	(void(*)())vevo_list_properties		},
		{	(void(*)())livido_dummy_function },
		{ 	(void(*)())livido_dummy_function },


	};
#else
	livido_setup_t setup[] = {
		{	(void(*)()) vj_malloc_ 			},	
		{	(void(*)()) free			},
		{	(void(*)())memset			},
        {	(void(*)())memcpy			},
        {	(void(*)())vevo_port_new	},
        {	(void(*)())vevo_port_free		},
        {	(void(*)())vevo_property_set		},
        {	(void(*)())vevo_property_get		},
        {	(void(*)())vevo_property_num_elements	},
        {	(void(*)())vevo_property_atom_type	},
        {	(void(*)())vevo_property_element_size	},
        {	(void(*)())vevo_list_properties		},
		{	(void(*)())livido_dummy_function },
		{ 	(void(*)())livido_dummy_function },
	};


#endif

	void *livido_plugin = livido_setup( setup, LIVIDO_API_VERSION );
	
#ifdef STRICT_CHECKING
	assert( livido_plugin != NULL );
#endif
	if(!livido_plugin)
	{
	//	( port );		//FIXME leak

		return NULL;
	}

	vevo_property_set( port, "instance", LIVIDO_ATOM_TYPE_PORTPTR, 1,&livido_plugin );
	vevo_property_set( port, "handle", LIVIDO_ATOM_TYPE_VOIDPTR,1,&handle );

	void *filter_templ = NULL;
	int error = vevo_property_get( livido_plugin, "filters",0,&filter_templ);
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	plugin_name =  get_str_vevo( filter_templ, "name" );

	int compiled_as = 0;
	if( vevo_property_get( filter_templ, "api_version", 0,&compiled_as ) != LIVIDO_NO_ERROR )
	{
		veejay_msg(VEEJAY_MSG_WARNING,"Plugin '%s' does not have the property 'api_version'. ", plugin_name );
		return NULL;	
	}

	if( compiled_as < LIVIDO_API_VERSION ) {
		veejay_msg(VEEJAY_MSG_WARNING,  "I am using a newer LiViDO API. Overwrite your livido.h from libplugger/specs/livido.h and recompile your plugins.");
		return NULL;
	}

	if( compiled_as > LIVIDO_API_VERSION ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Plugin '%s' uses newer LiViDO API (version %d).", plugin_name, compiled_as);
		return NULL;
	}

	plugin_name =  get_str_vevo( filter_templ, "name" );


	int n_params = livido_scan_parameters( filter_templ, port );


	int n_oparams = livido_scan_out_parameters( filter_templ, port );
	
//@ p%02d is a key with a portptr value. it contains min,max,defaults for each plugin setup()	
	int is_mix = 0;
	int n_inputs = livido_property_num_elements( filter_templ, "in_channel_templates" );
	int n_outputs = livido_property_num_elements( filter_templ, "out_channel_templates" );



	veejay_msg(0, "Loading LiVIDO-%d plugin '%s' , %d IP, %d OP" , compiled_as, plugin_name, n_params, n_oparams );
	
#ifdef STRICT_CHECKING
	assert( plugin_name != NULL );
#endif
	char *clone_name = (char*) vj_malloc( strlen(plugin_name) + 4);
	sprintf(clone_name, "LVD%s", plugin_name );

	vevo_property_set( port, "num_params", VEVO_ATOM_TYPE_INT, 1, &n_params );
	vevo_property_set( port, "num_out_params", VEVO_ATOM_TYPE_INT,1,&n_oparams );
	vevo_property_set( port, "name", VEVO_ATOM_TYPE_STRING,1, &clone_name );
	vevo_property_set( port, "num_inputs", VEVO_ATOM_TYPE_INT,1, &n_inputs);
	vevo_property_set( port, "num_outputs",VEVO_ATOM_TYPE_INT,1, &n_outputs);
	vevo_property_set( port, "info", LIVIDO_ATOM_TYPE_PORTPTR,1,&filter_templ );
	vevo_property_set( port, "HOST_plugin_type", VEVO_ATOM_TYPE_INT,1,&livido_signature_);

	free(clone_name);
	free(plugin_name);	

	return port;
}

void	livido_set_pref_palette( int pref_palette )
{
#ifdef STRICT_CHECKING
	assert( pref_palette == PIX_FMT_YUV420P || pref_palette == PIX_FMT_YUV422P ||
			pref_palette == PIX_FMT_YUV444P);
#endif
	pref_palette_ffmpeg_ = pref_palette;
	pref_palette_        = select_livido_palette( pref_palette );
}

void	livido_exit( void )
{
}
