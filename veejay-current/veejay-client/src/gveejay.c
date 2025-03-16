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

#include <veejaycore/vevo.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/libvevo.h>
#include <src/vj-api.h>

#define RELOADED_SUMMARY "-------------------------------------\n\
Reloaded, a graphical interface for Veejay.\n\n\
Reloaded is a client for veejay. As long as veejay \
(the server) is running, you can connect and disconnect from it with reloaded.\n\
-------------------------------------"

#define RELOADED_DESCRIPTION "-------------------------------------\n\
The veejay website is over http://veejayhq.net\n\n\
If you found a bug, please use the ticket system on https://github.com/c0ntrol/veejay/issues\n\
-------------------------------------"

extern int mt_get_max_tracks();
static int load_midi = 0;
static int port_num = DEFAULT_PORT_NUM;
static char hostname[255];
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
static gchar *arg_style = NULL;
static gboolean arg_smallaspossible = FALSE;
static gboolean arg_fasterui = FALSE;
static gchar *help_text = NULL;

static const char skinfile[] = "gveejay.reloaded.glade"; //FIXME Has binary ressource ?

extern void reloaded_launcher( char *h, int p );

static volatile gulong g_trap_free_size = 0;

static void usage ()
{
    g_printerr ("%s\n", help_text);  //FIXME why program name is (null) ???
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
static void vj_gui_activate (GApplication* app, gpointer        user_data)
{
    vj_gui_set_debug_level( verbosity , n_tracks,0,0);
    default_bank_values( &col, &row );

    register_signals();

    if( preview )
    {
        gveejay_preview(preview);
    }
    
    vj_gui_init( skinfile, launcher, hostname, port_num, use_threads, load_midi, midi_file,arg_beta, arg_autoconnect, arg_fasterui);

    while( gveejay_idle(NULL) )
    {
        while( gtk_events_pending() ) {
            gtk_main_iteration_do(FALSE);
            if (!gveejay_idle(NULL))
                break;
        }

        if(!gtk_events_pending() ) {
            g_usleep(5000);
        }
    }

    g_application_quit(app);
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

    if( arg_style ) {
        vj_gui_set_stylesheet(arg_style,arg_smallaspossible);
        g_free(arg_style);
    } else {
        vj_gui_set_stylesheet(NULL,arg_smallaspossible);
    }

    if( err )
    {
        usage();
        g_free(help_text);
        return EXIT_FAILURE;
    }

    g_application_activate(app);

    veejay_msg(VEEJAY_MSG_INFO, "See you!");

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
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
    {"auto-connect", 'a', 0, G_OPTION_ARG_NONE, &arg_autoconnect, "Auto-connect to local running veejays.", NULL},
    {"beta",        'b', 0, G_OPTION_ARG_NONE, &arg_beta, "Enable beta features.", NULL},
    {"geometry",    'g', 0, G_OPTION_ARG_STRING, &arg_geometry, "Window position on screen \"X,Y\".", NULL},
    {"host",        'h', 0, G_OPTION_ARG_STRING, &arg_host, "Veejay host to connect to (defaults to localhost).", NULL},
    {"lowband",     'L', 0, G_OPTION_ARG_NONE, &arg_lowband, "Low-bandwith connection (disables image loading in samplebank)", NULL},
    {"midi",        'm', 0, G_OPTION_ARG_FILENAME, &arg_midifile, "MIDI configuration file.", NULL},
    {"no-color",    'n', 0, G_OPTION_ARG_NONE, &arg_notcolored, "Do not use colored text in console logging.", NULL},
    {"port",        'p', 0, G_OPTION_ARG_INT, &arg_port, port_description, NULL},
    {"preview",     'P', 0, G_OPTION_ARG_INT, &arg_preview, "Start with preview enabled (1=1/1,2=1/2,3=1/4,4=1/8)", NULL},
    {"size",        's', 0, G_OPTION_ARG_STRING, &arg_size, "Set bank resolution \"CxR\".", NULL},
    {"verbose",     'v', 0, G_OPTION_ARG_NONE, &arg_verbose,"Be extra verbose", NULL},
    {"version",     'V', 0, G_OPTION_ARG_NONE, &arg_version,"Show version, data directory and exit.", NULL},
    {"tracks",      'X', 0, G_OPTION_ARG_INT, &arg_tracks,"Set number of tracks.", NULL},
    {"theme",       't', 0, G_OPTION_ARG_FILENAME, &arg_style, "CSS FILE or \"default\"", NULL },
    {"small-as-possible",'S',0,G_OPTION_ARG_NONE,&arg_smallaspossible, "Create the smallest possible UI",NULL},
#if GTK_CHECK_VERSION(3,22,30)
    {"faster-ui",   'f', 0, G_OPTION_ARG_NONE, &arg_fasterui, "Hide FX parameter sliders instead of disabling to reduce CPU usage (GTK3 3.22.30)", NULL},
#endif
    {NULL}};

    context = g_option_context_new (NULL);
    g_option_context_set_summary (context, RELOADED_SUMMARY);
    g_option_context_set_description (context, RELOADED_DESCRIPTION);
    g_option_context_set_help_enabled(context, TRUE);
    g_option_context_add_main_entries (context, options, NULL);
    g_option_context_add_group (context, gtk_get_option_group (TRUE));

    help_text = g_option_context_get_help (context, TRUE, NULL);
    if (!g_option_context_parse (context, &argc, &argv, &error))
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Option parsing failed: %s\n", error->message);
        usage();
        g_free (help_text);
        g_error_free (error);
        g_option_context_free(context);
        return -1;
    }
    g_option_context_free(context);

    vj_mem_init(0,0);
    vevo_strict_init();

    status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);

    return status;
}
