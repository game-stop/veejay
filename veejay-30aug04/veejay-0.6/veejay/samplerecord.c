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
#include <veejay/sampleadm.h>
#include <veejay/vj-common.h>
#include <veejay/vj-ffmpeg.h>
#include <veejay/editlist.h>
extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;

void	clip_reset_encoder(int clip_id);

static uint8_t *clip_encoder_buf;

int clip_record_init(int len)
{
	if(len <= 0) return 0;
	if(clip_encoder_buf) free(clip_encoder_buf);
	clip_encoder_buf = (uint8_t*) malloc(sizeof(uint8_t) * len * 3);
	if(!clip_encoder_buf) return 0;
	memset(clip_encoder_buf, 0, len * 3 );
	return 1;
}

int clip_get_encoded_file(int clip_id, char *description)
{
    clip_info *si;
    si = clip_get(clip_id);
    if (!si)
	return -1;
    sprintf(description, "%s", si->encoder_destination
				 );
    return 1;
}

int clip_get_num_encoded_files(int clip_id)
{
	clip_info *si;
	si = clip_get(clip_id);
	if(!si) return -1;
	return si->sequence_num;
}

int clip_get_sequenced_file(int clip_id, char *descr, int num)
{
    clip_info *si;
    si = clip_get(clip_id);
    if (!si)
	return -1;
    sprintf(descr, "%s-%05d.avi", si->encoder_destination,
				   num);
    return 1;

}

int clip_get_encoder_format(int clip_id)
{
	clip_info *si;
	si = clip_get(clip_id);
	if(!si) return -1;
	return si->encoder_format;
}

int clip_try_filename(int clip_id, char *filename)
{
	clip_info *si= clip_get(clip_id);
	if(!si) return 0;
	
	if(filename != NULL)
	{
		snprintf(si->encoder_base,255,"%s",filename);
	}
	sprintf(si->encoder_destination, "%s-%05d.avi", si->encoder_base,si->sequence_num);

	veejay_msg(VEEJAY_MSG_INFO, "Recording to [%s]", si->encoder_destination);
	return (clip_update(si,clip_id));	
}


static int clip_start_encoder(clip_info *si, EditList *el, int format, long nframes)
{
	char descr[100];
	char cformat = 'Y';
	int clip_id = si->clip_id;
	switch(format)
	{
		case DATAFORMAT_DV2: sprintf(descr,"DV2"); cformat='d'; break;
		case DATAFORMAT_MJPG: sprintf(descr, "MJPEG"); cformat='a'; break;
		case DATAFORMAT_YUV420: sprintf(descr, "YUV 4:2:0 YV12"); cformat='Y'; break;
		case DATAFORMAT_MPEG4: sprintf(descr, "MPEG4"); cformat='M'; break;
		case DATAFORMAT_DIVX: sprintf(descr, "DIVX"); cformat='D'; break;
		default:
		   veejay_msg(VEEJAY_MSG_ERROR, "Unsupported video codec");
		   return -1;
                break;
	}

	if(!el->has_audio && si->external_wave==1)
	{
		veejay_msg(VEEJAY_MSG_WARNING,"EditList has no Audio, taking properties from external WAV file");
		si->encoder_file = lav_open_output_file(
					si->encoder_destination,
					cformat,
					el->video_width,
					el->video_height,
					(el_auto_deinter(el) == 1 ? 0: el->video_inter),
					el->video_fps,
					si->wav->bits,
					si->wav->channels,
					si->wav->rate); 
		if(si->encoder_file)
		veejay_msg(VEEJAY_MSG_INFO, "Encoding to %s file [%s] %dx%d@%2.2f %d/%d/%d %s >%09d<",
		    descr,
		    si->encoder_destination, 
		    el->video_width,
		    el->video_height,
		    (float) el->video_fps,
		    si->wav->bits,
		    si->wav->channels,
		    si->wav->rate,
			( el_auto_deinter(el)==1 ? "Deinterlaced" : "Interlaced"),
			( si->encoder_duration - si->encoder_total_frames)
		);


	}
	else 
	{
		si->encoder_file = lav_open_output_file(si->encoder_destination,cformat,
			el->video_width,el->video_height,(el_auto_deinter(el) == 1 ? 0: el->video_inter),
			el->video_fps,el->audio_bits,el->audio_chans,el->audio_rate);
		if(si->encoder_file)
		veejay_msg(VEEJAY_MSG_INFO, "Encoding to %s file [%s] %dx%d@%2.2f %d/%d/%d %s >%09d<",
		    descr,
		    si->encoder_destination, 
		    el->video_width,
		    el->video_height,
		    (float) el->video_fps,
		    el->audio_bits,
		    el->audio_chans,
		    el->audio_rate,
			(el_auto_deinter(el) == 1 ? "Deinterlaced" : "Interlaced"),
			( si->encoder_duration - si->encoder_total_frames)
		);


	}

	if(!si->encoder_file)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Cannot write to %s (%s)",si->encoder_destination,
		lav_strerror());
		return -1;
	}


	si->encoder_active = 1;
	si->encoder_format = format;

	if(si->encoder_total_frames == 0)
	{
		si->encoder_duration = nframes +1; 
		si->encoder_num_frames = 0;
	}
	else
	{
		si->encoder_duration = si->encoder_duration - si->encoder_num_frames;
	}


	si->rec_total_bytes= 0;
	si->encoder_succes_frames = 0;

	if(format==DATAFORMAT_DV2)
		si->encoder_max_size = ( el->video_height == 480 ? 120000: 144000);
	else
		if(format==DATAFORMAT_YUV420)
		{
			si->encoder_max_size=0;
		}
		else
		{
			si->encoder_max_size = ( 4 * 65535 );
		}
	
	si->encoder_width = el->video_width;
	si->encoder_height = el->video_height;

	if(clip_update(si,clip_id)==0) return 1;
	return 0;
}

int clip_init_encoder(int clip_id, char *filename, int format, EditList *el,
	long nframes) {

	int res = -1;
	clip_info *si;

	if(! clip_try_filename( clip_id, filename ) )
	{
		return -1;
	}  

	si  = clip_get(clip_id);
	if(!si)
	{
		 return -1; 
	}
	if(format < 0 || format > 5)
	{
		return -1;
	}
	if(nframes <= 0) return -1;
	if(!el) return -1;

	if(si->encoder_active) {
		veejay_msg(VEEJAY_MSG_ERROR, "Clip is already encoding to [%s]",
		   si->encoder_destination);
		return -1;
	}

	if (clip_start_encoder( si , el, format, nframes ) == 0) 	
	{
		return 1;
	}

	
	return -1;
}

int clip_continue_record( int s1 )
{
	clip_info *si = clip_get(s1);
	if(!si) return -1;

	if( si->rec_total_bytes == 0) return -1;
	if(si->encoder_num_frames >= si->encoder_duration)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Ready recording %d frames", si->encoder_succes_frames);

		return 1;
	}

	// 2 GB barrier
	if (( si->rec_total_bytes / 1048576)  >= VEEJAY_FILE_LIMIT)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Auto splitting files (reached internal 2GB barrier see vj-common.h)");
		si->sequence_num ++;
		si->rec_total_bytes = 0;

		printf(" %d %ld %d (%d)%d \n",
			(int)si->sequence_num,
			si->rec_total_bytes,
			si->encoder_num_frames,
			si->encoder_total_frames,
			si->encoder_duration);

	
		clip_update(si,s1);
		return 2;
	}
		
	
	return 0;
}

int clip_record_frame(vj_ffmpeg *encoder, int s1, uint8_t *buffer[3], uint8_t *abuff, int audio_size) {
   clip_info *si = clip_get(s1);
   int buf_len = 0;
   if(!si) return -1;


   if(!si->encoder_active)
	{
	 return -1;
  	}

	switch(si->encoder_format)
	{
		case DATAFORMAT_MJPG:
		case DATAFORMAT_MPEG4:
		case DATAFORMAT_DIVX:
			buf_len = vj_ffmpeg_encode_frame(encoder,buffer,clip_encoder_buf,si->encoder_max_size);
			if(buf_len > 0)
			{
				if(lav_write_frame(si->encoder_file,clip_encoder_buf,buf_len,1))
				{
					veejay_msg(VEEJAY_MSG_ERROR, "writing frame, giving up %s", lav_strerror());
					return 1;
				}
				si->rec_total_bytes += buf_len;
			}
			break;
#ifdef SUPPORT_READ_DV2
		case DATAFORMAT_DV2:
			buf_len = vj_dv_encode_frame( buffer, clip_encoder_buf );
			if(lav_write_frame( si->encoder_file, clip_encoder_buf, buf_len, 1))
			{
				veejay_msg(VEEJAY_MSG_ERROR, "writing frame , giving up: %s", lav_strerror());
				return 1;
			}
			si->rec_total_bytes += ( si->encoder_width * si->encoder_height * 2 );
			break;
#endif
		case DATAFORMAT_YUV420:
	
			veejay_memcpy(clip_encoder_buf, 
				buffer[0], 
				si->encoder_width*si->encoder_height );
			veejay_memcpy(clip_encoder_buf+(si->encoder_width*si->encoder_height),
				buffer[1],
				si->encoder_width*si->encoder_height/4);
			veejay_memcpy(clip_encoder_buf+((si->encoder_width*si->encoder_height*5)/4),
				buffer[2],
				si->encoder_width*si->encoder_height/4);
			buf_len = (si->encoder_width*si->encoder_height) + ((si->encoder_width*si->encoder_height)/2);
			if(lav_write_frame( si->encoder_file, clip_encoder_buf, buf_len, 1))
			{
				veejay_msg(VEEJAY_MSG_ERROR, "writing frame , giving up: %s", lav_strerror());
				return 1;
			}
			si->rec_total_bytes += ( si->encoder_width * si->encoder_height * 2 );
			break;
	}

	if(audio_size > 0)
	{
		if(lav_write_audio(si->encoder_file, abuff, audio_size))
		{
	 	    veejay_msg(VEEJAY_MSG_ERROR, "Error writing output audio [%s]",lav_strerror());
		}
		// estimate samples , dont care
		si->rec_total_bytes += ( audio_size * 4 );
	}
	/* write OK */
	si->encoder_succes_frames ++;
	si->encoder_num_frames ++;
/*
	veejay_msg(VEEJAY_MSG_INFO, "%d Total bytes %d (%2.2f mb , %2.2f gb ) seq nr %d du %d tf %d", 
		si->encoder_num_frames,
		si->rec_total_bytes, (float)(si->rec_total_bytes/1048576.0),
			(float)(si->rec_total_bytes/(float)(1048576*1024)),
		si->sequence_num,si->encoder_duration, si->encoder_total_frames );
*/
	si->encoder_total_frames ++;

	//fixmE	
	clip_update(si,s1);

	return (clip_continue_record(s1));
}



int clip_stop_encoder(int s1) {
   clip_info *si = clip_get(s1);
   if(!si) return -1;
   if(si->encoder_active) {
     lav_close(si->encoder_file);
     veejay_msg(VEEJAY_MSG_INFO, "Stopped clip encoder [%s]",si->encoder_destination);
     si->encoder_active = 0;
     clip_update(si,s1);	
     //clip_reset_encoder(s1);
     return 1; 
  }
   return 0;
}


void clip_reset_encoder(int s1) {
	clip_info *si = clip_get(s1);
	if(!si) return;
	  /* added clip */
 	si->encoder_active = 0;
	si->encoder_format = 0;
	si->encoder_succes_frames = 0;
	si->encoder_num_frames = 0;
	si->encoder_width = 0;
	si->encoder_height = 0;
	si->encoder_max_size = 0;
	si->encoder_active = 0;
	si->rec_total_bytes = 0;
	clip_update(si, s1);
}

int clip_get_encoded_frames(int s1) {
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  //return ( si->encoder_succes_frames );
  return ( si->encoder_total_frames );
}


int clip_get_total_frames( int s1 )
{
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  return ( si->encoder_total_frames );
}

int clip_reset_autosplit(int s1)
{
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  bzero( si->encoder_base, 255 );
  bzero( si->encoder_destination , 255 );
  si->encoder_total_frames = 0;
  si->sequence_num = 0;
  return (clip_update(si,s1));  
}

int clip_get_frames_left(int s1)
{
	clip_info *si= clip_get(s1);
	if(!si) return 0;
	return ( si->encoder_duration - si->encoder_total_frames );
}

int clip_encoder_active(int s1)
{
	clip_info *si = clip_get(s1);
	if(!si)return 0;
	return si->encoder_active;
}
