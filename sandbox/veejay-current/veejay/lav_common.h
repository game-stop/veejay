/* lav_common - some general utility functionality used by multiple
	lavtool utilities. */

/* Copyright (C) 2000, Rainer Johanni, Andrew Stevens */
/* - added scene change detection code 2001, pHilipp Zabel */
/* - broke some code out to lav_common.h and lav_common.c
 *   July 2001, Shawn Sulma <lavtools@athos.cx>.  Part of these changes were
 *   to replace the large number of globals with a handful of structs that
 *   get passed in to the relevant functions.  Some of this may be
 *   inefficient, subtly broken, or Wrong.  Helpful feedback is certainly
 *   welcome.
 */
/* - removed a lot of subsumed functionality and unnecessary cruft
 *   March 2002, Matthew Marjanovic <maddog@mir.com>.
 */

/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.                
*/

#include <config.h>

#include "editlist.h"
#include "yuv4mpeg.h"



#include "mjpeg_logging.h"


#define MAX_EDIT_LIST_FILES 4096
#define MAX_JPEG_LEN (3*576*768/2)

#define BUFFER_ALIGN 16

/**
 * (SS 2001-July-13)
 * The split of the globals into three structs is somewhat arbitrary, but
 * I've tried to do them based on role as used in lav2yuv and (my own)
 * lav2divx.  
 * - LavParam handles data that is generally per-run dependent
 *   (e.g. from the command line).
 * - LavBounds contains data about bounds used in processing.  It is generally
 *   not dependent on command line alteration.
 * - LavBuffer contains buffers used to perform various tasks.
 *
 **/

typedef struct {
    int offset;
    int frames;
    int mono;
    char *scenefile;
    int delta_lum_threshold;	/* = 4; */
    unsigned int scene_detection_decimation;	/* = 2; */
    int output_width;
    int output_height;
    int interlace;
    y4m_ratio_t sar;		/* sample aspect ratio (default 0:0 == unspecified) */
    y4m_ratio_t dar;		/* 'suggested' display aspect ratio */

    int chroma_width;
    int chroma_height;
    int luma_size;
    int chroma_size;
} LavParam;


int luminance_mean(uint8_t * frame[], int w, int h);

int readframe(int numframe, uint8_t * frame[],
	      LavParam * param, EditList el);

void writeoutYUV4MPEGheader(int out_fd, LavParam * param, EditList el,
			    y4m_stream_info_t * streaminfo);

void init(LavParam * param, uint8_t * frame[]);


#ifdef SUPPORT_READ_DV2

#include <libdv/dv.h>

void frame_YUV422_to_YUV420P(uint8_t ** output, uint8_t * input,
			     int width, int height);
void lav_init_dv_decoder(void);

#endif				/* SUPPORT_READ_DV2 */
