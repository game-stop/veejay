/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#define MAX_CHAIN_LEN	20

#include "gtk3curve.h"

int	set_points_in_curve_ext( GtkWidget *curve, unsigned char *blob, int id, int fx_entry, int *lo, int *hi, int *ct, int *status);
void	set_points_in_curve( Gtk3CurveType type, GtkWidget *curve);
void	reset_curve( GtkWidget *curve );
void	get_points_from_curve( GtkWidget *curve, int len, float *v );

#endif
