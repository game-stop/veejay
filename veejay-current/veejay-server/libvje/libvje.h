/* 
 * veejay  
 *
 * Copyright (C) 2019 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
#ifndef VJELIB_H
#define VJELIB_H

// set/get pixel value ranges
unsigned int get_pixel_range_min_UV(); 
unsigned int get_pixel_range_min_Y(); 
void vje_set_pixel_range(uint8_t Yhi,uint8_t Uhi, uint8_t Ylo, uint8_t Ulo);
void vje_set_rgb_parameter_conversion_type(int full_range);
int vje_get_rgb_parameter_conversion_type();
void vje_set_bg(VJFrame *bg);
// enable/disable running multithreaded FX 
void vje_enable_parallel(); 
void vje_disable_parallel();

// init all FX descriptors
int vje_init(int w, int h);

// FX calls
void *vje_fx_malloc(int fx_id, int chain_id, int chain_pos, int w, int h, int *error );
void vje_fx_prepare( int fx_id, void *ptr, VJFrame *A );
void vje_fx_apply( int fx_id, void *ptr, VJFrame *A, VJFrame *B, int *args );
void vje_fx_free( int fx_id, int chain_id, int chain_pos, void *ptr );
int vje_fx_is_transition_ready( int fx_id, void *ptr, int w, int h );
uint8_t *vje_fx_get_bg( int fx_id, void *ptr, unsigned int plane);

// informative
int vje_get_last_id();
int vje_max_effects();
int vje_max_space();
int vje_get_info(int fx_id, int *is_mixer, int *n_params, int *rgba_only);
int vje_is_plugin( int fx_id ); 
int vje_get_num_params( int fx );
const char *vje_get_description( int fx_id );
const char *vje_get_param_description( int fx_id, int param_nr );
int vje_get_param_default( int fx_id, int param_nr );
int vje_get_param_min_limit( int fx_id, int param_nr );
int vje_get_param_max_limit( int fx_id, int param_nr );
int vje_get_extra_frame( int fx_id );
int vje_is_param_value_valid( int fx_id, int param_nr, int value );
int vje_has_rgbkey( int fx_id );
int vje_is_valid( int fx_id );
int vje_get_param_hints_length( int fx_id, int p, int limit );
int vje_get_subformat( int fx_id );
int vje_get_plugin_id(int fx_id);

// textual (VIMS output)
int vje_get_summarylen(int fx_id);
int vje_get_summary(int fx_id, char *dst);

void vje_dump();

// processing
void vjert_apply( void *entry, VJFrame **frames, int chain_id, int chain_position, int *args ); //FIXME
void vjert_del_fx( void *ptr, int chain_id, int chain_position ); //FIXME
void vjert_update( void *ptr, VJFrame *frame );

#endif
