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

#include <config.h>
#include <stdint.h>
#include <veejay/vjmem.h>
#include <veejay/vims.h>
#include <veejay/vj-client.h>
#include <veejay/vj-msg.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <sys/time.h>
#include <veejay/libvevo.h>
#include <src/sequence.h>
#include <src/utils.h>
#include <libavutil/avutil.h>
#include <veejay/vje.h>
#include <veejay/yuvconv.h>
#include <string.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
extern void reloaded_schedule_restart();
typedef struct
{
	uint8_t *image_data[16];
	int *status_tokens[16];
	int	widths[16];	
	int	heights[16];
	int	active_list[16];
	int	frame_list[16];
} track_sync_t;

typedef struct
{
	char *hostname;
	int   port_num;
	vj_client *fd;
	uint8_t *data_buffer;
	uint8_t *tmp_buffer;
	uint8_t *status_buffer;		
	int	 track_list[16];
	int	 track_items;			//shared
	int	 status_tokens[32];	//shared
	int   	 active;
	int 	have_frame;
	int	grey_scale;
	int	preview;
	int	width;
	int	height;
	int	prevwidth;
	int	prevheight;
	int	prevmode;
	int	need_track_list;
	unsigned char 	*queue[16];
	int	n_queued;
	int	bw;
	int	is_master;
} veejay_track_t;

typedef struct
{
	void 	*lzo;
	veejay_track_t **tracks;
	int	n_tracks;
	int	state;
	track_sync_t *track_sync;
#ifdef STRICT_CHECKING
	int	locked;
	char	**locklist[256];
#endif
} veejay_preview_t;

static	int	sendvims( veejay_track_t *v, int vims_id, const char format[], ... );
static	int	recvvims( veejay_track_t *v, gint header_len, gint *payload, guchar *buffer );
static	int	veejay_get_image_data(veejay_preview_t *vp, veejay_track_t *v );
static	int	track_find(  veejay_preview_t *vp );
static int	veejay_process_status( veejay_preview_t *vp, veejay_track_t *v );
static int	gvr_preview_process_image( veejay_preview_t *vp, veejay_track_t *v );
static	int	track_exists( veejay_preview_t *vp, const char *hostname, int port_num, int *at );
static	int	gvr_preview_process_status( veejay_preview_t *vp, veejay_track_t *v );

static	GStaticRecMutex	mutex_ = G_STATIC_REC_MUTEX_INIT;

void		gvr_veejay_grabber_step( void *data );

static  float   get_ratio(int w, int h)
{
        return ( (float) w / (float) h);
}



void	*gvr_preview_init(int max_tracks, int use_threads)
{
	veejay_preview_t *vp = (veejay_preview_t*) vj_calloc(sizeof( veejay_preview_t ));
	GError *err = NULL;
	//vp->mutex = g_mutex_new();
	vp->tracks = (veejay_track_t**) vj_calloc(sizeof( veejay_track_t*) * max_tracks );
	vp->track_sync = (track_sync_t*) vj_calloc(sizeof( track_sync_t ));
	int i;
	for( i = 0; i < max_tracks; i++ )
		vp->track_sync->status_tokens[i] = (int*) vj_calloc(sizeof(int) * 32);

	vp->n_tracks = max_tracks;

	yuv_init_lib(0,0,0);

	return (void*) vp;
}		

static	void	gvr_close_connection( veejay_track_t *v )
{
   if(v)
   {
         veejay_msg(VEEJAY_MSG_WARNING, "Stopping VeejayGrabber to %s:%d",
                      v->hostname,v->port_num );
         vj_client_close(v->fd);
      	 vj_client_free(v->fd);
	 if(v->hostname) free(v->hostname);
         if(v->status_buffer) free(v->status_buffer);
         if(v->data_buffer) free(v->data_buffer);
	 if(v->tmp_buffer) free(v->tmp_buffer);
 
         free(v);
	 v= NULL;
   }
}

static	int	sendvims( veejay_track_t *v, int vims_id, const char format[], ... )
{
	gchar	block[255];	
	gchar	tmp[255];
	va_list args;
	gint    n;
	if( format == NULL )
	{
		g_snprintf( block, sizeof(block)-1, "%03d:;", vims_id );
		n = vj_client_send( v->fd, V_CMD, block );
		if( n <= 0 ) {
			if( n == -1 && v->is_master )	
				reloaded_schedule_restart();
			return 0;
		}
		return n;
	}

	va_start( args, format );
	vsnprintf( tmp, sizeof(tmp)-1, format, args );
	g_snprintf( block,sizeof(block)-1, "%03d:%s;", vims_id, tmp );
	va_end( args );
	
	n = vj_client_send( v->fd, V_CMD, block );
	if( n <= 0 ) {
		if( n == -1 && v->is_master )
			reloaded_schedule_restart();
	}
	return n;
}

static	int	recvvims( veejay_track_t *v, gint header_len, gint *payload, guchar *buffer )
{
	gint tmp_len = header_len + 1;
	unsigned char *tmp = vj_calloc( tmp_len );
	gint len = 0;
	gint n = vj_client_read( v->fd, V_CMD, tmp, header_len );

	if( n<= 0 )
	{
		if( n == -1 && v->is_master)
			reloaded_schedule_restart();
		veejay_msg(0,"Reading header of %d bytes: %d", header_len,n );
		free(tmp);
		return 0;
	}

	if( sscanf( (char*)tmp, "%6d%1d", &len,&(v->grey_scale) )<=0)
	{
		veejay_msg(0, "Can't parse header (datastream polluted)");
		free(tmp);
		return 0;
	}

	if( len <= 0 )
	{
		free(tmp);
		veejay_msg(0, "Frame is empty");
		return 0;
	}
	
	gint bw = 0;
	gint bytes_read = len;
	unsigned char *buf_ptr = buffer;

	*payload = 0;

	while( bw < len )
	{
		n = vj_client_read( v->fd, V_CMD, buf_ptr, bytes_read );
		if ( n <= 0 )
		{
			if( n == -1 && v->is_master )
				reloaded_schedule_restart();
			veejay_msg(0, "Received %d out of %d bytes", bw,len);
			free(tmp);
			return 0;
		}
		bw += n;

		bytes_read -= n;
		buf_ptr += bw;
	}
	*payload = bw;

	free(tmp);
	return 1;
}


static unsigned char		*vims_track_list( veejay_track_t *v, int slen, int *bytes_written )
{
	unsigned char message[10];
	int tmp_len = slen + 1;
	unsigned char *tmp = vj_calloc( tmp_len );
 
	sprintf(message, "%03d:;", VIMS_TRACK_LIST );
	int ret = vj_client_send( v->fd, V_CMD, message );
	if( ret <= 0)
	{	
		if( ret == -1  && v->is_master )
			reloaded_schedule_restart();
		free(tmp);
		return NULL;
	}	

        ret = vj_client_read( v->fd, V_CMD, tmp, slen );
	if( ret <= 0 )
	{

		if( ret == -1 && v->is_master )
			reloaded_schedule_restart();
		free(tmp);
		return NULL;
	}

        int len = 0;
        sscanf( (char*) tmp, "%d", &len );
        unsigned char *result = NULL;

        if( len <= 0 || slen <= 0)
        {
		free(tmp);
	        return result;
	}

        result = (unsigned char*) vj_calloc(sizeof( unsigned char) * (len + 1) );
        int bytes_left = len;
        *bytes_written = 0;

        while( bytes_left > 0)
        {
                int n = vj_client_read( v->fd, V_CMD, result + (*bytes_written), bytes_left );
                if( n <= 0 )
  		{
			if( n == -1 && v->is_master )
				reloaded_schedule_restart();	
                        bytes_left = 0;
                }
                if( n > 0 )
                {
                        *bytes_written +=n;
                        bytes_left -= n;
                }
        }
	free(tmp);
        return result;
}


static int	veejay_process_status( veejay_preview_t *vp, veejay_track_t *v )
{

	unsigned char status_len[6];
	veejay_memset( status_len, 0, sizeof(status_len) );
	int k = -1;
	int n = 0;
	while( (k = vj_client_poll( v->fd, V_STATUS )) ) // is there a more recent message?
	{
		veejay_memset( status_len, 0, sizeof( status_len ) );
		n = vj_client_read(v->fd, V_STATUS, status_len, 5 );
		int bytes= 0;
#ifdef STRICT_CHECKING
	assert( status_len[0] == 'V' );
#endif

		if( status_len[0] != 'V' ) {
			veejay_msg(0, "Error parsing status message.");
			n = -1;
		}

		if( n == -1  && v->is_master )
			reloaded_schedule_restart();
		
		if( sscanf( status_len+1, "%03d", &bytes ) != 1 ) {
			veejay_msg(0, "Invalid status message.");
			bytes = 0;
			reloaded_schedule_restart();
		}

		if(bytes > 0 )
		{
			n = vj_client_read( v->fd, V_STATUS, v->status_buffer, bytes );
			if( n <= 0 ) {	
				if( n == -1 && v->is_master )
					reloaded_schedule_restart();		
									
				break;
			}
		}
	}
	if( k == -1 && v->is_master )
		reloaded_schedule_restart();
	
	veejay_memset( v->status_tokens,0, sizeof(sizeof(int) * 32));
	status_to_arr( v->status_buffer, v->status_tokens );	
/*


	gint n = vj_client_read( v->fd, V_STATUS, status_len, 5 );

	if( n <= 0 && v->is_master )
		reloaded_schedule_restart();

#ifdef STRICT_CHECKING
	assert( status_len[0] == 'V' );
#endif

	if( status_len[0] == 'V' )
	{
		gint bytes = 0;
		sscanf( status_len + 1, "%03d", &bytes );
		if( bytes > 0 )
		{
			veejay_memset( v->status_buffer,0, sizeof(v->status_buffer));
			n = vj_client_read( v->fd, V_STATUS, v->status_buffer, bytes );
			veejay_msg(0, " --> [%s]", v->status_buffer );
			if( n <= 0 )
			{
				if( n == -1 && v->is_master )
					reloaded_schedule_restart();
				veejay_msg(0, "Status message corrupted.");
				return 0;
			}
			int k = -1;
			while( (k = vj_client_poll( v->fd, V_STATUS )) ) // is there a more recent message?
			{
				veejay_msg(0,"More in buffer!");
				veejay_memset( status_len, 0, sizeof( status_len ) );
				n = vj_client_read(v->fd, V_STATUS, status_len, 5 );
				veejay_msg(0, "FLUSH %d [%s]",n,status_len );
				if( n == -1  && v->is_master )
					reloaded_schedule_restart();
				sscanf( status_len+1, "%03d", &bytes );
				if(bytes > 0 )
				{
					n = vj_client_read( v->fd, V_STATUS, v->status_buffer, bytes );
					veejay_msg(0, "FLUSH --> [%s]", v->status_buffer );
					if( n <= 0 ) {	
						if( n == -1 && v->is_master )
							reloaded_schedule_restart();		
												
						break;
					}
				}
			}
			if( k == -1 && v->is_master )
		
			reloaded_schedule_restart();
			veejay_memset( v->status_tokens,0, sizeof(sizeof(int) * 32));
			status_to_arr( v->status_buffer, v->status_tokens );	
			return 1;
		}
#ifdef STRICT_CHECKING
		else {
			assert(0);
		}
#endif
	}
	else
	{
		veejay_msg(0, "FATAL! cannot parse status message.");
		assert(0);
		int err = 0;

		while( ( n = vj_client_poll( v->fd, V_STATUS ))) 
		{
			char trashcan[100];
			int k = vj_client_read( v->fd, V_STATUS, trashcan,sizeof(trashcan));
#ifdef STRICT_CHECKING
			veejay_msg(VEEJAY_MSG_DEBUG, "Flushing %d bytes '%s'",k,trashcan);
#endif
			if( k <= 0 && v->is_master)
				err = -1;
		}

		if( n <= 0 && v->is_master )
			err = -1;
		
		if( err == -1)
			reloaded_schedule_restart();
	}*/
	return 1;
}
extern int     is_button_toggled(const char *name);
#define    RUP8(num)(((num)+8)&~8)

static	int	veejay_get_image_data(veejay_preview_t *vp, veejay_track_t *v )
{
	if(!v->have_frame && (v->width <= 0 || v->height <= 0) )
		return 1;
	gint res = sendvims( v, VIMS_RGB24_IMAGE, "%d %d", v->width,v->height );
	if( res <= 0 )
	{
		v->have_frame = 0;
		return 0;
	}
	gint bw = 0;

	res = recvvims( v, 7, &bw, v->data_buffer );
	if( res <= 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Can't get a preview image");
		v->have_frame = 0;
		return 0;
	}

	int expected_len = (v->width * v->height) + (v->width*v->height/2);

	if( bw != (v->width * v->height) && bw != expected_len )
	{
//@ try to fallback to a different resolution
		veejay_msg(VEEJAY_MSG_WARNING, "Corrupted image. Should be %dx%d but have %d bytes %s",
			v->width,v->height,abs(bw - expected_len),( (bw-expected_len<0)? "too few" : "too many") );
		v->have_frame = 0;
#ifdef STRICT_CHECKING
		assert(0);
#endif
		return 0;
	}

	uint8_t *in = v->data_buffer;
	uint8_t *out = v->tmp_buffer;
#ifdef STRICT_CHECKING
	if( v->grey_scale )
		assert( bw == (v->width * v->height));
	else
		assert( bw == ( (v->width*v->height) + (v->width*v->height)/2 ) );
#endif
	v->bw = 0;

	VJFrame *src1 = NULL;
	if( v->grey_scale == 0 )
		src1 = yuv_yuv_template( in, in + (v->width * v->height),
						      in + (v->width * v->height) + (v->width*v->height)/4 ,
						      v->width,v->height, PIX_FMT_YUV420P );
	else
		src1 = yuv_yuv_template( in, in, in, v->width,v->height, PIX_FMT_GRAY8 );

	
	if( (v->prevwidth != v->width) || (v->prevheight != v->height) || (v->prevmode != v->grey_scale ) ) {
		v->prevwidth = v->width;
		v->prevheight = v->height;
		v->prevmode = v->grey_scale;
		veejay_memset( in, 0, v->width*v->height);
		if(!v->grey_scale) {
			veejay_memset( in + (v->width*v->height), 128, ((v->width*v->height)/4));
			veejay_memset( in + (v->width*v->height) + ((v->width*v->height)/4),128,((v->width*v->height)/4));
		}
	}
	
	VJFrame *dst1 = NULL;

	dst1 = yuv_rgb_template( out, v->width,v->height, PIX_FMT_BGR24 );
	
	yuv_convert_any_ac( src1, dst1, src1->format, dst1->format );	
	v->have_frame = 1;

	free(src1);
	free(dst1);

	return bw;
}


static int	gvr_preview_process_status( veejay_preview_t *vp, veejay_track_t *v )
{
	if(!v)
		return 0;
	int tmp1 = 0;
	tmp1 = vj_client_poll( v->fd , V_STATUS );
 	if(tmp1)
	{
		int k =	 veejay_process_status( vp, v );
		if( k == -1 && v->is_master)
			reloaded_schedule_restart();
	}
	else if( tmp1 == -1 )
	{
		if(v->is_master)
			reloaded_schedule_restart();
		else
			gvr_close_connection(v);
	}
	return 0;
}

static int 	gvr_preview_process_image( veejay_preview_t *vp, veejay_track_t *v )
{
	if( veejay_get_image_data( vp, v ) == 0 )
		return 0;
	return 1;
}

void		gvr_set_master(void *data, int master_track )
{
	veejay_preview_t *vp = (veejay_preview_t*) data;
	int i;
	for( i = 0; i < vp->n_tracks; i ++ )
		if( vp->tracks[i] )
			vp->tracks[i]->is_master = 0;
	vp->tracks[master_track]->is_master = 1;
}

static	int	track_exists( veejay_preview_t *vp, const char *hostname, int port_num, int *at_track )
{
	int i;

	for( i = 0; i < vp->n_tracks ; i++ )
	{
		if( vp->tracks[i] )
		{	
			veejay_track_t *v = vp->tracks[i];
			if( strcasecmp( hostname, v->hostname ) == 0 && v->port_num == port_num )
			{
				if( at_track )
					*at_track = i;
				return 1;
			}
		}
	}
	return 0;
}

int		gvr_track_test( void *preview, int track_id )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	if( track_id < 0 || track_id > vp->n_tracks )
		return 0;
	return (vp->tracks[track_id]  ? 1:0);
}

static	int	track_find(  veejay_preview_t *vp )
{
	int i;
	int res = -1;
	for( i = 0;i < vp->n_tracks ;i ++ )
	{
		if( !vp->tracks[i] )
		{
			res = i;
			break;
		}
	}
	return res;
}

char*		gvr_track_get_hostname( void *preview , int num )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;

	if( vp->tracks[num] )
		return vp->tracks[num]->hostname;
	return NULL;
}

int		gvr_track_get_portnum( void *preview, int num) 
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;

	if( vp->tracks[num] )
		return vp->tracks[num]->port_num;
	return 0;
}

int		gvr_track_already_open( void *preview, const char *hostname,
	int port )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;

	if(track_exists( vp, hostname, port, NULL ) )
		return 1;
	return 0;
}

int		gvr_track_connect( void *preview, const char *hostname, int port_num, int *new_track )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int track_num = track_find( vp );

	if(track_num == -1)
	{
		vj_msg(0, "All tracks used.");
		return 0;
	}
	if(track_exists( vp, hostname, port_num, new_track ) )
	{
		vj_msg(VEEJAY_MSG_WARNING, "Veejay '%s':%d already in track %d", hostname, port_num, *new_track );
		return 0;
	}
	vj_client *fd = vj_client_alloc(0,0,0);
	if(!vj_client_connect( fd, hostname, NULL, port_num ) )
	{
		vj_msg(VEEJAY_MSG_ERROR, "Unable to connect to %s:%d", hostname, port_num );
		vj_client_free( fd );
		return 0;
	}
	
	veejay_track_t *vt = (veejay_track_t*) vj_calloc( sizeof(veejay_track_t));
	vt->hostname = strdup(hostname);
	vt->port_num  = port_num;
	vt->active   = 1;
	vt->fd       = fd;
	vt->preview  = is_button_toggled( "previewtoggle" );

	vt->status_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * 256);
	vt->data_buffer  = (uint8_t*) vj_calloc(sizeof(uint8_t) * 512 * 512 * 3 );
	vt->tmp_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * 512 * 512 * 3 );
	
	*new_track = track_num;

	vp->tracks[ track_num ] = vt;
	vp->track_sync->active_list[ track_num ] = 1;
	return 1;
}


static	void	gvr_single_queue_vims( veejay_track_t *v, int vims_id )
{
	char message[10];

	sprintf(message, "%03d:;", vims_id );

	if( v->n_queued < 16 )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}

static void	gvr_multi_queue_vims( veejay_track_t *v, int vims_id, int val )
{
	char message[10];

	sprintf(message, "%03d:%d;", vims_id,val );

	if( v->n_queued < 16 )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}
static	void	gvr_multivx_queue_vims( veejay_track_t *v, int vims_id, int val1,unsigned char *val2 )
{
	char message[300];

	sprintf(message, "%03d:%d %s;", vims_id,val1,val2 );

	if( v->n_queued < 16 )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}

static	void	gvr_multiv_queue_vims( veejay_track_t *v, int vims_id, int val1,int val2 )
{
	char message[10];

	sprintf(message, "%03d:%d %d;", vims_id,val1,val2 );

	if( v->n_queued < 16 )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}
void		gvr_queue_cxvims( void *preview, int track_id, int vims_id, int val1,unsigned char *val2 )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int i;

	if( track_id == -1 )
	{
		for( i = 0; i < vp->n_tracks; i ++ )
			if( vp->tracks[i] && vp->tracks[i]->active )
				gvr_multivx_queue_vims( vp->tracks[i], vims_id,val1,val2 );
	}
	else
	{
		if( vp->tracks[track_id] && vp->tracks[track_id]->active)
			gvr_multivx_queue_vims( vp->tracks[track_id], vims_id,val1,val2 );
	}
}

void		gvr_queue_vims( void *preview, int track_id, int vims_id )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int i;

	if( track_id == -1 )
	{
		for( i = 0; i < vp->n_tracks; i ++ )
			if( vp->tracks[i]  && vp->tracks[i]->active )
				gvr_single_queue_vims( vp->tracks[i], vims_id );
	}
	else
	{
		if( vp->tracks[track_id] && vp->tracks[track_id]->active)
			gvr_single_queue_vims( vp->tracks[track_id], vims_id );
	}
}

void		gvr_queue_mvims( void *preview, int track_id, int vims_id, int val )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int i;

	if( track_id == -1 )
	{
		for( i = 0; i < vp->n_tracks ; i ++ )
			if( vp->tracks[i] && vp->tracks[i]->active )
				gvr_multi_queue_vims( vp->tracks[i], vims_id,val );
	}
	else
	{
		if( vp->tracks[track_id] && vp->tracks[track_id]->active )
			gvr_multi_queue_vims( vp->tracks[track_id], vims_id,val );
	}
}

void		gvr_need_track_list( void *preview, int track_id )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	veejay_track_t *v = vp->tracks[track_id];
	if(v)
		v->need_track_list = 1;
}

void		gvr_queue_mmvims( void *preview, int track_id, int vims_id, int val1,int val2 )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int i;

	if( track_id == -1 )
	{
		for( i = 0; i < vp->n_tracks; i ++ )
			if( vp->tracks[i] && vp->tracks[i]->active )
				gvr_multiv_queue_vims( vp->tracks[i], vims_id,val1,val2 );
	}
	else
	{
		if( vp->tracks[track_id] && vp->tracks[track_id]->active)
			gvr_multiv_queue_vims( vp->tracks[track_id], vims_id,val1,val2 );
	}
}

void		gvr_track_disconnect( void *preview, int track_num )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;

	veejay_track_t *v = vp->tracks[ track_num ];
	if(v)
		gvr_close_connection( v );
	vp->tracks[ track_num ] = NULL;
	vp->track_sync->active_list[ track_num ] = 0;

}

int		gvr_track_configure( void *preview, int track_num, int w, int h )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	if( vp->tracks[track_num] )
	{
		vp->tracks[ track_num ]->width   = w;
		vp->tracks[ track_num ]->height  = h;
	}

	veejay_msg(VEEJAY_MSG_INFO, "VeejayGrabber to %s:%d started on Track %d in %dx%d",
		vp->tracks[ track_num ]->hostname,
		vp->tracks[ track_num ]->port_num,
		track_num,
		w,h);

	vp->track_sync->widths[track_num] = w;
	vp->track_sync->heights[track_num] = h;

	return 1;	
}

int		gvr_get_preview_status( void *preview, int track_num )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	if(!vp->tracks[track_num] )
		return 0;
	return vp->tracks[track_num]->preview;
}


int		gvr_track_toggle_preview( void *preview, int track_num, int status )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	vp->tracks[ track_num ]->preview = status;

	vj_msg(VEEJAY_MSG_INFO, "VeejayGrabber to %s:%d on Track %d %s",
		vp->tracks[ track_num ]->hostname,
		vp->tracks[ track_num ]->port_num,
		track_num,
		(status ? "starts loading images" : "stops loading images") );
	return status;
}



static GdkPixbuf	**gvr_grab_images(void *preview)
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	GdkPixbuf **list = (GdkPixbuf**) vj_calloc( sizeof(GdkPixbuf*) * vp->n_tracks );
	if(!list)
		return NULL;

	int i;
	
	for( i = 0; i < vp->n_tracks; i ++ )
	{
		if( vp->tracks[i] && vp->tracks[i]->active && vp->track_sync->widths[i] > 0)
		{
#ifdef STRICT_CHECKING
			assert( vp->track_sync->widths[i] > 0 );
			assert( vp->track_sync->heights[i] > 0 );
#endif
			veejay_track_t *v = vp->tracks[i];
//@FIXME HERE
			list[i] =gdk_pixbuf_new_from_data(vp->tracks[i]->tmp_buffer,GDK_COLORSPACE_RGB,FALSE,	
				8,vp->tracks[i]->width,vp->tracks[i]->height,
				  vp->tracks[i]->width*3,NULL,NULL );
			
		} 
	}

	return list;
}

static	int	*int_dup( int *status )
{
	int *res = (int*) vj_calloc( sizeof(int) * 32 );
	int i;
	for(i =0; i < 32 ; i ++ )
		res[i] = status[i];
	return res;
}

static int	**gvr_grab_stati( void *preview )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int **list = (int**) vj_calloc( sizeof(int*) * vp->n_tracks );
	if(!list)
		return NULL;

	int i;

	for( i = 0; i < vp->n_tracks; i ++ )
		if( vp->tracks[i] && vp->tracks[i]->active)
			list[i] = int_dup( vp->tracks[i]->status_tokens );
	return list;
}

static int	*gvr_grab_widths( void *preview )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int *list = (int**) vj_calloc( sizeof(int) * vp->n_tracks );
	if(!list)
		return NULL;

	int i;
	for( i = 0; i < vp->n_tracks; i ++ )
		if( vp->tracks[i] && vp->tracks[i]->active )
			list[i] = vp->track_sync->widths[i];
	
	return list;
}
static int	*gvr_grab_heights( void *preview )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int *list = (GdkPixbuf**) vj_calloc( sizeof(int) * vp->n_tracks );
	if(!list)
		return NULL;

	int i;
	for( i = 0; i < vp->n_tracks; i ++ )
		if( vp->tracks[i] && vp->tracks[i]->active )
			list[i] = vp->track_sync->widths[i];
	
	return list;
}

sync_info	*gvr_sync( void *preview )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	sync_info *s = (sync_info*) vj_calloc(sizeof(sync_info));

	gvr_veejay_grabber_step( preview );

	s->status_list = gvr_grab_stati( preview );
	s->tracks      = vp->n_tracks;
	s->widths      = gvr_grab_widths( preview );
	s->heights     = gvr_grab_heights( preview);
	s->img_list    = gvr_grab_images( preview ); 

	return s;
}


static	void	gvr_parse_track_list( veejay_preview_t *vp, veejay_track_t *v, unsigned char *tmp, int len )
{
	int i = 0;
	int items = 0;
	unsigned char *ptr = tmp;

	unsigned char **z = vj_calloc( sizeof( unsigned char * ) * vp->n_tracks );

	while( i < len )
	{
		int k = 0;
		unsigned char k_str[4];
		strncpy( (char*) k_str,(char*) ptr, 3 );
		if( k > 0 )
		{
			ptr += 3;
			z[items] = strndup( (char*) ptr, k );
			items ++;
			ptr += k;
		}
		i += ( 3 + k );
	}

	if( items > 0 )
	{
		for( i = 0; i < items ; i ++ )
		{
			int k;
			int in_track = -1;
			for( k = 0; k < vp->n_tracks ; k ++ )
			{
				veejay_track_t *t = vp->tracks[k];
				if(t)
				{
					char hostname[255];
					int  port = 0;
					int  stream_id = 0;
					veejay_memset(hostname,0,255 );
					if( sscanf( (char*) z[i], "%s %d %d", hostname, &port, &stream_id ))
					{
						if( strcasecmp(	hostname, t->hostname ) == 0 &&
							port == t->port_num )
							in_track = k;	
					}
				}
			}

			v->track_list[i] = in_track;			

			free( z[i] );
		}
		v->track_items = items;
	}

	free( z );
}

int		gvr_get_stream_id( void  *data, int id )
{
	veejay_preview_t *vp = (veejay_preview_t*) data;

	veejay_track_t *v = vp->tracks[id];

	if(v)
		return v->track_list[id];
	return 0;
}

static	void	gvr_parse_queue( veejay_track_t *v )
{
	int i;

	for( i = 0; i < v->n_queued ; i ++ )
	{
		if( vj_client_send( v->fd, V_CMD, v->queue[i] ) == -1  &&
			v->is_master )
			reloaded_schedule_restart();
		free( v->queue[i] );
		v->queue[i] = NULL;
	}
	v->n_queued = 0;
}
static	int	 gvr_veejay( veejay_preview_t *vp , veejay_track_t *v, int track_num )
{
	int score = 0;
	if( v->need_track_list || v->n_queued > 0 )
	{
		if( v->need_track_list )
		{
			int bw = 0;
			unsigned char *tmp = vims_track_list( v, 5, &bw );
			gvr_parse_track_list( vp, v, tmp, bw );
			v->need_track_list = 0;
		}
		if( v->n_queued > 0 )
		{
			gvr_parse_queue( v );
		}
		score ++;
	}

	if( v->preview )
	{
		if( gvr_preview_process_image( vp,v ))
			score++;
		else
		{
		        v->preview = 0;
			vj_client_close(v->fd);
    		 	int ok = vj_client_connect( v->fd, v->hostname, NULL, v->port_num );
			if( ok <= 0 )
			{
				veejay_msg(0, "VeejayGrabber: Unable to reconnect to %s, Destroying Track %d",
					(v->hostname ? v->hostname : "<unknown>"),
					track_num );
				vj_client_free(v->fd);
				if(v->hostname) free(v->hostname);
       				if(v->status_buffer) free(v->status_buffer);
				if(v->data_buffer) free(v->data_buffer);
				if(v->tmp_buffer) free(v->tmp_buffer);
				vp->tracks[track_num] = NULL;			
				if(  v->is_master )
					reloaded_schedule_restart();
				free(v);
			}
			else
			{
				v->preview = is_button_toggled( "previewtoggle");
				v->active = 1;
  				vj_msg(VEEJAY_MSG_WARNING, "VeejayGrabber: connected with %s:%d on Track %d  %d x %d", 
					v->hostname, v->port_num, track_num, v->width,v->height);
			} 
		}

	}

	return score;
}
#define MS_TO_NANO(a) (a *= 1000000)
static  void    net_delay(long nsec )
{
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = MS_TO_NANO( nsec);
        nanosleep( &ts, NULL );
}

void		gvr_veejay_grabber_step( void *data )
{
	veejay_preview_t *vp = (veejay_preview_t*) data;
	int i;

	int ac    = 1;
	int try_picture = 0;

	for( i = 0; i < vp->n_tracks ; i ++ )
	{
		if( vp->tracks[i] && vp->tracks[i]->active) {
			if(gvr_preview_process_status( vp, vp->tracks[i] ))
			{
				if( gvr_get_preview_status( vp, i ) )
					try_picture ++;
			}
		}
	}

	long ttw = 0;
	for( i = 0; i < vp->n_tracks ; i ++ )
	{
		if( vp->tracks[i] && vp->tracks[i]->active) 
		{
			if( vp->tracks[i] )
				ttw += gvr_veejay( vp, vp->tracks[i],i );	
		}
	}

}


