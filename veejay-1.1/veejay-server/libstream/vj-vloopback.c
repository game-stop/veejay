/* veejay - Linux VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
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
	Put vloopback back in place
	Re-used large portions of dc1394_vloopback.c 
	from Dan Dennedy <dan@dennedy.org>

*/

/*
	vloopback pusher (using pipes)
	If someone wants to implement mmap, add SIGIO to the signal catcher
	and use mutexes for asynchronosouly handling IO. I am too lazy.
 */

#include <config.h>
#ifdef HAVE_V4L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <linux/videodev.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>

#define VLOOPBACK_MMAP 0 	// commented out
#define VLOOPBACK_PIPE 1
#define VLOOPBACK_N_BUFS 2

typedef struct
{
	char *dev_name;		/* device name */
	int   palette;		/* palette from vjframe */
	int   width;
	int   height;
	int   norm;
        int   mode;		/* PAL or NTSC */
	int   fd;
	int   size;		/* size of image out_buf */
	uint8_t *out_buf;
	uint8_t *out_map;	/* mmap segment */
} vj_vloopback_t;


/* Open the vloopback device */

void *vj_vloopback_open(const char *device_name, int norm, int mode,
		int w, int h, int pixel_format)
{
	void *ret = NULL;
	vj_vloopback_t *v = (vj_vloopback_t*) vj_malloc(sizeof(vj_vloopback_t));
	if(!v) return ret;

	memset(v , 0, sizeof(vj_vloopback_t ));

	v->fd = open( device_name, O_RDWR );
	v->norm = norm;
	v->mode = mode;
	v->width = w;
	v->height = h;
	v->palette = (pixel_format == 1 ? VIDEO_PALETTE_YUV422P :
				VIDEO_PALETTE_YUV420P );

	if(!v->fd)
	{
		if(v) free(v);
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot open vloopback %s",
			device_name );
		return ret;
	}

	v->dev_name = strdup( device_name );

	ret = (void*) v;

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Vloopback %s size %d x %d, palette YUV42%sP",
		v->dev_name,
		v->width,
		v->height,
		(pixel_format == 1  ? "2" : "0" ) );

	return (void*) ret;
}

int	vj_vloopback_get_mode( void *vloop )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	return v->mode;
}

/* write mode*/
int	vj_vloopback_start_pipe( void *vloop )
{
	struct video_capability caps;
	struct video_window	win;
	struct video_picture 	pic;

	vj_vloopback_t *v = (vj_vloopback_t*) vloop;

	if(!v) return 0;

	if(v->mode != VLOOPBACK_PIPE)
		veejay_msg(VEEJAY_MSG_ERROR,"Program error");

	/* the out_palette defines what format ! */

	/* get capabilities */
	if( ioctl( v->fd, VIDIOCGCAP, &caps ) < 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Cant get video capabilities");
		return 0;
	}
	/* get picture */
	if( ioctl( v->fd, VIDIOCGPICT, &pic ) < 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Cant get video picture");
		return 0;
	}
	/* set palette */
	pic.palette = v->palette;
	if( ioctl( v->fd, VIDIOCSPICT, &pic ) < 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Cant set video picture (palette %d)",v->palette);
		return 0;
	}
	/* set window */
	win.width  = v->width;
	win.height = v->height;
	if( ioctl( v->fd, VIDIOCSWIN, &win ) < 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG ,"Cant set video window %d x %d",
			v->width,v->height );
		return 0;
	}

	int len = v->width * v->height ;
	int vshift = (v->palette == 
		VIDEO_PALETTE_YUV422P ? 0 : 1 );
	int uv_len = (v->width >> 1 ) * (v->height >> vshift);

	v->size = len + (2 * uv_len);

	veejay_msg(VEEJAY_MSG_DEBUG,
	   "vloopback pipe (Y plane %d bytes, UV plane %d bytes) H=%d, V=%d",
		len,uv_len,1,vshift );

	v->out_buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * v->size );

	memset(v->out_buf, 0 , v->size );

	if(!v->out_buf)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant allocate sufficient memory for vloopback");
		return 0;
	}
	return 1;
}

int	vj_vloopback_write_pipe( void *vloop  )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	if(!v) return 0;
	int res = write( v->fd, v->out_buf, v->size );
	if(res <= 0)
		return 0;
	return 1;
}

int	vj_vloopback_fill_buffer( void *vloop, uint8_t **frame )
{
	// write frame to v->out_buf (veejay_memcpy)
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	if(!v) return 0;

	int len = v->width * v->height ;
	int hshift = (v->palette == 
		VIDEO_PALETTE_YUV422P ? 0 : 1 );
	int uv_len = (v->width >> hshift ) * (v->height >> 1);

	// copy data to linear buffer */	
	veejay_memcpy( v->out_buf, frame[0], len );

	veejay_memcpy( v->out_buf + len,
					frame[1], uv_len );
	veejay_memcpy( v->out_buf + len + uv_len,
					frame[2], uv_len );
	
	return 1;
}

/*
int	vj_vloopback_start_mmap( void *vloop )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	if(!v)
	 return 0;
	

	int len = v->width * v->height ;
	int hshift = (v->palette == 
		VIDEO_PALETTE_YUV422P ? 0 : 1 );
	int uv_len = (v->width >> hshift ) * (v->height >> 1);
	v->size = len + (2 * uv_len);
	v->out_buf = (uint8_t*) vj_malloc(
			sizeof(uint8_t) * v->size * VLOOPBACK_N_BUFS );

	if(!v->out_buf)
		return 0;

	v->out_map = mmap( 0,  (v->size * VLOOPBACK_N_BUFS), PROT_READ| PROT_WRITE,
				MAP_SHARED, v->fd , 0 );
	if( v->out_map == (uint8_t*) -1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot mmap memory");
		return 0;
	}
	veejay_msg(VEEJAY_MSG_ERROR, "%s", __FUNCTION__ );
	return 1;
}

int	vj_vloopback_write_mmap( void *vloop, int frame )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	veejay_memcpy( v->out_map + (v->size * frame), v->out_buf, v->size );
	return 1;
}

int	vj_vloopback_ioctl( void *vloop, unsigned long int cmd, void *arg )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
			veejay_msg(VEEJAY_MSG_INFO, "%s %d / %d",
				__FUNCTION__, __LINE__ , cmd);

	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			veejay_msg(VEEJAY_MSG_INFO, "%s %d",
				__FUNCTION__, __LINE__ );
			struct video_capability *cap = arg;
			sprintf( cap->name, "Veejay Digital Sampler");
			cap->type = VID_TYPE_CAPTURE;
			cap->channels = 1;
			cap->audios = 0;
			cap->maxwidth = v->width;
			cap->maxheight = v->height;
			cap->minwidth = v->width;
			cap->minheight = v->height;
			break;
		}
		case VIDIOCGTUNER:
		{
			veejay_msg(VEEJAY_MSG_INFO, "%s %d",
				__FUNCTION__, __LINE__ );

			struct video_tuner *tuner = arg;
			sprintf( tuner->name, "Veejay Digital Sampler");
			tuner->tuner = 0;
			tuner->rangelow = 0;
			tuner->rangehigh = 0;
			tuner->flags = VIDEO_TUNER_PAL | VIDEO_TUNER_NTSC;
			tuner->mode = (v->norm ? VIDEO_MODE_PAL : VIDEO_MODE_NTSC);
			tuner->signal = 0;
			break;
		}
		case VIDIOCGCHAN:
		{
			struct video_channel *vidchan=arg;
			vidchan->channel = 0;
			vidchan->flags = 0;
			vidchan->tuners = 0;
			vidchan->type = VIDEO_TYPE_CAMERA;
			strcpy(vidchan->name, "Veejay Dummy channel");
			break;
		}
		case VIDIOCGPICT:
		{
			veejay_msg(VEEJAY_MSG_INFO, "%s %d",
				__FUNCTION__, __LINE__ );

			struct video_picture *vidpic=arg;

			vidpic->colour = 0xffff;
			vidpic->hue = 0xffff;
			vidpic->brightness = 0xffff;
			vidpic->contrast = 0xffff;
			vidpic->whiteness = 0xffff;
			
			vidpic->palette = v->palette;
			vidpic->depth = (
					v->palette == VIDEO_PALETTE_YUV420P ?
					12 : 16 );
			break;
		}
		case VIDIOCSPICT:
		{
			veejay_msg(VEEJAY_MSG_INFO, "%s %d",
				__FUNCTION__, __LINE__ );

			struct video_picture *vidpic=arg;
			if(vidpic->palette != v->palette )
				veejay_msg(VEEJAY_MSG_ERROR,
				 "requested palette %d, but only using %d now",
					vidpic->palette, v->palette );
			return 1;
		}

		case VIDIOCCAPTURE:
		{
				veejay_msg(VEEJAY_MSG_INFO, "%s %d",
				__FUNCTION__, __LINE__ );
	
	break;
		}

		case VIDIOCGWIN:
		{
			veejay_msg(VEEJAY_MSG_INFO, "%s %d",
				__FUNCTION__, __LINE__ );

			struct video_window *vidwin=arg;

			vidwin->x=0;
			vidwin->y=0;
			vidwin->width=v->width;
			vidwin->height=v->height;
			vidwin->chromakey=0; 
			vidwin->flags=0;
			vidwin->samplecount=0;
			break;
		}
		case VIDIOCSWIN:
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot change size ! ");
			break;
		}

		case VIDIOCGMBUF:
		{
			veejay_msg(VEEJAY_MSG_INFO, "%s %d",
				__FUNCTION__, __LINE__ );

			struct video_mbuf *vidmbuf=arg;
			int i;
			
			vidmbuf->size = v->size;
			vidmbuf->frames = VLOOPBACK_N_BUFS;

			for (i=0; i < VLOOPBACK_N_BUFS; i++)
				vidmbuf->offsets[i] = i * vidmbuf->size;
			vidmbuf->size *= vidmbuf->frames;
			break;
		}

		case VIDIOCMCAPTURE:
		{
			veejay_msg(VEEJAY_MSG_INFO, "%s %d",
				__FUNCTION__, __LINE__ );

			struct video_mmap *vidmmap=arg;

			if ( vidmmap->format != v->palette )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "capture palette not current palette!");
				return 1;
			}
				
			if (vidmmap->height != v->height ||
			    vidmmap->width != v->width) {
				veejay_msg(VEEJAY_MSG_ERROR, "caputure: invalid size %dx%d\n", vidmmap->width, vidmmap->height );
				return 1;
			}
			break;
		}
		case VIDIOCSYNC:
		{
			veejay_msg(VEEJAY_MSG_INFO, "%s %d",
				__FUNCTION__, __LINE__ );

			struct video_mmap *vidmmap=arg;
			if(!vj_vloopback_write_mmap( vloop, vidmmap->frame ))
				return 1;
			break;
		}
		default:
		{
			veejay_msg(VEEJAY_MSG_INFO, "%s %d",
				__FUNCTION__, __LINE__ );

			veejay_msg(VEEJAY_MSG_ERROR, "ioctl %ld unhandled\n", cmd & 0xff);
			break;
		}


	}
	return 0;
}
*/
void	vj_vloopback_close( void *vloop )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	if(v)
	{
		if(v->fd)
			close( v->fd );
		if(v->out_buf)
			free(v->out_buf);
/*		if(v->out_map)
			munmap( v->out_map,
					v->size * VLOOPBACK_N_BUFS );*/
		free(v);
	}
}	

/*
void	vj_vloopback_signal_handler( void *vloop, int sig_no )
{
	int size,ret;
	unsigned long int cmd;
	struct pollfd ufds;
	char ioctlbuf[1024];
	
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;

	if(sig_no != SIGIO )
		return;

	
	ufds.fd = v->fd;
	ufds.events = POLLIN;
	ufds.revents = 0;
  
	poll( &ufds, 1, 10 ); // 10 ms too small ?
	
	if( !ufds.revents & POLLIN )
	{
		veejay_msg(VEEJAY_MSG_ERROR,
			"Received signal but got negative on poll");
		return;
	}

	size = read( v->fd, ioctlbuf, 1024 );
	if( size >= sizeof( unsigned long int )) 
	{
		veejay_memcpy( &cmd, ioctlbuf, sizeof(unsigned long int));
		if( cmd == 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
				"Client closed device");
			return;
		}
		ret = vj_vloopback_ioctl( vloop, cmd, ioctlbuf + sizeof( unsigned long int ));
		if(ret)
		{
			memset( ioctlbuf + sizeof( unsigned long int ), 1024 - sizeof( unsigned long int ),0xff);
			veejay_msg(VEEJAY_MSG_ERROR,
				"IOCTL %d unsuccessfull", cmd & 0xff);
		}
		ioctl( v->fd, cmd, ioctlbuf + sizeof( unsigned long int ));
	}
	return ;
}
*/
#endif

