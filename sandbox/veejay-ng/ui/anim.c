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
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

typedef struct
{
	GtkWidget *curve;
	char *osc_path;
	char *types;
	double length;
	gfloat *vec;
	void *sender;
} anim_t;

void	*anim_new( void *sender, char *osc_path, char *types )
{
	anim_t *anim = (anim_t*)malloc(sizeof(anim_t));
	anim->curve = gtk_curve_new();
	anim->osc_path = strdup( osc_path );
	anim->types = strdup(types);
	anim->length =0.0;
	anim->vec = NULL;
	anim->sender = sender;
	return (void*) anim;
}

void	anim_destroy( void *danim )
{
	anim_t *anim =(anim_t*) danim;
	if(anim->vec)
		free(anim->vec);
	free(anim->osc_path);
	free(anim->types );
}

void	 anim_change_curve( void *danim, int type )
{
	GtkCurveType curve_type = GTK_CURVE_TYPE_FREE;
	anim_t *anim =(anim_t*) danim;
	if(type==1)
		curve_type = GTK_CURVE_TYPE_LINEAR;
	else
		curve_type = GTK_CURVE_TYPE_SPLINE;
	
	gtk_curve_set_curve_type( anim->curve, curve_type );
}

void	 anim_set_range( void *danim, double min_x, double max_x, 
			 double min_y, double max_y )
{
	anim_t *anim =(anim_t*) danim;
	if(anim->vec)
		free(anim->vec);
	
	gtk_curve_set_range( anim->curve,
			(gfloat) min_x, (gfloat) max_x,
			(gfloat) min_y, (gfloat) max_y );
	anim->length = (max_x - min_x);
	anim->vec    = (gfloat*) malloc(sizeof(gfloat) * ((int) anim->length));
}

void	 anim_clear( void *danim )
{
	anim_t *anim =(anim_t*) danim;
	gtk_curve_reset( anim->curve );	
}

void	 anim_update( void *danim )
{
	anim_t *anim =(anim_t*) danim;
	
	gtk_curve_get_vector( anim->curve,
			      (int) anim->length,
			      anim->vec );
}

char	anim_get_path( void *danim)
{
	anim_t *anim =(anim_t*) danim;
	return anim->osc_path;
}

GtkWidget	*anim_get( void *danim )
{
	anim_t *anim =(anim_t*) danim;
	return anim->curve;
}



void	anim_bang( void *danim, double position )
{
	anim_t *anim =(anim_t*) danim;
#ifdef STRICT_CHECKING
	assert( position >= 0 && position < anim->length );
#endif

	int idx = (int) position;
	
	gfloat value = anim->vec[idx];

	ui_send_osc_( anim->sender,
			anim->osc_path,
			anim->types,
			value );
}
