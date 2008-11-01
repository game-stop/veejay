/* veejay - Linux VeeJay
 *           (C) 2002-2006 Niels Elburg <nelburg@looze.net> 
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
#include <stdio.h>
#include <ui/builder.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glade/glade.h>
#include <stdint.h>
#include <lo/lo.h>
#include <libvevo/libvevo.h>
#include <ui/samplebank.h>
//#include <ui/anim_ui.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
//@ need logging function to large character buffer
//@ need OSC server
//@ need to keep track when to destroy a window


static	void	*directors_  = NULL;
static  void	*windowport_    = NULL;
static  void    *widgetport_    = NULL;
static  int	screen_width_ = 0;
static  int	screen_height_ = 0;
static	int	main_panel_y_  = 0;
static  int	main_panel_x_  = 0;
static	int	current_x_     = 0;
static  int     current_y_     = 0;
static	int	fxcurrent_x_     = 0;
static  int     fxcurrent_y_     = 0;
static  int     veejay_tick_	 = 0;


static	GtkWidget	*MainWindow_ = NULL;
static  GtkWidget	*MainBox_    = NULL;
static  GtkWidget	*MainSubBox_    = NULL;
static  GtkWidget	*MainBotBox_    = NULL;

static  GtkWidget	*SamplePad_  = NULL;
static  GtkWidget	*PlayList_   = NULL;
static  GtkWidget       *EditList_   = NULL;
static  int		geom_[4] = { 0,0,0,0 };
static	int		skip_update_ = 0;

static	void	*sender_ = NULL;
static	void	*ssender_ = NULL;

typedef struct
{
	void *stats;
	void *info;
	char *key;
	void *values;
	int   wused;
	int   hused;
	int   ystart;
	int   right_fx;
} director_t;

static int prompt_dialog(const char *title, char *msg);

void 	*director_window_is_realized( const char *name);
////@@@@@@@2

void	director_lock()
{
	skip_update_ = 1;
}
void    director_unlock()
{
	skip_update_ = 0;
}

static	void	gtk_box_pack_end__( GtkWidget *a, GtkWidget *b, gboolean c, gboolean d, gint space,
	       const char *f, const int line	)
{
	printf("%s:%d\n",f,line);
	gtk_box_pack_end( a,b,c,d,space);
	
}

#define gtk_box_pack_end_( a,b,c,d,space ) gtk_box_pack_end__(a,b,c,d,space,__FUNCTION__,__LINE__ )

//@ optimize: take out gdk_threads from cb
void	director_update_widget( void *stats, const char *path, const char *format, int argc, int arg_offset, void **darg )
{
	lo_arg **argv = (lo_arg**) darg;
	GtkWidget *widget = (GtkWidget*) builder_widget_by_path( stats, path );
	if(!widget)
		return;

	char *widget_type = builder_get_signalwidget( stats, widget );
	if(!widget_type)
	{
		veejay_msg(0, "Dont know how to handle this widget");
		return;
	}
	
	concretebuilder_lock(stats);
	if(strncasecmp( widget_type, "checkbutton",11 ) == 0 )
	{
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget), argv[arg_offset+0]->i == 1 ? TRUE: FALSE );
	}
	else if(strncasecmp(widget_type, "vscale",6 ) == 0 || strncasecmp(widget_type, "hscale",6)==0)
	{
		switch(format[arg_offset+0])
		{
			case 'i':
			gtk_adjustment_set_value( GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment), (gdouble) argv[arg_offset+0]->i );
			break;
			case 'h':
			gtk_adjustment_set_value( GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment), (gdouble) argv[arg_offset+0]->h );
			break;
			case 'd':
			gtk_adjustment_set_value( GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment), (gdouble) argv[arg_offset+0]->d );
			break;
		}
		gtk_widget_queue_draw( widget );
	}
	else if(strncasecmp(widget_type, "spin",4 ) == 0 )
	{
		gtk_spin_button_set_value( GTK_SPIN_BUTTON( widget ), 
						(format[arg_offset+0]=='i' ? (gdouble) argv[arg_offset+0]->i : argv[arg_offset+0]->d ));
	}
	else if(strncasecmp(widget_type, "combo",5 ) == 0 )
	{
		gtk_combo_box_set_active( GTK_COMBO_BOX(widget), (gint) argv[arg_offset+0]->d );
	}
	else if (strncasecmp(widget_type, "label", 5 ) == 0 )
	{
		gtk_label_set_text( GTK_LABEL(widget), (gchar*) argv[arg_offset+0]->s );
	}
	concretebuilder_unlock(stats);
}

void	fx_window_handler( GtkWidget *widget, gpointer user_data )
{
	//@ bring up window or destroy it	
}

static	int	guess_suitable_entry( void *values )
{
	char **choices = vevo_list_properties(values);
	int k;
	if(!choices)
		return 0;
	for(k=0; choices[k]!=NULL;k++)
	{
		int atom_type = vevo_property_atom_type(values, choices[k]);
		if(atom_type == VEVO_ATOM_TYPE_INT && choices[k][0] =='f')
		{
			int value = 0;
			if(vevo_property_get( values, choices[k],0,&value )==VEVO_NO_ERROR )
			{
				if(value)
				{
					int id = 0;
					if( sscanf( choices[k], "fx_%d",&id ) == 1 )
						return id;
				}
				
			}
		}
		free(choices[k]);
	}
	free(choices);
	return 0;
}

static	char *verify_pulldown(void *values, char *path )
{
	char *my_path = strdup(path);
        char *token   = strtok( my_path, "/");
	char buf[128];
        char *res = NULL;
	sprintf(buf, "%s", "/");
	strcat(buf, token);
        while( (res = strtok(NULL, "/" ) ))
        {
#ifdef STRICT_CHECKING
		assert( strncasecmp(res, "fx_XX",5 ) != 0 );
#endif
                if(strncasecmp( res, "fx_X", 4 ) == 0 )
                {
			int id = guess_suitable_entry( values );
 			char tmp[32];
			sprintf(tmp,"/fx_%d",id);
			strcat( buf, tmp);
 		}
		else
		{
			strcat( buf, "/" );
			strcat( buf, res );
		}
        }
        free(my_path);
	return strdup( buf );
}



void	signal_handler( GtkWidget *widget, gpointer user_data )
{
	director_t *d = (director_t*) user_data;
	if(concretebuilder_islocked(d->stats))
		return;
	
	char *path = builder_get_signaldata(d->stats, widget );
	char *format = builder_get_signalformat(d->stats,widget);
	char *widget_type = builder_get_signalwidget(d->stats,widget);

	if(path)
	{	
		if(strncasecmp( widget_type, "checkbutton",11 ) == 0 )
		{
			int active = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget ) ) ? 1: 0;
			ui_send_osc( d->info, path,"i", active );
		}
		else if(  strncasecmp(widget_type, "vscale",6 ) == 0 ||     
			  strncasecmp(widget_type, "hscale",6 ) == 0 )
		{
			double value = GTK_ADJUSTMENT( GTK_RANGE(widget)->adjustment )->value;
			int ivalue = (int) value;
			if( format[0] == 'i' )
				ui_send_osc( d->info,path,format, ivalue );
			else if( format[0] == 'd' )
				ui_send_osc( d->info,path,format, value );
			else if (format[0] == 'h' )
				ui_send_osc( d->info,path, format,(int64_t) value );
		}
		else if (  strncasecmp(widget_type, "spin",4 ) == 0 )
		{
			double value  = gtk_spin_button_get_value( GTK_SPIN_BUTTON( widget ) );	
			int ivalue = (int) value;

			if(format[0] == 'i' )
			   ui_send_osc( d->info,path,format, ivalue );
			else
			   ui_send_osc( d->info,path,format, value );
		}
		else if ( strncasecmp(widget_type, "button",6 ) == 0 )
		{
			if(strncasecmp(path,"none",4) == 0)
			{
				int	selected_fx = guess_suitable_entry( d->values );
				if(selected_fx>=0)
				{
					char mypath[128]; //@ should take label of button
					sprintf(mypath,"/sample_%d/fx_%d/clear",
						builder_get_default_sample(d->stats),
						selected_fx);
					ui_send_osc(d->info,mypath, NULL);				
				}
			}
			else if( strncasecmp( path, "bind", 4 ) == 0 )
			{
				int	selected_fx = guess_suitable_entry( d->values );
				int     p_id = atoi( path+6);
				ui_send_osc(d->info, "/veejay/bindreq","iii",
						builder_get_default_sample(d->stats),
						selected_fx,p_id);
			}
			else if(strncasecmp( path, "unbind", 6 )== 0 )
			{
				int	selected_fx = guess_suitable_entry( d->values );
				if(selected_fx>=0)
				{
					char mypath[128]; //@ should take label of button
					sprintf(mypath,"/sample_%d/fx_%d/release",
						builder_get_default_sample(d->stats),
						selected_fx);
					int p_id = atoi( path+8);
					ui_send_osc(d->info,mypath, "i", p_id);				
				}
			} else if (strncasecmp( path, "clone",5 ) == 0 )
			{
				ui_send_osc( d->info, "/veejay/clone", "i", builder_get_default_sample(d->stats) );
			}
			else
				ui_send_osc( d->info, path , NULL );
		}
		else if ( strncasecmp(widget_type, "combo", 5 ) == 0 )
		{
			if( strncasecmp( widget_type, "combobox_editlist",14 ) == 0 )
			{
				gchar *text = gtk_combo_box_get_active_text( GTK_COMBO_BOX(widget) );
				int n = 0;
				if(sscanf( text, "sample%d",&n))
				{
					ui_send_osc( d->info, path,"i", n );
					free(text);
					return;
				}
				free(text);
			}
			else if (strncasecmp( widget_type, "combobox_load_ip",14 ) == 0) {
				int id = 0;
			 	sscanf( format, "SampleBind%dFX",&id);
				gchar *text = gtk_combo_box_get_active_text( GTK_COMBO_BOX(widget) );
				if( text[0] == 'F' && text[1] == 'X' )
				{
					int fe = 0;
					sscanf( text,"FX %d",&fe );
					ui_send_osc( d->info, path, "iis", id, fe,format );
					d->right_fx = fe;
				}
				free(text);
			}
			else if (strncasecmp( widget_type+5, "box_list_ip",10 ) == 0) {
				int id = 0;
				int fx = 0;
				int pid = gtk_combo_box_get_active( GTK_COMBO_BOX(widget )) - 1;
				int dummy = 0;
				int n = sscanf(format, "SampleBind%dFX%dOP%d", &id,&fx,&dummy );
				ui_send_osc( d->info, path, "iii", dummy,d->right_fx,pid );
				ui_send_osc( d->info, "/veejay/blreq", "is", id, format );

			} else if (strncasecmp(widget_type+5, "box_release_bind",14 ) == 0 ) {
				gchar *text = gtk_combo_box_get_active_text( GTK_COMBO_BOX(widget) );
				gchar *p_id = strstr(text, "' p");
				if( text[0] == 'f' && text[1] == 'x' )
				{
					int fx= 0;
					int fl = 0;
					int id= 0;
					int op=0;
					int dummy=0;
					int n = sscanf( format,"SampleBind%dFX%dOP%d",&id,&fl,&op );
					    n = sscanf( text,  "fx_%d", &fx );
					    n = sscanf( p_id,  "' p%d '", &p_id );
					ui_send_osc( d->info, path, "iii", op,fx,p_id );
					ui_send_osc( d->info, "/veejay/blreq", "is", id, format );
				}				
			}
			else
			{
			gchar *text = gtk_combo_box_get_active_text( GTK_COMBO_BOX(widget) );
			if( text && strncasecmp( text,"none",4 ) != 0)
			{
				if(format[0] == 's')
				{
					char *new_path = verify_pulldown(d->values, path);
					ui_send_osc( d->info, new_path, "s", text ); 
					free (new_path);
				}
				else
				ui_send_osc( d->info, path, "d", 
						(gdouble) gtk_combo_box_get_active( GTK_COMBO_BOX(widget) ));
			}
			free(text);
			}
		}
		else if (strncasecmp( widget_type, "menuitem",8 ) == 0 )
		{
			printf("menu item clicked\n");
		}
		else if( strncasecmp( widget_type, "radio",5 ) == 0 )
		{
			printf("Radio button clicked\n");
		}
		else
			printf("Dont know how to handle '%s' signals\n", widget_type);
		

		free(path);
		if(format)
			free(format);
	}
	else
	{
		char *widget_type = builder_get_signalwidget(d->stats,widget);
		int active = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget ) );
		vevo_property_set( d->values, widget_type, VEVO_ATOM_TYPE_INT,1, &active);

		if(active)
		{
			// fx_0 is also name of frame send on /create/frame
			char expected_window[128];
			sprintf(expected_window, "Sample%dFX%d",
					builder_get_default_sample( d->stats ),
					atoi( widget_type+3));
			void *exwin = director_window_is_realized( expected_window );
			if(exwin)
			{
				GtkWidget *gexwin = (GtkWidget*) exwin;
				gtk_window_present( GTK_WINDOW( gexwin ));
			}
			else
			{
				ui_send_osc_( ssender_, "/veejay/request", "ii",(int) builder_get_default_sample(d->stats),
					(int) atoi(widget_type+3));
			}
		}
	}
	free(widget_type);
}

static GtkWidget	*get_widget( GladeXML *g, const char *id )
{
	GtkWidget *w = glade_xml_get_widget( g, id );
	if( w== NULL)
	{
		veejay_msg(0,"Widget '%s' not found\n",id);
		return NULL;
	}	
	return w;
}

void		director_set_widget( void *xml, const char *id, int argc, void **dargv )
{
	lo_arg **argv = (lo_arg**) dargv;
	GladeXML *gxml = (GladeXML*) xml;

	GtkWidget *widget = get_widget(gxml, id );
	if(!widget)
	{
		veejay_msg(0,"GladeXML: widget '%s' does not exist\n", id);
	}

	gtk_label_set_justify( GTK_LABEL(widget), GTK_JUSTIFY_RIGHT );
	gtk_label_set_text( GTK_LABEL(widget), &argv[2]->s );
	
}
static void remove_all (GtkComboBox *combo_box)
{
  GtkTreeModel* model;
  model = gtk_combo_box_get_model (combo_box);
  gtk_list_store_clear (GTK_LIST_STORE(model));
}


void		director_update_combobox( void *xml, void *stats, void *info,
			char *id, int argc, void **dargv, int k )
{
	GladeXML *gxml = (GladeXML*) xml;
	GtkWidget *box = get_widget(gxml, id );
	lo_arg **argv = (lo_arg**) dargv;
	concretebuilder_lock(stats);
	remove_all( GTK_COMBO_BOX( box ) );

	int i;
	for ( i = 0; i < (argc-k) ; i ++ )
		gtk_combo_box_append_text( GTK_COMBO_BOX(box), (char*) &argv[k+i]->s );
	gtk_combo_box_set_active( GTK_COMBO_BOX(box), 0 );
	concretebuilder_unlock(stats);
	
}

void		*director_connect_full( void *xml, void *stats, void *info )
{
	GladeXML *gxml = (GladeXML*) xml;
	char **list = builder_get_full( stats );
	int i;

	GtkWidget *win = get_widget( gxml, "window_0" );

	if(!list)
	{
		veejay_msg(0 ,"No signals to connect\n");
		return (void*) win;
	}

	director_t *d = (director_t*) malloc(sizeof(director_t));
	d->stats = stats;
	d->info  = info;
	d->values = vpn( VEVO_ANONYMOUS_PORT );

	for( i = 0; list[i] != NULL ;i ++ )
	{
		char *s = builder_get_signalname( stats,list[i] );
		char *tip = builder_get_tooltip( stats,list[i]);
		int lerror = 0;
		double  dv = 0;
		GtkWidget *w = get_widget( gxml, list[i] );	

		if(strncasecmp( list[i], "combobox",8 ) == 0 )
		{
			dv = builder_get_value_after( stats, list[i],&lerror );
			if( lerror == 0)
				gtk_combo_box_set_active( GTK_COMBO_BOX( w ) , (gint) dv );
		}

		//@ list[i] -> combox -> need to set active value!
		if( builder_register_method( stats, list[i],w, list[i] ) == 0 )
		{
			g_signal_connect( GTK_OBJECT(w),
				  s,
				  signal_handler,
				  d ); 
			if(tip && strncasecmp(tip, "none", 4 ) != 0)
			{
				GtkTooltips *tt = gtk_tooltips_new();
				gtk_tooltips_set_tip( tt,
						GTK_WIDGET(w),
						tip,
						NULL );
				gtk_tooltips_enable( tt );
			}
			if( strncasecmp( list[i], "vscale",6)==0)
			{
				GtkRequisition req;
				gtk_widget_size_request( w, &req );
				gtk_widget_set_size_request( w, req.width, (3*req.height));
			}
			if( strncasecmp( list[i], "button", 6) == 0)
			{
				GtkRequisition req;
				gtk_widget_size_request( w, &req );
				gtk_widget_set_size_request( w, req.width,req.height);
			}
		}
		else
			veejay_msg(0, "\tFailed to register '%s' with widget '%s'\n",s,list[i]);
		
		if(tip) free(tip);
		free(list[i]);
		free(s);
	}
	free(list);
	return (void*) win;
}

static	gboolean	director_delete_window( GtkWidget *widget, GdkEvent *event, gpointer user_data )
{
	director_t *d = (director_t*) user_data;
	director_grab_window_position( widget );
	return FALSE;
}


static	void	director_destroy_window( GtkWidget *widget, gpointer user_data)
{
	director_t *d = (director_t*) user_data;
	osc_cleanup_window( d->info, d->key,
			builder_from_register( d->stats, d->key ),
			d->stats );
	director_unrealize_window( d->info, d->stats, (void*) widget );
}

gint gui_client_event_signal(GtkWidget *widget, GdkEventClient *event,
        void *data)
{
        static GdkAtom atom_rcfiles = GDK_NONE;
        if(!atom_rcfiles)
                atom_rcfiles = gdk_atom_intern("_GTK_READ_RCFILES", FALSE);

        if(event->message_type == atom_rcfiles)
        {
                gtk_rc_parse("/usr/local/share/veejay/gveejay.rc");
                return TRUE;
        }
        return FALSE;
}


static struct
{
        const char *descr;
        const char *filter;
} content_file_filters[] = {
        { "AVI Files (*.avi)", "*.avi", },
        { "Digital Video Files (*.dv)", "*.dv" },
        { "Edit Decision List Files (*.edl)", "*.edl" },
        { "PNG (Portable Network Graphics) (*.png)", "*.png" },
        { "JPG (Joint Photographic Experts Group) (*.jpg)", "*.jpg" },
        { NULL, NULL },
        
};

static void add_file_filters(GtkWidget *dialog )
{
        GtkFileFilter *filter = NULL;
                
        int i;
        for( i = 0; content_file_filters[i].descr != NULL ; i ++ )
        {
                filter = gtk_file_filter_new();
                gtk_file_filter_set_name( filter, content_file_filters[i].descr);
                gtk_file_filter_add_pattern( filter, content_file_filters[i].filter);
                gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter );
        }
        
        filter = gtk_file_filter_new();
        gtk_file_filter_set_name( filter, "All Files (*.*)");
        gtk_file_filter_add_pattern( filter, "*");
        gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter); 
}

gchar 	*dialog_open_sample(const char *title, GtkWidget *parent_window )
{
	static gchar *_file_path = NULL;

        GtkWidget *dialog = 
                gtk_file_chooser_dialog_new( title,
                                parent_window,
                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                NULL);

        add_file_filters(dialog );
        gchar *file = NULL;
        if( _file_path )
        {
                gtk_file_chooser_set_current_folder_uri(GTK_FILE_CHOOSER(dialog), _file_path);  
                g_free(_file_path);
        }

        if( gtk_dialog_run( GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        {
                file = gtk_file_chooser_get_filename(
                                GTK_FILE_CHOOSER(dialog) );
                _file_path = gtk_file_chooser_get_current_folder_uri(GTK_FILE_CHOOSER(dialog));
        }

        gtk_widget_destroy(GTK_WIDGET(dialog));
        return file;
}

void	on_sample_open_clicked(GtkWidget *w, gpointer user_data)
{
	gchar *file = dialog_open_sample( 
			"Add Sample",
			user_data );
	if(file)
	{
		ui_send_osc_(ssender_,
				"/veejay/new", "iis",
				0,0, file );
	}
}

static	gboolean	main_window_delete( GtkWidget *widget, GdkEvent *event, gpointer user_data )
{

	if( prompt_dialog( "Quit GVeejay", "Are you sure ?" ) == GTK_RESPONSE_REJECT)
		       return;	
	return TRUE;
}
static	gboolean	main_window_destroy( GtkWidget *widget, GdkEvent *event, gpointer user_data )
{
	return FALSE;
}


GtkWidget	*director_get_sample_pad(void)
{
	return SamplePad_;
}



void	on_about_clicked(GtkWidget *w, gpointer user_data)
{
    const gchar *authors[] = { 
      "Niels Elburg <nelburg@looze.net>",
      NULL 
    };

	const gchar *license = 
	{
		"Veejay-NG\thttp://veejay.dyne.org\n\n"\
		"This program is Free Software; You can redistribute it and/or modify\n"\
		"under the terms of the GNU General Public License as published by\n" \
		"the Free Software Foundation; either version 2, or (at your option)\n"\
		"any later version.\n\n"\
		"This program is distributed in the hope it will be useful,\n"\
		"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"\
		"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"\
		"See the GNU General Public License for more details.\n\n"\
		"For more information , see also: http://www.gnu.org\n"
	};

	char path[1024];
	bzero(path,1024);
	//get_gd( path, NULL,  "veejay-logo.png" );
//	sprintf(path,"/usr/local/share/veejay/veejay-logo.png");
//	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file
//		( path, NULL );
	GtkWidget *about = g_object_new(
		GTK_TYPE_ABOUT_DIALOG,
		"name", "Veejay-NG Live User Interface",
		"version", VERSION,
		"copyright", "(C) 2004 - 2006 N. Elburg",
		"comments", "A graphical interface for Veejay-NG",
		"authors", authors,
		"license", license,
		NULL,NULL,NULL );
		//"logo", pixbuf, NULL );
	//g_object_unref(pixbuf);

	g_signal_connect( about , "response", G_CALLBACK( gtk_widget_destroy),NULL);
	gtk_window_present( GTK_WINDOW( about ) );

}


void		director_construct_local_widgets(  )
{
	GtkWidget *win = gtk_window_new( GTK_WINDOW_TOPLEVEL );
	gtk_window_set_title( win, "Loading" );
	gtk_window_set_resizable( win, FALSE );
	
	g_signal_connect( G_OBJECT(win), "delete_event",
				G_CALLBACK( main_window_delete ), NULL );
	g_signal_connect( G_OBJECT(win), "destroy",
				G_CALLBACK( main_window_destroy ), NULL );

	gtk_container_set_border_width( GTK_CONTAINER( win ), 1 );
	
	GtkWidget *box = gtk_vbox_new( FALSE, 0 );
	gtk_container_add( GTK_CONTAINER( win ), box );

	GtkWidget *hbox = gtk_hbox_new( FALSE,0 );
	gtk_box_pack_end_( box, hbox, FALSE,FALSE, 0 );
	
	GtkWidget *bbox = gtk_hbox_new( FALSE,0 );
	gtk_box_pack_end_( box, bbox, FALSE,FALSE, 0 );


/*	GtkWidget *sample_buttons = gtk_frame_new(NULL);
	GtkWidget *fbox = gtk_vbox_new( TRUE, 0 );
	gtk_box_pack_end_( box, fbox, FALSE,FALSE, 0);
	GtkWidget *but = gtk_button_new_with_label( "New Sample" );
	g_signal_connect( but, "clicked", (GCallback) on_sample_open_clicked, win );
	gtk_box_pack_end_( fbox, but, FALSE,FALSE, 0);
	but = gtk_button_new_with_label( "Load Samples" );
	gtk_box_pack_end_( fbox, but, FALSE, FALSE,0);
	but = gtk_button_new_with_label( "Save Samples" );
	gtk_box_pack_end_( fbox, but, FALSE, FALSE,0 );
	
	GtkWidget *sample_box = gtk_frame_new( NULL );
	gtk_box_pack_end_( box, sample_box, FALSE,FALSE,0);
	GtkWidget *pad = (GtkWidget*) samplebank_new(ssender_,10,2 );
		samplebank_add_page(pad);
	gtk_box_pack_end_( sample_box, pad,FALSE,FALSE,0);
*/
	GtkWidget *pad = (GtkWidget*) samplebank_new(ssender_,10,2 );
	samplebank_add_page(pad);
	gtk_box_pack_end_( hbox, pad, FALSE,FALSE,0 );

	GtkWidget *frame = gtk_frame_new(NULL);
	GtkWidget *mbox = gtk_vbox_new(FALSE,0 );
	gtk_container_add( GTK_CONTAINER(frame), mbox );
	GtkWidget *but = gtk_button_new_with_label( "New Sample" );
	gtk_box_pack_end_( mbox, but, FALSE,FALSE, 0);
	but = gtk_button_new_with_label( "Load Samples" );
	gtk_box_pack_end_( mbox, but, FALSE,FALSE, 0);
	but = gtk_button_new_with_label( "Save Samples" );
	gtk_box_pack_end_( mbox, but, FALSE,FALSE, 0);
	but = gtk_button_new_with_label( "About" );
	g_signal_connect( but, "clicked", (GCallback) on_about_clicked, win );

	gtk_box_pack_end_( mbox, but, FALSE,FALSE, 0);

	gtk_box_pack_end_( hbox, frame, TRUE,TRUE, 0 );
	gtk_widget_show(frame);
	gtk_widget_show( bbox );	
	gtk_widget_show_all( box );
	gtk_widget_show_all( win );


	MainWindow_ = win;
	MainBox_    = box;
	MainSubBox_	= hbox;
	MainBotBox_	= bbox;

	SamplePad_  = pad;
	GtkRequisition req;
	gtk_widget_size_request( win, &req );
	gint wid = req.width;
	gint hei = req.height;
	int x = (screen_width_/2) - (wid/2);
	int y = (screen_height_ - hei - 100);

	gtk_window_move( win, x,y );
	main_panel_y_ = y - 5;
	main_panel_x_ = 0;
	current_y_ = main_panel_y_; 
	current_x_ = 0;

	//printf("Window at %d x %d , size %dx%d\n", x,y,wid,hei);	
//	anim_ui_new(ssender_, "/sample/test/curve", "d");

}

void		director_reload_lists()
{
	if(!PlayList_ || !EditList_)
		return;
	
	samplebank_store_in_combobox( PlayList_ );
	samplebank_store_in_combobox( EditList_ );
}

void	playlist_click( GtkWidget *w, gpointer d)
{
	if(skip_update_)
		return;
	
	gchar *text = gtk_combo_box_get_active_text(w);
	if(text)
	{
		int id = 0;
		sscanf( text, "Sample %d", &id);
		ui_send_osc_( sender_, "/veejay/select", "i", id );
		g_free(text);
	}	
}

void	editlist_click( GtkWidget *w, gpointer d)
{
	if(skip_update_)
		return;
	gchar *text = gtk_combo_box_get_active_text(w);
	if(text)
	{
		int id = 0;
		sscanf( text, "Sample %d", &id);
		ui_send_osc_( sender_, "/veejay/show", "i", id );
		g_free(text);
	}	
}	


void		director_instantiate_template(void *gxml, void *mainw, void *stats,
		void *template )
{
	GtkWidget *w = (GtkWidget*) mainw;
	GtkWidget *box = get_widget( gxml, "vbox_0" );
	
	int n_elem = vevo_property_num_elements( template, "string_list" );
	char **strs = NULL;
	int i;

	strs = (char**) malloc(sizeof(char*) * n_elem );
	memset( strs,0, sizeof(char*) * n_elem );
	

	for( i = 0; i < n_elem; i ++ )
	{
		int len = vevo_property_element_size( template, "string_list",i );
		strs[i] = (char*) malloc(sizeof(char) * len );
		vevo_property_get( template, "string_list", i, &strs[i]);
	}
	char *osc_path = vevo_property_get_string( template, "osc_path" );
	
	void  *custom_widget = channelbank_new( sender_,
					osc_path,
					n_elem,
					strs );

	GtkWidget *ui_widget = channelbank_get_container( custom_widget );

	vevo_property_set( template, "widget", VEVO_ATOM_TYPE_VOIDPTR,
				1, &custom_widget );

	free(osc_path);
	
	for( i =0 ; i < n_elem ; i ++ )
		free(strs[i]);
	free(strs);
	
	gtk_box_pack_end_defaults( box, ui_widget );
}
static	void	director_window_placement( void *gxml, GtkWidget *w, const char *name )
{
	GtkRequisition req;
	gtk_widget_size_request( w, &req );
	int wid = req.width;
	int hei = req.height;
	int x,y,wi,he;

	char xkey[32],ykey[32],wkey[32],hkey[32];
	
#ifdef STRICT_CHECKING
	assert( strlen(name) < 32 );
#endif
	sprintf(xkey,"%s_x",name);
	sprintf(ykey,"%s_y",name);
	sprintf(wkey,"%s_w",name);
	sprintf(hkey,"%s_h",name);

	char *fx = strstr( name, "FX" );
	int error = vevo_property_get(windowport_, xkey,0,&x);
	if( error == VEVO_NO_ERROR )
		error = vevo_property_get(windowport_,ykey,0,&y);

	int done = 0;
	if( error == VEVO_NO_ERROR )
	{
		gtk_window_move(w,x,y);
		done = 1;
		if(vevo_property_get(windowport_, wkey,0,&wi)==VEVO_NO_ERROR)
		{
			vevo_property_get(windowport_,hkey,0,&he);
			gtk_window_resize( w, wi, he );
			done =2;
		}
	}
	
	if(fx)
	{
		fxcurrent_y_ = (screen_height_/5)*2 + (0.135 * screen_height_);
		x = fxcurrent_x_ + 15;
		y = fxcurrent_y_;
		if( (x + wid) < screen_width_ )
			fxcurrent_x_ = (x + wid);
		else
			fxcurrent_x_ = 0;
		y -= hei;
		if(done<=1)
			gtk_window_resize( w, wid, hei);
	}
	else
	{
		current_y_ = main_panel_y_;
		x = current_x_;
		y = current_y_;
		if( (x + wid) < screen_width_)
			current_x_ = (x + wid);
		else
			current_x_ = 0;
		y -= hei;
	}
		
	if(done==0)
		gtk_window_move( w, x, y );

}

static	void	director_append_widgets( void *gxml, GtkWidget *w )
{
	GtkWidget *box22 = get_widget( gxml, "vbox_0" );
	gtk_widget_reparent( box22, MainBox_ );
	gchar *title = gtk_window_get_title(GTK_WINDOW(w ) );
	gtk_window_set_title( MainWindow_, title );
	gtk_widget_destroy( w );
	
	GtkWidget *box = MainBotBox_;
	GtkWidget *hbox = gtk_hbox_new( FALSE,0 );
	GtkWidget *playlist = gtk_combo_box_new_text();
	GtkWidget *editlist = gtk_combo_box_new_text();

	GtkWidget *label1   = gtk_label_new( "Play" );
	GtkWidget *label2   = gtk_label_new( "Edit" );

	GtkWidget *hbox1 = gtk_hbox_new( FALSE,10 );
	GtkWidget *hbox2 = gtk_hbox_new( FALSE,10 );

	gtk_box_pack_end_( hbox1, editlist,FALSE,FALSE,0 );
	gtk_box_pack_end_( hbox1, label2, FALSE,FALSE,0);
	gtk_box_pack_end_( hbox2, playlist,FALSE,FALSE,0 );
	gtk_box_pack_end_( hbox2, label1, FALSE,FALSE,0);
	
	g_signal_connect( playlist, "changed", (GCallback) playlist_click, NULL );
	g_signal_connect( editlist, "changed", (GCallback) editlist_click, NULL );

	gtk_box_pack_end_( box, hbox, FALSE,FALSE,0 );
	gtk_box_pack_end_( hbox, hbox1,FALSE,FALSE,0 );
	gtk_box_pack_end_( hbox, hbox2,FALSE,FALSE,0 );
	gtk_widget_show( label1 );
	gtk_widget_show( label2 );
	gtk_widget_show_all( hbox );	

	PlayList_ = playlist;
	EditList_ = editlist;
	veejay_msg(0, "Appended local widgets");
	director_reload_lists( );
}


void		director_finish( void *gxml, void *mainw, void *stats, void *info,
	       const char  *name	)
{
	GtkWidget *w = (GtkWidget*) mainw;
	director_t *d = (director_t*) malloc(sizeof(director_t));
	memset(d,0,sizeof(director_t));
	d->stats = stats;
	d->info  = info;
	d->key   = strdup(name);

	g_assert( GTK_IS_WIDGET(w) );

        g_signal_connect_after( GTK_OBJECT(w), "client_event",
                GTK_SIGNAL_FUNC( G_CALLBACK(gui_client_event_signal) ), NULL );
	
    	gtk_container_set_border_width (GTK_CONTAINER (w), 10);

	if( strncasecmp(name,"MainWindow",6)==0)
        {
		director_append_widgets( gxml, w );	
	}
	else
	{
		g_signal_connect (G_OBJECT (w), "delete_event",
                      G_CALLBACK (director_delete_window), d);
			
    		g_signal_connect (G_OBJECT (w), "destroy",
       	               G_CALLBACK (director_destroy_window), d);
	

		director_window_placement( gxml,w,name );
	}	


	
}

void 	*director_window_is_realized( const char *name)
{
	void *result = NULL;
	int error =
		vevo_property_get( windowport_, name, 0,&result );
	if( error == VEVO_NO_ERROR )
		return result;
	return NULL;
}

void	director_realize_window( const char *name , void *ptr)
{
	int error =
		vevo_property_set( windowport_, name, VEVO_ATOM_TYPE_VOIDPTR,1,&ptr );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	char widget_name[32];
	sprintf(widget_name,"%p", ptr );
	error = vevo_property_set( widgetport_, widget_name, VEVO_ATOM_TYPE_STRING,1,&name );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

}
void	director_grab_window_position( void *ptr )
{
	GtkWidget *test= ptr;
	int x,y,w,h;
	gdk_window_get_origin( GDK_WINDOW( test->window),&x,&y);
	GtkRequisition req;
	gtk_widget_size_request( test, &req );
	w=req.width;
	h=req.height;
	char widget_name[32];
	sprintf(widget_name,"%p",ptr );
	char *key = vevo_property_get_string( widgetport_, widget_name);
	if(key)
	{
		char sizekey[32];
		sprintf(sizekey,"%s_x", key);
		int error = vevo_property_set( windowport_, sizekey, VEVO_ATOM_TYPE_INT,1,&x);
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		sprintf(sizekey,"%s_y", key);
		error = vevo_property_set( windowport_, sizekey, VEVO_ATOM_TYPE_INT,1,&y);
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif

		sprintf(sizekey, "%s_w", key );
		error = vevo_property_set( windowport_, sizekey, VEVO_ATOM_TYPE_INT,1,&w);
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif

		sprintf(sizekey, "%s_h", key );
		error = vevo_property_set( windowport_, sizekey, VEVO_ATOM_TYPE_INT,1,&h);
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		veejay_msg(0, "%s is %d x %d at pos %d x %d", key, x,y,w,h);
		
	}
}

static int prompt_dialog(const char *title, char *msg)
{
        GtkWidget *mainw = MainWindow_;
        GtkWidget *dialog = gtk_dialog_new_with_buttons( title,
                                GTK_WINDOW( mainw ),
                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                GTK_STOCK_NO,
                                GTK_RESPONSE_REJECT,
                                GTK_STOCK_YES,  
                                GTK_RESPONSE_ACCEPT,
                                NULL);
        gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_REJECT );
        gtk_window_set_resizable( GTK_WINDOW(dialog), FALSE );
        g_signal_connect( G_OBJECT(dialog), "response", 
                G_CALLBACK( gtk_widget_hide ), G_OBJECT(dialog ) );
        GtkWidget *hbox1 = gtk_hbox_new( FALSE, 12 );
        gtk_container_set_border_width( GTK_CONTAINER( hbox1 ), 6 );
        GtkWidget *icon = gtk_image_new_from_stock( GTK_STOCK_DIALOG_QUESTION,
                GTK_ICON_SIZE_DIALOG );
        GtkWidget *label = gtk_label_new( msg );
        gtk_container_add( GTK_CONTAINER( hbox1 ), icon );
        gtk_container_add( GTK_CONTAINER( hbox1 ), label );
        gtk_container_add( GTK_CONTAINER( GTK_DIALOG( dialog )->vbox ), hbox1 );
        gtk_widget_show_all( dialog );

        int n = gtk_dialog_run(GTK_DIALOG(dialog));

        gtk_widget_destroy(dialog);
        return n;
}

void	director_unrealize_window(void *info, void *stats, void *ptr )
{
	char widget_name[32];
	sprintf(widget_name, "%p", ptr );
	char *name = vevo_property_get_string( widgetport_, widget_name );
#ifdef STRICT_CHECKING
	if(name==NULL)
		printf("Cannot find widget '%s'\n", widget_name);
	assert( name != NULL );
#endif

	if(name)
	{
		vevo_property_set( windowport_, name, VEVO_ATOM_TYPE_VOIDPTR,0,NULL );
		vevo_property_set( widgetport_, widget_name, VEVO_ATOM_TYPE_STRING,0,NULL);

		char *builder = builder_from_register( stats, name );
		if( builder)
			builder_destroy_rootnode(stats, builder );
		
		builder_free( stats );

		osc_delete_window( info, name );
	}
}

static	gboolean	tick_veejay(gpointer data)
{
	ui_send_osc_( data, "/veejay/tick", NULL,NULL );
	return TRUE;
}


void		director_init(const char *addr, const char *port)
{
	directors_ = vpn( VEVO_ANONYMOUS_PORT );
	screen_width_  = gdk_screen_width();
	screen_height_ = gdk_screen_height();
	windowport_ = vpn( VEVO_ANONYMOUS_PORT );
	widgetport_ = vpn( VEVO_ANONYMOUS_PORT );
	gtk_rc_parse("/usr/local/share/veejay/gveejay.rc");

	ssender_ = ui_new_osc_sender( addr, port );
	sender_ = ui_new_osc_sender( addr, port );
	veejay_tick_ = g_timeout_add( 1000, tick_veejay, (gpointer) sender_ );
	if(ui_send_osc_( sender_, "/veejay/ui", NULL,NULL ) <= 0 )
		veejay_msg(0, "Error sending /veejay/ui to %s %s", addr,port );

	//anim_ui_collection_init();


	director_construct_local_widgets( );
}



void		director_free()
{
	vevo_port_free( windowport_ );
	ui_free_osc_sender(sender_);
	ui_free_osc_sender(ssender_);
}
