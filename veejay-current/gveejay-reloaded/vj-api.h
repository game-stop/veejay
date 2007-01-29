/* gveejay - Linux VeeJay - GVeejay GTK+-2/Glade User Interface
 *           (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
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
 
#ifndef VJAPI_H
#define VJAPI_H

void	vj_gui_init(char *glade_file, int launcher, char *hostname, int port_num);
int	vj_gui_reconnect( char *host, char *group, int port);
void	vj_gui_free();
void	vj_fork_or_connect_veejay();
void	vj_gui_enable(void);
void	vj_gui_disable(void);
void	vj_gui_disconnect(void);
void	vj_gui_set_debug_level(int level, int preview_p, int pw, int ph);
void   get_gd(char *buf, char *suf, const char *filename);
void   vj_gui_theme_setup(int default_theme);
void   vj_gui_set_timeout(int timer);
void   set_skin(int skin);
void   default_bank_values(int *col, int *row );
void   vj_gui_style_setup();
gboolean       gveejay_running();
gboolean       is_alive( void );
int    vj_gui_sleep_time( void );
int    get_total_frames();
int    vj_img_cb(GdkPixbuf *img );


#endif 
