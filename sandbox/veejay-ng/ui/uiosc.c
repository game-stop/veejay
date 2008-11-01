/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
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
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <glib.h>
#include <glade/glade.h>
#include <libvevo/libvevo.h>
#include <ui/builder.h>
#include <ui/director.h>
#include <lo/lo.h>

#ifdef STRICT_CHECKING
#include <assert.h>
#endif
typedef struct
{
        lo_address addr;
        char *addr_str;
        char *port_str;
} oscclient_t;


typedef struct
{
	lo_server		st;
	void			*tree;
	void			*sender;
	int			softlock;
	void			*template;
} osc_recv_t;

void	*ui_new_osc_sender_uri( const char *uri )
{
	oscclient_t *osc = (oscclient_t*) vj_malloc(sizeof(oscclient_t));
	memset(osc,0,sizeof(oscclient_t));
	osc->addr = lo_address_new_from_url( uri );

	osc->addr_str = lo_address_get_hostname( osc->addr );
	osc->port_str = lo_address_get_port ( osc->addr );
	veejay_msg(0,"New OSC sender from uri '%s', Host %s, Port %s",
			uri, osc->addr_str, osc->port_str );
	return (void*) osc;
}

void    *ui_new_osc_sender( const char *addr, const char *port )
{
        oscclient_t *osc = (oscclient_t*) malloc(sizeof(oscclient_t));
        osc->addr = lo_address_new( addr, port );

        osc->addr_str = strdup( addr );
        osc->port_str = port ? strdup( port ) : NULL;

        return (void*) osc;
}

void    ui_free_osc_sender( void *dosc )
{
        oscclient_t *osc = (oscclient_t*) dosc;

        lo_address_free( osc->addr );
        if( osc->port_str )
                free( osc->port_str);
        free(osc->addr_str);
        free(osc);
        osc = NULL;
}
static  void veejay_add_arguments_ ( lo_message lmsg, const char *format, va_list ap )
{
        //http://liblo.sourceforge.net/docs/group__liblolowlevel.html#g31ac1e4c0ec6c61f665ce3f9bbdc53c3
   	while( *format != 'x' && *format != '\0' )
        {
                switch(*format)
                {
                        case 'i':
				{ int32_t val =  (int32_t) va_arg( ap, int32_t);
                                lo_message_add_int32( lmsg,val);
				}
                                break;
                        case 'h':
                                lo_message_add_int64( lmsg, (int64_t) va_arg( ap, int64_t));
                                break;
                        case 's':
                                lo_message_add_string( lmsg, (char*) va_arg( ap, char*) );
                                break;
                        case 'd':
                                lo_message_add_double( lmsg, (double) va_arg(ap, double));
                                break;
                        default:
                                break;
                }
                *format ++;
        }
}

int     ui_send_osc( void *info ,const char *msg, const char *format,  ... )
{
	osc_recv_t *d = (osc_recv_t*) info;

	oscclient_t *c = d->sender;
	if(!c->addr)
	{
		veejay_msg(0,"OSC sender not ready\n");
#ifdef STRICT_CHECKING
		assert(0);
#endif
		return 0;
	}
	lo_message lmsg = lo_message_new();
        if( format )
	{
        	va_list ap;
       		va_start( ap, format );
       		veejay_add_arguments_( lmsg, format, ap );
       		va_end(ap);
	}

        int result = lo_send_message( c->addr, msg, lmsg );
        lo_message_free( lmsg );
	return result;
}	
int     ui_send_osc_( void *info ,const char *msg, const char *format,  ... )
{
	oscclient_t *c = (oscclient_t*) info;
	if(!c->addr)
	{
		veejay_msg(0,"OSC sender not ready\n");
		return 0;
	}
	lo_message lmsg = lo_message_new();
        if( format )
	{
        	va_list ap;
       		va_start( ap, format );
       		veejay_add_arguments_( lmsg, format, ap );
       		va_end(ap);
	}

        int result = lo_send_message( c->addr, msg, lmsg );
        lo_message_free( lmsg );
	return result;
}

int	send_once(const char *addr,
	       const char *port	)
{
	void *sender = ui_new_osc_sender( addr,port );
	if(!sender)
		return 0;
	if(ui_send_osc_( sender, "/veejay/ui", NULL ) <= 0 )
		veejay_msg(0, "Error sending /veejay/ui to %s %s", addr,port );

	ui_free_osc_sender(sender);
	return 1;
}

int	request_window(const char *addr,
	       const char *port, int sample, int entry	)
{
	void *sender = ui_new_osc_sender( addr,port );
	if(!sender)
		return 0;
	if(ui_send_osc_( sender, "/veejay/request", "ii",sample, entry ) <= 0 )
		veejay_msg(0, "Error sending /veejay/ui to %s %s", addr,port );

	ui_free_osc_sender(sender);
	return 1;
}






void	error_handler( int num, const char *msg, const char *path )
{
	veejay_msg(0,"Liblo server error %d in path %s: %s\n",num,path,msg );
}

void	osc_new_veejay_sender(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	void *stats = NULL;
	osc_recv_t *info = (osc_recv_t*) ptr;
  	lo_address a = lo_message_get_source(data);
        char *uri = lo_address_get_url(a);

	if(info->sender)
		ui_free_osc_sender( info->sender );
	info->sender = ui_new_osc_sender_uri( uri );
	free(uri);
}


void	osc_new_window(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	char *window_name = (char*) &argv[0]->s;
	char *window_label = (char*) &argv[1]->s;
	//@ check if window doesnt already exist
	void *wt = NULL;
	if( vevo_property_get( info->tree, window_name,0,&wt ) == VEVO_NO_ERROR )
	{
		veejay_msg(0,"A window with the name '%s' already exists\n", window_name);
	
		void *exwin = director_window_is_realized( window_name );
#ifdef STRICT_CHECKING
		assert( exwin != NULL );
#endif
		if(exwin)
		{
			GtkWidget *gexwin = (GtkWidget*)exwin;
			gtk_window_present( GTK_WINDOW(gexwin) );
		}

		return;
	}

	void *stats  = builder_init();

	vevo_property_set( info->tree, window_name, VEVO_ATOM_TYPE_VOIDPTR,1,&stats);
	
	void *window = builder_new_window( stats, window_label );
	void *box    = builder_new_box( stats, window, VBOX );
	
	char  box_name[128];
	sprintf(box_name,"%s_box", window_name );
	
	builder_register_widget(
			stats, window_name, window );

	builder_register_widget(
			stats, box_name, box );

}
void	osc_cleanup_window( void *ptr, const char *window_name, void *window, void *stats )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
//	builder_destroy_rootnode( stats,window );
//	builder_free(stats);
//	stats = NULL;
//	vevo_property_set( info->tree, window_name, VEVO_ATOM_TYPE_VOIDPTR, 0, NULL );

}

void	osc_delete_window( void *ptr, const char *name )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	vevo_property_set( info->tree, name, 
					VEVO_ATOM_TYPE_VOIDPTR,0,NULL);

	if( info->template )
	{
		void *custom_widget = NULL;
		int error = vevo_property_get( info->template,
				"widget",0,&custom_widget );
		if(custom_widget && error == VEVO_NO_ERROR)
			channelbank_free( custom_widget );
		vevo_port_free( info->template );
		info->template = NULL;
	}
	
}

void	osc_destroy_window(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	
	char *window_name = &argv[0]->s;
	void *stats = NULL;
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *window = builder_from_register( stats, window_name );
		if(!window)
		{
			veejay_msg(0,"Window '%s' does not exist\n", window_name);
			return;
		}

		GladeXML *xml = builder_glade_from_register( stats, window_name );

		char     *id  = builder_get_identifier( stats, window );
		
		GtkWidget *w = glade_xml_get_widget( xml, id );
		if(w) 
		{
			gtk_widget_destroy( w );
		//	builder_destroy_rootnode(stats,window);
		//	builder_free( stats );
			//@ free stats
			vevo_property_set( info->tree, window_name, 
					VEVO_ATOM_TYPE_VOIDPTR,0,NULL);
		}
		free(id);
	}
	else
		veejay_msg(0,"Cannot find window '%s'\n",window_name);
}

void	osc_new_widget_template(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	char *window_name = (char*) &argv[0]->s;
	char *osc_path = (char*) &argv[1]->s;
	int   n        = argv[2]->i; // number of names
	//@ check if window doesnt already exist
	void *wt = NULL;

	if( vevo_property_get( info->tree, window_name,0,&wt ) == VEVO_NO_ERROR )
	{
		if(info->template == NULL )
			info->template = vpn(VEVO_ANONYMOUS_PORT);		

		int i;
		char **strs = (char**) malloc(sizeof(char*) * (n+1));
		memset(strs,0,sizeof(char*) * (n+1));
		for( i = 0; i < n; i ++ )
		{
			strs[i] = strdup( (char*) &argv[i+3]->s );
		}
		int error = vevo_property_set( info->template, "string_list",VEVO_ATOM_TYPE_STRING, n, strs );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		error = vevo_property_set( info->template, "osc_path", VEVO_ATOM_TYPE_STRING, 1, &osc_path );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		for ( i = 0; i < n ; i ++ )
			free(strs[i]);
		free(strs);		
	}
}

void	osc_realize_window(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	char *window_name = &argv[0]->s;
	void *stats = NULL;
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *window = builder_from_register( stats, window_name );
		if(!window)
		{
			veejay_msg(0,"Window '%s' does not exist\n", window_name);
			return;
		}
	
		void *exwin = director_window_is_realized( window_name );
		if(exwin)
		{
			GtkWidget *gexwin = (GtkWidget*)exwin;
			gtk_window_present( GTK_WINDOW(gexwin) );
			return;
		}

		
		int size = 0;
		char *data = builder_formalize( stats,window, &size );
		
		if(!data || size <= 0 )
		{
			veejay_msg(0,"Nothing to formalize\n");
		}
	//	builder_writefile( stats, "glade-dump-window.xml" );
		//@ wipe xml

		GladeXML *gxml = glade_xml_new_from_memory( data, size, NULL,NULL );
		void *mainw = NULL;
		if(gxml)
		{
			mainw = director_connect_full( gxml, stats,info );
			builder_register_glade( stats, window_name, gxml );
			director_finish( gxml,mainw,stats,info,window_name );
		
			if(info->template)
				director_instantiate_template( gxml, mainw,stats, info->template );

		}

		if(!exwin && mainw)
			director_realize_window( window_name, mainw );

	}
}

void	osc_new_box(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;

	char *window_name    = (char*) &argv[0]->s;
	char *container_name = (char*) &argv[1]->s;
	char *my_name        = (char*) &argv[2]->s;
	int   type	     = argv[3]->i;
	
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *container = builder_from_register( stats, container_name );
		if(!container)
		{
			veejay_msg(0,"%s Container '%s' not in register of Window %s\n", __FUNCTION__ , container_name, window_name);
			return;
		}

		void *box       = builder_new_box( stats, container, 
				( type == 0  ? HBOX : VBOX ) );
		builder_register_widget( stats, my_name, box );
	}
	else
	{
		veejay_msg(0,"%s: cannot get '%s\n", __FUNCTION__, window_name );
	}

}

void	osc_new_vframe(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;

	char *window_name    = (char*) &argv[0]->s;
	char *container_name = (char*) &argv[1]->s;
	char *my_name        = (char*) &argv[2]->s;
	char *label          = (char*) &argv[3]->s;

	char frame_name[128];
	sprintf(frame_name, "%s_frame", my_name );
	
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *container = builder_from_register( stats, container_name );
		if(!container)
		{
			veejay_msg(0,"%s Container '%s' not in register of Window %s\n", __FUNCTION__ , container_name, window_name);
			return;
		}
	
		void *panel = builder_new_frame( stats, container, label, HBOX, frame_name,1 );
		builder_register_widget( stats, my_name, panel );
	}
	else
	{
		veejay_msg(0,"%s: cannot get '%s\n", __FUNCTION__, window_name );
	}
}
void	osc_new_frame(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;

	char *window_name    = (char*) &argv[0]->s;
	char *my_name        = (char*) &argv[1]->s;
	char *label          = (char*) &argv[2]->s;

	char container_name[128];
	sprintf(container_name, "%s_box", window_name );

	char frame_name[128];
	sprintf(frame_name, "%s_frame", my_name );
	
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *container = builder_from_register( stats, container_name );
		if(!container)
		{
			veejay_msg(0,"%s Container '%s' not in register of Window %s\n", __FUNCTION__ , container_name, window_name);
			return;
		}
	
		void *panel = builder_new_frame( stats, container, label, HBOX, frame_name,0 );
		builder_register_widget( stats, my_name, panel );
	}
	else
	{
		veejay_msg(0,"%s: cannot get '%s\n", __FUNCTION__, window_name );
	}
}

void	osc_new_switch(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;

	char *window_name = &argv[0]->s;
	char *panel_name = &argv[1]->s;
	char *widget_name = &argv[2]->s;
	char *label      = &argv[3]->s;
	int   active     = argv[4]->i;
	char *osc_path   = &argv[5]->s;
	char *tooltip    = &argv[6]->s;

	int  widget_type = builder_get_widget_type( widget_name );
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *container = builder_from_register( stats, panel_name );
		if(!container)
		{
			veejay_msg(0,"Widget '%s' is not in register!\n", panel_name);
			return;
		}
		veejay_msg(0,"WIDGET %s is %d\n", label, active );
		builder_new_boolean_object( stats, container, label, active, widget_type, osc_path ,tooltip);	
	}
	else
	{
		veejay_msg(0,"%s: cannot get '%s\n", __FUNCTION__, window_name );
	}

}

void	osc_new_label(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;

	char *window_name = &argv[0]->s;
	char *panel_name = &argv[1]->s;
	char *label_name      = &argv[2]->s;
	char *label = &argv[3]->s;

	int  widget_type = LABEL;
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *container = builder_from_register( stats, panel_name );
		if(!container)
		{
			veejay_msg(0,"Widget '%s' is not in register!\n", panel_name);
			return;
		}
		builder_new_label_object( stats, container, label, label_name );	
	//	builder_register_widget(
	//		stats, window_name, window );
	}
	else
	{
		veejay_msg(0,"%s: cannot get '%s\n", __FUNCTION__, window_name );
	}
}

void	osc_new_button(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;

	char *window_name = &argv[0]->s;
	char *panel_name = &argv[1]->s;
	char *label      = &argv[2]->s;
	char *osc_path   = &argv[3]->s;
	char *tooltip    = &argv[4]->s;
	int  widget_type = BUTTON;
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *container = builder_from_register( stats, panel_name );
		if(!container)
		{
			veejay_msg(0,"Widget '%s' is not in register!\n", panel_name);
			return;
		}
		builder_new_button_object( stats, container, label, widget_type, osc_path,tooltip );
	}
	else
	{
		veejay_msg(0,"%s: cannot get '%s\n", __FUNCTION__, window_name );
	}
}

void	osc_new_radio_group( const char *path, const char *types,
	lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;
	int i;
	char *window_name  = &argv[0]->s;  // window 
	char *panel_name   = &argv[1]->s;  // which frame
	char *prefix	   = &argv[2]->s;  // identifier prefix
	char *label_prefix = &argv[3]->s;  // label prefix
	int   n_buttons    = argv[4]->i;   // number of buttons in group
	int   active_button = argv[5]->i;  // active button
	if(n_buttons <= 0 )
	{
		veejay_msg(0,"Empty radio group!\n");
		return;
	}
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *container = builder_from_register( stats, panel_name );
		if(!container)
		{
			veejay_msg(0,"No such panel: %s\n",panel_name);
			return;
		}
		builder_new_boolean_group_object(
				stats,
				container,
				prefix,
				label_prefix,
				active_button, 
				n_buttons );
	}
}
void	osc_new_fxpulldown(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;
	if( argc <= 5 )
	{
		veejay_msg(0,"Insufficient arguments\n");
		return;
	}

	if (strncmp( types, "ssssss", 6 ) != 0 )
	{
		veejay_msg(0,"%s Invalid format: '%s' \n",__FUNCTION__, types );
		return;
	}

	char *window_name = &argv[0]->s;
	char *panel_name = &argv[1]->s;
	char *label      = &argv[2]->s;
	char *osc_path   = &argv[3]->s;
	char *format     = &argv[4]->s;
	char *tooltip    = &argv[5]->s;
	double dv = 0.0;	
	int   n_items    = argc - 6;
	char **items     = NULL;
	if(n_items <= 0 )
	{
		veejay_msg(0,"Empty pulldown!\n");
		return;
	}
	
	items = (char**)malloc(sizeof(char*) * n_items );
	int i;
	for(i = 0; i < n_items; i ++ )
		items[i] = strdup( (char*) &argv[(i+6)]->s );

	
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		int  widget_type = COMBOBOX;
		void *container = builder_from_register( stats, panel_name );
		if(!container)
		{
			veejay_msg(0,"Widget '%s' is not in register!\n", panel_name);
			return;
		}
		builder_new_pulldown_object( stats, container, label,NULL, COMBOBOX, n_items, items, osc_path,format,dv,tooltip );
	}
	else
	{
		veejay_msg(0,"%s: cannot get '%s\n", __FUNCTION__, window_name );
	}

	for(i = 0; i < n_items; i ++ )
		free(items[i]);
	free(items);
}
void	osc_new_pulldown(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;

	if( argc <= 6 )
	{
		veejay_msg(0,"Insufficient arguments\n");
		return;
	}

	if (strncmp( types, "sssssds", 7 ) != 0 )
	{
		veejay_msg(0,"%s Invalid format: '%s' \n",__FUNCTION__, types );
		return;
	}

	char *window_name = &argv[0]->s;
	char *panel_name = &argv[1]->s;
	char *identifier = &argv[2]->s;
	char *label      = &argv[3]->s;
	char *osc_path   = &argv[4]->s;
	double row       = argv[5]->d;
	char *tooltip    = &argv[6]->s;

	
#ifdef STRICT_CHECKING
	assert( window_name != NULL );
	assert( panel_name != NULL );
	assert( label != NULL );
	assert( osc_path != NULL );
	assert( tooltip != NULL );
#endif	
	int   n_items    = argc - 7;
	char **items     = NULL;
	if(n_items <= 0 )
	{
		veejay_msg(0,"Empty pulldown!\n");
		return;
	}
	
	items = (char**)malloc(sizeof(char*) * n_items );
	int i;
	for(i = 0; i < n_items; i ++ )
		items[i] = strdup( (char*) &argv[(i+7)]->s );
	
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		int  widget_type = COMBOBOX;
		void *container = builder_from_register( stats, panel_name );
		if(!container)
		{
			veejay_msg(0,"Widget '%s' is not in register!\n", panel_name);
			return;
		}
		builder_new_pulldown_object( stats, container, label,identifier,COMBOBOX, n_items, items, osc_path,window_name, row,tooltip );
	}
	else
	{
		veejay_msg(0,"%s: cannot get '%s\n", __FUNCTION__, window_name );
	}

	for(i = 0; i < n_items; i ++ )
		free(items[i]);
	free(items);
}

void	osc_update_pulldown(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;

	if( argc <= 5 )
	{
		veejay_msg(0,"Insufficient arguments\n");
		return;
	}

	if (strncmp( types, "sssssds", 7 ) != 0 )
	{
		veejay_msg(0,"%s Invalid format: '%s' \n",__FUNCTION__, types );
		return;
	}

	char *window_name = &argv[0]->s;
	char *panel_name = &argv[1]->s;
	char *widget_id  = &argv[2]->s;
	char *new_label  = &argv[3]->s;
	double selected_fx_id       = argv[4]->d;
	char *tooltip    = &argv[5]->s;
#ifdef STRICT_CHECKING
	assert( window_name != NULL );
	assert( panel_name != NULL );
	assert( new_label != NULL );
#endif	
	int   n_items    = argc - 6;
	char **items     = NULL;
	if(n_items <= 0 )
	{
		veejay_msg(0,"Empty pulldown!\n");
		return;
	}
	
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		GladeXML *xml = builder_glade_from_register( stats, window_name );
		//char     *id  = builder_get_identifier( stats, wid );
		director_update_combobox( xml, stats, info,widget_id, argc, argv, 6 );	
	}
}


void	osc_new_numeric(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	void *stats = NULL;
	char *window_name = &argv[0]->s;
	char *panel_name = &argv[1]->s;
	char *label      = &argv[2]->s;
	double min       = argv[3]->d;
	double max	 = argv[4]->d;
	double value     = argv[5]->d;
	int    digits    = argv[6]->i;
	int    extra     = argv[7]->i;
	char *widgetname = &argv[8]->s;
	char *osc_path    = &argv[9]->s;
	char *format     = &argv[10]->s;
	char *tooltip    = &argv[11]->s;
	
	int widget_type = builder_get_widget_type( widgetname );

	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *panel = builder_from_register( stats, panel_name );
		if(!panel)
		{
			veejay_msg(0,"Widget '%s' is not in register!\n", panel_name);
			return;
		}

		builder_new_numeric_object(
			stats,
			panel,
			label,
			min,
			max, 
			value,
			digits,
			extra,
			widget_type,
			osc_path,
			format,
			tooltip
		);
	}
	else
	{
		veejay_msg(0,"%s: cannot get '%s\n", __FUNCTION__, window_name );
	}

}

void	osc_load_sample(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;

	const int	sample_id = argv[0]->i;
	const char *title = &argv[1]->s;
		
#ifdef STRICT_CHECKING
	assert( sample_id >= 0 );
	assert( title != NULL );
#endif	

	samplebank_store_sample( director_get_sample_pad(), sample_id, title );
	director_lock();
	director_reload_lists();
	director_unlock();
	
}
void	osc_update_preview(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;

}

void	osc_destroy_sample(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;

	const int	sample_id = argv[0]->i;
		
//	samplebank_destroy_sample( sample_id );
//		director_reload_lists()

}

void	osc_set_widget(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	char *window_name = &argv[0]->s;
	char *widget_name = &argv[1]->s;
	void *stats = NULL;
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		void *wid = builder_from_register( stats, widget_name );
		if(!wid)
		{
			veejay_msg(0,"Widget '%s' does not exist in window %s\n", widget_name, window_name);
			return;
		}

		GladeXML *xml = builder_glade_from_register( stats, window_name );
		char     *id  = builder_get_identifier( stats, wid );
	
		director_set_widget( xml, id,argc,argv );
	}
	else
	{
		veejay_msg(0,"Cannot find Window '%s'\n", window_name );
	}
}

void	osc_update_widget(const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *ptr )
{
	osc_recv_t *info = (osc_recv_t*) ptr;
	char *window_name = &argv[0]->s;
	void *stats = NULL;
	if( vevo_property_get(info->tree, window_name,0,&stats ) == VEVO_NO_ERROR )
	{
		director_update_widget( stats, (const char*) &argv[1]->s, types, argc,2, argv );
	}
}

gboolean osc_message_handler( GIOChannel *ch, GIOCondition condition, gpointer data )
{
	osc_recv_t *info = (osc_recv_t*) data;
	if( condition & G_IO_IN )
		lo_server_recv_noblock( info->st, 0 );
	if( condition & G_IO_NVAL )
		return FALSE;
	if( condition & G_IO_HUP )
	{
		g_io_channel_close( ch );
		return FALSE;
	}
	return TRUE;
}

int	ui_osc_new_listener(void *data, int fd)
{
	GIOChannel *ch = g_io_channel_unix_new( fd );

	int watch = -1;
	watch = g_io_add_watch_full(
			ch,
			G_PRIORITY_HIGH,
			G_IO_IN | G_IO_NVAL | G_IO_HUP,
			osc_message_handler, 
			data,
		        NULL	);

	return watch;
}



void	*ui_new_osc_server(void *data , const char *port)
{
	osc_recv_t *s = (osc_recv_t*) malloc(sizeof( osc_recv_t));
	memset(s,0,sizeof(osc_recv_t));
	s->st = lo_server_new( port, error_handler );

	s->tree = vpn(VEVO_ANONYMOUS_PORT );
	s->sender = NULL;
	lo_server_add_method( s->st,
			"/veejay",
			"s",
			osc_new_veejay_sender,
			(void*) s );
	
	lo_server_add_method( s->st,
			"/create/window",
			"ss",
			osc_new_window,
			(void*)s );
	lo_server_add_method( s->st,
			"/create/frame",
			"sss",
			osc_new_frame,
			(void*) s );
	lo_server_add_method( s->st,
			"/create/box",
			"sssi",
			osc_new_box,
			(void*) s );
	lo_server_add_method( s->st,
			"/create/vframe",
			"ssss",
			osc_new_vframe,
			(void*) s );
	lo_server_add_method( s->st,
			"/create/switch",
			"ssssiss",
			osc_new_switch,
			(void*) s );
	lo_server_add_method( s->st,
			"/create/button",
			"sssss",
			osc_new_button,
			(void*) s );
	lo_server_add_method( s->st,
			"/create/label",
			"ssss",
			osc_new_label,
			(void*) s );
	
	lo_server_add_method( s->st,
			"/update/widget",
			NULL,
			osc_update_widget,
			(void*) s );
	
	lo_server_add_method( s->st,
			"/update/tegdiw",
			NULL,
			osc_set_widget,
			(void*) s );

	lo_server_add_method( s->st,
			"/update/sample",
			"is",
			osc_load_sample,
			(void*) s);

	lo_server_add_method( s->st,
			"/create/pulldown",
			NULL,
			osc_new_pulldown,
			(void*) s );

	lo_server_add_method( s->st,
			"/update/pulldown",
			NULL,
			osc_update_pulldown,
			(void*) s );

	lo_server_add_method( s->st,
			"/create/fxpulldown",
			NULL,
			osc_new_fxpulldown,
			(void*) s );

	lo_server_add_method( s->st,
			"/create/radiogroup",
			"ssssii",
			osc_new_radio_group,
			(void*) s);

	lo_server_add_method( s->st,
			"/create/numeric",
			"sssdddiissss",
			osc_new_numeric,
			(void*)s );

	lo_server_add_method( s->st,
			"/create/channels",
			NULL,
			osc_new_widget_template,
			(void*) s );
	
	lo_server_add_method( s->st,
			"/show/window",
			"s",
			osc_realize_window,
			(void*) s );
	
	lo_server_add_method( s->st,
			"/destroy/sample",
			"s",
			osc_destroy_sample,
			(void*) s );
	
	lo_server_add_method( s->st,
			"/destroy/window",
			"s",
			osc_destroy_window,
			(void*) s );

	lo_server_add_method( s->st,
			"/update/preview",
			NULL,
			osc_update_preview,
			(void*) s );

	int w = ui_osc_new_listener( (void*) s, lo_server_get_socket_fd( s->st ) );

	veejay_msg(0,"liblo OSC server ready at port %s", port );
	
	return (void*) s;
}


void	ui_free_osc_server( void *dosc)
{
	osc_recv_t *s = (osc_recv_t*) dosc;
	vevo_port_free(s->tree);
	lo_server_free( s->st );
	free(s);
	s = NULL;
}

void	ui_print_help(void)
{
}
