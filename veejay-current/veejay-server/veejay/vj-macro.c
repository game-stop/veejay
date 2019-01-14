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
#include <veejay/vims.h>
#ifdef HAVE_XML2
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libvjxml/vj-xml.h>
#endif
#include <veejay/vj-macro.h>

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
	int loop_stat_stop[MAX_MACRO_BANKS];
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
int vj_macro_put(void *ptr, char *message, long frame_num, int at_dup, int at_loop)
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
	
	//veejay_msg(VEEJAY_MSG_DEBUG, "VIMS [%s]/%d at position %ld.%d, loop %d", message,m->num_msg, frame_num, at_dup, at_loop );
	
	return 1;
}

static  void vj_macro_reclaim_block( macro_block_t *blk, int seq_no )
{
    int i=0;
    if( blk->num_msg == 0 )
        return;

    /* delete all VIMS messages if seq_no equals -1 */
    if( seq_no == -1 ) {
        for( i = 0;i < blk->num_msg; i ++ ) {
            if(blk->msg[i]) {
                free( blk->msg[i] );
                blk->msg[i] = NULL;
            }
        }
        blk->num_msg = 0;
        return;
    }

    /* delete VIMS message */
    if(blk->msg[seq_no] != NULL) {
        free( blk->msg[seq_no] );
        blk->msg[seq_no] = NULL;
    }

    /* permutate message pointers in block */
    for( i = (seq_no + 1); i < blk->num_msg ; i ++ ) {
        blk->msg[i - 1] = blk->msg[i];
    }
    blk->msg[ blk->num_msg - 1 ] = NULL;
    
    blk->num_msg --;

}

void    vj_macro_del(void *ptr, long frame_num, int at_dup, int at_loop, int seq_no)
{
    vj_macro_t *macro = (vj_macro_t*) ptr;
	char key[32];
	snprintf(key,sizeof(key),"%08ld%02d%08d", frame_num, at_dup, at_loop );

    void *mb = NULL;
	int error = vevo_property_get(macro->macro_bank[ macro->current_bank ], key,0, &mb );
	macro_block_t *m = NULL;

    if( error == VEVO_NO_ERROR ) {
        m = (macro_block_t*) mb;
        vj_macro_reclaim_block( m, seq_no );
    }
}

void vj_macro_clear_bank(void *ptr, int bank)
{
	vj_macro_t* macro = (vj_macro_t*) ptr;
	if(bank < 0 || bank > MAX_MACRO_BANKS ) 
	       return;
	vpf( macro->macro_bank[ bank ] );
	macro->macro_bank[ bank ] = NULL;

	vj_macro_select( ptr, bank );
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

int vj_macro_get_loop_stat_stop( void *ptr )
{
	vj_macro_t *macro = (vj_macro_t*) ptr;
	return macro->loop_stat_stop[ macro->current_bank ];	
}

int	vj_macro_set_loop_stat_stop( void *ptr, int stop)
{
	vj_macro_t *macro = (vj_macro_t*) ptr;
    if( stop == 0 ) {
        macro->loop_stat_stop[ macro->current_bank ] = 0;
    } else {
        // set new stop position only if greater than current stop position
        if( stop > macro->loop_stat_stop[ macro->current_bank ] )
        	macro->loop_stat_stop[ macro->current_bank ] = stop;
    }
    return 1;
}

static char** vj_macro_get_messages(void *ptr, long frame_num, int at_dup, int at_loop, int *len)
{
	int i;
	char **messages = vj_macro_pull(ptr, frame_num, at_loop, at_dup);
	if(messages != NULL) {
        int msglen = 0;
		for( i = 0; messages[i] != NULL; i ++ ) {
			msglen += strlen(messages[i]);
			msglen += (8 + 2 + 8 + 2 + 3);
		}
		*len += msglen;
	}
	return messages;
}

static char *vj_macro_flatten_messages(void *ptr, char **messages, long frame_num, int at_dup, int at_loop, int len)
{
	int i;
	char *buf = vj_calloc(sizeof(char) * (len + 1));
	char *buf_ptr = buf;

	for( i = 0; messages[i] != NULL; i ++ ) {
		int msg_len = strlen(messages[i]);
		sprintf(buf_ptr, "%08ld%02d%08d%02d%03d%s", frame_num, at_dup, at_loop, i,msg_len, messages[i] );
		buf_ptr += (msg_len + 8 +2 + 8 + 2 + 3);
	}

	int buf_len = strlen(buf);
	if( buf_len != len ) {
		veejay_msg(0, "Length is calculated incorrectly: was %d, expected %d", buf_len, len);
	}

	return buf;
}

char* vj_macro_serialize_macro(void *ptr, long frame_num, int at_dup, int at_loop )
{
	int len = 0;
	char **messages = vj_macro_get_messages(ptr, frame_num, at_dup, at_loop, &len );
	if(len == 0) {
		return NULL;
	}		
	return vj_macro_flatten_messages( ptr, messages, frame_num, at_dup, at_loop, len );
}

static int vj_macro_get_serialized_length(void *ptr, char **items)
{
	int i;
	int len = 0;
	/* calculate data length */
	for( i = 0; items[i] != NULL;  i++ ) {
		long frame_num = 0;
    	int  at_loop = 0;
    	int  at_dup = 0;

    	if( sscanf( items[i], "%08ld%02d%08d", &frame_num, &at_dup, &at_loop ) == 3 ) {
			char **messages = vj_macro_get_messages(ptr, frame_num, at_dup, at_loop, &len );
			if(messages) {
				free(messages);
			}
		}
	}
	return len;
}

char *vj_macro_serialize(void *ptr)
{
 	vj_macro_t *macro = (vj_macro_t*) ptr;
	char **items = vevo_list_properties( macro->macro_bank[ macro->current_bank ] );
	if(items == NULL) {
		return NULL;
	}
	
	int len = vj_macro_get_serialized_length(ptr, items);
	
	size_t data_len = 8 + len + 1;
	char *buf = (char*) vj_calloc(sizeof(char) * data_len);
	char *save_ptr = buf;
	char *buf_ptr = buf + 8;
	int i;
	for( i = 0; items[i] != NULL; i ++ ) {
		long frame_num = 0;
    	int  at_loop = 0;
    	int  at_dup = 0;

    	if( sscanf( items[i], "%08ld%02d%08d", &frame_num, &at_dup, &at_loop ) == 3 ) {
			char *macro_str = vj_macro_serialize_macro( ptr, frame_num, at_dup, at_loop );
			if(macro_str == NULL) {
				free(items[i]);
				continue;
			}

			int macro_len = strlen(macro_str);
            memcpy( buf_ptr, macro_str, macro_len );
			buf_ptr += (macro_len);
		}
		free(items[i]);
	}

	free(items);

	char header[9];
	sprintf(header, "%08d", (int) len );
	memcpy(buf, header, 8);
	
	return save_ptr;
}

#ifdef HAVE_XML2
static void vj_macro_store_bank(void *ptr, xmlNodePtr node)
{
    vj_macro_t *macro = (vj_macro_t*) ptr;
	char **items = vevo_list_properties( macro->macro_bank[ macro->current_bank ] );
	if(items == NULL) {
		return;
	}

    xmlNodePtr mnode = xmlNewChild( node, NULL, (const xmlChar*) XMLTAG_MACRO_BANK, NULL );

	put_xml_int( mnode, XMLTAG_MACRO_LOOP_STAT_STOP, macro->loop_stat_stop[ macro->current_bank ] );

    if( macro->status == MACRO_STOP || macro->status == MACRO_PLAY ) {
        put_xml_int( mnode, XMLTAG_MACRO_STATUS, macro->status );
    }

	int i,j;
	for( i = 0; items[i] != NULL; i ++ ) {
		void *mb = NULL;
		if( vevo_property_get( macro->macro_bank[ macro->current_bank ], items[i], 0, &mb ) == VEVO_NO_ERROR ) {
			macro_block_t *m = (macro_block_t*) mb;
            put_xml_str( mnode, XMLTAG_MACRO_KEY, items[i] );
			    
            xmlNodePtr childnode = xmlNewChild( mnode, NULL, (const xmlChar*) XMLTAG_MACRO_MESSAGES, NULL );

			for( j = 0; j < m->num_msg; j ++ ) {
			    put_xml_str( childnode, XMLTAG_MACRO_MSG, m->msg[j] );
            }	
		}
		free(items[i]);
	}
	free(items);
}

void vj_macro_store( void *ptr, xmlNodePtr node )
{
    vj_macro_t *macro = (vj_macro_t*) ptr;
    int i;
    int cur = macro->current_bank;
    for( i = 0; i < MAX_MACRO_BANKS; i ++ ) {
        macro->current_bank = i;
        vj_macro_store_bank( ptr, node );
    }
    macro->current_bank = cur;
}

static void vj_macro_load_messages( void *ptr, char *key, xmlDocPtr doc, xmlNodePtr cur )
{
    long frame_num = 0;
    int  at_loop = 0;
    int  at_dup = 0;

    if( sscanf( key, "%08ld%02d%08d", &frame_num, &at_dup, &at_loop ) == 3 ) {
        while(cur != NULL) {
            if( !xmlStrcmp( cur->name, (const xmlChar*) XMLTAG_MACRO_MSG )) {
                char *msg = get_xml_str(doc, cur );
                vj_macro_put( ptr, msg, frame_num, at_dup, at_loop );
                free(msg);
            }
            cur = cur->next;
        }
    }
}

static void vj_macro_load_bank( void *ptr, xmlDocPtr doc, xmlNodePtr cur )
{
	vj_macro_t* macro = (vj_macro_t*) ptr;
    char *key = NULL;
    while( cur != NULL ) {

		if( !xmlStrcmp( cur->name, (const xmlChar*) XMLTAG_MACRO_LOOP_STAT_STOP)) {
			int stop = get_xml_int(doc, cur);
			macro->loop_stat_stop[ macro->current_bank ] = stop;
		}

        if( !xmlStrcmp( cur->name, (const xmlChar*) XMLTAG_MACRO_STATUS )) {
            int status = get_xml_int(doc, cur);
            macro->status = (uint8_t) status;
        }

        if( !xmlStrcmp( cur->name, (const xmlChar*) XMLTAG_MACRO_KEY)) {
            key = get_xml_str( doc, cur );
        }
        
        if( !xmlStrcmp( cur->name, (const xmlChar*) XMLTAG_MACRO_MESSAGES)) {
            vj_macro_load_messages( ptr, key, doc, cur->xmlChildrenNode );
            free(key);
        }
      
        cur = cur->next;
    }
}

void vj_macro_load( void *ptr, xmlDocPtr doc, xmlNodePtr cur)
{   
    vj_macro_t *macro = (vj_macro_t*) ptr;
    macro->current_bank = 0;
    while( cur != NULL ) {
        if(!xmlStrcmp( cur->name, (const xmlChar*) XMLTAG_MACRO_BANK)) {
            vj_macro_load_bank( ptr, doc, cur->xmlChildrenNode );
            macro->current_bank ++;
            vj_macro_select( ptr, macro->current_bank );
        }
        cur = cur->next;
    }
    macro->current_bank = 0;
}

#endif

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
	vvm_[VIMS_DEL_MACRO] = 0;
	vvm_[VIMS_PUT_MACRO] = 0;
	vvm_[VIMS_QUIT] = 0;
}

int vj_macro_is_vims_accepted(int net_id)
{
    if(net_id > 400 || net_id >= 388 || (net_id >= 80 && net_id <= 86) || (net_id >= 50 && net_id <= 59))
        return 0;

    return vvm_[net_id];
}

