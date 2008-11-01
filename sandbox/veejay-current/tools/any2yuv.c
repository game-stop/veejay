/*
 *  any2yuv - read anything to yuv
 *
 *  (C) Niels Elburg 2003 <nielselburg@yahoo.de>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */


#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef HAVE_STDINT_H
#define HAVE_STDINT_H
#endif

#include <string.h>
#include <signal.h>
#include "mjpeg_logging.h"
#include "yuv4mpeg.h"
#include <fcntl.h>

#define NTSC_W 720
#define NTSC_H 480
#define PAL_W 720
#define PAL_H 576

static int got_sigint = 0;
static uint8_t *raw_in[3];		/* decoder buffer */
static uint8_t *yuv[3];			/* yuv destination buffer */

static int width = 352;
static int height = 288;
static int swap = 0;			/* dont swap cb/cr by default */
static int verbose = 1;			/* level 1 verbosity by default */
static int PAL=1;			/* use PAL by default */
	
static int input_len;			/* length of dv input len */
	
static double fps = 25.0;		/* PAL framerate */


static void usage(void) 
{
  fprintf(stderr,
	  "This program reads anything from stdin and puts YV12/I420 to stdout\n"
	  "Usage:  any2yuv [params]\n"
	  "where possible params are:\n"
	  "   -v num     Verbosity [0..2] (default 1)\n"
	  "   -x         Swap Cb/Cr channels to produce IV12 (default is I420)\n"
	  "   -n num     Norm to use: 0 = NTSC, 1 = PAL (default 1)\n"	
	);
   exit(0);
}

static void sigint_handler (int signal) {
 
   mjpeg_debug( "Caught SIGINT, exiting...");
   got_sigint = 1;
   
}

static void allocate_mem() {
 
   raw_in[0] = (uint8_t*)malloc(width * height * sizeof(uint8_t) * 3);
   yuv[0] = (uint8_t*)malloc( sizeof(uint8_t) * width * height );   
   yuv[1] = (uint8_t*)malloc( sizeof(uint8_t) * (width * height)/4 );  
   yuv[2] = (uint8_t*)malloc( sizeof(uint8_t) * (width * height)/4 );  

   memset(raw_in[0],0,width*height*3);
   memset(yuv[0],16,width*height);

   memset(yuv[1],128,(width*height)/4);
   memset(yuv[2],128,(width*height)/4);
}

static void free_mem() {
   if(raw_in[0]) free(raw_in[0]);
   if(yuv[1]) free(yuv[1]);
   if(yuv[2]) free(yuv[2]);
}

static void set_options(int argc, char *argv[]) {
  int n;
   while ((n = getopt(argc, argv, "v:sn:w:h:f:")) != -1) {
      switch (n) {
  	case 'v':
         verbose = atoi(optarg);
         if (verbose < 0 || verbose > 2) {
            mjpeg_error( "-v option requires arg 0, 1, or 2");
			usage();
         }
         break;
	case 's':
	  swap = 1;
	  break;
	case 'n':
	  PAL = atoi(optarg);
	  if(PAL <0 || PAL > 1) {
		mjpeg_error("-n option requires arg 0 or 1");
		usage();
	  }
	  break;
	case 'w':
	  width = atoi(optarg);
	  break;
	case 'h':
	  height = atoi(optarg);
	  break;
	case 'f':
	  fps = (double) atof(optarg); 

          break; 
      default:
         usage();
         exit(1);
      }
   }

}

int main(int argc, char *argv[] ) {
   int frame;
   int fd_in;			
   int fd_out;
   int res;
   y4m_frame_info_t frameinfo;
   y4m_stream_info_t streaminfo;
   int error = 0;
   uint8_t *raw; 


   set_options(argc,argv);

   (void)mjpeg_default_handler_verbosity(verbose);   

   fd_in = 0;                   /* stdin */
   fd_out = 1;  		/* stdout */

   
   allocate_mem();
   
   /* setup the y4m stream */
   y4m_init_stream_info(&streaminfo);
   y4m_init_frame_info(&frameinfo);
 
   y4m_si_set_width( &streaminfo, width);
   y4m_si_set_height( &streaminfo,height);
   y4m_si_set_interlace( &streaminfo, 0);

   if (y4m_write_stream_header(fd_out, &streaminfo) != Y4M_OK) {
      mjpeg_error( "Couldn't write YUV4MPEG header!");
      exit (1);
   }
 
   /* setup interrupt handler */
   signal (SIGINT, sigint_handler);

   /* be verbosive */
   if(verbose) {
 	mjpeg_info("width: %d",width);
	mjpeg_info("height: %d",height);
   }

   /* first frame */
   frame = 0;
   raw = raw_in[0];
   /* start processing */
   while ( (!got_sigint) && !error) {
	  int skip = 0;
	  res = read( fd_in ,raw,(width*height*2));
	  if( res != (width*height*2)) {
		if(res <= 0) {
		  error = 1; /* error */
		  skip = 1;
		}
		else { /* try to read the rest of the frame */
		     int bytes=0;
		     int bytes_left = (width*height*2)- res;
		     while(bytes_left) {
			bytes = read(fd_in,raw+res, bytes_left);
			if(bytes <= 0) { error = 1; break; }
			bytes_left -= bytes;
		    }
		}
	  }

	  if(!skip) {
		int i,j=0;
		for(i=0; i < (width*height); i++) { 
			yuv[0][i] = raw[i];
			j++;
		}
		for(i=0; i < (width*height)/4; i++) {
			yuv[1][i] = raw[j];
			yuv[2][i] = raw[j++];
		}	
 
		if ( y4m_write_frame( fd_out,&streaminfo,&frameinfo, yuv)!=Y4M_OK) {
			mjpeg_warn("Error writing frame");
	   	}
		else {
			if(verbose) mjpeg_info("Writing frame %d",frame);
		}	


	  }

	  frame++;
	  	
   }

   if(verbose) mjpeg_info("Wrote %d frames",frame);

   y4m_fini_stream_info(&streaminfo);
   y4m_fini_frame_info(&frameinfo);

   
   free_mem();

   return 0;
}

