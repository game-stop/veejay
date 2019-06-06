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
#include <libsample/sampleadm.h>  
#include <libstream/vj-tag.h>
#include <libvjnet/vj-server.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-lib.h>
#include <libel/vj-el.h>
#include <math.h>
#include <libel/vj-avcodec.h>
#include <veejay/vj-event.h>
#include <mjpegtools/mpegconsts.h>
#include <mjpegtools/mpegtimecode.h>
#include <libyuv/yuvconv.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vj-perform.h>
#include <veejay/libveejay.h>
#include <libsamplerec/samplerecord.h>
#include <libel/pixbuf.h>
#include <libel/avcommon.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-task.h>
#include <liblzo/lzo.h>
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
#include <libvjmem/vjmem.h>
#include <libvje/effects/opacity.h>
#include <libvje/effects/masktransition.h>
#include <libqrwrap/qrwrapper.h>
#include <veejay/vj-split.h>
#include <libresample/resample.h>

#define PERFORM_AUDIO_SIZE 16384
#define PSLOW_A 3
#define PSLOW_B 4

#ifndef SAMPLE_FMT_S16
#define SAMPLE_FMT_S16 AV_SAMPLE_FMT_S16
#endif

#define PRIMARY_FRAMES 5

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
static long performer_frame_size_ = 0;

extern uint8_t pixel_Y_lo_;

static varcache_t pvar_;
static VJFrame *crop_frame = NULL;
static VJFrame *rgba_frame[2] = { NULL };
static VJFrame *yuva_frame[2] = { NULL };
static VJFrame *yuv420_frame[2] = { NULL };
static ycbcr_frame **video_output_buffer = NULL; /* scaled video output */
static int  video_output_buffer_convert = 0;
static ycbcr_frame **frame_buffer = NULL;   /* chain */
static ycbcr_frame **primary_buffer = NULL; /* normal */
static ycbcr_frame *preview_buffer = NULL;
static int preview_max_w;
static int preview_max_h;
static void *encoder_ = NULL;

#define CACHE_TOP 0
#define CACHE 1
#define CACHE_SIZE (SAMPLE_MAX_EFFECTS+CACHE)
static int cached_tag_frames[CACHE_SIZE];   /* cache a frame into the buffer only once */
static int cached_sample_frames[CACHE_SIZE];
static int frame_info[64][SAMPLE_MAX_EFFECTS];  /* array holding frame lengths  */
static uint8_t *audio_buffer[SAMPLE_MAX_EFFECTS];   /* the audio buffer */
static uint8_t *audio_silence_ = NULL;
static uint8_t *lin_audio_buffer_ = NULL;
static uint8_t *top_audio_buffer = NULL;
static int play_audio_sample_ = 0;
static uint8_t *audio_rec_buffer = NULL;
static uint8_t *audio_render_buffer = NULL;
static uint8_t *down_sample_buffer = NULL;
static uint8_t *down_sample_rec_buffer = NULL;
static uint8_t *temp_buffer[4];
static uint8_t temp_ssm = 0;
static uint8_t *subrender_buffer[4];
static uint8_t *feedback_buffer[4];
static VJFrame feedback_frame;
static uint8_t *rgba_buffer[2];
static void *rgba2yuv_scaler = NULL;
static void *yuv2rgba_scaler = NULL;
static void *yuv420_scaler = NULL;
static uint8_t *pribuf_area = NULL;
static size_t pribuf_len = 0;
static uint8_t *fx_chain_buffer = NULL;
static  size_t fx_chain_buflen = 0;
static ycbcr_frame *record_buffer = NULL;   // needed for recording invisible streams
static VJFrame *helper_frame = NULL;
static int vj_perform_record_buffer_init();
static void vj_perform_record_buffer_free();
static void *resample_context[(MAX_SPEED+1)];
static void *downsample_context[(MAX_SPEED+1)];
static uint32_t is_supersampled = 0;

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
static  int vj_perform_try_transition( veejay_t *info, int is_tag );
static  int vj_perform_preprocess_secundary( veejay_t *info, int id, int mode,int current_ssm,int chain_entry, VJFrame **frames, VJFrameInfo *frameinfo );
static int vj_perform_get_frame_fx(veejay_t *info, int s1, long nframe, VJFrame *src, VJFrame *dst, uint8_t *p0plane, uint8_t *p1plane);
static void vj_perform_pre_chain(veejay_t *info, VJFrame *frame);
static int vj_perform_post_chain_sample(veejay_t *info, VJFrame *frame);
static int vj_perform_post_chain_tag(veejay_t *info, VJFrame *frame);
static int vj_perform_next_sequence( veejay_t *info, int *type );
static void vj_perform_plain_fill_buffer(veejay_t * info, int *result);
static void vj_perform_tag_fill_buffer(veejay_t * info);
static void vj_perform_clear_cache(void);
static int vj_perform_increase_tag_frame(veejay_t * info, long num);
static int vj_perform_increase_plain_frame(veejay_t * info, long num);
static int vj_perform_apply_secundary_tag(veejay_t * info, int sample_id,int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0, uint8_t *p1, int subrender);
static int vj_perform_apply_secundary(veejay_t * info, int this_sample_id,int sample_id,int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0, uint8_t *p1, int subrender);
static int vj_perform_tag_complete_buffers(veejay_t * info, int *h);
static int vj_perform_increase_sample_frame(veejay_t * info, long num);
static int vj_perform_sample_complete_buffers(veejay_t * info, int *h);
static int vj_perform_use_cached_ycbcr_frame(veejay_t *info, int centry, VJFrame *dst,int chain_entry);
static int vj_perform_apply_first(veejay_t *info, vjp_kf *todo_info, VJFrame **frames, VJFrameInfo *frameinfo, int e, int c, int n_frames, void *ptr, int playmode );
static int vj_perform_render_sample_frame(veejay_t *info, uint8_t *frame[4], int sample, int type);
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4]);
static int vj_perform_record_commit_single(veejay_t *info);
static int vj_perform_get_subframe(veejay_t * info, int this_sample_id, int sub_sample,int chain_entyr );
static int vj_perform_get_subframe_tag(veejay_t * info, int sub_sample,int chain_entry );
#ifdef HAVE_JACK
static void vj_perform_reverse_audio_frame(veejay_t * info, int len, uint8_t *buf );
#endif


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
    frame->format = PIX_FMT_YUVJ444P;
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
    frame->format = PIX_FMT_YUVJ422P;
    frame->ssm = 0;
}

static void vj_perform_supersample(video_playback_setup *settings, VJFrame *one, VJFrame *two, int sm)
{
    if(is_supersampled) {
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
        is_supersampled = 1;
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
static inline void vj_perform_play_audio( video_playback_setup *settings, uint8_t *source, int len )
{
    // if auto mute is enabled, play muted data 
    // audio_silence_ is allocated once and played in-place of, thus recording chain is not affected by auto-mute
    if( settings->auto_mute ) {
        vj_jack_play( audio_silence_, len );
    } else {
        vj_jack_play( source, len );
    }

}
#endif

static int vj_perform_tag_is_cached(veejay_t *info, int chain_entry, int tag_id)
{
    int c;
    int res = -1;

    if( cached_tag_frames[0] == tag_id ) 
        return 0;

    for(c=0; c < CACHE_SIZE; c++)
    {
        if(cached_tag_frames[c] == tag_id) 
        {
            if( info->settings->feedback && info->uc->sample_id == tag_id &&
                        info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
                return -1; //@ feedback cannot be cached source
            }
            res = c;
            break;  
        }
    }

    return res;
}
static int vj_perform_sample_is_cached(veejay_t *info,int sample_id, int chain_entry)
{
    int c;
    int res = -1;

    if( sample_id == cached_sample_frames[0] )  
        return 0;

    for(c=1; c < CACHE_SIZE ; c++)
    {
        if(cached_sample_frames[c] == sample_id) 
        {
        if( info->settings->feedback && info->uc->sample_id == sample_id  &&
                    info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
            return -1;
        }
        res = c;
        break;
        }
    }
    return res;
}

static void vj_perform_clear_cache()
{
    veejay_memset(cached_tag_frames, 0 , sizeof(cached_tag_frames));
    veejay_memset(cached_sample_frames, 0, sizeof(cached_sample_frames));
}

static int vj_perform_increase_tag_frame(veejay_t * info, long num)
{
    video_playback_setup *settings = info->settings;
    settings->current_frame_num += num;
 
    if (settings->current_frame_num >= settings->max_frame_num) {
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

    if( info->seq->active ) {
         if( settings->current_frame_num >= settings->max_frame_num ) {
         	settings->current_frame_num = settings->min_frame_num;
            int type = 0;
            int n = vj_perform_next_sequence( info, &type );
            if( n > 0 )
                veejay_change_playback_mode(info,(type == 0 ? VJ_PLAYBACK_MODE_SAMPLE: VJ_PLAYBACK_MODE_TAG),n );
        }
    } else {
		if( settings->current_frame_num >= settings->max_frame_num ) {
        	settings->current_frame_num = settings->min_frame_num;
    
        	if( vj_perform_try_transition(info,1) ) {
            	return 0;
        	}
		}
    }

    if (settings->current_frame_num >= settings->max_frame_num) {
        settings->current_frame_num = settings->min_frame_num;
    }

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

static  int vj_perform_next_sequence( veejay_t *info, int *type )
{

    if( info->seq->samples[info->seq->current].type == 0 &&
        info->seq->samples[info->seq->current].sample_id > 0 ) {
    
        if(sample_loop_dec(info->seq->samples[info->seq->current].sample_id) != 0) /* loop count zero, select next sequence */
            return 0;
        veejay_prepare_sample_positions( info->seq->samples[info->seq->current].sample_id );
    }


    int cur = info->seq->current + 1;
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

    info->seq->current = cur;

    if( info->seq->samples[info->seq->current].type == 0 ) {
        sample_update_ascociated_samples( info->seq->samples[cur].sample_id );
        sample_reset_offset( info->seq->samples[cur].sample_id );
        sample_set_loops( info->seq->samples[cur].sample_id, -1 ); /* reset loop count */
    }
    else {
        vj_tag_reset_offset( info->seq->samples[cur].sample_id );
    }
    *type = info->seq->samples[cur].type;
    return info->seq->samples[cur].sample_id;
}

static  int vj_perform_try_transition( veejay_t *info, int is_tag )
{
    for(int c = 0; c < SAMPLE_MAX_EFFECTS; c ++ ) { 
        int type = 0;

        if( !is_tag ) {
            int id = sample_chain_entry_transition_now( info->uc->sample_id, c, &type );
            if( id == 0 )
                continue;
            veejay_prepare_sample_positions( info->uc->sample_id );

            sample_update_ascociated_samples( id );
            sample_reset_offset( id );
            sample_set_loops( id, -1 );
            veejay_change_playback_mode( info, (type == 0 ? VJ_PLAYBACK_MODE_SAMPLE : VJ_PLAYBACK_MODE_TAG), id );
        
            return 1;
        }
        else {
            int id = vj_tag_chain_entry_transition_now(info->uc->sample_id,c, &type);
            if( id == 0 )
                continue;
            vj_tag_reset_offset(id);
            veejay_change_playback_mode( info, (type == 0 ? VJ_PLAYBACK_MODE_SAMPLE : VJ_PLAYBACK_MODE_TAG), id );
            return 1;
        }   
    }

    return 0;
}

static  int vj_perform_try_sequence( veejay_t *info )
{
    if(!info->seq->active && vj_perform_try_transition(info,0) ) {
        return 1; /* transition not compatible with sequencer */
    }

    if(! info->seq->active )
        return 0;

    int type = 0;
    int n = vj_perform_next_sequence( info, &type );
    if( n > 0 )
    {
        veejay_msg(VEEJAY_MSG_INFO, "Sequence play selects %s %d", (type == 0 ? "sample" : "stream" ) , info->seq->samples[info->seq->current].sample_id);
        
        veejay_change_playback_mode( info, 
                info->seq->samples[ info->seq->current ].type == 0 ? VJ_PLAYBACK_MODE_SAMPLE : VJ_PLAYBACK_MODE_TAG,
                info->seq->samples[ info->seq->current ].sample_id);

        return 1;
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
        //  settings->current_frame_num += num;
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
                    if(!vj_perform_try_sequence( info ) )
                    {
                        veejay_set_frame(info, end);
                        veejay_set_speed(info, (-1 * speed));
                    }
                    break;
                case 1:
                    sample_set_loop_stats(info->uc->sample_id, -1);
                    if(!vj_perform_try_sequence(info) ) {
                        veejay_set_frame(info, start);
                    }
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
                    if(!vj_perform_try_sequence(info) )
                    {
                        veejay_set_frame(info, start);
                        veejay_set_speed(info, (-1 * speed));
                    }
                    break;
                case 1:
                    sample_set_loop_stats(info->uc->sample_id, -1);
                    if(!vj_perform_try_sequence(info)) {
                        veejay_set_frame(info, end);
                    }
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

static long vj_perform_alloc_row(veejay_t *info, int c, int plane_len)
{
    if( fx_chain_buffer )
        return 1;

    size_t frame_len = RUP8( ((plane_len+helper_frame->width)/7)*8 );
    size_t buf_len = sizeof(uint8_t) * frame_len * 4 * 3 * sizeof(uint8_t);

    uint8_t *buf = vj_malloc(buf_len);

    if(!buf)
        return 0;

    mlock( buf, frame_len * buf_len);

    frame_buffer[c]->Y = buf;
    frame_buffer[c]->Cb = frame_buffer[c]->Y + frame_len;
    frame_buffer[c]->Cr = frame_buffer[c]->Cb + frame_len;
    frame_buffer[c]->alpha = frame_buffer[c]->Cr + frame_len;
    frame_buffer[c]->P0  = buf + (frame_len * 4);
    frame_buffer[c]->P1  = frame_buffer[c]->P0 + (frame_len*4);

    return buf_len;
}

static void vj_perform_free_row(int c)
{
    if( fx_chain_buffer )
        return;
    
    size_t frame_len = RUP8( (((helper_frame->len)+helper_frame->width)/7)*8 );

    if(frame_buffer[c]->Y)
    {
        munlock( frame_buffer[c]->Y, frame_len * 4 * 3 * sizeof(uint8_t));
        free( frame_buffer[c]->Y );
    }
    frame_buffer[c]->Y = NULL;
    frame_buffer[c]->Cb = NULL;
    frame_buffer[c]->Cr = NULL;
    frame_buffer[c]->alpha = NULL;
    frame_buffer[c]->P0 = NULL;
    frame_buffer[c]->P1 = NULL;
    cached_sample_frames[c+1] = 0;
    cached_tag_frames[c+1] = 0;
}

#define vj_perform_row_used(c) ( frame_buffer[c]->Y == NULL ? 0 : 1 )
static int  vj_perform_verify_rows(veejay_t *info )
{
    if( pvar_.fx_status == 0 )
        return 0;

    if( fx_chain_buffer )
        return 1;
    
    int c,v,has_rows = 0;
    const int w = info->video_output_width;
    const int h = info->video_output_height;
    for(c=0; c < SAMPLE_MAX_EFFECTS; c++)
    {
        v = (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ? 
            sample_get_effect_any(info->uc->sample_id,c) : vj_tag_get_effect_any(info->uc->sample_id,c));
    
        if( v > 0)
        {
            if( !vj_perform_row_used(c))
            {
                if( vj_perform_alloc_row( info, c, w * h ) <= 0 ) {
                    veejay_msg(0, "Unable to allocate memory for FX entry %d",c );
                    veejay_change_state( info, LAVPLAY_STATE_STOP );
                    return -1;
                }
            }
            has_rows ++;
        }
        else
        {
            if(vj_perform_row_used(c))
                vj_perform_free_row(c); 
        }
    }
    return has_rows;
}


static int vj_perform_record_buffer_init()
{
    if(record_buffer->Cb==NULL)
            record_buffer->Cb = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(helper_frame->uv_len) );
    if(!record_buffer->Cb) return 0;
    if(record_buffer->Cr==NULL)
            record_buffer->Cr = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(helper_frame->uv_len) );
    if(!record_buffer->Cr) return 0;

    if(record_buffer->Y == NULL)
        record_buffer->Y = (uint8_t*)vj_malloc(sizeof(uint8_t) * RUP8(helper_frame->len));
    if(!record_buffer->Y) return 0;

    veejay_memset( record_buffer->Y , pixel_Y_lo_, helper_frame->len );
    veejay_memset( record_buffer->Cb, 128, helper_frame->uv_len );
    veejay_memset( record_buffer->Cr, 128, helper_frame->uv_len );

    return 1;
}

static void vj_perform_record_buffer_free()
{
    if(record_buffer) {
        if(record_buffer->Y) {
            free(record_buffer->Y);
            record_buffer->Y = NULL;
        }
        if(record_buffer->Cb) {
            free(record_buffer->Cb);
            record_buffer->Cb = NULL;
        }
        if(record_buffer->Cr) {
            free(record_buffer->Cr);
            record_buffer->Cr = NULL;
        }
        free(record_buffer);
        record_buffer = NULL;
    }
}

int vj_perform_init(veejay_t * info)
{
    const int w = info->video_output_width;
    const int h = info->video_output_height;

    const long frame_len = RUP8( ((w*h)+w+w) );
    unsigned int c;
    long total_used = 0;
    performer_frame_size_ = frame_len * 4;

    // buffer used to store encoded frames (for plain and sample mode)
    frame_buffer = (ycbcr_frame **) vj_calloc(sizeof(ycbcr_frame*) * SAMPLE_MAX_EFFECTS);
    if(!frame_buffer) return 0;

    record_buffer = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame) );
    if(!record_buffer) return 0;

    primary_buffer = (ycbcr_frame **) vj_calloc(sizeof(ycbcr_frame **) * PRIMARY_FRAMES); 
    if(!primary_buffer) return 0;

    size_t buf_len = performer_frame_size_ * sizeof(uint8_t);
    int mlock_success = 1;

    pribuf_len = PRIMARY_FRAMES * performer_frame_size_;
    pribuf_area = vj_hmalloc( pribuf_len, "in primary buffers" );
    if( !pribuf_area ) {
        return 0;
    }
    
    for( c = 0; c < PRIMARY_FRAMES; c ++ )
    {
        primary_buffer[c] = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
        primary_buffer[c]->Y = pribuf_area + (performer_frame_size_ * c);
        primary_buffer[c]->Cb = primary_buffer[c]->Y  + frame_len;
        primary_buffer[c]->Cr = primary_buffer[c]->Cb + frame_len;
        primary_buffer[c]->alpha = primary_buffer[c]->Cr + frame_len;

        veejay_memset( primary_buffer[c]->Y, pixel_Y_lo_,frame_len);
        veejay_memset( primary_buffer[c]->Cb,128,frame_len);
        veejay_memset( primary_buffer[c]->Cr,128,frame_len);
        veejay_memset( primary_buffer[c]->alpha,0,frame_len);
        total_used += buf_len;
    }

    preview_buffer = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
    preview_max_w = w * 2;
    preview_max_h = h * 2;

    preview_buffer->Y = (uint8_t*) vj_calloc( RUP8(preview_max_w * preview_max_h * 2) );

    video_output_buffer_convert = 0;
    video_output_buffer = (ycbcr_frame**) vj_calloc(sizeof(ycbcr_frame*) * 2 );

    if(!video_output_buffer)
        return 0;

    for(c=0; c < 2; c ++ )
    {
        video_output_buffer[c] = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
    }

    sample_record_init(frame_len);
    vj_tag_record_init(w,h);

    temp_buffer[0] = (uint8_t*) vj_malloc( buf_len );
    if(!temp_buffer[0]) {
        return 0;
    }
    temp_buffer[1] = temp_buffer[0] + frame_len;
    temp_buffer[2] = temp_buffer[1] + frame_len;
    temp_buffer[3] = temp_buffer[2] + frame_len;
    if(mlock( temp_buffer[0], buf_len ) != 0 )
        mlock_success = 0;

    rgba_buffer[0] = (uint8_t*) vj_malloc( buf_len * 2 );
    if(!rgba_buffer[0] ) {
        return 0;
    }

    rgba_buffer[1] = rgba_buffer[0] + buf_len;

    if( mlock( rgba_buffer[0], buf_len * 2 ) != 0 )
        mlock_success = 0;

    veejay_memset( rgba_buffer[0], 0, buf_len * 2 );

    subrender_buffer[0] = (uint8_t*) vj_malloc( buf_len * 3 ); //frame, p0, p1
    if(!subrender_buffer[0]) {
        return 0;
    }
    subrender_buffer[1] = subrender_buffer[0] + frame_len;
    subrender_buffer[2] = subrender_buffer[1] + frame_len;
    subrender_buffer[3] = subrender_buffer[2] + frame_len;


    if(mlock( subrender_buffer[0], buf_len ) != 0 )
        mlock_success = 0;

    veejay_memset( subrender_buffer[0], pixel_Y_lo_,frame_len);
    veejay_memset( subrender_buffer[1],128,frame_len);
    veejay_memset( subrender_buffer[2],128,frame_len);
    veejay_memset( subrender_buffer[3],0,frame_len);

    feedback_buffer[0] = (uint8_t*) vj_malloc( buf_len );
    feedback_buffer[1] = feedback_buffer[0] + frame_len;
    feedback_buffer[2] = feedback_buffer[1] + frame_len;
    feedback_buffer[3] = feedback_buffer[2] + frame_len;

    if(mlock( feedback_buffer[c], buf_len )!=0)
        mlock_success = 0;

    veejay_memset( feedback_buffer[0], pixel_Y_lo_,frame_len);
    veejay_memset( feedback_buffer[1],128,frame_len);
    veejay_memset( feedback_buffer[2],128,frame_len);
    veejay_memset( feedback_buffer[3],0,frame_len);

    total_used += buf_len; //temp_buffer
    total_used += buf_len; //feedback_buffer
    total_used += (buf_len * 3); //subrender_buffer
    total_used += (buf_len * 2); //rgb conversion buffer

    helper_frame = (VJFrame*) vj_malloc(sizeof(VJFrame));
    veejay_memcpy(helper_frame, info->effect_frame1, sizeof(VJFrame));
    veejay_memcpy(&feedback_frame, info->effect_frame1, sizeof(VJFrame));
    veejay_memset( &pvar_, 0, sizeof( varcache_t));

    feedback_frame.data[0] = feedback_buffer[0];
    feedback_frame.data[1] = feedback_buffer[1];
    feedback_frame.data[2] = feedback_buffer[2];
    feedback_frame.data[3] = feedback_buffer[3];

    size_t tmp1 = 0;
    if( info->uc->ram_chain ) {
        //allocate fx_chain_buffer
        tmp1 = buf_len * 4 * sizeof(uint8_t) * SAMPLE_MAX_EFFECTS;
        fx_chain_buffer = vj_hmalloc( tmp1, "in fx chain buffers" );
        if(fx_chain_buffer == NULL ) {
            veejay_msg(VEEJAY_MSG_WARNING,"Unable to allocate sufficient memory to keep all FX chain buffers in RAM");
        } else {
            total_used += tmp1;
            fx_chain_buflen = tmp1;
        }
        /*  tmp1 = [ secundary source on entry X ] [ slow motion buffer A ] [ slow motion buffer B ]
         */
    }

     /* allocate space for frame_buffer pointers */
     for (c = 0; c < SAMPLE_MAX_EFFECTS; c++) {
        frame_buffer[c] = (ycbcr_frame *) 
            vj_calloc(sizeof(ycbcr_frame));
         if(!frame_buffer[c]) return 0;

         if(fx_chain_buffer != NULL ) {
                uint8_t *ptr = fx_chain_buffer + (c * frame_len * 4 * 3); // each entry, has 3 frames with 4 planes each
                frame_buffer[c]->Y = ptr;
                frame_buffer[c]->Cb = frame_buffer[c]->Y + frame_len;
                frame_buffer[c]->Cr = frame_buffer[c]->Cb + frame_len;
                frame_buffer[c]->alpha = frame_buffer[c]->Cr + frame_len;

                frame_buffer[c]->P0  = ptr + (frame_len * 4);
                frame_buffer[c]->P1  = frame_buffer[c]->P0 + (frame_len*4);

                veejay_memset( frame_buffer[c]->Y, pixel_Y_lo_,frame_len);
                veejay_memset( frame_buffer[c]->Cb,128,frame_len);
                veejay_memset( frame_buffer[c]->Cr,128,frame_len);
                veejay_memset( frame_buffer[c]->alpha,0,frame_len);
                veejay_memset( frame_buffer[c]->P0, pixel_Y_lo_, frame_len );
                veejay_memset( frame_buffer[c]->P0 + frame_len, 128, frame_len * 4);
                veejay_memset( frame_buffer[c]->P1, pixel_Y_lo_, frame_len );
                veejay_memset( frame_buffer[c]->P1 + frame_len, 128, frame_len * 4);
         }
     }


    vj_perform_clear_cache();
    veejay_memset( frame_info[0],0,SAMPLE_MAX_EFFECTS);

    sws_template templ;
    veejay_memset(&templ,0,sizeof(sws_template));
    templ.flags = yuv_which_scaler();

    rgba_frame[0] = yuv_rgb_template( rgba_buffer[0], w, h, PIX_FMT_RGBA );
    rgba_frame[1] = yuv_rgb_template( rgba_buffer[1], w, h, PIX_FMT_RGBA );
    yuva_frame[0] = yuv_yuv_template( NULL, NULL, NULL, w,h,PIX_FMT_YUVA444P );
    yuva_frame[1] = yuv_yuv_template( NULL, NULL, NULL, w,h,PIX_FMT_YUVA444P );
    yuv420_frame[0] = yuv_yuv_template( NULL, NULL, NULL, w, h, PIX_FMT_YUV420P );

    yuv420_scaler =  yuv_init_swscaler( yuva_frame[0], yuv420_frame[0], &templ, yuv_sws_get_cpu_flags() );
    if(yuv420_scaler == NULL )
        return 0;

    yuv2rgba_scaler = yuv_init_swscaler( yuva_frame[0], rgba_frame[0], &templ, yuv_sws_get_cpu_flags() );
    if(yuv2rgba_scaler == NULL )
        return 0;

    rgba2yuv_scaler = yuv_init_swscaler( rgba_frame[1], yuva_frame[0], &templ, yuv_sws_get_cpu_flags());
    if(rgba2yuv_scaler == NULL )
        return 0;

    rgba_frame[0]->data[0] = rgba_buffer[0];
    rgba_frame[1]->data[0] = rgba_buffer[1];

    veejay_msg(VEEJAY_MSG_INFO,
        "Using %.2f MB RAM in performer (memory %s paged to the swap area, %.2f MB pre-allocated for fx-chain)",
            ((float)total_used/1048576.0f),
            ( mlock_success ? "is not going to be" : "may be" ),
            ((float)tmp1/1048576.0f)
        ); 

    if( info->uc->scene_detection ) {
        vj_el_auto_detect_scenes( info->edit_list, temp_buffer, w,h, info->uc->scene_detection );
    }

    return 1;
}

size_t  vj_perform_fx_chain_size()
{
    return fx_chain_buflen;
}

static void vj_perform_close_audio() {
    if( lin_audio_buffer_ )
        free(lin_audio_buffer_ );
    if( audio_silence_ )
        free(audio_silence_);
    veejay_memset( audio_buffer, 0, sizeof(uint8_t*) * SAMPLE_MAX_EFFECTS );

#ifdef HAVE_JACK
    if(top_audio_buffer) free(top_audio_buffer);
    if(audio_rec_buffer) free(audio_rec_buffer);
    if(audio_render_buffer) free( audio_render_buffer );
    if(down_sample_buffer) free( down_sample_buffer );
    int i;
    for(i=0; i <= MAX_SPEED; i ++)
    {
        if(resample_context[i])
            vj_audio_resample_close( resample_context[i] );
        if(downsample_context[i])
            vj_audio_resample_close( downsample_context[i]);
        resample_context[i] = NULL;
        downsample_context[i] = NULL;
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

    top_audio_buffer =
        (uint8_t *) vj_calloc(sizeof(uint8_t) * 8 * RUP8( PERFORM_AUDIO_SIZE ) );
    if(!top_audio_buffer)
        return 0;

    audio_rec_buffer =
        (uint8_t *) vj_calloc(sizeof(uint8_t) * RUP8( PERFORM_AUDIO_SIZE) );
    if(!audio_rec_buffer)
        return 0;

    down_sample_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * MAX_SPEED * 8 );
    if(!down_sample_buffer)
        return 0;
    down_sample_rec_buffer = down_sample_buffer + (sizeof(uint8_t) * PERFORM_AUDIO_SIZE * MAX_SPEED * 4);

    audio_render_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * PERFORM_AUDIO_SIZE * MAX_SPEED * 4 );
    if(!audio_render_buffer)
        return 0;

    lin_audio_buffer_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * PERFORM_AUDIO_SIZE * SAMPLE_MAX_EFFECTS );
    if(!lin_audio_buffer_)
        return 0;

    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
        audio_buffer[i] = lin_audio_buffer_ + (PERFORM_AUDIO_SIZE * i);
    }
 
    audio_silence_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * PERFORM_AUDIO_SIZE * SAMPLE_MAX_EFFECTS );
    if(!audio_silence_)
        return 0;

    /* 
     * The simplest way to time stretch the audio is to resample it and then playback the waveform at the original sampling frequency
     * This also lowers or raises the pitch, making it just like speeding up or down a tape recording. Perfect!
     */

    for( i = 0; i <= MAX_SPEED; i ++ )
    {
        int out_rate = (info->edit_list->audio_rate * (i+2));
        int down_rate = (info->edit_list->audio_rate / (i+2));
        resample_context[i] = vj_av_audio_resample_init(
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
        if(!resample_context[i])
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize audio upsampler for speed %d", i);
            return 0;
        }
        downsample_context[i] = vj_av_audio_resample_init(
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
       
        if(!downsample_context[i])
        {
            veejay_msg(VEEJAY_MSG_WARNING, "Cannot initialize audio downsampler for dup %d",i);
            return 0;
        }
    }
    
    return 1;
#endif
}

void vj_perform_free(veejay_t * info)
{
    int fblen = SAMPLE_MAX_EFFECTS; // mjpg buf
    int c;

    munlockall();

    sample_record_free();

    if(info->edit_list && info->edit_list->has_audio)
        vj_perform_close_audio();

    if( fx_chain_buffer == NULL ) {
        if(frame_buffer) {
            for (c = 0; c < fblen; c++) {
            if(vj_perform_row_used(c))
                vj_perform_free_row(c);
            }
        }
    }

    if(frame_buffer) {
        for(c = 0; c < fblen; c ++ )
        {
            if(frame_buffer[c]) {
                free(frame_buffer[c]);
                frame_buffer[c] = NULL;
            }
        }
        free(frame_buffer);
        frame_buffer = NULL;
    }

    if(primary_buffer){
        for( c = 0;c < PRIMARY_FRAMES; c++ )
        {
            free(primary_buffer[c] );
            primary_buffer[c] = NULL;
        }
        free(primary_buffer);
        primary_buffer = NULL;
    }

    if(crop_frame)
    {
        if(crop_frame->data[0]) {
            free(crop_frame->data[0]);
            crop_frame->data[0] = NULL;
        }
        if(crop_frame->data[1]) {
            free(crop_frame->data[1]);
            crop_frame->data[1] = NULL;
        }
        if(crop_frame->data[2]) {
            free(crop_frame->data[2]);
            crop_frame->data[2] = NULL;
        }
        free(crop_frame);
        crop_frame = NULL;
    }

   if(temp_buffer[0]) {
       free(temp_buffer[0]);
       temp_buffer[0] = NULL;
   }
   if(subrender_buffer[0]) {
       free(subrender_buffer[0]);
       subrender_buffer[0] = NULL;
   }

   if(feedback_buffer[0]) {
       free(feedback_buffer[0]);
       feedback_buffer[0] = NULL;
   }

   if(rgba_buffer[0]) {
       free(rgba_buffer[0]);
       rgba_buffer[0] = NULL;
   }
    
   vj_perform_record_buffer_free();

    if(video_output_buffer){
        for( c = 0 ; c < 2 ; c ++ )
        {
            if(video_output_buffer[c]->Y ) {
                free(video_output_buffer[c]->Y);
                video_output_buffer[c]->Y = NULL;
            }
            if(video_output_buffer[c]->Cb ) {
                free(video_output_buffer[c]->Cb );
                video_output_buffer[c]->Cb = NULL;
            }
            if(video_output_buffer[c]->Cr ) {
                free(video_output_buffer[c]->Cr );
                video_output_buffer[c]->Cr = NULL;
            }
            free(video_output_buffer[c]);
            video_output_buffer[c] = NULL;
        }
        free(video_output_buffer);
        video_output_buffer = NULL;
    }

    free(helper_frame);

    if (preview_buffer){
        if(preview_buffer->Y)
            free(preview_buffer->Y);
        free(preview_buffer);
    }

    if(fx_chain_buffer)
    {
        munlock(fx_chain_buffer, fx_chain_buflen);
        free(fx_chain_buffer);
    }
    if(pribuf_area)
    {
        munlock(pribuf_area, pribuf_len);
        free(pribuf_area);
    }

    yuv_free_swscaler( rgba2yuv_scaler );
    yuv_free_swscaler( yuv2rgba_scaler );
    yuv_free_swscaler( yuv420_scaler );

    free(rgba_frame[0]);
    free(rgba_frame[1]);

    vj_avcodec_stop(encoder_,0);
}

int vj_perform_preview_max_width() {
    return preview_max_w;
}

int vj_perform_preview_max_height() {
    return preview_max_h;
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
    frame[0] = primary_buffer[info->out_buf]->Y;
    frame[1] = primary_buffer[info->out_buf]->Cb;
    frame[2] = primary_buffer[info->out_buf]->Cr;
    frame[3] = primary_buffer[info->out_buf]->alpha;
}

uint8_t *vj_perform_get_preview_buffer()
{
    return preview_buffer->Y;
}

void    vj_perform_get_crop_dimensions(veejay_t *info, int *w, int *h)
{
    *w = info->video_output_width - info->settings->viewport.left - info->settings->viewport.right;
    *h = info->video_output_height - info->settings->viewport.top - info->settings->viewport.bottom;
}

static long stream_pts_ = 0;
static int vj_perform_compress_primary_frame_s2(veejay_t *info,VJFrame *frame )
{
    if( encoder_ == NULL ) {
        encoder_ = vj_avcodec_start(info->effect_frame1, ENCODER_MJPEG, NULL);
        if(encoder_ == NULL) {
            return 0;
        }
    }

    return vj_avcodec_encode_frame(encoder_,
            stream_pts_ ++,
            ENCODER_MJPEG,
                    frame->data, 
            vj_avcodec_get_buf(encoder_), 
            8 * 16 * 65535, 
            frame->format);
}

void    vj_perform_send_primary_frame_s2(veejay_t *info, int mcast, int to_mcast_link_id)
{
    int i;
    
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

                if( vj_server_send_frame( info->vjs[3], link_id, vj_avcodec_get_buf(encoder_),data_len, frame ) <= 0 ) {
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

        vj_copy_frame_holder(info->effect_frame1, primary_buffer[info->out_buf], &fxframe);

        int data_len = vj_perform_compress_primary_frame_s2( info,&fxframe );
        if( data_len <= 0) {
            return;
        }

        int id = (mcast ? 2: 3);

        if(!mcast) 
        {
            for( i = 0; i < VJ_MAX_CONNECTIONS; i++ ) {
                if( info->rlinks[i] != -1 ) {
                    if(vj_server_send_frame( info->vjs[id], info->rlinks[i], vj_avcodec_get_buf(encoder_), data_len, &fxframe )<=0)
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
            if(vj_server_send_frame( info->vjs[id], to_mcast_link_id, vj_avcodec_get_buf(encoder_), data_len, &fxframe )<=0)
            {
                veejay_msg(VEEJAY_MSG_DEBUG,  "Error sending multicast frame");
            }
        }
    }
}

void vj_perform_get_primary_frame_420p(veejay_t *info, uint8_t **frame )   
{
    yuv420_frame[0]->data[0] = frame[0] = temp_buffer[0];
    yuv420_frame[0]->data[1] = frame[1] = temp_buffer[1];
    yuv420_frame[0]->data[2] = frame[2] = temp_buffer[2];
    yuv420_frame[0]->data[3] = frame[3] = temp_buffer[3];
    
    VJFrame pframe;
    memcpy(&pframe, info->effect_frame1, sizeof(VJFrame));
    pframe.data[0] = primary_buffer[info->out_buf]->Y;
    pframe.data[1] = primary_buffer[info->out_buf]->Cb;
    pframe.data[2] = primary_buffer[info->out_buf]->Cr;
    pframe.stride[0] = yuv420_frame[0]->stride[0];
    pframe.stride[1] = yuv420_frame[0]->stride[0] >> 1;
    pframe.stride[2] = yuv420_frame[0]->stride[1];
    pframe.stride[3] = 0;

    yuv_convert_and_scale( yuv420_scaler, &pframe, yuv420_frame[0] );
}

static int  vj_perform_apply_first(veejay_t *info, vjp_kf *todo_info,
    VJFrame **frames, VJFrameInfo *frameinfo, int e , int c, int n_frame, void *ptr, int playmode)
{
    int args[SAMPLE_MAX_PARAMETERS];
    int n_a = 0;
    int is_mixer = 0;
    int rgb = vj_effect_get_info( e, &is_mixer, &n_a );
    int entry = e;
    
    veejay_memset( args, 0 , sizeof(args) );
    
    if( playmode == VJ_PLAYBACK_MODE_TAG )
    {
        if(!vj_tag_get_all_effect_args(todo_info->ref, c, args, n_a, n_frame ))
            return 1;
    }
    else
    {
        if(!sample_get_all_effect_arg( todo_info->ref, c, args, n_a, n_frame))
            return 1;
    }

    if( rgb ) {
        yuva_frame[0]->data[0] = frames[0]->data[0];
        yuva_frame[0]->data[1] = frames[0]->data[1];
        yuva_frame[0]->data[2] = frames[0]->data[2];
        yuva_frame[0]->data[3] = frames[0]->data[3];

        yuv_convert_and_scale_rgb( yuv2rgba_scaler, yuva_frame[0], rgba_frame[0] );
        if(is_mixer) {
            yuva_frame[1]->data[0] = frames[1]->data[0];
            yuva_frame[1]->data[1] = frames[1]->data[1];
            yuva_frame[1]->data[2] = frames[1]->data[2];
            yuva_frame[1]->data[3] = frames[1]->data[3];

            yuv_convert_and_scale_rgb( yuv2rgba_scaler, yuva_frame[1], rgba_frame[1] ); 
        }

        int res = vj_effect_apply( rgba_frame, frameinfo, todo_info,entry, args, ptr );
    
        if(res == 0) {
            if( rgb )  {
                yuv_convert_and_scale_from_rgb( rgba2yuv_scaler, rgba_frame[0],yuva_frame[0] );
            }
        }
        return res;
    }
    
    return vj_effect_apply( frames,frameinfo, todo_info,entry,args, ptr );
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

static int vj_perform_use_cached_ycbcr_frame(veejay_t *info, int centry, VJFrame *dst, int chain_entry)
{
    if( centry == 0 )
    {
        vj_perform_copy( primary_buffer[0], frame_buffer[chain_entry], info->effect_frame1->len , info->effect_frame1->uv_len , info->effect_frame1->stride[3] * info->effect_frame1->height );
        if(info->effect_frame1->ssm)
            vj_perform_set_444( dst );
        else 
            vj_perform_set_422( dst );

        frame_buffer[chain_entry]->ssm = info->effect_frame1->ssm;
    }
    else
    {
        int c = centry - 1;
        vj_perform_copy( frame_buffer[c], frame_buffer[chain_entry], info->effect_frame1->len , info->effect_frame1->uv_len ,info->effect_frame1->stride[3] * info->effect_frame1->height );
        if( info->effect_frame1->ssm ) 
            vj_perform_set_444(dst);
        else
            vj_perform_set_422(dst);

        frame_buffer[chain_entry]->ssm = frame_buffer[c]->ssm;
     }
    return frame_buffer[chain_entry]->ssm;
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

                n_samples = vj_audio_resample( resample_context[sc],(short*) audio_buf, (short*) sambuf, n_samples );
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

            int tds = vj_audio_resample( downsample_context[sc], (short*) downsample_buffer, (short*) audio_buf, n_samples );
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

static int vj_perform_apply_secundary_tag(veejay_t * info, int sample_id, int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0_ref, uint8_t *p1_ref, int subrender )
{   
    int error = 1;
    int nframe;
    int len = 0;
    int centry = -1;
    int ssm = 0;


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
            centry = vj_perform_tag_is_cached(info,chain_entry, sample_id);
    
        if (centry == -1 || subrender)
        {
            if(! vj_tag_get_active( sample_id ) )
            {
                // active stream if neccesaary
                vj_tag_set_active(sample_id, 1 );
            }
        
            //if (vj_tag_get_active(sample_id) == 1 )
            //{ 
                int res = vj_tag_get_frame(sample_id, dst,audio_buffer[chain_entry]);
                if(res==1)  {
                    error = 0;
                    ssm = dst->ssm;
                }
                else
                {
                    //vj_tag_set_active(sample_id, 0);  <-- user issue now, delete the stream
                }
            //}
        }
        else
        {   
            ssm = vj_perform_use_cached_ycbcr_frame(info, centry, dst,chain_entry);
            error = 0;
        }

    if(!error && !subrender)
        cached_tag_frames[1 + chain_entry ] = sample_id;    

    break;
    
   case VJ_TAG_TYPE_NONE:
        nframe = vj_perform_get_subframe_tag(info, sample_id, chain_entry);

        sample_set_resume( sample_id, nframe );

        if(!subrender)
            centry = vj_perform_sample_is_cached(info,sample_id, chain_entry);
        if(centry == -1 || info->no_caching|| subrender )
        {
            len = vj_perform_get_frame_fx( info, sample_id, nframe, src,dst,p0_ref,p1_ref );    
            if(len > 0 ) {
               error = 0;
               ssm = dst->ssm;
            }
        }
        else
        {
            ssm = vj_perform_use_cached_ycbcr_frame( info, centry, dst, chain_entry);
            cached_sample_frames[ 1 + chain_entry ] = sample_id;
            error = 0;
        }
        if(!error && !subrender)
         cached_sample_frames[chain_entry+1] = sample_id;

       break;

    }

    return ssm;
}

static  int vj_perform_get_feedback_frame(veejay_t *info, VJFrame *src, VJFrame *dst, int check_sample, int s1)
{
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
                primary_buffer[4]->Y,
                primary_buffer[4]->Cb,
                primary_buffer[4]->Cr,
                primary_buffer[4]->alpha
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


static int vj_perform_apply_secundary(veejay_t * info, int this_sample_id, int sample_id, int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0_ref, uint8_t *p1_ref, int subrender)
{       
    int error = 1;
    int nframe;
    int len;
    int res = 1;
    int centry = -1;
    int ssm = 0;

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
                centry = vj_perform_tag_is_cached(info,chain_entry, sample_id); // is it cached?
            //@ not cached
            if (centry == -1 || subrender)
            {
                if(! vj_tag_get_active( sample_id ) )
                {
                    vj_tag_set_active(sample_id, 1 );
                }
        
                res = vj_tag_get_frame(sample_id, dst,audio_buffer[chain_entry]);
                if(res) {
                    error = 0; 
                    ssm = dst->ssm;
                }                   
            }
            else
            {
                ssm = vj_perform_use_cached_ycbcr_frame(info,centry, dst,chain_entry);
                error = 0;
            }
            if(!error && !subrender)
                cached_tag_frames[1 + chain_entry ] = sample_id;    
            
            break;
        
        case VJ_TAG_TYPE_NONE:
                nframe = vj_perform_get_subframe(info,this_sample_id, sample_id, chain_entry); // get exact frame number to decode

            sample_set_resume( sample_id, nframe );

            if(!subrender)
                centry = vj_perform_sample_is_cached(info,sample_id, chain_entry);
                
            if(centry == -1 || info->no_caching||subrender) {
                len = vj_perform_get_frame_fx( info, sample_id, nframe, src, dst, p0_ref, p1_ref ); 
                if(len > 0 ) {
                    error = 0;
                    ssm = dst->ssm;
                }
            }
            else
            {
                ssm = vj_perform_use_cached_ycbcr_frame(info,centry,dst, chain_entry);  
                cached_sample_frames[ 1 + chain_entry ] = sample_id;
                error = 0;
            }   
            
            if(!error && !subrender)
                cached_sample_frames[chain_entry+1] = sample_id;
            
            break;
    }

    return ssm;
}

static void vj_perform_tag_render_chain_entry(veejay_t *info, sample_eff_chain *fx_entry,int chain_entry, VJFrame *frames[2], int subrender)
{
    VJFrameInfo *frameinfo;
    video_playback_setup *settings = info->settings;
        
    frameinfo = info->effect_frame_info;
    
    frames[1]->data[0] = frame_buffer[chain_entry]->Y;
    frames[1]->data[1] = frame_buffer[chain_entry]->Cb;
    frames[1]->data[2] = frame_buffer[chain_entry]->Cr;
    frames[1]->data[3] = frame_buffer[chain_entry]->alpha; 

    vjp_kf *setup = info->effect_info;
    setup->ref = info->uc->sample_id;

    int effect_id = fx_entry->effect_id;
    int sub_mode = vj_effect_get_subformat(effect_id);
    int ef = vj_effect_get_extra_frame(effect_id);

    vj_perform_supersample(settings, frames[0], (ef ? frames[1] : NULL), sub_mode );

    frame_buffer[chain_entry]->ssm = frames[0]->ssm;

    if(ef)
    {
        frames[1]->ssm = vj_perform_apply_secundary_tag(info,fx_entry->channel,fx_entry->source_type,chain_entry,frames[0],frames[1],frame_buffer[chain_entry]->P0, frame_buffer[chain_entry]->P1, 0 );

        if( subrender && settings->fxdepth ) {
            frames[1]->ssm = vj_perform_preprocess_secundary( info,fx_entry->channel,fx_entry->source_type,sub_mode,chain_entry, frames, frameinfo );
        }

        vj_perform_supersample(settings, NULL, frames[1], sub_mode);
    }
    
    if( pvar_.fade_entry == chain_entry && pvar_.fade_method == 1) {
        vj_perform_pre_chain( info, frames[0] );
    }

    if(vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,(int) settings->current_frame_num,fx_entry->fx_instance,info->uc->playback_mode) == -2)
    {
        int res = 0;
        void *pfx = vj_effect_activate( effect_id, &res );
        if( res )  {
            settings->fxrow[chain_entry] = effect_id;
            if( pfx ) {
                fx_entry->fx_instance = pfx;
                vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,(int) settings->current_frame_num,pfx,info->uc->playback_mode );
            }
        }
    }
    if( pvar_.fade_entry == chain_entry && pvar_.fade_method == 2) {
        vj_perform_pre_chain( info, frames[0] );
    }
}

static  int vj_perform_preprocess_secundary( veejay_t *info, int id, int mode,int expected_ssm,int chain_entry, VJFrame **F, VJFrameInfo *frameinfo )
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
    sub.data[0] = subrender_buffer[0];
    sub.data[1] = subrender_buffer[1];
    sub.data[2] = subrender_buffer[2];
    sub.data[3] = subrender_buffer[3];
    sub.ssm     = 0; 

    VJFrame *subframes[2];
    subframes[0] = &top;
    subframes[1] = &sub;

    /* top frame is F[1] (render on sub-sample) */

    uint8_t *p0_ref = subrender_buffer[0] + ( F[0]->len * 4 );
    uint8_t *p1_ref = p0_ref + (F[0]->len * 4);

    /* render FX chain */
    vjp_kf setup;
    veejay_memset(&setup,0,sizeof(vjp_kf));
    setup.ref = id; 

    sample_eff_chain **chain = NULL;

    if( pvar_.fade_entry == chain_entry && pvar_.fade_method == 3 ) {
        vj_perform_pre_chain( info, &top );
    }

    switch( mode ) {
        case VJ_PLAYBACK_MODE_SAMPLE:   
            chain = sample_get_effect_chain( id );
            for( n=0; n < SAMPLE_MAX_EFFECTS-1; n ++ ) {
                sample_eff_chain *fx_entry = chain[n];
                if( fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
                    continue;

                int fx_id = fx_entry->effect_id;
                int sm = vj_effect_get_subformat(fx_id);
                int ef = vj_effect_get_extra_frame(fx_id);
                
                if( ef ) {
                    subframes[1]->ssm = vj_perform_apply_secundary(info,id,fx_entry->channel,fx_entry->source_type,n,subframes[0],subframes[1],p0_ref, p1_ref, 1);
                }   

                vj_perform_supersample( settings, subframes[0], subframes[1], sm );

                if(vj_perform_apply_first(info,&setup,subframes,frameinfo,fx_id,n,(int) settings->current_frame_num,fx_entry->fx_instance, mode ) == -2 ) {
                    int res = 0;
                    void *pfx = vj_effect_activate( fx_id, &res );
                    if( res )  {
                        settings->fxrow[n] = fx_id;
                        if( pfx ) {
                            fx_entry->fx_instance = pfx;
                            vj_perform_apply_first(info,&setup,subframes,frameinfo,fx_id,n,(int) settings->current_frame_num,pfx,mode );
                        }
                    }
                }   
            }
            break;
        case VJ_PLAYBACK_MODE_TAG:
            chain = vj_tag_get_effect_chain( id );
            for( n=0; n < SAMPLE_MAX_EFFECTS; n ++ ) {
                sample_eff_chain *fx_entry = chain[n];
                if( fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
                    continue;
                
                int fx_id = fx_entry->effect_id;
                int sm = vj_effect_get_subformat(fx_id);
                int ef = vj_effect_get_extra_frame(fx_id);
                
                if( ef ) {
                    subframes[1]->ssm = vj_perform_apply_secundary_tag(info,fx_entry->channel,fx_entry->source_type,n,subframes[0],subframes[1],p0_ref,p1_ref,1);
                }
               
                vj_perform_supersample(settings, subframes[0],subframes[1], sm); 
                
                if(vj_perform_apply_first(info,&setup,subframes,frameinfo,fx_id,n,(int) settings->current_frame_num,fx_entry->fx_instance, mode ) == - 2) {
                        int res = 0;
                        void *pfx = vj_effect_activate( fx_id, &res);
                        if( res ) {
                            settings->fxrow[n] = fx_id; 
                            if(pfx) {
                                fx_entry->fx_instance = pfx;
                                vj_perform_apply_first(info,&setup,subframes,frameinfo,fx_id,n,(int) settings->current_frame_num, pfx,mode );
                            }
                        }
                }
            }   
            break;
    }

    if( pvar_.fade_entry == chain_entry && pvar_.fade_method == 4 ) {
        vj_perform_pre_chain( info, &top );
    }

    return top.ssm;
}

static void vj_perform_render_chain_entry(veejay_t *info, sample_eff_chain *fx_entry, int chain_entry, VJFrame *frames[2], int subrender)
{
    VJFrameInfo *frameinfo;
    video_playback_setup *settings = info->settings;
        
    frameinfo = info->effect_frame_info;
    
    frames[1]->data[0] = frame_buffer[chain_entry]->Y;
    frames[1]->data[1] = frame_buffer[chain_entry]->Cb;
    frames[1]->data[2] = frame_buffer[chain_entry]->Cr;
    frames[1]->data[3] = frame_buffer[chain_entry]->alpha;

    vjp_kf *setup = info->effect_info;
    setup->ref = info->uc->sample_id;

    int effect_id = fx_entry->effect_id;
    int sub_mode = vj_effect_get_subformat(effect_id);
    int ef = vj_effect_get_extra_frame(effect_id);

    vj_perform_supersample(settings,frames[0], NULL, sub_mode);

    frame_buffer[chain_entry]->ssm = frames[0]->ssm;

    if(ef)
    {
        frames[1]->ssm = vj_perform_apply_secundary(info,info->uc->sample_id,fx_entry->channel,fx_entry->source_type,chain_entry,frames[0],frames[1],frame_buffer[chain_entry]->P0, frame_buffer[chain_entry]->P1, 0);
        
        if( subrender && settings->fxdepth) {
            frames[1]->ssm = vj_perform_preprocess_secundary( info, fx_entry->channel,fx_entry->source_type,sub_mode,chain_entry, frames, frameinfo );
        }

        vj_perform_supersample(settings, NULL, frames[1], sub_mode);
    }

    if( pvar_.fade_entry == chain_entry && pvar_.fade_method == 1) {
        vj_perform_pre_chain( info, frames[0] );
    }

    if( vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,
            (int) settings->current_frame_num, fx_entry->fx_instance,info->uc->playback_mode   ) == -2 )
    {
        int res = 0;
        void *pfx = vj_effect_activate( effect_id, &res );
        if( res )  {
            settings->fxrow[chain_entry] = effect_id;
            if( pfx ) {
                fx_entry->fx_instance = pfx;
                vj_perform_apply_first(info,setup,frames,frameinfo,effect_id,chain_entry,(int) settings->current_frame_num,pfx,info->uc->playback_mode  );
            }
        }
    }
    
    if( pvar_.fade_entry == chain_entry && pvar_.fade_method == 2) {
        vj_perform_pre_chain( info, frames[0] );
    }
}

static int clear_framebuffer__ = 0;

static int vj_perform_sample_complete_buffers(veejay_t * info, int *hint444)
{
    int chain_entry;
    vjp_kf *setup;
    VJFrame *frames[2];
    setup = info->effect_info;

    frames[0] = info->effect_frame1;
    frames[1] = info->effect_frame2;
    setup->ref = info->uc->sample_id;
    frames[0]->data[0] = primary_buffer[0]->Y;
    frames[0]->data[1] = primary_buffer[0]->Cb;
    frames[0]->data[2] = primary_buffer[0]->Cr;
    frames[0]->data[3] = primary_buffer[0]->alpha;
    
    sample_eff_chain **chain = sample_get_effect_chain( info->uc->sample_id );
    for( chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry ++ ) {
        if( chain[chain_entry]->clear ) {
            clear_framebuffer__ = 1;
            if( frames[0]->stride[3] > 0 )
                veejay_memset( frame_buffer[chain_entry]->alpha, 0, frames[0]->stride[3] * frames[0]->height );
            chain[chain_entry]->clear = 0;
        }
    }

    if(clear_framebuffer__ == 1 || info->settings->clear_alpha == 1)
    {
        if( frames[0]->stride[3] > 0 ) 
            veejay_memset( frames[0]->data[3], info->settings->alpha_value, frames[0]->stride[3] * frames[0]->height );
        clear_framebuffer__ = 0;
    }

    if(pvar_.fader_active || pvar_.fade_value > 0 || pvar_.fade_alpha ) {
        if( pvar_.fade_entry == -1 ) {
            vj_perform_pre_chain( info, frames[0] );
        }
    }

    int subrender_entry = -1;
    for(chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
    {
        int subrender = sample_get_subrender(info->uc->sample_id, chain_entry, &subrender_entry);
        sample_eff_chain *fx_entry = chain[chain_entry];
        if(fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
            continue;
        
        vj_perform_render_chain_entry(info, fx_entry, chain_entry, frames, (subrender? subrender_entry: subrender));
    }
    *hint444 = frames[0]->ssm;

    return 1;
}

static int vj_perform_tag_complete_buffers(veejay_t * info,int *hint444  )
{
    int chain_entry;
    VJFrame *frames[2];
    vjp_kf *setup = info->effect_info;

    frames[0] = info->effect_frame1;
    frames[1] = info->effect_frame2;
    setup->ref= info->uc->sample_id;

    frames[0]->data[0] = primary_buffer[0]->Y;
    frames[0]->data[1] = primary_buffer[0]->Cb;
    frames[0]->data[2] = primary_buffer[0]->Cr;
    frames[0]->data[3] = primary_buffer[0]->alpha;

    sample_eff_chain **chain = vj_tag_get_effect_chain( info->uc->sample_id );
    for( chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry ++ ) {
        if( chain[chain_entry]->clear ) {
            clear_framebuffer__ = 1;
            if( frames[0]->stride[3] > 0 )
                veejay_memset( frame_buffer[chain_entry]->alpha, 0, frames[0]->stride[3] * frames[0]->height );
            chain[chain_entry]->clear = 0;
        }
    }

    if(clear_framebuffer__ == 1 || info->settings->clear_alpha == 1)
    {
        if( frames[0]->stride[3] > 0 )
            veejay_memset( frames[0]->data[3], info->settings->alpha_value, frames[0]->stride[3] * frames[0]->height );
        clear_framebuffer__ = 0;
    }

    if( pvar_.fader_active || pvar_.fade_value >0 || pvar_.fade_alpha) {
        if( pvar_.fade_entry == -1 ) {
            vj_perform_pre_chain( info, frames[0] );
        }
    }

    int subrender_entry = -1;
    for(chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
    {
        int subrender = vj_tag_get_subrender( info->uc->sample_id, chain_entry, &subrender_entry );
        sample_eff_chain *fx_entry = chain[chain_entry];
        if(fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
            continue;

        vj_perform_tag_render_chain_entry( info, fx_entry, chain_entry, frames, (subrender ? subrender_entry : subrender));
    }

    *hint444 = frames[0]->ssm;
    
    return 1;
}

static void vj_perform_plain_fill_buffer(veejay_t * info, int *ret)
{
    video_playback_setup *settings = (video_playback_setup*)  info->settings;
    
    VJFrame frame;

    if(info->settings->feedback && info->settings->feedback_stage > 1 ) {
        vj_copy_frame_holder(info->effect_frame1, NULL, &frame); 
        frame.data[0] = feedback_frame.data[0];
        frame.data[1] = feedback_frame.data[1];
        frame.data[2] = feedback_frame.data[2];
        frame.data[3] = feedback_frame.data[3];
    } else {
        vj_copy_frame_holder(info->effect_frame1, primary_buffer[0], &frame);
    }

    uint8_t *p0_buffer[PSLOW_B] = {
        primary_buffer[PSLOW_B]->Y,
        primary_buffer[PSLOW_B]->Cb,
        primary_buffer[PSLOW_B]->Cr,
        primary_buffer[PSLOW_B]->alpha };

    uint8_t *p1_buffer[4]= {
        primary_buffer[PSLOW_A]->Y,
        primary_buffer[PSLOW_A]->Cb,
        primary_buffer[PSLOW_A]->Cr,
        primary_buffer[PSLOW_A]->alpha };

    if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
    {
        *ret = vj_perform_get_frame_(info, info->uc->sample_id, settings->current_frame_num,&frame,&frame, p0_buffer,p1_buffer,0 );
    } else if ( info->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN ) {
        *ret = vj_perform_get_frame_(info, 0, settings->current_frame_num,&frame,&frame, p0_buffer, p1_buffer,0 );
    }

    if(info->uc->take_bg==1 )
    {
        info->uc->take_bg = vj_perform_take_bg(info,&frame);
    } 

}
static int rec_audio_sample_ = 0;
static int vj_perform_render_sample_frame(veejay_t *info, uint8_t *frame[4], int sample, int type)
{
    int audio_len = 0;
    if( type == 0 && info->audio == AUDIO_PLAY ) {
        if( info->current_edit_list->has_audio )
        {
            audio_len = vj_perform_fill_audio_buffers(info, audio_rec_buffer, audio_render_buffer + (2* PERFORM_AUDIO_SIZE * MAX_SPEED), down_sample_rec_buffer, &rec_audio_sample_ );
        } 
    }

    return sample_record_frame( sample,frame,audio_rec_buffer,audio_len,info->pixel_format );
}

static int vj_perform_render_offline_tag_frame(veejay_t *info, uint8_t *frame[4])
{
    vj_tag_get_frame( info->settings->offline_tag_id, info->effect_frame1, NULL );

    return vj_tag_record_frame( info->settings->offline_tag_id, info->effect_frame1->data, NULL, 0, info->pixel_format );
}
    
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4])
{
    return vj_tag_record_frame( info->uc->sample_id, frame, NULL, 0, info->pixel_format);
}   

static int vj_perform_record_offline_commit_single(veejay_t *info)
{
    char filename[1024];

    int stream_id = info->settings->offline_tag_id;
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

void vj_perform_record_offline_stop(veejay_t *info)
{   
    video_playback_setup *settings = info->settings;
    int df = vj_event_get_video_format();

    int stream_id = settings->offline_tag_id;
    int play = settings->offline_created_sample;
    
    vj_tag_reset_encoder(stream_id);
    vj_tag_reset_autosplit(stream_id);
    
    settings->offline_record = 0;
    settings->offline_created_sample = 0;
    settings->offline_tag_id = 0;

    if( play ) {
        if(df != ENCODER_YUV4MPEG && df != ENCODER_YUV4MPEG420)
        {
            info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
            int id = sample_highest_valid_id();
            veejay_set_sample(info, id );
            veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d",id);
        }
        else {

            veejay_msg(VEEJAY_MSG_INFO, "Completed offline recording");
        }
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
            veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d",id);
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
            veejay_set_sample(info, id );
            veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d",id);
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
    
    frame[0] = primary_buffer[0]->Y;
    frame[1] = primary_buffer[0]->Cb;
    frame[2] = primary_buffer[0]->Cr;
    frame[3] = NULL;

    res = vj_perform_render_sample_frame(info, frame, sample,type);

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
    
    if( record_buffer->Y == NULL )
        vj_perform_record_buffer_init();

    frame[0] = record_buffer->Y;
    frame[1] = record_buffer->Cb;
    frame[2] = record_buffer->Cr;
    frame[3] = NULL;

    info->effect_frame1->data[0] = frame[0];
    info->effect_frame1->data[1] = frame[1];
    info->effect_frame1->data[2] = frame[2];
    info->effect_frame1->data[3] = frame[3];

    res = vj_perform_render_offline_tag_frame(info, frame);

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
                report_bug();
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
    
    frame[0] = primary_buffer[0]->Y;
    frame[1] = primary_buffer[0]->Cb;
    frame[2] = primary_buffer[0]->Cr;
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
                report_bug();
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

static void vj_perform_tag_fill_buffer(veejay_t * info)
{
    int error = 1;
    int type = pvar_.type;
    int active = pvar_.active;

    if(info->settings->feedback && info->settings->feedback_stage > 1 ) {
        info->effect_frame1->data[0] = feedback_frame.data[0];
        info->effect_frame1->data[1] = feedback_frame.data[1];
        info->effect_frame1->data[2] = feedback_frame.data[2];
        info->effect_frame1->data[3] = feedback_frame.data[3];
        info->effect_frame1->ssm = feedback_frame.ssm;
    } else {
        info->effect_frame1->data[0] = primary_buffer[0]->Y;
        info->effect_frame1->data[1] = primary_buffer[0]->Cb;
        info->effect_frame1->data[2] = primary_buffer[0]->Cr;
        info->effect_frame1->data[3] = primary_buffer[0]->alpha;
    }
    
    if(!active)
    {
        if (type == VJ_TAG_TYPE_V4L || type == VJ_TAG_TYPE_NET || type == VJ_TAG_TYPE_MCAST || type == VJ_TAG_TYPE_PICTURE || type == VJ_TAG_TYPE_AVFORMAT ) 
            vj_tag_enable( info->uc->sample_id );
    }
    else
    {
        if (vj_tag_get_frame(info->uc->sample_id, info->effect_frame1,NULL))
        {
            error = 0;
            cached_tag_frames[0] = info->uc->sample_id;
        }
    }         

    if (error == 1)
    {
        dummy_apply(info->effect_frame1,VJ_EFFECT_COLOR_BLACK );
    }

    if(info->uc->take_bg==1 )
    {
        info->uc->take_bg = vj_perform_take_bg(info,info->effect_frame1);
    } 
}

static void vj_perform_pre_chain(veejay_t *info, VJFrame *frame)
{
    vj_perform_copy3( frame->data, temp_buffer, frame->len, (frame->ssm ? frame->len : frame->uv_len), frame->stride[3] * frame->height  );
    temp_ssm = frame->ssm;
}

static  inline  void    vj_perform_supersample_chain( veejay_t *info, VJFrame *frame )
{
    //temp_buffer contains state of frame before entering render chain 
    if(temp_ssm == 0 ) {
        chroma_supersample(
        info->settings->sample_mode,
        frame,
        temp_buffer );
    }
    //but top source is conditional
    if( frame->ssm == 0 ) {
        chroma_supersample(
            info->settings->sample_mode,
            frame,
            frame->data );
        vj_perform_set_444(frame); 
    }
    info->effect_frame1->ssm = frame->ssm;
}

void    vj_perform_follow_fade(int status) {
    pvar_.follow_fade = status;
}

static int vj_perform_post_chain_sample(veejay_t *info, VJFrame *frame)
{
    int opacity; 
    int mode   = pvar_.fader_active;
    int follow = 0;
    int fade_alpha_method = pvar_.fade_alpha;

    if(mode == 2 ) // manual fade
        opacity = (int) sample_get_fader_val(info->uc->sample_id);
    else    // fade in/fade out
        opacity = (int) sample_apply_fader_inc(info->uc->sample_id);

    pvar_.fade_value = opacity;

    switch( fade_alpha_method ) {
        case 0:
            if( opacity > 0 ) {
                vj_perform_supersample_chain( info, frame );
                opacity_blend_apply( frame->data ,temp_buffer,frame->len,(frame->ssm ? frame->len: frame->uv_len), opacity );
            }
            break;
        default:
            vj_perform_supersample_chain( info, frame );
            alpha_transition_apply( frame, temp_buffer,(0xff - opacity) );
            break;
    }

    if(mode != 2)
    {
        int dir =sample_get_fader_direction(info->uc->sample_id);
        if((dir<0) &&(opacity == 0))
        {
            int fade_method = sample_get_fade_method(info->uc->sample_id );
            if( fade_method == 0 )
                sample_set_effect_status(info->uc->sample_id, 1);
            sample_reset_fader(info->uc->sample_id);
            if( pvar_.follow_fade ) {
              follow = 1;
            }
            veejay_msg(VEEJAY_MSG_DEBUG, "Sample Chain Auto Fade Out done");
        }
        if((dir>0) && (opacity==255))
        {
            sample_reset_fader(info->uc->sample_id);
            veejay_msg(VEEJAY_MSG_DEBUG, "Sample Chain Auto fade In done");
            if( pvar_.follow_fade ) {
              follow = 1;
            }
        }
        } else if(mode) {
            if( pvar_.follow_fade ) {
              follow = 1;
        }
    }

    if( follow ) {
        if( pvar_.fade_entry == -1 ) {
            //@ follow first channel B in chain
            int i,k;
            int tmp = 0;    
            for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++) {
                k = sample_get_chain_channel(info->uc->sample_id, i );
                tmp = sample_get_chain_source( info->uc->sample_id, i );
                if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k) )) {
                    pvar_.follow_now[1] = tmp;  
                    pvar_.follow_now[0] = k;
                    break;
                }
            }
        }
        else {
            //@ follow channel B in chain from seleted entry 
            int tmp = 0;
            int k = sample_get_chain_channel(info->uc->sample_id, pvar_.fade_entry );
            tmp = sample_get_chain_source(info->uc->sample_id, pvar_.fade_entry );
            if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k))) {
                pvar_.follow_now[1] = tmp;
                pvar_.follow_now[0] = k;
            }
        }
    }

    return follow;
}

static int vj_perform_post_chain_tag(veejay_t *info, VJFrame *frame)
{
    int opacity = 0; //@off 
    int mode = pvar_.fader_active;
    int follow = 0;
    int fade_alpha_method = pvar_.fade_alpha;

    if(mode == 2)
        opacity = (int) vj_tag_get_fader_val(info->uc->sample_id);
    else if( mode )
        opacity = (int) vj_tag_apply_fader_inc(info->uc->sample_id);

    pvar_.fade_value = opacity;

    if( opacity == 0 ) {
        if( pvar_.follow_fade ) {
           follow = 1;
        }
    }

    switch( fade_alpha_method ) {
        case 0:
            if( opacity > 0 ) {
                vj_perform_supersample_chain( info, frame );
                opacity_blend_apply( frame->data ,temp_buffer,frame->len, (frame->ssm ? frame->len : frame->uv_len), opacity );
            }
            break;
        default:
            vj_perform_supersample_chain( info, frame );
            alpha_transition_apply( frame, temp_buffer,0xff - opacity );
            break;
    }

    if(mode != 2)
    {
        int dir = vj_tag_get_fader_direction(info->uc->sample_id);

        if((dir < 0) && (opacity == 0))
        {
            int fade_method = vj_tag_get_fade_method(info->uc->sample_id);
            if( fade_method == 0 )
                vj_tag_set_effect_status(info->uc->sample_id,1);
            vj_tag_reset_fader(info->uc->sample_id);
            if( pvar_.follow_fade ) {
               follow = 1;
            }
            veejay_msg(VEEJAY_MSG_DEBUG, "Stream Chain Auto Fade done");
        }
        if((dir > 0) && (opacity == 255))
        {
            vj_tag_reset_fader(info->uc->sample_id);
            if( pvar_.follow_fade ) {
               follow = 1;
            }
            veejay_msg(VEEJAY_MSG_DEBUG, "Stream Chain Auto Fade done");
        }
        } else if(mode){
            if( pvar_.follow_fade ) {
             follow = 1;
        }
    }

    if( follow ) {
        int i;
        int tmp=0,k;
        for( i = 0; i < SAMPLE_MAX_EFFECTS - 1; i ++ ) {
            k = vj_tag_get_chain_channel(info->uc->sample_id, i );
            tmp = vj_tag_get_chain_source( info->uc->sample_id, i );
            if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k)) ) {
                pvar_.follow_now[1] = tmp;  
                pvar_.follow_now[0] = k;
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

    if( settings->audio_mute || !el->has_audio || settings->current_playback_speed == 0) {
        int num_samples = (info->edit_list->audio_rate / info->edit_list->video_fps);
        int bps = info->edit_list->audio_bps;
        veejay_memset( top_audio_buffer, 0, num_samples * bps);
        vj_perform_play_audio( settings, top_audio_buffer, (num_samples * bps ));
        return 1;
    }

    long this_frame = settings->current_frame_num;
    int num_samples =  (el->audio_rate/el->video_fps);
    int pred_len = num_samples;
    int bps     =   el->audio_bps;
    uint8_t *a_buf = top_audio_buffer;
    if (info->audio == AUDIO_PLAY)
    {
        switch (info->uc->playback_mode)
        {
            case VJ_PLAYBACK_MODE_SAMPLE:
                if( el->has_audio )
                    num_samples = vj_perform_fill_audio_buffers(info,a_buf, audio_render_buffer, down_sample_buffer, &play_audio_sample_);

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
        
        vj_perform_play_audio( settings, a_buf, (num_samples * bps ));
     }  
#endif
    return 1;
}

int vj_perform_get_width( veejay_t *info )
{
#ifdef HAVE_GL
    if(info->video_out == 3 )
        return  x_display_width(info->gl);
#endif
#ifdef HAVE_SDL
    if(info->video_out <= 1 )
        return vj_sdl_screen_w(info->sdl[0]);
#endif
    return info->video_output_width;
}

int vj_perform_get_height( veejay_t *info )
{
#ifdef HAVE_GL
    if(info->video_out == 3 )
        return x_display_height(info->gl);
#endif
#ifdef HAVE_SDL
    if(info->video_out <= 1 )
        return vj_sdl_screen_h(info->sdl[0]);
#endif
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

static  void    vj_perform_render_osd( veejay_t *info, video_playback_setup *settings, int destination )
{
    if(info->use_osd <= 0 ) 
        return;

    VJFrame *frame = info->effect_frame1; 
    frame->data[0] = primary_buffer[destination]->Y;
    frame->data[1] = primary_buffer[destination]->Cb;
    frame->data[2] = primary_buffer[destination]->Cr;

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

static  void    vj_perform_finish_chain( veejay_t *info, int is444 )
{
    VJFrame frame;
    veejay_memcpy(&frame,info->effect_frame1,sizeof(VJFrame));

    frame.data[0] = primary_buffer[0]->Y;
    frame.data[1] = primary_buffer[0]->Cb;
    frame.data[2] = primary_buffer[0]->Cr;
    frame.data[3] = primary_buffer[0]->alpha;
    frame.ssm     = is444;

    int result = 0;

    if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
    {
        result = vj_perform_post_chain_tag(info,&frame);
    }
    else if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE )
    {
        result = vj_perform_post_chain_sample(info,&frame);
    }

    if( result ) {
        pvar_.follow_run = result;
    }
}

static  void    vj_perform_finish_render( veejay_t *info, video_playback_setup *settings, int destination )
{
    VJFrame *frame = info->effect_frame1;
    VJFrame *frame2= info->effect_frame2;
    uint8_t *pri[4];
    char *osd_text = NULL;
    char *more_text = NULL;
    int   placement= 0;

    pri[0] = primary_buffer[destination]->Y;
    pri[1] = primary_buffer[destination]->Cb;
    pri[2] = primary_buffer[destination]->Cr;
    pri[3] = primary_buffer[destination]->alpha;

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
                vj_sdl_grab( info->sdl[0], 0 );
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
                if( vj_effect_has_rgbkey( fx_id ) ) {
                    sample_set_effect_arg( info->uc->sample_id, pos, 1, r );
                    sample_set_effect_arg( info->uc->sample_id, pos, 2, g );
                    sample_set_effect_arg( info->uc->sample_id, pos, 3, b );
                    veejay_msg(VEEJAY_MSG_INFO,"Selected RGB color #%02x%02x%02x",r,g,b);
                }
            }
            else if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
                int pos = vj_tag_get_selected_entry(info->uc->sample_id);
                int fx_id = vj_tag_get_effect( info->uc->sample_id, pos );
                if( vj_effect_has_rgbkey( fx_id ) ) {
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
    
    if( frame->ssm == 1 )
    {
        chroma_subsample(settings->sample_mode,frame,pri); 
        vj_perform_set_422(frame);
    }

    if( frame2->ssm == 1 )
    {
        vj_perform_set_422(frame2);
    }
}

static  void    vj_perform_render_font( veejay_t *info, video_playback_setup *settings )
{
    VJFrame *frame = info->effect_frame1;

    frame->data[0] = primary_buffer[0]->Y;
    frame->data[1] = primary_buffer[0]->Cb;
    frame->data[2] = primary_buffer[0]->Cr;

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

//FIXME refactor recorders - there only needs to be two: online (what is playing) , offline (what is not playing but active)
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

static  int vj_perform_render_magic( veejay_t *info, video_playback_setup *settings, int is444 )
{
    int deep = 0;
    vj_perform_finish_chain( info, is444 );

    vj_perform_render_font( info, settings);

    if(!info->settings->composite)
        vj_perform_render_osd( info, settings, deep );

    vj_perform_finish_render( info, settings,deep );

    return deep;
}

void    vj_perform_record_video_frame(veejay_t *info)
{
    if( pvar_.enc_active )
        vj_perform_record_frame(info);
}

int vj_perform_queue_video_frame(veejay_t *info, const int skip_incr)
{
    video_playback_setup *settings = info->settings;
    if(skip_incr)
        return 0;

    int is444 = 0;
    int res = 0;
    int i = 0;
    int safe_ff = pvar_.follow_fade;
    int safe_fv = pvar_.fade_value;

    is_supersampled = 0;

    veejay_memset( &pvar_, 0, sizeof(varcache_t));
    
    pvar_.follow_fade = safe_ff;
    pvar_.fade_value = safe_fv;
    pvar_.fade_entry = -1;

    int cur_out = info->out_buf;

    info->effect_frame_info->timecode = settings->current_frame_num;
    info->effect_frame1->ssm = 0;
    info->effect_frame2->ssm = 0;
    info->effect_frame1->timecode = (double) settings->current_frame_num;

    for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
        frame_buffer[i]->ssm = -1;
    }

    switch (info->uc->playback_mode)
    {
        case VJ_PLAYBACK_MODE_SAMPLE:

            sample_var( info->uc->sample_id,
                        &(pvar_.type),
                        &(pvar_.fader_active),
                        &(pvar_.fx_status),
                        &(pvar_.enc_active),
                        &(pvar_.active),
                        &(pvar_.fade_method),
                        &(pvar_.fade_entry),
                        &(pvar_.fade_alpha));

            if( (info->seq->active && info->seq->rec_id) || info->settings->offline_record )
                pvar_.enc_active = 1;
            
            vj_perform_plain_fill_buffer(info, &res);
            if( res > 0 ) {  
                cached_sample_frames[0] = info->uc->sample_id;

                if(vj_perform_verify_rows(info))
                    vj_perform_sample_complete_buffers(info, &is444);

                cur_out = vj_perform_render_magic( info, info->settings,is444 );
            }
            break;
            
        case VJ_PLAYBACK_MODE_PLAIN:

            if( info->settings->offline_record )
                pvar_.enc_active = 1;

            vj_perform_plain_fill_buffer(info, &res);
            if( res > 0 ) {
                cur_out = vj_perform_render_magic( info, info->settings,0 );
            }
            break;
        case VJ_PLAYBACK_MODE_TAG:

            vj_tag_var( info->uc->sample_id,
                        &(pvar_.type),
                        &(pvar_.fader_active),
                        &(pvar_.fx_status),
                        &(pvar_.enc_active),
                        &(pvar_.active),
                        &(pvar_.fade_method),
                        &(pvar_.fade_entry),
                        &(pvar_.fade_alpha));
    
            if( (info->seq->active && info->seq->rec_id) || info->settings->offline_record )
                pvar_.enc_active = 1;

            vj_perform_tag_fill_buffer(info);

            if(vj_perform_verify_rows(info))
                vj_perform_tag_complete_buffers(info, &is444);
                
            cur_out = vj_perform_render_magic( info, info->settings, is444 );
            res = 1;     
            break;
        default:
            return 0;
    }

    info->out_buf = cur_out;
    
    if( info->settings->feedback && info->settings->feedback_stage == 1 ) 
    {
        int idx = info->out_buf;
        uint8_t *dst[4] = { 
            primary_buffer[idx]->Y,
            primary_buffer[idx]->Cb,
            primary_buffer[idx]->Cr,
            primary_buffer[idx]->alpha };

        vj_perform_copy3( dst,feedback_buffer, info->effect_frame1->len, info->effect_frame1->uv_len,info->effect_frame1->stride[3] * info->effect_frame1->height  );

        info->settings->feedback_stage = 2;
    }

    return res;
}

int vj_perform_queue_frame(veejay_t * info, int skip )
{
    video_playback_setup *settings = (video_playback_setup*) info->settings;
    int ret_val = 0;

    if( pvar_.follow_run ) {
        veejay_msg(VEEJAY_MSG_DEBUG, "Following to [%d,%d]", pvar_.follow_now[0], pvar_.follow_now[1] );

        if( pvar_.follow_now[1] == 0 ) {
            info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
            veejay_set_sample( info, pvar_.follow_now[0] );
        } else {
            info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
            veejay_set_sample( info, pvar_.follow_now[0] );
        }

        pvar_.follow_run    = 0;
        pvar_.follow_now[0] = 0;
        pvar_.follow_now[1] = 0;
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

        //@ increase tick
        vj_perform_clear_cache();
    
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
    char timecode[15];
    int use = ( take_n == track_dup ? 0: 1 );

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

    MPEG_timecode_t tc;
    y4m_ratio_t ratio = mpeg_conform_framerate( (double)info->current_edit_list->video_fps );
    mpeg_timecode(&tc,max_delay,mpeg_framerate_code(ratio),info->current_edit_list->video_fps );
    sprintf(timecode, "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f);
    veejay_msg(VEEJAY_MSG_DEBUG, "Sample randomizer trigger in %s", timecode );

    veejay_set_sample( info, take_n );
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

