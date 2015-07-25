/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nwelburg@gmail.com>
 *
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
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>
#include <libstream/vj-yuv4mpeg.h>
#include <string.h>
#include <libvjmem/vjmem.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libyuv/yuvconv.h>

static int lock(vj_yuv *t)
{
	return pthread_mutex_lock( &(t->mutex ));
}

static int unlock(vj_yuv *t)
{
	return pthread_mutex_unlock( &(t->mutex ));
}

#define     RUP8(num)(((num)+8)&~8)
vj_yuv *vj_yuv4mpeg_alloc(int w, int h, float fps, int out_pixel_format)
{
    vj_yuv *yuv4mpeg = (vj_yuv *) vj_calloc(sizeof(vj_yuv));
    if(!yuv4mpeg) return NULL;
    yuv4mpeg->sar = y4m_sar_UNKNOWN;
    yuv4mpeg->dar = y4m_dar_4_3;
    y4m_accept_extensions( 1 );
    y4m_init_stream_info(&(yuv4mpeg->streaminfo));
    y4m_init_frame_info(&(yuv4mpeg->frameinfo));
    yuv4mpeg->width = w;
    yuv4mpeg->format = vj_to_pixfmt(out_pixel_format);
    yuv4mpeg->height = h;
    yuv4mpeg->video_fps = fps;
    yuv4mpeg->is_jpeg = vj_is_full_range(out_pixel_format);
    yuv4mpeg->chroma = get_chroma_from_pixfmt( yuv4mpeg->format );
    yuv4mpeg->scaler = NULL;	
    return yuv4mpeg;
}


void vj_yuv4mpeg_free(vj_yuv *v) {
	if(v) {
		if( v->scaler ) {
			yuv_free_swscaler( v->scaler );
			v->scaler = NULL;
		}
		if(v->buf[0] != NULL )
			free(v->buf[0]);
		if(v->src)
			free(v->src);
		if(v->dst)
			free(v->dst);
		free(v);

	}
}

static int vj_yuv_stream_start_read1(vj_yuv * yuv4mpeg, int fd, int width, int height);

int vj_yuv_stream_start_read(vj_yuv * yuv4mpeg, char *filename, int width,
			     int height) {
	int fd = open(filename,O_RDONLY);
	if(!fd) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to open video stream %s\n", filename);
		return -1;
	}
	return vj_yuv_stream_start_read1( yuv4mpeg, fd, width,height );
}

int vj_yuv_stream_start_read_fd( vj_yuv *yuv4mpeg, int fd, int width,int height )
{
	return vj_yuv_stream_start_read1( yuv4mpeg,fd,width,height );
}

static int vj_yuv_stream_start_read1(vj_yuv * yuv4mpeg, int fd, int width, int height)
{
    int i, w, h;
 
    yuv4mpeg->fd = fd;

    i = y4m_read_stream_header(yuv4mpeg->fd, &(yuv4mpeg->streaminfo));
    if (i != Y4M_OK) {
		veejay_msg(VEEJAY_MSG_ERROR, "yuv4mpeg: %s", y4m_strerr(i));
		return -1;
    }

	w = y4m_si_get_width(&(yuv4mpeg->streaminfo));
    h = y4m_si_get_height(&(yuv4mpeg->streaminfo));

    yuv4mpeg->chroma = y4m_si_get_chroma( &(yuv4mpeg->streaminfo) );

    if( yuv4mpeg->chroma != Y4M_CHROMA_422 ) {
		yuv4mpeg->buf[0] = vj_calloc( sizeof(uint8_t) * RUP8( w * h * 4) );
		yuv4mpeg->buf[1] = yuv4mpeg->buf[0] + (w*h);
		yuv4mpeg->buf[2] = yuv4mpeg->buf[1] + (w*h);
		yuv4mpeg->buf[3] = yuv4mpeg->buf[2] + (w*h);
	}

	int plane_count   = y4m_si_get_plane_count( &(yuv4mpeg->streaminfo) );

	veejay_msg(VEEJAY_MSG_DEBUG, "Frame: %dx%d, number of planes: %d, chroma: %s",
					w,h,plane_count, y4m_chroma_keyword( yuv4mpeg->chroma ));

   for( i =0; i < plane_count ; i ++ ) {
	yuv4mpeg->plane_w[i] = y4m_si_get_plane_width( &(yuv4mpeg->streaminfo), i);
	yuv4mpeg->plane_h[i] = y4m_si_get_plane_height( &(yuv4mpeg->streaminfo), i);
	yuv4mpeg->plane_size[i] = y4m_si_get_plane_length( &(yuv4mpeg->streaminfo), i );
	veejay_msg(VEEJAY_MSG_DEBUG,
					"\tPlane %d: %dx%d, length=%d",
					i,
					yuv4mpeg->plane_w[i],
					yuv4mpeg->plane_h[i],
					yuv4mpeg->plane_size[i] );
    }
   


   if( w != width || h != height )
   {
    	veejay_msg(VEEJAY_MSG_ERROR,
	       "Video dimensions: %d x %d must match %d x %d. Stream cannot be opened", w, h,
	       width, height);
		return -1;
    }

    return 0;
}



/*
uint8_t *vj_yuv_get_buf( void *v ) {
	vj_yuv *y = (vj_yuv*)v;
	return y->buf;
}*/

int vj_yuv_stream_write_header(vj_yuv * yuv4mpeg, VJFrame *frame, int out_chroma)
{
    int i = 0;
    y4m_si_set_width(&(yuv4mpeg->streaminfo), yuv4mpeg->width);
    y4m_si_set_height(&(yuv4mpeg->streaminfo), yuv4mpeg->height);
    y4m_si_set_interlace(&(yuv4mpeg->streaminfo), Y4M_ILACE_NONE);
    y4m_si_set_chroma( &(yuv4mpeg->streaminfo), out_chroma );
    y4m_si_set_framerate(&(yuv4mpeg->streaminfo), mpeg_conform_framerate(frame->fps));

    yuv4mpeg->chroma = out_chroma;
    yuv4mpeg->buf[0] = vj_calloc(sizeof(uint8_t) * yuv4mpeg->width * yuv4mpeg->height * 4 );
    yuv4mpeg->buf[1] = yuv4mpeg->buf[0] + yuv4mpeg->width * yuv4mpeg->height;
    yuv4mpeg->buf[2] = yuv4mpeg->buf[1] + yuv4mpeg->width * yuv4mpeg->height;
    yuv4mpeg->buf[3] = yuv4mpeg->buf[2] + yuv4mpeg->width * yuv4mpeg->height;
    y4m_ratio_t dar2 = y4m_guess_sar(yuv4mpeg->width,
					 yuv4mpeg->height,
					 yuv4mpeg->dar);
    y4m_si_set_sampleaspect(&(yuv4mpeg->streaminfo), dar2);

    i = y4m_write_stream_header(yuv4mpeg->fd, &(yuv4mpeg->streaminfo));

    y4m_log_stream_info(2, "yuv4mpeg: ", &(yuv4mpeg->streaminfo));
    if (i != Y4M_OK)
		return -1;

    sws_template sws_tem;
    veejay_memset(&sws_tem, 0,sizeof(sws_template));
    sws_tem.flags = yuv_which_scaler();
    
    yuv4mpeg->dst = yuv_yuv_template( NULL,NULL,NULL, frame->width,frame->height, get_pixfmt_from_chroma(out_chroma));
	yuv4mpeg->src = yuv_yuv_template( NULL,NULL,NULL, yuv4mpeg->width,yuv4mpeg->height, yuv4mpeg->format );

    yuv4mpeg->scaler = yuv_init_swscaler( frame, yuv4mpeg->dst, &sws_tem, yuv_sws_get_cpu_flags());
   
    yuv4mpeg->dst->data[0] = yuv4mpeg->buf[0];
    yuv4mpeg->dst->data[1] = yuv4mpeg->buf[1];
    yuv4mpeg->dst->data[2] = yuv4mpeg->buf[2];

    return 0;
}

int vj_yuv_stream_open_pipe(vj_yuv *yuv4mpeg, char *filename)
{
	yuv4mpeg->fd = open(filename,O_WRONLY,0600);
        if(!yuv4mpeg->fd) return 0;
	return 1;
}
int vj_yuv_stream_header_pipe(vj_yuv *yuv4mpeg,VJFrame *frame)
{	
    vj_yuv_stream_write_header(yuv4mpeg, frame, get_chroma_from_pixfmt(frame->format) );

    return 1;	
}

void	*y4m_writer_thread( void *data )
{
	vj_yuv *y4m = (vj_yuv*)data;

	lock(y4m);
	y4m->state = 1;
	unlock(y4m);

	for( ;; ) {

		lock(y4m);
		if( y4m->state == 0 ) {
			unlock(y4m);
			goto Y4MTHREADEXIT;
		}
		pthread_cond_wait( &(y4m->task), &(y4m->mutex));
		unlock(y4m);

		int error = y4m_write_frame(
				y4m->fd,
				&(y4m->streaminfo),
				&(y4m->frameinfo),
				y4m->dst->data);

		if (error != Y4M_OK) {
			veejay_msg(VEEJAY_MSG_ERROR, "yuv4mpeg: %s", y4m_strerr(error));
			goto Y4MTHREADEXIT;
		}
		
		if( y4m->state == 0 ) {
			goto Y4MTHREADEXIT;
		}
	}

Y4MTHREADEXIT:
	veejay_msg(VEEJAY_MSG_INFO,"Exiting Y4M writer thread" );
	return NULL;
}

int vj_yuv_stream_start_write(vj_yuv * yuv4mpeg,VJFrame *frame, char *filename, int outchroma)
{
    //if(mkfifo( filename, 0600)!=0) return -1;
    /* if the file exists gamble and simply append,
       if it does not exist write header 

     */
    struct stat sstat;
 
    if(strncasecmp( filename, "stdout", 6) == 0)
    {
		yuv4mpeg->fd = 1;
    }  
    else 
    {
       if(strncasecmp(filename, "stderr", 6) == 0)
	{
		yuv4mpeg->fd = 2;
	}
        else
	{
	    if (stat(filename, &sstat) == 0)
	    {
		if (S_ISREG(sstat.st_mode))
		{
		    	/* the file is a regular file */
		    	yuv4mpeg->fd = open(filename, O_APPEND | O_WRONLY, 0600);
		  	if (!yuv4mpeg->fd)
				return -1;
		}
	    	else
	    	{
			if (S_ISFIFO(sstat.st_mode))
			  veejay_msg(VEEJAY_MSG_INFO, "Waiting for a program to open %s", filename);
		       	yuv4mpeg->fd = open(filename,O_WRONLY,0600);
   			     if(!yuv4mpeg->fd) return 0;

		}
	    }
	    else
	    {
		veejay_msg(VEEJAY_MSG_INFO, "Creating YUV4MPEG regular file '%s'",
			   filename);
		yuv4mpeg->fd = open(filename, O_CREAT | O_WRONLY, 0600);
  		if (!yuv4mpeg->fd)
 			return -1;
   	     }
	}

    }

    if( vj_yuv_stream_write_header(yuv4mpeg, frame, outchroma) < 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error while writing y4m header.");
		return -1;
    }

    yuv4mpeg->has_audio = 0;

	pthread_mutex_init( &(yuv4mpeg->mutex), NULL );

	int p_err = pthread_create( &(yuv4mpeg->thread), NULL, y4m_writer_thread, (void*) yuv4mpeg );
	if( p_err == 0 ) {
		veejay_msg(VEEJAY_MSG_INFO, "Created new Y4M writer thread" );
	}


    return 0;
}

void vj_yuv_stream_stop_write(vj_yuv * yuv4mpeg)
{
    y4m_fini_stream_info(&(yuv4mpeg->streaminfo));
    y4m_fini_frame_info(&(yuv4mpeg->frameinfo));
    close(yuv4mpeg->fd);

    if( yuv4mpeg->scaler ) {
		yuv_free_swscaler( yuv4mpeg->scaler );
		yuv4mpeg->scaler = NULL;
    }

	lock( yuv4mpeg );
	if( yuv4mpeg->state == 0 ) {
		unlock(yuv4mpeg);
		return;
	}

	yuv4mpeg->state = 0;
	unlock(yuv4mpeg);

	pthread_mutex_destroy(&(yuv4mpeg->mutex));
	pthread_cond_destroy(&(yuv4mpeg->task));
}

void vj_yuv_stream_stop_read(vj_yuv * yuv4mpeg)
{
    y4m_fini_stream_info(&(yuv4mpeg->streaminfo));
    y4m_fini_frame_info(&(yuv4mpeg->frameinfo));
    close(yuv4mpeg->fd);
    yuv4mpeg->sar = y4m_sar_UNKNOWN;
    yuv4mpeg->dar = y4m_dar_4_3;
	
	if( yuv4mpeg->scaler ) {
		yuv_free_swscaler( yuv4mpeg->scaler );
		yuv4mpeg->scaler = NULL;
	}
	if( yuv4mpeg->buf[0] ) {
		free(yuv4mpeg->buf[0]);
		yuv4mpeg->buf[0] = NULL;
	}

}

static	int	vj_yuv_restart(vj_yuv *yuv4mpeg )
{
	y4m_stream_info_t dummy;
	if( lseek( yuv4mpeg->fd , 0, SEEK_SET ) < 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error seeking to start of y4m stream.");
		return -1;
	}

	//@ read out header
	int i = y4m_read_stream_header(yuv4mpeg->fd, &dummy);
    	if (i != Y4M_OK) {
		veejay_msg(VEEJAY_MSG_ERROR, "yuv4mpeg: %s", y4m_strerr(i));
		return -1;
    	}

	return 0;
}

int vj_yuv_get_frame(vj_yuv * yuv4mpeg, uint8_t *dst[3])
{
    int i;
// playing in CCIR	
	if( yuv4mpeg->chroma == Y4M_CHROMA_422 && yuv4mpeg->is_jpeg == 0 ) {
		 i = y4m_read_frame(yuv4mpeg->fd, &(yuv4mpeg->streaminfo),&(yuv4mpeg->frameinfo), dst);
  		 if (i != Y4M_OK)
    	 	 {
			if( i == Y4M_ERR_EOF ) {
				if( vj_yuv_restart( yuv4mpeg ) == 0 ) 
				{
					i = y4m_read_frame(yuv4mpeg->fd, &(yuv4mpeg->streaminfo),&(yuv4mpeg->frameinfo), dst);
					if ( i == Y4M_OK )
						return 0;
				}	
			}
			veejay_msg(VEEJAY_MSG_ERROR, "yuv4mpeg %s", y4m_strerr(i));
			return -1;
		 }
		 return 0;
// in JPEG
	} else if ( yuv4mpeg->chroma == Y4M_CHROMA_422 && yuv4mpeg->is_jpeg == 1 ) {
  		i = y4m_read_frame(yuv4mpeg->fd, &(yuv4mpeg->streaminfo),&(yuv4mpeg->frameinfo), dst);
  	  	if (i != Y4M_OK)
    		{
			if( i == Y4M_ERR_EOF ) {
				if( vj_yuv_restart( yuv4mpeg ) == 0 ) {
					i = y4m_read_frame( yuv4mpeg->fd,&(yuv4mpeg->streaminfo),&(yuv4mpeg->frameinfo),dst );
					if( i == Y4M_OK ) {
						yuv_scale_pixels_from_ycbcr( dst[0], 16.0f, 235.0f, yuv4mpeg->width * yuv4mpeg->height );
						yuv_scale_pixels_from_ycbcr( dst[1], 16.0f, 240.0f, (yuv4mpeg->width * yuv4mpeg->height) / 2 );
						return 0;

					}
				}
			}
			veejay_msg(VEEJAY_MSG_ERROR, "yuv4mpeg %s", y4m_strerr(i));
			return -1;
		}
		yuv_scale_pixels_from_ycbcr( dst[0], 16.0f, 235.0f, yuv4mpeg->width * yuv4mpeg->height );
		yuv_scale_pixels_from_ycbcr( dst[1], 16.0f, 240.0f, (yuv4mpeg->width * yuv4mpeg->height) / 2 );
		return 0;
	}
// not 422
    	if( yuv4mpeg->chroma != Y4M_CHROMA_422 ) {
	  	i = y4m_read_frame(yuv4mpeg->fd, &(yuv4mpeg->streaminfo),&(yuv4mpeg->frameinfo), yuv4mpeg->buf);
  	  	if( i != Y4M_OK ) {
			if( i == Y4M_ERR_EOF ) {
				if( vj_yuv_restart( yuv4mpeg ) == 0 ) {
					i = y4m_read_frame(yuv4mpeg->fd,&(yuv4mpeg->streaminfo),&(yuv4mpeg->frameinfo),yuv4mpeg->buf);
					if( i != Y4M_OK ) {
						veejay_msg(VEEJAY_MSG_ERROR, "yuv4mpeg %s", y4m_strerr(i));
						return -1;
					}
				}
			}
			else {
				veejay_msg(VEEJAY_MSG_ERROR, "yuv4mpeg %s", y4m_strerr(i));
				return -1;
			}
		}

		int src_fmt = get_pixfmt_from_chroma( yuv4mpeg->chroma );
		
		VJFrame *srcf = yuv_yuv_template( yuv4mpeg->buf[0], yuv4mpeg->buf[1], yuv4mpeg->buf[2],yuv4mpeg->width,yuv4mpeg->height, src_fmt );

		VJFrame *dstf = yuv_yuv_template( dst[0],dst[1],dst[2], yuv4mpeg->width,yuv4mpeg->height, yuv4mpeg->format );

		if(!yuv4mpeg->scaler) {
				sws_template sws_tem;
				memset(&sws_tem, 0,sizeof(sws_template));
				sws_tem.flags = yuv_which_scaler();
				yuv4mpeg->scaler = yuv_init_swscaler( srcf,dstf, &sws_tem, yuv_sws_get_cpu_flags());
				if(!yuv4mpeg->scaler) {
						free(srcf); free(dstf); 
						return -1;
				}
		}

		yuv_convert_and_scale( yuv4mpeg->scaler, srcf, dstf );
	
		free(srcf);
		free(dstf);

		return 0;
	}

    return -1;
}


int vj_yuv_put_frame(vj_yuv * vjyuv, uint8_t **src)
{
   	if (!vjyuv->fd) {
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid file descriptor for y4m stream");
		return -1;
	}

	VJFrame *f = vjyuv->src;
	f->data[0] = src[0];
	f->data[1] = src[1];
	f->data[2] = src[2];

	yuv_convert_and_scale( vjyuv->scaler, f, vjyuv->dst );
	
	lock(vjyuv);
	pthread_cond_signal( &(vjyuv->task) );
	unlock(vjyuv);
	
 	return 0;
}
