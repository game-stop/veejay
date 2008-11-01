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


/** \defgroup VeVoPlugin Plugin Loader
 *
 * The Plugin Loader can handle:
 *   -# Livido plugins
 *   -# Frei0r plugins
 *   -# FreeFrame plugins
 */

#include <config.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <libhash/hash.h>
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <veejay/portdef.h>
#include <libvevo/libvevo.h>
#include <libplugger/defs.h>
#include <libplugger/ldefs.h>
#include <libplugger/specs/livido.h>
#include <libyuv/yuvconv.h>
#include <ffmpeg/avcodec.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <libplugger/plugload.h>
#include <libplugger/freeframe-loader.h>
#include <libplugger/frei0r-loader.h>
#include <libplugger/livido-loader.h>
#include <ffmpeg/avcodec.h>
#include <ffmpeg/avutil.h>

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
static	int	base_fmt_ = -1;

static	int	select_f( const struct dirent *d )
{
	return ( strstr( d->d_name, ".so" ) != NULL );
}

int		plug_set_param_from_str( void *plugin , int p, const char *str, void *values )
{
	int type = 0;
#ifdef STRICT_CHECKING
	assert( plugin != NULL );
#endif
	return livido_set_parameter_from_string( plugin, p, str, values );
}

char		*plug_describe_param( void *plugin, int p )
{
	return livido_describe_parameter_format( plugin,p );
}


static	void *instantiate_plugin( void *plugin, int w , int h )
{
	int type = 0;
#ifdef STRICT_CHECKING
	assert( plugin != NULL );
#endif
	int error = vevo_property_get( plugin, "HOST_plugin_type", 0, &type);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	switch( type )
	{
		case VEVO_PLUG_LIVIDO:
			return livido_plug_init( plugin,w,h );
			break;
		case VEVO_PLUG_FF:
			return freeframe_plug_init( plugin,w,h);
			break;
		case VEVO_PLUG_FR:
			return frei0r_plug_init( plugin,w,h );
			break;
		default:
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;
	}
	return NULL;
}


static	void	deinstantiate_plugin( void *instance )
{
#ifdef STRICT_CHECKING
	assert( instance != NULL );
#endif
	generic_deinit_f	gin;
	int error = vevo_property_get( instance, "HOST_plugin_deinit_f", 0, &gin );

#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	(*gin)(instance);
}

static	void	add_to_plugin_list( const char *path )
{
	if(!path)
		return;

	int i;
	char fullname[PATH_MAX+1];
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
		vevo_property_set( illegal_plugins_, path, LIVIDO_ATOM_TYPE_STRING, 0, NULL );
		return;
	}

	if( !S_ISDIR( sbuf.st_mode ) )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Not a directory : '%s'", path );
		return;
	}
	int n_files = scandir( path, &files, select_f, alphasort );
	if( n_files <= 0 )
		return;

	for( i = 0 ; i < n_files; i ++ )
	{
		char *name = files[i]->d_name;

		if( vevo_property_get( illegal_plugins_, name, 0 , NULL ) == 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "'%s' marked as bad", name);
			continue; 
		}
		bzero(fullname , PATH_MAX+1);

		sprintf(fullname, "%s/%s", path,name );

		void *handle = dlopen(fullname, RTLD_NOW );

		if(!handle) 
		{
			veejay_msg(0,"\tPlugin '%s' error '%s'", fullname,
				 dlerror() );
			continue;
		}

	//	veejay_msg(0, "\tOpened plugin '%s' in '%s'", name,path );
		
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
		} else if(dlsym( handle, "f0r_construct" )) {
			void *plugin = deal_with_fr( handle, name );
			if( plugin )
			{
				index_map_[ index_ ] = plugin;	
				index_ ++;
				n_fr_ ++;
			}
			else
				dlclose( handle );
		} else if(dlsym( handle, "livido_setup" )) {
			void *plugin = deal_with_livido( handle , name );
			if( plugin )
			{
				index_map_[index_] = plugin;
				index_ ++;
				n_lvd_ ++;
			}
			else
				dlclose(handle);
		} else
			dlclose(handle);

	}

	for( i = 0; i < n_files; i ++ )
		free( files[i] );
	free(files);
}

static	void	free_plugin(void *plugin)
{

#ifdef STRICT_CHECKING
	assert( plugin != NULL );
#endif
	void *handle = NULL;
	int error = vevo_property_get( plugin, "handle", 0 , &handle );
#ifdef STRICT_CHECKING
	assert( error == 0 );
#endif

	vevo_port_recursive_free( plugin );

	if( handle ) dlclose( handle );

	plugin = NULL;
}

char		*list_plugins()
{
	int i = 0;
	int len = 0;
	char *res = NULL;
	for ( i = 0; i < index_; i ++ )
	{
		char *name = plug_get_name( i );
		if(name)
		{
			len += strlen(name) + 1;
			free(name);
		}
	}

	if(len <= 0 )
		return NULL;

	res = (char*) vj_malloc( len );
	memset( res,0,len );
	char *p = res;
	for ( i = 0; i < index_; i ++ )
	{
		char *name = plug_get_name(i);
		if(name)
		{
			sprintf(p, "%s:",name );
			p += strlen(name) + 1;
		}
	}
	return res;
}

static	void	free_plugins()
{
	int i;
	for( i = 0; i < index_ ; i ++ )
		free_plugin( index_map_[i]);

	vevo_port_recursive_free( illegal_plugins_ );
	
	free( index_map_ );
	index_ = 0;
	index_map_ = NULL;
}

#define CONFIG_FILE_LEN 65535

static	int	scan_plugins()
{
	char *home = getenv( "HOME" );
	char path[PATH_MAX];
	char data[CONFIG_FILE_LEN];
	if(!home) return 0;
	
	sprintf( path , "%s/.veejay/plugins" , home );

	int fd = open( path, O_RDONLY );
	if( fd < 0 )
		return 0;

	bzero( data, CONFIG_FILE_LEN );

	if( read( fd, data, CONFIG_FILE_LEN ) > 0 )
	{
		int len = strlen(data);
		int j;
		int k = 0;
		char value[PATH_MAX];
		bzero( value, PATH_MAX );
		
		char *pch = strtok( data, "\n" );
		while( pch != NULL )
		{
			add_to_plugin_list( pch );
			pch = strtok( NULL, "\n");
		}
	}
	return 1;
}

void	plug_sys_free(void)
{
	free_plugins();
	if( buffer_ )
		free( buffer_ );
	if( buffer2_ )
		free( buffer2_ );
	if( buffer_b_)
		free( buffer_b_ );
}

void	plug_sys_init( int fmt, int w, int h )
{
	buffer_ = (void*) vj_malloc( w * h * 4);
	memset( buffer_, 0, w * h * 4);
	buffer2_ = (void*) vj_malloc( w * h * 4);
	memset( buffer2_, 0, w * h  * 4);
	buffer_b_ = (void*) vj_malloc( w * h * 4);
	memset( buffer_b_, 0, w * h  * 4);
	
	base_width_ = w;
	base_height_ = h;

	switch(fmt)
	{
		case FMT_420:
			base_fmt_ = PIX_FMT_YUV420P;
			break;
		case FMT_422:
			base_fmt_ = PIX_FMT_YUV422P;
			break;
		case FMT_444:
			base_fmt_ = PIX_FMT_YUV444P;
			break;
		default:
		veejay_msg(0, "%s: Unknown pixel format",__FUNCTION__);
#ifdef STRICT_CHECKING
			assert(0);
#endif	
			break;	
	}
	
	plug_sys_set_palette( base_fmt_ );
}

int	plug_sys_detect_plugins(void)
{
	index_map_ = (vevo_port_t**) vj_malloc(sizeof(vevo_port_t*) * 256 );
	illegal_plugins_ = vpn( VEVO_ILLEGAL );
#ifdef STRICT_CHECKING
	assert( illegal_plugins_ != NULL );
#endif	
	if(!scan_plugins())
	{
		veejay_msg(VEEJAY_MSG_ERROR,
				"Cannot locate plugins in $HOME/.veejay/plugins" );
		return 0;
	}

	veejay_msg(VEEJAY_MSG_INFO, "Veejay plugin system initialized");
	veejay_msg(VEEJAY_MSG_INFO, "-------------------------------------------------------------------------------------------");
	//@ display copyright notice in binary form
	veejay_msg(VEEJAY_MSG_INFO, "\tFreeFrame - cross-platform real-time video effects");
	veejay_msg(VEEJAY_MSG_INFO, "\t(C) Copyright 2002 Marcus Clements www.freeframe.org. All Rights reserved.");
	veejay_msg(VEEJAY_MSG_INFO, "\thttp://freeframe.sourceforge.net");
	veejay_msg(VEEJAY_MSG_INFO, "\tFound %d FreeFrame %s",
		n_ff_ , n_ff_ == 1 ? "plugin" : "plugins" );
	veejay_msg(VEEJAY_MSG_INFO, "\tfrei0r - a minimalistic plugin API for video effects");
	veejay_msg(VEEJAY_MSG_INFO, "\t(C) Copyright 2004 Georg Seidel, Phillip Promesberger and Martin Bayer.");
	veejay_msg(VEEJAY_MSG_INFO, "\t                   Licensed as GPL");
	veejay_msg(VEEJAY_MSG_INFO, "\thttp://www.piksel.org/frei0r");
	veejay_msg(VEEJAY_MSG_INFO, "\tFound %d frei0r %s",
		n_fr_ , n_fr_ == 1 ? "plugin" : "plugins" );
	veejay_msg(VEEJAY_MSG_INFO, "\tLivido - (Linux) Video Dynamic Objects" );
	veejay_msg(VEEJAY_MSG_INFO, "\t(C) Copyright 2005 Gabriel 'Salsaman' Finch, Niels Elburg, Dennis 'Jaromil' Rojo");
	veejay_msg(VEEJAY_MSG_INFO, "\t                   Daniel Fischer, Martin Bayer, Kentaro Fukuchi and Andraz Tori");
	veejay_msg(VEEJAY_MSG_INFO, "\t                   Licensed as LGPL");
	veejay_msg(VEEJAY_MSG_INFO, "\tFound %d Livido %s",
		n_lvd_, n_lvd_ == 1 ? "plugin" :"plugins" );
	veejay_msg(VEEJAY_MSG_INFO, "-------------------------------------------------------------------------------------------");
	
	plug_print_all();
	
	return index_;
}

void	plug_clone_from_parameters(void *instance, void *fx_values)
{
#ifdef STRICT_CHECKING
	assert( instance != NULL );
#endif
	generic_reverse_clone_parameter_f	grc;
	int error = vevo_property_get( instance, "HOST_plugin_param_reverse_f", 0, &grc );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	// copy parameters from plugin to fx_values	
	(*grc)( instance ,0, fx_values );
}

int	plug_clone_from_output_parameters( void *instance, void *fx_values )
{
#ifdef STRICT_CHECKING
	assert( instance != NULL );
#endif
	generic_reverse_clone_out_parameter_f	grc;
	int error = vevo_property_get( instance, "HOST_plugin_out_param_reverse_f", 0, &grc );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	int n = (*grc)(instance,fx_values);	
	return n;
}

void	plug_clone_parameters( void *instance, void *fx_values )
{
#ifdef STRICT_CHECKING
	assert( instance != NULL );
#endif
	generic_clone_parameter_f	gcc;
	int error = vevo_property_get( instance, "HOST_plugin_param_clone_f", 0, &gcc );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	(*gcc)( instance, 0, fx_values );
}

void	plug_set_parameter( void *instance, int seq_num,int n_elements,void *value )
{
#ifdef STRICT_CHECKING
	assert( instance != NULL );
#endif
	generic_push_parameter_f	gpp;
	int error = vevo_property_get( instance, "HOST_plugin_param_f", 0, &gpp );
	if( error == VEVO_NO_ERROR)
		(*gpp)( instance, seq_num, value );
#ifdef STRICT_CHECKING
	else
	{
		assert(0);
	}
#endif	
}

void	plug_get_defaults( void *instance, void *fx_values )
{
#ifdef STRICT_CHECKING
	assert( instance != NULL );
#endif
	generic_default_values_f	gdv;
	int error = vevo_property_get( instance, "HOST_plugin_defaults_f", 0, &gdv );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	(*gdv)( instance, fx_values );

}
void	plug_set_defaults( void *instance, void *fx_values )
{
#ifdef STRICT_CHECKING
	assert( instance != NULL );
#endif
	generic_clone_parameter_f	gcp;	
	int error = vevo_property_get( instance, "HOST_plugin_param_clone_f", 0, &gcp );

#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR);
#endif
	(*gcp)( instance, 0,fx_values );
}

void	plug_deactivate( void *instance )
{
	deinstantiate_plugin( instance );	
}

static	int	linear_len( char **items )
{
	int i = 0;
	int len = 0;
	for( i = 0; items[i] != NULL ; i ++ )
		len += strlen(items[i]);
	return len;
}

static	int	memory_needed_for_port( void *port, const char *key )
{
	void *subport = NULL;
	int error = vevo_property_get( port , key, 0, &subport );
	if( error != VEVO_NO_ERROR )
		return 0;
	char **items = vevo_sprintf_port( subport );
	
	int len = linear_len(items);
	int k   = 0;
	for( k = 0; items[k] != NULL; k ++ )
		free(items[k]);
	free(items);

	return len;
}

static	char *	flatten_port( void *port, const char *key )
{
	int len = memory_needed_for_port( port, key );
	if( len <= 0 )
		return NULL;
	
	char *res = (char*) vj_malloc( len );
	void *subport = NULL;

	int error = vevo_property_get( port , key, 0, &subport );
	if( error != VEVO_NO_ERROR )
		return 0;
	
	memset(res,0,len);
	char **items = vevo_sprintf_port( subport );
	int k   = 0;
	for( k = 0; items[k] != NULL; k ++ )
	{
		strncat(res, items[k],strlen(items[k]));
		free(items[k]);
	}
	free(items);
	return res;
}

char	*plug_describe( int fx_id )
{
	void *plug = index_map_[fx_id];
	if(!plug)
		return NULL;
	void *instance = NULL;
	void *filter = NULL;
	int pi = 0;
	int po = 0;
	int ci = 0;
	int co = 0;
	char *res = NULL;
	char key[64];
	int i;
	int len = 0;
	int error = 0;

	error = vevo_property_get( plug, "num_inputs", 0, &ci );
	error = vevo_property_get( plug, "num_params", 0, &pi );
	error = vevo_property_get( plug, "num_out_params",0,&po );
	error = vevo_property_get( plug, "num_outputs",0,&co );
	error = vevo_property_get( plug, "instance", 0,&instance );
	
	error = vevo_property_get( instance, "filters",0,&filter );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	//@ cannot handle multiple filters yet
	char *maintainer = get_str_vevo( instance, "maintainer");
	char *version    = get_str_vevo( instance, "version" );
	char *description = get_str_vevo( filter, "description" );
	char *name	 = get_str_vevo(  filter, "name");
	char *author     = get_str_vevo(  filter, "author" );
	char *license    = get_str_vevo(  filter, "license" );
	char **in_params = NULL;
	char **out_params = NULL;
	if( pi > 0 )
	{
		in_params = (char*) vj_malloc(sizeof(char*) * pi );
	
		for( i = 0; i < pi; i ++ )
		{
			sprintf(key, "p%02d",i);
			in_params[i] = flatten_port( plug , key );
			len += strlen(in_params[i])+1;
		}
	}
	if( po > 0 )
	{
		out_params = (char*) vj_malloc(sizeof(char*) * pi );
	
		for( i = 0; i < pi; i ++ )
		{
			sprintf(key, "q%02d",i);
			out_params[i] = flatten_port( plug , key );
			len += strlen(out_params[i])+1;
		}
	}


	len += strlen( maintainer ) + 12;
	len += strlen( version ) + 9;
	len += strlen( description ) + 13;
	len += strlen( name ) +6;
	len += strlen( author )+8;
	len += strlen( license )+9;

	res = (char*) vj_malloc(sizeof(char) * len + 150 );
	memset(res,0,len);

	sprintf( res,
			"name=%s:description=%s:author=%s:maintainer=%s:license=%s:version=%s:",
				name,description,author,maintainer,license,version );

	char *p = res + strlen(res);
	
	for( i = 0; i < pi ; i ++ )
	{
		sprintf(p, "p%02d=[%s]:", i, in_params[i] );
		p += strlen(in_params[i]) + 7;
		free(in_params[i]);
	}
	for( i = 0; i < po ; i ++ )
	{
		sprintf(p, "q%02d=[%s]:", i, out_params[i] );
		p += strlen( out_params[i] ) + 7;
		free(out_params[i]);
	}

	free(in_params);
	free(out_params);
	free(maintainer);
	free(version);
	free(description);
	free(name);
	free(author);
	free(license);

	return res;	
}

void	*plug_activate( int fx_id )
{
	if(!index_map_[fx_id] )
	{
		veejay_msg(0,"Plugin %d is not loaded",fx_id);
		return NULL;
	}
	return instantiate_plugin( index_map_[fx_id], base_width_,base_height_);
}

void	plug_clear_namespace( void *fx_instance, void *data )
{
	livido_plug_free_namespace( fx_instance , data );
}

void	plug_build_name_space( int fx_id, void *fx_instance, void *data, int entry_id , int sample_id,
		generic_osc_cb_f cbf, void *cb_data)
{
	void *plugin = index_map_[fx_id];
	int type = 0;
#ifdef STRICT_CHECKING
	assert( plugin != NULL );
#endif
	int error = vevo_property_get( plugin, "HOST_plugin_type", 0, &type);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	switch( type )
	{
		case VEVO_PLUG_LIVIDO:
			livido_plug_build_namespace( plugin, entry_id, fx_instance, data, sample_id, cbf, cb_data );
			break;
		case VEVO_PLUG_FF:
			break;
		case VEVO_PLUG_FR:
			break;
		default:
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;
	}



}


void	plug_print_all()
{
	int n;
	for(n = 0; n < index_ ; n ++ )
	{
		char *fx_name = plug_get_name(n);
		if(fx_name)
		{
			veejay_msg(VEEJAY_MSG_INFO, "\t'FX %s loaded", fx_name ); 
			free(fx_name);
		}
	}
		
}

void plug_concatenate_all(void *osc, void *msg)
{
	int len = 1;
	int n;
	int fx = 0;
	veejay_message_add_argument( osc, msg, "s", "none");
	for(n = 0; n < index_ ; n ++ )
	{
		char *fx_name = plug_get_name(n);
		if(fx_name)
		{
			veejay_message_add_argument( osc, msg, "s", fx_name );
			free(fx_name);
		}
	}

}

char	*plug_get_name( int fx_id )
{
	if(!index_map_[fx_id] )
		return NULL;
	char *name = get_str_vevo( index_map_[fx_id], "name" );
	return name;
}

char	*plug_get_osc_format(void *fx_instance, int seq_num)
{
/*	void *param_templ = NULL;
	int error = vevo_property_get( fx_instance, "parent_template",0,&param_templ);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif*/
	
	char *required_format = livido_describe_parameter_format_osc( fx_instance,seq_num );
	return required_format;
}

int	plug_get_fx_id_by_name( const char *name )
{
	int n = 0;
	for(n = 0; n < index_; n ++ )
	{
		char *plugname = plug_get_name( n );
		if(plugname)
		{
			if( strncasecmp(name,plugname,strlen(plugname)) == 0 )
			{
				free(plugname);
				return n;
			}
			free(plugname);
		}
	}
	return -1;
}

int	plug_get_num_input_channels( int fx_id )
{
	if(!index_map_[fx_id] )
		return NULL;

	int res = 0;
	int error = vevo_property_get( index_map_[fx_id], "num_inputs",0,&res);

#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	return res;
}

int	plug_get_num_parameters( int fx_id )
{
	if(!index_map_[fx_id] )
		return NULL;

	int res = 0;
	int error = vevo_property_get( index_map_[fx_id], "num_params",0,&res);

#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	return res;
}	

void	plug_sys_set_palette( int pref_palette )
{
	base_fmt_ = pref_palette;
	livido_set_pref_palette( base_fmt_ );
}

void	plug_push_frame( void *instance, int out, int seq_num, void *frame_info )
{
	VJFrame *frame = (VJFrame*) frame_info;
#ifdef STRICT_CHECKING
	assert( instance != NULL );
#endif
	generic_push_channel_f	gpu;
	int error = vevo_property_get( instance, "HOST_plugin_push_f", 0, &gpu );
#ifdef STRICT_CHECKING
	assert( error == 0 );
#endif
	(*gpu)( instance, (out ? "out_channels" : "in_channels" ), seq_num, frame );
}



void	plug_process( void *instance )
{
#ifdef STRICT_CHECKING
	assert( instance != NULL );
#endif
	generic_process_f	gpf;
	int error = vevo_property_get( instance, "HOST_plugin_process_f", 0, &gpf );
#ifdef STRICT_CHECKING
	assert( error == 0 );
#endif
	(*gpf)( instance,0.0 );
}


void	*plug_get_name_space( void *instance )
{
	return livido_get_name_space(instance);
}

char 	*plug_get_osc_path_parameter(void *instance, int k)
{
	void *p = NULL;
	if( vevo_property_get( instance, "in_parameters",k,&p ) == VEVO_NO_ERROR )
		return vevo_property_get_string( p, "HOST_osc_path" );
	return NULL;
}

int	plug_parameter_get_range_dbl( void *fx_instance,const char *key, int k, double *min, double *max , int *kind )
{
	return livido_plug_parameter_get_range_dbl( fx_instance,key, k, min,max ,kind);
}

int plug_get_index_parameter_as_dbl( void *fx_instance,const char *key, int k , double *res)
{
	return livido_plug_get_index_parameter_as_dbl( fx_instance, key, k,res );
}

int	plug_get_number_parameter_as_dbl( void *fx_instance, const char *key, int k , double *res)
{
	return livido_plug_get_number_parameter_as_dbl( fx_instance,key, k,res );
}

int	plug_get_coord_parameter_as_dbl( void *fx_instance,const char *key, int k, double *res_x, double *res_y )
{
	return	livido_plug_get_coord_parameter_as_dbl( fx_instance, key,k,res_x,res_y );
}
