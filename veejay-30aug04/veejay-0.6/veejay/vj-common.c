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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/time.h>
#include <stdarg.h>
#include <string.h>
#include "veejay/jpegutils.h"
#include "vj-lib.h"
#include "vj-common.h"
//#include "mjpeg_logging.h"

#ifdef CAN_COMPILE_X86_ASM

void mymemset_generic(void * s, char c,size_t count)
{
int d0, d1;
__asm__ __volatile__(
	"rep\n\t"
	"stosb"
	: "=&c" (d0), "=&D" (d1)
	:"a" (c),"1" (s),"0" (count)
	:"memory");
}
#else
void mymemset_generic(void *s, char c, size_t count) {
	if(count<=0) return;
	memset(s,c,count);
}
#endif

static unsigned int vj_relative_time = 0;

static unsigned int vj_get_timer()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((tv.tv_sec & 1000000) + tv.tv_usec);
}
/*
static unsigned int vj_get_timer_ms()
{
    struct timeval tv;
    struct timezone tz;
    gettimeofday(&tv, &tz);
    return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}
*/
float vj_get_relative_time()
{
    long int time, relative;
    time = vj_get_timer();
    relative = time - vj_relative_time;
    vj_relative_time = time;
    //fprintf(stderr, "relative_time: %d, %d, %f\n",
    //      time,relative,(relative*0.000001F));
    if (relative < 0)
	relative = 0;		/* cant keep counter increasing forever */
    return (float) relative *0.000001F;
}

int vj_perform_take_bg(veejay_t *info, uint8_t **src)
{
		
	veejay_msg(VEEJAY_MSG_INFO, "Warning: taking current frame %d as static bg (%p)",info->settings->current_frame_num, src[0]);
	diff_set_background( src, info->editlist->video_width, info->editlist->video_height );
	return 1;
}

int vj_perform_screenshot2(veejay_t * info, uint8_t ** src)
{
    FILE *frame;
    int res = 0;
    char filename[15];
    uint8_t *jpeg_buff;
    int jpeg_size;

    video_playback_setup *settings = info->settings;

    jpeg_buff = (uint8_t *) malloc( 65535 * 4);
    if (!jpeg_buff)
	return -1;

    //vj_perform_get_primary_frame(info, src, settings->currently_processed_frame);

    sprintf(filename, "screenshot-%08d.jpg", settings->current_frame_num);

    frame = fopen(filename, "wb");

    if (!frame)
	return -1;

    jpeg_size = encode_jpeg_raw(jpeg_buff, (65535*4), 100,
				settings->dct_method,  
				info->editlist->video_inter, 0,
				info->editlist->video_width,
				info->editlist->video_height, src[0],
				src[1], src[2]);

    res = fwrite(jpeg_buff, jpeg_size, 1, frame);
    fclose(frame);
    if(res) veejay_msg(VEEJAY_MSG_INFO, "Dumped frame to %s", filename);
    if (jpeg_buff)
	free(jpeg_buff);

    return res;
}


static unsigned long _pe_total = 0;
void *vj_malloc(unsigned int size)
{

	void *ptr;

#if defined (HAVE_MEMALIGN)
//	ptr = memalign( 16, size );
 	posix_memalign( ptr, 16, size );
#else	
	ptr = malloc ( size ) ;
#endif
	if(!ptr)
        {
		veejay_msg(VEEJAY_MSG_ERROR, "allocating %d bytes of memory ",size);
        }

	_pe_total += size;
	return ptr;
}

int veejay_get_total_mem()
{
	return _pe_total;
}

/******************************************************
 * veejay_msg()
 *   simplicity function which will give messages
 ******************************************************/
void veejay_msg(int type, const char format[], ...) GNUC_PRINTF(2, 3);

#define TXT_RED		"\033[0;31m"
#define TXT_RED_B 	"\033[1;31m"
#define TXT_GRE		"\033[0;32m"
#define TXT_GRE_B	"\033[1;32m"
#define TXT_YEL		"\033[0;33m"
#define TXT_YEL_B	"\033[1;33m"
#define TXT_BLU		"\033[0;34m"
#define TXT_BLU_B	"\033[1;34m"
#define TXT_WHI		"\033[0;37m"
#define TXT_WHI_B	"\033[1;37m"
#define TXT_END		"\033[0m"


static int _debug_level = 0;
static int _color_level = 1;
static int _no_msg = 0;

void veejay_set_debug_level(int level)
{
	if(level)
	{
		_debug_level = 1;
	}
	else
	{
		_debug_level = 0;
	}
}
void veejay_set_colors(int l)
{
	if(l) _color_level = 1;
	else _color_level = 0;
}
void veejay_silent()
{
	_no_msg = 1;
}

int veejay_is_silent()
{
	if(_no_msg) return 1;          
	return 0;
}


void veejay_msg(int type, const char format[], ...)
{
    char prefix[10];
    char buf[1024];
    va_list args;
    if(_no_msg) return;
    if(type == 4 && _debug_level==0 ) return; // bye

    va_start(args, format);
    vsnprintf(buf, sizeof(buf) - 1, format, args);
    int line = 0;

    if(_color_level)
    {
	  switch (type) {
	    case 2: //info
		sprintf(prefix, "%sI: ", TXT_GRE);
		break;
	    case 1: //warning
		sprintf(prefix, "%sW: ", TXT_YEL);
		break;
	    case 0: // error
		sprintf(prefix, "%sE: ", TXT_RED);
		break;
	    case 3:
	        line = 1;
		break;
	    case 4: // debug
		sprintf(prefix, "%sD: ", TXT_BLU);
		break;
	 }
 	 if(!line)
	     printf("%s %s %s\n", prefix, buf, TXT_END);
	     else
	     printf("%s%s%s", TXT_GRE, buf, TXT_END );

     }
     else
     {
	   switch (type) {
	    case 2: //info
		sprintf(prefix, "I: ");
		break;
	    case 1: //warning
		sprintf(prefix, "W: ");
		break;
	    case 0: // error
		sprintf(prefix, "E: ");
		break;
	    case 3:
	        line = 1;
		break;
	    case 4: // debug
		sprintf(prefix, "D: ");
		break;
	   }
	   if(!line)
	     printf("%s %s\n", prefix, buf);
	     else
	     printf("%s", buf );

     }
	
     va_end(args);
}

void veejay_strrep(char *s, char delim, char tok)
{
  unsigned int i;
  unsigned int len = strlen(s);
  if(!s) return;
  for(i=0; i  < len; i++)
  {
    if( s[i] == delim ) s[i] = tok;
  }
}

