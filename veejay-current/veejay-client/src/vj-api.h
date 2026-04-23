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
void default_bank_values(int *col, int *row );
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
void vj_gui_set_debug_level(int level, int preview_p, int pw, int ph);
void vj_gui_set_timeout(int timer);
int vj_gui_sleep_time( void );
void vj_gui_style_setup(void);

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
  FX_ANIM_SHAPE_ZIGZAG,
  FX_ANIM_SHAPE_SINE,
  FX_ANIM_SHAPE_COSINE,
  FX_ANIM_SHAPE_TRIANGLE,
  FX_ANIM_SHAPE_SAWTOOTH,
  FX_ANIM_SHAPE_REVERSE_SAWTOOTH,
  FX_ANIM_SHAPE_SQUARE,
  FX_ANIM_SHAPE_BOUNCE,
  FX_ANIM_SHAPE_NOISE,
  FX_ANIM_SHAPE_SMOOTHSTEP,
  FX_ANIM_SHAPE_RANDOMWALK,
  FX_ANIM_SHAPE_RANDOMWALK_INERTIA,
  FX_ANIM_SHAPE_RANDOMWALK_MEAN,
  FX_ANIM_SHAPE_RANDOMWALK_QUANTIZED,
  FX_ANIM_SHAPE_RANDOMWALK_BURST,
  FX_ANIM_SHAPE_RANDOMWALK_SMOOTH,
  FX_ANIM_SHAPE_GAUSSIAN,
  FX_ANIM_SHAPE_EXPONENTIAL,
  FX_ANIM_SHAPE_EASE_IN,
  FX_ANIM_SHAPE_EASE_OUT,
  FX_ANIM_SHAPE_PULSE,
  FX_ANIM_SHAPE_DAMPED_SINE,
  FX_ANIM_SHAPE_SMOOTH_NOISE,
  FX_ANIM_SHAPE_STEPS,
  FX_ANIM_SHAPE_RAMP_DROP,
  FX_ANIM_SHAPE_BURST_ENVELOPE,
  FX_ANIM_SHAPE_MAX // sentinel
};

static struct
{
    const char *description;
    const int id;
} fx_anim_shape_map[] =
{
    { "ZigZag",             FX_ANIM_SHAPE_ZIGZAG },
    { "Sine",               FX_ANIM_SHAPE_SINE },
    { "Cosine",             FX_ANIM_SHAPE_COSINE },
    { "Triangle",           FX_ANIM_SHAPE_TRIANGLE },
    { "Sawtooth",           FX_ANIM_SHAPE_SAWTOOTH },
    { "Reverse Sawtooth",   FX_ANIM_SHAPE_REVERSE_SAWTOOTH },
    { "Square",             FX_ANIM_SHAPE_SQUARE },
    { "Bounce",             FX_ANIM_SHAPE_BOUNCE },
    { "Noise",              FX_ANIM_SHAPE_NOISE },
    { "Smooth",             FX_ANIM_SHAPE_SMOOTHSTEP },
    { "Random Walk",        FX_ANIM_SHAPE_RANDOMWALK },
    { "Random Inertia",     FX_ANIM_SHAPE_RANDOMWALK_INERTIA },
    { "Random Quantized",   FX_ANIM_SHAPE_RANDOMWALK_QUANTIZED },
    { "Random Mean",        FX_ANIM_SHAPE_RANDOMWALK_MEAN },
    { "Random Burst",       FX_ANIM_SHAPE_RANDOMWALK_BURST },
    { "Random Smooth",      FX_ANIM_SHAPE_RANDOMWALK_SMOOTH },
    { "Gaussian",           FX_ANIM_SHAPE_GAUSSIAN },
    { "Exponential",        FX_ANIM_SHAPE_EXPONENTIAL },
    { "Ease In",            FX_ANIM_SHAPE_EASE_IN },
    { "Ease Out",           FX_ANIM_SHAPE_EASE_OUT },
    { "Pulse",              FX_ANIM_SHAPE_PULSE },
    { "Damped Sine",        FX_ANIM_SHAPE_DAMPED_SINE },
    { "Smooth Noise",       FX_ANIM_SHAPE_SMOOTH_NOISE },
    { "Shape Steps",        FX_ANIM_SHAPE_STEPS },
    { "Ramp Drop",          FX_ANIM_SHAPE_RAMP_DROP },
    { "Burst Envelope",     FX_ANIM_SHAPE_BURST_ENVELOPE },
    { NULL, -1 },
};

#endif
