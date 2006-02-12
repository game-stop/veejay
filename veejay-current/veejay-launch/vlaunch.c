/* veejay launcher - Linux VeeJay - GVeejay GTK+-2/Glade User Interface
 *           (C) 2002-2007 Niels Elburg <nelburg@looze.net> 
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glade/glade.h>
#include <libvjnet/vj-client.h>
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <libvevo/vevo.h>
#include <libvevo/livido.h>
#include <veejay/vevo.h>
#include <gtk/gtkversion.h>
#include <gdk/gdk.h>
#include <assert.h>
static int default_port_num	= 3490;
static int verbosity = 0;

static	void	*options_ = NULL;
static  void	*files_   = NULL;
static	void	*gui_	  = NULL;

static	int	is_running_ = 0;
static gint 	is_alive_ = 0;

GladeXML *main_window = NULL;

#define SPIN 1
#define TOGGLE 2

static struct
{
	const char *name;
	int	    value;
	int	    type;
} default_arguments[] = 
{
	{ "spin_width", 352, SPIN },	
	{ "spin_height",288, SPIN },
	{ "use_bezerk",   FALSE, TOGGLE },
	{ "use_autodeinterlace", FALSE, TOGGLE },
	{ "have_sync",    TRUE, TOGGLE },
	{ "have_path", 	  TRUE, TOGGLE },
	{ "fileassample", FALSE, TOGGLE },
	{ "assample",	  FALSE, TOGGLE },
	{ "portnumber",3490, SPIN },
	{ "cachepercentage",15,SPIN },
	{ "cachelines", 3, SPIN },
	{ "use_verbose", FALSE, TOGGLE },
	{ "use_audio", FALSE, TOGGLE },
	{ "framerate", 25, SPIN },
	{ "gveejayreloaded",1, TOGGLE },
	{ "fromfile", 0, TOGGLE },
	{ "fromdummy",0, TOGGLE },
	{ NULL,	0, 0 },
};

void	set_defaults()
{
	int i;
	for( i = 0; default_arguments[i].name != NULL ; i ++ )
	{
		switch( default_arguments[i].type )
		{
			case SPIN:
				gtk_spin_button_set_value(
				  GTK_SPIN_BUTTON(glade_xml_get_widget( main_window, default_arguments[i].name )),(gdouble) default_arguments[i].value );
				break;
			case TOGGLE:
				gtk_toggle_button_set_active(
				  GTK_TOGGLE_BUTTON(glade_xml_get_widget( main_window, default_arguments[i].name )), default_arguments[i].value );
				break;
			default:
				break;
		}
	}

//	gtk_combo_box_append_text( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "theme" ) ), "Reloaded" );
//	gtk_combo_box_append_text( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "theme" ) ), "Default" );
//	gtk_combo_box_set_active(  GTK_COMBO_BOX( glade_xml_get_widget( main_window, "theme" ) ) , 1 );


#ifdef HAVE_SDL
	gtk_combo_box_append_text( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "video_out" ) ), "SDL" );
#endif
	gtk_combo_box_append_text( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "video_out" ) ), "HeadLess");
	gtk_combo_box_set_active( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "video_out" ) ) , 0 );
#ifdef HAVE_DIRECTFB
	gtk_combo_box_append_text( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "video_out" ) ), "DirectFB" );
#ifdef HAVE_SDL
	gtk_combo_box_append_text( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "video_out" ) ), "SDL and DirectFB" );
#endif
#endif

#ifdef HAVE_JACK
	gtk_combo_box_append_text( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "audiorate" ) ), "44 Khz" );
	gtk_combo_box_append_text( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "audiorate" ) ), "48 Khz" );
	gtk_combo_box_set_active( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "audiorate" ) ) , 1 );

#else
	gtk_widget_set_sensitive( 
		glade_xml_get_widget( main_window, "audiorate" ) , FALSE );
	gtk_widget_set_sensitive(
		glade_xml_get_widget( main_window, "use_audio" ), FALSE );	
#endif


	gtk_combo_box_append_text( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "pixelformat" ) ), "YUV 4:2:0" );
	gtk_combo_box_append_text( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "pixelformat" ) ), "YUV 4:2:2" );
	gtk_combo_box_set_active( GTK_COMBO_BOX( glade_xml_get_widget( main_window, "pixelformat" ) ) , 1 );
	gtk_widget_set_sensitive( glade_xml_get_widget( main_window, "launch" ), FALSE );


}

void	get_gd(char *buf, char *suf, const char *filename)
{

	if(filename !=NULL && suf != NULL)
		sprintf(buf, "%s/%s/%s", GVEEJAY_DATADIR,suf, filename );
	if(filename !=NULL && suf==NULL)
		sprintf(buf, "%s/%s", GVEEJAY_DATADIR, filename);
	if(filename == NULL && suf != NULL)
		sprintf(buf, "%s/%s/" , GVEEJAY_DATADIR, suf);
}

void	theme(int use)
{
	char path[1024];
	bzero(path,1024);
	if(use)
		get_gd(path,NULL, "gveejay-default.rc");
	else
		get_gd(path,NULL, "gveejay.rc");
	gtk_rc_parse(path);
}

void	init()
{
	gtk_init(NULL,NULL);

	options_ = vevo_port_new( 5005 );
	files_   = vevo_port_new( 5006 );
	gui_	 = vevo_port_new( 5007 );
	glade_init();
}

void	refresh_files()
{
	vevo_port_free( files_ );
	files_ = vevo_port_new( 5006 );
}

static	void	run_gveejay_now()
{
	gchar *args[4];
	gint pid = 0;
	GError *error = NULL;
	gint use = 0;
	gint no_t =0;
	gint port_num = 0;
	char tmp[10];
	vevo_property_get( gui_, "usegui", 0, &use );
	if(!use)
		exit(0);

	args[0] = strdup( "gveejayreloaded" );	
	vevo_property_get( options_, "-p", 0, &port_num );
	sprintf(tmp, "-p %d", port_num );
	args[1] = strdup(tmp);
	args[2] = NULL;
	args[3] = NULL;
	vevo_property_get( gui_, "theme", 0, &no_t );
	if( no_t )
		args[2] = strdup( "-n");

	gboolean ret = g_spawn_async_with_pipes(
			NULL,
			args,
			NULL,
			G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			NULL,
			NULL,
			&pid,
			NULL,
			NULL,
			NULL,
			&error );
	gint p = 0;
	exit(0);
}



static	void	*my_veejay_ = NULL;

static void	run_gveejay()
{
	gtk_widget_hide( glade_xml_get_widget( main_window, "LauncherWindow"));
	g_usleep( 1000000 );
	run_gveejay_now();
}

void	deinit()
{
//	vevo_port_free( options_ );
//	vevo_port_free( files_ );
}
char	*clone_file_arg( char *key )
{
	char    token[100]; 
	int	atom_type = vevo_property_atom_type( files_, key );
	int	res = 0;
	char 	*res2= NULL;
	size_t	 rs = 0;
	switch(atom_type)
	{
		case LIVIDO_ATOM_TYPE_STRING:
			// return value of atom
			rs = vevo_property_element_size( files_, key,0 );
			if(rs<=0)
				return NULL;
			res2 = (char*) malloc(sizeof(char) * rs );
			vevo_property_get(files_, key, 0, &res2 );
			return res2; 	
			break;
		default:
			return NULL;
	}
	return NULL;
}
char	*clone_arg( char *key )
{
	char    token[100]; 
	int	atom_type = vevo_property_atom_type( options_, key );
	int	res = 0;
	switch(atom_type)
	{
		case LIVIDO_ATOM_TYPE_INT:
			vevo_property_get( options_, key, 0, &res );
			sprintf(token, "%s %d", key, res );
			break;
		case LIVIDO_ATOM_TYPE_BOOLEAN:
			vevo_property_get( options_, key, 0, &res );
			sprintf(token, "%s" , key );
			break;
		default:
			return NULL;
	}
	if(res)
		return strdup(token);

	return NULL;
}

int	num_files()
{
	char **args = vevo_list_properties( files_ );
	if(!args) return 0;
	int i = 0;
	while( args[i] != NULL ) i ++;
	return i;
}

int	num_args()
{
	char **args = vevo_list_properties( options_ );
	if(!args) return 0;
	int i = 0;
	while( args[i] != NULL ) i ++;
	return i;
}

void	set_property_int( char *name, int value )
{
	vevo_property_set( options_, name, LIVIDO_ATOM_TYPE_INT, 1,&value );
}
void	set_property_bool( char *name, int value )
{
	vevo_property_set( options_, name, LIVIDO_ATOM_TYPE_BOOLEAN, 1,&value );
}
void	gui_property( char *name, int value )
{
	vevo_property_set( gui_, name, LIVIDO_ATOM_TYPE_BOOLEAN, 1,&value );
}
void	add_property_file( int num, char *filename )
{
	char key[10];
	sprintf(key,"f%d", num);
	vevo_property_set( files_,key, LIVIDO_ATOM_TYPE_STRING, 1, &filename );

	gtk_widget_set_sensitive(
		glade_xml_get_widget(main_window, "launch"),
		TRUE );
}


void	start()
{
	char **ui = NULL;
	char **args = NULL;
	int n = 0;
	int i = 0;
	int n_arg = num_args();
	int n_files = num_files();

	if( n_files == 0 )
		n_arg += 1;

	if( n_arg == 0 )
	{
		return;
	}
	
	args = (char**) malloc(sizeof(char*) * ( n_arg + n_files + 4) );
	memset( args, 0, sizeof(char*) * (n_arg + n_files + 4) );

	args[0] = strdup( "veejay" );
	n++;

	char **tmp_args = vevo_list_properties( options_ );
	if(tmp_args)
	{
		for( i = 0 ; tmp_args[i] != NULL ; i ++ )
		{
			char *token = clone_arg(tmp_args[i]);
			if(token) { args[n] = strdup(token); n ++ ; free(token); }
			free(tmp_args[i]);
		}
		free(tmp_args);
	}

	tmp_args = vevo_list_properties( files_ );
	if(tmp_args)
	{
		for( i = 0; tmp_args[i] != NULL ; i ++ )
		{
		  char *token = clone_file_arg(tmp_args[i]);
		  if(token) { args[n] = strdup(token); n ++ ; free(token); }
		  free(tmp_args[i]);
		}
		free(tmp_args);    
        } 
  
	gint pid = 0;
	GError *error = NULL;
	gboolean ret = g_spawn_async_with_pipes(
			NULL,
			args,
			NULL,
			G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			NULL,
			NULL,
			&pid,
			NULL,
			NULL,
			NULL,
			&error );
	gint p = 0;
	for( p = 0; args[p] != NULL; p ++ )
	{
printf("'%s'\n", args[p]);
		free(args[p]);
	}
	free(args);

	if( error )
	{
		fprintf(stderr, "Error %s\n", error->message );
		g_error_free( error );
		return;
	}
	if(! ret )
	{
		fprintf(stderr, "Failed to start veejay\n");
		return;
	}

	run_gveejay();


}

int main(int argc, char *argv[]) {

	if( !g_thread_supported() )
	{
	     g_thread_init(NULL);
	     gdk_threads_init();
        }

	init();
	theme(0);

	char path[PATH_MAX];
	get_gd( path, NULL, "veejaylaunch.glade" );
	main_window = glade_xml_new( path, NULL,NULL );

	glade_xml_signal_autoconnect( main_window );
	GtkWidget *mainw = glade_xml_get_widget(main_window,"LauncherWindow" );
	gtk_widget_show(mainw);

	set_defaults();

        gtk_main();

        return 0;  
}


