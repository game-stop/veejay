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

GdkPixbuf	*veejay_get_image( void *data , gint *error);

void	*veejay_sequence_init(int port, char *hostname, gint w, gint h);

void	veejay_configure_sequence( void *data, gint w, gint h );

void	veejay_sequence_free( void *data );

int	veejay_sequence_send( void *data , int vims_id, const char format[], ... );

void	veejay_toggle_image_loader( void *data, gint state );

void	veejay_get_status( void *data, guchar *dst );

gchar	*veejay_sequence_get_track_list( void *data, int slen, int *bytes_written );

void	veejay_abort_sequence( void *data );

void	veejay_sequence_preview_delay( void *data, double value );
