/*
 * Copyright (C) 2002 Niels Elburg <elburg@hio.hen.nl>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */


#include <config.h>
#include "vj-ffmpeg.h"
#include "editlist.h"
#include "vj-common.h"



	/* allocate a pointer to the newly allocated ffmpeg object */
vj_ffmpeg *vj_ffmpeg_alloc()
{
    vj_ffmpeg *vf = (vj_ffmpeg *) malloc(sizeof(vj_ffmpeg));
    //vf->c = NULL;
    return vf;
}


/* initialize the decoder or encoder, given editlist and allocated ffmpeg object */
int vj_ffmpeg_init(vj_ffmpeg * ffmpeg, EditList * el, int mode, int palette)
{
    char palette_name[20];

    switch(mode)
    {
    	case FFMPEG_DECODE_MJPEG: 
		ffmpeg->codec = avcodec_find_decoder( CODEC_ID_MJPEG );
		break;
	case FFMPEG_ENCODE_MJPEG:
		ffmpeg->codec = avcodec_find_encoder( CODEC_ID_MJPEG );
		break;
	case FFMPEG_DECODE_DIVX:
		ffmpeg->codec = avcodec_find_decoder( CODEC_ID_MSMPEG4V3 );
		break;
	case FFMPEG_ENCODE_DIVX:
		ffmpeg->codec = avcodec_find_encoder( CODEC_ID_MSMPEG4V3 );
		break;
	case FFMPEG_DECODE_MPEG4:
		ffmpeg->codec = avcodec_find_decoder( CODEC_ID_MPEG4 );
		break;
	case FFMPEG_ENCODE_MPEG4:
		ffmpeg->codec = avcodec_find_encoder( CODEC_ID_MPEG4 );
		break;

	case FFMPEG_DECODE_DV:
		ffmpeg->codec = avcodec_find_decoder( CODEC_ID_DVVIDEO );
		break;
	case FFMPEG_ENCODE_DV:
		ffmpeg->codec = avcodec_find_encoder( CODEC_ID_DVVIDEO );
		break;
	default:
		veejay_msg(VEEJAY_MSG_INFO, "Unsupported codec");
		return -1;
    }

    switch(palette)
	{
	  case PIX_FMT_YUV420P:sprintf(palette_name,"yuv420p");break;
	  case PIX_FMT_YUV422P:sprintf(palette_name,"yuv422p");break;
	  case PIX_FMT_YUV444P:sprintf(palette_name,"yuv444p (untested)");break;
	  default:
		sprintf(palette_name, "(invalid)"); break;
	}


    ffmpeg->c = avcodec_alloc_context();
    ffmpeg->picture = avcodec_alloc_frame();
    ffmpeg->c->width = el->video_width;
    ffmpeg->c->height = el->video_height;
    ffmpeg->c->frame_rate = el->video_fps;
    ffmpeg->c->pix_fmt = palette;
    ffmpeg->encode_fmt = palette;
    if((mode % 2)== 1) {
	ffmpeg->c->qcompress = 0.0;
	ffmpeg->c->qblur = 0.0;
	ffmpeg->c->flags = CODEC_FLAG_QSCALE;
	ffmpeg->c->gop_size = 1;
	ffmpeg->c->sub_id = 1;
	ffmpeg->c->workaround_bugs = FF_BUG_AUTODETECT;
	ffmpeg->c->dct_algo = FF_DCT_AUTO;
	}
    ffmpeg->mode = mode;
    return 0;
}


int vj_ffmpeg_open_codec(vj_ffmpeg * ffmpeg)
{
    if (!ffmpeg)
	fprintf(stderr, "codec invalied\n");
    if (!ffmpeg->codec)
	fprintf(stderr, "cannot initialize codec (%d)\n",ffmpeg->mode);
    if( !ffmpeg->c)
	fprintf(stderr, "initialization wrong (%d)\n",
	 ffmpeg->mode );
    if (avcodec_open(ffmpeg->c, ffmpeg->codec) < 0) {
	return -1;
    }
    return 1;
}

int vj_ffmpeg_deinterlace(vj_ffmpeg *ffmpeg, uint8_t *Y, uint8_t *Cb, uint8_t *Cr, int w, int h ) {
	return 0;
}

int vj_ffmpeg_decode_frame(vj_ffmpeg * ffmpeg, uint8_t * buff, int buf_len,
			   uint8_t * Y, uint8_t * Cb, uint8_t * Cr)
{
    int ptr = 0;
    int len = 0;
    if ( buf_len <= 1) 
	{
	 veejay_msg(VEEJAY_MSG_INFO, "Corrupt frame (inserting black)", buff,buf_len,
		sizeof(buff));
	 //return -1;
	 memset( Y, 16,ffmpeg->c->width * ffmpeg->c->height ); 
	 memset( Cb, 128, (ffmpeg->c->width * ffmpeg->c->height)/4);
	 memset( Cr, 128, (ffmpeg->c->width * ffmpeg->c->height)/4);
	 return 0;
	}
    //if(buf_len < 2 || buf_len > (10*65535)) return -1;
    len = avcodec_decode_video(ffmpeg->c, ffmpeg->picture, &ptr, buff,
			       buf_len);

    if(len > 0) {
	AVPicture pict;
	int dst_pix_fmt = PIX_FMT_YUV420P;
	pict.data[0] = Y;
	pict.data[1] = Cb;
	pict.data[2] = Cr;
	pict.linesize[0] = ffmpeg->c->width;
	pict.linesize[1] = ffmpeg->c->width >>1;
	pict.linesize[2] = ffmpeg->c->width >>1;
	
	img_convert( &pict, dst_pix_fmt,(const AVPicture*) ffmpeg->picture, ffmpeg->c->pix_fmt,
		ffmpeg->c->width, ffmpeg->c->height);

	return len;
	

    }

    return -1;
}


int vj_ffmpeg_encode_frame(vj_ffmpeg * ffmpeg, uint8_t * yuv_420_frame[3],
			   uint8_t * buf, int buf_len)
{
	int res = 0;
	//int size;
	if (!ffmpeg->picture) {
		return -1;
	}

	/* frame is in YUV 4:2:0 Planar */
	ffmpeg->picture->data[0] = yuv_420_frame[0];
	ffmpeg->picture->data[1] = yuv_420_frame[1];
	ffmpeg->picture->data[2] = yuv_420_frame[2];
	ffmpeg->picture->linesize[0] = ffmpeg->c->width;
	ffmpeg->picture->linesize[1] = ffmpeg->c->width >> 1;
 	ffmpeg->picture->linesize[2] = ffmpeg->c->width >> 1;

 
    	res = avcodec_encode_video(ffmpeg->c, buf, buf_len, ffmpeg->picture);
	

    	return res;
}

void vj_ffmpeg_close(vj_ffmpeg * ffmpeg)
{
    avcodec_close(ffmpeg->c);
    avcodec_default_free_buffers( ffmpeg->c );
    free(ffmpeg);

}
