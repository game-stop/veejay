/* gveejay - Linux VeeJay - GVeejay GTK+-2/Glade User Interface
 *           (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include "vj-api.h"

static int port_num	= 3490;
static char hostname[255];
static int gveejay_theme = 1;
static	int verbosity = 0;
static int timer = 6;
static int preview_width = 0;
static int preview_height = 0;

static void usage(char *progname)
{
        printf( "Usage: %s <options>\n",progname);
        printf( "where options are:\n");
        printf( "-h/--hostname\t\tVeejay host to connect to (defaults to localhost) \n");         
        printf( "-p/--port\t\tVeejay port to connect to (defaults to 3490) \n");
	printf( "-n/--no-theme\t\tDont load gveejay's GTK theme\n");
	printf( "-v/--verbose\t\tBe extra verbose (usefull for debugging)\n");
	printf( "-t/--timeout\t\tSet timeout (default 6 seconds)\n");
	printf( "-S/--size\t\tSet preview size (widht X height)\n");
        printf( "\n\n");
        exit(-1);
}
static int      set_option( const char *name, char *value )
{
        int err = 0;
        if( strcmp(name, "h") == 0 || strcmp(name, "hostname") == 0 )
        {
                strcpy( hostname, optarg );
        }
        else if( strcmp(name, "p") == 0 || strcmp(name ,"port") == 0 )
        {
                port_num = atoi(optarg);
        }
	else if( strcmp(name, "n") == 0 || strcmp(name, "no-theme") == 0)
	{
		gveejay_theme = 0;
	}
	else if( strcmp(name, "v") == 0 || strcmp(name, "verbose") == 0)
	{
		verbosity = 1;
	}
	else if( strcmp(name, "t") == 0 || strcmp(name, "timeout") == 0)
	{
		timer = atoi(optarg);
	}
	else if (strcmp(name, "s") == 0 || strcmp(name, "size") == 0)
	{
		if(sscanf( (char*) optarg, "%dx%d",
			&preview_width, &preview_height ) != 2 )
		{
			fprintf(stderr, "--size parameter requires NxN argument");
			err++;
		}
	}
        else
                err++;
        return err;
}

int main(int argc, char *argv[]) {
        char option[2];
        int n;
        int err=0;

        if(!argc) usage(argv[0]);

        while( ( n = getopt( argc, argv, "s:h:p:nv")) != EOF )
        {
                sprintf(option, "%c", n );
                err += set_option( option, optarg);
                if(err) usage(argv[0]);
        }
        if( optind > argc )
                err ++;

        if( err ) usage(argv[0]);

	if(gveejay_theme)
		vj_gui_theme_setup();

	gtk_init(&argc, &argv);

	vj_gui_set_debug_level( verbosity );
		
	vj_gui_set_timeout(timer);

	vj_gui_init("gveejay.reloaded.glade");
	vj_gui_set_preview_window( preview_width,preview_height);
	
	if(gveejay_theme)
		vj_gui_style_setup();

        gtk_main();

        return 0;  
}


