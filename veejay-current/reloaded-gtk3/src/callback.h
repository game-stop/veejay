/* gveejay - Linux VeeJay - GVeejay GTK+-2/Glade User Interface
 *           (C) 2002-2015 Niels Elburg <nwelburg@gmail.com>
 *           (C) 2016 Jérôme Blanchi
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

#ifndef VJCALLBACK_H
#define VJCALLBACK_H


#define	SLIDER_CHANGED( arg_num, value ) \
{\
if(!info->status_lock && !info->parameter_lock)\
{\
info->parameter_lock = 1;\
multi_vims( VIMS_CHAIN_ENTRY_SET_ARG_VAL, "%d %d %d %d", 0, info->uc.selected_chain_entry,arg_num, value );\
vj_midi_learning_vims_fx( info->midi, arg_num, VIMS_CHAIN_ENTRY_SET_ARG_VAL, 0,info->uc.selected_chain_entry, arg_num,1 );\
if(info->uc.selected_rgbkey) update_rgbkey_from_slider(); \
int *entry_tokens = &(info->uc.entry_tokens[0]);\
update_label_str( "value_friendlyname", _effect_get_hint( entry_tokens[ENTRY_FXID], arg_num, value ));\
info->parameter_lock = 0;\
}\
}

#define	PARAM_CHANGED( arg_num, fraction, name ) \
{\
if(!info->status_lock && !info->parameter_lock)\
{\
info->parameter_lock = 1;\
multi_vims( VIMS_CHAIN_ENTRY_SET_ARG_VAL, "%d %d %d %d", 0, info->uc.selected_chain_entry,arg_num, (get_slider_val(name) + fraction) );\
update_slider_value( name, (get_slider_val(name) + fraction), 0 );\
vj_midi_learning_vims_fx( info->midi, arg_num, VIMS_CHAIN_ENTRY_SET_ARG_VAL, 0, info->uc.selected_chain_entry,arg_num,2 );\
if(info->uc.selected_rgbkey) update_rgbkey_from_slider(); \
int *entry_tokens = &(info->uc.entry_tokens[0]);\
update_label_str( "value_friendlyname", _effect_get_hint( entry_tokens[ENTRY_FXID], arg_num, get_slider_val(name) ));\
info->parameter_lock = 0;\
}\
}


#define KF_CHANGED( arg_num ) \
{\
enable_widget("fxanimcontrols");\
if(arg_num != info->uc.selected_parameter_id)\
{\
vj_kf_select_parameter(arg_num);\
}\
}

/*int sample_calctime();*/
void text_defaults();

gboolean boxbg_draw ( GtkWidget *w,  cairo_t *cr);
gboolean boxfg_draw ( GtkWidget *w, cairo_t *cr );
gboolean boxln_draw ( GtkWidget *w, cairo_t *cr );
gboolean boxred_draw ( GtkWidget *w, cairo_t *cr );
gboolean boxblue_draw ( GtkWidget *w, cairo_t *cr );
gboolean boxgreen_draw ( GtkWidget *w, cairo_t *cr );


void on_timeline_value_changed ( GtkWidget *widget, gpointer user_data );
void on_timeline_in_point_changed ( GtkWidget *widget, gpointer user_data );
void on_timeline_out_point_changed ( GtkWidget *widget, gpointer user_data );
void on_timeline_bind_toggled( GtkWidget *widget, gpointer user_data );
void on_timeline_cleared ( GtkWidget *widget, gpointer user_data );

#endif
