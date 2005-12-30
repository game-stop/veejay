/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
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

#ifndef VJCURVE_H
#define VJCURVE_H
#include <gtk/gtkversion.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#define MAX_PARAMETERS  8
#define MAX_CHAIN_LEN	20
typedef struct
{
	gint	parameter_id;
	gint	running;
	gfloat	*vector;
	gint	start_pos;
	gint	end_pos;	
	gint	curve_len;
	gint	type;
} key_parameter_t;

typedef struct
{
	key_parameter_t **parameters;
	gint		enabled;
} key_effect_t;

typedef struct
{
	key_effect_t **effects;
	gint		enabled;
} key_chain_t;


key_chain_t	*new_chain(void);
void		del_chain(key_chain_t *chain);
key_effect_t	*new_chain_entry(void);
void		del_chain_entry( key_effect_t *ke );
key_parameter_t *new_paramater_key( );
void		 free_parameter_key( key_parameter_t *key );

int		parameter_for_frame( key_parameter_t *key, gint frame_pos ); 
void		clear_parameter_values( key_parameter_t *key );
void		renew_parameter_key( key_parameter_t *key,gint id, gint start, gint end , gint run, gint min,gint max );
int	get_parameter_key_value( key_parameter_t *key, gint pos, float *res);
void		update_parameter_key( key_parameter_t *key, GtkWidget *c);
void		set_parameter_key( key_parameter_t *key, GtkWidget *c );
void		reset_curve( key_parameter_t *key,GtkWidget *c );
void		curve_timeline_changed( key_parameter_t *key, GtkWidget *curve);
#endif
