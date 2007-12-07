/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
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
#include <libsamplerec/samplerecord.h>
#include <veejay/vj-misc.h>
#include <ffmpeg/avutil.h>
#include <ffmpeg/avcodec.h>
#include <libel/vj-avcodec.h>

void	sample_reset_encoder(int sample_id);
extern int   sufficient_space(int max_size, int nframes); 


static uint8_t *sample_encoder_buf;

int sample_record_init(int len)
{
	if(len <= 0) return 0;
	if(sample_encoder_buf) free(sample_encoder_buf);
	sample_encoder_buf = (uint8_t*) malloc(sizeof(uint8_t) * len * 3);
	if(!sample_encoder_buf) return 0;
	memset(sample_encoder_buf, 0, len * 3 );
	return 1;
}

void	sample_record_free()
{
	if( sample_encoder_buf)
		free(sample_encoder_buf);
	sample_encoder_buf = NULL;
}

int sample_get_encoded_file(int sample_id, char *description)
{
    sample_info *si;
    si = sample_get(sample_id);
    if (!si)
	return -1;
    sprintf(description, "%s", si->encoder_destination
				 );
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

	if( filename == NULL )
		snprintf(si->encoder_base, 255, "Sample_%04d", sample_id);
	else
		snprintf(si->encoder_base,255,"%s",filename);
	

	int i = 0;
	int len = strlen(si->encoder_base);
	for(i=0; i <len; i ++ ) {
		if( si->encoder_base[i] == 0x20 )
			si->encoder_base[i] = '_';
	} 

	sprintf(si->encoder_destination, "%s-%05ld.avi",
		si->encoder_base,si->sequence_num);

	char ext[5];
	switch(format)
	{
		case ENCODER_DVVIDEO:
			sprintf(ext,"dv");
			break;
		case ENCODER_QUICKTIME_MJPEG:
		case ENCODER_QUICKTIME_DV:
			sprintf(ext,"mov");
			break;
		default:
			sprintf(ext,"avi");
			break;
	}
	sprintf(si->encoder_destination, "%s.%s", si->encoder_base , ext);

	veejay_msg(VEEJAY_MSG_INFO, "Recording to [%s]", si->encoder_destination);
	return (sample_update(si,sample_id));	
}


static int sample_start_encoder(sample_info *si, editlist *el, int format, long nframes)
{
	char descr[100];
	char cformat = 'Y';
	int sample_id = si->sample_id;
	switch(format)
	{
		case ENCODER_DVVIDEO: sprintf(descr,"DV2"); cformat='d'; break;
		case ENCODER_MJPEG: sprintf(descr, "MJPEG"); cformat='a'; break;
		case ENCODER_YUV420: sprintf(descr, "YUV 4:2:0 YV12"); cformat='Y'; break;
		case ENCODER_YUV422: sprintf(descr, "YUV 4:2:2 Planar"); cformat='P'; break;
		case ENCODER_MPEG4: sprintf(descr, "MPEG4"); cformat='M'; break;
		case ENCODER_DIVX: sprintf(descr, "DIVX"); cformat='D'; break;
		case ENCODER_QUICKTIME_DV:
			   sprintf(descr, "Quicktime"); cformat = 'Q'; break;

		case ENCODER_QUICKTIME_MJPEG:
			   sprintf(descr, "Quicktime"); cformat = 'q'; break;
		case ENCODER_LZO:
			   sprintf(descr, "LZO YUV"); cformat = 'L'; break;
		
		default:
		   veejay_msg(VEEJAY_MSG_ERROR, "Unsupported video codec");
		   return -1;
                break;
	}

	si->encoder = vj_avcodec_start( el, format );
	if(!si->encoder)
		return -1;


	si->encoder_active = 1;
	si->encoder_format = format;

	if(si->encoder_total_frames == 0)
	{
		si->encoder_duration = nframes; 
		si->encoder_num_frames = 0;
	}
	else
	{
		si->encoder_duration = si->encoder_duration - si->encoder_num_frames;
	}


	si->rec_total_bytes= 0;
	si->encoder_succes_frames = 0;

	if(format==ENCODER_DVVIDEO)
		si->encoder_max_size = ( el->video_height == 480 ? 120000: 144000);
	else
		switch(format)
		{
			case ENCODER_YUV420:
			 si->encoder_max_size=(el->video_height * el->video_width * 2);break;
			case ENCODER_YUV422:
			si->encoder_max_size = (el->video_width * el->video_height * 2);break;
			case ENCODER_LZO:
			si->encoder_max_size = (el->video_width * el->video_height * 3 ); break;
			default:
			si->encoder_max_size = ( 4 * 65535 );
			break;
		}
	
	si->encoder_width = el->video_width;
	si->encoder_height = el->video_height;


	if( sufficient_space( si->encoder_max_size, nframes ) == 0 )
	{
		vj_avcodec_close_encoder( si->encoder );
		si->encoder = NULL;
		si->encoder_active = 0;
		return -1;
	}

	si->encoder_file = (void*)lav_open_output_file(si->encoder_destination,cformat,
			el->video_width,el->video_height,el->video_inter,
			el->video_fps,el->audio_bits, el->audio_chans, el->audio_rate );
		
	if(!si->encoder_file)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Cannot write to %s (%s)",si->encoder_destination,
		lav_strerror());
		vj_avcodec_close_encoder( si->encoder );
		si->encoder = NULL;
		return -1;
	}

	
	veejay_msg(VEEJAY_MSG_INFO, "Encoding to %s file [%s] %dx%d@%2.2f %d/%d/%d %s >%09d< f=%c",
	    descr,
	    si->encoder_destination, 
	    el->video_width,
	    el->video_height,
	    (float) el->video_fps,
	    el->audio_bits,
	    el->audio_chans,
	    el->audio_rate,
		(el->video_inter == 1 ? "Deinterlaced" : "Interlaced"),
		( si->encoder_duration - si->encoder_total_frames),
		cformat );
	

	sample_update(si,sample_id);
	return 0;
}

int sample_init_encoder(int sample_id, char *filename, int format, editlist *el,
	long nframes) {

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
	if(format < 0 || format > 8)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid format!");
		return -1;
	}
	if(nframes <= 0) return -1;
	if(!el) return -1;

	if(si->encoder_active) {
		veejay_msg(VEEJAY_MSG_ERROR, "Sample is already encoding to [%s]",
		   si->encoder_destination);
		return -1;
	}

	if (sample_start_encoder( si , el, format, nframes ) == 0) 	
	{
		return 1;
	}

	
	return -1;
}

int sample_continue_record( int s1 )
{
	sample_info *si = sample_get(s1);
	if(!si) return -1;

	if( si->rec_total_bytes == 0) return -1;
	if(si->encoder_num_frames > si->encoder_duration)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Ready recording %d frames", si->encoder_succes_frames);

		si->encoder_total_frames = 0;
		sample_update(si,s1);
		return 1;
	}

	if ( si->rec_total_bytes  >= VEEJAY_FILE_LIMIT)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Auto splitting files (max filesize is %ld)", VEEJAY_FILE_LIMIT);
		si->sequence_num ++;
		si->rec_total_bytes = 0;

		printf(" %d %ld %ld (%ld)%ld \n",
			(int)si->sequence_num,
			si->rec_total_bytes,
			si->encoder_num_frames,
			si->encoder_total_frames,
			si->encoder_duration);

		si->encoder_total_frames = 0;	
		sample_update(si,s1);
		return 2;
	}
		
	
	return 0;
}

int sample_record_frame(int s1, uint8_t *buffer[3], uint8_t *abuff, int audio_size) {
   sample_info *si = sample_get(s1);
   int buf_len = 0;
   if(!si) return -1;

   if(!si->encoder_active)
	{
	 return -1;
  	}

   // si->encoder_format has one of ENCODER_*


   buf_len =  vj_avcodec_encode_frame(si->encoder, si->encoder_total_frames ++,
		si->encoder_format, buffer, sample_encoder_buf, si->encoder_max_size);
   if(buf_len <= 0) 
   {

  	veejay_msg(VEEJAY_MSG_ERROR, "Cannot encode frame");
	return -1;
   }
    if(lav_write_frame( (lav_file_t*) si->encoder_file,sample_encoder_buf,buf_len,1))
	{
			veejay_msg(VEEJAY_MSG_ERROR, "writing frame, giving up %s", lav_strerror());
			return 1;
	}

	si->rec_total_bytes += buf_len;

	if(audio_size > 0)
	{
		if(lav_write_audio( (lav_file_t*) si->encoder_file, (uint8_t*)abuff, audio_size))
		{
	 	    veejay_msg(VEEJAY_MSG_ERROR, "Error writing output audio [%s] (%d)",lav_strerror(),audio_size);
		}
		si->rec_total_bytes += audio_size;
	}
	/* write OK */
	si->encoder_succes_frames ++;
	si->encoder_num_frames ++;

	sample_update(si,s1);

	return (sample_continue_record(s1));
}



int sample_stop_encoder(int s1) {
   sample_info *si = sample_get(s1);
   if(!si) return -1;
   if(si->encoder_active) {
     lav_close((lav_file_t*)si->encoder_file);

	vj_avcodec_stop( si->encoder, si->encoder_format );
     
     veejay_msg(VEEJAY_MSG_INFO, "Stopped sample encoder [%s]",si->encoder_destination);
     si->encoder_active = 0;
     si->encoder_file = NULL;
     sample_update(si,s1);	
    
     //sample_reset_encoder(s1);
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
	si->encoder_succes_frames = 0;
	si->encoder_num_frames = 0;
	si->encoder_width = 0;
	si->encoder_height = 0;
	si->encoder_max_size = 0;
	si->encoder_active = 0;
	si->rec_total_bytes = 0;
	si->encoder_duration = 0;

	sample_update(si, s1);
}

int sample_get_encoded_frames(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  //return ( si->encoder_succes_frames );
  return ( si->encoder_total_frames );
}


int sample_get_total_frames( int s1 )
{
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  return ( si->encoder_total_frames );
}

int sample_reset_autosplit(int s1)
{
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  bzero( si->encoder_base, 255 );
  bzero( si->encoder_destination , 255 );
  si->encoder_total_frames = 0;
  si->sequence_num = 0;
  return (sample_update(si,s1));  
}

int sample_get_frames_left(int s1)
{
	sample_info *si= sample_get(s1);
	if(!si) return 0;
	return ( si->encoder_duration - si->encoder_total_frames );
}

int sample_encoder_active(int s1)
{
	sample_info *si = sample_get(s1);
	if(!si)return 0;
	return si->encoder_active;
}
