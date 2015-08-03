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

#ifndef SAMPLEADM_H
#define SAMPLEADM_H
#include <stdint.h>
#include <config.h>
#include <libvje/vje.h>
#include <libhash/hash.h>
#include <libel/vj-el.h>
#ifdef HAVE_XML2
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#endif
#define SAMPLE_MAX_SAMPLES  16384 /* 4096 samples at most */

#define SAMPLE_MAX_PARAMETERS 32	/* 32 parameters per effect at most */
#ifdef HAVE_XML2
#define XMLTAG_RENDER_ENTRY "render_entry"
#define XMLTAG_SAMPLES    "veejay_samples"
#define XMLTAG_SAMPLE     "sample"
#define XMLTAG_SAMPLEID   "sampleid"
#define XMLTAG_SAMPLEDESCR "description"
#define XMLTAG_FIRSTFRAME "startframe"
#define XMLTAG_LASTFRAME  "endframe"
#define XMLTAG_EFFECTS    "effects"
#define XMLTAG_VOL	  "volume"
#define XMLTAG_EFFECT     "effect"
#define XMLTAG_EFFECTID   "effectid"
#define XMLTAG_ARGUMENTS  "arguments"
#define XMLTAG_ARGUMENT   "argument"
#define XMLTAG_EFFECTSOURCE "source"
#define XMLTAG_EFFECTCHANNEL "channel"
#define XMLTAG_EFFECTTRIMMER "trimmer"
#define XMLTAG_EFFECTOFFSET "offset"
#define XMLTAG_EFFECTACTIVE "active"
#define XMLTAG_EFFECTAUDIOFLAG "use_audio"
#define XMLTAG_EFFECTAUDIOVOLUME "chain_volume"
#define XMLTAG_SPEED      "speed"
#define XMLTAG_FRAMEDUP   "frameduplicator"
#define XMLTAG_LOOPTYPE   "looptype"
#define XMLTAG_MAXLOOPS   "maxloops"
#define XMLTAG_NEXTSAMPLE "nextsample"
#define XMLTAG_DEPTH	  "depth"
#define XMLTAG_PLAYMODE   "playmode"
#define XMLTAG_VOLUME	  "volume"
#define XMLTAG_SUBAUDIO	  "subaudio"
#define XMLTAG_MARKERSTART "markerstart"
#define XMLTAG_MARKEREND   "markerend"
#define XMLTAG_EFFECTPOS   "position"
#define XMLTAG_FADER_ACTIVE "chain_fade"
#define XMLTAG_FADER_VAL    "chain_fade_value"
#define XMLTAG_FADER_INC    "chain_fade_increment"
#define XMLTAG_FADER_DIRECTION "chain_direction"
#define XMLTAG_LASTENTRY    "current_entry"
#define XMLTAG_CHAIN_ENABLED "fx"
#define XMLTAG_EDIT_LIST_FILE "editlist_filename"
#define XMLTAG_BOGUSVIDEO	 "playlength"
#endif
#define SAMPLE_FREEZE_NONE 0
#define SAMPLE_FREEZE_PAUSE 1
#define SAMPLE_FREEZE_BLACK 2

#define SAMPLE_RENDER_START 1
#define SAMPLE_RENDER_STOP 0

#define SAMPLE_MAX_DESCR_LEN 24

enum {
    SAMPLE_LOAD = 0,
    SAMPLE_RUN = 1,
    SAMPLE_PEEK = 2,
};



typedef struct sample_eff_t {
    int effect_id;		/* effect ID */
    int e_flag;

    int arg[SAMPLE_MAX_PARAMETERS];	/* array of arguments */
    int frame_offset;
    int frame_trimmer;		/* sub frame scratcher */
    /* audio settings */
    int a_flag;			/* audio enabled/disabled */
    int volume;			/* volume of 0-100 of audio */
    int source_type;		/* source type to mix with */
    int channel;		/* secundary source id */
    int is_rendering;		/* is rendering */
    void *kf;			/* keyframe values for this entry */
    int kf_status;	        /* use keyframed values */
    int kf_type;		/* store type used */
    void *fx_instance;		/* lib plugger instance */
} sample_eff_chain;


typedef struct sample_info_t {
    int sample_id;		/* identifies a unique sample */
    sample_eff_chain *effect_chain[SAMPLE_MAX_EFFECTS];	/* effect chain */
    long first_frame;		/* start of sample */
    long last_frame;		/* end of sample */
    char descr[SAMPLE_MAX_DESCR_LEN];
    int speed;			/* playback speed */
    int looptype;		/* pingpong or loop */
    int max_loops;		/* max looops before going to */
    int max_loops2;		/* count remaining loops */
    int next_sample_id;		/* the next sample */
    int depth;			/* sample effect chain render depth */
    int source;			/* source or tag */
    int channel;		/* which channel (which tag) */
    int playmode;		/* various playmodes */
    int playmode_frame;
    int sub_audio;		/* mix underlying sample yes or no */
    int audio_volume;		/* volume setting of this sample */
    int marker_start;
    int marker_end;
    int dup;			/* frame duplicator */
    int dups;
    int loop_dec;
    int loop_periods;
    int marker_speed;
    int fader_active;
    int fader_direction;
    float fader_val;
    float fader_inc;
    int encoder_active;
    unsigned long sequence_num;
    unsigned long rec_total_bytes;
    unsigned long encoder_total_frames;
    char *encoder_destination;
    int encoder_format;
    void *encoder;
    void  *encoder_file;
    long  encoder_frames_to_record;
    long  encoder_frames_recorded;
    long  encoder_total_frames_recorded;
 
    int encoder_width;
    int encoder_height;
    int encoder_max_size;
    
    int auto_switch;	
    int selected_entry;
    int effect_toggle;
    int offset;
    int play_length;
    int loopcount;
    editlist *edit_list;
    char     *edit_list_file;
    int		soft_edl;
    void	*dict;
    void	*kf;
    int          composite;
    void	*viewport_config;
    void	*viewport;
    long	resume_pos;
	int		subrender;
} sample_info;

#define SAMPLE_YUV420_BUFSIZE 16
#define SAMPLE_MAX_DEPTH 4
#define SAMPLE_DEC_BIBBER 1
#define SAMPLE_DEC_FREEZE 2
extern int sample_chain_malloc(int sample_id);
extern int sample_chain_free(int sample_id);
extern int sample_size();
extern int sample_verify();
extern void sample_init(int len, void *font, editlist *el);
extern int sample_update(sample_info *sample, int s1);
#ifdef HAVE_XML2
extern int sample_readFromFile(char *, void *vp, void *ptr, void *font, void *el, int *id, int *mode);
extern int sample_writeToFile(char *, void *vp, void *ptr, void *font, int id, int mode);
#endif
extern int sample_update_offset(int s1, int nframe);
extern int sample_set_state(int new_state);
extern int sample_get_state();
extern sample_info *sample_skeleton_new(long startFrame, long endFrame);
extern sample_info *sample_get(int sample_id);
void 	sample_new_simple( void *el, long start, long end );
extern int sample_store(sample_info * skel);
extern int sample_is_deleted(int s1);
extern int sample_exists(int sample_id);
extern int sample_verify_delete( int sample_id, int sample_type );
extern int sample_del(int sample_id);
extern void sample_del_all(void *edl);
extern int sample_get_startFrame(int sample_id);
extern int sample_get_endFrame(int sample_id);
extern int sample_set_marker_start(int sample_id, int marker);
extern int sample_set_marker_end(int sample_id, int marker);
extern int sample_set_startframe(int s1, long frame_num);
extern int sample_set_endframe(int s1, long frame_num);
extern int sample_set_marker(int s1, int start, int end);
extern int sample_get_longest(int sample_id);
extern int sample_get_playmode(int s1);
extern int sample_set_playmode(int s1, int playmode);
extern int sample_get_subrender(int s1);
extern void sample_set_subrender(int s1, int status);
extern int sample_get_loops(int s1);
extern int sample_get_loops2(int s1);
extern int sample_get_next(int s1);
extern int sample_get_depth(int s1);
extern int sample_set_depth(int s1, int n);
extern int sample_set_speed(int s1, int speed);
extern void sample_loopcount(int s1);
extern void sample_reset_loopcount(int s1);
extern int sample_get_loopcount(int s1);
extern int sample_set_composite(void *compiz,int s1, int composite);
extern int sample_get_composite(int s1);
extern int sample_set_framedup(int s1, int n);
extern int sample_get_framedup(int s1);
extern int sample_set_framedups(int s1, int n);
extern int sample_get_framedups(int s1);
extern int sample_marker_clear(int sample_id); 
extern int sample_set_looptype(int s1, int looptype);
extern int sample_get_speed(int s1);
extern int sample_get_looptype(int s1);
extern int sample_set_loops(int s1, int nr_of_loops);
extern int sample_set_loops2(int s1, int nr);
extern int sample_set_next(int s1, int next_sample_id);
extern int sample_get_chain_source(int sample_id, int position);
extern int sample_set_chain_source(int sample_id, int position, int source);
extern int sample_get_sub_audio(int s1);
void    *sample_get_kf_port( int s1, int entry );
extern int	sample_chain_set_kf_status( int s1, int entry, int status );
extern int	sample_get_kf_status( int s1, int entry, int *type );
extern unsigned char * sample_chain_get_kfs( int s1, int entry, int parameter_id, int *len );
extern int     sample_chain_set_kf_status( int s1, int entry, int status );
extern int     sample_chain_set_kfs( int s1, int len, char *data );
extern int	sample_chain_reset_kf( int s1, int entry );
extern int	sample_has_cali_fx(int sample_id);
extern void	sample_cali_prepare( int sample_id, int slot, int chan );
extern int sample_set_sub_audio(int s1, int audio);
extern int sample_get_audio_volume(int s1);
extern int sample_set_audio_volume(int s1, int volume);
extern int sample_copy(int s1);
void	*sample_get_plugin( int s1, int position, void *ptr );
extern int sample_get_effect(int s1, int position);
/* get effect any, even if effect is disabled (required for informational purposes)*/
extern int sample_get_effect_any(int s1, int position);
extern int sample_get_offset(int s1, int position);
extern int sample_get_first_mix_offset(int s1, int *parent, int look_for );
/* trimmer is usefull for underlying samples in the effect chain.
   you can manual adjust the video/audio sync of the underlying sample */
extern int sample_get_trimmer(int s1, int position);
extern int sample_set_trimmer(int s1, int position, int trimmer);
extern int sample_get_short_info(int sample_id, int *, int *, int *, int *) ;
extern int sample_get_chain_volume(int s1, int position);
extern void    sample_set_kf_type(int s1, int entry, int type );
/* set volume of audio data coming to the chain */
extern int sample_set_chain_volume(int s1, int position, int volume);

/* whether to mix underlying sample's audio */
extern int sample_get_chain_audio(int s1, int position);
extern int sample_has_extra_frame(int s1, int position);
/* mix the audio from entry from the effect chain, if any */
extern int sample_set_chain_audio(int s1, int position, int flag);

extern int sample_set_chain_status(int s1, int position, int status);
extern int sample_get_chain_status(int s1, int position);

extern int sample_set_offset(int s1, int position, int frame_offset);
extern int sample_get_effect_arg(int s1, int position, int argnr);
extern int sample_set_effect_arg(int s1, int position, int argnr, int value);

extern int sample_get_all_effect_arg(int s1, int position, int *args,
			      int arg_len, int n_elapsed);
extern int sample_chain_remove(int s1, int position);
extern int sample_chain_clear(int s1);
extern int sample_chain_size(int s1);
extern int sample_chain_get_free_entry(int s1);
extern int sample_chain_add(int s1, int c, int effect_nr);

/* channel depends on source , it select a channel of a certain video source */
extern int sample_get_chain_channel(int s1, int position);
extern int sample_set_chain_channel(int s1, int position, int channel);

//int sample_chain_replace(int s1, int position, int effect_id);

extern int sample_chain_sprint_status(int s1,int cache,int sa,int ca, int r, int f, int m, int t,int sr,int curfps,uint32_t lo, uint32_t hi, int macro,char *s ); 

extern int sample_set_render_entry(int s1, int entry);
extern int sample_get_render_entry(int s1);

extern int sample_set_description(int sample_id, char *description);
extern int sample_get_description(int sample_id, char *description);

extern int sample_entry_is_rendering(int sample_id, int entry);
extern int sample_entry_set_is_rendering(int sample_id, int entry, int value);
extern int sample_get_loop_dec(int s1);
extern int sample_set_loop_dec(int s1, int active);
extern int sample_apply_loop_dec(int s1, double fps); 

extern int	sample_set_manual_fader(int s1, int value );
extern int sample_apply_fader_inc(int s1);
extern int sample_set_fader_active(int s1, int nframes, int direction);
extern int sample_set_fader_val(int s1, float val);
extern int sample_get_fader_active(int s1);
extern float sample_get_fader_val(int s1);
extern float sample_get_fader_inc(int s1);
extern int sample_get_fader_direction(int s1);
extern int sample_reset_fader(int t1);

extern int sample_reset_offset(int s1);

extern int sample_get_effect_status(int s1);
extern int sample_get_selected_entry(int s1);

extern int sample_set_effect_status(int s1, int status);
extern int sample_set_selected_entry(int s1, int position);

extern int sample_set_editlist( int s1, editlist *edl );
extern editlist *sample_get_editlist(int s1 );
extern int     sample_get_el_position( int sample_id, int *start, int *end );

extern	void	*sample_get_dict( int sample_id );

extern int sample_var( int s1, int *type, int *fader, int *fx, int *rec, int *active );

extern void        sample_set_project(int fmt, int deinterlace, int flags, int force, char norm, int w, int h );

extern int     sample_video_length( int s1 );
extern int	sample_usable_edl( int s1 );

extern int sample_cache_used( int s1 );
extern void        sample_free(void *edl);
extern int	sample_load_composite_config( void *compiz, int s1 );
extern int sample_stop_playing(int s1, int new_s1);
extern int sample_start_playing(int s1, int no_cache);
extern int sample_get_kf_tokens( int s1, int entry, int id, int *start,int *end, int *type);
extern char *UTF8toLAT1(unsigned char *in);
extern int sample_read_edl( sample_info *sample );

extern int     sample_max_video_length(int s1);

extern	int	sample_set_composite_view(int s1, void *vp );
extern void	*sample_get_composite_view(int s1);

extern	long	sample_get_resume(int s1);
extern	int		sample_set_resume(int s1, long pos );

extern void	sample_chain_alloc_kf( int s1, int entry );

#ifdef HAVE_XML2
extern void CreateSample(xmlNodePtr node, sample_info * sample, void *font);
extern void CreateEffects(xmlNodePtr node, sample_eff_chain ** effects);
extern void CreateEffect(xmlNodePtr node, sample_eff_chain * effect, int pos);
extern void CreateArguments(xmlNodePtr node, int *arg, int argcount);
extern void CreateKeys(xmlNodePtr node, int argcount, void *port );
extern xmlNodePtr ParseSample(xmlDocPtr doc, xmlNodePtr cur, sample_info * skel, void *el, void *font, int start_at, void *vp);
extern void ParseEffects(xmlDocPtr doc, xmlNodePtr cur, sample_info * skel, int start_at);
extern void ParseEffect(xmlDocPtr doc, xmlNodePtr cur, int dst_sample, int start_at);
extern void ParseArguments(xmlDocPtr doc, xmlNodePtr cur, int *arg );
extern void ParseKEys(xmlDocPtr doc, xmlNodePtr cur, void *port);
#endif
void	sample_reload_config(void *compiz, int s1, int mode );


#endif
