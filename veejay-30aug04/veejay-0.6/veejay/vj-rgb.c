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
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include "editlist.h"
#include "vj-common.h"
#include "../ccvt/ccvt.h"
/* vj_raw_alloc
  
   allocates memory to hold buffers and frame information. 
*/


extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;


vj_raw *vj_raw_alloc(EditList *el) {
	vj_raw *raw;
	raw = (vj_raw*) malloc(sizeof(vj_raw));
	if(!raw) return NULL;
	raw->size = (el->video_width * el->video_height) * 3;
	raw->line_size[0] = raw->size;
	raw->line_size[1] = 0;
	raw->line_size[2] = 0;
	raw->width = el->video_width;
	raw->height = el->video_height;
	//raw->buf = (uint8_t*)vj_malloc(sizeof(uint8_t) * raw->size );
	raw->buf =NULL;
	raw->yuv =NULL;
	raw->need_mem =1;
	return raw;
}

int	vj_raw_acquire(vj_raw *r)
{
	r->buf= (uint8_t*)vj_malloc(sizeof(uint8_t) * r->size); 
	if(!r->buf) return 0;
	memset(r->buf,0, r->size);
	r->yuv = (uint8_t*)vj_malloc(sizeof(uint8_t) * r->size);
	if(!r->yuv) return 0;
	memset(r->yuv,0,r->size);
	r->need_mem = 0;
	return 1;	

}

void	vj_raw_discard(vj_raw *r)
{
	if(r->buf) free(r->buf);
	r->buf = NULL;
	if(r->yuv) free(r->yuv);
	r->yuv = NULL;
	r->need_mem = 1;
}


void vj_raw_free(vj_raw *raw) {
	vj_raw_discard(raw);
	if(raw) free(raw);
}

/*
  vj_raw_stream_start_write

  open the file in append, create or fifo mode

*/
int vj_raw_stream_start_write(vj_raw *raw, char *file) {
	struct stat sstat;
	if(stat(file, &sstat)==0) {
         if(S_ISREG(sstat.st_mode)) {
           raw->fd = open(file, O_APPEND | O_WRONLY , 0600);
	   veejay_msg(VEEJAY_MSG_INFO, "Appending to a regular file");
	 }
	 else {
         if(S_ISFIFO(sstat.st_mode)) {
 	   raw->fd = open(file, O_WRONLY, 0600 );
	   veejay_msg(VEEJAY_MSG_INFO, "Writing to FIFO");
	  }
         }
	}
        else {
	  veejay_msg(VEEJAY_MSG_INFO, "Creating regular file %s",file);
	  raw->fd = open(file, O_CREAT | O_WRONLY , 0600);
        }

	if(!raw->fd) return -1;
	return 0;
}

/*

  vj_raw_stream_start_read

  read from file (open only)

*/

int vj_raw_stream_start_read(vj_raw *raw, char *file) {
   
  raw->fd = open( file, O_RDONLY );
  if(!raw->fd){
    veejay_msg(VEEJAY_MSG_ERROR, "Unable to open raw stream %s\n",file);
    return -1;
  }
  return 0;
}

/*

 close stream

*/
int vj_raw_stream_stop_rw(vj_raw *raw) {
   close(raw->fd);
   veejay_msg(VEEJAY_MSG_INFO, "Closed RAW stream");
   vj_raw_discard(raw);
   return 0;
}

/* just copy data to yuv buffer */
void vj_raw_any(vj_raw *raw, uint8_t *dst[3]) {
	if(raw->need_mem)
	{
		if(!vj_raw_acquire(raw)) return -1;
	}	
	veejay_memcpy( dst[0], raw->buf, (raw->width*raw->height));
	veejay_memcpy( dst[1], raw->buf + (raw->width * raw->height) , (raw->width * raw->height *5)/4);
	veejay_memcpy( dst[2], raw->buf + (raw->width * raw->height * 5) /4 ,(raw->width * raw->height)/4);
}

/* get a frame from the stream and convert it from rgb24 to ycbcr */
/* the any mode is not callable yet due to implementation of vj_tag_new */
int vj_raw_get_frame(vj_raw *raw, uint8_t *dst[3]) {
	int i;
	if(!raw->fd) return -1;
	if(raw->need_mem) 
	{
		if(!vj_raw_acquire(raw)) return -1;
	}
	i = read( raw->fd, raw->buf, raw->size);
	if( i != raw->size ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Unexpected nr. of bytes, got %d out of %d",i,raw->size);
		return -1;
	}
	switch(raw->palette) {
	  case RAW_RGB24: ccvt_rgb24_420p(raw->width,raw->height, raw->buf, dst[0],dst[1],dst[2] ); break;
	  case RAW_ANY  : vj_raw_any(raw, dst); break;
	}
	return i;
}

int vj_raw_put_frame(vj_raw *raw, uint8_t *src[3]) {
	int i;
	if(!raw->fd) return -1;

	if(raw->need_mem)
	{
		if(!vj_raw_acquire(raw)) return -1;
	}
	/* fixme clever color conversion routine takes src[0],src[1],src[2], 
	  now we must copy */
	veejay_memcpy(raw->yuv, src[0], raw->width * raw->height );
	veejay_memcpy(raw->yuv + (raw->width * raw->height), src[1], (raw->width * raw->height *5)/4);
	veejay_memcpy(raw->yuv + (raw->width * raw->height * 5) / 4, src[2],(raw->width * raw->height)/4);
	
	ccvt_420p_rgb24( raw->width, raw->height, raw->yuv, raw->buf);

	i = write( raw->fd, raw->buf, raw->size);

	return i;
}

int vj_raw_init(vj_raw *raw, int palette) {

   if(raw->palette == RAW_RGB24) {
	veejay_msg(VEEJAY_MSG_INFO, "Using RGB24 -> YUV4:2:0 YV12 conversion");
	}
   if(raw->palette == RAW_ANY) {
	veejay_msg(VEEJAY_MSG_INFO, "Interpreting data as YUV 4:2:0 YV12");
	}
   raw->palette = palette;
   return 0;
}

