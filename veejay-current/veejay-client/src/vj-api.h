/* gveejay - Linux VeeJay - GVeejay GTK+-2/Glade User Interface
 *           (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
int	veejay_tick();
void *get_ui_info();
void vj_gui_set_geom(int x, int y);
void vj_gui_init(char *glade_file, int launcher, char *hostname, int port_num, int threads,int load_midi, char *midi_file);
int	vj_gui_reconnect( char *host, char *group, int port);
void vj_gui_free();
void vj_fork_or_connect_veejay();
void vj_gui_wipe();
void vj_gui_enable(void);
void vj_gui_disable(void);
void vj_gui_disconnect(void);
void vj_gui_set_debug_level(int level, int preview_p, int pw, int ph);
void get_gd(char *buf, char *suf, const char *filename);
void vj_gui_theme_setup(int default_theme);
void vj_gui_set_timeout(int timer);
void set_skin(int skin, int invert);
void default_bank_values(int *col, int *row );
void vj_gui_style_setup();
gboolean gveejay_running();
gboolean is_alive( int *sync );
int vj_gui_sleep_time( void );
int get_total_frames();
int vj_img_cb(GdkPixbuf *img );
int	vj_get_preview_box_w();
int	vj_get_preview_box_h();
int _effect_get_minmax( int effect_id, int *min, int *max, int index );
void vj_gui_cb(int state, char *hostname, int port_num);
void veejay_preview(int p);	
int is_button_toggled(const char *name);	
gchar *_utf8str( const char *c_str );
void find_user_themes();
int	gveejay_user_preview();
char *get_glade_path();
char *get_gveejay_dir();
int	gveejay_restart();
int	gveejay_update();
int update_gveejay();
int veejay_update_multitrack( void *data );
void veejay_sleep( void *ui );
int gveejay_time_to_sync( void *ptr );
void gui_load_theme();
void register_signals();
void gveejay_preview(int p);

#endif 
