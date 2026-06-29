/* 
 * veejay  
 *
 * Copyright (C) 2000-2026 Niels Elburg <nwelburg@gmail.com>
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
#ifndef VJ_CALI_H
#define VJ_CALI_H

#include <stdint.h>
#include <libstream/vj-tag.h>

#define VJ_CALI_DARK   0
#define VJ_CALI_LIGHT  1
#define VJ_CALI_FLAT   2
#define VJ_CALI_MFLAT  3
#define VJ_CALI_BUF    4

#define V4L_WHITEFRAME         4
#define V4L_WHITEFRAME_NEXT    5
#define V4L_WHITEFRAME_PROCESS 6

int      vj_cali_tag_new(vj_tag *tag, int stream_nr, int w, int h);
void     vj_cali_tag_free(int stream_nr);
void     vj_cali_free_capture(vj_tag *tag);
int      vj_cali_get_frame(vj_tag *tag, VJFrame *dst, int *args);
void     vj_cali_process_frame(vj_tag *tag, uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int *args);
uint8_t *vj_cali_get(vj_tag *tag, int type, int len, int uv_len);

#define VJ_CALI_PARAM_COUNT       2
#define VJ_CALI_STREAM_EFFECT_ID  190
#define VJ_CALI_MODE_CORRECT      0
#define VJ_CALI_MODE_DARK         1
#define VJ_CALI_MODE_LIGHT        2
#define VJ_CALI_MODE_FLAT         3

void     vj_cali_default_args(int *args);
int      vj_cali_set_args(vj_tag *tag, const int *args);
int      vj_cali_get_args(vj_tag *tag, int *args, int *n_args, int *fx_id);
uint8_t *vj_tag_get_cali_buffer(int t1, int type, int *total, int *plane, int *planeuv);
int      vj_tag_cali_write_file(int t1, char *name, editlist *el);
uint8_t *vj_tag_get_cali_data(int t1, int what);
int      vj_tag_has_cali_fx(int t1);
void     vj_tag_cali_prepare_now(vj_tag *tag);
void     vj_tag_cali_prepare(int t1, int pos, int cali_tag);
int      vj_tag_grab_blackframe(int t1, int duration, int median_radius, int mode);
int      vj_tag_drop_blackframe(int t1);

#endif
