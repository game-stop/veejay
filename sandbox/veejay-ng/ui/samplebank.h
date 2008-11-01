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
#ifndef UISAMPLEBANK
#define UISAMPLEBANK



void	*samplebank_new( void *sender, gint columns, gint rows );
void	samplebank_add_page( GtkWidget *pad );
void	samplebank_store_sample( GtkWidget *notebook, const int id, const char *title );
void	samplebank_store_in_combobox( GtkWidget *combo_boxA );



void		channelbank_free(void *cb);
GtkWidget	*channelbank_get_container(void *cb);
void *channelbank_new( void *sender,char *path, gint columns, const char **strs );


#endif
