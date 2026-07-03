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
#include <veejaycore/core.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/libvevo.h>
#include <src/vj-api.h>
#include <locale.h>
#define RELOADED_SUMMARY "-------------------------------------\n\
Reloaded, a graphical interface for Veejay.\n\n\
Reloaded is a client for Veejay. As long as Veejay \
(the server) is running, you can connect and disconnect from it with Reloaded.\n\
-------------------------------------"


extern int mt_get_max_tracks();
static int load_midi = 0;
static int port_num = DEFAULT_PORT_NUM;
static char hostname[255];
static int verbosity = 0;
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
static gint arg_sample_pages = 0;
static gboolean arg_version = FALSE;
static gchar *arg_style = NULL;
static gboolean arg_smallaspossible = FALSE;
static gboolean arg_fasterui = FALSE;
static gchar *help_text = NULL;

static const char skinfile[] = "gveejay.reloaded.glade"; //FIXME Has binary ressource ?

extern void reloaded_launcher( char *h, int p );

static volatile gulong g_trap_free_size = 0;

static void usage(void)
{
    g_printerr ("%s\n", help_text);  //FIXME why program name is (null) ???
}

static int parse_samplebank_size_option(const char *text, int *cols, int *rows, int *pages)
{
    int a = 0;
    int b = 0;
    char tail = 0;

    if(!text || !*text)
        return 0;

    if(sscanf(text, " %d %c", &a, &tail) == 1) {
        *pages = a;
        return 1;
    }

    if(sscanf(text, " %d x %d %c", &a, &b, &tail) == 2 ||
       sscanf(text, " %d X %d %c", &a, &b, &tail) == 2 ||
       sscanf(text, " %d:%d %c", &a, &b, &tail) == 2 ||
       sscanf(text, " %d,%d %c", &a, &b, &tail) == 2)
    {
        *cols = a;
        *rows = b;
        return 1;
    }

    return 0;
}

void vj_gui_startup (GApplication *application, gpointer user_data)
{

}

typedef struct {
    GApplication *app;
} GuiContext;

static gboolean vj_gui_idle_cb1(gpointer data)
{
    GuiContext *ctx = data;
    if (!gveejay_idle(NULL)) {
        fprintf(stderr, "Quitting application");
        g_application_quit(ctx->app);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void vj_gui_activate(GApplication *app, gpointer user_data)
{
    GuiContext *ctx = g_new0(GuiContext, 1);
    ctx->app = app;


    g_application_hold(app);

    vj_gui_set_debug_level(verbosity, n_tracks, 0, 0);
    default_bank_values(NULL, NULL);

    register_signals();

    if (preview) {
        gveejay_preview(preview);
    }

    vj_gui_init(skinfile, launcher, hostname, port_num,
                use_threads, load_midi, midi_file,
                arg_beta, arg_autoconnect, arg_fasterui);

    g_timeout_add_full(
        G_PRIORITY_DEFAULT,
        16,
        vj_gui_idle_cb1,
        ctx,
        (GDestroyNotify)g_free
    );
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
        fprintf(stdout, "Version: %s\n", PACKAGE_VERSION);
        fprintf(stdout, "\tLinked against libveejaycore %s\n", veejay_core_build());
        fprintf(stdout, "Data directory: %s\n", get_gveejay_dir());
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
          veejay_msg(VEEJAY_MSG_WARNING, "--geometry parameter requires \"X,Y\" screen coordinates: \"%s\"", arg_geometry);
        }else
        {
          if(verbosity) veejay_msg(VEEJAY_MSG_INFO, "Placing window at %d,%d.", geom_[0],geom_[1]);
          vj_gui_set_geom(geom_[0],geom_[1]);
        }
        g_free(arg_geometry);
    }

    if ( arg_host )
    {
        strcpy( hostname, arg_host );
        g_free(arg_host);
        if(verbosity) veejay_msg(VEEJAY_MSG_INFO, "Selected host: %s.", hostname);
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
        if(verbosity) veejay_msg(VEEJAY_MSG_INFO, "Using port %d.", port_num);
        launcher++;
    }

    if ( arg_preview )
    {
        preview = arg_preview;
        if(preview <= 0 || preview > 4 )
        {
            veejay_msg(VEEJAY_MSG_ERROR, "--preview parameter must be in the range 0-4: %d", preview);
            err++;
        }
        else if(verbosity) veejay_msg(VEEJAY_MSG_INFO, "Preview quality: %d", preview);
    }

    {
        int sample_cols = 6;
        int sample_rows = 2;
        int sample_pages = arg_sample_pages > 0 ? arg_sample_pages : 12;

        if(arg_size) {
            if(!parse_samplebank_size_option(arg_size, &sample_cols, &sample_rows, &sample_pages)) {
                veejay_msg(VEEJAY_MSG_ERROR, "--size/-s requires pages or a sample-bank layout N, CxR, C:R or C,R: \"%s\"", arg_size);
                err++;
            }
            g_free(arg_size);
            arg_size = NULL;
        }

        if(!set_samplebank_layout(sample_cols, sample_rows, sample_pages))
            err++;
    }

    if ( arg_tracks )
    {
        n_tracks = 1 + arg_tracks;
        if( n_tracks < 1 || n_tracks > mt_get_max_tracks() )
            n_tracks = 1;
        if(verbosity) veejay_msg(VEEJAY_MSG_INFO, "Track count set to %d", n_tracks);
    }

    if( arg_style ) {
        vj_gui_set_stylesheet(arg_style,arg_smallaspossible);
        g_free(arg_style);
    } else {
        vj_gui_set_stylesheet(arg_style,arg_smallaspossible);
    }

    if( err )
    {
        usage();
        g_free(help_text);
        return EXIT_FAILURE;
    }

    g_application_activate(app);

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
/* default host to connect to */
    snprintf(hostname,sizeof(hostname), "127.0.0.1");
    char port_description [255];
    snprintf (port_description, sizeof (port_description),
              "Veejay port to connect to (default: %d).", DEFAULT_PORT_NUM);

    setlocale(LC_NUMERIC, "C");

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
    {"auto-connect", 'a', 0, G_OPTION_ARG_NONE, &arg_autoconnect, "Auto-connect to locally running Veejay instances.", NULL},
    {"beta",        'b', 0, G_OPTION_ARG_NONE, &arg_beta, "Enable beta features.", NULL},
    {"geometry",    'g', 0, G_OPTION_ARG_STRING, &arg_geometry, "Window position on screen \"X,Y\".", NULL},
    {"host",        'h', 0, G_OPTION_ARG_STRING, &arg_host, "Veejay host to connect to (default: localhost).", NULL},
    {"lowband",     'L', 0, G_OPTION_ARG_NONE, &arg_lowband, "Low-bandwidth connection (disables image loading in the sample bank).", NULL},
    {"midi",        'm', 0, G_OPTION_ARG_FILENAME, &arg_midifile, "MIDI configuration file.", NULL},
    {"no-color",    'n', 0, G_OPTION_ARG_NONE, &arg_notcolored, "Do not use colored text in console logs.", NULL},
    {"port",        'p', 0, G_OPTION_ARG_INT, &arg_port, port_description, NULL},
    {"preview",     'P', 0, G_OPTION_ARG_INT, &arg_preview, "Start with preview enabled (1=full, 2=1/2, 3=1/4, 4=1/8).", NULL},
    {"sample-pages", 0, 0, G_OPTION_ARG_INT, &arg_sample_pages, "Number of sample-bank pages to allocate in the grid (default: 12, max: 512).", "N"},
    {"size",        's', 0, G_OPTION_ARG_STRING, &arg_size, "Sample-bank pages or layout: N, CxR, C:R or C,R (default: 12 pages, 6x2).", "N|CxR"},
    {"verbose",     'v', 0, G_OPTION_ARG_NONE, &arg_verbose,"Be more verbose.", NULL},
    {"version",     'V', 0, G_OPTION_ARG_NONE, &arg_version,"Show version and data directory, then exit.", NULL},
    {"tracks",      'X', 0, G_OPTION_ARG_INT, &arg_tracks,"Set the number of tracks.", NULL},
    {"theme",       't', 0, G_OPTION_ARG_FILENAME, &arg_style, "Use \"system\" for the default theme, or pass a stylesheet filename.", NULL },
    {"small-as-possible",'S',0,G_OPTION_ARG_NONE,&arg_smallaspossible, "Create the smallest possible UI.",NULL},
#if GTK_CHECK_VERSION(3,22,30)
    {"faster-ui",   'f', 0, G_OPTION_ARG_NONE, &arg_fasterui, "Hide FX parameter sliders instead of disabling them to reduce CPU usage (GTK3 3.22.30).", NULL},
#endif
    {NULL}};

    context = g_option_context_new (NULL);
    g_option_context_set_summary (context, RELOADED_SUMMARY);
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
