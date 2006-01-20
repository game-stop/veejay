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

#include <config.h>
#include <stdlib.h>

#ifdef SUPPORT_READ_DV2

#ifndef HAVE_STDINT_H
#define HAVE_STDINT_H
#endif

#include <string.h>
#include <signal.h>
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
static uint8_t *dv_in[3];		/* decoder buffer */
static uint8_t *yuv[3];			/* yuv destination buffer */
static uint8_t input_buf[144008];	/* buffer containing raw dv data */
static uint8_t *half[3];		/* half size yuv destination buffer */	

static int width;
static int height;
static int swap = 0;			/* dont swap cb/cr by default */
static int clip = 0;			/* no clipping by default */
static int verbose = 1;			/* level 1 verbosity by default */
static int PAL=1;			/* use PAL by default */
static int quality=0;			/* use best quality by default */

static dv_decoder_t *decoder;		/* dv decoder */
	
static int input_len;			/* length of dv input len */
static int half_size = 0;
static int half_width;
static int half_height;
	
static double fps = 25.0;		/* PAL framerate */


static void yuv_clip(uint8_t *input[3], uint8_t *output[3], int w, int h) {
    unsigned int uv_len = (w*h)>>2;
    unsigned int len = (w*h)>>2;
    int uv = clip>>1;
    unsigned int r,c;
    uint8_t *Y_out = output[0];
    uint8_t *Y_in = input[0];
    uint8_t *Cr_in = input[2];
    uint8_t *Cr_out = output[2];
    uint8_t *Cb_out = output[1];
    uint8_t *Cb_in = input[1];
    unsigned int nw = w >> 1;
    unsigned int nh = h >> 1;

    memcpy( Y_in, Y_out, len); 
    memcpy( Cb_in, Cb_out, uv_len);
    memcpy( Cr_in, Cr_out, uv_len);


    /* Luminance */
    for(r=0; r < (nw*nh)+(nw-clip); r+=nw) {
      for(c=0; c < nw-clip; c++) {
	*(Y_out)++ = Y_in[r+c]; 
	} 
    }
    /* Chroma Cb */
    nw = (w>>2);
    nh = (h>>2);
    for(r=0; r < (nw*nh)+(nw-uv); r+=nw) {
      for(c=0; c < nw-uv; c++) {
        *(Cb_out)++ = Cb_in[r+c];
	} 
    }
    /* Chroma Cr */
    nw = (w>>2);
    nh = (h>>2);
    uv = clip>>1;
    for(r=0; r < (nw*nh)+(nw-uv); r+=nw) {
      for(c=0; c < nw-uv; c++) {
	*(Cr_out)++  = Cr_in[r+c];
	} 
    }
}

static void yuv_resize(uint8_t *input[3], uint8_t *output[3], int w, int h) {
  int len = (w * h);
  int r,c;
  int sw = w * 2;
  int i=0;
  uint8_t p1,p2,p3,p4,ps;
  uint8_t *Y_out = output[0];
  uint8_t *Y_in = input[0];
  uint8_t *Cb_out = output[1];
  uint8_t *Cb_in = input[1];
  uint8_t *Cr_out = output[2];
  uint8_t *Cr_in = input[2];
  /* Luminance */
  for(r=w; r < len+w; r+= sw) {
     for(c=0; c < w; c+=2) {
       p1 = Y_in[r+c];       /* center */  
       p2 = Y_in[r+c+1];     /* west */
       p3 = Y_in[r-w+c+1];   /* north west */
       p4 = Y_in[r-w+c];     /* north */
	
       /* clip pixels to valid range */
       if(p1 < 16) p1 = 16; else if (p1 > 240) p1 = 240;
       if(p2 < 16) p2 = 16; else if (p2 > 240) p2 = 240;
       if(p3 < 16) p3 = 16; else if (p3 > 240) p3 = 240;
       if(p4 < 16) p4 = 16; else if (p4 > 240) p4 = 240;
 

       /* look at neighbours to determine brightest value */
       if (p1 < p2 && p1 < p3)
          ps = ( p2 + p3 ) >> 1;
       else
         if( ( p1 < p3 ) && (p1 < p4) )
          ps = ( p3 + p4 ) >> 1;
         if( ( p1 < p4 ) && ( p1 < p2) ) 
	  ps = ( p2 + p4 ) >> 1;
         else /* average 4 pixels and divide by 4 */
          ps = ( p1 + p2 + p3 + p4 ) >> 2; 
	
       /* store pixel */
       *(Y_out)++ = ps;
       
     }
  }

  /* Chroma Cb */

  len = len >> 1;
  sw = w * 2;

  for(r=w; r < len+w; r+= sw) {
     for(c=0; c < w; c+=2) {
       p1 = Cb_in[r+c];       /* center */  
       p2 = Cb_in[r+c+1];     /* west */
       p3 = Cb_in[r-w+c+1];   /* north west */
       p4 = Cb_in[r-w+c];     /* north */
	
       /* clip pixels to valid range */
       if(p1 < 16) p1 = 16; else if (p1 > 235) p1 = 235;
       if(p2 < 16) p2 = 16; else if (p2 > 235) p2 = 235;
       if(p3 < 16) p3 = 16; else if (p3 > 235) p3 = 235;
       if(p4 < 16) p4 = 16; else if (p4 > 235) p4 = 235;
       /* look at neighbours to determine better value */
       if (p1 < p2 && p1 < p3)
          ps = ( p2 + p3 ) >> 1;
       else
         if( ( p1 < p3 ) && (p1 < p4) )
          ps = ( p3 + p4 ) >> 1;
         if( ( p1 < p4 ) && ( p1 < p2) ) 
	  ps = ( p2 + p4 ) >> 1;
         else /* average 4 pixels and divide by 4 */
          ps = ( p1 + p2 + p3 + p4 ) >> 2; 

       *(Cb_out)++ = ps;
      	
    }
  }

  /* Chroma Cr */

  len = len >> 1;
  sw = w * 2;

  for(r=w; r < len+w; r+= sw) {
     for(c=0; c < w; c += 2) {
       p1 = Cr_in[r+c];       /* center */  
       p2 = Cr_in[r+c+1];     /* west */
       p3 = Cr_in[r-w+c+1];   /* north west */
       p4 = Cr_in[r-w+c];     /* north */
	
       /* clip pixels to valid range */
       if(p1 < 16) p1 = 16; else if (p1 > 235) p1 = 235;
       if(p2 < 16) p2 = 16; else if (p2 > 235) p2 = 235;
       if(p3 < 16) p3 = 16; else if (p3 > 235) p3 = 235;
       if(p4 < 16) p4 = 16; else if (p4 > 235) p4 = 235;
       /* look at neighbours to determine better value */
       if (p1 < p2 && p1 < p3)
          ps = ( p2 + p3 ) >> 1;
       else
         if( ( p1 < p3 ) && (p1 < p4) )
          ps = ( p3 + p4 ) >> 1;
         if( ( p1 < p4 ) && ( p1 < p2) ) 
	  ps = ( p2 + p4 ) >> 1;
         else /* average 4 pixels and divide by 4 */
          ps = ( p1 + p2 + p3 + p4 ) >> 2; 

       *(Cr_out)++ = ps;

     }
  }

}

/* convert 4:2:2 to IV12 ,swap u v to get 4:2:0 */
static void yuy2toyv12(uint8_t * _y, uint8_t * _u, uint8_t * _v, uint8_t * input,
		int width, int height)
{
    int i, j, w2;
    uint8_t *y, *u, *v;
    w2 = width / 2;
    y = _y;
    v = _v;
    u = _u;
    for (i = 0; i < height; i += 2) {
	for (j = 0; j < w2; j++) {
	    /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
	    *(y++) = *(input++);
	    *(u++) = *(input++);
	    *(y++) = *(input++);
	    *(v++) = *(input++);
	}
	//down sampling
	for (j = 0; j < w2; j++) {
	     *(y++) = *(input++); *(input++);
	     *(y++) = *(input++); *(input++);
	}
    }
}


static void usage(void) 
{
  fprintf(stderr,
	  "This program reads a raw DV stream from stdin and puts YV12/I420 to stdout\n"
	  "Usage:  rawdv2yuv [params]\n"
	  "where possible params are:\n"
	  "   -v num     Verbosity [0..2] (default 1)\n"
	  "   -x         Swap Cb/Cr channels to produce IV12 (default is I420)\n"
	  "   -n num     Norm to use: 0 = NTSC, 1 = PAL (default 1)\n"	
	  "   -q         DV quality to fastest (Monochrome)\n"
	  "   -h	 Output Half frame size\n"
	  "   -c num     clip off <num> rows of frame (for use with -h)\n"
          "              must be a multiple of 8\n"
	);
   exit(0);
}

static void sigint_handler (int signal) {
 
   mjpeg_debug( "Caught SIGINT, exiting...");
   got_sigint = 1;
   
}

static void allocate_mem() {
 
   dv_in[0] = (uint8_t*)malloc(width * height * sizeof(uint8_t) * 4);
   dv_in[1] = NULL;
   dv_in[2] = NULL;
   yuv[0] = (uint8_t*)malloc( sizeof(uint8_t) * width * height );   
   yuv[1] = (uint8_t*)malloc( sizeof(uint8_t) * (width * height)/2 );  
   yuv[2] = (uint8_t*)malloc( sizeof(uint8_t) * (width * height)/2 );  

   half[0] = (uint8_t*)malloc( sizeof(uint8_t) * (half_width * half_height) );  
   half[1] = (uint8_t*)malloc( sizeof(uint8_t) * (half_width * half_height) );  
   half[2] = (uint8_t*)malloc( sizeof(uint8_t) * (half_width * half_height) );  

   memset(dv_in[0],0,width*height*4);
   memset(yuv[0],0,width*height);
   memset(half[0],0,half_width * half_height);
   memset(input_buf,0,input_len);

   memset(yuv[1],0,(width*height)/2);
   memset(half[1],0,(half_width * half_height));

   memset(yuv[1],0,(width*height)/2);
   memset(half[0],0,(half_width * half_height));
}

static void free_mem() {
   if(dv_in[0]) free(dv_in[0]);
   if(yuv[0]) free(yuv[0]);
   if(yuv[1]) free(yuv[1]);
   if(yuv[2]) free(yuv[2]);
   if(half[0]) free(half[0]);
   if(half[1]) free(half[1]);
   if(half[2]) free(half[2]); 
}

static void set_options(int argc, char *argv[]) {
  int n;
   while ((n = getopt(argc, argv, "v:sqhn:c:")) != -1) {
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
	case 'q':
	  quality = 1;
	  break;
	case 'h':
	  half_size = 1;
	  break;
        case 'c':
	  clip = atoi(optarg);
	  if(clip < 0) {
		mjpeg_error("That does not make sense");
		usage();	
	  }
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
   int pitches[3];
   int error = 0;
  
   set_options(argc,argv);

   if(PAL) {
     width = PAL_W;
     height = PAL_H;
     input_len = 144000;
   }
   else {
     width = NTSC_W;
     height = NTSC_H;
     input_len = 120000;
   }

   (void)mjpeg_default_handler_verbosity(verbose);   

   fd_in = 0;                   /* stdin */
   fd_out = 1;  		/* stdout */

   
   half_width = width >> 1;
   half_height = height >> 1; 

   allocate_mem();
   
   /* setup the dv decoder */
   decoder = dv_decoder_new(1,1,0);
   decoder->quality = (quality==0 ? DV_QUALITY_BEST : DV_QUALITY_FASTEST);

   /* setup the y4m stream */
   y4m_init_stream_info(&streaminfo);
   y4m_init_frame_info(&frameinfo);
 
   y4m_si_set_width( &streaminfo, (half_size==1 ? half_width-clip: width));
   y4m_si_set_height( &streaminfo,(half_size==1 ? half_height: height));
   y4m_si_set_interlace( &streaminfo, 0);

   if (y4m_write_stream_header(fd_out, &streaminfo) != Y4M_OK) {
      mjpeg_error( "Couldn't write YUV4MPEG header!");
      exit (1);
   }
 
   if( height == PAL_H && width == PAL_W) {
	mjpeg_info("Video is PAL, dimensions are %d x %d\n",width,height);
   }
   else if(height == NTSC_H && width == NTSC_W) {
		mjpeg_info("Video is NTSC, dimensions are %d x %d\n", width,height);
	}
 	else {
		mjpeg_error("Error: Video is not PAL or NTSC.\n");
		exit(1); 
	}
   if(half_size) {
	mjpeg_info("Resizing to %d x %d\n",half_width-clip,half_height);
	}

   /* setup interrupt handler */
   signal (SIGINT, sigint_handler);

   /* be verbosive */
   if(verbose) {
 	mjpeg_info("width: %d",width);
	mjpeg_info("height: %d",height);
	mjpeg_info("norm: %s", (PAL ? "PAL" : "NTSC"));
	mjpeg_info("%s", (swap ? "IV12" : "I420"));
   }

   /* first frame */
   frame = 0;

   /* try to read first frame of raw stream to extract dv header */
   if(read(fd_in,input_buf,input_len) <= 0) {
	mjpeg_warn("Error reading first frame\n");
	}

   if(dv_parse_header(decoder, input_buf) < 0) mjpeg_warn("Error parsing dv header");

   /* set the format */
   if(dv_format_wide(decoder)) {
	mjpeg_warn("format 16:9");
	}

   if(dv_format_normal(decoder)) {
	mjpeg_warn("format 4:3");
	}

   if (decoder->sampling == e_dv_sample_411 ||
	 	decoder->sampling == e_dv_sample_422 ||
	 	decoder->sampling == e_dv_sample_420) {

	 	pitches[0] = width * 2;
	 	pitches[1] = 0;
	 	pitches[2] = 0;
	}


   /* start processing */
   while ( (!got_sigint) && !error) {
	  int skip = 0;
	  res = read(fd_in, input_buf,input_len);
	  if( res !=input_len) {
		if(res <= 0) {
		  error = 1; /* error */
		  skip = 1;
		}
		else { /* try to read the rest of the frame */
		     int bytes=0;
		     int bytes_left = input_len - res;
	 	     while(bytes_left) {
 			bytes = read(fd_in,input_buf+res, bytes_left);
			if(bytes <= 0) { error = 1; break; }
			bytes_left -= bytes;
		    }
		}
	   } 

	  //dv_parse_header(decoder,input_buf);    

	  if(!skip) {
	 	dv_decode_full_frame( decoder, input_buf,
			     e_dv_color_yuv, dv_in, pitches);

	        if(!swap) 
	        	yuy2toyv12(yuv[0],yuv[1], yuv[2], dv_in[0], width, height);
           	else 
	     		yuy2toyv12(yuv[0],yuv[2],yuv[1],dv_in[0],width,height);
		
		if(half_size) {
			yuv_resize(yuv,half,width,height);
			if(clip) yuv_clip(yuv, half,width,height);		
		  	if ( y4m_write_frame( fd_out,&streaminfo,&frameinfo, half)!=Y4M_OK) {
				mjpeg_warn("Error writing frame");
	   		}
		}
		else {
			if ( y4m_write_frame( fd_out,&streaminfo,&frameinfo, yuv)!=Y4M_OK) {
				mjpeg_warn("Error writing frame");
	   		}
			else {
				if(verbose) mjpeg_info("Writing frame %d",frame);
			}	

		}

	  }

	  frame++;
	  	
   }

   if(verbose) mjpeg_info("Wrote %d frames",frame);

   /* free the dv decoder */
   dv_decoder_free(decoder);

   /* finalize the y4m stream */
   y4m_fini_stream_info(&streaminfo);
   y4m_fini_frame_info(&frameinfo);

   
   free_mem();

   return 0;
}

#else

int main(int argc, char *argv[])
{
	mjpeg_error("%s requires libdv, get it from http://libdv.sourceforge.net and recompile veejay's tools",argv[0]);
	return 1;
}

#endif

/*



*/
