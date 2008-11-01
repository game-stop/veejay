/* veejay - Linux VeeJay
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

#ifndef BUILDER_H
#define BUILDER_H

#define LABEL 0
#define WINDOW 1
#define VBOX 2
#define FRAME 3
#define BUTTON 4
#define HBUTTONBOX 5
#define ALIGN 6
#define PACKING 7
#define TOGGLEBUTTON 8
#define RADIOBUTTON 9
#define VSCALE 10
#define HSCALE 11
#define CHECKBUTTON 12
#define COMBOBOX 13
#define SPINBUTTON 14
#define HBOX 15
#define SCROLLEDWINDOW 16
#define VIEWPORT 17
#define FRAMELABEL 18
#define MENUBAR 19
#define MENU 20
#define MENUITEM 21
//@ todo: builder can be renamed to builder or factor, what design pattern it matches
//@       the live_builder can be made private, only 'builder' uses it
//@       stats allocation is done by builder ('builder')


int		builder_get_widget_type( const char *name );

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
						const char *tooltip);


void		*builder_new_boolean_object( void *stats, void *container,
						const char *name,
						int	    active,
	       					int	    widget_type,
					 	const char *osc_path,
						const char *tooltip	);

void		*builder_new_pulldown_object( void *stats, void *container,
						const char *name,
						const char *id,
						int	   widget_type,
						int	    n_items,
						char	    **items,
						const char *osc_path,
						const char *format,
						double	    row,
						const char *tooltip	);

void		*builder_new_button_object( void *stats, void *container,
						const char *name,
	       					int	    widget_type,
						const char *osc_path,
						const char *tooltip	);

void		*builder_new_boolean_group_object( void *stats, void *container,
						const char *prefix,
						const char *label_prefix,
						int	   selected,
						int	    n_buttons );

void		*builder_new_label_object( void *stats, void *container,
						const char *name, const char *wname );

char  		*builder_formalize( void *stats, void *rootnode, int *size );

void		*builder_new_frame( void *stats, void *container, const char *name, int box_type, const char *my_frame, int colapse );
void		*builder_new_box( void *stats, void *container, int box_type );
void		*builder_new_window( void *stats, const char *label );
void		builder_destroy_rootnode( void *stats, void *rootnode );
void		builder_free( void *stats);
void		*builder_init();
void		 builder_writefile( void *stats, const char *filename );
char		*builder_get_signalname( void *stats, const char *id );
char		*builder_get_oscpath( void *stats, const char *id );
char	       **builder_get_full( void *stats );
char		*builder_get_signalname( void *stats, const char *id );
double		builder_get_value_after( void *stats, const char *id, int *error);
char		*builder_get_format(void *stats, const char *id );
int		builder_register_method( void *stats, const char *name, void *ptr, const char *widgetname);
char		*builder_get_signaldata( void *stats, void *ptr);
char		*builder_get_signalformat( void *stats, void *ptr);
char		*builder_get_signalwidget( void *stats, void *ptr);
void		builder_register_widget( void *stats, const char *name, void *ptr );
void		builder_unregister_widget( void *stats, const char *name );
void		*builder_from_register( void *stats, const char *key );
void		builder_register_glade( void *stats, const char *key, void *ptr);
void		*builder_glade_from_register( void *stats, const char *key );
char		*builder_get_identifier( void *stats, void *widget);
char		*builder_get_tooltip( void *stats, const char *id);
void		*concretebuilder_widget_by_path( void *stats, const char *path );
#endif
