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
static int _label = 1;
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

#ifdef HAVE_LIBUNWIND
static void addr2line_unw(unw_word_t addr, char *file, size_t len, int *line_out)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "addr2line -C -e /proc/self/exe -f -i %p", (void *)addr);

    FILE *fd = popen(buf, "r");
    if (!fd) return;

    char temp[512];
    if (fgets(temp, sizeof(temp), fd) != NULL) {
        if (fgets(temp, sizeof(temp), fd) != NULL) {
            if (temp[0] != '?') {
                char *p = strchr(temp, ':');
                if (p) {
                    *p = '\0';
                    strncpy(file, temp, len - 1);
                    file[len - 1] = '\0';
                    *line_out = atoi(p + 1);
                    pclose(fd);
                    return;
                }
            }
        }
    }

    strncpy(file, "optimized out", len - 1);
    file[len - 1] = '\0';
    *line_out = 0;
    pclose(fd);
}

void 	veejay_print_backtrace(void)
{
    unw_cursor_t cursor;
    unw_context_t uc;
    unw_word_t ip, offp;
    char name[512];
    char file[512];

    unw_getcontext(&uc);
    unw_init_local(&cursor, &uc);

    veejay_msg(VEEJAY_MSG_ERROR, "Stack Trace:");

    while (unw_step(&cursor) > 0)
    {
        int line = -1;

        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        
        if (unw_get_proc_name(&cursor, name, sizeof(name), &offp) != 0) {
            strncpy(name, "<unknown>", sizeof(name));
        }

        file[0] = '\0';

        addr2line_unw((unw_word_t)(ip - 1), file, sizeof(file), &line);

        if (line > 0) {
            veejay_msg(VEEJAY_MSG_ERROR, "\t at %s (%s:%d)", name, file, line);
        } else {
            veejay_msg(VEEJAY_MSG_ERROR, "\t at %s (offset 0x%lx)", name, (long)offp);
        }
    }
}
#else
void veejay_print_backtrace(void)
{
    void *stack_buffer[64];
    int num_frames;
    char **symbols;

    num_frames = backtrace(stack_buffer, 64);
    
    symbols = backtrace_symbols(stack_buffer, num_frames);

    veejay_msg(VEEJAY_MSG_ERROR, "Crash Backtrace (%d frames)", num_frames);

    if (symbols == NULL) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to resolve symbols (backtrace_symbols returned NULL)");
        for (int i = 0; i < num_frames; i++) {
            veejay_msg(VEEJAY_MSG_ERROR, "#%d %p", i, stack_buffer[i]);
        }
    } else {
        for (int i = 0; i < num_frames; i++) {
            veejay_msg(VEEJAY_MSG_ERROR, "#%-2d %s", i, symbols[i]);
        }
        free(symbols);
    }

    veejay_msg(VEEJAY_MSG_ERROR, "Tip: Use 'addr2line -pC -e <binary> <address>' for line numbers.");
}
#endif

static const char digits[100][2] = {
        {'0','0'},{'0','1'},{'0','2'},{'0','3'},{'0','4'},{'0','5'},{'0','6'},{'0','7'},{'0','8'},{'0','9'},
        {'1','0'},{'1','1'},{'1','2'},{'1','3'},{'1','4'},{'1','5'},{'1','6'},{'1','7'},{'1','8'},{'1','9'},
        {'2','0'},{'2','1'},{'2','2'},{'2','3'},{'2','4'},{'2','5'},{'2','6'},{'2','7'},{'2','8'},{'2','9'},
        {'3','0'},{'3','1'},{'3','2'},{'3','3'},{'3','4'},{'3','5'},{'3','6'},{'3','7'},{'3','8'},{'3','9'},
        {'4','0'},{'4','1'},{'4','2'},{'4','3'},{'4','4'},{'4','5'},{'4','6'},{'4','7'},{'4','8'},{'4','9'},
        {'5','0'},{'5','1'},{'5','2'},{'5','3'},{'5','4'},{'5','5'},{'5','6'},{'5','7'},{'5','8'},{'5','9'},
        {'6','0'},{'6','1'},{'6','2'},{'6','3'},{'6','4'},{'6','5'},{'6','6'},{'6','7'},{'6','8'},{'6','9'},
        {'7','0'},{'7','1'},{'7','2'},{'7','3'},{'7','4'},{'7','5'},{'7','6'},{'7','7'},{'7','8'},{'7','9'},
        {'8','0'},{'8','1'},{'8','2'},{'8','3'},{'8','4'},{'8','5'},{'8','6'},{'8','7'},{'8','8'},{'8','9'},
        {'9','0'},{'9','1'},{'9','2'},{'9','3'},{'9','4'},{'9','5'},{'9','6'},{'9','7'},{'9','8'},{'9','9'}
    };

static inline void veejay_msg_timestamp(char *out)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    out[0] = digits[tm.tm_hour][0];
    out[1] = digits[tm.tm_hour][1];
    out[2] = ':';
    out[3] = digits[tm.tm_min][0];
    out[4] = digits[tm.tm_min][1];
    out[5] = ':';
    out[6] = digits[tm.tm_sec][0];
    out[7] = digits[tm.tm_sec][1];
    out[8] = '.';

    long ns = ts.tv_nsec;
    out[ 9] = '0' + (ns / 100000000); ns %= 100000000;
    out[10] = '0' + (ns / 10000000);  ns %= 10000000;
    out[11] = '0' + (ns / 1000000);   ns %= 1000000;
    out[12] = '0' + (ns / 100000);    ns %= 100000;
    out[13] = '0' + (ns / 10000);     ns %= 10000;
    out[14] = '0' + (ns / 1000);      ns %= 1000;
    out[15] = '0' + (ns / 100);       ns %= 100;
    out[16] = '0' + (ns / 10);        ns %= 10;
    out[17] = '0' + ns;
    out[18] = '\0';
}

int	veejay_get_debug_level(void)
{
	return _debug_level;
}

int	veejay_is_timestamped(void)
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

void veejay_set_label(int status) {
	_label = status;
}

void veejay_set_timestamp(int status) {
	_timestamp = status;
}

void veejay_set_colors(int l)
{
	if(l) _color_level = 1;
	else _color_level = 0;
}

int	veejay_is_colored(void)
{
	return _color_level;
}

void veejay_silent(void)
{
	_no_msg = 1;
}

int veejay_is_silent(void)
{
	if(_no_msg) return 1;          
	return 0;
}

#define MESSAGE_RING_SIZE 5000
static message_ring_t *msg_ring = NULL;
static int msg_ring_enabled = 0;
void	veejay_init_msg_ring(void)
{
	msg_ring = vj_calloc( sizeof(message_ring_t));
	msg_ring->dommel = vj_calloc( sizeof(char*) * MESSAGE_RING_SIZE );
	msg_ring->semaphore = vj_malloc(sizeof(sem_t));
	msg_ring->size = MESSAGE_RING_SIZE;
	msg_ring->write_pos = 0;
	msg_ring->read_pos = 0;
	sem_init( msg_ring->semaphore, 0, 0 );
}

void	veejay_destroy_msg_ring(void)
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

void veejay_msg_buffer(const uint8_t *buffer, size_t len, size_t max_bytes_to_log, const char *prefix)
{
    if (!buffer || len == 0)
        return;

    size_t dump_len = (len < max_bytes_to_log) ? len : max_bytes_to_log;
    size_t line_width = 16;

    for (size_t offset = 0; offset < dump_len; offset += line_width) {
        size_t line_len = ((offset + line_width) <= dump_len) ? line_width : (dump_len - offset);

        char hexbuf[line_width * 3 + 1];
        char asciibuf[line_width + 1];
        char *hp = hexbuf;
        char *ap = asciibuf;

        for (size_t i = 0; i < line_len; i++) {
            uint8_t b = buffer[offset + i];
            hp += snprintf(hp, 4, "%02X ", b);
            *ap++ = (b >= 32 && b <= 126) ? b : '.';
        }
        *hp = '\0';
        *ap = '\0';

        if (prefix)
            veejay_msg(VEEJAY_MSG_WARNING, "%s [%04zu]: %-48s |%s|", prefix, offset, hexbuf, asciibuf);
        else
            veejay_msg(VEEJAY_MSG_WARNING, "[%04zu]: %-48s |%s|", offset, hexbuf, asciibuf);
    }
}

int	veejay_log_to_ringbuffer(void)
{
	return ( msg_ring == NULL ? 0 : msg_ring_enabled );
}

void	veejay_toggle_osl(void)
{
	msg_ring_enabled = (msg_ring_enabled == 0 ? 1: 0);
}

char *veejay_msg_ringfetch(void)
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


void veejay_print_banner(void) {
    const char* banner =
    "                                  ██                        \n"
    "                                  ██                        \n"
    "                                  ██                        \n"
    "                                                            \n"
    " ██▒  ▒██   ░████▒    ░████▒    ████      ▒████▓   ██▓  ▓██ \n"
    " ▓██  ██▓  ░██████▒  ░██████▒   ████      ██████▓  ▒██  ██▓ \n"
    " ▒██  ██▒  ██▒  ▒██  ██▒  ▒██     ██      █▒  ▒██   ██▒ ██░ \n"
    "  ██░░██   ████████  ████████     ██       ▒█████   ███▒██  \n"
    "  ██▒▒██   ████████  ████████     ██     ░███████   ░██▓█▓  \n"
    "  ▒████▒   ██        ██           ██     ██▓░  ██    ████░  \n"
    "   ████    ███░  ▒█  ███░  ▒█     ██     ██▒  ███    ▒███   \n"
    "   ████    ░███████  ░███████     ██     ████████     ██▓   \n"
    "   ▒██▒     ░█████▒   ░█████▒     ██      ▓███░██     ██░   \n"
    "                                 ▒██                 ▒██    \n"
    "                               █████                ███▒    \n"
    "                               ████░                ███     \n\n";

    if (_color_level) {
        printf("%s%s%s%s", TXT_GRE, banner,PACKAGE_VERSION, TXT_END);
    } else {
        printf("%s%s", banner, PACKAGE_VERSION) ;
    }
}


void veejay_msg(int type, const char format[], ...)
{
    if ((type != VEEJAY_MSG_ERROR && _no_msg) || (!_debug_level && type == VEEJAY_MSG_DEBUG))
        return;
    static const char *labels[] = { "E: ", "W: ", "I: ", "", "D: " };
    static const char *colors[] = { TXT_RED, TXT_YEL, TXT_GRE, "", TXT_BLU };

    int idx = (type >= 0 && type <= 4) ? type : 2;
    int is_continuation = (type == 3);

    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    char out_buf[2048];
    char *p = out_buf;
    char *end = out_buf + sizeof(out_buf);

    if (_color_level && !is_continuation)
        p += snprintf(p, end - p, "%s", colors[idx]);

    if (_label && !is_continuation)
        p += snprintf(p, end - p, "%s", labels[idx]);

    if (_timestamp && !is_continuation) {
        char time_buf[19];
        veejay_msg_timestamp(time_buf);
        p += snprintf(p, end - p, "[%s] ", time_buf);
    }

    p += snprintf(p, end - p, "%s", buf);

    if (_color_level)
        p += snprintf(p, end - p, "%s", TXT_END);

    if (!is_continuation)
        p += snprintf(p, end - p, "\n");

    veejay_msg_prnt(_no_msg ? stderr : stdout, out_buf, (size_t)(p - out_buf));
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
