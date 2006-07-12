/*
 * Copyright (C) 2002-2006 Niels Elburg <nelburg@looze.net>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
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
#ifndef VEVOSAMPLE_H
#define VEVOSAMPLE_H
#define SAMPLE_CHAIN_LEN 2
typedef	struct
{
	int w;
	int h;
	int fmt;
	int inter;
	int sar_w;
	int sar_h;
	int norm;
	float fps;
	int rate;
	int  chans;
	int  bits;
	int  bps;
	int audio;
	int has_audio;
	int type;
} sample_video_info_t;

int	vevo_num_devices();
void	samplebank_init();
void	samplebank_free();
int	samplebank_size();
void	sample_delete_ptr(void *info);
int	sample_delete(int id);
void	 sample_set_property_ptr( void *ptr, const char *key, int atom_type, void *value );
void	sample_set_property( int sample_id, const char *key, int atom_type, void *value );
void	sample_get_property( int sample_id, const char *key, void *dst );
void	*sample_new( int type );
void	sample_free(int sample_id);
int	sample_open( void *sample, const char *token, int extra_token, sample_video_info_t *settings  );
int	sample_get_frame( void *current_sample, VJFrame *slot );
void	sample_fx_set_parameter( int id, int fx_entry, int param_id,int n, void *data );

void	sample_fx_get_parameter( int id, int fx_enty,int param_id,int idx, void *dst );
void	sample_toggle_process_entry( void *data, int fx_entry, int v );
int	sample_fx_set_active( int id, int fx_entry, int switch_value);
int	sample_fx_set_channel( int id, int fx_entry, int n_input, int channel_id );

int	sample_get_fx_alpha( void *data, int fx_entry );
void	sample_set_fx_alpha( void *data, int fx_entry, int v );
void	sample_set_itu601( void *current_sample, int status );


int	sample_fx_set( void *info, int fx_entry, const int new_fx );

void	sample_set_input_frame( void *info, int fx_entry, int seq_num, VJFrame *frame );
void	sample_process_fx_chain( void *srd );
int	sample_fx_del( int id, int fx_entry );

int	sample_xml_load( const char *filename, void *samplebank );
int	samplebank_xml_save( const char *filename );

void	*sample_get_fx_port( int id, int fx_entry );
int	sample_process_inplace( void *sample , int fx_entry );

void	sample_cache_data( void *sample_id );

void	sample_save_cache_data( void *sample );

int	sample_fx_set_in_channel( void *info, int fx_entry, int seq_num, const int sample_id );


int	sample_edl_get_audio_properties( void *current_sample, int *bits, int *bps, int *num_chans, long *rate );

int	sample_get_audio_frame( void *current_sample, void *buffer , int n);

uint64_t	sample_get_start_pos( void *sample );
uint64_t	sample_get_end_pos( void *sample );
int		sample_get_speed( void *sample );
uint64_t	sample_get_current_pos( void *sample );
int		sample_get_looptype( void *sample );
int		sample_has_audio( void *sample );

int		sample_valid_speed(void *sample, int new_speed);
int		sample_valid_pos(void *sample, uint64_t pos);

void	*sample_last(void);
void	*find_sample(int id);

const	char	*sample_describe_type( int type );

void	sample_fx_chain_reset( void *sample );
int	sample_fx_chain_entry_clear(void *sample, int id );

int	sample_sscanf_port( void *sample, const char *s );
char 	*sample_sprintf_port( void *sample );

int	sample_fx_push_in_channel( void *info, int fx_entry, int seq_num, void *frame_info );
int	sample_fx_push_out_channel(void *info, int fx_entry, int seq_num, void *frame_info );

int	sample_scan_out_channels( void *data, int fx_entry );
void	*sample_scan_in_channels( void *data, int fx_entry );
void	*sample_get_fx_port_ptr( void *data, int fx_entry );
void	*sample_get_fx_port_channels_ptr( int id, int fx_entry );

int		sample_get_key_ptr( void *sample );

int	sample_edl_delete( void *current_sample, uint64_t start, uint64_t end );

int	sample_edl_paste_from_buffer( void *current_sample, uint64_t insert_at );

int	sample_edl_cut_to_buffer( void *current_sample, uint64_t start_pos, uint64_t end_pos );


#endif
