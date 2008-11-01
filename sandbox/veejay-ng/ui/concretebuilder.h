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

#ifndef CONCRETEBUILDER_H
#define CONCRETEBUILDER_H


void	*concretebuilder_init( void );

void	concretebuilder_free( void *stats );

int	concretebuilder_change_default( void *properties, const char *key, const char *value );

void	*concretebuilder_new_widget(void *ui, void *container,const char *identfier, int type, const char *osc_path, const char *format, const char *signalname, double *dv, const char *tooltip);
void	*concretebuilder_new_collapsed_widget(void *ui, void *container,const char *identifier, int type, const char *OSC_Path, const char *format,const char *signalname, double *dv, const char *tooltip);
void 	concretebuilder_formalize(void *ui, void *rootnode );

char	*concretebuilder_get_buffer( void *stats );

int	concretebuilder_get_size( void *stats );

void	concretebuilder_destroy_container( void *stats, void *rootnode );

char	*concretebuilder_lookup_signalname( int widget_type );

char	*concretebuilder_get_signalname(void *stats, const char *identifier);

char	*concretebuilder_get_oscpath(void *stats, const char *identifier );
char	*concretebuilder_get_format(void *stats, const char *identifier );

char	**concretebuilder_get_full( void *stats );

int	concretebuilder_register_method( void *stats, const char *name, void *ptr, const char *widget_name);

char	*concretebuilder_get_signaldata( void *stats, void *ptr );

void	concretebuilder_new_packing( void *container, int expand, int filled );

void	concretebuilder_register_as( void *stats, const char *key, void *ptr);
void	concretebuilder_register_empty( void *stats, const char *key );

void	*concretebuilder_from_register( void *stats, const char *key );

char	*concretebuilder_get_signalformat( void *stats, void *ptr );
double	concretebuilder_get_valueafter(void *stats,const char *identifier, int *error);

void	concretebuilder_register_gladeptr( void *stats, const char *key, void *ptr);

void	concretebuilder_set_default_sample( void *stats, const char *label);
int	concretebuilder_get_default_sample( void *stats );

void	*concretebuilder_gladeptr_from_register( void *stats, const char *key );
char	*concretebuilder_get_tooltip(void *stats,const char *identifier );
char	*concretebuilder_get_identifier( void *stats, void *widget );
#endif
