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

#include <gdk/gdk.h>

#define DEFAULT_PORT_NUM 3490

int veejay_tick();
void veejay_preview(int p);
int veejay_update_multitrack();
void veejay_sleep( void *ui );
void reloaded_restart();
void *get_ui_info();
char *get_glade_path();
char *get_gveejay_dir();
int get_total_frames();
void get_gd(char *buf, char *suf, const char *filename);

void set_disable_sample_image(gboolean status);

void default_bank_values(int *col, int *row );
gboolean is_alive( int *sync );

int _effect_get_minmax( int effect_id, int *min, int *max, int index );
void register_signals();
int is_button_toggled(const char *name);
gchar *_utf8str( const char *c_str );

void vj_gui_cb(int state, char *hostname, int port_num);
void vj_gui_init(const char *glade_file, int launcher, char *hostname, int port_num, int threads,int load_midi, char *midi_file, gboolean beta, gboolean autoconnect);
int vj_gui_reconnect( char *host, char *group, int port);
void vj_gui_free();
void vj_gui_wipe();
void vj_gui_enable(void);
void vj_gui_disable(void);
void vj_gui_disconnect(void);
int vj_get_preview_box_w();
int vj_get_preview_box_h();
void vj_gui_set_geom(int x, int y);
void vj_gui_set_debug_level(int level, int preview_p, int pw, int ph);
void vj_gui_set_timeout(int timer);
int vj_gui_sleep_time( void );
void vj_gui_style_setup();

int vj_img_cb(GdkPixbuf *img );
void vj_fork_or_connect_veejay();
void vj_event_list_free();
gboolean gveejay_running();
gboolean gveejay_relaunch();
int gveejay_user_preview();
void gveejay_preview(int p);
int gveejay_restart();
int gveejay_update();
int gveejay_time_to_sync( void *ptr );
int update_gveejay();
void reloaded_show_launcher();
void reloaded_restart ();
#endif
