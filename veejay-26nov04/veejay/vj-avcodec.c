#include <config.h>
#include <veejay/vj-common.h>
#include <veejay/vj-avcodec.h>
#include <veejay/vj-el.h>

#define __FALLBACK_LIBDV 1

#ifdef __FALLBACK_LIBDV
#include <veejay/vj-dv.h>
#endif

void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;

static int out_pixel_format = FMT_420; 

static vj_encoder *_encoders[NUM_ENCODERS];

static vj_encoder	*vj_avcodec_new_encoder( int id, editlist *el, int pixel_format)
{
	vj_encoder *e = (vj_encoder*) vj_malloc(sizeof(vj_encoder));
	if(!e) exit(0);
	memset(e, 0, sizeof(vj_encoder));
		
	if(id != -1)
	{
#ifdef __FALLBACK_LIBDV
		if(id != CODEC_ID_DVVIDEO)
#endif
			e->codec = avcodec_find_encoder( id );
	}
	e->context = avcodec_alloc_context();
	e->context->width = el->video_width;
 	e->context->height = el->video_height;
	e->context->frame_rate = el->video_fps;
	e->context->frame_rate_base = 1;
	e->context->qcompress = 0.0;
	e->context->qblur = 0.0;
	e->context->flags = CODEC_FLAG_QSCALE;
	e->context->gop_size = 0;
	e->context->sub_id = 0;
	e->context->me_method = 0; // motion estimation algorithm
	e->context->workaround_bugs = FF_BUG_AUTODETECT;
	e->context->prediction_method = 0;
	e->context->dct_algo = FF_DCT_AUTO; //global_quality?


	if ( pixel_format == FMT_422)
	{
		e->context->pix_fmt = PIX_FMT_YUV422P;
		e->len = el->video_width * el->video_height;
		e->uv_len = e->len / 2;
		e->width = el->video_width;
		e->height = el->video_height;
	}
	if( pixel_format == FMT_420)
	{
		e->len = el->video_width * el->video_height;
		e->uv_len = e->len / 4;
		if(out_pixel_format == FMT_422)
			e->sub_sample =1;
		e->width = el->video_width;
		e->height = el->video_height;
		e->context->pix_fmt = PIX_FMT_YUV420P;
	}

	veejay_msg(VEEJAY_MSG_WARNING, "ffmpeg using %s",
		( pixel_format == FMT_422 ? "422"  :"420" ) );

	if( id != -1)
	{
#ifdef __FALLBACK_LIBDV
	  if(id != CODEC_ID_DVVIDEO )
#endif
		if ( avcodec_open( e->context, e->codec ) < 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error initializing codec %d",id); 
			if(e) free(e);
			return NULL;
		}
	} 
	//veejay_msg(VEEJAY_MSG_DEBUG, "Initialized encoder %d", id);
/*
	if( el->has_audio )
	{
		e->audiocodec = avcodec_find_encoder( CODEC_ID_PCM_U8 );
		if(!e->audiocodec)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error initializing audio codec");
			if(e) free(e);
		}
		e->context->sample_rate = el->audio_rate;
		e->context->channels	= el->audio_chans;
		if( avcodec_open( e->context, e->audiocodec ) < 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot open audio context");
			if(e) free(e);
			return NULL;
		}

	}
*/
	return e;
}

static	void		vj_avcodec_close_encoder( vj_encoder *av )
{
	if(av)
	{
		if(av->context)	avcodec_close( av->context );
		free(av);
	}
}


int		vj_avcodec_init(editlist *el, int pixel_format)
{
	if(!el) return 0;
	int fmt = PIX_FMT_YUV420P;

	out_pixel_format = pixel_format;

	if(out_pixel_format == FMT_422) fmt = PIX_FMT_YUV422P;

	_encoders[ENCODER_MJPEG] = vj_avcodec_new_encoder( CODEC_ID_MJPEG, el, FMT_420);
	if(!_encoders[ENCODER_MJPEG]) return 0;

#ifndef __FALLBACK_LIBDV
	_encoders[ENCODER_DVVIDEO] = vj_avcodec_new_encoder( CODEC_ID_DVVIDEO, el, out_pixel_format );
	if(!_encoders[ENCODER_DVVIDEO]) return 0;
#else
#ifdef SUPPORT_READ_DV2
	veejay_msg(VEEJAY_MSG_DEBUG, "Using libdv for encoding video");
	vj_dv_init_encoder( el , out_pixel_format);
#endif
#endif
	_encoders[ENCODER_DIVX] = vj_avcodec_new_encoder( CODEC_ID_MSMPEG4V3 , el, out_pixel_format);
	if(!_encoders[ENCODER_DIVX]) return 0;

	_encoders[ENCODER_MPEG4] = vj_avcodec_new_encoder( CODEC_ID_MPEG4, el, out_pixel_format);

	if(!_encoders[ENCODER_MPEG4]) return 0;

	_encoders[ENCODER_YUV420] = vj_avcodec_new_encoder( -1, el, FMT_420);
	if(!_encoders[ENCODER_YUV420]) return 0;

	_encoders[ENCODER_YUV422] = vj_avcodec_new_encoder( -1, el, FMT_422);
	if(!_encoders[ENCODER_YUV422]) return 0;


	return 1;
}

int		vj_avcodec_free()
{
	int i;
	for( i = 0; i < NUM_ENCODERS; i++)
	{
		if(_encoders[i]) vj_avcodec_close_encoder(_encoders[i]);
	}
#ifdef __FALLBACK_LIBDV
#ifdef SUPPORT_READ_DV2
	vj_dv_free_encoder();
#endif
#endif
	return 1;
}

/* this routine is the same as frame_YUV422_to_YUV420P , unpack
 * libdv's 4:2:2-packed into 4:2:0 planar 
 * See http://mjpeg.sourceforge.net/ (MJPEG Tools) (lav-common.c)
 */
void yuy2toyv12(uint8_t * _y, uint8_t * _u, uint8_t * _v, uint8_t * input,
		int width, int height)
{

    int i, j, w2;
    uint8_t *y, *u, *v;

    w2 = width / 2;

    //I420
    y = _y;
    v = _v;
    u = _u;

    for (i = 0; i < height; i += 4) {
	/* top field scanline */
	for (j = 0; j < w2; j++) {
	    /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
	    *(y++) = *(input++);
	    *(u++) = *(input++);
	    *(y++) = *(input++);
	    *(v++) = *(input++);
	}
	for (j = 0; j < w2; j++)
	{
	    *(y++) = *(input++);
	    *(u++) = *(input++);
	    *(y++) = *(input++);
	    *(v++) = *(input++);
	
	}

	/* next two scanlines, one frome each field , interleaved */
	for (j = 0; j < w2; j++) {
	    /* skip every second line for U and V */
	    *(y++) = *(input++);
	    input++;
	    *(y++) = *(input++);
	    input++;
	}
	/* bottom field scanline*/
	for (j = 0; j < w2; j++) {
	    /* skip every second line for U and V */
	    *(y++) = *(input++);
	    input++;
	    *(y++) = *(input++);
	    input++;
	}

    }
}
/* convert 4:2:0 to yuv 4:2:2 packed */
void yuv422p_to_yuv422(uint8_t * yuv420[3], uint8_t * dest, int width,
		       int height)
{
    unsigned int x, y;


    for (y = 0; y < height; ++y) {
	uint8_t *Y = yuv420[0] + y * width;
	uint8_t *Cb = yuv420[1] + (y / 2) * (width);
	uint8_t *Cr = yuv420[2] + (y / 2) * (width);
	for (x = 0; x < width; x +=2) {
	    *(dest + 0) = Y[0];
	    *(dest + 1) = Cb[0];
	    *(dest + 2) = Y[1];
	    *(dest + 3) = Cr[0];
	    dest += 4;
	    Y += 2;
	    ++Cb;
	    ++Cr;
	}
    }
}



/* convert 4:2:0 to yuv 4:2:2 */
void yuv420p_to_yuv422(uint8_t * yuv420[3], uint8_t * dest, int width,
		       int height)
{
    unsigned int x, y;


    for (y = 0; y < height; ++y) {
	uint8_t *Y = yuv420[0] + y * width;
	uint8_t *Cb = yuv420[1] + (y >> 1) * (width >> 1);
	uint8_t *Cr = yuv420[2] + (y >> 1) * (width >> 1);
	for (x = 0; x < width; x += 2) {
	    *(dest + 0) = Y[0];
	    *(dest + 1) = Cb[0];
	    *(dest + 2) = Y[1];
	    *(dest + 3) = Cr[0];
	    dest += 4;
	    Y += 2;
	    ++Cb;
	    ++Cr;
	}
    }
}

/* convert yuv422 planar to YUYV */
void yuv422_to_yuyv(uint8_t *yuv422[3], uint8_t *pixels, int w, int h)
{
    int x,y;
    uint8_t *Y = yuv422[0];
    uint8_t *U = yuv422[1];
    uint8_t *V = yuv422[2]; // U Y V Y
	for(y = 0; y < h; y ++ )
	{
		Y = yuv422[0] + y * w;
		U = yuv422[1] + (y>>1) * w;
		V = yuv422[2] + (y>>1) * w;
	/*	for(x=0; x < w; x+= 2)
		{
			*(pixels + 0) = Y[0];
			*(pixels + 1) = U[0];
			*(pixels + 2) = Y[1];
			*(pixels + 3) = V[0];
			pixels += 4;
			Y+=2;
			++U;
			++V;
		}
	*/
		for( x = 0 ; x < w ; x += 4 )
		{
			*(pixels + 0) = Y[0];
			*(pixels + 1) = U[0];
			*(pixels + 2) = Y[1];
			*(pixels + 3) = V[0];
			*(pixels + 4) = Y[2];
			*(pixels + 5) = U[1];
			*(pixels + 6) = Y[3];
			*(pixels + 7) = V[1];
			pixels += 8;
			Y+=4;
			U+=2;
			V+=2;
		}
    }
}

void	yuv422p_to_yuv420p2( uint8_t *src[3], uint8_t *dst[3], int w, int h)
{
	int len = w* h ;
	int uv_len = len / 4;
	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
	memset(&pict2,0,sizeof(pict2));

	pict1.data[0] = src[0];
	pict1.data[1] = src[1];
	pict1.data[2] = src[2];
	pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;
	pict2.data[0] = dst[0];
	pict2.data[1] = dst[1];
	pict2.data[2] = dst[2];
	pict2.linesize[0] = w;
	pict2.linesize[1] = w >> 1;
	pict2.linesize[2] = w >> 1;	

	img_convert( &pict2, PIX_FMT_YUV420P, &pict1, PIX_FMT_YUV422P, w, h );
	return;
}


int	yuv422p_to_yuv420p( uint8_t *src[3], uint8_t *dst, int w, int h)
{
	int len = w* h ;
	int uv_len = len / 4;
	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
	memset(&pict2,0,sizeof(pict2));

	pict1.data[0] = src[0];
	pict1.data[1] = src[1];
	pict1.data[2] = src[2];
	pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;
	pict2.data[0] = dst;
	pict2.data[1] = dst + len;
	pict2.data[2] = dst + len + uv_len;
	pict2.linesize[0] = w;
	pict2.linesize[1] = w >> 1;
	pict2.linesize[2] = w >> 1;	

	img_convert( &pict2, PIX_FMT_YUV420P, &pict1, PIX_FMT_YUV422P, w, h );
	return (len + uv_len + uv_len);
}
int	yuv420p_to_yuv422p2( uint8_t *sY,uint8_t *sCb, uint8_t *sCr, uint8_t *dst[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 2; 
	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
	memset(&pict2,0,sizeof(pict2));
	pict1.data[0] = sY;
	pict1.data[1] = sCb;
	pict1.data[2] = sCr;
	pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;
	pict2.data[0] = dst[0];
	pict2.data[1] = dst[1];
	pict2.data[2] = dst[2];
	pict2.linesize[0] = w;
	pict2.linesize[1] = w >> 1;
	pict2.linesize[2] = w >> 1;	

	img_convert( &pict2, PIX_FMT_YUV422P, &pict1, PIX_FMT_YUV420P, w, h );
	return (len + uv_len + uv_len);

}
int	yuv420p_to_yuv422p( uint8_t *sY,uint8_t *sCb, uint8_t *sCr, uint8_t *dst[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 2; 
	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
	memset(&pict2,0,sizeof(pict2));
	pict1.data[0] = sY;
	pict1.data[1] = sCb;
	pict1.data[2] = sCr;
	pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;
	pict2.data[0] = dst[0];
	pict2.data[1] = dst[1];
	pict2.data[2] = dst[2];
	pict2.linesize[0] = w;
	pict2.linesize[1] = w >> 1;
	pict2.linesize[2] = w >> 1;	
	img_convert( &pict2, PIX_FMT_YUV422P, &pict1, PIX_FMT_YUV420P, w, h );
	return (len + uv_len + uv_len);
}

static	int	vj_avcodec_copy_frame( vj_encoder  *av, uint8_t *src[3], uint8_t *dst )
{
	if(av->sub_sample)
	{
		return(yuv422p_to_yuv420p(src,dst, av->width, av->height ));
	}
	else
	{
		veejay_memcpy( dst, src[0], av->len );
		veejay_memcpy( dst+(av->len), src[1], av->uv_len );
		veejay_memcpy( dst+(av->len+av->uv_len) , src[2], av->uv_len);
	}
	return (av->len + av->uv_len + av->uv_len);

}



int		vj_avcodec_encode_frame( int format, uint8_t *src[3], uint8_t *buf, int buf_len)
{
	//AVPicture pict;
	AVFrame pict;
	memset( &pict, 0, sizeof(pict));
	int res=0;

	if(format == ENCODER_YUV420) // no compression, just copy
		return vj_avcodec_copy_frame( _encoders[ENCODER_YUV420],src, buf );
	if(format == ENCODER_YUV422) // no compression, just copy
		return vj_avcodec_copy_frame( _encoders[ENCODER_YUV422],src, buf );


#ifdef __FALLBACK_LIBDV
	if(format == ENCODER_DVVIDEO )
#ifdef SUPPORT_READ_DV2
		return vj_dv_encode_frame( src, buf );
#else
	return 0;
#endif
#endif
	vj_encoder *av = _encoders[format];

	pict.quality = 1;
	pict.data[0] = src[0];
	pict.data[1] = src[1];
	pict.data[2] = src[2];

	if( out_pixel_format == FMT_422 )
	{
		pict.linesize[0] = av->context->width;
		pict.linesize[1] = av->context->width;
		pict.linesize[2] = av->context->width;
	}
	else
	{
		pict.linesize[0] = av->context->width;
		pict.linesize[1] = av->context->width >> 1;
		pict.linesize[2] = av->context->width >> 1;
	}
	res = avcodec_encode_video( av->context, buf, buf_len, &pict );

	return res;
}
/*
static	int	vj_avcodec_copy_audio_frame( uint8_t *src, uint8_t *buf, int len)
{
	veejay_memcpy( buf, src, len );
	return len;
}

int		vj_avcodec_encode_audio( int format, uint8_t *src, uint8_t *dst, int len, int nsamples )
{
	if(format == ENCODER_YUV420)
		return vj_avcodec_copy_audio_frame;
	if(format == ENCODER_YUV422)
		return vj_avcodec_copy_audio_frame;
	vj_encoder *av = _encoders[format];

	int len = avcodec_encode_audio( av->context, src, len, nsamples );
	return len;
}
*/
