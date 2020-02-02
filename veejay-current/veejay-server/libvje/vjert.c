/* 
 * veejay  
 *
 * Copyright (C) 2000-2019 Niels Elburg <nwelburg@gmail.com>
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
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <libvje/internal.h>
#include <libsample/sampleadm.h>
#include <libvje/libvje.h>
#include <libplugger/plugload.h>
#include <veejaycore/vevo.h>
#include <veejaycore/libvevo.h>

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
    if( entry->fx_instance ) {
        if( entry->effect_id >= VJ_PLUGIN ) {
            plug_deactivate( entry->fx_instance );
        }
        else {
            vje_fx_free( entry->effect_id, chain_id, chain_position, entry->fx_instance );
        }

        entry->fx_instance = NULL;
    }

    if( clear ) {
        entry->effect_id = -1;
        if( entry->kf ) {
            vpf( entry->kf );
            entry->kf = NULL;
        }
        entry->kf = vpn( VEVO_ANONYMOUS_PORT );
    }
}

static void vjert_process_fx( sample_eff_chain *entry, VJFrame **frames, int chain_id, int chain_position, int *args )
{
    int doProcess = 0;
    if( entry->fx_instance == NULL && entry->effect_id > 0 ) {
        doProcess = vjert_new_fx( entry, chain_id, chain_position, frames[0] );
    }

    if( entry->fx_instance && entry->e_flag ) {
        doProcess = 1;
    }

    if( doProcess ) {
        vje_fx_apply( entry->effect_id, entry->fx_instance, frames[0], frames[1], args );
    }
}

static void vjert_process_plugin( sample_eff_chain *entry, VJFrame **frames, int *args )
{
    const int plug_id = vje_get_plugin_id( entry->effect_id );
    int num_inputs = plug_get_num_input_channels( plug_id );
    int num_params = vje_get_num_params( entry->effect_id );
    int i;

    if( entry->fx_instance == NULL ) {
        entry->fx_instance = plug_activate( plug_id );
    }

    if( entry->fx_instance == NULL || entry->e_flag == 0 ) {
        return;
    }

    if( plug_is_frei0r( entry->fx_instance ) ) {
        plug_set_parameters( entry->fx_instance, num_params, args);
    }
    else {
        for( i = 0; i < num_params; i ++ ) {
            plug_set_parameter( entry->fx_instance, i, 1, &(args[i]) );
        }
    }

    for( i = 0; i < num_inputs; i ++ ) {
        plug_push_frame( entry->fx_instance, 0, i, frames[i] );
    }

    if( plug_get_num_output_channels( plug_id ) > 0 ) {
        plug_push_frame( entry->fx_instance, 1, 0, frames[0] );
    }

    plug_process( entry->fx_instance, frames[0]->timecode );

}


void vjert_apply( void *ptr, VJFrame **frames, int chain_id, int chain_position, int *args )
{
    sample_eff_chain *entry = (sample_eff_chain*) ptr;
    if( entry->effect_id >= VJ_PLUGIN ) {
        vjert_process_plugin( entry, frames,args );
    }
    else {
        vjert_process_fx( entry, frames, chain_id, chain_position, args );
    }
}

void vjert_update( void *ptr, VJFrame *frame )
{
    sample_eff_chain **chain = (sample_eff_chain**) ptr;
    int i;
    for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
        sample_eff_chain *entry = chain[i];
        if(entry->fx_instance) {
            vje_fx_prepare( entry->effect_id, entry->fx_instance, frame );
        }
    }
}


