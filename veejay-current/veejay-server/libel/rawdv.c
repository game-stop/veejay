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
#include <sys/types.h>
#ifdef SUPPORT_READ_DV2
#include <libel/rawdv.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libel/vj-mmap.h>
#include <libdv/dv.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>

#include <errno.h>
#define	DV_PAL_SIZE 144000
#define DV_NTSC_SIZE 120000

static void	rawdv_free(dv_t *dv)
{
	if(dv->filename) free(dv->filename);
	if(dv->buf) free(dv->buf);
	if(dv) free(dv);
}

int	rawdv_close(dv_t *dv)
{
	close(dv->fd);
	mmap_free(dv->mmap_region);
	rawdv_free( dv);
	return 1;
}

int	rawdv_sampling(dv_t *dv)
{
	switch(dv->fmt)
	{
		case e_dv_sample_411: return 4;
		case e_dv_sample_422: return 2;
		case e_dv_sample_420: return 1;
	}
	return 3;
}

#define DV_HEADER_SIZE 120000
dv_t	*rawdv_open_input_file(const char *filename, int mmap_size)
{
	dv_t *dv = (dv_t*) vj_malloc(sizeof(dv_t));
	if(!dv) return NULL;
	memset(dv, 0, sizeof(dv_t));
	dv_decoder_t *decoder = NULL;

	uint8_t *tmp = (uint8_t*) vj_malloc(sizeof(uint8_t) * DV_HEADER_SIZE);
	memset( tmp, 0, sizeof(uint8_t) * DV_HEADER_SIZE);
	off_t file_size = 0;
	int n = 0;

	decoder = dv_decoder_new( 1,0,0);
	dv->fd = open( filename, O_RDONLY );
	
	if(!dv->fd)
	{
		dv_decoder_free(decoder); 
		rawdv_free(dv);
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to open file '%s'",filename);
		if(tmp)free(tmp);
		return NULL;
	}
	/* fseek sometimes lies about filesize - seek to end (lseek returns file offset from start)*/
	file_size = lseek( dv->fd, 0, SEEK_END );
	if( file_size < DV_HEADER_SIZE)
	{
		dv_decoder_free(decoder);
		veejay_msg(VEEJAY_MSG_ERROR, "File %s is not a DV file", filename);
		rawdv_free(dv);
		if(tmp) free(tmp);
		return NULL;
	}
	/* And back to start offset */
	if( lseek(dv->fd,0, SEEK_SET ) < 0)
	{
		dv_decoder_free(decoder);
		veejay_msg(VEEJAY_MSG_ERROR, "Seek error in %s", filename);
		rawdv_free(dv);
		if(tmp) free(tmp);
		return NULL;
	}

	dv->mmap_region = NULL;
	if( mmap_size > 0 ) // user wants mmap
	{
		dv->mmap_region = mmap_file( dv->fd, 0, (mmap_size * 720 * 576 * 3),
			file_size );
	}

	if( dv->mmap_region == NULL )
	{
		if(mmap_size>0)
			veejay_msg(VEEJAY_MSG_DEBUG, "Mmap of DV file failed - fallback to read");
		n = read( dv->fd, tmp, DV_HEADER_SIZE );
	}
	else
	{
		n = mmap_read( dv->mmap_region, 0, DV_HEADER_SIZE, tmp );
	}

	if( n <= 0 )
	{
		dv_decoder_free(decoder);
		rawdv_free(dv);
		if(tmp) free(tmp);
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot read from '%s'", filename);
		return NULL;
	}

	if( dv_parse_header( decoder, tmp) < 0 )
	{
		dv_decoder_free( decoder );
		rawdv_free(dv);
		if(tmp) free(tmp);
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot parse header of file %s", filename);
		return NULL;
	}
/*	if(decoder->sampling == e_dv_sample_411)
	{
		dv_decoder_free( decoder );
		rawdv_free(dv);
		if(tmp) free(tmp);
		return NULL;
	}*/


	if(dv_is_PAL( decoder ) )
		dv->chunk_size = DV_PAL_SIZE;
	else
		dv->chunk_size = DV_NTSC_SIZE;

	dv->width = decoder->width;
	dv->height = decoder->height;
	dv->audio_rate = decoder->audio->frequency;
	dv->audio_chans = decoder->audio->num_channels;
	dv->audio_qbytes = decoder->audio->quantization;
	dv->fps = ( dv_is_PAL( decoder) ? 25.0 : 29.97 );
	dv->size = decoder->frame_size;
	dv->num_frames = (file_size - DV_HEADER_SIZE) / dv->size;
	dv->fmt = decoder->sampling;	
//	dv->fmt = ( decoder->sampling == e_dv_sample_422 ? 1 : 0);
	dv->buf = (uint8_t*) vj_malloc(sizeof(uint8_t*) * dv->size);
	dv->offset = 0;

	veejay_msg(VEEJAY_MSG_DEBUG,
			"DV properties %d x %d, %f, %d frames, %d sampling",
			dv->width,dv->height, dv->fps, dv->num_frames,
			dv->fmt );
	
	dv_decoder_free( decoder );

	if(tmp)
		free(tmp);

/*	if(dv->audio_rate)
	{
		int i;
		for( i = 0; i < 4; i ++ )
		dv->audio_buffers[i] = (int16_t*) vj_malloc(sizeof(int16_t) * 2 * DV_AUDIO_MAX_SAMPLES);
	}*/

/*
	veejay_msg(VEEJAY_MSG_DEBUG,
		"rawDV: num frames %ld, dimensions %d x %d, at %2.2f in %s",
		dv->num_frames,
		dv->width,
		dv->height,
		dv->fps,
		(dv->fmt==1?"422":"420"));
	veejay_msg(VEEJAY_MSG_DEBUG,
		"rawDV: frame size %d, rate %d, channels %d, bits %d",
		dv->size,
		dv->audio_rate,
		dv->audio_chans,
		dv->audio_qbytes);*/

	return dv;
}


int	rawdv_set_position(dv_t *dv, long nframe)
{
	off_t offset = nframe * dv->size;

	if(nframe <= 0 )
		offset = 0;
	else
		if(nframe > dv->num_frames) 
			offset = dv->num_frames * dv->size;

	dv->offset = offset;

	if( lseek( dv->fd, offset, SEEK_SET ) < 0 )
		return -1;
	return 0;
}

int	rawdv_read_frame(dv_t *dv, uint8_t *buf )
{
	int n = 0;
	if(dv->mmap_region == NULL)
	{
		n = read( dv->fd, dv->buf, dv->size );
		memcpy( buf, dv->buf, dv->size );
	}
	else
	{
		n = mmap_read( dv->mmap_region, dv->offset, dv->size, buf );
	}
	return n;
}

int	rawdv_read_audio_frame(dv_t *dv, uint8_t *audio )
{

	return 0;
/*
	int n = dv_decode_full_audio( dv->decoder, dv->buf, dv->audio_buffers );
	// interleave buffers to audio
	int n_samples = dv_get_num_samples( dv->decoder );
	int n_chans   = dv->audio_chans;
	int16_t *ch0 = dv->audio_buffers[0];
	int16_t *ch1 = dv->audio_buffers[1];
	int i,j;
	for( i = 0; i < n_samples; i ++ )
	{
		*(audio) = *(ch0)  & 0xff;
		*(audio+1) =  (*(ch0) << 8) & 0xff;
		*(audio+2) =  *(ch1) & 0xff;
		*(audio+3) = (*(ch1)<<8) & 0xff;
		*(ch0) ++;
		*(ch1) ++;
		*(audio) += 4;
	}
	return n_samples * 4;
*/
}

int	rawdv_video_frames(dv_t *dv)
{
	return dv->num_frames;
}

int	rawdv_width(dv_t *dv)
{
	return dv->width;
}

int	rawdv_height(dv_t *dv)
{
	return dv->height;
}

double	rawdv_fps(dv_t *dv)
{
	return (double) dv->fps;
}

int	rawdv_compressor(dv_t *dv)
{
	return CODEC_ID_DVVIDEO;
}

char 	*rawdv_video_compressor(dv_t *dv)
{
	char res[5] = "dvsd\0";
	return res;
}

int	rawdv_audio_channels(dv_t *dv)
{
	return dv->audio_chans;
}

int	rawdv_audio_bits(dv_t *dv)
{
	return dv->audio_qbytes;
}	

int	rawdv_audio_format(dv_t *dv)
{
	// pcm wave format
	return (0x0001);
}

int	rawdv_audio_rate(dv_t *dv)
{
	return dv->audio_rate;
}

int	rawdv_audio_bps(dv_t *dv)
{
	return 4;
}

int	rawdv_frame_size(dv_t *dv)
{
	return dv->size;
}

int	rawdv_interlacing(dv_t *dv)
{
	return 0;
}
#endif
