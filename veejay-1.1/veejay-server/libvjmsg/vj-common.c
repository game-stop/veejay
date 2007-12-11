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
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>

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
/*
static	vj_msg_hist	_message_history;
static	int		_message_his_status = 0;
*/
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

void	report_bug(int verbose)
{
	veejay_msg(VEEJAY_MSG_WARNING, "Please report this error to http://groups.google.com/group/veejay-discussion?hl=en");
	veejay_msg(VEEJAY_MSG_WARNING, "You can also use our mailinglist: veejay-users@lists.sourceforge.net");
	veejay_msg(VEEJAY_MSG_WARNING, "Send at least veejay's output and include the command(s) you have used to start it.");
	if( !verbose )
		veejay_msg(VEEJAY_MSG_WARNING, "Please re-run veejay with the -v commandline option to produce additional details");

}

void veejay_msg(int type, const char format[], ...)
{
    char prefix[10];
    char buf[256];
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
	     fprintf(out,"%s %s %s\n", prefix, buf, TXT_END);
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
	bzero(tmp,dlen);
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
        veejay_msg(VEEJAY_MSG_WARNING, "You can also use our mailinglist: veejay-users@lists.sourceforge.net");
        veejay_msg(VEEJAY_MSG_WARNING, "Send at least veejay's output and include the command(s) you have used to start it.");
	veejay_msg(VEEJAY_MSG_WARNING, "If you compiled it yourself, please include information about your system.");
}

