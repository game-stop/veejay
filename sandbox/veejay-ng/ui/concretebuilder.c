/* veejayui - Live GUI interface via OSC for Veejay
 *           (C) 2002-2006 Niels Elburg <nelburg@looze.net> 
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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <libvevo/libvevo.h>
#include <ui/builder.h>
#include <ui/buffer.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

/*
 * module for building ports from templates.
 * it provides the basic building blocks for 
 * building a GladeXML
 *
 */

typedef struct
{
	const char *name;
	const char *default_value;
} property_t;

static struct
{
	int	id;
	const   char *class_name;
	const	char *id_prefix;
} class_list[] =
{
	{ LABEL, 		"GtkLabel", 		"label_" 	},
	{ WINDOW, 		"GtkWindow", 		"window_" 	},
	{ VBOX, 		"GtkVBox",		"vbox_" 	},
	{ FRAME,  		"GtkFrame", 		"frame_" 	},
	{ BUTTON, 		"GtkButton", 		"button_" 	},
	{ HBUTTONBOX, 		"GtkButtonBox", 	"buttonbox_" 	},
	{ ALIGN, 		"GtkAlignment", 	"alignment_"	},
	{ PACKING, 		NULL,			 NULL 		},
	{ TOGGLEBUTTON, 	"GtkToggleButton",	"togglebutton_" },
	{ RADIOBUTTON,		"GtkRadioButton",	"radiobutton_" },
	{ VSCALE,    		"GtkVScale",		"vscale_"	},
	{ HSCALE,		"GtkHScale",		"hscale_"	},
	{ CHECKBUTTON,		"GtkCheckButton",	"checkbutton_"  },
	{ COMBOBOX,		"GtkComboBoxEntry",    	"combobox_"	},
	{ SPINBUTTON,		"GtkSpinButton",   	"spin_"		},
	{ HBOX,			"GtkHBox",	    	"hbox_"		},
	{ SCROLLEDWINDOW,	"GtkScrolledWindow", 	"scrolledwindow_"},
	{ VIEWPORT,		"GtkViewport",		"viewport_" 	},
	{ FRAMELABEL,		"GtkLabel",		"label_"	},
	{ MENUBAR,		"GtkMenuBar",		"menubar_"	},
	{ MENU,			"GtkMenu",		"chainmenu_"	},
	{ MENUITEM,		"GtkMenuItem",		"menuitem_"	},
	{ 0, 			NULL,			NULL		}
};

static property_t window_widget[] = 
{
	{	"visible",	"True" 		},
	{	"title",	"Untitled"	},
	{	"type",		"GTK_WINDOW_TOPLEVEL" },
	{	"window_position","GTK_WIN_POS_NONE" },
	{	"modal",	"False" },
	{	"resizable",	"True"  },
	{	"destroy_with_parent", "False" },
	{	"decorated",	"True" },
	{	"skip_taskbar_hint", "False"  },
	{	"skip_pager_hint" , "False" },
	{	"type_hint",	"GDK_WINDOW_TYPE_HINT_NORMAL" },
	{	"gravity",	"GDK_GRAVITY_NORTH_WEST"},
	{	"focus_on_map", "True"  },
	{	"urgency_hint", "False" },
	{	NULL,		NULL,	}
};

static	property_t	menubar_widget[] = 
{
	{	"visible", 		"True"		},
	{	"pack_direction", 	"GTK_PACK_DIRECTION_LTR"	},
	{	"child_pack_direction", "GTK_PACK_DIRECTION_LTR"	},
	{	NULL,			NULL,				}
};

static	property_t	menuitem_widget[] =
{
	{	"visible",		"True"		},
	{	"label",		"_Menuitem"	},
	{	"use_underline",	"True"		},
	{	NULL,			NULL		}
};

static	property_t	menu_widget[] = 
{
	{	NULL,			NULL		}
};


static property_t vbox_widget[] = 
{
	{ "visible",		"True"	},
	{ "homogeneous",	"False"  },
	{ "spacing",		"0" 	 },
	{ NULL,			NULL 	 }
};
static property_t hbox_widget[] = 
{
	{ "visible",		"True"	},
	{ "homogeneous",	"False"  },
	{ "spacing",		"0" 	 },
	{ NULL,			NULL 	 }
};

static property_t min_packing_widget[] = 
{
	{ "type",	"label_item"  },
	{ NULL,			NULL 	 }
};

static property_t scrolledwindow_widget[] =
{
	{ "visible",	"True" },
	{ "can_focus",	"True" },
	{ "hscrollbar_policy", "GTK_POLICY_AUTO" },
	{ "vscrollbar_policy", "GTK_POLICY_AUTO" },
	{ "shadow_type" , "GTK_SHADOW_IN" },
	{ "window_placement" , "GTK_CORNER_TOP_LEFT" },
	{ NULL, NULL }
};

static property_t viewport_widget[] = 
{
	{ "visible", "True" },
	{ "shadow_type", "GTK_SHADOW_IN" },
	{ NULL,NULL}
};


static property_t frame_widget[] =
{
	{ "visible",		"True" },
	{ "label_xalign",	"0.0"  },
	{ "label_yalign",	"0.5"  },
	{ "shadow_type",	"GTK_SHADOW_NONE" },
	{ NULL,			NULL,	}
};

static property_t align_widget[] =
{
	{ "visible",		"True" },
	{ "xalign",		"0.5"  },
	{ "yalign",		"0.5"  },
	{ "xscale",		"1"    },
	{ "yscale",		"1"    },
	{ "top_padding",	"0"    },
	{ "bottom_padding",	"1"    },
	{ "left_padding",	"1"   },
	{ "right_padding",	"0"    },
	{ NULL,			NULL   }
};

static property_t hbuttonbox_widget[] =
{
	{ "visible",		"True"   },
	{ "can_default",	"True"   },
	{ "can_focus",		"True"   },
	{ "label",		"buttonbox"	 },
	{ "use_underline",	"True"   },
	{ "relief",		"GTK_RELIEF_NORMAL"  },
	{ "focus_on_click",	"True"   },
	{ NULL,			NULL	 }
};

static property_t button_widget[] = 
{
	{ "visible",		"True"   },
	{ "can_default",	"True"   },
	{ "can_focus",		"True"	 },
	{ "label",		"button" },
	{ "use_underline",	"True"     },
	{ "relief",		"GTK_RELIEF_NORMAL"  },
	{ "focus_on_click",	"True" 	},
	{ NULL,			NULL    }
};
static	property_t checkbutton_widget[] =
{
	{ "visible",		"True" },
	{ "can_focus",		"True" },
	{ "label",		" " },
	{ "use_underline",	"True"  },
	{ "relief",		"GTK_RELIEF_NORMAL" },
	{ "focus_on_click",	"True" },
	{ "active",		"False" },
	{ "inconsistent",	"False" },
	{ "draw_indicator",	"True" },
	{ NULL,			NULL }
};
static	property_t radiobutton_widget[] =
{
	{ "visible",		"True" },
	{ "can_focus",		"True" },
	{ "label",		" " },
	{ "active",		"False"},
	{ "use_underline",	"True"  },
	{ "relief",		"GTK_RELIEF_NORMAL" },
	{ "focus_on_click",	"True" },
	{ "inconsistent",	"False" },
	{ "draw_indicator",	"True" },
	{ NULL,			NULL }
};

static	property_t togglebutton_widget[] = 
{
	{ "visible",		"True" },
	{ "can_focus",		"True" },
	{ "label",		"togglebutton" },
	{ "use_underline",	"True" },
	{ "relief",		"GTK_RELIEF_NORMAL" },
	{ "focus_on_click",	"True" },
	{ "active",		"False" },
	{ "inconsistent",	"False" }
};


static	property_t	spinbox_widget[] = 
{
	{ "visible",		"True"	},
	{ "can_focus",		"True"  },
	{ "climb_rate",		"1"	},
	{ "digits",		"0"	},
	{ "numeric",		"False" },
	{ "update_policy",	"GTK_UPDATE_ALWAYS" },
	{ "snap_to_ticks",	"False" },
	{ "wrap",		"False" },
	{ "adjustment",		"1 0 100 1 10 10" },
	{ NULL,			NULL }
};

static	property_t	combobox_widget[] = 
{
	{ "visible",		"True"	},
	{ "add_tearoffs",	"True"	},
	{ "focus_on_click",	"True" },
	{ "has-frame",		"True" },
//	{ "wrap-width",		"1"	},
	{ NULL,			NULL }
};	

static	property_t	hscale_widget[] =
{
	{ "visible",		"True" },
	{ "can_focus",		"True" },
	{ "draw_value",		"True" },
	{ "value_pos",		"GTK_POS_TOP" },
	{ "digits",		"1" },
	{ "update_policy",	"GTK_UPDATE_CONTINUOUS" },
	{ "inverted",		"False" },
	{ "adjustment",		"0 0 0 0 0 0" },
	{ NULL,			NULL }
};
static	property_t	vscale_widget[] =
{
	{ "visible",		"True" },
	{ "can_focus",		"True" },
	{ "draw_value",		"True" },
	{ "value_pos",		"GTK_POS_TOP" },
	{ "digits",		"1" },
	{ "update_policy",	"GTK_UPDATE_CONTINUOUS" },
	{ "inverted",		"False" },
	{ "adjustment",		"0 0 0 0 0 0" },
	{ NULL,			NULL }
};

static property_t label_widget[] = 
{
	{ "visible",		"True"  },
	{ "label",		"&lt;b&gt;Untitled&lt;/b&gt;"     },
	{ "use_underline",	"False"  },
	{ "use_markup",		"True"  },
	{ "justify",		"GTK_JUSTIFY_LEFT" },
	{ "wrap",		"False"  },
	{ "selectable",		"False"  },
	{ "xalign",		"0.5"	 },
	{ "yalign",		"0.5"    },
	{ "xpad",		"0"	 },
	{ "ypad",		"0"	},
	{ "ellipsize",		"PANGO_ELLIPSIZE_NONE" },
	{ "width_chars",	"-1"	},
	{ "single_line_mode",   "False" },
	{ "angle",		"0" 	},
	{ NULL,			NULL    }	

};

static property_t all_packing_widget[] = 
{
	{	"padding",	"0" },
	{	"expand",	"True" },
	{	"fill",		"True" },
	{	NULL,		NULL }
};

static struct
{
	int widget_type;
	const char *name;
} signalnames[] = 
{
	{	BUTTON,		"clicked"	 	},
	{	SPINBUTTON,     "value_changed" 	},
	{	HSCALE,		"value_changed"		},
	{	VSCALE,		"value_changed"		},
	{	COMBOBOX,	"changed"		},
	{	RADIOBUTTON,	"toggled"		},
	{	CHECKBUTTON,	"toggled"		},
	{	MENUITEM,	"activate"		},
	{	-1,		NULL			}
	
};


typedef struct
{
	int autonumber[32];
	void *b;
	void *methods;
	void *registry;
	void *symbols;
	void *glade;
	void *mapping;
	void *update_signals;
	int  lock;
	int  sample_id;
} ui_stats_t;

typedef struct
{
	void *tree;
//	xmlDocPtr doc;	
} ui_file_t;

static	void *concretebuilder_init_template( property_t *list )
{
	int i;
	void *res = vpn( VEVO_ANONYMOUS_PORT);
	for ( i = 0; list[i].name != NULL ; i ++ )
	{
		vevo_property_set(
				res,
				list[i].name,
			        VEVO_ATOM_TYPE_STRING,
				1,
				&(list[i].default_value)
		       	);
	}
	return res;
}

static void		concretebuilder_new_packing( void *container, int expand, int filled )
{
	void *packing_port = vpn( VEVO_ANONYMOUS_PORT);
	void *properties = concretebuilder_init_template( all_packing_widget );

	concretebuilder_change_default( properties, "Expand", (expand == 0 ? "False"  : "True" ));
	concretebuilder_change_default( properties, "Fill",   (filled == 0 ? "False"  : "True" ));
	
	vevo_property_set( packing_port, "properties", VEVO_ATOM_TYPE_PORTPTR,1,
						&properties );
	
	vevo_property_set( container, "packing", VEVO_ATOM_TYPE_PORTPTR,1,&packing_port );
}

void	concretebuilder_set_packing(void *container)
{
	void *packing_port = vpn( VEVO_ANONYMOUS_PORT );
	
	void *properties = concretebuilder_init_template( min_packing_widget );

	vevo_property_set( packing_port, "properties", VEVO_ATOM_TYPE_PORTPTR,1,
						&properties );

	vevo_property_set( container, "packing", VEVO_ATOM_TYPE_PORTPTR,1,&packing_port );
}

static void	concretebuilder_set_packing_box(void *container)
{
	void *packing_port = vpn( VEVO_ANONYMOUS_PORT);
	
	void *properties = concretebuilder_init_template( all_packing_widget );

	vevo_property_set( packing_port, "properties", VEVO_ATOM_TYPE_PORTPTR,1,
						&properties );

	vevo_property_set( container, "packing", VEVO_ATOM_TYPE_PORTPTR,1,&packing_port );
}

void	concretebuilder_set_packing_collapsed(void *container)
{
	void *packing_port = vpn( VEVO_ANONYMOUS_PORT );
	
	void *properties = concretebuilder_init_template( all_packing_widget );

	concretebuilder_change_default( properties, "expand", "False" );
	concretebuilder_change_default( properties, "fill", "False" );

	vevo_property_set( packing_port, "properties", VEVO_ATOM_TYPE_PORTPTR,1,
						&properties );
	
	vevo_property_set( container, "packing", VEVO_ATOM_TYPE_PORTPTR,1,&packing_port );
}

int	concretebuilder_change_default( void *properties, const char *key, const char *value )
{
	int error = vevo_property_set( properties, key, VEVO_ATOM_TYPE_STRING,1, &value );
	return error;	
}

void	concretebuilder_set_default_sample( void *stats, const char *label)
{
	
	ui_stats_t	*n = (ui_stats_t*) stats;
	char *start = strcasestr( label, "Sample " );
	if( start )
	{
		int id = 0;
		sscanf(start, "Sample %d", &(n->sample_id) );
#ifdef STRICT_CHECKING
		assert( n->sample_id >= 0 );
#endif
	}
}

int	concretebuilder_get_default_sample( void *stats )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	return n->sample_id;
}


void	concretebuilder_reset_stats (void *stats )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	memset( n->autonumber,0, sizeof(int) * 32 );
}

void	concretebuilder_lock(void *ui)
{
	ui_stats_t	*n = (ui_stats_t*) ui;
	n->lock = 1;
}

void	concretebuilder_unlock(void *ui)
{
	ui_stats_t	*n = (ui_stats_t*) ui;
	n->lock = 0;
}


int	concretebuilder_islocked(void *ui)
{
	ui_stats_t	*n = (ui_stats_t*) ui;
	return n->lock;
}

static void	*concretebuilder_create_widget( void *ui, int type, const char *ident, const char *OSC_Path, const char *format,const char *signalname, double *dv , char *my_id, const char *tooltip)
{
	ui_stats_t	*n = (ui_stats_t*) ui;

	void *widget_port = vpn( VEVO_ANONYMOUS_PORT );
	char identifier[256];

	if( ident == NULL )
	{
		sprintf( identifier, "%s%d", class_list[type].id_prefix,
			n->autonumber[ type ] ++ );
	}
	else
	{
		sprintf(identifier, "%s", ident );
	}

	snprintf(my_id, 256, "%s", identifier);

	if( signalname)
	{
		void *method = vpn( VEVO_ANONYMOUS_PORT );
		if(OSC_Path)
			vevo_property_set( method, "OSC_Path", VEVO_ATOM_TYPE_STRING, 1, &OSC_Path );
		if(format)
			vevo_property_set( method, "format",   VEVO_ATOM_TYPE_STRING, 1, &format );
		if(tooltip)
			vevo_property_set( method, "tooltip",   VEVO_ATOM_TYPE_STRING, 1, &tooltip );

		vevo_property_set( method, "signal"  , VEVO_ATOM_TYPE_STRING, 1, &signalname);
		if( dv )
			vevo_property_set( method, "default", VEVO_ATOM_TYPE_DOUBLE, 1, dv );

		vevo_property_set( n->methods,identifier, VEVO_ATOM_TYPE_PORTPTR,1,&method );
	}
	
	char *class_name = strdup( class_list[type].class_name );
	char *id         = strdup( identifier );

	vevo_property_set( widget_port, "class", VEVO_ATOM_TYPE_STRING,1,&class_name);
	vevo_property_set( widget_port, "id",    VEVO_ATOM_TYPE_STRING,1,&id );

//	snprintf( idn, 100, "%s", id );

	void *widget_properties = NULL; 
		switch( type )
	{
		case WINDOW:widget_properties = concretebuilder_init_template( window_widget ); break;
		case FRAMELABEL: widget_properties = concretebuilder_init_template( label_widget);break;
		case LABEL:widget_properties = concretebuilder_init_template( label_widget ); break;
		case VBOX:widget_properties = concretebuilder_init_template( vbox_widget );break;
		case HBOX:widget_properties = concretebuilder_init_template( hbox_widget); break;
		case FRAME:widget_properties = concretebuilder_init_template( frame_widget); break;
		case ALIGN:widget_properties = concretebuilder_init_template( align_widget); break;
		case BUTTON:widget_properties =  concretebuilder_init_template( button_widget); break;
	        case HBUTTONBOX:widget_properties = concretebuilder_init_template( hbuttonbox_widget); break;
		case SPINBUTTON: widget_properties = concretebuilder_init_template( spinbox_widget );break;
		case COMBOBOX: widget_properties = concretebuilder_init_template( combobox_widget ); break;
		case HSCALE: widget_properties = concretebuilder_init_template( hscale_widget );break;
		case VSCALE: widget_properties = concretebuilder_init_template( vscale_widget );break;
		case RADIOBUTTON: widget_properties = concretebuilder_init_template( radiobutton_widget) ;break;
		case TOGGLEBUTTON: widget_properties = concretebuilder_init_template( togglebutton_widget); break;
		case SCROLLEDWINDOW: widget_properties = concretebuilder_init_template( scrolledwindow_widget); break;
		case VIEWPORT: widget_properties = concretebuilder_init_template( viewport_widget) ; break;
	  	case CHECKBUTTON: widget_properties = concretebuilder_init_template( checkbutton_widget) ;break;
	        case MENUBAR: widget_properties = concretebuilder_init_template( menubar_widget) ; break;
    		case MENU: widget_properties = concretebuilder_init_template( menu_widget ) ; break;
		case MENUITEM: widget_properties = concretebuilder_init_template( menuitem_widget ); break;			   
		default:
		 break;		 
	}

	vevo_property_set( widget_port, "properties", VEVO_ATOM_TYPE_PORTPTR, 1, &widget_properties);

	free( class_name );
	free( id );

	return widget_port;
}

void	*concretebuilder_new_method( void *container, char *name, char *handler )
{
	return NULL;
}

char	*concretebuilder_lookup_signalname( int widget_type )
{
	int i;
	for( i = 0; signalnames[i].name != NULL ; i ++ )
	{
		if( widget_type == signalnames[i].widget_type )
			return strdup( signalnames[i].name );
	}
	return NULL;
}

static	void	concretebuilder_register_id( ui_stats_t *n, void *keyp, const char *id )
{
	char pkey[64];
	sprintf(pkey ,"%p", keyp );
	vevo_property_set( n->mapping, pkey, VEVO_ATOM_TYPE_STRING, 1, &id );
}
void	*concretebuilder_new_collapsed_widget(void *ui, void *container,const char *identifier, int type, const char *OSC_Path, const char *format,const char *signalname, double *dv, const char *tooltip)
{
	ui_stats_t	*n = (ui_stats_t*) ui;
	char		my_identifier[256];
	void		*widget = concretebuilder_create_widget( ui,type,identifier, OSC_Path,format, signalname, dv, my_identifier,tooltip );
	void 		*properties = NULL;
//	char		*map_id = strdup( identifier );
	
	vevo_property_get( widget, "properties",0,&properties );

	if( container == NULL )
	{
		vevo_property_set( properties, "parent", VEVO_ATOM_TYPE_VOIDPTR ,1, &widget);
		concretebuilder_register_id( n, properties, my_identifier );
		return properties;
	}


	if( container != NULL )
	{	
		char key[64];
		// store port with widget in container as child
		void *child_port = vpn( VEVO_ANONYMOUS_PORT ); 
		sprintf( key, "widget_%p", widget );
		vevo_property_set( child_port, key, VEVO_ATOM_TYPE_PORTPTR,1,&widget );
		sprintf(key, "child_%p", child_port );
		vevo_property_set( container, key, VEVO_ATOM_TYPE_PORTPTR,1,&child_port );

		if( type == FRAMELABEL )
			concretebuilder_set_packing( child_port );
		else if( type == FRAME || type == VSCALE || type == HSCALE ) {
			concretebuilder_set_packing_box( child_port );
		}
		else if ( type == FRAME || type == LABEL || type ==COMBOBOX || type == BUTTON) //type == VBOX || type == HBOX)
		{
			concretebuilder_set_packing_collapsed(child_port);
		}

		
		concretebuilder_register_id( n, properties, my_identifier );
		return properties;
	}
	return NULL;
}
void	*concretebuilder_new_widget(void *ui, void *container,const char *identifier, int type, const char *OSC_Path, const char *format,const char *signalname, double *dv, const char *tooltip)
{
	ui_stats_t	*n = (ui_stats_t*) ui;
	char		my_identifier[256];
	void		*widget = concretebuilder_create_widget( ui,type,identifier, OSC_Path,format, signalname, dv, my_identifier,tooltip );
	void 		*properties = NULL;
//	char		*map_id = strdup( identifier );
	
	vevo_property_get( widget, "properties",0,&properties );

	if( container == NULL )
	{
		vevo_property_set( properties, "parent", VEVO_ATOM_TYPE_VOIDPTR ,1, &widget);
		concretebuilder_register_id( n, properties, my_identifier );
		return properties;
	}


	if( container != NULL )
	{	
		char key[64];
		// store port with widget in container as child
		void *child_port = vpn( VEVO_ANONYMOUS_PORT );
		sprintf( key, "widget_%p", widget );
		vevo_property_set( child_port, key, VEVO_ATOM_TYPE_PORTPTR,1,&widget );
		sprintf(key, "child_%p", child_port );
		vevo_property_set( container, key, VEVO_ATOM_TYPE_PORTPTR,1,&child_port );

		if( type == FRAMELABEL )
			concretebuilder_set_packing( child_port );
		else if( type == FRAME || type == VSCALE || type == HSCALE ) {
			concretebuilder_set_packing_box( child_port );
		}
		else if ( type == LABEL || type ==COMBOBOX || type == BUTTON) //type == VBOX || type == HBOX)
		{
			concretebuilder_set_packing_collapsed(child_port);
		}
		
		concretebuilder_register_id( n, properties, my_identifier );
		return properties;
	}
	return NULL;
}

void	*concretebuilder_init( void )
{
	ui_stats_t	*n = (ui_stats_t*) malloc(sizeof(ui_stats_t));
	memset( n,0,sizeof(ui_stats_t));
	n->b = blob_new( 1024 * 128 );  // 128 Kb
        n->methods = vpn( VEVO_ANONYMOUS_PORT);
	n->registry = vpn( VEVO_ANONYMOUS_PORT);
	n->symbols = vpn( VEVO_ANONYMOUS_PORT);
	n->glade   = vpn( VEVO_ANONYMOUS_PORT );
	n->mapping = vpn( VEVO_ANONYMOUS_PORT );
	n->update_signals = vpn( VEVO_ANONYMOUS_PORT );
	return (void*) n;
}

void	concretebuilder_free( void *stats )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	blob_free( n->b );
	//@ if a frame closes, we need to delete registry, symbol properties !
	vevo_port_free( n->registry );
	vevo_port_free( n->symbols );
	vevo_port_recursive_free( n->methods );
}

void	concretebuilder_destroy_container( void *stats, void *rootnode )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	vevo_port_recursive_free(rootnode);
}

static	void	signal_dump( ui_stats_t *stats, void *widget_entry )
{
	char *name = vevo_property_get_string( widget_entry, "class" );
	char *handler = vevo_property_get_string( widget_entry, "id" );
	char *last_time = "Wed, 06 Sep 2006 15:02:28 GMT";
	
	blob_printf(stats->b, "<signal name=\"%s\" handler=\"%s\" last_modification_time=\"%s\"/>\n",
		name, handler, last_time );

	free(name);
	free(handler);
	
}

static	void	*window_dump_properties(ui_stats_t *stats, void *widget_entry )
{
	void *res = NULL;
	vevo_property_get( widget_entry, "properties",0,&res);

	char *classname = vevo_property_get_string( widget_entry, "class" );
	char *identifier = vevo_property_get_string( widget_entry, "id" );

	blob_printf(stats->b, "<widget class=\"%s\" id=\"%s\">\n",
			classname, identifier );

	free(classname);
	free(identifier);
	
	return res;
}

static	void	print_tabs(ui_stats_t *stats, int n)
{
	int i;
	for( i = 0; i < n; i ++ )
		blob_printf( stats->b,"  ");
}

static void	dump_ui_( ui_stats_t *stats, void *rootnode )
{
	static int tabs = 1;
	char **items = vevo_list_properties( rootnode );
	int i;
	
	for( i = 0; items[i] != NULL ; i ++ )
	{
		int type = vevo_property_atom_type( rootnode, items[i] );

		if( type == VEVO_ATOM_TYPE_PORTPTR )
		{
			void *sub_tree = NULL;
			int error = vevo_property_get( rootnode,items[i],0,&sub_tree);
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif
			char *end = NULL;
			if( strncasecmp( items[i], "child", 5 ) == 0 )
			{  print_tabs(stats,tabs);	blob_printf(stats->b, "<child>\n"); end = strdup("</child>\n"); tabs++; }
			else if (strncasecmp( items[i], "widget", 6)==0)
			{ print_tabs(stats,tabs);	sub_tree = window_dump_properties( stats,sub_tree ); end = strdup("</widget>\n");tabs++ ;}
			else if( strncasecmp( items[i], "packing", 7 ) == 0 )
			{ print_tabs(stats,tabs);
				blob_printf(stats->b, "<packing>\n") ; end = strdup("</packing>\n"); tabs++;}

#ifdef STRICT_CHECKING
			assert( sub_tree != NULL );
#endif
			if( strncasecmp( items[i], "signal", 5 ) == 0 )
			{ print_tabs(stats,tabs) ; signal_dump(stats, sub_tree ) ; end = strdup("</signal>\n"); tabs ++ ; }
			else
				dump_ui_( stats,sub_tree );

			if( end)
			{ tabs--; print_tabs(stats, tabs);	blob_printf(stats->b, "%s\n", end); }
		}
		else  if ( type == VEVO_ATOM_TYPE_STRING )
		{
			char *str = vevo_property_get_string( rootnode, items[i] );
			print_tabs( stats,tabs );
			blob_printf(stats->b,"<property name=\"%s\">%s</property>\n",items[i],str );
			free(str);
		}

		free(items[i]);
	}
	free(items);
}

void 	concretebuilder_formalize2( void *stats, void *rootnode )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	blob_printf( n->b, "<?xml version=\"1.0\" standalone=\"no\"?> <!--*- mode: xml -*-->\n<!DOCTYPE glade-interface SYSTEM \"http://glade.gnome.org/glade-2.0.dtd\">\n<glade-interface>\n");

	void *root = NULL;
	int error = vevo_property_get( rootnode, "parent", 0, &root );
	if( error )
		return;

	window_dump_properties( n, root );
	dump_ui_( n, rootnode );

	blob_printf( n->b, "</widget>\n</glade-interface>\n\n");
}


void 	concretebuilder_formalize( void *stats, void *rootnode )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	blob_printf( n->b, "<?xml version=\"1.0\" standalone=\"no\"?> <!--*- mode: xml -*-->\n<!DOCTYPE glade-interface SYSTEM \"http://glade.gnome.org/glade-2.0.dtd\">\n<glade-interface>\n");

	void *root = NULL;
	int error = vevo_property_get( rootnode, "parent", 0, &root );
	if( error )
		return;

	window_dump_properties( n, root );
	dump_ui_( n, rootnode );

	blob_printf( n->b, "</widget>\n</glade-interface>\n\n");
}

void	concretebuilder_reap( void *stats )
{
	ui_stats_t	*n = (ui_stats_t*) stats;

	blob_wipe( n->b );
}

char	*concretebuilder_get_buffer( void *stats )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	return blob_get_buffer( n->b );
}


int	concretebuilder_get_size( void *stats )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	return blob_get_buffer_size( n->b );

}

char	*concretebuilder_get_format(void *stats,const char *identifier)
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	void *method = NULL;
	
	if(vevo_property_get( n->methods, identifier,0,&method) != VEVO_NO_ERROR)
		return NULL;
	
	return vevo_property_get_string( method, "format" );
}
char	*concretebuilder_get_signalname(void *stats,const char *identifier)
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	void *method = NULL;
	
	if(vevo_property_get( n->methods, identifier,0,&method) != VEVO_NO_ERROR)
		return NULL;
	
	return vevo_property_get_string( method, "signal" );
}

double	concretebuilder_get_valueafter(void *stats,const char *identifier, int *error)
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	void *method = NULL;
	
	*error = vevo_property_get( n->methods, identifier,0,&method);
        if(*error != VEVO_NO_ERROR)
		return 0.0;

	double val = 0.0;
	*error = vevo_property_get( method, "default",0,&val );
	return val;	
}
char	*concretebuilder_get_tooltip(void *stats,const char *identifier )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	void *method = NULL;
	
	if(vevo_property_get( n->methods, identifier,0,&method) != VEVO_NO_ERROR)
		return NULL;
	
	return vevo_property_get_string( method, "tooltip" );
}

char	*concretebuilder_get_oscpath(void *stats,const char *identifier )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	void *method = NULL;
	
	if(vevo_property_get( n->methods, identifier,0,&method) != VEVO_NO_ERROR)
		return NULL;
	
	return vevo_property_get_string( method, "OSC_Path" );
}

char	**concretebuilder_get_full( void *stats )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	return vevo_list_properties( n->methods );
}

int	concretebuilder_register_method( void *stats, const char *name, void *ptr , const char *widget_name )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	
	char *path = concretebuilder_get_oscpath( stats, name );
	char *format = concretebuilder_get_format( stats ,name);
	
	char key[64];
	sprintf(key, "%p", ptr );

	if(path)	
	{
		vevo_property_set( n->registry, key, VEVO_ATOM_TYPE_STRING,1, &path );
		vevo_property_set( n->update_signals, path, VEVO_ATOM_TYPE_VOIDPTR,1,&ptr );
		free(path);
	}
	else
	{
		vevo_property_set( n->registry, key, VEVO_ATOM_TYPE_STRING,0, NULL );
	}

	if( format )
	{
		sprintf(key, "%p_fmt", ptr );
		vevo_property_set( n->registry, key, VEVO_ATOM_TYPE_STRING,1, &format );
	}

	sprintf(key, "%p_widget",ptr);
	vevo_property_set( n->registry, key, VEVO_ATOM_TYPE_STRING,1,&widget_name);

	return VEVO_NO_ERROR;
}

void	*concretebuilder_widget_by_path( void *stats, const char *path )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	void *widget = NULL;
	vevo_property_get( n->update_signals, path, 0,&widget );
	return widget;
}

char	*concretebuilder_get_signaldata( void *stats, void *ptr )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	char key[64];
	sprintf(key, "%p", ptr );

	return vevo_property_get_string( n->registry, key );
}
char	*concretebuilder_get_signalformat( void *stats, void *ptr )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	char key[64];
	sprintf(key, "%p_fmt", ptr );

	return vevo_property_get_string( n->registry, key );
}

char	*concretebuilder_get_signalwidget( void *stats, void *ptr )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	char key[64];
	sprintf(key, "%p_widget", ptr );

	return vevo_property_get_string( n->registry, key );
}

void	concretebuilder_register_as( void *stats, const char *key, void *ptr)
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	vevo_property_set( n->symbols, key, VEVO_ATOM_TYPE_VOIDPTR,1, &ptr );
}

void	concretebuilder_register_empty( void *stats, const char *key )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	vevo_property_set( n->symbols, key, VEVO_ATOM_TYPE_VOIDPTR,0, NULL );
}

void	*concretebuilder_from_register( void *stats, const char *key )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	void	        *ptr = NULL;
	if( vevo_property_get( n->symbols,key,0,&ptr ) == VEVO_NO_ERROR )
	{
		return ptr;
	}
	return NULL;
}


void	concretebuilder_register_gladeptr( void *stats, const char *key, void *ptr)
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	vevo_property_set( n->glade, key, VEVO_ATOM_TYPE_VOIDPTR,1, &ptr );
}

void	*concretebuilder_gladeptr_from_register( void *stats, const char *key )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	void	        *ptr = NULL;
	if( vevo_property_get( n->glade,key,0,&ptr ) == VEVO_NO_ERROR )
		return ptr;
	return NULL;
}

char	*concretebuilder_get_identifier( void *stats, void *widget )
{
	ui_stats_t	*n = (ui_stats_t*) stats;
	char key[64];
	sprintf(key, "%p", widget );
	
	return vevo_property_get_string( n->mapping, key );
}
