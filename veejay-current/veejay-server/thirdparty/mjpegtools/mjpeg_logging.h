/*
    $Id: mjpeg_logging.h,v 1.11 2007/04/01 18:06:06 sms00 Exp $

    Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __MJPEG_LOGGING_H__
#define __MJPEG_LOGGING_H__

#include <mjpeg_types.h>

/*  to avoid changing all the places log_level_t is used */
typedef int log_level_t; 

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
#define GNUC_PRINTF( format_idx, arg_idx )    \
  __attribute__((format (printf, format_idx, arg_idx)))
#else   /* !__GNUC__ */
#define GNUC_PRINTF( format_idx, arg_idx )
#endif  /* !__GNUC__ */

#ifdef __cplusplus
extern "C" {
#endif
void
mjpeg_log(log_level_t level, const char format[], ...) GNUC_PRINTF(2, 3);

typedef int(*mjpeg_log_filter_t)(log_level_t level);
    
typedef void(*mjpeg_log_handler_t)(log_level_t level, const char message[]);

mjpeg_log_handler_t
mjpeg_log_set_handler(mjpeg_log_handler_t new_handler);

int
mjpeg_default_handler_identifier(const char *new_id);

int
mjpeg_default_handler_verbosity(int verbosity);

void
mjpeg_debug(const char format[], ...) GNUC_PRINTF(1,2);

void
mjpeg_info(const char format[], ...) GNUC_PRINTF(1,2);

void
mjpeg_warn(const char format[], ...) GNUC_PRINTF(1,2);

void
mjpeg_error(const char format[], ...) GNUC_PRINTF(1,2);

void
mjpeg_error_exit1(const char format[], ...) GNUC_PRINTF(1,2);

log_level_t
mjpeg_loglev_t(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* __MJPEG_LOGGING_H__ */
