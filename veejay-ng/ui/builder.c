/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
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

#include <stdio.h>
#include <config.h>
#include <ui/concretebuilder.h>
#include <ui/builder.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
static struct
{
	const char *name;
	const int id;
} builder_list[] = 
{
	{	"Window",	WINDOW		},
	{	"Frame",	FRAME		},
	{	"Button",	BUTTON		},
	{	"HSlider",	HSCALE		},
	{	"VSlider",	VSCALE		},
	{	"Spin",		SPINBUTTON	},
	{	"Radio",	RADIOBUTTON	},
	{	"Check",	CHECKBUTTON	},
	{	NULL,		0		}
};

int		builder_get_widget_type( const char *name )
{
	int i;
	for(i = 0; builder_list[i].name != NULL ; i ++ )
		if( strcasecmp(name, builder_list[i].name ) == 0 )
			return builder_list[i].id;
	return -1;
}

static void		*builder_create_named_component( 
		int type,
		void *container,
		void *stats,
		const char *identifier,
		const char *label_str,
		const char *osc_path,
		const char *format,
		const char *signalname,
		double *dv,
		const char *tooltip)
{
	int   box_type = (type == VSCALE  ? VBOX: HBOX );
	void *box = concretebuilder_new_widget( stats, container, NULL,box_type,NULL,NULL,NULL,NULL,NULL );
	void *action = NULL;
	void *label = NULL;
	if( box_type == HBOX )
	{
		label = concretebuilder_new_widget( stats,box,NULL,LABEL,NULL,NULL,NULL,NULL,NULL);
		action = concretebuilder_new_widget( stats,box,identifier,type, osc_path,format, signalname, dv,tooltip);
	}
	else
	{
		action = concretebuilder_new_widget( stats, box,identifier, type, osc_path,format,signalname,dv,tooltip );
		label = concretebuilder_new_widget( stats, box,NULL, LABEL,NULL,NULL,NULL,NULL,NULL );
	}
	if( strncasecmp(label_str, "none",4 ) == 0 )
		concretebuilder_change_default( label, "label", " ");
	else
		concretebuilder_change_default( label, "label", label_str );
	return action;
}



static void		*builder_create_component( 
		int type,
		void *container,
		void *stats,
		const char *label_str,
		const char *osc_path,
		const char *format,
		const char *signalname,
		double *dv,
		const char *tooltip)
{
	int   box_type = (type == VSCALE  ? VBOX: HBOX );
	void *box = concretebuilder_new_widget( stats, container, NULL,box_type,NULL,NULL,NULL,NULL,NULL );
	void *action = NULL;
	void *label = NULL;
	if( box_type == HBOX )
	{
		label = concretebuilder_new_widget( stats,box,NULL,LABEL,NULL,NULL,NULL,NULL,NULL);
		action = concretebuilder_new_widget( stats,box,NULL,type, osc_path,format, signalname, dv,tooltip);
	}
	else
	{
		action = concretebuilder_new_widget( stats, box,NULL, type, osc_path,format,signalname,dv,tooltip );
		label = concretebuilder_new_widget( stats, box,NULL, LABEL,NULL,NULL,NULL,NULL,NULL );
	}
	if( strncasecmp(label_str, "none",4 ) == 0 )
		concretebuilder_change_default( label, "label", " ");
	else
		concretebuilder_change_default( label, "label", label_str );
	return action;
}

static void		*builder_create_widget( 
		int type,
		void *container,
		void *stats,
		const char *identifier,
		const char *label_str,
		const char *osc_path,
		const char *format,
		const char *signalname,
		double *dv,
		const char *tooltip)
{
	void *action = NULL;
	action = concretebuilder_new_widget( stats,container,identifier, type, osc_path,format, signalname,dv,tooltip);
	concretebuilder_change_default( action, "label", label_str );
	return action;
}

void		*builder_new_numeric_object( void *stats, void *container, 
						const char *name,
						double	    min,
						double	    max,
						double      value,
	       					int	    n_digits,
						int	    wrap_or_invert,
						int	    widget_type,
						const char *osc_path,
						const char *format,
						const char *tooltip	)
{
	char tmp_str[128];
	char digit_str[8];
	char *signalname = NULL;
	if( osc_path )
	{
		signalname = concretebuilder_lookup_signalname( widget_type );
	}
	void *parameter = builder_create_component( widget_type, container, stats, name, osc_path,format,signalname,NULL,tooltip );
	char *inverted = (wrap_or_invert ? strdup( "True") : strdup("False"));

	if(widget_type == SPINBUTTON || widget_type == HSCALE || widget_type == VSCALE )
	{
		sprintf(tmp_str, "%g %g %g 0.00999999977648 0.10000000149 0", value,min,max );
		sprintf(digit_str, "%d", n_digits );
		concretebuilder_change_default( parameter, "adjustment", tmp_str);
		concretebuilder_change_default( parameter, "digits", digit_str );

		if( widget_type == SPINBUTTON )
			concretebuilder_change_default( parameter, "wrap", inverted );
		else
			concretebuilder_change_default( parameter, "inverted", inverted );

		if( widget_type == HSCALE )
			concretebuilder_change_default( parameter, "value_pos", "GTK_POS_RIGHT" );
	}
	
	free(inverted);
	if(signalname)
		free(signalname);

	return parameter;
}
void		*builder_new_label_object( void *stats, void *container,
						const char *name, const char *widname )
{
	char *signalname = NULL;
	void *parameter = builder_create_widget( LABEL, container, stats,NULL, name, NULL,NULL,NULL,NULL,NULL );
	char value_str[4];

	if(widname)
	{
		builder_register_widget(stats, widname, parameter ); 
		veejay_msg(0, "Parameter %s : %p registered", widname, parameter );	
	}
	else
		veejay_msg(0, "No widget name given!");

	if(signalname)
		free(signalname);
	return parameter;
}

void		*builder_new_button_object( void *stats, void *container,
						const char *name,
	       					int	    widget_type,
						const char *osc_path,
						const char *tooltip	)
{
	char *signalname = NULL;
	if( osc_path )
		signalname = concretebuilder_lookup_signalname( widget_type );
	
	
	void *parameter = builder_create_widget( widget_type, container, stats,NULL, name, osc_path,NULL,signalname,NULL,tooltip );
	char value_str[4];

	if(signalname)
		free(signalname);
	return parameter;
}
void		*builder_new_boolean_object( void *stats, void *container,
						const char *name,
						int	    active,
	       					int	    widget_type,
						const char *osc_path,
						const char *tooltip	)
{
	char *signalname = NULL;
	if( osc_path )
		signalname = concretebuilder_lookup_signalname( widget_type );
	
	
	void *parameter = builder_create_widget( widget_type, container,stats,NULL, name, osc_path, "i", signalname,NULL,tooltip );	
	char value_str[6];

	sprintf(value_str, "%s",active == 0 ? "False" : "True");
	concretebuilder_change_default( parameter, "active", value_str );
	if(signalname)
		free(signalname);
	return parameter;
}

void		*builder_new_boolean_group_object( void *stats, void *container,
						const char *prefix,
						const char *label_prefix,
						int	   selected,
						int	    n_buttons )
{
	int k;
#ifdef STRICT_CHECKING
	assert( n_buttons >= 2 );
	assert( label_prefix != NULL );
	assert( prefix != NULL );
#endif
	char *signalname = concretebuilder_lookup_signalname( RADIOBUTTON );
#ifdef STRICT_CHECKING
	assert( signalname != NULL );
#endif
	void *box = concretebuilder_new_widget( stats, container, NULL,HBOX,NULL,NULL,NULL,NULL,NULL );
	char button_id[32];
	char button_name[32];
	char *identifier = NULL;
	for( k = 0; k < n_buttons; k ++ )
	{
		sprintf(button_name, "%s %d",label_prefix,k);
		if( k == 0 )
		{
			sprintf(button_id, "%s_0", prefix );
			identifier = strdup( button_id );
		}
		else
			sprintf(button_id, "%s_%d",prefix, k);
		
		void *parameter = builder_create_widget( RADIOBUTTON, box, stats, button_id, button_name,NULL, NULL, signalname,&selected,NULL );	
		if(k == selected)
			concretebuilder_change_default( parameter, "active", "True" );
		else
			concretebuilder_change_default( parameter, "active", "False" );

		if(k > 0)
			concretebuilder_change_default( parameter, "group", identifier );
	}
	free(identifier);

	if(signalname)
		free(signalname);
	return box;
}

void		*builder_new_pulldown_object( void *stats, void *container,
						const char *name,
						const char *identifier,
						int	   widget_type,
						int	    n_items,
						char	    **items,
						const char *osc_path,
						const char *format,
						double	     dv,
						const char *tooltip	)
{
	char *signalname = NULL;
	if( osc_path )
		signalname = concretebuilder_lookup_signalname( widget_type );
	
	void *parameter = builder_create_named_component(
				widget_type,	
				container,
				stats,
				identifier,
				name,
				osc_path,
				(format == NULL ? "i": format),
				signalname,
				&dv,
		       	        tooltip	);

	int n = 1;
	int i;
	for( i = 0; i < n_items  ; i ++ )
	 	n+= (strlen(items[i]) + 1);

	char *list = (char*) malloc(sizeof(char) * n );
	bzero(list, n );
	for( i = 0; i < n_items ; i ++ )
	{
		strcat( list, items[i] );
		strcat( list, "\n");
	}


	concretebuilder_change_default( parameter, "items", list );
	free(list);
	if( signalname)
		free(signalname);
	return parameter;
}

void		*builder_new_frame( void *stats, void *container, const char *name, int box_type, const char *my_frame, int colapse )
{
	void *frame, *align, *label, *box;
	if(!colapse)
	{
		frame = concretebuilder_new_widget( stats, container,NULL, FRAME,NULL,NULL,NULL,NULL,NULL );
			concretebuilder_change_default( frame, "shadow_type", "GTK_SHADOW_ETCHED_IN" );
	
		align = concretebuilder_new_widget( stats, frame,NULL, ALIGN,NULL,NULL,NULL,NULL,NULL );
		label = concretebuilder_new_widget( stats, frame,NULL, FRAMELABEL,NULL,NULL,NULL,NULL ,NULL);
		box   = concretebuilder_new_widget( stats, align,NULL, box_type ,NULL,NULL,NULL,NULL,NULL);
	}
	else
	{
		frame = concretebuilder_new_collapsed_widget( stats, container,NULL, FRAME,NULL,NULL,NULL,NULL,NULL );
			concretebuilder_change_default( frame, "shadow_type", "GTK_SHADOW_ETCHED_IN" );
	
		align = concretebuilder_new_collapsed_widget( stats, frame,NULL, ALIGN,NULL,NULL,NULL,NULL,NULL );
		label = concretebuilder_new_widget( stats, frame,NULL, FRAMELABEL,NULL,NULL,NULL,NULL ,NULL);
		box   = concretebuilder_new_collapsed_widget( stats, align,NULL, box_type ,NULL,NULL,NULL,NULL,NULL);

	}

	char bold_name[256];
	sprintf(bold_name, "&lt;b&gt;%s&lt;/b&gt;",name);
	concretebuilder_change_default( label, "label", bold_name );

	builder_register_widget( stats, my_frame, frame );

	my_frame = frame;
	return box;
}

void		*builder_new_box( void *stats, void *container, int box_type )
{
	void *box = concretebuilder_new_collapsed_widget(stats, container,NULL, box_type ,NULL,NULL, NULL,NULL,NULL);
	return box;
}

void		*builder_new_window( void *stats, const char *label )
{
	void *window = concretebuilder_new_widget( stats, NULL, NULL,WINDOW , NULL,NULL, NULL,NULL,NULL);
	concretebuilder_change_default( window, "title", label );
	concretebuilder_set_default_sample( stats, label );
	return window;
}

int		builder_get_default_sample( void *stats )
{
	return	concretebuilder_get_default_sample( stats );
}

char  *		builder_formalize( void *stats, void *rootnode, int *size )
{
	concretebuilder_formalize(stats, rootnode );
	*size = concretebuilder_get_size( stats );
	return concretebuilder_get_buffer( stats );
}
char  *		builder_formalize2( void *stats, void *rootnode, int *size )
{
	concretebuilder_formalize2(stats, rootnode );
	*size = concretebuilder_get_size( stats );
	return concretebuilder_get_buffer( stats );
}
void		builder_reap( void *stats )
{
	concretebuilder_reap( stats );
}

void		 builder_writefile( void *stats, const char *filename )
{
	int size = concretebuilder_get_size( stats );
	char *res = concretebuilder_get_buffer( stats );


	FILE *f = fopen( filename, "w" );
	int   n = fwrite( res, size, 1, f );
	fclose( f );
}

void		*builder_init()
{
	return concretebuilder_init();
}

void		builder_destroy_rootnode( void *stats, void *rootnode )
{
	concretebuilder_destroy_container(stats, rootnode );
}

void		builder_free( void *stats)
{
	concretebuilder_free( stats);
}

char		**builder_get_full( void *ui )
{
	return concretebuilder_get_full( ui );
}

double		builder_get_value_after( void *stats, const char *id, int *error)
{
	return concretebuilder_get_valueafter( stats,id, error );
}

char		*builder_get_signalname( void *stats, const char *id )
{
	return concretebuilder_get_signalname(stats,id);
}
char		*builder_get_oscpath(void *stats, const char *id )
{
	return concretebuilder_get_oscpath(stats,id);
}
char		*builder_get_format(void *stats, const char *id )
{
	return concretebuilder_get_format(stats,id);
}
char		*builder_get_tooltip(void *stats, const char *id )
{
	return concretebuilder_get_tooltip(stats,id);
}

int		builder_register_method( void *stats, const char *name,void *ptr, const char *widget)
{
	return concretebuilder_register_method(stats,name,ptr,widget);
}
char		*builder_get_signaldata( void *stats, void *ptr)
{
	return concretebuilder_get_signaldata( stats,ptr);
}
char		*builder_get_signalformat( void *stats, void *ptr)
{
	return concretebuilder_get_signalformat( stats,ptr);
}
char		*builder_get_signalwidget( void *stats, void *ptr)
{
	return concretebuilder_get_signalwidget( stats,ptr);
}

void		builder_register_widget( void *stats, const char *name, void *ptr )
{
	concretebuilder_register_as( stats,name,ptr);
}
void		builder_unregister_widget(void *stats, const char *name)
{
	concretebuilder_register_empty(stats,name);
}

void		*builder_from_register( void *stats, const char *key )
{
	return concretebuilder_from_register( stats, key );
}

void		builder_register_glade( void *stats, const char *key, void *ptr)
{
	concretebuilder_register_gladeptr( stats, key,ptr );
}

void		*builder_glade_from_register( void *stats, const char *key )
{
	return concretebuilder_gladeptr_from_register( stats, key );
}

char		*builder_get_identifier( void *stats, void *ptr )
{
	return concretebuilder_get_identifier( stats,ptr);
}
void	*builder_widget_by_path( void *stats, const char *path )
{
	return concretebuilder_widget_by_path( stats, path );
}
