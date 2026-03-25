/* veejay - Linux VeeJay
 * 	     (C) 2002-2010 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <config.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sysexits.h>
#include <veejaycore/defs.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/atomic.h>
#include <veejaycore/core.h>
#include <libveejay/vj-sdl.h>
#include <veejaycore/vj-msg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <libveejay/vj-lib.h>
#include <libveejay/vj-event.h>
#include <libveejay/libveejay.h>
#include <veejaycore/libvevo.h>
#include <libvje/vje.h>
#include <veejaycore/vims.h>
#ifndef X_DISPLAY_MISSING
#include <libveejay/x11misc.h>
#endif
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <libveejay/vj-OSC.h>
#include <build.h>
#include <libvje/libvje.h>

extern void vj_avcodec_print_version();
static veejay_t *info = NULL;
static float override_fps = 0.0;
static int default_geometry_x = -1;
static int default_geometry_y = -1;
static int use_keyb = 1;
static int use_mouse = 1;
static int show_cursor = 0;
static int borderless = 0;
static int force_video_file = 0; // unused
static int override_pix_fmt = 0;
static int switch_jpeg = 0;
static char override_norm = '\0';
static int auto_loop = 0;
static int n_slots_ = 0;
static int max_mem_ = 0;
static int live = -1;
static int ta = -1;

static void report_bug(void)
{
    veejay_msg(VEEJAY_MSG_WARNING, "Please report this error to veejay's issue tracker");
    veejay_msg(VEEJAY_MSG_WARNING, "Send at least veejay's output and include the command(s) you have used to start it");
	veejay_msg(VEEJAY_MSG_WARNING, "Also, please consider sending in the recovery files if any have been created");
	veejay_msg(VEEJAY_MSG_WARNING, "If you compiled it yourself, please include information about your system");
}

static void CompiledWith(void)
{
	fprintf(stdout,"This is Veejay %s\n\n", VERSION);

	fprintf(stdout,    
		"Build for %s/%s arch %s on %s\n\n",
	    BUILD_OS,
	    BUILD_KERNEL,
	    BUILD_MACHINE,
	    BUILD_DATE );

	fprintf(stdout,
		"libveejaycore is build from git commit hash %s\n", veejay_core_build());
	fprintf(stdout,
		"libveejay is build from git commit hash %s\n\n", GIT_HASH_VEEJAY);

	fprintf(stdout,
		"Detected cpu cache line size: %d\n", cpu_get_cacheline_size());
	fprintf(stdout,
		"Memory alignment size: %d\n" , mem_align_size());

	fprintf(stdout,"\nArchitecture:\n\n");
#ifdef ARCH_MIPS
	fprintf(stdout, "\tMIPS\n");
#endif
#ifdef ARCH_PPC
	fprintf(stdout, "\tPPC\n");
#endif
#ifdef ARCH_X86_64
	fprintf(stdout, "\tX86_64\n");
#endif
#ifdef ARCH_X86
	fprintf(stdout, "\tX86\n");
#endif
#ifdef HAVE_ARM_ASIMD
    fprintf(stdout, "\tARM ASIMD\n");
#endif
#ifdef HAVE_ARM_NEON
    fprintf(stdout, "\tARM NEON\n");
#endif
#ifdef HAVE_ARM
    fprintf(stdout, "\tARM\n");
#endif
#ifdef HAVE_ARMV7A
	fprintf(stdout, "\tARMv7A\n");
#endif
#ifdef HAVE_DARWIN
	fprintf(stdout, "\tDarwin\n");
#endif
#ifdef HAVE_PS2
	fprintf(stdout, "\tSony Playstation 2 (TM)\n");
#endif
	fprintf(stdout, "\n\nCompiled in support for:\n\n");
#ifdef HAVE_ALTIVEC
	fprintf(stdout,"\tAltivec\n");
#endif
#ifdef HAVE_CMOV
	fprintf(stdout,"\tCMOV\n");
#endif
#ifdef HAVE_ASM_3DNOW
    fprintf(stdout, "\t3DNOW\n");
#endif
#ifdef HAVE_ASM_SSE
	fprintf(stdout,"\tSSE\n");
#endif
#ifdef HAVE_ASM_SSE2
	fprintf(stdout,"\tSSE2\n");
#endif
#ifdef HAVE_ASM_SSE4_1
	fprintf(stdout,"\tSSE4.1\n");
#endif
#ifdef HAVE_ASM_SSE4_2
	fprintf(stdout,"\tSSE4.2\n");
#endif
#ifdef HAVE_ASM_MMX
	fprintf(stdout,"\tMMX\n");
#endif
#ifdef HAVE_ASM_MMX2
    fprintf(stdout,"\tMMX2\n");
#endif
#ifdef HAVE_ASM_MMXEXT
	fprintf(stdout,"\tMMXEXT\n");
#endif
#ifdef HAVE_ASM_MMX2	
	fprintf(stdout,"\tMMX2\n");
#endif
#ifdef HAVE_ASM_3DNOW
	fprintf(stdout,"\t3Dnow\n");
#endif
#ifdef HAVE_ASM_AVX
	fprintf(stdout,"\tAVX\n");
#endif
#ifdef HAVE_ASM_AVX2
    fprintf(stdout,"\tAVX2\n");
#endif
#ifdef HAVE_ASM_AVX512
    fprintf(stdout, "\tAVX512\n");
#endif

	fprintf(stdout,"\n\nDependencies:\n");

#ifdef USE_GDK_PIXBUF
	fprintf(stdout,"\tSupport for GDK image loading\n");
#endif
#ifdef HAVE_JACK
	fprintf(stdout,"\tSupport for Jack Audio Connection Kit\n");
#endif
#ifdef SUPPORT_READ_DV2
	fprintf(stdout,"\tSupport for Digital Video\n");
#endif
#ifdef HAVE_LIBQUICKTIME
	fprintf(stdout,"\tSupport for Quicktime Video\n");
#endif
#ifdef HAVE_XML2
	fprintf(stdout,"\tSupport for XML\n");
#endif
#ifdef HAVE_SDL
	fprintf(stdout,"\tSupport for Simple Direct Media Layer\n");
#endif
#ifdef HAVE_JPEG
	fprintf(stdout,"\tSupport for JPEG\n");
#endif
#ifdef HAVE_LIBPTHREAD
	fprintf(stdout,"\tSupport for Multithreading\n");
#endif
#ifdef HAVE_V4L2
	fprintf(stdout, "\tSupport for Capture Devices\n");
#endif
#ifdef HAVE_FREETYPE
	fprintf(stdout, "\tSupport for TrueType Fonts\n");
#endif
#ifdef HAVE_LIBUNWIND
    fprintf(stdout, "\tSupport for stack unwinding\n");
#endif
#ifdef HAVE_DIRECTFB
	fprintf(stdout,"\tSupport for Direct Framebuffer\n");
#endif
#ifdef HAVE_LIBLO
	fprintf(stdout,"\tSupport for liblo\n");
#endif
#ifdef HAVE_QRCODE
	fprintf(stdout,"\tSupport for QR code\n");
#endif

    fprintf(stdout, "\n\n");

	exit(0);
}

static void Usage(char *progname)
{
    fprintf(stderr, "This is Veejay %s\n\n", VERSION);
    fprintf(stderr, "Usage: %s [options] <file name> [<file name> ...]\n", progname);
    fprintf(stderr,"where options are:\n\n");
	fprintf(stderr,
			"  -v/--verbose  \t\tEnable debugging output (default off)\n");
	fprintf(stderr,
			"  -n/--no-color     \t\tDo not use colored text\n");
   	fprintf(stderr,
	    	"  -u/--dump-events  \t\tDump veejay's documentation to stdout\n");
	fprintf(stderr,
			"  -B/--features  \t\tList of compiled features\n");
   	fprintf(stderr,
			"  -D/--composite \t\tDo not start with camera/projection calibration viewport \n");
	fprintf(stderr,
	   		"  -p/--port <num>\t\tTCP port to accept/send messages (default: 3490)\n");
	fprintf(stderr,
			"  -M/--multicast-osc <multicast address> \t\tmulticast OSC\n");
	fprintf(stderr,
			"  -T/--multicast-vims <multicast address> \t\tmulticast VIMS\n");
 	fprintf(stderr,
			"  -m/--memory <num>\t\tMaximum memory to use for cache (0=disable, default=0 max=100)\n");  
	fprintf(stderr,
			"  -j/--max_cache <num>\t\tDivide cache memory over N samples (default=0)\n");
	fprintf(stderr,
			"  -Y/--yuv [01]\t\t\tForce YCbCr (defaults to YUV)\n");
	fprintf(stderr,
	    	"  -t/--timer <num>\t\tSpecify timer to use (0=none, 1=default timer) (default=1) \n");
    fprintf(stderr,
	    	"  -P/--preserve-pathnames\tDo not 'canonicalise' pathnames in edit lists\n");
	fprintf(stderr,
            "  -e/--swap-range <num>\t\tSwap YUV range [0..255] <-> [16..235] on videofiles\n");
	fprintf(stderr,
		"\t\t\t\t 0 = YUV 4:2:0 Planar\n");
	fprintf(stderr,
		"\t\t\t\t 1 = YUV 4:2:2 Planar (default)\n");
	fprintf(stderr,
		"\t\t\t\t 2 = YUV 4:2:0 Planar full range\n");
	fprintf(stderr,
		"\t\t\t\t 3 = YUV 4:2:2 Planar full range\n");
   	fprintf(stderr,
			"  -L/--auto-loop   \t\tStart with default sample\n");
	fprintf(stderr,
			"  -b/--bezerk    \t\tBezerk (default off)   \n");
	fprintf(stderr,
	   		"  -l/--sample-file <file name>\tLoad a sample list file (none at default)\n");
	fprintf(stderr,
			"  -F/--action-file <file name>\tLoad an action file (none at default)\n");
	fprintf(stderr,
			"  -g/--clip-as-sample\t\tLoad every video clip as a new sample\n");	
	fprintf(stderr,
	    	"  -a/--audio [01]\t\tEnable (1) or disable (0) audio (default 1)\n");
    fprintf(stderr,
			"     --pace-correction [ms]\tAudio pace correction offset in milliseconds\n");
	fprintf(stderr,
	    	"  -c/--synchronization [01]\tSync correction off/on (default on)\n");
    fprintf(stderr,
	    	"  -O/--output [0..6]\t\tOutput video\n");
#ifdef HAVE_SDL
	fprintf(stderr,
			"\t\t\t\t0 = SDL (default)\t\n");
#endif
#ifdef HAVE_DIRECTFB
	fprintf(stderr,
			"\t\t\t\t1 = DirectDB\t\n");
#ifdef HAVE_SDL
	fprintf(stderr,
			"\t\t\t\t2 = SDL and DirectDB secondary head (TV-Out) clone mode\n");
#endif
#endif
	fprintf(stderr,
			"\t\t\t\t3 = Head less (no video output)\n");	
	fprintf(stderr,
			"\t\t\t\t4 = Y4M Stream 4:2:0 (requires -o <filename>)\n");
	fprintf(stderr,
			"\t\t\t\t5 = Vloopback device (requires -o <filename>)\n");
	fprintf(stderr,
			"\t\t\t\t6 = Y4M Stream 4:2:2 (requires -o <filename>)\n");
	fprintf(stderr,
			"  -o/--output-file [file]\tWrite to file (for use with -O/--output)\n");
#ifdef HAVE_SDL
    fprintf(stderr,
	    "  -s/--size NxN\t\t\tDisplay dimension for video window, use Width x Height\n");
	fprintf(stderr,
		"  -x/--geometry-x <num> \tTop left x offset for SDL video window\n");
	fprintf(stderr,
		"  -y/--geometry-y <num> \tTop left y offset for SDL video window\n");
    fprintf(stderr,
        "  --borderless\t\t\tOpen video window without title bar\n");
	fprintf(stderr,
		"  --no-keyboard\t\t\tdisable keyboard for SDL video window\n");
	fprintf(stderr,
		"  --no-mouse\t\t\tdisable mouse for SDL video window\n");
	fprintf(stderr, 
		"  --show-cursor\t\t\tshow mouse cursor in SDL video window\n");	
#endif
	fprintf(stderr,
		"  -A/--all [num] \t\tStart with capture device <num> \n");
	fprintf(stderr,
		"  -d/--dummy	\t\tStart with no video (black frames)\n");
	fprintf(stderr,
		"  -Z/--load-generators [num]\tLoad all generator plugins as streams and start with N\n");
	fprintf(stderr,
		"  -W/--input-width <num>\tSet input video width\n");
	fprintf(stderr,
		"  -H/--input-height <num>\tSet input video height\n");
    fprintf(stderr,
		"  -f/--fps <num>\t\tOverride default frame rate (default: read from first loaded file)\n");
   	fprintf(stderr,
		"  -r/--audiorate <num>\t\tSet audio rate (defaults to 48Khz)\n");
	fprintf(stderr,
	    "  -I/--deinterlace\t\tDeinterlace video if it is interlaced\n");    
	fprintf(stderr,
		"  -N/--norm <num>\t\tSet video norm [0=PAL, 1=NTSC, 2=SECAM (defaults to PAL)]\n");
	fprintf(stderr,
		"  -w/--output-width <num>\tSet output video width (Projection)\n");
	fprintf(stderr,
		"  -h/--output-height <num>\tSet output video height (Projection)\n");
	fprintf(stderr,
		"  --benchmark NxN\t\tveejay benchmark using NxN resolution\n");
#ifdef HAVE_QRENCODE  
	fprintf(stderr,
		"  --qrcode-connection-info\tEncode veejay's external IP and port number into QR code\n" );
#endif
	fprintf(stderr,
		"  -S/--scene-detection <num>\tCreate new samples based on scene detection threshold <num>\n");
	fprintf(stderr,
		"  -X/--dynamic-fx-chain\t\tDo not keep FX chain buffers in RAM (default off)\n");
	fprintf(stderr,
		"    /--split-screen\t\tLoad split screen configuration\n");
    fprintf(stderr,
        "    /--fx-custom-default-values Read FX custom default values from ~/.veejay/[livido,frei0r]\n");
	fprintf(stderr,"  -q/--quit \t\t\tQuit at end of file\n");
    fprintf(stderr,"  -?/--help \t\t\tYou are looking at it\n");
	fprintf(stderr,"\n\n");
}

#define OUT_OF_RANGE(val) ( val < 0 ? 1 : ( val > 100 ? 1 : 0) )
#define OUT_OF_RANGE_ERR(val) if(OUT_OF_RANGE(val)) { fprintf(stderr,"\tValue must be 0-100\n"); exit(1); }

#define check_val(val,msg) {\
if(val==NULL){\
fprintf(stderr, " Invalid argument given for %s\n",msg);\
}\
}

static int set_option(const char *name, char *value)
{
    /* return 1 means error, return 0 means okay */
    int nerr = 0;
    if (strcmp(name, "port") == 0 || strcmp(name, "p") == 0) {
	info->uc->port = atoi(optarg);
    } else if (strcmp(name, "verbose") == 0 || strcmp(name, "v") == 0) {
		info->verbose = 1;
		veejay_set_debug_level(info->verbose);
		veejay_set_timestamp(1);
    } else if (strcmp(name, "no-color") == 0 || strcmp(name,"n") == 0)
	{
	 veejay_set_colors(0);
    } else if (strcmp(name, "audio") == 0 || strcmp(name, "a") == 0) {
	info->audio = atoi(optarg);
	} else if (strcmp(name, "pace-correction") == 0 ) {
	info->settings->pace_correction = atoi( optarg);
		if( info->settings->pace_correction < 0 ) {
			fprintf(stderr, "Audio pace correction must be a positive value\n");
			nerr++;
		}
    } else if ( strcmp(name, "A" ) == 0 || strcmp(name, "capture-device" ) == 0 ) {
	live = atoi(optarg);
    } else if ( strcmp(name, "Z" ) == 0 || strcmp(name, "load-generators" ) == 0 ) {
		if( sscanf( optarg, "%d",&ta ) != 1 ) {
			fprintf(stderr, "-Z/--load-generators requires an argument\n");
			nerr++;
		}
	} else if (strcmp(name, "bezerk") == 0 || strcmp(name, "b") == 0) {
	info->bezerk = 1;
	} else if (strcmp(name, "qrcode-connection-info" ) == 0 ) {
		info->qrcode = 1;
    } else if (strcmp(name, "timer") == 0 || strcmp(name, "t") == 0) {
	info->uc->use_timer = atoi(optarg);
	if (info->uc->use_timer < 0 || info->uc->use_timer > 1) {
	    fprintf(stderr, "Valid timers:\n\t0=none\n\t1=default timer\n");
	    nerr++;
	}
	} else if (strcmp(name, "multicast-vims") == 0 || strcmp(name,"T")==0)
	{
		check_val(optarg, name);
		info->settings->use_vims_mcast = 1;
        info->settings->mcast_frame_sender = 1;
		info->settings->vims_group_name = strdup(optarg);
	}
	else if (strcmp(name, "multicast-osc") == 0  || strcmp(name,"M")==0)
	{
		check_val(optarg,name);
		info->settings->use_mcast = 1;
		info->settings->group_name = strdup( optarg );
	}
	else if (strcmp(name, "max_cache" )== 0 || strcmp(name, "j" ) == 0 )
	{
		n_slots_ = atoi( optarg );
		if(n_slots_ < 0 ) n_slots_ = 0; else if (n_slots_ > 100) n_slots_ = 100;
		info->uc->max_cached_slots = n_slots_;
	}
	else if (strcmp(name, "memory" ) == 0 || strcmp(name, "m" ) == 0)
	{
		max_mem_ =  atoi(optarg);
		if(max_mem_ < 0 ) max_mem_ = 0; else if (max_mem_ > 100) max_mem_ = 100;
		info->uc->max_cached_mem = max_mem_;
    } else if (strcmp(name, "synchronization") == 0
	       || strcmp(name, "c") == 0) {
	info->sync_correction = atoi(optarg);
	} else if (strcmp(name, "version") == 0 )
	{ printf("Veejay %s\n", VERSION); exit(0); 
    } else if (strcmp(name, "graphics-driver") == 0
	       || strcmp(name, "G") == 0
	       || strcmp(name, "output") == 0
	       || strcmp(name, "O") == 0) {
	    info->video_out = atoi(optarg);	/* use SDL */
/*#ifndef HAVE_GL
            if(info->video_out==3)
	    {
		fprintf(stderr, "OpenGL support not enabled at compile time\n");
		exit(-1);
	    }
#endif
*/
	    if( info->video_out < 0 || info->video_out > 6 ) {
		    fprintf(stderr, "Select a valid output display driver\n");
		    exit(-1);
		   }
    } else if (strcmp(name, "B") == 0 || strcmp(name, "features")==0) {
	CompiledWith();
        nerr++;
	} else if ( strcmp(name, "output-file" ) == 0 || strcmp(name, "o") == 0 ) {
		check_val(optarg,name);
		veejay_strncpy(info->y4m_file,(char*) optarg, strlen( (char*) optarg));
    } else if (strcmp(name, "preserve-pathnames") == 0 ) {
		info->preserve_pathnames = 1;
	} else if (strcmp(name, "benchmark" ) == 0 ) {
		int w=0,h=0;
		int n = 0;
		if( value != NULL )
			n = sscanf(value, "%dx%d", &w,&h );
		if( n != 2 || value == NULL ) {
			fprintf(stderr,"  --benchmark parameter requires NxN argument\n");
		    	nerr++;
		} 
		if( n == 2 ) {
		    vj_mem_init(w,h);
		    benchmark_veejay(w,h);
		    exit(0);
		}
    } else if (strcmp(name, "deinterlace") == 0 || strcmp(name, "I" )==0) {
		info->auto_deinterlace = 1;
    } else if (strcmp(name, "size") == 0 || strcmp(name, "s") == 0) {
	if (sscanf(value, "%dx%d", &info->bes_width, &info->bes_height) !=
	    2) {
	    fprintf(stderr,"-s/--size parameter requires NxN argument\n");
	    nerr++;
	}
     } else if (strcmp(name,"scene-detection" ) == 0 || strcmp( name,"S") == 0 ) {
	if ((sscanf(value, "%d", &info->uc->scene_detection )) != 1 ) {
		fprintf(stderr, "-S/--scene-detection requires threshold argument\n");
		nerr++;
	}
     }
    else if (strcmp(name, "action-file")==0 || strcmp(name,"F")==0) {
	check_val(optarg,name);
	veejay_strncpy(info->action_file[0],(char*) optarg, strlen( (char*) optarg));
	info->load_action_file = 1;
	}else if (strcmp(name, "sample-file")==0 || strcmp(name,"l")==0) {
	check_val(optarg,name);
	veejay_strncpy(info->action_file[1],(char*) optarg, strlen( (char*) optarg));
	info->load_sample_file = 1;
	}
	else if (strcmp(name, "geometry-x") == 0 || strcmp(name, "x")==0) {
		default_geometry_x = atoi(optarg);
	}
	else if (strcmp(name, "geometry-y") == 0 || strcmp(name,"y")==0) {
		default_geometry_y = atoi(optarg);
	}
	else if (strcmp(name, "no-keyboard") == 0 ) {
		use_keyb = 0;
	}
    else if (strcmp(name, "borderless") == 0 ) {
        borderless = 1;
    }
	else if (strcmp(name, "no-mouse") == 0 ) {
		use_mouse = 0;
	}
	else if (strcmp(name, "show-cursor") == 0 ) {
		show_cursor = 1;
	}
	else if(strcmp(name,"dump-events")==0 || strcmp(name,"u")==0) {
	info->dump = 1;
	}
	else if(strcmp(name, "input-width") == 0 || strcmp(name, "W") == 0 ) {
		info->dummy->width = atoi(optarg);
	}
	else if(strcmp(name, "input-height") == 0 || strcmp(name, "H") == 0 ) {
		info->dummy->height = atoi(optarg);
	}
	else if(strcmp(name, "norm") == 0 || strcmp(name, "N") == 0 ) {
		int val = atoi(optarg);
		if(val == 1 )	
			override_norm = 'n';
		if(val == 0 )
			override_norm = 'p';
		if(val == 2 )
			override_norm = 's';
	}
	else if(strcmp(name, "D") == 0 || strcmp(name, "composite") == 0)
	{
		info->settings->composite = 0;
	}
	else if(strcmp(name, "output-width") == 0 || strcmp(name, "w") == 0) {
		info->video_output_width = atoi(optarg);
	}
	else if(strcmp(name, "output-height") == 0 || strcmp(name, "h") == 0) {
		info->video_output_height = atoi(optarg);
	}
	else if(strcmp(name, "audiorate") == 0 || strcmp(name, "r") == 0 )
	{
		info->dummy->arate = atoi(optarg);
	}
    	else if (strcmp(name,"fps")==0 || strcmp(name, "f")==0) {
		override_fps = atof(optarg);
	}
	else if(strcmp(name,"yuv")==0 || strcmp(name,"Y")==0)
	{
		override_pix_fmt = atoi(optarg);
		if( override_pix_fmt < 0 || override_pix_fmt > 2 )
			override_pix_fmt = 0;
	}
	else if(strcmp(name, "swap-range") == 0 || strcmp(name, "e") == 0 )
	{
		switch_jpeg = 1;
	}
	else if( strcmp(name,"auto-loop")==0 || strcmp(name,"L") == 0)
	{
		auto_loop = 1;
	}
	else if (strcmp(name, "quit") == 0 || strcmp(name, "q") == 0 )
	{
		info->continuous = 0;
	}
	else if (strcmp(name, "clip-as-sample") == 0 || strcmp(name, "g") == 0 )
	{	
		info->uc->file_as_sample = 1;
	}
	else if (strcmp(name, "dummy") == 0 || strcmp(name, "d" ) == 0 )
	{
		info->dummy->active = 1; // enable DUMMY MODE
	}
    else if (strcmp(name, "dynamic-fx-chain" ) == 0 || strcmp(name, "X" ) == 0 )
	{
		info->uc->ram_chain = 0;
	}
	else if (strcmp(name, "split-screen" ) == 0 )
	{
		info->settings->splitscreen = 1;
	}
    else if (strcmp(name, "fx-custom-default-values" ) == 0 )
    {
        info->read_plug_cfg = 1;
    }
    else if (strcmp(name, "help" ) == 0 || strcmp(name, "?") == 0 ) 
    {
        nerr++;
    }
    else {
		nerr++;			/* unknown option - error */
    }
    return nerr;
}

static int check_command_line_options(int argc, char *argv[])
{
    int nerr, n, option_index = 0;
    char option[2];
#ifdef HAVE_GETOPT_LONG
    /* getopt_long options */
    static struct option long_options[] = {
	{"verbose", 0, 0, 0},	/* -v/--verbose         */
	{"skip", 1, 0, 0},	/* -s/--skip            */
	{"synchronization", 1, 0, 0},	/* -c/--synchronization */
	{"preserve-pathnames", 0, 0, 0},	/* -P/--preserve-pathnames    */
	{"audio", 1, 0, 0},	/* -a/--audio num       */
	{"size", 1, 0, 0},	/* -S/--size            */
	{"benchmark", 1, 0, 0}, /* --benchmark	 */
/*#ifdef HAVE_XINERAMA
#ifndef X_DISPLAY_MISSING
	{"xinerama",1,0,0},
#endif
#endif
*/
	{"graphics-driver", 1, 0, 0},
	{"timer", 1, 0, 0},	/* timer */
	{"dump-events",0,0,0},
	{"bezerk",0,0,0},
	{"sample-file",1,0,0},
	{"action-file",1,0,0},
	{"features",0,0,0},
	{"deinterlace",0,0,0},
	{"clip-as-sample",0,0,0},
	{"port", 1, 0, 0},
	{"sample-mode",1,0,0},
	{"dummy",0,0,0},
	{"geometry-x",1,0,0},
	{"geometry-y",1,0,0},
	{"no-keyboard",0,0,0},
	{"no-mouse",0,0,0},
    {"borderless",0,0,0},
	{"show-cursor",0,0,0},
	{"auto-loop",0,0,0},
	{"fps",1,0,0},
	{"no-color",0,0,0},
	{"version",0,0,0},
	{"input-width",1,0,0},
	{"input-height",1,0,0},
	{"output-width", 1,0,0 },
	{"output",1,0,0},
	{"output-file",1,0,0},
	{"output-height", 1,0,0 },
	{"norm",1,0,0},
	{"audiorate",1,0,0},
	{"yuv",1,0,0},
	{"multicast-osc",1,0,0},
	{"multicast-vims",1,0,0},
	{"composite",0,0,0},
	{"quit",0,0,0},
	{"memory",1,0,0},
	{"max_cache",1,0,0},
	{"capture-device",1,0,0},
	{"swap-range",0,0,0},
	{"load-generators",1,0,0},
	{"qrcode-connection-info",0,0,0},
	{"scene-detection",1,0,0},
	{"dynamic-fx-chain",0,0,0},
	{"pace-correction",1,0,0},
	{"split-screen",0,0,0},
    {"fx-custom-default-values",0,0,0},
    {"help",0,0,0},
	{0, 0, 0, 0}
    };
#endif
    if (argc < 2) {
	Usage(argv[0]);
	return 0;
    }
	
    nerr = 0;
#ifdef HAVE_GETOPT_LONG
    while ((n =
	    getopt_long(argc, argv,
			"o:G:O:a:H:s:c:t:j:l:p:m:h:w:x:y:r:f:Y:A:N:H:W:T:F:Z:nILPVDugvBdibjqeM:S:X?",
			long_options, &option_index)) != EOF)
#else
    while ((n =
	    getopt(argc, argv,
		   	"o:G:O:a:H:s:c:t:j:l:p:m:h:w:x:y:r:f:Y:A:N:H:W:T:F:Z:nILPVDugvBdibjqeM:S:X?"
						   )) != EOF)
#endif
    {
	switch (n) {
#ifdef HAVE_GETOPT_LONG
	case 0:
	    nerr += set_option(long_options[option_index].name, optarg);
	    break;
#endif

	default:
	    sprintf(option, "%c", n);
	    nerr += set_option(option, optarg);
	    break;
	}
	
    }
    if (optind > argc)
	nerr++;

    if (nerr) {
        Usage(argv[0]);
        return 0;
    }

    if(!nerr) 
		return 1;
	return 0;
}

static void print_license(void)
{
	veejay_msg(VEEJAY_MSG_INFO,
	    "Veejay -<|Classic +|>- %s Copyright (C) Niels Elburg and others",VERSION);
	veejay_msg(VEEJAY_MSG_INFO,    
		"Build for %s/%s arch %s on %s",
	    BUILD_OS,
	    BUILD_KERNEL,
	    BUILD_MACHINE,
	    BUILD_DATE );

	veejay_msg(VEEJAY_MSG_INFO,
	    "This software is subject to the GNU GENERAL PUBLIC LICENSE");

	vj_avcodec_print_version();

}

static void	veejay_backtrace_handler(int n , siginfo_t *si, void *ptr)
{
	switch(n) {
		case SIGSEGV:
			veejay_msg(VEEJAY_MSG_ERROR,"Found Gremlins in your system"); //@ Suggested by Matthijs
			veejay_msg(VEEJAY_MSG_WARNING, "No fresh ale found in the fridge!"); //@
			veejay_msg(VEEJAY_MSG_INFO, "Running with sub-atomic precision..."); //@

			veejay_print_backtrace();
			break;
		default:
			veejay_print_backtrace();
			break;
	}

	//@ Bye
	veejay_msg(VEEJAY_MSG_ERROR, "Bugs compromised the system.");

	report_bug();

	exit(EX_SOFTWARE);
}

static void	sigsegfault_handler(void) {
	struct sigaction sigst;
	sigemptyset(&sigst.sa_mask);
	sigaddset(&sigst.sa_mask, SIGSEGV );
	sigst.sa_flags = SA_SIGINFO | SA_ONESHOT;
	sigst.sa_sigaction = &veejay_backtrace_handler;

	if( sigaction(SIGSEGV, &sigst, NULL) == - 1) 
		veejay_msg(0,"sigaction");
}

int main(int argc, char **argv)
{
    video_playback_setup *settings;
    int main_ret = 0;
    
    fflush(stdout);

    vj_mem_init(0, 0);
    vj_mem_optimize();
    vevo_strict_init();

    info = veejay_malloc();
    if (!info) {
        return 1;
    }

    settings = (video_playback_setup *)info->settings;

    if (!check_command_line_options(argc, argv)) {
        veejay_free(info);
        return 0;
    }



    if (info->dump) {
        veejay_set_colors(0);
		veejay_set_timestamp(0);
		veejay_set_label(0);
        vj_event_init(NULL);
        vje_init(info->video_output_width <= 0 ? 720 : info->video_output_width, info->video_output_height <= 0 ? 576 : info->video_output_width);
		vj_osc_allocate(VJ_PORT + 2);
        vj_event_dump();
        vje_dump();
		fprintf(stdout,
            "\n\nEnvironment variables:\n"
            "  [ Video & Rendering ]\n"
            "\tSDL_VIDEO_HWACCEL\t\tSet to 1 to use SDL video hardware accel (default=on)\n"
            "\tVEEJAY_PERFORMANCE\t\tSet to \"quality\" or \"fastest\" (default=fastest)\n"
            "\tVEEJAY_AUTO_SCALE_PIXELS\tSet to 1 to convert CCIR 601 / JPEG automatically\n"
            "\tVEEJAY_INTERPOLATE_CHROMA\tSet to 1 to interpolate chroma samples when scaling\n"
            "\tVEEJAY_ORIGINAL_FRAME\t\tForce use of original frame in composition\n"
            "\tVEEJAY_SUBSAMPLE_MODE\t\tSet subsampling algorithm mode\n"
            "\tVEEJAY_SUPERSAMPLE_MODE\t\tSet supersampling algorithm mode\n"
            "\tVEEJAY_BG_AUTO_HISTOGRAM_EQ\tEnable auto histogram equalization for bg-subtraction\n"
            "\n"
            "  [ Display & UI ]\n"
            "\tVEEJAY_FULLSCREEN\t\tStart in fullscreen (1) or windowed (0) mode\n"
            "\tVEEJAY_VIDEO_POSITION\t\tPosition of video window\n"
            "\tVEEJAY_VIDEO_SIZE\t\tSize of video window\n"
            "\tVEEJAY_DESKTOP_GEOMETRY\tSpecify geometry for video window positioning\n"
            "\tVEEJAY_SDL_DRIVER\t\tSpecify SDL video driver to use\n"
            "\tVEEJAY_SDL_HINT_RENDER_DRIVER\tSet SDL_HINT_RENDER_DRIVER\n"
            "\tVEEJAY_SDL_KEY_REPEAT_DELAY\tDelay key repeat in ms\n"
            "\tVEEJAY_SDL_KEY_REPEAT_INTERVAL\tInterval of key repeat while pressed down\n"
            "\n"
            "  [ System & Streaming ]\n"
            "\tVEEJAY_MAX_FILESIZE\t\tMaximum allowed file size for processing\n"
            "\tVEEJAY_NUM_DECODE_THREADS\tNumber of threads for avcodec decoding\n"
            "\tVEEJAY_MULTITHREAD_TASKS\tNumber of parallel tasks (default=CPU cores)\n"
            "\tVEEJAY_AV_LOG\t\t\tSet libavcodec logging level\n"
            "\tVEEJAY_LOG_NET_IO\t\tIf set, enable network I/O logging\n"
            "\tVEEJAY_MMAP_PER_FILE\t\tEnable mmap allocation per file (in Kb)\n"
            "\tVEEJAY_VLOOPBACK_PIXELFORMAT\tSet pixel format for vloopback streaming\n"
            "\tVEEJAY_V4L2_CAPTURE_METHOD\tSpecify V4L2 capture method\n"
            "\tVEEJAY_V4L2_NO_THREADING\tSet to 1 to query frame in main-loop\n"
            "\n"
            "  Example for bash:\n"
            "\t$ export VEEJAY_AUTO_SCALE_PIXELS=1\n"
            "\t$ export VEEJAY_NUM_DECODE_THREADS=4\n\n"
        );

        return 0;
    }
	
	veejay_print_banner();
    print_license();
	
    veejay_check_homedir(info);

    sigsegfault_handler();
    
	signal(SIGPIPE, SIG_IGN);

	struct { int signo; } safe_signals[] = {
		{ SIGINT }, { SIGQUIT }, { SIGFPE },
		{ SIGTERM }, { SIGABRT }, { SIGILL }, { SIGPWR }
	};

	for (int i = 0; i < sizeof(safe_signals)/sizeof(safe_signals[0]); i++) {
		struct sigaction sa = {0};
		sa.sa_sigaction = veejay_handle_signal;
		sa.sa_flags = SA_SIGINFO | SA_ONESHOT;
		sigemptyset(&sa.sa_mask);
		sigaction(safe_signals[i].signo, &sa, NULL);
	}

    veejay_msg(VEEJAY_MSG_INFO, "CPU cache line size: %d bytes", cpu_get_cacheline_size());
    veejay_msg(VEEJAY_MSG_INFO, "Memory page size: %d bytes", mem_align_size());

    info->use_keyb = use_keyb;
    info->use_mouse = use_mouse;
    info->show_cursor = show_cursor;
    info->borderless = borderless;


	if(!info->dump) {

       if(veejay_open_files(
			info,
			argv + optind,
			argc - optind,
			override_fps,
			force_video_file,
			override_pix_fmt,
			override_norm,
			switch_jpeg )<=0)
       {
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to open video file(s)");
			main_ret = 1;
			goto VEEJAY_MAIN_EXIT;
       }
	
	}

    if (veejay_init(info, default_geometry_x, default_geometry_y, NULL, live, ta) < 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Cannot start Veejay");
        main_ret = 1;
        goto VEEJAY_MAIN_EXIT;
    }

    if (auto_loop) veejay_auto_loop(info);

    veejay_init_msg_ring();  // logging

    if (veejay_setup_video_out(info) != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to setup output driver");
        main_ret = 1;
		goto VEEJAY_MAIN_EXIT;
    }
	
    if (!veejay_main(info)) {
        veejay_msg(VEEJAY_MSG_ERROR, "Cannot start main playback cycle");
        main_ret = 1;
        goto VEEJAY_MAIN_EXIT;
    }

	settings->display_frame.pixels[0] = vj_sdl_get_buffer(info->sdl,0);
	settings->display_frame.pixels[1] = vj_sdl_get_buffer(info->sdl,1);

    while (atomic_load_int(&settings->first_audio_frame_ready) == 0) {
        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP)
            break;
        usleep_accurate(100, settings);
    }

	long long last_seq = -1;

    while (veejay_get_state(info) != LAVPLAY_STATE_STOP) {
        
		veejay_event_handle(info);

		long long seq = atomic_consume(&settings->display_frame.seq);
		if( seq != last_seq ) {
			display_frame_t *df = &settings->display_frame;

			int read_index = df->current_write;
			uint8_t *pixels_to_render = df->pixels[read_index];

			vj_sdl_put_to_screen(info->sdl, pixels_to_render);
		}

   		usleep_accurate(5000,settings);

    }

    veejay_msg(VEEJAY_MSG_INFO, "Thank you for using Veejay");

    veejay_close(info);

VEEJAY_MAIN_EXIT:
    veejay_busy(info);
    veejay_free(info);
    veejay_destroy_msg_ring();

    return main_ret;
}
