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

#include <gtk/gtk.h>

#define DEFAULT_PORT_NUM 3490

int veejay_tick(void);
void veejay_preview(int p);
int veejay_update_multitrack(void *);
void veejay_sleep( void *ui );
void reloaded_restart(void);
void *get_ui_info(void);
char *get_glade_path(void);
char *get_gveejay_dir(void);
int get_total_frames(void);
void get_gd(char *buf, char *suf, const char *filename);
char *format_selection_time(int start, int end);
void set_disable_sample_image(gboolean status);

void add_class(GtkWidget *widget, const char *name);
void remove_class(GtkWidget *widget, const char *name);
int set_samplebank_layout(int columns, int rows, int pages);
void default_bank_values(int *col, int *row );
void samplebank_goto_page(int page);
void samplebank_step_page(int delta);
int samplebank_get_page(void);
gboolean is_alive( int *sync );
gboolean gveejay_idle(gpointer data);
int _effect_get_minmax( int effect_id, int *min, int *max, int index );
void register_signals(void);
int is_button_toggled(const char *name);
gchar *_utf8str( const char *c_str );
void vj_gui_set_stylesheet(const char *css_file, gboolean small_as_possible); 
void vj_gui_cb(int state, char *hostname, int port_num);
void vj_gui_init(const char *glade_file, int launcher, char *hostname, int port_num, int threads,int load_midi, char *midi_file, gboolean beta, gboolean autoconnect, gboolean fasterui);
int vj_gui_reconnect( char *host, char *group, int port);
void vj_gui_free(void);
void vj_gui_wipe(void);
void vj_gui_enable(void);
void vj_gui_disable(void);
void vj_gui_disconnect(int restart_schedule);
int vj_get_preview_box_w(void);
int vj_get_preview_box_h(void);
void vj_gui_set_geom(int x, int y);
int vj_gui_is_connected(void);
int vj_gui_connected_to_master(void);
int vj_gui_upstream_master_info_known(void);
int vj_gui_connected_has_upstream_master(void);
int vj_gui_upstream_master_port(void);
const char *vj_gui_upstream_master_host(void);
int vj_gui_vims_forwarding_enabled(void);
int vj_gui_connected_port(void);
const char *vj_gui_connected_host(void);
double vj_gui_video_fps(void);
void vj_gui_update_sync_samplelist_sensitivity(void);
void vj_gui_set_debug_level(int level, int preview_p, int pw, int ph);
void vj_gui_set_timeout(int timer);
int vj_gui_sleep_time( void );
void vj_gui_style_setup(void);
gboolean vj_gui_tooltips_enabled(void);
void vj_gui_tooltips_set_enabled(gboolean enabled);
void vj_gui_widget_set_has_tooltip(GtkWidget *widget, gboolean has_tooltip);
void vj_gui_widget_set_tooltip_text(GtkWidget *widget, const char *text);
void vj_gui_widget_set_tooltip_markup(GtkWidget *widget, const char *markup);
gboolean vj_gui_tooltip_set_text(GtkWidget *widget, GtkTooltip *tooltip, const char *text);
gboolean vj_gui_tooltip_set_markup(GtkWidget *widget, GtkTooltip *tooltip, const char *markup);
void vj_gui_plain_sample_attention_start(void);
void vj_gui_sync_preview_toggle(int enabled);
void vj_gui_apply_multitrack_preview(GdkPixbuf *pixbuf);
void vj_gui_vims_observe_external(int id, const char format[], ...);
int vj_gui_switch_cached_track(int old_track, int new_track, const char *hostname, int port_num);
gboolean vj_gui_sequence_insert_source_at(int bank, int slot, int sample_id, int sample_type);
void vj_gui_sequence_clear_all_local(void);
void vj_gui_reveal_sequence_slot(int bank, int slot);
void vj_gui_reveal_source(int sample_id, int sample_type);

int vj_gui_vims_get_selected_action(int *event_id, int *key, int *modifier, char **args, int *is_bundle);
int vj_gui_vims_get_selected_bundle(int *event_id);
int vj_gui_vims_get_binding_target(int *event_id, char **args);
int vj_gui_vims_get_event_metadata(int event_id, int *params, const char **format, const char **description);
int vj_gui_vims_prompt_keybinding(int event_id, const char *initial_args, int *key, int *modifier, char **args);
void vj_gui_vims_execute_selected(void);
void vj_gui_vims_add_selected_to_bundle(void);
void vj_gui_vims_clear_response(void);
char *vj_gui_vims_editor_get_text(void);
void vj_gui_vims_editor_clear(void);

int vj_img_cb(GdkPixbuf *img );
void vj_fork_or_connect_veejay(void);
void vj_event_list_free(void);
gboolean gveejay_running(void);
gboolean gveejay_relaunch(void);
int gveejay_user_preview(void);
void gveejay_preview(int p);
int gveejay_restart(void);
int gveejay_update(void);
int gveejay_time_to_sync( void *ptr );
int update_gveejay(void);
void reloaded_show_launcher(void);
void reloaded_restart(void);



enum {
  FX_ANIM_SHAPE_NO_SHAPE ,
  FX_ANIM_SHAPE_BOUNCE ,
  FX_ANIM_SHAPE_BURST_ENVELOPE ,
  FX_ANIM_SHAPE_COSINE ,
  FX_ANIM_SHAPE_SINE ,
  FX_ANIM_SHAPE_DAMPED_SINE ,
  FX_ANIM_SHAPE_EASE_IN ,
  FX_ANIM_SHAPE_EASE_OUT ,
  FX_ANIM_SHAPE_EXPONENTIAL ,
  FX_ANIM_SHAPE_GAUSSIAN ,
  FX_ANIM_SHAPE_NOISE ,
  FX_ANIM_SHAPE_PULSE ,
  FX_ANIM_SHAPE_RAMP_DROP ,
  FX_ANIM_SHAPE_RANDOMWALK_BURST ,
  FX_ANIM_SHAPE_RANDOMWALK_INERTIA ,
  FX_ANIM_SHAPE_RANDOMWALK_MEAN ,
  FX_ANIM_SHAPE_RANDOMWALK_QUANTIZED ,
  FX_ANIM_SHAPE_RANDOMWALK_SMOOTH ,
  FX_ANIM_SHAPE_RANDOMWALK ,
  FX_ANIM_SHAPE_SAWTOOTH ,
  FX_ANIM_SHAPE_STEPS ,
  FX_ANIM_SHAPE_SMOOTHSTEP ,
  FX_ANIM_SHAPE_SMOOTH_NOISE ,
  FX_ANIM_SHAPE_SQUARE ,
  FX_ANIM_SHAPE_ZIGZAG ,
  FX_ANIM_SHAPE_MAX // sentinel
};


#endif
