/*
 * Linux VeeJay
 * V4l1 driver , classic stuff
 * Copyright(C)2008 Niels Elburg <nwelburg@gmail.com>
 * uses v4lutils - utility library for Video4Linux
 *      Copyright (C) 2001-2002 FUKUCHI Kentaro

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */


#include <config.h>
#ifdef HAVE_V4L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libstream/v4lvideo.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libel/lav_io.h>
#include <libstream/v4lutils.h>
#include <linux/videodev.h>
#include <libvevo/vevo.h>
#include <libvevo/libvevo.h>
#include <libvje/vje.h>
#include <libyuv/yuvconv.h>
#include <veejay/vims.h>
#include <libstream/frequencies.h>
#include <time.h>
#include <string.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <pthread.h>

typedef struct _normlist
{
        char name[10];
        int type;
} normlist;


static normlist normlists[] =
{
	{"ntsc"   , VIDEO_MODE_NTSC},
	{"pal"    , VIDEO_MODE_PAL},
	{"secam"  , VIDEO_MODE_SECAM},
	{"auto"   , VIDEO_MODE_AUTO},
/* following values are supproted by bttv driver. */
	{"pal-nc" , 3},
	{"pal-m"  , 4},
	{"pal-n"  , 5},
	{"ntsc-jp", 6},
	{"", -1}
};


typedef struct {
	int w;
	int h;
	int palette;
	int src_fmt;
	int dst_fmt;		
	int native;
	VJFrame *src;
	VJFrame *dst;
} v4lprocessing;

typedef struct {
	v4ldevice	vd;
	int		video_width;
	int		video_height;
	int		video_area;
	int		video_channel;
	int		video_norm;
	int		video_palette;
	int		has_tuner;
	int		freqtable;
	int		tvchannel;
	char		*video_file;
	int		brightness;
	int		hue;
	int		colour;
	int		contrast;
	int		white;
	void		*converter;
	int		max_width;
	int		max_height;
	int		min_width;
	int		min_height;
	int		composite;
	sws_template	sws_templ;
	void		*scaler;
	v4lprocessing	*info;
	int		native;
	int		jpeglen;
	int		active;
	int		has_video;
	int		dps;
	int		grabbing;
} v4lvideo_t;


typedef struct {
	char    *filename;
	int	width;
	int	height;
	int	palette;
	int	norm;
	int	channel;
	int	frequency;
	int	composite;
	void	*v4l;
	pthread_mutex_t mutex;
	pthread_t	thread;
	pthread_attr_t	attr;
	int	status;
	uint8_t *frame_buffer;
	uint8_t *temp_buffer;
	int	len;
	int	uvlen;
	int	pause;
	int	error;
} v4lvideo_template_t;

static void	__v4lvideo_destroy( void *vv );
static  const char             *get_palette_name( int v4l ); 
static int	__v4lvideo_grabstart( void *vv );
static int	__v4lvideo_grabstop(void *vv);
static void	__v4lvideo_copy_framebuffer_to(v4lvideo_t *v1, v4lvideo_template_t *v2, uint8_t *dstY, uint8_t *dstU, uint8_t *dstV );
int	v4lvideo_grab_check( v4lvideo_t *v, int palette );
static	int	v4lvideo_set_grabformat( v4lvideo_t *v, int palette );
static int	__v4lvideo_init( v4lvideo_t *v, char* file, int channel, int norm, int freq, int dst_w, int dst_h, int dst_palette );

static	void	*device_list_ = NULL;
static	int	device_count_ = 0;
static	int	max_device_   = 0;



void *v4lvideo_init( char* file, int channel, int norm, int freq, int dst_w, int dst_h, int dst_palette )
{
	v4lvideo_template_t *v = (v4lvideo_template_t*) vj_calloc(sizeof(v4lvideo_template_t));
	v->filename = strdup(file);
	v->channel  = channel;
	v->norm     = norm;
	v->frequency= freq;
	v->width    = dst_w;
	v->height   = dst_h;
	v->palette  = dst_palette;
	v->len	    = dst_w * dst_h;
	if( dst_palette == VIDEO_PALETTE_YUV422P ) {
	 	v->uvlen = (dst_w/2) * dst_h;
	} else if (dst_palette == VIDEO_PALETTE_YUV420) {	
		v->uvlen = (dst_w/2) * (dst_h/2);
	} else {
		veejay_msg(0, "Wrong palette: %s", get_palette_name(dst_palette));
		free(v);
		return NULL;
	}
	v->frame_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * 8 * v->width * v->height);
	v->temp_buffer  = v->frame_buffer + (4 * v->width * v->height );
	pthread_mutex_init( &(v->mutex), NULL );

	return v;
}
static void lock_(v4lvideo_template_t *t)
{
        pthread_mutex_lock( &(t->mutex ));
}
static void unlock_(v4lvideo_template_t *t)
{
        pthread_mutex_unlock( &(t->mutex ));
}

int	v4lvideo_templ_get_palette( int p )
{
	switch(p) {
		case FMT_420: case FMT_420F:
			return VIDEO_PALETTE_YUV420P;
		case FMT_422: case FMT_422F:		
			return VIDEO_PALETTE_YUV422P;
		default:
			return -1;
	}
	return -1;
}

char	*v4lvideo_templ_get_norm_str( int id ) {
	int i;
	for( i = 0; normlists[i].type != -1; i ++ ) {
		if( normlists[i].type == id ) 
			return normlists[i].name;
	}
	return "?";
}

int	v4lvideo_templ_get_norm( const char *name )
{
	int i;

	for(i=0; normlists[i].type != -1; i++) {
		if(strcasecmp(name, normlists[i].name) == 0) {
			return normlists[i].type;
		}
	}

	return -1;
}

int	v4lvideo_templ_getfreq( const char *name ) 
{
	int i;

	for(i=0; chanlists[i].name; i++) {
		if(strcmp(name, chanlists[i].name) == 0) {
			return i;
		}
	}

	return -1;
}

int	v4lvideo_templ_num_devices() 
{
	return device_count_;
}
#define VIDEO_PALETTE_JPEG	64 //fun
static struct {
	const int id;
	const char *name;
	const int ff;
} palette_descr_[] =
{	
	{ VIDEO_PALETTE_JPEG,      "JPEG Compressed", PIX_FMT_YUVJ420P },
	{ VIDEO_PALETTE_RGB24 ,    "RGB 24 bit",	PIX_FMT_BGR24 	},
	{ VIDEO_PALETTE_YUV422P ,  "YUV 4:2:2 Planar",	PIX_FMT_YUVJ422P  },
	{ VIDEO_PALETTE_YUV420P ,  "YUV 4:2:0 Planar",  PIX_FMT_YUVJ420P  },
	{ VIDEO_PALETTE_YUYV,      "YUYV 4:2:2 Packed",	PIX_FMT_YUYV422 },
	{ VIDEO_PALETTE_UYVY,	   "UYVY 4:2:2 Packed", PIX_FMT_UYVY422 },
	{ VIDEO_PALETTE_RGB32 ,	   "RGB 32 bit",		PIX_FMT_RGB32 },
	{  VIDEO_PALETTE_PLANAR,   "Planar data",		PIX_FMT_YUVJ422P },
	{ -1,			"Unsupported colour space", -1},
};
//@fixme: 16-235 yuv
//
static int is_YUV(int a) {
	if( a == VIDEO_PALETTE_YUV422P || 
            a == VIDEO_PALETTE_YUV420P ||
	    	a == VIDEO_PALETTE_YUV422  ||
            a == VIDEO_PALETTE_YUYV    ||
            a == VIDEO_PALETTE_UYVY ||
			a == VIDEO_PALETTE_PLANAR
			)
		return 1;
	return 0;
}

static	v4lvideo_t*	v4lvideo_templ_try( int num )
{
	char *refname = (char*) vj_calloc(sizeof(char) * 100);
	v4lvideo_t *v = (v4lvideo_t*) vj_calloc(sizeof(v4lvideo_t));
	if(!v || !refname) {
		if(v) free(v);
		if(refname) free(refname);
		return NULL;
	}
	snprintf(refname,100,"/dev/video%d",num);
	if( v4lopen(refname,&(v->vd)) ) {
		free(v);
		free(refname);
		return NULL;
	}
	v4lgetcapability( &(v->vd));
	free(refname);
	return v;
}

static	int		get_ffmpeg_palette( int v4l ) 
{
	int i = 0;
	while( palette_descr_[i].id != -1 ) {
		if( palette_descr_[i].id == v4l )
			return palette_descr_[i].ff;
		i++;
	}
	return -1;
}

static	const char		*get_palette_name( int v4l ) 
{
	int i = 0;
	while( palette_descr_[i].id != -1 ) {
		if( palette_descr_[i].id == v4l )
			return palette_descr_[i].name;
		i++;
	}
	return NULL;
}

static	int		v4lvideo_fun(v4lvideo_t *v, int w, int h, int palette, int *cap_palette, int *is_jpeg_frame )
{
	int i = 0;
	int supported_palette = -1;
	
	int is_jpeg[2]	      = {0,0};
	while( palette_descr_[i].id != -1 ) {
		if(palette_descr_[i].id == VIDEO_PALETTE_JPEG ) {
			i ++;
			continue;
		}

		if(v4lvideo_grab_check( v, palette_descr_[i].id ) == 0 ) {
#ifdef HAVE_JPEG
			uint8_t *tmp = (uint8_t*) vj_malloc(sizeof(uint8_t) * v->video_width * v->video_height * 4 );
			veejay_msg(VEEJAY_MSG_DEBUG, "Checking if device outputs in JPEG ...");
			__v4lvideo_grabstart(v);
			if(v4lvideo_syncframe(v) == 0 ) {
				uint8_t *src = v4lgetaddress(&(v->vd));
				uint8_t *dst[3] = { tmp , tmp + (v->video_width*v->video_height), 
						    tmp + (2 * v->video_width * v->video_height) };
				unsigned short *ptr = (unsigned short *)( src );
				int count = (ssize_t)((unsigned int)(ptr[0])<<3);
				int len = decode_jpeg_raw( src+2, count,0,420,v->video_width,v->video_height,dst[0],dst[1],dst[2] );
				if(len >= 0 ) {
					is_jpeg[0] = 1;
					is_jpeg[1] = palette_descr_[i].id;	
					veejay_msg(VEEJAY_MSG_DEBUG, "Device outputs in JPEG, but says %s",
										palette_descr_[i].name );
					supported_palette = VIDEO_PALETTE_JPEG;
				} else {
					veejay_msg(VEEJAY_MSG_DEBUG, "%s seems to be okay, using it ...", palette_descr_[i].name );
					supported_palette = palette_descr_[i].id;
				}
			}
			__v4lvideo_grabstop(v);
			free(tmp);
			break;
#else
			veejay_msg(VEEJAY_MSG_ERROR, "No spport for %s (compiled without libjpeg)", palette_descr_[i].name );
#endif
		} else {
			veejay_msg(VEEJAY_MSG_DEBUG, "No support for %s", palette_descr_[i].name );
		}
		i++;
	}
	
	*cap_palette 	   = is_jpeg[1];
	*is_jpeg_frame     = is_jpeg[0];

	return supported_palette;
}

static	v4lprocessing	*v4lvideo_get_processing( v4lvideo_t *v, int w, int h, int palette, int *cap_palette ) 
{
	int i = 0;
	int supported_palette = -1;
	int native            = 0;
	int arr[2] = {0,0};
	int skip = 0;
	int fun = 	v4lvideo_fun(v, w,h,palette, &(arr[0]), &(arr[1]) );
	if( fun < 0 ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Frame grabcheck failed ..." );
	} else {
		if( fun == VIDEO_PALETTE_JPEG ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Grabbing JPEG from video device");
			skip = 1;
			supported_palette = fun;
			*cap_palette = arr[1];
		}
	}

	
	if(!skip) {
		while( palette_descr_[i].id != -1 ) {
			if(v4lvideo_grab_check( v, palette_descr_[i].id ) == 0 ) {
				supported_palette = palette_descr_[i].id;
				veejay_msg(VEEJAY_MSG_DEBUG, "Device supports %s",
					palette_descr_[i].name );
				break;
			} else {
				veejay_msg(VEEJAY_MSG_DEBUG, "No support for %s", palette_descr_[i].name );
			}
			i++;
		}
	}

	if( supported_palette == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Did not find any supported video palette");
		return NULL;
	}

	v4lprocessing *p = (v4lprocessing*) vj_calloc(sizeof(v4lprocessing));
	p->src_fmt = get_ffmpeg_palette( supported_palette );
	p->dst_fmt = get_ffmpeg_palette( palette );

	int max_w = v->vd.capability.maxwidth;
	int min_w = v->vd.capability.minwidth;
	int max_h = v->vd.capability.maxheight;
	int min_h = v->vd.capability.minheight;

	int src_w = w;
	int src_h = h;
	if( src_w > max_w ) src_w = max_w; else if ( src_w < min_w ) src_w = min_w;
	if( src_h > max_h ) src_h = max_h; else if ( src_h < min_h ) src_h = min_h;

	p->w = src_w;
	p->h = src_h;
	
	if( palette == supported_palette && w == p->w && h == p->h) {
		veejay_msg(VEEJAY_MSG_DEBUG, "No color space conversion required for this device.");
		native = 1;
	}

	if( is_YUV( supported_palette )) {
		p->src = yuv_yuv_template( NULL,NULL,NULL,p->w,p->h,p->src_fmt );	
	}
	else {
		if( supported_palette != VIDEO_PALETTE_JPEG ) {
			//p->src = yuv_rgb_template( NULL, p->w,p->h, p->src_fmt );
			char *swaprgb = getenv("VEEJAY_SWAP_RGB");
			if(swaprgb!=NULL) {
				int val = atoi(swaprgb);
				if( val == 1 ) {
				if( p->src_fmt == PIX_FMT_BGR24 ) 
					p->src_fmt = PIX_FMT_RGB24;
				else if ( p->src_fmt == PIX_FMT_RGB24 )
					p->src_fmt = PIX_FMT_BGR24;
				veejay_msg(VEEJAY_MSG_DEBUG, "Swapped RGB format to %s",
						(p->src_fmt==PIX_FMT_RGB24? "RGB" : "BGR" ));
				}
			}	
			else {
				veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_SWAP_RGB=[0|1] not set");
			}
			p->src = yuv_rgb_template( NULL, p->w,p->h, p->src_fmt );
		}
		else {
			native = 2;
			p->src = yuv_yuv_template( NULL, NULL,NULL, p->w, p->h, p->src_fmt );
		}
	}
	p->native = native;

	p->dst = yuv_yuv_template( NULL,NULL,NULL, w, h, p->dst_fmt );

	if(!skip)
		*cap_palette = supported_palette; 

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Capture device info: %dx%d - %dx%d  src=%s,dst=%s, is_YUV=%d, native =%d ",
		min_w,min_h,max_w,max_h, get_palette_name(supported_palette), get_palette_name(palette), is_YUV(supported_palette), native );

	return p;	
}

char	**v4lvideo_templ_get_devices(int *num)
{
	char **items = (char**) vevo_list_properties( device_list_ );
	char filename[200];
	int error = 0;
	int len   = 0;
	int i;
	if( items == NULL )
		return NULL;

	for( i = 0; items[i] != NULL ; i ++ ) {
		error = vevo_property_get( device_list_,items[i], 0, NULL );
		if( error == VEVO_NO_ERROR )
			len ++;
	}

	if( len == 0 ) {
		free(items);
		return NULL;
	}

	char **result = (char**) vj_calloc(sizeof(char*) * (len+1));
	char tmp[1024];
	for( i = 0; items[i] != NULL ; i ++ ) {
		error = vevo_property_get(device_list_,items[i],0,NULL);
		size_t name_len = vevo_property_element_size( device_list_, items[i], 0 );
		char   *name    = (char*) vj_calloc(name_len);
		vevo_property_get( device_list_, items[i], 0, &name );
		snprintf(filename,sizeof(filename),"/dev/video%s",items[i]);
		snprintf(tmp,sizeof(tmp),"%03d%s%03d%s",strlen(name),name,strlen(filename),filename);
		result[i] = strdup(tmp);
	
		free(items[i]);
	}
	free(items);
	*num = device_count_;

	return result;
}

void	v4lvideo_templ_init()
{
	device_list_ = vpn( VEVO_ANONYMOUS_PORT );
	int max = 128;
	int i;
	char key[64];
	for( i = 0; i < max ;i ++ ) {
		v4lvideo_t *v = v4lvideo_templ_try(i);
		if(!v)
			continue;
		char *name = v4lgetdevicename( &(v->vd));
		snprintf(key,sizeof(key), "%d",i);
		int error = vevo_property_set( device_list_, key, VEVO_ATOM_TYPE_STRING, 1,&name );
		veejay_msg(VEEJAY_MSG_DEBUG, "Detected %s as device /dev/video/%d",name,i);
		device_count_ ++;

		if( i > max_device_ )
			max_device_ = i;
		v4lclose( &(v->vd));
		free(v);
	}

}



static int	__v4lvideo_init( v4lvideo_t *v, char *file, int channel, int norm, int freq, int dst_w, int dst_h, int dst_palette )
{
	if(file==NULL)
		return -1;
	
	if ( v4lopen(file, &(v->vd)) ) {
		//veejay_msg(0, "Unable to open capture device '%s' (no such device?)",
		//			file );
		return -1;
	}

	v->video_file = strdup(file);
	v->video_channel = channel;
	v->video_norm	= norm;
	v->video_palette = 0;

	v4lsetdefaultnorm( &(v->vd), norm );
	v4lgetcapability( &(v->vd));

	if( !(v->vd.capability.type & VID_TYPE_CAPTURE )) {
		veejay_msg(0, "This device seems not to support video capturing.");
		return -1;
	}

	if( (v->vd.capability.type & VID_TYPE_TUNER )) {
		v->has_tuner = 1;
		v->freqtable = freq;
		v->tvchannel = 0;
		v4lvideo_setfreq(v,0);
	}

	v->max_width = v->vd.capability.maxwidth;
	v->min_width = v->vd.capability.minwidth;
	v->max_height = v->vd.capability.maxheight;
	v->min_height = v->vd.capability.minheight;

	int w = dst_w;
	int h = dst_h;
	if( w < v->min_width ) w = v->min_width ; else if ( w > v->max_width )
		w = v->max_width;
	if( h < v->min_height ) h = v->min_height; else if ( h > v->max_height )
		h = v->max_height;

	v->video_width = w;
	v->video_height= h;
	v->video_area  = w * h;

	if ( v4lmaxchannel( &(v->vd) ) ) {	
		if( v4lsetchannel( &(v->vd) , channel ) ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Unable to select channel %d", channel );
		//	v4lclose(&(v->vd));
		//	return -1;
		}
	}

	if( v4lmmap( &(v->vd)) ) {
		veejay_msg(0, "mmap interface is not supported by this driver.");
		v4lclose(&(v->vd));
		return -1;
	}
	
	if( v4lgrabinit( &(v->vd), v->video_width,v->video_height ) ) {
		v4lclose(&(v->vd));
		return -1;
	}


	if( v->vd.mbuf.frames < 2 ) {
		v4lclose( &(v->vd));
		return -1;
	}

	/* detect pixel format */	

	v->info       = v4lvideo_get_processing( v, dst_w, dst_h, dst_palette, &(v->video_palette) ); 
	if(v->info == NULL ) {
		veejay_msg(0, "No support for this device.");
		v4lclose(&(v->vd));
		return -1;
	}

	if( v4lvideo_set_grabformat(v,v->video_palette)) {
		v4lclose(&(v->vd));
		veejay_msg(0, "Can't find a supported pixelformat.");
		return -1;
	}

	v4lgetpicture( &(v->vd));

	v->brightness = v->vd.picture.brightness;
	v->hue	      = v->vd.picture.hue;
	v->colour     = v->vd.picture.colour;
        v->contrast   = v->vd.picture.contrast;
	v->white      = v->vd.picture.whiteness;

	veejay_memset( &(v->sws_templ), 0,sizeof(sws_template));
	v->sws_templ.flags = yuv_which_scaler();
	
	v->native     = v->info->native;

	return 1;
}

void	v4lvideo_set_paused(void *vv, int pause) 
{
	v4lvideo_template_t *v = (v4lvideo_template_t*) vv;
	lock_(v);
	v->pause = pause;
	unlock_(v);
}

int	v4lvideo_is_paused(void *vv )
{
	v4lvideo_template_t *v = (v4lvideo_template_t*) vv;
	return v->pause;
}

int	v4lvideo_is_active( void *vv )
{
	v4lvideo_template_t *v = (v4lvideo_template_t*) vv;
	if( v->v4l == NULL )
		return 0;
	return v->status;
}

void	v4lvideo_destroy( void *vv )
{
	v4lvideo_template_t *v = (v4lvideo_template_t*) vv;
	if(v) {
	if(v->frame_buffer )
		free(v->frame_buffer);
	free(v);
	v = NULL;
	}
}

static void	__v4lvideo_destroy( void *vv )
{
	v4lvideo_t *v = (v4lvideo_t*) vv;
	if( v ) {
		v4lmunmap( &(v->vd));
		v4lclose( &(v->vd));
		if( v->scaler ) yuv_free_swscaler( v->scaler );
		if( v->info ) {
			free(v->info->dst);
			free(v->info->src);
			free(v->info);
		}
		free(v->video_file);

		free(v);
		v = NULL;
	}
}

static	int	v4lvideo_set_grabformat( v4lvideo_t *v, int palette )
{
	return v4lsetpalette( &(v->vd), palette );
}

int	v4lvideo_grab_check(v4lvideo_t *v, int palette ) {
	int ret=0;
	v4lseterrorlevel( V4L_PERROR_NONE );
	if( v4lvideo_set_grabformat(v, palette ) ) {	
		ret = -1;
		goto VIDEOEXIT;
	}

	if( v4lgrabstart( &(v->vd),0) < 0 ) {
		ret = -1;	
		goto VIDEOEXIT;
	}
	ret = v4lsync( &(v->vd),0);
	return ret;
VIDEOEXIT:
	v4lseterrorlevel( V4L_PERROR_ALL );
	return ret;
}

static	void	__v4lvideo_draw_error_pattern(v4lvideo_template_t *v, 
				uint8_t *dstY, uint8_t *dstU, uint8_t *dstV)
{
	lock_(v);
	veejay_memset( dstY, 0, v->len );
	veejay_memset( dstU, 128, v->uvlen );
	veejay_memset( dstV, 128, v->uvlen );
	unlock_(v);
}

static	void	*v4lvideo_grabber_thread( void * vv ) 
{
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	v4lvideo_template_t *i = (v4lvideo_template_t*) vv;
	v4lvideo_t *v = (v4lvideo_t*) vj_calloc(sizeof(v4lvideo_t));
	lock_(i);
	i->v4l = (void*) v;
	
	if( __v4lvideo_init( v, i->filename, i->channel, i->norm,i->frequency, i->width, i->height, i->palette ) < 0 ) {
		veejay_msg(0, "Unable to open capture device '%s' in %dx%d - %s",
			i->filename, v->video_width,v->video_height,get_palette_name(v->video_palette) );
		i->error = 1;
		unlock_(i);
		pthread_exit(NULL);
		return NULL;
	}

	unlock_(i);

	veejay_msg(VEEJAY_MSG_INFO, "Capture device looks ready to go!");
	v4lprint( &(v->vd));
	
/*	if( __v4lvideo_grabstart( v ) != 0 ) {
		veejay_msg(0, "Unable to start grabbing from device '%s'", i->filename);
		pthread_exit(NULL);
		return NULL;
	}*/

	uint8_t *dstY = i->frame_buffer;
	uint8_t *dstU = i->frame_buffer + v->info->dst->len;
	uint8_t *dstV = i->frame_buffer + v->info->dst->len + v->info->dst->uv_len;

	v->active = 1;
	int retry = 4;
	int flag  = 0;

	struct timespec req;

	req.sec = 0;
	req.nsec = 20000 * 1000;

	veejay_msg(VEEJAY_MSG_DEBUG, "Capture device thread enters inner loop");
RESTART:
	while( v->active ) {
PAUSED:
		if( i->pause ) {
			lock_(i);
			if(v->grabbing) {
				__v4lvideo_grabstop( v );
			}
			unlock_(i);
			clock_nanosleep( CLOCK_REALTIME,0,&req, NULL);
			goto PAUSED;
		} else {
			if(!v->grabbing) {
				lock_(i);
				veejay_msg(VEEJAY_MSG_DEBUG, "Trying to start capturing.");
				__v4lvideo_grabstart(v);
				unlock_(i);
				clock_nanosleep(CLOCK_REALTIME,0,&req, NULL );
				goto RESTART;
			}
		}
		
	
		if(v->grabbing) {
			flag = v4lvideo_syncframe(v);

			lock_(i);
			if(flag==0) {
				__v4lvideo_copy_framebuffer_to( v,i, dstY, dstU, dstV );
			}
			unlock_(i);

			if(flag == -1) {
				if( retry == 0 ) {
					veejay_msg(0,"Giving up on this device.");
					v->active = 0;
					goto CAPTURE_STOP;
				} else {
					veejay_msg(0,"Error syncing frame from capture device!");
					retry --;
				}
			}

			v4lvideo_grabframe(v);
			goto RESTART;
		}
	
	
/*
		if(!v->grabbing || flag < 0 ) {
			__v4lvideo_draw_error_pattern( i, dstY, dstU,dstV );
		}*/
	}
	if(v->grabbing)
		__v4lvideo_grabstop(v);
CAPTURE_STOP:
	v4lvideo_destroy(v);

	veejay_msg(0, "Stopped grabbing from device '%s'", i->filename);
	i->status = 0;
	pthread_exit(NULL);
	return NULL;
}
int	v4lvideo_grabstop( void *vv )
{
	v4lvideo_template_t *t = (v4lvideo_template_t*) vv;
	lock_( vv );	
	v4lvideo_t *v = (v4lvideo_t*) t->v4l;
	v->active = 0;
	unlock_(vv);
	t->status = 0;
	return 0;
}

int	v4lvideo_grabstart( void *vv )
{
	v4lvideo_template_t *v = (v4lvideo_template_t*) vv;
	pthread_attr_init(&(v->attr) );
	pthread_attr_setdetachstate(&(v->attr), PTHREAD_CREATE_DETACHED );

	int err = pthread_create( &(v->thread), NULL,
			v4lvideo_grabber_thread, v );

	pthread_attr_destroy( &(v->attr) );

	if( err == 0 )
	{
		v->status = 1;
		return 0;
	}
	return -1;
}

static int	__v4lvideo_grabstart( void *vv )
{
	v4lvideo_t *v = (v4lvideo_t*) vv;
	
	v->vd.frame = 0;
	if( v4lgrabstart( &(v->vd), 0 ) < 0 ) {
		veejay_msg(0, "Failed to grab frame 0");	
		return -1;
	}
	if( v4lgrabstart( &(v->vd), 1 ) < 0 ) {
		veejay_msg(0, "Failed to grab frame 1");	
		return -1;
	}
	v->grabbing = 1;
	return 0;
}

static int	__v4lvideo_grabstop( void *vv )
{
	v4lvideo_t *v = (v4lvideo_t*) vv;
	if( v->vd.framestat[v->vd.frame] ) {
		if(v4lsync( &(v->vd), v->vd.frame ) < 0 )	
			return -1;
	}
	if( v->vd.framestat[v->vd.frame ^ 1 ]) {
		if(v4lsync( &(v->vd), v->vd.frame ^ 1 ) < 0 )
			return -1;
	}
	v->grabbing = 0;
	return 0;
}

int	v4lvideo_syncframe(void *vv)
{
	v4lvideo_t *v = (v4lvideo_t*) vv;
	return v4lsyncf( &(v->vd));
}

int	v4lvideo_grabframe( void *vv )
{
	v4lvideo_t *v = (v4lvideo_t*) vv;
	return v4lgrabf( &(v->vd));
}

int	v4lvideo_copy_framebuffer_to( void *vv, uint8_t *dstY, uint8_t *dstU, uint8_t *dstV )
{
	v4lvideo_template_t *v = (v4lvideo_template_t*) vv;
	lock_(v);
	if(!v->status) {	
		unlock_(v);
		return -1;
	}
	
	if(!v->v4l ) {
		unlock_(v);
		return 0;
	}
	v4lvideo_t *v1 = (v4lvideo_t*) v->v4l;
	if( v1->has_video ) {
			uint8_t *src = v->frame_buffer;
			veejay_memcpy( dstY, src, v->len );
			veejay_memcpy( dstU, src+v->len, v->uvlen );
  			veejay_memcpy( dstV, src+v->len + v->uvlen, v->uvlen );
	} else {
		// damage per second: some devices need a *long* time to settle
		if( (v1->dps%25)== 1 && v->error == 0)
			veejay_msg(VEEJAY_MSG_INFO, "Capture device is initializing, just a moment ...");
		v1->dps++;
		unlock_(v);
		return 0;
	}
	unlock_(v);
	return 1;
}

static void	__v4lvideo_copy_framebuffer_to(v4lvideo_t *v1, v4lvideo_template_t *v2, uint8_t *dstY, uint8_t *dstU, uint8_t *dstV )
{
	uint8_t *src  = NULL;
	if( v1->native == 1 ) {
		src = v4lgetaddress(&(v1->vd));
	/*	lock_(v2);
		uint8_t *srcin[3] = { src, src + v2->len, src + v2->len + v2->uvlen };
		uint8_t *dstout[3]= { dstY,dstU,dstV };
		if( yuv_use_auto_ccir_jpeg() ) {
			yuv_scale_pixels_from_ycbcr2( srcin, dstout,v2->len );
		} else {
			veejay_memcpy( dstY, src, v2->len );
			veejay_memcpy( dstU, src + v2->len, v2->uvlen);
			veejay_memcpy( dstV, src + v2->len + v2->uvlen, v2->uvlen );
		}*/
		VJFrame *srcf = v1->info->src;
		VJFrame *dstf = v1->info->dst;
		dstf->data[0] = dstY;
		dstf->data[1] = dstU;
		dstf->data[2] = dstV;
		srcf->data[0] = src;
		srcf->data[1] = src + srcf->len;
		srcf->data[2] = src + srcf->len + srcf->uv_len;


		if(!v1->scaler) 
			v1->scaler = 
				yuv_init_swscaler( srcf,dstf,&(v1->sws_templ), yuv_sws_get_cpu_flags());
		//lock_(v2);
			yuv_convert_and_scale( v1->scaler, srcf, dstf );
				v1->has_video = 1;

		//unlock_(v2);

	} else if ( v1->native == 2 ) {
#ifdef HAVE_JPEG
		src = v4lgetaddress(&(v1->vd));
	//	lock_(v2);
		uint8_t *tmp[3] = {
				v2->temp_buffer,
				v2->temp_buffer + v2->len ,
				v2->temp_buffer + v2->len + v2->len };
		uint8_t *dst[3] = { dstY, dstU, dstV };
		VJFrame *srcf = v1->info->src;
		VJFrame *dstf = v1->info->dst;
		unsigned short *ptr = (unsigned short *)( src );
		int count = (ssize_t)((unsigned int)(ptr[0])<<3);
		int len = decode_jpeg_raw( src+2, count,0,420,srcf->width,srcf->height,tmp[0],tmp[1],tmp[2] ); 
		//420 dat
		if(!v1->scaler) 
			v1->scaler = 
				yuv_init_swscaler( srcf,dstf,&(v1->sws_templ), yuv_sws_get_cpu_flags());

		dstf->data[0] = dstY;
		dstf->data[1] = dstU;
		dstf->data[2] = dstV;
		srcf->data[0] = tmp[0];
		srcf->data[1] = tmp[1];
		srcf->data[2] = tmp[2];
	//	lock_(v2);
			yuv_convert_and_scale( v1->scaler, srcf, dstf );
				v1->has_video = 1;

	//	unlock_(v2);
#endif
	} else {
		VJFrame *srcf = v1->info->src;
		VJFrame *dstf = v1->info->dst;

		dstf->data[0] = dstY;
		dstf->data[1] = dstU;
		dstf->data[2] = dstV;

/*		if( is_YUV( v1->video_palette ) ) {
			src = v4lgetaddress(&(v1->vd));
			srcf->data[0] = src;
			srcf->data[1] = src + srcf->len;
			srcf->data[2] = src + srcf->len + srcf->uv_len;
				
		} else {*/
			srcf->data[0] = v4lgetaddress(&(v1->vd));
			if(!v1->scaler) 
				v1->scaler = 
					yuv_init_swscaler( srcf,dstf,&(v1->sws_templ), yuv_sws_get_cpu_flags());
			
			//lock_(v2);
			yuv_convert_and_scale_from_rgb( v1->scaler, srcf, dstf );
			v1->has_video = 1;

			//unlock_(v2);
	//	}	
	}
//	v1->has_video = 1;
}


int	v4lvideo_setfreq( void *vv, int f )
{
	v4lvideo_t *v = (v4lvideo_t*) vv;
	if(v->has_tuner && (v->freqtable >= 0 ) ) {
		v->tvchannel += f;
		while( v->tvchannel < 0 ) {
			v->tvchannel += chanlists[v->freqtable].count;
		}
		v->tvchannel %= chanlists[v->freqtable].count;

		return v4lsetfreq( &(v->vd), chanlists[v->freqtable].list[v->tvchannel].freq );
	} 
	return 0;
}
#define MAXVAL 65535
int	v4lvideo_get_brightness( void *vvv ) 
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	return v->brightness;
}
int	v4lvideo_get_hue( void *vvv ) 
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	return v->hue;
}
int	v4lvideo_get_colour( void *vvv ) 
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	return v->colour;
}
int	v4lvideo_get_contrast( void *vvv ) 
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	return v->contrast;
}
int	v4lvideo_get_white(void *vvv)
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	return v->white;
}


void	v4lvideo_set_brightness( void *vvv, int x )
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	if( x < 0 ) v = 0; else if ( x > MAXVAL ) x = MAXVAL;

	v->brightness = x;
	v4lsetpicture( &(v->vd), v->brightness,-1,-1,-1,-1);
}
void	v4lvideo_set_hue( void *vvv, int x )
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	if( x < 0 ) x = 0; else if (x > MAXVAL ) x = MAXVAL;

	v->hue = x;
	v4lsetpicture( &(v->vd), -1,v->hue,-1,-1,-1);
}
void	v4lvideo_set_colour( void *vvv, int x )
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	if( x < 0 ) x = 0; else if ( x > MAXVAL ) x = MAXVAL;

	v->colour = x;
	v4lsetpicture( &(v->vd), -1,-1,v->colour,-1,-1);
}
void	v4lvideo_set_contrast( void *vvv, int x )
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	if( x < 0 ) x = 0; else if ( x > MAXVAL ) x = MAXVAL;

	v->contrast = x;
	v4lsetpicture( &(v->vd), -1,-1,-1,v->contrast,-1);
}
void	v4lvideo_set_white( void *vvv, int x )
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	if( x < 0 ) x = 0; else if ( x > MAXVAL ) x = MAXVAL;

	v->contrast = x;
	v4lsetpicture( &(v->vd), -1,-1,-1,-1, x);
}


int	v4lvideo_change_channel( void *vvv, int channel )
{
	v4lvideo_template_t *vv = (v4lvideo_template_t*) vvv;
	v4lvideo_t *v = (v4lvideo_t*) vv->v4l;
	int ret = 0;
	int maxch;

	maxch = v4lmaxchannel( &(v->vd));
	if( channel < 0 ) channel = 0;
	if( channel > maxch ) channel = maxch;

	v4lvideo_grabstop( vv );
	if( v4lsetchannel( &(v->vd), channel ) ) ret = -1;
	v->video_channel = channel;
	v4lvideo_grabstart(vv);
	return ret;
}

void	v4lvideo_set_composite_status( void *vv, int status )
{
	v4lvideo_template_t *v = (v4lvideo_template_t*) vv;
	v->composite = status;
}

int	v4lvideo_get_composite_status( void *vv)
{
	v4lvideo_template_t *v = (v4lvideo_template_t*) vv;
	return v->composite;
}
#endif
