/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef GTKTIMESELECTION_H
#define GTKTIMESELECTION_H

#include <gtk/gtk.h>


#ifdef __cplusplus
extern "C"
{
#endif

#define TIMELINE_SELECTION(obj)           (G_TYPE_CHECK_INSTANCE_CAST(obj, timeline_get_type(), TimelineSelection ))

#define TIMELINE_SELECTION_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST( klass, timeline_get_type(), TimelineSelectionClass ))

#define IS_TIMELINE_SELECTION(obj)        (G_TYPE_CHECK_INSTANCE_TYPE( obj, timeline_get_type() ))

typedef struct _TimelineSelection TimelineSelection;
typedef struct _TimelineSelectionClass TimelineSelectionClass;

GType		timeline_get_type	(void);
GtkWidget*	timeline_new		(void);

gdouble		timeline_get_length	(TimelineSelection *te);
gdouble		timeline_get_pos	(TimelineSelection *te);
gdouble		timeline_get_in_point	(TimelineSelection *te );
gdouble		timeline_get_out_point  (TimelineSelection *te );
gboolean	timeline_get_selection  (TimelineSelection *te );
gboolean	timeline_get_bind	(TimelineSelection *te );

void		timeline_set_pos	(GtkWidget *widget, gdouble pos );

void		timeline_set_in_point	(GtkWidget *widget, gdouble pos);
void		timeline_set_out_point	(GtkWidget *widget, gdouble pos);

void		timeline_set_length	(GtkWidget *widget, gdouble length, gdouble pos);

void		timeline_set_bind	(GtkWidget *widget, gboolean active); 


void		timeline_clear_points( GtkWidget *widget );

void 		timeline_set_selection( GtkWidget *widget, gboolean active);


#ifdef __cplusplus
}
#endif
#endif
