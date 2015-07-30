/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
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
/*
	inspired by ffmpeg/ffmpeg/libavformat/dv1394.c
	dv1394 has no audio apparently ...
*/
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#ifdef SUPPORT_READ_DV2
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <libel/vj-el.h>
#include <libel/vj-dv.h>
#include <libvjmsg/vj-msg.h>
#include <libstream/vj-dv1394.h>
#include <libstream/dv1394.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <errno.h>
#define DV_PAL_SIZE 144000
#define DV_NTSC_SIZE 120000


#define DV1394_DEFAULT_CHANNEL 63
#define DV1394_DEFAULT_CARD    0
#define DV1394_RING_FRAMES     10

static	int	vj_dv1394_reset(vj_dv1394 *v  )
{
	struct dv1394_init init;
	init.channel = v->channel;
	init.api_version = DV1394_API_VERSION;
	init.n_frames = DV1394_RING_FRAMES;
	init.format = v->norm;

	if( ioctl( v->handle, DV1394_INIT, &init ) < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize ieee1394 device");
		return 0;
	}
	v->done = v->avail = 0;
	return 1;
}

static	int	vj_dv1394_start(vj_dv1394 *v )
{
	/* enable receiver */
	if( ioctl( v->handle, DV1394_START_RECEIVE, 0) < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot start receiver");
		return 0;
	}
	return 1;
}

vj_dv1394      *vj_dv1394_init(void *e, int channel, int quality)
{
	editlist *el = (editlist*)e;

	if(el->video_width != 720 && ( el->video_height != 576 || el->video_height != 480) )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No software scaling to %d x %d",el->video_width,
			el->video_height);
		return NULL;
	}

	vj_dv1394 *v = (vj_dv1394*)vj_malloc(sizeof(vj_dv1394));

	v->map_size = (el->video_norm == 'p' ? DV_PAL_SIZE: DV_NTSC_SIZE);
	v->handle = -1;
	v->width = el->video_width;
	v->height = el->video_height;
	v->norm = (el->video_norm == 'p' ? DV1394_PAL: DV1394_NTSC );
	v->handle = open( "/dev/dv1394", O_RDONLY);
	v->channel = channel == -1 ? DV1394_DEFAULT_CHANNEL : channel;
	v->index = 0;
	v->quality = quality;
	if( v->handle <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "opening /dev/dv1394'");
		if(v) free(v);
		return NULL;
	}
	if( vj_dv1394_reset(v) <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to initialize DV interface");
		close(v->handle);
		if(v) free(v);
		return NULL;
	}

	v->map = mmap( NULL, v->map_size * DV1394_RING_FRAMES, PROT_READ,
			MAP_PRIVATE, v->handle, 0);
	if(v->map == MAP_FAILED)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to mmap dv ring buffer");
		close(v->handle);
		if(v)free(v);
		return NULL;
	}
	if( vj_dv1394_start(v) <= 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to start capturing");
		if(v)free(v);
		close(v->handle);
		return NULL;
	}


	v->decoder = (void*)vj_dv_decoder_init( v->quality,v->width,v->height, el->pixel_format );
	if(!v->decoder)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to initailize DV decoder");
	}

	return v;
}


void	vj_dv1394_close(vj_dv1394 *v)
{
	if(v)
	{
		if( ioctl( v->handle, DV1394_SHUTDOWN, 0) < 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to shutdown dv1394");
		}
		if( munmap( v->map, v->map_size * DV1394_RING_FRAMES ) < 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to munmap dv1394 ring buffer");
		}
		close(v->handle);
		if(v->decoder)
			vj_dv_free_decoder( (vj_dv_decoder*) v->decoder );
		free(v);
	}

}

int	vj_dv1394_read_frame(vj_dv1394 *v, uint8_t *frame[3], uint8_t *audio, int fmt)
{
	if( !v->avail )
	{
		struct dv1394_status s;
		struct pollfd p;
		if( v->done )
		{
			/* request more frames */
			if( ioctl( v->handle, DV1394_RECEIVE_FRAMES, v->done ) < 0 )
			{
				veejay_msg(VEEJAY_MSG_DEBUG, "Ring buffer overflow,reset");
				vj_dv1394_reset( v );	
				vj_dv1394_start( v );
			}
			v->done = 0;
		}

restart_poll:
		p.fd = v->handle;
		p.events = POLLIN | POLLERR | POLLHUP;
		if( poll(&p, 1, -1 ) < 0 )
		{
			if( errno == EAGAIN || errno == EINTR )
			{
				veejay_msg(VEEJAY_MSG_DEBUG, "Waiting for DV");
				goto restart_poll;
			}
			veejay_msg(VEEJAY_MSG_ERROR, "Poll failed");
			return 0;
		}

		if( ioctl( v->handle, DV1394_GET_STATUS, &s ) < 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to get status");
			return 0;
		}

		v->avail = s.n_clear_frames;
		v->index = s.first_clear_frame;
		v->done	= 0;

		if( s.dropped_frames )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "dv1394: frame drop detected %d",
				s.dropped_frames);
		//	vj_dv1394_reset( v );
		//	vj_dv1394_start( v );
		}
	}

	if(!vj_dv_decode_frame( 
		(vj_dv_decoder*) v->decoder,
		v->map + (v->index * v->map_size),
		frame[0],frame[1],frame[2],
		v->width,
		v->height, fmt ))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "decoding DV frame");
			return 0;
		}

	v->index = (v->index + 1) % DV1394_RING_FRAMES;
	v->done ++ ;
	v->avail --;

	return 1;
}
#endif 
