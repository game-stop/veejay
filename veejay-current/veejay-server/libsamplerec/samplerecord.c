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
#include <string.h>
#include <libsample/sampleadm.h>
#include <libvjmsg/vj-msg.h>
#include <libsubsample/subsample.h>
#include <libsamplerec/samplerecord.h>
#include <veejay/vj-misc.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libel/vj-avcodec.h>
#include <libvjmem/vjmem.h>
void	sample_reset_encoder(int sample_id);
extern int   sufficient_space(int max_size, int nframes); 


int sample_record_init(int len)
{
	if(len <= 0) return 0;
	return 1;
}

void	sample_record_free()
{
}

int sample_get_encoded_file(int sample_id, char *description)
{
    sample_info *si;
    si = sample_get(sample_id);
    if (!si)
	return -1;
    sprintf(description, "%s", si->encoder_destination	 );
    return 1;
}

int sample_get_num_encoded_files(int sample_id)
{
	sample_info *si;
	si = sample_get(sample_id);
	if(!si) return -1;
	return si->sequence_num;
}

int sample_get_sequenced_file(int sample_id, char *descr, int num, char *ext)
{
    sample_info *si;
    si = sample_get(sample_id);
    if (!si)
	return -1;

    	
    
    sprintf(descr, "%s-%05d.%s", si->encoder_destination,
				   num, ext);
    return 1;

}

int sample_get_encoder_format(int sample_id)
{
	sample_info *si;
	si = sample_get(sample_id);
	if(!si) return -1;
	return si->encoder_format;
}

int sample_try_filename(int sample_id, char *filename, int format)
{
	sample_info *si= sample_get(sample_id);
	if(!si) return 0;

	char tmp[32];
	if( filename  == NULL )
		snprintf(tmp,32, "Sample_%04d", sample_id );
	else
		snprintf(tmp,32, "%s", filename );
	
	int i = 0;
	int len = strlen(tmp);
	for(i=0; i <len; i ++ ) {
		if( tmp[i] == 0x20 )
			tmp[i] = '_';
	} 

	char ext[5];
	switch(format)
	{
		case ENCODER_DVVIDEO:
			sprintf(ext,"dv");
			break;
		case ENCODER_YUV4MPEG:
			sprintf(ext,"yuv");
			break;
		case ENCODER_QUICKTIME_MJPEG:
		case ENCODER_QUICKTIME_DV:
			sprintf(ext,"mov");
			break;
		default:
			sprintf(ext,"avi");
			break;
	}

	int est_len = len + len + 4 + 1 + 3 + 1 + 1;
	if(si->encoder_destination) {
		free(si->encoder_destination);
		si->encoder_destination = NULL;
	}
	si->encoder_destination = (char*) vj_malloc( sizeof(char) * est_len );
	snprintf( si->encoder_destination, est_len,"%s-%04d.%s", tmp, (int) si->sequence_num, ext);

	veejay_msg(VEEJAY_MSG_INFO, "Recording to [%s]", si->encoder_destination);
	return 1;	
}


static int sample_start_encoder(sample_info *si, VJFrame *frame, editlist *el, int format, long nframes)
{
	char cformat = vj_avcodec_find_lav( format );
	
	if( cformat == '\0' ) 
		return -1;

	si->encoder = vj_avcodec_start( frame, format, si->encoder_destination );
	if(!si->encoder)
		return -1;


	si->encoder_active = 1;
	si->encoder_format = format;

	if( si->encoder_total_frames_recorded == 0 ) {
   	 si->encoder_frames_to_record = nframes;
   	 si->encoder_frames_recorded  = 0;
    }
	else {
	 si->encoder_frames_recorded = 0;
	}

	int tmp = frame->len;
	int tmp1 = frame->uv_len;

	switch(format)
	{
		case ENCODER_YUV420:
		case ENCODER_YUV420F:
		 si->encoder_max_size= 2048 + tmp + (tmp/4) + (tmp/4);break;
		case ENCODER_YUV422:
		case ENCODER_YUV422F:
		case ENCODER_YUV4MPEG:
		si->encoder_max_size = 2048 + tmp + tmp1 + tmp1;break;
		case ENCODER_LZO:
		si->encoder_max_size = (tmp * 3 ); break;
		case ENCODER_DVVIDEO:
		si->encoder_max_size = ( frame->height == 480 ? 120000: 144000); break;
		default:
		si->encoder_max_size = 8 * ( 16 * 65535 );
		break;
	}
	
	si->encoder_width = frame->width;
	si->encoder_height =  frame->height;


	if( sufficient_space( si->encoder_max_size, nframes ) == 0 )
	{
		vj_avcodec_close_encoder( si->encoder );
		si->encoder = NULL;
		si->encoder_active = 0;
		return -1;
	}

	if( cformat != 'S' ) {

		si->encoder_file = (void*)lav_open_output_file(si->encoder_destination,cformat,
			frame->width,frame->height,0,frame->fps,el->audio_bits, el->audio_chans, el->audio_rate );
		
		if(!si->encoder_file)
		{
			veejay_msg(VEEJAY_MSG_ERROR,"Cannot write to %s (%s)",si->encoder_destination,
			lav_strerror());
			vj_avcodec_close_encoder( si->encoder );
			si->encoder = NULL;
			return -1;
		}

	}
	veejay_msg(VEEJAY_MSG_INFO, "Encoding to %s file [%s]",  vj_avcodec_get_encoder_name(format),
	    si->encoder_destination );

	return 0;
}

int sample_init_encoder(int sample_id, char *filename, int format, VJFrame *frame, editlist *el, long nframes) {

	sample_info *si;

	if(! sample_try_filename( sample_id, filename,format ) )
	{
		return -1;
	}  

	si  = sample_get(sample_id);
	if(!si)
	{
		 return -1; 
	}
	if(format < 0 || format > NUM_ENCODERS)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid format!");
		return -1;
	}
	if(nframes <= 0) return -1;
	if(!el || !frame) return -1;

	if(si->encoder_active) {
		veejay_msg(VEEJAY_MSG_ERROR, "Sample is already encoding to [%s]",
		   si->encoder_destination);
		return -1;
	}

	if (sample_start_encoder( si , frame, el, format, nframes ) == 0) 	
	{
		return 1;
	}

	
	return -1;
}

int sample_continue_record( int s1 )
{
	sample_info *si = sample_get(s1);
	if(!si) return -1;

	long	bytesRemaining = lav_bytes_remain( si->encoder_file );
	if( bytesRemaining >= 0 && bytesRemaining < (512 * 1024) ) {
		si->sequence_num ++;
		veejay_msg(VEEJAY_MSG_WARNING, "Auto splitting file, %ld frames left to record.", 
			( si->encoder_frames_to_record - si->encoder_total_frames_recorded ) );
		si->encoder_frames_recorded= 0;	
		return 2;
	}

	if( si->encoder_total_frames_recorded >= si->encoder_frames_to_record ) {
		veejay_msg(VEEJAY_MSG_INFO, "Recorded %ld frames", si->encoder_total_frames_recorded );
		return 1;
	}

	return 0;
}

int sample_record_frame(int s1, uint8_t *buffer[4], uint8_t *abuff, int audio_size, int pix_fmt) {
   sample_info *si = sample_get(s1);
   int buf_len = 0;
   if(!si) return -1;

   if(!si->encoder_active) {
		return -1;
   }

   long nframe = si->encoder_frames_recorded;

   buf_len =  vj_avcodec_encode_frame(si->encoder,  nframe,
		si->encoder_format, buffer, vj_avcodec_get_buf(si->encoder), si->encoder_max_size, pix_fmt);

   if(buf_len <= 0) 
   {

  	veejay_msg(VEEJAY_MSG_ERROR, "Cannot encode frame");
	return -1;
   }
//	si->rec_total_bytes += buf_len;


   //@ if writing to AVI/QT
   if( si->encoder_file != NULL ) {

    if(lav_write_frame( (lav_file_t*) si->encoder_file,vj_avcodec_get_buf(si->encoder),buf_len,1))
	{
			veejay_msg(VEEJAY_MSG_ERROR, "writing frame, giving up: '%s' (%d bytes buffer)", lav_strerror(),
					buf_len);
			return 1;
	}

	if(audio_size > 0)
	{
		if(lav_write_audio( (lav_file_t*) si->encoder_file, (uint8_t*)abuff, audio_size))
		{
	 	    veejay_msg(VEEJAY_MSG_ERROR, "Error writing output audio [%s] (%d)",lav_strerror(),audio_size);
		}
	}
   }

	si->encoder_frames_recorded ++;
    si->encoder_total_frames_recorded ++; 

	return (sample_continue_record(s1));
}



int sample_stop_encoder(int s1) {
   sample_info *si = sample_get(s1);
   if(!si) return -1;
   if(si->encoder_active) {
	if(si->encoder_file)
   	  lav_close((lav_file_t*)si->encoder_file);
	if( si->encoder)
		vj_avcodec_stop( si->encoder, si->encoder_format );
     
     veejay_msg(VEEJAY_MSG_INFO, "Stopped sample encoder [%s]",si->encoder_destination);
     si->encoder_active = 0;
     si->encoder_file = NULL;
     si->encoder = NULL;
     return 1; 
  }
   return 0;
}


void sample_reset_encoder(int s1) {
	sample_info *si = sample_get(s1);
	if(!si) return;
	  /* added sample */
 	si->encoder_active = 0;
	si->encoder_format = 0;
	si->encoder_width = 0;
	si->encoder_height = 0;
	si->encoder_max_size = 0;
	si->encoder_active = 0;
//	si->rec_total_bytes = 0;
	si->encoder_file = NULL;
	si->encoder = NULL;
	si->encoder_total_frames_recorded = 0;
	si->encoder_frames_recorded = 0;
	si->encoder_frames_to_record =0;
}


int sample_get_encoded_frames(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  //return ( si->encoder_succes_frames );
  return ( si->encoder_total_frames_recorded );
}


long sample_get_total_frames( int s1 )
{
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  return ( si->encoder_frames_to_record );
}

int sample_reset_autosplit(int s1)
{
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  si->sequence_num = 0;
  return 1;
}

long sample_get_frames_left(int s1)
{
	sample_info *si= sample_get(s1);
	if(!si) return 0;

	return ( si->encoder_frames_to_record - 
		     si->encoder_total_frames_recorded );
}

int sample_encoder_active(int s1)
{
	sample_info *si = sample_get(s1);
	if(!si)return 0;
	return si->encoder_active;
}
