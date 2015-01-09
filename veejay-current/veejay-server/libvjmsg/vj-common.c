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

#ifdef STRICT_CHECKING
__thread struct timeval _start_time;
__thread int timerdy = 0;
__thread int _start_time_offset = 0;
#endif

#define MAX_LINES 100 
typedef struct
{
	char *msg[MAX_LINES];
	int	r_index;
	int	w_index;
} vj_msg_hist;
/*
static	vj_msg_hist	_message_history;
static	int		_message_his_status = 0;
*/

#ifdef STRICT_CHECKING 	

/* Subtract the `struct timeval' values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0.  
   gnu/docs/glibc/libc_428.html
   */

static int
timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}
#endif

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
		strncpy( file, len, buf );
		sscanf( p, "%d", line );
	}
	else {
		strncpy( file,"optimized out", sizeof(file));
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
	siginfo_t *ist = (siginfo_t*) dist;
	static char *strerr = "???";
	static struct ucontext *puc;
	int i,f=0;
	void *ip = NULL;
	void **bp = NULL;
	Dl_info info;
	
	puc = (struct ucontext*) x;
#define SICCASE(c) case c: strerr = #c
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

#ifdef STRICT_CHECKING
	struct tm *nowtm = NULL;
	
	gettimeofday( &timenow, NULL );
	if( !timerdy  ) {
		memcpy( &_start_time, &timenow, sizeof(struct timeval));
		timerdy = 1;
		timeval_subtract( &timenow,&timenow,&_start_time);
		nowtm = localtime(&timenow);
		if(nowtm->tm_hour > 0 )
			_start_time_offset = nowtm->tm_hour; // wat
	}
	else {
		timeval_subtract( &timenow, &timenow, &_start_time );
		nowtm = localtime(&timenow);
	}

	nowtm->tm_hour -= _start_time_offset;

	strftime( tmbuf,sizeof(tmbuf), "%H:%M:%S",nowtm );
	snprintf( timebuf, sizeof(timebuf), "%s.%06d", tmbuf, timenow.tv_usec );

 /*
    if(!_message_his_status)
    {
	veejay_memset( &_message_history , 0 , sizeof(vj_msg_hist));
	_message_his_status = 1;
    }
*/
    if(_color_level)
    {
	  switch (type) {
	    case 2: //info
		sprintf(prefix, "%s %s I: ", TXT_GRE,timebuf);
		break;
	    case 1: //warning
		sprintf(prefix, "%s %s W: ", TXT_YEL,timebuf);
		break;
	    case 0: // error
		sprintf(prefix, "%s %s E: ", TXT_RED,timebuf);
		break;
	    case 3:
	        line = 1;
		break;
	    case 4: // debug
		sprintf(prefix, "%s %s D: ", TXT_BLU,timebuf);
		break;
	 }

 	 if(!line)
	     fprintf(out,"%s %s %s\n",prefix, buf, TXT_END);
	     else
	     fprintf(out,"%s%s%s", TXT_GRE, buf, TXT_END );
/* 
	if( _message_history.w_index < MAX_LINES )
	{
		if(type == 3)
			sprintf(sline, "%s", buf );
		else
			sprintf( sline, "%s\n", buf );	
		_message_history.msg[_message_history.w_index ++ ] = strndup(sline,200);
	}*/
     }
     else
     {
	   switch (type) {
	    case 2: //info
		sprintf(prefix, "%s I: ",timebuf);
		break;
	    case 1: //warning
		sprintf(prefix, "%s W: ",timebuf);
		break;
	    case 0: // error
		sprintf(prefix, "%s E: ",timebuf);
		break;
	    case 3:
	        line = 1;
		break;
	    case 4: // debug
		sprintf(prefix, "%s D: ",timebuf);
		break;
	   }

	   if(!line)
	     fprintf(out,"%s %s\n", prefix, buf);
	     else
	     fprintf(out,"%s", buf );

	/*  if( _message_history.w_index < MAX_LINES )
	  {
		if(type == 3 )
			sprintf(sline, "%s", buf );
		else
			sprintf(sline, "%s\n", buf );
		_message_history.msg[_message_history.w_index ++ ] = strdup(sline);
	  }*/
     }
#else
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

 	 if(!line)
	     fprintf(out,"%s %s %s\n",prefix, buf, TXT_END);
	     else
	     fprintf(out,"%s%s%s", TXT_GRE, buf, TXT_END );
/* 
	if( _message_history.w_index < MAX_LINES )
	{
		if(type == 3)
			sprintf(sline, "%s", buf );
		else
			sprintf( sline, "%s\n", buf );	
		_message_history.msg[_message_history.w_index ++ ] = strndup(sline,200);
	}*/
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

	   if(!line)
	     fprintf(out,"%s %s\n", prefix, buf);
	     else
	     fprintf(out,"%s", buf );

	/*  if( _message_history.w_index < MAX_LINES )
	  {
		if(type == 3 )
			sprintf(sline, "%s", buf );
		else
			sprintf(sline, "%s\n", buf );
		_message_history.msg[_message_history.w_index ++ ] = strdup(sline);
	  }*/
     }

#endif
     va_end(args);
}

char *veejay_pop_messages(int *num_lines, int *total_len)
{
	char *res = NULL;
/*	if( _message_his_status == 0 )
		return res;
	if( _message_history.w_index == 0 )
		return res;
	int i;
	int len = 0;
	for( i = 0; i < _message_history.w_index ; i ++ )
		len += strlen( _message_history.msg[i] );
	if(len <= 0)
		return res;

	res = (char*) vj_malloc(sizeof(char) * (len+1) );
	if(!res)
		return NULL;
	bzero(res, len );
	*num_lines = i;

	for( i = 0; i < _message_history.w_index ; i ++ )
	{
		if( strlen(_message_history.msg[i]) > 0 )
		strcat( res, _message_history.msg[i] );
	}
	*total_len = len;
	_message_history.r_index ++;
*/
	return res;

}

int	veejay_keep_messages(void)
{
/*	if( _message_history.r_index )
		return 0;
*/
	return 1;
}

void	veejay_reap_messages(void)
{
/*	for( i = 0; i < _message_history.w_index ; i ++ )
	{
		if( _message_history.msg[i] ) 
		{
			free(_message_history.msg[i] );
			_message_history.msg[i] = NULL;
		}
	}

	_message_his_status = 0;
	_message_history.w_index = 0;
*/
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

#ifdef STRICT_CHECKING
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
#endif
		

}

