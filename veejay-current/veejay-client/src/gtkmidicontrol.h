/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2026 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef GTK_MIDI_CONTROL_H
#define GTK_MIDI_CONTROL_H

#include <gtk/gtk.h>
#include "vj-midi-map.h"

G_BEGIN_DECLS

#define GVR_TYPE_MIDI_CONTROL            (gvr_midi_control_get_type())
#define GVR_MIDI_CONTROL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_MIDI_CONTROL, GvrMidiControl))
#define GVR_IS_MIDI_CONTROL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_MIDI_CONTROL))

typedef struct _GvrMidiControl GvrMidiControl;
typedef struct _GvrMidiControlClass GvrMidiControlClass;

GType      gvr_midi_control_get_type(void);
GtkWidget *gvr_midi_control_new(void *midi_context);
void       gvr_midi_control_refresh(GtkWidget *widget);
void       gvr_midi_control_midi_event(GtkWidget *widget, const VjMidiEvent *event);
void       gvr_midi_control_set_learning(GtkWidget *widget, gboolean learning);

G_END_DECLS

#endif
