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
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvevo/libvevo.h>
#include <veejay/vj-macro.h>
#include <veejay/vims.h>

#define MAX_MACROS 16
#define MAX_MACRO_BANKS 12


/**
 * MACRO|keystrokes|VIMS macro
 *
 * in vj-event.c, macro banks are selected by [CTRL] + [F1...F12] (or via VIMS 34) bound to vj_macro_select
 *
 * select a macro bank will allocate it, and you can record vims messages to it
 *
 * the event handler pulls these messages and replays them
 *
*/

typedef struct {
	char *msg[MAX_MACROS];
	uint8_t num_msg;
} macro_block_t;

typedef struct {
	void *macro_bank[MAX_MACRO_BANKS];
	uint8_t current_bank;
	uint8_t status;
} vj_macro_t;

//static void vj_macro_print(vj_macro_t *macro);

static  int vvm_[VIMS_QUIT];

void *vj_macro_new(void)
{
	vj_macro_t *macro = (vj_macro_t*) vj_calloc(sizeof(vj_macro_t));
	vj_macro_select(macro,0);
	return (void*) macro;
}

void vj_macro_free(void *ptr)
{
	vj_macro_t *macro = (vj_macro_t*) ptr;
	int i;
	for(macro->current_bank = 0; macro->current_bank < MAX_MACRO_BANKS; macro->current_bank ++ ) {
		vj_macro_clear(ptr);
	}
	
	free(macro);
}

void vj_macro_set_status(void *ptr, uint8_t status)
{
	vj_macro_t *macro = (vj_macro_t*) ptr;
	macro->status = status;
}

uint8_t vj_macro_get_status(void *ptr) {
	vj_macro_t *macro = (vj_macro_t*) ptr;
	return macro->status;
}

// caller frees array of pointers, not contents
// returns NULL if there are no macros recorded at this position
char **vj_macro_pull(void *ptr, long frame_num, int at_loop, int at_dup)
{
	vj_macro_t *macro = (vj_macro_t*) ptr;
	char key[32];
	snprintf(key,sizeof(key),"%08ld%02d%08d", frame_num, at_dup, at_loop );

	void *mb = NULL;
	int error = vevo_property_get(macro->macro_bank[ macro->current_bank ], key, 0, &mb );
	if( error != VEVO_NO_ERROR )
		return NULL;
	macro_block_t *m = (macro_block_t*) mb;

	char **messages = (char**) vj_calloc(sizeof(char*) * (m->num_msg + 1));
	for( int i = 0; i < m->num_msg; i ++ ) {
		messages[i] = m->msg[ i ];
//		veejay_msg(VEEJAY_MSG_DEBUG, "playback VIMS [%s] at position %ld.%d, loop %d", messages[i], frame_num, at_dup, at_loop );
	}

	return messages;
}

// store a macro at the given position
int vj_macro_put(void *ptr, char *message, long frame_num, int at_loop, int at_dup)
{
	vj_macro_t *macro = (vj_macro_t*) ptr;
	char key[32];
	snprintf(key,sizeof(key),"%08ld%02d%08d", frame_num, at_dup, at_loop );

	void *mb = NULL;
	int error = vevo_property_get(macro->macro_bank[ macro->current_bank ], key,0, &mb );
	macro_block_t *m = NULL;
	if( error != VEVO_NO_ERROR ) {
		m = (macro_block_t*) vj_calloc(sizeof(macro_block_t));

		vevo_property_set(macro->macro_bank[ macro->current_bank ], key, VEVO_ATOM_TYPE_VOIDPTR, 1, &m );
	}
	else {
		m = (macro_block_t*) mb;
		if(m->num_msg == MAX_MACROS ) {
			return 0; // no more space
		}
	}	

	m->msg[ m->num_msg ] = strdup(message);
	m->num_msg = m->num_msg + 1;
	
//	veejay_msg(VEEJAY_MSG_DEBUG, "record VIMS [%s]/%d at position %ld.%d, loop %d", message,m->num_msg, frame_num, at_dup, at_loop );
	
	return 1;
}

void vj_macro_clear(void *ptr)
{
	vj_macro_t* macro = (vj_macro_t*) ptr;

	char **items = vevo_list_properties( macro->macro_bank[ macro->current_bank ] );
	if(items == NULL)
		return;

	int i,j;
	for( i = 0; items[i] != NULL; i ++ ) {
		void *mb = NULL;
		if( vevo_property_get( macro->macro_bank[ macro->current_bank ], items[i], 0, &mb ) == VEVO_NO_ERROR ) {
			macro_block_t *m = (macro_block_t*) mb;
			for( j = 0; j < m->num_msg; j ++ ) {
				free(m->msg[j]);
			}	
		}
		free(items[i]);
	}
	free(items);

	vpf(macro->macro_bank[ macro->current_bank ]);
	macro->macro_bank[macro->current_bank] = NULL;
}

int vj_macro_select( void *ptr, int slot )
{
	vj_macro_t *macro = (vj_macro_t*) ptr;

	if( slot > MAX_MACRO_BANKS )
		return 0;
	if( slot == -1 ) {
		slot = macro->current_bank;
	}

	if(macro->macro_bank[slot] == NULL) {
		macro->macro_bank[slot] = vpn( VEVO_ANONYMOUS_PORT );
		if(!macro->macro_bank[slot]) {
			return 0;
		}
	}

	macro->current_bank = slot;
	return 1;
}
/*
static void vj_macro_print(vj_macro_t *macro)
{
	char **items = vevo_list_properties( macro->macro_bank[ macro->current_bank ] );
	if(items == NULL) {
		veejay_msg(VEEJAY_MSG_DEBUG, "No VIMS events have been recorded in bank %d", macro->current_bank);
		return;
	}

	int i,j;
	for( i = 0; items[i] != NULL; i ++ ) {
		void *mb = NULL;
		if( vevo_property_get( macro->macro_bank[ macro->current_bank ], items[i], 0, &mb ) == VEVO_NO_ERROR ) {
			macro_block_t *m = (macro_block_t*) mb;
			long n_frame = 0;
			int at_dup = 0;
			int at_loop = 0;
			sscanf( items[i], "%08ld%02d%08d", &n_frame, &at_dup, &at_loop );
			for( j = 0; j < m->num_msg; j ++ ) {
				veejay_msg(VEEJAY_MSG_DEBUG,"VIMS [%s] at frame position %d.%d, loop %d", m->msg[j], n_frame, at_dup, at_loop );
			}	
		}
		free(items[i]);
	}
	free(items);
}
*/

void vj_macro_init(void)
{
    veejay_memset( vvm_,1, sizeof(vvm_));
    vvm_[VIMS_MACRO] = 0;
    vvm_[VIMS_TRACK_LIST] = 0;
    vvm_[VIMS_RGB24_IMAGE] = 0;
    vvm_[VIMS_SET_SAMPLE_START] =0;
    vvm_[VIMS_SET_SAMPLE_END] = 0;
    vvm_[VIMS_SAMPLE_NEW] = 0;
    vvm_[VIMS_SAMPLE_DEL] = 0;
    vvm_[VIMS_STREAM_DELETE] = 0;
    vvm_[VIMS_SAMPLE_LOAD_SAMPLELIST]=0;
    vvm_[VIMS_SAMPLE_SAVE_SAMPLELIST]=0;
    vvm_[VIMS_SAMPLE_DEL_ALL] = 0;
    vvm_[VIMS_SAMPLE_COPY] = 0;
    vvm_[VIMS_SAMPLE_UPDATE] = 0;
    vvm_[VIMS_SAMPLE_KF_GET]=0;
    vvm_[VIMS_SAMPLE_KF_RESET]=0;
    vvm_[VIMS_SAMPLE_KF_STATUS]=0;
    vvm_[VIMS_STREAM_NEW_V4L] = 0;
    vvm_[VIMS_STREAM_NEW_DV1394] = 0;
    vvm_[VIMS_STREAM_NEW_COLOR] = 0;
    vvm_[VIMS_STREAM_NEW_Y4M] = 0;
    vvm_[VIMS_STREAM_NEW_UNICAST]=0;
    vvm_[VIMS_STREAM_NEW_MCAST]=0;
    vvm_[VIMS_STREAM_NEW_PICTURE]=0;
	vvm_[VIMS_STREAM_NEW_AVFORMAT]=0;
    vvm_[VIMS_STREAM_SET_DESCRIPTION]=0;
    vvm_[VIMS_SAMPLE_SET_DESCRIPTION]=0;
    vvm_[VIMS_STREAM_SET_LENGTH]=0;
    vvm_[VIMS_SEQUENCE_STATUS]=0;
    vvm_[VIMS_SEQUENCE_ADD]=0;
    vvm_[VIMS_SEQUENCE_DEL]=0;
    vvm_[VIMS_CHAIN_LIST]=0;
    vvm_[VIMS_OUTPUT_Y4M_START]=0;
    vvm_[VIMS_OUTPUT_Y4M_STOP]=0;
    vvm_[VIMS_GET_FRAME]=0;
    vvm_[VIMS_VLOOPBACK_START]=0;
    vvm_[VIMS_VLOOPBACK_STOP]=0;
    vvm_[VIMS_VIDEO_MCAST_START]=0;
    vvm_[VIMS_VIDEO_MCAST_STOP]=0;
    vvm_[VIMS_SYNC_CORRECTION]=0;
    vvm_[VIMS_NO_CACHING]=0;
    vvm_[VIMS_SCREENSHOT]=0;
    vvm_[VIMS_RGB_PARAMETER_TYPE]=0;
    vvm_[VIMS_RESIZE_SDL_SCREEN] =0;
    vvm_[VIMS_DEBUG_LEVEL]=0;
    vvm_[VIMS_SAMPLE_MODE]=0;
    vvm_[VIMS_BEZERK] = 0;
    vvm_[VIMS_AUDIO_ENABLE]=0;
    vvm_[VIMS_AUDIO_DISABLE]=0;
    vvm_[VIMS_RECORD_DATAFORMAT]=0;
    vvm_[VIMS_INIT_GUI_SCREEN]=0;
    vvm_[VIMS_SUSPEND]=0;
    vvm_[VIMS_VIEWPORT]=0;
    vvm_[VIMS_PREVIEW_BW]=0;
    vvm_[VIMS_FRONTBACK]=0;
    vvm_[VIMS_RECVIEWPORT]=0;
    vvm_[VIMS_PROJECTION] = 0;
}

int vj_macro_is_vims_accepted(int net_id)
{
    if(net_id > 400 || net_id >= 388 || (net_id >= 80 && net_id <= 86) || (net_id >= 50 && net_id <= 59))
        return 0;

    return vvm_[net_id];
}

