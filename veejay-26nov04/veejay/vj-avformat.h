
#ifndef VJ_AV_FORMAT_H
#define VJ_AV_FORMAT_H
#include "avcodec.h"
#include "avformat.h"

typedef struct vj_avformat_t
{
	AVFormatContext *context;
	AVInputFormat 	*av_input_format;
	AVImageFormat	*image_format;
	AVFormatParameters *av_format_par;
	AVStream	*stream;
	AVStream	*audio_stream;
	AVCodecContext  *cct;   
	AVFrame		*frame;
	AVCodec		*codec;
	AVCodec		*audiocodec;
	AVStream	*audiostream;
	AVCodecContext  *audiocct;
	int		seekable;
	int		video_index;
	int		audio_index;
	int64_t		start_time;
	long		time_unit;
	int64_t		requested_timecode;
	int64_t		expected_timecode;
	int64_t		current_video_pts;
	int64_t		current_video_pts_time;
	int64_t		video_clock;
	int64_t		video_last_P_pts;
	int64_t 	previous_frame;
	int		frame_rate_base;
	int		frame_rate;
} vj_avformat;

void	vj_avformat_init();

vj_avformat *vj_avformat_open_input(const char *filename);

void	vj_avformat_close_input( vj_avformat *av );

int	vj_avformat_get_video_frame( vj_avformat *av, uint8_t *yuv420[3], long nframe, int pix_fmt );

int	vj_avformat_get_audio( vj_avformat *av, uint8_t *dst, long nframe );

int	vj_avformat_get_video_pixfmt(vj_avformat *av);

int	vj_avformat_get_video_codec(vj_avformat *av);

int 	vj_avformat_get_video_gop_size(vj_avformat *av);

int	vj_avformat_get_video_width( vj_avformat *av );

int	vj_avformat_get_video_height( vj_avformat *av );

int	vj_avformat_get_video_inter( vj_avformat *av );

float	vj_avformat_get_video_fps( vj_avformat *av  );

long	vj_avformat_get_video_frames( vj_avformat *av );

int	vj_avformat_get_video_inter( vj_avformat *av );

int	vj_avformat_get_audio_rate(vj_avformat *av);

int	vj_avformat_get_audio_channels(vj_avformat *av);	
#endif



