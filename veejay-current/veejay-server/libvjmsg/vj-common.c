/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2010 Niels Elburg <nwelburg@gmail.com>
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


/**
 * Printing the stack trace, explanation by Jaco Kroon:
 * http://tlug.up.ac.za/wiki/index.php/Obtaining_a_stack_trace_in_C_upon_SIGSEGV
 * Author: Jaco Kroon <jaco@kroon.co.za>
 * Copyright (C) 2005 - 2008 Jaco Kroon
 */

#include <config.h>
#include <stdio.h>
#include <stdarg.h>
#include <execinfo.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/ucontext.h>
#include <execinfo.h>
#include <unistd.h>
#include <libgen.h>
#include <mjpeg_logging.h>
#include <sysexits.h>
#ifdef HAVE_LIBUNWIND
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif
#include <semaphore.h>
#define TXT_RED		"\033[0;31m"
#define TXT_RED_B 	"\033[1;31m"
#define TXT_GRE		"\033[0;32m"
#define TXT_GRE_B	"\033[1;32m"
#define TXT_YEL		"\033[0;33m"
#define TXT_YEL_B	"\033[1;33m"
#define TXT_BLU		"\033[0;34m"
#define TXT_BLU_B	"\033[1;34m"
#define TXT_WHI		"\033[0;37m"
#define TXT_WHI_B	"\033[1;37m"
#define TXT_END		"\033[0m"


static int _debug_level = 0;
static int _color_level = 1;
static int _no_msg = 0;

#define MAX_LINES 100 
typedef struct
{
	char *msg[MAX_LINES];
	int	r_index;
	int	w_index;
} vj_msg_hist;

typedef struct {
	sem_t *semaphore;
	char **dommel;
	uint64_t pos;
	size_t size;
} message_ring_t;

/*
static	vj_msg_hist	_message_history;
static	int		_message_his_status = 0;
*/

#ifdef HAVE_LIBUNWIND
static void addr2line_unw( unw_word_t addr, char*file, size_t len, int *line )
{
	static char buf[512];
	snprintf(buf, sizeof(buf), "addr2line -C -e /usr/local/bin/veejay -f -i %lx", addr );
	FILE *fd = popen( buf, "r" );
	if( fd == NULL ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "failed: %s", buf );
		return;
	}

	fgets( buf, sizeof(buf), fd );
	fgets( buf, sizeof(buf), fd );

	if( buf[0] != '?' ) {
		int line = -1;
		char *p = buf;

		while( *p != ':' ) {
			p++;
		}

		*p++ = 0;
		strncpy( file, buf, len );
		sscanf( p, "%d", &line );
	}
	else {
		strcpy( file, "optimized out" );
		*line = 0;
	}

	pclose( fd );
}

void 	veejay_print_backtrace()
{
	char name[512];
	unw_cursor_t cursor; 
	unw_context_t uc;
	unw_word_t ip, sp, offp;
	
	unw_getcontext( &uc );
	unw_init_local( &cursor, &uc );

	while( unw_step( &cursor ) > 0 )
	{
		char file[512];
		int line = 0;
		veejay_memset(name,0,sizeof(name));
		veejay_memset(file, 0,sizeof(file));
		unw_get_proc_name( &cursor, name, sizeof(name), &offp );
		unw_get_reg( &cursor, UNW_REG_IP, &ip );
		unw_get_reg( &cursor, UNW_REG_SP, &sp );

		addr2line_unw( (long) ip, file, sizeof(file), &line );
		if( line >= 0 )
			veejay_msg(VEEJAY_MSG_ERROR, "\t at %s (%s:%d)", name, file, line );
		else
			veejay_msg(VEEJAY_MSG_ERROR, "\t at %s", name );
	}
}
#else
void	veejay_print_backtrace()
{
	void *space[100];
	size_t i,s;
	char **strings;

	s = backtrace( space, 100 );
	strings = backtrace_symbols(space,s);

	for( i = 0; i < s ; i ++ ) 
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strings[i] );

}
#endif
void	veejay_backtrace_handler(int n , void *dist, void *x)
{
	switch(n) {
		case SIGSEGV:
			veejay_msg(VEEJAY_MSG_ERROR,"Found Gremlins in your system."); //@ Suggested by Matthijs
			veejay_msg(VEEJAY_MSG_WARNING, "No fresh ale found in the fridge."); //@
			veejay_msg(VEEJAY_MSG_INFO, "Running with sub-atomic precision..."); //@

			veejay_print_backtrace();
			break;
		default:
			veejay_print_backtrace();
			break;
	}

	//@ Bye
	veejay_msg(VEEJAY_MSG_ERROR, "Bugs compromised the system.");

	report_bug();

	exit(EX_SOFTWARE);
}

int	veejay_get_debug_level()
{
	return _debug_level;
}

void veejay_set_debug_level(int level)
{
	if(level)
	{
		_debug_level = 1;
	}
	else
	{
		_debug_level = 0;
	}

    	mjpeg_default_handler_verbosity( _debug_level ? 1:0);
}

void veejay_set_colors(int l)
{
	if(l) _color_level = 1;
	else _color_level = 0;
}

int	veejay_is_colored()
{
	return _color_level;
}

void veejay_silent()
{
	_no_msg = 1;
}

int veejay_is_silent()
{
	if(_no_msg) return 1;          
	return 0;
}

#define MESSAGE_RING_SIZE 5000
static message_ring_t *msg_ring = NULL;
static int msg_ring_enabled = 0;
void	veejay_init_msg_ring()
{
	msg_ring = vj_calloc( sizeof(message_ring_t));
	msg_ring->dommel = vj_calloc( sizeof(char*) * MESSAGE_RING_SIZE );
	msg_ring->semaphore = vj_malloc(sizeof(sem_t));
	msg_ring->size = MESSAGE_RING_SIZE;
	sem_init( msg_ring->semaphore, 0, 0 );
}

void	veejay_destroy_msg_ring()
{
	if(msg_ring) {
		int i;
		for( i = 0; i < MESSAGE_RING_SIZE; i ++ ) {
			if( msg_ring->dommel[i]) {
				free(msg_ring->dommel[i]);
			}
		}
		free(msg_ring->dommel);
		free(msg_ring->semaphore);
		sem_destroy( msg_ring->semaphore );
		free(msg_ring);
	}
}

static void veejay_msg_ringbuffer( char *line )
{
	uint64_t pos = __sync_fetch_and_add( &msg_ring->pos, 1) % MESSAGE_RING_SIZE;
	//using gcc atomic built-ins
	while(!__sync_bool_compare_and_swap( &(msg_ring->dommel[pos]), NULL, line ));
	sem_post( msg_ring->semaphore );
}

int	veejay_log_to_ringbuffer()
{
	return ( msg_ring == NULL ? 0 : msg_ring_enabled );
}

void	veejay_toggle_osl()
{
	msg_ring_enabled = (msg_ring_enabled == 0 ? 1: 0);
}

static uint64_t pos = 0;
char	*veejay_msg_ringfetch()
{
	char *line = NULL;
	if( sem_trywait( msg_ring->semaphore ) != 0 )
		return NULL;

	if( msg_ring->dommel[pos] == NULL )
		return NULL;

	line = vj_strdup( msg_ring->dommel[pos]);
	free(msg_ring->dommel[ pos ]);
	msg_ring->dommel[ pos ] = NULL;
	pos = (pos + 1) % MESSAGE_RING_SIZE;
	return line;
}

static inline void veejay_msg_prnt(const char *line, const char *format, FILE *out, 
			          const char *prefix, const char *end )
{
	if( msg_ring_enabled == 0 ) {
		if( end ) {
			fprintf( out, format, prefix, line, end );
		} else{
			fprintf(out,format, prefix, line);
		}
	}
	else {
		veejay_msg_ringbuffer( vj_strdup(line) );
	}
}

void veejay_msg(int type, const char format[], ...)
{
    char prefix[128];
    char buf[1024];
    va_list args;
    int line = 0;
    FILE *out = (_no_msg ? stderr: stdout );
    if( type != VEEJAY_MSG_ERROR && _no_msg )
		return;

    if( !_debug_level && type == VEEJAY_MSG_DEBUG )
	return ; // bye

    // parse arguments
    va_start(args, format);
    vsnprintf(buf, sizeof(buf) - 1, format, args);

    if(_color_level)
    {
	  switch (type) {
	    case 2: //info
		sprintf(prefix, "%sI: ", TXT_GRE);
		break;
	    case 1: //warning
		sprintf(prefix, "%sW: ", TXT_YEL);
		break;
	    case 0: // error
		sprintf(prefix, "%sE: ", TXT_RED);
		break;
	    case 3:
	        line = 1;
		break;
	    case 4: // debug
		sprintf(prefix, "%sD: ", TXT_BLU);
		break;
	 }

 	 if(!line) {
		veejay_msg_prnt( buf, "%s%s%s\n", out, prefix, TXT_END );
	 }
	 else {
		veejay_msg_prnt( buf, "%s%s%s", out, TXT_GRE, TXT_END );
	}
     }
     else
     {
	   switch (type) {
	    case 2: //info
		sprintf(prefix, "I: ");
		break;
	    case 1: //warning
		sprintf(prefix, "W: ");
		break;
	    case 0: // error
		sprintf(prefix, "E: ");
		break;
	    case 3:
	        line = 1;
		break;
	    case 4: // debug
		sprintf(prefix, "D: ");
		break;
	   }

	   if(!line) {
		veejay_msg_prnt( buf, "%s%s\n", out, prefix, NULL );
	   } else {
		veejay_msg_prnt( buf, "%s%s", out, prefix, NULL );
	   }
     }
     va_end(args);
}

int	veejay_get_file_ext( char *file, char *dst, int dlen)
{
	int len = strlen(file)-1;
	int i = 0;
	char tmp[dlen];
	memset( tmp, 0, dlen );
	while(len)
	{
		if(file[len] == '.')
		{
			if(i==0) return 0;
			int j;
			int k = 0;
			for(j = i-1; j >= 0;j--)
			{
			  dst[k] = tmp[j];
			 k ++;
			}
			return 1;
		}
		tmp[i] = file[len];
		i++;
		if( i >= dlen)
			return 0;
		len --;
	}
	return 0;
}

void veejay_strrep(char *s, char delim, char tok)
{
  unsigned int i;
  unsigned int len = strlen(s);
  if(!s) return;
  for(i=0; i  < len; i++)
  {
    if( s[i] == delim ) s[i] = tok;
  }
}

void	veejay_chomp_str( char *msg, int *nlen )
{
	int len = strlen( msg ) - 1;
	if(len > 0 )
	{
		if( msg[len] == '\n' )
		{
			msg[len] = '\0';
			*nlen = len;
		}
	}
}

void    report_bug(void)
{
    veejay_msg(VEEJAY_MSG_WARNING, "Please report this error to http://groups.google.com/group/veejay-discussion?hl=en");
    veejay_msg(VEEJAY_MSG_WARNING, "Send at least veejay's output and include the command(s) you have used to start it.");
	veejay_msg(VEEJAY_MSG_WARNING, "Also, please consider sending in the recovery files if any have been created.");
	veejay_msg(VEEJAY_MSG_WARNING, "If you compiled it yourself, please include information about your system.");
/*
	veejay_msg(VEEJAY_MSG_WARNING, "Dumping core file to: core.%d",getpid() );
	
	char cmd[128];
	memset(cmd,0,sizeof(cmd));
	sprintf(cmd, "generate-core-file");
	int fd = open( "veejay.cmd", O_RDWR|O_CREAT );
	if(!fd) {
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to write gdb batch commands, no core dump written. ");
	} else {
		int res = write( fd , cmd, strlen(cmd));
		close(fd);
		sprintf(cmd, "gdb -p %d -batch -x veejay.cmd", getpid());
		veejay_msg(VEEJAY_MSG_WARNING,"Please wait! Running command '%s'", cmd);
		system(cmd);
		veejay_msg(VEEJAY_MSG_WARNING, "Done!");
		veejay_msg(VEEJAY_MSG_INFO, "Please bzip2 and upload the coredump somewhere and tell us where to find it!");
	}
	*/	

}

int	has_env_setting( const char *env, const char *value )
{
	char *tmp = getenv( env );
	if( tmp ) {
		if( strncasecmp( value, tmp, strlen(value)) == 0 )
			return 1;
	}
	return 0;
}
