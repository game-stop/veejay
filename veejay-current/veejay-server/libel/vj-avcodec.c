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
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <veejaycore/defs.h>
#include <libel/vj-avcodec.h>
#include <libel/vj-el.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <stdint.h>
#include <string.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/lzo.h>
#include <libstream/vj-yuv4mpeg.h>
#ifdef SUPPORT_READ_DV2
#define __FALLBACK_LIBDV
#include <libel/vj-dv.h>
#endif
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <veejaycore/av.h>
#include <veejaycore/avhelper.h>
#include <veejaycore/avcommon.h>
#define QOI_IMPLEMENTATION 1
#include <libel/qoi.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#ifdef HAVE_NVJPEG
#include <nvjpeg.h>
#include <cuda_runtime_api.h>
#ifdef HAVE_NVJPEG_CUDA_KERNEL
#include <libel/vj-nvjpeg-kernel.h>
#endif

typedef struct {
    nvjpegHandle_t         handle;
    nvjpegEncoderState_t   enc_state;
    nvjpegEncoderParams_t  enc_params;
    cudaStream_t           stream;

	size_t pitch_y;
    size_t pitch_c_in;
    size_t pitch_c_out;

    int width;
    int height;
    int is_422;                     /* needs chroma upsample before encode */

    /* device planes for the raw input */
    uint8_t *d_y;
    uint8_t *d_u;
    uint8_t *d_v;

    /* device planes for upsampled 4:4:4 chroma (only when is_422) */
    uint8_t *d_u_444;
    uint8_t *d_v_444;

	uint8_t *registered_src[3];
	
	uint8_t *h_y;
    uint8_t *h_u;
    uint8_t *h_v;

    vj_nvjpeg_upsample_mode upsample_mode;

	int pin_thrash_count;
	int disable_dynamic_pinning;

} vj_nvjpeg_enc_state;
#endif

#define CODEC_ID_CUDA_MJPEG_422F 1000
#define CODEC_ID_CUDA_MJPEG_422  1001
#define CODEC_ID_CUDA_MJPEG_444F 1002
#define CODEC_ID_CUDA_MJPEG_444  1003


//from gst-ffmpeg, round up a number
#define GEN_MASK(x) ((1<<(x))-1)
#define ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) & ~GEN_MASK(x))
#define ROUND_UP_2(x) ROUND_UP_X (x, 1)
#define ROUND_UP_4(x) ROUND_UP_X (x, 2)
#define ROUND_UP_8(x) ROUND_UP_X (x, 3)
#define DIV_ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) >> (x))

extern int avhelper_set_num_decoders();

static int out_pixel_format = FMT_422F; 

static char*	vj_avcodec_get_codec_name(int codec_id )
{
	char name[64];
	switch(codec_id)
	{
		case CODEC_ID_MJPEG: snprintf(name,sizeof(name),"MJPEG"); break;
#if LIBAVCODEC_VERSION_MAJOR >= 59
		case AV_CODEC_ID_QOI: snprintf(name, sizeof(name), "QOI (ffmpeg)"); break;
#endif
		case CODEC_ID_MPEG4: snprintf(name,sizeof(name), "MPEG4"); break;
		case CODEC_ID_MSMPEG4V3: snprintf(name,sizeof(name), "DIVX"); break;
		case CODEC_ID_DVVIDEO: snprintf(name,sizeof(name), "DVVideo"); break;
		case CODEC_ID_LJPEG: snprintf(name,sizeof(name), "LJPEG" );break;
		case CODEC_ID_SP5X: snprintf(name,sizeof(name), "SP5x"); break;
		case CODEC_ID_THEORA: snprintf(name,sizeof(name),"Theora");break;
		case CODEC_ID_H264: snprintf(name,sizeof(name), "H264");break;
		case CODEC_ID_HUFFYUV: snprintf(name,sizeof(name),"HuffYUV");break;
		case CODEC_ID_CUDA_MJPEG_422F: 
            snprintf(name, sizeof(name), "CUDA MJPEG 4:2:2 Full Range "); 
            break;
        case CODEC_ID_CUDA_MJPEG_422:  
            snprintf(name, sizeof(name), "CUDA MJPEG 4:2:2 Limited Range "); 
            break;
        case CODEC_ID_CUDA_MJPEG_444F: 
            snprintf(name, sizeof(name), "CUDA MJPEG 4:4:4 Full Range "); 
            break;
        case CODEC_ID_CUDA_MJPEG_444:  
            snprintf(name, sizeof(name), "CUDA MJPEG 4:4:4 Limited Range "); 
            break;	
		case 997 : snprintf(name,sizeof(name), "RAW YUV 4:2:2 Planar JPEG"); break;
		case 996 : snprintf(name,sizeof(name), "RAW YUV 4:2:0 Planar JPEG"); break;
		case 995 : snprintf(name,sizeof(name), "YUV4MPEG Stream 4:2:2"); break;
		case 994 : snprintf(name,sizeof(name), "YUV4MPEG Stream 4:2:0"); break;
		case 993 : snprintf(name,sizeof(name), "QOI YUV 4:2:2 Planar (experimental)"); break;
		case 999 : snprintf(name,sizeof(name), "RAW YUV 4:2:0 Planar"); break;
		case 998 : snprintf(name,sizeof(name), "RAW YUV 4:2:2 Planar"); break;
		case 900 : snprintf(name,sizeof(name), "LZO YUV 4:2:2 Planar (experimental)"); break;
		default:
			snprintf(name,sizeof(name), "Unknown"); break;
	}
	return vj_strdup(name);
}

uint8_t 		*vj_avcodec_get_buf( vj_encoder *av )
{
#ifdef SUPPORT_READ_DV2
	vj_dv_encoder *dv = av->dv;
	switch(av->encoder_id) {
		case CODEC_ID_DVVIDEO:
			return dv->dv_video;
	}
#endif
	return av->data[0];
}

int 			vj_avcodec_get_buf_size( vj_encoder *av )
{
	return av->out_frame->len + av->out_frame->uv_len + av->out_frame->uv_len;
}

static vj_encoder	*vj_avcodec_new_encoder( int id, VJFrame *frame, char *filename)
{
	vj_encoder *e = (vj_encoder*) vj_calloc(sizeof(vj_encoder));

	if(!e) return NULL;

	char errbuf[512];
	int selected_out_pixfmt = out_pixel_format;
	int chroma_val = -1;
	
	switch( id ) {
		case 997:
			selected_out_pixfmt = FMT_422F;
			break;
		case 996:
			selected_out_pixfmt = FMT_420F;
			break;
		case 995:
			selected_out_pixfmt = FMT_422;
			chroma_val = Y4M_CHROMA_422;
			break;
		case 994:
			selected_out_pixfmt = FMT_420;
			chroma_val = (out_pixel_format == FMT_422F || out_pixel_format == FMT_420F ? Y4M_CHROMA_420JPEG : Y4M_CHROMA_420MPEG2);
			break;
		case 999:
			selected_out_pixfmt = FMT_420;
			break;
		case 998:
			selected_out_pixfmt = FMT_422;
			break;
		case CODEC_ID_HUFFYUV:
			selected_out_pixfmt = FMT_422;
			break;
		case CODEC_ID_CUDA_MJPEG_422F:
            selected_out_pixfmt = FMT_422F;
            break;
        case CODEC_ID_CUDA_MJPEG_422:
            selected_out_pixfmt = FMT_422;
            break;
        case CODEC_ID_CUDA_MJPEG_444F:
            selected_out_pixfmt = FMT_444F;
            break;
        case CODEC_ID_CUDA_MJPEG_444:
            selected_out_pixfmt = FMT_444;
            break;
		default:
			break;
	}

	int pf = get_ffmpeg_pixfmt( selected_out_pixfmt );

	e->out_frame = yuv_yuv_template( NULL,NULL,NULL, frame->width, frame->height, pf );
	e->in_frame = (VJFrame*) vj_malloc(sizeof(VJFrame));
	veejay_memcpy( e->in_frame, frame, sizeof(VJFrame));

	e->data[0] = (uint8_t*) vj_malloc((e->out_frame->len * 4));
	e->data[1] = e->data[0] + e->out_frame->len;
	e->data[2] = e->data[1] + e->out_frame->len;
	e->data[3] = NULL;

	e->out_frame->data[0] = e->data[0];
	e->out_frame->data[1] = e->data[1];
	e->out_frame->data[2] = e->data[2];
	 
	e->in_frame->data[0] = NULL;
	e->in_frame->data[1] = NULL;
	e->in_frame->data[2] = NULL;
	e->in_frame->data[3] = NULL;

	veejay_memset( e->data[0], 0, e->out_frame->len);
	veejay_memset( e->data[1], 128, e->out_frame->uv_len);
	veejay_memset( e->data[2], 128, e->out_frame->uv_len );


	// strip any A*
	e->in_frame->format = vj_to_pixfmt( out_pixel_format );
	e->in_frame->stride[3] = 0;

	veejay_msg(VEEJAY_MSG_DEBUG, "[AV] Selected output pixel format: %s (internal out fmt %d, chroma %d). Source is %s", yuv_get_pixfmt_description(pf), selected_out_pixfmt, chroma_val,
	 	yuv_get_pixfmt_description(e->in_frame->format));


	if( id > 900 ) {
		sws_template tmpl;
		tmpl.flags = 1;
		e->scaler = yuv_init_swscaler( e->in_frame,e->out_frame, &tmpl, yuv_sws_get_cpu_flags());
		if(e->scaler == NULL) {
			veejay_msg(VEEJAY_MSG_ERROR, "[AV] Failed to initialize scaler context");
			free(e->out_frame);
			free(e->in_frame);
			free(e->data[0]);
			free(e);
			return NULL;
		}
	}

#ifdef SUPPORT_READ_DV2
	if( id == CODEC_ID_DVVIDEO )
	{
		if(!is_dv_resolution(frame->width, frame->height ))
		{	
			veejay_msg(VEEJAY_MSG_ERROR,"[AV] Source video is not in DV resolution");
			free(e->out_frame);
			free(e->in_frame);
			free(e->data[0]);
			free(e);
			return NULL;
		}
		else
		{
			e->dv = (void*)vj_dv_init_encoder( (void*)frame, pf );
		}
	}
	else {
#endif
		
#ifdef SUPPORT_READ_DV2
	}
#endif
	
	if( id == 900 )
	{
		e->lzo = lzo_new(frame->format, frame->width, frame->height, 0 );
	}

	if( id == 995 || id == 994) {
		e->y4m = vj_yuv4mpeg_alloc(frame->width,frame->height,frame->fps, selected_out_pixfmt );
		if( !e->y4m) {
			veejay_msg(0, "[AV] Error while trying to setup Y4M stream, abort");
			free(e->out_frame);
			free(e->in_frame);
			free(e->data[0]);
			yuv_free_swscaler(e->scaler);

			free(e);
			
			return NULL;
		}

		if( vj_yuv_stream_start_write( e->y4m, frame,filename,chroma_val )== -1 )
		{
			veejay_msg(0, "[AV] Unable to write header to  YUV4MPEG stream");
			vj_yuv4mpeg_free( e->y4m );
			free(e->out_frame);
			free(e->in_frame);
			free(e->data[0]);
			yuv_free_swscaler(e->scaler);
			
			free(e);
			return NULL;
		}
	}
	
	if(id != 998 && id != 999 && id != 900 && id != 997 && id != 996 && id != 995 && id != 994 && id != 993 && 
		id != CODEC_ID_CUDA_MJPEG_422 && id != CODEC_ID_CUDA_MJPEG_422F && id != CODEC_ID_CUDA_MJPEG_444 && id != CODEC_ID_CUDA_MJPEG_444F)
	{
#ifdef __FALLBACK_LIBDV
		if(id != CODEC_ID_DVVIDEO)
		{
#endif
			e->codec = avcodec_find_encoder( id );
			if(!e->codec)
			{
			 char *descr = vj_avcodec_get_codec_name(id);
			 veejay_msg(VEEJAY_MSG_ERROR, "[AV] Unable to find encoder '%s'", 	descr );
			 free(e->out_frame);
			 free(e->in_frame);
			 free(e->data[0]);
			 free(e);
			 return NULL;
			}
#ifdef __FALLBACK_LIBDV
		}
#endif

	}
    
	/* Initialize nvJPEG encoder */
if(id == CODEC_ID_CUDA_MJPEG_422F || id == CODEC_ID_CUDA_MJPEG_422 ||
   id == CODEC_ID_CUDA_MJPEG_444F || id == CODEC_ID_CUDA_MJPEG_444) {
#ifdef HAVE_NVJPEG
    nvjpegStatus_t nvs;
    cudaError_t cus;

    vj_nvjpeg_enc_state *state = (vj_nvjpeg_enc_state*) vj_calloc(sizeof(vj_nvjpeg_enc_state));
    if(!state) {
        veejay_msg(VEEJAY_MSG_ERROR, "[AV] Failed to allocate nvJPEG encoder state");
        goto nvenc_fail;
    }

    state->width  = frame->width;
    state->height = frame->height;
    state->is_422 = (id == CODEC_ID_CUDA_MJPEG_422F || id == CODEC_ID_CUDA_MJPEG_422);

#ifdef HAVE_NVJPEG_CUDA_KERNEL
    {
        const char *mode = getenv("VEEJAY_SUPERSAMPLE_MODE");
        state->upsample_mode = (mode && strcmp(mode, "mitchell") == 0)
            ? VJ_NVJPEG_UPSAMPLE_MITCHELL
            : VJ_NVJPEG_UPSAMPLE_DUP;
    }
	if (state->is_422) {
        veejay_msg(VEEJAY_MSG_INFO, 
            "[NVJPEG encoder] CUDA Chroma upsampler is active (mode: %s)", 
            state->upsample_mode == VJ_NVJPEG_UPSAMPLE_MITCHELL ? "mitchell" : "dup");
    }
#else
    if(state->is_422) {
        veejay_msg(VEEJAY_MSG_ERROR,
            "[AV] CUDA MJPEG 4:2:2 requires the chroma upsampling kernel (HAVE_NVJPEG_CUDA_KERNEL)");
        free(state);
        goto nvenc_fail;
    }
#endif

    /* CREATE STREAM FIRST - before any encoder objects */
    cus = cudaStreamCreateWithFlags(&state->stream, cudaStreamNonBlocking);
    if(cus != cudaSuccess) {
        veejay_msg(VEEJAY_MSG_ERROR, "[AV] cudaStreamCreate failed (%d)", (int)cus);
        free(state); goto nvenc_fail;
    }

    nvs = nvjpegCreateSimple(&state->handle);
    if(nvs != NVJPEG_STATUS_SUCCESS) {
        veejay_msg(VEEJAY_MSG_ERROR, "[AV] nvjpegCreateSimple failed (%d)", (int)nvs);
        cudaStreamDestroy(state->stream);
        free(state); goto nvenc_fail;
    }

    /* Pass state->stream to all encoder creation calls */
    nvs = nvjpegEncoderStateCreate(state->handle, &state->enc_state, state->stream);
    if(nvs != NVJPEG_STATUS_SUCCESS) {
        veejay_msg(VEEJAY_MSG_ERROR, "[AV] nvjpegEncoderStateCreate failed (%d)", (int)nvs);
        nvjpegDestroy(state->handle);
        cudaStreamDestroy(state->stream);
        free(state); goto nvenc_fail;
    }

    nvs = nvjpegEncoderParamsCreate(state->handle, &state->enc_params, state->stream);
    if(nvs != NVJPEG_STATUS_SUCCESS) {
        veejay_msg(VEEJAY_MSG_ERROR, "[AV] nvjpegEncoderParamsCreate failed (%d)", (int)nvs);
        nvjpegEncoderStateDestroy(state->enc_state);
        nvjpegDestroy(state->handle);
        cudaStreamDestroy(state->stream);
        free(state); goto nvenc_fail;
    }

    nvs = nvjpegEncoderParamsSetQuality(state->enc_params, 90, state->stream);
    if(nvs != NVJPEG_STATUS_SUCCESS)
        veejay_msg(VEEJAY_MSG_WARNING, "[AV] nvjpegEncoderParamsSetQuality failed (%d)", (int)nvs);


	// upsample to 4:4:4 so nvjpeg accepts the input
     nvs = nvjpegEncoderParamsSetSamplingFactors(state->enc_params, NVJPEG_CSS_444, state->stream);
     if(nvs != NVJPEG_STATUS_SUCCESS) {
        veejay_msg(VEEJAY_MSG_ERROR, "[AV] nvjpegEncoderParamsSetSamplingFactors failed (%d)", (int)nvs);
        nvjpegEncoderStateDestroy(state->enc_state);
        nvjpegDestroy(state->handle);
        cudaStreamDestroy(state->stream);
        free(state); goto nvenc_fail;
     }

    /* Allocate pitched device planes for optimal 2D alignment */
    size_t y_size = (size_t)state->width * state->height;
    size_t chroma_in_w  = state->is_422 ? (state->width / 2) : state->width;
    size_t chroma_in_sz = chroma_in_w * state->height;

    if(cudaMallocPitch((void**)&state->d_y, &state->pitch_y, state->width, state->height) != cudaSuccess ||
       cudaMallocPitch((void**)&state->d_u, &state->pitch_c_in, chroma_in_w, state->height) != cudaSuccess ||
       cudaMallocPitch((void**)&state->d_v, &state->pitch_c_in, chroma_in_w, state->height) != cudaSuccess) {
        veejay_msg(VEEJAY_MSG_ERROR, "[AV] cudaMallocPitch for encoder planes failed");
        goto nvenc_fail_state;
    }

    if(state->is_422) {
        if(cudaMallocPitch((void**)&state->d_u_444, &state->pitch_c_out, state->width, state->height) != cudaSuccess ||
           cudaMallocPitch((void**)&state->d_v_444, &state->pitch_c_out, state->width, state->height) != cudaSuccess) {
            veejay_msg(VEEJAY_MSG_ERROR, "[AV] cudaMallocPitch for 4:4:4 chroma failed");
            goto nvenc_fail_state;
        }
    } else {
        state->pitch_c_out = state->pitch_c_in;
    }

	/* Initialize registered_src tracking */
    state->registered_src[0] = NULL;
    state->registered_src[1] = NULL;
    state->registered_src[2] = NULL;

    /* Allocate pinned host staging buffers as fallback if cudaHostRegister fails */
    if(cudaHostAlloc((void**)&state->h_y, y_size, cudaHostAllocDefault) != cudaSuccess ||
       cudaHostAlloc((void**)&state->h_u, chroma_in_sz, cudaHostAllocDefault) != cudaSuccess ||
       cudaHostAlloc((void**)&state->h_v, chroma_in_sz, cudaHostAllocDefault) != cudaSuccess) {
        veejay_msg(VEEJAY_MSG_WARNING, "[AV] cudaHostAlloc for pinned fallback buffers failed");
        if(state->h_y) cudaFreeHost(state->h_y);
        if(state->h_u) cudaFreeHost(state->h_u);
        if(state->h_v) cudaFreeHost(state->h_v);
        state->h_y = state->h_u = state->h_v = NULL;
    }

    e->nvjpeg = state;
    e->encoder_id = id;
    e->width  = frame->width;
    e->height = frame->height;
    e->len    = e->out_frame->len;
    e->uv_len = e->out_frame->uv_len;

    veejay_msg(VEEJAY_MSG_INFO,
        "[AV] Initialized nvJPEG CUDA MJPEG encoder (%s, quality=90, stream=%p)",
        state->is_422 ? "4:2:2 -> 4:4:4 upsample" : "4:4:4",
        (void*)state->stream);
    return e;

nvenc_fail_state:
    if(state->d_y)     cudaFree(state->d_y);
    if(state->d_u)     cudaFree(state->d_u);
    if(state->d_v)     cudaFree(state->d_v);
    if(state->d_u_444) cudaFree(state->d_u_444);
    if(state->d_v_444) cudaFree(state->d_v_444);
    if(state->enc_params) nvjpegEncoderParamsDestroy(state->enc_params);
    if(state->enc_state)  nvjpegEncoderStateDestroy(state->enc_state);
    if(state->handle)     nvjpegDestroy(state->handle);
    if(state->stream)     cudaStreamDestroy(state->stream);
    free(state);
nvenc_fail:
    free(e->out_frame);
    free(e->in_frame);
    free(e->data[0]);
    free(e);
    return NULL;
#else
    veejay_msg(VEEJAY_MSG_ERROR, "[AV] CUDA MJPEG encoding not supported (nvJPEG not available)");
    free(e->out_frame); free(e->in_frame); free(e->data[0]); free(e);
    return NULL;
#endif
}


	if( id != 998 && id != 999 && id!= 900 && id != 997 && id != 996 && id != CODEC_ID_DVVIDEO && id != 995 && id != 994 && id != 993)
	{
#ifdef __FALLBACK_LIBDV
	  if(id != CODEC_ID_DVVIDEO )
		{
#endif
#if LIBAVCODEC_VERSION_MAJOR > 54  
   	    e->context = avcodec_alloc_context3(e->codec);
#else
		e->context = avcodec_alloc_context();
#endif
		e->context->bit_rate = 2750 * 1024;
		e->context->width = frame->width;
 		e->context->height = frame->height;
		
#if LIBAVCODEC_VERSION_MAJOR >= 50
		e->context->time_base = (AVRational) { 1, frame->fps };
#else
		e->context->frame_rate = frame->fps;
		e->context->frame_rate_base = 1;
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 60
		e->packet = av_packet_alloc();
		e->frame = av_frame_alloc();
		e->frame->format = get_ffmpeg_pixfmt( selected_out_pixfmt );
		e->frame->width = frame->width;
		e->frame->height = frame->height;

	    int av_ret = av_frame_get_buffer(e->frame, 0);
		if( av_ret < 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "[AV] Unable to allocate buffers for encoder");
			 free(e->out_frame);
			 free(e->in_frame);
			 free(e->data[0]);
			 av_packet_free(&(e->packet));
			 av_frame_free(&(e->frame));
			 free(e);
		
			return NULL;
		}

		e->context->framerate = (AVRational) { 1, frame->fps };
#endif
		e->context->sample_aspect_ratio.den = 1;
		e->context->sample_aspect_ratio.num = 1;
		e->context->qcompress = 0.0;
		e->context->qblur = 0.0;
		e->context->max_b_frames = 0;
		e->context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
		e->context->flags = CODEC_FLAG_QSCALE;
		e->context->gop_size = 0;
		e->context->workaround_bugs = FF_BUG_AUTODETECT;
#if LIBAVCODEC_VERSION_MAJOR < 60
		e->context->prediction_method = 0;
#endif
		e->context->dct_algo = FF_DCT_AUTO; 
		e->context->pix_fmt = pf;

		//pf = e->context->pix_fmt;
		char *descr = vj_avcodec_get_codec_name( id );
#if LIBAVCODEC_VERSION_MAJOR > 54

		int n_threads = avhelper_set_num_decoders();

		if (e->codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
			e->context->thread_type = FF_THREAD_FRAME;
			e->context->thread_count = n_threads;	
		}
		else if (e->codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) {
			e->context->thread_type = FF_THREAD_SLICE;
			e->context->thread_count = n_threads;	
		}

		int ret = avcodec_open2( e->context, e->codec, NULL );
#else
		int ( avcodec_open( e->context, e->codec ) < 0 );
#endif
		if( ret < 0 ) {
			av_strerror( ret, errbuf, sizeof(errbuf));
			veejay_msg(VEEJAY_MSG_ERROR, "[AV] Unable to open codec '%s': %s" , descr, errbuf );
			avhelper_free_context( &(e->context) );
			free(e->out_frame);
			free(e->in_frame);
			free(e->data[0]);
			free(e);
			if(descr) free(descr);
			return NULL;
		}
		else
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "[AV] Opened codec %s [in pixfmt=%d]", descr, e->context->pix_fmt );
			if(e->context->color_range == AVCOL_RANGE_JPEG ) {
				veejay_msg(VEEJAY_MSG_DEBUG, "[AV] Full pixel range (0-255)");
			}
			if(e->context->color_range == AVCOL_RANGE_UNSPECIFIED) {
				veejay_msg(VEEJAY_MSG_WARNING, "[AV] Limited range (not specified)" );
			}

			if(e->context->color_range == AVCOL_RANGE_MPEG ) {
				veejay_msg(VEEJAY_MSG_INFO, "[AV] Limited pixel range (16-240)");
			}

			free(descr);
		}
#ifdef __FALLBACK_LIBDV
	}
#endif
	}

	e->width = e->out_frame->width;
	e->height = e->out_frame->height;
	e->encoder_id = id;
	e->shift_y = e->out_frame->shift_v;
	e->shift_x = e->out_frame->shift_h;
	e->len = e->out_frame->len;
	e->uv_len = e->out_frame->uv_len;

	return e;
}
void		vj_avcodec_close_encoder( vj_encoder *av )
{
	if(av)
	{
		if(av->context)
		{
#if LIBAVCODEC_VERSION_MAJOR > 59
			avcodec_free_context( &(av->context) );
#else
			avcodec_close( av->context );
#endif
		}
#if LIBAVCODEC_VERSION_MAJOR >= 60
		if(av->packet)
			av_packet_free( &(av->packet) );
		if(av->frame)
			av_frame_free( &(av->frame) );
#endif
		if(av->data[0])
			free(av->data[0]);
		if(av->lzo)
			lzo_free(av->lzo);
#ifdef SUPPORT_READ_DV2
		if(av->dv)
			vj_dv_free_encoder( (vj_dv_encoder*) av->dv );
#endif
		if(av->scaler)
			yuv_free_swscaler(av->scaler);

	    if(av->out_frame)
			free(av->out_frame);
		if(av->in_frame)
			free(av->in_frame);
		if(av->y4m)
			vj_yuv4mpeg_free( (vj_yuv*) av->y4m );

#ifdef HAVE_NVJPEG
		if(av->nvjpeg) {
			vj_nvjpeg_enc_state *state = (vj_nvjpeg_enc_state*) av->nvjpeg;
			if(state->stream) cudaStreamSynchronize(state->stream);

			for(int i = 0; i < 3; i++) {
				if(state->registered_src[i]) {
					cudaHostUnregister(state->registered_src[i]);
					state->registered_src[i] = NULL;
				}
			}

			if(state->d_y)     cudaFree(state->d_y);
			if(state->d_u)     cudaFree(state->d_u);
			if(state->d_v)     cudaFree(state->d_v);
			if(state->d_u_444) cudaFree(state->d_u_444);
			if(state->d_v_444) cudaFree(state->d_v_444);

			if(state->h_y) cudaFreeHost(state->h_y);
			if(state->h_u) cudaFreeHost(state->h_u);
			if(state->h_v) cudaFreeHost(state->h_v);

			if(state->stream)  cudaStreamDestroy(state->stream);
			if(state->enc_params) nvjpegEncoderParamsDestroy(state->enc_params);
			if(state->enc_state)  nvjpegEncoderStateDestroy(state->enc_state);
			if(state->handle)     nvjpegDestroy(state->handle);
			free(state);
			av->nvjpeg = NULL;
		}	
#endif

		free(av);
	}
	av = NULL;
}

int vj_avcodec_is_internal(int format) {
    if(format == ENCODER_MJPEG || format == ENCODER_QUICKTIME_MJPEG || 
       format == ENCODER_DVVIDEO || format == ENCODER_QUICKTIME_DV ||
       format == ENCODER_HUFFYUV || format == ENCODER_LJPEG ||
       format == ENCODER_CUDA_MJPEG_422F || format == ENCODER_CUDA_MJPEG_422 ||
       format == ENCODER_CUDA_MJPEG_444F || format == ENCODER_CUDA_MJPEG_444)
        return 0;
    return 1;
}
int		vj_avcodec_find_codec( int encoder )
{
	switch( encoder)
	{
		case ENCODER_MJPEG:
		case ENCODER_QUICKTIME_MJPEG:
			return CODEC_ID_MJPEG;
		case ENCODER_DVVIDEO:
		case ENCODER_QUICKTIME_DV:
			return CODEC_ID_DVVIDEO;
		case ENCODER_HUFFYUV:
			return CODEC_ID_HUFFYUV;
		case ENCODER_LJPEG:
			return CODEC_ID_LJPEG;	
		case ENCODER_YUV420:
			return 999;
		case ENCODER_YUV422:
			return 998;
		case ENCODER_YUV422F:
			return 997;
		case ENCODER_YUV420F:
			return 996;
		case ENCODER_LZO:
			return 900;
		case ENCODER_QOI:
			return 993;
		case ENCODER_YUV4MPEG:
			return 995;
		case ENCODER_YUV4MPEG420:
			return 994;
	    case ENCODER_CUDA_MJPEG_422F: return CODEC_ID_CUDA_MJPEG_422F;
        case ENCODER_CUDA_MJPEG_422:  return CODEC_ID_CUDA_MJPEG_422;
        case ENCODER_CUDA_MJPEG_444F: return CODEC_ID_CUDA_MJPEG_444F;
        case ENCODER_CUDA_MJPEG_444:  return CODEC_ID_CUDA_MJPEG_444;	
		default:
			veejay_msg(VEEJAY_MSG_DEBUG, "[AV] Unknown format %d selected", encoder );
			return 0;
	}
	return 0;
}

char		vj_avcodec_find_lav( int encoder )
{
	switch( encoder)
	{
		case ENCODER_MJPEG:
        case ENCODER_CUDA_MJPEG_422F:
        case ENCODER_CUDA_MJPEG_422:
        case ENCODER_CUDA_MJPEG_444F:
        case ENCODER_CUDA_MJPEG_444:		
			return 'a';
		case ENCODER_HUFFYUV:
			return 'H';
		case ENCODER_QUICKTIME_MJPEG:
		       	return 'q';
		case ENCODER_DVVIDEO:
			return 'd';
		case ENCODER_QUICKTIME_DV:
			return 'Q';
		case ENCODER_LJPEG:
			return 'l';
		case ENCODER_YUV420:
			return 'Y';
		case ENCODER_YUV422:
			return 'P';
		case ENCODER_YUV422F:
			return 'V';
		case ENCODER_YUV420F:
			return 'v';
		case ENCODER_LZO:
			return 'L';
		case ENCODER_QOI:
			return 'o';
		case ENCODER_YUV4MPEG:
		case ENCODER_YUV4MPEG420:
			return 'S';
		default:
			veejay_msg(VEEJAY_MSG_DEBUG, "[AV] Unknown format %d selected", encoder );
			return 0;
	}
	return 0;
}


static struct {
    const char *descr;
    int encoder_id;
} encoder_names[] = {
    { "Invalid codec ", -1 },
    { "DV2 ", ENCODER_DVVIDEO },
    { "MJPEG ", ENCODER_MJPEG },
    { "HuffYUV ", ENCODER_HUFFYUV },
    { "YUV 4:2:2 Planar, 0-255 full range ", ENCODER_YUV422F },
    { "YUV 4:2:0 Planar, 0-255 full range ", ENCODER_YUV420F },
    { "YUV 4:2:2 Planar, CCIR 601. 16-235/16-240 ", ENCODER_YUV422 },
    { "YUV 4:2:0 Planar, CCIR 601, 16-235/16-240 ", ENCODER_YUV420 },
    { "YUV 4:2:2 Planar, LZO compressed (experimental) ", ENCODER_LZO },
    { "QOI grayscale, QOI (experimental) ", ENCODER_QOI },
    { "DIVX ", ENCODER_DIVX },
    { "Quicktime DV ", ENCODER_QUICKTIME_DV },
    { "Quicktime MJPEG ", ENCODER_QUICKTIME_MJPEG },
    { "YUV4MPEG Stream 4:2:2 ", ENCODER_YUV4MPEG },
    { "YUV4MPEG Stream 4:2:0 for MPEG2 ", ENCODER_YUV4MPEG420 },
    { "CUDA MJPEG 4:2:2 Planar, 0-255 full range ", ENCODER_CUDA_MJPEG_422F },
    { "CUDA MJPEG 4:2:2 Planar, CCIR 601, 16-235/16-240 ", ENCODER_CUDA_MJPEG_422 },
    { "CUDA MJPEG 4:4:4 Planar, 0-255 full range ", ENCODER_CUDA_MJPEG_444F },
    { "CUDA MJPEG 4:4:4 Planar, CCIR 601, 16-235/16-240 ", ENCODER_CUDA_MJPEG_444 },
    
    { NULL, 0 }
};

const char		*vj_avcodec_get_encoder_name( int encoder_id )
{
	int i;
	for( i =1 ; encoder_names[i].descr != NULL ; i ++ ) {
		if( encoder_names[i].encoder_id == encoder_id ) {
			return encoder_names[i].descr;
		}
	}
	return encoder_names[0].descr;
}

int		vj_avcodec_stop( void *encoder , int fmt)
{
	if(!encoder)
		return 0;
	vj_encoder *env = (vj_encoder*) encoder;

	if( fmt == 900 )
	{
		return 1;
	}

	vj_avcodec_close_encoder( env );

	encoder = NULL;
	return 1;
}

void 		*vj_avcodec_start( VJFrame *frame, int encoder, char *filename )
{
	int codec_id = vj_avcodec_find_codec( encoder );
	void *ee = NULL;
#ifndef SUPPORT_READ_DV2
	if( codec_id == CODEC_ID_DVVIDEO ) {
		veejay_msg(VEEJAY_MSG_ERROR, "[AV] No support for DV encoding built in");
		return NULL;
	}
#endif	
	ee = vj_avcodec_new_encoder( codec_id, frame ,filename);
	if(!ee)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "[AV] Failed to start encoder %x",encoder);
		return NULL;
	}
	return ee;
}


int		vj_avcodec_init( int pixel_format, int verbose)
{
	out_pixel_format = pixel_format;
	
	char *av_log_setting = getenv("VEEJAY_AV_LOG");
	if(av_log_setting != NULL) {
		int level = atoi(av_log_setting);
		veejay_msg(VEEJAY_MSG_DEBUG, "[AV] ffmpeg/libav log level set to %d", level);
		av_log_set_level(level);
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "[AV] ffmpeg/libav log level not set (use VEEJAY_AV_LOG=level)");
		av_log_set_level( AV_LOG_QUIET);
	}

#if LIBAVCODEC_VERSION_MAJOR < 54
	avcodec_register_all();
	
#else
#if LIBAVCODEC_VERSION_MAJOR < 60
	av_register_all();
#endif
#endif
	return 1;
}

void	    vj_avcodec_print_version(void) {
	veejay_msg(VEEJAY_MSG_INFO, "[AV] ffmpeg/libav library version %d.%d.%d", LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO);

}

static	int	vj_avcodec_copy_frame( vj_encoder  *av, uint8_t *src[4], uint8_t *dst)
{
	VJFrame *A = av->in_frame;
	VJFrame *B = av->out_frame;

	A->data[0] = src[0]; 
	A->data[1] = src[1]; 
	A->data[2] = src[2]; 
	A->data[3] = NULL;
	
	B->data[0] = dst;    
	B->data[1] = dst + B->len; 
	B->data[2] = dst + B->len + B->uv_len;
	B->data[3] = NULL;

	yuv_convert_and_scale( av->scaler, A, B);

	return (B->len + B->uv_len + B->uv_len);
}

static int vj_avcodec_encode_video( AVPacket *pkt, AVCodecContext *ctx, uint8_t *buf, int len, AVFrame *frame )
{
#if LIBAVCODEC_VERSION_MAJOR < 60
	if( avcodec_encode_video2) {
		char errbuf[512];
		int got_packet_ptr = 0;
		pkt->data = buf;
		pkt->size = len;

		int res = avcodec_encode_video2( ctx, pkt, frame, &got_packet_ptr);
		if( res < 0) {
			av_strerror( res, errbuf, sizeof(errbuf));
			veejay_msg(0, "[AV] Unable to encode frame: %s", errbuf);
			return -1;
		}

		if( res == 0 ) {
			return pkt->size;
		}

		return -1;
	}
	else if( avcodec_encode_video ) {
		return avcodec_encode_video(ctx,buf,len,frame);
	}
#else

	int ret = avcodec_send_frame( ctx, frame );
	//av_frame_free(&enc_frame);

	if( ret < 0 ) {
		veejay_msg(0, "[AV] Error sending frame to decoder: %s", av_err2str(ret));
		return -1;
	}

	pkt->data = buf;
	pkt->size = len;


	 int total_bytes = 0;
	 while (ret >= 0)
     {
        ret = avcodec_receive_packet(ctx, pkt);
     	if( ret == AVERROR(EAGAIN)) {
			break;
		}
		else if( ret == AVERROR_EOF) {
			break;
		}
		else if( ret < 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "[AV] Encoding failed: %s",av_err2str(ret));
		}
	 
	 	int copy_size = pkt->size;
		if( total_bytes + copy_size > len ) {
			veejay_msg(VEEJAY_MSG_WARNING,"[AV] Output buffer too small (%d < %d), truncating", len, total_bytes + copy_size );
			copy_size = len - total_bytes;
		}

		veejay_memcpy(buf + total_bytes, pkt->data, copy_size );
		total_bytes += copy_size;

		av_packet_unref(pkt);

	 }

	 return total_bytes;

#endif

	return -1;
}

void	vj_avcodec_flush_frame(void *encoder, uint8_t *buf, int buf_len ) 
{
	vj_encoder *av = (vj_encoder*) encoder;
#if LIBAVCODEC_VERSION_MAJOR >= 60

	int total_bytes = 0;

	avcodec_send_frame(av->context, NULL);

	AVPacket *pkt = av_packet_alloc();
	if(!pkt) {
		return;
	}

	while( 1 ) {
		int ret = avcodec_receive_packet(av->context, pkt);
		if( ret == AVERROR(EAGAIN) || ret == AVERROR_EOF ) {
			break;
		}
		else if (ret < 0) {
			veejay_msg(0, "[AV] Error receiving packet during flush: %s", av_err2str(ret));
			break;
		}

		int copy_size = pkt->size;
		if( total_bytes + copy_size > buf_len ) {
			veejay_msg(0, "[AV] Flush buffer too small (%d < %d), truncating", buf_len, total_bytes + copy_size );
			copy_size = buf_len - total_bytes;
		}

		veejay_memcpy( buf + total_bytes, pkt->data, copy_size );
		total_bytes += copy_size;

		av_packet_unref(pkt);
	}

	av_packet_free(&pkt);
#endif
}

int vj_avcodec_encode_frame(void *encoder, long nframe, int format, 
                            uint8_t *src[4], uint8_t *buf, int buf_len, int in_fmt)
{
	vj_encoder *av = (vj_encoder*) encoder;
  
/*
    if(format == ENCODER_CUDA_MJPEG_422F || format == ENCODER_CUDA_MJPEG_422 ||
       format == ENCODER_CUDA_MJPEG_444F || format == ENCODER_CUDA_MJPEG_444) {
#ifdef HAVE_NVJPEG
        vj_nvjpeg_enc_state *state = (vj_nvjpeg_enc_state*) av->nvjpeg;
        if(!state || !state->enc_state) {
            veejay_msg(VEEJAY_MSG_ERROR, "[AV] nvJPEG encoder not initialized");
            return -1;
        }

        size_t y_size       = (size_t)state->width * state->height;
        size_t chroma_in_w  = state->is_422 ? (size_t)(state->width / 2) : (size_t)state->width;
        size_t chroma_in_sz = chroma_in_w * state->height;

        veejay_msg(VEEJAY_MSG_DEBUG, "[NVJPEG encoder] frame=%dx%d is_422=%d y_size=%zu chroma_in_w=%zu chroma_in_sz=%zu",
                   state->width, state->height, state->is_422,
                   y_size, chroma_in_w, chroma_in_sz);

        cudaError_t cu0 = cudaMemcpyAsync(state->d_y, src[0], y_size,       cudaMemcpyHostToDevice, state->stream);
        cudaError_t cu1 = cudaMemcpyAsync(state->d_u, src[1], chroma_in_sz, cudaMemcpyHostToDevice, state->stream);
        cudaError_t cu2 = cudaMemcpyAsync(state->d_v, src[2], chroma_in_sz, cudaMemcpyHostToDevice, state->stream);

        if(cu0 != cudaSuccess || cu1 != cudaSuccess || cu2 != cudaSuccess) {
            veejay_msg(VEEJAY_MSG_ERROR, "[NVJPEG encoder] H2D upload failed: y=%d u=%d v=%d", (int)cu0, (int)cu1, (int)cu2);
            return -1;
        }

        uint8_t *enc_u = state->d_u;
        uint8_t *enc_v = state->d_v;

#ifdef HAVE_NVJPEG_CUDA_KERNEL
        if(state->is_422) {
            cudaError_t cus = vj_nvjpeg_upsample_chroma(
                state->d_u, chroma_in_w,
                state->d_v, chroma_in_w,
                state->d_u_444, (size_t)state->width,
                state->d_v_444, (size_t)state->width,
                (int)chroma_in_w, state->height,
                state->width,     state->height,
                state->upsample_mode,
                state->stream);
            if(cus != cudaSuccess) {
                veejay_msg(VEEJAY_MSG_ERROR, "[NVJPEG encoder] chroma upsample failed (%d): %s",
                           (int)cus, cudaGetErrorString(cus));
                return -1;
            }
            enc_u = state->d_u_444;
            enc_v = state->d_v_444;
            veejay_msg(VEEJAY_MSG_DEBUG, "[NVJPEG encoder] chroma upsampled %dx%d -> %dx%d (mode=%d)",
                       (int)chroma_in_w, state->height, state->width, state->height,
                       (int)state->upsample_mode);
        }
#endif

        nvjpegImage_t img;
        memset(&img, 0, sizeof(img));
        img.channel[0] = state->d_y;   img.pitch[0] = (size_t)state->width;
        img.channel[1] = enc_u;        img.pitch[1] = (size_t)state->width;
        img.channel[2] = enc_v;        img.pitch[2] = (size_t)state->width;

        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[NVJPEG encoder] EncodeYUV: css=444 w=%d h=%d pitch=[%zu,%zu,%zu] ptrs=[%p,%p,%p] handle=%p state=%p params=%p stream=%p",
                   state->width, state->height,
                   img.pitch[0], img.pitch[1], img.pitch[2],
                   (void*)img.channel[0], (void*)img.channel[1], (void*)img.channel[2],
                   (void*)state->handle, (void*)state->enc_state,
                   (void*)state->enc_params, (void*)state->stream);

        nvjpegStatus_t nvs = nvjpegEncodeYUV(
            state->handle, state->enc_state, state->enc_params,
            &img, NVJPEG_CSS_444, state->width, state->height, state->stream);
        if(nvs != NVJPEG_STATUS_SUCCESS) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "[NVJPEG encoder] nvjpegEncodeYUV failed (status %d) w=%d h=%d css=%d",
                       (int)nvs, state->width, state->height, (int)NVJPEG_CSS_444);
            return -1;
        }

        size_t length = (size_t)buf_len;
        nvs = nvjpegEncodeRetrieveBitstream(
            state->handle, state->enc_state, buf, &length, state->stream);
        if(nvs != NVJPEG_STATUS_SUCCESS) {
            veejay_msg(VEEJAY_MSG_ERROR, "[NVJPEG encoder] nvjpegEncodeRetrieveBitstream failed (status %d)", (int)nvs);
            return -1;
        }

        cudaStreamSynchronize(state->stream);

        veejay_msg(VEEJAY_MSG_DEBUG, "[NVJPEG encoder] encoded %zu bytes (buf_len=%d)", length, buf_len);
        return (int)length;
#else
        veejay_msg(VEEJAY_MSG_ERROR, "[AV] CUDA MJPEG encoding not supported");
        return -1;
#endif
    }
*/
    if(format == ENCODER_CUDA_MJPEG_422F || format == ENCODER_CUDA_MJPEG_422 ||
       format == ENCODER_CUDA_MJPEG_444F || format == ENCODER_CUDA_MJPEG_444) {
#ifdef HAVE_NVJPEG
        vj_nvjpeg_enc_state *state = (vj_nvjpeg_enc_state*) av->nvjpeg;
        if(!state || !state->enc_state) {
            veejay_msg(VEEJAY_MSG_ERROR, "[AV] nvJPEG encoder not initialized");
            return -1;
        }

        /*size_t y_size       = (size_t)state->width * state->height;
        size_t chroma_in_w  = state->is_422 ? (size_t)(state->width / 2) : (size_t)state->width;
        size_t chroma_in_sz = chroma_in_w * state->height;

        cudaError_t cu0 = cudaMemcpyAsync(state->d_y, src[0], y_size,       cudaMemcpyHostToDevice, state->stream);
        cudaError_t cu1 = cudaMemcpyAsync(state->d_u, src[1], chroma_in_sz, cudaMemcpyHostToDevice, state->stream);
        cudaError_t cu2 = cudaMemcpyAsync(state->d_v, src[2], chroma_in_sz, cudaMemcpyHostToDevice, state->stream);
        if(cu0 != cudaSuccess || cu1 != cudaSuccess || cu2 != cudaSuccess) {
            veejay_msg(VEEJAY_MSG_ERROR, "[NVJPEG encoder] H2D failed: y=%d u=%d v=%d", (int)cu0, (int)cu1, (int)cu2);
            return -1;
        }
		*/

		size_t y_size       = (size_t)state->width * state->height;
        size_t chroma_in_w  = state->is_422 ? (size_t)(state->width / 2) : (size_t)state->width;
        size_t chroma_in_sz = chroma_in_w * state->height;
        
        /* Packed CPU sizes for registration */
        size_t sizes[3] = { y_size, chroma_in_sz, chroma_in_sz };
        
        /* 2D Dimensions for Transfer */
        size_t widths[3]  = { (size_t)state->width, chroma_in_w, chroma_in_w };
        size_t heights[3] = { (size_t)state->height, (size_t)state->height, (size_t)state->height };
        size_t dpitch[3]  = { state->pitch_y, state->pitch_c_in, state->pitch_c_in };
        
        /* Assume CPU buffer stride equals width (packed) */
        size_t spitch[3]  = { (size_t)state->width, chroma_in_w, chroma_in_w };

        uint8_t *d_planes[3] = { state->d_y, state->d_u, state->d_v };
        uint8_t *h_planes[3] = { state->h_y, state->h_u, state->h_v };

        for(int i = 0; i < 3; i++) {
            int use_fallback = 0;

            if (!state->disable_dynamic_pinning) {
                if(src[i] != state->registered_src[i]) {
                    if(state->registered_src[i]) {
                        cudaHostUnregister(state->registered_src[i]);
                        state->registered_src[i] = NULL;
                        state->pin_thrash_count++;
                    }
                    
                    if (state->pin_thrash_count > 30) {
                        veejay_msg(VEEJAY_MSG_WARNING, "[NVJPEG encoder] Pointer thrashing detected. Disabling dynamic pinning.");
                        state->disable_dynamic_pinning = 1;
                        use_fallback = 1;
                    } else {
                        cudaError_t reg_err = cudaHostRegister(src[i], sizes[i], cudaHostRegisterDefault);
                        if(reg_err == cudaSuccess) {
                            state->registered_src[i] = src[i];
                            cudaMemcpy2DAsync(d_planes[i], dpitch[i], src[i], spitch[i], widths[i], heights[i], cudaMemcpyHostToDevice, state->stream);
                        } else {
                            veejay_msg(VEEJAY_MSG_DEBUG, "[NVJPEG encoder] cudaHostRegister failed for plane %d, using pinned fallback", i);
                            use_fallback = 1;
                        }
                    }
                } else {
                    cudaMemcpy2DAsync(d_planes[i], dpitch[i], src[i], spitch[i], widths[i], heights[i], cudaMemcpyHostToDevice, state->stream);
                }
            } else {
                use_fallback = 1;
            }

            if (use_fallback) {
                if(h_planes[i]) {
                    veejay_memcpy(h_planes[i], src[i], sizes[i]);
                    cudaMemcpy2DAsync(d_planes[i], dpitch[i], h_planes[i], spitch[i], widths[i], heights[i], cudaMemcpyHostToDevice, state->stream);
                } else {
                    veejay_msg(VEEJAY_MSG_ERROR, "[NVJPEG encoder] H2D failed: cannot register and no fallback buffer for plane %d", i);
                    return -1;
                }
            }
        }

		uint8_t *enc_u = state->d_u;
        uint8_t *enc_v = state->d_v;

#ifdef HAVE_NVJPEG_CUDA_KERNEL
        if(state->is_422) {
            cudaError_t cus = vj_nvjpeg_upsample_chroma(
                state->d_u, state->pitch_c_in,
                state->d_v, state->pitch_c_in,
                state->d_u_444, state->pitch_c_out,
                state->d_v_444, state->pitch_c_out,
                (int)chroma_in_w, state->height,
                state->width,     state->height,
                state->upsample_mode,
                state->stream);
                
            if(cus != cudaSuccess) {
                veejay_msg(VEEJAY_MSG_ERROR, "[NVJPEG encoder] chroma upsample failed (%d)", (int)cus);
                return -1;
            }
            enc_u = state->d_u_444;
            enc_v = state->d_v_444;
        }
#endif

        /* Describe the pitched 4:4:4 image for nvJPEG */
        nvjpegImage_t img;
        memset(&img, 0, sizeof(img));
        img.channel[0] = state->d_y;   img.pitch[0] = state->pitch_y;
        img.channel[1] = enc_u;        img.pitch[1] = state->pitch_c_out;
        img.channel[2] = enc_v;        img.pitch[2] = state->pitch_c_out;

        /* Encode directly as 4:4:4 */
        nvjpegStatus_t nvs = nvjpegEncodeYUV(
            state->handle, state->enc_state, state->enc_params,
            &img, NVJPEG_CSS_444, state->width, state->height, state->stream);
			
		if(nvs != NVJPEG_STATUS_SUCCESS) {
			veejay_msg(VEEJAY_MSG_ERROR, "[NVJPEG encoder] nvjpegEncodeYUV failed (status %d)", (int)nvs);
			return -1;
		}

		size_t length = (size_t)buf_len;
		nvs = nvjpegEncodeRetrieveBitstream(
			state->handle, state->enc_state, buf, &length, state->stream);
		if(nvs != NVJPEG_STATUS_SUCCESS) {
			veejay_msg(VEEJAY_MSG_ERROR, "[NVJPEG encoder] nvjpegEncodeRetrieveBitstream failed (status %d)", (int)nvs);
			return -1;
		}

		cudaStreamSynchronize(state->stream);
		return (int)length;
#else
        veejay_msg(VEEJAY_MSG_ERROR, "[AV] CUDA MJPEG encoding not supported");
        return -1;
#endif
    }

	if(format == ENCODER_QOI) {
		int res = 0;
	    qoi_desc d;
		d.channels = 1;
		d.colorspace = QOI_LINEAR;
		d.height = av->height;
		d.width = av->width;
	
	
		const unsigned char *tmp[4] = { src[0], src[1], src[2], src[3] };
	    	qoi_encode( tmp, &d, &res, buf, buf_len );

		return res;
	}

	if(format == ENCODER_LZO )
		return lzo_compress_frame( av->lzo, av->in_frame,src, buf );
		
	if(format == ENCODER_YUV420 || format == ENCODER_YUV422 || format == ENCODER_YUV422F || format == ENCODER_YUV420F)
		return vj_avcodec_copy_frame( encoder,src, buf );

	if(format == ENCODER_YUV4MPEG || format == ENCODER_YUV4MPEG420 ) {
			if( in_fmt == FMT_422 ) {
					vj_yuv_put_frame( av->y4m, src );
					return ( av->width * av->height ) * 2;
			} else {
					yuv_scale_pixels_from_yuv( src,av->data,av->len, av->uv_len);
					vj_yuv_put_frame(av->y4m, av->data );
					return ( av->width * av->height * 3 / 2);
			}
	}


#ifdef __FALLBACK_LIBDV
	if(format == ENCODER_DVVIDEO || format == ENCODER_QUICKTIME_DV )
	{
		vj_dv_encoder *dv = av->dv;
		return vj_dv_encode_frame( dv,src );
	}
#endif
#if LIBAVCODEC_VERSION_MAJOR < 60
	AVFrame pict;
	veejay_memset( &pict, 0, sizeof(pict));

	pict.quality = 1;
	pict.pts = (int64_t)( (int64_t)nframe );
	pict.data[0] = src[0];
	pict.data[1] = src[1];
	pict.data[2] = src[2];
	pict.format  = av->out_frame->format;

	pict.linesize[0] = ROUND_UP_4( av->out_frame->width );
	pict.linesize[1] = ROUND_UP_4( av->out_frame->uv_width );
	pict.linesize[2] = ROUND_UP_4( av->out_frame->uv_width );


	pict.width = av->out_frame->width;
	pict.height = av->out_frame->height;

	AVPacket pkt;
	veejay_memset(&pkt,0,sizeof(pkt));
		
	return vj_avcodec_encode_video( &pkt, av->context, buf, buf_len, &pict );
#else
	av->frame->pts = (int64_t) nframe;
	av->frame->quality = FF_QP2LAMBDA * 3.0;
	
	av->frame->data[0] = src[0];
	av->frame->data[1] = src[1];
	av->frame->data[2] = src[2];

	av->frame->linesize[0] = av->out_frame->width;
	av->frame->linesize[1] = av->out_frame->uv_width;
	av->frame->linesize[2] = av->out_frame->uv_width;

	//av->frame->quality = 1;
	//av->frame->key_frame = 1;
	av->frame->pict_type = AV_PICTURE_TYPE_I;

	//av->frame->quality = 1;
	return vj_avcodec_encode_video( av->packet, av->context, buf, buf_len, av->frame );
#endif
}

