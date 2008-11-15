/* gveejay - Linux VeeJay - GVeejay GTK+-2/Glade User Interface
 *           (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
#include <glib.h>
#include <glade/glade.h>
#include <veejay/vevo.h>
#include <veejay/vjmem.h>
#include <veejay/vj-msg.h>
#include <src/vj-api.h>
#include <sched.h>


static int port_num	= 3490;
static char hostname[255];
static int gveejay_theme = 1;
static	int verbosity = 0;
static int timer = 6;
static int col = 0;
static int row = 0;
static int n_tracks = 1;
static int launcher = 0;
static int pw = 176;
static int ph = 144;
static int preview = 0; // off
static int use_threads = 0;
static struct
{
	char *file;
} skins[] = {
 {	"gveejay.reloaded.glade" },
 {	NULL 	}
};

extern void	reloaded_launcher( char *h, int p );

static void usage(char *progname)
{
        printf( "Usage: %s <options>\n",progname);
        printf( "where options are:\n");
        printf( "-h\t\tVeejay host to connect to (defaults to localhost) \n");         
        printf( "-p\t\tVeejay port to connect to (defaults to 3490) \n");
	printf( "-t\t\tDont load gveejay's GTK theme\n");
	printf( "-n\t\tDont use colored text\n");
	printf( "-v\t\tBe extra verbose (usefull for debugging)\n");
	printf( "-s\t\tSet bank resolution (row X columns)\n");
	printf( "-P\t\tStart with preview enabled (1=1/1,2=1/2,3=1/4,4=1/8)\n");
        printf( "-X\t\tSet number of tracks\n");
	printf( "\n\n");
        exit(-1);
}
static int      set_option( const char *name, char *value )
{
        int err = 0;
        if( strcmp(name, "h") == 0 || strcmp(name, "hostname") == 0 )
        {
                strcpy( hostname, optarg );
		launcher ++;
        }
        else if( strcmp(name, "p") == 0 || strcmp(name ,"port") == 0 )
        {
                if(sscanf( optarg, "%d", &port_num ))
			launcher++;
        }
	else if (strcmp(name, "n") == 0 )
	{
		veejay_set_colors(0);
	}
	else if (strcmp(name, "X") == 0 )
	{
		n_tracks = atoi(optarg); 
	}
	else if( strcmp(name, "t") == 0 || strcmp(name, "no-theme") == 0)
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
			&row, &col ) != 2 )
		{
			fprintf(stderr, "--size parameter requires NxN argument");
			err++;
		}
	}
	else if (strcmp(name, "q") == 0 )
	{
		fprintf(stdout, "%s", get_gveejay_dir());
		exit(0);
	}
	else if (strcmp(name, "P" ) == 0 || strcmp(name, "preview" ) == 0 )
	{
		preview = atoi(optarg);
		if(preview <= 0 || preview > 4 )
		{
			fprintf(stderr, "--preview [0-4]\n");
			err++;
		}
	}
        else
	        err++;
        return err;
}
static volatile gulong g_trap_free_size = 0;
static struct timeval time_last_;

static char **cargv = NULL;


gboolean	gveejay_idle(gpointer data)
{
	if(gveejay_running())
	{
		int sync = 0;
		if( is_alive(&sync) == FALSE ) {
			//@ restart reloaded
			veejay_msg(VEEJAY_MSG_WARNING,
				"No connection with veejay, attempting restart.");
			reloaded_restart();
		} 
		if( sync ) {
			if( gveejay_time_to_sync( get_ui_info() ) )
			{
				if(veejay_update_multitrack( get_ui_info() ))
					update_gveejay();
			}
		}

	}

	if( gveejay_restart() )
	{
		//@ reinvoke 
		if( execvp( cargv[0], cargv ) == -1 )
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to restart");
	}

	return TRUE;
}

static	void	clone_args( char *argv[], int argc )
{	
	int i = 0;
	if( argc <= 0 )
		return;

	cargv = (char**) malloc(sizeof(char*) * (argc+1) );
	memset( cargv, 0, sizeof(char*) * (argc+1));
	for( i = 0; i < argc ; i ++ )
		cargv[i] = strdup( argv[i] );

}

int main(int argc, char *argv[]) {
        char option[2];
        int n;
        int err=0;

        if(!argc) usage(argv[0]);

	clone_args( argv, argc );

	// default host to connect to
	sprintf(hostname, "127.0.0.1");

        while( ( n = getopt( argc, argv, "s:h:p:tnvHf:X:P:q")) != EOF )
        {
                sprintf(option, "%c", n );
                err += set_option( option, optarg);
                if(err) usage(argv[0]);
        }
        if( optind > argc )
                err ++;

        if( err ) usage(argv[0]);
/*
	if( !g_thread_supported() )
	{
	    veejay_msg(2, "Initializing GDK threads");
	     g_thread_init(NULL);
	     gdk_threads_init();                   // Called to initialize internal mutex "gdk_threads_mutex".
        }*/

	gtk_init( NULL,NULL );
	glade_init();
	
//	g_mem_set_vtable( glib_mem_profiler_table );
	
	vj_mem_init();

	vevo_strict_init();

	find_user_themes(gveejay_theme);
	
	vj_gui_set_debug_level( verbosity , n_tracks,pw,ph);
	vj_gui_set_timeout(timer);
	set_skin( 1 );

	default_bank_values( &col, &row );
	gui_load_theme();	
	vj_gui_init( skins[0].file, launcher, hostname, port_num, use_threads );
	vj_gui_style_setup();


	if( preview )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Starting with preview enabled");
		gveejay_preview(preview);
	}
	if( launcher )
	{
		reloaded_launcher( hostname, port_num );
	}

	memset( &time_last_, 0, sizeof(struct timeval));

	while(gveejay_running()) {
		gveejay_idle(NULL);
		while( gtk_events_pending()  ) 
			gtk_main_iteration();

	}


	return 0;  
}


