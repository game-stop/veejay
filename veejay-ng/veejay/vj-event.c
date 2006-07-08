/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */

#include <config.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-common.h>
#include <veejay/veejay.h>
#include <veejay/defs.h>
#include <veejay/libveejay.h>
#include <veejay/performer.h>
#include <mjpegtools/mpegconsts.h>
#include <mjpegtools/mpegtimecode.h>
#include <veejay/vims.h>
#include <veejay/vj-event.h>
#include <libvevo/libvevo.h>
#include <vevosample/vevosample.h>
#include <libplugger/plugload.h>
#include <libvjnet/vj-server.h>
#include <libhash/hash.h>
#include <veejay/vj-jack.h>
#include <veejay/vj-misc.h>

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#define SEND_BUF 125000
#define MSG_MIN_LEN 4
#define MESSAGE_SIZE 300

static int _last_known_num_args = 0;

/* define the function pointer to any event */
typedef void (*vj_event)(void *ptr, const char format[], va_list ap);

void vj_event_create_effect_bundle(veejay_t * v,char *buf, int key_id, int key_mod );

/* struct for runtime initialization of event handlers */
typedef struct {
	int list_id;			// VIMS id
	vj_event act;			// function pointer
} vj_events;

static	vj_events	net_list[VIMS_MAX];

static char _print_buf[SEND_BUF];
static char _s_print_buf[SEND_BUF];

enum {
	VJ_ERROR_NONE=0,	
	VJ_ERROR_MODE=1,
	VJ_ERROR_EXISTS=2,
	VJ_ERROR_VIMS=3,
	VJ_ERROR_DIMEN=4,
	VJ_ERROR_MEM=5,
	VJ_ERROR_INVALID_MODE = 6,
};

#define VIMS_REQUIRE_ALL_PARAMS (1<<0)			/* all params needed */
#define VIMS_DONT_PARSE_PARAMS (1<<1)		/* dont parse arguments */
#define VIMS_LONG_PARAMS (1<<3)				/* long string arguments (bundle, plugin) */
#define VIMS_ALLOW_ANY (1<<4)				/* use defaults when optional arguments are not given */			

#define FORMAT_MSG(dst,str) sprintf(dst,"%03d%s",strlen(str),str)
#define APPEND_MSG(dst,str) strncat(dst,str,strlen(str))
#define SEND_MSG_DEBUG(v,str) \
{\
char *__buf = str;\
int  __len = strlen(str);\
int  __done = 0;\
veejay_msg(VEEJAY_MSG_INFO, "--------------------------------------------------------");\
for(__done = 0; __len > (__done + 80); __done += 80)\
{\
	char *__tmp = strndup( str+__done, 80 );\
veejay_msg(VEEJAY_MSG_INFO, "[%d][%s]",strlen(str),__tmp);\
	if(__tmp) free(__tmp);\
}\
veejay_msg(VEEJAY_MSG_INFO, "[%s]", str + __done );\
vj_server_send(v->command_socket, v->current_link, __buf, strlen(__buf));\
veejay_msg(VEEJAY_MSG_INFO, "--------------------------------------------------------");\
}

#define SEND_MSG(v,str)\
{\
vj_server_send(v->command_socket, v->current_link, str, strlen(str));\
}
#define RAW_SEND_MSG(v,str,len)\
{\
vj_server_send(v->command_socket,v->current_link, str, len );\
}	

#define SEND_LOG_MSG(v,str)\
{\
vj_server_send(v->frame_socket, v->current_link,str,strlen(str));\
}

/* some macros for commonly used checks */


#define P_A(a,b,c,d)\
{\
int __z = 0;\
unsigned char *__tmpstr = NULL;\
if(a!=NULL){\
unsigned int __rp;\
unsigned int __rplen = (sizeof(a) / sizeof(int) );\
for(__rp = 0; __rp < __rplen; __rp++) a[__rp] = 0;\
}\
while(*c) { \
if(__z > _last_known_num_args )  break; \
switch(*c++) {\
 case 's':\
__tmpstr = (char*)va_arg(d,char*);\
if(__tmpstr != NULL) {\
	sprintf( b,"%s",__tmpstr);\
	}\
__z++ ;\
 break;\
 case 'd': a[__z] = *( va_arg(d, int*)); __z++ ;\
 break; }\
 }\
}

/* P_A16: Parse 16 integer arguments. This macro is used in 1 function */
#define P_A16(a,c,d)\
{\
int __z = 0;\
while(*c) { \
if(__z > 15 )  break; \
switch(*c++) { case 'd': a[__z] = va_arg(d, int); __z++ ; break; }\
}}\


#define DUMP_ARG(a)\
if(sizeof(a)>0){\
int __l = sizeof(a)/sizeof(int);\
int __i; for(__i=0; __i < __l; __i++) veejay_msg(VEEJAY_MSG_DEBUG,"[%02d]=[%06d], ",__i,a[__i]);}\
else { veejay_msg(VEEJAY_MSG_DEBUG,"arg has size of 0x0");}


#define CLAMPVAL(a) { if(a<0)a=0; else if(a >255) a =255; }
#define IS_BOOLEAN(a)  ( (a == 0 || a==1) ? 1 : 0 )
#define BOOLEAN_ERROR(i) { veejay_msg(VEEJAY_MSG_ERROR, "Argument %d must be 0 or 1",i); }

#define VALID_RESOLUTION(w,h,x,y) (( (w >=64 && w<=2048) && (h >= 64 && h <= 2048) && (x >= 0 && x <2048) && (y>=0 && y <2048)) ? 1: 0 )

static inline hash_val_t int_bundle_hash(const void *key)
{
	return (hash_val_t) key;
}

static inline int int_bundle_compare(const void *key1,const void *key2)
{
	return ((int)key1 < (int) key2 ? -1 : 
		((int) key1 > (int) key2 ? +1 : 0));
}

static inline void* which_sample( veejay_t *info, int *args )
{
	if(args[0] == 0 )
		return info->current_sample;
	if(args[0] == -1)
		return sample_last();
	void *res = find_sample( args[0] );	
	if(!res)
		veejay_msg(VEEJAY_MSG_ERROR, "Sample '%d' does not exist", args[0]);
	return res;
}

typedef struct {
	int event_id;
	int accelerator;
	int modifier;
	char *bundle;
} vj_msg_bundle;


/* forward declarations (former console sample/tag print info) */
void vj_event_print_sample_info(veejay_t *v, int id); 
//int vj_event_bundle_update( vj_msg_bundle *bundle, int bundle_id );
//vj_msg_bundle *vj_event_bundle_get(int event_id);
//int vj_event_bundle_exists(int event_id);
//int vj_event_suggest_bundle_id(void);
//int vj_event_load_bundles(char *bundle_file);
//int vj_event_bundle_store( vj_msg_bundle *m );
//int vj_event_bundle_del( int event_id );
//vj_msg_bundle *vj_event_bundle_new(char *bundle_msg, int event_id);
void vj_event_trigger_function(void *ptr, vj_event f, int max_args, const char format[], ...); 
//void  vj_event_parse_bundle(veejay_t *v, char *msg );
void vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen, int type);
void    vj_event_commit_bundle( veejay_t *v, int key_num, int key_mod);

#ifdef HAVE_XML2
void    vj_event_format_xml_event( xmlNodePtr node, int event_id );
void	vj_event_format_xml_stream( xmlNodePtr node, int stream_id );
#endif

void	vj_event_init(void);
/*
int vj_event_bundle_update( vj_msg_bundle *bundle, int bundle_id )
{
	if(bundle) {
		hnode_t *n = hnode_create(bundle);
		if(!n) return 0;
		hnode_put( n, (void*) bundle_id);
		hnode_destroy(n);
		return 1;
	}
	return 0;
}

vj_msg_bundle *vj_event_bundle_get(int event_id)
{
	vj_msg_bundle *m;
	hnode_t *n = hash_lookup(BundleHash, (void*) event_id);
	if(n) 
	{
		m = (vj_msg_bundle*) hnode_get(n);
		if(m)
		{
			return m;
		}
	}
	return NULL;
}*/
/*
int vj_event_bundle_exists(int event_id)
{
	hnode_t *n = hash_lookup( BundleHash, (void*) event_id );
	if(!n)
		return 0;
	return ( vj_event_bundle_get(event_id) == NULL ? 0 : 1);
}

int vj_event_suggest_bundle_id(void)
{
	int i;
	for(i=VIMS_BUNDLE_START ; i < VIMS_BUNDLE_END; i++)
	{
		if ( vj_event_bundle_exists(i ) == 0 ) return i;
	}

	return -1;
}

int vj_event_bundle_store( vj_msg_bundle *m )
{
	hnode_t *n;
	if(!m) return 0;
	n = hnode_create(m);
	if(!n) return 0;
	if(!vj_event_bundle_exists(m->event_id))
	{
		hash_insert( BundleHash, n, (void*) m->event_id);
	}
	else
	{
		hnode_put( n, (void*) m->event_id);
		hnode_destroy( n );
	}

	// add bundle to VIMS list
	veejay_msg(VEEJAY_MSG_DEBUG,
		"Added Bundle VIMS %d to net_list", m->event_id );
 
	net_list[ m->event_id ].list_id = m->event_id;
	net_list[ m->event_id ].act = vj_event_none;


	return 1;
}
int vj_event_bundle_del( int event_id )
{
	hnode_t *n;
	vj_msg_bundle *m = vj_event_bundle_get( event_id );
	if(!m) return -1;

	n = hash_lookup( BundleHash, (void*) event_id );
	if(!n)
		return -1;

	net_list[ m->event_id ].list_id = 0;
	net_list[ m->event_id ].act = vj_event_none;

#ifdef USE_DISPLAY
	vj_event_unregister_keyb_event( m->accelerator, m->modifier );
#endif	

	if( m->bundle )
		free(m->bundle);
	if(m)
		free(m);
	m = NULL;

	hash_delete( BundleHash, n );


	return 0;
}

vj_msg_bundle *vj_event_bundle_new(char *bundle_msg, int event_id)
{
	vj_msg_bundle *m;
	int len = 0;
	if(!bundle_msg || strlen(bundle_msg) < 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Doesnt make sense to store empty bundles in memory");
		return NULL;
	}	

	len = strlen(bundle_msg);
	m = (vj_msg_bundle*) malloc(sizeof(vj_msg_bundle));
	if(!m) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error allocating memory for bundled message");
		return NULL;
	}
	memset(m, 0, sizeof(m) );
	m->bundle = (char*) malloc(sizeof(char) * len+1);
	bzero(m->bundle, len+1);
	m->accelerator = 0;
	m->modifier = 0;
	if(!m->bundle)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error allocating memory for bundled message context");
		return NULL;
	}
	strncpy(m->bundle, bundle_msg, len);
	
	m->event_id = event_id;

	veejay_msg(VEEJAY_MSG_DEBUG, 
		"New VIMS Bundle %d [%s]",
			event_id, m->bundle );

	return m;
}
*/

void vj_event_trigger_function(void *ptr, vj_event f, int max_args, const char *format, ...) 
{
	va_list ap;
	va_start(ap,format);
	f(ptr, format, ap);	
	va_end(ap);
}


int vj_event_parse_msg(veejay_t *v, char *msg);

/* parse a message received from network */
void vj_event_parse_bundle(veejay_t *v, char *msg )
{

	int num_msg = 0;
	int offset = 3;
	int i = 0;
	
	
	if ( msg[offset] == ':' )
	{
		int j = 0;
		offset += 1; /* skip ':' */
		if( sscanf(msg+offset, "%03d", &num_msg )<= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) Invalid number of messages. Skipping message [%s] ",msg);
		}
		if ( num_msg <= 0 ) 
		{
			veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) Invalid number of message given to execute. Skipping message [%s]",msg);
			return;
		}

		offset += 3;

		if ( msg[offset] != '{' )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) 'al' expected. Skipping message [%s]",msg);
			return;
		}	

		offset+=1;	/* skip # */

		for( i = 1; i <= num_msg ; i ++ ) /* iterate through message bundle and invoke parse_msg */
		{				
			char atomic_msg[256];
			int found_end_of_msg = 0;
			int total_msg_len = strlen(msg);
			bzero(atomic_msg,256);
			while( (offset+j) < total_msg_len)
			{
				if(msg[offset+j] == '}')
				{
					return; /* dont care about semicolon here */
				}	
				else
				if(msg[offset+j] == ';')
				{
					found_end_of_msg = offset+j+1;
					strncpy(atomic_msg, msg+offset, (found_end_of_msg-offset));
					atomic_msg[ (found_end_of_msg-offset) ] ='\0';
					offset += j + 1;
					j = 0;
					vj_event_parse_msg( v, atomic_msg );
				}
				j++;
			}
		}
	}
}

void vj_event_dump()
{
	vj_event_vevo_dump();
	
}

typedef struct {
	void *value;
} vims_arg_t; 

static	void	dump_arguments_(int net_id,int arglen, int np, int prefixed, char *fmt)
{
	int i;
	char *name = vj_event_vevo_get_event_name( net_id );
	veejay_msg(VEEJAY_MSG_ERROR, "VIMS '%03d' : '%s'", net_id, name );
	veejay_msg(VEEJAY_MSG_ERROR, "Invalid number of arguments given: %d out of %d",
			arglen,np);	
	veejay_msg(VEEJAY_MSG_ERROR, "Format is '%s'", fmt );

	for( i = prefixed; i < np; i ++ )
	{
		char *help = vj_event_vevo_help_vims( net_id, i );
		veejay_msg(VEEJAY_MSG_ERROR,"\tArgument %d : %s",
			i,help );
		if(help) free(help);
	}
}

static	void	dump_argument_( int net_id , int i )
{
	char *help = vj_event_vevo_help_vims( net_id, i );
		veejay_msg(VEEJAY_MSG_ERROR,"\tArgument %d : %s",
			i,help );
	if(help) free(help);
}

static	int	vj_event_verify_args( int *fx, int net_id , int arglen, int np, int prefixed, char *fmt )
{
	return 1;
}

void	vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen, int prefixed)
{
	int np = vj_event_vevo_get_num_args(net_id);
	char *fmt = vj_event_vevo_get_event_format( net_id );
	int flags = vj_event_vevo_get_flags( net_id );
	int fmt_offset = 1; 
	vims_arg_t	vims_arguments[16];
	memset( vims_arguments, 0, sizeof(vims_arguments) );

	if(!vj_event_verify_args(args , net_id, arglen, np, prefixed, fmt ))
	{
		if(fmt) free(fmt);
		return;
	}

	if( np == 0 )
	{
		vj_event_vevo_inline_fire( (void*) v, net_id,NULL,NULL );
		//vj_event_vevo_inline_fire_default( (void*) v, net_id, fmt );
		//if(fmt) free(fmt);
		return;
	}
	
	int i=0;
	while( i < arglen )
	{
		if( fmt[fmt_offset] == 'd' )
		{
			vims_arguments[i].value = (void*) &(args[i]);
		}
		if( fmt[fmt_offset] == 's' )
		{
			if(str_arg == NULL )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Argument %d must be a string!", i );
				if(fmt) free(fmt);
				return;
			}
			vims_arguments[i].value = (void*) strdup( str_arg );
			if(flags & VIMS_REQUIRE_ALL_PARAMS )
			{
				if( strlen((char*)vims_arguments[i].value) <= 0 )
				{
					veejay_msg(VEEJAY_MSG_ERROR, "Argument %d is not a string!",i );
					if(fmt)free(fmt);
					return;
				}
			}
		}
		fmt_offset += 3;
		i++;
	}
	_last_known_num_args = arglen;

	while( i < np )
	{
		int dv = vj_event_vevo_get_default_value( net_id, i);
		if( fmt[fmt_offset] == 'd' )
		{
			vims_arguments[i].value = (void*) &(dv);
		}
		i++;
	}

	vj_event_vevo_inline_fire( (void*) v, net_id, 	
				fmt,
				vims_arguments[0].value,
				vims_arguments[1].value,
				vims_arguments[2].value,
				vims_arguments[3].value,
				vims_arguments[4].value,
				vims_arguments[5].value,
				vims_arguments[6].value,		
				vims_arguments[7].value,
				vims_arguments[8].value,
				vims_arguments[9].value,
				vims_arguments[10].value,
				vims_arguments[11].value,
				vims_arguments[12].value,
				vims_arguments[13].value,
				vims_arguments[14].value,
				vims_arguments[15].value);
	fmt_offset = 1;
	for ( i = 0; i < np ; i ++ )
	{
		if( vims_arguments[i].value &&
			fmt[fmt_offset] == 's' )
			free( vims_arguments[i].value );
		fmt_offset += 3;
	}
	if(fmt)
		free(fmt);

}

static		int	inline_str_to_int(const char *msg, int *val)
{
	char longest_num[16];
	int str_len = 0;
	if( sscanf( msg , "%d", val ) <= 0 )
		return 0;
	bzero( longest_num, 16 );
	sprintf(longest_num, "%d", *val );

	str_len = strlen( longest_num ); 
	return str_len;
}

static		char 	*inline_str_to_str(int flags, char *msg)
{
	char *res = NULL;	
	int len = strlen(msg);
	if( len <= 0 )
		return res;

	if( (flags & VIMS_LONG_PARAMS) ) /* copy rest of message */
	{
		res = (char*) malloc(sizeof(char) * len );
		memset(res, 0, len );
		if( msg[len-1] == ';' )
			strncpy( res, msg, len- 1 );
	}
	else			
	{
		char str[255];
		bzero(str, 255 );
		if(sscanf( msg, "%s", str ) <= 0 )
			return res;
		res = strndup( str, 255 ); 	
	}	
	return res;
}

static	char	*vj_event_get_port_name(char *in , char *buf)
{
	char *c = in;
	int n = 0;
	while( *c )
	{
		if( *c == '#' )
		{
			*c++;
			n++;
			strncpy(buf,in,n);
			return c;
		}
		*c++;
		n++;
	}
	return NULL;
}

static	void	vj_event_parse_port( veejay_t *v, char *msg )
{
	char port_name[128];

	char *port_str = vj_event_get_port_name( msg, port_name );

	if(!port_str)
	{
		veejay_msg(0, "Invalid property set");
		return;
	}

	int        len = strlen(port_str);
	if( port_str[len-1] == ';' )
		port_str[len-1] = '\0';


	if( strncasecmp(port_name, "sample#",5 ) == 0 )
	{
		sample_sscanf_port( v->current_sample, port_str );
	}
	//@ setting fx on fx chain !! FIXME
	//if( strncasecmp(port_name, "fx", 2 ) == 0 )
	//{
	//	sample_fx_sscanf_port( v->current_sample, port_str, port_name);
	//}
}

int	vj_event_parse_msg( veejay_t * v, char *msg )
{
	char *head = NULL;
	int net_id = 0;
	int np = 0;
	if( msg == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Empty VIMS, dropped!");
		return 0;
	}
	veejay_msg(VEEJAY_MSG_INFO, "Parse: '%s'",msg);
	int msg_len = strlen( msg );

	veejay_chomp_str( msg, &msg_len );
	msg_len --;

	veejay_msg(VEEJAY_MSG_DEBUG, "VIMS: Parse message '%s'", msg );

	if( msg_len < MSG_MIN_LEN )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "VIMS Message too small, dropped!");
		return 0;
	}

	head = strndup( msg, 3 );

	if( strncasecmp( head, "bun", 3 ) == 0 )
	{
		vj_event_parse_bundle( v, msg );
		return 1;
	}

	/* try to scan VIMS id */
	if ( sscanf( head, "%03d", &net_id ) != 1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error parsing VIMS selector");
		return 0;
	}
	if( head ) free(head );
	
	if( net_id <= 0 || net_id >= VIMS_MAX || !vj_event_exists(net_id) )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "VIMS Selector %d invalid", net_id );
		return 0;
	}

	/* verify format */
	if( msg[3] != 0x3a || msg[msg_len] != ';' )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Syntax error in VIMS message");
		if( msg[3] != 0x3a )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "\tExpected ':' after VIMS selector");
			return 0;
		}
		if( msg[msg_len] != ';' )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "\tExpected ';' to terminate VIMS message");	
			return 0;
		}
	}

	//@ also for FX setting / removing etc etc
	if( net_id == VIMS_SET_PORT )
	{
		char *port_str = msg + 4;
		vj_event_parse_port( v, port_str );
		return 1;
	}
	//@ we can drop ALL VIMS Get stuff -> extract properties with vevo_printf

//	if( net_id >= 400 && net_id < 499 )
//		vj_server_client_promote( v->command_socket , v->current_link );

	np = vj_event_vevo_get_num_args( net_id );
		
	if ( msg_len <= MSG_MIN_LEN )
	{
		int i_args[16];
		int i = 0;
		while(  i < np  )
		{
			i_args[i] = vj_event_vevo_get_default_value( net_id, i );
			i++;
		}
		vj_event_fire_net_event( v, net_id, NULL, i_args, np, 0 );
	}
	else
	{
		char *arguments = NULL;
		char *fmt = vj_event_vevo_get_event_format( net_id );
		int flags = vj_event_vevo_get_flags( net_id );
		int i = 0;
		int i_args[16];
		char *str = NULL;
		int fmt_offset = 1;
		char *arg_str = NULL;
		memset( i_args, 0, sizeof(i_args) );

		arg_str = arguments = strndup( msg + 4 , msg_len - 3 );

		if( arguments == NULL )
		{
			dump_arguments_( net_id, 0, np, 0, fmt );
			if(fmt) free(fmt );
			return 0;
		}
		if( np <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "VIMS %d accepts no arguments", net_id );
			if(fmt) free(fmt);
			return 0;
		}
		
		while( i < np )
		{
			if( fmt[fmt_offset] == 'd' )
				i_args[i] = vj_event_vevo_get_default_value(net_id, i);
			i++;
		}
			
		for( i = 0; i < np; i ++ )
		{
			int failed_arg = 1;
			
			if( fmt[fmt_offset] == 'd' )
			{
				int il = inline_str_to_int( arguments, &i_args[i] );	
				if( il > 0 )
				{
					failed_arg = 0;
					arguments += il;
				}
			}
			if( fmt[fmt_offset] == 's' && str == NULL)
			{
				str = inline_str_to_str( flags,arguments );
				if(str != NULL )
				{
					failed_arg = 0;
					arguments += strlen(str);
				}
			}
			
			if( failed_arg )
			{
				char *name = vj_event_vevo_get_event_name( net_id );
				veejay_msg(VEEJAY_MSG_ERROR, "Invalid argument %d for VIMS '%03d' : '%s' ",
					i, net_id, name );
				if(name) free(name);
				dump_argument_( net_id, i );	
				if(fmt) free(fmt);
				return 0;
			}

			if( *arguments == ';' || *arguments == 0 )
				break;
			fmt_offset += 3;

			if( *arguments == 0x20 )	
			   *arguments ++;
		}

		i ++;

		if( flags & VIMS_ALLOW_ANY )
 			i = np;

		vj_event_fire_net_event( v, net_id, str, i_args, i, 0 );
		

		if(fmt) free(fmt);
		if(arg_str) free(arg_str);
		if(str) free(str);

		return 1;
		
	}

	return 0;
}

/*
	update connections
 */
void vj_event_update_remote(void *ptr)
{
	veejay_t *v = (veejay_t*)ptr;
	int cmd_poll = 0;	// command port
	int sta_poll = 0;	// status port
	int new_link = -1;
	int sta_link = -1;
	int msg_link = -1;
	int msg_poll = 0;
	int i;
	cmd_poll = vj_server_poll(v->command_socket);
	sta_poll = vj_server_poll(v->status_socket);
	msg_poll = vj_server_poll(v->frame_socket);
	// accept connection command socket    

	if( cmd_poll > 0)
	{
		new_link = vj_server_new_connection ( v->command_socket );
	}
	// accept connection on status socket
	if( sta_poll > 0) 
	{
		sta_link = vj_server_new_connection ( v->status_socket );
	}
	if( msg_poll > 0)
	{
		msg_link = vj_server_new_connection( v->frame_socket );
	}

	// see if there is any link interested in status information
	
	for( i = 0; i <  VJ_MAX_CONNECTIONS; i ++ )
		if( vj_server_link_used( v->status_socket, i ))
			veejay_playback_status( v, i );
/*	
	if( v->settings->use_vims_mcast )
	{
		int res = vj_server_update(v->vjs[2],0 );
		if(res > 0)
		{
			v->current_link = 0;
			char buf[MESSAGE_SIZE];
			bzero(buf, MESSAGE_SIZE);
			while( vj_server_retrieve_msg( v->vjs[2], 0, buf ) )
			{
				vj_event_parse_msg( v, buf );
				bzero( buf, MESSAGE_SIZE );
			}
		}
		
	}*/

	for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
	{	
		if( vj_server_link_used( v->command_socket, i ) )
		{
			int res = 1;
			while( res != 0 )
			{
				res = vj_server_update( v->command_socket, i );
				if(res>0)
				{
					v->current_link = i;
					char buf[MESSAGE_SIZE];
					bzero(buf, MESSAGE_SIZE);
					int n = 0;
					while( vj_server_retrieve_msg(v->command_socket,i,buf) != 0 )
					{
						vj_event_parse_msg( v, buf );
						bzero( buf, MESSAGE_SIZE );
						n++;
					}
				}	
				if( res == -1 )
				{
					veejay_msg(VEEJAY_MSG_DEBUG,
							"Lost connection with link %d",i);
					_vj_server_del_client( v->command_socket, i );
					_vj_server_del_client( v->status_socket, i );
					_vj_server_del_client( v->frame_socket, i );
					break;
				}

			}
		}
	}

	if(!veejay_keep_messages())
		veejay_reap_messages();
	
}


void	vj_event_lvd_parse_set_entry( veejay_t *v, const char *format[], va_list ap)
{
	//@ format is constructed dynamicly, based on whatever entry in sample
	//@ -> atom types of fx_values and fx_chain
}

void	vj_event_commit_bundle( veejay_t *v, int key_num, int key_mod)
{
	char bundle[4096];
	bzero(bundle,4096);
	vj_event_create_effect_bundle(v, bundle, key_num, key_mod );
}


//@ FIXME

void vj_event_none(void *ptr, const char format[], va_list ap)
{
	veejay_msg(VEEJAY_MSG_INFO, "No event attached on this key");
}

static	void	vj_event_send_new_id(veejay_t * v, int new_id)
{

	if( vj_server_client_promoted( v->command_socket, v->current_link ))
	{
		char result[6];
		if(new_id < 0 ) new_id = 0;
		bzero(result,6);
		bzero( _s_print_buf,SEND_BUF);

		sprintf( result, "%05d",new_id );
		sprintf(_s_print_buf, "%03d%s",5, result);	
		SEND_MSG( v,_s_print_buf );
	}
}

void	vj_event_read_file( void *ptr, 	const char format[], va_list ap )
{
	char file_name[512];
	int args[1];

//	P_A(args,file_name,format,ap);

}

void	vj_event_init_network_events()
{
	int i;
	int net_id = 0;
	for( i = 0; i <= 600; i ++ )
	{
		net_list[ net_id ].act =
			(vj_event) vj_event_vevo_get_event_function( i );

		if( net_list[ net_id ].act )
		{
			net_list[net_id].list_id = i;
			net_id ++;
		}
	}	
	veejay_msg(VEEJAY_MSG_INFO, "\tVIMS selectors:   %d", net_id );
}

void vj_event_init()
{
	int i;
	vj_init_vevo_events();
	for(i=0; i < VIMS_MAX; i++)
	{
		net_list[i].act = vj_event_none;
		net_list[i].list_id = 0;
	}

/*	if( !(BundleHash = hash_create(HASHCOUNT_T_MAX, int_bundle_compare, int_bundle_hash)))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize hashtable for message bundles");
		return;
	}*/
	veejay_msg(VEEJAY_MSG_INFO,"Initializing Event system");
	vj_event_init_network_events();
}

void	vj_event_stop()
{
	veejay_msg(VEEJAY_MSG_INFO, "Shutting down event system");
	// destroy bundlehash, destroy keyboard_events
	vj_event_vevo_free();
}
void vj_event_linkclose(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	veejay_msg(VEEJAY_MSG_INFO, "Remote requested session-end, quitting Client");
	int i = v->current_link;
        _vj_server_del_client( v->command_socket, i );
        _vj_server_del_client( v->status_socket, i );
        _vj_server_del_client( v->frame_socket, i );
}

void vj_event_quit(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	veejay_msg(VEEJAY_MSG_INFO, "Remote requested Quit.");
	veejay_change_state(v, VEEJAY_STATE_STOP);         
}

void vj_event_bezerk(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if(v->no_bezerk) v->no_bezerk = 0; else v->no_bezerk = 1;
	if(v->no_bezerk==1)
		veejay_msg(VEEJAY_MSG_INFO,"Bezerk On  :No sample-restart when changing input channels");
	else
		veejay_msg(VEEJAY_MSG_INFO,"Bezerk Off :Sample-restart when changing input channels"); 
}

void vj_event_debug_level(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if(v->verbose) v->verbose = 0; else v->verbose = 1;
	veejay_set_debug_level( v->verbose );
	if(v->verbose)
		veejay_msg(VEEJAY_MSG_INFO, "Displaying debug information" );
	else
		veejay_msg(VEEJAY_MSG_INFO, "Not displaying debug information");
}

void	vj_event_send_bundles(void *ptr, const char format[], va_list ap)
{
}

void	vj_event_send_vimslist(void *ptr, const char format[], va_list ap)
{
}



void vj_event_sample_select(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *s = NULL;	
	P_A( args, s , format, ap);

	void *sample = which_sample( v,args );
	if(sample)
	{
		if(sample == v->current_sample)
		{
			uint64_t pos = sample_get_start_pos( v->current_sample );
			sample_set_property_ptr(
				v->current_sample, "current_pos", VEVO_ATOM_TYPE_UINT64, &pos );
		}
		else
		{
			sample_save_cache_data( v->current_sample );
			v->current_sample = sample;
		}	
	}
}


void	vj_event_set_volume(void *ptr, const char format[], va_list ap)
{
	int args[1];	
	char *s = NULL;
	P_A(args,s,format,ap)
	if(args[0] >= 0 && args[0] <= 100)
	{
#ifdef HAVE_JACK
		if(vj_jack_set_volume(args[0])) //@ TODO: audio
		{
			veejay_msg(VEEJAY_MSG_INFO, "Volume set to %d", args[0]);
		}
#else
		veejay_msg(VEEJAY_MSG_ERROR, "Audio support not compiled in");
#endif
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Use a value between 0-100 for audio volume");
	}
}

void vj_event_sample_new(void *ptr, const char format[], va_list ap)
{
	int new_id = 0;
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *token[1024];
	bzero(token,1024);
	P_A(args,token,format, ap);
	
	const char *type_str = sample_describe_type( args[0] );
	if(type_str==NULL)
	{
		veejay_msg(0,"Invalid sample type: %d",args[0]);
	}
	else
	{
		void *sample = sample_new( args[0] );
		if(sample_open( sample, token,1, v->video_info))
		{
			new_id = samplebank_add_sample( sample );
		 	veejay_msg(VEEJAY_MSG_INFO,"Created new %s from %s as Sample %d",
			       type_str, token, new_id );
		}
 		else
		{
			veejay_msg(VEEJAY_MSG_ERROR,"Could not create sample from %s",token);
		}		
	}
}

void	vj_event_fullscreen(void *ptr, const char format[], va_list ap )
{
#ifdef USE_DISPLAY
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);

	if(!IS_BOOLEAN(args[0]))
	{
		BOOLEAN_ERROR(0);
		return;
	}		
		
	if( v->use_display == 2 )
	{
		if(x_display_set_fullscreen( v->display, args[0] ))
			veejay_msg(VEEJAY_MSG_INFO,"OpenGL display window Fullscreen %s",
					args[0] == 0 ? "disabled" : "enabled");
	}
	if( v->use_display == 1 )
	{
		if(vj_sdl_set_fullscreen( v->display, args[0] ))
			veejay_msg(VEEJAY_MSG_INFO,"SDL display window fullsceen %s",
					args[0] == 0 ? "disabled" : "enabled");
	}	
#endif
}
void vj_event_set_screen_size(void *ptr, const char format[], va_list ap) 
{
#ifdef USE_DISPLAY
	int args[5];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);

	int w  = args[0];
	int h  = args[1];
        int x  = args[2];
        int y  = args[3];

	if( !VALID_RESOLUTION(w,h,x,y))
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Invalid dimensions for video window. Width and Height must be >= 64 and <= 2048");
		return;
	}
	
	if( v->use_display == 2)
	{
		veejay_msg(VEEJAY_MSG_INFO, "OpenGL video window is %gx%g+%gx%g",
				w,h,x,y );
		x_display_resize( v->display );	
	}
	if( v->use_display == 1 )
	{
		 if(vj_sdl_resize_window(v->display,w,h,x,y))
			 veejay_msg(VEEJAY_MSG_INFO, "SDL video window is %dx%d+%dx%d",
					w,h,x,y); 
	}
#endif
}

void vj_event_play_stop(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*) ptr;
	int speed = sample_get_speed( v->current_sample );
	if(speed != 0)
	{
		speed = 0;
		sample_set_property_ptr(
				v->current_sample, "speed", VEVO_ATOM_TYPE_INT, &speed );
	}
}


void vj_event_play_reverse(void *ptr,const char format[],va_list ap) 
{
	veejay_t *v = (veejay_t*) ptr;
	int speed = sample_get_speed( v->current_sample );
	if(speed >= 0)
	{
		if(speed == 0)
			speed = 1;
		speed *= -1;
		sample_set_property_ptr(
				v->current_sample, "speed", VEVO_ATOM_TYPE_INT, &speed );
	}
}

void vj_event_play_forward(void *ptr, const char format[],va_list ap) 
{
	veejay_t *v = (veejay_t*) ptr;
	int speed = sample_get_speed( v->current_sample );
	if(speed <= 0)
	{
		if(speed == 0)
			speed = -1;
		speed *= -1;
		sample_set_property_ptr(
				v->current_sample, "speed", VEVO_ATOM_TYPE_INT, &speed );
	}
}

void vj_event_play_speed(void *ptr, const char format[], va_list ap)
{
	int args[5];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);

	int speed = sample_get_speed( v->current_sample );
	if(speed != args[0])
	{
		if( sample_valid_speed( v->current_sample, args[0] ))
		{
			sample_set_property_ptr(
					v->current_sample, "speed", VEVO_ATOM_TYPE_INT, &(args[0]) );
			veejay_msg(VEEJAY_MSG_INFO, "Playback speed changed to %d", args[0]);
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Playback speed %d bounces beyond sample boundaries", args[0]);
		}
	}
}

void vj_event_set_frame(void *ptr, const char format[], va_list ap)
{
	uint64_t args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;

//	P_A(args,s,format,ap);

	uint64_t pos = sample_get_current_pos( v->current_sample );
	if(pos != args[0])
	{
		if( sample_valid_pos( v->current_sample, args[0] ))
		{
			uint64_t new_pos = (uint64_t) args[0];
			sample_set_property_ptr(
					v->current_sample, "current_pos", VEVO_ATOM_TYPE_UINT64, &new_pos );
			veejay_msg(VEEJAY_MSG_INFO, "Position changed to %d", args[0]);
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Position %d outside sample boundaries", args[0]);
		}
	}
	

}

void vj_event_inc_frame(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	uint64_t pos = sample_get_current_pos( v->current_sample );
	pos += 1;

	if( sample_valid_pos( v->current_sample, pos ))
	{
		sample_set_property_ptr(
				v->current_sample, "current_pos", VEVO_ATOM_TYPE_UINT64, &pos );
		veejay_msg(VEEJAY_MSG_INFO, "Position changed to %lld",pos);
	}
}

void vj_event_dec_frame(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	
	uint64_t pos = sample_get_current_pos( v->current_sample );
	pos -= 1;

	if( sample_valid_pos( v->current_sample, pos ))
	{
		sample_set_property_ptr(
				v->current_sample, "current_pos", VEVO_ATOM_TYPE_UINT64, &pos );
		veejay_msg(VEEJAY_MSG_INFO, "Position changed to %lld", pos);
	}
}

void vj_event_prev_second(void *ptr, const char format[], va_list ap)
{
 	veejay_t *v = (veejay_t*) ptr;
	
 	sample_video_info_t *svit = (sample_video_info_t*) v->video_info;
	uint64_t pos = sample_get_current_pos( v->current_sample );
	
	pos += svit->fps;
	if( sample_valid_pos( v->current_sample, pos ))
	{
		sample_set_property_ptr(
				v->current_sample, "current_pos", VEVO_ATOM_TYPE_UINT64, &pos );
		veejay_msg(VEEJAY_MSG_INFO, "Position changed to %lld", pos);
	}
}

void vj_event_next_second(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	
        sample_video_info_t *svit = (sample_video_info_t*) v->video_info;
	uint64_t pos = sample_get_current_pos( v->current_sample );
	
	pos -= svit->fps;
	if( sample_valid_pos( v->current_sample, pos ))
	{
		sample_set_property_ptr(
				v->current_sample, "current_pos", VEVO_ATOM_TYPE_UINT64, &pos );
		veejay_msg(VEEJAY_MSG_INFO, "Position changed to %lld", pos);
	}
}

void vj_event_goto_end(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	
	sample_video_info_t *svit = (sample_video_info_t*) v->video_info;
	uint64_t pos = sample_get_end_pos( v->current_sample );
	
	sample_set_property_ptr(
			v->current_sample, "current_pos", VEVO_ATOM_TYPE_UINT64, &pos );
	veejay_msg(VEEJAY_MSG_INFO, "Position changed to %lld", pos);
}

void vj_event_goto_start(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
			
	sample_video_info_t *svit = (sample_video_info_t*) v->video_info;
	uint64_t pos = sample_get_start_pos( v->current_sample );
	
	sample_set_property_ptr(
			v->current_sample, "current_pos", VEVO_ATOM_TYPE_UINT64, &pos );
	veejay_msg(VEEJAY_MSG_INFO, "Position changed to %lld", pos);
}

void vj_event_set_property(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;

	// arbitrary parsing of arguments.
	
}

void vj_event_get_property_value(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args, s, format, ap);

}

#ifdef HAVE_XML2
void vj_event_sample_save_list(void *ptr, const char format[], va_list ap)
{
}

void vj_event_sample_load_list(void *ptr, const char format[], va_list ap)
{
}
#endif

void vj_event_sample_del(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args, s, format, ap);

	void *sample = which_sample(v,args[0]);
	if( sample == v->current_sample)
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot delete current playing sample");
	else
	{
		if( sample_delete( args[0] ) )
			veejay_msg(VEEJAY_MSG_ERROR,"Sample %d is deleted", args[0]);
	}
}

void vj_event_sample_copy(void *ptr, const char format[] , va_list ap)
{
}

void vj_event_sample_clear_all(void *ptr, const char format[], va_list ap)
{
} 

void vj_event_chain_clear(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args, s, format, ap);

	void *sample = which_sample(v,args[0]);
	if(sample)
	{
		sample_fx_chain_reset( sample );
		veejay_msg(VEEJAY_MSG_INFO, "Wiped contents of FX Chain in sample %d",
				args[0]);
	}
}

void vj_event_chain_entry_clear(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args, s, format, ap);

	void *sample = which_sample(v,args);
	if(sample)
	{
		if(sample_fx_chain_entry_clear( sample, args[1] ) )
		veejay_msg(VEEJAY_MSG_INFO, "Cleared FX slot %d in sample %d",
				args[1], args[0]);
	}

}

void vj_event_chain_entry_set_defaults(void *ptr, const char format[], va_list ap) 
{

}

void	vj_event_chain_entry_set_alpha( void *ptr, const char format[], va_list ap)
{
	int args[4];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args, s, format, ap);
	void *sample = which_sample( v, args );
	if( sample )
	{
		int idx = args[1];
		if( idx < 0 || idx >= SAMPLE_CHAIN_LEN )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					"Invalid entry '%d'", idx );
			return;
		}
		sample_set_fx_alpha( sample, idx, args[2] );
	}
}

void 	vj_event_chain_entry_set_input( void *ptr, const char format[], va_list ap )
{
	int args[4];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args, s, format, ap);

	void *sample = which_sample( v, args );
	if( sample )
	{
		int idx = args[1];
		if( idx < 0 || idx >= SAMPLE_CHAIN_LEN )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					"Invalid entry '%d'", idx );
			return;
		}

		int n = sample_fx_set_in_channel( sample, idx, args[2], args[3] );
		if(n)
			veejay_msg(VEEJAY_MSG_ERROR, "Input channel %d set to sample %d",args[2],args[3]);
		else
			veejay_msg(VEEJAY_MSG_ERROR, "Error setting input channel");
	}
}
void 	vj_event_chain_entry_set_parameter_value( void *ptr, const char format[], va_list ap )
{
	int args[4];
	veejay_t *v = (veejay_t*) ptr;
	char s[1024];
	P_A(args, s, format, ap);

	void *sample = which_sample( v, args );
	if( sample )
	{
		int idx = args[1];
		if( idx < 0 || idx >= SAMPLE_CHAIN_LEN )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					"Invalid entry '%d'", idx );
			return;
		}

		int n = sample_set_param_from_str( sample,idx,args[2],s );
		if(n)
			veejay_msg(VEEJAY_MSG_INFO, "Parameter %d value '%s'",
				args[2],s);
		else
			veejay_msg(VEEJAY_MSG_ERROR, "Error parsing parameter value");	
	}
}


void	vj_event_chain_entry_set_active( void *ptr, const char format[], va_list ap )
{
	int args[4];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args, s, format, ap);

	void *sample = which_sample( v, args );
	if( sample )
	{
		int idx = args[1];
		if( idx < 0 || idx >= SAMPLE_CHAIN_LEN )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					"Invalid entry '%d'", idx );
			return;
		}
		if(!IS_BOOLEAN(args[2]))
		{
			BOOLEAN_ERROR(2);
			return;
		}	
		sample_toggle_process_entry( sample, idx, args[2] );
		veejay_msg(VEEJAY_MSG_INFO, "Entry %d is %s", idx,args[2] ? "Enabled" : "Disabled" );
	}
}

void vj_event_chain_entry_set(void *ptr, const char format[], va_list ap)
{
	// NET_ID:11 0 LVDNegation 
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char s[256];
	P_A(args, s, format, ap);

	void *sample = which_sample( v, args );
	if( sample )
	{
		int idx = args[1];
		if( idx < 0 || idx >= SAMPLE_CHAIN_LEN )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					"Invalid entry '%d'", idx );
			return;
		}
		int fx_id = plug_get_fx_id_by_name( s );
		if( fx_id < 0 )
			veejay_msg(VEEJAY_MSG_ERROR, "FX '%s' not found", s );
		else
		{
			if( sample_fx_set( sample, idx,fx_id ) )
			       veejay_msg(VEEJAY_MSG_INFO,
				       "Added '%s' to entry %d of sample %d",
						s,idx,args[0]);
	 		else
				veejay_msg(VEEJAY_MSG_ERROR,
					"Unable to add '%s' to entry %d of sample %d",
						s,idx,args[0]);		
		}
	}
	

}

void vj_event_chain_entry_preset(void *ptr,const char format[], va_list ap)
{
	//@ arbitrary format, all fx_values and fx_channels
}


void vj_event_el_cut(void *ptr, const char format[], va_list ap)
{
}

void vj_event_el_copy(void *ptr, const char format[], va_list ap)
{
}

void vj_event_el_del(void *ptr, const char format[], va_list ap)
{
}

void vj_event_el_crop(void *ptr, const char format[], va_list ap) 
{
}

void vj_event_el_paste_at(void *ptr, const char format[], va_list ap)
{
}

void vj_event_el_save_editlist(void *ptr, const char format[], va_list ap)
{
}

void vj_event_el_load_editlist(void *ptr, const char format[], va_list ap)
{
}


void vj_event_el_add_video(void *ptr, const char format[], va_list ap)
{
}

void vj_event_create_effect_bundle(veejay_t * v, char *buf, int key_id, int key_mod )
{
}

#ifdef USE_GDK_PIXBUF
void	vj_event_get_scaled_image		(	void *ptr,	const char format[],	va_list	ap	)
{
	/* send performer preview buffer ! */

}
#endif

void	vj_event_send_sample			(	void *ptr,	const char format[],	va_list ap)
{
	// @ send sample info
}

void	vj_event_send_sample_list		(	void *ptr,	const char format[],	va_list ap	)
{
	veejay_t *v = (veejay_t*)ptr;

	//@ send list of samples (primary keys)
}

void	vj_event_send_log			(	void *ptr,	const char format[],	va_list ap 	)
{
	veejay_t *v = (veejay_t*) ptr;
	int num_lines = 0;
	int str_len = 0;
	char *messages = NULL;
	bzero( _s_print_buf,SEND_BUF);

	messages = veejay_pop_messages( &num_lines, &str_len );

	if(str_len == 0 || num_lines == 0 )
		sprintf(_s_print_buf, "%06d", 0);
	else
		sprintf(_s_print_buf, "%06d%s", str_len, messages );
	if(messages)
		free(messages);	

	SEND_LOG_MSG( v, _s_print_buf );
}

void	vj_event_send_frame				( 	void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
}


void	vj_event_mcast_start				(	void *ptr,	const char format[],	va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
/*	if(!v->settings->use_vims_mcast)
		veejay_msg(VEEJAY_MSG_ERROR, "start veejay in multicast mode (see -V commandline option)");	
	else
	{
		v->settings->mcast_frame_sender = 1;
		veejay_msg(VEEJAY_MSG_INFO, "Veejay started mcast frame sender");
	}*/	
}


void	vj_event_mcast_stop				(	void *ptr,	const char format[],	va_list ap )
{
	/*veejay_t *v = (veejay_t*) ptr;
	if(!v->settings->use_vims_mcast)
		veejay_msg(VEEJAY_MSG_ERROR, "start veejay in multicast mode (see -V commandline option)");	
	else
	{
		v->settings->mcast_frame_sender = 0;
		veejay_msg(VEEJAY_MSG_INFO, "Veejay stopped mcast frame sender");
	}*/	
}
/*
int vj_event_load_bundles(char *bundle_file)
{
	FILE *fd;
	char *event_name, *event_msg;
	char buf[65535];
	int event_id=0;
	if(!bundle_file) return -1;
	fd = fopen(bundle_file, "r");
	bzero(buf,65535);
	if(!fd) return -1;
	while(fgets(buf,4096,fd))
	{
		buf[strlen(buf)-1] = 0;
		event_name = strtok(buf, "|");
		event_msg = strtok(NULL, "|");
		if(event_msg!=NULL && event_name!=NULL) {
			//veejay_msg(VEEJAY_MSG_INFO, "Event: %s , Msg [%s]",event_name,event_msg);
			event_id = atoi( event_name );
			if(event_id && event_msg)
			{
				vj_msg_bundle *m = vj_event_bundle_new( event_msg, event_id );
				if(m != NULL) 
				{
					if( vj_event_bundle_store(m) ) 
					{
						veejay_msg(VEEJAY_MSG_INFO, "(VIMS) Registered a bundle as event %03d",event_id);
					}
				}
			}
		}
	}
	fclose(fd);
	return 1;
}

void vj_event_do_bundled_msg(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char s[1024];	
	vj_msg_bundle *m;
	P_A( args, s , format, ap);
	//veejay_msg(VEEJAY_MSG_INFO, "Parsing message bundle as event");
	m = vj_event_bundle_get(args[0]);
	if(m)
	{
		vj_event_parse_bundle( v, m->bundle );
	}	
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Requested event %d does not exist. ",args[0]);
	}
}*/

/*
void vj_event_bundled_msg_del(void *ptr, const char format[], va_list ap)
{
	
	int args[1];	
	char *s = NULL;
	P_A(args,s,format,ap);
	if ( vj_event_bundle_del( args[0] ) == 0)
	{
		veejay_msg(VEEJAY_MSG_INFO,"Bundle %d deleted from event system",args[0]);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Bundle is %d is not known",args[0]);
	}
}




void vj_event_bundled_msg_add(void *ptr, const char format[], va_list ap)
{
	
	int args[2] = {0,0};
	char s[1024];
	bzero(s, 1024);
	P_A(args,s,format,ap);

	if(args[0] == 0)
	{
		args[0] = vj_event_suggest_bundle_id();
		veejay_msg(VEEJAY_MSG_DEBUG, "(VIMS) suggested new Event id %d", args[0]);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "(VIMS) requested to add/replace %d", args[0]);
	}

	if(args[0] < VIMS_BUNDLE_START|| args[0] > VIMS_BUNDLE_END )
	{
		// invalid bundle
		veejay_msg(VEEJAY_MSG_ERROR, "Customized events range from %d-%d", VIMS_BUNDLE_START, VIMS_BUNDLE_END);
		return;
	}
	// allocate new
	veejay_strrep( s, '_', ' ');
	vj_msg_bundle *m = vj_event_bundle_new(s, args[0]);
	if(!m)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error adding bundle ?!");
		return;
	}

	// bye existing bundle
	if( vj_event_bundle_exists(args[0]))
	{
		veejay_msg(VEEJAY_MSG_DEBUG,"(VIMS) Bundle exists - replace ");
		vj_event_bundle_del( args[0] );
	}

	if( vj_event_bundle_store(m)) 
	{
		veejay_msg(VEEJAY_MSG_INFO, "(VIMS) Registered Bundle %d in VIMS",args[0]);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) Error in Bundle %d '%s'",args[0],s );
	}
}*/

#ifdef USE_GDK_PIXBUF
void vj_event_screenshot(void *ptr, const char format[], va_list ap)
{
}
#else
#ifdef HAVE_JPEG
void vj_event_screenshot(void *ptr, const char format[], va_list ap)
{
	int args[4];
	char filename[1024];
	bzero(filename,1024);
	P_A(args, filename, format, ap );
	veejay_t *v = (veejay_t*) ptr;

}
#endif
#endif

void		vj_event_quick_bundle( void *ptr, const char format[], va_list ap)
{
	vj_event_commit_bundle( (veejay_t*) ptr,0,0);
}

