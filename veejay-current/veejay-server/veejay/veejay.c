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
#include <stdlib.h>
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
#include <libveejay/vj-split.h>
#include <build.h>
#include <libvje/libvje.h>
#include <libstream/vj-ndi.h>
#ifdef _OPENMP
#include <omp.h>
#endif

extern void el_cache_configure(int t);
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
static int audio_option_explicit = 0;

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
#ifdef HAVE_LIBLO
	fprintf(stdout,"\tSupport for liblo\n");
#endif
#ifdef HAVE_QRCODE
	fprintf(stdout,"\tSupport for QR code\n");
#endif
#ifdef HAVE_NDI
    fprintf(stdout, "\tSupport for NDI network video/audio (runtime: %s)\n", vj_ndi_runtime_version());
#endif

    fprintf(stdout, "\n\n");

	exit(0);
}

static void Usage(char *progname)
{
    fprintf(stderr, "This is Veejay %s\n\n", VERSION);
    fprintf(stderr, "Usage: %s [options] <file name> [<file name> ...]\n\n", progname);
    fprintf(stderr, "Options:\n\n");

    fprintf(stderr, "  -v/--verbose                 Enable debug output\n");
    fprintf(stderr, "  -n/--no-color                Disable colored console output\n");
    fprintf(stderr, "  -u/--dump-events             Dump VIMS, OSC, FX and environment docs\n");
    fprintf(stderr, "  -B/--features                List compiled-in features and exit\n");
    fprintf(stderr, "     --version                 Print version and exit\n");
    fprintf(stderr, "  -?/--help                    Print this help and exit\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "Project and control:\n");
    fprintf(stderr, "  -p/--port <num>              TCP port for VIMS messages (default: 3490)\n");
    fprintf(stderr, "  -M/--multicast-osc <addr>    Use multicast OSC\n");
    fprintf(stderr, "  -T/--multicast-vims <addr>   Use multicast VIMS\n");
    fprintf(stderr, "  -K/--master                  Run as the master video output\n");
    fprintf(stderr, "  -C/--connect <addr[:port]>   Connect to a master VeeJay instance\n");
    fprintf(stderr, "     --instance-role <role>    standalone, program, or output\n");
    fprintf(stderr, "     --instance-id <name>      Stable backend identity for a show topology\n");
    fprintf(stderr, "     --output-source <host:port>\n");
    fprintf(stderr, "                                Output role: consume Program via local SHM or remote TCP frames\n");
    fprintf(stderr, "     --output-source-pid <pid> Output role: consume SHM published by process PID\n");
    fprintf(stderr, "     --output-source-shm <key> Output role: consume an explicit SysV SHM key\n");
    fprintf(stderr, "  -l/--sample-file <file>      Load a sample list file\n");
    fprintf(stderr, "  -F/--action-file <file>      Load an action/keybinding file\n");
    fprintf(stderr, "  -P/--preserve-pathnames      Do not canonicalize paths in edit lists\n");
    fprintf(stderr, "  -q/--quit                    Quit at end of file\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "Playback and samples:\n");
    fprintf(stderr, "  -L/--auto-loop               Start with the default sample loop\n");
    fprintf(stderr, "  -b/--bezerk                  Enable bezerk mode\n");
    fprintf(stderr, "  -g/--clip-as-sample          Load each video clip as a new sample\n");
    fprintf(stderr, "  -c/--synchronization [0|1]   Disable or enable sync correction (default: 1)\n");
    fprintf(stderr, "  -f/--fps <num>               Override frame rate (disables source audio sync)\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "Audio:\n");
    fprintf(stderr, "  -a/--audio <0|1>             Enable or disable audio playback output (default: 1)\n");
    fprintf(stderr, "                                -a 0 disables source/media playback audio; clock and external input services can remain available\n");
    fprintf(stderr, "     --audio-muted             Start with all audio playback output muted\n");
    fprintf(stderr, "                                JACK input, sync and beat services remain available\n");
    fprintf(stderr, "     --audio-sync-thread <0|1> Allow or suppress the lazy audio sync/control worker\n");
    fprintf(stderr, "     --no-audio-sync-thread    Suppress sync/capture services (beat analysis then has no input provider)\n");
    fprintf(stderr, "     --audio-beat-thread <0|1> Allow or suppress the lazy audio beat detector worker\n");
    fprintf(stderr, "     --no-audio-beat-thread    Suppress the audio beat detector worker\n");
    fprintf(stderr, "     --pace-correction <ms>    Audio pace correction offset in milliseconds (>= 0)\n");
    fprintf(stderr, "  -r/--audiorate <num>         Set dummy/sample audio rate (default: 48000 Hz)\n");
    fprintf(stderr, "     --audio-channels <num>    Set dummy/sample audio channel count\n");
    fprintf(stderr, "     --audio-bits <num>        Set dummy/sample audio bits per sample\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "Video input and format:\n");
    fprintf(stderr, "  -d/--dummy, --blank          Start without video files; render a blank source\n");
    fprintf(stderr, "  -A/--capture-device <num>    Start with capture device <num>\n");
    fprintf(stderr, "     --ndi-receive <source>    Start with an NDI network video/audio source\n");
    fprintf(stderr, "     --ndi-list                Discover NDI sources and exit\n");
    fprintf(stderr, "  -Z/--load-generators <num>   Load generator plugins and start stream <num>\n");
    fprintf(stderr, "  -W/--input-width, --source-width <num>\n");
    fprintf(stderr, "                                Set blank-source width\n");
    fprintf(stderr, "  -H/--input-height, --source-height <num>\n");
    fprintf(stderr, "                                Set blank-source height\n");
    fprintf(stderr, "  -N/--norm <num>              Set norm: 0=PAL, 1=NTSC, 2=SECAM (default: PAL)\n");
    fprintf(stderr, "  -Y/--yuv [0|1|2]             Force YCbCr mode: 0=default, 1=limited, 2=full-range\n");
    fprintf(stderr, "  -e/--swap-range              Swap YUV range 0-255 <-> 16-235 on video files\n");
    fprintf(stderr, "  -I/--deinterlace             Deinterlace interlaced video\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "Video output and display:\n");
    fprintf(stderr, "     --ndi-send                Publish the Program/final Output as NDI\n");
    fprintf(stderr, "     --ndi-name <name>         Set the advertised NDI sender name\n");
    fprintf(stderr, "     --ndi-no-tally            Disable NDI program/preview tally output\n");
    fprintf(stderr, "     --ndi-follow-clock        Follow received NDI timestamps when the source clock is healthy\n");
    fprintf(stderr, "  -D/--composite, --no-viewport\n");
    fprintf(stderr, "                                Do not start with the legacy camera/projection viewport\n");
    fprintf(stderr, "  -G/--graphics-driver <num>   Alias for -O/--output\n");
    fprintf(stderr, "  -O/--output <num>            Select video output driver\n");
#ifdef HAVE_SDL
    fprintf(stderr, "                                0 = SDL (default)\n");
#endif
    fprintf(stderr, "                                3 = Headless, no video output\n");
    fprintf(stderr, "                                4 = Y4M stream 4:2:0 (requires -o)\n");
    fprintf(stderr, "                                5 = Vloopback native format (requires -o)\n");
    fprintf(stderr, "                                6 = Y4M stream 4:2:2 (requires -o)\n");
    fprintf(stderr, "                                7 = Vloopback YUV 4:2:0 (requires -o)\n");
    fprintf(stderr, "                                8 = Vloopback BGR (requires -o)\n");
    fprintf(stderr, "  -o/--output-file <file>      Write output to file/device for -O/--output\n");
    fprintf(stderr, "  -w/--output-width, --project-width <num>\n");
    fprintf(stderr, "                                Set project/output canvas width\n");
    fprintf(stderr, "  -h/--output-height, --project-height <num>\n");
    fprintf(stderr, "                                Set project/output canvas height\n");
#ifdef HAVE_SDL
    fprintf(stderr, "  -s/--size, --window-size WxH Set SDL window size\n");
    fprintf(stderr, "  -x/--geometry-x, --window-x <num>\n");
    fprintf(stderr, "                                Set SDL window X position\n");
    fprintf(stderr, "  -y/--geometry-y, --window-y <num>\n");
    fprintf(stderr, "                                Set SDL window Y position\n");
    fprintf(stderr, "     --fullscreen              Start SDL output in fullscreen-desktop mode\n");
    fprintf(stderr, "     --windowed                Start SDL output in windowed mode\n");
    fprintf(stderr, "     --borderless              Open SDL window without a title bar\n");
    fprintf(stderr, "     --no-keyboard             Disable SDL keyboard input\n");
    fprintf(stderr, "     --no-mouse                Disable SDL mouse input\n");
    fprintf(stderr, "     --show-cursor             Show the mouse cursor in the SDL window\n");
#endif
    fprintf(stderr, "\n");

    fprintf(stderr, "Cache, rendering and utilities:\n");
    fprintf(stderr, "  -m/--memory <num>            Cache memory percentage (0=disable, max=80)\n");
    fprintf(stderr, "  -j/--max_cache, --max-cache <num>\n");
    fprintf(stderr, "                                Divide cache memory over N samples\n");
    fprintf(stderr, "  -t/--timer <num>             Select timer: 0=none, 1=default (default: 1)\n");
    fprintf(stderr, "  -S/--scene-detection <num>   Create samples using scene detection threshold\n");
    fprintf(stderr, "  -X/--dynamic-fx-chain        Do not keep FX chain buffers in RAM\n");
    fprintf(stderr, "     --split-screen <file>     Load video-wall split-screen configuration\n");
    fprintf(stderr, "     --fx-custom-default-values\n");
    fprintf(stderr, "                                Read FX defaults from ~/.veejay/livido and frei0r\n");
    fprintf(stderr, "     --benchmark WxH           Benchmark memory and FX thread counts\n");
    fprintf(stderr, "\n");
}

#define OUT_OF_RANGE(val) ( val < 0 ? 1 : ( val > 100 ? 1 : 0) )
#define OUT_OF_RANGE_ERR(val) if(OUT_OF_RANGE(val)) { fprintf(stderr,"\tValue must be 0-100\n"); exit(1); }

static int parse_binary_option(const char *name, const char *value, int *dst)
{
    if(!value || !dst) {
        fprintf(stderr, "%s requires argument 0 or 1\n", name ? name : "option");
        return 1;
    }

    if(strcmp(value, "0") == 0) {
        *dst = 0;
        return 0;
    }

    if(strcmp(value, "1") == 0) {
        *dst = 1;
        return 0;
    }

    fprintf(stderr, "%s must be 0 or 1\n", name ? name : "option");
    return 1;
}

static int parse_positive_int_option(const char *name, const char *value, int *dst)
{
    char *end = NULL;
    long parsed;

    if(!value || !*value) {
        fprintf(stderr, "%s requires a positive integer\n", name);
        return 1;
    }

    errno = 0;
    parsed = strtol(value, &end, 10);
    if(errno != 0 || end == value || *end != '\0' || parsed <= 0 || parsed > INT_MAX) {
        fprintf(stderr, "%s requires a positive integer\n", name);
        return 1;
    }

    *dst = (int)parsed;
    return 0;
}

static int parse_port_option(const char *name, const char *value, int *dst)
{
    if(parse_positive_int_option(name, value, dst))
        return 1;
    if(*dst > 65535) {
        fprintf(stderr, "%s must be in the range 1..65535\n", name);
        return 1;
    }
    return 0;
}

static int parse_split_screen_option(const char *value)
{
    if(!value || value[0] == '\0') {
        fprintf(stderr, "--split-screen requires a configuration file\n");
        return 1;
    }

    snprintf(info->settings->split_screen_file,
             sizeof(info->settings->split_screen_file),
             "%s",
             value);
    info->settings->splitscreen = 1;
    return 0;
}

#define check_val(val,msg) {\
if(val==NULL){\
fprintf(stderr, " Invalid argument given for %s\n",msg);\
}\
}

static void print_ndi_sources_and_exit(void)
{
    vj_ndi_source_info sources[256];
    if(!vj_ndi_runtime_available()) {
        fprintf(stderr, "NDI runtime is not available. Install the NDI runtime or configure without NDI.\n");
        exit(1);
    }
    int count = vj_ndi_discover(sources, 256, 1500);
    printf("NDI runtime: %s\n", vj_ndi_runtime_version());
    printf("Discovered %d NDI source%s\n", count, count == 1 ? "" : "s");
    for(int i = 0; i < count; i++)
        printf("%3d  %s%s%s\n", i, sources[i].name,
               sources[i].url[0] ? "  " : "", sources[i].url);
    exit(0);
}

static int set_option(const char *name, char *value)
{
    /* return 1 means error, return 0 means okay */
    int nerr = 0;
    if (strcmp(name, "port") == 0 || strcmp(name, "p") == 0) {
        nerr += parse_port_option("-p/--port", value, &info->uc->port);
    } else if (strcmp(name, "verbose") == 0 || strcmp(name, "v") == 0) {
		info->verbose = 1;
		veejay_set_debug_level(info->verbose);
		veejay_set_timestamp(1);
    } else if (strcmp(name, "no-color") == 0 || strcmp(name,"n") == 0)
	{
	 veejay_set_colors(0);
    } else if (strcmp(name, "audio") == 0 || strcmp(name, "a") == 0) {
        int enabled = 0;
        if(parse_binary_option("-a/--audio", value, &enabled))
            nerr++;
        else {
            info->audio = enabled ? AUDIO_PLAY : NO_AUDIO;
            audio_option_explicit = 1;
        }
    } else if (strcmp(name, "audio-muted") == 0) {
        atomic_store_int(&info->settings->audio_mute, 1);
    } else if (strcmp(name, "no-audio-sync-thread") == 0) {
#ifdef HAVE_JACK
        veejay_audio_sync_thread_set_enabled(0);
#endif
    } else if (strcmp(name, "audio-sync-thread") == 0) {
        int enabled = 0;
        if(parse_binary_option("--audio-sync-thread", value, &enabled))
            nerr++;
        else {
#ifdef HAVE_JACK
            veejay_audio_sync_thread_set_enabled(enabled);
#endif
        }
    } else if (strcmp(name, "no-audio-beat-thread") == 0) {
#ifdef HAVE_JACK
        veejay_audio_beat_thread_set_enabled(0);
#endif
    } else if (strcmp(name, "audio-beat-thread") == 0) {
        int enabled = 0;
        if(parse_binary_option("--audio-beat-thread", value, &enabled))
            nerr++;
        else {
#ifdef HAVE_JACK
            veejay_audio_beat_thread_set_enabled(enabled);
#endif
        }
	} else if (strcmp(name, "pace-correction") == 0 ) {
	info->settings->pace_correction = atoi( optarg);
		if( info->settings->pace_correction < 0 ) {
			fprintf(stderr, "Audio pace correction must be a non-negative value\n");
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
	else if (strcmp(name, "max_cache" )== 0 || strcmp(name, "max-cache") == 0 ||
             strcmp(name, "j" ) == 0 )
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
		el_cache_configure(max_mem_);

    } else if (strcmp(name, "synchronization") == 0 || strcmp(name, "c") == 0) {
        int enabled = 0;
        if(parse_binary_option("-c/--synchronization", value, &enabled))
            nerr++;
        else
            info->sync_correction = enabled;
	} else if (strcmp(name, "version") == 0 )
	{ printf("Veejay %s\n", VERSION); exit(0); 
    } else if (strcmp(name, "graphics-driver") == 0
	       || strcmp(name, "G") == 0
	       || strcmp(name, "output") == 0
	       || strcmp(name, "O") == 0) {
        char *end = NULL;
        long driver = value ? strtol(value, &end, 10) : -1;
        if(!value || end == value || *end != '\0' || driver < 0 || driver > 8) {
            fprintf(stderr, "-O/--output requires a display driver between 0 and 8\n");
            nerr++;
        }
        else {
            info->video_out = (int)driver;
        }
    } else if (strcmp(name, "B") == 0 || strcmp(name, "features")==0) {
	CompiledWith();
        nerr++;
	} else if ( strcmp(name, "output-file" ) == 0 || strcmp(name, "o") == 0 ) {
		check_val(optarg,name);
		veejay_strncpy(info->y4m_file,(char*) optarg, strlen( (char*) optarg));
    } else if (strcmp(name, "preserve-pathnames") == 0 || strcmp(name, "P") == 0 ) {
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
            vje_benchmark_threads(w,h);
		    exit(0);
		}
    } else if (strcmp(name, "deinterlace") == 0 || strcmp(name, "I" )==0) {
		info->auto_deinterlace = 1;
    } else if (strcmp(name, "size") == 0 || strcmp(name, "window-size") == 0 ||
               strcmp(name, "s") == 0) {
        if(!value || sscanf(value, "%dx%d", &info->bes_width, &info->bes_height) != 2 ||
           info->bes_width <= 0 || info->bes_height <= 0) {
            fprintf(stderr,"-s/--size/--window-size requires positive WxH\n");
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

		if(!sample_readInfoFromSampleFile(info->action_file[1], &(info->dummy->width), &(info->dummy->height),
			&(info->dummy->fps), &(info->dummy->arate), &(info->dummy->achans), &(info->dummy->abits), &(info->dummy->abps))) {
				fprintf(stderr, "Failed to load %s\n", info->action_file[1]);
				exit(1);
			}

		info->load_sample_file = 1;
	}
    else if(strcmp(name, "instance-role") == 0) {
        if(!value) {
            nerr++;
        }
        else if(strcmp(value, "standalone") == 0) {
            info->instance_role = VJ_INSTANCE_ROLE_STANDALONE;
        }
        else if(strcmp(value, "program") == 0) {
            info->instance_role = VJ_INSTANCE_ROLE_PROGRAM;
        }
        else if(strcmp(value, "output") == 0) {
            info->instance_role = VJ_INSTANCE_ROLE_OUTPUT;
            info->dummy->active = 1;
            info->audio = NO_AUDIO;
            info->settings->composite = 0;
        }
        else {
            fprintf(stderr, "--instance-role must be standalone, program, or output\n");
            nerr++;
        }
    }
    else if(strcmp(name, "instance-id") == 0) {
        if(!value || value[0] == '\0') {
            fprintf(stderr, "--instance-id requires a non-empty name\n");
            nerr++;
        }
        else {
            snprintf(info->instance_id, sizeof(info->instance_id), "%s", value);
            info->instance_id_explicit = 1;
        }
    }
    else if(strcmp(name, "output-source") == 0) {
        const char *sep = value ? strrchr(value, ':') : NULL;
        if(!sep || sep == value || sep[1] == '\0') {
            fprintf(stderr, "--output-source requires host:port\n");
            nerr++;
        }
        else {
            size_t host_len = (size_t)(sep - value);
            if(host_len >= sizeof(info->output_source_host))
                host_len = sizeof(info->output_source_host) - 1;
            memcpy(info->output_source_host, value, host_len);
            info->output_source_host[host_len] = '\0';
            info->output_source_port = atoi(sep + 1);
            if(info->output_source_port <= 0 || info->output_source_port > 65535) {
                fprintf(stderr, "--output-source port is invalid\n");
                nerr++;
            }
        }
    }
    else if(strcmp(name, "output-source-pid") == 0) {
        info->output_source_pid = value ? atoi(value) : 0;
        if(info->output_source_pid <= 0) {
            fprintf(stderr, "--output-source-pid requires a positive PID\n");
            nerr++;
        }
    }
    else if(strcmp(name, "output-source-shm") == 0) {
        info->output_source_shm_id = value ? atoi(value) : 0;
        if(info->output_source_shm_id <= 0) {
            fprintf(stderr, "--output-source-shm requires a positive key\n");
            nerr++;
        }
    }
    else if(strcmp(name, "ndi-receive") == 0) {
        if(!value || !*value) {
            fprintf(stderr, "--ndi-receive requires an advertised NDI source name\n");
            nerr++;
        } else if(strlen(value) > VJ_NDI_SOURCE_NAME_MAX) {
            fprintf(stderr, "--ndi-receive source name exceeds %d characters\n",
                    VJ_NDI_SOURCE_NAME_MAX);
            nerr++;
        } else {
            snprintf(info->ndi_receive_name, sizeof(info->ndi_receive_name), "%s", value);
            info->ndi_receive_enabled = 1;
            info->dummy->active = 1;
            if(!audio_option_explicit)
                info->audio = AUDIO_PLAY;
        }
    }
    else if(strcmp(name, "ndi-send") == 0) {
        info->ndi_send_enabled = 1;
    }
    else if(strcmp(name, "ndi-name") == 0) {
        if(!value || !*value) {
            fprintf(stderr, "--ndi-name requires a non-empty sender name\n");
            nerr++;
        } else if(strlen(value) > VJ_NDI_SOURCE_NAME_MAX) {
            fprintf(stderr, "--ndi-name exceeds %d characters\n",
                    VJ_NDI_SOURCE_NAME_MAX);
            nerr++;
        } else {
            snprintf(info->ndi_send_name, sizeof(info->ndi_send_name), "%s", value);
        }
    }
    else if(strcmp(name, "ndi-no-tally") == 0) {
        info->ndi_tally_enabled = 0;
    }
    else if(strcmp(name, "ndi-follow-clock") == 0) {
        info->ndi_follow_clock = 1;
    }
    else if(strcmp(name, "ndi-list") == 0) {
        print_ndi_sources_and_exit();
    }
	else if (strcmp(name, "master") == 0 || strcmp(name, "K") == 0) {
		info->is_master = 1;
	}
	else if (strcmp(name, "connect") == 0 || strcmp(name, "C") == 0) {
        const char *sep = value ? strrchr(value, ':') : NULL;
        int port = VJ_PORT;
        size_t host_len = value ? strlen(value) : 0;

        if(sep) {
            char *endp = NULL;
            long parsed = strtol(sep + 1, &endp, 10);
            if(sep == value || sep[1] == '\0' || endp == sep + 1 || *endp != '\0' ||
               parsed < 1 || parsed > 65535) {
                fprintf(stderr, "-C/--connect requires host or host:port with a valid TCP port\n");
                nerr++;
                return nerr;
            }
            host_len = (size_t)(sep - value);
            port = (int)parsed;
        }
        if(!value || host_len == 0) {
            fprintf(stderr, "-C/--connect requires a non-empty host\n");
            nerr++;
            return nerr;
        }

        char *host = (char*)malloc(host_len + 1);
        if(!host) {
            fprintf(stderr, "Unable to allocate master endpoint\n");
            nerr++;
            return nerr;
        }
        memcpy(host, value, host_len);
        host[host_len] = '\0';
        free(info->master_origin);
        info->master_origin = host;
        info->master_origin_port = port;
        info->master_origin_explicit = 1;
	}
	else if (strcmp(name, "geometry-x") == 0 || strcmp(name, "window-x") == 0 ||
             strcmp(name, "x")==0) {
        if(!value) {
            fprintf(stderr, "--window-x requires an integer\n");
            nerr++;
        }
        else
            default_geometry_x = atoi(value);
	}
	else if (strcmp(name, "geometry-y") == 0 || strcmp(name, "window-y") == 0 ||
             strcmp(name,"y")==0) {
        if(!value) {
            fprintf(stderr, "--window-y requires an integer\n");
            nerr++;
        }
        else
            default_geometry_y = atoi(value);
	}
	else if (strcmp(name, "no-keyboard") == 0 ) {
		use_keyb = 0;
	}
    else if (strcmp(name, "fullscreen") == 0 ) {
        info->settings->full_screen = 1;
    }
    else if (strcmp(name, "windowed") == 0 ) {
        info->settings->full_screen = 0;
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
	else if(strcmp(name, "input-width") == 0 || strcmp(name, "source-width") == 0 ||
            strcmp(name, "W") == 0 ) {
        nerr += parse_positive_int_option("--source-width", value, &info->dummy->width);
	}
	else if(strcmp(name, "input-height") == 0 || strcmp(name, "source-height") == 0 ||
            strcmp(name, "H") == 0 ) {
        nerr += parse_positive_int_option("--source-height", value, &info->dummy->height);
	}
	else if(strcmp(name, "norm") == 0 || strcmp(name, "N") == 0 ) {
		int val = atoi(optarg);
		if(val == 0)
			override_norm = 'p';
		else if(val == 1)
			override_norm = 'n';
		else if(val == 2)
			override_norm = 's';
		else {
			fprintf(stderr, "-N/--norm must be 0=PAL, 1=NTSC or 2=SECAM\n");
			nerr++;
		}
	}
	else if(strcmp(name, "D") == 0 || strcmp(name, "composite") == 0 ||
            strcmp(name, "no-viewport") == 0)
	{
		info->settings->composite = 0;
	}
	else if(strcmp(name, "output-width") == 0 || strcmp(name, "project-width") == 0 ||
            strcmp(name, "w") == 0) {
        nerr += parse_positive_int_option("--project-width", value, &info->video_output_width);
	}
	else if(strcmp(name, "output-height") == 0 || strcmp(name, "project-height") == 0 ||
            strcmp(name, "h") == 0) {
        nerr += parse_positive_int_option("--project-height", value, &info->video_output_height);
	}
	else if(strcmp(name, "audiorate") == 0 || strcmp(name, "r") == 0 )
	{
		info->dummy->arate = atoi(optarg);
		if(info->dummy->arate <= 0) {
			fprintf(stderr, "Audio rate must be greater than zero\n");
			nerr++;
		}
	}
	else if(strcmp(name, "audio-channels") == 0 )
	{
		info->dummy->achans = atoi(optarg);
		if(info->dummy->achans <= 0) {
			fprintf(stderr, "Audio channels must be greater than zero\n");
			nerr++;
		}
	}
	else if(strcmp(name, "audio-bits") == 0 )
	{
		info->dummy->abits = atoi(optarg);
		if(info->dummy->abits <= 0) {
			fprintf(stderr, "Audio bits must be greater than zero\n");
			nerr++;
		}
	}
    else if (strcmp(name,"fps")==0 || strcmp(name, "f")==0) {
        char *end = NULL;
        override_fps = value ? strtof(value, &end) : 0.0f;
        if(!value || end == value || *end != '\0' || override_fps <= 0.0f || override_fps > 240.0f) {
            fprintf(stderr, "-f/--fps must be greater than 0 and at most 240\n");
            nerr++;
        }
	}
	else if(strcmp(name,"yuv")==0 || strcmp(name,"Y")==0)
	{
		override_pix_fmt = atoi(optarg);
		if( override_pix_fmt < 0 || override_pix_fmt > 2 ) {
			fprintf(stderr, "-Y/--yuv must be 0=default, 1=limited or 2=full-range\n");
			nerr++;
		}
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
	else if (strcmp(name, "dummy") == 0 || strcmp(name, "blank") == 0 || strcmp(name, "d" ) == 0 )
	{
		info->dummy->active = 1; // enable DUMMY MODE
	}
    else if (strcmp(name, "dynamic-fx-chain" ) == 0 || strcmp(name, "X" ) == 0 )
	{
		info->uc->ram_chain = 0;
	}
	else if (strcmp(name, "split-screen" ) == 0 )
	{
		nerr += parse_split_screen_option(value);
	}
    else if (strcmp(name, "fx-custom-default-values" ) == 0 )
    {
        info->read_plug_cfg = 1;
    }
    else if (strcmp(name, "help" ) == 0 || strcmp(name, "?") == 0 )
    {
        Usage("veejay");
        exit(0);
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
	{"master", 0 ,0 ,0},
    {"connect", 1, 0, 0},
    {"instance-role", 1, 0, 0},
    {"instance-id", 1, 0, 0},
    {"output-source", 1, 0, 0},
    {"output-source-pid", 1, 0, 0},
    {"output-source-shm", 1, 0, 0},
    {"ndi-receive", 1, 0, 0},
    {"ndi-send", 0, 0, 0},
    {"ndi-name", 1, 0, 0},
    {"ndi-no-tally", 0, 0, 0},
    {"ndi-follow-clock", 0, 0, 0},
    {"ndi-list", 0, 0, 0},
	{"synchronization", 1, 0, 0},	/* -c/--synchronization */
	{"preserve-pathnames", 0, 0, 0},	/* -P/--preserve-pathnames    */
	{"audio", 1, 0, 0},	/* -a/--audio num       */
	{"audio-muted", 0, 0, 0},
	{"no-audio-sync-thread", 0, 0, 0},
	{"audio-sync-thread", 1, 0, 0},
	{"no-audio-beat-thread", 0, 0, 0},
	{"audio-beat-thread", 1, 0, 0},
	{"size", 1, 0, 0},	/* -s/--size            */
    {"window-size", 1, 0, 0},
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
	{"dummy",0,0,0},
    {"blank",0,0,0},
	{"geometry-x",1,0,0},
    {"window-x",1,0,0},
	{"geometry-y",1,0,0},
    {"window-y",1,0,0},
	{"no-keyboard",0,0,0},
	{"no-mouse",0,0,0},
    {"fullscreen",0,0,0},
    {"windowed",0,0,0},
    {"borderless",0,0,0},
	{"show-cursor",0,0,0},
	{"auto-loop",0,0,0},
	{"fps",1,0,0},
	{"no-color",0,0,0},
	{"version",0,0,0},
	{"input-width",1,0,0},
    {"source-width",1,0,0},
	{"input-height",1,0,0},
    {"source-height",1,0,0},
	{"output-width", 1,0,0 },
    {"project-width", 1,0,0 },
	{"output",1,0,0},
	{"output-file",1,0,0},
	{"output-height", 1,0,0 },
    {"project-height", 1,0,0 },
	{"norm",1,0,0},
	{"audiorate",1,0,0},
	{"audio-channels",1,0,0},
	{"audio-bits",1,0,0},
	{"yuv",1,0,0},
	{"multicast-osc",1,0,0},
	{"multicast-vims",1,0,0},
	{"composite",0,0,0},
    {"no-viewport",0,0,0},
	{"quit",0,0,0},
	{"memory",1,0,0},
	{"max_cache",1,0,0},
    {"max-cache",1,0,0},
	{"capture-device",1,0,0},
	{"swap-range",0,0,0},
	{"load-generators",1,0,0},
	{"qrcode-connection-info",0,0,0},
	{"scene-detection",1,0,0},
	{"dynamic-fx-chain",0,0,0},
	{"pace-correction",1,0,0},
	{"split-screen",1,0,0},
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
			"o:G:O:a:H:s:c:t:j:l:p:m:h:w:x:y:r:f:Y:A:N:W:T:F:Z:C:M:S:nKILPDugvBbedXq?",
			long_options, &option_index)) != EOF)
#else
    while ((n =
	    getopt(argc, argv,
		   	"o:G:O:a:H:s:c:t:j:l:p:m:h:w:x:y:r:f:Y:A:N:W:T:F:Z:C:M:S:nKILPDugvBbedXq?"
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
	    snprintf(option, sizeof(option), "%c", n);
	    nerr += set_option(option, optarg);
	    break;
	}
	
    }
    if (optind > argc)
	nerr++;

    const int media_count = argc - optind;
    if(!nerr && info->dummy->active && media_count > 0) {
        fprintf(stderr, "--blank/--dummy cannot be combined with startup video files\n");
        nerr++;
    }
    if(!nerr && info->instance_role == VJ_INSTANCE_ROLE_OUTPUT && media_count > 0) {
        fprintf(stderr, "output role consumes a Program instance and cannot load startup video files\n");
        nerr++;
    }
    if(!nerr && info->instance_role == VJ_INSTANCE_ROLE_OUTPUT && info->ndi_receive_enabled) {
        fprintf(stderr, "output role cannot use --ndi-receive; use a standalone/program instance or publish from the Program instance\n");
        nerr++;
    }

    if(!nerr && info->instance_role == VJ_INSTANCE_ROLE_OUTPUT) {
        int sources = (info->output_source_port > 0) +
                      (info->output_source_pid > 0) +
                      (info->output_source_shm_id > 0);
        if(sources > 1) {
            fprintf(stderr, "output role accepts at most one of --output-source, --output-source-pid, or --output-source-shm\n");
            nerr++;
        }
    }

    if(!nerr && info->is_master && info->master_origin) {
        fprintf(stderr, "-K/--master and -C/--connect are mutually exclusive\n");
        nerr++;
    }

    if (nerr) {
        Usage(argv[0]);
        return -1;
    }

    return 1;
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
#ifdef HAVE_NDI
    veejay_msg(VEEJAY_MSG_INFO, "NDI transport: %s", vj_ndi_runtime_version());
    veejay_msg(VEEJAY_MSG_INFO, "NDI is a registered trademark of Vizrt NDI AB");
#endif

}

static int veejay_fatal_signal_active_ = 0;

static void veejay_backtrace_handler(int n, siginfo_t *si, void *ptr)
{
    (void) si;
    (void) ptr;

    if(__atomic_exchange_n(&veejay_fatal_signal_active_, 1, __ATOMIC_ACQ_REL))
        _exit(EX_SOFTWARE);

    if(info && info->homedir)
        (void) veejay_write_recovery_files(info);

    if(n == SIGSEGV) {
        veejay_msg(VEEJAY_MSG_ERROR, "Found Gremlins in your system");
        veejay_msg(VEEJAY_MSG_WARNING, "No fresh ale found in the fridge!");
        veejay_msg(VEEJAY_MSG_INFO, "Running with sub-atomic precision...");
    }

    veejay_print_backtrace();
    veejay_msg(VEEJAY_MSG_ERROR, "Bugs compromised the system (signal %d).", n);
    report_bug();

    _exit(EX_SOFTWARE);
}

static void install_crash_signal_handlers(void)
{
    const int crash_signals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGABRT, SIGILL };

    for(size_t i = 0; i < sizeof(crash_signals) / sizeof(crash_signals[0]); i++) {
        struct sigaction sa = {0};
        sa.sa_sigaction = veejay_backtrace_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigemptyset(&sa.sa_mask);

        for(size_t j = 0; j < sizeof(crash_signals) / sizeof(crash_signals[0]); j++)
            sigaddset(&sa.sa_mask, crash_signals[j]);

        if(sigaction(crash_signals[i], &sa, NULL) == -1)
            veejay_msg(VEEJAY_MSG_ERROR, "Unable to install handler for signal %d", crash_signals[i]);
    }
}

static void install_shutdown_signal_handlers(void)
{
    const int shutdown_signals[] = { SIGINT, SIGQUIT, SIGTERM, SIGPWR };

    for(size_t i = 0; i < sizeof(shutdown_signals) / sizeof(shutdown_signals[0]); i++) {
        struct sigaction sa = {0};
        sa.sa_sigaction = veejay_handle_signal;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);

        if(sigaction(shutdown_signals[i], &sa, NULL) == -1)
            veejay_msg(VEEJAY_MSG_ERROR, "Unable to install handler for signal %d", shutdown_signals[i]);
    }
}

static const char *veejay_cli_role_name(int role)
{
    switch(role) {
        case VJ_INSTANCE_ROLE_PROGRAM: return "program";
        case VJ_INSTANCE_ROLE_OUTPUT: return "output";
        default: return "standalone";
    }
}

static void veejay_log_startup_plan(int argc, char **argv)
{
    const int media_count = argc - optind;
    const char *role = veejay_cli_role_name(info->instance_role);
    const char *id = info->instance_id[0] ? info->instance_id : "veejay";
    const char *source = info->instance_role == VJ_INSTANCE_ROLE_OUTPUT ? "program-shm" :
                         (info->ndi_receive_enabled ? "ndi" :
                          (info->dummy->active ? "blank" :
                           (media_count > 0 ? "media" : "backend-default")));
    char source_detail[320];
    char fps_detail[48];
    char project_detail[64];

    if(override_fps > 0.0f)
        snprintf(fps_detail, sizeof(fps_detail), "%.3f override", override_fps);
    else
        snprintf(fps_detail, sizeof(fps_detail), "source/default");

    if(info->video_output_width > 0 && info->video_output_height > 0)
        snprintf(project_detail, sizeof(project_detail), "%dx%d",
                 info->video_output_width, info->video_output_height);
    else
        snprintf(project_detail, sizeof(project_detail), "automatic");

    if(info->instance_role == VJ_INSTANCE_ROLE_OUTPUT) {
        if(info->output_source_port > 0)
            snprintf(source_detail, sizeof(source_detail), "%s:%d",
                     info->output_source_host, info->output_source_port);
        else if(info->output_source_pid > 0)
            snprintf(source_detail, sizeof(source_detail), "pid:%d", info->output_source_pid);
        else
            snprintf(source_detail, sizeof(source_detail), "shm:%d", info->output_source_shm_id);
    }
    else if(info->ndi_receive_enabled) {
        snprintf(source_detail, sizeof(source_detail), "%s", info->ndi_receive_name);
    }
    else if(info->dummy->active) {
        snprintf(source_detail, sizeof(source_detail), "%dx%d",
                 info->dummy->width, info->dummy->height);
    }
    else {
        snprintf(source_detail, sizeof(source_detail), "%d file%s",
                 media_count, media_count == 1 ? "" : "s");
    }

    veejay_msg(VEEJAY_MSG_INFO,
               "[STARTUP] plan role=%s id=%s control=%d source=%s(%s) project=%s fps=%s output-driver=%d",
               role, id, info->uc->port, source, source_detail,
               project_detail, fps_detail, info->video_out);
    if(info->bes_width > 0 && info->bes_height > 0)
        veejay_msg(VEEJAY_MSG_INFO,
                   "[STARTUP] SDL window=%dx%d position=%d,%d mode=%s",
                   info->bes_width, info->bes_height,
                   default_geometry_x, default_geometry_y,
                   info->settings->full_screen ? "fullscreen" : "windowed");
    if(info->video_out == 0 && !(info->bes_width > 0 && info->bes_height > 0))
        veejay_msg(VEEJAY_MSG_INFO,
                   "[STARTUP] SDL window=project-size position=%d,%d mode=%s",
                   default_geometry_x, default_geometry_y,
                   info->settings->full_screen ? "fullscreen" : "windowed");
    if(media_count > 0) {
        veejay_msg(VEEJAY_MSG_INFO, "[STARTUP] media import mode=%s",
                   info->uc->file_as_sample ? "one sample per file" : "single edit list");
        for(int i = optind; i < argc; i++)
            veejay_msg(VEEJAY_MSG_INFO, "[STARTUP] media[%d]=%s", i - optind + 1, argv[i]);
    }
}

static void veejay_log_resolved_startup(void)
{
    video_playback_setup *settings = (video_playback_setup*)info->settings;
    veejay_msg(VEEJAY_MSG_INFO,
               "[STARTUP] resolved project=%dx%d source=%dx%d fps=%.3f output-driver=%d",
               info->video_output_width, info->video_output_height,
               info->dummy->width, info->dummy->height,
               settings->output_fps, info->video_out);
}

int main(int argc, char **argv)
{
    video_playback_setup *settings;
    int main_ret = 0;
    
    fflush(stdout);

    vj_mem_init(0, 0);
    vj_mem_optimize();
    vevo_strict_init();

    /* Disable OpenMP nested parallelism to prevent deadlocks.
       When effects use #pragma omp parallel for within sequential chain execution,
       nested parallelism can cause thread starvation and deadlock. Serializing 
       nested regions prevents this issue while still allowing effects to parallelize
       work within their own scope when called from a non-parallel context.
     */
#ifdef _OPENMP
    omp_set_nested(0);
#endif

    info = veejay_malloc();
    if (!info) {
        return 1;
    }

    settings = (video_playback_setup *)info->settings;

    const int command_line_status = check_command_line_options(argc, argv);
    if(command_line_status <= 0) {
        veejay_free(info);
        return command_line_status < 0 ? EX_USAGE : 0;
    }



    if (info->dump) {
        veejay_set_colors(0);
		veejay_set_timestamp(0);
		veejay_set_label(0);
        vj_event_init(NULL);
        vje_init(info->video_output_width <= 0 ? 720 : info->video_output_width, info->video_output_height <= 0 ? 576 : info->video_output_height);

        vj_event_dump();

		fflush(stdout);
		vj_osc_allocate(VJ_PORT + 4);
		vj_osc_dump();
		fflush(stdout);

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
			"\tVJ_SDL_YUV_MODE\tjpeg,auto,bt601 or bt709\n"
            "\n"
            "  [ System & Streaming ]\n"
            "\tVEEJAY_MAX_FILESIZE\t\tMaximum allowed file size for processing\n"
            "\tVEEJAY_NUM_DECODE_THREADS\tNumber of threads for avcodec decoding\n"
            "\tVEEJAY_MULTITHREAD_TASKS\tOpenMP threads (default=online CPU cores minus one)\n"
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
    veejay_set_instance(info);

    signal(SIGPIPE, SIG_IGN);
    install_crash_signal_handlers();
    install_shutdown_signal_handlers();

    veejay_msg(VEEJAY_MSG_INFO, "CPU cache line size: %d bytes", cpu_get_cacheline_size());
    veejay_msg(VEEJAY_MSG_INFO, "Memory alignment size: %d bytes", mem_align_size());

    info->use_keyb = use_keyb;
    info->use_mouse = use_mouse;
    info->show_cursor = show_cursor;
    info->borderless = borderless;

    veejay_log_startup_plan(argc, argv);

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

    veejay_log_resolved_startup();

    if (veejay_init(info, default_geometry_x, default_geometry_y, NULL, live, ta) < 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Cannot start Veejay");
        main_ret = 1;
        goto VEEJAY_MAIN_EXIT;
    }

    if(info->ndi_receive_enabled) {
        if(!veejay_create_ndi_stream(info, info->ndi_receive_name)) {
            veejay_msg(VEEJAY_MSG_ERROR, "Unable to create NDI input '%s'", info->ndi_receive_name);
            main_ret = 1;
            goto VEEJAY_MAIN_EXIT;
        }
        veejay_msg(VEEJAY_MSG_INFO, "NDI input '%s' is the active live stream", info->ndi_receive_name);
    }

    if (settings->splitscreen) {
        if(settings->split_screen_file[0] == '\0') {
            veejay_msg(VEEJAY_MSG_ERROR, "Split-screen mode requires a configuration file");
            main_ret = 1;
            goto VEEJAY_MAIN_EXIT;
        }

        vj_split_set_master(info->uc->port);
        info->splitter = vj_split_new_from_file(settings->split_screen_file,
                                                info->video_output_width,
                                                info->video_output_height,
                                                info->pixel_format);
        if(!info->splitter) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "Unable to load split-screen configuration '%s'",
                       settings->split_screen_file);
            main_ret = 1;
            goto VEEJAY_MAIN_EXIT;
        }
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

    while(veejay_get_state(info) != LAVPLAY_STATE_STOP)
        usleep_accurate(5000, settings);

    veejay_msg(VEEJAY_MSG_INFO, "Thank you for using Veejay");

VEEJAY_MAIN_EXIT:
    veejay_busy(info);
    veejay_close(info);
    veejay_set_instance(NULL);
	veejay_free(info);
    veejay_destroy_msg_ring();

    return main_ret;
}
