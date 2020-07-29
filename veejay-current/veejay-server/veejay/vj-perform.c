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
#include <veejaycore/defs.h>
#include <libsample/sampleadm.h>  
#include <libstream/vj-tag.h>
#include <veejaycore/vj-server.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-lib.h>
#include <libel/vj-el.h>
#include <math.h>
#include <libel/vj-avcodec.h>
#include <veejay/vj-event.h>
#include <veejaycore/mpegconsts.h>
#include <veejaycore/mpegtimecode.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/vj-msg.h>
#include <veejay/vj-perform.h>
#include <veejay/libveejay.h>
#include <veejay/vj-sdl.h>
#include <libsamplerec/samplerecord.h>
#include <libel/pixbuf.h>
#include <libel/avcommon.h>
#include <veejay/vj-misc.h>
#include <veejaycore/vj-task.h>
#include <veejaycore/lzo.h>
#include <veejay/vj-viewport.h>
#include <veejay/vj-composite.h>
#ifdef HAVE_FREETYPE
#include <veejay/vj-font.h>
#endif
#define RECORDERS 1
#ifdef HAVE_JACK
#include <veejay/vj-jack.h>
#endif
#include <libvje/internal.h>
#include <veejaycore/vjmem.h>
#include <libvje/effects/opacity.h>
#include <libvje/effects/masktransition.h>
#include <libvje/effects/shapewipe.h>
#include <libqrwrap/qrwrapper.h>
#include <veejay/vj-split.h>
#include <libresample/resample.h>
#include <libvje/libvje.h>
#define PERFORM_AUDIO_SIZE 16384
#define PSLOW_A 3
#define PSLOW_B 4
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#ifndef SAMPLE_FMT_S16
#define SAMPLE_FMT_S16 AV_SAMPLE_FMT_S16
#endif

#define PRIMARY_FRAMES 5

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

#define RUP8(num)(((num)+8)&~8)

extern uint8_t pixel_Y_lo_;

#define CACHE_TOP 0
#define CACHE 1
#define CACHE_SIZE (SAMPLE_MAX_EFFECTS+CACHE) * 2

typedef struct
{
    varcache_t pvar_;
    VJFrame *rgba_frame[2];
    uint8_t *rgba_buffer[2];

    VJFrame *yuva_frame[2];
    VJFrame *yuv420_frame[2];
    ycbcr_frame **frame_buffer;   /* chain */
    ycbcr_frame **primary_buffer; /* normal */

    int frame_info[64][SAMPLE_MAX_EFFECTS];  /* array holding frame lengths  */
    uint8_t *audio_buffer[SAMPLE_MAX_EFFECTS];   /* the audio buffer */
    uint8_t *audio_silence_;
    uint8_t *lin_audio_buffer_;
    uint8_t *top_audio_buffer;
    int play_audio_sample_;
    uint8_t *audio_rec_buffer;
    uint8_t *audio_render_buffer;
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

    void *resample_context[(MAX_SPEED+1)];
    void *downsample_context[(MAX_SPEED+1)];
    uint32_t is_supersampled;

    VJFrame *tmp1;
    VJFrame *tmp2;

    int chain_id;
} performer_t;

typedef struct
{
    int sample_id;
    ycbcr_frame *frame;
} performer_cache_t;


typedef struct
{
    performer_cache_t cached_tag_frames[CACHE_SIZE];   /* cache a frame into the buffer only once */
    performer_cache_t cached_sample_frames[CACHE_SIZE];
    
    int n_cached_tag_frames;
    int n_cached_sample_frames;


    void *encoder_;
    ycbcr_frame *preview_buffer;
    int preview_max_w;
    int preview_max_h;
    performer_t *A;
    performer_t *B;
    VJFrame feedback_frame;
    uint8_t *feedback_buffer[4];
    VJFrame *offline_frame;
} performer_global_t;

static const char *intro = 
    "A visual instrument for GNU/Linux\n";
static const char *license =
    "This program is licensed as\nFree Software (GNU/GPL version 2)\n\nFor more information see:\nhttp://veejayhq.net\n";
static const char *copyr =
    "(C) 2002-2018 Copyright N.Elburg et all (nwelburg@gmail.com)\n";

#define MLIMIT(var, low, high) \
if((var) < (low)) { var = (low); } \
if((var) > (high)) { var = (high); }

//forward
static  int vj_perform_preprocess_secundary( veejay_t *info, performer_t *p, int id, int mode,int current_ssm,int chain_entry, VJFrame **frames, VJFrameInfo *frameinfo );
static int vj_perform_get_frame_fx(veejay_t *info, int s1, long nframe, VJFrame *src, VJFrame *dst, uint8_t *p0plane, uint8_t *p1plane);

static void vj_perform_pre_chain(performer_t *p, VJFrame *frame);
static int vj_perform_post_chain_sample(veejay_t *info,performer_t *p, VJFrame *frame,int sample_id);
static int vj_perform_post_chain_tag(veejay_t *info,performer_t*p, VJFrame *frame, int sample_id);
static int vj_perform_next_sequence( veejay_t *info, int *type, int *new_current );
static void vj_perform_plain_fill_buffer(veejay_t * info,performer_t *p,VJFrame *dst,int sample_id, long frame_num);
static void vj_perform_tag_fill_buffer(veejay_t * info, performer_t *p, VJFrame *dst, int sample_id);
static void vj_perform_clear_cache(performer_global_t *g);
static int vj_perform_increase_tag_frame(veejay_t * info, long num);
static int vj_perform_increase_plain_frame(veejay_t * info, long num);
static int vj_perform_apply_secundary_tag(veejay_t * info, performer_t *p, int sample_id,int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0, uint8_t *p1, int subrender);
static int vj_perform_apply_secundary(veejay_t * info, performer_t *p, int this_sample_id,int sample_id,int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0, uint8_t *p1, int subrender);
static void vj_perform_tag_complete_buffers(veejay_t * info, performer_t *p, vjp_kf *effect_info, int *h, VJFrame *f0, VJFrame *f1, int sample_id, int pm, vjp_kf *setup);
static int vj_perform_increase_sample_frame(veejay_t * info, long num);
static void vj_perform_sample_complete_buffers(veejay_t * info, performer_t *p, vjp_kf *effect_info, int *h, VJFrame *f0, VJFrame *f2, int sample_id, int pm, vjp_kf *setup);
static int vj_perform_use_cached_frame(ycbcr_frame *cached_frame, VJFrame *dst);
static void vj_perform_apply_first(veejay_t *info, performer_t *p, vjp_kf *todo_info, VJFrame **frames, sample_eff_chain *entry, int e, int c, int n_frames, void *ptr, int playmode );
static int vj_perform_render_sample_frame(veejay_t *info, performer_t *p, uint8_t *frame[4], int sample, int type);
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4]);
static int vj_perform_record_commit_single(veejay_t *info);
static int vj_perform_get_subframe(veejay_t * info, int this_sample_id, int sub_sample,int chain_entyr );
static int vj_perform_get_subframe_tag(veejay_t * info, int sub_sample,int chain_entry );
#ifdef HAVE_JACK
static void vj_perform_reverse_audio_frame(veejay_t * info, int len, uint8_t *buf );
#endif
static void vj_perform_end_transition(veejay_t *info, int mode, int sample);

static void vj_perform_set_444(VJFrame *frame)
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

static void vj_perform_set_422(VJFrame *frame)
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

static void vj_perform_supersample(video_playback_setup *settings,performer_t *p, VJFrame *one, VJFrame *two, int sm)
{
    if(p->is_supersampled) {
        if(one != NULL && one->ssm == 0) {
            chroma_supersample( settings->sample_mode,one,one->data );
            vj_perform_set_444(one);
        }
        if(two != NULL && two->ssm == 0) {
            chroma_supersample( settings->sample_mode,two,two->data );
            vj_perform_set_444(two);
        }

        return;
    }
    
    if(sm == 1) {
        if(one != NULL && one->ssm == 0) {
            chroma_supersample( settings->sample_mode,one,one->data );
            vj_perform_set_444(one);
        }
        if(two != NULL && two->ssm == 0) {
            chroma_supersample( settings->sample_mode,two,two->data );
            vj_perform_set_444(two);
        }
        p->is_supersampled = 1;
    } 
    else if( sm == 0 ) {
        if(one != NULL && one->ssm == 1) { 
            chroma_subsample( settings->sample_mode,one,one->data);
            vj_perform_set_422(one);
        }
        if(two != NULL && two->ssm == 1) {
            chroma_subsample( settings->sample_mode,two,two->data);
            vj_perform_set_422(two);
        }
    }
}

static  void    vj_perform_copy( ycbcr_frame *src, ycbcr_frame *dst, int Y_len, int UV_len, int alpha_len )
{
    uint8_t *input[4] = { src->Y, src->Cb, src->Cr,src->alpha };
    uint8_t *output[4] = { dst->Y, dst->Cb, dst->Cr,dst->alpha };
    int     strides[4] = { Y_len, UV_len, UV_len, alpha_len };
    vj_frame_copy(input,output,strides);
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
static inline void vj_perform_play_audio( video_playback_setup *settings, uint8_t *source, int len, uint8_t *silence )
{
    // if auto mute is enabled, play muted data 
    // audio_silence_ is allocated once and played in-place of, thus recording chain is not affected by auto-mute
    if( settings->auto_mute ) {
        vj_jack_play( silence, len );
    } else {
        vj_jack_play( source, len );
    }

}
#endif

static ycbcr_frame *vj_perform_tag_is_cached(veejay_t *info,int tag_id)
{
    int c;
    performer_global_t *g = (performer_global_t*) info->performer;    

    for(c=0; c < g->n_cached_tag_frames; c++)
    {
        if(g->cached_tag_frames[c].sample_id == tag_id) 
        {
            if( info->settings->feedback && info->uc->sample_id == tag_id &&
                        info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
                return NULL; //@ feedback cannot be cached source
            }
            return g->cached_tag_frames[c].frame;
        }
    }

    return NULL;
}

static ycbcr_frame *vj_perform_sample_is_cached(veejay_t *info,int sample_id)
{
    int c;
    performer_global_t *g = (performer_global_t*) info->performer;    

    for(c=0; c < g->n_cached_sample_frames; c++)
    {
        if(g->cached_sample_frames[c].sample_id == sample_id) 
        {
            if( info->settings->feedback && info->uc->sample_id == sample_id  &&
                    info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
                return NULL;
            }
            return g->cached_sample_frames[c].frame;
        }
    }

    return NULL;
}

static void vj_perform_clear_cache(performer_global_t *g)
{
    int c;
    for( c = 0;  c < g->n_cached_sample_frames; c ++ ) {
        g->cached_sample_frames[c].sample_id = 0;
        g->cached_sample_frames[c].frame = NULL;
    }
    for( c = 0;  c < g->n_cached_tag_frames; c ++ ) {
        g->cached_tag_frames[c].sample_id = 0;
        g->cached_tag_frames[c].frame = NULL;
    }

    g->n_cached_tag_frames = 0;
    g->n_cached_sample_frames = 0;
}

static int vj_perform_increase_tag_frame(veejay_t * info, long num)
{
    video_playback_setup *settings = info->settings;
    settings->current_frame_num += num;
 
    if (settings->current_frame_num > settings->max_frame_num) {
        vj_tag_set_loop_stats( info->uc->sample_id, -1 );
    }

   if (settings->current_frame_num < settings->min_frame_num) {
    settings->current_frame_num = settings->min_frame_num;
    if (settings->current_playback_speed < 0) {
        settings->current_playback_speed =
        +(settings->current_playback_speed);
    }
        return 0;
    }

    if( settings->current_frame_num > settings->max_frame_num ) {
        settings->current_frame_num = settings->min_frame_num;
    
        vj_perform_try_sequence(info);
    }

/*
    if( info->seq->active ) {
        veejay_msg(VEEJAY_MSG_DEBUG, "Play frame %ld", settings->current_frame_num );

 
        if( settings->current_frame_num > settings->max_frame_num ) {
         	settings->current_frame_num = settings->max_frame_num;
            int type = 0;

            veejay_msg(VEEJAY_MSG_DEBUG, "Reach end position %ld (ready=%d,sample=%d)", settings->current_frame_num,settings->transition.ready,info->uc->sample_id );

            int n = vj_perform_next_sequence( info, &type );
            if( n > 0 ) {
                //if(!settings->transition.active) {
                    veejay_msg(VEEJAY_MSG_INFO, "Sequence play selects %s %d", (type == 0 ? "sample" : "stream" ) , n );
                    veejay_change_playback_mode(info,(type == 0 ? VJ_PLAYBACK_MODE_SAMPLE: VJ_PLAYBACK_MODE_TAG),n );
                //}
            }
        }
    } else {
		if( settings->current_frame_num > settings->max_frame_num ) {
        	settings->current_frame_num = settings->min_frame_num;
    
        	}
		}
    }
    */

    return 0;
}


static int vj_perform_increase_plain_frame(veejay_t * info, long num)
{
    video_playback_setup *settings = info->settings;

    settings->simple_frame_dup ++;
    if (settings->simple_frame_dup >= info->sfd) {
        settings->current_frame_num += num;
        settings->simple_frame_dup = 0;
    }

    // auto loop plain mode
    if (settings->current_frame_num < settings->min_frame_num) {
        settings->current_frame_num = settings->max_frame_num;
        return 0;
    }

    if (settings->current_frame_num > settings->max_frame_num) {
        if(!info->continuous)
        {
            veejay_msg(VEEJAY_MSG_DEBUG, "Reached end of video - Ending veejay session ... ");
            veejay_change_state(info, LAVPLAY_STATE_STOP);
        }
        settings->current_frame_num = settings->min_frame_num;
        return 0;
    }

    return 0;
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

void vj_perform_setup_transition(veejay_t *info, int next_sample_id, int next_type, int sample_id, int current_type )
{
     video_playback_setup *settings = info->settings;
     settings->transition.active = ( current_type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_transition_active( sample_id ) : vj_tag_get_transition_active(sample_id));
    
     if( settings->transition.active )
     {
        int transition_length = ( current_type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_transition_length( sample_id ) : vj_tag_get_transition_length( sample_id ) );
        int transition_shape = ( current_type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_transition_shape( sample_id ) : vj_tag_get_transition_shape( sample_id ) );
     
        if(transition_shape == -1) {
            transition_shape = ( (int) ( (double) shapewipe_get_num_shapes( settings->transition.ptr ) * rand() / (RAND_MAX)));
        }
    
        int speed = (current_type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_speed(sample_id) : 1 );
        int start = (current_type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_startFrame( sample_id) : 1 );
        int end = (current_type == VJ_PLAYBACK_MODE_SAMPLE ?  sample_get_endFrame(sample_id) : vj_tag_get_n_frames(sample_id));
     
        if( speed < 0 ) {
            settings->transition.start = start + transition_length;
            settings->transition.end = start;
        }
        else if(speed > 0 ) {
            settings->transition.start = end - transition_length;
            settings->transition.end = end;
        }

        settings->transition.shape = transition_shape;
        settings->transition.next_type = next_type;
        settings->transition.next_id = next_sample_id;
        settings->transition.ready = 0;
    }
    else {
        vj_perform_reset_transition(info);
    }
}


static  int vj_perform_next_sequence( veejay_t *info, int *type, int *next_slot )
{
    video_playback_setup *settings = info->settings;
    int new_current = -1;
    int current_type = -1;
    int sample_id = vj_perform_get_next_sequence_id(info,&current_type, info->seq->current, &new_current);

    int next_current = 0;
    int next_sample_id = vj_perform_get_next_sequence_id(info,type, new_current + 1, &next_current );
    int next_type = *type;

    *next_slot = next_current;

    if( current_type == 0 ) {
        sample_update_ascociated_samples( sample_id );
    }
    else {
        vj_tag_update_ascociated_samples( sample_id );
    }
  
    return next_sample_id;
}

int vj_perform_seq_setup_transition(veejay_t *info)
{
    int next_type = 0;
    int current = info->seq->current + 1;
    int next_sample_id = vj_perform_get_next_sequence_id(info, &next_type, current, &current );
    
    if( next_sample_id != 0 ) {
        vj_perform_setup_transition( info, next_sample_id, next_type, info->uc->sample_id, info->uc->playback_mode );
        return 1;
    }
    return 0;
}

int vj_perform_try_sequence( veejay_t *info )
{
    if(! info->seq->active )
        return 0;

    int n_loops = (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ? sample_loop_dec(info->uc->sample_id) : vj_tag_loop_dec(info->uc->sample_id));
    int at_next_loop = (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ? sample_at_next_loop(info->uc->sample_id) : vj_tag_at_next_loop(info->uc->sample_id));

    if( at_next_loop == 1 ) {
        if( vj_perform_seq_setup_transition(info) == 0 ) {
            veejay_msg(VEEJAY_MSG_ERROR, "There is no next sample in the sequencer?");
            info->seq->active = 0;
            info->seq->current = 0;
    	    veejay_reset_sample_positions( info, -1 );
            return 0;
        }
    }

    if( n_loops == 1 ) {
        info->settings->transition.active = 0;
        
        int type = 0;
        int next_slot = 0;
        int n = vj_perform_next_sequence( info, &type, &next_slot );
        if( n > 0 )
        {
            if(!info->settings->transition.active) {
                if(info->seq->active) {
                    info->seq->current = next_slot;
                }
                veejay_change_playback_mode( info, (type == 0 ? VJ_PLAYBACK_MODE_SAMPLE: VJ_PLAYBACK_MODE_TAG ), n );
                return 0;
            }

            if(info->settings->transition.start == info->settings->transition.end) {
                if(info->seq->active) {
                    info->seq->current = next_slot;
                }
                veejay_change_playback_mode( info, (type == 0 ? VJ_PLAYBACK_MODE_SAMPLE: VJ_PLAYBACK_MODE_TAG ), n );
                return 0;
            }
        }
    }

    return 0;
}

static int vj_perform_increase_sample_frame(veejay_t * info, long num)
{
    video_playback_setup *settings = (video_playback_setup *) info->settings;
    int start,end,looptype,speed;

    if(sample_get_short_info(info->uc->sample_id,&start,&end,&looptype,&speed)!=0) {
            veejay_msg(0, "Sample %d no longer exists ?! Unable to queue frame", info->uc->sample_id);
            return -1;
    }

    settings->current_playback_speed = speed;

    int cur_sfd = sample_get_framedups( info->uc->sample_id );
    int max_sfd = sample_get_framedup( info->uc->sample_id );
    
    if( num )
        cur_sfd ++;

    if( max_sfd > 0 ) {
        if( cur_sfd >= max_sfd )
        {
            cur_sfd = 0;
        }
        sample_set_framedups( info->uc->sample_id , cur_sfd);
        
        if( cur_sfd != 0 ) 
            return 0;
    }

    settings->current_frame_num += num;

    if( num == 0 ) {
        sample_set_resume( info->uc->sample_id, settings->current_frame_num );
        return 0;
    }

    if (speed >= 0) {       /* forward play */

        if(looptype==3)
        {
            int range = end - start;
            int n2   = start + ((int) ( (double)range * rand()/(RAND_MAX)));
            settings->current_frame_num = n2;
        }   

        if (settings->current_frame_num > end || settings->current_frame_num < start) {
            switch (looptype) {
                case 2:
                    info->uc->direction = -1;
                    sample_set_loop_stats(info->uc->sample_id, -1);
                    veejay_set_frame(info, end);
                    veejay_set_speed(info, (-1 * speed));
                    vj_perform_try_sequence(info);

                    break;
                case 1:
                    sample_set_loop_stats(info->uc->sample_id, -1);
                    veejay_set_frame(info, start);
                    vj_perform_try_sequence(info);
                    break;
                case 3:
                    veejay_set_frame(info, start);
                    break;
                case 4:
                    veejay_set_frame(info,end);
                    break;
                default:
                    veejay_set_frame(info, end);
                    veejay_set_speed(info, 0);
                    break;
            }
        }
    } 
    else
    {           /* reverse play */
        if( looptype == 3)
        {
            int range = end - start;
            int n2   = end - ((int) ( (double)range*rand()/(RAND_MAX)));
            settings->current_frame_num = n2;
        }

        if (settings->current_frame_num < start || settings->current_frame_num >= end ) {
            switch (looptype) {
                case 2:
                    info->uc->direction = 1;
                    sample_set_loop_stats(info->uc->sample_id, -1);
                    veejay_set_frame(info, start);
                    veejay_set_speed(info, (-1 * speed));
                    vj_perform_try_sequence(info);
                    break;
                case 1:
                    sample_set_loop_stats(info->uc->sample_id, -1);
                    veejay_set_frame(info, end);

                    vj_perform_try_sequence(info);

                    break;
                case 3:
                    veejay_set_frame(info,end);
                    break;
                case 4:
                    veejay_set_frame(info,start);
                    break;
                default:
                    veejay_set_frame(info, start);
                    veejay_set_speed(info, 0);
                    break;
            }
        }
    }
    
    if(!info->seq->active) {
        sample_set_resume( info->uc->sample_id, settings->current_frame_num );
    }
    vj_perform_rand_update( info );

    return 0;
}

static int vj_perform_record_buffer_init(veejay_t *info)
{
    performer_global_t *g = (performer_global_t*) info->performer;

    g->offline_frame = (VJFrame*) vj_calloc(sizeof(VJFrame));
    if(!g->offline_frame) return 0;

    veejay_memcpy( g->offline_frame, info->effect_frame1, sizeof(VJFrame) );

    uint8_t *region = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8( g->offline_frame->len * 3 ) );
    if(!region) return 0;

    g->offline_frame->data[0] = region;
    g->offline_frame->data[1] = region + RUP8( g->offline_frame->len );
    g->offline_frame->data[2] = region + RUP8( g->offline_frame->len + g->offline_frame->uv_len );

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

int vj_perform_allocate(veejay_t *info)
{
    performer_global_t *global = (performer_global_t*) vj_calloc( sizeof(performer_global_t ));
    if(!global) {
        return 1;
    }
    global->preview_buffer = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
    if(!global->preview_buffer) {
        return 1;
    }
    global->preview_max_w = info->video_output_width * 2;
    global->preview_max_h = info->video_output_height * 2;
    global->preview_buffer->Y = (uint8_t*) vj_calloc( RUP8(global->preview_max_w * global->preview_max_h * 2) );
    if(!global->preview_buffer->Y) {
        return 1;
    }

    const int w = info->video_output_width;
    const int h = info->video_output_height;
    const long frame_len = RUP8( ((w*h)+w+w) );
    size_t buf_len = frame_len * 4 * sizeof(uint8_t);
    int mlock_success = 1;

    sample_record_init(frame_len);
    vj_tag_record_init(w,h);


    global->feedback_buffer[0] = (uint8_t*) vj_malloc( buf_len );
    global->feedback_buffer[1] = global->feedback_buffer[0] + frame_len;
    global->feedback_buffer[2] = global->feedback_buffer[1] + frame_len;
    global->feedback_buffer[3] = global->feedback_buffer[2] + frame_len;

    if(mlock( global->feedback_buffer[0], buf_len )!=0)
        mlock_success = 0;

    veejay_memset( global->feedback_buffer[0], pixel_Y_lo_,frame_len);
    veejay_memset( global->feedback_buffer[1],128,frame_len);
    veejay_memset( global->feedback_buffer[2],128,frame_len);
    veejay_memset( global->feedback_buffer[3],0,frame_len);

    veejay_memcpy(&(global->feedback_frame), info->effect_frame1, sizeof(VJFrame));

    global->feedback_frame.data[0] = global->feedback_buffer[0];
    global->feedback_frame.data[1] = global->feedback_buffer[1];
    global->feedback_frame.data[2] = global->feedback_buffer[2];
    global->feedback_frame.data[3] = global->feedback_buffer[3];

    vj_perform_clear_cache(global);

    if(mlock_success != 1) {
        veejay_msg(VEEJAY_MSG_WARNING, "Unable to lock the performer into RAM (memory may be paged to the swap area)");
    }

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
    const long frame_len = RUP8( ((w*h)+w+w) );
    unsigned int c;
    long total_used = 0;
    int performer_frame_size = frame_len * 4;

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

    size_t buf_len = performer_frame_size * sizeof(uint8_t);
    int mlock_success = 1;

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
        total_used += buf_len;
    }

    p->temp_buffer[0] = (uint8_t*) vj_malloc( buf_len );
    if(!p->temp_buffer[0]) {
        return NULL;
    }
    p->temp_buffer[1] = p->temp_buffer[0] + frame_len;
    p->temp_buffer[2] = p->temp_buffer[1] + frame_len;
    p->temp_buffer[3] = p->temp_buffer[2] + frame_len;

    if(mlock( p->temp_buffer[0], buf_len ) != 0 )
        mlock_success = 0;

    veejay_memset(p->temp_buffer[2], 128, frame_len);
    veejay_memset(p->temp_buffer[1], 128, frame_len);
    veejay_memset(p->temp_buffer[3], 0, frame_len);
    veejay_memset(p->temp_buffer[0], 0, frame_len);

    p->rgba_buffer[0] = (uint8_t*) vj_malloc( buf_len * 2 );
    if(!p->rgba_buffer[0] ) {
        return NULL;
    }

    p->rgba_buffer[1] = p->rgba_buffer[0] + buf_len;

    if( mlock( p->rgba_buffer[0], buf_len * 2 ) != 0 )
        mlock_success = 0;

    veejay_memset( p->rgba_buffer[0], 0, buf_len * 2 );

    p->subrender_buffer[0] = (uint8_t*) vj_malloc( buf_len * 3 ); //frame, p0, p1
    if(!p->subrender_buffer[0]) {
        return NULL;
    }
    p->subrender_buffer[1] = p->subrender_buffer[0] + frame_len;
    p->subrender_buffer[2] = p->subrender_buffer[1] + frame_len;
    p->subrender_buffer[3] = p->subrender_buffer[2] + frame_len;


    if(mlock( p->subrender_buffer[0], buf_len ) != 0 )
        mlock_success = 0;

    veejay_memset( p->subrender_buffer[0], pixel_Y_lo_,frame_len);
    veejay_memset( p->subrender_buffer[1],128,frame_len);
    veejay_memset( p->subrender_buffer[2],128,frame_len);
    veejay_memset( p->subrender_buffer[3],0,frame_len);



    total_used += buf_len; //temp_buffer
    total_used += (buf_len * 3); //subrender_buffer
    total_used += (buf_len * 2); //rgb conversion buffer

//allocate fx_chain_buffer
    size_t tmp1 = buf_len * 4 * sizeof(uint8_t) * SAMPLE_MAX_EFFECTS;
    p->fx_chain_buffer = vj_hmalloc( tmp1, "in fx chain buffers" );
    if(p->fx_chain_buffer == NULL ) {
        veejay_msg(VEEJAY_MSG_WARNING,"Unable to allocate sufficient memory to keep all FX chain buffers in RAM");
        return NULL;
    }
    total_used += tmp1;
    p->fx_chain_buflen = tmp1;
        
    /* set up pointers for frame_buffer */
    for (c = 0; c < SAMPLE_MAX_EFFECTS; c++) {
        p->frame_buffer[c] = (ycbcr_frame *) vj_calloc(sizeof(ycbcr_frame));
         if(!p->frame_buffer[c]) {
             return NULL;
         }

         uint8_t *ptr = p->fx_chain_buffer + (c * frame_len * 4 * 3); // each entry, has 3 frames with 4 planes each
         p->frame_buffer[c]->Y = ptr;
         p->frame_buffer[c]->Cb = p->frame_buffer[c]->Y + frame_len;
         p->frame_buffer[c]->Cr = p->frame_buffer[c]->Cb + frame_len;
         p->frame_buffer[c]->alpha = p->frame_buffer[c]->Cr + frame_len;

         p->frame_buffer[c]->P0  = ptr + (frame_len * 4);
         p->frame_buffer[c]->P1  = p->frame_buffer[c]->P0 + (frame_len*4);

         veejay_memset( p->frame_buffer[c]->Y, pixel_Y_lo_,frame_len);
         veejay_memset( p->frame_buffer[c]->Cb,128,frame_len);
         veejay_memset( p->frame_buffer[c]->Cr,128,frame_len);
         veejay_memset( p->frame_buffer[c]->alpha,0,frame_len);
         veejay_memset( p->frame_buffer[c]->P0, pixel_Y_lo_, frame_len );
         veejay_memset( p->frame_buffer[c]->P0 + frame_len, 128, frame_len * 4);
         veejay_memset( p->frame_buffer[c]->P1, pixel_Y_lo_, frame_len );
         veejay_memset( p->frame_buffer[c]->P1 + frame_len, 128, frame_len * 4);
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
        "Using %.2f MB RAM in performer for chain %p (memory %s paged to the swap area, %.2f MB pre-allocated for fx-chain)",
            ((float)total_used/1048576.0f),
            p,
            ( mlock_success ? "is not going to be" : "may be" ),
            ((float)tmp1/1048576.0f)
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

    global->A = vj_perform_init_performer(info,0);
    global->B = vj_perform_init_performer(info,1);

    if( info->uc->scene_detection ) {
        vj_el_auto_detect_scenes( info->edit_list, global->A->temp_buffer,info->video_output_width,info->video_output_height, info->uc->scene_detection );
    }

    return 1;
}

static void vj_perform_close_audio(performer_t *p) {
    if( p->lin_audio_buffer_ )
        free(p->lin_audio_buffer_ );
    if( p->audio_silence_ )
        free(p->audio_silence_);
    veejay_memset( p->audio_buffer, 0, sizeof(uint8_t*) * SAMPLE_MAX_EFFECTS );

#ifdef HAVE_JACK
    if(p->top_audio_buffer) free(p->top_audio_buffer);
    if(p->audio_rec_buffer) free(p->audio_rec_buffer);
    if(p->audio_render_buffer) free( p->audio_render_buffer );
    if(p->down_sample_buffer) free( p->down_sample_buffer );
    int i;
    for(i=0; i <= MAX_SPEED; i ++)
    {
        if(p->resample_context[i])
            vj_audio_resample_close( p->resample_context[i] );
        if(p->downsample_context[i])
            vj_audio_resample_close( p->downsample_context[i]);
        p->resample_context[i] = NULL;
        p->downsample_context[i] = NULL;
    }

#endif
    veejay_msg(VEEJAY_MSG_INFO, "Stopped Audio playback task");
}

int vj_perform_init_audio(veejay_t * info)
{
#ifndef HAVE_JACK
    veejay_msg(VEEJAY_MSG_DEBUG, "Jack was not enabled during build, no support for audio");
    return 0;
#else
    int i;
    performer_global_t *global = (performer_global_t*) info->performer;
    performer_t *p = global->A;

    p->top_audio_buffer =
        (uint8_t *) vj_calloc(sizeof(uint8_t) * 8 * RUP8( PERFORM_AUDIO_SIZE ) );
    if(!p->top_audio_buffer)
        return 0;

    p->audio_rec_buffer =
        (uint8_t *) vj_calloc(sizeof(uint8_t) * RUP8( PERFORM_AUDIO_SIZE) );
    if(!p->audio_rec_buffer)
        return 0;

    p->down_sample_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * MAX_SPEED * 8 );
    if(!p->down_sample_buffer)
        return 0;
    p->down_sample_rec_buffer = p->down_sample_buffer + (sizeof(uint8_t) * PERFORM_AUDIO_SIZE * MAX_SPEED * 4);

    p->audio_render_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * MAX_SPEED * 4 );
    if(!p->audio_render_buffer)
        return 0;

    p->lin_audio_buffer_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * PERFORM_AUDIO_SIZE * SAMPLE_MAX_EFFECTS );
    if(!p->lin_audio_buffer_)
        return 0;

    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
        p->audio_buffer[i] = p->lin_audio_buffer_ + (PERFORM_AUDIO_SIZE * i);
    }
 
    p->audio_silence_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * PERFORM_AUDIO_SIZE * SAMPLE_MAX_EFFECTS );
    if(!p->audio_silence_)
        return 0;

    /* 
     * The simplest way to time stretch the audio is to resample it and then playback the waveform at the original sampling frequency
     * This also lowers or raises the pitch, making it just like speeding up or down a tape recording. Perfect!
     */

    for( i = 0; i <= MAX_SPEED; i ++ )
    {
        int out_rate = (info->edit_list->audio_rate * (i+2));
        int down_rate = (info->edit_list->audio_rate / (i+2));
        p->resample_context[i] = vj_av_audio_resample_init(
                    info->edit_list->audio_chans,
                    info->edit_list->audio_chans, 
                    info->edit_list->audio_rate,
                    out_rate,
                    SAMPLE_FMT_S16,
                    SAMPLE_FMT_S16,
                    2,
                    MAX_SPEED,
                    1,
                    1.0
                    );
        if(!p->resample_context[i])
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize audio upsampler for speed %d", i);
            return 0;
        }
        p->downsample_context[i] = vj_av_audio_resample_init(
                    info->edit_list->audio_chans,
                    info->edit_list->audio_chans,
                    info->edit_list->audio_rate,
                    down_rate,
                    SAMPLE_FMT_S16,
                    SAMPLE_FMT_S16,
                    2,
                    MAX_SPEED,
                    1,
                    1.0 );
       
        if(!p->downsample_context[i])
        {
            veejay_msg(VEEJAY_MSG_WARNING, "Cannot initialize audio downsampler for dup %d",i);
            return 0;
        }
    }
    
    return 1;
#endif
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

    munlockall();

    sample_record_free();

    if(global) {
    if (global->preview_buffer){
        if(global->preview_buffer->Y)
            free(global->preview_buffer->Y);
        free(global->preview_buffer);
    }
    if(global->feedback_buffer[0]) {
       free(global->feedback_buffer[0]);
       global->feedback_buffer[0] = NULL;
    }

    vj_perform_free_performer( global->A );
    vj_perform_free_performer( global->B );
    }

    if(info->edit_list && info->edit_list->has_audio)
        vj_perform_close_audio(global->A);

    vj_perform_record_buffer_free(global);

    if( global ) {
        vj_avcodec_stop(global->encoder_,0);
    }
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
            veejay_msg(0, "Audio playback disabled");
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

        veejay_msg(VEEJAY_MSG_DEBUG,"Jack audio playback enabled");
        return 1;
#else
        veejay_msg(VEEJAY_MSG_WARNING, "Jack support not compiled in (no audio)");
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
            8 * 16 * 65535, 
            frame->format);
}

void    vj_perform_send_primary_frame_s2(veejay_t *info, int mcast, int to_mcast_link_id)
{
    int i;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
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

        info->settings->unicast_frame_sender = 0;
    }
    else {
        VJFrame fxframe;

        vj_copy_frame_holder(info->effect_frame1, p->primary_buffer[info->out_buf], &fxframe);

        int data_len = vj_perform_compress_primary_frame_s2( info,&fxframe );
        if( data_len <= 0) {
            return;
        }

        int id = (mcast ? 2: 3);

        if(!mcast) 
        {
            for( i = 0; i < VJ_MAX_CONNECTIONS; i++ ) {
                if( info->rlinks[i] != -1 ) {
                    if(vj_server_send_frame( info->vjs[id], info->rlinks[i], vj_avcodec_get_buf(g->encoder_), data_len, &fxframe )<=0)
                    {
                            _vj_server_del_client( info->vjs[id], info->rlinks[i] );
                    }
                    info->rlinks[i] = -1;
                }   
            }

            info->settings->unicast_frame_sender = 0;
        }
        else
        {       
            if(vj_server_send_frame( info->vjs[id], to_mcast_link_id, vj_avcodec_get_buf(g->encoder_), data_len, &fxframe )<=0)
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
    VJFrame **frames, sample_eff_chain *entry, int e , int c, int n_frame, void *ptr, int playmode) 
{
    performer_global_t *g = (performer_global_t*) info->performer;

    int args[SAMPLE_MAX_PARAMETERS];
    int n_a = 0;
    int is_mixer = 0;
    int rgb = 0;
    
    if(!vje_get_info( e, &is_mixer, &n_a, &rgb)) {
        return;
    }

    veejay_memset( args, 0 , sizeof(args) );
    
    if( playmode == VJ_PLAYBACK_MODE_TAG )
    {
        if(!vj_tag_get_all_effect_args(todo_info->ref, c, args, n_a, n_frame ))
            return;
    }
    else
    {
        if(!sample_get_all_effect_arg( todo_info->ref, c, args, n_a, n_frame))
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

#ifdef HAVE_JACK
static void vj_perform_reverse_audio_frame(veejay_t * info, int len,
                    uint8_t * buf)
{
    int i;
    int bps = info->current_edit_list->audio_bps;
    uint8_t sample[bps];
    int x=len*bps;
    for( i = 0; i < x/2 ; i += bps ) {
        veejay_memcpy(sample,buf+i,bps);    
        veejay_memcpy(buf+i ,buf+(x-i-bps),bps);
        veejay_memcpy(buf+(x-i-bps), sample,bps);
    }
}
#endif

static int vj_perform_use_cached_frame(ycbcr_frame *cached_frame, VJFrame *dst)
{
    veejay_memcpy( dst->data[0], cached_frame->Y, dst->stride[0] *  dst->height );
    veejay_memcpy( dst->data[1], cached_frame->Cb, dst->stride[1] * dst->height );
    veejay_memcpy( dst->data[2], cached_frame->Cr, dst->stride[2] * dst->height );
    //veejay_memcpy( dst->data[3], cached_frame->data[3], cached_frame->stride[3] * cached_frame->height );
    return dst->ssm;
}

static int vj_perform_get_subframe(veejay_t * info, int this_sample_id, int sub_sample,int chain_entry)

{
    int a = this_sample_id;
    int b = sub_sample;
    int sample_a[4];
    int sample_b[4];

    int offset = sample_get_offset(a, chain_entry); 
    int len_b;

    if(sample_get_short_info(b,&sample_b[0],&sample_b[1],&sample_b[2],&sample_b[3])!=0) return -1;

    if(sample_get_short_info(a,&sample_a[0],&sample_a[1],&sample_a[2],&sample_a[3])!=0) return -1;

    if( sample_b[3] == 0 ) 
    {
        return sample_b[0] + offset;
    }

    len_b = sample_b[1] - sample_b[0];

    int max_sfd = sample_get_framedup( b );
    int cur_sfd = sample_get_framedups( b );

    cur_sfd ++;

    if( max_sfd > 0 ) {
        if( cur_sfd > max_sfd )
        {
            cur_sfd = 0;
        }
        if( sub_sample != a ) sample_set_framedups( b , cur_sfd);
        if( cur_sfd != 0 ) {
            return 1;
        }
    }

    /* offset + start >= end */
    if(sample_b[3] >= 0) /* sub sample plays forward */
    {
        offset += sample_b[3]; /* speed */

        if( sample_b[2] == 3 )
            offset = sample_b[0] + ( (int) ( (double) len_b * rand()/RAND_MAX) );
    
        /* offset reached sample end */
        if(  offset > len_b )
        {
            if(sample_b[2] == 2) /* sample is in pingpong loop */
            {
                /* then set speed in reverse and set offset to sample end */
                //offset = sample_b[1] - sample_b[0];
                offset = len_b;
                sample_set_speed( b, (-1 * sample_b[3]) );
                sample_set_offset(a,chain_entry,offset);
                return sample_b[1];
            }
            if(sample_b[2] == 1)
            {
                offset = 0;
            }
            if(sample_b[2] == 0)
            {
                offset = 0; 
                sample_set_speed(b,0);
            }
            if(sample_b[2] == 3 )
                offset = 0;
        }
        sample_set_offset(a,chain_entry,offset);
        return (sample_b[0] + offset);
    }
    else
    {   /* sub sample plays reverse */
        offset += sample_b[3]; /* speed */

        if( sample_b[2] == 3 )
            offset = sample_b[0] + ( (int) ( (double) len_b * rand()/RAND_MAX));

        if (  (sample_b[0] + offset ) <=0  ) // < -(len_b)  )
        {
            /* reached start position */
            if(sample_b[2] == 2)
            {
                //offset = sample_b[1] - sample_b[0];
                offset = 0;
                sample_set_speed( b, (-1 * sample_b[3]));
                sample_set_offset(a,chain_entry,offset);
                return sample_b[0];
            }
            if(sample_b[2] == 1)
            {
                offset = len_b;
                //offset = sample_b[1]; // play from end to start
            }   
            if(sample_b[2]== 0)
            {
                sample_set_speed(b , 0);
                offset = 0; // stop playing
            }
            if(sample_b[2] == 3 )
                offset = 0;
        }
        sample_set_offset(a, chain_entry, offset);

        return (sample_b[0] + offset); //1
    }
    return 0;
}

static int vj_perform_get_subframe_tag(veejay_t * info, int sub_sample, int chain_entry)

{
    int sample[4];
    int offset = sample_get_offset(sub_sample, chain_entry);    
    int len;
    
    if(sample_get_short_info(sub_sample,&sample[0],&sample[1],&sample[2],&sample[3])!=0) return -1;
    
    if( sample[3] == 0 ) 
    {
        return sample[0] + offset;
    }
    
    len = sample[1] - sample[0];
    int max_sfd = sample_get_framedup( sub_sample );
    int cur_sfd = sample_get_framedups( sub_sample );

    cur_sfd ++;

    if( max_sfd > 0 ) {
        if( cur_sfd >= max_sfd )
        {
            cur_sfd = 0;
        }
        sample_set_framedups( sub_sample, cur_sfd);
        if( cur_sfd != 0 )
            return 1;
    }
 
    /* offset + start >= end */
    if(sample[3] >= 0) /* sub sample plays forward */
    {
        offset += sample[3]; /* speed */

        if( sample[2] == 3  )
            offset = sample[0] + ( (int) ( (double) len * rand()/RAND_MAX));
    
        /* offset reached sample end */
        if(  offset > len )
        {
            if(sample[2] == 2) /* sample is in pingpong loop */
            {
                /* then set speed in reverse and set offset to sample end */
                //offset = sample_b[1] - sample_b[0];
                offset = len;
                sample_set_speed( sub_sample, (-1 * sample[3]) );
                sample_set_offset( sub_sample,chain_entry,offset);
                return sample[1];
            }
            if(sample[2] == 1)
            {
                offset = 0;
            }
            if(sample[2] == 0)
            {
                offset = 0; 
                sample_set_speed( sub_sample,0);
            }
            if(sample[2] == 3 )
                offset = 0;
        }

        sample_set_offset(sub_sample,chain_entry,offset);
        return (sample[0] + offset);
    }
    else
    {   /* sub sample plays reverse */
            offset += sample[3]; /* speed */
        
        if( sample[2] == 3  )
            offset = sample[0] + ( (int) ( (double) len * rand()/RAND_MAX));
    
        if ( (sample[0] + offset) <= 0  )
        {
            /* reached start position */
            if(sample[2] == 2)
            {
                //offset = sample_b[1] - sample_b[0];
                offset = 0;
                sample_set_speed( sub_sample, (-1 * sample[3]));
                sample_set_offset( sub_sample,chain_entry,offset);
                return sample[0];
            }
            if(sample[2] == 1)
            {
                offset = len;
            //  offset = sample_b[1];
            }   
            if(sample[2]== 0)
            {
                sample_set_speed( sub_sample , 0);
                offset = 0;
            }
            if(sample[2] == 3 )
                offset = 0;
        }
        sample_set_offset(sub_sample, chain_entry, offset);
    
        return (sample[0] + offset);
    }
    return 0;
}

#define ARRAY_LEN(x) ((int)(sizeof(x)/sizeof((x)[0])))
int vj_perform_fill_audio_buffers(veejay_t * info, uint8_t *audio_buf, uint8_t *temporary_buffer, uint8_t *downsample_buffer, int *sampled_down)
{
#ifdef HAVE_JACK
    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    int len = 0;
    int speed = sample_get_speed(info->uc->sample_id);
    int bps   =  info->current_edit_list->audio_bps;
    int pred_len = (info->current_edit_list->audio_rate / info->current_edit_list->video_fps );
        int rs = 0;
    int n_samples = 0;

    int cur_sfd = sample_get_framedups( info->uc->sample_id );
    int max_sfd = sample_get_framedup( info->uc->sample_id );

    if( cur_sfd <= 0 )
    {
        if (speed > 1 || speed < -1) 
        {
            int a_len = 0;
            int n_frames = abs(speed);
            uint8_t *tmp = temporary_buffer;
            uint8_t *sambuf = tmp;
            long i,start,end;

            if( n_frames >= MAX_SPEED )
                n_frames = MAX_SPEED - 1;

            if( speed < 0 )
            {
                start = settings->current_frame_num - n_frames;
                end   = settings->current_frame_num;
            }
            else
            {
                start = settings->current_frame_num;
                end   = settings->current_frame_num + n_frames;
            }

            for ( i = start; i < end; i ++ )    
            {
                a_len = vj_el_get_audio_frame(info->current_edit_list,i, tmp);
                if( a_len <= 0 )
                {
                    n_samples += pred_len;
                    veejay_memset( tmp, 0, pred_len * bps );
                    tmp += (pred_len*bps);
                }
                else
                {
                    n_samples += a_len;
                    tmp += (a_len* bps );
                    rs = 1;
                }
            }
            
            if( rs )
            {
                if( speed < 0 )
                    vj_perform_reverse_audio_frame(info, n_samples, sambuf );

                int sc = n_frames - 2;
                
                //@ clip sc into range, there isn't a resampler for every speed
                if( sc < 0 ) sc = 0; else if ( sc > MAX_SPEED ) sc = MAX_SPEED;

                n_samples = vj_audio_resample( p->resample_context[sc],(short*) audio_buf, (short*) sambuf, n_samples );
            }
        } else if( speed == 0 ) {
            n_samples = len = pred_len;
            veejay_memset( audio_buf, 0, pred_len * bps );
        } else  {
            n_samples = vj_el_get_audio_frame( info->current_edit_list, settings->current_frame_num, audio_buf );
            if(n_samples <= 0 )
            {
                veejay_memset( audio_buf,0, pred_len * bps );
                n_samples = pred_len;
            }
            else
            {
                rs = 1;
            }
            if( speed < 0 && rs)
                vj_perform_reverse_audio_frame(info,n_samples,audio_buf);
        }
    
        if( n_samples < pred_len )
        {
            veejay_memset( audio_buf + (n_samples * bps ) , 0, (pred_len- n_samples) * bps );
            n_samples = pred_len;
        } 

    }

    if(cur_sfd <= max_sfd && max_sfd > 1) 
    {
        if( cur_sfd == 0 ) {
            int sc = max_sfd - 2;
            if( sc < 0 ) sc = 0; else if( sc > MAX_SPEED ) sc = MAX_SPEED;

            int tds = vj_audio_resample( p->downsample_context[sc], (short*) downsample_buffer, (short*) audio_buf, n_samples );
            *sampled_down = tds / max_sfd;
        }
        
        n_samples = *sampled_down;
        
        veejay_memcpy( audio_buf, downsample_buffer + (cur_sfd * n_samples * bps), n_samples * bps );
    }

    return n_samples;
#else
    return 0;
#endif
}

static int vj_perform_apply_secundary_tag(veejay_t * info, performer_t *p, int sample_id, int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0_ref, uint8_t *p1_ref, int subrender )
{   
    int error = 1;
    int nframe;
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
    
        if(!subrender)  
            cached_frame = vj_perform_tag_is_cached(info,sample_id);
    
        if (cached_frame == NULL || subrender)
        {
            if(! vj_tag_get_active( sample_id ) )
            {
                vj_tag_set_active(sample_id, 1 );
            }
        
            int res = vj_tag_get_frame(sample_id, dst,p->audio_buffer[chain_entry]);
            if(res==1)  {
                 error = 0;
                 ssm = dst->ssm;

                 global->cached_tag_frames[ global->n_cached_tag_frames ].sample_id = sample_id;
                 global->cached_tag_frames[ global->n_cached_tag_frames ].frame = p->frame_buffer[ chain_entry ];
                 global->n_cached_tag_frames ++;
            }
        }
        else
        {   
            ssm = vj_perform_use_cached_frame(cached_frame, dst);
            error = 0;
        }

        break;
    
   case VJ_TAG_TYPE_NONE:
        nframe = vj_perform_get_subframe_tag(info, sample_id, chain_entry);

        sample_set_resume( sample_id, nframe );

        if(!subrender)
            cached_frame = vj_perform_sample_is_cached(info,sample_id);

        if(cached_frame == NULL || subrender )
        {
            len = vj_perform_get_frame_fx( info, sample_id, nframe, src,dst,p0_ref,p1_ref );    
            if(len > 0 ) {
               error = 0;
               ssm = dst->ssm;

               global->cached_sample_frames[ global->n_cached_sample_frames ].sample_id = sample_id;
               global->cached_sample_frames[ global->n_cached_sample_frames ].frame = p->frame_buffer[ chain_entry ];
               global->n_cached_sample_frames ++;
            }
        }
        else
        {
            ssm = vj_perform_use_cached_frame(cached_frame, dst);
            error = 0;
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
                (src->ssm == 1 ? src->len : src->uv_len ),
                (src->ssm == 1 ? src->len : src->uv_len ),
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

static  int vj_perform_get_frame_( veejay_t *info, int s1, long nframe, VJFrame *src, VJFrame *dst, uint8_t *p0_buffer[4], uint8_t *p1_buffer[4], int check_sample )
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
        int res = vj_el_get_video_frame( el, nframe, dst->data );
        if(res) {
            dst->ssm = 0;
        }
        return res;
    }

    int cur_sfd = (s1 ? sample_get_framedups(s1 ) : info->settings->simple_frame_dup );
    int speed = (s1 ? sample_get_speed(s1) : info->settings->current_playback_speed);
    int uv_len = (dst->ssm == 1 ? dst->len : dst->uv_len );
    
    long p0_frame = 0;
    long p1_frame = 0;

    long    start = ( s1 ? sample_get_startFrame(s1) : info->settings->min_frame_num);
    long    end   = ( s1 ? sample_get_endFrame(s1) : info->settings->max_frame_num );

    if( cur_sfd == 0 ) {
        p0_frame = nframe;
        vj_el_get_video_frame( el, p0_frame, p0_buffer );
        p1_frame = nframe + speed;

        if( speed > 0 && p1_frame > end )
            p1_frame = end;
        else if ( speed < 0 && p1_frame < start )
            p1_frame = start;

        if( p1_frame != p0_frame )
            vj_el_get_video_frame( el, p1_frame, p1_buffer );
        
        vj_perform_copy3( p0_buffer, dst->data, dst->len, uv_len,0 );
    } else {
        const uint32_t N = max_sfd;
        const uint32_t n1 = cur_sfd;
        const float frac = 1.0f / (float) N * n1;

        vj_frame_slow_threaded( p0_buffer, p1_buffer, dst->data, dst->len, uv_len, frac );
        
        if( (n1 + 1 ) == N ) {
            vj_perform_copy3( dst->data, p0_buffer, dst->len,uv_len,0);
        }
    }
    
    return 1;
}

static int vj_perform_get_frame_fx(veejay_t *info, int s1, long nframe, VJFrame *src, VJFrame *dst, uint8_t *p0plane, uint8_t *p1plane)
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
    int error = 1;
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
    
            if( !subrender )
                cached_frame = vj_perform_tag_is_cached(info,sample_id); // is it cached?
            //@ not cached
            if (cached_frame == NULL || subrender)
            {
                if(! vj_tag_get_active( sample_id ) )
                {
                    vj_tag_set_active(sample_id, 1 );
                }
        
                res = vj_tag_get_frame(sample_id, dst,p->audio_buffer[chain_entry]);
                if(res) {
                    error = 0; 
                    ssm = dst->ssm;

                    g->cached_tag_frames[ g->n_cached_tag_frames ].sample_id = sample_id;
                    g->cached_tag_frames[ g->n_cached_tag_frames ].frame = p->frame_buffer[ chain_entry ];
                    g->n_cached_tag_frames ++;
                }                   
            }
            else
            {
                ssm = vj_perform_use_cached_frame(cached_frame, dst);
                error = 0;
            }
            
            break;
        
        case VJ_TAG_TYPE_NONE:
                nframe = vj_perform_get_subframe(info,this_sample_id, sample_id, chain_entry); // get exact frame number to decode

            sample_set_resume( sample_id, nframe );

            if(!subrender)
                cached_frame = vj_perform_sample_is_cached(info,sample_id);
                
            if(cached_frame == NULL || subrender) {
                len = vj_perform_get_frame_fx( info, sample_id, nframe, src, dst, p0_ref, p1_ref ); 
                if(len > 0 ) {
                    error = 0;
                    ssm = dst->ssm;
                    g->cached_sample_frames[ g->n_cached_sample_frames ].sample_id = sample_id;
                    g->cached_sample_frames[ g->n_cached_sample_frames ].frame = p->frame_buffer[ chain_entry ];
                    g->n_cached_sample_frames ++;
                }
            }
            else
            {
                ssm = vj_perform_use_cached_frame(cached_frame,dst);  
                error = 0;
            }   
            
            break;
    }

    return ssm;
}

static void vj_perform_tag_render_chain_entry(veejay_t *info,performer_t *p,vjp_kf *setup, int sample_id, int pm, sample_eff_chain *fx_entry,int chain_entry, VJFrame *frames[2], int subrender)
{
    VJFrameInfo *frameinfo;
    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*) info->performer;
    
    frameinfo = info->effect_frame_info;
    
    frames[1]->data[0] = p->frame_buffer[chain_entry]->Y;
    frames[1]->data[1] = p->frame_buffer[chain_entry]->Cb;
    frames[1]->data[2] = p->frame_buffer[chain_entry]->Cr;
    frames[1]->data[3] = p->frame_buffer[chain_entry]->alpha; 

    setup->ref = sample_id;

    int effect_id = fx_entry->effect_id;
    int sub_mode = vje_get_subformat(effect_id);
    int ef = vje_get_extra_frame(effect_id);

    vj_perform_supersample(settings,p, frames[0], (ef ? frames[1] : NULL), sub_mode );

    p->frame_buffer[chain_entry]->ssm = frames[0]->ssm;

    if(ef)
    {
        frames[1]->ssm = vj_perform_apply_secundary_tag(info,p,fx_entry->channel,fx_entry->source_type,chain_entry,frames[0],frames[1],p->frame_buffer[chain_entry]->P0, p->frame_buffer[chain_entry]->P1, 0 );

        if( subrender && settings->fxdepth ) {
            frames[1]->ssm = vj_perform_preprocess_secundary( info,p, fx_entry->channel,fx_entry->source_type,sub_mode,chain_entry, frames, frameinfo );
        }

        vj_perform_supersample(settings,p, NULL, frames[1], sub_mode);
    }
    
    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 1) {
        vj_perform_pre_chain( p, frames[0] ); 
    }

    vj_perform_apply_first(info,p,setup,frames,fx_entry,effect_id,chain_entry,(int) settings->current_frame_num,fx_entry->fx_instance,pm);
    settings->fxrow[ chain_entry ] = effect_id;
    
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
    performer_global_t *g = (performer_global_t*) info->performer;

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

                vj_perform_supersample( settings,p, subframes[0], subframes[1], sm );

                vj_perform_apply_first(info,p,&setup,subframes,fx_entry,fx_id,n,(int) settings->current_frame_num,fx_entry->fx_instance, mode );
                settings->fxrow[n] = fx_id;
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
               
                vj_perform_supersample(settings,p, subframes[0],subframes[1], sm);

                vj_perform_apply_first(info,p,&setup,subframes,fx_entry,fx_id,n,(int) settings->current_frame_num,fx_entry->fx_instance, mode );
                settings->fxrow[n] = fx_id; 
            }
            break;
    }

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 4 ) {
        vj_perform_pre_chain( p, &top );
    }

    return top.ssm;
}

static void vj_perform_render_chain_entry(veejay_t *info,performer_t *p, vjp_kf *setup, int sample_id, int pm, sample_eff_chain *fx_entry, int chain_entry, VJFrame *frames[2], int subrender)
{
    VJFrameInfo *frameinfo;
    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*) info->performer;
    
    frameinfo = info->effect_frame_info;
    
    frames[1]->data[0] = p->frame_buffer[chain_entry]->Y;
    frames[1]->data[1] = p->frame_buffer[chain_entry]->Cb;
    frames[1]->data[2] = p->frame_buffer[chain_entry]->Cr;
    frames[1]->data[3] = p->frame_buffer[chain_entry]->alpha;

    setup->ref = sample_id;

    int effect_id = fx_entry->effect_id;
    int sub_mode = vje_get_subformat(effect_id);
    int ef = vje_get_extra_frame(effect_id);

    vj_perform_supersample(settings,p, frames[0], NULL, sub_mode);

    p->frame_buffer[chain_entry]->ssm = frames[0]->ssm;

    if(ef)
    {
        frames[1]->ssm = vj_perform_apply_secundary(info,p,sample_id,fx_entry->channel,fx_entry->source_type,chain_entry,frames[0],frames[1],p->frame_buffer[chain_entry]->P0, p->frame_buffer[chain_entry]->P1, 0);
        
        if( subrender && settings->fxdepth) {
            frames[1]->ssm = vj_perform_preprocess_secundary(info,p,fx_entry->channel,fx_entry->source_type,sub_mode,chain_entry,frames,frameinfo );
        }

        vj_perform_supersample(settings,p, NULL, frames[1], sub_mode);
    }

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 1) {
        vj_perform_pre_chain( p,frames[0] ); 
    }

    vj_perform_apply_first(info,p,setup,frames,fx_entry,effect_id,chain_entry,
            (int) settings->current_frame_num, fx_entry->fx_instance,pm  );
    
    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 2) {
        vj_perform_pre_chain( p, frames[0] );
    }
}

static int clear_framebuffer__ = 0;

static void vj_perform_sample_complete_buffers(veejay_t * info,performer_t *p, vjp_kf *effect_info, int *hint444, VJFrame *f0, VJFrame *f1, int sample_id, int pm, vjp_kf *setup)
{
    sample_info *si = sample_get(sample_id);
    if(si == NULL)
        return;

    sample_eff_chain **chain = si->effect_chain;
    performer_global_t *g = (performer_global_t*) info->performer;

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

        vj_perform_supersample( info->settings, p, f0, f1, 
               vje_get_subformat( fx_entry->effect_id )
        );

        vj_perform_render_chain_entry(info,p,effect_info,sample_id,pm,fx_entry,chain_entry,frames,(si->subrender? fx_entry->is_rendering: si->subrender));
    }
    *hint444 = frames[0]->ssm;
}

static void vj_perform_tag_complete_buffers(veejay_t * info, performer_t *p,vjp_kf *effect_info, int *hint444, VJFrame *f0, VJFrame *f1, int sample_id, int pm, vjp_kf *setup  )
{
    vj_tag *tag = vj_tag_get( sample_id );
    if(tag == NULL)
        return;

    sample_eff_chain **chain = tag->effect_chain;
    performer_global_t *g = (performer_global_t*) info->performer;
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

static void vj_perform_plain_fill_buffer(veejay_t * info, performer_t *p,VJFrame *dst, int sample_id, long frame_num)
{
    video_playback_setup *settings = (video_playback_setup*)  info->settings;
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

    if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
    {
        vj_perform_get_frame_(info, sample_id, frame_num,&frame,&frame, p0_buffer,p1_buffer,0 );

        g->cached_sample_frames[ g->n_cached_sample_frames ].sample_id = sample_id;
        g->cached_sample_frames[ g->n_cached_sample_frames ].frame = p->primary_buffer[0];
        g->n_cached_sample_frames ++;

    } else if ( info->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN ) {
        vj_perform_get_frame_(info, 0, frame_num,&frame,&frame, p0_buffer, p1_buffer,0 );
    }

    //FIXME error important ?
}

static int rec_audio_sample_ = 0;
static int vj_perform_render_sample_frame(veejay_t *info, performer_t *p, uint8_t *frame[4], int sample, int type)
{
    int audio_len = 0;
    performer_global_t *g = (performer_global_t*) info->performer;

    if( type == 0 && info->audio == AUDIO_PLAY ) {
        if( info->current_edit_list->has_audio )
        {
            audio_len = vj_perform_fill_audio_buffers(info, p->audio_rec_buffer, p->audio_render_buffer + (2* PERFORM_AUDIO_SIZE * MAX_SPEED), p->down_sample_rec_buffer, &rec_audio_sample_ );
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
    video_playback_setup *s = v->settings;

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
    uint8_t *frame[4];
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
    int type = p->pvar_.type;
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
            g->cached_tag_frames[ g->n_cached_tag_frames ].sample_id = sample_id;
            g->cached_tag_frames[ g->n_cached_tag_frames].frame = p->primary_buffer[0];
            g->n_cached_tag_frames ++;
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

static  inline  void    vj_perform_supersample_chain( performer_t *p, subsample_mode_t sample_mode, VJFrame *frame )
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
    performer_global_t *g = (performer_global_t*) info->performer;
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
    performer_global_t *g = (performer_global_t*) info->performer;
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
int vj_perform_queue_audio_frame(veejay_t *info)
{
    if( info->audio == NO_AUDIO )
        return 1;
#ifdef HAVE_JACK
    editlist *el = info->current_edit_list;
    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    if( settings->audio_mute || !el->has_audio || settings->current_playback_speed == 0) {
        int num_samples = (info->edit_list->audio_rate / info->edit_list->video_fps);
        int bps = info->edit_list->audio_bps;
        veejay_memset( p->top_audio_buffer, 0, num_samples * bps);
        vj_perform_play_audio( settings, p->top_audio_buffer, (num_samples * bps ), p->audio_silence_);
        return 1;
    }

    long this_frame = settings->current_frame_num;
    int num_samples =  (el->audio_rate/el->video_fps);
    int pred_len = num_samples;
    int bps     =   el->audio_bps;
    uint8_t *a_buf = p->top_audio_buffer;
    if (info->audio == AUDIO_PLAY)
    {
        switch (info->uc->playback_mode)
        {
            case VJ_PLAYBACK_MODE_SAMPLE:
                if( el->has_audio )
                    num_samples = vj_perform_fill_audio_buffers(info,a_buf, p->audio_render_buffer, p->down_sample_buffer, &(p->play_audio_sample_));

                //  num_samples = vj_perform_fill_audio_buffers(info,a_buf, audio_render_buffer + (2* PERFORM_AUDIO_SIZE * MAX_SPEED), down_sample_buffer, &play_audio_sample_);
                if(num_samples <= 0 )
                {
                    num_samples = pred_len;
                    veejay_memset( a_buf, 0, num_samples * bps );
                }
                break;
            case VJ_PLAYBACK_MODE_PLAIN:
                if( el->has_audio )
                {
                    int rs = 0;
                    if (settings->current_frame_num <= settings->max_frame_num) 
                        num_samples =   vj_el_get_audio_frame(el, this_frame,a_buf );
                    else
                        num_samples = 0;
                    if( num_samples <= 0 )
                    {
                        num_samples = pred_len;
                        veejay_memset( a_buf, 0, num_samples * bps );
                    }
                    else
                        rs = 1;

                    if( settings->current_playback_speed < 0 && rs )
                        vj_perform_reverse_audio_frame(info,num_samples,a_buf);
                }
                        break;
            case VJ_PLAYBACK_MODE_TAG:
                if(el->has_audio)
                {
                        num_samples = vj_tag_get_audio_frame(info->uc->sample_id, a_buf);
                }

                if(num_samples <= 0 )
                {
                    num_samples = pred_len;
                    veejay_memset(a_buf, 0, num_samples * bps );
                }
                break;
        }

        vj_jack_continue( settings->current_playback_speed );
        
        vj_perform_play_audio( settings, a_buf, (num_samples * bps ), p->audio_silence_);
     }  
#endif
    return 1;
}

int vj_perform_get_width( veejay_t *info )
{
    return info->video_output_width;
}

int vj_perform_get_height( veejay_t *info )
{
    return info->video_output_height;
}

static  char    *vj_perform_print_credits( veejay_t *info ) 
{
    char text[1024];
    snprintf(text, 1024,"This is Veejay version %s\n%s\n%s\n%s\n",VERSION,intro,copyr,license);
    return vj_strdup(text);
}

static  char    *vj_perform_osd_status( veejay_t *info )
{
    video_playback_setup *settings = info->settings;
    char *more = NULL;
    if(info->composite ) {
        void *vp = composite_get_vp( info->composite );
        if(viewport_get_mode(vp)==1 ) {
            more = viewport_get_my_status( vp );
        }
    }
    MPEG_timecode_t tc;
    char timecode[64];
    char buf[256];
    veejay_memset(&tc,0,sizeof(MPEG_timecode_t));
        y4m_ratio_t ratio = mpeg_conform_framerate( (double)info->current_edit_list->video_fps );
        int n = mpeg_framerate_code( ratio );

        mpeg_timecode(&tc, settings->current_frame_num, n, info->current_edit_list->video_fps );

        snprintf(timecode, sizeof(timecode), "%2d:%2.2d:%2.2d:%2.2d",
                tc.h, tc.m, tc.s, tc.f );

    char *extra = NULL;
    
    switch(info->uc->playback_mode) {
        case VJ_PLAYBACK_MODE_SAMPLE:
            snprintf(buf,256, "%s %d of %d Cache=%dMb Cost=%dms",
                    timecode,
                    info->uc->sample_id,
                    sample_size(),
                    sample_cache_used(0),
                    info->real_fps );
            break;
        case VJ_PLAYBACK_MODE_TAG:
            snprintf(buf,256, "%s %d of %d Cost=%dms",
                    timecode,
                    info->uc->sample_id,
                    vj_tag_size(),
                    info->real_fps );
            break;
        default:    
            snprintf(buf,256, "(P) %s", timecode );
            break;
    }

    int total_len = strlen(buf) + ( extra == NULL ? 0 : strlen(extra)) + 1;
    if( more )
        total_len += strlen(more);
    char *status_str = (char*) malloc(sizeof(char) * total_len);
    if( extra && more )
        snprintf(status_str,total_len,"%s %s %s",more, buf,extra);
    else if ( more ) 
        snprintf(status_str, total_len, "%s %s", more,buf );
    else
        strncpy( status_str, buf, total_len );

    if(extra)
        free(extra);
    if(more)
        free(more);

    return status_str;
}

static  void    vj_perform_render_osd( veejay_t *info, video_playback_setup *settings,VJFrame *frame )
{
    if(info->use_osd <= 0 ) 
        return;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

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

static  void    vj_perform_finish_chain( veejay_t *info,performer_t *p,VJFrame *frame, int sample_id, int source_type )
{
    performer_global_t *g = (performer_global_t*) info->performer;
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
    performer_global_t *g = (performer_global_t*) info->performer;

    pri[0] = p->primary_buffer[0]->Y;
    pri[1] = p->primary_buffer[0]->Cb;
    pri[2] = p->primary_buffer[0]->Cr;
    pri[3] = p->primary_buffer[0]->alpha;

  //  vj_perform_transition_sample( info, frame );


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

    //FIXME: refactor this 
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

        // draw font/qr in transformed image
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
        if(info->use_osd == 2 ){
            /* draw qr picture if present */
            qrwrap_draw( frame, info->uc->port, info->homedir, frame->height/4,frame->height/4, frame->format );
        }
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
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
/*
    frame->data[0] = p->primary_buffer[0]->Y;
    frame->data[1] = p->primary_buffer[0]->Cb;
    frame->data[2] = p->primary_buffer[0]->Cr;
*/
#ifdef HAVE_FREETYPE
    int n = vj_font_norender( info->font, settings->current_frame_num );
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
        vj_font_render( info->font, frame , settings->current_frame_num );
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
    settings->transition.next_id = 0;
    settings->transition.next_type = 0;
    settings->transition.shape = -1;
    settings->transition.start = 0;
    settings->transition.end = 0;
    settings->transition.active = 0;
}

static void vj_perform_end_transition( veejay_t *info, int mode, int sample )
{
    video_playback_setup *settings = info->settings;
    int type = 0;
    if(settings->transition.ready) {
        int next_slot = 0;
        if(info->seq->active) {
          vj_perform_next_sequence( info, &type,&next_slot );
        }

        vj_perform_reset_transition(info);

        veejay_change_playback_mode( info, mode, sample );
        if(info->seq->active) {
            info->seq->current = next_slot;
        }
        settings->transition.ready = 0;
    }

}

static int vj_perform_transition_get_sample_position(int sample_id)
{
    int sample_b[4];

    if(sample_get_short_info(sample_id,&sample_b[0],&sample_b[1],&sample_b[2],&sample_b[3])!=0) {
        return 0;
    }
    int position = sample_get_position( sample_id );

    if( sample_b[3] == 0 ) {
        return sample_b[0] + position;
    }
    
    int len_b = sample_b[1] - sample_b[0];

    // duplicate frames
    int max_sfd = sample_get_framedup( sample_id );
    if(max_sfd > 0 ) {
        int cur_sfd = sample_get_framedups( sample_id );

        cur_sfd = cur_sfd + 1;

        if( cur_sfd > max_sfd )
        {
            cur_sfd = 0;
        }

        sample_set_framedups( sample_id, cur_sfd);
    }

    /* offset + start >= end */
    if(sample_b[3] >= 0) /* sub sample plays forward */
    {
        position += sample_b[3]; /* speed */

        if( sample_b[2] == 3 )
            position = sample_b[0] + ( (int) ( (double) len_b * rand()/RAND_MAX) );
    
        /* offset reached sample end */
        if(  position > len_b )
        {
            if(sample_b[2] == 2) /* sample is in pingpong loop */
            {
                /* then set speed in reverse and set offset to sample end */
                //offset = sample_b[1] - sample_b[0];
                position = len_b;
                sample_set_speed( sample_id, (-1 * sample_b[3]) );
                sample_update_offset( sample_id, position );
                return sample_b[1];
            }
            if(sample_b[2] == 1)
            {
                position = 0;
            }
            if(sample_b[2] == 0)
            {
                position = 0; 
                sample_set_speed(sample_id,0);
            }
            if(sample_b[2] == 3 )
                position = 0;
        }
        sample_update_offset(sample_id, position);
        return (sample_b[0] + position);
    }

    /* sub sample plays reverse */
    position += sample_b[3]; /* speed */

    if( sample_b[2] == 3 )
        position = sample_b[0] + ( (int) ( (double) len_b * rand()/RAND_MAX));

    if (  (sample_b[0] + position ) <=0  ) // < -(len_b)  )
    {
        /* reached start position */
        if(sample_b[2] == 2)
        {
            position = 0;
            sample_set_speed( sample_id, (-1 * sample_b[3]));
            sample_update_offset( sample_id, position );
            return sample_b[0];
        }
        if(sample_b[2] == 1)
        {
            position = len_b;
        }   
        if(sample_b[2]== 0)
        {
            sample_set_speed(sample_id , 0);
            position = 0; // stop playing
        }
        if(sample_b[2] == 3 )
            position = 0;
    }
    sample_update_offset(sample_id, position);

    return (sample_b[0] + position); //1
}

int vj_perform_transition_sample( veejay_t *info, VJFrame *srcA, VJFrame *srcB ) {
    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *a = g->A;
    performer_t *b = g->B;

    if( settings->current_playback_speed > 0 ) {
        settings->transition.timecode = 
              (settings->current_frame_num - settings->transition.start) / (double) (settings->transition.end - settings->transition.start);
    }
    else if (settings->current_playback_speed < 0 ) {
        settings->transition.timecode =
            (settings->transition.start - settings->current_frame_num ) / (double) (settings->transition.start - settings->transition.end);
    }

    if( settings->transition.timecode < 0.0 || settings->transition.timecode > 1.0 ) {
        veejay_msg(0, "invalid transition timecode: frame %ld, transition %d - %d", settings->current_frame_num, settings->transition.start, settings->transition.end);
        return 0;
    }

    if( !srcA->ssm ) {
        chroma_supersample( settings->sample_mode,srcA,srcA->data);
        vj_perform_set_444( srcA );
    }
    if( !srcB->ssm ) {
        chroma_supersample( settings->sample_mode,srcB, srcB->data);
        vj_perform_set_444( srcB );
    }

    settings->transition.ready = shapewipe_process(  // TODO: use a function pointer
                                        settings->transition.ptr,
                                        srcA, srcB,
                                        settings->transition.timecode,
                                        settings->transition.shape,
                                        0,
                                        1, // (settings->current_playback_speed > 0 ? 1 : 0),
                                        1
                                  );
    
    vj_perform_end_transition( info, (settings->transition.next_type == 0 ? VJ_PLAYBACK_MODE_SAMPLE: VJ_PLAYBACK_MODE_TAG),settings->transition.next_id );

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

int vj_perform_queue_video_frames(veejay_t *info, VJFrame *frame, VJFrame *frame2, performer_t *p, const int skip_incr, const int sample_id, const int mode, long frame_num)
{
    if(skip_incr)
        return 0;

    sample_eff_chain **fx_chain = NULL;
    veejay_memcpy( p->tmp1, frame, sizeof(VJFrame));
    
    p->tmp1->data[0] = p->primary_buffer[0]->Y; 
    p->tmp1->data[1] = p->primary_buffer[0]->Cb;
    p->tmp1->data[2] = p->primary_buffer[0]->Cr;
    p->tmp1->data[3] = p->primary_buffer[0]->alpha;

    int is_sample = (mode == VJ_PLAYBACK_MODE_SAMPLE);

    if(mode != VJ_PLAYBACK_MODE_TAG ) {
        vj_perform_plain_fill_buffer(info,p, p->tmp1, sample_id, frame_num);
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
                p->tmp2->ssm = 0;
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
    int res = 0;
    int i = 0;
    int safe_ff = p->pvar_.follow_fade;
    int safe_fv = p->pvar_.fade_value;

    p->is_supersampled = 0;
    veejay_memset( &(p->pvar_), 0, sizeof(varcache_t));
    
    p->pvar_.follow_fade = safe_ff;
    p->pvar_.fade_value = safe_fv;
    p->pvar_.fade_entry = -1;

    int cur_out = info->out_buf;

    topinfo->timecode = settings->current_frame_num;
    a->ssm = 0;
    b->ssm = 0;
    a->timecode = settings->current_frame_num /  (double) (settings->max_frame_num - settings->min_frame_num);

    for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
        p->frame_buffer[i]->ssm = -1;
    }

    a->data[0] = p->primary_buffer[0]->Y;       
    a->data[1] = p->primary_buffer[0]->Cb;
    a->data[2] = p->primary_buffer[0]->Cr;
    a->data[3] = p->primary_buffer[0]->alpha;

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
            
            if(p->pvar_.fx_status )
                vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type,setup);
            
            vj_perform_finish_chain( info,p,a,sample_id,source_type );

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

            if(p->pvar_.fx_status )
                vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup);
               
            vj_perform_finish_chain( info,p,a,sample_id,source_type ); 
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

int vj_perform_queue_video_frame(veejay_t *info, const int skip_incr)
{
    if(skip_incr)
        return 0;

    performer_global_t *g = (performer_global_t*) info->performer;

    vj_perform_queue_video_frames( info, info->effect_frame1, info->effect_frame2, g->A, skip_incr, info->uc->sample_id, info->uc->playback_mode, info->settings->current_frame_num);

    int transition_enabled = info->settings->transition.active;
    if(transition_enabled) {
        if(info->settings->current_playback_speed < 0 ) {
            if(info->settings->current_frame_num < info->settings->transition.end || info->settings->current_frame_num > info->settings->transition.start)
                transition_enabled = 0;
        } else {
            if(info->settings->current_frame_num < info->settings->transition.start || info->settings->current_frame_num > info->settings->transition.end )
                transition_enabled = 0;
        }
    }

    if(transition_enabled) {
        
        int sample_position = 0;
        if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
            sample_position = vj_perform_transition_get_sample_position( info->settings->transition.next_id );
        vj_perform_queue_video_frames( info,info->effect_frame3, info->effect_frame4, g->B, skip_incr, info->settings->transition.next_id, info->settings->transition.next_type, sample_position );
        vje_disable_parallel();
    }

    if(!transition_enabled) {
        vj_perform_render_video_frames(info, g->A, info->effect_info, info->uc->sample_id, info->uc->playback_mode, info->effect_frame1, info->effect_frame2, info->effect_frame_info, info->effect_info );
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


    vj_perform_render_font( info, info->settings, info->effect_frame1);

    if(!info->settings->composite)
        vj_perform_render_osd( info, info->settings, info->effect_frame1 );

    vj_perform_finish_render( info, g->A, info->effect_frame1, info->settings );

    if( info->effect_frame1->ssm == 1 )
    {
        chroma_subsample(info->settings->sample_mode,info->effect_frame1,info->effect_frame1->data); 
        vj_perform_set_422(info->effect_frame1);
        vj_perform_set_422(info->effect_frame2);
        vj_perform_set_422(info->effect_frame3);
        vj_perform_set_422(info->effect_frame4);
    }

    vj_perform_clear_cache(info->performer);
    
    vje_enable_parallel();

    return 1;
}

int vj_perform_queue_frame(veejay_t * info, int skip )
{
    video_playback_setup *settings = (video_playback_setup*) info->settings;
    int ret_val = 0;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
    if( p->pvar_.follow_run ) {
        if( p->pvar_.follow_now[1] == 0 ) {
            info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
            veejay_set_sample( info, p->pvar_.follow_now[0] );
        } else {
            info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
            veejay_set_sample( info, p->pvar_.follow_now[0] );
        }

        p->pvar_.follow_run    = 0;
        p->pvar_.follow_now[0] = 0;
        p->pvar_.follow_now[1] = 0;
    }

    if(!skip)
    {
        int speed = settings->current_playback_speed;
        if( settings->hold_status == 1 ) {
            speed = 0;
            if(settings->hold_pos == 0 ) {
                settings->hold_status = 0;
                long was_at_pos = settings->current_frame_num;
                veejay_increase_frame(info, settings->hold_resume );
                veejay_msg(VEEJAY_MSG_DEBUG, "Reached hold position, skip from frame %ld to frame %ld",
                        was_at_pos,
                        settings->current_frame_num );

                if( speed == 0 )
                    speed = settings->current_playback_speed;
            }
            settings->hold_pos --;
        }

        switch(info->uc->playback_mode) 
        {
            case VJ_PLAYBACK_MODE_TAG:
                ret_val = vj_perform_increase_tag_frame(info, speed);
                break;
            case VJ_PLAYBACK_MODE_SAMPLE: 
                ret_val = vj_perform_increase_sample_frame(info, speed);
                break;
            case VJ_PLAYBACK_MODE_PLAIN:
                ret_val = vj_perform_increase_plain_frame(info, speed);
                break;
            default:
                break;
        }

        settings->cycle_count[0] ++;
        if( settings->cycle_count[0] == 0 )
            settings->cycle_count[1] ++;
    }

    return ret_val;
}

static int track_dup = 0;
void    vj_perform_randomize(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    if(settings->randplayer.mode == RANDMODE_INACTIVE)
        return;

    double n_sample = (double) (sample_highest_valid_id());

    if( settings->randplayer.mode == RANDMODE_SAMPLE )
    track_dup = info->uc->sample_id;

    if( n_sample == 1.0 )
        track_dup = 0;

    int take_n   = 1 + (int) (n_sample * rand() / (RAND_MAX+1.0));
    int min_delay = 1;
    int max_delay = 0;
    int use = ( take_n == track_dup ? 0: 1 );

    if(take_n == 1 && !sample_exists(take_n)) {
        veejay_msg(0,"No samples to randomize");
        settings->randplayer.mode = RANDMODE_INACTIVE;
        return;
    }

    while(!sample_exists(take_n)  || !use)
    {
        veejay_msg(VEEJAY_MSG_DEBUG, 
         "Sample to play (at random) %d does not exist",
            take_n);
        take_n = 1 + (int) ( n_sample * rand()/(RAND_MAX+1.0));
        use = (track_dup == take_n ? 0:1 );
    }

    int start,end;
    start = sample_get_startFrame( take_n);
    end   = sample_get_endFrame( take_n );
    
    if( settings->randplayer.timer == RANDTIMER_FRAME )
    {
        max_delay = (end-start) + 1;
        max_delay = min_delay + (int) ((double)max_delay * rand() / (RAND_MAX+1.0));
    }
    else
    {
        max_delay = (end-start);    
    }

    settings->randplayer.max_delay = max_delay;
    settings->randplayer.min_delay = min_delay; 

    veejay_msg(VEEJAY_MSG_INFO, "Sample randomizer trigger in %d frame periods", max_delay);

    veejay_change_playback_mode( info, VJ_PLAYBACK_MODE_SAMPLE, take_n);
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

