/* 
 * veejay  
 *
 * Copyright (C) 2000-2008 Niels Elburg <nwelburg@gmail.com>
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
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <veejaycore/defs.h>
#include <libsample/sampleadm.h>  
#include <libstream/vj-tag.h>
#include <veejaycore/vj-server.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <libveejay/vj-lib.h>
#include <libel/vj-el.h>
#include <math.h>
#include <libel/vj-avcodec.h>
#include <libveejay/vj-event.h>
#include <veejaycore/mpegconsts.h>
#include <veejaycore/mpegtimecode.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/atomic.h>
#include <veejaycore/vj-msg.h>
#include <libveejay/vj-perform.h>
#include <libveejay/libveejay.h>
#include <libveejay/vj-sdl.h>
#include <libsamplerec/samplerecord.h>
#include <libel/pixbuf.h>
#include <veejaycore/avcommon.h>
#include <libveejay/vj-misc.h>
#include <veejaycore/vj-task.h>
#include <veejaycore/lzo.h>
#include <libveejay/vj-viewport.h>
#include <libveejay/vj-composite.h>
#ifdef HAVE_FREETYPE
#include <libveejay/vj-font.h>
#endif
#define RECORDERS 1
#ifdef HAVE_JACK
#include <libveejay/vj-jack.h>
#endif
#include <libvje/internal.h>
#include <veejaycore/vjmem.h>
#include <libvje/effects/opacity.h>
#include <libvje/effects/masktransition.h>
#include <libvje/effects/shapewipe.h>
#include <libqrwrap/qrwrapper.h>
#include <libveejay/vj-split.h>
#include <libveejay/vjkf.h>
#include <veejaycore/libvevo.h>
#include <libvje/libvje.h>
#ifdef HAVE_JACK
#include <libveejay/audioscratcher.h>
#include <libveejay/vj-jack.h>
#endif
#define PERFORM_AUDIO_SIZE 16384
#define PSLOW_A 3
#define PSLOW_B 4
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#include <build.h>
#ifndef SAMPLE_FMT_S16
#define SAMPLE_FMT_S16 AV_SAMPLE_FMT_S16
#endif

#define PRIMARY_FRAMES 6
#define FADE_LUT_SIZE 256

#include <libvje/effects/shapewipe.h>

typedef struct {
    uint8_t *Y;
    uint8_t *Cb;
    uint8_t *Cr;
    uint8_t *alpha;
    uint8_t *P0;
    uint8_t *P1;
    int      ssm;
    int      fx_id;
} ycbcr_frame; //@ TODO: drop this structure and replace for veejay-next 's VJFrame

typedef struct {
    int fader_active;
    int fade_method;
    int fade_value;
    int fade_entry;
    int fade_alpha;
    int follow_fade;
    int follow_now[2];
    int follow_run;
    int fx_status;
    int enc_active;
    int type;
    int active;
} varcache_t;

extern uint8_t pixel_Y_lo_;


static long vj_frame_rand(long long frame_num, long start, long end, unsigned long long seed) {
    if (start >= end) return (int)start;

    unsigned long long x = ((unsigned long long)frame_num ^ seed);
    
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);

    long long range = (end - start) + 1;
    return (start + (x % (unsigned long long)range));
}

#define CACHE_TOP 0
#define CACHE 1
#define CACHE_SIZE (SAMPLE_MAX_EFFECTS+CACHE) * 2


#define AC_STATE_IDLE      0
#define AC_STATE_PRODUCING 1
#define AC_STATE_READY     2
#define AC_STATE_CONSUMING 3

typedef struct {
    volatile long long offset;
    long long start;
    long long end;
    long long last_resampled_frame;
    int consumed_samples;
    int  sample_id;
    int  sample_type;
    int  direction;
    int  max_sfd;
    int  cur_sfd;
    int  direction_changed;
    int  audio_last_stretched_samples;
    int  audio_src_offset;
    int  speed;
    int loopmode;
    int audio_total_samples;
    int last_resampled_dir;
    float last_rms_slope;
   	int		prev_n_samples;
    int frame_in_history[8];
    int flip_lock;
    int pending_flip_dir;
} sample_b_t; // FIXME clean up fields

typedef struct
{
    varcache_t pvar_;
    VJFrame *rgba_frame[2];
    uint8_t *rgba_buffer[2];

    VJFrame *yuva_frame[2];
    VJFrame *yuv420_frame[2];
    ycbcr_frame **frame_buffer;   /* chain */
    ycbcr_frame **primary_buffer; /* normal */

    uint8_t *audio_buffer[SAMPLE_MAX_EFFECTS];   /* the audio buffer */
    uint8_t *audio_silence_;
    uint8_t *lin_audio_buffer_;
    uint8_t *top_audio_buffer;
    int play_audio_sample_;
    uint8_t *audio_rec_buffer;
    uint8_t *audio_render_buffer;
    size_t audio_render_buffer_capacity;
    uint8_t *audio_downmix_buffer;
    uint8_t *audio_chain_final_buffer;
    uint8_t *down_sample_buffer;
    uint8_t *down_sample_rec_buffer;
    uint8_t *temp_buffer[4];
    VJFrame temp_frame;
    uint8_t *subrender_buffer[4];
    void *rgba2yuv_scaler;
    void *yuv2rgba_scaler;
    void *yuv420_scaler;
    uint8_t *pribuf_area;
    size_t pribuf_len;
    uint8_t *fx_chain_buffer;
    size_t fx_chain_buflen;

    VJFrame *tmp1;
    VJFrame *tmp2;

    int chain_id;

    audio_edge_t *audio_edge;
    
    sample_b_t sample_b;
    sample_b_t sample_a;

    void *audio_scratcher;
} performer_t;

typedef struct audio_chain_t {
    int sample_id;
    int sample_type;
    long long start;
    long long end;
    int speed;
    int loopmode;
    uint8_t *buffer;
    editlist *el;
    float opacity;
    long long offset;
    int cur_sfd;
    int max_sfd;
    volatile int ready;
    int buffer_size;
    volatile int state;
} audio_chain_entry_t;

typedef struct {
    audio_chain_entry_t entries[SAMPLE_MAX_EFFECTS];
    int count;
    volatile int state;
} audio_chain_buffer_t;

typedef struct
{
    int sample_id;
    int mode;
    int entry;
    ycbcr_frame *frame;
} performer_cache_t;

typedef struct
{
    performer_cache_t cached_frames[CACHE_SIZE];
    audio_chain_buffer_t audio_chain_buffers[VIDEO_QUEUE_LEN];
    int played_sample_ids[SAMPLE_MAX_EFFECTS];
    long long played_sample_positions[SAMPLE_MAX_EFFECTS];
    float *accum[VIDEO_QUEUE_LEN];
    volatile int audio_chain_index;
    int n_cached_frames;
    void *encoder_;
    ycbcr_frame *preview_buffer;
    int preview_max_w;
    int preview_max_h;
    performer_t *A;
    performer_t *B;
    VJFrame feedback_frame;
    uint8_t *feedback_buffer[4];
    VJFrame *offline_frame;
    float *fade_lut;
    float *gain_lut[2];

} performer_global_t;

static const char *intro = 
    "A visual instrument for GNU/Linux\n";

#define MLIMIT(var, low, high) \
if((var) < (low)) { var = (low); } \
if((var) > (high)) { var = (high); }

//forward
static  int vj_perform_preprocess_secundary( veejay_t *info, performer_t *p, int id, int mode,int current_ssm,int chain_entry, VJFrame **frames, VJFrameInfo *frameinfo );
static int vj_perform_get_frame_fx(veejay_t *info, int s1, long long nframe, VJFrame *src, VJFrame *dst, uint8_t *p0plane, uint8_t *p1plane);

static void vj_perform_pre_chain(performer_t *p, VJFrame *frame);
static int vj_perform_post_chain_sample(veejay_t *info,performer_t *p, VJFrame *frame,int sample_id);
static int vj_perform_post_chain_tag(veejay_t *info,performer_t*p, VJFrame *frame, int sample_id);
static void vj_perform_plain_fill_buffer(veejay_t * info,performer_t *p,VJFrame *dst,int sample_id, int mode, long frame_num);
static void vj_perform_tag_fill_buffer(veejay_t * info, performer_t *p, VJFrame *dst, int sample_id);
static int vj_perform_apply_secundary_tag(veejay_t * info, performer_t *p, int sample_id,int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0, uint8_t *p1, int subrender);
static int vj_perform_apply_secundary(veejay_t * info, performer_t *p, int this_sample_id,int sample_id,int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0, uint8_t *p1, int subrender);
static void vj_perform_tag_complete_buffers(veejay_t * info, performer_t *p, vjp_kf *effect_info, int *h, VJFrame *f0, VJFrame *f1, int sample_id, int pm, vjp_kf *setup, sample_eff_chain **chain, vj_tag *tag);
static void vj_perform_sample_complete_buffers(veejay_t * info, performer_t *p, vjp_kf *effect_info, int *h, VJFrame *f0, VJFrame *f2, int sample_id, int pm, vjp_kf *setup, sample_eff_chain **chain, sample_info *si);
static void vj_perform_apply_first(veejay_t *info, performer_t *p, vjp_kf *todo_info, VJFrame **frames, sample_eff_chain *entry, int e, int c, long long n_frames, void *ptr, int playmode );
static int vj_perform_render_sample_frame(veejay_t *info, performer_t *p, uint8_t *frame[4], int sample, int type);
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4]);
static int vj_perform_record_commit_single(veejay_t *info);
static void vj_perform_end_transition(veejay_t *info, int mode, int sample);

static void vj_perform_set_444__(VJFrame *frame)
{
    frame->ssm = 1;
    frame->shift_h = 0;
    frame->shift_v = 0;
    frame->uv_width = frame->width;
    frame->uv_height = frame->height;
    frame->uv_len = frame->len;
    frame->stride[1] = frame->stride[0];
    frame->stride[2] = frame->stride[0];
    frame->format = (frame->range ? PIX_FMT_YUVJ444P : PIX_FMT_YUV444P);
}

static void vj_perform_set_422__( VJFrame *frame)
{
    frame->shift_h = 1;
    frame->shift_v = 0;
    frame->uv_width = frame->width/2;
    frame->uv_height = frame->height;
    frame->uv_len = frame->uv_width * frame->uv_height;
    frame->stride[1] = frame->uv_width;
    frame->stride[2] = frame->stride[1];
    frame->format = (frame->range ? PIX_FMT_YUV422P : PIX_FMT_YUVJ422P);
    frame->ssm = 0;
}

#define vj_perform_set_444(f)  vj_perform_set_444__( f)
#define vj_perform_set_422(f)  vj_perform_set_422__( f)

static void vj_perform_sample_tick_reset(performer_global_t *g) {
    veejay_memset( g->played_sample_ids, 0 , sizeof(g->played_sample_ids));
    veejay_memset( g->played_sample_positions, 0, sizeof(g->played_sample_positions));
    veejay_memset( g->cached_frames, 0, sizeof(g->cached_frames));
    g->n_cached_frames = 0;
}

static long long vj_perform_sample_already_ticked(performer_global_t *g, int target_id, int chain_id) {
    for( int i = 0; i <= chain_id && i < SAMPLE_MAX_EFFECTS; i ++ ) {
        if(g->played_sample_ids[i]==target_id) {
            return g->played_sample_positions[i];
        }
    }
    return -1;
}

static void vj_perform_sample_ticked(performer_global_t *g, int target_id, int chain_id, long long pos) {
    g->played_sample_ids[chain_id] = target_id;
    g->played_sample_positions[chain_id] = pos;
}


static void vj_perform_supersample(video_playback_setup *settings,performer_t *p, VJFrame *one, VJFrame *two, int sm, int chain_entry)
{
    if( one != NULL ) {
        int no_matter = (sm == -1 ? 1: 0);
        if(!no_matter) {
            if( sm == 1 && one->ssm == 0) {
                chroma_supersample( settings->sample_mode,one,one->data );
                vj_perform_set_444(one);
                p->primary_buffer[0]->ssm = 1;
            }
            else if ( sm == 0 && one->ssm == 1) {
                chroma_subsample( settings->sample_mode,one,one->data);
                vj_perform_set_422(one);
                p->primary_buffer[0]->ssm = 1;
            }
        }
    }

    if( two != NULL ) {
        int no_matter = (sm == -1 ? 1: 0);
        if(!no_matter) {
            if( sm == 1 && two->ssm == 0) {
                chroma_supersample( settings->sample_mode,two,two->data );
                vj_perform_set_444(two);
                p->frame_buffer[ chain_entry ]->ssm = 1;
            }       
            else if ( sm == 0 && two->ssm == 1) {
                chroma_subsample( settings->sample_mode,two,two->data);
                vj_perform_set_422(two);
                p->frame_buffer[ chain_entry ]->ssm = 0;
            }
        }
		else if( p->frame_buffer[ chain_entry ]->ssm == 1 && two->ssm == 0 ) {
			chroma_supersample( settings->sample_mode, two, two->data );
			vj_perform_set_444(two);
			p->frame_buffer[ chain_entry ]->ssm = 1;
		}
    }
}

static  void    vj_perform_copy3( uint8_t **input, uint8_t **output, int Y_len, int UV_len, int alpha_len )
{
    int     strides[4] = { Y_len, UV_len, UV_len, alpha_len };
    vj_frame_copy(input,output,strides);
}

static inline void vj_copy_frame_holder(VJFrame *src, ycbcr_frame *data, VJFrame *dst) {
    int i;
    
    if(data != NULL) {
        dst->data[0] = data->Y;
        dst->data[1] = data->Cb;
        dst->data[2] = data->Cr;
        dst->data[3] = data->alpha;
    }
    
    dst->uv_len = src->uv_len;
    dst->len = src->len;
    dst->uv_width = src->uv_width;
    dst->uv_height = src->uv_height;
    dst->shift_v = src->shift_v;
    dst->shift_h = src->shift_h;
    dst->format = src->format;
    dst->width = src->width;
    dst->height = src->height;
    dst->ssm = src->ssm;

    for( i = 0; i < 4; i ++ ) {
        dst->stride[i] = src->stride[i];
    }

    dst->stand = src->stand;
    dst->fps = src->fps;
    dst->timecode = src->timecode;
}

#ifdef HAVE_JACK

int vj_perform_play_audio( video_playback_setup *settings, uint8_t *source, int len, uint8_t *silence )
{
    if( atomic_load_int(&settings->audio_mute) ) {
        return vj_jack_play( silence, len );
    } 
    else {

        return vj_jack_play( source, len );
    }

}
#endif

static ycbcr_frame *vj_perform_cache_get_frame(veejay_t *info,int id, int mode)
{
    int c;
    performer_global_t *g = (performer_global_t*) info->performer;    

    for(c=0; c < g->n_cached_frames; c++)
    {
        if(g->cached_frames[c].sample_id == id && g->cached_frames[c].mode == mode) 
        {
            if( info->settings->feedback && info->uc->sample_id == id &&
                        info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
                return NULL; //@ feedback cannot be cached source
            }
            return g->cached_frames[c].frame;
        }
    }

    return NULL;
}

static void vj_perform_cache_put_frame(performer_global_t *g, int id, int mode, ycbcr_frame *frame) {
    
    if( mode != VJ_PLAYBACK_MODE_SAMPLE && mode != VJ_PLAYBACK_MODE_TAG )
        return;

    for(int c=0; c < CACHE_SIZE; c++)
    {
        if( g->cached_frames[c].sample_id == 0 || ( g->cached_frames[c].sample_id == id && g->cached_frames[c].mode == mode) ) {
            g->cached_frames[c].sample_id = id;
            g->cached_frames[c].mode = mode;
            g->cached_frames[c].frame = frame;
            g->n_cached_frames ++;
            return;
        }
    }
}

static inline int vj_perform_cache_use_frame(ycbcr_frame *cached_frame, VJFrame *dst) {

    const int cached_in_ssm = cached_frame->ssm;
    const int len = (cached_in_ssm ? dst->len : dst->uv_len );

    veejay_memcpy( dst->data[0], cached_frame->Y, dst->len);
    veejay_memcpy( dst->data[1], cached_frame->Cb, len);
    veejay_memcpy( dst->data[2], cached_frame->Cr, len );

    //veejay_memcpy( dst->data[3], cached_frame->data[3], cached_frame->stride[3] * cached_frame->height );
    return cached_frame->ssm;
}

void vj_perform_initiate_edge_change(
    veejay_t *info,
    int edge_type,
    int prev_dir,
    int cur_dir
)
{
    if(info->audio == NO_AUDIO)
        return;

    performer_global_t *p = (performer_global_t*) info->performer;
    performer_t *A = p->A;
    performer_t *B = p->B;

    int real_direction_change =
        (prev_dir != 0 &&
         cur_dir  != 0 &&
         prev_dir != cur_dir);

    if (edge_type == AUDIO_EDGE_DIRECTION && !real_direction_change)
        return;

    atomic_store_int(&A->audio_edge->pending_edge, edge_type);
    atomic_store_int(&B->audio_edge->pending_edge, edge_type);

    /*veejay_msg(
        0,
        "EDGE CHANGE type=%d prev_dir=%d cur_dir=%d real_direction_change=%d",
        edge_type, prev_dir, cur_dir, real_direction_change
    ); */
}


int vj_perform_get_next_sequence_id(veejay_t *info, int *type, int current, int *new_current)
{
    int cur = current; // info->seq->current + 1;
    int cycle = 0;
    
    if( cur >= MAX_SEQUENCES )
            cur = 0;

    while( info->seq->samples[ cur ].sample_id == 0 )
    {
        cur ++;
        if( cur >= MAX_SEQUENCES && !cycle) {
            cur = 0;
            cycle = 1;
        }
        else if (cur >= MAX_SEQUENCES && cycle) {
            veejay_msg(VEEJAY_MSG_ERROR, "No valid sequence to play. Sequence Play disabled");
            info->seq->active = 0;
            return 0;
        }
    }

    *type = info->seq->samples[cur].type;
    *new_current = cur;

    return info->seq->samples[cur].sample_id;
}

void vj_perform_setup_transition(veejay_t *info, int next_sample_id, int next_type, int sample_id, int current_type, int next_seq_idx )
{
     video_playback_setup *settings = info->settings;
     int transition_active = ( current_type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_transition_active( sample_id ) : vj_tag_get_transition_active(sample_id));
    
     if( transition_active )
     {
        int transition_length = ( current_type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_transition_length( sample_id ) : vj_tag_get_transition_length( sample_id ) );
        int transition_shape = ( current_type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_transition_shape( sample_id ) : vj_tag_get_transition_shape( sample_id ) );
     
        if(transition_shape == -1) {
            transition_shape = ( (int) ( (double) shapewipe_get_num_shapes( settings->transition.ptr ) * rand() / (RAND_MAX)));
        }
    
        int speed = (current_type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_speed(sample_id) : 1 );
        int start = (current_type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_startFrame( sample_id) : 1 );
        int end = (current_type == VJ_PLAYBACK_MODE_SAMPLE ?  sample_get_endFrame(sample_id) : vj_tag_get_n_frames(sample_id));
     
        long long start_tx;
        long long end_tx;
        if( speed < 0 ) {
            start_tx = start + transition_length;
            end_tx = start;
        }
        else if(speed > 0 ) {
            start_tx = end - transition_length;
            end_tx = end;
        }

        settings->transition.shape = transition_shape;
        settings->transition.next_type = next_type;
        settings->transition.next_id = next_sample_id;
        settings->transition.ready = 0;
        settings->transition.seq_index = next_seq_idx;

        atomic_store_long_long(&settings->transition.start, start_tx);
        atomic_store_long_long(&settings->transition.end, end_tx);
        atomic_store_int(&settings->transition.active,transition_active );
    }
    else {

        vj_perform_reset_transition(info);
    }
}


int vj_perform_next_sequence( veejay_t *info, int *type, int *next_slot )
{
    int new_current = -1;
    int current_type = -1;
    int sample_id = vj_perform_get_next_sequence_id(info,&current_type, info->seq->current, &new_current);

    int next_current = 0;
    int next_sample_id = vj_perform_get_next_sequence_id(info,type, new_current + 1, &next_current );
 
    *next_slot = next_current;

    if( info->bezerk && current_type == 0 ) {
 	sample_set_resume_override( sample_id, -1 );
    }
  
    return next_sample_id;
}

int vj_perform_try_sequence( veejay_t *info )
{
    if(! info->seq->active )
        return 0;

    video_playback_setup *settings = info->settings;
    int id = info->uc->sample_id;

    int loops = sample_get_loops(id);
        
    // drive the sequencer 
    if( loops == 0 ) {

        int type = 0;
        int next_slot = 0;
        int n = vj_perform_next_sequence( info, &type, &next_slot );
        if( n > 0 )
        {
            // if not transitioning, we hard cut into the next sequence here
            if( atomic_load_int(&settings->transition.global_state) ) {
                info->seq->current = next_slot;
                veejay_change_playback_mode( info, type, n );
                return 0;
            }
        }
    }
         

    return 0;
}
static int vj_perform_record_buffer_init(veejay_t *info)
{
    performer_global_t *g = (performer_global_t*) info->performer;

    g->offline_frame = (VJFrame*) vj_calloc(sizeof(VJFrame));
    if(!g->offline_frame) return 0;

    veejay_memcpy( g->offline_frame, info->effect_frame1, sizeof(VJFrame) );

    uint8_t *region = (uint8_t*) vj_malloc(sizeof(uint8_t) * g->offline_frame->len * 3);
    if(!region) return 0;

    g->offline_frame->data[0] = region;
    g->offline_frame->data[1] = region + ( g->offline_frame->len );
    g->offline_frame->data[2] = region + ( g->offline_frame->len + g->offline_frame->uv_len );

    veejay_memset( g->offline_frame->data[0] , pixel_Y_lo_, g->offline_frame->len );
    veejay_memset( g->offline_frame->data[1], 128, g->offline_frame->uv_len );
    veejay_memset( g->offline_frame->data[2], 128, g->offline_frame->uv_len );

    g->offline_frame->data[3] = NULL;

    return 1;
}

static void vj_perform_record_buffer_free(performer_global_t *g)
{
    if(g->offline_frame) {
        if(g->offline_frame->data[0]) {
            free(g->offline_frame->data[0]);
            g->offline_frame->data[0] = NULL;
        }
        free(g->offline_frame);
        g->offline_frame = NULL;
    }

}

int vj_init_audio_fader_luts(veejay_t *info) {
    performer_global_t *global = (performer_global_t*) info->performer;
    
    const int audio_rate = info->current_edit_list->audio_rate;
    const double video_fps = info->current_edit_list->video_fps;

    const int max_samples_per_frame = (int)ceil((double)audio_rate / video_fps);
    size_t buffer_size = max_samples_per_frame * info->current_edit_list->audio_bps;

    global->fade_lut = (float*) vj_calloc( sizeof(float) * buffer_size);
    if(!global->fade_lut) {
        return 0;
    }
    global->gain_lut[0] = (float*) vj_calloc( (sizeof(float) * buffer_size));
    if(!global->gain_lut[0]) {
        return 0;
    }
    
    global->gain_lut[1] = (float*) vj_calloc( (sizeof(float) * buffer_size));
    if(!global->gain_lut[1]) {
        return 0;
    }
    
    for (int i = 0; i < max_samples_per_frame; i++) {
        float t = (float)i / (float)(max_samples_per_frame - 1); 

        global->gain_lut[0][i] = cosf(t * (M_PI / 2.0f)); 
        global->gain_lut[1][i] = sinf(t * (M_PI / 2.0f));

        global->fade_lut[i] = t; 
    }

    // alternating audio chunk size, 1471 , 1472 samples ...
    global->gain_lut[0][max_samples_per_frame - 1] = 0.0f;
    global->gain_lut[0][max_samples_per_frame - 2] = 0.0f;

    global->gain_lut[1][max_samples_per_frame - 1] = 1.0f;
    global->gain_lut[1][max_samples_per_frame - 2] = 1.0f;

    int mid = max_samples_per_frame / 2;
    int last = max_samples_per_frame - 1;

    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] Init Fader LUT | rate=%d Hz fps=%.3f samples/frame=%d bytes=%zu", audio_rate, video_fps, max_samples_per_frame, buffer_size);
    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] Fade curve: equal-power (cos/sin)");
    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] LUT[0]   : out=%.3f in=%.3f (start)", global->gain_lut[0][0], global->gain_lut[1][0]);
    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] LUT[%d]  : out=%.3f in=%.3f (mid)", mid, global->gain_lut[0][mid], global->gain_lut[1][mid]);
    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] LUT[%d]  : out=%.3f in=%.3f (end)", last, global->gain_lut[0][last], global->gain_lut[1][last]);              
    float energy_mid = global->gain_lut[0][mid] * global->gain_lut[0][mid] +
                    global->gain_lut[1][mid] * global->gain_lut[1][mid];

    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] Mid energy check: %.3f (should be ~1.000 for equal-power)", energy_mid);
    return 1;
}

// return 0 on success
int vj_perform_allocate(veejay_t *info)
{
    performer_global_t *global = (performer_global_t*) vj_calloc( sizeof(performer_global_t ));
    
    if(!global) {
        return 1;
    }

    info->performer = global;
    
    if(info->audio != NO_AUDIO) {
        if( vj_init_audio_fader_luts(info) == 0 ) {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to initialize audio mixer");
            return 1;
        }
    }

    global->preview_buffer = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
    if(!global->preview_buffer) {
        return 1;
    }
    global->preview_max_w = info->video_output_width * 2;
    global->preview_max_h = info->video_output_height * 2;
    global->preview_buffer->Y = (uint8_t*) vj_calloc( global->preview_max_w * global->preview_max_h * 2 );
    if(!global->preview_buffer->Y) {
        return 1;
    }

    const int w = info->video_output_width;
    const int h = info->video_output_height;
    const long frame_len = ((w*h)+w+w);
    size_t buf_len = frame_len * 4 * sizeof(uint8_t);

    sample_record_init(frame_len);
    vj_tag_record_init(w,h);


    global->feedback_buffer[0] = (uint8_t*) vj_malloc( buf_len );
    global->feedback_buffer[1] = global->feedback_buffer[0] + frame_len;
    global->feedback_buffer[2] = global->feedback_buffer[1] + frame_len;
    global->feedback_buffer[3] = global->feedback_buffer[2] + frame_len;

    veejay_memset( global->feedback_buffer[0], pixel_Y_lo_,frame_len);
    veejay_memset( global->feedback_buffer[1],128,frame_len);
    veejay_memset( global->feedback_buffer[2],128,frame_len);
    veejay_memset( global->feedback_buffer[3],0,frame_len);

    veejay_memcpy(&(global->feedback_frame), info->effect_frame1, sizeof(VJFrame));

    global->feedback_frame.data[0] = global->feedback_buffer[0];
    global->feedback_frame.data[1] = global->feedback_buffer[1];
    global->feedback_frame.data[2] = global->feedback_buffer[2];
    global->feedback_frame.data[3] = global->feedback_buffer[3];

    info->performer = (void*) global;
    return 0;
}

void vj_perform_destroy(veejay_t *info) {
    performer_global_t *global = (performer_global_t*) info->performer;
    free(global->preview_buffer->Y);
    free(global->preview_buffer);
    free(global);
}

static performer_t *vj_perform_init_performer(veejay_t *info, int chain_id)
{
    const int w = info->video_output_width;
    const int h = info->video_output_height;
   
    unsigned int c;
    long total_used = 0;

    performer_t *p = (performer_t*) vj_calloc(sizeof(performer_t));
    if(!p) {
        return NULL;
    }

    p->tmp1 = (VJFrame*) vj_calloc(sizeof(VJFrame));
    p->tmp2 = (VJFrame*) vj_calloc(sizeof(VJFrame));

    // buffer used to store encoded frames (for plain and sample mode)
    p->frame_buffer = (ycbcr_frame **) vj_calloc(sizeof(ycbcr_frame*) * SAMPLE_MAX_EFFECTS);
    if(!p->frame_buffer) {
        return NULL;
    }
 
    p->primary_buffer = (ycbcr_frame **) vj_calloc(sizeof(ycbcr_frame **) * PRIMARY_FRAMES); 
    if(!p->primary_buffer) {
        return NULL;
    }

    size_t plane_len = (w*h);
    size_t frame_len = 4 * plane_len;

    size_t performer_frame_size = frame_len * 4;
   
    p->pribuf_len = PRIMARY_FRAMES * performer_frame_size;
    p->pribuf_area = vj_hmalloc( p->pribuf_len, "in primary buffers" );
    if( !p->pribuf_area ) {
        return NULL;
    }
    
    for( c = 0; c < PRIMARY_FRAMES; c ++ )
    {
        p->primary_buffer[c] = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
        p->primary_buffer[c]->Y = p->pribuf_area + (performer_frame_size * c);
        p->primary_buffer[c]->Cb = p->primary_buffer[c]->Y  + frame_len;
        p->primary_buffer[c]->Cr = p->primary_buffer[c]->Cb + frame_len;
        p->primary_buffer[c]->alpha = p->primary_buffer[c]->Cr + frame_len;

        veejay_memset( p->primary_buffer[c]->Y, pixel_Y_lo_,frame_len);
        veejay_memset( p->primary_buffer[c]->Cb,128,frame_len);
        veejay_memset( p->primary_buffer[c]->Cr,128,frame_len);
        veejay_memset( p->primary_buffer[c]->alpha,0,frame_len);
        total_used += performer_frame_size;
    }

    p->temp_buffer[0] = (uint8_t*) vj_malloc( frame_len );
    if(!p->temp_buffer[0]) {
        return NULL;
    }
    p->temp_buffer[1] = p->temp_buffer[0] + plane_len;
    p->temp_buffer[2] = p->temp_buffer[1] + plane_len;
    p->temp_buffer[3] = p->temp_buffer[2] + plane_len;

    veejay_memset(p->temp_buffer[2], 128, plane_len);
    veejay_memset(p->temp_buffer[1], 128, plane_len);
    veejay_memset(p->temp_buffer[3], 0, plane_len);
    veejay_memset(p->temp_buffer[0], 0, plane_len);

    p->rgba_buffer[0] = (uint8_t*) vj_malloc( frame_len * 2 );
    if(!p->rgba_buffer[0] ) {
        return NULL;
    }

    p->rgba_buffer[1] = p->rgba_buffer[0] + frame_len;

    veejay_memset( p->rgba_buffer[0], 0, frame_len * 2 );

    p->subrender_buffer[0] = (uint8_t*) vj_malloc( frame_len ); //frame, p0, p1
    if(!p->subrender_buffer[0]) {
        return NULL;
    }
    p->subrender_buffer[1] = p->subrender_buffer[0] + plane_len;
    p->subrender_buffer[2] = p->subrender_buffer[1] + plane_len;
    p->subrender_buffer[3] = p->subrender_buffer[2] + plane_len;

    veejay_memset( p->subrender_buffer[0], pixel_Y_lo_,plane_len);
    veejay_memset( p->subrender_buffer[1],128,plane_len);
    veejay_memset( p->subrender_buffer[2],128,plane_len);
    veejay_memset( p->subrender_buffer[3],0,plane_len);

    total_used += frame_len; //temp_buffer
    total_used += frame_len; //subrender_buffer
    total_used += (frame_len * 2); //rgb conversion buffer

    size_t fx_chain_size = (frame_len + frame_len + frame_len) * SAMPLE_MAX_EFFECTS;
    p->fx_chain_buffer = vj_hmalloc( fx_chain_size, "in fx chain buffers" );
    if(p->fx_chain_buffer == NULL ) {
        veejay_msg(VEEJAY_MSG_WARNING,"Unable to allocate sufficient memory to keep all FX chain buffers in RAM");
        return NULL;
    }
    total_used += fx_chain_size;
    p->fx_chain_buflen = fx_chain_size;
        
    /* set up pointers for frame_buffer */
    for (c = 0; c < SAMPLE_MAX_EFFECTS; c++) {
        p->frame_buffer[c] = (ycbcr_frame *) vj_calloc(sizeof(ycbcr_frame));
         if(!p->frame_buffer[c]) {
             return NULL;
         }

         const int space = frame_len * 3;
         uint8_t *ptr = p->fx_chain_buffer + (c * space);
         p->frame_buffer[c]->Y = ptr;
         p->frame_buffer[c]->Cb = p->frame_buffer[c]->Y + plane_len;
         p->frame_buffer[c]->Cr = p->frame_buffer[c]->Cb + plane_len;
         p->frame_buffer[c]->alpha = p->frame_buffer[c]->Cr + plane_len;

         p->frame_buffer[c]->P0  = ptr + frame_len;
         p->frame_buffer[c]->P1  = p->frame_buffer[c]->P0 + frame_len;

         veejay_memset( p->frame_buffer[c]->Y, pixel_Y_lo_,plane_len);
         veejay_memset( p->frame_buffer[c]->Cb,128,plane_len);
         veejay_memset( p->frame_buffer[c]->Cr,128,plane_len);
         veejay_memset( p->frame_buffer[c]->alpha,0,plane_len);
         veejay_memset( p->frame_buffer[c]->P0, pixel_Y_lo_, plane_len );
         veejay_memset( p->frame_buffer[c]->P0 + plane_len, 128, plane_len * 2);
         veejay_memset( p->frame_buffer[c]->P1, pixel_Y_lo_, plane_len );
         veejay_memset( p->frame_buffer[c]->P1 + plane_len, 128, plane_len * 2);
     }

    veejay_memset( &(p->pvar_), 0, sizeof( varcache_t));

    sws_template templ;
    veejay_memset(&templ,0,sizeof(sws_template));
    templ.flags = yuv_which_scaler();

    p->rgba_frame[0] = yuv_rgb_template( p->rgba_buffer[0], w, h, PIX_FMT_RGBA );
    p->rgba_frame[1] = yuv_rgb_template( p->rgba_buffer[1], w, h, PIX_FMT_RGBA );
    p->yuva_frame[0] = yuv_yuv_template( NULL, NULL, NULL, w,h,PIX_FMT_YUVA444P );
    p->yuva_frame[1] = yuv_yuv_template( NULL, NULL, NULL, w,h,PIX_FMT_YUVA444P );
    p->yuv420_frame[0] = yuv_yuv_template( NULL, NULL, NULL, w, h, PIX_FMT_YUV420P );

    p->yuv420_scaler =  yuv_init_swscaler( p->yuva_frame[0], p->yuv420_frame[0], &templ, yuv_sws_get_cpu_flags() );
    if(p->yuv420_scaler == NULL )
        return NULL;

    p->yuv2rgba_scaler = yuv_init_swscaler( p->yuva_frame[0], p->rgba_frame[0], &templ, yuv_sws_get_cpu_flags() );
    if(p->yuv2rgba_scaler == NULL )
        return NULL;

    p->rgba2yuv_scaler = yuv_init_swscaler( p->rgba_frame[1], p->yuva_frame[0], &templ, yuv_sws_get_cpu_flags());
    if(p->rgba2yuv_scaler == NULL )
        return NULL;

    p->rgba_frame[0]->data[0] = p->rgba_buffer[0];
    p->rgba_frame[1]->data[0] = p->rgba_buffer[1];

    veejay_msg(VEEJAY_MSG_INFO,
        "[PRODUCER] Using %.2f MB RAM, %.2f MB RAM pre-allocated for FX",
            ((float)total_used/1048576.0f),
            ((float)fx_chain_size/1048576.0f)
        ); 

    p->chain_id = chain_id;

    return p;
}

int vj_perform_init(veejay_t * info)
{
    int res = vj_perform_allocate( info );
    if( res != 0 ) {
        veejay_msg(0, "Failed to initialize performer.");
        return 0;
    }

    performer_global_t *global = (performer_global_t*) info->performer;

    chroma_subsample_init();
    chroma_supersample_init();

    global->A = vj_perform_init_performer(info,0);
    global->B = vj_perform_init_performer(info,1);

    if( info->uc->scene_detection ) {
        vj_el_auto_detect_scenes( info->edit_list, global->A->temp_buffer,info->video_output_width,info->video_output_height, info->uc->scene_detection );
    }

    return 1;
}

static void vj_perform_close_audio(performer_t *p) {
    if (!p) return;

    if (p->lin_audio_buffer_)
        free(p->lin_audio_buffer_);
    p->lin_audio_buffer_ = NULL;

    if (p->audio_silence_)
        free(p->audio_silence_);
    p->audio_silence_ = NULL;

    veejay_memset(p->audio_buffer, 0, sizeof(uint8_t*) * SAMPLE_MAX_EFFECTS);

#ifdef HAVE_JACK
    if (p->top_audio_buffer) {
        free(p->top_audio_buffer);
        p->top_audio_buffer = NULL;
    }
    if (p->audio_rec_buffer) {
        free(p->audio_rec_buffer);
        p->audio_rec_buffer = NULL;
    }
    if (p->audio_render_buffer) {
        free(p->audio_render_buffer);
        p->audio_render_buffer = NULL;
    }

    if (p->audio_chain_final_buffer) {
        free(p->audio_chain_final_buffer);
        p->audio_chain_final_buffer = NULL;
    }
    
    if (p->down_sample_buffer) {
        free(p->down_sample_buffer);
        p->down_sample_buffer = NULL;
        p->down_sample_rec_buffer = NULL;
    }
    if( p->audio_downmix_buffer) {
        free(p->audio_downmix_buffer);
        p->audio_downmix_buffer = NULL;
    }

    if (p->audio_edge) {
        audio_edge_t *edge = p->audio_edge;
        if (edge->fwdL) free(edge->fwdL);
        if (edge->fwdR) free(edge->fwdR);
        if (edge->silenceL) free(edge->silenceL);
        if (edge->silenceR) free(edge->silenceR);
        if (edge->fade_lut) free(edge->fade_lut);
        free(edge);
        p->audio_edge = NULL;
    }

#endif

}

int init_audio_resampler(veejay_t *info, performer_t *p) {
#ifdef HAVE_JACK
    const int chans = info->edit_list->audio_chans;
    const int rate  = info->edit_list->audio_rate;

    p->audio_scratcher = vj_scratch_init( chans, rate, info->edit_list->video_fps );
#endif
    return 1;
}


static void vj_audio_chain_init(performer_global_t *g, performer_t *p, const uint32_t sample_len) {
    g->audio_chain_index = 0;
    for (int i = 0; i < 2; i++) {
        veejay_memset(&g->audio_chain_buffers[i], 0, sizeof(audio_chain_buffer_t));
        g->audio_chain_buffers[i].state = AC_STATE_IDLE;
        g->audio_chain_buffers[i].count = 0;
        g->accum[i] = (float*) vj_calloc(sizeof(float) * sample_len);
        
        for (int j = 0; j < SAMPLE_MAX_EFFECTS; j++) {
            g->audio_chain_buffers[i].entries[j].state = AC_STATE_IDLE;
            g->audio_chain_buffers[i].entries[j].buffer = p->audio_downmix_buffer + ( sample_len * i );
            g->audio_chain_buffers[i].entries[j].buffer_size = sample_len;
        }
    }
    
    g->audio_chain_buffers[0].state = AC_STATE_CONSUMING;
}

int vj_perform_init_audio(veejay_t * info, int AorB)
{
#ifndef HAVE_JACK
    veejay_msg(VEEJAY_MSG_DEBUG, "Jack was not enabled during build, no support for audio");
#else

    if(info->audio == NO_AUDIO) {
        veejay_msg(VEEJAY_MSG_DEBUG, "Skipping Audio initialization");
        return 1;
    }

    performer_global_t *global = (performer_global_t*) info->performer;
    editlist *el = info->edit_list;
    performer_t *p = (AorB ? global->A : global->B);
    double samples_per_frame = (double)el->audio_rate / (double)el->video_fps;
    const uint32_t sample_len = ceil(samples_per_frame) * el->audio_bps;

    p->top_audio_buffer =
        (uint8_t *) vj_calloc(sizeof(uint8_t) * 8 * PERFORM_AUDIO_SIZE);
    if(!p->top_audio_buffer)
        return 0;

    p->audio_rec_buffer =
        (uint8_t *) vj_calloc(sizeof(uint8_t) * 8 * PERFORM_AUDIO_SIZE );
    if(!p->audio_rec_buffer)
        return 0;

    p->down_sample_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * (sample_len * MAX_SPEED_AV) + 1024 + 128 );
    if(!p->down_sample_buffer)
        return 0;
    p->down_sample_rec_buffer = p->down_sample_buffer + (sizeof(uint8_t) * sample_len * MAX_SPEED_AV );

    p->audio_render_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * sample_len * SAMPLE_MAX_EFFECTS );
    if(!p->audio_render_buffer)
        return 0;
    p->audio_render_buffer_capacity = sample_len * SAMPLE_MAX_EFFECTS;

    p->audio_chain_final_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * sample_len ); //FIXME delete?
    if(!p->audio_chain_final_buffer)
        return 0;

    p->audio_downmix_buffer =  (uint8_t *) vj_calloc(sizeof(uint8_t) * sample_len * SAMPLE_MAX_EFFECTS ); // FIXME unused
    if(!p->audio_downmix_buffer)
        return 0;

    /*if(AorB == 0) {
       vj_audio_chain_init(g, p, sample_len);
    }*/   
    
    p->lin_audio_buffer_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * sample_len * SAMPLE_MAX_EFFECTS );
    if(!p->lin_audio_buffer_)
        return 0;

    for (int i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
        p->audio_buffer[i] = p->lin_audio_buffer_ + (sample_len * i);
    }
 
    p->audio_silence_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * sample_len );
    if(!p->audio_silence_)
        return 0;
    
    audio_edge_t *edge = (audio_edge_t*) vj_calloc( sizeof(audio_edge_t) );
    if(!edge) {
        return 0;
    }

    edge->buflen = (sample_len * MAX_SPEED_AV * sizeof(int16_t));

    edge->fwdL = (int16_t*) vj_calloc( sizeof(int16_t) * edge->buflen);
    edge->fwdR = (int16_t*) vj_calloc( sizeof(int16_t) * edge->buflen); // FIXME remove ?
    edge->silenceL = (int16_t*) vj_calloc( sizeof(int16_t) * edge->buflen);
    edge->silenceR = (int16_t*) vj_calloc( sizeof(int16_t) * edge->buflen);

    edge->history = (int16_t*) vj_calloc( sizeof(int16_t) * edge->buflen * el->audio_chans);
        
    edge->fade_lut = (float*) vj_calloc( sizeof(float) * 257);
    for (int i = 0; i <= FADE_LUT_SIZE; i++) {
        // Store sine values from 0 to PI/2
        edge->fade_lut[i] = sinf(((float)i / FADE_LUT_SIZE) * (1.57079632679f));
    }

    p->audio_edge = edge;

    
    return init_audio_resampler(info, p );
    
#endif
    return 0;
}

static void vj_perform_free_performer(performer_t *p)
{
    int c;
    if(p->frame_buffer) {
        for(c = 0; c < SAMPLE_MAX_EFFECTS; c ++ )
        {
            if(p->frame_buffer[c]) {
                free(p->frame_buffer[c]);
                p->frame_buffer[c] = NULL;
            }
        }
        free(p->frame_buffer);
        p->frame_buffer = NULL;
    }

    if(p->primary_buffer){
        for( c = 0;c < PRIMARY_FRAMES; c++ )
        {
            free(p->primary_buffer[c] );
            p->primary_buffer[c] = NULL;
        }
        free(p->primary_buffer);
        p->primary_buffer = NULL;
    }


   if(p->temp_buffer[0]) {
       free(p->temp_buffer[0]);
       p->temp_buffer[0] = NULL;
   }
   if(p->subrender_buffer[0]) {
       free(p->subrender_buffer[0]);
       p->subrender_buffer[0] = NULL;
   }

   if(p->rgba_buffer[0]) {
       free(p->rgba_buffer[0]);
       p->rgba_buffer[0] = NULL;
   }
    
   if(p->fx_chain_buffer)
   {
        munlock(p->fx_chain_buffer, p->fx_chain_buflen);
        free(p->fx_chain_buffer);
   }
   
   if(p->pribuf_area)
   {
       munlock(p->pribuf_area, p->pribuf_len);
       free(p->pribuf_area);
   }

   yuv_free_swscaler( p->rgba2yuv_scaler );
   yuv_free_swscaler( p->yuv2rgba_scaler );
   yuv_free_swscaler( p->yuv420_scaler );

   free(p->rgba_frame[0]);
   free(p->rgba_frame[1]);

}

void vj_perform_free(veejay_t * info)
{
    performer_global_t *global = (performer_global_t*)info->performer;
    if( global == NULL ) {
        return;
    }       

    munlockall();

    sample_record_free();

    if (global->preview_buffer){
        if(global->preview_buffer->Y)
            free(global->preview_buffer->Y);
        free(global->preview_buffer);
    }
    if(global->feedback_buffer[0]) {
       free(global->feedback_buffer[0]);
       global->feedback_buffer[0] = NULL;
    }

    if( global->A )
        vj_perform_free_performer( global->A );
    if( global->B )
        vj_perform_free_performer( global->B );

    if(info->edit_list && info->edit_list->has_audio) {
        vj_perform_close_audio(global->A);
        vj_perform_close_audio(global->B);
    }

    vj_perform_record_buffer_free(global);

    vj_avcodec_stop(global->encoder_,0);

    free(global);
}

int vj_perform_preview_max_width(veejay_t *info) {
    performer_global_t *global = (performer_global_t*)info->performer;
    return global->preview_max_w;
}

int vj_perform_preview_max_height(veejay_t *info) {
    performer_global_t *global = (performer_global_t*)info->performer;
    return global->preview_max_h;
}

int vj_perform_audio_start(veejay_t * info)
{
    editlist *el = info->edit_list; 

    if (el->has_audio)
    {
#ifdef HAVE_JACK
        vj_jack_initialize();
        int res = vj_jack_init(el);
        if( res <= 0 ) {    
            veejay_msg(0, "[AUDIO] Audio playback disabled");
            info->audio = NO_AUDIO;
            return 0;
        }

        if ( res == 2 )
        {
            vj_jack_stop();
            info->audio = NO_AUDIO;
            veejay_msg(VEEJAY_MSG_ERROR,"Please run jackd with a sample rate of %ld",el->audio_rate );
            return 0;
        }

        veejay_msg(VEEJAY_MSG_DEBUG,"[AUDIO] Jack audio playback started");
        return 1;
#else
        veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Jack support not compiled in (no audio)");
        return 0;
#endif
    }
    return 0;
}

void vj_perform_audio_stop(veejay_t * info)
{
    if (info->edit_list->has_audio) {
#ifdef HAVE_JACK
        vj_jack_stop();
#endif
        info->audio = NO_AUDIO;
    }
}

void vj_perform_get_primary_frame(veejay_t * info, uint8_t **frame)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
    frame[0] = p->primary_buffer[info->out_buf]->Y;
    frame[1] = p->primary_buffer[info->out_buf]->Cb;
    frame[2] = p->primary_buffer[info->out_buf]->Cr;
    frame[3] = p->primary_buffer[info->out_buf]->alpha;
}

uint8_t *vj_perform_get_preview_buffer(veejay_t *info)
{
    performer_global_t *global = (performer_global_t*)info->performer;
    return global->preview_buffer->Y;
}

void    vj_perform_get_crop_dimensions(veejay_t *info, int *w, int *h)
{
    *w = info->video_output_width - info->settings->viewport.left - info->settings->viewport.right;
    *h = info->video_output_height - info->settings->viewport.top - info->settings->viewport.bottom;
}

static long stream_pts_ = 0;
static int vj_perform_compress_primary_frame_s2(veejay_t *info,VJFrame *frame )
{
    performer_global_t *g = (performer_global_t*) info->performer;
    if( g->encoder_ == NULL ) {
        g->encoder_ = vj_avcodec_start(info->effect_frame1, ENCODER_MJPEG, NULL);
        if(g->encoder_ == NULL) {
            return 0;
        }
    }

    return vj_avcodec_encode_frame(g->encoder_,
            stream_pts_ ++,
            ENCODER_MJPEG,
            frame->data, 
            vj_avcodec_get_buf(g->encoder_), 
            vj_avcodec_get_buf_size(g->encoder_),
            frame->format);
}

void    vj_perform_send_primary_frame_s2(veejay_t *info, int mcast, int to_mcast_link_id, VJFrame *display_frame)
{
    int i;
    performer_global_t *g = (performer_global_t*) info->performer;

    if( info->splitter ) {
        for ( i = 0; i < VJ_MAX_CONNECTIONS ; i ++ ) {
            if( info->rlinks[i] >= 0 ) {
                int link_id = info->rlinks[i];
                if( link_id == -1 )
                    continue;

                int screen_id = info->splitted_screens[ i ];
                if( screen_id < 0 )
                    continue;

                VJFrame *frame = vj_split_get_screen( info->splitter, screen_id );
                int data_len = vj_perform_compress_primary_frame_s2( info, frame );
                if(data_len <= 0)
                    continue;

                if( vj_server_send_frame( info->vjs[3], link_id, vj_avcodec_get_buf(g->encoder_),data_len, frame ) <= 0 ) {
                    _vj_server_del_client( info->vjs[3], link_id );
                }

                info->rlinks[i] = -1;
                info->splitted_screens[i] = -1;
            }
        }

        atomic_store_int(&info->settings->unicast_frame_sender, 0);
    }
    else {

        int data_len = vj_perform_compress_primary_frame_s2( info, display_frame );
        if( data_len <= 0) {
            return;
        }

        int id = (mcast ? 2: 3);

        if(!mcast) 
        {
            for( i = 0; i < VJ_MAX_CONNECTIONS; i++ ) {
                if( info->rlinks[i] != -1 ) {
                    if(vj_server_send_frame( info->vjs[id], info->rlinks[i], vj_avcodec_get_buf(g->encoder_), data_len, display_frame )<=0)
                    {
                            _vj_server_del_client( info->vjs[id], info->rlinks[i] );
                    }
                    info->rlinks[i] = -1;
                }   
            }

            atomic_store_int(&info->settings->unicast_frame_sender ,0);
        }
        else
        {       
            if(vj_server_send_frame( info->vjs[id], to_mcast_link_id, vj_avcodec_get_buf(g->encoder_), data_len, display_frame )<=0)
            {
                veejay_msg(VEEJAY_MSG_DEBUG,  "Error sending multicast frame");
            }
        }
    }
}

void vj_perform_get_primary_frame_420p(veejay_t *info, uint8_t **frame )   
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    p->yuv420_frame[0]->data[0] = frame[0] = p->temp_buffer[0];
    p->yuv420_frame[0]->data[1] = frame[1] = p->temp_buffer[1];
    p->yuv420_frame[0]->data[2] = frame[2] = p->temp_buffer[2];
    p->yuv420_frame[0]->data[3] = frame[3] = p->temp_buffer[3];
    
    VJFrame pframe;
    memcpy(&pframe, info->effect_frame1, sizeof(VJFrame));
    pframe.data[0] = p->primary_buffer[info->out_buf]->Y;
    pframe.data[1] = p->primary_buffer[info->out_buf]->Cb;
    pframe.data[2] = p->primary_buffer[info->out_buf]->Cr;
    pframe.stride[0] = p->yuv420_frame[0]->stride[0];
    pframe.stride[1] = p->yuv420_frame[0]->stride[0] >> 1;
    pframe.stride[2] = p->yuv420_frame[0]->stride[1];
    pframe.stride[3] = 0;

    yuv_convert_and_scale( p->yuv420_scaler, &pframe, p->yuv420_frame[0] );
}

static void vj_perform_apply_first(veejay_t *info,performer_t *p, vjp_kf *todo_info,
    VJFrame **frames, sample_eff_chain *entry, int e , int c, long long n_frame, void *ptr, int playmode) 
{
    int n_params = 0;
    int is_mixer = 0;
    int rgb = 0;

    if(entry == NULL || !vje_get_info( e, &is_mixer, &n_params, &rgb)) {
        return;
    }

    if(n_params > SAMPLE_MAX_PARAMETERS) {
        veejay_msg(VEEJAY_MSG_WARNING, "FX %d has more than %d parameters", SAMPLE_MAX_PARAMETERS);
        n_params = SAMPLE_MAX_PARAMETERS;
    }

    int args[SAMPLE_MAX_PARAMETERS];
    veejay_memset(args,0,sizeof(args));

    if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
    {
        if(!vj_tag_get_all_effect_args(entry, args, n_params, (int) n_frame))
            return;
    }
    else
    {
        if(!sample_get_all_effect_arg(entry, args, n_params, (int) n_frame))
            return;
    }
    
    if( rgb ) {
        p->yuva_frame[0]->data[0] = frames[0]->data[0];
        p->yuva_frame[0]->data[1] = frames[0]->data[1];
        p->yuva_frame[0]->data[2] = frames[0]->data[2];
        p->yuva_frame[0]->data[3] = frames[0]->data[3];

        yuv_convert_and_scale_rgb( p->yuv2rgba_scaler, p->yuva_frame[0], p->rgba_frame[0] );
        if(is_mixer) {
            p->yuva_frame[1]->data[0] = frames[1]->data[0];
            p->yuva_frame[1]->data[1] = frames[1]->data[1];
            p->yuva_frame[1]->data[2] = frames[1]->data[2];
            p->yuva_frame[1]->data[3] = frames[1]->data[3];

            yuv_convert_and_scale_rgb( p->yuv2rgba_scaler, p->yuva_frame[1], p->rgba_frame[1] ); 
        }

        vjert_apply( entry, p->rgba_frame, p->chain_id,c, args );
    
        yuv_convert_and_scale_from_rgb( p->rgba2yuv_scaler, p->rgba_frame[0],p->yuva_frame[0] );
    }
    else {
        vjert_apply( entry, frames, p->chain_id, c, args );
    }

}

static long vj_calc_next_sample_offset(
    sample_b_t *sb,
    int *advance_out
) {
    long start = sb->start;
    long end = sb->end;
    int speed = sb->speed;
    int loop_mode  = sb->loopmode;
    const long len = end - start;
    int advance = 1;

    if (sb->direction_changed) {
        sb->cur_sfd = 0;
        sb->direction_changed = 0;
    }

    if (sb->max_sfd > 0) {
        sb->cur_sfd++;

        if (sb->cur_sfd < sb->max_sfd) {
            advance = 0;
        } else {
            sb->cur_sfd = 0;
            advance = 1;
        }
    }


    *advance_out = advance;

    long off = sb->offset;

    if (advance && loop_mode == 3) {
        off = (long)((double)len * rand() / RAND_MAX);
        sb->offset = off;
        return off;
    }

    if (advance) {
        const long step = llabs(speed);
        off += step * sb->direction;

        if (off > len) {
            if (loop_mode == 2) {
                off = len;
                sb->direction = -1;
            } else if (loop_mode == 1) {
                off = 0;
            } else {
                off = len;
            }
        } else if (off < 0) {
            if (loop_mode == 2) {
                off = 0;
                sb->direction = +1;
            } else if (loop_mode == 1) {
                off = len;
            } else {
                off = 0;
            }
        }

        sb->offset = off;
    }

    return off;
}

long vj_calc_next_sub_audioframe(veejay_t *info, int b, audio_chain_entry_t *audio_entry) {
    sample_b_t sb;

    sb.start = audio_entry->start;
    sb.end = audio_entry->end;
    sb.speed = audio_entry->speed;
    sb.offset = audio_entry->offset;
    sb.direction = (sb.speed < 0 ? -1: 1);
    sb.loopmode = audio_entry->loopmode;
    sb.cur_sfd = audio_entry->cur_sfd;
    sb.max_sfd = audio_entry->max_sfd;
   
    int advance = 0;
    long off = vj_calc_next_sample_offset(
        &sb,
        &advance
    );

    return sb.start + off;
}

long vj_calc_next_subframe(veejay_t *info, int b)
{
    performer_global_t *g = info->performer;
    performer_t *perf = g->A;
    sample_b_t *sb = &perf->sample_b;

    int sample_i[6];

    if(sample_get_long_info(b,&sample_i[0],&sample_i[1],&sample_i[2],&sample_i[3],&sample_i[4], &sample_i[5])!=0) return -1;

    const long start = sample_i[0];
    const long end   = sample_i[1];

    sb->offset  = atomic_load_long_long(&sb->offset);
    sb->cur_sfd = sample_i[4];
    sb->max_sfd = sample_i[5];
    sb->speed   = sample_i[3];
    sb->start   = start;
    sb->end     = end;
    sb->loopmode= sample_i[2];

    if (sb->direction == 0) {
        int dir = (sb->speed < 0) ? -1 : +1;
        sb->direction_changed = (dir != sb->direction);
        sb->direction = dir;
    }

    int advance = 0;

    long off = vj_calc_next_sample_offset(
        sb,
        &advance
    );

    atomic_store_long_long(&sb->offset, off);
    atomic_store_long_long(&sb->start, start);

    sample_set_framedups(b, sb->cur_sfd);
/*
    veejay_msg(
        VEEJAY_MSG_DEBUG,
        "[AUDIO] pos=%lld off=%ld/%ld dir=%d speed=%d sfd=%d/%d adv=%d direction=%d",
        (long long)(start + off),
        off, (end - start),
        sb->direction,
        sb->speed,
        sb->cur_sfd, sb->max_sfd,
        advance,
        sb->direction
    );
 */
    return start + off;
}

#ifdef HAVE_JACK
static int get_audio_frame_safe(
    veejay_t *info,
    editlist *el,
    long long frame,
    uint8_t *audio_buf,
    int pred_len,
    int bps,
    int speed
) {
    int n_samples = vj_el_get_audio_frame(el, frame, audio_buf);
    if (n_samples <= 0) {
        veejay_memset(audio_buf, 0, pred_len * bps);
        n_samples = pred_len;
        veejay_msg(0, "[AUDIO] Error fetching frame %lld — zeroed %d samples",
                   frame, pred_len);
    } else if (n_samples < pred_len) {
        veejay_memset(audio_buf + n_samples * bps, 0,
                      (pred_len - n_samples) * bps);
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO] Frame %lld fetched %d samples, zero-filled %d remaining",
                   frame, n_samples, pred_len - n_samples);
    }

    return n_samples;
}

// not necessary any more via audioscratcher, FIXME
static void reverse_audio_buffer(uint8_t *buf, int n_samples, int bps) 
{
    if (n_samples <= 1 || bps <= 0) return;

    int i = 0;
    int j = n_samples - 1;

    switch (bps) {
        case 2: { // 16-bit
            uint16_t *p = (uint16_t *)buf;
            while (i < j) {
                uint16_t tmp = p[i]; 
                p[i] = p[j]; 
                p[j] = tmp; 
                i++; j--;
            }
            break;
        }
        case 4: { // 32-bit (or 16-bit stereo)
            uint32_t *p = (uint32_t *)buf;
            while (i < j) {
                uint32_t tmp = p[i]; 
                p[i] = p[j]; 
                p[j] = tmp; 
                i++; j--;
            }
            break;
        }
        case 8: { // 64-bit (or 32-bit stereo)
            uint64_t *p = (uint64_t *)buf;
            while (i < j) {
                uint64_t tmp = p[i]; 
                p[i] = p[j]; 
                p[j] = tmp; 
                i++; j--;
            }
            break;
        }
        default: { // generic

            uint8_t tmp[16]; 
            if (bps > 16) return;
            
            while (i < j) {
                uint8_t *a = buf + (i * bps);
                uint8_t *b = buf + (j * bps);
                
                memcpy(tmp, a, bps);
                memcpy(a, b, bps);
                memcpy(b, tmp, bps);
                
                i++; j--;
            }
            break;
        }
    }
}

static int perform_normal_playback(
    veejay_t *info,
    editlist *el,
    performer_t *p,
    uint8_t *audio_buf,
    uint8_t *temporary_buffer,
    int pred_len,
    int sample_size,
    long long cur_frame,
    sample_b_t *sample_ptr
) {
    const int speed = sample_ptr->speed;
    const int bps = el->audio_bps;
    const int chans = el->audio_chans;
    audio_edge_t *edge = p->audio_edge;
    int total_samples = 0;

    int last_dir = atomic_load_int(&edge->last_direction);
    int cur_dir = (speed > 0 ? 1 : speed < 0 ? -1 : 0);
    int pending_edge = atomic_load_int(&edge->pending_edge);
    int sc = av_clip(abs(speed) - 2, 0, MAX_SPEED_AV - 1);
    int direction_flipped = (last_dir != 0 && cur_dir != last_dir);
    
    if(pending_edge == AUDIO_EDGE_RESET ) { // FIXME
       
    }

    if (abs(speed) > 1) {

        int n_frames = abs(speed);
        if (n_frames >= MAX_SPEED)
            n_frames = MAX_SPEED - 1;

        uint8_t *tmp_ptr = temporary_buffer;
        total_samples = 0;

        for (int i = 0; i < n_frames; i++) {
            long long f;
            if (speed < 0) {
                f = cur_frame - (n_frames - 1 - i);
            } else {
                f = cur_frame + i;
            }

            int a_len = vj_el_get_audio_frame(el, f, tmp_ptr);
            int valid_len = (a_len <= 0) ? pred_len : a_len;
            if (a_len <= 0) memset(tmp_ptr, 0, valid_len * bps);

            total_samples += valid_len;
            tmp_ptr += valid_len * bps;
        }

        if (speed < 0 && total_samples > 1)
            reverse_audio_buffer(temporary_buffer, total_samples, bps);

        int max_dst_samples = p->audio_render_buffer_capacity;
        int consumed = 0;

        total_samples = vj_scratch_process(p->audio_scratcher, (short*) audio_buf,max_dst_samples, (short*) temporary_buffer, total_samples, (float) speed * cur_dir);

    } else {
        int a_len = vj_el_get_audio_frame(el, cur_frame, audio_buf);
        int valid_len = (a_len <= 0) ? pred_len : a_len;
        if (a_len <= 0) memset(audio_buf, 0, valid_len * bps);
        total_samples = valid_len;

        if (speed < 0)
            reverse_audio_buffer(audio_buf, total_samples, bps);
    }

    int16_t *samples = (int16_t *)audio_buf;

    int fwd_v = atomic_load_int(&edge->fwd_history_valid);
    int rev_v = atomic_load_int(&edge->rev_history_valid);
    int history_valid = (cur_dir > 0) ? fwd_v : rev_v;
    

    if (pending_edge != AUDIO_EDGE_NONE || direction_flipped) {
        // FIXME handle audio edge or not
    }

    atomic_store_int(&edge->last_direction, cur_dir); // normal_playback
    atomic_store_int(&edge->pending_edge, AUDIO_EDGE_NONE);
    sample_ptr->direction_changed = 0;

    return total_samples;
}

static void perform_slow_motion_reset_edge(
    performer_t *p,
    sample_b_t *posdata,
    int sc,
    int edge_type,
    int cur_dir
) {
    
    //FIXME reset stuffs
}

static int perform_slow_motion_fetch_resample(
    veejay_t *info,
    editlist *el,
    performer_t *p,
    uint8_t *audio_buf,
    uint8_t *downsample_buffer,
    int bps,
    int pred_len,
    int sc1,
    int direction_flipped,
    int speed_int, 
    sample_b_t *posdata,
    long long target_frame,
    int direction,
    int is_flip
) {
    int sc = sc1 + 2;
    float target_speed = 1.0f / (float)sc;

    if (target_speed < 0.01f) target_speed = 0.01f; 

    int audio_samples = get_audio_frame_safe(info, el, target_frame, audio_buf, pred_len, bps, speed_int);

    if (audio_samples > 0) {
        int consumed = 0;
        int max_dst_samples = (512 * 1024) / (el->audio_chans * sizeof(short));

        posdata->audio_last_stretched_samples = vj_scratch_process(p->audio_scratcher, (short*) downsample_buffer,max_dst_samples, (short*) audio_buf, audio_samples, 1.0f / (float) sc * direction);

    } else {
        posdata->audio_last_stretched_samples = 0;
    }

    return posdata->audio_last_stretched_samples;
}

static inline int copy_slow_motion_slice(
    uint8_t *audio_buf,
    uint8_t *slice_src,
    int frame_bytes,
    int slice_len
) {
    if (slice_len <= 0)
        return 0;

    veejay_memcpy(audio_buf, slice_src, slice_len * frame_bytes);

    return slice_len;
}

int perform_slow_motion(
    veejay_t *info,
    editlist *el,
    performer_t *p,
    uint8_t *audio_buf,
    uint8_t *downsample_buffer,
    int *sampled_down,
    long long target_frame,
    sample_b_t *posdata
) {
    const int frame_bytes = el->audio_bps;
    const int pred_len = el->audio_rate / el->video_fps;
    const int chans = el->audio_chans;
    audio_edge_t *edge = p->audio_edge;

    int cur_dir = (posdata->speed > 0 ? 1 : (posdata->speed < 0 ? -1 : 0));

    if (cur_dir == 0) {
        memset(audio_buf, 0, pred_len * frame_bytes);
        return pred_len;
    }

    int last_dir = atomic_load_int(&edge->last_direction);
    int direction_flipped = (last_dir != 0 && cur_dir != last_dir);

    if (direction_flipped && posdata->cur_sfd == 0) {
        posdata->audio_last_stretched_samples = 0;
        posdata->consumed_samples = 0;
        atomic_store_int(&edge->last_direction, cur_dir);
    }

    int sc = av_clip(posdata->max_sfd - 2, 0, MAX_SPEED_AV - 1);

    int pending_edge = atomic_load_int(&edge->pending_edge);

    if (pending_edge == AUDIO_EDGE_RESET) {
        perform_slow_motion_reset_edge(p, posdata, sc, pending_edge, cur_dir);
    }

    if (posdata->cur_sfd == 0 && (posdata->last_resampled_frame != target_frame || direction_flipped)) {
        perform_slow_motion_fetch_resample(
            info, el, p, audio_buf, downsample_buffer,
            frame_bytes, pred_len, sc, direction_flipped,
            posdata->speed, posdata, target_frame,
            cur_dir, direction_flipped // is_flip
        );
        posdata->last_resampled_frame = target_frame;
        posdata->consumed_samples = 0;
    }

    int total_samples = posdata->audio_last_stretched_samples;
    if (total_samples <= 0) return 0;

    int base_step = total_samples / posdata->max_sfd;
    int remainder = total_samples % posdata->max_sfd;
    int is_last = (posdata->cur_sfd == posdata->max_sfd - 1);
    int slice_len = base_step + (is_last ? remainder : 0);

    int start_sample = posdata->cur_sfd * base_step;
    if (is_last) start_sample = total_samples - slice_len;

    uint8_t *slice_src = downsample_buffer + (start_sample * frame_bytes);
    
    int copied = copy_slow_motion_slice(
        audio_buf,
        slice_src,
        frame_bytes,
        slice_len);

    posdata->consumed_samples += copied;
    int16_t *samples = (int16_t *)audio_buf;
    edge->ticks_since_last_flip++;

    atomic_store_int(&edge->pending_edge, AUDIO_EDGE_NONE);
    posdata->direction_changed = 0;

    return copied;
}

#endif

int vj_perform_fill_audio_buffers(
    veejay_t *info,
    editlist *el,
    uint8_t *audio_buf,
    performer_t *p,
    int *sampled_down,
    long long target_frame
) {
#ifdef HAVE_JACK
    video_playback_setup *settings = info->settings;
    uint8_t *temporary_buffer = p->audio_render_buffer;
    uint8_t *downsample_buffer = p->down_sample_buffer;
    performer_global_t *g = (performer_global_t*) info->performer;
    sample_b_t *sample_ptrB = &(g->A->sample_b);
    sample_b_t *sample_ptrA = &(g->A->sample_a);
    
    // for which frame (A or B are we filling audio data ?)
    sample_b_t *sample_ptr = (p == g->A ? sample_ptrA : sample_ptrB);

    if( p == g->A) {
        // its for A
        sample_ptr->audio_last_stretched_samples = settings->audio_last_stretched_samples;
        sample_ptr->direction_changed = atomic_load_int(&settings->audio_direction_changed);
        sample_ptr->max_sfd = atomic_load_int(&settings->audio_slice_len);
        sample_ptr->cur_sfd = atomic_load_int(&settings->audio_slice);
        sample_ptr->speed = settings->current_playback_speed;

      //  veejay_msg(VEEJAY_MSG_DEBUG, "Sample A : cur_sfd=%d,max_sfd=%d,start=%lld,end=%lld,offset=%lld", 
      //      sample_ptr->cur_sfd, sample_ptr->max_sfd, sample_ptr->start, sample_ptr->end, sample_ptr->offset);

    } else {
       // veejay_msg(VEEJAY_MSG_DEBUG, "Sample B : cur_sfd=%d,max_sfd=%d,start=%lld,end=%lld,offset=%lld", 
        //    sample_ptr->cur_sfd, sample_ptr->max_sfd, sample_ptr->start, sample_ptr->end, sample_ptr->offset);
        // its for B
        // audio_last_stretched_sample, direction_changed, cur_sfd, max_sfd known
    }

    int num_samples = (el->audio_rate / el->video_fps);
    int result = 0;

    if (sample_ptr->max_sfd > 1) {
        result = perform_slow_motion(info,el,p, audio_buf, downsample_buffer, sampled_down, target_frame, sample_ptr);
        if( p == g->A ) {
            settings->audio_last_stretched_samples = sample_ptr->audio_last_stretched_samples;
            atomic_store_int(&settings->audio_direction_changed, 0);
        }
        sample_ptr->prev_n_samples = result;

        return result;
    } //FIXME: better handling of "slowed" high speeds (sfd set to 2, speed set to 4 etc)

    result = perform_normal_playback(info,el,p, audio_buf, temporary_buffer,num_samples, el->audio_bps, target_frame, sample_ptr);
    if( p == g->A ) {
        atomic_store_int(&settings->audio_direction_changed, 0);
        atomic_store_int(&settings->audio_slice, 0);
    }
    sample_ptr->direction_changed = 0;
    sample_ptr->prev_n_samples = result;

    return result;
#else
    return 0;
#endif
}


static int vj_perform_apply_secundary_tag(veejay_t * info, performer_t *p, int sample_id, int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0_ref, uint8_t *p1_ref, int subrender )
{   
    long long nframe;
    int len = 0;
    int ssm = 0;
    performer_global_t *global = (performer_global_t*) info->performer;

    ycbcr_frame *cached_frame = NULL;

    switch (type)
    {       
        case VJ_TAG_TYPE_YUV4MPEG:  /* playing from stream */
        case VJ_TAG_TYPE_V4L:
        case VJ_TAG_TYPE_VLOOPBACK:
        case VJ_TAG_TYPE_AVFORMAT:
        case VJ_TAG_TYPE_NET:
        case VJ_TAG_TYPE_MCAST:
        case VJ_TAG_TYPE_PICTURE:
        case VJ_TAG_TYPE_COLOR:
    
        cached_frame = vj_perform_cache_get_frame(info,sample_id, VJ_PLAYBACK_MODE_TAG);
    
        if (cached_frame == NULL)
        {
            if(! vj_tag_get_active( sample_id ) )
            {
                vj_tag_set_active(sample_id, 1 );
            }
        
            int res = vj_tag_get_frame(sample_id, dst,p->audio_buffer[chain_entry]);
            if(res==1)  {
                vj_perform_cache_put_frame( global, sample_id, VJ_PLAYBACK_MODE_TAG, p->frame_buffer[ chain_entry ]);
            }
        }
        else
        {   
            ssm = vj_perform_cache_use_frame(cached_frame, dst);
        }

        break;
    
   case VJ_TAG_TYPE_NONE:
    
        nframe = vj_perform_sample_already_ticked(info->performer, sample_id, chain_entry );
        if( nframe == -1 ) {
            nframe = vj_calc_next_subframe(info, sample_id);
            vj_perform_sample_ticked(info->performer, sample_id, chain_entry, nframe );
        }

        cached_frame = vj_perform_cache_get_frame(info,sample_id, VJ_PLAYBACK_MODE_SAMPLE);

        if(cached_frame == NULL)
        {
            len = vj_perform_get_frame_fx( info, sample_id, nframe, src,dst,p0_ref,p1_ref );    
            if(len > 0 ) {
               vj_perform_cache_put_frame(global,sample_id, VJ_PLAYBACK_MODE_SAMPLE, p->frame_buffer[ chain_entry]);
            }
        }
        else
        {
            ssm = vj_perform_cache_use_frame(cached_frame, dst );
        }

        break;
    }

    return ssm;
}

static  int vj_perform_get_feedback_frame(veejay_t *info, VJFrame *src, VJFrame *dst, int check_sample, int s1)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    if(check_sample && info->settings->feedback == 0) {
        if(info->uc->sample_id == s1 && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ) {
            int max_sfd = (s1 ? sample_get_framedup( s1 ) : info->sfd );
            int strides[4] = {
                src->len,
                src->uv_len,
                src->uv_len,
                src->stride[3] * src->height
            };
            
            if( max_sfd <= 1 ) {
                vj_frame_copy( src->data, dst->data, strides );
                return 1;
            }

            uint8_t *pri7[4] = {
                p->primary_buffer[4]->Y,
                p->primary_buffer[4]->Cb,
                p->primary_buffer[4]->Cr,
                p->primary_buffer[4]->alpha
            };

            vj_frame_copy( pri7, dst->data, strides );

            return 1;
        }
    }

    return 0;
}

static  int vj_perform_get_frame_( veejay_t *info, int s1, long long nframe, VJFrame *src, VJFrame *dst, uint8_t *p0_buffer[4], uint8_t *p1_buffer[4], int check_sample )
{
    if( vj_perform_get_feedback_frame(info, src,dst, check_sample, s1) )
        return 1;

    int max_sfd = ( s1 ? sample_get_framedup(s1) : info->sfd );
    editlist *el = ( s1 ? sample_get_editlist(s1) : info->edit_list);
    if( el == NULL ) {
        veejay_msg(VEEJAY_MSG_WARNING, "Selected mixing source and ID does not exist, Use / to toggle mixing type" );
        if( info->edit_list == NULL ) {
            veejay_msg(VEEJAY_MSG_WARNING, "No plain source playing");
            return 0;
        } else {
            veejay_msg(VEEJAY_MSG_WARNING, "Fetching frame %d from plain source", nframe );
            el = info->edit_list;
        }
    }

    if( max_sfd <= 1 ) {
        int res = vj_el_get_video_frame( el, (long) nframe, dst->data );
        if(res) {
            dst->ssm = 0;
        }
        return res;
    }

    int cur_sfd = (s1 ? sample_get_framedups(s1 ) : 0);
    int speed = (s1 ? sample_get_speed(s1) : info->settings->current_playback_speed);
    int uv_len = dst->uv_len;
    
    long p0_frame = 0;
    long p1_frame = 0;

    long    start = ( s1 ? sample_get_startFrame(s1) : info->settings->min_frame_num);
    long    end   = ( s1 ? sample_get_endFrame(s1) : info->settings->max_frame_num );

    if( cur_sfd == 0 ) {
        p0_frame = nframe;
        vj_el_get_video_frame( el, p0_frame, p0_buffer );
        p1_frame = nframe + speed;

        if(p1_frame > end )
            p1_frame = end;
        else if ( p1_frame < start )
            p1_frame = start;

        if( p1_frame != p0_frame )
            vj_el_get_video_frame( el, p1_frame, p1_buffer );
        
        vj_perform_copy3( p0_buffer, dst->data, dst->len, uv_len,0 );
    } else {
        const uint32_t N = max_sfd;
        const uint32_t n1 = cur_sfd;
        //const float frac = 1.0f / (float) N * n1;
        const float frac = (float)n1 / (float)(N - 1);

        vj_frame_slow_single( p0_buffer, p1_buffer, dst->data, dst->len, uv_len, frac );
     
        if( (n1 + 1 ) == N ) {
            vj_perform_copy3( dst->data, p0_buffer, dst->len,uv_len,0);
        }
    }
    
    cur_sfd ++;
    if( cur_sfd == max_sfd)
        cur_sfd = 0;
    
    sample_set_framedups(s1, cur_sfd);

    return 1;
}

static int vj_perform_get_frame_fx(veejay_t *info, int s1, long long nframe, VJFrame *src, VJFrame *dst, uint8_t *p0plane, uint8_t *p1plane)
{
    uint8_t *p0_buffer[4] = {
        p0plane,
        p0plane + dst->len,
        p0plane + dst->len + dst->len,
        p0plane + dst->len + dst->len + dst->len
    };
    uint8_t *p1_buffer[4] = {
        p1plane,
        p1plane + dst->len,
        p1plane + dst->len + dst->len,
        p1plane + dst->len + dst->len + dst->len
    };

    return vj_perform_get_frame_(info, s1, nframe,src,dst,p0_buffer,p1_buffer,1 );
}

static int vj_perform_apply_secundary(veejay_t * info,performer_t *p, int this_sample_id, int sample_id, int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0_ref, uint8_t *p1_ref, int subrender)
{       
    int nframe;
    int len;
    int res = 1;
    int ssm = 0;
    performer_global_t *g = (performer_global_t*) info->performer;


    ycbcr_frame *cached_frame = NULL;

    switch (type)
    {
        case VJ_TAG_TYPE_YUV4MPEG:  
        case VJ_TAG_TYPE_V4L:
        case VJ_TAG_TYPE_VLOOPBACK:
        case VJ_TAG_TYPE_AVFORMAT:
        case VJ_TAG_TYPE_NET:
        case VJ_TAG_TYPE_MCAST:
        case VJ_TAG_TYPE_COLOR:
        case VJ_TAG_TYPE_PICTURE:
        case VJ_TAG_TYPE_GENERATOR:
    
            cached_frame = vj_perform_cache_get_frame(info,sample_id, VJ_PLAYBACK_MODE_TAG);
            if (cached_frame == NULL)
            {
                if(! vj_tag_get_active( sample_id ) )
                {
                    vj_tag_set_active(sample_id, 1 );
                }
        
                res = vj_tag_get_frame(sample_id, dst,p->audio_buffer[chain_entry]);
                if(res) {
                    vj_perform_cache_put_frame(g, sample_id, VJ_PLAYBACK_MODE_TAG, p->frame_buffer[ chain_entry ]);
                }                   
            }
            else
            {
                ssm = vj_perform_cache_use_frame(cached_frame, dst);
            }
            
            break;
        
        case VJ_TAG_TYPE_NONE:

            nframe = vj_perform_sample_already_ticked(info->performer, sample_id, chain_entry );
            if( nframe == -1 ) {
                nframe = vj_calc_next_subframe(info, sample_id);
                vj_perform_sample_ticked(info->performer, sample_id, chain_entry, nframe );
            }

            cached_frame = vj_perform_cache_get_frame(info,sample_id, VJ_PLAYBACK_MODE_SAMPLE);

            if(cached_frame == NULL) {
                len = vj_perform_get_frame_fx( info, sample_id, nframe, src, dst, p0_ref, p1_ref ); 
               
                if(len > 0 ) {
                    vj_perform_cache_put_frame(g, sample_id, VJ_PLAYBACK_MODE_SAMPLE, p->frame_buffer[ chain_entry]);
                }
            }
            else
            {
                ssm = vj_perform_cache_use_frame(cached_frame,dst);  
            }   
            
            break;
    }

    return ssm;
}

static void vj_perform_tag_render_chain_entry(veejay_t *info,performer_t *p,vjp_kf *setup, int sample_id, int pm, sample_eff_chain *fx_entry,int chain_entry, VJFrame *frames[2], int subrender)
{
    VJFrameInfo *frameinfo;
    video_playback_setup *settings = info->settings;
    
    frameinfo = info->effect_frame_info;
    
    frames[1]->data[0] = p->frame_buffer[chain_entry]->Y;
    frames[1]->data[1] = p->frame_buffer[chain_entry]->Cb;
    frames[1]->data[2] = p->frame_buffer[chain_entry]->Cr;
    frames[1]->data[3] = p->frame_buffer[chain_entry]->alpha; 

    setup->ref = sample_id;

    int effect_id = fx_entry->effect_id;
    int sub_mode = vje_get_subformat(effect_id);
    int ef = vje_get_extra_frame(effect_id);

    vj_perform_supersample(settings,p, frames[0], (ef ? frames[1] : NULL), sub_mode, chain_entry );

    if(ef)
    {
        frames[1]->ssm = vj_perform_apply_secundary_tag(info,p,fx_entry->channel,fx_entry->source_type,chain_entry,frames[0],frames[1],p->frame_buffer[chain_entry]->P0, p->frame_buffer[chain_entry]->P1, 0 );

        if( subrender && settings->fxdepth ) {
            frames[1]->ssm = vj_perform_preprocess_secundary( info,p, fx_entry->channel,fx_entry->source_type,sub_mode,chain_entry, frames, frameinfo );
        }

        vj_perform_supersample(settings,p, NULL, frames[1], sub_mode, chain_entry); 
    }
    
    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 1) {
        vj_perform_pre_chain( p, frames[0] ); 
    }

    vj_perform_apply_first(info,p,setup,frames,fx_entry,effect_id,chain_entry,atomic_load_long_long(&settings->current_frame_num),fx_entry->fx_instance,pm);
    
    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 2) {
        vj_perform_pre_chain( p, frames[0] );
    }
}

static  int vj_perform_preprocess_secundary( veejay_t *info,performer_t *p, int id, int mode,int expected_ssm,int chain_entry, VJFrame **F, VJFrameInfo *frameinfo )
{
    if( mode == 0 ) {
       if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE && id == info->uc->sample_id ) { 
           return F[0]->ssm;
       }
    }
    else {
        if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG && id == info->uc->sample_id )
            return F[0]->ssm;
    }

    int n  = 0;
    video_playback_setup *settings = info->settings;

    VJFrame top,sub;
    veejay_memcpy(&top, F[1], sizeof(VJFrame));
    top.data[0] = F[1]->data[0]; 
    top.data[1] = F[1]->data[1];
    top.data[2] = F[1]->data[2];
    top.data[3] = F[1]->data[3];
    top.ssm     = F[1]->ssm;

    veejay_memcpy(&sub, F[0], sizeof(VJFrame));
    sub.data[0] = p->subrender_buffer[0];
    sub.data[1] = p->subrender_buffer[1];
    sub.data[2] = p->subrender_buffer[2];
    sub.data[3] = p->subrender_buffer[3];
    sub.ssm     = 0; 

    VJFrame *subframes[2];
    subframes[0] = &top;
    subframes[1] = &sub;

    /* top frame is F[1] (render on sub-sample) */

    uint8_t *p0_ref = p->subrender_buffer[0] + ( F[0]->len * 4 );
    uint8_t *p1_ref = p0_ref + (F[0]->len * 4);

    /* render FX chain */
    vjp_kf setup;
    veejay_memset(&setup,0,sizeof(vjp_kf));
    setup.ref = id; 

    sample_eff_chain **chain = NULL;

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 3 ) {
        vj_perform_pre_chain( p, &top );
    }

    switch( mode ) {
        case VJ_PLAYBACK_MODE_SAMPLE:   
            chain = sample_get_effect_chain( id );
            for( n=0; n < SAMPLE_MAX_EFFECTS; n ++ ) {
                sample_eff_chain *fx_entry = chain[n];
                if( fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
                    continue;

                int fx_id = fx_entry->effect_id;
                int sm = vje_get_subformat(fx_id);
                int ef = vje_get_extra_frame(fx_id);
                
                if( ef ) {
                    subframes[1]->ssm = vj_perform_apply_secundary(info,p,id,fx_entry->channel,fx_entry->source_type,n,subframes[0],subframes[1],p0_ref, p1_ref, 1);
                }   
		
                vj_perform_supersample( settings,p, subframes[0], subframes[1], sm, chain_entry );

                vj_perform_apply_first(info,p,&setup,subframes,fx_entry,fx_id,n,atomic_load_long_long(&info->settings->current_frame_num),fx_entry->fx_instance, mode );
            }
            break;
        case VJ_PLAYBACK_MODE_TAG:
            chain = vj_tag_get_effect_chain( id );
            for( n=0; n < SAMPLE_MAX_EFFECTS; n ++ ) {
                sample_eff_chain *fx_entry = chain[n];
                if( fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
                    continue;
                
                int fx_id = fx_entry->effect_id;
                int sm = vje_get_subformat(fx_id);
                int ef = vje_get_extra_frame(fx_id);
                
                if( ef ) {
                    subframes[1]->ssm = vj_perform_apply_secundary_tag(info,p,fx_entry->channel,fx_entry->source_type,n,subframes[0],subframes[1],p0_ref,p1_ref,1);
                }
               
                vj_perform_supersample(settings,p, subframes[0],subframes[1], sm, chain_entry);

                vj_perform_apply_first(info,p,&setup,subframes,fx_entry,fx_id,n,atomic_load_long_long(&info->settings->current_frame_num),fx_entry->fx_instance, mode );
            }
            break;
    }

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 4 ) {
        vj_perform_pre_chain( p, &top );
    }

    return top.ssm;
}

static void vj_perform_render_chain_entry(veejay_t *info,performer_t *p, vjp_kf *setup, int sample_id, int pm, sample_eff_chain *fx_entry,
     int chain_entry, VJFrame *frames[2], int subrender)
{
    VJFrameInfo *frameinfo;
    video_playback_setup *settings = info->settings;
    
    frameinfo = info->effect_frame_info;
    
    frames[1]->data[0] = p->frame_buffer[chain_entry]->Y;
    frames[1]->data[1] = p->frame_buffer[chain_entry]->Cb;
    frames[1]->data[2] = p->frame_buffer[chain_entry]->Cr;
    frames[1]->data[3] = p->frame_buffer[chain_entry]->alpha;

    setup->ref = sample_id;

    int effect_id = fx_entry->effect_id;
    int sub_mode = vje_get_subformat(effect_id);
    int ef = vje_get_extra_frame(effect_id);

    vj_perform_supersample(settings,p, frames[0], NULL, sub_mode, chain_entry);

    if(ef)
    {
        frames[1]->ssm = vj_perform_apply_secundary(info,p,sample_id,fx_entry->channel,fx_entry->source_type,chain_entry,frames[0],frames[1],p->frame_buffer[chain_entry]->P0, p->frame_buffer[chain_entry]->P1, 0);
        
        if( subrender && settings->fxdepth) {
            frames[1]->ssm = vj_perform_preprocess_secundary(info,p,fx_entry->channel,fx_entry->source_type,sub_mode,chain_entry,frames,frameinfo );
        }

        vj_perform_supersample(settings,p, NULL, frames[1], sub_mode, chain_entry);     
    }

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 1) {
        vj_perform_pre_chain( p,frames[0] ); 
    }

    vj_perform_apply_first(info,p,setup,frames,fx_entry,effect_id,chain_entry,
            atomic_load_long_long(&settings->current_frame_num), fx_entry->fx_instance,pm  );
    
    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 2) {
        vj_perform_pre_chain( p, frames[0] );
    }

}

void vj_perform_global_chain_reset(veejay_t *info) {
    global_chain_t *g = info->global_chain;
    sample_eff_chain **gfx = g->fx_chain;

    if(gfx == NULL)
        return;

    for(int chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
    {
        if(gfx[chain_entry]->kf) {
            vpf(gfx[chain_entry]->kf);
            gfx[chain_entry]->kf = NULL;
            gfx[chain_entry]->kf_type = 0;
            gfx[chain_entry]->kf_status = 0;
            veejay_msg(VEEJAY_MSG_DEBUG, "Global KF reset");
        }
    }
}

static void vj_perform_global_chain_sync(veejay_t *info, global_chain_t *g_chain) {
    int pm = info->uc->playback_mode;
    int id = info->uc->sample_id;
    video_playback_setup *settings = info->settings;

    if(!g_chain->enabled)
        return;

    sample_eff_chain **origin = (g_chain->origin_mode == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_effect_chain(g_chain->origin_id) :
                                (g_chain->origin_mode == VJ_PLAYBACK_MODE_TAG ? vj_tag_get_effect_chain(g_chain->origin_id) : NULL ));
    if(origin == NULL) {
        return; // no keyframes
    }

    if(g_chain->origin_id == id && g_chain->origin_mode == pm )
        return;

    sample_eff_chain **gfx = g_chain->fx_chain;

    for(int chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
    {
        if(gfx[chain_entry]->effect_id <= 0) {
            continue;
        }

        if(gfx[chain_entry]->kf) {
            continue;
        }

        if(origin[chain_entry]->kf && origin[chain_entry]->kf_status) {
            int frame_len = settings->max_frame_num - settings->min_frame_num;
            void *new_kf = keyframe_port_clone_and_resize( origin[chain_entry]->kf, frame_len);
            if(new_kf) {
                gfx[chain_entry]->kf = new_kf;
                gfx[chain_entry]->kf_status = 1;
                gfx[chain_entry]->kf_type = origin[chain_entry]->kf_type;
                veejay_msg(VEEJAY_MSG_DEBUG, "Resampled KF to new length of %d frames", frame_len);
            }
        }
    }
}


static void vj_perform_sample_complete_buffers(veejay_t * info,performer_t *p, vjp_kf *effect_info, int *hint444, 
    VJFrame *f0, VJFrame *f1, int sample_id, int pm, vjp_kf *setup, sample_eff_chain **chain, sample_info *si)
{
    if(info->uc->sample_id == info->global_chain->origin_id && info->uc->playback_mode == info->global_chain->origin_mode && chain == info->global_chain->fx_chain)
        return; // already rendered

    int chain_entry;
    VJFrame *frames[2];
    frames[0] = f0;
    frames[1] = f1;
    setup->ref = sample_id;

    if(p->pvar_.fader_active || p->pvar_.fade_value > 0 || p->pvar_.fade_alpha ) { 
        if( p->pvar_.fade_entry == -1 ) {
            vj_perform_pre_chain( p, frames[0] );
        }
    }

    for(chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
    {
        sample_eff_chain *fx_entry = chain[chain_entry];
        if(fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
            continue;
       
        frames[1]->data[0] = p->frame_buffer[chain_entry]->Y;
        frames[1]->data[1] = p->frame_buffer[chain_entry]->Cb;
        frames[1]->data[2] = p->frame_buffer[chain_entry]->Cr;
        frames[1]->data[3] = p->frame_buffer[chain_entry]->alpha;

        vj_perform_render_chain_entry(info,p,effect_info,sample_id,pm,fx_entry,chain_entry,frames,(si->subrender? fx_entry->is_rendering: si->subrender));
    }
    *hint444 = frames[0]->ssm;
}

static void vj_perform_tag_complete_buffers(veejay_t * info, performer_t *p,vjp_kf *effect_info, int *hint444, VJFrame *f0, VJFrame *f1, int sample_id, int pm, vjp_kf *setup, sample_eff_chain **chain, vj_tag *tag  )
{
    if(info->uc->sample_id == info->global_chain->origin_id && info->uc->playback_mode == info->global_chain->origin_mode && chain == info->global_chain->fx_chain)
        return; // already rendered

    int chain_entry;
    VJFrame *frames[2];
    frames[0] = f0;
    frames[1] = f1;
    setup->ref = sample_id;

    if( p->pvar_.fader_active || p->pvar_.fade_value >0 || p->pvar_.fade_alpha) {
        if( p->pvar_.fade_entry == -1 ) {
            vj_perform_pre_chain(p, frames[0] );
        }
    }

    for(chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
    {
        sample_eff_chain *fx_entry = chain[chain_entry];
        if(fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
            continue;
        vj_perform_tag_render_chain_entry(info,p,effect_info,sample_id,pm,fx_entry,chain_entry,frames,(tag->subrender ? fx_entry->is_rendering : tag->subrender));
    }

    *hint444 = frames[0]->ssm;
}

static void vj_perform_plain_fill_buffer(veejay_t * info, performer_t *p,VJFrame *dst, int sample_id, int mode, long frame_num)
{
    performer_global_t *g = (performer_global_t*) info->performer;

    VJFrame frame;

    if(info->settings->feedback && info->settings->feedback_stage > 1 ) {
        vj_copy_frame_holder(dst, NULL, &frame); 
        frame.data[0] = g->feedback_frame.data[0];
        frame.data[1] = g->feedback_frame.data[1];
        frame.data[2] = g->feedback_frame.data[2];
        frame.data[3] = g->feedback_frame.data[3];
    } else {
        vj_copy_frame_holder(dst, NULL, &frame);
        frame.data[0] = dst->data[0];
        frame.data[1] = dst->data[1];
        frame.data[2] = dst->data[2];
        frame.data[3] = dst->data[3];
    }

    uint8_t *p0_buffer[PSLOW_B] = {
        p->primary_buffer[PSLOW_B]->Y,
        p->primary_buffer[PSLOW_B]->Cb,
        p->primary_buffer[PSLOW_B]->Cr,
        p->primary_buffer[PSLOW_B]->alpha };

    uint8_t *p1_buffer[4]= {
        p->primary_buffer[PSLOW_A]->Y,
        p->primary_buffer[PSLOW_A]->Cb,
        p->primary_buffer[PSLOW_A]->Cr,
        p->primary_buffer[PSLOW_A]->alpha };

    if(mode == VJ_PLAYBACK_MODE_SAMPLE)
    {
        vj_perform_get_frame_(info, sample_id, frame_num,&frame,&frame, p0_buffer,p1_buffer,0 );
    } 
    else if ( mode == VJ_PLAYBACK_MODE_PLAIN ) {
        vj_perform_get_frame_(info, 0, frame_num,&frame,&frame, p0_buffer, p1_buffer,0 );
    }

}

static int rec_audio_sample_ = 0;
static int vj_perform_render_sample_frame(veejay_t *info, performer_t *p, uint8_t *frame[4], int sample, int type)
{
    int audio_len = 0;

    if( type == 0 && info->audio == AUDIO_PLAY ) {
        if( info->current_edit_list->has_audio )
        {
            audio_len = vj_perform_fill_audio_buffers(
                            info,
                            info->current_edit_list,
                            p->audio_rec_buffer,
                            p,
                            
                            &rec_audio_sample_,
                            atomic_load_long_long(&info->settings->current_frame_num)
                        );
        } 
    }

    return sample_record_frame( sample,frame,p->audio_rec_buffer,audio_len,info->pixel_format );
}

static int vj_perform_render_offline_tag_frame(veejay_t *info)
{
    performer_global_t *g = (performer_global_t*) info->performer;

    if(vj_tag_get_active( info->settings->offline_tag_id ) == 0 ) {
        vj_tag_enable( info->settings->offline_tag_id );
    }

    vj_tag_get_frame( info->settings->offline_tag_id, g->offline_frame, NULL );

    return vj_tag_record_frame( info->settings->offline_tag_id, g->offline_frame->data, NULL, 0, info->pixel_format );
}
    
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4])
{
    return vj_tag_record_frame( info->uc->sample_id, frame, NULL, 0, info->pixel_format);
}   

int vj_perform_commit_offline_recording(veejay_t *info, int id, char *recording)
{
    sample_info *sample = NULL;
    if( id > 0 ) {
        sample = sample_get(id);
        if(!sample) {
            veejay_msg(VEEJAY_MSG_ERROR, "Sample %d no longer exists, creating new sample for recording", id);
            id = 0;
        }
    }

    int new_id = -1;

    if( id > 0 && sample != NULL ) {
        long end_pos = sample->last_frame;
        new_id = veejay_edit_addmovie_sample( info, recording, id );
        if(new_id != -1) {
            if(end_pos < sample->last_frame) {
                sample->first_frame = end_pos;
            }
            veejay_msg(VEEJAY_MSG_DEBUG, "Sample position set to %ld - %ld to loop newly recorded video %s",
                    sample->first_frame, sample->last_frame, recording );
        }
        else {
            veejay_msg(VEEJAY_MSG_ERROR,"Failed to add recording %s to EditList", recording);
        }
    }

    if( id == 0 ) {
        new_id = veejay_edit_addmovie_sample(info, recording, 0 );
        if(new_id != -1) {
            veejay_msg(VEEJAY_MSG_DEBUG, "Added recording %s to new sample", recording);
        }
        else {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to add recording %s to EditList", recording );
        }
    }

    return new_id;
}

static int vj_perform_record_offline_commit_single(veejay_t *info)
{
    char filename[2048];
    int stream_id = info->settings->offline_tag_id;
    int n_id = 0;
    if(vj_tag_get_encoded_file(stream_id, filename))
    {
        int df = vj_event_get_video_format();
        int id = 0;

        if(info->settings->offline_linked_sample_id == -1 ) {
            id = 0;
            veejay_msg(VEEJAY_MSG_INFO, "Adding recorded video file %s to a new sample", filename);
        }
        else {
            id = info->settings->offline_linked_sample_id;
            veejay_msg(VEEJAY_MSG_INFO, "Appending recorded video file %s to sample %d", filename, id );
        }

        if( df == ENCODER_YUV4MPEG || df == ENCODER_YUV4MPEG420 ) {
            if(info->settings->offline_linked_sample_id > 0) {
                veejay_msg(VEEJAY_MSG_WARNING, "Cannot append to a yuv4mpeg stream (change recording format)");
            }
            n_id = veejay_create_tag( info, VJ_TAG_TYPE_YUV4MPEG,filename,info->nstreams,0,0 );
        } else {

            n_id = vj_perform_commit_offline_recording( info, id, filename );

            veejay_msg(VEEJAY_MSG_INFO, "Sample %d has new video data", n_id );
        }

        return n_id;
    }

    return 0;
}

static int vj_perform_record_commit_single(veejay_t *info)
{
    char filename[1024];

    if( info->seq->active && info->seq->rec_id ) {
            int id = 0;
            if( sample_get_encoded_file( info->seq->rec_id, filename ) ) {
                int df = vj_event_get_video_format();
                if ( df == ENCODER_YUV4MPEG || df == ENCODER_YUV4MPEG420 ) {
                    id = veejay_create_tag( info, VJ_TAG_TYPE_YUV4MPEG,filename,info->nstreams,0,0 );
                }
                else {  
                    id = veejay_edit_addmovie_sample(info,filename,0);
                }

                if( id <= 0 ) {
                    veejay_msg(VEEJAY_MSG_ERROR, "Error trying to add %s as a sample", filename);
                }

            }
            return id;
    }
    else {
        if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
        {
            if(sample_get_encoded_file(info->uc->sample_id, filename))
            {
                int df = vj_event_get_video_format();
                int id = 0;
                if ( df == ENCODER_YUV4MPEG || df == ENCODER_YUV4MPEG420 ) {
                    id = veejay_create_tag( info, VJ_TAG_TYPE_YUV4MPEG,filename,info->nstreams,0,0 );
                }
                else 
                {
                    id = veejay_edit_addmovie_sample(info,filename, 0 );
                }
                if(id <= 0)
                {
                    veejay_msg(VEEJAY_MSG_ERROR, "Error trying to add %s as sample or stream", filename);
                    return 0;
                }
                return id;
            }
        }

        if(info->uc->playback_mode==VJ_PLAYBACK_MODE_TAG)
        {
            int stream_id = info->uc->sample_id;
            if(vj_tag_get_encoded_file(stream_id, filename))
            {
                int df = vj_event_get_video_format();
                int id = 0;
                if( df == ENCODER_YUV4MPEG || df == ENCODER_YUV4MPEG420 ) {
                    id = veejay_create_tag( info, VJ_TAG_TYPE_YUV4MPEG,filename,info->nstreams,0,0 );
                } else {
                    id = veejay_edit_addmovie_sample(info, filename, 0);
                }
                if( id <= 0 )
                {
                    veejay_msg(VEEJAY_MSG_ERROR, "Adding file %s to new sample", filename);
                    return 0;
                }
                return id;
            }
        }
    }
    return 0;
}

void vj_perform_start_offline_recorder(veejay_t *v, int rec_format, int stream_id, int duration, int autoplay, int sample_id)
{
    char tmp[2048];
    char prefix[40];

    if(rec_format==-1)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "No video recording format selected");
        return;
    }

    snprintf(prefix,sizeof(prefix),"stream-%02d", stream_id);

    if(!veejay_create_temp_file(prefix, tmp ))
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Error creating temporary file %s. Unable to start offline recorder", tmp);
        return;
    }

    veejay_msg(VEEJAY_MSG_DEBUG, "Created temporary file %s", tmp );

    if( vj_tag_init_encoder(stream_id, tmp, rec_format,duration) ) 
    {
        video_playback_setup *s = v->settings;
        s->offline_record = 1;
        s->offline_tag_id = stream_id;
        s->offline_created_sample = autoplay;
        s->offline_linked_sample_id = ( sample_exists(sample_id) ? sample_id: -1 );

        if(v->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE && s->offline_linked_sample_id > 0 ) {
            sample_set_offline_recorder( v->uc->sample_id, duration, stream_id, rec_format );
        }
    }
    else
    {
       veejay_msg(VEEJAY_MSG_ERROR, "Error starting offline recorder stream %d",stream_id);
    }
}

void vj_perform_record_offline_stop(veejay_t *info)
{   
    video_playback_setup *settings = info->settings;
    int df = vj_event_get_video_format();

    int stream_id = settings->offline_tag_id;
    int play = settings->offline_created_sample;
    
    vj_tag_reset_encoder(stream_id);
    vj_tag_reset_autosplit(stream_id);
    
    if( play ) {
        if(df != ENCODER_YUV4MPEG && df != ENCODER_YUV4MPEG420)
        {
            info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
            int id = sample_highest_valid_id();
            veejay_set_sample(info, id );
            if( id > 0 ) {
                veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d",id);
            }
        }
        else {

            veejay_msg(VEEJAY_MSG_INFO, "Completed offline recording");
        }
    }

    vj_perform_record_buffer_free(info->performer);

    if( settings->offline_linked_sample_id > 0 ) {
        int n_frames = 0;
        int linked_id = 0;
        int rec_format = 0;
        sample_get_offline_recorder( settings->offline_linked_sample_id, &n_frames, &linked_id, &rec_format );
        vj_perform_start_offline_recorder(info, rec_format, linked_id, n_frames, 0, settings->offline_linked_sample_id ); 
    }
    else {
        settings->offline_record = 0;
        settings->offline_created_sample = 0;
        settings->offline_tag_id = 0;
    }
    
}

void vj_perform_record_stop(veejay_t *info)
{
 video_playback_setup *settings = info->settings;
 int df = vj_event_get_video_format();

 if(info->uc->playback_mode==VJ_PLAYBACK_MODE_SAMPLE || ( info->seq->active && info->seq->rec_id > 0 ))
 {
     sample_reset_encoder(info->uc->sample_id);
     sample_reset_autosplit(info->uc->sample_id);
     if( settings->sample_record && settings->sample_record_switch)
     {
        settings->sample_record_switch = 0;
        if( df != ENCODER_YUV4MPEG && df != ENCODER_YUV4MPEG420 ) {
            int id = sample_highest_valid_id();
            veejay_set_sample( info,id);
            if(id > 0 ) {
                veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d",id);
            }
        } else {
            veejay_msg(VEEJAY_MSG_WARNING, "Not autoplaying new streams");
        }
     }
     settings->sample_record = 0;
     settings->sample_record_switch =0;
     settings->render_list = 0;

     info->seq->rec_id = 0;

 } 
 else if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
 {
    int stream_id = info->uc->sample_id;
    int play = settings->tag_record_switch;
    vj_tag_reset_encoder(stream_id);
    vj_tag_reset_autosplit(stream_id);
    
    settings->tag_record = 0;
    settings->tag_record_switch = 0;

    if(play)
    {
        if(df != ENCODER_YUV4MPEG && df != ENCODER_YUV4MPEG420)
        {
            info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
            int id = sample_highest_valid_id();
            if( id > 0 ) {
                veejay_set_sample(info, id );
                veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d",id);
            }
        }
        else {

            veejay_msg(VEEJAY_MSG_WARNING, "Not autoplaying new streams");
        }
    }
  }
}

void vj_perform_record_sample_frame(veejay_t *info, int sample, int type) {
    uint8_t *frame[4];
    int res = 1;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    frame[0] = p->primary_buffer[0]->Y;
    frame[1] = p->primary_buffer[0]->Cb;
    frame[2] = p->primary_buffer[0]->Cr;
    frame[3] = NULL;

    res = vj_perform_render_sample_frame(info, p, frame, sample,type);

    if( res == 2 || res == 1)
    {
        sample_stop_encoder( sample );
        
        vj_perform_record_commit_single( info );
        vj_perform_record_stop(info);
     }
    
     if( res == -1)
     {
        sample_stop_encoder(sample);
        vj_perform_record_stop(info);
     }
}

void vj_perform_record_offline_tag_frame(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    int res = 1;
    int stream_id = settings->offline_tag_id;
    performer_global_t *g = (performer_global_t*) info->performer;
 
    if( g->offline_frame == NULL ) {
        if(vj_perform_record_buffer_init(info) == 0) {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to allocate buffer for recorder");
            vj_tag_stop_encoder(stream_id);
            return;
        }
    }

    res = vj_perform_render_offline_tag_frame(info);

    if( res == 2)
    {
        int df = vj_event_get_video_format();
        long frames_left = vj_tag_get_frames_left(stream_id) ;

        vj_tag_stop_encoder( stream_id );
        int n = vj_perform_record_offline_commit_single( info );
        vj_tag_reset_encoder( stream_id );
        
        if(frames_left > 0 )
        {
            if( vj_tag_init_encoder( stream_id, NULL,
                df, frames_left)==-1)
            {
                veejay_msg(VEEJAY_MSG_WARNING,"Error while auto splitting"); 
            }
        }
        else
        {
            long len = vj_tag_get_total_frames(stream_id);
    
            veejay_msg(VEEJAY_MSG_DEBUG, "Added new sample %d of %ld frames",n,len);
            vj_tag_reset_encoder( stream_id );
            vj_tag_reset_autosplit( stream_id );
        }
     }
    
     if( res == 1)
     {
        vj_tag_stop_encoder(stream_id);
        vj_perform_record_offline_commit_single( info );        
        vj_perform_record_offline_stop(info);
     }

     if( res == -1)
     {
        vj_tag_stop_encoder(stream_id);
        vj_perform_record_offline_stop(info);
     }
}

void vj_perform_record_tag_frame(veejay_t *info) {
    uint8_t *frame[4];
    int res = 1;
    int stream_id = info->uc->sample_id;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    frame[0] = p->primary_buffer[0]->Y;
    frame[1] = p->primary_buffer[0]->Cb;
    frame[2] = p->primary_buffer[0]->Cr;
    frame[3] = NULL;

    info->effect_frame1->data[0] = frame[0];
    info->effect_frame1->data[1] = frame[1];
    info->effect_frame1->data[2] = frame[2];
    info->effect_frame1->data[3] = frame[3];

    res = vj_perform_render_tag_frame(info, frame);

    if( res == 2)
    {
        int df = vj_event_get_video_format();
        long frames_left = vj_tag_get_frames_left(stream_id) ;
        vj_tag_stop_encoder( stream_id );
        int n = vj_perform_record_commit_single( info );
        vj_tag_reset_encoder( stream_id );
        if(frames_left > 0 )
        {
            if( vj_tag_init_encoder( stream_id, NULL, df, frames_left)==-1)
            {
                veejay_msg(VEEJAY_MSG_WARNING,
                    "Error while auto splitting"); 
            }
        }
        else
        {
            long len = vj_tag_get_total_frames(stream_id);
    
            veejay_msg(VEEJAY_MSG_DEBUG, "Added new sample %d of %ld frames",n,len);
            vj_tag_reset_encoder( stream_id );
            vj_tag_reset_autosplit( stream_id );
        }
     }
    
     if( res == 1)
     {
        vj_tag_stop_encoder(stream_id);
        vj_perform_record_commit_single( info );        
        vj_perform_record_stop(info);
     }

     if( res == -1)
     { 
        vj_tag_stop_encoder(stream_id);
        vj_perform_record_stop(info);
     }
}

static void vj_perform_tag_fill_buffer(veejay_t * info, performer_t *p, VJFrame *dst, int sample_id)
{
    int error = 1;
    performer_global_t *g = (performer_global_t*) info->performer;
    int active = p->pvar_.active;

    if(info->settings->feedback && info->settings->feedback_stage > 1 ) {
        dst->data[0] = g->feedback_frame.data[0];
        dst->data[1] = g->feedback_frame.data[1];
        dst->data[2] = g->feedback_frame.data[2];
        dst->data[3] = g->feedback_frame.data[3];
        dst->ssm = g->feedback_frame.ssm;
    } else {
        dst->data[0] = p->primary_buffer[0]->Y;
        dst->data[1] = p->primary_buffer[0]->Cb;
        dst->data[2] = p->primary_buffer[0]->Cr;
        dst->data[3] = p->primary_buffer[0]->alpha;
    }
    
    if(!active)
    {
        vj_tag_enable( sample_id );
    }
    else
    {
        if (vj_tag_get_frame(sample_id, dst,NULL))
        {
            error = 0;
        }
    }         

    if (error == 1)
    {
        dummy_apply(dst,VJ_EFFECT_COLOR_BLACK );
    }

}

static void vj_perform_pre_chain(performer_t *p, VJFrame *frame)
{
    vj_perform_copy3( frame->data, p->temp_buffer, frame->len, (frame->ssm ? frame->len : frame->uv_len), frame->stride[3] * frame->height  );

    veejay_memcpy( &(p->temp_frame), frame, sizeof(VJFrame) );    
}

static  inline  void    vj_perform_supersample_chain( performer_t *p, subsample_mode_t sample_mode, VJFrame *frame ) //FIXME
{
    if( frame->ssm == p->temp_frame.ssm ) {
        return; // nothing to do
    }

    if( p->temp_frame.ssm == 0 && frame->ssm == 1 ) {
        chroma_supersample(sample_mode,&(p->temp_frame), p->temp_buffer );
        p->temp_frame.ssm = 1;
    }

    if( p->temp_frame.ssm == 1 && frame->ssm == 0 ) {
        chroma_subsample(sample_mode,&(p->temp_frame),p->temp_frame.data);
        p->temp_frame.ssm = 0;
    }
}

void    vj_perform_follow_fade(veejay_t *info, int status) {
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    p->pvar_.follow_fade = status;
}

static int vj_perform_post_chain_sample(veejay_t *info,performer_t *p, VJFrame *frame, int sample_id)
{
    int opacity; 
    int mode   = p->pvar_.fader_active;
    int follow = 0;
    int fade_alpha_method = p->pvar_.fade_alpha;

    if(mode == 2 ) // manual fade
        opacity = (int) sample_get_fader_val(sample_id);
    else    // fade in/fade out
        opacity = (int) sample_apply_fader_inc(sample_id);

    p->pvar_.fade_value = opacity;

    switch( fade_alpha_method ) {
        case 0:
            if( opacity > 0 ) {
                vj_perform_supersample_chain(p, info->settings->sample_mode,frame );
                opacity_blend_apply( frame->data ,p->temp_buffer,frame->len,(frame->ssm ? frame->len: frame->uv_len), opacity );
            }
            break;
        default:
            vj_perform_supersample_chain( p, info->settings->sample_mode, frame );
            alpha_transition_apply( frame, p->temp_buffer,(0xff - opacity) );
            break;
    }

    if(mode != 2)
    {
        int dir =sample_get_fader_direction(sample_id);
        if((dir<0) &&(opacity == 0))
        {
            int fade_method = sample_get_fade_method(sample_id );
            if( fade_method == 0 )
                sample_set_effect_status(sample_id, 1);
            sample_reset_fader(sample_id);
            if( p->pvar_.follow_fade ) {
              follow = 1;
            }
        }
        if((dir>0) && (opacity==255))
        {
            sample_reset_fader(sample_id);
            if( p->pvar_.follow_fade ) {
              follow = 1;
            }
        }
        } else if(mode) {
            if( p->pvar_.follow_fade ) {
              follow = 1;
        }
    }

    if( follow ) {
        if( p->pvar_.fade_entry == -1 ) {
            //@ follow first channel B in chain
            int i,k;
            int tmp = 0;    
            for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++) {
                k = sample_get_chain_channel(sample_id, i );
                tmp = sample_get_chain_source(sample_id, i );
                if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k) )) {
                    p->pvar_.follow_now[1] = tmp;  
                    p->pvar_.follow_now[0] = k;
                    break;
                }
            }
        }
        else {
            //@ follow channel B in chain from seleted entry 
            int tmp = 0;
            int k = sample_get_chain_channel(sample_id, p->pvar_.fade_entry );
            tmp = sample_get_chain_source(sample_id, p->pvar_.fade_entry );
            if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k))) {
                p->pvar_.follow_now[1] = tmp;
                p->pvar_.follow_now[0] = k;
            }
        }
    }

    return follow;
}

static int vj_perform_post_chain_tag(veejay_t *info,performer_t *p, VJFrame *frame, int sample_id)
{
    int opacity = 0; //@off 
    int mode = p->pvar_.fader_active;
    int follow = 0;
    int fade_alpha_method = p->pvar_.fade_alpha;

    if(mode == 2)
        opacity = (int) vj_tag_get_fader_val(sample_id);
    else if( mode )
        opacity = (int) vj_tag_apply_fader_inc(sample_id);

    p->pvar_.fade_value = opacity;

    if( opacity == 0 ) {
        if( p->pvar_.follow_fade ) {
           follow = 1;
        }
    }

    switch( fade_alpha_method ) {
        case 0:
            if( opacity > 0 ) {
                vj_perform_supersample_chain( p, info->settings->sample_mode, frame );
                opacity_blend_apply( frame->data ,p->temp_buffer,frame->len, (frame->ssm ? frame->len : frame->uv_len), opacity );
            }
            break;
        default:
            vj_perform_supersample_chain( p, info->settings->sample_mode, frame );
            alpha_transition_apply( frame, p->temp_buffer,0xff - opacity );
            break;
    }

    if(mode != 2)
    {
        int dir = vj_tag_get_fader_direction(sample_id);

        if((dir < 0) && (opacity == 0))
        {
            int fade_method = vj_tag_get_fade_method(sample_id);
            if( fade_method == 0 )
                vj_tag_set_effect_status(sample_id,1);
            vj_tag_reset_fader(sample_id);
            if( p->pvar_.follow_fade ) {
               follow = 1;
            }
        }
        if((dir > 0) && (opacity == 255))
        {
            vj_tag_reset_fader(sample_id);
            if( p->pvar_.follow_fade ) {
               follow = 1;
            }
        }
        } else if(mode){
            if( p->pvar_.follow_fade ) {
             follow = 1;
        }
    }

    if( follow ) {
        int i;
        int tmp=0,k;
        for( i = 0; i < SAMPLE_MAX_EFFECTS - 1; i ++ ) {
            k = vj_tag_get_chain_channel(sample_id, i );
            tmp = vj_tag_get_chain_source(sample_id, i );
            if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k)) ) {
                p->pvar_.follow_now[1] = tmp;  
                p->pvar_.follow_now[0] = k;
                break;
            }
        }
    }
    return follow;
}

int vj_perform_queue_audio_chunk_ext(
    veejay_t *info,
    int client_frames_to_write,
    long long target_frame,
    int fade_in,
    uint8_t *audio_payload_chunk
) {
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
    video_playback_setup *settings = info->settings;
    
    int speed = settings->current_playback_speed;
    int num_samples = 0;

    num_samples = vj_perform_queue_audio_frame(info,(void*) p, audio_payload_chunk,speed, target_frame, info->uc->sample_id);

    return num_samples;
}

static int32_t read_sample(const uint8_t *buf, int bytes_per_sample) {
    int32_t s = 0;

    switch (bytes_per_sample) {
        case 1: s = (int8_t)buf[0]; break;
        case 2: s = (int16_t)(buf[0] | (buf[1] << 8)); break;
        case 3:
            s = buf[0] | (buf[1] << 8) | (buf[2] << 16);
            if (s & 0x800000) s |= ~0xFFFFFF;
            break;
        case 4:
            s = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
            break;
        default: s = 0; break;
    }

    return s;
}

static void write_sample(uint8_t *buf, int bytes_per_sample, int32_t s) {
    int32_t max_val = (1 << (bytes_per_sample*8 - 1)) - 1;
    int32_t min_val = -(1 << (bytes_per_sample*8 - 1));
    if (s > max_val) s = max_val;
    if (s < min_val) s = min_val;

    switch (bytes_per_sample) {
        case 1: buf[0] = (uint8_t)s; break;
        case 2:
            buf[0] = s & 0xFF;
            buf[1] = (s >> 8) & 0xFF;
            break;
        case 3:
            buf[0] = s & 0xFF;
            buf[1] = (s >> 8) & 0xFF;
            buf[2] = (s >> 16) & 0xFF;
            break;
        case 4:
            {
                uint32_t u = (uint32_t)s;
                buf[0] = u & 0xFF;
                buf[1] = (u >> 8) & 0xFF;
                buf[2] = (u >> 16) & 0xFF;
                buf[3] = (u >> 24) & 0xFF;
            }
            break;
        default: break;
    }
}

#define POST_MIX_TRIM 0.70710678f   /* -3 dB headroom */

int vj_audio_crossfade_buffers( // FIXME optimize
    performer_global_t *g,
    const uint8_t *buf_a,
    const uint8_t *buf_b,
    uint8_t *out,
    int num_samples,
    int num_channels,
    int bytesperframe,
    float t,
    float opacity_a,
    float opacity_b
) {

    if (num_samples <= 0)
        return 0;

    t = fminf(fmaxf(t, 0.0f), 1.0f);

    int bps = bytesperframe / num_channels; // bytes per sample

    const int lut_max = (int)ceil(num_samples) - 1;
    const float fidx = t * (float)lut_max;
    const int idx0 = (int)fidx;
    const int idx1 = (idx0 < lut_max) ? idx0 + 1 : idx0;
    const float frac = fidx - (float)idx0;

    const float g_out =
        g->gain_lut[0][idx0] +
        frac * (g->gain_lut[0][idx1] - g->gain_lut[0][idx0]);

    const float g_in  =
        g->gain_lut[1][idx0] +
        frac * (g->gain_lut[1][idx1] - g->gain_lut[1][idx0]);

    const float gain_a = g_out * opacity_a;
    const float gain_b = g_in  * opacity_b;

    const float max_val =
        (bps == 1) ? 127.0f :
        (bps == 2) ? 32767.0f :
        (bps == 3) ? 8388607.0f :
                     2147483647.0f;

    for (int i = 0; i < num_samples; i++) {
        for (int ch = 0; ch < num_channels; ch++) {
            const int off = (i * num_channels + ch) * bps;

            float sa = buf_a ? (float)read_sample(&buf_a[off], bps) : 0.0f;
            float sb = buf_b ? (float)read_sample(&buf_b[off], bps) : 0.0f;

            if (bps == 1) {
                sa -= 128.0f;
                sb -= 128.0f;
            }

            float mixed = (sa * gain_a + sb * gain_b) * POST_MIX_TRIM;

            if (mixed > max_val) mixed = max_val;
            else if (mixed < -max_val) mixed = -max_val;

            int32_t out_val =
                (bps == 1) ? (int32_t)(mixed + 128.0f)
                           : (int32_t)mixed;

            write_sample(&out[off], bps, out_val);
        }
    }

    return num_samples;
}


// vj_perform_queue_audio_frame
int vj_perform_queue_audio_chunk_crossfade(
    veejay_t *info,
    int client_frames_to_write,
    long long target_frame_a,
    long long target_frame_b,
    uint8_t *audio_payload_chunk,
    int sample_b,
    long long trans_start_frame,
    long long trans_end_frame
) {
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
    performer_t *q = g->B;
    editlist *el = info->current_edit_list;

    const int bps = el->audio_bps;
    const int num_channels = el->audio_chans;
    const int speed_a = info->settings->current_playback_speed;
    const int speed_b = sample_get_speed(sample_b);

    int num_samples_a =
        vj_perform_queue_audio_frame(info, (void*)p,
                                    p->top_audio_buffer,
                                     speed_a,
                                     target_frame_a,
                                     info->uc->sample_id);


    vj_audio_consume_chain(info,p->top_audio_buffer,num_samples_a);                                 

    int num_samples_b =
        vj_perform_queue_audio_frame(info, (void*)q,
                                     q->top_audio_buffer,
                                     speed_b,
                                     target_frame_b,
                                     sample_b);

    int num_samples = (num_samples_a < num_samples_b)
                        ? num_samples_a
                        : num_samples_b;

    if (num_samples > client_frames_to_write)
        num_samples = client_frames_to_write;

    if (num_samples <= 0)
        return 0;

    double trans_len = (double)(trans_end_frame - trans_start_frame);
    if (trans_len <= 0.0)
        trans_len = 1.0;

    const double absolute_frame =
        (double)target_frame_a;

    float t = (float)((absolute_frame - trans_start_frame) / trans_len);
    t = fminf(fmaxf(t, 0.0f), 1.0f);

    if (absolute_frame >= (double)trans_end_frame) {

        if (num_samples > 0) {
            veejay_msg(VEEJAY_MSG_DEBUG,
                "[AudioMix] SWITCH a=%lld b=%lld gain A=0.00 gain B=1.00",
                (long long)absolute_frame,
                target_frame_b);
        }

        vj_audio_crossfade_buffers(
            g,
            NULL,
            (speed_b != 0) ? q->top_audio_buffer : NULL,
            audio_payload_chunk,
            num_samples,
            num_channels,
            bps,
            1.0f,
            0.0f,
            1.0f
        );

        return num_samples;
    }

    vj_audio_crossfade_buffers(
        g,
        (speed_a != 0) ? p->top_audio_buffer : NULL,
        (speed_b != 0) ? q->top_audio_buffer : NULL,
        audio_payload_chunk,
        num_samples,
        num_channels,
        bps,
        t,
        1.0f,
        1.0f
    );

    return num_samples;
}

static int vj_perform_queue_audio_frame_buf(veejay_t *info, performer_t *p, uint8_t *a_buf, editlist *el,int speed, long long target_frame )
{
    int num_samples = 0;
    video_playback_setup *settings = info->settings;
    if( settings->audio_mute || !el->has_audio || speed == 0 || target_frame == -1) {
        num_samples = (el->audio_rate / el->video_fps);
        int bps = el->audio_bps;
        veejay_memset( a_buf, 0, num_samples * bps);
        return num_samples;
    }
    
    if(info->audio != AUDIO_PLAY)
        return num_samples;

    if( el->has_audio )
        num_samples = vj_perform_fill_audio_buffers(info,el, a_buf, p, &(p->play_audio_sample_), target_frame);

    return num_samples;
}

int vj_perform_queue_audio_frame(veejay_t *info, void *ptr, uint8_t *a_buf, int speed, long long target_frame,int sample_id )
{
    if( info->audio == NO_AUDIO )
        return 0;
#ifdef HAVE_JACK
    editlist *el_fallback = info->current_edit_list;
    editlist *el = (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_editlist(sample_id) : info->edit_list);
    if(el == NULL)
        el = el_fallback; //safety

    video_playback_setup *settings = info->settings;
    performer_t *p = (performer_t*) ptr;

    if( settings->audio_mute || !el->has_audio || speed == 0 || target_frame == -1) {
        int num_samples = (el->audio_rate / el->video_fps);
        int bps = el->audio_bps;
        veejay_memset( a_buf, 0, num_samples * bps);
        return num_samples;
    }

    int num_samples =  (el->audio_rate/el->video_fps);
    int pred_len = num_samples;
    int bps     =   el->audio_bps;

    if (info->audio == AUDIO_PLAY)
    {
        switch (info->uc->playback_mode)
        {
            case VJ_PLAYBACK_MODE_SAMPLE:
                if( el->has_audio )
                    num_samples = vj_perform_fill_audio_buffers(info,el, a_buf, p, &(p->play_audio_sample_), target_frame);
                break;
            case VJ_PLAYBACK_MODE_PLAIN:
                if( el->has_audio )
                    num_samples = vj_perform_fill_audio_buffers(info,el, a_buf, p, &(p->play_audio_sample_), target_frame);
                break;
            case VJ_PLAYBACK_MODE_TAG:
                if(el->has_audio)
                {
                    num_samples = vj_tag_get_audio_frame(info->uc->sample_id, a_buf);
                }
                break;
        }

        if(num_samples <= 0 )
        {
            num_samples = pred_len;
            veejay_memset(a_buf, 0, num_samples * bps );
        }
  
        return num_samples;
     }  
#endif
    return 0;
}


// FX audio chain processing
void vj_produce_audio_chain(veejay_t *info, int sample_id) {
    performer_global_t *g = (performer_global_t*) info->performer;
    if (info->uc->playback_mode != VJ_PLAYBACK_MODE_SAMPLE) return;

    sample_eff_chain **chain = sample_get_effect_chain(sample_id);
    if (chain == NULL) return;

    audio_chain_buffer_t *current_buf = &g->audio_chain_buffers[g->audio_chain_index];
    int changed = 0;
    int requested_active_count = 0;

    for (int i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
        if (chain[i] && chain[i]->e_flag == 1 && chain[i]->channel > 0)
            requested_active_count++;
    }

    if (requested_active_count != current_buf->count) {
        changed = 1;
    } else {
        int current_entry_idx = 0;
        for (int i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
            if (!chain[i] || chain[i]->e_flag != 1 || chain[i]->channel <= 0) continue;

            audio_chain_entry_t *curr = &current_buf->entries[current_entry_idx];
            
            if (curr->sample_id   != chain[i]->channel || 
                curr->sample_type != chain[i]->source_type ||
                curr->opacity     != chain[i]->audio_opacity) 
            {
                changed = 1;
                break;
            }
            current_entry_idx++;
        }
    }

    if (!changed) return;

    int target_idx = 1 - g->audio_chain_index;
    audio_chain_buffer_t *buf = &g->audio_chain_buffers[target_idx];

    int expected = AC_STATE_IDLE;
    if (!__atomic_compare_exchange_n(&buf->state, &expected, AC_STATE_PRODUCING,
                                   false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return; 
    }

    buf->count = 0;

    for (int i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
        if (!chain[i] || chain[i]->e_flag != 1 || chain[i]->channel <= 0) continue;
        if (chain[i]->source_type != VJ_PLAYBACK_MODE_SAMPLE) continue;

        audio_chain_entry_t *dst = &buf->entries[buf->count];
        int sample_i[6];

        if (sample_get_long_info(chain[i]->channel, &sample_i[0], &sample_i[1], 
            &sample_i[2], &sample_i[3], &sample_i[4], &sample_i[5]) == 0) 
        {
            dst->sample_type = chain[i]->source_type;
            dst->sample_id   = chain[i]->channel;
            dst->opacity     = chain[i]->audio_opacity;

            dst->start    = sample_i[0];
            dst->end      = sample_i[1];
            dst->loopmode = sample_i[2];
            dst->speed    = sample_i[3];
            dst->cur_sfd  = sample_i[4];
            dst->max_sfd  = sample_i[5];
            dst->el       = sample_get_editlist(dst->sample_id);
            dst->offset   = sample_get_resume(dst->sample_id);
            buf->count++;
        }
    }

    atomic_store_int(&buf->state, AC_STATE_READY);
}

static void vj_audio_load_to_bus(
    float *dest_bus, 
    const uint8_t *src, 
    int num_samples, 
    int num_channels, 
    int bps
) {
    const int total_elements = num_samples * num_channels;

    for (int i = 0; i < total_elements; i++) {
        const int off = i * bps;
        float s = (float)read_sample(&src[off], bps);

        if (bps == 1) {
            s -= 128.0f;
        }

        dest_bus[i] = s;
    }
}
static void vj_audio_accumulate_to_bus(
    float *bus, 
    const uint8_t *in, 
    int num_samples, 
    int num_channels, 
    int bps, 
    float opacity
) {
    const int total_elements = num_samples * num_channels;

    for (int i = 0; i < total_elements; i++) {
        const int off = i * bps;
        
        float s_new = (float)read_sample(&in[off], bps);
        
        if (bps == 1) {
            s_new -= 128.0f;
        }

        bus[i] += (s_new * opacity);
    }
}


// fxchain down mixing
static void vj_audio_finalize_mix(
    uint8_t *dest, 
    const float *mix_bus, 
    int num_samples, 
    int num_channels, 
    int bps
) {
    const float max_val =
        (bps == 1) ? 127.0f :
        (bps == 2) ? 32767.0f :
        (bps == 3) ? 8388607.0f :
                     2147483647.0f;

    const int total_elements = num_samples * num_channels;

    for (int i = 0; i < total_elements; i++) {
        const int off = i * bps;
        float mixed = mix_bus[i] * POST_MIX_TRIM;

        if (mixed > max_val) {
            mixed = max_val;
        } else if (mixed < -max_val) {
            mixed = -max_val;
        }

        int32_t out_val;
        if (bps == 1) {
            out_val = (int32_t)(mixed + 128.0f);
        } else {
            out_val = (int32_t)mixed;
        }

        write_sample(&dest[off], bps, out_val);
    }

}

// FX Chain down mixing, we can do it with this consumer/producer pattern,
// the audio thread will drive it so, the producer pushes a sample_eff_chain as a job description
// and the audio thread will drive it
// FIXME not fully implemented yet
void vj_audio_consume_chain(veejay_t *info, uint8_t *audio_chunk, int in_samples) {
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    int next_idx = 1 - g->audio_chain_index;
    audio_chain_buffer_t *next_buf = &g->audio_chain_buffers[next_idx];
    int expected = AC_STATE_READY;
    int chans = info->current_edit_list->audio_chans;
    int bytesperframe = info->current_edit_list->audio_bps;
    int bps1 = bytesperframe / chans;

    if (__atomic_compare_exchange_n(&next_buf->state, &expected, AC_STATE_CONSUMING,
                                   false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) 
    {
        atomic_store_int(&g->audio_chain_buffers[g->audio_chain_index].state, AC_STATE_IDLE);
        g->audio_chain_index = next_idx;
    }
    audio_chain_buffer_t *buf = &g->audio_chain_buffers[g->audio_chain_index];

    if (buf->state != AC_STATE_CONSUMING) {
        return;
    }
    
    float *mix_bus = g->accum[g->audio_chain_index];
    vj_audio_load_to_bus(mix_bus, audio_chunk, in_samples, chans, bps1);

    for (int i = 0; i < buf->count; i++) {
        audio_chain_entry_t *entry = &buf->entries[i];

        if (entry->sample_id == info->uc->sample_id) {
            continue;
        }
        if (entry->sample_type != VJ_PLAYBACK_MODE_SAMPLE) continue;

        if(entry->sample_id <= 0) continue;

        int num_samples = vj_perform_queue_audio_frame_buf(
            info, p, entry->buffer, entry->el, entry->speed, entry->offset
        );

        if (num_samples > 0) {
            
            int num_channels = entry->el->audio_chans;
            int bps = entry->el->audio_bps / num_channels;
            
            vj_audio_accumulate_to_bus(
                mix_bus, 
                entry->buffer, 
                num_samples, 
                chans, 
                bps, 
                entry->opacity
            );

            entry->offset = vj_calc_next_sub_audioframe(info, entry->sample_id, entry);
        }
    }

    vj_audio_finalize_mix(audio_chunk, mix_bus, in_samples, chans, bps1);
}


int vj_perform_get_width( veejay_t *info )
{
    return info->video_output_width;
}

int vj_perform_get_height( veejay_t *info )
{
    return info->video_output_height;
}

// OSD strings print useful information about the build
static char *vj_perform_print_credits(veejay_t *info)
{
    char text[2048];
    char arch[256] = "";
    char simd[512] = "";
    size_t pos;

    pos = 0;
#ifdef ARCH_MIPS
    pos += snprintf(arch + pos, sizeof(arch) - pos, "MIPS ");
#endif
#ifdef ARCH_PPC
    pos += snprintf(arch + pos, sizeof(arch) - pos, "PPC ");
#endif
#ifdef ARCH_X86_64
    pos += snprintf(arch + pos, sizeof(arch) - pos, "X86_64 ");
#endif
#ifdef ARCH_X86
    pos += snprintf(arch + pos, sizeof(arch) - pos, "X86 ");
#endif
#ifdef HAVE_ARM_ASIMD
    pos += snprintf(arch + pos, sizeof(arch) - pos, "ARM ASIMD ");
#endif
#ifdef HAVE_ARM_NEON
    pos += snprintf(arch + pos, sizeof(arch) - pos, "ARM NEON ");
#endif
#ifdef HAVE_ARM
    pos += snprintf(arch + pos, sizeof(arch) - pos, "ARM ");
#endif
#ifdef HAVE_ARMV7A
    pos += snprintf(arch + pos, sizeof(arch) - pos, "ARMv7A ");
#endif
#ifdef HAVE_DARWIN
    pos += snprintf(arch + pos, sizeof(arch) - pos, "Darwin ");
#endif
#ifdef HAVE_PS2
    pos += snprintf(arch + pos, sizeof(arch) - pos, "PS2 ");
#endif

    pos = 0;
#ifdef HAVE_ALTIVEC
    pos += snprintf(simd + pos, sizeof(simd) - pos, "Altivec ");
#endif
#ifdef HAVE_ASM_SSE
    pos += snprintf(simd + pos, sizeof(simd) - pos, "SSE ");
#endif
#ifdef HAVE_ASM_SSE2
    pos += snprintf(simd + pos, sizeof(simd) - pos, "SSE2 ");
#endif
#ifdef HAVE_ASM_SSE4_1
    pos += snprintf(simd + pos, sizeof(simd) - pos, "SSE4.1 ");
#endif
#ifdef HAVE_ASM_SSE4_2
    pos += snprintf(simd + pos, sizeof(simd) - pos, "SSE4.2 ");
#endif
#ifdef HAVE_ASM_MMX
    pos += snprintf(simd + pos, sizeof(simd) - pos, "MMX ");
#endif
#ifdef HAVE_ASM_MMXEXT
    pos += snprintf(simd + pos, sizeof(simd) - pos, "MMXEXT ");
#endif
#ifdef HAVE_ASM_MMX2
    pos += snprintf(simd + pos, sizeof(simd) - pos, "MMX2 ");
#endif
#ifdef HAVE_ASM_3DNOW
    pos += snprintf(simd + pos, sizeof(simd) - pos, "3DNow ");
#endif
#ifdef HAVE_ASM_AVX
    pos += snprintf(simd + pos, sizeof(simd) - pos, "AVX ");
#endif
#ifdef HAVE_ASM_AVX2
    pos += snprintf(simd + pos, sizeof(simd) - pos, "AVX2 ");
#endif
#ifdef HAVE_ASM_AVX512
    pos += snprintf(simd + pos, sizeof(simd) - pos, "AVX512 ");
#endif

    snprintf(text, sizeof(text),
        "This is Veejay %s\n"
        "%s\n\n"
        "%-15s: %s\n"
        "%-15s: %s\n"
        "%-15s: %s\n"
        "%-15s: %d bytes\n"
        "%-15s: %d bytes\n"
        "%-15s: %s\n"
        "%-15s: %s\n",
        VERSION,
        intro,
        "Build OS", BUILD_OS,
        "Kernel", BUILD_KERNEL,
        "Machine", BUILD_MACHINE,
        "CPU Cache", cpu_get_cacheline_size(),
        "Mem Align", mem_align_size(),
        "Architecture", arch,
        "SIMD/Ext", simd
    );

    return vj_strdup(text);
}

static char *osd_drift_indicator(double drift_s, double spvf)
{
    static char buf[32];
    static double ema_drift = 0.0;
    static int initialized = 0;

    const int len = 13;
    const int center = len / 2;

    const double alpha = 0.12;
    const double deadzone = 0.02 * spvf;

    if (!initialized) {
        ema_drift = drift_s;
        initialized = 1;
    } else {
        ema_drift = alpha * drift_s + (1.0 - alpha) * ema_drift;
    }

    double display = ema_drift;

    if (display > -deadzone && display < deadzone)
        display = 0.0;

    double drift_frames = display / spvf;
    double abs_frames = fabs(drift_frames);

    const double range_frames = 0.5;

    if (drift_frames >  range_frames) drift_frames =  range_frames;
    if (drift_frames < -range_frames) drift_frames = -range_frames;

    int bars = (int)(drift_frames / range_frames * center);

    char fill = '#';
    if (abs_frames > 0.5)
        fill = '!';
    else if (abs_frames > 0.25)
        fill = '+';

    for (int i = 0; i < len; i++)
        buf[i] = '-';

    buf[center] = '|';

    if (bars > 0) {
        for (int i = 1; i <= bars && center + i < len; i++)
            buf[center + i] = fill;
    }
    else if (bars < 0) {
        for (int i = -1; i >= bars && center + i >= 0; i--)
            buf[center + i] = fill;
    }

    buf[len] = '\0';

    snprintf(buf + len, sizeof(buf) - len,
             " %+0.2f", drift_frames);

    return buf;
}

// OSD status strings
static char *osd_performance_indicator(double render_duration, double spvf) {
    static char buf[128];
    static double ema_duration = -1.0;
    static double ema_load_pct = -1.0;

    const double alpha_dur  = 0.15;
    const double alpha_load = 0.25;

    if (ema_duration < 0) {
        ema_duration = render_duration;
        ema_load_pct = (render_duration / spvf) * 100.0;
    } else {
        ema_duration = alpha_dur * render_duration + (1.0 - alpha_dur) * ema_duration;
        double raw_load = (ema_duration / spvf) * 100.0;
        ema_load_pct = alpha_load * raw_load + (1.0 - alpha_load) * ema_load_pct;
    }

    const double skip_threshold = 1.5 * spvf;
    const char *status;
    if (ema_duration > skip_threshold) status = "OVLD";
    else if (ema_duration > spvf)      status = "LAG";
    else if (ema_duration > 0.85*spvf) status = "WARN";
    else                               status = " OK";

    char spark[11];
    int bars = (int)((ema_load_pct + 5) / 10.0);
    if (bars > 10) bars = 10;
    if (bars < 0)  bars = 0;
    for (int i = 0; i < 10; i++) spark[i] = (i < bars) ? '#' : '-';
    spark[10] = '\0';

    snprintf(buf, sizeof(buf),
             "%-4s [%s] %2.4fs (%3.0f%%)",
             status, spark, ema_duration, ema_load_pct);

    return buf;
}
// OSD status strings
static char *osd_xrun_indicator(long underruns, long xruns)
{
    static char buf[16];
    static long last_u = 0;
    static long last_x = 0;
    static double severity = 0.0;

    const int len = 5;
    const double decay = 0.95;

    long du = underruns - last_u;
    long dx = xruns - last_x;

    last_u = underruns;
    last_x = xruns;

    severity += du * 0.3;
    severity += dx * 2.0;
    severity *= decay;

    if (severity > len)
        severity = len;

    int bars = (int)(severity + 0.5);

    buf[0] = '[';
    for (int i = 0; i < len; i++)
    {
        if (i < bars)
        {
            if (severity > 3.5)
                buf[i + 1] = '!';// critical zone
            else if (severity > 1.5)
                buf[i + 1] = '+';
            else
                buf[i + 1] = '#';
        }
        else
            buf[i + 1] = '-';
    }
    buf[len + 1] = ']';
    buf[len + 2] = '\0';

    return buf;
}

/*

spvf = seconds per video frame

cpu performance:

" OK  [###-------] 0.0330s (33%)"  -> normal load, under 85% spvf
"WARN [#####-----] 0.0420s (88%)"  -> approaching full load
"LAG  [#######---] 0.0500s (105%)" -> load > spvf
"OVLD [##########] 0.0800s (200%)" -> load is more than 1.5 * spvf

xrun/underrun audio indicator:

"[#----]" -> minor underrun, severity low
"[##---]" -> moderate severity
"[+##--]" -> above warning threshold
"[!!+++]" -> critical, repeated XRUNs


OSD string layout:

00:08:34:05 |08:34:05:12 |Sample  5/ 20 |Frame  012345 Skipped   12 Speed 1.00x +#--- LAG [######----] 0.0450s (113%)

*/
static char *vj_perform_osd_status(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    video_playback_stats *stats = &info->stats;

    char *more = NULL;
    if (info->composite) {
        void *vp = composite_get_vp(info->composite);
        if (viewport_get_mode(vp) == 1)
            more = viewport_get_my_status(vp);
    }

    MPEG_timecode_t tc;
    veejay_memset(&tc, 0, sizeof(tc));

    y4m_ratio_t ratio = mpeg_conform_framerate((double)info->current_edit_list->video_fps);
    int n = mpeg_framerate_code(ratio);

    mpeg_timecode(&tc, stats->current_frame, n, info->current_edit_list->video_fps);
    char timecode[20];
    snprintf(timecode, sizeof(timecode), "%02d:%02d:%02d:%02d", tc.h, tc.m, tc.s, tc.f);

    mpeg_timecode(&tc, stats->total_frames_produced, n, info->current_edit_list->video_fps);
    char master_timecode[20];
    snprintf(master_timecode, sizeof(master_timecode), "%02d:%02d:%02d:%02d", tc.h, tc.m, tc.s, tc.f);

    float speed = settings->current_playback_speed;
    if(info->sfd) {
        speed = 1.0 / info->sfd;
    }

    long ur = 0;
#ifdef HAVE_JACK
    ur = (info->audio == AUDIO_PLAY ? vj_jack_underruns() : 0);
#endif

    char *audio_info = osd_xrun_indicator( ur,stats->xruns);

    const char *mode_str = "Plain";
    switch (info->uc->playback_mode) {
        case VJ_PLAYBACK_MODE_SAMPLE: mode_str = "Sample"; break;
        case VJ_PLAYBACK_MODE_TAG:    mode_str = "Tag";    break;
    }

    char buf[512];
    if(info->video_output_width < 1920)
        snprintf(buf, sizeof(buf),
            "%s |%s |%-5s %3d/%3d |Frame %7lld\nSkipped %4lld "
            "Speed %2.2fx %s %s\n%s",
            master_timecode,
            timecode,
            mode_str,
            info->uc->sample_id,
            (mode_str[0] == 'S') ? sample_size() :
            (mode_str[0] == 'T') ? vj_tag_size() : 1,
            (long long)stats->current_frame,
            (long long)stats->total_frames_skipped,
            speed,
            osd_drift_indicator(stats->delta_s, settings->spvf),
            audio_info,
            osd_performance_indicator(stats->render_duration, settings->spvf)
        );
    else
        snprintf(buf, sizeof(buf),
            "%12s |%12s |%-5s %3d/%3d |Frame %07lld Skipped %04lld "
            "Speed %2.2fx %s %s %s",
            master_timecode,
            timecode,
            mode_str,
            info->uc->sample_id,
            (mode_str[0] == 'S') ? sample_size() :
            (mode_str[0] == 'T') ? vj_tag_size() : 1,
            (long long)stats->current_frame,
            (long long)stats->total_frames_skipped,
            speed,
            osd_drift_indicator(stats->delta_s, settings->spvf),
            audio_info,
            osd_performance_indicator(stats->render_duration, settings->spvf)
        );

    int total_len = strlen(buf) + 1 + (more ? strlen(more) + 1 : 0);
    char *status_str = (char *)vj_malloc(total_len);
    if (!status_str) {
        if (more) free(more);
        return NULL;
    }

    if (more) {
        snprintf(status_str, total_len, "%s | %s", more, buf);
        free(more);
    } else {
        strncpy(status_str, buf, total_len);
    }

    return status_str;
}

static  void    vj_perform_render_osd( veejay_t *info, video_playback_setup *settings,VJFrame *frame )
{
    if(info->use_osd <= 0 ) 
        return;

    if( !frame->ssm) //FIXME: this is costly just to render OSD
    {
        chroma_supersample(
            settings->sample_mode,
            frame,
            frame->data );
        vj_perform_set_444(frame);
    }
    
    char *osd_text = NULL;
    int placement = 0;
    if( info->use_osd == 2 ) {
        placement = 1;
        osd_text = vj_perform_print_credits(info);
    } else if (info->use_osd == 1 ) {
        osd_text = vj_perform_osd_status(info);
    } else if (info->use_osd == 3 && info->composite ) {
        placement = 1;
        osd_text = viewport_get_my_help( composite_get_vp(info->composite ) );
    }
    if(osd_text) {
        vj_font_render_osd_status(info->osd,frame,osd_text,placement);
        free(osd_text);
    }
}

static inline void vj_fx_hold_update(veejay_t *info, VJFrame *current_fx)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    video_playback_setup *s = info->settings;

    uint8_t slot = 5; // fx latch buffer index in primary_buffers

    if (!s->hold_fx) {
        s->hold_fx_prev = 0;
        return;
    }

    int plane_sizes[4] = {
        current_fx->len, current_fx->uv_len, current_fx->uv_len, current_fx->stride[3] * current_fx->height
    };

    if (!s->hold_fx_prev && s->hold_fx) {
        
        uint8_t *pri5[4] = {
            p->primary_buffer[slot]->Y,
            p->primary_buffer[slot]->Cb,
            p->primary_buffer[slot]->Cr,
            p->primary_buffer[slot]->alpha
        };

        vj_frame_copy(current_fx->data, pri5, plane_sizes);
        if(current_fx->ssm) {
            chroma_subsample(info->settings->sample_mode, current_fx, pri5);
        }
    }

    // steady hold
    //vj_frame_copy(p->primary_buffer[slot], p->primary_buffer[info->out_buf],plane_sizes );

    s->hold_fx_prev = 1;
}

static  void    vj_perform_finish_chain( veejay_t *info,performer_t *p,VJFrame *frame, int sample_id, int source_type )
{
    int result = 0;

    if(source_type == VJ_PLAYBACK_MODE_TAG )
    {
        result = vj_perform_post_chain_tag(info,p, frame, sample_id);
    }
    else if(source_type == VJ_PLAYBACK_MODE_SAMPLE )
    {
        result = vj_perform_post_chain_sample(info,p, frame, sample_id);
    }

    if( result ) {
        p->pvar_.follow_run = result;
    }
}

static  void    vj_perform_finish_render( veejay_t *info,performer_t *p,VJFrame *frame, video_playback_setup *settings )
{
    uint8_t *pri[4];
    char *osd_text = NULL;
    char *more_text = NULL;
    int   placement= 0;

    pri[0] = p->primary_buffer[0]->Y;
    pri[1] = p->primary_buffer[0]->Cb;
    pri[2] = p->primary_buffer[0]->Cr;
    pri[3] = p->primary_buffer[0]->alpha;

    if( settings->composite  )
    { //@ scales in software
        if( settings->ca ) {
            settings->ca = 0;
        }
        //@ focus on projection screen
        if(composite_event( info->composite, pri, info->uc->mouse[0],info->uc->mouse[1],info->uc->mouse[2], 
            vj_perform_get_width(info), vj_perform_get_height(info),info->homedir,info->uc->playback_mode,info->uc->sample_id ) ) {
#ifdef HAVE_SDL
            if( info->video_out == 0 ) {
                //@ release focus
                vj_sdl_grab( info->sdl, 0 );
            }
#endif
        }

#ifdef HAVE_SDL
        if( info->use_osd == 2 ) {
            osd_text = vj_perform_print_credits(info);  
            placement= 1;
        } else if (info->use_osd == 1 ) {
            osd_text = vj_perform_osd_status(info);
            placement= 0;
        } else if (info->use_osd == 3 && info->composite ) {
            placement = 1;
            osd_text = viewport_get_my_help( composite_get_vp(info->composite ) );
            more_text = vj_perform_osd_status(info);
        }
#endif
    }

    if( settings->composite  ) {
        VJFrame out;
        veejay_memcpy( &out, info->effect_frame1, sizeof(VJFrame));
        int curfmt = out.format;
        if( out.ssm ) 
        {
            out.format = (info->pixel_format == FMT_422F ? PIX_FMT_YUVJ444P : PIX_FMT_YUV444P );
        }

        if(!frame->ssm) {
              chroma_supersample(settings->sample_mode,frame,pri );
              vj_perform_set_444(frame);
        }

        composite_process(info->composite,&out,info->effect_frame1,settings->composite,frame->format); 

        if( settings->splitscreen ) {
            composite_process_divert(info->composite,out.data,frame, info->splitter, settings->composite );
        }

        if(osd_text ) {
            VJFrame *tst = composite_get_draw_buffer( info->composite );
            if(tst) {
                    if( info->use_osd == 3 ) {  
                    vj_font_render_osd_status(info->osd,tst,osd_text,placement);
                    if(more_text)
                        vj_font_render_osd_status(info->osd,tst,more_text,0);
                }
                free(tst);
            }
        } 

        if( frame->ssm ) {
            frame->uv_len = frame->uv_width * frame->uv_height;
            frame->format = curfmt;
        }
    } 
    else {

        if(settings->splitscreen ) {
            if(!frame->ssm) {
              chroma_supersample(settings->sample_mode,frame,pri );
              vj_perform_set_444(frame);
            }
            vj_split_process( info->splitter, frame );
        }

        if( osd_text ) 
            vj_font_render_osd_status(info->osd,frame,osd_text,placement);
        if(more_text)
            vj_font_render_osd_status(info->osd,frame,more_text,0);
    }

    if( osd_text)
        free(osd_text);
    if( more_text)  
        free(more_text);

    if(!settings->composite && info->uc->mouse[0] > 0 && info->uc->mouse[1] > 0) 
    {
        if( info->uc->mouse[2] == 1 ) {
            uint8_t y,u,v,r,g,b;

            y = pri[0][ info->uc->mouse[1] * frame->width + info->uc->mouse[0] ];
            if( frame->ssm == 1 ) {
                u = pri[1][ info->uc->mouse[1] * frame->width + info->uc->mouse[0] ];
                v = pri[2][ info->uc->mouse[1] * frame->width + info->uc->mouse[0] ];
            }
            else {
                u = pri[1][ info->uc->mouse[1] * frame->uv_width + (info->uc->mouse[0]>>1) ];
                v = pri[2][ info->uc->mouse[1] * frame->uv_width + (info->uc->mouse[0]>>1) ];
            }
        
            r = y + (1.370705f * ( v- 128 ));
            g = y - (0.698001f * ( v - 128)) - (0.337633 * (u-128));
            b = y + (1.732446f * ( u - 128 ));

            if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
                int pos = sample_get_selected_entry(info->uc->sample_id);
                int fx_id = sample_get_effect( info->uc->sample_id,pos);
                if( vje_has_rgbkey( fx_id ) ) {
                    sample_set_effect_arg( info->uc->sample_id, pos, 1, r );
                    sample_set_effect_arg( info->uc->sample_id, pos, 2, g );
                    sample_set_effect_arg( info->uc->sample_id, pos, 3, b );
                    veejay_msg(VEEJAY_MSG_INFO,"Selected RGB color #%02x%02x%02x",r,g,b);
                }
            }
            else if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
                int pos = vj_tag_get_selected_entry(info->uc->sample_id);
                int fx_id = vj_tag_get_effect( info->uc->sample_id, pos );
                if( vje_has_rgbkey( fx_id ) ) {
                    vj_tag_set_effect_arg( info->uc->sample_id,pos,1,r);
                    vj_tag_set_effect_arg( info->uc->sample_id,pos,2,g);
                    vj_tag_set_effect_arg( info->uc->sample_id,pos,3,b);    
                    veejay_msg(VEEJAY_MSG_INFO,"Selected RGB color #%02x%02x%02x",r,g,b);
                }
            }
        }

        if( info->uc->mouse[2] == 2 ) {
            info->uc->drawmode = !info->uc->drawmode;
        }

        if( info->uc->mouse[2] == 0 && info->uc->drawmode ) {
            int x1 = info->uc->mouse[0] - info->uc->drawsize;
            int y1 = info->uc->mouse[1] - info->uc->drawsize;
            int x2 = info->uc->mouse[0] + info->uc->drawsize;
            int y2 = info->uc->mouse[1] + info->uc->drawsize;
    
            if( x1 < 0 ) x1 = 0; else if ( x1 > frame->width ) x1 = frame->width;
            if( y1 < 0 ) y1 = 0; else if ( y1 > frame->height ) y1 = frame->height;
            if( x2 < 0 ) x2 = 0; else if ( x2 > frame->width ) x2 = frame->width;
            if( y2 < 0 ) y2 = 0; else if ( y2 > frame->height ) y2 = frame->height;
    
            unsigned int i,j;
            for( j = x1; j < x2 ; j ++ )
                pri[0][ y1 * frame->width + j ] = 0xff - pri[0][y1 * frame->width + j];
    
            for( i = y1; i < y2; i ++ ) 
            {
                pri[0][ i * frame->width + x1 ] = 0xff - pri[0][i * frame->width + x1];
                pri[0][ i * frame->width + x2 ] = 0xff - pri[0][i * frame->width + x2];
            }

            for( j = x1; j < x2 ; j ++ )
                pri[0][ y2 * frame->width + j ] = 0xff - pri[0][y2*frame->width+j];
        }
    }
}

static  void    vj_perform_render_font( veejay_t *info, video_playback_setup *settings, VJFrame *frame )
{
#ifdef HAVE_FREETYPE
    long long cur_frame = atomic_load_long_long(&settings->current_frame_num);
    int n = vj_font_norender( info->font, cur_frame );
    if( n > 0 )
    {
        if( !frame->ssm )
        {
            chroma_supersample(
                settings->sample_mode,
                frame,
                frame->data );
            vj_perform_set_444(frame);
        }
        vj_font_render( info->font, frame , cur_frame );
    }
#endif
}

static  void    vj_perform_record_frame( veejay_t *info )
{
    if( info->settings->offline_record )
    {
        vj_perform_record_offline_tag_frame(info);
    }

    if( info->seq->active && info->seq->rec_id > 0 ) {
        vj_perform_record_sample_frame(info,info->seq->rec_id, 0);
    }
    else {

        if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG && info->settings->tag_record )
            vj_perform_record_tag_frame(info);
        else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE && info->settings->sample_record )
            vj_perform_record_sample_frame(info, info->uc->sample_id,0 );
    }
}

void    vj_perform_record_video_frame(veejay_t *info)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
    if( p->pvar_.enc_active )
        vj_perform_record_frame(info);
}

void    vj_perform_reset_transition(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *A = g->A;
    performer_t *B = g->B;

    settings->transition.shape = -1;
    settings->transition.skip_audio_edge = 1;

    atomic_store_int(&settings->transition.active,0 );
    atomic_store_long_long(&settings->transition.start,0);
    atomic_store_long_long(&settings->transition.end, 0);

    if( info->audio != NO_AUDIO ) {
        atomic_store_int(&A->audio_edge->pending_edge, AUDIO_EDGE_NONE );
        atomic_store_int(&B->audio_edge->pending_edge, AUDIO_EDGE_NONE );
    }

    sample_b_t *sb = &A->sample_b;
    long long sample_b_pos = atomic_load_long_long(&sb->offset);
    long long start_pos = atomic_load_long_long(&sb->start);

    if(settings->transition.next_type == VJ_PLAYBACK_MODE_SAMPLE) {
        sample_set_resume_override( settings->transition.next_id,sample_b_pos + start_pos  );
    }

    //FIXME: are we resuming at the correct position ?
    //veejay_msg(VEEJAY_MSG_DEBUG, "TX completed, resume at %lld", start_pos + sample_b_pos );

    settings->transition.next_id = 0;
    settings->transition.next_type = 0;

    veejay_memset( &A->sample_b, 0 , sizeof(sample_b_t));
    veejay_memset( &B->sample_b, 0 , sizeof(sample_b_t));
}

static void vj_perform_end_transition( veejay_t *info, int mode, int sample )
{
    video_playback_setup *settings = info->settings;
    if(settings->transition.ready) {
                
        vj_perform_reset_transition(info);
        // drive the sequencer in transition path
        if(info->seq->active) { 
            int seq_idx = settings->transition.seq_index;
            if( seq_idx >= 0 && seq_idx < MAX_SEQUENCES ) {
                int playback_mode = info->seq->samples[ seq_idx ].type;
                int sample_id = info->seq->samples[ seq_idx ].sample_id;
                info->seq->current = seq_idx;
                veejay_change_playback_mode( info, playback_mode, sample_id );
            }
        }
        else {
            veejay_change_playback_mode( info, mode, sample );
        }
        settings->transition.ready = 0;
    }
}

int vj_perform_transition_sample(veejay_t *info, VJFrame *srcA, VJFrame *srcB) {
    video_playback_setup *settings = info->settings;

    long long cur_frame = atomic_load_long_long(&settings->current_frame_num);

    long long start = atomic_load_long_long(&settings->transition.start);
    long long end = atomic_load_long_long(&settings->transition.end);

    if (settings->current_playback_speed > 0) {
        settings->transition.timecode = 
            (cur_frame - start) / 
            (double)(end - start);
    } else if (settings->current_playback_speed < 0) {
        settings->transition.timecode =
            (start - cur_frame) / 
            (double)(start- end);
    }

    if (settings->transition.timecode < 0.0 || settings->transition.timecode > 1.0) {
        veejay_msg(0, "invalid transition timecode: frame %lld, transition %lld - %lld",
                   cur_frame, start, end);
        return 0;
    }

    if (!srcA->ssm) {
        chroma_supersample(settings->sample_mode, srcA, srcA->data);
        vj_perform_set_444(srcA);
    }
    if (!srcB->ssm) {
        chroma_supersample(settings->sample_mode, srcB, srcB->data);
        vj_perform_set_444(srcB);
    }


    settings->transition.ready = shapewipe_process(
        settings->transition.ptr,
        srcA, srcB,
        settings->transition.timecode,
        settings->transition.shape,
        0,
        1,
        1
    );

    vj_perform_end_transition(info, 
        (settings->transition.next_type == 0 ? VJ_PLAYBACK_MODE_SAMPLE : VJ_PLAYBACK_MODE_TAG),
        settings->transition.next_id
    );

    return 1;
}


static void vj_perform_queue_fx_entry( veejay_t *info, int sample_id, int entry_id, sample_eff_chain *entry, performer_t *p, VJFrame *a, VJFrame *b,const int is_sample, int *alpha_clear )
{
    if( entry->clear && b->stride[3] > 0 ) {
        veejay_memset( b->data[3], 0, b->stride[3] * b->height );
        entry->clear = 0;
        *alpha_clear = 1;
    }

    if(is_sample) {
        vj_perform_apply_secundary( info, p, sample_id, entry->channel, entry->source_type, entry_id, a, b, 
                p->frame_buffer[ entry_id ]->P0,
                p->frame_buffer[ entry_id ]->P1,
                1);
    }
    else {
        vj_perform_apply_secundary_tag( info, p, entry->channel, entry->source_type, entry_id, a, b, 
                p->frame_buffer[ entry_id ]->P0,
                p->frame_buffer[ entry_id ]->P1,
                0);
    }

} 

int vj_perform_queue_video_frames(veejay_t *info, VJFrame *frame, VJFrame *frame2, performer_t *p, const int sample_id, const int mode, long frame_num)
{
    sample_eff_chain **fx_chain = NULL;
    veejay_memcpy( p->tmp1, frame, sizeof(VJFrame));
    
    p->tmp1->data[0] = p->primary_buffer[0]->Y; 
    p->tmp1->data[1] = p->primary_buffer[0]->Cb;
    p->tmp1->data[2] = p->primary_buffer[0]->Cr;
    p->tmp1->data[3] = p->primary_buffer[0]->alpha;

    int is_sample = (mode == VJ_PLAYBACK_MODE_SAMPLE);

    if(mode != VJ_PLAYBACK_MODE_TAG ) {
        vj_perform_plain_fill_buffer(info,p, p->tmp1, sample_id,mode, frame_num);
        if( is_sample ) 
            fx_chain = sample_get_effect_chain( sample_id );
    }
    else {
        vj_perform_tag_fill_buffer(info, p, p->tmp1, sample_id);
        fx_chain = vj_tag_get_effect_chain( sample_id );
    }

    int alpha_clear = 0;

    if( fx_chain != NULL )
    {
        veejay_memcpy( p->tmp2, frame2, sizeof(VJFrame));

        if( info->uc->take_bg && p->chain_id == 0 ) {
            vjert_update( fx_chain, p->tmp1 );
        }
                
        for( int c = 0; c < SAMPLE_MAX_EFFECTS; c ++ ) {
            sample_eff_chain *entry = fx_chain[c];
            if( entry->e_flag == 0 || entry->effect_id <= 0 )
                continue;

            if( vje_get_extra_frame( entry->effect_id ) ) {
                p->tmp2->data[0] = p->frame_buffer[c]->Y;
                p->tmp2->data[1] = p->frame_buffer[c]->Cb;
                p->tmp2->data[2] = p->frame_buffer[c]->Cr;
                p->tmp2->data[3] = p->frame_buffer[c]->alpha;
                vj_perform_queue_fx_entry( info, sample_id, c, entry, p, p->tmp1, p->tmp2, is_sample, &alpha_clear );
            }
        }
    }

    if( info->uc->take_bg == 1 ) {
        info->uc->take_bg = 0;
    }

    if(alpha_clear) {
        veejay_memset( p->tmp1->data[3], 0, p->tmp1->stride[3] * p->tmp1->height );
    }

    return 1;
}

void vj_perform_render_video_frames(veejay_t *info, performer_t *p, vjp_kf *effect_info, const int sample_id, const int source_type, VJFrame *a, VJFrame *b, VJFrameInfo *topinfo, vjp_kf *setup)
{
    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*) info->performer;

    int is444 = 0;
    int i = 0;
    int safe_ff = p->pvar_.follow_fade;
    int safe_fv = p->pvar_.fade_value;

    veejay_memset( &(p->pvar_), 0, sizeof(varcache_t));

    p->pvar_.follow_fade = safe_ff;
    p->pvar_.fade_value = safe_fv;
    p->pvar_.fade_entry = -1;

    int cur_out = info->out_buf;

    long long cur_frame = atomic_load_long_long(&settings->current_frame_num);
    long long max_frame = atomic_load_long_long(&settings->max_frame_num);
    long long min_frame = atomic_load_long_long(&settings->min_frame_num);

    topinfo->timecode = cur_frame;
    a->ssm = 0;
    b->ssm = 0;
    a->timecode = cur_frame / (double)(max_frame - min_frame);

    for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
        p->frame_buffer[i]->ssm = 0;
    }

    a->data[0] = p->primary_buffer[0]->Y;       
    a->data[1] = p->primary_buffer[0]->Cb;
    a->data[2] = p->primary_buffer[0]->Cr;
    a->data[3] = p->primary_buffer[0]->alpha;

    vj_perform_cache_put_frame( g, info->uc->sample_id, info->uc->playback_mode, p->primary_buffer[0]);

    switch (info->uc->playback_mode)
    {
        case VJ_PLAYBACK_MODE_SAMPLE:

            sample_var( sample_id,
                        &(p->pvar_.type),
                        &(p->pvar_.fader_active),
                        &(p->pvar_.fx_status),
                        &(p->pvar_.enc_active),
                        &(p->pvar_.active),
                        &(p->pvar_.fade_method),
                        &(p->pvar_.fade_entry),
                        &(p->pvar_.fade_alpha));

            if( (info->seq->active && info->seq->rec_id) || info->settings->offline_record )
                p->pvar_.enc_active = 1;
            
            
            sample_info *si = sample_get(sample_id);
            if(si) {
                if( info->global_chain->enabled == 1) { 
                    if(p->pvar_.fx_status) vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type,setup, si->effect_chain, si);
                    vj_perform_global_chain_sync(info, info->global_chain);
                    vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,info->global_chain->fx_chain, si);
                } else if(info->global_chain->enabled == 2) {
                    vj_perform_global_chain_sync(info, info->global_chain);
                    vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,info->global_chain->fx_chain, si);
                    if(p->pvar_.fx_status) vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type,setup, si->effect_chain, si);
                } else {
                    if(p->pvar_.fx_status) vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type,setup, si->effect_chain, si);
                }
            }
        
            vj_perform_finish_chain( info,p,a,sample_id,source_type );

            vj_fx_hold_update(info, a);

            break;
            
        case VJ_PLAYBACK_MODE_PLAIN:

            if( info->settings->offline_record )
                p->pvar_.enc_active = 1;

            break;
        case VJ_PLAYBACK_MODE_TAG:

            vj_tag_var( sample_id,
                        &(p->pvar_.type),
                        &(p->pvar_.fader_active),
                        &(p->pvar_.fx_status),
                        &(p->pvar_.enc_active),
                        &(p->pvar_.active),
                        &(p->pvar_.fade_method),
                        &(p->pvar_.fade_entry),
                        &(p->pvar_.fade_alpha));
    
            if( (info->seq->active && info->seq->rec_id) || info->settings->offline_record )
                p->pvar_.enc_active = 1;

            vj_tag *tag = vj_tag_get( sample_id );
            if(tag) {
                if( info->global_chain->enabled == 1) {
                    if(p->pvar_.fx_status ) vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,tag->effect_chain, tag);
                    vj_perform_global_chain_sync(info, info->global_chain);
                    vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,info->global_chain->fx_chain, tag);

                } else if(info->global_chain->enabled == 2) {
                    vj_perform_global_chain_sync(info, info->global_chain);
                    vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,info->global_chain->fx_chain, tag);
                    if(p->pvar_.fx_status ) vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,tag->effect_chain, tag);
                } else {
                    if(p->pvar_.fx_status ) vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,tag->effect_chain, tag);
                }
            }
            
            vj_perform_finish_chain( info,p,a,sample_id,source_type );

            vj_fx_hold_update(info, a);

            break;
        default:
            break;
    }

    info->out_buf = cur_out; 
    
    if( info->settings->feedback && info->settings->feedback_stage == 1 ) 
    {
        int idx = info->out_buf;
        uint8_t *dst[4] = { 
            p->primary_buffer[idx]->Y,
            p->primary_buffer[idx]->Cb,
            p->primary_buffer[idx]->Cr,
            p->primary_buffer[idx]->alpha };

        vj_perform_copy3( dst,g->feedback_buffer, a->len, a->uv_len, a->stride[3] * a->height  );

        info->settings->feedback_stage = 2;
    }
}
#define FP_S    14
#define FP_M    (1 << FP_S)
#define FP_HALF (1 << (FP_S - 1))

static void vj_perform_color_vibrancy1(uint8_t *U, uint8_t *V, const int len, int vib) {
    uint8_t *restrict pu = U;
    uint8_t *restrict pv = V;
    const float gain = 0.50f + ((float)vib / 255.0f) * 1.30f;
    const int32_t gain_fp = (int32_t)(gain * FP_M);

    for (int i = 0; i < len; i++)
    {
        int u = pu[i] - 128;
        int v = pv[i] - 128;

        u = (u * gain_fp + FP_HALF) >> FP_S;
        v = (v * gain_fp + FP_HALF) >> FP_S;

        pu[i] = (uint8_t)(u + 128);
        pv[i] = (uint8_t)(v + 128);
    }
}

static void vj_perform_color_vibrancy(uint8_t *U, uint8_t *V, const int len, int vib)
{
    const float gain = 0.50f + ((float)vib / 255.0f) * 1.30f;
    const int32_t gain_fp = (int32_t)(gain * FP_M);

    // skip near-identity transform
    const int32_t diff = (gain_fp > FP_M) ? (gain_fp - FP_M) : (FP_M - gain_fp);
    if (diff < 164) return;

    uint8_t *restrict pu = U;
    uint8_t *restrict pv = V;

    for (int i = 0; i < len; i++)
    {
        int u = pu[i] - 128;
        int v = pv[i] - 128;

        u = (u * gain_fp + FP_HALF) >> FP_S;
        v = (v * gain_fp + FP_HALF) >> FP_S;

        pu[i] = (uint8_t)(u + 128);
        pv[i] = (uint8_t)(v + 128);
    }
}

int vj_perform_queue_video_frame(veejay_t *info, VJFrame *dst)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    video_playback_setup *settings = info->settings;
    performer_t *p = g->A;

    if (info->settings->hold_fx && info->settings->hold_fx_prev)
    {
        performer_global_t *g = (performer_global_t*) info->performer;
        performer_t *p = g->A;

        uint8_t slot = 5;

        uint8_t *fx_hold_data[4] = {
            p->primary_buffer[slot]->Y,
            p->primary_buffer[slot]->Cb,
            p->primary_buffer[slot]->Cr,
            p->primary_buffer[slot]->alpha
        };

        int plane_sizes[4] = {
            dst->len,
            dst->uv_len,
            dst->uv_len,
            0,
        };

        vj_frame_copy( fx_hold_data, dst->data, plane_sizes);

        return 1;
    }

    vj_perform_sample_tick_reset(g);

    long long cur_frame = atomic_load_long_long(&info->settings->current_frame_num);

    vj_perform_queue_video_frames( info, info->effect_frame1, info->effect_frame2, g->A, info->uc->sample_id, info->uc->playback_mode, cur_frame);

    int transition_enabled = atomic_load_int(&settings->transition.active) && atomic_load_int(&settings->transition.global_state);
    if (transition_enabled) {
        long long start = atomic_load_long_long(&settings->transition.start);
        long long end = atomic_load_long_long(&settings->transition.end);

        if (cur_frame < start || cur_frame > end)
            transition_enabled = 0;
    }

    if(transition_enabled) {
        sample_b_t *sb = &p->sample_b;
        long long sample_position = atomic_load_long_long(&sb->offset);
        long long start = atomic_load_long_long(&sb->start);
        
        sample_position += start;

        vj_perform_queue_video_frames( info,info->effect_frame3, info->effect_frame4, g->B, info->settings->transition.next_id, info->settings->transition.next_type, sample_position );
        vje_disable_parallel();
    }

    if(!transition_enabled) {
        vj_perform_render_video_frames(info, g->A, info->effect_info, info->uc->sample_id, info->uc->playback_mode, info->effect_frame1, info->effect_frame2, info->effect_frame_info, info->effect_info );
        vj_perform_try_sequence(info);
    }
    else
    {
#pragma omp parallel num_threads(2)
{       
#pragma omp single
{
#pragma omp task
{
        vj_perform_render_video_frames(info, g->A, info->effect_info, info->uc->sample_id, info->uc->playback_mode, info->effect_frame1, info->effect_frame2, info->effect_frame_info, info->effect_info );
}
#pragma omp task
{
        vj_perform_render_video_frames(info, g->B, info->effect_info2, info->settings->transition.next_id, info->settings->transition.next_type,
                info->effect_frame3, info->effect_frame4, info->effect_frame_info2, info->effect_info2 );
}
#pragma omp taskwait
        vj_perform_transition_sample( info, info->effect_frame1, info->effect_frame3 );
}
}
    }

    vj_perform_render_font( info, settings, info->effect_frame1);

    if(!settings->composite)
        vj_perform_render_osd( info, settings, info->effect_frame1 );

    vj_perform_finish_render( info, g->A, info->effect_frame1, settings );

    if( info->effect_frame1->ssm == 1 )
    {
        chroma_subsample(settings->sample_mode,info->effect_frame1,info->effect_frame1->data); 
        vj_perform_set_422(info->effect_frame1);
        vj_perform_set_422(info->effect_frame2);
        vj_perform_set_422(info->effect_frame3);
        vj_perform_set_422(info->effect_frame4);

        g->A->primary_buffer[0]->ssm = 0;
    }

    vje_enable_parallel();

    vj_perform_record_video_frame(info);

    int col_vib = atomic_load_long_long(&settings->color_vibrance);
    vj_perform_color_vibrancy(info->effect_frame1->data[1], info->effect_frame1->data[2],info->effect_frame1->uv_len, col_vib);

    // must copy?
    int strides[4] = {info->effect_frame1->len, info->effect_frame1->uv_len,info->effect_frame1->uv_len,0};
    uint8_t *input[4] = { p->primary_buffer[0]->Y, p->primary_buffer[0]->Cb, p->primary_buffer[0]->Cr, NULL };
    vj_frame_copy( input, dst->data, strides );

    return 1;
}

void vj_perform_inc_frame(veejay_t *info, int num)
{
    video_playback_setup *settings = info->settings;
    const int mode = info->uc->playback_mode;
    int looptype = 1;
    int speed = settings->current_playback_speed;

    long long end = atomic_load_long_long(&settings->max_frame_num);
    long long start = atomic_load_long_long(&settings->min_frame_num);

    int prev_dir = (settings->current_playback_speed < 0 ? -1 :
                    settings->current_playback_speed > 0 ? 1 : 0);
    int cur_dir = prev_dir;

    if (mode == VJ_PLAYBACK_MODE_SAMPLE) {
        int s_start, s_end, s_loop, s_speed;
        if (sample_get_short_info(info->uc->sample_id, &s_start, &s_end,
                                  &s_loop, &s_speed) != 0)
            return;

        start = s_start;
        end   = s_end;
        looptype = s_loop;
        speed = s_speed;
        settings->current_playback_speed = speed;
        cur_dir = (speed < 0) ? -1 : speed > 0 ? 1 : 0;
    } else {
        looptype = 1;
        speed = settings->current_playback_speed;
        cur_dir = (speed < 0) ? -1 : speed > 0 ? 1 : 0;
    }

    long long cur_slice = atomic_load_int(&settings->audio_slice);
    long long max_sfd   = atomic_load_int(&settings->audio_slice_len);
    long long cur_frame = atomic_load_long_long(&settings->current_frame_num);
    long long next_frame = cur_frame + num;

    if (max_sfd > 1 && cur_slice <(max_sfd - 1)) {
        atomic_store_int(&settings->audio_slice, cur_slice + 1);
        return;
    }

    int edge_type = AUDIO_EDGE_NONE;
    int next_dir = cur_dir;

    if(looptype == 3) { //option: either do this here or at the sample boundary
        if( speed >= 0 ) {
            next_frame = vj_frame_rand(cur_frame,start,end, settings->master_frame_num );
        }
        else if(speed < 0) {
            next_frame = vj_frame_rand(cur_frame, end,start, settings->master_frame_num);
        }
    }

    if (next_frame > end) {
        switch (looptype) {
            case 2: // bounce loop
                next_dir = -cur_dir;
                veejay_set_speed(info, next_dir * abs(speed), 1);
                next_frame = end;
                edge_type = AUDIO_EDGE_DIRECTION;
                break;
            case 1: // standard loop
                next_frame = start;
                next_dir = 1;
                edge_type = AUDIO_EDGE_DIRECTION;
                break;
            case 3:
                //option:
                //next_frame = vj_frame_rand(cur_frame,start,end, settings->master_frame_num );
                edge_type = AUDIO_EDGE_JUMP;
                break;
            default: // stop at end
                next_frame = end;
                veejay_set_speed(info, 0, 1);
                next_dir = 0;
                edge_type = AUDIO_EDGE_DIRECTION;
                break;
        }
    } else if (next_frame < start) {
        switch (looptype) {
            case 2: // bounce loop
                next_dir = -cur_dir;
                veejay_set_speed(info, next_dir * abs(speed), 1);
                next_frame = start;
                edge_type = AUDIO_EDGE_DIRECTION;
                break;
            case 1: // standard loop
                next_frame = end;
                next_dir = -1;
                edge_type = AUDIO_EDGE_DIRECTION;
                break;
            case 3:
                //option:
                //next_frame = vj_frame_rand(cur_frame,start,end, settings->master_frame_num );
                edge_type = AUDIO_EDGE_JUMP;
                break;
            default: // stop at start
                next_frame = start;
                veejay_set_speed(info, 0, 1);
                next_dir = 0;
                edge_type = AUDIO_EDGE_DIRECTION;
                break;
        }
    }

    if (cur_dir != prev_dir) {
        atomic_store_int(&settings->audio_direction_changed, 1);
    }

    if (edge_type != AUDIO_EDGE_NONE) {
        vj_perform_initiate_edge_change(info, edge_type, prev_dir, next_dir);
    }

    if (mode != VJ_PLAYBACK_MODE_PLAIN && edge_type != AUDIO_EDGE_NONE) {
        int playback_ended = ( mode == VJ_PLAYBACK_MODE_SAMPLE ? sample_loop_dec(info->uc->sample_id): vj_tag_loop_dec(info->uc->sample_id) );
    }

    atomic_store_long_long(&settings->current_frame_num, next_frame);
    if (max_sfd > 1)
        atomic_store_int(&settings->audio_slice, 0);

    if (mode == VJ_PLAYBACK_MODE_SAMPLE) {
        vj_perform_rand_update(info);
    }
}

void    vj_perform_randomize(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    if(settings->randplayer.mode == RANDMODE_INACTIVE)
        return;

    double n_sample = (double) (sample_highest_valid_id());
    int track_dup  = 0;

    if( settings->randplayer.mode == RANDMODE_SAMPLE && n_sample > 1.0 )
        track_dup = info->uc->sample_id;

    if( settings->randplayer.seed == 0 )
        settings->randplayer.seed = (unsigned long long) time(NULL);

    int take_n   = vj_frame_rand(track_dup, 1, n_sample, settings->randplayer.seed ++ );
    int min_delay = 1;
    int max_delay = 0;

    if(take_n == 1 && !sample_exists(take_n)) {
        veejay_msg(0,"No samples to randomize");
        settings->randplayer.mode = RANDMODE_INACTIVE;
        return;
    }

    while(!sample_exists(take_n))
    {
        veejay_msg(VEEJAY_MSG_DEBUG, 
         "Sample to play (at random) %d does not exist",
            take_n);
        take_n = vj_frame_rand(track_dup, 1, n_sample, settings->randplayer.seed ++ );
    }

    int start,end;
    start = sample_get_startFrame( take_n);
    end   = sample_get_endFrame( take_n );
    
    if( settings->randplayer.timer == RANDTIMER_FRAME )
    {
        int sample_len = (end - start + 1);
        if (sample_len <= 1) sample_len = 2; // prevent zero-length

        // random delay between min_delay and sample length
        max_delay = vj_frame_rand(track_dup, min_delay, sample_len, settings->randplayer.seed ++ );
    }
    else
    {
        max_delay = (end-start);    
    }

    settings->randplayer.max_delay = max_delay;
    settings->randplayer.min_delay = min_delay; 

    veejay_msg(VEEJAY_MSG_INFO, "Sample randomizer trigger in %d frame periods", max_delay);

    settings->randplayer.next_id = take_n;
    settings->randplayer.next_mode = VJ_PLAYBACK_MODE_SAMPLE;

}

int vj_perform_rand_update(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    if(settings->randplayer.mode == RANDMODE_INACTIVE)
        return 0;
    if(settings->randplayer.mode == RANDMODE_SAMPLE)
    {
        settings->randplayer.max_delay --;
        if(settings->randplayer.max_delay <= 0 )
            vj_perform_randomize(info);
        return 1;
    }
    return 0;   
}

