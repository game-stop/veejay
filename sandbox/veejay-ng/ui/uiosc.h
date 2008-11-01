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
#ifndef UIOSC_H
#define UIOSC_H
void	ui_print_help(void);
void	ui_free_osc_server( void *dosc);
void	*ui_new_osc_server(void *data , const char *port);

int     ui_send_osc( void *osc ,const char *msg, const char *format, ... );
void    ui_free_osc_sender( void *dosc );
void    *ui_new_osc_sender( const char *addr, const char *port );


#endif
