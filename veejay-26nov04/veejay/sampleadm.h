/*
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg < nelburg@sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#ifndef CLIPADM_H
#define CLIPADM_H
#include <stdint.h>
#include <config.h>
#include <veejay/hash.h>
#include <veejay/lav_io.h>
#include <veejay/vj-effman.h>
#include <veejay/vj-global.h>
#include <veejay/vj-el.h>
#ifdef HAVE_XML2
#include <libxml2/xmlmemory.h>
#include <libxml2/parser.h>
#endif
#define CLIP_MAX_RENDER     10 /* 10 at most */
#define CLIP_MAX_CLIPS  16384 /* 4096 clips at most */

#define CLIP_MAX_PARAMETERS 10	/* 10 parameters per effect at most */
#define CLIP_ARG1 0
#define CLIP_ARG2 1
#define CLIP_ARG3 2
#define CLIP_ARG4 3
#define CLIP_ARG5 4
#define CLIP_ARG6 5
#define CLIP_ARG7 6
#define CLIP_ARG8 7
#define CLIP_ARG9 8
#define CLIP_ARG10 9

#define CLIP_FREEZE_NONE 0
#define CLIP_FREEZE_PAUSE 1
#define CLIP_FREEZE_BLACK 2

#define CLIP_RENDER_START 1
#define CLIP_RENDER_STOP 0
enum {
    CLIP_LOAD = 0,
    CLIP_RUN = 1,
    CLIP_PEEK = 2,
};

typedef struct vj_wav_file_t
{
	int	fd;
	int	channels;
	int	rate;
	int	bits;
	int	bps;
	long    num_samples;
	float	seconds;
	int	buf_size;
} vj_wav_file;


typedef struct vj_effect_key_frame_t {
	int frame_start_offset;
	int frame_end_offset;
	int enabled;
	int duration;
	int direction;
	int start_value;
	int end_value;
	int last_value;
	int clip_offset;
 	float inc_val;
} vj_effect_key_frame;

typedef struct clip_eff_t {
    vj_effect_key_frame *ekf[CLIP_MAX_PARAMETERS];
    int effect_id;		/* effect ID */
    int e_flag;

    int arg[CLIP_MAX_PARAMETERS];	/* array of arguments */
    int frame_offset;
    int frame_trimmer;		/* sub frame scratcher */
    /* audio settings */
    int a_flag;			/* audio enabled/disabled */
    int volume;			/* volume of 0-100 of audio */
    int source_type;		/* source type to mix with */
    int channel;		/* secundary source id */
    int is_rendering;		/* is rendering */
} clip_eff_chain;


typedef struct clip_info_t {
    int clip_id;		/* identifies a unique clip */
    clip_eff_chain *effect_chain[CLIP_MAX_EFFECTS];	/* effect chain */
    long first_frame[CLIP_MAX_RENDER];		/* start of clip */
    long last_frame[CLIP_MAX_RENDER];		/* end of clip */
    char descr[150];
    int speed;			/* playback speed */
    int looptype;		/* pingpong or loop */
    int max_loops;		/* max looops before going to */
    int max_loops2;		/* count remaining loops */
    int next_clip_id;		/* the next clip */
    int depth;			/* clip effect chain render depth */
    int source;			/* source or tag */
    int channel;		/* which channel (which tag) */
    int playmode;		/* various playmodes */
    int playmode_frame;
    int sub_audio;		/* mix underlying clip yes or no */
    int audio_volume;		/* volume setting of this clip */
    int marker_start;
    int marker_end;
    int dup;			/* frame duplicator */
    int active_render_entry;
    int loop_dec;
    int loop_periods;
    int freeze_mode;
    int freeze_nframes;
    int freeze_pframes;
    int freeze_val_nframes;
    int freeze_val_pframes;
    int marker_speed;
    int fader_active;
    int fader_direction;
    float fader_val;
    float fader_inc;
    int encoder_active;
    unsigned long sequence_num;
    unsigned long rec_total_bytes;
    char *encoder_base;
    unsigned long encoder_total_frames;
    char *encoder_destination;
    int encoder_format;
    lav_file_t *encoder_file;
    long encoder_duration; /* in seconds */
    long encoder_num_frames;
    long encoder_succes_frames;
    int encoder_width;
    int encoder_height;
    int encoder_max_size;
    int auto_switch;	
    int selected_entry;
    int effect_toggle;
    int offset;
    vj_wav_file *wav;
    int external_wave;
} clip_info;

#define CLIP_YUV420_BUFSIZE 16
#define CLIP_MAX_DEPTH 4
#define CLIP_DEC_BIBBER 1
#define CLIP_DEC_FREEZE 2

int clip_chain_malloc(int clip_id);
int clip_chain_free(int clip_id);
int clip_size();
int clip_verify();
void clip_init(int len);
int clip_update(clip_info *clip, int s1);
#ifdef HAVE_XML2
int clip_readFromFile(char *);
int clip_writeToFile(char *);
#endif
int clip_update_offset(int s1, int nframe);
int clip_set_state(int new_state);
int clip_get_state();
clip_info *clip_skeleton_new(long startFrame, long endFrame);
clip_info *clip_get(int clip_id);
int clip_store(clip_info * skel);
int clip_is_deleted(int s1);
int clip_exists(int clip_id);
int clip_del(int clip_id);
void clip_del_all();
int clip_close_external(int clip_id);
int clip_use_external(int clip_id);
int clip_read_external(int clip_id, uint8_t *dst);
int clip_start_external(int clip_id, char *filename, editlist *el);
int clip_get_startFrame(int clip_id);
int clip_get_endFrame(int clip_id);
int clip_set_marker_start(int clip_id, int marker);
int clip_set_marker_end(int clip_id, int marker);
int clip_set_startframe(int s1, long frame_num);
int clip_set_endframe(int s1, long frame_num);
int clip_set_marker(int s1, int start, int end);
int clip_get_longest(int clip_id);
int clip_get_playmode(int s1);
int clip_set_playmode(int s1, int playmode);
int clip_get_loops(int s1);
int clip_get_loops2(int s1);
int clip_get_next(int s1);
int clip_get_depth(int s1);
int clip_set_depth(int s1, int n);
int clip_set_speed(int s1, int speed);
int clip_set_framedup(int s1, int n);
int clip_get_framedup(int s1);
int clip_marker_clear(int clip_id); 
int clip_set_looptype(int s1, int looptype);
int clip_get_speed(int s1);
int clip_get_looptype(int s1);
int clip_set_loops(int s1, int nr_of_loops);
int clip_set_loops2(int s1, int nr);
int clip_set_next(int s1, int next_clip_id);
int clip_get_chain_source(int clip_id, int position);
int clip_set_chain_source(int clip_id, int position, int source);
int clip_get_sub_audio(int s1);
int clip_set_sub_audio(int s1, int audio);
int clip_get_audio_volume(int s1);
int clip_set_audio_volume(int s1, int volume);
int clip_copy(int s1);
int clip_get_effect(int s1, int position);
/* get effect any, even if effect is disabled (required for informational purposes)*/
int clip_get_effect_any(int s1, int position);
int clip_get_offset(int s1, int position);

/* trimmer is usefull for underlying clips in the effect chain.
   you can manual adjust the video/audio sync of the underlying clip */
int clip_get_trimmer(int s1, int position);
int clip_set_trimmer(int s1, int position, int trimmer);
int clip_get_short_info(int clip_id, int *, int *, int *, int *) ;
int clip_get_chain_volume(int s1, int position);

/* set volume of audio data coming to the chain */
int clip_set_chain_volume(int s1, int position, int volume);

/* whether to mix underlying clip's audio */
int clip_get_chain_audio(int s1, int position);
int clip_has_extra_frame(int s1, int position);
/* mix the audio from entry from the effect chain, if any */
int clip_set_chain_audio(int s1, int position, int flag);

int clip_set_chain_status(int s1, int position, int status);
int clip_get_chain_status(int s1, int position);

int clip_set_offset(int s1, int position, int frame_offset);
int clip_get_effect_arg(int s1, int position, int argnr);
int clip_set_effect_arg(int s1, int position, int argnr, int value);

int clip_get_all_effect_arg(int s1, int position, int *args,
			      int arg_len, int n_elapsed);
int clip_chain_remove(int s1, int position);
int clip_chain_clear(int s1);
int clip_chain_size(int s1);
int clip_chain_get_free_entry(int s1);
int clip_chain_add(int s1, int c, int effect_nr);

/* channel depends on source , it select a channel of a certain video source */
int clip_get_chain_channel(int s1, int position);
int clip_set_chain_channel(int s1, int position, int channel);

//int clip_chain_replace(int s1, int position, int effect_id);

int clip_chain_sprint_status(int s1, int entry, int changed, int r_changed, char *str,
			       int frame);

int clip_set_render_entry(int s1, int entry);
int clip_get_render_entry(int s1);

int clip_set_description(int clip_id, char *description);
int clip_get_description(int clip_id, char *description);

int clip_entry_is_rendering(int clip_id, int entry);
int clip_entry_set_is_rendering(int clip_id, int entry, int value);
int clip_get_loop_dec(int s1);
int clip_set_loop_dec(int s1, int active, int periods);
int clip_apply_loop_dec(int s1, double fps); 

int clip_set_freeze_mode(int s1, int mode, int nframes, int pframes);
int clip_get_freeze_nframes(int s1);
int clip_get_freeze_pframes(int s1);
int clip_get_freeze_mode(int s1);
int clip_set_freeze_pframes(int s1 , int nframes);
int clip_set_freeze_nframes(int s1,  int nframes);
int clip_reset_freeze(int s1);
int	clip_set_manual_fader(int s1, int value );
int clip_apply_fader_inc(int s1);
int clip_set_fader_active(int s1, int nframes, int direction);
int clip_set_fader_val(int s1, float val);
int clip_get_fader_active(int s1);
float clip_get_fader_val(int s1);
float clip_get_fader_inc(int s1);
int clip_get_fader_direction(int s1);
int clip_reset_fader(int t1);

int clip_reset_offset(int s1);

int clip_get_effect_status(int s1);
int clip_get_selected_entry(int s1);

int clip_set_effect_status(int s1, int status);
int clip_set_selected_entry(int s1, int position);


int clip_set_effect_key_frame(int s1, int position, int arg_num, int end_val, int length);
int clip_enable_effect_key_frame(int s1, int position, int arg_num);
int clip_disable_effect_key_frame(int s1, int position, int arg_num);
int clip_clear_effect_key_frame(int s1, int position, int arg_num);
int clip_apply_effect_key_frame(int s1, int position, int arg_num, int n_frame);
#ifdef HAVE_XML2
void CreateClip(xmlNodePtr node, clip_info * clip);
void CreateEffects(xmlNodePtr node, clip_eff_chain ** effects);
void CreateEffect(xmlNodePtr node, clip_eff_chain * effect, int pos);
void CreateArguments(xmlNodePtr node, int *arg, int argcount);
void ParseClip(xmlDocPtr doc, xmlNodePtr cur, clip_info * skel);
void ParseEffects(xmlDocPtr doc, xmlNodePtr cur, clip_info * skel);
void ParseEffect(xmlDocPtr doc, xmlNodePtr cur, int dst_clip);
void ParseArguments(xmlDocPtr doc, xmlNodePtr cur, int *arg);
unsigned char *UTF8toLAT1(unsigned char *in);
#endif

#endif
