#include <config.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <libvje/internal.h>
#include <libsample/sampleadm.h>
#include <libvje/libvje.h>
#include <libplugger/plugload.h>
#include <libstream/vj-cali.h>
#include <veejaycore/vevo.h>
#include <veejaycore/libvevo.h>

extern void vje_fx_apply_collective( int fx_id, void *ptr, VJFrame *A, VJFrame *B, int *args );

static pthread_mutex_t vjert_frame_lock = PTHREAD_MUTEX_INITIALIZER;

void vjert_frame_begin(void)
{
    pthread_mutex_lock(&vjert_frame_lock);
}

void vjert_frame_end(void)
{
    pthread_mutex_unlock(&vjert_frame_lock);
}

static int vjert_new_fx( sample_eff_chain *entry,int chain_id, int chain_position, VJFrame *frame)
{
    if( entry->fx_instance == NULL ) {
        int error = 0;
        entry->fx_instance = vje_fx_malloc( entry->effect_id, chain_id, chain_position, frame->width, frame->height, &error );
        if(error) {
            return 0;
        }
        if(entry->fx_instance) {
            vje_fx_prepare( entry->effect_id, entry->fx_instance, frame );
        }
        return 1;
    }
    return 0;
}

void vjert_del_fx( void *ptr, int chain_id, int chain_position, int clear ) {
    sample_eff_chain *entry  = (sample_eff_chain*) ptr;
    pthread_mutex_lock(&vjert_frame_lock);
    if( entry->fx_instance && entry->fx_instance != (void*)0x1 ) {
        if( entry->effect_id >= VJ_PLUGIN ) {
            plug_deactivate( entry->fx_instance );
        }
        else {
            vje_fx_free( entry->effect_id, chain_id, chain_position, entry->fx_instance );
        }
    }
    entry->fx_instance = NULL;

    if( clear ) {
        entry->effect_id = -1;
        entry->beat_flag = 0;
        entry->beat_param_mask = SAMPLE_BEAT_PARAM_MASK_ALL;
        if( entry->kf ) {
            vpf( entry->kf );
            entry->kf = NULL;
        }
        entry->kf = vpn( VEVO_ANONYMOUS_PORT );
    }
    pthread_mutex_unlock(&vjert_frame_lock);
}

static void vjert_process_plugin( int effect_id, void *fx_instance, VJFrame **frames, int *args )
{
    const int plug_id = vje_get_plugin_id( effect_id );
    int num_inputs = plug_get_num_input_channels( plug_id );
    int num_params = vje_get_num_params( effect_id );
    if(num_inputs < 0 || num_inputs > 2) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "Plugin %d requires %d inputs; the effect chain supports at most 2",
                   plug_id, num_inputs);
        return;
    }
    VJFrame *output =
        plug_get_num_output_channels(plug_id) > 0 ? frames[0] : NULL;

    plug_process_frame(fx_instance, frames, num_inputs, output,
                       args, num_params, frames[0]->timecode);
}

int vjert_prepare_frame( void *ptr, int chain_id, int chain_position,
                         VJFrame *frame, int *effect_id, int *e_flag,
                         void **fx_instance )
{
    sample_eff_chain *entry = (sample_eff_chain*) ptr;
    const int frozen_effect_id = entry->effect_id;
    const int frozen_e_flag = entry->e_flag;

    *effect_id = frozen_effect_id;
    *e_flag = frozen_e_flag;
    *fx_instance = NULL;

    if(!frozen_e_flag || frozen_effect_id <= 0) {
        if(!frozen_e_flag && frozen_effect_id >= VJ_PLUGIN &&
           entry->fx_instance && entry->fx_instance != (void*)0x1)
            plug_reset(entry->fx_instance);
        return 0;
    }

    if(frozen_effect_id >= VJ_PLUGIN) {
        if(entry->fx_instance == NULL) {
            const int plug_id = vje_get_plugin_id(frozen_effect_id);
            entry->fx_instance = plug_activate(plug_id);
        }
        if(entry->fx_instance == NULL)
            return 0;
    }
    else if(entry->fx_instance == NULL) {
        if(vje_fx_needs_instance(frozen_effect_id)) {
            if(!vjert_new_fx(entry, chain_id, chain_position, frame))
                return 0;
        }
        else {
            entry->fx_instance = (void*) 0x1;
        }
    }

    *fx_instance = entry->fx_instance;
    return 1;
}

void vjert_apply_frame( int effect_id, int e_flag, void *fx_instance,
                        VJFrame **frames, int *args )
{
    if(!e_flag || effect_id <= 0 || fx_instance == NULL)
        return;

    if(effect_id >= VJ_PLUGIN) {
#pragma omp single
        {
            vjert_process_plugin(effect_id, fx_instance, frames, args);
        }
        return;
    }

    void *instance_ptr = (fx_instance == (void*)0x1) ? NULL : fx_instance;
    vje_fx_apply_collective(effect_id, instance_ptr, frames[0], frames[1], args);
}

void vjert_apply( void *ptr, VJFrame **frames, int chain_id, int chain_position, int *args )
{
    int effect_id = -1;
    int e_flag = 0;
    void *fx_instance = NULL;

    if(!vjert_prepare_frame(ptr, chain_id, chain_position, frames[0],
                            &effect_id, &e_flag, &fx_instance))
        return;

    if(effect_id >= VJ_PLUGIN) {
        vjert_process_plugin(effect_id, fx_instance, frames, args);
    }
    else {
        void *instance_ptr = (fx_instance == (void*)0x1) ? NULL : fx_instance;
        vje_fx_apply(effect_id, instance_ptr, frames[0], frames[1], args);
    }
}

void vjert_update( void *ptr, VJFrame *frame )
{
    sample_eff_chain **chain = (sample_eff_chain**) ptr;
    int i;

    pthread_mutex_lock(&vjert_frame_lock);
    vje_set_bg(frame);

    for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
        sample_eff_chain *entry = chain[i];
        if(entry->fx_instance && entry->fx_instance != (void*)0x1 &&
           entry->effect_id > 0 && entry->effect_id < VJ_PLUGIN) {
            vje_fx_prepare( entry->effect_id, entry->fx_instance, frame );
        }
    }
    pthread_mutex_unlock(&vjert_frame_lock);
}
