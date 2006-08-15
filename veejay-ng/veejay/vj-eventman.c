/* veejay - Linux VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
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
#include <string.h>
#include <libvevo/libvevo.h>
#include <veejay/vj-event.h>
#include <veejay/vims.h>
#include <libvjmsg/vj-common.h>
#include <veejay/portdef.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#define MAX_INDEX 1024

#define VIMS_REQUIRE_ALL_PARAMS (1<<0)			/* all params needed */
#define VIMS_DONT_PARSE_PARAMS (1<<1)		/* dont parse arguments */
#define VIMS_LONG_PARAMS (1<<3)				/* long string arguments (bundle, plugin) */
#define VIMS_ALLOW_ANY (1<<4)				/* use defaults when optional arguments are not given */	

#define	SAMPLE_ID_HELP	"Sample ID (0=current playing, -1=last created, > 0 = Sample ID)"
#define SAMPLE_FX_ENTRY_HELP "Entry ID"
#define	STREAM_ID_HELP	"Stream ID (-1=last created, > 0 = Stream ID)"
#define	SAMPLE_STREAM_ID_HELP	"Sample or Stream ID (0=current playing, -1=last created, > 0 = ID)"
static	vevo_port_t **index_map_ = NULL;
/* define the function pointer to any event */
typedef void (*vevo_event)(void *ptr, const char format[], va_list ap);

void 	*vj_event_vevo_get_event_function( int id );
char	*vj_event_vevo_get_event_name( int id );
char	*vj_event_vevo_get_event_format( int id );
int	vj_event_vevo_get_num_args(int id);
int	vj_event_vevo_get_flags( int id );
int	vj_event_vevo_get_vims_id( int id );
void	vj_init_vevo_events(void);
void	vj_event_vevo_inline_fire(void *super, int vims_id, const char *format, ... );
char	*vj_event_vevo_list_serialize(void);
void	vj_event_vevo_dump(void);
char	*vj_event_vevo_help_vims( int id, int n );
int	vj_event_vevo_get_default_value(int id, int p);
void 	vj_event_vevo_free(void);

static	void		dump_event_stderr(vevo_port_t *event)
{
	char *fmt = NULL;
	char *name = NULL;	
	int  n_arg = 0;
 	int  vims_id = 0;
	char *param = NULL;
	int i;
	char key[10];

	size_t len = vevo_property_element_size(event, "format", 0 );
	if(len > 0 )
	{
		fmt = malloc(sizeof(char) * len);
		vevo_property_get( event, "format", 0, &fmt );
	}
	name = malloc(sizeof(char) * vevo_property_element_size( event, "description", 0 ));
	vevo_property_get( event, "description", 0, &name );
	
	vevo_property_get( event, "arguments", 0, &n_arg );
	vevo_property_get( event, "vims_id", 0, &vims_id );

	veejay_msg(VEEJAY_MSG_INFO, "VIMS selector %03d\t'%s'", vims_id, name );  
	if(fmt)
		veejay_msg(VEEJAY_MSG_INFO, "\tFORMAT: '%s', where:", fmt );
	
	for( i = 0; i < n_arg; i ++ )
	{
		sprintf(key, "help_%d", i );
		size_t len2 = vevo_property_element_size( event, key, 0 );
		if(len2 > 0 )
		{
			param = malloc(sizeof(char) * len2 );
			vevo_property_get( event, key, 0, &param );
			veejay_msg(VEEJAY_MSG_INFO,"\t\tArgument %d is %s", i, param );
			free(param);
		}
	}
	
	if(fmt) free(fmt);
	free(name);
	veejay_msg(VEEJAY_MSG_INFO," ");	
}

int	vj_event_vevo_list_size(void)
{
	int i;	
	int len =0;
	for ( i = 0; i < MAX_INDEX ;i ++ )
	{
		if( index_map_[i] != NULL )
		{
			char *name  = vj_event_vevo_get_event_name( i );
			char *format= vj_event_vevo_get_event_format( i ); 
			len += (name == NULL ?  0: strlen( name ));
			len += (format == NULL ? 0: strlen( format ));
			len += 12;	
			if(name) free(name);
			if(format)free(format);
		}
	}
	return len;
}

char	*vj_event_vevo_help_vims( int id, int n )
{
	char *help = NULL;
	char key[10];
	sprintf(key, "help_%d", n);
	size_t len = vevo_property_element_size( index_map_[id], key, 0  );
	if(len > 0 )
	{
		help = (char*) malloc(sizeof(char) * len );
		vevo_property_get( index_map_[id], key, 0, &help );
	}
	return help;
}

char	*vj_event_vevo_list_serialize(void)
{
	int len = vj_event_vevo_list_size() + 5;
	char *res = (char*) malloc(sizeof(char) * len + 100 );
	int i;
	memset( res, 0, len );
	sprintf(res, "%05d", len  - 5);
	for ( i = 0; i < MAX_INDEX ;i ++ )
	{
		if ( index_map_[i] != NULL )
		{
			char *name  = vj_event_vevo_get_event_name( i );
			char *format= vj_event_vevo_get_event_format( i ); 
			int name_len = (name == NULL ?  0: strlen( name ));
			int fmt_len  = (format == NULL? 0: strlen( format ));
			char tmp[13];
			sprintf( tmp, "%04d%02d%03d%03d",
				i, vj_event_vevo_get_num_args(i), fmt_len, name_len );
			strncat( res, tmp, 12 );
			if( format != NULL )
				strncat( res, format, fmt_len );
			if( name != NULL )
				strncat( res, name, name_len );
			if(name) free(name);
			if(format) free(format);
			
		}
	}
	return res;
}

void	vj_event_vevo_inline_fire(void *super, int vims_id, const char *format, ... )
{
	va_list ap;
	va_start( ap, format );
	void *func = NULL;
	vevo_property_get( index_map_[vims_id], "function", 0, &func );
	vevo_event f = (vevo_event) func;
	f( super, format, ap );
	va_end( ap );
}

static vevo_port_t	*_new_event(
		const char *format,
		int vims_id,	
		const char *name,
		void *function,
		int n_arg,
		int flags,
		... )
{
	int n = 0;
	int it = 1;
	char param_name[16];
	char descr_name[255];

#ifdef STRICT_CHECKING
	assert( name != NULL );
	assert( function != NULL );
	assert( vims_id > 0 );
#endif

	vevo_port_t *p = (void*) vevo_port_new( VEVO_EVENT_PORT );
#ifdef STRICT_CHECKING
	assert( p != NULL );
#endif
	if( format )
		vevo_property_set( p, "format", VEVO_ATOM_TYPE_STRING, 1, &format );
	else
		vevo_property_set( p, "format", VEVO_ATOM_TYPE_STRING, 0, NULL );

	vevo_property_set( p, "description", VEVO_ATOM_TYPE_STRING, 1, &name );
	vevo_property_set( p, "function", VEVO_ATOM_TYPE_VOIDPTR, 1,&function );
	vevo_property_set( p, "arguments", VEVO_ATOM_TYPE_INT, 1, &n_arg );
	vevo_property_set( p, "flags",	VEVO_ATOM_TYPE_INT, 1, &flags );
	vevo_property_set( p, "vims_id", VEVO_ATOM_TYPE_INT, 1, &vims_id );

	va_list ap;
	va_start(ap, flags);
#ifdef STRICT_CHECKING
	veejay_msg(VEEJAY_MSG_DEBUG,
	  "VIMS %03d: '%s' '%s' %d arguments", vims_id, name, format, n_arg );
#endif

	for( n = 0; n < n_arg ; n ++)
	{
		int dd   = 0;
		char *ds = NULL;
		bzero( param_name, 16 );
		bzero( descr_name, 255 );


		sprintf(param_name, "argument_%d", n );
		const char *arg = va_arg( ap, const char*);
#ifdef STRICT_CHECKING
		if(!arg) veejay_msg(VEEJAY_MSG_DEBUG, "\t%s - %d = '%s' of format %c (%s)",param_name, n, arg, format[it],format );
		assert( arg != NULL );
#endif
		char *descr = (char*) strdup( arg );
		sprintf(descr_name, "help_%d", n );
		
		if (format[it] == 'd')
		{
			dd = va_arg( ap, int );
			vevo_property_set( p, param_name, VEVO_ATOM_TYPE_INT,1, &dd );
		}
		else
		{
			ds = va_arg( ap, char*);
			if(!ds)
			 vevo_property_set( p, param_name, VEVO_ATOM_TYPE_STRING, 0, NULL );
			else
			 vevo_property_set( p, param_name, VEVO_ATOM_TYPE_STRING,1, &ds );
		}

		vevo_property_set( p, descr_name, VEVO_ATOM_TYPE_STRING, 1,&descr );

		it += 3;

		if( ds )
			free( ds);
		if( descr )
			free( descr );
	}

	va_end(ap);

	return p;
}

void *		vj_event_vevo_get_event_function( int id )
{
	void *func = NULL;
	if( index_map_[id] )
		vevo_property_get( index_map_[id] , "function", 0, &func );
	return func;
}

char	*vj_event_vevo_get_event_name( int id )
{
	char *descr = NULL;
	size_t len = vevo_property_element_size( index_map_[id], "description", 0  );
	if(len > 0 )
	{
		descr = (char*) malloc(sizeof(char) * len );
		vevo_property_get( index_map_[id], "description", 0, &descr );
	}
	return descr;
}
char	*vj_event_vevo_get_event_format( int id )
{
	char *fmt = NULL;
	size_t len = vevo_property_element_size( index_map_[id], "format", 0 );
	if(len > 0 )
	{
		fmt = (char*) malloc(sizeof(char) * len );
		vevo_property_get( index_map_[id], "format", 0, &fmt );
	}
	return fmt;
}

int	vj_event_exists( int id )
{
	if( index_map_[id])
		return 1;
	return 0;	
}


int	vj_event_vevo_get_default_value(int id, int p)
{
	int n =0;
	char key[15];
	sprintf(key, "argument_%d",p);
	vevo_property_get(index_map_[id], key, 0, &n );
	return n;
}
int	vj_event_vevo_get_num_args(int id)
{
	int n =0;
	vevo_property_get(index_map_[id], "arguments", 0, &n );
	return n;
}
int		vj_event_vevo_get_flags( int id )
{
	int flags = 0;
	vevo_property_get( index_map_[id], "flags", 0, &flags );
	return flags;
}

int		vj_event_vevo_get_vims_id( int id )
{
	int vims_id = 0;
	vevo_property_get( index_map_[id], "vims_id", 0, &vims_id );
	return vims_id;
}

void		vj_event_vevo_dump(void)
{
	int i;
	veejay_msg(VEEJAY_MSG_INFO, "VIMS  Syntax: '<selector>:<arguments>;'");
	veejay_msg(VEEJAY_MSG_INFO, "Use arguments according to FORMAT");
	veejay_msg(VEEJAY_MSG_INFO, "FORMAT controls the arguments as in C printf. Interpreted sequences are:");
	veejay_msg(VEEJAY_MSG_INFO, "\t%%d\tinteger");
	veejay_msg(VEEJAY_MSG_INFO, "\t%%s\tstring");
	
	for( i = 0; i < MAX_INDEX; i ++ )
	{
		if( index_map_[i] )
		{
			dump_event_stderr( index_map_[i] );
		}
	}
}

void		vj_event_vevo_free(void)
{
	int i;
#ifdef STRICT_CHECKING
	assert( index_map_ != NULL );
#endif
	if( !index_map_)
		return;

	for( i = 0 ; i < MAX_INDEX  ; i ++ )
	  if( index_map_[i] ) vevo_port_free( index_map_[i] );
	
	free(index_map_);
}

void		vj_init_vevo_events(void)
{	
	index_map_ = (vevo_port_t*) malloc(sizeof(vevo_port_t*) * MAX_INDEX );
	memset( index_map_, 0, sizeof( vevo_port_t *) * MAX_INDEX );


	index_map_[VIMS_SAMPLE_NEW] 		=	_new_event(
				"%d %d %s",
				VIMS_SAMPLE_NEW,
				"Create a new sample",
				vj_event_sample_new,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				"Type",
				0,
				"Number",
				0,
				"Token",
				NULL,
				NULL );

	
	index_map_[VIMS_SAMPLE_PLAY_FORWARD] = _new_event(  
				"%d",
				VIMS_SAMPLE_PLAY_FORWARD,
				"Play forward",
				vj_event_play_forward,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_SAMPLE_PLAY_BACKWARD]	= 	_new_event(
				"%d",
				 VIMS_SAMPLE_PLAY_BACKWARD,
				"Play backward",
				vj_event_play_reverse,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_SAMPLE_PLAY_STOP]	=	_new_event(
				"%d",
				VIMS_SAMPLE_PLAY_STOP,
				"Play stop",
				vj_event_play_stop,
				1,	
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_SAMPLE_SKIP_FRAME]	=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SKIP_FRAME,
				"Skip N frames forward",
				vj_event_inc_frame,
				2,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				"Number of frames", // param label
				1,		 // default
				NULL );
	
	index_map_[VIMS_SAMPLE_PREV_FRAME]	=	_new_event(
				"%d %d",
				VIMS_SAMPLE_PREV_FRAME,
				"Skip N frames backward",
				vj_event_dec_frame,
				2,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				"Number of frames",
				1,
				NULL );

	index_map_[VIMS_SAMPLE_SKIP_SECOND]	=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SKIP_SECOND,
				"Skip N seconds forward",
				vj_event_next_second,
				2,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				"Number of seconds",
				1,
				NULL );

	index_map_[VIMS_SAMPLE_PREV_SECOND]	=	_new_event(
				"%d %d",
				VIMS_SAMPLE_PREV_SECOND,
				"Skip N seconds backward",
				vj_event_prev_second,
				2,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				"Number of seconds",
				1,
				NULL );

	index_map_[VIMS_SAMPLE_GOTO_START]	=	_new_event(
				"%d",
				VIMS_SAMPLE_GOTO_START,
				"Go to starting position",
				vj_event_goto_start,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_SAMPLE_GOTO_END]		=	_new_event(	
				"%d",
				VIMS_SAMPLE_GOTO_END,
				"Go to ending position",
				vj_event_goto_end,
				1,
				VIMS_ALLOW_ANY,		
				SAMPLE_ID_HELP,
				0,
				NULL );
	
	index_map_[VIMS_SAMPLE_SET_SPEED] 	= 	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_SPEED,
				"Change trickplay speed",
				vj_event_play_speed,
				2,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				"Frame step",	
				1,	
				NULL );

	index_map_[VIMS_SAMPLE_SET_SLOW] 	= 	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_SLOW,
				"Change repeat speed",
				vj_event_play_repeat,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Frame repeat",	
				0,	
				NULL );

	index_map_[VIMS_SAMPLE_SET_FRAME]	= 	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_FRAME,
				"Set current frame number",
				vj_event_set_frame,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Frame number",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_DEL]				=	_new_event(
				"%d",
				VIMS_SAMPLE_DEL,
				"Delete sample",
				vj_event_sample_del,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Sample ID >= 1",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_COPY]				=	_new_event(
				"%d",
				VIMS_SAMPLE_COPY,	
				"Copy sample to new",
				vj_event_sample_copy,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_SAMPLE_SELECT]				=	_new_event(
				"%d",
				VIMS_SAMPLE_SELECT,
				"Select and play sample",
				vj_event_sample_select,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_SAMPLE_CHAIN_CLEAR]				=	_new_event(
				"%d",	
				VIMS_SAMPLE_CHAIN_CLEAR,
				"Reset Effect Chain",
				vj_event_chain_clear,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_STREAM_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_SAMPLE_CHAIN_ENTRY_SET_ACTIVE]			=	_new_event(
				"%d %d %d",
				VIMS_SAMPLE_CHAIN_ENTRY_SET_ACTIVE,
				"Activate or deactivate processing of a fx slot",
				vj_event_chain_entry_set_active,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				SAMPLE_FX_ENTRY_HELP,
				0,
				"0=off,1=on",
				1,
				NULL );
		index_map_[VIMS_SET_PORT] =	_new_event(
				"%s",
				VIMS_SET_PORT,
				"Set properties",
				vj_event_none,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"formatted text",
				NULL,
				NULL );

	index_map_[VIMS_SAMPLE_CHAIN_ENTRY_SET_FX]			=	_new_event(
				"%d %d %s",
				VIMS_SAMPLE_CHAIN_ENTRY_SET_FX,
				"Put an Effect on a Entry",
				vj_event_chain_entry_set,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				SAMPLE_FX_ENTRY_HELP,
				0,
				"Effect name",
				NULL,
				NULL );

	index_map_[VIMS_SAMPLE_CHAIN_ENTRY_CLEAR]			=	_new_event(
				"%d %d",
				VIMS_SAMPLE_CHAIN_ENTRY_CLEAR,
				"Clear an entry",
				vj_event_chain_entry_clear,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				SAMPLE_FX_ENTRY_HELP,
				0,
				NULL );
	
	index_map_[VIMS_SAMPLE_CHAIN_ENTRY_SET_INPUT]			=	_new_event(
				"%d %d %d %d",
				VIMS_SAMPLE_CHAIN_ENTRY_SET_INPUT,
				"Set Input Channel",
				vj_event_chain_entry_set_input,
				4,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				SAMPLE_FX_ENTRY_HELP,
				0,
				"Input Channel ID",
				0,
				SAMPLE_ID_HELP,
				0,
				NULL );
				
	index_map_[VIMS_SAMPLE_CHAIN_ENTRY_SET_VALUE]			=	_new_event(
				"%d %d %d %s",
				VIMS_SAMPLE_CHAIN_ENTRY_SET_VALUE,
				"Set Input parameter value",
				vj_event_chain_entry_set_parameter_value,
				4,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				SAMPLE_FX_ENTRY_HELP,
				0,
				"Input Parameter ID",
				0,
				"Character string",
				NULL,
				NULL );		

	index_map_[VIMS_SAMPLE_CHAIN_ENTRY_SET_ALPHA]			=	_new_event(
				"%d %d %d",
				VIMS_SAMPLE_CHAIN_ENTRY_SET_ALPHA,
				"Set opacity of an entry",
				vj_event_chain_entry_set_alpha,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				SAMPLE_FX_ENTRY_HELP,
				0,
				"Opacity value",
				256,
				NULL );


	index_map_[VIMS_SAMPLE_DETACH_OUT_PARAMETER]	= _new_event(
				"%d %d %d",
				VIMS_SAMPLE_DETACH_OUT_PARAMETER,
				"Detach output parameter",
				vj_event_sample_detach_out_parameter,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				SAMPLE_FX_ENTRY_HELP,
				0,
				"Output parameter",
				-1,
				NULL );

	index_map_[VIMS_SAMPLE_ATTACH_OUT_PARAMETER] = _new_event(
				"%d %d %d %d %d",
				VIMS_SAMPLE_ATTACH_OUT_PARAMETER,
				"Attach output parameter",
				vj_event_sample_attach_out_parameter,
				5,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				SAMPLE_FX_ENTRY_HELP,
				0,
				"Output parameter",
				-1,
				SAMPLE_FX_ENTRY_HELP,
				0,
				"Input parameter",
				-1,
				NULL );

	index_map_[VIMS_PERFORMER_SETUP_PREVIEW]	= _new_event(
				"%d %d",
				VIMS_PERFORMER_SETUP_PREVIEW,
				"Configure preview image",
				vj_event_performer_configure_preview,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Preview mode",
				0,
				"Recude",
				0,
				NULL );

	index_map_[VIMS_PERFORMER_GET_PREVIEW] 		= _new_event(
				NULL,
				VIMS_PERFORMER_GET_PREVIEW,
				"Get preview image",
				vj_event_performer_get_preview_image,
				0,
				VIMS_REQUIRE_ALL_PARAMS,
				NULL,
				NULL );
	
	index_map_[VIMS_SAMPLE_CONFIGURE_RECORDER]	 = _new_event(
				"%d %d %d %s",
				VIMS_SAMPLE_CONFIGURE_RECORDER,
				"Configure sample recorder",
				vj_event_sample_configure_recorder,
				4,
				VIMS_LONG_PARAMS|VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Dataformat",
				0,
				"Number of frames",
				0,
				"Filename",
				NULL,
				NULL );

	index_map_ [ VIMS_SAMPLEBANK_LIST ] = _new_event(
				NULL,
				VIMS_SAMPLEBANK_LIST,
				"List samples in samplebank",
				vj_event_samplebank_list,
				0,
				VIMS_REQUIRE_ALL_PARAMS,
				NULL,
				NULL );

	index_map_ [ VIMS_SAMPLEBANK_ADD ] = _new_event(
				"%d %d %s",
				VIMS_SAMPLEBANK_ADD,
				"Add file to samplebank",
				vj_event_samplebank_add,
				3,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Type of sample",
				0,
				"Extra token",
				0,
				"Filename",
				NULL,
				NULL );

	index_map_[ VIMS_SAMPLEBANK_DEL ] = _new_event(
				"%d",
				VIMS_SAMPLEBANK_DEL,
				"Delete sample from samplebank",
				vj_event_samplebank_del,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Sample ID",
				0,
				NULL );
	
	index_map_ [ VIMS_FX_LIST ] = _new_event(
				NULL,
				VIMS_FX_LIST,
				"List all loaded plugins",
				vj_event_fx_list,
				0,
				VIMS_REQUIRE_ALL_PARAMS,
				NULL,
				NULL );

	index_map_[ VIMS_FX_DETAILS ] = _new_event(
				"%s",
				VIMS_FX_DETAILS,
				"Get plugin information",
				vj_event_fx_info,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Plugin name",
				NULL,
				NULL );	
	index_map_[ VIMS_FX_CURRENT_DETAILS ] = _new_event(
				"%d %d",
				VIMS_FX_DETAILS,
				"Get fx information on sample entry",
				vj_event_sample_fx_details,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				SAMPLE_FX_ENTRY_HELP,
				0,
				NULL );	

	index_map_[ VIMS_FX_CHAIN ] = _new_event(
				"%d",
				VIMS_FX_CHAIN,
				"Get fx chain of sample",
				vj_event_sample_fx_chain,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				NULL );
	
	index_map_ [ VIMS_SAMPLE_START_RECORDER ] = _new_event(
				"%d",
				VIMS_SAMPLE_START_RECORDER,
				"Start recording from sample",
				vj_event_sample_start_recorder,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				NULL );

	index_map_[ VIMS_SAMPLE_STOP_RECORDER ] = _new_event(
				"%d",
				VIMS_SAMPLE_STOP_RECORDER,
				"Stop recording from sample",
				vj_event_sample_stop_recorder,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				NULL );
			
		
	index_map_[VIMS_SAMPLE_EDL_PASTE_AT]			=	_new_event(
				"%d %d",
				VIMS_SAMPLE_EDL_PASTE_AT,
				"Paste frames from buffer at frame into edit descision list",
				vj_event_el_paste_at,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Position to insert frames",
				0,
				NULL );
	
	index_map_[VIMS_SAMPLE_EDL_CUT]				=	_new_event(
				"%d %d %d",
				VIMS_SAMPLE_EDL_CUT,
				"Cut frames from edit descision list to buffer",
				vj_event_el_cut,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,	
				"Starting position",
				0,
				"Ending position",
				0,
				NULL );
	
	index_map_[VIMS_SAMPLE_EDL_COPY]				=	_new_event(
				"%d %d %d",
				VIMS_SAMPLE_EDL_COPY,
				"Copy frames from edit descision list to buffer",
				vj_event_el_copy,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Starting position",
				0,
				"Ending position",
				0,
				NULL );
	
	index_map_[VIMS_SAMPLE_EDL_DEL]				=	_new_event(
				"%d %d",
				VIMS_SAMPLE_EDL_DEL,
				"Delete frames from editlist (no undo!)",
				vj_event_el_del,
				3,	
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Starting position",
				0,
				"Ending position",
				0,
				NULL );
	
	
	index_map_[VIMS_SAMPLE_EDL_ADD]				=	_new_event(
				"%s",
				VIMS_SAMPLE_EDL_ADD,
				"Add video file to edit descision list",
				vj_event_el_add_video,
				1,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Filename",
				NULL,
				NULL );

	index_map_[VIMS_SAMPLE_SET_VOLUME]			=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_VOLUME,
				"Set audio volume",
				vj_event_set_volume,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Volume 0-100",
				0,
				NULL );
	index_map_[VIMS_DEBUG_LEVEL]			=	_new_event(
				NULL,
				VIMS_DEBUG_LEVEL,
				"More/Less verbosive console output",
				vj_event_debug_level,
				0,
				VIMS_ALLOW_ANY,	
				NULL );
	index_map_[VIMS_BEZERK]				=	_new_event(
				NULL,
				VIMS_BEZERK,	
				"Bezerk mode toggle ",
				vj_event_bezerk,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	
#ifdef USE_DISPLAY
	index_map_[VIMS_RESIZE_SCREEN]			=	_new_event(
				"%d %d %d %d",
				VIMS_RESIZE_SCREEN,
				"(OUT) Resize video display",
				vj_event_set_screen_size,
				4,
				VIMS_REQUIRE_ALL_PARAMS,
				"Width",
				0,
				"Height",
				0,
				"X offset",
				0,
				"Y offset",
				0,
				NULL );
	index_map_[VIMS_FULLSCREEN]			=	_new_event(
				"%d",
				VIMS_FULLSCREEN,
				"Enable / Disable Fullscreen video output",
				vj_event_fullscreen,
				1,
				VIMS_ALLOW_ANY,
				"On = 1, Off=0",
				1,
				NULL );

#endif
	index_map_[VIMS_QUIT]				=	_new_event(
				NULL,
				VIMS_QUIT,
				"Quit Veejay (caution!)",
				vj_event_quit,
				0,
				VIMS_ALLOW_ANY,
				NULL );

}



