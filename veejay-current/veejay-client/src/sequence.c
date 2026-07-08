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
#include <stdio.h>
#include <stdlib.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
#include <veejaycore/defs.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vims.h>
#include <veejaycore/vj-client.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/avcommon.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <sys/time.h>
#include <veejaycore/libvevo.h>
#include <src/sequence.h>
#include <src/utils.h>
#include <veejaycore/yuvconv.h>
#include <string.h>
#include "common.h"

extern void reloaded_schedule_restart();
extern void vj_msg(int type, const char format[], ...);
extern void multitrack_cleanup_track( void *data, int track );

extern int alphaonly_view;

typedef struct
{
	uint8_t *image_data[__MAX_TRACKS];
	int *status_tokens[__MAX_TRACKS];
	int	widths[__MAX_TRACKS];
	int	heights[__MAX_TRACKS];
	int	active_list[__MAX_TRACKS];
	int	frame_list[__MAX_TRACKS];
} track_sync_t;

typedef struct
{
	char *hostname;
	int   port_num;
	vj_client *fd;
	uint8_t *data_buffer;
	uint8_t *tmp_buffer;
	uint8_t *status_buffer;
	size_t data_buffer_size;
	size_t tmp_buffer_size;
	int	 track_list[__MAX_TRACKS];
	int	 track_items;
	int	 status_tokens[VJ_STATUS_ARRAY_SIZE];
	int  active;
	int  have_frame;
	int	grey_scale;
	int full_range;
    int	preview;
	int	width;
	int	height;
	int	prevmode;
	int	need_track_list;
	char 	*queue[__MAX_TRACKS];
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

static int sendvims( veejay_track_t *v, int vims_id, const char format[], ... );
static int recvvims( veejay_track_t *v, gint header_len, gint *payload, guchar *buffer );
static int veejay_get_image_data(veejay_preview_t *vp, veejay_track_t *v );
static int track_find(  veejay_preview_t *vp );
static int veejay_process_status( veejay_preview_t *vp, veejay_track_t *v );
static int gvr_preview_process_image( veejay_preview_t *vp, veejay_track_t *v );
static int track_exists( veejay_preview_t *vp, const char *hostname, int port_num, int *at );
static int gvr_preview_process_status( veejay_preview_t *vp, veejay_track_t *v );
static int gvr_veejay_grabber_step( void *data, void *caller_data );


static int gvr_status_to_arr(const char *status, int *tokens)
{
    const char *p = status;
    int count = 0;

    if(!status || !tokens)
        return 0;

    veejay_memset(tokens, 0, sizeof(int) * VJ_STATUS_ARRAY_SIZE);

    while(*p && count < VJ_STATUS_ARRAY_SIZE)
    {
        char *end = NULL;
        long v;

        while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;

        if(*p == '\0')
            break;

        v = strtol(p, &end, 10);
        if(end == p)
            break;

        tokens[count++] = (int)v;
        p = end;
    }

    return count;
}

void	*gvr_preview_init(int max_tracks, int use_threads)
{
	veejay_preview_t *vp = (veejay_preview_t*) vj_calloc(sizeof( veejay_preview_t ));
	vp->tracks = (veejay_track_t**) vj_calloc(sizeof( veejay_track_t*) * max_tracks );
	vp->track_sync = (track_sync_t*) vj_calloc(sizeof( track_sync_t ));
	int i;

    for( i = 0; i < max_tracks; i++ )
		vp->track_sync->status_tokens[i] = (int*) vj_calloc(sizeof(int) * VJ_STATUS_ARRAY_SIZE);

	vp->n_tracks = max_tracks;

	yuv_init_lib(0,0,0);

	return (void*) vp;
}

static inline int gvr_track_index_valid(veejay_preview_t *vp, int track_num)
{
	return (vp && track_num >= 0 && track_num < vp->n_tracks);
}

static inline veejay_track_t *gvr_track_ptr(veejay_preview_t *vp, int track_num)
{
	if(!gvr_track_index_valid(vp, track_num))
		return NULL;
	return vp->tracks[track_num];
}

static int gvr_track_ensure_buffers(veejay_track_t *v, int w, int h)
{
	size_t pixels;
	size_t data_need;
	size_t tmp_need;
	uint8_t *data;
	uint8_t *tmp;

	if(!v || w <= 0 || h <= 0)
		return 0;

	pixels = (size_t) w * (size_t) h;
	data_need = pixels * 3;
	tmp_need = pixels * 4;

	if(data_need > v->data_buffer_size) {
		data = (uint8_t*) vj_malloc(data_need);
		if(!data)
			return 0;
		if(v->data_buffer)
			free(v->data_buffer);
		v->data_buffer = data;
		v->data_buffer_size = data_need;
	}

	if(tmp_need > v->tmp_buffer_size) {
		tmp = (uint8_t*) vj_malloc(tmp_need);
		if(!tmp)
			return 0;
		if(v->tmp_buffer)
			free(v->tmp_buffer);
		v->tmp_buffer = tmp;
		v->tmp_buffer_size = tmp_need;
	}

	return 1;
}

static	void	gvr_close_connection( veejay_track_t *v )
{
    if(v != NULL) {
        veejay_msg(VEEJAY_MSG_INFO, "Closing connection %s:%d",v->hostname,v->port_num );
        if(v->fd) {
            vj_client_close(v->fd);
            vj_client_free(v->fd);
            v->fd = NULL;
        }

        if(v->hostname) {
            free(v->hostname);
            v->hostname = NULL;
        }
        if(v->status_buffer) {
            free(v->status_buffer);
            v->status_buffer = NULL;
        }
        if(v->data_buffer) {
            free(v->data_buffer);
            v->data_buffer = NULL;
        }
        if(v->tmp_buffer) {
            free(v->tmp_buffer);
            v->tmp_buffer = NULL;
        }
        free(v);
    }
}

static	int	sendvims( veejay_track_t *v, int vims_id, const char format[], ... )
{
	gchar	block[32];
	gchar	tmp[255];
	va_list args;
	gint    n;
	if( format == NULL )
	{
		g_snprintf( block, sizeof(block)-1, "%03d:;", vims_id );
		n = vj_client_send( v->fd, V_CMD, (unsigned char*) block );
		if( n <= 0 ) {
			if( n == -1 && v->is_master )
				reloaded_schedule_restart();
			return 0;
		}
		return 0;
	}

	va_start( args, format );
	vsnprintf( tmp, sizeof(tmp)-1, format, args );
	g_snprintf( block,sizeof(block)-1, "%03d:%s;", vims_id, tmp );
	va_end( args );

	n = vj_client_send( v->fd, V_CMD,(unsigned char*) block );
	if( n <= 0 ) {
		if( n == -1 && v->is_master )
			reloaded_schedule_restart();
	}
	return 1;
}

static int	recvvims( veejay_track_t *v, gint header_len, gint *payload, guchar *buffer )
{
	unsigned char tmp_stack[64];
	unsigned char *tmp = tmp_stack;
	memset(tmp_stack, 0, sizeof(tmp_stack));
	size_t len = 0;
	int wid = 0;
	int hei = 0;

	if(header_len <= 0)
		return 0;

	if(header_len >= (gint)sizeof(tmp_stack)) {
		tmp = vj_calloc((size_t)header_len + 1);
		if(!tmp)
			return 0;
	}
	else {
		tmp[header_len] = '\0';
	}

	gint n = vj_client_read_no_wait( v->fd, V_CMD, tmp, header_len );

	if( n<= 0 )
	{
		if( n == -1 && v->is_master)
			reloaded_schedule_restart();
		veejay_msg(VEEJAY_MSG_ERROR,"Reading header of %d bytes: %d", header_len,n );
		if(tmp != tmp_stack)
			free(tmp);
		return n;
	}

	if( sscanf( (char*)tmp, "%06zu%04d%02d%1d", &len,&wid, &hei, &(v->full_range) ) != 4 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot parse header (data stream polluted)");
		if(tmp != tmp_stack)
			free(tmp);
		return 0;
	}

	if( len <= 0 )
	{
		if(tmp != tmp_stack)
			free(tmp);
		veejay_msg(VEEJAY_MSG_ERROR, "Frame is empty");
		return 0;
	}

	size_t bw = 0;
	gint bytes_read = len;
	unsigned char *buf_ptr = buffer;

	*payload = 0;

	while( bw < len )
	{
		n = vj_client_read_no_wait( v->fd, V_CMD, buf_ptr, bytes_read );
		if ( n <= 0 )
		{
			if( n == -1 && v->is_master )
				reloaded_schedule_restart();
			veejay_msg(VEEJAY_MSG_ERROR, "Received %d out of %d bytes", bw,len);
			if(tmp != tmp_stack)
				free(tmp);
			*payload = 0;
			return n;
		}
		bw += n;

		bytes_read -= n;
		buf_ptr += n;
	}
	*payload = (gint) bw;

	if(tmp != tmp_stack)
		free(tmp);
	return 1;
}


static unsigned char		*vims_track_list( veejay_track_t *v, int slen, int *bytes_written )
{
	unsigned char message[8];
	int tmp_len = slen + 1;
	unsigned char *result = NULL;
	unsigned char *tmp = vj_calloc( tmp_len );
	if( tmp == NULL )
	{
		if( v->is_master ) {
			reloaded_schedule_restart();
		}
		return NULL;
	}

	snprintf( (char*) message,sizeof(message), "%03d:;", VIMS_TRACK_LIST );
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
    if( sscanf( (char*) tmp, "%d", &len ) != 1 )
	{
		if(v->is_master)
			reloaded_schedule_restart();
		free(tmp);
		return result;
	}

	if( len <= 0 || slen <= 0)
    {
		free(tmp);
	    return result;
	}

    result = (unsigned char*) vj_calloc(sizeof( unsigned char) * (len + 1) );
	if( result == NULL ) {
		if(v->is_master)
			reloaded_schedule_restart();
		free(tmp);
		return result;
	}

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
			break;
        }
        if( n > 0 )
        {
			*bytes_written +=n;
			bytes_left -= n;
        }
    }
	free(tmp);

	if( bytes_left ) {
		free(result);
		return NULL;
	}

    return result;
}


static int	veejay_process_status( veejay_preview_t *vp, veejay_track_t *v )
{
    (void) vp;

	unsigned char status_len[VJ_STATUS_WIRE_HEADER_LEN + 1];
	int k = -1;
	int n = 0;

	while( (k = vj_client_poll( v->fd, V_STATUS )) )
    {
        veejay_memset(status_len, 0, sizeof(status_len));
        n = vj_client_read(v->fd, V_STATUS, status_len, VJ_STATUS_WIRE_HEADER_LEN );

		if( n <= 0 ) {
            veejay_msg(0, "Lost connection with Veejay");
			return 0;
		}

        if( n != VJ_STATUS_WIRE_HEADER_LEN ||
            status_len[0] != 'V' ||
            status_len[VJ_STATUS_WIRE_HEADER_LEN - 1] != 'S' )
        {
            veejay_msg(0,
                       "Unexpected status header [%s] with %s:%d",
                       status_len,
                       v->hostname,
                       v->port_num);
			return 0;
		}

        int bytes = 0;
        for(int i = 1; i < VJ_STATUS_WIRE_HEADER_LEN - 1; i++)
        {
            if(status_len[i] < '0' || status_len[i] > '9')
            {
                veejay_msg(0,
                           "Invalid status header length [%s] with %s:%d",
                           status_len,
                           v->hostname,
                           v->port_num);
                return 0;
            }

            bytes = (bytes * 10) + (status_len[i] - '0');
        }

		if(bytes > 0 )
		{
            if(bytes >= STATUS_LENGTH) {
                veejay_msg(VEEJAY_MSG_WARNING, "Status packet too large: %d bytes", bytes);
                return 0;
            }

            veejay_memset(v->status_buffer, 0, STATUS_LENGTH);

            int got = 0;
            while(got < bytes) {
			    n = vj_client_read( v->fd, V_STATUS, v->status_buffer + got, bytes - got );
			    if( n <= 0 ) {
				    veejay_msg(0,"Failed to read %d status bytes", bytes);
			        return 0;
                }
                got += n;
            }
            v->status_buffer[bytes] = '\0';

            int token_count = gvr_status_to_arr( (char*) v->status_buffer, v->status_tokens );

			if( token_count < VIMS_STATUS_TOKENS )
			{
				veejay_msg(VEEJAY_MSG_WARNING,
                           "Expected at least %d status tokens, got %d: %s",
                           VIMS_STATUS_TOKENS,
                           token_count,
                           v->status_buffer);
				return 0;
			}
		}
	}

    if( k == -1 ) {
        veejay_msg(0, "Dropping connection to Veejay");
        return 0;
    }

	return 1;
}

extern int     is_button_toggled(const char *name);

static int veejay_get_image_data(veejay_preview_t *vp, veejay_track_t *v)
{
    if (!v->have_frame && (v->width <= 0 || v->height <= 0))
        return 0;

    gint res = sendvims(v, VIMS_RGB24_IMAGE, "%d %d %d", v->width, v->height, alphaonly_view);
    if (res <= 0) {
        v->have_frame = 0;
        return res;
    }

    if(!gvr_track_ensure_buffers(v, v->width, v->height)) {
        v->have_frame = 0;
        return 0;
    }

    gint bw = 0;
    res = recvvims(v, 13, &bw, v->data_buffer);
    if (res <= 0 || bw <= 0) {
        veejay_msg(VEEJAY_MSG_WARNING, "Cannot get a preview image; only received %d bytes", bw);
        v->have_frame = 0;
        return res;
    }

    int y_size = v->width * v->height;
    int srcfmt;
    size_t total_needed;

    if (bw == y_size) {

        srcfmt = PIX_FMT_GRAY8;
        total_needed = y_size;
    } else {

        srcfmt = (v->full_range ? PIX_FMT_YUVJ420P : PIX_FMT_YUV420P);
        int uv_size = y_size / 4;
        total_needed = y_size + uv_size * 2;

        if (bw < total_needed) {
            veejay_msg(VEEJAY_MSG_WARNING, "Received only %d bytes, expected %zu for YUV420", bw, total_needed);
			veejay_msg_buffer(v->data_buffer, bw,64, "veejay_get_image_data");
            v->have_frame = 0;
            return 0;
        }
    }

    uint8_t *in = v->data_buffer;
    v->bw = 0;

    uint8_t *buf_u = NULL;
    uint8_t *buf_v = NULL;

    if (srcfmt != PIX_FMT_GRAY8) {
        buf_u = in + y_size;
        buf_v = buf_u + (y_size / 4);
    }

    VJFrame *src1 = yuv_yuv_template(in, buf_u, buf_v, v->width, v->height, srcfmt);
    VJFrame *dst1 = yuv_rgb_template(v->tmp_buffer, v->width, v->height, PIX_FMT_RGB24);

    yuv_convert_any_ac(src1, dst1);

    v->have_frame = 1;

    free(src1);
    free(dst1);

    return total_needed;
}

static int	gvr_preview_process_status( veejay_preview_t *vp, veejay_track_t *v )
{
    int k =	veejay_process_status( vp, v );
	if( k == 0 && v->is_master) {
	    veejay_msg(VEEJAY_MSG_INFO, "Opening launcher window");
        reloaded_schedule_restart();
	}
	return (k > 0);
}

static int fail_connection	 = 0;
static int continue_anyway       = 0;
static int 	gvr_preview_process_image( veejay_preview_t *vp, veejay_track_t *v )
{
	if( v->preview == 0 )
		return 1;

	int n = veejay_get_image_data( vp, v );

	if(n == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "No image data %dx%d" , v->width,v->height);

		fail_connection ++;
		if( fail_connection > 2 ) {
			fail_connection = 0;
			return 0;
		}
		return 1;
	} if( n == -1 ) {
		return 0;
	} else {
		continue_anyway = (continue_anyway + 1) % 10;
		if(continue_anyway == 0)
		  fail_connection = 0;
	}

	return 1;
}

void		gvr_set_master(void *data, int master_track )
{
	veejay_preview_t *vp = (veejay_preview_t*) data;
	int i;

	if(!gvr_track_ptr(vp, master_track))
		return;

	for( i = 0; i < vp->n_tracks; i ++ )
		if( vp->tracks[i] )
			vp->tracks[i]->is_master = 0;

	vp->tracks[master_track]->is_master = 1;
}

int		gvr_track_swap(void *data, int track_a, int track_b)
{
	veejay_preview_t *vp = (veejay_preview_t*) data;
	veejay_track_t *tmp_track;
	int tmp;

	if(!gvr_track_index_valid(vp, track_a) || !gvr_track_index_valid(vp, track_b))
		return 0;
	if(track_a == track_b)
		return 1;

	tmp_track = vp->tracks[track_a];
	vp->tracks[track_a] = vp->tracks[track_b];
	vp->tracks[track_b] = tmp_track;

	if(vp->track_sync)
	{
		tmp = vp->track_sync->active_list[track_a];
		vp->track_sync->active_list[track_a] = vp->track_sync->active_list[track_b];
		vp->track_sync->active_list[track_b] = tmp;

		tmp = vp->track_sync->widths[track_a];
		vp->track_sync->widths[track_a] = vp->track_sync->widths[track_b];
		vp->track_sync->widths[track_b] = tmp;

		tmp = vp->track_sync->heights[track_a];
		vp->track_sync->heights[track_a] = vp->track_sync->heights[track_b];
		vp->track_sync->heights[track_b] = tmp;

		tmp = vp->track_sync->frame_list[track_a];
		vp->track_sync->frame_list[track_a] = vp->track_sync->frame_list[track_b];
		vp->track_sync->frame_list[track_b] = tmp;
	}

	return 1;
}

static int gvr_host_is_loopback(const char *host)
{
	return host &&
		(!strcasecmp(host, "localhost") ||
		 !strcasecmp(host, "127.0.0.1") ||
		 !strcasecmp(host, "::1"));
}

static int gvr_host_matches(const char *a, const char *b)
{
	if(!a || !b)
		return 0;
	if(!strcasecmp(a, b))
		return 1;
	return gvr_host_is_loopback(a) && gvr_host_is_loopback(b);
}

static	int	track_exists( veejay_preview_t *vp, const char *hostname, int port_num, int *at_track )
{
	int i;

	if(!vp || !hostname || port_num <= 0)
		return 0;

	for( i = 0; i < vp->n_tracks ; i++ )
	{
		if( vp->tracks[i] )
		{
			veejay_track_t *v = vp->tracks[i];
			if( gvr_host_matches(hostname, v->hostname) && v->port_num == port_num )
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
	return gvr_track_ptr(vp, track_id) ? 1 : 0;
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
	veejay_track_t *v = gvr_track_ptr(vp, num);

	return v ? v->hostname : NULL;
}

int		gvr_track_get_portnum( void *preview, int num)
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	veejay_track_t *v = gvr_track_ptr(vp, num);

	return v ? v->port_num : 0;
}

int		gvr_track_find_open( void *preview, const char *hostname, int port, int *track_num )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;

	if(track_num)
		*track_num = -1;

	return track_exists( vp, hostname, port, track_num );
}

int		gvr_track_already_open( void *preview, const char *hostname,
	int port )
{
	return gvr_track_find_open( preview, hostname, port, NULL );
}

int		gvr_track_connect( void *preview, char *hostname, int port_num, int *new_track )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int track_num;

	if(new_track)
		*new_track = -1;

	if(!vp || !hostname || port_num <= 0)
		return -1;

	if(track_exists( vp, hostname, port_num, new_track ) )
	{
		veejay_msg(VEEJAY_MSG_INFO,
			"Veejay %s:%d is already assigned to track %d",
			hostname, port_num, new_track ? *new_track : -1 );
		return 0;
	}

	track_num = track_find( vp );
	if(track_num == -1)
	{
		vj_msg(0, "All tracks are in use.");
		return 0;
	}
	vj_client *fd = vj_client_alloc();
	if(!fd)
		return -1;
	if(!vj_client_connect( fd, hostname, NULL, port_num ) )
	{
		vj_msg(VEEJAY_MSG_ERROR, "Unable to connect to %s:%d", hostname, port_num );
		vj_client_free( fd );
		return -1;
	}

	veejay_track_t *vt = (veejay_track_t*) vj_calloc( sizeof(veejay_track_t));
	if(!vt)
	{
		vj_client_close( fd );
		vj_client_free( fd );
		return -1;
	}

	vt->fd       = fd;
	vt->hostname = strdup(hostname);
	vt->port_num  = port_num;
	vt->active   = 1;

	if(!vt->hostname)
	{
		vj_client_close( fd );
		vj_client_free( fd );
		free(vt);
		return -1;
	}

	vt->status_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * STATUS_LENGTH);
	if(vt->status_buffer == NULL ) {
		gvr_close_connection( vt );
		return -1;
	}

	if(!gvr_track_ensure_buffers(vt, MAX_PREVIEW_WIDTH, MAX_PREVIEW_HEIGHT)) {
		gvr_close_connection( vt );
		return -1;
	}

	if(new_track)
		*new_track = track_num;

	veejay_memset(vt->data_buffer, 128, vt->data_buffer_size );
	veejay_memset(vt->tmp_buffer, 0, vt->tmp_buffer_size );

	vp->tracks[ track_num ] = vt;
	vp->track_sync->active_list[ track_num ] = 1;
	return 1;
}



static	void	gvr_single_queue_vims( veejay_track_t *v, int vims_id )
{
	char message[16];

	sprintf(message, "%03d:;", vims_id );

	if( v->n_queued < __MAX_TRACKS )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}

static void	gvr_multi_queue_vims( veejay_track_t *v, int vims_id, int val )
{
	char message[64];

	sprintf(message, "%03d:%d;", vims_id,val );

	if( v->n_queued < __MAX_TRACKS )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}
static	void	gvr_multivx_queue_vims( veejay_track_t *v, int vims_id, int val1,unsigned char *val2 )
{
	char message[1024];

	sprintf(message, "%03d:%d %s;", vims_id,val1,val2 );

	if( v->n_queued < __MAX_TRACKS )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}
static	void	gvr_multivvv_queue_vims( veejay_track_t *v, int vims_id, int val1,int val2, int val3 )
{
	char message[64];

	sprintf(message, "%03d:%d %d %d;", vims_id,val1,val2, val3 );

	if( v->n_queued < __MAX_TRACKS )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}

static	void	gvr_multiv_queue_vims( veejay_track_t *v, int vims_id, int val1,int val2 )
{
	char message[64];

	sprintf(message, "%03d:%d %d;", vims_id,val1,val2 );

	if( v->n_queued < __MAX_TRACKS )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}
void		gvr_queue_cxvims( void *preview, int track_id, int vims_id, int val1,unsigned char *val2 )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int i;

	if(!vp)
		return;

	if( track_id == -1 )
	{
		for( i = 0; i < vp->n_tracks; i ++ )
			if( vp->tracks[i] && vp->tracks[i]->active )
				gvr_multivx_queue_vims( vp->tracks[i], vims_id,val1,val2 );
	}
	else
	{
		veejay_track_t *v = gvr_track_ptr(vp, track_id);
		if( v && v->active)
			gvr_multivx_queue_vims( v, vims_id,val1,val2 );
	}
}

void		gvr_queue_vims( void *preview, int track_id, int vims_id )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int i;

	if(!vp)
		return;

	if( track_id == -1 )
	{
		for( i = 0; i < vp->n_tracks; i ++ )
			if( vp->tracks[i]  && vp->tracks[i]->active )
				gvr_single_queue_vims( vp->tracks[i], vims_id );
	}
	else
	{
		veejay_track_t *v = gvr_track_ptr(vp, track_id);
		if( v && v->active)
			gvr_single_queue_vims( v, vims_id );
	}
}

void		gvr_queue_mvims( void *preview, int track_id, int vims_id, int val )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int i;

	if(!vp)
		return;

	if( track_id == -1 )
	{
		for( i = 0; i < vp->n_tracks ; i ++ )
			if( vp->tracks[i] && vp->tracks[i]->active )
				gvr_multi_queue_vims( vp->tracks[i], vims_id,val );
	}
	else
	{
		veejay_track_t *v = gvr_track_ptr(vp, track_id);
		if( v && v->active )
			gvr_multi_queue_vims( v, vims_id,val );
	}
}

void		gvr_need_track_list( void *preview, int track_id )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	veejay_track_t *v = gvr_track_ptr(vp, track_id);

	if(v)
		v->need_track_list = 1;
}

void		gvr_queue_mmvims( void *preview, int track_id, int vims_id, int val1,int val2 )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int i;

	if(!vp)
		return;

	if( track_id == -1 )
	{
		for( i = 0; i < vp->n_tracks; i ++ )
			if( vp->tracks[i] && vp->tracks[i]->active )
				gvr_multiv_queue_vims( vp->tracks[i], vims_id,val1,val2 );
	}
	else
	{
		veejay_track_t *v = gvr_track_ptr(vp, track_id);
		if( v && v->active)
			gvr_multiv_queue_vims( v, vims_id,val1,val2 );
	}
}

void		gvr_queue_mmmvims( void *preview, int track_id, int vims_id, int val1,int val2, int val3 )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	int i;

	if(!vp)
		return;

	if( track_id == -1 )
	{
		for( i = 0; i < vp->n_tracks; i ++ )
			if( vp->tracks[i] && vp->tracks[i]->active )
				gvr_multivvv_queue_vims( vp->tracks[i], vims_id,val1,val2,val3);
	}
	else
	{
		veejay_track_t *v = gvr_track_ptr(vp, track_id);
		if( v && v->active)
			gvr_multivvv_queue_vims( v, vims_id,val1,val2,val3 );
	}
}

void		gvr_track_disconnect( void *preview, int track_num )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	veejay_track_t *v;

	if(!gvr_track_index_valid(vp, track_num))
		return;

	v = vp->tracks[track_num];
	if(!v) {
		if(vp->track_sync)
			vp->track_sync->active_list[track_num] = 0;
		return;
	}

    gvr_close_connection( v );

    vp->tracks[ track_num ] = NULL;
	if(vp->track_sync) {
		vp->track_sync->active_list[ track_num ] = 0;
		vp->track_sync->frame_list[ track_num ] = 0;
		vp->track_sync->widths[ track_num ] = 0;
		vp->track_sync->heights[ track_num ] = 0;
	}

    veejay_msg(VEEJAY_MSG_INFO,"Closed track %d", track_num);
}

int		gvr_track_configure( void *preview, int track_num, int wid, int hei )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	veejay_track_t *v;
	int w;
	int h;

	if(!gvr_track_index_valid(vp, track_num))
		return 0;

	w = wid;
	h = hei;

	if(w < 16)
		w = 16;
	if(h < 16)
		h = 16;
	w &= ~1;
	h &= ~1;

	v = vp->tracks[track_num];
	if( v && v->width == w && v->height == h )
	{
		if( vp->track_sync ) {
			vp->track_sync->widths[track_num] = w;
			vp->track_sync->heights[track_num] = h;
		}
		return 1;
	}

	if( v )
	{
		if(!gvr_track_ensure_buffers(v, w, h))
			return 0;
		v->width  = w;
		v->height = h;
		v->have_frame = 0;
	}

	if( vp->track_sync ) {
		vp->track_sync->widths[track_num] = w;
		vp->track_sync->heights[track_num] = h;
	}

	if( v ) {
		veejay_memset(v->data_buffer , 0, w * h );
		veejay_memset(v->data_buffer + (w*h), 128, (w * h * 2) );
	}
	return 1;
}

int		gvr_get_preview_status( void *preview, int track_num )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	veejay_track_t *v = gvr_track_ptr(vp, track_num);

	return v ? v->preview : 0;
}


int		gvr_track_toggle_preview( void *preview, int track_num, int status )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	veejay_track_t *v = gvr_track_ptr(vp, track_num);

	if(!gvr_track_index_valid(vp, track_num)) {
		veejay_msg(0, "Track %d is not valid", track_num);
		return 0;
	}

	if(!v) {
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Ignoring preview toggle for unopened track %d", track_num);
		return 0;
	}

    status = status ? 1 : 0;
    if(v->preview == status)
        return 1;

    v->preview = status;

    veejay_msg(VEEJAY_MSG_INFO, "Live view %dx%d with %s:%d on track %d %s",
	    v->width,
	    v->height,
	    v->hostname,
	    v->port_num,
	    track_num,
	    (v->preview ? "enabled" : "disabled") );

	return 1;
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
		if( vp->tracks[i] && vp->tracks[i]->active && vp->track_sync->widths[i] > 0 && vp->tracks[i]->preview &&
			vp->tracks[i]->tmp_buffer != NULL )
		{
			GdkPixbuf *wrapped = gdk_pixbuf_new_from_data(vp->tracks[i]->tmp_buffer, GDK_COLORSPACE_RGB, FALSE,
				8, vp->tracks[i]->width, vp->tracks[i]->height,
				vp->tracks[i]->width * 3, NULL, NULL);

			if(wrapped) {
				list[i] = gdk_pixbuf_copy(wrapped);
				g_object_unref(wrapped);
			}
		}
	}

	return list;
}

static	int	*int_dup( int *status )
{
	int *res = (int*) vj_calloc( sizeof(int) * VJ_STATUS_ARRAY_SIZE );
    veejay_memcpy( res, status, VJ_STATUS_ARRAY_SIZE * sizeof(int));
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
	int *list = (int*) vj_calloc( sizeof(int) * vp->n_tracks );
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
	int *list = (int*) vj_calloc( sizeof(int) * vp->n_tracks );
	if(!list)
		return NULL;

	int i;
	for( i = 0; i < vp->n_tracks; i ++ )
		if( vp->tracks[i] && vp->tracks[i]->active )
			list[i] = vp->track_sync->heights[i];

	return list;
}

sync_info	*gvr_sync( void *preview, void *caller_data )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	sync_info *s = (sync_info*) vj_calloc(sizeof(sync_info));

	if(gvr_veejay_grabber_step( preview, caller_data )) {

		s->status_list = gvr_grab_stati( preview );
		s->tracks      = vp->n_tracks;
		s->widths      = gvr_grab_widths( preview );
		s->heights     = gvr_grab_heights( preview);
		s->img_list    = gvr_grab_images( preview );

	}

	return s;
}


static	void	gvr_parse_track_list( veejay_preview_t *vp, veejay_track_t *v, unsigned char *tmp, int len )
{
	int i = 0;
	int items = 0;
	unsigned char *ptr = tmp;

	if(!vp || !v || !tmp || len <= 0)
		return;

	for(i = 0; i < __MAX_TRACKS; i++)
		v->track_list[i] = -1;
	v->track_items = 0;

	char **z = vj_calloc( sizeof( char * ) * vp->n_tracks );
	if(!z)
		return;

	i = 0;
	while( i + 3 <= len )
	{
		int k;
		char k_str[4];
		veejay_memset(k_str, 0, sizeof(k_str));
		memcpy(k_str, ptr, 3);
		k = atoi(k_str);
		ptr += 3;
		i += 3;

		if( k <= 0 || i + k > len )
			break;

		if(items < vp->n_tracks)
		{
			z[items] = strndup( (char*) ptr, k );
			items ++;
		}

		ptr += k;
		i += k;
	}

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
				if( sscanf( (char*) z[i], "%254s %d %d", hostname, &port, &stream_id ) == 3 )
				{
					if( gvr_host_matches(hostname, t->hostname) && port == t->port_num )
						in_track = k;
				}
			}
		}

		v->track_list[i] = in_track;
		free( z[i] );
	}
	v->track_items = items;

	free( z );
}

int		gvr_get_stream_id( void  *data, int id )
{
	veejay_preview_t *vp = (veejay_preview_t*) data;
	veejay_track_t *v = gvr_track_ptr(vp, id);

	if(v)
		return v->track_list[id];
	return 0;
}

static	void	gvr_parse_queue( veejay_track_t *v )
{
	int i;

	for( i = 0; i < v->n_queued ; i ++ )
	{
		if( vj_client_send( v->fd, V_CMD, (unsigned char*) v->queue[i] ) == -1  &&
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


	if( gvr_preview_process_image( vp,v ))
		score++;
	else
	{
		vj_client_close(v->fd);
    	int ok = vj_client_connect( v->fd, v->hostname, NULL, v->port_num );
		if( ok <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Veejay grabber: unable to reconnect to %s; destroying track %d",
				(v->hostname ? v->hostname : "<unknown>"),
				track_num );
			vj_client_free(v->fd);
			if(v->hostname) free(v->hostname);
     		if(v->status_buffer) free(v->status_buffer);
			if(v->data_buffer) free(v->data_buffer);
			if(v->tmp_buffer) free(v->tmp_buffer);
			v->data_buffer = NULL;
			v->tmp_buffer = NULL;
			vp->tracks[track_num] = NULL;

			if(  v->is_master )
				reloaded_schedule_restart();
			free(v);
		}
		else
		{


			v->active = 1;
  			vj_msg(VEEJAY_MSG_WARNING, "Veejay grabber: %s:%d track %d @ %dx%d preview: %s",
				v->hostname, v->port_num, track_num, v->width,v->height, (v->preview ? "yes" : "no"));
		}
	}

	return score;
}

static int gvr_veejay_grabber_step( void *data, void *caller_data )
{
	veejay_preview_t *vp = (veejay_preview_t*) data;
	int i;

	for( i = 0; i < vp->n_tracks ; i ++ )
	{
        if(!vp->tracks[i])
            continue;
        if( !vp->tracks[i]->active )
            continue;

		if(gvr_preview_process_status( vp, vp->tracks[i] ))
		{
		    gvr_get_preview_status( vp, i );
	    }
        else
        {
           gvr_track_disconnect(vp, i);
           multitrack_cleanup_track(caller_data, i);
		   return 0;
        }
	}

	for( i = 0; i < vp->n_tracks ; i ++ )
	{
		if( vp->tracks[i] && vp->tracks[i]->active)
		{
			if( vp->tracks[i] )
				gvr_veejay( vp, vp->tracks[i],i );
		}
	}

	return 1;

}


