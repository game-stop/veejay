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
#include <limits.h>
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
extern void vj_gui_process_pattern_status(const int *tokens);

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
	vj_client *ui_fd;
	vj_client *preview_fd;
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
    int	preview;
	int	width;
	int	height;
	int	prevmode;
	int	need_track_list;
	int need_sequence_timeline;
	int sequence_timeline_bank;
	gvr_sequence_timeline_t sequence_timeline;
	char 	*queue[__MAX_TRACKS];
	int	n_queued;
	int	is_master;
	GThread *preview_thread;
	GMutex preview_lock;
	GCond preview_cond;
	gboolean preview_stop;
	gboolean preview_request;
	int preview_alpha_only;
	int preview_failures;
	int preview_connected;
	GdkPixbuf *preview_pixbuf;
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

static int sendvims( vj_client *client, int vims_id, const char format[], ... );
static int recvvims( vj_client *client, gint header_len, gint *payload, guchar *buffer, int *full_range );
static int veejay_get_image_data(veejay_track_t *v, int width, int height, int alpha_only );
static int track_find(  veejay_preview_t *vp );
static int veejay_process_status( veejay_preview_t *vp, veejay_track_t *v );
static int track_exists( veejay_preview_t *vp, const char *hostname, int port_num, int *at );
static int gvr_preview_process_status( veejay_preview_t *vp, veejay_track_t *v );
static int gvr_veejay_grabber_step( void *data, void *caller_data );
static gpointer gvr_preview_worker( gpointer data );
static void gvr_preview_request_frame( veejay_track_t *v );


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
	(void) use_threads;
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

static void gvr_preview_worker_stop(veejay_track_t *v)
{
	if(!v || !v->preview_thread)
		return;

	g_mutex_lock(&v->preview_lock);
	v->preview_stop = TRUE;
	g_cond_broadcast(&v->preview_cond);
	g_mutex_unlock(&v->preview_lock);

	if(v->preview_fd)
		vj_client_shutdown(v->preview_fd, V_CMD);

	g_thread_join(v->preview_thread);
	v->preview_thread = NULL;
}

static	void	gvr_close_connection( veejay_track_t *v )
{
    if(v != NULL) {
        veejay_msg(VEEJAY_MSG_INFO, "Closing connection %s:%d", v->hostname ? v->hostname : "<unknown>", v->port_num);

        gvr_preview_worker_stop(v);

        if(v->preview_fd) {
            vj_client_close(v->preview_fd);
            vj_client_free(v->preview_fd);
            v->preview_fd = NULL;
        }

        if(v->fd) {
            vj_client_close(v->fd);
            vj_client_free(v->fd);
            v->fd = NULL;
        }

        if(v->ui_fd) {
            vj_client_close(v->ui_fd);
            vj_client_free(v->ui_fd);
            v->ui_fd = NULL;
        }

        if(v->preview_pixbuf) {
            g_object_unref(v->preview_pixbuf);
            v->preview_pixbuf = NULL;
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

        for(int i = 0; i < v->n_queued; i++)
            free(v->queue[i]);

        g_cond_clear(&v->preview_cond);
        g_mutex_clear(&v->preview_lock);
        free(v);
    }
}

static	int	sendvims( vj_client *client, int vims_id, const char format[], ... )
{
	gchar	block[32];
	gchar	tmp[255];
	va_list args;
	gint n;

	if(!client)
		return -1;

	if( format == NULL )
	{
		g_snprintf( block, sizeof(block)-1, "%03d:;", vims_id );
		n = vj_client_send( client, V_CMD, (unsigned char*) block );
		return n > 0 ? 1 : n;
	}

	va_start( args, format );
	vsnprintf( tmp, sizeof(tmp)-1, format, args );
	g_snprintf( block,sizeof(block)-1, "%03d:%s;", vims_id, tmp );
	va_end( args );

	n = vj_client_send( client, V_CMD,(unsigned char*) block );
	return n > 0 ? 1 : n;
}

static int	recvvims( vj_client *client, gint header_len, gint *payload, guchar *buffer, int *full_range )
{
	unsigned char tmp_stack[64];
	unsigned char *tmp = tmp_stack;
	memset(tmp_stack, 0, sizeof(tmp_stack));
	size_t len = 0;
	int wid = 0;
	int hei = 0;
	int range = 0;

	if(!client || header_len <= 0)
		return 0;

	if(header_len >= (gint)sizeof(tmp_stack)) {
		tmp = vj_calloc((size_t)header_len + 1);
		if(!tmp)
			return 0;
	}
	else {
		tmp[header_len] = '\0';
	}

	gint n = vj_client_read_no_wait( client, V_CMD, tmp, header_len );

	if( n <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Reading preview header of %d bytes: %d", header_len,n );
		if(tmp != tmp_stack)
			free(tmp);
		return n;
	}

	if( sscanf( (char*)tmp, "%06zu%04d%02d%1d", &len,&wid, &hei, &range ) != 4 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot parse preview header (data stream polluted)");
		if(tmp != tmp_stack)
			free(tmp);
		return 0;
	}

	if(full_range)
		*full_range = range;

	if( len <= 0 )
	{
		if(tmp != tmp_stack)
			free(tmp);
		veejay_msg(VEEJAY_MSG_ERROR, "Preview frame is empty");
		return 0;
	}

	size_t bw = 0;
	gint bytes_read = len;
	unsigned char *buf_ptr = buffer;

	*payload = 0;

	while( bw < len )
	{
		n = vj_client_read_no_wait( client, V_CMD, buf_ptr, bytes_read );
		if ( n <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Received %zu out of %zu preview bytes", bw,len);
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


static int gvr_read_framed_reply(veejay_track_t *v,
                                 int vims_id,
                                 const char *arguments,
                                 int header_len,
                                 unsigned char **body,
                                 int *body_len)
{
    unsigned char command[64];
    unsigned char header[16];
    int sent;
    int got;
    int length = 0;

    if(body)
        *body = NULL;
    if(body_len)
        *body_len = 0;
    if(!v || !v->fd || !body || !body_len || header_len <= 0 || header_len >= (int)sizeof(header))
        return 0;

    if(arguments && arguments[0])
        snprintf((char *)command, sizeof(command), "%03d:%s;", vims_id, arguments);
    else
        snprintf((char *)command, sizeof(command), "%03d:;", vims_id);

    sent = vj_client_send(v->fd, V_CMD, command);
    if(sent <= 0)
        return 0;

    got = 0;
    while(got < header_len) {
        int n = vj_client_read(v->fd, V_CMD, header + got, header_len - got);
        if(n <= 0)
            return 0;
        got += n;
    }

    header[header_len] = '\0';
    if(sscanf((char *)header, "%d", &length) != 1 || length <= 0 || length > 1024 * 1024)
        return 0;

    *body = (unsigned char *)vj_calloc((size_t)length + 1u);
    if(!*body)
        return 0;

    got = 0;
    while(got < length) {
        int n = vj_client_read(v->fd, V_CMD, *body + got, length - got);
        if(n <= 0) {
            free(*body);
            *body = NULL;
            return 0;
        }
        got += n;
    }

    (*body)[length] = '\0';
    *body_len = length;
    return 1;
}

static int gvr_parse_fixed_int(const unsigned char *text, int width, long long *value)
{
    char buffer[32];
    char *end = NULL;
    long long parsed;

    if(!text || !value || width <= 0 || width >= (int)sizeof(buffer))
        return 0;

    memcpy(buffer, text, (size_t)width);
    buffer[width] = '\0';
    parsed = strtoll(buffer, &end, 10);
    if(end != buffer + width)
        return 0;

    *value = parsed;
    return 1;
}

static int gvr_fetch_sequence_timeline(veejay_track_t *v, int bank)
{
    unsigned char *body = NULL;
    int body_len = 0;
    char arguments[16];
    long long value;
    int reply_bank;
    unsigned int revision;
    int finite;
    long long total;
    int entries;
    int offset;
    gvr_sequence_timeline_t parsed;

    if(!v || bank < 0 || bank >= 4)
        return 0;

    snprintf(arguments, sizeof(arguments), "%d", bank);
    if(!gvr_read_framed_reply(v,
                              VIMS_SEQUENCE_TIMELINE,
                              arguments,
                              VJ_SEQUENCE_TIMELINE_REPLY_PREFIX_LENGTH,
                              &body,
                              &body_len))
        return 0;

    if(body_len < VJ_SEQUENCE_TIMELINE_HEADER_LENGTH ||
       !gvr_parse_fixed_int(body, 2, &value)) {
        free(body);
        return 0;
    }
    reply_bank = (int)value;

    if(!gvr_parse_fixed_int(body + 2, 10, &value) || value < 0 || value > UINT_MAX) {
        free(body);
        return 0;
    }
    revision = (unsigned int)value;

    if(!gvr_parse_fixed_int(body + 12, 1, &value) || (value != 0 && value != 1)) {
        free(body);
        return 0;
    }
    finite = value != 0;

    if(!gvr_parse_fixed_int(body + 13, 12, &total) ||
       !gvr_parse_fixed_int(body + 25, 3, &value)) {
        free(body);
        return 0;
    }
    entries = (int)value;

    if(reply_bank != bank || entries < 0 || entries > GVR_SEQUENCE_TIMELINE_MAX_SLOTS ||
       body_len != VJ_SEQUENCE_TIMELINE_HEADER_LENGTH + entries * VJ_SEQUENCE_TIMELINE_ENTRY_LENGTH) {
        free(body);
        return 0;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.valid = 1;
    parsed.finite = finite;
    parsed.bank = bank;
    parsed.revision = revision;
    parsed.total_frames = finite && total > 0 ? (total > INT_MAX ? INT_MAX : (int)total) : 0;
    parsed.count = 0;
    offset = VJ_SEQUENCE_TIMELINE_HEADER_LENGTH;

    for(int i = 0; i < entries; i++) {
        long long slot;
        long long sample_id;
        long long sample_type;
        long long start;
        long long length;
        gvr_sequence_timeline_clip_t *clip;

        if(!gvr_parse_fixed_int(body + offset, 3, &slot) ||
           !gvr_parse_fixed_int(body + offset + 3, 5, &sample_id) ||
           !gvr_parse_fixed_int(body + offset + 8, 2, &sample_type) ||
           !gvr_parse_fixed_int(body + offset + 10, 12, &start) ||
           !gvr_parse_fixed_int(body + offset + 22, 12, &length)) {
            free(body);
            return 0;
        }

        if(slot < 0 || slot >= GVR_SEQUENCE_TIMELINE_MAX_SLOTS ||
           sample_id <= 0 || sample_id > INT_MAX || sample_type < 0 || sample_type > INT_MAX) {
            free(body);
            return 0;
        }

        offset += VJ_SEQUENCE_TIMELINE_ENTRY_LENGTH;
        if(start < 0 || length <= 0) {
            parsed.finite = 0;
            continue;
        }
        if(start > INT_MAX || length > INT_MAX || start > INT_MAX - length + 1) {
            free(body);
            return 0;
        }

        clip = &parsed.clips[parsed.count++];
        clip->slot = (int)slot;
        clip->sample_id = (int)sample_id;
        clip->sample_type = (int)sample_type;
        clip->project_in = (int)start;
        clip->project_out = (int)(start + length - 1);
        if(clip->project_out == INT_MAX)
            parsed.total_frames = INT_MAX;
        else if(clip->project_out + 1 > parsed.total_frames)
            parsed.total_frames = clip->project_out + 1;
    }

    free(body);
    v->sequence_timeline = parsed;
    return 1;
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

            if(v->is_master)
                vj_gui_process_pattern_status(v->status_tokens);
		}
	}

    if( k == -1 ) {
        veejay_msg(0, "Dropping connection to Veejay");
        return 0;
    }

	return 1;
}

extern int     is_button_toggled(const char *name);

static int veejay_get_image_data(veejay_track_t *v, int width, int height, int alpha_only)
{
    gint res = sendvims(v->preview_fd, VIMS_RGB24_IMAGE, "%d %d %d", width, height, alpha_only);
    if (res <= 0)
        return res;

    if(!gvr_track_ensure_buffers(v, width, height))
        return 0;

    gint bw = 0;
    int full_range = 0;
    res = recvvims(v->preview_fd, 13, &bw, v->data_buffer, &full_range);
    if (res <= 0 || bw <= 0) {
        veejay_msg(VEEJAY_MSG_WARNING, "Cannot get a preview image; only received %d bytes", bw);
        return res;
    }

    int y_size = width * height;
    int srcfmt;
    size_t total_needed;

    if (bw == y_size) {
        srcfmt = PIX_FMT_GRAY8;
        total_needed = y_size;
    } else {
        srcfmt = (full_range ? PIX_FMT_YUVJ420P : PIX_FMT_YUV420P);
        int uv_size = y_size / 4;
        total_needed = y_size + uv_size * 2;

        if ((size_t) bw < total_needed) {
            veejay_msg(VEEJAY_MSG_WARNING, "Received only %d bytes, expected %zu for YUV420", bw, total_needed);
            veejay_msg_buffer(v->data_buffer, bw,64, "veejay_get_image_data");
            return 0;
        }
    }

    uint8_t *in = v->data_buffer;
    uint8_t *buf_u = NULL;
    uint8_t *buf_v = NULL;

    if (srcfmt != PIX_FMT_GRAY8) {
        buf_u = in + y_size;
        buf_v = buf_u + (y_size / 4);
    }

    VJFrame *src1 = yuv_yuv_template(in, buf_u, buf_v, width, height, srcfmt);
    VJFrame *dst1 = yuv_rgb_template(v->tmp_buffer, width, height, PIX_FMT_RGB24);

    yuv_convert_any_ac(src1, dst1);

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

static int gvr_preview_worker_connect(veejay_track_t *v)
{
	if(v->preview_connected)
		return 1;

	if(!vj_client_connect_cmd(v->preview_fd, v->hostname, v->port_num))
		return 0;

	v->preview_connected = 1;
	return 1;
}

static gpointer gvr_preview_worker(gpointer data)
{
	veejay_track_t *v = (veejay_track_t*) data;

	for(;;)
	{
		int width;
		int height;
		int alpha_only;

		g_mutex_lock(&v->preview_lock);
		while(!v->preview_stop && !v->preview_request)
			g_cond_wait(&v->preview_cond, &v->preview_lock);

		if(v->preview_stop) {
			g_mutex_unlock(&v->preview_lock);
			break;
		}

		v->preview_request = FALSE;
		width = v->width;
		height = v->height;
		alpha_only = v->preview_alpha_only;
		if(!v->preview || width <= 0 || height <= 0) {
			g_mutex_unlock(&v->preview_lock);
			continue;
		}
		g_mutex_unlock(&v->preview_lock);

		if(!gvr_preview_worker_connect(v)) {
			v->preview_failures++;
			continue;
		}

		g_mutex_lock(&v->preview_lock);
		gboolean stopping = v->preview_stop;
		g_mutex_unlock(&v->preview_lock);
		if(stopping)
			break;

		int result = veejay_get_image_data(v, width, height, alpha_only);

		g_mutex_lock(&v->preview_lock);
		stopping = v->preview_stop;
		g_mutex_unlock(&v->preview_lock);
		if(stopping)
			break;

		if(result <= 0) {
			v->preview_failures++;
			if(result < 0 || v->preview_failures >= 3) {
				veejay_msg(VEEJAY_MSG_WARNING,
					"Preview connection %s:%d will be reopened",
					v->hostname, v->port_num);
				v->preview_connected = 0;
				v->preview_failures = 0;
			}
			continue;
		}

		v->preview_failures = 0;

		GdkPixbuf *wrapped = gdk_pixbuf_new_from_data(v->tmp_buffer,
			GDK_COLORSPACE_RGB, FALSE, 8, width, height, width * 3, NULL, NULL);
		GdkPixbuf *frame = wrapped ? gdk_pixbuf_copy(wrapped) : NULL;
		if(wrapped)
			g_object_unref(wrapped);
		if(!frame)
			continue;

		GdkPixbuf *old = NULL;
		g_mutex_lock(&v->preview_lock);
		if(!v->preview_stop && v->preview && v->width == width && v->height == height) {
			old = v->preview_pixbuf;
			v->preview_pixbuf = frame;
			v->have_frame = 1;
			frame = NULL;
		}
		g_mutex_unlock(&v->preview_lock);

		if(old)
			g_object_unref(old);
		if(frame)
			g_object_unref(frame);
	}

	return NULL;
}

static void gvr_preview_request_frame(veejay_track_t *v)
{
	g_mutex_lock(&v->preview_lock);
	if(!v->preview_stop && v->preview && v->width > 0 && v->height > 0) {
		v->preview_alpha_only = alphaonly_view;
		v->preview_request = TRUE;
		g_cond_signal(&v->preview_cond);
	}
	g_mutex_unlock(&v->preview_lock);
}

void		gvr_set_master(void *data, int master_track )
{
	veejay_preview_t *vp = (veejay_preview_t*) data;
	int i;

	veejay_track_t *master = gvr_track_ptr(vp, master_track);

	if(!master)
		return;

	for( i = 0; i < vp->n_tracks; i ++ )
		if( vp->tracks[i] )
			vp->tracks[i]->is_master = 0;

	master->is_master = 1;
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

	g_mutex_init(&vt->preview_lock);
	g_cond_init(&vt->preview_cond);

	vt->fd       = fd;
	vt->preview_fd = vj_client_alloc();
	vt->hostname = strdup(hostname);
	vt->port_num  = port_num;
	vt->active   = 1;
	vt->sequence_timeline_bank = -1;

	if(!vt->preview_fd || !vt->hostname)
	{
		gvr_close_connection(vt);
		return -1;
	}

	vt->status_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * STATUS_LENGTH);
	if(vt->status_buffer == NULL ) {
		gvr_close_connection( vt );
		return -1;
	}

	GError *thread_error = NULL;
	vt->preview_thread = g_thread_try_new("gvr-preview", gvr_preview_worker, vt, &thread_error);
	if(!vt->preview_thread) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot start preview worker: %s",
			thread_error ? thread_error->message : "unknown error");
		if(thread_error)
			g_error_free(thread_error);
		gvr_close_connection(vt);
		return -1;
	}

	if(new_track)
		*new_track = track_num;

	vp->tracks[ track_num ] = vt;
	vp->track_sync->active_list[ track_num ] = 1;
	return 1;
}



static	void	gvr_single_queue_vims( veejay_track_t *v, int vims_id )
{
	char message[16];

	snprintf(message, sizeof(message), "%03d:;", vims_id );

	if( v->n_queued < __MAX_TRACKS )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}

static void	gvr_multi_queue_vims( veejay_track_t *v, int vims_id, int val )
{
	char message[64];

	snprintf(message, sizeof(message), "%03d:%d;", vims_id,val );

	if( v->n_queued < __MAX_TRACKS )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}
static	void	gvr_multivx_queue_vims( veejay_track_t *v, int vims_id, int val1,unsigned char *val2 )
{
	char message[1024];

	snprintf(message, sizeof(message), "%03d:%d %s;", vims_id,val1,val2 );

	if( v->n_queued < __MAX_TRACKS )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}
static	void	gvr_multivvv_queue_vims( veejay_track_t *v, int vims_id, int val1,int val2, int val3 )
{
	char message[64];

	snprintf(message, sizeof(message), "%03d:%d %d %d;", vims_id,val1,val2, val3 );

	if( v->n_queued < __MAX_TRACKS )
	{
		v->queue[ v->n_queued ] = strdup( message );
		v->n_queued ++;
	}
}

static void gvr_multivvvv_queue_vims(veejay_track_t *v, int vims_id,
                                      int val1, int val2, int val3, int val4)
{
    char message[128];

    snprintf(message, sizeof(message), "%03d:%d %d %d %d;",
             vims_id, val1, val2, val3, val4);

    if(v->n_queued < __MAX_TRACKS)
        v->queue[v->n_queued++] = strdup(message);
}

static void gvr_multivvvvv_queue_vims(veejay_track_t *v, int vims_id,
                                            int val1, int val2, int val3,
                                            int val4, int val5)
{
	char message[128];

	snprintf(message, sizeof(message), "%03d:%d %d %d %d %d;",
	         vims_id, val1, val2, val3, val4, val5);

	if(v->n_queued < __MAX_TRACKS)
		v->queue[v->n_queued++] = strdup(message);
}

static	void	gvr_multiv_queue_vims( veejay_track_t *v, int vims_id, int val1,int val2 )
{
	char message[64];

	snprintf(message, sizeof(message), "%03d:%d %d;", vims_id,val1,val2 );

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


void gvr_need_sequence_timeline(void *preview, int track_id, int bank)
{
    veejay_preview_t *vp = (veejay_preview_t*)preview;
    veejay_track_t *v = gvr_track_ptr(vp, track_id);

    if(!v || bank < 0 || bank >= 4)
        return;

    v->sequence_timeline_bank = bank;
    v->need_sequence_timeline = 1;
}

int gvr_get_sequence_timeline(void *preview, int track_id, gvr_sequence_timeline_t *timeline)
{
    veejay_preview_t *vp = (veejay_preview_t*)preview;
    veejay_track_t *v = gvr_track_ptr(vp, track_id);

    if(!v || !timeline || !v->sequence_timeline.valid)
        return 0;

    *timeline = v->sequence_timeline;
    return 1;
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

void gvr_queue_mmmmvims(void *preview, int track_id, int vims_id,
                         int val1, int val2, int val3, int val4)
{
    veejay_preview_t *vp = (veejay_preview_t*)preview;

    if(!vp)
        return;

    if(track_id == -1)
    {
        for(int i = 0; i < vp->n_tracks; i++)
            if(vp->tracks[i] && vp->tracks[i]->active)
                gvr_multivvvv_queue_vims(vp->tracks[i], vims_id,
                                         val1, val2, val3, val4);
    }
    else
    {
        veejay_track_t *v = gvr_track_ptr(vp, track_id);
        if(v && v->active)
            gvr_multivvvv_queue_vims(v, vims_id,
                                     val1, val2, val3, val4);
    }
}

void gvr_queue_mmmmmvims(void *preview, int track_id, int vims_id,
                         int val1, int val2, int val3, int val4, int val5)
{
	veejay_preview_t *vp = (veejay_preview_t*)preview;

	if(!vp)
		return;

	if(track_id == -1)
	{
		for(int i = 0; i < vp->n_tracks; i++)
			if(vp->tracks[i] && vp->tracks[i]->active)
				gvr_multivvvvv_queue_vims(vp->tracks[i], vims_id,
				                            val1, val2, val3, val4, val5);
	}
	else
	{
		veejay_track_t *v = gvr_track_ptr(vp, track_id);
		if(v && v->active)
			gvr_multivvvvv_queue_vims(v, vims_id,
			                            val1, val2, val3, val4, val5);
	}
}

int gvr_track_prepare_ui_client(void *preview, int track_num)
{
    veejay_preview_t *vp = (veejay_preview_t*) preview;
    veejay_track_t *v = gvr_track_ptr(vp, track_num);
    vj_client *client;

    if(!v || !v->active)
        return 0;
    if(v->ui_fd)
        return 1;

    client = vj_client_alloc();
    if(!client)
        return 0;
    if(!vj_client_connect(client, v->hostname, NULL, v->port_num)) {
        vj_client_free(client);
        return 0;
    }

    v->ui_fd = client;
    return 1;
}

void *gvr_track_take_ui_client(void *preview, int track_num)
{
    veejay_preview_t *vp = (veejay_preview_t*) preview;
    veejay_track_t *v = gvr_track_ptr(vp, track_num);
    vj_client *client;

    if(!v)
        return NULL;

    client = v->ui_fd;
    v->ui_fd = NULL;
    return client;
}

void gvr_track_store_ui_client(void *preview, int track_num, void *client_ptr)
{
    veejay_preview_t *vp = (veejay_preview_t*) preview;
    veejay_track_t *v = gvr_track_ptr(vp, track_num);
    vj_client *client = (vj_client*) client_ptr;

    if(!v) {
        if(client) {
            vj_client_close(client);
            vj_client_free(client);
        }
        return;
    }

    if(v->ui_fd && v->ui_fd != client) {
        vj_client_close(v->ui_fd);
        vj_client_free(v->ui_fd);
    }

    v->ui_fd = client;
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
	GdkPixbuf *old = NULL;
	if(v)
	{
		g_mutex_lock(&v->preview_lock);
		if(v->width != w || v->height != h) {
			v->width = w;
			v->height = h;
			v->have_frame = 0;
			old = v->preview_pixbuf;
			v->preview_pixbuf = NULL;
			if(v->preview) {
				v->preview_alpha_only = alphaonly_view;
				v->preview_request = TRUE;
				g_cond_signal(&v->preview_cond);
			}
		}
		g_mutex_unlock(&v->preview_lock);
	}

	if(old)
		g_object_unref(old);

	if( vp->track_sync ) {
		vp->track_sync->widths[track_num] = w;
		vp->track_sync->heights[track_num] = h;
	}

	return 1;
}

int		gvr_get_preview_status( void *preview, int track_num )
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	veejay_track_t *v = gvr_track_ptr(vp, track_num);
	int status = 0;

	if(v) {
		g_mutex_lock(&v->preview_lock);
		status = v->preview;
		g_mutex_unlock(&v->preview_lock);
	}

	return status;
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

	GdkPixbuf *old = NULL;
	int width;
	int height;
	g_mutex_lock(&v->preview_lock);
	if(v->preview == status) {
		g_mutex_unlock(&v->preview_lock);
		return 1;
	}

	v->preview = status;
	width = v->width;
	height = v->height;
	if(v->preview) {
		v->preview_alpha_only = alphaonly_view;
		v->preview_request = TRUE;
		g_cond_signal(&v->preview_cond);
	}
	else {
		v->preview_request = FALSE;
		v->have_frame = 0;
		old = v->preview_pixbuf;
		v->preview_pixbuf = NULL;
	}
	g_mutex_unlock(&v->preview_lock);

	if(old)
		g_object_unref(old);

    veejay_msg(VEEJAY_MSG_INFO, "Live view %dx%d with %s:%d on track %d %s",
	    width,
	    height,
	    v->hostname,
	    v->port_num,
	    track_num,
	    (status ? "enabled" : "disabled") );

	return 1;
}

static GdkPixbuf	**gvr_grab_images(void *preview)
{
	veejay_preview_t *vp = (veejay_preview_t*) preview;
	GdkPixbuf **list = (GdkPixbuf**) vj_calloc( sizeof(GdkPixbuf*) * vp->n_tracks );
	if(!list)
		return NULL;

	for( int i = 0; i < vp->n_tracks; i ++ )
	{
		veejay_track_t *v = vp->tracks[i];
		if(!v || !v->active)
			continue;

		g_mutex_lock(&v->preview_lock);
		if(v->preview && v->have_frame && v->preview_pixbuf)
			list[i] = g_object_ref(v->preview_pixbuf);
		g_mutex_unlock(&v->preview_lock);
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
	int offset = 0;
	unsigned char *ptr = tmp;

	if(!vp || !v || !tmp || len <= 0)
		return;

	for(int i = 0; i < __MAX_TRACKS; i++)
		v->track_list[i] = -1;
	v->track_items = 0;

	while(offset + 3 <= len)
	{
		char length_text[4];
		int item_length;
		char *item;
		char hostname[255];
		int port = 0;
		int stream_id = 0;

		veejay_memset(length_text, 0, sizeof(length_text));
		memcpy(length_text, ptr, 3);
		item_length = atoi(length_text);
		ptr += 3;
		offset += 3;

		if(item_length <= 0 || offset + item_length > len)
			break;

		item = strndup((char*)ptr, item_length);
		ptr += item_length;
		offset += item_length;
		if(!item)
			continue;

		veejay_memset(hostname, 0, sizeof(hostname));
		if(sscanf(item, "%254s %d %d", hostname, &port, &stream_id) == 3 &&
		   stream_id > 0)
		{
			for(int track = 0; track < vp->n_tracks && track < __MAX_TRACKS; track++)
			{
				veejay_track_t *source = vp->tracks[track];
				if(source && gvr_host_matches(hostname, source->hostname) &&
				   port == source->port_num)
				{
					if(v->track_list[track] <= 0)
						v->track_items++;
					v->track_list[track] = stream_id;
					break;
				}
			}
		}

		free(item);
	}
}

int gvr_get_stream_id_for(void *data, int destination_track, int source_track)
{
	veejay_preview_t *vp = (veejay_preview_t*)data;
	veejay_track_t *v;

	if(!vp || destination_track < 0 || destination_track >= __MAX_TRACKS ||
	   source_track < 0 || source_track >= __MAX_TRACKS)
		return 0;

	v = gvr_track_ptr(vp, destination_track);
	if(v && v->track_list[source_track] > 0)
		return v->track_list[source_track];
	return 0;
}

int gvr_get_stream_id(void *data, int id)
{
	return gvr_get_stream_id_for(data, 0, id);
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
static int gvr_veejay( veejay_preview_t *vp , veejay_track_t *v, int track_num )
{
    (void) track_num;
    int score = 0;

    /* A stream-create command must be processed before the track-list query
     * that resolves the resulting stream ID. */
    if(v->n_queued > 0) {
        gvr_parse_queue(v);
        score++;
    }

    if(v->need_track_list) {
        int bw = 0;
        unsigned char *tmp = vims_track_list(v, 5, &bw);
        if(tmp && bw > 0) {
            gvr_parse_track_list(vp, v, tmp, bw);
            v->need_track_list = 0;
        }
        free(tmp);
        score++;
    }

    if(v->need_sequence_timeline) {
        if(gvr_fetch_sequence_timeline(v, v->sequence_timeline_bank))
            v->need_sequence_timeline = 0;
        score++;
    }

    gvr_preview_request_frame(v);
    return score + 1;
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


