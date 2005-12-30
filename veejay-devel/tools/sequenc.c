#include <assert.h>
#include <veejay/vims.h>
#include <libvjnet/vj-client.h>
#include <libvjmsg/vj-common.h>
#include <glib.h>

#define DATA_ERROR 0
#define DATA_DONE 1
#define RETRIEVING_DATA 2
#define DATA_READY 3

typedef struct
{
	GThread *thread;
	gchar *hostname;
	gint port_num;
	gint active;
	vj_client *fd;
	gchar status_buffer[100];
	guchar *data_buffers[2];
	int	data_status[2];
	gint	frame_num;
	gint	width;
	gint	height;
	glong	time_out; // in microseconds
	GCond  *cond;
	GMutex *mutex;
} veejay_sequence_t;

  // 3 second timeout
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
/*
int			veejay_sequence_send( void *data , int vims_id, const char format[], ... )
{
	
	g_mutex_lock( v->data );
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
	g_mutex_unlock( v->data );
	return ret;
}*/

static	int	veejay_ipc_recv( veejay_sequence_t *v, gint header_len, gint *payload, guchar *buffer )
{
	gint tmp_len = header_len + 1;
	gchar tmp[tmp_len];
	gint len = 0;
	bzero( tmp, tmp_len );

	gint n = vj_client_read( v->fd, V_CMD, tmp, header_len );

	if( n<= 0 )
		return 0;

	if( sscanf( tmp, "%6d", &len )<=0)
		return 0;
	
	if( len <= 0 )
		return 0;

	
	gint bw = 0;
	gint bytes_read = len;
	guchar *buf_ptr = buffer;

	*payload = 0;

	while( bw < len )
	{
		n = vj_client_read( v->fd, V_CMD, buf_ptr, bytes_read );
		if ( n <= 0 )
			return 0;

		bw += n;

		bytes_read -= n;
		buf_ptr += bw;
	}

	*payload = bw;
  
	return 1;
}

static int	veejay_process_status( veejay_sequence_t *v )
{
	gchar status_len[6];
	bzero( status_len, 6 );


	gint nb = vj_client_read( v->fd, V_STATUS, status_len, 5 );
	if( status_len[0] == 'V' )
	{
		gint bytes = 0;
		sscanf( status_len + 1, "%03d", &bytes );
		if( bytes > 0 )
		{
			bzero( v->status_buffer, 100 );
			gint n = vj_client_read( v->fd, V_STATUS, v->status_buffer, bytes );
			if( n <= 0 )
				return 0;
			printf( "'%s'\n", v->status_buffer );
			return 1;
		}
	}
	assert(0);
	return 0;
}


static	int	veejay_get_image_data(veejay_sequence_t *v )
{
	gint res = veejay_ipc_send( v, VIMS_RGB24_IMAGE, "%d %d", v->width,v->height );
	if( res <= 0 )
		return 0;
	gint bw = 0;

	res = veejay_ipc_recv( v, 6, &bw, v->data_buffers[v->frame_num] );
	if( res <= 0 )
		return 0;
	return bw;
}

int		veejay_get_image( void *data, guchar *pixels)
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	gint ret = 0;
	g_mutex_lock( v->mutex );
	GTimeVal time_val;

	g_get_current_time( &time_val );
	g_time_val_add( &time_val, v->time_out );

	while( v->data_status[v->frame_num] == RETRIEVING_DATA )
	{
		if(!g_cond_timed_wait( v->cond, v->mutex, &time_val ))
		{	// timeout !
			v->data_status[v->frame_num] = DATA_ERROR;
			g_mutex_unlock(v->mutex);
			return ret;
		}
	}
	if( v->data_status[v->frame_num] == DATA_READY )
	{
		ret = 1;
		v->data_status[v->frame_num] = DATA_DONE;
	}
	if( v->data_status[v->frame_num] == DATA_ERROR )
	{
		ret = -1;
	}
	g_mutex_unlock( v->mutex );
	return ret;
} 

void		veejay_configure_sequence( void *data, gint w, gint h )
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	g_mutex_lock( v->mutex );

	while( v->data_status[v->frame_num] == RETRIEVING_DATA )
		g_cond_wait( v->cond, v->mutex );

	if( v->data_status[v->frame_num] == DATA_READY || v->data_status[v->frame_num] == DATA_DONE )
	{
		v->width = w;
		v->height = h;
	}

	g_mutex_unlock( v->mutex );	
}


static	int	veejay_process_data( veejay_sequence_t *v )
{
	gint ret = 0;
	g_mutex_lock( v->mutex );

	if( v->width <= 0 || v->height <= 0 )
	{
		g_mutex_unlock( v->mutex );
		return 1;
	}

	if(v->data_status[v->frame_num] == DATA_READY)
	{
		v->data_status[v->frame_num] = DATA_DONE;
	}
	if(v->data_status[v->frame_num] == DATA_DONE )
	{
		v->data_status[v->frame_num] = RETRIEVING_DATA;
		ret = veejay_get_image_data( v );
		if(ret)
			v->data_status[v->frame_num] = DATA_READY;
		else
			v->data_status[v->frame_num] = DATA_ERROR;
	
		g_cond_signal( v->cond );  
	}
	g_mutex_unlock( v->mutex );

	return ret;
}


void	*veejay_sequence_thread(gpointer data)
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;

	for ( ;; )
	{
		if( vj_client_poll( v->fd, V_STATUS ))
		{
			if( veejay_process_status( v ) == 0 )
			{
				printf("Status line terminated\n");
				return NULL;
			}
			if ( veejay_process_data( v ) == 0 )
			{
				printf("Command line terminated\n");
				return NULL;
			}
		}	
	}
	return NULL;	
}


void	*veejay_sequence_init(int port, char *hostname, gint max_width, gint max_height)
{	
	GError *err = NULL;
	veejay_sequence_t *v = (veejay_sequence_t*) malloc(sizeof( veejay_sequence_t ));
	memset( v, 0, sizeof(veejay_sequence_t));

	v->hostname = strdup( hostname );
	v->port_num = port;
	v->data_buffers[0] = (guchar*) malloc(sizeof(guchar) * max_width * max_height * 3 );
	v->data_buffers[1] = (guchar*) malloc(sizeof(guchar) * max_width * max_height * 3 );
	v->fd = vj_client_alloc(0,0,0);
	if(!vj_client_connect( v->fd, v->hostname, NULL,v->port_num ) )
	{
		vj_client_free( v->fd );
		printf("Error while connecting.\n");
		if( v->hostname ) free(v->hostname );
		if( v ) free (v);
		return NULL;
	}

	v->data_status[0] = DATA_DONE;
	v->data_status[1] = DATA_DONE;
	v->time_out = 1000000 * 3; // 3 second timeout
	v->frame_num = 0;
	v->mutex = g_mutex_new();
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
	
	return v;
}

void	veejay_sequence_free( void *data )
{
	veejay_sequence_t *v = (veejay_sequence_t*) data;
	
	g_thread_join( v->thread );
	g_cond_free( v->cond );
	g_mutex_free( v->mutex );
	vj_client_close( v->fd );

	if( v->hostname )
		free(v->hostname );
	if( v->data_buffers[0] )
		free( v->data_buffers[0] );
	if( v->data_buffers[1] )
		free( v->data_buffers[1] );

	free(v);
	v = NULL;
}


int main(int argc, char *argv[])
{

	if(!g_thread_supported())
	{
		g_thread_init(NULL);
	}

	void *v = veejay_sequence_init( 3490, "localhost",512,512 );
	if(!v)
		return 0;
	void *v2 = veejay_sequence_init( 4490, "localhost",512,512 );
	if(!v2)
		return 0;

	while(1)	
	{
		if( veejay_get_image(v,NULL) > 0 )
		{
			printf("grabbed frame 3490\n");
		}
		if( veejay_get_image(v2,NULL) > 0)
		{
			printf("grabbed frame 4490\n");
		}
	}
	veejay_sequence_free( v );
	veejay_sequence_free( v2 );
	return 1;
}
