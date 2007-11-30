/*
 *  yuv2rawdv - write rawdv data stream from stdin to stdout
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
#ifndef HAVE_STDINT_H
#define HAVE_STDINT_H
#endif
#include <config.h>
#ifdef SUPPORT_READ_DV2
#include <signal.h>
#include <config.h>
#include "mjpeg_logging.h"
#include "yuv4mpeg.h"
#include <fcntl.h>
#include <libdv/dv.h>
#define NTSC_W 720
#define NTSC_H 480
#define PAL_W 720
#define PAL_H 576
#define DV_PAL_SIZE 144000
#define DV_NTSC_SIZE 120000

static int got_sigint = 0;
static int verbose = 1;
static uint8_t *yuv_in[3];		/* yuv frame from stdin */
static uint8_t *yuv422;			/* yuv 4:2:2 buffer for encoding to dv */
static uint8_t *output_buf;	        /* buffer containing rawdv data */
static int width;
static int height;
static dv_encoder_t *encoder;		/* dv decoder */
static int clamp_chroma = 0;
static int clamp_luma = 0;
static int ntsc_setup = 0;

/* convert 4:2:0 to yuv 4:2:2  
   derived from Kino's ExtractYUV (src/frame.cc)
*/
static void convert_yuv420p_to_yuv422() {
	unsigned int x,y;
	unsigned int i=0;

	for(y=0; y < height; ++y) {
		uint8_t *Y = yuv_in[0] + y * width;
		uint8_t *Cb = yuv_in[1] + (y/2) * (width/2);
		uint8_t *Cr = yuv_in[2] + (y/2) * (width/2);
		for(x=0; x < width; x+=2) {
			*(yuv422+i) = Y[0];
			*(yuv422+i+1) = Cb[0];
			*(yuv422+i+2) = Y[1];
			*(yuv422+i+3) = Cr[0];
			i+=4;
			Y += 2;
			++Cb;
			++Cr;
		}
	}
}

/* make a yuv422 frame and give it to the DV encoder */
static void encode_yuv420_to_dv(uint8_t *outbuf) {
	static uint8_t *pixels[3];	        /* pointers */
	time_t now = time(NULL);		/* time */
	

	convert_yuv420p_to_yuv422();		/* conversion */
	
        pixels[0] = (uint8_t*) yuv422;
        if(encoder->isPAL) {
		pixels[2] = (uint8_t*)yuv422 + (PAL_W * PAL_H);
		pixels[1] = (uint8_t*)yuv422 + (PAL_W * PAL_H*5)/4;
        }
        else {
		pixels[2] = (uint8_t*)yuv422 + (NTSC_W * NTSC_H);
       		pixels[1] = (uint8_t*)yuv422 + (NTSC_W * NTSC_H * 5)/4;
      	}
	
        dv_encode_full_frame(encoder, pixels, e_dv_color_yuv, output_buf);
        dv_encode_metadata(output_buf, encoder->isPAL, encoder->is16x9, &now, 0);
        dv_encode_timecode(output_buf, encoder->isPAL, 0);

}


void usage(void) 
{
  fprintf(stderr,
	  "This program reads a YUV4MPEG stream and puts RAW DV to stdout\n"
	  "Usage:  yuv2rawdv [params]\n"
	  "where possible params are:\n"
	  "   -v num     Verbosity [0..2] (default 1)\n"
	  "   -l num     Clamp Luma (default 0)\n"
	  "   -c num     Clamp Chroma (default 0) \n"
	);
}

void sigint_handler (int signal) {
 
   mjpeg_debug( "Caught SIGINT, exiting...");
   got_sigint = 1;
   
}

int main(int argc, char *argv[])
{
   int frame;
   int fd_in;			
   int fd_out;
   int n;
   int output_len;		/* length of buffer, either 144000 or 120000 */
   int bytes_written = 0;
   y4m_frame_info_t frameinfo;
   y4m_stream_info_t streaminfo;

   

   while ((n = getopt(argc, argv, "v:l:c:")) != -1) {
      switch (n) {
  	case 'v':
         verbose = atoi(optarg);
         if (verbose < 0 || verbose > 2) {
            mjpeg_error( "-v option requires arg 0, 1, or 2");
			usage();
         }
         break;
  	case 'l':
	 clamp_luma = atoi(optarg);
	 if(clamp_luma < 0 || clamp_luma > 1) {
		mjpeg_error("-l option requires arg 0 or 1");
		usage();
	 }
	 break;
	case 'c':
	 clamp_chroma = atoi(optarg);
	 if(clamp_chroma < 0 || clamp_chroma > 1) {
		mjpeg_error("-c option requires arg 0 or 1");
	 	usage();
	 }
	 break;
      default:
         usage();
         exit(1);
      }
   }

   (void)mjpeg_default_handler_verbosity(verbose);   

   fd_in = 0;                   /* stdin */
   fd_out = 1;  

   y4m_init_stream_info(&streaminfo);
   y4m_init_frame_info(&frameinfo);

   if (y4m_read_stream_header(fd_in, &streaminfo) != Y4M_OK) {
      mjpeg_error( "Couldn't read YUV4MPEG header!");
      exit (1);
   }

   width = y4m_si_get_width(&streaminfo);
   height = y4m_si_get_height(&streaminfo);
 
   if( height == PAL_H && width == PAL_W) {
	mjpeg_info("Video is PAL, dimensions are %d x %d\n",width,height);
   }
   else if(height == NTSC_H && width == NTSC_W) {
		mjpeg_info("Video is NTSC, dimensions are %d x %d\n", width,height);
		ntsc_setup = 1;
	}
 	else {
		mjpeg_error("Error: Video is not PAL or NTSC.\n");
		exit(1); 
	}

   encoder = dv_encoder_new(ntsc_setup,clamp_luma,clamp_chroma);
   encoder->isPAL = ( height == PAL_H ? 1: 0);
   encoder->is16x9 = FALSE;
   encoder->vlc_encode_passes = 3;
   encoder->static_qno = 0;
   encoder->force_dct = DV_DCT_AUTO;
 
   if(encoder->isPAL) {
     	output_len = 144000; /* PAL size */
   }
   else {
        output_len = 120000; /* NTSC size */
   }

   yuv_in[0] = (uint8_t*)malloc(width * height * sizeof(uint8_t));
   yuv_in[1] = (uint8_t*)malloc(width * height * sizeof(uint8_t));
   yuv_in[2] = (uint8_t*)malloc(width * height * sizeof(uint8_t));
   yuv422 = (uint8_t*)malloc( sizeof(uint8_t) * 4 * width * height );   
   output_buf = (uint8_t*) malloc(sizeof(uint8_t) * output_len);


   signal (SIGINT, sigint_handler);

   if(verbose) {
 	mjpeg_info("width: %d",width);
	mjpeg_info("height: %d",height);
	mjpeg_info("norm: %s", (encoder->isPAL ? "PAL" : "NTSC"));
	mjpeg_info("clamp luma: %s", clamp_luma ? "yes" : "no");
        mjpeg_info("clamp chroma: %s", clamp_chroma ? "yes" : "no");
   }

   frame = 0;
   while (y4m_read_frame(fd_in, &streaminfo, &frameinfo, yuv_in)==Y4M_OK && (!got_sigint)) {
    
      encode_yuv420_to_dv(output_buf); 	
     
      frame++;
	
      bytes_written = write( fd_out, output_buf, output_len);
      if(verbose) mjpeg_info ("Raw DV frame %d len = %d\r", frame,bytes_written);
      if(bytes_written != output_len) {
		mjpeg_warn("Unable to write Full frame. (%d out of %d bytes)\n",
			bytes_written,output_len);
	}
   }

 
   /* clean up */     
   for (n=0; n<3; n++) {
      free(yuv_in[n]);
   }
   free(output_buf);
   free(yuv422);

   y4m_fini_frame_info(&frameinfo);
   y4m_fini_stream_info(&streaminfo);

   return 0;
}
#else

int main(int argc, char *argv[])
{
	printf("yuv2rawdv needs libdv\n");
	return 0;
}
#endif
