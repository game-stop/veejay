/*
    $Id: mjpeg_logging.c,v 1.16 2007/04/01 18:06:06 sms00 Exp $

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


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "mjpeg_logging.h"

#ifdef HAVE___PROGNAME
extern const char *__progname;
#endif

/*
 * Put these here and NOT in the exported header file mjpeg_logging.h
 * 
 * since ALL program use of these should use the API below (mjpeg_warn, 
 * mjpeg_error,etc) these symbols do not need to be exported and possibly
 * conflict with syslog.h
*/

#define LOG_DEBUG 1
#define LOG_INFO 2
#define LOG_WARN 3
#define LOG_ERROR 4

static log_level_t mjpeg_log_verbosity = 0;
static char *default_handler_id = NULL;

static int default_mjpeg_log_filter( log_level_t level )
{
  int verb_from_env;
  if( mjpeg_log_verbosity == 0 )
    {
      char *mjpeg_verb_env = getenv("MJPEG_VERBOSITY");
      if( mjpeg_verb_env != NULL )
        {
          verb_from_env = LOG_WARN-atoi(mjpeg_verb_env);
          if( verb_from_env >= LOG_DEBUG && verb_from_env <= LOG_ERROR )
            mjpeg_log_verbosity = (log_level_t)verb_from_env;
        }
    }
  return (level < LOG_WARN && level < mjpeg_log_verbosity);
}

static mjpeg_log_filter_t _filter = default_mjpeg_log_filter;

static void
default_mjpeg_log_handler(log_level_t level, const char message[])
{
  const char *ids;

  if( (*_filter)( level ) )
    return;
  if (default_handler_id != NULL) {
    ids = default_handler_id;
  } else {
#ifdef HAVE___PROGNAME
    ids = __progname;
#else
    ids = "???";
#endif
  }
  switch(level) {
  case LOG_ERROR:
    fprintf(stderr, "**ERROR: [%s] %s\n", ids, message);
    break;
  case LOG_DEBUG:
    fprintf(stderr, "--DEBUG: [%s] %s\n", ids, message);
    break;
  case LOG_WARN:
    fprintf(stderr, "++ WARN: [%s] %s\n", ids, message);
    break;
  case LOG_INFO:
    fprintf(stderr, "   INFO: [%s] %s\n", ids, message);
    break;
  default:
    assert(0);
  }
}

static mjpeg_log_handler_t _handler = default_mjpeg_log_handler;


mjpeg_log_handler_t
mjpeg_log_set_handler(mjpeg_log_handler_t new_handler)
{
  mjpeg_log_handler_t old_handler = _handler;

  _handler = new_handler;
  return old_handler;
}

/***************
 * Set default log handlers degree of verboseity.
 * 0 = quiet, 1 = info, 2 = debug
 *************/

int
mjpeg_default_handler_verbosity(int verbosity)
{
  int prev_verb = mjpeg_log_verbosity;
  mjpeg_log_verbosity = (log_level_t)(LOG_WARN - verbosity);
  return prev_verb;
}

/*
 * Set identifier string used by default handler
 */
int
mjpeg_default_handler_identifier(const char *new_id)
{
  const char *s;
  if (new_id == NULL) {
    if (default_handler_id != NULL)
       free(default_handler_id);
    default_handler_id = NULL;
    return 0;
  }
  /* find basename of new_id (remove any directory prefix) */
  if ((s = strrchr(new_id, '/')) == NULL)
    s = new_id;
  else
    s = s + 1;
  default_handler_id = strdup(s);
  return 0;
}

static void
mjpeg_logv(log_level_t level, const char format[], va_list args)
{
  char buf[1024] = { 0, };

  /* TODO: Original had a re-entrancy error trap to assist bug
     finding.  To make this work with multi-threaded applications a
     lock is needed hence delete.
  */

  vsnprintf(buf, sizeof(buf)-1, format, args);
  _handler(level, buf);
}

void
mjpeg_log(log_level_t level, const char format[], ...)
{
  va_list args;
  va_start (args, format);
  mjpeg_logv(level, format, args);
  va_end (args);
}

void
mjpeg_debug(const char format[], ...)
{
  va_list args;
  va_start (args, format);
  mjpeg_logv(LOG_DEBUG, format, args);
  va_end (args);
}

void
mjpeg_info(const char format[], ...)
{
  va_list args;
  va_start (args, format);
  mjpeg_logv(LOG_INFO, format, args);
  va_end (args);
}

void
mjpeg_warn(const char format[], ...)
{
  va_list args;
  va_start (args, format);
  mjpeg_logv(LOG_WARN, format, args);
  va_end (args);
}

void
mjpeg_error(const char format[], ...)
{
  va_list args;
  va_start (args, format);
  mjpeg_logv(LOG_ERROR, format, args);
  va_end (args);
}

void
mjpeg_error_exit1(const char format[], ...)
{
  va_list args;
  va_start( args, format );
  mjpeg_logv( LOG_ERROR, format, args);
  va_end(args);           
  exit(EXIT_FAILURE);
}

log_level_t
mjpeg_loglev_t(const char *level)
{
    if (strcasecmp("debug", level) == 0) return(LOG_DEBUG);
    else if (strcasecmp("info", level) == 0) return(LOG_INFO);
    else if (strcasecmp("warn", level) == 0) return(LOG_WARN);
    else if (strcasecmp("error", level) == 0) return(LOG_ERROR);
    return(0);
}
