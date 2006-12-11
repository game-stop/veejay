#include <veejay/vims.h>
#include <libvjnet/vj-client.h>
#include <libvjmsg/vj-common.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <sys/time.h>
#define DATA_ERROR 0
#define DATA_DONE 1
#define RETRIEVING_DATA 2
#define DATA_READY 3
#define MAX_BUF 2
typedef struct
{
	GThread *thread;
	gchar *hostname;
	gint port_num;
	gint active;
	vj_client *fd;
	unsigned char status_buffer[100];
	unsigned char *data_buffers[MAX_BUF];
	int	data_status[MAX_BUF];
	gint	frame_num;
	gint	wframe_num;
	gint	preview;
	gint	width;
	float  fps;
	gint	height;
	gint	abort;
	unsigned long preview_delay;
	glong	time_out; // in microseconds
	GCond  *cond;
	GMutex *mutex;
//	GMutex *serialize;
	guchar *serialized[100];
	int	sta[22];
} veejay_sequence_t;

static unsigned long vj_get_timer()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((long) (tv.tv_sec * 1000000) + tv.tv_usec);
}

void	veejay_sequence_free( void *data );

  // 3 second timeout
//@ Check if this works!
static	int	veejay_ipc_send( veejay_sequence_t *v,
		int vims_id, const char format[], ... )
{
	gchar	block[255];	
	gchar	tmp[255];
	va_list args;
	
	if( format == NULL )
	{
		g_snprintf( block, sizeof(block)-1, "%03d:;", vims_id );
		gint n = vj_client_send( v->fd, V_CMD, block );
		if( n <= 0 )
			return 0;
		return n;
	}

	va_start( args, format );
	vsnprintf( tmp, sizeof(tmp)-1, format, args );
	g_snprintf( block,sizeof(block)-1, "%03d:%s;", vims_id, tmp );
	va_end( args );
	
	gint n	= vj_client_send( v->fd, V_CMD, block );
	if( n <= 0 )
		return 0;

	return n;
}

int			veejay_sequence_send( void *data , int vims_id, const char format[], ... )
{
	
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	if(!v || !v->active) return 0;
	
	g_mutex_lock( v->mutex );
	gint ret = 0;
	gchar	block[255];	
	gchar	tmp[255];
	va_list args;
	
	if( format == NULL )
	{
		g_snprintf( block, sizeof(block)-1, "%03d:;", vims_id );
		ret = vj_client_send( v->fd, V_CMD, block );
	}
	else
	{
		va_start( args, format );
		vsnprintf( tmp, sizeof(tmp)-1, format, args );
		g_snprintf( block,sizeof(block)-1, "%03d:%s;", vims_id, tmp );
		va_end( args );
	
		ret = vj_client_send( v->fd, V_CMD, block );
	}
	g_mutex_unlock( v->mutex );
	return ret;
}

unsigned char		*veejay_sequence_get_track_list( void *data, int slen, int *bytes_written )
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	if(!v || !v->active) return NULL;
	g_mutex_lock( v->mutex );
	
	unsigned char message[10];
	int tmp_len = slen + 1;
	unsigned char *tmp = vj_calloc( tmp_len );
 
	sprintf(message, "%03d:;", VIMS_TRACK_LIST );
	int ret = vj_client_send( v->fd, V_CMD, message );
	if( ret <= 0)
	{
		free(tmp);
		g_mutex_unlock(v->mutex);
		return NULL;
	}	

        ret = vj_client_read( v->fd, V_CMD, tmp, slen );
	if( ret <= 0 )
	{
		free(tmp);
		g_mutex_unlock(v->mutex);
		return NULL;
	}

        int len = 0;
        sscanf( tmp, "%d", &len );
        unsigned char *result = NULL;

        if( len <= 0 || slen <= 0)
        {
		free(tmp);
		g_mutex_unlock( v->mutex );
	        return result;
	}

        result = (gchar*) vj_calloc(sizeof(gchar) * (len + 1) );
        int bytes_left = len;
        *bytes_written = 0;

        while( bytes_left > 0)
        {
                int n = vj_client_read( v->fd, V_CMD, result + (*bytes_written), bytes_left );
                if( n <= 0 )
  		{
                        bytes_left = 0;
                }
                if( n > 0 )
                {
                        *bytes_written +=n;
                        bytes_left -= n;
                }
        }
	free(tmp);
	g_mutex_unlock( v->mutex );
        return result;
}

static	int	veejay_ipc_recv( veejay_sequence_t *v, gint header_len, gint *payload, guchar *buffer )
{
	gint tmp_len = header_len + 1;
	unsigned char *tmp = vj_calloc( tmp_len );
	gint len = 0;

	gint n = vj_client_read( v->fd, V_CMD, tmp, header_len );

	if( n<= 0 )
	{
		free(tmp);
		veejay_msg(0,"Error reading header of %d bytes: %d", header_len,n );
		return 0;
	}

	if( sscanf( tmp, "%6d", &len )<=0)
	{
		free(tmp);
		veejay_msg(0, "Error reading header contents");
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
			veejay_msg(0, "Recv %d out of %d bytes", bw,len);
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

static int	veejay_process_status( veejay_sequence_t *v )
{
	unsigned char status_len[6];
	veejay_memset( status_len, 0, sizeof(status_len) );
	gint nb = vj_client_read( v->fd, V_STATUS, status_len, 5 );
	if( status_len[0] == 'V' )
	{
		gint bytes = 0;
		sscanf( status_len + 1, "%03d", &bytes );
		if( bytes > 0 )
		{
			veejay_memset( v->status_buffer,0, sizeof(v->status_buffer));
			gint n = vj_client_read( v->fd, V_STATUS, v->status_buffer, bytes );
			if( n <= 0 )
			{
				veejay_msg(0, "Error reading statusline");
				return 0;
			}
//			g_mutex_lock( v->serialize );
			veejay_memcpy( v->serialized, v->status_buffer, bytes );
			status_to_arr( v->status_buffer, v->sta );
	//		g_mutex_unlock( v->serialize );
			return 1;
		}
	}
	return 0;
}

static	int	veejay_get_image_data(veejay_sequence_t *v )
{
	if(!v->active )
		return 1;
	
	gint res = veejay_ipc_send( v, VIMS_RGB24_IMAGE, "%d %d", v->width,v->height );
	if( res <= 0 )
	{
		veejay_msg(0, "Error sending VIMS_RGB24_IMAGE");	
		return 0;
	}
	gint bw = 0;

	res = veejay_ipc_recv( v, 6, &bw, v->data_buffers[v->frame_num] );
	if( res <= 0 )
	{
		veejay_msg(0, "Error receiving RGB24 image");
		return 0;
	}
	return bw;
}

void		veejay_get_status( void *data, guchar *dst )
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	if(!v->active)
		return;
	g_mutex_lock( v->mutex );
	veejay_memcpy( dst, v->serialized, 100);
	g_mutex_unlock(v->mutex );
}

GdkPixbuf	*veejay_get_image( void *data, gint *error)
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	if(!v || !v->active) return NULL;

	gint ret = 0;
	g_mutex_lock( v->mutex );
	GTimeVal time_val;

	g_get_current_time( &time_val );
	g_time_val_add( &time_val, v->time_out );

//	while( v->data_status[v->frame_num] == RETRIEVING_DATA )
	while( v->data_status[v->frame_num] != DATA_READY )

	{
		//@ sleeping for new frames!
		if(!g_cond_timed_wait( v->cond, v->mutex, &time_val ))
		{	// timeout !
			v->data_status[v->frame_num] = DATA_ERROR;
			g_mutex_unlock(v->mutex);
			*error = 1;
			return NULL;
		}
		if( v->abort )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Abort image preview thread");
			g_mutex_unlock(v->mutex);

			return NULL;
		}
	}

	if( v->data_status[v->frame_num] == DATA_READY )
	{
		*error = 0;
		GdkPixbuf *res = gdk_pixbuf_new_from_data(
				v->data_buffers[v->frame_num],
				GDK_COLORSPACE_RGB,
				FALSE,
				8,
				v->width,
				v->height,
				v->width * 3,
				NULL,
				NULL );
		v->data_status[v->frame_num] = DATA_DONE;
		v->frame_num = (v->frame_num+1)%MAX_BUF;
		g_mutex_unlock( v->mutex );
		return res;
	}
	if( v->data_status[v->frame_num] == DATA_ERROR )
		*error = 1;

	g_mutex_unlock( v->mutex );
	return NULL;
} 

void		veejay_configure_sequence( void *data, gint w, gint h , float fps)
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	if(!v) return;
	g_mutex_lock( v->mutex );
	
	while( v->data_status[v->frame_num] == RETRIEVING_DATA )
		g_cond_wait( v->cond, v->mutex );

	v->width = w;
	v->height = h;
	if(fps>0.0)
		v->fps = fps;

	veejay_msg(VEEJAY_MSG_INFO, "Configured %d x %d @ %2.2f",
			w,h,fps );
	
	g_mutex_unlock( v->mutex );	
}


static	int	veejay_process_data( veejay_sequence_t *v )
{
	gint ret = 0;
	g_mutex_lock( v->mutex );

	if( v->width <= 0 || v->height <= 0 || v->preview == 0 || v->active == 0)
	{
		g_mutex_unlock( v->mutex );
		return 1;
	}

	if(v->data_status[v->wframe_num] == DATA_READY||v->data_status[v->wframe_num]==DATA_ERROR)
		v->data_status[v->wframe_num] = DATA_DONE;
	if(v->data_status[v->wframe_num] == DATA_DONE )
	{
		v->data_status[v->wframe_num] = RETRIEVING_DATA;
		ret = veejay_get_image_data( v );
		if(ret)
		{
			v->data_status[v->wframe_num] = DATA_READY;
			v->wframe_num = (v->wframe_num+1) % MAX_BUF;
		}
		else
			v->data_status[v->wframe_num] = DATA_ERROR;
		g_cond_signal( v->cond );  
	}
	g_mutex_unlock( v->mutex );

	return ret;
}

void	*veejay_sequence_thread(gpointer data)
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	if(!v) return NULL;
	unsigned long time_now = 0;
	unsigned long tn = vj_get_timer() + v->preview_delay;
	
	for ( ;; )
	{	
		glong spf = (glong)(((float)1.0 / v->fps) * 1000);

	//	time_now = vj_get_timer();
		
		if( v->abort )
		{
			veejay_msg(0, "Thread aborted");
			return NULL;
		}
		if( vj_client_poll( v->fd, V_STATUS ))
		{
			if( veejay_process_status( v ) == 0 )
			{
				veejay_msg(0, "Error reading status from track");
				v->active = 0;
				return NULL;
			}
		}
		
	//	if( time_now > tn && v->sta[0] < 250)
	//	{
			if( v->preview )
			{
				veejay_msg(0, "%s: process_data", __FUNCTION__ );
				if ( veejay_process_data( v ) == 0 )
				{
					veejay_msg(0, "Abort, data error");
					v->active = 0;
					return NULL;
				}
			}
	//		tn = time_now + v->preview_delay; 
	//	}
		
		glong ms_passed = v->sta[0];
	        if( (spf - ms_passed) > 0 )
			g_usleep( (spf-ms_passed)*1000);
	}
	return NULL;	
}

void	veejay_abort_sequence( void *data )
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	if(!v || !v->active)	return;

	g_mutex_lock(v->mutex);
	v->abort = 1;
	g_mutex_unlock(v->mutex);

	veejay_sequence_free( data );
}

void	veejay_toggle_image_loader( void *data, gint state )
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	if(!v) return;
	if(!v->active)
		return;
	g_mutex_lock(v->mutex);
	v->preview = state;
	g_mutex_unlock(v->mutex);
}

int	veejay_sequence_active_preview( void *data )
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	if(!v) return 0;
	return v->preview;
}

void	veejay_sequence_preview_delay( void *data, double value )
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	if(!v) return;

	g_mutex_lock(v->mutex);
	gint max = 4 * 100000;
	v->preview_delay = (unsigned long)( value * (double) max);
	g_mutex_unlock(v->mutex);
}

void	*veejay_sequence_init(int port, char *hostname, gint max_width, gint max_height)
{	
	int k = 0;
	GError *err = NULL;
	veejay_sequence_t *v = (veejay_sequence_t*) vj_calloc(sizeof( veejay_sequence_t ));

	v->hostname = strdup( hostname );
	v->port_num = port;
	for( k = 0; k < MAX_BUF; k ++ )
	{
		v->data_buffers[k] = (guchar*) vj_calloc(sizeof(guchar) * max_width * max_height * 3 );
		v->data_status[k] = DATA_ERROR;
	}
	v->fd = vj_client_alloc(0,0,0);
	if(!vj_client_connect( v->fd, v->hostname, NULL,v->port_num ) )
	{
		vj_client_free( v->fd );
		if( v->hostname ) free(v->hostname );
		if( v ) free (v);
		return NULL;
	}

	v->time_out = 4500000; // Micro seconds
	v->frame_num = 0;
	v->wframe_num = 0;
	v->abort = 0;
	v->preview_delay = 40000;
	v->mutex = g_mutex_new();
//	v->serialize = g_mutex_new();
	v->active = 1;
	v->fps = 25.0;
	v->cond = g_cond_new();
	v->thread = g_thread_create(
			(GThreadFunc) veejay_sequence_thread,
			(gpointer*) v,
			TRUE,
			&err );
	if(!v->thread)
	{
		printf("%s\n", err->message );
		if(v) free(v);
		return NULL;
	}
	veejay_msg(2, "New connection with %s, %d (max %d x %d), lock %p",
			hostname,port,max_width,max_height, v->mutex );
	return v;
}

void	veejay_sequence_free( void *data )
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	int k;	
	g_thread_join( v->thread );
	g_cond_free( v->cond );
	g_mutex_free( v->mutex );
	vj_client_close( v->fd );

	if( v->hostname )
		free(v->hostname );
	for( k = 0; k < MAX_BUF; k ++ )
		free( v->data_buffers[k] );

	free(v);
	v = NULL;
}

