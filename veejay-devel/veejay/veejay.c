/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
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
#define VJ_PROMPT "$> "

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <veejay/vj-lib.h>
#include <veejay/vj-event.h>
#include <veejay/libveejay.h>
#include <libvje/vje.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vj-global.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <unistd.h>

static int run_server = 1;
static veejay_t *info;
static float override_fps = 0.0;
static int default_geometry_x = -1;
static int default_geometry_y = -1;
static int force_video_file = 0; // unused
static int override_pix_fmt = -1;
static char override_norm = 'p';
static int auto_loop = 0;

static void CompiledWith()
{
	veejay_msg(VEEJAY_MSG_INFO,"Compilation flags:");
#ifdef USE_GDK_PIXBUF
	veejay_msg(VEEJAY_MSG_INFO,"\tUse GDK Pixbuf");
#endif
#ifdef USE_SWSCALER
	veejay_msg(VEEJAY_MSG_INFO,"\tUse software scaler");
#endif
#ifdef HAVE_JACK
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing Jack audio server");
#endif
#ifdef HAVE_V4L
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing Video4linux");
#endif
#ifdef SUPPORT_READ_DV2
	veejay_msg(VEEJAY_MSG_INFO,"\tSupport for Digital Video enabled");
#endif
#ifdef HAVE_XML2
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing XML library for Gnome");
#endif
#ifdef SUPPORT_READ_DV2
	veejay_msg(VEEJAY_MSG_INFO,"\tSupport for Digital Video enabled");
#endif
#ifdef HAVE_SDL
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing Simple Direct Media Layer");
#endif
#ifdef USE_GTKCAIRO
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing GTK Cairo");
#endif
#ifdef HAVE_DIRECTFB
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing DirectFB");
#endif
#ifdef HAVE_FREETYPE
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing Freetype");
#endif
#ifdef HAVE_X86CPU
	veejay_msg(VEEJAY_MSG_INFO,"\tCompiled for x86 architecture");
#endif
#ifdef HAVE_PPCCPU
	veejay_msg(VEEJAY_MSG_INFO,"\tCompiled for PPC architecture");
#endif
#ifdef HAVE_ASM_SSE
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing SSE instruction set");
#endif
#ifdef HAVE_CMOV
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing CMOV");
#endif
#ifdef HAVE_ASM_SSE2
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing SSE2 instruction set");
#endif
#ifdef HAVE_ASM_MMX
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing MMX instruction set");
#endif
#ifdef HAVE_ASM_3DNOW
	veejay_msg(VEEJAY_MSG_INFO,"\tUsing 3Dnow instruction set");
#endif
	exit(0);
}

static void Usage(char *progname)
{
    fprintf(stderr, "Usage: %s [options] <filename> [<filename> ...]\n",
	    progname);
    fprintf(stderr, "where options are:\n");

    fprintf(stderr,
	    "  -p/--portoffset\t\t\tTCP port to accept/send messages (default: 3490)\n");
    fprintf(stderr,
	    "  -t/--timer num\t\tspecify timer to use (none:0,normal:2,rtc:1) default is 1\n");

#ifdef HAVE_DIRECTFB
    fprintf(stderr,
	    "  -O/--output\t\t\tSDL(0) , DirectFB(1), SDL and DirectFB(2), YUV4MPEG stream (3)\n");
#else
    fprintf(stderr, "  -O/--output\t\t\tSDL(0), (3) yuv4mpeg (4) SHM (broken) (5) no visual\n");
#endif
    	fprintf(stderr,
	    "  -o/--outstream <filename>\twhere to write the yuv4mpeg stream (use with -O3)\n");
    	fprintf(stderr,
	    "  -c/--synchronization [01]\tSync correction off/on (default on)\n");
    	fprintf(stderr, "  -f/--fps num\t\t\tOverride default framerate (default read from file)\n");
    	fprintf(stderr,
	    "  -P/--preserve-pathnames\tDo not 'canonicalise' pathnames in editlists\n");
    	fprintf(stderr,
	    "  -a/--audio [01]\t\tEnable (1) or disable (0) audio (default 1)\n");
    	fprintf(stderr,
	    "  -s/--size NxN\t\t\twidth X height for SDL video window\n");
	fprintf(stderr,
	    "  -l/--action-file <filename>\tLoad an Configuartion/Action File (none at default)\n");
	fprintf(stderr,
	    "  -u/--dump-events  \t\tDump event information to screen\n");
	fprintf(stderr,
	    "  -I/--deinterlace\t\tDeinterlace video if it is interlaced\n");    
	fprintf(stderr,"  -x/--geometryx <num> \t\tTop left x offset for SDL video window\n");
	fprintf(stderr,"  -y/--geometryy <num> \t\tTop left y offset for SDL video window\n");
 	fprintf(stderr,"  -F/--features  \t\tList of compiled features\n");
	fprintf(stderr,"  -v/--verbose  \t\tEnable debugging output (default off)\n");
	fprintf(stderr,"  -b/--bezerk    \t\tBezerk (default off)   \n");
 	fprintf(stderr,"  -L/--auto-loop   \t\tStart with default sample\n");
	fprintf(stderr,"  -g/--clip-as-sample	\t\tLoad every video clip as a new sample\n");	
	fprintf(stderr,"  -n/--no-color     \t\tDont use colored text\n");
	fprintf(stderr,"  -r/--audiorate	\t\tDummy audio rate\n");
	fprintf(stderr,"  -m/--sample-mode [01]\t\tSampling mode 1 = best quality (default), 0 = best performance\n");  
	fprintf(stderr,"  -Y/--ycbcr [01]\t\t0 = YUV 4:2:0 Planar, 1 = YUV 4:2:2 Planar\n");

	fprintf(stderr,"  -d/--dummy	\t\tDummy playback\n");
	fprintf(stderr,"  -W/--width <num>\t\tdummy width\n");
	fprintf(stderr,"  -H/--height <num>\t\tdummy height\n");

	fprintf(stderr,"  -N/--norm [0=PAL, 1=NTSC (defaults to PAL)]\t\tdummy norm , PAL or NTSC\n");
	fprintf(stderr,"  -R/--framerate <num>\t\tdummy frame rate\n");
	fprintf(stderr,"  -M/--multicast-osc \t\tmulticast OSC\n");
	fprintf(stderr,"  -V/--multicast-vims \t\tmulticast VIMS\n");
	fprintf(stderr,"     --map-from-file <num>\tmap N frames to memory\n");
#ifdef USE_SWSCALER
	fprintf(stderr,"  -z/--zoom [1-11]\n");
	fprintf(stderr,"\t\t\t\tsoftware scaler type (also use -W, -H ). \n");
	fprintf(stderr,"\t\t\t\tAvailable types are:\n");         
	fprintf(stderr,"\t\t\t\t1\tFast bilinear (default)\n");
	fprintf(stderr,"\t\t\t\t2\tBilinear\n");
	fprintf(stderr,"\t\t\t\t3\tBicubic (good quality)\n");
	fprintf(stderr,"\t\t\t\t4\tExperimental\n");
	fprintf(stderr,"\t\t\t\t5\tNearest Neighbour (bad quality)\n");
	fprintf(stderr,"\t\t\t\t6\tArea\n");
	fprintf(stderr,"\t\t\t\t7\tLuma bicubic / chroma bilinear\n");
	fprintf(stderr,"\t\t\t\t9\tGauss\n");
	fprintf(stderr,"\t\t\t\t9\tsincR\n");
	fprintf(stderr,"\t\t\t\t10\tLanczos\n");
	fprintf(stderr,"\t\t\t\t11\tNatural bicubic spline\n");
	fprintf(stderr,"\n\t\t\t\tsoftware scaler options:\n");
	fprintf(stderr,"\t\t\t\t--lgb=<0-100>\tGaussian blur filter (luma)\n");
	fprintf(stderr,"\t\t\t\t--cgb=<0-100>\tGuassian blur filter (chroma)\n");
	fprintf(stderr,"\t\t\t\t--ls=<0-100>\tSharpen filter (luma)\n");
	fprintf(stderr,"\t\t\t\t--cs=<0-100>\tSharpen filter (chroma)\n");
	fprintf(stderr,"\t\t\t\t--chs=<h>\tChroma horizontal shifting\n");
	fprintf(stderr,"\t\t\t\t--cvs=<v>\tChroma vertical shifting\n");
	fprintf(stderr,"\t\t\t\t-w/--zoomwidth \n");
	fprintf(stderr,"\t\t\t\t-h/--zoomheight \n");
	fprintf(stderr,"\t\t\t\t-C/--zoomcrop [top:bottom:left:right] (crop source before scaling)\n");
#endif
	fprintf(stderr,"  -q/--quit \t\t\tQuit at end of file\n");
	fprintf(stderr,"\n\n");
}

#define OUT_OF_RANGE(val) ( val < 0 ? 1 : ( val > 100 ? 1 : 0) )

#define OUT_OF_RANGE_ERR(val) if(OUT_OF_RANGE(val)) { fprintf(stderr,"\tValue must be 0-100\n"); exit(1); }

#define check_val(val,msg) {\
char *v = strdup(val);\
if(v==NULL){\
fprintf(stderr, " Invalid argument given for %s\n",msg);\
}\
else\
{\
free(v);\
}\
}
static int set_option(const char *name, char *value)
{
    /* return 1 means error, return 0 means okay */
    int nerr = 0;
    if (strcmp(name, "portoffset") == 0 || strcmp(name, "p") == 0) {
	info->uc->port = atoi(optarg);
    } else if (strcmp(name, "verbose") == 0 || strcmp(name, "v") == 0) {
	info->verbose = 1;
    } else if (strcmp(name, "no-color") == 0 || strcmp(name,"n") == 0)
	{
	 veejay_set_colors(0);
    } else if (strcmp(name, "audio") == 0 || strcmp(name, "a") == 0) {
	info->audio = atoi(optarg);
    } else if (strcmp(name, "bezerk") == 0 || strcmp(name, "b") == 0) {
	info->no_bezerk = 0;
    } else if (strcmp(name, "timer") == 0 || strcmp(name, "t") == 0) {
	info->uc->use_timer = atoi(optarg);
	if (info->uc->use_timer < 0 || info->uc->use_timer > 2) {
	    printf("Valid timers:\n\t0=none\n\t2=normal\n\t1=rtc\n");
	    nerr++;
	}
	} else if (strcmp(name, "multicast-vims") == 0 || strcmp(name,"V")==0)
	{
		check_val(optarg, name);
		info->settings->use_vims_mcast = 1;
		info->settings->vims_group_name = strdup(optarg);
	}
	else if (strcmp(name, "multicast-osc") == 0 || strcmp(name, "M") == 0 )
	{
		check_val(optarg,name);
		info->settings->use_mcast = 1;
		info->settings->group_name = strdup( optarg );
	} else if (strcmp(name, "sample-mode" ) == 0 || strcmp(name, "m" ) == 0)
	{
		veejay_set_sampling( info,atoi(optarg));
    } else if (strcmp(name, "synchronization") == 0
	       || strcmp(name, "c") == 0) {
	info->sync_correction = atoi(optarg);
	} else if (strcmp(name, "version") == 0 )
	{ printf("Veejay %s\n", VERSION); exit(0); 
    } else if (strcmp(name, "graphics-driver") == 0
	       || strcmp(name, "G") == 0
	       || strcmp(name, "output-driver") == 0
	       || strcmp(name, "O") == 0) {
	    info->video_out = atoi(optarg);	/* use SDL */
    } else if (strcmp(name, "F") == 0 || strcmp(name, "features")==0) {
	CompiledWith();
        nerr++;
    } else if (strcmp(name, "preserve-pathnames") == 0
	       || strcmp(name, "P") == 0) {
	info->preserve_pathnames = 1;
    } else if (strcmp(name, "deinterlace") == 0 || strcmp(name, "I" )==0) {
	info->auto_deinterlace = 1;
    } else if (strcmp(name, "size") == 0 || strcmp(name, "s") == 0) {
	if (sscanf(value, "%dx%d", &info->bes_width, &info->bes_height) !=
	    2) {
	    mjpeg_error("--size parameter requires NxN argument");
	    nerr++;
	}
     }
    else if (strcmp(name, "outstream") == 0 || strcmp(name, "o") == 0) {
	check_val(optarg,name);
	snprintf(info->stream_outname,256,"%s", (char*) optarg);
#ifdef HAVE_XML2
    } else if (strcmp(name, "action-file")==0 || strcmp(name,"l")==0) {
	check_val(optarg,name);
	strcpy(info->action_file,(char*) optarg);
	info->load_action_file = 1;
#endif
	}
	else if(strcmp(name,"map-from-file") == 0 ) {
		info->seek_cache = atoi(optarg);
	}
	else if (strcmp(name, "geometry-x") == 0 || strcmp(name, "x")==0) {
		default_geometry_x = atoi(optarg);
	}
	else if (strcmp(name, "geometry-y") == 0 || strcmp(name,"y")==0) {
		default_geometry_y = atoi(optarg);
	}
	else if(strcmp(name,"dump-events")==0 || strcmp(name,"u")==0) {
	info->dump = 1;
	}
	else if(strcmp(name, "width") == 0 || strcmp(name, "W") == 0 ) {
		info->dummy->width = atoi(optarg);
	}
	else if(strcmp(name, "height") == 0 || strcmp(name, "H") == 0 ) {
		info->dummy->height = atoi(optarg);
	}
	else if(strcmp(name, "norm") == 0 || strcmp(name, "N") == 0 ) {
		info->dummy->norm = optarg[0];
		if(info->dummy->norm == 1 )	
			override_norm = 'n';
	}
	else if(strcmp(name, "zoomwidth") == 0 || strcmp(name, "w") == 0) {
		info->video_output_width = atoi(optarg);
	}
	else if(strcmp(name, "zoomheight") == 0 || strcmp(name, "h") == 0) {
		info->video_output_height = atoi(optarg);
	}
	else if(strcmp(name, "audiorate") == 0 || strcmp(name, "r") == 0 )
	{
		info->dummy->arate = atoi(optarg);
	}
	else if(strcmp(name, "framerate") == 0 || strcmp(name, "R" ) == 0 ) {
		info->dummy->fps = atof(optarg);
	}
    else if (strcmp(name,"fps")==0 || strcmp(name, "f")==0) {
	override_fps = atof(optarg);
	}
	else if(strcmp(name,"ycbcr")==0 || strcmp(name,"Y")==0)
	{
		override_pix_fmt = atoi(optarg);
	}
	else if( strcmp(name,"auto-loop")==0 || strcmp(name,"L") == 0)
	{
		auto_loop = 1;
	}
#ifdef USE_SWSCALER 
	else if (strcmp(name, "zoom") == 0 || strcmp(name, "z" ) == 0)
	{
		info->settings->zoom = atoi(optarg);
		if(info->settings->zoom < 1 || info->settings->zoom > 11)
		{
			fprintf(stderr, "Use --zoom [1-11] or -z [1-11]\n");
			nerr++;
		}
	}
	else if (strcmp(name, "lgb") == 0) 	
	{
		info->settings->sws_templ.lumaGBlur = (float)atof(optarg);
		OUT_OF_RANGE_ERR( info->settings->sws_templ.lumaGBlur);
		info->settings->sws_templ.use_filter = 1;
	}
	else if (strcmp(name, "cgb") == 0)
	{
		info->settings->sws_templ.chromaGBlur = (float)atof(optarg);
		OUT_OF_RANGE_ERR( info->settings->sws_templ.chromaGBlur );
		info->settings->sws_templ.use_filter = 1;

	}
	else if (strcmp(name, "ls") == 0)
	{
		info->settings->sws_templ.lumaSarpen = (float) atof(optarg);
		OUT_OF_RANGE_ERR( info->settings->sws_templ.lumaSarpen);
		info->settings->sws_templ.use_filter = 1;

	}
	else if (strcmp(name, "cs") == 0)
	{
		info->settings->sws_templ.chromaSharpen = (float) atof(optarg);
		OUT_OF_RANGE_ERR(info->settings->sws_templ.chromaSharpen);
		info->settings->sws_templ.use_filter = 1;

	}	
	else if (strcmp(name, "chs") == 0)
	{
		info->settings->sws_templ.chromaHShift = (float) atof(optarg);
		OUT_OF_RANGE_ERR(info->settings->sws_templ.chromaHShift );
		info->settings->sws_templ.use_filter = 1;

	}
	else if (strcmp(name, "cvs") == 0)
	{
		info->settings->sws_templ.chromaVShift = (float) atof(optarg);
		OUT_OF_RANGE_ERR(info->settings->sws_templ.chromaVShift );
		info->settings->sws_templ.use_filter = 1;

	}	
	else if (strcmp(name, "C") == 0 || strcmp(name, "zoomcrop") == 0 )
	{
		if (sscanf(value, "%d:%d:%d:%d", &(info->settings->viewport.top),
						 &(info->settings->viewport.bottom),
						 &(info->settings->viewport.left),
						 &(info->settings->viewport.right)) < 4)
		{
			fprintf(stderr, "Crop requires top:bottom:left:right\n");
			nerr++;
		}
		info->settings->crop = 1;
	}
#endif
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
    else
	nerr++;			/* unknown option - error */

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
	{"graphics-driver", 1, 0, 0},
	{"timer", 1, 0, 0},	/* timer */
	{"dump-events",0,0,0},
	{"bezerk",0,0,0},
	{"outstream", 1, 0, 0},
	{"action-file",1,0,0},
	{"features",0,0,0},
	{"deinterlace",0,0,0},
	{"zoom",1,0,0},
	{"clip-as-sample",0,0,0},
	{"portoffset", 1, 0, 0},
	{"sample-mode",1,0,0},
	{"dummy",0,0,0},
	{"geometry-x",1,0,0},
	{"geometry-y",1,0,0},
	{"auto-loop",0,0,0},
	{"fps",1,0,0},
	{"no-color",0,0,0},
	{"version",0,0,0},
	{"width",1,0,0},
	{"height",1,0,0},
	{"zoomwidth", 1,0,0 },
	{"zoomheight", 1,0,0 },
	{"norm",1,0,0},
	{"framerate",1,0,0},
	{"audiorate",1,0,0},
	{"ycbcr",1,0,0},
	{"multicast-osc",1,0,0},
	{"multicast-vims",1,0,0},
	{"map-from-file",1,0,0},
	{"zoomcrop",1,0,0},
	{"lgb",1,0,0},
	{"cgb",1,0,0},
	{"ls",1,0,0},
	{"cs",1,0,0},
	{"chs",1,0,0},
	{"cvs",1,0,0},
	{"quit",0,0,0},
	{0, 0, 0, 0}
    };
#endif
    if (argc < 2) {
	Usage(argv[0]);
	return 0;
    }
    
/* Get options */
    nerr = 0;
#ifdef HAVE_GETOPT_LONG
    while ((n =
	    getopt_long(argc, argv,
			"o:G:O:a:H:V:s:c:t:l:p:m:x:y:nLFPY:ugr:vdibIjf:N:H:W:R:M:V:z:qw:h:C:",
			long_options, &option_index)) != EOF)
#else
    while ((n =
	    getopt(argc, argv,
		   "o:G:s:O:a:c:t:l:t:x:y:m:p:nLFPY:vudgibr:Ijf:N:H:W:R:M:V:z:qw:h:C:")) != EOF)
#endif
    {
	switch (n) {
#ifdef HAVE_GETOPT_LONG
	    /* getopt_long values */
	case 0:
	    nerr += set_option(long_options[option_index].name, optarg);
	    break;
#endif

	    /* These are the old getopt-values (non-long) */
	default:
	    sprintf(option, "%c", n);
	    nerr += set_option(option, optarg);
	    break;
	}
	
    }
    if (optind > argc)
	nerr++;

    if (nerr)
	Usage(argv[0]);

    veejay_set_debug_level(info->verbose);

    if(info->video_out == 3)
    {
	veejay_silent();
	mjpeg_default_handler_verbosity( 0 );	
    }
    else
    {
    	mjpeg_default_handler_verbosity( (info->verbose ? 1:0) );
    }
    if(!info->dump)
       if(veejay_open_files(info, argv + optind, argc - optind,override_fps, force_video_file, override_pix_fmt, override_norm )<=0)
       {
	vj_el_show_formats();
	veejay_msg(VEEJAY_MSG_ERROR, "Cannot start veejay");
	nerr++;
       }
    if(!nerr) 
	return 1;
	return 0;
}

static void print_license()
{
	veejay_msg(VEEJAY_MSG_INFO,
	    "Veejay -<|End of the Line 2k6 beta 1|>- %s Copyright (C) Niels Elburg and others",VERSION);
	veejay_msg(VEEJAY_MSG_INFO,
	    "This software is subject to the GNU GENERAL PUBLIC LICENSE");

	veejay_msg(VEEJAY_MSG_INFO,
	    "Veejay comes with ABSOLUTELY NO WARRANTY; this is free software and");
	veejay_msg(VEEJAY_MSG_INFO,
		"you are welcome to redistribute it under certain conditions.");
	veejay_msg(VEEJAY_MSG_INFO,
	    "The license must be included in the (source) package (COPYING)");
}

static void smp_check()
{
	int n_cpu = get_nprocs();
	int c_cpu = get_nprocs_conf();
	
	if(n_cpu == c_cpu)
	{
		if(c_cpu>1) veejay_msg(VEEJAY_MSG_INFO, "Running on Multiple procesors");
	}
	else
	{
		veejay_msg(VEEJAY_MSG_WARNING, "You have %d CPU's but your system is configured for only %d",
			n_cpu, c_cpu);
	}
}

int main(int argc, char **argv)
{
    video_playback_setup *settings;
    char *dont_use; 
    
    struct sched_param schp;
    /*EditList *editlist = info->editlist; */

	fflush(stdout);
    vj_mem_init();

    info = veejay_malloc();
    /* start with initing */
    if (!info)
	return 1;
     settings = (video_playback_setup *) info->settings;

    if(!check_command_line_options(argc, argv))
    {
	veejay_free(info);
	return 0;
    }

    print_license();

   

    if(info->dump)
 	{
		veejay_set_colors(0);
		vj_event_init();
		vj_effect_initialize(720,576);
		vj_osc_allocate(VJ_PORT+2);	
		vj_event_dump();
		vj_effect_dump();
		veejay_free(info);
		return 0;
	}

    //print_license();
  /* setup SIGPIPE and SIGINT catcher as a thread */
    sigemptyset(&(settings->signal_set));
    sigaddset(&(settings->signal_set), SIGINT);
    sigaddset(&(settings->signal_set), SIGPIPE);
    sigaddset(&(settings->signal_set), SIGBUS);
    sigaddset(&(settings->signal_set), SIGILL);
    sigaddset(&(settings->signal_set), SIGSEGV);

    pthread_sigmask(SIG_BLOCK, &(settings->signal_set), NULL);
    pthread_create(&(settings->signal_thread), NULL, veejay_signal_loop,
		   (void *) info); 

	dont_use = getenv("VEEJAY_SCHEDULE_NORMAL");
	if(dont_use==NULL || strcmp(dont_use, "0")==0||strcmp(dont_use,"no")==0)
	{
 	 mymemset_generic(&schp, 0, sizeof(schp));
    	schp.sched_priority = sched_get_priority_max(SCHED_FIFO);
   	 if (sched_setscheduler(0, SCHED_FIFO, &schp) != 0)
	 {
		veejay_msg(VEEJAY_MSG_ERROR,
			    "Cannot set real-time playback thread scheduling (not root?)");
   	 }
	 else
	 {
		veejay_msg(VEEJAY_MSG_WARNING,
		    "Using First In-First Out II scheduling");
	 }
	}

	smp_check();


	char *mem_func = get_memcpy_descr();
	if(mem_func)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Using SIMD %s", mem_func);
		free(mem_func);
	}

    if(veejay_init(
		info,
		default_geometry_x,
		default_geometry_y,
		NULL,
		1)<0)
	{	
		veejay_msg(VEEJAY_MSG_ERROR, "Initializing veejay");
		return 0;
	}

	if(auto_loop)
		veejay_auto_loop(info);

    if(!veejay_main(info))
	{
	    veejay_msg(VEEJAY_MSG_ERROR, "Cannot start main playback cycle");
		return 1;
	}

	//veejay_set_frame(info, 0);
	veejay_msg(VEEJAY_MSG_DEBUG, "Starting playback");
	veejay_change_state(info, LAVPLAY_STATE_PLAYING);
	veejay_set_frame(info, 0);

	veejay_set_speed(info, 1);
	  
    while (veejay_get_state(info) != LAVPLAY_STATE_STOP) 
    {
       usleep(400000);
    }

    veejay_quit(info);
    veejay_busy(info);		/* wait for all the nice goodies to shut down */
    veejay_free(info);

    return 0;
}
