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

#include <libveejay/audioscratcher.h>
#ifdef HAVE_JACK
#include <libveejay/vj-jack.h>
#include <libveejay/vj-audio-beat.h>
#endif
#define PERFORM_AUDIO_SIZE 16384
#define AUDIO_TURN_HISTORY_BYTES 4096
#define PSLOW_A 3
#define PSLOW_B 4

void vj_audio_declick_observe(const void *owner, const uint8_t *buf, int samples,
                              int frame_bytes, int path, int speed, int dir);
int vj_scratch_process(void *ptr,
                       short *output,
                       int max_out_frames,
                       const short *input,
                       int src_frames,
                       double speed);
int vj_audio_render_slow_stream_bend_s16(uint8_t *dst,
                                           int dst_samples,
                                           const uint8_t *src,
                                           int source_base_sample,
                                           int context_samples,
                                           int slice_count,
                                           int start_stretched_sample,
                                           int phase_offset_start,
                                           int phase_offset_end,
                                           int frame_bytes);
int vj_audio_render_slow_stream_turn_s16(uint8_t *dst,
                                           int dst_samples,
                                           const uint8_t *src,
                                           int source_base_sample,
                                           int context_samples,
                                           int slice_count,
                                           int start_stretched_sample,
                                           int phase_offset_start,
                                           int phase_offset_end,
                                           int frame_bytes);
int vj_audio_render_slow_stream_velocity_turn_s16(uint8_t *dst,
                                                  int dst_samples,
                                                  const uint8_t *src,
                                                  int source_base_sample,
                                                  int context_samples,
                                                  int slice_count,
                                                  int start_stretched_sample,
                                                  int phase_offset_start,
                                                  int phase_offset_end,
                                                  int frame_bytes);
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
    int audio_diag_valid;
    int audio_diag_frame_bytes;
    uint8_t audio_diag_prev_frame[64];
    uint8_t audio_diag_last_frame[64];
    uint8_t audio_turn_history[AUDIO_TURN_HISTORY_BYTES];
    int audio_turn_history_samples;
    int audio_turn_history_frame_bytes;
    int frame_in_history[8];
    int flip_lock;
    int pending_flip_dir;
    int audio_phase_offset_valid;
    int audio_phase_offset_samples;
    int audio_phase_offset_dir;
    int audio_phase_offset_sfd;
    int audio_phase_offset_start_slice;

    int scratch_initialized;
    double scratch_pos;
    double scratch_vel;
    double scratch_target_vel;
    double scratch_last_sync_pos;
    double scratch_last_sync_error;
    double scratch_sync_bias;
    int scratch_sync_hold_blocks;
    int scratch_stable_blocks;
    int scratch_last_dir;
    int scratch_last_sfd;
    int scratch_ramp_left;
    int scratch_last_reset;
} sample_b_t; // FIXME clean up fields

#define SLOW_SCRATCH_MAX_CTX_FRAMES 16

typedef struct
{
    int valid;
    int frames;
    long long first_frame;
    long long last_frame;
    int frame_len[SLOW_SCRATCH_MAX_CTX_FRAMES];
    int frame_off[SLOW_SCRATCH_MAX_CTX_FRAMES];
    double exact_start[SLOW_SCRATCH_MAX_CTX_FRAMES];
    double exact_len[SLOW_SCRATCH_MAX_CTX_FRAMES];
} slow_scratch_ctx_map_t;

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
    int fx_rgb_format;
    int fx_yuv_format;
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
static int vj_perform_format_changed_rgb(performer_t *p, VJFrame *frame);
static int vj_perform_format_changed_yuv(performer_t *p, VJFrame *frame);

#ifdef HAVE_JACK
static int vj_perform_queue_audio_frame_impl(veejay_t *info,
                                             void *ptr,
                                             uint8_t *a_buf,
                                             int speed,
                                             long long target_frame,
                                             int sample_id,
                                             int *audio_sample_ptr);
#endif

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


static const char *vj_audio_edge_name(int edge_type)
{
    switch (edge_type) {
        case AUDIO_EDGE_NONE:      return "none";
        case AUDIO_EDGE_JUMP:      return "jump";
        case AUDIO_EDGE_RESET:     return "reset";
        case AUDIO_EDGE_SILENCE:   return "silence";
        case AUDIO_EDGE_DIRECTION: return "direction";
        default:                   return "unknown";
    }
}

void vj_perform_initiate_edge_change(
    veejay_t *info,
    int edge_type,
    int prev_dir,
    int cur_dir
)
{
    if (edge_type == AUDIO_EDGE_NONE)
        return;

    if (info == NULL || info->audio == NO_AUDIO || info->performer == NULL)
        return;

    performer_global_t *g = (performer_global_t*)info->performer;
    if (g->A == NULL || g->B == NULL ||
        g->A->audio_edge == NULL || g->B->audio_edge == NULL)
        return;

    const int real_direction_change =
        (prev_dir != 0 && cur_dir != 0 && prev_dir != cur_dir);

    if (edge_type == AUDIO_EDGE_DIRECTION && !real_direction_change)
        edge_type = AUDIO_EDGE_JUMP;

    const int old_a = atomic_load_int(&g->A->audio_edge->pending_edge);
    const int old_b = atomic_load_int(&g->B->audio_edge->pending_edge);

/*    if (edge_type != AUDIO_EDGE_DIRECTION || old_a != AUDIO_EDGE_NONE ||
        old_b != AUDIO_EDGE_NONE || !real_direction_change) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-DIAG] edge-request-watch edge=%s(%d) prev_dir=%d cur_dir=%d real_flip=%d oldA=%s(%d) oldB=%s(%d)",
                   vj_audio_edge_name(edge_type), edge_type,
                   prev_dir, cur_dir, real_direction_change,
                   vj_audio_edge_name(old_a), old_a,
                   vj_audio_edge_name(old_b), old_b);
    } */

    atomic_store_int(&g->A->audio_edge->pending_edge, edge_type);
    atomic_store_int(&g->B->audio_edge->pending_edge, edge_type);
}

static inline int vj_audio_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

#ifdef HAVE_JACK
typedef struct
{
    sample_eff_chain **chain[2];
    int chain_count;
} vj_perform_ab_render_ctx_t;

static int vj_perform_ab_ctx_add_chain(vj_perform_ab_render_ctx_t *ctx, sample_eff_chain **chain)
{
    if(!ctx || !chain || ctx->chain_count >= 2)
        return 0;

    for(int i = 0; i < ctx->chain_count; i++)
    {
        if(ctx->chain[i] == chain)
            return 0;
    }

    ctx->chain[ctx->chain_count++] = chain;
    return 1;
}

static sample_eff_chain *vj_perform_ab_ctx_entry(void *vctx, int chain_pos)
{
    vj_perform_ab_render_ctx_t *ctx = (vj_perform_ab_render_ctx_t *)vctx;
    int lane;
    int entry;

    if(!ctx || chain_pos < 0)
        return NULL;

    lane = chain_pos / SAMPLE_MAX_EFFECTS;
    entry = chain_pos % SAMPLE_MAX_EFFECTS;

    if(lane < 0 || lane >= ctx->chain_count || entry < 0 || entry >= SAMPLE_MAX_EFFECTS)
        return NULL;

    if(!ctx->chain[lane])
        return NULL;

    return ctx->chain[lane][entry];
}

static int vj_perform_ab_chain_get_fx_id(void *ctx, int chain_pos)
{
    sample_eff_chain *entry = vj_perform_ab_ctx_entry(ctx, chain_pos);

    if(!entry)
        return 0;

    /*
     * Match the real renderer: disabled chain entries are not rendered and
     * must not become beat-auto targets.
     */
    if(entry->e_flag == 0)
        return 0;

    /* If beat detector is off */
    if(entry->beat_flag == 0)
        return 0;

    return entry->effect_id;
}

static int vj_perform_ab_chain_get_arg(void *ctx, int chain_pos, int param_nr)
{
    sample_eff_chain *entry = vj_perform_ab_ctx_entry(ctx, chain_pos);

    if(!entry)
        return 0;

    if(param_nr < 0 || param_nr >= SAMPLE_MAX_PARAMETERS)
        return 0;

    return entry->arg[param_nr];
}

static int vj_perform_ab_chain_set_arg(void *ctx, int chain_pos, int param_nr, int value)
{
    sample_eff_chain *entry = vj_perform_ab_ctx_entry(ctx, chain_pos);

    if(!entry)
        return 0;

    if(param_nr < 0 || param_nr >= SAMPLE_MAX_PARAMETERS)
        return 0;

    entry->arg[param_nr] = value;
    return 1;
}

static int vj_perform_ab_ctx_active_fx_count(vj_perform_ab_render_ctx_t *ctx)
{
    int active = 0;

    if(!ctx)
        return 0;

    for(int lane = 0; lane < ctx->chain_count; lane++)
    {
        sample_eff_chain **chain = ctx->chain[lane];

        if(!chain)
            continue;

        for(int entry_nr = 0; entry_nr < SAMPLE_MAX_EFFECTS; entry_nr++)
        {
            sample_eff_chain *entry = chain[entry_nr];

            if(!entry)
                continue;

            if(entry->e_flag == 0 || entry->effect_id <= 0)
                continue;

            active++;
        }
    }

    return active;
}

static int vj_perform_ab_ctx_debug_should_log(const char *tag, int active_fx, int changed)
{
    static int seq = 0;

    seq++;

    if(changed > 0)
        return 1;

    if(seq <= 16)
        return 1;

    if(active_fx <= 0)
        return ((seq % 30) == 0);

    if(tag && strcmp(tag, "state") != 0)
        return ((seq % 30) == 0);

    return ((seq % 45) == 0);
}
static void vj_perform_ab_ctx_debug_dump(
    veejay_t *veejay_instance_,
    const char *tag,
    int sample_id,
    int playmode,
    int renderer_id,
    vj_perform_ab_render_ctx_t *ctx,
    int local_enabled,
    int global_enabled,
    int active_fx,
    int changed
)
{
    char summary[1024];
    int pos = 0;
    int action = -1;
    int enabled = -1;
    int running = -1;
    int open = -1;
    int paused = -1;
    int hit_seq = -1;
    int consumed_seq = -1;
    int sample_speed = 0;
    int play_speed = 0;

    static long last_log_ms = 0;
    static int log_count = 0;

    long now_ms = 0;

    if(!tag)
        tag = "unknown";

    now_ms = (long)(time(NULL) * 1000L);

    if(strcmp(tag, "state") == 0 && changed <= 0 && log_count > 20) {
        if(last_log_ms != 0 && (now_ms - last_log_ms) < 1000L)
            return;
    }

    last_log_ms = now_ms;
    log_count++;

    summary[0] = '\0';

    if(ctx)
    {
        for(int lane = 0; lane < ctx->chain_count && pos < (int)sizeof(summary) - 1; lane++)
        {
            sample_eff_chain **chain = ctx->chain[lane];
            int lane_active = 0;
            int lane_raw = 0;
            int lane_disabled = 0;
            int lane_beat = 0;
            int first_active_fx = 0;
            int first_raw_fx = 0;
            int first_beat_fx = 0;
            int first_active_entry = -1;
            int first_raw_entry = -1;
            int first_beat_entry = -1;

            if(chain)
            {
                for(int entry_nr = 0; entry_nr < SAMPLE_MAX_EFFECTS; entry_nr++)
                {
                    sample_eff_chain *entry = chain[entry_nr];

                    if(!entry || entry->effect_id <= 0)
                        continue;

                    lane_raw++;

                    if(first_raw_fx == 0) {
                        first_raw_fx = entry->effect_id;
                        first_raw_entry = entry_nr;
                    }

                    if(entry->e_flag == 0) {
                        lane_disabled++;
                        continue;
                    }

                    lane_active++;

                    if(first_active_fx == 0) {
                        first_active_fx = entry->effect_id;
                        first_active_entry = entry_nr;
                    }

                    if(entry->beat_flag) {
                        lane_beat++;

                        if(first_beat_fx == 0) {
                            first_beat_fx = entry->effect_id;
                            first_beat_entry = entry_nr;
                        }
                    }
                }
            }

            pos += snprintf(summary + pos,
                            sizeof(summary) - (size_t)pos,
                            "%sL%d:raw=%d active=%d beat=%d disabled=%d first_raw=%d@%d first_active=%d@%d first_beat=%d@%d",
                            lane == 0 ? "" : " | ",
                            lane,
                            lane_raw,
                            lane_active,
                            lane_beat,
                            lane_disabled,
                            first_raw_fx,
                            first_raw_entry,
                            first_active_fx,
                            first_active_entry,
                            first_beat_fx,
                            first_beat_entry);
        }
    }
    else
    {
        snprintf(summary, sizeof(summary), "ctx=NULL");
    }

#ifdef HAVE_JACK
    if(veejay_instance_ && veejay_instance_->settings) {
        video_playback_setup *settings = veejay_instance_->settings;

        action = atomic_load_int(&settings->audio_beat.action_mode);
        enabled = vj_audio_beat_is_enabled(&settings->audio_beat);
        running = vj_audio_beat_is_running(&settings->audio_beat);
        open = vj_audio_beat_is_open(&settings->audio_beat);
        paused = vj_audio_beat_is_paused_by_beat(&settings->audio_beat);
        hit_seq = atomic_load_int(&settings->audio_beat.hit_seq);
        consumed_seq = atomic_load_int(&settings->audio_beat.consumed_seq);
        play_speed = settings->current_playback_speed;

        if(veejay_instance_->uc &&
           veejay_instance_->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
            sample_speed = sample_get_speed(veejay_instance_->uc->sample_id);
    }
#endif

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT-PERFORM] %s sample=%d mode=%d renderer=%d local=%d global=%d chains=%d active_fx=%d changed=%d action=%d enabled=%d open=%d running=%d paused=%d hit_seq=%d consumed=%d play_speed=%d sample_speed=%d :: %s",
               tag,
               sample_id,
               playmode,
               renderer_id,
               local_enabled,
               global_enabled,
               ctx ? ctx->chain_count : -1,
               active_fx,
               changed,
               action,
               enabled,
               open,
               running,
               paused,
               hit_seq,
               consumed_seq,
               play_speed,
               sample_speed,
               summary);
}
static int vj_perform_audio_beat_playmode_has_fx_chain(int playmode)
{
    return playmode == VJ_PLAYBACK_MODE_SAMPLE ||
           playmode == VJ_PLAYBACK_MODE_TAG;
}
static int vj_perform_audio_beat_global_chain_is_rendered(
    veejay_t *info,
    int sample_id,
    int playmode
)
{
    (void)sample_id;

    if(!vj_perform_audio_beat_playmode_has_fx_chain(playmode))
        return 0;

    /*
     * The global FX chain is a real render target for SAMPLE and STREAM only.
     * It may be rendered for the selected source, a non-selected source, a
     * secondary source, or a transition source.  Do not suppress it because it
     * happens to originate from the currently rendered sample/stream; that made
     * the beat mapper see an empty local chain and kept auto-FX at targets=0.
     */
    if(!info || !info->global_chain || !info->global_chain->enabled)
        return 0;

    return 1;
}

static int vj_perform_ab_ctx_beat_fx_count(vj_perform_ab_render_ctx_t *ctx)
{
    int active = 0;

    if(!ctx)
        return 0;

    for(int lane = 0; lane < ctx->chain_count; lane++)
    {
        sample_eff_chain **chain = ctx->chain[lane];

        if(!chain)
            continue;

        for(int entry_nr = 0; entry_nr < SAMPLE_MAX_EFFECTS; entry_nr++)
        {
            sample_eff_chain *entry = chain[entry_nr];

            if(!entry)
                continue;

            if(entry->e_flag == 0 || entry->effect_id <= 0)
                continue;

            if(entry->beat_flag == 0)
                continue;

            active++;
        }
    }

    return active;
}

static int vj_perform_audio_beat_apply_render_chains(
    veejay_t *info,
    int sample_id,
    int playmode,
    int renderer_id,
    sample_eff_chain **local_chain,
    int local_enabled
)
{
    video_playback_setup *settings;
    vj_perform_ab_render_ctx_t ctx;
    sample_eff_chain **global_chain = NULL;
    int global_enabled = 0;
    int active_fx = 0;
    int beat_fx = 0;
    int changed = 0;
    int action = VJ_AUDIO_BEAT_ACTION_NONE;

    if(!info || !info->settings)
        return 0;

    settings = info->settings;

    if(!vj_audio_beat_is_enabled(&settings->audio_beat))
        return 0;

    if(!vj_audio_beat_is_running(&settings->audio_beat))
        return 0;

    action = atomic_load_int(&settings->audio_beat.action_mode);


    if(action != VJ_AUDIO_BEAT_ACTION_AUTO_FX &&
       action != VJ_AUDIO_BEAT_ACTION_FREEZE_AND_AUTO_FX)
        return 0;

    if(!vj_perform_audio_beat_playmode_has_fx_chain(playmode))
        return 0;

    veejay_memset(&ctx, 0, sizeof(ctx));

    if(vj_perform_audio_beat_global_chain_is_rendered(info, sample_id, playmode))
    {
        global_chain = info->global_chain->fx_chain;
        global_enabled = info->global_chain->enabled;
    }

    if(global_enabled == 2)
        vj_perform_ab_ctx_add_chain(&ctx, global_chain);

    if(local_enabled && local_chain)
        vj_perform_ab_ctx_add_chain(&ctx, local_chain);

    if(global_enabled == 1)
        vj_perform_ab_ctx_add_chain(&ctx, global_chain);

    if(ctx.chain_count <= 0)
    {
        vj_perform_ab_ctx_debug_dump(info, "skip-no-chain",
                                     sample_id,
                                     playmode,
                                     renderer_id,
                                     &ctx,
                                     (local_enabled && local_chain) ? 1 : 0,
                                     global_enabled,
                                     -1,
                                     0);
        return 0;
    }

    active_fx = vj_perform_ab_ctx_active_fx_count(&ctx);

    if(active_fx <= 0)
    {
        vj_perform_ab_ctx_debug_dump(info, "skip-no-active-fx",
                                     sample_id,
                                     playmode,
                                     renderer_id,
                                     &ctx,
                                     (local_enabled && local_chain) ? 1 : 0,
                                     global_enabled,
                                     active_fx,
                                     0);
        return 0;
    }

    beat_fx = vj_perform_ab_ctx_beat_fx_count(&ctx);

    if(beat_fx <= 0)
    {
        vj_perform_ab_ctx_debug_dump(info, "skip-no-beat-fx",
                                     sample_id,
                                     playmode,
                                     renderer_id,
                                     &ctx,
                                     (local_enabled && local_chain) ? 1 : 0,
                                     global_enabled,
                                     active_fx,
                                     0);
        return 0;
    }

#ifdef _OPENMP
#pragma omp critical(vj_perform_audio_beat_auto_apply)
#endif
    {
        changed = vj_audio_beat_auto_apply_chain(
            &settings->audio_beat,
            &ctx,
            ctx.chain_count * SAMPLE_MAX_EFFECTS,
            vj_perform_ab_chain_get_fx_id,
            vj_perform_ab_chain_get_arg,
            vj_perform_ab_chain_set_arg
        );
    }

    vj_perform_ab_ctx_debug_dump(info, "state",
                                 sample_id,
                                 playmode,
                                 renderer_id,
                                 &ctx,
                                 (local_enabled && local_chain) ? 1 : 0,
                                 global_enabled,
                                 active_fx,
                                 changed);

    return changed;
}

#endif

static void vj_perform_clear_audio_edges(
    veejay_t *info,
    audio_edge_t *edge,
    int cur_dir
) {
    if (info == NULL || info->performer == NULL) {
        vj_audio_clear_edge(edge, cur_dir);
        return;
    }

    performer_global_t *g = (performer_global_t*)info->performer;
    audio_edge_t *edge_a = (g != NULL && g->A != NULL) ? g->A->audio_edge : NULL;
    audio_edge_t *edge_b = (g != NULL && g->B != NULL) ? g->B->audio_edge : NULL;

    const int old_a = (edge_a != NULL) ? atomic_load_int(&edge_a->pending_edge) : AUDIO_EDGE_NONE;
    const int old_b = (edge_b != NULL) ? atomic_load_int(&edge_b->pending_edge) : AUDIO_EDGE_NONE;

    if (edge_a != NULL)
        vj_audio_clear_edge(edge_a, cur_dir);
    if (edge_b != NULL && edge_b != edge_a)
        vj_audio_clear_edge(edge_b, cur_dir);
    if (edge == NULL && edge_a == NULL && edge_b == NULL)
        vj_audio_clear_edge(edge, cur_dir);

    (void)old_a;
    (void)old_b;
}

static inline int vj_seq_type_to_playback_mode(int type)
{
    return (type == 0 || type == VJ_PLAYBACK_MODE_SAMPLE)
        ? VJ_PLAYBACK_MODE_SAMPLE
        : VJ_PLAYBACK_MODE_TAG;
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

void vj_perform_setup_transition(veejay_t *info,
                                 int next_sample_id,
                                 int next_type,
                                 int sample_id,
                                 int current_type,
                                 int next_seq_idx)
{
    video_playback_setup *settings = info->settings;

    current_type = vj_seq_type_to_playback_mode(current_type);
    next_type    = vj_seq_type_to_playback_mode(next_type);

    if (next_sample_id <= 0) {
        vj_perform_reset_transition(info);
        return;
    }

    int transition_active = (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_transition_active(sample_id) : vj_tag_get_transition_active(sample_id);

    if (!transition_active) {
        vj_perform_reset_transition(info);
        return;
    }

    int transition_length = (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_transition_length(sample_id) : vj_tag_get_transition_length(sample_id);

    if (transition_length <= 0) {
        vj_perform_reset_transition(info);
        return;
    }

    int transition_shape = (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_transition_shape(sample_id) : vj_tag_get_transition_shape(sample_id);

    if (transition_shape == -1) {
        transition_shape = (int)(((double)shapewipe_get_num_shapes(settings->transition.ptr)) * rand() / RAND_MAX);
    }

    int speed =
        (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_speed(sample_id) : 1;

    int start =
        (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_startFrame(sample_id) : 0;

    int end =
        (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_endFrame(sample_id) : vj_tag_get_n_frames(sample_id);

    if (end <= start) {
        vj_perform_reset_transition(info);
        return;
    }

    int span = end - start;
    if (transition_length > span)
        transition_length = span;

    long long start_tx = start;
    long long end_tx   = end;

    if (speed < 0) {
        start_tx = start + transition_length;
        end_tx   = start;
    }
    else {
        start_tx = end - transition_length;
        end_tx   = end;
    }

    settings->transition.shape     = transition_shape;
    settings->transition.next_type = next_type;
    settings->transition.next_id   = next_sample_id;
    settings->transition.ready     = 0;
    settings->transition.seq_index = next_seq_idx;

    atomic_store_long_long(&settings->transition.start, start_tx);
    atomic_store_long_long(&settings->transition.end, end_tx);
    atomic_store_int(&settings->transition.active, transition_active);
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

int vj_perform_try_sequence(veejay_t *info)
{
    if (!info->seq->active)
        return 0;

    video_playback_setup *settings = info->settings;
    int id = info->uc->sample_id;

    int loops = -1;

    if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
        loops = sample_get_loops(id);
    }
    else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG) {
        loops = vj_tag_get_loops(id);
    }
    else {
        return 0;
    }

    if (loops != 0)
        return 0;

    int type = 0;
    int next_slot = 0;
    int next_id = vj_perform_next_sequence(info, &type, &next_slot);

    if (next_id <= 0)
        return 0;

    int playback_mode = vj_seq_type_to_playback_mode(type);

    const int global_transition_on =
        atomic_load_int(&settings->transition.global_state);

    const int armed_transition_active =
        atomic_load_int(&settings->transition.active);

    if (!global_transition_on || !armed_transition_active) {
        info->seq->current = next_slot;
        veejay_change_playback_mode(info, playback_mode, next_id);
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
    vj_audio_declick_forget_owner(p);

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
        if (edge->history) free(edge->history);
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
    const uint32_t runtime_audio_len = (uint32_t)(ceil((double)el->audio_rate) * el->audio_bps) + 4096;
    const uint32_t top_audio_len = (runtime_audio_len > (8 * PERFORM_AUDIO_SIZE)) ? runtime_audio_len : (8 * PERFORM_AUDIO_SIZE);

    p->top_audio_buffer =
        (uint8_t *) vj_calloc(sizeof(uint8_t) * top_audio_len);
    if(!p->top_audio_buffer)
        return 0;

    p->audio_rec_buffer =
        (uint8_t *) vj_calloc(sizeof(uint8_t) * top_audio_len );
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

        int res = vj_jack_init_duplex(el);

        if( res <= 0 ) {
            veejay_msg(VEEJAY_MSG_WARNING,
                       "[AUDIO] Jack duplex start failed; falling back to playback-only mode");

            vj_jack_initialize();
            res = vj_jack_init(el);
        }

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

        if(vj_jack_has_input())
            veejay_msg(VEEJAY_MSG_DEBUG,"[AUDIO] Jack audio playback started with capture input ports");
        else
            veejay_msg(VEEJAY_MSG_WARNING,"[AUDIO] Jack audio playback started without capture input ports");

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

    vj_perform_format_changed_yuv(p, &pframe);
    yuv_convert_and_scale( p->yuv420_scaler, &pframe, p->yuv420_frame[0] );
}

static void *vj_perform_init_scaler(VJFrame *src, VJFrame *dst) {
    sws_template templ;
    veejay_memset(&templ,0,sizeof(sws_template));
    templ.flags = yuv_which_scaler();

    return yuv_init_swscaler( src, dst, &templ, yuv_sws_get_cpu_flags() );
}

static int vj_perform_format_changed_yuv(performer_t *p, VJFrame *frame) {
    // yuv -> yuv 4:2:0 planar
    if( p->yuv420_scaler == NULL || p->yuv420_frame[0] == NULL || p->fx_yuv_format != frame->format) {
        if(p->yuv420_frame[0]) {
           free(p->yuv420_frame[0]); 
        }
        p->yuv420_frame[0] = yuv_yuv_template(NULL, NULL,NULL, frame->width,frame->height,
            frame->range ? PIX_FMT_YUVJ420P : PIX_FMT_YUV420P);
        if(!p->yuv420_frame[0]) {
            return 0;
        }
        p->yuv420_scaler =  vj_perform_init_scaler( frame, p->yuv420_frame[0]);
        if(p->yuv420_scaler == NULL )
            return 0;
    }

    p->fx_yuv_format = frame->format;

    return 1;
    
}

static int vj_perform_format_changed_rgb(performer_t *p, VJFrame *frame) {

    // yuv -> rgb
    if( p->yuv2rgba_scaler == NULL || p->rgba_frame[0] == NULL || p->fx_rgb_format != frame->format ) {
        if(p->rgba_frame[0]) {
            free(p->rgba_frame[0]);
        }
        
        p->rgba_frame[0] = yuv_rgb_template( p->rgba_buffer[0], frame->width, frame->height, PIX_FMT_RGBA );
        if(!p->rgba_frame[0])
            return 0;
        
        if(p->yuv2rgba_scaler) {
            yuv_free_swscaler(p->yuv2rgba_scaler);
        }

        p->yuv2rgba_scaler = vj_perform_init_scaler(frame, p->rgba_frame[0]);
        if(p->yuv2rgba_scaler == NULL )
            return 0;
    }

    // rgb -> yuv
    if( p->rgba2yuv_scaler == NULL || p->rgba_frame[1] == NULL || p->fx_rgb_format != frame->format ) {
        if(p->rgba_frame[1]) {
            free(p->rgba_frame[1]);
        }

        p->rgba_frame[1] = yuv_rgb_template( p->rgba_buffer[0], frame->width, frame->height, PIX_FMT_RGBA );
        if(!p->rgba_frame[1])
            return 0;
        
        if(p->rgba2yuv_scaler) {
            yuv_free_swscaler(p->rgba2yuv_scaler);
        }
        p->rgba2yuv_scaler = vj_perform_init_scaler(p->rgba_frame[1], frame);
        if(p->rgba2yuv_scaler == NULL )
            return 0;
    }

    
    p->rgba_frame[0]->data[0] = p->rgba_buffer[0];
    p->rgba_frame[1]->data[0] = p->rgba_buffer[1];            
    p->fx_rgb_format = frame->format;

    return 1;
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
        
        if(!vj_perform_format_changed_rgb(p, frames[0])) {
            return;
        }

        yuv_convert_and_scale_rgb( p->yuv2rgba_scaler, frames[0], p->rgba_frame[0] );
        if(is_mixer) {
            yuv_convert_and_scale_rgb( p->yuv2rgba_scaler, frames[1], p->rgba_frame[1] );
        }

        vjert_apply( entry, p->rgba_frame, p->chain_id,c, args );
    
        yuv_convert_and_scale_from_rgb( p->rgba2yuv_scaler, p->rgba_frame[0],frames[0] );
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
    int loop_mode = sb->loopmode;

    const long len = (end - start) + 1;

    if (len <= 0) {
        *advance_out = 0;
        sb->offset = 0;
        return 0;
    }

    const long max_off = len - 1;
    int advance = 1;

    if(speed == 0) {
        long off = sb->offset;

        if(off < 0)
            off = 0;
        else if(off > max_off)
            off = max_off;

        *advance_out = 0;
        sb->offset = off;
        return off;
    }

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

    if (off < 0)
        off = 0;
    else if (off > max_off)
        off = max_off;

    if (advance && loop_mode == 3) {
        off = (long)((double)len * (double)rand() / ((double)RAND_MAX + 1.0));
        if (off > max_off)
            off = max_off;

        sb->offset = off;
        return off;
    }

    if (advance) {
        const long step = llabs((long long)speed);

        off += step * sb->direction;

        if (off > max_off) {
            if (loop_mode == 2) {
                off = max_off;
                sb->direction = -1;
            } else if (loop_mode == 1) {
                off = 0;
            } else {
                off = max_off;
            }
        } else if (off < 0) {
            if (loop_mode == 2) {
                off = 0;
                sb->direction = +1;
            } else if (loop_mode == 1) {
                off = max_off;
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

    return start + off;
}

#ifdef HAVE_JACK

static void slow_motion_update_turn_history(sample_b_t *posdata,
                                            const uint8_t *buf,
                                            int samples,
                                            int frame_bytes);


static int get_audio_frame_safe(
    veejay_t *info,
    editlist *el,
    long long frame,
    uint8_t *audio_buf,
    int pred_len,
    int frame_bytes,
    int speed
) {
    (void)info;
    (void)speed;

    if (el == NULL || audio_buf == NULL || pred_len <= 0 || frame_bytes <= 0)
        return 0;

    int n_samples = vj_el_get_audio_frame(el, frame, audio_buf);

    if (n_samples <= 0) {
        veejay_memset(audio_buf, 0, pred_len * frame_bytes);
        veejay_msg(0, "[AUDIO] Error fetching frame %lld — zeroed %d samples",
                   frame, pred_len);
        return pred_len;
    }

    if (n_samples < pred_len) {
        veejay_memset(audio_buf + ((size_t)n_samples * (size_t)frame_bytes), 0,
                      (pred_len - n_samples) * frame_bytes);
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO] Frame %lld fetched %d samples, zero-filled %d remaining",
                   frame, n_samples, pred_len - n_samples);
        return pred_len;
    }

    return n_samples;
}


static inline double slow_motion_sync_abs_pos(const editlist *el,
                                       const sample_b_t *posdata,
                                       long long source_frame,
                                       int pred_len,
                                       int slice_count,
                                       int cur_slice,
                                       int direction);
static inline int slow_motion_frame_len_exact(const editlist *el,
                                              long long frame,
                                              int pred_len);
static int slow_motion_fetch_scratch_context(veejay_t *info,
                                             editlist *el,
                                             sample_b_t *posdata,
                                             uint8_t *ctx,
                                             slow_scratch_ctx_map_t *map,
                                             int pred_len,
                                             int frame_bytes,
                                             int speed_int,
                                             double pos_a,
                                             double pos_b,
                                             double *ctx_abs_start_out,
                                             int *ctx_samples_out,
                                             long long *ctx_first_frame_out,
                                             long long *ctx_last_frame_out);
static double slow_motion_ctx_exact_to_actual_rel(const slow_scratch_ctx_map_t *map,
                                                  double abs_pos,
                                                  int ctx_samples);
static inline int16_t slow_motion_cubic_interp_s16(int p0, int p1, int p2, int p3, double t);

static inline void normal_ctx_sample_frame_s16(const uint8_t *ctx,
                                               int ctx_samples,
                                               double ctx_abs_start,
                                               const slow_scratch_ctx_map_t *ctx_map,
                                               double abs_pos,
                                               int frame_bytes,
                                               int16_t *dst,
                                               int dst_words)
{
    if (dst == NULL || dst_words <= 0)
        return;

    for (int c = 0; c < dst_words; c++)
        dst[c] = 0;

    if (ctx == NULL || ctx_samples <= 0 || frame_bytes <= 0 || (frame_bytes & 1))
        return;

    const int words = frame_bytes / 2;
    const int out_words = (dst_words < words) ? dst_words : words;
    const int max_index = ctx_samples - 1;
    const int16_t *in = (const int16_t*)ctx;

    double rel = (ctx_map != NULL && ctx_map->valid)
        ? slow_motion_ctx_exact_to_actual_rel(ctx_map, abs_pos, ctx_samples)
        : (abs_pos - ctx_abs_start);

    if (rel < 0.0)
        rel = 0.0;
    else if (rel > (double)max_index)
        rel = (double)max_index;

    int idx = (int)floor(rel);
    double frac = rel - (double)idx;
    if (idx < 0) {
        idx = 0;
        frac = 0.0;
    } else if (idx > max_index) {
        idx = max_index;
        frac = 0.0;
    }

    int i0 = idx - 1;
    int i1 = idx;
    int i2 = idx + 1;
    int i3 = idx + 2;
    if (i0 < 0) i0 = 0;
    if (i2 > max_index) i2 = max_index;
    if (i3 > max_index) i3 = max_index;

    const int b0 = i0 * words;
    const int b1 = i1 * words;
    const int b2 = i2 * words;
    const int b3 = i3 * words;

    for (int c = 0; c < out_words; c++) {
        dst[c] = slow_motion_cubic_interp_s16(
            in[b0 + c], in[b1 + c], in[b2 + c], in[b3 + c], frac);
    }
}

static int normal_ctx_turn_candidate_cost_s16(const uint8_t *ctx,
                                              int ctx_samples,
                                              double ctx_abs_start,
                                              const slow_scratch_ctx_map_t *ctx_map,
                                              double base_pos,
                                              double start_pos,
                                              double v0,
                                              double v1,
                                              int turn_samples,
                                              int frame_bytes,
                                              const uint8_t *prev_prev_frame,
                                              const uint8_t *prev_frame,
                                              int *value_delta_out,
                                              int *slope_delta_out,
                                              int *step_max_out)
{
    if (value_delta_out != NULL)
        *value_delta_out = 0x3fffffff;
    if (slope_delta_out != NULL)
        *slope_delta_out = 0x3fffffff;
    if (step_max_out != NULL)
        *step_max_out = 0x3fffffff;

    if (ctx == NULL || prev_frame == NULL || ctx_samples <= 0 ||
        frame_bytes <= 0 || (frame_bytes & 1))
        return 0x3fffffff;

    const int words = frame_bytes / 2;
    const int local_words = (words > 8) ? 8 : words;
    const int preview = (turn_samples < 32) ? turn_samples : 32;
    const int16_t *prev = (const int16_t*)prev_frame;
    const int16_t *prev2 = (const int16_t*)prev_prev_frame;
    int16_t cur[8];
    int16_t oldv[8];
    int16_t newv[8];
    int16_t last[8];
    int have_last = 0;
    int value_delta = 0;
    int slope_delta = 0;
    int step_max = 0;
    int cost;

    normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                start_pos, frame_bytes, cur, local_words);

    for (int c = 0; c < local_words; c++) {
        int d = (int)cur[c] - (int)prev[c];
        d = (d < 0) ? -d : d;
        if (d > value_delta)
            value_delta = d;
        last[c] = cur[c];
    }
    have_last = 1;

    for (int i = 1; i < preview; i++) {
        const double u = (turn_samples > 1) ? ((double)i / (double)(turn_samples - 1)) : 1.0;
        const double w = u * u * (3.0 - (2.0 * u));
        normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                    start_pos + (v0 * (double)i),
                                    frame_bytes, oldv, local_words);
        normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                    base_pos + (v1 * (double)i),
                                    frame_bytes, newv, local_words);
        for (int c = 0; c < local_words; c++) {
            const double mixed = ((1.0 - w) * (double)oldv[c]) + (w * (double)newv[c]);
            int sample = (int)((mixed >= 0.0) ? (mixed + 0.5) : (mixed - 0.5));
            if (sample < -32768) sample = -32768;
            if (sample >  32767) sample =  32767;
            if (have_last) {
                int st = sample - (int)last[c];
                st = (st < 0) ? -st : st;
                if (st > step_max)
                    step_max = st;
            }
            last[c] = (int16_t)sample;
        }
    }

    if (prev_prev_frame != NULL) {
        normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                    start_pos + v0, frame_bytes, cur, local_words);
        for (int c = 0; c < local_words; c++) {
            int old_slope = (int)prev[c] - (int)prev2[c];
            int new_slope = (int)cur[c] - (int)prev[c];
            int sd = new_slope - old_slope;
            sd = (sd < 0) ? -sd : sd;
            if (sd > slope_delta)
                slope_delta = sd;
        }
    } else {
        slope_delta = step_max;
    }

    cost = (value_delta * 8) + (slope_delta * 12) + (step_max * 18);
    if (value_delta > 4096)
        cost += (value_delta - 4096) * 48;
    if (slope_delta > 8192)
        cost += (slope_delta - 8192) * 24;
    if (step_max > 768)
        cost += (step_max - 768) * 64;

    if (value_delta_out != NULL)
        *value_delta_out = value_delta;
    if (slope_delta_out != NULL)
        *slope_delta_out = slope_delta;
    if (step_max_out != NULL)
        *step_max_out = step_max;

    return cost;
}

static int perform_normal_turn_render_s16(uint8_t *dst,
                                          int dst_samples,
                                          const uint8_t *ctx,
                                          int ctx_samples,
                                          double ctx_abs_start,
                                          const slow_scratch_ctx_map_t *ctx_map,
                                          double base_pos,
                                          double start_pos,
                                          double v0,
                                          double v1,
                                          int turn_samples,
                                          int frame_bytes,
                                          int *step_max_out,
                                          int *step_avg_out)
{
    if (step_max_out != NULL)
        *step_max_out = 0;
    if (step_avg_out != NULL)
        *step_avg_out = 0;

    if (dst == NULL || ctx == NULL || dst_samples <= 0 || ctx_samples <= 0 ||
        frame_bytes <= 0 || (frame_bytes & 1))
        return 0;

    if (turn_samples < 8)
        turn_samples = 8;
    if (turn_samples > dst_samples)
        turn_samples = dst_samples;

    const int words = frame_bytes / 2;
    const int local_words = (words > 8) ? 8 : words;
    int16_t *out = (int16_t*)dst;
    int16_t oldv[8];
    int16_t newv[8];
    int16_t prev_words[8];
    int prev_valid = 0;
    int step_peak = 0;
    int64_t step_sum = 0;
    int step_n = 0;

    for (int i = 0; i < dst_samples; i++) {
        const int bo = i * words;
        int frame_step = 0;

        if (i < turn_samples) {
            const double u = (turn_samples > 1) ? ((double)i / (double)(turn_samples - 1)) : 1.0;
            const double w = u * u * (3.0 - (2.0 * u));
            normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                        start_pos + (v0 * (double)i),
                                        frame_bytes, oldv, local_words);
            normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                        base_pos + (v1 * (double)i),
                                        frame_bytes, newv, local_words);

            if (words <= 8) {
                for (int c = 0; c < words; c++) {
                    const double mixed = ((1.0 - w) * (double)oldv[c]) + (w * (double)newv[c]);
                    int sample = (int)((mixed >= 0.0) ? (mixed + 0.5) : (mixed - 0.5));
                    if (sample < -32768) sample = -32768;
                    if (sample >  32767) sample =  32767;
                    out[bo + c] = (int16_t)sample;
                    if (prev_valid) {
                        int d = sample - (int)prev_words[c];
                        d = (d < 0) ? -d : d;
                        if (d > frame_step)
                            frame_step = d;
                    }
                    prev_words[c] = (int16_t)sample;
                }
            } else {
                for (int c = 0; c < words; c++) {
                    int16_t a[1];
                    int16_t b[1];
                    normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                                start_pos + (v0 * (double)i),
                                                frame_bytes, a, 1);
                    normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                                base_pos + (v1 * (double)i),
                                                frame_bytes, b, 1);
                    const double mixed = ((1.0 - w) * (double)a[0]) + (w * (double)b[0]);
                    int sample = (int)((mixed >= 0.0) ? (mixed + 0.5) : (mixed - 0.5));
                    if (sample < -32768) sample = -32768;
                    if (sample >  32767) sample =  32767;
                    out[bo + c] = (int16_t)sample;
                }
            }
        } else {
            int16_t sample_words[8];
            normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                        base_pos + (v1 * (double)i),
                                        frame_bytes, sample_words, local_words);
            for (int c = 0; c < local_words; c++) {
                out[bo + c] = sample_words[c];
                if (prev_valid) {
                    int d = (int)sample_words[c] - (int)prev_words[c];
                    d = (d < 0) ? -d : d;
                    if (d > frame_step)
                        frame_step = d;
                }
                prev_words[c] = sample_words[c];
            }
        }

        if (prev_valid) {
            if (frame_step > step_peak)
                step_peak = frame_step;
            step_sum += frame_step;
            step_n++;
        }
        prev_valid = 1;
    }

    if (step_max_out != NULL)
        *step_max_out = step_peak;
    if (step_avg_out != NULL)
        *step_avg_out = (step_n > 0) ? (int)(step_sum / step_n) : 0;

    return dst_samples;
}

static int perform_normal_direction_turn(veejay_t *info,
                                         editlist *el,
                                         performer_t *p,
                                         uint8_t *audio_buf,
                                         uint8_t *ctx_buf,
                                         int pred_len,
                                         int frame_bytes,
                                         long long cur_frame,
                                         sample_b_t *sample_ptr,
                                         int cur_dir,
                                         int last_dir,
                                         int pending_edge)
{
    if (info == NULL || el == NULL || p == NULL || audio_buf == NULL ||
        ctx_buf == NULL || sample_ptr == NULL || pred_len <= 0 || frame_bytes <= 0 ||
        cur_dir == 0 || last_dir == 0 || cur_dir == last_dir)
        return 0;

    int out_samples = slow_motion_frame_len_exact(el, cur_frame, pred_len);
    if (out_samples <= 0)
        out_samples = pred_len;
    if (out_samples > pred_len + 64)
        out_samples = pred_len + 64;

    const double base_pos = slow_motion_sync_abs_pos(el, sample_ptr, cur_frame,
                                                     pred_len, 1, 0, cur_dir);
    int turn_speed = abs(sample_ptr->speed);
    if (turn_speed < 1)
        turn_speed = 1;
    if (turn_speed > 4)
        turn_speed = 4;
    const double v0 = (double)(last_dir * turn_speed);
    const double v1 = (double)(cur_dir * turn_speed);
    int phase_radius = 768 * turn_speed;
    if (phase_radius > 3072)
        phase_radius = 3072;

    int turn_samples = 96;
    if (turn_samples > out_samples)
        turn_samples = out_samples;
    if (turn_samples < 16)
        turn_samples = (out_samples < 16) ? out_samples : 16;

    const double direct_end = base_pos + (v1 * (double)((out_samples > 1) ? (out_samples - 1) : 0));
    const double turn_end = base_pos + (v1 * (double)((turn_samples > 1) ? (turn_samples - 1) : 0));
    const double turn_overshoot = base_pos + (v0 * (double)turn_samples);

    double minp = base_pos - (double)phase_radius;
    double maxp = base_pos + (double)phase_radius;
    if (direct_end < minp) minp = direct_end;
    if (direct_end > maxp) maxp = direct_end;
    if (turn_end < minp) minp = turn_end;
    if (turn_end > maxp) maxp = turn_end;
    if (turn_overshoot < minp) minp = turn_overshoot;
    if (turn_overshoot > maxp) maxp = turn_overshoot;

    double ctx_abs_start = 0.0;
    int ctx_samples = 0;
    long long ctx_first = 0;
    long long ctx_last = 0;
    slow_scratch_ctx_map_t ctx_map;
    veejay_memset(&ctx_map, 0, sizeof(ctx_map));

    int got_ctx = slow_motion_fetch_scratch_context(info, el, sample_ptr,
                                                    ctx_buf, &ctx_map,
                                                    pred_len, frame_bytes,
                                                    cur_dir, minp, maxp,
                                                    &ctx_abs_start,
                                                    &ctx_samples,
                                                    &ctx_first,
                                                    &ctx_last);
    if (got_ctx <= 0)
        return 0;

    int phase_shift = 0;
    int phase_delta = -1;
    int phase_slope = -1;
    int phase_step = -1;
    if (sample_ptr->audio_diag_valid &&
        sample_ptr->audio_diag_frame_bytes == frame_bytes) {
        int best_cost = 0x3fffffff;
        int best_off = 0;
        int best_delta = 0x3fffffff;
        int best_slope = 0x3fffffff;
        int best_step = 0x3fffffff;

        for (int off = -phase_radius; off <= phase_radius; off += 16) {
            int d = 0, sd = 0, st = 0;
            int cost = normal_ctx_turn_candidate_cost_s16(ctx_buf, ctx_samples,
                                                          ctx_abs_start, &ctx_map,
                                                          base_pos, base_pos + (double)off,
                                                          v0, v1, turn_samples,
                                                          frame_bytes,
                                                          sample_ptr->audio_diag_prev_frame,
                                                          sample_ptr->audio_diag_last_frame,
                                                          &d, &sd, &st);
            cost += ((off < 0 ? -off : off) * 1);
            if (cost < best_cost) {
                best_cost = cost;
                best_off = off;
                best_delta = d;
                best_slope = sd;
                best_step = st;
            }
        }

        int fine_lo = best_off - 8;
        int fine_hi = best_off + 8;
        if (fine_lo < -phase_radius) fine_lo = -phase_radius;
        if (fine_hi >  phase_radius) fine_hi =  phase_radius;

        for (int off = fine_lo; off <= fine_hi; off++) {
            int d = 0, sd = 0, st = 0;
            int cost = normal_ctx_turn_candidate_cost_s16(ctx_buf, ctx_samples,
                                                          ctx_abs_start, &ctx_map,
                                                          base_pos, base_pos + (double)off,
                                                          v0, v1, turn_samples,
                                                          frame_bytes,
                                                          sample_ptr->audio_diag_prev_frame,
                                                          sample_ptr->audio_diag_last_frame,
                                                          &d, &sd, &st);
            cost += ((off < 0 ? -off : off) * 1);
            if (cost < best_cost) {
                best_cost = cost;
                best_off = off;
                best_delta = d;
                best_slope = sd;
                best_step = st;
            }
        }

        phase_shift = best_off;
        phase_delta = best_delta;
        phase_slope = best_slope;
        phase_step = best_step;
    }

    const int unsafe_step_limit = (turn_speed > 1) ? 1024 : 768;
    const int unsafe_slope_limit = (turn_speed > 1) ? 14000 : 12000;
    if (phase_delta > 4096 || phase_slope > unsafe_slope_limit || phase_step > unsafe_step_limit) {
        /*veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-DIAG] normal-turn-skip stage=59 edge=%s(%d) flip=1 speed=%d dir=%d target=%lld phase_delta=%d phase_slope=%d phase_step=%d phase=%d reason=unsafe-candidate mode=guarded-dual-head-microfade",
                   vj_audio_edge_name(pending_edge), pending_edge,
                   sample_ptr->speed, cur_dir, cur_frame,
                   phase_delta, phase_slope, phase_step, phase_shift); */
        return 0;
    }

    int abs_shift = (phase_shift < 0) ? -phase_shift : phase_shift;
    if (phase_delta > 2500 || phase_slope > 8000 || phase_step > 512)
        turn_samples = 128;
    else if (abs_shift > 384)
        turn_samples = 160;
    else if (abs_shift > 192)
        turn_samples = 144;
    else if (abs_shift > 64)
        turn_samples = 112;

    if (turn_samples > out_samples)
        turn_samples = out_samples;

    const double start_pos = base_pos + (double)phase_shift;

    int step_max = 0;
    int step_avg = 0;
    int copied = perform_normal_turn_render_s16(audio_buf, out_samples,
                                                ctx_buf, ctx_samples,
                                                ctx_abs_start, &ctx_map,
                                                base_pos, start_pos, v0, v1,
                                                turn_samples,
                                                frame_bytes,
                                                &step_max, &step_avg);
    if (copied <= 0)
        return 0;

    int edge_delta = -1;
    if (sample_ptr->audio_diag_valid && sample_ptr->audio_diag_frame_bytes == frame_bytes) {
        edge_delta = vj_audio_frame_delta_s16(audio_buf,
                                              sample_ptr->audio_diag_last_frame,
                                              frame_bytes);
    }

    const int peak = vj_audio_peak_s16(audio_buf, copied, frame_bytes);

    const int declick_path = (turn_speed > 1) ? AUDIO_PATH_FAST : AUDIO_PATH_DIRECT;
    const int hard_turn_boundary =
        (edge_delta >= 3072 ||
         phase_slope >= 8192 ||
         phase_step >= 512 ||
         step_max >= 1024 ||
         peak >= 32760);

    if (hard_turn_boundary) {
        vj_audio_declick_apply(p, audio_buf, copied, frame_bytes,
                               declick_path, sample_ptr->speed, cur_dir,
                               AUDIO_EDGE_DIRECTION, 1);
    } else {
        vj_audio_declick_observe(p, audio_buf, copied, frame_bytes,
                                 declick_path, sample_ptr->speed, cur_dir);
    }

    /*if (edge_delta >= 768 || step_max >= 768 || peak >= 32760) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-DIAG] normal-turn stage=59 edge=%s(%d) flip=1 speed=%d dir=%d target=%lld samples=%d turn_speed=%d delta=%d phase_delta=%d phase_slope=%d phase_step=%d peak=%d step_max=%d step_avg=%d phase=%d turn=%d base=%.3f start=%.3f v0=%.3f v1=%.3f ctx=%lld..%lld mode=guarded-dual-head-microfade",
                   vj_audio_edge_name(pending_edge), pending_edge,
                   sample_ptr->speed, cur_dir, cur_frame, copied, turn_speed,
                   edge_delta, phase_delta, phase_slope, phase_step, peak, step_max, step_avg,
                   phase_shift, turn_samples,
                   base_pos, start_pos, v0, v1, ctx_first, ctx_last);
    }*/

    sample_ptr->scratch_initialized = 0;
    sample_ptr->scratch_pos = 0.0;
    sample_ptr->scratch_vel = 0.0;
    sample_ptr->scratch_target_vel = 0.0;
    sample_ptr->scratch_last_dir = 0;
    sample_ptr->scratch_last_sfd = 0;
    sample_ptr->scratch_ramp_left = 0;
    sample_ptr->scratch_sync_bias = 0.0;
    sample_ptr->scratch_sync_hold_blocks = 0;
    sample_ptr->scratch_stable_blocks = 0;

    return copied;
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
    if (el == NULL || p == NULL || audio_buf == NULL || temporary_buffer == NULL ||
        sample_ptr == NULL || pred_len <= 0 || sample_size <= 0)
        return 0;

    const int speed = sample_ptr->speed;
    const int frame_bytes = sample_size;
    const int abs_speed = abs(speed);
    const int cur_dir = (speed > 0) ? 1 : ((speed < 0) ? -1 : 0);
    audio_edge_t *edge = p->audio_edge;

    int pending_edge = AUDIO_EDGE_NONE;
    int last_dir = 0;

    if (edge != NULL) {
        pending_edge = atomic_load_int(&edge->pending_edge);
        last_dir = atomic_load_int(&edge->last_direction);
    }

    if (pending_edge == AUDIO_EDGE_SILENCE && cur_dir != 0)
        pending_edge = AUDIO_EDGE_JUMP;

    const int direction_flipped =
        (last_dir != 0 && cur_dir != 0 && last_dir != cur_dir);

    /*if (pending_edge != AUDIO_EDGE_NONE || direction_flipped) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-DIAG] normal-edge edge=%s(%d) flip=%d last_dir=%d cur_dir=%d speed=%d target=%lld pred=%d",
                   vj_audio_edge_name(pending_edge), pending_edge, direction_flipped,
                   last_dir, cur_dir, speed, cur_frame, pred_len);
    }*/

    if (speed == 0 || pending_edge == AUDIO_EDGE_SILENCE) {
        veejay_memset(audio_buf, 0, pred_len * frame_bytes);

        vj_audio_declick_apply(p, audio_buf, pred_len, frame_bytes,
                            AUDIO_PATH_SILENCE, 0, 0,
                            pending_edge, direction_flipped);

        vj_perform_clear_audio_edges(info, edge, 0);
        sample_ptr->direction_changed = 0;
        sample_ptr->prev_n_samples = pred_len;
        return pred_len;
    }

    if (abs_speed <= 1) {
        if (vj_audio_edge_is_hard(pending_edge) && p->audio_scratcher != NULL)
            vj_scratch_reset(p->audio_scratcher);

        const int stale_direction_edge =
            (pending_edge == AUDIO_EDGE_DIRECTION && !direction_flipped);
        const int effective_edge = stale_direction_edge ? AUDIO_EDGE_NONE : pending_edge;
        const int normal_turn = direction_flipped &&
            (pending_edge == AUDIO_EDGE_NONE || pending_edge == AUDIO_EDGE_DIRECTION);

        int n_samples = 0;
        int guarded_turn_skipped = 0;
        if (normal_turn) {
            n_samples = perform_normal_direction_turn(info, el, p,
                                                      audio_buf,
                                                      temporary_buffer,
                                                      pred_len,
                                                      frame_bytes,
                                                      cur_frame,
                                                      sample_ptr,
                                                      cur_dir,
                                                      last_dir,
                                                      pending_edge);
            guarded_turn_skipped = (n_samples <= 0);
        }

        if (n_samples <= 0) {
            n_samples = get_audio_frame_safe(info, el, cur_frame,
                                             audio_buf, pred_len,
                                             frame_bytes, speed);

            if (speed < 0)
                vj_audio_reverse_buffer(audio_buf, n_samples, frame_bytes);

            if (guarded_turn_skipped) {
                vj_audio_declick_apply(p, audio_buf, n_samples, frame_bytes,
                                       AUDIO_PATH_DIRECT, speed, cur_dir,
                                       AUDIO_EDGE_DIRECTION, 1);
                /*veejay_msg(VEEJAY_MSG_DEBUG,
                           "[AUDIO-DIAG] normal-turn-fallback stage=60 edge=%s(%d) flip=1 speed=%d dir=%d target=%lld reason=unsafe-declick-fallback mode=direct-copy-declick",
                           vj_audio_edge_name(pending_edge), pending_edge,
                           speed, cur_dir, cur_frame);  */
            } else {
                vj_audio_declick_apply(p, audio_buf, n_samples, frame_bytes,
                                    AUDIO_PATH_DIRECT, speed, cur_dir,
                                    effective_edge, 0);
            }
        }

        slow_motion_update_turn_history(sample_ptr, audio_buf, n_samples, frame_bytes);

        if (sample_ptr->audio_diag_valid &&
            sample_ptr->audio_diag_frame_bytes == frame_bytes) {
            vj_audio_copy_last_frame(sample_ptr->audio_diag_prev_frame,
                                     (int)sizeof(sample_ptr->audio_diag_prev_frame),
                                     sample_ptr->audio_diag_last_frame, 1, frame_bytes);
        } else {
            veejay_memset(sample_ptr->audio_diag_prev_frame, 0,
                          sizeof(sample_ptr->audio_diag_prev_frame));
        }

        vj_audio_copy_last_frame(sample_ptr->audio_diag_last_frame,
                                 (int)sizeof(sample_ptr->audio_diag_last_frame),
                                 audio_buf, n_samples, frame_bytes);
        sample_ptr->audio_diag_valid = 1;
        sample_ptr->audio_diag_frame_bytes = frame_bytes;

        vj_perform_clear_audio_edges(info, edge, cur_dir);

        sample_ptr->direction_changed = 0;
        sample_ptr->prev_n_samples = n_samples;
        return n_samples;
    }

    if (vj_audio_edge_is_hard(pending_edge) && p->audio_scratcher != NULL)
        vj_scratch_reset(p->audio_scratcher);

    int n_frames = abs_speed;
    n_frames = (n_frames >= MAX_SPEED) ? (MAX_SPEED - 1) : n_frames;
    n_frames = (n_frames < 1) ? 1 : n_frames;

    const int fast_turn = direction_flipped &&
        (pending_edge == AUDIO_EDGE_NONE || pending_edge == AUDIO_EDGE_DIRECTION);

    if (fast_turn) {
        int turn_out = perform_normal_direction_turn(info, el, p,
                                                     audio_buf,
                                                     temporary_buffer,
                                                     pred_len,
                                                     frame_bytes,
                                                     cur_frame,
                                                     sample_ptr,
                                                     cur_dir,
                                                     last_dir,
                                                     pending_edge);
        if (turn_out > 0) {
            slow_motion_update_turn_history(sample_ptr, audio_buf, turn_out, frame_bytes);
            if (sample_ptr->audio_diag_valid &&
                sample_ptr->audio_diag_frame_bytes == frame_bytes) {
                vj_audio_copy_last_frame(sample_ptr->audio_diag_prev_frame,
                                         (int)sizeof(sample_ptr->audio_diag_prev_frame),
                                         sample_ptr->audio_diag_last_frame, 1, frame_bytes);
            } else {
                veejay_memset(sample_ptr->audio_diag_prev_frame, 0,
                              sizeof(sample_ptr->audio_diag_prev_frame));
            }
            vj_audio_copy_last_frame(sample_ptr->audio_diag_last_frame,
                                     (int)sizeof(sample_ptr->audio_diag_last_frame),
                                     audio_buf, turn_out, frame_bytes);
            sample_ptr->audio_diag_valid = 1;
            sample_ptr->audio_diag_frame_bytes = frame_bytes;
            vj_perform_clear_audio_edges(info, edge, cur_dir);
            sample_ptr->direction_changed = 0;
            sample_ptr->prev_n_samples = turn_out;
            return turn_out;
        }

        /*veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-DIAG] fast-turn-fallback stage=60 edge=%s(%d) flip=1 speed=%d dir=%d target=%lld reason=unsafe-declick-fallback",
                   vj_audio_edge_name(pending_edge), pending_edge,
                   speed, cur_dir, cur_frame); */
    }

    uint8_t *tmp_ptr = temporary_buffer;
    int total_input_samples = 0;

    for (int i = 0; i < n_frames; i++) {
        long long f = (speed < 0)
            ? (cur_frame - (long long)(n_frames - 1 - i))
            : (cur_frame + (long long)i);

        int got = get_audio_frame_safe(info, el, f, tmp_ptr,
                                       pred_len, frame_bytes, speed);

        total_input_samples += got;
        tmp_ptr += (size_t)got * (size_t)frame_bytes;
    }

    int out = vj_audio_resample_block_s16(
        audio_buf,
        pred_len,
        temporary_buffer,
        total_input_samples,
        (double)speed,
        frame_bytes
    );

    if (fast_turn) {
        vj_audio_declick_apply(p, audio_buf, out, frame_bytes,
                               AUDIO_PATH_FAST, speed, cur_dir,
                               AUDIO_EDGE_DIRECTION, 1);
    } else {
        vj_audio_declick_apply(p, audio_buf, out, frame_bytes,
                               AUDIO_PATH_FAST, speed, cur_dir,
                               pending_edge, direction_flipped);
    }

    slow_motion_update_turn_history(sample_ptr, audio_buf, out, frame_bytes);
    if (sample_ptr->audio_diag_valid &&
        sample_ptr->audio_diag_frame_bytes == frame_bytes) {
        vj_audio_copy_last_frame(sample_ptr->audio_diag_prev_frame,
                                 (int)sizeof(sample_ptr->audio_diag_prev_frame),
                                 sample_ptr->audio_diag_last_frame, 1, frame_bytes);
    } else {
        veejay_memset(sample_ptr->audio_diag_prev_frame, 0,
                      sizeof(sample_ptr->audio_diag_prev_frame));
    }
    vj_audio_copy_last_frame(sample_ptr->audio_diag_last_frame,
                             (int)sizeof(sample_ptr->audio_diag_last_frame),
                             audio_buf, out, frame_bytes);
    sample_ptr->audio_diag_valid = 1;
    sample_ptr->audio_diag_frame_bytes = frame_bytes;

    vj_perform_clear_audio_edges(info, edge, cur_dir);

    sample_ptr->direction_changed = 0;
    sample_ptr->prev_n_samples = out;
    return out;
}

static int slow_motion_turn_history_capacity(int frame_bytes)
{
    if (frame_bytes <= 0)
        return 0;

    int cap = AUDIO_TURN_HISTORY_BYTES / frame_bytes;
    if (cap < 0)
        cap = 0;
    if (cap > 1024)
        cap = 1024;
    return cap;
}

static void slow_motion_clear_turn_history(sample_b_t *posdata)
{
    if (posdata == NULL)
        return;

    posdata->audio_turn_history_samples = 0;
    posdata->audio_turn_history_frame_bytes = 0;
}

static void slow_motion_update_turn_history(sample_b_t *posdata,
                                            const uint8_t *buf,
                                            int samples,
                                            int frame_bytes)
{
    if (posdata == NULL || buf == NULL || samples <= 0 || frame_bytes <= 0)
        return;

    const int cap = slow_motion_turn_history_capacity(frame_bytes);
    if (cap <= 0)
        return;

    if (posdata->audio_turn_history_frame_bytes != frame_bytes) {
        posdata->audio_turn_history_samples = 0;
        posdata->audio_turn_history_frame_bytes = frame_bytes;
    }

    int keep_from_new = samples;
    if (keep_from_new > cap)
        keep_from_new = cap;

    const uint8_t *src = buf + ((size_t)(samples - keep_from_new) * (size_t)frame_bytes);

    if (keep_from_new >= cap) {
        veejay_memcpy(posdata->audio_turn_history, src,
                      (size_t)cap * (size_t)frame_bytes);
        posdata->audio_turn_history_samples = cap;
        return;
    }

    int old_keep = posdata->audio_turn_history_samples;
    if (old_keep < 0)
        old_keep = 0;
    if (old_keep > cap)
        old_keep = cap;

    if (old_keep + keep_from_new > cap)
        old_keep = cap - keep_from_new;

    if (old_keep > 0) {
        memmove(posdata->audio_turn_history,
                posdata->audio_turn_history +
                    ((size_t)(posdata->audio_turn_history_samples - old_keep) * (size_t)frame_bytes),
                (size_t)old_keep * (size_t)frame_bytes);
    }

    veejay_memcpy(posdata->audio_turn_history + ((size_t)old_keep * (size_t)frame_bytes),
                  src,
                  (size_t)keep_from_new * (size_t)frame_bytes);
    posdata->audio_turn_history_samples = old_keep + keep_from_new;
    posdata->audio_turn_history_frame_bytes = frame_bytes;
}

static inline double slow_motion_clampd(double v, double lo, double hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline double slow_motion_exact_samples_per_frame(const editlist *el, int pred_len)
{
    if (el == NULL || el->audio_rate <= 0 || el->video_fps <= 0.0)
        return (double)((pred_len > 0) ? pred_len : 1);

    return (double)el->audio_rate / (double)el->video_fps;
}

static inline double slow_motion_frame_abs_start_exact(const editlist *el,
                                                       long long frame,
                                                       int pred_len)
{
    if (frame <= 0)
        return 0.0;

    const double spf = slow_motion_exact_samples_per_frame(el, pred_len);
    return floor(((double)frame * spf) + 1.0e-9);
}

static inline int slow_motion_frame_len_exact(const editlist *el,
                                              long long frame,
                                              int pred_len)
{
    const double a = slow_motion_frame_abs_start_exact(el, frame, pred_len);
    const double b = slow_motion_frame_abs_start_exact(el, frame + 1, pred_len);
    int n = (int)(b - a);

    if (n <= 0)
        n = (pred_len > 0) ? pred_len : 1;
    return n;
}

static inline long long slow_motion_frame_for_abs_pos_exact(const editlist *el,
                                                            double pos,
                                                            int pred_len)
{
    if (pos <= 0.0)
        return 0;

    const double spf = slow_motion_exact_samples_per_frame(el, pred_len);
    if (spf <= 0.0)
        return (long long)floor(pos / (double)((pred_len > 0) ? pred_len : 1));

    long long f = (long long)floor(pos / spf);
    if (f < 0)
        f = 0;
    return f;
}

static inline int slow_motion_has_valid_frame_bounds(const sample_b_t *posdata)
{
    return (posdata != NULL && posdata->end > posdata->start);
}

static inline int16_t slow_motion_cubic_interp_s16(int p0, int p1, int p2, int p3, double t)
{
    double t2 = t * t;
    double t3 = t2 * t;
    double y = 0.5 * ((2.0 * (double)p1) +
        ((double)(-p0 + p2) * t) +
        ((double)(2 * p0 - 5 * p1 + 4 * p2 - p3) * t2) +
        ((double)(-p0 + 3 * p1 - 3 * p2 + p3) * t3));

    int yi = (int)((y >= 0.0) ? (y + 0.5) : (y - 0.5));
    if (yi > 32767)
        yi = 32767;
    else if (yi < -32768)
        yi = -32768;
    return (int16_t)yi;
}

static void slow_motion_clear_scratch_head(sample_b_t *posdata)
{
    if (posdata == NULL)
        return;

    posdata->scratch_initialized = 0;
    posdata->scratch_pos = 0.0;
    posdata->scratch_vel = 0.0;
    posdata->scratch_target_vel = 0.0;
    posdata->scratch_last_sync_pos = 0.0;
    posdata->scratch_last_sync_error = 0.0;
    posdata->scratch_sync_bias = 0.0;
    posdata->scratch_sync_hold_blocks = 0;
    posdata->scratch_stable_blocks = 0;
    posdata->scratch_last_dir = 0;
    posdata->scratch_last_sfd = 0;
    posdata->scratch_ramp_left = 0;
    posdata->scratch_last_reset = 0;
}

static double slow_motion_sync_abs_pos(const editlist *el,
                                       const sample_b_t *posdata,
                                       long long source_frame,
                                       int pred_len,
                                       int slice_count,
                                       int cur_slice,
                                       int direction)
{
    if (pred_len <= 0)
        return 0.0;

    if (slice_count <= 1)
        slice_count = 1;
    cur_slice = vj_audio_clampi(cur_slice, 0, slice_count - 1);

    const double frame_start = slow_motion_frame_abs_start_exact(el, source_frame, pred_len);
    const int frame_samples = slow_motion_frame_len_exact(el, source_frame, pred_len);
    const double slice_phase = ((double)cur_slice * (double)frame_samples) / (double)slice_count;

    double p = (direction >= 0)
        ? (frame_start + slice_phase)
        : (frame_start + (double)(frame_samples - 1) - slice_phase);

    if (slow_motion_has_valid_frame_bounds(posdata)) {
        const double lo = slow_motion_frame_abs_start_exact(el, posdata->start, pred_len);
        const double hi = slow_motion_frame_abs_start_exact(el, posdata->end + 1, pred_len) - 1.0;
        p = slow_motion_clampd(p, lo, hi);
    }

    return p;
}

static int slow_motion_fetch_scratch_context(veejay_t *info,
                                             editlist *el,
                                             sample_b_t *posdata,
                                             uint8_t *ctx,
                                             slow_scratch_ctx_map_t *map,
                                             int pred_len,
                                             int frame_bytes,
                                             int speed_int,
                                             double pos_a,
                                             double pos_b,
                                             double *ctx_abs_start_out,
                                             int *ctx_samples_out,
                                             long long *ctx_first_frame_out,
                                             long long *ctx_last_frame_out)
{
    if (ctx_abs_start_out != NULL)
        *ctx_abs_start_out = 0.0;
    if (ctx_samples_out != NULL)
        *ctx_samples_out = 0;
    if (ctx_first_frame_out != NULL)
        *ctx_first_frame_out = 0;
    if (ctx_last_frame_out != NULL)
        *ctx_last_frame_out = 0;

    if (map != NULL)
        veejay_memset(map, 0, sizeof(*map));

    if (info == NULL || el == NULL || posdata == NULL || ctx == NULL ||
        pred_len <= 0 || frame_bytes <= 0)
        return 0;

    double minp = (pos_a < pos_b) ? pos_a : pos_b;
    double maxp = (pos_a > pos_b) ? pos_a : pos_b;

    long long first = slow_motion_frame_for_abs_pos_exact(el, minp, pred_len) - 3;
    long long last  = slow_motion_frame_for_abs_pos_exact(el, maxp, pred_len) + 3;

    if (slow_motion_has_valid_frame_bounds(posdata)) {
        if (first < posdata->start)
            first = posdata->start;
        if (last > posdata->end)
            last = posdata->end;
    }

    if (first < 0)
        first = 0;
    if (last < first)
        last = first;

    const int max_frames = SLOW_SCRATCH_MAX_CTX_FRAMES;
    if ((last - first + 1) > max_frames) {
        long long center = slow_motion_frame_for_abs_pos_exact(el, (pos_a + pos_b) * 0.5, pred_len);
        first = center - (max_frames / 2);
        last = first + max_frames - 1;
        if (first < 0) {
            first = 0;
            last = first + max_frames - 1;
        }
        if (slow_motion_has_valid_frame_bounds(posdata)) {
            if (first < posdata->start) {
                first = posdata->start;
                last = first + max_frames - 1;
            }
            if (last > posdata->end) {
                last = posdata->end;
                first = last - max_frames + 1;
                if (first < posdata->start)
                    first = posdata->start;
            }
        }
    }

    const int frames = (int)(last - first + 1);
    if (frames <= 0)
        return 0;

    const int scratch_capacity_samples = (512 * 1024) / frame_bytes;
    const int slot_capacity = pred_len + 64;
    const int context_capacity_samples = frames * slot_capacity;

    if (context_capacity_samples > scratch_capacity_samples) {
        /*veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-DIAG] scratch-context stage=59 no-room frames=%d need=%d cap=%d",
                   frames, context_capacity_samples, scratch_capacity_samples); */
        return 0;
    }

    uint8_t *dst = ctx;
    int total_samples = 0;
    int min_got = 0;
    int max_got = 0;
    int over_frames = 0;

    for (int i = 0; i < frames; i++) {
        long long f = first + (long long)i;

        veejay_memset(dst, 0, (size_t)slot_capacity * (size_t)frame_bytes);

        int got = get_audio_frame_safe(info, el, f, dst, pred_len, frame_bytes, speed_int);
        if (got < 0)
            got = 0;

        if (got > slot_capacity) {
            got = slot_capacity;
            over_frames++;
        }

        if (map != NULL && i < SLOW_SCRATCH_MAX_CTX_FRAMES) {
            map->frame_len[i] = got;
            map->frame_off[i] = total_samples;
            map->exact_start[i] = slow_motion_frame_abs_start_exact(el, f, pred_len);
            map->exact_len[i] = (double)slow_motion_frame_len_exact(el, f, pred_len);
            if (map->exact_len[i] <= 0.0)
                map->exact_len[i] = (double)((got > 0) ? got : pred_len);
        }

        if (i == 0 || got < min_got)
            min_got = got;
        if (i == 0 || got > max_got)
            max_got = got;

        dst += (size_t)got * (size_t)frame_bytes;
        total_samples += got;
    }

    const double exact_start = slow_motion_frame_abs_start_exact(el, first, pred_len);
    const double exact_end = slow_motion_frame_abs_start_exact(el, last + 1, pred_len);
    const int exact_total = (int)(exact_end - exact_start);
    const int map_err = total_samples - exact_total;

    if (map != NULL) {
        map->valid = 1;
        map->frames = frames;
        map->first_frame = first;
        map->last_frame = last;
    }
/*
    if (map_err <= -3 || map_err >= 3 || over_frames > 0) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-DIAG] scratch-context stage=59 actual-prefix frames=%d got=%d..%d total=%d exact=%d map_err=%d over=%d pred=%d ctx=%lld..%lld",
                   frames, min_got, max_got, total_samples, exact_total, map_err,
                   over_frames, pred_len, first, last);
    } */

    if (ctx_abs_start_out != NULL)
        *ctx_abs_start_out = exact_start;
    if (ctx_samples_out != NULL)
        *ctx_samples_out = total_samples;
    if (ctx_first_frame_out != NULL)
        *ctx_first_frame_out = first;
    if (ctx_last_frame_out != NULL)
        *ctx_last_frame_out = last;

    return total_samples;
}

static double slow_motion_ctx_exact_to_actual_rel(const slow_scratch_ctx_map_t *map,
                                                  double abs_pos,
                                                  int ctx_samples)
{
    if (map == NULL || !map->valid || map->frames <= 0 || ctx_samples <= 0)
        return abs_pos;

    int best = -1;
    for (int i = 0; i < map->frames && i < SLOW_SCRATCH_MAX_CTX_FRAMES; i++) {
        const double a = map->exact_start[i];
        const double b = a + map->exact_len[i];
        if (abs_pos >= a && abs_pos < b) {
            best = i;
            break;
        }
    }

    if (best < 0) {
        if (abs_pos < map->exact_start[0])
            return 0.0;
        best = map->frames - 1;
        if (best >= SLOW_SCRATCH_MAX_CTX_FRAMES)
            best = SLOW_SCRATCH_MAX_CTX_FRAMES - 1;
    }

    const double exact_len = (map->exact_len[best] > 0.0) ? map->exact_len[best] : 1.0;
    double phase = (abs_pos - map->exact_start[best]) / exact_len;
    phase = slow_motion_clampd(phase, 0.0, 0.999999);

    int got = map->frame_len[best];
    if (got <= 0)
        got = 1;

    double rel = (double)map->frame_off[best] + phase * (double)got;
    if (rel < 0.0)
        rel = 0.0;
    else if (rel > (double)(ctx_samples - 1))
        rel = (double)(ctx_samples - 1);
    return rel;
}

static int slow_motion_render_scratch_head_s16(uint8_t *dst,
                                               int dst_samples,
                                               const uint8_t *ctx,
                                               int ctx_samples,
                                               double ctx_abs_start,
                                               const slow_scratch_ctx_map_t *ctx_map,
                                               sample_b_t *posdata,
                                               double target_vel,
                                               double sync_start_pos,
                                               int frame_bytes,
                                               int pred_len,
                                               int edge_transition,
                                               int *step_max_out,
                                               int *step_avg_out,
                                               double *pos_start_out,
                                               double *pos_end_out,
                                               double *vel_start_out,
                                               double *vel_end_out)
{
    if (step_max_out != NULL)
        *step_max_out = 0;
    if (step_avg_out != NULL)
        *step_avg_out = 0;
    if (pos_start_out != NULL)
        *pos_start_out = (posdata != NULL) ? posdata->scratch_pos : 0.0;
    if (pos_end_out != NULL)
        *pos_end_out = (posdata != NULL) ? posdata->scratch_pos : 0.0;
    if (vel_start_out != NULL)
        *vel_start_out = (posdata != NULL) ? posdata->scratch_vel : 0.0;
    if (vel_end_out != NULL)
        *vel_end_out = (posdata != NULL) ? posdata->scratch_vel : 0.0;

    if (dst == NULL || ctx == NULL || posdata == NULL || dst_samples <= 0 ||
        ctx_samples <= 0 || frame_bytes <= 0 || (frame_bytes & 1))
        return 0;

    const int words = frame_bytes / 2;
    const int16_t *in = (const int16_t*)ctx;
    int16_t *out = (int16_t*)dst;
    const int max_index = ctx_samples - 1;

    double head = posdata->scratch_pos;
    double vel = posdata->scratch_vel;
    const double start_head = head;
    const double start_vel = vel;

    const int turn_time = edge_transition ? 1536 : 384;
    const double vel_alpha_edge = 1.0 / (double)turn_time;
    const double vel_alpha_normal = 1.0 / 256.0;

    const double block_sync_error = sync_start_pos - head;
    (void)block_sync_error;

    int step_peak = 0;
    int64_t step_sum = 0;
    int step_n = 0;
    int16_t prev_words[8];
    int prev_valid = 0;
    const int local_words = (words > 8) ? 8 : words;

    for (int i = 0; i < dst_samples; i++) {
        const double wanted = target_vel;
        const double a = (posdata->scratch_ramp_left > 0) ? vel_alpha_edge : vel_alpha_normal;

        vel += (wanted - vel) * a;
        if (posdata->scratch_ramp_left > 0)
            posdata->scratch_ramp_left--;

        double rel = (ctx_map != NULL && ctx_map->valid)
            ? slow_motion_ctx_exact_to_actual_rel(ctx_map, head, ctx_samples)
            : (head - ctx_abs_start);
        if (rel < 0.0)
            rel = 0.0;
        else if (rel > (double)max_index)
            rel = (double)max_index;

        int idx = (int)floor(rel);
        double frac = rel - (double)idx;
        if (idx < 0) {
            idx = 0;
            frac = 0.0;
        } else if (idx > max_index) {
            idx = max_index;
            frac = 0.0;
        }

        int i0 = idx - 1;
        int i1 = idx;
        int i2 = idx + 1;
        int i3 = idx + 2;
        if (i0 < 0) i0 = 0;
        if (i2 > max_index) i2 = max_index;
        if (i3 > max_index) i3 = max_index;

        const int b0 = i0 * words;
        const int b1 = i1 * words;
        const int b2 = i2 * words;
        const int b3 = i3 * words;
        const int bo = i * words;

        int frame_step = 0;
        for (int c = 0; c < words; c++) {
            int16_t s = slow_motion_cubic_interp_s16(
                in[b0 + c], in[b1 + c], in[b2 + c], in[b3 + c], frac);
            out[bo + c] = s;
            if (c < local_words) {
                if (prev_valid) {
                    int d = (int)s - (int)prev_words[c];
                    d = (d < 0) ? -d : d;
                    if (d > frame_step)
                        frame_step = d;
                }
                prev_words[c] = s;
            }
        }
        if (prev_valid) {
            if (frame_step > step_peak)
                step_peak = frame_step;
            step_sum += frame_step;
            step_n++;
        }
        prev_valid = 1;

        head += vel;
    }

    posdata->scratch_pos = head;
    posdata->scratch_vel = vel;
    posdata->scratch_target_vel = target_vel;
    posdata->scratch_last_sync_pos = sync_start_pos + target_vel * (double)dst_samples;
    posdata->scratch_last_sync_error = posdata->scratch_last_sync_pos - head;

    if (step_max_out != NULL)
        *step_max_out = step_peak;
    if (step_avg_out != NULL)
        *step_avg_out = (step_n > 0) ? (int)(step_sum / step_n) : 0;
    if (pos_start_out != NULL)
        *pos_start_out = start_head;
    if (pos_end_out != NULL)
        *pos_end_out = head;
    if (vel_start_out != NULL)
        *vel_start_out = start_vel;
    if (vel_end_out != NULL)
        *vel_end_out = vel;

    return dst_samples;
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
    (void)sampled_down;

    if (el == NULL || p == NULL || audio_buf == NULL || downsample_buffer == NULL ||
        posdata == NULL)
        return 0;

    const int frame_bytes = el->audio_bps;
    const int pred_len = el->audio_rate / el->video_fps;
    audio_edge_t *edge = p->audio_edge;

    if (frame_bytes <= 0 || pred_len <= 0)
        return 0;

    int cur_dir = (posdata->speed > 0) ? 1 : ((posdata->speed < 0) ? -1 : 0);

    int pending_edge = AUDIO_EDGE_NONE;
    int last_dir = 0;
    int direction_flipped = 0;

    if (edge != NULL) {
        last_dir = atomic_load_int(&edge->last_direction);
        pending_edge = atomic_load_int(&edge->pending_edge);
        direction_flipped = (last_dir != 0 && cur_dir != 0 && cur_dir != last_dir);
    }

    if (pending_edge == AUDIO_EDGE_SILENCE && cur_dir != 0)
        pending_edge = AUDIO_EDGE_JUMP;

    if (cur_dir == 0 || pending_edge == AUDIO_EDGE_SILENCE) {
        veejay_memset(audio_buf, 0, pred_len * frame_bytes);
        vj_audio_declick_apply(p, audio_buf, pred_len, frame_bytes,
                               AUDIO_PATH_SILENCE, 0, 0,
                               pending_edge, direction_flipped);
        vj_perform_clear_audio_edges(info, edge, 0);
        slow_motion_clear_scratch_head(posdata);
        posdata->prev_n_samples = pred_len;
        return pred_len;
    }

    int slice_count = posdata->max_sfd;
    slice_count = (slice_count <= 1) ? 2 : slice_count;
    if (slice_count > MAX_SPEED_AV)
        slice_count = MAX_SPEED_AV;

    int cur_slice = posdata->cur_sfd;
    cur_slice = (cur_slice < 0) ? 0 : cur_slice;
    cur_slice = (cur_slice >= slice_count) ? (slice_count - 1) : cur_slice;

    const int edge_requested = (pending_edge != AUDIO_EDGE_NONE || direction_flipped);
    const int hard_edge = (pending_edge != AUDIO_EDGE_NONE &&
                           pending_edge != AUDIO_EDGE_DIRECTION);
    const int edge_transition = (pending_edge == AUDIO_EDGE_DIRECTION || direction_flipped);

    int speed_mag = abs(posdata->speed);
    if (speed_mag < 1)
        speed_mag = 1;
    if (speed_mag > MAX_SPEED_AV)
        speed_mag = MAX_SPEED_AV;

    const int target_frame_samples = slow_motion_frame_len_exact(el, target_frame, pred_len);
    const double nominal_target_vel = ((double)cur_dir * (double)speed_mag * (double)target_frame_samples) /
                                      ((double)slice_count * (double)pred_len);
    const double sync_pos = slow_motion_sync_abs_pos(el, posdata, target_frame, pred_len,
                                                     slice_count, cur_slice, cur_dir);

    double target_vel = nominal_target_vel;
    double sync_bias = 0.0;
    double sync_bias_raw = 0.0;

    int reset_head = 0;
    int init_head = 0;
    if (!posdata->scratch_initialized) {
        reset_head = 1;
        init_head = 1;
    }
    if (hard_edge)
        reset_head = 1;


    if (reset_head) {
        posdata->scratch_initialized = 1;
        posdata->scratch_pos = sync_pos;
        posdata->scratch_vel = target_vel;
        posdata->scratch_target_vel = target_vel;
        posdata->scratch_last_dir = cur_dir;
        posdata->scratch_last_sfd = slice_count;
        posdata->scratch_ramp_left = 0;
        posdata->scratch_last_reset = 1;
        posdata->scratch_sync_bias = 0.0;
        posdata->scratch_sync_hold_blocks = 0;
        posdata->scratch_stable_blocks = 0;
        slow_motion_clear_turn_history(posdata);
    } else {
        posdata->scratch_last_reset = 0;
        if (edge_transition || posdata->scratch_last_dir != cur_dir ||
            posdata->scratch_last_sfd != slice_count) {
            posdata->scratch_ramp_left = pred_len;
            if (posdata->scratch_ramp_left < 512)
                posdata->scratch_ramp_left = 512;
            if (posdata->scratch_ramp_left > 2048)
                posdata->scratch_ramp_left = 2048;

            if (edge_transition) {
                posdata->scratch_sync_hold_blocks = (slice_count >= 8) ? 12 : 8;
                posdata->scratch_stable_blocks = 0;
                posdata->scratch_sync_bias = 0.0;
            }
        }
        posdata->scratch_target_vel = target_vel;
        posdata->scratch_last_dir = cur_dir;
        posdata->scratch_last_sfd = slice_count;
    }

    if (!reset_head && !hard_edge) {
        const double sync_err_now = sync_pos - posdata->scratch_pos;
        const double abs_nominal = fabs(nominal_target_vel);
        const int hold_active = (edge_transition || posdata->scratch_sync_hold_blocks > 0);
        const int stable_needed = (slice_count >= 8) ? 10 : 6;

        if (hold_active) {
            if (!edge_transition && posdata->scratch_sync_hold_blocks > 0)
                posdata->scratch_sync_hold_blocks--;

            posdata->scratch_stable_blocks = 0;
            posdata->scratch_sync_bias *= 0.50;
            if (fabs(posdata->scratch_sync_bias) < 0.000001)
                posdata->scratch_sync_bias = 0.0;

            sync_bias = 0.0;
            sync_bias_raw = 0.0;
            target_vel = nominal_target_vel;
        } else {
            if (posdata->scratch_stable_blocks < 1000000)
                posdata->scratch_stable_blocks++;

            if (posdata->scratch_stable_blocks < stable_needed) {
                posdata->scratch_sync_bias *= 0.75;
                if (fabs(posdata->scratch_sync_bias) < 0.000001)
                    posdata->scratch_sync_bias = 0.0;

                sync_bias = 0.0;
                sync_bias_raw = 0.0;
                target_vel = nominal_target_vel;
            } else {
                const double abs_err = fabs(sync_err_now);
                double leash_den = (double)pred_len * 640.0;
                double leash_smooth = 0.03125;
                double max_bias = abs_nominal * 0.006;

                if (abs_err > (double)(pred_len * 2)) {
                    leash_den = (double)pred_len * 384.0;
                    leash_smooth = 0.0625;
                    max_bias = abs_nominal * 0.014;
                    if (max_bias < 0.00035)
                        max_bias = 0.00035;
                    if (max_bias > 0.0070)
                        max_bias = 0.0070;
                } else if (abs_err > (double)((pred_len * 3) / 4)) {
                    leash_den = (double)pred_len * 512.0;
                    leash_smooth = 0.046875;
                    max_bias = abs_nominal * 0.010;
                    if (max_bias < 0.00025)
                        max_bias = 0.00025;
                    if (max_bias > 0.0050)
                        max_bias = 0.0050;
                } else {
                    if (max_bias < 0.00015)
                        max_bias = 0.00015;
                    if (max_bias > 0.0030)
                        max_bias = 0.0030;
                }

                sync_bias_raw = sync_err_now / leash_den;
                sync_bias_raw = slow_motion_clampd(sync_bias_raw, -max_bias, max_bias);

                posdata->scratch_sync_bias +=
                    (sync_bias_raw - posdata->scratch_sync_bias) * leash_smooth;
                sync_bias = posdata->scratch_sync_bias;
                target_vel = nominal_target_vel + sync_bias;
            }
        }
    } else {
        posdata->scratch_sync_bias = 0.0;
        posdata->scratch_sync_hold_blocks = 0;
        posdata->scratch_stable_blocks = 0;
        sync_bias = 0.0;
        sync_bias_raw = 0.0;
        target_vel = nominal_target_vel;
    }

    const double predicted_end = posdata->scratch_pos +
        (posdata->scratch_vel * (double)pred_len) +
        (target_vel * (double)pred_len);

    double ctx_abs_start = 0.0;
    int ctx_samples = 0;
    long long ctx_first = 0;
    long long ctx_last = 0;
    slow_scratch_ctx_map_t ctx_map;
    veejay_memset(&ctx_map, 0, sizeof(ctx_map));

    int got_ctx = slow_motion_fetch_scratch_context(info, el, posdata,
                                                    downsample_buffer,
                                                    &ctx_map,
                                                    pred_len, frame_bytes,
                                                    posdata->speed,
                                                    posdata->scratch_pos,
                                                    predicted_end,
                                                    &ctx_abs_start,
                                                    &ctx_samples,
                                                    &ctx_first,
                                                    &ctx_last);

    if (got_ctx <= 0) {
        veejay_memset(audio_buf, 0, pred_len * frame_bytes);
        vj_audio_declick_apply(p, audio_buf, pred_len, frame_bytes,
                               AUDIO_PATH_SILENCE, 0, 0,
                               pending_edge, direction_flipped);
        vj_perform_clear_audio_edges(info, edge, cur_dir);
        posdata->prev_n_samples = pred_len;
        return pred_len;
    }

    double pos0 = posdata->scratch_pos;
    double pos1 = posdata->scratch_pos;
    double vel0 = posdata->scratch_vel;
    double vel1 = posdata->scratch_vel;
    int step_max = 0;
    int step_avg = 0;

    int copied = slow_motion_render_scratch_head_s16(audio_buf,
                                                     pred_len,
                                                     downsample_buffer,
                                                     ctx_samples,
                                                     ctx_abs_start,
                                                     &ctx_map,
                                                     posdata,
                                                     target_vel,
                                                     sync_pos,
                                                     frame_bytes,
                                                     pred_len,
                                                     edge_transition,
                                                     &step_max,
                                                     &step_avg,
                                                     &pos0,
                                                     &pos1,
                                                     &vel0,
                                                     &vel1);
    if (copied <= 0) {
        veejay_memset(audio_buf, 0, pred_len * frame_bytes);
        copied = pred_len;
    }

    int edge_delta = -1;
    if (posdata->audio_diag_valid && posdata->audio_diag_frame_bytes == frame_bytes)
        edge_delta = vj_audio_frame_delta_s16(audio_buf,
                                              posdata->audio_diag_last_frame,
                                              frame_bytes);

    const double sync_end = sync_pos + (target_vel * (double)copied);
    const double sync_err0 = sync_pos - pos0;
    const double sync_err1 = sync_end - pos1;
    const int peak = vj_audio_peak_s16(audio_buf, copied, frame_bytes);

    /*if (edge_requested || reset_head || hard_edge ||
        fabs(sync_err0) > (double)(pred_len * 4) ||
        fabs(sync_err1) > (double)(pred_len * 4) ||
        edge_delta >= 1500 || step_max >= 3000 || peak >= 32760) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-DIAG] scratch-head stage=59 edge=%s(%d) flip=%d init=%d reset=%d hard=%d dir=%d speed_mag=%d sfd=%d audio_slice=%d req=%lld sync=%.3f pos0=%.3f pos1=%.3f vel0=%.6f vel1=%.6f target=%.6f nominal=%.6f bias=%.6f raw=%.6f hold=%d stable=%d err0=%.3f err1=%.3f ramp_left=%d ctx=%lld..%lld samples=%d delta=%d peak=%d step_max=%d step_avg=%d mode=persistent-readhead pll=stable-window-leash map=actual-prefix",
                   vj_audio_edge_name(pending_edge), pending_edge, direction_flipped,
                   init_head, reset_head, hard_edge, cur_dir, speed_mag, slice_count, cur_slice,
                   target_frame, sync_pos, pos0, pos1, vel0, vel1, target_vel,
                   nominal_target_vel, sync_bias, sync_bias_raw,
                   posdata->scratch_sync_hold_blocks, posdata->scratch_stable_blocks,
                   sync_err0, sync_err1, posdata->scratch_ramp_left,
                   ctx_first, ctx_last, ctx_samples, edge_delta, peak,
                   step_max, step_avg);
    } */

    if (hard_edge) {
        vj_audio_declick_apply(p, audio_buf, copied, frame_bytes,
                               AUDIO_PATH_SLOW, posdata->speed, cur_dir,
                               pending_edge, direction_flipped);
    } else {
        vj_audio_declick_observe(p, audio_buf, copied, frame_bytes,
                                 AUDIO_PATH_SLOW, posdata->speed, cur_dir);
        /*if (edge_requested || edge_delta >= 1500 || step_max >= 3000 || peak >= 32760) {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[AUDIO-DIAG] declick-observe stage=59 reason=persistent-readhead edge_delta=%d step_max=%d sync_err=%.3f",
                       edge_delta, step_max, sync_err1);
        } */
    }

    if (posdata->audio_diag_valid && posdata->audio_diag_frame_bytes == frame_bytes) {
        vj_audio_copy_last_frame(posdata->audio_diag_prev_frame,
                                 (int)sizeof(posdata->audio_diag_prev_frame),
                                 posdata->audio_diag_last_frame, 1, frame_bytes);
    } else {
        veejay_memset(posdata->audio_diag_prev_frame, 0,
                      sizeof(posdata->audio_diag_prev_frame));
    }

    vj_audio_copy_last_frame(posdata->audio_diag_last_frame,
                             (int)sizeof(posdata->audio_diag_last_frame),
                             audio_buf, copied, frame_bytes);
    posdata->audio_diag_valid = 1;
    posdata->audio_diag_frame_bytes = frame_bytes;

    slow_motion_update_turn_history(posdata, audio_buf, copied, frame_bytes);

    posdata->audio_last_stretched_samples = pred_len * slice_count;
    posdata->audio_total_samples = ctx_samples;
    posdata->audio_src_offset = (int)((ctx_map.valid)
        ? slow_motion_ctx_exact_to_actual_rel(&ctx_map, posdata->scratch_pos, ctx_samples)
        : (posdata->scratch_pos - ctx_abs_start));
    posdata->last_resampled_frame = target_frame;
    posdata->last_resampled_dir = cur_dir;
    posdata->consumed_samples = cur_slice * pred_len;

    if (edge != NULL)
        edge->ticks_since_last_flip++;

    vj_perform_clear_audio_edges(info, edge, cur_dir);

    posdata->direction_changed = 0;
    posdata->prev_n_samples = copied;
    return copied;
}

int vj_perform_fill_audio_buffers(
    veejay_t *info,
    editlist *el,
    uint8_t *audio_buf,
    performer_t *p,
    int *sampled_down,
    long long target_frame
) {
    video_playback_setup *settings = info->settings;
    uint8_t *temporary_buffer = p->audio_render_buffer;
    uint8_t *downsample_buffer = p->down_sample_buffer;
    performer_global_t *g = (performer_global_t*) info->performer;

    sample_b_t *sample_ptr = (p == g->A) ? &(g->A->sample_a) : &(g->A->sample_b);

    sample_ptr->audio_last_stretched_samples = settings->audio_last_stretched_samples;
    sample_ptr->direction_changed = atomic_load_int(&settings->audio_direction_changed);
    sample_ptr->max_sfd = atomic_load_int(&settings->audio_slice_len);
    sample_ptr->cur_sfd = atomic_load_int(&settings->audio_slice);
    sample_ptr->speed = settings->current_playback_speed;

    int num_samples = (el->audio_rate / el->video_fps);
    int result = 0;

    if (sample_ptr->max_sfd > 1) {
        long long slow_audio_frame = atomic_load_long_long(&settings->current_frame_num);
        if (slow_audio_frame < 0)
            slow_audio_frame = target_frame;


        result = perform_slow_motion(info, el, p, audio_buf, downsample_buffer,
                                     sampled_down, slow_audio_frame, sample_ptr);

        settings->audio_last_stretched_samples = sample_ptr->audio_last_stretched_samples;
        atomic_store_int(&settings->audio_direction_changed, 0);

        sample_ptr->prev_n_samples = result;
        return result;
    }

    result = perform_normal_playback(info, el, p, audio_buf, temporary_buffer,
                                     num_samples, el->audio_bps, target_frame,
                                     sample_ptr);

    atomic_store_int(&settings->audio_direction_changed, 0);
    atomic_store_int(&settings->audio_slice, 0);

    sample_ptr->direction_changed = 0;
    sample_ptr->prev_n_samples = result;

    return result;
}

#endif



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
    if(speed == 0) {
        int res = vj_el_get_video_frame(el, (long)nframe, dst->data);

        if(res)
            dst->ssm = 0;

        return res;
    }

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

static void vj_perform_global_chain_sync(veejay_t *info, global_chain_t *g_chain, int id, int pm) {
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

static editlist *vj_perform_record_audio_editlist(veejay_t *info)
{
    editlist *el = NULL;

    if(!info)
        return NULL;

    if(info->uc && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
        el = sample_get_editlist(info->uc->sample_id);

    if(!el)
        el = info->current_edit_list;
    if(!el)
        el = info->edit_list;

    return el;
}

static int vj_perform_record_audio_expected_frames(editlist *el)
{
    if(!el || el->audio_rate <= 0 || el->video_fps <= 0.0)
        return 0;

    return (int)ceil((double)el->audio_rate / (double)el->video_fps);
}

#ifdef HAVE_JACK
static int vj_perform_record_audio_beat_active(video_playback_setup *settings)
{
    if(!settings)
        return 0;

    return vj_audio_beat_is_enabled(&settings->audio_beat) &&
           vj_audio_beat_is_running(&settings->audio_beat) &&
           vj_audio_beat_is_open(&settings->audio_beat);
}

static int vj_perform_record_audio_from_beat(
    veejay_t *info,
    performer_t *p,
    editlist *el
) {
    video_playback_setup *settings;
    int wanted_frames;
    int frame_bytes;
    int copied_frames;

    if(!info || !info->settings || !p || !p->audio_rec_buffer || !el)
        return 0;

    settings = info->settings;

    wanted_frames = vj_perform_record_audio_expected_frames(el);
    if(wanted_frames <= 0)
        return 0;

    frame_bytes = el->audio_bps;
    if(frame_bytes <= 0)
        return 0;

    copied_frames = vj_audio_beat_copy_record_audio(
        &settings->audio_beat,
        p->audio_rec_buffer,
        wanted_frames,
        frame_bytes,
        el->audio_chans,
        el->audio_rate
    );

    if(copied_frames < 0)
        copied_frames = 0;

    if(copied_frames > wanted_frames)
        copied_frames = wanted_frames;

    /*
     * Keep recorder cadence stable: one video frame gets one expected
     * audio slice. Underrun becomes silence, not a sudden fallback to
     * editlist audio mid-recording.
     */
    if(copied_frames < wanted_frames) {
        veejay_memset(
            p->audio_rec_buffer + ((size_t)copied_frames * (size_t)frame_bytes),
            0,
            (size_t)(wanted_frames - copied_frames) * (size_t)frame_bytes
        );
    }

    return wanted_frames;
}
#endif

static int vj_perform_record_audio_from_original(
    veejay_t *info,
    performer_t *p
) {
#ifdef HAVE_JACK
    video_playback_setup *settings;
    editlist *el;
    long long target_frame;
    int wanted_frames;
    int frame_bytes;
    int sample_id;
    int got;

    if(!info || !info->settings || !p || !p->audio_rec_buffer)
        return 0;

    settings = info->settings;
    el = vj_perform_record_audio_editlist(info);
    if(!el || !el->has_audio)
        return 0;

    wanted_frames = vj_perform_record_audio_expected_frames(el);
    frame_bytes = el->audio_bps;
    if(wanted_frames <= 0 || frame_bytes <= 0)
        return 0;

    target_frame = atomic_load_long_long(&settings->current_frame_num);
    sample_id = (info->uc ? info->uc->sample_id : 0);

    got = vj_perform_queue_audio_frame_impl(info,
                                            (void*)p,
                                            p->audio_rec_buffer,
                                            settings->current_playback_speed,
                                            target_frame,
                                            sample_id,
                                            &rec_audio_sample_);

    if(got < 0)
        got = 0;
    if(got > wanted_frames)
        got = wanted_frames;

    if(got < wanted_frames) {
        veejay_memset(p->audio_rec_buffer + ((size_t)got * (size_t)frame_bytes),
                      0,
                      (size_t)(wanted_frames - got) * (size_t)frame_bytes);
        got = wanted_frames;
    }

    return got;
#else
    (void)info;
    (void)p;
    return 0;
#endif
}

static int vj_perform_record_audio_frame(
    veejay_t *info,
    performer_t *p
) {
    video_playback_setup *settings;
    editlist *el;
    int source = VJ_RECORD_AUDIO_SOURCE_AUTO;

    if(!info || !info->settings || !p || !p->audio_rec_buffer)
        return 0;

    settings = info->settings;
    el = vj_perform_record_audio_editlist(info);

    source = atomic_load_int(&settings->record_audio_source);

#ifdef HAVE_JACK
    if(source == VJ_RECORD_AUDIO_SOURCE_BEAT_JACK) {
        if(!vj_perform_record_audio_beat_active(settings) &&
        vj_audio_beat_is_running(&settings->audio_beat))
        {
            vj_audio_beat_set_action(&settings->audio_beat, VJ_AUDIO_BEAT_ACTION_NONE);
            vj_audio_beat_enable(&settings->audio_beat);
        }

        if(vj_perform_record_audio_beat_active(settings))
            return vj_perform_record_audio_from_beat(info, p, el);

        if(el) {
            int wanted_frames = vj_perform_record_audio_expected_frames(el);
            int frame_bytes = el->audio_bps;

            if(wanted_frames > 0 && frame_bytes > 0) {
                veejay_memset(
                    p->audio_rec_buffer,
                    0,
                    (size_t)wanted_frames * (size_t)frame_bytes
                );
                return wanted_frames;
            }
        }

        return 0;
    }

    if(source == VJ_RECORD_AUDIO_SOURCE_AUTO &&
       vj_perform_record_audio_beat_active(settings))
    {
        return vj_perform_record_audio_from_beat(info, p, el);
    }
#endif

    return vj_perform_record_audio_from_original(info, p);
}

void vj_perform_record_audio_source_reset(veejay_t *info)
{
    rec_audio_sample_ = 0;

    if(!info || !info->settings)
        return;

    video_playback_setup *settings = info->settings;

    atomic_store_int(&settings->audio_slice, 0);
    atomic_store_int(&settings->audio_flush_request, 1);
    settings->audio_last_stretched_samples = 0;

    vj_perform_initiate_edge_change(info, AUDIO_EDGE_RESET, 0, 0);
}

static int vj_perform_render_sample_frame(
    veejay_t *info,
    performer_t *p,
    uint8_t *frame[4],
    int sample,
    int type
) {
    int audio_len = 0;

    if(type == 0 && info->audio == AUDIO_PLAY) {
        audio_len = vj_perform_record_audio_frame(info, p);
    }

    return sample_record_frame(
        sample,
        frame,
        p->audio_rec_buffer,
        audio_len,
        info->pixel_format
    );
}

static int vj_perform_render_offline_tag_frame(veejay_t *info)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g ? g->A : NULL;
    int audio_len = 0;

    if(vj_tag_get_active( info->settings->offline_tag_id ) == 0 ) {
        vj_tag_enable( info->settings->offline_tag_id );
    }

    vj_tag_get_frame( info->settings->offline_tag_id, g->offline_frame, NULL );

    if(p && info->audio == AUDIO_PLAY)
        audio_len = vj_perform_record_audio_frame(info, p);

    return vj_tag_record_frame(info->settings->offline_tag_id,
                               g->offline_frame->data,
                               (audio_len > 0 && p) ? p->audio_rec_buffer : NULL,
                               audio_len,
                               info->pixel_format);
}
    
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4])
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g ? g->A : NULL;
    int audio_len = 0;

    if(p && info->audio == AUDIO_PLAY)
        audio_len = vj_perform_record_audio_frame(info, p);

    return vj_tag_record_frame(info->uc->sample_id,
                               frame,
                               (audio_len > 0 && p) ? p->audio_rec_buffer : NULL,
                               audio_len,
                               info->pixel_format);
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

static void vj_perform_tag_fill_buffer(veejay_t *info, performer_t *p, VJFrame *dst, int sample_id)
{
    int error = 1;
    performer_global_t *g = (performer_global_t*) info->performer;
    int active = p->pvar_.active;
    video_playback_setup *settings = info->settings;

    if(info->settings->feedback && info->settings->feedback_stage > 1) {
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

    if(settings && settings->current_playback_speed == 0)
        return;

    if(!active)
    {
        vj_tag_enable(sample_id);
    }
    else
    {
        if(vj_tag_get_frame(sample_id, dst, NULL))
            error = 0;
    }

    if(error == 1)
        dummy_apply(dst, VJ_EFFECT_COLOR_BLACK);
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

#ifdef HAVE_JACK
static double vj_perform_runtime_audio_rate(veejay_t *info, editlist *el);
static int vj_perform_runtime_sfd(veejay_t *info);
static double vj_perform_runtime_effective_audio_rate(veejay_t *info, editlist *el);
static void vj_audio_pad_exact_tail(uint8_t *dst, int produced, int expected, int frame_bytes);
static int vj_audio_retime_slow_cubic_s16(uint8_t *dst, int dst_samples, const uint8_t *src, int src_samples, int frame_bytes, double rate);
static int vj_perform_retime_audio_chunk(veejay_t *info, performer_t *p, editlist *el, uint8_t *dst, int dst_samples, const uint8_t *src, int src_samples, int frame_bytes);
static int vj_perform_runtime_slow_audio_chunk(veejay_t *info, performer_t *p, editlist *el, uint8_t *dst, int dst_samples, long long target_frame, int frame_bytes, double rate);
int vj_perform_queue_audio_frame(veejay_t *info, void *ptr, uint8_t *a_buf, int speed, long long target_frame,int sample_id);

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

    (void)fade_in;

    if (client_frames_to_write <= 0 || audio_payload_chunk == NULL)
        return 0;

    editlist *el = (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
        ? sample_get_editlist(info->uc->sample_id)
        : info->edit_list;
    if (el == NULL)
        el = info->current_edit_list;

    const double rate = vj_perform_runtime_audio_rate(info, el);
    const double effective_rate = vj_perform_runtime_effective_audio_rate(info, el);
    int speed = settings->current_playback_speed;

    if (rate > 0.9995 && rate < 1.0005) {
        return vj_perform_queue_audio_frame(info,
                                            (void*)p,
                                            audio_payload_chunk,
                                            speed,
                                            target_frame,
                                            info->uc->sample_id);
    }

    int frame_bytes = (el != NULL) ? el->audio_bps : 0;

    if (effective_rate < 0.9995 && frame_bytes > 0 && !(frame_bytes & 1)) {
        return vj_perform_runtime_slow_audio_chunk(info,
                                                   p,
                                                   el,
                                                   audio_payload_chunk,
                                                   client_frames_to_write,
                                                   target_frame,
                                                   frame_bytes,
                                                   effective_rate);
    }

    int num_samples = vj_perform_queue_audio_frame(info,
                                                   (void*)p,
                                                   p->top_audio_buffer,
                                                   speed,
                                                   target_frame,
                                                   info->uc->sample_id);

    if (num_samples > 0)
        vj_audio_consume_chain(info, p->top_audio_buffer, num_samples);

    return vj_perform_retime_audio_chunk(info,
                                         p,
                                         el,
                                         audio_payload_chunk,
                                         client_frames_to_write,
                                         p->top_audio_buffer,
                                         num_samples,
                                         frame_bytes);
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


static double vj_perform_runtime_audio_rate(veejay_t *info, editlist *el)
{
    if (info == NULL || info->settings == NULL || el == NULL || el->video_fps <= 0.0)
        return 1.0;

    video_playback_setup *settings = info->settings;
    double rate = settings->runtime_playback_rate;

    if (rate <= 0.0) {
        double fps = settings->output_fps;
        if (fps <= 0.0)
            fps = el->video_fps;
        rate = fps / (double)el->video_fps;
    }

    if (rate < 0.01)
        rate = 0.01;
    else if (rate > 16.0)
        rate = 16.0;

    return rate;
}

static int vj_perform_runtime_sfd(veejay_t *info)
{
    if (info == NULL || info->settings == NULL)
        return 1;

    video_playback_setup *settings = info->settings;
    int sfd = atomic_load_int(&settings->audio_slice_len);

    if (sfd < 1)
        sfd = settings->sfd;

    if (sfd < 1) {
        if (info->uc != NULL && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
            sfd = sample_get_framedup(info->uc->sample_id);
        else
            sfd = info->sfd;
    }

    if (sfd < 1)
        sfd = 1;
    else if (sfd > MAX_SPEED_AV)
        sfd = MAX_SPEED_AV;

    return sfd;
}

static double vj_perform_runtime_effective_audio_rate(veejay_t *info, editlist *el)
{
    double rate = vj_perform_runtime_audio_rate(info, el);
    const int sfd = vj_perform_runtime_sfd(info);

    if (sfd > 1)
        rate /= (double)sfd;

    if (rate < 0.01)
        rate = 0.01;
    else if (rate > 16.0)
        rate = 16.0;

    return rate;
}


static int vj_perform_runtime_slow_audio_chunk(veejay_t *info,
                                               performer_t *p,
                                               editlist *el,
                                               uint8_t *dst,
                                               int dst_samples,
                                               long long target_frame,
                                               int frame_bytes,
                                               double rate)
{
    if (info == NULL || p == NULL || el == NULL || dst == NULL ||
        dst_samples <= 0 || frame_bytes <= 0)
        return 0;

    if ((frame_bytes & 1) || p->down_sample_buffer == NULL) {
        int pred_len = (el->audio_rate > 0 && el->video_fps > 0.0)
            ? (int)((double)el->audio_rate / (double)el->video_fps)
            : dst_samples;
        if (pred_len < 1)
            pred_len = 1;
        int got = vj_perform_queue_audio_frame(info, (void*)p, p->top_audio_buffer,
                                               info->settings->current_playback_speed,
                                               target_frame, info->uc->sample_id);
        return vj_audio_retime_slow_cubic_s16(dst, dst_samples, p->top_audio_buffer,
                                              got > 0 ? got : pred_len,
                                              frame_bytes, rate);
    }

    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*)info->performer;
    sample_b_t *posdata = (g != NULL && g->A != NULL && p == g->A)
        ? &(g->A->sample_a)
        : ((g != NULL && g->A != NULL) ? &(g->A->sample_b) : &(p->sample_b));
    audio_edge_t *edge = p->audio_edge;

    int pred_len = (el->audio_rate > 0 && el->video_fps > 0.0)
        ? (int)((double)el->audio_rate / (double)el->video_fps)
        : dst_samples;
    if (pred_len < 1)
        pred_len = 1;

    int speed = settings->current_playback_speed;
    int cur_dir = (speed > 0) ? 1 : ((speed < 0) ? -1 : 0);
    int speed_mag = abs(speed);
    if (speed_mag < 1)
        speed_mag = 1;
    if (speed_mag > MAX_SPEED_AV)
        speed_mag = MAX_SPEED_AV;

    int pending_edge = AUDIO_EDGE_NONE;
    int last_dir = 0;
    int direction_flipped = 0;
    if (edge != NULL) {
        pending_edge = atomic_load_int(&edge->pending_edge);
        last_dir = atomic_load_int(&edge->last_direction);
        direction_flipped = (last_dir != 0 && cur_dir != 0 && cur_dir != last_dir);
    }

    if (pending_edge == AUDIO_EDGE_SILENCE && cur_dir != 0)
        pending_edge = AUDIO_EDGE_JUMP;

    if (cur_dir == 0 || pending_edge == AUDIO_EDGE_SILENCE ||
        !el->has_audio || target_frame == -1) {
        veejay_memset(dst, 0, (size_t)dst_samples * (size_t)frame_bytes);
        vj_audio_declick_apply(p, dst, dst_samples, frame_bytes,
                               AUDIO_PATH_SILENCE, 0, 0,
                               pending_edge, direction_flipped);
        vj_perform_clear_audio_edges(info, edge, 0);
        slow_motion_clear_scratch_head(posdata);
        posdata->prev_n_samples = dst_samples;
        return dst_samples;
    }

    int target_frame_samples = slow_motion_frame_len_exact(el, target_frame, pred_len);
    if (target_frame_samples < 1)
        target_frame_samples = pred_len;

    int slice_count = vj_perform_runtime_sfd(info);
    int cur_slice = atomic_load_int(&settings->audio_slice);
    cur_slice = (cur_slice < 0) ? 0 : cur_slice;
    cur_slice = (cur_slice >= slice_count) ? (slice_count - 1) : cur_slice;
    const int last_slice = posdata->cur_sfd;
    const int last_slice_count = posdata->scratch_last_sfd;

    double sync_pos = slow_motion_sync_abs_pos(el, posdata, target_frame, pred_len,
                                               slice_count, cur_slice, cur_dir);

    double target_vel = (double)cur_dir * (double)speed_mag * rate;

    const int hard_edge = (pending_edge != AUDIO_EDGE_NONE &&
                           pending_edge != AUDIO_EDGE_DIRECTION);
    const int edge_transition = (pending_edge == AUDIO_EDGE_DIRECTION || direction_flipped);
    const int same_target_frame = (posdata->scratch_initialized &&
                                   posdata->last_resampled_frame == target_frame &&
                                   last_slice == cur_slice &&
                                   last_slice_count == slice_count &&
                                   !hard_edge && !edge_transition);
    if (same_target_frame)
        sync_pos = posdata->scratch_pos;
    const int reset_head = (!posdata->scratch_initialized || hard_edge);

    if (reset_head) {
        posdata->scratch_initialized = 1;
        posdata->scratch_pos = sync_pos;
        posdata->scratch_vel = target_vel;
        posdata->scratch_target_vel = target_vel;
        posdata->scratch_last_dir = cur_dir;
        posdata->scratch_last_sfd = slice_count;
        posdata->cur_sfd = cur_slice;
        posdata->max_sfd = slice_count;
        posdata->scratch_ramp_left = 0;
        posdata->scratch_last_reset = 1;
        posdata->scratch_sync_bias = 0.0;
        posdata->scratch_sync_hold_blocks = 0;
        posdata->scratch_stable_blocks = 0;
        slow_motion_clear_turn_history(posdata);
    } else {
        posdata->scratch_last_reset = 0;
        double dv = fabs(posdata->scratch_target_vel - target_vel);
        if (edge_transition || posdata->scratch_last_dir != cur_dir ||
            posdata->scratch_last_sfd != slice_count || posdata->cur_sfd != cur_slice ||
            dv > 0.0005) {
            int ramp = dst_samples / 2;
            if (ramp < 512)
                ramp = 512;
            if (ramp > 4096)
                ramp = 4096;
            posdata->scratch_ramp_left = ramp;
            posdata->scratch_sync_hold_blocks = 4;
            posdata->scratch_stable_blocks = 0;
        }
        posdata->scratch_target_vel = target_vel;
        posdata->scratch_last_dir = cur_dir;
        posdata->scratch_last_sfd = slice_count;
        posdata->cur_sfd = cur_slice;
        posdata->max_sfd = slice_count;
    }

    if (!reset_head && !edge_transition) {
        double err = sync_pos - posdata->scratch_pos;
        double max_snap = (double)pred_len * 8.0;
        if (fabs(err) > max_snap) {
            posdata->scratch_pos = sync_pos;
            posdata->scratch_vel = target_vel;
            posdata->scratch_ramp_left = 0;
        } else {
            double max_bias = fabs(target_vel) * 0.0125;
            if (max_bias < 0.0001)
                max_bias = 0.0001;
            if (max_bias > 0.006)
                max_bias = 0.006;
            double bias = err / ((double)dst_samples * 64.0);
            bias = slow_motion_clampd(bias, -max_bias, max_bias);
            target_vel += bias;
        }
    }

    double predicted_end = posdata->scratch_pos + target_vel * (double)dst_samples;
    double ctx_abs_start = 0.0;
    int ctx_samples = 0;
    long long ctx_first = 0;
    long long ctx_last = 0;
    slow_scratch_ctx_map_t ctx_map;
    veejay_memset(&ctx_map, 0, sizeof(ctx_map));

    int got_ctx = slow_motion_fetch_scratch_context(info, el, posdata,
                                                    p->down_sample_buffer,
                                                    &ctx_map,
                                                    pred_len,
                                                    frame_bytes,
                                                    speed,
                                                    posdata->scratch_pos,
                                                    predicted_end,
                                                    &ctx_abs_start,
                                                    &ctx_samples,
                                                    &ctx_first,
                                                    &ctx_last);

    if (got_ctx <= 0 || ctx_samples <= 0) {
        veejay_memset(dst, 0, (size_t)dst_samples * (size_t)frame_bytes);
        vj_audio_declick_apply(p, dst, dst_samples, frame_bytes,
                               AUDIO_PATH_SILENCE, 0, 0,
                               pending_edge, direction_flipped);
        vj_perform_clear_audio_edges(info, edge, cur_dir);
        posdata->prev_n_samples = dst_samples;
        return dst_samples;
    }

    double pos0 = posdata->scratch_pos;
    double pos1 = posdata->scratch_pos;
    double vel0 = posdata->scratch_vel;
    double vel1 = posdata->scratch_vel;
    int step_max = 0;
    int step_avg = 0;

    int copied = slow_motion_render_scratch_head_s16(dst,
                                                     dst_samples,
                                                     p->down_sample_buffer,
                                                     ctx_samples,
                                                     ctx_abs_start,
                                                     &ctx_map,
                                                     posdata,
                                                     target_vel,
                                                     sync_pos,
                                                     frame_bytes,
                                                     pred_len,
                                                     edge_transition,
                                                     &step_max,
                                                     &step_avg,
                                                     &pos0,
                                                     &pos1,
                                                     &vel0,
                                                     &vel1);
    if (copied <= 0) {
        veejay_memset(dst, 0, (size_t)dst_samples * (size_t)frame_bytes);
        copied = dst_samples;
    }

    int edge_delta = -1;
    if (posdata->audio_diag_valid && posdata->audio_diag_frame_bytes == frame_bytes)
        edge_delta = vj_audio_frame_delta_s16(dst, posdata->audio_diag_last_frame, frame_bytes);

    if (hard_edge || edge_delta >= 1800 || step_max >= 3200) {
        vj_audio_declick_apply(p, dst, copied, frame_bytes,
                               AUDIO_PATH_SLOW, speed, cur_dir,
                               hard_edge ? pending_edge : AUDIO_EDGE_DIRECTION,
                               direction_flipped || edge_delta >= 1800 || step_max >= 3200);
    } else {
        vj_audio_declick_observe(p, dst, copied, frame_bytes,
                                 AUDIO_PATH_SLOW, speed, cur_dir);
    }

    if (posdata->audio_diag_valid && posdata->audio_diag_frame_bytes == frame_bytes) {
        vj_audio_copy_last_frame(posdata->audio_diag_prev_frame,
                                 (int)sizeof(posdata->audio_diag_prev_frame),
                                 posdata->audio_diag_last_frame, 1, frame_bytes);
    } else {
        veejay_memset(posdata->audio_diag_prev_frame, 0,
                      sizeof(posdata->audio_diag_prev_frame));
    }

    vj_audio_copy_last_frame(posdata->audio_diag_last_frame,
                             (int)sizeof(posdata->audio_diag_last_frame),
                             dst, copied, frame_bytes);
    posdata->audio_diag_valid = 1;
    posdata->audio_diag_frame_bytes = frame_bytes;
    slow_motion_update_turn_history(posdata, dst, copied, frame_bytes);

    posdata->audio_last_stretched_samples = dst_samples;
    posdata->audio_total_samples = ctx_samples;
    posdata->audio_src_offset = (int)((ctx_map.valid)
        ? slow_motion_ctx_exact_to_actual_rel(&ctx_map, posdata->scratch_pos, ctx_samples)
        : (posdata->scratch_pos - ctx_abs_start));
    posdata->last_resampled_frame = target_frame;
    posdata->last_resampled_dir = cur_dir;
    posdata->cur_sfd = cur_slice;
    posdata->max_sfd = slice_count;
    posdata->scratch_last_sfd = slice_count;
    posdata->consumed_samples = 0;
    posdata->prev_n_samples = copied;

    if (edge != NULL)
        edge->ticks_since_last_flip++;

    vj_perform_clear_audio_edges(info, edge, cur_dir);
    return copied;
}

static int vj_audio_retime_slow_cubic_s16(uint8_t *dst, int dst_samples, const uint8_t *src, int src_samples, int frame_bytes, double rate)
{
    if (dst == NULL || dst_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || src_samples <= 0) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    if ((frame_bytes & 1) || rate <= 0.0) {
        int n = (src_samples < dst_samples) ? src_samples : dst_samples;
        if (src != dst && n > 0)
            veejay_memcpy(dst, src, n * frame_bytes);
        vj_audio_pad_exact_tail(dst, n, dst_samples, frame_bytes);
        return dst_samples;
    }

    const int words = frame_bytes / 2;
    const int16_t *in = (const int16_t*)src;
    int16_t *out = (int16_t*)dst;
    const int max_index = src_samples - 1;

    if (src_samples == 1) {
        for (int i = 0; i < dst_samples; i++)
            for (int c = 0; c < words; c++)
                out[(i * words) + c] = in[c];
        return dst_samples;
    }

    const double step = ((double)src_samples / (double)dst_samples);

    for (int i = 0; i < dst_samples; i++) {
        double pos = (double)i * step;
        if (pos < 0.0)
            pos = 0.0;
        else if (pos > (double)max_index)
            pos = (double)max_index;

        int idx = (int)floor(pos);
        double frac = pos - (double)idx;

        if (idx < 0) {
            idx = 0;
            frac = 0.0;
        } else if (idx > max_index) {
            idx = max_index;
            frac = 0.0;
        }

        int i0 = idx - 1;
        int i1 = idx;
        int i2 = idx + 1;
        int i3 = idx + 2;

        if (i0 < 0) i0 = 0;
        if (i2 > max_index) i2 = max_index;
        if (i3 > max_index) i3 = max_index;

        const int b0 = i0 * words;
        const int b1 = i1 * words;
        const int b2 = i2 * words;
        const int b3 = i3 * words;
        const int bo = i * words;

        for (int c = 0; c < words; c++) {
            out[bo + c] = slow_motion_cubic_interp_s16(
                in[b0 + c], in[b1 + c], in[b2 + c], in[b3 + c], frac
            );
        }
    }

    return dst_samples;
}

static void vj_audio_pad_exact_tail(uint8_t *dst, int produced, int expected, int frame_bytes)
{
    if (dst == NULL || expected <= 0 || frame_bytes <= 0)
        return;

    if (produced <= 0) {
        veejay_memset(dst, 0, expected * frame_bytes);
        return;
    }

    if (produced >= expected)
        return;

    uint8_t *last = dst + ((size_t)(produced - 1) * (size_t)frame_bytes);
    uint8_t *out = dst + ((size_t)produced * (size_t)frame_bytes);

    for (int i = produced; i < expected; i++) {
        veejay_memcpy(out, last, frame_bytes);
        out += frame_bytes;
    }
}

static int vj_perform_retime_audio_chunk(veejay_t *info,
                                         performer_t *p,
                                         editlist *el,
                                         uint8_t *dst,
                                         int dst_samples,
                                         const uint8_t *src,
                                         int src_samples,
                                         int frame_bytes)
{
    if (dst == NULL || dst_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || src_samples <= 0) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    const double rate = vj_perform_runtime_effective_audio_rate(info, el);

    if (rate < 0.9995 && !(frame_bytes & 1))
        return vj_audio_retime_slow_cubic_s16(dst, dst_samples, src, src_samples, frame_bytes, rate);

    if ((frame_bytes & 1) || p == NULL || p->audio_scratcher == NULL) {
        if (src != dst) {
            int n = (src_samples < dst_samples) ? src_samples : dst_samples;
            veejay_memcpy(dst, src, n * frame_bytes);
            vj_audio_pad_exact_tail(dst, n, dst_samples, frame_bytes);
        } else {
            vj_audio_pad_exact_tail(dst, src_samples, dst_samples, frame_bytes);
        }
        return dst_samples;
    }

    if (src_samples == dst_samples && rate > 0.999 && rate < 1.001) {
        if (src != dst)
            veejay_memcpy(dst, src, dst_samples * frame_bytes);
        return dst_samples;
    }

    int produced = vj_scratch_process(p->audio_scratcher,
                                      (short*)dst,
                                      dst_samples,
                                      (const short*)src,
                                      src_samples,
                                      rate);

    if (produced < 0)
        produced = 0;
    if (produced > dst_samples)
        produced = dst_samples;

    vj_audio_pad_exact_tail(dst, produced, dst_samples, frame_bytes);
    return dst_samples;
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

    if (client_frames_to_write <= 0 || audio_payload_chunk == NULL || el == NULL)
        return 0;

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

    if (num_samples_a > 0)
        vj_audio_consume_chain(info, p->top_audio_buffer, num_samples_a);

    int num_samples_b =
        vj_perform_queue_audio_frame(info, (void*)q,
                                     q->top_audio_buffer,
                                     speed_b,
                                     target_frame_b,
                                     sample_b);

    int num_samples = (num_samples_a < num_samples_b)
                        ? num_samples_a
                        : num_samples_b;

    if (num_samples <= 0) {
        veejay_memset(audio_payload_chunk, 0, client_frames_to_write * bps);
        return client_frames_to_write;
    }

    double trans_len = (double)(trans_end_frame - trans_start_frame);
    if (trans_len <= 0.0)
        trans_len = 1.0;

    const double absolute_frame = (double)target_frame_a;
    float t = (float)((absolute_frame - trans_start_frame) / trans_len);
    t = fminf(fmaxf(t, 0.0f), 1.0f);

    if (absolute_frame >= (double)trans_end_frame) {
        vj_audio_crossfade_buffers(
            g,
            NULL,
            (speed_b != 0) ? q->top_audio_buffer : NULL,
            p->audio_render_buffer,
            num_samples,
            num_channels,
            bps,
            1.0f,
            0.0f,
            1.0f
        );
    } else {
        vj_audio_crossfade_buffers(
            g,
            (speed_a != 0) ? p->top_audio_buffer : NULL,
            (speed_b != 0) ? q->top_audio_buffer : NULL,
            p->audio_render_buffer,
            num_samples,
            num_channels,
            bps,
            t,
            1.0f,
            1.0f
        );
    }

    const double rate = vj_perform_runtime_audio_rate(info, el);
    if (rate > 0.9995 && rate < 1.0005) {
        int n = (num_samples < client_frames_to_write) ? num_samples : client_frames_to_write;
        if (n > 0)
            veejay_memcpy(audio_payload_chunk, p->audio_render_buffer, n * bps);
        return n;
    }

    return vj_perform_retime_audio_chunk(info,
                                         p,
                                         el,
                                         audio_payload_chunk,
                                         client_frames_to_write,
                                         p->audio_render_buffer,
                                         num_samples,
                                         bps);
}

static int vj_perform_queue_audio_frame_buf(veejay_t *info, performer_t *p, uint8_t *a_buf, editlist *el,int speed, long long target_frame )
{
    int num_samples = 0;

    if(!info || !p || !a_buf || !el)
        return 0;

    if(!el->has_audio || speed == 0 || target_frame == -1) {
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

static int vj_perform_queue_audio_frame_impl(veejay_t *info, void *ptr, uint8_t *a_buf, int speed, long long target_frame,int sample_id, int *audio_sample_ptr)
{
    if( info->audio == NO_AUDIO )
        return 0;
    editlist *el_fallback = info->current_edit_list;
    editlist *el = (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_editlist(sample_id) : info->edit_list);
    if(el == NULL)
        el = el_fallback; //safety

    performer_t *p = (performer_t*) ptr;
    int *sample_cursor = audio_sample_ptr ? audio_sample_ptr : &(p->play_audio_sample_);

    if(!el->has_audio || speed == 0 || target_frame == -1) {
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
                    num_samples = vj_perform_fill_audio_buffers(info,el, a_buf, p, sample_cursor, target_frame);
                break;
            case VJ_PLAYBACK_MODE_PLAIN:
                if( el->has_audio )
                    num_samples = vj_perform_fill_audio_buffers(info,el, a_buf, p, sample_cursor, target_frame);
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
    return 0;
}

int vj_perform_queue_audio_frame(veejay_t *info, void *ptr, uint8_t *a_buf, int speed, long long target_frame,int sample_id )
{
    performer_t *p = (performer_t*) ptr;
    if(!p)
        return 0;

    return vj_perform_queue_audio_frame_impl(info,
                                             ptr,
                                             a_buf,
                                             speed,
                                             target_frame,
                                             sample_id,
                                             &(p->play_audio_sample_));
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
        if (chain[i] && chain[i]->e_flag == 1)
            requested_active_count++;
    }

    if (requested_active_count != current_buf->count) {
        changed = 1;
    } else {
        int current_entry_idx = 0;
        for (int i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
            if (chain[i]->e_flag != 1
                || chain[i]->beat_flag != 1 ) continue;

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
        if (chain[i]->e_flag != 1 || chain[i]->beat_flag != 1) continue;
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

#endif // HAVE_JACK


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

static void vj_perform_end_transition(veejay_t *info, int mode, int sample)
{
    video_playback_setup *settings = info->settings;

    if (!settings->transition.ready)
        return;

    int target_mode = vj_seq_type_to_playback_mode(mode);
    int target_id = sample;
    int target_slot = settings->transition.seq_index;

    if (info->seq->active &&
        target_slot >= 0 &&
        target_slot < MAX_SEQUENCES &&
        info->seq->samples[target_slot].sample_id > 0)
    {
        target_mode = vj_seq_type_to_playback_mode(
            info->seq->samples[target_slot].type
        );

        target_id = info->seq->samples[target_slot].sample_id;
        info->seq->current = target_slot;
    }

    vj_perform_reset_transition(info);

    veejay_change_playback_mode(info, target_mode, target_id);

    settings->transition.ready = 0;
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

    vj_perform_cache_put_frame( g, sample_id, source_type, p->primary_buffer[0]);

    switch (source_type)
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

            sample_eff_chain **beat_local_chain = sample_get_effect_chain(sample_id);

            if(info->global_chain && info->global_chain->enabled)
                vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);

#ifdef HAVE_JACK
            vj_perform_audio_beat_apply_render_chains(
                info,
                sample_id,
                source_type,
                p->chain_id,
                beat_local_chain,
                p->pvar_.fx_status);
#endif

            sample_info *si = sample_get(sample_id);
            if(si) {
                if( info->global_chain->enabled == 1) { 
                    if(p->pvar_.fx_status) vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type,setup, si->effect_chain, si);
                    vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);
                    vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,info->global_chain->fx_chain, si);
                } else if(info->global_chain->enabled == 2) {
                    vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);
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

            /*
             * PLAIN has no FX render chain and must not compose the global FX
             * chain.  Beat auto-FX is intentionally not applied here.
             */
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

            sample_eff_chain **beat_tag_chain = vj_tag_get_effect_chain(sample_id);

            if(info->global_chain && info->global_chain->enabled)
                vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);

#ifdef HAVE_JACK
            vj_perform_audio_beat_apply_render_chains(
                info,
                sample_id,
                source_type,
                p->chain_id,
                beat_tag_chain,
                p->pvar_.fx_status);
#endif

            vj_tag *tag = vj_tag_get( sample_id );
            if(tag) {
                if( info->global_chain->enabled == 1) {
                    if(p->pvar_.fx_status ) vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,tag->effect_chain, tag);
                    vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);
                    vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,info->global_chain->fx_chain, tag);

                } else if(info->global_chain->enabled == 2) {
                    vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);
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

#ifdef HAVE_JACK
    {
        static int ab_queue_seq = 0;
        int ab_mode = info->uc ? info->uc->playback_mode : -1;

        if(vj_perform_audio_beat_playmode_has_fx_chain(ab_mode))
        {
            ab_queue_seq++;
          /*  if(ab_queue_seq <= 12 || (ab_queue_seq % 120) == 0) {
                veejay_msg(VEEJAY_MSG_INFO,
                           "] performer-v27 queue-video seq=%d sample=%d mode=%d audio=%d beat_enabled=%d beat_open=%d beat_running=%d global_enabled=%d hold=%d",
                           ab_queue_seq,
                           info->uc ? info->uc->sample_id : -1,
                           ab_mode,
                           info->audio,
                           vj_audio_beat_is_enabled(&settings->audio_beat),
                           vj_audio_beat_is_open(&settings->audio_beat),
                           vj_audio_beat_is_running(&settings->audio_beat),
                           ((info->uc && vj_perform_audio_beat_playmode_has_fx_chain(info->uc->playback_mode) && info->global_chain) ? info->global_chain->enabled : 0),
                           settings->hold_fx);
            }*/
        }
    }
#endif

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

    int col_vib = atomic_load_int(&settings->color_vibrance);
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

    const int prev_dir = (settings->current_playback_speed < 0 ? -1 :
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

    if(speed == 0) {
        settings->current_playback_speed = 0;
        return;
    }

    long long cur_slice = atomic_load_int(&settings->audio_slice);
    long long max_sfd   = atomic_load_int(&settings->audio_slice_len);
    long long cur_frame = atomic_load_long_long(&settings->current_frame_num);
    long long next_frame = cur_frame + num;

    if (max_sfd > 1 && cur_slice < (max_sfd - 1)) {
        atomic_store_int(&settings->audio_slice, cur_slice + 1);
        return;
    }

    int edge_type = AUDIO_EDGE_NONE;
    int next_dir = cur_dir;

    if (looptype == 3) {
        long long random_frame = next_frame;

        if (speed > 0)
            random_frame = vj_frame_rand(cur_frame, start, end, settings->master_frame_num);
        else if (speed < 0)
            random_frame = vj_frame_rand(cur_frame, end, start, settings->master_frame_num);

        if (random_frame != next_frame) {
            next_frame = random_frame;
            edge_type = AUDIO_EDGE_JUMP;
        }
    }

    if (next_frame > end) {
        switch (looptype) {
            case 2:
                next_dir = -cur_dir;
                veejay_set_speed(info, next_dir * abs(speed), 1);
                next_frame = end;
                edge_type = AUDIO_EDGE_DIRECTION;
                break;
            case 1:
                next_frame = start;
                next_dir = 1;
                edge_type = AUDIO_EDGE_RESET;
                break;
            case 3:
                edge_type = AUDIO_EDGE_JUMP;
                break;
            default:
                next_frame = end;
                veejay_set_speed(info, 0, 1);
                next_dir = 0;
                edge_type = AUDIO_EDGE_SILENCE;
                break;
        }
    } else if (next_frame < start) {
        switch (looptype) {
            case 2:
                next_dir = -cur_dir;
                veejay_set_speed(info, next_dir * abs(speed), 1);
                next_frame = start;
                edge_type = AUDIO_EDGE_DIRECTION;
                break;
            case 1:
                next_frame = end;
                next_dir = -1;
                edge_type = AUDIO_EDGE_JUMP;
                break;
            case 3:
                edge_type = AUDIO_EDGE_JUMP;
                break;
            default:
                next_frame = start;
                veejay_set_speed(info, 0, 1);
                next_dir = 0;
                edge_type = AUDIO_EDGE_SILENCE;
                break;
        }
    }

    if (edge_type == AUDIO_EDGE_NONE && speed != 0) {
        int expected_span = abs(speed);
        expected_span = (expected_span < 1) ? 1 : expected_span;

        long long moved = next_frame - cur_frame;
        moved = (moved < 0) ? -moved : moved;

        if (moved > expected_span)
            edge_type = AUDIO_EDGE_JUMP;
    }

    if (next_dir != prev_dir)
        atomic_store_int(&settings->audio_direction_changed, 1);

    if (edge_type != AUDIO_EDGE_NONE)
        vj_perform_initiate_edge_change(info, edge_type, prev_dir, next_dir);

    if (mode != VJ_PLAYBACK_MODE_PLAIN && edge_type != AUDIO_EDGE_NONE) {
        int playback_ended = (mode == VJ_PLAYBACK_MODE_SAMPLE ?
            sample_loop_dec(info->uc->sample_id) :
            vj_tag_loop_dec(info->uc->sample_id));
        (void)playback_ended;
    }

    atomic_store_long_long(&settings->current_frame_num, next_frame);

    if (max_sfd > 1)
        atomic_store_int(&settings->audio_slice, 0);

    if (mode == VJ_PLAYBACK_MODE_SAMPLE)
        vj_perform_rand_update(info);
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

    int remaining = sample_get_remaining_frames(take_n);

    if (remaining < min_delay)
        remaining = min_delay;

    if( settings->randplayer.timer == RANDTIMER_FRAME )
    {
        max_delay = vj_frame_rand(
            track_dup,
            min_delay,
            remaining,
            settings->randplayer.seed ++ );
    }
    else
    {
        max_delay = remaining;
    }

    settings->randplayer.max_delay = max_delay;
    settings->randplayer.min_delay = min_delay; 

    veejay_msg(VEEJAY_MSG_INFO, "Sample randomizer triggers in %d frame periods", max_delay);

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

