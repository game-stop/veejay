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

#ifndef VJ_MACRO_H
#define VJ_MACRO_H

#define MACRO_STOP 0
#define MACRO_REC 1
#define MACRO_PLAY 2
#define MACRO_DESTROY 3

#define XMLTAG_MACRO_BANK "macrobank"
#define XMLTAG_MACRO "macro"
#define XMLTAG_MACRO_MESSAGES "list"
#define XMLTAG_MACRO_MSG "msg"
#define XMLTAG_MACRO_KEY "key"
#define XMLTAG_MACRO_LOOP_STAT_STOP "macro_ends_at_loop"
#define XMLTAG_MACRO_STATUS "status"

void *vj_macro_new(void);
void vj_macro_free(void *ptr);
void vj_macro_set_status(void *ptr, uint8_t status);
uint8_t vj_macro_get_status(void *ptr);
char **vj_macro_pull(void *ptr, long frame_num, int at_dup, int at_loop);
int vj_macro_put(void *ptr, char *message, long frame_num, int at_dup, int at_loop);
void vj_macro_clear(void *ptr);
int vj_macro_select( void *ptr, int slot );
void vj_macro_init(void);
int vj_macro_is_vims_accepted(int vims_id);
int vj_macro_get_loop_stat_stop( void *ptr );
int vj_macro_set_loop_stat_stop( void *ptr, int stop);
void vj_macro_clear_bank(void *ptr, int bank);
void vj_macro_del(void *ptr, long frame_num, int at_dup, int at_loop, int seq_no);
char* vj_macro_serialize_macro(void *ptr, long frame_num, int at_dup, int at_loop );
char *vj_macro_serialize(void *ptr);
char **vj_macro_play_event(void *ptr, char *key );
void vj_macro_finish_event(void *ptr, char *key);
void vj_macro_get_key( long frame_num, int at_dup, int at_loop, char *key, size_t key_len);
#ifdef HAVE_XML2
void vj_macro_load( void *ptr, xmlDocPtr doc, xmlNodePtr cur);
void vj_macro_store( void *ptr, xmlNodePtr node );
#endif
#endif
