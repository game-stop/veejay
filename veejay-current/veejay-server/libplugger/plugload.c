/*
 * Copyright (C) 2002-2006 Niels Elburg <nwelburg@gmail.com>
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
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <libhash/hash.h>
#include <libvje/vje.h>
#include <veejay/vims.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libplugger/portdef.h>
#include <libvevo/libvevo.h>
#include <libplugger/defs.h>
#include <libplugger/ldefs.h>
#include <libplugger/specs/livido.h>
#include <libplugger/utility.h>
#include <libyuv/yuvconv.h>
#include <libavutil/avutil.h>
#ifndef SAMPLE_MAX_PARAMETERS
#define SAMPLE_MAX_PARAMETERS 32 //sampleadm.h
#endif
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libplugger/plugload.h>
#include <libplugger/freeframe-loader.h>
#include <libplugger/frei0r-loader.h>
#include <libplugger/livido-loader.h>

static	vevo_port_t **index_map_ = NULL;
static  vevo_port_t *illegal_plugins_ =NULL;
static  int	index_     = 0;
static	int	base_width_	=0;
static	int	base_height_	=0;
static  int	n_ff_ = 0;
static	int	n_fr_ = 0;
static  int	n_lvd_ = 0;
static	int	base_fmt_ = -1;

static struct {
	const char *path;
} plugger_paths[] = {
	{ "/usr/local/lib/frei0r-1" },
	{ "/usr/lib/frei0r-1"},
	{ "/usr/local/lib64/frei0r-1"},
	{ "/usr/lib64/frei0r-1"},
	{ NULL },
};

//forward decl
void plug_print_all();


static	int	select_f( const struct dirent *d )
{
	return ( strstr( d->d_name, ".so" ) != NULL );
}

int		plug_set_param_from_str( void *plugin , int p, const char *str, void *values )
{
	//FIXME other types
	return livido_set_parameter_from_string( plugin, p, str, values );
}

char		*plug_describe_param( void *plugin, int p )
{
	//FIXME other types
	return livido_describe_parameter_format( plugin,p );
}


static	void* instantiate_plugin( void *plugin, int w , int h )
{
	int type = 0;
	int error = vevo_property_get( plugin, "HOST_plugin_type", 0, &type);
	if( error != VEVO_NO_ERROR )
		return NULL;

	void *instance = NULL;
		
	switch( type )
	{
		case VEVO_PLUG_LIVIDO:
			instance = livido_plug_init( plugin,w,h, base_fmt_ );
			break;
		case VEVO_PLUG_FF:
			instance = freeframe_plug_init( plugin,w,h);
			break;
		case VEVO_PLUG_FR:
			instance = frei0r_plug_init( plugin,w,h,base_fmt_ );
			break;
		default:
			veejay_msg(0, "Plugin type not supported.");
			break;
	}

	if( instance != NULL )
		vevo_property_set( instance, "HOST_type", VEVO_ATOM_TYPE_INT, 1, &type );

	
	return instance;
}


static	void	deinstantiate_plugin( void *instance )
{
	generic_deinit_f	gin = NULL;
	int error = vevo_property_get( instance, "HOST_plugin_deinit_f", 0, &gin );

	if( error == VEVO_NO_ERROR && gin != NULL )
		(*gin)(instance);

	instance = NULL;
}

static	int	add_to_plugin_list( const char *path )
{
	if(path == NULL)
		return 0;

	int i;
	char fullname[PATH_MAX+1];
	struct	dirent	**files = NULL;
	struct stat sbuf;
	int	res = 0;

	memset( &sbuf,0 ,sizeof(struct stat));
	res = stat( path, &sbuf );

	if( res != 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "File or directory '%s' does not exist (skip)", path);
		return 0;
	}
	
	if( S_ISREG( sbuf.st_mode ) )
	{
		vevo_property_set( illegal_plugins_, path, LIVIDO_ATOM_TYPE_STRING, 0, NULL );
		return 0;
	}

	if( !S_ISDIR( sbuf.st_mode ) )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Not a directory : '%s'", path );
		return 0;
	}
	int n_files = scandir( path, &files, select_f, alphasort );
	if( n_files <= 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "No plugins found in %s", path );
		return 0;
	}

	for( i = 0 ; i < n_files; i ++ )
	{
		char *name = files[i]->d_name;

		if( vevo_property_get( illegal_plugins_, name, 0 , NULL ) == 0 )
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "'%s' marked as bad", name);
			continue; 
		}

		snprintf(fullname,sizeof(fullname),"%s/%s", path,name );

		void *handle = dlopen(fullname, RTLD_NOW );

		if(!handle) 
		{
			veejay_msg(0,"Plugin '%s' error '%s'", fullname,
				 dlerror() );
			continue;
		}
		char *bname = basename( fullname );
		char *basename = vj_strdup( bname );
		void *plugin = NULL;
		veejay_msg(VEEJAY_MSG_DEBUG, "Loading %s",fullname );

		if(dlsym( handle, "plugMain" ) && sizeof(long) == 4)
		{
			plugin = deal_with_ff( handle, name, base_width_, base_height_ );
			if( plugin )
			{
				index_map_[ index_ ] = plugin;
				index_ ++;
				n_ff_ ++;
			}
			else
				dlclose( handle );	
		} else if(dlsym( handle, "f0r_construct" )) {
			plugin = deal_with_fr( handle, name );
			if( plugin )
			{
				index_map_[ index_ ] = plugin;	
				index_ ++;
				n_fr_ ++;
			}
			else
				dlclose( handle );
		} else if(dlsym( handle, "livido_setup" )) {
			plugin = deal_with_livido( handle , name );
			if( plugin )
			{
				index_map_[index_] = plugin;
				index_ ++;
				n_lvd_ ++;
			}
			else
				dlclose(handle);
		} else{
			dlclose(handle);
			veejay_msg(VEEJAY_MSG_DEBUG, "%s is not compatible.", fullname );
		}

		if( plugin ) {
			vevo_property_set( plugin, "so_name",VEVO_ATOM_TYPE_STRING,1,&basename );
		}

		free(basename);
	
	}

	for( i = 0; i < n_files; i ++ )
		free( files[i] );
	free(files);

	return n_files;
}

static	void	free_plugin(void *plugin)
{

	void *handle = NULL;
	int error = vevo_property_get( plugin, "handle", 0 , &handle );
	if( error == VEVO_NO_ERROR )
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

	vpf( illegal_plugins_ );
	freeframe_destroy();
	frei0r_destroy();

	free( index_map_ );
	index_ = 0;
	index_map_ = NULL;
}

int plug_get_idx_by_so_name( char *soname )
{
	int i;
	for( i = 0; i < index_ ; i ++ ) {

		char *str = vevo_property_get_string( index_map_[i], "so_name" );
		if( str == NULL )
			continue;

		if( strcmp( soname,str ) == 0 )
			return i;
	}

	return -1;
}

void	*plug_get_by_so_name( char *soname )
{
	int i;
	for( i = 0; i < index_ ; i ++ ) {

		char *str = vevo_property_get_string( index_map_[i], "so_name" );
		if( str == NULL )
			continue;

		if( strcmp( soname,str ) == 0 )
			return index_map_[i];
	}

	return NULL;
}

int	plug_get_idx_by_name( char *name )
{
	int i;
	int len = strlen(name);
	for( i = 0; i < index_; i ++ ) {
		char *plugname = plug_get_name( i );
		if( strncasecmp( name, plugname, len ) == 0 ) {
			return i;
		}
	}
	return -1;
}


void	*plug_get( int fx_id ) {
	return index_map_[fx_id];
}

#define CONFIG_FILE_LEN 65535

static	char	*get_livido_plug_path()
{ //@ quick & dirty
	char location[1024];
	snprintf( location, sizeof(location)-1, "/proc/%d/exe", getpid() );
	
	char target[1024];
	char lvdpath[1024];
	
	veejay_memset(lvdpath,0,sizeof(lvdpath));

	int  err = readlink( location, target, sizeof(target) );
	if( err >= 0 )
	{
	 target[err] = '\0';

	 int n = err;
	 while( target[n] != '/' && n > 0 ) {
	   n--; //@ strip name of executable
	 }
	 if( n > 0 ) n --;
	 
	 while( target[n] != '/' && n > 0 ) {
	   n--; //@ strip bin dir of executable
	 }
	 
	 strncpy(lvdpath, target, n );
 	 strcat( lvdpath, "/lib/livido-plugins" );	 

	 return vj_strdup( lvdpath );
	}
	return NULL;
}


static	int	scan_plugins()
{
	char *home = getenv( "HOME" );
	char path[PATH_MAX];
	char data[CONFIG_FILE_LEN];
	if(!home) return 0;
	
	sprintf( path , "%s/.veejay/plugins.cfg" , home );

	int fd = open( path, O_RDONLY );
	if( fd < 0 ) {
		return 0;
	}

	veejay_memset( data,0, sizeof(data));

	if( read( fd, data, CONFIG_FILE_LEN ) > 0 )
	{
		char *pch = strtok( data, "\n" );
		while( pch != NULL )
		{
			add_to_plugin_list( pch );
			pch = strtok( NULL, "\n");
		}
	}

	close(fd);

	return 1;
}

void	plug_sys_free(void)
{
	free_plugins();
}

void	plug_sys_init( int fmt, int w, int h )
{
	base_width_ = w;
	base_height_ = h;
	base_fmt_ = fmt;
	
	plug_sys_set_palette( base_fmt_ );
}

int *plug_find_all_generator_plugins( int *total )
{
	int n;
	int len = 0;
	for( n = 0; n < index_ ; n ++ ) {
		if( index_map_[n] == NULL )
			continue;
		int in_c = plug_get_num_input_channels( n );
		int out_c= plug_get_num_output_channels( n );
		if( in_c == 0 && out_c == 1 ) {
			len ++;
		}
	}

	if( len == 0 )
		return NULL;

	int *arr = (int*) vj_calloc( sizeof(int) * (len+1) );
	if( !arr )
		return NULL;

	len = 0;

	for( n = 0; n < index_ ; n ++ ) {
		if( index_map_[n] == NULL )
			continue;
		int in_c = plug_get_num_input_channels( n );
		int out_c= plug_get_num_output_channels( n );
		if( in_c == 0 && out_c == 1 ) {
			arr[ len ] = n;
			len ++;
		}
	}

	*total = len;
	return arr;
}

int	plug_find_generator_plugins(int *total, int seq )
{
	int n;
	int last_found = -1;
	int total_found = 0;
	int seqno = 0;
	for( n = 0; n < index_ ; n ++ ) {
		if( index_map_[n] == NULL )
			continue;
		int in_c = plug_get_num_input_channels( n );
		int out_c= plug_get_num_output_channels( n );
		if( in_c == 0 && out_c == 1 ) {
			if( seq >= 0 && seq == seqno ) {
				*total = -1; // N/A
				return n;
			}
			seqno++;
			total_found ++;
			last_found = n;
		}
	
	}
	*total = total_found;
	return last_found;
}


int	plug_sys_detect_plugins(void)
{
	index_map_ = (vevo_port_t**) vj_calloc(sizeof(vevo_port_t*) * 1024 );
	illegal_plugins_ = vpn( VEVO_ILLEGAL );

	char *lvd_path = get_livido_plug_path();
	if( lvd_path != NULL ) {
		add_to_plugin_list( lvd_path );
		free(lvd_path);
	}


	if(!scan_plugins())
	{
		veejay_msg(VEEJAY_MSG_WARNING,
				"No plugins found in $HOME/.veejay/plugins.cfg" );
	}

	veejay_msg(VEEJAY_MSG_INFO, "Looking for plugins in common locations ...");
	
	int i;
	for( i = 0; plugger_paths[i].path != NULL; i ++ ) {
		add_to_plugin_list( plugger_paths[i].path );
	}

	//@ the freeframe version we use is not compatible with 64 bit systems. So, lets see if long is size 4
	//@ For every time there is a void* passed as int a gremlin will be happy
	if( sizeof(long) == 4 ) {
		if( n_ff_ > 0 ) { 
			veejay_msg(VEEJAY_MSG_INFO, "FreeFrame - cross-platform real-time video effects (http://freeframe.sourceforge.net)");
			veejay_msg(VEEJAY_MSG_INFO, "            found %d FreeFrame %s",	n_ff_ , n_ff_ == 1 ? "plugin" : "plugins" );
		}
	}

	if( n_fr_ > 0 ) {
		veejay_msg(VEEJAY_MSG_INFO, "frei0r - a minimalistic plugin API for video effects (http://www.piksel.org/frei0r)");
		veejay_msg(VEEJAY_MSG_INFO, "         found %d frei0r %s",
			n_fr_ , n_fr_ == 1 ? "plugin" : "plugins" );
	}	

	if( n_lvd_ > 0 ) {
		veejay_msg(VEEJAY_MSG_INFO, "Livido - (Linux) Video Dynamic Objects" );
		veejay_msg(VEEJAY_MSG_INFO, "         found %d Livido %s",
			n_lvd_, n_lvd_ == 1 ? "plugin" :"plugins" );
	}
	
	plug_print_all();
	
	return index_;
}

void	plug_clone_from_parameters(void *instance, void *fx_values)
{
	generic_reverse_clone_parameter_f	grc;
	int error = vevo_property_get( instance, "HOST_plugin_param_reverse_f", 0, &grc );
	if( error != VEVO_NO_ERROR )
		return;
	// copy parameters from plugin to fx_values	
	(*grc)( instance ,0, fx_values );
}

int	plug_clone_from_output_parameters( void *instance, void *fx_values )
{
	generic_reverse_clone_out_parameter_f	grc;
	int error = vevo_property_get( instance, "HOST_plugin_out_param_reverse_f", 0, &grc );
	if( error != VEVO_NO_ERROR )
		return 0;
	int n = (*grc)(instance,fx_values);	
	return n;
}

void	plug_clone_parameters( void *instance, void *fx_values )
{
	generic_clone_parameter_f	gcc;
	int error = vevo_property_get( instance, "HOST_plugin_param_clone_f", 0, &gcc );
	if( error == VEVO_NO_ERROR )
		(*gcc)( instance, 0, fx_values );
}

int	plug_is_frei0r( void *instance )
{
	int type = 0;
	vevo_property_get(instance, "HOST_type", 0, &type );
	if( type == VEVO_PLUG_FR )
		return 1;
	return 0;
}

void	plug_set_parameter( void *instance, int seq_num,int n_elements,void *value )
{
	generic_push_parameter_f	gpp;
	int error = vevo_property_get( instance, "HOST_plugin_param_f", 0, &gpp );
	if( error == VEVO_NO_ERROR)
		(*gpp)( instance, seq_num, value );
}

void 	plug_set_parameters( void *instance, int n_args, void *values )
{
	frei0r_plug_param_f( instance, n_args, (int*) values );
}
void	plug_get_defaults( void *instance, void *fx_values )
{
	generic_default_values_f	gdv;
	int error = vevo_property_get( instance, "HOST_plugin_defaults_f", 0, &gdv );
	if( error == VEVO_NO_ERROR )
		(*gdv)( instance, fx_values );
}

void	plug_set_defaults( void *instance, void *fx_values )
{
	generic_clone_parameter_f	gcp;	
	if( vevo_property_get( instance, "HOST_plugin_param_clone_f", 0, &gcp ) == VEVO_NO_ERROR )
		(*gcp)( instance, 0,fx_values );
}

void	plug_deactivate( void *instance )
{
	if( instance )
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

	vevo_property_get( plug, "num_inputs", 0, &ci );
	vevo_property_get( plug, "num_params", 0, &pi );
	vevo_property_get( plug, "num_out_params",0,&po );
	vevo_property_get( plug, "num_outputs",0,&co );

	if( vevo_property_get( plug, "instance", 0,&instance ) != VEVO_NO_ERROR )
		return NULL;
	
	if( vevo_property_get( instance, "filters",0,&filter ) != VEVO_NO_ERROR )
		return NULL;

	//@ cannot handle multiple filters yet
	char *maintainer = get_str_vevo( instance, "maintainer");
	char *version    = get_str_vevo( instance, "version" );
	char *description = get_str_vevo( filter, "description" );
	char *name		 = get_str_vevo(  filter, "name");
	char *author     = get_str_vevo(  filter, "author" );
	char *license    = get_str_vevo(  filter, "license" );
	char **in_params = NULL;
	char **out_params = NULL;
	if( pi > 0 )
	{
		in_params = (char**) vj_malloc(sizeof(char*) * pi );
	
		for( i = 0; i < pi; i ++ )
		{
			sprintf(key, "p%02d",i);
			in_params[i] = flatten_port( plug , key );
			len += strlen(in_params[i])+1;
		}
	}
	if( po > 0 )
	{
		out_params = (char**) vj_malloc(sizeof(char*) * pi );
	
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
			"name=%s:description=%s:author=%s:maintainer=%s:license=%s:version=%s:outs=%d:ins=%d",
				name,description,author,maintainer,license,version,co,ci );

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
	if(index_map_[fx_id] == NULL)
	{
		veejay_msg(0,"Plugin %d is not loaded",fx_id);
		return NULL;
	}

	void *instance = instantiate_plugin( index_map_[fx_id], base_width_,base_height_);
	if( instance == NULL ) {
		veejay_msg(0, "Error instantiating plugin.");
		return NULL;
	}
	return instance;
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
	if( vevo_property_get( plugin, "HOST_plugin_type", 0, &type) == VEVO_NO_ERROR ) {
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
				break;
		}
	}


}


void	plug_print_all()
{
	int n;
	int x=0;
	for(n = 0; n < index_ ; n ++ )
	{
		char *fx_name = plug_get_name(n);
		if(fx_name)
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "\t'FX %s loaded", fx_name ); 
			free(fx_name);
			x++;
		}
	}

	if( x < n ) {
		veejay_msg(VEEJAY_MSG_INFO, "Loaded %d/%d plugins", x,n );
	}
	veejay_msg(VEEJAY_MSG_INFO, "Loaded %d plugins in total",n);
		
}

void plug_concatenate_all(void *osc, void *msg)
{
/*	int len = 1;
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
*/
	//@ TODO OMC
}

char	*plug_get_name( int fx_id )
{
	if(index_map_[fx_id] == NULL )
		return NULL;
	char *name = vevo_property_get_string( index_map_[fx_id], "name" );
	return name;
}

char	*plug_get_osc_format(void *fx_instance, int seq_num)
{
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
int	plug_get_num_output_channels( int fx_id )
{
	if(index_map_[fx_id] == NULL)
		return 0;

	int res = 0;
	vevo_property_get( index_map_[fx_id], "num_outputs",0,&res);
	return res;
}
int	plug_get_num_input_channels( int fx_id )
{
	if(index_map_[fx_id] == NULL )
		return 0;

	int res = 0;
	vevo_property_get( index_map_[fx_id], "num_inputs",0,&res);
	return res;
}

int	plug_get_num_parameters( int fx_id )
{
	if(index_map_[fx_id] == NULL )
		return 0;

	int res = 0;
	if( vevo_property_get( index_map_[fx_id], "num_params",0,&res) != VEVO_NO_ERROR )
		return 0;

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
	
	generic_push_channel_f	gpu;
	int error = vevo_property_get( instance, "HOST_plugin_push_f", 0, &gpu );
	
	if( error == VEVO_NO_ERROR )
		(*gpu)( instance, seq_num,out, frame );
}

void	plug_process( void *instance, double timecode )
{
	generic_process_f	gpf;
	int error = vevo_property_get( instance, "HOST_plugin_process_f", 0, &gpf );
	if( error == VEVO_NO_ERROR )
		(*gpf)( instance, timecode );
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
/* create simple vj_effect holder for plug */

vj_effect *plug_get_plugin( int fx_id ) {
	void *port = index_map_[fx_id];
	if(port == NULL)
		return NULL;

	vj_effect *vje = (vj_effect*) vj_calloc(sizeof(vj_effect));
	
	size_t name_len = vevo_property_element_size( port, "name", 0);
	vje->description = (char*) vj_calloc(name_len);
	vevo_property_get( port, "name", 0 , &(vje->description));
	vevo_property_get( port, "num_params", 0, &(vje->num_params));
	vevo_property_get( port, "mixer", 0, &(vje->extra_frame));

	if( vje->num_params > 0 ) {
		if( vje->num_params > SAMPLE_MAX_PARAMETERS ) {
			veejay_msg(VEEJAY_MSG_WARNING, "%s has %d parameters, supporting only %d.",
				vje->description,vje->num_params, SAMPLE_MAX_PARAMETERS );
			vje->num_params = SAMPLE_MAX_PARAMETERS;
		}
		vje->defaults = (int*) vj_calloc(sizeof(int) * vje->num_params);
		vje->limits[0]= (int*) vj_calloc(sizeof(int) * vje->num_params);
		vje->limits[1]= (int*) vj_calloc(sizeof(int) * vje->num_params);
		
		int k = 0;
		int valid_p = 0;
		char **param_descr = NULL;
		if( vje->num_params > 0 ) 
			param_descr = (char**) vj_calloc(sizeof(char*) * vje->num_params );

		for( k = 0; k < vje->num_params;k++ )
		{
			char key[20];
			sprintf(key, "p%02d", k );
			void *parameter = NULL;
			vevo_property_get( port, key, 0, &parameter );
			if(parameter)
			{	
				vevo_property_get( parameter, "min", 0, &(vje->limits[0][k]));
				vevo_property_get( parameter, "max", 0, &(vje->limits[1][k]));	
				vevo_property_get( parameter, "default", 0,&(vje->defaults[k]));
				param_descr[valid_p] = vevo_property_get_string(parameter,"name");
				if(param_descr[valid_p]==NULL)
					param_descr[valid_p] = vj_strdup( "Number" );
				valid_p ++;
			}
		}		
		vje->num_params = valid_p;
		vje->param_description = param_descr;
	}
	return vje;
}
