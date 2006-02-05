 /*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/* todo: cleanup. lots bad things. */
#ifndef VJ_LIBLAVPLAY_H
#define VJ_LIBLAVPLAY_H
#include "vj-lib.h"

veejay_t *veejay_malloc();

void veejay_change_playback_mode(veejay_t *info, int pm, int sample);

int veejay_free(veejay_t *info);

void veejay_signal_loop(void *);

int veejay_init_editlist(veejay_t * info);

int veejay_init(veejay_t *info,int w, int h, char *arg, int td);

int veejay_open(veejay_t *info);

int veejay_open_files(veejay_t * info, char **files, int num_files, float fps, int force, int pixfmt,
	char norm);

int veejay_main(veejay_t * info);

int veejay_stop(veejay_t *info);

void veejay_quit(veejay_t *info);

int veejay_dummy_open(veejay_t * info,int ofps, char *file, int pixfmt);

int veejay_close(veejay_t *info);

void veejay_stop_sampling(veejay_t *info);

void veejay_set_sample(veejay_t *info, int sample);

int veejay_set_frame(veejay_t *info, long frame_num);

void veejay_change_state(veejay_t *info, int new_state);

int veejay_set_speed(veejay_t *info , int speed);

void veejay_busy(veejay_t *info);

int veejay_increase_frame(veejay_t * info, long numframes);

int veejay_create_tag(veejay_t * info, int type, char *filename,
			int index, int palette, int channel);

int veejay_set_framedup(veejay_t *info, int n);

int veejay_get_state(veejay_t *info); 

int veejay_create_sample(veejay_t * info, long start, long end);

int veejay_edit_copy(veejay_t *info, editlist *el, long start, long end);

int veejay_edit_delete(veejay_t *info, editlist *el,  long start, long end);

int veejay_edit_cut(veejay_t * info, editlist *el, long start, long end);

int veejay_edit_paste(veejay_t * info, editlist *el, long destination);

int veejay_edit_move(veejay_t * info, editlist *el , long start, long end,long dest);

int veejay_edit_addmovie(veejay_t * info, editlist *el, char *movie, long start,
			  long end, long destination);
int veejay_edit_addmovie_sample(veejay_t * info, char *movie , int new_s);

int veejay_edit_set_playable(veejay_t * info, long start, long end);

void veejay_set_sampling(veejay_t *info, subsample_mode_t m);


int veejay_toggle_audio(veejay_t * info, int audio);

int veejay_save_selection(veejay_t * info, char *filename, long start,long end);

int veejay_save_all(veejay_t * info, char *filename, long n1, long n2);

int vj_server_setup(veejay_t *info);

void veejay_default_tags(veejay_t *info); 

void veejay_loop_count(veejay_t *info);

editlist *veejay_edit_copy_to_new(veejay_t * info, editlist *el, long start, long end);
#endif
