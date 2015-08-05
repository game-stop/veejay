/* veejay - Linux VeeJay
 * 	     (C) 2002-2007 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef VJ_COMMON_H
#define VJ_COMMON_H
#include <stdarg.h>
#include <stdint.h>

enum {
    VEEJAY_MSG_INFO = 2,
    VEEJAY_MSG_WARNING = 1,
    VEEJAY_MSG_ERROR = 0,
    VEEJAY_MSG_PRINT = 3,
    VEEJAY_MSG_DEBUG = 4,
};

extern void veejay_backtrace_handler(int n , void *ist, void *x);
extern void veejay_strrep(char *s, char delim, char tok);
extern void report_bug();
extern void veejay_msg(int type, const char format[], ...);
extern int veejay_is_colored();
extern void veejay_set_debug_level(int level);
extern int veejay_get_debug_level();
extern void veejay_set_colors(int level);
extern void veejay_silent();
extern int veejay_is_silent();
extern int veejay_get_file_ext( char *file, char *dst, int dlen);
extern void veejay_chomp_str( char *str, int *dlen );
extern int has_env_setting( const char *env, const char *value );
extern char* veejay_msg_ringfetch();
extern int veejay_log_to_ringbuffer();
extern void veejay_toggle_osl();
extern void veejay_init_msg_ring();
extern void veejay_destroy_msg_ring();
#endif

