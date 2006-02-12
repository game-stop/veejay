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



#include <gtk/gtk.h>
#include <glib.h>
#include <glade/glade.h>
extern GladeXML *main_window;

void	on_spin_width_value_changed( GtkWidget *w , gpointer data )
{
	set_property_int( "-W",
		(int)gtk_spin_button_get_value( GTK_SPIN_BUTTON(w)) );
}
void	on_spin_height_value_changed( GtkWidget *w, gpointer data)
{
	set_property_int( "-H",
		(int)gtk_spin_button_get_value( GTK_SPIN_BUTTON(w)) );
}

void	on_use_bezerk_toggled( GtkWidget *w, gpointer data )
{
	set_property_bool( "-b",
		(int)gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w)) );
}

void	on_use_autodeinterlace_toggled( GtkWidget *w , gpointer data)
{
	set_property_bool( "-I",
		(int)gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w)) );
}

void	on_have_sync_toggled(GtkWidget *w, gpointer data )
{
	set_property_int( "-c",
		(int)gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w)) );
}

void	on_have_path_toggled(GtkWidget *w, gpointer data)
{
	 set_property_bool( "-P",
		(int)gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w)) );
}

void	on_fileassample_toggled( GtkWidget *w, gpointer data )
{
	 set_property_bool( "-g",
		(int)gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w)) );
}

void	on_assample_toggled( GtkWidget *w, gpointer data)
{
	set_property_bool( "-L",
		(int)gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w)) );
}

void	on_portnumber_value_changed(GtkWidget *w, gpointer data)
{
	set_property_int( "-p",
		(int)gtk_spin_button_get_value( GTK_SPIN_BUTTON(w)) );
}

void	on_cachepercentage_value_changed( GtkWidget *w, gpointer data)
{
	set_property_int( "-m",
		(int)gtk_spin_button_get_value( GTK_SPIN_BUTTON(w)) );
}
void	on_cachelines_value_changed( GtkWidget *w, gpointer data)
{
	set_property_int( "-j",
		(int)gtk_spin_button_get_value( GTK_SPIN_BUTTON(w)) );

}

void	on_use_verbose_toggled( GtkWidget *w, gpointer data )
{
	set_property_bool( "-v",
		(int)gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w)) );
}
void	on_use_audio_toggled( GtkWidget *w, gpointer data )
{
	set_property_bool( "-a",
		(int)gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w)) );
}
void	on_framerate_value_changed( GtkWidget *w, gpointer data)
{
	set_property_int( "-f",
		(int)gtk_spin_button_get_value( GTK_SPIN_BUTTON(w)) );
}

void	on_audiorate_changed( GtkWidget *w , gpointer data )
{
	gchar *fmt = gtk_entry_get_text (GTK_ENTRY (GTK_BIN (w)->child));
	int res = 48000;
	if( strncasecmp( fmt, "44 Khz", 8 ) == 0 )
		res = 44000;
	set_property_int( "-r", res );
}

void	on_pixelformat_changed( GtkWidget *w , gpointer data)
{
	gchar *fmt = gtk_entry_get_text (GTK_ENTRY (GTK_BIN (w)->child));
	int res = 0;
	if(strncasecmp(fmt, "YUV 4:2:2", 9) == 0 )
		res = 1;
	set_property_int( "-Y", res );
}

void	on_video_out_changed( GtkWidget *w, gpointer data)
{
	gchar *fmt = gtk_entry_get_text (GTK_ENTRY (GTK_BIN (w)->child));
	int res = 0;
	if( strncasecmp( fmt, "HeadLess", 8 ) == 0 )
		res = 5;
	if( strncasecmp( fmt, "DirectFB", 8 ) == 0 )
		res = 1;
	if( strncasecmp( fmt, "SDL and DirectFB",17) == 0 )
		res = 2;
	set_property_int( "-O", res );
}

void	on_options_toggled( GtkWidget *w, gpointer data )
{
	GtkWidget *right = glade_xml_get_widget( main_window, "vbox_extra_options" );	 
	GtkWidget *mw = glade_xml_get_widget( main_window, "LauncherWindow" );	 
		  	 
 	if(gtk_toggle_button_get_active(w))
                gtk_widget_show(right);
        else{
                 gtk_widget_hide(right);
                 gtk_window_resize(mw, 352, 520);
        }

}
static	void	file_ok_sel( GtkWidget *w, GtkFileSelection *fs )
{
	gchar **list = gtk_file_selection_get_selections( fs ); 

	int i;
	if(list)
		for( i = 0; list[i] != NULL ; i ++ )
			add_property_file( i, list[i] );
	g_strfreev(list);
	gtk_widget_destroy( GTK_WIDGET(fs));
}

void	on_filebrowse_clicked( GtkWidget *widget, gpointer data)
{
	GtkWidget *w = gtk_file_selection_new( "Open EDL and / or video files");
	gtk_file_selection_hide_fileop_buttons( GTK_FILE_SELECTION( w ) );
	gtk_file_selection_set_select_multiple( GTK_FILE_SELECTION( w ), TRUE );

	g_signal_connect( G_OBJECT( w ), "destroy",
				G_CALLBACK( gtk_widget_destroy ), (gpointer) w );
	g_signal_connect( G_OBJECT( GTK_FILE_SELECTION( w )->ok_button ),
			"clicked",
			G_CALLBACK( file_ok_sel ), (gpointer) w );
	g_signal_connect_swapped( G_OBJECT( GTK_FILE_SELECTION( w )->cancel_button ),
			"clicked",
			G_CALLBACK( gtk_widget_destroy ), G_OBJECT(w));
	gtk_widget_show( w );
}

void	on_actionbrowse_clicked( GtkWidget *widget, gpointer data)
{

}

void	on_fromfile_toggled( GtkWidget *w , gpointer data)
{
	gboolean state = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w));
	if(state)
	{
		set_property_int( "-W" , 0 );
		set_property_int( "-H" , 0 );
		set_property_int( "-r" , 0 );
		gtk_widget_show( 
		 glade_xml_get_widget(main_window, "nodummy" ));
		if( gtk_toggle_button_get_active( glade_xml_get_widget(main_window, "fromdummy" )))
			gtk_toggle_button_set_active( glade_xml_get_widget(main_window, "fromdummy" ), FALSE );
	}
	else
	{
		gtk_widget_hide( 
		 glade_xml_get_widget(main_window, "nodummy" ));
	}

}

void	on_fromdummy_toggled( GtkWidget *w, gpointer data )
{
	gboolean state = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w));
	if( state )
	{
		refresh_files();
		gtk_widget_show( 
		 glade_xml_get_widget(main_window, "dummyvideo" ) );
		gtk_widget_show( 
		 glade_xml_get_widget(main_window, "dummyaudio" ) );
		if( gtk_toggle_button_get_active( glade_xml_get_widget(main_window, "fromfile" )))
			gtk_toggle_button_set_active( glade_xml_get_widget(main_window, "fromfile" ), FALSE );

		gtk_widget_set_sensitive(
			glade_xml_get_widget(main_window, "launch"),
			TRUE );
	}
	else
	{
		gtk_widget_hide( 
		 glade_xml_get_widget(main_window, "dummyvideo" ));
		gtk_widget_hide( 
		 glade_xml_get_widget(main_window, "dummyaudio" ) );
		gtk_widget_set_sensitive(
			glade_xml_get_widget(main_window, "launch"),
			FALSE );

	}
}

void	on_launch_clicked( GtkWidget *w , gpointer data )
{
	start();
}

void	on_theme_changed( GtkWidget *w, gpointer user_data)
{
	gchar *fmt = gtk_entry_get_text (GTK_ENTRY (GTK_BIN (w)->child));
	char token[100];
	int res = 0;
	if( strncasecmp( fmt, "Default",7 ) == 0 )
	{
		res = 1;
	}
	if( res )
		gui_property( "theme", res );

}

void	on_gveejayreloaded_toggled(GtkWidget *w , gpointer user_data)
{
	gui_property( "usegui",
		(int) gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w) ));
}
