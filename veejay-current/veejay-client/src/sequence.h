/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2006 Niels Elburg <nwelburg@gmail.com> 
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

#ifndef PREVIEWH
#define PREVIEWH
typedef struct
{
	int tracks;
	int master;
	int *widths;
	int *heights;
	int **status_list;
	GdkPixbuf **img_list;
} sync_info;

void		*gvr_preview_init(int max_tracks, int use_thread);
int		gvr_track_connect( void *preview, char *hostname, int port_num, int *track_num );
void		gvr_track_disconnect( void *preview, int track_num );
int		gvr_track_configure( void *preview, int track_num, int w, int h);
int		gvr_track_toggle_preview( void *preview, int track_num, int status );
void		gvr_need_track_list( void *preview, int track_id );

int		gvr_get_stream_id( void  *data, int id );
void		gvr_set_master( void *preview, int master_track );
//format and queue vims messages from extern

void		gvr_queue_mmvims( void *preview, int track_id, int vims_id, int val1,int val2 );
void		gvr_queue_mvims( void *preview, int track_id, int vims_id, int val );
void		gvr_queue_vims( void *preview, int track_id, int vims_id );

void		gvr_queue_cxvims( void *preview, int track_id, int vims_id, int val1,unsigned char *val2 );

int             gvr_track_already_open( void *preview, const char *hostname,        int port );

int          gvr_get_preview_status( void *preview, int track_num );

char*        gvr_track_get_hostname( void *preview , int num );

int          gvr_track_get_portnum( void *preview, int num);

int		gvr_track_test( void *preview, int track_id );

sync_info	*gvr_sync( void *preview );

#endif


