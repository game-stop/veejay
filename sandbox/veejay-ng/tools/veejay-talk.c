/*
 *  Copyright (C) 2004 Steve Harris, Uwe Koloska
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  $Id: example_client.c,v 1.1.1.1 2004/08/07 22:21:02 theno23 Exp $
 */
#include <config.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "lo/lo.h"

static	void	Usage(const char *progname)
{
    fprintf(stderr, "Usage: %s [address] [port] [format] [path] {arguments}\n",progname );
    exit(0);
}

int main(int argc, char *argv[])
{
    const char *addr = argv[1];
    if(!addr || argc < 5)
	Usage(argv[0]);
    const char *port = argv[2];
    const char *format = argv[3];
    
    lo_address t = lo_address_new(addr, port);
    if(!t)
    {
      fprintf(stderr, "Failed to communicate with %s:%s\n",addr,port);
      Usage(argv[0]);
    }
    const char *path = argv[4];
    lo_message	msg = lo_message_new();
    int i;
    int n = 5;
    
    while (*format )
    {
	if( *format == 'i' ) {
	    lo_message_add_int32( msg, atoi( argv[n] )); n ++ ; 
	} else if( *format == 'd' ) {
	    lo_message_add_double( msg, atof( argv[n] )); n ++ ; 
	} else if ( *format == 's' ) {
	    lo_message_add_string( msg, (char*) argv[n] ); n ++ ;
	} else if ( *format == 'h' ) {
	    lo_message_add_string( msg, (int64_t) atoll( argv[n]) ); n++ ;
	} else {
	   // fprintf(stderr, "Unknown format identifier %c", *format );
	   // return 0;
	}
	*format++;
    }
   
    lo_send_message( t,path, msg ); 
    
    return 0;
}

