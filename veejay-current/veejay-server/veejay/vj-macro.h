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

void *vj_macro_new(void);
void vj_macro_free(void *ptr);
void vj_macro_set_status(void *ptr, uint8_t status);
uint8_t vj_macro_get_status(void *ptr);
char **vj_macro_pull(void *ptr, long frame_num, int at_loop, int at_dup);
int vj_macro_put(void *ptr, char *message, long frame_num, int at_loop, int at_dup);
void vj_macro_clear(void *ptr);
int vj_macro_select( void *ptr, int slot );
void vj_macro_init(void);
int vj_macro_is_vims_accepted(int vims_id);
#endif
