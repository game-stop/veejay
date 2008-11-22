/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2007 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef VMIDI_H
#define VMIDI_H

void    *vj_midi_new(void *mw);

int    vj_midi_handle_events(void *vv);

void    vj_midi_play(void *vv );

void    vj_midi_learn( void *vv );

void    vj_midi_load(void *vv, const char *filename);

void    vj_midi_save(void *vv, const char *filename);

void	vj_midi_reset( void *vv );

void    vj_midi_learning_vims( void *vv, char *widget, char *msg, int extra );
void    vj_midi_learning_vims_simple( void *vv, char *widget, int id );
void    vj_midi_learning_vims_complex( void *vv, char *widget, int id, int first , int extra );
void    vj_midi_learning_vims_fx( void *vv, int widget, int id, int a, int b, int c, int extra );
void    vj_midi_learning_vims_msg2(void *vv, char *widget, int id, int arg, int b );
void    vj_midi_learning_vims_msg( void *vv, char *widget, int id, int arg );
void    vj_midi_learning_vims_spin( void *vv, char *widget, int id );
#endif
