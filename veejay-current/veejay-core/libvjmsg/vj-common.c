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
static int _timestamp   = 1;
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
	uint64_t read_pos;
	uint64_t write_pos;
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
	int i,s;
	char **strings;

	s = backtrace( space, 100 );
	strings = backtrace_symbols(space,s);
	if(strings == NULL ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to get backtrace");
		return;
	}

	for( i = 0; i < s ; i ++ ) {
		veejay_msg(VEEJAY_MSG_ERROR, "%s", strings[i] );
	}
	
	free(strings);
}
#endif


static inline void veejay_msg_timestamp(char *out)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    time_t sec = ts.tv_sec;
    struct tm tm;
    localtime_r(&sec, &tm);

    int ms = ts.tv_nsec / 1000000;

    // Format: HH:MM:SS.mmm
    out[0]  = '0' + (tm.tm_hour / 10);
    out[1]  = '0' + (tm.tm_hour % 10);
    out[2]  = ':';
    out[3]  = '0' + (tm.tm_min / 10);
    out[4]  = '0' + (tm.tm_min % 10);
    out[5]  = ':';
    out[6]  = '0' + (tm.tm_sec / 10);
    out[7]  = '0' + (tm.tm_sec % 10);
    out[8]  = '.';
    out[9]  = '0' + (ms / 100);
    out[10] = '0' + ((ms / 10) % 10);
    out[11] = '0' + (ms % 10);
    out[12] = '\0';
}

int	veejay_get_debug_level()
{
	return _debug_level;
}

int	veejay_is_timestamped()
{
	return _timestamp;
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

void veejay_set_timestamp(int status) {
	_timestamp = status;
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
	msg_ring->write_pos = 0;
	msg_ring->read_pos = 0;
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
		if(msg_ring->semaphore) {
			sem_destroy( msg_ring->semaphore );
			free(msg_ring->semaphore);
		}
		free(msg_ring);
		msg_ring = NULL;
	}
}

static void veejay_msg_ringbuffer(char *line)
{
    if (!msg_ring || !line) return;

    uint64_t current_pos = __sync_fetch_and_add(&msg_ring->write_pos, 1) % MESSAGE_RING_SIZE;

    if (__sync_bool_compare_and_swap(&(msg_ring->dommel[current_pos]), NULL, line)) {
        sem_post(msg_ring->semaphore);
    } else {
		// buffer full
        free(line); 
    }
}

int	veejay_log_to_ringbuffer()
{
	return ( msg_ring == NULL ? 0 : msg_ring_enabled );
}

void	veejay_toggle_osl()
{
	msg_ring_enabled = (msg_ring_enabled == 0 ? 1: 0);
}

char *veejay_msg_ringfetch()
{
    if (!msg_ring) return NULL;

    char *line = NULL;
    
    if (sem_trywait(msg_ring->semaphore) != 0)
        return NULL;


    uint64_t r_pos = msg_ring->read_pos % MESSAGE_RING_SIZE;

    line = __sync_lock_test_and_set(&(msg_ring->dommel[r_pos]), NULL);

    if (line) {
        msg_ring->read_pos++;
    }

    return line; 
}

void veejay_msg_prnt(FILE *out, const char *buffer, size_t size)
{
    if(out) write(fileno(out), buffer, size);
}
// Updated veejay_msg function with log level before timestamp
void veejay_msg(int type, const char format[], ...)
{
    if (type != VEEJAY_MSG_ERROR && _no_msg)
        return;

    if (!_debug_level && type == VEEJAY_MSG_DEBUG)
        return;

    char buf[1024];
    va_list args;
    int line = 0;
    FILE *out = (_no_msg ? stderr : stdout);

    va_start(args, format);
    vsnprintf(buf, sizeof(buf) - 1, format, args);
    va_end(args);

    char temp_buffer[2048];
    size_t temp_size = 0;

    // --- log level prefix first ---
    if (_color_level)
    {
        switch (type)
        {
        case 2: // info
            temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "%sI: %s", TXT_GRE, TXT_END);
            break;
        case 1: // warning
            temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "%sW: %s", TXT_YEL, TXT_END);
            break;
        case 0: // error
            temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "%sE: %s", TXT_RED, TXT_END);
            break;
        case 3:
            line = 1;
            break;
        case 4: // debug
            temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "%sD: %s", TXT_BLU, TXT_END);
            break;
        }
    }
    else
    {
        switch (type)
        {
        case 2: temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "I: "); break;
        case 1: temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "W: "); break;
        case 0: temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "E: "); break;
        case 3: line = 1; break;
        case 4: temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "D: "); break;
        }
    }

	if(_timestamp) {
		char time_buf[13];
		veejay_msg_timestamp(time_buf);

		temp_size += snprintf(
			temp_buffer + temp_size,
			sizeof(temp_buffer) - temp_size,
			"[%s] ",
			time_buf
		);
	}

    // --- finally the message ---
    if (_color_level && !line)
    {
        temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "%s%s\n", buf, TXT_END);
    }
    else if (_color_level && line)
    {
        temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "%s%s%s", buf, TXT_GRE, TXT_END);
    }
    else if (!line)
    {
        temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "%s\n", buf);
    }
    else
    {
        temp_size += snprintf(temp_buffer + temp_size, sizeof(temp_buffer) - temp_size, "%s", buf);
    }

    veejay_msg_prnt(out, temp_buffer, temp_size);
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

int	has_env_setting( const char *env, const char *value )
{
	char *tmp = getenv( env );
	if( tmp ) {
		if( strncasecmp( value, tmp, strlen(value)) == 0 )
			return 1;
	}
	return 0;
}
