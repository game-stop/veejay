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


/** \mainpage Veejay - A Visual Instrument
 *
 * Veejay is a visual instrument and realtime video sampler.
 * It allows you to "play" the video like you would play a piano. 
 * While playing, you can record the resulting video directly to disk (video sampling).
 *
 * Veejay can be used to manipulate video in a realtime environment 
 * i.e. 'VeeJay' for visual performances or for (automated) interactive 
 * video installations.
 *
 * You can interact with veejay by using GVeejayReloaded, sendVIMS or an 
 * application that supports OSC.
 *
 * This document is Veejay's source code documentation.
 */
#include <config.h>
#include <string.h>
#define VJ_PROMPT "$> "

#include <stdio.h>
#include <sys/stat.h>
#include <signal.h>
#include <getopt.h>
#include <sched.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <libvjmem/vjmem.h>
#include <veejay/defs.h>
#include <veejay/veejay.h>
#include <libvjmsg/vj-common.h>
#include <veejay/libveejay.h>
static veejay_t *info = NULL;
static int verbose_ = 0;
static int n_slots_ = 8;
static int memory_ = 0;
static int audio_ = 0;
static int timer_ = 1;
static float fps_ = 25.0;
static int width_ = 352;
static int height_ = 288;
static int inter_ = 0;
static int norm_ = 0;
static int skip_ = 0;
static int synchronization_ = 1;
static int bezerk_ = 0;
static int fmt_ = 0;
static int rate_ = 0;
static int n_chans_ = 0;
static int bps_ = 0;
static int display_ = 0;
static int itu601_ = 0;
static int dump_ = 0;
static int devices_ = 0;

static void Usage(char *progname)
{
    fprintf(stderr, "Usage: %s [options] \n",
	    progname);
    fprintf(stderr, "where options are:\n");
	fprintf(stderr, " -v/--verbose\t\t\n");
	fprintf(stderr, " -s/--skip\t\t\n");
	fprintf(stderr, " -c/--synchronization\t\t\n");
	fprintf(stderr, " -a/--audio\t\t\n");
	fprintf(stderr, " -r/--audio-rate\t\t\n");
	fprintf(stderr, " -n/--audio-channels\t\t\n");
	fprintf(stderr, " -B/--audio-bps\n");
	fprintf(stderr, " -t/--timer\t\t\n");
	fprintf(stderr, " -b/--bezerk\t\t\n");
	fprintf(stderr, " -f/--fps\t\t\n");
	fprintf(stderr, " -w/--width\t\t\n");
	fprintf(stderr, " -h/--height\t\t\n");
	fprintf(stderr, " -n/--norm\t\t\n");
	fprintf(stderr, " -i/--inter\t\t\n");
	fprintf(stderr, " -m/--memory\t\t\n");
	fprintf(stderr, " -P/--pixelformat\t\t\n");
	fprintf(stderr, " -J/--cache\t\t\n");
	fprintf(stderr, " -p/--port\t\t\n");
	fprintf(stderr, " -d/--display [num] 1=SDL, 2=OpenGL\t\t\n");
	fprintf(stderr, " -u/--itu601 [0|1]\n");
	fprintf(stderr, " -l/--list\n");
	fprintf(stderr, " -A/--alldevices\n");
			
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
	if(strcmp(name, "verbose") == 0 || strcmp(name,"v") == 0 ) {
		verbose_ = 1;
	} else if(strcmp(name, "skip" ) == 0 || strcmp(name, "s" ) == 0 ) {
		skip_ = atoi( value );
	} else if(strcmp(name, "synchronization") == 0 || strcmp(name, "c") == 0 ) {
		synchronization_ = atoi( value );
	} else if(strcmp(name, "audio") == 0 || strcmp(name, "a" ) == 0 ) {
		audio_ = atoi( value );
	} else if(strcmp(name, "timer") == 0 || strcmp(name, "t" ) == 0 ) {
		timer_ = atoi( value );
	} else if(strcmp(name, "bezerk") == 0 || strcmp(name, "b") == 0 ) {
		bezerk_ = atoi( value );
	} else if(strcmp(name, "fps" ) == 0 || strcmp(name, "f" ) == 0 ) {
		fps_ = atof( value );
	} else if(strcmp(name, "width" ) == 0 || strcmp(name, "w" ) == 0 ) {
		width_ = atoi( value );
	} else if(strcmp(name, "height" ) == 0 || strcmp(name, "h" ) == 0 ) {
		height_ = atoi( value );
	} else if(strcmp(name, "norm" ) == 0 || strcmp(name, "n" ) == 0 ) {
		norm_ = atoi( value );
	} else if(strcmp(name, "inter" ) == 0 || strcmp(name, "i" ) == 0 ) {
		inter_ = atoi( value );
	} else if(strcmp(name, "memory" ) == 0 || strcmp(name, "m" )== 0 ) {
		memory_ = atoi(value );
	} else if(strcmp(name, "cache" )== 0 || strcmp(name, "J" ) == 0 ) {
		n_slots_ = atoi(value);
	} else if(strcmp(name, "pixelformat") == 0 || strcmp(name, "P" ) == 0 ) {
		fmt_ = atoi(value);
	} else if(strcmp(name, "audio-rate" ) == 0 || strcmp(name, "r" ) == 0 ) {
		rate_ = atoi(value);
	} else if(strcmp(name, "audio-channels" ) == 0 || strcmp(name, "N" ) == 0 ) {
		n_chans_ = atoi(value);
	} else if(strcmp(name, "audio-bps" ) == 0 || strcmp(name, "B" ) == 0 ) {
		bps_ = atoi(value);
	} else if(strcmp(name, "display") == 0 || strcmp(name, "d") == 0 ) {
		display_ = atoi(value);
	} else if(strcmp(name, "port") == 0 || strcmp(name, "p") == 0 ) {
		info->port_offset = atoi(value);
	} else if(strcmp(name, "itu601") == 0 || strcmp(name,"u") == 0 ){
		info->itu601 = atoi(value);
	} else if(strcmp(name, "list") == 0 || strcmp(name,"l") == 0 ) {
		dump_ = 1;
	} else if(strcmp(name, "alldevices") == 0 ||strcmp(name,"A")== 0 ) {
		devices_ = 1;
	} else
		nerr++;	
	
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
	{"audio", 1, 0, 0},	/* -a/--audio num       */
	{"timer", 1, 0, 0},	/* timer */
	{"bezerk",0,0,0},
	{"fps",1,0,0},
	{"width",1,0,0},
	{"height",1,0,0},
	{"norm",1,0,0},
	{"inter",1,0,0},
	{"memory",1,0,0},
	{"max_cache",1,0,0},
	{"pixelformat",1,0,0},
	{"audio-rate",1,0,0},
	{"audio-channels",1,0,0},
	{"audio-bps",1,0,0},
	{"display",1,0,0},
	{"port",1,0,0},
	{"itu601",1,0,0},
	{"list",0,0,0},
	{"alldevices",0,0,0},
	{0, 0, 0, 0}
    };
#endif
//    if (argc < 2) {
//	Usage(argv[0]);
//	return 0;
  //  }
    
/* Get options */
    nerr = 0;
#ifdef HAVE_GETOPT_LONG
    while ((n =
	    getopt_long(argc, argv,
			"lvs:c:a:t:b:f:w:h:n:i:m:j:p:P:r:n:B:d:u:N:A",
			long_options, &option_index)) != EOF)
#else
    while ((n =
	    getopt(argc, argv,
		   "lvs:c:a:t:b:f:w:h:n:i:m:j:p:P:r:n:B:d:u:N:A")) != EOF)
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
    if (optind > argc || argc <= 1)
	nerr++;

    if (nerr)
	Usage(argv[0]);

    veejay_set_debug_level(info->verbose);

	//veejay_silent();
	mjpeg_default_handler_verbosity( 0 );	
    	mjpeg_default_handler_verbosity( (info->verbose ? 1:0) );

    if(!nerr) 
	return 1;
    return 0;
}

static void print_license()
{
	veejay_msg(VEEJAY_MSG_INFO,
		"    11x0            11      00101010101011  001010101010x0        110x      0000    1100        1100");
	veejay_msg(VEEJAY_MSG_INFO,
		"  1111__            0x11    1011111111110x  0x0011111111__        11x0    __0x11__  __1111    __11x0");
	veejay_msg(VEEJAY_MSG_INFO,
		"11110x              100x11  1011            0x11                  11x0    00000000    1111__  1000");
	veejay_msg(VEEJAY_MSG_INFO,
		"110x0xx0            0x0x10  100x________    0x11________          11x0    0xx0x011      1111110x" );
 	veejay_msg(VEEJAY_MSG_INFO,
		"  0x0x11          110x10    101111111111__  0x1111111111          11x0  x00x    0x11    __1111x0" );
	veejay_msg(VEEJAY_MSG_INFO,
		"    0x0x11      111110      1011            0x11                  11x0  110x11110x10      0011");
	veejay_msg(VEEJAY_MSG_INFO,
		"      0x11110x110x10        1011            0x11          1111    11x0  111111111111__    0011" ); 
       	veejay_msg(VEEJAY_MSG_INFO,
		"        0x11110x10          10000x0x0x0x__  0x0x0x0x0x0x__110x__x011__110x        1000    0011");
	veejay_msg(VEEJAY_MSG_INFO,
		"          111110            110x0x0x0x0x00  100x0x0x0x0x00  11111111  1111        1110    1100");
	veejay_msg(VEEJAY_MSG_INFO,
		" ");
	veejay_msg(VEEJAY_MSG_INFO,
		" ");
	
	veejay_msg(VEEJAY_MSG_INFO,
	    "Veejay -<|Resurged|>- %s Copyright (C) Niels Elburg and others",VERSION);

	veejay_msg(VEEJAY_MSG_INFO,
	    "                         http://veejay.dyne.org" );
	veejay_msg(VEEJAY_MSG_INFO,
		" ");
	
	veejay_msg(VEEJAY_MSG_INFO,
	    "This software is subject to the GNU GENERAL PUBLIC LICENSE Version 2.1");

	veejay_msg(VEEJAY_MSG_INFO,
	    "Veejay comes with ABSOLUTELY NO WARRANTY; It is licensed as Free Software and");
	veejay_msg(VEEJAY_MSG_INFO,
		"you are welcome to redistribute it under certain conditions.");
	veejay_msg(VEEJAY_MSG_INFO,
		"If you fry your CPU with this program, you are out of luck");
	veejay_msg(VEEJAY_MSG_INFO,
		" ");

}

static void smp_check()
{
	int n_cpu = get_nprocs();
	int c_cpu = get_nprocs_conf();
	
	if(n_cpu == c_cpu)
	{
		if(c_cpu>1) veejay_msg(VEEJAY_MSG_INFO, "\tProcessor(s):   %d",
				c_cpu);
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
   print_license();

    fflush(stdout);
    vj_mem_init();
    vevo_strict_init();
    
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
  
    	if( dump_ )
	{
	//	vj_init_vevo_events();
	//	vj_event_vevo_dump();
		return 0;
	}
 

    info->sync_correction = synchronization_;
    info->sync_skip_frames = skip_;
    info->timer = timer_;
    info->continuous = 1;
    if(!veejay_init_project_from_args( info, width_, height_ , fps_,inter_,norm_,fmt_,audio_,rate_,n_chans_,bps_, display_ ))
    {
	veejay_msg(0, "Invalid project settings");
	return 0;
    }

	if(devices_)
		veejay_load_devices( info);
    
    if(!veejay_load_samples( info, argv + optind, argc - optind ) )
    {
	veejay_msg(0, "Project setting do not match input file(s)");
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
   	 int res =  sched_setscheduler(0, SCHED_FIFO, &schp);
	  veejay_msg(VEEJAY_MSG_INFO, "\tScheduler:      %s",
		  (res == 0 ? "First in First Out II" : "Default" ) );	  
	}

	smp_check();

	char *mem_func = get_memcpy_descr();
	if(mem_func)
	{
		veejay_msg(VEEJAY_MSG_INFO, "\tSIMD memory:    %s", mem_func);
		free(mem_func);
	}

    
      	if(veejay_init( info ) < 0 )
	{	
		veejay_msg(VEEJAY_MSG_ERROR, "Initializing veejay");
		return 0;
	}

	veejay_msg(VEEJAY_MSG_INFO, "Entering veejay");
	
	if(!veejay_main(info))
	{
	    veejay_msg(VEEJAY_MSG_ERROR, "Cannot start main playback cycle");
		return 1;
	}
    
	//veejay_set_frame(info, 0);
	veejay_msg(VEEJAY_MSG_DEBUG, "Starting playback");
	veejay_change_state(info, VEEJAY_STATE_PLAYING);

//	veejay_set_frame(info, 0);
//	veejay_set_speed(info, 1);
	  
//@below never run
    while (veejay_get_state(info) != VEEJAY_STATE_STOP) 
    {
	  // 	veejay_push_results(info);
	    usleep(4000000);
    }
    veejay_quit(info);
    veejay_busy(info);		/* wait for all the nice goodies to shut down */
    veejay_free(info);


    
    return 0;
}
