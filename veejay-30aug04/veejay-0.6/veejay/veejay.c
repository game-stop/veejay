/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <elburg@hio.hen.nl> 
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
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include "vj-lib.h"
#include "vj-event.h"
#include "libveejay.h"
#include "vj-common.h"
#include "vj-global.h"
#include <sys/mman.h>

static int skip_seconds = 0;
static int current_frame = 0;
static char _vjdir[1024];
static int run_server = 1;
static veejay_t *info;
static int default_use_tags=1;
static int default_fps = 25;
static int default_width = 352;
static int default_height = 288;
static char default_name[1024];
static int use_dummy_video = 0;
static int override_fps = 0;
static int default_geometry_x = -1;
static int default_geometry_y = -1;



static void CompiledWith()
{
 fprintf(stderr, "Audio:\n");
 fprintf(stderr, "\tJack : ");
#ifdef HAVE_JACK
 fprintf(stderr, "YES\n");
#else
 fprintf(stderr, "NO\n");
#endif

 fprintf(stderr, "Video:\n");
 fprintf(stderr, "\tDirectFB: ");
#ifdef HAVE_DIRECTFB
 fprintf(stderr, "YES\n");
#else
 fprintf(stderr, "NO\n");
#endif

 fprintf(stderr, "\tSDL     : ");
#ifdef HAVE_SDL
 fprintf(stderr, "YES\n");
#else
 fprintf(stderr, "NO\n");
#endif

 fprintf(stderr, "Arch  :\n");
 fprintf(stderr, "\tMMX  : ");
#ifdef HAVE_ASM_MMX
 fprintf(stderr, "YES\n");
#else
 fprintf(stderr, "NO\n");
#endif

 fprintf(stderr, "\tSSE  : ");
#ifdef HAVE_ASM_SSE
 fprintf(stderr, "YES\n");
#else
 fprintf(stderr, "NO\n");
#endif


 fprintf(stderr, "\tCMOV : ");
#ifdef HAVE_CMOV
 fprintf(stderr, "YES\n");
#else
 fprintf(stderr, "NO\n");
#endif


 fprintf(stderr, "\t3DNOW: ");
#ifdef HAVE_3DNOW
 fprintf(stderr, "YES\n");
#else
 fprintf(stderr, "NO\n");
#endif


 fprintf(stderr, "\tMMX2 : ");
#ifdef HAVE_ASM_MMX2
 fprintf(stderr, "YES\n");
#else
 fprintf(stderr, "NO\n");
#endif


}

static void Usage(char *progname)
{
    fprintf(stderr, "Usage: %s [options] <filename> [<filename> ...]\n",
	    progname);
    fprintf(stderr, "where options are:\n");

    fprintf(stderr,
	    "  -p/--port\t\t\tTCP port to accept/send messages (default: 3490)\n");
    fprintf(stderr, "  -h/--host\t\t\tStart as host (default: server)\n");
    fprintf(stderr,
	    "  -t/--timer num\t\tspecify timer to use (none:0,normal:2,rtc:1) default is 1\n");

#ifdef HAVE_DIRECTFB
    fprintf(stderr,
	    "  -O/--output\t\t\tSDL(0) , DirectFB(1), SDL and DirectFB(2), YUV4MPEG stream (3)\n");
#else
    fprintf(stderr, "  -O/--output\t\t\tSDL(0), (3) yuv4mpeg (4) SHM (experimental)\n");
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
	    "  -S/--size NxN\t\t\twidth X height for SDL (S) or video (H) window\n");
    fprintf(stderr,
	    "  -d/--dummy\t\t\tStart using dummy video\n");
    fprintf(stderr,
	    "     --dummy-width <num>\n     --dummy-height <num> \n     --dummy-fps <num> \n     --dummy-filename <filename>\n");
    fprintf(stderr,
	    "  -X/--no-default-tags\t\tDo not create solid color tags at startup\n");
    fprintf(stderr,
	    "  -l/--action-file <filename>\tLoad an Action File (none at default)\n");
    fprintf(stderr,
	    "  -u/--dump-events  \t\tDump event information to screen\n");
    fprintf(stderr,
	    "  -j/--no-ffmpeg   \t\tDont use FFmpeg for MJPEG decoding\n");
    fprintf(stderr,
	    "  -I/--deinterlace\t\tDeinterlace video if it is interlaced\n");    
    fprintf(stderr,"  -x/--geometryx <num> \t\tTop left x offset for SDL video window\n");
    fprintf(stderr,"  -y/--geometryy <num> \t\tTop left y offset for SDL video window\n");
    fprintf(stderr,"  -F/--features  \t\tList of compiled features\n");
    fprintf(stderr,"  -g/--gui      \t\tSetup a SDL video window on a given WindowID (see -u)\n");
    fprintf(stderr,"  -v/--verbose  \t\tEnable debugging output (default off)\n");
    fprintf(stderr,"  -b/--bezerk    \t\tBezerk (default off)   \n");
    fprintf(stderr,"  -n/--no-color     \t\tDont use colored text\n");
    fprintf(stderr,"  -m/--sample-mode [01]\t\tSampling mode 1 = best quality (default), 0 = best performance\n");  
    exit(-1);
}


static int set_option(const char *name, char *value)
{
    /* return 1 means error, return 0 means okay */
    int nerr = 0;
    if (strcmp(name, "host") == 0 || strcmp(name, "h") == 0) {
	run_server = 0;
	fprintf(stderr, "Run as server, host is not yet implemented.\n");
	veejay_free(info);
	exit(0);
    } else if (strcmp(name, "port") == 0 || strcmp(name, "p") == 0) {
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
	    exit(1);
	}
	} else if (strcmp(name, "gui") == 0 || strcmp(name, "g") == 0) {
	     info->video_out = -1;
	} else if (strcmp(name, "sample-mode" ) == 0 || strcmp(name, "m" ) == 0)
	{
		veejay_set_sampling( info,atoi(optarg));
    } else if (strcmp(name, "synchronization") == 0
	       || strcmp(name, "c") == 0) {
	info->sync_correction = atoi(optarg);
    } else if (strcmp(name, "graphics-driver") == 0
	       || strcmp(name, "G") == 0
	       || strcmp(name, "output-driver") == 0
	       || strcmp(name, "O") == 0) {
	    info->video_out = atoi(optarg);	/* use SDL */
    } else if (strcmp(name, "gui") == 0 || strcmp(name,"g") == 0) {
		info->gui_screen = 1;
    } else if (strcmp(name, "F") == 0 || strcmp(name, "features")==0) {
	CompiledWith();
        exit(0);
    } else if (strcmp(name, "preserve-pathnames") == 0
	       || strcmp(name, "P") == 0) {
	info->preserve_pathnames = 1;
    } else if (strcmp(name, "deinterlace") == 0 || strcmp(name, "I" )==0) {
	info->auto_deinterlace = 1;
    } else if (strcmp(name, "size") == 0 || strcmp(name, "s") == 0) {
	if (sscanf(value, "%dx%d", &info->sdl_width, &info->sdl_height) !=
	    2) {
	    mjpeg_error("--size parameter requires NxN argument");
	    nerr++;
	}
    } 
    else if (strcmp(name, "d")==0 || strcmp(name, "dummy")==0) {
	use_dummy_video = 1;
    } else if (strcmp(name, "outstream") == 0 || strcmp(name, "o") == 0) {
	snprintf(info->stream_outname,256,"%s",(char*) optarg);
    } else if (strcmp(name, "dummy-width")==0) {
	 default_width = atoi(optarg);
    } else if (strcmp(name, "dummy-height")==0) {
         default_height = atoi(optarg);
    } else if (strcmp(name, "action-file")==0 || strcmp(name,"l")==0) {
	strcpy(info->action_file,(char*) optarg);
	info->load_action_file = 1;
	}
	else if(strcmp(name,"no-ffmpeg")==0 || strcmp(name,"j")==0) {
		info->no_ffmpeg = 1;
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
    else if (strcmp(name, "dummy-fps") == 0) {
	default_fps = atoi(optarg);
    }
    else if (strcmp(name, "dummy-filename")==0) {
	strcpy(default_name, optarg);
    }
    else if (strcmp(name,"fps")==0 || strcmp(name, "f")==0) {
	override_fps = atoi(optarg);
	}
    else if (strcmp(name,"no-default-tags")==0||strcmp(name, "X")==0) {
	default_use_tags=0;
	}	
    else
	nerr++;			/* unknown option - error */

    return nerr;
}

static void check_command_line_options(int argc, char *argv[])
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
	{"no-default-tags",0,0,0},
	{"dump-events",0,0,0},
	{"bezerk",0,0,0},
	{"outstream", 1, 0, 0},
	{"action-file",1,0,0},
	{"features",0,0,0},
	{"deinterlace",0,0,0},
	{"no-ffmpeg",0,0,0},
	{"port", 1, 0, 0},
	{"host", 1, 0, 0},
	{"sample-mode",1,0,0},
	{"dummy-width",1,0,0},
        {"dummy-height",1,0,0},
        {"dummy-filename",1,0,0},
	{"dummy-fps",1,0,0},
	{"dummy",0,0,0},
	{"gui",0,0,0},
	{"interactive",0,0,0},
	{"geometry-x",1,0,0},
	{"geometry-y",1,0,0},
	{"gui",0,0,0},
	{"fps",1,0,0},
	{"no-color",0,0,0},
	{0, 0, 0, 0}
    };
#endif
    if (argc < 2) {
	Usage(argv[0]);
	exit(-1);
    }
    
/* Get options */
    nerr = 0;
#ifdef HAVE_GETOPT_LONG
    while ((n =
	    getopt_long(argc, argv,
			"o:G:O:a:H:V:s:c:t:l:C:p:m:x:y:nFPXugvdibIjf:",
			long_options, &option_index)) != EOF)
#else
    while ((n =
	    getopt(argc, argv,
		   "o:G:s:O:a:c:t:l:t:C:x:y:m:p:nFPXvudgibIjf:")) != EOF)
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
    mjpeg_default_handler_verbosity( (info->verbose ? 2:0) );

    if(use_dummy_video) {
	if(veejay_init_fake_video(info,default_width,default_height, default_name, default_fps )==0)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Wrote 30 frames in %d x %d at %2.2f to %s in MJPEG format",
			default_width,default_height,(float) default_fps, default_name);
	veejay_dummy_open(info, default_name, override_fps);

	}
    }
    else {
       if(!info->dump)
       if(veejay_open(info, argv + optind, argc - optind,override_fps)<=0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot start veejay");
		exit(1);
	}
   }

     
}

static void print_license()
{
	veejay_msg(VEEJAY_MSG_INFO,
	    "Veejay -<|Prodigium|>- %s Copyright (C) Niels Elburg and others",VERSION);
	veejay_msg(VEEJAY_MSG_INFO,
	    "This software is subject to the GNU GENERAL PUBLIC LICENSE");

	veejay_msg(VEEJAY_MSG_INFO,
	    "Veejay comes with ABSOLUTELY NO WARRANTY; this is free software and");
	veejay_msg(VEEJAY_MSG_INFO,
		"you are welcome to redistribute it under certain conditions.");
	veejay_msg(VEEJAY_MSG_INFO,
	    "The license must be included in the (source) package (COPYING)");
}

static void process_input(char *buffer)
{
	char msg[100];
	buffer[strlen(buffer)-1] = '\0';
	vj_lock(info);
	vj_event_parse_msg((void*)info, buffer);
	vj_unlock(info);
}

static void human_friendly_msg( int net_id,char * buffer )
{

	char vims_msg[150];
	if(buffer==NULL)
		sprintf(vims_msg, "%03d:;", net_id );
	else
		sprintf( vims_msg, "%03d:%s;", net_id,buffer+1);

	vj_event_parse_msg( (void*)info, vims_msg);
}

static int human_friendly_vims(char *buffer)
{
   if(buffer[0] == 'H')
   {
 	int n1,n2;
	if(sscanf(buffer+1,"%d %d",&n1,&n2)==2) 
	{
		vj_event_print_range(n1,n2);
		return 1;
	}
   }
   if(buffer[0] == 'h' || buffer[0] == '?')
   {
	veejay_msg(VEEJAY_MSG_INFO, "vi\t\tOpen video4linux device");
	veejay_msg(VEEJAY_MSG_INFO, "li\t\tOpen vloopback device");
 	veejay_msg(VEEJAY_MSG_INFO, "fi\t\tOpen Y4M stream for input");
	veejay_msg(VEEJAY_MSG_INFO, "fo\t\tOpen Y4M stream for output");
	veejay_msg(VEEJAY_MSG_INFO, "lo\t\tOpen vloopback device for output");
	veejay_msg(VEEJAY_MSG_INFO, "cl\t\tLoad cliplist from file");
	veejay_msg(VEEJAY_MSG_INFO, "cn\t\tNew clip from frames n1 to n2");
	veejay_msg(VEEJAY_MSG_INFO, "cd\t\tDelete clip n1");
	veejay_msg(VEEJAY_MSG_INFO, "sd\t\tDelete Stream n1");
	veejay_msg(VEEJAY_MSG_INFO, "cs\t\tSave cliplist to file");
	veejay_msg(VEEJAY_MSG_INFO, "es\t\tSave editlist to file");
	veejay_msg(VEEJAY_MSG_INFO, "ec\t\tCut frames n1 - n2 to buffer");
	veejay_msg(VEEJAY_MSG_INFO, "ed\t\tDel franes n1 - n2 ");
	veejay_msg(VEEJAY_MSG_INFO, "ep\t\tPaste from buffer at frame n1");
	veejay_msg(VEEJAY_MSG_INFO, "ex\t\tCopy frames n1 - n2 to buffer");
	veejay_msg(VEEJAY_MSG_INFO, "er\t\tCrop frames n1 - n2");
        veejay_msg(VEEJAY_MSG_INFO, "al\t\tAction file Load");
        veejay_msg(VEEJAY_MSG_INFO, "as\t\tAction file save");
	veejay_msg(VEEJAY_MSG_INFO, "de\t\tToggle debug level (%s)", (info->verbose==0?"off":"on"));
	veejay_msg(VEEJAY_MSG_INFO, "be\t\tToggle bezerk mode (%s)", (info->no_bezerk==0?"on":"off")); 
	veejay_msg(VEEJAY_MSG_INFO, "sa\t\tToggle 4:2:0 -> 4:4:4 sampling mode");
	return 1;
   }

   if(strncmp( buffer, "vi",2 ) == 0 ) { human_friendly_msg( NET_TAG_NEW_V4L, buffer+2); return 1; }
   if(strncmp( buffer, "li",2 ) == 0 ) { human_friendly_msg( NET_TAG_NEW_VLOOP_BY_NAME,buffer+2); return 1; }
   if(strncmp( buffer, "fi",2 ) == 0 ) { human_friendly_msg( NET_TAG_NEW_Y4M, buffer+2); return 1; }
   if(strncmp( buffer, "fo",2 ) == 0 ) { human_friendly_msg( NET_OUTPUT_Y4M_START,buffer+2); return 1; }
   if(strncmp( buffer, "lo",2 ) == 0 ) { human_friendly_msg( NET_OUTPUT_VLOOPBACK_STARTN, buffer+2); return 1;}
   if(strncmp( buffer, "cl",2 ) == 0 ) { human_friendly_msg( NET_CLIP_LOAD_CLIPLIST, buffer+2); return 1;}
   if(strncmp( buffer, "cn",2 ) == 0 ) { human_friendly_msg( NET_CLIP_NEW, buffer+2); return 1;}
   if(strncmp( buffer, "cd",2 ) == 0 ) { human_friendly_msg( NET_CLIP_DEL, buffer+2); return 1;}
   if(strncmp( buffer, "sd",2 ) == 0 ) { human_friendly_msg( NET_TAG_DELETE,buffer+2); return 1;}
   if(strncmp( buffer, "cs",2 ) == 0 ) { human_friendly_msg( NET_CLIP_SAVE_CLIPLIST,buffer+2); return 1;}
   if(strncmp( buffer, "es",2 ) == 0 ) { human_friendly_msg( NET_EDITLIST_SAVE,buffer+2); return 1;}
   if(strncmp( buffer, "ec",2 ) == 0 ) { human_friendly_msg( NET_EDITLIST_CUT,buffer+2); return 1;}
   if(strncmp( buffer, "ed",2 ) == 0 ) { human_friendly_msg( NET_EDITLIST_DEL,buffer+2); return 1;}
   if(strncmp( buffer, "ep",2 ) == 0 ) { human_friendly_msg( NET_EDITLIST_PASTE_AT,buffer+2); return 1; }
   if(strncmp( buffer, "ex",2 ) == 0 ) { human_friendly_msg( NET_EDITLIST_COPY, buffer+2); return 1; }
   if(strncmp( buffer, "er",2 ) == 0 ) { human_friendly_msg( NET_EDITLIST_CROP, buffer+2); return 1; }   
   if(strncmp( buffer, "al",2 ) == 0 ) { human_friendly_msg( NET_BUNDLE_FILE,buffer+2); return 1; }
   if(strncmp( buffer, "as",2 ) == 0 ) { human_friendly_msg( NET_BUNDLE_SAVE,buffer+2); return 1; }
   if(strncmp( buffer, "de",2 ) == 0 ) { human_friendly_msg( NET_DEBUG_LEVEL, NULL); return 1;}
   if(strncmp( buffer, "be",2 ) == 0 ) { human_friendly_msg( NET_BEZERK,NULL); return 1;}
   if(strncmp( buffer, "sa",2 ) == 0 ) { human_friendly_msg( NET_SAMPLE_MODE,NULL); return 1;}
   return 0;
}

int main(int argc, char **argv)
{
    int uid;
    video_playback_setup *settings;
    char buffer[256];
    int d_i=0,d_j=1,d_t=0;
    
    
    struct sched_param schp;
    /*EditList *editlist = info->editlist; */


   
    find_best_memcpy();

    fflush(stdout);
    sprintf(default_name , "dummy.avi");

    info = veejay_malloc();
    /* start with initing */
    if (!info)
	return 1;
  
    check_command_line_options(argc, argv);
    if(info->video_out == 3)
    {
	veejay_silent();

    }

    if(info->dump)
 	{
		veejay_set_colors(0);
		vj_event_init();
		vj_effect_initialize(5,5);
		vj_osc_allocate(VJ_PORT+2);	
		vj_event_dump();
		vj_effect_dump();
		veejay_free(info);
		return 0;
	}

    print_license();

    //find_best_memcpy();
   // if(info->uc->use_timer==1) {
 	 mymemset_generic(&schp, 0, sizeof(schp));
    	schp.sched_priority = sched_get_priority_max(SCHED_FIFO);
   	 if (sched_setscheduler(0, SCHED_FIFO, &schp) != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
			    "Cannot set real-time playback thread scheduling (not root?)");
   	 } else {
		veejay_msg(VEEJAY_MSG_WARNING,
		    "Using First In-First Out II scheduling");
	    }
//	}


    fcntl(0, F_SETFL, O_NONBLOCK);

//	mlockall( MCL_FUTURE );

    settings = (video_playback_setup *) info->settings;

	/* setup SIGPIPE and SIGINT catcher as a thread */
    sigemptyset(&(settings->signal_set));
    sigaddset(&(settings->signal_set), SIGINT);
    sigaddset(&(settings->signal_set), SIGPIPE);

    pthread_sigmask(SIG_BLOCK, &(settings->signal_set), NULL);
    pthread_create(&(settings->signal_thread), NULL, veejay_signal_loop,
		   (void *) info);

    if(info->video_out==5) {
	/* when streaming rgb , no sync, no timer, only 1 buffer in render queue, no audio yet */
	info->uc->use_timer = 2;
	info->sync_correction = 0;
	info->audio = 0;
    }

   if (run_server == 1)
   {
	if(vj_server_setup(info)==0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot start services");
		exit(0); 
	}
    }

    if(veejay_init(info,default_geometry_x,default_geometry_y,NULL)!=0) return -1;

    if(default_use_tags)
      veejay_default_tags(info);


    if (!veejay_main(info))
	return 1;

    if(info->video_out != 5 ) {
     if (!veejay_set_speed(info, 1))
	return 1;
    }
    else {
	if(info->render_continous) veejay_set_speed(info,1);
    }

	// start veejay
	veejay_set_frame(info, 0);


 
    if( use_dummy_video )
    {
	char msg[50];
	sprintf(msg, "099:0 0;\n");
	process_input( msg );
	sprintf(msg, "100:-1;\n");
	process_input( msg );
	sprintf(msg, "182:0 0 231 4 1 3 0 0;\n");
	process_input( msg );
	sprintf(msg, "192:0 0 1 1;\n");
	process_input(msg);
	sprintf(msg, "182:0 1 141 34 1 5;\n");
	process_input(msg);
    }
    while (veejay_get_state(info) != LAVPLAY_STATE_STOP) 
    {
      if(!veejay_is_silent())
	{	
	      memset(buffer,0,sizeof(buffer));
	      if(read(0,buffer,255)>0)
	      {
		if(!human_friendly_vims( buffer) )
		 process_input(buffer);
	      }
    	}
       usleep(400000);
    }
    veejay_quit(info);
    veejay_busy(info);		/* wait for all the nice goodies to shut down */
    veejay_free(info);

   
    return 0;
}
