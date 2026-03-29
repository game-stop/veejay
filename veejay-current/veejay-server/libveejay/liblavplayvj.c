/*liblavplayvj - a extended librarified Linux Audio Video playback/Editing
 * VJ'fied by	Niels Elburg <nwelburg@gmail.com>
 *
 *
 * libveejay - a librarified Linux Audio Video PLAYback
 *
 * Copyright (C) 2000 Rainer Johanni <Rainer@Johanni.de>
 * Extended by:   Gernot Ziegler  <gz@lysator.liu.se>
 *                Ronald Bultje   <rbultje@ronald.bitfreak.net>
 *              & many others
 *
 * A library for playing back MJPEG video via softwastre MJPEG
 * decompression (using SDL) 
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <math.h>
#include "jpegutils.h"
#include "vj-event.h"
#ifndef X_DISPLAY_MISSING
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#ifdef HAVE_FREETYPE
#include <libveejay/vj-font.h>
#endif
#ifndef X_DISPLAY_MISSING
#include <libveejay/x11misc.h>
#endif
#ifdef HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#endif
#include <veejaycore/defs.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/atomic.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <libveejay/vj-misc.h>
#include <libveejay/vj-perform.h>
#include <libveejay/vj-plug.h>
#include <libveejay/vj-lib.h>
#include <libveejay/vj-sdl.h>
#include <libel/vj-avcodec.h>
#include <libel/pixbuf.h>
#include <veejaycore/avcommon.h>
#include <veejaycore/vj-client.h>
#ifdef HAVE_JACK
#include <libveejay/vj-jack.h>
#endif
#include <veejaycore/yuvconv.h>
#include <libveejay/vj-composite.h>
#include <libveejay/vj-viewport.h>
#include <libveejay/vj-OSC.h>
#include <veejaycore/vj-task.h>
#include <libveejay/vj-split.h>
#include <libveejay/vj-macro.h>
#include <libplugger/plugload.h>
#include <libstream/vj-vloopback.h>
#include <veejaycore/vims.h>
#include <libqrwrap/qrwrapper.h>
#include <sched.h>
#include <libveejay/vj-shm.h>
#include <pthread.h>
#include <signal.h>
#include <veejaycore/mpegconsts.h>
#include <veejaycore/mpegtimecode.h>
#include <libstream/vj-tag.h>
#include "libveejay.h"
#include <veejaycore/mjpeg_types.h>
#include "vj-perform.h"
#include <veejaycore/vj-server.h>
#ifdef HAVE_SDL
#include <SDL2/SDL.h>
#endif
#ifdef HAVE_DIRECTFB
#include <libveejay/vj-dfb.h>
#endif
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#ifdef HAVE_V4L2
#include <libstream/vj-vloopback.h>
#endif

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ( (caddr_t) -1 )
#endif
#define HZ 100
#include <libel/vj-el.h>

#include <libvje/libvje.h>
#include <libvje/effects/shapewipe.h>

#include <omp.h>

#define VALUE_NOT_FILLED -10000


extern void vj_osc_set_veejay_t(veejay_t *t);
extern void GoMultiCast(const char *groupname);
 
#ifdef HAVE_SDL
extern int vj_event_single_fire(void *ptr, SDL_Event event, int pressed);
#endif

static	veejay_t	*veejay_instance_ = NULL;

static void veejay_playback_close(veejay_t *info);

int veejay_get_state(veejay_t *info) {
	video_playback_setup *settings = (video_playback_setup*)info->settings;

	return atomic_load_int(&settings->state);
}

int	veejay_set_yuv_range(veejay_t *info) {
	switch(info->pixel_format) {
		case FMT_422:
			vje_set_pixel_range( 235,240,16,16 );
			veejay_msg(VEEJAY_MSG_DEBUG, "YUV pixel range set to limited (16-235 / 16-240)");
			return 0;
			break;
		default:
			vje_set_pixel_range( 255, 255,0,0 );
			veejay_msg(VEEJAY_MSG_DEBUG, "Using full-range YUV (luma 0-255, chroma 0-255)");
			break;
	}
	return 1;
}

static void veejay_free_frame_buffer(VJFrame *f) {
	if(f) {
		if(f->data[0])
			free(f->data[0]);
		free(f);
	}
}

static	VJFrame *veejay_allocate_frame_buffer(veejay_t *info) {
	VJFrame *buf = yuv_yuv_template( NULL,NULL,NULL, info->video_output_width, info->video_output_height, vj_to_pixfmt(info->pixel_format) );
	buf->fps = info->settings->output_fps;
	
	size_t len = sizeof(uint8_t) * ( buf->len + buf->uv_len + buf->uv_len);
	uint8_t *plane = (uint8_t*)vj_malloc( len );
	if(!plane) {
		free(buf);
		return NULL;
	}


	buf->data[0] = plane;
	buf->data[1] = plane + buf->len;
	buf->data[2] = plane + buf->len + buf->uv_len;
	buf->data[3] = NULL; 

	return buf;
}

static inline double monotonic_now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static inline void
vj_spin_until_absolute_deadline(const struct timespec *deadline,
                                double pause_cost_ns)
{
    struct timespec now;

    long long deadline_ns =
        (long long)deadline->tv_sec * 1000000000LL + deadline->tv_nsec;

    const double inv_pause = 1.0 / pause_cost_ns;

    for (;;) {

        clock_gettime(CLOCK_MONOTONIC, &now);

        long long now_ns =
            (long long)now.tv_sec * 1000000000LL + now.tv_nsec;

        long long remaining_ns = deadline_ns - now_ns;

        if (remaining_ns <= 0)
            break;

        int spin_batch = (int)((remaining_ns * inv_pause) * 0.5);

        if (spin_batch > 128) spin_batch = 128;
        if (spin_batch < 1)   spin_batch = 1;

        for (int i = 0; i < spin_batch; ++i) {
#if defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
            __asm__ volatile("yield");
#endif
        }
    }
}

// audio timer function
void usleep_accurate(long long usec, video_playback_setup *settings)
{
    if (usec <= 0) return;

    struct timespec start, deadline;
    clock_gettime(CLOCK_MONOTONIC, &start);

    const long long target_ns    = usec * 1000LL;
    const long long threshold_ns = settings->clock_overshoot;

    const long long target_sec  = target_ns / 1000000000LL;
    const long long target_rem  = target_ns - target_sec * 1000000000LL;

    deadline.tv_sec  = start.tv_sec + target_sec;
    deadline.tv_nsec = start.tv_nsec + target_rem;
    if (deadline.tv_nsec >= 1000000000LL) {
        deadline.tv_nsec -= 1000000000LL;
        deadline.tv_sec++;
    }

    if (target_ns > threshold_ns) {
        const long long sleep_ns = target_ns - threshold_ns;

        const long long sleep_sec = sleep_ns / 1000000000LL;
        const long long sleep_rem = sleep_ns - sleep_sec * 1000000000LL;

        struct timespec req = { .tv_sec = sleep_sec, .tv_nsec = sleep_rem };
        struct timespec rem;

        while (nanosleep(&req, &rem) == -1) {
            if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) return;
            req = rem;
        }
    }

	// precise landing
    vj_spin_until_absolute_deadline(&deadline, settings->pause_cost_ns);
}

// wait until absolute time to prevent drift
static inline void usleep_hybrid(long long usec, video_playback_setup *settings)
{
    if (usec <= 0)
        return;

    struct timespec now, deadline;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long long target_ns = usec * 1000LL;

    deadline.tv_sec  = now.tv_sec  + target_ns / 1000000000LL;
    deadline.tv_nsec = now.tv_nsec + target_ns % 1000000000LL;

    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        deadline.tv_sec++;
    }

    while (clock_nanosleep(CLOCK_MONOTONIC,
                           TIMER_ABSTIME,
                           &deadline,
                           NULL) == EINTR)
    {
        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP)
            return;
    }
}


static void veejay_seek(veejay_t *info, int speed, int max_sfd)
{
    video_playback_setup *settings =
        (video_playback_setup *)info->settings;

    /* Reset slice logic */
    atomic_store_int(&settings->audio_slice, 0);
    atomic_store_int(&settings->audio_slice_len, max_sfd);
    /* Request audio flush */
    atomic_store_int(&settings->audio_flush_request, 1);
}

static void	veejay_reset_el_buffer( veejay_t *info );

void veejay_change_state(veejay_t * info, int new_state)
{
	video_playback_setup *settings = info->settings;
	atomic_store_int(&settings->state, new_state);
}

void veejay_change_state_save(veejay_t * info, int new_state)
{
	if(new_state == LAVPLAY_STATE_STOP )
	{
		pid_t my_pid = getpid();
        int len = snprintf(NULL, 0, "%s/recovery/recovery_samplelist_p%d.sl", info->homedir, (int)my_pid);
        char *recover_samples = vj_malloc(len + 1);
        if (!recover_samples) { 
			return;
		}
        snprintf(recover_samples, len + 1, "%s/recovery/recovery_samplelist_p%d.sl", info->homedir, (int)my_pid);
        len = snprintf(NULL, 0, "%s/recovery/recovery_editlist_p%d.edl", info->homedir, (int)my_pid);
        char *recover_edl = malloc(len + 1);
        if (!recover_edl) { 
			free(recover_samples); 
			return;
		}
        snprintf(recover_edl, len + 1, "%s/recovery/recovery_editlist_p%d.edl", info->homedir, (int)my_pid);
        
		int rs = sample_writeToFile( recover_samples,info->composite,info->seq,info->font,
				info->uc->sample_id, info->uc->playback_mode );
		int re = veejay_save_all( info, recover_edl, 0, 0 );
		if(rs)
			veejay_msg(VEEJAY_MSG_WARNING, "Saved samplelist to %s", recover_samples );
		if(re)
			veejay_msg(VEEJAY_MSG_WARNING, "Saved Editlist to %s", recover_edl );

		free(recover_samples);
	}

	veejay_change_state( info, new_state );
}

static inline int playback_dir(int speed)
{
    if (speed > 0) return 1;
    if (speed < 0) return -1;
    return 0; // paused
}


int veejay_set_framedup(veejay_t *info, int n)
{
	 video_playback_setup *settings = info->settings;

	 int cur_dir = playback_dir( settings->current_playback_speed );
	 int cur_sfd = 0;

	switch(info->uc->playback_mode) {
		case VJ_PLAYBACK_MODE_PLAIN:
			cur_sfd = info->sfd;	
			if( cur_sfd != n) {
				veejay_seek(info,settings->current_playback_speed, n);
				info->sfd = n;
			}
			break;
		case VJ_PLAYBACK_MODE_SAMPLE:
			cur_sfd = sample_get_framedup( info->uc->sample_id );
			if(cur_sfd != n ) {
				sample_set_framedup(info->uc->sample_id,n);

				veejay_seek(info,settings->current_playback_speed, n);

			}
		break;

		default:
		return -1;
	}

	if(cur_sfd != n) {
		vj_perform_initiate_edge_change(info, AUDIO_EDGE_RESET,cur_dir, cur_dir );
	}
	
	settings->sfd = n;
	info->sfd = n;
	
	return 1;
}

int veejay_set_speed(veejay_t * info, int speed, int force_seek)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    int len=0;
	
    int prev_dir = playback_dir( settings->current_playback_speed );
	int cur_dir = playback_dir( speed );
	int direction_changed = (prev_dir != cur_dir);


	int old_speed = settings->current_playback_speed;
	int max_sfd = 1;

    if( speed > MAX_SPEED )
		speed = MAX_SPEED;
    if( speed < -(MAX_SPEED))
		speed = -(MAX_SPEED);

	switch (info->uc->playback_mode)
	{
		case VJ_PLAYBACK_MODE_PLAIN:
			if( abs(speed) <= info->current_edit_list->total_frames )
				settings->current_playback_speed = speed;	
			else
				veejay_msg(VEEJAY_MSG_DEBUG, "Speed %d too high to set", speed);
			max_sfd = info->sfd;
		break;

		case VJ_PLAYBACK_MODE_SAMPLE:
			len = sample_get_endFrame(info->uc->sample_id) - sample_get_startFrame(info->uc->sample_id);
			if( speed < 0)
			{
				if ( (-1*len) > speed )
				{
					veejay_msg(VEEJAY_MSG_ERROR,"Speed %d too high to set",speed);
					return 1;
				}
			}
			else
			{
				if(speed >= 0)
				{
					if( len < speed )
					{
						veejay_msg(VEEJAY_MSG_ERROR, "Speed %d too high to set",speed);
						return 1;
					}
				}
			}
			if(sample_set_speed(info->uc->sample_id, speed) != -1)
				settings->current_playback_speed = speed;
			max_sfd = sample_get_framedup(info->uc->sample_id);

		break;

		case VJ_PLAYBACK_MODE_TAG:
			settings->current_playback_speed = 1;
			max_sfd = 1;
		break;

		default:
			veejay_msg(VEEJAY_MSG_ERROR, "Unknown playback mode");
		break;
	}

	int edge_type = (speed == 0 ? AUDIO_EDGE_RESET : AUDIO_EDGE_NONE);
	if(old_speed != speed && old_speed != 0)
		edge_type = AUDIO_EDGE_RESET;
	if(old_speed != speed && abs(old_speed) == abs(speed))
		edge_type = AUDIO_EDGE_DIRECTION;

	if(direction_changed) {
		atomic_store_int(&settings->audio_direction_changed, 1);
	}

	vj_perform_initiate_edge_change(info, edge_type,prev_dir, cur_dir);

	if(force_seek) {
		veejay_seek( info, settings->current_playback_speed, max_sfd);
	}


	return 1;
}


int veejay_hold_frame(veejay_t * info, int rel_resume_pos, int hold_pos)
{
    video_playback_setup *settings = (video_playback_setup *) info->settings;

    if( rel_resume_pos > 0 )
    {
        if( settings->hold_status == 1 )
        {
            settings->hold_pos += rel_resume_pos;
            settings->hold_resume ++;
            if(settings->hold_resume < hold_pos )
                settings->hold_resume = hold_pos;
        } 
        else
        {
            settings->hold_pos = hold_pos + rel_resume_pos;
            settings->hold_resume = hold_pos;  
        }
        settings->hold_status = 1;
    } 
    else 
    {
        if (settings->hold_status == 1) 
        {
            atomic_store_double(&settings->smoothed_drift_us, 0.0);
            
            
            settings->hold_status = 0;
            veejay_msg(VEEJAY_MSG_INFO, "HOLD: Released. Sync re-anchored to T=0.");
        }
    }

    return 1;
}

static void	veejay_sample_resume_at( veejay_t *info, int cur_id )
{
	long pos = sample_get_resume(cur_id);
	veejay_set_frame(info, pos );
	veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d continues with frame %d", cur_id, pos );
}

int veejay_increase_frame(veejay_t *info, long num)
{
    video_playback_setup *settings = info->settings;
    long long current_frame_num = atomic_load_long_long(&settings->current_frame_num);
    long long next_frame = current_frame_num + num;

    if(info->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN) {
        if(next_frame < settings->min_frame_num) return 0;
        if(next_frame > settings->max_frame_num) return 0;
    } else if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
        if(next_frame < sample_get_startFrame(info->uc->sample_id)) return 0;
        if(next_frame > sample_get_endFrame(info->uc->sample_id)) return 0;
    }

    // detect jump vs normal increment ---
    int edge_type = AUDIO_EDGE_NONE;

    if(abs(num) != 1) {
        // large skip = jump
        edge_type = AUDIO_EDGE_JUMP;
    } else if((num > 0 && settings->current_playback_speed < 0) ||
              (num < 0 && settings->current_playback_speed > 0)) {
        // changing direction
        edge_type = AUDIO_EDGE_DIRECTION;
    }

    atomic_add_fetch_old_long_long(&settings->current_frame_num, num);

    if(edge_type != AUDIO_EDGE_NONE) {
        int prev_dir = settings->current_playback_speed >= 0 ? 1 : -1;
        int cur_dir  = num >= 0 ? 1 : -1;
		
        vj_perform_initiate_edge_change(info, edge_type, prev_dir, cur_dir);
    }

    return 1;
}


int veejay_free(veejay_t * info)
{
	video_playback_setup *settings = (video_playback_setup *) info->settings;

    veejay_playback_close(info);

	vj_event_destroy(info);

	vj_tag_free();
   	
	sample_free(info->edit_list);

	if( info->plain_editlist )
		vj_el_free(info->plain_editlist);

	//task_destroy(); FIXME

	if( info->composite )
		composite_destroy( info->composite );

	if( info->settings->action_scheduler.state )
	{
		if(info->settings->action_scheduler.sl )
			free(info->settings->action_scheduler.sl );
		info->settings->action_scheduler.state = 0;
	}

	if( info->effect_frame1) free(info->effect_frame1);
	if( info->effect_frame_info) free(info->effect_frame_info);
	if( info->effect_frame2) free(info->effect_frame2);
	if( info->effect_info) free( info->effect_info );
	if( info->effect_frame3) free(info->effect_frame3);
	if( info->effect_frame_info2) free(info->effect_frame_info2);
	if( info->effect_frame4) free(info->effect_frame4);
	if( info->effect_info2) free( info->effect_info2 );

	if(info->global_chain) {
		for( int i = 0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
			free(info->global_chain->fx_chain[i]);
		}
		free(info->global_chain);
	}
	
    if( info->settings->transition.ptr ) {
        shapewipe_free(info->settings->transition.ptr);
    }

	if( info->dummy ) free(info->dummy );
	
    free( info->seq );
    free(info->status_what);
	free(info->homedir);  
    free(info->uc);
	free(info->status_line);	
	if(info->cpumask) free(info->cpumask);
	if(info->mask) free(info->mask);
	if(info->rlinks) free(info->rlinks);
    if(info->rmodes) free(info->rmodes );
	if(info->splitted_screens) free(info->splitted_screens);
	free(settings);
    free(info);

    return 1;
}

void veejay_busy(veejay_t * info)
{
    video_playback_setup *settings = (video_playback_setup*)(info->settings);

    veejay_msg(VEEJAY_MSG_DEBUG, "Waiting for threads to finish...");
    
	// audio master clock
    if (settings->audio_playback_thread) {
        pthread_join(settings->audio_playback_thread, NULL);
        veejay_msg(VEEJAY_MSG_DEBUG, "Producer thread finished.");
    }

	// display render thread
    if (settings->renderer_thread) {
        pthread_join(settings->renderer_thread, NULL);
        veejay_msg(VEEJAY_MSG_DEBUG, "Renderer thread finished.");
    }
    
	// producer
    if (settings->producer_thread) {
        pthread_join(settings->producer_thread, NULL);
        veejay_msg(VEEJAY_MSG_DEBUG, "Producer thread finished.");
    }
    
    veejay_msg(VEEJAY_MSG_INFO, "Playback engine terminated.");
}

void veejay_quit(veejay_t * info)
{
	veejay_change_state(info, LAVPLAY_STATE_STOP);
}

int veejay_set_frame(veejay_t *info, long framenum)
{
    video_playback_setup *settings = (video_playback_setup *)info->settings;

    if (framenum < settings->min_frame_num)
        framenum = settings->min_frame_num;

    if (framenum > settings->max_frame_num)
        framenum = settings->max_frame_num;

    if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
        int start, end, loop, speed;
        sample_get_short_info(info->uc->sample_id, &start, &end, &loop, &speed);

        if (framenum < start)
            framenum = start;
        if (framenum > end)
            framenum = end;

        if (framenum == start || framenum == end)
            sample_set_framedups(info->uc->sample_id, 0);
    }
    else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG) {
        if (framenum > settings->max_frame_num)
            framenum = settings->max_frame_num;
    }
	long long current_frame_num = atomic_load_long_long(&settings->current_frame_num);
	long long delta = abs( settings->current_frame_num - framenum);

	if(delta != abs(settings->current_playback_speed) ) {
		int prev_dir = playback_dir( settings->current_playback_speed );
		int cur_dir = playback_dir( current_frame_num - framenum );
		vj_perform_initiate_edge_change(info, AUDIO_EDGE_JUMP, prev_dir, cur_dir);
	}

    atomic_store_long_long(&settings->current_frame_num, framenum);

    return 1;
}


int	veejay_composite_active( veejay_t *info )
{
	return info->settings->composite;
}

void	veejay_auto_loop(veejay_t *info)
{
	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN)
	{
		char sam[32];
		int len;

		len = sprintf(sam, "%03d:0 -1;", VIMS_SAMPLE_NEW);
		vj_event_parse_msg(info, sam, len);

		len = sprintf(sam, "%03d:-1;", VIMS_SAMPLE_SELECT);
		vj_event_parse_msg(info,sam,len);
	}
}

void	veejay_set_framerate( veejay_t *info , float fps )
{
	video_playback_setup *settings = (video_playback_setup*) info->settings;
	settings->spvf = 1.0 / (double) fps;

    settings->usec_per_frame = vj_el_get_usec_per_frame(fps);
	veejay_msg(VEEJAY_MSG_DEBUG, "Playing at %f FPS, usec_per_frame set to %d" , fps, settings->usec_per_frame);
}


int veejay_init_editlist(veejay_t * info)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    editlist *el = info->edit_list;

	atomic_store_long_long(&settings->min_frame_num, 0);
	atomic_store_long_long(&settings->max_frame_num, (long long) el->total_frames);

    if (info->audio==AUDIO_PLAY && info->dummy->arate > 0)
    {
		settings->spas = 1.0 / (double) info->dummy->arate;
    }
    else
    {
 		settings->spas = 0.0;
    }

	if(info->video_output_height == 0 && info->video_output_width == 0) {
		info->video_output_width = el->video_width;
		info->video_output_height = el->video_height;
		veejay_msg(VEEJAY_MSG_WARNING, "Output dimensions not setup, using video resolution %dx%d", el->video_width, el->video_height);
	}

    return 0;
}

int	veejay_stop_playing_sample( veejay_t *info, int new_sample_id )
{
	video_playback_setup *settings = info->settings;
	int tx_active = atomic_load_int(&settings->transition.active);
    if( tx_active ) {
        return 0;
    }

	if(!sample_stop_playing( info->uc->sample_id, new_sample_id ) )
	{
		veejay_msg(0, "Error while stopping sample %d", new_sample_id );
		return 0;
	}
	
	if( info->composite ) {
		if( info->settings->composite == 2 ) {
			info->settings->composite = 1; // back to top
		} 
	}

	sample_chain_free( info->uc->sample_id,0);
	sample_set_framedups(info->uc->sample_id,0);

	return 1;
}
static  void	veejay_stop_playing_stream( veejay_t *info, int new_stream_id )
{
	video_playback_setup *settings = info->settings;
	int tx_active = atomic_load_int(&settings->transition.active);
    if( tx_active ) {
        return;
    }

	vj_tag_disable( info->uc->sample_id );
	if( info->composite ) {
		if( info->settings->composite == 2 ) {
			info->settings->composite = 1;
		}
	}

	vj_tag_chain_free( info->uc->sample_id, 0);
}
int	veejay_start_playing_sample( veejay_t *info, int sample_id )
{
	video_playback_setup *settings = info->settings;
    int looptype,speed,start,end;

    editlist *E = sample_get_editlist( sample_id );
	info->current_edit_list = E;
	veejay_reset_el_buffer(info);

	sample_start_playing( sample_id, info->no_caching );

   	sample_get_short_info( sample_id , &start,&end,&looptype,&speed);

	settings->first_frame = 1;
 
	atomic_store_long_long(&settings->min_frame_num, 0);
	atomic_store_long_long(&settings->max_frame_num, (long long) sample_video_length(sample_id));

	info->uc->sample_id = sample_id;
	info->last_sample_id = sample_id;

	info->sfd = sample_get_framedup(sample_id);

	sample_set_loop_stats( sample_id, 0 );
    sample_set_loops( sample_id, -1 ); /* reset loop count */

	if( speed != 0 )
	    settings->previous_playback_speed = speed;

    veejay_set_speed(info, speed,1);

	veejay_msg(VEEJAY_MSG_INFO, "Playing sample %d (Slow: %d, Speed: %d, Start: %d, End: %d, Looptype: %d, Current position: %d)",
			sample_id, info->sfd, speed, start, end, looptype, info->settings->current_frame_num
            );
	 
	return 1;
}

static int	veejay_start_playing_stream(veejay_t *info, int stream_id )
{
	video_playback_setup *settings = info->settings;
	
	if(vj_tag_enable( stream_id ) <= 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Unable to activate stream %d", stream_id);
	}

	if( settings->current_playback_speed == 0 )
		settings->current_playback_speed = 1;

	atomic_store_long_long(&settings->min_frame_num, 0);
	atomic_store_long_long(&settings->max_frame_num, (long long) vj_tag_get_n_frames(stream_id));

    info->last_tag_id = stream_id;
	info->uc->sample_id = stream_id;

	veejay_msg(VEEJAY_MSG_INFO,"Playing stream %d (%ld - %ld)", stream_id, settings->min_frame_num, settings->max_frame_num );

	info->current_edit_list = info->edit_list;
  	 
	veejay_reset_el_buffer(info);

    vj_tag_set_loop_stats( stream_id, 0 );
    vj_tag_set_loops( stream_id, -1 );
	
	return 1;
}

void veejay_change_playback_mode(veejay_t *info, int new_pm, int sample_id)
{
    video_playback_setup *settings = info->settings;
    int current_pm = info->uc->playback_mode;
    int cur_id     = info->uc->sample_id;

    if (new_pm == VJ_PLAYBACK_MODE_SAMPLE) {
        if (!sample_exists(sample_id)) {
            veejay_msg(VEEJAY_MSG_ERROR, "[Playback] Sample %d does not exist", sample_id);
            return;
        }
    } else if (new_pm == VJ_PLAYBACK_MODE_TAG) {
        if (!vj_tag_exists(sample_id)) {
            veejay_msg(VEEJAY_MSG_ERROR, "[Playback] Stream %d does not exist", sample_id);
            return;
        }
    } else if (new_pm == VJ_PLAYBACK_MODE_PLAIN) {
        if (info->edit_list->video_frames < 1) {
            veejay_msg(VEEJAY_MSG_ERROR, "[Playback] No video frames in EditList");
            return; 
        }
    }

    if (!info->seq->active && // transitions from current position in the timeline
        settings->transition.ready == 0 &&
        new_pm != VJ_PLAYBACK_MODE_PLAIN &&
        (sample_id != cur_id || new_pm != current_pm) &&
		atomic_load_int(&settings->transition.global_state)
	) 
    {
        settings->transition.next_type = new_pm;
        settings->transition.next_id = sample_id;

        int transition_length = (current_pm == VJ_PLAYBACK_MODE_SAMPLE)
                                    ? sample_get_transition_length(cur_id)
                                    : vj_tag_get_transition_length(cur_id);

        long long start = settings->current_frame_num;
        long long end;

        if (settings->current_playback_speed < 0) {
            end = start - transition_length;
            if (end < settings->min_frame_num) end = settings->min_frame_num;
        } else if (settings->current_playback_speed > 0) {
            end = start + transition_length;
            if (end > settings->max_frame_num) end = settings->max_frame_num;
        } else {
            end = start; 
        }
        
        atomic_store_long_long(&settings->transition.start, start);
        atomic_store_long_long(&settings->transition.end, end);

        int transition_shape = (current_pm == VJ_PLAYBACK_MODE_SAMPLE)
                                ? sample_get_transition_shape(cur_id)
                                : vj_tag_get_transition_shape(cur_id);

        settings->transition.shape = (transition_shape != -1)
                                        ? transition_shape
                                        : (int)(  ((double) shapewipe_get_num_shapes(settings->transition.ptr)) * rand() / (RAND_MAX));

        int active = (current_pm == VJ_PLAYBACK_MODE_SAMPLE)
                                        ? sample_get_transition_active(cur_id)
                                        : vj_tag_get_transition_active(cur_id);

        atomic_store_int(&settings->transition.active, active);

        veejay_msg(VEEJAY_MSG_INFO, "[Playback] Transition: length=%d, shape=%d, active=%d", 
                   transition_length, settings->transition.shape, active);

        if (active) {
            veejay_msg(VEEJAY_MSG_INFO, "[Playback] Deferring mode switch for transition");
            return;
        }
    }

    if (current_pm == VJ_PLAYBACK_MODE_SAMPLE)
    {
        if (cur_id == sample_id && new_pm == VJ_PLAYBACK_MODE_SAMPLE)
        {
            if (!info->seq->active) {
                if (info->settings->sample_restart) {
                    veejay_msg(VEEJAY_MSG_INFO, "[Playback] Restarting Sample %d", cur_id);
                    sample_set_resume_override(cur_id, -1);
                    veejay_sample_resume_at(info, cur_id);
                } else {
                    veejay_msg(VEEJAY_MSG_INFO, "[Playback] (Continuous mode) Sample %d already playing", sample_id);
                }
                return;
            }
        } else {
            veejay_msg(VEEJAY_MSG_INFO, "[Playback] Stopping Sample %d", cur_id);
            veejay_stop_playing_sample(info, cur_id);
        }
    }
    
    if (current_pm == VJ_PLAYBACK_MODE_TAG)
    {
        if (cur_id != sample_id) {
            veejay_msg(VEEJAY_MSG_INFO, "[Playback] Stopping Stream %d", cur_id);
            veejay_stop_playing_stream(info, cur_id);
        }
    }

    if (new_pm == VJ_PLAYBACK_MODE_PLAIN)
    {
        if (current_pm == VJ_PLAYBACK_MODE_TAG) veejay_stop_playing_stream(info, cur_id);
        if (current_pm == VJ_PLAYBACK_MODE_SAMPLE) veejay_stop_playing_sample(info, cur_id);
            
        info->uc->playback_mode = new_pm;
        info->current_edit_list = info->edit_list;

		atomic_store_long_long(&settings->min_frame_num, 0 );
		atomic_store_long_long(&settings->max_frame_num, (long long) info->edit_list->total_frames);
		atomic_store_long_long(&settings->current_frame_num, 0);
	}
    else if (new_pm == VJ_PLAYBACK_MODE_TAG)
    {
        info->uc->playback_mode = new_pm;
        
		atomic_store_long_long(&settings->min_frame_num, 0 );
        atomic_store_long_long(&settings->max_frame_num, (long long) vj_tag_get_n_frames(sample_id));
        atomic_store_long_long(&settings->current_frame_num, 0);

        veejay_start_playing_stream(info, sample_id);
    }
    else if (new_pm == VJ_PLAYBACK_MODE_SAMPLE) 
    {
        info->uc->playback_mode = new_pm;
        veejay_start_playing_sample(info, sample_id);
    }
    
    if (info->seq->active) {
		sample_set_resume(sample_id, -1);
        long pos = sample_get_resume(sample_id);
        veejay_set_frame(info, pos);
  		int next_id = sample_id;
		int next_slot = info->seq->current;
        int next_mode = new_pm;
		next_id = vj_perform_next_sequence( info, &next_mode,&next_slot );
        vj_perform_setup_transition( info, next_id, next_mode, sample_id, new_pm, next_slot );
    }
    else if (new_pm == VJ_PLAYBACK_MODE_SAMPLE) {
        veejay_sample_resume_at(info, sample_id);
    }
}

void veejay_sample_set_initial_positions(veejay_t *info)
{
	int first_sample = -1;
	int i;
	for( i = 0; i < MAX_SEQUENCES; i ++ ) {
			if( info->seq->samples[i].sample_id > 0 && first_sample == -1) {
				first_sample = i;
			}

			if( info->seq->samples[i].type != 0 ) 
				continue;
			int id = info->seq->samples[i].sample_id;
			int stats[4];
    
    		sample_set_loops(id,-1);
    		sample_get_short_info( id, &stats[0],&stats[1],&stats[2],&stats[3]);
    		if( stats[2] == 2 ) {
				if( stats[3] < 0 ) {
		  			sample_set_speed(id, -1 * stats[3]);
				}
            sample_set_resume(id, stats[0]);
         }
    	   else {
        		if( stats[3] < 0 ) { 
            	sample_set_resume(id, stats[1]);
        		}
       		else if(stats[3] > 0) {
            	sample_set_resume(id, stats[0]);
        		}
    	   } 
	}

	if(first_sample >= 0) {
		veejay_change_playback_mode(info, (info->seq->samples[first_sample].type == 0 ?
														VJ_PLAYBACK_MODE_SAMPLE : VJ_PLAYBACK_MODE_TAG), info->seq->samples[first_sample].sample_id);
	}
}

void veejay_prepare_sample_positions(int id) {
    int stats[4] = { 0,0,0,0 };
    
    sample_set_loops(id,-1);
    sample_get_short_info( id, &stats[0],&stats[1],&stats[2],&stats[3]);
    if( stats[2] == 2 ) {
        if( stats[3] < 0 ) { 
		  		sample_set_speed(id, -1 * stats[3]);
            sample_set_resume(id, stats[0]);
        }
    } 
    else {
        if( stats[3] < 0 ) { 
            sample_set_resume(id, stats[1]);
        }
        else if(stats[3] > 0) {
            sample_set_resume(id, stats[0]);
        }
    } 
}

void veejay_reset_sample_positions(veejay_t *info, int sample_id)
{
	if( sample_id == -1 ) {
		int i;
		for( i = 0; i < MAX_SEQUENCES; i ++ ) {
			if( info->seq->samples[i].type != 0 ) 
				continue;
			int id = info->seq->samples[i].sample_id;
            veejay_prepare_sample_positions(id);
        }
	}
	else {
        veejay_prepare_sample_positions(sample_id);
	}
}

void veejay_set_sample(veejay_t * info, int sampleid)
{
   	if ( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
	{
		veejay_start_playing_stream(info,sampleid );
   	}
    else if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
	{
		if( info->uc->sample_id == sampleid)
		{
			veejay_sample_resume_at( info, sampleid );
		}
		else
		{
			veejay_start_playing_sample(info,sampleid );
		}
	}
}

int veejay_create_tag(veejay_t * info, int type, char *filename,
			int index, int channel, int device)
{
	if( type == VJ_TAG_TYPE_NET || type == VJ_TAG_TYPE_MCAST ) {
		if( (filename != NULL) && ((strcasecmp( filename, "localhost" ) == 0)  || (strcmp( filename, "127.0.0.1" ) == 0)) ) {
			if( channel == info->uc->port )	{
				veejay_msg(VEEJAY_MSG_ERROR, "It makes no sense to connect to myself (%s - %d)",
					filename,channel);
				return 0;
			}	   
		}
	}

	int id = vj_tag_new(type, filename, index, info->edit_list, info->pixel_format, channel, device,info->settings->composite );
	char descr[200];
	veejay_memset(descr,0,200);
	vj_tag_get_by_type(id,type,descr);
	if(id > 0 )	{
		info->nstreams++;
		veejay_msg(VEEJAY_MSG_INFO, "New Input Stream '%s' with ID %d created",descr, id );
		return id;
	} else	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to create new Input Stream '%s'", descr );
    	}

 	return 0;
}

void veejay_stop_sampling(veejay_t * info)
{
    info->uc->playback_mode = VJ_PLAYBACK_MODE_PLAIN;
    info->uc->sample_id = 0;
    info->uc->sample_start = 0;
    info->uc->sample_end = 0;
    info->current_edit_list = info->edit_list;
}

VJFrame *veejay_video_queue_reserve_buffer(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    int idx = -1;

    pthread_mutex_lock(&settings->mutex);
    for (;;) {
        for (int i = 0; i < VIDEO_QUEUE_LEN; i++) {
            if (settings->states[i] == BUFFER_FREE) {
                idx = i;
                break;
            }
        }
        if (idx >= 0) break;

        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) {
            pthread_mutex_unlock(&settings->mutex);
            return NULL;
        }
        pthread_cond_wait(&settings->producer_wait_cv, &settings->mutex);
    }

    settings->states[idx] = BUFFER_RESERVED;
    settings->buffers[idx]->queue_index = idx; 

    pthread_mutex_unlock(&settings->mutex);
    return settings->buffers[idx];
}

void veejay_video_queue_post_frame(veejay_t *info, VJFrame *vf)
{
    video_playback_setup *settings = info->settings;
    int idx = vf->queue_index;
    pthread_mutex_lock(&settings->mutex);

    settings->frame_seq[idx] = settings->next_seq++;
    settings->states[idx] = BUFFER_FILLED;

    pthread_cond_signal(&settings->renderer_wait_cv);
    pthread_mutex_unlock(&settings->mutex);
}
VJFrame *veejay_video_queue_get_frame(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    int idx = -1;

    pthread_mutex_lock(&settings->mutex);
    for (;;) {
        uint64_t best_seq = UINT64_MAX;
        idx = -1;
        for (int i = 0; i < VIDEO_QUEUE_LEN; i++) {
            if (settings->states[i] == BUFFER_FILLED && settings->frame_seq[i] < best_seq) {
                best_seq = settings->frame_seq[i];
                idx = i;
            }
        }
        if (idx >= 0) break;
        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) {
            pthread_mutex_unlock(&settings->mutex);
            return NULL;
        }
		// wait is here
        pthread_cond_wait(&settings->renderer_wait_cv, &settings->mutex);
    }

    settings->states[idx] = BUFFER_IN_RENDER;
    pthread_mutex_unlock(&settings->mutex);
	
    return settings->buffers[idx];
}

void video_queue_return_frame(veejay_t *info, VJFrame *vf)
{
    video_playback_setup *settings = info->settings;
    int idx = vf->queue_index;

    pthread_mutex_lock(&settings->mutex);
    settings->states[idx] = BUFFER_FREE;

    pthread_cond_signal(&settings->producer_wait_cv);
    pthread_mutex_unlock(&settings->mutex);
}

static void veejay_screen_update(veejay_t *info, VJFrame *frame_to_display) {
    video_playback_setup *settings = (video_playback_setup*) info->settings;
    display_frame_t *df = &settings->display_frame;

    int render_vp  = settings->composite;
    int write_index = (df->current_write + 1) % VIDEO_QUEUE_LEN;
    uint8_t *pixels = df->pixels[write_index];

	switch (info->video_out) {
		case 0:
			if (render_vp == 0) {
				vj_sdl_convert_to_screen(info->sdl, frame_to_display, pixels );

			} else {
				composite_blit_yuyv(info->composite, frame_to_display->data, pixels, render_vp);
			}
			break;// SDL
		case 5:
			break;
		case 4: // Y4M
		case 6:
			if( vj_yuv_put_frame( info->y4m, frame_to_display->data ) == -1 ) {
				veejay_msg(0, "Failed to write a frame");
				veejay_change_state(info,LAVPLAY_STATE_STOP);
				return;
			}
			break;
		default:
			break; // Headless 
	}

	if( info->vloopback )
	{
		vj_vloopback_fill_buffer( info->vloopback , frame_to_display );		
		vj_vloopback_write( info->vloopback );
	}

	if( info->splitter ) {
		vj_split_render( info->splitter );
	}
	
	if( atomic_load_int(&info->settings->unicast_frame_sender) ) {
		vj_perform_send_primary_frame_s2(info, 0, info->uc->current_link, frame_to_display );
	}

	if( info->settings->mcast_frame_sender && info->settings->use_vims_mcast ) {
		vj_perform_send_primary_frame_s2(info, 1, info->uc->current_link, frame_to_display);
	}

	if( info->shm && vj_shm_get_status(info->shm) == 1 ) {
		int plane_sizes[4] = { frame_to_display->len, frame_to_display->uv_len,
		   		frame_to_display->uv_len,0 };	
		if( vj_shm_write(info->shm, frame_to_display->data,plane_sizes) == -1 ) {
			veejay_msg(0, "[DISPLAY] Failed to write to shared resource");
		}
	}


#ifdef HAVE_JPEG
#ifdef USE_GDK_PIXBUF 
	if (info->uc->take_screenshot == 1)
	{
		info->uc->take_screenshot = 0;
#ifdef USE_GDK_PIXBUF
		if(!vj_picture_save( info->settings->export_image, frame_to_display->data, 
			info->video_output_width, info->video_output_height,
			get_ffmpeg_pixfmt( info->pixel_format )) )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to write frame %lld to image as '%s'",	frame_to_display->frame_num, info->settings->export_image );
		}
#else
#ifdef HAVE_JPEG
		vj_perform_screenshot2(info, frame_to_display);
		if(info->uc->filename) free(info->uc->filename);
#endif
#endif
    }
#endif
#endif

    df->current_write = write_index;
    settings->display_frame.pixels[write_index] = pixels;
    atomic_store_long_long(&settings->display_frame.seq, frame_to_display->frame_num);
}



static void veejay_pipe_write_status(veejay_t * info)
{
    video_playback_setup *settings = (video_playback_setup *) info->settings;
    int d_len = 0;
    int pm = info->uc->playback_mode;
    int n_samples = sample_size();
    int tags = vj_tag_size();

	int cache_used = 0;
	int mstatus = 0;
	int curfps  = (int) ( 100.0f / settings->spvf );
	int total_slots = n_samples;
	int seq_cur = (info->seq->active ? info->seq->current : MAX_SEQUENCES );
	if(tags>0)
		total_slots+=tags;

	veejay_memset( info->status_what, 0, sizeof(info->status_what));
  
	switch (info->uc->playback_mode) {
    	case VJ_PLAYBACK_MODE_SAMPLE:
			cache_used = sample_cache_used(0);

		mstatus = vj_macro_get_status( sample_get_macro( info->uc->sample_id ));

		if( info->settings->randplayer.mode == RANDMODE_SAMPLE)
			pm = VJ_PLAYBACK_MODE_PATTERN;

		if( sample_chain_sprint_status
			(info->uc->sample_id, tags,cache_used,info->seq->size,seq_cur,info->real_fps,settings->current_frame_num, pm, total_slots,info->seq->rec_id,curfps,settings->cycle_count[0],settings->cycle_count[1],mstatus,info->status_what, settings->feedback ) != 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Fatal error, tried to collect properties of invalid sample");
			veejay_change_state( info, LAVPLAY_STATE_STOP );
		}
		break;
       case VJ_PLAYBACK_MODE_PLAIN:
            {
                char *ptr = info->status_what;
                
                ptr = vj_sprintf( ptr, info->real_fps );
                ptr = vj_sprintf( ptr, settings->current_frame_num );
                ptr = vj_sprintf( ptr, info->uc->playback_mode );
                
                *ptr++ = '0'; *ptr++ = ' ';
                *ptr++ = '0'; *ptr++ = ' ';
                
                ptr = vj_sprintf( ptr, settings->min_frame_num );
                ptr = vj_sprintf( ptr, settings->max_frame_num );
                ptr = vj_sprintf( ptr, settings->current_playback_speed );
                
                for(int i=0; i<4; i++) { *ptr++ = '0'; *ptr++ = ' '; }

                ptr = vj_sprintf( ptr, n_samples );
                
                for(int i=0; i<3; i++) { *ptr++ = '0'; *ptr++ = ' '; }

                ptr = vj_sprintf( ptr, total_slots );
                ptr = vj_sprintf( ptr, cache_used );
                ptr = vj_sprintf( ptr, curfps );
                ptr = vj_sprintf( ptr, settings->cycle_count[0] );
                ptr = vj_sprintf( ptr, settings->cycle_count[1] );

                for(int i=0; i<3; i++) { *ptr++ = '0'; *ptr++ = ' '; }

                ptr = vj_sprintf( ptr, info->sfd);
                ptr = vj_sprintf( ptr, mstatus );

                for(int i=0; i<8; i++) { *ptr++ = '0'; *ptr++ = ' '; }

                ptr = vj_sprintf( ptr, settings->feedback);
                ptr = vj_sprintf( ptr, tags);
                
            }
        break;
    	case VJ_PLAYBACK_MODE_TAG:

		mstatus = vj_macro_get_status( vj_tag_get_macro( info->uc->sample_id ));

		if( vj_tag_sprint_status( info->uc->sample_id,n_samples,cache_used,info->seq->size,seq_cur, info->real_fps,
			settings->current_frame_num, info->uc->playback_mode,total_slots,info->seq->rec_id,curfps,settings->cycle_count[0],settings->cycle_count[1],mstatus, info->status_what, settings->feedback ) != 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid status");
		}
		break;
    }
   
	d_len = strlen(info->status_what);
	info->status_line_len = d_len + 5;
	snprintf( info->status_line, MESSAGE_SIZE, "V%03dS%s", d_len, info->status_what );

    if (info->uc->chain_changed == 1)
		info->uc->chain_changed = 0;
}

static inline char *veejay_concat_paths(const char *path, const char *suffix)
{
    size_t len1 = strlen(path);
    size_t len2 = strlen(suffix);

    int need_slash = (len1 > 0 && path[len1 - 1] != '/');

    size_t total = len1 + len2 + (need_slash ? 1 : 0) + 1;

    char *str = vj_calloc(total);
	if(!str) {
		return NULL;
	}

    char *p = str;

    memcpy(p, path, len1);
    p += len1;

    if (need_slash) {
        *p++ = '/';
    }

    memcpy(p, suffix, len2);
    p += len2;

    *p = '\0';

    return str;
}

static inline int	veejay_is_dir(char *path)
{
	struct stat s;
	if( stat( path, &s ) == -1 )
	{
		veejay_msg(0, "%s (%s)", strerror(errno),path);
		return 0;
	}
	if( !S_ISDIR( s.st_mode )) {
		veejay_msg(0, "%s is not a valid path");
		return 0;
	}
	return 1;
}	
static	int	veejay_valid_homedir(char *path)
{
	char *recovery_dir = veejay_concat_paths( path, "recovery" );
	char *theme_dir    = veejay_concat_paths( path, "theme" );
	int sum = veejay_is_dir( recovery_dir );
	sum += veejay_is_dir( theme_dir );
	sum += veejay_is_dir( path );
	free(theme_dir);
	free(recovery_dir);
	if( sum == 3 ) 
		return 1;
	return 0;
}
static	int	veejay_create_homedir(char *path)
{
	if( mkdir(path,0700 ) == -1 )
	{
		if( errno != EEXIST )
		{
			veejay_msg(0, "Unable to create %s - No veejay home setup (%s)", strerror(errno));
			return 0;	
		}
	}

	char *recovery_dir = veejay_concat_paths( path, "recovery" );
	if( mkdir(recovery_dir,0700) == -1 ) {
		if( errno != EEXIST )
		{
			veejay_msg(0, "%s (%s)", strerror(errno), path);
			free(recovery_dir);
			return 0;
		}
	}
	free(recovery_dir);

	char *theme_dir = veejay_concat_paths( path, "theme" );
	if( mkdir(theme_dir,0700) == -1 ) {
		if( errno != EEXIST )
		{
			veejay_msg(0, "%s (%s)", strerror(errno), path);
			free(theme_dir);
			return 0;
		}
	}
	free(theme_dir);
	
	char *font_dir = veejay_concat_paths( path, "fonts" );
	if( mkdir(font_dir,0700) == -1 ) {
		if( errno != EEXIST )
		{
			veejay_msg(0, "%s (%s)", strerror(errno), path);
			free(font_dir);
			return 0;
		}
	}
	free(font_dir);
	return 1;
}

#define PATH_TMP_SIZE 1024
#define PATH_SUFFIX_SIZE 64
void	veejay_check_homedir(void *arg)
{
	veejay_t *info = (veejay_t *) arg;
	char path[ PATH_TMP_SIZE - PATH_SUFFIX_SIZE ];
	char tmp[ PATH_TMP_SIZE ];
	char *home = getenv("HOME");
	if(!home)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "HOME environment variable not set");
		return;
	}
	
	int n = snprintf(path,sizeof(path), "%s/.veejay", home );
	if( n < 0 || n >= (int) sizeof(path)) {
		veejay_msg(VEEJAY_MSG_ERROR, "HOME path too long: %s", home );
		return;
	}

	info->homedir = vj_strndup( path, sizeof(path) );

	if( veejay_valid_homedir(path) == 0)
	{
		if( veejay_create_homedir(path) == 0 )
		{	
			veejay_msg(VEEJAY_MSG_ERROR,
				"Can't create %s",path);
			return;
		}
	}

	n = snprintf(tmp,sizeof(tmp), "%s/plugins.cfg", path );
	if(n < 0 || n >= (int) sizeof(tmp)) {
		veejay_msg(VEEJAY_MSG_ERROR, "Path too long for plugins.cfg: %s", path );
		return;
	}

	struct statfs ts;
	if( statfs( tmp, &ts ) != 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING,"No plugin configuration file found in [%s] (see DOC/HowtoPlugins)",tmp);
	}
	
	n = snprintf(tmp,sizeof(tmp), "%s/viewport.cfg", path);
	if( n < 0 || n >= (int) sizeof(tmp)) {
		veejay_msg(VEEJAY_MSG_ERROR, "Path too long for viewport.cfg: %s",path);
		return;
	}

	if( statfs( tmp, &ts ) != 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING,"No projection mapping file found in [%s]", tmp);
	    veejay_msg(VEEJAY_MSG_WARNING,"Press CTRL-s to setup then CTRL-h for help. Press CTRL-s again to exit, CTRL-p to switch");
	}
}

void veejay_handle_signal(int sig, siginfo_t *si, void *unused)
{
    veejay_t *info = veejay_instance_;

    switch (sig) {
        case SIGINT:
        case SIGQUIT:
            veejay_change_state(info, LAVPLAY_STATE_STOP);
            break;

        case SIGSEGV:
        case SIGBUS:
        case SIGPWR:
        case SIGABRT:
        case SIGFPE:
            if (info && info->homedir) {
                veejay_change_state_save(info, LAVPLAY_STATE_STOP);
            } else if (info) {
                veejay_change_state(info, LAVPLAY_STATE_STOP);
            }
            signal(sig, SIG_DFL);
            break;

        case SIGPIPE:
            if (info) veejay_change_state(info, LAVPLAY_STATE_STOP);
            break;

        default:
            veejay_msg(VEEJAY_MSG_WARNING, "Unhandled signal %d received", sig);
            break;
    }
}


static void veejay_handle_callbacks(veejay_t *info) {
	video_playback_setup *settings = info->settings;

	vj_osc_get_packet(info->osc);

	vj_event_update_remote( (void*)info );

	if( settings->randplayer.next_id != 0 ) {
		veejay_change_playback_mode( info, settings->randplayer.next_mode, settings->randplayer.next_id);
		settings->randplayer.next_id = 0;
	}

	veejay_pipe_write_status(info);

	int i;
	for( i = 0; i < VJ_MAX_CONNECTIONS ; i ++ ) {
		if( !vj_server_link_can_write( info->vjs[VEEJAY_PORT_STA],  i ) ) 
			continue;	
		int res = vj_server_send( info->vjs[VEEJAY_PORT_STA], i, (uint8_t*)info->status_line, info->status_line_len);
		if( res < 0 ) {
			_vj_server_del_client( info->vjs[VEEJAY_PORT_CMD], i );
			_vj_server_del_client( info->vjs[VEEJAY_PORT_STA], i );
			_vj_server_del_client( info->vjs[VEEJAY_PORT_DAT], i );
		}
	}
}

void vj_lock(veejay_t *info)
{
	video_playback_setup *settings = info->settings;
	pthread_mutex_lock(&(settings->control_mutex));
}
void vj_unlock(veejay_t *info)
{
	video_playback_setup *settings = info->settings;
	pthread_mutex_unlock(&(settings->control_mutex));
}	 

static void 	veejay_consume_events(veejay_t *info) {
#ifdef HAVE_SDL
	SDL_Event event;
	int ctrl_pressed = 0;
	int shift_pressed = 0;
	int alt_pressed = 0;
	int mouse_x=0,mouse_y=0,but=0;
	int mod = 0;

	veejay_handle_callbacks(info);

	while( vj_event_pop(&event, &mod)) {
		if( event.type == SDL_QUIT ) {
			veejay_change_state(info, LAVPLAY_STATE_STOP );
		}
		if( event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN)
		{
			vj_event_single_fire( (void*) info, event, 0);
		}
		if( event.type == SDL_MOUSEMOTION )
		{
			mouse_x = event.button.x;
			mouse_y = event.button.y;
		}

		if( info->use_mouse && event.type == SDL_MOUSEBUTTONDOWN )
		{
			mouse_x = event.button.x;
			mouse_y = event.button.y;
			shift_pressed = (mod & KMOD_LSHIFT );
			alt_pressed = (mod & KMOD_RSHIFT );
			if( mod == 0x1080 || mod == 0x1040 || (mod & KMOD_LCTRL) || (mod & KMOD_RCTRL) )
				ctrl_pressed = 1; 
			else
				ctrl_pressed = 0;

			SDL_MouseButtonEvent *mev = &(event.button);

			if( mev->button == SDL_BUTTON_LEFT && shift_pressed)
			{
				but = 6;
				info->uc->mouse[3] = 1;
			} else if( mev->button == SDL_BUTTON_LEFT && ctrl_pressed )
			{
				but = 10;
				info->uc->mouse[3] = 4;
			}
			if (mev->button == SDL_BUTTON_MIDDLE && shift_pressed )
			{
				but = 7;
				info->uc->mouse[3] = 2;
			}
			if( mev->button == SDL_BUTTON_LEFT && alt_pressed )
			{
				but = 11;
				info->uc->mouse[3] = 11;
			}
		}

		if( info->use_mouse && event.type == SDL_MOUSEBUTTONUP )
		{
			SDL_MouseButtonEvent *mev = &(event.button);
			alt_pressed = (mod & KMOD_RSHIFT );
			if( mod == 0x1080 || mod == 0x1040 || (mod & KMOD_LCTRL) || (mod & KMOD_RCTRL) )
				ctrl_pressed = 1; 
			else
				ctrl_pressed = 0;

			if( mev->button == SDL_BUTTON_LEFT )
			{
				if( info->uc->mouse[3] == 1 )
				{
					but = 6;
					info->uc->mouse[3] = 0;
				}
				else if (info->uc->mouse[3] == 4 )
				{	
					but = 10;
					info->uc->mouse[3] = 0;
				} else if (info->uc->mouse[3] == 0 )
				{
					but = 1;
				} else if ( info->uc->mouse[3] == 11 )
				{	
					but = 12;
					info->uc->mouse[3] = 0;
				}
			}
			else if (mev->button == SDL_BUTTON_RIGHT ) {
				but = 2;
			}
			else if (mev->button == SDL_BUTTON_MIDDLE ) {
				if( info->uc->mouse[3] == 2 )
				{	but = 0;
					info->uc->mouse[3] = 0;
				}
				else {if( info->uc->mouse[3] == 0 )
					but = 3;}
			}
			mouse_x = event.button.x;
			mouse_y = event.button.y;
			}
			if( info->use_mouse && event.type == SDL_MOUSEWHEEL ) {
			if ((event.wheel.y > 0 ) && !alt_pressed && !ctrl_pressed)
			{
				but = 4;
			}
			else if ((event.wheel.y < 0) && !alt_pressed && !ctrl_pressed)
			{
				but = 5;
			}
			else if ((event.wheel.y  > 0) && alt_pressed && !ctrl_pressed )
			{
				but = 13;
			}
			else if ((event.wheel.y < 0) && alt_pressed && !ctrl_pressed )
			{
				but = 14;
			}
			else if (event.wheel.y > 0)  
			{	
				but = 15;
			}	
			else if (event.wheel.y < 0)
			{
				but = 16;
			}
			mouse_x = event.button.x;
			mouse_y = event.button.y;
		}
		
	}
	info->uc->mouse[0] = mouse_x;
	info->uc->mouse[1] = mouse_y;
	info->uc->mouse[2] = but;
#endif
}


void	veejay_event_handle(veejay_t *info)
{

#ifdef HAVE_SDL
	if( info->video_out == 0 || info->video_out == 2)
	{
		SDL_Event event;
		while(SDL_PollEvent(&event) == 1) 
		{

			vj_event_push(&event, SDL_GetModState());
		}
	}
#endif
}

int veejay_setup_video_out(veejay_t *info) {
	char *title;
	int x = 0, y = 0;
	editlist *el = info->edit_list;
	video_playback_setup *settings = info->settings;

	if( info->uc->geox != 0 && info->uc->geoy != 0 )
	{
		x = info->uc->geox;
		y = info->uc->geoy;
	}

	switch(info->video_out) {
		case 0:
			veejay_msg(VEEJAY_MSG_INFO, "Using output driver SDL");
#ifdef HAVE_SDL
			info->sdl = vj_sdl_allocate( info->effect_frame1, info->use_keyb, info->use_mouse,info->show_cursor, info->borderless);
			if( !info->sdl )
				return -1;

			title = veejay_title( info );

			if (!vj_sdl_init(info->sdl, x,y,info->video_output_width, info->video_output_height, info->bes_width, info->bes_height, title,1,info->settings->full_screen,info->pixel_format,el->video_fps, &settings->vsync_interval_s))
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Error initializing SDL");
				free(title);
				return -1;
			}
			free(title);
#endif
		break;

		case 1:
			veejay_msg(VEEJAY_MSG_INFO, "Using output driver DirectFB");
#ifdef HAVE_DIRECTFB
			info->dfb =(vj_dfb *) vj_dfb_allocate(info->video_output_width,info->video_output_height,el->video_norm);
			if( !info->dfb )
				return -1;
			if (vj_dfb_init(info->dfb) != 0)
				return -1;
#endif
		break;


		case 2:
			veejay_msg(VEEJAY_MSG_INFO, 
			           "Using output driver SDL & DirectFB");
#ifdef HAVE_SDL
			info->sdl = vj_sdl_allocate(info->effect_frame1, info->use_keyb,info->use_mouse,info->show_cursor, info->borderless);
			if(!info->sdl)
				return -1;

			title = veejay_title(info);	
			if (!vj_sdl_init(info->sdl,x,y,info->video_output_width, info->video_output_height, info->bes_width, info->bes_height,title,1,info->settings->full_screen,info->pixel_format, el->video_fps, &settings->vsync_interval_s)) {
				free(title);
				return -1;
			}
			free(title);
#endif
#ifdef HAVE_DIRECTFB
			info->dfb = (vj_dfb *) vj_dfb_allocate( info->video_output_width, info->video_output_height, el->video_norm);
			if(!info->dfb)
				return -1;

			if (vj_dfb_init(info->dfb) != 0)
				return -1;
#endif
		break;
		case 3:
	    case 4:
		case 5:
		case 6:
		case 7:
		case 8:
			break;
		default:
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode. Use -O [012345678]");
			return -1;
	}

	//vj_sdl_preroll(info->sdl, 5);

	return 0;
}

static double vj_get_relative_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    double t = (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
    return t;
}

int veejay_mjpeg_software_frame_sync_interruptible(veejay_t *info, long wait_us, pthread_cond_t *sync_cv)
{
    video_playback_setup *settings = (video_playback_setup *) info->settings;
    if (wait_us <= 0) return 0;

    double start_time = vj_get_relative_time();

    struct timespec abstime;
    clock_gettime(CLOCK_MONOTONIC, &abstime);

    long total_ns = abstime.tv_nsec + wait_us * 1000L;
    abstime.tv_sec += total_ns / 1000000000L;
    abstime.tv_nsec = total_ns % 1000000000L;

    pthread_mutex_lock(&settings->mutex);

    struct timespec now;
    const long max_sleep_us = 2000; // 2 ms maximum sleep interval

    while (atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
        clock_gettime(CLOCK_MONOTONIC, &now);

        long rem_us = (abstime.tv_sec - now.tv_sec) * 1000000L +
                      (abstime.tv_nsec - now.tv_nsec) / 1000L;

        if (rem_us <= 0) break; // done waiting

        long sleep_us = rem_us > max_sleep_us ? max_sleep_us : rem_us;

        struct timespec short_abstime;
        clock_gettime(CLOCK_MONOTONIC, &short_abstime);
        long total_ns2 = short_abstime.tv_nsec + sleep_us * 1000L;
        short_abstime.tv_sec += total_ns2 / 1000000000L;
        short_abstime.tv_nsec = total_ns2 % 1000000000L;

        pthread_cond_timedwait(sync_cv, &settings->mutex, &short_abstime);
    }

    pthread_mutex_unlock(&settings->mutex);

    double end_time = vj_get_relative_time();
    veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Display thread slept %.6f s (requested %.6f s)",end_time - start_time, wait_us / 1.0e6);

    return 0;
}

char	*veejay_title(veejay_t *info)
{
	char tmp[128];
	snprintf(tmp,sizeof(tmp), "Veejay %s on port %d in %dx%d@%2.2f", VERSION,
	      info->uc->port, info->video_output_width,info->video_output_height,info->dummy->fps );
	return strdup(tmp);
}

void *veejay_display_renderer_thread(void *arg)
{
    veejay_t *info = (veejay_t *) arg;
    video_playback_setup *settings = info->settings;

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);


/*
	settings->warmup_frames = 2;

    veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Starting GPU warm-up");

	int drained = 0;
	while (drained < settings->warmup_frames &&
		atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {

		VJFrame *frame = veejay_video_queue_get_frame(info);
		if (!frame) continue;

		veejay_screen_update(info, frame);
		video_queue_return_frame(info, frame);
		drained++;
	}
    veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] GPU warm-up complete (drained %d video frames)", drained);
	*/

    // handshake with producer
    pthread_mutex_lock(&settings->start_mutex);
    settings->video_out_ready = 1;
    pthread_cond_broadcast(&settings->start_cond);
    pthread_mutex_unlock(&settings->start_mutex);

	veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Waiting for first audio frame to be ready");
    while (atomic_load_int(&settings->first_audio_frame_ready) == 0 &&
           atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
        usleep_accurate(200, settings);
    }

    double audio_start_offset = atomic_load_double(&settings->audio_start_offset);
    veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Audio anchor established at %.6f s", audio_start_offset);

	while (atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {

		VJFrame *frame = veejay_video_queue_get_frame(info);
		if (!frame) break;

		if (frame->frame_num < 0) {
			video_queue_return_frame(info, frame);
			continue;
		}

		double audio_master = atomic_load_double(&settings->audio_master_s);
		double target_time_s = audio_start_offset + (frame->frame_num * settings->spvf);
		double delay_s = target_time_s - audio_master;

		// drop frame if too late
		if (delay_s < -settings->spvf) {
			veejay_screen_update(info, frame);	// even if late, display and immediately continue
			video_queue_return_frame(info,frame);
			continue;
		}

		if (delay_s > (settings->spvf * 1.5f)) {
			// audio catchup
			usleep_accurate((long long)(settings->spvf * 1.0e6), settings);
			veejay_screen_update(info, frame);		
			video_queue_return_frame(info, frame);
			continue;
		}
		else{
			if (delay_s > 0) {
				usleep_accurate((long long)(delay_s * 1.0e6), settings);
			}
			veejay_screen_update(info, frame);
			video_queue_return_frame(info, frame);
		}
		/*if (delay_s > 0) {
			double safe_wait = (delay_s > settings->spvf) ? settings->spvf : delay_s;
			long long wait_us = (long long)(safe_wait * 1.0e6);

			usleep_accurate(wait_us, settings);
	
			veejay_screen_update(info, frame);

			video_queue_return_frame(info,frame);
		}*/

		// hold frame for remaining duration based on audio_master_s
		audio_master = atomic_load_double(&settings->audio_master_s); // refresh
		/*double hold_s = next_target_time_s - audio_master;
		if (hold_s > settings->spvf) hold_s = settings->spvf;
		if (hold_s > 0 && delay_s > 0) {
			usleep_accurate((long long)(hold_s * 1.0e6), settings);
		}*/
	}

    veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Renderer thread exiting...");
	pthread_exit(NULL);
    return NULL;
}


static void Welcome(veejay_t *info)
{
	veejay_msg(VEEJAY_MSG_INFO, "Video project settings: %ldx%ld, Norm: [%s], fps [%2.2f], %s",
			info->video_output_width,
			info->video_output_height,
			info->current_edit_list->video_norm == 'n' ? "NTSC" : "PAL",
			info->current_edit_list->video_fps, 
			info->current_edit_list->video_inter==0 ? "Not interlaced" : "Interlaced" );
	if(info->audio==AUDIO_PLAY && info->edit_list->has_audio)
	veejay_msg(VEEJAY_MSG_INFO, "                        %ldHz %d Channels %dBps (%d Bit)",
			info->current_edit_list->audio_rate,
			info->current_edit_list->audio_chans,
			info->current_edit_list->audio_bps,
			info->current_edit_list->audio_bits);

	veejay_msg(VEEJAY_MSG_INFO, "Bezeker: %s", (info->bezerk == 1 ? "Sample restart when switching mixing channels" : 
		"No sample restart when switching mixing channels" ));
  	veejay_msg(VEEJAY_MSG_INFO, "Log level: %s", (info->verbose == 0 ? "Normal" : "Verbose" ));

	if(info->settings->composite )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Software composite - projection screen is %d x %d",
			info->video_output_width, info->video_output_height );
	}

	veejay_msg(VEEJAY_MSG_INFO,"Type 'man veejay' in a shell to learn more about veejay");
	veejay_msg(VEEJAY_MSG_INFO,"For a list of events, type 'veejay -u |less' in a shell");
	veejay_msg(VEEJAY_MSG_INFO,"Use 'reloaded' to enter interactive mode");
	veejay_msg(VEEJAY_MSG_INFO,"Alternatives are OSC applications or 'sendVIMS' extension for PD"); 

	int k = verify_working_dir();
	if( k > 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING,
			"Found %d veejay project files in current working directory (.edl,.sl, .cfg,.avi)",k);
		veejay_msg(VEEJAY_MSG_WARNING,
			"If you want to start a new project, start veejay in an empty directory");
	}
}


int veejay_open(veejay_t * info)
{
    video_playback_setup *settings = (video_playback_setup *) info->settings;
	pthread_condattr_t attr;
	pthread_condattr_init(&attr);
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);

    int i;
    
    pthread_mutex_init(&(settings->control_mutex), NULL);
    
    pthread_mutex_init(&settings->mutex, NULL);
    pthread_cond_init(&settings->producer_wait_cv, NULL);
    pthread_cond_init(&settings->renderer_wait_cv, &attr);
	pthread_cond_init(&settings->data_ready_cv, &attr);
    
    settings->producer_index = 0;
    settings->renderer_index = 0;
    settings->frames_available = 0;
	settings->warmup_frames = 2;
 
    for(i = 0; i < VIDEO_QUEUE_LEN ; i ++ )
    {
        settings->buffers[i] = veejay_allocate_frame_buffer(info); 
        if (!settings->buffers[i]) {
             veejay_msg(VEEJAY_MSG_ERROR, "Failed to allocate queue buffer %d. Aborting.", i);
             return 0;
        }
    }

	return 1;
}

static int veejay_get_norm( char n )
{
	if( n == 'p' )
		return VIDEO_MODE_PAL;
	if( n == 's' )
		return VIDEO_MODE_SECAM;
	if( n == 'n' )
		return VIDEO_MODE_NTSC;

	return VIDEO_MODE_PAL;
}

static int veejay_mjpeg_set_playback_rate(veejay_t *info, float video_fps, int norm)
{
	video_playback_setup *settings = (video_playback_setup *) info->settings;

	int fps_2dp = (int)(video_fps * 100.0f + 0.5f);

	switch (fps_2dp)
	{
		case 2398: /* 23.976 */
			settings->spvf = 1001.0 / 24000.0;
			break;

		case 2997: /* 29.97 */
			settings->spvf = 1001.0 / 30000.0;
			break;

		case 5994: /* 59.94 */
			settings->spvf = 1001.0 / 60000.0;
			break;

		default:
			settings->spvf = 1.0 / video_fps;
			break;
	}

	settings->usec_per_frame = vj_el_get_usec_per_frame(video_fps);

	veejay_msg(
		VEEJAY_MSG_INFO,
		"Playback rate is %f FPS, %d usec_per_frame, SPVF=%g",
		video_fps,
		settings->usec_per_frame,
		settings->spvf
	);

	return 1;
}


int veejay_close(veejay_t *info)
{
    video_playback_setup *settings = (video_playback_setup *)info->settings;
    int success = 1;

    veejay_change_state_save(info, LAVPLAY_STATE_STOP);

    pthread_mutex_lock(&settings->mutex);
    pthread_cond_broadcast(&settings->producer_wait_cv);
    pthread_cond_broadcast(&settings->renderer_wait_cv);
    pthread_mutex_unlock(&settings->mutex);

    if (pthread_join(settings->renderer_thread, NULL))
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to join Renderer thread."), success = 0;

    if (pthread_join(settings->producer_thread, NULL))
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to join Producer thread."), success = 0;

    if (pthread_join(settings->audio_playback_thread, NULL))
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to join Audio thread."), success = 0;

    pthread_mutex_destroy(&settings->mutex);
    pthread_cond_destroy(&settings->producer_wait_cv);
    pthread_cond_destroy(&settings->renderer_wait_cv);

    for (int i = 0; i < VIDEO_QUEUE_LEN; i++)
        if (settings->buffers[i]) veejay_free_frame_buffer(settings->buffers[i]);

    return success;
}

int veejay_init(veejay_t * info, int x, int y,char *arg, int def_tags, int gen_tags )
{
	editlist *el = NULL;
	video_playback_setup *settings = info->settings;

	int id=0;

	if(info->video_out<0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No video output driver selected (see man veejay)");
		return -1;
	}
	// override geometry set in config file
	if( info->uc->geox != 0 && info->uc->geoy != 0 )
	{
		x = info->uc->geox;
		y = info->uc->geoy;
	}

	vj_event_init((void*)info);

	if (veejay_init_editlist(info) != 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
		           "Cannot initialize the EditList");
		return -1;
	}

	el = info->edit_list;

	if(!vj_mem_threaded_init( info->video_output_width, info->video_output_height ) )
		return 0;

	vj_tag_set_veejay_t(info);

	int driver = 1;
	if (vj_tag_init(info->video_output_width, info->video_output_height, info->pixel_format,driver) != 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing Stream Manager");
		return -1;
	}
#ifdef HAVE_FREETYPE
	info->font = vj_font_init( info->video_output_width,info->video_output_height,el->video_fps,0 );

	if(!info->font) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing font system");
		return -1;
	}
#endif

	if(info->settings->splitscreen) {
		char split_cfg[1024];
		snprintf(split_cfg,sizeof(split_cfg),"%s/splitscreen.cfg", info->homedir );

		vj_split_set_master( info->uc->port );
		
		info->splitter = vj_split_new_from_file( split_cfg, info->video_output_width,info->video_output_height, PIX_FMT_YUV444P );
	}

	if(info->settings->composite)
	{
		info->osd = vj_font_single_init( info->video_output_width,info->video_output_height,el->video_fps,info->homedir  );
	}
	else
	{
		info->osd = vj_font_single_init( info->video_output_width,info->video_output_height,el->video_fps,info->homedir );
	}

	if(!info->osd) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing font system for OSD");
		return -1;
	}

	if(!sample_init( (info->video_output_width * info->video_output_height), info->font, info->plain_editlist,info ) ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Internal error while initializing sample administrator");
		return -1;
	}

	sample_set_project(info->pixel_format,
	                   info->auto_deinterlace,
	                   info->preserve_pathnames,
	                   0,
	                   el->video_norm,
	                   info->video_output_width,
	                   info->video_output_height);

	int full_range = veejay_set_yuv_range( info );
	yuv_set_pixel_range(full_range);
    vje_set_rgb_parameter_conversion_type(full_range);

	info->settings->sample_mode = SSM_422_444;

	if(( info->effect_frame1->width % 4) != 0 || (info->effect_frame1->height % 4) != 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "You should specify an output resolution that is a multiple of 4");
	}

	if(!vj_perform_init(info))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize Veejay Performer");
		return -1;
	}

	info->shm = vj_shm_new_master( info->homedir,info->effect_frame1 );
	if( !info->shm ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Unable to initialize shared resource");
	}

	if( info->settings->composite )
	{
		char path[1024];
		snprintf(path,sizeof(path),"%s/viewport.cfg", info->homedir);
		int comp_mode = info->settings->composite;

		info->composite = composite_init( info->video_output_width, info->video_output_height,
		                                 info->video_output_width, info->video_output_height,
		                                 path,
		                                 info->settings->sample_mode,
		                                 yuv_which_scaler(),
		                                 info->pixel_format,
		                                 &comp_mode);

		if(!info->composite) {
			return -1;
		}
		composite_set_file_mode( info->composite, info->homedir, info->uc->playback_mode,info->uc->sample_id);

		info->settings->zoom = 0;
		info->settings->composite = ( comp_mode == 1 ? 1: 0);
	}

	if(!info->bes_width)
		info->bes_width = info->video_output_width;
	if(!info->bes_height)
		info->bes_height = info->video_output_height;


	vje_init( info->video_output_width,info->video_output_height);

	if(info->dump)
		vje_dump();
	
    if( info->settings->action_scheduler.sl && info->settings->action_scheduler.state )
	{
		if(sample_readFromFile( info->settings->action_scheduler.sl,
		                       info->composite,
		                       info->seq, info->font, el, &(info->uc->sample_id), &(info->uc->playback_mode) ) )
			veejay_msg(VEEJAY_MSG_INFO, "Loaded samplelist %s from actionfile - ",
			           info->settings->action_scheduler.sl );
	}

	if( settings->action_scheduler.state )
	{
		settings->action_scheduler.state = 0;
	}

	int instances = 0;

	while( (instances < 4 ) && !vj_server_setup(info))
	{
		int port = info->uc->port;
		int new_port = info->uc->port + 1000;
		instances ++;
		veejay_msg((instances < 4 ? VEEJAY_MSG_WARNING: VEEJAY_MSG_ERROR),
			"Port %d -~ %d in use, trying to start on port %d (%d/%d attempts)", port,port+6, new_port , instances, 4 - instances);
		info->uc->port = new_port;
	}

	if( instances >= 4 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to start network server. Most likely, there is already a veejay running");
		veejay_msg(VEEJAY_MSG_ERROR,"If you want to run multiple veejay's on the same machine, use the '-p/--port' option");
		veejay_msg(VEEJAY_MSG_ERROR,"For example: $ veejay -p 4490 -d");
		return -1;
	}

	/* now setup the output driver */
	switch (info->video_out)
	{
		case 0:
		case 1:
		case 2:
			break;
		case 3:
			veejay_msg(VEEJAY_MSG_INFO, "Entering headless mode (no visual output)");
		break;

		case 4:
			veejay_msg(VEEJAY_MSG_INFO, "Entering Y4M streaming mode (420JPEG)");
			info->y4m = vj_yuv4mpeg_alloc( info->video_output_width,info->video_output_height,info->effect_frame1->fps, info->pixel_format );
			if( vj_yuv_stream_start_write( info->y4m, info->effect_frame1, info->y4m_file, Y4M_CHROMA_420JPEG ) == -1 )
			{
				return -1;
			}
		break;

		case 5:
			veejay_msg(VEEJAY_MSG_INFO, "Entering vloopback streaming mode. ");
			info->vloopback = vj_vloopback_open( info->y4m_file, info->effect_frame1, info->video_output_width, info->video_output_height, -1 );
			if( info->vloopback == NULL )
			{
				veejay_msg(0, "Cannot open %s as vloopback", info->y4m_file);
				return -1;
			}
		break;

		case 6:
			veejay_msg(VEEJAY_MSG_INFO, "Entering Y4M streaming mode (422)");
			info->y4m = vj_yuv4mpeg_alloc( info->video_output_width,info->video_output_height,info->effect_frame1->fps, info->pixel_format );
			if( vj_yuv_stream_start_write( info->y4m, info->effect_frame1, info->y4m_file, Y4M_CHROMA_422 ) == -1 ) {
				return -1;
			}
		break;
		case 7:
			veejay_msg(VEEJAY_MSG_INFO, "Entering vloopback streaming mode. ");
			info->vloopback = vj_vloopback_open( info->y4m_file, info->effect_frame1, info->video_output_width, info->video_output_height, PIX_FMT_YUV420P );
			if( info->vloopback == NULL )
			{
				veejay_msg(0, "Cannot open %s as vloopback", info->y4m_file);
				return -1;
			}
		break;
		case 8:
			veejay_msg(VEEJAY_MSG_INFO, "Entering vloopback streaming mode. ");
			info->vloopback = vj_vloopback_open( info->y4m_file, info->effect_frame1, info->video_output_width, info->video_output_height, PIX_FMT_BGR24);
			if( info->vloopback == NULL )
			{
				veejay_msg(0, "Cannot open %s as vloopback", info->y4m_file);
				return -1;
			}
		break;		
		default:
			break;
	}

	if( gen_tags > 0 ) {
		int total  = 0;
		int *world = plug_find_all_generator_plugins( &total );
		if( total == 0 ) {
			veejay_msg(0,"No generator plugins found");
			return -1;
		}
		int i;
		int plugrdy = 0;
		for ( i = 0; i < total; i ++ ) {
			int plugid = world[i];
			if( vj_tag_new( VJ_TAG_TYPE_GENERATOR, NULL,-1, el, info->pixel_format,
			               plugid, 0, 0 ) > 0 )
				plugrdy++;
		}

		free(world);

		if( plugrdy > 0 ) {
			veejay_msg(VEEJAY_MSG_INFO, "Initialized %d generators", plugrdy);
			info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
			info->uc->sample_id = ( gen_tags <= plugrdy ? gen_tags : 1 );
		} else {
			return -1;
		}
	}

	if(def_tags >= 0 && id <= 0)
	{
		char vidfile[1024];
		int default_chan = 1;
		char *chanid = getenv("VEEJAY_DEFAULT_CHANNEL");
		if(chanid != NULL )
			default_chan = atoi(chanid);
		else
			veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_DEFAULT_CHANNEL=channel not set (defaulting to 1)");

		snprintf(vidfile,sizeof(vidfile),"/dev/video%d", def_tags);
		int nid =	veejay_create_tag( info, VJ_TAG_TYPE_V4L, vidfile, info->nstreams, default_chan, def_tags );
		if( nid> 0)
		{
			veejay_msg(VEEJAY_MSG_INFO, "Requested capture device available as stream %d", nid );
		}
		else
		{
			return -1;
		}

		info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
		info->uc->sample_id = nid;
	
	}
	else if( info->uc->file_as_sample && id <= 0)
	{
		long i,n=el->num_video_files;
		for(i = 0; i < n; i ++ )
		{
			long start=0,end=2;
			if(vj_el_get_file_entry( info->edit_list, &start,&end, i ))
			{
				editlist *sample_el = veejay_edit_copy_to_new(	info,info->edit_list,start,end );
				if(!el)
				{
					veejay_msg(0, "Unable to start from file, Abort");
					return -1;
				}
			
				sample_info *skel = sample_skeleton_new( 0, end - start );
				if(skel)
				{
					skel->edit_list = sample_el;
          char* sample_name;
          if(!vj_get_sample_display_name(&sample_name, el->video_file_list[i])){
            veejay_msg(VEEJAY_MSG_ERROR,"Failed to create new sample filename for '%s'", el->video_file_list[i]);
          } else {
            snprintf(skel->descr, SAMPLE_MAX_DESCR_LEN, "%s", sample_name);
            free (sample_name);
          }
					sample_store(skel);
				}
			}
		}
		info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
		info->uc->sample_id = 1;
	}
	else if(info->dummy->active && id <= 0)
	{
		int dummy_id;
		/* Use dummy mode, action file could have specified something */
		if( vj_tag_size() <= 0 )
			dummy_id = vj_tag_new( VJ_TAG_TYPE_COLOR, (char*) "Solid", -1, el,info->pixel_format,-1,0,0);
		else
			dummy_id = vj_tag_highest_valid_id();
		
		if( info->uc->sample_id <= 0 )
		{
			info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
			info->uc->sample_id = dummy_id;
		}
	}

	if (!veejay_mjpeg_set_playback_rate(info, info->dummy->fps, veejay_get_norm(info->dummy->norm) ))
	{
		return -1;
	}

//	veejay_change_state( info, LAVPLAY_STATE_PLAYING );  

    info->settings->transition.ptr = shapewipe_malloc( info->video_output_width, info->video_output_height);
    if(!info->settings->transition.ptr) {
        veejay_msg(VEEJAY_MSG_ERROR,"Unable to initialize shapewipe");
        return -1;
    }

	if(veejay_open(info) != 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize the threading system");
		return -1;
	}

	return 0;
}

static double vj_measure_pause_cost_once(void)
{
    const int iterations = 10000000; // 10M
    struct timespec start, end;


    /* warmup */
    for (int i = 0; i < 10000; ++i) {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
        __asm__ volatile("yield");
#endif
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < iterations; ++i) {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
        __asm__ volatile("yield" ::: "memory");
#endif
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    long sec  = end.tv_sec  - start.tv_sec;
    long nsec = end.tv_nsec - start.tv_nsec;
    long long total_ns = (long long)sec * 1000000000LL + nsec;

    return (double)total_ns / (double)iterations;
}

double vj_calibrate_pause_cost_ns(void)
{
    double best = 1e9;

    // run multiple times and take minimum (filters scheduler noise)
    for (int i = 0; i < 3; ++i) {
        double v = vj_measure_pause_cost_once();
        if (v < best)
            best = v;
    }

    return best;
}
// read clock jitter
static long long vj_calibrate_nanosleep_overshoot(int iterations)
{
    struct timespec t1, t2;
    const long long request_ns = 100000;

    long long max_overshoot = 0;

    for (int i = 0; i < iterations; i++) {

        struct timespec req = {
            .tv_sec = 0,
            .tv_nsec = request_ns
        };

        clock_gettime(CLOCK_MONOTONIC, &t1);
        nanosleep(&req, NULL);
        clock_gettime(CLOCK_MONOTONIC, &t2);

        long long elapsed =
            (t2.tv_sec - t1.tv_sec) * 1000000000LL +
            (t2.tv_nsec - t1.tv_nsec);

        long long overshoot = elapsed - request_ns;

        if (overshoot > max_overshoot)
            max_overshoot = overshoot;
    }

    return max_overshoot;
}

static int vj_detect_preempt_rt(void) {
    FILE *fp = fopen("/sys/kernel/realtime", "r");
    if (fp) {
        int is_rt = 0;
        if (fscanf(fp, "%d", &is_rt) == 1) {
            fclose(fp);
            return is_rt;
        }
        fclose(fp);
    }
    return 0;
}

static void vj_set_realtime_priority(video_playback_setup *settings, int user_max_priority) {
    struct sched_param param;
    int policy;
    int ret;

    ret = pthread_getschedparam(pthread_self(), &policy, &param);
    if (ret != 0) {
        perror("pthread_getschedparam failed");
        return;
    }

    int max_priority = sched_get_priority_max(SCHED_FIFO);
    if (max_priority == -1) {
        perror("sched_get_priority_max failed");
        return;
    }
    
    if(max_priority > user_max_priority)
	max_priority = user_max_priority;

    param.sched_priority = max_priority;
    
    ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

	if(settings->is_rt_kernel) {
		veejay_msg(VEEJAY_MSG_INFO, " PERFORMANCE: PREEMPT_RT kernel detected!");
		veejay_msg(VEEJAY_MSG_INFO, " SCHEDULER:   Hard real-time constraints enabled.");
		veejay_msg(VEEJAY_MSG_INFO, " AUDIO:       Jitter minimized for stable playback.");
	}
	else {
		veejay_msg(VEEJAY_MSG_INFO, "[PERF] Standard kernel detected. Using safety sleep thresholds.");
	}
    
    if (ret != 0) {
        veejay_msg(VEEJAY_MSG_WARNING, "pthread_setschedparam failed. Not running as RT thread.");
    } else {
        veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] TID %lu: Priority successfully set to SCHED_FIFO with level %d", 
               (unsigned long)gettid(), max_priority);
    }
    

    veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] TID %lu: Running as high-priority task, clock overshoot set to %lld µs", (unsigned long)gettid(), settings->clock_overshoot);
}

static void vj_audio_setup_rt_thread(video_playback_setup *settings) {

	// 10k iterations to have a good change to catch a spike
	long long overshoot = vj_calibrate_nanosleep_overshoot(10000);
	
	// add 25% margin to clock jitter
	settings->clock_overshoot = (overshoot + overshoot / 4);
	settings->pause_cost_ns = vj_calibrate_pause_cost_ns();

	// clamp 30-300 us
	if (settings->clock_overshoot < 30000) 
		settings->clock_overshoot = 30000;
	if (settings->clock_overshoot > 300000) 
		settings->clock_overshoot = 300000;
	
	// note: a wrong clock overshoot (too short) can cause slips in the audio playback buffers

	// are we on pre-emptive kernel?
	settings->is_rt_kernel = vj_detect_preempt_rt();

	// schedule autio thread 1 priority lower than jack
    vj_set_realtime_priority(settings,49);
	
	if (seteuid(getuid()) < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to drop privileges to effective user: %s", strerror(errno));
		return;
	}


}

static void veejay_producer_initialize_playmode(veejay_t *info) {
	video_playback_setup *settings = info->settings;

    if(info->load_action_file)
    {
        if(veejay_load_action_file(info, info->action_file[0] ))
        {
            veejay_msg(VEEJAY_MSG_INFO, "Loaded configuration file %s", info->action_file[0] );
        } else {
            veejay_msg(VEEJAY_MSG_WARNING, "File %s is not an action file", info->action_file[0]);
        }
    }

    if(info->load_sample_file ) {
        if(sample_readFromFile( info->action_file[1],info->composite,info->seq,info->font,info->edit_list,
             &(info->uc->sample_id), &(info->uc->playback_mode)  ))
        {
            veejay_msg(VEEJAY_MSG_INFO, "Loaded samplelist %s", info->action_file[1]);
        } else {
            veejay_msg(VEEJAY_MSG_WARNING, "File %s is not a sample file", info->action_file[1]);
        }
    }

	switch(info->uc->playback_mode)
    {
        case VJ_PLAYBACK_MODE_PLAIN:
            info->current_edit_list = info->edit_list;

			atomic_store_long_long(&settings->min_frame_num, 0);
			atomic_store_long_long(&settings->max_frame_num, info->edit_list->total_frames);
			veejay_msg(VEEJAY_MSG_INFO, "Playing plain video, frames %lld - %lld", settings->min_frame_num, settings->max_frame_num );
            settings->current_playback_speed = 1;
        break;

        case VJ_PLAYBACK_MODE_TAG:
            veejay_start_playing_stream(info,info->uc->sample_id);    
            veejay_msg(VEEJAY_MSG_INFO, "Playing stream %d", info->uc->sample_id);
        break;

        case VJ_PLAYBACK_MODE_PATTERN:
            info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;

        case VJ_PLAYBACK_MODE_SAMPLE:
            veejay_start_playing_sample(info, info->uc->sample_id);
            veejay_msg(VEEJAY_MSG_INFO, "Playing sample %d", info->uc->sample_id);
        break;
    }
}  

#ifdef HAVE_JACK
static inline void vj_audio_wait_for_jack_space(
    int jack_frames_needed,
    video_playback_setup *settings
) {

    long min_busy_us = settings->clock_overshoot;

    while (vj_jack_get_ringbuffer_frames_free() < jack_frames_needed) {

        usleep_accurate(1500, settings);

        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) return;
    }

    struct timespec start, current;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (vj_jack_get_ringbuffer_frames_free() < jack_frames_needed) {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
        __asm__ volatile("yield");
#endif
        clock_gettime(CLOCK_MONOTONIC, &current);

        long elapsed_us = (current.tv_sec - start.tv_sec) * 1000000L +
                          (current.tv_nsec - start.tv_nsec) / 1000L;

        if (elapsed_us >= min_busy_us) break;

        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) break;
    }
}
 
#endif

void *veejay_audio_producer_thread(void *arg)
{
    veejay_t *info = (veejay_t *)arg;
    video_playback_setup *settings = info->settings;
    editlist *el = info->current_edit_list;

	vj_audio_setup_rt_thread(settings);

	int has_audio = (info->current_edit_list->has_audio &&
                     vj_perform_init_audio(info,0) &&
					 vj_perform_init_audio(info, 1) &&
                     info->audio == AUDIO_PLAY); 
#ifndef HAVE_JACK
	has_audio = 0;
#endif

#ifdef HAVE_JACK
    const long CLIENT_RATE = vj_jack_get_client_samplerate();
    const long JACK_RATE   = vj_jack_get_rate();
    const double half_frame_s = 0.5 / (double) el->video_fps;
#else
    const long CLIENT_RATE = el->audio_rate;
#endif

    const double SPVF      = settings->spvf;
    double anchor_s = 0;
    unsigned long long loop_count = 0; // 19.5 billion years playing 29fps/44.1Khz
    const int BPS = el->audio_bps;
    const int MAX_CLIENT_FRAMES = (int)(SPVF * CLIENT_RATE * 2 + 1024);

    uint8_t *audio_chunk = NULL;
    uint8_t *silenced    = NULL;

	if( has_audio ) {
		size_t audio_buf_len = MAX_CLIENT_FRAMES * BPS * sizeof(uint8_t);
		audio_chunk = (uint8_t*) vj_calloc(audio_buf_len);
    	if(!audio_chunk) {
			goto AUDIO_PRODUCER_THREAD_EXIT;
		}
		silenced    = (uint8_t*) vj_calloc(audio_buf_len);
		if(!silenced) {
			free(audio_chunk);
			goto AUDIO_PRODUCER_THREAD_EXIT;
		}
		mlock(audio_chunk,audio_buf_len);
		mlock(silenced,audio_buf_len);
	}

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    atomic_store_int(&info->audio_running, 1);
    atomic_store_long_long(&settings->current_frame_num, -1);

#ifdef HAVE_JACK
    if (has_audio) {
		const int seed_frames = el->video_fps * 2;
		const long seed_client_frames = (long)(CLIENT_RATE * seed_frames / el->video_fps);
        long seeded = 0;

        /*veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO] Seeding Jack ringbuffers with %ld client frames (~%d video frames)", seed_client_frames, seed_frames);

        while (seeded < seed_client_frames && atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
            long frames_free = vj_jack_get_ringbuffer_frames_free();
            if (frames_free <= 0) { usleep_accurate(100,settings); continue; }

            long frames_free_client = frames_free * CLIENT_RATE / JACK_RATE;
            if (frames_free_client <= 0) frames_free_client = 1;

            int chunk = (seed_client_frames - seeded > frames_free_client)
                        ? frames_free_client
                        : (seed_client_frames - seeded);

            int written = vj_perform_play_audio(settings, silenced, chunk * BPS, silenced);
            seeded += written;
        }

		veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO] Done seeding JACK buffers");*/

        unsigned long start_hw = vj_jack_get_played_frames();
        unsigned long frames_to_wait = (unsigned long)seed_client_frames * JACK_RATE / CLIENT_RATE;
        
        while ((vj_jack_get_played_frames() - start_hw) < frames_to_wait) {
            if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) break;
            usleep_accurate(100, settings);
        }

        anchor_s = (double) vj_jack_get_played_frames() / JACK_RATE;

        atomic_store_double(&settings->audio_master_s, anchor_s);
        atomic_store_double(&settings->audio_start_offset, anchor_s);

        veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] Audio anchor established at %fs", anchor_s);
        atomic_store_long_long(&settings->current_frame_num, 0);
    } else {
#endif    
        // fallback anchor for monotonic clock
		anchor_s = monotonic_now_s();
        atomic_store_double(&settings->audio_start_offset, anchor_s);
        atomic_store_double(&settings->audio_master_s, anchor_s);
        atomic_store_long_long(&settings->current_frame_num, 0);

        veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] Monotonic clock anchor established at %fs", anchor_s);

#ifdef HAVE_JACK
    }
#endif

	if (loop_count == 0) {
		atomic_store_int(&settings->first_audio_frame_ready, 1);
		veejay_msg(VEEJAY_MSG_INFO, has_audio
			? "[AUDIO] First audio frame queued, starting video"
			: "[AUDIO] Monotonic clock initialized, starting video");
	}

    while (atomic_load_int(&info->audio_running) &&
           atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP)
    {

#ifdef HAVE_JACK
        if (has_audio) {
            int needed = (int)(SPVF * CLIENT_RATE + 0.5);
            long long media_frame = atomic_load_long_long(&settings->current_frame_num); 
			int decoded;
			if(vj_jack_xrun_flag()) {
		
				atomic_add_fetch_old_int(&settings->xruns, 1);
				veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] JACK xrun detected!");
				
				//resync
				double played_hw = (double) vj_jack_get_played_frames() / JACK_RATE;
				double master = atomic_load_double(&settings->audio_master_s);

				double delta = played_hw - master;
				if (delta > half_frame_s) {
					atomic_store_double(&settings->audio_master_s, played_hw);
					veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO] Audio master clock resynced after xrun by %.6fs (~1 video frame)", delta);
				}
			}

			int tx_active = atomic_load_int(&settings->transition.active) && atomic_load_int(&settings->transition.global_state);
			if(!tx_active) {
				decoded = vj_perform_queue_audio_chunk_ext(info, needed, media_frame, 0, audio_chunk);
				
				if (decoded <= 0) { 
					long sleep_us = 100;
					while (decoded <= 0) {
						usleep_accurate(sleep_us, settings);
						decoded = vj_perform_queue_audio_chunk_ext(info, needed, media_frame, 0, audio_chunk);
						sleep_us = sleep_us < 2000 ? sleep_us * 2 : 2000; // max 2ms
					}
				}
			}
			else {
				long long b_frame = vj_calc_next_subframe(info, settings->transition.next_id);
				long long start = atomic_load_long_long(&settings->transition.start);
				long long end = atomic_load_long_long(&settings->transition.end);
				decoded = vj_perform_queue_audio_chunk_crossfade(info, needed, media_frame, b_frame, audio_chunk, settings->transition.next_id,start,end);
			}

            const int jack_frames_needed = (int)((double)decoded * JACK_RATE / CLIENT_RATE + 1);

         	vj_audio_wait_for_jack_space( jack_frames_needed, settings);

            const int frames_written = vj_perform_play_audio(settings, audio_chunk, decoded * BPS, silenced);
          
            double played_hw = (double) vj_jack_get_played_frames() / JACK_RATE;
            double predicted = played_hw + (double)frames_written / JACK_RATE;
            atomic_store_double(&settings->audio_master_s, predicted);

            vj_perform_inc_frame(info, settings->current_playback_speed);		

			loop_count++;
            double next_frame_target = anchor_s + (loop_count * SPVF);
            double now = monotonic_now_s();
            double sleep_s = next_frame_target - now;

            if (sleep_s > 0.001) { 
                usleep_accurate((long long)(sleep_s * 1e6 * 0.9), settings);
            }
        } else {
#endif
			const double next_frame_target = anchor_s + ((double)(loop_count + 1) * SPVF);
				
			double now = monotonic_now_s();
			double remaining_s = next_frame_target - now;

			if (remaining_s > 0.0) {
				usleep_hybrid((long long)(remaining_s * 1e6), settings);
			}

			now = monotonic_now_s();
			
			atomic_store_double(&settings->audio_master_s, now );
			vj_perform_inc_frame(info, settings->current_playback_speed);
			loop_count++;

		}
#ifdef HAVE_JACK
	}
#endif

	atomic_store_int(&info->audio_running, 0);

    free(audio_chunk);
    free(silenced);
AUDIO_PRODUCER_THREAD_EXIT:
    pthread_exit(NULL);
    return NULL;
}


static void veejay_producer_thread_audio_startup(veejay_t *info)
{
    video_playback_setup *settings = info->settings;

	int has_audio = (info->audio == AUDIO_PLAY);
#ifndef HAVE_JACK
	has_audio = 0;
#endif

    if (has_audio) {
#ifdef HAVE_JACK	    
        vj_perform_audio_start(info);
	vj_jack_enable();
#endif
    } else {
        info->audio = NO_AUDIO;
        veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Audio playback not enabled; using monotonic fallback");
    }

	veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] Starting Audio Producer (Master Clock)");
    int ret = pthread_create(&(settings->audio_playback_thread), NULL,
                             veejay_audio_producer_thread, info);
    if (ret != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "[AUDIO] Failed to start Audio Producer thread.");
        if (has_audio) {
            vj_perform_audio_stop(info);
            info->audio = NO_AUDIO;
        }
        return;
    }

    while (!atomic_load_int(&info->audio_running))
        usleep_accurate(200,settings);

}

static void *veejay_producer_thread_loop(void *ptr)
{
    veejay_t *info = (veejay_t*) ptr;
    video_playback_setup *settings = info->settings;
    const double SPVF = settings->spvf;

    const double SKIP_TOLERANCE = 1.1 * SPVF + 0.002; // ~1.1 frames + 2 ms margin

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    while (atomic_load_int(&settings->warmup_active)) { //FIXME waits for warmup to complete
        usleep_accurate(100, settings);
    }

    atomic_store_long_long(&settings->current_frame_num, -1);
    atomic_store_int(&settings->audio_mode, AUDIO_MODE_SILENCE_FILL);

	if(info->audio) {
#ifdef HAVE_JACK
    	veejay_msg(VEEJAY_MSG_DEBUG, "[PRODUCER] waiting for audio anchor");
    	while (atomic_load_double(&settings->audio_start_offset) <= 0.0) {
        	usleep_accurate(100, settings);
    	}
#endif
	}

    veejay_msg(VEEJAY_MSG_DEBUG, "[PRODUCER] waiting for first audio frame ready");
    while (atomic_load_int(&settings->first_audio_frame_ready) == 0) {
        usleep_accurate(100, settings);
    }

    veejay_msg(VEEJAY_MSG_DEBUG, "[PRODUCER] Wait for playback to reach anchor");

	veejay_producer_initialize_playmode(info);
    veejay_set_speed(info, 1, 0);

    atomic_store_int(&settings->audio_mode, AUDIO_MODE_CONTENT);
    atomic_store_long_long(&settings->master_frame_num, 0);

	while (atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
        info->stats.skipped_frames = 0;

        long long frame = atomic_load_long_long(&settings->master_frame_num);
        double audio_now = atomic_load_double(&settings->audio_master_s);
        double audio_anchor = atomic_load_double(&settings->audio_start_offset);
        double elapsed_audio = audio_now - audio_anchor;
        if (elapsed_audio < 0) elapsed_audio = 0;

        double pts = frame * SPVF;
        
        double diff = pts - elapsed_audio;

        if (diff < -SKIP_TOLERANCE) {
            long long frames_to_skip = (long long)((-diff / SPVF));
            
            if (frames_to_skip > 0) {
                 if( frame > 0 ) {
                    veejay_msg(VEEJAY_MSG_WARNING,
                        "[PRODUCER] Performance lag: Dropping %lld frames to maintain A/V sync", frames_to_skip);
                 }
                 atomic_add_fetch_old_long_long(&settings->master_frame_num, frames_to_skip);
                 info->stats.skipped_frames = frames_to_skip;
                 info->stats.total_frames_skipped += frames_to_skip;
                 frame += frames_to_skip;
                 
                 pts = frame * SPVF;
                 diff = pts - elapsed_audio;
            }
        }

        if (diff > 0) {
            double sleep_s = (diff > SPVF) ? SPVF : diff;
            
            if (sleep_s > 0.0001) {
                usleep_accurate((long long)(sleep_s * 1e6), settings);
                
                audio_now = atomic_load_double(&settings->audio_master_s);
                elapsed_audio = audio_now - audio_anchor;
                diff = pts - elapsed_audio;
            }
        }

        VJFrame *vf = veejay_video_queue_reserve_buffer(info);
        if (!vf) {
            if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) break;
            usleep_accurate(1000, settings); // yield
            continue;
        }

        vf->frame_num = frame;

		double t_before = monotonic_now_s();
        vj_perform_queue_video_frame(info, vf);
        
		double t_after = monotonic_now_s();
		info->stats.render_duration = (t_after - t_before);
		
        veejay_video_queue_post_frame(info, vf);
		long long master_frame = atomic_add_fetch_old_long_long(&settings->master_frame_num, 1);

        info->stats.current_frame = atomic_load_long_long(&settings->current_frame_num);
        info->stats.total_frames_produced = master_frame;
        info->stats.last_pts_s = pts;
        info->stats.delta_s = diff;
		info->stats.xruns = atomic_load_int(&settings->xruns);

		veejay_consume_events(info);
    }

    pthread_exit(NULL);
    return NULL;
}

static void veejay_playback_close(veejay_t *info)
{
    int i;
    
    if(info->uc->is_server) {
		for(i = 0; i < 4; i ++ )
			if(info->vjs[i]) vj_server_shutdown(info->vjs[i]); 
    }

    if(info->osc) vj_osc_free(info->osc);

	if( info->shm ) {
		vj_shm_stop(info->shm);
		vj_shm_free(info->shm);
	}
	if( info->splitter ) {
		vj_split_free(info->splitter);
	}

    if( info->y4m ) {
	    vj_yuv_stream_stop_write( info->y4m );
	    vj_yuv4mpeg_free(info->y4m );
	    info->y4m = NULL;
	}

	if( info->vloopback ) {
		vj_vloopback_close( info->vloopback );
		info->vloopback = NULL;

	}
	
#ifdef HAVE_SDL
	if( info->sdl ) {
		vj_sdl_enable_screensaver(); 
        vj_sdl_free(info->sdl);
    }
    vj_sdl_quit();
#endif
#ifdef HAVE_DIRECTFB
    if( info->dfb ) {
		vj_dfb_free(info->dfb);
		free(info->dfb);
	}
#endif
#ifdef HAVE_FREETYPE
	//vj_font_destroy( info->font );
	vj_font_destroy( info->osd );
#endif
    vj_perform_free(info);

}

int vj_server_setup(veejay_t * info)
{
	if (info->uc->port == 0)
		info->uc->port = VJ_PORT;

	size_t recv_len   = (info->edit_list->video_width * info->edit_list->video_height * 3); // max
	size_t status_len = 4096 * 16; //@ 16x 4kb 

	info->vjs[VEEJAY_PORT_CMD] = vj_server_alloc(info->uc->port, NULL, V_CMD, recv_len);
	if(!info->vjs[VEEJAY_PORT_CMD]) {
		return 0;
	}

	info->vjs[VEEJAY_PORT_STA] = vj_server_alloc(info->uc->port, NULL, V_STATUS, status_len);
	if(!info->vjs[VEEJAY_PORT_STA])
	{
		vj_server_shutdown(info->vjs[VEEJAY_PORT_CMD]);
		return 0;
	}
	//@ second VIMS control port
	info->vjs[VEEJAY_PORT_DAT] = vj_server_alloc(info->uc->port + 5, NULL, V_CMD, recv_len);
	if(!info->vjs[VEEJAY_PORT_DAT]) {
		vj_server_shutdown(info->vjs[VEEJAY_PORT_CMD]);
		vj_server_shutdown(info->vjs[VEEJAY_PORT_STA]);
		return 0;
	}

	info->vjs[VEEJAY_PORT_MAT] = NULL;
	if( info->settings->use_vims_mcast ) 
	{
		info->vjs[VEEJAY_PORT_MAT] =
			vj_server_alloc(info->uc->port, info->settings->vims_group_name, V_CMD, recv_len );
		if(!info->vjs[VEEJAY_PORT_MAT])
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize multicast sender");
			return 0;
		}
	}
	if(info->settings->use_mcast)
	{
		GoMultiCast( info->settings->group_name );
	}

	info->osc = (void*) vj_osc_allocate(info->uc->port+6);

	if(!info->osc) 
	{
		veejay_msg(VEEJAY_MSG_ERROR,  "Unable to start OSC server at port %d", info->uc->port + 6 );
		vj_server_shutdown(info->vjs[VEEJAY_PORT_CMD]);
		vj_server_shutdown(info->vjs[VEEJAY_PORT_STA]);
		vj_server_shutdown(info->vjs[VEEJAY_PORT_DAT]);
		return 0;
	}

	if( info->settings->use_mcast )
		veejay_msg(VEEJAY_MSG_INFO, "UDP multicast OSC channel ready at port %d (group '%s')",
			info->uc->port + 6, info->settings->group_name );
	else
		veejay_msg(VEEJAY_MSG_INFO, "UDP unicast OSC channel ready at port %d",
			info->uc->port + 6 );

	if(vj_osc_setup_addr_space(info->osc) == 0)
		veejay_msg(VEEJAY_MSG_DEBUG, "Initialized OSC (http://www.cnmat.berkeley.edu/OpenSoundControl/)");

   	info->uc->is_server = 1;

	return 1;
}

int	prepare_cache_line(int perc, int n_slots)
{
	int total = 0; 
	char line[128];
	FILE *file = fopen( "/proc/meminfo","r");
	if(!file)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant open proc, memory size cannot be determined");
		veejay_msg(VEEJAY_MSG_ERROR, "Cache disabled");
		return 1;
	}

	if(fgets(line, 128, file ) == NULL ) {
		fclose(file);
		return 1;
	}

	sscanf( line, "%*s %i", &total );
	fclose(file);

	double p = (double) perc * 0.01f;
	long max_memory = (p * total);
	long mmap_memory = (long) (0.005f * (float) total);

	char *user_defined_mmap = getenv( "VEEJAY_MMAP_PER_FILE" );
	if( user_defined_mmap ) {
		max_memory = atoi( user_defined_mmap ) * 1024;
		veejay_msg(VEEJAY_MSG_DEBUG, "User-defined %2.2f Kb mmap size per AVI file",(float) (max_memory / 1024.0f));
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "You can define mmap size per AVI file with VEEJAY_MMAP_PER_FILE=Kb");
	}

	max_memory -= mmap_memory;

	if( perc > 0 && max_memory <= 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Please enter a larger value for -m");
		return 1;
	}

	if( n_slots <= 0)
		n_slots = 1;

	return 1;
}

static int smp_check(void)
{
	return get_nprocs();
}

veejay_t *veejay_malloc()
{
    
    veejay_t *info = (veejay_t *) vj_calloc(sizeof(veejay_t));
    if (!info)
		return NULL;

    info->settings = (video_playback_setup *) vj_calloc(sizeof(video_playback_setup));
    if (!(info->settings)) 
		return NULL;
	info->settings->fxdepth = 1; //@ default to on (VEEJAY_CLASSIC env turns it off)
	
	veejay_memset( &(info->settings->action_scheduler), 0, sizeof(vj_schedule_t));
    veejay_memset( &(info->settings->viewport ), 0, sizeof(VJRectangle)); 

    info->status_what = (char*) vj_calloc(sizeof(char) * MESSAGE_SIZE );

	info->uc = (user_control *) vj_calloc(sizeof(user_control));
	info->uc->drawsize = 4;

	info->global_chain = (global_chain_t*) vj_calloc(sizeof(global_chain_t));
	if(!info->global_chain)
		return NULL;

	for( int i = 0; i < SAMPLE_MAX_EFFECTS ; i ++ ) {
		info->global_chain->fx_chain[i] = (sample_eff_chain*) vj_calloc(sizeof(sample_eff_chain));
		if(!info->global_chain->fx_chain[i])
			return NULL;
	}

	if (!(info->uc)) 
		return NULL;

    info->effect_frame_info = (VJFrameInfo*) vj_calloc(sizeof(VJFrameInfo));
	if(!info->effect_frame_info)
		return NULL;
    info->effect_frame_info2 = (VJFrameInfo*) vj_calloc(sizeof(VJFrameInfo));
	if(!info->effect_frame_info2)
		return NULL;

    info->effect_info = (vjp_kf*) vj_calloc(sizeof(vjp_kf));
	if(!info->effect_info) 
		return NULL;   
    
    info->effect_info2 = (vjp_kf*) vj_calloc(sizeof(vjp_kf));
	if(!info->effect_info2) 
		return NULL;   

	info->dummy = (dummy_t*) vj_calloc(sizeof(dummy_t));
    if(!info->dummy)
		return NULL;
	
	info->seq = (sequencer_t*) vj_calloc(sizeof( sequencer_t) );
    info->audio = AUDIO_PLAY;
    info->continuous = 1;
    info->sync_correction = 1;
    info->sync_ins_frames = 1;
    info->sync_skip_frames = 0;
    info->double_factor = 1;
    info->bezerk = 0;
    info->nstreams = 1;
    info->stream_outformat = -1;
    info->rlinks = (int*) vj_malloc(sizeof(int) * VJ_MAX_CONNECTIONS );
    info->rmodes = (int*) vj_malloc(sizeof(int) * VJ_MAX_CONNECTIONS );
	info->splitted_screens = (int*) vj_malloc( sizeof(int) * VJ_MAX_CONNECTIONS );
   	info->settings->currently_processed_entry = -1;
    info->settings->first_frame = 1;
    info->settings->state = LAVPLAY_STATE_PLAYING;
    info->settings->composite = 1;
    info->uc->playback_mode = VJ_PLAYBACK_MODE_PLAIN;
    info->uc->use_timer = 2;
    info->uc->sample_key = 1;
    info->uc->direction = 1;	/* pause */
    info->uc->sample_start = 0;
    info->uc->sample_end = 0;
	info->uc->ram_chain = 1; /* enable, keep FX chain buffers in memory (reduces the number of malloc/free of frame buffers) */
	info->net = 1;
	info->status_line = (char*) vj_calloc(sizeof(char) * MESSAGE_SIZE );
	info->status_line_len = 0;
    for( int i =0; i < VJ_MAX_CONNECTIONS ; i ++ ) {
		info->rlinks[i] = -1;
		info->rmodes[i] = -1;
		info->splitted_screens[i] = -1;
	}

    veejay_memset(info->action_file[0],0,sizeof(info->action_file[0])); 
    veejay_memset(info->action_file[1],0,sizeof(info->action_file[1])); 
	veejay_memset( info->dummy, 0, sizeof(dummy_t));
	veejay_memset(&(info->settings->sws_templ), 0, sizeof(sws_template));

#ifdef HAVE_SDL
    info->video_out = 0;
#else
#ifdef HAVE_DIRECTFB
    info->video_out = 1;
#else
    info->video_out = 3;
#endif
#endif

	info->pixel_format = FMT_422F; //@default 
	info->settings->ncpu = smp_check();

    omp_set_num_threads( info->settings->ncpu );

	int status = 0;
	int acj    = 0;
	char *interpolate_chroma = getenv("VEEJAY_INTERPOLATE_CHROMA");
	if( interpolate_chroma ) {
		sscanf( interpolate_chroma, "%d", &status );
		}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_INTERPOLATE_CHROMA=[0|1] not set");
	}

	char *auto_ccir_jpeg = getenv("VEEJAY_AUTO_SCALE_PIXELS");
	if( auto_ccir_jpeg ) {
		sscanf( auto_ccir_jpeg, "%d", &acj );
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_AUTO_SCALE_PIXELS=[0|1] not set");
	}

	char *key_repeat_interval = getenv("VEEJAY_SDL_KEY_REPEAT_INTERVAL");
	char *key_repeat_delay    = getenv("VEEJAY_SDL_KEY_REPEAT_DELAY");
	if(key_repeat_interval) {
		sscanf( key_repeat_interval, "%d", &(info->settings->repeat_interval));
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_SDL_KEY_REPEAT_INTERVAL=[Num] not set");
	}
	if( key_repeat_delay) {
		sscanf( key_repeat_delay, "%d", &(info->settings->repeat_delay));
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_SDL_KEY_REPEAT_DELAY=[Num] not set");
	}

	char *best_performance = getenv( "VEEJAY_PERFORMANCE");
	int default_zoomer = 1;
	if(!best_performance) {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_PERFORMANCE=[fastest|quality] not set");
	}

	char *sdlfs = getenv("VEEJAY_FULLSCREEN");
	if( sdlfs ) {
		int val = 0;
		if( sscanf( sdlfs, "%d", &val ) ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Playing in %s mode",
				(val== 1 ? "fullscreen" : "windowed" ) );
			info->settings->full_screen = val;
		}
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_FULLSCREEN=[0|1] not set");
	}


	char *audiostats = getenv("VEEJAY_AUDIO_STATS");
	if(audiostats) {
		int val = 0;
		if( sscanf( audiostats, "%d", &val ) ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Outputing audio synchronization statistics is %s", (val == 0 ? "disabled" : "enabled" ));
			info->settings->audiostats = val;
		}
	}

	if( best_performance) {
		if (strncasecmp( best_performance, "quality", 7 ) == 0 ) {
			default_zoomer = 2;
			status = 1;
			veejay_msg(VEEJAY_MSG_WARNING, "Performance set to maximum quality");
		}
		else if( strncasecmp( best_performance, "fastest", 7) == 0 ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Performance set to maximum speed");
			if( acj ) {
				veejay_msg(VEEJAY_MSG_WARNING, "\tdisabling flag VEEJAY_AUTO_SCALE_PIXELS");
				acj = 0;
			}
			if( status ) {
				veejay_msg(VEEJAY_MSG_WARNING, "\tdisabling flag VEEJAY_INTERPOLATE_CHROMA");
				status = 0;
			}
			default_zoomer = 1;
		}
	}

	yuv_init_lib( status ,acj, default_zoomer);

	if(!vj_avcodec_init( info->pixel_format, info->verbose))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize encoders");
		return 0;
	}

    vj_osc_set_veejay_t(info);
    vj_tag_set_veejay_t(info);

	veejay_instance_ = info;
	
    return info;
}

int veejay_main(veejay_t *info)
{
    video_playback_setup *settings = (video_playback_setup *)info->settings;
    pthread_attr_t attr;
    cpu_set_t cpuset;
    int err;

    veejay_memset(&attr, 0, sizeof(attr));

    if (vj_task_get_num_cpus() > 1) {
        CPU_ZERO(&cpuset);
        CPU_SET(1, &cpuset);

        err = pthread_attr_init(&attr);
        if (err == ENOMEM) {
            veejay_msg(VEEJAY_MSG_ERROR, "Out of memory initializing thread attributes.");
            return 0;
        }

        if (pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset) != 0) {
            veejay_msg(VEEJAY_MSG_WARNING, "[DISPLAY] Unable to pin display/renderer thread to CPU #1.");
        } else {
            veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Thread affinity set to CPU #1.");
        }
    }

    veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Starting Video Renderer Thread (Display)");
    err = pthread_create(&settings->renderer_thread, &attr,
                         veejay_display_renderer_thread, (void *)info);
    if (err != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to create Renderer thread, code %d", err);
        pthread_attr_destroy(&attr);
        return 0;
    }

    pthread_attr_destroy(&attr);
    veejay_msg(VEEJAY_MSG_DEBUG, "[PRODUCER] Starting Video Producer Thread (Decode/Effects)");
    if (pthread_create(&settings->producer_thread, NULL,
                       veejay_producer_thread_loop, (void *)info) != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to create Producer thread");
        veejay_change_state(info, LAVPLAY_STATE_STOP);
        pthread_join(settings->renderer_thread, NULL);
        return 0;
    }


    veejay_producer_thread_audio_startup(info);



    while (atomic_load_int(&settings->first_audio_frame_ready) == 0) {
        usleep_accurate(5000, settings);
    }

    Welcome(info);

    return 1;
}

static void	veejay_reset_el_buffer( veejay_t *info )
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    if (settings->save_list)
	free(settings->save_list);

    settings->save_list = NULL;
    settings->save_list_len = 0;
}

int veejay_edit_copy(veejay_t * info, editlist *el, long start, long end)
{
    if(el->is_empty)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No frames in EDL to copy");
		return 0;
	}

    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    uint64_t k, i;
    uint64_t n1 = (uint64_t) start;
    uint64_t n2 = (uint64_t) end;
    if (settings->save_list)
		free(settings->save_list);

    settings->save_list =
		(uint64_t *) vj_calloc((n2 - n1 + 1) * sizeof(uint64_t));

	if (!settings->save_list)
	{
		veejay_change_state_save(info, LAVPLAY_STATE_STOP);
		return 0;
	}

    k = 0;

#pragma omp simd
    for (i = n1; i <= n2; i++) {
	settings->save_list[k] = el->frame_list[i];
        k++;
    }
    settings->save_list_len = (n2 - n1 + 1);

    veejay_msg(VEEJAY_MSG_DEBUG, "Copied frames %d - %d to buffer (of size %d)",n1,n2,k );

    return 1;
}
editlist *veejay_edit_copy_to_new(veejay_t * info, editlist *el, long start, long end)
{
	long len = end - start;
  
	if(el->is_empty)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No frames in EDL to copy");
		return 0;
	}

	if( end > el->total_frames)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample end is outside of editlist");
		return NULL;
	}

    if( start < 0 ) {
        veejay_msg(VEEJAY_MSG_ERROR, "Sample start cannot be a negative value");
        return NULL;
    }

	if(len < 1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample too short");
		return NULL;
	}

    veejay_msg(VEEJAY_MSG_DEBUG, "New EDL %ld - %ld (%ld frames)", start,end, len);
	/* Copy edl */
	editlist *new_el = vj_el_soft_clone_range( el,start,end );
	if(!new_el)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot soft clone EDL");
		return NULL;
	}

	return new_el;
}

int veejay_edit_delete(veejay_t *info, editlist *el, long start, long end)
{
    if (el->is_empty) {
        veejay_msg(VEEJAY_MSG_ERROR, "Nothing in EDL to delete");
        return 0;
    }

    video_playback_setup *settings = (video_playback_setup *)info->settings;

    uint64_t i;
    uint64_t n1 = (uint64_t)start;
    uint64_t n2 = (uint64_t)end;

    if (info->dummy->active) {
        veejay_msg(VEEJAY_MSG_ERROR, "Playing dummy video");
        return 0;
    }

    if (n2 < n1 || n1 > el->total_frames || n2 > el->total_frames) {
        veejay_msg(VEEJAY_MSG_ERROR, "Incorrect parameters for deleting frames");
        return 0;
    }

    for (i = n2 + 1; i < el->video_frames; i++)
        el->frame_list[i - (n2 - n1 + 1)] = el->frame_list[i];

	long long min_fn = atomic_load_long_long(&settings->min_frame_num);
	long long max_fn = atomic_load_long_long(&settings->max_frame_num);

    if (n1 - 1 < min_fn) {
        if (n2 < min_fn)
            min_fn -= (n2 - n1 + 1);
        else
            min_fn = n1;
    }

    if (n1 - 1 < max_fn) {
        if (n2 <= max_fn)
            max_fn -= (n2 - n1 + 1);
        else
            max_fn = n1 - 1;
    }

	atomic_store_long_long(&settings->min_frame_num, min_fn);
	atomic_store_long_long(&settings->max_frame_num, max_fn);

    long long cur = atomic_load_long_long(&settings->current_frame_num);
    if (n1 <= cur) {
        if (cur <= n2) {
            atomic_store_long_long(&settings->current_frame_num, n1);
        } else {
            atomic_store_long_long(&settings->current_frame_num, cur - (n2 - n1 + 1));
        }
    }

    el->video_frames -= (n2 - n1 + 1);
    el->total_frames -= (n2 - n1 + 1);

    return 1;
}

int veejay_edit_cut(veejay_t * info, editlist *el, long start, long end)
{
	if( el->is_empty )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Nothing to cut in EDL");
		return 0;
	}
    if (!veejay_edit_copy(info, el,start, end))
	return 0;
    if (!veejay_edit_delete(info, el,start, end))
	return 0;

    return 1;
}

int veejay_edit_paste(veejay_t * info, editlist *el, long destination)
{
	video_playback_setup *settings =
		(video_playback_setup *) info->settings;
	uint64_t i, k;

	if (!settings->save_list_len || !settings->save_list)
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
			    "No frames in the buffer to paste");
		return 0;
	 }

	if(el->is_empty)
	{
		destination = 0;
	}
	else
	{
		if (destination < 0 || destination > el->total_frames)
		{
			if(destination < 0)
				veejay_msg(VEEJAY_MSG_ERROR, 
					    "Destination cannot be negative");
			if(destination > el->total_frames)
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot paste beyond Edit List");
			return 0;
    		}
	}

    el->frame_list = (uint64_t*)realloc(el->frame_list,
	    ((el->is_empty ? 0 :el->video_frames) +
		settings->save_list_len) *
		sizeof(uint64_t));

	if (!el->frame_list)
	{
		veejay_change_state_save(info, LAVPLAY_STATE_STOP);
		return 0;
   	}

   	k = (uint64_t)settings->save_list_len;
    for (i = el->total_frames; i >= destination && i > 0; i--)
		el->frame_list[i + k] = el->frame_list[i];
    
    k = destination;
	for (i = 0; i < settings->save_list_len; i++)
	{
		el->frame_list[k] = settings->save_list[i];
		k++;
	}

    if( destination < settings->min_frame_num ) {
        settings->min_frame_num = destination;
    }

	el->video_frames += settings->save_list_len;

	el->total_frames += settings->save_list_len;

	long long max_fn = atomic_load_long_long(&settings->max_frame_num);
    if( el->total_frames < max_fn ) {
        atomic_store_long_long(&settings->max_frame_num, (long long) el->total_frames);
    }

    atomic_store_long_long(&settings->current_frame_num, destination);

	if(el->is_empty)
		el->is_empty = 0;

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Pasted %lld frames from buffer into position %ld in movie",
			settings->save_list_len, destination );
	return 1;
}

int veejay_edit_move(veejay_t * info,editlist *el, long start, long end,
		      long destination)
{
    long dest_real;
    if( el->is_empty )
		return 0;
	
    if (destination > el->total_frames || destination < 0
		|| start < 0 || end < 0 || start >= el->total_frames
		|| end > el->total_frames || end < start)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid parameters for moving video from %ld - %ld to position %ld",
			start,end,destination);
		veejay_msg(VEEJAY_MSG_ERROR, "Range is 0 - %ld", el->total_frames);   
		return 0;
    }

    if (destination < start)
		dest_real = destination;
    else if (destination > end)
		dest_real = destination - (end - start + 1);
    else
		dest_real = start;

    if (!veejay_edit_cut(info, el, start, end))
		return 0;

    if (!veejay_edit_paste(info, el,dest_real))
		return 0;


	return 1;
}

int veejay_edit_addmovie_sample(veejay_t * info, char *movie, int id )
{
	char *files[1];

	files[0] = strdup(movie);
	sample_info *sample = NULL;
	editlist *sample_edl = NULL;
	// if sample exists, get it for update
	if(sample_exists(id) )
		sample = sample_get(id);

	// if sample exists, it could have a edit list */
	if( sample )
	{
		if( !sample_usable_edl( id ) )
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d is a picture", id );
			if(files[0])
				free(files[0]);
			return -1;
		}

		sample_edl = sample_get_editlist( id );
	}

	// if both, append it to sample's edit list 
	if(sample_edl && sample)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Adding video file to existing sample %d", id );
		long endpos = sample_get_endFrame( id );
		
		int res = veejay_edit_addmovie( info, sample_edl, movie, endpos );

		if(files[0]) 
			free(files[0]);

		if( res > 0 ) {
			sample_set_endframe( id, sample_edl->total_frames );
			veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d new ending position is %ld",id, sample_edl->video_frames );
			return id;
		}
		return -1;
	}

	// create initial edit list for sample (is currently playing)
	if(!sample_edl) 
		sample_edl = vj_el_init_with_args( files,1,info->preserve_pathnames,info->auto_deinterlace,0,
				info->edit_list->video_norm , info->pixel_format, info->video_output_width, info->video_output_height);
	// if that fails, bye
	if(!sample_edl)
	{
		veejay_msg(0, "Error while creating EDL");
		if(files[0]) free(files[0]);

		return -1;
	}

	// check audio properties 
	if( info->edit_list->has_audio && info->audio == AUDIO_PLAY ) {
		if( sample_edl->audio_rate != info->edit_list->audio_rate ||
		    sample_edl->audio_chans != info->edit_list->audio_chans ||
		    sample_edl->audio_bits != info->edit_list->audio_bits ||
		    sample_edl->audio_bps != info->edit_list->audio_bps ) {

			veejay_msg(VEEJAY_MSG_WARNING, "Silencing this sample (mismatching audio properties)" );
			sample_edl->has_audio = 0;
			sample_edl->audio_chans = 0;
			sample_edl->audio_rate = 0;
			sample_edl->audio_bps = 0;
			sample_edl->audio_bits = 0;
		}
	}


	// the sample is not there yet,create it
	if(!sample)
	{
		sample = sample_skeleton_new( 0, sample_edl->total_frames );
		if(sample)
		{
			sample->edit_list = sample_edl;
			sample_store(sample);
		//	sample->speed = info->settings->current_playback_speed;
			veejay_msg(VEEJAY_MSG_INFO,"Created new sample %d from file %s",sample->sample_id,	files[0]);
		}
		else {
			veejay_msg(VEEJAY_MSG_ERROR,"Failed to create new sample from file '%s'", files[0]);
			if(files[0])
				free(files[0]);
			return -1;
		}
	}

  char* sample_name;
  if(!vj_get_sample_display_name(&sample_name, files[0])){
    veejay_msg(VEEJAY_MSG_ERROR,"Failed to create new sample filename for '%s'", files[0]);
  } else {
    sample_set_description(sample->sample_id, sample_name);
    free (sample_name);
  }

	// free temporary values
   	if(files[0]) free(files[0]);

    return sample->sample_id;
}

int veejay_edit_addmovie(veejay_t * info, editlist *el, char *movie, long start )
{
	video_playback_setup *settings =
		(video_playback_setup *) info->settings;
	uint64_t i,n;
	uint64_t c = el->video_frames;
	if( el->is_empty )
		c -= 2;

	n = open_video_file(movie, el, info->preserve_pathnames, info->auto_deinterlace,1,
		info->edit_list->video_norm, get_ffmpeg_pixfmt(info->pixel_format), info->video_output_width, info->video_output_height );

	if (n < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Error adding file '%s' to EDL", movie );
		return 0;
	}

	el->frame_list = (uint64_t *) realloc(el->frame_list, (c + el->num_frames[n])*sizeof(uint64_t));
	if (el->frame_list==NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
		vj_el_free(el);
		return 0;
	}

	for (i = 0; i < el->num_frames[n]; i++)
	{
		el->frame_list[c ++] = EL_ENTRY(n, i);
	}
 
	el->video_frames = c;
    el->total_frames = el->video_frames - 1;
	atomic_store_long_long(&settings->min_frame_num, 0);
	atomic_store_long_long(&settings->max_frame_num, (long long) el->total_frames);

	return 1;
}

int veejay_toggle_audio(veejay_t * info, int audio)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    editlist *el = info->current_edit_list;

    if( !(el->has_audio) ) {
	veejay_msg(VEEJAY_MSG_WARNING, 
		    "Audio playback has not been enabled");
	info->audio = NO_AUDIO;
	return 0;
    }

    settings->audio_mute = !settings->audio_mute;

    veejay_msg(VEEJAY_MSG_DEBUG, 
		"Audio playback was %s", audio == 0 ? "muted" : "unmuted");
 
    return 1;
}

int veejay_save_all(veejay_t * info, char *filename, long n1, long n2)
{
	editlist *e = info->edit_list;
	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ) {
		e = info->current_edit_list;
	}
	if( e->num_video_files <= 0 )
		return 0;
		
	if(n1 == 0 && n2 == 0 )
		n2 = e->total_frames;

	if( vj_el_write_editlist( filename, n1,n2, e ) )
		veejay_msg(VEEJAY_MSG_INFO, "Saved EDL to file %s", filename);
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error while saving EDL to %s", filename);
		return 0;
	}	

	return 1;
}

static int	veejay_open_video_files(veejay_t *info, char **files, int num_files, int force , char override_norm)
{
	if( info->dummy->active )
	{
		info->plain_editlist = vj_el_dummy( 0, 
				info->auto_deinterlace,
				info->dummy->chroma,
				info->dummy->norm,
				info->dummy->width,
				info->dummy->height,
				info->dummy->fps,
				info->pixel_format 
				);

		if( info->dummy->arate )
		{
			editlist *el = info->plain_editlist;
			el->has_audio = 1;
			el->audio_rate = info->dummy->arate;
			el->audio_chans = 2;
			el->audio_bits = 16;
			el->audio_bps = 4;
			veejay_msg(VEEJAY_MSG_DEBUG, "Dummy Audio: %f KHz, %d channels, %d bps, %d bit audio",
				(float)el->audio_rate/1000.0,el->audio_chans,el->audio_bps,el->audio_bits);
		}
		
		veejay_msg(VEEJAY_MSG_DEBUG,"Dummy Video: %dx%d, chroma %x, framerate %2.2f, norm %s",
					info->dummy->width,info->dummy->height, info->dummy->chroma,info->dummy->fps,
					(info->dummy->norm == 'n' ? "NTSC" :"PAL"));

		info->video_output_width = info->dummy->width;
		info->video_output_height = info->dummy->height;
	}
	else
	{
		int tmp_wid = info->video_output_width;
		int tmp_hei = info->video_output_height;

	    info->plain_editlist = 
			vj_el_init_with_args(
					files,
					num_files,
					info->preserve_pathnames,
					info->auto_deinterlace,
				    force,
					override_norm,
					info->pixel_format, tmp_wid, tmp_hei);
		if(!info->plain_editlist ) 
			return 0;
	}	
	info->edit_list = info->plain_editlist;
	//@ set current
	info->current_edit_list = info->edit_list;

	info->effect_frame_info->width = info->video_output_width;
	info->effect_frame_info->height= info->video_output_height;

	info->effect_frame_info2->width = info->video_output_width;
	info->effect_frame_info2->height= info->video_output_height;

	return 1;
}


static int configure_dummy_defaults(veejay_t *info, char override_norm, float fps, char **files, int n_files)
{
	int default_dw = 720;
	int default_dh = (override_norm == 'n' ? 480 : 576);
	int default_norm = (override_norm == '\0' ? VIDEO_MODE_PAL : veejay_get_norm( override_norm ) );
	float default_fps = vj_el_get_default_framerate(default_norm);
	
	if( has_env_setting( "VEEJAY_RUN_MODE", "CLASSIC" ) ) {
	       default_dw = (default_norm == VIDEO_MODE_PAL || default_norm == VIDEO_MODE_SECAM ? 352 : 360 );
	       default_dh = (default_norm == VIDEO_MODE_PAL || default_norm == VIDEO_MODE_SECAM ? 288 : 240 );
		   info->settings->fxdepth = 0;
	}

	int dw = default_dw;
	int dh = default_dh;

	float dfps = (fps <= 0.0f ? default_fps : fps );
	float tmp_fps = 0.0f;
	long tmp_arate = 0;

	if( n_files > 0  ) {
		int in_w = 0, in_h = 0;

		vj_el_scan_video_file( files[0], &in_w, &in_h, &tmp_fps, &tmp_arate );

		if( in_w <= 0 || in_h <= 0 ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Unable to determine video properties" );
		}

		if(info->video_output_width<=0)
			dw = in_w;
		if(info->video_output_height<=0)
			dh = in_h;
		
		if( tmp_fps > 0.0f && fps == 0 ) 
			dfps = tmp_fps;

		if( dw == default_dw && in_w > 0 )
			dw = in_w;
		if( dh == default_dh && in_h > 0 )
			dh = in_h;

		default_norm = (override_norm == '\0' ? veejay_get_norm(vj_el_get_default_norm(tmp_fps)) : veejay_get_norm(override_norm));
        
		//dw = (dw / 8) * 8;
        //dh = (dh / 8) * 8;

		veejay_msg(VEEJAY_MSG_DEBUG, "Video input source is: %dx%d %2.2f fps norm %d",in_w,in_h,tmp_fps, default_norm);

		if( in_w <= 0 || in_h <= 0) {
			return 0;
		}

	} 

	if( info->video_output_width <= 0 ) 
		info->video_output_width = dw;
	else
		dw = info->video_output_width;

	if( info->video_output_height <= 0 ) 
		info->video_output_height = dh;
	else
		dh = info->video_output_height;
	
	lav_set_project( dw, dh, dfps, info->pixel_format);	
	
	if( override_norm != '\0' ) {
		info->dummy->norm = override_norm;
	} else {
		info->dummy->norm = vj_el_get_default_norm( dfps );
	}

	if( info->dummy->width <= 0 )
		info->dummy->width  = dw;
	if( info->dummy->height <= 0)
		info->dummy->height = dh;
	if( info->dummy->fps <= 0.0f)
		info->dummy->fps = dfps;
	
	info->dummy->chroma = get_chroma_from_pixfmt( vj_to_pixfmt( info->pixel_format ) );
	info->settings->output_fps = dfps;

	if( n_files <= 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Dummy source is: %dx%d %2.2f fps norm %d",dw,dh,dfps, info->dummy->norm );
	}

	if( info->audio ) {

		if( tmp_arate > 0 && info->audio == AUDIO_PLAY && fps > 0.0f) {
			/* just warn if user customizes framerate */
			veejay_msg(VEEJAY_MSG_WARNING, "Going to run with user specified FPS. This will affect audio playback");
			veejay_msg(VEEJAY_MSG_WARNING, "Specify -a0 to start without audio playback");
		}

		if( tmp_arate == 0 && info->audio == AUDIO_PLAY ) {
			tmp_arate = 48000;
			veejay_msg(VEEJAY_MSG_WARNING, "Defaulting to 48Khz audio");
		}

		if( info->dummy->arate <= 0)
			info->dummy->arate = tmp_arate;

	}

	veejay_msg(VEEJAY_MSG_DEBUG, "Video output is %dx%d pixels, %2.2f fps", info->video_output_width, info->video_output_height, dfps );
	return 1;
}

int veejay_open_files(veejay_t * info, char **files, int num_files, float ofps, int force,int force_pix_fmt, char override_norm, int switch_jpeg)
{
	int ret = 0;
	switch( force_pix_fmt ) {
			case 1: info->pixel_format = FMT_422;break;
			case 2: info->pixel_format = FMT_422F;break;
			default:
				break;
	}

	char text[24];
	switch(info->pixel_format) {
		case FMT_422:
			sprintf(text, "4:2:2 [16-235][16-240]");
			break;
		case FMT_422F:	
			sprintf(text, "4:2:2 [0-255]");
			break;
		default:
			veejay_msg(VEEJAY_MSG_ERROR, "Unsupported pixel format selected"); 
			return 0;
	}

	if(force_pix_fmt > 0 ) {
	  	veejay_msg(VEEJAY_MSG_WARNING , "Output pixel format set to %s by user", text );
	}
	else
		veejay_msg(VEEJAY_MSG_DEBUG, "Processing set to YUV %s", text );

	if(!configure_dummy_defaults(info,override_norm, ofps,files,num_files)) {
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to setup video dimensions (did we load any video file ?)");
		return 0;
	}

	vj_el_init( info->pixel_format, switch_jpeg, info->video_output_width,info->video_output_height, info->dummy->fps );
#ifdef USE_GDK_PIXBUF	
    vj_picture_init( &(info->settings->sws_templ));
#endif

	info->effect_frame1 = yuv_yuv_template( NULL,NULL,NULL, info->video_output_width, info->video_output_height, yuv_to_alpha_fmt(vj_to_pixfmt(info->pixel_format)) );
	info->effect_frame1->fps = info->settings->output_fps;
	info->effect_frame2 = yuv_yuv_template( NULL,NULL,NULL, info->video_output_width, info->video_output_height, yuv_to_alpha_fmt(vj_to_pixfmt(info->pixel_format)) );
	info->effect_frame2->fps = info->settings->output_fps;
	info->effect_frame3 = yuv_yuv_template( NULL,NULL,NULL, info->video_output_width, info->video_output_height, yuv_to_alpha_fmt(vj_to_pixfmt(info->pixel_format)) );
	info->effect_frame3->fps = info->settings->output_fps;
	info->effect_frame4 = yuv_yuv_template( NULL,NULL,NULL, info->video_output_width, info->video_output_height, yuv_to_alpha_fmt(vj_to_pixfmt(info->pixel_format)) );
	info->effect_frame4->fps = info->settings->output_fps;

	veejay_msg(VEEJAY_MSG_DEBUG,"Performer is working in %s (%d)", yuv_get_pixfmt_description(info->effect_frame1->format), info->effect_frame1->format);

	if(num_files == 0)
	{
		if(!info->dummy->active)
			info->dummy->active = 1; /* auto enable dummy mode if not already enabled */
		ret = veejay_open_video_files( info, NULL, 0 , force, override_norm );
	}
	else
	{
		ret = veejay_open_video_files( info, files, num_files, force, override_norm );
	}

	return ret;
}
