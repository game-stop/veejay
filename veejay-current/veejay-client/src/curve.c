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
#include <config.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <veejay/vj-msg.h>
#include <veejay/vjmem.h>
#include <src/vj-api.h>
#include <stdlib.h>
#include "curve.h"
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
void	get_points_from_curve( GtkWidget *curve, int len, float *vec )
{
	gtk_curve_get_vector( GTK_CURVE(curve), len, vec );
}

void	reset_curve( GtkWidget *curve )
{
	gtk_curve_reset(GTK_CURVE(curve));
	gtk_curve_set_range( GTK_CURVE(curve), 0.0, 1.0, 0.0, 1.0 );
}

void	set_points_in_curve( int type, GtkWidget *curve)
{
	gtk_curve_set_curve_type( GTK_CURVE(curve), type );
}


int	set_points_in_curve_ext( GtkWidget *curve, unsigned char *blob, int id, int fx_entry, int *lo, int *hi, int *curve_type)
{
	int parameter_id = 0;
	int start = 0, end =0,type=0;
	int entry  = 0;
	int n = sscanf( (char*) blob, "key%2d%2d%8d%8d%2d", &entry, &parameter_id, &start, &end,&type );
	int len = end - start;
	int i;
	int min = 0, max = 0;
#ifdef STRICT_CHECKING
	assert( fx_entry == entry );
#endif

	if(n != 5 || len <= 0 )	
	{
		veejay_msg(0, "Error parsing FX Anim KF header");
		return -1;
	}

	_effect_get_minmax(id, &min, &max, parameter_id );

	unsigned int k = 0;
	unsigned char *in = blob + 25;
	float	*vec = (float*) vj_calloc(sizeof(float) * len );
	for(i = start ; i < end; i ++ )
	{
		unsigned char *ptr = in + (k * 4);
		int value = 
		  ( ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24) );
	

		float top = 1.0 / (float) max;
		float val = ( (float)value * top );

		veejay_msg(4, "Load FX Anim position %d, value %f", i, val );

		vec[k] = val;
		k++;
	}
	
	gtk_curve_set_vector( GTK_CURVE( curve ), len, vec );

	switch( type ) {
		case 1: *curve_type = GTK_CURVE_TYPE_SPLINE; break;
		case 2: *curve_type = GTK_CURVE_TYPE_FREE; break;
		default: *curve_type = GTK_CURVE_TYPE_LINEAR; break;
	}

	gtk_curve_set_curve_type( GTK_CURVE(curve), *curve_type );

	*lo = start;
	*hi = end;

	free(vec);

	return parameter_id;
}
