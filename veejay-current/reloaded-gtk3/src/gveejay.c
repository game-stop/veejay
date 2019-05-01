/* gveejay - Linux VeeJay - GVeejay GTK+-3/Glade User Interface
 *           (C) 2002-2018 Niels Elburg <nwelburg@gmail.com>
 *  with contributions by  Jerome Blanchi (2016-2018)
 *                        (Gtk3 Migration and other stuff)
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

#include <config.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>

#include <gtk/gtk.h>
#include <glib.h>

#include <veejay/vevo.h>
#include <veejay/vjmem.h>
#include <veejay/vj-msg.h>
#include <veejay/libvevo.h>
#include <src/vj-api.h>

extern int mt_get_max_tracks();
static int load_midi = 0;
static int port_num = DEFAULT_PORT_NUM;
static char hostname[255];
static int gveejay_theme = 0; //GTK3Migr : KEEP for now // set to 1 to load with the default reloaded theme
static int verbosity = 0;
static int col = 0;
static int row = 0;
static int n_tracks = 7;
static int launcher = 0;
static int preview = 0; // off
static int use_threads = 0;
static char midi_file[1024];
static int geom_[2] = { -1 , -1};

static gboolean arg_autoconnect = FALSE;
static gboolean arg_beta = FALSE;
static gchar *arg_geometry = NULL;
static gchar *arg_host = NULL;
static gboolean arg_lowband = FALSE;
static gchar *arg_midifile = NULL;
static gboolean arg_notcolored = FALSE;
static gint arg_port = 0;
static gboolean arg_verbose = FALSE;
static gint arg_preview = 0;
static gint arg_tracks = 0;
static gchar *arg_size = NULL;
static gboolean arg_version = FALSE;

static const char skinfile[] = "gveejay.reloaded.glade"; //FIXME Has binary ressource ?

extern void reloaded_launcher( char *h, int p );

static void usage(char *progname)
{
    printf( "Usage: %s <options>\n",progname);
    printf( "where options are:\n");
    printf( "-h\t\tVeejay host to connect to (defaults to localhost) \n");
    printf( "-p\t\tVeejay port to connect to (defaults to %d) \n", DEFAULT_PORT_NUM);
    printf( "-n\t\tDont use colored text\n");
    printf( "-v\t\tBe extra verbose (usefull for debugging)\n");
    printf( "-s\t\tSet bank resolution (row X columns)\n");
    printf( "-P\t\tStart with preview enabled (1=1/1,2=1/2,3=1/4,4=1/8)\n");
    printf( "-X\t\tSet number of tracks\n");
    printf( "-V\t\tShow version, data directory and exit.\n");
    printf( "-m <file>\tMIDI configuration file.\n");
    printf( "-g\t\t<X,Y>\tWindow position on screen.\n");
    printf( "-b\t\tEnable beta features.\n");
    printf( "-a\t\tAuto-connect to local running veejays.\n");
    printf( "-L\t\tLow-bandwith connection (disables image loading in samplebank)\n");

    printf( "\n\n");
}

static volatile gulong g_trap_free_size = 0;
static struct timeval time_last_;

static char **cargv = NULL;

gboolean gveejay_idle(gpointer data)
{
    if(gveejay_running())
    {
        int sync = 0;
        if( is_alive(&sync) == FALSE )
        {
          return FALSE;
        } 
        if( sync )
        {
          if( gveejay_time_to_sync( get_ui_info() ) )
          {
            veejay_update_multitrack( get_ui_info() );
          }
        }
        update_gveejay();
    }

    if( gveejay_restart() )
    {
        //@ reinvoke
        if( execvp( cargv[0], cargv ) == -1 )
            veejay_msg(VEEJAY_MSG_ERROR, "Unable to restart");
    }

    return TRUE;
}

static  void  clone_args( char *argv[], int argc )
{
    int i = 0;
    if( argc <= 0 )
        return;

    cargv = (char**) malloc(sizeof(char*) * (argc+1) );
    memset( cargv, 0, sizeof(char*) * (argc+1));
    for( i = 0; i < argc ; i ++ )
        cargv[i] = strdup( argv[i] );
}

void vj_gui_startup (GApplication *application, gpointer user_data)
{

}

/*
 * GApplication "activate" handler
 *
 *
 * FIXME  gtk_builder_new (); -ADD->   g_object_unref (info->main_window); (vj_gui_clean()?)
 */
static void vj_gui_activate (GtkApplication* app, gpointer        user_data)
{
    vj_gui_set_debug_level( verbosity , n_tracks,0,0);
    default_bank_values( &col, &row );

    register_signals();

    vj_gui_init( skinfile, launcher, hostname, port_num, use_threads, load_midi, midi_file,arg_beta, arg_autoconnect);
    vj_gui_style_setup();

    if( preview )
    {
        gveejay_preview(preview);
    }

restart_me:

    reloaded_show_launcher ();
    if( launcher )
    {
        reloaded_launcher( hostname, port_num );
    }

    memset( &time_last_, 0, sizeof(struct timeval));

    while(gveejay_running())
    {
        if(gveejay_idle(NULL)==FALSE)
            break;
        while( gtk_events_pending()  )
            gtk_main_iteration();
    }

    vj_event_list_free();

    if( gveejay_relaunch() ) {
      launcher = 1;
      reloaded_restart();
      goto restart_me;
    }
}

/*
 * GApplication "command-line" handler
 *
 * A nice place to check arguments validity and
 * do some preleminary actions.
 *
 */
gint vj_gui_command_line (GApplication            *app,
                          GApplicationCommandLine *cmdline)
{
  int err = 0;
  gint argc;
  gchar **argv;
  argv = g_application_command_line_get_arguments (cmdline, &argc);

/* First check version and quit */
    if ( arg_version )
    {
        fprintf(stdout, "version : %s\n", PACKAGE_VERSION);
        fprintf(stdout, "data directory : %s\n", get_gveejay_dir());
        return EXIT_FAILURE;
    }

    if (arg_verbose )
    {
        verbosity = 1;
    }

    if ( arg_geometry )
    { //FIXME NxN format
        if(sscanf( (char*) arg_geometry, "%d,%d",&geom_[0],&geom_[1]) != 2 )
        {
          veejay_msg(VEEJAY_MSG_WARNING, "--geometry parameter invalid \"X,Y\" screen coordinates : \"%s\"", arg_geometry);
        }else
        {
          if(verbosity) veejay_msg(VEEJAY_MSG_INFO, "Place window at %d,%d.", geom_[0],geom_[1]);
          vj_gui_set_geom(geom_[0],geom_[1]);
        }
        g_free(arg_geometry);
    }

    if ( arg_host )
    {
        strcpy( hostname, arg_host );
        g_free(arg_host);
        if(verbosity) veejay_msg(VEEJAY_MSG_INFO, "Selected host is %s.", hostname);
        launcher ++;
    }

    if ( arg_lowband )
    {
        set_disable_sample_image(TRUE);
    }

    if ( arg_midifile )
    {
        strcpy(midi_file, arg_midifile);
        g_free(arg_midifile);
        load_midi = 1;
    }

    if ( arg_notcolored )
    {
        veejay_set_colors(0);
    }

    if ( arg_port )
    {
        port_num=arg_port;
        if(verbosity) veejay_msg(VEEJAY_MSG_INFO, "We will have fun on port %d !", port_num);
        launcher++;
    }

    if ( arg_preview )
    {
        preview = arg_preview;
        if(preview <= 0 || preview > 4 )
        {
            veejay_msg(VEEJAY_MSG_ERROR, "--preview parameter invalid [0-4] : %d", preview);
            err++;
        }
        else if(verbosity) veejay_msg(VEEJAY_MSG_INFO, "Preview at quality %d", preview);
    }

    if ( arg_size )
    {
        if(sscanf( (char*) arg_size, "%dx%d", &row, &col ) != 2 )
        {
            veejay_msg(VEEJAY_MSG_ERROR, "--size parameter requires \"NxN\" argument : \"%s\"", arg_size);
            err++;
        }
        g_free(arg_size);
    }

    if ( arg_tracks )
    {
        n_tracks = 1 + arg_tracks;
        if( n_tracks < 1 || n_tracks > mt_get_max_tracks() )
            n_tracks = 1;
        if(verbosity) veejay_msg(VEEJAY_MSG_INFO, "TracXs parameted at %d", n_tracks);
    }

    if( err )
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    g_application_activate(app);

    veejay_msg(VEEJAY_MSG_INFO, "See you!");

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if(!argc) { usage(argv[0]); exit(-1);}// ??? FIXME
    clone_args( argv, argc );

/* default host to connect to */
    snprintf(hostname,sizeof(hostname), "127.0.0.1");
    char port_description [255];
    snprintf (port_description, sizeof (port_description),
              "Veejay port to connect to (defaults to %d).", DEFAULT_PORT_NUM);

    GtkApplication *app;
    int status;

    app = gtk_application_new ("org.veejay.reloaded", G_APPLICATION_HANDLES_COMMAND_LINE|G_APPLICATION_NON_UNIQUE);
    g_signal_connect (app, "activate", G_CALLBACK (vj_gui_activate), NULL);
    g_signal_connect (app, "startup", G_CALLBACK (vj_gui_startup), NULL);
    g_signal_connect (app, "command-line", G_CALLBACK (vj_gui_command_line), NULL);

    GError *error = NULL;
    GOptionContext *context;

/* in alphabetical order of short options */
    const GOptionEntry options[] = {
    {"autoconnect", 'a', 0, G_OPTION_ARG_NONE, &arg_autoconnect, "Auto-connect to local running veejays.", NULL},
    {"beta",        'b', 0, G_OPTION_ARG_NONE, &arg_beta, "Enable beta features.", NULL},
    {"geometry",    'g', 0, G_OPTION_ARG_STRING, &arg_geometry, "Window position on screen \"X,Y\".", NULL},
    {"host",        'h', 0, G_OPTION_ARG_STRING, &arg_host, "Veejay host to connect to (defaults to localhost).", NULL},
    {"lowband",     'L', 0, G_OPTION_ARG_NONE, &arg_lowband, "Low-bandwith connection (disables image loading in samplebank)", NULL},
    {"midi",        'm', 0, G_OPTION_ARG_FILENAME, &arg_midifile, "MIDI configuration file.", NULL},
    {"notcolored",  'n', 0, G_OPTION_ARG_NONE, &arg_notcolored, "Dont use colored text.", NULL},
    {"port",        'p', 0, G_OPTION_ARG_INT, &arg_port, port_description, NULL},
    {"preview",     'P', 0, G_OPTION_ARG_INT, &arg_preview, "Start with preview enabled (1=1/1,2=1/2,3=1/4,4=1/8)", NULL},
    {"size",        's', 0, G_OPTION_ARG_STRING, &arg_size, "Set bank row and columns resolution \"RxC\".", NULL},
    {"verbose",     'v', 0, G_OPTION_ARG_NONE, &arg_verbose,"Be extra verbose (usefull for debugging)", NULL},
    {"version",     'V', 0, G_OPTION_ARG_NONE, &arg_version,"Show version, data directory and exit.", NULL},
    {"tracXs",      'X', 0, G_OPTION_ARG_INT, &arg_tracks,"Set number of tracks.", NULL},
    {NULL}};

    context = g_option_context_new (NULL);
    g_option_context_set_help_enabled(context, TRUE);
    g_option_context_add_main_entries (context, options, NULL);
    g_option_context_add_group (context, gtk_get_option_group (TRUE));
    if (!g_option_context_parse (context, &argc, &argv, &error))
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Option parsing failed: %s\n", error->message);
        usage(argv[0]);
        g_error_free (error);
        g_option_context_free(context);
        return -1;
    }
    g_option_context_free(context);

    vj_mem_init();
    vevo_strict_init();

    status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);

    return status;
}
