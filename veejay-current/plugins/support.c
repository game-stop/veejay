/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
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

/*
 * This file contains helper/support functions for Veejay's plugins
 *
 */
#include <stdio.h>
#include <string.h>
#include "veejay-plugin.h"

int	OptionParse(char *format, void *dst, const char *needle, const char *args)
{
	char *line_start;
	char *line_end;
	char tmp[1024];
	
	char option[20];
	unsigned int opt_len =0;
	
	unsigned int n_len =0;

	sprintf(option, ":%s", needle);

	line_start = strstr(args, option );

	if(!line_start) return 0;
	if(!needle) return 0;

	n_len = strlen(needle);
	
	 *(line_start)++;
	
	line_end = line_start;


	/* forward until '=' */
	while( *(line_start) != '=' )
	{
		*(line_start)++;
		opt_len ++;
	}
	bzero(option,20);

	strncpy( option, line_end, opt_len );

	if( strcmp(needle, option) == 0 )
	{
		int expr_len = 0;
		char *token;
		*(line_start)++;	
		token = line_start;
		while ( *(line_start) != ':' &&  *(line_start) != ';' )
		{
			*(line_start)++;
			expr_len ++;
		}
		bzero(tmp, 1024);
		strncpy( tmp, token, expr_len );
//		veejay_msg(VEEJAY_MSG_DEBUG, "Option: [%s] Token [%s]",option, tmp);

		if( strcmp( format, "%s" ) == 0 )
		{
			// a str seperated with whitespaces
			strncpy( dst, tmp, strlen(tmp));
			return 1;
		}
		else
		{
			int n = sscanf( tmp, format, dst );		
//			veejay_msg(VEEJAY_MSG_DEBUG, "Scanf : %d ", n);
			if(n>0) return 1;
		}
	}	

	return 0;

}

